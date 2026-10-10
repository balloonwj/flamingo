/**
 *  一对一通话在 P2P 数据报通道上的消息格式与丢包处理（纯逻辑，docs/P2P通信方案.md 第 6 期），CallP2PFraming.h
 *
 *  通话走 P2P 时，音视频改走数据报通道（P2PDatagramChannel）：一条消息就是一个 Opus 包或一整帧 H.264，DLL 按路径
 *  分片，任何一片丢失整条丢弃，不重传。与 imgserver 中转（TCP，不丢不乱）相比要多处理两件事：
 *    - 音频：按序号发现丢了几个包，让 Opus 解码器做同样次数的丢包补偿（PLC），连续丢得太多只补前几个；迟到、
 *      重复的包丢弃（它对应的时段已经补偿过了）。
 *    - 视频：H.264 的非关键帧依赖前面的帧，中间丢了一帧，后面解出来就是花屏。所以序号一断档就丢弃后续帧，直到收到
 *      下一个关键帧，同时请对方立即编一个关键帧（请求限频，等待期间按间隔重发）；会话刚开始还没收到关键帧时同样等。
 *      发送端收到请求后让编码器出一个关键帧，同样限频。
 *
 *  消息格式（数据报通道上的一条消息）：
 *    [1 字节 类型 CallP2PKind][1 字节 标志][4 字节 序号，网络字节序][8 字节 会话相对时间戳（毫秒），网络字节序][负载...]
 *  音频与视频各自从 0 编号；关键帧请求没有负载，序号与时间戳填 0。中转路径仍用 CallMediaFraming 的格式，两者互不相干。
 *
 *  典型用法（CallMediaSession）：
 *      发送：BuildCallP2PMessage(call_p2p_kind_audio, 0, m_audioSeq++, tsMs, opus, len, msg);  datagram.Send(msg)
 *      接收：ParseCallP2PMessage(data, len, m);  m_audioTracker.OnPacket(m.m_seq, lost)  →  补 lost 次 PLC 再解码
 *            m_videoGuard.OnFrame(m.m_seq, keyframe, now, request)  →  解码或丢弃；request 为真时发关键帧请求
 *
 *  纯逻辑，不碰网络与设备，单测见 Source/Tests/CallP2PFramingTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace call {

//消息的类型（消息首字节）
enum CallP2PKind
{
    call_p2p_kind_audio            = 1,   //音频：一个 Opus 包
    call_p2p_kind_video            = 2,   //视频：一整帧 H.264，标志里带是否关键帧
    call_p2p_kind_keyframe_request = 3,   //接收端请对方尽快编一个关键帧：视频帧丢了，或会话开始后还没收到关键帧
    call_p2p_kind_stats            = 4,   //质量报告（2026-10-05，网络状况标识第 2 期；负载格式见 CallNetStats.h），序号为报告序号。
    call_p2p_kind_system_audio     = 5,   //电脑声音（2026-10-06，docs/音视频通话方案.md 第 10 节）：一个 Opus 包，序号与时间戳
                                          //都是这一路自己的
};

//视频消息的标志位：本帧是关键帧（IDR）
const uint8_t kCallP2PFlagKeyframe = 0x01;

//消息头的长度（字节）：类型 1 + 标志 1 + 序号 4 + 时间戳 8
const size_t kCallP2PHeaderBytes = 14;

//音频连续丢包时最多补偿几帧：Opus 一帧 20 毫秒，补 3 帧（60 毫秒）以内听感自然，再长的空缺补出来是拖长的怪音，
//不如直接接上后面的声音
const int kCallMaxConcealFrames = 3;

//等关键帧期间重发关键帧请求的间隔（毫秒）：请求本身也可能丢；对方编一个关键帧加传输一般在 100 毫秒量级
const int64_t kCallKeyframeRequestIntervalMs = 300;

//发送端两次强制关键帧之间的最短间隔（毫秒）：关键帧大，接连强制会把码率冲上去，加剧拥塞造成更多丢包
const int64_t kCallKeyframeForceMinIntervalMs = 500;

//解析出的一条消息（负载指向原缓冲，只在缓冲有效期间可用）
struct CallP2PMessage
{
    int            m_kind;         //类型，取值见 CallP2PKind
    uint8_t        m_flags;        //标志位（视频消息为 kCallP2PFlagKeyframe 等）
    uint32_t       m_seq;          //序号（音频、视频各自编号）
    uint64_t       m_tsMs;         //会话相对时间戳（毫秒）
    const uint8_t* m_payload;      //负载起始；没有负载时为 NULL
    size_t         m_payloadLen;   //负载长度（字节）

    CallP2PMessage()
        : m_kind(0)
        , m_flags(0)
        , m_seq(0)
        , m_tsMs(0)
        , m_payload(NULL)
        , m_payloadLen(0)
    {
    }
};

/**
 *  组一条消息。
 *  @param kind  类型，取值见 CallP2PKind
 *  @param flags 标志位
 *  @param seq   序号
 *  @param tsMs  会话相对时间戳（毫秒）
 *  @param data  负载（len 为 0 时可以为 NULL）
 *  @param len   负载长度（字节）
 *  @param out   [出参] 消息（先清空）
 */
void BuildCallP2PMessage(int kind, uint8_t flags, uint32_t seq, uint64_t tsMs, const uint8_t* data, size_t len,
                         std::vector<uint8_t>& out);

/**
 *  解析一条消息。
 *  @param data 消息
 *  @param len  长度（字节）
 *  @param out  [出参] 解析结果，负载指向 data 内部
 *  @return 长度不足或类型不认识时返回 false
 */
bool ParseCallP2PMessage(const uint8_t* data, size_t len, CallP2PMessage& out);

/**
 *  音频接收端的序号跟踪：判断收到的包之前丢了几个（据此补偿），以及迟到、重复的包。
 *  只在收包的那个线程上使用，不加锁。
 */
class CallAudioSeqTracker
{
public:
    CallAudioSeqTracker();

    //回到初始状态（新的会话）
    void Reset();

    /**
     *  收到一个音频包。
     *  @param seq     包序号
     *  @param lostOut [出参] 本包之前丢了几个，已按 kCallMaxConcealFrames 截断；第一个包为 0
     *  @return true 本包应交给解码器；false 迟到或重复，丢弃（lostOut 为 0）
     */
    bool OnPacket(uint32_t seq, int& lostOut);

private:
    bool     m_started;   //是否已收到过包
    uint32_t m_next;      //期望的下一个序号
};

/**
 *  视频接收端的丢帧处理：序号断档或会话开始时，在收到关键帧之前丢弃非关键帧，并按间隔请求关键帧。
 *  只在收包的那个线程上使用，不加锁。
 */
class CallVideoLossGuard
{
public:
    //OnFrame 的判定
    enum verdict
    {
        video_decode = 1,   //交给解码器
        video_drop   = 2,   //丢弃：在等关键帧，或是迟到、重复的帧
    };

    CallVideoLossGuard();

    //回到初始状态（新的会话，或本端重新开始收视频）：之后先等关键帧
    void Reset();

    /**
     *  收到一帧。
     *  @param seq             帧序号
     *  @param keyframe        是否关键帧
     *  @param nowMs           当前时间（单调时钟毫秒）
     *  @param requestKeyframe [出参] 是否应当现在向对方发一个关键帧请求
     *  @return 判定，取值见 verdict
     */
    int OnFrame(uint32_t seq, bool keyframe, int64_t nowMs, bool& requestKeyframe);

    /**
     *  定时检查：仍在等关键帧、且距上次请求已超过 kCallKeyframeRequestIntervalMs 时返回 true（调用方再发一次请求）。
     *  @param nowMs 当前时间（单调时钟毫秒）
     *  @return 是否应当现在再发一次请求
     */
    bool PollRequest(int64_t nowMs);

    //是否在等关键帧
    bool WaitingKeyframe() const { return m_waitingKey; }

    //累计因等关键帧而丢弃的帧数
    uint64_t DroppedFrames() const { return m_dropped; }

private:
    //需要请求时返回 true 并记下请求时刻
    bool DueRequest(int64_t nowMs);

private:
    bool     m_started;         //是否已收到过帧
    uint32_t m_next;            //期望的下一个序号
    bool     m_waitingKey;      //是否在等关键帧
    bool     m_requested;       //等关键帧期间是否已经请求过
    int64_t  m_lastRequestMs;   //上次请求的时刻（单调时钟毫秒）；m_requested 为 false 时无意义
    uint64_t m_dropped;         //累计丢弃的帧数
};

/**
 *  发送端的强制关键帧限频：收到对方的关键帧请求时，两次强制之间至少隔 kCallKeyframeForceMinIntervalMs。
 *  只在处理请求的那个线程上使用，不加锁。
 */
class CallKeyframeForceLimiter
{
public:
    CallKeyframeForceLimiter();

    //回到初始状态
    void Reset();

    /**
     *  收到一个关键帧请求，判断现在是否强制。
     *  @param nowMs 当前时间（单调时钟毫秒）
     *  @return 允许现在强制返回 true（同时记下时刻）；离上次强制太近返回 false
     */
    bool Allow(int64_t nowMs);

private:
    bool    m_forced;         //是否已经强制过
    int64_t m_lastForcedMs;   //上次强制的时刻；m_forced 为 false 时无意义
};

} // namespace call
