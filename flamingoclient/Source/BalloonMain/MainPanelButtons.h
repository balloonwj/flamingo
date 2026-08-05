/**
 *  MainPanelButtons.h
 *
 *  主面板底部栏用到的两个按钮控件：
 *    · MainPanelIconButton —— 透明底图标按钮（左下角汉堡菜单）。
 *    · MainPanelActionButton —— 白底 + 1px 边框 + 加号图标 + 文字的主操作
 *      按钮（联系人页"查找" / 群页"建群"）。
 *
 *  两者都继承 DuiButton 复用点击 / 通知逻辑，只覆写 OnPaint。对照 design
 *  main-panel.jsx 的 footerBtn 与底部主按钮样式。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonmain {

/**
 *  MainPanelIconButton —— 透明底图标按钮。悬停时显示浅灰圆角底。
 */
class MainPanelIconButton : public balloonwjui::DuiButton
{
public:
    MainPanelIconButton();

    // 设置图标类型，取值见 MainPanelIcons.h 的 IconKind。
    void SetIconKind(int kind);

    // DuiControl 覆写：绘制图标（+ 悬停底）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int m_iconKind;   // 图标类型（IconKind）
};

/**
 *  MainPanelActionButton —— 底部主操作按钮（白底圆角 + 边框 + 加号 + 文字）。
 *  文字经 SetText 设置（如"查找" / "建群"），加号图标由控件自绘。
 */
class MainPanelActionButton : public balloonwjui::DuiButton
{
public:
    MainPanelActionButton();

    // DuiControl 覆写：绘制圆角白底按钮 + 加号图标 + 文字。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }
};

} // namespace balloonmain
