/**
 *  HistorySelection.cpp —— 消息记录窗口多选集合的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */
#include "HistorySelection.h"

#include <algorithm>          // std::find：重新发送的文件按路径去重
#include "HistoryRowMenu.h"   // CanForwardHistoryRow

namespace balloonhistory {

HistorySelection::HistorySelection(size_t maxCount)
    : m_maxCount(maxCount)
{
}

bool HistorySelection::Toggle(int64_t msgid, int msgType, bool* outFull)
{
    return Toggle(msgid, msgType, false, std::wstring(), outFull);
}

bool HistorySelection::Toggle(int64_t msgid, int msgType, bool direct, const std::wstring& localPath,
                              bool* outFull)
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
        m_directPaths.erase(msgid);
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
    if (direct)
    {
        m_directPaths[msgid] = localPath;
    }
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
    m_directPaths.clear();
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

void HistorySelection::BuildForward(bool merge, balloonmain::ForwardRequest& out) const
{
    out.m_merge = merge;
    out.m_msgids.clear();
    out.m_skipped = 0;
    out.m_reuploadFiles.clear();
    out.m_skippedDirect = 0;
    out.m_directPlaceholders = 0;
    for (std::map<int64_t, int>::const_iterator it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (!CanForwardHistoryRow(it->second, it->first))
        {
            ++out.m_skipped;
            continue;
        }
        std::map<int64_t, std::wstring>::const_iterator dit = m_directPaths.find(it->first);
        if (dit == m_directPaths.end())
        {
            //不是在线直传的文件：照常按消息转发
            out.m_msgids.push_back(it->first);
            continue;
        }
        switch (balloonmain::DecideDirectFileForward(merge, !dit->second.empty()))
        {
        //合并转发：照常进入记录，对方在记录里看到说明文字
        case balloonmain::kDirectForwardPlaceholder:
            out.m_msgids.push_back(it->first);
            ++out.m_directPlaceholders;
            break;
        //逐条转发且本机有文件：改为以离线文件重新发送，同一路径只发一次
        case balloonmain::kDirectForwardReupload:
            if (std::find(out.m_reuploadFiles.begin(), out.m_reuploadFiles.end(), dit->second)
                == out.m_reuploadFiles.end())
            {
                out.m_reuploadFiles.push_back(dit->second);
            }
            break;
        //逐条转发且本机没有文件：无法转发
        case balloonmain::kDirectForwardSkip:
        default:
            ++out.m_skippedDirect;
            break;
        }
    }
}

} // namespace balloonhistory
