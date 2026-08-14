/**
 *  联调测试共用的 TCP 连接与三帧握手脚手架，LiveTestConn.h
 *
 *  协议加密的<u>跨实现联调</u>测试必须走一条真实的 socket：客户端与服务端的加密是
 *  两份互为镜像的实现（flamingoclient/Source/net/FrameCrypto.* 与
 *  flamingoserver/net/FrameCrypto.*），同一个进程里链接不到一起（同名 namespace
 *  下有同名的 FrameCipher、buildFrameAd 等符号，会重复定义），所以只能让两份实现
 *  真的对话一次。
 *
 *  本文件把这类用例都要用到的东西收在一处：winsock 初始化、按帧收发的测试连接、
 *  三帧握手的入口，以及各个测试服务端实例的端口与身份公钥。新增联调用例文件包含
 *  本文件即可，不必各写一份。
 *
 *  典型用法（完整例子见 HandshakeLiveTests.cpp / RelayForwardLiveTests.cpp）：
 *
 *      livetest::TestConn conn;
 *      REQUIRE(conn.Connect(livetest::kTestRelayPort));
 *      net::ClientHandshake hs;
 *      std::string reason;
 *      REQUIRE(livetest::DoHandshake(conn, hs, reason));
 *
 *  <u>本文件只服务于带 [.live] 标签的用例</u>：不做重连、不做缓冲区管理之外的任何
 *  事，收发一律阻塞并带固定超时——服务端没起来时要尽快失败，而不是把整个测试卡住。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <string>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

//单元测试工程本身不联网，故没有链 winsock；联调用例是唯一要建连接的地方，
//在这里就地声明依赖，而不是给整个工程加一项全局的附加依赖。
#pragma comment(lib, "ws2_32.lib")

#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/HandshakeRunner.h"

namespace livetest {

//联调用 chatserver 实例的监听端口。刻意避开正式的 20000 / 20001，以免和开发中正在跑的实例撞上
const unsigned short kTestServerPort = 20100;

//联调用 fileserver 实例的监听端口。fileserver 与 imgserver 共用同一套会话实现
//（fileserversrc/FileSession），所以验通这一条，图片链路的服务端侧也一并有了着落
const unsigned short kTestFileServerPort = 20101;

//联调用 imgserver 实例的中转端口。中转通道的服务端实现（imgserversrc/RelaySession）
//与文件 / 图片那条完全不同：它要把包体用本端密钥解开、再用配对端的密钥重新加密，
//所以必须单独验一次。
const unsigned short kTestRelayPort = 20103;

//上面那些服务端实例的身份公钥（base64），取自它们共用的 etc/server_key.pub。
//服务端重新生成密钥之后这里要跟着改，否则验签会失败——那时的报错正是联调用例要防的
//那种"看着像 bug 其实是配置没跟上"，故在此写明。
const char* const kTestServerPubKey = "QLcjYOULvHX+dgwbRRvlwg0dwNRnO2H6D9a/u+7KZEg=";

//每次收发的超时，单位毫秒。本地回环上握手是毫秒级的，给 3 秒足够；
//给太长会让"服务端没起来"这种情形把整个测试卡住
const int kIoTimeoutMs = 3000;

//一次 recv 的缓冲大小，单位字节。握手帧与心跳应答都只有几百字节，4 KiB 绰绰有余
const int kRecvBufSize = 4096;

/**
 *  确保本进程已初始化 winsock，进程内只做一次。
 *
 *  主程序是在 InitSocket 里调 WSAStartup 的，而单元测试进程根本不走那条启动路径，
 *  不补这一步的话 socket() 一律失败，现象是"连不上服务端"，很容易误判成服务端没起。
 */
inline void EnsureWinsockReady()
{
    static bool s_started = false;
    if (s_started)
        return;

    WSADATA wsaData;
    memset(&wsaData, 0, sizeof(wsaData));
    //2.2 是自 Windows 98 起就有的版本，本仓库主程序用的也是它
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
        s_started = true;
}

/**
 *  一条测试用的 TCP 连接，负责连接、按帧收发，并在析构时关闭。
 *  只服务于联调用例，故不做重连、不做缓冲区管理之外的任何事。
 *
 *  同时实现 net::IHandshakeChannel，好把自己直接交给产品代码里的握手流程去驱动。
 */
class TestConn : public net::IHandshakeChannel
{
public:
    TestConn()
        : m_sock(INVALID_SOCKET)
    {
    }

    ~TestConn()
    {
        Close();
    }

    /**
     *  连接本机（127.0.0.1）的指定端口。
     *  @param port 服务端监听端口
     *  @return 连接成功返回 true；建套接字或连接失败返回 false，此时本对象仍可析构
     *  副作用：首次调用会初始化本进程的 winsock。
     */
    bool Connect(unsigned short port)
    {
        EnsureWinsockReady();

        m_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_sock == INVALID_SOCKET)
            return false;

        DWORD timeout = (DWORD)kIoTimeoutMs;
        ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        ::setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");

        if (::connect(m_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        {
            Close();
            return false;
        }
        return true;
    }

    //关闭本连接；未连接时什么都不做，可重复调用
    void Close()
    {
        if (m_sock != INVALID_SOCKET)
        {
            ::closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    //本连接是否已建立
    bool Valid() const
    {
        return m_sock != INVALID_SOCKET;
    }

    /**
     *  把一段完整的帧字节全部发出去（实现 net::IHandshakeChannel）。
     *  @param data 待发送字节的首地址，不接管其所有权
     *  @param len  待发送字节数
     *  @return 全部发出返回 true；中途出错或超时返回 false
     */
    virtual bool SendAll(const char* data, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            const int n = ::send(m_sock, data + sent, (int)(len - sent), 0);
            if (n <= 0)
                return false;
            sent += (size_t)n;
        }
        return true;
    }

    /**
     *  收满 len 个字节（实现 net::IHandshakeChannel）。
     *
     *  先把握手前累积缓冲里剩的字节用掉，再从 socket 上补齐——RecvFrame 与本方法
     *  共用同一个缓冲，不这么做会丢掉上一次多收进来的那一段。
     *
     *  @param buf [出参] 接收缓冲，容量须不小于 len
     *  @param len 要收的字节数
     *  @return 收满返回 true；超时或对端关闭返回 false
     */
    virtual bool RecvAll(char* buf, size_t len)
    {
        size_t got = 0;

        if (!m_recvBuf.empty())
        {
            const size_t take = m_recvBuf.size() < len ? m_recvBuf.size() : len;
            memcpy(buf, m_recvBuf.data(), take);
            m_recvBuf.erase(0, take);
            got = take;
        }

        while (got < len)
        {
            const int n = ::recv(m_sock, buf + got, (int)(len - got), 0);
            if (n <= 0)
                return false;
            got += (size_t)n;
        }
        return true;
    }

    //发一段完整的帧字节，供不走握手流程的用例直接用
    bool SendAll(const std::string& data)
    {
        return SendAll(data.data(), data.size());
    }

    /**
     *  收一个完整的 v2 帧。内部维护累积缓冲，收到半个帧会继续等。
     *  @param header [出参] 帧头
     *  @param body   [出参] <u>线上</u>包体（未解密、未解压）
     *  @param maxbody 包体长度上限，用于帧合法性校验；中转通道承载屏幕帧 / 大分片，
     *                 应传 MAX_BODY_FILESERVER
     *  @return 超时或对端关闭时返回 false
     */
    bool RecvFrame(frame_header& header, std::string& body,
                   uint32_t maxbody = MAX_BODY_CHATSERVER)
    {
        while (true)
        {
            size_t consumed = 0;
            const net::frame_decode_result r =
                net::decodeFrame(m_recvBuf.data(), m_recvBuf.size(), header, body, consumed, maxbody);
            if (r == net::frame_decode_ok)
            {
                m_recvBuf.erase(0, consumed);
                return true;
            }
            if (r == net::frame_decode_error)
                return false;

            char buf[kRecvBufSize] = { 0 };
            const int n = ::recv(m_sock, buf, kRecvBufSize, 0);
            if (n <= 0)
                return false;
            m_recvBuf.append(buf, (size_t)n);
        }
    }

private:
    SOCKET      m_sock;      //本连接的套接字，INVALID_SOCKET 表示未连接
    std::string m_recvBuf;   //收取累积缓冲，可能含不足一帧的尾巴
};

/**
 *  在一条已连上的连接上走完三帧握手。
 *
 *  <u>直接调用产品代码里的 net::performClientHandshake</u>，而不是在测试里另写一份
 *  流程。这一点是刻意的：先前测试与产品各写了一份，两份都踩了 encodeFrame 的追加
 *  语义，于是测试全绿、真机却握不上手。共用一份之后，联调用例验的就是真正跑的那段代码。
 *
 *  @param conn   已连接的测试连接，不接管其所有权
 *  @param hs     [出参] 握手器，成功后其 cipher() 即可用
 *  @param reason [出参] 失败原因，便于用例断言失败时看清是哪一步挂的
 *  @return 三帧全部走通返回 true
 *  副作用：会在 conn 上收发三帧握手报文。
 */
inline bool DoHandshake(TestConn& conn, net::ClientHandshake& hs, std::string& reason)
{
    reason.clear();

    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    if (!net::parseServerPublicKey(kTestServerPubKey, serverPubKey))
    {
        reason = "cannot parse kTestServerPubKey";
        return false;
    }

    return net::performClientHandshake(conn, serverPubKey, hs, reason);
}

} // namespace livetest
