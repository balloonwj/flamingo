/**
 *  历史消息记录窗口——布局“控件点名契约”测试（Suite V2L），HistoryWindowLayoutTests.cpp
 *
 *  验证 BuildHistoryWindowBody() 产出的 balloonui 控件树里，所有需要被窗口代码引用
 *  的控件都能按既定数字 id 找到：标题 / 副标题 / 搜索框 / 6 个类型胶囊 / 时间下拉 /
 *  列表容器 / 底部信息 / 分页四按钮 / 导出，以及按行 / 按页动态生成的“定位”按钮和
 *  页码按钮；并检查无重复 id。不创建窗口，纯内存 build + 断言。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // 量文字宽度要用与控件同一支默认字体
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"   // 时间下拉的项与宽度断言
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"      // “展开 / 收起”链接的文案与模式断言

#include <algorithm>
#include <vector>

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

// 递归按 id 查控件；命中返回指针，否则 nullptr。
const DuiControl* FindById(const DuiControl* node, UINT id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->GetCtrlId() == id)
    {
        return node;
    }
    for (const auto& child : node->Children())
    {
        const DuiControl* hit = FindById(child.get(), id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// 递归收集子树里所有非 0 的 ctrlId。
void CollectIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectIds(child.get(), out);
    }
}

} // anonymous namespace

TEST_CASE("V2L1 历史窗固定控件按 id 都能找到")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);

    //标题“消息记录 · <名>”改在窗口标题栏显示，客户区不再放标题控件（kIdHistoryTitle 不再创建）。
    CHECK(FindById(root.get(), kIdHistorySubtitle) != nullptr);
    CHECK(FindById(root.get(), kIdHistorySearch) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipAll) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipText) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipImage) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipFile) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipVoice) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryChipLink) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryTimeCombo) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryList) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryFooterInfo) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryPageFirst) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryPagePrev) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryPageNext) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryPageLast) != nullptr);
    CHECK(FindById(root.get(), kIdHistoryExport) != nullptr);
}

TEST_CASE("V2L2 每行“定位”按钮按行下标都能找到")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    REQUIRE(!data.m_rows.empty());

    for (int i = 0; i < (int)data.m_rows.size(); ++i)
    {
        CHECK(FindById(root.get(), (UINT)(kIdHistoryLocateBase + i)) != nullptr);
    }
    //行数之外的下标不应存在对应按钮。
    CHECK(FindById(root.get(), (UINT)(kIdHistoryLocateBase + (int)data.m_rows.size())) == nullptr);
}

TEST_CASE("V2L3 页码按钮按总页数都能找到")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    REQUIRE(data.m_pageCount >= 1);

    for (int p = 1; p <= data.m_pageCount; ++p)
    {
        CHECK(FindById(root.get(), (UINT)(kIdHistoryPageNumBase + (p - 1))) != nullptr);
    }
    CHECK(FindById(root.get(), (UINT)(kIdHistoryPageNumBase + data.m_pageCount)) == nullptr);
}

TEST_CASE("V2L4 控件 id 无重复")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    std::vector<UINT> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    bool hasDup = (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end());
    CHECK_FALSE(hasDup);
}

// ---------------------------------------------------------------------------
// V2L5-V2L8  控件 id 分段、空状态、省略式分页器。
//            方案见 docs/消息记录界面优化方案.md §3。
// ---------------------------------------------------------------------------

TEST_CASE("V2L5 各段动态 id 互不重叠且容量够用")
{
    //这条是 2026-08-04 那个 bug 的哨兵：页码段原本只有 600~699 共 100 个位置，
    //而分发处拿"下一段的基址(700)"当上界，于是第 101 页的按钮 id 恰好落进
    //"定位"段，点第 101 页变成"定位到第 0 行"。每页 8 条、800 条消息就能触到。
    const UINT pageEnd   = (UINT)kIdHistoryPageNumBase + kIdHistoryPageNumCapacity;
    const UINT locateEnd = (UINT)kIdHistoryLocateBase + kIdHistoryLocateCapacity;
    const UINT pinEnd    = (UINT)kIdHistoryPinBase + kIdHistoryPinCapacity;
    const UINT rowEnd    = (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity;
    const UINT expandEnd = (UINT)kIdHistoryExpandBase + kIdHistoryExpandCapacity;

    //各段按基址递增排列，且前一段的末尾不越进后一段的开头
    CHECK((UINT)kIdHistoryPageNumBase < (UINT)kIdHistoryLocateBase);
    CHECK((UINT)kIdHistoryLocateBase < (UINT)kIdHistoryPinBase);
    CHECK((UINT)kIdHistoryPinBase < (UINT)kIdHistoryRowBase);
    CHECK(pageEnd <= (UINT)kIdHistoryLocateBase);
    CHECK(locateEnd <= (UINT)kIdHistoryPinBase);
    CHECK(pinEnd <= (UINT)kIdHistoryRowBase);
    CHECK((UINT)kIdHistoryRowBase < (UINT)kIdHistoryExpandBase);
    CHECK(rowEnd <= (UINT)kIdHistoryExpandBase);
    CHECK(expandEnd > (UINT)kIdHistoryExpandBase);

    //固定 id 都在 5xx 段，不能被这几段动态 id 盖住
    CHECK((UINT)kIdHistoryPageJump < (UINT)kIdHistoryPageNumBase);
    CHECK((UINT)kIdHistoryEmptySub < (UINT)kIdHistoryPageNumBase);

    //页码容量要能装下很大的会话：省略式分页器一次只渲染十来个，这里是兜底
    CHECK(kIdHistoryPageNumCapacity >= 1000);
    //行相关的容量要远大于每页条数（当前 8）
    CHECK(kIdHistoryLocateCapacity >= 100);
    CHECK(kIdHistoryPinCapacity >= 100);
    CHECK(kIdHistoryRowCapacity >= 100);
    CHECK(kIdHistoryExpandCapacity >= 100);
}

TEST_CASE("V2L6 列表为空时产出空状态，且一行都不建")
{
    std::vector<HistoryRowView> none;
    std::unique_ptr<DuiControl> content =
        BuildListContent(none, _T("没有找到「季度报告」相关的消息"), _T("换个词试试"));
    REQUIRE(content != nullptr);

    CHECK(FindById(content.get(), kIdHistoryEmptyTitle) != nullptr);
    CHECK(FindById(content.get(), kIdHistoryEmptySub) != nullptr);

    //一行都不该有：行是靠这几个 id 认的
    CHECK(FindById(content.get(), (UINT)kIdHistoryRowBase) == nullptr);
    CHECK(FindById(content.get(), (UINT)kIdHistoryLocateBase) == nullptr);
    CHECK(FindById(content.get(), (UINT)kIdHistoryPinBase) == nullptr);

    //副文案为空串时不建那个控件，免得空标签占一行高度、把主文案顶偏
    std::unique_ptr<DuiControl> noSub =
        BuildListContent(none, _T("这个会话还没有消息"), _T(""));
    REQUIRE(noSub != nullptr);
    CHECK(FindById(noSub.get(), kIdHistoryEmptyTitle) != nullptr);
    CHECK(FindById(noSub.get(), kIdHistoryEmptySub) == nullptr);
}

TEST_CASE("V2L7 列表非空时不出现空状态")
{
    std::vector<HistoryRowView> rows;
    HistoryRowView r;
    r.m_senderName  = _T("苏沐辰");
    r.m_timeText    = _T("11:01");
    r.m_previewText = _T("改了发你看下");
    r.m_msgid       = 9001;
    rows.push_back(r);

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T("不该出现的主文案"), _T("不该出现的副文案"));
    REQUIRE(content != nullptr);

    CHECK(FindById(content.get(), kIdHistoryEmptyTitle) == nullptr);
    CHECK(FindById(content.get(), kIdHistoryEmptySub) == nullptr);
    //这一行本身要在
    CHECK(FindById(content.get(), (UINT)kIdHistoryRowBase) != nullptr);
    CHECK(FindById(content.get(), (UINT)kIdHistoryLocateBase) != nullptr);
    CHECK(FindById(content.get(), (UINT)kIdHistoryPinBase) != nullptr);
}

TEST_CASE("V2L8 页数很多时分页器只建有限个页码按钮")
{
    //原先按总页数平铺：625 页就是 625 个按钮塞进一行，整行失控。
    //改省略式之后，无论多少页，页码按钮数都不超过 kHistoryPagerMaxCells。
    HistoryViewData data;
    FillMockHistory(data);
    data.m_pageCount   = 625;
    data.m_currentPage = 300;

    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);

    int built = 0;
    for (int p = 1; p <= data.m_pageCount; ++p)
    {
        if (FindById(root.get(), (UINT)(kIdHistoryPageNumBase + (p - 1))) != nullptr)
        {
            ++built;
        }
    }
    CHECK(built <= kHistoryPagerMaxCells);
    CHECK(built > 0);

    //首页、末页、当前页三格恒在 —— 少了任何一个都会让用户失去落脚点
    CHECK(FindById(root.get(), (UINT)(kIdHistoryPageNumBase + 0)) != nullptr);
    CHECK(FindById(root.get(), (UINT)(kIdHistoryPageNumBase + 624)) != nullptr);
    CHECK(FindById(root.get(), (UINT)(kIdHistoryPageNumBase + 299)) != nullptr);

    //跳页微调框在（省略式页码没法直达中间某页，靠它兜底）
    CHECK(FindById(root.get(), kIdHistoryPageJump) != nullptr);
}

// =====================================================================
// V2L9. 自定义时间区间行的六个下拉必须放得下自己最宽的那一项
//
// 这几个下拉原先写死像素宽（年 76 / 月 56 / 日 56）。默认字体是按当前 DPI 算出来的
// （DuiResMgr：lfHeight = -MulDiv(9, dpi, 72)），而 DuiLayout 的 Fixed() 给的是不随
// DPI 变化的像素 —— 125% 缩放下“2026年”就被 DT_END_ELLIPSIS 截成了“202...”，“8月”
// 截成“8...”。改为按实测字宽定宽之后，用本用例把"放得下"这条契约钉住。
// =====================================================================

namespace {

// 用与 DuiComboBox 绘制时同一支字体量文字宽度（像素）。
int MeasureWithDefaultFont(LPCTSTR text)
{
    HDC hdc = ::GetDC(NULL);
    REQUIRE(hdc != NULL);
    HFONT hFont = DuiResMgr::Inst().GetDefaultFont();
    HFONT hOld = (hFont != NULL) ? (HFONT)::SelectObject(hdc, hFont) : NULL;
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32(hdc, text, ::lstrlen(text), &sz);
    if (hOld != NULL)
    {
        ::SelectObject(hdc, hOld);
    }
    ::ReleaseDC(NULL, hdc);
    return sz.cx;
}

// DuiComboBox 只读态下留给内边距与右侧箭头的那一截（见 HistoryWindowLayout.cpp
// 的 kComboChromeW，两处必须一致）。
const int kComboChromeForTest = 34;

} // anonymous namespace

TEST_CASE("V2L9 自定义时间区间的年月日下拉宽度放得下最宽的一项")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);

    //自定义区间行默认隐藏、不占位，须先显示出来再布局，否则量到的矩形全是空的。
    DuiControl* customRow = const_cast<DuiControl*>(
        FindById(root.get(), kIdHistoryCustomRow));
    REQUIRE(customRow != nullptr);
    customRow->SetVisible(true);
    root->ForceLayout(RECT{ 0, 0, 900, 600 });

    struct ComboExpect
    {
        UINT    m_id;        //下拉控件 id
        LPCTSTR m_widest;    //该下拉里最宽的一项文字
    };
    //年份下拉的取值范围是 2018..2035，全是四位数，取哪一年量出来都一样宽；
    //月份最宽的是两位数的 10~12 月，日期最宽的是两位数的 10~31 日。
    const ComboExpect kExpects[] =
    {
        { kIdHistoryCustomStartY, _T("2035年") },
        { kIdHistoryCustomStartM, _T("12月") },
        { kIdHistoryCustomStartD, _T("31日") },
        { kIdHistoryCustomEndY,   _T("2035年") },
        { kIdHistoryCustomEndM,   _T("12月") },
        { kIdHistoryCustomEndD,   _T("31日") },
    };

    for (int i = 0; i < (int)(sizeof(kExpects) / sizeof(kExpects[0])); ++i)
    {
        const DuiControl* combo = FindById(root.get(), kExpects[i].m_id);
        REQUIRE(combo != nullptr);
        RECT rc = combo->GetRect();
        const int actualW = rc.right - rc.left;
        const int needW = MeasureWithDefaultFont(kExpects[i].m_widest) + kComboChromeForTest;
        INFO("下拉 id=" << kExpects[i].m_id << " 实宽=" << actualW << " 需要=" << needW);
        CHECK(actualW >= needW);
    }

    //起止两组同名下拉应当一样宽 —— 内容一模一样，宽度不齐看着别扭。
    CHECK((FindById(root.get(), kIdHistoryCustomStartY)->GetRect().right
           - FindById(root.get(), kIdHistoryCustomStartY)->GetRect().left)
          == (FindById(root.get(), kIdHistoryCustomEndY)->GetRect().right
              - FindById(root.get(), kIdHistoryCustomEndY)->GetRect().left));
    CHECK((FindById(root.get(), kIdHistoryCustomStartM)->GetRect().right
           - FindById(root.get(), kIdHistoryCustomStartM)->GetRect().left)
          == (FindById(root.get(), kIdHistoryCustomEndM)->GetRect().right
              - FindById(root.get(), kIdHistoryCustomEndM)->GetRect().left));
}

// =====================================================================
// V2L10. 时间下拉：项的顺序必须与 HistoryTimeRange 一一对应，且放得下最宽的一项
//
// 窗口侧的 TimeRangeFromComboIndex / ComboIndexFromRange 是照"下标即枚举值"这条
// 约定直接转换的 —— 谁往中间插一项而忘了改枚举，筛选就会静默地筛错档（选"近半年"
// 查出来是"近 3 个月"，界面上看不出任何异常）。本用例把这条对应关系钉死。
// =====================================================================

TEST_CASE("V2L10 时间下拉项与 HistoryTimeRange 一一对应且宽度够用")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);

    const DuiComboBox* combo = dynamic_cast<const DuiComboBox*>(
        FindById(root.get(), kIdHistoryTimeCombo));
    REQUIRE(combo != nullptr);

    struct TimeItemExpect
    {
        HistoryTimeRange m_range;   //该项对应的时间范围
        LPCTSTR          m_text;    //该项的显示文字
    };
    const TimeItemExpect kExpects[] =
    {
        { kHistoryTimeAll,     _T("全部时间") },
        { kHistoryTime7Days,   _T("近 7 天") },
        { kHistoryTime30Days,  _T("近 30 天") },
        { kHistoryTime3Months, _T("近 3 个月") },
        { kHistoryTime6Months, _T("近半年") },
        { kHistoryTime1Year,   _T("近一年") },
        { kHistoryTimeCustom,  _T("自定义...") },
    };
    const int kExpectCount = (int)(sizeof(kExpects) / sizeof(kExpects[0]));

    REQUIRE(combo->GetCount() == kExpectCount);
    for (int i = 0; i < kExpectCount; ++i)
    {
        INFO("第 " << i << " 项");
        //下标即枚举值 —— 这正是两个换算函数依赖的那条约定。
        CHECK((int)kExpects[i].m_range == i);
        CHECK(combo->GetItemText(i) == CString(kExpects[i].m_text));
    }

    //宽度得放得下最宽的一项，否则高 DPI 下会被截成“全部时...”。
    root->ForceLayout(RECT{ 0, 0, 900, 600 });
    RECT rc = combo->GetRect();
    const int actualW = rc.right - rc.left;
    int needW = 0;
    for (int i = 0; i < kExpectCount; ++i)
    {
        const int w = MeasureWithDefaultFont(kExpects[i].m_text) + kComboChromeForTest;
        if (w > needW)
        {
            needW = w;
        }
    }
    INFO("时间下拉 实宽=" << actualW << " 需要=" << needW);
    CHECK(actualW >= needW);
}

// =====================================================================
// V2L11-V2L13  行正文改成多行内容块之后的布局契约
//
// 行高从编译期常量变成了"按正文在给定宽度下折出几行来算"，这三条把它钉住：
// 长文比短文高、展开比折叠高、以及"只有真的放不下的行才挂展开链接"。
// 方案见 docs/消息记录内容交互方案.md §5 F1。
// =====================================================================

namespace {

// 造一行只有正文不同的测试数据。
HistoryRowView MakeContentRow(LPCTSTR fullText, bool expanded, long long msgid)
{
    HistoryRowView row;
    row.m_senderName = _T("苏沐辰");
    row.m_timeText   = _T("11:01");
    row.m_previewText = fullText;
    row.m_fullText   = fullText;
    row.m_expanded   = expanded;
    row.m_msgid      = msgid;
    return row;
}

// 取列表里第 index 行控件的高度（像素）。列表须已布局过。
int RowHeightAt(const DuiControl* listContent, int index)
{
    const DuiControl* row = FindById(listContent, (UINT)(kIdHistoryRowBase + index));
    if (row == nullptr)
    {
        return -1;
    }
    RECT rc = row->GetRect();
    return rc.bottom - rc.top;
}

// 这段长正文在测试用的列表宽度下必然超过折叠行数上限（5 行）。
LPCTSTR const kLongText =
    _T("对了，关于这次品牌升级我整理了几点想法：主色调建议从橙色微调到更暖一些的")
    _T("珊瑚色，辅助色保留现在的灰绿搭配；字体方面思源黑体确实更适合长段落阅读，")
    _T("标题也可以试试更重的字重。另外 banner 那块的留白我觉得还可以再松一点，")
    _T("现在四周挤得有些满，视线没有落脚的地方。你看周五之前能不能出一版对比稿？")
    _T("另外上次提到的图标风格，我倾向于统一走线性，实心的那套在小尺寸下糊成一团。");

// 测试用的列表宽度（像素）。取一个偏窄的值，好让长正文确实折出很多行。
const int kTestListWidth = 520;

} // anonymous namespace

TEST_CASE("V2L11 长正文的行比短正文的行高")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeContentRow(_T("短"), false, 9001));
    rows.push_back(MakeContentRow(kLongText, false, 9002));

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, 2000 });

    const int shortH = RowHeightAt(content.get(), 0);
    const int longH  = RowHeightAt(content.get(), 1);
    REQUIRE(shortH > 0);
    REQUIRE(longH > 0);
    INFO("短行高=" << shortH << " 长行高=" << longH);
    CHECK(longH > shortH);
}

TEST_CASE("V2L12 同一条长正文，展开态的行比折叠态高")
{
    std::vector<HistoryRowView> collapsed;
    collapsed.push_back(MakeContentRow(kLongText, false, 9101));
    std::unique_ptr<DuiControl> c1 =
        BuildListContent(collapsed, _T(""), _T(""), kTestListWidth);
    REQUIRE(c1 != nullptr);
    c1->ForceLayout(RECT{ 0, 0, kTestListWidth, 4000 });

    std::vector<HistoryRowView> expanded;
    expanded.push_back(MakeContentRow(kLongText, true, 9101));
    std::unique_ptr<DuiControl> c2 =
        BuildListContent(expanded, _T(""), _T(""), kTestListWidth);
    REQUIRE(c2 != nullptr);
    c2->ForceLayout(RECT{ 0, 0, kTestListWidth, 4000 });

    const int collapsedH = RowHeightAt(c1.get(), 0);
    const int expandedH  = RowHeightAt(c2.get(), 0);
    REQUIRE(collapsedH > 0);
    REQUIRE(expandedH > 0);
    INFO("折叠=" << collapsedH << " 展开=" << expandedH);
    CHECK(expandedH > collapsedH);
}

TEST_CASE("V2L13 只有正文放不下的行才挂「展开 / 收起」链接")
{
    //第 0 行短、第 1 行长。只有第 1 行该有链接控件。
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeContentRow(_T("短短一句"), false, 9201));
    rows.push_back(MakeContentRow(kLongText, false, 9202));

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);

    CHECK(FindById(content.get(), (UINT)(kIdHistoryExpandBase + 0)) == nullptr);
    const DuiControl* link = FindById(content.get(), (UINT)(kIdHistoryExpandBase + 1));
    REQUIRE(link != nullptr);

    //链接文案随展开态取反 —— 展开之后它得变成「收起」，否则没有回头路。
    const DuiLabel* label = dynamic_cast<const DuiLabel*>(link);
    REQUIRE(label != nullptr);
    CHECK(label->GetText() == CString(_T("展开")));
    //可点：ModeText 是纯绘制控件，点了没反应。
    CHECK(label->GetMode() == DuiLabel::ModeLink);

    std::vector<HistoryRowView> opened;
    opened.push_back(MakeContentRow(kLongText, true, 9202));
    std::unique_ptr<DuiControl> content2 =
        BuildListContent(opened, _T(""), _T(""), kTestListWidth);
    REQUIRE(content2 != nullptr);
    const DuiLabel* label2 = dynamic_cast<const DuiLabel*>(
        FindById(content2.get(), (UINT)(kIdHistoryExpandBase + 0)));
    REQUIRE(label2 != nullptr);
    CHECK(label2->GetText() == CString(_T("收起")));
}
