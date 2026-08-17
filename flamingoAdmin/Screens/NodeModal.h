/**
 *  flamingoAdmin —— 组织节点对话框 NodeModal（新增部门 / 新增员工）。
 *
 *  企业详情屏「组织架构」tab 里点「新增子部门 / 新增员工」时弹出。两种模式各自
 *  一套字段：
 *    · AddDept —— 部门名称 + 部门负责人；
 *    · AddUser —— 姓名 + 职位 + 邮箱 + 手机号 + 性别。
 *
 *  【为什么是独立顶层窗口而不是 overlay】
 *  上一代实现继承 DuiControl，是 RootContainer 弹窗层里的一个绘制层：自绘半透明
 *  遮罩 + 居中卡片，两组字段都建好再切显隐。当时它盖不住底层的输入框 —— 员工详情
 *  面板(OrgDetailPanel)的 11 个只读值字段当时各自内嵌一个 Win32 EDIT 子窗口，而
 *  子窗口总是绘制在父窗口客户区的 GDI 内容之上，于是弹窗一开，底层的员工姓名 /
 *  登录账号就显示在表单上面（2026-08-11 现场）。该现场的直接成因已在 OrgDetailPanel
 *  侧修掉（见 SetBasicEditsSuppressed），但"overlay 盖不住子窗口"是 Win32 的固有
 *  行为、治不了本；本类改走 DuiFrameWindow 独立顶层窗口，从根上不再有这个问题，
 *  与 EmployeeEditDialog / ConfirmDialog 保持一致。（输入框已于 2026-08-17 改为
 *  无窗口实现，该冲突不复存在，此处保留作为选型缘由的记录。）
 *
 *  真模态语义：CreateAndShow 时 EnableWindow(parent, FALSE) 禁掉主窗口，
 *  WM_DESTROY 时恢复。期间主窗口不响应鼠标 / 键盘。
 *
 *  生命周期：on-demand —— 用户点「新增子部门 / 新增员工」时 AdminFrame new 出来，
 *  关闭时经 WM_APP_DELETE_NODE_MODAL 延迟 delete（不能在自己的 WM_DESTROY 里
 *  self-delete，栈展开还要回到它的成员）。模式与挂载目标在构造时定死，不再中途
 *  切换，故也不需要上一代那套按模式切字段显隐的逻辑。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Controls/Window/DuiFrameWindow.h"

namespace balloonwjui
{
    class DuiEditHost;
    class DuiComboBox;
}

namespace flamingoadmin {

class AdminFrame;

// ===========================================================================
// NodeModal —— 组织节点对话框（独立顶层窗口）
// ===========================================================================
class NodeModal : public balloonwjui::DuiFrameWindow
{
public:
    // 对话框模式。构造时定死，中途不改。
    enum class Mode
    {
        AddDept,   // 新增部门：部门名称 + 部门负责人
        AddUser,   // 新增员工：姓名 + 职位 + 邮箱 + 手机号 + 性别
    };

    /**
     *  构造对话框。参数仅记录，不创建任何 HWND / 控件 —— 真正创建在 CreateAndShow。
     *  @param frame      所属 AdminFrame，用于回调提交逻辑与弹 Toast；不持有，只引用。
     *                    生命期约束：本对话框存活期间 frame 必须有效。
     *  @param parentHwnd 主窗口 HWND，真模态期间被 EnableWindow(FALSE) 禁用，
     *                    本对话框 WM_DESTROY 时恢复。
     *  @param mode       新增部门还是新增员工，决定建哪一组字段。
     *  @param parentName 挂载目标部门名，显示在表单顶部的提示横幅上；空串则横幅省略。
     */
    NodeModal(AdminFrame* frame, HWND parentHwnd, Mode mode, const CString& parentName);
    ~NodeModal();

    /**
     *  创建顶层窗口 + 构造客户区控件树 + 真模态启用 + 居中显示。
     *  一个实例最多调一次。
     *  @return false 表示创建失败，此时本对象应被 delete。
     */
    bool CreateAndShow();

    // 关闭对话框（供 AdminFrame 在 HTTP 提交成功后调用）。等价于 PostMessage
    // WM_CLOSE —— 走 OnCloseMsg → DestroyWindow → OnDestroyMsg（恢复主窗口
    // Enable + 通知 AdminFrame 清指针）。
    void RequestClose();

    // 当前模式（AdminFrame 提交时据此决定调建部门还是建员工的接口）。
    Mode GetMode() const { return m_mode; }

    // 校验必填项：部门模式要求部门名非空；员工模式要求姓名与邮箱非空。
    bool Validate() const;

    // ---- 表单字段读取（供 AdminFrame 提交时取数据）----
    // 字段不属于当前模式时返回空串（该模式下这些控件根本没建）。
    CString GetDeptName() const;     // 部门名称
    CString GetDeptLeader() const;   // 部门负责人
    CString GetUserName() const;     // 员工姓名
    CString GetUserTitle() const;    // 员工职位
    CString GetUserEmail() const;    // 员工邮箱
    CString GetUserPhone() const;    // 员工手机号
    CString GetUserGender() const;   // 员工性别

    /**
     *  切换「提交中」状态。提交 HTTP 期间把「创建」按钮置灰、文字改为「创建中…」，
     *  防止用户连点；HTTP 应答回来后由 AdminFrame 再切回 false（若弹窗保留，
     *  例如服务端校验失败）。
     *  @param busy true=进入提交中；false=恢复可点。
     */
    void SetBusy(bool busy);

    BEGIN_MSG_MAP(NodeModal)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE,      OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY,    OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 按 m_mode 构造客户区控件树并 SetClientContent。纯代码构造，不引 XML，
    // 与 EmployeeEditDialog 保持一致的风格。
    void BuildClientContent();

    // 「创建」按钮入口：校验必填项 → 通过则调 AdminFrame 的提交入口并置 Busy，
    // 未通过则弹 Toast 提示。
    void SubmitOrShake();

    // 按钮点击的统一入口（取消 / 创建）。
    LRESULT OnDuiNotify(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CLOSE：用户点标题栏关闭按钮（或调 RequestClose）→ DestroyWindow。
    LRESULT OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // WM_DESTROY：无论怎么关，都在此恢复主窗口 Enable + 通知 AdminFrame 清指针。
    // 是真模态语义解除的唯一收口。
    LRESULT OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

private:
    AdminFrame* m_frame;        // 所属 AdminFrame，只引用不持有
    HWND        m_parentHwnd;   // 主窗口 HWND（真模态期间被禁用）
    Mode        m_mode;         // 新增部门还是新增员工，构造时定死
    CString     m_parentName;   // 挂载目标部门名，仅用于顶部提示横幅
    bool        m_busy;         // 是否在 HTTP 提交中（防连点）

    // 输入控件裸指针。所有权在控件树里（SetClientContent 接管），本类只借用；
    // 只有当前模式用到的那几个非空，另一组恒为 nullptr。
    balloonwjui::DuiEditHost* m_editDeptName;    // 部门名称 *（AddDept）
    balloonwjui::DuiEditHost* m_editDeptLeader;  // 部门负责人（AddDept）
    balloonwjui::DuiEditHost* m_editUserName;    // 姓名 *（AddUser）
    balloonwjui::DuiEditHost* m_editUserTitle;   // 职位（AddUser）
    balloonwjui::DuiEditHost* m_editUserEmail;   // 邮箱 *（AddUser）
    balloonwjui::DuiEditHost* m_editUserPhone;   // 手机号（AddUser）
    balloonwjui::DuiComboBox* m_cbUserGender;    // 性别（AddUser）

    balloonwjui::DuiControl*  m_btnSubmit;       // 「创建」按钮（busy 时改文字并置灰）
};

} // namespace flamingoadmin
