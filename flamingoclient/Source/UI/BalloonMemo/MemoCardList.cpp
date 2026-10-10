/**
 *  MemoCardList.cpp —— 群「备忘」页签与本人备忘窗口共用的备忘卡片列表，见 MemoCardList.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （两个窗口的布局单测要建出完整的控件树）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 MemoCalendarControls.cpp 相同 ----
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

#include "MemoCardList.h"
#include "../BalloonHistory/HistoryRow.h"   // 卡片外层：悬停时显示登记过的按钮
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonmemo {

namespace {

// ---- 颜色：与群「公告」页签的公告卡同一套浅底与描边 ----
const COLORREF kCardBg          = RGB(0xFA, 0xF9, 0xF6);   // 卡片浅底
const COLORREF kCardHoverBg     = RGB(0xF2, 0xF0, 0xEB);   // 悬停时的卡片底色
const COLORREF kCardBorder      = RGB(0xEC, 0xEA, 0xE6);   // 卡片描边
const COLORREF kCardHoverBorder = RGB(0xDC, 0xD9, 0xD3);   // 悬停时的描边
const COLORREF kTitleColor      = RGB(0x2A, 0x2A, 0x2A);   // 标题
const COLORREF kNoteColor       = RGB(0x55, 0x55, 0x55);   // 备注
const COLORREF kWhenColor       = RGB(0x1E, 0x6F, 0xFF);   // 提醒时间（与月历选中色同一个品牌蓝）
const COLORREF kEndedColor      = RGB(0x9A, 0x98, 0x93);   // 「已结束」
const COLORREF kMetaColor       = RGB(0x8A, 0x8A, 0x8A);   // 规则、创建人
const COLORREF kDefaultOuterBg  = RGB(0xFF, 0xFF, 0xFF);   // 视口默认底色（两处宿主都是白底）

// ---- 版式（逻辑像素）----
const int   kCardPadH     = 12;    // 卡片左右内边距
const int   kCardPadV     = 8;     // 卡片上下内边距
const int   kCardLineGap  = 2;     // 卡片内各行之间的间距
const int   kCardRadius   = 8;     // 卡片圆角
const float kCardBorderW  = 1.0f;  // 卡片描边线宽
const int   kTitleRowH    = 24;    // 标题一行的高（按钮与标题同高）
const int   kLineH        = 20;    // 备注、时间、规则、创建人各行的高
const int   kActionW      = 40;    // 「修改」「删除」各自的宽
const int   kActionGap    = 2;     // 标题一行里各控件的间距
const int   kListGap      = 8;     // 卡片之间的间距
const int   kAlwaysLines  = 2;     // 标题以下总有的行数：提醒时间、规则
const int   kBothSides    = 2;     // 上下两侧（内边距按两侧计）

// ---- 字号（磅）----
const int kTitlePt  = 10;   // 标题（加粗）
const int kLinePt   = 9;    // 其余各行与两个按钮

//建一行单行文字；放不下时末尾省略
std::unique_ptr<DuiControl> MakeLine(UINT id, const std::wstring& text, COLORREF color, int pt, bool bold)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextPointSize(pt, bold);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

//建标题右侧的一个文字按钮（「修改」或「删除」）
std::unique_ptr<DuiButton> MakeAction(UINT id, LPCTSTR text)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(DuiButton::Variant::Text);
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetTextPointSize(kLinePt);
    return button;
}

//建第 index 张卡片
std::unique_ptr<DuiControl> BuildCard(int index, const MemoCardText& card, COLORREF outerBg)
{
    typedef DuiLayout::Hint Hint;
    const UINT base = kMemoCardIdBase + (UINT)index * kMemoCardIdStride;

    std::unique_ptr<balloonhistory::HistoryRow> row(new balloonhistory::HistoryRow());
    row->SetCtrlId(base + kMemoCardPartRow);
    row->SetHoverBgColor(outerBg);

    std::unique_ptr<DuiVBox> body(new DuiVBox());
    body->SetCtrlId(base + kMemoCardPartBody);
    body->SetBgColor(kCardBg);
    body->SetBorderColor(kCardBorder);
    body->SetBorderWidth(kCardBorderW);
    body->SetCornerRadius(kCardRadius);
    body->SetPadding(kCardPadH, kCardPadV, kCardPadH, kCardPadV);
    body->SetGap(kCardLineGap);

    //标题一行：能修改的卡片在标题右侧放「修改」「删除」，平时隐藏、悬停时显示。按钮隐藏时标题用满整行，
    //显示时标题让出按钮的宽度（长标题的省略位置随之左移）
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetCtrlId(base + kMemoCardPartTitleRow);
    titleRow->SetGap(kActionGap);
    titleRow->AddChild(MakeLine(base + kMemoCardPartTitle, card.m_title, kTitleColor, kTitlePt, true), Hint().Weight(1));
    DuiButton* edit = NULL;
    DuiButton* del = NULL;
    if (card.m_editable)
    {
        std::unique_ptr<DuiButton> editBtn = MakeAction(base + kMemoCardPartEdit, TR(L"修改"));
        edit = editBtn.get();
        titleRow->AddChild(std::unique_ptr<DuiControl>(editBtn.release()), Hint().Fixed(kActionW));
        std::unique_ptr<DuiButton> delBtn = MakeAction(base + kMemoCardPartDelete, TR(L"删除"));
        del = delBtn.get();
        titleRow->AddChild(std::unique_ptr<DuiControl>(delBtn.release()), Hint().Fixed(kActionW));
    }
    body->AddChild(std::unique_ptr<DuiControl>(titleRow.release()), Hint().Fixed(kTitleRowH));

    if (!card.m_note.empty())
    {
        body->AddChild(MakeLine(base + kMemoCardPartNote, card.m_note, kNoteColor, kLinePt, false), Hint().Fixed(kLineH));
    }
    body->AddChild(MakeLine(base + kMemoCardPartWhen, card.m_when, card.m_ended ? kEndedColor : kWhenColor, kLinePt, false),
                   Hint().Fixed(kLineH));
    body->AddChild(MakeLine(base + kMemoCardPartRule, card.m_rule, kMetaColor, kLinePt, false), Hint().Fixed(kLineH));
    if (!card.m_creator.empty())
    {
        body->AddChild(MakeLine(base + kMemoCardPartCreator, card.m_creator, kMetaColor, kLinePt, false),
                       Hint().Fixed(kLineH));
    }
    row->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));

    //登记要等按钮已经挂进控件树：登记时立即隐藏它们，鼠标进入卡片才显示
    if (edit != NULL)
    {
        row->AddHoverOnlyControl(edit);
    }
    if (del != NULL)
    {
        row->AddHoverOnlyControl(del);
    }
    return std::unique_ptr<DuiControl>(row.release());
}

} // anonymous namespace

// ---------------- CMemoCardScrollView ----------------

CMemoCardScrollView::CMemoCardScrollView()
    : m_outerBg(kDefaultOuterBg)
{
    SetAutoContentHeight(true);
}

void CMemoCardScrollView::SetOuterBg(COLORREF color)
{
    if (m_outerBg == color)
    {
        return;
    }
    m_outerBg = color;
    Invalidate();
}

RECT CMemoCardScrollView::ThumbRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    balloonwjui::DuiScrollBar* bar = const_cast<CMemoCardScrollView*>(this)->GetScrollBar();
    if (bar == NULL || !bar->IsVisible())
    {
        return rc;
    }
    return bar->ComputePaintThumbRect();
}

void CMemoCardScrollView::Layout(const RECT& rcAvail)
{
    //卡片列表（SetMemoCards 放进来的 DuiVBox）：内容超出视口时右侧让出滚动条那一条，滑块不压在卡片上；
    //放得下时卡片用满整宽。滚动条是悬浮式的，基类不会替内容让出宽度，这里自己留
    DuiVBox* list = dynamic_cast<DuiVBox*>(GetContent());
    if (list != NULL)
    {
        const int viewH = rcAvail.bottom - rcAvail.top;
        const bool overflow = viewH > 0 && list->GetDesiredSize().cy > viewH;
        list->SetPadding(0, 0, overflow ? kMemoScrollGutter : 0, 0);
    }
    DuiScrollView::Layout(rcAvail);
}

void CMemoCardScrollView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    HBRUSH brush = ::CreateSolidBrush(m_outerBg);
    ::FillRect(hdc, &m_rcItem, brush);
    ::DeleteObject(brush);

    //卡片裁在视口里画。用 IntersectClipRect 而不是 SelectClipRgn，理由同基类 DuiScrollView::OnPaint：
    //后者是替换语义，会撤掉外层容器已经设好的裁剪
    DuiControl* content = GetContent();
    const int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top, m_rcItem.right, m_rcItem.bottom);
    RECT inter;
    if (content != NULL && ::IntersectRect(&inter, &content->GetRect(), &rcDirty))
    {
        content->OnPaint(hdc, inter);
    }
    ::RestoreDC(hdc, saved);

    //最后画基类的滚动条：浮在右侧窄条之上，自动隐藏时由它自己按不透明度决定画不画
    balloonwjui::DuiScrollBar* bar = GetScrollBar();
    if (bar != NULL && bar->IsVisible() && ::IntersectRect(&inter, &bar->GetRect(), &rcDirty))
    {
        bar->OnPaint(hdc, inter);
    }
}

// ---------------- 卡片列表 ----------------

std::unique_ptr<DuiScrollView> CreateMemoCardScroll(UINT ctrlId)
{
    std::unique_ptr<CMemoCardScrollView> scroll(new CMemoCardScrollView());
    scroll->SetCtrlId(ctrlId);
    //先放一个空的竖排容器，SetMemoCards 之前也有内容控件可排版
    scroll->SetContent(std::unique_ptr<DuiControl>(new DuiVBox()));
    return std::unique_ptr<DuiScrollView>(scroll.release());
}

int MemoCardHeight(const MemoCardText& card)
{
    int lines = kAlwaysLines;
    if (!card.m_note.empty())
    {
        ++lines;
    }
    if (!card.m_creator.empty())
    {
        ++lines;
    }
    //标题一行加其余 lines 行，共 lines + 1 行、lines 个行间距，再加上下内边距
    return kCardPadV * kBothSides + kTitleRowH + lines * kLineH + lines * kCardLineGap;
}

void SetMemoCards(DuiScrollView* scroll, const std::vector<MemoCardText>& cards, COLORREF outerBg)
{
    if (scroll == NULL)
    {
        return;
    }
    typedef DuiLayout::Hint Hint;
    CMemoCardScrollView* memoScroll = dynamic_cast<CMemoCardScrollView*>(scroll);
    if (memoScroll != NULL)
    {
        memoScroll->SetOuterBg(outerBg);
    }
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetGap(kListGap);
    const size_t count = (cards.size() < (size_t)kMemoCardMaxCount) ? cards.size() : (size_t)kMemoCardMaxCount;
    for (size_t i = 0; i < count; ++i)
    {
        list->AddChild(BuildCard((int)i, cards[i], outerBg), Hint().Fixed(MemoCardHeight(cards[i])));
    }
    const int keepPos = scroll->GetScrollPos();
    scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    scroll->ForceLayout(scroll->GetRect());
    //内容变短后原位置可能超出范围，由 SetScrollPos 收回到合法范围
    scroll->SetScrollPos(keepPos);
    scroll->Invalidate();
}

bool MemoCardFromCtrlId(UINT ctrlId, int& index, int& part)
{
    const UINT end = kMemoCardIdBase + (UINT)kMemoCardMaxCount * kMemoCardIdStride;
    if (ctrlId < kMemoCardIdBase || ctrlId >= end)
    {
        return false;
    }
    index = (int)((ctrlId - kMemoCardIdBase) / kMemoCardIdStride);
    part = (int)((ctrlId - kMemoCardIdBase) % kMemoCardIdStride);
    return true;
}

void UpdateMemoCardHover(DuiScrollView* scroll, POINT pt)
{
    if (scroll == NULL || !scroll->IsVisible())
    {
        return;
    }
    DuiControl* content = scroll->GetContent();
    if (content == NULL)
    {
        return;
    }
    const RECT viewRc = scroll->GetRect();
    const bool inView = ::PtInRect(&viewRc, pt) != FALSE;
    const std::vector<std::unique_ptr<DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonhistory::HistoryRow* row = dynamic_cast<balloonhistory::HistoryRow*>(kids[i].get());
        if (row == NULL)
        {
            continue;
        }
        const RECT rc = row->GetRect();
        const bool hit = inView && ::PtInRect(&rc, pt) != FALSE;
        if (row->IsHovered() == hit)
        {
            continue;
        }
        row->SetHovered(hit);
        //底板是外层唯一的子控件，见 BuildCard
        DuiVBox* body = row->Children().empty() ? NULL : dynamic_cast<DuiVBox*>(row->Children()[0].get());
        if (body != NULL)
        {
            body->SetBgColor(hit ? kCardHoverBg : kCardBg);
            body->SetBorderColor(hit ? kCardHoverBorder : kCardBorder);
            body->Invalidate();
        }
    }
}

} // namespace balloonmemo
