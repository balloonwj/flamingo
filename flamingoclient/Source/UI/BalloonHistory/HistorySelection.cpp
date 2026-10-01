/**
 *  HistorySelection.cpp —— 消息记录窗口多选集合的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */
#include "HistorySelection.h"

#include "HistoryRowMenu.h"   // CanForwardHistoryRow

namespace balloonhistory {

HistorySelection::HistorySelection(size_t maxCount)
    : m_maxCount(maxCount)
{
}

bool HistorySelection::Toggle(int64_t msgid, int msgType, bool* outFull)
{
    if (outFull != NULL)
    {
        *outFull = false;
    }
    //msgid 未分配：服务端不认识这条，既不能转发也不能删除
    if (msgid <= 0)
    {
        return false;
    }
    std::map<int64_t, int>::iterator it = m_items.find(msgid);
    if (it != m_items.end())
    {
        //已选中：取消，不受上限影响
        m_items.erase(it);
        return false;
    }
    if (m_maxCount > 0 && m_items.size() >= m_maxCount)
    {
        if (outFull != NULL)
        {
            *outFull = true;
        }
        return false;
    }
    m_items[msgid] = msgType;
    return true;
}

bool HistorySelection::IsSelected(int64_t msgid) const
{
    return m_items.find(msgid) != m_items.end();
}

size_t HistorySelection::Count() const
{
    return m_items.size();
}

void HistorySelection::Clear()
{
    m_items.clear();
}

std::vector<int64_t> HistorySelection::SelectedMsgids() const
{
    //map 按键升序遍历，结果天然升序、不重复
    std::vector<int64_t> out;
    out.reserve(m_items.size());
    for (std::map<int64_t, int>::const_iterator it = m_items.begin(); it != m_items.end(); ++it)
    {
        out.push_back(it->first);
    }
    return out;
}

void HistorySelection::BuildForward(std::vector<int64_t>& outMsgids, size_t& outSkipped) const
{
    outMsgids.clear();
    outSkipped = 0;
    for (std::map<int64_t, int>::const_iterator it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (CanForwardHistoryRow(it->second, it->first))
        {
            outMsgids.push_back(it->first);
        }
        else
        {
            ++outSkipped;
        }
    }
}

} // namespace balloonhistory
