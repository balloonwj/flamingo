/**
 *  flamingoAdmin —— TCP 连通性探测实现。
 *
 *  winsock2.h / ws2tcpip.h 必须先于 windows.h（经 stdafx.h 间接引入）出现，
 *  否则 windows.h 会拉入旧版 winsock.h 引发冲突。本工程未开启预编译头，
 *  故可在此把 winsock 头放到 stdafx.h 之前。
 *
 *  本文件在 stdafx.h 之前就引入了 Windows SDK 的头文件，因此不能依赖
 *  stdafx.h 去设定 WINVER / _WIN32_WINNT / _WIN32_IE —— 那样 SDK 会先按
 *  默认值展开、再被 stdafx.h 改写，既报 warning C4005，取值也前后不一。
 *  这三个宏统一由工程文件的预处理器定义给出（见 flamingoAdmin.vcxproj），
 *  在任何头文件之前就已生效，GetAddrInfoW 等 API 所需的 Windows 7 版本
 *  条件由此满足，本文件无须再自行定义。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include <winsock2.h>
#include <ws2tcpip.h>

#include "stdafx.h"
#include "Util/NetProbe.h"

#include <mutex>

#pragma comment(lib, "ws2_32.lib")

namespace flamingoadmin {

namespace {

// 端口数字字符串缓冲长度（端口最多 5 位，足够）。
const int kPortStrLen = 16;

// Winsock 惰性初始化的 once 标志。
std::once_flag g_wsaOnce;

// 首次使用时初始化 Winsock。进程退出时由系统回收，不显式 WSACleanup。
void StartupWsa()
{
    WSADATA data = {};
    ::WSAStartup(MAKEWORD(2, 2), &data);
}

} // anonymous namespace

ProbeResult ProbeTcp(const CString& host, int port, int timeoutMs)
{
    std::call_once(g_wsaOnce, &StartupWsa);

    wchar_t portStr[kPortStrLen] = {};
    ::swprintf_s(portStr, kPortStrLen, L"%d", port);

    // 解析主机名 / IP。
    ADDRINFOW hints = {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOW* res = nullptr;
    if (::GetAddrInfoW(host, portStr, &hints, &res) != 0 || res == nullptr)
    {
        return ProbeResult::DnsFail;
    }

    ProbeResult result = ProbeResult::Timeout;
    SOCKET sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock != INVALID_SOCKET)
    {
        // 置非阻塞：connect 立即返回 WSAEWOULDBLOCK，再用 select 等结果，
        // 这样才能控制超时。
        u_long nonBlocking = 1;
        ::ioctlsocket(sock, FIONBIO, &nonBlocking);

        const int rc = ::connect(sock, res->ai_addr,
                                 static_cast<int>(res->ai_addrlen));
        if (rc == 0)
        {
            result = ProbeResult::Ok;          // 极少见：立即握手成功
        }
        else if (::WSAGetLastError() == WSAEWOULDBLOCK)
        {
            fd_set writeSet;
            fd_set exceptSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptSet);
            FD_SET(sock, &writeSet);
            FD_SET(sock, &exceptSet);

            timeval tv = {};
            tv.tv_sec  = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;

            const int sel = ::select(0, nullptr, &writeSet, &exceptSet, &tv);
            if (sel > 0 && FD_ISSET(sock, &writeSet))
            {
                // 套接字可写，再查 SO_ERROR 确认 connect 真成功。
                int soError = 0;
                int len = sizeof(soError);
                ::getsockopt(sock, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&soError), &len);
                result = (soError == 0) ? ProbeResult::Ok
                                        : ProbeResult::Refused;
            }
            else if (sel > 0)
            {
                result = ProbeResult::Refused;  // 落在异常集，连接被拒
            }
            else
            {
                // sel == 0 超时；sel < 0 select 出错，一并按超时计。
                result = ProbeResult::Timeout;
            }
        }
        else
        {
            result = ProbeResult::Refused;
        }
        ::closesocket(sock);
    }
    else
    {
        result = ProbeResult::Refused;          // 创建套接字失败
    }

    ::FreeAddrInfoW(res);
    return result;
}

} // namespace flamingoadmin
