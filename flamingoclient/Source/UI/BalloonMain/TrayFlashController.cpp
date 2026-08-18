/**
 *  TrayFlashController.cpp
 *
 *  CTrayFlashController 的实现。见 TrayFlashController.h 的类用途与排序约定。
 *  全部为内存中的 vector 增删与查找，无任何 Win32 / GDI 副作用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"
#include "TrayFlashController.h"

namespace balloonmain {

unsigned int ClampTrayFlashInterval(unsigned int blinkTimeMs)
{
    // INFINITE 表示用户关掉了系统的文本光标闪烁，0 表示系统没有给出有效值。
    // 这两种都不是"用户希望闪得很慢"的意思，直接用兜底值，不要往上界靠。
    if (blinkTimeMs == 0 || blinkTimeMs == INFINITE)
    {
        return kTrayFlashIntervalDefaultMs;
    }

    if (blinkTimeMs < kTrayFlashIntervalMinMs)
    {
        return kTrayFlashIntervalMinMs;
    }

    if (blinkTimeMs > kTrayFlashIntervalMaxMs)
    {
        return kTrayFlashIntervalMaxMs;
    }

    return blinkTimeMs;
}

CTrayFlashController::CTrayFlashController()
{
}

int CTrayFlashController::FindEntry(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].m_ctype == ctype && m_entries[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

void CTrayFlashController::PushUnread(int ctype, int64_t cid, const std::wstring& displayName,
                                      const std::wstring& preview, const std::wstring& avatarText,
                                      COLORREF avatarColor)
{
    // 非法会话 ID 直接忽略，避免把 0 / 负数会话塞进集合污染"最近一条"。
    if (cid <= 0)
    {
        return;
    }

    int nIndex = FindEntry(ctype, cid);
    if (nIndex >= 0)
    {
        // 已存在：累加未读、用最新资料覆盖刷新（预览也更新成最新这条），再把它移到
        // 最前（成为最近一条）。
        TrayFlashEntry entry = m_entries[(size_t)nIndex];
        entry.m_unreadCount += 1;
        entry.m_displayName = displayName;
        entry.m_preview     = preview;
        entry.m_avatarText  = avatarText;
        entry.m_avatarColor = avatarColor;

        m_entries.erase(m_entries.begin() + nIndex);
        m_entries.insert(m_entries.begin(), entry);
        return;
    }

    // 新会话：未读置 1，插到最前作为最近一条。
    TrayFlashEntry entry;
    entry.m_ctype       = ctype;
    entry.m_cid         = cid;
    entry.m_unreadCount = 1;
    entry.m_displayName = displayName;
    entry.m_preview     = preview;
    entry.m_avatarText  = avatarText;
    entry.m_avatarColor = avatarColor;
    m_entries.insert(m_entries.begin(), entry);
}

bool CTrayFlashController::ClearConversation(int ctype, int64_t cid)
{
    int nIndex = FindEntry(ctype, cid);
    if (nIndex >= 0)
    {
        m_entries.erase(m_entries.begin() + nIndex);
    }
    return IsFlashing();
}

void CTrayFlashController::Clear()
{
    m_entries.clear();
}

bool CTrayFlashController::IsFlashing() const
{
    return !m_entries.empty();
}

int CTrayFlashController::GetConversationCount() const
{
    return (int)m_entries.size();
}

bool CTrayFlashController::GetTarget(TrayFlashEntry& out) const
{
    if (m_entries.empty())
    {
        return false;
    }
    out = m_entries.front();
    return true;
}

const std::vector<TrayFlashEntry>& CTrayFlashController::GetEntries() const
{
    return m_entries;
}

} // namespace balloonmain
