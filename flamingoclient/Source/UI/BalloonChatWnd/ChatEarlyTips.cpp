/**
 *  ChatEarlyTips.cpp
 *
 *  首屏历史渲染之前插入的居中提示的记录，说明见 ChatEarlyTips.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ChatEarlyTips.h"

namespace balloonchatwnd {

// 类内初始化的静态常量在被取地址或按引用传递（如单元测试的断言）时需要这一处定义
const size_t EarlySystemTipTracker::kMaxTips;

EarlySystemTipTracker::EarlySystemTipTracker()
    : m_firstPageRendered(false)
{
}

void EarlySystemTipTracker::OnTipShown(const std::wstring& text)
{
    if (m_firstPageRendered || text.empty())
        return;
    m_tips.push_back(text);
    while (m_tips.size() > kMaxTips)
        m_tips.pop_front();
}

std::vector<std::wstring> EarlySystemTipTracker::TakeTipsOnFirstPageRendered()
{
    m_firstPageRendered = true;
    std::vector<std::wstring> tips(m_tips.begin(), m_tips.end());
    m_tips.clear();
    return tips;
}

std::vector<std::wstring> DropTipsShownInHistory(const std::vector<std::wstring>& tips,
                                                 const std::vector<std::wstring>& shown)
{
    std::vector<std::wstring> rest(tips);
    for (size_t i = 0; i < shown.size(); ++i)
    {
        for (std::vector<std::wstring>::iterator it = rest.begin(); it != rest.end(); ++it)
        {
            if (*it == shown[i])
            {
                rest.erase(it);
                break;
            }
        }
    }
    return rest;
}

void EarlySystemTipTracker::OnReloadRequested()
{
    //已经记录、还没补回的提示保留：新的首屏到达时一并补回
    m_firstPageRendered = false;
}

bool EarlySystemTipTracker::IsFirstPageRendered() const
{
    return m_firstPageRendered;
}

size_t EarlySystemTipTracker::PendingCount() const
{
    return m_tips.size();
}

ChatMessage MakeSystemTipMessage(const CString& text)
{
    // 复用 MakeTextMessage 填默认值，再改成居中提示类型（kMsgTime）
    ChatMessage tip = MakeTextMessage(false, text);
    tip.kind = kMsgTime;
    return tip;
}

} // namespace balloonchatwnd
