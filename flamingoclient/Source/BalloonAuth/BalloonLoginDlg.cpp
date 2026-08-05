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
#include "../Updater.h"              // Updater（检测新版本）
#include "../UpdateDlg.h"            // CUpdateDlg（自动升级对话框）
#include "../Startup.h"              // CheckOnlyOneInstance
#include "../Path.h"                 // Hootina::CPath::GetAppPath（历史账号列表落盘路径）

#include "BalloonLoginLayout.h"      // BuildLoginBody / ValidateLoginInput / CtrlId
#include "AuthBanner.h"              // AuthBanner（顶部 Banner，回填用户名 + 可点击状态胶囊）
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/List/DuiMenu.h"     // 在线状态选择菜单
#include "../BalloonMain/MainPanelStatusMenu.h"  // GetPresenceVisual（状态色/文字，不含 Protocol.h，与 Msg.h 不冲突）
#include "../BalloonMain/MainPanelMenuIcons.h"   // AppendStatusMenuItems（状态菜单装配，与主面板共用）
#include "../BalloonMain/MainPanelIcons.h"       // kIconOnline（实心圆点图标）
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../ImageEx.h"             // CImageEx（菜单图标位图）
#include <vector>

// ---- 文件作用域常量 -----------------------------------------------------

// 登录窗固定客户区尺寸（逻辑像素）—— 对应设计稿 AuthShell 的 380×520。
static const int kLoginWindowWidth  = 380;
static const int kLoginWindowHeight = 520;

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

// 登录线程读取登录返回 JSON 时，业务成功对应的 code 值。
static const int kLoginRespCodeOk = 0;
// 服务器返回"用户未注册"的 code 值。
static const int kLoginRespCodeUnregistered = 102;
// 服务器返回"密码错误"的 code 值。
static const int kLoginRespCodePasswordError = 103;

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
    , m_pRememberPwd(NULL)
    , m_pAutoLogin(NULL)
    , m_pBanner(NULL)
    , m_loginPresence(kMyPresenceOnline)   // 默认在线；由头像下状态胶囊菜单覆盖
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
    m_pRememberPwd = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonauth::kIdRememberPwd));
    m_pAutoLogin = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonauth::kIdAutoLogin));
    m_pBanner = dynamic_cast<balloonauth::AuthBanner*>(
        content->FindCtrlById(balloonauth::kIdLoginBanner));

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

    LOG_INFO(_T("登录窗客户区 UI 初始化完成 (combo=%p, pwd=%p)."),
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
            return 0;
        }
        if (pNotify->ctrlId == balloonauth::kIdAutoLogin)
        {
            if (m_pAutoLogin != NULL && m_pAutoLogin->IsChecked()
                && m_pRememberPwd != NULL)
            {
                m_pRememberPwd->SetCheck(true, false);
            }
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
    else if (pNotify->code == balloonauth::AuthBanner::DUIN_BANNER_STATUS_CLICK)
    {
        // 点击头像下方"在线状态胶囊" —— 弹出在线状态选择菜单（在线 / 离开 / 忙碌）。
        if (pNotify->ctrlId == balloonauth::kIdLoginBanner)
        {
            ShowLoginStatusMenu();
            return 0;
        }
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
    // 调试版不检测自动升级，便于调试（与旧 CLoginDlg 一致）。
#ifndef _DEBUG
    DetectNewVersion();
#endif

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

    DoLogin();
    EndModal(IDOK);
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

    // 注册成功：登录账号是服务端自动生成的，用户没有手输过，这里替他填进
    // 账号框，省得他再照着完成页一个字符一个字符敲。
    const CString& account = regDlg.RegisteredAccount();
    if (account.IsEmpty())
    {
        // 理论上不会发生：DoModal 返回 IDOK 就意味着服务端已下发账号。
        LOG_ERROR(_T("注册返回成功但账号为空，无法回填登录框。"));
        return;
    }

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

    // DuiComboBox::SetText 是明确压制通知的（m_suppressEditNotify + SetCurSel
    // 的 notify=false），不会触发 OnDuiNotify 里那条"账号变了"的联动，所以
    // Banner 上的头像首字母与用户名得在这里手工同步一次 —— 否则账号框已经是
    // 新账号，Banner 却还停在上一个账号。PopulateAccountCombo 里 SetText 之后
    // 同样补了这一步。
    UpdateBannerUserName(account);

    // 账号已就位，焦点直接落到密码框，用户接着输密码就能登录。
    if (m_pPasswordEdit != NULL)
    {
        m_pPasswordEdit->SetFocus();
    }

    LOG_INFO(_T("注册成功，已把账号 %s 回填到登录框。"), (LPCTSTR)account);
}

void CBalloonLoginDlg::OnNetworkClicked()
{
    // 打开新版网络设置窗口（balloonui），模态于登录窗。
    // “保存”把三组服务器配置写回 flamingo.ini，下次连接 / 启动生效。
    CBalloonNetSettingsWindow netDlg(m_lpFMGClient);
    netDlg.DoModal(m_hWnd);
}

void CBalloonLoginDlg::DetectNewVersion()
{
    if (m_lpFMGClient == NULL)
        return;

    Updater updater(&(m_lpFMGClient->m_FileTask));
    if (CheckOnlyOneInstance() && updater.IsNeedUpdate())
    {
        if (IDYES == ::MessageBox(m_hWnd, _T("检测到新版本，是否现在就升级？"),
                                  _T("新版本升级提示"),
                                  MB_YESNO | MB_ICONQUESTION | MB_TOPMOST))
        {
            ShowWindow(SW_HIDE);
            CUpdateDlg updateDlg;
            updateDlg.m_pFMGClient = m_lpFMGClient;
            updateDlg.m_aryFileInfo = updater.m_aryUpdateFileList;
            if (updateDlg.DoModal(m_hWnd, NULL) == IDCANCEL)
            {
                ShowWindow(SW_SHOW);
                return;
            }
        }
    }
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

    // 预填：优先用调用方设的默认账号 / 密码，否则用最后登录账号，再否则
    // 兜底预填固定测试账号（v2 测试用：本机首次启动 / 无任何历史登录时，
    // 让登录窗立即可点"登录"完成端到端测试，无需手工敲账号 / 密码）。
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
        else
        {
            // 本机尚无任何历史登录 —— 兜底预填固定测试账号
            // （见 flamingoserver/migrations/测试账号.md）。
            // 一旦用户走过一次真实登录、留下了历史账号，本分支不再被命中。
            m_pAccountCombo->SetText(_T("test99990"));
            if (m_pPasswordEdit != NULL)
                m_pPasswordEdit->SetText(_T("test1234"));
        }
    }

    // 账号预填完毕 —— 把账号同步给 Banner（头像首字母 + 用户名）。这里不是
    // 由通知触发的，传 kComboExtraNoItem 表示"直接读下拉框现在的文本"。
    UpdateBannerUserName(
        ResolveAccountFromCombo(balloonauth::kComboExtraNoItem));
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

// =======================================================================
// 发起登录
//
// 以下 DoLogin / LoginThreadProc 自旧 CLoginDlg 原样迁移，仅把 this 指针
// 类型从 CLoginDlg* 改为 CBalloonLoginDlg*，登录协议与解析逻辑不变。
// （其中的字面量数字已就地命名为本文件顶部的常量。）
// =======================================================================

void CBalloonLoginDlg::DoLogin()
{
    HANDLE hLoginThread =
        (HANDLE)::_beginthreadex(NULL, 0, LoginThreadProc, this, 0, NULL);
    if (hLoginThread != NULL)
        ::CloseHandle(hLoginThread);
}

UINT CBalloonLoginDlg::LoginThreadProc(void* pParam)
{
    CBalloonLoginDlg* pLoginDlg = (CBalloonLoginDlg*)pParam;
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
            else
            {
                // 其它错误码（如 4 版本过低 / 104 账号停用）本期统一按登录失败处理
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

    //临时排查用：把代理窗句柄与投递结果打出来，确认登录结果有没有真的进到消息队列
    const HWND hProxy = pLoginDlg->m_lpFMGClient->m_UserMgr.m_hProxyWnd;
    const BOOL bPosted = ::PostMessage(hProxy, FMG_MSG_LOGIN_RESULT, 0, (LPARAM)pLoginResult);
    LOG_INFO("LoginThreadProc: proxyWnd=%p, isWindow=%d, PostMessage=%d, lastErr=%lu.",
             hProxy, (int)::IsWindow(hProxy), (int)bPosted, ::GetLastError());

    return 1;
}
