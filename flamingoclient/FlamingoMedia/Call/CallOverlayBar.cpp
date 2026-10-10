/**
 *  视频通话浮在画面底部、可淡入淡出的操作条的实现，CallOverlayBar.cpp
 *  说明见 CallOverlayBar.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CallOverlayBar.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>

namespace call {

namespace {

//完全不透明、完全透明时的不透明度
const int kAlphaOpaque = 255;
const int kAlphaClear = 0;

} // namespace

CallOverlayBar::CallOverlayBar(int heightPx)
    : m_height(heightPx)
    , m_alpha(kAlphaOpaque)
    , m_memDc(NULL)
    , m_memBmp(NULL)
    , m_memOld(NULL)
    , m_memW(0)
    , m_memH(0)
{
}

CallOverlayBar::~CallOverlayBar()
{
    ReleaseMemDc();
}

void CallOverlayBar::SetBarAlpha(int alpha)
{
    if (alpha < kAlphaClear)
        alpha = kAlphaClear;
    if (alpha > kAlphaOpaque)
        alpha = kAlphaOpaque;
    if (alpha == m_alpha)
        return;
    m_alpha = alpha;
    Invalidate();
}

void CallOverlayBar::Layout(const RECT& rcAvail)
{
    //父布局给的是 Fixed(0) 的矩形，不是操作条该在的位置：改按父容器的整个矩形贴底
    RECT rc = rcAvail;
    balloonwjui::DuiControl* parent = GetParent();
    if (parent != NULL)
    {
        rc = parent->GetRect();
        if (rc.bottom - rc.top > m_height)
            rc.top = rc.bottom - m_height;
    }
    balloonwjui::DuiHBox::Layout(rc);
}

balloonwjui::DuiControl* CallOverlayBar::HitTest(POINT pt)
{
    if (m_alpha <= kAlphaClear)
        return NULL;
    return balloonwjui::DuiHBox::HitTest(pt);
}

void CallOverlayBar::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const RECT& rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (!IsVisible() || m_alpha <= kAlphaClear || w <= 0 || h <= 0)
        return;

    //内存位图从客户区原点起算，宽高取到操作条的右下角
    HDC mem = EnsureMemDc(hdc, rc.right, rc.bottom);
    if (mem == NULL)
    {
        //内存位图创建失败：直接画在窗口上（没有淡入淡出与渐变底），至少按钮可用
        balloonwjui::DuiHBox::OnPaint(hdc, rcDirty);
        return;
    }
    //底下已画好的画面 → 渐变底 → 子控件
    ::BitBlt(mem, rc.left, rc.top, w, h, hdc, rc.left, rc.top, SRCCOPY);
    DrawShade(mem, rc);
    balloonwjui::DuiHBox::OnPaint(mem, rcDirty);

    if (m_alpha >= kAlphaOpaque)
    {
        ::BitBlt(hdc, rc.left, rc.top, w, h, mem, rc.left, rc.top, SRCCOPY);
        return;
    }
    //内存位图里已含底下的画面，按整体不透明度混合回去即得淡入淡出的效果（不使用逐像素透明度）
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = static_cast<BYTE>(m_alpha);
    bf.AlphaFormat = 0;
    ::AlphaBlend(hdc, rc.left, rc.top, w, h, mem, rc.left, rc.top, w, h, bf);
}

HDC CallOverlayBar::EnsureMemDc(HDC ref, int widthPx, int heightPx)
{
    if (m_memDc != NULL && m_memW >= widthPx && m_memH >= heightPx)
        return m_memDc;
    ReleaseMemDc();
    m_memDc = ::CreateCompatibleDC(ref);
    if (m_memDc == NULL)
        return NULL;
    m_memBmp = ::CreateCompatibleBitmap(ref, widthPx, heightPx);
    if (m_memBmp == NULL)
    {
        ::DeleteDC(m_memDc);
        m_memDc = NULL;
        return NULL;
    }
    m_memOld = ::SelectObject(m_memDc, m_memBmp);
    m_memW = widthPx;
    m_memH = heightPx;
    return m_memDc;
}

void CallOverlayBar::ReleaseMemDc()
{
    if (m_memDc != NULL)
    {
        if (m_memOld != NULL)
            ::SelectObject(m_memDc, m_memOld);
        ::DeleteDC(m_memDc);
    }
    if (m_memBmp != NULL)
        ::DeleteObject(m_memBmp);
    m_memDc = NULL;
    m_memBmp = NULL;
    m_memOld = NULL;
    m_memW = 0;
    m_memH = 0;
}

void CallOverlayBar::DrawShade(HDC dc, const RECT& rc)
{
    //GDI+ 由通话窗在建窗时初始化（BalloonCallDlg 的 EnsureGdiplus）；未初始化时 Graphics 构造失败，只是少一层渐变
    Gdiplus::Graphics g(dc);
    if (g.GetLastStatus() != Gdiplus::Ok)
        return;
    const Gdiplus::REAL top = static_cast<Gdiplus::REAL>(rc.top);
    const Gdiplus::REAL bottom = static_cast<Gdiplus::REAL>(rc.bottom);
    const Gdiplus::REAL left = static_cast<Gdiplus::REAL>(rc.left);
    //起点比上沿高 1 像素：避免 GDI+ 在渐变起点那一行重复铺下沿的颜色
    Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(left, top - 1.0f), Gdiplus::PointF(left, bottom),
                                       Gdiplus::Color(kAlphaClear, 0, 0, 0),
                                       Gdiplus::Color(static_cast<BYTE>(kCallOverlayShadeAlpha), 0, 0, 0));
    g.FillRectangle(&brush, left, top, static_cast<Gdiplus::REAL>(rc.right - rc.left), bottom - top);
}

} // namespace call
