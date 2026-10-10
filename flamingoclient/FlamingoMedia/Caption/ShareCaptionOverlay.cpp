/**
 *  屏幕共享实时字幕的浮层窗口，见 ShareCaptionOverlay.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "ShareCaptionOverlay.h"

#include <string.h>

#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//窗口类名
const wchar_t* const kOverlayClass = L"FlamingoShareCaptionOverlay";
//浮层高度占显示区域高度的比例（分子 / 分母），以及上下限（像素）
const int kHeightNum = 1;
const int kHeightDen = 4;
const int kMinHeightPx = 80;
const int kMaxHeightPx = 320;
//字号：显示区域高度的几分之一，以及上下限（像素）
const int kFontDen = 24;
const int kMinFontPx = 16;
const int kMaxFontPx = 44;
//排除在屏幕采集之外（Windows 10 2004 起支持；更早的系统上调用失败时浮层不显示）
const DWORD kWdaExcludeFromCapture = 0x00000011;
//每像素位数
const WORD kBitsPerPixel = 32;
//满不透明度
const BYTE kAlphaOpaque = 255;

} // namespace

CShareCaptionOverlay::CShareCaptionOverlay()
    : m_hwnd(NULL)
    , m_excluded(false)
    , m_owner(NULL)
    , m_excludeFailLogged(false)
{
}

CShareCaptionOverlay::~CShareCaptionOverlay()
{
    Destroy();
}

LRESULT CALLBACK CShareCaptionOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //鼠标一律穿透（WS_EX_TRANSPARENT 之外再保险一次）
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CShareCaptionOverlay::ensureWindow(HWND owner, bool excludeFromCapture)
{
    if (m_hwnd != NULL && ::IsWindow(m_hwnd) && owner == m_owner && excludeFromCapture == m_excluded)
        return true;
    //所有者窗口或是否排除采集变了（共享者与观看者之间切换）：重建。所有者只能在建窗时可靠地指定；排除采集也只能在
    //窗口还不是分层窗口时设置，见下
    Destroy();
    HINSTANCE inst = NULL;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&CShareCaptionOverlay::WndProc), &inst);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CShareCaptionOverlay::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOverlayClass;
    ::RegisterClassExW(&wc);   //重复注册返回失败，不影响使用
    //有所有者时不置顶：浮层随所有者窗口的 Z 序，始终在它之上。先建成普通窗口（不带 WS_EX_LAYERED），见下
    DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (owner == NULL)
        exStyle |= WS_EX_TOPMOST;
    m_hwnd = ::CreateWindowExW(exStyle, kOverlayClass, L"", WS_POPUP, 0, 0, 0, 0, owner, NULL, inst, NULL);
    if (m_hwnd == NULL)
    {
        LOG_ERROR("share caption: create overlay window failed, error=%lu.", ::GetLastError());
        return false;
    }
    //排除在采集之外要在窗口成为分层窗口之前设置：据实证（2026-10-06，tmp/caption/affinity 的实验），对已经带
    //WS_EX_LAYERED、用 UpdateLayeredWindow 绘制的窗口调 SetWindowDisplayAffinity 一律失败（错误码 8），而先在普通窗口上
    //设好再加 WS_EX_LAYERED，排除照样生效（截屏里没有这个窗口）
    if (excludeFromCapture && !::SetWindowDisplayAffinity(m_hwnd, kWdaExcludeFromCapture))
    {
        const DWORD error = ::GetLastError();
        //要求排除却排除不了（如 Windows 10 2004 之前的系统）：浮层会被采集进共享画面或录像里，宁可不显示
        //（字幕照常发送、照常写进草稿）
        if (!m_excludeFailLogged)
        {
            LOG_WARNING("share caption: cannot exclude overlay from capture (error=%lu), overlay hidden.", error);
            m_excludeFailLogged = true;
        }
        ::DestroyWindow(m_hwnd);
        m_hwnd = NULL;
        return false;
    }
    ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    m_excluded = excludeFromCapture;
    m_owner = owner;
    return true;
}

void CShareCaptionOverlay::Update(const RECT& area, bool excludeFromCapture, HWND owner,
                                  const std::vector<ShareCaptionLine>& lines, caption::ICaptionModule* module)
{
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    if (lines.empty() || module == NULL || areaW <= 0 || areaH <= 0)
    {
        Hide();
        return;
    }
    if (!ensureWindow(owner, excludeFromCapture))
        return;
    int height = areaH * kHeightNum / kHeightDen;
    if (height < kMinHeightPx)
        height = kMinHeightPx;
    if (height > kMaxHeightPx)
        height = kMaxHeightPx;
    if (height > areaH)
        height = areaH;
    int fontPx = areaH / kFontDen;
    if (fontPx < kMinFontPx)
        fontPx = kMinFontPx;
    if (fontPx > kMaxFontPx)
        fontPx = kMaxFontPx;
    std::vector<caption::CaptionOverlayLine> ol(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
    {
        ol[i].m_speaker = lines[i].m_speaker.c_str();
        ol[i].m_text = lines[i].m_text.c_str();
        ol[i].m_partial = lines[i].m_partial;
    }
    m_pixels.assign(static_cast<size_t>(areaW) * height, 0);
    if (!module->RenderOverlay(&ol[0], static_cast<uint32_t>(ol.size()), static_cast<uint32_t>(areaW),
                               static_cast<uint32_t>(height), &m_pixels[0], fontPx))
    {
        Hide();
        return;
    }
    //画到一张 32 位自上而下的 DIB 上交给 UpdateLayeredWindow（像素已是预乘透明度）
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = areaW;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib != NULL && bits != NULL)
    {
        memcpy(bits, &m_pixels[0], m_pixels.size() * sizeof(uint32_t));
        HGDIOBJ old = ::SelectObject(mem, dib);
        POINT dst = { area.left, area.bottom - height };
        SIZE size = { areaW, height };
        POINT src = { 0, 0 };
        BLENDFUNCTION blend;
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = kAlphaOpaque;
        blend.AlphaFormat = AC_SRC_ALPHA;
        ::UpdateLayeredWindow(m_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
        ::SelectObject(mem, old);
    }
    if (dib != NULL)
        ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    if (!::IsWindowVisible(m_hwnd))
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    //置顶的浮层每次重新提到最上面，免得被后来置顶的窗口盖住；有所有者的浮层由系统保持在所有者之上
    if (m_owner == NULL)
        ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CShareCaptionOverlay::Hide()
{
    if (m_hwnd != NULL && ::IsWindow(m_hwnd) && ::IsWindowVisible(m_hwnd))
        ::ShowWindow(m_hwnd, SW_HIDE);
}

void CShareCaptionOverlay::Destroy()
{
    if (m_hwnd != NULL && ::IsWindow(m_hwnd))
        ::DestroyWindow(m_hwnd);
    m_hwnd = NULL;
    m_excluded = false;
    m_owner = NULL;
}

HWND CShareCaptionOverlay::WindowHandle() const
{
    return m_hwnd;
}

bool CShareCaptionOverlay::IsShown() const
{
    return m_hwnd != NULL && ::IsWindow(m_hwnd) && ::IsWindowVisible(m_hwnd);
}

} // namespace screenshare
