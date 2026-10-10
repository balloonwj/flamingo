/**
 *  摄像头采集（Media Foundation Source Reader）实现，MfCameraCapture.cpp
 *  balloonwj@qq.com   2026-06-15
 *
 *  线程模型：采集线程内完成 MFStartup + COM(MTA) 初始化、打开默认摄像头、循环同步
 *  ReadSample 读 NV12 帧回调上层；Start 阻塞等线程报告初始化结果(含宽高)。不碰 UI 线程套间。
 */
#include "MfCameraCapture.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

#include <thread>
#include <atomic>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace call {

struct MfCameraCapture::Impl
{
    std::thread          thread;       //采集线程
    std::atomic<bool>    stopping;     //是否正在停止
    HANDLE               initDone;     //初始化完成事件(Start 等它)
    bool                 initOk;       //初始化是否成功(initDone 触发后有效)
    int                  width;        //协商到的画面宽
    int                  height;       //协商到的画面高
    MfVideoFrameHandler* handler;      //采集回调(不拥有)
    std::atomic<int>     failReason;   //最近一次失败原因,取 CameraFailReason(采集线程写、调用方线程读)

    Impl()
        : stopping(false)
        , initDone(NULL)
        , initOk(false)
        , width(0)
        , height(0)
        , handler(NULL)
        , failReason(kCameraFailNone)
    {
    }
};

MfCameraCapture::MfCameraCapture()
    : m_impl(new Impl())
{
}

MfCameraCapture::~MfCameraCapture()
{
    Stop();
    if (m_impl->initDone != NULL)
        ::CloseHandle(m_impl->initDone);
    delete m_impl;
}

bool MfCameraCapture::Start(MfVideoFrameHandler* handler, int& outWidth, int& outHeight)
{
    if (handler == NULL)
        return false;

    m_impl->handler = handler;
    m_impl->stopping.store(false);
    m_impl->initOk = false;
    m_impl->failReason.store(kCameraFailNone);
    if (m_impl->initDone == NULL)
        m_impl->initDone = ::CreateEvent(NULL, TRUE, FALSE, NULL);   //手动复位
    else
        ::ResetEvent(m_impl->initDone);

    m_impl->thread = std::thread(&MfCameraCapture::CaptureProc, this);

    ::WaitForSingleObject(m_impl->initDone, INFINITE);
    if (!m_impl->initOk)
    {
        if (m_impl->thread.joinable())
            m_impl->thread.join();
        return false;
    }
    outWidth = m_impl->width;
    outHeight = m_impl->height;
    return true;
}

void MfCameraCapture::Stop()
{
    m_impl->stopping.store(true);
    if (m_impl->thread.joinable())
        m_impl->thread.join();
}

int MfCameraCapture::FailReason() const
{
    return m_impl->failReason.load();
}

void MfCameraCapture::CaptureProc()
{
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hr);
    bool mfInited = false;

    IMFAttributes*  attrs = NULL;
    IMFActivate**   devices = NULL;
    UINT32          deviceCount = 0;
    IMFMediaSource* source = NULL;
    IMFSourceReader* reader = NULL;
    IMFMediaType*   wantType = NULL;
    IMFMediaType*   actualType = NULL;
    int             width = 0;
    int             height = 0;
    int             stride = 0;
    bool            ok = false;
    bool            streamBroken = false;   //采集是否因读帧失败 / 断流而中断（区别于本端主动 Stop）

    do
    {
        //初始化阶段的失败先按"打不开摄像头"记；枚举不到设备时下面会改成 kCameraFailNoDevice
        m_impl->failReason.store(kCameraFailOpen);

        hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr))
        {
            LOG_ERROR("MfCameraCapture: MFStartup failed, hr=0x%08x.", hr);
            break;
        }
        mfInited = true;

        //枚举视频采集设备，取第一个（默认摄像头）
        hr = ::MFCreateAttributes(&attrs, 1);
        if (FAILED(hr))
            break;
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        hr = ::MFEnumDeviceSources(attrs, &devices, &deviceCount);
        if (FAILED(hr) || deviceCount == 0)
        {
            //机器上压根没有可用摄像头（没接 / 被禁用 / 隐私设置不允许访问）
            m_impl->failReason.store(kCameraFailNoDevice);
            LOG_ERROR("MfCameraCapture: no video capture device (count=%u, hr=0x%08x).", deviceCount, hr);
            break;
        }
        hr = devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        if (FAILED(hr) || source == NULL)
        {
            LOG_ERROR("MfCameraCapture: activate camera device failed, hr=0x%08x.", hr);
            break;
        }

        //从媒体源建 Source Reader
        hr = ::MFCreateSourceReaderFromMediaSource(source, NULL, &reader);
        if (FAILED(hr) || reader == NULL)
        {
            LOG_ERROR("MfCameraCapture: create source reader failed, hr=0x%08x.", hr);
            break;
        }

        //请求输出 NV12（MF 自动在内部插入 设备原始格式→NV12 的转换器）
        hr = ::MFCreateMediaType(&wantType);
        if (FAILED(hr))
            break;
        wantType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        wantType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, wantType);
        if (FAILED(hr))
        {
            LOG_ERROR("MfCameraCapture: set NV12 output failed, hr=0x%08x.", hr);
            break;
        }

        //读回协商后的实际类型，取宽高 / 行跨度
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
        if (FAILED(hr) || actualType == NULL)
            break;
        UINT32 w = 0;
        UINT32 h = 0;
        ::MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &w, &h);
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        UINT32 defStride = 0;
        if (FAILED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, &defStride)) || static_cast<int>(defStride) <= 0)
            stride = width;   //取不到默认跨度则按宽度（NV12 连续缓冲常态）
        else
            stride = static_cast<int>(defStride);

        if (width <= 0 || height <= 0)
        {
            LOG_ERROR("MfCameraCapture: invalid negotiated size %dx%d.", width, height);
            break;
        }
        ok = true;
        m_impl->failReason.store(kCameraFailNone);   //初始化全程走通，清掉预置的失败原因
    } while (false);

    //报告初始化结果给 Start
    m_impl->initOk = ok;
    m_impl->width = width;
    m_impl->height = height;
    if (ok)
        LOG_INFO("MfCameraCapture: started, %dx%d NV12, stride=%d.", width, height, stride);
    ::SetEvent(m_impl->initDone);

    if (ok)
    {
        while (!m_impl->stopping.load())
        {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            IMFSample* sample = NULL;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                    &streamIndex, &flags, &timestamp, &sample);
            if (FAILED(hr))
            {
                //读帧失败（典型 hr=0xC00D3704 MF_E_HW_MFT_FAILED_START_STREAMING：摄像头已被
                //其它程序占用）。这类失败一次就不再恢复，采集线程随后退出，故上报给上层提示用户。
                LOG_ERROR("MfCameraCapture: ReadSample failed, hr=0x%08x.", hr);
                streamBroken = true;
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            {
                //设备断流（如摄像头被拔出）：同样视为采集意外中断
                LOG_ERROR("MfCameraCapture: capture stream ended unexpectedly.");
                streamBroken = true;
                if (sample != NULL)
                    sample->Release();
                break;
            }
            if (sample == NULL)
                continue;   //无数据（如格式变更通知）——继续

            //取连续缓冲，锁定后把 NV12 交给上层
            IMFMediaBuffer* buf = NULL;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf != NULL)
            {
                BYTE*  data = NULL;
                DWORD  maxLen = 0;
                DWORD  curLen = 0;
                if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen)) && data != NULL)
                {
                    if (m_impl->handler != NULL)
                        m_impl->handler->OnVideoFrame(data, width, height, stride);
                    buf->Unlock();
                }
                buf->Release();
            }
            sample->Release();
        }
    }

    //采集中途断掉（非本端主动 Stop）：记下原因并回调上层，让界面能提示"摄像头不可用"。
    //本回调在采集线程上下文，实现方须自己转投 UI 线程。
    if (streamBroken && !m_impl->stopping.load())
    {
        m_impl->failReason.store(kCameraFailStream);
        if (m_impl->handler != NULL)
            m_impl->handler->OnVideoCaptureStopped(kCameraFailStream);
    }

    //清理（均在本线程创建）
    if (actualType != NULL)
        actualType->Release();
    if (wantType != NULL)
        wantType->Release();
    if (reader != NULL)
        reader->Release();
    if (source != NULL)
    {
        source->Shutdown();
        source->Release();
    }
    if (devices != NULL)
    {
        for (UINT32 i = 0; i < deviceCount; ++i)
        {
            if (devices[i] != NULL)
                devices[i]->Release();
        }
        ::CoTaskMemFree(devices);
    }
    if (attrs != NULL)
        attrs->Release();
    if (mfInited)
        ::MFShutdown();
    if (comInited)
        ::CoUninitialize();
}

} // namespace call
