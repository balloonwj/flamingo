/**
 *  SubtitleWindow.cpp
 *
 *  聊天字幕窗口的实现，说明见 SubtitleWindow.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleWindow.h"

#include <commctrl.h>

#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"
#include "../../../../third-party/balloonui/balloonui/DuiDpi.h"
#include "../../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   // 设置菜单

#pragma comment(lib, "comctl32.lib")

namespace subtitle {

namespace {

//窗口类名
const wchar_t* const kWindowClass = L"FlamingoSubtitleWindow";
//窗口的基本扩展样式：置顶、不进任务栏、不抢焦点、分层
const DWORD kBaseExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
//分层窗口整窗的不透明度（DirectComposition 方式下内容自带逐像素透明度，整窗保持不透明）
const BYTE kLayerOpaque = 255;
//百分比的分母
const int kPercent = 100;
//不排除采集时的显示亲和性（WDA_NONE）；排除失败时退而求其次，在采集里显示为黑色（WDA_MONITOR）
const DWORD kDisplayAffinityNone = 0x00000000;
const DWORD kDisplayAffinityMonitor = 0x00000001;

//设置菜单的项 id
enum SettingsMenuId
{
    //字号：小
    kMenuFontSmall = 1,
    //字号：中
    kMenuFontMedium = 2,
    //字号：大
    kMenuFontLarge = 3,
    //字号：特大
    kMenuFontHuge = 4,
    //最多显示 1 行
    kMenuLines1 = 11,
    //最多显示 2 行
    kMenuLines2 = 12,
    //最多显示 3 行
    kMenuLines3 = 13,
    //共享屏幕或录屏时隐藏字幕（勾选项）
    kMenuHideCapture = 21,
    //「字号」子菜单的父项
    kMenuFontParent = 30,
    //「最多显示行数」子菜单的父项
    kMenuLinesParent = 31,
};

//悬停提示的工具 id：与工具按钮一一对应
enum TooltipToolId
{
    //设置按钮
    kTipSettings = 1,
    //锁定 / 解锁按钮
    kTipLock = 2,
    //回到聊天窗按钮
    kTipBack = 3,
    //退出字幕模式按钮
    kTipClose = 4,
};

//字号菜单项与字号档位的对应
struct FontChoice
{
    UINT           m_id;    //菜单项 id
    int            m_size;  //字号（96 DPI 下的像素）
    const wchar_t* m_text;  //菜单文字（原文，显示时经 TR 翻译）
};

const FontChoice kFontChoices[] = {
    { kMenuFontSmall, kFontSizeSmall, TR_NOOP(L"小") },
    { kMenuFontMedium, kFontSizeMedium, TR_NOOP(L"中") },
    { kMenuFontLarge, kFontSizeLarge, TR_NOOP(L"大") },
    { kMenuFontHuge, kFontSizeHuge, TR_NOOP(L"特大") },
};

//说话人前缀模板
const wchar_t* const kSpeakerPattern = TR_NOOP(L"{0}说：");
//群聊字幕行首的来源标注模板（{0} 为群名）
const wchar_t* const kOriginPattern = TR_NOOP(L"【{0}】");

//EnumDisplayMonitors 的回调：把每台显示器的工作区追加到 lParam 指向的 std::vector<RECT>
BOOL CALLBACK CollectWorkAreaProc(HMONITOR monitor, HDC dc, LPRECT rc, LPARAM lParam)
{
    (void)dc;
    (void)rc;
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(monitor, &mi))
        reinterpret_cast<std::vector<RECT>*>(lParam)->push_back(mi.rcWork);
    return TRUE;
}

uint64_t NowMs()
{
    return ::GetTickCount64();
}

bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

//是否为工具按钮（含锁定后浮出的小锁按钮）
bool IsButtonPart(int part)
{
    return part == kPartSettings || part == kPartLock || part == kPartBack || part == kPartClose;
}

} // anonymous namespace

CSubtitleWindow::CSubtitleWindow()
    : m_hwnd(NULL)
    , m_tooltip(NULL)
    , m_host(NULL)
    , m_compositionMode(false)
    , m_titleWidth(0)
    , m_clickThrough(false)
    , m_fastTimer(false)
    , m_hoverPart(kPartNone)
    , m_pressedPart(kPartNone)
    , m_dragMode(kPartNone)
    , m_tooltipsActive(false)
{
    m_dragStartCursor.x = 0;
    m_dragStartCursor.y = 0;
    ::SetRectEmpty(&m_dragStartRect);
}

CSubtitleWindow::~CSubtitleWindow()
{
    Destroy();
}

bool CSubtitleWindow::registerClass()
{
    static bool s_registered = false;
    if (s_registered)
        return true;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CSubtitleWindow::WndProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        LOG_WARNING("subtitle: RegisterClassEx failed, error=%lu.", ::GetLastError());
        return false;
    }
    s_registered = true;
    return true;
}

bool CSubtitleWindow::Create(const SubtitleViewInit& init)
{
    Destroy();
    if (!registerClass())
        return false;
    m_host = init.m_host;
    m_title = init.m_title;
    m_appearance = NormalizeAppearance(init.m_appearance);
    m_line = init.m_line;

    //决定窗口位置：先按目标显示器的 DPI 算出高度与默认宽度
    std::vector<RECT> works;
    RECT primary;
    collectWorkAreas(works, primary);
    POINT probe;
    if (init.m_hasSaved)
    {
        probe.x = init.m_saved.m_left + init.m_saved.m_width / 2;
        probe.y = init.m_saved.m_top;
    }
    else
    {
        probe.x = (primary.left + primary.right) / 2;
        probe.y = (primary.top + primary.bottom) / 2;
    }
    const SubtitleMetrics m = ComputeSubtitleMetrics(m_appearance.m_fontSize, balloonwjui::DuiDpi::GetDpiForPoint(probe));
    SubtitlePlaceInput place;
    place.m_hasSaved = init.m_hasSaved;
    place.m_saved = init.m_saved;
    place.m_height = SubtitleWindowHeight(m, m_appearance.m_maxLines);
    place.m_defaultWidth = DefaultSubtitleWidth(primary, m);
    place.m_minWidth = m.m_minWidth;
    place.m_stackGap = ::MulDiv(kStackGapDip, m.m_dpi, balloonwjui::DuiDpi::kDefaultDpi);
    place.m_workAreas = works;
    place.m_primaryWork = primary;
    place.m_occupied = init.m_occupied;
    const RECT rc = ResolveSubtitleRect(place);

    //优先 DirectComposition 方式；建不出合成层时换成 UpdateLayeredWindow 方式的窗口
    if (createNative(true, rc) && m_composition.Attach(m_hwnd))
    {
        m_compositionMode = true;
    }
    else
    {
        if (m_hwnd != NULL)
        {
            ::DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
        if (!createNative(false, rc))
        {
            LOG_WARNING("subtitle: cannot create the subtitle window.");
            return false;
        }
        m_compositionMode = false;
    }

    const uint64_t now = NowMs();
    m_state = CSubtitleState();
    m_state.Tick(now);
    m_state.SetLocked(init.m_hasSaved && init.m_saved.m_locked, now);
    m_visual = m_state.Tick(now);

    //悬停提示：原生提示控件，按钮的矩形与文字在 updateTooltips 里更新
    m_tooltip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, NULL,
                                  ::GetModuleHandleW(NULL), NULL);
    if (m_tooltip != NULL)
    {
        const UINT ids[kToolbarButtonCount] = { kTipSettings, kTipLock, kTipBack, kTipClose };
        for (int i = 0; i < kToolbarButtonCount; ++i)
        {
            TTTOOLINFOW ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = TTTOOLINFOW_V2_SIZE;
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = m_hwnd;
            ti.uId = ids[i];
            ti.lpszText = const_cast<wchar_t*>(L"");
            ::SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }
        ::SendMessageW(m_tooltip, TTM_ACTIVATE, FALSE, 0);
    }

    relayout();
    applyCaptureAffinity();
    setClickThrough(m_visual.m_clickThrough);
    render();
    ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetTimer(m_hwnd, kSubtitleTimerId, kSubtitleSlowTickMs, NULL);
    m_fastTimer = false;
    const RECT shown = GetRect();
    LOG_INFO("subtitle: window created, composition=%d, rect=(%ld,%ld,%ld,%ld), locked=%d, font=%d, lines=%d.",
             m_compositionMode ? 1 : 0, shown.left, shown.top, shown.right, shown.bottom, IsLocked() ? 1 : 0,
             m_appearance.m_fontSize, m_appearance.m_maxLines);
    return true;
}

bool CSubtitleWindow::createNative(bool composition, const RECT& rc)
{
    const DWORD ex = kBaseExStyle | (composition ? graphics::kExStyleNoRedirectionBitmap : 0);
    HWND hwnd = ::CreateWindowExW(ex, kWindowClass, L"Flamingo Subtitle", WS_POPUP, rc.left, rc.top,
                                  rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, ::GetModuleHandleW(NULL),
                                  this);
    if (hwnd == NULL)
    {
        LOG_WARNING("subtitle: CreateWindowEx failed, composition=%d, error=%lu.", composition ? 1 : 0,
                    ::GetLastError());
        return false;
    }
    m_hwnd = hwnd;
    m_clickThrough = false;
    //DirectComposition 方式：整窗不透明度 255，透明度由内容决定；UpdateLayeredWindow 方式不能调用它
    if (composition)
        ::SetLayeredWindowAttributes(hwnd, 0, kLayerOpaque, LWA_ALPHA);
    return true;
}

void CSubtitleWindow::Destroy()
{
    if (m_hwnd == NULL)
        return;
    HWND hwnd = m_hwnd;
    ::KillTimer(hwnd, kSubtitleTimerId);
    if (m_tooltip != NULL)
    {
        ::DestroyWindow(m_tooltip);
        m_tooltip = NULL;
    }
    m_composition.Detach();
    m_hwnd = NULL;
    ::DestroyWindow(hwnd);
    m_compositionMode = false;
    m_dragMode = kPartNone;
    m_pressedPart = kPartNone;
}

void CSubtitleWindow::SetContent(const SubtitleLine& line)
{
    m_line = line;
    if (m_hwnd == NULL)
        return;
    relayout();
    render();
}

void CSubtitleWindow::SetTitle(const std::wstring& title)
{
    m_title = title;
    if (m_hwnd == NULL)
        return;
    relayout();
    render();
}

void CSubtitleWindow::SetAppearance(const SubtitleAppearance& ap)
{
    m_appearance = NormalizeAppearance(ap);
    if (m_hwnd == NULL)
        return;
    //relayout 按新字号与行数调整高度，底边保持不动
    relayout();
    applyCaptureAffinity();
    render();
}

RECT CSubtitleWindow::GetRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (m_hwnd != NULL)
        ::GetWindowRect(m_hwnd, &rc);
    return rc;
}

bool CSubtitleWindow::IsLocked() const
{
    return m_state.IsLocked();
}

void CSubtitleWindow::SetHidden(bool hidden)
{
    if (m_hwnd == NULL)
        return;
    if (hidden)
    {
        ::ShowWindow(m_hwnd, SW_HIDE);
        return;
    }
    //与 Create 相同：恢复显示时重新放到置顶层的最上面，不激活
    ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

HWND CSubtitleWindow::GetHwnd() const
{
    return m_hwnd;
}

bool CSubtitleWindow::IsCompositionMode() const
{
    return m_hwnd != NULL && m_compositionMode;
}

bool CSubtitleWindow::IsCaptureExclusionSupported() const
{
    return IsCompositionMode();
}

void CSubtitleWindow::SetLockedForTest(bool locked)
{
    applyLocked(locked);
}

LRESULT CALLBACK CSubtitleWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    CSubtitleWindow* self = reinterpret_cast<CSubtitleWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCDESTROY)
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    //只处理当前持有的窗口：建窗失败后换方式重建时，旧窗口的销毁消息不再转给对象
    if (self == NULL || self->m_hwnd != hwnd)
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    //注意：handleMessage 处理「退出字幕模式」时对象可能已被删除，返回后不得再访问 self
    return self->handleMessage(msg, wParam, lParam);
}

LRESULT CSubtitleWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    //不抢焦点：点在字幕上不激活本窗口，用户正在用的程序保持前台
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    //光标形状：左右边缘为左右调整箭头，其它地方为普通箭头
    case WM_SETCURSOR:
    {
        POINT pt;
        ::GetCursorPos(&pt);
        const int part = (m_dragMode != kPartNone) ? m_dragMode : hitTestScreen(pt);
        const bool sizing = (part == kPartLeftEdge || part == kPartRightEdge);
        ::SetCursor(::LoadCursorW(NULL, sizing ? IDC_SIZEWE : IDC_ARROW));
        return TRUE;
    }

    //悬停检测与渐变的定时器
    case WM_TIMER:
        if (wParam == kSubtitleTimerId)
            onTimer();
        return 0;

    //鼠标按下：按钮记下按下状态，空白与文字处开始拖动，边缘开始调宽
    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        onMouseDown(pt);
        return 0;
    }

    //鼠标移动：拖动或调宽中跟随光标，否则刷新悬停
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        onMouseMove(pt);
        return 0;
    }

    //鼠标松开：结束拖动，或在同一按钮上松开时执行按钮命令
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        onMouseUp(pt);
        return 0;
    }

    //鼠标捕获被别的窗口拿走（如弹出了系统对话框）：按松开处理，拖动到此为止
    case WM_CAPTURECHANGED:
        if (m_dragMode != kPartNone)
            finishDrag();
        if (m_pressedPart != kPartNone)
        {
            m_pressedPart = kPartNone;
            render();
        }
        return 0;

    //执行工具按钮命令（鼠标松开时投递给自己，等鼠标消息处理完才执行）
    case kMsgSubtitleCommand:
        //runCommand 处理「退出字幕模式」时本对象会被删除，之后直接返回
        runCommand((int)wParam);
        return 0;

    //换到 DPI 不同的显示器：按新 DPI 重新排版，窗口高度随之调整
    case WM_DPICHANGED:
        relayout();
        render();
        return 0;

    //显示器布局变化或工作区变化（任务栏移动、拔掉显示器）：限制回工作区里
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    {
        std::vector<RECT> works;
        RECT primary;
        collectWorkAreas(works, primary);
        const RECT rc = GetRect();
        const RECT clamped = ClampSubtitleRect(rc, works, primary);
        if (!SameRect(rc, clamped))
        {
            ::SetWindowPos(m_hwnd, NULL, clamped.left, clamped.top, clamped.right - clamped.left,
                           clamped.bottom - clamped.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        relayout();
        render();
        return 0;
    }

    //其它消息交给系统默认处理
    default:
        break;
    }
    return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

int CSubtitleWindow::currentDpi() const
{
    if (m_hwnd != NULL)
        return balloonwjui::DuiDpi::GetWindowDpi(m_hwnd);
    return balloonwjui::DuiDpi::GetSystemDpi();
}

void CSubtitleWindow::collectWorkAreas(std::vector<RECT>& out, RECT& primary)
{
    out.clear();
    ::EnumDisplayMonitors(NULL, NULL, &CollectWorkAreaProc, reinterpret_cast<LPARAM>(&out));
    POINT origin = { 0, 0 };
    HMONITOR mon = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (mon != NULL && ::GetMonitorInfoW(mon, &mi))
        primary = mi.rcWork;
    else
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &primary, 0);
}

void CSubtitleWindow::relayout()
{
    if (m_hwnd == NULL)
        return;
    m_metrics = ComputeSubtitleMetrics(m_appearance.m_fontSize, currentDpi());

    //高度只取决于字号与行数：与当前不同（改了外观、换了 DPI）时保持底边不动调整高度
    RECT rc = GetRect();
    const int width = (rc.right - rc.left > m_metrics.m_minWidth) ? (rc.right - rc.left) : m_metrics.m_minWidth;
    const int height = SubtitleWindowHeight(m_metrics, m_appearance.m_maxLines);
    if (rc.bottom - rc.top != height || rc.right - rc.left != width)
    {
        RECT target = { rc.left, rc.bottom - height, rc.left + width, rc.bottom };
        std::vector<RECT> works;
        RECT primary;
        collectWorkAreas(works, primary);
        target = ClampSubtitleRect(target, works, primary);
        ::SetWindowPos(m_hwnd, NULL, target.left, target.top, target.right - target.left, target.bottom - target.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        rc = target;
    }
    const int w = rc.right - rc.left;

    //会话名的宽度决定工具条的摆放
    {
        CSubtitleTextMeasurer titleMeasurer(m_metrics.m_titleFontPx, false);
        m_titleWidth = titleMeasurer.Measure(m_title.c_str(), (int)m_title.size());
    }
    m_toolbar = ComputeSubtitleToolbar(m_metrics, w, m_titleWidth);

    //折行：群聊的来源标注不超过行宽的四分之一，说话人前缀不超过行宽的一半
    const RECT text = SubtitleTextRect(m_metrics, w, m_appearance.m_maxLines);
    const int textW = text.right - text.left;
    CSubtitleTextMeasurer measurer(m_metrics.m_fontPx, true);
    std::wstring origin;
    if (!m_line.m_origin.empty())
    {
        origin = FitSpeakerPrefix(TR(kOriginPattern), m_line.m_origin, textW * kOriginMaxWidthPercent / kPercent,
                                  &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    }
    std::wstring prefix;
    if (m_line.m_kind == kLineMessage)
    {
        prefix = FitSpeakerPrefix(TR(kSpeakerPattern), m_line.m_speaker, textW * kSpeakerMaxWidthPercent / kPercent,
                                  &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    }
    m_fit = FitSubtitleText(origin, prefix, m_line.m_content, textW, m_appearance.m_maxLines,
                            &CSubtitleTextMeasurer::MeasureThunk, &measurer);
    updateTooltips();
}

void CSubtitleWindow::render()
{
    if (m_hwnd == NULL)
        return;
    const RECT rc = GetRect();
    SubtitleRenderParams p;
    p.m_width = rc.right - rc.left;
    p.m_height = rc.bottom - rc.top;
    p.m_metrics = m_metrics;
    p.m_toolbar = m_toolbar;
    p.m_maxLines = m_appearance.m_maxLines;
    p.m_title = m_title;
    p.m_text = m_fit;
    p.m_lineKind = m_line.m_kind;
    p.m_locked = m_state.IsLocked();
    p.m_chromeAlpha = m_visual.m_chromeAlpha;
    p.m_lockButtonAlpha = m_visual.m_lockButtonAlpha;
    p.m_hoverPart = m_visual.m_buttonsActive ? m_hoverPart : kPartNone;
    p.m_pressedPart = m_pressedPart;
    p.m_hitTestable = !m_state.IsLocked();
    if (!RenderSubtitle(p, m_bits))
        return;

    if (m_compositionMode)
    {
        m_composition.Present(&m_bits[0], p.m_width, p.m_height);
        return;
    }

    //UpdateLayeredWindow 方式：把像素放进 32 位 DIB，按逐像素透明度更新分层窗口
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = p.m_width;
    bi.bmiHeader.biHeight = -p.m_height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib != NULL && bits != NULL)
    {
        memcpy(bits, &m_bits[0], m_bits.size());
        HGDIOBJ old = ::SelectObject(mem, dib);
        POINT dst = { rc.left, rc.top };
        SIZE size = { p.m_width, p.m_height };
        POINT src = { 0, 0 };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, kLayerOpaque, AC_SRC_ALPHA };
        ::UpdateLayeredWindow(m_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
        ::SelectObject(mem, old);
    }
    if (dib != NULL)
        ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
}

void CSubtitleWindow::onTimer()
{
    updateVisual(false);
}

int CSubtitleWindow::hitTestScreen(POINT screenPt) const
{
    if (m_hwnd == NULL)
        return kPartNone;
    const RECT rc = GetRect();
    POINT pt = { screenPt.x - rc.left, screenPt.y - rc.top };
    return HitTestSubtitle(m_metrics, m_toolbar, rc.right - rc.left, rc.bottom - rc.top, pt, m_state.IsLocked(),
                           m_visual.m_buttonsActive, m_visual.m_lockButtonAlpha > 0);
}

void CSubtitleWindow::updateVisual(bool forceRender)
{
    if (m_hwnd == NULL)
        return;
    POINT pt;
    ::GetCursorPos(&pt);
    //拖动、调宽过程中光标可能一度跑到窗口外，这时仍按在字幕上处理
    const int part = (m_dragMode != kPartNone) ? kPartBody : hitTestScreen(pt);
    const uint64_t now = NowMs();
    m_state.OnCursor(part, now);
    const SubtitleVisual v = m_state.Tick(now);
    setClickThrough(v.m_clickThrough);
    const bool changed = forceRender || v.m_chromeAlpha != m_visual.m_chromeAlpha ||
                         v.m_lockButtonAlpha != m_visual.m_lockButtonAlpha ||
                         v.m_buttonsActive != m_visual.m_buttonsActive || part != m_hoverPart;
    m_visual = v;
    m_hoverPart = part;
    if (changed)
    {
        updateTooltips();
        render();
    }
    ensureTimerRate(v.m_animating || m_dragMode != kPartNone);
}

void CSubtitleWindow::setClickThrough(bool on)
{
    if (m_hwnd == NULL || on == m_clickThrough)
        return;
    LONG_PTR ex = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (on)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
    ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    m_clickThrough = on;
}

void CSubtitleWindow::ensureTimerRate(bool fast)
{
    if (m_hwnd == NULL || fast == m_fastTimer)
        return;
    ::SetTimer(m_hwnd, kSubtitleTimerId, fast ? kSubtitleFastTickMs : kSubtitleSlowTickMs, NULL);
    m_fastTimer = fast;
}

void CSubtitleWindow::onMouseDown(POINT pt)
{
    const RECT rc = GetRect();
    const int part = HitTestSubtitle(m_metrics, m_toolbar, rc.right - rc.left, rc.bottom - rc.top, pt,
                                     m_state.IsLocked(), m_visual.m_buttonsActive, m_visual.m_lockButtonAlpha > 0);
    if (IsButtonPart(part))
    {
        m_pressedPart = part;
        ::SetCapture(m_hwnd);
        render();
        return;
    }
    //锁定时不能拖动（锁定时窗口一般收不到点击，只有光标在小锁按钮上时例外）
    if (m_state.IsLocked() || part == kPartNone)
        return;
    m_dragMode = part;
    ::GetCursorPos(&m_dragStartCursor);
    m_dragStartRect = rc;
    ::SetCapture(m_hwnd);
    m_state.SetDragging(true, NowMs());
    ensureTimerRate(true);
}

void CSubtitleWindow::onMouseMove(POINT pt)
{
    (void)pt;
    if (m_dragMode == kPartNone)
    {
        updateVisual(false);
        return;
    }
    POINT cur;
    ::GetCursorPos(&cur);
    const int dx = cur.x - m_dragStartCursor.x;
    const int dy = cur.y - m_dragStartCursor.y;
    const RECT& s = m_dragStartRect;
    const int startW = s.right - s.left;
    const int h = s.bottom - s.top;
    RECT target = s;
    if (m_dragMode == kPartBody)
    {
        ::OffsetRect(&target, dx, dy);
    }
    else if (m_dragMode == kPartLeftEdge)
    {
        int w = startW - dx;
        if (w < m_metrics.m_minWidth)
            w = m_metrics.m_minWidth;
        target.left = s.right - w;
    }
    else if (m_dragMode == kPartRightEdge)
    {
        int w = startW + dx;
        if (w < m_metrics.m_minWidth)
            w = m_metrics.m_minWidth;
        target.right = s.left + w;
    }
    const RECT now = GetRect();
    if (SameRect(now, target))
        return;
    ::SetWindowPos(m_hwnd, NULL, target.left, target.top, target.right - target.left, h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    //调宽时重新折行并重画；移动时内容不变，只是窗口换了位置
    if (target.right - target.left != now.right - now.left)
    {
        relayout();
        render();
    }
}

void CSubtitleWindow::onMouseUp(POINT pt)
{
    if (m_dragMode != kPartNone)
    {
        finishDrag();
        return;
    }
    if (m_pressedPart == kPartNone)
        return;
    const int pressed = m_pressedPart;
    m_pressedPart = kPartNone;
    if (::GetCapture() == m_hwnd)
        ::ReleaseCapture();
    const RECT rc = GetRect();
    const int part = HitTestSubtitle(m_metrics, m_toolbar, rc.right - rc.left, rc.bottom - rc.top, pt,
                                     m_state.IsLocked(), m_visual.m_buttonsActive, m_visual.m_lockButtonAlpha > 0);
    render();
    //在同一个按钮上松开才算点击；命令留到当前鼠标消息处理完再执行
    if (part == pressed)
        ::PostMessageW(m_hwnd, kMsgSubtitleCommand, (WPARAM)pressed, 0);
}

void CSubtitleWindow::finishDrag()
{
    const int mode = m_dragMode;
    m_dragMode = kPartNone;
    if (::GetCapture() == m_hwnd)
        ::ReleaseCapture();
    m_state.SetDragging(false, NowMs());
    if (mode == kPartNone || m_hwnd == NULL)
        return;
    //限制到工作区里，避免拖到屏幕外找不回来
    std::vector<RECT> works;
    RECT primary;
    collectWorkAreas(works, primary);
    const RECT rc = GetRect();
    const RECT clamped = ClampSubtitleRect(rc, works, primary);
    if (!SameRect(rc, clamped))
    {
        ::SetWindowPos(m_hwnd, NULL, clamped.left, clamped.top, clamped.right - clamped.left,
                       clamped.bottom - clamped.top, SWP_NOZORDER | SWP_NOACTIVATE);
        relayout();
    }
    render();
    if (m_host != NULL)
        m_host->OnSubtitleMoved(GetRect());
}

void CSubtitleWindow::runCommand(int part)
{
    switch (part)
    {
    //设置：弹出字号、行数、共享时隐藏的菜单
    case kPartSettings:
        showSettingsMenu();
        break;

    //锁定 / 解锁：切换后通知会话保存
    case kPartLock:
    {
        const bool locked = !m_state.IsLocked();
        applyLocked(locked);
        if (m_host != NULL)
            m_host->OnSubtitleLockChanged(locked);
        break;
    }

    //回到聊天窗：交给会话转告聊天窗
    case kPartBack:
        if (m_host != NULL)
            m_host->OnSubtitleBack();
        break;

    //退出字幕模式：会话会在这次调用里销毁并删除本对象，之后不得再访问任何成员
    case kPartClose:
        if (m_host != NULL)
            m_host->OnSubtitleClose();
        return;

    //其它取值不是按钮，忽略
    default:
        break;
    }
}

void CSubtitleWindow::showSettingsMenu()
{
    if (m_hwnd == NULL)
        return;
    SubtitleAppearance ap = m_appearance;

    balloonwjui::DuiMenu fontMenu;
    for (size_t i = 0; i < sizeof(kFontChoices) / sizeof(kFontChoices[0]); ++i)
        fontMenu.AppendChecked(kFontChoices[i].m_id, TR(kFontChoices[i].m_text), ap.m_fontSize == kFontChoices[i].m_size);
    balloonwjui::DuiMenu linesMenu;
    const UINT lineIds[] = { kMenuLines1, kMenuLines2, kMenuLines3 };
    for (int n = kMinMaxLines; n <= kMaxMaxLines; ++n)
    {
        const std::wstring text = TRF(L"{0} 行", n);
        linesMenu.AppendChecked(lineIds[n - kMinMaxLines], text.c_str(), ap.m_maxLines == n);
    }
    balloonwjui::DuiMenu menu;
    menu.AppendSubMenu(kMenuFontParent, TR(L"字号"), &fontMenu);
    menu.AppendSubMenu(kMenuLinesParent, TR(L"最多显示行数"), &linesMenu);
    menu.AppendSeparator();
    if (IsCaptureExclusionSupported())
        menu.AppendChecked(kMenuHideCapture, TR(L"共享屏幕或录屏时隐藏字幕"), ap.m_hideFromCapture);
    else
        menu.AppendDisabled(kMenuHideCapture, TR(L"共享屏幕或录屏时隐藏字幕"));

    //菜单弹在设置按钮下方；放不下时 DuiMenu 自己翻到上方
    const RECT rc = GetRect();
    const int x = rc.left + m_toolbar.m_settings.left;
    const int y = rc.top + m_toolbar.m_settings.bottom;
    //用户刚点过本窗口，本进程可以设前台；菜单要求所有者在前台，点到菜单外面时才能正常关闭。
    //先记下此刻的前台窗口（用户原来在用的程序），菜单关闭后交还给它，免得它一直失去键盘焦点
    const HWND foregroundBefore = ::GetForegroundWindow();
    ::SetForegroundWindow(m_hwnd);
    m_state.SetMenuOpen(true, NowMs());
    const UINT chosen = menu.TrackPopup(x, y, m_hwnd);
    m_state.SetMenuOpen(false, NowMs());
    ::PostMessageW(m_hwnd, WM_NULL, 0, 0);
    const HWND foregroundNow = ::GetForegroundWindow();
    const bool beforeAlive = (foregroundBefore != NULL) && (::IsWindow(foregroundBefore) != FALSE);
    if (ShouldRestoreForegroundAfterMenu(foregroundBefore, beforeAlive, foregroundNow, m_hwnd))
    {
        //本进程此刻持有前台，可以把前台交给别的进程的窗口
        ::SetForegroundWindow(foregroundBefore);
    }
    LOG_INFO("subtitle: settings menu closed, chosen=%u, foreground before=%p, now=%p, restored=%d.", chosen,
             foregroundBefore, foregroundNow, (::GetForegroundWindow() == foregroundBefore) ? 1 : 0);
    updateVisual(true);

    bool changed = false;
    for (size_t i = 0; i < sizeof(kFontChoices) / sizeof(kFontChoices[0]); ++i)
    {
        if (chosen == kFontChoices[i].m_id && ap.m_fontSize != kFontChoices[i].m_size)
        {
            ap.m_fontSize = kFontChoices[i].m_size;
            changed = true;
        }
    }
    for (int n = kMinMaxLines; n <= kMaxMaxLines; ++n)
    {
        if (chosen == lineIds[n - kMinMaxLines] && ap.m_maxLines != n)
        {
            ap.m_maxLines = n;
            changed = true;
        }
    }
    if (chosen == kMenuHideCapture && IsCaptureExclusionSupported())
    {
        ap.m_hideFromCapture = !ap.m_hideFromCapture;
        changed = true;
    }
    //外观所有字幕共用：交给会话保存，并由它让所有字幕（包括本窗口）换成新外观
    if (changed && m_host != NULL)
        m_host->OnSubtitleAppearanceChanged(ap);
}

void CSubtitleWindow::applyCaptureAffinity()
{
    if (m_hwnd == NULL)
        return;
    const bool exclude = m_appearance.m_hideFromCapture && m_compositionMode;
    const DWORD want = exclude ? kDisplayAffinityExclude : kDisplayAffinityNone;
    if (::SetWindowDisplayAffinity(m_hwnd, want))
        return;
    const DWORD error = ::GetLastError();
    //Windows 10 2004 之前没有「排除」这一档：退而求其次，让字幕在采集里显示为黑色，聊天内容同样不会外泄
    if (exclude && ::SetWindowDisplayAffinity(m_hwnd, kDisplayAffinityMonitor))
    {
        LOG_INFO("subtitle: exclude-from-capture unsupported (error=%lu), using monitor affinity.", error);
        return;
    }
    LOG_WARNING("subtitle: SetWindowDisplayAffinity(%lu) failed, error=%lu.", want, error);
}

void CSubtitleWindow::applyLocked(bool locked)
{
    if (locked == m_state.IsLocked())
        return;
    if (m_dragMode != kPartNone)
        finishDrag();
    m_pressedPart = kPartNone;
    m_state.SetLocked(locked, NowMs());
    updateVisual(true);
    LOG_INFO("subtitle: window %s.", locked ? "locked" : "unlocked");
}

void CSubtitleWindow::updateTooltips()
{
    if (m_tooltip == NULL || m_hwnd == NULL)
        return;
    const bool locked = m_state.IsLocked();
    const bool active = locked ? (m_visual.m_lockButtonAlpha > 0) : m_visual.m_buttonsActive;
    const RECT* rects[kToolbarButtonCount] = { &m_toolbar.m_settings, &m_toolbar.m_lock, &m_toolbar.m_back,
                                               &m_toolbar.m_close };
    const UINT ids[kToolbarButtonCount] = { kTipSettings, kTipLock, kTipBack, kTipClose };
    const wchar_t* texts[kToolbarButtonCount] = { TR(L"设置"), locked ? TR(L"解锁字幕") : TR(L"锁定字幕"),
                                                  TR(L"回到聊天窗"), TR(L"退出字幕模式") };
    for (int i = 0; i < kToolbarButtonCount; ++i)
    {
        TTTOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = TTTOOLINFOW_V2_SIZE;
        ti.hwnd = m_hwnd;
        ti.uId = ids[i];
        //锁定时只有小锁按钮有提示，其它按钮的区域置空
        if (!locked || ids[i] == kTipLock)
            ti.rect = *rects[i];
        ::SendMessageW(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
        ti.lpszText = const_cast<wchar_t*>(texts[i]);
        ::SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    if (active != m_tooltipsActive)
    {
        ::SendMessageW(m_tooltip, TTM_ACTIVATE, active ? TRUE : FALSE, 0);
        m_tooltipsActive = active;
    }
}

ISubtitleView* CreateSubtitleWindowView(void* ctx)
{
    (void)ctx;
    return new CSubtitleWindow();
}

bool ShouldRestoreForegroundAfterMenu(HWND before, bool beforeAlive, HWND now, HWND self)
{
    //弹菜单前没有前台窗口、原来的前台就是字幕自己，或那个窗口已经关掉：没有可交还的对象
    if (before == NULL || before == self || !beforeAlive)
        return false;
    //前台仍是字幕自己（或暂时没有前台窗口）才交还；用户点别的窗口关闭菜单时那个窗口已在前台，不再改动
    return now == self || now == NULL;
}

} // namespace subtitle
