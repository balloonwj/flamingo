/**
 *  imgserver 的 P2P 地址探测服务实现，见 P2PAddressProbe.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PAddressProbe.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <chrono>
#include <functional>
#include <utility>

#include "../base/AsyncLog.h"
#include "../base/ConfigFileReader.h"
#include "../net/Sockets.h"

//类内静态常量的类外定义：g++ 在 ODR 使用时需要
const int P2PAddressProbe::kDefaultPort1;
const int P2PAddressProbe::kDefaultPort2;
const int P2PAddressProbe::kDefaultPerIpPerSecond;

namespace {

//配置项名（含义见 P2PAddressProbe.h 文件头）
const char* const kKeyListenIp = "p2pprobelistenip";
const char* const kKeyPort1 = "p2pprobeport1";
const char* const kKeyPort2 = "p2pprobeport2";
const char* const kKeyPerIpPerSecond = "p2pprobeperipps";

//监听地址的默认值：全部网卡
const char* const kDefaultListenIp = "0.0.0.0";

//端口号的上限
const long long kMaxPort = 65535;

//限频表最多同时记录的来源 IP 数：每个表项只有十几字节，6.5 万个约 1 MB，足以容纳正常负载，
//又能在伪造大量来源 IP 时限制内存
const size_t kMaxRateLimitEntries = 65536;

//一次可读事件里最多处理的报文数：报文很多时分几轮处理，避免长时间占住事件循环
const int kMaxPacketsPerWakeup = 64;

//接收缓冲（字节）：可应答的请求最短 32 字节，更长的请求截断后长度字段对不上、按非法报文忽略即可，
//留到一个以太网报文的大小足够
const int kRecvBufBytes = 1500;

//「限频丢弃」日志的最小间隔（毫秒）：被大量请求冲击时不让日志刷屏
const int64_t kRateLimitLogIntervalMs = 1000;

//「IP:端口」文本的缓冲长度（字节）
const size_t kIpTextBytes = 64;

//把整段文本解析成十进制整数；为空、带非数字字符时返回 false
bool ParseInteger(const char* text, long long& value)
{
    if (text == NULL || *text == '\0')
        return false;
    errno = 0;
    char* end = NULL;
    value = strtoll(text, &end, 10);
    return errno == 0 && end != NULL && *end == '\0';
}

//取端口配置：配成 0 时返回 0（不开这个端口）；缺项、非数字、为负或超过 65535 时返回默认值
int ReadPort(CConfigFileReader& config, const char* key, int defaultValue)
{
    long long value = 0;
    if (!ParseInteger(config.getConfigName(key), value) || value < 0 || value > kMaxPort)
        return defaultValue;
    return static_cast<int>(value);
}

} // namespace

P2PAddressProbe::P2PAddressProbe()
    : m_loop(NULL)
    , m_answered(0)
    , m_rateLimited(0)
    , m_ignored(0)
    , m_lastRateLimitLogMs(0)
{
}

P2PAddressProbe::~P2PAddressProbe()
{
}

P2PAddressProbeConfig P2PAddressProbe::loadConfig(CConfigFileReader& config)
{
    P2PAddressProbeConfig cfg;
    const char* listenIp = config.getConfigName(kKeyListenIp);
    cfg.m_listenIp = (listenIp != NULL && *listenIp != '\0') ? listenIp : kDefaultListenIp;
    cfg.m_port1 = ReadPort(config, kKeyPort1, kDefaultPort1);
    cfg.m_port2 = ReadPort(config, kKeyPort2, kDefaultPort2);
    long long perIp = 0;
    if (!ParseInteger(config.getConfigName(kKeyPerIpPerSecond), perIp) || perIp <= 0 || perIp > INT_MAX)
        perIp = kDefaultPerIpPerSecond;
    cfg.m_perIpPerSecond = static_cast<int>(perIp);
    return cfg;
}

int P2PAddressProbe::init(net::EventLoop* loop, const P2PAddressProbeConfig& config)
{
    uninit();
    m_loop = loop;
    m_limiter.reset(new P2PProbeRateLimiter(config.m_perIpPerSecond, kMaxRateLimitEntries));

    const int ports[] = { config.m_port1, config.m_port2 };
    const size_t portCount = sizeof(ports) / sizeof(ports[0]);
    for (size_t i = 0; i < portCount; ++i)
    {
        //配成 0：不开这个端口
        if (ports[i] <= 0)
            continue;
        SOCKET fd = net::sockets::createUdpNonblocking();
        if (fd == INVALID_SOCKET)
            continue;
        struct sockaddr_in addr;
        net::sockets::fromIpPort(config.m_listenIp.c_str(), static_cast<uint16_t>(ports[i]), &addr);
        if (!net::sockets::bindUdp(fd, addr))
        {
            LOGE("p2p probe: bind %s:%d failed, this port is disabled", config.m_listenIp.c_str(), ports[i]);
            net::sockets::close(fd);
            continue;
        }

        std::unique_ptr<ProbePort> port(new ProbePort());
        port->m_fd = fd;
        port->m_port = ports[i];
        port->m_channel.reset(new net::Channel(loop, fd));
        //读回调带的接收时刻用不上，std::bind 产生的调用对象会忽略多出的参数
        port->m_channel->setReadCallback(std::bind(&P2PAddressProbe::onReadable, this, m_ports.size()));
        port->m_channel->enableReading();
        m_ports.push_back(std::move(port));
        LOGI("p2p probe listening on %s:%d (udp)", config.m_listenIp.c_str(), ports[i]);
    }
    LOGI("p2p probe started, ports opened: %u, per-ip limit: %d/s", static_cast<unsigned>(m_ports.size()),
         config.m_perIpPerSecond);
    return static_cast<int>(m_ports.size());
}

void P2PAddressProbe::uninit()
{
    if (m_loop == NULL)
        return;
    for (size_t i = 0; i < m_ports.size(); ++i)
    {
        m_ports[i]->m_channel->disableAll();
        m_ports[i]->m_channel->remove();
        m_ports[i]->m_channel.reset();
        net::sockets::close(m_ports[i]->m_fd);
    }
    m_ports.clear();
    LOGI("p2p probe stopped, answered: %llu, rate limited: %llu, ignored: %llu", (unsigned long long)m_answered,
         (unsigned long long)m_rateLimited, (unsigned long long)m_ignored);
    m_loop = NULL;
}

void P2PAddressProbe::onReadable(size_t index)
{
    if (index >= m_ports.size())
        return;
    const SOCKET fd = m_ports[index]->m_fd;
    char buf[kRecvBufBytes];
    for (int i = 0; i < kMaxPacketsPerWakeup; ++i)
    {
        struct sockaddr_in from;
        const int32_t n = net::sockets::recvFrom(fd, buf, kRecvBufBytes, &from);
        //没有更多报文（或出错）：等下一次可读事件
        if (n < 0)
            break;

        StunTransactionId txid;
        if (!P2PStunCodec::parseBindingRequest(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n), txid))
        {
            ++m_ignored;
            continue;
        }

        const uint32_t ip = ntohl(from.sin_addr.s_addr);
        const uint16_t port = ntohs(from.sin_port);
        const int64_t now = nowMs();
        if (!m_limiter->allow(ip, now))
        {
            ++m_rateLimited;
            if (now - m_lastRateLimitLogMs >= kRateLimitLogIntervalMs)
            {
                m_lastRateLimitLogMs = now;
                //用 toIpPort 而不用 toIp：后者的长度判断写反了，缓冲足够大时什么都不写（bugs.md BUG-79）
                char fromText[kIpTextBytes] = { 0 };
                net::sockets::toIpPort(fromText, sizeof(fromText), from);
                LOGW("p2p probe rate limited, from: %s, total rate limited: %llu", fromText,
                     (unsigned long long)m_rateLimited);
            }
            continue;
        }

        const std::string resp = P2PStunCodec::buildBindingResponse(txid, ip, port);
        if (net::sockets::sendTo(fd, resp.data(), static_cast<int32_t>(resp.size()), from) < 0)
        {
            char fromText[kIpTextBytes] = { 0 };
            net::sockets::toIpPort(fromText, sizeof(fromText), from);
            LOGW("p2p probe sendto %s failed", fromText);
            continue;
        }
        ++m_answered;
    }
}

int64_t P2PAddressProbe::nowMs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
