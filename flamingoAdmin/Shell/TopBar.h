/**
 *  flamingoAdmin —— 顶栏。
 *
 *  主界面外壳的顶部条：折叠侧栏按钮 + 面包屑（当前界面名）+ 审计滚动提示 +
 *  登出按钮。
 *
 *  2026-08-12 起本栏不再有任何输入框控件 —— 原先的全局搜索框与通知
 *  铃铛已移除，登出按钮从侧边栏页脚移到这里（右上角是找"退出登录"的习惯位置）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Widgets/Widgets.h"
#include "Controls/Basic/DuiLabel.h"

namespace flamingoadmin {

class AuditTicker;   // 审计滚动提示控件，定义在 TopBar.cpp

// 顶栏控件。白底、底部 1px 分隔边框。
// 左起：折叠侧栏按钮 + 面包屑 + 审计滚动提示（占满中部剩余宽度）+ 登出按钮。
class TopBar : public ui::SolidHBox
{
public:
    // 构造时建好折叠按钮 / 面包屑 / 审计提示 / 登出按钮全部子控件。
    TopBar();

    // 设置面包屑文字（当前界面名）。
    void SetBreadcrumb(LPCTSTR text);

    // 审计滚动提示前进到下一条（由 AdminFrame 的定时器周期性驱动）。
    void AdvanceTicker();

private:
    balloonwjui::DuiLabel* m_breadcrumb = nullptr;   // 面包屑文字标签（借用指针）
    AuditTicker*           m_ticker     = nullptr;   // 审计滚动提示（借用指针）
};

} // namespace flamingoadmin
