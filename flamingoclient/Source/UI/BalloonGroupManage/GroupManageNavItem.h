/**
 *  GroupManageNavItem.h
 *
 *  群管理对话框左侧导航的一项 —— 引导图标 + 文字 + 右侧计数徽章 + 选中
 *  高亮底。对照 design/群管理1.png 左栏。继承 DuiButton 复用点击 /
 *  DUIN_CLICK 通知 / hover 状态，只覆写 OnPaint。
 *
 *  「危险区域」一项文字常红（danger=true）；选中时高亮底也用淡红。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloongroupmanage {

/**
 *  GroupManageNavItem —— 左侧导航项（自绘）。
 */
class GroupManageNavItem : public balloonwjui::DuiButton
{
public:
    GroupManageNavItem();

    // 配置图标 / 文字 / 是否危险项。
    //   iconKind：引导图标种类，取值见 GmIconKind；
    //   label：   显示文字；
    //   danger：  true = 危险项（文字常红、选中高亮用淡红）。
    void SetNavInfo(int iconKind, LPCTSTR label, bool danger);

    // 设置右侧计数徽章。count <= 0 时不显示徽章。
    //   count：计数值；
    //   alert：true = 红色徽章（需关注，如待处理申请数）；
    //          false = 灰色徽章（一般计数，如成员数）。
    void SetBadge(int count, bool alert);

    // 读取右侧计数徽章的数值（即最近一次 SetBadge 传入的 count）。
    // 返回：计数值；<= 0 表示不显示徽章。供单元测试核对导航角标。
    int GetBadgeCount() const { return m_count; }

    // 设置 / 读取选中态（选中：高亮底 + 文字加粗）。
    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // DuiControl 覆写：绘制高亮底 + 图标 + 文字 + 计数徽章。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    int     m_iconKind;    // 引导图标种类，取值见 GmIconKind
    CString m_label;       // 显示文字
    bool    m_danger;      // 是否危险项（文字常红）
    int     m_count;       // 计数徽章数值（<= 0 不显示）
    bool    m_alert;       // 徽章是否红色（true 红 / false 灰）
    bool    m_active;      // 是否当前选中
};

} // namespace balloongroupmanage
