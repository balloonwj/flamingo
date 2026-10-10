/**
 *  屏幕共享画面的硬件 H.264 编码器，ShareHwH264Encoder.cpp
 *  说明见 ShareHwH264Encoder.h。
 *
 *  异步 MFT 的事件用 BeginGetEvent 接收：回调（在 Media Foundation 的工作队列线程上）把事件放进队列并置信号，
 *  推流线程等信号（带超时）后取出事件处理。这样硬件编码器卡住时推流线程不会无限期阻塞。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareHwH264Encoder.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <strmif.h>      //ICodecAPI
#include <codecapi.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

#include <string.h>
#include <deque>
#include <mutex>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace screenshare {

namespace {

//一秒的 100ns 计数（帧时长 = 此值 / fps）
const int64_t kHnsPerSecond = 10000000LL;
//喂入一帧前等编码器要输入（METransformNeedInput）的最长时间（毫秒）：超时视为编码器卡住
const DWORD kInputWaitMs = 500;
//喂入一帧后等它的码流的最长时间（毫秒）：硬件编码器通常几毫秒就出，等不到就留到下一帧再取，不再多等
const DWORD kOutputWaitMs = 30;
//初始化时依次尝试的 H.264 档次：先基本档次（与软件编码器一致），硬件不支持时退到主档次
const UINT32 kProfiles[] = { eAVEncH264VProfile_Base, eAVEncH264VProfile_Main };

//按 IID 取 ICodecAPI 并设一个 UINT32 属性；失败不致命（各家硬件支持的属性不一样）
bool setCodecUint(IMFTransform* mft, const GUID& key, UINT32 value)
{
    ICodecAPI* api = NULL;
    if (FAILED(mft->QueryInterface(IID_PPV_ARGS(&api))) || api == NULL)
        return false;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI4;
    v.ulVal = value;
    const HRESULT hr = api->SetValue(&key, &v);
    api->Release();
    return SUCCEEDED(hr);
}

//设一个 BOOL 型的 ICodecAPI 属性
bool setCodecBool(IMFTransform* mft, const GUID& key, bool value)
{
    ICodecAPI* api = NULL;
    if (FAILED(mft->QueryInterface(IID_PPV_ARGS(&api))) || api == NULL)
        return false;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    const HRESULT hr = api->SetValue(&key, &v);
    api->Release();
    return SUCCEEDED(hr);
}

//异步 MFT 的事件接收：回调把事件放进队列并置信号；推流线程等信号、取事件
class HwEventCallback : public IMFAsyncCallback
{
public:
    explicit HwEventCallback(IMFMediaEventGenerator* gen)
        : m_ref(1)
        , m_gen(gen)
        , m_signal(::CreateEventW(NULL, FALSE, FALSE, NULL))
        , m_pending(false)
        , m_stopped(false)
        , m_failed(false)
    {
        m_gen->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFAsyncCallback))
        {
            *ppv = static_cast<IMFAsyncCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG ref = ::InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return static_cast<ULONG>(ref);
    }

    STDMETHODIMP GetParameters(DWORD* flags, DWORD* queue) override
    {
        (void)flags;
        (void)queue;
        return E_NOTIMPL;
    }

    //Media Foundation 工作队列线程：取出事件放进队列，再请求下一个事件
    STDMETHODIMP Invoke(IMFAsyncResult* result) override
    {
        IMFMediaEvent* ev = NULL;
        const HRESULT hr = m_gen->EndGetEvent(result, &ev);
        bool rearm = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_pending = false;
            if (SUCCEEDED(hr) && ev != NULL)
            {
                m_events.push_back(ev);
                rearm = !m_stopped;
            }
            else if (!m_stopped)
            {
                m_failed = true;
            }
        }
        if (rearm)
            Arm();
        ::SetEvent(m_signal);
        return S_OK;
    }

    //请求下一个事件（已有一个请求在等时什么都不做）
    bool Arm()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_pending || m_stopped)
            return true;
        const HRESULT hr = m_gen->BeginGetEvent(this, NULL);
        if (FAILED(hr))
        {
            m_failed = true;
            return false;
        }
        m_pending = true;
        return true;
    }

    //取出一个事件（调用方 Release）；没有时返回 NULL
    IMFMediaEvent* Pop()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_events.empty())
            return NULL;
        IMFMediaEvent* ev = m_events.front();
        m_events.pop_front();
        return ev;
    }

    //接收事件出错（不含主动停止）
    bool Failed()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_failed;
    }

    //不再请求新的事件（关闭编码器前调用）
    void Stop()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_stopped = true;
    }

    HANDLE Signal() const
    {
        return m_signal;
    }

private:
    ~HwEventCallback()
    {
        for (size_t i = 0; i < m_events.size(); ++i)
            m_events[i]->Release();
        m_gen->Release();
        if (m_signal != NULL)
            ::CloseHandle(m_signal);
    }

    volatile LONG               m_ref;       //引用计数
    IMFMediaEventGenerator*     m_gen;       //编码器的事件源（本对象持有一个引用）
    HANDLE                      m_signal;    //有新事件（或出错）时置位的自动复位事件
    std::mutex                  m_mutex;     //保护以下各项
    std::deque<IMFMediaEvent*>  m_events;    //收到、还没处理的事件（各持有一个引用）
    bool                        m_pending;   //是否有一个 BeginGetEvent 在等
    bool                        m_stopped;   //是否已停止接收
    bool                        m_failed;    //接收是否出错
};

} // namespace

struct ShareHwH264Encoder::Impl
{
    IMFActivate*     activate;     //所用硬件编码器的激活对象（关闭时 ShutdownObject）
    IMFTransform*    mft;          //硬件编码器
    HwEventCallback* events;       //事件接收（本结构持有一个引用）
    std::wstring     name;         //硬件编码器名称
    DWORD            inStream;     //输入流号
    DWORD            outStream;    //输出流号
    bool             providesSamples;   //输出样本是否由编码器分配
    int              width;        //编码宽
    int              height;       //编码高
    int              fps;          //帧率
    int              gopFrames;    //关键帧间隔（帧），重建编码器时沿用
    int              needInput;    //编码器要了、还没喂的输入帧数
    int              outputs;      //累计取出的码流包数
    bool             ready;        //是否已建好
    bool             broken;       //是否已出错（之后一律返回失败）

    Impl()
        : activate(NULL)
        , mft(NULL)
        , events(NULL)
        , inStream(0)
        , outStream(0)
        , providesSamples(false)
        , width(0)
        , height(0)
        , fps(0)
        , gopFrames(0)
        , needInput(0)
        , outputs(0)
        , ready(false)
        , broken(false)
    {
    }
};

namespace {

//列出硬件 H.264 编码器（调用方逐个 Release 并 CoTaskMemFree 数组）
HRESULT enumHardwareEncoders(IMFActivate*** acts, UINT32* count)
{
    MFT_REGISTER_TYPE_INFO in = { MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO out = { MFMediaType_Video, MFVideoFormat_H264 };
    return ::MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &in, &out,
                       acts, count);
}

//设输出类型（H.264）：依次尝试 kProfiles 里的档次
bool setOutputType(IMFTransform* mft, DWORD stream, int width, int height, int fps, int bitrateBps)
{
    for (size_t i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); ++i)
    {
        IMFMediaType* t = NULL;
        if (FAILED(::MFCreateMediaType(&t)) || t == NULL)
            return false;
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        t->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrateBps));
        ::MFSetAttributeSize(t, MF_MT_FRAME_SIZE, width, height);
        ::MFSetAttributeRatio(t, MF_MT_FRAME_RATE, fps, 1);
        ::MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        t->SetUINT32(MF_MT_MPEG2_PROFILE, kProfiles[i]);
        const HRESULT hr = mft->SetOutputType(stream, t, 0);
        t->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    return false;
}

//设输入类型（NV12）：先直接设，不行就从编码器给出的可用类型里找 NV12 再补上尺寸与帧率
bool setInputType(IMFTransform* mft, DWORD stream, int width, int height, int fps)
{
    IMFMediaType* t = NULL;
    if (FAILED(::MFCreateMediaType(&t)) || t == NULL)
        return false;
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    ::MFSetAttributeSize(t, MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(t, MF_MT_FRAME_RATE, fps, 1);
    ::MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    HRESULT hr = mft->SetInputType(stream, t, 0);
    t->Release();
    if (SUCCEEDED(hr))
        return true;
    for (DWORD i = 0;; ++i)
    {
        IMFMediaType* avail = NULL;
        if (FAILED(mft->GetInputAvailableType(stream, i, &avail)) || avail == NULL)
            return false;
        GUID sub = GUID_NULL;
        avail->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFVideoFormat_NV12)
        {
            ::MFSetAttributeSize(avail, MF_MT_FRAME_SIZE, width, height);
            ::MFSetAttributeRatio(avail, MF_MT_FRAME_RATE, fps, 1);
            hr = mft->SetInputType(stream, avail, 0);
            avail->Release();
            return SUCCEEDED(hr);
        }
        avail->Release();
    }
}

//关闭并释放一个硬件编码器的全部对象
void releaseEncoder(ShareHwH264Encoder::Impl* impl);

} // namespace

ShareHwH264Encoder::ShareHwH264Encoder()
    : m_impl(new Impl())
{
}

ShareHwH264Encoder::~ShareHwH264Encoder()
{
    Close();
}

int ShareHwH264Encoder::CountHardwareEncoders()
{
    IMFActivate** acts = NULL;
    UINT32 count = 0;
    if (FAILED(enumHardwareEncoders(&acts, &count)))
        return 0;
    for (UINT32 i = 0; i < count; ++i)
        acts[i]->Release();
    ::CoTaskMemFree(acts);
    return static_cast<int>(count);
}

bool ShareHwH264Encoder::Init(int width, int height, int fps, int bitrateBps, int gopFrames)
{
    Close();
    if (width <= 0 || height <= 0 || fps <= 0 || bitrateBps <= 0)
        return false;
    IMFActivate** acts = NULL;
    UINT32 count = 0;
    if (FAILED(enumHardwareEncoders(&acts, &count)) || count == 0)
    {
        if (acts != NULL)
            ::CoTaskMemFree(acts);
        LOG_INFO("ShareHwH264Encoder: no hardware H.264 encoder.");
        return false;
    }
    for (UINT32 i = 0; i < count && !m_impl->ready; ++i)
    {
        IMFActivate* act = acts[i];
        WCHAR* name = NULL;
        UINT32 nameLen = 0;
        act->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &nameLen);
        const std::wstring friendly = (name != NULL) ? name : L"(unnamed)";
        ::CoTaskMemFree(name);

        IMFTransform* mft = NULL;
        if (FAILED(act->ActivateObject(IID_PPV_ARGS(&mft))) || mft == NULL)
        {
            LOG_WARNING("ShareHwH264Encoder: activate %S failed.", friendly.c_str());
            continue;
        }
        m_impl->activate = act;
        m_impl->activate->AddRef();
        m_impl->mft = mft;
        m_impl->name = friendly;

        //解锁异步模式、开低延迟
        IMFAttributes* attr = NULL;
        if (SUCCEEDED(mft->GetAttributes(&attr)) && attr != NULL)
        {
            attr->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            attr->SetUINT32(MF_LOW_LATENCY, TRUE);
            attr->Release();
        }
        //流号：多数编码器没有实现 GetStreamIDs（返回 E_NOTIMPL），这时就是 0
        DWORD inId = 0;
        DWORD outId = 0;
        if (SUCCEEDED(mft->GetStreamIDs(1, &inId, 1, &outId)))
        {
            m_impl->inStream = inId;
            m_impl->outStream = outId;
        }
        setCodecBool(mft, CODECAPI_AVLowLatencyMode, true);
        setCodecUint(mft, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        setCodecUint(mft, CODECAPI_AVEncCommonMeanBitRate, static_cast<UINT32>(bitrateBps));
        if (gopFrames > 0)
            setCodecUint(mft, CODECAPI_AVEncMPVGOPSize, static_cast<UINT32>(gopFrames));

        bool ok = setOutputType(mft, m_impl->outStream, width, height, fps, bitrateBps) &&
                  setInputType(mft, m_impl->inStream, width, height, fps);
        IMFMediaEventGenerator* gen = NULL;
        if (ok)
            ok = SUCCEEDED(mft->QueryInterface(IID_PPV_ARGS(&gen))) && gen != NULL;
        if (ok)
        {
            MFT_OUTPUT_STREAM_INFO si;
            memset(&si, 0, sizeof(si));
            mft->GetOutputStreamInfo(m_impl->outStream, &si);
            m_impl->providesSamples = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                                     MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            m_impl->events = new HwEventCallback(gen);
            gen->Release();
            ok = SUCCEEDED(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) &&
                 SUCCEEDED(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)) && m_impl->events->Arm();
        }
        if (!ok)
        {
            LOG_WARNING("ShareHwH264Encoder: %S does not accept %dx%d @%dfps, try next.", friendly.c_str(), width,
                        height, fps);
            releaseEncoder(m_impl.get());
            continue;
        }
        m_impl->width = width;
        m_impl->height = height;
        m_impl->fps = fps;
        m_impl->gopFrames = gopFrames;
        m_impl->needInput = 0;
        m_impl->outputs = 0;
        m_impl->broken = false;
        m_impl->ready = true;
        LOG_INFO("ShareHwH264Encoder: using %S, %dx%d @%dfps %dbps, encoder allocates output=%d.", friendly.c_str(),
                 width, height, fps, bitrateBps, m_impl->providesSamples ? 1 : 0);
    }
    for (UINT32 i = 0; i < count; ++i)
        acts[i]->Release();
    ::CoTaskMemFree(acts);
    return m_impl->ready;
}

namespace {

void releaseEncoder(ShareHwH264Encoder::Impl* impl)
{
    if (impl->events != NULL)
        impl->events->Stop();
    if (impl->mft != NULL)
    {
        impl->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        //关闭事件队列：还在等的 BeginGetEvent 随之结束，回调不再请求新事件
        IMFShutdown* sd = NULL;
        if (SUCCEEDED(impl->mft->QueryInterface(IID_PPV_ARGS(&sd))) && sd != NULL)
        {
            sd->Shutdown();
            sd->Release();
        }
        impl->mft->Release();
        impl->mft = NULL;
    }
    if (impl->events != NULL)
    {
        impl->events->Release();
        impl->events = NULL;
    }
    if (impl->activate != NULL)
    {
        impl->activate->ShutdownObject();
        impl->activate->Release();
        impl->activate = NULL;
    }
    impl->name.clear();
    impl->inStream = 0;
    impl->outStream = 0;
    impl->providesSamples = false;
    impl->ready = false;
}

//取出一个码流包交给回调；返回 false 表示编码器出错
bool takeOutput(ShareHwH264Encoder::Impl* impl, call::MfH264EncoderSink* sink)
{
    MFT_OUTPUT_DATA_BUFFER odb;
    memset(&odb, 0, sizeof(odb));
    odb.dwStreamID = impl->outStream;
    IMFSample* own = NULL;
    if (!impl->providesSamples)
    {
        //编码器不分配输出样本：按它报的大小（报 0 时按一帧原始画面大小）自己分配
        MFT_OUTPUT_STREAM_INFO si;
        memset(&si, 0, sizeof(si));
        impl->mft->GetOutputStreamInfo(impl->outStream, &si);
        const DWORD cap = (si.cbSize > 0) ? si.cbSize : static_cast<DWORD>(impl->width * impl->height);
        IMFMediaBuffer* buf = NULL;
        if (FAILED(::MFCreateSample(&own)) || FAILED(::MFCreateMemoryBuffer(cap, &buf)))
        {
            if (own != NULL)
                own->Release();
            return false;
        }
        own->AddBuffer(buf);
        buf->Release();
        odb.pSample = own;
    }
    DWORD status = 0;
    HRESULT hr = impl->mft->ProcessOutput(0, 1, &odb, &status);
    if (odb.pEvents != NULL)
        odb.pEvents->Release();
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
    {
        //输出格式变了（个别编码器开始时会这样）：按编码器给的第一个可用类型重设，下一个 HaveOutput 再取
        IMFMediaType* t = NULL;
        if (SUCCEEDED(impl->mft->GetOutputAvailableType(impl->outStream, 0, &t)) && t != NULL)
        {
            impl->mft->SetOutputType(impl->outStream, t, 0);
            t->Release();
        }
        if (own != NULL)
            own->Release();
        return true;
    }
    if (FAILED(hr))
    {
        LOG_ERROR("ShareHwH264Encoder: ProcessOutput failed, hr=0x%08x.", static_cast<unsigned>(hr));
        if (own != NULL)
            own->Release();
        else if (odb.pSample != NULL)
            odb.pSample->Release();
        return false;
    }
    IMFSample* sample = odb.pSample;
    if (sample != NULL)
    {
        UINT32 cleanPoint = 0;
        const bool keyframe = SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &cleanPoint)) && cleanPoint != 0;
        LONGLONG t = 0;
        sample->GetSampleTime(&t);
        IMFMediaBuffer* cont = NULL;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&cont)) && cont != NULL)
        {
            BYTE* p = NULL;
            DWORD len = 0;
            if (SUCCEEDED(cont->Lock(&p, NULL, &len)) && p != NULL && len > 0)
            {
                sink->OnEncodedH264(p, len, keyframe, static_cast<int64_t>(t));
                ++impl->outputs;
                cont->Unlock();
            }
            cont->Release();
        }
    }
    //自己分配的样本自己释放；编码器分配的样本由取用方释放
    if (own != NULL)
        own->Release();
    else if (sample != NULL)
        sample->Release();
    return true;
}

//处理队列里的全部事件；返回 false 表示编码器出错
bool drainEvents(ShareHwH264Encoder::Impl* impl, call::MfH264EncoderSink* sink)
{
    for (;;)
    {
        IMFMediaEvent* ev = impl->events->Pop();
        if (ev == NULL)
            break;
        MediaEventType type = MEUnknown;
        ev->GetType(&type);
        HRESULT status = S_OK;
        ev->GetStatus(&status);
        ev->Release();
        if (FAILED(status))
        {
            LOG_ERROR("ShareHwH264Encoder: event %u failed, hr=0x%08x.", static_cast<unsigned>(type),
                      static_cast<unsigned>(status));
            return false;
        }
        switch (type)
        {
        //编码器要一帧输入
        case METransformNeedInput:
            ++impl->needInput;
            break;
        //有一包码流可以取
        case METransformHaveOutput:
            if (!takeOutput(impl, sink))
                return false;
            break;
        //出错
        case MEError:
            return false;
        //其余事件（排空完成、标记等）与本用法无关
        default:
            break;
        }
    }
    return !impl->events->Failed();
}

} // namespace

void ShareHwH264Encoder::Close()
{
    if (!m_impl)
        return;
    releaseEncoder(m_impl.get());
    m_impl->width = 0;
    m_impl->height = 0;
    m_impl->fps = 0;
    m_impl->needInput = 0;
    m_impl->outputs = 0;
    m_impl->broken = false;
}

bool ShareHwH264Encoder::IsReady() const
{
    return m_impl->ready && !m_impl->broken;
}

const std::wstring& ShareHwH264Encoder::Name() const
{
    return m_impl->name;
}

bool ShareHwH264Encoder::ForceKeyFrame()
{
    if (!IsReady())
        return false;
    return setCodecUint(m_impl->mft, CODECAPI_AVEncVideoForceKeyFrame, 1);
}

bool ShareHwH264Encoder::SetBitrate(int bitrateBps)
{
    if (!IsReady() || bitrateBps <= 0)
        return false;
    //按新码率重建（理由见头文件）；Init 会先 Close，参数先拷出来
    const int w = m_impl->width;
    const int h = m_impl->height;
    const int fps = m_impl->fps;
    const int gop = m_impl->gopFrames;
    return Init(w, h, fps, bitrateBps, gop);
}

bool ShareHwH264Encoder::EncodeNv12(const uint8_t* nv12, int stride, int64_t ptsHns, call::MfH264EncoderSink* sink)
{
    if (!IsReady() || nv12 == NULL || sink == NULL || stride < m_impl->width)
        return false;
    Impl* impl = m_impl.get();

    //1. 等编码器要输入（期间到达的码流照常取出）
    const ULONGLONG inputDeadline = ::GetTickCount64() + kInputWaitMs;
    for (;;)
    {
        if (!drainEvents(impl, sink))
        {
            impl->broken = true;
            return false;
        }
        if (impl->needInput > 0)
            break;
        const ULONGLONG now = ::GetTickCount64();
        if (now >= inputDeadline)
        {
            LOG_ERROR("ShareHwH264Encoder: %S did not request input within %lu ms.", impl->name.c_str(), kInputWaitMs);
            impl->broken = true;
            return false;
        }
        ::WaitForSingleObject(impl->events->Signal(), static_cast<DWORD>(inputDeadline - now));
    }

    //2. 把画面拷进连续的缓冲（行跨度 = 编码宽度）后喂入
    const int w = impl->width;
    const int h = impl->height;
    const DWORD frameBytes = static_cast<DWORD>(w * h * 3 / 2);
    IMFMediaBuffer* buf = NULL;
    if (FAILED(::MFCreateMemoryBuffer(frameBytes, &buf)) || buf == NULL)
        return false;
    BYTE* p = NULL;
    if (FAILED(buf->Lock(&p, NULL, NULL)))
    {
        buf->Release();
        return false;
    }
    for (int y = 0; y < h; ++y)
        memcpy(p + static_cast<size_t>(y) * w, nv12 + static_cast<size_t>(y) * stride, w);
    const uint8_t* srcUv = nv12 + static_cast<size_t>(stride) * h;
    BYTE* dstUv = p + static_cast<size_t>(w) * h;
    for (int y = 0; y < h / 2; ++y)
        memcpy(dstUv + static_cast<size_t>(y) * w, srcUv + static_cast<size_t>(y) * stride, w);
    buf->Unlock();
    buf->SetCurrentLength(frameBytes);
    IMFSample* sample = NULL;
    ::MFCreateSample(&sample);
    sample->AddBuffer(buf);
    buf->Release();
    sample->SetSampleTime(ptsHns);
    sample->SetSampleDuration(kHnsPerSecond / impl->fps);
    const HRESULT hr = impl->mft->ProcessInput(impl->inStream, sample, 0);
    sample->Release();
    if (FAILED(hr))
    {
        LOG_ERROR("ShareHwH264Encoder: ProcessInput failed, hr=0x%08x.", static_cast<unsigned>(hr));
        impl->broken = true;
        return false;
    }
    --impl->needInput;

    //3. 等一小段时间取这一帧的码流，取不到就留到下一帧
    const int outputsBefore = impl->outputs;
    const ULONGLONG outputDeadline = ::GetTickCount64() + kOutputWaitMs;
    for (;;)
    {
        if (!drainEvents(impl, sink))
        {
            impl->broken = true;
            return false;
        }
        if (impl->outputs > outputsBefore)
            break;
        const ULONGLONG now = ::GetTickCount64();
        if (now >= outputDeadline)
            break;
        ::WaitForSingleObject(impl->events->Signal(), static_cast<DWORD>(outputDeadline - now));
    }
    return true;
}

} // namespace screenshare
