/**
 *  字幕编辑器的视频预览播放器，见 CaptionVideoPlayer.h。做法来自第 0 期试验程序 tmp/caption0c/enginetest.cpp（14.7）。
 *  balloonwj@qq.com   2026-10-06
 */
//不包含 stdafx.h：它把 _WIN32_WINNT 定为 Win7，而 mfmediaengine.h 的声明要求 Win8 及以上（媒体引擎本身 Win8 起才有）
#include "CaptionVideoPlayer.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <string.h>

#include <atomic>

#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace caption {

namespace {

//Media Foundation 的时间单位换算：秒 ↔ 毫秒、100 纳秒 ↔ 毫秒
const double kMsPerSecond = 1000.0;
const LONGLONG kHnsPerMs = 10000;
//每像素字节数（BGRA）
const UINT kBytesPerPixel = 4;

template <class T>
void SafeRelease(T*& p)
{
    if (p != NULL)
    {
        p->Release();
        p = NULL;
    }
}

} // namespace

/**
 *  媒体引擎的事件回调：在引擎线程上收到事件，记下状态并投给编辑器窗口。
 */
class CaptionPlayerNotify : public IMFMediaEngineNotify
{
public:
    CaptionPlayerNotify(HWND wnd, UINT msg)
        : m_ref(1)
        , m_wnd(wnd)
        , m_msg(msg)
        , m_ready(false)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify))
        {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_ref));
    }

    STDMETHODIMP_(ULONG) Release()
    {
        const LONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return static_cast<ULONG>(r);
    }

    STDMETHODIMP EventNotify(DWORD ev, DWORD_PTR param1, DWORD param2)
    {
        int event = 0;
        switch (ev)
        {
        //首帧就绪，可以播放
        case MF_MEDIA_ENGINE_EVENT_CANPLAY:
            m_ready.store(true);
            event = caption_player_ready;
            break;
        //跳转完成
        case MF_MEDIA_ENGINE_EVENT_SEEKED:
            event = caption_player_seeked;
            break;
        //播放到结尾
        case MF_MEDIA_ENGINE_EVENT_ENDED:
            event = caption_player_ended;
            break;
        //出错：param1 为 MF_MEDIA_ENGINE_ERR，param2 为 HRESULT
        case MF_MEDIA_ENGINE_EVENT_ERROR:
            LOG_ERROR("caption editor: media engine error %lu, hr=0x%08lX.", static_cast<unsigned long>(param1),
                      static_cast<unsigned long>(param2));
            event = caption_player_error;
            break;
        //其余事件不关心
        default:
            break;
        }
        HWND wnd = m_wnd;
        if (event != 0 && wnd != NULL && ::IsWindow(wnd))
            ::PostMessage(wnd, m_msg, static_cast<WPARAM>(event), 0);
        return S_OK;
    }

    //关闭时调用：此后不再投递事件
    void Detach()
    {
        m_wnd = NULL;
    }

    bool IsReady() const
    {
        return m_ready.load();
    }

private:
    volatile LONG     m_ref;     //引用计数
    HWND volatile     m_wnd;     //接收事件的窗口；关闭后为 NULL
    UINT              m_msg;     //事件的消息号
    std::atomic<bool> m_ready;   //是否收到过「可以播放」
};

CaptionVideoPlayer::CaptionVideoPlayer()
    : m_device(NULL)
    , m_context(NULL)
    , m_dxgiManager(NULL)
    , m_engine(NULL)
    , m_notify(NULL)
    , m_renderTex(NULL)
    , m_stagingTex(NULL)
    , m_texWidth(0)
    , m_texHeight(0)
    , m_lastPts(-1)
{
}

CaptionVideoPlayer::~CaptionVideoPlayer()
{
    Close();
}

bool CaptionVideoPlayer::Open(const std::wstring& path, HWND notifyWnd, UINT msg)
{
    Close();
    const UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION, &m_device,
                                   NULL, &m_context);
    if (FAILED(hr))
    {
        //没有可用的显卡驱动时退回软件渲染
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, NULL, 0, D3D11_SDK_VERSION, &m_device, NULL,
                               &m_context);
    }
    if (FAILED(hr))
    {
        LOG_ERROR("caption editor: D3D11CreateDevice failed, hr=0x%08lX.", static_cast<unsigned long>(hr));
        return false;
    }
    //媒体引擎在自己的线程上使用这台设备
    ID3D10Multithread* mt = NULL;
    if (SUCCEEDED(m_device->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&mt))))
    {
        mt->SetMultithreadProtected(TRUE);
        mt->Release();
    }
    UINT resetToken = 0;
    hr = MFCreateDXGIDeviceManager(&resetToken, &m_dxgiManager);
    if (SUCCEEDED(hr))
        hr = m_dxgiManager->ResetDevice(m_device, resetToken);
    if (FAILED(hr))
    {
        LOG_ERROR("caption editor: DXGI device manager failed, hr=0x%08lX.", static_cast<unsigned long>(hr));
        Close();
        return false;
    }
    m_notify = new CaptionPlayerNotify(notifyWnd, msg);
    IMFAttributes* attrs = NULL;
    MFCreateAttributes(&attrs, 3);
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_notify);
    attrs->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_dxgiManager);
    attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    IMFMediaEngineClassFactory* factory = NULL;
    hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateInstance(0, attrs, &m_engine);
    SafeRelease(factory);
    SafeRelease(attrs);
    if (FAILED(hr))
    {
        LOG_ERROR("caption editor: create media engine failed, hr=0x%08lX.", static_cast<unsigned long>(hr));
        Close();
        return false;
    }
    BSTR url = ::SysAllocString(path.c_str());
    hr = m_engine->SetSource(url);
    ::SysFreeString(url);
    if (FAILED(hr))
    {
        LOG_ERROR("caption editor: SetSource failed, hr=0x%08lX.", static_cast<unsigned long>(hr));
        Close();
        return false;
    }
    m_lastPts = -1;
    return true;
}

void CaptionVideoPlayer::Close()
{
    if (m_notify != NULL)
        m_notify->Detach();
    if (m_engine != NULL)
    {
        m_engine->Shutdown();
        SafeRelease(m_engine);
    }
    SafeRelease(m_notify);
    releaseTextures();
    SafeRelease(m_dxgiManager);
    SafeRelease(m_context);
    SafeRelease(m_device);
    m_lastPts = -1;
}

bool CaptionVideoPlayer::IsReady() const
{
    return m_engine != NULL && m_notify != NULL && m_notify->IsReady();
}

void CaptionVideoPlayer::Play()
{
    if (IsReady())
        m_engine->Play();
}

void CaptionVideoPlayer::Pause()
{
    if (IsReady())
        m_engine->Pause();
}

bool CaptionVideoPlayer::IsPlaying() const
{
    return IsReady() && m_engine->IsPaused() == FALSE && m_engine->IsEnded() == FALSE;
}

void CaptionVideoPlayer::Seek(int64_t ms)
{
    if (!IsReady())
        return;
    int64_t target = ms < 0 ? 0 : ms;
    const int64_t duration = DurationMs();
    if (duration > 0 && target > duration)
        target = duration;
    m_engine->SetCurrentTime(static_cast<double>(target) / kMsPerSecond);
}

int64_t CaptionVideoPlayer::PositionMs() const
{
    if (!IsReady())
        return 0;
    return static_cast<int64_t>(m_engine->GetCurrentTime() * kMsPerSecond);
}

int64_t CaptionVideoPlayer::DurationMs() const
{
    if (!IsReady())
        return 0;
    const double d = m_engine->GetDuration();
    //直播流的时长为无穷大，录像不会出现；保险起见按 0 处理
    if (d != d || d <= 0.0 || d > 1e9)
        return 0;
    return static_cast<int64_t>(d * kMsPerSecond);
}

bool CaptionVideoPlayer::VideoSize(uint32_t& width, uint32_t& height) const
{
    if (!IsReady())
        return false;
    DWORD w = 0;
    DWORD h = 0;
    if (FAILED(m_engine->GetNativeVideoSize(&w, &h)) || w == 0 || h == 0)
        return false;
    width = w;
    height = h;
    return true;
}

void CaptionVideoPlayer::SetMuted(bool muted)
{
    if (m_engine != NULL)
        m_engine->SetMuted(muted ? TRUE : FALSE);
}

RECT CaptionVideoPlayer::VideoRectIn(uint32_t width, uint32_t height) const
{
    RECT rc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    uint32_t vw = 0;
    uint32_t vh = 0;
    if (!VideoSize(vw, vh) || width == 0 || height == 0)
        return rc;
    //保持宽高比：按较紧的一边缩放，另一边居中
    if (static_cast<uint64_t>(width) * vh > static_cast<uint64_t>(height) * vw)
    {
        const LONG w = static_cast<LONG>(static_cast<uint64_t>(height) * vw / vh);
        rc.left = (static_cast<LONG>(width) - w) / 2;
        rc.right = rc.left + w;
    }
    else
    {
        const LONG h = static_cast<LONG>(static_cast<uint64_t>(width) * vh / vw);
        rc.top = (static_cast<LONG>(height) - h) / 2;
        rc.bottom = rc.top + h;
    }
    return rc;
}

bool CaptionVideoPlayer::ensureTextures(uint32_t width, uint32_t height)
{
    if (m_renderTex != NULL && m_texWidth == width && m_texHeight == height)
        return true;
    releaseTextures();
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&td, NULL, &m_renderTex)))
        return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(m_device->CreateTexture2D(&td, NULL, &m_stagingTex)))
    {
        releaseTextures();
        return false;
    }
    m_texWidth = width;
    m_texHeight = height;
    return true;
}

void CaptionVideoPlayer::releaseTextures()
{
    SafeRelease(m_renderTex);
    SafeRelease(m_stagingTex);
    m_texWidth = 0;
    m_texHeight = 0;
}

bool CaptionVideoPlayer::GrabFrame(uint32_t width, uint32_t height, std::vector<uint32_t>& bgra, bool force)
{
    if (!IsReady() || width == 0 || height == 0)
        return false;
    LONGLONG pts = 0;
    const bool newFrame = (m_engine->OnVideoStreamTick(&pts) == S_OK && pts != m_lastPts);
    const bool sizeChanged = (width != m_texWidth || height != m_texHeight);
    if (!newFrame && !force && !sizeChanged)
        return false;
    if (!ensureTextures(width, height))
        return false;
    const RECT dst = VideoRectIn(width, height);
    MFVideoNormalizedRect src = { 0.0f, 0.0f, 1.0f, 1.0f };
    MFARGB border = { 0, 0, 0, 255 };
    //补黑：TransferVideoFrame 只写目标矩形，先把整块纹理清成黑色
    ID3D11RenderTargetView* rtv = NULL;
    if (SUCCEEDED(m_device->CreateRenderTargetView(m_renderTex, NULL, &rtv)))
    {
        const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_context->ClearRenderTargetView(rtv, black);
        rtv->Release();
    }
    if (FAILED(m_engine->TransferVideoFrame(m_renderTex, &src, &dst, &border)))
        return false;
    if (newFrame)
        m_lastPts = pts;
    m_context->CopyResource(m_stagingTex, m_renderTex);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    bgra.resize(static_cast<size_t>(width) * height);
    const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height; ++y)
    {
        memcpy(&bgra[static_cast<size_t>(y) * width], srcRow, static_cast<size_t>(width) * kBytesPerPixel);
        srcRow += mapped.RowPitch;
    }
    m_context->Unmap(m_stagingTex, 0);
    return true;
}

} // namespace caption
