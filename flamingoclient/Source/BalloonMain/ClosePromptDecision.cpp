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

} // namespace balloonmain
