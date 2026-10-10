/**
 *  RelayComposeDlg.h
 *
 *  「发起接龙」窗口：群聊窗工具栏点接龙按钮后打开（docs/群接龙方案.md §3.1）。填写标题（必填）、
 *  说明（选填，多行）、截止时间（必填，年月日时分秒六个微调框，不能早于当前时间 + 5 分钟）与
 *  「我的接龙」（选填），点「发起」
 *  后以 cmd_send_message（msgtype = 11）发出卡片；收到发送回执、拿到卡片 msgid 之后，若「我的接龙」
 *  不为空，自动补发一次 cmd_join_relay，发起人因此排在第 1 位。
 *
 *  由群聊窗持有（每个群聊窗同时只开一个），是该群聊窗的模态窗口：以群聊窗为 owner 创建，打开期间
 *  群聊窗不可操作，关闭后恢复（做法与 CBalloonClosePromptDlg 相同，判定复用 ClosePromptHostGuard）。
 *  不开嵌套的消息循环，所以窗口开着时发送回执照常送达：回执经主面板广播给各群聊窗，群聊窗再交给
 *  本窗口的 OnSendAck；失败（如被禁言 306）时在窗口里写出原因并保持打开，已填写的内容不丢。
 *  只用 balloonui 现成控件（DuiLabel / DuiEdit / DuiSpinBox / DuiButton / DuiVBox / DuiHBox），不改 balloonui。
 *
 *  典型用法（群聊窗点了工具栏的接龙按钮）：
 *      if (!m_relayComposeDlg) m_relayComposeDlg.reset(new CRelayComposeDlg());
 *      m_relayComposeDlg->Open(m_hWnd, m_groupId);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由
 *  stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include <vector>
#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "RelayComposeModel.h"                                   // RelayDeadlineFields
#include "../BalloonMain/ClosePromptDecision.h"                  // ClosePromptHostGuard（模态时禁用 / 恢复群聊窗）

namespace balloonwjui { class DuiLabel; class DuiEdit; class DuiButton; class DuiSpinBox; }

/**
 *  「发起接龙」窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 */
class CRelayComposeDlg : public balloonwjui::DuiFrameWindow
{
public:
    CRelayComposeDlg();
    ~CRelayComposeDlg();

    /**
     *  打开窗口；已打开时调到前台、保留已填写的内容。新打开时把截止时间填为当前时间加 1 小时。
     *  @param owner   群聊窗窗口句柄：作为本窗口的 owner，本窗口居中摆在它上面；打开期间它被禁用，
     *                 本窗口关闭或销毁时恢复。句柄无效时窗口照常打开，只是不模态
     *  @param groupId 要在哪个群里发起；须 >0
     *  @return 窗口已显示返回 true
     */
    bool Open(HWND owner, int64_t groupId);

    /**
     *  卡片的发送回执（群聊窗把主面板广播来的回执转给本窗口）。clientMsgId 不是本窗口发出的那张时
     *  不做任何事、返回 false。成功时：「我的接龙」不为空则补发 cmd_join_relay，然后关闭窗口；
     *  失败时：在窗口里写出原因、恢复「发起」按钮，窗口保持打开（窗口已被用户关掉时只记日志）。
     *  @param clientMsgId 回执里的客户端消息ID
     *  @param code        回执结果码，0 为成功，取值见 enum error_code
     *  @param msgid       成功时服务端分配的卡片 msgid；失败时忽略
     *  @return 是本窗口发出的那张卡片的回执返回 true
     */
    bool OnSendAck(const std::string& clientMsgId, int code, int64_t msgid);

    BEGIN_MSG_MAP(CRelayComposeDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //销毁时兜底恢复群聊窗，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口（owner 为群聊窗）、配置标题栏、搭好客户区。成功返回 true。
    bool BuildWindow(HWND owner);

    //模态化：禁用 owner（群聊窗）。owner 无效、或已被别的窗口禁用时不接管，判定见 ClosePromptHostGuard。
    void DisableOwner();

    //恢复 owner 为可操作。幂等，关闭、发起成功、销毁等各条路径都调用；须在 DestroyWindow 之前调，
    //否则系统把激活权交回 owner 时它还是禁用的，激活权会落到别的程序上。
    void RestoreOwner();

    //搭客户区控件树，捕获各控件指针。
    void BuildClient();

    //按当前时间设定年份范围（今年起 kDeadlineYearsAhead 年），并把六个微调框填为截止时间的初始值。
    void FillDefaultDeadline();

    //读出六个微调框里的年月日时分秒。手输的数字先写回微调框（DuiSpinBox 不会自动同步手输入），
    //超出范围的被限制到范围内。
    ballooncard::RelayDeadlineFields ReadDeadlineFields();

    //点「发起」：检查填写的内容，合法时发出卡片。
    void OnSubmit();

    //在按钮行上方写一行提示（检查不通过、发送失败的原因）；传空串清掉。
    void ShowError(const CString& text);

    //进入 / 退出「正在发起」状态：期间「发起」按钮置灰，避免重复发出。
    void SetSending(bool sending);

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    int64_t     m_groupId;              // 要在哪个群里发起；Open 时设定
    HWND        m_ownerHwnd;            // owner（群聊窗）的句柄；Open 时设定，窗口打开期间它被禁用
    balloonmain::ClosePromptHostGuard m_ownerGuard;   // 是否由本窗口禁用了 owner、关闭时要不要恢复
    // 已发出、还没收到回执的卡片的客户端消息ID；空=没有。窗口被关掉之后仍然保留，
    // 回执到达时照样补发「我的接龙」
    std::string m_pendingClientMsgId;
    std::string m_pendingEntry;         // 与 m_pendingClientMsgId 配对的「我的接龙」（UTF-8，已去掉首尾空白）；空=没填

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiEdit*     m_pTitle;      // 标题
    balloonwjui::DuiEdit*     m_pNote;       // 说明（多行）
    balloonwjui::DuiSpinBox*  m_pDeadlineYear;     // 截止时间：年
    balloonwjui::DuiSpinBox*  m_pDeadlineMonth;    // 截止时间：月
    balloonwjui::DuiSpinBox*  m_pDeadlineDay;      // 截止时间：日
    balloonwjui::DuiSpinBox*  m_pDeadlineHour;     // 截止时间：时
    balloonwjui::DuiSpinBox*  m_pDeadlineMinute;   // 截止时间：分
    balloonwjui::DuiSpinBox*  m_pDeadlineSecond;   // 截止时间：秒
    balloonwjui::DuiEdit*     m_pEntry;      // 我的接龙
    balloonwjui::DuiLabel*    m_pError;      // 提示行；为空时隐藏
    balloonwjui::DuiButton*   m_pSubmit;     // 「发起」

    // 标题栏左侧图标位图。DuiFrameWindow::SetIcon 只借用不释放，故本类持有、析构时 DeleteObject
    HBITMAP     m_hTitleIcon;
};
