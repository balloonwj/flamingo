/**
 *  帖子群窗口左栏的帖子卡片，见 ThreadPostCard.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadPostCard.h"

#include <gdiplus.h>
#include <memory>

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "ThreadUiText.h"
#include "ThreadWidgets.h"   // MakeTextLabel / MakePill / AddReactionChips / MeasureTextWidthPx

using balloonwjui::DuiAvatar;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素（样稿第 0 期） ----
const int kCardRadius = 8;          //卡片圆角
const float kCardBorderWidth = 1.0f;//卡片描边宽
const int kAvatarPx = 22;           //作者头像边长
const int kAvatarGap = 8;           //头像与名字之间的距离
const int kNameMaxWidth = 160;      //名字最多占的宽度，更长时省略
const int kMetaGap = 8;             //名字与时间之间的距离
const int kPinHeight = 18;          //「置顶」标记高
const int kPinPadding = 5;          //「置顶」标记左右内边距
const int kPinRadius = 4;           //「置顶」标记圆角
const int kThumbRadius = 6;         //缩略图圆角
const int kChipGap = 6;             //互动标签之间的距离
const BYTE kMoreOverlayAlpha = 110; //「+N」底下那层黑色的不透明度（0~255）

//---- 颜色（样稿第 0 期） ----
const COLORREF kCardBg = RGB(0xFF, 0xFF, 0xFF);           //卡片底色
const COLORREF kCardBgSelected = RGB(0xFF, 0xFC, 0xFA);   //选中时的卡片底色（极浅的主色调）
const COLORREF kCardBorder = RGB(0xEB, 0xEA, 0xE7);       //卡片描边
const COLORREF kCardBorderHover = RGB(0xD6, 0xD3, 0xCE);  //悬停时的描边
const COLORREF kCardBorderSelected = RGB(0xFF, 0x8F, 0x66);//选中时的描边（主色调稍浅）
const COLORREF kNameColor = RGB(0x33, 0x33, 0x33);        //作者名字
const COLORREF kFaintColor = RGB(0x9A, 0x9A, 0x9A);       //时间、留言数
const COLORREF kTitleColor = RGB(0x1A, 0x1A, 0x1A);       //标题
const COLORREF kTextColor = RGB(0x33, 0x33, 0x33);        //正文
const COLORREF kPinBg = RGB(0xFF, 0xF8, 0xEA);            //「置顶」标记底色
const COLORREF kPinText = RGB(0xE8, 0xA3, 0x3D);          //「置顶」标记文字
const COLORREF kThumbPlaceholder = RGB(0xEE, 0xEC, 0xE8); //缩略图还没下载时的占位底色

//界面文字（「置顶」标记用 TRC 与详情里的「置顶」按钮区分，英文等语言两处译法不同）
const wchar_t* const kMorePattern = TR_NOOP(L"+{0}");

/**
 *  缩略图：圆角的 DuiAvatar；还没下载时画占位底色；more 大于 0 时在图上叠一层半透明黑色与「+N」。
 *  只覆写 OnPaint（子类化扩展，不改 balloonui）。
 */
class CThumbTile : public DuiAvatar
{
public:
    CThumbTile(int more, HFONT font)
        : m_more(more)
        , m_font(font)
    {
        SetShape(DuiAvatar::ShapeRoundRect);
        SetCornerRadius(kThumbRadius);
        SetFallbackBgColor(kThumbPlaceholder);
    }

    /**
     *  绘制：先由 DuiAvatar 画图片（或占位底色），再叠「+N」。
     *  @param hdc     宿主的背缓冲 DC
     *  @param rcDirty 需要重画的区域
     */
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        DuiAvatar::OnPaint(hdc, rcDirty);
        if (m_more <= 0 || !IsVisible())
            return;
        const RECT rc = GetRect();
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(2 * kThumbRadius);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rc.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rc.top);
        const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(rc.right - rc.left);
        const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(rc.bottom - rc.top);
        const Gdiplus::REAL kQuarter = 90.0f;
        const Gdiplus::REAL kHalf = 180.0f;
        const Gdiplus::REAL kThreeQuarters = 270.0f;
        path.AddArc(x, y, d, d, kHalf, kQuarter);
        path.AddArc(x + w - d, y, d, d, kThreeQuarters, kQuarter);
        path.AddArc(x + w - d, y + h - d, d, d, 0.0f, kQuarter);
        path.AddArc(x, y + h - d, d, d, kQuarter, kQuarter);
        path.CloseFigure();
        Gdiplus::SolidBrush shade(Gdiplus::Color(kMoreOverlayAlpha, 0, 0, 0));
        g.FillPath(&shade, &path);
        const std::wstring text = TRF(kMorePattern, m_more);
        HGDIOBJ oldFont = (m_font != NULL) ? ::SelectObject(hdc, m_font) : NULL;
        const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldColor = ::SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
        RECT textRc = rc;
        ::DrawTextW(hdc, text.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SetTextColor(hdc, oldColor);
        ::SetBkMode(hdc, oldMode);
        if (oldFont != NULL)
            ::SelectObject(hdc, oldFont);
    }

private:
    int   m_more;   //「+N」里的 N；0 表示不叠
    HFONT m_font;   //「+N」的字体（借用）
};

} // namespace

CThreadPostCard::CThreadPostCard(int index)
    : m_index(index)
    , m_postId(0)
    , m_selected(false)
    , m_hover(false)
    , m_pressed(false)
{
    SetBgColor(kCardBg);
    SetCornerRadius(kCardRadius);
    SetBorderWidth(kCardBorderWidth);
    SetPadding(kCardPadX, kCardPadY, kCardPadX, kCardPadY);
    SetGap(kCardGap);
    SetCtrlId(kIdPostCard);
    ApplyBorder();
}

void CThreadPostCard::Build(const PostCardData& data, HFONT bodyFont, HFONT titleFont, int innerWidth)
{
    typedef DuiLayout::Hint Hint;
    m_postId = data.m_postId;
    //各部分的高度都已由 ThreadCardMetrics 按这个宽度算好，这里不再用到它
    (void)innerWidth;

    //作者行：头像 | 名字 | 时间 | 空白 | 「置顶」
    std::unique_ptr<DuiHBox> meta(new DuiHBox());
    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetName(data.m_avatarGlyph.c_str());
    avatar->SetFallbackBgColor(data.m_avatarColor);
    if (data.m_avatarBitmap != NULL)
        avatar->SetBitmap(data.m_avatarBitmap);
    meta->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                   Hint().Fixed(kAvatarPx, kAvatarPx).Margin(0, 0, kAvatarGap, 0).AlignC(DuiLayout::AlignCenter));
    int nameWidth = MeasureTextWidthPx(bodyFont, data.m_authorName);
    if (nameWidth > kNameMaxWidth)
        nameWidth = kNameMaxWidth;
    meta->AddChild(std::unique_ptr<DuiControl>(
                       MakeTextLabel(data.m_authorName, kNameColor, bodyFont,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX).release()),
                   Hint().Fixed(nameWidth).Margin(0, 0, kMetaGap, 0));
    meta->AddChild(std::unique_ptr<DuiControl>(
                       MakeTextLabel(data.m_timeText, kFaintColor, NULL, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                   Hint().Weight(1));
    if (data.m_pinned)
    {
        int pinWidth = 0;
        std::unique_ptr<DuiVBox> pin = MakePill(TRC(L"帖子卡片标记", L"置顶"), kPinBg, kPinText, bodyFont, kPinRadius, kPinPadding, pinWidth);
        meta->AddChild(std::unique_ptr<DuiControl>(pin.release()), Hint().Fixed(pinWidth, kPinHeight).AlignC(DuiLayout::AlignCenter));
    }
    AddChild(std::unique_ptr<DuiControl>(meta.release()), Hint().Fixed(kCardMetaHeight));

    //标题（单行，过长时省略）
    if (!data.m_title.empty())
    {
        AddChild(std::unique_ptr<DuiControl>(
                     MakeTextLabel(data.m_title, kTitleColor, titleFont,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX).release()),
                 Hint().Fixed(data.m_metrics.m_titleHeight));
    }

    //正文（最多三行，已按宽度截好）
    if (!data.m_metrics.m_text.empty())
    {
        std::unique_ptr<DuiLabel> text = MakeTextLabel(data.m_metrics.m_text, kTextColor, bodyFont, DT_LEFT | DT_NOPREFIX | DT_EDITCONTROL);
        text->SetWordWrap(true);
        AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Fixed(data.m_metrics.m_textHeight));
    }

    //缩略图
    if (data.m_metrics.m_thumbCount > 0)
    {
        std::unique_ptr<DuiHBox> thumbs(new DuiHBox());
        thumbs->SetGap(kCardThumbGap);
        HFONT moreFont = titleFont;
        for (int i = 0; i < data.m_metrics.m_thumbCount; ++i)
        {
            const bool last = (i == data.m_metrics.m_thumbCount - 1);
            std::unique_ptr<CThumbTile> tile(new CThumbTile(last ? data.m_metrics.m_moreCount : 0, moreFont));
            if (i < static_cast<int>(data.m_thumbs.size()) && data.m_thumbs[i] != NULL)
                tile->SetBitmap(data.m_thumbs[i]);
            thumbs->AddChild(std::unique_ptr<DuiControl>(tile.release()), Hint().Fixed(kCardThumbPx, kCardThumbPx));
        }
        thumbs->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        AddChild(std::unique_ptr<DuiControl>(thumbs.release()), Hint().Fixed(kCardThumbPx));
    }

    //底部一行：互动标签 | 空白 | 留言数。两样都没有时不要这一行（排版时已经没算它的高度）
    if (!data.m_metrics.m_hasFoot)
        return;
    std::unique_ptr<DuiHBox> foot(new DuiHBox());
    foot->SetGap(kChipGap);
    AddReactionChips(foot.get(), data.m_chips, bodyFont);
    foot->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    if (!data.m_commentText.empty())
    {
        const int width = MeasureTextWidthPx(bodyFont, data.m_commentText);
        foot->AddChild(std::unique_ptr<DuiControl>(
                           MakeTextLabel(data.m_commentText, kFaintColor, bodyFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE).release()),
                       Hint().Fixed(width));
    }
    AddChild(std::unique_ptr<DuiControl>(foot.release()), Hint().Fixed(kCardFootHeight));
}

int64_t CThreadPostCard::PostId() const
{
    return m_postId;
}

void CThreadPostCard::SetSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    ApplyBorder();
    Invalidate();
}

bool CThreadPostCard::IsSelected() const
{
    return m_selected;
}

DuiControl* CThreadPostCard::HitTest(POINT ptHostClient)
{
    if (!IsVisible())
        return NULL;
    const RECT rc = GetRect();
    return ::PtInRect(&rc, ptHostClient) ? this : NULL;
}

bool CThreadPostCard::OnLButtonDown(POINT pt, UINT mkFlags)
{
    (void)pt;
    (void)mkFlags;
    m_pressed = true;
    return true;
}

bool CThreadPostCard::OnLButtonUp(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    const RECT rc = GetRect();
    const bool click = m_pressed && ::PtInRect(&rc, pt);
    m_pressed = false;
    //发通知之后不再访问成员：窗口处理通知时不会销毁卡片（选中延后执行），这里仍按最保守的写法放在最后
    if (click)
        NotifyParent(balloonwjui::DUIN_CLICK, static_cast<LPARAM>(m_index));
    return true;
}

bool CThreadPostCard::OnMouseEnter()
{
    DuiVBox::OnMouseEnter();
    m_hover = true;
    ApplyBorder();
    Invalidate();
    return true;
}

bool CThreadPostCard::OnMouseLeave()
{
    DuiVBox::OnMouseLeave();
    m_hover = false;
    m_pressed = false;
    ApplyBorder();
    Invalidate();
    return true;
}

void CThreadPostCard::ApplyBorder()
{
    SetBgColor(m_selected ? kCardBgSelected : kCardBg);
    if (m_selected)
        SetBorderColor(kCardBorderSelected);
    else if (m_hover)
        SetBorderColor(kCardBorderHover);
    else
        SetBorderColor(kCardBorder);
}

} // namespace threadgroup
