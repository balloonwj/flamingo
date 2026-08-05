/**
 *  「消息盒子」窗口——balloonui 客户区布局实现，AnnounceBoxLayout.cpp
 *
 *  程序化搭建：左栏（顶部信息 + 公告列表）、右栏（标题 + 发布者·时间 + 正文）、
 *  底部（全部标为已读）。除列表行用 AnnounceRow（DuiHBox 子类，补悬停 / 选中态与
 *  未读点）外，一律用 balloonui 现成控件组合，不自绘。
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
using balloonwjui::DuiScrollView;

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
const int kScrollBarW  = 10;   // 滚动条宽

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
//未读圆点与级别色条由 AnnounceRow 自己在 OnPaint 里画（见其头注释）。
std::unique_ptr<DuiControl> MakeRow(const AnnounceRowView& row, int index, bool selected)
{
    std::unique_ptr<AnnounceRow> box(new AnnounceRow());
    box->SetCtrlId(kIdAnnRowBase + (UINT)index);
    box->SetPadding(kRowPadLeft, kRowPadV, kRowPadRight, kRowPadV);
    box->SetGap(0);
    box->SetUnread(row.m_unread);
    box->SetSeverity(row.m_severity);
    box->SetSelected(selected);

    //两行文字竖排。整行只有这一个子控件，让它占满剩余宽度。
    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    texts->SetGap(2);

    //置顶的公告在标题前加一个标记，让它在一列里能与普通公告区分开
    //（置顶本来就排在最前，但翻到列表中段时看不出边界在哪）。
    CString titleText = row.m_pinned ? (CString(_T("[置顶] ")) + row.m_title) : row.m_title;
    texts->AddChild(MakeLabel(titleText,
                              row.m_unread ? kColorTitleUnread : kColorTitle,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                    Hint().Fixed(kRowTitleH));
    texts->AddChild(MakeLabel(row.m_metaText, kColorMeta,
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
        list->AddChild(MakeRow(rows[i], (int)i, selected), Hint().Fixed(kRowH));
        //行之间一条淡分隔线，最后一行之后不加。
        if (i + 1 < rows.size())
        {
            list->AddChild(MakeSeparator(), Hint().Fixed(1));
        }
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
    std::unique_ptr<DuiVBox> summaryBox(new DuiVBox());
    summaryBox->SetPadding(kPadH, 8, kPadH, 4);
    summaryBox->AddChild(MakeLabel(summary, kColorMeta,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdAnnSummary),
                         Hint().Weight(1));
    leftPane->AddChild(std::move(summaryBox), Hint().Fixed(kSummaryH));
    leftPane->AddChild(MakeSeparator(), Hint().Fixed(1));

    std::unique_ptr<DuiScrollView> listScroll(new DuiScrollView());
    listScroll->SetCtrlId(kIdAnnScroll);
    listScroll->SetAutoContentHeight(true);
    listScroll->SetScrollBarWidth(kScrollBarW);
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
    std::unique_ptr<DuiScrollView> bodyScroll(new DuiScrollView());
    bodyScroll->SetCtrlId(kIdAnnDetailScroll);
    //正文区<u>不</u>用 auto-content-height：内容是一个 Weight(1) 的多行 Label，
    //自动模式问不出它折行后的真实高度。改由窗口按实际宽度算好喂进来
    //（与主面板 SyncPaneContentHeight 同套路）。
    bodyScroll->SetAutoContentHeight(false);
    bodyScroll->SetScrollBarWidth(kScrollBarW);

    std::unique_ptr<DuiVBox> bodyBox(new DuiVBox());
    bodyBox->SetPadding(0, 10, 0, 10);

    //正文用会转发滚轮的 Label（裸 DuiLabel 会把滚轮吃掉，长公告滚不动）。
    //<u>必须调 SetWordWrap(true)</u>：DuiLabel 的 OnPaint / MeasureHeight 看的是
    //m_wordWrap 这个标志，只在 SetTextAlign 里给 DT_WORDBREAK 是<u>不生效</u>的
    //——之前正文只显示得出第一行、看着像"只有标题没有内容"，就是漏了这一句。
    std::unique_ptr<AnnounceWheelLabel> bodyLbl(new AnnounceWheelLabel());
    bodyLbl->SetText(hasSelection ? data.m_detailBody
                                  : CString(_T("从左侧选择一条公告查看详情。")));
    bodyLbl->SetTextColor(hasSelection ? kColorBody : kColorEmptySub);
    bodyLbl->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    bodyLbl->SetWordWrap(true);
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
