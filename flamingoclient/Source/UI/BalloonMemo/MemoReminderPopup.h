/**
 *  MemoReminderPopup.h
 *
 *  备忘的右下角提醒弹窗（docs/备忘与提醒方案.md 1.3）：贴桌面工作区右下角的置顶浮窗，不抢焦点。
 *  同时有多条时合并在一个窗口里逐条列出，每条有「10 分钟后再提醒」「知道了」两个按钮；错过的提醒合并成
 *  一行「你有 N 条错过的提醒」排在最后；多于一条时底部有「全部知道了」。没有关闭按钮，一直留着，点
 *  「知道了」才关（与用户确认）。内容由备忘中枢的提醒队列给出，按钮事件交回中枢。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <vector>
#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "MemoViewModel.h"   // ReminderEntry

/**
 *  备忘的右下角提醒弹窗。
 */
class CMemoReminderPopup : public balloonwjui::DuiFrameWindow
{
public:
    //窗口投给自己的私有消息：按钮的点击通知处理完之后再执行按钮动作。动作会重建整个控件树，
    //若在点击通知里直接执行，按钮所在的控件树在它自己的回调栈上就被释放了。wParam、lParam 均为 0，
    //要执行的动作记在 m_pendingAction / m_pendingMemoId 里
    static const UINT kMsgDeferredAction = WM_APP + 0xA0;

    CMemoReminderPopup();
    ~CMemoReminderPopup();

    /**
     *  按提醒队列的内容显示（已显示则就地换成新内容并重新贴角）。两个列表都为空时隐藏窗口。
     *    due：   到点的提醒，按加入顺序；
     *    missed：错过的提醒，按加入顺序。
     */
    void ShowEntries(const std::vector<balloonmemo::ReminderEntry>& due,
                     const std::vector<balloonmemo::ReminderEntry>& missed);

    /**
     *  隐藏并销毁窗口（注销时中枢调用）。
     */
    void CloseWindow();

    BEGIN_MSG_MAP(CMemoReminderPopup)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgDeferredAction, OnDeferredAction)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    bool EnsureWindow();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //记下按钮动作并投递 kMsgDeferredAction
    void PostAction(int action, int64_t memoid);

private:
    int     m_pendingAction;             //待执行的按钮动作，取值见 .cpp 的 PopupAction；0 表示没有
    int64_t m_pendingMemoId;             //待执行动作对应的备忘 ID
    std::vector<int64_t> m_rowMemoIds;   //当前列出的到点提醒各行对应的备忘 ID，按钮 id 按行号分配
    std::vector<int64_t> m_allMemoIds;   //当前列出的全部提醒的备忘 ID（「全部知道了」用）
    HBITMAP m_hTitleIcon;                //标题栏左侧图标位图；SetIcon 只借用，本类持有、析构时释放
};
