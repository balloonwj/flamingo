/**
 *  升级窗实现，BalloonUpdateWindow.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"
#include "BalloonUpdateWindow.h"

#include <string>
#include <vector>

#include "../ClientVersion.h"                  // FLAMINGO_CLIENT_VERSION_W
#include "../EncodeUtil.h"                     // Utf8ToUnicode（更新说明是 UTF-8）
#include "../IULog.h"
#include "../resource.h"                     // IDR_MAINFRAME（标题栏图标）
#include "../UserSessionData.h"                 // g_szHomePath（更新器与主程序同目录）
#include "../Update/UpdateManager.h"
#include "../BalloonChatWnd/FileBubbleFormat.h" // FormatFileSize（复用，不另写一份）
#include "../../balloonui/balloonui/DuiResMgr.h"
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"

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
static const int kNoteCaptionH  = 20;    // 「更新说明」小标题行高
static const int kNoteH         = 120;   // 更新说明正文高度（可滚动，不够就滚）
static const int kProgressTopGap= 12;    // 更新说明与进度条之间的间距
static const int kProgressH     = 14;    // 进度条高
static const int kProgressTextH = 20;    // 进度文字行高
static const int kButtonRowH    = 36;    // 按钮行高
static const int kButtonH       = 32;    // 按钮高
static const int kBtnLaterW     = 88;    // 「稍后」按钮宽
static const int kBtnUpdateW    = 110;   // 「立即更新」按钮宽
static const int kButtonGap     = 10;    // 两个按钮之间的间距

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
static const wchar_t* const kWindowTitle = L"FlamingoIM升级提醒";

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
    , m_readyToInstall(false)
    , m_hostWnd(NULL)
    , m_loginGateWnd(NULL)
    , m_titleIconBmp(NULL)
    , m_labTitle(NULL)
    , m_labVersion(NULL)
    , m_labSize(NULL)
    , m_labNoteCaption(NULL)
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

void CBalloonUpdateWindow::SetLoginGateWindow(HWND hWnd)
{
    m_loginGateWnd = hWnd;
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

    // owner 传 NULL：让子控件通知自路由回本窗口（与关于框 / 改密框同处理）。
    if (Create(NULL, CWindow::rcDefault, kWindowTitle,
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("升级窗 Create 失败。"));
        return false;
    }
    SetTitleBarHeight(kDlgTitleBarH);
    SetMinSize(kDlgMinWidth, kDlgMinHeight);
    SetTitle(kWindowTitle);

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
            LOG_ERROR(_T("升级窗：加载 app 图标失败，标题栏将无图标。"));
        }
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    m_built = false;
    BuildUiOnce();
    ResizeClient(kDlgWidth, kDlgHeight);
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
    TakeFocusFromNote();

    //false = 只检查、不下载。用户主动点检查时不该在他不知情的情况下下载几十兆的包，
    //下载等他看到版本信息后点「立即更新」。
    update::UpdateManager::GetInstance().CheckAndDownload(false);
    LOG_INFO(_T("升级窗：已发起一次检查更新（只检查不下载）。"));
}

void CBalloonUpdateWindow::ShowResult(const update::UpdateTaskResult& r)
{
    if (!EnsureWindowCreated())
    {
        return;
    }

    //记下包大小供进度文字用。无可用包时为 0，进度文字会退化成只显示已收字节。
    m_packageSize = r.m_packageUsable ? static_cast<long long>(r.m_size) : 0;

    const update::UpdateViewState st =
        update::BuildUpdateViewState(r, FLAMINGO_CLIENT_VERSION_W);
    ApplyViewState(st);

    //更新说明是 UTF-8，转宽串后填进只读 RichEdit。没有说明时（已是最新 / 检查失败，
    //或者发布侧没填）连同小标题一起隐掉——留一个空框在那里既难看，又会带出一个
    //闪烁的光标让人以为可以往里打字。
    const std::wstring note = EncodeUtil::Utf8ToUnicode(r.m_releaseNote);
    if (m_editNote != NULL)
    {
        m_editNote->SetText(note.c_str());
    }
    SetNoteAreaVisible(!note.empty());

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    TakeFocusFromNote();

    LOG_INFO(_T("升级窗已显示：kind=%d，强制=%d。"),
             (int)st.kind, st.closable ? 0 : 1);
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
        HFONT titleFont = balloonwjui::DuiResMgr::Inst().GetFontByPointSize(kTitlePt, true);
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetTextColor(kTitleColor);
        if (titleFont != NULL)
        {
            lab->SetFont(titleFont);
        }
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
        HFONT capFont = balloonwjui::DuiResMgr::Inst().GetFontByPointSize(kCaptionPt, false);
        std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
        lab->SetText(_T("更新说明"));
        lab->SetTextColor(kGrayTextColor);
        if (capFont != NULL)
        {
            lab->SetFont(capFont);
        }
        lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_labNoteCaption = lab.get();
        root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kNoteCaptionH));
    }

    // ---- 更新说明正文（只读多行，自带滚动）----
    // 用 DuiRichEditHost 而不是 DuiLabel：说明可能很长，需要能滚动，而 balloonui
    // 里没有独立的滚动容器控件，RichEdit 宿主本身就带滚动，只读化后正好合用。
    {
        std::unique_ptr<balloonwjui::DuiRichEditHost> re(new balloonwjui::DuiRichEditHost());
        re->SetMultiLine(true);
        re->SetWordWrap(true);
        re->SetReadOnly(true);
        re->SetBackgroundColor(kNoteBgColor);
        re->SetTextColor(kNoteTextColor);
        re->SetMargins(8, 6, 8, 6);
        //只读展示，不需要右键菜单里的剪切 / 粘贴
        re->SetContextMenuEnabled(false);
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
        later->SetText(_T("稍后"));
        m_btnLater = later.get();
        row->AddChild(std::move(later),
                      balloonwjui::DuiLayout::Hint().Fixed(kBtnLaterW, kButtonH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> now(new balloonwjui::DuiButton());
        now->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        now->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        now->SetCtrlId(kIdUpdateNow);
        now->SetText(_T("立即更新"));
        m_btnUpdateNow = now.get();
        row->AddChild(std::move(now),
                      balloonwjui::DuiLayout::Hint().Fixed(kBtnUpdateW, kButtonH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    }

    SetClientContent(std::move(root));

    //RichEdit 是真实子窗口，须在客户区内容设置到宿主窗口之后再创建，否则没有父窗口。
    if (m_editNote != NULL)
    {
        if (!m_editNote->EnsureCreated(m_hWnd))
        {
            LOG_ERROR(_T("升级窗：更新说明 RichEdit 创建失败，说明区将为空白。"));
        }
    }

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

    if (m_labSize != NULL)
    {
        //包大小只在有可下载的包时才有意义
        if (st.canDownload && m_packageSize > 0)
        {
            const std::wstring text =
                std::wstring(L"升级包 ") + balloonchatwnd::FormatFileSize(m_packageSize);
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
        //强制升级时隐藏「稍后」；其余形态下该按钮同时充当"知道了"的关闭按钮
        m_btnLater->SetVisible(st.showLaterButton);
        m_btnLater->SetText(st.canDownload ? _T("稍后") : _T("知道了"));
    }

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

    Invalidate();
}

void CBalloonUpdateWindow::SetNoteAreaVisible(bool hasNote)
{
    if (m_labNoteCaption != NULL)
    {
        m_labNoteCaption->SetVisible(hasNote);
    }
    if (m_editNote != NULL)
    {
        m_editNote->SetVisible(hasNote);
    }
}

void CBalloonUpdateWindow::TakeFocusFromNote()
{
    //更新说明用的 DuiRichEditHost 内部包着一个真的 RICHEDIT50W 子窗口，窗口显示后
    //键盘焦点默认落在它身上，于是只读的说明区里会有一个闪烁的光标，看着像可以编辑。
    //balloonui 没有对外暴露那个子窗口句柄，也没有"不接受焦点"的开关，所以这里换个
    //办法：把 DUI 焦点交给一个按钮，宿主随之把 Win32 焦点从 RichEdit 子窗口上收走，
    //光标消失。之所以不去改 balloonui 加接口，是因为改库要先经用户同意，而在库外
    //就能解决。
    if (m_btnUpdateNow != NULL && m_btnUpdateNow->IsVisible())
    {
        m_btnUpdateNow->SetFocus();
        return;
    }
    if (m_btnLater != NULL && m_btnLater->IsVisible())
    {
        m_btnLater->SetFocus();
        return;
    }
    if (m_hWnd != NULL)
    {
        ::SetFocus(m_hWnd);
    }
}

void CBalloonUpdateWindow::ApplyCheckingUi()
{
    //过渡态一律允许关闭：此刻还不知道是不是强制升级，不能先把用户关在窗口里。
    //真查出是强制升级，结果回来时 ApplyViewState 会把关闭途径收掉。
    m_blockClose = false;
    SetButtons(false, false, true);

    if (m_labTitle != NULL)
    {
        m_labTitle->SetText(_T("正在检查更新..."));
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
    //还没查到任何东西，说明区整块收起来
    SetNoteAreaVisible(false);
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
        m_btnLater->SetText(_T("取消"));
    }

    Invalidate();
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
        m_labProgress->SetText(_T("正在准备下载..."));
        m_labProgress->SetVisible(true);
    }
    if (m_btnUpdateNow != NULL)
    {
        //下载期间不让重复点：再点一次会排队跑第二次下载
        m_btnUpdateNow->SetEnabled(false);
    }

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

    //第一阶段：先把包下下来
    EnterDownloadingUi();
    //true = 发现新版本就顺带下载。结果与进度都会经窗口消息回到本窗口。
    update::UpdateManager::GetInstance().CheckAndDownload(true);
    LOG_INFO(_T("升级窗：用户点击「立即更新」，已投递下载任务。"));
}

bool CBalloonUpdateWindow::LaunchUpdaterAndExit()
{
    if (m_packagePath.empty())
    {
        LOG_ERROR(_T("升级窗：升级包路径为空，无法启动更新器。"));
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
    updater += L"iUpdateAuto.exe";

    if (::GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_ERROR(_T("升级窗：找不到更新器 %s。"), updater.c_str());
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(_T("找不到更新程序，无法安装"));
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
                         + L" --pid " + pidText;

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
        LOG_ERROR(_T("升级窗：启动更新器失败，错误码 %u。"), ::GetLastError());
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(_T("启动更新程序失败"));
        }
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    LOG_INFO(_T("升级窗：更新器已启动，命令行 %s。请主程序退出以便覆盖文件。"),
             cmdline.c_str());

    //更新器要等主进程退出才能动文件。这里请主窗口关闭；主窗口没登记时只能靠用户
    //自己关，更新器等 30 秒后会如实提示并放弃，不会损坏任何文件。
    if (m_hostWnd != NULL && ::IsWindow(m_hostWnd))
    {
        //专用消息而非 WM_CLOSE：后者会先弹「退出程序还是最小化到托盘？」，
        //用户选「最小化」的话程序不退出，更新器等 30 秒后放弃，升级白做一场。
        ::PostMessage(m_hostWnd, FMG_MSG_EXIT_FOR_UPDATE, 0, 0);
    }
    else
    {
        LOG_ERROR(_T("升级窗：未登记主窗口句柄，无法自动退出主程序。"));
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(_T("请手动退出程序以完成安装"));
        }
    }

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
        LOG_INFO(_T("升级窗：强制升级中，关闭请求被忽略。"));
        return 0;
    }

    //关窗即"用户选择了稍后"：放行那个被挡住的登录。强制升级走不到这里（上面已拦下），
    //所以强制升级时登录会一直被挡着。
    if (m_loginGateWnd != NULL)
    {
        if (::IsWindow(m_loginGateWnd))
        {
            ::PostMessage(m_loginGateWnd, FMG_MSG_UPDATE_LOGIN_CONTINUE, 0, 0);
        }
        //只放行一次
        m_loginGateWnd = NULL;
    }

    //摘掉投递目标要在销毁窗口之前：否则工作线程可能正好往这个 HWND 投消息，
    //消息被系统丢弃，lParam 上的堆对象就此泄漏且没有任何报错。
    update::UpdateManager::GetInstance().SetNotifyTarget(NULL);

    //窗口销毁会连带销毁整棵控件树，本类持有的都是借用裸指针，必须一并置空，
    //否则下次打开时 BuildUiOnce 之前的任何访问都是野指针。
    m_labTitle     = NULL;
    m_labVersion   = NULL;
    m_labSize      = NULL;
    m_labNoteCaption = NULL;
    m_editNote     = NULL;
    m_progress     = NULL;
    m_labProgress  = NULL;
    m_btnUpdateNow = NULL;
    m_btnLater     = NULL;
    m_built        = false;

    //安装态不跨越"关窗再打开"：重开时应当从头走一遍检查，而不是拿上一次的包直接装。
    m_readyToInstall = false;
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
            //「稍后」等价于关闭窗口；强制升级时该按钮不显示，不会执行到此处
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
        std::wstring text = std::wstring(L"正在下载 ")
                          + balloonchatwnd::FormatFileSize(received);
        if (total > 0)
        {
            text += L" / ";
            text += balloonchatwnd::FormatFileSize(total);
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
            m_labProgress->SetText(_T("下载完成，可以安装了"));
        }
        if (m_btnUpdateNow != NULL)
        {
            m_btnUpdateNow->SetText(_T("立即重启并安装"));
            m_btnUpdateNow->SetVisible(true);
            m_btnUpdateNow->SetEnabled(true);
        }
        LOG_INFO(_T("升级窗：升级包下载完成并校验通过，路径 %s。"), m_packagePath.c_str());
    }
    else if (r->m_hasUpdate && r->m_packageUsable)
    {
        //升级包可用但下载失败：把按钮恢复为可用以便重试，并在进度文字处说明失败
        if (m_labProgress != NULL)
        {
            m_labProgress->SetText(_T("下载失败，请重试"));
        }
        if (m_btnUpdateNow != NULL)
        {
            m_btnUpdateNow->SetEnabled(true);
        }
        LOG_ERROR(_T("升级窗：升级包下载失败。"));
    }
    else
    {
        //本次是一次纯检查（或检查本身失败）：按新结果重新呈现界面
        m_packageSize = r->m_packageUsable ? static_cast<long long>(r->m_size) : 0;
        ApplyViewState(update::BuildUpdateViewState(*r, FLAMINGO_CLIENT_VERSION_W));
    }

    Invalidate();
    delete r;
    return 0;
}
