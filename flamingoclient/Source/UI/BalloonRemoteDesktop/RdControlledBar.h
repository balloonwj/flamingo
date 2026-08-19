/**
 *  远程桌面被控端受控提示条，RdControlledBar.h
 *
 *  本端作为被控端（cmd_rd_relay_info 下发 role=被控端）开始抓屏推流后，在屏幕
 *  顶部居中浮一条醒目的置顶提示条：「您的桌面正在被 XXX 远程控制」+「结束控制」
 *  按钮，让被控方随时知情、并能随手主动终止远程控制。点「结束控制」时本条不直接
 *  操作推流器，而是向宿主（主面板）PostMessage 一条
 *  FMG_MSG_RD_CONTROLLED_END_BY_USER，由宿主统一收尾（发 cmd_rd_close + 停推流
 *  + 关本条），收尾逻辑与服务端推来的 cmd_rd_state_notify 收尾保持一处。
 *
 *  形态：基于 balloonui DuiFrameWindow——无最小化 / 最大化 / 关闭按钮、不可缩放、
 *  WS_POPUP 无边框小窗；置顶（WS_EX_TOPMOST）、不抢焦点（WS_EX_NOACTIVATE，避免
 *  从被控端正在操作的窗口偷走输入焦点）、不进任务栏（WS_EX_TOOLWINDOW）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp
 *  里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify / DUIN_CLICK

namespace balloonwjui
{
    class DuiLabel;   //提示文字标签（成员仅持裸指针引用，前向声明即可）
}

//被控端受控提示条：屏幕顶部置顶浮条，显示受控提示 + 结束控制按钮。
class CRdControlledBar : public balloonwjui::DuiFrameWindow
{
public:
    CRdControlledBar();
    ~CRdControlledBar();

    /**
     *  显示受控提示条（幂等：重复调用会重建内容并刷新文案 / 位置）。
     *  @param peerName  控制端昵称（宽字符；为空则文案退化为"正在被远程控制"）
     *  @param notifyWnd 通知宿主窗口句柄；点"结束控制"时向它 PostMessage
     *                   FMG_MSG_RD_CONTROLLED_END_BY_USER（宿主据此统一收尾）。
     *                   不拥有该句柄、不参与其生命周期管理。
     */
    void Show(const std::wstring& peerName, HWND notifyWnd);

    //隐藏并销毁提示条（会话结束 / 用户主动结束时调）。幂等：未显示时为空操作。
    void Hide();

    //结束按钮的控件 id：避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3，从 >= 10 起编。
    enum { kEndBtnId = 10 };

    BEGIN_MSG_MAP(CRdControlledBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //DUI 通知：结束按钮点击（DUIN_CLICK, ctrlId==kEndBtnId）→ 通知宿主收尾；其余交基类。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //确保窗口已创建（首次 Create + 设外观）。
    void EnsureWindow();
    //构建客户区：醒目橙底横条 [受控提示文字][结束控制按钮]。
    void BuildUi();
    //把窗口摆到（含本窗口的）显示器工作区顶部居中、不抢焦点地显示。
    void MoveToTopCenter();

private:
    HWND                    m_notifyWnd;   //通知宿主（点结束按钮时 PostMessage 的目标）；不拥有
    balloonwjui::DuiLabel*  m_tipLabel;    //受控提示文字标签（控件树持有所有权，本指针仅引用）
    std::wstring            m_peerName;    //控制端昵称（用于刷新提示文案）
    HBITMAP                 m_bgBitmap;    //纯色背景图（铺满 host 背景，配透明标题栏成无缝一整条）；本类生成、析构释放
};
