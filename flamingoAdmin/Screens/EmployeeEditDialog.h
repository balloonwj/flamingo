/**
 *  flamingoAdmin —— 修改员工资料对话框 EmployeeEditDialog。
 *
 *  企业详情屏「组织架构」tab 选中员工后,在「基本信息」tab 点「编辑资料」时
 *  弹出。表单 9 个可编辑字段:姓名 / 职位 / 工号 / 入职日期 / 邮箱 / 手机号 /
 *  座机 / 工位地址 / 个性签名;1 个只读字段「登录账号」。
 *
 *  本类与上一代 EmployeeEditModal 的【根本差别】:
 *  上一代是 RootContainer 的 modal overlay 子控件 —— 共享主窗口 DuiHost,
 *  自绘半透明遮罩 + 居中卡片 + header / banner / 按钮栏,自己写 BuildFieldLayout
 *  手算 9 字段几何,自己拦 OnLButtonUp 吃遮罩点击。这种走法跟 DuiHost 的 paint
 *  模型对撞 —— 弹窗首次显示时 9 个 EDIT 子窗的 paint 会被 host 的 back-buffer +
 *  全 client BitBlt 吞掉,屏幕上看不到任何文字 / 占位字,必须点一下框才会重画。
 *
 *  本类改走 balloonui 标准路径:继承 DuiFrameWindow 拿独立顶层 HWND + 自带
 *  标题栏 / 关闭按钮 / DuiHost paint;客户区用 DuiVBox / DuiHBox 容器纯代码
 *  构造控件树,字段是现成的 DuiEditHost / DuiLabel / DuiButton,完全不自绘
 *  对话框框架。由 balloonui 处理整个 paint 周期 —— paint 时序问题消失。
 *
 *  真模态语义:CreateAndShow 调用时 EnableWindow(parent, FALSE) 把主窗口禁掉,
 *  关闭时(WM_DESTROY)恢复。期间主窗口不响应鼠标 / 键盘。
 *
 *  生命周期:本对话框 on-demand —— 用户点「编辑资料」时 AdminFrame new 出来,
 *  关闭时 DestroyWindow + delete。状态不存档,下次打开重新构造。
 *
 *  balloonwj@qq.com   2026-05-28
 */
#pragma once

#include "Controls/Window/DuiFrameWindow.h"
#include "Mock/MockData.h"

namespace balloonwjui { class DuiEditHost; }

namespace flamingoadmin {

class AdminFrame;

// ===========================================================================
// EmployeeEditDialog —— 修改员工资料对话框(独立顶层窗口)
// ===========================================================================
class EmployeeEditDialog : public balloonwjui::DuiFrameWindow
{
public:
    // 构造对话框。参数仅记录,不创建任何 HWND/控件 —— 真正创建在
    // CreateAndShow 里。
    //   frame      :所属 AdminFrame,用于回调提交逻辑;不持有,只引用。
    //                生命期约束:本对话框生存期间 frame 必须有效。
    //   parentHwnd :主窗口 HWND,真模态期间会被 EnableWindow(FALSE)
    //                禁用,本对话框 WM_DESTROY 时恢复。
    //   node       :被编辑员工的完整资料(由 AdminFrame 经 user/detail.do
    //                取回);用于预填 9 个字段 + banner 标题 + 登录账号显示。
    EmployeeEditDialog(AdminFrame* frame, HWND parentHwnd, const OrgNode& node);
    ~EmployeeEditDialog();

    // 创建顶层窗口 + 构造客户区控件树 + 灌入 m_node 字段值 + 真模态启用
    // (EnableWindow(parent, FALSE))+ 居中显示。
    // 一个实例最多调一次。失败返回 false,此时本对象应被 delete。
    bool CreateAndShow();

    // 关闭对话框(供 AdminFrame 在 HTTP 提交成功后调用)。等价于 PostMessage
    // WM_CLOSE —— 走 OnCloseMsg → DestroyWindow → OnDestroyMsg(EnableWindow
    // 恢复 + AdminFrame 清空指针)。
    void RequestClose();

    // ---- 表单字段读取(供 AdminFrame 提交时拉数据用) ----
    CString GetUserId() const;        // 被编辑员工的 ID
    CString GetName() const;          // 姓名
    CString GetTitle() const;         // 职位
    CString GetEmployeeNo() const;    // 工号
    CString GetJoinDate() const;      // 入职日期
    CString GetEmail() const;         // 邮箱
    CString GetPhone() const;         // 手机号
    CString GetLandline() const;      // 座机
    CString GetWorkstation() const;   // 工位地址
    CString GetSignature() const;     // 个性签名

    // 切换"提交中"状态。提交 HTTP 期间,保存按钮置灰、文字改为"保存中…",
    // 防止用户连点;HTTP reply 回来后由 AdminFrame 再切回 false(若保留弹窗,
    // 例如校验失败)。
    void SetBusy(bool busy);

    BEGIN_MSG_MAP(EmployeeEditDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE,      OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY,    OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 构造客户区控件树并 SetClientContent。XML 不引入,纯代码构造保持
    // admin 现有风格一致。
    void BuildClientContent();

    // 把 m_node 的字段值灌进 9 个 DuiEditHost + banner / 登录账号文字。
    // 在 BuildClientContent 之后立即调一次。
    void LoadNodeIntoUi();

    // 9 个字段的 EnsureCreated:XML / 纯代码构造完控件树后,DuiEditHost
    // 寄宿的真 Win32 EDIT 子窗还没建出来;必须在 host HWND 就绪后逐个
    // 显式调 EnsureCreated。参考 CBalloonProfileWindow::RebuildUi 同套路。
    void EnsureFieldEditsCreated();

    // 「保存」按钮入口:校验必填项(姓名 + 邮箱非空)→ 若通过则调
    // AdminFrame 的 OnEmployeeEditDialogSubmit 入口,本对话框置 Busy 等
    // HTTP reply;若校验未通过则 ShowToast 提示用户。
    void SubmitOrShake();

    // 按钮 / 关闭点击的统一入口。
    LRESULT OnDuiNotify(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CLOSE:用户点标题栏关闭按钮(或调 RequestClose)→ DestroyWindow。
    LRESULT OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // WM_DESTROY:无论怎么关,都在此恢复主窗口 Enable + 通知 AdminFrame 清
    // 自己指针。是真模态语义解除的唯一收口。
    LRESULT OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

private:
    AdminFrame* m_frame;              // 所属 AdminFrame,只引用不持有
    HWND        m_parentHwnd;         // 主窗口 HWND(真模态期间被禁用)
    OrgNode     m_node;               // 待编辑员工的完整资料(预填用)
    bool        m_busy;               // 是否在 HTTP 提交中(防双击)

    // 输入控件 / 按钮裸指针。所有权在控件树中,本类只借用;控件树由
    // SetClientContent 接管 -> 本对话框析构时一并销毁。
    balloonwjui::DuiEditHost* m_editName;        // 姓名 *
    balloonwjui::DuiEditHost* m_editTitle;       // 职位
    balloonwjui::DuiEditHost* m_editEmployeeNo;  // 工号
    balloonwjui::DuiEditHost* m_editJoinDate;    // 入职日期
    balloonwjui::DuiEditHost* m_editEmail;       // 邮箱 *
    balloonwjui::DuiEditHost* m_editPhone;       // 手机号
    balloonwjui::DuiEditHost* m_editLandline;    // 座机
    balloonwjui::DuiEditHost* m_editWorkstation; // 工位地址
    balloonwjui::DuiEditHost* m_editSignature;   // 个性签名

    balloonwjui::DuiControl*  m_btnSave;         // 「保存」按钮(busy 时改文字)
};

} // namespace flamingoadmin
