/**
 *  一对一通话接收端的网络统计（纯逻辑），CallNetStats.h
 *
 *  第 2 期（同日）另加质量报告的编解码与 CallReportExchange（每秒互发报告、算往返时延），见文件后半部分。
 *  不考虑不支持报告的老版本客户端（2026-10-05 用户决定）：接通后每秒都发，不做试探与停发。
 *
 *  docs/音视频通话-网络状况标识方案.md 第 1 期：在接收端按窗口统计对方 → 本端这个方向的音频包到达情况——
 *    - 丢包：P2P 路径的音频包带序号，按序号缺口累计丢掉的个数；中转路径走 TCP、没有序号，丢包恒为 0；
 *    - 抖动：RFC 3550 的到达间隔抖动，D = 到达时刻差 − 发送时间戳差，J += (|D| − J) / 16，单位毫秒；
 *    - 断流：连续 kCallSilentMs 没有收到对方的任何消息（音频、视频或以后的质量报告）。
 *  CallMediaSession 在收包线程上喂数据、在统计线程上取窗口与判断断流；本类不加锁，由调用方串行化。
 *  不碰 socket、设备与窗口，单元测试见 Source/Tests/CallNetStatsTests.cpp（标签 [callnet]）。
 *
 *  典型用法：
 *      stats.Reset(nowMs);                                   //会话开始
 *      stats.OnAudioPacket(true, seq, tsMs, nowMs);          //收到一个音频包
 *      stats.OnAnyMessage(nowMs);                            //收到视频等其它消息
 *      CallRecvWindow w = stats.TakeWindow();                //统计线程每隔一段时间取走一个窗口
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace call {

//断流判定时长（毫秒）：连续这么久没有收到对方的任何消息即判为断流
const int64_t kCallSilentMs = 3000;

//质量报告（第 2 期）的固定长度（字节）与当前版本号；格式见 docs/音视频通话-网络状况标识方案.md 第 4.1 节
const size_t  kCallReportBytes   = 24;
const uint8_t kCallReportVersion = 1;
//报告里「档位」字段的「未评级」取值：第 3 期才做评级，此前一律填它
const uint8_t kCallGradeUnknown  = 0xFF;
//报告标志字节（偏移 22，2026-10-06 起占用原保留字段的第 1 字节，docs/音视频通话方案.md 第 10 节）的各位：
//本端能接收电脑声音（新版本每份报告都带；旧版本这个字节是 0，据此不向它发送电脑声音）
const uint8_t kCallReportFlagRecvSystemAudio    = 0x01;
//本端正在共享电脑声音（对方据此显示「对方正在共享电脑声音」）
const uint8_t kCallReportFlagSharingSystemAudio = 0x02;

//一个统计窗口的结果
struct CallRecvWindow
{
    uint32_t m_received;       //窗口内收到的音频包数（不含迟到、重复的包）
    uint32_t m_lost;           //窗口内按序号缺口判定丢掉的音频包数（中转路径恒为 0）
    uint32_t m_late;           //窗口内迟到或重复、被丢弃的音频包数
    uint32_t m_lossPermille;   //丢包率（千分比）= m_lost / (m_received + m_lost)；没有应收包时为 0
    uint32_t m_jitterMs;       //取窗口时的抖动估计（毫秒，四舍五入）

    CallRecvWindow();
};

//对方 → 本端这个方向的接收统计
class CallRecvStats
{
public:
    CallRecvStats();

    /**
     *  从头开始统计（会话开始时调用）。断流计时从 nowMs 起算：一条都没收到时，过了 kCallSilentMs 也判为断流。
     *  @param nowMs 当前时刻（单调时钟毫秒）
     */
    void Reset(int64_t nowMs);

    /**
     *  收到一个音频包。
     *  @param hasSeq 是否带序号（P2P 路径为 true，中转路径为 false）
     *  @param seq    序号（hasSeq 为 false 时忽略），按 32 位回绕比较
     *  @param tsMs   发送端打的会话时间戳（毫秒），用于计算抖动
     *  @param nowMs  本端收到的时刻（单调时钟毫秒）
     */
    void OnAudioPacket(bool hasSeq, uint32_t seq, uint64_t tsMs, int64_t nowMs);

    /**
     *  收到对方的任意一条消息（视频帧、关键帧请求、以后的质量报告）：只刷新断流计时。音频包由 OnAudioPacket 刷新，不必再调本函数。
     *  @param nowMs 本端收到的时刻（单调时钟毫秒）
     */
    void OnAnyMessage(int64_t nowMs);

    /**
     *  取走当前窗口：返回窗口内的计数并清零；抖动是滑动估计，不清零。
     *  @return 窗口结果
     */
    CallRecvWindow TakeWindow();

    /**
     *  是否断流：距最后一条消息（一条都没收到时距 Reset）不少于 kCallSilentMs。
     *  @param nowMs 当前时刻（单调时钟毫秒）
     */
    bool IsSilent(int64_t nowMs) const;

    //当前的抖动估计（毫秒，四舍五入）
    uint32_t JitterMs() const;

private:
    bool     m_seqStarted;    //是否已收到过带序号的音频包
    uint32_t m_nextSeq;       //期望的下一个序号
    bool     m_tsStarted;     //是否已收到过音频包（算抖动要有上一个包）
    uint64_t m_lastTsMs;      //上一个音频包的发送时间戳（毫秒）
    int64_t  m_lastArriveMs;  //上一个音频包的到达时刻（毫秒）
    double   m_jitterMs;      //抖动估计（毫秒）
    int64_t  m_lastHeardMs;   //最后一次收到对方任何消息的时刻（Reset 时为开始时刻）
    uint32_t m_received;      //当前窗口收到的音频包数
    uint32_t m_lost;          //当前窗口判定丢掉的音频包数
    uint32_t m_late;          //当前窗口迟到或重复的音频包数
};

//一份质量报告（第 2 期）：本端对「对方 → 本端」这个方向的接收情况，外加测往返时延用的回带字段
struct CallQualityReport
{
    uint8_t  m_version;        //版本号；本端发出的恒为 kCallReportVersion，收到的可能更高
    uint8_t  m_grade;          //本端对「我收对方」评出的档位；第 3 期之前恒为 kCallGradeUnknown
    uint16_t m_lossPermille;   //丢包率（千分比）
    uint16_t m_jitterMs;       //抖动（毫秒）
    uint16_t m_stallPermille;  //播放端补静音的比例（千分比）
    uint16_t m_bufferMs;       //播放缓冲延迟（毫秒，2026-10-06 方案 B 第 B4 步）：这一秒里正常播放时抖动缓冲中已收到、
                               //还没播放的平均时长；占原先的保留字段（偏移 20 起 2 字节），版本号不变
    uint64_t m_echoTsMs;       //回带：上一次收到的对方报告的发送时刻（对方的会话毫秒）；还没收到过时为 0
    uint32_t m_holdMs;         //回带：收到那份报告到发出本报告之间的毫秒数（本端时钟）
    uint8_t  m_flags;          //标志位（2026-10-06，偏移 22）：kCallReportFlagRecvSystemAudio 等按位或；旧版本发来的为 0

    CallQualityReport();
};

/**
 *  把报告编成 kCallReportBytes 字节（网络字节序），保留字段（偏移 23 的 1 字节）填 0。报告自身的发送时刻不在负载里，沿用两种封装头里的时间戳。
 *  @param r   报告
 *  @param out [出参] 编码结果（先清空）
 */
void BuildCallQualityReport(const CallQualityReport& r, std::vector<uint8_t>& out);

/**
 *  解析一份报告。版本号更高的报告只读认识的字段（后面多出的字节忽略）。
 *  @return 长度不足 kCallReportBytes 或版本号为 0 时返回 false
 */
bool ParseCallQualityReport(const uint8_t* data, size_t len, CallQualityReport& out);

/**
 *  双方互发质量报告的状态（第 2 期）：给每秒的报告填回带字段，收到对方报告时算往返时延。
 *  往返时延按 RTCP 的 LSR / DLSR 做法：RTT = 收到时刻 − 回带时刻 − 对方停留时长，回带时刻与收到时刻都是本端时钟，
 *  不要求两端时钟同步。所有时刻都是本端的「会话毫秒」（与封装头里的时间戳同一时间轴）。
 *  不加锁，由调用方串行化（统计线程发、收包线程收，CallMediaSession 用一把锁保护）。
 */
class CallReportExchange
{
public:
    CallReportExchange();

    //从头开始（会话开始时调用）
    void Reset();

    /**
     *  准备发一份报告：填版本号与回带字段。还没收到过对方报告时回带时刻与停留时长都为 0。
     *  @param nowMs 当前时刻（本端会话毫秒）
     *  @param r     [入出参] 调用方先填好接收统计字段，本函数填版本号、回带时刻与停留时长
     */
    void PrepareReport(int64_t nowMs, CallQualityReport& r);

    /**
     *  收到对方的一份报告：记下它的发送时刻（供下一份回带），回带时刻不为 0 时算一次往返时延。
     *  @param r             解析出的报告
     *  @param peerSendTsMs  对方发出它的时刻（对方会话毫秒，取自封装头）
     *  @param nowMs         本端收到的时刻（本端会话毫秒）
     */
    void OnPeerReport(const CallQualityReport& r, uint64_t peerSendTsMs, int64_t nowMs);

    //是否已有往返时延样本
    bool HasRtt() const { return m_hasRtt; }

    //最近一次的往返时延（毫秒）；HasRtt 为 false 时为 0
    uint32_t RttMs() const { return m_rttMs; }

    //是否收到过对方的报告
    bool HasPeerReport() const { return m_peerReports > 0; }

    //最近一份对方报告（HasPeerReport 为 false 时为默认值）
    const CallQualityReport& LastPeerReport() const { return m_lastPeer; }

    //累计收到的对方报告份数
    uint64_t ReportsReceived() const { return m_peerReports; }

private:
    uint64_t          m_peerReports;    //累计收到的对方报告份数
    uint64_t          m_peerSendTsMs;   //最近一份对方报告的发送时刻（对方会话毫秒），下一份报告回带它
    int64_t           m_peerRecvMs;     //收到那份报告的时刻（本端会话毫秒），用来算停留时长
    bool              m_hasRtt;         //是否已有往返时延样本
    uint32_t          m_rttMs;          //最近一次的往返时延（毫秒）
    CallQualityReport m_lastPeer;       //最近一份对方报告
};

} // namespace call
