/**
 *  P2P 会话管理（服务端侧）实现，P2PSessionManager.cpp
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PSessionManager.h"

#include <stdio.h>

#include "../utils/RelayToken.h"

namespace {

//会话号前缀：imgserver 的中转配对表与别的中转共用，带前缀不会与远程桌面、通话的会话号重名
const char* const kSessionIdPrefix = "p2p-";

//联调建会话（2306）使用的用途名
const char* const kDebugPurpose = "test";

//会话号文本缓冲的字节数（会话号上限 kP2PSessionIdMaxBytes 为 96，留余量）
const int kSessionIdBufBytes = 128;

} // namespace

//类内初始化的静态常量补上类外定义：被 ODR-use（如绑定到引用）时 g++ 需要，否则链接失败（见 RelayToken.cpp 的说明）
const int P2PSessionManager::kDefaultConnectTimeoutMs;
const int P2PSessionManager::kDefaultKeepaliveMs;
const int P2PSessionManager::kDefaultLinkTimeoutMs;
const int P2PSessionManager::kDefaultMaxPerUser;
const int64_t P2PSessionManager::kRelayTokenTtlMs;

P2PSessionConfig::P2PSessionConfig()
    : m_relayPort(0)
    , m_connectTimeoutMs(P2PSessionManager::kDefaultConnectTimeoutMs)
    , m_keepaliveMs(P2PSessionManager::kDefaultKeepaliveMs)
    , m_linkTimeoutMs(P2PSessionManager::kDefaultLinkTimeoutMs)
    , m_maxPerUser(P2PSessionManager::kDefaultMaxPerUser)
    , m_debugOpen(false)
{
}

P2PSessionManager::P2PSessionManager()
    : m_sessionSeq(0)
    , m_registerSeq(0)
{
}

void P2PSessionManager::configure(const P2PSessionConfig& config)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_config = config;
}

P2PSessionConfig P2PSessionManager::getConfig() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_config;
}

int P2PSessionManager::registerConnection(P2PConnKey conn, int64_t userId, const std::string& deviceId, int version,
                                          const std::vector<std::string>& purposes)
{
    if (version != kP2PProtocolVersion)
        return error_code_p2p_version_unsupported;

    std::lock_guard<std::mutex> guard(m_mutex);
    Registration& reg = m_registrations[conn];
    reg.m_userId = userId;
    reg.m_deviceId = deviceId;
    reg.m_order = ++m_registerSeq;
    reg.m_purposes = std::set<std::string>(purposes.begin(), purposes.end());
    return error_code_ok;
}

bool P2PSessionManager::isRegistered(P2PConnKey conn) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_registrations.find(conn) != m_registrations.end();
}

std::vector<P2PEndedSession> P2PSessionManager::unregisterConnection(P2PConnKey conn)
{
    std::vector<P2PEndedSession> ended;
    std::lock_guard<std::mutex> guard(m_mutex);
    m_registrations.erase(conn);

    std::map<std::string, Session>::iterator it = m_sessions.begin();
    while (it != m_sessions.end())
    {
        const Session& s = it->second;
        if (s.m_connA != conn && s.m_connB != conn)
        {
            ++it;
            continue;
        }
        //断开的是哪一方，就把另一方交还调用方推送 2305
        P2PEndedSession e;
        e.m_sessionId = it->first;
        e.m_peerConn = (s.m_connA == conn) ? s.m_connB : s.m_connA;
        e.m_peerUserId = (s.m_connA == conn) ? s.m_userB : s.m_userA;
        ended.push_back(e);
        m_sessions.erase(it++);
    }
    return ended;
}

int P2PSessionManager::tryCreate(const std::string& purpose, const std::string& featureSessionId, P2PConnKey connA,
                                 P2PConnKey connB, bool enabledA, bool enabledB, int64_t nowMs, P2PCreateResult& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return createLocked(purpose, featureSessionId, connA, connB, enabledA, enabledB, nowMs, out);
}

int P2PSessionManager::debugOpen(P2PConnKey fromConn, int64_t peerUserId, bool selfEnabled, bool peerEnabled,
                                 int64_t nowMs, P2PCreateResult& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_config.m_debugOpen)
        return error_code_p2p_debug_disabled;
    if (m_registrations.find(fromConn) == m_registrations.end())
        return error_code_bad_request;

    //对端可能多端在线：取它最近一次报到的那条连接
    const P2PConnKey peerConn = latestConnectionLocked(peerUserId, fromConn);
    if (peerConn == 0)
        return error_code_p2p_peer_offline;

    return createLocked(kDebugPurpose, std::string(), fromConn, peerConn, selfEnabled, peerEnabled, nowMs, out);
}

P2PConnKey P2PSessionManager::latestConnectionOf(int64_t userId) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return latestConnectionLocked(userId, 0);
}

P2PConnKey P2PSessionManager::latestConnectionLocked(int64_t userId, P2PConnKey excludeConn) const
{
    P2PConnKey conn = 0;
    uint64_t latest = 0;
    for (std::map<P2PConnKey, Registration>::const_iterator it = m_registrations.begin(); it != m_registrations.end(); ++it)
    {
        if (it->second.m_userId == userId && it->first != excludeConn && it->second.m_order > latest)
        {
            latest = it->second.m_order;
            conn = it->first;
        }
    }
    return conn;
}

int P2PSessionManager::routeSignal(const std::string& sessionId, P2PConnKey fromConn, P2PRoute& out) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    const Session* s = NULL;
    int code = findMemberLocked(sessionId, fromConn, &s);
    if (code != error_code_ok)
        return code;
    out.m_targetConn = (s->m_connA == fromConn) ? s->m_connB : s->m_connA;
    out.m_fromUserId = (s->m_connA == fromConn) ? s->m_userA : s->m_userB;
    return error_code_ok;
}

int P2PSessionManager::checkMember(const std::string& sessionId, P2PConnKey fromConn) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    const Session* s = NULL;
    return findMemberLocked(sessionId, fromConn, &s);
}

int P2PSessionManager::close(const std::string& sessionId, P2PConnKey fromConn, P2PEndedSession& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    const Session* s = NULL;
    int code = findMemberLocked(sessionId, fromConn, &s);
    if (code != error_code_ok)
        return code;
    out.m_sessionId = sessionId;
    out.m_peerConn = (s->m_connA == fromConn) ? s->m_connB : s->m_connA;
    out.m_peerUserId = (s->m_connA == fromConn) ? s->m_userB : s->m_userA;
    m_sessions.erase(sessionId);
    return error_code_ok;
}

size_t P2PSessionManager::sessionCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sessions.size();
}

size_t P2PSessionManager::sessionCountOfUser(int64_t userId) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return countOfUserLocked(userId);
}

int P2PSessionManager::createLocked(const std::string& purpose, const std::string& featureSessionId, P2PConnKey connA,
                                    P2PConnKey connB, bool enabledA, bool enabledB, int64_t nowMs, P2PCreateResult& out)
{
    const int checked = checkCreateLocked(purpose, connA, connB, enabledA, enabledB);
    if (checked != error_code_ok)
        return checked;
    std::map<P2PConnKey, Registration>::const_iterator regA = m_registrations.find(connA);
    std::map<P2PConnKey, Registration>::const_iterator regB = m_registrations.find(connB);
    const int64_t userA = regA->second.m_userId;
    const int64_t userB = regB->second.m_userId;

    //会话号：p2p-<发起方>-<应答方>-<毫秒>-<序号>，序号进程内单调递增，同一毫秒内也不会重名
    char idBuf[kSessionIdBufBytes];
    snprintf(idBuf, sizeof(idBuf), "%s%lld-%lld-%lld-%llu", kSessionIdPrefix, static_cast<long long>(userA),
             static_cast<long long>(userB), static_cast<long long>(nowMs),
             static_cast<unsigned long long>(++m_sessionSeq));
    const std::string sessionId(idBuf);

    //两枚回落中转令牌：同一会话号、角色相反、uid 与 peeruid 对调
    RelayTokenPayload payloadA;
    payloadA.sessionid = sessionId;
    payloadA.role = RelayToken::role_p2p_controlling;
    payloadA.uid = userA;
    payloadA.peeruid = userB;
    payloadA.expire = nowMs + kRelayTokenTtlMs;
    RelayTokenPayload payloadB = payloadA;
    payloadB.role = RelayToken::role_p2p_controlled;
    payloadB.uid = userB;
    payloadB.peeruid = userA;

    P2PSessionNotify common;
    common.m_sessionId = sessionId;
    common.m_purpose = purpose;
    common.m_featureSessionId = featureSessionId;
    common.m_relayIp = m_config.m_relayHost;
    common.m_relayPort = m_config.m_relayPort;
    common.m_connectTimeoutMs = m_config.m_connectTimeoutMs;
    common.m_keepaliveMs = m_config.m_keepaliveMs;
    common.m_linkTimeoutMs = m_config.m_linkTimeoutMs;

    out.m_sessionId = sessionId;
    out.m_connA = connA;
    out.m_connB = connB;
    out.m_notifyA = common;
    out.m_notifyA.m_role = p2p_role_controlling;
    out.m_notifyA.m_peerUid = userB;
    out.m_notifyA.m_peerDeviceId = regB->second.m_deviceId;
    out.m_notifyA.m_token = RelayToken::sign(payloadA, m_config.m_relayTokenKey);
    out.m_notifyB = common;
    out.m_notifyB.m_role = p2p_role_controlled;
    out.m_notifyB.m_peerUid = userA;
    out.m_notifyB.m_peerDeviceId = regA->second.m_deviceId;
    out.m_notifyB.m_token = RelayToken::sign(payloadB, m_config.m_relayTokenKey);

    Session session;
    session.m_connA = connA;
    session.m_connB = connB;
    session.m_userA = userA;
    session.m_userB = userB;
    m_sessions[sessionId] = session;
    return error_code_ok;
}

size_t P2PSessionManager::countOfUserLocked(int64_t userId) const
{
    size_t n = 0;
    for (std::map<std::string, Session>::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second.m_userA == userId || it->second.m_userB == userId)
            ++n;
    }
    return n;
}

int P2PSessionManager::findMemberLocked(const std::string& sessionId, P2PConnKey fromConn, const Session** sessionOut) const
{
    std::map<std::string, Session>::const_iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return error_code_p2p_session_not_exist;
    if (it->second.m_connA != fromConn && it->second.m_connB != fromConn)
        return error_code_p2p_not_member;
    *sessionOut = &it->second;
    return error_code_ok;
}

int P2PSessionManager::checkCreate(const std::string& purpose, P2PConnKey connA, P2PConnKey connB, bool enabledA,
                                   bool enabledB) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return checkCreateLocked(purpose, connA, connB, enabledA, enabledB);
}

int P2PSessionManager::checkCreateLocked(const std::string& purpose, P2PConnKey connA, P2PConnKey connB, bool enabledA,
                                         bool enabledB) const
{
    if (connA == connB)
        return error_code_bad_request;
    std::map<P2PConnKey, Registration>::const_iterator regA = m_registrations.find(connA);
    std::map<P2PConnKey, Registration>::const_iterator regB = m_registrations.find(connB);
    if (regA == m_registrations.end() || regB == m_registrations.end())
        return error_code_p2p_peer_offline;
    if (!enabledA || !enabledB)
        return error_code_p2p_disabled;
    //两端的客户端都要登记了这个用途的业务方：只有一端能接住链路时，它走 P2P、另一端走中转，两端对不上。
    //联调用途不检查（联调会话只在开了 p2pdebugopen 的测试服务端上建）
    if (purpose != kDebugPurpose
        && (regA->second.m_purposes.count(purpose) == 0 || regB->second.m_purposes.count(purpose) == 0))
        return error_code_p2p_purpose_unsupported;

    const size_t limit = m_config.m_maxPerUser > 0 ? static_cast<size_t>(m_config.m_maxPerUser) : 0;
    if (countOfUserLocked(regA->second.m_userId) >= limit || countOfUserLocked(regB->second.m_userId) >= limit)
        return error_code_p2p_too_many;
    return error_code_ok;
}
