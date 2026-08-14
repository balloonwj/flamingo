/**
 *  MainPanelTab.h
 *
 *  主面板顶部的图标 Tab 控件（消息 / 联系人 / 群）。对照 design 05/06
 *  与 main-panel.jsx 的 BigTab：图标居中、无文字标签；选中态橙底 +
 *  橙色图标 + 2px 橙色底边；"消息"Tab 带未读红色角标。
 *
 *  继承 DuiButton 以复用其点击 / 通知逻辑（Tab 切换在后续 P2 阶段接），
 *  只覆写 OnPaint。SetText 设的文字不绘制，仅作可访问标签 / 测试契约用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonmain {

/**
 *  MainPanelTab —— 主面板图标 Tab。
 */
class MainPanelTab : public balloonwjui::DuiButton
{
public:
    MainPanelTab();

    // 设置 Tab 图标类型。
    //   kind：取值见 MainPanelIcons.h 的 IconKind。
    void SetIconKind(int kind);

    // 设置 / 查询是否为当前选中 Tab。
    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // 设置未读角标数字。0 表示不显示角标。
    void SetBadgeCount(int count);

    // DuiControl 覆写：绘制图标 + 选中高亮 + 角标。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标 —— 不用 DuiButton 默认的手型（手型
    // 是超链接语义，主面板的 Tab / 列表行不该用）。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int  m_iconKind;     // 图标类型（IconKind）
    bool m_active;       // 是否当前选中 Tab
    int  m_badgeCount;   // 未读角标数字，0 = 不显示
};

} // namespace balloonmain
