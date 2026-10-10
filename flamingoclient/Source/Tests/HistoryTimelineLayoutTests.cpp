/**
 *  聊天记录页时间轴模式的布局测试（Suite TLL），HistoryTimelineLayoutTests.cpp
 *
 *  覆盖 docs/聊天记录管理器方案.md §11.7 的界面部分（HistoryWindowLayout 与 HistoryTimelineLayout）：
 *    TLL1 右侧界面新增的控件：「列表 | 时间轴」在搜索行最右边、默认选中「列表」，导航条与命中条平时隐藏；
 *         最小宽度下搜索框仍不窄于 200 像素，切换胶囊在发送人下拉右边、不越出搜索行；
 *    TLL2 切到时间轴：导航条出现在列表右侧、列表相应变窄；切回列表后恢复，命中条一并隐藏（UI 约定第 5 条）；
 *    TLL3 命中条：显示时列表下移一个命中条的高度，文字与按钮可用状态按参数设置；隐藏后列表回到原位；
 *    TLL4 时间轴样式的列表：日期大节点行、行首时间列与竖线节点列、命中节点的颜色、行高与普通样式相同、
 *         竖线贯穿整行、名字后面不再写时间；
 *    TLL5 导航条内容：年标题行与月份行的编号、收起的年份不建月份行、当前月份高亮、编号换回年月、图例。
 *  不创建窗口，纯内存构建 + 排版 + 断言。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/HistoryTimelineLayout.h"
#include "../UI/BalloonHistory/HistoryRow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

#include <memory>
#include <set>
#include <vector>

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//排版用的右侧区域尺寸（像素）：宽度取原来消息记录窗的默认宽度
const int kTestWidth = 898;
const int kTestHeight = 700;
//右侧界面的最小宽度（像素），与 HistoryWindowLayout.h 的 kMgrBodyMinW 相同
const int kBodyMinW = 640;
//命中条高（像素），与 HistoryWindowLayout.cpp 的 kHitBarH 相同
const int kHitBarH = 36;
//命中节点与普通节点的颜色，与 HistoryWindowLayout.cpp 的 kColorTimelineHit / kColorTimelineDot 相同
const COLORREF kHitDotColor = RGB(0xE0, 0xA8, 0x00);
const COLORREF kDotColor = RGB(0xB7, 0xC3, 0xD6);

//递归按 id 查控件；命中返回指针，否则 nullptr
DuiControl* FindById(DuiControl* node, UINT id)
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
        DuiControl* hit = FindById(child.get(), id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

int Width(const RECT& rc)
{
    return rc.right - rc.left;
}

int Height(const RECT& rc)
{
    return rc.bottom - rc.top;
}

//矩形 inner 是否完整落在 outer 里
bool RectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right
        && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

//建好并排好版的右侧界面（假数据）
std::unique_ptr<DuiControl> BuildBodyAt(int width, int height, bool timeline)
{
    HistoryViewData data;
    FillMockHistory(data);
    data.m_timeline = timeline;
    std::unique_ptr<DuiControl> root = BuildHistoryWindowBody(data);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, width, height });
    return root;
}

//一行文字消息的显示数据
HistoryRowView TextRow(long long msgid, LPCTSTR time, LPCTSTR text)
{
    HistoryRowView v;
    v.m_senderName = _T("林知夏");
    v.m_timeText = time;
    v.m_previewText = text;
    v.m_fullText = text;
    v.m_msgid = msgid;
    return v;
}

//导航数据：2026 年 10、9 月，2025 年 12 月
std::vector<HistoryTimelineYear> NavYears()
{
    std::vector<HistoryPeriodCount> counts;
    counts.push_back(HistoryPeriodCount(202610, 30));
    counts.push_back(HistoryPeriodCount(202609, 85));
    counts.push_back(HistoryPeriodCount(202512, 10));
    std::vector<HistoryPeriodCount> hits;
    hits.push_back(HistoryPeriodCount(202610, 5));
    return BuildTimelineYears(counts, hits);
}

} // anonymous namespace

TEST_CASE("TLL1 新控件：「列表 | 时间轴」在搜索行最右边、默认选中列表；导航条与命中条平时隐藏；最小宽度下排得下", "[timeline]")
{
    std::unique_ptr<DuiControl> root = BuildBodyAt(kTestWidth, kTestHeight, false);
    const UINT ids[] =
    {
        kIdHistoryViewList, kIdHistoryViewTimeline, kIdHistoryListHost, kIdHistoryNav, kIdHistoryNavScroll,
        kIdHistoryNavList, kIdHistoryHitBar, kIdHistoryHitInfo, kIdHistoryHitPos, kIdHistoryHitPrev, kIdHistoryHitNext,
        kIdHistoryScroll,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id = " << ids[i]);
        CHECK(FindById(root.get(), ids[i]) != nullptr);
    }
    DuiButton* listChip = dynamic_cast<DuiButton*>(FindById(root.get(), kIdHistoryViewList));
    DuiButton* timelineChip = dynamic_cast<DuiButton*>(FindById(root.get(), kIdHistoryViewTimeline));
    REQUIRE(listChip != nullptr);
    REQUIRE(timelineChip != nullptr);
    CHECK(listChip->GetVariant() == DuiButton::Variant::Primary);
    CHECK(timelineChip->GetVariant() == DuiButton::Variant::Default);
    CHECK_FALSE(FindById(root.get(), kIdHistoryNav)->IsVisible());
    CHECK_FALSE(FindById(root.get(), kIdHistoryHitBar)->IsVisible());
    //列表在列表那一行里占满整宽
    DuiControl* host = FindById(root.get(), kIdHistoryListHost);
    DuiControl* scroll = FindById(root.get(), kIdHistoryScroll);
    CHECK(scroll->GetParent() == host);
    CHECK(Width(scroll->GetRect()) == Width(host->GetRect()));

    //建出来时就是时间轴模式
    std::unique_ptr<DuiControl> tl = BuildBodyAt(kTestWidth, kTestHeight, true);
    CHECK(FindById(tl.get(), kIdHistoryNav)->IsVisible());
    CHECK(dynamic_cast<DuiButton*>(FindById(tl.get(), kIdHistoryViewTimeline))->GetVariant()
          == DuiButton::Variant::Primary);

    //最小宽度：搜索框不窄于 200 像素，切换胶囊在发送人下拉右边、不越出右侧界面
    const int kSearchMinW = 200;
    std::unique_ptr<DuiControl> narrow = BuildBodyAt(kBodyMinW, kTestHeight, false);
    DuiControl* search = FindById(narrow.get(), kIdHistorySearch);
    DuiControl* sender = FindById(narrow.get(), kIdHistorySenderCombo);
    DuiControl* nList = FindById(narrow.get(), kIdHistoryViewList);
    DuiControl* nTimeline = FindById(narrow.get(), kIdHistoryViewTimeline);
    CHECK(Width(search->GetRect()) >= kSearchMinW);
    CHECK(nList->GetRect().left >= sender->GetRect().right);
    CHECK(nTimeline->GetRect().left >= nList->GetRect().right);
    CHECK(nTimeline->GetRect().right <= kBodyMinW);
    CHECK(Width(nList->GetRect()) > 0);
    CHECK(Width(nTimeline->GetRect()) > 0);
}

TEST_CASE("TLL2 切到时间轴：导航条出现在列表右侧、列表变窄；切回列表后恢复，命中条一并隐藏", "[timeline]")
{
    std::unique_ptr<DuiControl> root = BuildBodyAt(kTestWidth, kTestHeight, false);
    DuiControl* host = FindById(root.get(), kIdHistoryListHost);
    DuiControl* scroll = FindById(root.get(), kIdHistoryScroll);
    DuiControl* nav = FindById(root.get(), kIdHistoryNav);
    const int fullW = Width(scroll->GetRect());

    ApplyHistoryTimelineMode(root.get(), true);
    CHECK(nav->IsVisible());
    CHECK(Width(nav->GetRect()) == kHistoryNavW);
    CHECK(nav->GetRect().right == host->GetRect().right);
    CHECK(Width(scroll->GetRect()) == fullW - kHistoryNavW);
    CHECK(scroll->GetRect().right == nav->GetRect().left);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdHistoryViewTimeline))->GetVariant()
          == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdHistoryViewList))->GetVariant()
          == DuiButton::Variant::Default);
    //导航条的滚动视口排进了导航条里
    DuiControl* navScroll = FindById(root.get(), kIdHistoryNavScroll);
    CHECK(Width(navScroll->GetRect()) > 0);
    CHECK(RectInside(navScroll->GetRect(), nav->GetRect()));

    //时间轴模式下显示过命中条，切回列表时一并隐藏
    ApplyHistoryHitBar(root.get(), true, _T("共 3 处命中"), _T("第 1 / 3 处"), true);
    CHECK(IsHistoryHitBarVisible(root.get()));
    ApplyHistoryTimelineMode(root.get(), false);
    CHECK_FALSE(nav->IsVisible());
    CHECK_FALSE(IsHistoryHitBarVisible(root.get()));
    CHECK(Width(scroll->GetRect()) == fullW);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdHistoryViewList))->GetVariant()
          == DuiButton::Variant::Primary);

    //根容器为空时什么都不做
    ApplyHistoryTimelineMode(NULL, true);
}

TEST_CASE("TLL3 命中条：显示时列表下移一个命中条的高度，文字与按钮状态按参数设置；隐藏后回到原位", "[timeline]")
{
    std::unique_ptr<DuiControl> root = BuildBodyAt(kTestWidth, kTestHeight, true);
    DuiControl* host = FindById(root.get(), kIdHistoryListHost);
    const int topBefore = host->GetRect().top;

    ApplyHistoryHitBar(root.get(), true, _T("共 31 处命中"), _T("第 3 / 31 处"), true);
    DuiControl* bar = FindById(root.get(), kIdHistoryHitBar);
    REQUIRE(bar != nullptr);
    CHECK(bar->IsVisible());
    CHECK(Height(bar->GetRect()) == kHitBarH);
    CHECK(host->GetRect().top == topBefore + kHitBarH);
    CHECK(bar->GetRect().bottom == host->GetRect().top);
    DuiLabel* info = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdHistoryHitInfo));
    DuiLabel* pos = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdHistoryHitPos));
    REQUIRE(info != nullptr);
    REQUIRE(pos != nullptr);
    CHECK(info->GetText() == CString(L"共 31 处命中"));
    CHECK(pos->GetText() == CString(L"第 3 / 31 处"));
    DuiControl* prev = FindById(root.get(), kIdHistoryHitPrev);
    DuiControl* next = FindById(root.get(), kIdHistoryHitNext);
    CHECK(prev->IsEnabled());
    CHECK(next->IsEnabled());
    CHECK(Width(prev->GetRect()) > 0);
    CHECK(next->GetRect().left >= prev->GetRect().right);
    CHECK(next->GetRect().right <= kTestWidth);

    //没有命中：按钮不可点
    ApplyHistoryHitBar(root.get(), true, _T("没有找到命中的消息"), _T(""), false);
    CHECK_FALSE(prev->IsEnabled());
    CHECK_FALSE(next->IsEnabled());
    CHECK(host->GetRect().top == topBefore + kHitBarH);

    ApplyHistoryHitBar(root.get(), false, CString(), CString(), false);
    CHECK_FALSE(IsHistoryHitBarVisible(root.get()));
    CHECK(host->GetRect().top == topBefore);
}

TEST_CASE("TLL4 时间轴样式的列表：日期大节点、时间列与竖线列、命中节点标黄、行高不变、竖线贯穿整行", "[timeline]")
{
    std::vector<HistoryRowView> rows;
    rows.push_back(TextRow(3, _T("20:58"), _T("收到")));
    rows.push_back(TextRow(2, _T("18:12"), _T("第三季度工作报告")));
    rows.push_back(TextRow(1, _T("16:05"), _T("上个季度报告里的数字要不要沿用？")));
    rows[0].m_groupLabel = _T("2026-10-01");
    rows[0].m_groupDetail = _T("星期四 · 2 条");
    rows[2].m_groupLabel = _T("2026-09-30");
    rows[2].m_groupDetail = _T("星期三 · 1 条");
    std::vector<HistoryRowView> plain = rows;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        rows[i].m_timeline = true;
    }
    rows[1].m_timelineHit = true;

    std::unique_ptr<DuiControl> list = BuildListContent(rows, _T(""), _T(""), kTestWidth);
    std::unique_ptr<DuiControl> plainList = BuildListContent(plain, _T(""), _T(""), kTestWidth);
    REQUIRE(list != nullptr);
    list->ForceLayout(RECT{ 0, 0, kTestWidth, kTestHeight });
    plainList->ForceLayout(RECT{ 0, 0, kTestWidth, kTestHeight });

    //子控件依次是：日期大节点、行 0、行 1、日期大节点、行 2
    REQUIRE(list->Children().size() == 5);
    CHECK(dynamic_cast<HistoryRow*>(list->Children()[0].get()) == nullptr);
    CHECK(dynamic_cast<HistoryRow*>(list->Children()[3].get()) == nullptr);
    DuiControl* day = list->Children()[0].get();
    //日期大节点：空出时间列 + 竖线节点列 + 日期 + 间距 + 说明
    REQUIRE(day->Children().size() == 5);
    DuiLabel* date = dynamic_cast<DuiLabel*>(day->Children()[2].get());
    DuiLabel* detail = dynamic_cast<DuiLabel*>(day->Children()[4].get());
    REQUIRE(date != nullptr);
    REQUIRE(detail != nullptr);
    CHECK(date->GetText() == CString(L"2026-10-01"));
    CHECK(detail->GetText() == CString(L"星期四 · 2 条"));
    CHECK(date->GetRect().right <= detail->GetRect().left);

    //行：行首是时间列，接着是竖线节点列，再后面是原来的各列
    HistoryRow* row1 = dynamic_cast<HistoryRow*>(FindById(list.get(), kIdHistoryRowBase + 1));
    HistoryRow* row0 = dynamic_cast<HistoryRow*>(FindById(list.get(), kIdHistoryRowBase + 0));
    REQUIRE(row1 != nullptr);
    REQUIRE(row0 != nullptr);
    REQUIRE(row1->Children().size() == 3);
    DuiControl* timeCol = row1->Children()[0].get();
    DuiControl* rail = row1->Children()[1].get();
    DuiLabel* timeLabel = dynamic_cast<DuiLabel*>(timeCol->Children()[1].get());
    REQUIRE(timeLabel != nullptr);
    CHECK(timeLabel->GetText() == CString(L"18:12"));
    //竖线节点列：上段竖线、节点、下段竖线，贯穿整行
    REQUIRE(rail->Children().size() == 3);
    CHECK(rail->GetRect().top == row1->GetRect().top);
    CHECK(rail->GetRect().bottom == row1->GetRect().bottom);
    DuiLabel* hitDot = dynamic_cast<DuiLabel*>(rail->Children()[1].get());
    DuiLabel* normalDot = dynamic_cast<DuiLabel*>(row0->Children()[1]->Children()[1].get());
    REQUIRE(hitDot != nullptr);
    REQUIRE(normalDot != nullptr);
    CHECK(hitDot->GetTextColor() == kHitDotColor);
    CHECK(normalDot->GetTextColor() == kDotColor);
    //相邻两行的竖线首尾相接
    HistoryRow* row0Line = row0;
    CHECK(row0Line->Children()[1]->GetRect().bottom == rail->GetRect().top);

    //行高与普通样式相同（时间轴样式的上下内边距挪到了内层容器上）
    HistoryRow* plainRow1 = dynamic_cast<HistoryRow*>(FindById(plainList.get(), kIdHistoryRowBase + 1));
    REQUIRE(plainRow1 != nullptr);
    CHECK(Height(row1->GetRect()) == Height(plainRow1->GetRect()));
    //内层容器：名字那一行只有名字（不再写时间），与普通样式相比少了时间那一列
    DuiControl* body = row1->Children()[2].get();
    CHECK(body->GetRect().left >= rail->GetRect().right);
    CHECK(body->GetRect().right <= row1->GetRect().right);
}

TEST_CASE("TLL5 导航条内容：编号、收起的年份不建月份行、当前月份高亮、编号换回年月、图例", "[timeline]")
{
    const std::vector<HistoryTimelineYear> years = NavYears();
    REQUIRE(years.size() == 2);
    std::set<int> expanded;
    expanded.insert(2026);
    std::unique_ptr<DuiControl> content = BuildTimelineNavContent(years, 202609, expanded);
    REQUIRE(content != nullptr);
    CHECK(content->GetCtrlId() == (UINT)kIdHistoryNavList);
    content->ForceLayout(RECT{ 0, 0, kHistoryNavW, kTestHeight });

    //2026 年展开：两个月份行（编号 0、1）；2025 年收起：只有标题行，它的月份（编号 2）不建
    CHECK(FindById(content.get(), kIdHistoryNavYearBase + 0) != nullptr);
    CHECK(FindById(content.get(), kIdHistoryNavYearBase + 1) != nullptr);
    CHECK(FindById(content.get(), kIdHistoryNavMonthBase + 0) != nullptr);
    CHECK(FindById(content.get(), kIdHistoryNavMonthBase + 1) != nullptr);
    CHECK(FindById(content.get(), kIdHistoryNavMonthBase + 2) == nullptr);

    //当前月份（9 月，编号 1）高亮底色，其它月份没有底色
    DuiVBox* current = dynamic_cast<DuiVBox*>(FindById(content.get(), kIdHistoryNavMonthBase + 1));
    DuiVBox* other = dynamic_cast<DuiVBox*>(FindById(content.get(), kIdHistoryNavMonthBase + 0));
    REQUIRE(current != nullptr);
    REQUIRE(other != nullptr);
    CHECK(current->GetBgColor() != CLR_INVALID);
    CHECK(other->GetBgColor() == CLR_INVALID);
    //月份行排在导航条宽度以内
    CHECK(RectInside(current->GetRect(), content->GetRect()));

    //编号换回年月：收起的年份同样能换算
    int year = 0;
    int month = 0;
    CHECK(TimelineNavMonthOfId(years, kIdHistoryNavMonthBase + 0, year, month));
    CHECK(year == 2026);
    CHECK(month == 10);
    CHECK(TimelineNavMonthOfId(years, kIdHistoryNavMonthBase + 2, year, month));
    CHECK(year == 2025);
    CHECK(month == 12);
    CHECK_FALSE(TimelineNavMonthOfId(years, kIdHistoryNavMonthBase + 3, year, month));
    CHECK_FALSE(TimelineNavMonthOfId(years, kIdHistoryNavYearBase, year, month));
    CHECK(TimelineNavYearOfId(years, kIdHistoryNavYearBase + 1, year));
    CHECK(year == 2025);
    CHECK_FALSE(TimelineNavYearOfId(years, kIdHistoryNavYearBase + 2, year));
    CHECK_FALSE(TimelineNavYearOfId(years, kIdHistoryNavMonthBase, year));

    //两年都展开：三个月份行
    expanded.insert(2025);
    content = BuildTimelineNavContent(years, 0, expanded);
    CHECK(FindById(content.get(), kIdHistoryNavMonthBase + 2) != nullptr);

    //没有数据：没有任何行，也没有图例
    content = BuildTimelineNavContent(std::vector<HistoryTimelineYear>(), 0, expanded);
    CHECK(content->Children().empty());

    //编号段不与右侧界面其余各段重叠
    CHECK((UINT)kIdHistoryNavMonthBase + kIdHistoryNavMonthCapacity <= (UINT)kIdHistoryNavYearBase);
    CHECK((UINT)kIdHistoryViewList >= (UINT)7400);
}
