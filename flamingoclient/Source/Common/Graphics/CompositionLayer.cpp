/**
 *  用 DirectComposition 显示逐像素透明的图层，说明见 CompositionLayer.h。
 *  2026-10-05 由 FlamingoMedia/Share/ShareCompositionLayer.cpp 挪到此处，供 exe 与 FlamingoMedia 共用。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "CompositionLayer.h"

#include <d3d11.h>
#include <dxgi1_2.h>

//dcomp.h 只在 NTDDI_VERSION >= NTDDI_WIN8 时声明接口，而各工程的目标系统为 Windows 7（stdafx.h 的 _WIN32_WINNT）。
//这里只为本文件放开接口声明；唯一的函数入口 DCompositionCreateDevice 运行期从 dcomp.dll 取，不产生静态导入，
//Windows 7 上各模块照常加载，Attach 返回 false
#pragma push_macro("NTDDI_VERSION")
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000   // NTDDI_WIN8
#include <dcomp.h>
#pragma pop_macro("NTDDI_VERSION")

#include "../Log/IULog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace graphics {

namespace {

//交换链的缓冲区个数：翻转模式至少两个
const UINT kSwapChainBuffers = 2;
//每像素字节数（BGRA）
const int kBytesPerPixel = 4;

//dcomp.dll 导出的 DCompositionCreateDevice
typedef HRESULT (WINAPI* DCompositionCreateDeviceFn)(IDXGIDevice* dxgiDevice, REFIID iid, void** dcompositionDevice);

//释放 COM 指针并置空
template <typename T>
void releaseCom(T*& p)
{
    if (p != NULL)
    {
        p->Release();
        p = NULL;
    }
}

//按驱动类型建 Direct3D 11 设备；要求支持 BGRA（交换链与 DirectComposition 都用 BGRA）
bool createDevice(D3D_DRIVER_TYPE type, ID3D11Device** device, ID3D11DeviceContext** context)
{
    const HRESULT hr = ::D3D11CreateDevice(NULL, type, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                           D3D11_SDK_VERSION, device, NULL, context);
    return SUCCEEDED(hr) && *device != NULL && *context != NULL;
}

} // namespace

CCompositionLayer::CCompositionLayer()
    : m_dcompDll(NULL)
    , m_device(NULL)
    , m_context(NULL)
    , m_swapChain(NULL)
    , m_dcomp(NULL)
    , m_target(NULL)
    , m_visual(NULL)
    , m_width(0)
    , m_height(0)
{
}

CCompositionLayer::~CCompositionLayer()
{
    Detach();
}

bool CCompositionLayer::Attach(HWND hwnd)
{
    Detach();
    if (hwnd == NULL || !::IsWindow(hwnd))
        return false;
    m_dcompDll = ::LoadLibraryW(L"dcomp.dll");
    if (m_dcompDll == NULL)
    {
        LOG_INFO("composition layer: dcomp.dll not available, composition layer disabled.");
        return false;
    }
    DCompositionCreateDeviceFn createDcomp =
        reinterpret_cast<DCompositionCreateDeviceFn>(::GetProcAddress(m_dcompDll, "DCompositionCreateDevice"));
    if (createDcomp == NULL)
    {
        Detach();
        return false;
    }
    //先用显卡，建不出时用 WARP 软件渲染（远程桌面会话、没有驱动的虚拟机）
    if (!createDevice(D3D_DRIVER_TYPE_HARDWARE, &m_device, &m_context))
    {
        releaseCom(m_context);
        releaseCom(m_device);
        if (!createDevice(D3D_DRIVER_TYPE_WARP, &m_device, &m_context))
        {
            LOG_WARNING("composition layer: cannot create a d3d11 device.");
            Detach();
            return false;
        }
    }
    IDXGIDevice* dxgiDevice = NULL;
    HRESULT hr = m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = createDcomp(dxgiDevice, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&m_dcomp));
    releaseCom(dxgiDevice);
    //内容放在窗口最上层（topmost = TRUE）；窗口本身没有重定向位图，看到的只有这一层
    if (SUCCEEDED(hr))
        hr = m_dcomp->CreateTargetForHwnd(hwnd, TRUE, &m_target);
    if (SUCCEEDED(hr))
        hr = m_dcomp->CreateVisual(&m_visual);
    if (SUCCEEDED(hr))
        hr = m_target->SetRoot(m_visual);
    if (SUCCEEDED(hr))
        hr = m_dcomp->Commit();
    if (FAILED(hr))
    {
        LOG_WARNING("composition layer: setup failed, hr=0x%08X.", static_cast<unsigned>(hr));
        Detach();
        return false;
    }
    LOG_INFO("composition layer: attached.");
    return true;
}

bool CCompositionLayer::ensureSwapChain(int width, int height)
{
    if (m_swapChain != NULL && width == m_width && height == m_height)
        return true;
    if (m_swapChain != NULL)
    {
        //尺寸变了：缓冲区重建，交换链与可视对象的关联不变
        const HRESULT hr = m_swapChain->ResizeBuffers(kSwapChainBuffers, static_cast<UINT>(width),
                                                      static_cast<UINT>(height), DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(hr))
        {
            LOG_WARNING("composition layer: resize failed, hr=0x%08X.", static_cast<unsigned>(hr));
            return false;
        }
        m_width = width;
        m_height = height;
        return true;
    }
    IDXGIDevice* dxgiDevice = NULL;
    IDXGIAdapter* adapter = NULL;
    IDXGIFactory2* factory = NULL;
    HRESULT hr = m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr))
    {
        DXGI_SWAP_CHAIN_DESC1 desc;
        memset(&desc, 0, sizeof(desc));
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kSwapChainBuffers;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        //预乘透明度：与 GDI+ 的 PARGB 图层、AlphaBlend 的约定相同
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = factory->CreateSwapChainForComposition(m_device, &desc, NULL, &m_swapChain);
    }
    releaseCom(factory);
    releaseCom(adapter);
    releaseCom(dxgiDevice);
    if (SUCCEEDED(hr))
        hr = m_visual->SetContent(m_swapChain);
    if (SUCCEEDED(hr))
        hr = m_dcomp->Commit();
    if (FAILED(hr))
    {
        LOG_WARNING("composition layer: swap chain failed, hr=0x%08X.", static_cast<unsigned>(hr));
        releaseCom(m_swapChain);
        return false;
    }
    m_width = width;
    m_height = height;
    return true;
}

bool CCompositionLayer::Present(const uint8_t* bgra, int width, int height)
{
    if (!IsAttached() || bgra == NULL || width <= 0 || height <= 0)
        return false;
    if (!ensureSwapChain(width, height))
        return false;
    ID3D11Texture2D* buffer = NULL;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
    if (FAILED(hr) || buffer == NULL)
        return false;
    m_context->UpdateSubresource(buffer, 0, NULL, bgra, static_cast<UINT>(width * kBytesPerPixel), 0);
    releaseCom(buffer);
    hr = m_swapChain->Present(0, 0);
    if (FAILED(hr))
    {
        LOG_WARNING("composition layer: present failed, hr=0x%08X.", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

void CCompositionLayer::Detach()
{
    if (m_visual != NULL)
        m_visual->SetContent(NULL);
    if (m_target != NULL)
        m_target->SetRoot(NULL);
    if (m_dcomp != NULL)
        m_dcomp->Commit();
    releaseCom(m_visual);
    releaseCom(m_target);
    releaseCom(m_swapChain);
    releaseCom(m_dcomp);
    releaseCom(m_context);
    releaseCom(m_device);
    if (m_dcompDll != NULL)
    {
        ::FreeLibrary(m_dcompDll);
        m_dcompDll = NULL;
    }
    m_width = 0;
    m_height = 0;
}

bool CCompositionLayer::IsAttached() const
{
    return m_dcomp != NULL && m_visual != NULL && m_device != NULL;
}

} // namespace graphics
