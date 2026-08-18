/**
 *  BalloonKickoutDlg.h
 *
 *  "被踢下线"提示框 —— 账号在别处登录被顶下线（以及被停用 / 服务器维护等）时，
 *  贴着托盘图标弹出的<u>模态</u>小窗：一段说明文字 + 一个"确定"按钮。用户关掉它之后，
 *  CMainDlg 接着走登出序列回到登录界面（见 CMainDlg::OnV2Kickout）。
 *
 *  为什么是模态：被踢下线是必须被看见的事（原先只弹托盘气泡，用户反馈"效果不明显"）。
 *  balloonui 的 DuiFrameWindow 本身不带模态，故照 CBalloonLoginDlg 的做法手写模态消息
 *  循环（见 DoModal / EndModal）—— 这也是本工程里"balloonui 窗口要模态"的既有做法。
 *
 *  为什么贴着托盘：被顶下线时主面板可能已被最小化到托盘，用户的注意力多半在托盘那侧；
 *  落点走 PopupMenuPlacement 的 PlaceTrayAnchoredWindow，与菜单共用同一套夹取逻辑，
 *  保证整窗落在桌面工作区内（任务栏在下时自动翻到托盘图标左上方）。
 *
 *  为什么置顶：本框以 HWND_TOPMOST 显示，压在所有窗口（含别的程序）之上。被顶下线时
 *  用户多半正在别的程序里忙，提示框若混在窗口堆里就等于没提示。
 *
 *  控件全部为 balloonui 现成控件（DuiVBox / DuiHBox / DuiLabel / DuiButton），没有自绘
 *  控件，符合开发约定文档 "新增控件 / 布局须最大化复用 balloonui 能力"。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

/**
 *  CBalloonKickoutDlg —— 被踢下线提示框（模态，owner 一般为主面板）。
 *
 *  典型用法（见 CMainDlg::OnV2Kickout）：
 *      RECT rcTray = { 0 };
 *      m_TrayIcon.GetTrayIconRect(&rcTray);
 *      CBalloonKickoutDlg dlg;
 *      dlg.DoModal(hOwner, nReason, rcTray);
 *      // 返回后再走登出序列，回到登录界面
 */
class CBalloonKickoutDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonKickoutDlg();
    ~CBalloonKickoutDlg();

    /**
     *  模态显示提示框，直到用户点"确定"或关掉窗口才返回。
     *
     *    hWndParent：  owner 窗口句柄，模态期间被禁用、返回前恢复；可为 NULL
     *                  （主面板尚未创建或已销毁时传隐藏的 CMainDlg 即可）。
     *    reason：      被踢原因，取值见 BalloonMain/KickoutNotice.h 的 KickoutReasonValue；
     *                  未知 / 越界值会落到兜底文案，不会显示空白。
     *    trayIconRect：托盘图标的屏幕矩形（CTrayIcon::GetTrayIconRect 的产物）；传空矩形
     *                  表示没取到，此时窗口退回桌面工作区右下角。
     *
     *  @return 恒为 IDOK —— 本框只是告知，没有"取消"语义，点按钮与点标题栏关闭按钮
     *          等价，调用方拿到返回值后一律继续走回登录界面的流程。
     *
     *  副作用：期间跑一个嵌套消息循环（其它窗口消息照常分发），返回前销毁本窗口。
     */
    INT_PTR DoModal(HWND hWndParent, int reason, const RECT& trayIconRect);

    // WM_DUI_NOTIFY 路由"确定"按钮的点击；WM_CLOSE / WM_KEYDOWN 自行处理（都以
    // IDOK 结束模态）；其余消息经 CHAIN 交回 DuiFrameWindow（标题栏关闭按钮等）。
    BEGIN_MSG_MAP(CBalloonKickoutDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY,        OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE,             OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN,           OnKeyDownMsg)
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGING, OnWindowPosChangingMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_DUI_NOTIFY：只认"确定"按钮的 DUIN_CLICK，命中即结束模态。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_CLOSE：以 IDOK 结束模态循环。窗口本身由 DoModal 在循环退出后统一销毁。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_KEYDOWN：回车 / ESC 同样结束模态，键盘用户不必非得去点按钮。
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_WINDOWPOSCHANGING：置顶的最后一道兜底。
     *
     *  凡是要改 z 序的位置变更，一律把插入位置改回 HWND_TOPMOST —— 无论是别人调
     *  SetWindowPos 把本框往下压，还是基类 / 系统在某条路径上重排 z 序，本框都回到
     *  最上层。建窗时的 WS_EX_TOPMOST 与显示后的重申都可能被后来的操作推翻，只有这
     *  一道是持续生效的，故三者并存。
     *
     *  处理后置 bHandled=FALSE，让基类 DuiFrameWindow / DefWindowProc 继续处理本消息
     *  （它还要据此更新自己的尺寸状态），本处只改 z 序、不截断消息。
     */
    LRESULT OnWindowPosChangingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //配置窗体外观（标题、标题栏高度、只留关闭按钮、不可缩放、标题栏图标）。
    void ConfigureFrame();

    /**
     *  构建控件树（正文 + 底部按钮行），并算出容纳这段文字所需的客户区高度。
     *    reason：被踢原因，决定正文文案。
     *  @return 客户区应有的高度（像素）；正文行数不同则高度不同。
     */
    int BuildUi(int reason);

    /**
     *  把窗口摆到托盘图标旁边，并置顶（HWND_TOPMOST，压在所有窗口之上）。
     *  须在 ResizeClient 之后、ShowWindow 之前调用：前者保证用最终窗口尺寸算落点，
     *  后者保证窗口从第一帧起就在最前，不会先闪一下再跳上来。
     *    trayIconRect：托盘图标屏幕矩形，空矩形表示没取到。
     */
    void PlaceNearTray(const RECT& trayIconRect);

    //结束 DoModal 的模态消息循环（result 写入 DoModal 的返回值）。
    void EndModal(INT_PTR result);

private:
    bool    m_modalRunning;    //模态消息循环是否在运行；EndModal 置 false 后循环跳出
    INT_PTR m_modalResult;     //DoModal 的返回值缓存（本框恒为 IDOK）
    HBITMAP m_titleIconBmp;    //标题栏图标位图（程序图标合成到标题栏底色上），本类持有并释放
};
