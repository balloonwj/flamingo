/**
 *  命令行调试入口：开发期不登录、直接弹出某个窗口或跑一段自测的那批 --xxx 开关，
 *  连同它们用到的预览窗口对象，集中在这一个类里。
 *
 *  全部分支只在 _DEBUG 构建中编译，正式发布构建里 TryRun 直接返回「未命中」。
 *
 *  典型用法：宿主按值持有本类，在初始化流程的末尾（其它初始化都做完、正式登录之前）
 *  问一次：
 *
 *      balloonapp::DebugEntryContext ctx;
 *      ctx.m_hHost = m_panel.m_hWnd;
 *      ctx.m_pPanel = &m_panel;
 *      ...
 *      const int r = m_debugEntries.TryRun(ctx);
 *      if (r == balloonapp::kDebugEntryRequestExit)
 *      {
 *          CloseDialog(IDOK);
 *          return TRUE;
 *      }
 *      if (r == balloonapp::kDebugEntryHandled)
 *          return TRUE;
 *
 *  balloonwj@qq.com   2026-08-19
 */

#pragma once

#include "../UI/BalloonChatWnd/BalloonChatWindow.h"
#include "../UI/BalloonChatWnd/BalloonGroupChatWindow.h"
#include "../UI/BalloonRegister/BalloonRegisterWindow.h"
#include "../UI/BalloonProfile/BalloonProfileWindow.h"
#include "../UI/BalloonContact/BalloonContactWindow.h"
#include "../UI/BalloonGroupManage/BalloonGroupManageWindow.h"
#include "../UI/BalloonHistory/BalloonHistoryWindow.h"
#include "../UI/BalloonHistory/MergedRecordWindow.h"
#include "../UI/BalloonSettings/BalloonSystemSettingDlg.h"
#include "../UI/BalloonMain/BalloonAboutDlg.h"
#include "../UI/BalloonMain/BalloonGroupInfoDlg.h"
#include "../UI/BalloonMain/ForwardTargetDlg.h"
#include "../UI/BalloonImageViewer/BalloonImageViewerWindow.h"
#include "../UI/BalloonCode/CodeSnippetDemoWindow.h"

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonmain { class CTrayController; }
namespace flamingomedia { class IMediaModule; }

namespace balloonapp {

/**
 *  TryRun 的处理结果。
 */
enum DebugEntryResult
{
    //命令行里没有任何调试开关，调用方照常往下走正常的初始化与登录流程。
    kDebugEntryNotMatched = 0,

    //命中某个调试开关并已处理完毕。调用方应当结束初始化直接返回，进程留在消息
    //循环里，预览窗口开着，靠托盘菜单或关窗退出。
    kDebugEntryHandled,

    //命中某个调试开关并已处理完毕，且该分支要求立即退出进程（目前只有 --kickout：
    //它模拟的是被踢下线，关掉提示框就该走完整的退出流程）。调用方应当执行自己的
    //退出序列。
    kDebugEntryRequestExit,
};

/**
 *  调试入口运行时需要向宿主借用的对象。
 *
 *  这些对象本类都不拥有，只在本次调用期间借用。m_bFakeLoggedIn 是出参。
 */
struct DebugEntryContext
{
    //主面板窗口句柄。两处用途：作 MessageBox 与模态提示框的父窗口；以及作预览窗口的
    //「关掉我就退出进程」目标 —— 主面板收到 WM_CLOSE_MAINDLG 会请求进程退出。
    //不可为 NULL。
    HWND m_hHost;

    //主面板。--mainpanel 用它建窗并推连接态。可为 NULL，此时该入口不生效。
    CBalloonMainPanel* m_pPanel;

    //托盘控制器。--kickout 用它取托盘图标位置，好让提示框贴着托盘弹出。可为 NULL。
    balloonmain::CTrayController* m_pTray;

    //FlamingoMedia.dll 的模块接口。已随代码迁入 DLL 的调试入口（2026-10-02 起为一对一通话的 7 个与远程桌面的 8 个）
    //由它运行：TryRun 先把命令行交给它，它未命中才检查本类自己的开关。可为 NULL（DLL 未加载），
    //此时这批入口不生效。
    flamingomedia::IMediaModule* m_pMediaModule;

    //[出参] --mainpanel 模拟的是「已经登录、主面板已打开」这个局面，它会把这一项置真，
    //宿主据此把自己的登录标志一并置真 —— 托盘右键菜单的形态就是据登录标志选的。
    //其余入口不改动本项。
    bool m_bFakeLoggedIn;

    DebugEntryContext()
        : m_hHost(NULL)
        , m_pPanel(NULL)
        , m_pTray(NULL)
        , m_pMediaModule(NULL)
        , m_bFakeLoggedIn(false)
    {
    }
};

/**
 *  命令行调试入口的执行者，同时是那批预览窗口的持有者。
 *
 *  预览窗口都是 ATL 窗口类（balloonui 的 DuiHost / DuiFrameWindow 派生），对象析构时
 *  若仍持有 HWND 会触发 ATL 的断言（atlwin.h 的 "Object deleted before window was
 *  destroyed"），所以本类的析构函数会统一销毁它们，宿主也可以提前调
 *  DestroyPreviewWindows 收尾。
 */
class CDebugEntryRunner
{
public:
    /**
     *  构造。
     *    pFMGClient  网络客户端，本类不拥有。其中八个预览窗口的构造函数需要它，
     *                --groupinfo 也要把它传给群资料窗。可为 NULL。
     */
    explicit CDebugEntryRunner(CFlamingoClient* pFMGClient);
    ~CDebugEntryRunner();

    /**
     *  按命令行逐个匹配调试开关，命中就执行对应分支。
     *    ctx  借用的宿主对象与出参，见 DebugEntryContext。
     *  返回值见 DebugEntryResult。正式发布构建（非 _DEBUG）恒返回 kDebugEntryNotMatched。
     */
    DebugEntryResult TryRun(DebugEntryContext& ctx);

    /**
     *  销毁本类持有的全部预览窗口。宿主退出前调用；没建过窗口时调用也安全，
     *  可重复调用。
     */
    void DestroyPreviewWindows();

    /**
     *  本类持有的预览窗口里，还有几个仍然持有窗口句柄。
     *  只供宿主的退出诊断日志使用 —— 退出若仍崩，据它判断是不是预览窗口没收干净。
     */
    int CountAlivePreviewWindows() const;

private:
    CFlamingoClient* m_lpFMGClient;                     //网络客户端，借用不拥有

    CBalloonChatWindow m_chatWindow;                    //--chatwindow：气泡式单聊窗预览
    CBalloonGroupChatWindow m_groupChatWindow;          //--groupchatwindow：群聊窗预览
    CBalloonRegisterWindow m_registerWindow;            //--register：注册窗预览
    CBalloonProfileWindow m_profileWindow;              //--profile：个人信息窗预览
    CBalloonContactWindow m_contactWindow;              //--contact：联系人资料窗预览
    CBalloonGroupManageWindow m_groupManageWindow;      //--groupmanage：群管理窗预览
    CBalloonHistoryWindow m_historyWindow;              //--history：历史消息记录窗预览
    CBalloonSystemSettingDlg m_systemSettingDlg;        //--syssettings：系统设置窗预览
    CBalloonAboutDlg m_aboutDlg;                        //--about：关于窗预览
    balloonmain::CBalloonGroupInfoDlg m_groupInfoDlg;   //--groupinfo：群资料窗预览
    balloonmain::CForwardTargetDlg m_forwardPickerDlg;  //--forwardpicker：转发目标选择窗预览（压测数据）
    CMergedRecordWindow m_mergedViewer;                 //--mergedviewer：合并转发聊天记录查看窗预览（构造的记录）
    CBalloonImageViewerWindow m_imageViewerWindow;      //--imageviewer：聊天图片放大看图窗预览
    ballooncode::CCodeSnippetDemoWindow m_codeSnippetDemo;  //--codesnippet：代码片段效果演示窗（粘贴代码对照现状与新方案）
};

} // namespace balloonapp
