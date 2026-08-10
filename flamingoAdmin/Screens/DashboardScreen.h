/**
 *  flamingoAdmin —— 仪表板界面 DashboardScreen。
 *
 *  对应设计稿「02 仪表板」：页头问候语 + 双列网格。
 *    · 左列：3 个统计卡 + 近 14 天消息量柱状图 + 活跃企业列表。
 *    · 右列：最新公告 + 近期审计。
 *  统计卡来自 dashboard/stats.do、柱状图来自 dashboard/messagetrend.do、
 *  活跃企业 / 公告 / 审计 各来自对应列表接口 —— 均经 AdminFrame 注入。
 *
 *  「活跃企业 / 最新公告 / 近期审计」三张卡片右上角的「查看全部」链接被
 *  点击时，经 WM_DUI_NOTIFY 上报对应 ctrlId（kIdDashView*），由 AdminFrame
 *  跳转到相应界面。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Widgets/CommonWidgets.h"
#include "Util/AdminApi.h"   // DashboardStatsResult

namespace flamingoadmin {

// 仪表板界面。整体是一个纵向布局：页头 + 双列内容区。
class DashboardScreen : public balloonwjui::DuiVBox
{
public:
    // 构造时一次性建好整棵控件树。
    DashboardScreen();

    // 用 dashboard/stats.do 的结果填充 3 个统计卡（企业总数 / 总用户 / 今日审计）。
    //   stats：仪表盘概览统计。
    void SetStats(const DashboardStatsResult& stats);

private:
    // 把 3 张统计卡置为加载中占位（构造时调用）。
    void SetStatsLoading();

    // 3 个统计卡（借用指针，所有权在控件树中）。
    ui::StatTile* m_tileEnterprises = nullptr;   // 企业总数
    ui::StatTile* m_tileUsers       = nullptr;   // 总用户
    ui::StatTile* m_tileAudit       = nullptr;   // 今日审计
};

} // namespace flamingoadmin
