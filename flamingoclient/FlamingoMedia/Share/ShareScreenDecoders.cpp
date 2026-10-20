/**
 *  多路屏幕画面的解码，见 ShareScreenDecoders.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareScreenDecoders.h"

#include <windows.h>   // IULog.h 用到 PCSTR 等类型（本文件不经 stdafx.h）
#include <string.h>
#include "../Call/MfH264Codec.h"
#include "../Call/VideoFrameConvert.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"

namespace screenshare {

namespace {

//每像素字节数（BGRA）
const int kBytesPerPixel = 4;
//解码器的初始尺寸：按 1080p 建，解码时跟随码流里的真实尺寸
const int kInitialDecodeW = 1920;
const int kInitialDecodeH = 1080;

} // namespace

/**
 *  一路屏幕画面的解码状态：解码器、真实宽高、最新一帧与共享内容状态。解码在接收线程上进行（同一路的帧总在同一个线程上到达），
 *  最新一帧经 owner 的互斥量保护后交给 UI 线程。
 */
class ShareScreenDecoders::Stream : public call::MfH264DecoderSink
{
public:
    Stream(ShareScreenDecoders* owner, int seat)
        : m_owner(owner)
        , m_seat(seat)
        , m_cropW(0)
        , m_cropH(0)
        , m_latestW(0)
        , m_latestH(0)
        , m_serial(0)
        , m_sourceState(screenshare_source_sharing)
        , m_keysWithoutOutput(0)
        , m_undecodable(false)
    {
    }

    //解一帧（接收线程）
    void Decode(int w, int h, const char* h264, size_t len, uint64_t timestamp, bool keyframe)
    {
        m_cropW = w;
        m_cropH = h;
        if (keyframe)
            ++m_keysWithoutOutput;
        if (!m_decoder.IsReady() && !m_decoder.Init(kInitialDecodeW, kInitialDecodeH))
            LOG_ERROR("screenshare screens: decoder init failed, seat=%d.", m_seat);
        else
            m_decoder.DecodePacket(reinterpret_cast<const uint8_t*>(h264), len, static_cast<int64_t>(timestamp), this);
        //解完这一包仍没解出画面：大尺寸的码流连续几个关键帧都解不出，多半是本机系统的解码器不支持这个分辨率（Win7）
        if (m_keysWithoutOutput > 0 && !m_undecodable && ShareLooksUndecodable(w, h, m_keysWithoutOutput))
        {
            {
                std::lock_guard<std::mutex> guard(m_owner->m_mutex);
                m_undecodable = true;
            }
            LOG_WARNING("screenshare screens: %dx%d stream not decodable after %d keyframes, seat=%d.", w, h,
                        m_keysWithoutOutput, m_seat);
            if (m_owner->m_listener != NULL)
                m_owner->m_listener->OnScreenUndecodable(m_seat, true);
        }
    }

    //MfH264DecoderSink：解出一帧 NV12，按载荷里的真实尺寸转成 BGRA（裁掉解码补齐的部分）后存为最新一帧。
    //2026-10-15 起直接转换裁剪后的区域、写进备用缓冲，在锁内与最新一帧交换：原来先转整幅再逐行拷贝，每帧多一次整帧拷贝
    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override
    {
        (void)ptsHns;
        const int w = (m_cropW > 0 && m_cropW <= width) ? m_cropW : width;
        const int h = (m_cropH > 0 && m_cropH <= height) ? m_cropH : height;
        m_staging.resize(static_cast<size_t>(w) * h * kBytesPerPixel);
        call::Nv12ToBgraCropped(nv12, height, stride, w, h, &m_staging[0], w * kBytesPerPixel);
        {
            std::lock_guard<std::mutex> guard(m_owner->m_mutex);
            //交换后备用缓冲里是上一帧的内存，下一帧直接复用，不再分配
            m_latest.swap(m_staging);
            m_latestW = w;
            m_latestH = h;
            ++m_serial;
        }
        m_owner->m_decoded.fetch_add(1);
        //解出了画面：关键帧计数从头算；之前判为超出解码能力的（例如对方改小了分辨率），判定清除
        m_keysWithoutOutput = 0;
        bool cleared = false;
        {
            std::lock_guard<std::mutex> guard(m_owner->m_mutex);
            cleared = m_undecodable;
            m_undecodable = false;
        }
        if (cleared)
        {
            LOG_INFO("screenshare screens: stream decodable again, seat=%d.", m_seat);
            if (m_owner->m_listener != NULL)
                m_owner->m_listener->OnScreenUndecodable(m_seat, false);
        }
        if (m_owner->m_listener != NULL)
            m_owner->m_listener->OnScreenDecoded(m_seat);
    }

    ShareScreenDecoders*  m_owner;         //所属的多路解码（不拥有）
    int                   m_seat;          //发送者座位
    call::MfH264Decoder   m_decoder;       //这一路的解码器（只在接收线程上用）
    int                   m_cropW;         //当前帧的真实宽（载荷里带的）
    int                   m_cropH;         //当前帧的真实高
    std::vector<uint8_t>  m_staging;       //备用缓冲：转换出的下一帧 BGRA，转好后与 m_latest 交换（只在接收线程上用）
    std::vector<uint8_t>  m_latest;        //最新一帧（BGRA，已裁剪；受 owner 的互斥量保护）
    int                   m_latestW;       //最新一帧的宽
    int                   m_latestH;       //最新一帧的高
    uint64_t              m_serial;        //最新一帧的序号
    int                   m_sourceState;   //共享内容状态（受 owner 的互斥量保护）
    int                   m_keysWithoutOutput;   //自上次解出画面以来收到的关键帧个数（只在接收线程上用）
    bool                  m_undecodable;   //是否判为超出本机解码能力（接收线程写，受 owner 的互斥量保护）
};

ShareScreenDecoders::ShareScreenDecoders()
    : m_listener(NULL)
    , m_decoded(0)
{
}

ShareScreenDecoders::~ShareScreenDecoders()
{
    Clear();
}

void ShareScreenDecoders::SetListener(ShareScreenDecodersListener* listener)
{
    m_listener = listener;
}

void ShareScreenDecoders::OnScreenFrame(const ShareMediaFrame& frame)
{
    if (frame.m_track != screenshare_track_screen)
        return;
    int w = 0;
    int h = 0;
    const char* h264 = NULL;
    size_t h264Len = 0;
    if (!ShareFraming::parseVideoPayload(frame.m_data, w, h, h264, h264Len))
        return;
    //取这一路（没有就新建）；解码在锁外做，同一路的帧总在同一个接收线程上到达
    std::shared_ptr<Stream> stream;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<int, std::shared_ptr<Stream> >::iterator it = m_streams.find(frame.m_senderSeat);
        if (it == m_streams.end())
        {
            stream.reset(new Stream(this, frame.m_senderSeat));
            m_streams[frame.m_senderSeat] = stream;
            LOG_INFO("screenshare screens: new screen stream, seat=%d.", frame.m_senderSeat);
        }
        else
        {
            stream = it->second;
        }
    }
    stream->Decode(w, h, h264, h264Len, frame.m_timestamp, frame.m_keyframe);
}

void ShareScreenDecoders::SetSourceState(int seat, int state)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::iterator it = m_streams.find(seat);
    if (it == m_streams.end())
    {
        std::shared_ptr<Stream> stream(new Stream(this, seat));
        it = m_streams.insert(std::make_pair(seat, stream)).first;
    }
    it->second->m_sourceState = state;
}

int ShareScreenDecoders::SourceState(int seat) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(seat);
    return (it == m_streams.end()) ? static_cast<int>(screenshare_source_sharing) : it->second->m_sourceState;
}

bool ShareScreenDecoders::GetLatestFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                         uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(seat);
    if (it == m_streams.end() || it->second->m_latest.empty())
        return false;
    bgra = it->second->m_latest;
    width = it->second->m_latestW;
    height = it->second->m_latestH;
    serial = it->second->m_serial;
    return true;
}

bool ShareScreenDecoders::LatestSerial(int seat, uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(seat);
    if (it == m_streams.end() || it->second->m_latest.empty())
        return false;
    serial = it->second->m_serial;
    return true;
}

std::vector<int> ShareScreenDecoders::Seats() const
{
    std::vector<int> out;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.begin(); it != m_streams.end(); ++it)
    {
        if (!it->second->m_latest.empty())
            out.push_back(it->first);
    }
    return out;
}

void ShareScreenDecoders::Remove(int seat)
{
    //在锁外销毁解码器：析构可能较慢，且不会与同一路的解码并发（调用方在这一路停止发送之后才移除）
    std::shared_ptr<Stream> stream;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<int, std::shared_ptr<Stream> >::iterator it = m_streams.find(seat);
        if (it == m_streams.end())
            return;
        stream = it->second;
        m_streams.erase(it);
    }
    LOG_INFO("screenshare screens: screen stream removed, seat=%d.", seat);
}

void ShareScreenDecoders::Clear()
{
    std::map<int, std::shared_ptr<Stream> > old;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        old.swap(m_streams);
    }
}

uint64_t ShareScreenDecoders::DecodedCount() const
{
    return m_decoded.load();
}

bool ShareScreenDecoders::IsUndecodable(int seat) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(seat);
    return it != m_streams.end() && it->second->m_undecodable;
}

bool ShareScreenDecoders::AnyUndecodable() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.begin(); it != m_streams.end(); ++it)
    {
        if (it->second->m_undecodable)
            return true;
    }
    return false;
}

bool ShareLooksUndecodable(int frameW, int frameH, int keyframesWithoutOutput)
{
    if (frameW <= 0 || frameH <= 0)
        return false;
    const bool tooLarge = (frameW > kShareLegacyDecodeMaxW || frameH > kShareLegacyDecodeMaxH);
    return tooLarge && keyframesWithoutOutput >= kShareUndecodableKeyframes;
}

} // namespace screenshare
