/**
 *  屏幕共享发起人一侧的摄像头浮动窗口，见 ShareCameraWindow.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareCameraWindow.h"

#include <memory>
#include <string>
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace screenshare {

namespace {

//标题栏高（与 ComputeCameraWindowRect 计算高度时用的一致）
const int kTitleBarH = 28;
//小窗的刷新定时器：每 100 毫秒一次；id 取较大值避开 balloonui 基类可能占用的小 id
const UINT_PTR kRefreshTimerId = 501;
const UINT kRefreshMs = 100;
//小把手：按钮四周的留白、摄像头图标的边长与图标后的间距（像素）
const int kHandlePad = 4;
const int kHandleIconPx = 16;
const int kHandleIconGap = 4;
//小把手底色（与工具条一致的深灰）与图标颜色
const COLORREF kHandleBgColor = RGB(44, 48, 56);
const COLORREF kHandleIconColor = RGB(255, 255, 255);

} // namespace

CShareCameraWindow::CShareCameraWindow()
    : m_panel(NULL)
    , m_source(NULL)
    , m_handle(NULL)
    , m_collapsed(false)
    , m_overflow(0)
    , m_cameraIcon(NULL)
{
    memset(&m_expandedRect, 0, sizeof(m_expandedRect));
}

CShareCameraWindow::~CShareCameraWindow()
{
    Close();
    //摄像头图标：按钮随窗口销毁之后才释放
    if (m_cameraIcon != NULL)
    {
        ::DeleteObject(m_cameraIcon);
        m_cameraIcon = NULL;
    }
}

void CShareCameraWindow::SetSource(IShareCameraFrames* source)
{
    m_source = source;
    if (m_panel != NULL)
        m_panel->SetSource(source);
}

void CShareCameraWindow::EnsureWindow()
{
    if (IsWindow())
        return;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, TR(L"摄像头"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create camera window failed.");
        return;
    }
    SetTitle(TR(L"摄像头"));
    SetResizable(false);
    //摄像头小窗不能被采进共享画面
    ExcludeWindowFromCapture(m_hWnd);
    SetTimer(kRefreshTimerId, kRefreshMs, NULL);
}

int CShareCameraWindow::peopleCount() const
{
    return static_cast<int>(m_tiles.size()) + (m_overflow > 0 ? m_overflow : 0);
}

void CShareCameraWindow::applyLayout()
{
    if (!IsWindow())
        return;
    if (m_collapsed)
    {
        //收起：标题条取最小高度、不放按钮，内容换成一个带摄像头图标与人数的按钮
        if (m_handle == NULL)
        {
            m_panel = NULL;
            SetTitleBarHeight(kCameraHandleTitleH);
            SetButtons(false, false, false);
            std::unique_ptr<balloonwjui::DuiVBox> row(new balloonwjui::DuiVBox());
            row->SetBgColor(kHandleBgColor);
            row->SetPadding(kHandlePad, kHandlePad, kHandlePad, kHandlePad);
            std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
            btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            btn->SetCtrlId(kIdHandle);
            if (m_cameraIcon == NULL)
                m_cameraIcon = MakeShareBarIconBitmap(share_bar_icon_camera, kHandleIconPx, kHandleIconColor);
            btn->SetLeadingIcon(m_cameraIcon);
            btn->SetLeadingIconSize(kHandleIconPx);
            btn->SetLeadingIconGap(kHandleIconGap);
            m_handle = btn.get();
            row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Weight(1));
            SetClientContent(std::move(row));
            //底图与工具条的按钮同一样式；保留按钮文字（人数）
            m_art.Apply(m_handle, ShareBarLookFor(share_bar_viewers, false), TR(L"展开摄像头小窗"), true);
        }
        m_handle->SetText(std::to_wstring(peopleCount()).c_str());
        m_handle->Invalidate();
        const RECT rc = ComputeCameraHandleRect(m_expandedRect);
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    //展开：标题栏带 X（收起），内容是小窗面板
    if (m_panel == NULL)
    {
        m_handle = NULL;
        SetTitleBarHeight(kTitleBarH);
        SetButtons(false, false, true);
        std::unique_ptr<CShareCameraPanel> panel(new CShareCameraPanel());
        panel->SetSource(m_source);
        m_panel = panel.get();
        SetClientContent(std::move(panel));
    }
    m_panel->SetTiles(m_tiles, m_overflow);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_expandedRect.left, m_expandedRect.top,
                   m_expandedRect.right - m_expandedRect.left, m_expandedRect.bottom - m_expandedRect.top,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CShareCameraWindow::Update(const std::vector<ShareCameraTile>& tiles, int overflowCount, const RECT& monitorRect)
{
    m_tiles = tiles;
    m_overflow = overflowCount;
    if (tiles.empty() && overflowCount <= 0)
    {
        //没有人开摄像头：小窗与把手都隐藏，收起状态保留
        if (IsWindow())
            ShowWindow(SW_HIDE);
        return;
    }
    EnsureWindow();
    if (!IsWindow())
        return;
    //贴在被共享显示器工作区的右边缘，高度随人数变化、不超过工作区
    HMONITOR mon = ::MonitorFromRect(&monitorRect, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    m_expandedRect = ComputeCameraWindowRect(work, static_cast<int>(tiles.size()), overflowCount);
    applyLayout();
}

void CShareCameraWindow::Close()
{
    m_panel = NULL;
    m_handle = NULL;
    //下一场共享从展开开始
    m_collapsed = false;
    m_tiles.clear();
    m_overflow = 0;
    if (IsWindow())
        DestroyWindow();
}

bool CShareCameraWindow::IsCollapsed() const
{
    return m_collapsed;
}

LRESULT CShareCameraWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kRefreshTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (m_panel != NULL && IsWindowVisible())
        m_panel->Refresh();
    return 0;
}

LRESULT CShareCameraWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //X 是「收起」（2026-10-12）：关闭按钮的点击还在处理中，收起要换掉标题栏与内容，投给自己等它处理完
    PostMessage(kMsgCameraCollapse, 1, 0);
    return 0;
}

LRESULT CShareCameraWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //点了小把手：展开会删掉把手按钮本身，投给自己等这次点击通知处理完（开发约定文档「UI 约定」第 13 条）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) &&
        n->ctrlId == static_cast<UINT>(kIdHandle))
    {
        PostMessage(kMsgCameraCollapse, 0, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareCameraWindow::OnCollapseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    const bool collapse = (wParam != 0);
    if (collapse == m_collapsed || !IsWindow())
        return 0;
    m_collapsed = collapse;
    LOG_INFO("screenshare: camera window %s.", collapse ? "collapsed" : "expanded");
    //没有人开摄像头时只记下状态，等下一次 Update 再显示
    if (m_tiles.empty() && m_overflow <= 0)
        return 0;
    applyLayout();
    return 0;
}

} // namespace screenshare
