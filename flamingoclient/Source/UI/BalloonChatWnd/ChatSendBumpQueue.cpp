/**
 *  ChatSendBumpQueue.cpp
 *
 *  发送后待冒泡队列的实现，说明见 ChatSendBumpQueue.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "ChatSendBumpQueue.h"
#include "../../net/MessageProtocolClient.h"   // digestForMsgType：文件消息的会话预览文字
#include "../../net/Protocol.h"                // message_type_file

namespace balloonchatwnd {

// 类内初始化的静态常量在被取地址或按引用传递（如单元测试的断言）时需要这一处定义
const size_t ChatSendBumpQueue::kMaxPending;

ConvSendBumpData MakeFileSendBump(int ctype, long long cid)
{
    ConvSendBumpData bump;
    bump.ctype   = ctype;
    bump.cid     = cid;
    bump.msgType = message_type_file;
    bump.digest  = net::MessageProtocolClient::digestForMsgType(message_type_file, std::string());
    return bump;
}

void ChatSendBumpQueue::Add(const std::string& clientMsgId, const ConvSendBumpData& data)
{
    if (clientMsgId.empty())
    {
        return;
    }
    if (m_pending.find(clientMsgId) == m_pending.end())
    {
        m_order.push_back(clientMsgId);
    }
    m_pending[clientMsgId] = data;

    //超出上限：从最早登记的开始丢弃。m_order 里可能留着已取走的 id，跳过它们即可
    while (m_pending.size() > kMaxPending && !m_order.empty())
    {
        m_pending.erase(m_order.front());
        m_order.pop_front();
    }
    //m_order 里已取走的 id 累积过多时整理一次，免得它无限增长
    if (m_order.size() > kMaxPending * 2)
    {
        std::deque<std::string> alive;
        for (size_t i = 0; i < m_order.size(); ++i)
        {
            if (m_pending.find(m_order[i]) != m_pending.end())
            {
                alive.push_back(m_order[i]);
            }
        }
        m_order.swap(alive);
    }
}

bool ChatSendBumpQueue::TakeOnSuccess(const std::string& clientMsgId, ConvSendBumpData& out)
{
    std::map<std::string, ConvSendBumpData>::iterator it = m_pending.find(clientMsgId);
    if (it == m_pending.end())
    {
        return false;
    }
    out = it->second;
    m_pending.erase(it);
    return true;
}

void ChatSendBumpQueue::Drop(const std::string& clientMsgId)
{
    m_pending.erase(clientMsgId);
}

} // namespace balloonchatwnd
