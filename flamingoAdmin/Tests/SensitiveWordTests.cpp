/**
 *  flamingoAdmin —— 敏感词管理（docs/敏感词方案.md 第 7 节）单元测试。
 *
 *  覆盖 Util/AdminApi 里模块 11（敏感词）八个接口的封装：
 *    - SA-1：请求构造（路径、整数字段按 JSON 整数编码、中文往返、enterpriseid 为 0 时不带）；
 *    - SA-2：应答解析（列表、添加、删除、导入、拦截记录；120～124 的中文提示；三条通用失败路径；
 *            缺字段时取默认值）。
 *  以及 Util/SensitiveWordText 的纯文本逻辑：
 *    - SA-3：导入文件解码（UTF-8 BOM、UTF-16 LE / BE BOM、无 BOM 的 UTF-8、GBK、空文件、
 *            字节数为奇数的 UTF-16）；
 *    - SA-4：切行与分批（换行符混用、末尾换行、中间空行、首尾空白；按 1000 行分批与行号换算；
 *            全是空白行的批次不提交）；
 *    - SA-5：导出存盘格式（BOM、CRLF）与「导出后再按导入流程读回」逐行一致；
 *    - SA-6：分类名与拦截来源名。
 *
 *  全部为纯函数测试，不发起网络请求，也不依赖 balloonui。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "Util/SensitiveWordText.h"
#include "json.h"      // 第三方 jsoncpp：把请求体解析回来逐字段比对

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::swtext;
using namespace flamingoadmin::adminapi;

// 测试用的企业ID与一个超过 2^53 的词条ID（检查 64 位整数不被截断）
const long long kEnterpriseId = 9001;
const long long kBigId = 9007199254740993LL;

// 构造一份测试用的服务器配置。主机与端口不影响被测的纯函数，取固定值即可。
ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

// 构造一个「传输成功、HTTP 200、body 为给定 JSON」的应答，供解析用例使用。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 把请求体解析回 Json::Value。jsoncpp 默认把非 ASCII 转义成 \uXXXX，中文字段要解析回来才能比对。
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

// 判断 JSON 成员是否为指定值的整数（JSON 整数，而不是字符串）。
bool IsIntField(const Json::Value& obj, const char* key, long long expected)
{
    return obj[key].isIntegral() && obj[key].asInt64() == expected;
}

// UTF-8 的 BOM
const char* const kUtf8Bom = "\xEF\xBB\xBF";

// 「敏感」两个字的 GBK 编码（敏 = C3 F4，感 = B8 D0）。这四个字节不是合法的 UTF-8
// （C3 之后跟的 F4 不是续字节），用来触发 GBK 分支
const char* const kGbkMinGan = "\xC3\xF4\xB8\xD0";

// 全角空格 U+3000 的 UTF-8 编码
const char* const kIdeographicSpace = "\xE3\x80\x80";

// 把宽字符串编成 UTF-16 字节，前面加上对应的 BOM。
//   text：文字。
//   bigEndian：true 编成大端，false 编成小端。
std::string MakeUtf16Bytes(const wchar_t* text, bool bigEndian)
{
    std::string bytes;
    bytes += bigEndian ? "\xFE\xFF" : "\xFF\xFE";
    for (const wchar_t* p = text; *p != L'\0'; ++p)
    {
        const char high = static_cast<char>((static_cast<unsigned int>(*p) >> 8) & 0xFF);
        const char low  = static_cast<char>(static_cast<unsigned int>(*p) & 0xFF);
        if (bigEndian)
        {
            bytes += high;
            bytes += low;
        }
        else
        {
            bytes += low;
            bytes += high;
        }
    }
    return bytes;
}

// 造 count 行「w1」「w2」……的 UTF-8 行，行号从 first 起。
std::vector<std::string> MakeWordLines(int first, int count)
{
    std::vector<std::string> lines;
    for (int i = 0; i < count; ++i)
        lines.push_back("w" + std::to_string(first + i));
    return lines;
}

} // namespace

// ---------------------------------------------------------------------------
// SA-3：导入文件解码
// ---------------------------------------------------------------------------

void TestDecodeImportFile()
{
    BeginCase("SA-3 导入文件解码：按 BOM 识别，没有 BOM 时先试 UTF-8 再按 GBK");

    TextEncoding enc = TextEncoding::Gbk;
    FA_EXPECT_STREQ(DecodeImportFile(std::string(kUtf8Bom) + u8"敏感\t违法违规", &enc), L"敏感\t违法违规");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf8Bom);

    FA_EXPECT_STREQ(DecodeImportFile(MakeUtf16Bytes(L"敏感词\r\n第二行", false), &enc), L"敏感词\r\n第二行");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf16Le);

    FA_EXPECT_STREQ(DecodeImportFile(MakeUtf16Bytes(L"敏感词\r\n第二行", true), &enc), L"敏感词\r\n第二行");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf16Be);

    FA_EXPECT_STREQ(DecodeImportFile(u8"敏感\nabc", &enc), L"敏感\nabc");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf8);

    //纯 ASCII 也是合法 UTF-8
    FA_EXPECT_STREQ(DecodeImportFile("abc", &enc), L"abc");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf8);

    FA_EXPECT_STREQ(DecodeImportFile(std::string(kGbkMinGan) + "\r\n", &enc), L"敏感\r\n");
    FA_EXPECT_TRUE(enc == TextEncoding::Gbk);

    //空文件：空串，编码记为 UTF-8；出参可以传 NULL
    FA_EXPECT_STREQ(DecodeImportFile(std::string(), &enc), L"");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf8);
    FA_EXPECT_STREQ(DecodeImportFile("abc", NULL), L"abc");

    //只有 BOM：空串
    FA_EXPECT_STREQ(DecodeImportFile(kUtf8Bom, &enc), L"");

    //UTF-16 的字节数为奇数（文件被截断）：舍去最后一个字节，不崩溃
    std::string odd = MakeUtf16Bytes(L"AB", false);
    odd.resize(odd.size() - 1);
    FA_EXPECT_STREQ(DecodeImportFile(odd, &enc), L"A");
    FA_EXPECT_TRUE(enc == TextEncoding::Utf16Le);
}

// ---------------------------------------------------------------------------
// SA-4：切行与分批
// ---------------------------------------------------------------------------

void TestSplitLines()
{
    BeginCase("SA-4 切行：换行符混用、末尾换行、中间空行与首尾空白保留在原位");

    std::vector<std::string> lines = SplitLines(L"a\r\nb\nc\rd");
    FA_EXPECT_EQ(lines.size(), 4);
    if (lines.size() == 4)
    {
        FA_EXPECT_TRUE(lines[0] == "a");
        FA_EXPECT_TRUE(lines[1] == "b");
        FA_EXPECT_TRUE(lines[2] == "c");
        FA_EXPECT_TRUE(lines[3] == "d");
    }

    //末尾换行之后的空串不算一行；中间的空行与首尾空白原样保留，行号才能与原文对齐
    lines = SplitLines(L"a\r\n\r\n  b \r\n");
    FA_EXPECT_EQ(lines.size(), 3);
    if (lines.size() == 3)
    {
        FA_EXPECT_TRUE(lines[0] == "a");
        FA_EXPECT_TRUE(lines[1].empty());
        FA_EXPECT_TRUE(lines[2] == "  b ");
    }

    //中文转成 UTF-8，Tab 原样保留
    lines = SplitLines(L"敏感\t违法违规\t备注");
    FA_EXPECT_EQ(lines.size(), 1);
    if (lines.size() == 1)
        FA_EXPECT_TRUE(lines[0] == std::string(u8"敏感\t违法违规\t备注"));

    FA_EXPECT_EQ(SplitLines(L"").size(), 0);
    //只有一个换行：第一行是空行
    FA_EXPECT_EQ(SplitLines(L"\n").size(), 1);
}

void TestBlankLines()
{
    BeginCase("SA-4 空白行：ASCII 空白与全角空格都算空白，口径与服务端一致");

    FA_EXPECT_TRUE(IsBlankLine(""));
    FA_EXPECT_TRUE(IsBlankLine(" \t "));
    FA_EXPECT_TRUE(IsBlankLine(std::string(kIdeographicSpace) + " " + kIdeographicSpace));
    FA_EXPECT_FALSE(IsBlankLine(" a "));
    FA_EXPECT_FALSE(IsBlankLine(u8"　敏感　"));

    std::vector<std::string> lines;
    lines.push_back("a");
    lines.push_back("");
    lines.push_back("  ");
    lines.push_back(kIdeographicSpace);
    lines.push_back("b");
    FA_EXPECT_EQ(CountNonBlankLines(lines), 2);
    FA_EXPECT_EQ(CountNonBlankLines(std::vector<std::string>()), 0);
}

void TestImportBatches()
{
    BeginCase("SA-4 分批：每批 1000 行，记下每批首行在原文里的行号");

    std::vector<ImportBatch> batches = MakeImportBatches(MakeWordLines(1, 2500), kImportBatchMaxLines);
    FA_EXPECT_EQ(batches.size(), 3);
    if (batches.size() == 3)
    {
        FA_EXPECT_EQ(batches[0].m_firstLine, 1);
        FA_EXPECT_EQ(batches[1].m_firstLine, 1001);
        FA_EXPECT_EQ(batches[2].m_firstLine, 2001);
        FA_EXPECT_EQ(batches[0].m_lines.size(), 1000);
        FA_EXPECT_EQ(batches[1].m_lines.size(), 1000);
        FA_EXPECT_EQ(batches[2].m_lines.size(), 500);
        FA_EXPECT_TRUE(batches[1].m_lines[0] == "w1001");
        FA_EXPECT_TRUE(batches[2].m_lines[499] == "w2500");
    }

    //中间一整批都是空白行：不提交，但其后批次的首行行号仍按原文计算
    std::vector<std::string> lines = MakeWordLines(1, 1000);
    for (int i = 0; i < 1000; ++i)
        lines.push_back(i % 2 == 0 ? "" : " ");
    std::vector<std::string> tail = MakeWordLines(2001, 5);
    lines.insert(lines.end(), tail.begin(), tail.end());
    batches = MakeImportBatches(lines, kImportBatchMaxLines);
    FA_EXPECT_EQ(batches.size(), 2);
    if (batches.size() == 2)
    {
        FA_EXPECT_EQ(batches[0].m_firstLine, 1);
        FA_EXPECT_EQ(batches[1].m_firstLine, 2001);
        FA_EXPECT_EQ(batches[1].m_lines.size(), 5);
    }

    //空输入、全是空白行：没有批次
    FA_EXPECT_EQ(MakeImportBatches(std::vector<std::string>(), kImportBatchMaxLines).size(), 0);
    FA_EXPECT_EQ(MakeImportBatches(std::vector<std::string>(3, " "), kImportBatchMaxLines).size(), 0);

    //每批行数不大于 0 时按 kImportBatchMaxLines 处理
    batches = MakeImportBatches(MakeWordLines(1, 1500), 0);
    FA_EXPECT_EQ(batches.size(), 2);
}

// ---------------------------------------------------------------------------
// SA-5：导出存盘格式与读回
// ---------------------------------------------------------------------------

void TestExportRoundTrip()
{
    BeginCase("SA-5 导出存盘：带 BOM、换行为 CRLF，按导入流程读回后逐行一致");

    const std::string exported = u8"敏感\t违法违规\t备注一\n广告\t广告引流\t\n纯词条\t其他\t\n";
    const std::string bytes = BuildExportFileBytes(exported);

    FA_EXPECT_TRUE(bytes.compare(0, 3, kUtf8Bom) == 0);
    //每个 \n 前面都是 \r，且一共三处
    int crlf = 0;
    bool bareLf = false;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (bytes[i] != '\n')
            continue;
        if (i > 0 && bytes[i - 1] == '\r')
            ++crlf;
        else
            bareLf = true;
    }
    FA_EXPECT_EQ(crlf, 3);
    FA_EXPECT_FALSE(bareLf);

    TextEncoding enc = TextEncoding::Gbk;
    const std::vector<std::string> back = SplitLines(DecodeImportFile(bytes, &enc));
    FA_EXPECT_TRUE(enc == TextEncoding::Utf8Bom);
    FA_EXPECT_EQ(back.size(), 3);
    if (back.size() == 3)
    {
        FA_EXPECT_TRUE(back[0] == std::string(u8"敏感\t违法违规\t备注一"));
        FA_EXPECT_TRUE(back[1] == std::string(u8"广告\t广告引流\t"));
        FA_EXPECT_TRUE(back[2] == std::string(u8"纯词条\t其他\t"));
    }

    //原文已经是 CRLF 的不会变成 \r\r\n
    FA_EXPECT_TRUE(BuildExportFileBytes("a\r\nb\n") == std::string(kUtf8Bom) + "a\r\nb\r\n");
    //空导出：只有 BOM
    FA_EXPECT_TRUE(BuildExportFileBytes("") == std::string(kUtf8Bom));
}

// ---------------------------------------------------------------------------
// SA-6：分类名与拦截来源名
// ---------------------------------------------------------------------------

void TestNames()
{
    BeginCase("SA-6 分类名与拦截来源名：与服务端取值一一对应，不认识的取值有兜底文字");

    FA_EXPECT_STREQ(CategoryName(1), L"违法违规");
    FA_EXPECT_STREQ(CategoryName(2), L"色情低俗");
    FA_EXPECT_STREQ(CategoryName(3), L"攻击辱骂");
    FA_EXPECT_STREQ(CategoryName(4), L"商业机密");
    FA_EXPECT_STREQ(CategoryName(5), L"广告引流");
    FA_EXPECT_STREQ(CategoryName(kCategoryOther), L"其他");
    FA_EXPECT_STREQ(CategoryName(0), L"其他");
    FA_EXPECT_STREQ(CategoryName(kCategoryCount + 1), L"其他");

    FA_EXPECT_STREQ(HitSourceName(1), L"文字消息");
    FA_EXPECT_STREQ(HitSourceName(2), L"转发");
    FA_EXPECT_STREQ(HitSourceName(3), L"文件名");
    FA_EXPECT_STREQ(HitSourceName(4), L"卡片与接龙");
    FA_EXPECT_STREQ(HitSourceName(5), L"个人资料");
    FA_EXPECT_STREQ(HitSourceName(6), L"群资料");
    FA_EXPECT_STREQ(HitSourceName(7), L"群名片");
    FA_EXPECT_STREQ(HitSourceName(8), L"加群申请");
    FA_EXPECT_STREQ(HitSourceName(9), L"禁言原因");
    FA_EXPECT_STREQ(HitSourceName(10), L"文件夹名");
    FA_EXPECT_STREQ(HitSourceName(11), L"自助注册");
    FA_EXPECT_STREQ(HitSourceName(12), L"群公告");
    FA_EXPECT_STREQ(HitSourceName(13), L"屏幕共享");
    FA_EXPECT_STREQ(HitSourceName(14), L"群备忘");
    FA_EXPECT_STREQ(HitSourceName(15), L"文件共享");
    FA_EXPECT_STREQ(HitSourceName(16), L"云端录像");
    FA_EXPECT_STREQ(HitSourceName(17), L"群问答");
    //2026-10-08 起补上帖子群与机器人的四个来源（服务端 sensitive_source_thread_post 18 ～ sensitive_source_bot_external 21）
    FA_EXPECT_STREQ(HitSourceName(18), L"帖子");
    FA_EXPECT_STREQ(HitSourceName(19), L"帖子留言");
    FA_EXPECT_STREQ(HitSourceName(20), L"机器人配置");
    FA_EXPECT_STREQ(HitSourceName(kHitSourceCount), L"机器人外部内容");
    FA_EXPECT_STREQ(HitSourceName(0), L"未知来源");
    FA_EXPECT_STREQ(HitSourceName(kHitSourceCount + 1), L"未知来源");
}

// ---------------------------------------------------------------------------
// SA-1：八个请求的构造
// ---------------------------------------------------------------------------

void TestSensitiveRequests()
{
    BeginCase("SA-1 八个请求的构造：路径、整数字段按 JSON 整数编码、中文原样往返");

    const ServerConfig server = MakeServer();
    Json::Value body;

    HttpRequest req = MakeSensitiveListRequest(server, _T("tok"), kEnterpriseId, L"敏感", 5, 2, 50);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/list.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(body["token"].asString() == "tok");
    FA_EXPECT_TRUE(IsIntField(body, "enterpriseid", kEnterpriseId));
    FA_EXPECT_TRUE(body["keyword"].asString() == std::string(u8"敏感"));
    FA_EXPECT_TRUE(IsIntField(body, "category", 5));
    FA_EXPECT_TRUE(IsIntField(body, "page", 2));
    FA_EXPECT_TRUE(IsIntField(body, "pagesize", 50));

    //企业管理员传 0：请求里不带 enterpriseid，由服务端改写为本企业
    req = MakeSensitiveListRequest(server, _T("tok"), 0, L"", 0, 1, 50);
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));

    req = MakeSensitiveAddRequest(server, _T("tok"), kEnterpriseId, L"新词", 1, L"备注");
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/add.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(IsIntField(body, "enterpriseid", kEnterpriseId));
    FA_EXPECT_TRUE(body["word"].asString() == std::string(u8"新词"));
    FA_EXPECT_TRUE(IsIntField(body, "category", 1));
    FA_EXPECT_TRUE(body["remark"].asString() == std::string(u8"备注"));

    req = MakeSensitiveUpdateRequest(server, _T("tok"), kEnterpriseId, kBigId, L"改后", 3, L"");
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/update.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(IsIntField(body, "id", kBigId));
    FA_EXPECT_TRUE(body["word"].asString() == std::string(u8"改后"));
    FA_EXPECT_TRUE(IsIntField(body, "category", 3));
    FA_EXPECT_TRUE(body["remark"].isString() && body["remark"].asString().empty());

    std::vector<long long> ids;
    ids.push_back(3);
    ids.push_back(kBigId);
    req = MakeSensitiveDeleteRequest(server, _T("tok"), kEnterpriseId, ids);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/delete.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(body["ids"].isArray() && body["ids"].size() == 2);
    if (body["ids"].isArray() && body["ids"].size() == 2)
    {
        FA_EXPECT_TRUE(body["ids"][0].isIntegral() && body["ids"][0].asInt64() == 3);
        FA_EXPECT_TRUE(body["ids"][1].isIntegral() && body["ids"][1].asInt64() == kBigId);
    }

    std::vector<std::string> lines;
    lines.push_back(u8"敏感\t违法违规\t备注");
    lines.push_back("");
    lines.push_back(u8"广告");
    req = MakeSensitiveImportRequest(server, _T("tok"), kEnterpriseId, lines, kCategoryOther);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/import.do");
    //一次提交上千行、在一个事务里写库，超时比常规接口宽
    FA_EXPECT_TRUE(req.m_timeoutMs >= 30000);
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(IsIntField(body, "category", kCategoryOther));
    FA_EXPECT_TRUE(body["lines"].isArray() && body["lines"].size() == 3);
    if (body["lines"].isArray() && body["lines"].size() == 3)
    {
        //空行原样提交，服务端报回的行号才与原文对齐
        FA_EXPECT_TRUE(body["lines"][0].asString() == lines[0]);
        FA_EXPECT_TRUE(body["lines"][1].isString() && body["lines"][1].asString().empty());
        FA_EXPECT_TRUE(body["lines"][2].asString() == lines[2]);
    }

    req = MakeSensitiveExportRequest(server, _T("tok"), kEnterpriseId);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/export.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(IsIntField(body, "enterpriseid", kEnterpriseId));

    req = MakeSensitiveSetEnabledRequest(server, _T("tok"), kEnterpriseId, false);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/setenabled.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(body["enabled"].isBool() && !body["enabled"].asBool());
    req = MakeSensitiveSetEnabledRequest(server, _T("tok"), kEnterpriseId, true);
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(body["enabled"].isBool() && body["enabled"].asBool());

    const long long beginTime = 1700000000000LL;
    const long long endTime = 1700086400000LL;
    req = MakeSensitiveHitsRequest(server, _T("tok"), kEnterpriseId, L"张", 3, 2, beginTime, endTime, 1, 20);
    FA_EXPECT_TRUE(req.m_path == L"/admin/sensitive/hits.do");
    FA_EXPECT_TRUE(ParseJsonBack(req, body));
    FA_EXPECT_TRUE(body["keyword"].asString() == std::string(u8"张"));
    FA_EXPECT_TRUE(IsIntField(body, "source", 3));
    FA_EXPECT_TRUE(IsIntField(body, "category", 2));
    FA_EXPECT_TRUE(IsIntField(body, "begintime", beginTime));
    FA_EXPECT_TRUE(IsIntField(body, "endtime", endTime));
    FA_EXPECT_TRUE(IsIntField(body, "page", 1));
    FA_EXPECT_TRUE(IsIntField(body, "pagesize", 20));
}

// ---------------------------------------------------------------------------
// SA-2：应答解析
// ---------------------------------------------------------------------------

void TestSensitiveListResponse()
{
    BeginCase("SA-2 词库列表、添加、删除的解析：字段正确，64 位 ID 不被截断");

    const std::string json = std::string("{\"code\":0,\"msg\":\"ok\",\"total\":2,\"enabled\":1,\"wordcount\":1234,")
        + u8"\"list\":[{\"id\":9007199254740993,\"word\":\"敏感\",\"category\":1,\"remark\":\"备注\","
          u8"\"creatorname\":\"管理员\",\"createtime\":1700000000000,\"updatetime\":1700000000001},"
          u8"{\"id\":2,\"word\":\"广告\",\"category\":5,\"remark\":\"\",\"creatorname\":\"\","
          u8"\"createtime\":1,\"updatetime\":2}]}";
    const SensitiveWordListResult r = ParseSensitiveListResponse(MakeHttpOk(json));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_EQ(r.m_total, 2);
    FA_EXPECT_TRUE(r.m_enabled);
    FA_EXPECT_EQ(r.m_wordCount, 1234);
    FA_EXPECT_EQ(r.m_list.size(), 2);
    if (r.m_list.size() == 2)
    {
        FA_EXPECT_TRUE(r.m_list[0].m_id == kBigId);
        FA_EXPECT_STREQ(r.m_list[0].m_word, L"敏感");
        FA_EXPECT_EQ(r.m_list[0].m_category, 1);
        FA_EXPECT_STREQ(r.m_list[0].m_remark, L"备注");
        FA_EXPECT_STREQ(r.m_list[0].m_creatorName, L"管理员");
        FA_EXPECT_TRUE(r.m_list[0].m_createTime == 1700000000000LL);
        FA_EXPECT_TRUE(r.m_list[0].m_updateTime == 1700000000001LL);
        FA_EXPECT_STREQ(r.m_list[1].m_word, L"广告");
    }

    //总开关关闭
    const SensitiveWordListResult off = ParseSensitiveListResponse(
        MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"total\":0,\"enabled\":0,\"wordcount\":0,\"list\":[]}"));
    FA_EXPECT_TRUE(off.Succeeded());
    FA_EXPECT_FALSE(off.m_enabled);

    const SensitiveWordAddResult add = ParseSensitiveAddResponse(
        MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":9007199254740993}"));
    FA_EXPECT_TRUE(add.Succeeded());
    FA_EXPECT_TRUE(add.m_id == kBigId);

    const SensitiveWordDeleteResult del = ParseSensitiveDeleteResponse(
        MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"deleted\":3}"));
    FA_EXPECT_TRUE(del.Succeeded());
    FA_EXPECT_EQ(del.m_deleted, 3);
}

void TestSensitiveImportAndHitsResponse()
{
    BeginCase("SA-2 导入结果与拦截记录的解析：跳过的行带中文原因，拦截记录字段齐全");

    const SensitiveImportResult imp = ParseSensitiveImportResponse(MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"added\":5,\"skipped\":["
        u8"{\"line\":2,\"word\":\"重复词\",\"reason\":122},{\"line\":7,\"word\":\"!!!\",\"reason\":120}]}"));
    FA_EXPECT_TRUE(imp.Succeeded());
    FA_EXPECT_EQ(imp.m_added, 5);
    FA_EXPECT_EQ(imp.m_skipped.size(), 2);
    if (imp.m_skipped.size() == 2)
    {
        FA_EXPECT_EQ(imp.m_skipped[0].m_line, 2);
        FA_EXPECT_STREQ(imp.m_skipped[0].m_word, L"重复词");
        FA_EXPECT_EQ(imp.m_skipped[0].m_reason, 122);
        FA_EXPECT_STREQ(imp.m_skipped[0].m_reasonText, L"词条已存在");
        FA_EXPECT_EQ(imp.m_skipped[1].m_line, 7);
        FA_EXPECT_STREQ(imp.m_skipped[1].m_reasonText, L"词条为空，或只有空白与符号");
    }

    const SensitiveHitListResult hits = ParseSensitiveHitsResponse(MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"total\":1,\"list\":[{\"id\":9007199254740993,\"time\":1700000000000,"
        u8"\"userid\":101,\"username\":\"张三\",\"source\":1,\"ctype\":2,\"cid\":268435457,"
        u8"\"targetname\":\"项目组\",\"word\":\"敏感\",\"category\":1,\"snippet\":\"前文敏感后文\"}]}"));
    FA_EXPECT_TRUE(hits.Succeeded());
    FA_EXPECT_EQ(hits.m_total, 1);
    FA_EXPECT_EQ(hits.m_list.size(), 1);
    if (hits.m_list.size() == 1)
    {
        const SensitiveHitItem& h = hits.m_list[0];
        FA_EXPECT_TRUE(h.m_id == kBigId);
        FA_EXPECT_TRUE(h.m_time == 1700000000000LL);
        FA_EXPECT_TRUE(h.m_userId == 101);
        FA_EXPECT_STREQ(h.m_userName, L"张三");
        FA_EXPECT_EQ(h.m_source, 1);
        FA_EXPECT_EQ(h.m_ctype, 2);
        FA_EXPECT_TRUE(h.m_cid == 268435457LL);
        FA_EXPECT_STREQ(h.m_targetName, L"项目组");
        FA_EXPECT_STREQ(h.m_word, L"敏感");
        FA_EXPECT_EQ(h.m_category, 1);
        FA_EXPECT_STREQ(h.m_snippet, L"前文敏感后文");
    }
}

void TestSensitiveErrorsAndMissingFields()
{
    BeginCase("SA-2 错误码 120～124 的中文提示、三条通用失败路径、应答缺字段");

    //120～124：词库写操作的业务错误码，各有一句中文提示
    const int codes[] = { 120, 121, 122, 123, 124 };
    const wchar_t* const messages[] =
    {
        L"词条为空，或只有空白与符号",
        L"词条超过 32 个字",
        L"词条已存在",
        L"词库已满，每个企业最多 10000 条",
        L"词条不存在，可能已被删除",
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
        const SensitiveWordAddResult r = ParseSensitiveAddResponse(
            MakeHttpOk("{\"code\":" + std::to_string(codes[i]) + ",\"msg\":\"x\"}"));
        FA_EXPECT_FALSE(r.Succeeded());
        FA_EXPECT_EQ(r.m_code, codes[i]);
        FA_EXPECT_STREQ(r.m_message, messages[i]);
    }

    //传输失败：把网络层的中文原因带给用户
    HttpResult transportFail;
    transportFail.m_transportOk = false;
    transportFail.m_error = _T("连接被拒绝");
    const SensitiveWordListResult r1 = ParseSensitiveListResponse(transportFail);
    FA_EXPECT_FALSE(r1.Succeeded());
    FA_EXPECT_STREQ(r1.m_message, _T("连接被拒绝"));

    //HTTP 状态码异常
    HttpResult badStatus;
    badStatus.m_transportOk = true;
    badStatus.m_statusCode = 500;
    const SensitiveHitListResult r2 = ParseSensitiveHitsResponse(badStatus);
    FA_EXPECT_FALSE(r2.Succeeded());
    FA_EXPECT_FALSE(r2.m_message.IsEmpty());

    //应答不是合法 JSON
    const SensitiveImportResult r3 = ParseSensitiveImportResponse(MakeHttpOk("not a json"));
    FA_EXPECT_FALSE(r3.Succeeded());
    FA_EXPECT_FALSE(r3.m_message.IsEmpty());

    //缺字段：取默认值，不崩溃；列表里不是对象的元素跳过
    const SensitiveWordListResult bare = ParseSensitiveListResponse(MakeHttpOk("{\"code\":0}"));
    FA_EXPECT_TRUE(bare.Succeeded());
    FA_EXPECT_EQ(bare.m_total, 0);
    FA_EXPECT_FALSE(bare.m_enabled);
    FA_EXPECT_EQ(bare.m_list.size(), 0);

    const SensitiveHitListResult partial = ParseSensitiveHitsResponse(
        MakeHttpOk("{\"code\":0,\"total\":2,\"list\":[1,{\"id\":5}]}"));
    FA_EXPECT_TRUE(partial.Succeeded());
    FA_EXPECT_EQ(partial.m_list.size(), 1);
    if (partial.m_list.size() == 1)
    {
        FA_EXPECT_TRUE(partial.m_list[0].m_id == 5);
        FA_EXPECT_TRUE(partial.m_list[0].m_userName.IsEmpty());
        FA_EXPECT_EQ(partial.m_list[0].m_source, 0);
    }

    const SensitiveImportResult noSkip = ParseSensitiveImportResponse(MakeHttpOk("{\"code\":0,\"added\":3}"));
    FA_EXPECT_TRUE(noSkip.Succeeded());
    FA_EXPECT_EQ(noSkip.m_added, 3);
    FA_EXPECT_EQ(noSkip.m_skipped.size(), 0);
}

void RunAllSensitiveWordTests()
{
    TestSensitiveRequests();
    TestSensitiveListResponse();
    TestSensitiveImportAndHitsResponse();
    TestSensitiveErrorsAndMissingFields();
    TestDecodeImportFile();
    TestSplitLines();
    TestBlankLines();
    TestImportBatches();
    TestExportRoundTrip();
    TestNames();
}

}} // namespace flamingoadmin::test
