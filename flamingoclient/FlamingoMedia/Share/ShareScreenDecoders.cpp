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
    {
    }

    //解一帧（接收线程）
    void Decode(int w, int h, const char* h264, size_t len, uint64_t timestamp)
    {
        m_cropW = w;
        m_cropH = h;
        if (!m_decoder.IsReady() && !m_decoder.Init(kInitialDecodeW, kInitialDecodeH))
        {
            LOG_ERROR("screenshare screens: decoder init failed, seat=%d.", m_seat);
            return;
        }
        m_decoder.DecodePacket(reinterpret_cast<const uint8_t*>(h264), len, static_cast<int64_t>(timestamp), this);
    }

    //MfH264DecoderSink：解出一帧 NV12，转成 BGRA、裁掉解码补齐的部分后存为最新一帧
    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override
    {
        (void)ptsHns;
        const int w = (m_cropW > 0 && m_cropW <= width) ? m_cropW : width;
        const int h = (m_cropH > 0 && m_cropH <= height) ? m_cropH : height;
        m_convert.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
        call::Nv12ToBgra(nv12, width, height, stride, &m_convert[0], width * kBytesPerPixel);
        {
            std::lock_guard<std::mutex> guard(m_owner->m_mutex);
            m_latest.resize(static_cast<size_t>(w) * h * kBytesPerPixel);
            const size_t rowBytes = static_cast<size_t>(w) * kBytesPerPixel;
            for (int y = 0; y < h; ++y)
                memcpy(&m_latest[rowBytes * y], &m_convert[static_cast<size_t>(y) * width * kBytesPerPixel], rowBytes);
            m_latestW = w;
            m_latestH = h;
            ++m_serial;
        }
        m_owner->m_decoded.fetch_add(1);
        if (m_owner->m_listener != NULL)
            m_owner->m_listener->OnScreenDecoded(m_seat);
    }

    ShareScreenDecoders*  m_owner;         //所属的多路解码（不拥有）
    int                   m_seat;          //发送者座位
    call::MfH264Decoder   m_decoder;       //这一路的解码器（只在接收线程上用）
    int                   m_cropW;         //当前帧的真实宽（载荷里带的）
    int                   m_cropH;         //当前帧的真实高
    std::vector<uint8_t>  m_convert;       //NV12 转出的整幅 BGRA（含补齐部分，只在接收线程上用）
    std::vector<uint8_t>  m_latest;        //最新一帧（BGRA，已裁剪；受 owner 的互斥量保护）
    int                   m_latestW;       //最新一帧的宽
    int                   m_latestH;       //最新一帧的高
    uint64_t              m_serial;        //最新一帧的序号
    int                   m_sourceState;   //共享内容状态（受 owner 的互斥量保护）
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
    stream->Decode(w, h, h264, h264Len, frame.m_timestamp);
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

} // namespace screenshare
