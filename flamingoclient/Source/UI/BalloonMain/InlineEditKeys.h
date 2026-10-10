/**
 *  InlineEditKeys.h
 *
 *  行内编辑输入框（DuiEdit）的通知判定：回车提交、Esc 取消、失去焦点延后判定（2026-10-07）。联系人
 *  资料窗的「备注名」与群资料对话框的「我在本群的昵称」共用这一份判定，两处的回车与 Esc 行为一致。
 *  在此之前两处只处理了失去焦点，回车与 Esc 的通知落到各自 OnDuiNotify 的末尾被忽略，按了没有反应。
 *
 *  判定条件同时比较通知码与控件 id（开发约定文档「UI 约定」第 4 条）：balloonui 的自定义通知码按控件各自
 *  编号，别的控件发来的同值通知不能被当成输入框的回车或 Esc。
 *
 *  纯函数，header-only，不碰窗口，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用；单元测试见
 *  Tests/InlineEditKeysTests.cpp。
 *
 *  典型用法（窗口的 OnDuiNotify）：
 *      switch (balloonmain::ClassifyInlineEditNotify(pNotify->code, pNotify->ctrlId, kIdRemarkEdit, m_editing))
 *      {
 *      //回车：提交
 *      case balloonmain::inline_edit_commit:
 *          CommitRemarkEdit();
 *          return 0;
 *      ...
 *      }
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"               // DUIN_KILLFOCUS
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"   // DUIN_EDIT_ENTER / DUIN_EDIT_ESCAPE

namespace balloonmain {

// 行内编辑输入框发来一条通知之后要做的事
enum InlineEditKeyAction
{
    inline_edit_none   = 0,   // 不是这个输入框的回车、Esc 或失焦通知，或者当前不在编辑态：交给后面的分支继续判定
    inline_edit_commit = 1,   // 输入框里按了回车：立即提交并退出编辑态
    inline_edit_cancel = 2,   // 输入框里按了 Esc：放弃改动、恢复原值并退出编辑态，不提交
    inline_edit_blur   = 3,   // 输入框失去焦点：延后一拍再判定是否提交（在框内点击会短暂失焦再得焦）
};

/**
 *  判定一条控件通知对行内编辑输入框意味着什么。
 *    code：       通知码（DuiNotify::code）。
 *    ctrlId：     发出通知的控件 id（DuiNotify::ctrlId）。
 *    editCtrlId： 行内编辑输入框的控件 id。
 *    editing：    当前是否处于该输入框的编辑态；不在编辑态时一律返回 inline_edit_none。
 *  返回：要做的事，取值见 InlineEditKeyAction。纯函数，无副作用。
 */
inline InlineEditKeyAction ClassifyInlineEditNotify(UINT code, UINT ctrlId, UINT editCtrlId, bool editing)
{
    if (!editing || ctrlId != editCtrlId)
    {
        return inline_edit_none;
    }
    if (code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER)
    {
        return inline_edit_commit;
    }
    if (code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE)
    {
        return inline_edit_cancel;
    }
    if (code == (UINT)balloonwjui::DUIN_KILLFOCUS)
    {
        return inline_edit_blur;
    }
    return inline_edit_none;
}

} // namespace balloonmain
