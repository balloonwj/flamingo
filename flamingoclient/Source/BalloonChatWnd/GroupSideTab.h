/**
 *  GroupSideTab.h
 *
 *  群聊窗右侧边栏的 Tab 按钮 —— 成员 / 群文件 / 公告。下划线选中态：
 *  选中时文字用主题色、底部一条 2px 主题色横线。继承 DuiButton 复用
 *  点击 / 通知逻辑，只覆写 OnPaint。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonchatwnd {

/**
 *  GroupSideTab —— 边栏下划线式 Tab。
 */
class GroupSideTab : public balloonwjui::DuiButton
{
public:
    GroupSideTab();

    // 设置 / 读取选中态。
    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // DuiControl 覆写：绘制 Tab 文字 + 选中下划线。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    bool m_active;   // 是否当前选中
};

} // namespace balloonchatwnd
