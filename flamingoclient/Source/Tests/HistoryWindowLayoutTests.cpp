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
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"       // DUIN_EDIT_ENTER：「选日期」弹层的回车判定（DR9）
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"   // 时间下拉的项与宽度断言
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"      // “展开 / 收起”链接的文案与模式断言
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"     // 从行内取出头像控件断言其尺寸
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"     // 多选模式下行首复选框的勾选状态断言
#include "../UI/BalloonHistory/HistoryRow.h"                                         // HistoryRowThumb：从行内取出文件类型色块
#include "../UI/BalloonHistory/HistoryContentView.h"                                 // 从行内取出正文控件，核对其高度是否够用
#include "../UI/BalloonChatWnd/ImagePlaceholder.h"                                   // HistoryThumbState：缩略图状态取值
#include "../net/Protocol.h"                                                         // message_type_image：解析图片消息内容的用例要用
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView：按窗口刷新列表的流程换上列表
#include "../UI/BalloonMain/MainPanelScrollView.h"                                 // 列表滚动视口改用悬浮式自动隐藏滚动条（V2L26）

#include <algorithm>
#include <vector>
#include <gdiplus.h>   // 底部一行效果图（V2LSNAP）把画布存成 PNG

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
    const UINT thumbEnd  = (UINT)kIdHistoryThumbBase + kIdHistoryThumbCapacity;

    //各段按基址递增排列，且前一段的末尾不越进后一段的开头
    CHECK((UINT)kIdHistoryPageNumBase < (UINT)kIdHistoryLocateBase);
    CHECK((UINT)kIdHistoryLocateBase < (UINT)kIdHistoryPinBase);
    CHECK((UINT)kIdHistoryPinBase < (UINT)kIdHistoryRowBase);
    CHECK(pageEnd <= (UINT)kIdHistoryLocateBase);
    CHECK(locateEnd <= (UINT)kIdHistoryPinBase);
    CHECK(pinEnd <= (UINT)kIdHistoryRowBase);
    CHECK((UINT)kIdHistoryRowBase < (UINT)kIdHistoryExpandBase);
    CHECK(rowEnd <= (UINT)kIdHistoryExpandBase);
    CHECK((UINT)kIdHistoryExpandBase < (UINT)kIdHistoryThumbBase);
    CHECK(expandEnd <= (UINT)kIdHistoryThumbBase);
    CHECK(thumbEnd > (UINT)kIdHistoryThumbBase);

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
    CHECK(kIdHistoryThumbCapacity >= 100);

    //Markdown 卡片段（docs/Markdown消息方案.md 第 12 节）排在已有各个行级段之后，与它们都不相交，容量够用
    const UINT markdownBase = (UINT)kIdHistoryMarkdownCardBase;
    CHECK(markdownBase >= (UINT)kIdHistoryCodeCardBase + kIdHistoryCodeCardCapacity);
    CHECK(markdownBase >= (UINT)kIdHistoryVoiceBase + kIdHistoryVoiceCapacity);
    CHECK(markdownBase >= (UINT)kIdHistoryNavMonthBase + kIdHistoryNavMonthCapacity);
    CHECK(markdownBase >= (UINT)kIdHistoryNavYearBase);
    CHECK(kIdHistoryMarkdownCardCapacity >= 100);
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

//V2L9（自定义时间区间那一行的年月日下拉宽度）随那一行一起删除：自定义起止改由「选日期」
//弹层设置（2026-10-01，见 HistoryDateRange.h）。

// =====================================================================
// V2L10. 时间下拉：项的顺序必须与 HistoryTimeRange 一一对应，且放得下最宽的一项
//
// 窗口侧的 TimeRangeFromComboIndex / ComboIndexFromRange 是照"下标即枚举值"这条
// 约定直接转换的 —— 谁往中间插一项而忘了改枚举，筛选就会静默地筛错档（选"近半年"
// 查出来是"近 3 个月"，界面上看不出任何异常）。本用例把这条对应关系钉死。
// =====================================================================

TEST_CASE("V2L10 时间下拉项与 HistoryTimeRange 一一对应且宽度够用，不再有自定义")
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
    };
    //自定义起止不在下拉框里，由「选日期」弹层的「只看这段时间」设置（DR6）
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

// =====================================================================
// V2L14-V2L16  行内固定尺寸的控件不随行高拉伸
//
// 每一行是一个 DuiHBox，子控件在交叉轴（竖直方向）上默认撑满整行。正文一长、行高
// 增加，头像、文件类型色块、Pin / 定位按钮就会被纵向拉成长条（2026-09-30 报告，
// 见 snapshots/Q11.png）。这三条用例断言它们在长正文行里仍保持固定尺寸，并贴在
// 行内容区的顶部，与昵称、时间那一行对齐。
// =====================================================================

namespace {

// 以下期望值与 HistoryWindowLayout.cpp 中的同名常量一致。那几个常量是该文件内部的
// 常量，测试无法直接引用，故在此各写一份；产品代码调整尺寸时须同步修改这里。
const int kExpectAvatarSize    = 36;   // 头像边长（像素），对应 kAvatarSize
const int kExpectFileBlockSize = 32;   // 文件卡片里类型色块的边长（像素），对应 kFileCardBlockSize
const int kExpectRowPadTop     = 8;    // 行的上内边距（像素），对应 kRowPadV
const int kExpectActionH       = 40;   // Pin / 定位按钮槽的高度（像素），对应 kRowActionH

// 在一个控件及其全部后代里找第一个类型为 T 的控件（先序遍历）。找不到返回 nullptr。
template <typename T>
const T* FindFirstInSubtree(const DuiControl* node)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    const T* self = dynamic_cast<const T*>(node);
    if (self != nullptr)
    {
        return self;
    }
    for (const std::unique_ptr<DuiControl>& child : node->Children())
    {
        const T* hit = FindFirstInSubtree<T>(child.get());
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// 布局时给列表的高度（像素）。取一个足够大的值，使展开态的长正文行也能完整排下。
const int kTestLayoutHeight = 4000;

// 在一行的直接子控件里找第一个类型为 T 的控件（头像、文件类型色块在一行里各只有
// 一个）。找不到返回 nullptr。
template <typename T>
const T* FindDirectChild(const DuiControl* row)
{
    if (row == nullptr)
    {
        return nullptr;
    }
    for (const std::unique_ptr<DuiControl>& child : row->Children())
    {
        const T* hit = dynamic_cast<const T*>(child.get());
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// 控件矩形的宽 / 高（像素）。
int RectWidth(const RECT& rc)
{
    return rc.right - rc.left;
}

int RectHeight(const RECT& rc)
{
    return rc.bottom - rc.top;
}

// 取按 id 找到的按钮的外层按钮槽。按钮平时隐藏，而布局会跳过不可见的子控件，按钮
// 自身的矩形不可靠；外层槽始终可见，它的矩形才代表这一列实际占的位置。
const DuiControl* ActionSlotOf(const DuiControl* listContent, UINT buttonId)
{
    const DuiControl* button = FindById(listContent, buttonId);
    if (button == nullptr)
    {
        return nullptr;
    }
    return button->GetParent();
}

} // anonymous namespace

TEST_CASE("V2L14 头像在长短正文行里都是固定尺寸的正方形，并贴在行内容区顶部")
{
    //第 0 行短正文，第 1 行长正文且处于展开态 —— 展开态的行最高，最容易暴露拉伸。
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeContentRow(_T("短"), false, 9301));
    rows.push_back(MakeContentRow(kLongText, true, 9302));

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });

    for (int i = 0; i < (int)rows.size(); ++i)
    {
        const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != nullptr);
        const DuiAvatar* avatar = FindDirectChild<DuiAvatar>(row);
        REQUIRE(avatar != nullptr);

        const RECT rcRow    = row->GetRect();
        const RECT rcAvatar = avatar->GetRect();
        INFO("第 " << i << " 行：行高=" << RectHeight(rcRow)
             << " 头像=" << RectWidth(rcAvatar) << "x" << RectHeight(rcAvatar)
             << " 头像顶边相对行顶=" << (rcAvatar.top - rcRow.top));
        CHECK(RectWidth(rcAvatar) == kExpectAvatarSize);
        CHECK(RectHeight(rcAvatar) == kExpectAvatarSize);
        CHECK(rcAvatar.top == rcRow.top + kExpectRowPadTop);
    }
}

TEST_CASE("V2L15 文件卡片里的类型色块保持固定尺寸，且不再是行的直接子控件")
{
    //2026-09-30 起文件消息改为在昵称下方显示一张卡片（类型色块 + 文件名 + 大小），
    //头像右侧不再单独占一列。色块在卡片里也必须保持固定尺寸，不随卡片高度拉伸。
    HistoryRowView fileRow = MakeContentRow(_T("季度报告.xlsx · 182.8 KB"), false, 9401);
    fileRow.m_thumbFileExt = _T("xlsx");
    fileRow.m_fileName     = _T("季度报告.xlsx");
    fileRow.m_fileSizeText = _T("182.8 KB");
    std::vector<HistoryRowView> rows;
    rows.push_back(fileRow);

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });

    const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    REQUIRE(row != nullptr);
    CHECK(FindDirectChild<HistoryRowThumb>(row) == nullptr);
    const HistoryRowThumb* thumb = FindFirstInSubtree<HistoryRowThumb>(row);
    REQUIRE(thumb != nullptr);

    const RECT rcThumb = thumb->GetRect();
    INFO("色块=" << RectWidth(rcThumb) << "x" << RectHeight(rcThumb));
    CHECK(RectWidth(rcThumb) == kExpectFileBlockSize);
    CHECK(RectHeight(rcThumb) == kExpectFileBlockSize);
}

TEST_CASE("V2L16 Pin 与定位按钮槽在长短正文行里高度相同，并贴在行内容区顶部")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeContentRow(_T("短"), false, 9501));
    rows.push_back(MakeContentRow(kLongText, true, 9502));

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });

    for (int i = 0; i < (int)rows.size(); ++i)
    {
        const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != nullptr);
        const DuiControl* pinSlot    = ActionSlotOf(content.get(), (UINT)(kIdHistoryPinBase + i));
        const DuiControl* locateSlot = ActionSlotOf(content.get(), (UINT)(kIdHistoryLocateBase + i));
        REQUIRE(pinSlot != nullptr);
        REQUIRE(locateSlot != nullptr);

        const RECT rcRow    = row->GetRect();
        const RECT rcPin    = pinSlot->GetRect();
        const RECT rcLocate = locateSlot->GetRect();
        INFO("第 " << i << " 行：行高=" << RectHeight(rcRow)
             << " Pin 槽高=" << RectHeight(rcPin)
             << " 定位槽高=" << RectHeight(rcLocate)
             << " Pin 槽顶边相对行顶=" << (rcPin.top - rcRow.top)
             << " 定位槽顶边相对行顶=" << (rcLocate.top - rcRow.top));
        CHECK(RectHeight(rcPin) == kExpectActionH);
        CHECK(RectHeight(rcLocate) == kExpectActionH);
        CHECK(rcPin.top == rcRow.top + kExpectRowPadTop);
        CHECK(rcLocate.top == rcRow.top + kExpectRowPadTop);
    }
}

// =====================================================================
// V2L17  列表滚动视口给窗口右侧的缩放边框让出位置
//
// DuiFrameWindow 把窗口最外侧 GetBorderPx() 个逻辑像素判为缩放边框（HTRIGHT 等），
// 列表滚动视口原先铺满整个客户区宽度，滚动条紧贴窗口右缘，大半落在缩放边框里：
// 鼠标移到滑块上只会变成左右双向箭头，无法拖动（2026-09-30 报告，见
// snapshots/144150.png）。BuildHistoryWindowBody 为此新增 listRightInsetPx 参数，
// 本用例断言滚动视口的右缘按该参数左移，且不传时行为与改动前一致。
// =====================================================================

namespace {

// 布局整窗时给客户区的宽 / 高（像素），取一个常见的窗口尺寸。
const int kTestBodyWidth  = 880;
const int kTestBodyHeight = 700;

// 测试用的右侧让出宽度（像素）：8 个逻辑像素的缩放边框在 150% 缩放下折算为 12 个物理像素。
const int kTestListRightInset = 12;

// 按给定的让出宽度构建整窗并布局，返回列表滚动视口的矩形。视口找不到时返回全 0 矩形。
RECT LayoutAndGetScrollRect(const HistoryViewData& data, int listRightInsetPx,
                            std::unique_ptr<DuiControl>& outRoot)
{
    outRoot = BuildHistoryWindowBody(data, listRightInsetPx);
    RECT empty = { 0, 0, 0, 0 };
    if (!outRoot)
    {
        return empty;
    }
    outRoot->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });
    const DuiControl* scroll = FindById(outRoot.get(), kIdHistoryScroll);
    if (scroll == nullptr)
    {
        return empty;
    }
    return scroll->GetRect();
}

} // anonymous namespace

TEST_CASE("V2L17 列表滚动视口按传入的宽度给窗口右侧缩放边框让位")
{
    HistoryViewData data;
    FillMockHistory(data);

    //不传让出宽度：滚动视口铺满客户区宽度，与改动前的行为一致。
    std::unique_ptr<DuiControl> plainRoot;
    const RECT rcPlain = LayoutAndGetScrollRect(data, 0, plainRoot);
    INFO("不让位时视口 left=" << rcPlain.left << " right=" << rcPlain.right);
    REQUIRE(RectWidth(rcPlain) > 0);
    CHECK(rcPlain.right == kTestBodyWidth);

    //传入让出宽度：滚动视口的右缘左移这么多，左缘保持不变。
    std::unique_ptr<DuiControl> insetRoot;
    const RECT rcInset = LayoutAndGetScrollRect(data, kTestListRightInset, insetRoot);
    INFO("让位时视口 left=" << rcInset.left << " right=" << rcInset.right);
    CHECK(rcInset.right == kTestBodyWidth - kTestListRightInset);
    CHECK(rcInset.left == rcPlain.left);

    //负值按 0 处理：不会把视口撑出客户区之外。
    std::unique_ptr<DuiControl> negativeRoot;
    const RECT rcNegative = LayoutAndGetScrollRect(data, -kTestListRightInset, negativeRoot);
    INFO("传负值时视口 right=" << rcNegative.right);
    CHECK(rcNegative.right == kTestBodyWidth);
}

// =====================================================================
// V2L18  正文控件分到的高度足够容纳它按实际宽度排出的全部行
//
// 行高是按 RowContentWidth 算出的宽度测量正文得到的，而正文绘制时用的是控件排版后的
// 实际宽度。两者不一致、且实际宽度更窄时，正文会多折出几行，画出本行的范围，压在下
// 一行上（2026-09-30 报告，见 snapshots/145519.png）。本用例按窗口刷新列表的同一
// 流程（先布局整窗，再按滚动视口的实际宽度重建列表）构建，然后逐行核对。
// =====================================================================

namespace {

// 仿照调试器调用栈的长正文：各行长短不一，多数接近满宽，最容易暴露测量宽度与实际
// 宽度之间的差异。
LPCTSTR const kStackTraceText =
    _T(" Flamingo.exe!std::_Default_allocate_traits::_Allocate(const unsigned int _Bytes=1164724240) Line 253C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!std::allocator<char>::allocate(const unsigned int _Count=1164724240) Line 990C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!std::_Allocate_at_least_helper<std::allocator<char>>(std::allocator<char> & _Al={...}, unsigned int & _Count=1164724240) Line 2305C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!std::string::_Allocate_for_capacity<0>(std::allocator<char> & _Al={...}, unsigned int & _Capacity=1164724240) Line 829C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!std::string::resize(const unsigned int _New_size=1164724235, const char _Ch='\\0') Line 2392C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!net::readWholeFile(const std::wstring & path={...}, std::string & out={...}) Line 48C++Symbols loaded.\n")
    _T(" Flamingo.exe!net::FileTransfer::ProcessGroupFileUpload(const net::FileTransfer::Task & task={...}) Line 778C++Symbols loaded.\n")
    _T(" Flamingo.exe!net::FileTransfer::WorkerProc() Line 476C++Symbols loaded.\n")
    _T(" Flamingo.exe!std::invoke<void (__thiscall net::FileTransfer::*)(void),net::FileTransfer *>(void(net::FileTransfer::*)() && _Obj=0x00e2cf99, net::FileTransfer * && _Arg1=0x0180b838) Line 1686C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!std::thread::_Invoke<std::tuple<void (__thiscall net::FileTransfer::*)(void),net::FileTransfer *>,0,1>(void * _RawVals=0x00a50b48) Line 60C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!thread_start<unsigned int (__stdcall*)(void *),1>(void * const parameter=0x1258b3a0) Line 97C++Symbols loaded.\n")
    _T(" [Frames below may be incorrect and/or missing, no symbols loaded for KernelBase.dll]Annotated Frame\n")
    _T(">Flamingo.exe!_CxxThrowException(void * pExceptionObject=0x0625f654, const _s__ThrowInfo * pThrowInfo=0x017e62cc) Line 79C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!__scrt_throw_std_bad_alloc() Line 35C++Non-user code. Symbols loaded.\n")
    _T(" Flamingo.exe!operator new(unsigned int size=1164724279) Line 49C++Non-user code. Symbols loaded.");

// 在子树里找第一个 HistoryContentView（每一行里只有一个）。找不到返回 nullptr。
const HistoryContentView* FindContentView(const DuiControl* node)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    const HistoryContentView* self = dynamic_cast<const HistoryContentView*>(node);
    if (self != nullptr)
    {
        return self;
    }
    for (const std::unique_ptr<DuiControl>& child : node->Children())
    {
        const HistoryContentView* hit = FindContentView(child.get());
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

} // anonymous namespace

TEST_CASE("V2L18 正文控件分到的高度足够容纳它按实际宽度排出的全部行")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(root->FindCtrlById(kIdHistoryScroll));
    REQUIRE(scroll != nullptr);

    //三种行：纯文字的调用栈、纯文字的中文长段落、带文件类型色块的调用栈。都处于
    //展开态，行最高、折出的行数最多。
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeContentRow(kStackTraceText, true, 9601));
    rows.push_back(MakeContentRow(kLongText, true, 9602));
    HistoryRowView fileRow = MakeContentRow(kStackTraceText, true, 9603);
    fileRow.m_thumbFileExt = _T("txt");
    rows.push_back(fileRow);

    //与 CBalloonHistoryWindow 刷新列表的流程一致：取滚动视口的实际宽度重建列表并布局。
    const RECT rcScroll = scroll->GetRect();
    const int listWidth = RectWidth(rcScroll);
    scroll->SetContent(BuildListContent(rows, _T(""), _T(""), listWidth));
    scroll->ForceLayout(scroll->GetRect());

    for (int i = 0; i < (int)rows.size(); ++i)
    {
        const DuiControl* row = FindById(scroll, (UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != nullptr);
        const HistoryContentView* view = FindContentView(row);
        REQUIRE(view != nullptr);

        const RECT rcView = view->GetRect();
        const int needH = view->MeasureHeight(RectWidth(rcView));
        INFO("第 " << i << " 行：列表宽=" << listWidth
             << " 正文实际宽=" << RectWidth(rcView)
             << " 分到的高=" << RectHeight(rcView)
             << " 按实际宽排版所需的高=" << needH);
        CHECK(RectHeight(rcView) >= needH);
    }
}

// =====================================================================
// V2L19  图片行的缩略图：带缩略图 id 段的 ctrlId，行数据里的状态传到控件上
//
// 消息记录窗里本地没有原图的图片可以单击缩略图下载（设计稿见
// snapshots/history_image_download_placeholder_design.png）。缩略图要有自己的 ctrlId
// 才能在单击时告诉窗口"下载第几行的图"；文字行没有缩略图，文件行的类型色块不可点击，
// 两者都不占这一段 id。
// =====================================================================

TEST_CASE("V2L19 图片行的缩略图带缩略图段 ctrlId，并收到行数据里的状态")
{
    std::vector<HistoryRowView> rows;

    //第 0 行：图片，本地没有原图、可以下载。调试入口没有登录态时路径为空，同样要建缩略图。
    HistoryRowView image = MakeContentRow(_T("[图片]"), false, 9701);
    image.m_hasImageThumb  = true;
    image.m_thumbState     = balloonchatwnd::kHistoryThumbNeedDownload;
    image.m_thumbClickable = true;
    image.m_thumbTooltip   = _T("单击下载图片");
    rows.push_back(image);

    //第 1 行：纯文字，没有缩略图。
    rows.push_back(MakeContentRow(_T("一句话"), false, 9702));

    //第 2 行：文件，只有类型色块。
    HistoryRowView file = MakeContentRow(_T("季度报告.xlsx"), false, 9703);
    file.m_thumbFileExt = _T("xlsx");
    rows.push_back(file);

    std::unique_ptr<DuiControl> content =
        BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);

    const HistoryRowThumb* thumb = dynamic_cast<const HistoryRowThumb*>(
        FindById(content.get(), (UINT)(kIdHistoryThumbBase + 0)));
    REQUIRE(thumb != nullptr);
    CHECK(thumb->GetImageState() == balloonchatwnd::kHistoryThumbNeedDownload);
    CHECK(thumb->IsClickable());

    //文字行与文件行都不占缩略图段的 id。
    CHECK(FindById(content.get(), (UINT)(kIdHistoryThumbBase + 1)) == nullptr);
    CHECK(FindById(content.get(), (UINT)(kIdHistoryThumbBase + 2)) == nullptr);

    //文件行仍然画类型色块（只是不可点击）。色块在昵称下方的卡片里，不是行的直接子控件。
    const DuiControl* fileRow = FindById(content.get(), (UINT)(kIdHistoryRowBase + 2));
    REQUIRE(fileRow != nullptr);
    const HistoryRowThumb* fileThumb = FindFirstInSubtree<HistoryRowThumb>(fileRow);
    REQUIRE(fileThumb != nullptr);
    CHECK_FALSE(fileThumb->IsClickable());
}

// =====================================================================
// HIM1-HIM7  图片预览与文件卡片显示在昵称下方
//
// 2026-09-30 之前，图片行与文件行在头像右侧多插一列 36×36 的缩略图 / 类型色块，
// 昵称与正文被整体推向右边，和文字行对不齐，图片也小得看不清（snapshots/204413.png、
// snapshots/204554.png）。改为：图片按原图宽高等比显示在昵称下方（最大 200×150），
// 文件在昵称下方显示一张"类型色块 + 文件名 + 大小"的卡片，三种行的昵称左边对齐。
// =====================================================================

namespace {

// 以下期望值与 HistoryWindowLayout.cpp 中的同名常量一致（那几个常量是该文件内部的，
// 测试无法直接引用）；产品代码调整尺寸时须同步修改这里。
const int kExpectNameLineH = 20;   // 昵称与时间那一行的高（像素），对应 kNameLineH
const int kExpectMidGap    = 2;    // 内容列里各块之间的间距（像素），对应 BuildRow 里 mid 的 SetGap
const int kExpectRowPadV   = 8;    // 行的上下内边距（像素），对应 kRowPadV

// 测试用的发送者昵称。三种行用同一个名字，才能按文字找到各行的昵称标签。
LPCTSTR const kHimSender = _T("苏沐辰");

// 在一个控件及其全部后代里找文字等于 text 的 DuiLabel。找不到返回 nullptr。
const DuiLabel* FindLabelByText(const DuiControl* node, LPCTSTR text)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    const DuiLabel* label = dynamic_cast<const DuiLabel*>(node);
    if (label != nullptr && label->GetText() == CString(text))
    {
        return label;
    }
    for (const std::unique_ptr<DuiControl>& child : node->Children())
    {
        const DuiLabel* hit = FindLabelByText(child.get(), text);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// 造一条图片行：原图 w×h，缩略图处于 state 状态；path 为空表示本地还没有原图。
HistoryRowView MakeImageRow(long long msgid, int w, int h, int state, LPCTSTR path)
{
    HistoryRowView row = MakeContentRow(_T("[图片]"), false, msgid);
    row.m_senderName     = kHimSender;
    row.m_hasImageThumb  = true;
    row.m_imageWidth     = w;
    row.m_imageHeight    = h;
    row.m_thumbState     = state;
    row.m_thumbImagePath = path;
    row.m_thumbClickable = (state == balloonchatwnd::kHistoryThumbNeedDownload);
    return row;
}

// 造一条文件行：季度报告.xlsx，182.8 KB。
HistoryRowView MakeFileRow(long long msgid)
{
    HistoryRowView row = MakeContentRow(_T("季度报告.xlsx · 182.8 KB"), false, msgid);
    row.m_senderName   = kHimSender;
    row.m_thumbFileExt = _T("xlsx");
    row.m_fileName     = _T("季度报告.xlsx");
    row.m_fileSizeText = _T("182.8 KB");
    return row;
}

// 建一个只有给定各行的列表并按测试宽度布局好。
std::unique_ptr<DuiControl> BuildLaidOutList(const std::vector<HistoryRowView>& rows)
{
    std::unique_ptr<DuiControl> content = BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    if (content != nullptr)
    {
        content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });
    }
    return content;
}

} // anonymous namespace

TEST_CASE("HIM1 预览图按原图等比缩放到 200x150 以内，小图不放大，缺宽高取默认尺寸")
{
    int w = 0;
    int h = 0;

    //宽图：宽先顶到 200，高按比例 870*200/1408=123.6，四舍五入为 124
    ComputeHistoryImageSize(1408, 870, w, h);
    CHECK(w == 200);
    CHECK(h == 124);

    //高图：高顶到 150，宽按比例 400*150/1200=50
    ComputeHistoryImageSize(400, 1200, w, h);
    CHECK(w == 50);
    CHECK(h == 150);

    //小图不放大
    ComputeHistoryImageSize(60, 40, w, h);
    CHECK(w == 60);
    CHECK(h == 40);

    //宽高缺失（老消息）：取默认尺寸
    ComputeHistoryImageSize(0, 0, w, h);
    CHECK(w == kHistoryImageDefaultW);
    CHECK(h == kHistoryImageDefaultH);
    ComputeHistoryImageSize(640, 0, w, h);
    CHECK(w == kHistoryImageDefaultW);
    CHECK(h == kHistoryImageDefaultH);
}

TEST_CASE("HIM2 解析图片消息内容时取得到原图宽高")
{
    //本地库落盘的格式：image 子对象里宽高存为 w / h 两个整数（见 V2MsgQuery.cpp）
    const HistoryContentBrief brief = ParseContentBrief(::message_type_image,
        "{\"image\":{\"fileid\":\"f1\",\"md5\":\"abc123\",\"w\":1408,\"h\":870,\"size\":\"2048\"}}");
    CHECK(brief.m_imageWidth == 1408);
    CHECK(brief.m_imageHeight == 870);

    //缺字段时为 0
    const HistoryContentBrief missing = ParseContentBrief(::message_type_image,
        "{\"image\":{\"fileid\":\"f1\",\"md5\":\"abc123\"}}");
    CHECK(missing.m_imageWidth == 0);
    CHECK(missing.m_imageHeight == 0);
}

TEST_CASE("HIM3 图片预览在内容列里、位于昵称下方并与昵称左对齐，行里没有多出的一列")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeImageRow(9801, 1408, 870, balloonchatwnd::kHistoryThumbImage,
                                _T("C:\\cache\\img")));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    REQUIRE(row != nullptr);
    CHECK(FindDirectChild<HistoryRowThumb>(row) == nullptr);

    const DuiControl* thumb = FindById(content.get(), (UINT)(kIdHistoryThumbBase + 0));
    const DuiLabel* name = FindLabelByText(row, kHimSender);
    REQUIRE(thumb != nullptr);
    REQUIRE(name != nullptr);

    const RECT rcThumb = thumb->GetRect();
    const RECT rcName  = name->GetRect();
    INFO("预览图=(" << rcThumb.left << "," << rcThumb.top << ") " << RectWidth(rcThumb) << "x"
         << RectHeight(rcThumb) << "，昵称=(" << rcName.left << "," << rcName.top << ")-"
         << rcName.bottom);
    CHECK(RectWidth(rcThumb) == 200);
    CHECK(RectHeight(rcThumb) == 124);
    CHECK(rcThumb.left == rcName.left);
    CHECK(rcThumb.top >= rcName.bottom);
}

TEST_CASE("HIM4 图片行的行高等于内边距 + 昵称行 + 间距 + 预览图高度")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeImageRow(9811, 1408, 870, balloonchatwnd::kHistoryThumbImage,
                                _T("C:\\cache\\img")));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const int expect = kExpectRowPadV * 2 + kExpectNameLineH + kExpectMidGap + 124;
    CHECK(RowHeightAt(content.get(), 0) == expect);
}

TEST_CASE("HIM5 文件卡片在内容列里、位于昵称下方，色块与昵称左对齐，文件名与大小都显示")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeFileRow(9821));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    REQUIRE(row != nullptr);
    const HistoryRowThumb* block = FindFirstInSubtree<HistoryRowThumb>(row);
    const DuiLabel* name = FindLabelByText(row, kHimSender);
    REQUIRE(block != nullptr);
    REQUIRE(name != nullptr);

    const RECT rcBlock = block->GetRect();
    const RECT rcName  = name->GetRect();
    CHECK(rcBlock.left == rcName.left);
    CHECK(rcBlock.top >= rcName.bottom);

    //文件名由正文控件显示（保留搜索高亮与拖选），大小是一枚标签
    const HistoryContentView* fileName = FindFirstInSubtree<HistoryContentView>(row);
    REQUIRE(fileName != nullptr);
    CHECK(fileName->GetFlatText() == CString(_T("季度报告.xlsx")));
    CHECK(FindLabelByText(row, _T("182.8 KB")) != nullptr);
}

TEST_CASE("HIM6 文字行、图片行、文件行的昵称左边横坐标相同")
{
    std::vector<HistoryRowView> rows;
    HistoryRowView text = MakeContentRow(_T("一句话"), false, 9831);
    text.m_senderName = kHimSender;
    rows.push_back(text);
    rows.push_back(MakeImageRow(9832, 1408, 870, balloonchatwnd::kHistoryThumbImage,
                                _T("C:\\cache\\img")));
    rows.push_back(MakeFileRow(9833));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    int lefts[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i)
    {
        const DuiControl* row = FindById(content.get(), (UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != nullptr);
        const DuiLabel* name = FindLabelByText(row, kHimSender);
        REQUIRE(name != nullptr);
        lefts[i] = name->GetRect().left;
    }
    INFO("文字行=" << lefts[0] << " 图片行=" << lefts[1] << " 文件行=" << lefts[2]);
    CHECK(lefts[1] == lefts[0]);
    CHECK(lefts[2] == lefts[0]);
}

TEST_CASE("HRI1 图片行里的预览图、昵称都能找回所在的行")
{
    //双击图片行的任何位置都应当打开看图窗：窗口收到的是最深层子控件发来的双击，
    //要靠本函数找回是哪一行（2026-09-30：图片挪到昵称下方后，预览图的父控件不再是行）。
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeImageRow(9851, 1408, 870, balloonchatwnd::kHistoryThumbImage,
                                _T("C:\\cache\\img")));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const DuiControl* thumb = FindById(content.get(), (UINT)(kIdHistoryThumbBase + 0));
    const DuiControl* row   = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    REQUIRE(thumb != nullptr);
    REQUIRE(row != nullptr);
    const DuiLabel* name = FindLabelByText(row, kHimSender);
    REQUIRE(name != nullptr);

    CHECK(HistoryRowIndexOfControl(thumb) == 0);
    CHECK(HistoryRowIndexOfControl(name) == 0);
    CHECK(HistoryRowIndexOfControl(row) == 0);
}

TEST_CASE("HRI2 多行时各行的子控件找回各自的行，行外的控件返回 -1")
{
    std::vector<HistoryRowView> rows;
    HistoryRowView text = MakeContentRow(_T("一句话"), false, 9861);
    text.m_senderName = kHimSender;
    text.m_groupLabel = _T("今天");   //行前插一条日期分组头：它不属于任何一行
    rows.push_back(text);
    rows.push_back(MakeFileRow(9862));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const DuiControl* row0 = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    const DuiControl* row1 = FindById(content.get(), (UINT)(kIdHistoryRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);

    const DuiLabel* name0 = FindLabelByText(row0, kHimSender);
    const HistoryRowThumb* block1 = FindFirstInSubtree<HistoryRowThumb>(row1);
    const DuiLabel* groupHeader = FindLabelByText(content.get(), _T("今天"));
    REQUIRE(name0 != nullptr);
    REQUIRE(block1 != nullptr);
    REQUIRE(groupHeader != nullptr);

    CHECK(HistoryRowIndexOfControl(name0) == 0);
    CHECK(HistoryRowIndexOfControl(block1) == 1);
    CHECK(HistoryRowIndexOfControl(groupHeader) == -1);
    CHECK(HistoryRowIndexOfControl(content.get()) == -1);
    CHECK(HistoryRowIndexOfControl(nullptr) == -1);
}

TEST_CASE("HIM7 需要下载的占位图与已下载的图片一样大")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeImageRow(9841, 1408, 870, balloonchatwnd::kHistoryThumbNeedDownload, _T("")));
    rows.push_back(MakeImageRow(9842, 1408, 870, balloonchatwnd::kHistoryThumbImage,
                                _T("C:\\cache\\img")));
    std::unique_ptr<DuiControl> content = BuildLaidOutList(rows);
    REQUIRE(content != nullptr);

    const DuiControl* placeholder = FindById(content.get(), (UINT)(kIdHistoryThumbBase + 0));
    const DuiControl* loaded      = FindById(content.get(), (UINT)(kIdHistoryThumbBase + 1));
    REQUIRE(placeholder != nullptr);
    REQUIRE(loaded != nullptr);
    CHECK(RectWidth(placeholder->GetRect()) == RectWidth(loaded->GetRect()));
    CHECK(RectHeight(placeholder->GetRect()) == RectHeight(loaded->GetRect()));
    CHECK(RowHeightAt(content.get(), 0) == RowHeightAt(content.get(), 1));
}

// =====================================================================
// V2L20  底部一行（信息 / 分页栏 / 跳至 / 导出）各控件互不重叠
//
// 分页栏原先按权重分宽度，里面的按钮却是固定宽度。页码格多时（首页 < 1 … 12 13 14 … 25
// > 末页）按钮总宽超出分到的宽度，「末页」画到了右侧的“跳至”上（2026-10-01 清理聊天
// 记录联调时发现，见 snapshots/chatclean_it_history_pager_overlap.png）。
// 分别在默认客户区宽度与最小窗口宽度下核对。
// =====================================================================

namespace {

// 最小窗口宽度（BalloonHistoryWindow.cpp 的 kHisMinWidth = 640）扣掉左右窗框后的客户区宽度，
// 取略小的值以覆盖最坏情况（像素）
const int kTestNarrowBodyWidth = 620;

// 页码格最多时的分页状态：共 25 页、停在中间第 13 页，显示 1 … 12 13 14 … 25
const int kTestManyPages   = 25;
const int kTestMiddlePage  = 13;

// 按给定宽度构建并布局整窗，逐项核对底部一行与分页栏内部互不重叠、都不越出所在容器。
void CheckFooterNoOverlap(int bodyWidth, int pageCount, int currentPage)
{
    HistoryViewData data;
    FillMockHistory(data);
    data.m_pageCount   = pageCount;
    data.m_currentPage = currentPage;

    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, bodyWidth, kTestBodyHeight });

    const DuiControl* info = FindById(root.get(), kIdHistoryFooterInfo);
    REQUIRE(info != nullptr);
    const DuiControl* footer = info->GetParent();
    REQUIRE(footer != nullptr);
    const RECT rcFooter = footer->GetRect();

    // 底部一行：可见子控件自左至右依次排开，前一个的右缘不超过后一个的左缘，且都在行内
    const DuiControl* prev = nullptr;
    for (const std::unique_ptr<DuiControl>& child : footer->Children())
    {
        if (!child->IsVisible())
        {
            continue;
        }
        const RECT rc = child->GetRect();
        INFO("width " << bodyWidth << ", footer child id " << child->GetCtrlId()
             << " rect (" << rc.left << ", " << rc.right << "), footer (" << rcFooter.left
             << ", " << rcFooter.right << ")");
        CHECK(rc.left >= rcFooter.left);
        CHECK(rc.right <= rcFooter.right);
        if (prev != nullptr)
        {
            INFO("previous child id " << prev->GetCtrlId() << " right " << prev->GetRect().right);
            CHECK(prev->GetRect().right <= rc.left);
        }
        prev = child.get();
    }

    // 分页栏内部：可见按钮都落在分页栏自己的矩形里
    const DuiControl* pager = FindById(root.get(), kIdHistoryPager);
    REQUIRE(pager != nullptr);
    const RECT rcPager = pager->GetRect();
    for (const std::unique_ptr<DuiControl>& cell : pager->Children())
    {
        if (!cell->IsVisible())
        {
            continue;
        }
        const RECT rc = cell->GetRect();
        INFO("width " << bodyWidth << ", pager cell id " << cell->GetCtrlId() << " rect ("
             << rc.left << ", " << rc.right << "), pager (" << rcPager.left << ", "
             << rcPager.right << ")");
        CHECK(rc.left >= rcPager.left);
        CHECK(rc.right <= rcPager.right);
    }
}

} // anonymous namespace

TEST_CASE("V2L20 底部一行各控件互不重叠：页码格最多时，默认宽度与最小宽度都不重叠")
{
    CheckFooterNoOverlap(kTestBodyWidth, kTestManyPages, kTestMiddlePage);
    CheckFooterNoOverlap(kTestNarrowBodyWidth, kTestManyPages, kTestMiddlePage);
}

namespace {

// 三级隐藏的分界（页码格最多、共 25 页时实测，2026-10-03 底部加入「清空聊天记录」之后）：宽度低于 970
// 隐藏左侧信息，低于 840 再隐藏首页 / 末页，低于 736 再隐藏「跳至 _ 页」。下面三个宽度各落在一段里，
// 用来核对隐藏的先后顺序
// 只装不下左侧信息的宽度：首页 / 末页与跳页框都在
const int kTestMediumBodyWidth = 900;
// 还装不下首页 / 末页、但装得下跳页框的宽度
const int kTestTightBodyWidth = 760;
// 什么都不用隐藏的宽度（高于 970）
const int kTestWideBodyWidth = 1000;

// 页数少的情形：3 页，页码格全部平铺
const int kTestFewPages = 3;

// 按给定宽度与分页状态构建并布局整窗。
std::unique_ptr<DuiControl> BuildFooterAt(int bodyWidth, int pageCount, int currentPage)
{
    HistoryViewData data;
    FillMockHistory(data);
    data.m_pageCount   = pageCount;
    data.m_currentPage = currentPage;
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, bodyWidth, kTestBodyHeight });
    return root;
}

// 某个控件是否存在且可见。
bool IsShown(const DuiControl* root, UINT id)
{
    const DuiControl* ctrl = FindById(root, id);
    return ctrl != nullptr && ctrl->IsVisible();
}

// 断言当前分页状态下应有的页码格与前后翻页按钮都显示着。
void CheckPageCellsShown(const DuiControl* root, int pageCount, int currentPage)
{
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(currentPage, pageCount);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i].m_isEllipsis)
        {
            continue;
        }
        INFO("page cell " << cells[i].m_page);
        CHECK(IsShown(root, (UINT)(kIdHistoryPageNumBase + (cells[i].m_page - 1))));
    }
    CHECK(IsShown(root, kIdHistoryPagePrev));
    CHECK(IsShown(root, kIdHistoryPageNext));
}

} // anonymous namespace

TEST_CASE("V2L21 默认宽度下页码格最多时只隐藏左侧信息，首页末页、跳页框与两个按钮都在")
{
    //2026-10-03 底部加入「清空聊天记录」后与用户确认：默认宽度、页数多时左侧信息让出位置
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestBodyWidth, kTestManyPages, kTestMiddlePage);
    CHECK_FALSE(IsShown(root.get(), kIdHistoryFooterInfo));
    CHECK(IsShown(root.get(), kIdHistoryPageFirst));
    CHECK(IsShown(root.get(), kIdHistoryPageLast));
    CHECK(IsShown(root.get(), kIdHistoryPageJump));
    CHECK(IsShown(root.get(), kIdHistoryClearConversation));
    CHECK(IsShown(root.get(), kIdHistoryExport));
    CheckPageCellsShown(root.get(), kTestManyPages, kTestMiddlePage);

    //页数少时默认宽度下什么都不隐藏
    std::unique_ptr<DuiControl> few = BuildFooterAt(kTestBodyWidth, kTestFewPages, 1);
    CHECK(IsShown(few.get(), kIdHistoryFooterInfo));
    CHECK(IsShown(few.get(), kIdHistoryPageFirst));
    CHECK(IsShown(few.get(), kIdHistoryPageJump));
}

TEST_CASE("V2L22 窗口变窄时依次隐藏左侧信息、首页与末页、跳页框，页码格与两个按钮始终都在")
{
    // 只隐藏左侧信息
    {
        std::unique_ptr<DuiControl> root = BuildFooterAt(kTestMediumBodyWidth, kTestManyPages, kTestMiddlePage);
        CHECK_FALSE(IsShown(root.get(), kIdHistoryFooterInfo));
        CHECK(IsShown(root.get(), kIdHistoryPageFirst));
        CHECK(IsShown(root.get(), kIdHistoryPageLast));
        CHECK(IsShown(root.get(), kIdHistoryPageJump));
        CheckPageCellsShown(root.get(), kTestManyPages, kTestMiddlePage);
    }
    // 再隐藏首页与末页，跳页框还在
    {
        std::unique_ptr<DuiControl> root = BuildFooterAt(kTestTightBodyWidth, kTestManyPages, kTestMiddlePage);
        CHECK_FALSE(IsShown(root.get(), kIdHistoryFooterInfo));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageFirst));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageLast));
        CHECK(IsShown(root.get(), kIdHistoryPageJump));
        CHECK(IsShown(root.get(), kIdHistoryPageJumpLabel));
        CHECK(IsShown(root.get(), kIdHistoryPageJumpUnit));
        CheckPageCellsShown(root.get(), kTestManyPages, kTestMiddlePage);
    }
    // 最小宽度：「跳至 _ 页」整组也隐藏
    {
        std::unique_ptr<DuiControl> root = BuildFooterAt(kTestNarrowBodyWidth, kTestManyPages, kTestMiddlePage);
        CHECK_FALSE(IsShown(root.get(), kIdHistoryFooterInfo));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageFirst));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageLast));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageJump));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageJumpLabel));
        CHECK_FALSE(IsShown(root.get(), kIdHistoryPageJumpUnit));
        CHECK(IsShown(root.get(), kIdHistoryClearConversation));
        CHECK(IsShown(root.get(), kIdHistoryExport));
        CheckPageCellsShown(root.get(), kTestManyPages, kTestMiddlePage);
    }
}

TEST_CASE("V2L23 页数少时最小宽度下也放得下：页码格、跳页框与两个按钮都在")
{
    //页数少时首页 / 末页与页码格重复（第 1 页与最后一页本来就显示着），最小宽度下让出位置
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestNarrowBodyWidth, kTestFewPages, 1);
    CHECK_FALSE(IsShown(root.get(), kIdHistoryPageFirst));
    CHECK_FALSE(IsShown(root.get(), kIdHistoryPageLast));
    CHECK(IsShown(root.get(), kIdHistoryPageJump));
    CHECK(IsShown(root.get(), kIdHistoryClearConversation));
    CHECK(IsShown(root.get(), kIdHistoryExport));
    CheckPageCellsShown(root.get(), kTestFewPages, 1);
    CheckFooterNoOverlap(kTestNarrowBodyWidth, kTestFewPages, 1);
}

TEST_CASE("V2L24 窄宽度下隐藏过的控件，在宽度恢复后重新排版时全部恢复显示且位置正确")
{
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestNarrowBodyWidth, kTestManyPages, kTestMiddlePage);
    REQUIRE_FALSE(IsShown(root.get(), kIdHistoryPageFirst));

    // 同一棵控件树按足够宽的宽度重新排版（默认宽度下页数多时左侧信息本来就隐藏）
    root->ForceLayout(RECT{ 0, 0, kTestWideBodyWidth, kTestBodyHeight });
    CHECK(IsShown(root.get(), kIdHistoryFooterInfo));
    CHECK(IsShown(root.get(), kIdHistoryPageFirst));
    CHECK(IsShown(root.get(), kIdHistoryPageLast));

    // 与直接按同一宽度构建的结果逐个比对位置
    std::unique_ptr<DuiControl> direct = BuildFooterAt(kTestWideBodyWidth, kTestManyPages, kTestMiddlePage);
    const UINT ids[] = { kIdHistoryFooterInfo, kIdHistoryPageFirst, kIdHistoryPagePrev,
                         kIdHistoryPageNext, kIdHistoryPageLast, kIdHistoryPageJump,
                         kIdHistoryClearConversation, kIdHistoryExport };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        const DuiControl* a = FindById(root.get(), ids[i]);
        const DuiControl* b = FindById(direct.get(), ids[i]);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        const RECT ra = a->GetRect();
        const RECT rb = b->GetRect();
        INFO("id " << ids[i] << ": relayout (" << ra.left << ", " << ra.right << ") direct ("
             << rb.left << ", " << rb.right << ")");
        CHECK(ra.left == rb.left);
        CHECK(ra.right == rb.right);
    }
}

// =====================================================================
// 效果图（默认不运行）：消息记录窗底部一行在三种宽度下的样子
//
// 用与界面相同的绘制代码，把页码格最多（共 25 页、停在第 13 页）时的底部一行，分别按默认
// 宽度、中间宽度、最小宽度排版后画到一张 PNG 上，供人工核对底部一行不重叠的效果。标签带
// [.]，默认不运行；输出路径取自环境变量 FLAMINGO_SNAPSHOT_PATH，没设时直接跳过。用法：
//     set FLAMINGO_SNAPSHOT_PATH=E:\flamingo\snapshots\history_footer.png
//     FlamingoUnit.exe "[snapshot]"
// =====================================================================

namespace {

// 效果图里每一条之间的间距（像素），以及整张图的底色。
const int kSnapFooterGap = 12;
const COLORREF kSnapBgColor = RGB(0xE8, 0xE8, 0xE8);

// 在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID。
bool FindPngEncoderClsid(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
    {
        return false;
    }
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

} // anonymous namespace

TEST_CASE("V2LSNAP 底部一行在默认、中间、最小三种宽度下的效果图", "[.][snapshot][history]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }

    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);

    const int widths[] = { kTestBodyWidth, kTestMediumBodyWidth, kTestNarrowBodyWidth };
    const int widthCount = (int)(sizeof(widths) / sizeof(widths[0]));

    // 先各自排版，量出底部一行的高度
    std::vector<std::unique_ptr<DuiControl> > roots;
    int footerH = 0;
    for (int i = 0; i < widthCount; ++i)
    {
        roots.push_back(BuildFooterAt(widths[i], kTestManyPages, kTestMiddlePage));
        const DuiControl* info = FindById(roots.back().get(), kIdHistoryFooterInfo);
        REQUIRE(info != nullptr);
        REQUIRE(info->GetParent() != nullptr);
        const RECT rc = info->GetParent()->GetRect();
        footerH = rc.bottom - rc.top;
    }
    REQUIRE(footerH > 0);

    // 整张图：三条自上而下排列，宽度取最宽的那条
    const int canvasW = kTestBodyWidth;
    const int canvasH = widthCount * (footerH + kSnapFooterGap) + kSnapFooterGap;
    HDC screenDC = ::GetDC(NULL);
    HDC canvasDC = ::CreateCompatibleDC(screenDC);
    HBITMAP canvasBmp = ::CreateCompatibleBitmap(screenDC, canvasW, canvasH);
    HDC bodyDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bodyBmp = ::CreateCompatibleBitmap(screenDC, kTestBodyWidth, kTestBodyHeight);
    ::ReleaseDC(NULL, screenDC);
    HGDIOBJ oldCanvas = ::SelectObject(canvasDC, canvasBmp);
    HGDIOBJ oldBody = ::SelectObject(bodyDC, bodyBmp);

    RECT all = { 0, 0, canvasW, canvasH };
    HBRUSH bg = ::CreateSolidBrush(kSnapBgColor);
    ::FillRect(canvasDC, &all, bg);
    ::DeleteObject(bg);

    for (int i = 0; i < widthCount; ++i)
    {
        // 整窗按该宽度画到 bodyDC，再把底部一行拷到效果图上
        RECT body = { 0, 0, widths[i], kTestBodyHeight };
        roots[i]->OnPaint(bodyDC, body);
        const RECT rcFooter = FindById(roots[i].get(), kIdHistoryFooterInfo)->GetParent()->GetRect();
        const int y = kSnapFooterGap + i * (footerH + kSnapFooterGap);
        ::BitBlt(canvasDC, 0, y, rcFooter.right - rcFooter.left, footerH,
                 bodyDC, rcFooter.left, rcFooter.top, SRCCOPY);
    }

    ::SelectObject(bodyDC, oldBody);
    ::SelectObject(canvasDC, oldCanvas);
    {
        Gdiplus::Bitmap png(canvasBmp, NULL);
        CLSID pngClsid;
        REQUIRE(FindPngEncoderClsid(pngClsid));
        CHECK(png.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
    }
    ::DeleteObject(bodyBmp);
    ::DeleteDC(bodyDC);
    ::DeleteObject(canvasBmp);
    ::DeleteDC(canvasDC);
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("V2L25 按窗口换页的流程重建分页栏并重排整行后，新的页码按钮都排进了分页栏", "[history]")
{
    // 2026-10-01 在真实客户端核对分页栏重叠的修复时发现：窗口换页时先清空分页栏、重填按钮，再重排
    // 整行。分页栏的矩形与上次相同时，重排整行不会把新按钮排进去（SetRect 在矩形未变时
    // 直接返回），按钮矩形全是空的，表现为整排分页按钮消失。本用例按窗口的同一流程操作。
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestBodyWidth, kTestManyPages, kTestMiddlePage);
    DuiControl* pager = root->FindCtrlById(kIdHistoryPager);
    REQUIRE(pager != nullptr);
    DuiControl* footer = pager->GetParent();
    REQUIRE(footer != nullptr);

    // 与 BalloonHistoryWindow 的刷新流程一致：清空、重填、重排整行（换到第 14 页，格子数不变）
    std::vector<DuiControl*> kids;
    for (const std::unique_ptr<DuiControl>& child : pager->Children())
    {
        kids.push_back(child.get());
    }
    for (size_t i = 0; i < kids.size(); ++i)
    {
        pager->RemoveChild(kids[i]);
    }
    const int nextPage = kTestMiddlePage + 1;
    PopulatePagerButtons(pager, nextPage, kTestManyPages);
    footer->ForceLayout(footer->GetRect());

    const RECT rcPager = pager->GetRect();
    REQUIRE(pager->Children().size() > 0);
    for (const std::unique_ptr<DuiControl>& cell : pager->Children())
    {
        if (!cell->IsVisible())
        {
            continue;
        }
        const RECT rc = cell->GetRect();
        INFO("pager cell id " << cell->GetCtrlId() << " rect (" << rc.left << ", " << rc.top << ", "
             << rc.right << ", " << rc.bottom << "), pager (" << rcPager.left << ", " << rcPager.right << ")");
        CHECK(rc.right > rc.left);
        CHECK(rc.bottom > rc.top);
        CHECK(rc.left >= rcPager.left);
        CHECK(rc.right <= rcPager.right);
    }
    CheckPageCellsShown(root.get(), kTestManyPages, nextPage);
}

// =====================================================================
// V2L26 / V2L27  列表滚动条与聊天窗同一风格：悬浮式、自动隐藏的灰色细条
//
// 列表原先用 DuiScrollView 自带的滚动条：宽 10 像素、带浅灰轨道与蓝灰方形滑块，且占用
// 列表宽度。改用主面板与公告窗已在用的 MainPanelScrollView：内容用满视口宽度，滑块是悬浮在
// 内容上方的 5 像素灰色圆角细条，滚动时淡入、停下后淡出（2026-10-04 起即 balloonui 统一的滚动条）。
// =====================================================================

namespace {

// 让列表内容一定超出视口的行数（每行至少几十像素，视口高几百像素）
const int kTestOverflowRows = 20;

// 按窗口刷新列表的流程，用实际视口宽度装入足够多的长正文行，使内容超出视口。
//   root：已按默认宽度排版的整窗；返回列表滚动视口。
DuiScrollView* FillOverflowRows(DuiControl* root)
{
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(root->FindCtrlById(kIdHistoryScroll));
    REQUIRE(scroll != nullptr);
    std::vector<HistoryRowView> rows;
    for (int i = 0; i < kTestOverflowRows; ++i)
    {
        rows.push_back(MakeContentRow((i % 2 == 0) ? kStackTraceText : kLongText, false, 9700 + i));
    }
    const int listWidth = RectWidth(scroll->GetRect());
    scroll->SetContent(BuildListContent(rows, _T(""), _T(""), listWidth));
    scroll->ForceLayout(scroll->GetRect());
    return scroll;
}

} // anonymous namespace

TEST_CASE("V2L26 列表滚动视口是悬浮式自动隐藏的 MainPanelScrollView，内容用满视口宽度")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });

    balloonmain::MainPanelScrollView* overlay =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdHistoryScroll));
    REQUIRE(overlay != nullptr);
    REQUIRE(overlay->GetScrollBar() != nullptr);
    CHECK(overlay->GetScrollBar()->IsAutoHide());
    // 命中带保持默认宽度：置 0 就完全没有滚动条（2026-10-04 界面核对时发现过这一遗漏）
    CHECK(overlay->GetScrollBarWidth() == DuiScrollBar::kOverlayBandPx);

    // 内容超出视口时，列表内容仍然用满视口宽度，不为滚动条让出一列；滚动条出现
    DuiScrollView* scroll = FillOverflowRows(root.get());
    REQUIRE(scroll->GetContent() != nullptr);
    CHECK(scroll->GetScrollBar()->IsVisible());
    const RECT rcView = scroll->GetRect();
    const RECT rcContent = scroll->GetContent()->GetRect();
    INFO("viewport (" << rcView.left << ", " << rcView.right << "), content (" << rcContent.left
         << ", " << rcContent.right << "), content height " << RectHeight(rcContent)
         << ", viewport height " << RectHeight(rcView));
    REQUIRE(RectHeight(rcContent) > RectHeight(rcView));
    CHECK(rcContent.left == rcView.left);
    CHECK(rcContent.right == rcView.right);
}

TEST_CASE("V2L27 行高按正文实际宽度排出的高度分配，测量宽度不再为滚动条多扣一列")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });
    DuiScrollView* scroll = FillOverflowRows(root.get());

    // 测量宽度与实际宽度一致时，分到的高度恰好等于按实际宽度排出的高度；测量时多扣了宽度，
    // 长正文会多折出几行，分到的高度就比实际需要的高，行下方空出一截
    for (int i = 0; i < kTestOverflowRows; ++i)
    {
        const DuiControl* row = FindById(scroll, (UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != nullptr);
        const HistoryContentView* view = FindContentView(row);
        REQUIRE(view != nullptr);
        const RECT rcView = view->GetRect();
        INFO("row " << i << ": content width " << RectWidth(rcView) << ", height " << RectHeight(rcView)
             << ", needed " << view->MeasureHeight(RectWidth(rcView)));
        CHECK(RectHeight(rcView) == view->MeasureHeight(RectWidth(rcView)));
    }
}

TEST_CASE("HP1 一页 30 条时，每行的行容器与定位、Pin 按钮都能找到，且 id 互不重复、不越段")
{
    std::vector<HistoryRowView> rows;
    for (int i = 0; i < kHistoryPageSize; ++i)
    {
        rows.push_back(MakeContentRow(_T("一条普通消息"), false, 9800 + i));
    }
    std::unique_ptr<DuiControl> list = BuildListContent(rows, _T(""), _T(""), kTestBodyWidth);
    REQUIRE(list != nullptr);

    for (int i = 0; i < kHistoryPageSize; ++i)
    {
        INFO("row " << i);
        CHECK(FindById(list.get(), (UINT)(kIdHistoryRowBase + i)) != nullptr);
        CHECK(FindById(list.get(), (UINT)(kIdHistoryLocateBase + i)) != nullptr);
        CHECK(FindById(list.get(), (UINT)(kIdHistoryPinBase + i)) != nullptr);
    }
    // 最后一行的 id 仍落在各自的段内（段容量见 HistoryWindowLayout.h）
    CHECK((UINT)(kHistoryPageSize - 1) < kIdHistoryRowCapacity);
    CHECK((UINT)(kHistoryPageSize - 1) < kIdHistoryLocateCapacity);
    CHECK((UINT)(kHistoryPageSize - 1) < kIdHistoryPinCapacity);

    // 整个列表里非 0 的 ctrlId 互不重复
    std::vector<UINT> ids;
    CollectIds(list.get(), ids);
    std::vector<UINT> sorted(ids);
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

TEST_CASE("V2L28 搜索行：发送人下拉在搜索框右侧，默认宽度与最小宽度下都不重叠、不越出窗口", "[historysender]")
{
    const int widths[] = { kTestBodyWidth, kTestNarrowBodyWidth };
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
    {
        INFO("body width " << widths[i]);
        HistoryViewData data;
        FillMockHistory(data);
        std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
        REQUIRE(root != nullptr);
        root->ForceLayout(RECT{ 0, 0, widths[i], kTestBodyHeight });

        const DuiControl* search = FindById(root.get(), kIdHistorySearch);
        const DuiControl* sender = FindById(root.get(), kIdHistorySenderCombo);
        REQUIRE(search != nullptr);
        REQUIRE(sender != nullptr);
        const RECT rs = search->GetRect();
        const RECT rd = sender->GetRect();
        CHECK(rd.left >= rs.right);           //在搜索框右侧，不重叠
        CHECK(rd.right <= widths[i]);         //不越出窗口
        CHECK(rd.right - rd.left > 0);
        //搜索框仍留有足够宽度：至少比发送人下拉宽
        CHECK(rs.right - rs.left > rd.right - rd.left);

        //「跳到日期」按钮已经去掉：搜索行只剩搜索框与发送人下拉，下拉贴着搜索行右缘内边距
        CHECK(rd.right <= widths[i]);
    }
}

TEST_CASE("HFW5 多选操作栏平时隐藏；显示后最小宽度下按钮互不重叠、不越出窗口", "[historymultiselect]")
{
    const int widths[] = { kTestBodyWidth, kTestNarrowBodyWidth };
    const UINT buttons[] = { kIdHistorySelectMerge, kIdHistorySelectEach,
                             kIdHistorySelectDelete, kIdHistorySelectCancel };
    for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w)
    {
        INFO("body width " << widths[w]);
        HistoryViewData data;
        FillMockHistory(data);
        std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
        REQUIRE(root != nullptr);
        DuiControl* bar = root->FindCtrlById(kIdHistorySelectBar);
        REQUIRE(bar != nullptr);
        CHECK_FALSE(bar->IsVisible());

        bar->SetVisible(true);
        root->ForceLayout(RECT{ 0, 0, widths[w], kTestBodyHeight });
        const DuiControl* info = FindById(root.get(), kIdHistorySelectInfo);
        REQUIRE(info != nullptr);
        int prevRight = info->GetRect().right;
        for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); ++b)
        {
            INFO("button " << buttons[b]);
            const DuiControl* btn = FindById(root.get(), buttons[b]);
            REQUIRE(btn != nullptr);
            const RECT rc = btn->GetRect();
            CHECK(rc.left >= prevRight);
            CHECK(rc.right - rc.left > 0);
            prevRight = rc.right;
        }
        CHECK(prevRight <= widths[w]);
        //「已选 N 条」至少放得下这几个字
        CHECK(info->GetRect().right - info->GetRect().left > 0);
    }
}

TEST_CASE("HFW6 多选模式下每行行首有复选框，按行下标编号，勾选状态取自行视图", "[historymultiselect]")
{
    std::vector<HistoryRowView> rows;
    const int kRows = 3;
    for (int i = 0; i < kRows; ++i)
    {
        HistoryRowView row = MakeContentRow(_T("一条普通消息"), false, 9100 + i);
        row.m_selectMode = true;
        row.m_selected = (i == 1);
        rows.push_back(row);
    }
    std::unique_ptr<DuiControl> list = BuildListContent(rows, _T(""), _T(""), kTestBodyWidth);
    REQUIRE(list != nullptr);
    for (int i = 0; i < kRows; ++i)
    {
        INFO("row " << i);
        DuiButton* box = dynamic_cast<DuiButton*>(list->FindCtrlById((UINT)(kIdHistorySelectBase + i)));
        REQUIRE(box != nullptr);
        CHECK(box->IsChecked() == (i == 1));
    }

    //不在多选模式：没有复选框
    rows[0].m_selectMode = false;
    std::unique_ptr<DuiControl> plain = BuildListContent(rows, _T(""), _T(""), kTestBodyWidth);
    CHECK(plain->FindCtrlById((UINT)kIdHistorySelectBase) == nullptr);
}

namespace {

//「图片」分类的测试数据：第一天 7 张、第二天 2 张，各组第一张带日期分组头
std::vector<HistoryRowView> MakeGridRows()
{
    std::vector<HistoryRowView> rows;
    const int kFirstDay = 7;
    const int kSecondDay = 2;
    for (int i = 0; i < kFirstDay + kSecondDay; ++i)
    {
        HistoryRowView row;
        row.m_senderName = _T("苏沐辰");
        row.m_timeText = _T("20:07");
        row.m_msgid = 9200 + i;
        row.m_hasImageThumb = true;
        if (i == 0)
        {
            row.m_groupLabel = _T("昨天");
        }
        else if (i == kFirstDay)
        {
            row.m_groupLabel = _T("2026-09-29");
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace

TEST_CASE("HGV2 图片网格：缩略图按记录下标编号且不重复，一行多张，新的一天另起一组；文件分类有「全部下载」", "[historygrid]")
{
    const std::vector<HistoryRowView> rows = MakeGridRows();
    std::unique_ptr<DuiControl> grid = BuildImageGridContent(rows, _T(""), _T(""), kTestBodyWidth);
    REQUIRE(grid != nullptr);
    grid->ForceLayout(RECT{ 0, 0, kTestBodyWidth, grid->GetDesiredSize().cy });

    std::vector<const DuiControl*> tiles;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        INFO("tile " << i);
        const DuiControl* tile = FindById(grid.get(), (UINT)(kIdHistoryGridBase + i));
        REQUIRE(tile != nullptr);
        CHECK(dynamic_cast<const HistoryRowThumb*>(tile) != nullptr);
        tiles.push_back(tile);
    }
    std::vector<UINT> ids;
    CollectIds(grid.get(), ids);
    std::vector<UINT> sorted(ids);
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    //第一行放了不止一张，且同一行的缩略图顶边对齐、从左到右排
    int firstLine = 0;
    for (size_t i = 0; i < 7; ++i)
    {
        if (tiles[i]->GetRect().top == tiles[0]->GetRect().top)
        {
            ++firstLine;
        }
    }
    CHECK(firstLine > 1);
    CHECK(tiles[1]->GetRect().left > tiles[0]->GetRect().right);
    //第二天另起一组：第一张在第一天最后一张之下、回到行首
    CHECK(tiles[7]->GetRect().top > tiles[6]->GetRect().bottom);
    CHECK(tiles[7]->GetRect().left == tiles[0]->GetRect().left);

    //「文件」分类：有待下载的文件才出现「全部下载」
    std::vector<HistoryRowView> fileRows;
    fileRows.push_back(MakeContentRow(_T("报告.docx"), false, 9300));
    std::unique_ptr<DuiControl> withBar = BuildListContent(fileRows, _T(""), _T(""), kTestBodyWidth, true, 3);
    CHECK(withBar->FindCtrlById(kIdHistoryBatchDownload) != nullptr);
    std::unique_ptr<DuiControl> noBar = BuildListContent(fileRows, _T(""), _T(""), kTestBodyWidth, true, 0);
    CHECK(noBar->FindCtrlById(kIdHistoryBatchDownload) == nullptr);
}

TEST_CASE("HGV4 图片网格效果图", "[.][snapshot][historygrid]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);

    const std::vector<HistoryRowView> rows = MakeGridRows();
    std::unique_ptr<DuiControl> grid = BuildImageGridContent(rows, _T(""), _T(""), kTestBodyWidth);
    const int h = grid->GetDesiredSize().cy;
    grid->ForceLayout(RECT{ 0, 0, kTestBodyWidth, h });

    HDC screenDC = ::GetDC(NULL);
    HDC dc = ::CreateCompatibleDC(screenDC);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kTestBodyWidth, h);
    ::ReleaseDC(NULL, screenDC);
    HGDIOBJ old = ::SelectObject(dc, bmp);
    RECT all = { 0, 0, kTestBodyWidth, h };
    HBRUSH bg = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(dc, &all, bg);
    ::DeleteObject(bg);
    grid->OnPaint(dc, all);
    ::SelectObject(dc, old);
    {
        Gdiplus::Bitmap png(bmp, NULL);
        CLSID pngClsid;
        REQUIRE(FindPngEncoderClsid(pngClsid));
        CHECK(png.Save(path, &pngClsid, NULL) == Gdiplus::Ok);
    }
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    Gdiplus::GdiplusShutdown(gdipToken);
}

// =====================================================================
// DR7 / DR8：「选日期」入口与日期区间标签（两个选日期的入口合并为一个，2026-10-01）
// =====================================================================

namespace {

//已经删除的控件编号：「跳到日期」按钮（522）与自定义时间区间那一行（560～566）
const UINT kRetiredDateJumpId = 522;
const UINT kRetiredCustomRowFirstId = 560;
const UINT kRetiredCustomRowLastId = 566;

//用例里的日期区间标签文字，取值无特殊含义
const wchar_t* const kSampleRangeLabel = L"9月1日 至 9月30日";

//控件矩形是否有面积。
bool HasArea(const DuiControl* c)
{
    if (c == nullptr)
    {
        return false;
    }
    const RECT rc = c->GetRect();
    return rc.right > rc.left && rc.bottom > rc.top;
}

} // anonymous namespace

TEST_CASE("DR7 搜索行没有「跳到日期」，时间下拉右侧有「选日期」，自定义区间那一行不再存在", "[history][daterange]")
{
    const int widths[] = { kTestBodyWidth, kTestNarrowBodyWidth };
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
    {
        INFO("body width " << widths[i]);
        HistoryViewData data;
        FillMockHistory(data);
        std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
        REQUIRE(root != nullptr);
        root->ForceLayout(RECT{ 0, 0, widths[i], kTestBodyHeight });

        //已经删除的控件一个都不在
        std::vector<UINT> ids;
        CollectIds(root.get(), ids);
        for (size_t k = 0; k < ids.size(); ++k)
        {
            CHECK(ids[k] != kRetiredDateJumpId);
            CHECK_FALSE((ids[k] >= kRetiredCustomRowFirstId && ids[k] <= kRetiredCustomRowLastId));
        }

        //「选日期」在时间下拉右侧，不重叠、不越出窗口
        const DuiControl* combo = FindById(root.get(), kIdHistoryTimeCombo);
        const DuiControl* pick = FindById(root.get(), kIdHistoryDatePick);
        REQUIRE(combo != nullptr);
        REQUIRE(pick != nullptr);
        CHECK(HasArea(pick));
        CHECK(pick->GetRect().left >= combo->GetRect().right);
        CHECK(pick->GetRect().right <= widths[i]);

        //没有日期筛选时：时间下拉可见，日期区间标签隐藏
        const DuiControl* chip = FindById(root.get(), kIdHistoryDateRangeChip);
        REQUIRE(chip != nullptr);
        CHECK(combo->IsVisible());
        CHECK_FALSE(chip->IsVisible());
    }
}

TEST_CASE("DR8 日期筛选生效时标签替下时间下拉，清除后恢复，隐藏后再显示的控件都重新排过版", "[history][daterange]")
{
    //建窗口时就带着日期筛选：标签可见、文字正确，时间下拉隐藏
    {
        HistoryViewData data;
        FillMockHistory(data);
        data.m_dateRangeLabel = kSampleRangeLabel;
        std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
        REQUIRE(root != nullptr);
        root->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });
        const DuiControl* chip = FindById(root.get(), kIdHistoryDateRangeChip);
        const DuiButton* label = dynamic_cast<const DuiButton*>(
            FindById(root.get(), kIdHistoryDateRangeLabel));
        REQUIRE(chip != nullptr);
        REQUIRE(label != nullptr);
        CHECK(chip->IsVisible());
        CHECK(HasArea(chip));
        CHECK(label->GetText() == CString(kSampleRangeLabel));
        CHECK_FALSE(FindById(root.get(), kIdHistoryTimeCombo)->IsVisible());
        CHECK(FindById(root.get(), kIdHistoryDateRangeClear) != nullptr);
    }

    //运行中切换：先没有筛选并排好版，再设上筛选、再清除
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, kTestBodyWidth, kTestBodyHeight });
    DuiControl* combo = const_cast<DuiControl*>(FindById(root.get(), kIdHistoryTimeCombo));
    DuiControl* chip = const_cast<DuiControl*>(FindById(root.get(), kIdHistoryDateRangeChip));
    const DuiControl* pick = FindById(root.get(), kIdHistoryDatePick);
    REQUIRE(combo != nullptr);
    REQUIRE(chip != nullptr);
    REQUIRE(pick != nullptr);

    ApplyHistoryDateRangeChip(root.get(), kSampleRangeLabel);
    CHECK(chip->IsVisible());
    CHECK_FALSE(combo->IsVisible());
    //标签此前一直隐藏，必须在显示后重新排过版：有面积、落在「选日期」左侧
    CHECK(HasArea(chip));
    CHECK(chip->GetRect().right <= pick->GetRect().left);

    ApplyHistoryDateRangeChip(root.get(), L"");
    CHECK(combo->IsVisible());
    CHECK_FALSE(chip->IsVisible());
    //时间下拉隐藏期间发生过重排，再显示时同样要重新排过版（开发约定文档 UI 约定第 5 条）
    CHECK(HasArea(combo));
    CHECK(combo->GetRect().right <= pick->GetRect().left);
}

TEST_CASE("DR9 「选日期」弹层：两组年月日与两个按钮，点哪个按钮就对应哪个动作", "[history][daterange]")
{
    std::unique_ptr<DuiControl> content = BuildHistoryDatePopupContent();
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kHistoryDatePopupW, kHistoryDatePopupH });

    //「从」「到」两组年月日微调框都在、都有面积，「到」那一行在「从」下面
    const UINT spins[] = { kIdDatePopupFromYear, kIdDatePopupFromMonth, kIdDatePopupFromDay,
                           kIdDatePopupToYear, kIdDatePopupToMonth, kIdDatePopupToDay };
    for (size_t i = 0; i < sizeof(spins) / sizeof(spins[0]); ++i)
    {
        INFO("spin id " << spins[i]);
        const DuiControl* c = FindById(content.get(), spins[i]);
        REQUIRE(c != nullptr);
        CHECK(HasArea(c));
        CHECK(c->GetRect().right <= kHistoryDatePopupW);
        CHECK(c->GetRect().bottom <= kHistoryDatePopupH);
    }
    CHECK(FindById(content.get(), kIdDatePopupToYear)->GetRect().top
          >= FindById(content.get(), kIdDatePopupFromYear)->GetRect().bottom);

    //两个按钮在最下面一行、文字正确，「只看这段时间」在最右
    const DuiButton* jump = dynamic_cast<const DuiButton*>(FindById(content.get(), kIdDatePopupJump));
    const DuiButton* filter = dynamic_cast<const DuiButton*>(FindById(content.get(), kIdDatePopupFilter));
    REQUIRE(jump != nullptr);
    REQUIRE(filter != nullptr);
    CHECK(jump->GetText() == CString(_T("跳到这一天")));
    CHECK(filter->GetText() == CString(_T("只看这段时间")));
    CHECK(HasArea(jump));
    CHECK(HasArea(filter));
    CHECK(jump->GetRect().top >= FindById(content.get(), kIdDatePopupToYear)->GetRect().bottom);
    CHECK(filter->GetRect().left >= jump->GetRect().right);
    CHECK(filter->GetRect().right <= kHistoryDatePopupW);

    //通知 → 动作
    CHECK(HistoryDatePopupActionFromNotify(DUIN_CLICK, kIdDatePopupJump) == kHistoryDatePopupJump);
    CHECK(HistoryDatePopupActionFromNotify(DUIN_CLICK, kIdDatePopupFilter) == kHistoryDatePopupFilter);
    for (size_t i = 0; i < sizeof(spins) / sizeof(spins[0]); ++i)
    {
        INFO("enter in edit of spin " << spins[i]);
        CHECK(HistoryDatePopupActionFromNotify((UINT)DuiEdit::DUIN_EDIT_ENTER,
                                               spins[i] + kIdDatePopupEditOffset) == kHistoryDatePopupJump);
    }
    //与弹层无关的通知
    CHECK(HistoryDatePopupActionFromNotify(DUIN_CLICK, kIdDatePopupFromYear) == kHistoryDatePopupNone);
    CHECK(HistoryDatePopupActionFromNotify((UINT)DuiEdit::DUIN_EDIT_ENTER, kIdDatePopupJump) == kHistoryDatePopupNone);
}

// =====================================================================
// HWL-C1~C2 —— 底部「清空聊天记录」按钮（2026-10-03 从聊天窗头部移到这里，放在「导出」左边）
// =====================================================================

TEST_CASE("HWL-C1 底部一行有「清空聊天记录」，紧挨在「导出」左边、与「导出」等高；关掉开关时没有", "[history][clearconv]")
{
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestBodyWidth, kTestFewPages, 1);
    const DuiControl* clear = FindById(root.get(), kIdHistoryClearConversation);
    const DuiControl* exportBtn = FindById(root.get(), kIdHistoryExport);
    REQUIRE(dynamic_cast<const DuiButton*>(clear) != nullptr);
    REQUIRE(exportBtn != nullptr);
    CHECK(clear->IsVisible());

    //同一行、子控件顺序里紧挨在「导出」前面
    REQUIRE(clear->GetParent() == exportBtn->GetParent());
    const std::vector<std::unique_ptr<DuiControl>>& kids = exportBtn->GetParent()->Children();
    size_t clearAt = kids.size();
    size_t exportAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == clear)
            clearAt = i;
        if (kids[i].get() == exportBtn)
            exportAt = i;
    }
    CHECK(clearAt + 1 == exportAt);

    //排版后在「导出」左边、与它等高且上下对齐
    const RECT rcClear = clear->GetRect();
    const RECT rcExport = exportBtn->GetRect();
    CHECK(rcClear.right > rcClear.left);
    CHECK(rcClear.right <= rcExport.left);
    CHECK(rcClear.top == rcExport.top);
    CHECK(rcClear.bottom == rcExport.bottom);
    const DuiButton* button = dynamic_cast<const DuiButton*>(clear);
    CHECK(button->GetText() == CString(_T("清空聊天记录")));

    //调试入口没有主面板，窗口把开关关掉：这一行里没有这个按钮
    HistoryViewData data;
    FillMockHistory(data);
    data.m_showClearConversation = false;
    std::unique_ptr<DuiControl> noClear = BuildHistoryWindowBody(data);
    REQUIRE(noClear != nullptr);
    CHECK(FindById(noClear.get(), kIdHistoryClearConversation) == nullptr);
}

TEST_CASE("HWL-C2 最小宽度且页码格最多时，「清空聊天记录」照常显示，与分页器等互不重叠", "[history][clearconv]")
{
    //底部一行逐个可见子控件检查不重叠、不越界（含新增的按钮）
    CheckFooterNoOverlap(kTestNarrowBodyWidth, kTestManyPages, kTestMiddlePage);
    std::unique_ptr<DuiControl> root = BuildFooterAt(kTestNarrowBodyWidth, kTestManyPages, kTestMiddlePage);
    CHECK(IsShown(root.get(), kIdHistoryClearConversation));
    CHECK(IsShown(root.get(), kIdHistoryExport));
    //为它让出位置的是「跳至 _ 页」（第三级隐藏，2026-10-03 与用户确认）
    CHECK_FALSE(IsShown(root.get(), kIdHistoryPageJump));
    CheckPageCellsShown(root.get(), kTestManyPages, kTestMiddlePage);
}

// =====================================================================
// TGE13b  帖子卡片与补充卡片下的缩略图（docs/帖子群方案.md 14.5，帖子群第 5g 步）
//
// 带图片的帖子卡片在正文下面排一行缩略图（最多 3 张，按原图宽高等比缩进 72 见方的方框），最后一张写「+N」；
// 每张有自己的编号，单击看图、双击不转交给行（行的双击是「定位」）。
// =====================================================================

TEST_CASE("TGE13b 消息记录行里帖子卡片的缩略图：编号、「+N」、双击与排版", "[threadgroup]")
{
    const int kWideW = 800;     //横图原图宽
    const int kWideH = 400;     //横图原图高
    const int kExtra = 2;       //没显示出来的张数
    std::vector<HistoryRowView> rows;
    HistoryRowView card = MakeContentRow(_T("发帖：周报"), false, 9801);
    for (size_t k = 0; k < kHistoryCardMaxThumbs; ++k)
    {
        HistoryCardThumbView view;
        view.m_state = balloonchatwnd::kHistoryThumbNeedDownload;
        view.m_width = kWideW;
        view.m_height = kWideH;
        card.m_cardThumbs.push_back(view);
    }
    card.m_cardThumbExtra = kExtra;
    rows.push_back(card);
    //第 1 行正文相同、没有图片
    rows.push_back(MakeContentRow(_T("发帖：周报"), false, 9802));

    std::unique_ptr<DuiControl> content = BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, kTestLayoutHeight });

    const DuiControl* row0 = FindById(content.get(), (UINT)(kIdHistoryRowBase + 0));
    const DuiControl* row1 = FindById(content.get(), (UINT)(kIdHistoryRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    const RECT rcRow0 = row0->GetRect();

    int expectW = 0;
    int expectH = 0;
    ComputeHistoryCardThumbSize(kWideW, kWideH, expectW, expectH);
    for (size_t k = 0; k < kHistoryCardMaxThumbs; ++k)
    {
        const HistoryCardThumb* thumb = dynamic_cast<const HistoryCardThumb*>(
            FindById(content.get(), (UINT)(kIdHistoryCardThumbBase + k)));
        INFO("第 " << k << " 张");
        REQUIRE(thumb != nullptr);
        //一律可点：本机有图时看图，没有时先下载
        CHECK(thumb->IsClickable());
        CHECK(thumb->GetImageState() == balloonchatwnd::kHistoryThumbNeedDownload);
        //只有最后一张写「+N」
        CHECK(thumb->GetExtraCount() == ((k + 1 == kHistoryCardMaxThumbs) ? kExtra : 0));
        //按原图宽高等比缩放，整张落在本行里
        const RECT rc = thumb->GetRect();
        CHECK(RectWidth(rc) == expectW);
        CHECK(RectHeight(rc) == expectH);
        CHECK(rc.top >= rcRow0.top);
        CHECK(rc.bottom <= rcRow0.bottom);
        //双击由缩略图拦下，不转交给行
        CHECK(const_cast<HistoryCardThumb*>(thumb)->OnLButtonDblClk(POINT{ rc.left + 1, rc.top + 1 }, 0));
    }
    //没有图片的行不占这一段编号，行也比带缩略图的行矮
    CHECK(FindById(content.get(), (UINT)(kIdHistoryCardThumbBase + kHistoryCardMaxThumbs)) == nullptr);
    CHECK(RectHeight(row1->GetRect()) < RectHeight(rcRow0));
    //缩略图那一行的高度取最高的一张（横图缩放后只有 36 像素高，不按 72 预留，下面不留空白；2026-10-08 实机发现）
    CHECK(HistoryCardThumbStripHeight(rows[0]) == expectH);
    CHECK(HistoryCardThumbStripHeight(rows[1]) == 0);
}

TEST_CASE("TGE13c 帖子卡片缩略图的尺寸与点击编号的换算", "[threadgroup]")
{
    SECTION("尺寸：按长边缩进 72 见方，小图不放大，宽高未知时为方框本身")
    {
        int w = 0;
        int h = 0;
        ComputeHistoryCardThumbSize(800, 400, w, h);
        CHECK(w == kHistoryCardThumbPx);
        CHECK(h == kHistoryCardThumbPx / 2);
        ComputeHistoryCardThumbSize(300, 600, w, h);
        CHECK(w == kHistoryCardThumbPx / 2);
        CHECK(h == kHistoryCardThumbPx);
        ComputeHistoryCardThumbSize(40, 30, w, h);
        CHECK(w == 40);
        CHECK(h == 30);
        ComputeHistoryCardThumbSize(0, 30, w, h);
        CHECK(w == kHistoryCardThumbPx);
        CHECK(h == kHistoryCardThumbPx);
        //极扁的图至少留 1 像素
        ComputeHistoryCardThumbSize(10000, 1, w, h);
        CHECK(w == kHistoryCardThumbPx);
        CHECK(h == 1);
    }

    SECTION("编号：基址 + 行 × 每行张数 + 第几张；段外返回 false 且不改出参")
    {
        int row = -1;
        int thumb = -1;
        const int kRow = 5;
        const int kThumb = 2;
        REQUIRE(ResolveHistoryCardThumbClick((UINT)(kIdHistoryCardThumbBase + kRow * kHistoryCardMaxThumbs + kThumb),
                                             row, thumb));
        CHECK(row == kRow);
        CHECK(thumb == kThumb);
        row = -1;
        thumb = -1;
        CHECK_FALSE(ResolveHistoryCardThumbClick((UINT)(kIdHistoryCardThumbBase - 1), row, thumb));
        CHECK_FALSE(ResolveHistoryCardThumbClick((UINT)(kIdHistoryCardThumbBase + kIdHistoryCardThumbCapacity), row, thumb));
        CHECK(row == -1);
        CHECK(thumb == -1);
    }
}
