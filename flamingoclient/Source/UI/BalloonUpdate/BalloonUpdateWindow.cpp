/**
 *  升级窗实现，BalloonUpdateWindow.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"
#include "BalloonUpdateWindow.h"

#include <string>
#include <vector>

#include "../../ClientVersion.h"                  // FLAMINGO_CLIENT_VERSION_W
#include "../../Common/Util/EncodeUtil.h"                     // Utf8ToUnicode（更新说明是 UTF-8）
#include "../../Common/Log/IULog.h"
#include "../../resource.h"                     // IDR_MAINFRAME（标题栏图标）
#include "../../Session/UserSessionData.h"                 // g_szHomePath（更新器与主程序同目录）
#include "../../Feature/Update/UpdateManager.h"
#include "../BalloonChatWnd/FileBubbleFormat.h" // FormatFileSize（复用，不另写一份）
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

// ---- 窗口固定尺寸（逻辑像素）-------------------------------------------
static const int kDlgWidth      = 420;   // 客户区宽
static const int kDlgHeight     = 400;   // 客户区高（按下面各段高度累加得出）
static const int kDlgMinWidth   = 400;
static const int kDlgMinHeight  = 380;
static const int kDlgTitleBarH  = 36;

// ---- 各段尺寸 -----------------------------------------------------------
static const int kRootPadding   = 24;    // 根容器四周内边距
static const int kTitleH        = 32;    // 标题行高
static const int kTitleGap      = 8;     // 标题与版本行之间的间距
static const int kInfoLineH     = 22;    // 版本行 / 包大小行 行高
static const int kNoteTopGap    = 12;    // 包大小与「更新说明」小标题之间的间距
//「更新说明」小标题行高。比文字本身高出一截，文字顶部对齐，多出来的部分就是它与
//下面正文框之间的留白 —— 这样不必单独放一个间隔控件，也就不用让那个控件跟着
//说明区一起显隐。
static const int kNoteCaptionH  = 28;
static const int kNoteH         = 120;   // 更新说明正文高度（可滚动，不够就滚）
static const int kProgressTopGap= 12;    // 更新说明与进度条之间的间距
//进度条高。取 20 而不是更细的 14：进度条内部要画「53%」这样的百分比文字，
//条比字还矮的话文字会从上下两边溢出条外，看着像画糊了。
static const int kProgressH     = 20;
//进度文字行高。文字在行内垂直居中，行高比文字高出来的部分自然成为它与上方进度条
//之间的留白 —— 与「更新说明」小标题同一手法，不必单独放间隔控件。
static const int kProgressTextH = 26;
static const int kButtonRowH    = 36;    // 按钮行高
static const int kButtonH       = 32;    // 按钮高
static const int kBtnLaterW     = 88;    // 「稍后」按钮宽
static const int kBtnUpdateW    = 110;   // 「立即更新」按钮宽
static const int kButtonGap     = 10;    // 两个按钮之间的间距

// ---- 假进度调试入口（--updatewindow --progress）--------------------------
// 定时器 id。取 1 即可：本窗口目前只有这一个定时器。
static const UINT kMockProgressTimerId = 1;
// 每跳间隔（毫秒）与总跳数。60 跳 × 120ms 约 7 秒走完，够看清进度条的行进过程。
static const UINT kMockProgressIntervalMs = 120;
static const int  kMockProgressSteps      = 60;

// ---- 按钮命令 id --------------------------------------------------------
// 须 >= 10：DuiFrameWindow 标题栏保留了 1/2/3（最小化 / 最大化 / 关闭），
// 取小于 10 的 id 会导致点击触发窗口行为而不是走到 OnDuiNotify。
static const UINT kIdUpdateNow  = 10;    // 「立即更新」
static const UINT kIdLater      = 11;    // 「稍后」

// ---- 配色 ---------------------------------------------------------------
static const COLORREF kDialogBg      = RGB(0xFF, 0xFF, 0xFF);   // 白底
static const COLORREF kTitleColor    = RGB(0x22, 0x22, 0x22);   // 标题深色
static const COLORREF kGrayTextColor = RGB(0x8C, 0x8C, 0x8C);   // 次要信息灰
static const COLORREF kNoteTextColor = RGB(0x33, 0x33, 0x33);   // 更新说明正文
static const COLORREF kNoteBgColor   = RGB(0xFA, 0xFA, 0xFA);   // 更新说明底色

// 标题栏文字。用户指定的文案，不要随手改成别的说法。
// 窗口标题为「<产品名称>升级提醒」。产品名称在启动时才从 flamingo.ini 读出，不能做成
// 文件级常量，故只定义整句模板（{0} 为产品名称，只做标记），建窗时再取译文并代入 g_strAppTitle。
static const wchar_t* const kWindowTitleTemplate = TR_NOOP(L"{0}升级提醒");

// 标题栏左侧小图标边长（逻辑像素），与关于框取同一尺寸。
static const int kTitleIconSize = 16;

// 标题栏底色。图标带透明区，须先填这个底色再把图标合成上去，否则透明处是黑的。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);

// ---- 字号（磅值）-------------------------------------------------------
static const int kTitlePt       = 14;    // 标题（粗体）
static const int kCaptionPt     = 10;    // 「更新说明」小标题


//把图标合成到指定底色上，得到一张不透明位图。DuiFrameWindow::SetIcon 只认位图，
//而 app 图标带透明区，直接转过去透明处会是黑的，故先填底色再 DrawIconEx 合成。
//与关于框（BalloonAboutDlg.cpp）里的同名函数做法一致；两处各留一份是因为它们
//都只有十几行、且分属不同模块，抽公共头文件反而绕。
static HBITMAP MakeIconBitmapOnColor(HICON hIcon, int sizePx, COLORREF bgColor)
{
    if (hIcon == NULL || sizePx <= 0)
    {
        return NULL;
    }

    HDC     screen = ::GetDC(NULL);
    HDC     mem    = ::CreateCompatibleDC(screen);
    HBITMAP hbmp   = ::CreateCompatibleBitmap(screen, sizePx, sizePx);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || hbmp == NULL)
    {
        if (mem != NULL)
        {
            ::DeleteDC(mem);
        }
        if (hbmp != NULL)
        {
            ::DeleteObject(hbmp);
        }
        return NULL;
    }

    HGDIOBJ old = ::SelectObject(mem, hbmp);

    RECT   rc  = { 0, 0, sizePx, sizePx };
    HBRUSH hbr = ::CreateSolidBrush(bgColor);
    ::FillRect(mem, &rc, hbr);
    ::DeleteObject(hbr);

    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

// =======================================================================
// 单实例 / 构造 / 析构
// =======================================================================

CBalloonUpdateWindow& CBalloonUpdateWindow::GetInstance()
{
    static CBalloonUpdateWindow s_instance;
    return s_instance;
}

CBalloonUpdateWindow::CBalloonUpdateWindow()
    : m_built(false)
    , m_blockClose(false)
    , m_packageSize(0)
    , m_mockReceived(0)
    , m_mockTotal(0)
    , m_readyToInstall(false)
    , m_installAfterDownload(false)
    , m_hostWnd(NULL)
    , m_gatedWnd(NULL)
    , m_gatedKind(kGatedNone)
    , m_titleIconBmp(NULL)
    , m_labTitle(NULL)
    , m_labVersion(NULL)
    , m_labSize(NULL)
    , m_editNote(NULL)
    , m_progress(NULL)
    , m_labProgress(NULL)
    , m_btnUpdateNow(NULL)
    , m_btnLater(NULL)
{
}

CBalloonUpdateWindow::~CBalloonUpdateWindow()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }

    //本类是进程内静态单实例，析构发生在退出阶段。此时若还挂着投递目标，
    //工作线程可能正往一个即将消失的窗口投消息，故这里再摘一次。
    update::UpdateManager::GetInstance().SetNotifyTarget(NULL);
}

// =======================================================================
// 打开 / 刷新
// =======================================================================

void CBalloonUpdateWindow::SetHostWindow(HWND hWnd)
{
    m_hostWnd = hWnd;
}

void CBalloonUpdateWindow::SetGatedWindow(HWND hWnd, GatedWindowKind kind)
{
    m_gatedWnd = hWnd;
    m_gatedKind = (hWnd != NULL) ? kind : kGatedNone;

    //立刻禁用：调用方紧接着就会 ShowResult / ShowChecking 把本窗口摆出来。
    if (m_gatedWnd != NULL && ::IsWindow(m_gatedWnd))
    {
        ::EnableWindow(m_gatedWnd, FALSE);
    }
}

void CBalloonUpdateWindow::ExitApplication()
{
    //先把两个判断依据取下来：下面 ReleaseGatedWindow 会把它们清掉。
    const GatedWindowKind gatedKind = m_gatedKind;
    const HWND            gatedWnd  = m_gatedWnd;

    //被挡的窗口恢复可用：它马上要收到退出请求，禁用状态下有些收尾动作走不通。
    //这里先把类别降级成"普通模态窗"，免得 ReleaseGatedWindow 请它继续登录 ——
    //用户选的是退出，不是稍后。
    m_gatedKind = kGatedModalDlg;
    ReleaseGatedWindow();

    RequestApplicationExit(gatedKind, gatedWnd);
}

void CBalloonUpdateWindow::RequestApplicationExit(GatedWindowKind gatedKind, HWND gatedWnd)
{
    //先同步销毁本窗口：无论走哪条路，它都不该再留在屏幕上；也避免程序退出时
    //这个静态单实例仍持有有效 HWND 而触发 ATL 的析构断言。
    //m_blockClose 必须先撤，否则 OnCloseMsg 会把这次关闭拦掉。
    m_blockClose = false;
    if (IsWindow())
    {
        SendMessage(WM_CLOSE, 0, 0);
    }

    if (gatedKind == kGatedLoginDlg && gatedWnd != NULL && ::IsWindow(gatedWnd))
    {
        //登录前：交给登录窗自己关。它退出模态循环后，会话控制器的 StartLogin 会统一
        //收尾一次。详见本函数在头文件里的说明。
        LOG_INFO(_T("Update window: exit before login, the login dialog finishes the shutdown."));
        ::PostMessage(gatedWnd, WM_CLOSE, 0, 0);
        return;
    }

    //登录后从关于框一类的模态对话框里拉起本窗口：先请那个对话框关闭。它的模态循环
    //还在运行，主窗口若在它下面先被销毁，模态循环返回时对着已销毁的窗口收尾会出问题。
    //这条消息只是排进消息队列，主窗口的退出请求排在它后面，顺序上是安全的。
    if (gatedKind == kGatedModalDlg && gatedWnd != NULL && ::IsWindow(gatedWnd))
    {
        LOG_INFO(_T("Update window: ask the blocked modal dialog to close before exiting."));
        ::PostMessage(gatedWnd, WM_CLOSE, 0, 0);
    }

    //登录后：请主窗口直接退出，不弹退出提示框
    if (m_hostWnd != NULL && ::IsWindow(m_hostWnd))
    {
        LOG_INFO(_T("Update window: exit after login, the main window finishes the shutdown."));
        ::PostMessage(m_hostWnd, FMG_MSG_EXIT_FOR_UPDATE, 0, 0);
        return;
    }

    LOG_ERROR(_T("Update window: neither a login dialog nor a main window to ask, cannot exit the program."));
}

void CBalloonUpdateWindow::ReleaseGatedWindow()
{
    if (m_gatedWnd == NULL)
    {
        return;
    }

    if (::IsWindow(m_gatedWnd))
    {
        //先恢复可用再谈别的：哪怕下面投消息失败，用户也不至于对着一个点不动的窗口发呆
        ::EnableWindow(m_gatedWnd, TRUE);
        if (m_gatedKind == kGatedLoginDlg)
        {
            ::PostMessage(m_gatedWnd, FMG_MSG_UPDATE_LOGIN_CONTINUE, 0, 0);
        }
    }

    //只处理一次
    m_gatedWnd           = NULL;
    m_gatedKind = kGatedNone;
}

bool CBalloonUpdateWindow::IsVersionPostponed(const std::string& version) const
{
    if (version.empty() || m_postponedVersion.empty())
    {
        return false;
    }
    return m_postponedVersion == version;
}

bool CBalloonUpdateWindow::IsShowing() const
{
    //IsWindow / IsWindowVisible 都是 CWindow 的方法，需要 const_cast：
    //它们只读窗口状态，不改本对象。
    CBalloonUpdateWindow* self = const_cast<CBalloonUpdateWindow*>(this);
    return self->IsWindow() && self->IsWindowVisible();
}

bool CBalloonUpdateWindow::EnsureWindowCreated()
{
    if (IsWindow())
    {
        return true;
    }

    // owner 传 NULL，与关于框 / 改密框相同。原先这样做是为了让控件通知发回本窗口；
    // 2026-10-14 起有无所有者，控件通知都发回本窗口（开发约定文档 UI 约定第 11 条）。
    const std::wstring windowTitle = TRF(kWindowTitleTemplate, g_strAppTitle);
    if (Create(NULL, CWindow::rcDefault, windowTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("Update window: Create failed."));
        return false;
    }
    SetTitleBarHeight(kDlgTitleBarH);
    SetMinSize(kDlgMinWidth, kDlgMinHeight);
    SetTitle(windowTitle.c_str());

    //标题栏小图标：app 图标 16x16 合成到标题栏底色上。只建一次，跨"关窗再打开"复用。
    if (m_titleIconBmp == NULL)
    {
        HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                       kTitleIconSize, kTitleIconSize);
        if (hIcon != NULL)
        {
            m_titleIconBmp = MakeIconBitmapOnColor(hIcon, kTitleIconSize, kTitleBarBgColor);
            ::DestroyIcon(hIcon);
        }
        else
        {
            LOG_ERROR(_T("Update window: failed to load the app icon, the title bar will have no icon."));
        }
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);

    m_built = false;
    BuildUiOnce();
    //kDlgHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
    ResizeClient(kDlgWidth, kDlgHeight + kDlgTitleBarH);
    CenterWindow();

    //窗口创建完成后才登记投递目标：登记过早会导致工作线程向尚未创建的 HWND 投递消息。
    update::UpdateManager::GetInstance().SetNotifyTarget(m_hWnd);
    return true;
}

void CBalloonUpdateWindow::ShowChecking()
{
    if (!EnsureWindowCreated())
    {
        return;
    }

    //先呈现过渡态再投递任务：反过来的话，若应答很快回来，结果可能先于过渡态被应用，
    //随后又被过渡态覆盖，界面会停在"正在检查更新"再也不动。
    ApplyCheckingUi();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //false = 只检查、不下载。用户主动点检查时不该在他不知情的情况下下载几十兆的包，
    //下载等他看到版本信息后点「立即更新」。
    update::UpdateManager::GetInstance().CheckAndDownload(false);
    LOG_INFO(_T("Update window: check-update started (check only, no download)."));
}

void CBalloonUpdateWindow::ShowResult(const update::UpdateTaskResult& r)
{
    if (!EnsureWindowCreated())
    {
        return;
    }

    m_shownVersion = r.m_latestVersion;

    //记下包大小供进度文字用。无可用包时为 0，进度文字会退化成只显示已收字节。
    m_packageSize = r.m_packageUsable ? static_cast<long long>(r.m_size) : 0;

    //版本行、按钮与更新说明都在 ApplyViewState 里按视图状态填写
    const update::UpdateViewState st =
        update::BuildUpdateViewState(r, FLAMINGO_CLIENT_VERSION_W);
    ApplyViewState(st);

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    LOG_INFO(_T("Update window shown: kind=%d, forced=%d."),
             (int)st.kind, st.closable ? 0 : 1);

    //强制升级的自动流程放在最后：它可能一路走到"拉起更新器并请程序退出"，
    //那之后本窗口已被销毁、控件指针全部置空，不能再有后续的界面操作。
    AutoRunForcedUpdate(r);
}

// =======================================================================
// 客户区骨架
// =======================================================================

void CBalloonUpdateWindow::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kDialogBg);
    root->SetPadding(kRootPadding);
    root->SetGap(0);

    // ---- 标题 ----
    {
        // 按磅值设字号，绘制时按窗口 DPI 取字体。
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetTextColor(kTitleColor);
        lab->SetTextPointSize(kTitlePt, true);
        lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_labTitle = lab.get();
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kTitleH));
    }

    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kTitleGap));
    }

    // ---- 版本行 ----
    {
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetTextColor(kGrayTextColor);
        lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_labVersion = lab.get();
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kInfoLineH));
    }

    // ---- 升级包大小 ----
    {
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetTextColor(kGrayTextColor);
        lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_labSize = lab.get();
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kInfoLineH));
    }

    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kNoteTopGap));
    }

    // ---- 「更新说明」小标题 ----
    {
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetText(TR(L"更新说明"));
        lab->SetTextColor(kGrayTextColor);
        lab->SetTextPointSize(kCaptionPt, false);
        //顶部对齐：行高比文字高，空出来的部分落在文字下方，与正文框拉开距离
        lab->SetTextAlign(DT_LEFT | DT_TOP | DT_SINGLELINE);
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kNoteCaptionH));
    }

    // ---- 更新说明正文（只读多行，自带滚动）----
    // 用富文本控件而不是 DuiLabel：说明可能很长，需要能滚动，而 balloonui
    // 里没有独立的滚动容器控件，富文本控件本身就带滚动，只读化后正好合用。
    {
        std::unique_ptr<balloonwjui::DuiRichEdit> re(new balloonwjui::DuiRichEdit());
        re->SetMultiLine(true);
        re->SetWordWrap(true);
        re->SetReadOnly(true);
        re->SetBackgroundColor(kNoteBgColor);
        re->SetTextColor(kNoteTextColor);
        re->SetMargins(8, 6, 8, 6);
        //只读展示，不需要右键菜单里的剪切 / 粘贴
        re->SetContextMenuEnabled(false);
        //不显示光标。只读的展示区带着一个闪烁的光标，会让人以为可以编辑。
        //注意**不能**改用 SetFocusable(false) 达到同样效果：那样文字会变得
        //选不中（排版引擎在拖选过程中需要控件持有焦点），而这里恰恰希望用户
        //能把版本说明选中复制出来。
        re->SetShowCaret(false);
        //高度由外层布局固定为 kNoteH，内容多了在框内滚动，不要随内容长高 ——
        //布局随内容变化会把下方的进度条和按钮行顶来顶去。
        re->SetAutoGrow(false);
        m_editNote = re.get();
        root->AddChild(std::move(re), balloonwjui::DuiLayout::Hint().Fixed(kNoteH));
    }

    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kProgressTopGap));
    }

    // ---- 下载进度条（下载开始前隐藏）----
    {
        std::unique_ptr<balloonwjui::DuiProgressBar> bar(new balloonwjui::DuiProgressBar());
        bar->SetRange(0, 100);
        bar->SetPos(0, false);
        bar->SetShowPercent(true);
        bar->SetVisible(false);
        m_progress = bar.get();
        root->AddChild(std::move(bar), balloonwjui::DuiLayout::Hint().Fixed(kProgressH));
    }

    // ---- 进度文字（下载开始前隐藏）----
    {
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetTextColor(kGrayTextColor);
        lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        lab->SetVisible(false);
        m_labProgress = lab.get();
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kProgressTextH));
    }

    // ---- 弹性占位：把按钮行推到客户区底部 ----
    {
        std::unique_ptr<balloonwjui::DuiControl> spring(new balloonwjui::DuiControl());
        root->AddChild(std::move(spring), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    // ---- 按钮行（右对齐）----
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kButtonGap);

        //左侧弹性占位把两个按钮推到右边
        std::unique_ptr<balloonwjui::DuiControl> spring(new balloonwjui::DuiControl());
        row->AddChild(std::move(spring), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> later(new balloonwjui::DuiButton());
        later->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        later->SetVariant(balloonwjui::DuiButton::Variant::Default);
        later->SetCtrlId(kIdLater);
        later->SetText(TR(L"稍后"));
        m_btnLater = later.get();
        row->AddChild(std::move(later),
                      balloonwjui::DuiLayout::Hint().Fixed(kBtnLaterW, kButtonH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> now(new balloonwjui::DuiButton());
        now->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        now->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        now->SetCtrlId(kIdUpdateNow);
        now->SetText(TR(L"立即更新"));
        m_btnUpdateNow = now.get();
        row->AddChild(std::move(now),
                      balloonwjui::DuiLayout::Hint().Fixed(kBtnUpdateW, kButtonH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    }

    SetClientContent(std::move(root));

    m_built = true;
}

// =======================================================================
// 状态呈现
// =======================================================================

void CBalloonUpdateWindow::ApplyViewState(const update::UpdateViewState& st)
{
    m_blockClose = !st.closable;

    if (m_labTitle != NULL)
    {
        m_labTitle->SetText(st.title.c_str());
    }

    if (m_labVersion != NULL)
    {
        m_labVersion->SetText(st.versionLine.c_str());
        //无更新 / 失败时版本行为空串，整行隐藏，避免留下一条空白行占据版面
        m_labVersion->SetVisible(!st.versionLine.empty());
    }

    //更新说明是 UTF-8，转宽串后填进只读 RichEdit；没有可用新版本时为空串，说明框清空。
    //
    //<u>说明区始终显示、高度固定</u>，即便发布侧没填说明也留着那个空框。早先是"没内容
    //就整块隐掉"，结果窗口布局会随内容有无而变，下方控件跟着上下挪 —— 实测出现过
    //进度条压住进度文字、只剩半截的花屏。固定高度换来的是布局恒定，代价只是空框占位。
    if (m_editNote != NULL)
    {
        const std::wstring note = EncodeUtil::Utf8ToUnicode(st.releaseNoteUtf8);
        m_editNote->SetText(note.c_str());
    }

    if (m_labSize != NULL)
    {
        //包大小只在有可下载的包时才有意义
        if (st.canDownload && m_packageSize > 0)
        {
            const std::wstring text =
                TRF(L"升级包 {0}", balloonchatwnd::FormatFileSize(m_packageSize));
            m_labSize->SetText(text.c_str());
            m_labSize->SetVisible(true);
        }
        else
        {
            m_labSize->SetVisible(false);
        }
    }

    if (m_btnUpdateNow != NULL)
    {
        //没有可下载的包时（已是最新 / 检查失败）不给这个按钮
        m_btnUpdateNow->SetVisible(st.canDownload);
        m_btnUpdateNow->SetEnabled(st.canDownload);
    }

    if (m_btnLater != NULL)
    {
        if (st.showLaterButton)
        {
            //非强制：这个按钮兼作"稍后"与"知道了"的关窗出口
            m_btnLater->SetVisible(true);
            m_btnLater->SetText(st.canDownload ? TR(L"稍后") : TR(L"知道了"));
        }
        else if (st.canDownload)
        {
            //强制升级且升级包可用：下载与安装全程自动完成，用户没有任何需要做的
            //选择，因此不给按钮 —— 既没有「稍后」，也不提供退出程序的出口。
            m_btnLater->SetVisible(false);
        }
        else
        {
            //强制升级、但服务端给的升级包信息不全（缺下载令牌 / MD5 / 大小），自动
            //升级无从进行。这种情况下再不给出口，用户就只能去任务管理器结束进程了，
            //所以保留一条退出途径。属于发布侧的记录有问题，正常不会走到。
            m_btnLater->SetVisible(true);
            m_btnLater->SetText(TR(L"退出程序"));
        }
    }

    //强制升级期间置顶：此时其它窗口都被禁用，升级窗一旦被别的窗口盖住，用户既看不到
    //进度、又点不动任何东西，会以为程序卡死。非强制形态取消置顶，不去妨碍用户干活。
    SetWindowTopMost(m_blockClose);

    //切换形态时隐藏上一轮的下载进度
    if (m_progress != NULL)
    {
        m_progress->SetVisible(false);
        m_progress->SetPos(0, false);
    }
    if (m_labProgress != NULL)
    {
        m_labProgress->SetVisible(false);
    }

    //强制升级时连标题栏的关闭按钮也去掉，只留一个"必须升级"的出口
    SetButtons(false, false, st.closable);

    //上面改了一堆控件的显隐，必须重排，否则新露出来的控件没有矩形、画不出来
    RelayoutClientContent();
    Invalidate();
}

void CBalloonUpdateWindow::ApplyCheckingUi()
{
    //过渡态一律允许关闭：此刻还不知道是不是强制升级，不能先把用户关在窗口里。
    //真查出是强制升级，结果回来时 ApplyViewState 会把关闭途径收掉。
    m_blockClose = false;
    SetButtons(false, false, true);

    if (m_labTitle != NULL)
    {
        m_labTitle->SetText(TR(L"正在检查更新..."));
    }
    if (m_labVersion != NULL)
    {
        m_labVersion->SetVisible(false);
    }
    if (m_labSize != NULL)
    {
        m_labSize->SetVisible(false);
    }
    if (m_editNote != NULL)
    {
        m_editNote->SetText(_T(""));
    }
    if (m_progress != NULL)
    {
        m_progress->SetVisible(false);
        m_progress->SetPos(0, false);
    }
    if (m_labProgress != NULL)
    {
        m_labProgress->SetVisible(false);
    }

    //查完之前两个按钮都不可用：此时既没有可下载的包，也没有结论可"知道了"
    if (m_btnUpdateNow != NULL)
    {
        m_btnUpdateNow->SetVisible(false);
    }
    if (m_btnLater != NULL)
    {
        m_btnLater->SetVisible(true);
        m_btnLater->SetText(TR(L"取消"));
    }

    RelayoutClientContent();
    Invalidate();
}

void CBalloonUpdateWindow::RelayoutClientContent()
{
    balloonwjui::DuiControl* root = GetClientContent();
    if (root == NULL)
    {
        return;
    }
    //用它当前的矩形重排一遍：矩形没变，变的是哪些子控件参与布局。
    root->ForceLayout(root->GetRect());

    //再强制整窗擦除重绘。ForceLayout 内部那次 Invalidate 力度不够：控件位置变了之后，
    //原地留下的旧像素不会被擦掉 —— 实测表现为进度条压在进度文字上、文字只剩半截，
    //以及隐藏掉的更新说明框仍留一个灰色方框在屏幕上。erase 传 TRUE 才会先刷背景。
    if (m_hWnd != NULL && ::IsWindow(m_hWnd))
    {
        ::InvalidateRect(m_hWnd, NULL, TRUE);
        ::UpdateWindow(m_hWnd);
    }
}

void CBalloonUpdateWindow::StartMockProgress(long long totalBytes)
{
    if (totalBytes <= 0)
    {
        return;
    }

    m_packageSize  = totalBytes;
    m_mockTotal    = totalBytes;
    m_mockReceived = 0;

    EnterDownloadingUi();
    SetTimer(kMockProgressTimerId, kMockProgressIntervalMs);
    LOG_INFO(_T("Update window: mock progress started, total bytes %lld."), totalBytes);
}

LRESULT CBalloonUpdateWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kMockProgressTimerId)
    {
        //不是假进度的定时器：放行给基类，别把别人的定时器吃掉
        bHandled = FALSE;
        return 0;
    }

    bHandled = TRUE;

    const long long step = m_mockTotal / kMockProgressSteps + 1;
    m_mockReceived += step;

    const bool done = (m_mockReceived >= m_mockTotal);
    if (done)
    {
        m_mockReceived = m_mockTotal;
        KillTimer(kMockProgressTimerId);
    }

    //刻意走真实消息通路：这样看到的就是真下载时的样子
    update::UpdateProgressInfo* info = new update::UpdateProgressInfo();
    info->m_received = m_mockReceived;
    info->m_total    = m_mockTotal;
    if (!PostMessage(FMG_MSG_UPDATE_PROGRESS, 0, (LPARAM)info))
    {
        delete info;
    }

    if (done)
    {
        //把"下载完成"那一档也演出来：主按钮变「立即重启并安装」
        update::UpdateTaskResult* r = new update::UpdateTaskResult();
        r->m_valid         = true;
        r->m_transportOk   = true;
        r->m_hasUpdate     = true;
        r->m_packageUsable = true;
        r->m_size          = m_mockTotal;
        r->m_downloaded    = true;
        r->m_localPath     = TR(L"（演示用，未真正下载）");
        if (!PostMessage(FMG_MSG_UPDATE_RESULT, 0, (LPARAM)r))
        {
            delete r;
        }
        LOG_INFO(_T("Update window: mock progress finished."));
    }

    return 0;
}

void CBalloonUpdateWindow::EnterDownloadingUi()
{
    if (m_progress != NULL)
    {
        m_progress->SetPos(0, false);
        m_progress->SetVisible(true);
    }
    if (m_labProgress != NULL)
    {
        m_labProgress->SetText(TR(L"正在准备下载..."));
        m_labProgress->SetVisible(true);
    }
    if (m_btnUpdateNow != NULL)
    {
        //下载期间不让重复点：再点一次会排队跑第二次下载
        m_btnUpdateNow->SetEnabled(false);
    }

    //进度条与进度文字刚从隐藏切到显示，必须重排才画得出来
    RelayoutClientContent();
    Invalidate();
}

void CBalloonUpdateWindow::OnClickUpdateNow()
{
    //第二阶段：包已经下载并校验通过，这一下是"安装"
    if (m_readyToInstall)
    {
        LaunchUpdaterAndExit();
        return;
    }

    //第一阶段：先把包下下来。记下"这是用户主动要升级"，下载完成后直接装，
    //不再要求他再点一次。
    StartDownloadAndInstall();
    LOG_INFO(_T("Update window: user clicked update now, download task posted."));
}

void CBalloonUpdateWindow::StartDownloadAndInstall()
{
    m_installAfterDownload = true;
    EnterDownloadingUi();
    //true = 发现新版本就顺带下载。结果与进度都会经窗口消息回到本窗口。
    update::UpdateManager::GetInstance().CheckAndDownload(true);
}

void CBalloonUpdateWindow::SetWindowTopMost(bool topMost)
{
    if (m_hWnd == NULL || !::IsWindow(m_hWnd))
    {
        return;
    }

    //SWP_NOACTIVATE：只改 Z 序，不抢走当前的键盘焦点。
    ::SetWindowPos(m_hWnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST,
                   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CBalloonUpdateWindow::AutoRunForcedUpdate(const update::UpdateTaskResult& r)
{
    //非强制：升不升级由用户决定，这里什么都不做。
    if (!r.m_forced || !r.m_hasUpdate || !r.m_packageUsable)
    {
        return;
    }

    //包已经下好了（登录后静默检查那条路会顺带下载）：直接进安装。
    if (r.m_downloaded && !r.m_localPath.empty())
    {
        m_readyToInstall = true;
        m_packagePath    = r.m_localPath;
        LOG_INFO(_T("Update window: forced update and package ready, installing automatically. Path %s."),
                 m_packagePath.c_str());
        LaunchUpdaterAndExit();
        return;
    }

    LOG_INFO(_T("Update window: forced update, downloading the package automatically."));
    StartDownloadAndInstall();
}

bool CBalloonUpdateWindow::LaunchUpdaterAndExit()
{
    if (m_packagePath.empty())
    {
        LOG_ERROR(_T("Update window: package path is empty, cannot start the updater."));
        return false;
    }

    //更新器与主程序同处一个目录
    std::wstring updater(g_szHomePath);
    if (!updater.empty() && updater[updater.size() - 1] != L'\\'
        && updater[updater.size() - 1] != L'/')
    {
        updater += L'\\';
    }
    const std::wstring installDir = updater;   //以分隔符结尾的安装目录
    updater += L"FlamingoUpdater.exe";

    if (::GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_ERROR(_T("Update window: updater not found: %s."), updater.c_str());
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(TR(L"找不到更新程序，无法安装"));
        }
        return false;
    }

    //安装目录参数不带尾部分隔符：更新器内部按 <目录>\<相对路径> 拼，
    //带了会拼出双分隔符，虽然 Windows 能容忍，但日志里看着别扭。
    std::wstring targetDir = installDir;
    if (targetDir.size() > 1
        && (targetDir[targetDir.size() - 1] == L'\\' || targetDir[targetDir.size() - 1] == L'/'))
    {
        targetDir.erase(targetDir.size() - 1);
    }

    //主程序自身的完整路径。升级完成后由更新器把它重新拉起来。
    wchar_t selfPath[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    wchar_t pidText[32] = { 0 };
    ::swprintf_s(pidText, L"%u", ::GetCurrentProcessId());

    //路径一律加引号：安装目录常含空格（Program Files）
    std::wstring cmdline = L"\"" + updater + L"\""
                         + L" --zip \"" + m_packagePath + L"\""
                         + L" --target \"" + targetDir + L"\""
                         + L" --exec \"" + std::wstring(selfPath) + L"\""
                         + L" --pid " + pidText
                         //多语言第 3c 步：更新器的提示框用主程序当前生效的界面语言
                         + L" --lang " + i18n::GetLanguage();

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    //CreateProcessW 会就地修改命令行缓冲，不能直接传 c_str()
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    const BOOL ok = ::CreateProcessW(updater.c_str(), &buf[0], NULL, NULL, FALSE, 0, NULL,
                                     targetDir.c_str(), &si, &pi);
    if (!ok)
    {
        LOG_ERROR(_T("Update window: failed to start the updater, error %u."), ::GetLastError());
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(TR(L"启动更新程序失败"));
        }
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    LOG_INFO(_T("Update window: updater started, command line %s. Asking the main program to exit so the files can be replaced."),
             cmdline.c_str());

    //更新器要等主进程退出才能动文件，这里请程序退出。走与「退出程序」按钮同一套
    //逻辑：登录前交给登录窗自己关，登录后请主窗口直接收尾 —— 两条路混用会把程序
    //收尾两遍，第二遍对着已销毁的窗口调 DestroyWindow 直接崩（详见
    //RequestApplicationExit 的说明）。
    //
    //退出不了也不会损坏任何文件：更新器等 30 秒后会如实提示并放弃。
    const GatedWindowKind gatedKind = m_gatedKind;
    const HWND            gatedWnd  = m_gatedWnd;
    //先降级成"普通模态窗"：这次是去安装升级包，不是回头继续登录。
    m_gatedKind = kGatedModalDlg;
    ReleaseGatedWindow();
    RequestApplicationExit(gatedKind, gatedWnd);

    return true;
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonUpdateWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;

    if (m_blockClose)
    {
        //强制升级：拦截并忽略关闭请求。此时用户本就无法登录，允许关窗只会使其停留在
        //登录窗反复重试。
        LOG_INFO(_T("Update window: forced update in progress, close request ignored."));
        return 0;
    }

    //关窗即"用户选择了稍后"：记下这个版本，登录后的静默检查不再对它重复弹窗。
    //强制升级走不到这里（上面已拦下），所以不会误记。
    if (!m_shownVersion.empty())
    {
        m_postponedVersion = m_shownVersion;
    }

    //把被挡住的窗口恢复可用，并放行那次被拦下的登录。
    //强制升级走不到这里（上面已拦下），所以强制升级时它一直是禁用的。
    ReleaseGatedWindow();

    //摘掉投递目标要在销毁窗口之前：否则工作线程可能正好往这个 HWND 投消息，
    //消息被系统丢弃，lParam 上的堆对象就此泄漏且没有任何报错。
    update::UpdateManager::GetInstance().SetNotifyTarget(NULL);

    //窗口销毁会连带销毁整棵控件树，本类持有的都是借用裸指针，必须一并置空，
    //否则下次打开时 BuildUiOnce 之前的任何访问都是野指针。
    m_labTitle     = NULL;
    m_labVersion   = NULL;
    m_labSize      = NULL;
    m_editNote     = NULL;
    m_progress     = NULL;
    m_labProgress  = NULL;
    m_btnUpdateNow = NULL;
    m_btnLater     = NULL;
    m_built        = false;

    //安装态不跨越"关窗再打开"：重开时应当从头走一遍检查，而不是拿上一次的包直接装。
    m_readyToInstall = false;
    m_installAfterDownload = false;
    m_packagePath.clear();

    DestroyWindow();
    return 0;
}

LRESULT CBalloonUpdateWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;

    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    if (pNotify == NULL)
    {
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdUpdateNow:
            OnClickUpdateNow();
            return 0;
        case kIdLater:
            if (m_blockClose)
            {
                //强制升级形态下这个按钮是「退出程序」：直接退出，不回到登录窗 /
                //主面板 —— 那两个此刻都被禁用着，回去也没法用。
                LOG_INFO(_T("Update window: user chose to exit the program during a forced update."));
                ExitApplication();
                return 0;
            }
            //非强制：「稍后」等价于关闭窗口，关闭时会恢复被挡窗口并续上登录
            PostMessage(WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonUpdateWindow::OnUpdateProgress(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;

    update::UpdateProgressInfo* info = (update::UpdateProgressInfo*)lParam;
    if (info == NULL)
    {
        return 0;
    }

    //总大小以服务端逐片应答里的为准；它为 0 时退回本窗口记下的包大小
    const long long total = info->m_total > 0
                          ? static_cast<long long>(info->m_total)
                          : m_packageSize;
    const long long received = static_cast<long long>(info->m_received);

    if (m_progress != NULL)
    {
        m_progress->SetPos(update::ComputeDownloadPercent(received, total));
    }

    if (m_labProgress != NULL)
    {
        //总大小已知时写「正在下载 已下载 / 总大小」，未知时只写已下载的大小
        std::wstring text;
        if (total > 0)
        {
            text = TRF(L"正在下载 {0} / {1}", balloonchatwnd::FormatFileSize(received),
                       balloonchatwnd::FormatFileSize(total));
        }
        else
        {
            text = TRF(L"正在下载 {0}", balloonchatwnd::FormatFileSize(received));
        }
        m_labProgress->SetText(text.c_str());
    }

    delete info;
    return 0;
}

LRESULT CBalloonUpdateWindow::OnUpdateResult(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;

    update::UpdateTaskResult* r = (update::UpdateTaskResult*)lParam;
    if (r == NULL)
    {
        return 0;
    }

    if (r->m_downloaded)
    {
        //包已下载并校验通过。主按钮的含义从"下载"切换为"安装"：点下去拉起更新器、
        //请主程序退出，剩下的覆盖与重启由更新器完成。
        m_readyToInstall = true;
        m_packagePath    = r->m_localPath;

        if (m_progress != NULL)
        {
            m_progress->SetPos(100);
        }
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(TR(L"下载完成，可以安装了"));
        }
        //用户点过「立即更新」：下完直接装，不再要他确认第二次
        if (m_installAfterDownload)
        {
            //先把"正在启动安装"画出来再去起进程：CreateProcess 加上随后的退出收尾
            //会占住 UI 线程，不先刷一次的话这段时间界面还停在"下载完成"上。
            if (m_labProgress != NULL)
            {
                m_labProgress->SetText(TR(L"下载完成，正在启动安装..."));
            }
            if (m_btnUpdateNow != NULL)
            {
                m_btnUpdateNow->SetEnabled(false);
            }
            Invalidate();
            UpdateWindow();

            LOG_INFO(_T("Update window: package downloaded, user already chose to update, installing automatically. Path %s."),
                     m_packagePath.c_str());
            if (LaunchUpdaterAndExit())
            {
                delete r;
                return 0;
            }

            //起更新器失败（找不到 FlamingoUpdater.exe、CreateProcess 失败等）。
            //失败原因 LaunchUpdaterAndExit 内部已写进进度文字，这里退回"等用户确认"
            //的样子，把按钮还给他重试，而不是让界面停在"正在启动安装"上不动。
            m_installAfterDownload = false;
        }

        //自动下载（autodownloadupdate=1）那条路：包已备好，但装不装由用户决定
        if (m_btnUpdateNow != NULL)
        {
            m_btnUpdateNow->SetText(TR(L"立即重启并安装"));
            m_btnUpdateNow->SetVisible(true);
            m_btnUpdateNow->SetEnabled(true);
            RelayoutClientContent();
        }
        LOG_INFO(_T("Update window: package downloaded and verified, waiting for the user to confirm installing. Path %s."),
                 m_packagePath.c_str());
    }
    else if (r->m_downloadAttempted && r->m_hasUpdate && r->m_packageUsable)
    {
        //升级包可用但下载失败：把按钮恢复为可用以便重试，并在进度文字处说明失败。
        //这里必须先看 m_downloadAttempted：纯检查的结果同样是 hasUpdate=1、
        //packageUsable=1、downloaded=0，不加这一条就会把关于页「检查更新」查到的
        //新版本误报成"下载失败"（实测现象是关于页点检查后提示失败，查不出新版本）。
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(TR(L"下载失败，请重试"));
        }
        if (m_btnUpdateNow != NULL)
        {
            m_btnUpdateNow->SetEnabled(true);
        }
        //撤掉"下完就装"的意图：这次没下成，用户要重试得重新点一次
        m_installAfterDownload = false;
        LOG_ERROR(_T("Update window: package download failed."));
    }
    else
    {
        //本次是一次纯检查（或检查本身失败）：按新结果重新呈现界面，版本行、按钮与更新说明都随视图状态填写。
        //与 ShowResult 一样记下所显示的版本：用户随后关窗即视为对这个版本选了「稍后」
        m_shownVersion = r->m_latestVersion;
        m_packageSize = r->m_packageUsable ? static_cast<long long>(r->m_size) : 0;
        ApplyViewState(update::BuildUpdateViewState(*r, FLAMINGO_CLIENT_VERSION_W));
        Invalidate();

        //查出来是强制升级：不等用户点，直接开始下载并安装。关于页的「检查更新」查到
        //强制版本时走的就是这条路。放在 Invalidate 之后，因为它可能一路走到程序退出，
        //那之后本窗口已销毁、控件指针全部置空，不能再有界面操作。
        const update::UpdateTaskResult snapshot = *r;
        delete r;
        AutoRunForcedUpdate(snapshot);
        return 0;
    }

    Invalidate();
    delete r;
    return 0;
}
