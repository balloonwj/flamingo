/**
 *  flamingoAdmin —— 外壳随登录者刷新的单元测试（GRT、TBR、TCK、SHL 系列）。
 *
 *  外壳（顶栏、侧栏）只在第一次登录时构建，早先换账号登录后右上角仍是上一位管理员、
 *  侧栏「客户端版本」的有无也不随角色变化；仪表板问候语写死为「晚上好，李正阳」；顶栏
 *  「最新审计」与审计页共用一份缓存，审计页翻页或筛选后显示的就不是最新。本文件覆盖：
 *    - GRT1：问候语按时段与姓名生成；
 *    - TBR1：顶栏 SetAccount 后第一行为「姓名 · 角色」、第二行为完整的登录账户（2026-10-04 起
 *            不再把邮箱截成 @ 前面那段）；
 *    - TCK1：滚动提示用单独的数据，为空时显示「暂无审计」，转完一圈时报告给调用方；
 *    - SHL1：侧栏按角色显隐与命名导航项（企业管理员「本企业」、隐藏公告中心与客户端版本）；「共享审计」并入文件共享页后
 *            侧栏不再有这一项（2026-10-08）。
 *
 *  顶栏与侧栏在测试里只构造、不挂到宿主窗口，也不绘制。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Shell/TopBar.h"
#include "Shell/Sidebar.h"
#include "Widgets/UiHelpers.h"
#include "UiIds.h"

#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// 构造一条审计记录。
//   actor：操作者；detail：说明。
AuditLog MakeAudit(LPCTSTR actor, LPCTSTR detail)
{
    AuditLog l;
    l.m_actor  = actor;
    l.m_detail = detail;
    return l;
}

// GRT1：问候语的时段边界。
void TestGreetingForHour()
{
    BeginCase("GRT1 问候语按时段生成，有姓名时拼上姓名");

    FA_EXPECT_STREQ(ui::GreetingForHour(4, CString()), _T("晚上好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(5, _T("张三")), _T("早上好，张三"));
    FA_EXPECT_STREQ(ui::GreetingForHour(10, CString()), _T("早上好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(11, CString()), _T("中午好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(12, CString()), _T("中午好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(13, _T("李四")), _T("下午好，李四"));
    FA_EXPECT_STREQ(ui::GreetingForHour(17, CString()), _T("下午好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(18, CString()), _T("晚上好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(23, CString()), _T("晚上好"));
    // 超出 0～23 的值按「晚上好」。
    FA_EXPECT_STREQ(ui::GreetingForHour(-1, CString()), _T("晚上好"));
    FA_EXPECT_STREQ(ui::GreetingForHour(24, CString()), _T("晚上好"));
}

// TBR1：顶栏账户区可刷新。
void TestTopBarSetAccount()
{
    BeginCase("TBR1 顶栏 SetAccount 后第一行为「姓名 · 角色」，第二行为完整的登录账户");

    TopBar bar;
    bar.SetAccount(_T("张三"), _T("zhang@corp.com"), false);
    FA_EXPECT_STREQ(bar.GetNameLine(), _T("张三 · 企业管理员"));
    FA_EXPECT_STREQ(bar.GetAccountLine(), _T("zhang@corp.com"));

    // 换一个平台超管登录：两行都换掉，不残留上一位。
    bar.SetAccount(_T("超级管理员"), _T("admin@flamingo.com"), true);
    FA_EXPECT_STREQ(bar.GetNameLine(), _T("超级管理员 · 平台超管"));
    FA_EXPECT_STREQ(bar.GetAccountLine(), _T("admin@flamingo.com"));

    // 姓名为空时只显示角色，不留下孤零零的分隔点。
    FA_EXPECT_STREQ(TopBar::MakeNameRoleLine(CString(), true), _T("平台超管"));
    FA_EXPECT_STREQ(TopBar::MakeNameRoleLine(_T("李四"), false), _T("李四 · 企业管理员"));
}

// TCK1：滚动提示用单独的数据。
void TestTicker()
{
    BeginCase("TCK1 滚动提示：为空时显示「暂无审计」，转完一圈时报告给调用方");

    TopBar bar;
    bar.SetTickerAudits(std::vector<AuditLog>());
    FA_EXPECT_STREQ(bar.GetTickerText(), _T("最新审计 · 暂无审计"));
    // 没有数据时不报告「转完一圈」，免得调用方每一拍都去拉一次。
    FA_EXPECT_FALSE(bar.AdvanceTicker());

    std::vector<AuditLog> logs;
    logs.push_back(MakeAudit(_T("超级管理员"), _T("发布公告")));
    logs.push_back(MakeAudit(_T("联调管理员"), _T("新增员工")));
    bar.SetTickerAudits(logs);
    FA_EXPECT_STREQ(bar.GetTickerText(), _T("最新审计 · 超级管理员  发布公告"));
    FA_EXPECT_FALSE(bar.AdvanceTicker());
    FA_EXPECT_STREQ(bar.GetTickerText(), _T("最新审计 · 联调管理员  新增员工"));
    FA_EXPECT_TRUE(bar.AdvanceTicker());   // 回到第一条：转完一圈
    FA_EXPECT_STREQ(bar.GetTickerText(), _T("最新审计 · 超级管理员  发布公告"));

    // 换一批数据：从第一条重新开始。
    bar.AdvanceTicker();
    std::vector<AuditLog> fresh;
    fresh.push_back(MakeAudit(_T("甲"), _T("登录")));
    bar.SetTickerAudits(fresh);
    FA_EXPECT_STREQ(bar.GetTickerText(), _T("最新审计 · 甲  登录"));
}

// SHL1：侧栏按角色显隐与命名。
void TestSidebarApplyRole()
{
    BeginCase("SHL1 侧栏按角色显隐与命名导航项");

    Sidebar sb;
    sb.ApplyRole(true);
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavClientVersions));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavAnnouncements));
    FA_EXPECT_STREQ(sb.GetNavLabel(kIdNavEnterprises), _T("企业管理"));

    // 企业管理员：公告中心与客户端版本隐藏，「企业管理」改名「本企业」，其余照常。
    sb.ApplyRole(false);
    FA_EXPECT_FALSE(sb.IsNavVisible(kIdNavClientVersions));
    FA_EXPECT_FALSE(sb.IsNavVisible(kIdNavAnnouncements));
    FA_EXPECT_STREQ(sb.GetNavLabel(kIdNavEnterprises), _T("本企业"));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavEnterprises));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavDashboard));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavAudit));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavSensitiveWords));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavFileShares));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavSettings));
    // 「共享审计」2026-10-08 起是文件共享页的第二个页签，侧栏里不再有这一项（编号保留、不建导航项）
    FA_EXPECT_FALSE(sb.IsNavVisible(kIdNavFileShareAudit));
    FA_EXPECT_TRUE(sb.GetNavLabel(kIdNavFileShareAudit).IsEmpty());

    // 再换回平台超管：全部恢复（侧栏只建一次，换账号时靠它刷新）。
    sb.ApplyRole(true);
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavClientVersions));
    FA_EXPECT_TRUE(sb.IsNavVisible(kIdNavAnnouncements));
    FA_EXPECT_STREQ(sb.GetNavLabel(kIdNavEnterprises), _T("企业管理"));

    // 不存在的导航 ID。
    FA_EXPECT_FALSE(sb.IsNavVisible(-1));
    FA_EXPECT_TRUE(sb.GetNavLabel(-1).IsEmpty());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllShellSessionTests()
{
    TestGreetingForHour();
    TestTopBarSetAccount();
    TestTicker();
    TestSidebarApplyRole();
}

}} // namespace flamingoadmin::test
