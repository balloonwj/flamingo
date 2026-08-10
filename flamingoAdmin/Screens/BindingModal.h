/**
 *  flamingoAdmin —— 服务号绑定范围弹窗 BindingModal。
 *
 *  企业详情屏「服务号 → 绑定范围」tab 点击「修改绑定」时弹出。让管理员
 *  增删服务号的绑定对象：整个企业 / 按部门 / 按员工三种粒度。底部实时
 *  显示已选绑定与触达人数。保存后由 AdminFrame 写入 DataStore。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中卡片，常驻于 RootContainer 弹窗
 *  层，开 / 关只切显隐；点遮罩不关闭。本弹窗【无 EDIT 子控件】，部门 /
 *  员工列表为自绘可滚动列表。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

#include <vector>

namespace flamingoadmin {

class OverlayScrollbar;   // 部门 / 员工列表的悬浮滚动条子控件

// 服务号绑定范围弹窗。
class BindingModal : public balloonwjui::DuiControl
{
public:
    BindingModal();

    // 配置弹窗：要编辑的服务号名、所属企业、当前绑定列表。每次打开前调用。
    void Configure(const CString& serviceName, const CString& enterpriseId,
                   const std::vector<ServiceBinding>& bindings);

    // 取编辑后的绑定列表（AdminFrame 保存时读取）。
    const std::vector<ServiceBinding>& GetBindings() const { return m_bindings; }

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool                     OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 组织树里的一个部门 / 员工条目（Configure 时一次性收集）。
    struct DeptItem { CString m_id; CString m_name; CString m_path; };
    struct UserItem { CString m_id; CString m_name; CString m_title; CString m_dept; };

    // 一个可点区。
    struct Hit
    {
        RECT        m_rc;
        int         m_kind;       // 0=切模式 1=切换某绑定
        int         m_arg;        // kind=0：模式下标
        BindingKind m_bindKind;   // kind=1：绑定类型
        CString     m_bindId;     // kind=1：绑定目标 ID
        CString     m_bindLabel;  // kind=1：绑定显示名
    };

    // 当前绑定列表里是否已含 (kind,id)。
    bool IsBound(BindingKind kind, const CString& id) const;
    // 切换一个绑定（已含则移除，否则加入）。
    void ToggleBinding(BindingKind kind, const CString& id, const CString& label);

    // 递归收集组织树里的部门 / 员工。
    void CollectTree();
    // CollectTree 的递归实现。
    static void WalkOrg(const OrgNode* node, std::vector<CString>& path,
                        std::vector<DeptItem>& depts,
                        std::vector<UserItem>& users);

    // 部门 / 员工列表悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：
    // 把新滚动偏移写回本弹窗并重绘。
    //   ctx：本 BindingModal 实例；newScroll：拖动产生的新滚动偏移。
    static void OnListSbScroll(void* ctx, int newScroll);

    RECT    m_panelRect{};
    RECT    m_pickerRect{};        // 部门 / 员工列表所在的滚动区
    CString m_serviceName;
    CString m_entId;
    int     m_mode       = 0;      // 0 整个企业 / 1 按部门 / 2 按员工
    int     m_listScroll = 0;      // 部门 / 员工列表滚动偏移
    int     m_listContentH = 0;    // 列表内容总高

    std::vector<ServiceBinding> m_bindings;   // 工作副本
    std::vector<DeptItem>       m_depts;
    std::vector<UserItem>       m_users;
    std::vector<Hit>            m_hits;

    // 部门 / 员工列表的悬浮 auto-hide 滚动条（子控件，见 Widgets/OverlayScrollbar）。
    OverlayScrollbar* m_listSb = nullptr;
};

} // namespace flamingoadmin
