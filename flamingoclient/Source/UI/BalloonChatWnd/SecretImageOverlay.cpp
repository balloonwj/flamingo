/**
 *  SecretImageOverlay.cpp
 *
 *  密聊图片的大图浮层，说明见 SecretImageOverlay.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include <gdiplus.h>

#include "SecretImageOverlay.h"
#include "../BalloonImageViewer/ImageCanvas.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Common/I18n/I18n.h"   // TR：提示行文字
#include "../../Common/Log/IULog.h"

namespace balloonchatwnd {

namespace {

// 浮层底色：深色、约 80% 不透明，盖住后面的聊天内容
const BYTE kOverlayAlpha = 204;
const BYTE kOverlayGray  = 20;

// 画布四周的留白（像素）：留白处单击即关闭
const int kOverlayMarginPx = 24;

// 顶部提示行的高度（像素）与文字颜色
const int      kHintHeightPx = 28;
const COLORREF kHintText     = RGB(0xE6, 0xE6, 0xE6);

// 提示行文字（文件作用域的静态字符串表在语言确定之前初始化，这里只做标记，使用处再翻译）
const wchar_t* const kHintSource = TR_NOOP(L"按 Esc 或点击空白处关闭");

} // namespace

SecretImageOverlay::SecretImageOverlay()
    : m_canvas(NULL)
    , m_hint(NULL)
    , m_restoreFocus(NULL)
    , m_pressed(false)
{
    SetVisible(false);

    std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
    hint->SetText(TR(kHintSource));
    hint->SetTextColor(kHintText);
    hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    m_hint = hint.get();
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(hint.release()));

    std::unique_ptr<balloonimageviewer::ImageCanvas> canvas(new balloonimageviewer::ImageCanvas(true));
    canvas->SetBlankClickHandler(&SecretImageOverlay::OnCanvasBlankClick, this);
    m_canvas = canvas.get();
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(canvas.release()));
}

SecretImageOverlay::~SecretImageOverlay()
{
}

bool SecretImageOverlay::Open(const std::vector<unsigned char>& bytes)
{
    if (m_canvas == NULL || !m_canvas->LoadImageFromMemory(bytes))
    {
        LOG_WARNING("secret image overlay: image not decodable, bytes=%u.", (unsigned)bytes.size());
        return false;
    }
    SetVisible(true);
    //显隐变化不会重排（UI 约定第 5 条）：按父容器的矩形把自己与画布排一遍
    DuiControl* parent = GetParent();
    ForceLayout((parent != NULL) ? parent->GetRect() : GetRect());
    balloonwjui::DuiHost* host = GetHost();
    if (host != NULL)
    {
        host->SetDuiFocus(this);
        if (::IsWindow(host->m_hWnd))
        {
            ::InvalidateRect(host->m_hWnd, NULL, FALSE);
        }
    }
    LOG_INFO("secret image overlay opened, bytes=%u.", (unsigned)bytes.size());
    return true;
}

void SecretImageOverlay::Close()
{
    if (!IsVisible())
    {
        return;
    }
    //传空字节：画布释放位图、回到无图状态
    if (m_canvas != NULL)
    {
        m_canvas->LoadImageFromMemory(std::vector<unsigned char>());
    }
    m_pressed = false;
    SetVisible(false);
    balloonwjui::DuiHost* host = GetHost();
    if (host != NULL)
    {
        host->SetDuiFocus(m_restoreFocus);
        if (::IsWindow(host->m_hWnd))
        {
            ::InvalidateRect(host->m_hWnd, NULL, FALSE);
        }
    }
    LOG_INFO("secret image overlay closed.");
}

bool SecretImageOverlay::IsOpen() const
{
    return IsVisible();
}

void SecretImageOverlay::SetRestoreFocus(balloonwjui::DuiControl* ctrl)
{
    m_restoreFocus = ctrl;
}

void SecretImageOverlay::Layout(const RECT& rcAvail)
{
    //始终铺满父容器（做法同 DuiToast）；没有父容器时（单元测试）用传入的矩形
    DuiControl* parent = GetParent();
    m_rcItem = (parent != NULL) ? parent->GetRect() : rcAvail;
    if (!IsVisible())
    {
        return;
    }
    RECT hintRc = m_rcItem;
    hintRc.top += kOverlayMarginPx / 2;
    hintRc.bottom = hintRc.top + kHintHeightPx;
    if (m_hint != NULL)
    {
        m_hint->ForceLayout(hintRc);
    }
    RECT canvasRc = m_rcItem;
    ::InflateRect(&canvasRc, -kOverlayMarginPx, -kOverlayMarginPx);
    canvasRc.top = hintRc.bottom;
    //窗口比留白还小（或还没有布局）时不让宽高变成负数
    if (canvasRc.right < canvasRc.left)
    {
        canvasRc.right = canvasRc.left;
    }
    if (canvasRc.bottom < canvasRc.top)
    {
        canvasRc.bottom = canvasRc.top;
    }
    if (m_canvas != NULL)
    {
        m_canvas->ForceLayout(canvasRc);
    }
}

void SecretImageOverlay::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!IsVisible())
    {
        return;
    }
    {
        Gdiplus::Graphics g(hdc);
        Gdiplus::SolidBrush brush(Gdiplus::Color(kOverlayAlpha, kOverlayGray, kOverlayGray, kOverlayGray));
        g.FillRectangle(&brush, (INT)m_rcItem.left, (INT)m_rcItem.top, (INT)(m_rcItem.right - m_rcItem.left),
                        (INT)(m_rcItem.bottom - m_rcItem.top));
    }
    DuiControl::OnPaint(hdc, rcDirty);
}

bool SecretImageOverlay::OnLButtonDown(POINT /*pt*/, UINT /*mkFlags*/)
{
    //画布与提示行之外（落到本控件自己）的按下：记下，抬起时仍在空白处就关闭
    m_pressed = true;
    Capture();
    return true;
}

bool SecretImageOverlay::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    if (!m_pressed)
    {
        return false;
    }
    m_pressed = false;
    ReleaseCapture();
    if (::PtInRect(&m_rcItem, pt))
    {
        Close();
    }
    return true;
}

bool SecretImageOverlay::OnMouseWheel(POINT /*pt*/, short /*zDelta*/, UINT /*mkFlags*/)
{
    return true;
}

bool SecretImageOverlay::OnKeyDown(UINT vk, UINT /*flags*/)
{
    if (vk == VK_ESCAPE)
    {
        Close();
        return true;
    }
    //浮层打开时其余按键不往下传（不让输入框在背后收字）
    return true;
}

void SecretImageOverlay::OnCanvasBlankClick(void* ctx)
{
    SecretImageOverlay* self = static_cast<SecretImageOverlay*>(ctx);
    if (self != NULL)
    {
        self->Close();
    }
}

} // namespace balloonchatwnd
