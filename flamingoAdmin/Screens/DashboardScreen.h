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

    // 统计接口加载失败：3 个统计卡显示「—」，副标题写「加载失败」，不保留上一次的数值。
    void SetStatsFailed();

    // 仪表板上按接口分别加载的四张卡片。
    enum class Card
    {
        Trend,           // 「近 14 天消息量」柱状图（dashboard/messagetrend.do）
        Enterprises,     // 「活跃企业」列表（enterprise/list.do）
        Announcements,   // 「最新公告」列表（announcement/list.do）
        Audit,           // 「近期审计」列表（audit/list.do）
    };

    // 卡片的加载状态。
    enum class CardState
    {
        Loading,   // 请求已发出、应答未到：卡片内容区显示「加载中…」
        Loaded,    // 已加载：显示数据，没有数据时显示「暂无数据」
        Failed,    // 加载失败：显示「加载失败」，不显示缓存里的旧数据
    };

    // 设置某张卡片的加载状态并重绘。
    //   card：哪张卡片；state：新状态。
    void SetCardState(Card card, CardState state);

    // 取某张卡片当前的加载状态（供单元测试核对）。
    CardState GetCardState(Card card) const;

    // 按当前时间与管理员姓名设置页头问候语，如「下午好，张三」。进入主界面时调用。
    //   adminName：当前登录管理员的姓名，可为空。
    void SetGreeting(const CString& adminName);

    // 页头问候语当前的文字（供单元测试核对）。
    CString GetGreeting() const { return m_greeting; }

    // 按登录者角色切换显示内容，进入主界面时由 AdminFrame 调用（换账号登录时会再调一次）。
    //   platformAdmin：true 为平台超管，显示全部四张卡片，页头为「平台总览」；
    //                  false 为企业管理员，隐藏「活跃企业」「最新公告」两张卡（企业列表与
    //                  公告中心只对平台超管开放）与「企业总数」统计卡（只管辖一家企业，
    //                  这个数字没有意义），页头改为「企业总览」。
    // 副作用：改变上述卡片的可见性，并对它们所在的行或列重新布局。
    void SetPlatformAdminMode(bool platformAdmin);

    // 当前是否按平台超管显示（AdminFrame 据此决定是否请求那两张卡片的数据）。
    bool IsPlatformAdminMode() const { return m_platformAdmin; }

    // 某张卡片当前是否显示（供单元测试核对）。
    bool IsCardVisible(Card card) const;

    // 「企业总数」统计卡当前是否显示（供单元测试核对）。
    bool IsEnterpriseTileVisible() const;

private:
    // 把 3 张统计卡置为加载中占位（构造时调用）。
    void SetStatsLoading();

    // 按当前角色与问候语刷新页头三行文字。
    void UpdateHeaderTexts();

    // 设置一个子控件的可见性，并对它所在的行或列重新布局（SetVisible 本身不触发布局）。
    //   ctrl：卡片或统计卡（借用指针，可为空，为空时不做任何事）；visible：是否显示。
    static void SetVisibleAndRelayout(balloonwjui::DuiControl* ctrl, bool visible);

    // 3 个统计卡（借用指针，所有权在控件树中）。
    ui::StatTile* m_tileEnterprises = nullptr;   // 企业总数
    ui::StatTile* m_tileUsers       = nullptr;   // 总用户
    ui::StatTile* m_tileAudit       = nullptr;   // 今日审计

    ui::PageHeader*          m_header    = nullptr;  // 页头（借用指针），问候语写在它的标题行
    CString                  m_greeting;             // 当前问候语，SetGreeting 时更新
    bool                     m_platformAdmin = true; // 是否按平台超管显示，由 SetPlatformAdminMode 设置
    // 四张卡片（借用指针，按 Card 的取值下标存放；类型是 .cpp 里的卡片基类，这里以基类指针保存）。
    balloonwjui::DuiControl* m_cards[4]  = { nullptr, nullptr, nullptr, nullptr };
};

} // namespace flamingoadmin
