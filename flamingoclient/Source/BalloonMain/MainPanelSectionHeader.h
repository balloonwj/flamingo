/**
 *  MainPanelSectionHeader.h
 *
 *  联系人页里的可折叠分组标题行（"我的好友""服务号"）。对照 design
 *  main-panel.jsx 的 OrgNode 标题：展开三角 + 分组名 + 右侧"在线/总数"。
 *
 *  P1b 阶段为纯绘制（恒展开态，折叠交互留待 P2）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonmain {

/**
 *  MainPanelSectionHeader —— 联系人分组标题行。继承 DuiButton 以复用
 *  点击 / 通知逻辑：点击标题即展开 / 收起该分组（由对话框接收 DUIN_CLICK
 *  后切换 m_expanded 并显隐分组内的列表项）。
 */
class MainPanelSectionHeader : public balloonwjui::DuiButton
{
public:
    MainPanelSectionHeader();

    // 设置分组名。
    void SetTitle(LPCTSTR title);

    // 设置右侧"在线 / 总数"计数。
    void SetCount(int online, int total);

    // 设置 / 查询展开态（true=展开,三角朝下;false=收起,三角朝右）。
    void SetExpanded(bool expanded);
    bool IsExpanded() const { return m_expanded; }

    // DuiControl 覆写：绘制展开三角 + 分组名 + 右侧计数。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：把滚轮转发给祖先里的滚动视口。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // DuiControl 覆写：分组标题用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    CString m_title;     // 分组名
    int     m_online;    // 在线人数
    int     m_total;     // 总人数
    bool    m_expanded;  // 是否展开
};

} // namespace balloonmain
