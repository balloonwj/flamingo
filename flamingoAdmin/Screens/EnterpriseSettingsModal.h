/**
 *  flamingoAdmin —— 企业设置弹窗 EnterpriseSettingsModal。
 *
 *  企业详情屏顶部点「企业设置」时弹出。两块内容：
 *    · 企业信息 —— 全称 / 简称 / 行业 / 地区 / 套餐 / 账号前缀（2026-10-06 起），6 个字段，提交后经
 *      enterprise/update.do 落库；账号前缀只影响之后新建的账号（docs/登录账号与手机号登录方案.md）；
 *    · 企业管理员 —— 自绘的管理员列表（每行可「移除」）+ 一行「新增管理员」
 *      （姓名 + 邮箱 + 添加），分别经 removeadmin.do / addadmin.do。
 *  原先的第三块「功能开关」（十三个开关与云端录像空间）已于 2026-10-08 移到企业详情屏的「功能开关」子 tab
 *  （FeatureSwitchesPage），本弹窗不再涉及功能开关。
 *
 *  实现：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；
 *  点遮罩不关闭。企业信息字段与新增管理员行是 DuiEdit / DuiComboBox 真
 *  控件；管理员列表为自绘，「移除」命中区在 OnLButtonUp 里按坐标判定，命中
 *  后经 WM_DUI_NOTIFY 上报 AdminFrame（ctrlId = kIdEntSettings）。
 *
 *  滚动（2026-10-04 起，此前内容高过窗口时卡片会超出窗口）：卡片高度取内容所需高度与窗口可用高度中较小的一个，
 *  标题栏与底部按钮栏固定不动，两者之间的主体区内容放不下时可用滚轮或右侧的悬浮滚动条（OverlayScrollbar）
 *  上下滚动。主体区的子控件按滚动偏移重新摆放，绘制时裁剪到主体区；滚出主体区的子控件不参与命中测试。
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

class OverlayScrollbar;

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

    // 校验账号前缀：规则同 adminapi::NormalizeAccountPrefix（空串表示清除，合法）。保存前与 ValidateEnterprise
    // 一起调用，不合格时提示、不发请求。
    bool ValidateAccountPrefix() const;

    // 校验「新增管理员」行：姓名与邮箱均非空。
    bool ValidateNewAdmin() const;

    CString GetEnterpriseId() const { return m_entId; }

    // ---- 企业信息表单读取 ----
    CString GetName() const;        // 企业全称
    CString GetShortName() const;   // 企业简称
    CString GetIndustry() const;    // 所属行业
    CString GetRegion() const;      // 主要地区
    int     GetPlan() const;        // 套餐编号：0 基础版 / 1 专业版 / 2 旗舰版；列表外的原值（「套餐N」）原样返回 N
    CString GetAccountPrefix() const;   // 账号前缀输入框的原文（未规整；服务端会去空白、转大写）

    // ---- 新增管理员行读取 ----
    CString GetNewAdminName() const;
    CString GetNewAdminEmail() const;
    void    ClearNewAdmin();        // 添加成功后清空新增行

    // 上一次点「移除」的管理员 ID（AdminFrame 收到通知后查询）。
    long long GetPendingRemoveAdminId() const { return m_pendingRemoveAdminId; }

    // 上一次点「移除」的管理员姓名（移除前的确认框里显示）；在列表里找不到时返回空串。
    CString GetPendingRemoveAdminName() const
    {
        for (size_t i = 0; i < m_admins.size(); ++i)
        {
            if (m_admins[i].m_adminId == m_pendingRemoveAdminId)
            {
                return m_admins[i].m_name;
            }
        }
        return CString();
    }

    // ---- 主体区滚动（单元测试也用这几项核对布局）----

    // 当前滚动偏移（像素，0 表示在顶部）。
    int  GetScroll() const { return m_scroll; }

    // 最大滚动偏移（像素）：主体内容高度减去主体区可视高度，内容放得下时为 0。须在 Layout 之后读取。
    int  GetScrollMax() const { return m_scrollMax; }

    // 主体区（标题栏与底部按钮栏之间的可视区域）的矩形，host-client 坐标。须在 Layout 之后读取。
    RECT GetBodyRect() const { return m_bodyRect; }

    // 滚动到指定偏移并重新摆放主体区的子控件。
    //   scroll：目标偏移（像素），超出 [0, GetScrollMax()] 时夹取到边界。
    // 副作用：偏移确有变化时重排并重绘本弹窗，同步滚动条。
    void ScrollTo(int scroll);

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;

    // 滚轮：弹窗可见时一律在这里处理并返回 true（不让下层页面滚动）；主体内容放得下时不滚动。
    //   pt：host-client 坐标；zDelta：滚轮增量（WHEEL_DELTA 的倍数或分数）；mkFlags：不使用。
    bool                     OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 一个自绘「移除」命中区。
    struct RemoveHit
    {
        RECT      m_rc;        // 矩形（host 客户区坐标）
        long long m_adminId;   // 该行管理员 ID
    };

    // 算主体区内容的总高度（随管理员条数变化）：提示横幅、企业信息、管理员列表与新增管理员行。
    int ContentHeight() const;

    // 算面板在内容全部展开时的总高度：标题栏 + 主体内容 + 底部按钮栏。
    int PanelHeight() const;

    // 算企业信息表单各字段（全称、简称、行业、地区、套餐、账号前缀）的控件矩形（写入成员控件）。
    //   contentTop：主体内容顶边的 y 坐标（已减去滚动偏移，host-client 坐标）。
    void LayoutFormControls(int contentTop);

    // 判断子控件是否属于主体区（随内容滚动、绘制时裁剪到主体区）；标题栏的关闭按钮、底部的取消 / 保存
    // 按钮与滚动条不属于主体区。
    bool IsBodyChild(const balloonwjui::DuiControl* child) const;

    // 滚动条拖动回调（DuiScrollBar::OnScrollFn）：ctx 为本弹窗，newScroll 为新的滚动偏移。
    static void OnScrollbarScroll(void* ctx, int newScroll);

    CString m_entId;     // 当前企业 ID
    CString m_entName;   // 当前企业名（标题横幅用）
    RECT    m_panelRect{};

    std::vector<AdminItem>  m_admins;              // 管理员列表
    std::vector<RemoveHit>  m_removeHits;          // 本帧记录的「移除」命中区
    long long               m_pendingRemoveAdminId = 0;  // 上次点「移除」的管理员

    // 输入控件（所有权在控件树中；借用指针）。
    balloonwjui::DuiEdit* m_editName     = nullptr;
    balloonwjui::DuiEdit* m_editShort    = nullptr;
    // 所属行业下拉框：选项与新增企业共用 adminapi::kIndustryOptions；企业现有的行业不在其中或
    // 为空时，SetData 在末尾加一项额外项并选中（空行业显示为「（未填写）」）。
    balloonwjui::DuiComboBox* m_cbIndustry = nullptr;
    balloonwjui::DuiEdit* m_editRegion   = nullptr;
    balloonwjui::DuiComboBox* m_cbPlan       = nullptr;
    balloonwjui::DuiEdit* m_editPrefix   = nullptr;   // 账号前缀输入框（第三行右列，2026-10-06；借用，所有权在控件树中）
    balloonwjui::DuiEdit* m_editNewName  = nullptr;
    balloonwjui::DuiEdit* m_editNewEmail = nullptr;

    // 标题栏与底部按钮栏里的按钮、「添加」按钮（借用，所有权在控件树中）。
    balloonwjui::DuiControl* m_closeBtn    = nullptr;  // 标题栏右端的关闭按钮
    balloonwjui::DuiControl* m_cancelBtn   = nullptr;  // 底部「取消」
    balloonwjui::DuiControl* m_saveBtn     = nullptr;  // 底部「保存企业信息」
    balloonwjui::DuiControl* m_addAdminBtn = nullptr;  // 新增管理员行的「添加」（属于主体区，随内容滚动）

    // ---- 主体区滚动 ----
    OverlayScrollbar* m_scrollbar = nullptr;  // 主体区右缘的悬浮滚动条（借用，所有权在控件树中）
    int  m_scroll    = 0;     // 当前滚动偏移（像素）；SetData 打开新企业时归零
    int  m_scrollMax = 0;     // 最大滚动偏移（像素），每次 Layout 时重算；内容放得下时为 0
    RECT m_bodyRect{};        // 主体区可视矩形（host-client 坐标），每次 Layout 时重算
};

} // namespace flamingoadmin
