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

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/DuiNotify.h"
#include "../UserSessionData.h"    // FMG_MSG_UPDATE_PROGRESS / FMG_MSG_UPDATE_RESULT
#include "../Update/UpdateViewModel.h"

namespace balloonwjui {
class DuiLabel;
class DuiButton;
class DuiProgressBar;
class DuiRichEditHost;
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
     *  登记主窗口句柄。升级包安装前必须让主程序先退出，本类据此句柄请它关闭。
     *
     *    hWnd： CMainDlg 的窗口句柄；传 NULL 表示清除
     *
     *  由 CMainDlg 在自己创建完成后调一次。不登记的话，用户点「立即重启并安装」时
     *  更新器会因为等不到主进程退出而超时放弃 —— 不会损坏任何文件，但升级做不成。
     */
    void SetHostWindow(HWND hWnd);

    /**
     *  登记"登录闸门"窗口 —— 即因为本窗口而暂停了登录的那个登录窗。
     *
     *    hWnd： 登录窗句柄；传 NULL 表示本次不是登录前拦截
     *
     *  登记之后，用户关掉本窗口（点「稍后」或标题栏关闭）时会给它投一条
     *  FMG_MSG_UPDATE_LOGIN_CONTINUE，登录随即继续。强制升级时本窗口关不掉，
     *  这条消息发不出去，登录也就一直进行不下去 —— 这正是强制升级要的效果。
     */
    void SetLoginGateWindow(HWND hWnd);

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

    //构建一次静态骨架（各控件创建后结构固定，之后只修改文本与显隐状态）。
    void BuildUiOnce();

    //把 UpdateViewState 应用到各控件：文案、按钮显隐、是否允许关闭窗口。
    //  st：由 BuildUpdateViewState 计算出的状态。
    void ApplyViewState(const update::UpdateViewState& st);

    //把键盘焦点从更新说明那个只读文本框移开，消掉它里面闪烁的光标
    //（只读的展示区带光标会让人以为可以编辑）。窗口每次显示后调一次。
    void TakeFocusFromNote();

    //按是否有更新说明正文，决定「更新说明」小标题与正文框一起显示还是一起隐藏。
    //  hasNote：正文是否非空。
    //无更新 / 检查失败时正文本来就是空的，留一个空框在那里既难看又会带出光标。
    void SetNoteAreaVisible(bool hasNote);

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

    /**
     *  拉起 iUpdateAuto.exe 并请主程序退出，把安装交给更新器。
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

    //本次要下载的包大小（字节），用于进度文字「12.3 MB / 36.4 MB」。
    //取自最近一次 UpdateTaskResult::m_size；0 表示未知。
    long long m_packageSize;

    //升级包是否已经下载并校验通过。为 true 时主按钮的含义从"下载"变成"安装"。
    bool m_readyToInstall;

    //已下载落地的升级包完整路径，m_readyToInstall 为 true 时有效。
    std::wstring m_packagePath;

    //主窗口句柄，由 SetHostWindow 登记。安装前请它关闭，好让更新器能覆盖文件。
    HWND m_hostWnd;

    //登录闸门窗口句柄。非 NULL 表示本次是"登录前拦截"，关窗时要通知它继续登录。
    //通知过一次就置空，避免同一次登录被放行两遍。
    HWND m_loginGateWnd;

    //标题栏左侧小图标（app 图标 16x16 合成到标题栏底色上）。DuiFrameWindow::SetIcon
    //只借用不持有，故由本类持有并在析构时 DeleteObject。只建一次，跨"关窗再打开"复用。
    HBITMAP m_titleIconBmp;

    //以下控件由 balloonui 的控件树持有，本类只借用裸指针用于修改文本与显隐状态，
    //不负责释放。窗口销毁时控件树一并销毁，故须在 OnCloseMsg 中置空这些指针。
    balloonwjui::DuiLabel*        m_labTitle;       //标题
    balloonwjui::DuiLabel*        m_labVersion;     //版本行
    balloonwjui::DuiLabel*        m_labSize;        //升级包大小
    balloonwjui::DuiLabel*        m_labNoteCaption; //「更新说明」小标题
    balloonwjui::DuiRichEditHost* m_editNote;       //更新说明（只读多行）
    balloonwjui::DuiProgressBar*  m_progress;       //下载进度条
    balloonwjui::DuiLabel*        m_labProgress;    //进度文字
    balloonwjui::DuiButton*       m_btnUpdateNow;   //立即更新
    balloonwjui::DuiButton*       m_btnLater;       //稍后
};
