/**
 *  通话视频画面控件实现，CallVideoCanvas.cpp
 *  balloonwj@qq.com   2026-06-16（2026-07-31 增加可点击与帧交换）
 */
#include "CallVideoCanvas.h"

#include <windows.h>
#include <algorithm>   //std::swap（大 / 小画面对调时交换帧缓冲）
#include <atlstr.h>    //DuiToolTip.h 声明里用到 CString；本 .cpp 不走 PCH，故先显式引入
#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>   //语音态画面用 GDI+ 画抗锯齿的圆底与听筒

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //DuiToolTipMgr
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"                      //DuiResMgr::GetDefaultFont（占位提示用字体）
#include "../../Source/UI/BalloonChatWnd/ChatIcons.h"   //DrawChatIcon（与标题栏同一枚听筒图标）
#include "CallDlgLayout.h"                              //ComputeVoiceArtLayout（语音态画面的布局，有单测）

#pragma comment(lib, "msimg32.lib")   //AlphaBlend：头像位图带透明的圆角

namespace call {

//占位提示文字颜色：黑底上用中浅灰，既看得清又不喧宾夺主（有画面时本就不画）。
static const COLORREF kPlaceholderColor = RGB(0xC8, 0xC8, 0xC8);
//占位提示与控件左右边缘的间距（像素），避免长文字顶到边上。
static const int kPlaceholderPadding = 8;

//语音态画面的配色（第 10 期 10b、第 11 期 11c）：底色比窗口底色略深，四周一圈灰色细边框，使画面区与底部按钮栏
//分界清楚；圆底用主题蓝（与原标题栏图标同色）；听筒白色；文字深灰，时长稍浅
static const COLORREF kVoiceArtBgColor      = RGB(0xE8, 0xEA, 0xEE);
static const COLORREF kVoiceArtBorderColor  = RGB(0xC9, 0xCD, 0xD4);
static const COLORREF kVoiceArtSubTextColor = RGB(0x80, 0x86, 0x90);
static const BYTE     kVoiceArtBlueR     = 0x18;
static const BYTE     kVoiceArtBlueG     = 0x6E;
static const BYTE     kVoiceArtBlueB     = 0xDC;
static const COLORREF kVoiceArtTextColor = RGB(0x5A, 0x5F, 0x6A);
//听筒图标边长占圆底直径的比例（分子 / 分母），以及描边宽度（以图标 viewBox 24 为基准）
static const int            kVoiceArtIconNum    = 1;
static const int            kVoiceArtIconDen    = 2;
static const Gdiplus::REAL  kVoiceArtIconStroke = 2.0f;
//GDI+ 颜色的不透明 alpha
static const BYTE kOpaqueAlpha = 255;

CallVideoCanvas::CallVideoCanvas()
    : m_frameW(0)
    , m_frameH(0)
    , m_interactive(false)
    , m_pressed(false)
    , m_tooltipSet(false)
    , m_voiceArt(false)
    , m_avatar(NULL)
    , m_floating(false)
    , m_floatLeft(0)
    , m_floatBottom(0)
    , m_floatW(0)
    , m_floatH(0)
{
}

CallVideoCanvas::~CallVideoCanvas()
{
    //控件先于 DuiToolTipMgr 析构，必须在这里摘掉注册，否则管理器里留下悬空控件指针
    if (m_tooltipSet)
        balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void CallVideoCanvas::SetFrame(const uint8_t* bgra, int w, int h)
{
    //UI 线程上下文：拷一份最近帧。无效入参时清空（回到「无帧」黑底态）。
    if (bgra == NULL || w <= 0 || h <= 0)
    {
        m_frame.clear();
        m_frameW = 0;
        m_frameH = 0;
        return;
    }

    size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    m_frame.assign(bgra, bgra + bytes);
    m_frameW = w;
    m_frameH = h;
}

void CallVideoCanvas::SwapFrame(CallVideoCanvas& other)
{
    //UI 线程上下文：整块帧缓冲与尺寸一起换，两边画面即时互换（一路暂时没画面时换过来就是黑底）
    std::swap(m_frame, other.m_frame);
    std::swap(m_frameW, other.m_frameW);
    std::swap(m_frameH, other.m_frameH);
}

void CallVideoCanvas::SetInteractive(bool interactive)
{
    m_interactive = interactive;
    //从可交互切回纯显示时清掉按下态，避免残留一个"半按下"的状态
    if (!m_interactive)
        m_pressed = false;
}

void CallVideoCanvas::SetTooltipText(LPCTSTR text)
{
    if (text == NULL || text[0] == _T('\0'))
    {
        if (m_tooltipSet)
        {
            balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
            m_tooltipSet = false;
        }
        return;
    }

    //重复 Register 同一控件会覆盖文字，无需先 Unregister
    balloonwjui::DuiToolTipMgr::Inst().Register(this, text);
    m_tooltipSet = true;
}

void CallVideoCanvas::SetPlaceholderText(LPCTSTR text)
{
    if (text == NULL)
        m_placeholder.Empty();
    else
        m_placeholder = text;
}

bool CallVideoCanvas::OnLButtonDown(POINT /*pt*/, UINT /*mkFlags*/)
{
    if (!m_interactive)
        return false;

    //按下即捕获鼠标：与 DuiButton 同策略，按下 → 抬起都落在本控件内才算一次点击
    m_pressed = true;
    Capture();
    return true;
}

bool CallVideoCanvas::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    if (!m_interactive)
        return false;

    bool wasPressed = m_pressed;
    m_pressed = false;
    ReleaseCapture();

    //抬起时鼠标仍在控件内才算点击（按下后拖出去再放开视为取消）
    const RECT& rc = GetRect();
    bool inside = (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom);
    if (wasPressed && inside)
        NotifyParent(balloonwjui::DUIN_CLICK);

    return true;
}

bool CallVideoCanvas::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    if (!m_interactive)
        return false;

    //双击由宿主 DuiHost 转来（窗口类带 CS_DBLCLKS，系统直接投递 WM_LBUTTONDBLCLK），此处只负责上冒通知
    m_pressed = false;
    NotifyParent(balloonwjui::DUIN_DBLCLK);
    return true;
}

bool CallVideoCanvas::OnSetCursor(POINT /*pt*/)
{
    if (!m_interactive)
        return false;

    //可点击时给手型光标，提示"这块画面能点"
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

void CallVideoCanvas::SetBannerText(LPCTSTR text)
{
    if (text == NULL)
        m_banner.Empty();
    else
        m_banner = text;
}

void CallVideoCanvas::SetCornerText(LPCTSTR text)
{
    if (text == NULL)
        m_cornerText.Empty();
    else
        m_cornerText = text;
}

void CallVideoCanvas::SetFloatingBottomLeft(int marginLeft, int marginBottom, int width, int height)
{
    m_floating = (width > 0 && height > 0);
    m_floatLeft = marginLeft;
    m_floatBottom = marginBottom;
    m_floatW = width;
    m_floatH = height;
}

void CallVideoCanvas::Layout(const RECT& rcAvail)
{
    balloonwjui::DuiControl* parent = GetParent();
    if (!m_floating || parent == NULL)
    {
        balloonwjui::DuiControl::Layout(rcAvail);
        return;
    }
    //父布局给的是 Fixed(0) 的矩形，不是小画面该在的位置：改按父容器的整个矩形放到左下角
    const RECT& base = parent->GetRect();
    RECT rc;
    rc.left = base.left + m_floatLeft;
    rc.right = rc.left + m_floatW;
    rc.bottom = base.bottom - m_floatBottom;
    rc.top = rc.bottom - m_floatH;
    //基类的 Layout 不改写本控件的矩形（父布局经 SetRect 写入的是 Fixed(0) 的矩形），这里直接写入，并重绘新位置
    if (!::EqualRect(&m_rcItem, &rc))
    {
        m_rcItem = rc;
        Invalidate();
    }
    balloonwjui::DuiControl::Layout(rc);
}

void CallVideoCanvas::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT& rc = GetRect();
    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    PaintBody(hdc, rc);
    if (!m_banner.IsEmpty())
        DrawBanner(hdc, rc);
    if (!m_cornerText.IsEmpty())
        DrawCorner(hdc, rc);
    if (m_floating)
    {
        HBRUSH border = ::CreateSolidBrush(kCanvasFloatingBorderColor);
        ::FrameRect(hdc, &rc, border);
        ::DeleteObject(border);
    }
}

void CallVideoCanvas::DrawCorner(HDC hdc, const RECT& rc)
{
    HFONT font = GetDefaultFont();
    HFONT oldFont = NULL;
    if (font != NULL)
        oldFont = static_cast<HFONT>(::SelectObject(hdc, font));
    SIZE textSize = { 0, 0 };
    ::GetTextExtentPoint32(hdc, m_cornerText, m_cornerText.GetLength(), &textSize);

    //胶囊贴右上角；顶部提示条显示时排在提示条下面
    RECT pill;
    pill.right = rc.right - kCanvasCornerMargin;
    pill.left = pill.right - (textSize.cx + kCanvasCornerPadX * 2);
    pill.top = rc.top + kCanvasCornerMargin + (m_banner.IsEmpty() ? 0 : kCanvasBannerH);
    pill.bottom = pill.top + kCanvasCornerH;
    //画面太小放不下（如小画面）时不画
    if (pill.left < rc.left || pill.bottom > rc.bottom)
    {
        if (oldFont != NULL)
            ::SelectObject(hdc, oldFont);
        return;
    }

    {
        //两端为半圆的圆角矩形：半径取高度的一半
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(kCanvasCornerH);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(pill.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(pill.top);
        const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(pill.right - pill.left);
        const Gdiplus::REAL kHalfTurn = 180.0f;   //GDI+ 画圆弧的扫过角度：半圆
        const Gdiplus::REAL kUpAngle = 90.0f;     //左半圆从正下方（90 度）开始画
        const Gdiplus::REAL kDownAngle = 270.0f;  //右半圆从正上方（270 度）开始画
        Gdiplus::GraphicsPath path;
        path.AddArc(x, y, d, d, kUpAngle, kHalfTurn);
        path.AddArc(x + w - d, y, d, d, kDownAngle, kHalfTurn);
        path.CloseFigure();
        Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(kCanvasCornerBgAlpha), 0, 0, 0));
        g.FillPath(&brush, &path);
    }

    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, kCanvasCornerTextColor);
    ::DrawText(hdc, m_cornerText, -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
}

void CallVideoCanvas::DrawBanner(HDC hdc, const RECT& rc)
{
    RECT bar = rc;
    if (bar.bottom - bar.top > kCanvasBannerH)
        bar.bottom = bar.top + kCanvasBannerH;
    HBRUSH br = ::CreateSolidBrush(kCanvasBannerBgColor);
    ::FillRect(hdc, &bar, br);
    ::DeleteObject(br);

    //文字用 balloonui 的默认字体（按本控件所在窗口的 DPI 取），左右留与占位提示相同的边距
    HFONT font = GetDefaultFont();
    HFONT oldFont = NULL;
    if (font != NULL)
        oldFont = static_cast<HFONT>(::SelectObject(hdc, font));
    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, kCanvasBannerTextColor);
    RECT text = bar;
    text.left += kPlaceholderPadding;
    text.right -= kPlaceholderPadding;
    ::DrawText(hdc, m_banner, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
}

void CallVideoCanvas::PaintBody(HDC hdc, const RECT& rc)
{
    int rcW = rc.right - rc.left;
    int rcH = rc.bottom - rc.top;

    //语音态：不画画面帧，画浅灰底与听筒图标
    if (m_voiceArt)
    {
        DrawVoiceArt(hdc, rc);
        return;
    }

    //先铺黑底（letterbox 的黑边 / 尚无帧时的占位）
    HBRUSH br = ::CreateSolidBrush(RGB(0, 0, 0));
    ::FillRect(hdc, &rc, br);
    ::DeleteObject(br);

    //尚无帧：黑底 + 居中占位提示（如"正在等待对方的画面…""摄像头不可用"），等待首帧
    if (m_frameW <= 0 || m_frameH <= 0 || m_frame.empty())
    {
        DrawPlaceholder(hdc, rc);
        return;
    }

    //等比计算居中目标矩形（letterbox）：按宽高比取较小缩放因子，画面完整不变形
    double scale = static_cast<double>(rcW) / m_frameW;
    double scaleH = static_cast<double>(rcH) / m_frameH;
    if (scaleH < scale)
        scale = scaleH;

    int dstW = static_cast<int>(m_frameW * scale + 0.5);
    int dstH = static_cast<int>(m_frameH * scale + 0.5);
    if (dstW <= 0 || dstH <= 0)
        return;

    int dstX = rc.left + (rcW - dstW) / 2;
    int dstY = rc.top + (rcH - dstH) / 2;

    //自上而下 DIB（biHeight 取负）；半色调缩放更平滑
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = m_frameW;
    bi.bmiHeader.biHeight      = -m_frameH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    ::StretchDIBits(hdc,
                    dstX, dstY, dstW, dstH,
                    0, 0, m_frameW, m_frameH,
                    m_frame.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

void CallVideoCanvas::SetVoiceArt(bool on)
{
    m_voiceArt = on;
}

void CallVideoCanvas::SetVoiceSubText(LPCTSTR text)
{
    m_voiceSubText = (text != NULL) ? text : _T("");
}

void CallVideoCanvas::SetAvatar(HBITMAP avatar)
{
    m_avatar = avatar;
}

void CallVideoCanvas::DrawAvatar(HDC hdc, const RECT& rc)
{
    if (m_avatar == NULL || rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    BITMAP bm;
    if (::GetObject(m_avatar, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return;
    HDC mem = ::CreateCompatibleDC(hdc);
    if (mem == NULL)
        return;
    HGDIOBJ old = ::SelectObject(mem, m_avatar);
    //头像位图是 32 位预乘 alpha（圆形以外透明），按透明度缩放画上去
    BLENDFUNCTION blend;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = kOpaqueAlpha;
    blend.AlphaFormat = AC_SRC_ALPHA;
    ::SetStretchBltMode(hdc, HALFTONE);
    ::AlphaBlend(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, bm.bmWidth, bm.bmHeight,
                 blend);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
}

void CallVideoCanvas::DrawVoiceArt(HDC hdc, const RECT& rc)
{
    HBRUSH bg = ::CreateSolidBrush(kVoiceArtBgColor);
    ::FillRect(hdc, &rc, bg);
    ::DeleteObject(bg);
    //四周一圈 1 像素边框
    HBRUSH border = ::CreateSolidBrush(kVoiceArtBorderColor);
    ::FrameRect(hdc, &rc, border);
    ::DeleteObject(border);

    const call::VoiceArtLayout l = call::ComputeVoiceArtLayout(rc);
    const int d = l.m_circle.right - l.m_circle.left;
    //设了头像：画对方的头像，不画听筒（docs/音视频通话优化方案.md 第 5 项）
    if (d > 0 && m_avatar != NULL)
    {
        DrawAvatar(hdc, l.m_circle);
    }
    else if (d > 0)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush blue(Gdiplus::Color(kOpaqueAlpha, kVoiceArtBlueR, kVoiceArtBlueG, kVoiceArtBlueB));
        g.FillEllipse(&blue, static_cast<Gdiplus::REAL>(l.m_circle.left), static_cast<Gdiplus::REAL>(l.m_circle.top),
                      static_cast<Gdiplus::REAL>(d), static_cast<Gdiplus::REAL>(d));
        //听筒放在圆底正中，边长取直径的一半
        const Gdiplus::REAL icon = static_cast<Gdiplus::REAL>(d * kVoiceArtIconNum / kVoiceArtIconDen);
        const Gdiplus::REAL x = l.m_circle.left + (d - icon) / 2;
        const Gdiplus::REAL y = l.m_circle.top + (d - icon) / 2;
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconPhone, x, y, icon,
                                     Gdiplus::Color(kOpaqueAlpha, kOpaqueAlpha, kOpaqueAlpha, kOpaqueAlpha),
                                     kVoiceArtIconStroke);
    }

    if (m_placeholder.IsEmpty() && m_voiceSubText.IsEmpty())
        return;
    HFONT font = GetDefaultFont();
    HFONT oldFont = NULL;
    if (font != NULL)
        oldFont = static_cast<HFONT>(::SelectObject(hdc, font));
    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, kVoiceArtTextColor);
    RECT rcText = l.m_text;
    ::DrawText(hdc, m_placeholder, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    //通话时长（第 11 期 11b：从标题栏挪到这里）
    if (!m_voiceSubText.IsEmpty())
    {
        ::SetTextColor(hdc, kVoiceArtSubTextColor);
        RECT rcSub = l.m_subText;
        ::DrawText(hdc, m_voiceSubText, -1, &rcSub, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
}

void CallVideoCanvas::DrawPlaceholder(HDC hdc, const RECT& rc)
{
    //设了头像：头像在上、提示文字在下（比语音态画面紧凑，小画面里头像也够大）
    if (m_avatar != NULL)
    {
        const call::VoiceArtLayout l = call::ComputeAvatarTipLayout(rc);
        DrawAvatar(hdc, l.m_circle);
        if (m_placeholder.IsEmpty())
            return;
        HFONT avatarFont = GetDefaultFont();
        HFONT avatarOldFont = NULL;
        if (avatarFont != NULL)
            avatarOldFont = static_cast<HFONT>(::SelectObject(hdc, avatarFont));
        const int avatarBkMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF avatarColor = ::SetTextColor(hdc, kPlaceholderColor);
        RECT rcText = l.m_text;
        ::DrawText(hdc, m_placeholder, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ::SetTextColor(hdc, avatarColor);
        ::SetBkMode(hdc, avatarBkMode);
        if (avatarOldFont != NULL)
            ::SelectObject(hdc, avatarOldFont);
        return;
    }
    if (m_placeholder.IsEmpty())
        return;

    //用 balloonui 的默认字体（按本控件所在窗口的 DPI 取），与窗口其余文字观感一致
    HFONT font = GetDefaultFont();
    HFONT oldFont = NULL;
    if (font != NULL)
        oldFont = static_cast<HFONT>(::SelectObject(hdc, font));

    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, kPlaceholderColor);

    //先按控件宽度（去掉左右间距）量出文字要占的高度，再据此把整块文字在竖直方向摆中间。
    //DT_VCENTER 只对单行有效，故这里自己算，多行提示（含 '\n'）也能居中。
    RECT rcAvail = rc;
    ::InflateRect(&rcAvail, -kPlaceholderPadding, 0);

    RECT rcMeasure = rcAvail;
    ::DrawText(hdc, m_placeholder, -1, &rcMeasure, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
    int textH = rcMeasure.bottom - rcMeasure.top;

    RECT rcText = rcAvail;
    rcText.top = rc.top + ((rc.bottom - rc.top) - textH) / 2;
    if (rcText.top < rc.top)
        rcText.top = rc.top;
    rcText.bottom = rc.bottom;
    ::DrawText(hdc, m_placeholder, -1, &rcText, DT_CENTER | DT_WORDBREAK);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
}

} // namespace call
