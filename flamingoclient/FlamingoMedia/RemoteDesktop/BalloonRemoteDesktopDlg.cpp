/**
 *  远程桌面会话提示对话框实现，BalloonRemoteDesktopDlg.cpp
 *  balloonwj@qq.com   2026-06-09（2026-10-02 迁入 FlamingoMedia.dll）
 */
#include "stdafx.h"
#include "BalloonRemoteDesktopDlg.h"

#include <memory>

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/resource.h"                   // IDR_MAINFRAME（任务栏 / Alt-Tab 图标，取自 exe 模块的资源）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标）
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"   // MoveWindowToWorkAreaCorner（贴右下角）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "RdDialogLayout.h"                  // 提示文字、控件树与固定尺寸

// 尺寸、控件 id 与控件树都在 RdDialogLayout.h（2026-10-10 抽出，供 FlamingoUnit 断言排版）

// 标题栏左侧程序图标的边长（逻辑像素）。balloonui 最终按 16x16 绘制，取同值可免去缩放。
static const int kTitleIconSize = 16;
// 标题栏底色。本窗口没有把标题栏设成透明，balloonui 画的就是这个颜色。图标位图必须
// 合成到同色底上，否则图标四周会出现一块与标题栏不同色的方块，缘由详见 TitleBarIcon.h。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);

CBalloonRemoteDesktopDlg::CBalloonRemoteDesktopDlg()
    : m_mode(kModeCalling)
    , m_peerUserId(0)
    , m_built(false)
    , m_msgLabel(NULL)
    , m_btnLeft(NULL)
    , m_btnRight(NULL)
    , m_titleIconBmp(NULL)
    , m_hTaskIconBig(NULL)
    , m_hTaskIconSmall(NULL)
    , m_actions(NULL)
{
}

CBalloonRemoteDesktopDlg::~CBalloonRemoteDesktopDlg()
{
    //标题栏图标位图与任务栏图标由本类持有：DuiFrameWindow::SetIcon 只借用不释放，
    //WM_SETICON 交给系统的图标同样不由系统释放，都要在这里还回去。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_hTaskIconBig != NULL)
    {
        ::DestroyIcon(m_hTaskIconBig);
        m_hTaskIconBig = NULL;
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::DestroyIcon(m_hTaskIconSmall);
        m_hTaskIconSmall = NULL;
    }
}

void CBalloonRemoteDesktopDlg::EnsureWindow(HWND owner)
{
    if (IsWindow())
        return;

    // owner 传 NULL：让子控件通知自路由回本对话框（与关闭确认 / 改密对话框同处理）。
    if (Create(NULL, CWindow::rcDefault, TR(L"远程桌面"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("远程桌面对话框 Create 失败。"));
        return;
    }
    SetTitle(TR(L"远程桌面"));
    SetTitleBarHeight(rd::kRdDlgTitleBarH);
    //最小化 + 关闭。本窗口是无属主的独立顶层窗口（上面 Create 的第一个参数为 NULL、
    //也没有 WS_EX_TOOLWINDOW），最小化后在任务栏上有自己的按钮，点回来即可还原。
    //不给最大化：客户区是固定的 340x128，拉大只会留一片空白。
    SetButtons(true, false, true);
    SetResizable(false);
    SetMinSize(rd::kRdDlgWidth, rd::kRdDlgContentHeight + rd::kRdDlgTitleBarH);

    //标题栏左侧的程序图标。位图只建一次，跨"关闭再打开"复用；本类持有，析构时释放。
    if (m_titleIconBmp == NULL)
    {
        m_titleIconBmp = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBgColor);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    //任务栏 / Alt-Tab 图标。balloonui 的窗口类没有关联类图标，不显式设置的话，
    //窗口最小化后在任务栏上是一块空白 —— 本窗口现在有最小化按钮，这一步不能省。
    //图标资源在 exe 里：本类编进 FlamingoMedia.dll 后 AtlLoadIconImage 默认从 DLL 取资源会取不到，
    //故显式从 exe 模块（GetModuleHandle(NULL)）加载，与 TitleBarIcon.cpp 的做法一致。
    if (m_hTaskIconBig == NULL)
    {
        m_hTaskIconBig = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                            ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
                                            LR_DEFAULTCOLOR);
    }
    if (m_hTaskIconSmall == NULL)
    {
        m_hTaskIconSmall = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                              ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
                                              LR_DEFAULTCOLOR);
    }
    if (m_hTaskIconBig != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_hTaskIconBig);
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hTaskIconSmall);
    }
}

void CBalloonRemoteDesktopDlg::BuildUiOnce()
{
    // 每次 Open 按当前 m_mode 重建控件树：呼叫=一个"取消"，授权="拒绝/接受"两个，
    // 避免在两种形态间切换按钮可见性带来的布局刷新问题。
    SetClientContent(rd::BuildRdDialogBody(m_mode));
    balloonwjui::DuiControl* root = GetClientContent();
    m_msgLabel = (root != NULL) ? dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(rd::kRdIdMsg)) : NULL;
    m_btnLeft  = (root != NULL) ? dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(rd::kRdIdBtnLeft)) : NULL;
    m_btnRight = (root != NULL) ? dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(rd::kRdIdBtnRight)) : NULL;
    m_built = true;
}

void CBalloonRemoteDesktopDlg::ApplyMode()
{
    // 提示文字按提示区当前宽度折行、垂直居中，所以要在窗口尺寸定下来之后调用（见 ShowAtWorkAreaCorner）
    rd::ApplyRdDialogText(GetClientContent(), rd::MakeRdDialogText(m_mode, m_peerName));
}

void CBalloonRemoteDesktopDlg::OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeCalling;
    m_peerName   = peerName;
    m_peerUserId = peerUserId;
    m_sessionId.clear();   // 会话 id 待邀请应答回来后由远程桌面控制器 SetSessionId 回填

    BuildUiOnce();
    ShowAtWorkAreaCorner();
    LOG_INFO("RD dialog open (calling), to=%lld.", (long long)peerUserId);
}

void CBalloonRemoteDesktopDlg::OpenAuth(HWND owner, const CString& fromName,
                                        const std::string& sessionId, int64_t fromUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeAuth;
    m_peerName   = fromName;
    m_peerUserId = fromUserId;
    m_sessionId  = sessionId;

    BuildUiOnce();
    ShowAtWorkAreaCorner();
    LOG_INFO("RD dialog open (auth), from=%lld, session=%s.",
             (long long)fromUserId, sessionId.c_str());
}

void CBalloonRemoteDesktopDlg::ShowAtWorkAreaCorner()
{
    //先定尺寸再算落点：落点是按 GetWindowRect 的实际宽高从工作区右下角反推的，
    //顺序反过来就会按旧尺寸贴角，改完尺寸后右边缘超出工作区。
    //kRdDlgContentHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
    ResizeClient(rd::kRdDlgWidth, rd::kRdDlgContentHeight + rd::kRdDlgTitleBarH);

    //提示文字按提示区宽度折行后量高、垂直居中：必须在尺寸定下来之后、显示之前做，否则按旧宽度量高
    ApplyMode();

    //每次弹出都重新贴一次右下角：显示器分辨率、任务栏位置、用户当前在看哪块屏
    //都可能已经变了。取的是鼠标所在显示器的工作区（已排除任务栏）。
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd,
                                            balloonmain::kCornerPopupMarginX,
                                            balloonmain::kCornerPopupMarginY);

    ShowWindow(SW_SHOW);
    UpdateWindow();

    //置顶。放在 ShowWindow 之后，与「新公告」提示窗同一做法。SWP_NOACTIVATE 只是
    //说这一步不负责激活，紧接着的 SetForegroundWindow 才把窗口提到前台 —— 远程桌面
    //邀请要当场应答，不像公告那样可以不打扰用户。
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::SetForegroundWindow(m_hWnd);
}

void CBalloonRemoteDesktopDlg::CloseDlg()
{
    // 远程桌面控制器主动关闭：不再发信令（关闭原因已由服务端应答 / 推送决定）。
    if (IsWindow())
        DestroyWindow();
}

LRESULT CBalloonRemoteDesktopDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 用户点标题栏关闭按钮：等同取消（呼叫）/ 拒绝（授权）。
    if (m_actions == NULL)
    {
        LOG_INFO("RdDlg: no action receiver, close without signaling, mode=%d.", m_mode);
    }
    else if (m_mode == kModeCalling)
    {
        if (!m_sessionId.empty())
            m_actions->CancelInvite(m_sessionId);
    }
    else
    {
        if (!m_sessionId.empty())
            m_actions->RespondInvite(m_sessionId, false);
    }
    if (IsWindow())
        DestroyWindow();
    return 0;
}

LRESULT CBalloonRemoteDesktopDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case rd::kRdIdBtnLeft:
            // 授权场景的"拒绝"
            if (!m_sessionId.empty() && m_actions != NULL)
                m_actions->RespondInvite(m_sessionId, false);
            CloseDlg();
            return 0;

        case rd::kRdIdBtnRight:
            if (m_mode == kModeCalling)
            {
                // 呼叫中"取消"
                if (!m_sessionId.empty() && m_actions != NULL)
                    m_actions->CancelInvite(m_sessionId);
            }
            else
            {
                // 授权"接受"
                if (!m_sessionId.empty() && m_actions != NULL)
                    m_actions->RespondInvite(m_sessionId, true);
            }
            CloseDlg();
            return 0;

        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}
