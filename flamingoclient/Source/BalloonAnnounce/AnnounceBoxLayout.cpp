/**
 *  「消息盒子」窗口——balloonui 客户区布局实现，AnnounceBoxLayout.cpp
 *
 *  程序化搭建：左栏（顶部信息 + 公告列表）、右栏（标题 + 发布者·时间 + 正文）、
 *  底部（全部标为已读）。除列表行用 AnnounceRow（DuiHBox 子类，补悬停 / 选中态与
 *  未读点）外，一律用 balloonui 现成控件组合，不自绘。
 *
 *  两栏的滚动视口用 balloonmain::MainPanelScrollView（DuiScrollView 子类）：滚动条
 *  悬浮在内容上层、不占内容宽度，滚动时淡入、停下约 800 毫秒后淡出，滑块可拖动。
 *  它的淡入淡出要靠宿主窗口的 WM_TIMER 推进，见 CBalloonAnnounceBoxWindow。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，工程里标
 *  PrecompiledHeader = NotUsing（与 HistoryWindowLayout.cpp 同约定）。
 *
 *  balloonwj@qq.com   2026-08-05
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#include <time.h>

#include "AnnounceBoxLayout.h"
#include "AnnounceRow.h"
//左右两栏的滚动视口都用主面板那套「悬浮式、滚动时淡入、空闲后淡出」的滚动视口，
//观感与主面板三个列表一致，也免得再写一份一次性的滚动条（开发约定文档「UI 约定」第 1、2 条）。
#include "../BalloonMain/MainPanelScrollView.h"

#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../balloonui/balloonui/Controls/Window/DuiScrollBar.h"

namespace balloonannounce {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiSeparator;

namespace {

typedef DuiLayout::Hint Hint;

// ---- 配色（COLORREF）----
const COLORREF kColorWhite      = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorListBg     = RGB(0xFA, 0xF9, 0xF7);   // 左栏底色，与右栏白底区分开
const COLORREF kColorTitle      = RGB(0x22, 0x22, 0x22);   // 行标题 / 详情标题
const COLORREF kColorTitleUnread= RGB(0x11, 0x11, 0x11);   // 未读行标题（更深一档，配合圆点）
const COLORREF kColorMeta       = RGB(0x9A, 0x98, 0x93);   // 发布者·时间 / 顶部信息
const COLORREF kColorBody       = RGB(0x44, 0x44, 0x44);   // 正文
const COLORREF kColorSepLine    = RGB(0xEC, 0xEA, 0xE6);   // 分隔线
const COLORREF kColorEmptyTitle = RGB(0x6B, 0x6B, 0x6B);   // 空状态主文案
const COLORREF kColorEmptySub   = RGB(0xA8, 0xA6, 0xA2);   // 空状态副文案

// ---- 尺寸（像素）----
const int kListPaneW   = 268;  // 左栏宽（够放两行文字，又不至于挤掉右栏正文）
const int kSummaryH    = 34;   // 左栏顶部信息行高
const int kRowH        = 56;   // 列表行高（标题行 + 发布者·时间行 + 上下内边距）
const int kRowTitleH   = 20;   // 行内标题行高
const int kRowMetaH    = 16;   // 行内「发布者·时间」行高
const int kRowPadLeft  = 16;   // 行左内边距：给未读圆点让出槽位（圆点画在 x≈6 处）
const int kRowPadRight = 10;   // 行右内边距
const int kRowPadV     = 8;    // 行上下内边距
const int kPadH        = 16;   // 各区块的水平内边距
const int kDetailTitleH= 28;   // 详情标题行高
const int kDetailMetaH = 20;   // 详情「发布者·时间」行高
const int kFooterH     = 52;   // 底部行高
const int kMarkAllW    = 116;  // 「全部标为已读」按钮宽
const int kEmptyTitleH = 24;   // 空状态主文案行高
const int kEmptySubH   = 20;   // 空状态副文案行高
const int kBodyPadV    = 10;   // 正文区上下内边距
// 正文区右内边距：滚动条是悬浮式的，压在内容上层、不占布局宽度，故正文自己让出
// 一条，免得滑块淡入时盖住行尾的字。取值与列表行的 kRowPadRight 一致。
// 窗口那边算正文折行高度时要减去同一个值（见 CBalloonAnnounceBoxWindow 的
// kAnnBodyPadRight），两处必须同步改。
const int kBodyPadRight = 10;

// 造一个文本标签。
std::unique_ptr<DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD dtAlign, UINT ctrlId = 0)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(dtAlign);
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    return std::unique_ptr<DuiControl>(lbl.release());
}

// 造一个会把滚轮上抛给祖先滚动视口的文本标签。
//
// 列表行里的文字必须用这一种：DuiHost 只把滚轮投给<u>最深命中控件</u>，返回 false
// 就丢掉、不向上冒泡（见 DuiHost::OnMouseWheel）。行里的两行字几乎铺满整行，滚轮
// 十有八九落在它们身上，用裸 DuiLabel 的话鼠标停在列表上滚轮根本不动。
std::unique_ptr<DuiControl> MakeWheelLabel(LPCTSTR text, COLORREF color, DWORD dtAlign)
{
    std::unique_ptr<AnnounceWheelLabel> lbl(new AnnounceWheelLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(dtAlign);
    return std::unique_ptr<DuiControl>(lbl.release());
}

// 右栏没选中任何一条公告时，正文位置显示的占位提示。
// 建树与定点更新两条路都用它，免得两处文案对不上。
const TCHAR* const kDetailPlaceholder = _T("从左侧选择一条公告查看详情。");

// 拼左栏顶部那行「共 N 条 · N 条未读」。同样是建树与定点更新共用。
CString MakeSummaryText(const AnnounceBoxViewData& data)
{
    CString summary;
    if (data.m_unreadCount > 0)
    {
        summary.Format(_T("共 %d 条 · %d 条未读"), data.m_totalCount, data.m_unreadCount);
    }
    else
    {
        //一条未读都没有时不显示「0 条未读」——那是句废话，反而让人以为出了错。
        summary.Format(_T("共 %d 条"), data.m_totalCount);
    }
    return summary;
}

// 造一条横向分隔线。
std::unique_ptr<DuiControl> MakeSeparator()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kColorSepLine);
    return std::unique_ptr<DuiControl>(sep.release());
}

// 造一条纵向分隔线（左右栏之间那道）。
std::unique_ptr<DuiControl> MakeVSeparator()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Vertical);
    sep->SetColor(kColorSepLine);
    return std::unique_ptr<DuiControl>(sep.release());
}

//造列表里的一行：AnnounceRow 容器 + 内含「标题」「发布者·时间」两行 Label。
//未读圆点、级别色条与行底的分隔线都由 AnnounceRow 自己在 OnPaint 里画（见其头注释）。
//  showBottomLine：行底要不要画分隔线，最后一行传 false。
std::unique_ptr<DuiControl> MakeRow(const AnnounceRowView& row, int index, bool selected,
                                    bool showBottomLine)
{
    std::unique_ptr<AnnounceRow> box(new AnnounceRow());
    box->SetCtrlId(kIdAnnRowBase + (UINT)index);
    box->SetPadding(kRowPadLeft, kRowPadV, kRowPadRight, kRowPadV);
    box->SetGap(0);
    box->SetUnread(row.m_unread);
    box->SetSeverity(row.m_severity);
    box->SetSelected(selected);
    box->SetShowBottomLine(showBottomLine);

    //两行文字竖排。整行只有这一个子控件，让它占满剩余宽度。
    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    texts->SetGap(2);

    //置顶的公告在标题前加一个标记，让它在一列里能与普通公告区分开
    //（置顶本来就排在最前，但翻到列表中段时看不出边界在哪）。
    CString titleText = row.m_pinned ? (CString(_T("[置顶] ")) + row.m_title) : row.m_title;
    texts->AddChild(MakeWheelLabel(titleText,
                                   row.m_unread ? kColorTitleUnread : kColorTitle,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                    Hint().Fixed(kRowTitleH));
    texts->AddChild(MakeWheelLabel(row.m_metaText, kColorMeta,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                    Hint().Fixed(kRowMetaH));

    box->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(box.release());
}

} // namespace

std::unique_ptr<DuiControl> BuildAnnounceListContent(const std::vector<AnnounceRowView>& rows,
                                                     int selectedIndex,
                                                     LPCTSTR emptyTitle,
                                                     LPCTSTR emptySubtitle)
{
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetCtrlId(kIdAnnList);
    list->SetPadding(0);
    list->SetGap(0);

    //一条公告都没有：给一块居中的空状态，而不是一片白。
    if (rows.empty())
    {
        std::unique_ptr<DuiVBox> empty(new DuiVBox());
        empty->SetPadding(kPadH, 40, kPadH, 0);
        empty->SetGap(6);
        empty->AddChild(MakeLabel(emptyTitle != NULL ? emptyTitle : _T(""),
                                  kColorEmptyTitle,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                  kIdAnnEmptyTitle),
                        Hint().Fixed(kEmptyTitleH));
        //副文案为空串时不建那个控件 —— 免得留一条看不见却占位的空行。
        if (emptySubtitle != NULL && emptySubtitle[0] != _T('\0'))
        {
            empty->AddChild(MakeLabel(emptySubtitle, kColorEmptySub,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                      kIdAnnEmptySub),
                            Hint().Fixed(kEmptySubH));
        }
        list->AddChild(std::move(empty), Hint().Fixed(kEmptyTitleH + kEmptySubH + 46));
        return std::unique_ptr<DuiControl>(list.release());
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const bool selected = ((int)i == selectedIndex);
        //行之间那条淡分隔线由行自己在底边画，最后一行不画 —— 独立的分隔线控件会把
        //滚轮吃掉（原委见 AnnounceRow::SetShowBottomLine 的注释），所以列表里只放行控件。
        const bool showBottomLine = (i + 1 < rows.size());
        list->AddChild(MakeRow(rows[i], (int)i, selected, showBottomLine), Hint().Fixed(kRowH));
    }

    return std::unique_ptr<DuiControl>(list.release());
}

std::unique_ptr<DuiControl> BuildAnnounceBoxBody(const AnnounceBoxViewData& data)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // ============ 主体：左右双栏 ============
    std::unique_ptr<DuiHBox> main(new DuiHBox());
    main->SetPadding(0);
    main->SetGap(0);

    // ---- 左栏：顶部信息 + 公告列表 ----
    std::unique_ptr<DuiVBox> leftPane(new DuiVBox());
    leftPane->SetBgColor(kColorListBg);
    leftPane->SetPadding(0);
    leftPane->SetGap(0);

    CString summary = MakeSummaryText(data);
    std::unique_ptr<DuiVBox> summaryBox(new DuiVBox());
    summaryBox->SetPadding(kPadH, 8, kPadH, 4);
    summaryBox->AddChild(MakeLabel(summary, kColorMeta,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdAnnSummary),
                         Hint().Weight(1));
    leftPane->AddChild(std::move(summaryBox), Hint().Fixed(kSummaryH));
    leftPane->AddChild(MakeSeparator(), Hint().Fixed(1));

    //列表的内容是一串定高的行，DuiVBox::GetDesiredSize 问得出总高，故这里开自动测高
    //（MainPanelScrollView 的构造函数默认关掉它，主面板那三个面板装的是 DuiTreeView，
    //问不出高度，与这里的情形不同）。
    std::unique_ptr<balloonmain::MainPanelScrollView> listScroll(
        new balloonmain::MainPanelScrollView());
    listScroll->SetCtrlId(kIdAnnScroll);
    listScroll->SetAutoContentHeight(true);
    listScroll->SetContent(BuildAnnounceListContent(data.m_rows, data.m_selectedIndex,
                                                    data.m_emptyTitle, data.m_emptySubtitle));
    leftPane->AddChild(std::unique_ptr<DuiControl>(listScroll.release()), Hint().Weight(1));

    main->AddChild(std::move(leftPane), Hint().Fixed(kListPaneW));
    main->AddChild(MakeVSeparator(), Hint().Fixed(1));

    // ---- 右栏：标题 + 发布者·时间 + 正文 ----
    std::unique_ptr<DuiVBox> rightPane(new DuiVBox());
    rightPane->SetBgColor(kColorWhite);
    rightPane->SetPadding(kPadH, 12, kPadH, 0);
    rightPane->SetGap(0);

    //没选中任何一条时，标题与 meta 留空、正文位置给一句占位提示。
    const bool hasSelection = (data.m_selectedIndex >= 0);

    rightPane->AddChild(MakeLabel(hasSelection ? data.m_detailTitle : CString(_T("")),
                                  kColorTitle,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                  kIdAnnDetailTitle),
                        Hint().Fixed(kDetailTitleH));
    rightPane->AddChild(MakeLabel(hasSelection ? data.m_detailMeta : CString(_T("")),
                                  kColorMeta,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                                  kIdAnnDetailMeta),
                        Hint().Fixed(kDetailMetaH));
    rightPane->AddChild(MakeSeparator(), Hint().Fixed(1));

    //正文放进滚动视口：公告可能很长，不能指望一屏放得下。
    std::unique_ptr<balloonmain::MainPanelScrollView> bodyScroll(
        new balloonmain::MainPanelScrollView());
    bodyScroll->SetCtrlId(kIdAnnDetailScroll);
    //正文区<u>不</u>用 auto-content-height：内容是一个 Weight(1) 的多行文本控件，
    //自动模式问不出它折行后的真实高度。改由窗口按实际宽度算好喂进来
    //（与主面板 SyncPaneContentHeight 同套路）。MainPanelScrollView 的构造函数
    //已经把它置为 false，这里不必再调。

    std::unique_ptr<DuiVBox> bodyBox(new DuiVBox());
    bodyBox->SetPadding(0, kBodyPadV, kBodyPadRight, kBodyPadV);

    //正文用会转发滚轮的内联文本控件（裸控件会把滚轮吃掉，长公告滚不动）。
    //它取代了原先的 DuiLabel —— 换的理由是要把正文里的网址渲染成可点的链接，
    //DuiLabel 没有字符级排版模型，做不到；折行由该控件自己按可用宽度完成，
    //不再需要 DuiLabel 那个"必须显式 SetWordWrap(true)"的开关。
    std::unique_ptr<AnnounceWheelContentView> bodyLbl(new AnnounceWheelContentView());
    //公告是服务端原样存的纯文本，走纯文本入口（不解析聊天消息那套 '/' 转义）。
    bodyLbl->SetPlainContent(hasSelection ? data.m_detailBody : CString(kDetailPlaceholder));
    bodyLbl->SetTextColor(hasSelection ? kColorBody : kColorEmptySub);
    //正文一律画全，不要消息记录窗那套"折叠成 5 行"。
    bodyLbl->SetExpanded(true);
    bodyLbl->SetCtrlId(kIdAnnDetailBody);
    //高度给 Weight(1) 撑满视口即可；真正决定"能不能滚"的是滚动视口的内容高度，
    //由窗口在布局完成后按实际宽度调 MeasureHeight 算出来喂给 SetContentHeight
    //（见 CBalloonAnnounceBoxWindow::SyncDetailBodyHeight）。
    bodyBox->AddChild(std::unique_ptr<DuiControl>(bodyLbl.release()), Hint().Weight(1));

    bodyScroll->SetContent(std::unique_ptr<DuiControl>(bodyBox.release()));
    rightPane->AddChild(std::unique_ptr<DuiControl>(bodyScroll.release()), Hint().Weight(1));

    main->AddChild(std::move(rightPane), Hint().Weight(1));

    root->AddChild(std::move(main), Hint().Weight(1));
    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    // ============ 底部：右对齐的「全部标为已读」 ============
    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetPadding(kPadH, 10, kPadH, 10);
    footer->SetGap(8);
    //左侧空白把按钮顶到右边。
    footer->AddChild(MakeLabel(_T(""), kColorMeta, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                     Hint().Weight(1));

    std::unique_ptr<DuiButton> markAll(new DuiButton());
    markAll->SetButtonType(DuiButton::StylePushButton);
    markAll->SetText(_T("全部标为已读"));
    markAll->SetCtrlId(kIdAnnMarkAllRead);
    //一条未读都没有时按钮禁用 —— 点了也没有任何效果，留着可点会让人以为没生效。
    markAll->SetEnabled(data.m_unreadCount > 0);
    footer->AddChild(std::unique_ptr<DuiControl>(markAll.release()), Hint().Fixed(kMarkAllW));

    root->AddChild(std::move(footer), Hint().Fixed(kFooterH));

    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyAnnounceListState(DuiControl* root, const AnnounceBoxViewData& data)
{
    if (root == NULL)
    {
        return;
    }

    // ---- 每一行的选中态与未读点 ----
    for (size_t i = 0; i < data.m_rows.size(); ++i)
    {
        AnnounceRow* row = dynamic_cast<AnnounceRow*>(
            root->FindCtrlById(kIdAnnRowBase + (UINT)i));
        if (row == NULL)
        {
            //控件树与数据对不上（比如行集合刚变过还没重建），这一行跳过即可。
            continue;
        }

        const bool selected = ((int)i == data.m_selectedIndex);
        const bool unread   = data.m_rows[i].m_unread;
        //先记下要不要重画：SetSelected 只在值真的变了时自己重画，而 SetUnread
        //按约定只改内存状态、不重画，两者合起来判一次最省事。
        const bool needRepaint = (row->IsSelected() != selected) || (row->IsUnread() != unread);
        row->SetSelected(selected);
        row->SetUnread(unread);
        if (needRepaint)
        {
            row->Invalidate();
        }
    }

    // ---- 左栏顶部「共 N 条 · N 条未读」----
    DuiLabel* summary = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdAnnSummary));
    if (summary != NULL)
    {
        summary->SetText(MakeSummaryText(data));
    }

    // ---- 底部「全部标为已读」：一条未读都没有时禁用 ----
    DuiButton* markAll = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
    if (markAll != NULL)
    {
        markAll->SetEnabled(data.m_unreadCount > 0);
    }
}

void ApplyAnnounceDetail(DuiControl* root, const AnnounceBoxViewData& data)
{
    if (root == NULL)
    {
        return;
    }

    //没选中任何一条时，标题与 meta 留空、正文位置给一句占位提示（与建树时同一口径）。
    const bool hasSelection = (data.m_selectedIndex >= 0);

    DuiLabel* title = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdAnnDetailTitle));
    if (title != NULL)
    {
        title->SetText(hasSelection ? data.m_detailTitle : CString(_T("")));
    }

    DuiLabel* meta = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdAnnDetailMeta));
    if (meta != NULL)
    {
        meta->SetText(hasSelection ? data.m_detailMeta : CString(_T("")));
    }

    balloonhistory::HistoryContentView* body =
        dynamic_cast<balloonhistory::HistoryContentView*>(root->FindCtrlById(kIdAnnDetailBody));
    if (body != NULL)
    {
        //公告是服务端原样存的纯文本，走纯文本入口（不解析聊天消息那套 '/' 转义）。
        body->SetPlainContent(hasSelection ? data.m_detailBody : CString(kDetailPlaceholder));
        body->SetTextColor(hasSelection ? kColorBody : kColorEmptySub);
    }

    balloonwjui::DuiScrollView* scroll =
        dynamic_cast<balloonwjui::DuiScrollView*>(root->FindCtrlById(kIdAnnDetailScroll));
    //换了正文就得按新内容重算滚动范围，并把正文滚回顶部 —— 上一条看到一半的位置
    //对新的这条没有任何意义，留在那儿只会让人以为开头的内容丢了。
    SyncAnnounceDetailBodyHeight(scroll, body);
    if (scroll != NULL)
    {
        scroll->SetScrollPos(0);
    }
}

int SyncAnnounceDetailBodyHeight(balloonwjui::DuiScrollView* scroll,
                                 balloonhistory::HistoryContentView* body)
{
    if (scroll == NULL || body == NULL)
    {
        return 0;
    }

    const RECT svRc = scroll->GetRect();
    if (svRc.right <= svRc.left || svRc.bottom <= svRc.top)
    {
        //布局还没落定（视口矩形为空），等下一次调用。
        return 0;
    }

    //可用宽度取视口宽再扣掉正文区的右内边距，否则算出来的高度会偏小（文字实际能用的
    //宽度比视口窄），长公告的末尾会被截掉一点。
    const int width = svRc.right - svRc.left - kBodyPadRight;
    if (width <= 0)
    {
        return 0;
    }

    //MeasureHeight 按当前字体算出折行后的高度；再加上正文外层 vbox 的上下内边距，
    //即为视口的内容总高。
    const int contentH = body->MeasureHeight(width) + kBodyPadV + kBodyPadV;
    scroll->SetContentHeight(contentH);

    //关键的一步：让视口按新的内容高重排，把名义内容矩形顶到该有的高度（原委见
    //本函数在头文件里的注释）。回归用例见 Tests/AnnounceBoxLayoutTests.cpp 的 ANB11。
    scroll->ForceLayout(svRc);
    return contentH;
}

CString FormatAnnounceTime(int64_t timestampMs)
{
    if (timestampMs <= 0)
    {
        return CString(_T(""));
    }

    //毫秒 → 秒的换算基数。
    const int64_t MS_PER_SECOND = 1000;
    time_t secs = (time_t)(timestampMs / MS_PER_SECOND);

    struct tm t;
    if (localtime_s(&t, &secs) != 0)
    {
        return CString(_T(""));
    }

    time_t nowSecs = time(NULL);
    struct tm now;
    const bool nowOk = (localtime_s(&now, &nowSecs) == 0);

    CString text;
    if (nowOk && now.tm_year == t.tm_year)
    {
        text.Format(_T("%02d-%02d %02d:%02d"),
                    t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }
    else
    {
        //跨年之后光看月日分不清是哪一年的，带上年份。
        text.Format(_T("%d-%02d-%02d %02d:%02d"),
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }
    return text;
}

int RowIndexFromCtrlId(UINT ctrlId)
{
    //判定必须按「基址 <= id < 基址 + 本段容量」，不得拿下一段的基址当上界。
    if (ctrlId < kIdAnnRowBase || ctrlId >= kIdAnnRowBase + kIdAnnRowCapacity)
    {
        return -1;
    }
    return (int)(ctrlId - kIdAnnRowBase);
}

} // namespace balloonannounce
