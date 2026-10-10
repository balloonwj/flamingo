/**
 *  未收到应答的请求登记簿，见 BotRequestBook.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotRequestBook.h"

namespace flamingobot {

CBotRequestBook::CBotRequestBook()
{
}

void CBotRequestBook::Add(uint32_t seq, uint16_t cmd, uint32_t clientId)
{
    if (seq == 0 || clientId == 0)
        return;
    Entry entry;
    entry.m_cmd = cmd;
    entry.m_clientId = clientId;
    m_entries[seq] = entry;
}

bool CBotRequestBook::Take(uint32_t seq, uint16_t cmd, uint32_t& clientId)
{
    std::map<uint32_t, Entry>::iterator it = m_entries.find(seq);
    if (it == m_entries.end() || it->second.m_cmd != cmd)
        return false;
    clientId = it->second.m_clientId;
    m_entries.erase(it);
    return true;
}

size_t CBotRequestBook::RemoveClient(uint32_t clientId)
{
    size_t removed = 0;
    std::map<uint32_t, Entry>::iterator it = m_entries.begin();
    while (it != m_entries.end())
    {
        if (it->second.m_clientId == clientId)
        {
            it = m_entries.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

void CBotRequestBook::Clear()
{
    m_entries.clear();
}

size_t CBotRequestBook::Size() const
{
    return m_entries.size();
}

size_t CBotRequestBook::CountOf(uint32_t clientId) const
{
    size_t count = 0;
    for (std::map<uint32_t, Entry>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->second.m_clientId == clientId)
            ++count;
    }
    return count;
}

} // namespace flamingobot
