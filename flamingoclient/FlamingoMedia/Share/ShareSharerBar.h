/**
 *  多人共享的共享者控制条与主持人退出选择框（第 22 期，docs/屏幕共享与录制方案.md 第 2 节第 27～30 条），ShareSharerBar.h
 *
 *  - CShareSharerBar：主持人以外的人经同意共享自己的屏幕时，显示在被共享内容所在显示器顶部居中的一条小控制条：
 *    「你正在共享屏幕」与「停止共享」按钮（第 29 条：停的只是自己的画面，人还留在房间里继续观看）。排除在屏幕采集之外。
 *  - CShareHostExitDialog：主持人结束或离开时还有别人在共享（第 30 条），二选一：移交给其中一位正在共享的人（每人一个按钮，
 *    移交后原主持人离开房间），或结束整个共享；另有「取消」。
 *  两者的按钮都向通知窗口 PostMessage kMsgShareSharerCommand（wParam 为 ShareSharerCommand，移交时 lParam 为新主持人）。
 *  形态与控制条（CShareControlBar）同款：基于 balloonui DuiFrameWindow 的 WS_POPUP 小窗，置顶、不进任务栏。
 *  窗口不设所有者：balloonui 把按钮通知发给 GetParent 返回的窗口，弹出窗口设了所有者就会发到所有者那里。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "ShareBarPopupGuard.h"   // CShareBarPopupGuard：「更多」菜单开着时被隐藏的处理（2026-10-08）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace screenshare {

//共享者控制条与主持人退出选择框的命令（kMsgShareSharerCommand 的 wParam）
enum ShareSharerCommand
{
    share_sharer_stop     = 1,   //共享者控制条上点了「停止共享」：停掉自己的画面，留在房间里
    share_sharer_transfer = 2,   //主持人退出选择框里点了「移交给某人」：lParam 为新主持人，移交后离开房间
    share_sharer_end_all  = 3,   //主持人退出选择框里点了「结束共享」：结束整个共享
    share_sharer_cancel   = 4,   //主持人退出选择框里点了「取消」或关了窗
    share_sharer_annotate = 5,   //共享者控制条上点了「标注 / 退出标注」（第 22 期 22e）：在自己共享的内容上标注
    share_sharer_screen_board = 6,   //共享者控制条上点了「屏幕当白板 / 退出屏幕白板」（2026-10-07，方案第 14.1 节 ③）
    share_sharer_allow_capture = 7,  //共享者控制条「更多」菜单里点了「允许截图」（2026-10-08）：lParam 为 1 允许、0 恢复不允许
};

//共享者控制条
class CShareSharerBar : public balloonwjui::DuiFrameWindow
{
public:
    CShareSharerBar();
    ~CShareSharerBar();

    /**
     *  显示在 anchor 所在显示器工作区的顶部居中（重复调用会重新摆位）。
     *  @param anchor    被共享内容的矩形（屏幕坐标）
     *  @param notifyWnd 通知窗口，不拥有
     */
    void Show(const RECT& anchor, HWND notifyWnd);

    //隐藏并销毁；可重复调用
    void Hide();

    //刷新标注状态（第 22 期 22e）：标注中按钮显示「退出标注」，否则显示「标注」
    void SetAnnotating(bool on);

    /**
     *  「屏幕当白板」按钮（2026-10-07，方案第 14.1 节 ③）：共享者只共享一个窗口时才提供，点了向通知窗口发
     *  share_sharer_screen_board。按钮出现或消失时重建内容并重新摆位。
     *  @param available 是否提供（只共享一个窗口、或已在屏幕白板里）
     *  @param active    是否已在屏幕白板里：是的话按钮显示「退出屏幕白板」
     */
    void SetScreenBoard(bool available, bool active);

    //对外可见的控件 id（单元测试按它找按钮）
    enum
    {
        kIdScreenBoard = 12,   //屏幕当白板 / 退出屏幕白板（2026-10-07）
    };

    //投给自己的「弹出更多菜单」（2026-10-08）：等「更多」按钮的点击通知处理完再弹，菜单的嵌套消息循环不套在点击通知里；
    //菜单开着时被要求隐藏，先关掉菜单、只隐藏窗口，返回后再销毁（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgShowMoreMenu = WM_APP + 0xD6;

    BEGIN_MSG_MAP(CShareSharerBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgShowMoreMenu, OnShowMoreMenu)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdStop     = 10,   //停止共享
        kIdAnnotate = 11,   //标注 / 退出标注（第 22 期 22e）
        kIdMore     = 13,   //「更多」（2026-10-08）：弹出只有「允许截图」一项的菜单
    };
    //「更多」菜单的命令
    enum
    {
        kMenuAllowCapture = 101,   //允许截图（勾选项）
    };
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //kMsgShowMoreMenu：弹出「更多」菜单；菜单期间被要求隐藏时，菜单返回后在这里销毁窗口
    LRESULT OnShowMoreMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void BuildUi();
    //「屏幕当白板 / 退出屏幕白板」按钮的宽：按两种文字中较宽的一个，不小于 kScreenBoardW
    int screenBoardWidth() const;

    HWND    m_notifyWnd;   //通知窗口（不拥有）
    HBITMAP m_bgBitmap;    //纯色背景图（本类生成、析构释放）
    balloonwjui::DuiButton* m_annotButton;   //「标注 / 退出标注」按钮（控件树持有，本指针仅引用）；没建窗时为 NULL
    balloonwjui::DuiButton* m_moreButton;    //「更多」按钮（2026-10-08，控件树持有，本指针仅引用）；没建窗时为 NULL
    CShareBarPopupGuard     m_popup;         //「更多」菜单的弹出状态与推迟销毁（2026-10-08）
    bool    m_annotating;  //是否在自己共享的内容上标注（第 22 期 22e），按钮文字据此显示
    bool    m_screenBoardAvailable;   //是否提供「屏幕当白板」（2026-10-07）：只共享一个窗口、或已在屏幕白板里
    bool    m_screenBoardActive;      //是否已在屏幕白板里：按钮显示「退出屏幕白板」
    RECT    m_anchor;      //最近一次 Show 的参照矩形；按钮出现或消失时据此重新摆位
};

//主持人退出选择框
class CShareHostExitDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareHostExitDialog();
    ~CShareHostExitDialog();

    /**
     *  在 anchor 所在显示器的工作区居中打开；已打开时先关掉旧的（不发命令）。
     *  @param anchor     定位用的窗口，只用来确定显示器、不作为所有者；可以为 NULL
     *  @param notifyWnd  通知窗口，不拥有
     *  @param candidates 可以移交的人：userid 与显示名，按显示顺序
     */
    void Open(HWND anchor, HWND notifyWnd, const std::vector<std::pair<int64_t, std::wstring> >& candidates);

    //关闭（不发命令）；可重复调用
    void Close();

    BEGIN_MSG_MAP(CShareHostExitDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //按钮的控件 id：移交按钮从 kIdTransferBase 起按候选人的下标编
    enum
    {
        kIdEnd          = 10,   //结束共享
        kIdCancel       = 11,   //取消
        kIdTransferBase = 20,   //第一个「移交给某人」
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //发一条命令并关窗
    void finish(int command, int64_t userId);

    HWND                                           m_notifyWnd;    //通知窗口（不拥有）
    std::vector<std::pair<int64_t, std::wstring> > m_candidates;   //可以移交的人
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
