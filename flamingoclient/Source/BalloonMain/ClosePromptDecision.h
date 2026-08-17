/**
 *  ClosePromptDecision.h
 *
 *  主面板"关闭确认"对话框的纯逻辑决策 —— 无 UI / HWND / 业务对象依赖，便于被
 *  FlamingoUnit 测试工程直接断言。对话框 CBalloonClosePromptDlg 用它把"用户选了
 *  哪种关闭方式 + 是否勾选记住我的选择"换算成"本次执行什么动作 + 要写回 UserConfig
 *  的两个偏好位"；打开对话框时也用它由当前偏好推出默认选中哪张卡片。
 *
 *  本文件另收一个 ClosePromptHostGuard —— 对话框以模态方式显示期间，主面板
 *  "禁用 — 恢复"的判定状态机，同样是纯逻辑、可单测。
 *
 *  设计稿：design/退出确认1.png（默认选中"最小化到任务栏"）/ design/退出确认2.png
 *  （选中"退出 FlamingoIM"）。
 *
 *  本文件不依赖 MFC / balloonui，只用标准类型，可脱离 UI 单测。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

namespace balloonmain {

/**
 *  关闭主面板时的两种动作。
 */
enum CloseAction
{
    kCloseActionMinimize = 0,   //最小化到任务栏：隐藏主面板窗口，托盘图标保留，可再唤出
    kCloseActionExit     = 1,   //退出 FlamingoIM：整体收尾退出进程，下次需重新登录
};

/**
 *  点"确认"后的决策结果：本次要执行的动作 + 要写回 UserConfig 的两个偏好位。
 *  对话框据此调 EnableExitPrompt / EnableExitWhenCloseMainDlg 再落盘。
 */
struct CloseDecision
{
    int  action;          //本次要执行的动作，取值见 CloseAction
    bool enablePrompt;    //写回 CUserConfig::EnableExitPrompt：下次关闭是否仍弹确认框
    bool exitWhenClose;   //写回 CUserConfig::EnableExitWhenCloseMainDlg：无提示时是否退出
};

/**
 *  根据选中项与"记住我的选择"算出关闭决策。
 *    selectedAction：用户选中的关闭方式，取值见 CloseAction（越界一律按最小化处理）。
 *    remember：是否勾选"记住我的选择"。勾选 → 下次不再弹框（enablePrompt=false）。
 *  @return 见 CloseDecision；其中 exitWhenClose 恒等于"选中项是否为退出"，使
 *          记住的偏好与本次选择一致（与旧 CClosePromptDlg 同口径）。
 */
CloseDecision DecideOnConfirm(int selectedAction, bool remember);

/**
 *  由"无提示时是否退出"偏好推出对话框打开时默认选中哪张卡片。
 *    exitWhenClose：CUserConfig::IsEnableExitWhenCloseMainDlg() 的当前值。
 *  @return kCloseActionExit（exitWhenClose 为真）或 kCloseActionMinimize（为假）。
 *          首次使用时配置默认 FALSE，故默认选中"最小化"，与设计稿一致。
 */
int DefaultSelection(bool exitWhenClose);

/**
 *  ClosePromptHostGuard —— 关闭确认对话框显示期间，宿主窗（主面板）"禁用 — 恢复"
 *  的判定状态机。
 *
 *  对话框以"禁用宿主窗 + owner 关系"的方式做到模态：打开时把主面板置为不可操作，
 *  关闭时再恢复。真正调 ::EnableWindow 的是对话框，本类只回答"这一步该不该调"，
 *  从而把几处容易出错的判定（宿主窗已被别的窗口禁用、对话框重复打开、关闭路径
 *  重复走到、宿主窗已销毁）集中到一处，并且能脱离 UI 单测。
 *
 *  典型用法（对话框侧）：
 *      if (m_hostGuard.OnOpen(hostIsWindow, hostIsEnabled))
 *          ::EnableWindow(hostHwnd, FALSE);
 *      ...
 *      if (m_hostGuard.OnClose(hostIsWindow))
 *          ::EnableWindow(hostHwnd, TRUE);
 *
 *  生命周期：与对话框对象同生命周期（对话框窗口可反复创建销毁，本状态机跨次复用）。
 */
class ClosePromptHostGuard
{
public:
    ClosePromptHostGuard();

    /**
     *  对话框打开时调用，回答"是否应当禁用宿主窗"。
     *    hostIsWindow： 宿主窗句柄当前是否为有效窗口（调用方用 ::IsWindow 取得）。
     *    hostIsEnabled：宿主窗当前是否可操作（调用方用 ::IsWindowEnabled 取得）。
     *  @return true 表示调用方应当调 ::EnableWindow(host, FALSE)，同时本类记下
     *          "关闭时需要恢复"；false 表示不要动宿主窗。
     *
     *  返回 false 有三种情形：本类已经把宿主窗禁用着（对话框已在显示，又一次触发
     *  打开）；宿主窗句柄无效；宿主窗本来就不可操作（另有窗口把它禁用着，此时若
     *  再禁用一次，恢复那一下会把别人的禁用状态一并抹掉）。
     */
    bool OnOpen(bool hostIsWindow, bool hostIsEnabled);

    /**
     *  对话框关闭收尾时调用，回答"是否应当把宿主窗恢复为可操作"。
     *    hostIsWindow：宿主窗句柄当前是否为有效窗口。
     *  @return true 表示调用方应当调 ::EnableWindow(host, TRUE)。
     *
     *  本方法幂等：只要调用过一次，内部"待恢复"标记即被清掉，之后再调一律返回
     *  false。对话框的结束路径有多条（确认、取消、ESC、标题栏关闭按钮，以及主面板
     *  销毁时连带销毁本窗口），每条都会走到这里，靠幂等保证恢复动作只执行一次。
     *  宿主窗已销毁时同样清标记 —— 窗口已经不存在，没有可恢复的对象。
     */
    bool OnClose(bool hostIsWindow);

    //宿主窗当前是否正被本类禁用着（尚未恢复）。供排查与断言使用。
    bool IsHostDisabled() const { return m_hostDisabled; }

private:
    bool m_hostDisabled;   //本类是否禁用过宿主窗且尚未恢复；决定关闭时要不要恢复
};

} // namespace balloonmain
