/**
 *  GroupSideTab.h
 *
 *  群聊窗右侧边栏的 Tab 按钮 —— 成员 / 群文件 / 公告 / 备忘。下划线选中态：
 *  选中时文字用主题色、底部一条 2px 主题色横线。继承 DuiButton 复用
 *  点击 / 通知逻辑，只覆写 OnPaint。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

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

    // 设置 / 读取角标数：大于 0 时在文字右上方画一个红底白字的小圆角块（「备忘」页签显示本次登录后
    // 新到、还没点开过页签的群备忘条数）；0 不画。超过 99 显示「99+」。
    void SetBadgeCount(int count);
    int  GetBadgeCount() const { return m_badgeCount; }

    // DuiControl 覆写：绘制 Tab 文字 + 选中下划线。文字超出 Tab 宽度时以省略号结尾，不画到相邻的 Tab 上。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    /**
     *  按 Tab 的字号与选中时的字重量出一段文字的宽度（多语言第 4 期：非中文界面按文字宽度分配各 Tab 的宽度）。
     *  @param text Tab 文字；为 NULL 或空串时返回 0
     *  @return 文字宽度，单位像素
     */
    static int MeasureTabTextWidth(LPCWSTR text);

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    bool m_active;       // 是否当前选中
    int  m_badgeCount;   // 角标数；0 不画
};

} // namespace balloonchatwnd
