/**
 *  屏幕共享的窗口采集器（Windows.Graphics.Capture），WgcWindowCapturer.cpp
 *
 *  只用 C++11 语法、经 WRL 的 ComPtr 调用 Windows.Graphics.Capture 的 ABI 接口（第 0 期 0b 实测可行：被遮挡窗口、
 *  GPU 渲染窗口都能采到，窗口改变大小后约 60 ms 收到新尺寸的帧）。接口说明见 ShareCapturer.h。
 *
 *  DLL 必须能在 Win7 上载入，所以 Win10 才有的函数一律运行时取用，不做静态导入：
 *  combase.dll 的 RoGetActivationFactory / WindowsCreateStringReference，d3d11.dll 的
 *  CreateDirect3D11DeviceFromDXGIDevice。本翻译单元不包含 stdafx.h，自己把系统版本宏定到 Win10，只为让 WinRT
 *  头文件声明出需要的类型；对外接口只用普通类型，与其它翻译单元的系统版本宏无关。
 *
 *  帧池用自由线程模式（CreateFreeThreaded），在采集线程上轮询 TryGetNextFrame；采集线程须已初始化 COM（MTA）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00   //Windows 10：只影响本翻译单元能看到的系统声明，运行期接口仍按需动态取用
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00

#include "ShareCapturer.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <winstring.h>
#include <roapi.h>
#include <windows.graphics.capture.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <string.h>
#include <vector>
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;
namespace WGC = ABI::Windows::Graphics::Capture;
namespace WGDX = ABI::Windows::Graphics::DirectX;
namespace WGD3D = ABI::Windows::Graphics::DirectX::Direct3D11;

namespace screenshare {

namespace {

//帧池里的缓冲个数：两个足够（一个在用、一个在填）
const INT32 kFramePoolBuffers = 2;
//等新帧时每次轮询之间的间隔，单位毫秒
const DWORD kPollIntervalMs = 2;
//每像素字节数
const int kBytesPerPixel = 4;

typedef HRESULT(WINAPI* RoGetActivationFactoryFn)(HSTRING, REFIID, void**);
typedef HRESULT(WINAPI* WindowsCreateStringReferenceFn)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
typedef HRESULT(WINAPI* CreateD3DDeviceFromDxgiFn)(IDXGIDevice*, IInspectable**);

//运行期取用的 Win10 函数
struct WinRtApi
{
    RoGetActivationFactoryFn       m_getFactory;       //combase!RoGetActivationFactory
    WindowsCreateStringReferenceFn m_stringRef;        //combase!WindowsCreateStringReference
    CreateD3DDeviceFromDxgiFn      m_wrapD3dDevice;    //d3d11!CreateDirect3D11DeviceFromDXGIDevice

    WinRtApi()
        : m_getFactory(NULL)
        , m_stringRef(NULL)
        , m_wrapD3dDevice(NULL)
    {
        //combase.dll 与 d3d11.dll 在用到它们的进程里本来就会载入；这里取了句柄不释放，进程内只取一次
        HMODULE combase = ::LoadLibraryW(L"combase.dll");
        HMODULE d3d11 = ::LoadLibraryW(L"d3d11.dll");
        if (combase != NULL)
        {
            m_getFactory = reinterpret_cast<RoGetActivationFactoryFn>(::GetProcAddress(combase, "RoGetActivationFactory"));
            m_stringRef = reinterpret_cast<WindowsCreateStringReferenceFn>(
                ::GetProcAddress(combase, "WindowsCreateStringReference"));
        }
        if (d3d11 != NULL)
            m_wrapD3dDevice = reinterpret_cast<CreateD3DDeviceFromDxgiFn>(
                ::GetProcAddress(d3d11, "CreateDirect3D11DeviceFromDXGIDevice"));
    }

    bool ok() const
    {
        return m_getFactory != NULL && m_stringRef != NULL && m_wrapD3dDevice != NULL;
    }

    //按运行时类名取激活工厂
    HRESULT getFactory(const wchar_t* className, REFIID iid, void** out) const
    {
        HSTRING_HEADER header;
        HSTRING name = NULL;
        HRESULT hr = m_stringRef(className, static_cast<UINT32>(wcslen(className)), &header, &name);
        if (FAILED(hr))
            return hr;
        return m_getFactory(name, iid, out);
    }
};

const WinRtApi& winRtApi()
{
    static WinRtApi s_api;
    return s_api;
}

void closeObject(IUnknown* obj)
{
    if (obj == NULL)
        return;
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (SUCCEEDED(obj->QueryInterface(IID_PPV_ARGS(&closable))))
        closable->Close();
}

class WgcWindowCapturer : public IShareCapturer
{
public:
    explicit WgcWindowCapturer(HWND hwnd)
        : m_hwnd(hwnd)
        , m_width(0)
        , m_height(0)
        , m_stagingW(0)
        , m_stagingH(0)
    {
        m_poolSize.Width = 0;
        m_poolSize.Height = 0;
    }

    virtual ~WgcWindowCapturer()
    {
        Stop();
    }

    virtual bool Start()
    {
        Stop();
        const WinRtApi& api = winRtApi();
        if (!api.ok())
        {
            LOG_ERROR("screenshare wgc: WinRT capture API unavailable on this system.");
            return false;
        }
        if (!::IsWindow(m_hwnd))
            return false;

        HRESULT hr = ::D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL,
                                         0, D3D11_SDK_VERSION, &m_d3d, NULL, &m_context);
        if (FAILED(hr))
        {
            LOG_ERROR("screenshare wgc: D3D11CreateDevice failed, hr=0x%08x.", static_cast<unsigned>(hr));
            return false;
        }
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IInspectable> inspectable;
        if (FAILED(m_d3d.As(&dxgi)) || FAILED(api.m_wrapD3dDevice(dxgi.Get(), &inspectable)) ||
            FAILED(inspectable.As(&m_device)))
        {
            LOG_ERROR("screenshare wgc: wrap D3D11 device failed.");
            return false;
        }

        ComPtr<IGraphicsCaptureItemInterop> interop;
        hr = api.getFactory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, IID_PPV_ARGS(&interop));
        if (FAILED(hr))
        {
            LOG_ERROR("screenshare wgc: GraphicsCaptureItem factory unavailable, hr=0x%08x.", static_cast<unsigned>(hr));
            return false;
        }
        hr = interop->CreateForWindow(m_hwnd, __uuidof(WGC::IGraphicsCaptureItem),
                                      reinterpret_cast<void**>(m_item.GetAddressOf()));
        if (FAILED(hr))
        {
            LOG_ERROR("screenshare wgc: CreateForWindow failed, hr=0x%08x, hwnd=%p.", static_cast<unsigned>(hr),
                      m_hwnd);
            return false;
        }
        m_item->get_Size(&m_poolSize);

        ComPtr<WGC::IDirect3D11CaptureFramePoolStatics2> statics;
        hr = api.getFactory(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool, IID_PPV_ARGS(&statics));
        if (FAILED(hr))
            return false;
        hr = statics->CreateFreeThreaded(m_device.Get(), WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
                                         kFramePoolBuffers, m_poolSize, &m_pool);
        if (FAILED(hr))
            return false;
        hr = m_pool->CreateCaptureSession(m_item.Get(), &m_session);
        if (FAILED(hr))
            return false;
        //采集画面里带上鼠标指针（Session2 接口；没有时系统默认也带指针）
        ComPtr<WGC::IGraphicsCaptureSession2> session2;
        if (SUCCEEDED(m_session.As(&session2)))
            session2->put_IsCursorCaptureEnabled(TRUE);
        hr = m_session->StartCapture();
        if (FAILED(hr))
        {
            LOG_ERROR("screenshare wgc: StartCapture failed, hr=0x%08x.", static_cast<unsigned>(hr));
            return false;
        }
        LOG_INFO("screenshare wgc: capturing window %p, %dx%d.", m_hwnd, m_poolSize.Width, m_poolSize.Height);
        return true;
    }

    virtual int Capture(UINT timeoutMs, ShareCaptureFrame& out)
    {
        if (!m_pool)
            return share_capture_fatal;
        const DWORD start = ::GetTickCount();
        while (true)
        {
            if (!::IsWindow(m_hwnd))
                return share_capture_closed;
            //最小化期间系统不送新帧，直接报告没有新帧，观看端据此提示
            if (::IsIconic(m_hwnd))
                return share_capture_no_frame;

            ComPtr<WGC::IDirect3D11CaptureFrame> frame;
            if (SUCCEEDED(m_pool->TryGetNextFrame(&frame)) && frame)
                return copyFrame(frame.Get(), out);
            if (::GetTickCount() - start >= timeoutMs)
                return share_capture_no_frame;
            ::Sleep(kPollIntervalMs);
        }
    }

    virtual void Stop()
    {
        closeObject(m_session.Get());
        m_session.Reset();
        closeObject(m_pool.Get());
        m_pool.Reset();
        m_item.Reset();
        m_device.Reset();
        m_staging.Reset();
        m_context.Reset();
        m_d3d.Reset();
        m_stagingW = 0;
        m_stagingH = 0;
    }

    virtual bool IsMinimized() const
    {
        return ::IsWindow(m_hwnd) && ::IsIconic(m_hwnd);
    }

    virtual RECT SourceRect() const
    {
        return GetShareWindowBounds(m_hwnd);
    }

private:
    int copyFrame(WGC::IDirect3D11CaptureFrame* frame, ShareCaptureFrame& out)
    {
        ABI::Windows::Graphics::SizeInt32 content;
        frame->get_ContentSize(&content);
        ComPtr<WGD3D::IDirect3DSurface> surface;
        frame->get_Surface(&surface);
        ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        ComPtr<ID3D11Texture2D> tex;
        if (!surface || FAILED(surface.As(&access)) || FAILED(access->GetInterface(IID_PPV_ARGS(&tex))))
        {
            closeObject(frame);
            return share_capture_fatal;
        }
        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);
        if (!ensureStaging(desc))
        {
            closeObject(frame);
            return share_capture_fatal;
        }
        m_context->CopyResource(m_staging.Get(), tex.Get());
        closeObject(frame);

        //内容尺寸可能小于缓冲尺寸（窗口刚缩小），只取内容部分
        const int w = (static_cast<int>(desc.Width) < content.Width) ? static_cast<int>(desc.Width) : content.Width;
        const int h = (static_cast<int>(desc.Height) < content.Height) ? static_cast<int>(desc.Height) : content.Height;
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (w <= 0 || h <= 0 || FAILED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return share_capture_no_frame;
        m_width = w;
        m_height = h;
        m_buffer.resize(static_cast<size_t>(w) * h * kBytesPerPixel);
        for (int y = 0; y < h; ++y)
            memcpy(&m_buffer[static_cast<size_t>(y) * w * kBytesPerPixel],
                   static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                   static_cast<size_t>(w) * kBytesPerPixel);
        m_context->Unmap(m_staging.Get(), 0);

        //窗口尺寸变了：按新尺寸重建帧池，之后的帧不再被裁掉或留黑边
        if (content.Width != m_poolSize.Width || content.Height != m_poolSize.Height)
        {
            m_poolSize = content;
            m_pool->Recreate(m_device.Get(), WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized, kFramePoolBuffers,
                             content);
        }
        out.m_data = m_buffer.data();
        out.m_stride = w * kBytesPerPixel;
        out.m_width = w;
        out.m_height = h;
        return share_capture_ok;
    }

    bool ensureStaging(const D3D11_TEXTURE2D_DESC& src)
    {
        if (m_staging && static_cast<int>(src.Width) == m_stagingW && static_cast<int>(src.Height) == m_stagingH)
            return true;
        m_staging.Reset();
        D3D11_TEXTURE2D_DESC desc = src;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(m_d3d->CreateTexture2D(&desc, NULL, &m_staging)))
            return false;
        m_stagingW = static_cast<int>(src.Width);
        m_stagingH = static_cast<int>(src.Height);
        return true;
    }

    HWND                                      m_hwnd;        //被共享的窗口
    ComPtr<ID3D11Device>                      m_d3d;         //采集用 D3D11 设备
    ComPtr<ID3D11DeviceContext>               m_context;     //立即上下文
    ComPtr<WGD3D::IDirect3DDevice>            m_device;      //WinRT 包装的设备
    ComPtr<WGC::IGraphicsCaptureItem>         m_item;        //采集目标
    ComPtr<WGC::IDirect3D11CaptureFramePool>  m_pool;        //帧池
    ComPtr<WGC::IGraphicsCaptureSession>      m_session;     //采集会话
    ComPtr<ID3D11Texture2D>                   m_staging;     //CPU 可读的中转纹理（尺寸不变时复用）
    ABI::Windows::Graphics::SizeInt32         m_poolSize;    //帧池当前尺寸
    std::vector<uint8_t>                      m_buffer;      //最近一帧的像素（BGRA，行跨度 = 宽 × 4）
    int                                       m_width;       //最近一帧的宽，单位像素
    int                                       m_height;      //最近一帧的高，单位像素
    int                                       m_stagingW;    //中转纹理的宽
    int                                       m_stagingH;    //中转纹理的高
};

} // namespace

IShareCapturer* CreateWindowCapturer(HWND hwnd)
{
    return new WgcWindowCapturer(hwnd);
}

bool IsWindowCaptureSupported()
{
    return winRtApi().ok();
}

} // namespace screenshare
