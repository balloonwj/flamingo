/**
 *  共享浏览窗口的文件列表 CShareFileList 的实现，见 ShareFileList.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareFileList.h"

#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace fileshare {

CShareFileList::CShareFileList()
    : m_listener(NULL)
    , m_pressed(false)
    , m_dragCx(::GetSystemMetrics(SM_CXDRAG))
    , m_dragCy(::GetSystemMetrics(SM_CYDRAG))
{
    m_pressPt.x = 0;
    m_pressPt.y = 0;
}

void CShareFileList::SetListener(IShareFileListListener* listener)
{
    m_listener = listener;
}

void CShareFileList::SetDragThreshold(int cx, int cy)
{
    if (cx > 0)
        m_dragCx = cx;
    if (cy > 0)
        m_dragCy = cy;
}

bool CShareFileList::IsPressed() const
{
    return m_pressed;
}

bool CShareFileList::OnLButtonDown(POINT pt, UINT mkFlags)
{
    if (m_listener == NULL)
        return DuiVirtualList::OnLButtonDown(pt, mkFlags);
    m_pressed = true;
    m_pressPt = pt;
    //捕获鼠标：拖到列表或窗口之外也能继续收到移动与松开
    if (GetHost() != NULL)
        GetHost()->SetDuiCapture(this);
    m_listener->OnFileListMouseDown(pt, mkFlags);
    return true;
}

bool CShareFileList::OnLButtonUp(POINT pt, UINT mkFlags)
{
    if (!m_pressed)
        return DuiVirtualList::OnLButtonUp(pt, mkFlags);
    m_pressed = false;
    ReleaseMouse();
    if (m_listener != NULL)
        m_listener->OnFileListMouseUp(pt, mkFlags);
    return true;
}

bool CShareFileList::OnLButtonDblClk(POINT pt, UINT mkFlags)
{
    if (m_listener == NULL)
        return DuiVirtualList::OnLButtonDblClk(pt, mkFlags);
    //双击的第二次按下不会再有单独的按下消息：此前那次按下的状态已在松开时结束，这里只通知双击
    m_listener->OnFileListDoubleClick(pt);
    return true;
}

bool CShareFileList::OnMouseMove(POINT pt, UINT mkFlags)
{
    //悬停的底色与滚动条淡入照常由基类处理
    const bool handled = DuiVirtualList::OnMouseMove(pt, mkFlags);
    if (!m_pressed || m_listener == NULL)
        return handled;
    //左键已经不在按着（松开消息没有送到本控件，例如被别的窗口截走）：结束这次按下
    if ((mkFlags & MK_LBUTTON) == 0)
    {
        m_pressed = false;
        ReleaseMouse();
        return handled;
    }
    const int dx = (pt.x > m_pressPt.x) ? pt.x - m_pressPt.x : m_pressPt.x - pt.x;
    const int dy = (pt.y > m_pressPt.y) ? pt.y - m_pressPt.y : m_pressPt.y - pt.y;
    if (dx < m_dragCx && dy < m_dragCy)
        return handled;
    //超过阈值：开始拖动。先释放捕获，拖放的模态循环要自己捕获鼠标
    m_pressed = false;
    ReleaseMouse();
    m_listener->OnFileListDragStart(m_pressPt);
    return true;
}

bool CShareFileList::OnKeyDown(UINT vk, UINT flags)
{
    if (m_listener != NULL && m_listener->OnFileListKeyDown(vk))
        return true;
    return DuiVirtualList::OnKeyDown(vk, flags);
}

void CShareFileList::ReleaseMouse()
{
    if (GetHost() != NULL)
        GetHost()->ReleaseDuiCapture(this);
}

} // namespace fileshare
