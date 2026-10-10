/**
 *  屏幕共享「共享电脑声音」的回环采集，见 ShareSystemAudioCapture.h。
 *  balloonwj@qq.com   2026-10-03
 */
//本文件用到 Win8 / Win10 才有的音频接口声明（IActivateAudioInterfaceCompletionHandler 等），按 Win10 编译；
//真正的入口函数运行时再取，不影响 DLL 在旧系统上加载
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00

#include "ShareSystemAudioCapture.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>

#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "ole32.lib")

namespace screenshare {

namespace {

//回环的格式：48 kHz、16 位、立体声（交给系统转换）
const DWORD kLoopRate = 48000;
const WORD kLoopChannels = 2;
const WORD kLoopBits = 16;
const WORD kBitsPerByte = 8;
const WORD kLoopBlockAlign = kLoopChannels * (kLoopBits / kBitsPerByte);
//共享模式缓冲时长：200 毫秒（单位 100 纳秒）
const REFERENCE_TIME kLoopBufferHns = 2000000;
//轮询间隔与等待打开的最长时间（毫秒）
const DWORD kPollMs = 10;
const DWORD kOpenWaitMs = 3000;
//虚拟的「进程回环」设备名与激活参数（SDK 头文件 audioclientactivationparams.h 的同名定义，旧 SDK 没有，这里照抄）
const wchar_t* const kProcessLoopbackDevice = L"VAD\\Process_Loopback";
const int kActivationTypeProcessLoopback = 1;
const int kLoopbackModeExcludeTree = 1;

struct LoopbackParams
{
    DWORD m_targetProcessId;   //目标进程
    int   m_mode;              //包含还是排除目标进程树
};

struct ActivationParams
{
    int            m_type;     //激活类型
    LoopbackParams m_loopback; //进程回环参数
};

typedef HRESULT(WINAPI* ActivateAudioInterfaceAsyncFn)(LPCWSTR, REFIID, PROPVARIANT*,
                                                       IActivateAudioInterfaceCompletionHandler*,
                                                       IActivateAudioInterfaceAsyncOperation**);

//激活完成的回调：记下结果并置位事件。必须是灵活对象（实现 IAgileObject），系统会在任意线程上回调
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    ActivateHandler()
        : m_refs(1)
        , m_hr(E_PENDING)
        , m_client(NULL)
    {
        m_done = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    }

    ~ActivateHandler()
    {
        if (m_client != NULL)
            m_client->Release();
        if (m_done != NULL)
            ::CloseHandle(m_done);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == __uuidof(IAgileObject))
            *ppv = static_cast<IAgileObject*>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_refs));
    }

    STDMETHODIMP_(ULONG) Release()
    {
        const LONG n = ::InterlockedDecrement(&m_refs);
        if (n == 0)
            delete this;
        return static_cast<ULONG>(n);
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op)
    {
        HRESULT activateHr = E_FAIL;
        IUnknown* unk = NULL;
        const HRESULT hr = op->GetActivateResult(&activateHr, &unk);
        m_hr = FAILED(hr) ? hr : activateHr;
        if (SUCCEEDED(m_hr) && unk != NULL)
            unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&m_client));
        if (unk != NULL)
            unk->Release();
        ::SetEvent(m_done);
        return S_OK;
    }

    volatile LONG m_refs;     //引用计数
    HANDLE        m_done;     //完成事件
    HRESULT       m_hr;       //激活结果
    IAudioClient* m_client;   //成功时得到的音频客户端（本对象持有一个引用）
};

//回环客户端的格式
WAVEFORMATEX loopFormat()
{
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = kLoopChannels;
    wf.nSamplesPerSec = kLoopRate;
    wf.wBitsPerSample = kLoopBits;
    wf.nBlockAlign = kLoopBlockAlign;
    wf.nAvgBytesPerSec = kLoopRate * kLoopBlockAlign;
    return wf;
}

//打开「排除本进程树」的回环；成功时返回已初始化的客户端（调用方 Release）
IAudioClient* openProcessLoopback()
{
    HMODULE mod = ::LoadLibraryW(L"Mmdevapi.dll");
    if (mod == NULL)
        return NULL;
    ActivateAudioInterfaceAsyncFn activate =
        reinterpret_cast<ActivateAudioInterfaceAsyncFn>(::GetProcAddress(mod, "ActivateAudioInterfaceAsync"));
    if (activate == NULL)
        return NULL;
    ActivationParams params;
    memset(&params, 0, sizeof(params));
    params.m_type = kActivationTypeProcessLoopback;
    params.m_loopback.m_targetProcessId = ::GetCurrentProcessId();
    params.m_loopback.m_mode = kLoopbackModeExcludeTree;
    PROPVARIANT pv;
    ::PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivateHandler* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = NULL;
    HRESULT hr = activate(kProcessLoopbackDevice, __uuidof(IAudioClient), &pv, handler, &op);
    IAudioClient* client = NULL;
    if (SUCCEEDED(hr) && ::WaitForSingleObject(handler->m_done, kOpenWaitMs) == WAIT_OBJECT_0 &&
        SUCCEEDED(handler->m_hr) && handler->m_client != NULL)
    {
        WAVEFORMATEX wf = loopFormat();
        hr = handler->m_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                           kLoopBufferHns, 0, &wf, NULL);
        if (SUCCEEDED(hr))
        {
            client = handler->m_client;
            client->AddRef();
        }
        else
        {
            LOG_WARNING("screenshare sysaudio: process loopback initialize failed, hr=0x%08lx.",
                        static_cast<unsigned long>(hr));
        }
    }
    else
    {
        LOG_WARNING("screenshare sysaudio: process loopback activation failed, hr=0x%08lx, result=0x%08lx.",
                    static_cast<unsigned long>(hr), static_cast<unsigned long>(handler->m_hr));
    }
    if (op != NULL)
        op->Release();
    handler->Release();
    return client;
}

//打开默认播放设备的普通回环；成功时返回已初始化的客户端（调用方 Release）
IAudioClient* openEndpointLoopback()
{
    IMMDeviceEnumerator* enumerator = NULL;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr))
        return NULL;
    IMMDevice* device = NULL;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr))
        return NULL;
    IAudioClient* client = NULL;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&client));
    device->Release();
    if (FAILED(hr))
        return NULL;
    WAVEFORMATEX wf = loopFormat();
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            kLoopBufferHns, 0, &wf, NULL);
    if (FAILED(hr))
    {
        LOG_WARNING("screenshare sysaudio: endpoint loopback initialize failed, hr=0x%08lx.",
                    static_cast<unsigned long>(hr));
        client->Release();
        return NULL;
    }
    return client;
}

} // namespace

ShareSystemAudioCapture::ShareSystemAudioCapture()
    : m_handler(NULL)
    , m_stop(false)
    , m_running(false)
    , m_excludingSelf(false)
    , m_readyEvent(NULL)
{
}

ShareSystemAudioCapture::~ShareSystemAudioCapture()
{
    Stop();
}

bool ShareSystemAudioCapture::Start(ShareSystemAudioHandler* handler)
{
    if (handler == NULL || m_thread)
        return false;
    m_handler = handler;
    m_stop.store(false);
    m_running.store(false);
    m_readyEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    m_thread.reset(new std::thread(&ShareSystemAudioCapture::threadProc, this));
    ::WaitForSingleObject(static_cast<HANDLE>(m_readyEvent), kOpenWaitMs * 2);
    if (!m_running.load())
    {
        Stop();
        return false;
    }
    return true;
}

void ShareSystemAudioCapture::Stop()
{
    m_stop.store(true);
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
    if (m_readyEvent != NULL)
    {
        ::CloseHandle(static_cast<HANDLE>(m_readyEvent));
        m_readyEvent = NULL;
    }
    m_running.store(false);
    m_handler = NULL;
}

bool ShareSystemAudioCapture::IsRunning() const
{
    return m_running.load();
}

bool ShareSystemAudioCapture::IsExcludingSelf() const
{
    return m_excludingSelf.load();
}

void ShareSystemAudioCapture::threadProc()
{
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IAudioClient* client = openProcessLoopback();
    m_excludingSelf.store(client != NULL);
    if (client == NULL)
        client = openEndpointLoopback();
    IAudioCaptureClient* capture = NULL;
    if (client != NULL &&
        (FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))) ||
         FAILED(client->Start())))
    {
        if (capture != NULL)
            capture->Release();
        capture = NULL;
    }
    if (capture == NULL)
    {
        LOG_ERROR("screenshare sysaudio: no loopback available.");
        if (client != NULL)
            client->Release();
        ::SetEvent(static_cast<HANDLE>(m_readyEvent));
        ::CoUninitialize();
        return;
    }
    m_running.store(true);
    LOG_INFO("screenshare sysaudio: capturing, exclude self=%d.", m_excludingSelf.load() ? 1 : 0);
    ::SetEvent(static_cast<HANDLE>(m_readyEvent));

    std::vector<int16_t> mono;
    while (!m_stop.load())
    {
        UINT32 next = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&next)) && next > 0)
        {
            BYTE* data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, NULL, NULL)))
                break;
            mono.assign(frames, 0);
            //系统标了静音的包按全零处理；否则左右声道取平均
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != NULL)
            {
                const int16_t* s = reinterpret_cast<const int16_t*>(data);
                for (UINT32 i = 0; i < frames; ++i)
                    mono[i] = static_cast<int16_t>((static_cast<int32_t>(s[i * kLoopChannels]) +
                                                    s[i * kLoopChannels + 1]) / kLoopChannels);
            }
            capture->ReleaseBuffer(frames);
            if (frames > 0 && m_handler != NULL)
                m_handler->OnSystemAudio(&mono[0], static_cast<int>(frames));
        }
        ::Sleep(kPollMs);
    }
    client->Stop();
    capture->Release();
    client->Release();
    m_running.store(false);
    LOG_INFO("screenshare sysaudio: stopped.");
    ::CoUninitialize();
}

} // namespace screenshare
