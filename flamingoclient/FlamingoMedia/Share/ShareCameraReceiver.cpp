/**
 *  屏幕共享房间里其他成员摄像头的接收端，见 ShareCameraReceiver.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareCameraReceiver.h"

#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <string.h>

#include "ShareVideoPolicy.h"
#include "../Call/VideoFrameConvert.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace screenshare {

namespace {

//解码队列的上限：超过时丢掉最早的帧（解码跟不上时宁可跳帧，不无限积压）
const size_t kMaxQueuedFrames = 120;
//BGRA 每像素字节数
const int kBgraBytes = 4;

} // namespace

ShareCameraReceiver::SeatDecoder::SeatDecoder()
    : m_owner(NULL)
    , m_seat(0)
    , m_initTried(false)
    , m_cropW(0)
    , m_cropH(0)
{
}

void ShareCameraReceiver::SeatDecoder::OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride,
                                                     int64_t ptsHns)
{
    (void)ptsHns;
    if (m_owner != NULL)
        m_owner->storeFrame(m_seat, nv12, width, height, stride, m_cropW, m_cropH);
}

ShareCameraReceiver::SeatFrame::SeatFrame()
    : m_width(0)
    , m_height(0)
    , m_serial(0)
{
}

ShareCameraReceiver::ShareCameraReceiver()
    : m_stop(false)
{
}

ShareCameraReceiver::~ShareCameraReceiver()
{
    Stop();
}

void ShareCameraReceiver::Start()
{
    if (m_thread)
        return;
    m_stop.store(false);
    m_thread.reset(new std::thread(&ShareCameraReceiver::threadProc, this));
}

void ShareCameraReceiver::Stop()
{
    m_stop.store(true);
    m_queueCv.notify_all();
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        m_queue.clear();
        m_removed.clear();
    }
    std::lock_guard<std::mutex> guard(m_frameMutex);
    m_frames.clear();
    m_overflow.clear();
}

void ShareCameraReceiver::OnRemoteFrame(const ShareMediaFrame& frame)
{
    if (frame.m_track != screenshare_track_camera || !m_thread)
        return;
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        if (m_queue.size() >= kMaxQueuedFrames)
            m_queue.pop_front();
        m_queue.push_back(frame);
    }
    m_queueCv.notify_one();
}

void ShareCameraReceiver::RemoveSeat(int seat)
{
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        m_removed.push_back(seat);
    }
    m_queueCv.notify_one();
    //画面立即取不到；解码器在解码线程上释放
    std::lock_guard<std::mutex> guard(m_frameMutex);
    m_frames.erase(seat);
    m_overflow.erase(seat);
}

bool ShareCameraReceiver::GetFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                   uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_frameMutex);
    std::map<int, SeatFrame>::const_iterator it = m_frames.find(seat);
    if (it == m_frames.end() || it->second.m_bgra.empty())
        return false;
    bgra = it->second.m_bgra;
    width = it->second.m_width;
    height = it->second.m_height;
    serial = it->second.m_serial;
    return true;
}

std::vector<int> ShareCameraReceiver::Seats() const
{
    std::lock_guard<std::mutex> guard(m_frameMutex);
    std::vector<int> seats;
    for (std::map<int, SeatFrame>::const_iterator it = m_frames.begin(); it != m_frames.end(); ++it)
        seats.push_back(it->first);
    return seats;
}

std::vector<int> ShareCameraReceiver::OverflowSeats() const
{
    std::lock_guard<std::mutex> guard(m_frameMutex);
    std::vector<int> seats;
    for (std::map<int, bool>::const_iterator it = m_overflow.begin(); it != m_overflow.end(); ++it)
        seats.push_back(it->first);
    return seats;
}

size_t ShareCameraReceiver::PendingFrames() const
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    return m_queue.size();
}

void ShareCameraReceiver::threadProc()
{
    //解码器在本线程上创建与使用，本线程要初始化 COM 与 Media Foundation
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    while (true)
    {
        ShareMediaFrame frame;
        bool haveFrame = false;
        std::vector<int> removed;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            while (!m_stop.load() && m_queue.empty() && m_removed.empty())
                m_queueCv.wait(lock);
            if (m_stop.load())
                break;
            removed.swap(m_removed);
            if (!m_queue.empty())
            {
                frame = m_queue.front();
                m_queue.pop_front();
                haveFrame = true;
            }
        }
        for (size_t i = 0; i < removed.size(); ++i)
            m_decoders.erase(removed[i]);
        if (haveFrame)
            decode(frame);
    }
    //解码器在创建它的线程上释放
    m_decoders.clear();
    ::MFShutdown();
    ::CoUninitialize();
}

void ShareCameraReceiver::decode(const ShareMediaFrame& frame)
{
    int w = 0;
    int h = 0;
    const char* h264 = NULL;
    size_t len = 0;
    if (!ShareFraming::parseVideoPayload(frame.m_data, w, h, h264, len))
        return;
    std::map<int, std::unique_ptr<SeatDecoder> >::iterator it = m_decoders.find(frame.m_senderSeat);
    if (it == m_decoders.end())
    {
        //同屏最多解码 9 路：满了的话这个人只记下来，不解码
        if (m_decoders.size() >= static_cast<size_t>(kShareMaxCameraTiles))
        {
            std::lock_guard<std::mutex> guard(m_frameMutex);
            m_overflow[frame.m_senderSeat] = true;
            return;
        }
        //新的一路从关键帧开始解码（中途加入的非关键帧解不出来）
        if (!frame.m_keyframe)
            return;
        std::unique_ptr<SeatDecoder> d(new SeatDecoder());
        d->m_owner = this;
        d->m_seat = frame.m_senderSeat;
        it = m_decoders.insert(std::make_pair(frame.m_senderSeat, std::move(d))).first;
        std::lock_guard<std::mutex> guard(m_frameMutex);
        m_overflow.erase(frame.m_senderSeat);
        //先登记一个空画面，Seats 立即能看到这一路
        m_frames[frame.m_senderSeat];
    }
    SeatDecoder& d = *it->second;
    if (!d.m_decoder.IsReady())
    {
        if (d.m_initTried)
            return;
        d.m_initTried = true;
        if (!d.m_decoder.Init(w, h))
        {
            LOG_ERROR("screenshare camera: decoder init failed for seat %d.", frame.m_senderSeat);
            return;
        }
    }
    d.m_cropW = w;
    d.m_cropH = h;
    d.m_decoder.DecodePacket(reinterpret_cast<const uint8_t*>(h264), len, static_cast<int64_t>(frame.m_timestamp),
                             &d);
}

void ShareCameraReceiver::storeFrame(int seat, const uint8_t* nv12, int width, int height, int stride, int cropW,
                                     int cropH)
{
    if (nv12 == NULL || width <= 0 || height <= 0)
        return;
    //解码器输出按 16 对齐（如 360 行解成 368 行），按载荷里的真实宽高裁掉补齐的部分
    const int outW = (cropW > 0 && cropW <= width) ? cropW : width;
    const int outH = (cropH > 0 && cropH <= height) ? cropH : height;
    m_convert.resize(static_cast<size_t>(width) * height * kBgraBytes);
    call::Nv12ToBgra(nv12, width, height, stride, &m_convert[0], width * kBgraBytes);
    std::vector<uint8_t> bgra(static_cast<size_t>(outW) * outH * kBgraBytes);
    for (int y = 0; y < outH; ++y)
        memcpy(&bgra[static_cast<size_t>(y) * outW * kBgraBytes], &m_convert[static_cast<size_t>(y) * width * kBgraBytes],
               static_cast<size_t>(outW) * kBgraBytes);
    std::lock_guard<std::mutex> guard(m_frameMutex);
    //已被删除的座位（解码线程处理删除之前又解出了一帧）不再登记
    std::map<int, SeatFrame>::iterator it = m_frames.find(seat);
    if (it == m_frames.end())
        return;
    it->second.m_bgra.swap(bgra);
    it->second.m_width = outW;
    it->second.m_height = outH;
    ++it->second.m_serial;
}

} // namespace screenshare
