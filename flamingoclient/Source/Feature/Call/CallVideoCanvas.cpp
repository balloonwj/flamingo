/**
 *  通话视频画面控件实现，CallVideoCanvas.cpp
 *  balloonwj@qq.com   2026-06-16（2026-07-31 增加可点击与帧交换）
 */
#include "CallVideoCanvas.h"

#include <windows.h>
#include <algorithm>   //std::swap（大 / 小画面对调时交换帧缓冲）
#include <atlstr.h>    //DuiToolTip.h 声明里用到 CString；本 .cpp 不走 PCH，故先显式引入

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //DuiToolTipMgr
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"                      //DuiResMgr::GetDefaultFont（占位提示用字体）

namespace call {

//占位提示文字颜色：黑底上用中浅灰，既看得清又不喧宾夺主（有画面时本就不画）。
static const COLORREF kPlaceholderColor = RGB(0xC8, 0xC8, 0xC8);
//占位提示与控件左右边缘的间距（像素），避免长文字顶到边上。
static const int kPlaceholderPadding = 8;

CallVideoCanvas::CallVideoCanvas()
    : m_frameW(0)
    , m_frameH(0)
    , m_interactive(false)
    , m_pressed(false)
    , m_tooltipSet(false)
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

    //双击由宿主 DuiHost 的双击合成派发（窗口类未带 CS_DBLCLKS），此处只负责上冒通知
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

void CallVideoCanvas::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT& rc = GetRect();
    int rcW = rc.right - rc.left;
    int rcH = rc.bottom - rc.top;
    if (rcW <= 0 || rcH <= 0)
        return;

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

void CallVideoCanvas::DrawPlaceholder(HDC hdc, const RECT& rc)
{
    if (m_placeholder.IsEmpty())
        return;

    //用 balloonui 的默认字体（已按当前 DPI 构建），与窗口其余文字观感一致
    HFONT font = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
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
