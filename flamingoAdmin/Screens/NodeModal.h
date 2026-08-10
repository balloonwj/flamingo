/**
 *  flamingoAdmin —— 组织节点弹窗 NodeModal（新增部门 / 新增员工）。
 *
 *  企业详情屏「组织架构」tab 里点击「新增子部门 / 新增员工」时弹出。两种
 *  模式共用一个弹窗：
 *    · AddDept —— 部门名称 + 部门负责人；
 *    · AddUser —— 姓名 + 职位 + 邮箱 + 手机号 + 性别。
 *
 *  实现与 NewEnterpriseModal 一致：铺满客户区的半透明遮罩 + 居中表单卡片，
 *  常驻于 RootContainer 弹窗层，开 / 关只切显隐。表单输入框是 DuiEditHost /
 *  DuiComboBox 等真控件，按当前模式显示对应字段、隐藏另一组。
 *
 *  点击遮罩空白处不关闭弹窗；关闭只经 X / 取消按钮。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"

#include <vector>

namespace balloonwjui
{
    class DuiEditHost;
    class DuiComboBox;
}

namespace flamingoadmin {

// 组织节点弹窗。
class NodeModal : public balloonwjui::DuiControl
{
public:
    // 弹窗模式。
    enum class Mode
    {
        AddDept,   // 新增部门
        AddUser,   // 新增员工
    };

    NodeModal();

    // 设置弹窗模式与挂载目标部门名（用于提示横幅），并清空表单。
    //   每次打开弹窗前调用。
    void SetMode(Mode mode, const CString& parentName);

    // 校验必填项：部门模式要求部门名非空；员工模式要求姓名与邮箱非空。
    bool Validate() const;

    Mode    GetMode() const { return m_mode; }

    // ---- 表单字段读取 ----
    CString GetDeptName() const;     // 部门名称
    CString GetDeptLeader() const;   // 部门负责人
    CString GetUserName() const;     // 员工姓名
    CString GetUserTitle() const;    // 员工职位
    CString GetUserEmail() const;    // 员工邮箱
    CString GetUserPhone() const;    // 员工手机号
    CString GetUserGender() const;   // 员工性别

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    // 一个表单字段的布局描述。
    struct FieldRect
    {
        balloonwjui::DuiControl* m_ctrl;   // 输入控件
        CString                  m_label;  // 字段标签
        RECT                     m_label_rc;
        RECT                     m_ctrl_rc;
    };

    // 按当前模式 + 面板矩形算出各字段的标签 / 控件矩形。
    void BuildFieldLayout(std::vector<FieldRect>& out) const;

    // 把表单清空。
    void ResetFields();

    Mode    m_mode = Mode::AddDept;
    CString m_parentName;
    RECT    m_panelRect{};

    // 输入控件（所有权在控件树中；借用指针）。
    balloonwjui::DuiEditHost* m_editDeptName   = nullptr;
    balloonwjui::DuiEditHost* m_editDeptLeader = nullptr;
    balloonwjui::DuiEditHost* m_editUserName   = nullptr;
    balloonwjui::DuiEditHost* m_editUserTitle  = nullptr;
    balloonwjui::DuiEditHost* m_editUserEmail  = nullptr;
    balloonwjui::DuiEditHost* m_editUserPhone  = nullptr;
    balloonwjui::DuiComboBox* m_cbUserGender   = nullptr;
};

} // namespace flamingoadmin
