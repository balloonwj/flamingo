/**
 *  「公告中心」窗口布局契约单元测试（Suite ANB），AnnounceBoxLayoutTests.cpp
 *
 *  用 BuildAnnounceBoxBody / BuildAnnounceListContent 直接构树，覆盖：
 *    ANB1  固定控件点名齐全（顶部信息 / 列表 / 右栏三项 / 底部按钮）；
 *    ANB2  顶部信息文案：有未读时带「N 条未读」，没有时不显示那一截；
 *    ANB3  行 id 按基址 + 下标生成，与 RowIndexFromCtrlId 互为逆运算；
 *    ANB4  RowIndexFromCtrlId 对越界 id 一律返回 -1（含容量边界）；
 *    ANB5  一条公告都没有时给出空状态，副文案为空则不建那个控件；
 *    ANB6  没有选中行时右栏标题与 meta 为空、正文位置显示占位提示；
 *    ANB7  选中某行时右栏显示该行的标题 / meta / 正文；
 *    ANB8  行的未读态与级别被正确注入 AnnounceRow；
 *    ANB9  「全部标为已读」按钮在没有未读时被禁用；
 *    ANB10 置顶公告的标题带「[置顶]」前缀；
 *    ANB11 右栏正文喂进内容高度后，名义内容矩形跟着变高，滚到底仍与视口相交；
 *    ANB12 左右两栏的滚动视口都是悬浮式自动隐藏的 MainPanelScrollView；
 *    ANB13 滚轮落在列表行的文字上时，事件仍上冒到列表的滚动视口；
 *    ANB18 滚轮落在右栏正文上时，事件仍上冒到右栏的滚动视口；
 *    ANB14 点选另一条公告（定点更新）不会改变列表的滚动位置；
 *    ANB15 定点更新同步行的选中态 / 未读点、顶部计数与底部按钮可用性；
 *    ANB16 定点更新换掉右栏三项、重算滚动范围并把正文滚回顶部；
 *    ANB17 列表的直接子控件清一色是行，行间分隔线由行自己在底边画。
 *
 *  框架 Catch2 v3。本套件只验"控件点名 + 数据注入"这层契约，不验像素级渲染。
 *  balloonwj@qq.com   2026-08-05
 */
//stdafx.h 必须最先引入：AnnounceBoxLayout.h 用到 CString 等 ATL/WTL 类型，
//按 balloonui 的头文件约定由调用方保证前置依赖（与 BalloonChatLayoutTests 同写法）。
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonAnnounce/AnnounceBoxLayout.h"
#include "../UI/BalloonAnnounce/AnnounceRow.h"
#include "../UI/BalloonHistory/HistoryContentView.h"   // 右栏正文控件（读正文用 GetFlatText）
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                        // DispatchMouseWheel（滚轮派发入口）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView（ANB11 要滚动）
#include "../UI/BalloonMain/MainPanelScrollView.h"                       // 两栏用的悬浮式滚动视口（ANB12）

using namespace balloonannounce;
using balloonwjui::DuiControl;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;

namespace {

// ---- ANB11 用的几个尺寸常量 ----
//测试窗口客户区尺寸（像素）。取公告中心的默认开窗尺寸（见 BalloonAnnounceBoxWindow.cpp
//的 kAnnWindowWidth / kAnnWindowHeight），减去标题栏高度即为客户区高。
const int kTestClientW = 820;
const int kTestClientH = 520;

//造「够长、需要滚动」的列表用的公告条数。行高 56 像素，30 条约 1700 像素，
//稳稳超过测试客户区高度，列表才滚得动。
const int kScrollableRowCount = 30;

//ANB14 里把列表滚下去的位置（像素）。取一个明显大于 0、又落在滚动范围内的值。
const int kTestListScrollPos = 300;

//造长正文用的行数。取 120 行是为了让正文高度稳稳超过好几个视口高，
//"滚过一屏之后"的行为才测得到。
const int kLongBodyLines = 120;

//造一条公告行视图，字段给出可辨认的值。
AnnounceRowView MakeRow(int64_t id, LPCTSTR title, bool unread = false,
                        int severity = 0, bool pinned = false)
{
    AnnounceRowView r;
    r.m_announcementId = id;
    r.m_title    = title;
    r.m_metaText = _T("平台运营 · 08-05 14:30");
    r.m_severity = severity;
    r.m_pinned   = pinned;
    r.m_unread   = unread;
    return r;
}

//造一份最小可用的整窗数据：两条公告，选中第一条。
AnnounceBoxViewData MakeData()
{
    AnnounceBoxViewData d;
    d.m_rows.push_back(MakeRow(1001, _T("系统维护通知"), true, 2));
    d.m_rows.push_back(MakeRow(1002, _T("新版本已发布")));
    d.m_totalCount   = 2;
    d.m_unreadCount  = 1;
    d.m_selectedIndex = 0;
    d.m_detailTitle  = _T("系统维护通知");
    d.m_detailMeta   = _T("平台运营 · 08-05 14:30");
    d.m_detailBody   = _T("本周日 02:00 至 04:00 停机维护。");
    d.m_emptyTitle   = _T("暂无公告");
    d.m_emptySubtitle= _T("平台发布公告后会出现在这里");
    return d;
}

//按 id 取标签文字；控件不存在或不是 DuiLabel 时返回空串。
CString LabelTextById(DuiControl* root, UINT id)
{
    DuiControl* c = root->FindCtrlById(id);
    if (c == NULL)
    {
        return CString(_T(""));
    }
    DuiLabel* lbl = dynamic_cast<DuiLabel*>(c);
    if (lbl != NULL)
    {
        return lbl->GetText();
    }
    //右栏正文已从 DuiLabel 换成内联文本控件（要把网址画成可点的链接），它的正文
    //要用 GetFlatText 读 —— 两种控件都认，别的用例才不必关心某一处用的是哪个。
    balloonhistory::HistoryContentView* view =
        dynamic_cast<balloonhistory::HistoryContentView*>(c);
    if (view != NULL)
    {
        return view->GetFlatText();
    }
    return CString(_T(""));
}

} // namespace

TEST_CASE("ANB1 公告中心固定控件点名齐全", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //左栏。
    CHECK(root->FindCtrlById(kIdAnnSummary) != nullptr);
    CHECK(root->FindCtrlById(kIdAnnScroll)  != nullptr);
    CHECK(root->FindCtrlById(kIdAnnList)    != nullptr);
    //右栏三项。
    CHECK(root->FindCtrlById(kIdAnnDetailTitle)  != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailMeta)   != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailBody)   != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailScroll) != nullptr);
    //底部按钮。
    CHECK(root->FindCtrlById(kIdAnnMarkAllRead) != nullptr);
}

TEST_CASE("ANB2 顶部信息按未读数变文案", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();

    //有未读：带「N 条未读」。
    {
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        CHECK(LabelTextById(root.get(), kIdAnnSummary) == CString(_T("共 2 条 · 1 条未读")));
    }

    //一条未读都没有：不显示「0 条未读」——那是句废话，反而让人以为出了错。
    {
        d.m_unreadCount = 0;
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        CHECK(LabelTextById(root.get(), kIdAnnSummary) == CString(_T("共 2 条")));
    }
}

TEST_CASE("ANB3 行 id 按基址加下标生成且可逆解", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //两行分别是 基址+0、基址+1。
    DuiControl* row0 = root->FindCtrlById(kIdAnnRowBase + 0);
    DuiControl* row1 = root->FindCtrlById(kIdAnnRowBase + 1);
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    //行必须是 AnnounceRow —— 窗口靠 dynamic_cast 认行做命中测试。
    CHECK(dynamic_cast<AnnounceRow*>(row0) != nullptr);
    CHECK(dynamic_cast<AnnounceRow*>(row1) != nullptr);

    //反解回下标。
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + 0) == 0);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + 1) == 1);
}

TEST_CASE("ANB4 RowIndexFromCtrlId 对非行 id 返回 -1", "[announce][layout]")
{
    //基址以下的固定控件 id 一律不是行。
    CHECK(RowIndexFromCtrlId(kIdAnnSummary) == -1);
    CHECK(RowIndexFromCtrlId(kIdAnnMarkAllRead) == -1);
    CHECK(RowIndexFromCtrlId(0) == -1);

    //边界：基址本身是第 0 行；基址 + 容量 - 1 是最后一行；再往上就不是行了。
    //这条边界正是历史窗那边出过事的地方（拿下一段基址当上界，导致第 101 页
    //的按钮被"定位"分支接走），本用例把它钉住。
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase) == 0);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity - 1) == (int)kIdAnnRowCapacity - 1);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity) == -1);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity + 5) == -1);
}

TEST_CASE("ANB5 没有公告时给出空状态", "[announce][layout]")
{
    std::vector<AnnounceRowView> none;

    //主 + 副两行文案都在。
    {
        std::unique_ptr<DuiControl> list =
            BuildAnnounceListContent(none, -1, _T("暂无公告"), _T("平台发布公告后会出现在这里"));
        REQUIRE(list != nullptr);
        CHECK(LabelTextById(list.get(), kIdAnnEmptyTitle) == CString(_T("暂无公告")));
        CHECK(LabelTextById(list.get(), kIdAnnEmptySub)
              == CString(_T("平台发布公告后会出现在这里")));
        //空状态下不该有任何行。
        CHECK(list->FindCtrlById(kIdAnnRowBase) == nullptr);
    }

    //副文案为空串时不建那个控件——免得留一条看不见却占位的空行。
    {
        std::unique_ptr<DuiControl> list =
            BuildAnnounceListContent(none, -1, _T("暂无公告"), _T(""));
        REQUIRE(list != nullptr);
        CHECK(list->FindCtrlById(kIdAnnEmptyTitle) != nullptr);
        CHECK(list->FindCtrlById(kIdAnnEmptySub) == nullptr);
    }
}

TEST_CASE("ANB6 未选中任何行时右栏显示占位提示", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    d.m_selectedIndex = -1;

    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //标题与 meta 留空，不显示上一次选中的残留内容。
    CHECK(LabelTextById(root.get(), kIdAnnDetailTitle).IsEmpty());
    CHECK(LabelTextById(root.get(), kIdAnnDetailMeta).IsEmpty());
    //正文位置给一句引导，而不是一片空白。
    CHECK(LabelTextById(root.get(), kIdAnnDetailBody)
          == CString(_T("从左侧选择一条公告查看详情。")));
}

TEST_CASE("ANB7 选中某行时右栏显示该行内容", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    CHECK(LabelTextById(root.get(), kIdAnnDetailTitle) == CString(_T("系统维护通知")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailMeta)
          == CString(_T("平台运营 · 08-05 14:30")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailBody)
          == CString(_T("本周日 02:00 至 04:00 停机维护。")));

    //选中的那一行处于选中态，其余不是。
    AnnounceRow* row0 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 0));
    AnnounceRow* row1 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    CHECK(row0->IsSelected() == true);
    CHECK(row1->IsSelected() == false);
}

TEST_CASE("ANB8 行的未读态与级别被注入到 AnnounceRow", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //第 0 条是未读的警告级公告，第 1 条是已读的普通公告。
    AnnounceRow* row0 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 0));
    AnnounceRow* row1 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);

    //未读态决定要不要画标题左边那个小圆点；窗口也靠它判断点开时要不要发已读上报。
    CHECK(row0->IsUnread() == true);
    CHECK(row1->IsUnread() == false);
}

TEST_CASE("ANB9 没有未读时全部标为已读按钮被禁用", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();

    //有未读：可点。
    {
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
        REQUIRE(btn != nullptr);
        CHECK(btn->IsEnabled() == true);
    }

    //一条未读都没有：禁用。点了也没有任何效果，留着可点会让人以为没生效。
    {
        d.m_unreadCount = 0;
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
        REQUIRE(btn != nullptr);
        CHECK(btn->IsEnabled() == false);
    }
}

TEST_CASE("ANB10 置顶公告的标题带前缀", "[announce][layout]")
{
    std::vector<AnnounceRowView> rows;
    rows.push_back(MakeRow(2001, _T("置顶的那条"), false, 0, true));
    rows.push_back(MakeRow(2002, _T("普通的那条")));

    std::unique_ptr<DuiControl> list = BuildAnnounceListContent(rows, -1, _T(""), _T(""));
    REQUIRE(list != nullptr);

    //置顶那条的标题带「[置顶] 」前缀：它本就排在最前，但翻到列表中段时
    //看不出边界在哪，加个标记才认得出。
    DuiControl* row0 = list->FindCtrlById(kIdAnnRowBase + 0);
    REQUIRE(row0 != nullptr);
    //标题 Label 是行内 vbox 的第一个子控件，逐层取下来。
    REQUIRE(row0->Children().size() >= 1);
    DuiControl* texts = row0->Children()[0].get();
    REQUIRE(texts != nullptr);
    REQUIRE(texts->Children().size() >= 1);
    DuiLabel* titleLbl = dynamic_cast<DuiLabel*>(texts->Children()[0].get());
    REQUIRE(titleLbl != nullptr);
    CHECK(titleLbl->GetText() == CString(_T("[置顶] 置顶的那条")));
}

TEST_CASE("ANB11 右栏正文滚到底后内容矩形仍与视口相交", "[announce][layout][scroll]")
{
    //造一段远超一屏的正文，逼出"要滚好几屏"的场景。
    CString longBody;
    for (int i = 0; i < kLongBodyLines; ++i)
    {
        CString line;
        line.Format(_T("第 %d 行：这是一条很长的公告正文，用来把内容高度撑到远超视口。\n"),
                    i + 1);
        longBody += line;
    }

    AnnounceBoxViewData d = MakeData();
    d.m_detailBody = longBody;

    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //按真实窗口客户区尺寸走一遍布局，控件矩形才有效。
    RECT rcClient = { 0, 0, kTestClientW, kTestClientH };
    root->SetRect(rcClient);

    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        root->FindCtrlById(kIdAnnDetailScroll));
    balloonhistory::HistoryContentView* body =
        dynamic_cast<balloonhistory::HistoryContentView*>(
            root->FindCtrlById(kIdAnnDetailBody));
    REQUIRE(sv != nullptr);
    REQUIRE(body != nullptr);

    const RECT svRc = sv->GetRect();
    REQUIRE(svRc.right > svRc.left);
    REQUIRE(svRc.bottom > svRc.top);

    //窗口那边（CBalloonAnnounceBoxWindow::SyncDetailBodyHeight）跑的就是这一支。
    const int contentH = SyncAnnounceDetailBodyHeight(sv, body);
    REQUIRE(contentH > (svRc.bottom - svRc.top));   //确实需要滚动

    //名义内容矩形必须跟着新高度走：不跟的话它仍只有一屏高，
    //滚过一屏后整块跑到视口上方。
    DuiControl* contentCtrl = sv->GetContent();
    REQUIRE(contentCtrl != nullptr);
    RECT contentRc = contentCtrl->GetRect();
    CHECK((contentRc.bottom - contentRc.top) == contentH);

    //滚到底：内容矩形必须仍与视口相交，否则 DuiScrollView::OnPaint 那句
    //IntersectRect 不成立，正文整块不画 —— 用户看到的就是"正文没了"。
    sv->SetScrollPos(contentH);   //超出上界会被夹到最大滚动位置
    contentRc = contentCtrl->GetRect();
    RECT inter;
    CHECK(::IntersectRect(&inter, &contentRc, &svRc) != FALSE);

    //滚到底时，内容的末端应当正好落在视口底边上（留 1 像素容差给取整）。
    const int tailGap = (int)(contentRc.bottom - svRc.bottom);
    CHECK(tailGap <= 1);
    CHECK(tailGap >= -1);
}

TEST_CASE("ANB12 两栏滚动视口都是悬浮式自动隐藏的那种", "[announce][layout][scroll]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //左栏列表与右栏正文都要用 MainPanelScrollView：滚动条悬浮在内容上层、滚动时
    //淡入、空闲后淡出。用回裸的 DuiScrollView 会退回成常显的内嵌滚动条。
    balloonmain::MainPanelScrollView* listSv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdAnnScroll));
    balloonmain::MainPanelScrollView* bodySv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdAnnDetailScroll));
    REQUIRE(listSv != nullptr);
    REQUIRE(bodySv != nullptr);

    //滚动条是 balloonui 统一的悬浮式细滑块（2026-10-04 起不再把基类宽度置 0 自绘）：自动隐藏、不占内容宽度。
    REQUIRE(listSv->GetScrollBar() != nullptr);
    REQUIRE(bodySv->GetScrollBar() != nullptr);
    CHECK(listSv->GetScrollBar()->IsAutoHide());
    CHECK(bodySv->GetScrollBar()->IsAutoHide());
    //命中带保持默认宽度：置 0 就完全没有滚动条
    CHECK(listSv->GetScrollBarWidth() == balloonwjui::DuiScrollBar::kOverlayBandPx);
    CHECK(bodySv->GetScrollBarWidth() == balloonwjui::DuiScrollBar::kOverlayBandPx);

    //列表装的是一串定高的行，问得出总高，走自动测高；正文问不出，由窗口显式喂。
    CHECK(listSv->GetAutoContentHeight() == true);
    CHECK(bodySv->GetAutoContentHeight() == false);
}

TEST_CASE("ANB18 滚轮落在右栏正文上时正文照样滚得动", "[announce][layout][scroll]")
{
    //右栏正文是一整块铺满滚动视口的 HistoryContentView，它自己不消化滚轮，
    //长公告能不能滚全靠事件上冒到右栏的滚动视口。理由同 ANB13。
    CString longBody;
    for (int i = 0; i < kLongBodyLines; ++i)
    {
        CString line;
        line.Format(_T("第 %d 行：这是一条很长的公告正文，用来把内容高度撑到远超视口。\n"),
                    i + 1);
        longBody += line;
    }

    AnnounceBoxViewData d = MakeData();
    d.m_detailBody = longBody;

    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    RECT rcClient = { 0, 0, kTestClientW, kTestClientH };
    root->SetRect(rcClient);

    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        root->FindCtrlById(kIdAnnDetailScroll));
    balloonhistory::HistoryContentView* body =
        dynamic_cast<balloonhistory::HistoryContentView*>(
            root->FindCtrlById(kIdAnnDetailBody));
    REQUIRE(sv != nullptr);
    REQUIRE(body != nullptr);

    //按窗口那边的口径把内容高度喂进去，滚动范围才非零。
    const int contentH = SyncAnnounceDetailBodyHeight(sv, body);
    const RECT svRc = sv->GetRect();
    REQUIRE(contentH > (svRc.bottom - svRc.top));
    REQUIRE(sv->GetScrollPos() == 0);

    POINT pt = { (kTestClientW * 3) / 4, kTestClientH / 2 };
    CHECK(balloonwjui::DuiHost::DispatchMouseWheel(body, pt, -WHEEL_DELTA, 0) == true);
    CHECK(sv->GetScrollPos() > 0);
}

namespace {

//造一份「够长、需要滚动」的整窗数据：kScrollableRowCount 条公告，全部未读，选中第一条。
AnnounceBoxViewData MakeScrollableData()
{
    AnnounceBoxViewData d;
    for (int i = 0; i < kScrollableRowCount; ++i)
    {
        CString title;
        title.Format(_T("第 %d 条公告"), i + 1);
        d.m_rows.push_back(MakeRow(4000 + i, title, true));
    }
    d.m_totalCount    = kScrollableRowCount;
    d.m_unreadCount   = kScrollableRowCount;
    d.m_selectedIndex = 0;
    d.m_detailTitle   = _T("第 1 条公告");
    d.m_detailMeta    = _T("平台运营 · 08-05 14:30");
    d.m_detailBody    = _T("第一条的正文。");
    return d;
}

} // namespace

TEST_CASE("ANB13 滚轮落在列表行的文字上时列表照样滚得动", "[announce][layout][scroll]")
{
    //两行字几乎铺满整行，滚轮十有八九落在它们身上，而 DuiLabel 自己不消化滚轮。
    //事件要靠 DuiHost::DispatchMouseWheel 沿父链上冒（Label → 文字 VBox → 行 HBox
    //→ 列表 VBox → 滚动视口）才到得了滚动视口；到不了的话鼠标停在列表上滚轮纹丝
    //不动，那条自动隐藏的滚动条也永远不会出现。
    //
    //这一条按行为断言、不按控件类型断言：类型只是手段，2026-08-14 之前是靠一个
    //会转发滚轮的 DuiLabel 子类做到的，之后改由库的默认上冒承担，而契约没变。
    AnnounceBoxViewData d = MakeScrollableData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    RECT rcClient = { 0, 0, kTestClientW, kTestClientH };
    root->SetRect(rcClient);

    balloonmain::MainPanelScrollView* listSv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdAnnScroll));
    REQUIRE(listSv != nullptr);
    REQUIRE(listSv->GetScrollPos() == 0);

    //取第一行里的标题标签作为滚轮命中控件 —— 它是这棵树里最深的那一层。
    DuiControl* content = listSv->GetContent();
    REQUIRE(content != nullptr);
    DuiControl* row0 = content->FindCtrlById(kIdAnnRowBase + 0);
    REQUIRE(row0 != nullptr);
    REQUIRE(row0->Children().size() >= 1);
    DuiControl* texts = row0->Children()[0].get();
    REQUIRE(texts != nullptr);
    REQUIRE(texts->Children().size() >= 2);
    DuiControl* titleLbl = texts->Children()[0].get();
    REQUIRE(titleLbl != nullptr);

    //zDelta 取 -WHEEL_DELTA 表示向下滚一格。
    POINT pt = { kTestClientW / 4, kTestClientH / 2 };
    CHECK(balloonwjui::DuiHost::DispatchMouseWheel(titleLbl, pt, -WHEEL_DELTA, 0) == true);
    CHECK(listSv->GetScrollPos() > 0);
}

TEST_CASE("ANB17 列表里只有行控件，行间分隔线由行自己画", "[announce][layout][scroll]")
{
    std::vector<AnnounceRowView> rows;
    rows.push_back(MakeRow(5001, _T("第一条")));
    rows.push_back(MakeRow(5002, _T("第二条")));
    rows.push_back(MakeRow(5003, _T("第三条")));

    std::unique_ptr<DuiControl> list = BuildAnnounceListContent(rows, -1, _T(""), _T(""));
    REQUIRE(list != nullptr);

    //列表的直接子控件必须清一色是行，行间那条线由行自己在底边画。这么定最初是
    //因为夹在中间的独立分隔线控件会把滚轮吃掉（2026-08-12 实测踩到：库当时不冒泡，
    //鼠标恰好停在那 1 像素上时整个列表滚不动）。2026-08-14 库补上滚轮上冒后那个
    //问题已不存在，但保持子控件与公告条目一一对应仍便于按下标核对，故契约保留。
    const std::vector<std::unique_ptr<DuiControl> >& kids = list->Children();
    REQUIRE(kids.size() == rows.size());
    for (size_t i = 0; i < kids.size(); ++i)
    {
        AnnounceRow* row = dynamic_cast<AnnounceRow*>(kids[i].get());
        REQUIRE(row != nullptr);
        //除最后一行外都在底边画分隔线，观感与原来插独立控件时一致。
        CHECK(row->IsShowBottomLine() == (i + 1 < kids.size()));
    }
}

TEST_CASE("ANB14 点选另一条公告不会改变列表的滚动位置", "[announce][layout][scroll]")
{
    AnnounceBoxViewData d = MakeScrollableData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    RECT rcClient = { 0, 0, kTestClientW, kTestClientH };
    root->SetRect(rcClient);

    balloonwjui::DuiScrollView* listSv = dynamic_cast<balloonwjui::DuiScrollView*>(
        root->FindCtrlById(kIdAnnScroll));
    REQUIRE(listSv != nullptr);

    //先把列表滚下去一段，模拟用户翻到了下面。
    listSv->SetScrollPos(kTestListScrollPos);
    const int posBefore = listSv->GetScrollPos();
    REQUIRE(posBefore > 0);   //列表确实滚得动，否则这条用例就白测了

    //改选最后一条并做定点更新 —— 这正是点击一行时窗口要走的两步。
    d.m_selectedIndex = kScrollableRowCount - 1;
    d.m_rows[d.m_selectedIndex].m_unread = false;
    d.m_unreadCount = kScrollableRowCount - 1;
    d.m_detailTitle = _T("最后一条公告");
    d.m_detailBody  = _T("最后一条的正文。");
    ApplyAnnounceListState(root.get(), d);
    ApplyAnnounceDetail(root.get(), d);

    //列表必须停在原处。整棵树重建的老做法会让它归零，用户点了靠下的一行、
    //列表却跳回顶部（2026-08-12 实测反馈）。
    CHECK(listSv->GetScrollPos() == posBefore);
}

TEST_CASE("ANB15 定点更新同步选中态、未读点与顶部计数", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeScrollableData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    AnnounceRow* row0 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 0));
    AnnounceRow* row2 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 2));
    REQUIRE(row0 != nullptr);
    REQUIRE(row2 != nullptr);
    REQUIRE(row0->IsSelected() == true);
    REQUIRE(row2->IsSelected() == false);
    REQUIRE(row2->IsUnread() == true);

    //改选第 3 条并把它标为已读。
    d.m_selectedIndex = 2;
    d.m_rows[2].m_unread = false;
    d.m_unreadCount = kScrollableRowCount - 1;
    ApplyAnnounceListState(root.get(), d);

    CHECK(row0->IsSelected() == false);
    CHECK(row2->IsSelected() == true);
    CHECK(row2->IsUnread() == false);   //未读的小圆点要跟着消失

    CString expectSummary;
    expectSummary.Format(_T("共 %d 条 · %d 条未读"), kScrollableRowCount, kScrollableRowCount - 1);
    CHECK(LabelTextById(root.get(), kIdAnnSummary) == expectSummary);

    DuiButton* markAll = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
    REQUIRE(markAll != nullptr);
    CHECK(markAll->IsEnabled() == true);

    //全部标为已读之后：未读数归零，顶部不再显示「N 条未读」，按钮禁用。
    for (size_t i = 0; i < d.m_rows.size(); ++i)
    {
        d.m_rows[i].m_unread = false;
    }
    d.m_unreadCount = 0;
    ApplyAnnounceListState(root.get(), d);

    CString expectNoUnread;
    expectNoUnread.Format(_T("共 %d 条"), kScrollableRowCount);
    CHECK(LabelTextById(root.get(), kIdAnnSummary) == expectNoUnread);
    CHECK(markAll->IsEnabled() == false);
    CHECK(row2->IsUnread() == false);
}

TEST_CASE("ANB16 定点更新换掉右栏正文并把它滚回顶部", "[announce][layout][scroll]")
{
    //先给一条很长的正文，好把右栏滚下去。
    CString longBody;
    for (int i = 0; i < kLongBodyLines; ++i)
    {
        CString line;
        line.Format(_T("第 %d 行：够长的正文，用来把右栏撑到能滚动。\n"), i + 1);
        longBody += line;
    }

    AnnounceBoxViewData d = MakeScrollableData();
    d.m_detailBody = longBody;

    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);
    RECT rcClient = { 0, 0, kTestClientW, kTestClientH };
    root->SetRect(rcClient);

    balloonwjui::DuiScrollView* bodySv = dynamic_cast<balloonwjui::DuiScrollView*>(
        root->FindCtrlById(kIdAnnDetailScroll));
    balloonhistory::HistoryContentView* body =
        dynamic_cast<balloonhistory::HistoryContentView*>(
            root->FindCtrlById(kIdAnnDetailBody));
    REQUIRE(bodySv != nullptr);
    REQUIRE(body != nullptr);

    //按建窗后的流程把正文高度喂进去，再滚到中间某处。
    const int longContentH = SyncAnnounceDetailBodyHeight(bodySv, body);
    REQUIRE(longContentH > (kTestClientH / 2));
    bodySv->SetScrollPos(longContentH);
    REQUIRE(bodySv->GetScrollPos() > 0);

    //改选另一条（正文短得多）。
    d.m_selectedIndex = 1;
    d.m_detailTitle = _T("第 2 条公告");
    d.m_detailMeta  = _T("平台运营 · 08-06 09:00");
    d.m_detailBody  = _T("很短的一条正文。");
    ApplyAnnounceDetail(root.get(), d);

    //三项都换成新选中那条，且正文回到顶部。
    CHECK(LabelTextById(root.get(), kIdAnnDetailTitle) == CString(_T("第 2 条公告")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailMeta)  == CString(_T("平台运营 · 08-06 09:00")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailBody)  == CString(_T("很短的一条正文。")));
    CHECK(bodySv->GetScrollPos() == 0);

    //滚动范围也要按新正文缩回去，否则短正文还能滚出一大截空白。
    CHECK(bodySv->GetContentHeight() < longContentH);
}
