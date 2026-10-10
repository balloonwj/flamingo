/**
 *  FlamingoMedia 通用的轻量提示（toast），说明见 MediaToast.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "MediaToast.h"

#include <memory>
#include "../Share/ShareSources.h"   // ExcludeWindowFromCapture：提示不能被共享或录进去
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：按文字宽度定窗口宽
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace flamingomedia {

namespace {

//窗口类名：本文件自注册，窗口过程为 DefWindowProc，建好后由 DuiHost 子类化接管
const wchar_t* const kToastClass = L"FlamingoMediaToast";
//淡入淡出的定时器 id 与间隔（毫秒）
const UINT_PTR kFadeTimerId = 1;
const UINT kTickMs = 30;
//文字区最大宽度（像素）：超过时折行；最小宽度让很短的提示也不至于太窄
const int kMaxTextW = 420;
const int kMinTextW = 120;
//最多显示几行，超过的部分以省略号结尾
const int kMaxLines = 3;
//四周留白（像素）
const int kPadH = 18;
const int kPadV = 10;
//圆角半径（像素）
const int kCornerRadius = 10;
//与参照矩形之间的距离（像素）
const int kAnchorGap = 8;
//放在参照矩形里面靠底边时，与底边的距离（像素）
const int kInsideBottomGap = 48;
//放在屏幕中央偏下时，提示中心距工作区顶边的比例：工作区高的三分之二处
const int kScreenCenterNum = 2;
const int kScreenCenterDen = 3;
//底色：普通为深灰、失败为深红；文字为白色
const COLORREF kInfoBg = RGB(44, 48, 56);
const COLORREF kErrorBg = RGB(178, 40, 40);
const COLORREF kTextColor = RGB(255, 255, 255);
//完全不透明
const int kOpaque = 255;

//把 RGB 底色换成样式
COLORREF bgForStyle(int style)
{
    return style == media_toast_error ? kErrorBg : kInfoBg;
}

//矩形是否为空
bool isEmptyRect(const RECT& rc)
{
    return rc.right <= rc.left || rc.bottom <= rc.top;
}

//某个矩形所在显示器的工作区；矩形为空时取主显示器
RECT workAreaFor(const RECT& anchor)
{
    RECT work = { 0, 0, 0, 0 };
    POINT origin = { 0, 0 };
    HMONITOR mon = isEmptyRect(anchor) ? ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY)
                                       : ::MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

//注册窗口类（只注册一次；已注册时 GetClassInfoEx 成功）
bool ensureClass(HINSTANCE instance)
{
    WNDCLASSEXW existing;
    memset(&existing, 0, sizeof(existing));
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance, kToastClass, &existing))
        return true;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kToastClass;
    return ::RegisterClassExW(&wc) != 0;
}

} // namespace

RECT ComputeMediaToastRect(const RECT& anchor, int placement, SIZE size, const RECT& work)
{
    const int w = size.cx;
    const int h = size.cy;
    int x = 0;
    int y = 0;
    if (isEmptyRect(anchor) || placement == media_toast_screen)
    {
        //屏幕中央偏下
        x = work.left + ((work.right - work.left) - w) / 2;
        y = work.top + (work.bottom - work.top) * kScreenCenterNum / kScreenCenterDen - h / 2;
    }
    else if (placement == media_toast_inside_bottom)
    {
        //参照窗口里面靠底边
        x = anchor.left + ((anchor.right - anchor.left) - w) / 2;
        y = anchor.bottom - kInsideBottomGap - h;
        //窗口太矮放不下时贴着顶边往下
        if (y < anchor.top)
            y = anchor.top;
    }
    else
    {
        //参照矩形正下方；下方放不下时放进参照矩形底部
        x = anchor.left + ((anchor.right - anchor.left) - w) / 2;
        y = anchor.bottom + kAnchorGap;
        if (y + h > work.bottom)
            y = anchor.bottom - kAnchorGap - h;
    }
    //限制在工作区内
    if (x + w > work.right)
        x = work.right - w;
    if (x < work.left)
        x = work.left;
    if (y + h > work.bottom)
        y = work.bottom - h;
    if (y < work.top)
        y = work.top;
    RECT rc = { x, y, x + w, y + h };
    return rc;
}

int MediaToastAlphaAt(int64_t elapsedMs, bool& finished)
{
    finished = false;
    if (elapsedMs < 0)
        return 0;
    //淡入
    if (elapsedMs < kMediaToastFadeInMs)
        return static_cast<int>(elapsedMs * kOpaque / kMediaToastFadeInMs);
    //停留
    const int64_t holdEnd = static_cast<int64_t>(kMediaToastFadeInMs) + kMediaToastHoldMs;
    if (elapsedMs < holdEnd)
        return kOpaque;
    //淡出
    const int64_t fadeEnd = holdEnd + kMediaToastFadeOutMs;
    if (elapsedMs < fadeEnd)
        return static_cast<int>((fadeEnd - elapsedMs) * kOpaque / kMediaToastFadeOutMs);
    finished = true;
    return 0;
}

CMediaToast::CMediaToast()
    : m_root(NULL)
    , m_label(NULL)
    , m_style(media_toast_info)
    , m_shownAtMs(0)
    , m_showing(false)
{
}

CMediaToast::~CMediaToast()
{
    Destroy();
}

bool CMediaToast::ensureWindow()
{
    if (IsWindow())
        return true;
    HINSTANCE instance = _AtlBaseModule.GetModuleInstance();
    if (!ensureClass(instance))
    {
        LOG_ERROR("media toast: register window class failed, error %lu.", ::GetLastError());
        return false;
    }
    //不设所有者：置顶、不抢焦点、不进任务栏；分层窗口用整窗透明度做淡入淡出
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    HWND hwnd = ::CreateWindowExW(exStyle, kToastClass, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (hwnd == NULL)
    {
        LOG_ERROR("media toast: create window failed, error %lu.", ::GetLastError());
        return false;
    }
    if (!SubclassWindow(hwnd))
    {
        ::DestroyWindow(hwnd);
        LOG_ERROR("media toast: subclass window failed.");
        return false;
    }
    ::SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
    //提示浮在被共享或录制的内容上，不能被采进去；排除失败只记日志
    if (!screenshare::ExcludeWindowFromCapture(m_hWnd))
        LOG_WARNING("media toast: window not excluded from capture.");
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadH, kPadV, kPadH, kPadV);
    root->SetBgColor(kInfoBg);
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetTextColor(kTextColor);
    label->SetWordWrap(true);
    label->SetTextAlign(DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
    m_label = label.get();
    root->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Weight(1));
    m_root = root.get();
    SetRoot(std::move(root));
    return true;
}

void CMediaToast::Show(const RECT& anchor, int placement, const std::wstring& text, int style)
{
    if (text.empty() || !ensureWindow())
        return;
    m_text = text;
    m_style = style;
    m_root->SetBgColor(bgForStyle(style));
    m_label->SetText(text.c_str());
    //宽：按文字一行的宽度，超过最大宽度时折行；高：按折行后的实际高度（DuiLabel 自己量），最多 kMaxLines 行
    const int px = i18n::DefaultFontPixelHeight();
    const int lineH = i18n::MeasureUiLineHeight(px, false);
    int textW = i18n::MeasureUiTextWidth(text.c_str(), px, false) + 1;
    textW = textW > kMaxTextW ? kMaxTextW : textW;
    textW = textW < kMinTextW ? kMinTextW : textW;
    int textH = m_label->MeasureHeight(textW);
    textH = textH < lineH ? lineH : textH;
    textH = textH > kMaxLines * lineH ? kMaxLines * lineH : textH;
    SIZE size;
    size.cx = textW + 2 * kPadH;
    size.cy = textH + 2 * kPadV;
    const RECT rc = ComputeMediaToastRect(anchor, placement, size, workAreaFor(anchor));
    //圆角：窗口区域交给系统后由系统持有，不再释放
    HRGN rgn = ::CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1, 2 * kCornerRadius, 2 * kCornerRadius);
    if (rgn != NULL && ::SetWindowRgn(m_hWnd, rgn, FALSE) == 0)
        ::DeleteObject(rgn);
    //重新计时：已在显示时不从全透明重新淡入，直接保持不透明
    const bool wasShowing = m_showing;
    m_showing = true;
    m_shownAtMs = static_cast<int64_t>(::GetTickCount64()) - (wasShowing ? kMediaToastFadeInMs : 0);
    ::SetLayeredWindowAttributes(m_hWnd, 0, static_cast<BYTE>(wasShowing ? kOpaque : 0), LWA_ALPHA);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, size.cx, size.cy,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Invalidate();
    SetTimer(kFadeTimerId, kTickMs, NULL);
    LOG_INFO("media toast: shown, style=%d, placement=%d, at (%ld,%ld) %dx%d.", style, placement, rc.left, rc.top,
             static_cast<int>(size.cx), static_cast<int>(size.cy));
}

void CMediaToast::ShowNearWindow(HWND wnd, const std::wstring& text, int style)
{
    RECT anchor = { 0, 0, 0, 0 };
    if (wnd != NULL && ::IsWindow(wnd) && ::IsWindowVisible(wnd) && !::IsIconic(wnd))
    {
        ::GetWindowRect(wnd, &anchor);
        Show(anchor, media_toast_inside_bottom, text, style);
        return;
    }
    Show(anchor, media_toast_screen, text, style);
}

void CMediaToast::ShowNearActiveWindow(const std::wstring& text, int style)
{
    HWND fg = ::GetForegroundWindow();
    DWORD pid = 0;
    if (fg != NULL)
        ::GetWindowThreadProcessId(fg, &pid);
    //前台窗口是本进程的（点了聊天窗、通话窗、主面板上的按钮）：放在它里面；否则（托盘菜单等）放在屏幕中央偏下
    ShowNearWindow(pid == ::GetCurrentProcessId() ? fg : NULL, text, style);
}

void CMediaToast::Hide()
{
    m_showing = false;
    if (!IsWindow())
        return;
    KillTimer(kFadeTimerId);
    ShowWindow(SW_HIDE);
}

void CMediaToast::Destroy()
{
    m_showing = false;
    m_root = NULL;
    m_label = NULL;
    if (IsWindow())
    {
        KillTimer(kFadeTimerId);
        DestroyWindow();
    }
}

bool CMediaToast::IsShowing() const
{
    return m_showing;
}

const std::wstring& CMediaToast::Text() const
{
    return m_text;
}

int CMediaToast::Style() const
{
    return m_style;
}

LRESULT CMediaToast::OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kFadeTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    bool finished = false;
    const int alpha = MediaToastAlphaAt(static_cast<int64_t>(::GetTickCount64()) - m_shownAtMs, finished);
    if (finished)
    {
        Hide();
        return 0;
    }
    ::SetLayeredWindowAttributes(m_hWnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
    return 0;
}

LRESULT CMediaToast::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return HTTRANSPARENT;
}

LRESULT CMediaToast::OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return MA_NOACTIVATE;
}

} // namespace flamingomedia
