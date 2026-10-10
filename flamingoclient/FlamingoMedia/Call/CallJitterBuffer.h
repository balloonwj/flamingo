/**
 *  通话声音的抖动缓冲（包装 speexdsp 的 JitterBuffer），CallJitterBuffer.h
 *
 *  docs/音视频通话方案.md 第 9.9 节方案 B（2026-10-06）：每一路远端声音一个本对象。收包线程按「媒体
 *  时间戳」（采集帧序号 × 960 个采样）放入编码包，播放线程每 20 毫秒取一段：取到一个包、这一段缺失、或要插入一段
 *  （speexdsp 想加大缓冲）。延迟由 speexdsp 按近期的迟到情况自适应：迟到多了加大，长时间都按时到达就减小。
 *
 *  在 speexdsp 之上补了三件事：
 *    - 加锁：speexdsp 的抖动缓冲没有任何加锁（jitter.c），收包线程与播放线程共用时须串行化；
 *    - 跨度为 0 的处理：刚开始、重置之后还没收到包时，speexdsp 返回跨度为 0 的缺失，这里按一帧缺失交给调用方，
 *      避免调用方陷入「要产出 0 个采样」的死循环；
 *    - 时间线重置：缓冲已经取空时，收到时间戳明显落后（发送端序号在静音期间没有前进：麦克风开关、电脑声音间歇、
 *      老版本客户端）或明显超前的包，先重置再放入。否则落后的包会被 speexdsp 当作「迟到太久」直接丢掉，
 *      要等连续缺失 20 帧（400 毫秒）它才自己重置，说话的开头就没了。
 *
 *  不碰声卡与网络，不解码（负载原样交还调用方），单元测试见 Source/Tests/CallJitterBufferTests.cpp（标签 [jitterbuf]）。
 *
 *  典型用法：
 *      jb.Put(opus, len, frameIndex * kCallJbFrameSamples);   //收包线程
 *      CallJbFrame f;
 *      jb.GetFrame(f);                                         //播放线程，每 20 毫秒一次
 *      if (f.m_result == call_jb_packet) 解码 f.m_data；否则补 f.m_span 个采样（丢包补偿或静音）
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <mutex>
#include <vector>

struct JitterBuffer_;   //speexdsp 的抖动缓冲（speex/speex_jitter.h），这里只持有指针

namespace call {

//一帧的采样数：48 kHz 下 20 毫秒。媒体时间戳以采样为单位，每帧前进这么多
const int kCallJbFrameSamples = 960;

//GetFrame 的结果
enum CallJbResult
{
    call_jb_packet  = 0,   //取到一个包：m_data 是它的负载，m_span 是它覆盖的采样数，播放指针随之前进
    call_jb_missing = 1,   //这一段缺失（丢包、还没到、对方没在发）：调用方补 m_span 个采样，播放指针照常前进
    call_jb_insert  = 2,   //要加大缓冲：调用方补 m_span 个采样，播放指针不前进
};

//GetFrame 取出的一段
struct CallJbFrame
{
    int                        m_result;      //取 CallJbResult
    int                        m_span;        //本次要产出的采样数，恒大于 0
    uint32_t                   m_timestamp;   //call_jb_packet 时为包的媒体时间戳（采样）；其余结果为 0
    std::vector<unsigned char> m_data;        //call_jb_packet 时为包的负载；其余结果为空

    CallJbFrame();
};

//累计计数（Reset 时清零）
struct CallJbStats
{
    uint64_t m_puts;          //放入的包数
    uint64_t m_packets;       //取出的包数
    uint64_t m_missing;       //取到「缺失」的次数
    uint64_t m_inserts;       //取到「插入」的次数
    uint64_t m_lateDropped;   //因迟到太久被 speexdsp 丢弃的包数（放入时判定）
    uint64_t m_resets;        //时间线重置的次数（本类主动重置；不含 speexdsp 自己的重置）
    uint64_t m_bufferedSum;   //每次取到包之后量一次缓冲深度（采样），这里是它们的累计和（2026-10-06，B4）
    uint64_t m_bufferedCount; //上一项量了多少次；调用方取两次之差相除，得到一段时间里正常播放时的平均缓冲深度

    CallJbStats();
};

/**
 *  一路声音的抖动缓冲。Put 与 GetFrame 可以在不同线程上调用（内部加锁）。
 */
class CallJitterBuffer
{
public:
    CallJitterBuffer();

    //析构：释放 speexdsp 的抖动缓冲与其中尚未取出的包
    ~CallJitterBuffer();

    //清空并回到初始状态（计数一并清零）
    void Reset();

    /**
     *  收包线程：放入一个编码包（负载会被拷贝）。
     *  @param data       负载（借用，调用期间有效）；为 NULL 或 len 为 0 时忽略
     *  @param len        负载字节数
     *  @param tsSamples  媒体时间戳：发送端的采集帧序号 × kCallJbFrameSamples（32 位，约 24 小时回绕一次，内部按回绕比较）
     */
    void Put(const unsigned char* data, size_t len, uint32_t tsSamples);

    /**
     *  播放线程：取下一段（20 毫秒），随后内部推进一个节拍。调用方须按实际播放的节奏调用，每产出一帧调用一次。
     *  @param frame [出参] 取到的一段
     */
    void GetFrame(CallJbFrame& frame);

    //已收到、还没播放的时长（采样）：最近放入的包的末尾 − 播放指针；还没开始播放或缓冲为空时为 0
    int BufferedSamples() const;

    //累计计数
    CallJbStats Stats() const;

private:
    CallJitterBuffer(const CallJitterBuffer&);
    CallJitterBuffer& operator=(const CallJitterBuffer&);

    //重建 speexdsp 的抖动缓冲并设好参数（调用方已持锁）
    void create();

    //已收到、还没播放的采样数（调用方已持锁）
    int bufferedLocked() const;

private:
    mutable std::mutex         m_mutex;      //保护以下全部成员（收包线程与播放线程共用）
    JitterBuffer_*             m_jb;         //speexdsp 的抖动缓冲（本对象持有，析构时释放）
    bool                       m_started;    //是否已取到过第一个包（此后播放指针才有意义）；重置后回到 false
    bool                       m_hasPut;     //是否放入过包
    uint32_t                   m_lastPutEnd; //最近放入的包的末尾时间戳（采样）
    std::vector<unsigned char> m_scratch;    //取包时交给 speexdsp 拷贝负载的缓冲
    CallJbStats                m_stats;      //累计计数
};

} // namespace call
