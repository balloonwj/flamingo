/**
 *  远程桌面控制端全屏工具条，RdViewerToolbar.h
 *
 *  控制端观看窗进入全屏看对方桌面时，标题栏被隐藏——原本标题栏上的连接状态显示、
 *  全屏 / 最小化 / 关闭按钮都不可见，全屏态只能靠 F11 / ESC 快捷键操作。本工具条
 *  在全屏态浮于画面顶部居中，给出「连接状态文字 +『退出全屏』+『断开』」，让全屏
 *  态也有看得见的状态与操作入口；退出全屏时由观看窗隐藏本条。
 *
 *  本条不直接操作会话 / 全屏：按钮点击时向宿主观看窗 PostMessage 一条带命令码
 *  （ToolbarCmd）的私有消息，由观看窗执行退出全屏 / 断开（与标题栏按钮、F11 / ESC
 *  走同一套收尾逻辑），避免本条与观看窗的状态各管一份。
 *
 *  形态：与被控端受控提示条（CRdControlledBar）同款——基于 balloonui DuiFrameWindow
 *  的 WS_POPUP 无边框小窗，置顶（WS_EX_TOPMOST）、不抢焦点（WS_EX_NOACTIVATE）、不进
 *  任务栏（WS_EX_TOOLWINDOW）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里
 *  先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <string>
#include <vector>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify / DUIN_CLICK
#include "../Share/ShareTopBarCollapse.h"   // TopBarCollapse（收起 / 悬停展开，第 10 期 10c）

namespace balloonwjui
{
    class DuiLabel;   //状态文字标签（成员仅持裸指针引用，前向声明即可）
    class DuiButton;  //额外按钮（同上）
}

//控制端全屏工具条：全屏态浮于画面顶部，显示连接状态 + 退出全屏 / 断开按钮。
class CRdViewerToolbar : public balloonwjui::DuiFrameWindow
{
public:
    CRdViewerToolbar();
    ~CRdViewerToolbar();

    //按钮命令码：随私有消息的 wParam 发回宿主观看窗，由观看窗据此执行对应动作。
    enum ToolbarCmd
    {
        kCmdExitFullscreen = 1,   //点了"退出全屏"
        kCmdDisconnect     = 2,   //点了"断开"
        kCmdExtra          = 3,   //点了第一个额外按钮（SetExtraButtons 设了才有）；第 i 个额外按钮为 kCmdExtra + i
    };

    /**
     *  在指定显示器顶部居中显示工具条（幂等：重复调用重建内容并重新摆位）。
     *  @param mon       目标显示器；取其整屏矩形（rcMonitor，因全屏盖住任务栏）顶部
     *                   居中摆放。通常传观看窗所在显示器。NULL 时退回主显示器工作区。
     *  @param notifyWnd 宿主观看窗句柄；按钮点击时向它 PostMessage 命令消息。不拥有。
     *  @param cmdMsg    宿主接收命令的消息号（消息 wParam = ToolbarCmd 之一）。
     */
    void ShowOnMonitor(HMONITOR mon, HWND notifyWnd, UINT cmdMsg);

    //隐藏并销毁工具条（退出全屏 / 关窗时调）。幂等：未显示时为空操作。
    void Hide();

    /**
     *  刷新状态文字（观看窗每秒把"远程桌面 W×H N fps"之类塞进来）。
     *  @param text 状态文字；窗口未显示时为空操作。
     */
    void SetStatusText(const std::wstring& text);

    /**
     *  改工具条的标题与「断开」按钮的文字（屏幕共享观看窗复用本工具条时改成「屏幕共享」「离开」，2026-10-02 加入）。
     *  须在 ShowOnMonitor 之前调用；不调用时保持远程桌面原来的「远程桌面」「断开」。
     *  @param title          工具条标题
     *  @param disconnectText 「断开」按钮的文字
     */
    void SetLabels(const std::wstring& title, const std::wstring& disconnectText);

    /**
     *  设置「退出全屏」左边的一组额外按钮的文字（2026-10-03 加入，屏幕共享观看窗用作「麦克风」「摄像头」开关）。
     *  空表示没有额外按钮（远程桌面不调用，保持原样）。点第 i 个按钮发 kCmdExtra + i。ShowOnMonitor 之前调用决定有几个
     *  按钮；工具条已显示时个数不变只改文字（个数变了下次 ShowOnMonitor 生效）。
     *  @param texts 各按钮的文字
     */
    void SetExtraButtons(const std::vector<std::wstring>& texts);

    /**
     *  设置「退出全屏」「断开」两个固定按钮是否出现（第 13 期，屏幕共享强制模式下都不出现）；默认都出现。
     *  ShowOnMonitor 之前调用生效；工具条已显示时下次 ShowOnMonitor 生效。
     */
    void SetFixedButtonsVisible(bool showExitFullscreen, bool showDisconnect);

    /**
     *  取第 index 个额外按钮在屏幕上的矩形（第 13 期，屏幕共享观看者名单贴在「N 人观看 ▾」下方弹出）。
     *  @param out [出参] 屏幕坐标
     *  @return 工具条已显示、展开着且该按钮存在时返回 true
     */
    bool GetExtraButtonScreenRect(size_t index, RECT& out) const;

    //控件 id：避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3，从 >= 10 起编。
    enum { kExitFsBtnId = 10, kDisconnectBtnId = 11, kExtraBtnId = 12 };   //第 i 个额外按钮的 id 为 kExtraBtnId + i
    //「收起 / 固定」按钮的 id：放在额外按钮的编号段之后，避免与 kExtraBtnId + i 重号
    enum { kToggleBtnId = 40 };

    BEGIN_MSG_MAP(CRdViewerToolbar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //收起 / 临时展开状态下每 kTopBarTickMs 检查一次鼠标是否在工具条（或小把手）上；其余定时器交给基类
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按 m_collapse 的状态摆放窗口：展开时为 m_expandedRect，收起时缩成顶部的小把手；同时刷新按钮文字与定时器
    void applyCollapse();

    //DUI 通知：退出全屏 / 断开按钮点击（DUIN_CLICK）→ 向宿主 PostMessage 命令码；其余交基类。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //确保窗口已创建（首次 Create + 设外观）。
    void EnsureWindow();
    //构建客户区：深色横条 [状态文字][退出全屏][断开]。
    void BuildUi();
    //按当前语言的按钮文字算出各按钮宽度，写入 m_xxxBtnW（多语言第 4 期：译文较长时只增不减地加宽）
    void ComputeButtonWidths();

private:
    HWND                    m_notifyWnd;    //宿主观看窗（点按钮时 PostMessage 目标）；不拥有
    UINT                    m_cmdMsg;       //发给宿主的命令消息号
    balloonwjui::DuiLabel*  m_statusLabel;  //状态文字标签（控件树持有所有权，本指针仅引用）
    HBITMAP                 m_bgBitmap;     //纯色背景图（铺满 host 背景，配透明标题栏成无缝一整条）；本类生成、析构释放
    std::wstring            m_title;        //工具条标题，默认「远程桌面」
    std::wstring            m_disconnectText; //「断开」按钮的文字，默认「断开」
    std::vector<std::wstring>            m_extraTexts;     //额外按钮的文字；为空表示没有额外按钮
    std::vector<balloonwjui::DuiButton*> m_extraButtons;   //额外按钮（控件树持有所有权，本指针仅引用）
    balloonwjui::DuiButton*              m_toggleBtn;      //「收起 / 固定」按钮（控件树持有所有权，本指针仅引用）
    screenshare::TopBarCollapse          m_collapse;       //收起 / 悬停展开的状态；每次 ShowOnMonitor 回到一直展开
    RECT                                 m_expandedRect;   //展开时的窗口矩形（屏幕坐标），收起时据此算小把手的位置
    bool                                 m_showExitFs;     //是否有「退出全屏」（第 13 期；强制模式下没有）
    bool                                 m_showDisconnect; //是否有「断开 / 离开」（同上）
    int                                  m_exitFsBtnW;     //「退出全屏」按钮宽（像素），由 ComputeButtonWidths 按文字算出
    int                                  m_disconnBtnW;    //「断开 / 离开」按钮宽（像素），同上
    int                                  m_extraBtnW;      //每个额外按钮的宽（像素），按各额外按钮文字中最宽的一个算出
    int                                  m_toggleBtnW;     //「收起 / 固定」按钮宽（像素），按两种文字中较宽的一个算出
};
