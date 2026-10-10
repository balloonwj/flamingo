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
const wchar_t* const kPathSensitiveList       = L"/admin/sensitive/list.do";
const wchar_t* const kPathSensitiveAdd        = L"/admin/sensitive/add.do";
const wchar_t* const kPathSensitiveUpdate     = L"/admin/sensitive/update.do";
const wchar_t* const kPathSensitiveDelete     = L"/admin/sensitive/delete.do";
const wchar_t* const kPathSensitiveImport     = L"/admin/sensitive/import.do";
const wchar_t* const kPathSensitiveExport     = L"/admin/sensitive/export.do";
const wchar_t* const kPathSensitiveSetEnabled = L"/admin/sensitive/setenabled.do";
const wchar_t* const kPathSensitiveHits       = L"/admin/sensitive/hits.do";
const wchar_t* const kPathSettingGet          = L"/admin/setting/get.do";
const wchar_t* const kPathSettingSet          = L"/admin/setting/set.do";
const wchar_t* const kPathFileShareList       = L"/admin/fileshare/list.do";
const wchar_t* const kPathFileShareMembers    = L"/admin/fileshare/members.do";
const wchar_t* const kPathFileShareCancel     = L"/admin/fileshare/cancel.do";
const wchar_t* const kPathFileShareAudit      = L"/admin/fileshare/audit.do";
const wchar_t* const kPathAnnList          = L"/admin/announcement/list.do";
const wchar_t* const kPathAnnDetail        = L"/admin/announcement/detail.do";
const wchar_t* const kPathAnnRecall        = L"/admin/announcement/recall.do";
const wchar_t* const kPathAnnRepush        = L"/admin/announcement/repush.do";
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
const wchar_t* const kPathServiceDetail      = L"/admin/service/detail.do";
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
// 敏感词批量导入一批（最多 1000 行）在服务端的一个事务里逐行判重、写库，MySQL 下可能要好几秒，给 30 秒
const int kSensitiveImportTimeoutMs = 30000;

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
// 敏感词库写操作的错误码（doc 模块 11，服务端 chatserversrc/AdminApiCodes.h）。
const int kErrSensitiveInvalid  = 120;  // 词条为空，或去掉空白与符号后为空
const int kErrSensitiveTooLong  = 121;  // 词条超过 32 个字
const int kErrSensitiveExist    = 122;  // 词条已存在（按忽略大小写、全半角、空白与符号后的形式判重）
const int kErrSensitiveFull     = 123;  // 词库已满：每个企业最多 10000 条
const int kErrSensitiveNotExist = 124;  // 词条不存在，或不属于该企业
// 文件共享管理的错误码（doc 模块 12，服务端 chatserversrc/AdminApiCodes.h）。
const int kErrFileShareNotExist  = 130;  // 共享不存在（shareid 无效）
const int kErrFileShareCanceled  = 131;  // 共享已经取消过，不能再强制取消
const int kErrFileShareDenied    = 132;  // 共享不属于企业管理员的本企业
const int kErrFeatureSettingBad  = 133;  // 功能设置项或取值不认识
// 平台公告的错误码（服务端 chatserversrc/AdminApiCodes.h）。
const int kErrAnnNotPublished    = 140;  // 公告不是「已发布」状态（草稿或已撤回），不能重新推送
// 员工与企业资料的错误码（服务端 chatserversrc/AdminApiCodes.h）。此前控制台没有它们的提示，显示为「错误码 N」
const int kErrEmployeeNoExist    = 101;  // 新建或编辑员工时，工号与本企业其他员工重复
const int kErrEmployeeNotExist   = 102;  // 编辑的员工不存在（可能已被删除）
const int kErrEnterpriseNameExist = 119; // 企业全称已被其他企业使用
// 员工手机号与企业账号前缀的错误码（2026-10-06，docs/登录账号与手机号登录方案.md）。
const int kErrPhoneInvalid       = 150;  // 手机号为空，或不是 11 位以 1 开头的数字
const int kErrPhoneTaken         = 151;  // 手机号已被其他账号使用（手机号可用于登录，必须全局唯一）
const int kErrAccountPrefixBad   = 152;  // 企业账号前缀不合法

// 客户端本地错误码：表示「往返成功但应答不合法」（解析失败 / 缺字段 / 状态码
// 异常）。取负值，与服务端 error_code（非负）天然不重叠。
const int kErrLocalBadResponse = -1;

// ---- 企业状态编码（docs/Admin控制台接口文档.md 模块 3.5）----
const int kEntStatusPending   = 0;   // 待审核
const int kEntStatusActive    = 1;   // 活跃
const int kEntStatusSuspended = 2;   // 已停用

// ---- 服务号 / 部门成员编码 ----
const int kSvcKindNotification  = 0;   // 服务号类型：0=通知型
const int kSvcKindAssistant     = 1;   // 服务号类型：1=助手型
const int kSvcKindIntegration   = 2;   // 服务号类型：2=集成型（2026-10-04 服务端加入）
const int kSvcStatusDisabled    = 1;   // 服务号状态：1=停用（其余按正常）
// 员工账号状态：1=停用（其余按在职）。取自 tree.do 成员的 accountstatus 与 user/detail.do 的 status，两者都是 t_user.f_status；
// tree.do 成员的 status 是在线状态，与此无关
const int kMemberStatusDisabled = 1;

// ---- 员工性别编码（user/detail.do · create.do · update.do 的 gender 字段，与服务端
//      chatserversrc/Protocol.h 的 enum profile_gender 一致）----
const int kGenderUnset  = 0;   // 性别：保密（未设置）
const int kGenderMale   = 1;   // 性别：男
const int kGenderFemale = 2;   // 性别：女

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
    //敏感词：添加、修改、导入时词条为空或只有空白与符号
    case kErrSensitiveInvalid:
        return CString(_T("词条为空，或只有空白与符号"));
    //敏感词：词条超过服务端的长度上限
    case kErrSensitiveTooLong:
        return CString(_T("词条超过 32 个字"));
    //敏感词：与已有词条重复（忽略大小写、全半角、空白与符号后相同即算重复）
    case kErrSensitiveExist:
        return CString(_T("词条已存在"));
    //敏感词：该企业的词库已达上限
    case kErrSensitiveFull:
        return CString(_T("词库已满，每个企业最多 10000 条"));
    //敏感词：修改的词条已被删除或不属于该企业
    case kErrSensitiveNotExist:
        return CString(_T("词条不存在，可能已被删除"));
    //文件共享：shareid 无效
    case kErrFileShareNotExist:
        return CString(_T("共享不存在"));
    //文件共享：强制取消一个已经取消过的共享
    case kErrFileShareCanceled:
        return CString(_T("该共享已经取消过了"));
    //文件共享：企业管理员操作别的企业的共享
    case kErrFileShareDenied:
        return CString(_T("该共享不属于本企业，无权操作"));
    //功能设置：设置项或取值不认识（客户端与服务端版本不一致时可能出现）
    case kErrFeatureSettingBad:
        return CString(_T("功能设置项或取值无效"));
    //平台公告：重新推送一条草稿或已撤回的公告
    case kErrAnnNotPublished:
        return CString(_T("该公告不是已发布状态，不能重新推送"));
    //员工：新建或编辑时工号与本企业其他员工重复
    case kErrEmployeeNoExist:
        return CString(_T("工号已被本企业其他员工使用"));
    //员工：编辑的员工已不存在
    case kErrEmployeeNotExist:
        return CString(_T("员工不存在，可能已被删除"));
    //企业：新建或修改时企业全称与别的企业重复
    case kErrEnterpriseNameExist:
        return CString(_T("企业全称已被其他企业使用"));
    //员工：手机号为空或格式不对
    case kErrPhoneInvalid:
        return CString(_T("请填写正确的手机号（11 位，以 1 开头）"));
    //员工：手机号已被其他账号使用
    case kErrPhoneTaken:
        return CString(_T("该手机号已被其他账号使用"));
    //企业：账号前缀不合法
    case kErrAccountPrefixBad:
        return CString(_T("账号前缀只能由字母和点号组成，1～10 个字符"));
    //往返成功但应答不合法（客户端本地错误码）
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
    e.m_plan      = adminapi::PlanNameFromInt(JsonInt(item, "plan"));
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
    // 新建账号的前缀（2026-10-06）：空串表示没设、按默认的 EMP 生成；老服务端不下发时同样为空
    e.m_accountPrefix = WideFromUtf8(JsonStr(item, "accountprefix"));
    return e;
}

// 把服务端服务号类型整数映射为 ServiceKind。
ServiceKind ServiceKindFromInt(int kind)
{
    if (kind == kSvcKindAssistant)
        return ServiceKind::Assistant;
    if (kind == kSvcKindIntegration)
        return ServiceKind::Integration;
    return ServiceKind::Notification;
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

// 把服务端的员工账号状态整数（accountstatus / 员工详情的 status）映射为 UserStatus。
UserStatus MemberStatusFromInt(int status)
{
    return (status == kMemberStatusDisabled) ? UserStatus::Disabled
                                             : UserStatus::Active;
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
    if (kind == ServiceKind::Assistant)
        return kSvcKindAssistant;
    if (kind == ServiceKind::Integration)
        return kSvcKindIntegration;
    return kSvcKindNotification;
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

// 在组织树里按类型与 ID 找节点（深度优先，取第一个匹配项 —— 同一员工挂在多个
// 部门下时各处的姓名相同，取哪一个都一样）。找不到返回 nullptr。
//   node：子树根（可为 nullptr）；kind：要找的节点类型；id：节点 ID。
const OrgNode* FindOrgNodeById(const OrgNode* node, OrgNodeKind kind, const CString& id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->m_kind == kind && node->m_id == id)
    {
        return node;
    }
    for (size_t i = 0; i < node->m_children.size(); ++i)
    {
        const OrgNode* found = FindOrgNodeById(&node->m_children[i], kind, id);
        if (found != nullptr)
        {
            return found;
        }
    }
    return nullptr;
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

    // 管理控制台长连接（FlamingoLink.dll）的端口与服务端公钥；旧版服务端没有这两项时
    // 端口为 0、公钥为空，控制台的「在线用户」页提示服务器未开启实时连接。
    result.m_linkPort   = JsonInt(root, "linkport");
    result.m_linkPubkey = JsonStr(root, "linkpubkey");

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
        result.m_scheduledTime = WideFromUtf8(JsonStr(ann, "scheduledtime"));
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

// 前向声明：定义在本文件后部敏感词一节的匿名命名空间里。它做的是所有接口共有的
// 「传输 / 状态码 / JSON / 业务码」四步检查，公告重新推送与服务号详情的应答解析直接复用。
namespace {
bool ParseSensitivePrelude(const HttpResult& http, bool& transportOk, int& code, CString& message,
                           Json::Value& root);
} // namespace

HttpRequest MakeAnnRepushRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId)
{
    Json::Value root;
    root["token"]          = Utf8FromWide(token);
    root["announcementid"] = static_cast<Json::Int64>(announcementId);
    return MakeRequest(server, kPathAnnRepush, root, kEnterpriseTimeoutMs);
}

AnnRepushResult ParseAnnRepushResponse(const HttpResult& http)
{
    AnnRepushResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
    {
        return result;
    }
    result.m_pushed = JsonInt(root, "pushed");
    return result;
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

const TCHAR* const kAnnSeverityLabels[] =
{
    _T("普通"), _T("提醒"), _T("警告"),
};
const int kAnnSeverityLabelCount = sizeof(kAnnSeverityLabels) / sizeof(kAnnSeverityLabels[0]);

LPCTSTR AnnSeverityLabel(Severity severity)
{
    // 服务端编号 1、2 对应客户端的 Warn、Danger，见 SeverityFromServerCode。
    const int kCodeWarn   = 1;
    const int kCodeDanger = 2;
    switch (severity)
    {
    //提醒级
    case Severity::Warn:
        return kAnnSeverityLabels[kCodeWarn];
    //警告级
    case Severity::Danger:
        return kAnnSeverityLabels[kCodeDanger];
    //普通级：Info 与 Normal 都显示为「普通」
    case Severity::Info:
    case Severity::Normal:
    default:
        return kAnnSeverityLabels[0];
    }
}

const TCHAR* const kPlanOptions[] =
{
    _T("基础版"), _T("专业版"), _T("旗舰版"),
};
const int kPlanOptionCount = sizeof(kPlanOptions) / sizeof(kPlanOptions[0]);

namespace {

// 列表外套餐编号的名称前缀：显示成「套餐N」。
const TCHAR kUnknownPlanPrefix[] = _T("套餐");
const int   kDecimalRadix        = 10;

} // anonymous namespace

CString PlanNameFromInt(int plan)
{
    if (plan >= 0 && plan < kPlanOptionCount)
    {
        return CString(kPlanOptions[plan]);
    }
    CString name;
    name.Format(_T("%s%d"), kUnknownPlanPrefix, plan);
    return name;
}

bool NormalizeAccountPrefix(const CString& raw, CString& out)
{
    CString trimmed = raw;
    trimmed.Trim();
    // 空串：清除前缀、改回默认
    if (trimmed.IsEmpty())
    {
        out.Empty();
        return true;
    }
    if (trimmed.GetLength() > kAccountPrefixMaxLength)
    {
        return false;
    }
    CString upper;
    bool hasLetter = false;
    for (int i = 0; i < trimmed.GetLength(); ++i)
    {
        const TCHAR c = trimmed[i];
        if (c >= _T('a') && c <= _T('z'))
        {
            upper += static_cast<TCHAR>(c - _T('a') + _T('A'));
            hasLetter = true;
        }
        else if (c >= _T('A') && c <= _T('Z'))
        {
            upper += c;
            hasLetter = true;
        }
        else if (c == _T('.'))
        {
            upper += c;
        }
        else
        {
            // 数字、横线、汉字等其它字符一律不合格
            return false;
        }
    }
    if (!hasLetter)
    {
        return false;
    }
    out = upper;
    return true;
}

bool IsValidPhoneNumber(const CString& phone)
{
    if (phone.GetLength() != kPhoneNumberLength || phone[0] != _T('1'))
    {
        return false;
    }
    for (int i = 0; i < phone.GetLength(); ++i)
    {
        if (phone[i] < _T('0') || phone[i] > _T('9'))
        {
            return false;
        }
    }
    return true;
}

int PlanCodeFromName(const CString& name)
{
    for (int i = 0; i < kPlanOptionCount; ++i)
    {
        if (name == kPlanOptions[i])
        {
            return i;
        }
    }
    // 「套餐N」：N 必须是非负整数，前缀之后全是数字。
    const CString prefix(kUnknownPlanPrefix);
    if (name.GetLength() <= prefix.GetLength() || name.Left(prefix.GetLength()) != prefix)
    {
        return -1;
    }
    int value = 0;
    for (int i = prefix.GetLength(); i < name.GetLength(); ++i)
    {
        const TCHAR c = name[i];
        if (c < _T('0') || c > _T('9'))
        {
            return -1;
        }
        value = value * kDecimalRadix + (c - _T('0'));
    }
    return value;
}

const TCHAR* const kIndustryOptions[] =
{
    _T("软件与信息技术"), _T("金融服务"), _T("运输与仓储"),
    _T("生物医药"), _T("广告与设计"), _T("制造业"),
};
const int kIndustryOptionCount = sizeof(kIndustryOptions) / sizeof(kIndustryOptions[0]);

namespace {

// 把「更多筛选」里已设置的条件写进请求体（列表与导出共用）。关键字与状态由调用方写。
//   filter：筛选条件；root：请求体。
void AppendMoreFilters(const EnterpriseListFilter& filter, Json::Value& root)
{
    if (!filter.m_industry.IsEmpty())
    {
        root["industry"] = Utf8FromWide(filter.m_industry);
    }
    if (filter.m_plan >= 0)
    {
        root["plan"] = filter.m_plan;
    }
    if (IsValidDateText(filter.m_openFrom))
    {
        root["openfrom"] = Utf8FromWide(filter.m_openFrom);
    }
    if (IsValidDateText(filter.m_openTo))
    {
        root["opento"] = Utf8FromWide(filter.m_openTo);
    }
}

} // anonymous namespace

HttpRequest MakeEnterpriseListRequest(const ServerConfig& server,
                                      const CString& token,
                                      const CString& keyword, int status,
                                      int page, int pagesize)
{
    EnterpriseListFilter filter;
    filter.m_keyword = keyword;
    filter.m_status  = status;
    return MakeEnterpriseListRequest(server, token, filter, page, pagesize);
}

HttpRequest MakeEnterpriseListRequest(const ServerConfig& server,
                                      const CString& token,
                                      const EnterpriseListFilter& filter,
                                      int page, int pagesize)
{
    Json::Value root;
    root["token"]    = Utf8FromWide(token);
    root["keyword"]  = Utf8FromWide(filter.m_keyword);
    root["status"]   = filter.m_status;
    root["page"]     = page;
    root["pagesize"] = pagesize;
    AppendMoreFilters(filter, root);
    return MakeRequest(server, kPathEnterpriseList, root, kEnterpriseTimeoutMs);
}

bool IsValidDateText(const CString& text)
{
    // 「YYYY-MM-DD」：长度 10，下标 4 与 7 为分隔符。
    const int kDateTextLen = 10;
    const int kDash1 = 4;
    const int kDash2 = 7;
    const int kMonthMax = 12;
    const int kDayMax = 31;
    const int kDecimalBase = 10;
    if (text.GetLength() != kDateTextLen)
    {
        return false;
    }
    for (int i = 0; i < kDateTextLen; ++i)
    {
        const TCHAR c = text[i];
        if (i == kDash1 || i == kDash2)
        {
            if (c != _T('-'))
            {
                return false;
            }
        }
        else if (c < _T('0') || c > _T('9'))
        {
            return false;
        }
    }
    const int month = (text[kDash1 + 1] - _T('0')) * kDecimalBase + (text[kDash1 + 2] - _T('0'));
    const int day   = (text[kDash2 + 1] - _T('0')) * kDecimalBase + (text[kDash2 + 2] - _T('0'));
    return month >= 1 && month <= kMonthMax && day >= 1 && day <= kDayMax;
}

int CountMoreFilters(const EnterpriseListFilter& filter)
{
    int count = 0;
    if (!filter.m_industry.IsEmpty())
    {
        ++count;
    }
    if (filter.m_plan >= 0)
    {
        ++count;
    }
    if (IsValidDateText(filter.m_openFrom))
    {
        ++count;
    }
    if (IsValidDateText(filter.m_openTo))
    {
        ++count;
    }
    return count;
}

CString MoreFilterButtonText(int count)
{
    if (count <= 0)
    {
        return CString(_T("更多筛选"));
    }
    CString text;
    text.Format(_T("更多筛选 · %d"), count);
    return text;
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
    EnterpriseListFilter filter;
    filter.m_keyword = keyword;
    filter.m_status  = status;
    return MakeEnterpriseExportRequest(server, token, filter);
}

HttpRequest MakeEnterpriseExportRequest(const ServerConfig& server,
                                        const CString& token,
                                        const EnterpriseListFilter& filter)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["keyword"] = Utf8FromWide(filter.m_keyword);
    root["status"]  = filter.m_status;
    AppendMoreFilters(filter, root);
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
    std::vector<long long> leaderIds;   // 与 rawDepts 一一对应的负责人 ID，供下面补查姓名
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
                // 是否停用取 accountstatus（账号状态）；status 是在线状态，不能用。
                // 老服务端没有 accountstatus，JsonInt 取到 0，按在职处理
                user.m_userStatus   = MemberStatusFromInt(JsonInt(m, "accountstatus"));
                if (userId == leaderId && !user.m_name.IsEmpty())
                    raw.m_leader = user.m_name;
                raw.m_members.push_back(user);
            }
        }

        rawDepts.push_back(raw);
        leaderIds.push_back(leaderId);
    }

    // 负责人不一定是本部门的成员（新增部门时可以从本企业任何员工里选），本部门成员里查不到
    // 姓名的，再按全企业的成员补查一遍；仍查不到（如负责人已离开企业）时保持为空。
    std::map<long long, CString> memberNames;
    for (size_t i = 0; i < rawDepts.size(); ++i)
    {
        for (size_t k = 0; k < rawDepts[i].m_members.size(); ++k)
        {
            const OrgNode& m = rawDepts[i].m_members[k];
            if (!m.m_name.IsEmpty())
            {
                memberNames[_ttoi64(m.m_id)] = m.m_name;
            }
        }
    }
    for (size_t i = 0; i < rawDepts.size(); ++i)
    {
        if (rawDepts[i].m_leader.IsEmpty() && leaderIds[i] != 0)
        {
            std::map<long long, CString>::const_iterator it = memberNames.find(leaderIds[i]);
            if (it != memberNames.end())
            {
                rawDepts[i].m_leader = it->second;
            }
        }
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


HttpRequest MakeServiceDetailRequest(const ServerConfig& server,
                                     const CString& token,
                                     long long serviceId)
{
    Json::Value root;
    root["token"]     = Utf8FromWide(token);
    root["serviceid"] = static_cast<Json::Int64>(serviceId);
    return MakeRequest(server, kPathServiceDetail, root, kEnterpriseTimeoutMs);
}

ServiceDetailResult ParseServiceDetailResponse(const HttpResult& http)
{
    ServiceDetailResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
    {
        return result;
    }

    const Json::Value& svc = root["service"];
    if (!svc.isObject())
    {
        // 成功应答却缺少 service 对象：按应答格式异常处理，不能当成「没有绑定」，
        // 否则调用方会据此打开一个空的绑定弹窗。
        result.m_code    = kErrLocalBadResponse;
        result.m_message = DescribeAdminError(kErrLocalBadResponse);
        return result;
    }
    result.m_service = ServiceFromJson(svc);

    const Json::Value& arr = svc["bindings"];
    if (arr.isArray())
    {
        result.m_bindings.reserve(arr.size());
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& item = arr[i];
            if (!item.isObject())
            {
                continue;
            }
            RawServiceBinding b;
            b.m_bindType = JsonInt(item, "bindtype");
            b.m_targetId = JsonInt64(item, "targetid");
            result.m_bindings.push_back(b);
        }
    }
    return result;
}

std::vector<ServiceBinding> ResolveServiceBindings(
    const std::vector<RawServiceBinding>& raw, const OrgNode* orgRoot,
    const CString& enterpriseShortName)
{
    std::vector<ServiceBinding> out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const RawServiceBinding& r = raw[i];
        ServiceBinding b;
        if (r.m_bindType == kBindTypeEnterprise)
        {
            // 与绑定弹窗里「整个企业」那一档的 ID 与显示名保持一致，弹窗才能认出它已绑定。
            b.m_kind  = BindingKind::Enterprise;
            b.m_id    = _T("root");
            b.m_label = CString(_T("全员（")) + enterpriseShortName + _T("）");
        }
        else if (r.m_bindType == kBindTypeDept || r.m_bindType == kBindTypeUser)
        {
            const bool isDept = (r.m_bindType == kBindTypeDept);
            b.m_kind = isDept ? BindingKind::Dept : BindingKind::User;
            b.m_id.Format(_T("%lld"), r.m_targetId);
            const OrgNode* node = FindOrgNodeById(
                orgRoot, isDept ? OrgNodeKind::Dept : OrgNodeKind::User, b.m_id);
            if (node != nullptr)
            {
                b.m_label = node->m_name;
            }
            else
            {
                // 组织树尚未加载，或目标已被删除：显示类型与 ID，保证这一项仍可见、可移除。
                b.m_label.Format(isDept ? _T("部门 #%lld") : _T("员工 #%lld"), r.m_targetId);
            }
        }
        else
        {
            continue;
        }
        out.push_back(b);
    }
    return out;
}

void CarryOverLoadedBindings(const std::vector<Service>& oldList,
                             std::vector<Service>& newList)
{
    for (size_t i = 0; i < newList.size(); ++i)
    {
        Service& fresh = newList[i];
        for (size_t j = 0; j < oldList.size(); ++j)
        {
            const Service& known = oldList[j];
            if (known.m_id == fresh.m_id && known.m_bindingsLoaded)
            {
                fresh.m_bindings       = known.m_bindings;
                fresh.m_bindingsLoaded = true;
                // 覆盖人数是按绑定算出的，列表接口同样不返回，随绑定一起沿用。
                fresh.m_subs           = known.m_subs;
                break;
            }
        }
    }
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
                                  const CString& joinDate, int gender)
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
    root["gender"]       = gender;
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
    result.m_kicked = JsonInt(root, "kicked");
    return result;
}

CString FormatResetPasswordNotice(const CString& newPassword, int kicked)
{
    CString msg;
    msg.Format(_T("员工登录密码已重置。\n新密码：%s\n"), static_cast<LPCTSTR>(newPassword));
    //重置时服务端踢掉了该员工的在线设备：告诉管理员，免得员工反映「突然被踢下线」时对不上
    if (kicked > 0)
    {
        CString line;
        line.Format(_T("已强制该员工的 %d 台设备下线。\n"), kicked);
        msg += line;
    }
    msg += _T("请妥善保存并转交，此密码仅显示一次。");
    return msg;
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
        node.m_genderCode  = JsonInt(u, "gender");
        node.m_gender      = GenderDisplayText(node.m_genderCode);
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
                                  const CString& signature, int gender)
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
    root["gender"]      = gender;
    return MakeRequest(server, kPathUserUpdate, root, kEnterpriseTimeoutMs);
}

// 性别下拉框的三项依次是「保密 / 男 / 女」，序号恰好等于编码；仍单独写成映射函数，
// 免得调用方各自假设两者相等。
CString GenderDisplayText(int gender)
{
    if (gender == kGenderMale)
    {
        return CString(_T("男"));
    }
    if (gender == kGenderFemale)
    {
        return CString(_T("女"));
    }
    return CString(_T("保密"));
}

int GenderComboIndexToCode(int index)
{
    if (index == kGenderMale || index == kGenderFemale)
    {
        return index;
    }
    return kGenderUnset;
}

int GenderCodeToComboIndex(int gender)
{
    if (gender == kGenderMale || gender == kGenderFemale)
    {
        return gender;
    }
    return kGenderUnset;
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
                                        const CString& region, int plan,
                                        const CString& accountPrefix)
{
    Json::Value root;
    root["token"]        = Utf8FromWide(token);
    root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    root["name"]         = Utf8FromWide(name);
    root["shortname"]    = Utf8FromWide(shortName);
    root["industry"]     = Utf8FromWide(industry);
    root["region"]       = Utf8FromWide(region);
    root["plan"]         = plan;
    // 账号前缀总是带上：服务端「带了才改」，空串表示清除
    root["accountprefix"] = Utf8FromWide(accountPrefix);
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

namespace {

// 版本号允许的段数与每段最多位数。四段对应资源脚本 FILEVERSION 的 a,b,c,d，每段不超过
// 65535，所以最多 5 位。
const int kVersionMinSegments      = 2;
const int kVersionMaxSegments      = 4;
const int kVersionMaxSegmentDigits = 5;

// 把版本号拆成各段数值；无法识别的字符跳过，空段按 0。
//   text：版本号文字；out：[出参] 各段数值。
void SplitVersionSegments(const CString& text, std::vector<long>& out)
{
    out.clear();
    long cur = 0;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        const TCHAR ch = text[i];
        if (ch == _T('.'))
        {
            out.push_back(cur);
            cur = 0;
        }
        else if (ch >= _T('0') && ch <= _T('9'))
        {
            cur = cur * 10 + (ch - _T('0'));
        }
    }
    out.push_back(cur);
}

} // namespace

bool IsValidVersionText(const CString& text)
{
    if (text.IsEmpty())
    {
        return false;
    }
    int segments = 1;
    int digits   = 0;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        const TCHAR ch = text[i];
        if (ch == _T('.'))
        {
            if (digits == 0)
            {
                return false;   // 空段：开头的点或连续两个点
            }
            ++segments;
            if (segments > kVersionMaxSegments)
            {
                return false;
            }
            digits = 0;
        }
        else if (ch >= _T('0') && ch <= _T('9'))
        {
            ++digits;
            if (digits > kVersionMaxSegmentDigits)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    if (digits == 0)
    {
        return false;   // 结尾的点
    }
    return segments >= kVersionMinSegments;
}

int CompareVersionText(const CString& a, const CString& b)
{
    std::vector<long> sa;
    std::vector<long> sb;
    SplitVersionSegments(a, sa);
    SplitVersionSegments(b, sb);
    const size_t n = (sa.size() > sb.size()) ? sa.size() : sb.size();
    for (size_t i = 0; i < n; ++i)
    {
        const long va = (i < sa.size()) ? sa[i] : 0;
        const long vb = (i < sb.size()) ? sb[i] : 0;
        if (va != vb)
        {
            return (va < vb) ? -1 : 1;
        }
    }
    return 0;
}

CString CheckMinVersion(const CString& minVersion, const CString& version)
{
    CString minText = minVersion;
    minText.Trim();
    if (minText.IsEmpty())
    {
        return CString();
    }
    if (!IsValidVersionText(minText))
    {
        return CString(_T("最低版本格式不正确：应为 2～4 段数字、用点分隔，例如 3.1.0"));
    }
    CString verText = version;
    verText.Trim();
    if (IsValidVersionText(verText) && CompareVersionText(minText, verText) > 0)
    {
        CString msg;
        msg.Format(_T("最低版本 %s 高于版本号 %s，装了这个版本的客户端也会被拒绝登录"),
                   minText.GetString(), verText.GetString());
        return msg;
    }
    return CString();
}

// -------------------- 敏感词（模块 11） --------------------

namespace {

// 敏感词各解析函数共用的前半段。传输失败、HTTP 状态码异常、应答不是 JSON 对象、业务错误码
// 不为 0 这四种情况，填好 transportOk / code / message 后返回 false；返回 true 时 root 是一个
// code 为 0 的应答对象，调用方接着取各自的字段。
//   http：HTTP 往返结果。
//   transportOk / code / message：[出参] 对应结果结构里的同名成员。
//   root：[出参] 解析出的应答对象。
bool ParseSensitivePrelude(const HttpResult& http, bool& transportOk, int& code, CString& message,
                           Json::Value& root)
{
    if (!http.m_transportOk)
    {
        message = http.m_error;
        return false;
    }
    transportOk = true;

    if (http.m_statusCode != kHttpStatusOk)
    {
        code = kErrLocalBadResponse;
        message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return false;
    }

    if (!ParseJsonObject(http.m_body, root))
    {
        code    = kErrLocalBadResponse;
        message = DescribeAdminError(kErrLocalBadResponse);
        return false;
    }

    code = root["code"].isIntegral() ? root["code"].asInt() : kErrLocalBadResponse;
    if (code != kErrOk)
    {
        message = DescribeAdminError(code);
        return false;
    }
    return true;
}

// 敏感词各请求共有的两个字段：token 与 enterpriseid。enterpriseId 不大于 0（企业管理员）时
// 不写 enterpriseid，由服务端改写为本企业。
//   token：登录令牌。enterpriseId：平台超管选中的企业 ID。
Json::Value MakeSensitiveBody(const CString& token, long long enterpriseId)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    if (enterpriseId > 0)
        root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return root;
}

} // namespace

HttpRequest MakeSensitiveListRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int category, int page, int pageSize)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["keyword"]  = Utf8FromWide(keyword);
    root["category"] = category;
    root["page"]     = page;
    root["pagesize"] = pageSize;
    return MakeRequest(server, kPathSensitiveList, root, kEnterpriseTimeoutMs);
}

SensitiveWordListResult ParseSensitiveListResponse(const HttpResult& http)
{
    SensitiveWordListResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_total     = JsonInt(root, "total");
    result.m_enabled   = JsonInt(root, "enabled") != 0;
    result.m_wordCount = JsonInt(root, "wordcount");

    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    result.m_list.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        SensitiveWordItem w;
        //词条 ID 必须按 64 位读：超过 2^53 时按双精度读会丢精度
        w.m_id          = JsonInt64(item, "id");
        w.m_word        = WideFromUtf8(JsonStr(item, "word"));
        w.m_category    = JsonInt(item, "category");
        w.m_remark      = WideFromUtf8(JsonStr(item, "remark"));
        w.m_creatorName = WideFromUtf8(JsonStr(item, "creatorname"));
        w.m_createTime  = JsonInt64(item, "createtime");
        w.m_updateTime  = JsonInt64(item, "updatetime");
        result.m_list.push_back(w);
    }
    return result;
}

HttpRequest MakeSensitiveAddRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                    const CString& word, int category, const CString& remark)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["word"]     = Utf8FromWide(word);
    root["category"] = category;
    root["remark"]   = Utf8FromWide(remark);
    return MakeRequest(server, kPathSensitiveAdd, root, kEnterpriseTimeoutMs);
}

SensitiveWordAddResult ParseSensitiveAddResponse(const HttpResult& http)
{
    SensitiveWordAddResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_id = JsonInt64(root, "id");
    return result;
}

HttpRequest MakeSensitiveUpdateRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       long long id, const CString& word, int category, const CString& remark)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["id"]       = static_cast<Json::Int64>(id);
    root["word"]     = Utf8FromWide(word);
    root["category"] = category;
    root["remark"]   = Utf8FromWide(remark);
    return MakeRequest(server, kPathSensitiveUpdate, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeSensitiveDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       const std::vector<long long>& ids)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(static_cast<Json::Int64>(ids[i]));
    root["ids"] = arr;
    return MakeRequest(server, kPathSensitiveDelete, root, kEnterpriseTimeoutMs);
}

SensitiveWordDeleteResult ParseSensitiveDeleteResponse(const HttpResult& http)
{
    SensitiveWordDeleteResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_deleted = JsonInt(root, "deleted");
    return result;
}

HttpRequest MakeSensitiveImportRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       const std::vector<std::string>& linesUtf8, int defaultCategory)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < linesUtf8.size(); ++i)
        arr.append(linesUtf8[i]);
    root["lines"]    = arr;
    root["category"] = defaultCategory;
    return MakeRequest(server, kPathSensitiveImport, root, kSensitiveImportTimeoutMs);
}

SensitiveImportResult ParseSensitiveImportResponse(const HttpResult& http)
{
    SensitiveImportResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_added = JsonInt(root, "added");
    const Json::Value& skipped = root["skipped"];
    if (!skipped.isArray())
        return result;
    result.m_skipped.reserve(skipped.size());
    for (Json::ArrayIndex i = 0; i < skipped.size(); ++i)
    {
        const Json::Value& item = skipped[i];
        if (!item.isObject())
            continue;
        SensitiveImportSkip s;
        s.m_line       = JsonInt(item, "line");
        s.m_word       = WideFromUtf8(JsonStr(item, "word"));
        s.m_reason     = JsonInt(item, "reason");
        //跳过原因与添加单条时的失败提示是同一组错误码，提示文字也用同一份
        s.m_reasonText = DescribeAdminError(s.m_reason);
        result.m_skipped.push_back(s);
    }
    return result;
}

HttpRequest MakeSensitiveExportRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathSensitiveExport, MakeSensitiveBody(token, enterpriseId), kEnterpriseTimeoutMs);
}

HttpRequest MakeSensitiveSetEnabledRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                           bool enabled)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["enabled"] = enabled;
    return MakeRequest(server, kPathSensitiveSetEnabled, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeSensitiveHitsRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int source, int category,
                                     long long beginTime, long long endTime, int page, int pageSize)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["keyword"]   = Utf8FromWide(keyword);
    root["source"]    = source;
    root["category"]  = category;
    root["begintime"] = static_cast<Json::Int64>(beginTime);
    root["endtime"]   = static_cast<Json::Int64>(endTime);
    root["page"]      = page;
    root["pagesize"]  = pageSize;
    return MakeRequest(server, kPathSensitiveHits, root, kEnterpriseTimeoutMs);
}

SensitiveHitListResult ParseSensitiveHitsResponse(const HttpResult& http)
{
    SensitiveHitListResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_total = JsonInt(root, "total");
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    result.m_list.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        SensitiveHitItem h;
        h.m_id         = JsonInt64(item, "id");
        h.m_time       = JsonInt64(item, "time");
        h.m_userId     = JsonInt64(item, "userid");
        h.m_userName   = WideFromUtf8(JsonStr(item, "username"));
        h.m_source     = JsonInt(item, "source");
        h.m_ctype      = JsonInt(item, "ctype");
        h.m_cid        = JsonInt64(item, "cid");
        h.m_targetName = WideFromUtf8(JsonStr(item, "targetname"));
        h.m_word       = WideFromUtf8(JsonStr(item, "word"));
        h.m_category   = JsonInt(item, "category");
        h.m_snippet    = WideFromUtf8(JsonStr(item, "snippet"));
        result.m_list.push_back(h);
    }
    return result;
}

// ==================== 文件共享（模块 12）====================

HttpRequest MakeFeatureSettingsRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    //与敏感词接口相同：企业 ID 不大于 0（企业管理员）时不带 enterpriseid
    return MakeRequest(server, kPathSettingGet, MakeSensitiveBody(token, enterpriseId), kEnterpriseTimeoutMs);
}

FeatureSettingsResult ParseFeatureSettingsResponse(const HttpResult& http)
{
    FeatureSettingsResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_enterpriseId = JsonInt64(root, "enterpriseid");
    if (root.isMember("cloudrecordusedbytes"))
        result.m_cloudRecordUsedBytes = JsonInt64(root, "cloudrecordusedbytes");
    const Json::Value& list = root["settings"];
    if (!list.isArray())
        return result;
    result.m_settings.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        FeatureSettingItem s;
        s.m_key       = WideFromUtf8(JsonStr(item, "key"));
        s.m_value     = WideFromUtf8(JsonStr(item, "value"));
        s.m_isDefault = JsonBool(item, "isdefault");
        result.m_settings.push_back(s);
    }
    return result;
}

HttpRequest MakeFeatureSetRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                  const CString& key, const CString& value)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["key"]   = Utf8FromWide(key);
    root["value"] = Utf8FromWide(value);
    return MakeRequest(server, kPathSettingSet, root, kEnterpriseTimeoutMs);
}

FeatureSetResult ParseFeatureSetResponse(const HttpResult& http)
{
    FeatureSetResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_enterpriseId = JsonInt64(root, "enterpriseid");
    result.m_enabled      = JsonBool(root, "enabled");
    return result;
}

HttpRequest MakeFileShareListRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int state, int page, int pageSize)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["keyword"]  = Utf8FromWide(keyword);
    root["state"]    = state;
    root["page"]     = page;
    root["pagesize"] = pageSize;
    return MakeRequest(server, kPathFileShareList, root, kEnterpriseTimeoutMs);
}

FileShareListResult ParseFileShareListResponse(const HttpResult& http)
{
    FileShareListResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_total = JsonInt(root, "total");
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    result.m_list.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        FileShareItem f;
        f.m_shareId          = JsonInt64(item, "shareid");
        f.m_enterpriseId     = JsonInt64(item, "enterpriseid");
        f.m_ownerId          = JsonInt64(item, "ownerid");
        f.m_ownerName        = WideFromUtf8(JsonStr(item, "ownername"));
        f.m_ownerAccount     = WideFromUtf8(JsonStr(item, "owneraccount"));
        f.m_ctype            = JsonInt(item, "ctype");
        f.m_cid              = JsonInt64(item, "cid");
        f.m_conversationName = WideFromUtf8(JsonStr(item, "conversationname"));
        f.m_kind             = JsonInt(item, "kind");
        f.m_name             = WideFromUtf8(JsonStr(item, "name"));
        f.m_displayPath      = WideFromUtf8(JsonStr(item, "displaypath"));
        f.m_memberCount      = JsonInt(item, "membercount");
        f.m_state            = JsonInt(item, "state");
        f.m_cancelReason     = JsonInt(item, "cancelreason");
        f.m_createTime       = JsonInt64(item, "createtime");
        f.m_cancelTime       = JsonInt64(item, "canceltime");
        result.m_list.push_back(f);
    }
    return result;
}

HttpRequest MakeFileShareMembersRequest(const ServerConfig& server, const CString& token, long long shareId)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["shareid"] = static_cast<Json::Int64>(shareId);
    return MakeRequest(server, kPathFileShareMembers, root, kEnterpriseTimeoutMs);
}

FileShareMembersResult ParseFileShareMembersResponse(const HttpResult& http)
{
    FileShareMembersResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    const Json::Value& list = root["members"];
    if (!list.isArray())
        return result;
    result.m_members.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        FileShareMemberItem m;
        m.m_userId  = JsonInt64(item, "userid");
        m.m_name    = WideFromUtf8(JsonStr(item, "name"));
        m.m_account = WideFromUtf8(JsonStr(item, "account"));
        m.m_perms   = JsonInt(item, "perms");
        result.m_members.push_back(m);
    }
    return result;
}

HttpRequest MakeFileShareCancelRequest(const ServerConfig& server, const CString& token, long long shareId)
{
    Json::Value root;
    root["token"]   = Utf8FromWide(token);
    root["shareid"] = static_cast<Json::Int64>(shareId);
    return MakeRequest(server, kPathFileShareCancel, root, kEnterpriseTimeoutMs);
}

HttpRequest MakeFileShareAuditRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                      const CString& actorKeyword, int action, long long startTime,
                                      long long endTime, long long shareId, int page, int pageSize)
{
    Json::Value root = MakeSensitiveBody(token, enterpriseId);
    root["actorkeyword"] = Utf8FromWide(actorKeyword);
    root["action"]       = action;
    root["starttime"]    = static_cast<Json::Int64>(startTime);
    root["endtime"]      = static_cast<Json::Int64>(endTime);
    root["shareid"]      = static_cast<Json::Int64>(shareId);
    root["page"]         = page;
    root["pagesize"]     = pageSize;
    return MakeRequest(server, kPathFileShareAudit, root, kEnterpriseTimeoutMs);
}

FileShareAuditListResult ParseFileShareAuditResponse(const HttpResult& http)
{
    FileShareAuditListResult result;
    Json::Value root;
    if (!ParseSensitivePrelude(http, result.m_transportOk, result.m_code, result.m_message, root))
        return result;

    result.m_total = JsonInt(root, "total");
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    result.m_list.reserve(list.size());
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isObject())
            continue;
        FileShareAuditItem a;
        a.m_id           = JsonInt64(item, "id");
        a.m_time         = JsonInt64(item, "time");
        a.m_shareId      = JsonInt64(item, "shareid");
        a.m_shareName    = WideFromUtf8(JsonStr(item, "sharename"));
        a.m_ownerId      = JsonInt64(item, "ownerid");
        a.m_ownerName    = WideFromUtf8(JsonStr(item, "ownername"));
        a.m_actorId      = JsonInt64(item, "actorid");
        a.m_actorName    = WideFromUtf8(JsonStr(item, "actorname"));
        a.m_actorIsAdmin = JsonBool(item, "actorisadmin");
        a.m_action       = JsonInt(item, "action");
        a.m_source       = JsonInt(item, "source");
        a.m_path         = WideFromUtf8(JsonStr(item, "path"));
        a.m_toPath       = WideFromUtf8(JsonStr(item, "topath"));
        a.m_size         = JsonInt64(item, "size");
        a.m_result       = JsonInt(item, "result");
        a.m_accessId     = WideFromUtf8(JsonStr(item, "accessid"));
        result.m_list.push_back(a);
    }
    return result;
}

} // namespace adminapi

} // namespace flamingoadmin
