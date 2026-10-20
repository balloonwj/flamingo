/**
 *  远程桌面 DXGI Desktop Duplication 抓取器（被控端）实现，DxgiScreenCapturer.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "DxgiScreenCapturer.h"
#include <stdio.h>
#include "../../Source/Common/Log/IULog.h"   // LOG_WARNING：不可恢复的失败记下错误码
#include "CaptureFallbackPolicy.h"           // ClassifyAcquireResult：AcquireNextFrame 返回值的归类
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace remotedesktop {

//调试抓帧的最多重试次数与每次间隔：首帧常因 LastPresentTime==0（无更新）返回 Timeout，
//循环几次（期间动一下鼠标就有更新）拿到一帧即可。
static const int kDebugMaxTries     = 40;
static const int kDebugTryIntervalMs = 30;

CDxgiScreenCapturer::CDxgiScreenCapturer()
    : m_holdingFrame(false)
    , m_mapped(false)
    , m_mappedFastlane(false)
    , m_fastlane(false)
    , m_width(0)
    , m_height(0)
    , m_stagingW(0)
    , m_stagingH(0)
{
}

CDxgiScreenCapturer::~CDxgiScreenCapturer()
{
    cleanup();
}

void CDxgiScreenCapturer::cleanup()
{
    releaseFrameIfHolding();
    m_stagingSurf = NULL;
    m_staging = NULL;
    m_dupl = NULL;
    m_context = NULL;
    m_device = NULL;
}

bool CDxgiScreenCapturer::Init()
{
    //1) DXGI 工厂
    CComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr))
        return false;

    //2) 第 0 个适配器（多显卡选择留后续增量）
    CComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr))
        return false;

    //3) 选目标 output：优先桌面坐标左上为 (0,0) 的主显示器，否则用第 0 个
    CComPtr<IDXGIOutput> chosen;
    UINT i = 0;
    for (;;)
    {
        CComPtr<IDXGIOutput> out;
        HRESULT er = adapter->EnumOutputs(i, &out);
        if (er == DXGI_ERROR_NOT_FOUND)
            break;
        if (SUCCEEDED(er))
        {
            DXGI_OUTPUT_DESC od;
            if (SUCCEEDED(out->GetDesc(&od)))
            {
                bool primary = (od.DesktopCoordinates.left == 0 && od.DesktopCoordinates.top == 0);
                if (chosen == NULL || primary)
                {
                    chosen = out;
                    m_width = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
                    m_height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
                }
                if (primary)
                    break;
            }
        }
        ++i;
    }
    if (chosen == NULL)
        return false;

    //4) Desktop Duplication 需要 IDXGIOutput1
    CComPtr<IDXGIOutput1> output1;
    hr = chosen->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    if (FAILED(hr))
        return false;

    //5) 建 D3D11 设备：显式给了 adapter，driver type 必须 UNKNOWN；flags/feature levels 让系统自选
    D3D_FEATURE_LEVEL fl;
    hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
                           D3D11_SDK_VERSION, &m_device, &fl, &m_context);
    if (FAILED(hr))
    {
        LOG_WARNING("CDxgiScreenCapturer: D3D11CreateDevice failed, hr=0x%08x.", static_cast<unsigned>(hr));
        return false;
    }

    //6) 拿到桌面复制对象
    hr = output1->DuplicateOutput(m_device, &m_dupl);
    if (FAILED(hr))
    {
        LOG_WARNING("CDxgiScreenCapturer: DuplicateOutput failed, hr=0x%08x.", static_cast<unsigned>(hr));
        return false;
    }

    //7) 读 desc：桌面镜像是否已在系统内存（fastlane 可免 staging 拷贝）
    DXGI_OUTDUPL_DESC dd;
    memset(&dd, 0, sizeof(dd));
    m_dupl->GetDesc(&dd);
    m_fastlane = (dd.DesktopImageInSystemMemory == TRUE);
    if (m_width <= 0 || m_height <= 0)
    {
        m_width = (int)dd.ModeDesc.Width;
        m_height = (int)dd.ModeDesc.Height;
    }
    return true;
}

void CDxgiScreenCapturer::releaseFrameIfHolding()
{
    if (m_mapped)
    {
        if (m_mappedFastlane)
        {
            if (m_dupl != NULL)
                m_dupl->UnMapDesktopSurface();
        }
        else if (m_stagingSurf != NULL)
        {
            m_stagingSurf->Unmap();
        }
        m_mapped = false;
    }
    if (m_holdingFrame)
    {
        if (m_dupl != NULL)
            m_dupl->ReleaseFrame();
        m_holdingFrame = false;
    }
}

bool CDxgiScreenCapturer::ensureStaging(ID3D11Texture2D* frameTex)
{
    D3D11_TEXTURE2D_DESC desc;
    frameTex->GetDesc(&desc);

    //尺寸不变则复用已有 staging（避免 rustdesk 那样每帧重建）
    if (m_staging != NULL && (int)desc.Width == m_stagingW && (int)desc.Height == m_stagingH)
        return true;

    m_stagingSurf = NULL;
    m_staging = NULL;

    //改成 CPU 可读的 staging texture
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&desc, NULL, &m_staging);
    if (FAILED(hr))
        return false;
    m_staging->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_MAXIMUM);   //防显存驱逐

    hr = m_staging->QueryInterface(__uuidof(IDXGISurface), (void**)&m_stagingSurf);
    if (FAILED(hr))
        return false;

    m_stagingW = (int)desc.Width;
    m_stagingH = (int)desc.Height;
    return true;
}

CaptureResult CDxgiScreenCapturer::CaptureFrame(UINT timeoutMs, FrameView& out)
{
    if (m_dupl == NULL)
        return kCaptureFatal;

    //先释放上一帧（DXGI 同时只能持一帧；Map 期间不能 Release）
    releaseFrameIfHolding();

    DXGI_OUTDUPL_FRAME_INFO info;
    memset(&info, 0, sizeof(info));
    CComPtr<IDXGIResource> res;
    HRESULT hr = m_dupl->AcquireNextFrame(timeoutMs, &info, &res);
    //超时以外的失败都按「需要重建」交给降级策略（2026-10-17）：分辨率变化时实测返回 DXGI_ERROR_INVALID_CALL，
    //原先当作不可恢复、直接降级 GDI，本次会话从此只用 GDI
    const CaptureResult acquired = CCaptureFallbackPolicy::ClassifyAcquireResult(hr);
    if (acquired != kCaptureOk)
    {
        if (acquired == kCaptureAccessLost && hr != DXGI_ERROR_ACCESS_LOST)
            LOG_WARNING("CDxgiScreenCapturer: AcquireNextFrame failed, hr=0x%08x, rebuild the duplication.",
                        static_cast<unsigned>(hr));
        return acquired;
    }
    m_holdingFrame = true;

    //LastPresentTime==0：本次只有鼠标移动等元数据、无新画面，当无新帧（持帧下次释放）
    if (info.LastPresentTime.QuadPart == 0)
        return kCaptureTimeout;

    DXGI_MAPPED_RECT mapped;
    memset(&mapped, 0, sizeof(mapped));

    if (m_fastlane)
    {
        //桌面镜像在系统内存：直接映射，零 GPU→CPU 拷贝
        hr = m_dupl->MapDesktopSurface(&mapped);
        if (FAILED(hr))
        {
            LOG_WARNING("CDxgiScreenCapturer: MapDesktopSurface failed, hr=0x%08x.", static_cast<unsigned>(hr));
            return kCaptureFatal;
        }
        m_mapped = true;
        m_mappedFastlane = true;
    }
    else
    {
        //常规路径：QI 成纹理 → 拷进 staging → Map staging
        CComPtr<ID3D11Texture2D> frameTex;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&frameTex);
        if (FAILED(hr))
        {
            LOG_WARNING("CDxgiScreenCapturer: frame QueryInterface(ID3D11Texture2D) failed, hr=0x%08x.",
                        static_cast<unsigned>(hr));
            return kCaptureFatal;
        }
        if (!ensureStaging(frameTex))
        {
            LOG_WARNING("CDxgiScreenCapturer: create staging texture failed, frame %dx%d.", m_stagingW, m_stagingH);
            return kCaptureFatal;
        }
        m_context->CopyResource(m_staging, frameTex);
        hr = m_stagingSurf->Map(&mapped, DXGI_MAP_READ);
        if (FAILED(hr))
        {
            LOG_WARNING("CDxgiScreenCapturer: staging Map failed, hr=0x%08x.", static_cast<unsigned>(hr));
            return kCaptureFatal;
        }
        m_mapped = true;
        m_mappedFastlane = false;
    }

    out.m_data   = (const BYTE*)mapped.pBits;
    out.m_stride = (int)mapped.Pitch;   //可能 > width*4，逐行须按它走
    out.m_width  = m_width;
    out.m_height = m_height;
    return kCaptureOk;
    //Unmap / ReleaseFrame 延到下次 CaptureFrame 开头的 releaseFrameIfHolding
}

int CDxgiScreenCapturer::ScreenWidth() const
{
    return m_width;
}

int CDxgiScreenCapturer::ScreenHeight() const
{
    return m_height;
}

//把一帧 BGRA32（top-down）写成 32 位 bmp（biHeight 取负保持 top-down，逐行跳过 stride padding）。
static bool saveBgra32Bmp(const FrameView& fv, const wchar_t* path)
{
    if (fv.m_data == NULL || fv.m_width <= 0 || fv.m_height <= 0)
        return false;

    int rowBytes = fv.m_width * 4;
    int imgBytes = rowBytes * fv.m_height;

    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));

    ih.biSize        = sizeof(ih);
    ih.biWidth       = fv.m_width;
    ih.biHeight      = -fv.m_height;   //负 = top-down，与 BGRA 数据方向一致
    ih.biPlanes      = 1;
    ih.biBitCount    = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage   = (DWORD)imgBytes;

    fh.bfType    = 0x4D42;   // 'BM'
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize    = fh.bfOffBits + imgBytes;

    FILE* fp = _wfopen(path, L"wb");
    if (fp == NULL)
        return false;

    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    //逐行拷贝，跳过行尾 stride padding
    for (int y = 0; y < fv.m_height; ++y)
    {
        const BYTE* row = fv.m_data + (size_t)y * fv.m_stride;
        fwrite(row, rowBytes, 1, fp);
    }
    fclose(fp);
    return true;
}

// i18n-ignore-begin: 只供远程桌面调试入口自测抓屏，不是界面文字
bool CDxgiScreenCapturer::DebugCaptureToBmp(const wchar_t* path, std::wstring& msg)
{
    CDxgiScreenCapturer cap;
    if (!cap.Init())
    {
        msg = L"DXGI 初始化失败（Init 返回 false）——可能显卡 / 虚拟机 / RDP 不支持 Desktop Duplication。";
        return false;
    }

    FrameView fv;
    CaptureResult r = kCaptureTimeout;
    for (int t = 0; t < kDebugMaxTries; ++t)
    {
        r = cap.CaptureFrame(200, fv);
        if (r == kCaptureOk)
            break;
        if (r == kCaptureAccessLost || r == kCaptureFatal)
            break;
        ::Sleep(kDebugTryIntervalMs);
    }

    if (r != kCaptureOk)
    {
        wchar_t buf[128];
        swprintf(buf, 128, L"抓帧未成功，结果码=%d（0=Ok 1=Timeout 2=AccessLost 3=Fatal）。", (int)r);
        msg = buf;
        return false;
    }

    //此时帧仍处于映射态（cap 未析构、未再抓下一帧），像素有效，立即写出
    if (!saveBgra32Bmp(fv, path))
    {
        msg = L"抓帧成功但写 bmp 文件失败。";
        return false;
    }

    wchar_t buf[512];
    swprintf(buf, 512, L"DXGI 抓帧成功（%d×%d），已存到：\n%s", fv.m_width, fv.m_height, path);
    msg = buf;
    return true;
}
// i18n-ignore-end

} // namespace remotedesktop
