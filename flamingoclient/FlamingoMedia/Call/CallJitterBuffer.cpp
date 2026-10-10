/**
 *  通话声音的抖动缓冲（包装 speexdsp 的 JitterBuffer）的实现，CallJitterBuffer.cpp
 *  说明见 CallJitterBuffer.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CallJitterBuffer.h"

#include <speex/speex_jitter.h>

namespace call {

namespace {

//取包缓冲的大小（字节）：一个 20 毫秒的 Opus 包最大 1275 字节，留足余量
const size_t kScratchBytes = 4000;
//时间线重置的门槛（采样）：缓冲已空时，新包的时间戳比播放指针落后超过它（speexdsp 会把它当作迟到太久丢掉：
//落后超过「包跨度 + 延迟步长」即丢，这里两者都是一帧），或超前超过 kResyncAheadFrames 帧，就先重置再放入
const int32_t kResyncBehindSamples = 2 * kCallJbFrameSamples;
const int kResyncAheadFrames = 10;
const int32_t kResyncAheadSamples = kResyncAheadFrames * kCallJbFrameSamples;

//speexdsp 的延迟自适应参数（2026-10-06 方案 B 第 B5 步按弱网校准选定，数据见 docs/音视频通话方案.md 第 9.9.9 节）：
//允许迟到的包占的比例（百分比）。speexdsp 默认 4：网络平稳时几乎不留缓冲，代理与中转带来的轻微不均匀就造成 1%～3%
//的断续（实测单向延迟 60 毫秒时卡顿 17‰）；取 1 时同样条件下卡顿降到约 1‰，代价是多留约 20 毫秒缓冲；取 2 居中
//（12‰）。故取 1
const spx_int32_t kMaxLateRatePercent = 1;
//固定的安全余量（采样）：在 speexdsp 算出的最优延迟之上再多留这么多。取 1% 的迟到比例后已不需要，取 0
const spx_int32_t kMarginSamples = 0;

//32 位时间戳按回绕比较：a − b（有符号）
int32_t TsDiff(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b);
}

} // namespace

CallJbFrame::CallJbFrame()
    : m_result(call_jb_missing)
    , m_span(kCallJbFrameSamples)
    , m_timestamp(0)
{
}

CallJbStats::CallJbStats()
    : m_puts(0)
    , m_packets(0)
    , m_missing(0)
    , m_inserts(0)
    , m_lateDropped(0)
    , m_resets(0)
    , m_bufferedSum(0)
    , m_bufferedCount(0)
{
}

CallJitterBuffer::CallJitterBuffer()
    : m_jb(NULL)
    , m_started(false)
    , m_hasPut(false)
    , m_lastPutEnd(0)
    , m_scratch(kScratchBytes)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    create();
}

CallJitterBuffer::~CallJitterBuffer()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_jb != NULL)
        jitter_buffer_destroy(m_jb);
    m_jb = NULL;
}

void CallJitterBuffer::create()
{
    if (m_jb != NULL)
        jitter_buffer_destroy(m_jb);
    //步长一帧；丢包补偿的单位也是一帧（缺失时按整帧交给调用方补）
    m_jb = jitter_buffer_init(kCallJbFrameSamples);
    if (m_jb != NULL)
    {
        spx_int32_t concealment = kCallJbFrameSamples;
        jitter_buffer_ctl(m_jb, JITTER_BUFFER_SET_CONCEALMENT_SIZE, &concealment);
        spx_int32_t lateRate = kMaxLateRatePercent;
        jitter_buffer_ctl(m_jb, JITTER_BUFFER_SET_MAX_LATE_RATE, &lateRate);
        spx_int32_t margin = kMarginSamples;
        jitter_buffer_ctl(m_jb, JITTER_BUFFER_SET_MARGIN, &margin);
    }
    m_started = false;
    m_hasPut = false;
    m_lastPutEnd = 0;
}

void CallJitterBuffer::Reset()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    create();
    m_stats = CallJbStats();
}

void CallJitterBuffer::Put(const unsigned char* data, size_t len, uint32_t tsSamples)
{
    if (data == NULL || len == 0)
        return;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_jb == NULL)
        return;
    ++m_stats.m_puts;

    if (m_started)
    {
        spx_int32_t available = 0;
        jitter_buffer_ctl(m_jb, JITTER_BUFFER_GET_AVAILABLE_COUNT, &available);
        const int32_t diff = TsDiff(tsSamples, jitter_buffer_get_pointer_timestamp(m_jb));
        if (available == 0 && (diff < -kResyncBehindSamples || diff > kResyncAheadSamples))
        {
            //缓冲已空、新包的时间戳明显偏离播放指针：发送端的时间线变了（见文件头），重置后从这个包重新开始
            create();
            ++m_stats.m_resets;
        }
        else if (diff < -kResyncBehindSamples)
        {
            //缓冲里还有包时的迟到包：speexdsp 会丢弃它，这里只计数
            ++m_stats.m_lateDropped;
        }
    }

    JitterBufferPacket packet;
    packet.data = reinterpret_cast<char*>(const_cast<unsigned char*>(data));
    packet.len = static_cast<spx_uint32_t>(len);
    packet.timestamp = tsSamples;
    packet.span = kCallJbFrameSamples;
    packet.sequence = 0;
    packet.user_data = 0;
    jitter_buffer_put(m_jb, &packet);
    m_hasPut = true;
    m_lastPutEnd = tsSamples + static_cast<uint32_t>(kCallJbFrameSamples);
}

void CallJitterBuffer::GetFrame(CallJbFrame& frame)
{
    frame.m_result = call_jb_missing;
    frame.m_span = kCallJbFrameSamples;
    frame.m_timestamp = 0;
    frame.m_data.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_jb == NULL)
        return;
    JitterBufferPacket packet;
    packet.data = reinterpret_cast<char*>(&m_scratch[0]);
    packet.len = static_cast<spx_uint32_t>(m_scratch.size());
    packet.timestamp = 0;
    packet.span = 0;
    packet.sequence = 0;
    packet.user_data = 0;
    const int r = jitter_buffer_get(m_jb, &packet, kCallJbFrameSamples, NULL);
    switch (r)
    {
    //取到一个包
    case JITTER_BUFFER_OK:
        frame.m_result = call_jb_packet;
        frame.m_span = (packet.span > 0) ? static_cast<int>(packet.span) : kCallJbFrameSamples;
        frame.m_timestamp = packet.timestamp;
        frame.m_data.assign(m_scratch.begin(), m_scratch.begin() + packet.len);
        m_started = true;
        ++m_stats.m_packets;
        break;

    //speexdsp 要加大缓冲：补一段，播放指针不前进
    case JITTER_BUFFER_INSERTION:
        frame.m_result = call_jb_insert;
        frame.m_span = (packet.span > 0) ? static_cast<int>(packet.span) : kCallJbFrameSamples;
        ++m_stats.m_inserts;
        break;

    //缺失；刚开始或重置之后还没收到包时 speexdsp 给的跨度为 0，按一帧处理
    case JITTER_BUFFER_MISSING:
        frame.m_span = (packet.span > 0) ? static_cast<int>(packet.span) : kCallJbFrameSamples;
        ++m_stats.m_missing;
        break;

    //内部错误或参数错误：按一帧缺失处理
    default:
        ++m_stats.m_missing;
        break;
    }
    jitter_buffer_tick(m_jb);
    //正常播放时量一次缓冲深度：只在取到包时量，对方没在说话、缓冲空着的时段不计入平均值
    if (frame.m_result == call_jb_packet)
    {
        m_stats.m_bufferedSum += static_cast<uint64_t>(bufferedLocked());
        ++m_stats.m_bufferedCount;
    }
}

int CallJitterBuffer::bufferedLocked() const
{
    if (m_jb == NULL || !m_started || !m_hasPut)
        return 0;
    const int32_t d = TsDiff(m_lastPutEnd, jitter_buffer_get_pointer_timestamp(m_jb));
    return (d > 0) ? static_cast<int>(d) : 0;
}

int CallJitterBuffer::BufferedSamples() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return bufferedLocked();
}

CallJbStats CallJitterBuffer::Stats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

} // namespace call
