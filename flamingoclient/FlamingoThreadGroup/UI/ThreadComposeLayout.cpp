/**
 *  发帖窗口的控件树，见 ThreadComposeLayout.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadComposeLayout.h"

#include <gdiplus.h>

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../Core/ThreadComposeModel.h"   // ComposeImageState
#include "../Core/ThreadRules.h"          // TextLimitOf / ComposeKind
#include "../Core/ThreadTypes.h"          // kTitleMaxChars / kMaxImages / kMaxAddenda
#include "ThreadCardMetrics.h"            // MeasureWrappedHeight
#include "ThreadCommentEdit.h"
#include "ThreadWidgets.h"                // MakeTextLabel / MeasureTextWidthPx / CThreadClickPill

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素（样稿第 0 期） ----
const int kPadTop = 14;             //内容区上内边距
const int kPadBottom = 16;          //内容区下内边距
const int kGap = 10;                //上下相邻两行之间的距离
const int kTitleHeight = 34;        //标题输入框高
const int kTitleCountPad = 16;      //标题框右侧字数栏比文字多出的宽度（两侧留白）
const int kEditMargin = 8;          //正文输入框文字与边框之间的内边距
const int kTileGap = 8;             //格子之间的距离
const int kTileRadius = 6;          //格子圆角
const int kRemoveDiameter = 18;     //缩略图右上角「✕」圆的直径
const int kRemoveInset = 3;         //「✕」圆离格子上边、右边的距离
const float kRemoveCrossHalf = 4.0f;//「✕」两笔各自的半长
const float kRemovePenWidth = 1.5f; //「✕」笔画宽
const int kToolRowHeight = 26;      //工具行高
const int kToolIconPx = 16;         //工具按钮的图标边长
const int kToolIconGap = 4;         //工具按钮图标与文字之间的距离
const int kToolButtonPad = 8;       //工具按钮文字两侧的内边距
const int kToolGap = 6;             //工具按钮之间的距离
const int kMentionChipHeight = 22;  //名单标签高
const int kMentionChipPad = 8;      //名单标签文字两侧内边距
const int kMentionChipGap = 6;      //名单标签之间的距离
const int kMentionChipRadius = 11;  //名单标签圆角（高的一半，两端为半圆）
const int kFooterHeight = 32;       //底部一行高
const int kButtonPad = 16;          //底部按钮文字两侧内边距
const int kMinButtonWidth = 72;     //底部按钮最窄的宽度
const int kFooterGap = 8;           //底部两个按钮之间的距离
const BYTE kUploadingAlpha = 110;   //上传中的那层黑色的不透明度（0~255）
const BYTE kFailedAlpha = 150;      //上传失败的那层红色的不透明度（0~255）
const BYTE kRemoveAlpha = 140;      //「✕」圆底色的不透明度（0~255）
const Gdiplus::REAL kQuarterTurn = 90.0f;      //圆角路径每段弧的角度
const Gdiplus::REAL kHalfTurn = 180.0f;        //左上角弧的起始角
const Gdiplus::REAL kThreeQuarterTurn = 270.0f;//右上角弧的起始角

//---- 颜色（样稿第 0 期） ----
const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);               //内容区底色
const COLORREF kHintColor = RGB(0x6B, 0x6B, 0x6B);        //追加补充的说明
const COLORREF kFaint = RGB(0x9A, 0x9A, 0x9A);            //字数
const COLORREF kErrorColor = RGB(0xD9, 0x53, 0x4F);       //出错说明
const COLORREF kEditText = RGB(0x1A, 0x1A, 0x1A);         //输入的文字
const COLORREF kToolText = RGB(0x55, 0x55, 0x55);         //工具按钮文字
const COLORREF kTilePlaceholder = RGB(0xEE, 0xEC, 0xE8);  //缩略图还没读出来时的占位底色
const COLORREF kAddBg = RGB(0xF7, 0xF6, 0xF3);            //「＋ 图片」格子底色
const COLORREF kAddBgHover = RGB(0xEF, 0xED, 0xE9);       //「＋ 图片」格子悬停底色
const COLORREF kMentionBg = RGB(0xEE, 0xF3, 0xFC);        //名单标签底色
const COLORREF kMentionBgHover = RGB(0xE0, 0xE9, 0xF8);   //名单标签悬停底色
const COLORREF kMentionTextColor = RGB(0x2F, 0x6F, 0xD0);      //名单标签文字
const COLORREF kOverlayText = RGB(0xFF, 0xFF, 0xFF);      //缩略图上说明文字与「✕」的颜色
const BYTE kOpaque = 0xFF;          //GDI+ 颜色里「完全不透明」的 alpha
const BYTE kFailedRed = 0xC0;       //上传失败那层颜色的红色分量（另两个分量为 0x30）
const BYTE kFailedOther = 0x30;     //上传失败那层颜色的绿、蓝分量

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kAddendumHint = TR_NOOP(L"补充会显示在帖子正文之后，带上时间；发出后不能修改，可以删除。留过言的人会收到提醒。");
const wchar_t* const kTitlePlaceholder = TR_NOOP(L"标题（可选）");
const wchar_t* const kPostPlaceholder = TR_NOOP(L"写下帖子内容…（Ctrl+Enter 发布）");
const wchar_t* const kAddendumPlaceholder = TR_NOOP(L"写下补充内容…（Ctrl+Enter 补充）");
const wchar_t* const kFaceText = TR_NOOP(L"表情");
const wchar_t* const kImageText = TR_NOOP(L"图片");
const wchar_t* const kMentionText = TR_NOOP(L"@ 成员");
const wchar_t* const kCancelText = TR_NOOP(L"取消");
const wchar_t* const kPublishText = TR_NOOP(L"发布");
const wchar_t* const kSaveText = TR_NOOP(L"保存");
const wchar_t* const kAddendumText = TR_NOOP(L"补充");
const wchar_t* const kCountPattern = TR_NOOP(L"{0} / {1}");
const wchar_t* const kAddendumCountPattern = TR_NOOP(L"{0} / {1} · 第 {2} 段，最多 {3} 段");
const wchar_t* const kUploadingText = TR_NOOP(L"上传中");
const wchar_t* const kRetryText = TR_NOOP(L"重试");
const wchar_t* const kAddPlus = TR_NOOP(L"＋");
const wchar_t* const kMentionChipPattern = TR_NOOP(L"@{0} ✕");
const wchar_t* const kMentionMorePattern = TR_NOOP(L"等 {0} 人");

typedef DuiLayout::Hint Hint;

//一个格子的圆角矩形路径（GDI+）
void AddRoundRect(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(2 * radius);
    const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rc.left);
    const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rc.top);
    const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(rc.right - rc.left);
    const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(rc.bottom - rc.top);
    path.AddArc(x, y, d, d, kHalfTurn, kQuarterTurn);
    path.AddArc(x + w - d, y, d, d, kThreeQuarterTurn, kQuarterTurn);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, kQuarterTurn);
    path.AddArc(x, y + h - d, d, d, kQuarterTurn, kQuarterTurn);
    path.CloseFigure();
}

/**
 *  发帖窗口里的一张缩略图：圆角 DuiAvatar；上传中、上传失败时叠一层说明；右上角画「✕」。
 *  单击时以 NotifyParent(DUIN_CLICK, ComposeThumbClick) 通知窗口：点在「✕」上为删除，点在别处为重试（只对上传失败的图有意义）。
 *  只覆写绘制、命中与鼠标按键（子类化扩展，不改 balloonui）。
 */
class CComposeThumb : public DuiAvatar
{
public:
    /**
     *  @param state 状态，取值见 ComposeImageState
     *  @param font  说明文字的字体（借用）
     */
    CComposeThumb(int state, HFONT font)
        : m_state(state)
        , m_font(font)
        , m_pressed(false)
    {
        SetShape(DuiAvatar::ShapeRoundRect);
        SetCornerRadius(kTileRadius);
        SetFallbackBgColor(kTilePlaceholder);
    }

    /**
     *  绘制：先由 DuiAvatar 画图片（或占位底色），再叠说明与「✕」。
     *  @param hdc     宿主的背缓冲 DC
     *  @param rcDirty 需要重画的区域
     */
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        DuiAvatar::OnPaint(hdc, rcDirty);
        if (!IsVisible())
            return;
        const RECT rc = GetRect();
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        //上传中、上传失败：整张图叠一层颜色，中间写说明
        if (m_state == compose_image_uploading || m_state == compose_image_failed)
        {
            Gdiplus::GraphicsPath path;
            AddRoundRect(path, rc, kTileRadius);
            const Gdiplus::Color shade = (m_state == compose_image_uploading)
                ? Gdiplus::Color(kUploadingAlpha, 0, 0, 0)
                : Gdiplus::Color(kFailedAlpha, kFailedRed, kFailedOther, kFailedOther);
            Gdiplus::SolidBrush brush(shade);
            g.FillPath(&brush, &path);
            const std::wstring text = TR(m_state == compose_image_uploading ? kUploadingText : kRetryText);
            HGDIOBJ oldFont = (m_font != NULL) ? ::SelectObject(hdc, m_font) : NULL;
            const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
            const COLORREF oldColor = ::SetTextColor(hdc, kOverlayText);
            RECT textRc = rc;
            ::DrawTextW(hdc, text.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SetTextColor(hdc, oldColor);
            ::SetBkMode(hdc, oldMode);
            if (oldFont != NULL)
                ::SelectObject(hdc, oldFont);
        }
        //右上角的「✕」：半透明黑色圆底、两笔白线
        const RECT x = RemoveRect();
        Gdiplus::SolidBrush round(Gdiplus::Color(kRemoveAlpha, 0, 0, 0));
        g.FillEllipse(&round, static_cast<Gdiplus::REAL>(x.left), static_cast<Gdiplus::REAL>(x.top),
                      static_cast<Gdiplus::REAL>(kRemoveDiameter), static_cast<Gdiplus::REAL>(kRemoveDiameter));
        const Gdiplus::REAL cx = static_cast<Gdiplus::REAL>(x.left) + kRemoveDiameter / 2.0f;
        const Gdiplus::REAL cy = static_cast<Gdiplus::REAL>(x.top) + kRemoveDiameter / 2.0f;
        Gdiplus::Pen pen(Gdiplus::Color(kOpaque, GetRValue(kOverlayText), GetGValue(kOverlayText), GetBValue(kOverlayText)),
                         kRemovePenWidth);
        g.DrawLine(&pen, cx - kRemoveCrossHalf, cy - kRemoveCrossHalf, cx + kRemoveCrossHalf, cy + kRemoveCrossHalf);
        g.DrawLine(&pen, cx - kRemoveCrossHalf, cy + kRemoveCrossHalf, cx + kRemoveCrossHalf, cy - kRemoveCrossHalf);
    }

    //点在格子里时返回自己
    virtual DuiControl* HitTest(POINT ptHostClient) override
    {
        if (!IsVisible())
            return NULL;
        const RECT rc = GetRect();
        return ::PtInRect(&rc, ptHostClient) ? this : NULL;
    }

    //按下：记下按在了格子上
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override
    {
        (void)pt;
        (void)mkFlags;
        m_pressed = true;
        return true;
    }

    //抬起：按下与抬起都在格子上时发出单击通知，extra 区分点的是「✕」还是图本身
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override
    {
        (void)mkFlags;
        const RECT rc = GetRect();
        const bool click = m_pressed && ::PtInRect(&rc, pt);
        m_pressed = false;
        if (!click)
            return true;
        const RECT x = RemoveRect();
        const LPARAM where = ::PtInRect(&x, pt) ? compose_thumb_remove : compose_thumb_body;
        NotifyParent(balloonwjui::DUIN_CLICK, where);
        return true;
    }

private:
    //「✕」圆所在的矩形（宿主客户区坐标）
    RECT RemoveRect() const
    {
        const RECT rc = GetRect();
        RECT x;
        x.right = rc.right - kRemoveInset;
        x.left = x.right - kRemoveDiameter;
        x.top = rc.top + kRemoveInset;
        x.bottom = x.top + kRemoveDiameter;
        return x;
    }

private:
    int   m_state;     //状态，取值见 ComposeImageState
    HFONT m_font;      //说明文字的字体（借用）
    bool  m_pressed;   //鼠标是否在格子上按下（还没抬起）
};

//去掉一个容器里的全部子控件（逐个 RemoveChild，布局容器会一并去掉它们的排版设置）
void RemoveAllChildren(DuiControl* box)
{
    while (!box->Children().empty())
        box->RemoveChild(box->Children().back().get());
}

//正好放下文字的底部按钮宽度
int FooterButtonWidth(HFONT font, const std::wstring& text)
{
    const int width = MeasureTextWidthPx(font, text) + 2 * kButtonPad;
    return (width > kMinButtonWidth) ? width : kMinButtonWidth;
}

//建一个按钮
std::unique_ptr<DuiButton> MakeButton(const std::wstring& text, UINT id, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text.c_str());
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetTabStop(false);
    return button;
}

//建一个工具按钮（Ghost 样式，左侧留出图标位置；图标位图由窗口建好后设置）
std::unique_ptr<DuiButton> MakeToolButton(const std::wstring& text, UINT id, bool withIcon, HFONT font, int& width)
{
    std::unique_ptr<DuiButton> button = MakeButton(text, id, DuiButton::Variant::Ghost);
    button->SetTextColor(kToolText);
    button->SetLeadingIconSize(kToolIconPx);
    button->SetLeadingIconGap(kToolIconGap);
    width = MeasureTextWidthPx(font, text) + 2 * kToolButtonPad + (withIcon ? kToolIconPx + kToolIconGap : 0);
    return button;
}

} // namespace

//======================== CThreadComposeImageRow ========================

CThreadComposeImageRow::CThreadComposeImageRow()
{
    SetGap(kTileGap);
}

int CThreadComposeImageRow::TileSize(int count, int rowWidth)
{
    if (count <= 0)
        return kComposeImageRowHeight;
    const int fit = (rowWidth - kTileGap * (count - 1)) / count;
    return (fit < kComposeImageRowHeight) ? fit : kComposeImageRowHeight;
}

void CThreadComposeImageRow::Rebuild(const std::vector<ComposeTileView>& tiles, bool showAdd, const std::wstring& addText,
                                     HFONT font, int rowWidth)
{
    RemoveAllChildren(this);
    const int count = static_cast<int>(tiles.size()) + (showAdd ? 1 : 0);
    const int tile = TileSize(count, rowWidth);
    for (size_t i = 0; i < tiles.size() && i <= static_cast<size_t>(kIdComposeThumbLast - kIdComposeThumbBase); ++i)
    {
        std::unique_ptr<CComposeThumb> thumb(new CComposeThumb(tiles[i].m_state, font));
        thumb->SetBitmap(tiles[i].m_bitmap);
        thumb->SetCtrlId(kIdComposeThumbBase + static_cast<UINT>(i));
        AddChild(std::unique_ptr<DuiControl>(thumb.release()), Hint().Fixed(tile, tile).AlignC(DuiLayout::AlignCenter));
    }
    if (showAdd)
    {
        //「＋ 图片 n/9」：上半格是「＋」，下半格是张数
        std::unique_ptr<CThreadClickPill> add(new CThreadClickPill(kAddBg, kAddBgHover));
        add->SetCornerRadius(kTileRadius);
        add->SetCtrlId(kIdComposeAddImage);
        add->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(TR(kAddPlus), kFaint, font,
                                                                DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX).release()),
                      Hint().Weight(1));
        add->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(addText, kFaint, font,
                                                                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX).release()),
                      Hint().Weight(1));
        AddChild(std::unique_ptr<DuiControl>(add.release()), Hint().Fixed(tile, tile).AlignC(DuiLayout::AlignCenter));
    }
    //内容换了一批：按当前矩形重排（矩形没变时外层重排不会再排这一行）
    ForceLayout(GetRect());
    Invalidate();
}

//======================== CThreadMentionRow ========================

CThreadMentionRow::CThreadMentionRow()
{
    SetGap(kMentionChipGap);
}

void CThreadMentionRow::Rebuild(const std::vector<std::wstring>& names, HFONT font, int rowWidth)
{
    RemoveAllChildren(this);
    //逐个放标签；放不下时最后留出「等 N 人」的位置
    int used = 0;
    size_t shown = 0;
    const size_t maxChips = static_cast<size_t>(kIdComposeMentionLast - kIdComposeMentionBase + 1);
    for (size_t i = 0; i < names.size() && i < maxChips; ++i)
    {
        const std::wstring text = TRF(kMentionChipPattern, names[i]);
        const int width = MeasureTextWidthPx(font, text) + 2 * kMentionChipPad;
        const size_t rest = names.size() - i - 1;
        const int moreWidth = (rest > 0) ? MeasureTextWidthPx(font, TRF(kMentionMorePattern, static_cast<int>(rest))) + kMentionChipGap : 0;
        const int needed = used + (used > 0 ? kMentionChipGap : 0) + width;
        if (needed + moreWidth > rowWidth)
            break;
        std::unique_ptr<CThreadClickPill> chip(new CThreadClickPill(kMentionBg, kMentionBgHover));
        chip->SetCornerRadius(kMentionChipRadius);
        chip->SetCtrlId(kIdComposeMentionBase + static_cast<UINT>(i));
        chip->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(text, kMentionTextColor, font,
                                                                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX).release()),
                       Hint().Weight(1));
        AddChild(std::unique_ptr<DuiControl>(chip.release()),
                 Hint().Fixed(width, kMentionChipHeight).AlignC(DuiLayout::AlignCenter));
        used = needed;
        ++shown;
    }
    if (shown < names.size())
    {
        const std::wstring more = TRF(kMentionMorePattern, static_cast<int>(names.size() - shown));
        AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(more, kFaint, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                 Hint().Fixed(MeasureTextWidthPx(font, more), kMentionChipHeight).AlignC(DuiLayout::AlignCenter));
    }
    ForceLayout(GetRect());
    Invalidate();
}

//======================== 控件树 ========================

std::unique_ptr<DuiVBox> BuildComposeBody(int mode, HFONT font, ComposeControls& out)
{
    out = ComposeControls();
    const bool addendum = (mode == compose_mode_addendum);
    const int innerWidth = kComposeWindowWidth - 2 * kComposePadX;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBg);
    root->SetPadding(kComposePadX, kPadTop, kComposePadX, kPadBottom);
    root->SetGap(kGap);

    //追加补充：最上面一行说明（按内容区宽度自动换行，高度按换行后的行数定）
    if (addendum)
    {
        const std::wstring hintText = TR(kAddendumHint);
        std::unique_ptr<DuiLabel> hint = MakeTextLabel(hintText, kHintColor, font, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        out.m_hint = hint.get();
        HDC hdc = ::GetDC(NULL);
        HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
        const int hintHeight = MeasureWrappedHeight(hdc, hintText, innerWidth);
        if (oldFont != NULL)
            ::SelectObject(hdc, oldFont);
        ::ReleaseDC(NULL, hdc);
        root->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Fixed(hintHeight));
    }
    else
    {
        //标题输入框：右侧内嵌字数
        std::unique_ptr<DuiEdit> title(new DuiEdit());
        title->SetCtrlId(kIdComposeTitle);
        title->SetPlaceholder(TR(kTitlePlaceholder));
        title->SetTextColor(kEditText);
        if (font != NULL)
            title->SetDefaultFontFromHFONT(font);
        const std::wstring widest = TRF(kCountPattern, kTitleMaxChars, kTitleMaxChars);
        title->SetIconGlyph(DuiEdit::RightIcon, MeasureTextWidthPx(font, widest) + kTitleCountPad,
                            TRF(kCountPattern, 0, kTitleMaxChars).c_str(), kFaint);
        out.m_title = title.get();
        root->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(kTitleHeight));
    }

    //正文输入框：Enter 换行、Ctrl+Enter 发布；只接受纯文本粘贴（帖子只有文字、表情与图片，图片走「图片」按钮）
    std::unique_ptr<CThreadCommentEdit> body(new CThreadCommentEdit());
    body->SetCtrlId(kIdComposeBody);
    body->SetMultiLine(true);
    body->SetWordWrap(true);
    body->SetAutoGrow(false);
    body->SetShowBorder(true);
    body->SetTextColor(kEditText);
    body->SetMargins(kEditMargin, kEditMargin, kEditMargin, kEditMargin);
    body->SetPlaceholder(TR(addendum ? kAddendumPlaceholder : kPostPlaceholder));
    body->SetPasteAsPlainTextDefault(true);
    body->SetEnterSends(false);
    if (font != NULL)
        body->SetDefaultFontFromHFONT(font);
    out.m_body = body.get();
    root->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));

    //图片行
    std::unique_ptr<CThreadComposeImageRow> images(new CThreadComposeImageRow());
    out.m_images = images.get();
    root->AddChild(std::unique_ptr<DuiControl>(images.release()), Hint().Fixed(kComposeImageRowHeight));

    //工具行：「表情」「图片」「@ 成员」，右侧字数
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    int width = 0;
    std::unique_ptr<DuiButton> face = MakeToolButton(TR(kFaceText), kIdComposeFace, true, font, width);
    out.m_btnFace = face.get();
    tools->AddChild(std::unique_ptr<DuiControl>(face.release()), Hint().Fixed(width, kToolRowHeight));
    std::unique_ptr<DuiButton> image = MakeToolButton(TR(kImageText), kIdComposeImage, true, font, width);
    out.m_btnImage = image.get();
    tools->AddChild(std::unique_ptr<DuiControl>(image.release()), Hint().Fixed(width, kToolRowHeight));
    std::unique_ptr<DuiButton> mention = MakeToolButton(TR(kMentionText), kIdComposeMention, false, font, width);
    out.m_btnMention = mention.get();
    tools->AddChild(std::unique_ptr<DuiControl>(mention.release()), Hint().Fixed(width, kToolRowHeight));
    tools->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    const int kind = addendum ? compose_addendum : compose_post;
    const std::wstring widestCount = ComposeCountText(mode, TextLimitOf(kind), kMaxAddenda);
    std::unique_ptr<DuiLabel> count = MakeTextLabel(ComposeCountText(mode, 0, 1), kFaint, font,
                                                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    out.m_count = count.get();
    tools->AddChild(std::unique_ptr<DuiControl>(count.release()), Hint().Fixed(MeasureTextWidthPx(font, widestCount)));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowHeight));

    //已 @ 名单行：没有 @ 人时隐藏
    std::unique_ptr<CThreadMentionRow> mentions(new CThreadMentionRow());
    mentions->SetVisible(false);
    out.m_mentions = mentions.get();
    root->AddChild(std::unique_ptr<DuiControl>(mentions.release()), Hint().Fixed(kComposeMentionRowHeight));

    //底部一行：出错说明 | 取消 | 发布
    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetGap(kFooterGap);
    std::unique_ptr<DuiLabel> error = MakeTextLabel(std::wstring(), kErrorColor, font,
                                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    out.m_error = error.get();
    footer->AddChild(std::unique_ptr<DuiControl>(error.release()), Hint().Weight(1));
    const std::wstring cancelText = TR(kCancelText);
    std::unique_ptr<DuiButton> cancel = MakeButton(cancelText, kIdComposeCancel, DuiButton::Variant::Default);
    out.m_cancel = cancel.get();
    footer->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(FooterButtonWidth(font, cancelText), kFooterHeight));
    const std::wstring submitText = ComposeSubmitText(mode);
    std::unique_ptr<DuiButton> submit = MakeButton(submitText, kIdComposeSubmit, DuiButton::Variant::Primary);
    out.m_submit = submit.get();
    footer->AddChild(std::unique_ptr<DuiControl>(submit.release()), Hint().Fixed(FooterButtonWidth(font, submitText), kFooterHeight));
    root->AddChild(std::unique_ptr<DuiControl>(footer.release()), Hint().Fixed(kFooterHeight));
    return root;
}

int ComposeWindowHeight(int mode)
{
    return (mode == compose_mode_addendum) ? kComposeAddendumHeight : kComposePostHeight;
}

std::wstring ComposeSubmitText(int mode)
{
    switch (mode)
    {
    //编辑帖子：保存修改
    case compose_mode_edit_post:
        return TR(kSaveText);

    //追加补充
    case compose_mode_addendum:
        return TR(kAddendumText);

    //发新帖
    default:
        return TR(kPublishText);
    }
}

std::wstring ComposeCountText(int mode, int count, int addendumIndex)
{
    if (mode == compose_mode_addendum)
        return TRF(kAddendumCountPattern, count, kAddendumTextMaxChars, addendumIndex, kMaxAddenda);
    return TRF(kCountPattern, count, kPostTextMaxChars);
}

} // namespace threadgroup
