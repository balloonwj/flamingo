/**
 *  flamingoAdmin —— 企业设置弹窗 EnterpriseSettingsModal。
 *
 *  企业详情屏顶部点「企业设置」时弹出。两块内容：
 *    · 企业信息 —— 全称 / 简称 / 行业 / 地区 / 套餐，5 个字段，提交后经
 *      enterprise/update.do 落库；
 *    · 企业管理员 —— 自绘的管理员列表（每行可「移除」）+ 一行「新增管理员」
 *      （姓名 + 邮箱 + 添加），分别经 removeadmin.do / addadmin.do。
 *
 *  实现：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；
 *  点遮罩不关闭。企业信息字段与新增管理员行是 DuiEdit / DuiComboBox 真
 *  控件；管理员列表为自绘，「移除」命中区在 OnLButtonUp 里按坐标判定，命中
 *  后经 WM_DUI_NOTIFY 上报 AdminFrame（ctrlId = kIdEntSettings）。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"
#include "Util/AdminApi.h"   // AdminItem

#include <vector>

namespace balloonwjui
{
    class DuiEdit;
    class DuiComboBox;
}

namespace flamingoadmin {

// 企业设置弹窗。
class EnterpriseSettingsModal : public balloonwjui::DuiControl
{
public:
    EnterpriseSettingsModal();

    // 用某企业的信息预填表单、并设置管理员列表。每次打开弹窗前调用。
    //   ent：企业记录（其 m_id 即企业 ID）；admins：该企业的管理员列表。
    void SetData(const Enterprise& ent, const std::vector<AdminItem>& admins);

    // 仅刷新管理员列表（增 / 删管理员后、弹窗仍打开时调用）。
    void SetAdmins(const std::vector<AdminItem>& admins);

    // 校验企业信息表单：企业全称非空。
    bool ValidateEnterprise() const;

    // 校验「新增管理员」行：姓名与邮箱均非空。
    bool ValidateNewAdmin() const;

    CString GetEnterpriseId() const { return m_entId; }

    // ---- 企业信息表单读取 ----
    CString GetName() const;        // 企业全称
    CString GetShortName() const;   // 企业简称
    CString GetIndustry() const;    // 所属行业
    CString GetRegion() const;      // 主要地区
    int     GetPlan() const;        // 套餐编号：1 专业版 / 2 企业版 / 3 旗舰版

    // ---- 新增管理员行读取 ----
    CString GetNewAdminName() const;
    CString GetNewAdminEmail() const;
    void    ClearNewAdmin();        // 添加成功后清空新增行

    // 上一次点「移除」的管理员 ID（AdminFrame 收到通知后查询）。
    long long GetPendingRemoveAdminId() const { return m_pendingRemoveAdminId; }

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    // 一个自绘「移除」命中区。
    struct RemoveHit
    {
        RECT      m_rc;        // 矩形（host 客户区坐标）
        long long m_adminId;   // 该行管理员 ID
    };

    // 算面板总高度（随管理员条数变化）。
    int PanelHeight() const;

    // 算企业信息表单 4 个文本字段 + 套餐下拉的控件矩形（写入成员控件）。
    void LayoutFormControls();

    CString m_entId;     // 当前企业 ID
    CString m_entName;   // 当前企业名（标题横幅用）
    RECT    m_panelRect{};

    std::vector<AdminItem>  m_admins;              // 管理员列表
    std::vector<RemoveHit>  m_removeHits;          // 本帧记录的「移除」命中区
    long long               m_pendingRemoveAdminId = 0;  // 上次点「移除」的管理员

    // 输入控件（所有权在控件树中；借用指针）。
    balloonwjui::DuiEdit* m_editName     = nullptr;
    balloonwjui::DuiEdit* m_editShort    = nullptr;
    balloonwjui::DuiEdit* m_editIndustry = nullptr;
    balloonwjui::DuiEdit* m_editRegion   = nullptr;
    balloonwjui::DuiComboBox* m_cbPlan       = nullptr;
    balloonwjui::DuiEdit* m_editNewName  = nullptr;
    balloonwjui::DuiEdit* m_editNewEmail = nullptr;
};

} // namespace flamingoadmin
