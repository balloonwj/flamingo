/**
 *  屏幕共享房间里本人摄像头的发送端，见 ShareCameraSender.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareCameraSender.h"

#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <stdlib.h>
#include <string.h>

#include "ShareFraming.h"
#include "ShareVideoPolicy.h"
#include "../Call/VideoFrameConvert.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace screenshare {

namespace {

//时间戳换算：一秒的 100 纳秒数、100 纳秒到微秒
const int64_t kHnsPerSecond = 10000000;
const int64_t kHnsPerUs = 10;
//BGRA 每像素字节数
const int kBgraBytes = 4;
//模拟摄像头：画面尺寸、出帧间隔（毫秒，约 15 帧每秒）、彩条宽与每帧移动的距离（像素）
const int kFakeW = 640;
const int kFakeH = 480;
const DWORD kFakeIntervalMs = 66;
const int kFakeBarW = 96;
const int kFakeBarStep = 8;
//模拟画面的颜色（YUV）：底色深灰，彩条偏红
const uint8_t kFakeBgY = 60;
const uint8_t kFakeBarY = 150;
const uint8_t kFakeNeutralUv = 128;
const uint8_t kFakeBarU = 90;
const uint8_t kFakeBarV = 210;
//模拟摄像头的环境变量名
const char* const kFakeCameraEnv = "FLAMINGO_SHARE_FAKE_CAMERA";

#ifdef _DEBUG
//是否使用模拟摄像头：环境变量的值为 1
bool useFakeCamera()
{
    char value[8] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, value, sizeof(value), kFakeCameraEnv) != 0 || len == 0)
        return false;
    return strcmp(value, "1") == 0;
}
#endif

} // namespace

ShareCameraSender::ShareCameraSender()
    : m_transport(NULL)
    , m_seat(0)
    , m_running(false)
    , m_keyRequested(false)
    , m_targetBitrate(kShareCameraBitrateBps)
    , m_appliedBitrate(0)
    , m_encoderTried(false)
    , m_encW(0)
    , m_encH(0)
    , m_frameIndex(0)
    , m_previewW(0)
    , m_previewH(0)
    , m_previewSerial(0)
    , m_fakeStop(false)
{
}

ShareCameraSender::~ShareCameraSender()
{
    Stop();
}

bool ShareCameraSender::Start(ShareAudioTransport* transport, int seat)
{
    if (transport == NULL || m_running.load())
        return false;
    m_transport = transport;
    m_seat = seat;
    m_encoderTried = false;
    m_encW = 0;
    m_encH = 0;
    m_frameIndex = 0;
    m_keyRequested.store(false);
#ifdef _DEBUG
    //联调：不打开真实摄像头，改由模拟线程出画面
    if (useFakeCamera())
    {
        if (!StartWithoutCamera(transport, seat))
            return false;
        m_fakeStop.store(false);
        m_fakeThread.reset(new std::thread(&ShareCameraSender::fakeCameraProc, this));
        LOG_INFO("screenshare camera: fake camera started, seat %d.", seat);
        return true;
    }
#endif
    //先置运行标志：摄像头线程一开始就可能回调
    m_running.store(true);
    int w = 0;
    int h = 0;
    if (!m_capture.Start(this, w, h))
    {
        m_running.store(false);
        LOG_WARNING("screenshare camera: open camera failed, reason=%d.", m_capture.FailReason());
        return false;
    }
    LOG_INFO("screenshare camera: started, camera %dx%d, seat %d.", w, h, seat);
    return true;
}

bool ShareCameraSender::StartLocal()
{
    if (m_running.load())
        return false;
    m_transport = NULL;
    m_seat = 0;
    m_encoderTried = false;
    m_encW = 0;
    m_encH = 0;
    m_frameIndex = 0;
    m_keyRequested.store(false);
    m_running.store(true);
#ifdef _DEBUG
    //联调：不打开真实摄像头，改由模拟线程出画面
    if (useFakeCamera())
    {
        m_fakeStop.store(false);
        m_fakeThread.reset(new std::thread(&ShareCameraSender::fakeCameraProc, this));
        LOG_INFO("screenshare camera: fake camera started for local preview.");
        return true;
    }
#endif
    int w = 0;
    int h = 0;
    if (!m_capture.Start(this, w, h))
    {
        m_running.store(false);
        LOG_WARNING("screenshare camera: open camera for local preview failed, reason=%d.", m_capture.FailReason());
        return false;
    }
    LOG_INFO("screenshare camera: local preview started, camera %dx%d.", w, h);
    return true;
}

bool ShareCameraSender::StartWithoutCamera(ShareAudioTransport* transport, int seat)
{
    if (transport == NULL || m_running.load())
        return false;
    m_transport = transport;
    m_seat = seat;
    m_encoderTried = false;
    m_encW = 0;
    m_encH = 0;
    m_frameIndex = 0;
    m_keyRequested.store(false);
    m_running.store(true);
    return true;
}

void ShareCameraSender::SetTargetBitrate(int bitrateBps)
{
    m_targetBitrate.store(bitrateBps > 0 ? bitrateBps : kShareCameraBitrateBps);
}

int ShareCameraSender::AppliedBitrate() const
{
    return m_appliedBitrate.load();
}

void ShareCameraSender::Stop()
{
    const bool wasRunning = m_running.exchange(false);
    //join 采集线程（或模拟线程）之后编码器不再被并发使用
    m_fakeStop.store(true);
    if (m_fakeThread)
    {
        if (m_fakeThread->joinable())
            m_fakeThread->join();
        m_fakeThread.reset();
    }
    m_capture.Stop();
    m_encoder.Close();
    m_appliedBitrate.store(0);
    m_scaled.clear();
    {
        std::lock_guard<std::mutex> guard(m_previewMutex);
        m_preview.clear();
        m_previewW = 0;
        m_previewH = 0;
        m_previewSerial = 0;
    }
    if (wasRunning)
        LOG_INFO("screenshare camera: stopped, seat %d.", m_seat);
}

bool ShareCameraSender::IsRunning() const
{
    return m_running.load();
}

void ShareCameraSender::RequestKeyFrame()
{
    m_keyRequested.store(true);
}

void ShareCameraSender::FeedFrame(const uint8_t* nv12, int width, int height, int stride)
{
    OnVideoFrame(nv12, width, height, stride);
}

bool ShareCameraSender::GetPreview(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_previewMutex);
    if (m_preview.empty())
        return false;
    bgra = m_preview;
    width = m_previewW;
    height = m_previewH;
    serial = m_previewSerial;
    return true;
}

int ShareCameraSender::FailReason() const
{
    return m_capture.FailReason();
}

void ShareCameraSender::OnVideoFrame(const uint8_t* data, int width, int height, int stride)
{
    //采集线程上下文（无摄像头模式为调用线程）
    if (!m_running.load() || data == NULL || width <= 0 || height <= 0)
        return;
    //编码尺寸按第一帧确定；摄像头分辨率中途不会变
    if (m_encW == 0)
        ComputeCameraEncodeSize(width, height, m_encW, m_encH);
    if (m_encW <= 0 || m_encH <= 0)
        return;
    m_scaled.resize(static_cast<size_t>(m_encW) * m_encH * 3 / 2);
    ScaleNv12(data, stride, width, height, &m_scaled[0], m_encW, m_encH);

    //本人预览（编码尺寸）
    {
        std::vector<uint8_t> bgra(static_cast<size_t>(m_encW) * m_encH * kBgraBytes);
        call::Nv12ToBgra(&m_scaled[0], m_encW, m_encH, m_encW, &bgra[0], m_encW * kBgraBytes);
        std::lock_guard<std::mutex> guard(m_previewMutex);
        m_preview.swap(bgra);
        m_previewW = m_encW;
        m_previewH = m_encH;
        ++m_previewSerial;
    }

    //本地预览模式（录制的画中画）：不编码、不发送
    if (m_transport == NULL)
        return;
    //编码器在本线程上懒创建（Media Foundation 的编码器要在使用它的线程上创建）
    if (!m_encoder.IsReady())
    {
        if (m_encoderTried)
            return;
        m_encoderTried = true;
        const int bitrate = m_targetBitrate.load();
        if (!m_encoder.Init(m_encW, m_encH, kShareCameraFps, bitrate, kShareCameraGopFrames))
        {
            LOG_ERROR("screenshare camera: encoder init failed (%dx%d).", m_encW, m_encH);
            return;
        }
        m_appliedBitrate.store(bitrate);
    }
    //目标码率变了（群通话里开摄像头的人数变化）：交给编码器，失败时保持原码率、下一帧再试
    const int target = m_targetBitrate.load();
    if (target != m_appliedBitrate.load())
    {
        if (m_encoder.SetBitrate(target))
        {
            LOG_INFO("screenshare camera: bitrate %d -> %d bps.", m_appliedBitrate.load(), target);
            m_appliedBitrate.store(target);
        }
    }
    if (m_keyRequested.exchange(false))
        m_encoder.ForceKeyFrame();
    const int64_t pts = m_frameIndex * kHnsPerSecond / kShareCameraFps;
    ++m_frameIndex;
    m_encoder.EncodeNv12(&m_scaled[0], m_encW, pts, this);
}

void ShareCameraSender::fakeCameraProc()
{
    //编码器是 Media Foundation 的，出帧线程须初始化 COM 与 Media Foundation
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    std::vector<uint8_t> frame(static_cast<size_t>(kFakeW) * kFakeH * 3 / 2);
    int barX = 0;
    while (!m_fakeStop.load())
    {
        //Y 平面：深灰底上一条竖向彩条，每帧右移
        for (int y = 0; y < kFakeH; ++y)
        {
            uint8_t* row = &frame[static_cast<size_t>(y) * kFakeW];
            for (int x = 0; x < kFakeW; ++x)
                row[x] = (x >= barX && x < barX + kFakeBarW) ? kFakeBarY : kFakeBgY;
        }
        //UV 平面（交错，半分辨率）
        uint8_t* uv = &frame[static_cast<size_t>(kFakeW) * kFakeH];
        for (int y = 0; y < kFakeH / 2; ++y)
        {
            for (int x = 0; x < kFakeW / 2; ++x)
            {
                const bool inBar = (x * 2 >= barX && x * 2 < barX + kFakeBarW);
                uv[static_cast<size_t>(y) * kFakeW + x * 2] = inBar ? kFakeBarU : kFakeNeutralUv;
                uv[static_cast<size_t>(y) * kFakeW + x * 2 + 1] = inBar ? kFakeBarV : kFakeNeutralUv;
            }
        }
        FeedFrame(&frame[0], kFakeW, kFakeH, kFakeW);
        barX = (barX + kFakeBarStep) % kFakeW;
        ::Sleep(kFakeIntervalMs);
    }
    //编码器在本线程上创建，也在本线程上关闭
    m_encoder.Close();
    m_appliedBitrate.store(0);
    ::MFShutdown();
    ::CoUninitialize();
}

void ShareCameraSender::OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns)
{
    ShareAudioTransport* transport = m_transport;
    if (transport == NULL || data == NULL || len == 0)
        return;
    const std::string payload =
        ShareFraming::buildVideoPayload(m_encW, m_encH, reinterpret_cast<const char*>(data), len);
    std::vector<std::string> bodies;
    ShareFraming::fragmentFrame(screenshare_track_camera, m_seat, keyframe, static_cast<uint64_t>(ptsHns / kHnsPerUs),
                                payload.data(), payload.size(), 0, bodies);
    //同一帧的各分片按顺序发出；摄像头轨只有本线程在发，不会与别的帧交错
    for (size_t i = 0; i < bodies.size(); ++i)
        transport->SendRoomBody(bodies[i]);
}

} // namespace screenshare
