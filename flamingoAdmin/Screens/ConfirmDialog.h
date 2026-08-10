/**
 *  flamingoAdmin —— 通用确认 / 信息对话框 ConfirmDialog。
 *
 *  上一代 ConfirmModal 是 RootContainer 的 modal overlay 自绘子控件;本类
 *  按"复用 balloonui 现成对话框 + 真模态 + on-demand"约定改成独立顶层
 *  DuiFrameWindow,沿用与 EmployeeEditDialog 完全一致的套路。
 *
 *  两种模式共用同一类:
 *    · 确认模式 :红色危险图标 + 「取消 + 确认」两按钮,用于二次确认不可逆
 *                操作(删除组织节点 / 服务号 / 公告 / 企业 / 重置密码 ……)。
 *    · 信息模式 :绿色成功图标 + 单个「知道了」按钮;copyText 非空时额外
 *                出一个「复制密码」按钮,用于「新增企业/管理员/员工成功后
 *                给管理员复制初始口令」一类的场景。
 *
 *  消息流:
 *    · 取消 / 标题栏 X :DestroyWindow → AdminFrame 收
 *      OnConfirmDialogDestroyed,根据"待确认操作是否还在登记"判断是否走
 *      撤销逻辑(通常什么都不做)。
 *    · 确认按钮         :调用 AdminFrame::OnConfirmDialogConfirmed,由
 *      AdminFrame 根据自身状态机执行此前登记的待确认操作(如 ExecuteConfirmedDelete)。
 *      Dialog 自身不知道"确认后要做什么"。
 *    · 复制密码按钮     :Dialog 自己经 CopyTextToClipboard 把 spec.copyText
 *      拷入剪贴板,然后弹一个轻量 Toast 提示(由 AdminFrame 帮忙弹)。
 *
 *  balloonwj@qq.com   2026-05-28
 */
#pragma once

#include "Controls/Window/DuiFrameWindow.h"
#include <atlstr.h>

namespace flamingoadmin {

class AdminFrame;

// ===========================================================================
// ConfirmDialogSpec —— 构造对话框时的配置参数集合。
// ===========================================================================
struct ConfirmDialogSpec
{
    CString m_title;          // 标题栏文字
    CString m_message;        // 主体说明文字(自动换行)
    CString m_okText;         // 确认 / 知道了 按钮的文字
    bool    m_infoMode;       // false=确认模式(红+取消);true=信息模式(绿+单按钮)
    bool    m_confirmDanger;  // 确认模式下"确认"按钮是否用红色危险样式
    CString m_copyText;       // 信息模式下"复制密码"按钮拷贝的文本;空=不出此按钮

    ConfirmDialogSpec()
        : m_infoMode(false)
        , m_confirmDanger(false)
    {
    }
};

// ===========================================================================
// ConfirmDialog —— 通用确认 / 信息对话框(独立顶层窗口)
// ===========================================================================
class ConfirmDialog : public balloonwjui::DuiFrameWindow
{
public:
    // 构造对话框。参数仅记录,不创建任何 HWND/控件。
    //   frame      :所属 AdminFrame,用于回调与 Toast;不持有。
    //   parentHwnd :主窗口 HWND,真模态期间会被 EnableWindow(FALSE)。
    //   spec       :对话框配置(标题 / 文字 / 按钮 / 模式)。
    ConfirmDialog(AdminFrame* frame, HWND parentHwnd,
                  const ConfirmDialogSpec& spec);
    ~ConfirmDialog();

    // 创建顶层窗口 + 构造客户区 + EnableWindow(parent,FALSE) + 居中显示。
    // 一个实例最多调一次。失败返回 false,此时本对象应被 delete。
    bool CreateAndShow();

    // 关闭对话框(供 AdminFrame 在确认执行完毕后调用)。等价于 PostMessage
    // WM_CLOSE → DestroyWindow → 真模态解除。
    void RequestClose();

    BEGIN_MSG_MAP(ConfirmDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE,      OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY,    OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 构造客户区控件树 + SetClientContent。
    void BuildClientContent();

    // 把 spec.copyText 拷入剪贴板 + 弹 Toast。
    void OnCopyButtonClicked();

    // 按钮点击统一入口(取消 / 复制密码 / 确认)。
    LRESULT OnDuiNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled);

    // WM_CLOSE:DestroyWindow,具体收尾在 OnDestroyMsg。
    LRESULT OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // WM_DESTROY:解除真模态 + 通知 AdminFrame 清自己指针。
    LRESULT OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled);

private:
    AdminFrame*       m_frame;       // 所属 AdminFrame,只引用不持有
    HWND              m_parentHwnd;  // 主窗口 HWND(真模态期间被禁用)
    ConfirmDialogSpec m_spec;        // 构造时传入的配置
};

} // namespace flamingoadmin
