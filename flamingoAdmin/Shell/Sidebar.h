/**
 *  flamingoAdmin —— 侧边栏。
 *
 *  主界面外壳的左栏：品牌头部 + 5 个导航项 + 弹性空白 + 用户信息与登出。
 *  导航项被点击时经 WM_DUI_NOTIFY 上报（ctrlId 为对应的 kIdNav* 值）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Widgets/Widgets.h"

namespace flamingoadmin {

class SidebarNavItem;   // 导航项控件，定义在 Sidebar.cpp

// 侧边栏控件。白底、右侧 1px 分隔边框。
class Sidebar : public ui::SolidPanel<balloonwjui::DuiVBox>
{
public:
    // 构造时一次性建好头部 / 导航项 / 页脚全部子控件。
    Sidebar();

    // 高亮某个导航项，其余取消高亮。
    //   navId：kIdNav* 之一。
    void SetActiveNav(int navId);

    // 切换紧凑 / 标准态。紧凑态（64px）下导航项只画图标、隐藏文字，
    // 头部只留徽标、页脚只留登出按钮。调用方负责同步外层 HBox 的宽度。
    //   collapsed：true 紧凑态，false 标准态。
    void SetCollapsed(bool collapsed);

private:
    // 构建品牌头部（徽标 + 名称两行）。
    std::unique_ptr<balloonwjui::DuiControl> BuildHeader();

    // 构建页脚（用户名 / 角色 + 登出按钮）。
    std::unique_ptr<balloonwjui::DuiControl> BuildFooter();

    // 创建一个导航项并登记到 m_navItems。
    //   navId：控件 ID（kIdNav*）；icon：图标；label：文字。
    std::unique_ptr<balloonwjui::DuiControl> MakeNavItem(int navId,
                                                         ui::Icon icon,
                                                         LPCTSTR label);

    SidebarNavItem* m_navItems[5] = {};   // 5 个导航项（借用指针，树持有所有权）
    int             m_navCount    = 0;    // 已登记的导航项数

    balloonwjui::DuiControl* m_headerTitles = nullptr; // 头部副标题 VBox（紧凑态隐藏）
    balloonwjui::DuiControl* m_footerInfo   = nullptr; // 页脚用户信息 VBox（紧凑态隐藏）
    bool                     m_collapsed    = false;   // 当前是否紧凑态
};

} // namespace flamingoadmin
