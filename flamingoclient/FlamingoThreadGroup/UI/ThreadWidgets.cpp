/**
 *  帖子群窗口里卡片与详情共用的小控件建法，见 ThreadWidgets.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadWidgets.h"

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "ThreadPostCard.h"   // PostCardChip
#include "ThreadUiText.h"     // FormatLikeChip

using balloonwjui::DuiAvatar;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

const int kChipPadding = 7;         //互动标签左右内边距，单位：像素
const int kChipIconPx = 16;         //互动标签里的表情图标边长
const int kChipIconGap = 3;         //表情图标与人数之间的距离
//表情图标的圆角：DuiAvatar 用圆弧拼圆角路径，半径为 0 时路径无效、图画不出来，取一个看不出来的小圆角
const int kChipIconRadius = 3;

const COLORREF kChipBg = RGB(0xF3, 0xF1, 0xEE);        //互动标签底色
const COLORREF kChipHoverBg = RGB(0xE8, 0xE5, 0xE0);   //可点击的互动标签悬停时的底色
const COLORREF kChipText = RGB(0x55, 0x55, 0x55);      //互动标签文字
const COLORREF kChipMineBg = RGB(0xFF, 0xE6, 0xD6);    //本人加过的互动标签底色
const COLORREF kChipMineHoverBg = RGB(0xFF, 0xD6, 0xBD);//本人加过的互动标签悬停时的底色
const COLORREF kChipMineText = RGB(0xC2, 0x50, 0x1F);  //本人加过的互动标签文字

//建一个标签的容器：给了控件 id 时是可点击的胶囊，否则是普通的圆角底色容器
std::unique_ptr<DuiVBox> MakeChipBox(bool mine, UINT id)
{
    std::unique_ptr<DuiVBox> box;
    if (id != 0)
    {
        box.reset(new CThreadClickPill(mine ? kChipMineBg : kChipBg, mine ? kChipMineHoverBg : kChipHoverBg));
        box->SetCtrlId(id);
    }
    else
    {
        box.reset(new DuiVBox());
        box->SetBgColor(mine ? kChipMineBg : kChipBg);
    }
    box->SetCornerRadius(kReactionChipHeight / 2);
    return box;
}

} // namespace

//======================== CThreadClickPill ========================

CThreadClickPill::CThreadClickPill(COLORREF bg, COLORREF hoverBg)
    : m_bg(bg)
    , m_hoverBg(hoverBg)
    , m_pressed(false)
{
    SetBgColor(bg);
}

DuiControl* CThreadClickPill::HitTest(POINT ptHostClient)
{
    if (!IsVisible())
        return NULL;
    const RECT rc = GetRect();
    return ::PtInRect(&rc, ptHostClient) ? this : NULL;
}

bool CThreadClickPill::OnLButtonDown(POINT pt, UINT mkFlags)
{
    (void)pt;
    (void)mkFlags;
    m_pressed = true;
    return true;
}

bool CThreadClickPill::OnLButtonUp(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    const RECT rc = GetRect();
    const bool click = m_pressed && ::PtInRect(&rc, pt);
    m_pressed = false;
    //发通知放在最后：窗口会延后执行动作（动作会重建详情、销毁本控件），这里发完不再访问成员
    if (click)
        NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool CThreadClickPill::OnMouseEnter()
{
    DuiVBox::OnMouseEnter();
    SetBgColor(m_hoverBg);
    Invalidate();
    NotifyParent(kNotifyPillEnter);
    return true;
}

bool CThreadClickPill::OnMouseLeave()
{
    DuiVBox::OnMouseLeave();
    m_pressed = false;
    SetBgColor(m_bg);
    Invalidate();
    NotifyParent(kNotifyPillLeave);
    return true;
}

int MeasureTextWidthPx(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ old = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old != NULL)
        ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

std::unique_ptr<DuiLabel> MakeTextLabel(const std::wstring& text, COLORREF color, HFONT font, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextAlign(align);
    if (font != NULL)
        label->SetFont(font);
    return label;
}

std::unique_ptr<DuiVBox> MakePill(const std::wstring& text, COLORREF bg, COLORREF fg, HFONT font, int radius, int padding,
                                  int& width)
{
    std::unique_ptr<DuiVBox> pill(new DuiVBox());
    pill->SetBgColor(bg);
    pill->SetCornerRadius(radius);
    pill->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(text, fg, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                   DuiLayout::Hint().Weight(1));
    width = MeasureTextWidthPx(font, text) + 2 * padding;
    return pill;
}

void AddReactionChips(DuiHBox* row, const std::vector<PostCardChip>& chips, HFONT font, const std::vector<UINT>* ids)
{
    typedef DuiLayout::Hint Hint;
    if (row == NULL)
        return;
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const PostCardChip& chip = chips[i];
        const UINT id = (ids != NULL && i < ids->size()) ? (*ids)[i] : 0;
        const COLORREF fg = chip.m_mine ? kChipMineText : kChipText;
        if (chip.m_like || chip.m_icon == NULL)
        {
            //「赞 N」（没人点过时只写「赞」）；表情图片读不出来时退回只显示人数
            const std::wstring text = chip.m_like ? FormatLikeChip(chip.m_count) : std::to_wstring(static_cast<long long>(chip.m_count));
            std::unique_ptr<DuiVBox> pill = MakeChipBox(chip.m_mine, id);
            pill->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(text, fg, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                           Hint().Weight(1));
            const int width = MeasureTextWidthPx(font, text) + 2 * kChipPadding;
            row->AddChild(std::unique_ptr<DuiControl>(pill.release()),
                          Hint().Fixed(width, kReactionChipHeight).AlignC(DuiLayout::AlignCenter));
            continue;
        }
        //表情图标 + 人数
        const std::wstring count = std::to_wstring(static_cast<long long>(chip.m_count));
        std::unique_ptr<DuiVBox> pill = MakeChipBox(chip.m_mine, id);
        pill->SetPadding(kChipPadding, 0, kChipPadding, 0);
        std::unique_ptr<DuiHBox> inner(new DuiHBox());
        std::unique_ptr<DuiAvatar> icon(new DuiAvatar());
        icon->SetShape(DuiAvatar::ShapeRoundRect);
        icon->SetCornerRadius(kChipIconRadius);
        icon->SetBitmap(chip.m_icon);
        inner->AddChild(std::unique_ptr<DuiControl>(icon.release()),
                        Hint().Fixed(kChipIconPx, kChipIconPx).Margin(0, 0, kChipIconGap, 0).AlignC(DuiLayout::AlignCenter));
        const int countWidth = MeasureTextWidthPx(font, count);
        inner->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(count, fg, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                        Hint().Fixed(countWidth));
        pill->AddChild(std::unique_ptr<DuiControl>(inner.release()), Hint().Weight(1));
        const int width = 2 * kChipPadding + kChipIconPx + kChipIconGap + countWidth;
        row->AddChild(std::unique_ptr<DuiControl>(pill.release()),
                      Hint().Fixed(width, kReactionChipHeight).AlignC(DuiLayout::AlignCenter));
    }
}

} // namespace threadgroup
