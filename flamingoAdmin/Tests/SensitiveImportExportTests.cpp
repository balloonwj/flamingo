/**
 *  flamingoAdmin —— 敏感词批量导入、导出与导出文件存盘规格的单元测试（docs/敏感词方案.md 第 3c 步）。
 *
 *    - ST-1：CSV 与文本导出的存盘规格；成功提示按导出内容给出，不再一律是「企业列表已导出」；
 *    - ST-2：写盘字节：需要时补一次 UTF-8 BOM，内容已带 BOM 时不重复补；
 *    - ST-3：「另存为」对话框的过滤串；
 *    - ST-4：敏感词导出的默认文件名：带企业名、企业名为空、企业名含文件名不允许的字符；
 *    - SI-1：开始导入前的检查：没有可导入的内容、平台超管未选企业；
 *    - SI-2：2500 行（含空行）分三批依次提交，进度、请求内容、被跳过行的原文行号与汇总正确，结束后从第 1 页重拉列表；
 *    - SI-3：中途某批失败：不再提交后面的批次，汇总说明失败的批次与此前已导入的条数，同样重拉列表；
 *    - SI-4：导入进行中再次开始导入被拒绝；
 *    - SI-5：汇总文字的格式与「最多逐行列出 500 行」；
 *    - SI-6：点「批量导入」：已选企业时请宿主打开导入弹窗，平台超管未选企业时不做任何事；
 *    - SE-1：导出：请求、返回前重复点击不重复请求、取到内容后请宿主存盘（字节与默认文件名正确）；
 *    - SE-2：导出：词库为空时只提示、失败时报错，都不请宿主存盘；平台超管导出时文件名带所选企业名；
 *    - SIM-1：导入弹窗的初始状态与读入文件内容；
 *    - SIM-2：导入弹窗的「导入中」与「显示结果」两种状态，及再次打开时恢复初始状态。
 *
 *  页面的请求出口与界面请求处理函数都换成记录下来，测试不碰网络、不弹窗口。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/SensitiveWordsScreen.h"
#include "Screens/SensitiveImportModal.h"
#include "Util/TextFileSave.h"
#include "Util/SensitiveWordText.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiEdit.h"
#include "json.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 测试用的界面矩形
const RECT kScreenRect = { 0, 0, 1200, 800 };

// 测试用的企业 ID
const long long kEntA = 9001;

// SI-2 的总行数与其中空行的行号：分成 1000 / 1000 / 500 三批
const int kImportLines = 2500;
const int kBlankLine = 1500;

// 导入请求里的默认分类：广告引流
const int kImportCategory = 5;

// 记录页面发出的请求与界面请求
struct Captured
{
    std::vector<HttpRequest>                       m_requests;  // 请求出口收到的请求
    std::vector<SensitiveWordsScreen::UiRequest>   m_ui;        // 界面请求
};

void CaptureRequest(void* user, HttpRequest& request)
{
    static_cast<Captured*>(user)->m_requests.push_back(request);
}

void CaptureUi(void* user, const SensitiveWordsScreen::UiRequest& request)
{
    static_cast<Captured*>(user)->m_ui.push_back(request);
}

HttpResult MakeHttpOk(const std::string& body)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = body;
    return r;
}

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

void Setup(SensitiveWordsScreen& screen, Captured& captured)
{
    screen.SetRequestSink(&CaptureRequest, &captured);
    screen.SetUiRequestHandler(&CaptureUi, &captured);
    screen.SetRect(kScreenRect);
}

// 以企业管理员身份进入页面，并给出一页空的列表结果（页面进入「已加载」状态）
void EnterAsEnterpriseAdmin(SensitiveWordsScreen& screen, Captured& captured)
{
    AdminSession::Instance().Set(_T("tok-ent"), 1, _T("企业管理员"), _T("ent@test.com"), kEntA);
    Setup(screen, captured);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":0,\"enabled\":1,\"wordcount\":0,\"list\":[]}"));
}

// 第 i 行（从 1 起）的内容：第 kBlankLine 行为空行，其余为「w<i>」
CString ImportLine(int i)
{
    if (i == kBlankLine)
        return CString();
    CString line;
    line.Format(_T("w%d"), i);
    return line;
}

// 拼出 count 行的导入原文，行与行之间用 CRLF 分隔
CString ImportText(int count)
{
    CString text;
    for (int i = 1; i <= count; ++i)
    {
        if (i > 1)
            text += _T("\r\n");
        text += ImportLine(i);
    }
    return text;
}

// 找最后一条指定种类的界面请求；没有时返回 nullptr
const SensitiveWordsScreen::UiRequest* LastUi(const Captured& c, int kind)
{
    for (size_t i = c.m_ui.size(); i > 0; --i)
    {
        if (c.m_ui[i - 1].m_kind == kind)
            return &c.m_ui[i - 1];
    }
    return nullptr;
}

// 数某种请求标签的请求个数
int CountRequests(const Captured& c, ApiTag tag)
{
    int n = 0;
    for (size_t i = 0; i < c.m_requests.size(); ++i)
    {
        if (c.m_requests[i].m_tag == tag)
            ++n;
    }
    return n;
}

// 一批导入成功的应答
std::string ImportOkJson(int added, const std::string& skippedArray)
{
    return "{\"code\":0,\"msg\":\"ok\",\"added\":" + std::to_string(added) + ",\"skipped\":" + skippedArray + "}";
}

} // namespace

void SieSaveSpecs()
{
    BeginCase("ST-1 导出存盘规格：CSV 与文本两种，成功提示按导出内容给出");

    const filesave::SaveFileSpec audit = filesave::CsvFileSpec(_T("审计日志.csv"), _T("审计日志"));
    FA_EXPECT_STREQ(audit.m_defaultName, _T("审计日志.csv"));
    FA_EXPECT_STREQ(audit.m_filterName, _T("CSV 文件 (*.csv)"));
    FA_EXPECT_STREQ(audit.m_filterPattern, _T("*.csv"));
    FA_EXPECT_STREQ(audit.m_defExt, _T("csv"));
    FA_EXPECT_STREQ(audit.m_successToast, _T("审计日志已导出"));
    FA_EXPECT_TRUE(audit.m_addUtf8Bom);

    //不同的导出给出不同的提示，不再一律是「企业列表已导出」
    FA_EXPECT_STREQ(filesave::CsvFileSpec(_T("公告列表.csv"), _T("公告列表")).m_successToast, _T("公告列表已导出"));
    FA_EXPECT_STREQ(filesave::CsvFileSpec(_T("企业列表.csv"), _T("企业列表")).m_successToast, _T("企业列表已导出"));

    const filesave::SaveFileSpec words = filesave::TextFileSpec(_T("敏感词词库.txt"), _T("敏感词词库"));
    FA_EXPECT_STREQ(words.m_defaultName, _T("敏感词词库.txt"));
    FA_EXPECT_STREQ(words.m_filterName, _T("文本文件 (*.txt)"));
    FA_EXPECT_STREQ(words.m_filterPattern, _T("*.txt"));
    FA_EXPECT_STREQ(words.m_defExt, _T("txt"));
    FA_EXPECT_STREQ(words.m_successToast, _T("敏感词词库已导出"));
    FA_EXPECT_TRUE(words.m_addUtf8Bom);
}

void SieSaveBytes()
{
    BeginCase("ST-2 写盘字节：需要时补一次 UTF-8 BOM，已带 BOM 时不重复补");

    const std::string bom("\xEF\xBB\xBF");
    const filesave::SaveFileSpec csv = filesave::CsvFileSpec(_T("a.csv"), _T("a"));
    FA_EXPECT_TRUE(filesave::BuildFileBytes(csv, "x,y\r\n") == bom + "x,y\r\n");
    FA_EXPECT_TRUE(filesave::BuildFileBytes(csv, bom + "x") == bom + "x");
    FA_EXPECT_TRUE(filesave::BuildFileBytes(csv, std::string()) == bom);

    filesave::SaveFileSpec raw = csv;
    raw.m_addUtf8Bom = false;
    FA_EXPECT_TRUE(filesave::BuildFileBytes(raw, "x") == "x");
}

void SieSaveFilter()
{
    BeginCase("ST-3 「另存为」对话框的过滤串：说明、通配、所有文件，以两个 \\0 结尾");

    std::vector<TCHAR> filter;
    filter.push_back(_T('?'));
    filesave::BuildFilterBuffer(filesave::TextFileSpec(_T("a.txt"), _T("a")), filter);
    const CString expected = _T("文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*||");
    FA_EXPECT_EQ(filter.size(), static_cast<size_t>(expected.GetLength()));
    bool same = filter.size() == static_cast<size_t>(expected.GetLength());
    for (size_t i = 0; same && i < filter.size(); ++i)
    {
        const TCHAR want = (expected[static_cast<int>(i)] == _T('|')) ? _T('\0') : expected[static_cast<int>(i)];
        same = (filter[i] == want);
    }
    FA_EXPECT_TRUE(same);
}

void SieExportFileName()
{
    BeginCase("ST-4 敏感词导出的默认文件名");

    FA_EXPECT_STREQ(filesave::SensitiveExportFileName(_T("星河科技")), _T("敏感词词库-星河科技.txt"));
    FA_EXPECT_STREQ(filesave::SensitiveExportFileName(CString()), _T("敏感词词库.txt"));
    FA_EXPECT_STREQ(filesave::SensitiveExportFileName(_T("   ")), _T("敏感词词库.txt"));
    FA_EXPECT_STREQ(filesave::SensitiveExportFileName(_T(" A/B:C*?\"<>|\\ ")), _T("敏感词词库-A_B_C_______.txt"));
}

void SieImportPreconditions()
{
    BeginCase("SI-1 开始导入前的检查：没有可导入的内容、平台超管未选企业");

    {
        SensitiveWordsScreen screen;
        Captured c;
        EnterAsEnterpriseAdmin(screen, c);
        const size_t before = c.m_requests.size();
        CString error;
        FA_EXPECT_FALSE(screen.StartImport(_T("  \r\n\t\r\n"), kImportCategory, &error));
        FA_EXPECT_STREQ(error, _T("没有可导入的内容"));
        FA_EXPECT_FALSE(screen.IsImporting());
        FA_EXPECT_EQ(c.m_requests.size(), before);
    }

    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        const size_t before = c.m_requests.size();
        CString error;
        FA_EXPECT_FALSE(screen.StartImport(_T("w1"), kImportCategory, &error));
        FA_EXPECT_STREQ(error, _T("请先选择企业"));
        FA_EXPECT_EQ(c.m_requests.size(), before);
    }
}

void SieImportBatches()
{
    BeginCase("SI-2 2500 行分三批依次提交：进度、请求内容、被跳过行的原文行号与汇总");

    SensitiveWordsScreen screen;
    Captured c;
    EnterAsEnterpriseAdmin(screen, c);

    CString error;
    FA_EXPECT_TRUE(screen.StartImport(ImportText(kImportLines), kImportCategory, &error));
    FA_EXPECT_TRUE(screen.IsImporting());
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 1);
    const SensitiveWordsScreen::UiRequest* progress = LastUi(c, SensitiveWordsScreen::kUiImportProgress);
    FA_EXPECT_NOTNULL(progress);
    if (progress != nullptr)
        FA_EXPECT_STREQ(progress->m_text, _T("正在导入第 1 / 3 批…"));
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(c.m_requests.back().m_path == L"/admin/sensitive/import.do");
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["lines"].size(), 1000);
        FA_EXPECT_TRUE(body["lines"][0].asString() == "w1");
        FA_EXPECT_EQ(body["category"].asInt(), kImportCategory);
    }

    //第 1 批全部成功：发第 2 批（第 1001～2000 行，含第 1500 行那个空行）
    screen.OnApiResult(kApiTagSwImport, MakeHttpOk(ImportOkJson(1000, "[]")));
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 2);
    progress = LastUi(c, SensitiveWordsScreen::kUiImportProgress);
    if (progress != nullptr)
        FA_EXPECT_STREQ(progress->m_text, _T("正在导入第 2 / 3 批…"));
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["lines"].size(), 1000);
        FA_EXPECT_TRUE(body["lines"][0].asString() == "w1001");
    }

    //第 2 批：本批第 5 行（原文第 1005 行）重复被跳过
    screen.OnApiResult(kApiTagSwImport, MakeHttpOk(
        ImportOkJson(998, "[{\"line\":5,\"word\":\"w1005\",\"reason\":122}]")));
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 3);
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["lines"].size(), 500);
    }
    FA_EXPECT_NULL(LastUi(c, SensitiveWordsScreen::kUiImportDone));

    //第 3 批完成：汇总，结束导入，从第 1 页重拉列表
    screen.OnApiResult(kApiTagSwImport, MakeHttpOk(ImportOkJson(500, "[]")));
    FA_EXPECT_FALSE(screen.IsImporting());
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 3);
    const SensitiveWordsScreen::UiRequest* done = LastUi(c, SensitiveWordsScreen::kUiImportDone);
    FA_EXPECT_NOTNULL(done);
    if (done != nullptr)
    {
        FA_EXPECT_FALSE(done->m_isError);
        FA_EXPECT_TRUE(done->m_text.Find(_T("导入完成：成功 2498 条，跳过 1 条。")) == 0);
        FA_EXPECT_TRUE(done->m_text.Find(_T("第 1005 行「w1005」：词条已存在")) > 0);
    }
    FA_EXPECT_TRUE(!c.m_requests.empty() && c.m_requests.back().m_tag == kApiTagSwList);
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["page"].asInt(), 1);
    }
}

void SieImportFailure()
{
    BeginCase("SI-3 中途某批失败：不再提交后面的批次，汇总说明失败的批次与此前已导入的条数");

    SensitiveWordsScreen screen;
    Captured c;
    EnterAsEnterpriseAdmin(screen, c);

    //1500 行：两批
    CString error;
    FA_EXPECT_TRUE(screen.StartImport(ImportText(kBlankLine - 1), kImportCategory, &error));
    screen.OnApiResult(kApiTagSwImport, MakeHttpOk(
        ImportOkJson(999, "[{\"line\":2,\"word\":\"w2\",\"reason\":122}]")));
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 2);

    screen.OnApiResult(kApiTagSwImport, MakeHttpOk("{\"code\":1,\"msg\":\"x\"}"));
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 2);
    FA_EXPECT_FALSE(screen.IsImporting());
    const SensitiveWordsScreen::UiRequest* done = LastUi(c, SensitiveWordsScreen::kUiImportDone);
    FA_EXPECT_NOTNULL(done);
    if (done != nullptr)
    {
        FA_EXPECT_TRUE(done->m_isError);
        FA_EXPECT_TRUE(done->m_text.Find(_T("第 2 / 2 批导入失败")) == 0);
        FA_EXPECT_TRUE(done->m_text.Find(_T("在此之前已成功导入 999 条，跳过 1 条。")) > 0);
        FA_EXPECT_TRUE(done->m_text.Find(_T("第 2 行「w2」")) > 0);
    }
    FA_EXPECT_TRUE(!c.m_requests.empty() && c.m_requests.back().m_tag == kApiTagSwList);

    //网络不通同样按失败处理
    FA_EXPECT_TRUE(screen.StartImport(_T("a"), kImportCategory, &error));
    HttpResult broken;
    broken.m_transportOk = false;
    broken.m_error = _T("无法连接服务器");
    screen.OnApiResult(kApiTagSwImport, broken);
    FA_EXPECT_FALSE(screen.IsImporting());
    done = LastUi(c, SensitiveWordsScreen::kUiImportDone);
    if (done != nullptr)
    {
        FA_EXPECT_TRUE(done->m_isError);
        FA_EXPECT_TRUE(done->m_text.Find(_T("第 1 / 1 批导入失败：无法连接服务器")) == 0);
    }
}

void SieImportBusy()
{
    BeginCase("SI-4 导入进行中再次开始导入被拒绝");

    SensitiveWordsScreen screen;
    Captured c;
    EnterAsEnterpriseAdmin(screen, c);
    CString error;
    FA_EXPECT_TRUE(screen.StartImport(_T("a\nb"), kImportCategory, &error));
    FA_EXPECT_FALSE(screen.StartImport(_T("c"), kImportCategory, &error));
    FA_EXPECT_STREQ(error, _T("上一次导入尚未结束"));
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwImport), 1);
}

void SieImportSummary()
{
    BeginCase("SI-5 导入汇总的格式与「最多逐行列出 500 行」");

    std::vector<SensitiveWordsScreen::ImportSkipLine> skips;
    FA_EXPECT_STREQ(SensitiveWordsScreen::BuildImportSummary(3, skips, CString(), 0, 1),
                    _T("导入完成：成功 3 条，跳过 0 条。"));

    SensitiveWordsScreen::ImportSkipLine a;
    a.m_line = 2;
    a.m_word = _T("甲");
    a.m_reason = _T("词条已存在");
    SensitiveWordsScreen::ImportSkipLine b;
    b.m_line = 9;
    b.m_word = _T("!!!");
    b.m_reason = _T("词条为空");
    skips.push_back(a);
    skips.push_back(b);
    FA_EXPECT_STREQ(SensitiveWordsScreen::BuildImportSummary(1, skips, CString(), 0, 1),
                    _T("导入完成：成功 1 条，跳过 2 条。\r\n被跳过的行：\r\n")
                    _T("第 2 行「甲」：词条已存在\r\n第 9 行「!!!」：词条为空"));
    FA_EXPECT_STREQ(SensitiveWordsScreen::BuildImportSummary(1, skips, _T("网络中断"), 2, 3),
                    _T("第 2 / 3 批导入失败：网络中断。在此之前已成功导入 1 条，跳过 2 条。\r\n被跳过的行：\r\n")
                    _T("第 2 行「甲」：词条已存在\r\n第 9 行「!!!」：词条为空"));

    //超过 500 行只列前 500 行
    std::vector<SensitiveWordsScreen::ImportSkipLine> many;
    for (int i = 1; i <= SensitiveWordsScreen::kImportSummaryMaxLines + 1; ++i)
    {
        SensitiveWordsScreen::ImportSkipLine s;
        s.m_line = i;
        s.m_word.Format(_T("x%d"), i);
        s.m_reason = _T("词条已存在");
        many.push_back(s);
    }
    const CString text = SensitiveWordsScreen::BuildImportSummary(0, many, CString(), 0, 1);
    FA_EXPECT_TRUE(text.Find(_T("第 500 行「x500」")) > 0);
    FA_EXPECT_TRUE(text.Find(_T("第 501 行")) < 0);
    FA_EXPECT_TRUE(text.Find(_T("……另有 1 行未列出")) > 0);
}

void SieImportClicked()
{
    BeginCase("SI-6 点「批量导入」：已选企业时请宿主打开导入弹窗，平台超管未选企业时不做任何事");

    {
        SensitiveWordsScreen screen;
        Captured c;
        EnterAsEnterpriseAdmin(screen, c);
        screen.OnImportClicked();
        FA_EXPECT_NOTNULL(LastUi(c, SensitiveWordsScreen::kUiOpenImportModal));
    }

    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        screen.OnImportClicked();
        screen.OnExportClicked();
        FA_EXPECT_NULL(LastUi(c, SensitiveWordsScreen::kUiOpenImportModal));
        FA_EXPECT_EQ(CountRequests(c, kApiTagSwExport), 0);
    }
}

void SieExport()
{
    BeginCase("SE-1 导出：请求、返回前重复点击不重复请求、取到内容后请宿主存盘");

    SensitiveWordsScreen screen;
    Captured c;
    EnterAsEnterpriseAdmin(screen, c);

    screen.OnExportClicked();
    screen.OnExportClicked();
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwExport), 1);
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(c.m_requests.back().m_path == L"/admin/sensitive/export.do");
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    }

    const std::string exported = u8"敏感\t违法违规\tr1\n广告\t其他\t\n";
    screen.OnApiResult(kApiTagSwExport, MakeHttpOk(exported));
    const SensitiveWordsScreen::UiRequest* save = LastUi(c, SensitiveWordsScreen::kUiSaveExport);
    FA_EXPECT_NOTNULL(save);
    if (save != nullptr)
    {
        FA_EXPECT_TRUE(save->m_bytes == swtext::BuildExportFileBytes(exported));
        FA_EXPECT_STREQ(save->m_text, _T("敏感词词库.txt"));
    }

    //返回之后可以再次导出
    screen.OnExportClicked();
    FA_EXPECT_EQ(CountRequests(c, kApiTagSwExport), 2);
}

void SieExportEdgeCases()
{
    BeginCase("SE-2 导出：词库为空只提示、失败报错，都不存盘；平台超管导出时文件名带所选企业名");

    {
        SensitiveWordsScreen screen;
        Captured c;
        EnterAsEnterpriseAdmin(screen, c);
        screen.OnExportClicked();
        screen.OnApiResult(kApiTagSwExport, MakeHttpOk(std::string()));
        FA_EXPECT_NULL(LastUi(c, SensitiveWordsScreen::kUiSaveExport));
        const SensitiveWordsScreen::UiRequest* toast = LastUi(c, SensitiveWordsScreen::kUiToast);
        FA_EXPECT_NOTNULL(toast);
        if (toast != nullptr)
        {
            FA_EXPECT_STREQ(toast->m_text, _T("词库为空，没有可导出的词条"));
            FA_EXPECT_FALSE(toast->m_isError);
        }

        screen.OnExportClicked();
        screen.OnApiResult(kApiTagSwExport, MakeHttpOk("{\"code\":2,\"msg\":\"not login\"}"));
        FA_EXPECT_NULL(LastUi(c, SensitiveWordsScreen::kUiSaveExport));
        toast = LastUi(c, SensitiveWordsScreen::kUiToast);
        if (toast != nullptr)
        {
            FA_EXPECT_TRUE(toast->m_isError);
            FA_EXPECT_TRUE(toast->m_text.Find(_T("导出失败：")) == 0);
        }
    }

    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        screen.OnApiResult(kApiTagSwEnterprises, MakeHttpOk(
            u8"{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":[{\"enterpriseid\":9001,\"name\":\"甲公司\"},"
            u8"{\"enterpriseid\":9002,\"name\":\"乙公司\"}]}"));
        screen.OnEnterpriseSelected(1);
        screen.OnExportClicked();
        if (!c.m_requests.empty())
        {
            Json::Value body;
            FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
            FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == 9002);
        }
        screen.OnApiResult(kApiTagSwExport, MakeHttpOk(u8"词\t其他\t\n"));
        const SensitiveWordsScreen::UiRequest* save = LastUi(c, SensitiveWordsScreen::kUiSaveExport);
        FA_EXPECT_NOTNULL(save);
        if (save != nullptr)
            FA_EXPECT_STREQ(save->m_text, _T("敏感词词库-乙公司.txt"));
    }
}

void SieImportModalInitial()
{
    BeginCase("SIM-1 导入弹窗：初始状态与读入文件内容");

    SensitiveImportModal modal;
    modal.ShowFresh();
    FA_EXPECT_STREQ(modal.GetText(), _T(""));
    FA_EXPECT_EQ(modal.GetDefaultCategory(), swtext::kCategoryOther);
    FA_EXPECT_STREQ(modal.StatusText(), _T(""));
    FA_EXPECT_FALSE(modal.IsImporting());
    FA_EXPECT_FALSE(modal.IsDone());
    FA_EXPECT_NOTNULL(modal.SubmitButton());
    FA_EXPECT_NOTNULL(modal.CancelButton());
    FA_EXPECT_NOTNULL(modal.ReadFileButton());
    FA_EXPECT_NOTNULL(modal.TextEdit());
    if (modal.SubmitButton() == nullptr || modal.CancelButton() == nullptr || modal.ReadFileButton() == nullptr
        || modal.TextEdit() == nullptr)
        return;
    FA_EXPECT_STREQ(modal.SubmitButton()->GetText(), _T("开始导入"));
    FA_EXPECT_TRUE(modal.SubmitButton()->IsEnabled());
    FA_EXPECT_TRUE(modal.CancelButton()->IsVisible());
    FA_EXPECT_TRUE(modal.ReadFileButton()->IsVisible());
    FA_EXPECT_FALSE(modal.TextEdit()->IsReadOnly());

    //读入文件内容：覆盖输入框，行数不变
    modal.SetText(_T("甲\r\n乙\t广告引流"));
    FA_EXPECT_EQ(swtext::SplitLines(modal.GetText()).size(), 2);
    modal.SetStatus(_T("已读取 2 行"), false);
    FA_EXPECT_STREQ(modal.StatusText(), _T("已读取 2 行"));
    FA_EXPECT_FALSE(modal.IsStatusError());
}

void SieImportModalStates()
{
    BeginCase("SIM-2 导入弹窗：导入中、显示结果，再次打开时恢复初始状态");

    SensitiveImportModal modal;
    modal.ShowFresh();
    if (modal.SubmitButton() == nullptr || modal.CancelButton() == nullptr || modal.ReadFileButton() == nullptr
        || modal.CloseButton() == nullptr || modal.TextEdit() == nullptr)
    {
        FA_EXPECT_TRUE(false);
        return;
    }

    modal.SetImporting(true);
    FA_EXPECT_TRUE(modal.IsImporting());
    FA_EXPECT_FALSE(modal.SubmitButton()->IsEnabled());
    FA_EXPECT_FALSE(modal.CancelButton()->IsEnabled());
    FA_EXPECT_FALSE(modal.ReadFileButton()->IsEnabled());
    FA_EXPECT_FALSE(modal.CloseButton()->IsEnabled());
    FA_EXPECT_TRUE(modal.TextEdit()->IsReadOnly());

    modal.ShowResult(_T("导入完成：成功 1 条，跳过 0 条。"), false);
    FA_EXPECT_FALSE(modal.IsImporting());
    FA_EXPECT_TRUE(modal.IsDone());
    FA_EXPECT_STREQ(modal.GetText(), _T("导入完成：成功 1 条，跳过 0 条。"));
    FA_EXPECT_TRUE(modal.TextEdit()->IsReadOnly());
    FA_EXPECT_STREQ(modal.SubmitButton()->GetText(), _T("完成"));
    FA_EXPECT_TRUE(modal.SubmitButton()->IsEnabled());
    FA_EXPECT_TRUE(modal.CloseButton()->IsEnabled());
    FA_EXPECT_FALSE(modal.CancelButton()->IsVisible());
    FA_EXPECT_FALSE(modal.ReadFileButton()->IsVisible());
    FA_EXPECT_STREQ(modal.StatusText(), _T("导入完成"));
    FA_EXPECT_FALSE(modal.IsStatusError());

    modal.ShowResult(_T("第 1 / 1 批导入失败：x。"), true);
    FA_EXPECT_STREQ(modal.StatusText(), _T("导入中途失败"));
    FA_EXPECT_TRUE(modal.IsStatusError());

    //再次打开：恢复初始状态
    modal.ShowFresh();
    FA_EXPECT_FALSE(modal.IsDone());
    FA_EXPECT_STREQ(modal.GetText(), _T(""));
    FA_EXPECT_FALSE(modal.TextEdit()->IsReadOnly());
    FA_EXPECT_STREQ(modal.SubmitButton()->GetText(), _T("开始导入"));
    FA_EXPECT_TRUE(modal.CancelButton()->IsVisible());
    FA_EXPECT_TRUE(modal.CancelButton()->IsEnabled());
    FA_EXPECT_TRUE(modal.ReadFileButton()->IsVisible());
    FA_EXPECT_STREQ(modal.StatusText(), _T(""));
}

void RunAllSensitiveImportExportTests()
{
    SieSaveSpecs();
    SieSaveBytes();
    SieSaveFilter();
    SieExportFileName();
    SieImportPreconditions();
    SieImportBatches();
    SieImportFailure();
    SieImportBusy();
    SieImportSummary();
    SieImportClicked();
    SieExport();
    SieExportEdgeCases();
    SieImportModalInitial();
    SieImportModalStates();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
