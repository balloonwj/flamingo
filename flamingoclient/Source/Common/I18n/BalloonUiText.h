/**
 *  balloonui 内置文字的多语言：把 balloonui 库里自带的界面文字（输入框与富文本框右键菜单的命令文字）
 *  经 i18n::Translate 换成当前界面语言的译文。
 *
 *  balloonui 不在提取脚本的扫描范围内，它自带的文字在本文件里用 TR_NOOP 再列一遍，供提取脚本收进 .po；
 *  两边的原文必须逐字相同（含 & 助记符），否则查不到译文，菜单仍显示中文。
 *
 *  只有头文件，exe 与两个插件 DLL 直接包含即可，不必登记进各自的工程文件。典型用法（初始化语言之后、创建任何窗口之前）：
 *      balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>

#include "I18n.h"

namespace i18n {

/**
 *  balloonui 自带文字的清单，只做标记供提取脚本收录，运行时不使用。
 *  对应 balloonui 的 EditContextCommandLabel（Controls/Input/EditContextMenu.h）与
 *  DuiRichEditMenuCommandLabel（Controls/Input/RichEditContextMenu.h）；那边增删文字时这里同步。
 *  @param count [出参] 条数，可为 NULL
 *  @return 原文数组的首元素
 */
inline const wchar_t* const* BalloonUiBuiltinTexts(size_t* count)
{
    static const wchar_t* const kTexts[] =
    {
        TR_NOOP(L"剪切(&T)"),
        TR_NOOP(L"复制(&C)"),
        TR_NOOP(L"粘贴(&P)"),
        TR_NOOP(L"全选(&A)"),
        TR_NOOP(L"撤销(&U)"),
        TR_NOOP(L"重做(&R)"),
        TR_NOOP(L"粘贴为纯文本(&L)"),
        TR_NOOP(L"删除(&D)"),
    };
    if (count != NULL)
    {
        *count = sizeof(kTexts) / sizeof(kTexts[0]);
    }
    return kTexts;
}

/**
 *  交给 balloonwjui::SetTextResolver 的回调：查 balloonui 内置文字的译文。可在任意线程调用。
 *  @param text balloonui 内置的原文
 *  @return 当前界面语言的译文，在进程生命周期内有效；没有译文时返回 text 本身
 */
inline LPCTSTR ResolveBalloonUiText(LPCTSTR text)
{
    return Translate(text);
}

} // namespace i18n
