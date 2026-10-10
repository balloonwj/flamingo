/**
 *  P2P 信令处理（服务端侧）实现，P2PSignalService.cpp
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PSignalService.h"

#include "P2PProtocol.h"

namespace {

//各应答里 msg 字段的说明文字（英文，给排查的人看；客户端按 code 处理）
const char* const kMsgBadRequest = "bad request";
const char* const kMsgVersion = "unsupported p2p version";
const char* const kMsgUnknownCmd = "unsupported p2p command";

} // namespace

P2PSignalService::P2PSignalService(P2PSessionManager& manager, IP2PEnterpriseGate& gate)
    : m_manager(manager)
    , m_gate(gate)
{
}

int P2PSignalService::handle(uint16_t cmd, const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    switch (cmd)
    {
    //客户端登录后报到 P2P 协议版本
    case cmd_p2p_register:
        return handleRegister(ctx, body, out);

    //交换候选地址与公钥：转给会话对端
    case cmd_p2p_signal:
        return handleSignal(ctx, body, out);

    //上报建链结果：只核对成员身份，供日志与统计
    case cmd_p2p_result:
        return handleResult(ctx, body, out);

    //客户端结束一个会话：删除会话并通知对端
    case cmd_p2p_close:
        return handleClose(ctx, body, out);

    //联调用的直接建会话
    case cmd_p2p_debug_open:
        return handleDebugOpen(ctx, body, out);

    //2302 只由服务端推送，2300 段其余号码尚未定义：客户端发来一律按报文非法应答，不断开连接
    default:
        append(out, ctx.m_conn, cmd, ctx.m_seq, P2PProtocol::buildSimpleResponse(error_code_bad_request, kMsgUnknownCmd));
        return error_code_bad_request;
    }
}

void P2PSignalService::onDisconnect(P2PConnKey conn, std::vector<P2POutFrame>& out)
{
    std::vector<P2PEndedSession> ended = m_manager.unregisterConnection(conn);
    for (size_t i = 0; i < ended.size(); ++i)
    {
        P2PCloseMessage msg;
        msg.m_sessionId = ended[i].m_sessionId;
        msg.m_reason = p2p_close_peer_offline;
        msg.m_errCode = error_code_p2p_peer_offline;
        append(out, ended[i].m_peerConn, cmd_p2p_close, 0, P2PProtocol::buildClose(msg));
    }
}

int P2PSignalService::handleRegister(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    const std::vector<P2PProbeAddress> noProbes;
    int version = 0;
    std::vector<std::string> purposes;
    if (!P2PProtocol::parseRegisterRequest(body, version, purposes))
    {
        append(out, ctx.m_conn, cmd_p2p_register, ctx.m_seq,
               P2PProtocol::buildRegisterResponse(error_code_bad_request, kMsgBadRequest, false, noProbes));
        return error_code_bad_request;
    }

    int code = m_manager.registerConnection(ctx.m_conn, ctx.m_userId, ctx.m_deviceId, version, purposes);
    if (code != error_code_ok)
    {
        append(out, ctx.m_conn, cmd_p2p_register, ctx.m_seq, P2PProtocol::buildRegisterResponse(code, kMsgVersion, false, noProbes));
        return code;
    }

    //企业关闭时照样登记（开关可能在运行中打开，建会话时会再查一次），只是告诉客户端暂不必收集地址
    const bool enabled = m_gate.isP2PEnabledForUser(ctx.m_userId);
    append(out, ctx.m_conn, cmd_p2p_register, ctx.m_seq,
           P2PProtocol::buildRegisterResponse(error_code_ok, "", enabled, enabled ? m_manager.getConfig().m_probes : noProbes));
    return error_code_ok;
}

int P2PSignalService::handleSignal(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    P2PSignalRequest req;
    int code = P2PProtocol::parseSignalRequest(body, req);
    if (code == error_code_ok)
    {
        P2PRoute route;
        code = m_manager.routeSignal(req.m_sessionId, ctx.m_conn, route);
        if (code == error_code_ok)
            append(out, route.m_targetConn, cmd_p2p_signal, 0, P2PProtocol::buildSignalForward(req.m_sessionId, route.m_fromUserId, req.m_data));
    }
    append(out, ctx.m_conn, cmd_p2p_signal, ctx.m_seq, P2PProtocol::buildSimpleResponse(code, code == error_code_ok ? "" : kMsgBadRequest));
    return code;
}

int P2PSignalService::handleResult(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    P2PResultReport report;
    int code = error_code_bad_request;
    if (P2PProtocol::parseResultReport(body, report))
        code = m_manager.checkMember(report.m_sessionId, ctx.m_conn);
    append(out, ctx.m_conn, cmd_p2p_result, ctx.m_seq, P2PProtocol::buildSimpleResponse(code, code == error_code_ok ? "" : kMsgBadRequest));
    return code;
}

int P2PSignalService::handleClose(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    P2PCloseMessage msg;
    int code = error_code_bad_request;
    if (P2PProtocol::parseClose(body, msg))
    {
        P2PEndedSession ended;
        code = m_manager.close(msg.m_sessionId, ctx.m_conn, ended);
        //原因与错误码原样推给对端：对端据此知道是业务结束还是对方建链失败
        if (code == error_code_ok)
            append(out, ended.m_peerConn, cmd_p2p_close, 0, P2PProtocol::buildClose(msg));
    }
    append(out, ctx.m_conn, cmd_p2p_close, ctx.m_seq, P2PProtocol::buildSimpleResponse(code, code == error_code_ok ? "" : kMsgBadRequest));
    return code;
}

int P2PSignalService::handleDebugOpen(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out)
{
    int64_t peerUid = 0;
    if (!P2PProtocol::parseDebugOpenRequest(body, peerUid))
    {
        append(out, ctx.m_conn, cmd_p2p_debug_open, ctx.m_seq,
               P2PProtocol::buildDebugOpenResponse(error_code_bad_request, kMsgBadRequest, std::string()));
        return error_code_bad_request;
    }

    P2PCreateResult created;
    int code = m_manager.debugOpen(ctx.m_conn, peerUid, m_gate.isP2PEnabledForUser(ctx.m_userId),
                                   m_gate.isP2PEnabledForUser(peerUid), ctx.m_nowMs, created);
    if (code != error_code_ok)
    {
        append(out, ctx.m_conn, cmd_p2p_debug_open, ctx.m_seq, P2PProtocol::buildDebugOpenResponse(code, kMsgBadRequest, std::string()));
        return code;
    }

    //先回应答，再给双方各推一份 2302：客户端先拿到会话号，随后的推送就能对上
    append(out, ctx.m_conn, cmd_p2p_debug_open, ctx.m_seq, P2PProtocol::buildDebugOpenResponse(error_code_ok, "", created.m_sessionId));
    append(out, created.m_connA, cmd_p2p_session_notify, 0, P2PProtocol::buildSessionNotify(created.m_notifyA));
    append(out, created.m_connB, cmd_p2p_session_notify, 0, P2PProtocol::buildSessionNotify(created.m_notifyB));
    return error_code_ok;
}

int P2PSignalService::createForFeature(const std::string& purpose, const std::string& featureSessionId, int64_t userA,
                                       int64_t userB, int64_t nowMs, std::string& sessionIdOut, std::vector<P2POutFrame>& out)
{
    sessionIdOut.clear();
    const P2PConnKey connA = m_manager.latestConnectionOf(userA);
    const P2PConnKey connB = m_manager.latestConnectionOf(userB);
    if (connA == 0 || connB == 0)
        return error_code_p2p_peer_offline;
    return createForConnections(purpose, featureSessionId, connA, userA, connB, userB, nowMs, sessionIdOut, out);
}

int P2PSignalService::createForConnections(const std::string& purpose, const std::string& featureSessionId,
                                           P2PConnKey connA, int64_t userA, P2PConnKey connB, int64_t userB,
                                           int64_t nowMs, std::string& sessionIdOut, std::vector<P2POutFrame>& out)
{
    sessionIdOut.clear();
    //连接没有报到过时 tryCreate 返回 error_code_p2p_peer_offline，这里不另做检查
    P2PCreateResult created;
    const int code = m_manager.tryCreate(purpose, featureSessionId, connA, connB, m_gate.isP2PEnabledForUser(userA),
                                         m_gate.isP2PEnabledForUser(userB), nowMs, created);
    if (code != error_code_ok)
        return code;
    sessionIdOut = created.m_sessionId;
    append(out, created.m_connA, cmd_p2p_session_notify, 0, P2PProtocol::buildSessionNotify(created.m_notifyA));
    append(out, created.m_connB, cmd_p2p_session_notify, 0, P2PProtocol::buildSessionNotify(created.m_notifyB));
    return error_code_ok;
}

void P2PSignalService::append(std::vector<P2POutFrame>& out, P2PConnKey conn, uint16_t cmd, uint32_t seq, const std::string& body)
{
    P2POutFrame f;
    f.m_conn = conn;
    f.m_cmd = cmd;
    f.m_seq = seq;
    f.m_body = body;
    out.push_back(f);
}

int P2PSignalService::checkForFeature(const std::string& purpose, int64_t userA, int64_t userB)
{
    //与 createForFeature 取同样的连接：各取最近一次报到的那条
    const P2PConnKey connA = m_manager.latestConnectionOf(userA);
    const P2PConnKey connB = m_manager.latestConnectionOf(userB);
    if (connA == 0 || connB == 0)
        return error_code_p2p_peer_offline;
    return m_manager.checkCreate(purpose, connA, connB, m_gate.isP2PEnabledForUser(userA),
                                 m_gate.isP2PEnabledForUser(userB));
}
