/**
 *  屏幕共享的观看者名单浮窗，ShareViewerListPanel.h
 *
 *  第 13 期：发起人控制条与观看窗工具条上的「N 人观看 ▾」点开这个小窗，列出房间里的观看者（不含发起人），每人一行：
 *  名字、状态（正在说话 / 麦克风开着 / 麦克风已关），发起人一侧另有「静音」「移出」（第 14 期）按钮与顶部的「全员静音」。名单随每次 1725
 *  与说话人变化刷新。共享有描述时，名单最上方以灰字显示描述（第 15 期）。按钮点击不直接发信令，而是向通知窗口（屏幕共享控制器）PostMessage kMsgShareViewerListCommand，
 *  wParam 为 Command，lParam 为被操作的观看者的座位号。
 *
 *  形态：balloonui DuiFrameWindow 的 WS_POPUP 小窗，标题「观看者（N）」带关闭按钮，置顶、不进任务栏、不抢焦点，
 *  贴在「N 人观看 ▾」按钮下方，落点限制在工作区内。不设所有者窗口：2026-10-14 之前这样做是因为 balloonui 会把有所有者的
 *  WS_POPUP 窗口的控件通知发给所有者；现在控件通知一律发回本窗口（开发约定文档 UI 约定第 11 条），设不设所有者只看
 *  窗口关系（是否总在所有者之上、是否随它最小化、是否进任务栏）。发起人一侧排除在屏幕采集之外。
 *  内容全部用 balloonui 现成控件组合（DuiLabel、DuiButton、DuiScrollView），不新写自绘控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程）：
 *      m_viewerList.Show(buttonRect, m_hWnd, true, true);
 *      m_viewerList.SetRows(rows);   //每次 1725 或说话人变化后
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <vector>
#include "ShareRoomPolicy.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace screenshare {

//观看者名单浮窗
class CShareViewerListPanel : public balloonwjui::DuiFrameWindow
{
public:
    //按钮命令：随 kMsgShareViewerListCommand 的 wParam 发给控制器
    enum Command
    {
        kCmdMute    = 1,   //静音某个观看者，lParam 为其座位号
        kCmdMuteAll = 2,   //全员静音
        kCmdRemove  = 3,   //移出某个观看者（第 14 期），lParam 为其座位号；控制器先弹确认框
        kCmdControl = 4,   //「允许控制 / 收回控制」（第 18 期），lParam 为其座位号；控制器按他是否正在控制决定授予还是收回
    };

    CShareViewerListPanel();
    ~CShareViewerListPanel();

    /**
     *  在 anchor 下方打开名单（已打开时按新的锚点重新摆位）。
     *  @param anchor             「N 人观看 ▾」按钮在屏幕上的矩形
     *  @param notifyWnd          通知窗口，不拥有
     *  @param asHost             本人是否是发起人：是的话有「静音」与「全员静音」
     *  @param excludeFromCapture 是否排除在屏幕采集之外（发起人传 true）
     */
    void Show(const RECT& anchor, HWND notifyWnd, bool asHost, bool excludeFromCapture);

    //关闭名单；可重复调用
    void Hide();

    //名单是否开着
    bool IsShown() const;

    /**
     *  刷新名单。与上次相同时什么也不做；行数变了时窗口高度随之调整（左上角不动）。
     *  @param rows 观看者名单（见 BuildShareViewerRows）
     */
    void SetRows(const std::vector<ShareViewerRow>& rows);

    /**
     *  设置名单顶部显示的共享描述（第 15 期）。与上次相同时什么也不做；名单开着时重建内容并按描述行数调整高度。
     *  @param desc 共享描述，空串表示不显示描述行
     */
    void SetDescription(const std::wstring& desc);

    BEGIN_MSG_MAP(CShareViewerListPanel)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）；每行的「静音」从 kIdRowMuteBase 起按行号编
    enum
    {
        kIdMuteAll     = 10,    //全员静音
        kIdRowMuteBase = 100,   //第 0 行的「静音」
        kIdRowRemoveBase = 400, //第 0 行的「移出」（第 14 期）；与「静音」相隔 300，名单远到不了 300 行（房间上限 254 个座位）
        kIdRowControlBase = 700,//第 0 行的「允许控制 / 收回控制」（第 18 期）；与「移出」同样相隔 300
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按 m_rows 重建内容
    void buildUi();
    //按行数算出的整窗高度
    int panelHeight() const;
    //把窗口摆到 m_anchor 下方，放不下时挪进工作区
    void place();

    HWND                        m_notifyWnd;   //通知窗口（不拥有）
    bool                        m_asHost;      //本人是否是发起人
    RECT                        m_anchor;      //「N 人观看 ▾」按钮在屏幕上的矩形
    std::vector<ShareViewerRow> m_rows;        //当前显示的名单
    std::wstring                m_desc;        //顶部显示的共享描述（第 15 期），空表示不显示
    int                         m_descH;       //描述行按当前字体与内宽折行后的高度（像素），buildUi 时量出；无描述时为 0
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
