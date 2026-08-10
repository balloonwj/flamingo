/**
 *  flamingoAdmin —— 企业管理界面 EnterprisesScreen。
 *
 *  对应设计稿「03 企业管理 / 列表」：页头（含导出 / 新增按钮）+ 5 个统计卡
 *  + 筛选行（搜索框 + 状态分段 + 更多筛选 / 刷新）+ 企业表格。
 *
 *  数据来源：本界面通过 HttpWorker 异步向 chatserver 的 /admin/enterprise/
 *  list.do 拉取企业列表（服务端分页 + 服务端筛选），结果经 AdminFrame 的
 *  WM_APP_API_RESULT 回传到本类的 OnApiResult。m_enterprises 缓存的是
 *  「当前页」那一页企业；搜索词 / 状态分段作为请求参数下推服务端。表格用
 *  DuiVirtualList，行内容由「画一行」回调按需从 m_enterprises 取（PaintRow
 *  必须同步、极快，故不能直接联网）。顶部 5 个统计卡由 dashboard/stats.do
 *  单独喂（分页后本页数据不足以算总数）。
 *
 *  本界面内控件的通知（搜索框文字变化、分段切换、行点击）由 AdminFrame
 *  统一接收后转发到本类的 OnXxx 方法。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Controls/List/DuiListBox.h"
#include "Widgets/CommonWidgets.h"
#include "Util/AdminApi.h"

#include <vector>

namespace balloonwjui { class DuiEditHost; }

namespace flamingoadmin {

// 企业管理界面。整体是一个纵向布局：页头 + 统计卡行 + 筛选行 + 表格卡。
class EnterprisesScreen : public balloonwjui::DuiVBox
{
public:
    // 构造时一次性建好整棵控件树。
    EnterprisesScreen();

    // 注入主窗口句柄。AdminFrame 在本界面构建后调用一次；联网请求的结果
    // 经此 HWND 的 WM_APP_API_RESULT 回传。
    //   hwnd：主窗口句柄。
    void SetHostWindow(HWND hwnd) { m_hostWnd = hwnd; }

    // 重新向服务端拉取企业列表。进入本界面前、点刷新、写操作成功后调用。
    void Refresh();

    // 接收 AdminFrame 派发来的异步 API 结果。
    //   tag：结果标签，标明是哪个接口的应答。
    //   http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 搜索框文字变化时由 AdminFrame 转发到此（在本地缓存上重做筛选）。
    void OnSearchChanged();

    // 状态分段筛选切换时由 AdminFrame 转发到此。
    //   segIndex：0 全部 / 1 活跃 / 2 待审核 / 3 已停用。
    void OnSegmentChanged(int segIndex);

    // 翻页。由表尾分页按钮调用。
    //   delta：+1 下一页 / -1 上一页（越界自动夹取）。
    void TurnPage(int delta);

    // 取上一次被点击的表格行对应的企业 ID。
    CString GetClickedEnterpriseId() const { return m_clickedEnterpriseId; }

    // 取上一次被命中的表格行对应的企业记录（AdminFrame 据此弹行操作菜单）。
    //   返回：命中返回指向内部缓存的指针（下一次拉取前有效）；未命中返回 nullptr。
    const Enterprise* GetClickedEnterprise() const;

    // 命中测试：判断 host-客户区坐标点是否落在某企业行的「操作」列
    //（即三个点所在的最后一列）上。
    //   hostClientPt：host-客户区坐标（即主窗口客户区坐标）。
    //   返回：命中返回 true，并把该行企业 ID 记入内部（供 GetClicked* 取用）；
    //         未命中返回 false。AdminFrame 在 WM_LBUTTONDOWN 里用它判断是否
    //         点中了三个点、需弹出行操作菜单。
    bool HitTestActionDotsAt(POINT hostClientPt);

    // 取第 rowIndex 个表格数据行对应的企业 ID。表格行索引即筛选后列表的
    // 下标。AdminFrame 收到行双击通知（DUIN_DBLCLK，extra 为行索引）后据
    // 此解析出企业 ID。
    //   rowIndex：表格行索引。
    //   返回：命中返回企业 ID；越界返回空串。
    CString EnterpriseIdByRow(int rowIndex) const;

private:
    // 数据加载状态。
    enum class LoadState
    {
        Idle,     // 尚未发起加载
        Loading,  // 请求已发出，等待应答
        Loaded,   // 已成功载入
        Failed,   // 加载失败
    };

    // 发起一次企业列表拉取（list.do）。
    void StartLoad();

    // 处理企业列表应答：解析、填本页缓存、刷新表格与分页表尾。
    void OnListResult(const HttpResult& http);

    // 处理 dashboard/stats.do 应答：填顶部 5 个统计卡。
    void OnStatsResult(const HttpResult& http);

    // 按当前加载状态更新表格中央的占位文字（加载中 / 失败 / 空）。
    void UpdateTableState();

    // DuiVirtualList 的「画一行」回调（静态转发到成员函数）。
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex,
                              const RECT& rowRect, bool selected, bool hover);
    void        PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover);

    // ---- 子控件借用指针（所有权均在控件树中）----
    ui::StatTile*               m_tileTotal   = nullptr;  // 企业总数
    ui::StatTile*               m_tileActive  = nullptr;  // 活跃企业
    ui::StatTile*               m_tilePending = nullptr;  // 待审核
    ui::StatTile*               m_tileUsers   = nullptr;  // 总用户数
    ui::StatTile*               m_tileAudit   = nullptr;  // 今日审计事件
    balloonwjui::DuiEditHost*   m_search      = nullptr;  // 搜索输入框
    ui::SegControl*             m_seg         = nullptr;  // 状态分段筛选
    balloonwjui::DuiVirtualList* m_table      = nullptr;  // 企业表格（实际是 EnterpriseTable）
    balloonwjui::DuiControl*    m_footer      = nullptr;  // 表尾（显示计数）

    // ---- 数据 ----
    std::vector<Enterprise> m_enterprises;                  // 当前页的企业缓存
    LoadState               m_loadState = LoadState::Idle;  // 当前加载状态
    CString                 m_errorText;                    // 加载失败的中文原因
    HWND                    m_hostWnd   = nullptr;          // 主窗口句柄（结果回传用）

    int m_page       = 1;   // 当前页码（从 1 起）
    int m_totalCount = 0;   // 当前筛选下的企业总数（服务端报告）

    // 上一次被点击的行对应的企业 ID。
    CString m_clickedEnterpriseId;
};

} // namespace flamingoadmin
