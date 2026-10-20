/**
 *  窗口最小尺寸随 DPI 等比换算，见 DpiScaledMinSize.h。
 *  balloonwj@qq.com   2026-10-17
 */
#include "stdafx.h"
#include "DpiScaledMinSize.h"

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

namespace balloonmain {

namespace {

//DPI 缺省值（100% 缩放）
const int kDefaultDpi = 96;

//按 dpi / baseDpi 换算像素值，四舍五入；DPI 非法时原样返回
int scaleForDpi(int px, int baseDpi, int dpi)
{
    if (baseDpi <= 0 || dpi <= 0)
        return px;
    return ::MulDiv(px, dpi, baseDpi);
}

} // namespace

CDpiScaledMinSize::CDpiScaledMinSize()
    : m_baseW(0)
    , m_baseH(0)
    , m_baseDpi(kDefaultDpi)
{
}

void CDpiScaledMinSize::Init(balloonwjui::DuiFrameWindow& wnd, int minW, int minH, int dpi)
{
    m_baseW = minW;
    m_baseH = minH;
    m_baseDpi = (dpi > 0) ? dpi : kDefaultDpi;
    wnd.SetMinSize(m_baseW, m_baseH);
}

void CDpiScaledMinSize::OnDpiChanged(balloonwjui::DuiFrameWindow& wnd, int newDpi)
{
    if (newDpi <= 0)
        return;
    //须在基类按建议矩形 SetWindowPos 之前设好：WM_GETMINMAXINFO 会把窗口抬到当时的下限
    wnd.SetMinSize(ScaledWidth(newDpi), ScaledHeight(newDpi));
}

int CDpiScaledMinSize::ScaledWidth(int dpi) const
{
    return scaleForDpi(m_baseW, m_baseDpi, dpi);
}

int CDpiScaledMinSize::ScaledHeight(int dpi) const
{
    return scaleForDpi(m_baseH, m_baseDpi, dpi);
}

} // namespace balloonmain
