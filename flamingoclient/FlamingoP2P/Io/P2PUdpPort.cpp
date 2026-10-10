/**
 *  P2P 会话 UDP socket 的实现，见 P2PUdpPort.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include "P2PUdpPort.h"

#include <string.h>


#pragma comment(lib, "ws2_32.lib")

namespace p2p {

namespace {

//SIO_UDP_CONNRESET 定义在 mstcpip.h；为了不引入整个头文件，按它的定义（_WSAIOW(IOC_VENDOR, 12)）给出
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

//诊断用：环境变量 FLAMINGO_P2P_DISABLE_USO 为 1 时不用发送分段卸载（对比成批发送的效果，见方案第 14 节第 4f 步）
const char* const kDisableUsoEnvName = "FLAMINGO_P2P_DISABLE_USO";
const char* const kEnvOn = "1";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
const DWORD kEnvValueChars = 8;

//socket 的收发缓冲（字节）：系统默认只有几十 KB，可靠通道在局域网上满速发送时接收端的缓冲很快溢出，大量丢包后 KCP
//的超时重传不断退避，传输几乎停住。取第 0b 步原型的 8 MiB（千兆局域网上约 60 毫秒的数据）
const int kSocketBufferBytes = 8 * 1024 * 1024;

//是否按环境变量关闭发送分段卸载
bool UsoDisabledByEnv()
{
    char value[kEnvValueChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(kDisableUsoEnvName, value, kEnvValueChars);
    return n > 0 && n < kEnvValueChars && strcmp(value, kEnvOn) == 0;
}

//UDP_SEND_MSG_SIZE 定义在 ws2ipdef.h，只在目标系统为 Win10 及以上时可见；本 DLL 的目标下限是 Win7，按它的定义给出，
//运行时设置失败（Win10 之前的系统）就不用
#ifndef UDP_SEND_MSG_SIZE
#define UDP_SEND_MSG_SIZE 2
#endif

} // namespace

P2PUdpPort::P2PUdpPort()
    : m_socket((uintptr_t)INVALID_SOCKET)
    , m_port(0)
    , m_usoSegment(0)
{
}

P2PUdpPort::~P2PUdpPort()
{
    Close();
}

bool P2PUdpPort::Open()
{
    Close();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return false;
    u_long nonBlocking = 1;
    ::ioctlsocket(s, FIONBIO, &nonBlocking);
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&kSocketBufferBytes), sizeof(kSocketBufferBytes));
    ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&kSocketBufferBytes), sizeof(kSocketBufferBytes));
    BOOL reportConnReset = FALSE;
    DWORD bytesReturned = 0;
    ::WSAIoctl(s, SIO_UDP_CONNRESET, &reportConnReset, sizeof(reportConnReset), NULL, 0, &bytesReturned, NULL, NULL);

    //绑定随机端口：固定端口会让 Windows 弹出防火墙提示框
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    if (::bind(s, (const sockaddr*)&addr, sizeof(addr)) != 0)
    {
        ::closesocket(s);
        return false;
    }
    sockaddr_in bound;
    int boundLen = sizeof(bound);
    if (::getsockname(s, (sockaddr*)&bound, &boundLen) != 0)
    {
        ::closesocket(s);
        return false;
    }
    m_socket = (uintptr_t)s;
    m_port = ntohs(bound.sin_port);
    //发送分段卸载：设置成功才使用
    DWORD segment = (DWORD)kP2PMaxDatagramBytes;
    m_usoSegment = (!UsoDisabledByEnv() && ::setsockopt(s, IPPROTO_UDP, UDP_SEND_MSG_SIZE,
                                                         reinterpret_cast<const char*>(&segment), sizeof(segment)) == 0)
                 ? kP2PMaxDatagramBytes : 0;
    return true;
}

void P2PUdpPort::Close()
{
    if ((SOCKET)m_socket != INVALID_SOCKET)
    {
        ::closesocket((SOCKET)m_socket);
        m_socket = (uintptr_t)INVALID_SOCKET;
    }
    m_port = 0;
    m_usoSegment = 0;
}

bool P2PUdpPort::IsOpen() const
{
    return (SOCKET)m_socket != INVALID_SOCKET;
}

int P2PUdpPort::Receive(uint8_t* buf, int cap, uint32_t& fromIp, uint16_t& fromPort)
{
    if (!IsOpen())
        return -1;
    sockaddr_in from;
    int fromLen = sizeof(from);
    const int n = ::recvfrom((SOCKET)m_socket, (char*)buf, cap, 0, (sockaddr*)&from, &fromLen);
    if (n < 0)
        return -1;
    fromIp = ntohl(from.sin_addr.s_addr);
    fromPort = ntohs(from.sin_port);
    return n;
}

bool P2PUdpPort::SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len)
{
    if (!IsOpen())
        return false;
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(ip);
    to.sin_port = htons(port);
    return ::sendto((SOCKET)m_socket, (const char*)data, (int)len, 0, (const sockaddr*)&to, sizeof(to)) == (int)len;
}

size_t P2PUdpPort::BatchSegmentBytes() const
{
    return m_usoSegment;
}

bool P2PUdpPort::SendBatch(uint32_t ip, uint16_t port, const uint8_t* data, size_t len)
{
    if (!IsOpen())
        return false;
    if (m_usoSegment != 0 && len <= kP2PMaxBatchBytes)
        return SendTo(ip, port, data, len);
    //不支持发送分段卸载，或总长度超过一次能交出的上限：按满载报文的长度逐段发送
    bool ok = true;
    for (size_t offset = 0; offset < len; offset += kP2PMaxDatagramBytes)
    {
        const size_t n = (len - offset < kP2PMaxDatagramBytes) ? len - offset : kP2PMaxDatagramBytes;
        ok = SendTo(ip, port, data + offset, n) && ok;
    }
    return ok;
}

} // namespace p2p
