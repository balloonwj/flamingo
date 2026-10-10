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
#include "AdminApiCodes.h"
#include "ChatServer.h"
#include "UserManager.h"
#include "BussinessLogic.h"
#include "AdminManager.h"
#include "ClientVersionManager.h"
#include "UpdatePackageScanner.h"
#include "EnterpriseManager.h"
#include "AccountName.h"   //NormalizeAccountPrefix：企业账号前缀的校验
#include "DepartmentManager.h"
#include "DepartmentMsg.h"
#include "EmployeeManager.h"
#include "AnnouncementManager.h"
#include "ServiceAccountManager.h"
#include "SensitiveWordManager.h"
#include "SensitiveAdminProtocol.h"
#include "GroupQaManager.h"          //企业问答的存取（头文件不含 Protocol.h）
#include "QaAdminProtocol.h"         //企业问答管理接口的纯逻辑
#include "FileShareAdminService.h"   //文件共享的管理后台接口（头文件不含 Protocol.h）
#include "FeatureSwitchManager.h"    //改设置后管理员操作日志的说明文字（头文件只依赖标准库）
#include "CloudRecordService.h"      //读设置时附带云端录像的已用空间（第 27 期 27g）
#include "AdminLinkServer.h"         //login.do 应答里的 linkport / linkpubkey（头文件不含 Protocol.h）
#include "BotManager.h"              //机器人的存取（头文件不含 Protocol.h）
#include "BotRules.h"                //机器人的校验与 JSON 编解码
#include "BotEngine.h"               //Webhook 与「试一试」
#include "HttpRequestParse.h"        //GET 目标的解析
#include <exception>

//被踢下线原因，与 Protocol.h 的 enum kickout_reason 逐条同值。本文件属 AdminApiCodes.h 世界、
//引不进 Protocol.h（两者的 msg_type / error_code 枚举冲突），而这些值要作为参数交给
//ChatServer::kickUserSessions / kickEnterpriseSessions（Protocol.h 世界）下发，故在此
//显式对齐取值。改 Protocol.h 里那些枚举值时，这里必须同步。
#define ADMIN_KICKOUT_REASON_ACCOUNT_DISABLED     2
#define ADMIN_KICKOUT_REASON_ENTERPRISE_DISABLED  6
#define ADMIN_KICKOUT_REASON_ENTERPRISE_DELETED   7
#define ADMIN_KICKOUT_REASON_ACCOUNT_DELETED      8
#define ADMIN_KICKOUT_REASON_KICKED_BY_ADMIN      9
#define ADMIN_KICKOUT_REASON_PASSWORD_RESET       10   //管理员重置了员工密码（kickout_reason_password_reset_by_admin）

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
        //GET：参数位于 url 的 ?p= 之后（既有 register.do 等接口）。查询串不合规时直接断开，
        //不再对它 substr（原先查询串不足两个字符时抛出异常、整个进程退出）
        if (!httpparse::ParseGetTarget(target, url, param))
        {
            LOGW("http bad get target from %s", conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }
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

    //处理过程中抛出的任何异常都不能逸出 I/O 线程，否则整个 chatserver 退出
    try
    {
        if (!process(conn, url, param))
        {
            LOGE("handle http request error, url: %s, from: %s", url.c_str(), conn->peerAddress().toIpPort().c_str());
        }
    }
    catch (const std::exception& e)
    {
        LOGE("handle http request exception: %s, url: %s, from: %s", e.what(), url.c_str(),
             conn->peerAddress().toIpPort().c_str());
    }

    //短连接，处理完关闭连接
    conn->forceClose();
}

void HttpSession::send(const char* data, size_t length)
{
    if (!m_tmpConn.expired())
    {
        std::shared_ptr<TcpConnection> conn = m_tmpConn.lock();
        conn->send(data, static_cast<int>(length));
    }
}

bool HttpSession::process(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& param)
{
    if (url.empty())
        return false;

    //admin 控制台接口统一以 /admin/ 开头，单独分发。
    if (url.compare(0, 7, "/admin/") == 0)
        return processAdminRequest(conn, url, param);

    //外部系统经 Webhook 推送机器人消息（docs/机器人方案.md 6.3），凭机器人的 Webhook 令牌、不需要管理员 token
    if (url == "/bot/webhook.do")
    {
        onBotWebhook(param, conn);
        return true;
    }

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
    else if (url == "/admin/announcement/repush.do")
        onAdminAnnouncementRepush(body, conn);
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
    else if (url == "/admin/sensitive/list.do")
        onAdminSensitiveList(body, conn);
    else if (url == "/admin/sensitive/add.do")
        onAdminSensitiveAdd(body, conn);
    else if (url == "/admin/sensitive/update.do")
        onAdminSensitiveUpdate(body, conn);
    else if (url == "/admin/sensitive/delete.do")
        onAdminSensitiveDelete(body, conn);
    else if (url == "/admin/sensitive/import.do")
        onAdminSensitiveImport(body, conn);
    else if (url == "/admin/sensitive/export.do")
        onAdminSensitiveExport(body, conn);
    else if (url == "/admin/sensitive/setenabled.do")
        onAdminSensitiveSetEnabled(body, conn);
    else if (url == "/admin/sensitive/hits.do")
        onAdminSensitiveHits(body, conn);
    else if (url == "/admin/qa/list.do")
        onAdminQaList(body, conn);
    else if (url == "/admin/qa/add.do")
        onAdminQaAdd(body, conn);
    else if (url == "/admin/qa/update.do")
        onAdminQaUpdate(body, conn);
    else if (url == "/admin/qa/delete.do")
        onAdminQaDelete(body, conn);
    else if (url == "/admin/qa/groups.do")
        onAdminQaGroups(body, conn);
    else if (url == "/admin/bot/list.do")
        onAdminBotList(body, conn);
    else if (url == "/admin/bot/detail.do")
        onAdminBotDetail(body, conn);
    else if (url == "/admin/bot/save.do")
        onAdminBotSave(body, conn);
    else if (url == "/admin/bot/delete.do")
        onAdminBotDelete(body, conn);
    else if (url == "/admin/bot/setstatus.do")
        onAdminBotSetStatus(body, conn);
    else if (url == "/admin/bot/groups.do")
        onAdminBotGroups(body, conn);
    else if (url == "/admin/bot/try.do")
        onAdminBotTry(body, conn);
    else if (url == "/admin/bot/resettoken.do")
        onAdminBotResetToken(body, conn);
    else if (url == "/admin/bot/domains.do")
        onAdminBotDomains(body, conn);
    else if (url == "/admin/bot/setdomains.do")
        onAdminBotSetDomains(body, conn);
    else if (url == "/admin/setting/get.do")
        onAdminSettingGet(body, conn);
    else if (url == "/admin/setting/set.do")
        onAdminSettingSet(body, conn);
    else if (url == "/admin/fileshare/list.do")
        onAdminFileShareList(body, conn);
    else if (url == "/admin/fileshare/members.do")
        onAdminFileShareMembers(body, conn);
    else if (url == "/admin/fileshare/cancel.do")
        onAdminFileShareCancel(body, conn);
    else if (url == "/admin/fileshare/audit.do")
        onAdminFileShareAudit(body, conn);
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

        LOGI("Response to client: http register, data: %s, client: %s", retData.c_str(), conn->peerAddress().toIpPort().c_str());
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

    //管理控制台长连接（FlamingoLink.dll，docs/FlamingoLink方案.md 5.5）：管理端口与服务端身份公钥。
    //端口为 0 表示服务器未开启管理端口；公钥为空表示未开启协议加密，控制台据此跳过握手
    int linkPort = 0;
    std::string linkPubkey;
    Singleton<AdminLinkServer>::Instance().getLoginLinkInfo(linkPort, linkPubkey);
    resp["linkport"] = linkPort;
    resp["linkpubkey"] = linkPubkey;

    sendJsonResponse(conn, DumpJson(resp));
    LOGI("admin login ok, email: %s, adminid: %lld, linkport: %d", email.c_str(), (long long)admin.adminid, linkPort);
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
    //新建账号的前缀（大写）；空串表示没设、按默认的 EMP 生成（2026-10-06）
    je["accountprefix"]     = e.accountPrefix;
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

    //关键字与状态之外的三项筛选（行业、套餐、开通起止日期）均为可选，不带即不限。
    EnterpriseListFilter filter;
    filter.keyword  = JStr(root, "keyword");
    filter.status   = static_cast<int32_t>(JInt(root, "status", -1));
    filter.industry = JStr(root, "industry");
    filter.plan     = static_cast<int32_t>(JInt(root, "plan", -1));
    filter.openFrom = JStr(root, "openfrom");
    filter.openTo   = JStr(root, "opento");
    int32_t page = static_cast<int32_t>(JInt(root, "page", 1));
    int32_t pageSize = static_cast<int32_t>(JInt(root, "pagesize", 20));

    int32_t total = 0;
    std::vector<EnterpriseInfo> list;
    Singleton<EnterpriseManager>::Instance().listEnterprises(filter, page, pageSize, total, list);

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
    //企业管理员只能读本企业：请求里的 enterpriseid 一律改写为其所属企业（与敏感词接口的
    //做法一致），控制台据此直接打开本企业详情；平台超管按请求里的 enterpriseid 读。
    const int64_t enterpriseId = (admin.enterpriseid != 0) ? admin.enterpriseid
                                                           : JInt(root, "enterpriseid", 0);

    EnterpriseInfo info;
    if (!Singleton<EnterpriseManager>::Instance().getEnterprise(enterpriseId, info))
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

    //账号前缀（2026-10-06）带了才改：老版本控制台不带这个字段，不能因此把已设的前缀清空。
    //先校验再改别的字段，前缀不合格时整个请求不生效
    const bool hasPrefix = root.isMember("accountprefix") && root["accountprefix"].isString();
    const std::string rawPrefix = hasPrefix ? root["accountprefix"].asString() : std::string();
    std::string normalizedPrefix;
    if (hasPrefix && !NormalizeAccountPrefix(rawPrefix, normalizedPrefix))
    {
        LOGE("onAdminEnterpriseUpdate: invalid account prefix '%s', enterpriseid: %lld",
             rawPrefix.c_str(), (long long)req.enterpriseId);
        sendAdminResult(conn, error_code_account_prefix_invalid, "invalid account prefix");
        return;
    }

    int32_t code = Singleton<EnterpriseManager>::Instance().updateEnterprise(req);
    if (code == error_code_ok && hasPrefix)
        code = Singleton<EnterpriseManager>::Instance().setAccountPrefix(req.enterpriseId, rawPrefix);
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
    else if (code == error_code_account_prefix_invalid)
        failMsg = "invalid account prefix";
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

    //导出与列表使用同一组筛选条件，界面上筛完再导出得到的就是看到的那些企业。
    EnterpriseListFilter filter;
    filter.keyword  = JStr(root, "keyword");
    filter.status   = static_cast<int32_t>(JInt(root, "status", -1));
    filter.industry = JStr(root, "industry");
    filter.plan     = static_cast<int32_t>(JInt(root, "plan", -1));
    filter.openFrom = JStr(root, "openfrom");
    filter.openTo   = JStr(root, "opento");
    std::string csv;
    Singleton<EnterpriseManager>::Instance().exportEnterprisesCsv(filter, csv);

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
        //企业的全部机器人数据一并删除（docs/机器人方案.md §5.5）
        Singleton<BotManager>::Instance().onEnterpriseDeleted(entId);
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
//的 cmd 与 reason 取值，本文件属 AdminApiCodes.h 世界、引不进 Protocol.h。历史上这里是自己
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
    //性别可选，不带时按「保密」（0）建；取值合法性由 EmployeeManager::createEmployee 校验
    req.gender       = static_cast<int32_t>(JInt(root, "gender", 0));

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
    //性别可选：带了就改（合法性由 EmployeeManager::updateEmployee 校验），没带（老版本控制台）保持原值
    req.gender      = root.isMember("gender") ? static_cast<int32_t>(JInt(root, "gender", 0))
                                              : EMPLOYEE_GENDER_KEEP;

    int32_t code = empMgr.updateEmployee(req);
    if (code == error_code_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), empEnt, "user.update", req.name, 1, u8"修改员工信息");
        //姓名/职务等资料已变更，通知该企业在线 IM 客户端刷新通讯录，免得同事端要重登才看到。
        //deptid 传 0：本次改的是"人"而不是某个部门，客户端收到后整棵通讯录重拉，不依赖该字段。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            empEnt, dept::org_change_member, 0);
        //姓名即 IM 昵称：该员工所在的自动命名群要按新昵称重算群名。本接口不区分姓名是否
        //真的改了，未改时重算结果与原名相同，不会写库，也不会推送
        Singleton<ChatServer>::Instance().refreshAutoGroupNamesOfMemberAndNotify(
            static_cast<int32_t>(userId));
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
        //该员工的个人机器人（含自动回复）一并软删除
        Singleton<BotManager>::Instance().onUserDeleted(userId, Timestamp::now().microSecondsSinceEpoch() / 1000);
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

    //旧密码已失效：踢掉该员工全部在线设备，客户端提示密码已被管理员重置、须用新密码重新登录
    //（2026-10-07 起，与短信找回密码 ChatSession::onResetPassword 一致；此前已登录的设备会继续可用）
    int32_t kicked = KickUserSessions(userId, 0, ADMIN_KICKOUT_REASON_PASSWORD_RESET);
    LOGI("admin reset password, userid: %lld, adminid: %lld, kicked sessions: %d",
         (long long)userId, (long long)admin.adminid, kicked);

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), empEnt, "user.resetpassword", "-", 2, u8"重置员工密码");

    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "ok";
    resp["newpassword"] = newPwd;
    resp["kicked"] = kicked;   //因密码重置而下线的会话数
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

        //立即发布：把新公告推给在线受众，他们的「公告中心」即时亮起角标。
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

void HttpSession::onAdminAnnouncementRepush(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
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
    //公告中心只对平台超管开放，与发布、撤回一致。
    if (admin.enterpriseid != 0)
    {
        sendAdminResult(conn, error_code_dept_permissiondenied, "platform admin required");
        return;
    }

    int64_t annId = JInt(root, "announcementid", 0);
    //在线名单与 v2 推送报文都在 ChatServer 一侧，由它筛人并推送；这里只回结果与写审计。
    int32_t pushed = 0;
    int32_t code = Singleton<ChatServer>::Instance().repushAnnouncement(annId, pushed);
    if (code != error_code_ok)
    {
        LOGI("admin repush announcement rejected, announcementid: %lld, code: %d",
             (long long)annId, code);
        sendAdminResult(conn, code, code == error_code_announcement_notpublished
                                        ? "announcement not published" : "repush announcement failed");
        return;
    }

    const std::string detail = std::string(u8"重新推送公告，推送给 ") + std::to_string(pushed)
                             + u8" 名在线未读成员";
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), 0, "announcement.repush", std::to_string(annId), 0, detail);

    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["pushed"] = pushed;
    sendJsonResponse(conn, DumpJson(resp));
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
    if (!isValidServiceKind(req.kind))
    {
        LOGE("save service failed: invalid kind %d, adminid: %lld", req.kind, (long long)admin.adminid);
        sendAdminResult(conn, error_code_unknown, "invalid kind");
        return;
    }

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

    //群协议与群存储属于 v2（Protocol.h）世界，交给 ChatServer 编排，避免本文件引入会与 AdminApiCodes.h
    //冲突的 Protocol.h；这里只负责 token / 企业裁决 / 审计 / 应答（均用 AdminApiCodes.h 的错误码）。
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
// 注意错误码：本文件用的是 AdminApiCodes.h 里的 error_code（v1 那一套，admin 接口文档即按它
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

    //forced 字段可选：带了就按它改「强制升级」，没带（老版本控制台）就保持原值。
    const int32_t forced = root.isMember("forced")
                               ? (JBool(root, "forced") ? 1 : 0)
                               : CLIENT_VERSION_FORCED_KEEP;
    int errorCode = 0;
    if (!Singleton<ClientVersionManager>::Instance().updateVersion(
            id, JStr(root, "minversion"), JStr(root, "releasenote"), forced, errorCode))
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


// ====================================================================
// admin 控制台接口处理器（敏感词模块，/admin/sensitive/*.do）
// ====================================================================
//
//八个接口共用一条流水线：解析请求体 → 校验令牌 → 确定企业（企业管理员一律是本企业，平台超级
//管理员须在请求里用 enterpriseid 指定）→ 调 SensitiveWordManager → 写审计 → 回包。其中不碰网络
//与数据库的部分（结果码换算、企业范围、数组解析、应答拼装）在 SensitiveAdminProtocol 里，那里有
//单元测试。方案见 docs/敏感词方案.md 第 6.7 节。

//敏感词模块审计记录的严重程度：普通写操作为 1，删除为 2，与其它模块同一口径。
static const int kSensitiveAuditWriteSeverity  = 1;
static const int kSensitiveAuditDeleteSeverity = 2;

//解析请求体、校验令牌并确定本次操作的企业。
//  body：        请求体
//  root：        [出参] 解析出的 JSON
//  admin：       [出参] 令牌对应的管理员
//  enterpriseId：[出参] 本次操作的企业ID
//  failMsg：     [出参] 失败时回给管理端的描述
//@return error_code_ok 表示可以继续；否则为回给管理端的错误码
static int32_t PrepareSensitiveRequest(const std::string& body, Json::Value& root, AdminInfo& admin,
                                       int64_t& enterpriseId, const char*& failMsg)
{
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        failMsg = "invalid request body";
        return error_code_unknown;
    }
    if (!AdminCheckToken(root, admin))
    {
        failMsg = "not login or token expired";
        return error_code_notlogin;
    }
    if (!sensitiveadmin::ResolveEnterprise(admin.enterpriseid, JInt(root, "enterpriseid", 0), enterpriseId))
    {
        failMsg = "enterpriseid required";
        return error_code_unknown;
    }
    return error_code_ok;
}

//当前时间，Unix 毫秒。
static int64_t SensitiveNowMs()
{
    return Timestamp::now().microSecondsSinceEpoch() / 1000;
}

void HttpSession::onAdminSensitiveList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    SensitiveWordManager& mgr = Singleton<SensitiveWordManager>::Instance();
    //先读总开关：企业不存在时它会失败，顺便拦下平台超管填错的企业ID
    bool enabled = false;
    if (!mgr.getEnabled(enterpriseId, enabled))
    {
        sendAdminResult(conn, error_code_unknown, "enterprise not found");
        return;
    }

    SensitiveWordQuery query;
    query.m_enterpriseId = enterpriseId;
    query.m_keyword = JStr(root, "keyword");
    query.m_category = static_cast<int>(JInt(root, "category", 0));
    query.m_page = static_cast<int>(JInt(root, "page", 1));
    query.m_pageSize = static_cast<int>(JInt(root, "pagesize", chatserver::SENSITIVE_DEFAULT_PAGE_SIZE));
    std::vector<SensitiveWordRecord> words;
    int total = 0;
    int wordCount = 0;
    if (!mgr.listWords(query, words, total) || !mgr.countWords(enterpriseId, wordCount))
    {
        sendAdminResult(conn, error_code_unknown, "list sensitive words failed");
        return;
    }

    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["total"] = total;
    resp["enabled"] = enabled ? 1 : 0;
    resp["wordcount"] = wordCount;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < words.size(); ++i)
        arr.append(sensitiveadmin::WordToJson(words[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminSensitiveAdd(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    int64_t newId = 0;
    const int op = Singleton<SensitiveWordManager>::Instance().addWord(
        enterpriseId, JStr(root, "word"),
        static_cast<int>(JInt(root, "category", chatserver::sensitive_category_other)),
        JStr(root, "remark"), admin.adminid, admin.name, SensitiveNowMs(), &newId);
    const int32_t code = sensitiveadmin::ToAdminCode(op);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "add sensitive word failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "sensitive.add",
        std::to_string(static_cast<long long>(newId)), kSensitiveAuditWriteSeverity, u8"添加敏感词");

    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["id"] = static_cast<Json::Int64>(newId);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminSensitiveUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    const int64_t id = JInt(root, "id", 0);
    const int op = Singleton<SensitiveWordManager>::Instance().updateWord(
        enterpriseId, id, JStr(root, "word"),
        static_cast<int>(JInt(root, "category", chatserver::sensitive_category_other)),
        JStr(root, "remark"), SensitiveNowMs());
    const int32_t code = sensitiveadmin::ToAdminCode(op);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, "update sensitive word failed");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "sensitive.update",
        std::to_string(static_cast<long long>(id)), kSensitiveAuditWriteSeverity, u8"修改敏感词");
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminSensitiveDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    std::vector<int64_t> ids;
    if (!sensitiveadmin::ParseIds(root["ids"], static_cast<size_t>(chatserver::SENSITIVE_DELETE_MAX_IDS), ids))
    {
        sendAdminResult(conn, error_code_unknown, "ids must be a non-empty array of at most 500 ids");
        return;
    }

    int deleted = 0;
    if (!Singleton<SensitiveWordManager>::Instance().deleteWords(enterpriseId, ids, deleted))
    {
        sendAdminResult(conn, error_code_unknown, "delete sensitive words failed");
        return;
    }

    if (deleted > 0)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), enterpriseId, "sensitive.delete",
            std::to_string(deleted), kSensitiveAuditDeleteSeverity, u8"删除敏感词");
    }

    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["deleted"] = deleted;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminSensitiveImport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    std::vector<std::string> lines;
    if (!sensitiveadmin::ParseLines(root["lines"], static_cast<size_t>(chatserver::SENSITIVE_IMPORT_MAX_LINES), lines))
    {
        sendAdminResult(conn, error_code_unknown, "lines must be a non-empty array of at most 1000 strings");
        return;
    }

    int added = 0;
    std::vector<SensitiveImportSkip> skipped;
    if (!Singleton<SensitiveWordManager>::Instance().importWords(
            enterpriseId, lines,
            static_cast<int>(JInt(root, "category", chatserver::sensitive_category_other)),
            admin.adminid, admin.name, SensitiveNowMs(), added, skipped))
    {
        sendAdminResult(conn, error_code_unknown, "import sensitive words failed");
        return;
    }

    if (added > 0)
    {
        const std::string detail = std::string(u8"导入敏感词：成功 ") + std::to_string(added)
                                 + u8" 条，跳过 " + std::to_string(skipped.size()) + u8" 条";
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
            conn->peerAddress().toIpPort(), enterpriseId, "sensitive.import",
            std::to_string(added), kSensitiveAuditWriteSeverity, detail);
    }
    sendJsonResponse(conn, DumpJson(sensitiveadmin::ImportResultToJson(added, skipped)));
}

void HttpSession::onAdminSensitiveExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    std::string text;
    if (!Singleton<SensitiveWordManager>::Instance().exportWords(enterpriseId, text))
    {
        sendAdminResult(conn, error_code_unknown, "export sensitive words failed");
        return;
    }

    //纯文本应答，每行一条：词条<Tab>分类名<Tab>备注，可原样再导入
    std::string response;
    makeupResponse(text, response, "text/plain;charset=utf-8");
    conn->send(response);
}

void HttpSession::onAdminSensitiveSetEnabled(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    //enabled 接受布尔或整数（非 0 为启用）；缺字段时拒绝，免得误把开关关掉
    bool enabled = false;
    if (root["enabled"].isBool())
        enabled = root["enabled"].asBool();
    else if (root["enabled"].isIntegral())
        enabled = (root["enabled"].asInt64() != 0);
    else
    {
        sendAdminResult(conn, error_code_unknown, "enabled required");
        return;
    }

    if (!Singleton<SensitiveWordManager>::Instance().setEnabled(enterpriseId, enabled))
    {
        sendAdminResult(conn, error_code_unknown, "enterprise not found");
        return;
    }

    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name,
        conn->peerAddress().toIpPort(), enterpriseId, "sensitive.setenabled",
        enabled ? "1" : "0", kSensitiveAuditWriteSeverity,
        enabled ? u8"启用敏感词过滤" : u8"暂停敏感词过滤");
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminSensitiveHits(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }

    SensitiveHitQuery query;
    query.m_enterpriseId = enterpriseId;
    query.m_beginTime = JInt(root, "begintime", 0);
    query.m_endTime = JInt(root, "endtime", 0);
    query.m_keyword = JStr(root, "keyword");
    query.m_source = static_cast<int>(JInt(root, "source", 0));
    query.m_category = static_cast<int>(JInt(root, "category", 0));
    query.m_page = static_cast<int>(JInt(root, "page", 1));
    query.m_pageSize = static_cast<int>(JInt(root, "pagesize", chatserver::SENSITIVE_DEFAULT_PAGE_SIZE));
    std::vector<SensitiveHitRecord> hits;
    int total = 0;
    if (!Singleton<SensitiveWordManager>::Instance().listHits(query, hits, total))
    {
        sendAdminResult(conn, error_code_unknown, "list sensitive hits failed");
        return;
    }

    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["total"] = total;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < hits.size(); ++i)
        arr.append(sensitiveadmin::HitToJson(hits[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

//==================== 文件共享（docs/文件共享方案.md 6.5，第 3 期第 3 步）====================

//写管理员操作日志的级别：改功能设置为警告（影响整个企业），强制取消共享为危险
static const int kFileShareAuditSettingSeverity = 1;
static const int kFileShareAuditCancelSeverity  = 2;

//把文件共享业务层的结果码换成管理端错误码与诊断信息
static void FileShareAdminResultToCode(int rc, int32_t& code, const char*& msg)
{
    switch (rc)
    {
    //成功
    case fileshareadmin::file_share_admin_ok:
        code = error_code_ok;
        msg = "ok";
        break;
    //请求参数不对
    case fileshareadmin::file_share_admin_bad_request:
        code = error_code_feature_setting_invalid;
        msg = "invalid parameter";
        break;
    //共享不存在
    case fileshareadmin::file_share_admin_not_found:
        code = error_code_fileshare_notexist;
        msg = "share not exist";
        break;
    //共享已经取消过
    case fileshareadmin::file_share_admin_canceled:
        code = error_code_fileshare_canceled;
        msg = "share already canceled";
        break;
    //不在企业管理员的范围内
    case fileshareadmin::file_share_admin_denied:
        code = error_code_fileshare_denied;
        msg = "share belongs to another enterprise";
        break;
    //数据库失败与不认识的取值
    default:
        code = error_code_unknown;
        msg = "database error";
        break;
    }
}

//解析请求、校验令牌：失败时已回包，返回 false
bool HttpSession::prepareFileShareRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn,
                                          Json::Value& root, AdminInfo& admin)
{
    if (!ParseJsonBody(body, root) || !root.isObject())
    {
        sendAdminResult(conn, error_code_unknown, "invalid request body");
        return false;
    }
    if (!AdminCheckToken(root, admin))
    {
        sendAdminResult(conn, error_code_notlogin, "not login or token expired");
        return false;
    }
    return true;
}

//业务结果回包：成功时把应答主体并上 code 与 msg，失败时只回 code 与 msg
void HttpSession::sendFileShareResult(const std::shared_ptr<TcpConnection>& conn, int rc, Json::Value& out)
{
    int32_t code = error_code_ok;
    const char* msg = "";
    FileShareAdminResultToCode(rc, code, msg);
    if (code != error_code_ok)
    {
        sendAdminResult(conn, code, msg);
        return;
    }
    if (!out.isObject())
        out = Json::Value(Json::objectValue);
    out["code"] = error_code_ok;
    out["msg"] = "ok";
    sendJsonResponse(conn, DumpJson(out));
}

//确定读写设置作用的企业：企业管理员为本企业，平台管理员须指定、且企业必须存在。失败时已回包
bool HttpSession::resolveSettingEnterprise(const std::shared_ptr<TcpConnection>& conn, const Json::Value& root,
                                           const AdminInfo& admin, int64_t& enterpriseId)
{
    //与 fileshareadmin::ResolveTargetEnterprise 同一规则；这里多一步核对企业存在（平台管理员可能填错）
    enterpriseId = (admin.enterpriseid != 0) ? admin.enterpriseid : JInt(root, "enterpriseid", 0);
    if (enterpriseId <= 0)
    {
        sendAdminResult(conn, error_code_unknown, "enterpriseid required");
        return false;
    }
    EnterpriseInfo info;
    if (!Singleton<EnterpriseManager>::Instance().getEnterprise(enterpriseId, info))
    {
        sendAdminResult(conn, error_code_unknown, "enterprise not found");
        return false;
    }
    return true;
}

void HttpSession::onAdminSettingGet(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    int64_t enterpriseId = 0;
    if (!resolveSettingEnterprise(conn, root, admin, enterpriseId))
        return;
    Json::Value out;
    const int rc = fileshareadmin::GetSettings(enterpriseId, out);
    if (rc == fileshareadmin::file_share_admin_ok)
    {
        out["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
        //云端录像的已用空间，管理后台与配额一起显示；云端录制没有初始化或查库失败时不带这一项
        CloudRecordService* cloud = CloudRecordService::instance();
        int64_t usedBytes = 0;
        if (cloud != NULL && cloud->enterpriseUsedBytes(enterpriseId, usedBytes))
            out["cloudrecordusedbytes"] = static_cast<Json::Int64>(usedBytes);
    }
    sendFileShareResult(conn, rc, out);
}

void HttpSession::onAdminSettingSet(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    int64_t enterpriseId = 0;
    if (!resolveSettingEnterprise(conn, root, admin, enterpriseId))
        return;
    const std::string key = JStr(root, "key");
    //value 允许写成数字或字符串
    std::string value = JStr(root, "value");
    if (value.empty() && root.isMember("value") && root["value"].isIntegral())
        value = std::to_string(root["value"].asInt64());

    bool before = false;
    bool after = false;
    const int64_t nowMs = Timestamp::now().microSecondsSinceEpoch() / 1000;
    const int rc = fileshareadmin::SetSetting(enterpriseId, key, value, admin.adminid, nowMs, before, after);
    if (rc == fileshareadmin::file_share_admin_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
            enterpriseId, "setting.set", key + "=" + value, kFileShareAuditSettingSeverity,
            FeatureSwitchManager::describeSettingWrite(key, value, after));
        LOGI("admin setting set, admin: %lld, enterpriseid: %lld, %s=%s, enabled: %d -> %d", (long long)admin.adminid,
             (long long)enterpriseId, key.c_str(), value.c_str(), before ? 1 : 0, after ? 1 : 0);
    }
    Json::Value out(Json::objectValue);
    out["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    out["enabled"] = after ? 1 : 0;
    sendFileShareResult(conn, rc, out);
}

void HttpSession::onAdminFileShareList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    Json::Value out;
    sendFileShareResult(conn, fileshareadmin::ListShares(root, admin.enterpriseid, out), out);
}

void HttpSession::onAdminFileShareMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    Json::Value out;
    const int rc = fileshareadmin::GetMembers(JInt(root, "shareid", 0), admin.enterpriseid, out);
    sendFileShareResult(conn, rc, out);
}

void HttpSession::onAdminFileShareCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    const int64_t shareId = JInt(root, "shareid", 0);
    std::string shareName;
    int64_t shareEnterpriseId = 0;
    const int64_t nowMs = Timestamp::now().microSecondsSinceEpoch() / 1000;
    const int rc = fileshareadmin::CancelShare(shareId, admin.enterpriseid, admin.adminid, nowMs, shareName, shareEnterpriseId);
    if (rc == fileshareadmin::file_share_admin_ok)
    {
        Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
            shareEnterpriseId, "fileshare.cancel", std::to_string(shareId) + " " + shareName, kFileShareAuditCancelSeverity,
            u8"强制取消文件共享");
    }
    Json::Value out(Json::objectValue);
    sendFileShareResult(conn, rc, out);
}

void HttpSession::onAdminFileShareAudit(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    if (!prepareFileShareRequest(body, conn, root, admin))
        return;
    Json::Value out;
    sendFileShareResult(conn, fileshareadmin::QueryAudits(root, admin.enterpriseid, out), out);
}

// ====================================================================
// admin 控制台接口处理器（企业问答，/admin/qa/*.do）
// ====================================================================
//
//五个接口与敏感词模块共用 PrepareSensitiveRequest（解析请求体、校验令牌、确定企业，与模块无关）。
//「本企业的群」按群主所属企业认定（t_group 没有企业列），见 docs/群问答方案.md §1。

//企业问答审计记录的严重程度：与敏感词模块同一口径
static const int kQaAuditWriteSeverity  = 1;
static const int kQaAuditDeleteSeverity = 2;

//本企业的全部群：(群ID, 群名)，按群ID升序
static void ListEnterpriseGroups(int64_t enterpriseId, std::vector<std::pair<int64_t, std::string> >& out)
{
    out.clear();
    std::vector<std::pair<int64_t, std::pair<std::string, int64_t> > > all;
    if (!Singleton<GroupQaManager>::Instance().listAllGroups(all))
        return;
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (deptMgr.getUserEnterpriseId(all[i].second.second) == enterpriseId)
            out.push_back(std::make_pair(all[i].first, all[i].second.first));
    }
}

//本企业全部群的 ID 集合
static std::set<int64_t> EnterpriseGroupIds(int64_t enterpriseId)
{
    std::vector<std::pair<int64_t, std::string> > groups;
    ListEnterpriseGroups(enterpriseId, groups);
    std::set<int64_t> ids;
    for (size_t i = 0; i < groups.size(); ++i)
        ids.insert(groups[i].first);
    return ids;
}

void HttpSession::onAdminQaList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    std::vector<EnterpriseQaRecord> items;
    if (!Singleton<GroupQaManager>::Instance().listEnterpriseQa(enterpriseId, items))
    {
        sendAdminResult(conn, error_code_unknown, "list qa failed");
        return;
    }
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
        arr.append(qaadmin::QaToJson(items[i]));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
    LOGI("admin qa list ok, admin: %lld, enterprise: %lld, count: %u",
         (long long)admin.adminid, (long long)enterpriseId, static_cast<unsigned>(items.size()));
}

void HttpSession::onAdminQaAdd(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    EnterpriseQaRecord rec;
    if (!qaadmin::ParseQaFields(root, rec))
    {
        sendAdminResult(conn, error_code_qa_invalid, "invalid qa fields");
        return;
    }
    const int32_t check = qaadmin::CheckQa(rec, EnterpriseGroupIds(enterpriseId));
    if (check != error_code_ok)
    {
        sendAdminResult(conn, check, "qa rejected");
        return;
    }
    GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();
    if (mgr.countEnterpriseQa(enterpriseId) >= kEnterpriseQaPerEnterpriseMax)
    {
        sendAdminResult(conn, error_code_qa_full, "qa limit reached");
        return;
    }
    rec.enterpriseid = enterpriseId;
    rec.creatorid = admin.adminid;
    rec.creatorname = admin.name;
    int64_t newId = 0;
    if (!mgr.addEnterpriseQa(rec, SensitiveNowMs(), newId))
    {
        sendAdminResult(conn, error_code_unknown, "add qa failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "qa.add", std::to_string(static_cast<long long>(newId)), kQaAuditWriteSeverity, u8"添加企业问答");
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["id"] = std::to_string(static_cast<long long>(newId));
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminQaUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    EnterpriseQaRecord rec;
    if (!qaadmin::ParseQaFields(root, rec))
    {
        sendAdminResult(conn, error_code_qa_invalid, "invalid qa fields");
        return;
    }
    const int64_t id = static_cast<int64_t>(atoll(JStr(root, "id").c_str()));
    GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();
    EnterpriseQaRecord exist;
    if (id <= 0 || !mgr.getEnterpriseQa(id, exist) || exist.enterpriseid != enterpriseId)
    {
        sendAdminResult(conn, error_code_qa_notexist, "qa not exist");
        return;
    }
    const int32_t check = qaadmin::CheckQa(rec, EnterpriseGroupIds(enterpriseId));
    if (check != error_code_ok)
    {
        sendAdminResult(conn, check, "qa rejected");
        return;
    }
    rec.id = id;
    rec.enterpriseid = enterpriseId;
    rec.creatorid = admin.adminid;
    rec.creatorname = admin.name;
    if (!mgr.updateEnterpriseQa(rec, SensitiveNowMs()))
    {
        sendAdminResult(conn, error_code_unknown, "update qa failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "qa.update", std::to_string(static_cast<long long>(id)), kQaAuditWriteSeverity, u8"修改企业问答");
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminQaDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const int64_t id = static_cast<int64_t>(atoll(JStr(root, "id").c_str()));
    GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();
    EnterpriseQaRecord exist;
    if (id <= 0 || !mgr.getEnterpriseQa(id, exist) || exist.enterpriseid != enterpriseId)
    {
        sendAdminResult(conn, error_code_qa_notexist, "qa not exist");
        return;
    }
    if (!mgr.deleteEnterpriseQa(enterpriseId, id))
    {
        sendAdminResult(conn, error_code_unknown, "delete qa failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "qa.delete", std::to_string(static_cast<long long>(id)), kQaAuditDeleteSeverity, u8"删除企业问答");
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminQaGroups(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    std::vector<std::pair<int64_t, std::string> > groups;
    ListEnterpriseGroups(enterpriseId, groups);
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < groups.size(); ++i)
        arr.append(qaadmin::GroupToJson(groups[i].first, groups[i].second));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

//======================== 机器人（/admin/bot/*.do 与 /bot/webhook.do，docs/机器人方案.md 6.3） ========================

//机器人管理接口写操作的审计级别（与企业问答相同）
static const int kBotAuditWriteSeverity = 1;
//Webhook 的请求路径，随列表与详情一起回给管理端；服务端没有配置对外地址时，管理端用它按自己的连接地址拼出完整地址
static const char* const kBotWebhookPath = bot::kWebhookPath;

//读一个 id：数字或数字字符串都接受；缺失或不合法时为 0
static int64_t JId(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    return 0;
}

//ValidateBot 的结果换成管理端错误码
static int32_t BotCheckToAdminCode(int rc)
{
    switch (rc)
    {
    //合法
    case bot::bot_check_ok:
        return error_code_ok;
    //规则、关键词、所选的群超出上限
    case bot::bot_check_full:
        return error_code_botadmin_full;
    //不允许的触发方式、动作，或外部服务地址不在白名单内
    case bot::bot_check_not_allowed:
        return error_code_botadmin_not_allowed;
    //其余字段不合法
    default:
        return error_code_botadmin_invalid;
    }
}

//找本企业的一个企业机器人；找不到返回空
static std::shared_ptr<const bot::BotRecord> FindEnterpriseBot(int64_t enterpriseId, int64_t botId)
{
    std::shared_ptr<const bot::BotRecord> b = Singleton<BotManager>::Instance().snapshot()->find(botId);
    if (!b || b->m_ownerType != bot::bot_owner_enterprise || b->m_enterpriseId != enterpriseId)
        return std::shared_ptr<const bot::BotRecord>();
    return b;
}

//机器人 → 管理端 JSON：列表不带规则与密钥，详情都带；附上最近发消息时间与 Webhook 路径，
//服务端配置了对外地址（botwebhookbaseurl）时再附上完整地址 webhookurl（§11 Q19）
static Json::Value AdminBotJson(const bot::BotRecord& b, bool detail)
{
    Json::Value v = bot::BotToJson(b, detail, detail);
    v["lastfire"] = static_cast<Json::Int64>(Singleton<BotManager>::Instance().lastFireTime(b.m_botId));
    v["webhookpath"] = kBotWebhookPath;
    const std::string& url = Singleton<BotManager>::Instance().webhookUrl();
    if (!url.empty())
        v["webhookurl"] = url;
    return v;
}

//所选的群、定时消息的目标群都必须属于本企业
static bool BotGroupsInEnterprise(const bot::BotRecord& rec, const std::set<int64_t>& entGroups)
{
    if (rec.m_scope == bot::bot_scope_selected)
    {
        if (rec.m_groupIds.empty())
            return false;
        for (size_t i = 0; i < rec.m_groupIds.size(); ++i)
        {
            if (entGroups.count(rec.m_groupIds[i]) == 0)
                return false;
        }
    }
    for (size_t i = 0; i < rec.m_rules.size(); ++i)
    {
        const std::vector<int64_t>& targets = rec.m_rules[i].m_trigger.m_scheduleGroups;
        for (size_t k = 0; k < targets.size(); ++k)
        {
            if (entGroups.count(targets[k]) == 0)
                return false;
        }
    }
    return true;
}

void HttpSession::onAdminBotList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    std::vector<std::shared_ptr<const bot::BotRecord> > bots;
    Singleton<BotManager>::Instance().snapshot()->enterpriseBots(enterpriseId, bots);
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < bots.size(); ++i)
        arr.append(AdminBotJson(*bots[i], false));
    resp["list"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
    LOGI("admin bot list, enterprise: %lld, count: %u", (long long)enterpriseId, static_cast<unsigned>(bots.size()));
}

void HttpSession::onAdminBotDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    std::shared_ptr<const bot::BotRecord> b = FindEnterpriseBot(enterpriseId, JId(root, "botid"));
    if (!b)
    {
        sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
        return;
    }
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["bot"] = AdminBotJson(*b, true);
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminBotSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    bot::BotRecord rec;
    if (!root["bot"].isObject() || !bot::BotFromJson(root["bot"], rec))
    {
        sendAdminResult(conn, error_code_botadmin_invalid, "invalid bot fields");
        return;
    }
    rec.m_ownerType = bot::bot_owner_enterprise;
    rec.m_ownerId = enterpriseId;
    rec.m_enterpriseId = enterpriseId;
    rec.m_source = bot::bot_source_rules;
    if (!BotGroupsInEnterprise(rec, EnterpriseGroupIds(enterpriseId)))
    {
        sendAdminResult(conn, error_code_botadmin_group_invalid, "groups not in enterprise");
        return;
    }
    BotManager& mgr = Singleton<BotManager>::Instance();
    std::shared_ptr<const bot::BotSnapshot> snap = mgr.snapshot();
    std::string why;
    const int check = bot::ValidateBot(rec, true, snap->domains(enterpriseId), why);
    if (check != bot::bot_check_ok)
    {
        LOGW("admin bot save rejected, enterprise: %lld, reason: %s", (long long)enterpriseId, why.c_str());
        sendAdminResult(conn, BotCheckToAdminCode(check), why);
        return;
    }
    const int64_t now = SensitiveNowMs();
    const bool isNew = rec.m_botId <= 0;
    int stored = bot::bot_store_ok;
    if (!isNew)
    {
        std::shared_ptr<const bot::BotRecord> exist = FindEnterpriseBot(enterpriseId, rec.m_botId);
        if (!exist)
        {
            sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
            return;
        }
        if (exist->m_source != bot::bot_source_rules)
        {
            sendAdminResult(conn, error_code_botadmin_readonly, "repo bot is read-only here");
            return;
        }
        stored = mgr.updateBot(rec, now);
    }
    else
    {
        std::vector<std::shared_ptr<const bot::BotRecord> > existing;
        snap->enterpriseBots(enterpriseId, existing);
        if (static_cast<int>(existing.size()) >= bot::kEnterpriseBotMax)
        {
            sendAdminResult(conn, error_code_botadmin_full, "bot limit reached");
            return;
        }
        rec.m_creatorType = bot::bot_creator_admin;
        rec.m_creatorId = admin.adminid;
        rec.m_creatorName = admin.name;
        rec.m_webhookToken = bot::RandomHex32();
        rec.m_secret = bot::RandomHex32();
        stored = mgr.createBot(rec, now);
    }
    if (stored != bot::bot_store_ok)
    {
        sendAdminResult(conn, stored == bot::bot_store_not_exist ? error_code_botadmin_notexist : error_code_unknown,
                        "save bot failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, isNew ? "bot.create" : "bot.update", std::to_string(static_cast<long long>(rec.m_botId)),
        kBotAuditWriteSeverity, isNew ? u8"新建企业机器人" : u8"修改企业机器人");
    Singleton<ChatServer>::Instance().broadcastBotsChanged(enterpriseId);
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["botid"] = std::to_string(static_cast<long long>(rec.m_botId));
    sendJsonResponse(conn, DumpJson(resp));
    LOGI("admin bot saved, enterprise: %lld, botid: %lld, new: %d, rules: %u", (long long)enterpriseId,
         (long long)rec.m_botId, isNew ? 1 : 0, static_cast<unsigned>(rec.m_rules.size()));
}

void HttpSession::onAdminBotDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const int64_t botId = JId(root, "botid");
    std::shared_ptr<const bot::BotRecord> exist = FindEnterpriseBot(enterpriseId, botId);
    if (!exist)
    {
        sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
        return;
    }
    if (exist->m_source != bot::bot_source_rules)
    {
        sendAdminResult(conn, error_code_botadmin_readonly, "repo bot is read-only here");
        return;
    }
    if (Singleton<BotManager>::Instance().deleteBot(botId, SensitiveNowMs()) != bot::bot_store_ok)
    {
        sendAdminResult(conn, error_code_unknown, "delete bot failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "bot.delete", std::to_string(static_cast<long long>(botId)), kBotAuditWriteSeverity,
        u8"删除企业机器人");
    Singleton<ChatServer>::Instance().broadcastBotsChanged(enterpriseId);
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminBotSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const int64_t botId = JId(root, "botid");
    const int status = static_cast<int>(JId(root, "status"));
    if (status != bot::bot_status_active && status != bot::bot_status_disabled)
    {
        sendAdminResult(conn, error_code_botadmin_invalid, "bad status");
        return;
    }
    if (!FindEnterpriseBot(enterpriseId, botId))
    {
        sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
        return;
    }
    if (Singleton<BotManager>::Instance().setStatus(botId, status, SensitiveNowMs()) != bot::bot_store_ok)
    {
        sendAdminResult(conn, error_code_unknown, "set status failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "bot.status", std::to_string(static_cast<long long>(botId)), kBotAuditWriteSeverity,
        status == bot::bot_status_active ? u8"启用企业机器人" : u8"停用企业机器人");
    Singleton<ChatServer>::Instance().broadcastBotsChanged(enterpriseId);
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onAdminBotGroups(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    //与企业问答的 groups.do 相同：列出群主属于本企业的群
    onAdminQaGroups(body, conn);
}

void HttpSession::onAdminBotTry(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    bot::BotRecord rec;
    if (root["bot"].isObject())
    {
        if (!bot::BotFromJson(root["bot"], rec))
        {
            sendAdminResult(conn, error_code_botadmin_invalid, "invalid bot fields");
            return;
        }
        rec.m_ownerType = bot::bot_owner_enterprise;
        rec.m_ownerId = enterpriseId;
        rec.m_enterpriseId = enterpriseId;
    }
    else
    {
        std::shared_ptr<const bot::BotRecord> exist = FindEnterpriseBot(enterpriseId, JId(root, "botid"));
        if (!exist)
        {
            sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
            return;
        }
        rec = *exist;
    }
    const Json::Value& in = root["input"];
    BotTryInput input;
    input.m_text = in["text"].isString() ? in["text"].asString() : std::string();
    input.m_at = !in.isMember("at") || JId(in, "at") != 0 || (in["at"].isBool() && in["at"].asBool());
    input.m_groupid = JId(in, "groupid");
    input.m_timeMs = JId(in, "time");
    if (input.m_groupid > 0 && EnterpriseGroupIds(enterpriseId).count(input.m_groupid) == 0)
        input.m_groupid = 0;
    const bot::BotDecision d = Singleton<BotEngine>::Instance().tryBot(rec, input);
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["matched"] = d.m_kind != bot::bot_decision_none ? 1 : 0;
    resp["kind"] = d.m_kind;
    resp["ruleid"] = std::to_string(static_cast<long long>(d.m_ruleId));
    resp["rulename"] = d.m_ruleName;
    resp["text"] = d.m_text;
    resp["markdown"] = d.m_markdown ? 1 : 0;
    resp["url"] = d.m_url;
    resp["reason"] = d.m_reason;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminBotResetToken(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const int64_t botId = JId(root, "botid");
    if (!FindEnterpriseBot(enterpriseId, botId))
    {
        sendAdminResult(conn, error_code_botadmin_notexist, "bot not exist");
        return;
    }
    std::string token;
    std::string secret;
    if (Singleton<BotManager>::Instance().resetSecrets(botId, token, secret) != bot::bot_store_ok)
    {
        sendAdminResult(conn, error_code_unknown, "reset token failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "bot.resettoken", std::to_string(static_cast<long long>(botId)), kBotAuditWriteSeverity,
        u8"重置机器人的 Webhook 令牌与签名密钥");
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["webhooktoken"] = token;
    resp["secret"] = secret;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminBotDomains(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const std::vector<std::string> domains = Singleton<BotManager>::Instance().snapshot()->domains(enterpriseId);
    Json::Value resp;
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < domains.size(); ++i)
        arr.append(domains[i]);
    resp["domains"] = arr;
    sendJsonResponse(conn, DumpJson(resp));
}

void HttpSession::onAdminBotSetDomains(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Value root;
    AdminInfo admin;
    int64_t enterpriseId = 0;
    const char* failMsg = "";
    const int32_t prepared = PrepareSensitiveRequest(body, root, admin, enterpriseId, failMsg);
    if (prepared != error_code_ok)
    {
        sendAdminResult(conn, prepared, failMsg);
        return;
    }
    const Json::Value& arr = root["domains"];
    if (!arr.isArray() || static_cast<int>(arr.size()) > bot::kBotDomainMax)
    {
        sendAdminResult(conn, error_code_botadmin_domain_invalid, "bad domains");
        return;
    }
    std::vector<std::string> domains;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        std::string d = arr[i].isString() ? arr[i].asString() : std::string();
        for (size_t k = 0; k < d.size(); ++k)
        {
            if (d[k] >= 'A' && d[k] <= 'Z')
                d[k] = static_cast<char>(d[k] - 'A' + 'a');
        }
        if (!bot::IsValidDomain(d))
        {
            sendAdminResult(conn, error_code_botadmin_domain_invalid, "bad domain: " + d);
            return;
        }
        domains.push_back(d);
    }
    if (!Singleton<BotManager>::Instance().setDomains(enterpriseId, domains))
    {
        sendAdminResult(conn, error_code_unknown, "save domains failed");
        return;
    }
    Singleton<AdminManager>::Instance().writeAudit(admin.adminid, admin.name, conn->peerAddress().toIpPort(),
        enterpriseId, "bot.domains", std::to_string(static_cast<long long>(domains.size())), kBotAuditWriteSeverity,
        u8"修改机器人外部服务的域名白名单");
    sendAdminResult(conn, error_code_ok, "ok");
}

void HttpSession::onBotWebhook(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    std::string resp;
    const int code = Singleton<BotEngine>::Instance().handleWebhook(body, resp);
    LOGI("bot webhook from %s, code: %d", conn->peerAddress().toIpPort().c_str(), code);
    sendJsonResponse(conn, resp);
}
