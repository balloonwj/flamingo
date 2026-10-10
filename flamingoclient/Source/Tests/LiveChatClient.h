/**
 *  联调测试用的 chatserver 客户端，LiveChatClient.h
 *
 *  在 LiveTestConn 的「连接 + 三帧握手」之上补齐联调用例常用的几件事：用账号密码登录、按自增 seq 发送
 *  加密的业务请求、等待某个命令的应答或推送。等待期间读到的其它帧不会丢，而是暂存起来，之后等别的命令时
 *  先在暂存里找 —— 服务端登录后会连续推送会话列表、在线状态等，用例只关心其中几条。
 *
 *  只服务于带 [.live] 标签的用例：收发阻塞且带超时，服务端没起来时尽快失败。
 *
 *  典型用法（完整例子见 ScreenShareLiveTests.cpp）：
 *      livetest::LiveChatClient a;
 *      std::string reason;
 *      REQUIRE(a.ConnectAndLogin(port, serverPubKey, "MY0127", "perf1234", reason));
 *      REQUIRE(a.Send(cmd_screenshare_start, "{\"ctype\":1,\"cid\":\"5308\"}"));
 *      std::string body;
 *      REQUIRE(a.WaitFor(cmd_screenshare_start, body, 5000));
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <deque>
#include <string>
#include <utility>
#include <windows.h>

#include "LiveTestConn.h"
#include "../net/FramePayload.h"
#include "../net/LoginProtocolClient.h"
#include "../net/Protocol.h"
#include "../net/Sha256.h"
#include "../ClientVersion.h"

namespace livetest {

class LiveChatClient
{
public:
    LiveChatClient()
        : m_seq(0)
        , m_userId(0)
    {
    }

    /**
     *  连接本机 chatserver、走完三帧握手并登录。
     *  @param port         chatserver 监听端口
     *  @param serverPubKey 服务端身份公钥（base64），须与服务端 etc/server_key.pub 一致
     *  @param account      登录账号
     *  @param password     明文密码（本方法内部做 SHA-256）
     *  @param reason       [出参] 失败原因
     *  @return 登录成功返回 true，此后 UserId() 有效
     */
    bool ConnectAndLogin(unsigned short port, const char* serverPubKey, const std::string& account,
                         const std::string& password, std::string& reason)
    {
        reason.clear();
        if (!net::cryptoInit())
        {
            reason = "cryptoInit failed";
            return false;
        }
        if (!m_conn.Connect(port))
        {
            reason = "connect failed";
            return false;
        }
        unsigned char pubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
        if (!net::parseServerPublicKey(serverPubKey, pubKey))
        {
            reason = "bad server public key";
            return false;
        }
        if (!net::performClientHandshake(m_conn, pubKey, m_hs, reason))
        {
            return false;
        }

        net::LoginRequestParams params;
        params.clientversion = FLAMINGO_CLIENT_VERSION;
        params.account = account;
        params.password = net::Sha256::hexDigest(password);
        params.clienttype = platform_pc;
        params.presence = presence_online;
        params.deviceid = "livetest-" + account;
        params.devicename = "livetest";
        if (!Send(cmd_login, net::LoginProtocolClient::buildLoginRequest(params)))
        {
            reason = "send login failed";
            return false;
        }
        std::string body;
        if (!WaitFor(cmd_login, body, kLoginTimeoutMs))
        {
            reason = "no login response";
            return false;
        }
        net::LoginResponse resp;
        if (!net::LoginProtocolClient::parseLoginResponse(body, resp) || resp.code != 0)
        {
            reason = "login rejected: " + body;
            return false;
        }
        m_userId = resp.userid;
        return true;
    }

    /**
     *  发送一条加密的业务请求，seq 自增。
     *  @param cmd  命令号
     *  @param body 请求包体（UTF-8 JSON）
     *  @return 编码并发出返回 true
     */
    bool Send(uint16_t cmd, const std::string& body)
    {
        std::string wire;
        uint8_t flags = 0;
        uint32_t originlen = 0;
        ++m_seq;
        if (!net::encodeFramePayload(cmd, m_seq, body, &m_hs.cipher(), 0, net::payload_compress_never, wire, flags,
                                     originlen))
        {
            return false;
        }
        std::string frame;
        if (!net::encodeFrame(cmd, m_seq, flags, wire.data(), (uint32_t)wire.size(), originlen, frame))
        {
            return false;
        }
        return m_conn.SendAll(frame);
    }

    /**
     *  等待某个命令的应答或推送，返回解密后的包体。先在暂存里找，找不到再从连接上读；读到的其它帧暂存起来。
     *  @param cmd       要等的命令号
     *  @param body      [出参] 解密后的包体
     *  @param timeoutMs 最长等待时间，单位毫秒
     *  @return 等到返回 true；超时、连接断开或解密失败返回 false
     */
    bool WaitFor(uint16_t cmd, std::string& body, int timeoutMs)
    {
        for (std::deque<std::pair<uint16_t, std::string> >::iterator it = m_backlog.begin(); it != m_backlog.end(); ++it)
        {
            if (it->first == cmd)
            {
                body = it->second;
                m_backlog.erase(it);
                return true;
            }
        }
        const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
        while (::GetTickCount64() < deadline)
        {
            frame_header header;
            std::string wire;
            if (!m_conn.RecvFrame(header, wire))
            {
                //单次 recv 超时（kIoTimeoutMs）后继续等，直到总时限到期；连接断开则立即失败
                if (!m_conn.Valid())
                {
                    return false;
                }
                continue;
            }
            std::string plain;
            std::string err;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &m_hs.cipher(),
                                         plain, err))
            {
                return false;
            }
            if (header.cmd == cmd)
            {
                body = plain;
                return true;
            }
            m_backlog.push_back(std::make_pair(header.cmd, plain));
        }
        return false;
    }

    /**
     *  按到达顺序取下一帧（先取暂存里的），用于要把服务端的应答与推送按原顺序交给被测对象的用例（文件共享的
     *  传输联调：2224 的应答必须先于随后的 2222 交给被测对象）。可以在一条专门收帧的线程上调用，与主线程的 Send
     *  并行：加解密各方向有自己的锁，收发走同一个套接字的两个方向。
     *  暂存里的帧没有记 seq，取出时 seq 为 0（暂存的都是登录期间等别的命令时读到的推送）。
     *  @param cmd       [出参] 命令号
     *  @param seq       [出参] 帧序号；应答为对应请求的 seq，推送为 0
     *  @param body      [出参] 解密后的包体
     *  @param timeoutMs 最长等待时间，单位毫秒（单次读超时 kIoTimeoutMs 时可能多等一些）
     *  @return 取到返回 true；超时、连接断开或解密失败返回 false
     */
    bool WaitAny(uint16_t& cmd, uint32_t& seq, std::string& body, int timeoutMs)
    {
        if (!m_backlog.empty())
        {
            cmd = m_backlog.front().first;
            body = m_backlog.front().second;
            seq = 0;
            m_backlog.pop_front();
            return true;
        }
        const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
        while (::GetTickCount64() < deadline)
        {
            frame_header header;
            std::string wire;
            if (!m_conn.RecvFrame(header, wire))
            {
                if (!m_conn.Valid())
                {
                    return false;
                }
                continue;
            }
            std::string err;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &m_hs.cipher(),
                                         body, err))
            {
                return false;
            }
            cmd = header.cmd;
            seq = header.seq;
            return true;
        }
        return false;
    }

    //丢弃暂存的帧（用例换一个阶段、只关心之后到达的推送时调用）
    void ClearBacklog()
    {
        m_backlog.clear();
    }

    //断开连接（模拟掉线）
    void Close()
    {
        m_conn.Close();
    }

    //登录成功后的本人 userid
    int64_t UserId() const
    {
        return m_userId;
    }

private:
    //登录应答的最长等待时间，单位毫秒：登录要查库、建会话，比普通请求慢
    static const int kLoginTimeoutMs = 8000;

    TestConn                                       m_conn;      //底层连接
    net::ClientHandshake                           m_hs;        //握手器，握手成功后其 cipher() 用于加解密业务帧
    uint32_t                                       m_seq;       //请求 seq，每发一条自增
    int64_t                                        m_userId;    //登录成功后的 userid
    std::deque<std::pair<uint16_t, std::string> >  m_backlog;   //等待别的命令时读到的帧（命令号, 解密后包体）
};

} // namespace livetest
