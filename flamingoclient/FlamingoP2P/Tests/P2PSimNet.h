/**
 *  P2P 单测用的模拟网络（docs/P2P通信方案.md 第 11 节「模拟网络下的连通性检查」，第 3 期 3c）。
 *
 *  确定性地模拟：
 *    - NAT：每个 NAT 有一个公网地址，后面的主机用私网地址。出站时分配映射（锥形 NAT 按内部端点、对称型按「内部端点 +
 *      目的地址」，外部端口从 kSimFirstExternalPort 起依次分配）；入站时按类型过滤：完全锥形任何来源都放行，受限锥形
 *      只放行发过包的来源 IP，端口受限锥形与对称型只放行发过包的来源 IP 加端口；
 *    - 局域网：同一个 NAT 后面的主机之间按私网地址直接互通，不经过 NAT；
 *    - 公网主机：不在 NAT 后面，按自己的地址收发；
 *    - 地址探测服务器：收到 Binding 请求即按看到的来源地址回 Binding 成功应答；
 *    - 延迟、抖动（造成乱序）与丢包：每个报文到达时刻为发送时刻 + 基础延迟 + [0, 抖动] 内的伪随机值，并按概率丢弃。
 *  伪随机数用固定种子的线性同余发生器；事件按（到达时刻，序号）排序，同样的输入每次得到同样的结果。
 *
 *  只给单元测试用（编进 FlamingoUnit），不进 DLL。
 *
 *  典型用法（见 P2PSimTests.cpp）：
 *      SimNetwork net(seed);
 *      int natA = net.AddNat(sim_nat_port_restricted, publicIpA);
 *      int sockA = net.AddSocket(natA, privateIpA, portA, &peerA);
 *      net.AddStunServer(stunIp, stunPort);
 *      net.Send(sockA, dstIp, dstPort, data);
 *      net.RunUntil(t);      //把到达时刻不晚于 t 的报文按顺序交给接收方
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace p2psim {

//NAT 的类型
enum SimNatType
{
    sim_nat_full_cone       = 1,   //完全锥形：映射按内部端点分配，任何来源发往映射端口都放行
    sim_nat_restricted      = 2,   //受限锥形：映射按内部端点分配，只放行内部端点发过包的来源 IP
    sim_nat_port_restricted = 3,   //端口受限锥形：映射按内部端点分配，只放行发过包的来源 IP 加端口
    sim_nat_symmetric       = 4,   //对称型：映射按「内部端点 + 目的地址」分配，只放行那个目的地址发来的包
};

//外部端口的起始值
const uint16_t kSimFirstExternalPort = 40000;

/**
 *  模拟网络里的一个接收方（一个 socket 的拥有者），由测试实现。
 */
class ISimReceiver
{
public:
    /**
     *  收到一个报文。
     *  @param srcIp   来源地址（经过 NAT 时为 NAT 改写后的地址；主机字节序）
     *  @param srcPort 来源端口（主机字节序）
     *  @param data    报文内容
     *  @param nowMs   到达时刻（毫秒）
     */
    virtual void OnSimReceive(uint32_t srcIp, uint16_t srcPort, const std::string& data, int64_t nowMs) = 0;

protected:
    ~ISimReceiver()
    {
    }
};

/**
 *  模拟网络。单线程使用。
 */
class SimNetwork
{
public:
    /**
     *  @param seed 伪随机数的种子：同样的种子与同样的操作序列得到同样的结果
     */
    explicit SimNetwork(uint32_t seed);

    /**
     *  加一个 NAT。
     *  @param type     NAT 类型
     *  @param publicIp NAT 的公网地址（主机字节序）
     *  @return NAT 编号
     */
    int AddNat(SimNatType type, uint32_t publicIp);

    /**
     *  加一个 socket。
     *  @param natId    所在的 NAT 编号；-1 表示公网主机
     *  @param ip       主机地址（在 NAT 后面时为私网地址；主机字节序）
     *  @param port     绑定的端口（主机字节序）
     *  @param receiver 收包回调（借用，须比本网络活得久）
     *  @return socket 编号
     */
    int AddSocket(int natId, uint32_t ip, uint16_t port, ISimReceiver* receiver);

    /**
     *  加一个地址探测服务器（公网）。
     *  @param ip   地址（主机字节序）
     *  @param port 端口（主机字节序）
     */
    void AddStunServer(uint32_t ip, uint16_t port);

    /**
     *  设置之后发出的报文的延迟、抖动与丢包率。
     *  @param baseMs   基础延迟（毫秒）
     *  @param jitterMs 抖动上限（毫秒）：到达时刻再加 [0, jitterMs] 内的伪随机值，造成乱序
     *  @param lossPermille 丢包率（千分比，0～1000）
     */
    void SetLinkProfile(int64_t baseMs, int64_t jitterMs, int lossPermille);

    /**
     *  从一个 socket 发一个报文。经过 NAT 时按其类型分配映射、记下放行条件。
     *  @param socketId 发送方 socket
     *  @param dstIp    目的地址（主机字节序）
     *  @param dstPort  目的端口（主机字节序）
     *  @param data     报文内容
     */
    void Send(int socketId, uint32_t dstIp, uint16_t dstPort, const std::string& data);

    /**
     *  把到达时刻不晚于 untilMs 的报文按（到达时刻，序号）的顺序交给接收方，当前时间推进到 untilMs。
     *  接收方在回调里发出的新报文同样会被排进事件队列。
     *  @param untilMs 目标时刻（毫秒）
     */
    void RunUntil(int64_t untilMs);

    //当前时间（毫秒）
    int64_t Now() const { return m_now; }

    //下一个报文的到达时刻（毫秒）；没有待送的报文时为 INT64_MAX
    int64_t NextEventMs() const;

    //一个 socket 在 NAT 外看起来的地址（公网主机即本身）：对锥形 NAT 为它最先建立的映射，没有映射时返回 false
    bool ExternalAddressOf(int socketId, uint32_t& ip, uint16_t& port) const;

    //统计：已发出、已丢弃（丢包或被 NAT 过滤）、已送达的报文个数
    uint64_t SentCount() const { return m_sent; }
    uint64_t DroppedCount() const { return m_dropped; }
    uint64_t DeliveredCount() const { return m_delivered; }

private:
    //一个 NAT
    struct Nat
    {
        SimNatType m_type;       //类型
        uint32_t   m_publicIp;   //公网地址
        uint16_t   m_nextPort;   //下一个可分配的外部端口
    };

    //一个 socket
    struct Socket
    {
        int           m_natId;      //所在 NAT；-1 为公网主机
        uint32_t      m_ip;         //主机地址
        uint16_t      m_port;       //端口
        ISimReceiver* m_receiver;   //收包回调
    };

    //NAT 上的一个映射：外部端口 → 内部 socket，以及放行条件
    struct Mapping
    {
        int                                   m_socketId;     //内部 socket
        uint32_t                              m_dstIp;        //对称型：这个映射对应的目的地址
        uint16_t                              m_dstPort;      //对称型：这个映射对应的目的端口
        std::set<uint32_t>                    m_allowedIps;   //受限锥形：放行的来源 IP
        std::set<std::pair<uint32_t, uint16_t> > m_allowedEndpoints;   //端口受限锥形与对称型：放行的来源 IP 加端口
    };

    //一个在途的报文
    struct Packet
    {
        uint32_t    m_srcIp;     //接收方看到的来源地址
        uint16_t    m_srcPort;   //接收方看到的来源端口
        uint32_t    m_dstIp;     //目的地址（公网地址或局域网私网地址）
        uint16_t    m_dstPort;   //目的端口
        int         m_lanNatId;  //局域网内投递时为所在 NAT 编号；经公网投递时为 -1
        std::string m_data;      //报文内容
    };

    //伪随机数：[0, bound) 内的整数
    uint32_t Random(uint32_t bound);

    //排一个在途报文（按丢包率决定是否丢弃）
    void Enqueue(const Packet& packet);

    //投递一个到达的报文
    void Deliver(const Packet& packet);

    //地址探测服务器回应答
    void AnswerStun(const Packet& request);

    //按 socket 的私网地址找同一 NAT 后面的 socket；没有返回 -1
    int FindLanSocket(int natId, uint32_t ip, uint16_t port) const;

    //按公网地址找公网主机的 socket；没有返回 -1
    int FindPublicSocket(uint32_t ip, uint16_t port) const;

    //按公网地址找 NAT；没有返回 -1
    int FindNatByPublicIp(uint32_t ip) const;

private:
    uint32_t                                        m_seed;          //伪随机数状态
    int64_t                                         m_now;           //当前时间（毫秒）
    uint64_t                                        m_seq;           //事件序号（到达时刻相同的报文按它排序）
    int64_t                                         m_baseMs;        //基础延迟（毫秒）
    int64_t                                         m_jitterMs;      //抖动上限（毫秒）
    int                                             m_lossPermille;  //丢包率（千分比）
    std::vector<Nat>                                m_nats;          //全部 NAT
    std::vector<Socket>                             m_sockets;       //全部 socket
    std::set<std::pair<uint32_t, uint16_t> >        m_stunServers;   //地址探测服务器
    std::map<std::pair<int, uint16_t>, Mapping>     m_mappings;      //（NAT 编号，外部端口）→ 映射
    std::map<std::pair<int64_t, uint64_t>, Packet>  m_queue;         //（到达时刻，序号）→ 在途报文
    uint64_t                                        m_sent;          //已发出的报文个数
    uint64_t                                        m_dropped;       //已丢弃的报文个数
    uint64_t                                        m_delivered;     //已送达的报文个数
};

} // namespace p2psim
