/**
 *  BalloonRegisterWindow.cpp
 *
 *  新版注册对话框的实现。两步注册流程（填写资料 / 完成）+ 客户端字段校验
 *  + 走 cmd_register(1101) 的真实注册请求。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"
#include "BalloonRegisterWindow.h"

#include <memory>
#include <process.h>                  // _beginthreadex

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_WARNING / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"            // Unicode <-> UTF-8 互转
#include "../../App/BrandText.h"             // g_strAppTitle / g_strAppSlogan（产品名称与标语）
#include "RegisterLayout.h"           // BuildRegisterBody / RegCtrlId / SetRegisterText
#include "RegisterModel.h"            // RegisterModel / RegisterStep
#include "../../net/IUSocket.h"          // CIUSocket::Register
#include "../../net/Protocol.h"          // enum error_code
#include "../../net/RegisterProtocolClient.h"   // 注册报文编解码
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"   // 公司名称组合框
#include "../../Common/I18n/I18n.h"      // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../BalloonAuth/WindowFlipTransition.h"   // 登录窗与注册窗之间的翻转过渡

using namespace balloonwjui;
using namespace balloonregister;

// ---- 文件作用域常量 -----------------------------------------------------

// 注册窗的窗口尺寸与标题栏高度（kRegWindowWidth / kRegWindowHeight / kRegWindowTitleBarH）
// 定义在 RegisterLayout.h，与布局单测共用同一份。

// 注册请求的收发超时（秒）。与登录一致（CBalloonLoginDlg 走的也是 3 秒）。
static const int kRegRequestTimeoutSec = 3;

// 完整橙色 banner 的高度（design AuthShell banner = 160）。
static const int kRegBannerHeight = 160;

// 窗体圆角半径（design AuthShell borderRadius = 12）。
static const int kRegCornerRadius = 12;

// banner 135° 渐变起点色 #FFB793。
static const BYTE kRegGradTopR = 0xFF, kRegGradTopG = 0xB7, kRegGradTopB = 0x93;
// banner 135° 渐变终点色 #FF7A45（= 主题色 accent）。
static const BYTE kRegGradBotR = 0xFF, kRegGradBotG = 0x7A, kRegGradBotB = 0x45;

// =======================================================================
// 文件内 helper
// =======================================================================

namespace {

// 生成窗体 9-grid 背景图：上 kRegBannerHeight 像素 135° 橙色渐变（与
// design banner 一致），其余白色。配合 SetBgImage 的 9-grid inset，橙色
// banner 恒定在顶部、白色区拉伸填充表单区。返回的 HBITMAP 由调用方负责
// 释放。
//
// 直接在进程内绘制（不依赖外部 PNG 资源）—— 与 CBalloonLoginDlg 的
// CreateFrameBgBitmap 同思路，零部署步骤。位图用 CreateCompatibleBitmap
// （屏幕格式、无 alpha 通道），DuiNinePatch 按不透明位图处理，避免
// CreateDIBSection 未初始化 alpha 被当作"全透明"导致整窗背景消失。
HBITMAP CreateRegisterBgBitmap()
{
    // 位图按窗口实际尺寸生成；窗口固定尺寸，9-grid 基本是 1:1 贴图。
    const int kBmpW = kRegWindowWidth;
    const int kBmpH = kRegWindowHeight;

    HDC hdcScreen = ::GetDC(NULL);
    if (hdcScreen == NULL)
    {
        return NULL;
    }

    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, kBmpW, kBmpH);
    if (hbm == NULL)
    {
        ::ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    HDC     mem = ::CreateCompatibleDC(hdcScreen);
    HGDIOBJ old = ::SelectObject(mem, hbm);

    // 先整窗铺白。
    {
        RECT rcWhite = { 0, 0, kBmpW, kBmpH };
        HBRUSH brWhite = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(mem, &rcWhite, brWhite);
        ::DeleteObject(brWhite);
    }

    // 顶部 banner：135° 橙色线性渐变（与 RegBannerPanel / design 一致）。
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::RectF bannerRc(0.0f, 0.0f,
                                (Gdiplus::REAL)kBmpW,
                                (Gdiplus::REAL)kRegBannerHeight);
        Gdiplus::LinearGradientBrush grad(
            bannerRc,
            Gdiplus::Color(255, kRegGradTopR, kRegGradTopG, kRegGradTopB),
            Gdiplus::Color(255, kRegGradBotR, kRegGradBotG, kRegGradBotB),
            135.0f);
        g.FillRectangle(&grad, bannerRc);
    }

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, hdcScreen);
    return hbm;
}

// 把一个 DuiEdit 配成注册窗输入框风格：透明融入 RegInput 的圆角底
// （底色 #F5F4F1）、不画自身边框、设占位文字、必要时密码态，并回填
// 初始文本（用于步骤来回切换时保留已输入内容）。
//   edit：       目标输入框；NULL 安全返回。
//   placeholder：占位提示文字。
//   password：   true 时为密码态（显示圆点）。
//   initialText：回填的初始文本（来自数据模型）。
void SetupRegisterEdit(DuiEdit* edit, LPCTSTR placeholder, bool password,
                       LPCTSTR initialText)
{
    if (edit == NULL)
    {
        return;
    }
    // 密码态可以在任意时刻设置，与调用顺序无关。
    if (password)
    {
        edit->SetPassword(true);
    }
    edit->SetPlaceholder(placeholder);
    // 输入框底色与 RegInput 圆角底一致（design Input 底色 #F5F4F1）。
    edit->SetBgColor(RGB(245, 244, 241));
    edit->SetShowBorder(false);
    // 回填初始文本 —— 步骤来回切换时不丢失已输入内容。
    if (initialText != NULL && initialText[0] != _T('\0'))
    {
        edit->SetText(initialText);
    }
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonRegisterWindow::CBalloonRegisterWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_hBgBitmap(NULL)
    , m_modalRunning(false)
    , m_modalResult(IDCANCEL)
    , m_quitReceived(false)
    , m_returnLogin(NULL)
    , m_submitting(false)
    , m_registered(false)
{
    // frame 外观（标题栏透明 / 按钮 / 背景图等）须在 Create 之后配置，
    // 见 ConfigureFrame —— CreateAndShow 在 Create 后调用它。
}

CBalloonRegisterWindow::~CBalloonRegisterWindow()
{
    // 调用方没有调用 ReturnToLogin（例如提前 return）时，登录窗还隐藏且禁用着：直接恢复它，
    // 不留下两个窗口都看不见的状态。
    if (m_returnLogin != NULL)
    {
        LOG_WARNING(_T("register window: destroyed without ReturnToLogin, restoring the login window directly."));
        RestoreLoginDirectly();
    }

    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。窗体背景图
    // 由本类持有，需在此释放。
    if (m_hBgBitmap != NULL)
    {
        ::DeleteObject(m_hBgBitmap);
        m_hBgBitmap = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

bool CBalloonRegisterWindow::BuildWindow(HWND hWndParent, bool show)
{
    // 创建无边框 DUI 顶层窗口。WS_THICKFRAME 须保留 —— DuiFrameWindow 靠
    // WM_NCCALCSIZE 抠掉系统 chrome，但需要它维持 Aero snap / 最大化动画。
    if (Create(hWndParent, CWindow::rcDefault, g_strAppTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("register window: Create failed."));
        return false;
    }

    ConfigureFrame();
    RebuildStepUi(m_model.CurrentStep());
    ResizeClient(kRegWindowWidth, kRegWindowHeight);
    if (!show)
    {
        // 由调用方摆好位置再切换过来（翻转过渡要先截取隐藏着的本窗口）
        return true;
    }
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBalloonRegisterWindow::CreateAndShow(HWND hMainDlg)
{
    // 调试入口（命令行 --register）：无父窗口、非模态。
    m_hMainDlg = hMainDlg;
    if (!BuildWindow(NULL, true))
        return;

    LOG_INFO(_T("register window: created and shown (step=%d)."), m_model.CurrentStep());
}

INT_PTR CBalloonRegisterWindow::RunModalFromLogin(balloonwjui::DuiFrameWindow& login)
{
    // 接入登录流程：模态于登录窗，跑独立消息循环，直至 EndModal 把 m_modalRunning 置 false。
    m_modalResult  = IDCANCEL;
    m_modalRunning = true;
    m_quitReceived = false;

    if (!BuildWindow(login.m_hWnd, false))
    {
        m_modalRunning = false;
        return IDCANCEL;
    }
    // 从登录窗翻转过来时去掉最小化按钮：登录窗此时隐藏着，注册窗没有任务栏按钮（它归登录窗所有），
    // 最小化之后屏幕上只剩桌面左下角一小条标题栏，看上去像程序消失了。只留关闭按钮，关闭即翻回登录窗。
    SetButtons(false, false, true);

    // 摆在登录窗的位置上：上边缘对齐、水平居中；注册窗比登录窗高，超出工作区时挪回工作区之内
    RECT rcLogin;
    login.GetWindowRect(&rcLogin);
    RECT rcSelf;
    GetWindowRect(&rcSelf);
    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work = rcLogin;
    if (::GetMonitorInfo(::MonitorFromWindow(login.m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        work = mi.rcWork;
    }
    const RECT target = balloonauth::AlignTopCenter(rcLogin, rcSelf.right - rcSelf.left,
                                                    rcSelf.bottom - rcSelf.top, work);
    SetWindowPos(NULL, target.left, target.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    // 模态期间登录窗禁用：即使用户从托盘把它唤出来，也不能在注册窗开着时操作它
    m_returnLogin = &login;
    login.EnableWindow(FALSE);
    const bool flipped = balloonauth::SwitchDuiWindows(login, *this, balloonauth::kFlipForward,
                                                       balloonauth::IsFlipAnimationEnabled(), NULL);
    LOG_INFO(_T("register window: opened from the login window (flip=%d, pos=%ld,%ld)."),
             (int)flipped, target.left, target.top);

    // 取到 WM_QUIT 时 GetMessage 返回 0，它会被本循环消费掉；转投给外层循环，否则进程退不出去
    // （与 CBalloonLoginDlg::DoModal 的处理相同）。
    MSG msg;
    BOOL bGot = TRUE;
    while ((bGot = ::GetMessage(&msg, NULL, 0, 0)) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (!m_modalRunning)
            break;
    }
    if (bGot == 0)
    {
        LOG_INFO("register window modal loop got WM_QUIT, re-post it for the outer loop.");
        ::PostQuitMessage((int)msg.wParam);
        m_quitReceived = true;
        m_modalResult  = IDCANCEL;
    }
    m_modalRunning = false;
    return m_modalResult;
}

void CBalloonRegisterWindow::ReturnToLogin()
{
    if (m_returnLogin == NULL)
    {
        return;
    }
    balloonwjui::DuiFrameWindow& login = *m_returnLogin;
    m_returnLogin = NULL;
    if (!login.IsWindow())
    {
        // 登录窗已经不在了（进程正在退出）：只销毁本窗口
        if (IsWindow())
            DestroyWindow();
        return;
    }

    // 先恢复登录窗可用，再切换：切换的最后一步要激活登录窗
    login.EnableWindow(TRUE);
    bool flipped = false;
    if (IsWindow())
    {
        const bool animate = !m_quitReceived && balloonauth::IsFlipAnimationEnabled();
        flipped = balloonauth::SwitchDuiWindows(*this, login, balloonauth::kFlipBackward, animate, NULL);
    }
    else
    {
        login.ShowWindow(SW_SHOW);
        ::SetForegroundWindow(login.m_hWnd);
    }
    LOG_INFO(_T("register window: returned to the login window (flip=%d)."), (int)flipped);

    // 登录窗已显示并激活，再销毁本窗口
    if (IsWindow())
        DestroyWindow();
}

void CBalloonRegisterWindow::RestoreLoginDirectly()
{
    balloonwjui::DuiFrameWindow* login = m_returnLogin;
    m_returnLogin = NULL;
    if (login != NULL && login->IsWindow())
    {
        login->EnableWindow(TRUE);
        login->ShowWindow(SW_SHOW);
        ::SetForegroundWindow(login->m_hWnd);
    }
    if (IsWindow())
        DestroyWindow();
}

void CBalloonRegisterWindow::EndModal(INT_PTR result)
{
    m_modalResult  = result;
    m_modalRunning = false;
}

// =======================================================================
// frame 外观
// =======================================================================

void CBalloonRegisterWindow::ConfigureFrame()
{
    // 标题栏：透明、无标题文字、白色 min/close glyph、无最大化按钮。
    SetTitle(_T(""));
    // SetTitle 会把系统窗口文字一并清空（见 DuiFrameWindow::SetTitle）。透明标题栏不显示文字，
    // 但任务栏与 Alt+Tab 显示的是系统窗口文字，这里单独设回产品名称；标题栏控件另存一份文字，
    // 不受影响。
    ::SetWindowText(m_hWnd, g_strAppTitle.c_str());
    SetTitleBarHeight(kRegWindowTitleBarH);
    SetTitleBarTransparent(true);
    SetCaptionGlyphColor(RGB(255, 255, 255));
    SetButtons(true, false, true);
    SetResizable(false);
    SetMinSize(kRegWindowWidth, kRegWindowHeight);

    // 窗体背景图："上橙下白" 9-grid —— 透明标题栏透出顶部橙色 banner，
    // 与客户区 RegBannerPanel 的火烈鸟 / 品牌字衔接；白色区作为表单区
    // 底色。inset.top = banner 高度，使橙色 banner 恒定不被拉伸。
    if (m_hBgBitmap == NULL)
    {
        m_hBgBitmap = CreateRegisterBgBitmap();
    }
    if (m_hBgBitmap != NULL)
    {
        // 9-grid inset：上侧保住整条 banner，其余三边留 1px 防角点拉花。
        RECT bgInsets = { 1, kRegBannerHeight, 1, kRegCornerRadius };
        SetBgImage(m_hBgBitmap, bgInsets);
    }
}

// =======================================================================
// 客户区 —— 两步重建
// =======================================================================

void CBalloonRegisterWindow::RebuildStepUi(int step)
{
    // 构建该步的客户区控件树（来自 RegisterLayout 的内嵌 XML）。
    std::unique_ptr<DuiControl> root = BuildRegisterBody(step);
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("register window: BuildRegisterBody returned null, the client area could not be built (step=%d)."),
                  step);
        return;
    }

    // banner 的产品名称、标语与「<产品名称> 服务协议」链接取 flamingo.ini 的配置。
    // 须在交给窗口排版之前调用：链接宽度按文字实际宽度重设，排版时才生效。
    if (!ApplyRegisterBrand(root.get(), g_strAppTitle.c_str(), g_strAppSlogan.c_str()))
    {
        LOG_ERROR("register window: brand banner not found in step %d layout.", step);
    }
    SetClientContent(std::move(root));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("register window: GetClientContent is still null after SetClientContent."));
        return;
    }

    // 各步的输入框：按 ctrl-id 取回、配占位文字 / 密码态、回填已输入内容。
    switch (step)
    {
    case kRegStepFillInfo:
        SetupRegisterEdit(dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kRegIdEditNick)),
            TR(L"昵称"), false, m_model.Nickname());
        SetupRegisterEdit(dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kRegIdEditPwd)),
            TR(L"设置密码"), true, m_model.Password());
        SetupRegisterEdit(dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kRegIdEditPwd2)),
            TR(L"确认密码"), true, m_model.ConfirmPassword());
        SetupRegisterEdit(dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kRegIdEditPhone)),
            TR(L"手机号"), false, m_model.Phone());
        // 公司名称是可编辑组合框（下拉里是默认公司，占位文字由布局层设好），这里只回填
        // 模型里的值：首次打开是默认公司，步骤来回切换时是用户改过的内容。
        {
            DuiComboBox* company = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kRegIdComboCompany));
            if (company != NULL)
            {
                company->SetText(m_model.Company());
            }
            else
            {
                LOG_ERROR("register window: company combo (id=%d) not found.", (int)kRegIdComboCompany);
            }
        }
        break;

    case kRegStepDone:
        // 完成页无输入框，把服务端返回的账号与公司名回填到两行结果文字上。
        // 文案在 XML 里留空，值只可能来自服务端应答。
        {
            CString strAccount(TRF(L"你的账号是 {0}", (LPCTSTR)m_resultAccount).c_str());
            if (!SetRegisterText(content, kRegIdTextAccount, strAccount))
            {
                LOG_ERROR(_T("register window: account text control not found on the done page (id=%d)."), (int)kRegIdTextAccount);
            }

            CString strCompany(TRF(L"已加入 {0}", (LPCTSTR)m_resultCompany).c_str());
            if (!SetRegisterText(content, kRegIdTextCompany, strCompany))
            {
                LOG_ERROR(_T("register window: company text control not found on the done page (id=%d)."), (int)kRegIdTextCompany);
            }
        }
        break;

    default:
        break;
    }

    LOG_INFO(_T("register window: client area rebuilt (step=%d)."), step);
}

void CBalloonRegisterWindow::PostRebuildStepUi(int step)
{
    if (!IsWindow())
    {
        return;
    }
    PostMessage(WM_REG_REBUILD_STEP, (WPARAM)step, 0);
}

LRESULT CBalloonRegisterWindow::OnRebuildStepMsg(UINT /*uMsg*/, WPARAM wParam,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 到这里时，触发切换的那次按钮点击的调用栈早已退出，旧客户区树可以安全销毁。
    RebuildStepUi((int)wParam);
    return 0;
}

// =======================================================================
// 表单 -> 模型
// =======================================================================

void CBalloonRegisterWindow::CommitFormToModel()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 只有填写页有输入框；完成页什么都不用回收。
    if (m_model.CurrentStep() != kRegStepFillInfo)
    {
        return;
    }

    DuiEdit* nick = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kRegIdEditNick));
    if (nick != NULL)
    {
        m_model.SetNickname(nick->GetText());
    }
    DuiEdit* pwd = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kRegIdEditPwd));
    if (pwd != NULL)
    {
        m_model.SetPassword(pwd->GetText());
    }
    DuiEdit* pwd2 = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kRegIdEditPwd2));
    if (pwd2 != NULL)
    {
        m_model.SetConfirmPassword(pwd2->GetText());
    }
    DuiEdit* phone = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kRegIdEditPhone));
    if (phone != NULL)
    {
        m_model.SetPhone(phone->GetText());
    }
    // 公司名称读组合框的当前文字：选了默认公司是它，手动输入的是输入内容
    DuiComboBox* company = dynamic_cast<DuiComboBox*>(
        content->FindCtrlById(kRegIdComboCompany));
    if (company != NULL)
    {
        m_model.SetCompany(company->GetText());
    }
}

// =======================================================================
// 按钮动作
// =======================================================================

// 传给注册工作线程的参数。线程函数负责 delete 它。
// 之所以不直接把 CString 传进线程：注册请求包体是 UTF-8，转换在 UI 线程做完
// 更省事；同时把窗口句柄一并带上，线程结束时按句柄投递结果，不去碰 this
// 指针（窗口有可能在请求途中被关掉）。
struct RegisterThreadParam
{
    HWND                        m_hWnd;    // 结果投递目标窗口
    net::RegisterRequestParams  m_params;  // 已转成 UTF-8 的注册参数（密码仍是明文，由网络层做哈希）
};

void CBalloonRegisterWindow::DoSubmitRegister()
{
    // 请求在途时忽略重复点击：注册是有副作用的写操作，连点两下可能建出
    // 两个账号，也可能第二次撞上"账号已存在"把用户搞糊涂。
    if (m_submitting)
    {
        LOG_WARNING(_T("register window: a request is in progress, ignoring the repeated submit."));
        return;
    }

    CommitFormToModel();

    const int vr = m_model.ValidateForm();
    if (vr != kRegValidOk)
    {
        // 客户端格式校验失败 —— 打日志并弹框提示，停留在填写页。
        LOG_WARNING(_T("register window: form validation failed (result=%d)."), vr);
        ::MessageBox(m_hWnd, RegisterModel::ValidateMessage(vr),
                     TR(L"注册"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 公司名去掉首尾空白后再上网。服务端也会去一遍，这里去是为了让日志里
    // 记录的就是真正参与匹配的那个名字。
    CString company = m_model.Company();
    company.Trim();

    std::unique_ptr<RegisterThreadParam> param(new RegisterThreadParam());
    param->m_hWnd = m_hWnd;
    param->m_params.nickname = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.Nickname()));
    param->m_params.password = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.Password()));
    param->m_params.phone    = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.Phone()));
    param->m_params.company  = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)company));

    // 注册请求是阻塞式同步调用（与登录一致），必须放到工作线程，否则界面卡死。
    unsigned int threadId = 0;
    HANDLE hThread = (HANDLE)::_beginthreadex(NULL, 0, &CBalloonRegisterWindow::RegisterThreadProc,
                                              param.get(), 0, &threadId);
    if (hThread == NULL)
    {
        LOG_ERROR(_T("register window: failed to create the worker thread."));
        ::MessageBox(m_hWnd, TR(L"注册失败，请稍后重试。"),
                     TR(L"注册"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 线程已接管参数的所有权，这里放弃 unique_ptr 的托管。
    param.release();
    ::CloseHandle(hThread);

    m_submitting = true;
    // 账号此刻还不存在（服务端建号时生成），用手机号与公司名定位这次请求。
    LOG_INFO(_T("register window: request submitted (phone=%s, company=%s)."),
             (LPCTSTR)m_model.Phone(), (LPCTSTR)company);
}

unsigned int __stdcall CBalloonRegisterWindow::RegisterThreadProc(void* pParam)
{
    std::unique_ptr<RegisterThreadParam> param(static_cast<RegisterThreadParam*>(pParam));
    if (!param)
    {
        return 0;
    }

    // 结果对象交给窗口消息处理函数释放，故这里用裸指针 new 出来。
    RegisterResult* result = new RegisterResult();

    std::string respBody;
    result->m_transportOk = CIUSocket::GetInstance().Register(
        param->m_params, kRegRequestTimeoutSec, respBody);

    if (result->m_transportOk)
    {
        net::RegisterResponse resp;
        if (net::RegisterProtocolClient::parseRegisterResponse(respBody, resp))
        {
            result->m_code   = resp.code;
            result->m_userid = resp.userid;
            result->m_username =
                EncodeUtil::Utf8ToUnicode(resp.username).c_str();
            result->m_enterpriseName =
                EncodeUtil::Utf8ToUnicode(resp.enterprisename).c_str();
        }
        else
        {
            // 收到了帧但解析不出 code：当作服务端异常处理，m_code 保持 -1，
            // 界面走兜底文案。
            result->m_transportOk = false;
            LOG_ERROR("Register: parse register response failed, body: %s", respBody.c_str());
        }
    }

    // 窗口可能已被关闭（用户等不及点了叉），此时直接释放结果、不投消息。
    if (::IsWindow(param->m_hWnd))
    {
        ::PostMessage(param->m_hWnd, WM_REG_REGISTER_RESULT, 0, (LPARAM)result);
    }
    else
    {
        delete result;
    }

    return 1;
}

LRESULT CBalloonRegisterWindow::OnRegisterResultMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                    LPARAM lParam, BOOL& /*bHandled*/)
{
    std::unique_ptr<RegisterResult> result(reinterpret_cast<RegisterResult*>(lParam));
    m_submitting = false;

    if (!result)
    {
        return 0;
    }

    if (result->m_transportOk && result->m_code == error_code_ok)
    {
        OnRegisterSucceeded(*result);
    }
    else
    {
        OnRegisterFailed(*result);
    }
    return 0;
}

void CBalloonRegisterWindow::OnRegisterSucceeded(const RegisterResult& result)
{
    // 完成页显示的账号与公司名一律取服务端应答，不用本地输入的那份 ——
    // 这样公司名大小写、空格上的差异能当场被用户看见。
    m_resultAccount = result.m_username;
    m_resultCompany = result.m_enterpriseName;
    // 密码服务端不会回传（也不该回传），只能从本地模型里取用户刚设的那份明文。
    // 取值时机放在这里是安全的：CommitFormToModel 在非填写页会直接返回
    // （见本文件 kRegStepFillInfo 的判断），GoNextStep 也只改步骤号，故进入
    // 完成页后 m_model.Password() 仍是用户填的密码，不会被空串覆盖。
    m_resultPassword = m_model.Password();
    // 记下"已注册成功"，关窗时据此让 RunModalFromLogin 返回 IDOK，登录窗才知道要把
    // 账号与密码回填进登录框。用户在完成页直接点叉关窗也算数——账号已经发下来了。
    m_registered = true;

    // 日志只记账号与公司名，密码是敏感信息，任何情况下都不进日志。
    LOG_INFO(_T("register window: registration succeeded (userid=%I64d, account=%s, company=%s)."),
             result.m_userid, (LPCTSTR)m_resultAccount, (LPCTSTR)m_resultCompany);

    m_model.GoNextStep();
    // 重建须延后到当前调用栈退出之后（本函数是在 WM_REG_REGISTER_RESULT 的
    // 处理里跑的，虽然不在按钮调用栈上，但沿用同一条路径更省心）。
    PostRebuildStepUi(m_model.CurrentStep());
}

void CBalloonRegisterWindow::OnRegisterFailed(const RegisterResult& result)
{
    if (!result.m_transportOk)
    {
        LOG_ERROR(_T("register window: registration failed, no complete request/response with the server."));
        ::MessageBox(m_hWnd, TR(L"网络异常，注册失败，请检查网络后重试。"),
                     TR(L"注册"), MB_OK | MB_ICONWARNING);
        return;
    }

    LOG_WARNING(_T("register window: registration refused by the server (code=%d)."), result.m_code);
    ::MessageBox(m_hWnd, RegisterModel::ServerErrorMessage(result.m_code),
                 TR(L"注册"), MB_OK | MB_ICONINFORMATION);
}

void CBalloonRegisterWindow::DoCloseWindow()
{
    LOG_INFO(_T("register window: close requested."));
    if (IsWindow())
    {
        // 走标准关闭路径，统一由 OnCloseMsg 收尾。
        PostMessage(WM_CLOSE, 0, 0);
    }
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonRegisterWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 模态接入登录流程时：仅结束模态循环，窗口由 ReturnToLogin 翻回登录窗之后统一销毁，
    // 控制权回到登录窗。注册成功过就返回 IDOK，登录窗据此回填账号。
    if (m_modalRunning)
    {
        EndModal(m_registered ? IDOK : IDCANCEL);
        return 0;
    }

    // 调试入口（--register）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 请求进程退出。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonRegisterWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 各步按钮的点击。
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kRegIdBackBtn:
            // 返回按钮 —— 关闭注册窗回到登录。
            DoCloseWindow();
            return 0;
        case kRegIdBtnSubmit:
            DoSubmitRegister();
            return 0;
        case kRegIdLinkAgreement:
        case kRegIdLinkPrivacy:
            // 服务协议 / 隐私政策页面尚未接入，给一句占位提示。文案与"关于"
            // 对话框里同名两项保持一致（见 BalloonMain/BalloonAboutDlg.cpp）。
            // 日后拿到正式地址后，可直接给这两个 DuiLabel 设 URL 并打开
            // SetAutoNavigate(true)，由控件自己 ShellExecute，无需改这里。
            LOG_INFO(_T("register window: agreement link clicked (id=%d), page not available yet."), (int)pNotify->ctrlId);
            ::MessageBox(m_hWnd, TR(L"功能开发中，敬请期待。"),
                         TR(L"注册"), MB_OK | MB_ICONINFORMATION);
            return 0;
        case kRegIdBtnLogin:
            // "立即登录" —— 注册完成，关闭注册窗。
            DoCloseWindow();
            return 0;
        default:
            break;
        }
    }

    // 其它通知（标题栏 min/close 按钮等）交回 DuiFrameWindow 链。
    bHandled = FALSE;
    return 0;
}
