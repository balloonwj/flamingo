/**
 *  flamingoAdmin —— 服务号面板 ServicesPanel。
 *
 *  企业详情屏「服务号」子 tab 的内容：顶部 4 个统计卡 + 双栏主体（左栏
 *  服务号列表，右栏服务号详情）。详情含 4 个 tab：概览 / 绑定范围 /
 *  推送历史 / 设置。
 *
 *  本控件是【自绘控件，无子控件】，与 OrgDetailPanel 同构：左栏不滚动，
 *  右栏详情可滚轮滚动；所有可点区在 OnPaint 时记录、OnLButtonUp 命中。
 *  需要弹窗 / 写库的动作（新建 / 改绑定 / 停用 / 删除）经 WM_DUI_NOTIFY
 *  上报 AdminFrame（ctrlId = kIdServicesPanel）。
 *
 *  Phase 4b-① 实现：列表 + 详情 4 tab + 新建 + 停用 / 删除；绑定范围的
 *  编辑（BindingModal）留待 4b-②。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

#include <vector>

namespace balloonwjui {
class DuiBadge;        // 详情头的「状态」徽章（试点：DuiBadge + SetLeadingDot）
}

namespace flamingoadmin {

class OverlayScrollbar;   // 右栏详情区的悬浮 auto-hide 滚动条子控件

// 服务号面板。
class ServicesPanel : public balloonwjui::DuiControl
{
public:
    // 上报给 AdminFrame 的待处理动作。
    enum class Pending
    {
        None,
        New,           // 新建服务号
        ToggleStatus,  // 停用 / 重新启用 当前服务号
        Delete,        // 删除当前服务号
        EditBindings,  // 修改当前服务号的绑定范围
    };

    ServicesPanel();

    // 载入某企业的服务号（重置选中 / 滚动 / 筛选）。
    void SetEnterprise(const CString& enterpriseId);

    // 服务号增删改后重新取数、必要时修正选中项。
    void Refresh();

    CString GetEnterpriseId() const     { return m_entId; }
    Pending GetPending() const          { return m_pending; }
    CString GetSelectedServiceId() const { return m_selectedId; }

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 一个可点区。
    struct Hit
    {
        RECT    m_rc;
        int     m_kind;    // 0=选服务号 1=切筛选 2=切详情 tab 3=动作
        int     m_arg;     // kind=1:筛选下标 2:tab 下标 3:Pending
        CString m_svcId;   // kind=0 选中的服务号 ID
    };

    // 取当前选中的服务号（找不到返回 nullptr）。
    const Service* SelectedService() const;

    // 绘制左栏列表 / 右栏详情。
    void PaintList  (HDC hdc, const RECT& rc);
    void PaintDetail(HDC hdc, const RECT& rc);

    // 记一个动作并通知 AdminFrame。
    void RaisePending(Pending pending);

    void ClampScroll(const RECT& detailRc);

    // 右栏详情悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把新
    // 滚动偏移写回本面板并重绘。
    //   ctx：本 ServicesPanel 实例；newScroll：拖动产生的新滚动偏移（像素）。
    static void OnDetailSbScroll(void* ctx, int newScroll);

    CString           m_entId;
    const Enterprise* m_ent      = nullptr;
    CString           m_selectedId;             // 当前选中服务号 ID
    int               m_detailTab = 0;          // 详情 tab（0 概览…3 设置）
    int               m_listFilter = 0;         // 列表筛选（0 全部 1 活跃 2 草稿 3 停用）
    int               m_scrollY = 0;            // 右栏详情滚动偏移
    int               m_detailContentH = 0;     // 右栏内容总高
    RECT              m_detailRc{};             // 右栏矩形（OnMouseWheel 判定用）

    // 右栏详情悬浮 auto-hide 滚动条（子控件，见 Widgets/OverlayScrollbar）。
    // 生命周期由本控件的 m_children 持有，本指针仅作访问用。
    OverlayScrollbar* m_detailSb = nullptr;

    // 详情头的「状态」徽章（试点接入：DuiBadge + SetLeadingDot，把
    // 「● 运行中」「● 草稿」「已停用」状态指示从自绘 ui::DrawChip 切到
    // 库控件）。仅在选中服务号 + 该位置完整落在面板内时可见；其它情况
    // 隐藏，由 PaintDetail 自绘兜底。所有权归 m_children。
    balloonwjui::DuiBadge* m_statusBadge = nullptr;

    Pending           m_pending = Pending::None;
    std::vector<Hit>  m_hits;
};

} // namespace flamingoadmin
