/**
 *  CodeNotesPane.cpp —— 代码窗口右侧的批注栏与左右分栏，见 CodeNotesPane.h（docs/代码片段方案.md 第 10.5 节）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用（批注栏的
 *  排版单测）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 CodeComposeLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
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

#include "CodeNotesPane.h"
#include "../../Common/I18n/I18n.h"         // TR / TRF：界面文字的多语言查找
#include "../../Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：按钮按译文加宽
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

using namespace balloonwjui;

namespace ballooncode {

namespace {

// ---- 颜色 ----
const COLORREF kPaneBg          = RGB(0xFA, 0xFA, 0xF7);   //批注栏底色
const COLORREF kHeaderText      = RGB(0x55, 0x55, 0x55);   //顶部「批注 (N)」
const COLORREF kGrayText        = RGB(0x8A, 0x8A, 0x8A);   //没有批注时的提示、编辑卡片顶部的行范围
const COLORREF kNoteText        = RGB(0x33, 0x33, 0x33);   //说明文字
const COLORREF kLinkColor       = RGB(0x2F, 0x6F, 0xD6);   //链接（行范围、修改、删除、收起）
const COLORREF kLinkHoverColor  = RGB(0x1A, 0x4F, 0xA8);   //链接悬停
const COLORREF kCardBg          = RGB(0xFF, 0xFB, 0xEC);   //卡片底色（与被批注行的浅黄同色系）
const COLORREF kCardBorder      = RGB(0xF0, 0xE2, 0xB6);   //卡片边框
const COLORREF kCardHighlightBg = RGB(0xFF, 0xF1, 0xC9);   //高亮卡片的底色
const COLORREF kCardHighlightBorder = RGB(0xF0, 0x9A, 0x3E);   //高亮卡片的边框（与行号栏竖条同色）
const COLORREF kNewMarkText     = RGB(0xD9, 0x6A, 0x12);   //「新」标记的文字（比行号栏竖条略深，在浅黄卡片上看得清）

// ---- 版式（逻辑像素）----
const int kPanePadH      = 8;     //批注栏左右内边距
const int kPanePadV      = 6;     //批注栏上下内边距
const int kPaneGap       = 6;     //顶部一行与卡片列表之间的间距
const int kHeaderH       = 24;    //顶部一行高
const int kCollapseW     = 24;    //「»」宽
const int kListGap       = 8;     //卡片之间的间距
const int kCardPad       = 8;     //卡片内边距
const int kCardBorderW   = 1;     //卡片边框粗细
const int kCardRadius    = 4;     //卡片圆角半径
const int kCardInnerGap  = 4;     //卡片里各行之间的间距
const int kCardHeaderH   = 20;    //卡片顶部一行（行范围与「修改」「删除」）高
const int kLinkW         = 36;    //「修改」「删除」宽
const int kLinkPadPx     = 8;     //链接按译文加宽时，文字之外还要留的宽度
const int kCardRowGap    = 4;     //卡片顶部一行里行范围、「新」、作者之间的间距
const int kRangePadPx    = 2;     //后面还有「新」或作者时，行范围按文字宽度再留的宽度
const int kNewMarkW      = 16;    //「新」的设计宽度（按译文加宽）
const int kEditBoxH      = 72;    //编辑框高（约四行）
const int kEditButtonsH  = 28;    //编辑卡片底部按钮行高
const int kEditButtonW   = 56;    //「确定」「取消」宽
const int kEditButtonGap = 6;     //「确定」「取消」之间的间距
const int kMinTextH      = 18;    //说明文字至少的高度（空串或量不出时）
const int kSplitMin0     = 200;   //分栏左侧（代码区）最窄

//行范围的文字：「第 3 行」或「第 3～5 行」
std::wstring RangeText(int from, int to)
{
    if (from == to)
        return TRF(L"第 {0} 行", static_cast<unsigned int>(from));
    return TRF(L"第 {0}～{1} 行", static_cast<unsigned int>(from), static_cast<unsigned int>(to));
}

//造一个链接样式的文字（点了发 DUIN_CLICK）
std::unique_ptr<DuiLabel> MakeLink(UINT id, const std::wstring& text, DWORD align)
{
    std::unique_ptr<DuiLabel> link(new DuiLabel());
    link->SetCtrlId(id);
    link->SetText(text.c_str());
    link->SetMode(DuiLabel::ModeLink);
    link->SetAutoNavigate(false);
    link->SetLinkColor(kLinkColor);
    link->SetHoverColor(kLinkHoverColor);
    link->SetVisitedColor(kLinkColor);
    link->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return link;
}

//造一个按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, const std::wstring& text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text.c_str());
    return std::unique_ptr<DuiControl>(button.release());
}

//卡片上部位 part 的控件编号
UINT CardId(int index, int part)
{
    return kCodeNotesIdCardBase + (UINT)index * kCodeNotesIdStride + (UINT)part;
}

} // anonymous namespace

bool CodeNotesIdToNote(UINT id, int& index, int& part)
{
    if (id < kCodeNotesIdCardBase)
        return false;
    const UINT offset = id - kCodeNotesIdCardBase;
    index = (int)(offset / kCodeNotesIdStride);
    part = (int)(offset % kCodeNotesIdStride);
    return index < codesnippet::kCodeNotesMax;
}

void ResizeWindowForNotesPane(HWND hwnd, bool open, int grow, int& grownPx)
{
    if (!::IsWindow(hwnd))
        return;
    //最大化、最小化时不调整窗口；收起时加宽的部分不再记着
    if (::IsZoomed(hwnd) || ::IsIconic(hwnd))
    {
        if (!open)
            grownPx = 0;
        return;
    }
    RECT rcWnd;
    ::GetWindowRect(hwnd, &rcWnd);
    const int width = rcWnd.right - rcWnd.left;
    const int height = rcWnd.bottom - rcWnd.top;
    const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (!open)
    {
        if (grownPx > 0)
            ::SetWindowPos(hwnd, NULL, 0, 0, width - grownPx, height, flags);
        grownPx = 0;
        return;
    }
    grownPx = 0;
    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (grow <= 0 || !::GetMonitorInfo(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    //窗口已显示时向右加宽后右边不越出工作区；还没显示时（新打开、随后会居中）只看宽度放不放得下
    const bool fits = ::IsWindowVisible(hwnd) ? (rcWnd.right + grow <= mi.rcWork.right)
                                              : (width + grow <= mi.rcWork.right - mi.rcWork.left);
    if (!fits)
        return;
    ::SetWindowPos(hwnd, NULL, 0, 0, width + grow, height, flags);
    grownPx = grow;
}

// ======================= CodeNotesSplitter =======================

CodeNotesSplitter::CodeNotesSplitter()
    : m_rightWidth(kCodeNotesDefaultWidth)
    , m_collapsed(false)
    , m_inLayout(false)
    , m_userDragging(false)
{
    SetOrientation(DuiSplitter::Vertical);
    SetBarThickness(kCodeNotesSplitBarPx);
    SetMinSizes(kSplitMin0, kCodeNotesMinWidth);
}

void CodeNotesSplitter::SetRightWidth(int px)
{
    m_rightWidth = (px < kCodeNotesMinWidth) ? kCodeNotesMinWidth : px;
}

int CodeNotesSplitter::RightWidth() const
{
    return m_rightWidth;
}

void CodeNotesSplitter::SetCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    DuiControl* right = GetPane(1);
    if (right != NULL)
        right->SetVisible(!collapsed);
}

bool CodeNotesSplitter::IsCollapsed() const
{
    return m_collapsed;
}

void CodeNotesSplitter::Layout(const RECT& rcAvail)
{
    if (m_collapsed)
    {
        //收起：左栏占满，右栏已隐藏
        m_rcItem = rcAvail;
        DuiControl* left = GetPane(0);
        if (left != NULL)
            left->SetRect(rcAvail);
        return;
    }
    //拖动分隔条期间（基类改了目标位置再调本函数），或下面的 SetSplitPx 回调到本函数时：位置已经定好，直接交给基类排版
    if (m_userDragging || m_inLayout)
    {
        DuiSplitter::Layout(rcAvail);
        return;
    }
    //展开：右栏保持固定宽度，由它反推左栏宽度（窗口缩放时变的是代码区）。
    //SetSplitPx 内部会以 m_rcItem 再调一次 Layout（虚函数，回到本函数）：先把 m_rcItem 换成新的矩形，
    //并用 m_inLayout 让那一次直接交给基类，避免两个函数互相调用、无限递归
    m_rcItem = rcAvail;
    const int avail = rcAvail.right - rcAvail.left;
    m_inLayout = true;
    SetSplitPx(avail - GetBarThickness() - m_rightWidth);
    m_inLayout = false;
}

void CodeNotesSplitter::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_collapsed)
    {
        DuiSplitter::OnPaint(hdc, rcDirty);
        return;
    }
    if (!IsVisible())
        return;
    DuiControl* left = GetPane(0);
    if (left == NULL || !left->IsVisible())
        return;
    RECT inter;
    if (::IntersectRect(&inter, &left->GetRect(), &rcDirty))
        left->OnPaint(hdc, inter);
}

DuiControl* CodeNotesSplitter::HitTest(POINT ptHostClient)
{
    if (!m_collapsed)
        return DuiSplitter::HitTest(ptHostClient);
    if (!IsVisible() || !::PtInRect(&m_rcItem, ptHostClient))
        return NULL;
    DuiControl* left = GetPane(0);
    return (left != NULL) ? left->HitTest(ptHostClient) : NULL;
}

bool CodeNotesSplitter::OnSetCursor(POINT pt)
{
    if (m_collapsed)
        return false;
    return DuiSplitter::OnSetCursor(pt);
}

bool CodeNotesSplitter::OnLButtonDown(POINT pt, UINT mkFlags)
{
    //按在分隔条上（基类返回 true）就开始拖动；收起时不画分隔条，也不接受拖动
    if (m_collapsed)
        return false;
    const bool handled = DuiSplitter::OnLButtonDown(pt, mkFlags);
    if (handled)
        m_userDragging = true;
    return handled;
}

bool CodeNotesSplitter::OnLButtonUp(POINT pt, UINT mkFlags)
{
    m_userDragging = false;
    const bool handled = DuiSplitter::OnLButtonUp(pt, mkFlags);
    if (handled && !m_collapsed)
    {
        //拖完分隔条：记下新的右栏宽度，之后窗口缩放时保持这个宽度
        const int width = (m_rcItem.right - m_rcItem.left) - GetSplitPx() - GetBarThickness();
        SetRightWidth(width);
    }
    return handled;
}

// ======================= CodeNotesPane =======================

CodeNotesPane::CodeNotesPane()
    : m_readOnly(false)
    , m_selfId(0)
    , m_editing(false)
    , m_editIndex(-1)
    , m_editFrom(0)
    , m_editTo(0)
    , m_highlight(-1)
    , m_lastListWidth(-1)
    , m_pHeader(NULL)
    , m_pScroll(NULL)
    , m_pList(NULL)
    , m_pEditBox(NULL)
{
    typedef DuiLayout::Hint Hint;
    SetBgColor(kPaneBg);
    SetPadding(kPanePadH, kPanePadV, kPanePadH, kPanePadV);
    SetGap(kPaneGap);

    //顶部：「批注 (N)」与收起用的「»」
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    std::unique_ptr<DuiLabel> title(new DuiLabel());
    title->SetCtrlId(kCodeNotesIdHeader);
    title->SetTextColor(kHeaderText);
    title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_pHeader = title.get();
    header->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Weight(1));
    header->AddChild(std::unique_ptr<DuiControl>(MakeLink(kCodeNotesIdCollapse, L"\x00BB", DT_CENTER).release()),
                     Hint().Fixed(kCollapseW));
    AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kHeaderH));

    //卡片列表：内容高度由 Layout 按宽度算出后直接告诉滚动视图
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kCodeNotesIdScroll);
    m_pScroll = scroll.get();
    AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    Rebuild();
}

void CodeNotesPane::SetReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
}

void CodeNotesPane::SetSelfId(long long selfId)
{
    m_selfId = selfId;
}

void CodeNotesPane::SetAuthorNames(const std::map<long long, std::wstring>& names)
{
    m_authorNames = names;
}

void CodeNotesPane::SetEmptyText(const std::wstring& text)
{
    m_emptyText = text;
}

bool CodeNotesPane::CanEditNote(int index) const
{
    if (m_readOnly || index < 0 || index >= (int)m_notes.size())
        return false;
    return codesnippet::CanEditCodeNote(m_notes[(size_t)index], m_selfId);
}

std::wstring CodeNotesPane::AuthorName(long long authorId) const
{
    std::map<long long, std::wstring>::const_iterator it = m_authorNames.find(authorId);
    if (it != m_authorNames.end() && !it->second.empty())
        return it->second;
    return std::to_wstring(authorId);
}

void CodeNotesPane::SetNotes(const std::vector<codesnippet::CodeNote>& notes)
{
    m_notes = notes;
    m_editing = false;
    m_editIndex = -1;
    if (m_highlight >= (int)m_notes.size())
        m_highlight = -1;
    Rebuild();
}

const std::vector<codesnippet::CodeNote>& CodeNotesPane::Notes() const
{
    return m_notes;
}

void CodeNotesPane::BeginEdit(int from, int to, const std::wstring& text, int index)
{
    if (m_readOnly)
        return;
    //别人的批注不能修改（docs/代码片段方案.md 第 11.3 节）
    if (index >= 0 && index < (int)m_notes.size() && !CanEditNote(index))
        return;
    m_editing = true;
    m_editIndex = (index >= 0 && index < (int)m_notes.size()) ? index : -1;
    m_editFrom = from;
    m_editTo = to;
    m_editText = text;
    Rebuild();
}

void CodeNotesPane::EndEdit()
{
    if (!m_editing)
        return;
    m_editing = false;
    m_editIndex = -1;
    Rebuild();
}

bool CodeNotesPane::IsEditing() const
{
    return m_editing;
}

int CodeNotesPane::EditingIndex() const
{
    return m_editing ? m_editIndex : -1;
}

int CodeNotesPane::EditingFrom() const
{
    return m_editing ? m_editFrom : 0;
}

int CodeNotesPane::EditingTo() const
{
    return m_editing ? m_editTo : 0;
}

std::wstring CodeNotesPane::EditingText() const
{
    if (!m_editing || m_pEditBox == NULL)
        return std::wstring();
    return std::wstring((LPCWSTR)m_pEditBox->GetText());
}

DuiRichEdit* CodeNotesPane::EditBox() const
{
    return m_editing ? m_pEditBox : NULL;
}

void CodeNotesPane::SetHighlight(int index)
{
    m_highlight = index;
    for (size_t k = 0; k < m_cards.size(); ++k)
    {
        if (m_cards[k].m_card == NULL || m_cards[k].m_text == NULL)
            continue;
        const bool on = (m_cardNote[k] == index && index >= 0);
        m_cards[k].m_card->SetBgColor(on ? kCardHighlightBg : kCardBg);
        m_cards[k].m_card->SetBorderColor(on ? kCardHighlightBorder : kCardBorder);
    }
    Invalidate();
}

int CodeNotesPane::Highlight() const
{
    return m_highlight;
}

void CodeNotesPane::ScrollToNote(int index)
{
    if (m_pScroll == NULL || m_pList == NULL)
        return;
    int top = 0;
    for (size_t k = 0; k < m_cards.size(); ++k)
    {
        DuiControl* item = (m_cards[k].m_card != NULL) ? (DuiControl*)m_cards[k].m_card : (DuiControl*)m_cards[k].m_text;
        const int h = m_pList->GetHint(item).fixedMain;
        if (m_cardNote[k] == index)
        {
            const RECT& view = m_pScroll->GetRect();
            const int viewH = view.bottom - view.top;
            const int pos = m_pScroll->GetScrollPos();
            if (top < pos)
                m_pScroll->SetScrollPos(top);
            else if (top + h > pos + viewH)
                m_pScroll->SetScrollPos(top + h - viewH);
            return;
        }
        top += h + kListGap;
    }
}

void CodeNotesPane::Layout(const RECT& rcAvail)
{
    //balloonui 约定：覆写容器 Layout 的子类在开头放一个 LayoutGuard，期间对本容器的 SetHint 不再触发重排
    DuiLayout::LayoutGuard guard(*this);
    const int listWidth = (rcAvail.right - rcAvail.left) - kPanePadH * 2;
    const bool remeasure = (listWidth != m_lastListWidth);
    if (remeasure)
    {
        m_lastListWidth = listWidth;
        MeasureCards(listWidth);
    }
    DuiVBox::Layout(rcAvail);
    //DuiScrollView::SetContentHeight 只更新滚动范围，不重算卡片列表的矩形；滚动视图的矩形没变时，上面的排版也不会让它
    //重新排版。卡片换了一批（进入或结束编辑、批注增删）之后，列表的矩形会停在旧的高度上，超出部分的卡片画得出来却点不到
    //（命中测试逐层检查点是否在矩形内）。所以重新测量过卡片之后，对滚动视图强制重排一次
    if (remeasure && m_pScroll != NULL)
        m_pScroll->ForceLayout(m_pScroll->GetRect());
}

std::unique_ptr<DuiControl> CodeNotesPane::MakeCard(int index, CardRefs& refs)
{
    typedef DuiLayout::Hint Hint;
    const codesnippet::CodeNote& note = m_notes[(size_t)index];
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetCtrlId(CardId(index, kCodeNotesPartCard));
    card->SetPadding(kCardPad, kCardPad, kCardPad, kCardPad);
    card->SetGap(kCardInnerGap);
    card->SetBgColor(index == m_highlight ? kCardHighlightBg : kCardBg);
    card->SetBorderColor(index == m_highlight ? kCardHighlightBorder : kCardBorder);
    card->SetBorderWidth((float)kCardBorderW);
    card->SetCornerRadius(kCardRadius);

    //顶部一行：行范围（链接）；这一轮新增或修改的接着写「新」；设了作者名字表时再写作者（灰色，过长时以省略号结尾）；
    //本人能修改的批注右侧「修改」「删除」（docs/代码片段方案.md 第 11.3 节）
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kCardRowGap);
    const std::wstring rangeText = RangeText(note.m_from, note.m_to);
    std::unique_ptr<DuiLabel> rangeLink = MakeLink(CardId(index, kCodeNotesPartRange), rangeText, DT_LEFT);
    const bool showAuthor = !m_authorNames.empty() && note.m_authorId != 0;
    if (!showAuthor && !note.m_isNew)
    {
        row->AddChild(std::unique_ptr<DuiControl>(rangeLink.release()), Hint().Weight(1));
    }
    else
    {
        //后面还有「新」或作者：行范围按文字宽度，余下的宽度给作者
        const int rangeW = i18n::MeasureUiTextWidth(rangeText.c_str(), i18n::DefaultFontPixelHeight(), false) + kRangePadPx;
        row->AddChild(std::unique_ptr<DuiControl>(rangeLink.release()), Hint().Fixed(rangeW));
        if (note.m_isNew)
        {
            const std::wstring newText = TR(L"新");
            std::unique_ptr<DuiLabel> mark(new DuiLabel());
            mark->SetCtrlId(CardId(index, kCodeNotesPartNew));
            mark->SetText(newText.c_str());
            mark->SetTextColor(kNewMarkText);
            mark->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            row->AddChild(std::unique_ptr<DuiControl>(mark.release()),
                          Hint().Fixed(i18n::WidthForText(newText.c_str(), kNewMarkW, i18n::DefaultFontPixelHeight(),
                                                          false, kRangePadPx)));
        }
        std::unique_ptr<DuiLabel> author(new DuiLabel());
        if (showAuthor)
        {
            author->SetCtrlId(CardId(index, kCodeNotesPartAuthor));
            author->SetText(AuthorName(note.m_authorId).c_str());
            author->SetTextColor(kGrayText);
            author->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        row->AddChild(std::unique_ptr<DuiControl>(author.release()), Hint().Weight(1));
    }
    if (CanEditNote(index))
    {
        const std::wstring editText = TR(L"修改");
        const std::wstring deleteText = TR(L"删除");
        row->AddChild(std::unique_ptr<DuiControl>(MakeLink(CardId(index, kCodeNotesPartEdit), editText, DT_RIGHT).release()),
                      Hint().Fixed(i18n::WidthForText(editText.c_str(), kLinkW, i18n::DefaultFontPixelHeight(),
                                                      false, kLinkPadPx)));
        row->AddChild(std::unique_ptr<DuiControl>(
                          MakeLink(CardId(index, kCodeNotesPartDelete), deleteText, DT_RIGHT).release()),
                      Hint().Fixed(i18n::WidthForText(deleteText.c_str(), kLinkW, i18n::DefaultFontPixelHeight(),
                                                      false, kLinkPadPx)));
    }
    card->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kCardHeaderH));

    //说明：折行显示，高度在 MeasureCards 里按宽度算
    std::unique_ptr<DuiLabel> text(new DuiLabel());
    text->SetText(note.m_text.c_str());
    text->SetTextColor(kNoteText);
    text->SetWordWrap(true);
    text->SetTextAlign(DT_LEFT | DT_TOP | DT_NOPREFIX);
    refs.m_text = text.get();
    card->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Fixed(kMinTextH));

    refs.m_card = card.get();
    refs.m_fixedH = kCardPad * 2 + kCardHeaderH + kCardInnerGap;
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> CodeNotesPane::MakeEditCard(CardRefs& refs)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetPadding(kCardPad, kCardPad, kCardPad, kCardPad);
    card->SetGap(kCardInnerGap);
    card->SetBgColor(kCardHighlightBg);
    card->SetBorderColor(kCardHighlightBorder);
    card->SetBorderWidth((float)kCardBorderW);
    card->SetCornerRadius(kCardRadius);

    //顶部一行：要批注的行范围
    std::unique_ptr<DuiLabel> range(new DuiLabel());
    range->SetText(RangeText(m_editFrom, m_editTo).c_str());
    range->SetTextColor(kGrayText);
    range->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    card->AddChild(std::unique_ptr<DuiControl>(range.release()), Hint().Fixed(kCardHeaderH));

    //输入框：多行、折行，最多 kCodeNoteTextMaxChars 个字
    std::unique_ptr<DuiRichEdit> edit(new DuiRichEdit());
    edit->SetCtrlId(kCodeNotesIdEditBox);
    edit->SetPlainTextMode(true);
    edit->SetMultiLine(true);
    edit->SetWordWrap(true);
    edit->SetShowBorder(true);
    edit->SetVScrollPolicy(DuiRichEdit::kScrollBarAuto);
    edit->SetMaxLength(codesnippet::kCodeNoteTextMaxChars);
    edit->SetPlaceholder(TR(L"写下对这几行的说明"));
    edit->SetText(m_editText.c_str());
    m_pEditBox = edit.get();
    card->AddChild(std::unique_ptr<DuiControl>(edit.release()), Hint().Fixed(kEditBoxH));

    //底部：「确定」「取消」靠右
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kEditButtonGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    const std::wstring okText = TR(L"确定");
    const std::wstring cancelText = TR(L"取消");
    buttons->AddChild(MakeButton(kCodeNotesIdEditOk, okText, DuiButton::Variant::Primary),
                      Hint().Fixed(i18n::DefaultButtonWidthForText(okText.c_str(), kEditButtonW)));
    buttons->AddChild(MakeButton(kCodeNotesIdEditCancel, cancelText, DuiButton::Variant::Default),
                      Hint().Fixed(i18n::DefaultButtonWidthForText(cancelText.c_str(), kEditButtonW)));
    card->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kEditButtonsH));

    refs.m_card = card.get();
    refs.m_text = NULL;
    refs.m_fixedH = kCardPad * 2 + kCardHeaderH + kEditBoxH + kEditButtonsH + kCardInnerGap * 2;
    return std::unique_ptr<DuiControl>(card.release());
}

void CodeNotesPane::Rebuild()
{
    typedef DuiLayout::Hint Hint;
    m_cards.clear();
    m_cardNote.clear();
    m_pEditBox = NULL;

    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetGap(kListGap);
    m_pList = list.get();

    //编辑中的卡片按起始行插在对应位置；修改已有批注时代替那一张
    bool editPlaced = !m_editing;
    for (size_t i = 0; i <= m_notes.size(); ++i)
    {
        if (!editPlaced && (i == m_notes.size() || (int)i == m_editIndex
                            || (m_editIndex < 0 && m_notes[i].m_from > m_editFrom)))
        {
            //先建卡片、再按它的固定高度添加：两步写在一个调用的实参里时求值顺序不确定，可能先读到未填的高度
            CardRefs refs;
            std::unique_ptr<DuiControl> editCard = MakeEditCard(refs);
            list->AddChild(std::move(editCard), Hint().Fixed(refs.m_fixedH));
            m_cards.push_back(refs);
            m_cardNote.push_back(m_editIndex);
            editPlaced = true;
            if ((int)i == m_editIndex)
                continue;
        }
        if (i == m_notes.size())
            break;
        CardRefs refs;
        std::unique_ptr<DuiControl> card = MakeCard((int)i, refs);
        list->AddChild(std::move(card), Hint().Fixed(refs.m_fixedH + kMinTextH));
        m_cards.push_back(refs);
        m_cardNote.push_back((int)i);
    }

    //没有批注也没有在编辑：一行灰色提示
    if (m_cards.empty())
    {
        std::unique_ptr<DuiLabel> empty(new DuiLabel());
        empty->SetCtrlId(kCodeNotesIdEmpty);
        if (m_readOnly)
            empty->SetText(TR(L"没有批注"));
        else if (!m_emptyText.empty())
            empty->SetText(m_emptyText.c_str());
        else
            empty->SetText(TR(L"选中代码行后点「批注」添加说明"));
        empty->SetTextColor(kGrayText);
        empty->SetWordWrap(true);
        empty->SetTextAlign(DT_LEFT | DT_TOP | DT_NOPREFIX);
        CardRefs refs;
        refs.m_card = NULL;
        refs.m_text = empty.get();
        refs.m_fixedH = 0;
        list->AddChild(std::unique_ptr<DuiControl>(empty.release()), Hint().Fixed(kMinTextH));
        m_cards.push_back(refs);
        m_cardNote.push_back(-1);
    }

    if (m_pScroll != NULL)
        m_pScroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    UpdateHeader();

    //卡片换了一批：高度要按当前宽度重算；已经排过版时立即重排
    m_lastListWidth = -1;
    const RECT& rc = GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
        ForceLayout(rc);
}

void CodeNotesPane::MeasureCards(int listWidth)
{
    if (m_pList == NULL)
        return;
    int total = 0;
    for (size_t k = 0; k < m_cards.size(); ++k)
    {
        CardRefs& refs = m_cards[k];
        int height = refs.m_fixedH;
        if (refs.m_text != NULL)
        {
            //说明文字按卡片里的可用宽度折行（没有卡片容器的是空状态提示，用整行宽度）
            const int textWidth = (refs.m_card != NULL) ? listWidth - kCardPad * 2 - kCardBorderW * 2 : listWidth;
            int textH = refs.m_text->MeasureHeight(textWidth > 1 ? textWidth : 1);
            if (textH < kMinTextH)
                textH = kMinTextH;
            DuiLayout* textParent = dynamic_cast<DuiLayout*>(refs.m_text->GetParent());
            if (textParent != NULL)
            {
                DuiLayout::Hint hint = textParent->GetHint(refs.m_text);
                hint.fixedMain = textH;
                textParent->SetHint(refs.m_text, hint);
            }
            height += textH;
        }
        DuiControl* item = (refs.m_card != NULL) ? (DuiControl*)refs.m_card : (DuiControl*)refs.m_text;
        DuiLayout::Hint itemHint = m_pList->GetHint(item);
        itemHint.fixedMain = height;
        m_pList->SetHint(item, itemHint);
        total += height;
        if (k + 1 < m_cards.size())
            total += kListGap;
    }
    if (m_pScroll != NULL)
        m_pScroll->SetContentHeight(total);
}

void CodeNotesPane::UpdateHeader()
{
    if (m_pHeader != NULL)
        m_pHeader->SetText(TRF(L"批注 ({0})", static_cast<unsigned int>(m_notes.size())).c_str());
}

} // namespace ballooncode
