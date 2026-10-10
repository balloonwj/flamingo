/**
 *  聊天记录管理器——左栏与左右分栏的布局测试（Suite HML），HistoryManagerLayoutTests.cpp
 *
 *  覆盖 HistoryWindowLayout 里为聊天记录管理器新增的部分（docs/聊天记录管理器方案.md §3）：
 *    · BuildHistoryManagerPane：左栏控件都能按编号找到，初始的类型筛选、范围按钮、两段列表状态；
 *    · BuildHistoryManagerRoot / ApplyHistoryManagerCollapsed：展开与折叠时两侧的位置与宽度、
 *      右侧「»」的显隐，以及折叠后再展开时左栏重新排过版（开发约定文档 UI 约定第 5 条）；
 *    · 分隔位置受两侧最小宽度限制；
 *    · ApplyHistoryManagerListView / ApplyHistoryManagerScope / ApplyHistoryHeaderTitle：显隐与重排；
 *    · 默认窗口尺寸下右侧界面的宽度与原来的消息记录窗相同；
 *    · 搜索范围弹层的内容与一行的构造。
 *  不创建窗口，纯内存构建 + 排版 + 断言。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/HistoryConversationList.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // HML1：左栏列表滚动条的命中带宽度
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // HML11：「通配符」复选框的悬停说明

#include <algorithm>
#include <memory>
#include <vector>

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//排版用的窗口客户区高度（像素）
const int kTestHeight = 700;
//原来消息记录窗的默认宽度（像素），与 BalloonHistoryWindow.cpp 的 kHisWindowWidth 相同
const int kTestBodyDefaultW = 898;

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

//造一个排好版的分栏根容器：右侧用假数据的消息记录界面
std::unique_ptr<DuiControl> BuildRootAt(int width, int paneWidth, bool collapsed)
{
    HistoryViewData data;
    FillMockHistory(data);
    data.m_conversationName = _T("苏沐辰");
    std::unique_ptr<DuiControl> root = BuildHistoryManagerRoot(BuildHistoryManagerPane(kManagerTypeAll, 0),
                                                               BuildHistoryWindowBody(data),
                                                               paneWidth, collapsed);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, width, kTestHeight });
    return root;
}

} // anonymous namespace

TEST_CASE("HML1 左栏控件都能按编号找到，初始只显示会话树，范围为全部会话", "[historymgr]")
{
    std::unique_ptr<DuiControl> pane = BuildHistoryManagerPane(kManagerTypeSingle, 0);
    REQUIRE(pane != nullptr);
    CHECK(pane->GetCtrlId() == (UINT)kIdMgrPane);

    const UINT ids[] =
    {
        kIdMgrSearch, kIdMgrCollapse, kIdMgrScopeRow, kIdMgrScope, kIdMgrScopeClear, kIdMgrWildcard,
        kIdMgrTypeAll, kIdMgrTypeSingle, kIdMgrTypeGroup, kIdMgrScroll, kIdMgrListBox,
        kIdMgrConvHeader, kIdMgrConvTree, kIdMgrHitHeader, kIdMgrHitTree, kIdMgrEmpty,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id = " << ids[i]);
        CHECK(FindById(pane.get(), ids[i]) != nullptr);
    }

    //左栏列表的滚动视口保持默认的命中带宽度：置 0 就完全没有滚动条（2026-10-04 界面核对时发现过这一遗漏）
    DuiScrollView* mgrScroll = dynamic_cast<DuiScrollView*>(FindById(pane.get(), kIdMgrScroll));
    REQUIRE(mgrScroll != nullptr);
    CHECK(mgrScroll->GetScrollBarWidth() == DuiScrollBar::kOverlayBandPx);

    //类型筛选：初始选中「单聊」
    DuiButton* single = dynamic_cast<DuiButton*>(FindById(pane.get(), kIdMgrTypeSingle));
    DuiButton* all = dynamic_cast<DuiButton*>(FindById(pane.get(), kIdMgrTypeAll));
    REQUIRE(single != nullptr);
    REQUIRE(all != nullptr);
    CHECK(single->GetVariant() == DuiButton::Variant::Primary);
    CHECK(all->GetVariant() == DuiButton::Variant::Default);

    //范围按钮：全部会话，「×」隐藏
    DuiButton* scope = dynamic_cast<DuiButton*>(FindById(pane.get(), kIdMgrScope));
    REQUIRE(scope != nullptr);
    CHECK(scope->GetText() == CString(L"范围：全部会话"));
    CHECK_FALSE(FindById(pane.get(), kIdMgrScopeClear)->IsVisible());

    //列表：两段标题、命中树、空提示平时都隐藏
    CHECK_FALSE(FindById(pane.get(), kIdMgrConvHeader)->IsVisible());
    CHECK_FALSE(FindById(pane.get(), kIdMgrHitHeader)->IsVisible());
    CHECK_FALSE(FindById(pane.get(), kIdMgrHitTree)->IsVisible());
    CHECK_FALSE(FindById(pane.get(), kIdMgrEmpty)->IsVisible());
}

TEST_CASE("HML2 展开时左栏占给定宽度，右侧紧接在分隔条之后，「»」隐藏", "[historymgr]")
{
    const int kPaneW = 260;
    const int width = kTestBodyDefaultW + kPaneW + kMgrSplitBarW;
    std::unique_ptr<DuiControl> root = BuildRootAt(width, kPaneW, false);
    CHECK(root->GetCtrlId() == (UINT)kIdMgrSplitter);

    DuiControl* pane = FindById(root.get(), kIdMgrPane);
    DuiControl* body = FindById(root.get(), kIdHistoryBody);
    REQUIRE(pane != nullptr);
    REQUIRE(body != nullptr);
    CHECK(pane->IsVisible());
    CHECK(Width(pane->GetRect()) == kPaneW);
    CHECK(body->GetRect().left == kPaneW + kMgrSplitBarW);
    //默认窗口宽度下，右侧界面与原来的消息记录窗一样宽
    CHECK(Width(body->GetRect()) == kTestBodyDefaultW);
    CHECK_FALSE(FindById(root.get(), kIdHistoryExpandLeft)->IsVisible());

    //左栏里的搜索框确实排进了左栏
    DuiControl* search = FindById(root.get(), kIdMgrSearch);
    REQUIRE(search != nullptr);
    CHECK(Width(search->GetRect()) > 0);
    CHECK(search->GetRect().right <= kPaneW);
}

TEST_CASE("HML3 折叠后左栏隐藏、右侧几乎占满；再展开时左栏按新宽度重新排版", "[historymgr]")
{
    const int width = 1200;
    std::unique_ptr<DuiControl> root = BuildRootAt(width, kMgrPaneDefaultW, true);
    DuiControl* pane = FindById(root.get(), kIdMgrPane);
    DuiControl* body = FindById(root.get(), kIdHistoryBody);
    REQUIRE(pane != nullptr);
    REQUIRE(body != nullptr);
    CHECK_FALSE(pane->IsVisible());
    CHECK(body->GetRect().left == kMgrSplitBarCollapsedW);
    CHECK(Width(body->GetRect()) == width - kMgrSplitBarCollapsedW);
    CHECK(FindById(root.get(), kIdHistoryExpandLeft)->IsVisible());

    //展开到 300：左栏显示、宽度 300，左栏里的控件按新宽度排过（不是隐藏前的旧矩形）
    const int kNewPaneW = 300;
    ApplyHistoryManagerCollapsed(root.get(), false, kNewPaneW, _T("苏沐辰"));
    CHECK(pane->IsVisible());
    CHECK(Width(pane->GetRect()) == kNewPaneW);
    CHECK(body->GetRect().left == kNewPaneW + kMgrSplitBarW);
    CHECK_FALSE(FindById(root.get(), kIdHistoryExpandLeft)->IsVisible());
    DuiControl* search = FindById(root.get(), kIdMgrSearch);
    REQUIRE(search != nullptr);
    CHECK(Width(search->GetRect()) > 0);
    CHECK(search->GetRect().right <= kNewPaneW);
    //右侧标题区的会话名跟着展开重新排过
    DuiControl* title = FindById(root.get(), kIdHistoryTitle);
    REQUIRE(title != nullptr);
    CHECK(title->IsVisible());
    CHECK(title->GetRect().left >= body->GetRect().left);

    //再折叠回去
    ApplyHistoryManagerCollapsed(root.get(), true, kNewPaneW, _T("苏沐辰"));
    CHECK_FALSE(pane->IsVisible());
    CHECK(body->GetRect().left == kMgrSplitBarCollapsedW);
}

TEST_CASE("HML4 分隔位置受两侧最小宽度限制：右侧不窄于原来的最小窗口宽度，左栏不窄于最小宽度", "[historymgr]")
{
    //窗口不够宽时，左栏让出宽度直到右侧够最小宽度
    const int width = kMgrPaneMinW + kMgrSplitBarW + kMgrBodyMinW + 55;
    std::unique_ptr<DuiControl> root = BuildRootAt(width, 400, false);
    DuiControl* body = FindById(root.get(), kIdHistoryBody);
    DuiControl* pane = FindById(root.get(), kIdMgrPane);
    CHECK(Width(body->GetRect()) == kMgrBodyMinW);
    CHECK(Width(pane->GetRect()) == width - kMgrSplitBarW - kMgrBodyMinW);

    //给的左栏宽度小于最小宽度时按最小宽度
    std::unique_ptr<DuiControl> narrow = BuildRootAt(1200, 50, false);
    CHECK(Width(FindById(narrow.get(), kIdMgrPane)->GetRect()) == kMgrPaneMinW);

    //不大于 0 时按默认宽度
    std::unique_ptr<DuiControl> deflt = BuildRootAt(1200, 0, false);
    CHECK(Width(FindById(deflt.get(), kIdMgrPane)->GetRect()) == kMgrPaneDefaultW);
}

TEST_CASE("HML5 列表显示状态：平时只有会话树；搜索时显示有行的段及其标题；都没有行时显示空提示", "[historymgr]")
{
    std::unique_ptr<DuiControl> root = BuildRootAt(1200, kMgrPaneDefaultW, false);
    DuiControl* convHeader = FindById(root.get(), kIdMgrConvHeader);
    DuiControl* convTree = FindById(root.get(), kIdMgrConvTree);
    DuiControl* hitHeader = FindById(root.get(), kIdMgrHitHeader);
    DuiControl* hitTree = FindById(root.get(), kIdMgrHitTree);
    DuiLabel* empty = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdMgrEmpty));
    REQUIRE(empty != nullptr);

    SECTION("平时：三行会话，树高三行")
    {
        HistoryManagerListView view;
        view.m_convRows = 3;
        ApplyHistoryManagerListView(root.get(), view);
        CHECK(convTree->IsVisible());
        CHECK((convTree->GetRect().bottom - convTree->GetRect().top) == 3 * kMgrRowHeight);
        CHECK_FALSE(convHeader->IsVisible());
        CHECK_FALSE(hitHeader->IsVisible());
        CHECK_FALSE(hitTree->IsVisible());
        CHECK_FALSE(empty->IsVisible());
    }

    SECTION("搜索：两段都有行，命中树排在会话树下面")
    {
        HistoryManagerListView view;
        view.m_searching = true;
        view.m_convRows = 2;
        view.m_hitRows = 4;
        view.m_hitHeader = _T("聊天记录 · 仅搜索本机已保存的记录");
        ApplyHistoryManagerListView(root.get(), view);
        CHECK(convHeader->IsVisible());
        CHECK(hitHeader->IsVisible());
        CHECK(hitTree->IsVisible());
        CHECK((hitTree->GetRect().bottom - hitTree->GetRect().top) == 4 * kMgrRowHeight);
        CHECK(hitHeader->GetRect().top >= convTree->GetRect().bottom);
        CHECK(hitTree->GetRect().top >= hitHeader->GetRect().bottom);
        CHECK_FALSE(empty->IsVisible());
    }

    SECTION("搜索：只有聊天记录命中时，会话一段连标题一起隐藏")
    {
        HistoryManagerListView view;
        view.m_searching = true;
        view.m_convRows = 0;
        view.m_hitRows = 1;
        view.m_hitHeader = _T("聊天记录");
        ApplyHistoryManagerListView(root.get(), view);
        CHECK_FALSE(convHeader->IsVisible());
        CHECK_FALSE(convTree->IsVisible());
        CHECK(hitTree->IsVisible());
    }

    SECTION("两段都没有行：显示空提示")
    {
        HistoryManagerListView view;
        view.m_searching = true;
        view.m_emptyText = _T("没有找到相关的会话或聊天记录");
        ApplyHistoryManagerListView(root.get(), view);
        CHECK(empty->IsVisible());
        CHECK(empty->GetText() == CString(L"没有找到相关的会话或聊天记录"));
        CHECK_FALSE(convTree->IsVisible());
        CHECK_FALSE(hitTree->IsVisible());
        CHECK((empty->GetRect().bottom - empty->GetRect().top) > 0);
    }
}

TEST_CASE("HML6 范围按钮：选了会话后文字变为已选 N 个、出现「×」，范围按钮让出它的宽度；清掉后恢复", "[historymgr]")
{
    std::unique_ptr<DuiControl> root = BuildRootAt(1200, kMgrPaneDefaultW, false);
    DuiButton* scope = dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrScope));
    DuiControl* clear = FindById(root.get(), kIdMgrScopeClear);
    REQUIRE(scope != nullptr);
    REQUIRE(clear != nullptr);
    const int fullW = Width(scope->GetRect());
    CHECK(fullW > 0);

    ApplyHistoryManagerScope(root.get(), 3);
    CHECK(scope->GetText() == CString(L"范围：已选 3 个会话"));
    CHECK(clear->IsVisible());
    CHECK(Width(scope->GetRect()) < fullW);
    CHECK(clear->GetRect().left >= scope->GetRect().right);

    ApplyHistoryManagerScope(root.get(), 0);
    CHECK(scope->GetText() == CString(L"范围：全部会话"));
    CHECK_FALSE(clear->IsVisible());
    CHECK(Width(scope->GetRect()) == fullW);
}

TEST_CASE("HML7 标题区：有会话名时显示在「共 N 条消息」左边，「»」出现时会话名右移", "[historymgr]")
{
    HistoryViewData data;
    FillMockHistory(data);
    std::unique_ptr<DuiControl> body = BuildHistoryWindowBody(data);
    body->ForceLayout(RECT{ 0, 0, kTestBodyDefaultW, kTestHeight });
    DuiControl* title = FindById(body.get(), kIdHistoryTitle);
    DuiControl* sub = FindById(body.get(), kIdHistorySubtitle);
    DuiControl* expand = FindById(body.get(), kIdHistoryExpandLeft);
    REQUIRE(title != nullptr);
    REQUIRE(sub != nullptr);
    REQUIRE(expand != nullptr);

    //没有会话名（调试入口没有设置）：会话名与「»」都隐藏
    CHECK_FALSE(title->IsVisible());
    CHECK_FALSE(expand->IsVisible());

    ApplyHistoryHeaderTitle(body.get(), _T("产品研发群"), false);
    CHECK(title->IsVisible());
    CHECK(Width(title->GetRect()) > 0);
    CHECK(sub->GetRect().left >= title->GetRect().right);
    const int titleLeft = title->GetRect().left;

    ApplyHistoryHeaderTitle(body.get(), _T("产品研发群"), true);
    CHECK(expand->IsVisible());
    CHECK(title->GetRect().left >= expand->GetRect().right);
    CHECK(title->GetRect().left > titleLeft);

    ApplyHistoryHeaderTitle(body.get(), _T(""), false);
    CHECK_FALSE(title->IsVisible());
    CHECK_FALSE(expand->IsVisible());
}

TEST_CASE("HML8 左栏控件编号与右侧不重复", "[historymgr]")
{
    std::unique_ptr<DuiControl> root = BuildRootAt(1200, kMgrPaneDefaultW, false);
    std::vector<UINT> ids;
    std::vector<DuiControl*> stack;
    stack.push_back(root.get());
    while (!stack.empty())
    {
        DuiControl* node = stack.back();
        stack.pop_back();
        if (node->GetCtrlId() != 0)
        {
            ids.push_back(node->GetCtrlId());
        }
        for (const auto& child : node->Children())
        {
            stack.push_back(child.get());
        }
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    //左栏编号排在右侧各段动态编号之后
    CHECK((UINT)kIdMgrSplitter >= (UINT)kIdHistoryGridBase + kIdHistoryGridCapacity);
}

TEST_CASE("HML9 搜索范围弹层：内容有搜索框、列表、已选个数与两个按钮；一行是复选框 + 头像 + 名字", "[historymgr]")
{
    std::unique_ptr<DuiControl> content = BuildHistoryScopePopupContent();
    REQUIRE(content != nullptr);
    CHECK(FindById(content.get(), kIdScopePopupSearch) != nullptr);
    CHECK(FindById(content.get(), kIdScopePopupScroll) != nullptr);
    CHECK(FindById(content.get(), kIdScopePopupInfo) != nullptr);
    CHECK(FindById(content.get(), kIdScopePopupAll) != nullptr);
    CHECK(FindById(content.get(), kIdScopePopupOk) != nullptr);
    content->ForceLayout(RECT{ 0, 0, kHistoryScopePopupW, kHistoryScopePopupH });
    DuiControl* ok = FindById(content.get(), kIdScopePopupOk);
    CHECK(ok->GetRect().right <= kHistoryScopePopupW);
    CHECK(ok->GetRect().bottom <= kHistoryScopePopupH);

    const UINT checkId = kIdScopePopupCheckBase + 2;
    std::unique_ptr<DuiControl> row = BuildHistoryScopePopupRow(checkId, true, _T("苏沐辰"), NULL, 10);
    REQUIRE(row != nullptr);
    row->ForceLayout(RECT{ 0, 0, kHistoryScopePopupW, kHistoryScopePopupRowH });
    DuiButton* box = dynamic_cast<DuiButton*>(FindById(row.get(), checkId));
    REQUIRE(box != nullptr);
    CHECK(box->IsChecked());
    REQUIRE(row->Children().size() == 3);
    DuiLabel* name = dynamic_cast<DuiLabel*>(row->Children()[2].get());
    REQUIRE(name != nullptr);
    CHECK(name->GetText() == CString(L"苏沐辰"));
    //头像是正方形
    const RECT av = row->Children()[1]->GetRect();
    CHECK(Width(av) == kHistoryScopePopupAvatarSize);
    CHECK((av.bottom - av.top) == kHistoryScopePopupAvatarSize);
}

//两个矩形是否有重叠部分
static bool RectsOverlap(const RECT& a, const RECT& b)
{
    RECT x;
    return ::IntersectRect(&x, &a, &b) != FALSE;
}

//矩形 inner 是否完整落在 outer 里
static bool RectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right
        && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

TEST_CASE("HML10 「通配符」复选框：两处都是复选框、默认不勾、按要求勾选；最小宽度下排得下、不与相邻控件重叠", "[historymgr],[wildcard]")
{
    //勾选框左边距、勾选框与它到文字的间距合计 28 像素（DuiButton.cpp 的 kGlyphPadPx * 2 + kGlyphSizePx），
    //控件宽度必须比这多出文字的宽度
    const int kGlyphAreaW = 28;

    //默认：两处都不勾，文字都是「通配符」
    std::unique_ptr<DuiControl> root = BuildRootAt(1200, kMgrPaneDefaultW, false);
    const UINT boxIds[] = { kIdMgrWildcard, kIdHistoryWildcard };
    for (size_t i = 0; i < sizeof(boxIds) / sizeof(boxIds[0]); ++i)
    {
        INFO("id = " << boxIds[i]);
        DuiButton* box = dynamic_cast<DuiButton*>(FindById(root.get(), boxIds[i]));
        REQUIRE(box != nullptr);
        CHECK(box->GetButtonType() == DuiButton::StyleCheckbox);
        CHECK_FALSE(box->IsChecked());
        CHECK(box->GetText() == CString(L"通配符"));
        CHECK(Width(box->GetRect()) > kGlyphAreaW);
    }

    //按要求勾选
    std::unique_ptr<DuiControl> checkedPane = BuildHistoryManagerPane(kManagerTypeAll, 0, true);
    DuiButton* mgrBox = dynamic_cast<DuiButton*>(FindById(checkedPane.get(), kIdMgrWildcard));
    REQUIRE(mgrBox != nullptr);
    CHECK(mgrBox->IsChecked());
    HistoryViewData data;
    FillMockHistory(data);
    data.m_wildcard = true;
    std::unique_ptr<DuiControl> checkedBody = BuildHistoryWindowBody(data);
    DuiButton* rightBox = dynamic_cast<DuiButton*>(FindById(checkedBody.get(), kIdHistoryWildcard));
    REQUIRE(rightBox != nullptr);
    CHECK(rightBox->IsChecked());

    //两侧都取最小宽度：左栏范围行里范围按钮仍有足够宽度，复选框在范围按钮右边、落在这一行里；
    //右侧搜索行里复选框夹在搜索框与发送人下拉之间，三者互不重叠
    const int minWidth = kMgrPaneMinW + kMgrSplitBarW + kMgrBodyMinW;
    std::unique_ptr<DuiControl> narrow = BuildRootAt(minWidth, kMgrPaneMinW, false);
    DuiControl* scopeRow = FindById(narrow.get(), kIdMgrScopeRow);
    DuiControl* scope = FindById(narrow.get(), kIdMgrScope);
    DuiControl* leftBox = FindById(narrow.get(), kIdMgrWildcard);
    REQUIRE(scopeRow != nullptr);
    REQUIRE(scope != nullptr);
    REQUIRE(leftBox != nullptr);
    //「范围：全部会话」七个字在默认字体下约 90 像素
    const int kScopeMinW = 90;
    CHECK(Width(scope->GetRect()) >= kScopeMinW);
    CHECK(leftBox->GetRect().left >= scope->GetRect().right);
    CHECK(RectInside(leftBox->GetRect(), scopeRow->GetRect()));
    //选了范围后出现「×」，三者仍排得下、不重叠
    ApplyHistoryManagerScope(narrow.get(), 3);
    DuiControl* clear = FindById(narrow.get(), kIdMgrScopeClear);
    REQUIRE(clear != nullptr);
    CHECK_FALSE(RectsOverlap(clear->GetRect(), leftBox->GetRect()));
    CHECK_FALSE(RectsOverlap(clear->GetRect(), scope->GetRect()));
    CHECK(RectInside(leftBox->GetRect(), scopeRow->GetRect()));

    DuiControl* search = FindById(narrow.get(), kIdHistorySearch);
    DuiControl* box = FindById(narrow.get(), kIdHistoryWildcard);
    DuiControl* sender = FindById(narrow.get(), kIdHistorySenderCombo);
    REQUIRE(search != nullptr);
    REQUIRE(box != nullptr);
    REQUIRE(sender != nullptr);
    //最小宽度下搜索框仍不窄于 200 像素
    const int kSearchMinW = 200;
    CHECK(Width(search->GetRect()) >= kSearchMinW);
    CHECK(box->GetRect().left >= search->GetRect().right);
    CHECK(sender->GetRect().left >= box->GetRect().right);
    CHECK(sender->GetRect().right <= FindById(narrow.get(), kIdHistoryBody)->GetRect().right);
}

TEST_CASE("HML11 「通配符」复选框带多行悬停说明：讲清 * 与 ? 的含义并给出示例", "[historymgr],[wildcard]")
{
    //左栏范围行与右侧搜索行的两个复选框都由 MakeWildcardCheckbox 造出，悬停说明相同（2026-10-07 起）
    std::unique_ptr<DuiControl> root = BuildRootAt(1200, kMgrPaneDefaultW, false);
    const UINT boxIds[] = { kIdMgrWildcard, kIdHistoryWildcard };
    for (size_t i = 0; i < sizeof(boxIds) / sizeof(boxIds[0]); ++i)
    {
        INFO("id = " << boxIds[i]);
        DuiControl* box = FindById(root.get(), boxIds[i]);
        REQUIRE(box != nullptr);
        const CString tip = DuiToolTipMgr::Inst().GetText(box);
        REQUIRE_FALSE(tip.IsEmpty());
        //多行：两个通配字符各一行、示例若干行
        CHECK(tip.Find(L'\n') >= 0);
        CHECK(tip.Find(L"*") >= 0);
        CHECK(tip.Find(L"?") >= 0);
        CHECK(tip.Find(L"示例") >= 0);
        //示例里至少有一个同时用到 * 的写法和一个用到 ? 的写法
        CHECK(tip.Find(L"会议*纪要") >= 0);
        CHECK(tip.Find(L"张?") >= 0);
    }
}
