/**
 *  flamingoAdmin —— 客户端版本管理 API（模块 10）单元测试。
 *
 *  覆盖 6 对「构造请求 / 解析应答」纯函数：
 *    - 请求：路径与请求体字段是否正确，中文与特殊字符能否正确转成 UTF-8；
 *    - 应答：正常解析（含超过 2GiB 的包大小不被截断）、空列表、以及三条通用
 *      失败路径（传输失败 / 状态码异常 / JSON 非法）与业务错误码；
 *    - FormatPackageSize 的分档换算。
 *
 *  全部为纯函数测试，不发起真实网络请求，也不依赖 balloonui。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "json.h"      // 第三方 jsoncpp：把请求体解析回来比对含非 ASCII 的字段

#include <string>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 构造一份测试用的服务器配置。主机与端口不影响被测的纯函数，取固定值即可。
ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

// 构造一个"传输成功、HTTP 200、body 为给定 JSON"的应答，供解析用例使用。
//   json：应答体（UTF-8 JSON 文本）。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 判断请求体里是否含某个片段。请求体是 UTF-8 紧凑 JSON，直接做子串匹配即可。
//   req：待检查的请求。
//   fragment：期望出现的片段（窄字符串）。
bool BodyHas(const HttpRequest& req, const char* fragment)
{
    return req.m_bodyUtf8.find(fragment) != std::string::npos;
}

// 把请求体解析回 Json::Value，用于比对含非 ASCII 的字段。
// 必须这样比而不是在请求体里找中文字节：jsoncpp 默认把非 ASCII 转义成 \uXXXX。
//   req：待检查的请求。
//   out：[出参] 解析结果。
bool ParseJsonBack(const HttpRequest& req, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::CharReader* reader = builder.newCharReader();
    const bool ok = reader->parse(req.m_bodyUtf8.c_str(),
                                  req.m_bodyUtf8.c_str() + req.m_bodyUtf8.size(),
                                  &out, &errs);
    delete reader;
    return ok && errs.empty() && out.isObject();
}

} // namespace

// ---------------------------------------------------------------------------
// 用例 1：扫描升级包目录 —— 请求构造
// ---------------------------------------------------------------------------

void TestScanPackagesRequest()
{
    BeginCase("MakeScanPackagesRequest 路径与请求体");

    const HttpRequest req = MakeScanPackagesRequest(MakeServer(), _T("tok-123"));

    FA_EXPECT_TRUE(req.m_path == L"/admin/clientversion/scanpackages.do");
    FA_EXPECT_TRUE(BodyHas(req, "\"token\":\"tok-123\""));
    // 服务端要把目录里每个文件读一遍算 MD5，超时必须比常规接口宽
    FA_EXPECT_TRUE(req.m_timeoutMs >= 30000);
}

// ---------------------------------------------------------------------------
// 用例 2：扫描升级包目录 —— 应答解析
// ---------------------------------------------------------------------------

void TestScanPackagesResponse()
{
    BeginCase("ParseScanPackagesResponse 正常解析与空列表");

    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"dir\":\"/opt/flamingo/updatecache/\",\"list\":["
        "{\"filename\":\"Flamingo-3.3.0.zip\",\"size\":38219776,"
        "\"md5\":\"9f2c8ab1d4e5f60718293a4b5c6d7e8f\",\"iszip\":true,\"used\":false},"
        "{\"filename\":\"\xE6\x94\xBE\xE9\x94\x99\xE7\x9A\x84.txt\",\"size\":17,"
        "\"md5\":\"293060fec3a8b1c2d3e4f5061728394a\",\"iszip\":false,\"used\":true}]}";

    const ScanPackagesResult r = ParseScanPackagesResponse(MakeHttpOk(json));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_STREQ(r.m_dir, _T("/opt/flamingo/updatecache/"));
    FA_EXPECT_EQ(static_cast<int>(r.m_list.size()), 2);

    if (r.m_list.size() == 2)
    {
        FA_EXPECT_STREQ(r.m_list[0].m_fileName, _T("Flamingo-3.3.0.zip"));
        FA_EXPECT_EQ(static_cast<int>(r.m_list[0].m_size), 38219776);
        FA_EXPECT_STREQ(r.m_list[0].m_md5, _T("9f2c8ab1d4e5f60718293a4b5c6d7e8f"));
        FA_EXPECT_TRUE(r.m_list[0].m_isZip);
        FA_EXPECT_FALSE(r.m_list[0].m_used);

        // 中文文件名要能正确从 UTF-8 转回宽字符
        FA_EXPECT_STREQ(r.m_list[1].m_fileName, _T("放错的.txt"));
        // 放错的文件同样列出来，只是标成不是 zip，好让管理员发现问题
        FA_EXPECT_FALSE(r.m_list[1].m_isZip);
        FA_EXPECT_TRUE(r.m_list[1].m_used);
    }

    // 目录为空：解析成功但列表为空，不该被当成错误
    const ScanPackagesResult empty = ParseScanPackagesResponse(
        MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"dir\":\"/tmp/u/\",\"list\":[]}"));
    FA_EXPECT_TRUE(empty.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(empty.m_list.size()), 0);
}

// ---------------------------------------------------------------------------
// 用例 3：三条通用失败路径
// ---------------------------------------------------------------------------

void TestScanPackagesFailurePaths()
{
    BeginCase("ParseScanPackagesResponse 三条失败路径");

    // 传输失败：把网络层的中文原因带给用户
    HttpResult transportFail;
    transportFail.m_transportOk = false;
    transportFail.m_error = _T("连接被拒绝");
    const ScanPackagesResult r1 = ParseScanPackagesResponse(transportFail);
    FA_EXPECT_FALSE(r1.Succeeded());
    FA_EXPECT_STREQ(r1.m_message, _T("连接被拒绝"));

    // HTTP 状态码异常
    HttpResult badStatus;
    badStatus.m_transportOk = true;
    badStatus.m_statusCode = 500;
    const ScanPackagesResult r2 = ParseScanPackagesResponse(badStatus);
    FA_EXPECT_FALSE(r2.Succeeded());
    FA_EXPECT_FALSE(r2.m_message.IsEmpty());

    // 应答不是合法 JSON
    const ScanPackagesResult r3 = ParseScanPackagesResponse(MakeHttpOk("not a json"));
    FA_EXPECT_FALSE(r3.Succeeded());
    FA_EXPECT_FALSE(r3.m_message.IsEmpty());

    // 业务错误码非 0
    const ScanPackagesResult r4 = ParseScanPackagesResponse(
        MakeHttpOk("{\"code\":2,\"msg\":\"not login or token expired\"}"));
    FA_EXPECT_FALSE(r4.Succeeded());
    FA_EXPECT_EQ(r4.m_code, 2);
    FA_EXPECT_FALSE(r4.m_message.IsEmpty());
}

// ---------------------------------------------------------------------------
// 用例 4：版本记录列表
// ---------------------------------------------------------------------------

void TestClientVersionList()
{
    BeginCase("MakeClientVersionListRequest / ParseClientVersionListResponse");

    const HttpRequest req = MakeClientVersionListRequest(MakeServer(), _T("tok"), 1);
    FA_EXPECT_TRUE(req.m_path == L"/admin/clientversion/list.do");
    FA_EXPECT_TRUE(BodyHas(req, "\"platform\":1"));

    // 平台传 0 表示不过滤
    const HttpRequest reqAll = MakeClientVersionListRequest(MakeServer(), _T("tok"), 0);
    FA_EXPECT_TRUE(BodyHas(reqAll, "\"platform\":0"));

    // 包大小取 3GiB：超过 int32 上界，必须按 64 位读回，不能溢出成负数
    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"list\":["
        "{\"id\":7,\"platform\":1,\"version\":\"3.3.0\",\"minversion\":\"3.1.0\","
        "\"releasenote\":\"\xE6\x96\xB0\xE5\xA2\x9E\xE7\xBE\xA4\xE6\x8E\xA5\xE9\xBE\x99\","
        "\"md5\":\"9f2c8ab1d4e5f60718293a4b5c6d7e8f\",\"size\":3221225472,"
        "\"packagename\":\"Flamingo-3.3.0.zip\",\"enabled\":1,"
        "\"publishtime\":1723363200000,\"creatoradminid\":3,"
        "\"createtime\":1723363100000,\"iscurrent\":true}]}";

    const ClientVersionListResult r = ParseClientVersionListResponse(MakeHttpOk(json));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(r.m_list.size()), 1);

    if (!r.m_list.empty())
    {
        const ClientVersionItem& v = r.m_list[0];
        FA_EXPECT_TRUE(v.m_id == 7);
        FA_EXPECT_EQ(v.m_platform, 1);
        FA_EXPECT_STREQ(v.m_version, _T("3.3.0"));
        FA_EXPECT_STREQ(v.m_minVersion, _T("3.1.0"));
        FA_EXPECT_STREQ(v.m_releaseNote, _T("新增群接龙"));
        FA_EXPECT_STREQ(v.m_packageName, _T("Flamingo-3.3.0.zip"));
        FA_EXPECT_EQ(v.m_enabled, 1);
        FA_EXPECT_TRUE(v.m_isCurrent);
        FA_EXPECT_TRUE(v.m_size == 3221225472LL);
        FA_EXPECT_TRUE(v.m_publishTime == 1723363200000LL);
        FA_EXPECT_TRUE(v.m_createTime == 1723363100000LL);
        FA_EXPECT_TRUE(v.m_creatorAdminId == 3);
    }
}

// ---------------------------------------------------------------------------
// 用例 5：新建版本记录
// ---------------------------------------------------------------------------

void TestClientVersionCreate()
{
    BeginCase("MakeClientVersionCreateRequest / ParseClientVersionCreateResponse");

    const HttpRequest req = MakeClientVersionCreateRequest(
        MakeServer(), _T("tok"), 1, _T("3.3.0"), _T("3.1.0"),
        _T("新增群接龙；修复若干问题"), _T("9f2c8ab1d4e5f60718293a4b5c6d7e8f"), false, true);

    FA_EXPECT_TRUE(req.m_path == L"/admin/clientversion/create.do");
    // 没勾"强制升级"时也要显式带上 false，不能靠服务端猜默认值
    FA_EXPECT_TRUE(BodyHas(req, "\"forced\":false"));
    FA_EXPECT_TRUE(BodyHas(req, "\"platform\":1"));
    FA_EXPECT_TRUE(BodyHas(req, "\"version\":\"3.3.0\""));
    FA_EXPECT_TRUE(BodyHas(req, "\"minversion\":\"3.1.0\""));
    FA_EXPECT_TRUE(BodyHas(req, "\"md5\":\"9f2c8ab1d4e5f60718293a4b5c6d7e8f\""));
    FA_EXPECT_TRUE(BodyHas(req, "\"publish\":true"));

    // 中文说明要能原样送达。注意<u>不能</u>直接在请求体里找中文的 UTF-8 字节：
    // jsoncpp 默认把非 ASCII 转义成 \uXXXX（仍是合法 JSON，服务端解得回来），
    // 故这里把请求体解析回来比对字段值，断言才不受转义形式影响。
    Json::Value parsed;
    FA_EXPECT_TRUE(ParseJsonBack(req, parsed));
    // 解码回来应当是"新增群接龙"那几个字的 UTF-8 字节
    const std::string note = parsed["releasenote"].asString();
    FA_EXPECT_TRUE(note.find("\xE6\x96\xB0\xE5\xA2\x9E\xE7\xBE\xA4\xE6\x8E\xA5\xE9\xBE\x99")
                   != std::string::npos);
    FA_EXPECT_TRUE(parsed["version"].asString() == "3.3.0");
    // 包大小与文件名由服务端按 md5 自行查出，请求体里不该出现它们
    FA_EXPECT_FALSE(BodyHas(req, "\"size\""));
    FA_EXPECT_FALSE(BodyHas(req, "\"packagename\""));

    // 存草稿
    // 存草稿，且勾了强制升级：两个开关互不影响，草稿一样能记住 forced
    const HttpRequest draft = MakeClientVersionCreateRequest(
        MakeServer(), _T("tok"), 1, _T("3.4.0"), _T(""), _T(""), _T("abc"), true, false);
    FA_EXPECT_TRUE(BodyHas(draft, "\"publish\":false"));
    FA_EXPECT_TRUE(BodyHas(draft, "\"forced\":true"));

    // 应答带回新记录 ID
    const ClientVersionCreateResult ok = ParseClientVersionCreateResponse(
        MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":42}"));
    FA_EXPECT_TRUE(ok.Succeeded());
    FA_EXPECT_TRUE(ok.m_id == 42);

    // 失败时优先用服务端给的具体原因，而不是通用错误码描述 ——
    // "包不在目录里" / "选中的不是 zip" 这类提示只有服务端说得清
    const ClientVersionCreateResult failed = ParseClientVersionCreateResponse(
        MakeHttpOk("{\"code\":1,\"msg\":\"selected file is not a zip package\"}"));
    FA_EXPECT_FALSE(failed.Succeeded());
    FA_EXPECT_STREQ(failed.m_message, _T("selected file is not a zip package"));
}

// ---------------------------------------------------------------------------
// 用例 6：修改、发布 / 撤回、回滚
// ---------------------------------------------------------------------------

void TestClientVersionWriteRequests()
{
    BeginCase("修改 / 发布撤回 / 回滚 三个请求的构造");

    const HttpRequest upd = MakeClientVersionUpdateRequest(
        MakeServer(), _T("tok"), 9, _T("3.2.0"), _T("改过的说明"), true);
    FA_EXPECT_TRUE(upd.m_path == L"/admin/clientversion/update.do");
    FA_EXPECT_TRUE(BodyHas(upd, "\"forced\":true"));
    FA_EXPECT_TRUE(BodyHas(upd, "\"id\":9"));
    FA_EXPECT_TRUE(BodyHas(upd, "\"minversion\":\"3.2.0\""));

    const HttpRequest enable = MakeClientVersionSetEnabledRequest(
        MakeServer(), _T("tok"), 9, true);
    FA_EXPECT_TRUE(enable.m_path == L"/admin/clientversion/setenabled.do");
    FA_EXPECT_TRUE(BodyHas(enable, "\"enabled\":true"));

    const HttpRequest disable = MakeClientVersionSetEnabledRequest(
        MakeServer(), _T("tok"), 9, false);
    FA_EXPECT_TRUE(BodyHas(disable, "\"enabled\":false"));
}

// ---------------------------------------------------------------------------
// 用例 7：包大小格式化
// ---------------------------------------------------------------------------

void TestFormatPackageSize()
{
    BeginCase("FormatPackageSize 分档换算");

    FA_EXPECT_STREQ(FormatPackageSize(0), _T("0 B"));
    FA_EXPECT_STREQ(FormatPackageSize(512), _T("512 B"));
    FA_EXPECT_STREQ(FormatPackageSize(1024), _T("1.0 KB"));
    FA_EXPECT_STREQ(FormatPackageSize(1536), _T("1.5 KB"));
    FA_EXPECT_STREQ(FormatPackageSize(1024LL * 1024LL), _T("1.0 MB"));
    FA_EXPECT_STREQ(FormatPackageSize(38219776), _T("36.4 MB"));
    // 3GiB：超过 int32 上界的输入不能溢出
    FA_EXPECT_STREQ(FormatPackageSize(3221225472LL), _T("3.00 GB"));
    // 负数按 0 处理，不产生"-1 B"这种没意义的显示
    FA_EXPECT_STREQ(FormatPackageSize(-1), _T("0 B"));
}

void RunAllClientVersionApiTests()
{
    TestScanPackagesRequest();
    TestScanPackagesResponse();
    TestScanPackagesFailurePaths();
    TestClientVersionList();
    TestClientVersionCreate();
    TestClientVersionWriteRequests();
    TestFormatPackageSize();
}

}} // namespace flamingoadmin::test
