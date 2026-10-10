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
#include "Mock/MockData.h"   // AuditLog：滚动提示展示的审计记录

#include <vector>

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
    //   返回：true 表示刚好转完一整圈，调用方可借此时机重新拉取最近的审计记录。
    bool AdvanceTicker();

    // 设置滚动提示展示的审计记录（最新在前，取前几条循环展示），并从第一条重新开始。
    // 数据由 AdminFrame 单独拉取，不与审计页的翻页、筛选结果共用，所以展示的总是最新几条。
    //   logs：审计记录，按值复制保存；为空时显示「暂无审计」。
    void SetTickerAudits(const std::vector<AuditLog>& logs);

    // 刷新右上角账户区：第一行「姓名 · 角色」，第二行完整的登录账户。顶栏只在第一次登录时
    // 构建，换账号登录时要调用它，否则显示的还是上一位管理员。
    //   name：管理员姓名（t_admin.f_name）；email：登录邮箱，即登录账号（t_admin.f_email），原样显示；
    //   platformAdmin：true 为平台超管，false 为企业管理员。
    void SetAccount(const CString& name, const CString& email, bool platformAdmin);

    // 以下三个取值函数供单元测试核对显示内容。
    CString GetNameLine() const;         // 账户区第一行（姓名 · 角色）
    CString GetAccountLine() const;      // 账户区第二行（登录账户）
    CString GetTickerText() const;       // 滚动提示当前显示的文字

    // 拼出账户区第一行，形如「张三 · 企业管理员」「超级管理员 · 平台超管」；姓名为空时只有角色。
    //   name：管理员姓名；platformAdmin：true 为平台超管，false 为企业管理员。
    static CString MakeNameRoleLine(const CString& name, bool platformAdmin);

private:
    balloonwjui::DuiLabel* m_breadcrumb  = nullptr;  // 面包屑文字标签（借用指针）
    AuditTicker*           m_ticker      = nullptr;  // 审计滚动提示（借用指针）
    balloonwjui::DuiLabel* m_nameLine    = nullptr;  // 账户区第一行：姓名 · 角色（借用指针）
    balloonwjui::DuiLabel* m_accountLine = nullptr;  // 账户区第二行：完整的登录账户（借用指针）
};

} // namespace flamingoadmin
