/**
 *  WASAPI 音频采集与播放实现，WasapiAudioIo.cpp
 *  balloonwj@qq.com   2026-06-12
 *
 *  线程模型：采集 / 播放各自在自己的工作线程里完成全部 COM 初始化（线程内 CoInitializeEx
 *  为 MTA），Start 阻塞等工作线程报告初始化结果后返回；不触碰调用方(UI 线程)的 COM 套间，
 *  避免与 MFC 的 STA 冲突。所有 WASAPI 对象都在各自工作线程内创建、使用、释放。
 */
#include "WasapiAudioIo.h"
#include "AudioPcmUtils.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后：IULog.h 用到 PCTSTR/PCSTR 等 Windows 类型

#include <thread>
#include <atomic>
#include <vector>
#include <string.h>

#pragma comment(lib, "ole32.lib")

namespace call {

//单次等待设备事件的超时（毫秒）：周期性返回以检查停止标志，避免 Stop 时久等。
static const DWORD kWaitEventMs = 200;

//设备 PCM 格式要点（从 WAVEFORMATEX 解析出的、转换需要的字段）。
struct AudioFmt
{
    int  channels;     //声道数
    int  sampleRate;   //采样率(Hz)
    int  bits;         //每样本位数
    bool isFloat;      //样本是否为 IEEE 浮点(否则按 PCM 整型)
};

//从 WAVEFORMATEX(可能是 WAVEFORMATEXTENSIBLE)解析格式要点。
static AudioFmt parseFormat(const WAVEFORMATEX* wfx)
{
    AudioFmt f;
    f.channels = wfx->nChannels;
    f.sampleRate = static_cast<int>(wfx->nSamplesPerSec);
    f.bits = wfx->wBitsPerSample;
    f.isFloat = false;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        f.isFloat = true;
    }
    else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            f.isFloat = true;
    }
    return f;
}

//设备采集缓冲(原始字节) -> 单声道 int16。silent 为真表示本包是静音标志包。
static void deviceToMonoI16(const BYTE* data, int frames, const AudioFmt& fmt,
                            bool silent, std::vector<int16_t>& out)
{
    out.resize(static_cast<size_t>(frames));
    if (silent || data == NULL)
    {
        memset(out.data(), 0, static_cast<size_t>(frames) * sizeof(int16_t));
        return;
    }

    if (fmt.isFloat && fmt.bits == 32)
    {
        const float* f = reinterpret_cast<const float*>(data);
        if (fmt.channels == 1)
        {
            FloatToInt16(f, out.data(), static_cast<size_t>(frames));
        }
        else if (fmt.channels == 2)
        {
            std::vector<float> mono(static_cast<size_t>(frames));
            DownmixStereoToMonoF32(f, mono.data(), static_cast<size_t>(frames));
            FloatToInt16(mono.data(), out.data(), static_cast<size_t>(frames));
        }
        else
        {
            //多声道(>2)：取第 0 声道近似为单声道
            std::vector<float> mono(static_cast<size_t>(frames));
            for (int i = 0; i < frames; ++i)
                mono[static_cast<size_t>(i)] = f[i * fmt.channels];
            FloatToInt16(mono.data(), out.data(), static_cast<size_t>(frames));
        }
    }
    else if (!fmt.isFloat && fmt.bits == 16)
    {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        if (fmt.channels == 1)
        {
            memcpy(out.data(), s, static_cast<size_t>(frames) * sizeof(int16_t));
        }
        else if (fmt.channels == 2)
        {
            DownmixStereoToMonoI16(s, out.data(), static_cast<size_t>(frames));
        }
        else
        {
            for (int i = 0; i < frames; ++i)
                out[static_cast<size_t>(i)] = s[i * fmt.channels];
        }
    }
    else
    {
        //PoC 阶段只支持 32 位浮点 / 16 位整型；其余格式填静音
        memset(out.data(), 0, static_cast<size_t>(frames) * sizeof(int16_t));
    }
}

//单声道 int16 -> 设备渲染缓冲(原始字节，按 fmt 的格式与声道数铺开)。
static void monoI16ToDevice(const int16_t* mono, int frames, const AudioFmt& fmt, BYTE* out)
{
    if (fmt.isFloat && fmt.bits == 32)
    {
        float* f = reinterpret_cast<float*>(out);
        std::vector<float> fmono(static_cast<size_t>(frames));
        Int16ToFloat(mono, fmono.data(), static_cast<size_t>(frames));
        if (fmt.channels == 1)
        {
            memcpy(f, fmono.data(), static_cast<size_t>(frames) * sizeof(float));
        }
        else
        {
            for (int i = 0; i < frames; ++i)
            {
                float v = fmono[static_cast<size_t>(i)];
                for (int c = 0; c < fmt.channels; ++c)
                    f[i * fmt.channels + c] = v;
            }
        }
    }
    else if (!fmt.isFloat && fmt.bits == 16)
    {
        int16_t* s = reinterpret_cast<int16_t*>(out);
        if (fmt.channels == 1)
        {
            memcpy(s, mono, static_cast<size_t>(frames) * sizeof(int16_t));
        }
        else
        {
            for (int i = 0; i < frames; ++i)
            {
                for (int c = 0; c < fmt.channels; ++c)
                    s[i * fmt.channels + c] = mono[static_cast<size_t>(i)];
            }
        }
    }
    else
    {
        memset(out, 0, static_cast<size_t>(frames) * fmt.channels * (fmt.bits / 8));
    }
}

//==== WasapiCapture ====

struct WasapiCapture::Impl
{
    std::thread                  thread;        //采集线程
    std::atomic<bool>            stopping;      //是否正在停止
    HANDLE                       initDone;      //初始化完成事件(Start 等它)
    bool                         initOk;        //初始化是否成功(initDone 触发后有效)
    int                          sampleRate;    //设备采样率(初始化成功后有效)
    WasapiCaptureHandler*        handler;       //采集回调(不拥有)

    Impl()
        : stopping(false)
        , initDone(NULL)
        , initOk(false)
        , sampleRate(0)
        , handler(NULL)
    {
    }
};

WasapiCapture::WasapiCapture()
    : m_impl(new Impl())
{
}

WasapiCapture::~WasapiCapture()
{
    Stop();
    if (m_impl->initDone != NULL)
        ::CloseHandle(m_impl->initDone);
    delete m_impl;
}

bool WasapiCapture::Start(WasapiCaptureHandler* handler, int& outSampleRate)
{
    if (handler == NULL)
        return false;

    m_impl->handler = handler;
    m_impl->stopping.store(false);
    m_impl->initOk = false;
    if (m_impl->initDone == NULL)
        m_impl->initDone = ::CreateEvent(NULL, TRUE, FALSE, NULL);   //手动复位
    else
        ::ResetEvent(m_impl->initDone);

    m_impl->thread = std::thread(&WasapiCapture::CaptureProc, this);

    //等采集线程报告初始化结果
    ::WaitForSingleObject(m_impl->initDone, INFINITE);
    if (!m_impl->initOk)
    {
        if (m_impl->thread.joinable())
            m_impl->thread.join();
        return false;
    }
    outSampleRate = m_impl->sampleRate;
    return true;
}

void WasapiCapture::Stop()
{
    m_impl->stopping.store(true);
    if (m_impl->thread.joinable())
        m_impl->thread.join();
}

void WasapiCapture::CaptureProc()
{
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumr = NULL;
    IMMDevice*           dev = NULL;
    IAudioClient*        client = NULL;
    IAudioCaptureClient* capture = NULL;
    WAVEFORMATEX*        mixFormat = NULL;
    HANDLE               audioEvent = NULL;
    AudioFmt             fmt;
    bool                 ok = false;

    do
    {
        hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumr));
        if (FAILED(hr) || enumr == NULL)
        {
            LOG_ERROR("WasapiCapture: create device enumerator failed, hr=0x%08x.", hr);
            break;
        }
        hr = enumr->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
        if (FAILED(hr) || dev == NULL)
        {
            LOG_ERROR("WasapiCapture: no default capture endpoint, hr=0x%08x.", hr);
            break;
        }
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&client));
        if (FAILED(hr) || client == NULL)
            break;
        hr = client->GetMixFormat(&mixFormat);
        if (FAILED(hr) || mixFormat == NULL)
            break;
        fmt = parseFormat(mixFormat);
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, mixFormat, NULL);
        if (FAILED(hr))
        {
            LOG_ERROR("WasapiCapture: IAudioClient::Initialize failed, hr=0x%08x.", hr);
            break;
        }
        audioEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
        if (audioEvent == NULL)
            break;
        hr = client->SetEventHandle(audioEvent);
        if (FAILED(hr))
            break;
        hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
        if (FAILED(hr) || capture == NULL)
            break;
        hr = client->Start();
        if (FAILED(hr))
            break;
        ok = true;
    } while (false);

    //报告初始化结果给 Start
    m_impl->initOk = ok;
    m_impl->sampleRate = ok ? fmt.sampleRate : 0;
    if (ok)
        LOG_INFO("WasapiCapture: started, %dHz %dch %dbit %s.",
                 fmt.sampleRate, fmt.channels, fmt.bits, fmt.isFloat ? "float" : "int");
    ::SetEvent(m_impl->initDone);

    if (ok)
    {
        std::vector<int16_t> tmp;
        while (!m_impl->stopping.load())
        {
            DWORD w = ::WaitForSingleObject(audioEvent, kWaitEventMs);
            if (m_impl->stopping.load())
                break;
            if (w != WAIT_OBJECT_0)
                continue;   //超时，回去检查停止标志

            UINT32 packet = 0;
            capture->GetNextPacketSize(&packet);
            while (packet > 0 && !m_impl->stopping.load())
            {
                BYTE*  data = NULL;
                UINT32 frames = 0;
                DWORD  flags = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, NULL, NULL);
                if (FAILED(hr))
                    break;
                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                deviceToMonoI16(data, static_cast<int>(frames), fmt, silent, tmp);
                if (m_impl->handler != NULL && frames > 0)
                    m_impl->handler->OnAudioCaptured(tmp.data(), static_cast<int>(frames));
                capture->ReleaseBuffer(frames);
                capture->GetNextPacketSize(&packet);
            }
        }
        client->Stop();
    }

    //清理(均在本线程创建)
    if (capture != NULL)
        capture->Release();
    if (client != NULL)
        client->Release();
    if (mixFormat != NULL)
        ::CoTaskMemFree(mixFormat);
    if (dev != NULL)
        dev->Release();
    if (enumr != NULL)
        enumr->Release();
    if (audioEvent != NULL)
        ::CloseHandle(audioEvent);
    if (comInited)
        ::CoUninitialize();
}

//==== WasapiRender ====

struct WasapiRender::Impl
{
    std::thread          thread;
    std::atomic<bool>    stopping;
    HANDLE               initDone;
    bool                 initOk;
    int                  sampleRate;
    WasapiRenderSource*  source;

    Impl()
        : stopping(false)
        , initDone(NULL)
        , initOk(false)
        , sampleRate(0)
        , source(NULL)
    {
    }
};

WasapiRender::WasapiRender()
    : m_impl(new Impl())
{
}

WasapiRender::~WasapiRender()
{
    Stop();
    if (m_impl->initDone != NULL)
        ::CloseHandle(m_impl->initDone);
    delete m_impl;
}

bool WasapiRender::Start(WasapiRenderSource* source, int& outSampleRate)
{
    if (source == NULL)
        return false;

    m_impl->source = source;
    m_impl->stopping.store(false);
    m_impl->initOk = false;
    if (m_impl->initDone == NULL)
        m_impl->initDone = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    else
        ::ResetEvent(m_impl->initDone);

    m_impl->thread = std::thread(&WasapiRender::RenderProc, this);

    ::WaitForSingleObject(m_impl->initDone, INFINITE);
    if (!m_impl->initOk)
    {
        if (m_impl->thread.joinable())
            m_impl->thread.join();
        return false;
    }
    outSampleRate = m_impl->sampleRate;
    return true;
}

void WasapiRender::Stop()
{
    m_impl->stopping.store(true);
    if (m_impl->thread.joinable())
        m_impl->thread.join();
}

void WasapiRender::RenderProc()
{
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumr = NULL;
    IMMDevice*           dev = NULL;
    IAudioClient*        client = NULL;
    IAudioRenderClient*  render = NULL;
    WAVEFORMATEX*        mixFormat = NULL;
    HANDLE               audioEvent = NULL;
    UINT32               bufferFrames = 0;
    AudioFmt             fmt;
    bool                 ok = false;

    do
    {
        hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumr));
        if (FAILED(hr) || enumr == NULL)
            break;
        hr = enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (FAILED(hr) || dev == NULL)
        {
            LOG_ERROR("WasapiRender: no default render endpoint, hr=0x%08x.", hr);
            break;
        }
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&client));
        if (FAILED(hr) || client == NULL)
            break;
        hr = client->GetMixFormat(&mixFormat);
        if (FAILED(hr) || mixFormat == NULL)
            break;
        fmt = parseFormat(mixFormat);
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, mixFormat, NULL);
        if (FAILED(hr))
        {
            LOG_ERROR("WasapiRender: IAudioClient::Initialize failed, hr=0x%08x.", hr);
            break;
        }
        audioEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
        if (audioEvent == NULL)
            break;
        hr = client->SetEventHandle(audioEvent);
        if (FAILED(hr))
            break;
        hr = client->GetBufferSize(&bufferFrames);
        if (FAILED(hr) || bufferFrames == 0)
            break;
        hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
        if (FAILED(hr) || render == NULL)
            break;
        ok = true;
    } while (false);

    m_impl->initOk = ok;
    m_impl->sampleRate = ok ? fmt.sampleRate : 0;
    if (ok)
        LOG_INFO("WasapiRender: started, %dHz %dch %dbit %s, bufFrames=%u.",
                 fmt.sampleRate, fmt.channels, fmt.bits, fmt.isFloat ? "float" : "int", bufferFrames);
    ::SetEvent(m_impl->initDone);

    if (ok)
    {
        std::vector<int16_t> mono;

        //预滚：先用 source(此刻通常给静音)填满整个缓冲，避免启动爆音
        BYTE* data = NULL;
        if (SUCCEEDED(render->GetBuffer(bufferFrames, &data)) && data != NULL)
        {
            mono.resize(static_cast<size_t>(bufferFrames));
            m_impl->source->OnAudioNeeded(mono.data(), static_cast<int>(bufferFrames));
            monoI16ToDevice(mono.data(), static_cast<int>(bufferFrames), fmt, data);
            render->ReleaseBuffer(bufferFrames, 0);
        }

        hr = client->Start();
        if (SUCCEEDED(hr))
        {
            while (!m_impl->stopping.load())
            {
                DWORD w = ::WaitForSingleObject(audioEvent, kWaitEventMs);
                if (m_impl->stopping.load())
                    break;
                if (w != WAIT_OBJECT_0)
                    continue;

                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding)))
                    continue;
                UINT32 avail = bufferFrames - padding;
                if (avail == 0)
                    continue;

                BYTE* buf = NULL;
                if (FAILED(render->GetBuffer(avail, &buf)) || buf == NULL)
                    continue;
                mono.resize(static_cast<size_t>(avail));
                m_impl->source->OnAudioNeeded(mono.data(), static_cast<int>(avail));
                monoI16ToDevice(mono.data(), static_cast<int>(avail), fmt, buf);
                render->ReleaseBuffer(avail, 0);
            }
            client->Stop();
        }
    }

    if (render != NULL)
        render->Release();
    if (client != NULL)
        client->Release();
    if (mixFormat != NULL)
        ::CoTaskMemFree(mixFormat);
    if (dev != NULL)
        dev->Release();
    if (enumr != NULL)
        enumr->Release();
    if (audioEvent != NULL)
        ::CloseHandle(audioEvent);
    if (comInited)
        ::CoUninitialize();
}

} // namespace call
