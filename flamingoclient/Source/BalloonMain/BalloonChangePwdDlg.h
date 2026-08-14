/**
 *  BalloonChangePwdDlg.h
 *
 *  "修改密码"对话框 —— 对照 design/修改密码.png。基于 balloonui DuiFrameWindow。
 *  三个密码输入框（当前密码 / 新密码 / 确认新密码，左侧锁图标、右侧小眼睛、掩码
 *  显示）+ 琥珀色提示框 + 取消 / 保存按钮。点"保存"经校验后把旧/新口令做 SHA-256
 *  走 v2 cmd_change_password 发往服务端，应答回投后提示结果。
 *
 *  与主面板的关系：主面板系统菜单"修改密码"项点击时创建并显示本对话框；改密应答
 *  由 CMainDlg → 主面板 → 转发到本对话框的 OnChangePwdResult。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

class CFlamingoClient;

namespace net { struct ChangePasswordResponse; }
namespace balloonwjui { class DuiVBox; class DuiEditHost; class DuiButton; }

/**
 *  CBalloonChangePwdDlg —— 修改密码对话框（非模态浮窗，owner = 主面板）。
 */
class CBalloonChangePwdDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；当前实现
     *                发请求走 CIUSocket 单例，pFMGClient 暂留作后续扩展（如取当前账号）。
     */
    explicit CBalloonChangePwdDlg(CFlamingoClient* pFMGClient);
    ~CBalloonChangePwdDlg();

    /**
     *  创建并显示对话框（已显示则前置 + 清空重置三个输入框）。
     *    owner：owner 窗口句柄（主面板）；内部 Create 传 NULL 让子控件通知自路由
     *           回本对话框（与新建群对话框同处理），owner 仅作语义留存。
     */
    void OpenForChange(HWND owner);

    /**
     *  收到改密应答：0 → 提示成功并关闭；103（旧密码错）→ 提示当前密码不正确；
     *  其它 → 提示失败。由主面板的 OnChangePwdResult 转发。
     *    resp：改密应答解析结果。
     */
    void OnChangePwdResult(const net::ChangePasswordResponse& resp);

    BEGIN_MSG_MAP(CBalloonChangePwdDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（三个密码字段 + 琥珀提示框 + 取消/保存按钮）。
    void BuildUiOnce();

    // 往 root 加一个"标题 + 密码输入框"字段组，并把输入框指针写回 *outEdit。
    void AddField(balloonwjui::DuiVBox* root, LPCTSTR label, UINT editId,
                  balloonwjui::DuiEditHost** outEdit);

    // 点"保存"：读三框 → 校验（失败就地提示）→ SHA-256 → 发起 cmd_change_password。
    void DoSave();

    // 清空三个输入框、复位提交态。
    void ResetFields();

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），引用不持有

    // 控件指针（借用，控件树持有）：读值 / 复位时用。
    balloonwjui::DuiEditHost* m_oldEdit;       // 当前密码
    balloonwjui::DuiEditHost* m_newEdit;       // 新密码
    balloonwjui::DuiEditHost* m_confirmEdit;   // 确认新密码

    bool m_built;          // 骨架是否已构建
    bool m_editsCreated;   // 三个输入框内嵌 EDIT 是否已建
    bool m_submitting;     // 是否已发出改密请求、等待应答（防重复提交）

    // 标题栏图标位图（程序图标 IDR_MAINFRAME 合成到标题栏底色上的 HBITMAP）。
    // DuiFrameWindow::SetIcon 仅借用，故由本类持有、析构时 DeleteObject。
    HBITMAP m_titleIconBmp;
};
