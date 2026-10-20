/**
 *  远程桌面被控端的顶部条，RdControlledBar.h
 *
 *  本端作为被控端（cmd_rd_relay_info 下发 role=被控端）开始抓屏推流后，在主显示器顶部居中显示一条置顶工具条，让被控方
 *  随时知道谁在看、谁在控制，并能随手收回控制或结束远程桌面。
 *
 *  2026-10-12 起（docs/远程桌面控制权限与观看工具条方案.md 第 4.2 节）重写为屏幕共享控制条的样式：派生自浮动观看工具条
 *  CViewerFloatBar（深色底、30×30 方形图标按钮、悬停提示、「隐藏」「收起」），没有所有者、置顶、不抢焦点、不进任务栏，并排除在
 *  屏幕采集之外。左侧状态文字按控制状态分三种：「某某 正在查看你的桌面」「某某 申请控制你的电脑」「某某 正在控制你的电脑」；
 *  画面经直连链路传输时状态文字后面显示「P2P」徽章。按钮有两个：
 *    - 「允许控制 / 收回控制」（同一个按钮）：未授权时灰底「允许控制」，有人申请时橙底「允许控制」，已授权时蓝底「收回控制」；
 *    - 「结束远程桌面」：红底。
 *  点按钮不直接改控制状态，而是向通知窗口（远程桌面控制器的消息窗口）PostMessage kMsgRdControlledBarCommand（wParam 为
 *  按钮编号），由控制器发信令并在应答后调 SetControlState 刷新本条。
 *
 *  典型用法（远程桌面控制器里）：
 *      m_rdControlledBar.SetLinkPathSource(&m_rdStreamer);
 *      m_rdControlledBar.Open(peerName, m_hWnd);
 *      ...收到申请：m_rdControlledBar.SetControlState(net::rd_control_requested);
 *      ...会话结束：m_rdControlledBar.Close();
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <string>
#include <vector>
#include "../Share/ViewerFloatBar.h"   // CViewerFloatBar：浮动工具条（2026-10-12 起本条派生自它）
#include "../Share/ShareCornerToast.h"  // ShareToastTexts：被控端的控制申请提示复用屏幕共享的右下角提示

namespace remotedesktop
{
    class RdControlledStreamer;   //被控端推流器（只借用它查询链路路径，前向声明即可：它的头文件带进 winsock2.h）
}

//被控端顶部条上由本条给出的按钮编号（点击时作为 kMsgRdControlledBarCommand 的 wParam）
enum RdControlledBarButtonId
{
    kRdBarControl = screenshare::kViewerBarUserIdFirst,       //允许控制 / 收回控制：未授权或有人申请时授予，已授权时收回
    kRdBarEnd     = screenshare::kViewerBarUserIdFirst + 1,   //结束远程桌面：发 cmd_rd_close、停推流、关本条
};

/**
 *  被控端顶部条的状态文字（纯函数）。
 *  @param controlState 控制状态，取值见 net::rd_control_state；越界按未授权处理
 *  @param peerName     控制端的显示名；为空时用「对方」
 *  @return 已翻译的文字：未授权「某某 正在查看你的桌面」，申请中「某某 申请控制你的电脑」，已授权「某某 正在控制你的电脑」
 */
std::wstring RdControlledStatusText(int controlState, const std::wstring& peerName);

/**
 *  被控端顶部条的按钮清单（纯函数）：「允许控制 / 收回控制」与「结束远程桌面」，从左到右。
 *  @param controlState 控制状态，取值见 net::rd_control_state；越界按未授权处理
 *  @return 按钮清单；第一个按钮未授权时灰底、申请中橙底，提示都是「允许控制」，已授权时蓝底、提示「收回控制」
 */
std::vector<screenshare::ViewerBarButtonSpec> RdControlledBarButtons(int controlState);

/**
 *  被控端控制申请提示（右下角，复用 CShareCornerToast）的标题、按钮文字与命令消息（纯函数，2026-10-12）。
 *  @return 标题「控制申请」、主按钮「同意」、次按钮「拒绝」，命令消息 remotedesktop::kMsgRdControlRequestAnswer
 */
screenshare::ShareToastTexts MakeRdControlRequestToastTexts();

/**
 *  被控端控制申请提示的正文（纯函数，2026-10-12）。
 *  @param peerName 申请人（控制端）的显示名；为空时用「对方」
 *  @return 「某某 申请控制你的电脑」，与顶部条申请中的状态文字相同
 */
std::wstring RdControlRequestToastText(const std::wstring& peerName);

//被控端顶部条：主显示器顶部居中的置顶工具条，显示查看 / 控制状态，提供「允许控制 / 收回控制」与「结束远程桌面」
class CRdControlledBar : public screenshare::CViewerFloatBar
{
public:
    CRdControlledBar();
    ~CRdControlledBar();

    /**
     *  打开顶部条（已打开时按新的名字重建并重新摆位）。控制状态从未授权开始。
     *  @param peerName  控制端的显示名；为空时状态文字里写「对方」
     *  @param notifyWnd 按钮点击时 PostMessage(kMsgRdControlledBarCommand, 按钮编号, 0) 的窗口；不拥有
     */
    void Open(const std::wstring& peerName, HWND notifyWnd);

    //关闭并销毁顶部条；可重复调用
    void Close();

    /**
     *  刷新控制状态：状态文字与「允许控制 / 收回控制」按钮随之变化。没有打开时只记下。
     *  @param controlState 控制状态，取值见 net::rd_control_state
     */
    void SetControlState(int controlState);

    //当前的控制状态，取值见 net::rd_control_state
    int ControlState() const;

    /**
     *  登记查询链路路径的推流器：顶部条打开期间每秒问它一次，画面经直连链路传输时显示「P2P」徽章。在 Open 之前调用。
     *  @param streamer 被控端推流器（不拥有，须在顶部条打开期间一直有效）；NULL 表示不显示徽章
     */
    void SetLinkPathSource(const remotedesktop::RdControlledStreamer* streamer);

    BEGIN_MSG_MAP(CRdControlledBar)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(screenshare::CViewerFloatBar)
    END_MSG_MAP()

private:
    //每秒刷新一次「P2P」徽章；其余定时器交给基类（收起 / 隐藏的鼠标检查）
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按当前的名字与控制状态刷新状态文字与按钮
    void refresh();

    HWND         m_notifyWnd;      //按钮点击的通知窗口（远程桌面控制器的消息窗口，不拥有）
    std::wstring m_peerName;       //控制端的显示名，Open 时设置，用于状态文字
    int          m_controlState;   //控制状态，取值见 net::rd_control_state；Open 时复位为未授权
    const remotedesktop::RdControlledStreamer* m_linkSource;   //查询链路路径的推流器（不拥有）；NULL 时不显示徽章
};
