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

namespace balloonwjui { class DuiEdit; class DuiComboBox; class DuiButton; }

namespace flamingoadmin {

namespace ui { class PagerBar; }

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

    // 回到初始状态：清空本页缓存与统计、加载状态回到「尚未加载」、页码回到第 1 页。登出与
    // 会话失效时调用 —— 否则请求在途时失效，加载状态会一直停在「加载中」，重新登录后
    // Refresh 因「已有请求在途」被跳过，列表永远加载不出来。
    void ResetState();

    // 接收 AdminFrame 派发来的异步 API 结果。
    //   tag：结果标签，标明是哪个接口的应答。
    //   http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 搜索框文字变化时由 AdminFrame 转发到此（在本地缓存上重做筛选）。
    void OnSearchChanged();

    // 状态分段筛选切换时由 AdminFrame 转发到此。
    //   segIndex：0 全部 / 1 活跃 / 2 待审核 / 3 已停用。
    void OnSegmentChanged(int segIndex);

    // 展开或收起「更多筛选」条（筛选行下面那一行）。由 AdminFrame 在「更多筛选」按钮点击时调用。
    // 副作用：改变筛选条的可见性，并对本界面重新布局（SetVisible 本身不触发布局）。
    void ToggleMoreFilters();

    // 「更多筛选」条当前是否展开。
    bool IsMoreFiltersVisible() const { return m_moreVisible; }

    // 「更多筛选」里某个条件变化时由 AdminFrame 转发到此（行业、套餐下拉框的选择，起止日期
    // 输入框的文字）。起止日期只有为空或格式完整正确时才生效，输入到一半时不重新拉取；条件
    // 可用时回到第 1 页重新拉取。无论是否拉取，都会刷新「更多筛选」按钮上的条件数。
    void OnMoreFilterChanged();

    // 清除「更多筛选」里的全部条件（「清除」按钮），回到第 1 页重新拉取。
    void ClearMoreFilters();

    // 取当前完整的筛选条件：搜索框、状态分段与「更多筛选」。列表请求与「导出 CSV」都用它；
    // 格式不对的日期不放进返回值。
    EnterpriseListFilter GetFilter() const;

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
    //         未命中返回 false，点落在表格的垂直滚动条上时也返回 false。AdminFrame
    //         在 WM_LBUTTONDOWN 里用它判断是否点中了三个点、需弹出行操作菜单。
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

    // 按已设置的条件数刷新「更多筛选」按钮文字（「更多筛选」或「更多筛选 · N」）。
    void UpdateMoreButtonText();

    // 起止日期输入框是否都可用：为空或格式正确。输入到一半时为 false。
    bool AreDateInputsUsable() const;

    // 不发通知地清空「更多筛选」的四项条件（下拉框回到「不限」、日期框清空）。
    void ResetMoreFilterControls();

    // 分页条（ui::PagerBar）的翻页回调：静态转发到 TurnPage。
    //   user：本界面指针。newPage：目标页码，从 1 起。
    static void OnPageThunk(void* user, int newPage);

    // ---- 子控件借用指针（所有权均在控件树中）----
    ui::StatTile*               m_tileTotal   = nullptr;  // 企业总数
    ui::StatTile*               m_tileActive  = nullptr;  // 活跃企业
    ui::StatTile*               m_tilePending = nullptr;  // 待审核
    ui::StatTile*               m_tileUsers   = nullptr;  // 总用户数
    ui::StatTile*               m_tileAudit   = nullptr;  // 今日审计事件
    balloonwjui::DuiEdit*   m_search      = nullptr;  // 搜索输入框
    ui::SegControl*             m_seg         = nullptr;  // 状态分段筛选
    balloonwjui::DuiVirtualList* m_table      = nullptr;  // 企业表格（实际是 EnterpriseTable）
    ui::PagerBar*               m_footer      = nullptr;  // 表尾分页条（共 N 条 + 翻页）
    balloonwjui::DuiButton*     m_btnMore     = nullptr;  // 「更多筛选」按钮，文字带已设置的条件数
    balloonwjui::DuiControl*    m_moreRow     = nullptr;  // 「更多筛选」条整行，默认隐藏
    balloonwjui::DuiComboBox*   m_cbIndustry  = nullptr;  // 行业下拉框（第 0 项「不限行业」）
    balloonwjui::DuiComboBox*   m_cbPlan      = nullptr;  // 套餐下拉框（第 0 项「不限套餐」）
    balloonwjui::DuiEdit*       m_editOpenFrom = nullptr; // 开通起始日期输入框
    balloonwjui::DuiEdit*       m_editOpenTo  = nullptr;  // 开通截止日期输入框
    bool                        m_moreVisible = false;    // 「更多筛选」条是否展开

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
