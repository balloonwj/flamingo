/**
 *  MemoFullscreenWindow.h
 *
 *  备忘的全屏提醒窗口（docs/备忘与提醒方案.md 1.3）：盖住主面板所在的显示器（主面板收在托盘时盖主显示器），
 *  深色背景淡入，标题由下往上滑入；「稍后提醒」「知道了」两个按钮，Esc 等同「知道了」。同时有几条全屏方式
 *  的提醒时逐条显示，点「知道了」换下一条。60 秒内没有任何操作（鼠标移动、按键）就收起，把还没处理的
 *  提醒交给中枢转到右下角弹窗继续提醒。
 *
 *  2026-10-04 起另有预览模式（ShowPreview）：编辑对话框持有自己的一个实例，按当前填写的内容播放同样的
 *  动画，顶部小字为「备忘提醒（预览）」。预览不经过备忘中枢：「稍后提醒」「知道了」、Esc、60 秒无操作
 *  都只关闭窗口，不登记稍后提醒、不标记已处理，关闭后把焦点还给编辑对话框。
 *
 *  窗口用 DuiFrameWindow::ToggleFullscreen 铺满显示器（隐藏标题栏），淡入用分层窗口的整窗透明度
 *  （WS_EX_LAYERED + SetLayeredWindowAttributes），滑入靠逐帧调整客户区根容器的上内边距。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <deque>
#include <string>
#include <vector>
#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "MemoViewModel.h"   // ReminderEntry / CFullscreenTimeout / FullscreenReminderText

namespace balloonwjui { class DuiLabel; class DuiVBox; }

/**
 *  备忘的全屏提醒窗口。
 */
class CMemoFullscreenWindow : public balloonwjui::DuiFrameWindow
{
public:
    //窗口投给自己的私有消息：按钮的点击通知处理完之后再执行按钮动作（动作可能销毁本窗口）。
    //wParam、lParam 均为 0，要执行的动作记在 m_pendingAction 里
    static const UINT kMsgDeferredAction = WM_APP + 0xA1;

    CMemoFullscreenWindow();
    ~CMemoFullscreenWindow();

    /**
     *  加入一条全屏提醒。窗口没显示时在 monitor 上显示出来并播放淡入；已显示时排在后面。
     *    entry：  提醒；同一条备忘已经在队列里时不重复加入；
     *    monitor：要盖住的显示器的整块矩形（屏幕坐标）。
     */
    void Enqueue(const balloonmemo::ReminderEntry& entry, const RECT& monitor);

    /**
     *  某条备忘被删除：从队列里拿掉；正显示的是它时换下一条，队列空了就收起。
     */
    void RemoveMemo(int64_t memoid);

    /**
     *  收起并清空队列（注销时中枢调用；编辑对话框关闭时也用它收起预览）。
     */
    void CloseWindow();

    /**
     *  以预览模式显示全屏提醒（编辑对话框的「预览」按钮）。已在预览时换上新内容并重新播放动画。
     *    text：       标题、时间、备注三行（BuildFullscreenReminderText 的结果）；
     *    creatorLine：创建人一行（群备忘才有），个人备忘与还没保存的群备忘传空串；
     *    monitor：    要盖住的显示器的整块矩形（屏幕坐标）；
     *    returnFocus：预览关闭后把前台窗口还给它（编辑对话框）；句柄无效时不处理。
     *  @return 窗口已显示返回 true；本实例的提醒队列不为空（不是预览专用的实例）或建窗失败时返回 false
     */
    bool ShowPreview(const balloonmemo::FullscreenReminderText& text, const std::wstring& creatorLine,
                     const RECT& monitor, HWND returnFocus);

    // 是否正以预览模式显示
    bool IsPreviewing() const { return m_preview; }

    BEGIN_MSG_MAP(CMemoFullscreenWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnUserActivity)
        MESSAGE_HANDLER(kMsgDeferredAction, OnDeferredAction)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    bool EnsureWindow();

    //把（还没显示的）窗口铺满 monitor、置顶，开始播放淡入与滑入，并开始 60 秒无操作的计时。
    //调用前队首的内容（或预览内容）须已就绪；返回窗口是否已显示
    bool Present(const RECT& monitor);

    //把队首的那条（预览模式下是预览内容）填进控件
    void ShowFront();

    //队首处理完（知道了或稍后提醒）：换下一条，没有了就收起
    void PopFront();

    //收起窗口（不清队列，调用方负责）
    void HideWindow();

    //播放淡入与滑入的一帧；返回动画是否已结束
    bool AnimateStep();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnUserActivity(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //对队首那条执行「知道了」或「稍后提醒」
    void FinishFront(bool snooze);

private:
    int   m_pendingAction;    //待执行的按钮动作：0 没有、1 知道了、2 稍后提醒、3 关闭预览
    std::deque<balloonmemo::ReminderEntry> m_queue;   //待显示的全屏提醒，队首是正显示的那条
    balloonmemo::CFullscreenTimeout m_timeout;        //60 秒无操作转为右下角弹窗的计时
    DWORD m_animStart;        //淡入动画开始的时刻（GetTickCount 毫秒）
    bool  m_animating;        //淡入动画是否在进行
    POINT m_lastMouse;        //上一次 WM_MOUSEMOVE 的位置：窗口刚盖上来时系统会补发一次原地的移动，不算操作
    bool  m_preview;          //是否为预览模式（ShowPreview 打开，HideWindow 时复位）
    balloonmemo::FullscreenReminderText m_previewText;   //预览模式下显示的标题、时间、备注
    std::wstring m_previewCreator;   //预览模式下的创建人一行，空串表示不显示
    HWND  m_returnFocus;      //预览关闭后要还回前台的窗口（编辑对话框），借用句柄，不持有

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiVBox*  m_pRoot;     //客户区根容器，滑入时调整它的上内边距
    balloonwjui::DuiLabel* m_pTitle;    //标题（大字）
    balloonwjui::DuiLabel* m_pTime;     //时间与所属群
    balloonwjui::DuiLabel* m_pCreator;  //创建人一行（群备忘才有，个人备忘为空串）
    balloonwjui::DuiLabel* m_pNote;     //备注
    balloonwjui::DuiLabel* m_pMore;     //「还有 N 条提醒」
    balloonwjui::DuiLabel* m_pCaption;  //顶部小字「备忘提醒」，预览模式下为「备忘提醒（预览）」
};
