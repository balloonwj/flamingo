/**
 *  远程桌面会话管理器实现，RemoteDesktopManager.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RemoteDesktopManager.h"
#include "RemoteDesktopProtocol.h"      // enum rd_state
#include "../utils/RelayToken.h"

//初始化列表的书写顺序必须与头文件里成员的声明顺序一致，
//否则 GCC 会报 -Wreorder：实际的初始化顺序由声明顺序决定，与这里写的顺序无关
RemoteDesktopManager::RemoteDesktopManager()
    : m_seqCounter(0)
    , m_relayPort(0)
    , m_offlineGraceMs(kDefaultOfflineGraceMs)
{
}

void RemoteDesktopManager::setOfflineGraceMs(int64_t graceMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_offlineGraceMs = (graceMs > 0) ? graceMs : kDefaultOfflineGraceMs;
}

int64_t RemoteDesktopManager::offlineGraceMs() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_offlineGraceMs;
}

void RemoteDesktopManager::markUserOffline(int64_t userId, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, RemoteDesktopSessionInfo>::iterator it = m_sessions.begin(); it != m_sessions.end();
         ++it)
    {
        //已记过的不改：宽限期从第一次掉线算起
        if (it->second.controllerId == userId && it->second.controllerOfflineAt == 0)
            it->second.controllerOfflineAt = now;
        if (it->second.controlledId == userId && it->second.controlledOfflineAt == 0)
            it->second.controlledOfflineAt = now;
    }
}

void RemoteDesktopManager::markUserOnline(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, RemoteDesktopSessionInfo>::iterator it = m_sessions.begin(); it != m_sessions.end();
         ++it)
    {
        if (it->second.controllerId == userId)
            it->second.controllerOfflineAt = 0;
        if (it->second.controlledId == userId)
            it->second.controlledOfflineAt = 0;
    }
}

void RemoteDesktopManager::reapOfflineSessions(int64_t now, std::vector<RemoteDesktopSessionInfo>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, RemoteDesktopSessionInfo>::iterator it = m_sessions.begin();
    while (it != m_sessions.end())
    {
        const RemoteDesktopSessionInfo& s = it->second;
        const bool controllerGone = s.controllerOfflineAt != 0 && now - s.controllerOfflineAt >= m_offlineGraceMs;
        const bool controlledGone = s.controlledOfflineAt != 0 && now - s.controlledOfflineAt >= m_offlineGraceMs;
        if (controllerGone || controlledGone)
        {
            out.push_back(s);
            //erase 返回下一个迭代器，避免失效
            it = m_sessions.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

RemoteDesktopManager::~RemoteDesktopManager()
{
}

void RemoteDesktopManager::configure(const std::string& relayTokenKey,
                                     const std::string& relayHost, int relayPort)
{
    m_relayTokenKey = relayTokenKey;
    m_relayHost     = relayHost;
    m_relayPort     = relayPort;
}

const std::string& RemoteDesktopManager::getRelayHost() const
{
    return m_relayHost;
}

int RemoteDesktopManager::getRelayPort() const
{
    return m_relayPort;
}

std::string RemoteDesktopManager::createSession(int64_t controllerId, int64_t controlledId, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    //sessionid 由 控制端-被控端-时间-自增序号 拼成：序号保证同一毫秒内也唯一，
    //全部为数字与连字符，不含 RelayToken 的分隔符 '.' / '|'，可安全放入令牌 payload
    uint64_t seq = ++m_seqCounter;
    std::string sid = std::to_string(controllerId) + "-" + std::to_string(controlledId)
                    + "-" + std::to_string(now) + "-" + std::to_string(seq);

    RemoteDesktopSessionInfo info;
    info.sessionid    = sid;
    info.controllerId = controllerId;
    info.controlledId = controlledId;
    info.state        = rd_state_ringing;
    info.createTime   = now;

    m_sessions[sid] = info;
    return sid;
}

bool RemoteDesktopManager::getSession(const std::string& sessionid, RemoteDesktopSessionInfo& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, RemoteDesktopSessionInfo>::const_iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;
    out = it->second;
    return true;
}

bool RemoteDesktopManager::setState(const std::string& sessionid, int state)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, RemoteDesktopSessionInfo>::iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;
    it->second.state = state;
    return true;
}

bool RemoteDesktopManager::removeSession(const std::string& sessionid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sessions.erase(sessionid) > 0;
}

bool RemoteDesktopManager::signTokenFor(const std::string& sessionid, int role,
                                        int64_t now, std::string& outToken)
{
    //先在锁内取出会话的双方 userid，再在锁外签名（签名为纯计算，不必持锁）
    int64_t controllerId = 0;
    int64_t controlledId = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, RemoteDesktopSessionInfo>::const_iterator it = m_sessions.find(sessionid);
        if (it == m_sessions.end())
            return false;
        controllerId = it->second.controllerId;
        controlledId = it->second.controlledId;
    }

    RelayTokenPayload payload;
    payload.sessionid = sessionid;
    payload.role      = role;
    //控制端令牌：uid=控制端、peeruid=被控端；被控端令牌：两者对调
    if (role == RelayToken::role_controller)
    {
        payload.uid     = controllerId;
        payload.peeruid = controlledId;
    }
    else
    {
        payload.uid     = controlledId;
        payload.peeruid = controllerId;
    }
    payload.expire = now + kRelayTokenTtlMs;

    outToken = RelayToken::sign(payload, m_relayTokenKey);
    return true;
}
