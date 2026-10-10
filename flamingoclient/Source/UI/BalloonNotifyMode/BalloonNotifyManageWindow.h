/**
 *  BalloonNotifyManageWindow.h
 *
 *  批量管理窗口「消息提醒管理」（DuiFrameWindow 外壳，docs/消息提醒模式与消息盒子方案.md §4.6）。
 *  单例、非模态，由主面板按值持有。两个页签：群聊（我加入的全部群）、单聊（聊过的人与设置过模式的人，
 *  搜索时在通讯录全体同事里找）。勾选若干会话后在底部选目标模式点「应用」，一次请求提交全部改动
 *  （cmd_set_notify_modes，超过 200 条由网络层分批）；失败的条目在行上标红并保留勾选，便于重试。
 *  也可以在某一行上右键单独设置。
 *
 *  本窗口不持有模式表与 socket：当前模式经主面板的 GetNotifyModeTable 读取，提交与单独设置都回投
 *  主面板执行，与消息盒子窗口同一做法。候选列表由主面板在打开时组好传入（ManagePools）。
 *  客户区控件树由 NotifyManageLayout 构造，候选、过滤、勾选与提交条目由 NotifyManageModel 计算。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  典型用法（CBalloonMainPanel::OpenNotifyModeManage）：
 *      balloonnotifymode::ManagePools pools;
 *      BuildNotifyManagePools(pools);
 *      m_notifyManageWindow.Open(this, initialCtype, pools);
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "NotifyManageModel.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

class CBalloonMainPanel;

namespace net { struct SetNotifyModesResult; }

/**
 *  CBalloonNotifyManageWindow —— 消息提醒管理窗口。关闭只销毁本窗；勾选与失败标记随之清空。
 */
class CBalloonNotifyManageWindow : public balloonwjui::DuiFrameWindow
{
public:
    // 构造：只初始化成员，不建窗（第一次 Open 时才建）。
    CBalloonNotifyManageWindow();

    // 析构：释放标题栏图标位图；HWND 与客户区控件树由基类析构兜底。
    ~CBalloonNotifyManageWindow();

    /**
     *  打开（或前置）窗口，换上最新的候选列表并切到指定页签。已打开时保留勾选与搜索词。
     *    pHost：        主面板（读模式表、提交设置；引用不持有，须保证生命周期覆盖本窗）。
     *    initialCtype： 先显示的页签：1 单聊、2 群聊；其它值按群聊。
     *    pools：        两个页签的候选来源（本窗口复制一份）。
     *  返回：窗口可用（已打开或刚创建成功）时为 true。
     */
    bool Open(CBalloonMainPanel* pHost, int initialCtype, const balloonnotifymode::ManagePools& pools);

    /**
     *  模式表变了（本机修改、多端同步推送、会话列表到达）之后，重画各行右端的当前模式。
     *  窗口没开时什么也不做。
     */
    void RefreshModes();

    /**
     *  收到一批设置的应答：失败的条目记下来（行上标红、保留勾选），成功的条目取消勾选并清掉
     *  失败标记。窗口没开时什么也不做。网络层分批提交时每批一个应答，逐批调用即可。
     *    result：应答解析结果；整单失败（m_code 非 0）时把本窗口最近一次提交的全部条目都记为失败。
     */
    void OnApplyResult(const net::SetNotifyModesResult& result);

    BEGIN_MSG_MAP(CBalloonNotifyManageWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //尺寸变化：滚动条给缩放边框让出的宽度随 DPI 变；只旁听、不消费
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 关闭：销毁本窗，清空勾选与失败标记
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // DUI 通知分派：页签切换、搜索框文字变化、全选、行的勾选与右键、应用；其余交回基类
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 尺寸变化：更新滚动条给缩放边框让出的宽度，并放行给基类
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 当前页签对应的会话类型：1 单聊、2 群聊
    int CurrentCtype() const;

    // 按当前页签与搜索词重算要列出的行并重建列表
    void RebuildList();

    // 把一行（节点 id）的勾选态、右端模式文字与颜色按当前数据画上去
    void ApplyRow(int nodeId);

    // 列表之外的状态：已选计数、全选框、应用按钮、截断提示、空状态
    void ApplyListState();

    // 点了一行（复选框或行身）：切换勾选
    void ToggleRow(int nodeId);

    // 点了「应用」：按勾选与目标模式生成条目，预检特别关注上限后交主面板提交
    void ApplySelection();

    // 某一行右键：弹出这一个会话的提醒模式菜单（与右键二级菜单相同的选项，不含批量管理）
    void ShowRowMenu(int nodeId);

    // 按当前窗口与显示器 DPI 算出滚动条要给缩放边框让出的宽度并设给滚动视口
    void SyncScrollBarEdgeInset();

private:
    CBalloonMainPanel*                     m_pHost;        // 主面板：读模式表、提交设置；引用不持有
    HBITMAP                                m_hTitleIcon;   // 标题栏左侧的程序图标位图；本类持有，析构时释放
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
    balloonnotifymode::ManagePools         m_pools;        // 两个页签的候选来源；每次 Open 时换成主面板给的最新一份
    std::vector<balloonnotifymode::ManageItem> m_visible;  // 当前列出的行，下标与树节点的添加顺序一致
    std::map<int, size_t>                  m_nodeIndex;    // 树节点 id → m_visible 下标；RebuildList 重建
    bool                                   m_truncated;    // 当前列表是否被截断（只列出了前 kNotifyManageMaxRows 行）
    balloonnotifymode::ManageSelection     m_selection;    // 勾选集合（两个页签共用，按 (ctype, cid) 区分）
    std::set<std::pair<int, int64_t> >     m_failed;       // 最近一次提交里失败的会话 (ctype, cid)：行上标红
    std::vector<std::pair<int, int64_t> >  m_lastSubmitted; // 最近一次提交的会话，整单失败时据此标红
};
