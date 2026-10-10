/**
 *  可靠通道的 BBR 类拥塞控制（docs/P2P通信方案.md 7.4 节与 14.1 节，第 4 期 4c）。
 *
 *  经 ikcp_setcc 挂到 KCP 上，取代 KCP 自带的拥塞控制。思路是按「瓶颈带宽 × 最小往返时延」决定发送速率与在途数据量，
 *  不因随机丢包降速：
 *  - 送达速率：按「一个时间窗内累计确认的字节 / 时长」取样，窗口取最小往返时延与 10 毫秒中的大者；
 *    瓶颈带宽取最近 10 轮样本的最大值（发送受业务方限制的一轮，样本不高于当前估计时不计入）；
 *  - 四个阶段：启动（2.885 倍增速，带宽连续 3 轮增长不足 25% 或一轮内丢失超过 2% 时结束）→ 排空（把启动阶段多塞进
 *    队列的数据放掉，最多 2 轮）→ 带宽探测（按 1.25 / 0.75 / 1 × 6 循环，每阶段一轮）↔ 往返时延探测（10 秒没有刷新
 *    最小往返时延时，把在途数据压到 4 个分段持续 max(最小往返时延, 200 毫秒)，让队列清空后重新测量）；
 *  - 在途数据上限 = 增益 × 带宽 × 最小往返时延 + 2 × 带宽 × 刷新间隔，不少于 4 个分段；
 *  - 发送节奏：每次刷新的发送额度 = 增益 × 带宽 × 距上次刷新的时长（最多计 10 毫秒），上次没用完的额度最多结转一个分段；
 *  - 往返时延样本取自 on_rtt（按顺序到达时 on_pkt_acked 拿不到往返时延，第 0b 步实测）；同一回调里把 KCP 的超时放宽到
 *    平均往返时延 + max(4 × 偏差, 平均往返时延的四分之一, 10 毫秒)，否则平稳路径上会频繁误判超时；
 *  - on_ack / on_fast_retransmit / on_timeout 三个回调都要给出（可以是空实现），否则 KCP 仍会执行自带的窗口增减逻辑。
 *
 *  第 0b 步原型在模拟网络里的结果（14.1 节第 3 项）：带宽利用率 68%～98%，平均排队延迟多在 6 毫秒以内；无丢包的长肥
 *  链路上排队延迟约等于一个往返时延，这是 BBR 第一版的已知特点。
 *
 *  不加锁：由持有 KCP 的可靠通道在会话的互斥量内调用。
 *
 *  典型用法（P2PReliableChannel）：
 *      ikcp_setcc(kcp, P2PBbrCc::Ops());
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>

struct IKCPCB;
struct IKCPOPS;

namespace p2p {

//拥塞控制所处的阶段
enum p2p_bbr_mode
{
    p2p_bbr_startup   = 0,   //启动：快速探出瓶颈带宽
    p2p_bbr_drain     = 1,   //排空：放掉启动阶段塞进队列的数据
    p2p_bbr_probe_bw  = 2,   //带宽探测：稳态，周期性地试探更高的速率
    p2p_bbr_probe_rtt = 3,   //往返时延探测：压低在途数据，重新测量最小往返时延
};

//拥塞控制的对外可见状态（单测与统计用）
struct P2PBbrState
{
    int      m_mode;          //阶段，取值见 p2p_bbr_mode
    double   m_btlBwBytesMs;  //瓶颈带宽估计（字节 / 毫秒）
    uint32_t m_minRttMs;      //最小往返时延（毫秒），0 表示尚无样本
    double   m_pacingGain;    //当前的速率增益
    uint32_t m_cwnd;          //当前的在途数据上限（分段数）
};

/**
 *  BBR 类拥塞控制。KCP 的回调表由 Ops 给出，状态挂在 kcp->congest 上，由 KCP 在 ikcp_setcc / ikcp_release 时
 *  调用 init / release 创建与销毁。
 */
class P2PBbrCc
{
public:
    /**
     *  KCP 的拥塞控制回调表（静态存储，进程内共用）。
     *  @return 回调表，交给 ikcp_setcc
     */
    static const IKCPOPS* Ops();

    /**
     *  取一个 KCP 上的拥塞控制状态。
     *  @param kcp 已经挂了本拥塞控制的 KCP 控制块
     *  @param out [出参] 状态
     *  @return 挂的是本拥塞控制时返回 true
     */
    static bool GetState(const IKCPCB* kcp, P2PBbrState& out);
};

} // namespace p2p
