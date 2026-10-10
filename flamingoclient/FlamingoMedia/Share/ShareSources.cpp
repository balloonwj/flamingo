/**
 *  屏幕共享的「共享内容」枚举与缩略图，ShareSources.cpp
 *  说明见 ShareSources.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareSources.h"

#include <algorithm>
#include <dwmapi.h>
#include <dxgi.h>
#include "ShareSourcePolicy.h"
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

namespace screenshare {

namespace {

//Win10 2004 起的显示亲和性取值：窗口不出现在任何屏幕采集里（旧 SDK 头文件里没有这个宏）
const DWORD kWdaExcludeFromCapture = 0x00000011;
//SetWindowDisplayAffinity 的「不排除」（WDA_NONE）：截图与屏幕采集都能取到窗口
const DWORD kWdaNone = 0x00000000;
//「允许截图」开关（2026-10-08）：只在界面线程上读写（各工具条、控制条与两个控制器都在界面线程上）
bool g_barCaptureAllowed = false;
//PrintWindow 的标志：连同 DirectComposition / GPU 渲染的内容一起绘制（Win8.1 起）
const UINT kPwRenderFullContent = 0x00000002;
//DWM 窗口属性：是否被 DWM 隐藏（Win8 起）
const DWORD kDwmaCloaked = 14;
//DWM 窗口属性：可见外框（Vista 起）
const DWORD kDwmaExtendedFrameBounds = 9;
//窗口标题的最大长度，单位字符
const int kMaxTitleChars = 512;
//类名的最大长度，单位字符
const int kMaxClassChars = 256;
//32 位 DIB 的位深
const WORD kBitsPerPixel32 = 32;
//32 位 DIB 每个像素的字节数，以及透明通道在像素内的字节偏移（内存顺序为 B、G、R、A）
const int kBytesPerPixel32 = 4;
const int kAlphaByteOffset = 3;
//完全不透明的透明通道取值
const BYTE kOpaqueAlpha = 255;

BOOL CALLBACK enumMonitorProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM param)
{
    (void)dc;
    (void)rect;
    std::vector<ShareMonitor>* out = reinterpret_cast<std::vector<ShareMonitor>*>(param);
    MONITORINFOEXW info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return TRUE;
    ShareMonitor m;
    m.m_monitor = monitor;
    m.m_rect = info.rcMonitor;
    m.m_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.m_device = info.szDevice;
    out->push_back(m);
    return TRUE;
}

//枚举全部显卡上的全部 DXGI 输出
void enumerateDxgiOutputs(std::vector<ShareDxgiOutputRef>& outputs)
{
    IDXGIFactory1* factory = NULL;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || factory == NULL)
        return;
    for (UINT a = 0;; ++a)
    {
        IDXGIAdapter1* adapter = NULL;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        if (adapter == NULL)
            continue;
        for (UINT o = 0;; ++o)
        {
            IDXGIOutput* output = NULL;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
                break;
            if (output == NULL)
                continue;
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop)
                outputs.push_back(ShareDxgiOutputRef(static_cast<int>(a), static_cast<int>(o), desc.Monitor));
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
}

//主显示器在前，其余按左上角坐标（先左后上）排序
bool monitorLess(const ShareMonitor& a, const ShareMonitor& b)
{
    if (a.m_primary != b.m_primary)
        return a.m_primary;
    if (a.m_rect.left != b.m_rect.left)
        return a.m_rect.left < b.m_rect.left;
    return a.m_rect.top < b.m_rect.top;
}

struct EnumWindowsContext
{
    std::vector<ShareWindow>* m_out;         //结果
    DWORD                     m_processId;   //本进程 id
};

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM param)
{
    EnumWindowsContext* ctx = reinterpret_cast<EnumWindowsContext*>(param);
    ShareWindowFacts facts;
    facts.m_visible = ::IsWindowVisible(hwnd) != FALSE;
    if (!facts.m_visible)
        return TRUE;

    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, kDwmaCloaked, &cloaked, sizeof(cloaked))))
        facts.m_cloaked = (cloaked != 0);
    const LONG_PTR exStyle = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    facts.m_toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    facts.m_appWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    facts.m_noActivate = (exStyle & WS_EX_NOACTIVATE) != 0;
    facts.m_hasOwner = ::GetWindow(hwnd, GW_OWNER) != NULL;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    facts.m_ownProcess = (pid == ctx->m_processId);
    facts.m_minimized = ::IsIconic(hwnd) != FALSE;
    const RECT bounds = GetShareWindowBounds(hwnd);
    facts.m_width = bounds.right - bounds.left;
    facts.m_height = bounds.bottom - bounds.top;
    facts.m_title = GetShareWindowTitle(hwnd);
    wchar_t cls[kMaxClassChars] = { 0 };
    ::GetClassNameW(hwnd, cls, kMaxClassChars);
    facts.m_className = cls;

    if (!ShouldListShareWindow(facts))
        return TRUE;
    ShareWindow w;
    w.m_hwnd = hwnd;
    w.m_title = facts.m_title;
    w.m_bounds = bounds;
    w.m_minimized = facts.m_minimized;
    ctx->m_out->push_back(w);
    return TRUE;
}

//建一个自上而下的 32 位 DIB，bits 回填像素指针
HBITMAP createDib32(int w, int h, void** bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    return ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

//把源 DC 的一块区域按比例缩进框内，画到新建的 DIB 上
HBITMAP scaleToThumbnail(HDC srcDc, int srcX, int srcY, int srcW, int srcH, int boxW, int boxH)
{
    int w = 0;
    int h = 0;
    FitShareSize(srcW, srcH, boxW, boxH, w, h);
    if (w <= 0 || h <= 0)
        return NULL;
    void* bits = NULL;
    HBITMAP bmp = createDib32(w, h, &bits);
    if (bmp == NULL)
        return NULL;
    HDC memDc = ::CreateCompatibleDC(srcDc);
    HGDIOBJ old = ::SelectObject(memDc, bmp);
    ::SetStretchBltMode(memDc, HALFTONE);
    ::SetBrushOrgEx(memDc, 0, 0, NULL);
    ::StretchBlt(memDc, 0, 0, w, h, srcDc, srcX, srcY, srcW, srcH, SRCCOPY);
    ::SelectObject(memDc, old);
    ::DeleteDC(memDc);
    return bmp;
}

} // namespace

ShareMonitor::ShareMonitor()
    : m_monitor(NULL)
    , m_primary(false)
    , m_adapter(-1)
    , m_output(-1)
{
    memset(&m_rect, 0, sizeof(m_rect));
}

ShareWindow::ShareWindow()
    : m_hwnd(NULL)
    , m_minimized(false)
{
    memset(&m_bounds, 0, sizeof(m_bounds));
}

bool EnumerateShareMonitors(std::vector<ShareMonitor>& out)
{
    out.clear();
    ::EnumDisplayMonitors(NULL, NULL, enumMonitorProc, reinterpret_cast<LPARAM>(&out));
    std::vector<ShareDxgiOutputRef> outputs;
    enumerateDxgiOutputs(outputs);
    for (size_t i = 0; i < out.size(); ++i)
    {
        int adapter = -1;
        int output = -1;
        if (FindDxgiOutputForMonitor(outputs, out[i].m_monitor, adapter, output))
        {
            out[i].m_adapter = adapter;
            out[i].m_output = output;
        }
    }
    std::sort(out.begin(), out.end(), monitorLess);
    LOG_INFO("screenshare: %u monitor(s), %u dxgi output(s).", static_cast<unsigned>(out.size()),
             static_cast<unsigned>(outputs.size()));
    return !out.empty();
}

void EnumerateShareWindows(std::vector<ShareWindow>& out)
{
    out.clear();
    EnumWindowsContext ctx;
    ctx.m_out = &out;
    ctx.m_processId = ::GetCurrentProcessId();
    //EnumWindows 按 Z 序从上到下回调
    ::EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&ctx));
}

HBITMAP CaptureMonitorThumbnail(const RECT& rect, int boxW, int boxH)
{
    HDC screenDc = ::GetDC(NULL);
    if (screenDc == NULL)
        return NULL;
    HBITMAP bmp = scaleToThumbnail(screenDc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                   boxW, boxH);
    ::ReleaseDC(NULL, screenDc);
    return bmp;
}

HBITMAP CaptureWindowThumbnail(HWND hwnd, int boxW, int boxH)
{
    if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
        return NULL;
    RECT wr;
    if (!::GetWindowRect(hwnd, &wr))
        return NULL;
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0)
        return NULL;

    //先按窗口全尺寸画到 DIB，再缩略；PrintWindow 画的是整个窗口矩形（含 Win10 的隐形边框）
    void* bits = NULL;
    HBITMAP full = createDib32(w, h, &bits);
    if (full == NULL)
        return NULL;
    HDC screenDc = ::GetDC(NULL);
    HDC fullDc = ::CreateCompatibleDC(screenDc);
    HGDIOBJ old = ::SelectObject(fullDc, full);
    const BOOL printed = ::PrintWindow(hwnd, fullDc, kPwRenderFullContent);

    //缩略时裁掉隐形边框：只取可见外框那一块
    const RECT vb = GetShareWindowBounds(hwnd);
    int srcX = vb.left - wr.left;
    int srcY = vb.top - wr.top;
    int srcW = vb.right - vb.left;
    int srcH = vb.bottom - vb.top;
    if (srcX < 0 || srcY < 0 || srcW <= 0 || srcH <= 0 || srcX + srcW > w || srcY + srcH > h)
    {
        srcX = 0;
        srcY = 0;
        srcW = w;
        srcH = h;
    }
    HBITMAP thumb = printed ? scaleToThumbnail(fullDc, srcX, srcY, srcW, srcH, boxW, boxH) : NULL;
    ::SelectObject(fullDc, old);
    ::DeleteDC(fullDc);
    ::ReleaseDC(NULL, screenDc);
    ::DeleteObject(full);
    return thumb;
}

bool MakeDibOpaque(HBITMAP dib)
{
    if (dib == NULL)
        return false;
    DIBSECTION ds;
    if (::GetObject(dib, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL
        || ds.dsBm.bmBitsPixel != kBitsPerPixel32)
        return false;
    //先让 GDI 把尚未落到位图上的绘制做完，再直接改像素
    ::GdiFlush();
    BYTE* row = static_cast<BYTE*>(ds.dsBm.bmBits);
    const int height = ds.dsBm.bmHeight < 0 ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
    for (int y = 0; y < height; ++y)
    {
        BYTE* px = row + static_cast<size_t>(y) * static_cast<size_t>(ds.dsBm.bmWidthBytes);
        for (int x = 0; x < ds.dsBm.bmWidth; ++x)
            px[x * kBytesPerPixel32 + kAlphaByteOffset] = kOpaqueAlpha;
    }
    return true;
}

#ifdef _DEBUG
//调试版「不排除采集」开关的环境变量名（第 13 期）：值为 1 时控制条、名单浮窗等不排除在屏幕采集之外，联调时才截得到图核对界面。
//只能在共享单个测试窗口时使用；共享整个屏幕时这些窗口会被共享出去
static const char* const kNoExcludeEnv = "FLAMINGO_SHARE_NO_EXCLUDE";

//是否打开了「不排除采集」开关
static bool skipCaptureExclusion()
{
    char value[8] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, value, sizeof(value), kNoExcludeEnv) != 0 || len == 0)
        return false;
    return value[0] == '1';
}
#endif

bool ExcludeWindowFromCapture(HWND hwnd)
{
    if (!::IsWindow(hwnd))
        return false;
#ifdef _DEBUG
    if (skipCaptureExclusion())
    {
        LOG_INFO("screenshare: FLAMINGO_SHARE_NO_EXCLUDE=1, window %p is not excluded from capture.", hwnd);
        return true;
    }
#endif
    if (!::SetWindowDisplayAffinity(hwnd, kWdaExcludeFromCapture))
    {
        LOG_WARNING("screenshare: SetWindowDisplayAffinity(exclude) failed, error=%u, hwnd=%p.",
                    static_cast<unsigned>(::GetLastError()), hwnd);
        return false;
    }
    return true;
}

bool SetWindowCaptureAllowed(HWND hwnd, bool allowed)
{
    if (!allowed)
        return ExcludeWindowFromCapture(hwnd);
    if (!::IsWindow(hwnd))
        return false;
    if (!::SetWindowDisplayAffinity(hwnd, kWdaNone))
    {
        LOG_WARNING("screenshare: SetWindowDisplayAffinity(none) failed, error=%u, hwnd=%p.",
                    static_cast<unsigned>(::GetLastError()), hwnd);
        return false;
    }
    return true;
}

void SetBarCaptureAllowed(bool allowed)
{
    if (allowed != g_barCaptureAllowed)
        LOG_INFO("screenshare: bars %s screenshots.", allowed ? "allow" : "no longer allow");
    g_barCaptureAllowed = allowed;
}

bool IsBarCaptureAllowed()
{
    return g_barCaptureAllowed;
}

bool ApplyBarCaptureState(HWND hwnd)
{
    return SetWindowCaptureAllowed(hwnd, g_barCaptureAllowed);
}

RECT GetShareWindowBounds(HWND hwnd)
{
    RECT rc;
    memset(&rc, 0, sizeof(rc));
    if (FAILED(::DwmGetWindowAttribute(hwnd, kDwmaExtendedFrameBounds, &rc, sizeof(rc))))
        ::GetWindowRect(hwnd, &rc);
    return rc;
}

std::wstring GetShareWindowTitle(HWND hwnd)
{
    wchar_t title[kMaxTitleChars] = { 0 };
    ::GetWindowTextW(hwnd, title, kMaxTitleChars);
    return std::wstring(title);
}

} // namespace screenshare
