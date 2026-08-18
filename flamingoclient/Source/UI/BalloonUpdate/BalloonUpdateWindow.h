/**
 *  BalloonUpdateWindow.h
 *
 *  「发现新版本」升级窗 —— 基于 balloonui DuiFrameWindow 的非模态窗口。
 *  自上而下：标题（发现新版本 / 已是最新 / 检查失败）+ 版本行 + 升级包大小 +
 *  更新说明（只读多行 RichEdit，可滚动）+ 下载进度条与进度文字 + 两个按钮
 *  （立即更新 / 稍后）。
 *
 *  <u>本类只负责呈现状态</u>：具体呈现成什么形态由 Update/UpdateViewModel.h 的
 *  BuildUpdateViewState 算出，本类不自己做判断，那部分逻辑有单测（Suite UVM）。
 *
 *  三个触发点最终都进入本窗口：登录成功后的静默检查、关于页的「检查更新」按钮，
 *  以及登录被服务端以 error_code_version_too_old 拒绝后主动发起的升级。三者的
 *  区别只在于由谁发起。
 *
 *  强制升级时隐藏「稍后」按钮、去掉标题栏关闭按钮、拦截 WM_CLOSE 与 ESC —— 此时
 *  用户本就无法登录，保留关闭途径只会使其停留在登录窗反复重试。
 *
 *  线程：下载跑在 UpdateManager 的工作线程上，进度与结果经 PostMessage
 *  （FMG_MSG_UPDATE_PROGRESS / FMG_MSG_UPDATE_RESULT）回到本窗口所在的 UI 线程，
 *  载荷是堆上结构体指针，本窗口处理后 delete。工作线程不得直接访问任何控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（经 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Session/UserSessionData.h"    // FMG_MSG_UPDATE_PROGRESS / FMG_MSG_UPDATE_RESULT
#include "../../Feature/Update/UpdateViewModel.h"

namespace balloonwjui {
class DuiLabel;
class DuiButton;
class DuiProgressBar;
class DuiRichEdit;
}

/**
 *  CBalloonUpdateWindow —— 升级窗（非模态，进程内单实例由调用方持有）。
 *
 *  典型用法：
 *      CBalloonUpdateWindow& w = CBalloonUpdateWindow::GetInstance();
 *      w.ShowResult(result);          // 拿到一次检查结果后调用
 */
class CBalloonUpdateWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  取进程内单实例。升级窗同一时刻只应存在一个 —— 三个触发点共用本窗口，
     *  各自创建实例会出现两个窗口同时驱动同一次下载。
     */
    static CBalloonUpdateWindow& GetInstance();

    /**
     *  按一次检查更新的结果显示升级窗（窗口未创建则创建，已显示则前置刷新）。
     *
     *    r： 一次检查（可选下载）的结果，通常取自 UpdateManager::TakeLastResult
     *
     *  必须在 UI 线程调用。无可用新版本 / 检查失败时同样会显示 —— 这几种形态用于
     *  「关于页点击检查更新」这类由用户主动发起的场景；登录时的静默检查若不希望
     *  打扰用户，由调用方自行决定不调用本方法。
     */
    void ShowResult(const update::UpdateTaskResult& r);

    /**
     *  显示升级窗并立即发起一次检查更新（窗口先呈现"正在检查更新"）。
     *
     *  用于由用户主动发起、需要立刻给出反馈的两个场景：关于页点击「检查更新」，
     *  以及登录被服务端以 error_code_version_too_old 拒绝之后。这两种情况下不能等
     *  查完再弹窗——中间几秒钟没有任何反馈，用户会以为按钮没反应。
     *
     *  必须在 UI 线程调用。本方法只投递任务并返回，结果经 FMG_MSG_UPDATE_RESULT
     *  回到本窗口，届时再切换为对应形态。只检查、不下载；下载要等用户点「立即更新」。
     */
    void ShowChecking();

    /**
     *  <u>仅供调试入口使用</u>：用假数据驱动一遍下载进度，便于目检进度条与进度文字。
     *
     *    totalBytes： 假装要下载的总字节数
     *
     *  走的是与真实下载<u>完全相同</u>的消息通路（FMG_MSG_UPDATE_PROGRESS →
     *  OnUpdateProgress → ComputeDownloadPercent → DuiProgressBar），所以看到的观感
     *  与真下载一致，而不是单独画一段动画糊弄眼睛。走完后再投一条
     *  FMG_MSG_UPDATE_RESULT（m_downloaded 为 true），把"下载完成"那一档也演出来。
     *
     *  由 MainDlg 的命令行入口 `--updatewindow --progress` 调用，正式流程不涉及。
     */
    void StartMockProgress(long long totalBytes);

    /**
     *  登记主窗口句柄。升级包安装前必须让主程序先退出，本类据此句柄请它关闭。
     *
     *    hWnd： CMainDlg 的窗口句柄；传 NULL 表示清除
     *
     *  由 CMainDlg 在自己创建完成后调一次。不登记的话，用户点「立即重启并安装」时
     *  更新器会因为等不到主进程退出而超时放弃 —— 不会损坏任何文件，但升级做不成。
     */
    void SetHostWindow(HWND hWnd);

    //被升级窗挡住的那个窗口属于哪一类。关窗恢复与程序退出这两条路径都要按类别分别处理。
    enum GatedWindowKind
    {
        //没有登记被挡窗口
        kGatedNone      = 0,
        //登录窗。关掉升级窗要放行那次被拦下的登录；退出程序时交给登录窗自己收尾，
        //由 CMainDlg::StartLogin 在它的模态循环结束后统一处理。
        kGatedLoginDlg  = 1,
        //关于框一类的模态对话框。关掉升级窗只需恢复它可用；退出程序前必须先请它关闭，
        //否则它的模态循环仍在运行，主窗口在其下被销毁会出问题。
        kGatedModalDlg  = 2,
        //主面板。关掉升级窗只需恢复它可用；退出程序由主窗口统一收尾。
        kGatedMainPanel = 3,
    };

    /**
     *  登记被本窗口挡住的那个窗口，实现"相对它模态"。
     *
     *    hWnd：被挡住的窗口；传 NULL 表示本次不挡任何窗口
     *    kind：该窗口属于哪一类，取值见 GatedWindowKind
     *
     *  本窗口显示期间会 EnableWindow(FALSE) 把它整个禁用 —— 不这么做的话，
     *  用户再点一次登录按钮就能绕过升级提示直接登进去，闸门形同虚设；从关于框拉起时
     *  同理，用户还能在关于框上再点一次「检查更新」。
     *  本窗口关闭时恢复可用；kGatedLoginDlg 另投一条 FMG_MSG_UPDATE_LOGIN_CONTINUE
     *  让那次登录接着走。
     *
     *  强制升级时本窗口关不掉，于是被挡的窗口一直处于禁用状态 —— 这正是
     *  "不升级就不能用"要的效果。
     */
    void SetGatedWindow(HWND hWnd, GatedWindowKind kind);

    /**
     *  用户是否已经对某个版本点过「稍后」（本进程内）。
     *
     *    version： 待判断的版本号，取自 UpdateTaskResult::m_latestVersion
     *
     *  @return 该版本已被推迟返回 true。
     *
     *  用于避免同一个版本被反复弹窗：点击登录时先查了一次、用户选了「稍后」，
     *  登录成功后的静默检查还会再查一次并拿到同一个版本 —— 若不拦，用户刚关掉的窗
     *  立刻又弹出来。强制升级不受此限：它本来就没有「稍后」，用户也没机会推迟。
     */
    bool IsVersionPostponed(const std::string& version) const;

    /**
     *  窗口当前是否已创建并可见。调用方据此决定是否刷新已有窗口。
     *  @return 已创建且可见返回 true。
     */
    bool IsShowing() const;

    BEGIN_MSG_MAP(CBalloonUpdateWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(FMG_MSG_UPDATE_PROGRESS, OnUpdateProgress)
        MESSAGE_HANDLER(FMG_MSG_UPDATE_RESULT, OnUpdateResult)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    CBalloonUpdateWindow();
    ~CBalloonUpdateWindow();
    CBalloonUpdateWindow(const CBalloonUpdateWindow&) = delete;
    CBalloonUpdateWindow& operator=(const CBalloonUpdateWindow&) = delete;

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //收到下载进度（由工作线程 PostMessage 投递）。lParam 是堆上的
    //update::UpdateProgressInfo*，处理完毕后在本函数内 delete。
    LRESULT OnUpdateProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //收到一次检查 / 下载的最终结果。lParam 是堆上的 update::UpdateTaskResult*，
    //处理完毕后在本函数内 delete。
    LRESULT OnUpdateResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  重排客户区。<u>凡是改过控件显隐，都必须调一次</u>。
     *
     *  balloonui 的 DuiVBox / DuiHBox 在布局时会<u>整个跳过不可见的子控件</u>
     *  （见 DuiLayout.cpp 的 `if (!up->IsVisible()) continue;`）——它们连矩形都拿不到。
     *  所以事后单纯 SetVisible(true) 是画不出来的：控件的 m_rcItem 还是上一次布局时
     *  留下的空矩形，Invalidate 也只是让一块空区域重绘一遍。
     *
     *  实测踩过：下载开始后进度条与进度文字始终不出现，而按钮状态、文案都正常变化，
     *  看着像"控件没建出来"，实则是没重排。
     *
     *  DuiFrameWindow::LayoutSkeleton 是私有的，子类够不着，故走公开的
     *  GetClientContent() + DuiControl::ForceLayout()，效果相同且不必改 balloonui。
     */
    void RelayoutClientContent();

    //定时器消息。目前只用于假进度调试入口；其余定时器一律放行给基类。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建一次静态骨架（各控件创建后结构固定，之后只修改文本与显隐状态）。
    void BuildUiOnce();

    //把 UpdateViewState 应用到各控件：文案、按钮显隐、是否允许关闭窗口。
    //  st：由 BuildUpdateViewState 计算出的状态。
    void ApplyViewState(const update::UpdateViewState& st);

    //退出整个程序。强制升级形态下「退出程序」按钮用它 —— 此刻登录窗 / 主面板
    //都处于禁用状态，回到它们没有意义，只能直接走。
    void ExitApplication();

    /**
     *  请程序退出，并按当前处在登录前还是登录后选择正确的退出路径。
     *
     *  这两条路<u>不能混用</u>：
     *    · 登录前（被挡的是登录窗）——把 WM_CLOSE 投给登录窗，让它自己退出模态循环。
     *      随后 CMainDlg::StartLogin 见 DoModal 返回非 IDOK，会统一调一次 CloseDialog
     *      收尾。此时若由本窗口先调一次 CloseDialog，登录窗的模态循环随后仍会返回、
     *      StartLogin 再收尾一次，第二次对着已销毁的窗口调 DestroyWindow，触发
     *      ATL 断言 `::IsWindow(this->m_hWnd)`（atlwin.h:4066）—— 已实测崩过。
     *    · 登录后（被挡的是主面板或无人被挡）——投 FMG_MSG_EXIT_FOR_UPDATE 给主窗口，
     *      由它直接 CloseDialog，不弹退出提示框。
     *
     *  调用前本窗口应已自行销毁（见 ExitApplication / LaunchUpdaterAndExit）。
     *
     *    gatedKind：       被挡住的窗口属于哪一类，取值见 GatedWindowKind
     *    gatedWnd：        被挡住的窗口句柄，可为 NULL
     */
    void RequestApplicationExit(GatedWindowKind gatedKind, HWND gatedWnd);

    //把被挡住的窗口恢复可用；若登记时要求了，再请它继续那次被拦下的登录。
    //只生效一次，之后 m_gatedWnd 置空。本窗口关闭时调。
    void ReleaseGatedWindow();

    //切换为"正在检查更新"的呈现：标题给出提示，两个按钮都不可用，不显示进度条。
    //这一形态不经 BuildUpdateViewState —— 它不是由检查结果推出来的判断，
    //而是一个固定的过渡态。
    void ApplyCheckingUi();

    //窗口尚未创建时创建它并登记消息投递目标；已创建则什么都不做。
    //  @return 创建成功或已创建返回 true。
    bool EnsureWindowCreated();

    //切换为"正在下载"的呈现：显示进度条与进度文字，并禁用按钮。
    void EnterDownloadingUi();

    //响应主按钮。按当前阶段分流：升级包尚未下载时投递下载任务；已经下载并校验通过时
    //拉起更新器并请主程序退出。两阶段共用一个按钮，只改文案。
    void OnClickUpdateNow();

    //投递下载任务，并把本次标记为"下载完成后自动安装"。
    //由「立即更新」按钮与强制升级的自动流程共用：两者都是"用户已经确定要升级"，
    //区别只在于前者是他点的、后者是没得选。
    void StartDownloadAndInstall();

    //把窗口置顶 / 取消置顶。
    //  topMost：true 置顶。
    //强制升级期间用户做不了别的事，窗口一旦被其它窗口盖住就看不到升级进度，
    //会让人以为程序卡住了，所以那种形态下要置顶。
    void SetWindowTopMost(bool topMost);

    //强制升级且升级包可用时，自动开始下载 / 安装，不等用户操作。
    //  r：本次检查结果。
    //已经下载好的包直接进安装；尚未下载的先投递下载任务，下载完成后由
    //OnUpdateResult 里的自动安装分支接上。非强制、或包信息不全时什么都不做。
    void AutoRunForcedUpdate(const update::UpdateTaskResult& r);

    /**
     *  拉起 FlamingoUpdater.exe 并请主程序退出，把安装交给更新器。
     *
     *  @return 更新器已成功启动返回 true；启动失败返回 false，此时主程序不会退出。
     *
     *  更新器需要主进程先退出才能覆盖文件（它按 --pid 等待，等不到就放弃，绝不强杀），
     *  所以启动它之后必须请主窗口关闭。
     */
    bool LaunchUpdaterAndExit();

private:
    bool  m_built;              //客户区骨架是否已构建完成

    //当前形态是否禁止关闭窗口（强制升级）。OnCloseMsg 据此决定是否拦截关闭请求。
    //单独记录而不去读控件状态，避免界面尚未刷新时与逻辑判定结果不一致。
    bool  m_blockClose;

    //当前窗口正在呈现的版本号；关窗时据它记录"用户推迟了哪个版本"。
    std::string m_shownVersion;

    //用户点过「稍后」的版本号。同一进程内不再对它重复弹窗。
    std::string m_postponedVersion;

    //本次下载是不是用户点「立即更新」触发的。是的话下载完成即自动安装重启，
    //不再要求用户第二次点「立即重启并安装」—— 点第一下时就已经表达了要升级。
    //登录后静默检查顺带下载（flamingo.ini 的 autodownloadupdate=1）时为 false：
    //那是背着用户下的，不能下完就把程序关掉重启。
    bool m_installAfterDownload;

    //本次要下载的包大小（字节），用于进度文字「12.3 MB / 36.4 MB」。
    //取自最近一次 UpdateTaskResult::m_size；0 表示未知。
    long long m_packageSize;

    //假进度调试入口的状态：已"收到"字节数与总字节数。仅 --progress 入口用。
    long long m_mockReceived;
    long long m_mockTotal;

    //升级包是否已经下载并校验通过。为 true 时主按钮的含义从"下载"变成"安装"。
    bool m_readyToInstall;

    //已下载落地的升级包完整路径，m_readyToInstall 为 true 时有效。
    std::wstring m_packagePath;

    //主窗口句柄，由 SetHostWindow 登记。安装前请它关闭，好让更新器能覆盖文件。
    HWND m_hostWnd;

    //被本窗口挡住的窗口句柄（登录窗或主面板）。非 NULL 时它处于禁用状态，
    //本窗口关闭时恢复。恢复过一次就置空，避免重复处理。
    HWND m_gatedWnd;

    //本窗口关闭时是否要请被挡窗口继续那次被拦下的登录。只有登录窗那条路是 true。
    GatedWindowKind m_gatedKind;

    //标题栏左侧小图标（app 图标 16x16 合成到标题栏底色上）。DuiFrameWindow::SetIcon
    //只借用不持有，故由本类持有并在析构时 DeleteObject。只建一次，跨"关窗再打开"复用。
    HBITMAP m_titleIconBmp;

    //以下控件由 balloonui 的控件树持有，本类只借用裸指针用于修改文本与显隐状态，
    //不负责释放。窗口销毁时控件树一并销毁，故须在 OnCloseMsg 中置空这些指针。
    balloonwjui::DuiLabel*        m_labTitle;       //标题
    balloonwjui::DuiLabel*        m_labVersion;     //版本行
    balloonwjui::DuiLabel*        m_labSize;        //升级包大小
    balloonwjui::DuiRichEdit* m_editNote;       //更新说明（只读多行）
    balloonwjui::DuiProgressBar*  m_progress;       //下载进度条
    balloonwjui::DuiLabel*        m_labProgress;    //进度文字
    balloonwjui::DuiButton*       m_btnUpdateNow;   //立即更新
    balloonwjui::DuiButton*       m_btnLater;       //稍后
};
