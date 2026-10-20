/**
 *  屏幕共享的「共享说明」浮窗（第 15 期），ShareInfoPopup.h
 *
 *  观看窗标题栏的「共享说明」图标、或全屏工具条上的「共享说明」按钮点开这个小窗，显示共享标题（粗体）与描述（自动换行）。
 *  发起人共享中改了标题与描述（1734）时，已打开的浮窗随之更新。
 *
 *  形态与观看者名单浮窗（CShareViewerListPanel）一致：balloonui DuiFrameWindow 的 WS_POPUP 小窗，标题「共享说明」带关闭按钮，
 *  置顶（强制模式下观看窗全屏置顶，浮窗要压在它上面）、不进任务栏、不抢焦点，贴在锚点下方，落点限制在工作区内。
 *  不设所有者窗口。2026-10-14 之前这样做是因为 balloonui 会把有所有者的 WS_POPUP 窗口的控件通知发给所有者；现在控件
 *  通知一律发回本窗口（开发约定文档 UI 约定第 11 条），设不设所有者只看窗口关系（是否总在所有者之上、是否随它最小化、
 *  是否进任务栏）。
 *  内容全部用 balloonui 现成控件组合（DuiLabel），不新写自绘控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程）：
 *      if (m_infoPopup.IsShown()) m_infoPopup.Hide();
 *      else m_infoPopup.Show(anchorRect, title, desc);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace screenshare {

//「共享说明」浮窗
class CShareInfoPopup : public balloonwjui::DuiFrameWindow
{
public:
    CShareInfoPopup();
    ~CShareInfoPopup();

    /**
     *  在 anchor 下方打开浮窗（已打开时换内容并重新摆位）。
     *  @param anchor 锚点在屏幕上的矩形（标题栏右端一块，或全屏工具条上的按钮）
     *  @param title  共享标题
     *  @param desc   共享描述
     */
    void Show(const RECT& anchor, const std::wstring& title, const std::wstring& desc);

    /**
     *  已打开时更新内容（发起人改了标题与描述）；没打开时什么也不做。
     *  @param title 共享标题
     *  @param desc  共享描述
     */
    void SetContent(const std::wstring& title, const std::wstring& desc);

    //关闭；可重复调用
    void Hide();

    //浮窗是否开着
    bool IsShown() const;

    BEGIN_MSG_MAP(CShareInfoPopup)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按 m_title / m_desc 重建内容
    void buildUi();
    //把窗口摆到 m_anchor 下方，放不下时挪进工作区
    void place();

    RECT         m_anchor;   //锚点在屏幕上的矩形
    std::wstring m_title;    //当前显示的共享标题
    std::wstring m_desc;     //当前显示的共享描述
    int          m_descH;    //描述按当前字体与内宽折行后的高度（像素），buildUi 时量出；无描述时为 0
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
