/**
 *  flamingoAdmin —— 敏感词界面「拦截记录」页签的单元测试（docs/敏感词方案.md 第 3d 步、第 7.5 节）。
 *
 *    - SH-1：切到「拦截记录」时按默认条件（近 7 天、全部来源与分类、第 1 页）拉取；企业管理员请求里不带
 *            enterpriseid；平台超管未选企业时不拉取、提示「请先选择企业」，企业选择框在本页签同样可见；
 *    - SH-2：时间范围、来源、分类、发送者搜索各自从第 1 页重新拉取，请求里的条件正确；
 *    - SH-3：结果：行数、页数、覆盖提示（无记录、无符合条件的记录、加载失败）与翻页；
 *    - SH-4：一行各列的文字：时间格式、来源名、分类名、空发送者与空会话显示「-」、摘要里的换行换成空格；
 *    - SH-5：悬停提示：鼠标在某一行的「内容摘要」列上时为摘要全文，其它位置没有；
 *    - SH-6：停在「拦截记录」页签时换企业，按新企业重新拉取；每次切回本页签都重新拉取。
 *
 *  页面的请求出口与界面请求处理函数都换成记录下来，测试不碰网络、不弹窗口。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/SensitiveWordsScreen.h"
#include "Widgets/ActionTable.h"
#include "Util/SensitiveWordText.h"
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
const long long kEntB = 9002;

// 一天的毫秒数
const long long kDayMs = 24LL * 60 * 60 * 1000;

// 时间范围的容差：测试取当前时间与界面取当前时间之间允许相差的毫秒数
const long long kClockToleranceMs = 60 * 1000;

// 一条拦截记录的时间：2026-10-02 某时（UTC 毫秒），只用于比对格式
const long long kSampleTimeMs = 1791000000000LL;

// 来源与分类筛选用的取值
const int kSourceFile = 3;
const int kCategoryVulgar = 2;

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

// 测试自己取的当前时间（Unix 毫秒），与界面的 NowMs 相互独立
long long TestNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    //FILETIME 从 1601-01-01 起、以 100 纳秒为单位；减去到 1970-01-01 的间隔后换算成毫秒
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    const unsigned long long k100nsPerMs = 10000ULL;
    return static_cast<long long>((t.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

// 最后一个拦截记录请求的请求体；没有时返回 false
bool LastHitsBody(const Captured& c, Json::Value& body)
{
    for (size_t i = c.m_requests.size(); i > 0; --i)
    {
        if (c.m_requests[i - 1].m_tag == kApiTagSwHits)
            return ParseBody(c.m_requests[i - 1], body);
    }
    return false;
}

int CountHits(const Captured& c)
{
    int n = 0;
    for (size_t i = 0; i < c.m_requests.size(); ++i)
    {
        if (c.m_requests[i].m_tag == kApiTagSwHits)
            ++n;
    }
    return n;
}

// 拦截记录列表应答：count 条记录，总数为 total
std::string HitsJson(int count, int total)
{
    std::string json = "{\"code\":0,\"msg\":\"ok\",\"total\":" + std::to_string(total) + ",\"list\":[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            json += ",";
        json += "{\"id\":" + std::to_string(500 + i) + ",\"time\":" + std::to_string(kSampleTimeMs)
            + u8",\"userid\":7,\"username\":\"张三\",\"source\":1,\"ctype\":2,\"cid\":33,\"targetname\":\"研发群\","
              u8"\"word\":\"敏感\",\"category\":1,\"snippet\":\"前面的话 敏感 后面的话\"}";
    }
    json += "]}";
    return json;
}

// 以企业管理员身份进入页面并切到「拦截记录」
void EnterHitsAsEnterpriseAdmin(SensitiveWordsScreen& screen, Captured& c)
{
    AdminSession::Instance().Set(_T("tok-ent"), 1, _T("企业管理员"), _T("ent@test.com"), kEntA);
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":0,\"enabled\":1,\"wordcount\":0,\"list\":[]}"));
    screen.OnTabChanged(SensitiveWordsScreen::kTabHits);
}

bool Near(long long actual, long long expected)
{
    const long long diff = actual - expected;
    return diff <= kClockToleranceMs && diff >= -kClockToleranceMs;
}

} // namespace

void SwHitsEnterTab()
{
    BeginCase("SH-1 切到「拦截记录」按默认条件拉取；平台超管未选企业时不拉取");

    {
        SensitiveWordsScreen screen;
        Captured c;
        EnterHitsAsEnterpriseAdmin(screen, c);
        FA_EXPECT_EQ(CountHits(c), 1);
        Json::Value body;
        FA_EXPECT_TRUE(LastHitsBody(c, body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
        FA_EXPECT_EQ(body["page"].asInt(), 1);
        FA_EXPECT_EQ(body["pagesize"].asInt(), SensitiveWordsScreen::kPageSize);
        FA_EXPECT_EQ(body["source"].asInt(), 0);
        FA_EXPECT_EQ(body["category"].asInt(), 0);
        FA_EXPECT_TRUE(body["keyword"].asString().empty());
        FA_EXPECT_TRUE(Near(body["begintime"].asInt64(), TestNowMs() - 7 * kDayMs));
        FA_EXPECT_TRUE(body["endtime"].asInt64() == 0);
        FA_EXPECT_STREQ(screen.HitOverlayText(), _T("加载中…"));
    }

    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        screen.OnTabChanged(SensitiveWordsScreen::kTabHits);
        FA_EXPECT_EQ(CountHits(c), 0);
        FA_EXPECT_STREQ(screen.HitOverlayText(), _T("请先选择企业"));
        FA_EXPECT_TRUE(screen.IsEnterprisePickerVisible());
    }
}

void SwHitsFilters()
{
    BeginCase("SH-2 时间范围、来源、分类、发送者搜索各自从第 1 页重新拉取");

    SensitiveWordsScreen screen;
    Captured c;
    EnterHitsAsEnterpriseAdmin(screen, c);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));
    screen.GoToHitPage(2);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));
    FA_EXPECT_EQ(screen.HitPage(), 2);

    Json::Value body;
    screen.OnHitRangeChanged(SensitiveWordsScreen::kHitRange30Days);
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_EQ(body["page"].asInt(), 1);
    FA_EXPECT_TRUE(Near(body["begintime"].asInt64(), TestNowMs() - 30 * kDayMs));
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));

    screen.OnHitRangeChanged(SensitiveWordsScreen::kHitRange180Days);
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_TRUE(Near(body["begintime"].asInt64(), TestNowMs() - 180 * kDayMs));
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));

    screen.OnHitSourceChanged(kSourceFile);
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_EQ(body["source"].asInt(), kSourceFile);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));

    screen.OnHitCategoryChanged(kCategoryVulgar);
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_EQ(body["category"].asInt(), kCategoryVulgar);
    FA_EXPECT_EQ(body["source"].asInt(), kSourceFile);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));

    //发送者搜索：没有宿主窗口时不防抖，立即查询
    FA_EXPECT_NOTNULL(screen.HitSearchBox());
    if (screen.HitSearchBox() != nullptr)
    {
        screen.HitSearchBox()->SetText(_T("张"));
        screen.OnHitSearchChanged();
        FA_EXPECT_TRUE(LastHitsBody(c, body));
        FA_EXPECT_TRUE(body["keyword"].asString() == u8"张");
        FA_EXPECT_EQ(body["page"].asInt(), 1);
    }
}

void SwHitsResults()
{
    BeginCase("SH-3 结果：行数、页数、覆盖提示与翻页");

    SensitiveWordsScreen screen;
    Captured c;
    EnterHitsAsEnterpriseAdmin(screen, c);

    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));
    FA_EXPECT_EQ(screen.HitRowCount(), 2);
    FA_EXPECT_EQ(screen.HitPageCount(), 3);
    FA_EXPECT_STREQ(screen.HitOverlayText(), _T(""));

    //翻页：越界不翻
    const int before = CountHits(c);
    screen.GoToHitPage(4);
    FA_EXPECT_EQ(CountHits(c), before);
    screen.GoToHitPage(3);
    FA_EXPECT_EQ(CountHits(c), before + 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_EQ(body["page"].asInt(), 3);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 120)));

    //没有记录：不带筛选条件与带筛选条件的提示不同
    screen.OnHitRangeChanged(SensitiveWordsScreen::kHitRange7Days);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(0, 0)));
    FA_EXPECT_EQ(screen.HitRowCount(), 0);
    FA_EXPECT_STREQ(screen.HitOverlayText(), _T("暂无拦截记录"));
    screen.OnHitSourceChanged(kSourceFile);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(0, 0)));
    FA_EXPECT_STREQ(screen.HitOverlayText(), _T("没有符合条件的拦截记录"));

    //加载失败
    screen.OnHitSourceChanged(0);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk("{\"code\":1,\"msg\":\"x\"}"));
    FA_EXPECT_TRUE(screen.HitOverlayText().Find(_T("加载失败")) == 0);
}

void SwHitsCells()
{
    BeginCase("SH-4 一行各列的文字");

    SensitiveHitItem item;
    item.m_time = kSampleTimeMs;
    item.m_userName = _T("张三");
    item.m_source = 1;
    item.m_ctype = 2;
    item.m_targetName = _T("研发群");
    item.m_word = _T("敏感");
    item.m_category = 1;
    item.m_snippet = _T("前文\r\n敏感\n后文");

    std::vector<CString> cells;
    cells.push_back(_T("旧内容"));
    SensitiveWordsScreen::HitRowCells(item, cells);
    FA_EXPECT_EQ(cells.size(), static_cast<size_t>(SensitiveWordsScreen::kHitColumnCount));
    if (cells.size() == static_cast<size_t>(SensitiveWordsScreen::kHitColumnCount))
    {
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcTime], SensitiveWordsScreen::FormatHitTime(kSampleTimeMs));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcSender], _T("张三"));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcSource], swtext::HitSourceName(1));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcTarget], _T("研发群"));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcWord], _T("敏感"));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcCategory], swtext::CategoryName(1));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcSnippet], _T("前文 敏感 后文"));
    }

    //与会话无关、自助注册（没有用户名时）显示「-」
    item.m_userName.Empty();
    item.m_targetName.Empty();
    item.m_ctype = 0;
    SensitiveWordsScreen::HitRowCells(item, cells);
    if (cells.size() == static_cast<size_t>(SensitiveWordsScreen::kHitColumnCount))
    {
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcSender], _T("-"));
        FA_EXPECT_STREQ(cells[SensitiveWordsScreen::kHcTarget], _T("-"));
    }

    //时间：本地时间「YYYY-MM-DD HH:MM:SS」，不大于 0 时为「-」
    const CString t = SensitiveWordsScreen::FormatHitTime(kSampleTimeMs);
    __time64_t seconds = static_cast<__time64_t>(kSampleTimeMs / 1000);
    struct tm local;
    FA_EXPECT_TRUE(_localtime64_s(&local, &seconds) == 0);
    CString expected;
    expected.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"), local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                    local.tm_hour, local.tm_min, local.tm_sec);
    FA_EXPECT_STREQ(t, expected);
    FA_EXPECT_STREQ(SensitiveWordsScreen::FormatHitTime(0), _T("-"));

    //NowMs 与系统时间一致
    FA_EXPECT_TRUE(Near(SensitiveWordsScreen::NowMs(), TestNowMs()));
}

void SwHitsTooltip()
{
    BeginCase("SH-5 悬停提示：在「内容摘要」列上为摘要全文，其它位置没有");

    SensitiveWordsScreen screen;
    Captured c;
    EnterHitsAsEnterpriseAdmin(screen, c);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(2, 2)));

    ui::ActionTable* table = screen.HitTable();
    FA_EXPECT_NOTNULL(table);
    if (table == nullptr)
        return;
    const RECT rc = table->GetRect();
    const int rowH = table->GetRowHeight();
    //表格最右侧一段一定落在「内容摘要」列（它占满剩余宽度），第 1 行的垂直中点
    const POINT onSnippet = { rc.right - 30, rc.top + rowH / 2 };
    FA_EXPECT_STREQ(screen.HitTooltipAt(onSnippet), _T("前面的话 敏感 后面的话"));
    //最左侧是「时间」列
    const POINT onTime = { rc.left + 10, rc.top + rowH / 2 };
    FA_EXPECT_STREQ(screen.HitTooltipAt(onTime), _T(""));
    //最后一行下方
    const POINT below = { rc.right - 30, rc.top + rowH * 2 + rowH / 2 };
    FA_EXPECT_STREQ(screen.HitTooltipAt(below), _T(""));

    //表格本身经回调取到同样的文字
    table->OnMouseMove(onSnippet, 0);
    FA_EXPECT_STREQ(table->CurrentTooltip(), _T("前面的话 敏感 后面的话"));
    table->OnMouseMove(onTime, 0);
    FA_EXPECT_STREQ(table->CurrentTooltip(), _T(""));
}

void SwHitsReload()
{
    BeginCase("SH-6 停在本页签时换企业按新企业重新拉取；每次切回本页签都重新拉取");

    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwEnterprises, MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":[{\"enterpriseid\":9001,\"name\":\"甲公司\"},"
        u8"{\"enterpriseid\":9002,\"name\":\"乙公司\"}]}"));
    screen.OnTabChanged(SensitiveWordsScreen::kTabHits);
    FA_EXPECT_EQ(CountHits(c), 0);

    screen.OnEnterpriseSelected(0);
    FA_EXPECT_EQ(CountHits(c), 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(1, 1)));

    screen.OnEnterpriseSelected(1);
    FA_EXPECT_EQ(CountHits(c), 2);
    FA_EXPECT_TRUE(LastHitsBody(c, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntB);
    screen.OnApiResult(kApiTagSwHits, MakeHttpOk(HitsJson(1, 1)));

    //切回词库再切回来：重新拉取
    screen.OnTabChanged(SensitiveWordsScreen::kTabWords);
    FA_EXPECT_EQ(CountHits(c), 2);
    screen.OnTabChanged(SensitiveWordsScreen::kTabHits);
    FA_EXPECT_EQ(CountHits(c), 3);
}

void RunAllSensitiveHitsTests()
{
    SwHitsEnterTab();
    SwHitsFilters();
    SwHitsResults();
    SwHitsCells();
    SwHitsTooltip();
    SwHitsReload();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
