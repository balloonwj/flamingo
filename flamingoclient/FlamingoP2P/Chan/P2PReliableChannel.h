/**
 *  可靠有序通道（docs/P2P通信方案.md 7.3、7.4 节，第 4 期 4b）。
 *
 *  - UDP 路径：每条通道一个 KCP 会话（third-party/kcp，带本项目的修正），流模式。KCP 的输出加上通道头后经
 *    IP2PMuxOutput 作为数据报文发出；收到的数据报文去掉通道头后交给 ikcp_input，再用 ikcp_recv 取出按序的字节流；
 *  - 中转路径：中转本身是 TCP，已经可靠有序，不经 KCP，数据按单个报文的明文上限切块后直接发出，收到即交出；
 *  - 发送队列：UDP 路径为 KCP 尚未确认的分段（发送队列与发送缓冲），中转路径为中转连接里还没写进 socket 的字节。
 *    超过 kP2PReliableQueueLimitBytes 时 Send 返回 p2p_send_would_block，降到 kP2PReliableWritableBytes 以下时
 *    （由调用方调用 TakeWritable 检查）通知业务方可写；
 *  - 定时：KCP 有待发、待确认的数据或待回的确认时，按 ikcp_check 给出下一次刷新时刻；全部空闲时不需要定时
 *    （避免空闲通道每 2 毫秒唤醒一次 IO 线程），收到报文或业务方发送后再恢复。
 *
 *  拥塞控制：UDP 路径经 ikcp_setcc 挂入 BBR 类拥塞控制 P2PBbrCc（第 4c 步起），不用 KCP 自带的；中转路径由 TCP 负责。
 *  不加锁，由会话的互斥量保护；时间由调用方传入。
 *
 *  典型用法（P2PChannelMux）：
 *      P2PReliableChannel ch(channelId, out, true);
 *      ch.Send(data, len, now);                           //业务方发送
 *      ch.OnPayload(payload, len, now, delivered);        //收到数据报文（去掉通道头之后）
 *      ch.OnTimer(now);                                   //到了 NextTimerMs
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "../FlamingoP2PApi.h"

struct IKCPCB;

namespace p2p {

class IP2PMuxOutput;

//可靠通道发送队列的上限（字节）：超过时 Send 返回 p2p_send_would_block。取 4 MiB：千兆局域网上约 30 毫秒的数据，
//足以让发送不断流，又不至于在业务方暂停时积压太多
const size_t kP2PReliableQueueLimitBytes = 4 * 1024 * 1024;

//发送队列降到这个值以下时通知可写（字节）：上限的一半，避免在上限附近反复切换
const size_t kP2PReliableWritableBytes = kP2PReliableQueueLimitBytes / 2;

//KCP 的刷新间隔（毫秒）：第 0b 步的结论，2 毫秒；直接写 kcp->interval，ikcp_nodelay 会把它抬到 10 毫秒
const int kP2PKcpIntervalMs = 2;

//KCP 的快速重传阈值：收到 2 个跨过某分段的确认即重传它
const int kP2PKcpFastResend = 2;

//KCP 的收发窗口（分段数）：每段约 1.1 KB，4096 段约 4.5 MB，足以覆盖百兆宽带上百毫秒往返的在途数据
const int kP2PKcpWindow = 4096;

//一次交给 ikcp_send 的最大字节数：ikcp_send 一次最多切 127 个分段（IKCP_WND_RCV - 1），超过时要分次交
const size_t kP2PKcpSendChunkBytes = 64 * 1024;

/**
 *  一条可靠有序通道。
 */
class P2PReliableChannel
{
public:
    /**
     *  @param channelId 通道号（也作 KCP 的会话号 conv，两端相同）
     *  @param out       发送出口（借用，须比本对象活得久）
     *  @param overKcp   是否经 KCP（UDP 路径为 true，中转路径为 false）
     */
    P2PReliableChannel(uint16_t channelId, IP2PMuxOutput& out, bool overKcp);
    ~P2PReliableChannel();

    /**
     *  发送一段字节流。
     *  @param data  数据；len 为 0 时可以为 NULL（什么也不发）
     *  @param len   长度（字节）
     *  @param nowMs 当前时间（毫秒）
     *  @return p2p_send_ok；发送队列超过上限时返回 p2p_send_would_block（本次一个字节也没收下）
     */
    int Send(const uint8_t* data, uint32_t len, int64_t nowMs);

    /**
     *  收到本通道的一个数据报文的负载（已去掉通道头）。
     *  @param payload   负载
     *  @param len       长度（字节）
     *  @param nowMs     当前时间（毫秒）
     *  @param delivered [出参] 追加按序可交给业务方的字节
     */
    void OnPayload(const uint8_t* payload, size_t len, int64_t nowMs, std::string& delivered);

    /**
     *  定时驱动：KCP 的刷新（发送、重传、回确认）。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnTimer(int64_t nowMs);

    //下一次需要调用 OnTimer 的时刻（毫秒）；没有定时工作时为 INT64_MAX
    int64_t NextTimerMs() const;

    //发送队列积压的字节数
    size_t QueuedBytes() const;

    /**
     *  此前返回过 p2p_send_would_block、现在队列已降到 kP2PReliableWritableBytes 以下时返回 true（并清除标记），
     *  调用方据此通知业务方可写；其它情况返回 false。
     */
    bool TakeWritable();

    //KCP 平滑后的往返时延（毫秒）；中转路径或还没有样本时为 0
    uint32_t SmoothedRttMs() const;

    //累计重传的分段数（超时与快速重传）与累计发出的分段数，用于估算丢包率；中转路径都为 0
    uint64_t RetransmittedSegments() const;
    uint64_t SentSegments() const;

    //KCP 控制块（第 4c 步挂拥塞控制用）；中转路径为 NULL
    IKCPCB* Kcp() { return m_kcp; }

private:
    P2PReliableChannel(const P2PReliableChannel&);
    P2PReliableChannel& operator=(const P2PReliableChannel&);

    //KCP 的输出回调：加通道头后交给发送出口
    static int KcpOutput(const char* buf, int len, IKCPCB* kcp, void* user);

    //中转路径：按单个报文的明文上限切块发出
    void SendDirect(const uint8_t* data, uint32_t len, int64_t nowMs);

    //把当前时间记下并换算成 KCP 的 32 位毫秒时间
    uint32_t KcpNow(int64_t nowMs);

private:
    uint16_t         m_channelId;     //通道号
    IP2PMuxOutput&   m_out;           //发送出口（借用）
    bool             m_overKcp;       //是否经 KCP
    IKCPCB*          m_kcp;           //KCP 控制块（归本对象所有）；中转路径为 NULL
    int64_t          m_lastNowMs;     //最近一次调用传入的时间（毫秒）：NextTimerMs 据此把 KCP 的 32 位时间换算回来
    int64_t          m_outputNowMs;   //正在刷新时的时间（毫秒），交给发送出口
    bool             m_blocked;       //是否返回过 p2p_send_would_block 且尚未通知可写
    uint64_t         m_sentSegments;  //累计经 KCP 输出的报文数
    std::string      m_recvBuf;       //ikcp_recv 的接收缓冲（第一次收到数据时分配，之后复用）
};

} // namespace p2p
