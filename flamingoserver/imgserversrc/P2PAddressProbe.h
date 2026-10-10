/**
 *  imgserver 的 P2P 地址探测服务，P2PAddressProbe.h
 *
 *  docs/P2P通信方案.md 6.2 节、第 2 期 2e-3。在两个 UDP 端口上应答客户端的 Binding 请求，告诉客户端从服务端看来
 *  它的地址是什么；客户端比较两个端口的应答，判断自己的 NAT 映射是否与目的地址无关。报文的判断与构造、按来源 IP
 *  限频都在 P2PProbeCore，这里只负责端口、收发与日志。两个端口共用一个限频表（同一来源 IP 的请求合并计数）。
 *
 *  配置项（imgserver.conf，都可缺失）：
 *    p2pprobelistenip                 监听地址，默认 0.0.0.0
 *    p2pprobeport1 / p2pprobeport2    两个 UDP 端口，默认 20004 / 20005，须与 chatserver.conf 的同名项一致；配 0 表示不开这个端口
 *    p2pprobeperipps                  每个来源 IP 每秒最多应答的请求数，默认 200（应答不大于请求，限频只为控制负载）
 *  端口绑定失败（被占用等）只记日志，不影响 imgserver 的其它服务。
 *
 *  线程：全部在 init 传入的那个 EventLoop 的线程上运行（imgserver 的主循环），不加锁。
 *
 *  典型用法（imgserversrc/main.cpp）：
 *      Singleton<P2PAddressProbe>::Instance().init(&g_mainLoop, P2PAddressProbe::loadConfig(config));
 *      ...
 *      Singleton<P2PAddressProbe>::Instance().uninit();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "../base/Platform.h"
#include "../net/Channel.h"
#include "../net/EventLoop.h"
#include "P2PProbeCore.h"

class CConfigFileReader;

//地址探测服务的配置
struct P2PAddressProbeConfig
{
    std::string m_listenIp;        //监听地址
    int         m_port1;           //第一个 UDP 端口；0 表示不开
    int         m_port2;           //第二个 UDP 端口；0 表示不开
    int         m_perIpPerSecond;  //每个来源 IP 每秒最多应答的请求数

    P2PAddressProbeConfig()
        : m_port1(0)
        , m_port2(0)
        , m_perIpPerSecond(0)
    {
    }
};

/**
 *  P2P 地址探测服务。单例（Singleton<P2PAddressProbe>），由 imgserver 的 main.cpp 初始化与收尾。
 */
class P2PAddressProbe
{
public:
    //各配置项的默认值：两个端口须与 chatserver 的 P2PServerSetup 一致
    static const int kDefaultPort1 = 20004;
    static const int kDefaultPort2 = 20005;
    static const int kDefaultPerIpPerSecond = 200;

    P2PAddressProbe();
    ~P2PAddressProbe();

    P2PAddressProbe(const P2PAddressProbe&) = delete;
    P2PAddressProbe& operator=(const P2PAddressProbe&) = delete;

    /**
     *  从配置文件读出探测服务的配置。缺项或非法值（非数字、为负、端口超过 65535、限频值小于等于 0）取默认值。
     *  @param config 已加载的配置文件（借用）
     *  @return 配置
     */
    static P2PAddressProbeConfig loadConfig(CConfigFileReader& config);

    /**
     *  打开配置里的端口并挂到 loop 上开始应答。须在 loop 所在线程调用；重复调用时先收尾上一次。
     *  @param loop   事件循环（借用，须比本对象的使用期长）
     *  @param config 配置
     *  @return 成功打开的端口数（0～2）；失败的端口只记日志
     */
    int init(net::EventLoop* loop, const P2PAddressProbeConfig& config);

    //关闭全部端口，记一条统计日志。须在 init 时的 loop 线程调用；未初始化时什么都不做
    void uninit();

private:
    //一个探测端口
    struct ProbePort
    {
        SOCKET                         m_fd;        //UDP 套接字
        int                            m_port;      //端口号
        std::unique_ptr<net::Channel>  m_channel;   //挂在 loop 上的读事件
    };

    //端口可读：收取并应答本次能收到的报文（每次最多 kMaxPacketsPerWakeup 个，避免占住事件循环）
    void onReadable(size_t index);

    //当前时间（毫秒，单调时钟），限频用
    static int64_t nowMs();

private:
    net::EventLoop*                           m_loop;                //事件循环（借用）；未初始化时为 NULL
    std::vector<std::unique_ptr<ProbePort> >  m_ports;               //已打开的端口
    std::unique_ptr<P2PProbeRateLimiter>      m_limiter;             //按来源 IP 的限频表，两个端口共用
    uint64_t                                  m_answered;            //已应答的请求数
    uint64_t                                  m_rateLimited;         //因限频丢弃的请求数
    uint64_t                                  m_ignored;             //不是可应答的 Binding 请求而忽略的报文数
    int64_t                                   m_lastRateLimitLogMs;  //上一次记「限频丢弃」日志的时刻（毫秒），每秒至多记一条
};
