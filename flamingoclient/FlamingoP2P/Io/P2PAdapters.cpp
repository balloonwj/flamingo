/**
 *  本机网卡枚举与地址解析的实现，见 P2PAdapters.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "P2PAdapters.h"

#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace p2p {

namespace {

//GetAdaptersAddresses 的初始缓冲（字节）：微软文档建议 15 KB，缓冲不够时按返回的大小重试
const ULONG kInitialAdapterBufferBytes = 15 * 1024;

//缓冲不够时最多重试的次数（网卡在两次调用之间增加时需要再试）
const int kMaxAdapterRetries = 3;

//宽字符串转 UTF-8
std::string ToUtf8(const wchar_t* text)
{
    if (text == NULL || text[0] == L'\0')
        return std::string();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1)
        return std::string();
    std::string out((size_t)bytes - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], bytes, NULL, NULL);
    return out;
}

} // namespace

std::vector<P2PAdapterInfo> EnumerateAdapters()
{
    std::vector<P2PAdapterInfo> out;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = kInitialAdapterBufferBytes;
    std::vector<unsigned char> buf;
    ULONG ret = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < kMaxAdapterRetries && ret == ERROR_BUFFER_OVERFLOW; ++i)
    {
        buf.resize(size);
        ret = ::GetAdaptersAddresses(AF_INET, flags, NULL, (PIP_ADAPTER_ADDRESSES)&buf[0], &size);
    }
    if (ret != NO_ERROR)
        return out;

    for (PIP_ADAPTER_ADDRESSES a = (PIP_ADAPTER_ADDRESSES)&buf[0]; a != NULL; a = a->Next)
    {
        P2PAdapterInfo info;
        info.m_name = ToUtf8(a->FriendlyName);
        info.m_description = ToUtf8(a->Description);
        info.m_up = (a->OperStatus == IfOperStatusUp);
        info.m_loopback = (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u != NULL; u = u->Next)
        {
            if (u->Address.lpSockaddr == NULL || u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const sockaddr_in* sin = (const sockaddr_in*)u->Address.lpSockaddr;
            P2PAdapterAddress addr;
            addr.m_ip = ntohl(sin->sin_addr.s_addr);
            addr.m_prefixLen = u->OnLinkPrefixLength;
            info.m_addresses.push_back(addr);
        }
        out.push_back(info);
    }
    return out;
}

bool ResolveIpv4(const std::string& host, uint32_t& ip)
{
    if (host.empty())
        return false;
    //先按 IP 文本解析，不必查 DNS
    if (IpFromString(host, ip))
        return true;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = NULL;
    if (::getaddrinfo(host.c_str(), NULL, &hints, &result) != 0 || result == NULL)
        return false;
    bool found = false;
    for (addrinfo* p = result; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET && p->ai_addr != NULL)
        {
            ip = ntohl(((const sockaddr_in*)p->ai_addr)->sin_addr.s_addr);
            found = true;
            break;
        }
    }
    ::freeaddrinfo(result);
    return found;
}

} // namespace p2p
