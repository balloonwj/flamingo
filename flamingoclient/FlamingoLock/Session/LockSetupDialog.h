/**
 *  锁屏前设置临时密码的窗口（balloonui 的 DuiFrameWindow，运行在锁屏线程上）。
 *
 *  内容：两个密码框（输入、再输一次）、「锁屏画面」下拉框（五种动画、随机、轮流播放）与其右侧的「预览」按钮、
 *  窗口内的错误提示、「取消」「锁屏」按钮。点「锁屏」时按 CheckNewPassword 校验，不合规就在窗口内显示原因；
 *  合规则记下密码与选择、销毁窗口，并给锁屏线程投递 kThreadMsgSetupDone，由线程的消息循环取走结果开始锁屏。
 *  关闭窗口或点「取消」等于放弃锁屏（投递 kThreadMsgSetupCancelled）。
 *
 *  「预览」（2026-10-07）：按下拉框当前的选择，在本窗口所在的那块显示器上全屏播放动画（CLockPreviewSession），
 *  单击或按任意键结束。预览期间本窗口被禁用，不能再点「预览」或「锁屏」；结束时恢复可用并取回前台。
 *  预览的逐帧绘制由锁屏线程的消息循环驱动（IsPreviewActive / RenderPreviewFrame）。
 *  标题栏左侧显示程序图标（开发约定文档 UI 约定第 10 条）。
 *
 *  典型用法（锁屏线程）：
 *      dlg.Open(choice);
 *      消息循环每轮：if (dlg.IsPreviewActive()) dlg.RenderPreviewFrame();
 *      ...收到 kThreadMsgSetupDone 后：dlg.TakeResult(password, choice);
 *
 *  方案见 docs/锁屏方案.md 第 4.6 节、第 11 节。
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "../stdafx.h"
#include "../Core/LockAnimationChoice.h"
#include "LockPreviewSession.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui { class DuiVBox; class DuiEdit; class DuiLabel; class DuiComboBox; }

namespace flamingolock {

//锁屏线程的线程消息（PostThreadMessage，hwnd 为 NULL），偏移登记在开发约定文档的 WM_APP 占用表
const UINT kThreadMsgRequestLock = WM_APP + 0x101;       // 宿主请求锁屏：打开设置窗口
const UINT kThreadMsgSetupDone = WM_APP + 0x102;         // 设置窗口确认：取走密码与选择，开始锁屏
const UINT kThreadMsgSetupCancelled = WM_APP + 0x103;    // 设置窗口被取消或关闭

//设置窗口投给自己的私有消息：点了「预览」后，等按钮的点击通知处理完再开始全屏预览（2026-10-07）
const UINT kMsgSetupStartPreview = WM_APP + 0x105;

/**
 *  设置临时密码的窗口。非线程安全，只在锁屏线程上使用。
 */
class CLockSetupDialog : public balloonwjui::DuiFrameWindow, public ILockPreviewOwner
{
public:
    CLockSetupDialog();
    ~CLockSetupDialog();

    /**
     *  打开窗口（已打开则提到前面），清空两个密码框，下拉框选中给定的选择。
     *  @param choice 上次的锁屏画面选择
     *  @return 窗口已显示返回 true；创建失败返回 false
     */
    bool Open(const LockAnimationChoice& choice);

    /**
     *  把已打开的窗口提到前面（宿主在设置窗口开着时再次请求锁屏）。
     */
    void BringToFront();

    /**
     *  取走确认时记下的密码与选择，并把内部保存的密码清零。只在收到 kThreadMsgSetupDone 后调用一次。
     *  @param password 接收密码（调用方用完后自行清零）
     *  @param choice   接收锁屏画面选择
     */
    void TakeResult(std::wstring& password, LockAnimationChoice& choice);

    /**
     *  是否正在全屏预览。锁屏线程的消息循环据此决定要不要逐帧调用 RenderPreviewFrame。
     */
    bool IsPreviewActive() const;

    /**
     *  推进并画一帧预览；预览已决定结束时由预览会话回调 OnPreviewEnding 并关闭预览窗口。
     *  锁屏线程的消息循环在 IsPreviewActive 为 true 时每轮调用一次。
     */
    void RenderPreviewFrame();

    // ---- ILockPreviewOwner ----
    // 预览即将结束：恢复本窗口可用，并把它提到前面、焦点给密码框
    void OnPreviewEnding() override;

    BEGIN_MSG_MAP(CLockSetupDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgSetupStartPreview, OnStartPreviewMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnStartPreviewMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 按下拉框当前的选择开始全屏预览，并禁用本窗口；已在预览或已确认 / 取消时什么也不做
    void StartPreview();

    // 构建控件树（每次创建窗口时一次）
    void BuildUi();
    // 往根容器加一个「标题 + 控件」字段组
    void AddFieldLabel(balloonwjui::DuiVBox* root, LPCTSTR label);
    // 点「锁屏」：校验、记下结果、关窗并通知锁屏线程
    void Confirm();
    // 放弃：关窗并通知锁屏线程
    void Cancel();
    // 在错误提示行显示一句话（空串表示清除）
    void ShowError(LPCTSTR text);

private:
    balloonwjui::DuiEdit*     m_password;     // 密码框（借用，控件树持有）
    balloonwjui::DuiEdit*     m_confirm;      // 再输一次（借用）
    balloonwjui::DuiComboBox* m_animation;    // 锁屏画面下拉框（借用）
    balloonwjui::DuiLabel*    m_error;        // 错误提示（借用）
    LockAnimationChoice       m_initialChoice;// 打开时选中的选择
    std::wstring              m_resultPassword;  // 确认时记下的密码；TakeResult 后清零
    LockAnimationChoice       m_resultChoice;    // 确认时记下的选择
    bool                      m_finished;     // 本次打开是否已经确认或取消（防止重复投递）
    CLockPreviewSession       m_preview;      // 全屏预览；不在预览时不持有窗口
    balloonmain::ProgramWindowIcons m_programIcons; // 标题栏与任务栏的程序图标（本对象持有，析构时释放）
};

} // namespace flamingolock
