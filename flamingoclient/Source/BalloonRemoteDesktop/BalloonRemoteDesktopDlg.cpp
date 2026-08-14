/**
 *  远程桌面会话提示对话框实现，BalloonRemoteDesktopDlg.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "BalloonRemoteDesktopDlg.h"

#include <memory>

#include "../IULog.h"
#include "../resource.h"                   // IDR_MAINFRAME（任务栏 / Alt-Tab 图标）
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标）
#include "../BalloonMain/CornerPopupPlacement.h"   // MoveWindowToWorkAreaCorner（贴右下角）
#include "../net/IUSocket.h"      // CIUSocket::GetInstance().SendRemoteDesktopXxx
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

// ---- 固定尺寸（逻辑像素）-------------------------------------------------
static const int kDlgWidth     = 340;   // 客户区宽
static const int kDlgHeight    = 128;   // 客户区高
static const int kDlgTitleBarH = 44;    // 标题栏高
static const int kMsgH         = 62;    // 提示文字区高
static const int kBottomBarH   = 34;    // 底部按钮栏高
static const int kBtnWidth     = 88;    // 按钮宽
static const int kBtnGap       = 12;    // 两按钮间距
static const COLORREF kMsgColor = RGB(0x33, 0x33, 0x33);   // 提示文字色

// 标题栏左侧程序图标的边长（逻辑像素）。balloonui 最终按 16x16 绘制，取同值可免去缩放。
static const int kTitleIconSize = 16;
// 标题栏底色。本窗口没有把标题栏设成透明，balloonui 画的就是这个颜色。图标位图必须
// 合成到同色底上，否则图标四周会出现一块与标题栏不同色的方块，缘由详见 TitleBarIcon.h。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);

// ---- 控件 ctrlId（避开 DuiFrameWindow 保留的 1/2/3）---------------------
static const UINT kIdBtnLeft  = 10;   // 次按钮：授权场景的"拒绝"
static const UINT kIdBtnRight = 11;   // 主按钮：授权"接受" / 呼叫"取消"

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
    if (Create(NULL, CWindow::rcDefault, _T("远程桌面"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("远程桌面对话框 Create 失败。"));
        return;
    }
    SetTitle(_T("远程桌面"));
    SetTitleBarHeight(kDlgTitleBarH);
    //最小化 + 关闭。本窗口是无属主的独立顶层窗口（上面 Create 的第一个参数为 NULL、
    //也没有 WS_EX_TOOLWINDOW），最小化后在任务栏上有自己的按钮，点回来即可还原。
    //不给最大化：客户区是固定的 340x128，拉大只会留一片空白。
    SetButtons(true, false, true);
    SetResizable(false);
    SetMinSize(kDlgWidth, kDlgHeight + kDlgTitleBarH);

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
    if (m_hTaskIconBig == NULL)
    {
        m_hTaskIconBig = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                          ::GetSystemMetrics(SM_CXICON),
                                          ::GetSystemMetrics(SM_CYICON));
    }
    if (m_hTaskIconSmall == NULL)
    {
        m_hTaskIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                            ::GetSystemMetrics(SM_CXSMICON),
                                            ::GetSystemMetrics(SM_CYSMICON));
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
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());

    std::unique_ptr<balloonwjui::DuiLabel> msg(new balloonwjui::DuiLabel());
    msg->SetTextColor(kMsgColor);
    msg->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    m_msgLabel = msg.get();
    root->AddChild(std::move(msg), balloonwjui::DuiLayout::Hint().Fixed(kMsgH));

    std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());

    // 按钮组整体水平居中：左右各放一块等权重的弹性空白把按钮夹在中间，
    // 避免按钮紧贴窗口右边缘（与通话邀请对话框 CBalloonCallDlg 同一套观感）。
    std::unique_ptr<balloonwjui::DuiControl> spacerLeft(new balloonwjui::DuiControl());
    bottom->AddChild(std::move(spacerLeft), balloonwjui::DuiLayout::Hint().Weight(1));

    m_btnLeft  = NULL;
    m_btnRight = NULL;

    if (m_mode == kModeAuth)
    {
        // 拒绝（次要）
        std::unique_ptr<balloonwjui::DuiButton> reject(new balloonwjui::DuiButton());
        reject->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        reject->SetVariant(balloonwjui::DuiButton::Variant::Default);
        reject->SetCtrlId(kIdBtnLeft);
        reject->SetText(_T("拒绝"));
        m_btnLeft = reject.get();
        bottom->AddChild(std::move(reject), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kBtnGap));

        // 接受（主要）
        std::unique_ptr<balloonwjui::DuiButton> accept(new balloonwjui::DuiButton());
        accept->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        accept->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        accept->SetCtrlId(kIdBtnRight);
        accept->SetText(_T("接受"));
        m_btnRight = accept.get();
        bottom->AddChild(std::move(accept), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));
    }
    else
    {
        // 呼叫中：只有"取消"
        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdBtnRight);
        cancel->SetText(_T("取消"));
        m_btnRight = cancel.get();
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));
    }

    // 右侧弹性空白：与左侧那块等权重，二者一起把按钮组顶到水平正中
    std::unique_ptr<balloonwjui::DuiControl> spacerRight(new balloonwjui::DuiControl());
    bottom->AddChild(std::move(spacerRight), balloonwjui::DuiLayout::Hint().Weight(1));

    root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));

    SetClientContent(std::move(root));
    m_built = true;
}

void CBalloonRemoteDesktopDlg::ApplyMode()
{
    if (m_msgLabel == NULL)
        return;

    if (m_mode == kModeAuth)
    {
        CString text = m_peerName + _T(" 请求远程控制你的电脑");
        m_msgLabel->SetText(text);
    }
    else
    {
        CString text = _T("正在呼叫 ") + m_peerName + _T("，等待对方接受…");
        m_msgLabel->SetText(text);
    }
}

void CBalloonRemoteDesktopDlg::OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeCalling;
    m_peerName   = peerName;
    m_peerUserId = peerUserId;
    m_sessionId.clear();   // 会话 id 待邀请应答回来后由协调器 SetSessionId 回填

    BuildUiOnce();
    ApplyMode();
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
    ApplyMode();
    ShowAtWorkAreaCorner();
    LOG_INFO("RD dialog open (auth), from=%lld, session=%s.",
             (long long)fromUserId, sessionId.c_str());
}

void CBalloonRemoteDesktopDlg::ShowAtWorkAreaCorner()
{
    //先定尺寸再算落点：落点是按 GetWindowRect 的实际宽高从工作区右下角反推的，
    //顺序反过来就会按旧尺寸贴角，改完尺寸后右边缘超出工作区。
    ResizeClient(kDlgWidth, kDlgHeight);

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
    // 协调器主动关闭：不再发信令（关闭原因已由服务端应答 / 推送决定）。
    if (IsWindow())
        DestroyWindow();
}

LRESULT CBalloonRemoteDesktopDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 用户点标题栏关闭按钮：等同取消（呼叫）/ 拒绝（授权）。
    if (m_mode == kModeCalling)
    {
        if (!m_sessionId.empty())
            CIUSocket::GetInstance().SendRemoteDesktopCancel(m_sessionId);
    }
    else
    {
        if (!m_sessionId.empty())
            CIUSocket::GetInstance().SendRemoteDesktopResponse(m_sessionId, false);
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
        case kIdBtnLeft:
            // 授权场景的"拒绝"
            if (!m_sessionId.empty())
                CIUSocket::GetInstance().SendRemoteDesktopResponse(m_sessionId, false);
            CloseDlg();
            return 0;

        case kIdBtnRight:
            if (m_mode == kModeCalling)
            {
                // 呼叫中"取消"
                if (!m_sessionId.empty())
                    CIUSocket::GetInstance().SendRemoteDesktopCancel(m_sessionId);
            }
            else
            {
                // 授权"接受"
                if (!m_sessionId.empty())
                    CIUSocket::GetInstance().SendRemoteDesktopResponse(m_sessionId, true);
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
