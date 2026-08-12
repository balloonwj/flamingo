/**
 * Http会话类, HttpSession.cpp
 * zhangyl 2018.05.16
 */
#include "HttpSession.h"
#include <sstream>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <vector>
#include <set>
#include "../net/EventLoopThread.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../utils/StringUtil.h"
#include "../utils/URLEncodeUtil.h"
#include "../jsoncpp1.9.0/json.h"
#include "Msg.h"
#include "ChatServer.h"
#include "UserManager.h"
#include "BussinessLogic.h"
#include "AdminManager.h"
#include "ClientVersionManager.h"
#include "UpdatePackageScanner.h"
#include "EnterpriseManager.h"
#include "DepartmentManager.h"
#include "DepartmentMsg.h"
#include "EmployeeManager.h"
#include "AnnouncementManager.h"
#include "ServiceAccountManager.h"

//被踢下线原因，与 Protocol.h 的 enum kickout_reason 逐条同值。本文件属 Msg.h 世界、
//引不进 Protocol.h（两者的 msg_type / error_code 枚举冲突），而这些值要作为参数交给
//ChatServer::kickUserSessions / kickEnterpriseSessions（Protocol.h 世界）下发，故在此
//显式对齐取值。改 Protocol.h 里那些枚举值时，这里必须同步。
#define ADMIN_KICKOUT_REASON_ACCOUNT_DISABLED     2
#define ADMIN_KICKOUT_REASON_ENTERPRISE_DISABLED  6
#define ADMIN_KICKOUT_REASON_ENTERPRISE_DELETED   7
#define ADMIN_KICKOUT_REASON_ACCOUNT_DELETED      8
#define ADMIN_KICKOUT_REASON_KICKED_BY_ADMIN      9

//HTTP 头部最大长度，超过仍未出现 \r\n\r\n 视为非法请求。
#define MAX_HTTP_HEADER_LENGTH   8192
//POST 请求体最大长度（1MB），足够容纳公告正文等较大的 admin 请求。
#define MAX_HTTP_BODY_LENGTH     (1024 * 1024)

//把 JSON 文本解析为 Json::Value，成功返回 true。
static bool ParseJsonBody(const std::string& body, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    JSONCPP_STRING errs;
    bool ok = reader->parse(body.c_str(), body.c_str() + body.length(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑（无缩进）JSON 文本。
static std::string DumpJson(const Json::Value& root)
{
    Json::StreamWriterBuilder builder;
    builder.settings_["indentation"] = "";
    return Json::writeString(builder, root);
}

HttpSession::HttpSession(std::shared_ptr<TcpConnection>& conn) : m_tmpConn(conn)
{

}

void HttpSession::onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime)
{
    //短连接 HTTP：每次 onRead 都把缓冲区里已收到的全部数据取出（peek，不消费），
    //数据未收全时直接返回，等待网络层继续收取后再次回调。
    std::string inbuf;
    inbuf.append(pBuffer->peek(), pBuffer->readableBytes());
    if (inbuf.length() <= 4)
        return;

    //定位 HTTP 头部结束标志 \r\n\r\n；未出现说明头部尚未收全。
    size_t headerEnd = inbuf.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        //头部超长仍未结束，视为非法请求。
        if (inbuf.length() >= MAX_HTTP_HEADER_LENGTH)
            conn->forceClose();
        return;
    }

    //解析请求行：METHOD target HTTP/x.x
    std::string headerPart = inbuf.substr(0, headerEnd);
    std::vector<std::string> lines;
    StringUtil::split(headerPart, lines, "\r\n");
    if (lines.empty() || lines[0].empty())
    {
        conn->forceClose();
        return;
    }
    std::vector<std::string> chunk;
    StringUtil::split(lines[0], chunk, " ");
    //请求行至少为：方法 + 目标 + HTTP版本
    if (chunk.size() < 3)
    {
        conn->forceClose();
        return;
    }
    const std::string& method = chunk[0];
    const std::string& target = chunk[1];
    LOGI("http request: %s %s from %s", method.c_str(), target.c_str(), conn->peerAddress().toIpPort().c_str());

    std::string url;
    std::string param;
    if (method == "GET")
    {
        //GET：参数位于 url 的 ?p= 之后（既有 register.do 等接口）。
        std::vector<std::string> part;
        StringUtil::split(target, part, "?");
        if (part.size() < 2)
        {
            conn->forceClose();
            return;
        }
        url = part[0];
        param = part[1].substr(2);   //去掉前导的 "p="
    }
    else if (method == "POST")
    {
        //POST：url 即 target，参数为请求体；需按 Content-Length 收全请求体。
        url = target;
        int contentLength = 0;
        for (size_t i = 1; i < lines.size(); ++i)
        {
            //不区分大小写匹配 "Content-Length:"（共15个字符）。
            if (lines[i].length() <= 15)
                continue;
            std::string name = lines[i].substr(0, 15);
            for (size_t k = 0; k < name.size(); ++k)
                name[k] = (char)tolower((unsigned char)name[k]);
            if (name == "content-length:")
            {
                contentLength = atoi(lines[i].c_str() + 15);
                break;
            }
        }
        if (contentLength < 0 || contentLength > MAX_HTTP_BODY_LENGTH)
        {
            conn->forceClose();
            return;
        }
        //请求体尚未收全，返回等待后续数据。
        size_t bodyStart = headerEnd + 4;
        if (inbuf.length() < bodyStart + (size_t)contentLength)
            return;
        param = inbuf.substr(bodyStart, (size_t)contentLength);
    }
    else
    {
        //仅支持 GET / POST。
        conn->forceClose();
        return;
    }

    if (!process(conn, url, param))
    {
        LOGE("handle http request error, url: %s, from: %s", url.c_str(), conn->peerAddress().toIpPort().c_str());
    }

    //短连接，处理完关闭连接
    conn->forceClose();
}

void HttpSession::send(const char* data, size_t length)
{
    if (!m_tmpConn.expired())
    {
        std::shared_ptr<TcpConnection> conn = m_tmpConn.lock();
        conn->send(data, length);
    }
}

bool HttpSession::process(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& param)
{
    if (url.empty())
        return false;

    //admin 控制台接口统一以 /admin/ 开头，单独分发。
    if (url.compare(0, 7, "/admin/") == 0)
        return processAdminRequest(conn, url, param);

    if (url == "/register.do")
    {
        onRegisterResponse(param, conn);
    }
    else if (url == "/login.do")
    {
        onLoginResponse(param, conn);
    }
    else if (url == "/getgroupmembers.do")
    {

    }
    else
        return false;

    return true;
}

bool HttpSession::processAdminRequest(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& body)
{
    //登录接口无需 token；其余接口各自在处理器内校验 token。
    if (url == "/admin/login.do")
        onAdminLogin(body, conn);
    else if (url == "/admin/logout.do")
        onAdminLogout(body, conn);
    else if (url == "/admin/profile.do")
        onAdminProfile(body, conn);
    else if (url == "/admin/changepassword.do")
        onAdminChangePassword(body, conn);
    else if (url == "/admin/updateprofile.do")
        onAdminUpdateProfile(body, conn);
    else if (url == "/admin/dashboard/stats.do")
        onAdminDashboardStats(body, conn);
    else if (url == "/admin/dashboard/messagetrend.do")
        onAdminDashboardMessageTrend(body, conn);
    else if (url == "/admin/user/messagestats.do")
        onAdminUserMessageStats(body, conn);
    else if (url == "/admin/enterprise/list.do")
        onAdminEnterpriseList(body, conn);
    else if (url == "/admin/enterprise/detail.do")
        onAdminEnterpriseDetail(body, conn);
    else if (url == "/admin/enterprise/create.do")
        onAdminEnterpriseCreate(body, conn);
    else if (url == "/admin/enterprise/update.do")
        onAdminEnterpriseUpdate(body, conn);
    else if (url == "/admin/enterprise/setstatus.do")
        onAdminEnterpriseSetStatus(body, conn);
    else if (url == "/admin/enterprise/export.do")
        onAdminEnterpriseExport(body, conn);
    else if (url == "/admin/enterprise/delete.do")
        onAdminEnterpriseDelete(body, conn);
    else if (url == "/admin/enterprise/admins.do")
        onAdminEnterpriseAdmins(body, conn);
    else if (url == "/admin/enterprise/addadmin.do")
        onAdminEnterpriseAddAdmin(body, conn);
    else if (url == "/admin/enterprise/removeadmin.do")
        onAdminEnterpriseRemoveAdmin(body, conn);
    else if (url == "/admin/dept/tree.do")
        onAdminDeptTree(body, conn);
    else if (url == "/admin/dept/detail.do")
        onAdminDeptDetail(body, conn);
    else if (url == "/admin/dept/create.do")
        onAdminDeptCreate(body, conn);
    else if (url == "/admin/dept/update.do")
        onAdminDeptUpdate(body, conn);
    else if (url == "/admin/dept/move.do")
        onAdminDeptMove(body, conn);
    else if (url == "/admin/dept/delete.do")
        onAdminDeptDelete(body, conn);
    else if (url == "/admin/dept/reorder.do")
        onAdminDeptReorder(body, conn);
    else if (url == "/admin/user/list.do")
        onAdminUserList(body, conn);
    else if (url == "/admin/user/detail.do")
        onAdminUserDetail(body, conn);
    else if (url == "/admin/user/create.do")
        onAdminUserCreate(body, conn);
    else if (url == "/admin/user/update.do")
        onAdminUserUpdate(body, conn);
    else if (url == "/admin/user/setstatus.do")
        onAdminUserSetStatus(body, conn);
    else if (url == "/admin/user/delete.do")
        onAdminUserDelete(body, conn);
    else if (url == "/admin/user/move.do")
        onAdminUserMove(body, conn);
    else if (url == "/admin/user/loginrecords.do")
        onAdminUserLoginRecords(body, conn);
    else if (url == "/admin/user/activity.do")
        onAdminUserActivity(body, conn);
    else if (url == "/admin/user/kickdevice.do")
        onAdminUserKickDevice(body, conn);
    else if (url == "/admin/user/resetpassword.do")
        onAdminUserResetPassword(body, conn);
    else if (url == "/admin/user/setrole.do")
        onAdminUserSetRole(body, conn);
    else if (url == "/admin/user/getpermissions.do")
        onAdminUserGetPermissions(body, conn);
    else if (url == "/admin/user/setpermissions.do")
        onAdminUserSetPermissions(body, conn);
    else if (url == "/admin/announcement/list.do")
        onAdminAnnouncementList(body, conn);
    else if (url == "/admin/announcement/detail.do")
        onAdminAnnouncementDetail(body, conn);
    else if (url == "/admin/announcement/save.do")
        onAdminAnnouncementSave(body, conn);
    else if (url == "/admin/announcement/send.do")
        onAdminAnnouncementSend(body, conn);
    else if (url == "/admin/announcement/delete.do")
        onAdminAnnouncementDelete(body, conn);
    else if (url == "/admin/announcement/recall.do")
        onAdminAnnouncementRecall(body, conn);
    else if (url == "/admin/announcement/setpinned.do")
        onAdminAnnouncementSetPinned(body, conn);
    else if (url == "/admin/audit/list.do")
        onAdminAuditList(body, conn);
    else if (url == "/admin/audit/export.do")
        onAdminAuditExport(body, conn);
    else if (url == "/admin/audit/stats.do")
        onAdminAuditStats(body, conn);
    else if (url == "/admin/service/list.do")
        onAdminServiceList(body, conn);
    else if (url == "/admin/service/detail.do")
        onAdminServiceDetail(body, conn);
    else if (url == "/admin/service/save.do")
        onAdminServiceSave(body, conn);
    else if (url == "/admin/service/delete.do")
        onAdminServiceDelete(body, conn);
    else if (url == "/admin/service/setbindings.do")
        onAdminServiceSetBindings(body, conn);
    else if (url == "/admin/service/setstatus.do")
        onAdminServiceSetStatus(body, conn);
    else if (url == "/admin/group/create.do")
        onAdminGroupCreate(body, conn);
    else if (url == "/admin/clientversion/scanpackages.do")
        onAdminClientVersionScanPackages(body, conn);
    else if (url == "/admin/clientversion/list.do")
        onAdminClientVersionList(body, conn);
    else if (url == "/admin/clientversion/create.do")
        onAdminClientVersionCreate(body, conn);
    else if (url == "/admin/clientversion/update.do")
        onAdminClientVersionUpdate(body, conn);
    else if (url == "/admin/clientversion/setenabled.do")
        onAdminClientVersionSetEnabled(body, conn);
    else if (url == "/admin/clientversion/republish.do")
        onAdminClientVersionRepublish(body, conn);
    else
        sendAdminResult(conn, error_code_unknown, "admin endpoint not implemented yet");

    return true;
}

//组装http协议应答包
/*
    HTTP/1.1 200 OK\r\n
    Content-Type: text/html\r\n
    Content-Length: 4864\r\n
    \r\n
    {"code": 0, "msg": "ok"}
*/
void HttpSession::makeupResponse(const std::string& input, std::string& output, const std::string& contentType)
{
    std::ostringstream os;
    os << "HTTP/1.1 200 OK\r\nContent-Type: " << contentType << "\r\nContent-Length:"
        << input.length() << "\r\n\r\n"
        << input;

    output = os.str();
}

void HttpSession::sendJsonResponse(const std::shared_ptr<TcpConnection>& conn, const std::string& json)
{
    std::string response;
    makeupResponse(json, response);
    conn->send(response);
}

void HttpSession::sendAdminResult(const std::shared_ptr<TcpConnection>& conn, int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    sendJsonResponse(conn, DumpJson(root));
}

void HttpSession::onRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    std::string retData;
    std::string decodeData;
    URLEncodeUtil::decode(data, decodeData);
    BussinessLogic::registerUser(decodeData, conn, false, retData);
    if (!retData.empty())
    {
        std::string response;
        URLEncodeUtil::encode(retData, response);
        makeupResponse(retData, response);
        conn->send(response);

        LOGI("Response to client: cmd=msg_type_register, data: %s, client: %s", retData.c_str(), conn->peerAddress().toIpPort().c_str());
    }
}

void HttpSession::onLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{

}

//==== admin 控制台接口处理器 ====

void HttpSession::onAdminLogin(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string email = root["email"].isString() ? root["email"].asString() : "";
    std::string password = root["password"].isString() ? root["password"].asString() : "";
    if (email.empty() || password.empty())
    {
        sendAdminResult(conn, error_code_unknown, "email or password missing");
        return;
    }

    std::string ip = conn->peerAddress().toIpPort();
    std::string token;
    AdminInfo admin;
    int32_t code = Singleton<AdminManager>::Instance().login(email, password, ip, token, admin);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "login failed");
        LOGI("admin login failed, email: %s, code: %d", email.c_str(), code);
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["token"] = token;
    Json::Value a;
    a["adminid"] = static_cast<Json::Int64>(admin.adminid);
    a["name"] = admin.name;
    a["email"] = admin.email;
    a["enterpriseid"] = static_cast<Json::Int64>(admin.enterpriseid);
    resp["admin"] = a;
    sendJsonResponse(conn, DumpJson(resp));
    LOGI("admin login ok, email: %s, adminid: %lld", email.c_str(), (long long)admin.adminid);
}

void HttpSession::onAdminLogout(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    if (!token.empty())
        Singleton<AdminManager>::Instance().logout(token);

    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value a;
    a["adminid"] = static_cast<Json::Int64>(admin.adminid);
    a["name"] = admin.name;
    a["email"] = admin.email;
    a["enterpriseid"] = static_cast<Json::Int64>(admin.enterpriseid);
    resp["admin"] = a;
    //平台超管 enterpriseid 为 0，其余为企业级管理员。
    resp["scope"] = (admin.enterpriseid == 0) ? "platform" : "enterprise";
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminChangePassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    std::string oldPwd = root["oldpassword"].isString() ? root["oldpassword"].asString() : "";
    std::string newPwd = root["newpassword"].isString() ? root["newpassword"].asString() : "";
    int32_t code = Singleton<AdminManager>::Instance().changePassword(admin.adminid, oldPwd, newPwd);
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "change password failed");
}

void HttpSession::onAdminUpdateProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    std::string name = root["name"].isString() ? root["name"].asString() : "";
    int32_t code = Singleton<AdminManager>::Instance().updateProfile(admin.adminid, name);
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "update profile failed");
}

void HttpSession::onAdminDashboardStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管返回全平台口径；企业级管理员返回其所辖企业口径。
    DashboardStats st;
    Singleton<AdminManager>::Instance().getDashboardStats(admin.enterpriseid, st);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value s;
    s["enterprises_total"] = st.enterprisesTotal;
    s["enterprises_active"] = st.enterprisesActive;
    s["enterprises_pending"] = st.enterprisesPending;
    s["users_total"] = st.usersTotal;
    s["audit_today"] = st.auditToday;
    s["audit_today_warn"] = st.auditTodayWarn;
    resp["stats"] = s;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminDashboardMessageTrend(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管返回全平台口径；企业级管理员返回其所辖企业口径。
    std::vector<MessageTrendPoint> trend;
    Singleton<AdminManager>::Instance().getMessageTrend(admin.enterpriseid, trend);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < trend.size(); ++i)
    {
        Json::Value pt;
        pt["date"]  = trend[i].date;
        pt["count"] = trend[i].count;
        arr.append(pt);
    }
    resp["trend"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserMessageStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : "";
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = root["userid"].isIntegral() ? root["userid"].asInt64() : 0;
    UserMessageStats st;
    Singleton<AdminManager>::Instance().getUserMessageStats(userId, st);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value s;
    s["sent"]          = st.sent;
    s["received"]      = st.received;
    s["conversations"] = st.conversations;
    resp["stats"] = s;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < st.trend.size(); ++i)
    {
        Json::Value pt;
        pt["date"]  = st.trend[i].date;
        pt["count"] = st.trend[i].count;
        arr.append(pt);
    }
    resp["trend"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

//==== 企业管理模块辅助函数 ====

//安全取字符串字段：不存在或类型不符返回空串。
static std::string JStr(const Json::Value& root, const char* key)
{
    return root[key].isString() ? root[key].asString() : std::string();
}

//安全取整型字段：不存在或类型不符返回默认值。
static int64_t JInt(const Json::Value& root, const char* key, int64_t defVal)
{
    return root[key].isIntegral() ? root[key].asInt64() : defVal;
}

//校验请求体里的 token，有效则填充 outAdmin 并返回 true。
static bool AdminCheckToken(const Json::Value& root, AdminInfo& outAdmin)
{
    std::string token = root[ "token" ].isString() ? root["token"].asString() : "";
    return Singleton<AdminManager>::Instance().checkToken(token, outAdmin);
}

//把 EnterpriseInfo 转为应答用的 JSON 对象。
static Json::Value EnterpriseToJson(const EnterpriseInfo& e)
{
    Json::Value je;
    je["enterpriseid"]      = static_cast<Json::Int64>(e.enterpriseId);
    je["name"]              = e.name;
    je["shortname"]         = e.shortName;
    je["industry"]          = e.industry;
    je["region"]            = e.region;
    je["plan"]              = e.plan;
    je["status"]            = e.status;
    je["adminname"]         = e.adminName;
    je["adminemail"]        = e.adminEmail;
    je["logocolor"]         = e.logoColor;
    je["opendate"]          = e.openDate;
    je["licenseexpiretime"] = e.licenseExpireTime;
    je["createtime"]        = e.createTime;
    //公司根部门ID：管理端据此把组织架构树上的这一个节点认作"公司本身"，
    //从而允许直接往公司下挂员工，并禁止删除该节点。0 表示该企业尚未建立根部门。
    je["rootdeptid"]        = static_cast<Json::Int64>(e.rootDeptId);
    je["members"]           = e.memberCount;
    je["depts"]             = e.deptCount;
    return je;
}

//==== 企业管理模块处理器（均要求平台超级管理员）====

void HttpSession::onAdminEnterpriseList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    std::string keyword = JStr(root, "keyword");
    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));

    int32_t total = 0;
    std::vector<EnterpriseInfo> list;
    Singleton<EnterpriseManager>::Instance().listEnterprises(keyword, status, page, pageSize, total, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
        arr.append(EnterpriseToJson(list[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminEnterpriseDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    EnterpriseInfo info;
    if (!Singleton<EnterpriseManager>::Instance().getEnterprise(JInt(root, "enterpriseid", 0), info))
    {
        sendAdminResult(conn, error_code_unknown, "enterprise not found");
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["enterprise"] = EnterpriseToJson(info);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminEnterpriseCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    EnterpriseInfo req;
    req.name       = JStr(root, "name");
    req.shortName  = JStr(root, "shortname");
    req.industry   = JStr(root, "industry");
    req.region     = JStr(root, "region");
    req.plan       = static_cast<int32_t>(JInt(root, "plan", 0));
    req.adminName  = JStr(root, "adminname");
    req.adminEmail = JStr(root, "adminemail");

    int64_t entId = 0;
    int64_t adminId = 0;
    std::string initPwd;
    int32_t code = Singleton<EnterpriseManager>::Instance().createEnterprise(req, entId, adminId, initPwd);
    if (code != error_code_ok)
    {
        //重名要单独给一句能看懂的提示：企业全称现在是唯一的，且 PC 客户端注册时
        //正是靠它定位企业，管理员必须知道"改个名字重试"而不是以为服务出了故障。
        if (code == error_code_enterprise_nameexist)
            sendAdminResult(conn, code, "enterprise name already exists");
        else
            sendAdminResult(conn, code, "create enterprise failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), entId, "enterprise.create", req.name, 1, u8"新增企业");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["enterpriseid"] = static_cast<Json::Int64>(entId);
    resp["adminid"] = static_cast<Json::Int64>(adminId);
    resp["initialpassword"] = initPwd;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminEnterpriseUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    EnterpriseInfo req;
    req.enterpriseId = JInt(root, "enterpriseid", 0);
    req.name      = JStr(root, "name");
    req.shortName = JStr(root, "shortname");
    req.industry  = JStr(root, "industry");
    req.region    = JStr(root, "region");
    req.plan      = static_cast<int32_t>(JInt(root, "plan", 0));

    int32_t code = Singleton<EnterpriseManager>::Instance().updateEnterprise(req);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), req.enterpriseId, "enterprise.update", req.name, 1, u8"修改企业信息");
        //企业名称会显示在客户端通讯录树的根节点上（cmd_get_contacts 应答的 enterprise.name），
        //改了不通知的话同事端根节点会一直挂着旧名字。归为部门结构变更：改的是树的根那一层，
        //不是某个人。deptid 传 0，客户端整棵重拉、不依赖该字段。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            req.enterpriseId, dept::org_change_department, 0);
    }
    //与新建一样，重名单独给提示，其余失败沿用笼统文案。
    const char* failMsg = "update enterprise failed";
    if (code == error_code_enterprise_nameexist)
        failMsg = "enterprise name already exists";
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : failMsg);
}

void HttpSession::onAdminEnterpriseSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t entId = JInt(root, "enterpriseid", 0);
    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    int32_t code = Singleton<EnterpriseManager>::Instance().setEnterpriseStatus(entId, status);
    if (code == error_code_ok)
    {
        //停用（2）后把该企业所有在线会话踢下线，使停用立即生效 —— 与「停用员工」
        //对单个账号的处理一致。登录侧的拦截由 ChatSession::onLogin 的企业状态校验
        //负责，两者配合才是完整的「停用 = 停服」：踢掉已在线的，挡住再登录的。
        //改回活跃（1）/ 待审核（0）不踢人：前者本就该恢复使用，后者不是从活跃态
        //出发的正常流转。
        if (status == kEnterpriseStatusDisabled)
        {
            const int32_t kicked = Singleton<ChatServer>::Instance().kickEnterpriseSessions(
                entId, ADMIN_KICKOUT_REASON_ENTERPRISE_DISABLED);
            LOGI("onAdminEnterpriseSetStatus: enterprise %lld disabled, kicked %d sessions",
                 (long long)entId, kicked);
        }
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), entId, "enterprise.setstatus", "-", 1, u8"变更企业状态");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set status failed");
}

void HttpSession::onAdminEnterpriseExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    std::string keyword = JStr(root, "keyword");
    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    std::string csv;
    Singleton<EnterpriseManager>::Instance().exportEnterprisesCsv(keyword, status, csv);

    //CSV 应答，Content-Type 用 text/csv。
    std::string response;
    makeupResponse(csv, response, "text/csv;charset=utf-8");
    conn->send(response);
}

void HttpSession::onAdminEnterpriseDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t entId = JInt(root, "enterpriseid", 0);

    //踢人必须在删库【之前】：kickEnterpriseSessions 靠 getUserEnterpriseId 把在线
    //会话归到企业上，而 deleteEnterprise 会删掉部门数据并清掉那份缓存，删完再踢
    //一个都匹配不到。代价是删库失败时人已经被踢了 —— 相比「删成功却留着一批
    //拿着已注销企业身份的在线会话」，这个方向的代价更小，且被踢的人重连即可。
    const int32_t kicked = Singleton<ChatServer>::Instance().kickEnterpriseSessions(
        entId, ADMIN_KICKOUT_REASON_ENTERPRISE_DELETED);

    int32_t code = Singleton<EnterpriseManager>::Instance().deleteEnterprise(entId);
    if (code == error_code_ok)
    {
        LOGI("onAdminEnterpriseDelete: enterprise %lld deleted, kicked %d sessions beforehand",
             (long long)entId, kicked);
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), entId, "enterprise.delete", "-", 2, u8"硬删除企业");
    }
    else
    {
        //删库失败：上面已经踢掉的人重连后仍属原企业，功能不受影响，只是被打断一次。
        LOGE("onAdminEnterpriseDelete: delete failed after kicking %d sessions, enterpriseid: %lld, code: %d",
             kicked, (long long)entId, code);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "delete enterprise failed");
}

void HttpSession::onAdminEnterpriseAdmins(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t entId = JInt(root, "enterpriseid", 0);
    std::vector<AdminListItem> list;
    Singleton<AdminManager>::Instance().listAdmins(entId, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        Json::Value ja;
        ja["adminid"] = static_cast<Json::Int64>(list[i].adminid);
        ja["name"] = list[i].name;
        ja["email"] = list[i].email;
        ja["status"] = list[i].status;
        ja["lastlogintime"] = list[i].lastLoginTime;
        arr.append(ja);
    }
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminEnterpriseAddAdmin(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t entId = JInt(root, "enterpriseid", 0);
    std::string name = JStr(root, "name");
    std::string email = JStr(root, "email");
    int64_t adminId = 0;
    std::string initPwd;
    int32_t code = Singleton<AdminManager>::Instance().createAdmin(entId, name, email, adminId, initPwd);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "add admin failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), entId, "enterprise.addadmin", email, 1, u8"新增企业管理员");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["adminid"] = static_cast<Json::Int64>(adminId);
    resp["initialpassword"] = initPwd;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminEnterpriseRemoveAdmin(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t targetAdminId = JInt(root, "adminid", 0);
    //不允许把自己删掉。
    if (targetAdminId == admin.adminid)
    {
        sendAdminResult(conn, error_code_unknown, "cannot remove yourself");
        return;
    }

    int32_t code = Singleton<AdminManager>::Instance().removeAdmin(targetAdminId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), 0, "enterprise.removeadmin", "-", 2, u8"移除企业管理员");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "remove admin failed");
}

//==== 组织架构模块处理器 ====
//
//与企业模块（仅平台超管可用）不同，组织架构接口同时面向平台超管与企业级管理员：
//企业级管理员（admin.enterpriseid != 0）只能查改自己企业的部门，越权返回 116；
//平台超管（admin.enterpriseid == 0）可操作任意企业的部门。

void HttpSession::onAdminDeptTree(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 取树；企业级管理员强制只能查自己企业。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    std::vector<dept::DepartmentInfo> depts;
    deptMgr.getOrgStructure(enterpriseId, depts);

    //成员在线状态在部门缓存中未填充，这里按 userid 实时补齐，与 cmd 1200 口径一致。
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    for (size_t i = 0; i < depts.size(); ++i)
    {
        for (size_t j = 0; j < depts[i].members.size(); ++j)
        {
            dept::DepartmentMember& m = depts[i].members[j];
            m.status = imserver.getUserStatusByUserId((int32_t)m.userid);
            m.clienttype = imserver.getUserClientTypeByUserId((int32_t)m.userid);
        }
    }

    //departments 数组结构与 cmd 1200 完全一致，直接复用其应答拼装函数。
    sendJsonResponse(conn, dept::BuildGetOrgStructureResp(error_code_ok, "ok", depts));
}

void HttpSession::onAdminDeptDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t deptId = JInt(root, "deptid", 0);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();

    //租户越权校验：企业级管理员只能查自己企业的部门。
    if (admin.enterpriseid != 0 &&
        deptMgr.getDepartmentEnterpriseId(deptId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    DepartmentDetail d;
    if (!deptMgr.getDepartmentDetail(deptId, d))
    {
        sendAdminResult(conn, error_code_dept_notexist, "department not found");
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value jd;
    jd["deptid"]        = static_cast<Json::Int64>(d.deptid);
    jd["enterpriseid"]  = static_cast<Json::Int64>(d.enterpriseid);
    jd["deptname"]      = d.deptname;
    jd["parentdeptid"]  = static_cast<Json::Int64>(d.parentdeptid);
    jd["deptlevel"]     = d.deptlevel;
    jd["deptorder"]     = d.deptorder;
    jd["leaderuserid"]  = static_cast<Json::Int64>(d.leaderuserid);
    jd["leadername"]    = d.leadername;
    jd["deptdesc"]      = d.deptdesc;
    jd["directmembers"] = d.directMembers;
    jd["subdepts"]      = d.subDepts;
    jd["totalmembers"]  = d.totalMembers;
    resp["department"] = jd;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminDeptCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 建部门；企业级管理员强制建在自己企业。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;
    if (enterpriseId == 0)
    {
        sendAdminResult(conn, error_code_unknown, "enterpriseid required");
        return;
    }

    dept::OperateDepartmentReq req;
    req.parentdeptid = JInt(root, "parentdeptid", 0);
    req.deptname     = JStr(root, "deptname");
    req.deptdesc     = JStr(root, "deptdesc");
    req.leaderuserid = JInt(root, "leaderuserid", 0);
    req.deptorder    = static_cast<int32_t>(JInt(root, "deptorder", 0));

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    //指定了父部门时，父部门必须与目标企业同租户，防止把子部门挂到别的企业下。
    if (req.parentdeptid != 0 &&
        deptMgr.getDepartmentEnterpriseId(req.parentdeptid) != enterpriseId)
    {
        sendAdminResult(conn, error_code_dept_invalidparent, "parent dept not in this enterprise");
        return;
    }

    int64_t newDeptId = 0;
    int32_t code = deptMgr.createDepartment(enterpriseId, req, newDeptId);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "create department failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "dept.create", req.deptname, 1, u8"新增部门");

    //部门结构已变更，通知该企业在线 IM 客户端刷新通讯录
    Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
        enterpriseId, dept::org_change_department, newDeptId);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["deptid"] = static_cast<Json::Int64>(newDeptId);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminDeptUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t deptId = JInt(root, "deptid", 0);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int64_t deptEnt = deptMgr.getDepartmentEnterpriseId(deptId);
    if (admin.enterpriseid != 0 && deptEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    dept::OperateDepartmentReq req;
    req.deptid       = deptId;
    req.deptname     = JStr(root, "deptname");
    req.deptdesc     = JStr(root, "deptdesc");
    req.leaderuserid = JInt(root, "leaderuserid", 0);
    req.deptorder    = static_cast<int32_t>(JInt(root, "deptorder", 0));

    int32_t code = deptMgr.updateDepartment(req);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), deptEnt, "dept.update", req.deptname, 1, u8"修改部门");
        //部门结构已变更，通知该企业在线 IM 客户端刷新通讯录
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            deptEnt, dept::org_change_department, deptId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "update department failed");
}

void HttpSession::onAdminDeptMove(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t deptId = JInt(root, "deptid", 0);
    int64_t newParentDeptId = JInt(root, "newparentdeptid", 0);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int64_t deptEnt = deptMgr.getDepartmentEnterpriseId(deptId);
    if (admin.enterpriseid != 0 && deptEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //不允许跨企业移动：新父部门必须与被移动部门同属一个企业。
    if (newParentDeptId != 0 &&
        deptMgr.getDepartmentEnterpriseId(newParentDeptId) != deptEnt)
    {
        sendAdminResult(conn, error_code_dept_invalidparent, "cross-enterprise move not allowed");
        return;
    }

    int32_t code = deptMgr.moveDepartment(deptId, newParentDeptId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), deptEnt, "dept.move", "-", 1, u8"移动部门");
        //部门结构已变更，通知该企业在线 IM 客户端刷新通讯录
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            deptEnt, dept::org_change_department, deptId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "move department failed");
}

void HttpSession::onAdminDeptDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t deptId = JInt(root, "deptid", 0);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int64_t deptEnt = deptMgr.getDepartmentEnterpriseId(deptId);
    if (admin.enterpriseid != 0 && deptEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //deleteDepartment 内部已校验其下无在用子部门与在职成员，否则返回 112。
    int32_t code = deptMgr.deleteDepartment(deptId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), deptEnt, "dept.delete", "-", 2, u8"删除部门");
        //部门结构已变更，通知该企业在线 IM 客户端刷新通讯录
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            deptEnt, dept::org_change_department, deptId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "delete department failed");
}

void HttpSession::onAdminDeptReorder(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t parentDeptId = JInt(root, "parentdeptid", 0);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();

    //指定了父部门时，企业级管理员的父部门必须属于本企业。
    if (parentDeptId != 0 && admin.enterpriseid != 0 &&
        deptMgr.getDepartmentEnterpriseId(parentDeptId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //收集 orderedids 数组中的整型部门ID，下标即新排序值。
    std::vector<int64_t> orderedIds;
    const Json::Value& arr = root["orderedids"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (arr[i].isIntegral())
                orderedIds.push_back(arr[i].asInt64());
        }
    }

    //企业级管理员的重排范围被锁定在本企业；平台超管按显式给定的部门ID重排，不加企业过滤。
    int32_t code = deptMgr.reorderDepartments(admin.enterpriseid, parentDeptId, orderedIds);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), admin.enterpriseid, "dept.reorder", "-", 1, u8"重排部门顺序");
        //部门结构已变更，通知该企业在线 IM 客户端刷新通讯录
        //（平台超管 admin.enterpriseid 为 0 时 broadcast 内部直接返回、不推送）
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            admin.enterpriseid, dept::org_change_department, parentDeptId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "reorder departments failed");
}

//==== 员工/用户模块处理器 ====
//
//与组织架构模块一样同时面向平台超管与企业级管理员：企业级管理员只能查改
//本企业员工，越权返回 116；平台超管可操作任意企业的员工。

//把 EmployeeDetail 转为应答用的 JSON 对象。
static Json::Value EmployeeDetailToJson(const EmployeeDetail& e)
{
    Json::Value ju;
    ju["userid"]       = static_cast<Json::Int64>(e.userid);
    ju["enterpriseid"] = static_cast<Json::Int64>(e.enterpriseid);
    ju["username"]     = e.username;
    ju["name"]         = e.name;
    ju["title"]        = e.title;
    ju["deptid"]       = static_cast<Json::Int64>(e.deptid);
    ju["deptname"]     = e.deptname;
    ju["role"]         = e.role;
    ju["status"]       = e.status;
    ju["employeeno"]   = e.employeeNo;
    ju["joindate"]     = e.joinDate;
    ju["landline"]     = e.landline;
    ju["workstation"]  = e.workstation;
    ju["phone"]        = e.phone;
    ju["email"]        = e.email;
    ju["signature"]    = e.signature;
    ju["address"]      = e.address;
    ju["gender"]       = e.gender;
    ju["birthday"]     = static_cast<Json::Int64>(e.birthday);
    ju["registertime"] = e.registerTime;
    return ju;
}

//踢掉某用户的在线会话；clientTypeFilter<=0 表示全部设备，否则只踢该客户端类型。
//reason 为下发给客户端的被踢原因（见 ADMIN_KICKOUT_REASON_* 常量），返回实际踢下
//线的会话数。
//
//实际工作委托给 ChatServer::kickUserSessions —— 组 cmd_kickout 报文要用 Protocol.h
//的 cmd 与 reason 取值，本文件属 Msg.h 世界、引不进 Protocol.h。历史上这里是自己
//遍历会话并 forceClose 的，那样客户端只会当成网络断线去自动重连，看不到下线提示、
//也不退回登录窗（2026-08-11 现场：删企业后客户端自动重连）。
static int32_t KickUserSessions(int64_t userId, int32_t clientTypeFilter, int32_t reason)
{
    return Singleton<ChatServer>::Instance().kickUserSessions(userId, clientTypeFilter, reason);
}

//把形如 "7d" / "30d" / "all" 的时间窗口字符串解析为天数；"all"、空串、无效值返回 0（不限）。
static int32_t ParseRangeDays(const std::string& range)
{
    if (range.empty() || range == "all")
        return 0;

    //取前导数字部分作为天数。
    int32_t days = 0;
    size_t i = 0;
    for (; i < range.size() && range[i] >= '0' && range[i] <= '9'; ++i)
        days = days * 10 + (range[i] - '0');
    return (days > 0) ? days : 0;
}

void HttpSession::onAdminUserList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 过滤；企业级管理员强制只能查本企业。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;

    int64_t deptId = JInt(root, "deptid", 0);
    std::string keyword = JStr(root, "keyword");
    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));

    int32_t total = 0;
    std::vector<EmployeeListItem> list;
    Singleton<EmployeeManager>::Instance().listEmployees(enterpriseId, deptId, keyword,
                                                         page, pageSize, total, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        Json::Value ji;
        ji["userid"]   = static_cast<Json::Int64>(list[i].userid);
        ji["name"]     = list[i].name;
        ji["title"]    = list[i].title;
        ji["deptname"] = list[i].deptname;
        ji["role"]     = list[i].role;
        ji["status"]   = list[i].status;
        arr.append(ji);
    }
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();

    //租户越权校验：企业级管理员只能查本企业员工。
    if (admin.enterpriseid != 0 &&
        empMgr.getEmployeeEnterpriseId(userId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    EmployeeDetail detail;
    if (!empMgr.getEmployeeDetail(userId, detail))
    {
        sendAdminResult(conn, error_code_notregister, "employee not found");
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["user"] = EmployeeDetailToJson(detail);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 建员工；企业级管理员强制建在本企业。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;
    if (enterpriseId == 0)
    {
        sendAdminResult(conn, error_code_unknown, "enterpriseid required");
        return;
    }

    CreateEmployeeReq req;
    req.enterpriseId = enterpriseId;
    req.deptId       = JInt(root, "deptid", 0);
    req.name         = JStr(root, "name");
    req.title        = JStr(root, "title");
    req.employeeNo   = JStr(root, "employeeno");
    req.email        = JStr(root, "email");
    req.phone        = JStr(root, "phone");
    req.joinDate     = JStr(root, "joindate");

    //未指定部门时落到该企业的公司根部门，语义即"直接挂在公司下"。
    //这里必须给出确定的挂载部门：历史实现在 deptid 为 0 时会静默跳过下面的入部门
    //步骤，导致员工建出来却不属于任何部门，在管理端组织架构与客户端通讯录里都看不见。
    if (req.deptId == 0)
    {
        req.deptId = Singleton<EnterpriseManager>::Instance().getRootDeptId(enterpriseId);
        if (req.deptId == 0)
        {
            //企业尚无公司根部门（存量数据迁移之前）时无法确定挂载位置。宁可明确
            //拒绝并告知原因，也不要再建出无部门归属的孤儿账号。
            LOGE("onAdminUserCreate: enterprise %lld has no root department, reject creating employee",
                 (long long)enterpriseId);
            sendAdminResult(conn, error_code_dept_notexist, "enterprise has no root department");
            return;
        }
    }

    //部门必须属于目标企业，避免把员工挂到别的企业的部门。
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    if (deptMgr.getDepartmentEnterpriseId(req.deptId) != enterpriseId)
    {
        sendAdminResult(conn, error_code_dept_notexist, "dept not in this enterprise");
        return;
    }

    int64_t newUserId = 0;
    std::string initPwd;
    std::string newUsername;
    int32_t code = Singleton<EmployeeManager>::Instance().createEmployee(
        req, newUserId, initPwd, newUsername);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "create employee failed");
        return;
    }

    //员工建好后入部门（同时刷新其部门缓存）。req.deptId 在上面已保证非 0：
    //要么是调用方指定的部门，要么是该企业的公司根部门。
    {
        dept::OperateDeptMemberReq mreq;
        mreq.deptid      = req.deptId;
        mreq.userid      = newUserId;
        mreq.position    = req.title;
        mreq.isprimary   = 1;   //新建员工的首个部门即其主部门
        mreq.memberorder = 0;
        int32_t mcode = deptMgr.addMember(mreq);
        if (mcode != error_code_ok)
        {
            //入部门失败不回滚员工创建：员工已建好，记日志由管理员后续手工归部门。
            LOGE("onAdminUserCreate: addMember failed, userid: %lld, deptid: %lld, code: %d",
                 (long long)newUserId, (long long)req.deptId, mcode);
        }
        else
        {
            //新成员已入部门，通知该企业在线 IM 客户端刷新通讯录
            Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
                enterpriseId, dept::org_change_member, req.deptId);
        }
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "user.create", req.name, 1, u8"新增员工");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["userid"] = static_cast<Json::Int64>(newUserId);
    resp["username"] = newUsername;
    resp["initialpassword"] = initPwd;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    UpdateEmployeeReq req;
    req.userId      = userId;
    req.name        = JStr(root, "name");
    req.title       = JStr(root, "title");
    req.employeeNo  = JStr(root, "employeeno");
    req.joinDate    = JStr(root, "joindate");
    req.landline    = JStr(root, "landline");
    req.workstation = JStr(root, "workstation");
    req.phone       = JStr(root, "phone");
    req.email       = JStr(root, "email");
    req.signature   = JStr(root, "signature");

    int32_t code = empMgr.updateEmployee(req);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.update", req.name, 1, u8"修改员工信息");
        //姓名/职务等资料已变更，通知该企业在线 IM 客户端刷新通讯录，免得同事端要重登才看到。
        //deptid 传 0：本次改的是"人"而不是某个部门，客户端收到后整棵通讯录重拉，不依赖该字段。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, 0);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "update employee failed");
}

void HttpSession::onAdminUserSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t code = empMgr.setEmployeeStatus(userId, status);
    if (code == error_code_ok)
    {
        //停用后顺带把该员工已在线的会话踢下线，使停用立即生效。下发
        //「账号被停用」原因，客户端据此弹提示并退回登录窗；再登录时会被
        //onLogin 的账号停用校验以 error_code_account_disabled 挡住。
        if (status == 1)
            KickUserSessions(userId, 0, ADMIN_KICKOUT_REASON_ACCOUNT_DISABLED);
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.setstatus",
            (status == 1) ? "disable" : "enable", 1, u8"启用/停用员工");
        //账号启停会改变该员工在同事通讯录里的呈现，通知本企业在线 IM 客户端刷新。
        //被停用者自己已在上面踢下线，收不到也不影响。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, 0);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set status failed");
}

void HttpSession::onAdminUserDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t code = empMgr.deleteEmployee(userId);
    if (code == error_code_ok)
    {
        //删除后把该员工的在线会话一并踢下线。原因用「账号已删除」而非「已停用」：
        //前者不可恢复，客户端据此提示用户并清掉本机的自动登录标志，免得他每次
        //开机都被弹一次登录失败。
        KickUserSessions(userId, 0, ADMIN_KICKOUT_REASON_ACCOUNT_DELETED);
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.delete", "-", 2, u8"删除员工");
        //成员已移出企业，通知该企业在线 IM 客户端刷新通讯录
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, 0);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "delete employee failed");
}

void HttpSession::onAdminUserMove(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId    = JInt(root, "userid", 0);
    int64_t deptId    = JInt(root, "deptid", 0);
    int64_t newDeptId = JInt(root, "newdeptid", 0);

    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //不允许把员工迁到别的企业的部门。
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    if (deptMgr.getDepartmentEnterpriseId(newDeptId) != empEnt)
    {
        sendAdminResult(conn, error_code_dept_notexist, "target dept not in this enterprise");
        return;
    }

    int32_t code = deptMgr.moveMember(deptId, userId, newDeptId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.move", "-", 1, u8"调整员工部门");
        //成员部门归属已变更，通知该企业在线 IM 客户端刷新通讯录
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, newDeptId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "move employee failed");
}

void HttpSession::onAdminUserLoginRecords(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    if (admin.enterpriseid != 0 &&
        empMgr.getEmployeeEnterpriseId(userId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));
    int32_t total = 0;
    std::vector<LoginRecordItem> records;
    empMgr.listLoginRecords(userId, page, pageSize, total, records);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;

    //devices：该用户当前仍在线的会话（实时取自 ChatServer），区别于历史登录记录。
    Json::Value devices(Json::arrayValue);
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(sessions, static_cast<int32_t>(userId));
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin();
         it != sessions.end(); ++it)
    {
        if (!*it)
            continue;
        Json::Value jd;
        jd["clienttype"] = (*it)->getClientType();   //1=PC 2=android/ios
        jd["status"]     = (*it)->getUserStatus();
        devices.append(jd);
    }
    resp["devices"] = devices;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < records.size(); ++i)
    {
        Json::Value jr;
        jr["ts"]         = records[i].ts;
        jr["device"]     = records[i].device;
        jr["ip"]         = records[i].ip;
        jr["location"]   = records[i].location;
        jr["result"]     = records[i].result;
        jr["failreason"] = records[i].failReason;
        arr.append(jr);
    }
    resp["records"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserActivity(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    if (admin.enterpriseid != 0 &&
        empMgr.getEmployeeEnterpriseId(userId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //range 形如 "7d"/"30d"/"all"；type 暂统一按全部处理（操作类别尚未细分）。
    int32_t rangeDays = ParseRangeDays(JStr(root, "range"));
    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));

    int32_t total = 0;
    std::vector<ActivityItem> list;
    empMgr.listActivities(userId, rangeDays, page, pageSize, total, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        Json::Value ja;
        ja["ts"]       = list[i].ts;
        ja["action"]   = list[i].action;
        ja["detail"]   = list[i].detail;
        ja["severity"] = list[i].severity;
        arr.append(ja);
    }
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserKickDevice(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //deviceid 为空踢全部设备；为客户端类型数字（1=PC 2=移动端）时只踢该类设备。
    std::string deviceId = JStr(root, "deviceid");
    int32_t clientTypeFilter = deviceId.empty() ? 0 : atoi(deviceId.c_str());
    //管理员在控制台强制注销设备，与「本人在另一台设备上踢掉本设备」
    //（kickout_reason_kicked_by_user）是两回事，故用单独的原因码，让客户端能给出
    //「被管理员注销」而不是「你自己踢的」这种驴唇不对马嘴的提示。
    int32_t kicked = KickUserSessions(userId, clientTypeFilter,
                                      ADMIN_KICKOUT_REASON_KICKED_BY_ADMIN);

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), empEnt, "user.kickdevice", "-", 1, u8"强制注销员工设备");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["kicked"] = kicked;   //实际踢下线的会话数
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserResetPassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    std::string newPwd;
    int32_t code = empMgr.resetPassword(userId, newPwd);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "reset password failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), empEnt, "user.resetpassword", "-", 2, u8"重置员工密码");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["newpassword"] = newPwd;
    sendJsonResponse(conn, DumpJson(resp));
}

//==== 角色与权限模块处理器 ====
//
//接口挂在 /admin/user/ 前缀下，作用对象仍是员工，鉴权与租户隔离规则同员工模块：
//企业级管理员只能操作本企业员工，越权返回 116。

void HttpSession::onAdminUserSetRole(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    int32_t role = static_cast<int32_t>(JInt(root, "role", -1));
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //setRole 内部会连带清空该员工的逐人权限覆盖。
    int32_t code = empMgr.setRole(userId, role);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.setrole", "-", 1, u8"设置员工角色");
        //角色（普通成员/部门负责人/企业管理员）影响该员工在通讯录里的呈现，通知本企业在线客户端刷新。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, 0);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set role failed");
}

void HttpSession::onAdminUserGetPermissions(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    if (admin.enterpriseid != 0 &&
        empMgr.getEmployeeEnterpriseId(userId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t role = 0;
    std::vector<PermissionItem> perms;
    if (!empMgr.getPermissions(userId, role, perms))
    {
        sendAdminResult(conn, error_code_notregister, "employee not found");
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["role"] = role;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < perms.size(); ++i)
    {
        Json::Value jp;
        jp["group"]       = perms[i].group;
        jp["key"]         = perms[i].key;
        jp["label"]       = perms[i].label;
        jp["roledefault"] = perms[i].roleDefault;
        jp["overridden"]  = perms[i].overridden;
        jp["effective"]   = perms[i].effective;
        arr.append(jp);
    }
    resp["permissions"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminUserSetPermissions(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t userId = JInt(root, "userid", 0);
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    int64_t empEnt = empMgr.getEmployeeEnterpriseId(userId);
    if (admin.enterpriseid != 0 && empEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //收集 perms 数组里每一项的 (权限键名, 是否授予)。
    std::vector<std::pair<std::string, bool> > perms;
    const Json::Value& arr = root["perms"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& it = arr[i];
            if (!it.isObject() || !it["key"].isString())
                continue;
            bool granted = it["granted"].isBool() ? it["granted"].asBool()
                                                  : (it["granted"].isIntegral() && it["granted"].asInt() != 0);
            perms.push_back(std::make_pair(it["key"].asString(), granted));
        }
    }

    int32_t code = empMgr.setPermissions(userId, perms);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.setpermissions", "-", 1, u8"设置员工权限");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set permissions failed");
}

//==== 平台公告模块处理器（均要求平台超级管理员）====
//
//公告是平台级运营能力（受众可跨企业、可面向全部企业），故与企业模块一样
//只对平台超管（admin.enterpriseid == 0）开放，企业级管理员一律拒绝。

//安全取布尔字段：支持 JSON 布尔与整型两种写法，缺失时返回 false。
static bool JBool(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    if (v.isBool())
        return v.asBool();
    if (v.isIntegral())
        return v.asInt() != 0;
    return false;
}

//把 AnnouncementInfo 的摘要字段转为 JSON 对象（list 与 detail 共用）。
static Json::Value AnnouncementSummaryToJson(const AnnouncementInfo& a)
{
    Json::Value j;
    j["announcementid"] = static_cast<Json::Int64>(a.announcementId);
    j["title"]          = a.title;
    //正文也随列表下发：控制台列表项要画一行摘要，详情面板首次打开也靠它显示正文。
    //detail.do 在本函数之后会再赋一次同值，无害。
    j["body"]           = a.body;
    j["severity"]       = a.severity;
    j["audiencetype"]   = a.audienceType;
    j["status"]         = a.status;
    j["pinned"]         = a.pinned;
    j["scheduled"]      = a.scheduled;
    j["scheduledtime"]  = a.scheduledTime;
    j["senttime"]       = a.sentTime;
    j["sendername"]     = a.senderName;
    j["createtime"]     = a.createTime;
    j["readcount"]      = a.readCount;
    return j;
}

void HttpSession::onAdminAnnouncementList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));

    int32_t total = 0;
    std::vector<AnnouncementInfo> list;
    Singleton<AnnouncementManager>::Instance().listAnnouncements(status, page, pageSize, total, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
        arr.append(AnnouncementSummaryToJson(list[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminAnnouncementDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    AnnouncementInfo info;
    if (!Singleton<AnnouncementManager>::Instance().getAnnouncement(JInt(root, "announcementid", 0), info))
    {
        sendAdminResult(conn, error_code_unknown, "announcement not found");
        return;
    }

    Json::Value ja = AnnouncementSummaryToJson(info);
    ja["body"]          = info.body;
    ja["senderadminid"] = static_cast<Json::Int64>(info.senderAdminId);
    ja["updatetime"]    = info.updateTime;
    ja["totalcount"]    = info.totalCount;
    Json::Value targets(Json::arrayValue);
    for (size_t i = 0; i < info.targets.size(); ++i)
    {
        Json::Value jt;
        jt["type"] = info.targets[i].type;
        jt["id"]   = static_cast<Json::Int64>(info.targets[i].id);
        targets.append(jt);
    }
    ja["targets"] = targets;

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["announcement"] = ja;
    sendJsonResponse(conn, DumpJson(resp));
}

//从请求 JSON 的 targets 数组解析投放目标集；每元素形如 {"type":N,"id":N}。
static void ParseAnnouncementTargets(const Json::Value& root, std::vector<AnnouncementTarget>& out)
{
    const Json::Value& arr = root["targets"];
    if (!arr.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& it = arr[i];
        if (!it.isObject())
            continue;
        AnnouncementTarget t;
        t.type = it["type"].isIntegral() ? it["type"].asInt() : 0;
        t.id   = it["id"].isIntegral() ? it["id"].asInt64() : 0;
        out.push_back(t);
    }
}

void HttpSession::onAdminAnnouncementSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    SaveAnnouncementReq req;
    req.announcementId = JInt(root, "announcementid", 0);
    req.title          = JStr(root, "title");
    req.body           = JStr(root, "body");
    req.severity       = static_cast<int32_t>(JInt(root, "severity", 0));
    req.audienceType   = static_cast<int32_t>(JInt(root, "audiencetype", 0));
    req.pinned         = JBool(root, "pinned") ? 1 : 0;
    ParseAnnouncementTargets(root, req.targets);

    int64_t outId = 0;
    int32_t code = Singleton<AnnouncementManager>::Instance().saveAnnouncement(
        req, admin.adminid, admin.name, outId);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "save announcement failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0, "announcement.save", req.title, 1, u8"保存公告");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["announcementid"] = static_cast<Json::Int64>(outId);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminAnnouncementSend(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t annId = JInt(root, "announcementid", 0);
    bool scheduled = JBool(root, "scheduled");
    std::string scheduledTime = JStr(root, "scheduledtime");
    int32_t code = Singleton<AnnouncementManager>::Instance().sendAnnouncement(annId, scheduled, scheduledTime);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), 0, "announcement.send",
            scheduled ? "scheduled" : "now", 1, u8"发布公告");

        //立即发布：把新公告推给在线受众，他们的「消息盒子」即时亮起角标。
        //定时发布这里<u>不</u>推 —— 此刻公告仍是草稿状态，要等后台调度器到点把它转为
        //已发布时才推（见 main.cpp 里注册的定时公告调度器）。
        if (!scheduled)
            Singleton<ChatServer>::Instance().broadcastAnnouncementPublished(annId);
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "send announcement failed");
}

void HttpSession::onAdminAnnouncementDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t annId = JInt(root, "announcementid", 0);
    int32_t code = Singleton<AnnouncementManager>::Instance().deleteAnnouncement(annId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), 0, "announcement.delete", "-", 2, u8"删除公告");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "delete announcement failed");
}

void HttpSession::onAdminAnnouncementRecall(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t annId = JInt(root, "announcementid", 0);
    //走 ChatServer 而不是直接调 AnnouncementManager::recallAnnouncement：撤回要连带把
    //「已撤回」通知推给在线受众，而受众必须在状态改掉<u>之前</u>筛出来，两步的顺序由
    //recallAnnouncementAndNotify 保证。返回值语义与原来一致。
    int32_t code = Singleton<ChatServer>::Instance().recallAnnouncementAndNotify(annId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), 0, "announcement.recall", "-", 1, u8"撤回公告");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "recall announcement failed");
}

void HttpSession::onAdminAnnouncementSetPinned(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t annId = JInt(root, "announcementid", 0);
    int32_t pinned = JBool(root, "pinned") ? 1 : 0;
    int32_t code = Singleton<AnnouncementManager>::Instance().setPinned(annId, pinned);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), 0, "announcement.setpinned",
            pinned ? "pin" : "unpin", 0, u8"切换公告置顶");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set pinned failed");
}

//==== 审计日志模块处理器 ====
//
//审计接口同时面向平台超管与企业级管理员：平台超管可按请求 enterpriseid 过滤
//（0=全部），企业级管理员被强制只能看本企业的审计记录。

//从请求 JSON 解析审计过滤条件；调用方再据管理员身份决定是否强制企业过滤。
static AuditQuery ParseAuditQuery(const Json::Value& root)
{
    AuditQuery q;
    q.actorType    = static_cast<int32_t>(JInt(root, "actortype", -1));
    q.action       = JStr(root, "action");
    q.enterpriseId = JInt(root, "enterpriseid", 0);
    q.severity     = static_cast<int32_t>(JInt(root, "severity", -1));
    q.startTime    = JStr(root, "starttime");
    q.endTime      = JStr(root, "endtime");
    return q;
}

void HttpSession::onAdminAuditList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    AuditQuery query = ParseAuditQuery(root);
    //企业级管理员只能看本企业的审计记录，强制覆盖企业过滤项。
    if (admin.enterpriseid != 0)
        query.enterpriseId = admin.enterpriseid;

    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 50));

    int32_t total = 0;
    std::vector<AuditLogItem> list;
    Singleton<AdminManager>::Instance().queryAuditLogs(query, page, pageSize, total, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        Json::Value jl;
        jl["id"]           = static_cast<Json::Int64>(list[i].id);
        jl["ts"]           = list[i].ts;
        jl["actortype"]    = list[i].actorType;
        jl["actorname"]    = list[i].actorName;
        jl["ip"]           = list[i].ip;
        jl["enterpriseid"] = static_cast<Json::Int64>(list[i].enterpriseId);
        jl["action"]       = list[i].action;
        jl["target"]       = list[i].target;
        jl["severity"]     = list[i].severity;
        jl["detail"]       = list[i].detail;
        arr.append(jl);
    }
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminAuditExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    AuditQuery query = ParseAuditQuery(root);
    if (admin.enterpriseid != 0)
        query.enterpriseId = admin.enterpriseid;

    std::string csv;
    Singleton<AdminManager>::Instance().exportAuditLogsCsv(query, csv);

    //CSV 应答，Content-Type 用 text/csv。
    std::string response;
    makeupResponse(csv, response, "text/csv;charset=utf-8");
    conn->send(response);
}

//审计统计聚合：供 admin 控制台「审计日志」界面顶部 4 个统计卡使用。
//恒为全局（平台超管）或本企业（企业级管理员）概览，不接受任何筛选参数。
void HttpSession::onAdminAuditStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //作用域：平台超管（enterpriseid==0）统计全表，企业级管理员只统计本企业。
    //与 audit/list.do 保持一致——此处把管理员所辖企业ID直接作为统计范围。
    int32_t scopeEnterpriseId = static_cast<int32_t>(admin.enterpriseid);

    int32_t today = 0;
    int32_t total = 0;
    int32_t warn = 0;
    int32_t danger = 0;
    Singleton<AdminManager>::Instance().getAuditStats(scopeEnterpriseId, today, total, warn, danger);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value stats;
    stats["today"] = today;
    stats["total"] = total;
    stats["warn"] = warn;
    stats["danger"] = danger;
    resp["stats"] = stats;
    sendJsonResponse(conn, DumpJson(resp));
}

//==== 服务号模块处理器 ====
//
//服务号归属企业，同时面向平台超管与企业级管理员：企业级管理员只能操作本企业
//的服务号，越权返回 116。

//把 ServiceAccountInfo 的摘要字段转为 JSON 对象（list 与 detail 共用）。
static Json::Value ServiceSummaryToJson(const ServiceAccountInfo& s)
{
    Json::Value j;
    j["serviceid"]        = static_cast<Json::Int64>(s.serviceId);
    j["enterpriseid"]     = static_cast<Json::Int64>(s.enterpriseId);
    j["name"]             = s.name;
    j["kind"]             = s.kind;
    j["description"]      = s.description;
    j["avatarcolor"]      = s.avatarColor;
    j["avatarglyph"]      = s.avatarGlyph;
    j["owneradminid"]     = static_cast<Json::Int64>(s.ownerAdminId);
    j["allowunsubscribe"] = s.allowUnsubscribe;
    j["status"]           = s.status;
    j["createtime"]       = s.createTime;
    return j;
}

void HttpSession::onAdminServiceList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 列出；企业级管理员强制只能看本企业。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;

    std::vector<ServiceAccountInfo> list;
    Singleton<ServiceAccountManager>::Instance().listServices(enterpriseId, list);

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
        arr.append(ServiceSummaryToJson(list[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminServiceDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t serviceId = JInt(root, "serviceid", 0);
    ServiceAccountManager& svcMgr = Singleton<ServiceAccountManager>::Instance();
    if (admin.enterpriseid != 0 &&
        svcMgr.getServiceEnterpriseId(serviceId) != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    ServiceAccountInfo info;
    if (!svcMgr.getService(serviceId, info))
    {
        sendAdminResult(conn, error_code_unknown, "service not found");
        return;
    }

    Json::Value js = ServiceSummaryToJson(info);
    Json::Value bindings(Json::arrayValue);
    for (size_t i = 0; i < info.bindings.size(); ++i)
    {
        Json::Value jb;
        jb["bindtype"] = info.bindings[i].bindType;
        jb["targetid"] = static_cast<Json::Int64>(info.bindings[i].targetId);
        bindings.append(jb);
    }
    js["bindings"] = bindings;

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["service"] = js;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminServiceSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    SaveServiceReq req;
    req.serviceId        = JInt(root, "serviceid", 0);
    req.name             = JStr(root, "name");
    req.kind             = static_cast<int32_t>(JInt(root, "kind", 0));
    req.description      = JStr(root, "description");
    req.avatarColor      = JStr(root, "avatarcolor");
    req.allowUnsubscribe = JBool(root, "allowunsubscribe") ? 1 : 0;

    ServiceAccountManager& svcMgr = Singleton<ServiceAccountManager>::Instance();
    if (req.serviceId == 0)
    {
        //新建：企业ID取请求值；企业级管理员强制建在本企业。
        req.enterpriseId = JInt(root, "enterpriseid", 0);
        if (admin.enterpriseid != 0)
            req.enterpriseId = admin.enterpriseid;
        if (req.enterpriseId == 0)
        {
            sendAdminResult(conn, error_code_unknown, "enterpriseid required");
            return;
        }
    }
    else
    {
        //编辑：服务号必须属于本企业（企业级管理员）。
        if (admin.enterpriseid != 0 &&
            svcMgr.getServiceEnterpriseId(req.serviceId) != admin.enterpriseid)
        {
            sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
            return;
        }
    }

    int64_t outId = 0;
    int32_t code = svcMgr.saveService(req, admin.adminid, outId);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "save service failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), svcMgr.getServiceEnterpriseId(outId),
        "service.save", req.name, 1, u8"保存服务号");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["serviceid"] = static_cast<Json::Int64>(outId);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminServiceDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t serviceId = JInt(root, "serviceid", 0);
    ServiceAccountManager& svcMgr = Singleton<ServiceAccountManager>::Instance();
    int64_t svcEnt = svcMgr.getServiceEnterpriseId(serviceId);
    if (admin.enterpriseid != 0 && svcEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t code = svcMgr.deleteService(serviceId);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), svcEnt, "service.delete", "-", 2, u8"删除服务号");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "delete service failed");
}

void HttpSession::onAdminServiceSetBindings(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t serviceId = JInt(root, "serviceid", 0);
    ServiceAccountManager& svcMgr = Singleton<ServiceAccountManager>::Instance();
    int64_t svcEnt = svcMgr.getServiceEnterpriseId(serviceId);
    if (admin.enterpriseid != 0 && svcEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    //收集 bindings 数组里每一项的 (绑定类型, 目标ID)。
    std::vector<ServiceBinding> bindings;
    const Json::Value& arr = root["bindings"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& it = arr[i];
            if (!it.isObject())
                continue;
            ServiceBinding b;
            b.bindType = it["bindtype"].isIntegral() ? it["bindtype"].asInt() : 0;
            b.targetId = it["targetid"].isIntegral() ? it["targetid"].asInt64() : 0;
            bindings.push_back(b);
        }
    }

    int32_t code = svcMgr.setBindings(serviceId, bindings);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), svcEnt, "service.setbindings", "-", 1, u8"设置服务号可见范围");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set bindings failed");
}

void HttpSession::onAdminServiceSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    int64_t serviceId = JInt(root, "serviceid", 0);
    int32_t status = static_cast<int32_t>(JInt(root, "status", -1));
    ServiceAccountManager& svcMgr = Singleton<ServiceAccountManager>::Instance();
    int64_t svcEnt = svcMgr.getServiceEnterpriseId(serviceId);
    if (admin.enterpriseid != 0 && svcEnt != admin.enterpriseid)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "permission denied");
        return;
    }

    int32_t code = svcMgr.setStatus(serviceId, status);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), svcEnt, "service.setstatus",
            (status == 1) ? "disable" : "enable", 1, u8"启用/停用服务号");
    }
    sendAdminResult(conn, code, code == error_code_ok ? "ok" : "set status failed");
}

void HttpSession::onAdminGroupCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }

    //平台超管按请求 enterpriseid 建群；企业级管理员强制建在自己企业，防越权。
    int64_t enterpriseId = JInt(root, "enterpriseid", 0);
    if (admin.enterpriseid != 0)
        enterpriseId = admin.enterpriseid;
    if (enterpriseId == 0)
    {
        sendAdminResult(conn, error_code_unknown, "enterpriseid required");
        return;
    }

    //群协议与群存储属于 v2（Protocol.h）世界，交给 ChatServer 编排，避免本文件引入会与 Msg.h
    //冲突的 Protocol.h；这里只负责 token / 企业裁决 / 审计 / 应答（均用 Msg.h 的错误码）。
    int64_t newGroupId = 0;
    AdminCreateGroupOutcome outcome =
        Singleton<ChatServer>::Instance().adminCreateGroup(enterpriseId, body, newGroupId);
    switch (outcome)
    {
    case AdminCreateGroupOutcome::Ok:
        break;
    case AdminCreateGroupOutcome::BadParams:
        sendAdminResult(conn, error_code_unknown, "invalid group params");
        return;
    case AdminCreateGroupOutcome::OwnerNotInEnterprise:
        sendAdminResult(conn, error_code_dept_permissiondenied, "owner not in this enterprise");
        return;
    case AdminCreateGroupOutcome::AdminNotInEnterprise:
        sendAdminResult(conn, error_code_dept_permissiondenied, "admin not in this enterprise");
        return;
    case AdminCreateGroupOutcome::MemberNotInEnterprise:
        sendAdminResult(conn, error_code_dept_permissiondenied, "member not in this enterprise");
        return;
    case AdminCreateGroupOutcome::DbFail:
        sendAdminResult(conn, error_code_unknown, "create group failed");
        return;
    default:
        sendAdminResult(conn, error_code_unknown, "create group failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "group.create", JStr(root, "groupname"), 1, u8"新建群");

    //应答：返回新群ID（与 dept/user 等 admin 接口一致，id 以整型落入 JSON）
    Json::Value resp;
    resp["code"]    = 0;
    resp["msg"]     = "ok";
    resp["groupid"] = static_cast<Json::Int64>(newGroupId);
    sendJsonResponse(conn, DumpJson(resp));
}

// ===========================================================================
// admin 控制台接口：客户端版本模块
//
// 六个接口统一只对平台超管开放（admin.enterpriseid == 0）——客户端版本是平台级
// 资产，不属于任何一家企业。升级包本身由运维手工放进升级包目录，控制台只管元数据，
// 故本模块没有上传接口：scanpackages 把目录里的候选包连同大小与 MD5 列出来，
// 管理员从中挑一个，这两个值不需要人工输入。
//
// 注意错误码：本文件用的是 Msg.h 里的 error_code（v1 那一套，admin 接口文档即按它
// 编写），而 ClientVersionManager 返回的是 Protocol.h 里的 error_code。两套枚举不能
// 在同一个编译单元里共存，故这里<u>不转发</u> manager 的原始码，一律回
// error_code_unknown 并附一句能看懂的英文原因。
// ===========================================================================

//审计日志里客户端版本相关操作的严重级别。发版影响全体用户，按"需要留意"记，
//与企业创建等常规写操作同级。
static const int32_t CLIENT_VERSION_AUDIT_SEVERITY = 1;

void HttpSession::onAdminClientVersionScanPackages(const std::string& body,
                                                   const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    std::vector<UpdatePackageCandidate> candidates;
    Singleton<UpdatePackageScanner>::Instance().scan(candidates);

    //把已经建过版本记录的包标出来，免得管理员对同一个包重复建记录。
    //平台传 0 表示不过滤：同一个包理论上可以被不同平台的记录引用。
    std::set<std::string> usedMd5;
    std::vector<ClientVersionRecord> versions;
    if (Singleton<ClientVersionManager>::Instance().listVersions(0, versions))
    {
        for (size_t i = 0; i < versions.size(); ++i)
            usedMd5.insert(versions[i].packageMd5);
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    //把目录一并回给界面：管理员得知道该往哪儿放包
    resp["dir"] = Singleton<UpdatePackageScanner>::Instance().dir();
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        Json::Value item;
        item["filename"] = candidates[i].filename;
        item["size"] = static_cast<Json::Int64>(candidates[i].size);
        item["md5"] = candidates[i].md5;
        item["iszip"] = candidates[i].iszip;
        item["used"] = (usedMd5.find(candidates[i].md5) != usedMd5.end());
        arr.append(item);
    }
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminClientVersionList(const std::string& body,
                                           const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    //platform 取 0 或缺省表示不过滤，返回全部平台
    const int32_t platform = static_cast<int32_t>(JInt(root, "platform", 0));

    std::vector<ClientVersionRecord> list;
    if (!Singleton<ClientVersionManager>::Instance().listVersions(platform, list))
    {
        sendAdminResult(conn, error_code_unknown, "query client versions failed");
        return;
    }

    //标出每个平台当前真正生效的那一条：列表按发布时间倒序，故某平台第一条
    //f_enabled 为 1 的记录就是它。界面据此显示「当前生效」徽标。
    std::set<int32_t> currentMarked;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const ClientVersionRecord& rec = list[i];
        bool isCurrent = false;
        if (rec.enabled == 1 && currentMarked.find(rec.platform) == currentMarked.end())
        {
            isCurrent = true;
            currentMarked.insert(rec.platform);
        }

        Json::Value item;
        item["id"] = static_cast<Json::Int64>(rec.id);
        item["platform"] = rec.platform;
        item["version"] = rec.version;
        item["minversion"] = rec.minVersion;
        item["releasenote"] = rec.releaseNote;
        item["md5"] = rec.packageMd5;
        item["size"] = static_cast<Json::Int64>(rec.packageSize);
        item["packagename"] = rec.packageName;
        item["forced"] = rec.forced;
        item["enabled"] = rec.enabled;
        item["publishtime"] = static_cast<Json::Int64>(rec.publishTime);
        item["creatoradminid"] = static_cast<Json::Int64>(rec.creatorAdminId);
        item["createtime"] = static_cast<Json::Int64>(rec.createTime);
        item["iscurrent"] = isCurrent;
        arr.append(item);
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminClientVersionCreate(const std::string& body,
                                             const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    ClientVersionRecord rec;
    rec.platform       = static_cast<int32_t>(JInt(root, "platform", 0));
    rec.version        = JStr(root, "version");
    rec.minVersion     = JStr(root, "minversion");
    rec.releaseNote    = JStr(root, "releasenote");
    rec.packageMd5     = JStr(root, "md5");
    //强制升级开关。与 minversion 相互独立：前者是发布人的主观决定（这个版本必须装），
    //后者是版本门槛（低于它连登录都不放行），任一命中客户端即按强制处理。
    rec.forced         = JBool(root, "forced") ? 1 : 0;
    rec.enabled        = JBool(root, "publish") ? 1 : 0;
    rec.creatorAdminId = admin.adminid;

    if (rec.platform <= 0 || rec.version.empty() || rec.packageMd5.empty())
    {
        sendAdminResult(conn, error_code_unknown, "platform, version and md5 are required");
        return;
    }

    //包的大小与文件名一律以服务器扫描结果为准，不采信请求里传来的值——
    //两者一旦不一致，客户端下完校验必然失败，而那时已经很难查出是谁写错了。
    std::vector<UpdatePackageCandidate> candidates;
    Singleton<UpdatePackageScanner>::Instance().scan(candidates);
    const UpdatePackageCandidate* matched = NULL;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (candidates[i].md5 == rec.packageMd5)
        {
            matched = &candidates[i];
            break;
        }
    }
    if (matched == NULL)
    {
        sendAdminResult(conn, error_code_unknown,
                        "package not found in update dir, please rescan");
        return;
    }
    //最便宜的一道防呆：文件头都不是 zip，多半是放错了文件
    if (!matched->iszip)
    {
        sendAdminResult(conn, error_code_unknown, "selected file is not a zip package");
        return;
    }
    rec.packageSize = matched->size;
    rec.packageName = matched->filename;

    //发布时间必须取<u>真毫秒</u>：这一列是"哪条记录更新"的唯一判据，而 time(NULL)*1000
    //只有秒级精度——同一秒内先发布新版本、再回滚旧版本，两条记录的发布时间会一模一样，
    //随后按 f_id 兜底就会选中后插入的那条，回滚静默失效。
    const int64_t nowMs = Timestamp::now().microSecondsSinceEpoch() / 1000;
    int64_t newId = 0;
    int errorCode = 0;
    if (!Singleton<ClientVersionManager>::Instance().createVersion(rec, nowMs, newId, errorCode))
    {
        LOGE("create client version failed, version: %s, manager errorcode: %d",
             rec.version.c_str(), errorCode);
        sendAdminResult(conn, error_code_unknown, "create client version failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0, "clientversion.create", rec.version,
        CLIENT_VERSION_AUDIT_SEVERITY,
        rec.enabled == 1 ? u8"新建并发布客户端版本" : u8"新建客户端版本草稿");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["id"] = static_cast<Json::Int64>(newId);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminClientVersionUpdate(const std::string& body,
                                             const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    const int64_t id = JInt(root, "id", 0);
    if (id <= 0)
    {
        sendAdminResult(conn, error_code_unknown, "id is required");
        return;
    }

    int errorCode = 0;
    if (!Singleton<ClientVersionManager>::Instance().updateVersion(
            id, JStr(root, "minversion"), JStr(root, "releasenote"), errorCode))
    {
        LOGE("update client version failed, id: %lld, manager errorcode: %d",
             (long long)id, errorCode);
        sendAdminResult(conn, error_code_unknown, "update client version failed, check the id");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0, "clientversion.update",
        std::to_string(static_cast<long long>(id)),
        CLIENT_VERSION_AUDIT_SEVERITY, u8"修改客户端版本信息");

    sendAdminResult(conn, 0, "ok");
}

void HttpSession::onAdminClientVersionSetEnabled(const std::string& body,
                                                 const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    const int64_t id = JInt(root, "id", 0);
    if (id <= 0)
    {
        sendAdminResult(conn, error_code_unknown, "id is required");
        return;
    }
    const bool enabled = JBool(root, "enabled");

    //发布时间必须取<u>真毫秒</u>：这一列是"哪条记录更新"的唯一判据，而 time(NULL)*1000
    //只有秒级精度——同一秒内先发布新版本、再回滚旧版本，两条记录的发布时间会一模一样，
    //随后按 f_id 兜底就会选中后插入的那条，回滚静默失效。
    const int64_t nowMs = Timestamp::now().microSecondsSinceEpoch() / 1000;
    int errorCode = 0;
    if (!Singleton<ClientVersionManager>::Instance().setEnabled(id, enabled, nowMs, errorCode))
    {
        LOGE("set client version status failed, id: %lld, manager errorcode: %d",
             (long long)id, errorCode);
        sendAdminResult(conn, error_code_unknown, "set client version status failed, check the id");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0,
        enabled ? "clientversion.enable" : "clientversion.disable",
        std::to_string(static_cast<long long>(id)),
        CLIENT_VERSION_AUDIT_SEVERITY,
        enabled ? u8"发布客户端版本" : u8"撤回客户端版本");

    sendAdminResult(conn, 0, "ok");
}

void HttpSession::onAdminClientVersionRepublish(const std::string& body,
                                                const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return;
    }
    AdminInfo admin;
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return;
    }
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    const int64_t id = JInt(root, "id", 0);
    if (id <= 0)
    {
        sendAdminResult(conn, error_code_unknown, "id is required");
        return;
    }

    //数据层与"发布"是同一个动作：置为已发布并把发布时间刷到当前，使该条立刻成为
    //取数规则下的最新发布。分成两个接口是为了让审计日志能区分"首次发布"与"回滚"。
    //发布时间必须取<u>真毫秒</u>：这一列是"哪条记录更新"的唯一判据，而 time(NULL)*1000
    //只有秒级精度——同一秒内先发布新版本、再回滚旧版本，两条记录的发布时间会一模一样，
    //随后按 f_id 兜底就会选中后插入的那条，回滚静默失效。
    const int64_t nowMs = Timestamp::now().microSecondsSinceEpoch() / 1000;
    int errorCode = 0;
    if (!Singleton<ClientVersionManager>::Instance().setEnabled(id, true, nowMs, errorCode))
    {
        LOGE("republish client version failed, id: %lld, manager errorcode: %d",
             (long long)id, errorCode);
        sendAdminResult(conn, error_code_unknown, "republish client version failed, check the id");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0, "clientversion.republish",
        std::to_string(static_cast<long long>(id)),
        CLIENT_VERSION_AUDIT_SEVERITY, u8"回滚：重新发布旧版本客户端");

    sendAdminResult(conn, 0, "ok");
}
