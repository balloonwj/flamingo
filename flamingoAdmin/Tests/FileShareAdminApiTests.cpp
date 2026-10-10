/**
 *  flamingoAdmin —— 文件共享管理（docs/文件共享方案.md 第 10 节，接口见 docs/Admin控制台接口文档.md 第十二章）
 *  的接口封装与显示文字单元测试。
 *
 *    - FA-1：读 / 改功能设置的请求体（平台管理员带 enterpriseid、企业管理员不带）与应答解析（默认值标记）；
 *    - FA-2：共享列表的请求体（企业、关键字、状态、分页）与应答解析（含中文与 64 位 ID）；
 *    - FA-3：成员、强制取消、审计三个接口的请求体与应答解析；
 *    - FA-4：失败路径：传输失败、状态码异常、JSON 非法、会话过期，以及 130～133 四个专属错误码的中文提示；
 *    - FT-1：动作名、来源名、状态、取消原因、共享类型、所在会话；
 *    - FT-2：成员权限文字、字节数换算、操作结果与操作人；
 *    - FT-3：时间范围的起始时间与名称、时间格式。
 *
 *  全部为纯函数测试，不发起真实网络请求。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "Util/FileShareText.h"
#include "json.h"

#include <string>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 测试用的服务器端口（不影响被测的纯函数）
const int kTestPort = 12345;

// 测试用的企业 ID 与共享 ID；共享 ID 取超过 2^53 的值，验证按 64 位读写不丢精度
const long long kEnterpriseId = 7;
const long long kBigShareId = 9007199254740993LL;

// 一天的毫秒数
const long long kDayMs = 24LL * 60 * 60 * 1000;

// 一个确定已经过去的时刻（2023-11-14，Unix 毫秒），用来检查 NowMs 的量级
const long long kPastMs = 1700000000000LL;

ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = kTestPort;
    s.m_useTls = false;
    return s;
}

HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 把请求体解析回 Json::Value（jsoncpp 把非 ASCII 转义成 \uXXXX，中文字段只能解析回来比对）。
bool ParseBody(const HttpRequest& req, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::CharReader* reader = builder.newCharReader();
    const bool ok = reader->parse(req.m_bodyUtf8.c_str(), req.m_bodyUtf8.c_str() + req.m_bodyUtf8.size(),
                                  &out, &errs);
    delete reader;
    return ok && errs.empty() && out.isObject();
}

// 把 UTF-8 字节串转成 CString（比对中文字段用）。
CString Wide(const std::string& utf8)
{
    if (utf8.empty())
        return CString();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    CString w;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.GetBuffer(n), n);
    w.ReleaseBuffer(n);
    return w;
}

} // namespace

// FA-1：读 / 改功能设置
void TestFeatureSettingsApi()
{
    BeginCase("FA-1 功能设置的请求与应答");

    //平台管理员：带 enterpriseid
    Json::Value body;
    HttpRequest req = MakeFeatureSettingsRequest(MakeServer(), _T("tok"), kEnterpriseId);
    FA_EXPECT_TRUE(req.m_path == L"/admin/setting/get.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_TRUE(body["token"].asString() == "tok");
    FA_EXPECT_EQ(static_cast<int>(body["enterpriseid"].asInt64()), static_cast<int>(kEnterpriseId));

    //企业管理员：不带 enterpriseid，由服务端按本企业处理
    req = MakeFeatureSettingsRequest(MakeServer(), _T("tok"), 0);
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));

    //应答：默认值标记
    FeatureSettingsResult r = ParseFeatureSettingsResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":7,"
        "\"settings\":[{\"key\":\"fileshare.enabled\",\"value\":\"1\",\"isdefault\":1}]}"));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(r.m_enterpriseId), static_cast<int>(kEnterpriseId));
    FeatureSettingItem item;
    FA_EXPECT_TRUE(r.Find(kSettingFileShareEnabled, item));
    FA_EXPECT_STREQ(item.m_value, _T("1"));
    FA_EXPECT_TRUE(item.m_isDefault);
    FA_EXPECT_FALSE(r.Find(_T("other.key"), item));

    //有设置记录：isdefault 为 0
    r = ParseFeatureSettingsResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":7,"
        "\"settings\":[{\"key\":\"fileshare.enabled\",\"value\":\"0\",\"isdefault\":0}]}"));
    FA_EXPECT_TRUE(r.Find(kSettingFileShareEnabled, item));
    FA_EXPECT_STREQ(item.m_value, _T("0"));
    FA_EXPECT_FALSE(item.m_isDefault);

    //改设置：请求体带 key 与字符串取值
    req = MakeFeatureSetRequest(MakeServer(), _T("tok"), kEnterpriseId, kSettingFileShareEnabled, _T("0"));
    FA_EXPECT_TRUE(req.m_path == L"/admin/setting/set.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_TRUE(body["key"].asString() == "fileshare.enabled");
    FA_EXPECT_TRUE(body["value"].isString() && body["value"].asString() == "0");
    FA_EXPECT_EQ(static_cast<int>(body["enterpriseid"].asInt64()), static_cast<int>(kEnterpriseId));

    const FeatureSetResult set = ParseFeatureSetResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":7,\"enabled\":0}"));
    FA_EXPECT_TRUE(set.Succeeded());
    FA_EXPECT_FALSE(set.m_enabled);
    const FeatureSetResult setOn = ParseFeatureSetResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":7,\"enabled\":1}"));
    FA_EXPECT_TRUE(setOn.m_enabled);
}

// FA-2：共享列表
void TestFileShareListApi()
{
    BeginCase("FA-2 共享列表的请求与应答");

    Json::Value body;
    HttpRequest req = MakeFileShareListRequest(MakeServer(), _T("tok"), kEnterpriseId, _T("张三"), 1, 3, 50);
    FA_EXPECT_TRUE(req.m_path == L"/admin/fileshare/list.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_STREQ(Wide(body["keyword"].asString()), _T("张三"));
    FA_EXPECT_EQ(body["state"].asInt(), 1);
    FA_EXPECT_EQ(body["page"].asInt(), 3);
    FA_EXPECT_EQ(body["pagesize"].asInt(), 50);
    FA_EXPECT_EQ(static_cast<int>(body["enterpriseid"].asInt64()), static_cast<int>(kEnterpriseId));

    //平台管理员选「全部企业」：不带 enterpriseid（服务端按 0 处理，即全部企业）
    req = MakeFileShareListRequest(MakeServer(), _T("tok"), 0, CString(), 0, 1, 50);
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));

    const FileShareListResult r = ParseFileShareListResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":51,\"list\":[{\"shareid\":9007199254740993,\"enterpriseid\":7,"
        "\"ownerid\":5308,\"ownername\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"owneraccount\":\"MY0308\",\"ctype\":2,"
        "\"cid\":268435470,\"conversationname\":\"\xE9\xA1\xB9\xE7\x9B\xAE\xE7\xBB\x84\",\"kind\":2,"
        "\"name\":\"docs\",\"displaypath\":\"D:\\\\docs\",\"membercount\":3,\"state\":2,\"cancelreason\":3,"
        "\"createtime\":1791000000000,\"canceltime\":1791000600000}]}"));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_EQ(r.m_total, 51);
    FA_EXPECT_EQ(static_cast<int>(r.m_list.size()), 1);
    if (r.m_list.size() == 1)
    {
        const FileShareItem& f = r.m_list[0];
        FA_EXPECT_TRUE(f.m_shareId == kBigShareId);
        FA_EXPECT_EQ(static_cast<int>(f.m_ownerId), 5308);
        FA_EXPECT_STREQ(f.m_ownerName, _T("张三"));
        FA_EXPECT_STREQ(f.m_ownerAccount, _T("MY0308"));
        FA_EXPECT_EQ(f.m_ctype, 2);
        FA_EXPECT_EQ(static_cast<int>(f.m_cid), 268435470);
        FA_EXPECT_STREQ(f.m_conversationName, _T("项目组"));
        FA_EXPECT_EQ(f.m_kind, 2);
        FA_EXPECT_STREQ(f.m_name, _T("docs"));
        FA_EXPECT_STREQ(f.m_displayPath, _T("D:\\docs"));
        FA_EXPECT_EQ(f.m_memberCount, 3);
        FA_EXPECT_EQ(f.m_state, 2);
        FA_EXPECT_EQ(f.m_cancelReason, 3);
        FA_EXPECT_TRUE(f.m_createTime == 1791000000000LL);
        FA_EXPECT_TRUE(f.m_cancelTime == 1791000600000LL);
    }

    //空列表不是错误
    const FileShareListResult empty = ParseFileShareListResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":0,\"list\":[]}"));
    FA_EXPECT_TRUE(empty.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(empty.m_list.size()), 0);
}

// FA-3：成员、强制取消、审计
void TestFileShareDetailApis()
{
    BeginCase("FA-3 成员、强制取消与审计的请求与应答");

    Json::Value body;
    HttpRequest req = MakeFileShareMembersRequest(MakeServer(), _T("tok"), kBigShareId);
    FA_EXPECT_TRUE(req.m_path == L"/admin/fileshare/members.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_TRUE(body["shareid"].asInt64() == kBigShareId);

    const FileShareMembersResult m = ParseFileShareMembersResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"members\":[{\"userid\":5309,\"name\":\"\xE6\x9D\x8E\xE5\x9B\x9B\","
        "\"account\":\"MY0309\",\"perms\":9},{\"userid\":5310,\"name\":\"w\",\"account\":\"MY0310\",\"perms\":0}]}"));
    FA_EXPECT_TRUE(m.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(m.m_members.size()), 2);
    if (m.m_members.size() == 2)
    {
        FA_EXPECT_STREQ(m.m_members[0].m_name, _T("李四"));
        FA_EXPECT_STREQ(m.m_members[0].m_account, _T("MY0309"));
        FA_EXPECT_EQ(m.m_members[0].m_perms, 9);
        FA_EXPECT_EQ(m.m_members[1].m_perms, 0);
    }

    req = MakeFileShareCancelRequest(MakeServer(), _T("tok"), kBigShareId);
    FA_EXPECT_TRUE(req.m_path == L"/admin/fileshare/cancel.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_TRUE(body["shareid"].asInt64() == kBigShareId);

    req = MakeFileShareAuditRequest(MakeServer(), _T("tok"), kEnterpriseId, _T("王五"), 4,
                                    1790000000000LL, 1791000000000LL, kBigShareId, 2, 50);
    FA_EXPECT_TRUE(req.m_path == L"/admin/fileshare/audit.do");
    FA_EXPECT_TRUE(ParseBody(req, body));
    FA_EXPECT_STREQ(Wide(body["actorkeyword"].asString()), _T("王五"));
    FA_EXPECT_EQ(body["action"].asInt(), 4);
    FA_EXPECT_TRUE(body["starttime"].asInt64() == 1790000000000LL);
    FA_EXPECT_TRUE(body["endtime"].asInt64() == 1791000000000LL);
    FA_EXPECT_TRUE(body["shareid"].asInt64() == kBigShareId);
    FA_EXPECT_EQ(body["page"].asInt(), 2);
    FA_EXPECT_EQ(body["pagesize"].asInt(), 50);

    const FileShareAuditListResult a = ParseFileShareAuditResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":["
        "{\"id\":11,\"time\":1791000000000,\"shareid\":9007199254740993,\"sharename\":\"docs\",\"ownerid\":5308,"
        "\"ownername\":\"o\",\"actorid\":5309,\"actorname\":\"a\",\"actorisadmin\":0,\"action\":7,\"source\":2,"
        "\"path\":\"sub/a.txt\",\"topath\":\"b.txt\",\"size\":0,\"result\":0,\"accessid\":\"acc-1\"},"
        "{\"id\":12,\"time\":1791000001000,\"shareid\":1,\"sharename\":\"x\",\"ownerid\":5308,\"ownername\":\"o\","
        "\"actorid\":9901,\"actorname\":\"admin\",\"actorisadmin\":1,\"action\":11,\"source\":1,\"path\":\"\","
        "\"topath\":\"\",\"size\":5000000000,\"result\":810,\"accessid\":\"\"}]}"));
    FA_EXPECT_TRUE(a.Succeeded());
    FA_EXPECT_EQ(a.m_total, 2);
    FA_EXPECT_EQ(static_cast<int>(a.m_list.size()), 2);
    if (a.m_list.size() == 2)
    {
        FA_EXPECT_TRUE(a.m_list[0].m_shareId == kBigShareId);
        FA_EXPECT_FALSE(a.m_list[0].m_actorIsAdmin);
        FA_EXPECT_EQ(a.m_list[0].m_action, 7);
        FA_EXPECT_EQ(a.m_list[0].m_source, 2);
        FA_EXPECT_STREQ(a.m_list[0].m_path, _T("sub/a.txt"));
        FA_EXPECT_STREQ(a.m_list[0].m_toPath, _T("b.txt"));
        FA_EXPECT_STREQ(a.m_list[0].m_accessId, _T("acc-1"));
        FA_EXPECT_TRUE(a.m_list[1].m_actorIsAdmin);
        //超过 4 GiB 的字节数不被截断
        FA_EXPECT_TRUE(a.m_list[1].m_size == 5000000000LL);
        FA_EXPECT_EQ(a.m_list[1].m_result, 810);
    }
}

// FA-4：失败路径与专属错误码
void TestFileShareApiFailures()
{
    BeginCase("FA-4 失败路径与 130～133 的中文提示");

    HttpResult transport;
    transport.m_transportOk = false;
    transport.m_error = _T("连接服务器失败");
    const FileShareListResult t = ParseFileShareListResponse(transport);
    FA_EXPECT_FALSE(t.Succeeded());
    FA_EXPECT_FALSE(t.m_transportOk);
    FA_EXPECT_STREQ(t.m_message, _T("连接服务器失败"));

    HttpResult status = MakeHttpOk("{}");
    status.m_statusCode = 502;
    const FileShareAuditListResult s = ParseFileShareAuditResponse(status);
    FA_EXPECT_FALSE(s.Succeeded());
    FA_EXPECT_TRUE(s.m_transportOk);
    FA_EXPECT_TRUE(s.m_code < 0);

    const FileShareMembersResult bad = ParseFileShareMembersResponse(MakeHttpOk("not json"));
    FA_EXPECT_FALSE(bad.Succeeded());
    FA_EXPECT_TRUE(bad.m_code < 0);

    //会话过期
    const FeatureSettingsResult expired = ParseFeatureSettingsResponse(MakeHttpOk("{\"code\":2,\"msg\":\"x\"}"));
    FA_EXPECT_EQ(expired.m_code, 2);
    FA_EXPECT_STREQ(expired.m_message, _T("登录已过期，请重新登录"));

    //强制取消的应答复用 ParseEnterpriseActionResponse，三个专属错误码各有中文提示
    EnterpriseActionResult c = ParseEnterpriseActionResponse(MakeHttpOk("{\"code\":130,\"msg\":\"x\"}"));
    FA_EXPECT_EQ(c.m_code, 130);
    FA_EXPECT_STREQ(c.m_message, _T("共享不存在"));
    c = ParseEnterpriseActionResponse(MakeHttpOk("{\"code\":131,\"msg\":\"x\"}"));
    FA_EXPECT_STREQ(c.m_message, _T("该共享已经取消过了"));
    c = ParseEnterpriseActionResponse(MakeHttpOk("{\"code\":132,\"msg\":\"x\"}"));
    FA_EXPECT_STREQ(c.m_message, _T("该共享不属于本企业，无权操作"));
    const FeatureSetResult setBad = ParseFeatureSetResponse(MakeHttpOk("{\"code\":133,\"msg\":\"x\"}"));
    FA_EXPECT_FALSE(setBad.Succeeded());
    FA_EXPECT_STREQ(setBad.m_message, _T("功能设置项或取值无效"));
}

// FT-1：动作、来源、状态、原因、类型、会话
void TestFileShareTextNames()
{
    BeginCase("FT-1 动作名、来源名、状态、取消原因、共享类型与所在会话");

    FA_EXPECT_STREQ(fstext::ActionName(fstext::kActionOpen), _T("打开"));
    FA_EXPECT_STREQ(fstext::ActionName(fstext::kActionDownloadFile), _T("下载文件"));
    FA_EXPECT_STREQ(fstext::ActionName(fstext::kActionUpdateMembers), _T("调整成员"));
    FA_EXPECT_STREQ(fstext::ActionName(99), _T("动作 99"));
    //每个动作都有名字，不会落到「动作 N」
    for (int a = 1; a <= fstext::kActionCount; ++a)
        FA_EXPECT_TRUE(fstext::ActionName(a).Find(_T("动作 ")) != 0);

    FA_EXPECT_STREQ(fstext::SourceName(1), _T("服务端记录"));
    FA_EXPECT_STREQ(fstext::SourceName(2), _T("分享者上报"));
    FA_EXPECT_STREQ(fstext::SourceName(0), _T("-"));
    FA_EXPECT_STREQ(fstext::SourceShort(1), _T("服务端"));
    FA_EXPECT_STREQ(fstext::SourceShort(2), _T("分享者"));
    FA_EXPECT_STREQ(fstext::SourceShort(3), _T("-"));

    FA_EXPECT_STREQ(fstext::StateText(fstext::kStateActive), _T("有效"));
    FA_EXPECT_STREQ(fstext::StateText(fstext::kStateCanceled), _T("已取消"));
    FA_EXPECT_STREQ(fstext::StateText(0), _T("未知"));

    FA_EXPECT_STREQ(fstext::CancelReasonText(fstext::kReasonNone), _T("-"));
    FA_EXPECT_STREQ(fstext::CancelReasonText(fstext::kReasonAdminCanceled), _T("管理员在后台强制取消"));
    FA_EXPECT_STREQ(fstext::CancelReasonText(fstext::kReasonOwnerLeftGroup), _T("分享者已退群"));
    FA_EXPECT_STREQ(fstext::CancelReasonText(9), _T("其他原因（9）"));

    FA_EXPECT_STREQ(fstext::KindName(1), _T("整个磁盘"));
    FA_EXPECT_STREQ(fstext::KindName(2), _T("文件夹"));
    FA_EXPECT_STREQ(fstext::KindName(3), _T("单个文件"));
    FA_EXPECT_STREQ(fstext::KindName(0), _T("-"));

    FA_EXPECT_STREQ(fstext::ConversationText(1, _T("李四"), 5309), _T("单聊：李四"));
    FA_EXPECT_STREQ(fstext::ConversationText(2, _T("项目组"), 268435470), _T("群聊：项目组"));
    //会话名称为空时用会话 ID 代替
    FA_EXPECT_STREQ(fstext::ConversationText(2, CString(), 268435470), _T("群聊：268435470"));
}

// FT-2：权限、大小、结果、操作人
void TestFileShareTextValues()
{
    BeginCase("FT-2 成员权限、字节数、操作结果与操作人");

    FA_EXPECT_STREQ(fstext::PermsText(0), _T("只读"));
    FA_EXPECT_STREQ(fstext::PermsText(fstext::kPermUpload | fstext::kPermDelete), _T("上传、删除"));
    FA_EXPECT_STREQ(fstext::PermsText(31), _T("上传、新建文件夹、重命名、删除、移动"));
    //不认识的位忽略
    FA_EXPECT_STREQ(fstext::PermsText(64), _T("只读"));

    FA_EXPECT_STREQ(fstext::FormatSize(0), _T("-"));
    FA_EXPECT_STREQ(fstext::FormatSize(512), _T("512 B"));
    FA_EXPECT_STREQ(fstext::FormatSize(1536), _T("1.5 KB"));
    FA_EXPECT_STREQ(fstext::FormatSize(3LL * 1024 * 1024), _T("3.0 MB"));
    FA_EXPECT_STREQ(fstext::FormatSize(5000000000LL), _T("4.7 GB"));

    FA_EXPECT_STREQ(fstext::ResultShort(0), _T("成功"));
    FA_EXPECT_STREQ(fstext::ResultShort(813), _T("失败"));
    FA_EXPECT_STREQ(fstext::ResultText(0), _T("成功"));
    FA_EXPECT_STREQ(fstext::ResultText(810), _T("失败：无法放入回收站（810）"));
    FA_EXPECT_STREQ(fstext::ResultText(800), _T("失败：功能已被管理员关闭（800）"));
    FA_EXPECT_STREQ(fstext::ResultText(821), _T("失败：成员不属于同一个企业（821）"));
    FA_EXPECT_STREQ(fstext::ResultText(3), _T("失败（错误码 3）"));

    FA_EXPECT_STREQ(fstext::ActorText(_T("张三"), false), _T("张三"));
    FA_EXPECT_STREQ(fstext::ActorText(_T("平台测试管理员"), true), _T("平台测试管理员（管理员）"));
    FA_EXPECT_STREQ(fstext::ActorText(CString(), false), _T("-"));
}

// FT-3：时间范围与时间格式
void TestFileShareTextTime()
{
    BeginCase("FT-3 时间范围与时间格式");

    const long long now = 1791000000000LL;
    FA_EXPECT_TRUE(fstext::RangeStartMs(fstext::kRange7Days, now) == now - 7 * kDayMs);
    FA_EXPECT_TRUE(fstext::RangeStartMs(fstext::kRange30Days, now) == now - 30 * kDayMs);
    FA_EXPECT_TRUE(fstext::RangeStartMs(fstext::kRange180Days, now) == now - 180 * kDayMs);
    FA_EXPECT_TRUE(fstext::RangeStartMs(fstext::kRangeAll, now) == 0);
    //越界按近 7 天
    FA_EXPECT_TRUE(fstext::RangeStartMs(-1, now) == now - 7 * kDayMs);

    FA_EXPECT_STREQ(fstext::RangeName(fstext::kRange7Days), _T("近 7 天"));
    FA_EXPECT_STREQ(fstext::RangeName(fstext::kRange180Days), _T("近 180 天"));
    FA_EXPECT_STREQ(fstext::RangeName(fstext::kRangeAll), _T("全部时间"));

    FA_EXPECT_STREQ(fstext::FormatTime(0), _T("-"));
    //格式固定为 19 个字符「YYYY-MM-DD HH:MM:SS」；具体数字取决于本机时区，不比对
    const CString t = fstext::FormatTime(now);
    FA_EXPECT_EQ(t.GetLength(), 19);
    FA_EXPECT_TRUE(t.Left(3) == _T("202"));
    //当前时间必然晚于 2023-11-14（Unix 毫秒 1700000000000）
    FA_EXPECT_TRUE(fstext::NowMs() > kPastMs);
}

void RunAllFileShareAdminApiTests()
{
    TestFeatureSettingsApi();
    TestFileShareListApi();
    TestFileShareDetailApis();
    TestFileShareApiFailures();
    TestFileShareTextNames();
    TestFileShareTextValues();
    TestFileShareTextTime();
}

}} // namespace flamingoadmin::test
