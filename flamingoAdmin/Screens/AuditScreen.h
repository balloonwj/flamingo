/**
 *  flamingoAdmin —— 审计日志界面 AuditScreen。
 *
 *  对应设计稿「06 审计日志」：页头（刷新 / 导出）+ 4 个统计卡 + 筛选行
 *  （级别 / 操作者 分段）+ 双栏主体（左栏审计表 + 分页页脚，右栏事件详情）。
 *
 *  数据来自 chatserver：审计表走【服务端分页 + 服务端筛选】（audit/list.do），
 *  统计卡走 audit/stats.do。本控件只负责展示与交互 —— 切换筛选 / 翻页时把
 *  意图经 WM_DUI_NOTIFY 上报 AdminFrame，由 AdminFrame 发请求，再经
 *  SetPageData / SetStats 把结果回填本控件。
 *
 *  本控件是【自绘容器控件】：仅「刷新」「导出」是子按钮控件，表格 / 详情 /
 *  分页页脚全部自绘。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"

#include <vector>

namespace balloonwjui { class DuiButton; }   // FlatButton 已迁到 balloonwjui::DuiButton

namespace flamingoadmin {

namespace ui { class PagerBar; }

class OverlayScrollbar;   // 表格区 / 详情区的悬浮 auto-hide 滚动条子控件

// 审计日志界面。
class AuditScreen : public balloonwjui::DuiControl
{
public:
    AuditScreen();

    // AdminFrame 在 audit/list.do 应答后调用：传入服务端报告的（按当前筛选）
    // 总条数。本页数据本身已由 AdminFrame 注入 DataStore；本方法据 totalCount
    // 刷新分页页脚，并把选中行重置到新页首行、滚动复位。
    //   totalCount：当前筛选条件下的审计记录总数。
    void SetPageData(int totalCount);

    // AdminFrame 在 audit/stats.do 应答后调用：填充顶部 4 个统计卡。
    //   today / total / warn / danger：今日 / 累计 / 警告级 / 危险级 事件数。
    void SetStats(int today, int total, int warn, int danger);

    // 当前要加载的页码（从 1 起）。AdminFrame 据此构造 audit/list.do 请求。
    int GetPage() const { return m_page; }

    // 当前级别筛选下标：0 全部 / 1 普通 / 2 警告 / 3 危险。
    int GetSevFilter() const { return m_sevFilter; }

    // 当前操作者筛选下标：0 全部 / 1 管理员 / 2 系统。
    int GetActorFilter() const { return m_actorFilter; }

    // 每页条数。AdminFrame 构造分页请求时取用。
    int GetPageSize() const;

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 一个可点击命中区。
    struct Hit
    {
        RECT m_rc;     // 命中矩形（host 客户区坐标）
        int  m_kind;   // 0=选条目 1=切级别筛选 2=切操作者筛选（翻页由子控件 m_pager 处理）
        int  m_arg;    // kind=0：日志 id；kind=1/2：筛选下标
    };

    // 把「按当前页码 + 筛选重新加载」的意图上报 AdminFrame（并立即重绘以
    // 反映筛选高亮）。
    void RequestReload();

    void PaintTable (HDC hdc, const RECT& rc);
    void PaintDetail(HDC hdc, const RECT& rc);

    // 表格区悬浮滚动条拖动滑块时的回调（注册给 m_tableSb）：把新滚动偏移
    // 写回 m_tableScroll 并重绘本界面。
    //   ctx：本 AuditScreen 实例；newScroll：拖动产生的新滚动偏移（已夹取）。
    static void OnTableSbScroll(void* ctx, int newScroll);

    // 详情区悬浮滚动条拖动滑块时的回调（注册给 m_detailSb）：把新滚动偏移
    // 写回 m_detailScroll 并重绘本界面。
    //   ctx：本 AuditScreen 实例；newScroll：拖动产生的新滚动偏移（已夹取）。
    static void OnDetailSbScroll(void* ctx, int newScroll);

    balloonwjui::DuiButton* m_btnRefresh = nullptr;   // 「刷新」按钮（子控件）
    balloonwjui::DuiButton* m_btnExport  = nullptr;   // 「导出」按钮（子控件）

    // 分页条的翻页回调：记下目标页码并请求 AdminFrame 重新拉取（与原先点「上一页」
    // 「下一页」相同）。
    //   user：本界面指针；newPage：目标页码，从 1 起。
    static void OnPagerThunk(void* user, int newPage);

    // 表格区悬浮 auto-hide 滚动条（子控件，悬浮于表格行区右缘，见
    // Widgets/OverlayScrollbar）；生命周期由 m_children 持有，本指针仅作引用。
    OverlayScrollbar* m_tableSb  = nullptr;
    // 详情区悬浮 auto-hide 滚动条（子控件，悬浮于详情区右缘）；生命周期同上。
    OverlayScrollbar* m_detailSb = nullptr;

    int  m_selectedId   = -1;   // 当前选中日志 id（-1 = 无）
    int  m_sevFilter    = 0;    // 级别筛选：0 全部 / 1 普通 / 2 警告 / 3 危险
    int  m_actorFilter  = 0;    // 操作者筛选：0 全部 / 1 管理员 / 2 系统
    int  m_page         = 1;    // 当前页码（从 1 起）
    int  m_totalCount   = 0;    // 当前筛选下的审计记录总数（服务端报告）
    int  m_tableScroll  = 0;    // 表格区页内滚动偏移
    int  m_detailScroll = 0;    // 详情区滚动偏移
    int  m_tableContentH  = 0;  // 表格内容总高（供滚动夹取）
    int  m_detailContentH = 0;  // 详情内容总高

    bool m_statsLoaded  = false; // 统计卡是否已收到数据（未到时显示占位「—」）
    int  m_statToday    = 0;     // 统计卡：今日事件数
    int  m_statTotal    = 0;     // 统计卡：累计事件数
    int  m_statWarn     = 0;     // 统计卡：警告级事件数
    int  m_statDanger   = 0;     // 统计卡：危险级事件数

    RECT m_tableRc{};    // 表格区矩形（不含页脚）
    RECT m_detailRc{};   // 详情区矩形
    RECT m_footerRc{};   // 分页页脚矩形，共用分页条 m_pager 摆在这里

    // 表格下方的分页条（子控件，与企业列表、敏感词页共用 ui::PagerBar）；生命周期由
    // m_children 持有，本指针仅作引用。
    ui::PagerBar* m_pager = nullptr;

    std::vector<Hit> m_hits;   // 本帧的命中区集合
};

} // namespace flamingoadmin
