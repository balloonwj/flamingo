/**
 *  BalloonLoginDlg.cpp
 *
 *  登录对话框实现。P0：骨架 —— 窗口以 DuiFrameWindow 形式弹出，DoModal
 *  跑手写模态消息循环并返回 IDOK / IDCANCEL；账号信息读写与发起登录
 *  （DoLogin / LoginThreadProc）从旧 CLoginDlg 原样迁移过来，保持行为
 *  不变。客户区暂为占位文字，真正的登录界面在 P1a 接入。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonLoginDlg.h"

#include <process.h>                 // _beginthreadex
#include <memory>                    // std::unique_ptr

#include "../FlamingoClient.h"       // CFlamingoClient / m_UserMgr
#include "../UserSessionData.h"      // FMG_MSG_LOGIN_RESULT / STATUS_ONLINE
#include "../EncodeUtil.h"           // EncodeUtil::UnicodeToUtf8
#include "../IULog.h"                // LOG_ERROR
#include "../net/Msg.h"              // online_type_pc_online
#include "../net/IUProtocolData.h"   // CLoginResult / LOGIN_FAILED 等
#include "../net/IUSocket.h"         // CIUSocket
#include "../net/LoginProtocolClient.h"  // v2 登录应答解析
#include "../BalloonRegister/BalloonRegisterWindow.h"        // 新版注册窗口（“注册账号”链接）
#include "../BalloonNetSettings/BalloonNetSettingsWindow.h"  // 新版网络设置窗口（“网络设置”链接）
#include "../Startup.h"              // CheckOnlyOneInstance
#include "../Update/UpdateManager.h"          // 登录前检查更新
#include "../BalloonUpdate/BalloonUpdateWindow.h"  // 查出新版本时弹升级窗
#include "../Path.h"                 // Hootina::CPath::GetAppPath（历史账号列表落盘路径）

#include "BalloonLoginLayout.h"      // BuildLoginBody / ValidateLoginInput / CtrlId
#include "AccountRemoval.h"          // 删除历史账号的二次确认与本地数据清理
#include "AuthBanner.h"              // AuthBanner（顶部 Banner，回填用户名 + 可点击状态胶囊）
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"          // 状态区文字
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h" // 登录中的不确定进度条
#include "../UIText.h"                                                  // UI_LOGIN_* 文案
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"     // 在线状态选择菜单
#include "../BalloonMain/MainPanelStatusMenu.h"  // GetPresenceVisual（状态色/文字，不含 Protocol.h，与 Msg.h 不冲突）
#include "../BalloonMain/MainPanelMenuIcons.h"   // AppendStatusMenuItems（状态菜单装配，与主面板共用）
#include "../BalloonMain/MainPanelIcons.h"       // kIconOnline（实心圆点图标）
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../ImageEx.h"             // CImageEx（菜单图标位图）
#include <vector>

// ---- 文件作用域常量 -----------------------------------------------------

// 登录窗固定客户区尺寸（逻辑像素）—— 对应设计稿 AuthShell 的 380×520。
static const int kLoginWindowWidth  = 380;
//窗口高度须容得下「状态区显示」那一屏：状态区（正在登录 / 失败原因）是固定高度，
//它一出现就把下方的注册 / 网络设置一行往下推。高度不够时多出来的部分直接排到客户区
//外面，那一行就看不见了。这个值由 Tests/BalloonLoginLayoutTests.cpp 的 LSA7 把关，
//改动状态区高度后若那条用例报溢出，就按它给出的溢出量把这里加够。
static const int kLoginWindowHeight = 562;

// 账号下拉浮层一次最多显示几行。DuiComboBox 的行高默认 22 逻辑像素，故 20 行约
// 442 像素高 —— 在 768 高的屏幕上，从账号框（约在窗口顶部往下 254 处）往下弹也
// 落得进桌面工作区。取这个上限只是兜底；账号真多到装不下时该让浮层能滚，而滚动
// 要改 balloonui 的 DuiComboBoxPopup（它既没接滚轮消息、也不夹取落点），按项目
// 约定须先与用户对齐后再动。
static const int kAccountComboMaxRows = 20;

// 标题栏高度（逻辑像素）。标题栏透明、只承载 min/close 按钮；其同高的
// 橙色区由窗体 9-grid 背景图透出，与下方 Banner 渐变衔接，使 Banner
// 视觉上直抵窗口顶端。
static const int kLoginTitleBarHeight = 28;

// WM_SYSCOMMAND 的命令码在低 4 位保留作内部用途，取命令需与此掩码相与。
static const WPARAM kSysCommandMask = 0xFFF0;

// 驱动登录中那条 marquee 进度条的定时器 id。本窗口目前只有这一个定时器。
static const UINT_PTR kMarqueeTimerId = 1;
// 该定时器的间隔（毫秒）。约合 30Hz —— DuiProgressBar 的头文件里给的驱动示例就是
// 这个频率，再快对观感没有帮助，只是多占 CPU。
static const UINT kMarqueeIntervalMs = 33;
// 每次定时器触发时相位前进多少。与间隔取同一个数，使一个完整循环的时长
// 等于 DuiProgressBar::MarqueePeriod 个毫秒，滚动速度与该控件的设计一致。
static const int kMarqueePhaseStep = 33;

// 登录线程读取登录返回 JSON 时，业务成功对应的 code 值。
static const int kLoginRespCodeOk = 0;
// 服务器返回"用户未注册"的 code 值。
static const int kLoginRespCodeUnregistered = 102;
// 服务器返回"密码错误"的 code 值。
static const int kLoginRespCodePasswordError = 103;
// 服务器返回"客户端版本过低"的 code 值（服务端 error_code_version_too_old）。
// 这个错误重试多少次都是同一个结果，必须把用户引到升级流程，不能只报"登录失败"。
static const int kLoginRespCodeVersionTooOld = 4;

// 登录请求的超时时间（秒）。
static const int kLoginTimeoutSeconds = 3;
// 登录请求里固定的客户端类型参数（沿用旧 CLoginDlg 的取值）。
static const int kLoginClientType = 1;

// 账号 / 密码转 UTF-8 后的缓冲区长度（字节）。
static const int kAccountUtf8BufSize = 64;

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonLoginDlg::CBalloonLoginDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_pLoginAccountList(NULL)
    , m_pAccountCombo(NULL)
    , m_pPasswordEdit(NULL)
    , m_pLoginButton(NULL)
    , m_pRememberPwd(NULL)
    , m_pAutoLogin(NULL)
    , m_pBanner(NULL)
    , m_pStatusArea(NULL)
    , m_pStatusText(NULL)
    , m_pStatusProgress(NULL)
    , m_dwMarqueeTimerId(0)
    , m_loginPresence(kMyPresenceOnline)   // 默认在线；由头像下状态胶囊菜单覆盖
    , m_preLoginUpdateChecked(false)
    , m_modalRunning(false)
    , m_modalResult(IDCANCEL)
    , m_hBgBitmap(NULL)
{
    memset(&m_stAccountInfo, 0, sizeof(m_stAccountInfo));
    m_stAccountInfo.nStatus = STATUS_ONLINE;
    // frame 外观（标题栏透明 / 按钮 / 背景图等）须在 Create 之后配置，
    // 见 ConfigureFrame —— DoModal 在 Create 后调用它。
}

CBalloonLoginDlg::~CBalloonLoginDlg()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。窗体背景图
    // 由本类持有，需在此释放。
    if (m_hBgBitmap != NULL)
    {
        ::DeleteObject(m_hBgBitmap);
        m_hBgBitmap = NULL;
    }
}

// =======================================================================
// 账号信息读写
// =======================================================================

BOOL CBalloonLoginDlg::GetLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount)
{
    if (lpAccount == NULL)
        return FALSE;

    memcpy(lpAccount, &m_stAccountInfo, sizeof(LOGIN_ACCOUNT_INFO));
    return TRUE;
}

BOOL CBalloonLoginDlg::SetLoginAccountInfo(const LOGIN_ACCOUNT_INFO* lpAccount)
{
    if (lpAccount == NULL)
        return FALSE;

    memcpy(&m_stAccountInfo, lpAccount, sizeof(LOGIN_ACCOUNT_INFO));
    return TRUE;
}

void CBalloonLoginDlg::SetDefaultAccount(PCTSTR pszDefaultAccount)
{
    m_strDefaultAccount = (pszDefaultAccount != NULL) ? pszDefaultAccount : _T("");
}

void CBalloonLoginDlg::SetDefaultPassword(PCTSTR pszDefaultPassword)
{
    m_strDefaultPassword = (pszDefaultPassword != NULL) ? pszDefaultPassword : _T("");
}

// =======================================================================
// 模态显示
// =======================================================================

INT_PTR CBalloonLoginDlg::DoModal(HWND hWndParent, LPARAM /*dwInitParam*/)
{
    m_modalResult  = IDCANCEL;
    m_modalRunning = true;

    //每次把登录窗摆出来都从空闲开始。自动登录那条路会在窗口尚未创建时直接调
    //DoLogin 把状态机推进「登录中」，而它失败时结果不归本窗口处理，没人把状态
    //推回来；不复位的话用户点第一下「登录」会被当成「取消登录」而毫无反应。
    //详见 auth::LoginProgressState::reset 的说明。
    m_loginProgress.reset();

    // 创建无边框 DUI 顶层窗口。WS_THICKFRAME 须保留 —— DuiFrameWindow 靠
    // WM_NCCALCSIZE 抠掉系统 chrome，但需要它维持 Aero snap / 最大化动画。
    if (Create(hWndParent, CWindow::rcDefault, _T("Flamingo"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("登录窗口 Create 失败。"));
        m_modalRunning = false;
        return IDCANCEL;
    }

    ConfigureFrame();
    InitLoginUi();
    ResizeClient(kLoginWindowWidth, kLoginWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    // 模态化：禁用父窗口，跑独立消息循环，直至 EndModal 把 m_modalRunning
    // 置 false。触发关闭 / 登录的消息被 DispatchMessage 处理后，下一行的
    // 标志检查即跳出循环（与 XChat SettingsFrame::RunModal 同思路）。
    if (hWndParent != NULL)
        ::EnableWindow(hWndParent, FALSE);

    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (!m_modalRunning)
            break;
    }

    if (hWndParent != NULL)
    {
        ::EnableWindow(hWndParent, TRUE);
        ::SetForegroundWindow(hWndParent);
    }

    // 模态循环结束后统一销毁窗口 —— DestroyWindow 返回时 WM_NCDESTROY
    // 已处理完，m_hWnd 复位为 NULL，下次 DoModal 可再次 Create。
    if (IsWindow())
        DestroyWindow();

    return m_modalResult;
}

void CBalloonLoginDlg::EndModal(INT_PTR result)
{
    m_modalResult  = result;
    m_modalRunning = false;
}

// 生成窗体 9-grid 背景图：上 kLoginTitleBarHeight 像素橙色（与 Banner
// 渐变起点同色），其余白色。配合 SetBgImage 的 9-grid inset，橙色条恒定
// 在顶部、白色区拉伸填充表单区。返回的 HBITMAP 由调用方负责释放。
static HBITMAP CreateFrameBgBitmap()
{
    const int kBmpW = 4;
    // 位图高度跟随标题栏高度 —— 上半 kLoginTitleBarHeight 橙色，余下白色，
    // 留足下方 9-grid inset（4px）+ 可拉伸中段。
    const int kBmpH = kLoginTitleBarHeight + 8;

    HDC hdcScreen = ::GetDC(NULL);
    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, kBmpW, kBmpH);
    if (hbm != NULL)
    {
        HDC mem = ::CreateCompatibleDC(hdcScreen);
        HGDIOBJ old = ::SelectObject(mem, hbm);

        RECT rcOrange = { 0, 0, kBmpW, kLoginTitleBarHeight };
        HBRUSH brOrange = ::CreateSolidBrush(RGB(0xFF, 0xB7, 0x93));
        ::FillRect(mem, &rcOrange, brOrange);
        ::DeleteObject(brOrange);

        RECT rcWhite = { 0, kLoginTitleBarHeight, kBmpW, kBmpH };
        HBRUSH brWhite = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(mem, &rcWhite, brWhite);
        ::DeleteObject(brWhite);

        ::SelectObject(mem, old);
        ::DeleteDC(mem);
    }
    ::ReleaseDC(NULL, hdcScreen);
    return hbm;
}

void CBalloonLoginDlg::ConfigureFrame()
{
    // 标题栏：透明、无标题文字、白色 min/close glyph、无最大化按钮。
    SetTitle(_T(""));
    SetTitleBarHeight(kLoginTitleBarHeight);
    SetTitleBarTransparent(true);
    SetCaptionGlyphColor(RGB(255, 255, 255));
    SetButtons(true, false, true);
    SetResizable(false);
    SetMinSize(kLoginWindowWidth, kLoginWindowHeight);

    // 窗体背景图："上橙下白" 9-grid —— 透明标题栏透出顶部橙色条，与下方
    // AuthBanner 渐变衔接；白色区作为表单区底色。inset.top = 标题栏高度，
    // 使橙色条恒定不拉伸。
    if (m_hBgBitmap == NULL)
    {
        m_hBgBitmap = CreateFrameBgBitmap();
    }
    if (m_hBgBitmap != NULL)
    {
        RECT bgInsets = { 1, kLoginTitleBarHeight, 1, 4 };
        SetBgImage(m_hBgBitmap, bgInsets);
    }
}

CString CBalloonLoginDlg::ResolveAccountFromCombo(int nNotifyExtra) const
{
    if (m_pAccountCombo == NULL)
    {
        return CString();
    }

    // 从下拉列表选中一项时，通知带来的项索引才是当时正确的账号；内嵌 EDIT
    // 的文本要等通知返回之后才被 DuiComboBox 改写，此刻读它拿到的是旧账号。
    if (balloonauth::ShouldReadAccountFromComboItem(nNotifyExtra,
                                                    m_pAccountCombo->GetCount()))
    {
        return m_pAccountCombo->GetItemText(nNotifyExtra);
    }

    // 手工输入，或调用方并非由通知触发 —— EDIT 里就是最新文本。
    return m_pAccountCombo->GetText();
}

void CBalloonLoginDlg::UpdateBannerUserName(const CString& account)
{
    if (m_pBanner == NULL)
    {
        return;
    }

    // 曾登录成功过的账号在历史登录列表里存着他的姓名（登录成功时由 CMainDlg
    // 写入），切到这类账号时头像下方显示姓名而不是账号；查不到账号、或存下的
    // 姓名为空（老版本客户端存的账号 / 服务器没给昵称）则仍显示账号。
    LOGIN_ACCOUNT_INFO* lpAccount = NULL;
    if (m_pLoginAccountList != NULL && !account.IsEmpty())
    {
        lpAccount = m_pLoginAccountList->Find(account);
    }
    CString nickName;
    if (lpAccount != NULL)
    {
        nickName = lpAccount->szNickName;
    }
    CString displayName = balloonauth::ChooseBannerDisplayName(account, nickName);
    m_pBanner->SetUserName(displayName);

    // 显示的是姓名而非账号时记一条，便于凭日志确认"切到登录过的账号就显示
    // 他的姓名"这条链路生效。两者相同（没有姓名）时不记 —— 账号输入框每敲
    // 一个字符都会走到这里，无谓刷日志。
    if (displayName != account)
    {
        LOG_INFO(_T("登录窗切到账号 %s，头像下方显示姓名 %s。"),
                 (LPCTSTR)account, (LPCTSTR)displayName);
    }

    // 头像底色随账号变化：账号在历史登录列表里（= 曾登录成功过）则按账号
    // 稳定取色，使不同账号头像可凭颜色区分；未登录过 / 账号为空则用默认
    // 橙色方块。判定"是否登录过"直接查历史账号列表（登录成功才会被 Add）。
    bool bKnownAccount = (lpAccount != NULL);
    m_pBanner->SetAvatarColor(
        balloonauth::ChooseBannerAvatarColor(account, bKnownAccount));
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonLoginDlg::InitLoginUi()
{
    // 构建登录界面客户区控件树（来自 BalloonLoginLayout 的内嵌 XML）。
    std::unique_ptr<balloonwjui::DuiControl> root = balloonauth::BuildLoginBody();
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildLoginBody 返回空 —— 登录窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 控件点名：按 BalloonLoginLayout 约定的数字 id 取回关键控件指针。
    m_pAccountCombo = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(balloonauth::kIdAccountCombo));
    m_pPasswordEdit = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonauth::kIdPasswordEdit));
    m_pLoginButton = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonauth::kIdLoginButton));
    m_pRememberPwd = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonauth::kIdRememberPwd));
    m_pAutoLogin = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonauth::kIdAutoLogin));
    m_pBanner = dynamic_cast<balloonauth::AuthBanner*>(
        content->FindCtrlById(balloonauth::kIdLoginBanner));
    m_pStatusArea = content->FindCtrlById(balloonauth::kIdStatusArea);
    m_pStatusText = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonauth::kIdStatusText));
    m_pStatusProgress = dynamic_cast<balloonwjui::DuiProgressBar*>(
        content->FindCtrlById(balloonauth::kIdStatusProgress));

    // 状态区平时不占位置：只有登录中与登录失败时才显示
    if (m_pStatusArea != NULL)
        m_pStatusArea->SetVisible(false);

    // 密码框内嵌真 Win32 EDIT —— XML builder 只建控件对象、不调
    // EnsureCreated，须在 host HWND 就绪后由调用方显式建一次。
    if (m_pPasswordEdit != NULL && !m_pPasswordEdit->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("登录窗密码输入框 EnsureCreated 失败。"));
    }

    // 密码框启用"小眼睛"切换按钮 —— 点击在密文 / 明文显示间切换。
    if (m_pPasswordEdit != NULL)
    {
        m_pPasswordEdit->SetShowEyeToggle(true);
    }

    // 填充历史账号下拉框并预填账号 / 密码。
    PopulateAccountCombo();

    LOG_INFO("login window client UI ready (combo=%p, pwd=%p).",
             m_pAccountCombo, m_pPasswordEdit);
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonLoginDlg::OnSysCommand(UINT /*uMsg*/, WPARAM wParam,
                                       LPARAM /*lParam*/, BOOL& bHandled)
{
    // 最小化 —— 与旧 CLoginDlg 一致：隐藏窗口而非真正最小化到任务栏，
    // 登录窗驻留托盘，由托盘菜单（CMainDlg::OnMenu_ShowMainPanel）唤出。
    if ((wParam & kSysCommandMask) == SC_MINIMIZE)
    {
        ShowWindow(SW_HIDE);
        return 0;
    }

    // 其余 syscommand（含关闭按钮转来的 SC_CLOSE）交回基类 / DefWindowProc，
    // SC_CLOSE 经 DefWindowProc 会再生成 WM_CLOSE，由 OnCloseMsg 接住。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonLoginDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                     LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 用户关闭登录窗 —— 以 IDCANCEL 结束模态循环。消费本消息（不交回
    // DefWindowProc），窗口的销毁统一由 DoModal 循环退出后处理。
    EndModal(IDCANCEL);
    return 0;
}

LRESULT CBalloonLoginDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
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
        case balloonauth::kIdLoginButton:
            OnLoginClicked();
            return 0;
        case balloonauth::kIdLinkRegister:
            OnRegisterClicked();
            return 0;
        case balloonauth::kIdLinkNetwork:
            OnNetworkClicked();
            return 0;
        case balloonauth::kIdLinkForgotPwd:
            // 服务端无找回密码接口 —— 暂以提示占位。
            ::MessageBox(m_hWnd, _T("该功能暂未开放。"), _T("提示"),
                         MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            break;
        }
    }
    else if (pNotify->code == balloonwjui::DUIN_VALUECHANGED)
    {
        // “记住密码”取消时一并取消“自动登录”；“自动登录”勾选时自动勾上
        // “记住密码”（与旧 CLoginDlg 的两个复选框联动一致）。
        if (pNotify->ctrlId == balloonauth::kIdRememberPwd)
        {
            if (m_pRememberPwd != NULL && !m_pRememberPwd->IsChecked()
                && m_pAutoLogin != NULL)
            {
                m_pAutoLogin->SetCheck(false, false);
            }
            // 就地落盘 —— 勾选项此前要等登录成功才写盘，用户取消勾选后直接关掉
            // 登录窗的话改动就丢了，下次启动照旧自动登录。
            PersistCheckStateForCurrentAccount();
            return 0;
        }
        if (pNotify->ctrlId == balloonauth::kIdAutoLogin)
        {
            if (m_pAutoLogin != NULL && m_pAutoLogin->IsChecked()
                && m_pRememberPwd != NULL)
            {
                m_pRememberPwd->SetCheck(true, false);
            }
            PersistCheckStateForCurrentAccount();
            return 0;
        }
        // 账号下拉框选中 / 输入变化 —— 联动加载该账号的历史登录信息。
        //
        // 账号一律经 ResolveAccountFromCombo 解析，不直接读下拉框文本：从下拉
        // 列表选中一项时，DuiComboBox 先同步发出本通知、之后才把选中项文本写
        // 进内嵌 EDIT，此刻 GetText() 拿到的还是切换前的旧账号。下面回填密码
        // 与刷新头像用的必须是通知带来的那个账号，否则切换账号后头像下方的名
        // 字、回填的密码与勾选项都会滞留在上一个账号上。
        if (pNotify->ctrlId == balloonauth::kIdAccountCombo)
        {
            CString account = ResolveAccountFromCombo((int)pNotify->extra);
            LoadAccountIntoUi(account);
            UpdateBannerUserName(account);
            return 0;
        }
    }
    // 从这里往下是各控件的<u>自定义</u>通知码。这类码是<u>按控件各自编号</u>的
    // ——每个控件都从 balloonwjui::DUIN_CUSTOM 起算，于是不同控件的第一个自定义码
    // 数值必然相同（DuiComboBox::DUICBN_ITEMDELETE 与 AuthBanner::DUIN_BANNER_STATUS_CLICK
    // 都等于 DUIN_CUSTOM + 1）。所以判定<u>必须把 ctrlId 一起写进分支条件</u>，
    // 只比 code 会让排在前面的那个分支把别的控件的通知一并吃掉。
    //
    // 这正是"点击登录窗的在线状态胶囊没有反应"的成因（2026-08-12 定位）：胶囊点击
    // 先落进下面这个删除账号的分支，里面的 ctrlId 判定不成立就掉到函数末尾，
    // 后面那个 else if 根本没机会执行。
    else if (pNotify->code == (UINT)balloonwjui::DuiComboBox::DUICBN_ITEMDELETE
             && pNotify->ctrlId == balloonauth::kIdAccountCombo)
    {
        // 点了账号下拉里某一项右侧的删除叉。DuiComboBox 只发通知、不动数据，
        // 删不删由本窗口二次确认后决定。
        //
        // 这里<u>只投消息、不就地处理</u>：本通知是在账号下拉浮层的消息处理栈里
        // 同步发过来的，就地弹二次确认框会让模态框泵消息、浮层随即自我销毁，
        // 栈退回浮层的 handler 时那个对象已经没了（atlwin.h 会断言
        // "Object deleted before window was destroyed"）。延后一拍再做，
        // 那时浮层已经正常关闭。详见 WM_APP_REMOVE_ACCOUNT 的定义处。
        PostMessage(WM_APP_REMOVE_ACCOUNT, (WPARAM)pNotify->extra, 0);
        return 0;
    }
    else if (pNotify->code == (UINT)balloonauth::AuthBanner::DUIN_BANNER_STATUS_CLICK
             && pNotify->ctrlId == balloonauth::kIdLoginBanner)
    {
        // 点击头像下方"在线状态胶囊" —— 弹出在线状态选择菜单（在线 / 离开 / 忙碌）。
        ShowLoginStatusMenu();
        return 0;
    }

    // 其余通知（标题栏 min/close 按钮等）交回 DuiFrameWindow。
    bHandled = FALSE;
    return 0;
}

// =======================================================================
// 登录界面事件
// =======================================================================

void CBalloonLoginDlg::OnLoginClicked()
{
    //登录期间这个按钮是「取消登录」：把本轮作废、界面切回可输入。后台线程还阻塞在
    //socket 上、停不下来，它稍后带回来的结果会因轮次号对不上而被丢弃，
    //详见 auth::LoginProgressState 的说明。
    if (m_loginProgress.phase() == auth::login_phase_logining)
    {
        if (m_loginProgress.cancelLogin())
        {
            LOG_INFO("User cancelled the login in progress.");
            //取消是用户自己的动作，不必再留一行提示占着地方
            LeaveLoginingUi(NULL);
        }
        return;
    }

    // 这里原先调 DetectNewVersion() 走 v1 时代那套自动升级，且用 #ifndef _DEBUG
    // 把它挡在 Release 才生效。整条链路在 v2 改造后已经跑不通（协议与寻址方式都对不上），
    // 现已整体删除：检查更新改由登录成功后的静默检查、关于页按钮、以及登录被
    // error_code_version_too_old 拒绝这三个触发点驱动，见 Source/Update 与
    // Source/BalloonUpdate。要不要顺带下载由 flamingo.ini 的 [app] autodownloadupdate 控制，
    // 不再需要编译期开关。

    CString account;
    CString password;
    if (m_pAccountCombo != NULL)
        account = m_pAccountCombo->GetText();
    if (m_pPasswordEdit != NULL)
        password = m_pPasswordEdit->GetText();

    balloonauth::LoginInputResult vr = balloonauth::ValidateLoginInput(account, password);
    if (vr == balloonauth::kLoginInputNoAccount)
    {
        ::MessageBox(m_hWnd, _T("请输入账号！"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (vr == balloonauth::kLoginInputNoPassword)
    {
        ::MessageBox(m_hWnd, _T("请输入密码！"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 采集账号信息到 m_stAccountInfo。
    _tcsncpy_s(m_stAccountInfo.szUser, ARRAYSIZE(m_stAccountInfo.szUser),
               (LPCTSTR)account, _TRUNCATE);
    _tcsncpy_s(m_stAccountInfo.szPwd, ARRAYSIZE(m_stAccountInfo.szPwd),
               (LPCTSTR)password, _TRUNCATE);
    m_stAccountInfo.bRememberPwd = (m_pRememberPwd != NULL && m_pRememberPwd->IsChecked());
    m_stAccountInfo.bAutoLogin   = (m_pAutoLogin   != NULL && m_pAutoLogin->IsChecked());

    // 登录在线状态 m_loginPresence 由头像下"在线"胶囊的状态菜单（ShowLoginStatusMenu）实时
    // 写好，这里直接沿用——LoginThreadProc 取它作登录 presence。默认在线。

    // 记录当前用户（与旧 CLoginDlg 一致）。
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount = m_stAccountInfo.szUser;

    LOG_INFO(_T("登录窗发起登录 (account=%s)."), m_stAccountInfo.szUser);

    //先检查更新，查出可用新版本就把登录拦在这里。整个进程只查一次：用户点过「稍后」
    //之后再点登录不该又被拦一遍，否则永远登不进去。
    if (!m_preLoginUpdateChecked)
    {
        if (StartPreLoginUpdateCheck())
        {
            //<u>真的发起了才算查过</u>。这行原先写在 StartPreLoginUpdateCheck 之前，
            //于是那次检查因连不上服务器而没能发出去时，标志照样被消耗掉：此后再点
            //登录也不会重试，用户直接登进去，升级提示改由登录成功后的静默检查弹出
            //——表现就是"弹升级窗的同时人已经登录了"，而需求是可选更新须先弹窗、
            //用户点过「稍后」才继续登录。2026-08-14 实测复现过。
            m_preLoginUpdateChecked = true;
            //已发起，等应答回来再决定登不登
            return;
        }
        //没发起成功（多半是连不上服务器）：不记为已查，下次点登录再试一次。
        //本次照常登录——检查更新这条链路自己挂了不该把用户挡在门外。
    }

    //发起登录后<u>不再立即关窗</u>：窗口就地切到「登录中」，等结果回来再决定去留。
    //成功由 CMainDlg 关掉它，失败就地显示原因、界面回到可输入。
    //旧写法是发起后立刻 EndModal、失败时再由 StartLogin 重建一个登录窗，两者时序
    //稍有交错就会撞上 ATL 的 m_hWnd 断言——那个隐患也随之消掉了。
    DoLogin();
}

void CBalloonLoginDlg::ShowLoginStatusMenu()
{
    if (m_pBanner == NULL)
        return;
    RECT rcPill = m_pBanner->GetPillHitRect();
    if (::IsRectEmpty(&rcPill))
        return;

    // 登录可选三态：在线 / 离开 / 忙碌（不含"离线"——登录中离线无意义；也不含已删的"隐身"）。
    // 行模型与装配都走共用代码，和主面板状态菜单同一套：当前选中的那一项由模型标出
    // （带外环的圆点 + "（当前）"后缀），当前值取胶囊上正显示的 m_loginPresence。
    const int kStatusIconSize = 16;          // 菜单项圆点图标边长（与系统/状态菜单同口径）
    const int kStatusMenuGapPx = 4;          // 菜单顶边与胶囊底边的间隙

    balloonwjui::DuiMenu menu;
    std::vector<balloonmain::StatusMenuEntry> model =
        balloonmain::BuildLoginStatusMenuModel(m_loginPresence);
    std::vector<std::unique_ptr<CImageEx> > icons;   // 保活到 TrackPopup 返回（同步阻塞，局部即可）

    balloonmain::AppendStatusMenuItems(menu, icons, model, kStatusIconSize);

    // 锚定：菜单左对齐胶囊左边、顶边落在胶囊底边下方；再夹进显示器工作区兜底。
    POINT tl = { rcPill.left, rcPill.bottom + kStatusMenuGapPx };
    ::ClientToScreen(m_hWnd, &tl);
    SIZE  menuSize = menu.MeasureSize();
    POINT pos = balloonmain::ClampPopupMenuToWorkArea(tl, menuSize);

    UINT chosen = menu.TrackPopup(pos.x, pos.y, m_hWnd);   // 同步阻塞
    if (chosen == 0)
        return;   // 未选中（失焦 / ESC）

    // 把菜单 id 映射回目标在线状态：菜单 id（StatusMenuId）与 presence 是两套独立取值，
    // 不能直接强转（见 MainPanelStatusMenu.h 里 StatusMenuId 的说明）。
    int targetPresence = -1;
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].id == chosen)
        {
            targetPresence = model[i].presence;
            break;
        }
    }
    if (targetPresence < 0)
        return;   // 理论上不会发生（chosen 必为模型里某项的 id）

    // 存起来供登录使用，并刷新 Banner 胶囊的颜色 / 文字。
    m_loginPresence = targetPresence;
    m_pBanner->SetPresence(m_loginPresence);
    m_pBanner->Invalidate();
    LOG_INFO(_T("登录窗选择在线状态 presence=%d."), m_loginPresence);
}

void CBalloonLoginDlg::OnRegisterClicked()
{
    // 打开新版注册窗口（balloonui 两步流程），模态于登录窗。
    CBalloonRegisterWindow regDlg(m_lpFMGClient);
    if (regDlg.DoModal(m_hWnd) != IDOK)
    {
        // 用户中途放弃或注册未成功，登录框保持原样。
        return;
    }

    // 注册成功：登录账号是服务端自动生成的、用户没有手输过，密码则是他刚在
    // 注册窗里设的，两样都替他填进登录框，省得再照着完成页一个字符一个字符敲。
    // 具体回填成什么样由纯函数 MakePostRegisterFill 决定（可单测，见
    // Tests/BalloonLoginLayoutTests.cpp 的 PR 组用例）。
    const balloonauth::PostRegisterFill fill =
        balloonauth::MakePostRegisterFill(regDlg.RegisteredAccount(),
                                          regDlg.RegisteredPassword());
    if (!fill.m_shouldFill)
    {
        // 理论上不会发生：DoModal 返回 IDOK 就意味着服务端已下发账号。
        LOG_ERROR(_T("注册返回成功但账号为空，无法回填登录框。"));
        return;
    }
    const CString& account = fill.m_account;

    // 新账号还没登录过，不在历史账号列表里，因而也不在下拉列表里 —— 下拉列表
    // 是 PopulateAccountCombo 在窗口初始化时一次性灌进去的，之后没人再往里加。
    // 只 SetText 的话账号仅仅出现在编辑框的文本上，用户一拉下拉框就找不着它，
    // 得等到首次登录成功、CMainDlg 把它 Add 进列表之后才补得上。这里就地补齐。
    //
    // 加完立刻落盘：账号是在本机注册出来的，用户下次启动客户端理应还能在下拉
    // 列表里找到它，而不是非得先成功登录一次。落盘路径与 CMainDlg 读写该文件
    // 时用的是同一个（见 CMainDlg::SaveLoginAccountListFile）。
    if (m_pLoginAccountList != NULL && m_pLoginAccountList->Find(account) == NULL)
    {
        // 密码留空、两个勾选项都不勾 —— 用户此刻还没表达过"记住密码"的意愿，
        // 替他记下密码是越权；登录成功后 CMainDlg 会用真实采集到的信息覆盖本条。
        const BOOL bAdded = m_pLoginAccountList->Add(account, _T(""),
                                                     STATUS_ONLINE, FALSE, FALSE);

        int nComboCount = -1;
        if (bAdded && m_pAccountCombo != NULL)
        {
            nComboCount = m_pAccountCombo->AddString(account) + 1;
        }

        BOOL bSaved = FALSE;
        if (bAdded)
        {
            const tstring strFileName =
                Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
            bSaved = m_pLoginAccountList->SaveFile(strFileName.c_str());
        }

        LOG_INFO(_T("注册账号入历史列表: account=%s, 入列=%d, 落盘=%d, 列表条数=%d, 下拉项数=%d."),
                 (LPCTSTR)account, (int)bAdded, (int)bSaved,
                 m_pLoginAccountList->GetCount(), nComboCount);
    }
    else
    {
        // 走到这里说明要么登录窗没拿到历史账号列表（指针为空），要么这个账号
        // 已经在列表里了 —— 两种情形下拉列表里都不会新增一项，日志记下是哪种。
        LOG_INFO(_T("注册账号未入历史列表: account=%s, 列表指针空=%d, 已存在=%d."),
                 (LPCTSTR)account, (int)(m_pLoginAccountList == NULL),
                 (int)(m_pLoginAccountList != NULL
                       && m_pLoginAccountList->Find(account) != NULL));
    }

    if (m_pAccountCombo != NULL)
    {
        m_pAccountCombo->SetText(account);
        m_pAccountCombo->Invalidate();
    }

    // 密码框必须在这里显式回填。DuiComboBox::SetText 是压制通知的（理由见下
    // 一段），账号变化不会触发 OnDuiNotify 里那条联动、也就不会走
    // LoadAccountIntoUi，密码框里那串圆点仍是登录窗打开时 PopulateAccountCombo
    // 预填的内容 —— 上一个账号记住的密码，或本机无历史登录时的兜底测试密码。
    // 曾因此出现"账号是刚注册的、密码却是别人的，点登录报密码错误"。
    //
    // 密码只写进界面（显示为圆点），不写进上面那条历史账号记录 —— 用户还没
    // 勾"记住密码"，替他落盘存密码是越权。
    if (m_pPasswordEdit != NULL)
    {
        m_pPasswordEdit->SetText(fill.m_password);
    }

    // 两个勾选框一并复位，与上面写进历史列表的那条记录（两项皆否）对齐；
    // 否则它们会滞留在上一个账号的勾选状态上。notify 传 false —— 这是程序
    // 回填，不该触发用户手动勾选才走的 PersistCheckStateForCurrentAccount。
    if (m_pRememberPwd != NULL)
    {
        m_pRememberPwd->SetCheck(fill.m_rememberPwd, false);
    }
    if (m_pAutoLogin != NULL)
    {
        m_pAutoLogin->SetCheck(fill.m_autoLogin, false);
    }

    // DuiComboBox::SetText 是明确压制通知的（m_suppressEditNotify + SetCurSel
    // 的 notify=false），不会触发 OnDuiNotify 里那条"账号变了"的联动，所以
    // Banner 上的头像首字母与用户名得在这里手工同步一次 —— 否则账号框已经是
    // 新账号，Banner 却还停在上一个账号。PopulateAccountCombo 里 SetText 之后
    // 同样补了这一步。
    UpdateBannerUserName(account);

    // 账号与密码都已就位，焦点仍落到密码框：用户想核对或改密码时不必再点一下，
    // 直接就能编辑。
    if (m_pPasswordEdit != NULL)
    {
        m_pPasswordEdit->SetFocus();
    }

    // 密码不进日志，只记"填没填上"，便于日后仅凭日志判断这条路径是否走到。
    LOG_INFO(_T("注册成功，已把账号 %s 回填到登录框，密码框已回填=%d。"),
             (LPCTSTR)account, (int)(!fill.m_password.IsEmpty()));
}

void CBalloonLoginDlg::OnNetworkClicked()
{
    // 打开新版网络设置窗口（balloonui），模态于登录窗。
    // “保存”把三组服务器配置写回 flamingo.ini，下次连接 / 启动生效。
    CBalloonNetSettingsWindow netDlg(m_lpFMGClient);
    netDlg.DoModal(m_hWnd);
}


// =======================================================================
// 登录前检查更新
// =======================================================================

bool CBalloonLoginDlg::StartPreLoginUpdateCheck()
{
    //本函数<u>必须全程不阻塞</u>：它跑在 UI 线程上，是用户点下「登录」之后第一件
    //发生的事，这里停多久界面就僵多久。
    //
    //这里原先先调一次 CIUSocket::Connect() 提前建连，连不上就跳过检查直接登录。
    //那一句是同步的：gethostbyname 解析域名、connect 之后 select 最多等 3 秒、
    //还要做完协议握手的一问一答；服务器没起来时实测卡住 4 秒（2026-08-14 用户
    //反馈「点登录后界面卡一会儿」即由此而来）。而它其实是多余的 ——
    //UpdateManager 的工作线程走 CIUSocket::CheckUpdateSync，那个函数进来第一件事
    //就是自己 Connect()；连不上时结果里 transportOk 为假，OnPreLoginUpdateResult
    //照样放行登录，与原先「跳过检查直接登录」的效果一致，只是不再占着 UI 线程。
    //
    //先切界面再投任务，让用户点下按钮的一瞬间就看到「正在检查更新...」。
    SetLoginButtonChecking(true);

    //必须在这里把更新管理器的工作线程启起来。它平时是在 CIUSocket::Init 里启动的，
    //而那个函数要<u>登录成功之后</u>才被调用（CMainDlg::OnLoginResult 的 LOGIN_SUCCESS
    //分支）—— 此刻还没登录，线程没起，投进去的任务没人处理，界面就会一直停在
    //「正在检查更新...」再也不动。Init 可重复调用，登录成功后那次是空操作。
    update::UpdateManager::GetInstance().Init();

    //结果投给本窗口
    update::UpdateManager::GetInstance().SetNotifyTarget(m_hWnd);
    //false = 只查不下。要不要下载由用户在升级窗里决定。
    update::UpdateManager::GetInstance().CheckAndDownload(false);

    LOG_INFO(_T("登录前检查更新：已发起，登录暂缓。"));
    return true;
}

bool CBalloonLoginDlg::IsLogining() const
{
    return m_loginProgress.phase() == auth::login_phase_logining;
}

void CBalloonLoginDlg::CloseAfterLoginSuccess()
{
    EndModal(IDOK);
}

bool CBalloonLoginDlg::OnLoginResultArrived(long nLoginResultCode, uint32_t uGeneration)
{
    if (nLoginResultCode == LOGIN_SUCCESS)
    {
        if (!m_loginProgress.acceptSuccess(uGeneration))
        {
            //迟到的成功结果：用户已取消，或这是上一轮的。丢弃即可——上层不会关窗，
            //界面停在用户当前看到的样子。
            LOG_INFO("Login success result discarded, stale generation=%u.",
                     (unsigned)uGeneration);
            return false;
        }

        //成功之后窗口马上要关，只需把进度条定时器停掉，不必再改文字
        if (m_dwMarqueeTimerId != 0)
        {
            KillTimer(kMarqueeTimerId);
            m_dwMarqueeTimerId = 0;
        }
        return true;
    }

    if (!m_loginProgress.acceptFailure(uGeneration))
    {
        LOG_INFO("Login failure result discarded, stale generation=%u, code=%ld.",
                 (unsigned)uGeneration, nLoginResultCode);
        return false;
    }

    //就地显示失败原因：不弹 MessageBox，也不销毁重建本窗口。账号与口令都还在，
    //用户改完直接重试即可。
    LOG_INFO("Login failed, code=%ld, showing the reason in the login window.",
             nLoginResultCode);
    LeaveLoginingUi(auth::LoginErrorText(nLoginResultCode));
    return true;
}

LRESULT CBalloonLoginDlg::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                     BOOL& bHandled)
{
    if (wParam != kMarqueeTimerId)
    {
        //不是本窗口的定时器，交回默认处理，别把别人的定时器吞掉
        bHandled = FALSE;
        return 0;
    }

    bHandled = TRUE;

    //DuiProgressBar 的 marquee 不自驱动，相位由这里按固定步长推进；
    //相位在控件内部按 MarqueePeriod 取模，无需自己回绕。
    if (m_pStatusProgress != NULL && m_pStatusProgress->IsVisible())
    {
        m_pStatusProgress->SetMarqueePhase(m_pStatusProgress->GetMarqueePhase() + kMarqueePhaseStep);
        m_pStatusProgress->Invalidate();
    }

    return 0;
}

void CBalloonLoginDlg::RelayoutClientContent()
{
    //<u>状态区显隐之后必须重排一次</u>。DuiControl::SetVisible 只置标志位并 Invalidate，
    //<u>不会</u>触发重新布局（见 balloonui/DuiControl.cpp）。而竖直布局在排版时会跳过
    //不可见的子控件、把空间分给别人，于是状态区被隐藏期间若发生过一次重排，它自己
    //与两个子控件就停在了一份过时的矩形上；等 SetVisible(true) 时那份矩形并不会被
    //重新算过，进度条就照着它画——实测表现为进度条左边留着正常的内边距、右边却一直
    //顶到窗口边框（左 22 右 0，而同一容器里的登录按钮是左右各 22）。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_hWnd == NULL)
        return;

    //<u>重排要用客户区内容自己那份矩形，不能用整个窗口客户区</u>。
    //DuiFrameWindow 的骨架是「标题栏 + 客户区内容」上下两块，客户区内容拿到的是
    //标题栏<u>以下</u>那一块。这里若传 GetClientRect 的结果，等于把内容铺到整个
    //客户区、把标题栏那一条盖掉，表现为登录失败回到登录界面后<u>右上角的最小化与
    //关闭按钮不见了</u>（2026-08-14 用户反馈）。骨架分配给它的矩形就存在它自己的
    //m_rcItem 里，原样再排一遍即可，无需在此重算标题栏高度。
    const RECT rcContent = content->GetRect();
    if (rcContent.right <= rcContent.left || rcContent.bottom <= rcContent.top)
        return;   //还没被骨架布过局，此时无从重排，交给后续的正常布局

    //必须用 ForceLayout 而不是 SetRect：后者在矩形未变时会直接 return，
    //而这里矩形恰恰没变、变的是子控件的显隐，正需要强制重排一遍。
    content->ForceLayout(rcContent);
    content->Invalidate();
}

void CBalloonLoginDlg::SetInputEnabled(bool enabled)
{
    //登录期间禁用输入：一是避免用户改了账号却还在等上一个账号的结果，
    //二是让「界面正忙」这件事一眼可见。
    if (m_pAccountCombo != NULL)
    {
        m_pAccountCombo->SetEnabled(enabled);
        m_pAccountCombo->Invalidate();
    }
    if (m_pPasswordEdit != NULL)
    {
        m_pPasswordEdit->SetEnabled(enabled);
        m_pPasswordEdit->Invalidate();
    }
    if (m_pRememberPwd != NULL)
    {
        m_pRememberPwd->SetEnabled(enabled);
        m_pRememberPwd->Invalidate();
    }
    if (m_pAutoLogin != NULL)
    {
        m_pAutoLogin->SetEnabled(enabled);
        m_pAutoLogin->Invalidate();
    }
}

void CBalloonLoginDlg::EnterLoginingUi()
{
    SetInputEnabled(false);

    //主按钮就地变成「取消登录」。这里不另加一个取消按钮：主按钮占满整宽，
    //旁边塞不下第二个，而登录期间再点「登录」本来也没有意义。
    if (m_pLoginButton != NULL)
    {
        m_pLoginButton->SetText(UI_LOGIN_BTN_CANCEL);
        m_pLoginButton->SetEnabled(true);
        m_pLoginButton->Invalidate();
    }

    if (m_pStatusText != NULL)
        m_pStatusText->SetText(UI_LOGIN_STATUS_LOGINING);

    if (m_pStatusProgress != NULL)
    {
        m_pStatusProgress->SetMarquee(true);
        m_pStatusProgress->SetVisible(true);
    }

    if (m_pStatusArea != NULL)
    {
        m_pStatusArea->SetVisible(true);
        m_pStatusArea->Invalidate();
    }

    //DuiProgressBar 的 marquee 不自驱动，相位得由调用方按固定间隔推进
    if (m_dwMarqueeTimerId == 0)
        m_dwMarqueeTimerId = SetTimer(kMarqueeTimerId, kMarqueeIntervalMs, NULL);

    RelayoutClientContent();
}

void CBalloonLoginDlg::LeaveLoginingUi(PCTSTR pszMessage)
{
    if (m_dwMarqueeTimerId != 0)
    {
        KillTimer(kMarqueeTimerId);
        m_dwMarqueeTimerId = 0;
    }

    SetInputEnabled(true);

    if (m_pLoginButton != NULL)
    {
        m_pLoginButton->SetText(UI_LOGIN_BTN_LOGIN);
        m_pLoginButton->SetEnabled(true);
        m_pLoginButton->Invalidate();
    }

    //进度条只在登录中有意义；留一行文字（失败原因）时也要把它收起来，
    //否则界面上会有一条永远滚不动的进度条。
    if (m_pStatusProgress != NULL)
    {
        m_pStatusProgress->SetMarquee(false);
        m_pStatusProgress->SetVisible(false);
    }

    const bool bShowMessage = (pszMessage != NULL && pszMessage[0] != _T('\0'));
    if (m_pStatusText != NULL)
        m_pStatusText->SetText(bShowMessage ? pszMessage : _T(""));

    if (m_pStatusArea != NULL)
    {
        m_pStatusArea->SetVisible(bShowMessage);
        m_pStatusArea->Invalidate();
    }

    //显隐变化之后重排，理由见 RelayoutClientContent
    RelayoutClientContent();
}

void CBalloonLoginDlg::SetLoginButtonChecking(bool checking)
{
    if (m_pLoginButton == NULL)
    {
        return;
    }

    if (checking)
    {
        m_pLoginButton->SetText(_T("正在检查更新..."));
        m_pLoginButton->SetEnabled(false);
    }
    else
    {
        m_pLoginButton->SetText(_T("登 录"));
        m_pLoginButton->SetEnabled(true);
    }
    m_pLoginButton->Invalidate();
}

LRESULT CBalloonLoginDlg::OnPreLoginUpdateResult(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;

    update::UpdateTaskResult* pResult = (update::UpdateTaskResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    SetLoginButtonChecking(false);

    const bool hasUsableUpdate = pResult->m_hasUpdate && pResult->m_packageUsable;
    LOG_INFO(_T("登录前检查更新结果：transportOk=%d, code=%d, hasUpdate=%d, usable=%d, forced=%d。"),
             pResult->m_transportOk ? 1 : 0, pResult->m_code,
             pResult->m_hasUpdate ? 1 : 0, pResult->m_packageUsable ? 1 : 0,
             pResult->m_forced ? 1 : 0);

    if (hasUsableUpdate)
    {
        //拦住登录，把升级窗摆出来。用户点「稍后」关掉它时会给本窗口投一条
        //FMG_MSG_UPDATE_LOGIN_CONTINUE，那时再继续登录；强制升级关不掉，于是登不进去。
        //相对登录窗模态：显示期间把登录窗整个禁用，否则用户再点一次登录就能绕过去。
        //关闭升级窗时恢复可用并续上这次登录（第二个参数 true）。
        CBalloonUpdateWindow& w = CBalloonUpdateWindow::GetInstance();
        w.SetGatedWindow(m_hWnd, CBalloonUpdateWindow::kGatedLoginDlg);
        w.ShowResult(*pResult);
        delete pResult;
        return 0;
    }

    //已是最新 / 检查失败：照常登录。检查更新这条链路自己挂了不该把用户挡在门外。
    delete pResult;
    //不再立即关窗，理由见 OnLoginClicked 末尾的说明：窗口就地切到「登录中」，
    //由结果决定去留。原先那条「EndModal 必须紧跟 DoLogin」的约束是为了绕开
    //「失败时窗口还活着、StartLogin 又建一个」引发的 ATL 断言，现在失败不再重建窗口，
    //约束自然不成立了。
    DoLogin();
    return 0;
}

LRESULT CBalloonLoginDlg::OnUpdateLoginContinue(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    LOG_INFO(_T("用户关闭升级窗，继续被拦下的登录。"));
    //同上：不再立即关窗，理由见 OnPreLoginUpdateResult 里的说明。
    DoLogin();
    return 0;
}

void CBalloonLoginDlg::PopulateAccountCombo()
{
    if (m_pAccountCombo == NULL)
        return;

    // 历史账号填入下拉列表。
    if (m_pLoginAccountList != NULL)
    {
        int nCount = m_pLoginAccountList->GetCount();
        for (int i = 0; i < nCount; ++i)
        {
            LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->GetAccountInfo(i);
            if (lpAccount != NULL)
                m_pAccountCombo->AddString(lpAccount->szUser);
        }

        // 让浮层一次显示得下所有账号。DuiComboBox 默认只显示 8 行（m_maxVisible），
        // 超出的项虽然在列表里、却顶不进浮层的可视高度 —— 而浮层内部那个 DuiListBox
        // 收不到滚轮消息（DuiComboBoxPopup 的消息映射里没有 WM_MOUSEWHEEL，焦点又在
        // 浮层窗口自己身上），于是第 9 个往后的账号根本够不着。本机登录过十几个账号
        // 是常事，新账号又总是排在最后，正好全落在够不着的区间里。
        //
        // 上限 kAccountComboMaxRows 是防止账号特别多时浮层高得掉出屏幕 —— 真到那一天
        // 得让浮层能滚，而那要改 balloonui，须先与用户对齐。
        const int nRows = (nCount < kAccountComboMaxRows) ? nCount : kAccountComboMaxRows;
        if (nRows > 0)
        {
            m_pAccountCombo->SetMaxVisibleItems(nRows);
        }
    }

    // 预填：优先用调用方设的默认账号 / 密码，否则用最后登录账号。
    //
    // 两者都没有时<u>不填任何内容</u>，让账号框与密码框各自显示自己的占位文字。
    // 此处曾经兜底预填一组固定测试账号，方便本机首次启动就能直接点"登录"跑
    // 端到端测试；但它使"本机尚无历史账号"这一场景下账号框永远非空，占位文字
    // 因而永远显示不出来，2026-08-14 按用户要求移除。
    if (!m_strDefaultAccount.IsEmpty())
    {
        m_pAccountCombo->SetText(m_strDefaultAccount);
        if (m_pPasswordEdit != NULL)
            m_pPasswordEdit->SetText(m_strDefaultPassword);
    }
    else
    {
        LOGIN_ACCOUNT_INFO* lpLast = (m_pLoginAccountList != NULL)
                                     ? m_pLoginAccountList->GetLastLoginAccountInfo()
                                     : NULL;
        if (lpLast != NULL)
        {
            // 走过真实登录的本机 —— 用最近一次成功登录过的账号预填。
            m_pAccountCombo->SetText(lpLast->szUser);
            LoadAccountIntoUi(lpLast->szUser);
        }
    }

    // 账号下拉框的填充结果记一条 —— "历史账号没加载"这类问题只看界面分不清是
    // 文件没读到、还是读到了但预填的账号是空的，把条数与预填文本长度打出来即可
    // 一眼区分（2026-08-13 排查空账号记录时正是靠这条定位的）。
    LOG_INFO("PopulateAccountCombo: records=%d, comboItems=%d, prefilledLen=%d.",
             (m_pLoginAccountList != NULL) ? m_pLoginAccountList->GetCount() : -1,
             m_pAccountCombo->GetCount(),
             m_pAccountCombo->GetText().GetLength());

    // 账号预填完毕 —— 把账号同步给 Banner（头像首字母 + 用户名）。这里不是
    // 由通知触发的，传 kComboExtraNoItem 表示"直接读下拉框现在的文本"。
    UpdateBannerUserName(
        ResolveAccountFromCombo(balloonauth::kComboExtraNoItem));
}

void CBalloonLoginDlg::PersistCheckStateForCurrentAccount()
{
    if (m_pLoginAccountList == NULL)
        return;

    // 账号一律经 ResolveAccountFromCombo 解析（理由见 OnDuiNotify 里的说明）。
    // 这里不是由下拉框选中触发的，传 kComboExtraNoItem 表示"直接读现在的文本"。
    CString account = ResolveAccountFromCombo(balloonauth::kComboExtraNoItem);
    if (account.IsEmpty())
        return;   // 账号还没填，无从落盘

    LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->Find(account);
    if (lpAccount == NULL)
        return;   // 尚未在本机成功登录过的账号，入列等登录成功后再由 CMainDlg 做

    const BOOL bRemember =
        (m_pRememberPwd != NULL && m_pRememberPwd->IsChecked()) ? TRUE : FALSE;
    const BOOL bAuto =
        (m_pAutoLogin != NULL && m_pAutoLogin->IsChecked()) ? TRUE : FALSE;
    if (lpAccount->bRememberPwd == bRemember && lpAccount->bAutoLogin == bAuto)
        return;   // 与盘上的一致，不必白写一次

    lpAccount->bRememberPwd = bRemember;
    lpAccount->bAutoLogin   = bAuto;
    // 不记住密码就不该继续留着密码，否则文件里仍有明文，这个"取消"名不副实。
    if (!bRemember)
    {
        lpAccount->szPwd[0] = _T('\0');
    }

    const tstring strFileName =
        Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
    const BOOL bSaved = m_pLoginAccountList->SaveFile(strFileName.c_str());
    LOG_INFO(_T("登录窗勾选项就地落盘: account=%s, 记住密码=%d, 自动登录=%d, 落盘=%d."),
             (LPCTSTR)account, (int)bRemember, (int)bAuto, (int)bSaved);
}

void CBalloonLoginDlg::LoadAccountIntoUi(LPCTSTR lpszAccount)
{
    if (m_pLoginAccountList == NULL || lpszAccount == NULL)
        return;

    LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->Find(lpszAccount);
    if (lpAccount == NULL)
        return;   // 新账号 —— 保持界面当前状态不变

    if (m_pPasswordEdit != NULL)
    {
        // 记住密码的账号回填明文密码；否则清空密码框。
        if (lpAccount->bRememberPwd)
            m_pPasswordEdit->SetText(lpAccount->szPwd);
        else
            m_pPasswordEdit->SetText(_T(""));
    }
    if (m_pRememberPwd != NULL)
        m_pRememberPwd->SetCheck(lpAccount->bRememberPwd != FALSE, false);
    if (m_pAutoLogin != NULL)
        m_pAutoLogin->SetCheck(lpAccount->bAutoLogin != FALSE, false);
}

LRESULT CBalloonLoginDlg::OnRemoveAccount(UINT /*uMsg*/, WPARAM wParam,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 到这里时下拉浮层早已关闭、它的消息栈也完全展开了，弹模态确认框才是安全的。
    RemoveAccountFromHistory((int)wParam);
    return 0;
}

void CBalloonLoginDlg::RemoveAccountFromHistory(int comboIndex)
{
    if (m_pAccountCombo == NULL || m_pLoginAccountList == NULL)
    {
        return;
    }
    if (comboIndex < 0 || comboIndex >= m_pAccountCombo->GetCount())
    {
        return;
    }

    const CString account = m_pAccountCombo->GetItemText(comboIndex);
    if (account.IsEmpty())
    {
        return;
    }

    // 下拉项与历史列表按账号名对应（下拉是拿历史列表铺的），但两边的下标不一定
    // 一致，故按名字回查历史列表拿真正的下标。查不到说明两边已经不同步，
    // 那就只把下拉项摘掉、不动文件。
    int listIndex = -1;
    for (int i = 0; i < m_pLoginAccountList->GetCount(); ++i)
    {
        LOGIN_ACCOUNT_INFO* lpInfo = m_pLoginAccountList->GetAccountInfo(i);
        if (lpInfo != NULL && account.Compare(lpInfo->szUser) == 0)
        {
            listIndex = i;
            break;
        }
    }

    // 二次确认。删除叉紧挨着账号文字，误点的代价不该是"账号连同聊天记录一起没了"，
    // 所以这一步不能省。「同时删除本地聊天记录」做成确认框里的勾选项，默认不勾 ——
    // 多数人只是想让下拉列表干净点，并不想清历史。
    bool bAlsoDeleteLocalData = false;
    if (!balloonauth::ConfirmRemoveAccount(m_hWnd, account, bAlsoDeleteLocalData))
    {
        return;
    }

    // 先摘下拉项，界面立即有反馈。
    m_pAccountCombo->DeleteString(comboIndex);

    if (listIndex >= 0)
    {
        m_pLoginAccountList->Del(listIndex);
        const tstring strFileName =
            Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
        const BOOL bSaved = m_pLoginAccountList->SaveFile(strFileName.c_str());
        LOG_INFO(_T("删除历史账号: account=%s, 列表下标=%d, 落盘=%d, 剩余条数=%d."),
                 (LPCTSTR)account, listIndex, (int)bSaved, m_pLoginAccountList->GetCount());
    }
    else
    {
        LOG_INFO(_T("删除历史账号: account=%s 不在历史列表里，仅从下拉框摘除。"),
                 (LPCTSTR)account);
    }

    if (bAlsoDeleteLocalData)
    {
        const bool bWiped = balloonauth::DeleteAccountLocalData(account);
        LOG_INFO(_T("删除历史账号的本地数据: account=%s, 结果=%d."),
                 (LPCTSTR)account, (int)bWiped);
    }

    // 被删的正是当前显示的那个账号时，界面得换一个人 —— 否则账号框里还留着
    // 一个已经删掉的名字，点登录又会把它写回历史列表。
    CString current = m_pAccountCombo->GetText();
    current.Trim();
    if (current.Compare(account) != 0)
    {
        return;
    }

    CString next;
    if (m_pAccountCombo->GetCount() > 0)
    {
        next = m_pAccountCombo->GetItemText(0);
    }
    m_pAccountCombo->SetText(next);
    m_pAccountCombo->Invalidate();

    if (next.IsEmpty())
    {
        // 一个历史账号都不剩：清空密码与两个勾选项，回到"全新登录"的样子。
        if (m_pPasswordEdit != NULL)
        {
            m_pPasswordEdit->SetText(_T(""));
        }
        if (m_pRememberPwd != NULL)
        {
            m_pRememberPwd->SetCheck(false, false);
        }
        if (m_pAutoLogin != NULL)
        {
            m_pAutoLogin->SetCheck(false, false);
        }
    }
    else
    {
        LoadAccountIntoUi(next);
    }
    UpdateBannerUserName(next);
}

// =======================================================================
// 发起登录
//
// 以下 DoLogin / LoginThreadProc 自旧 CLoginDlg 原样迁移，仅把 this 指针
// 类型从 CLoginDlg* 改为 CBalloonLoginDlg*，登录协议与解析逻辑不变。
// （其中的字面量数字已就地命名为本文件顶部的常量。）
// =======================================================================

void CBalloonLoginDlg::DoLogin()
{
    //取一个新的轮次号。已经在登录中时它返回无效号，此处直接返回——重复点击
    //不该起第二个登录线程。
    const uint32_t uGeneration = m_loginProgress.startLogin();
    if (uGeneration == auth::kInvalidLoginGeneration)
    {
        LOG_INFO("DoLogin ignored: a login is already in progress.");
        return;
    }

    //线程参数用堆对象带走：轮次号必须随线程走，不能读成员——用户可能在结果回来
    //之前又发起了新的一轮，那时成员里已经是新号了。线程负责释放它。
    LoginThreadParam* pParam = new LoginThreadParam();
    pParam->m_pDlg = this;
    pParam->m_uGeneration = uGeneration;

    HANDLE hLoginThread =
        (HANDLE)::_beginthreadex(NULL, 0, LoginThreadProc, pParam, 0, NULL);
    if (hLoginThread == NULL)
    {
        //线程都起不来，本轮就此作废，界面不要停在"登录中"
        LOG_ERROR("DoLogin failed: cannot create login thread.");
        delete pParam;
        m_loginProgress.cancelLogin();
        return;
    }
    ::CloseHandle(hLoginThread);

    //窗口已创建时才切界面。自动登录路径会在窗口尚未创建时直接调本方法，
    //那时没有控件可动，只需把状态机推进即可。
    if (m_hWnd != NULL)
        EnterLoginingUi();
}

UINT CBalloonLoginDlg::LoginThreadProc(void* pParam)
{
    //参数是 DoLogin 堆分配的，无论走哪条返回路径都要释放
    LoginThreadParam* pThreadParam = (LoginThreadParam*)pParam;
    if (pThreadParam == NULL)
        return 0;

    CBalloonLoginDlg* pLoginDlg = pThreadParam->m_pDlg;
    const uint32_t uGeneration = pThreadParam->m_uGeneration;
    delete pThreadParam;

    if (pLoginDlg == NULL)
        return 0;

    char szUser[kAccountUtf8BufSize] = { 0 };
    EncodeUtil::UnicodeToUtf8(pLoginDlg->m_stAccountInfo.szUser, szUser, ARRAYSIZE(szUser));
    char szPassword[kAccountUtf8BufSize] = { 0 };
    EncodeUtil::UnicodeToUtf8(pLoginDlg->m_stAccountInfo.szPwd, szPassword, ARRAYSIZE(szPassword));

    std::string strReturnData;
    bool bRet = CIUSocket::GetInstance().Login(szUser, szPassword, kLoginClientType,
                                               pLoginDlg->m_loginPresence, kLoginTimeoutSeconds,
                                               strReturnData);
    CLoginResult* pLoginResult = new CLoginResult();
    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
    if (bRet)
    {
        // v2 登录应答：{"code":0,"msg":"ok","userid":"8","servertime":...,"profile":{...}}
        net::LoginResponse resp;
        if (net::LoginProtocolClient::parseLoginResponse(strReturnData, resp))
        {
            if (resp.code == kLoginRespCodeOk)
            {
                pLoginResult->m_LoginResultCode = LOGIN_SUCCESS;
                pLoginResult->m_uAccountID = (UINT)resp.userid;
                strcpy_s(pLoginResult->m_szAccountName, ARRAYSIZE(pLoginResult->m_szAccountName),
                         resp.profile.username.c_str());
                strcpy_s(pLoginResult->m_szNickName, ARRAYSIZE(pLoginResult->m_szNickName),
                         resp.profile.nickname.c_str());
                pLoginResult->m_nFace = resp.profile.facetype;
                pLoginResult->m_nGender = resp.profile.gender;
                pLoginResult->m_nBirthday = resp.profile.birthday;
                strcpy_s(pLoginResult->m_szSignature, ARRAYSIZE(pLoginResult->m_szSignature),
                         resp.profile.signature.c_str());
                strcpy_s(pLoginResult->m_szAddress, ARRAYSIZE(pLoginResult->m_szAddress),
                         resp.profile.address.c_str());
                strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace),
                         resp.profile.customface.c_str());
                strcpy_s(pLoginResult->m_szPhoneNumber, ARRAYSIZE(pLoginResult->m_szPhoneNumber),
                         resp.profile.phonenumber.c_str());
                strcpy_s(pLoginResult->m_szMail, ARRAYSIZE(pLoginResult->m_szMail),
                         resp.profile.mail.c_str());
            }
            else if (resp.code == kLoginRespCodeUnregistered)
                pLoginResult->m_LoginResultCode = LOGIN_UNREGISTERED;
            else if (resp.code == kLoginRespCodePasswordError)
                pLoginResult->m_LoginResultCode = LOGIN_PASSWORD_ERROR;
            else if (resp.code == kLoginRespCodeVersionTooOld)
            {
                // 版本过低：单独一个结果码，好让主窗口把用户引到升级流程去，
                // 而不是弹一句"网络故障，登录失败"让他反复重试。
                LOG_ERROR(_T("login rejected: client version too old, code=%d."), resp.code);
                pLoginResult->m_LoginResultCode = LOGIN_VERSION_TOO_OLD;
            }
            else
            {
                // 其它错误码（如 104 账号停用）统一按登录失败处理
                LOG_ERROR(_T("login failed, server returned code=%d."), resp.code);
                pLoginResult->m_LoginResultCode = LOGIN_FAILED;
            }
        }
        else
        {
            LOG_ERROR(_T("login failed, v2 response json is invalid, json=%s"), strReturnData.c_str());
            pLoginResult->m_LoginResultCode = LOGIN_FAILED;
        }
    }

    LOG_INFO("LoginThreadProc: post FMG_MSG_LOGIN_RESULT, code=%d "
             "(0=SUCCESS 1=FAILED 2=UNREG 3=PWD_ERR), userid=%u.",
             (int)pLoginResult->m_LoginResultCode,
             (unsigned int)pLoginResult->m_uAccountID);

    //把本轮的轮次号带回去：登录窗据它判断这个结果是不是自己还在等的那一个。
    //用户点了取消、或已经发起了新的一轮时，这个结果会被丢弃。
    pLoginResult->m_uLoginGeneration = uGeneration;

    //临时排查用：把代理窗句柄与投递结果打出来，确认登录结果有没有真的进到消息队列
    const HWND hProxy = pLoginDlg->m_lpFMGClient->m_UserMgr.m_hProxyWnd;
    const BOOL bPosted = ::PostMessage(hProxy, FMG_MSG_LOGIN_RESULT, 0, (LPARAM)pLoginResult);
    LOG_INFO("LoginThreadProc: proxyWnd=%p, isWindow=%d, PostMessage=%d, lastErr=%lu.",
             hProxy, (int)::IsWindow(hProxy), (int)bPosted, ::GetLastError());

    return 1;
}
