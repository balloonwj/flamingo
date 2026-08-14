/**
 *  flamingoAdmin —— admin 控制台接口业务层实现。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Util/AdminApi.h"
#include "Util/OrgTreeBuilder.h"   // 组织架构树的组装逻辑（可单测的纯模块）

#include "json.h"      // 第三方 jsoncpp 1.9.0（third-party/jsoncpp）

#include <memory>
#include <map>
#include <algorithm>

namespace flamingoadmin {

namespace {

// ---- 接口路径 ----
const wchar_t* const kPathLogin            = L"/admin/login.do";
const wchar_t* const kPathLogout           = L"/admin/logout.do";
const wchar_t* const kPathProfile          = L"/admin/profile.do";
const wchar_t* const kPathUpdateProfile    = L"/admin/updateprofile.do";
const wchar_t* const kPathChangePassword   = L"/admin/changepassword.do";
const wchar_t* const kPathDashStats        = L"/admin/dashboard/stats.do";
const wchar_t* const kPathMessageTrend     = L"/admin/dashboard/messagetrend.do";
const wchar_t* const kPathUserMessageStats = L"/admin/user/messagestats.do";
const wchar_t* const kPathAuditList        = L"/admin/audit/list.do";
const wchar_t* const kPathAuditExport      = L"/admin/audit/export.do";
const wchar_t* const kPathAuditStats       = L"/admin/audit/stats.do";
const wchar_t* const kPathClientVersionScan       = L"/admin/clientversion/scanpackages.do";
const wchar_t* const kPathClientVersionList       = L"/admin/clientversion/list.do";
const wchar_t* const kPathClientVersionCreate     = L"/admin/clientversion/create.do";
const wchar_t* const kPathClientVersionUpdate     = L"/admin/clientversion/update.do";
const wchar_t* const kPathClientVersionSetEnabled = L"/admin/clientversion/setenabled.do";
const wchar_t* const kPathAnnList          = L"/admin/announcement/list.do";
const wchar_t* const kPathAnnDetail        = L"/admin/announcement/detail.do";
const wchar_t* const kPathAnnRecall        = L"/admin/announcement/recall.do";
const wchar_t* const kPathAnnDelete        = L"/admin/announcement/delete.do";
const wchar_t* const kPathAnnSetPinned     = L"/admin/announcement/setpinned.do";
const wchar_t* const kPathAnnSave          = L"/admin/announcement/save.do";
const wchar_t* const kPathAnnSend          = L"/admin/announcement/send.do";
const wchar_t* const kPathEnterpriseList   = L"/admin/enterprise/list.do";
const wchar_t* const kPathEnterpriseCreate = L"/admin/enterprise/create.do";
const wchar_t* const kPathEnterpriseStatus = L"/admin/enterprise/setstatus.do";
const wchar_t* const kPathEnterpriseDelete = L"/admin/enterprise/delete.do";
const wchar_t* const kPathEnterpriseExport = L"/admin/enterprise/export.do";
const wchar_t* const kPathEnterpriseDetail = L"/admin/enterprise/detail.do";
const wchar_t* const kPathDeptTree         = L"/admin/dept/tree.do";
const wchar_t* const kPathServiceList      = L"/admin/service/list.do";
const wchar_t* const kPathDeptCreate       = L"/admin/dept/create.do";
const wchar_t* const kPathUserCreate       = L"/admin/user/create.do";
const wchar_t* const kPathGroupCreate      = L"/admin/group/create.do";
const wchar_t* const kPathDeptDelete       = L"/admin/dept/delete.do";
const wchar_t* const kPathUserDelete       = L"/admin/user/delete.do";
const wchar_t* const kPathServiceSave        = L"/admin/service/save.do";
const wchar_t* const kPathServiceSetStatus   = L"/admin/service/setstatus.do";
const wchar_t* const kPathServiceDelete      = L"/admin/service/delete.do";
const wchar_t* const kPathServiceSetBindings = L"/admin/service/setbindings.do";
const wchar_t* const kPathUserGetPerms       = L"/admin/user/getpermissions.do";
const wchar_t* const kPathUserSetRole        = L"/admin/user/setrole.do";
const wchar_t* const kPathUserSetPerms       = L"/admin/user/setpermissions.do";
const wchar_t* const kPathUserLoginRecords   = L"/admin/user/loginrecords.do";
const wchar_t* const kPathUserActivity       = L"/admin/user/activity.do";
const wchar_t* const kPathUserSetStatus      = L"/admin/user/setstatus.do";
const wchar_t* const kPathUserResetPwd       = L"/admin/user/resetpassword.do";
const wchar_t* const kPathUserKickDevice     = L"/admin/user/kickdevice.do";
const wchar_t* const kPathUserDetail         = L"/admin/user/detail.do";
const wchar_t* const kPathUserUpdate         = L"/admin/user/update.do";
const wchar_t* const kPathUserMove           = L"/admin/user/move.do";
const wchar_t* const kPathEntUpdate          = L"/admin/enterprise/update.do";
const wchar_t* const kPathEntAdmins          = L"/admin/enterprise/admins.do";
const wchar_t* const kPathEntAddAdmin        = L"/admin/enterprise/addadmin.do";
const wchar_t* const kPathEntRemoveAdmin     = L"/admin/enterprise/removeadmin.do";

// ---- 超时（毫秒）----
// 登录涉及查库 + 写会话，给 8 秒；登出尽力而为给 5 秒；企业接口统一给 8 秒；
// 仪表盘统计可能聚合多表，同样给 8 秒。
const int kLoginTimeoutMs      = 8000;
const int kLogoutTimeoutMs     = 5000;
const int kEnterpriseTimeoutMs = 8000;
const int kDashboardTimeoutMs  = 8000;

// ---- 服务端级别编码（审计 audit 与公告 announcement 共用，0=低 1=中 2=高）----
const int kSevWarn   = 1;    // 级别中档：审计「警告」/ 公告「提醒」
const int kSevDanger = 2;    // 级别高档：审计「危险」/ 公告「警告」（0=审计信息 / 公告普通）

// ---- 公告受众类型编码（服务端 chatserversrc/AnnouncementManager.h）----
const int kAudienceAll        = 0;   // 全部企业
const int kAudienceEnterprise = 1;   // 指定企业
const int kAudienceDept       = 2;   // 指定部门
const int kAudienceUser       = 3;   // 指定员工
const int kAudienceInternal   = 4;   // 平台内部

// HTTP 成功状态码。
const int kHttpStatusOk = 200;

// ---- 服务端业务错误码 ----
// 取自 flamingoserver/chatserversrc/Msg.h 的 enum error_code。客户端不直接
// include 服务端头文件，按数值一一对应即可（数值是协议约定，不会变）。
const int kErrOk              = 0;    // 成功
const int kErrUnknown         = 1;    // 服务端未知错误（如数据库故障）
const int kErrNotLogin        = 2;    // 未登录 / token 失效 / 账号被停用
const int kErrInvalidPassword = 103;  // 邮箱不存在或密码错误（服务端为防探测统一此码）
// 组织架构相关错误码（doc 模块 4）。
const int kErrDeptNotExist      = 110;  // 部门不存在
const int kErrDeptNameExist     = 111;  // 同级已存在同名部门
const int kErrDeptHasSub        = 112;  // 部门下仍有在用子部门 / 在职成员，不能删除
const int kErrDeptInvalidParent = 113;  // 非法父部门（不存在或会成环）
const int kErrDeptPermDenied    = 116;  // 无操作企业组织架构的权限

// 客户端本地错误码：表示「往返成功但应答不合法」（解析失败 / 缺字段 / 状态码
// 异常）。取负值，与服务端 error_code（非负）天然不重叠。
const int kErrLocalBadResponse = -1;

// ---- 企业状态编码（docs/Admin控制台接口文档.md 模块 3.5）----
const int kEntStatusPending   = 0;   // 待审核
const int kEntStatusActive    = 1;   // 活跃
const int kEntStatusSuspended = 2;   // 已停用

// ---- 套餐编码 ----
// 文档未明确定义取值，此处按约定：1专业版 / 2企业版 / 3旗舰版。
const int kPlanProfessional = 1;
const int kPlanEnterprise   = 2;
const int kPlanUltimate     = 3;

// ---- 服务号 / 部门成员编码 ----
const int kSvcKindNotification  = 0;   // 服务号类型：0=通知型
const int kSvcKindAssistant     = 1;   // 服务号类型：1=助手型
const int kSvcStatusDisabled    = 1;   // 服务号状态：1=停用（其余按正常）
const int kMemberStatusDisabled = 1;   // 部门成员状态：1=停用（其余按在职）

// ---- 员工性别编码（user/detail.do 的 gender 字段）----
const int kGenderMale   = 0;   // 性别：男
const int kGenderFemale = 1;   // 性别：女（其余按「其他」）

// ---- 服务号绑定类型编码（doc 模块 9.5）----
const int kBindTypeEnterprise = 0;   // 绑定类型：本企业全员
const int kBindTypeDept       = 1;   // 绑定类型：部门
const int kBindTypeUser       = 2;   // 绑定类型：员工

// 把宽字符串（CString）转成 UTF-8 字节串。
std::string Utf8FromWide(const CString& wide)
{
    if (wide.IsEmpty())
        return std::string();

    const int wideLen = wide.GetLength();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, wideLen,
                                            nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();

    std::string utf8(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, wideLen,
                          &utf8[0], bytes, nullptr, nullptr);
    return utf8;
}

// 把 UTF-8 字节串转成宽字符串（CString）。
CString WideFromUtf8(const std::string& utf8)
{
    if (utf8.empty())
        return CString();

    const int byteLen = static_cast<int>(utf8.size());
    const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                              byteLen, nullptr, 0);
    if (wideLen <= 0)
        return CString();

    CString wide;
    wchar_t* buffer = wide.GetBuffer(wideLen);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), byteLen, buffer, wideLen);
    wide.ReleaseBuffer(wideLen);
    return wide;
}

// 把 Json::Value 序列化为紧凑（无缩进）的 UTF-8 JSON 文本。
std::string DumpCompactJson(const Json::Value& root)
{
    Json::StreamWriterBuilder builder;
    builder.settings_["indentation"] = "";   // 紧凑输出，无换行无缩进
    return Json::writeString(builder, root);
}

// 解析一段 UTF-8 JSON 文本到 Json::Value，成功且为对象返回 true。
bool ParseJsonObject(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    const bool ok = reader->parse(text.data(), text.data() + text.size(),
                                  &root, &errs);
    return ok && errs.empty() && root.isObject();
}

// 从 JSON 对象里安全取字符串成员；缺失 / 类型不符返回空串。
std::string JsonStr(const Json::Value& obj, const char* key)
{
    return (obj.isMember(key) && obj[key].isString())
               ? obj[key].asString() : std::string();
}

// 从 JSON 对象里安全取整数成员；缺失 / 类型不符返回 0。
int JsonInt(const Json::Value& obj, const char* key)
{
    return (obj.isMember(key) && obj[key].isIntegral())
               ? obj[key].asInt() : 0;
}

// 从 JSON 对象里安全取 64 位整数成员；缺失 / 类型不符返回 0。
long long JsonInt64(const Json::Value& obj, const char* key)
{
    return (obj.isMember(key) && obj[key].isIntegral())
               ? obj[key].asInt64() : 0;
}

// 从 JSON 对象里安全取布尔成员（兼容布尔与整数两种写法）；缺失返回 false。
bool JsonBool(const Json::Value& obj, const char* key)
{
    if (!obj.isMember(key))
        return false;
    const Json::Value& v = obj[key];
    if (v.isBool())
        return v.asBool();
    if (v.isIntegral())
        return v.asInt() != 0;
    return false;
}

// 把一个十六进制字符（0-9 / a-f / A-F）转成数值；非法字符返回 -1。
int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;   // 'a' 对应数值 10
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;   // 'A' 对应数值 10
    return -1;
}

// 解析 "#rrggbb" 形式的颜色串为 COLORREF。格式非法时返回 fallback。
COLORREF ColorFromHexString(const std::string& hex, COLORREF fallback)
{
    // 期望形如 "#rrggbb"：1 个 '#' + 6 个十六进制位。
    const size_t kHexColorLen = 7;
    if (hex.size() != kHexColorLen || hex[0] != '#')
        return fallback;

    int component[3] = { 0, 0, 0 };   // R / G / B
    for (int i = 0; i < 3; ++i)
    {
        const int hi = HexDigit(hex[1 + i * 2]);
        const int lo = HexDigit(hex[2 + i * 2]);
        if (hi < 0 || lo < 0)
            return fallback;
        component[i] = (hi << 4) | lo;
    }
    return RGB(component[0], component[1], component[2]);
}

// 构造一个发往 server 指定 admin 接口的 HttpRequest（已填好除回传字段
// m_replyHwnd / m_replyMsg / m_tag 外的全部字段，由调用方补齐回传字段）。
HttpRequest MakeRequest(const ServerConfig& server, const wchar_t* path,
                        const Json::Value& body, int timeoutMs)
{
    HttpRequest request;
    request.m_host      = server.m_host.GetString();
    request.m_port      = server.m_port;
    request.m_useTls    = server.m_useTls;
    request.m_path      = path;
    request.m_bodyUtf8  = DumpCompactJson(body);
    request.m_timeoutMs = timeoutMs;
    return request;
}

// 把登录业务错误码翻译成给用户看的中文提示。
CString DescribeLoginError(int code)
{
    switch (code)
    {
    case kErrInvalidPassword:
        // 邮箱不存在与密码错误，服务端统一为此码。
        return CString(_T("邮箱或密码错误"));
    case kErrNotLogin:
        return CString(_T("该管理员账号已被停用，无法登录"));
    case kErrUnknown:
        return CString(_T("服务器内部错误，请稍后重试"));
    case kErrLocalBadResponse:
        return CString(_T("服务器应答异常，请稍后重试"));
    default:
        {
            CString msg;
            msg.Format(_T("登录失败（错误码 %d）"), code);
            return msg;
        }
    }
}

// 把企业管理类接口的业务错误码翻译成给用户看的中文提示。
CString DescribeAdminError(int code)
{
    switch (code)
    {
    case kErrNotLogin:
        return CString(_T("登录已过期，请重新登录"));
    case kErrUnknown:
        return CString(_T("操作失败，请稍后重试"));
    case kErrDeptNotExist:
        return CString(_T("部门不存在"));
    case kErrDeptNameExist:
        return CString(_T("同级已存在同名部门"));
    case kErrDeptHasSub:
        return CString(_T("该部门下仍有子部门或在职成员，无法删除"));
    case kErrDeptInvalidParent:
        return CString(_T("父部门非法（不存在或会造成循环）"));
    case kErrDeptPermDenied:
        return CString(_T("没有操作该企业组织架构的权限"));
    case kErrLocalBadResponse:
        return CString(_T("服务器应答异常，请稍后重试"));
    default:
        {
            CString msg;
            msg.Format(_T("操作失败（错误码 %d）"), code);
            return msg;
        }
    }
}

// 把服务端的企业状态整数映射为 EnterpriseStatus 枚举。
EnterpriseStatus EnterpriseStatusFromInt(int status)
{
    switch (status)
    {
    case kEntStatusActive:
        return EnterpriseStatus::Active;
    case kEntStatusSuspended:
        return EnterpriseStatus::Suspended;
    case kEntStatusPending:
    default:
        return EnterpriseStatus::Pending;
    }
}

// 把服务端的套餐整数映射为套餐名。文档未定义取值，按约定映射。
CString PlanNameFromInt(int plan)
{
    switch (plan)
    {
    case kPlanProfessional:
        return CString(_T("专业版"));
    case kPlanEnterprise:
        return CString(_T("企业版"));
    case kPlanUltimate:
        return CString(_T("旗舰版"));
    default:
        {
            CString name;
            name.Format(_T("套餐%d"), plan);
            return name;
        }
    }
}

// 从 JSON 对象解析一条企业记录。list.do 的 list 项与 detail.do 的 enterprise
// 对象字段一致，两者共用本函数。
Enterprise EnterpriseFromJson(const Json::Value& item)
{
    Enterprise e;
    // 服务端 enterpriseid 是数字；客户端用十进制字符串作 m_id。
    CString idText;
    idText.Format(_T("%lld"), JsonInt64(item, "enterpriseid"));
    e.m_id        = idText;
    e.m_name      = WideFromUtf8(JsonStr(item, "name"));
    e.m_shortName = WideFromUtf8(JsonStr(item, "shortname"));
    e.m_industry  = WideFromUtf8(JsonStr(item, "industry"));
    e.m_region    = WideFromUtf8(JsonStr(item, "region"));
    e.m_members   = JsonInt(item, "members");
    e.m_depts     = JsonInt(item, "depts");
    e.m_status    = EnterpriseStatusFromInt(JsonInt(item, "status"));
    e.m_plan      = PlanNameFromInt(JsonInt(item, "plan"));
    e.m_createdAt = WideFromUtf8(JsonStr(item, "opendate"));
    e.m_admin     = WideFromUtf8(JsonStr(item, "adminname"));
    e.m_contact   = WideFromUtf8(JsonStr(item, "adminemail"));
    // logocolor 形如 "#rrggbb"；缺失 / 非法时留默认黑色。
    e.m_logoColor = ColorFromHexString(JsonStr(item, "logocolor"), RGB(0, 0, 0));
    // 公司根部门 ID：组织架构树上代表企业本身的那个真实部门。服务端返回 0 表示
    // 该企业尚未建立根部门，这里留空串，组织树会退回旧的合成根节点。
    const long long rootDeptId = JsonInt64(item, "rootdeptid");
    if (rootDeptId != 0)
    {
        CString rootDeptIdText;
        rootDeptIdText.Format(_T("%lld"), rootDeptId);
        e.m_rootDeptId = rootDeptIdText;
    }
    return e;
}

// 把服务端服务号类型整数映射为 ServiceKind。
ServiceKind ServiceKindFromInt(int kind)
{
    return (kind == kSvcKindAssistant) ? ServiceKind::Assistant
                                       : ServiceKind::Notification;
}

// 把服务端服务号状态整数映射为 ServiceStatus。
ServiceStatus ServiceStatusFromInt(int status)
{
    return (status == kSvcStatusDisabled) ? ServiceStatus::Disabled
                                          : ServiceStatus::Active;
}

// 从 JSON 对象解析一条服务号记录（service/list.do 的 list 项）。
// 注：list.do 不返回绑定 / 推送量，m_bindings / m_msgCount / m_subs 留默认。
Service ServiceFromJson(const Json::Value& item)
{
    Service s;
    CString idText;
    idText.Format(_T("%lld"), JsonInt64(item, "serviceid"));
    s.m_id        = idText;
    s.m_name      = WideFromUtf8(JsonStr(item, "name"));
    s.m_glyph     = WideFromUtf8(JsonStr(item, "avatarglyph"));
    s.m_color     = ColorFromHexString(JsonStr(item, "avatarcolor"), RGB(0, 0, 0));
    s.m_kind      = ServiceKindFromInt(JsonInt(item, "kind"));
    s.m_desc      = WideFromUtf8(JsonStr(item, "description"));
    s.m_status    = ServiceStatusFromInt(JsonInt(item, "status"));
    s.m_createdAt = WideFromUtf8(JsonStr(item, "createtime"));
    return s;
}

// 把服务端部门成员状态整数映射为 UserStatus。
UserStatus MemberStatusFromInt(int status)
{
    return (status == kMemberStatusDisabled) ? UserStatus::Disabled
                                             : UserStatus::Active;
}

// 把服务端性别整数映射为中文显示文本。
CString GenderText(int gender)
{
    if (gender == kGenderMale)
    {
        return CString(_T("男"));
    }
    if (gender == kGenderFemale)
    {
        return CString(_T("女"));
    }
    return CString(_T("其他"));
}

// 把 COLORREF 格式化为 "#rrggbb" 颜色串（ColorFromHexString 的逆操作）。
CString HexStringFromColor(COLORREF color)
{
    CString hex;
    hex.Format(_T("#%02x%02x%02x"),
               GetRValue(color), GetGValue(color), GetBValue(color));
    return hex;
}

// 把 ServiceKind 映射回服务端的服务号类型整数。
int ServiceKindToInt(ServiceKind kind)
{
    return (kind == ServiceKind::Assistant) ? kSvcKindAssistant
                                            : kSvcKindNotification;
}

// 把 BindingKind 映射为服务端的绑定类型整数。
int BindingKindToInt(BindingKind kind)
{
    if (kind == BindingKind::Dept)
        return kBindTypeDept;
    if (kind == BindingKind::User)
        return kBindTypeUser;
    return kBindTypeEnterprise;
}

} // anonymous namespace

// ==================== AdminSession ====================

AdminSession::AdminSession()
    : m_adminId(0), m_enterpriseId(0)
{
}

AdminSession& AdminSession::Instance()
{
    static AdminSession s_instance;
    return s_instance;
}

void AdminSession::Set(const CString& token, long long adminId,
                       const CString& name, const CString& email,
                       long long enterpriseId)
{
    m_token        = token;
    m_adminId      = adminId;
    m_adminName    = name;
    m_adminEmail   = email;
    m_enterpriseId = enterpriseId;
}

void AdminSession::Clear()
{
    m_token.Empty();
    m_adminId      = 0;
    m_adminName.Empty();
    m_adminEmail.Empty();
    m_enterpriseId = 0;
}

// ==================== adminapi ====================

namespace adminapi {

// -------------------- 认证 --------------------

HttpRequest MakeLoginRequest(const ServerConfig& server,
                             const CString& email, const CString& password)
{
    Json::Value root;
    root["email"]    = Utf8FromWide(email);
    root["password"] = Utf8FromWide(password);
    return MakeRequest(server, kPathLogin, root, kLoginTimeoutMs);
}

HttpRequest MakeLogoutRequest(const ServerConfig& server, const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathLogout, root, kLogoutTimeoutMs);
}

LoginResult ParseLoginResponse(const HttpResult& http)
{
    LoginResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeLoginError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeLoginError(code);
        return result;
    }

    result.m_token = WideFromUtf8(JsonStr(root, "token"));

    const Json::Value& admin = root["admin"];
    if (admin.isObject())
    {
        result.m_adminId      = JsonInt64(admin, "adminid");
        result.m_adminName    = WideFromUtf8(JsonStr(admin, "name"));
        result.m_adminEmail   = WideFromUtf8(JsonStr(admin, "email"));
        result.m_enterpriseId = JsonInt64(admin, "enterpriseid");
    }

    // 业务码为 0 却没拿到 token，视为应答异常 —— 拒绝当作登录成功。
    if (result.m_token.IsEmpty())
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeLoginError(kErrLocalBadResponse);
    }
    return result;
}

HttpRequest MakeProfileRequest(const ServerConfig& server, const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathProfile, root, kEnterpriseTimeoutMs);
}

ProfileResult ParseProfileResponse(const HttpResult& http)
{
    ProfileResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& admin = root["admin"];
    if (admin.isObject())
    {
        result.m_name  = WideFromUtf8(JsonStr(admin, "name"));
        result.m_email = WideFromUtf8(JsonStr(admin, "email"));
    }
    result.m_scope = WideFromUtf8(JsonStr(root, "scope"));
    return result;
}

HttpRequest MakeUpdateProfileRequest(const ServerConfig& server,
                                     const CString& token, const CString& name)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    root["name"]  = Utf8FromWide(name);
    return MakeRequest(server, kPathUpdateProfile, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeChangePasswordRequest(const ServerConfig& server,
                                      const CString& token,
                                      const CString& oldPassword,
                                      const CString& newPassword)
{
    Json::Value root;
    root["token"]       = Utf8FromWide(token);
    root["oldpassword"] = Utf8FromWide(oldPassword);
    root["newpassword"] = Utf8FromWide(newPassword);
    return MakeRequest(server, kPathChangePassword, root, kEnterpriseTimeoutMs);
}

// -------------------- 通用 --------------------

int PeekBusinessCode(const HttpResult& http)
{
    if (!http.m_transportOk)
        return kErrLocalBadResponse;

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
        return kErrLocalBadResponse;   // 应答非 JSON（如 export 成功时回 CSV）

    return root["code"].isIntegral() ? root["code"].asInt()
                                     : kErrLocalBadResponse;
}

// -------------------- 仪表盘 --------------------

HttpRequest MakeDashboardStatsRequest(const ServerConfig& server,
                                      const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathDashStats, root, kDashboardTimeoutMs);
}

DashboardStatsResult ParseDashboardStatsResponse(const HttpResult& http)
{
    DashboardStatsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    // 6 个统计数都在 stats 子对象里（见 docs/Admin控制台接口文档.md 模块 2）。
    const Json::Value& stats = root["stats"];
    result.m_enterprisesTotal   = JsonInt(stats, "enterprises_total");
    result.m_enterprisesActive  = JsonInt(stats, "enterprises_active");
    result.m_enterprisesPending = JsonInt(stats, "enterprises_pending");
    result.m_usersTotal         = JsonInt(stats, "users_total");
    result.m_auditToday         = JsonInt(stats, "audit_today");
    result.m_auditTodayWarn     = JsonInt(stats, "audit_today_warn");
    return result;
}

HttpRequest MakeMessageTrendRequest(const ServerConfig& server,
                                    const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathMessageTrend, root, kDashboardTimeoutMs);
}

MessageTrendResult ParseMessageTrendResponse(const HttpResult& http)
{
    MessageTrendResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& trend = root["trend"];
    if (trend.isArray())
    {
        result.m_points.reserve(trend.size());
        for (Json::ArrayIndex i = 0; i < trend.size(); ++i)
        {
            const Json::Value& p = trend[i];
            MessageTrendPoint pt;
            pt.m_date  = WideFromUtf8(JsonStr(p, "date"));
            pt.m_count = JsonInt(p, "count");
            result.m_points.push_back(pt);
        }
    }
    return result;
}

HttpRequest MakeUserMessageStatsRequest(const ServerConfig& server,
                                        const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    return MakeRequest(server, kPathUserMessageStats, root, kEnterpriseTimeoutMs);
}

UserMessageStatsResult ParseUserMessageStatsResponse(const HttpResult& http)
{
    UserMessageStatsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& stats = root["stats"];
    result.m_sent          = JsonInt(stats, "sent");
    result.m_received      = JsonInt(stats, "received");
    result.m_conversations = JsonInt(stats, "conversations");

    const Json::Value& trend = root["trend"];
    if (trend.isArray())
    {
        result.m_trend.reserve(trend.size());
        for (Json::ArrayIndex i = 0; i < trend.size(); ++i)
        {
            const Json::Value& p = trend[i];
            MessageTrendPoint pt;
            pt.m_date  = WideFromUtf8(JsonStr(p, "date"));
            pt.m_count = JsonInt(p, "count");
            result.m_trend.push_back(pt);
        }
    }
    return result;
}

// -------------------- 审计日志 --------------------

// 把服务端级别整数映射为客户端 Severity 枚举。服务端审计与公告都用 3 档
// 编码 0/1/2（见 chatserversrc/AdminManager.h、AnnouncementManager.h），与
// 客户端 Severity 枚举值不同序，须显式映射、不能直接强转。
Severity SeverityFromServerCode(int severity)
{
    switch (severity)
    {
    case kSevWarn:
        return Severity::Warn;
    case kSevDanger:
        return Severity::Danger;
    default:
        return Severity::Info;
    }
}

// 从 JSON 对象解析一条审计记录（audit/list.do 的 list 项）。
AuditLog AuditLogFromJson(const Json::Value& item)
{
    AuditLog a;
    a.m_id        = static_cast<int>(JsonInt64(item, "id"));
    a.m_ts        = WideFromUtf8(JsonStr(item, "ts"));
    a.m_actorType = JsonInt(item, "actortype");
    a.m_actor     = WideFromUtf8(JsonStr(item, "actorname"));
    a.m_ip        = WideFromUtf8(JsonStr(item, "ip"));
    a.m_action    = WideFromUtf8(JsonStr(item, "action"));
    a.m_target    = WideFromUtf8(JsonStr(item, "target"));
    a.m_severity  = SeverityFromServerCode(JsonInt(item, "severity"));
    a.m_detail    = WideFromUtf8(JsonStr(item, "detail"));
    return a;
}

HttpRequest MakeAuditListRequest(const ServerConfig& server,
                                 const CString& token, int page, int pageSize,
                                 int actorType, int severity)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    // 操作者类型 / 级别筛选下推服务端；动作 / 企业 / 时间范围本界面不筛。
    root["actortype"]    = actorType;
    root["action"]       = "";
    root["enterpriseid"] = 0;
    root["severity"]     = severity;
    root["starttime"]    = "";
    root["endtime"]      = "";
    root["page"]         = page;
    root["pagesize"]     = pageSize;
    return MakeRequest(server, kPathAuditList, root, kEnterpriseTimeoutMs);
}

AuditListResult ParseAuditListResponse(const HttpResult& http)
{
    AuditListResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_total = JsonInt(root, "total");

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            result.m_list.push_back(AuditLogFromJson(list[i]));
        }
    }
    return result;
}

HttpRequest MakeAuditExportRequest(const ServerConfig& server,
                                   const CString& token,
                                   int actorType, int severity)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    // 按界面当前筛选导出（操作者类型 / 级别）。
    root["actortype"]    = actorType;
    root["action"]       = "";
    root["enterpriseid"] = 0;
    root["severity"]     = severity;
    root["starttime"]    = "";
    root["endtime"]      = "";
    return MakeRequest(server, kPathAuditExport, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeAuditStatsRequest(const ServerConfig& server,
                                  const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathAuditStats, root, kEnterpriseTimeoutMs);
}

AuditStatsResult ParseAuditStatsResponse(const HttpResult& http)
{
    AuditStatsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    // 4 个计数都在 stats 子对象里（见 docs/Admin控制台接口文档.md 模块 8）。
    const Json::Value& stats = root["stats"];
    result.m_today  = JsonInt(stats, "today");
    result.m_total  = JsonInt(stats, "total");
    result.m_warn   = JsonInt(stats, "warn");
    result.m_danger = JsonInt(stats, "danger");
    return result;
}

// -------------------- 公告中心 --------------------

// 公告受众类型整数 → 中文文案（见 chatserversrc/AnnouncementManager.h 编码）。
CString AudienceTypeText(int audienceType)
{
    switch (audienceType)
    {
    case kAudienceEnterprise:
        return CString(_T("指定企业"));
    case kAudienceDept:
        return CString(_T("指定部门"));
    case kAudienceUser:
        return CString(_T("指定员工"));
    case kAudienceInternal:
        return CString(_T("平台内部"));
    case kAudienceAll:
    default:
        return CString(_T("全部企业"));
    }
}

// 从 JSON 对象解析一条公告。list.do 的 list 项是摘要（无 body/totalcount），
// detail.do 的 announcement 是全量 —— 两者共用本函数，缺失字段取默认值。
Announcement AnnouncementFromJson(const Json::Value& item)
{
    Announcement a;
    CString idText;
    idText.Format(_T("%lld"), JsonInt64(item, "announcementid"));
    a.m_id        = idText;
    a.m_title     = WideFromUtf8(JsonStr(item, "title"));
    a.m_body      = WideFromUtf8(JsonStr(item, "body"));        // 仅 detail 含
    a.m_audience  = AudienceTypeText(JsonInt(item, "audiencetype"));
    a.m_severity  = SeverityFromServerCode(JsonInt(item, "severity"));
    a.m_sender    = WideFromUtf8(JsonStr(item, "sendername"));
    a.m_pinned    = (JsonInt(item, "pinned") != 0);
    a.m_scheduled = (JsonInt(item, "scheduled") != 0);
    a.m_status    = JsonInt(item, "status");
    a.m_read      = JsonInt(item, "readcount");
    a.m_total     = JsonInt(item, "totalcount");               // 仅 detail 含
    // 展示时间：已发取 senttime；定时未发取 scheduledtime；否则取创建时间。
    const CString sent  = WideFromUtf8(JsonStr(item, "senttime"));
    const CString sched = WideFromUtf8(JsonStr(item, "scheduledtime"));
    if (!sent.IsEmpty())
    {
        a.m_sentAt = sent;
    }
    else if (!sched.IsEmpty())
    {
        a.m_sentAt = sched;
    }
    else
    {
        a.m_sentAt = WideFromUtf8(JsonStr(item, "createtime"));
    }
    return a;
}

HttpRequest MakeAnnListRequest(const ServerConfig& server, const CString& token,
                               int status, int page, int pageSize)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["status"]   = status;
    root["page"]     = page;
    root["pagesize"] = pageSize;
    return MakeRequest(server, kPathAnnList, root, kEnterpriseTimeoutMs);
}

AnnouncementListResult ParseAnnListResponse(const HttpResult& http)
{
    AnnouncementListResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_total = JsonInt(root, "total");
    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            result.m_list.push_back(AnnouncementFromJson(list[i]));
        }
    }
    return result;
}

HttpRequest MakeAnnDetailRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    return MakeRequest(server, kPathAnnDetail, root, kEnterpriseTimeoutMs);
}

AnnouncementDetailResult ParseAnnDetailResponse(const HttpResult& http)
{
    AnnouncementDetailResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& ann = root["announcement"];
    if (ann.isObject())
    {
        result.m_announcement = AnnouncementFromJson(ann);
        // 编辑回填弹窗需要级别 / 受众类型的原始整数与投放目标。
        result.m_severityCode = JsonInt(ann, "severity");
        result.m_audienceType = JsonInt(ann, "audiencetype");
        const Json::Value& tgts = ann["targets"];
        if (tgts.isArray())
        {
            for (Json::ArrayIndex i = 0; i < tgts.size(); ++i)
            {
                const Json::Value& t = tgts[i];
                result.m_targets.push_back(
                    AnnTarget(JsonInt(t, "type"), JsonInt64(t, "id")));
            }
        }
    }
    return result;
}

HttpRequest MakeAnnRecallRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    return MakeRequest(server, kPathAnnRecall, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeAnnDeleteRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    return MakeRequest(server, kPathAnnDelete, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeAnnSetPinnedRequest(const ServerConfig& server, const CString& token,
                                    long long announcementId, bool pinned)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    root["pinned"]         = pinned;
    return MakeRequest(server, kPathAnnSetPinned, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeAnnSaveRequest(const ServerConfig& server, const CString& token,
                               long long announcementId, const CString& title,
                               const CString& body, int severity, int audienceType,
                               const std::vector<AnnTarget>& targets, bool pinned)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    root["title"]          = Utf8FromWide(title);
    root["body"]           = Utf8FromWide(body);
    root["severity"]       = severity;
    root["audiencetype"]   = audienceType;
    root["pinned"]         = pinned;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < targets.size(); ++i)
    {
        Json::Value t;
        t["type"] = targets[i].m_type;
        t["id"]   = static_cast<Json::Int64>(targets[i].m_id);
        arr.append(t);
    }
    root["targets"] = arr;
    return MakeRequest(server, kPathAnnSave, root, kEnterpriseTimeoutMs);
}

AnnSaveResult ParseAnnSaveResponse(const HttpResult& http)
{
    AnnSaveResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_announcementId = JsonInt64(root, "announcementid");
    return result;
}

HttpRequest MakeAnnSendRequest(const ServerConfig& server, const CString& token,
                               long long announcementId, bool scheduled,
                               const CString& scheduledTime)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    root["scheduled"]      = scheduled;
    root["scheduledtime"]  = Utf8FromWide(scheduledTime);
    return MakeRequest(server, kPathAnnSend, root, kEnterpriseTimeoutMs);
}

// -------------------- 企业管理 --------------------

HttpRequest MakeEnterpriseListRequest(const ServerConfig& server,
                                      const CString& token,
                                      const CString& keyword, int status,
                                      int page, int pagesize)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["keyword"]  = Utf8FromWide(keyword);
    root["status"]   = status;
    root["page"]     = page;
    root["pagesize"] = pagesize;
    return MakeRequest(server, kPathEnterpriseList, root, kEnterpriseTimeoutMs);
}

EnterpriseListResult ParseEnterpriseListResponse(const HttpResult& http)
{
    EnterpriseListResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_total = JsonInt(root, "total");

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& item = list[i];
            if (item.isObject())
                result.m_list.push_back(EnterpriseFromJson(item));
        }
    }
    return result;
}

HttpRequest MakeEnterpriseCreateRequest(const ServerConfig& server,
                                        const CString& token,
                                        const CString& name,
                                        const CString& shortName,
                                        const CString& industry,
                                        const CString& region, int plan,
                                        const CString& adminName,
                                        const CString& adminEmail)
{
    Json::Value root;
    root["token"]      = Utf8FromWide(token);
    root["name"]       = Utf8FromWide(name);
    root["shortname"]  = Utf8FromWide(shortName);
    root["industry"]   = Utf8FromWide(industry);
    root["region"]     = Utf8FromWide(region);
    root["plan"]       = plan;
    root["adminname"]  = Utf8FromWide(adminName);
    root["adminemail"] = Utf8FromWide(adminEmail);
    return MakeRequest(server, kPathEnterpriseCreate, root, kEnterpriseTimeoutMs);
}

EnterpriseCreateResult ParseEnterpriseCreateResponse(const HttpResult& http)
{
    EnterpriseCreateResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_enterpriseId    = JsonInt64(root, "enterpriseid");
    result.m_adminId         = JsonInt64(root, "adminid");
    result.m_initialPassword = WideFromUtf8(JsonStr(root, "initialpassword"));
    return result;
}

HttpRequest MakeEnterpriseSetStatusRequest(const ServerConfig& server,
                                           const CString& token,
                                           long long enterpriseId, int status)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["status"]       = status;
    return MakeRequest(server, kPathEnterpriseStatus, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeEnterpriseDeleteRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return MakeRequest(server, kPathEnterpriseDelete, root, kEnterpriseTimeoutMs);
}

EnterpriseActionResult ParseEnterpriseActionResponse(const HttpResult& http)
{
    EnterpriseActionResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
        result.m_message = DescribeAdminError(code);
    return result;
}

HttpRequest MakeEnterpriseExportRequest(const ServerConfig& server,
                                        const CString& token,
                                        const CString& keyword, int status)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["keyword"] = Utf8FromWide(keyword);
    root["status"]  = status;
    return MakeRequest(server, kPathEnterpriseExport, root, kEnterpriseTimeoutMs);
}

EnterpriseExportResult ParseEnterpriseExportResponse(const HttpResult& http)
{
    EnterpriseExportResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    // 失败时服务端回 JSON {code,msg}；成功时回 CSV 文本。先按 JSON 试探：
    // 若能解析成对象且带 code，即视为错误应答。
    Json::Value root;
    if (ParseJsonObject(http.m_body, root) && root["code"].isIntegral())
    {
        const int code = root["code"].asInt();
        if (code != kErrOk)
        {
            result.m_code    = code;
            result.m_message = DescribeAdminError(code);
            return result;
        }
    }

    // 正常路径：应答体即 CSV。
    result.m_csvUtf8 = http.m_body;
    return result;
}

// -------------------- 企业详情子屏（读取） --------------------

HttpRequest MakeEnterpriseDetailRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return MakeRequest(server, kPathEnterpriseDetail, root, kEnterpriseTimeoutMs);
}

EnterpriseDetailResult ParseEnterpriseDetailResponse(const HttpResult& http)
{
    EnterpriseDetailResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    if (root["enterprise"].isObject())
        result.m_enterprise = EnterpriseFromJson(root["enterprise"]);
    return result;
}

HttpRequest MakeDeptTreeRequest(const ServerConfig& server,
                                const CString& token, long long enterpriseId)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return MakeRequest(server, kPathDeptTree, root, kEnterpriseTimeoutMs);
}

DeptTreeResult ParseDeptTreeResponse(const HttpResult& http, long long rootDeptId,
                                     const CString& enterpriseName)
{
    DeptTreeResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& depts = root["departments"];

    // 把应答里的部门逐条解析成扁平的原始数据；父子挂接、排序、公司根节点的识别
    // 都交给 orgtree::BuildOrgTree，那部分逻辑在 Util/OrgTreeBuilder.cpp 里有单测。
    std::vector<orgtree::OrgDeptRaw> rawDepts;
    if (depts.isArray())
    {
        rawDepts.reserve(depts.size());
    }

    for (Json::ArrayIndex i = 0; depts.isArray() && i < depts.size(); ++i)
    {
        const Json::Value& d = depts[i];
        if (!d.isObject())
            continue;

        const long long deptId   = JsonInt64(d, "deptid");
        const long long leaderId = JsonInt64(d, "leaderuserid");

        orgtree::OrgDeptRaw raw;
        raw.m_deptId       = deptId;
        raw.m_parentDeptId = JsonInt64(d, "parentdeptid");
        raw.m_level        = JsonInt(d, "deptlevel");
        raw.m_order        = JsonInt(d, "deptorder");
        raw.m_name         = WideFromUtf8(JsonStr(d, "deptname"));

        // 解析成员；顺带把负责人姓名解析出来填 m_leader。
        const Json::Value& membersJson = d["members"];
        if (membersJson.isArray())
        {
            for (Json::ArrayIndex k = 0; k < membersJson.size(); ++k)
            {
                const Json::Value& m = membersJson[k];
                if (!m.isObject())
                    continue;

                const long long userId = JsonInt64(m, "userid");
                OrgNode user;
                user.m_kind = OrgNodeKind::User;
                CString userIdText;
                userIdText.Format(_T("%lld"), userId);
                user.m_id           = userIdText;
                user.m_name         = WideFromUtf8(JsonStr(m, "nickname"));
                user.m_title        = WideFromUtf8(JsonStr(m, "position"));
                // 登录账号与工号是两个不同字段：username = 登录凭据，employeeno = HR 工号。
                user.m_loginAccount = WideFromUtf8(JsonStr(m, "username"));
                user.m_employeeNo   = WideFromUtf8(JsonStr(m, "employeeno"));
                user.m_userStatus   = MemberStatusFromInt(JsonInt(m, "status"));
                if (userId == leaderId && !user.m_name.IsEmpty())
                    raw.m_leader = user.m_name;
                raw.m_members.push_back(user);
            }
        }

        rawDepts.push_back(raw);
    }

    result.m_root = orgtree::BuildOrgTree(rawDepts, rootDeptId, enterpriseName);
    return result;
}

HttpRequest MakeServiceListRequest(const ServerConfig& server,
                                   const CString& token,
                                   long long enterpriseId)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return MakeRequest(server, kPathServiceList, root, kEnterpriseTimeoutMs);
}

ServiceListResult ParseServiceListResponse(const HttpResult& http)
{
    ServiceListResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& item = list[i];
            if (item.isObject())
                result.m_list.push_back(ServiceFromJson(item));
        }
    }
    return result;
}

// -------------------- 企业详情子屏：组织架构写操作 --------------------

HttpRequest MakeDeptCreateRequest(const ServerConfig& server,
                                  const CString& token, long long enterpriseId,
                                  long long parentDeptId, const CString& deptName,
                                  const CString& deptDesc, long long leaderUserId,
                                  int deptOrder)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["parentdeptid"] = static_cast<Json::Int64>(parentDeptId);
    root["deptname"]     = Utf8FromWide(deptName);
    root["deptdesc"]     = Utf8FromWide(deptDesc);
    root["leaderuserid"] = static_cast<Json::Int64>(leaderUserId);
    root["deptorder"]    = deptOrder;
    return MakeRequest(server, kPathDeptCreate, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserCreateRequest(const ServerConfig& server,
                                  const CString& token, long long enterpriseId,
                                  long long deptId, const CString& name,
                                  const CString& title, const CString& employeeNo,
                                  const CString& email, const CString& phone,
                                  const CString& joinDate)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["deptid"]       = static_cast<Json::Int64>(deptId);
    root["name"]         = Utf8FromWide(name);
    root["title"]        = Utf8FromWide(title);
    root["employeeno"]   = Utf8FromWide(employeeNo);
    root["email"]        = Utf8FromWide(email);
    root["phone"]        = Utf8FromWide(phone);
    root["joindate"]     = Utf8FromWide(joinDate);
    return MakeRequest(server, kPathUserCreate, root, kEnterpriseTimeoutMs);
}

UserCreateResult ParseUserCreateResponse(const HttpResult& http)
{
    UserCreateResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_userId          = JsonInt64(root, "userid");
    result.m_loginAccount    = WideFromUtf8(JsonStr(root, "username"));
    result.m_initialPassword = WideFromUtf8(JsonStr(root, "initialpassword"));
    return result;
}

HttpRequest MakeGroupCreateRequest(const ServerConfig& server,
                                   const CString& token, long long enterpriseId,
                                   const CString& groupName, long long ownerId,
                                   const std::vector<long long>& adminIds,
                                   const std::vector<long long>& memberIds)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["groupname"]    = Utf8FromWide(groupName);
    //服务端按字符串解析这些 id（getStringId / arr[i].isString()），故 ownerid / adminids /
    //memberids 一律以字符串形式传；纯数字串为 ASCII，无需走宽转 UTF-8。
    root["ownerid"]      = std::to_string(ownerId);

    Json::Value adminArr(Json::arrayValue);
    for (size_t i = 0; i < adminIds.size(); ++i)
        adminArr.append(std::to_string(adminIds[i]));
    root["adminids"] = adminArr;

    Json::Value memberArr(Json::arrayValue);
    for (size_t i = 0; i < memberIds.size(); ++i)
        memberArr.append(std::to_string(memberIds[i]));
    root["memberids"] = memberArr;

    return MakeRequest(server, kPathGroupCreate, root, kEnterpriseTimeoutMs);
}

GroupCreateResult ParseGroupCreateResponse(const HttpResult& http)
{
    GroupCreateResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_groupId = JsonInt64(root, "groupid");
    return result;
}

HttpRequest MakeDeptDeleteRequest(const ServerConfig& server,
                                  const CString& token, long long deptId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["deptid"] = static_cast<Json::Int64>(deptId);
    return MakeRequest(server, kPathDeptDelete, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserDeleteRequest(const ServerConfig& server,
                                  const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    return MakeRequest(server, kPathUserDelete, root, kEnterpriseTimeoutMs);
}

// -------------------- 企业详情子屏：服务号写操作 --------------------

HttpRequest MakeServiceSaveRequest(const ServerConfig& server, const CString& token,
                                   long long serviceId, long long enterpriseId,
                                   const CString& name, ServiceKind kind,
                                   const CString& description, COLORREF avatarColor,
                                   const CString& avatarGlyph,
                                   bool allowUnsubscribe)
{
    Json::Value root;
    root["token"]            = Utf8FromWide(token);
    root["serviceid"]        = static_cast<Json::Int64>(serviceId);
    root["enterpriseid"]     = static_cast<Json::Int64>(enterpriseId);
    root["name"]             = Utf8FromWide(name);
    root["kind"]             = ServiceKindToInt(kind);
    root["description"]      = Utf8FromWide(description);
    root["avatarcolor"]      = Utf8FromWide(HexStringFromColor(avatarColor));
    root["avatarglyph"]      = Utf8FromWide(avatarGlyph);
    root["allowunsubscribe"] = allowUnsubscribe;
    return MakeRequest(server, kPathServiceSave, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeServiceSetStatusRequest(const ServerConfig& server,
                                        const CString& token, long long serviceId,
                                        int status)
{
    Json::Value root;
    root["token"]     = Utf8FromWide(token);
    root["serviceid"] = static_cast<Json::Int64>(serviceId);
    root["status"]    = status;
    return MakeRequest(server, kPathServiceSetStatus, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeServiceDeleteRequest(const ServerConfig& server,
                                     const CString& token, long long serviceId)
{
    Json::Value root;
    root["token"]     = Utf8FromWide(token);
    root["serviceid"] = static_cast<Json::Int64>(serviceId);
    return MakeRequest(server, kPathServiceDelete, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeServiceSetBindingsRequest(
    const ServerConfig& server, const CString& token, long long serviceId,
    const std::vector<ServiceBinding>& bindings)
{
    Json::Value root;
    root["token"]     = Utf8FromWide(token);
    root["serviceid"] = static_cast<Json::Int64>(serviceId);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < bindings.size(); ++i)
    {
        const ServiceBinding& b = bindings[i];
        Json::Value item;
        item["bindtype"] = BindingKindToInt(b.m_kind);
        // 本企业全员的 targetid 无意义置 0；部门 / 员工取其数字 ID。
        item["targetid"] = (b.m_kind == BindingKind::Enterprise)
                               ? static_cast<Json::Int64>(0)
                               : static_cast<Json::Int64>(_ttoi64(b.m_id));
        arr.append(item);
    }
    root["bindings"] = arr;
    return MakeRequest(server, kPathServiceSetBindings, root, kEnterpriseTimeoutMs);
}

// -------------------- 企业详情子屏：角色与权限 --------------------

HttpRequest MakeUserGetPermissionsRequest(const ServerConfig& server,
                                          const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    return MakeRequest(server, kPathUserGetPerms, root, kEnterpriseTimeoutMs);
}

UserPermissionsResult ParseUserPermissionsResponse(const HttpResult& http)
{
    UserPermissionsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_role = JsonInt(root, "role");

    const Json::Value& perms = root["permissions"];
    if (perms.isArray())
    {
        result.m_items.reserve(perms.size());
        for (Json::ArrayIndex i = 0; i < perms.size(); ++i)
        {
            const Json::Value& item = perms[i];
            if (!item.isObject())
                continue;
            PermissionEntry e;
            e.m_key         = WideFromUtf8(JsonStr(item, "key"));
            e.m_effective   = JsonBool(item, "effective");
            e.m_roleDefault = JsonBool(item, "roledefault");
            result.m_items.push_back(e);
        }
    }
    return result;
}

HttpRequest MakeUserSetRoleRequest(const ServerConfig& server,
                                   const CString& token, long long userId,
                                   int role)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    root["role"]   = role;
    return MakeRequest(server, kPathUserSetRole, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserSetPermissionsRequest(
    const ServerConfig& server, const CString& token, long long userId,
    const std::vector<PermOverride>& overrides)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < overrides.size(); ++i)
    {
        Json::Value item;
        item["key"]     = Utf8FromWide(overrides[i].m_key);
        item["granted"] = overrides[i].m_granted;
        arr.append(item);
    }
    root["perms"] = arr;
    return MakeRequest(server, kPathUserSetPerms, root, kEnterpriseTimeoutMs);
}

// -------------------- 企业详情子屏：登录记录与操作历史 --------------------

HttpRequest MakeUserLoginRecordsRequest(const ServerConfig& server,
                                        const CString& token, long long userId,
                                        int page, int pageSize)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["userid"]   = static_cast<Json::Int64>(userId);
    root["page"]     = page;
    root["pagesize"] = pageSize;
    return MakeRequest(server, kPathUserLoginRecords, root, kEnterpriseTimeoutMs);
}

UserLoginRecordsResult ParseUserLoginRecordsResponse(const HttpResult& http)
{
    UserLoginRecordsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    // 当前在线设备。
    const Json::Value& devices = root["devices"];
    if (devices.isArray())
    {
        for (Json::ArrayIndex i = 0; i < devices.size(); ++i)
        {
            const Json::Value& d = devices[i];
            if (!d.isObject())
                continue;
            LoginDevice dev;
            dev.m_clientType = JsonInt(d, "clienttype");
            dev.m_status     = JsonInt(d, "status");
            result.m_devices.push_back(dev);
        }
    }

    // 历史登录记录。
    const Json::Value& records = root["records"];
    if (records.isArray())
    {
        result.m_records.reserve(records.size());
        for (Json::ArrayIndex i = 0; i < records.size(); ++i)
        {
            const Json::Value& r = records[i];
            if (!r.isObject())
                continue;
            LoginRecord rec;
            rec.m_ts         = WideFromUtf8(JsonStr(r, "ts"));
            rec.m_device     = WideFromUtf8(JsonStr(r, "device"));
            rec.m_ip         = WideFromUtf8(JsonStr(r, "ip"));
            rec.m_location   = WideFromUtf8(JsonStr(r, "location"));
            rec.m_result     = JsonInt(r, "result");
            rec.m_failReason = WideFromUtf8(JsonStr(r, "failreason"));
            result.m_records.push_back(rec);
        }
    }
    return result;
}

HttpRequest MakeUserActivityRequest(const ServerConfig& server,
                                    const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    root["type"]   = "all";    // 取全部类型
    root["range"]  = "30d";    // 最近 30 天
    root["page"]   = 1;
    return MakeRequest(server, kPathUserActivity, root, kEnterpriseTimeoutMs);
}

UserActivityResult ParseUserActivityResponse(const HttpResult& http)
{
    UserActivityResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_items.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& a = list[i];
            if (!a.isObject())
                continue;
            ActivityEntry entry;
            entry.m_ts       = WideFromUtf8(JsonStr(a, "ts"));
            entry.m_action   = WideFromUtf8(JsonStr(a, "action"));
            entry.m_detail   = WideFromUtf8(JsonStr(a, "detail"));
            entry.m_severity = JsonInt(a, "severity");
            result.m_items.push_back(entry);
        }
    }
    return result;
}

// -------------------- 企业详情子屏：员工按钮类写操作 --------------------

HttpRequest MakeUserSetStatusRequest(const ServerConfig& server,
                                     const CString& token, long long userId,
                                     int status)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    root["status"] = status;
    return MakeRequest(server, kPathUserSetStatus, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserResetPasswordRequest(const ServerConfig& server,
                                         const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    return MakeRequest(server, kPathUserResetPwd, root, kEnterpriseTimeoutMs);
}

UserResetPwdResult ParseUserResetPasswordResponse(const HttpResult& http)
{
    UserResetPwdResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_newPassword = WideFromUtf8(JsonStr(root, "newpassword"));
    return result;
}

HttpRequest MakeUserKickDeviceRequest(const ServerConfig& server,
                                      const CString& token, long long userId,
                                      const CString& deviceId)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["userid"]   = static_cast<Json::Int64>(userId);
    root["deviceid"] = Utf8FromWide(deviceId);   // 空串 = 注销全部设备
    return MakeRequest(server, kPathUserKickDevice, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserDetailRequest(const ServerConfig& server,
                                  const CString& token, long long userId)
{
    Json::Value root;
    root["token"]  = Utf8FromWide(token);
    root["userid"] = static_cast<Json::Int64>(userId);
    return MakeRequest(server, kPathUserDetail, root, kEnterpriseTimeoutMs);
}

UserDetailResult ParseUserDetailResponse(const HttpResult& http)
{
    UserDetailResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& u = root["user"];
    if (u.isObject())
    {
        OrgNode& node = result.m_user;
        node.m_kind = OrgNodeKind::User;
        CString idText;
        idText.Format(_T("%lld"), JsonInt64(u, "userid"));
        node.m_id          = idText;
        node.m_name        = WideFromUtf8(JsonStr(u, "name"));
        node.m_title       = WideFromUtf8(JsonStr(u, "title"));
        node.m_employeeNo  = WideFromUtf8(JsonStr(u, "employeeno"));
        // 登录账号（= 服务端 f_username），只读展示，不参与提交。
        node.m_loginAccount = WideFromUtf8(JsonStr(u, "username"));
        node.m_email       = WideFromUtf8(JsonStr(u, "email"));
        node.m_phone       = WideFromUtf8(JsonStr(u, "phone"));
        node.m_joined      = WideFromUtf8(JsonStr(u, "joindate"));
        node.m_landline    = WideFromUtf8(JsonStr(u, "landline"));
        node.m_workstation = WideFromUtf8(JsonStr(u, "workstation"));
        node.m_signature   = WideFromUtf8(JsonStr(u, "signature"));
        node.m_gender      = GenderText(JsonInt(u, "gender"));
        node.m_userStatus  = MemberStatusFromInt(JsonInt(u, "status"));
    }
    return result;
}

HttpRequest MakeUserUpdateRequest(const ServerConfig& server,
                                  const CString& token, long long userId,
                                  const CString& name, const CString& title,
                                  const CString& employeeNo,
                                  const CString& joinDate,
                                  const CString& landline,
                                  const CString& workstation,
                                  const CString& phone, const CString& email,
                                  const CString& signature)
{
    Json::Value root;
    root["token"]       = Utf8FromWide(token);
    root["userid"]      = static_cast<Json::Int64>(userId);
    root["name"]        = Utf8FromWide(name);
    root["title"]       = Utf8FromWide(title);
    root["employeeno"]  = Utf8FromWide(employeeNo);
    root["joindate"]    = Utf8FromWide(joinDate);
    root["landline"]    = Utf8FromWide(landline);
    root["workstation"] = Utf8FromWide(workstation);
    root["phone"]       = Utf8FromWide(phone);
    root["email"]       = Utf8FromWide(email);
    root["signature"]   = Utf8FromWide(signature);
    return MakeRequest(server, kPathUserUpdate, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeUserMoveRequest(const ServerConfig& server,
                                const CString& token, long long userId,
                                long long deptId, long long newDeptId)
{
    Json::Value root;
    root["token"]      = Utf8FromWide(token);
    root["userid"]     = static_cast<Json::Int64>(userId);
    root["deptid"]     = static_cast<Json::Int64>(deptId);
    root["newdeptid"]  = static_cast<Json::Int64>(newDeptId);
    return MakeRequest(server, kPathUserMove, root, kEnterpriseTimeoutMs);
}

// -------------------- 企业设置：改企业信息 + 管理员管理 --------------------

HttpRequest MakeEnterpriseUpdateRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId,
                                        const CString& name,
                                        const CString& shortName,
                                        const CString& industry,
                                        const CString& region, int plan)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["name"]         = Utf8FromWide(name);
    root["shortname"]    = Utf8FromWide(shortName);
    root["industry"]     = Utf8FromWide(industry);
    root["region"]       = Utf8FromWide(region);
    root["plan"]         = plan;
    return MakeRequest(server, kPathEntUpdate, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeEnterpriseAdminsRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return MakeRequest(server, kPathEntAdmins, root, kEnterpriseTimeoutMs);
}

EnterpriseAdminsResult ParseEnterpriseAdminsResponse(const HttpResult& http)
{
    EnterpriseAdminsResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& a = list[i];
            if (!a.isObject())
                continue;
            AdminItem item;
            item.m_adminId       = JsonInt64(a, "adminid");
            item.m_name          = WideFromUtf8(JsonStr(a, "name"));
            item.m_email         = WideFromUtf8(JsonStr(a, "email"));
            item.m_status        = JsonInt(a, "status");
            item.m_lastLoginTime = WideFromUtf8(JsonStr(a, "lastlogintime"));
            result.m_list.push_back(item);
        }
    }
    return result;
}

HttpRequest MakeEnterpriseAddAdminRequest(const ServerConfig& server,
                                          const CString& token,
                                          long long enterpriseId,
                                          const CString& name,
                                          const CString& email)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["name"]         = Utf8FromWide(name);
    root["email"]        = Utf8FromWide(email);
    return MakeRequest(server, kPathEntAddAdmin, root, kEnterpriseTimeoutMs);
}

EnterpriseAddAdminResult ParseEnterpriseAddAdminResponse(const HttpResult& http)
{
    EnterpriseAddAdminResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_adminId         = JsonInt64(root, "adminid");
    result.m_initialPassword = WideFromUtf8(JsonStr(root, "initialpassword"));
    return result;
}

HttpRequest MakeEnterpriseRemoveAdminRequest(const ServerConfig& server,
                                             const CString& token,
                                             long long adminId)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["adminid"] = static_cast<Json::Int64>(adminId);
    return MakeRequest(server, kPathEntRemoveAdmin, root, kEnterpriseTimeoutMs);
}


// ===========================================================================
// 客户端版本（模块 10）
// ===========================================================================

// 扫描升级包目录的超时。服务端要把目录里每个文件完整读一遍算 MD5，
// 几百 MB 的包在机械盘上就要好几秒，故比其它接口给得宽。
const int kScanPackagesTimeoutMs = 30000;

// 大小换算的进位基数与各档阈值。用 1024 进制（KB/MB/GB 按二进制口径），
// 与 Windows 资源管理器显示的数值一致，免得管理员对不上。
const long long kSizeUnitKB = 1024LL;
const long long kSizeUnitMB = 1024LL * 1024LL;
const long long kSizeUnitGB = 1024LL * 1024LL * 1024LL;

HttpRequest MakeScanPackagesRequest(const ServerConfig& server, const CString& token)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    return MakeRequest(server, kPathClientVersionScan, root, kScanPackagesTimeoutMs);
}

ScanPackagesResult ParseScanPackagesResponse(const HttpResult& http)
{
    ScanPackagesResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    result.m_dir = WideFromUtf8(JsonStr(root, "dir"));

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& item = list[i];
            UpdatePackageItem pkg;
            pkg.m_fileName = WideFromUtf8(JsonStr(item, "filename"));
            pkg.m_size     = JsonInt64(item, "size");
            pkg.m_md5      = WideFromUtf8(JsonStr(item, "md5"));
            pkg.m_isZip    = JsonBool(item, "iszip");
            pkg.m_used     = JsonBool(item, "used");
            result.m_list.push_back(pkg);
        }
    }
    return result;
}

HttpRequest MakeClientVersionListRequest(const ServerConfig& server,
                                         const CString& token, int platform)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["platform"] = platform;
    return MakeRequest(server, kPathClientVersionList, root, kEnterpriseTimeoutMs);
}

ClientVersionListResult ParseClientVersionListResponse(const HttpResult& http)
{
    ClientVersionListResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        result.m_message = DescribeAdminError(code);
        return result;
    }

    const Json::Value& list = root["list"];
    if (list.isArray())
    {
        result.m_list.reserve(list.size());
        for (Json::ArrayIndex i = 0; i < list.size(); ++i)
        {
            const Json::Value& item = list[i];
            ClientVersionItem v;
            v.m_id             = JsonInt64(item, "id");
            v.m_platform       = JsonInt(item, "platform");
            v.m_version        = WideFromUtf8(JsonStr(item, "version"));
            v.m_minVersion     = WideFromUtf8(JsonStr(item, "minversion"));
            v.m_releaseNote    = WideFromUtf8(JsonStr(item, "releasenote"));
            v.m_md5            = WideFromUtf8(JsonStr(item, "md5"));
            // 包大小必须按 64 位读：升级包超过 2GiB 时 32 位会溢出成负数
            v.m_size           = JsonInt64(item, "size");
            v.m_packageName    = WideFromUtf8(JsonStr(item, "packagename"));
            v.m_forced         = JsonBool(item, "forced");
            v.m_enabled        = JsonInt(item, "enabled");
            v.m_publishTime    = JsonInt64(item, "publishtime");
            v.m_creatorAdminId = JsonInt64(item, "creatoradminid");
            v.m_createTime     = JsonInt64(item, "createtime");
            v.m_isCurrent      = JsonBool(item, "iscurrent");
            result.m_list.push_back(v);
        }
    }
    return result;
}

HttpRequest MakeClientVersionCreateRequest(const ServerConfig& server, const CString& token,
                                           int platform, const CString& version,
                                           const CString& minVersion,
                                           const CString& releaseNote,
                                           const CString& md5, bool forced, bool publish)
{
    Json::Value root;
    root["token"]       = Utf8FromWide(token);
    root["platform"]    = platform;
    root["version"]     = Utf8FromWide(version);
    root["minversion"]  = Utf8FromWide(minVersion);
    root["releasenote"] = Utf8FromWide(releaseNote);
    root["md5"]         = Utf8FromWide(md5);
    root["forced"]      = forced;
    root["publish"]     = publish;
    return MakeRequest(server, kPathClientVersionCreate, root, kEnterpriseTimeoutMs);
}

ClientVersionCreateResult ParseClientVersionCreateResponse(const HttpResult& http)
{
    ClientVersionCreateResult result;

    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return result;
    }
    result.m_transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return result;
    }

    Json::Value root;
    if (!ParseJsonObject(http.m_body, root))
    {
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }

    const int code = root["code"].isIntegral()
                         ? root["code"].asInt() : kErrLocalBadResponse;
    result.m_code = code;
    if (code != kErrOk)
    {
        // 这组接口的失败多半是"包不在目录里"或"选中的不是 zip"，服务端的 msg
        // 比通用错误码描述更具体，有就优先用它
        const CString serverMsg = WideFromUtf8(JsonStr(root, "msg"));
        result.m_message = serverMsg.IsEmpty() ? DescribeAdminError(code) : serverMsg;
        return result;
    }

    result.m_id = JsonInt64(root, "id");
    return result;
}

HttpRequest MakeClientVersionUpdateRequest(const ServerConfig& server, const CString& token,
                                           long long id, const CString& minVersion,
                                           const CString& releaseNote, bool forced)
{
    Json::Value root;
    root["token"]       = Utf8FromWide(token);
    root["id"]          = static_cast<Json::Int64>(id);
    root["minversion"]  = Utf8FromWide(minVersion);
    root["releasenote"] = Utf8FromWide(releaseNote);
    root["forced"]      = forced;
    return MakeRequest(server, kPathClientVersionUpdate, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeClientVersionSetEnabledRequest(const ServerConfig& server, const CString& token,
                                               long long id, bool enabled)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["id"]      = static_cast<Json::Int64>(id);
    root["enabled"] = enabled;
    return MakeRequest(server, kPathClientVersionSetEnabled, root, kEnterpriseTimeoutMs);
}

CString FormatPackageSize(long long bytes)
{
    CString text;
    if (bytes < 0)
        bytes = 0;

    if (bytes >= kSizeUnitGB)
        text.Format(_T("%.2f GB"), static_cast<double>(bytes) / static_cast<double>(kSizeUnitGB));
    else if (bytes >= kSizeUnitMB)
        text.Format(_T("%.1f MB"), static_cast<double>(bytes) / static_cast<double>(kSizeUnitMB));
    else if (bytes >= kSizeUnitKB)
        text.Format(_T("%.1f KB"), static_cast<double>(bytes) / static_cast<double>(kSizeUnitKB));
    else
        text.Format(_T("%lld B"), bytes);

    return text;
}

} // namespace adminapi

} // namespace flamingoadmin
