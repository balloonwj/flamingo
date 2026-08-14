/**
 *  远程桌面中转配对表实现，RelayHub.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelayHub.h"
#include "RelaySession.h"   //取会话所属连接时需要完整类型

bool RelayHub::join(const std::string& sessionid, int role,
                    const std::shared_ptr<RelaySession>& session)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<Endpoint>& eps = m_table[sessionid];   //不存在则建空向量

    //一个会话至多两端
    if (eps.size() >= 2)
        return false;

    //同角色重复登记视为非法（控制端 / 被控端各且仅一条）
    for (size_t i = 0; i < eps.size(); ++i)
    {
        if (eps[i].role == role)
            return false;
    }

    Endpoint ep;
    ep.role = role;
    ep.session = session;
    eps.push_back(ep);
    return true;
}

std::shared_ptr<RelaySession> RelayHub::getPeer(const std::string& sessionid,
                                                const TcpConnection* self)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::vector<Endpoint>>::iterator it = m_table.find(sessionid);
    if (it == m_table.end())
        return NULL;

    for (size_t i = 0; i < it->second.size(); ++i)
    {
        std::shared_ptr<RelaySession> s = it->second[i].session.lock();
        if (!s)
            continue;

        //按连接排除自己：两端各有各的会话对象与连接，比连接指针即可区分
        std::shared_ptr<TcpConnection> c = s->getConnectionPtr();
        if (c && c.get() != self)
            return s;
    }
    return NULL;
}

std::shared_ptr<RelaySession> RelayHub::leaveByConn(const TcpConnection* self)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, std::vector<Endpoint>>::iterator it = m_table.begin();
         it != m_table.end(); ++it)
    {
        std::vector<Endpoint>& eps = it->second;
        for (size_t i = 0; i < eps.size(); ++i)
        {
            std::shared_ptr<RelaySession> s = eps[i].session.lock();
            if (!s)
                continue;

            std::shared_ptr<TcpConnection> c = s->getConnectionPtr();
            if (!c || c.get() != self)
                continue;

            //找对端（另一槽位）供连带关闭
            std::shared_ptr<RelaySession> peer;
            for (size_t j = 0; j < eps.size(); ++j)
            {
                if (j == i)
                    continue;
                peer = eps[j].session.lock();
            }

            //移除本端；会话空了则清除整条
            eps.erase(eps.begin() + i);
            if (eps.empty())
                m_table.erase(it);
            return peer;
        }
    }
    return NULL;
}
