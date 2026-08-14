/**
 *  GroupMentionButton.h
 *
 *  群聊窗输入工具栏右侧的「@提醒」按钮 —— 白底、1px 描边、圆角的小按钮。
 *  对照 design 08 / chat.jsx 群聊 Composer 里的 @提醒 按钮。继承 DuiButton
 *  复用点击 / 通知逻辑，只覆写 OnPaint。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonchatwnd {

/**
 *  GroupMentionButton —— 白底描边「@提醒」按钮。文字在构造时默认设为
 *  "@提醒"。
 */
class GroupMentionButton : public balloonwjui::DuiButton
{
public:
    GroupMentionButton();
    ~GroupMentionButton();   // 注销本控件的 tooltip 注册

    // DuiControl 覆写：绘制白底圆角 + 1px 描边 + 文字。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }
};

} // namespace balloonchatwnd
