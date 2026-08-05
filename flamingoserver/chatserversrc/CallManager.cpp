/**
 *  音视频通话会话管理器实现，CallManager.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallManager.h"
#include "CallProtocol.h"               // enum call_state / call_media
#include "Protocol.h"                   // enum error_code（switchMedia 的失败码）
#include "../utils/RelayToken.h"

CallManager::CallManager()
    : m_seqCounter(0)
    , m_relayPort(0)
{
}

CallManager::~CallManager()
{
}

void CallManager::configure(const std::string& relayTokenKey,
                            const std::string& relayHost, int relayPort)
{
    m_relayTokenKey = relayTokenKey;
    m_relayHost     = relayHost;
    m_relayPort     = relayPort;
}

const std::string& CallManager::getRelayHost() const
{
    return m_relayHost;
}

int CallManager::getRelayPort() const
{
    return m_relayPort;
}

std::string CallManager::createSession(int64_t callerId, int64_t calleeId, int media, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    //sessionid 由 主叫-被叫-时间-自增序号 拼成：序号保证同一毫秒内也唯一，
    //全部为数字与连字符，不含 RelayToken 的分隔符 '.' / '|'，可安全放入令牌 payload
    uint64_t seq = ++m_seqCounter;
    std::string sid = std::to_string(callerId) + "-" + std::to_string(calleeId)
                    + "-" + std::to_string(now) + "-" + std::to_string(seq);

    CallSessionInfo info;
    info.sessionid  = sid;
    info.callerId   = callerId;
    info.calleeId   = calleeId;
    info.media      = media;
    info.state      = call_state_ringing;
    info.createTime = now;

    m_sessions[sid] = info;
    return sid;
}

bool CallManager::getSession(const std::string& sessionid, CallSessionInfo& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, CallSessionInfo>::const_iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;
    out = it->second;
    return true;
}

bool CallManager::setState(const std::string& sessionid, int state)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, CallSessionInfo>::iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;
    it->second.state = state;
    return true;
}

int CallManager::switchMedia(const std::string& sessionid, int64_t requesterId, int media,
                             bool& outChanged)
{
    outChanged = false;

    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, CallSessionInfo>::iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return error_code_call_not_exist;

    CallSessionInfo& s = it->second;

    //只有本次通话的主叫或被叫能切换；第三方即便拿到 sessionid 也无权操作
    if (requesterId != s.callerId && requesterId != s.calleeId)
        return error_code_call_not_member;

    //未接通（响铃中 / 已结束等）一律拒绝：此时双方还没有建立媒体通道，切换没有意义
    if (s.state != call_state_connected)
        return error_code_call_state_invalid;

    //幂等：与当前相同则不改也不推送，避免无意义的双向广播
    if (s.media == media)
        return 0;

    s.media    = media;
    outChanged = true;
    return 0;
}

bool CallManager::removeSession(const std::string& sessionid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sessions.erase(sessionid) > 0;
}

bool CallManager::signTokenFor(const std::string& sessionid, int role,
                               int64_t now, std::string& outToken)
{
    //先在锁内取出会话的双方 userid，再在锁外签名（签名为纯计算，不必持锁）
    int64_t callerId = 0;
    int64_t calleeId = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, CallSessionInfo>::const_iterator it = m_sessions.find(sessionid);
        if (it == m_sessions.end())
            return false;
        callerId = it->second.callerId;
        calleeId = it->second.calleeId;
    }

    RelayTokenPayload payload;
    payload.sessionid = sessionid;
    payload.role      = role;
    //主叫令牌：uid=主叫、peeruid=被叫；被叫令牌：两者对调
    if (role == RelayToken::role_caller)
    {
        payload.uid     = callerId;
        payload.peeruid = calleeId;
    }
    else
    {
        payload.uid     = calleeId;
        payload.peeruid = callerId;
    }
    payload.expire = now + kRelayTokenTtlMs;

    outToken = RelayToken::sign(payload, m_relayTokenKey);
    return true;
}

bool CallManager::isUserBusy(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, CallSessionInfo>::const_iterator it = m_sessions.begin();
         it != m_sessions.end(); ++it)
    {
        //活跃会话表里只保留进行中的通话（结束即移除），故命中即视为忙
        if (it->second.callerId == userId || it->second.calleeId == userId)
            return true;
    }
    return false;
}

void CallManager::reapTimedOutSessions(int64_t now, std::vector<CallSessionInfo>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, CallSessionInfo>::iterator it = m_sessions.begin();
    while (it != m_sessions.end())
    {
        //只清理仍在响铃、且响铃时长已达超时阈值的会话；已接通的通话不超时
        if (it->second.state == call_state_ringing
            && now - it->second.createTime >= kRingingTimeoutMs)
        {
            out.push_back(it->second);
            //erase 返回下一个迭代器，避免失效
            it = m_sessions.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
