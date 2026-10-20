/**
 *  屏幕共享的显示器采集器（DXGI Desktop Duplication），DxgiOutputCapturer.cpp
 *
 *  与远程桌面的 CDxgiScreenCapturer 不同：可指定显卡与输出（第 0 期 0c 在本机实测有 6 个显卡、只有 0 号有输出，
 *  所以必须按显示器找到对应的那一块），每帧拷进自己的 DIB 并画上鼠标指针（Desktop Duplication 的画面不含指针）。
 *  远程桌面原有的采集器不动，保持其行为不变。接口说明见 ShareCapturer.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareCapturer.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <string.h>
#include <vector>
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace screenshare {

namespace {

//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBytesPerPixel = 4;

template <typename T>
void safeRelease(T*& p)
{
    if (p != NULL)
    {
        p->Release();
        p = NULL;
    }
}

class DxgiOutputCapturer : public IShareCapturer
{
public:
    DxgiOutputCapturer(int adapter, int output, const RECT& monitorRect)
        : m_adapterIndex(adapter)
        , m_outputIndex(output)
        , m_monitorRect(monitorRect)
        , m_device(NULL)
        , m_context(NULL)
        , m_dupl(NULL)
        , m_staging(NULL)
        , m_holdingFrame(false)
        , m_width(0)
        , m_height(0)
        , m_dib(NULL)
        , m_dibDc(NULL)
        , m_dibOld(NULL)
        , m_dibBits(NULL)
        , m_hasFrame(false)
        , m_cursorCached(NULL)
    {
        memset(&m_cursorHotspot, 0, sizeof(m_cursorHotspot));
    }

    virtual ~DxgiOutputCapturer()
    {
        Stop();
    }

    virtual bool Start()
    {
        Stop();
        IDXGIFactory1* factory = NULL;
        if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
            return false;
        IDXGIAdapter1* adapter = NULL;
        HRESULT hr = factory->EnumAdapters1(static_cast<UINT>(m_adapterIndex), &adapter);
        safeRelease(factory);
        if (FAILED(hr) || adapter == NULL)
        {
            LOG_ERROR("screenshare dxgi: adapter %d not found.", m_adapterIndex);
            return false;
        }
        IDXGIOutput* output = NULL;
        hr = adapter->EnumOutputs(static_cast<UINT>(m_outputIndex), &output);
        if (FAILED(hr) || output == NULL)
        {
            LOG_ERROR("screenshare dxgi: output %d on adapter %d not found.", m_outputIndex, m_adapterIndex);
            safeRelease(adapter);
            return false;
        }
        //每次建立都重新读取显示器在虚拟桌面上的矩形（2026-10-12）：分辨率变化后采集器重建，指针位置、控制坐标、
        //标注与边框都按新矩形换算；之前一直沿用开始共享时的矩形
        DXGI_OUTPUT_DESC outDesc;
        if (SUCCEEDED(output->GetDesc(&outDesc)))
            m_monitorRect = outDesc.DesktopCoordinates;
        IDXGIOutput1* output1 = NULL;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
        safeRelease(output);
        if (FAILED(hr))
        {
            safeRelease(adapter);
            return false;
        }

        //显式给了显卡，驱动类型必须是 UNKNOWN
        D3D_FEATURE_LEVEL level;
        hr = ::D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &m_device,
                                 &level, &m_context);
        safeRelease(adapter);
        if (FAILED(hr))
        {
            safeRelease(output1);
            LOG_ERROR("screenshare dxgi: D3D11CreateDevice failed, hr=0x%08x.", static_cast<unsigned>(hr));
            return false;
        }
        hr = output1->DuplicateOutput(m_device, &m_dupl);
        safeRelease(output1);
        if (FAILED(hr))
        {
            LOG_ERROR("screenshare dxgi: DuplicateOutput failed, hr=0x%08x.", static_cast<unsigned>(hr));
            Stop();
            return false;
        }
        DXGI_OUTDUPL_DESC desc;
        m_dupl->GetDesc(&desc);
        m_width = static_cast<int>(desc.ModeDesc.Width);
        m_height = static_cast<int>(desc.ModeDesc.Height);
        LOG_INFO("screenshare dxgi: capturing adapter %d output %d, %dx%d.", m_adapterIndex, m_outputIndex, m_width,
                 m_height);
        return ensureDib();
    }

    virtual int Capture(UINT timeoutMs, ShareCaptureFrame& out)
    {
        if (m_dupl == NULL)
            return share_capture_fatal;
        releaseFrame();

        DXGI_OUTDUPL_FRAME_INFO info;
        memset(&info, 0, sizeof(info));
        IDXGIResource* res = NULL;
        HRESULT hr = m_dupl->AcquireNextFrame(timeoutMs, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return noNewFrame(out);
        if (hr == DXGI_ERROR_ACCESS_LOST)
            return share_capture_lost;
        if (FAILED(hr))
            return share_capture_fatal;
        m_holdingFrame = true;

        //只有指针移动、画面没变：画面沿用上一帧，但指针位置变了，仍要重新合成
        if (info.LastPresentTime.QuadPart == 0)
        {
            safeRelease(res);
            if (!m_hasFrame || info.LastMouseUpdateTime.QuadPart == 0)
                return noNewFrame(out);
            return composeFromLast(out);
        }

        ID3D11Texture2D* tex = NULL;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
        safeRelease(res);
        if (FAILED(hr))
            return share_capture_fatal;
        if (!ensureStaging(tex))
        {
            safeRelease(tex);
            return share_capture_fatal;
        }
        m_context->CopyResource(m_staging, tex);
        safeRelease(tex);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = m_context->Map(m_staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            return share_capture_fatal;
        //拷进自己的 DIB：之后可以随时在上面重画指针，Unmap / ReleaseFrame 也不必等编码完成
        const int rowBytes = m_width * kBytesPerPixel;
        for (int y = 0; y < m_height; ++y)
            memcpy(m_lastFrame.data() + static_cast<size_t>(y) * rowBytes,
                   static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
        m_context->Unmap(m_staging, 0);
        m_hasFrame = true;
        return composeFromLast(out);
    }

    virtual void Stop()
    {
        releaseFrame();
        safeRelease(m_staging);
        safeRelease(m_dupl);
        safeRelease(m_context);
        safeRelease(m_device);
        if (m_dibDc != NULL)
        {
            ::SelectObject(m_dibDc, m_dibOld);
            ::DeleteDC(m_dibDc);
            m_dibDc = NULL;
        }
        if (m_dib != NULL)
        {
            ::DeleteObject(m_dib);
            m_dib = NULL;
        }
        m_dibBits = NULL;
        m_hasFrame = false;
    }

    virtual bool IsMinimized() const
    {
        return false;
    }

    virtual RECT SourceRect() const
    {
        return m_monitorRect;
    }

private:
    //没有新画面：若指针也没动，就报告没有新帧
    int noNewFrame(ShareCaptureFrame& out)
    {
        (void)out;
        return share_capture_no_frame;
    }

    void releaseFrame()
    {
        if (m_holdingFrame && m_dupl != NULL)
            m_dupl->ReleaseFrame();
        m_holdingFrame = false;
    }

    bool ensureStaging(ID3D11Texture2D* frameTex)
    {
        D3D11_TEXTURE2D_DESC desc;
        frameTex->GetDesc(&desc);
        if (m_staging != NULL)
            return true;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        return SUCCEEDED(m_device->CreateTexture2D(&desc, NULL, &m_staging));
    }

    //建输出用的 DIB（画指针要用 GDI）与保存上一帧画面的缓冲
    bool ensureDib()
    {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = m_width;
        bi.bmiHeader.biHeight = -m_height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = kBitsPerPixel32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        m_dib = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (m_dib == NULL)
            return false;
        m_dibBits = static_cast<uint8_t*>(bits);
        m_dibDc = ::CreateCompatibleDC(NULL);
        m_dibOld = ::SelectObject(m_dibDc, m_dib);
        m_lastFrame.assign(static_cast<size_t>(m_width) * m_height * kBytesPerPixel, 0);
        return true;
    }

    //把上一帧画面拷进 DIB，再画上当前的鼠标指针
    int composeFromLast(ShareCaptureFrame& out)
    {
        memcpy(m_dibBits, m_lastFrame.data(), m_lastFrame.size());
        drawCursor();
        ::GdiFlush();
        out.m_data = m_dibBits;
        out.m_stride = m_width * kBytesPerPixel;
        out.m_width = m_width;
        out.m_height = m_height;
        return share_capture_ok;
    }

    void drawCursor()
    {
        CURSORINFO ci;
        memset(&ci, 0, sizeof(ci));
        ci.cbSize = sizeof(ci);
        if (!::GetCursorInfo(&ci) || (ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == NULL)
            return;
        //指针不在本显示器上就不画
        if (ci.ptScreenPos.x < m_monitorRect.left || ci.ptScreenPos.x >= m_monitorRect.right ||
            ci.ptScreenPos.y < m_monitorRect.top || ci.ptScreenPos.y >= m_monitorRect.bottom)
            return;
        if (ci.hCursor != m_cursorCached)
        {
            ICONINFO ii;
            memset(&ii, 0, sizeof(ii));
            if (::GetIconInfo(ci.hCursor, &ii))
            {
                m_cursorHotspot.x = static_cast<LONG>(ii.xHotspot);
                m_cursorHotspot.y = static_cast<LONG>(ii.yHotspot);
                if (ii.hbmMask != NULL)
                    ::DeleteObject(ii.hbmMask);
                if (ii.hbmColor != NULL)
                    ::DeleteObject(ii.hbmColor);
            }
            m_cursorCached = ci.hCursor;
        }
        const int x = ci.ptScreenPos.x - m_monitorRect.left - m_cursorHotspot.x;
        const int y = ci.ptScreenPos.y - m_monitorRect.top - m_cursorHotspot.y;
        ::DrawIconEx(m_dibDc, x, y, ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);
    }

    int                    m_adapterIndex;   //DXGI 显卡序号
    int                    m_outputIndex;    //输出序号
    RECT                   m_monitorRect;    //显示器矩形（虚拟桌面坐标）
    ID3D11Device*          m_device;         //D3D11 设备
    ID3D11DeviceContext*   m_context;        //立即上下文
    IDXGIOutputDuplication* m_dupl;          //桌面复制对象
    ID3D11Texture2D*       m_staging;        //CPU 可读的中转纹理
    bool                   m_holdingFrame;   //是否持有未 ReleaseFrame 的帧
    int                    m_width;          //画面宽，单位像素
    int                    m_height;         //画面高，单位像素
    HBITMAP                m_dib;            //输出 DIB（画面 + 指针）
    HDC                    m_dibDc;          //选入 m_dib 的内存 DC
    HGDIOBJ                m_dibOld;         //m_dibDc 原来选入的位图
    uint8_t*               m_dibBits;        //m_dib 的像素
    std::vector<uint8_t>   m_lastFrame;      //最近一帧画面（不含指针），只动指针时据此重新合成
    bool                   m_hasFrame;       //m_lastFrame 是否已有内容
    HCURSOR                m_cursorCached;   //热点已缓存的指针句柄
    POINT                  m_cursorHotspot;  //该指针的热点
};

} // namespace

IShareCapturer* CreateMonitorCapturer(int adapter, int output, const RECT& monitorRect)
{
    return new DxgiOutputCapturer(adapter, output, monitorRect);
}

} // namespace screenshare
