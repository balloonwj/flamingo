/**
 *  屏幕共享发起人一侧的摄像头浮动窗口，ShareCameraWindow.h
 *
 *  发起人的屏幕正被共享，摄像头小窗不能占用被共享的画面，所以放在一个单独的小窗口里：贴在被共享显示器工作区的
 *  右边缘，置顶、不抢焦点、不进任务栏，并排除在一切屏幕采集之外（docs/屏幕共享与录制方案.md 5.2 第 3 步）。
 *  房间里有人开摄像头（含本人）时显示，都关掉后隐藏。内容是 CShareCameraPanel。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <vector>
#include "ShareCameraPanel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

namespace screenshare {

//发起人一侧的摄像头浮动窗口
class CShareCameraWindow : public balloonwjui::DuiFrameWindow
{
public:
    CShareCameraWindow();
    ~CShareCameraWindow();

    //画面来源（屏幕共享控制器）；不拥有
    void SetSource(IShareCameraFrames* source);

    /**
     *  更新要显示的小窗：有内容时显示（贴在 monitorRect 所在显示器工作区的右边缘），没有时隐藏。
     *  @param tiles         小窗（含本人）
     *  @param overflowCount 超出同屏上限的人数
     *  @param monitorRect   被共享内容的矩形，窗口贴在它所在的显示器上
     */
    void Update(const std::vector<ShareCameraTile>& tiles, int overflowCount, const RECT& monitorRect);

    //关闭并销毁窗口；可重复调用
    void Close();

    BEGIN_MSG_MAP(CShareCameraWindow)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //关闭按钮只隐藏窗口（摄像头仍开着，下次有变化时再显示）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void EnsureWindow();

    CShareCameraPanel*  m_panel;    //小窗面板（控件树持有，本指针仅引用）
    IShareCameraFrames* m_source;   //画面来源（不拥有）
};

} // namespace screenshare
