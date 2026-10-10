/**
 *  「特别关注」上下线提示窗（右下角弹出），StarredPopupWindow.h
 *
 *  被特别关注的联系人上线 / 下线时，在桌面<u>右下角</u>弹一个置顶提示窗，让用户不必
 *  盯着主面板也能第一时间知道。内容是若干行「张三 已上线」，点某一行就打开与那个人
 *  的单聊。
 *
 *  与「新公告」提示窗（CAnnouncePopupWindow）的关系：外观风格与落点一致，落点计算
 *  两者共用 BalloonMain/CornerPopupPlacement.h 那一份；但内容形态不同（那个是一条
 *  公告的标题 + 正文，这个是一列人名），故另起一个类，没有强行合并。
 *
 *  三点与公告窗不同：
 *
 *  一、**能装多条**。同一时刻多人上下线时合并进<u>同一个</u>窗口、逐行列出，而不是弹一
 *      串窗口 —— 早高峰会变成弹窗轰炸。新事件追加进来并重置自动关闭的计时。行怎么排
 *      （去重、封顶、「等 N 人」）由纯函数 BuildStarredPopupRows 决定，见
 *      StarredPopupModel.h。
 *  二、**自动消失**。kAutoCloseMs 后自己收起；<u>鼠标悬停其上时暂停计时</u>——用户正在看
 *      的东西不该被抽走。移开后重新计时。
 *  三、**点行开单聊**。窗口自己不碰业务，把请求投给主面板既有的打开会话入口。
 *
 *  同一时刻只存在一个实例（主面板持有）。
 *
 *  落点：贴桌面工作区（rcWork，已排除任务栏）的右下角内缩一段边距，符合开发约定文档
 *  「UI 约定」第 3 条（弹层必须整体落在工作区内）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <stdint.h>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "StarredPopupModel.h"    // StarredPresenceEvent（成员按值持有，需完整类型）
#include "CornerPopupCountdown.h"  // CornerCountdown（标题后的倒计时，第 15 期）
#include "TitleBarIcon.h"          // ProgramWindowIcons（任务栏按钮的程序图标，第 15 期）

namespace balloonwjui {
class DuiListBox;
}

/**
 *  CStarredPopupWindow —— 特别关注的上下线提示窗（非模态、置顶浮窗、不抢焦点）。
 */
class CStarredPopupWindow : public balloonwjui::DuiFrameWindow
{
public:
    CStarredPopupWindow();
    ~CStarredPopupWindow();

    /**
     *  追加一条上下线事件并弹出（已经弹着则把新的一条并进去、重新计时）。
     *
     *    ownerPanel：主面板窗口句柄；点某一行时经它打开与那个人的单聊。
     *    userid：    发生变化的联系人 userid；<= 0 时本方法什么都不做。
     *    name：      显示名（备注优先，由主面板按通讯录缓存取好传进来）；
     *                空串时窗口内回落显示「用户<id>」。
     *    online：    true 上线、false 下线。
     *
     *  副作用：首次调用时建窗；每次调用都重新贴一次右下角、重置自动关闭计时。
     */
    void AddPresenceEvent(HWND ownerPanel, int64_t userid, LPCTSTR name, bool online);

    /**
     *  立刻收起窗口并清空已攒的事件。退出登录 / 断线时调，免得下次上线时把上一轮的
     *  旧提示又端出来。窗口没建过时为 no-op。
     */
    void HideAndClear();

    BEGIN_MSG_MAP(CStarredPopupWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //关闭按钮 / 系统关闭：只隐藏不销毁（本窗口由主面板持有并复用），同时清空事件。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //自动关闭计时器到点：鼠标不在窗口上就收起来。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //鼠标进入 / 移动：登记 WM_MOUSELEAVE 追踪并暂停自动关闭计时。
    LRESULT OnMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //鼠标离开窗口：重新开始计时。
    LRESULT OnMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //列表选中变化（单击某一行）：打开与那一行那个人的单聊。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建一次静态骨架（标题行 + 列表）。内容每次事件到达时改写，骨架不重建。
    void BuildUiOnce();

    //按当前攒下的事件重排列表内容，并把窗口高度调成刚好装下这些行。
    void RefreshRows();

    //重新开始自动关闭计时（每来一条新事件、或鼠标移开时调）。
    void RestartAutoCloseTimer();

    //停掉自动关闭计时（鼠标悬停时调）。
    void StopAutoCloseTimer();

    //按剩余时间刷新标题（「特别关注 · 5 秒后关闭」，第 15 期）；暂停中时停在暂停时的秒数。
    void RefreshTitle();

private:
    HWND m_panelHwnd;   //主面板窗口句柄（点行时请它打开单聊）；NULL=调试入口，点行只记日志
    bool m_built;       //骨架是否已构建
    bool m_hovering;    //鼠标当前是否在窗口上（决定计时器到点时收不收窗）

    //已攒下、尚未被用户看掉的事件，按到达先后排列。窗口收起时清空。
    std::vector<balloonmain::StarredPresenceEvent> m_events;

    //行列表（借用，控件树持有）。每行的 lParam 存该行对应的 userid。
    balloonwjui::DuiListBox* m_list;

    //标题栏图标位图（程序图标合成到标题栏底色上）。SetIcon 仅借用，本类持有、析构时释放。
    HBITMAP m_titleIconBmp;

    //任务栏按钮的程序图标（第 15 期，本窗口有任务栏按钮）。本对象持有，窗口先于它销毁。
    balloonmain::ProgramWindowIcons m_taskIcons;

    //自动关闭的倒计时（第 15 期）：新事件到达或鼠标移开时从头开始，鼠标停留时暂停。
    balloonmain::CornerCountdown m_countdown;
};
