/**
 *  BalloonMsgBoxWindow.h
 *
 *  「消息盒子」窗口（DuiFrameWindow 外壳，docs/消息提醒模式与消息盒子方案.md §4.5）：列出提醒模式为
 *  「收进消息盒子且不提醒」的会话。单例、非模态、可缩放，由主面板按值持有；已经打开时再点会话
 *  列表里的「消息盒子」那一行，只把窗口提到前台。
 *
 *  本窗口不持有会话数据：列表里的每一行由主面板用填会话行的同一段代码（FillConversationRow）
 *  填写，点击、右键、顶部两个按钮都回投给主面板处理 —— 会话缓存、模式表、socket 都在主面板手上，
 *  与群资料对话框（CBalloonGroupInfoDlg）同一做法。客户区控件树由 MsgBoxLayout 构造。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  典型用法（CBalloonMainPanel::OpenMsgBoxWindow）：
 *      m_msgBoxWindow.Open(this);
 *      FillMsgBoxWindowList();   //主面板往 GetTree() 里填行，再 ApplyState
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

class CBalloonMainPanel;

namespace balloonmain { class MainPanelTreeView; }

/**
 *  CBalloonMsgBoxWindow —— 消息盒子窗口。继承 DuiFrameWindow（无边框自绘窗体与标题栏）。
 *  关闭只销毁本窗，下次打开重新建窗，由主面板重新填列表。
 */
class CBalloonMsgBoxWindow : public balloonwjui::DuiFrameWindow
{
public:
    // 构造：只初始化成员，不建窗（第一次 Open 时才建）。
    CBalloonMsgBoxWindow();

    // 析构：释放标题栏图标位图；HWND 与客户区控件树由基类析构兜底。
    ~CBalloonMsgBoxWindow();

    /**
     *  打开（或前置）消息盒子窗口。已打开时只提到前台；未打开时创建、居中并显示。列表由调用方
     *  随后填写（GetTree + ApplyState）。
     *    pHost：主面板（点击与按钮回投给它；引用不持有，须保证生命周期覆盖本窗）。
     *  返回：窗口可用（已打开或刚创建成功）时为 true。
     */
    bool Open(CBalloonMainPanel* pHost);

    /**
     *  取会话列表控件，主面板往里填行。窗口没打开时返回 NULL。返回的指针归客户区控件树所有，
     *  只在本次窗口存续期间有效。
     */
    balloonmain::MainPanelTreeView* GetTree() const;

    /**
     *  主面板填完（或刷新完）列表之后调用：切换空状态、按未读决定「全部标为已读」是否可用、
     *  把滚动视口的内容高度设好并重绘。窗口没打开时什么也不做。
     *    convCount：  列表里的会话数（>= 0）。
     *    unreadTotal：这些会话的未读之和（>= 0）。
     */
    void ApplyState(int convCount, int unreadTotal);

    BEGIN_MSG_MAP(CBalloonMsgBoxWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //窗口尺寸变化后滚动条要给缩放边框让出的宽度可能变了；只旁听、不消费，DuiFrameWindow
        //还要靠它做客户区重排
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 关闭：销毁本窗，不影响主程序
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // DUI 通知分派：两个按钮的点击、列表行的单击与右键都回投主面板；其余交回基类
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 尺寸变化：重算滚动条给缩放边框让出的宽度，并放行给基类继续处理
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 按当前窗口与显示器 DPI 算出滚动条要给缩放边框让出的宽度并设给滚动视口
    void SyncScrollBarEdgeInset();

private:
    CBalloonMainPanel* m_pHost;        // 主面板：点击与按钮回投给它；引用不持有，Open 时设置
    HBITMAP            m_hTitleIcon;   // 标题栏左侧的程序图标位图；本类持有，析构时释放（DuiFrameWindow 只借用）
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
};
