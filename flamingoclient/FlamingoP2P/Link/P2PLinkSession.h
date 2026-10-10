/**
 *  一个 P2P 会话的状态机（docs/P2P通信方案.md 6、7 节，第 3 期 3d）：从收到 2302 到链路就绪、断开的全过程。
 *
 *  过程：
 *    1. Begin：生成临时密钥对，向地址探测服务器取公网映射地址（P2PAddressProber，约 1 秒内完成；没有探测服务器时跳过）；
 *    2. 探测完成后发 2303：本方公钥 + 本机地址 + 公网映射地址；
 *    3. 收到对方的 2303（经 OnPeerSignal，可能早于第 2 步）：推导会话密钥；本方的 2303 也已发出时开始打洞
 *       （P2PConnectivityChecker）；
 *    4. 打洞成功：发 2304（成功，路径与地址对），报告就绪；
 *       打洞失败且 2302 带了中转参数（第 3e 步起）：请观察者连 imgserver 的中转（StartRelay），连上后发起方每隔
 *       kP2PRelayNominateIntervalMs 经中转重发「选定」，应答方收到后回「选定确认」，双方就绪，路径为中转；中转连不上、
 *       中转连接断开或 kP2PRelayBudgetMs 内没有完成选定，按建链失败处理；
 *       建链失败：发 2304（失败）与 2305（建链失败），报告失败；
 *    5. 就绪之后：链路空闲超过保活间隔发保活包；超过断开判定时间收不到对方任何报文即判定断开，发 2305（链路超时），
 *       报告断开；收到对方的关闭报文或服务端推来的 2305 时同样报告断开；业务方 Close 时发关闭报文与 2305，不再报告。
 *  信令阶段也有总时限：开始后 kP2PGatherBudgetMs + 建链超时 + kP2PControlledExtraMs 内没有开始打洞（对方的 2303
 *  一直没来），按建链失败处理。对方的 2303 没来就没有对方的公钥，无法端到端加密，所以这种情况不改走中转。
 *  联调用的「强制中转」（SetForceRelay）：收到对方的 2303 后不打洞，直接改走中转。
 *
 *  中转路径上的报文与 UDP 路径相同（同一套端到端加密），imgserver 只看到密文；中转连接经观察者交来的
 *  IP2PDatagramSender 发出（地址参数被忽略），收到的报文经 OnRelayPacket 交回。中转就绪之后 UDP socket 上收到的
 *  链路报文一律忽略（路径选定后不切换）。
 *
 *  数据：就绪之后的数据报文交给通道复用层 P2PChannelMux（第 4 期起），它负责数据报的分片与重组、可靠通道。
 *
 *  线程：状态推进（Begin、OnPeerSignal、OnDatagram、OnTimer、OnServerClose 与四个中转入口）都在 IO 线程上调用；
 *  OpenChannel、SendData、Close、GetStats 可以在业务方的任意线程上调用。内部用递归互斥量保护。
 *  事件（就绪、失败、断开，以及通道收到数据、可写）在持有互斥量时只排进队列，由 IO 线程上的入口在释放互斥量之后
 *  依次交给观察者与通道的接收端（第 4a 步起）：业务方在回调里可以调用本对象的任何方法，也可以在持有自己的锁时调用
 *  本对象，不会因两把锁的获取顺序相反而互相等待。业务方线程上的入口不分发事件（它们也不产生事件）。
 *  业务方在别的线程上 Close 时，要等 IO 线程上正在进行的那一轮分发结束才返回，此后排队的事件一律不再分发：Close
 *  返回之后业务方可以立即销毁通道的接收端（第 5 期起）。在回调里 Close 时不等（等的就是自己）。
 *  发信令（SendSignal）与请求中转（StartRelay）仍在持有互斥量时调用，观察者在这两个回调里不得调用本对象。
 *
 *  成批发送（第 4e 步起）：UDP socket 支持发送分段卸载时，定时、收包与业务方发送这三个入口在持有互斥量期间把 UDP
 *  路径上的链路报文先攒起来，退出入口时把连续的满载报文（末尾可带一个短报文）一次交给 SendBatch。
 *  不碰 socket 与真实时钟：UDP 经 IP2PDatagramSender，信令与事件经 IP2PSessionObserver，时间由调用方传入。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include "../FlamingoP2PApi.h"
#include "../Core/P2PCrypto.h"
#include "../Core/P2PPacket.h"
#include "../Ice/P2PAddressProber.h"
#include "../Ice/P2PCandidate.h"
#include "../Ice/P2PConnectivityChecker.h"
#include "../Chan/P2PChannelMux.h"
#include "P2PSignalCodec.h"

namespace p2p {

class P2PLinkSession;

//会话的状态
enum p2p_session_state
{
    p2p_session_idle      = 0,   //尚未 Begin
    p2p_session_gathering = 1,   //正在取公网映射地址
    p2p_session_signaling = 2,   //本方 2303 已发出（或正在等对方的 2303），尚未开始打洞
    p2p_session_checking  = 3,   //正在打洞
    p2p_session_connected = 4,   //链路就绪
    p2p_session_failed    = 5,   //建链失败（终态）
    p2p_session_closed    = 6,   //已断开或已关闭（终态）
    p2p_session_relaying  = 7,   //打洞失败（或强制中转），正在连中转或经中转交换「选定 / 选定确认」（第 3e 步起）
};

//地址探测预留的时间（毫秒）：探测最长约 kP2PProbeMaxAttempts × kP2PProbeRetransmitMs，再留余量
const int64_t kP2PGatherBudgetMs = 1500;

//中转阶段的总时限（毫秒）：从改走中转起算，含连接、握手与报到（P2PRelayConn 最多 5 秒）和等对方经中转应答。
//应答方比发起方晚 kP2PControlledExtraMs 才改走中转，imgserver 在对方报到之前丢弃数据，发起方要一直重发
const int64_t kP2PRelayBudgetMs = 10000;

//发起方经中转重发「选定」的间隔（毫秒）：TCP 不丢包，重发只为等对方报到，取得比打洞的 50 毫秒宽
const int64_t kP2PRelayNominateIntervalMs = 200;

/**
 *  会话的观察者：由引擎实现（单测里用假的）。都在 IO 线程上被调用：SendSignal 与 StartRelay 在持有会话的互斥量时，
 *  OnSessionReady / OnSessionFailed / OnSessionClosed 在释放互斥量之后。
 *  析构函数受保护：会话只借用。
 */
class IP2PSessionObserver
{
public:
    /**
     *  经 chatserver 发一个 2300 段信令（2303 / 2304 / 2305）。
     *  @param cmd  命令号
     *  @param body 包体（UTF-8 JSON）
     */
    virtual void SendSignal(uint16_t cmd, const std::string& body) = 0;

    //链路就绪（直连或中转）
    virtual void OnSessionReady(P2PLinkSession* session) = 0;

    /**
     *  建链失败（就绪之前结束）。
     *  @param code 失败原因：Protocol.h 的 error_code（如 903 对端离线），本方打洞超时时为 0
     */
    virtual void OnSessionFailed(P2PLinkSession* session, int code) = 0;

    /**
     *  就绪之后断开（不包括本方业务方主动 Close）。
     *  @param reason 断开原因，取 p2p_close_reason
     */
    virtual void OnSessionClosed(P2PLinkSession* session, int reason) = 0;

    /**
     *  请求连接回落中转（第 3e 步起）：用会话信息里的中转地址与令牌连 imgserver，连上后调用会话的 OnRelayConnected，
     *  连不上时调用 OnRelayFailed；之后收到的报文经 OnRelayPacket 交回，连接断开时调用 OnRelayBroken。
     *  这些调用都不得在本回调里同步发生（本回调在持有会话互斥量时被调用），由 IO 线程稍后进行。
     *  一个会话最多请求一次。
     */
    virtual void StartRelay(P2PLinkSession* session) = 0;

    /**
     *  请 IO 线程尽快重新计算定时（第 4b 步起）：业务方在自己的线程上往可靠通道里交了数据，KCP 需要在下一个刷新间隔
     *  发出，而 IO 线程可能正按更晚的时刻等待；业务方打开的通道上有对方先发来、暂存着的数据，要由 IO 线程补交（第 6 期
     *  起）。可能在任意线程上、持有会话的互斥量时被调用，实现须线程安全且不得调用本会话。
     */
    virtual void RequestWake() = 0;

protected:
    ~IP2PSessionObserver()
    {
    }
};

/**
 *  一个 P2P 会话。私有继承 IP2PMuxOutput：通道复用层经它发数据报文。
 */
class P2PLinkSession : private IP2PMuxOutput
{
public:
    /**
     *  @param info     2302 推来的会话信息（复制保存）
     *  @param udp      本会话 UDP socket 的发送出口（借用）
     *  @param observer 观察者（借用）
     */
    P2PLinkSession(const P2PSessionInfo& info, IP2PDatagramSender& udp, IP2PSessionObserver& observer);

    /**
     *  联调用：收到对方的 2303 后不打洞，直接改走中转。须在 Begin 之前调用；2302 没带中转参数时不起作用。
     *  @param force 是否强制中转
     */
    void SetForceRelay(bool force);

    /**
     *  开始：生成密钥对、开始地址探测（没有探测服务器时直接发 2303）。
     *  @param localHosts 本机候选地址（端口为本会话 socket 的端口，带前缀长度）
     *  @param probes     地址探测服务器（已解析成地址）
     *  @param nowMs      当前时间（毫秒）
     */
    void Begin(const std::vector<P2PCandidate>& localHosts, const std::vector<P2PProbeServer>& probes, int64_t nowMs);

    /**
     *  收到对方的 2303。
     *  @param publicKey  对方公钥的 Base64 文本
     *  @param candidates 对方的候选地址
     *  @param nowMs      当前时间（毫秒）
     */
    void OnPeerSignal(const std::string& publicKey, const std::vector<P2PCandidate>& candidates, int64_t nowMs);

    /**
     *  本会话 socket 收到一个 UDP 报文。
     *  @param fromIp   来源地址（主机字节序）
     *  @param fromPort 来源端口（主机字节序）
     *  @param data     报文，只在本次调用期间有效
     *  @param len      报文长度（字节）
     *  @param nowMs    当前时间（毫秒）
     */
    void OnDatagram(uint32_t fromIp, uint16_t fromPort, const uint8_t* data, size_t len, int64_t nowMs);

    /**
     *  中转连接已完成握手与报到（第 3e 步起，由 IO 线程调用）。发起方随即经中转发出第一个「选定」。
     *  @param relay 中转连接的发送出口（借用，须在会话结束之前一直有效；SendTo 的地址参数被忽略）
     *  @param nowMs 当前时间（毫秒）
     */
    void OnRelayConnected(IP2PDatagramSender& relay, int64_t nowMs);

    /**
     *  中转连接失败（连不上、握手失败或报到被拒）：按建链失败处理。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnRelayFailed(int64_t nowMs);

    /**
     *  经中转收到对方的一个链路报文。
     *  @param data  报文（与 UDP 路径上的格式相同），只在本次调用期间有效
     *  @param len   报文长度（字节）
     *  @param nowMs 当前时间（毫秒）
     */
    void OnRelayPacket(const uint8_t* data, size_t len, int64_t nowMs);

    /**
     *  中转连接断开：就绪之前按建链失败处理，就绪之后按链路断开（链路超时）处理，都发 2305。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnRelayBroken(int64_t nowMs);

    /**
     *  中转连接把积压的数据写进了 socket、积压已经降下来（第 4b 步起，由 IO 线程调用）：此前被拒绝过的可靠通道
     *  收到可写通知。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnTransportDrained(int64_t nowMs);

    //2302 是否带了可用的中转参数（地址、端口、令牌都不为空）
    bool CanRelay() const;

    /**
     *  定时驱动：地址探测、发 2303、打洞、保活与断开判定、各阶段的时限。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnTimer(int64_t nowMs);

    //下一次需要调用 OnTimer 的时刻（毫秒）；终态时为 INT64_MAX
    int64_t NextTimerMs() const;

    /**
     *  服务端推来 2305（对方关闭、对方建链失败或对方掉线）。之后不再发 2305。
     *  @param reason  关闭原因，取 p2p_close_reason
     *  @param errCode 附带的错误码
     *  @param nowMs   当前时间（毫秒）
     */
    void OnServerClose(int reason, int errCode, int64_t nowMs);

    /**
     *  业务方关闭链路：就绪时先给对方发关闭报文，再经 chatserver 发 2305。之后不再报告任何事件。
     *  @param reason 关闭原因，取 p2p_close_reason
     *  @param nowMs  当前时间（毫秒）
     */
    void Close(int reason, int64_t nowMs);

    //以下三个方法供 IP2PLink 的实现转调，语义见 FlamingoP2PApi.h

    int OpenChannel(uint16_t channelId, int kind, IP2PChannelSink* sink);
    int SendData(uint16_t channelId, const uint8_t* data, uint32_t len, int64_t nowMs);
    void GetStats(P2PLinkStats* out) const;

    //当前状态，取值见 p2p_session_state
    int State() const;

    //是否已到终态（失败或关闭）
    bool IsFinished() const;

    //会话信息
    const P2PSessionInfo& Info() const { return m_info; }

    //就绪后的路径（P2PLinkPath）；未就绪时为 0
    int Path() const;

    //地址探测得到的 NAT 映射判断（p2p_nat_mapping）
    int NatMapping() const;

    //当前路径下数据报通道一次 Send 的上限（字节）
    size_t MaxDatagramMessageBytes() const;

private:
    P2PLinkSession(const P2PLinkSession&);
    P2PLinkSession& operator=(const P2PLinkSession&);

    //排队等待分发的事件种类
    enum session_event_type
    {
        session_event_ready   = 1,   //链路就绪：OnSessionReady
        session_event_failed  = 2,   //建链失败：OnSessionFailed，m_value 为错误码
        session_event_closed  = 3,   //就绪后断开：OnSessionClosed，m_value 为断开原因
        session_event_channel = 4,   //通道事件：交给 m_channel.m_sink
    };

    //一个排队等待分发的事件
    struct SessionEvent
    {
        int             m_type;      //事件种类，取值见 session_event_type
        int             m_value;     //错误码或断开原因
        P2PChannelEvent m_channel;   //通道事件（m_type 为 session_event_channel 时有效）

        SessionEvent()
            : m_type(0)
            , m_value(0)
        {
        }
    };

    /**
     *  作用域结束时分发排队的事件。须在获取互斥量之前构造：它晚于互斥量的守卫析构，分发时互斥量已经释放。
     *  只在 IO 线程上的入口使用。
     */
    class EventFlush
    {
    public:
        explicit EventFlush(P2PLinkSession& owner)
            : m_owner(owner)
        {
        }
        ~EventFlush()
        {
            m_owner.DispatchEvents();
        }

    private:
        EventFlush(const EventFlush&);
        EventFlush& operator=(const EventFlush&);

        P2PLinkSession& m_owner;   //所属会话
    };

    /**
     *  作用域内攒批：UDP 路径上的链路报文先放进 m_udpBatch，最外层作用域结束时一次发出。须在获取互斥量之后构造，
     *  它先于互斥量的守卫析构，发出时仍持有互斥量。
     */
    class BatchScope
    {
    public:
        explicit BatchScope(P2PLinkSession& owner)
            : m_owner(owner)
        {
            ++m_owner.m_batchDepth;
        }
        ~BatchScope()
        {
            if (--m_owner.m_batchDepth == 0)
                m_owner.FlushUdpBatch();
        }

    private:
        BatchScope(const BatchScope&);
        BatchScope& operator=(const BatchScope&);

        P2PLinkSession& m_owner;   //所属会话
    };

    //把攒下的 UDP 报文按组发出（调用方已持有 m_mutex）
    void FlushUdpBatch();

    //取出排队的事件，在不持有互斥量的情况下依次分发
    void DispatchEvents();

    //排一个会话事件
    void QueueEvent(int type, int value);

    //把通道事件排进队列
    void QueueChannelEvents(std::vector<P2PChannelEvent>& events);

    //IP2PMuxOutput 的实现：发一个数据报文（调用方已持有 m_mutex）
    virtual bool SendDataPacket(const std::string& plain, int64_t nowMs) override;

    //IP2PMuxOutput 的实现：发送路径上的积压（中转路径为中转连接的积压，UDP 路径为 0；调用方已持有 m_mutex）
    virtual size_t QueuedBytes() const override;

    //以下方法的调用方已持有 m_mutex

    //探测完成：发本方的 2303；对方的 2303 已到时开始打洞
    void SendOurSignal(int64_t nowMs);

    //开始打洞；强制中转时直接改走中转
    void StartChecking(int64_t nowMs);

    //打洞失败（或强制中转）：带中转参数时改走中转，否则按建链失败处理
    void FallbackToRelay(int64_t nowMs);

    //经中转就绪：发 2304（成功，路径为中转），报告就绪
    void MarkRelayReady(int64_t nowMs);

    //经中转发一个链路报文（用于「选定」与「选定确认」，就绪之后的报文走 SendLinkPacket）
    void SendRelayPacket(uint8_t type, int64_t nowMs);

    //处理就绪之后收到的报文（UDP 与中转两条路径共用）：数据、保活、关闭
    void HandleConnectedPacket(uint8_t type, const std::string& plain, int64_t nowMs);

    //打洞成功
    void MarkReady(int64_t nowMs);

    //建链失败：发 2304（失败）与 2305（建链失败），报告失败
    void FailConnect(int code, int64_t nowMs);

    //就绪后断开：sendClose 为真时发 2305，报告断开
    void FinishClosed(int reason, bool sendClose);

    //按选定的路径发一个链路报文
    void SendLinkPacket(uint8_t type, const std::string& plain, int64_t nowMs);

private:
    mutable std::recursive_mutex   m_mutex;              //保护以下全部状态
    P2PSessionInfo                 m_info;               //会话信息
    IP2PDatagramSender&            m_udp;                //UDP 发送出口（借用）
    IP2PSessionObserver&           m_observer;           //观察者（借用）
    P2PKeyPair                     m_keys;               //本方临时密钥对
    P2PLinkCipher                  m_cipher;             //加解密
    P2PAddressProber               m_prober;             //地址探测
    P2PConnectivityChecker         m_checker;            //打洞
    int                            m_state;              //当前状态，取值见 p2p_session_state
    std::vector<P2PCandidate>      m_localHosts;         //本机候选地址
    std::vector<P2PCandidate>      m_peerCandidates;     //对方的候选地址
    bool                           m_peerSignalReceived; //是否已收到对方的 2303
    int64_t                        m_beginMs;            //Begin 的时刻（毫秒）
    int64_t                        m_signalDeadlineMs;   //必须开始打洞的最晚时刻（毫秒）
    int64_t                        m_lastSendMs;         //就绪后最近一次向对方发报文的时刻（毫秒）
    int64_t                        m_lastRecvMs;         //就绪后最近一次收到对方报文的时刻（毫秒）
    int                            m_path;               //就绪后的路径，取 P2PLinkPath
    bool                           m_forceRelay;         //联调用：收到对方 2303 后不打洞，直接改走中转
    IP2PDatagramSender*            m_relay;              //中转连接的发送出口（借用）；连上中转之前为 NULL
    int64_t                        m_relayDeadlineMs;    //中转阶段必须完成选定的最晚时刻（毫秒）
    int64_t                        m_nextNominateMs;     //发起方下一次经中转发「选定」的时刻（毫秒）
    P2PChannelMux                  m_mux;                //通道复用层
    std::vector<SessionEvent>      m_events;             //排队等待分发的事件
    int                            m_dispatchDepth;      //正在分发事件的轮数（0 或 1：只有 IO 线程分发）
    std::thread::id                m_dispatchThread;     //正在分发事件的线程
    std::condition_variable_any    m_dispatchCv;         //一轮分发结束时唤醒等待中的 Close
    bool                           m_closedByBusiness;   //业务方是否已经 Close：之后排队的事件不再分发
    int                            m_batchDepth;         //攒批作用域的嵌套层数；大于 0 时 UDP 报文先攒起来
    std::vector<std::string>       m_udpBatch;           //攒下的 UDP 链路报文（发往打洞选定的地址）
};

} // namespace p2p
