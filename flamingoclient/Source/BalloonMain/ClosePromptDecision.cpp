/**
 *  ClosePromptDecision.cpp
 *
 *  主面板"关闭确认"对话框纯逻辑决策的实现。纯逻辑、不依赖任何头文件以外的东西，
 *  不 include stdafx.h —— 与 FlamingoUnit 测试工程共用，在 Flamingo.vcxproj 里
 *  需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#include "ClosePromptDecision.h"

namespace balloonmain {

CloseDecision DecideOnConfirm(int selectedAction, bool remember)
{
    //选中项归一化：只认"退出"，其余（含越界值）一律按"最小化"处理。
    bool exit = (selectedAction == kCloseActionExit);

    CloseDecision d;
    d.action       = exit ? kCloseActionExit : kCloseActionMinimize;
    //勾选"记住我的选择" → 下次不再弹框；未勾选则保持每次都提示。
    d.enablePrompt  = !remember;
    //记住的"无提示时是否退出"与本次选择一致，保证下次直达同一动作。
    d.exitWhenClose = exit;
    return d;
}

int DefaultSelection(bool exitWhenClose)
{
    //上次记住的是"退出"则默认选中退出，否则默认选中最小化（首次配置为 FALSE）。
    return exitWhenClose ? kCloseActionExit : kCloseActionMinimize;
}

// =======================================================================
// ClosePromptHostGuard —— 宿主窗"禁用 — 恢复"判定
// =======================================================================

ClosePromptHostGuard::ClosePromptHostGuard()
    : m_hostDisabled(false)
{
}

bool ClosePromptHostGuard::OnOpen(bool hostIsWindow, bool hostIsEnabled)
{
    //已经禁用着：对话框正在显示时又触发了一次打开（例如代码再次向主面板投
    //WM_CLOSE）。不重复禁用，也不动标记 —— 恢复仍由后面唯一的一次 OnClose 负责。
    if (m_hostDisabled)
        return false;

    //宿主窗句柄无效：没有可禁用的对象。此处不置标记，关闭时也就不会去恢复。
    if (!hostIsWindow)
        return false;

    //宿主窗本来就不可操作：说明另有窗口把它禁用着。这里若再禁用一次，本对话框
    //关闭时的恢复会把别人的禁用状态一并抹掉，让本不该能操作的窗口变得能操作。
    if (!hostIsEnabled)
        return false;

    m_hostDisabled = true;
    return true;
}

bool ClosePromptHostGuard::OnClose(bool hostIsWindow)
{
    //没禁用过就没有要恢复的东西。重复走到关闭收尾时也从这里直接返回，保证幂等。
    if (!m_hostDisabled)
        return false;

    //标记一律清掉：本轮"禁用 — 恢复"到此结束。宿主窗若已销毁则无需再调
    //::EnableWindow，故返回值取决于它是否仍是有效窗口。
    m_hostDisabled = false;
    return hostIsWindow;
}

} // namespace balloonmain
