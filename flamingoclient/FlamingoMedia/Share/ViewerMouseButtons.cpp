/**
 *  观看端鼠标按钮的记录与「要不要发送」的判断，说明见 ViewerMouseButtons.h。
 *  balloonwj@qq.com   2026-10-14
 */
#include "ViewerMouseButtons.h"

namespace screenshare {

int ViewerButtonOfDown(UINT msg)
{
    switch (msg)
    {
    //左键按下与双击
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        return kViewerButtonLeft;
    //右键按下与双击
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        return kViewerButtonRight;
    //中键按下与双击
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        return kViewerButtonMiddle;
    //其余消息不是按下
    default:
        return 0;
    }
}

int ViewerButtonOfUp(UINT msg)
{
    switch (msg)
    {
    //左键松开
    case WM_LBUTTONUP:
        return kViewerButtonLeft;
    //右键松开
    case WM_RBUTTONUP:
        return kViewerButtonRight;
    //中键松开
    case WM_MBUTTONUP:
        return kViewerButtonMiddle;
    //其余消息不是松开
    default:
        return 0;
    }
}

ViewerMouseButtons::ViewerMouseButtons()
    : m_buttonsDown(0)
{
}

bool ViewerMouseButtons::Decide(UINT msg, bool inside)
{
    const int down = ViewerButtonOfDown(msg);
    if (down != 0)
    {
        //画面外按下不发送，也不记
        if (!inside)
            return false;
        m_buttonsDown |= down;
        return true;
    }
    const int up = ViewerButtonOfUp(msg);
    if (up != 0)
    {
        //画面内按下的按钮在哪里松开都要发；没记过的按钮只在画面内松开时发
        const bool send = inside || (m_buttonsDown & up) != 0;
        m_buttonsDown &= ~up;
        return send;
    }
    //按住拖动时画面外的移动照发；没按住时画面外的移动不发
    if (msg == WM_MOUSEMOVE)
        return inside || m_buttonsDown != 0;
    //滚轮等其余消息：只在画面内发
    return inside;
}

void ViewerMouseButtons::Reset()
{
    m_buttonsDown = 0;
}

int ViewerMouseButtons::ButtonsDown() const
{
    return m_buttonsDown;
}

std::vector<uint32_t> ViewerMouseButtons::PendingButtonUps() const
{
    std::vector<uint32_t> ups;
    if ((m_buttonsDown & kViewerButtonLeft) != 0)
        ups.push_back(WM_LBUTTONUP);
    if ((m_buttonsDown & kViewerButtonRight) != 0)
        ups.push_back(WM_RBUTTONUP);
    if ((m_buttonsDown & kViewerButtonMiddle) != 0)
        ups.push_back(WM_MBUTTONUP);
    return ups;
}

} // namespace screenshare
