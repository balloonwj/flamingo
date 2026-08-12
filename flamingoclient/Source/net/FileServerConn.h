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
#include "../IULog.h"            // LOG_INFO / LOG_ERROR

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

    //连到 fileserver（地址 / 端口取自 CIUSocket）。非阻塞 connect + select 等待，
    //语义与 CIUSocket::ConnectToFileServer 一致。成功返回 true。
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

        int ret = ::connect(m_sock, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
        if (ret == 0)
            return true;
        if (ret == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
        {
            LOG_ERROR("FileServerConn: connect fileserver %s:%d failed.", addr.c_str(), (int)port);
            Close();
            return false;
        }

        fd_set writeset;
        FD_ZERO(&writeset);
        FD_SET(m_sock, &writeset);
        struct timeval tv = { timeoutSec, 0 };
        if (::select((int)m_sock + 1, NULL, &writeset, NULL, &tv) != 1)
        {
            LOG_ERROR("FileServerConn: connect fileserver %s:%d timeout.", addr.c_str(), (int)port);
            Close();
            return false;
        }
        return true;
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

        body.swap(decoded);
        return true;
    }

private:
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
};

} // namespace net