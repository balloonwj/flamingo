/**
 *  ClosePromptDecision.h
 *
 *  主面板"关闭确认"对话框的纯逻辑决策 —— 无 UI / HWND / 业务对象依赖，便于被
 *  FlamingoUnit 测试工程直接断言。对话框 CBalloonClosePromptDlg 用它把"用户选了
 *  哪种关闭方式 + 是否勾选记住我的选择"换算成"本次执行什么动作 + 要写回 UserConfig
 *  的两个偏好位"；打开对话框时也用它由当前偏好推出默认选中哪张卡片。
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

} // namespace balloonmain
