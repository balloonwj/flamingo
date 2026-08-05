/**
 *  ChatIconButton.h
 *
 *  单聊窗用的透明底图标按钮 —— 头部的语音/视频/记录/更多，输入工具栏的
 *  表情/图片/文件/截图/语音/更多，都用它。继承 DuiButton 复用点击 /
 *  通知逻辑，只覆写 OnPaint。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonchatwnd {

/**
 *  ChatIconButton —— 透明底图标按钮，悬停时显示浅灰圆角底。
 */
class ChatIconButton : public balloonwjui::DuiButton
{
public:
    ChatIconButton();
    ~ChatIconButton();   // 注销本控件的 tooltip 注册

    // 设置图标类型，取值见 ChatIcons.h 的 ChatIconKind。
    void SetIconKind(int kind);

    // DuiControl 覆写：绘制图标（+ 悬停底）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int m_iconKind;   // 图标类型（ChatIconKind）
};

} // namespace balloonchatwnd
