/**
 *  管理控制台长连接会话类的实现，AdminLinkSession.cpp
 *  说明见 AdminLinkSession.h，协议见 docs/协议设计-v2.md 第 22 节。
 *  balloonwj@qq.com   2026-10-04
 */
#include "AdminLinkSession.h"

#include <time.h>

#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "AdminLinkProtocol.h"
#include "AdminLinkServer.h"
#include "AdminManager.h"
#include "EnterpriseManager.h"

AdminLinkSession::AdminLinkSession(const std::shared_ptr<TcpConnection>& conn) :
    TcpSession(conn),
    m_connectTimeSec(static_cast<int64_t>(time(NULL))),
    m_lastRecvSec(static_cast<int64_t>(time(NULL))),
    m_authed(false),
    m_adminId(0),
    m_adminEnterpriseId(0)
{
    if (conn)
        m_peer = conn->peerAddress().toIpPort();
}

AdminLinkSession::~AdminLinkSession()
{
}

void AdminLinkSession::getIdentity(std::string& outToken, int64_t& outAdminId, int64_t& outAdminEnterpriseId)
{
    std::lock_guard<std::mutex> guard(m_identityMutex);
    outToken = m_token;
    outAdminId = m_adminId;
    outAdminEnterpriseId = m_adminEnterpriseId;
}

void AdminLinkSession::close(const char* reason)
{
    LOGI("adminlink close connection: %s, client: %s", (reason != NULL) ? reason : "", m_peer.c_str());
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
        conn->forceClose();
}

void AdminLinkSession::onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime)
{
    (void)receivTime;

    //一个缓冲区可能含 0~N 个完整帧，循环解出每一个
    while (true)
    {
        frame_header header;
        std::string body;
        size_t consumed = 0;
        net::frame_decode_result result =
            net::decodeFrame(pBuffer->peek(), pBuffer->readableBytes(), header, body, consumed);

        //数据不足一个完整帧，等待后续数据
        if (result == net::frame_decode_need_more)
            return;

        //协议错误：魔数 / 版本 / 长度 / CRC 非法
        if (result == net::frame_decode_error)
        {
            LOGE("adminlink decode frame error, close connection, client: %s", m_peer.c_str());
            conn->forceClose();
            return;
        }

        pBuffer->retrieve(consumed);
        m_lastRecvSec.store(static_cast<int64_t>(time(NULL)));

        //加密握手与解密，必须排在业务分发之前
        bool securityHandled = false;
        if (!processFrameSecurity(conn, header, body, securityHandled))
            return;

        //握手帧已在上面处理并应答完毕
        if (securityHandled)
            continue;

        if (!process(header.cmd, header.seq, body))
        {
            LOGE("adminlink process frame error, cmd: %u, close connection, client: %s",
                 (unsigned)header.cmd, m_peer.c_str());
            conn->forceClose();
            return;
        }
    }
}

bool AdminLinkSession::processFrameSecurity(const std::shared_ptr<TcpConnection>& conn,
                                            const frame_header& header, std::string& body, bool& handled)
{
    handled = false;

    std::string response;
    std::string err;
    net::frame_security_result r =
        net::processInboundFrame(m_handshake, header.cmd, header.seq, header.flags,
                                 header.originlen, body, response, err);

    if (r == net::frame_security_error)
    {
        LOGE("adminlink frame security check failed: %s, cmd: %u, close connection, client: %s",
             err.c_str(), (unsigned)header.cmd, m_peer.c_str());
        conn->forceClose();
        return false;
    }

    if (r == net::frame_security_handled)
    {
        handled = true;

        //三帧握手里只有第 2 帧有应答，且必须明文发出；此时 m_handshake.ready() 仍为 false，
        //send 走的正是明文分支，理由同 ChatSession::processFrameSecurity
        if (!response.empty())
            send(cmd_handshake, static_cast<int32_t>(header.seq), response);

        if (m_handshake.ready())
            LOGI("adminlink handshake done, client: %s", m_peer.c_str());
    }

    return true;
}

bool AdminLinkSession::process(uint16_t cmd, uint32_t seq, const std::string& body)
{
    switch (cmd)
    {
    //心跳：鉴权前后都允许，不打日志（过于频繁）
    case cmd_heartbeat:
        onHeartbeat(seq);
        return true;

    //鉴权：鉴权前唯一允许的业务命令；已鉴权后再发也照常处理（以新 token 为准）
    case cmd_adminlink_auth:
        onAuth(seq, body);
        return true;

    //其余命令都要求已鉴权，在下面的 switch 里分发
    default:
        break;
    }

    if (!m_authed.load())
    {
        LOGE("adminlink recv cmd %u before auth, client: %s", (unsigned)cmd, m_peer.c_str());
        return false;
    }

    switch (cmd)
    {
    //订阅某企业的在线名单
    case cmd_adminlink_subscribe_online:
        onSubscribe(seq, body);
        return true;

    //退订在线名单
    case cmd_adminlink_unsubscribe_online:
        onUnsubscribe(seq);
        return true;

    //管理端口不受理的命令（包括员工客户端的全部命令）：断开连接
    default:
        LOGE("adminlink unsupported cmd %u, client: %s", (unsigned)cmd, m_peer.c_str());
        return false;
    }
}

void AdminLinkSession::onHeartbeat(uint32_t seq)
{
    send(cmd_heartbeat, static_cast<int32_t>(seq), std::string());
}

void AdminLinkSession::onAuth(uint32_t seq, const std::string& body)
{
    std::string token;
    std::string clientVersion;
    if (!AdminLinkProtocol::parseAuthRequest(body, token, clientVersion))
    {
        LOGE("adminlink bad auth request, client: %s", m_peer.c_str());
        send(cmd_adminlink_auth, static_cast<int32_t>(seq),
             AdminLinkProtocol::buildAuthResponse(error_code_bad_request, "bad auth request", 0, 0));
        return;
    }

    //token 校验沿用 HTTP 管理接口的同一个函数：会话存在、未过期、管理员未停用
    AdminInfo admin;
    if (!Singleton<AdminManager>::Instance().checkToken(token, admin))
    {
        LOGI("adminlink auth failed: token invalid, client: %s", m_peer.c_str());
        send(cmd_adminlink_auth, static_cast<int32_t>(seq),
             AdminLinkProtocol::buildAuthResponse(error_code_adminlink_token_invalid, "token invalid", 0, 0));
        return;
    }

    {
        std::lock_guard<std::mutex> guard(m_identityMutex);
        m_token = token;
        m_adminId = admin.adminid;
        m_adminEnterpriseId = admin.enterpriseid;
    }
    m_authed.store(true);

    send(cmd_adminlink_auth, static_cast<int32_t>(seq),
         AdminLinkProtocol::buildAuthResponse(error_code_ok, "ok", admin.adminid, admin.enterpriseid));

    LOGI("adminlink auth ok, adminid: %lld, enterpriseid: %lld, clientversion: %s, client: %s",
         (long long)admin.adminid, (long long)admin.enterpriseid, clientVersion.c_str(), m_peer.c_str());
}

void AdminLinkSession::onSubscribe(uint32_t seq, const std::string& body)
{
    int64_t enterpriseId = 0;
    if (!AdminLinkProtocol::parseSubscribeRequest(body, enterpriseId))
    {
        LOGE("adminlink bad subscribe request, body: %s, client: %s", body.c_str(), m_peer.c_str());
        send(cmd_adminlink_subscribe_online, static_cast<int32_t>(seq),
             AdminLinkProtocol::buildSubscribeResponse(error_code_bad_request, "bad subscribe request", 0));
        return;
    }

    std::string token;
    int64_t adminId = 0;
    int64_t adminEnterpriseId = 0;
    getIdentity(token, adminId, adminEnterpriseId);

    EnterpriseInfo entInfo;
    const bool exists = Singleton<EnterpriseManager>::Instance().getEnterprise(enterpriseId, entInfo);
    const int32_t code = AdminLinkProtocol::checkSubscribePermission(adminEnterpriseId, enterpriseId, exists);
    if (code != error_code_ok)
    {
        LOGI("adminlink subscribe rejected, code: %d, adminid: %lld, admin enterprise: %lld, "
             "requested enterprise: %lld, client: %s",
             code, (long long)adminId, (long long)adminEnterpriseId, (long long)enterpriseId, m_peer.c_str());
        send(cmd_adminlink_subscribe_online, static_cast<int32_t>(seq),
             AdminLinkProtocol::buildSubscribeResponse(code, "subscribe rejected", 0));
        return;
    }

    //登记订阅、回应答、发完整名单都在推送线程上做，保证三者与之后的变化推送顺序一致
    std::shared_ptr<AdminLinkSession> self = std::static_pointer_cast<AdminLinkSession>(shared_from_this());
    Singleton<AdminLinkServer>::Instance().requestSubscribe(self, enterpriseId, seq);
}

void AdminLinkSession::onUnsubscribe(uint32_t seq)
{
    std::shared_ptr<AdminLinkSession> self = std::static_pointer_cast<AdminLinkSession>(shared_from_this());
    Singleton<AdminLinkServer>::Instance().requestUnsubscribe(self, seq);
}
