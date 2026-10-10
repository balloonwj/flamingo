/**
 *  一条链路上的通道复用（docs/P2P通信方案.md 7.3 节，第 4 期起）。
 *
 *  链路报文里类型为「数据」的报文，明文是 [通道号 2B][通道类型 1B][负载]。本类管理一条链路上打开的全部通道：
 *  - 数据报通道（第 4a 步）：一次 Send 的数据按路径切片（P2PDatagramFrag），每片一个数据报文；接收端收齐后交给业务方；
 *  - 可靠通道（第 4b 步起）：UDP 路径上经 KCP，中转路径上直接透传。
 *  发送经 IP2PMuxOutput 交给会话（会话负责加密并选路），收到的数据与可写通知不直接回调业务方，而是作为事件交给
 *  调用方，由会话在释放互斥量之后分发（业务方在回调里可以调用链路的任何方法，也可以在持有自己的锁时调用链路）。
 *
 *  对方先打开通道、先发了数据，而本方还没打开同号通道时（两端各自打开，先后没有保证），报文先暂存起来，本方打开
 *  之后在下一次 OnTimer 里按到达顺序补交（第 6 期起）。中转路径上的可靠通道不经 KCP、没有重传，丢掉这些报文会让
 *  字节流永久缺一段；暂存总量不超过 kP2PEarlyDataLimitBytes，超出的报文仍然丢弃。
 *
 *  不加锁，由会话的互斥量保护；时间由调用方传入。
 *
 *  典型用法（P2PLinkSession）：
 *      m_mux.SetPath(p2p_link_path_lan);                               //就绪时
 *      m_mux.Open(channelId, p2p_channel_datagram, sink);
 *      m_mux.Send(channelId, data, len, now);
 *      m_mux.OnPacket(plain, now, events);                             //收到数据报文
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../FlamingoP2PApi.h"
#include "P2PDatagramFrag.h"
#include "P2PReliableChannel.h"

namespace p2p {

//数据报文明文里通道头的长度（字节）：通道号 2 + 通道类型 1
const size_t kP2PChannelHeaderBytes = 3;

//本方还没打开的通道上暂存报文的总量上限（字节，含通道头）：远程桌面的被控端在控制端打开通道之前可能已推出一帧
//约 4 MB 的关键帧和几帧增量，文件夹传输在这段时间里只有清单与开头的几个分片，16 MiB 足够两者
const size_t kP2PEarlyDataLimitBytes = 16 * 1024 * 1024;

//通道事件的种类
enum p2p_channel_event_type
{
    p2p_channel_event_data     = 1,   //通道收到一块数据（数据报通道为一条完整消息，可靠通道为一段字节流）
    p2p_channel_event_writable = 2,   //发送队列已降到上限以下，此前返回过 p2p_send_would_block 的通道可以继续发送
};

//一个通道事件：由会话在释放互斥量之后交给业务方
struct P2PChannelEvent
{
    int              m_type;        //事件种类，取值见 p2p_channel_event_type
    uint16_t         m_channelId;   //通道号
    IP2PChannelSink* m_sink;        //接收端（借用）
    std::string      m_data;        //数据（m_type 为 p2p_channel_event_data 时有效）

    P2PChannelEvent()
        : m_type(0)
        , m_channelId(0)
        , m_sink(NULL)
    {
    }
};

/**
 *  通道复用层的发送出口：由会话实现。
 *  析构函数受保护：复用层只借用。
 */
class IP2PMuxOutput
{
public:
    /**
     *  发一个数据报文（会话负责加密并按路径发出）。
     *  @param plain 明文：通道头 + 负载，长度不超过当前路径的明文上限
     *  @param nowMs 当前时间（毫秒）
     *  @return 已交给系统发送返回 true
     */
    virtual bool SendDataPacket(const std::string& plain, int64_t nowMs) = 0;

    //发送路径上还没交给系统的字节数：中转路径为中转连接里积压的字节，UDP 路径为 0（第 4b 步起）
    virtual size_t QueuedBytes() const = 0;

protected:
    ~IP2PMuxOutput()
    {
    }
};

/**
 *  一条链路上的全部通道。
 */
class P2PChannelMux
{
public:
    /**
     *  @param out 发送出口（借用，须比本对象活得久）
     */
    explicit P2PChannelMux(IP2PMuxOutput& out);
    ~P2PChannelMux();

    /**
     *  设置路径（链路就绪时调用一次）：决定单个数据报文的明文上限与可靠通道的实现方式。
     *  @param path 路径，取 P2PLinkPath
     */
    void SetPath(int path);

    /**
     *  打开一条通道，语义见 IP2PLink::OpenChannel。该通道上已有暂存的报文时，登记为待补交（见 HasEarlyReplay），
     *  由下一次 OnTimer 交出；本函数本身不产生事件。
     *  @return p2p_send_ok；类型不支持、接收端为空或通道号已被占用时返回 p2p_send_bad_channel
     */
    int Open(uint16_t channelId, int kind, IP2PChannelSink* sink);

    //是否有刚打开的通道等着补交暂存的报文：为真时 NextTimerMs 返回 0，调用方应尽快调用 OnTimer
    bool HasEarlyReplay() const { return !m_replayChannels.empty(); }

    //暂存着的报文总字节数（含通道头）
    size_t EarlyBytes() const { return m_earlyBytes; }

    /**
     *  在通道上发送一块数据，语义见 IP2PLink::Send。
     *  @return P2PSendResult
     */
    int Send(uint16_t channelId, const uint8_t* data, uint32_t len, int64_t nowMs);

    /**
     *  收到一个数据报文。
     *  @param plain  明文：通道头 + 负载
     *  @param nowMs  当前时间（毫秒）
     *  @param events [出参] 追加产生的事件
     */
    void OnPacket(const std::string& plain, int64_t nowMs, std::vector<P2PChannelEvent>& events);

    /**
     *  定时驱动：先补交刚打开的通道上暂存的报文，再做可靠通道的重传与确认。
     *  @param nowMs  当前时间（毫秒）
     *  @param events [出参] 追加产生的事件
     */
    void OnTimer(int64_t nowMs, std::vector<P2PChannelEvent>& events);

    //下一次需要调用 OnTimer 的时刻（毫秒）：有待补交的暂存报文时为 0（立即），没有定时工作时为 INT64_MAX
    int64_t NextTimerMs() const;

    //当前路径下数据报通道一次 Send 的上限（字节）；路径未设置时按 UDP 路径计
    size_t MaxDatagramMessageBytes() const;

    /**
     *  发送路径的积压降下来了（中转连接把缓冲写进了 socket）：检查此前被拒绝过的可靠通道，产生可写事件。
     *  @param events [出参] 追加产生的事件
     */
    void CheckWritable(std::vector<P2PChannelEvent>& events);

    //通道是否已打开且为可靠通道
    bool IsReliable(uint16_t channelId) const;

    //全部通道发送队列积压的字节数（含发送路径上的积压）
    size_t QueuedBytes() const;

    //可靠通道里最大的平滑往返时延（毫秒）；没有样本时为 0
    uint32_t SmoothedRttMs() const;

    //可靠通道累计重传的分段数与累计发出的报文数（估算丢包率用）
    uint64_t RetransmittedSegments() const;
    uint64_t SentSegments() const;

    //累计发出 / 收到的业务字节数（不含通道头、分片头与重传）
    uint64_t BytesSent() const { return m_bytesSent; }
    uint64_t BytesReceived() const { return m_bytesReceived; }

private:
    P2PChannelMux(const P2PChannelMux&);
    P2PChannelMux& operator=(const P2PChannelMux&);

    //一条通道
    struct Channel
    {
        int                    m_kind;          //通道类型，取 P2PChannelKind
        IP2PChannelSink*       m_sink;          //接收端（借用）
        uint32_t               m_nextMsgId;     //数据报通道：下一条消息的消息号
        P2PDatagramReassembler m_reassembler;   //数据报通道：重组
        P2PReliableChannel*    m_reliable;      //可靠通道（归本对象所有）；数据报通道为 NULL

        Channel()
            : m_kind(0)
            , m_sink(NULL)
            , m_nextMsgId(0)
            , m_reliable(NULL)
        {
        }
    };

    //可靠通道 TakeWritable 为真时追加一个可写事件
    void AppendWritable(uint16_t channelId, Channel& ch, std::vector<P2PChannelEvent>& events);

    //当前路径下单个数据报文的明文上限（字节）
    size_t MaxPlain() const;

    //发数据报通道的一条消息
    int SendDatagram(uint16_t channelId, Channel& ch, const uint8_t* data, uint32_t len, int64_t nowMs);

    //本方还没打开的通道收到报文：总量未超上限时暂存，超出时丢弃
    void KeepEarly(uint16_t channelId, const std::string& plain);

    //把待补交通道上暂存的报文按到达顺序交给 OnPacket
    void ReplayEarly(int64_t nowMs, std::vector<P2PChannelEvent>& events);

private:
    IP2PMuxOutput&                m_out;             //发送出口（借用）
    int                           m_path;            //路径，取 P2PLinkPath；未设置时为 0
    std::map<uint16_t, Channel*>  m_channels;        //通道号 → 通道（归本对象所有）
    uint64_t                      m_bytesSent;       //累计发出的业务字节数
    uint64_t                      m_bytesReceived;   //累计收到的业务字节数
    std::map<uint16_t, std::deque<std::string> > m_early;   //本方还没打开的通道上暂存的报文：通道号 → 报文明文（按到达顺序）
    size_t                        m_earlyBytes;      //m_early 里报文的总字节数，不超过 kP2PEarlyDataLimitBytes
    std::set<uint16_t>            m_replayChannels;  //已打开、等着补交暂存报文的通道号；补交后清空
};

} // namespace p2p
