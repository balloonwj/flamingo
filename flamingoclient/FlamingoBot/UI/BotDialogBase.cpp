/**
 *  机器人管理界面窗口的共同基类，见 BotDialogBase.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotDialogBase.h"

#include "BotWidgets.h"
#include "../../Source/Common/Log/IULog.h"

namespace flamingobot {

CBotDialogBase::CBotDialogBase()
    : m_disabledOwner(NULL)
    , m_font(NULL)
{
}

CBotDialogBase::~CBotDialogBase()
{
    RestoreOwner();
}

void CBotDialogBase::BringToFront()
{
    if (!IsWindow())
        return;
    if (IsIconic())
        ShowWindow(SW_RESTORE);
    else
        ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
}

bool CBotDialogBase::CreateFramed(HWND owner, const std::wstring& title, bool minimizable)
{
    HWND validOwner = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    //用 WS_OVERLAPPEDWINDOW 建窗，有无所有者，控件通知都发回本窗口（开发约定文档 UI 约定第 11 条）。2026-10-14 之前
    //有所有者的 WS_POPUP 窗口的通知会发到所有者，当时为此选用重叠窗口；现在两种样式的通知都发回本窗口
    if (Create(validOwner, CWindow::rcDefault, title.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("bot dialog: Create failed, error=%lu.", ::GetLastError());
        return false;
    }
    SetTitleBarHeight(kBotTitleBarHeight);
    SetTitle(title.c_str());
    SetButtons(minimizable, false, true);
    SetResizable(false);
    //程序图标：标题栏左侧的位图（图标资源在 exe 里，见 TitleBarIcon.h）；没有所有者的窗口另设任务栏图标
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    if (validOwner == NULL)
        m_programIcons.ApplyTaskIcons(m_hWnd);
    m_font = WindowFont(GetDpi());
    return true;
}

void CBotDialogBase::FinishFramed(std::unique_ptr<balloonwjui::DuiControl> body, int width, int height, HWND owner)
{
    SetClientContent(std::move(body));
    //整窗尺寸含标题栏（UI 约定第 7 条）；固定尺寸的窗口下限与尺寸相同
    SetMinSize(width, height);
    ResizeClient(width, height);
    CenterOnOwner(owner);
}

void CBotDialogBase::UpdateTitle(const std::wstring& title)
{
    if (!IsWindow())
        return;
    SetTitle(title.c_str());
    SetWindowTextW(title.c_str());
}

void CBotDialogBase::DisableOwner(HWND owner)
{
    if (owner == NULL || !::IsWindow(owner) || m_disabledOwner != NULL)
        return;
    m_disabledOwner = owner;
    ::EnableWindow(owner, FALSE);
}

void CBotDialogBase::RestoreOwner()
{
    if (m_disabledOwner == NULL)
        return;
    HWND owner = m_disabledOwner;
    m_disabledOwner = NULL;
    if (!::IsWindow(owner))
        return;
    //先恢复所有者再销毁本窗口，系统才会把前台交还给所有者，而不是别的程序
    ::EnableWindow(owner, TRUE);
    ::SetForegroundWindow(owner);
}

HFONT CBotDialogBase::Font() const
{
    return m_font;
}

void CBotDialogBase::CenterOnOwner(HWND owner)
{
    RECT self = { 0, 0, 0, 0 };
    GetWindowRect(&self);
    const int width = self.right - self.left;
    const int height = self.bottom - self.top;
    RECT anchor = { 0, 0, 0, 0 };
    HMONITOR monitor = NULL;
    if (owner != NULL && ::IsWindow(owner) && ::IsWindowVisible(owner) && !::IsIconic(owner))
    {
        ::GetWindowRect(owner, &anchor);
        monitor = ::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        POINT pt = { 0, 0 };
        ::GetCursorPos(&pt);
        monitor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(monitor, &mi))
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    const RECT work = mi.rcWork;
    if (anchor.right <= anchor.left)
        anchor = work;
    int x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
    int y = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;
    //整窗落在工作区内
    if (x + width > work.right)
        x = work.right - width;
    if (y + height > work.bottom)
        y = work.bottom - height;
    if (x < work.left)
        x = work.left;
    if (y < work.top)
        y = work.top;
    SetWindowPos(NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace flamingobot
