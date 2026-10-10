/**
 *  主面板底部右侧主操作按钮的取值规则，MainPanelBottomAction.h
 *
 *  按钮随页签变化：「群」页签是「建群」；其余页签在文件共享可用（FlamingoFileShare.dll 已加载且本企业的功能开关打开）
 *  时是「文件共享」，打开「文件共享」窗口（2026-10-03 起，代替原来的「查找」），不可用时仍是「查找」（把光标移到顶部
 *  搜索框），避免出现点了没反应的入口。纯函数，主面板与 FlamingoUnit 共用。
 *
 *  典型用法：
 *      const int action = balloonmain::BottomActionFor(tabId == kIdTabGroups, fileShareReady);
 *      button->SetText(balloonmain::BottomActionText(action));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

//底部主操作按钮的动作
enum MainPanelBottomAction
{
    kBottomActionFind        = 0,   //「查找」：把键盘焦点移到顶部搜索框（文件共享不可用时的非群页签）
    kBottomActionCreateGroup = 1,   //「建群」：打开新建群对话框（「群」页签）
    kBottomActionFileShare   = 2,   //「文件共享」：打开「文件共享」窗口（文件共享可用时的非群页签）
};

/**
 *  当前页签下按钮的动作。
 *  @param groupsTab      当前是否为「群」页签
 *  @param fileShareReady 文件共享是否可用（CFileShareHost::IsModuleReady）
 *  @return MainPanelBottomAction 之一
 */
inline int BottomActionFor(bool groupsTab, bool fileShareReady)
{
    if (groupsTab)
        return kBottomActionCreateGroup;
    return fileShareReady ? kBottomActionFileShare : kBottomActionFind;
}

/**
 *  按钮文字。
 *  @param action MainPanelBottomAction 之一；不认识的值按「查找」处理
 *  @return 文字（静态存储）
 */
inline const wchar_t* BottomActionText(int action)
{
    switch (action)
    {
    //「群」页签
    case kBottomActionCreateGroup:
        return TR(L"建群");
    //文件共享可用时的非群页签
    case kBottomActionFileShare:
        return TR(L"文件共享");
    //文件共享不可用时的非群页签，以及不认识的值
    default:
        return TR(L"查找");
    }
}

} // namespace balloonmain
