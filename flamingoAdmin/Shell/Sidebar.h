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

    // 按当前登录者的角色调整导航项。侧栏只在第一次登录时构建，换账号登录时由 AdminFrame
    // 每次进入主界面都调用一次：
    //   · 平台超管：全部导航项可见，「企业管理」叫「企业管理」；
    //   · 企业管理员：「公告中心」「客户端版本」隐藏（相关接口只给平台超管），
    //     「企业管理」改名为「本企业」（点击直接打开本企业详情）。
    //   platformAdmin：true 为平台超管，false 为企业管理员。
    void ApplyRole(bool platformAdmin);

    // 以下两个取值函数供单元测试核对导航项的显隐与名称；navId 不存在时分别返回 false 与空串。
    bool    IsNavVisible(int navId) const;
    CString GetNavLabel(int navId) const;

private:
    // 构建品牌头部（徽标 + 名称两行）。
    std::unique_ptr<balloonwjui::DuiControl> BuildHeader();

    // 创建一个导航项并登记到 m_navItems。
    //   navId：控件 ID（kIdNav*）；icon：图标；label：文字。
    std::unique_ptr<balloonwjui::DuiControl> MakeNavItem(int navId,
                                                         ui::Icon icon,
                                                         LPCTSTR label);

    // 导航项（借用指针，控件树持有所有权）。
    // 用 vector 而非定长数组：导航项数量不再是常量
    // ——「客户端版本」仅平台超管可见，两类管理员看到的项数不同。
    // 此处曾是写死 5 的数组，而 .cpp 里的上限常量被改成 6，
    // 结果第 6 项越界写到紧跟其后的计数器上，平台超管登录即崩。
    std::vector<SidebarNavItem*> m_navItems;

    // 按 navId 找导航项，找不到返回 nullptr。
    SidebarNavItem* FindNavItem(int navId) const;

    balloonwjui::DuiControl* m_nav          = nullptr; // 导航区 VBox（借用；导航项显隐变化后对它 ForceLayout）
    balloonwjui::DuiControl* m_headerTitles = nullptr; // 头部副标题 VBox（紧凑态隐藏）
    bool                     m_collapsed    = false;   // 当前是否紧凑态
};

} // namespace flamingoadmin
