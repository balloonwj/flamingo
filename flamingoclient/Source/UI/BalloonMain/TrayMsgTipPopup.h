/**
 *  TrayMsgTipPopup.h
 *
 *  鼠标悬停在系统托盘图标上时弹出的"未读消息提示"小列表。列出当前正在
 *  托盘闪烁的未读会话（每条：彩色字母头像 + 会话名 + "N 条新消息"），点某条即打开
 *  对应会话。取代旧的 SkinLib 版 CMsgTipDlg —— 旧版数据绑死 v1 的 CMessageList，
 *  这里用 balloonui 重写，数据直接来自托盘控制器持有的闪烁状态机（CTrayFlashController）。
 *
 *  窗口形态：与 CSearchDropdown 同一套路 —— 顶层无边框浮窗（WS_POPUP），窗口类带
 *  CS_DROPSHADOW 系统投影使其明显浮于内容之上；WS_EX_NOACTIVATE 故弹出 / 点击都<u>不
 *  抢</u>前台与键盘焦点。宿主能力复用 balloonui DuiHost（SubclassWindow 接管自建 HWND）。
 *  通知自路由（owner 传 NULL）：行点击在本类 OnChildNotify 内处理，经回调把 (ctype,cid)
 *  交回托盘控制器打开会话。
 *
 *  自动隐藏：展示后起一个轮询定时器，每拍检查光标是否仍落在"弹窗矩形 ∪ 托盘图标矩形"
 *  内；都不在则隐藏（与旧 CMsgTipDlg 的 leave 轮询同思路，避免一移开就消失导致点不到）。
 *
 *  落点：锚定在托盘图标矩形<u>上方</u>（托盘在任务栏，须向上弹），并统一经
 *  balloonmain::ClampMenuOriginToRect 夹进显示器工作区，保证整窗可见（开发约定文档「UI
 *  约定」第 3 条：任何弹层落点都要先夹取，不允许直接用原始锚点）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "TrayFlashController.h"      // balloonmain::TrayFlashEntry（ShowForEntries 入参）

#include <vector>
#include <map>
#include <utility>

namespace balloonwjui { class DuiControl; class DuiScrollView; }

/**
 *  CTrayMsgTipPopup —— 托盘悬停未读消息提示浮窗。
 */
class CTrayMsgTipPopup : public balloonwjui::DuiHost
{
public:
    // 选中某会话行时的回调：ctype 会话类型（1 单聊 2 群聊），cid 会话 ID。
    // userdata 为注册回调时透传的用户数据（一般为托盘控制器 this）。
    typedef void (*PickConvCallback)(void* userdata, int ctype, long long cid);

    CTrayMsgTipPopup();
    ~CTrayMsgTipPopup();

    /**
     *  创建本浮窗（幂等，仅首次真正创建）。
     *  @return 创建成功（或已创建）返回 true；失败返回 false。
     */
    bool EnsureCreated();

    /**
     *  设置行点击回调。
     *    cb：       点某会话行时触发。
     *    userdata： 回调透传的用户数据（一般为托盘控制器 this）。
     */
    void SetPickCallback(PickConvCallback cb, void* userdata);

    /**
     *  用给定未读会话集合重建行并把浮窗锚定在托盘图标上方展示。集合为空则隐藏。
     *    entries：           未读会话（一般为 CTrayFlashController::GetEntries()，最近在前）。
     *    trayIconScreenRect：托盘图标矩形（<u>屏幕坐标</u>）；既作落点锚点，也作自动隐藏
     *                        命中判断的一部分（光标落在托盘上时不隐藏）。
     */
    void ShowForEntries(const std::vector<balloonmain::TrayFlashEntry>& entries,
                        const RECT& trayIconScreenRect);

    /**
     *  隐藏浮窗（不销毁；下次 ShowForEntries 再展示），并停掉自动隐藏轮询定时器。
     */
    void HidePopup();

    /**
     *  浮窗当前是否可见。
     */
    bool IsShown() const;

    /**
     *  取浮窗 HWND（未创建返回 NULL）。
     */
    HWND GetHwnd() const { return m_hWnd; }

    BEGIN_MSG_MAP(CTrayMsgTipPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    // 自路由的 DUI 通知处理：会话行单击 → 回调 (ctype,cid) 并隐藏；其余放行。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // 自动隐藏轮询：光标离开"弹窗 ∪ 托盘图标"矩形则隐藏。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次构建控件树（白底卡片 + 结果滚动区）。
    void BuildContentOnce();
    // 按 entries 重建会话行，返回行数。
    int RebuildRows(const std::vector<balloonmain::TrayFlashEntry>& entries);
    // 按行数算浮窗高度（逻辑像素，含卡片留白与上限封顶）。
    int ComputeHeight(int rowCount) const;

private:
    bool                m_built;        // 控件树是否已构建
    PickConvCallback    m_pickCb;       // 行点击回调
    void*               m_pickUd;       // 回调用户数据
    balloonwjui::DuiScrollView* m_scroll;   // 结果滚动区（借用，树持有）
    RECT                m_trayIconRect; // 托盘图标屏幕矩形（自动隐藏命中判断用）
    DWORD               m_hideTimerId;  // 自动隐藏轮询定时器 id；0 表示未起

    // 会话行 → (ctype, cid)。每次 RebuildRows 重建前清空。
    std::map<balloonwjui::DuiControl*, std::pair<int, long long> > m_rowConv;
};
