/**
 *  flamingoAdmin —— 公告中心界面 AnnouncementsScreen。
 *
 *  对应设计稿「05 公告中心」：页头（导出 / 新建公告）+ 双栏主体（左栏
 *  公告列表 + 筛选，右栏公告预览）。预览含已读进度 / 定时队列信息与
 *  编辑 / 撤回等操作按钮。
 *
 *  本控件是【自绘容器控件】：页头 / 列表 / 预览均自绘，仅「导出」「新建
 *  公告」是子按钮控件。列表选中 / 筛选为内部状态；删除（撤回）等需要
 *  确认弹窗的动作经 WM_DUI_NOTIFY 上报 AdminFrame（ctrlId = kIdAnnScreen）。
 *
 *  数据取自 Mock/DataStore。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

#include <vector>

namespace balloonwjui { class DuiButton; }   // FlatButton 已迁到 balloonwjui::DuiButton

namespace flamingoadmin {

class OverlayScrollbar;             // 公告列表 / 预览区的悬浮滚动条子控件
class AnnouncementPreviewPanel;     // 右栏「公告详情」面板(取代原 PaintPreview 自绘)

// 公告中心界面。
class AnnouncementsScreen : public balloonwjui::DuiControl
{
public:
    // 上报给 AdminFrame 的待处理动作。
    enum class Pending
    {
        None,
        Delete,       // 撤回 / 删除当前公告
        Misc,         // 复制 / 重新推送（后续阶段开放，先弹 Toast）
        SelectDetail, // 选中了某公告，请求 AdminFrame 拉取其详情
        TogglePin,    // 切换当前公告的置顶状态
        Edit,         // 编辑当前公告（打开预填的新建公告弹窗）
    };

    AnnouncementsScreen();

    // 公告增删后重新取数、必要时修正选中项。
    void Refresh();

    Pending GetPending() const       { return m_pending; }
    CString GetSelectedId() const    { return m_selectedId; }

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    struct Hit
    {
        RECT    m_rc;
        int     m_kind;    // 0=选公告 1=切筛选 2=动作
        int     m_arg;     // kind=1:筛选下标 2:Pending
        CString m_annId;   // kind=0 选中的公告 ID
    };

    const Announcement* Selected() const;
    void PaintList   (HDC hdc, const RECT& rc);

    // 画左侧列表底部的分页条(上一页 / 第 X / Y 页 / 下一页),并登记翻页命中区。
    //   rc：分页条矩形;total：当前筛选下公告总数;pageSize：每页条数。
    void PaintPager  (HDC hdc, const RECT& rc, int total, int pageSize);

    // 收集当前筛选(m_filter)下的公告,按 DataStore 顺序。返回总数;out 非空时
    // 顺带填充指针(指向 DataStore vector 元素,仅在本次同步调用内有效)。
    int  CollectFiltered(std::vector<const Announcement*>* out) const;

    // 每页条数 = 列表项可视区高度 / 项高(至少 1)。可视区 = 列表栏去掉页头、
    // 筛选行与底部分页条后的剩余高度,从而每页正好铺满、无需滚动。
    int  ListPageSize() const;
    // 原 PaintPreview 已删除 —— 公告详情整块外包给 AnnouncementPreviewPanel
    // (Screens/AnnouncementPreviewPanel.h),本控件只负责定位 + 滚动偏移 +
    // 选公告时调 m_previewPanel->SetAnnouncement。
    void RaisePending(Pending pending);

    // 用 Selected() 当前结果同步到 m_previewPanel —— 选公告 / Refresh 调用。
    void SyncPreviewPanel();

    // 预览区悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把新滚动
    // 偏移写回本界面并触发重绘。
    //   ctx：本 AnnouncementsScreen 实例；newScroll：拖动产生的新滚动偏移。
    static void OnPreviewSbScroll(void* ctx, int newScroll);

    // 左侧公告列表悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把
    // 新滚动偏移写回本界面并触发重绘。
    //   ctx：本 AnnouncementsScreen 实例；newScroll：拖动产生的新滚动偏移。
    static void OnListSbScroll(void* ctx, int newScroll);

    balloonwjui::DuiButton* m_btnExport = nullptr;   // 导出
    balloonwjui::DuiButton* m_btnNew    = nullptr;   // 新建公告

    CString m_selectedId;
    int     m_filter = 0;            // 0 全部 / 1 置顶 / 2 重要 / 3 定时
    int     m_listPage = 0;          // 左侧列表当前页(0-based);超长列表底部分页
    // 滚轮翻页的两个钝化状态（见 OnMouseWheel）。
    // 列表是分页显示的，一格滚轮就跳整页，
    // 不加这两道闸会“一甸飞过三四页”。
    int     m_wheelAccum = 0;        // 累积的滚轮增量（未满一格的部分留到下次）
    DWORD   m_lastPageFlipTick = 0;  // 上次翻页的时刻（GetTickCount），用于冷却
    int     m_previewScroll = 0;     // 预览区当前竖向滚动偏移（像素）
    int     m_previewContentH = 0;   // 预览区内容总高（像素，PaintPreview 中算出）
    int     m_listScroll = 0;        // 左侧公告列表当前竖向滚动偏移（像素）
    int     m_listContentH = 0;      // 左侧公告列表所有项的总高（像素，PaintList 中算出）
    RECT    m_previewRc{};

    // 公告预览区的悬浮滚动条（auto-hide 悬浮式子控件，见 Widgets/OverlayScrollbar）。
    OverlayScrollbar* m_previewSb = nullptr;

    // 左侧公告列表的悬浮滚动条（auto-hide 悬浮式子控件，见 Widgets/OverlayScrollbar）。
    OverlayScrollbar* m_listSb = nullptr;

    // 右栏「公告详情」面板(子控件,所有权在 m_children)。Layout 时按 -m_previewScroll
    // 偏移到 m_previewRc 内;高度从 GetDesiredSize().cy 取,用于算滚动条 metrics。
    AnnouncementPreviewPanel* m_previewPanel = nullptr;

    Pending          m_pending = Pending::None;
    std::vector<Hit> m_hits;
};

} // namespace flamingoadmin
