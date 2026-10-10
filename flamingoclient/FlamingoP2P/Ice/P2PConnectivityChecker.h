/**
 *  P2P 连通性检查（打洞）的状态机（docs/P2P通信方案.md 6.3 节，第 3 期 3c）。
 *
 *  一条链路只有一个 UDP socket，检查表就是对方的候选地址（经 2303 交换），每个地址一项：
 *  - 优先级：与本方某块网卡同网段的对方本机地址 > 对方其它本机地址 > 对方的公网映射地址 > 对端反射地址；
 *  - 每项每隔 kP2PCheckProbeIntervalMs 发一个探测（p2p_packet_probe，明文为 8 字节事务号），在建链超时之前持续重发
 *    —— 第 0a 步实测，Windows 防火墙会静默丢弃对方先发来的包，要等本方也向那个地址发过包之后，对方重发的包才进得来；
 *  - 收到探测：回探测应答（原样带回事务号，并写上看到的来源地址），并立即向那个地址补发一次本方的探测；来源地址不在
 *    检查表里时，作为对端反射地址加入（对方在对称型 NAT 后面时，这往往是唯一能通的地址）；
 *  - 收到探测应答且事务号是本方发往那个地址的：这一项双向连通；
 *  - 发起方：第一项连通后再等 kP2PNominateGraceMs，让优先级更高的项也有机会连通，然后选定已连通项里优先级最高的一项，
 *    在该地址上每隔 kP2PCheckProbeIntervalMs 重发「选定」，收到「选定确认」即链路就绪；
 *  - 应答方：收到「选定」即回「选定确认」并锁定该地址、链路就绪；之后再收到同一地址的「选定」照样回确认（发起方可能
 *    没收到前一个确认），其它地址的「选定」忽略（路径选定后不切换，2 节第 2 条）；
 *  - 超时：发起方在 connectTimeoutMs 内没有就绪即失败（改走中转，6.4 节）；应答方多等 kP2PControlledExtraMs 才判失败，
 *    避免发起方的「选定」还在路上时先放弃。
 *  所有报文都经 P2PLinkCipher 加密认证，认证失败、会话标签不对的报文在交给本类之前就已丢弃。
 *
 *  不碰 socket 与真实时钟：发送经 IP2PDatagramSender，时间由调用方传入，单测在模拟网络里确定性地驱动（[p2pice]）。
 *  在 IO 线程上使用（第 3d 步）。
 *
 *  典型用法：
 *      P2PConnectivityChecker checker(sender, cipher, controlling, connectTimeoutMs);
 *      checker.Start(localHostCandidates, remoteCandidates, now);
 *      ...收到对方的链路报文并解密后：checker.OnPacket(fromIp, fromPort, type, plain, now);
 *      ...定时：if (now >= checker.NextTimerMs()) checker.OnTimer(now);
 *      if (checker.State() == p2p_check_connected) 选定的地址 = checker.SelectedIp()/SelectedPort();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../Core/P2PCrypto.h"
#include "P2PAddressProber.h"
#include "P2PCandidate.h"

namespace p2p {

//连通性检查的状态
enum p2p_check_state
{
    p2p_check_idle       = 0,   //尚未 Start
    p2p_check_running    = 1,   //正在探测（发起方可能已在等待选定确认）
    p2p_check_connected  = 2,   //链路就绪：双方确认了同一个地址
    p2p_check_failed     = 3,   //超时没有就绪：改走中转
};

//检查表里一项的来历
enum p2p_check_origin
{
    p2p_check_origin_same_subnet = 1,   //对方的本机地址，与本方某块网卡同网段
    p2p_check_origin_host        = 2,   //对方的其它本机地址
    p2p_check_origin_srflx       = 3,   //对方的公网映射地址
    p2p_check_origin_prflx       = 4,   //对端反射地址：没在对方的候选里，但收到了来自它的探测
};

//每项探测的重发间隔（毫秒）：方案 6.3 节暂定 50～100 毫秒
const int64_t kP2PCheckProbeIntervalMs = 50;

//发起方在第一项连通后等待更好的项的时间（毫秒）
const int64_t kP2PNominateGraceMs = 100;

//应答方比发起方多等的时间（毫秒）
const int64_t kP2PControlledExtraMs = 500;

//探测的事务号长度（字节）
const size_t kP2PCheckTxidBytes = 8;

/**
 *  一条链路的连通性检查。
 */
class P2PConnectivityChecker
{
public:
    /**
     *  @param sender           发送出口（借用）：本链路的 UDP socket
     *  @param cipher           本链路的加解密（借用，须已 Init）：封装探测、选定等报文
     *  @param controlling      本方是否为发起方
     *  @param connectTimeoutMs 建链超时（毫秒，2302 的 connecttimeoutms）
     */
    P2PConnectivityChecker(IP2PDatagramSender& sender, P2PLinkCipher& cipher, bool controlling, int64_t connectTimeoutMs);

    /**
     *  开始检查：按优先级建好检查表，向每一项发出第一个探测。
     *  @param localHosts 本方的本机候选地址（带前缀长度，用于判断同网段）
     *  @param remotes    对方的候选地址（2303 里收到的）
     *  @param nowMs      当前时间（毫秒）
     */
    void Start(const std::vector<P2PCandidate>& localHosts, const std::vector<P2PCandidate>& remotes, int64_t nowMs);

    /**
     *  收到对方的一个链路报文（已解密认证）。
     *  @param fromIp   来源地址（主机字节序）
     *  @param fromPort 来源端口（主机字节序）
     *  @param type     报文类型（p2p_packet_type）
     *  @param plain    明文
     *  @param nowMs    当前时间（毫秒）
     */
    void OnPacket(uint32_t fromIp, uint16_t fromPort, uint8_t type, const std::string& plain, int64_t nowMs);

    /**
     *  定时驱动：到时刻的项重发探测，发起方到时刻选定或重发选定，超时判失败。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnTimer(int64_t nowMs);

    //下一次需要调用 OnTimer 的时刻（毫秒）；已就绪或已失败时为 INT64_MAX
    int64_t NextTimerMs() const;

    //当前状态，取值见 p2p_check_state
    int State() const { return m_state; }

    //选定的对方地址（主机字节序），链路就绪后有效
    uint32_t SelectedIp() const { return m_selectedIp; }
    uint16_t SelectedPort() const { return m_selectedPort; }

    //选定项的来历（p2p_check_origin），链路就绪后有效；2304 据此上报路径是局域网直连还是打洞
    int SelectedOrigin() const { return m_selectedOrigin; }

    //检查表的项数（含对端反射地址）
    size_t CheckCount() const { return m_checks.size(); }

    //已发出的探测个数（统计与单测用）
    uint64_t ProbesSent() const { return m_probesSent; }

private:
    //检查表的一项
    struct Check
    {
        uint32_t m_ip;                          //对方地址（主机字节序）
        uint16_t m_port;                        //对方端口（主机字节序）
        int      m_origin;                      //来历，取值见 p2p_check_origin
        uint32_t m_remotePriority;              //对方给出的候选优先级（对端反射地址为 0）
        uint8_t  m_txid[kP2PCheckTxidBytes];    //本方发往这一项的探测所用的事务号
        int64_t  m_nextProbeMs;                 //下一次发探测的时刻（毫秒）
        bool     m_succeeded;                   //是否已双向连通（收到了对本方探测的应答）
    };

    //检查表的排序：来历越靠前越优先，同一来历按对方给出的优先级从高到低
    static bool CheckBefore(const Check& a, const Check& b);

    //按地址找检查表的项；没有返回 NULL
    Check* FindCheck(uint32_t ip, uint16_t port);

    //向一项发一个探测
    void SendProbe(Check& check, int64_t nowMs);

    //发一个只带少量明文的控制报文（选定、选定确认、探测应答）
    void SendControl(uint8_t type, const std::string& plain, uint32_t ip, uint16_t port);

    //发起方：从已连通的项里选定最好的一项并发出第一个选定
    void Nominate(int64_t nowMs);

    //链路就绪
    void MarkConnected(uint32_t ip, uint16_t port, int origin);

private:
    IP2PDatagramSender& m_sender;              //发送出口（借用）
    P2PLinkCipher&      m_cipher;              //加解密（借用）
    bool                m_controlling;         //本方是否为发起方
    int64_t             m_connectTimeoutMs;    //建链超时（毫秒）
    int                 m_state;               //当前状态，取值见 p2p_check_state
    std::vector<P2PCandidate> m_localHosts;    //本方的本机候选地址（判断同网段用）
    std::vector<Check>  m_checks;              //检查表，按优先级排列（对端反射地址追加在后面）
    int64_t             m_startMs;             //Start 的时刻（毫秒）
    int64_t             m_deadlineMs;          //判定失败的时刻（毫秒）
    int64_t             m_firstSuccessMs;      //第一项连通的时刻（毫秒）；-1 表示还没有
    bool                m_nominating;          //发起方：是否已选定、正在等选定确认
    uint32_t            m_nominatedIp;         //发起方：选定的地址
    uint16_t            m_nominatedPort;       //发起方：选定的端口
    int                 m_nominatedOrigin;     //发起方：选定项的来历
    int64_t             m_nextNominateMs;      //发起方：下一次重发选定的时刻（毫秒）
    uint32_t            m_selectedIp;          //链路就绪后选定的对方地址
    uint16_t            m_selectedPort;        //链路就绪后选定的对方端口
    int                 m_selectedOrigin;      //链路就绪后选定项的来历
    uint64_t            m_probesSent;          //已发出的探测个数
};

} // namespace p2p
