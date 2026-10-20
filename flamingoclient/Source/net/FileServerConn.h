/**
 *  独立 fileserver 连接 FileServerConn，FileServerConn.h
 *
 *  自带 socket 的一条 fileserver 连接，实现 IFileFrameChannel，供
 *  FileStreamTransfer 的上传 / 下载跑分片收发。不碰 CIUSocket 的共享
 *  m_hFileSocket，因而可以多条并行、也可以在<u>未登录</u>时使用。
 *
 *  两处在用：
 *    - net/FileTransfer：群文件并发池里每笔传输各持一条，传完即断；
 *    - Update/UpdateManager：下载升级包。升级可能发生在登录被拒
 *      （版本过低）之后，那时根本没有共享文件连接可用。
 *
 *  本类原先内联在 FileTransfer.cpp 里，为了让升级下载复用而不是再抄
 *  一份 socket 代码，整体搬到本头文件（实现本来就全部内联在类体里）。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include "FileStreamTransfer.h"   // IFileFrameChannel
#include "FrameCodec.h"          // FRAME_HEADER_SIZE / MAX_BODY_FILESERVER / decodeFrame（内含 Protocol.h）
#include "FrameCrypto.h"         // net::ClientHandshake（本连接自己的会话密钥）
#include "FramePayload.h"        // net::decodeFramePayload（收包时的解密与解压）
#include "HandshakeRunner.h"     // net::performClientHandshake / IHandshakeChannel
#include "IUSocket.h"            // CIUSocket::GetServerPublicKey（连到三个服务的各条链路共用同一把公钥）
#include "TcpConnectWait.h"      // ConnectWithTimeout / DescribeTcpConnect：带超时的非阻塞 connect
#include "../Common/Log/IULog.h"            // LOG_INFO / LOG_ERROR

#include <string>
#include <vector>

namespace net {

//独立 fileserver 连接：群文件并发池里每笔传输各持有一条、传完即断，自带 socket，不碰
//CIUSocket 的共享 m_hFileSocket。连接 / 发送 / 收帧的实现照搬 CIUSocket 的
//ConnectToFileServer / SendOnFilePort / RecvOnFilePort / RecvOneFileFrame，只是作用在
//本对象自己的 m_sock 上。
class FileServerConn : public IFileFrameChannel
{
public:
    FileServerConn()
        : m_sock(INVALID_SOCKET)
    {
    }

    ~FileServerConn() override
    {
        Close();
    }

    //连到 fileserver（地址 / 端口取自 CIUSocket）。非阻塞 connect，用 ConnectWithTimeout 同时等可写与异常，
    //语义与 CIUSocket::ConnectToFileServer 一致，连上后仍为非阻塞模式。成功返回 true。
    bool Connect(const std::string& addr, short port, int timeoutSec)
    {
        Close();

        m_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_sock == INVALID_SOCKET)
            return false;

        long tmSend  = 3 * 1000L;
        long tmRecv  = 3 * 1000L;
        long noDelay = 1;
        setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (LPSTR)&noDelay, sizeof(long));
        setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (LPSTR)&tmSend, sizeof(long));
        setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (LPSTR)&tmRecv, sizeof(long));

        //非阻塞：与共享 socket 同口径，收发用 WSAEWOULDBLOCK + Sleep + 超时轮询
        unsigned long on = 1;
        if (::ioctlsocket(m_sock, FIONBIO, &on) == SOCKET_ERROR)
        {
            Close();
            return false;
        }

        struct sockaddr_in addrSrv = { 0 };
        struct hostent* pHostent = NULL;
        if ((addrSrv.sin_addr.s_addr = inet_addr(addr.c_str())) == INADDR_NONE)
        {
            pHostent = ::gethostbyname(addr.c_str());
            if (pHostent == NULL)
            {
                LOG_ERROR("FileServerConn: resolve fileserver %s failed.", addr.c_str());
                Close();
                return false;
            }
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
        }
        addrSrv.sin_family = AF_INET;
        addrSrv.sin_port   = htons((u_short)port);

        //被拒绝、不可达时立即返回，失败原因与超时分开写进日志（2026-10-14 起，原来只等「可写」，被拒绝也要等满超时）
        const int timeoutMs = timeoutSec * kTcpConnectMsPerSecond;
        int connectError = 0;
        const tcp_connect_result connectResult = ConnectWithTimeout(m_sock, addrSrv, timeoutMs, connectError);
        if (connectResult != tcp_connect_ok)
        {
            LOG_ERROR("FileServerConn: connect fileserver %s:%d %s.", addr.c_str(), (int)port,
                      DescribeTcpConnect(connectResult, connectError, timeoutMs).c_str());
            Close();
            return false;
        }
        return PerformHandshake(timeoutSec);
    }

    //关闭本连接（幂等）。Stop 时可从别的线程调以解阻塞正在收发的池线程。
    void Close()
    {
        if (m_sock != INVALID_SOCKET)
        {
            ::closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    bool SendBytes(const char* data, int64_t size, int timeoutSec) override
    {
        if (m_sock == INVALID_SOCKET)
            return false;

        int64_t startTime = (int64_t)time(NULL);
        int64_t sent = 0;
        while (sent < size)
        {
            int nRet = ::send(m_sock, data + sent, (int)(size - sent), 0);
            if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
            {
                ::Sleep(1);
                if ((int64_t)time(NULL) - startTime >= (int64_t)timeoutSec)
                {
                    Close();
                    return false;
                }
                continue;
            }
            if (nRet < 1)
            {
                Close();
                return false;
            }
            sent += (int64_t)nRet;
        }
        return true;
    }

    bool RecvFrame(std::string& body, int timeoutSec) override
    {
        //先收 24 字节帧头
        char hdr[FRAME_HEADER_SIZE] = { 0 };
        if (!RecvBytes(hdr, FRAME_HEADER_SIZE, timeoutSec))
            return false;

        //bodylen 为大端 uint32，位于帧头偏移 12
        uint32_t bodylen = (static_cast<uint32_t>(static_cast<unsigned char>(hdr[12])) << 24)
                         | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[13])) << 16)
                         | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[14])) << 8)
                         |  static_cast<uint32_t>(static_cast<unsigned char>(hdr[15]));
        if (bodylen > MAX_BODY_FILESERVER)
            return false;

        std::string buf;
        buf.resize(FRAME_HEADER_SIZE + bodylen);
        memcpy(&buf[0], hdr, FRAME_HEADER_SIZE);
        if (bodylen > 0)
        {
            if (!RecvBytes(&buf[FRAME_HEADER_SIZE], static_cast<int64_t>(bodylen), timeoutSec))
                return false;
        }

        frame_header header;
        std::string decoded;
        size_t consumed = 0;
        if (net::decodeFrame(buf.data(), buf.size(), header, decoded, consumed,
                             MAX_BODY_FILESERVER) != net::frame_decode_ok)
            return false;

        //还原包体：握手完成后服务端发来的包体是加密的，须在此解出明文再交给业务层。
        //本连接的加密器是自己独占的，与别的并发连接互不干扰。
        std::string err;
        if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                     decoded, &m_handshake.cipher(), body, err))
        {
            LOG_ERROR("FileServerConn: restore body failed: %s, cmd=%u, bodylen=%u.",
                      err.c_str(), header.cmd, (uint32_t)decoded.size());
            //解密失败意味着这个方向的流已不可恢复，本连接只能作废
            Close();
            return false;
        }

        return true;
    }

    //本连接自己的帧体加解密器（IFileFrameChannel）。每个实例各一个，
    //因为群文件并发池同时能跑三条独立连接，共用会让流状态互相踩踏。
    net::FrameCipher* GetCipher() override
    {
        return &m_handshake.cipher();
    }

private:
    /**
     *  握手期间的字节收发适配器，把 net::IHandshakeChannel 转接到本连接的
     *  SendBytes / RecvBytes 上。只在 Connect() 里活着，是栈上对象，不涉及并发。
     */
    class HandshakeChannel : public net::IHandshakeChannel
    {
    public:
        //  owner：宿主连接，生命周期覆盖本对象；timeoutSec：每次收发的超时，单位秒
        HandshakeChannel(FileServerConn* owner, int timeoutSec)
            : m_owner(owner), m_timeoutSec(timeoutSec)
        {
        }

        virtual bool SendAll(const char* data, size_t len)
        {
            return m_owner->SendBytes(data, (int64_t)len, m_timeoutSec);
        }

        virtual bool RecvAll(char* buf, size_t len)
        {
            return m_owner->RecvBytes(buf, (int64_t)len, m_timeoutSec);
        }

    private:
        FileServerConn* m_owner;       //宿主连接，不拥有
        int             m_timeoutSec;  //每次收发的超时，单位秒
    };

    /**
     *  在刚建好的连接上同步走完三帧协议加密握手。由 Connect 的两条成功路径调用。
     *
     *  <u>握手必须在这里做完</u>：本连接建好之后马上就会发第一个业务帧，而服务端在
     *  握手完成前只收握手帧，晚一步就会被对端断开。
     *
     *  未配置服务端公钥时直接返回成功且不做任何收发，此时本连接走明文。
     *
     *  @param timeoutSec 每次收发的超时时间，单位秒
     *  @return 成功返回 true；失败时已关闭连接，调用方应把它当作连接失败
     */
    bool PerformHandshake(int timeoutSec)
    {
        //连到三个服务的各条链路共用同一把服务端公钥，因为三个服务共用同一份身份密钥文件
        const unsigned char* pServerPubKey = CIUSocket::GetInstance().GetServerPublicKey();
        if (pServerPubKey == NULL)
            return true;

        HandshakeChannel channel(this, timeoutSec);
        std::string err;
        if (!net::performClientHandshake(channel, pServerPubKey, m_handshake, err))
        {
            LOG_ERROR("FileServerConn: handshake failed: %s.", err.c_str());
            Close();
            return false;
        }

        return true;
    }

    //在本连接上收满 nSize 字节（语义同 CIUSocket::RecvOnFilePort）。成功返回 true。
    bool RecvBytes(char* buf, int64_t nSize, int timeoutSec)
    {
        if (m_sock == INVALID_SOCKET)
            return false;

        int64_t startTime = (int64_t)time(NULL);
        int64_t got = 0;
        while (got < nSize)
        {
            int nRet = ::recv(m_sock, buf + got, (int)(nSize - got), 0);
            if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
            {
                ::Sleep(1);
                if ((int64_t)time(NULL) - startTime >= (int64_t)timeoutSec)
                {
                    Close();
                    return false;
                }
                continue;
            }
            if (nRet < 1)
            {
                Close();
                return false;
            }
            got += (int64_t)nRet;
        }
        return true;
    }

private:
    SOCKET m_sock;   //本连接独占的阻塞收发 socket（非阻塞 + 轮询超时）

    //本连接的协议加密状态。<u>必须每个实例各持一个</u>：群文件并发池同时最多跑三条
    //FileServerConn，升级包下载还会再占一条，共用一个加密器会让几条连接的流状态
    //互相踩踏，表现为随机的解密失败。
    //Close() 刻意不碰它——Close 会被 FileTransfer::Stop 从别的线程调来解阻塞，
    //在那里动加密器会与正在收发的池线程抢；本对象是用完即弃的，随析构一起走即可。
    net::ClientHandshake m_handshake;
};

} // namespace net