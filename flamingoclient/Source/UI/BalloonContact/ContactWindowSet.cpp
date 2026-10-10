/**
 *  ContactWindowSet.cpp
 *
 *  联系人资料窗多窗口管理的实现：按 userid 一人一窗、激活已有窗口、新窗口错开摆放、关闭后惰性回收。
 *  说明见 ContactWindowSet.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"
#include "ContactWindowSet.h"

#include <algorithm>   // std::find

#include "../../Common/Log/IULog.h"   // LOG_INFO

namespace ballooncontact {

namespace {

// 区间 [low, high] 内夹住 value；high < low 时返回 low（窗口比工作区还大时左上角对齐工作区）
LONG ClampCoordinate(LONG value, LONG low, LONG high)
{
    if (high < low)
    {
        return low;
    }
    if (value < low)
    {
        return low;
    }
    if (value > high)
    {
        return high;
    }
    return value;
}

} // anonymous namespace

POINT ComputeContactWindowCascade(const RECT& workArea, SIZE windowSize, bool hasPrevious,
                                  POINT previousTopLeft, int step)
{
    const LONG workW = workArea.right - workArea.left;
    const LONG workH = workArea.bottom - workArea.top;

    // 起点：窗口在工作区里居中时的左上角；工作区无效时取它的左上角
    POINT origin;
    origin.x = workArea.left;
    origin.y = workArea.top;
    if (workW <= 0 || workH <= 0)
    {
        return origin;
    }
    origin.x = workArea.left + (workW - windowSize.cx) / 2;
    origin.y = workArea.top + (workH - windowSize.cy) / 2;

    POINT result = origin;
    if (hasPrevious)
    {
        const LONG offset = (step > 0) ? step : 0;
        POINT next;
        next.x = previousTopLeft.x + offset;
        next.y = previousTopLeft.y + offset;
        // 错开后会超出工作区的右边或下边：回到起点，从头再错开
        const bool overflow = (next.x + windowSize.cx > workArea.right)
                           || (next.y + windowSize.cy > workArea.bottom);
        result = overflow ? origin : next;
    }

    // 整个窗口落在工作区内；窗口比工作区还大时左上角对齐工作区的左上角
    result.x = ClampCoordinate(result.x, workArea.left, workArea.right - windowSize.cx);
    result.y = ClampCoordinate(result.y, workArea.top, workArea.bottom - windowSize.cy);
    return result;
}

CContactWindowSet::CContactWindowSet(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
{
}

CContactWindowSet::~CContactWindowSet()
{
    DestroyAll();
}

CBalloonContactWindow* CContactWindowSet::Open(HWND hostPanel, UINT peerUserId, const ContactModel& model,
                                               bool beginRemarkEdit)
{
    if (peerUserId == 0)
    {
        return NULL;
    }
    ReapClosed();

    // 已开着：只激活，不重建界面（正在编辑的备注名或资料格不会被冲掉）
    std::map<UINT, CBalloonContactWindow*>::iterator it = m_windows.find(peerUserId);
    if (it != m_windows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->ActivateExisting(beginRemarkEdit);
        TouchOrder(peerUserId);
        LOG_INFO("contact windows: activate existing window, userid=%u, open=%u.", peerUserId,
                 (unsigned)m_windows.size());
        return it->second;
    }

    // 新建：相对最近打开、仍开着的那个资料窗错开摆放
    CBalloonContactWindow* previous = FindLastOpened();
    bool hasPrevious = false;
    POINT previousTopLeft;
    previousTopLeft.x = 0;
    previousTopLeft.y = 0;
    if (previous != NULL)
    {
        RECT rc;
        if (previous->GetWindowRect(&rc))
        {
            hasPrevious = true;
            previousTopLeft.x = rc.left;
            previousTopLeft.y = rc.top;
        }
    }

    CBalloonContactWindow* window = new CBalloonContactWindow(m_lpFMGClient);
    window->SetCascadeAnchor(hasPrevious, previousTopLeft);
    window->OpenForContact(hostPanel, peerUserId, model, beginRemarkEdit);
    if (!window->IsWindow())
    {
        // 建窗失败：不入表，直接释放
        LOG_INFO("contact windows: failed to create window, userid=%u.", peerUserId);
        delete window;
        return NULL;
    }
    m_windows[peerUserId] = window;
    TouchOrder(peerUserId);
    LOG_INFO("contact windows: open new window, userid=%u, cascade=%d, open=%u.", peerUserId,
             hasPrevious ? 1 : 0, (unsigned)m_windows.size());
    return window;
}

CBalloonContactWindow* CContactWindowSet::Find(UINT peerUserId)
{
    if (peerUserId == 0)
    {
        return NULL;
    }
    ReapClosed();
    std::map<UINT, CBalloonContactWindow*>::iterator it = m_windows.find(peerUserId);
    if (it == m_windows.end() || it->second == NULL || !it->second->IsWindow())
    {
        return NULL;
    }
    return it->second;
}

void CContactWindowSet::GetOpenWindows(std::vector<CBalloonContactWindow*>& outWindows)
{
    outWindows.clear();
    ReapClosed();
    for (size_t i = 0; i < m_openOrder.size(); ++i)
    {
        std::map<UINT, CBalloonContactWindow*>::iterator it = m_windows.find(m_openOrder[i]);
        if (it != m_windows.end() && it->second != NULL && it->second->IsWindow())
        {
            outWindows.push_back(it->second);
        }
    }
}

size_t CContactWindowSet::OpenCount()
{
    ReapClosed();
    return m_windows.size();
}

size_t CContactWindowSet::ReapClosed()
{
    size_t reaped = 0;
    std::map<UINT, CBalloonContactWindow*>::iterator it = m_windows.begin();
    while (it != m_windows.end())
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            ++it;
            continue;
        }
        // 窗口已关闭，C++ 对象残留：释放后从表与打开顺序里去掉
        const UINT userid = it->first;
        delete it->second;
        m_windows.erase(it++);
        RemoveFromOrder(userid);
        ++reaped;
    }
    if (reaped > 0)
    {
        LOG_INFO("contact windows: reaped %u closed window(s), open=%u.", (unsigned)reaped,
                 (unsigned)m_windows.size());
    }
    return reaped;
}

void CContactWindowSet::DestroyAll()
{
    for (std::map<UINT, CBalloonContactWindow*>::iterator it = m_windows.begin(); it != m_windows.end(); ++it)
    {
        if (it->second == NULL)
        {
            continue;
        }
        // 对象析构前必须先销毁窗口，否则 ATL 会因对象仍持有有效 HWND 而断言
        if (it->second->IsWindow())
        {
            it->second->DestroyWindow();
        }
        delete it->second;
    }
    m_windows.clear();
    m_openOrder.clear();
}

CBalloonContactWindow* CContactWindowSet::FindLastOpened() const
{
    for (size_t i = m_openOrder.size(); i > 0; --i)
    {
        std::map<UINT, CBalloonContactWindow*>::const_iterator it = m_windows.find(m_openOrder[i - 1]);
        if (it != m_windows.end() && it->second != NULL && it->second->IsWindow())
        {
            return it->second;
        }
    }
    return NULL;
}

void CContactWindowSet::TouchOrder(UINT peerUserId)
{
    RemoveFromOrder(peerUserId);
    m_openOrder.push_back(peerUserId);
}

void CContactWindowSet::RemoveFromOrder(UINT peerUserId)
{
    std::vector<UINT>::iterator it = std::find(m_openOrder.begin(), m_openOrder.end(), peerUserId);
    if (it != m_openOrder.end())
    {
        m_openOrder.erase(it);
    }
}

} // namespace ballooncontact
