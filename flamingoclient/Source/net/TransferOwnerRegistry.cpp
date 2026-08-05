/**
 *  传输信令归属登记表（客户端侧）实现，TransferOwnerRegistry.cpp
 *  balloonwj@qq.com   2026-06-05
 */
#include "TransferOwnerRegistry.h"

namespace net {

TransferOwnerRegistry::TransferOwnerRegistry()
{
}

void TransferOwnerRegistry::registerSeq(uint32_t seq, int owner)
{
    m_owners[seq] = owner;

    //超限兜底：淘汰 seq 最小（最旧）的条目，直到回落到上限。seq 由 CIUSocket
    //单调自增分配，最小键即最早发出、最可能已超时无应答的请求。
    while (m_owners.size() > kMaxEntries)
        m_owners.erase(m_owners.begin());
}

bool TransferOwnerRegistry::takeOwner(uint32_t seq, int& owner)
{
    std::map<uint32_t, int>::iterator it = m_owners.find(seq);
    if (it == m_owners.end())
        return false;

    owner = it->second;
    m_owners.erase(it);
    return true;
}

size_t TransferOwnerRegistry::size() const
{
    return m_owners.size();
}

} // namespace net
