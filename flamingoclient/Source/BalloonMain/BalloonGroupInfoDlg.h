/**
 *  BalloonGroupInfoDlg.h
 *
 *  「群资料」对话框窗口类。继承 balloonwjui::DuiFrameWindow,客户区由
 *  BalloonGroupInfoLayout::BuildGroupInfoBody 装入,数据由
 *  BalloonGroupInfoLayout::ApplyGroupInfoDataToView 注入(见同名 .cpp)。
 *
 *  非模态浮窗,由主面板 CBalloonMainPanel 在群会话右键菜单「查看群资料」时
 *  OpenForGroup 调出。同一实例可多次打开 —— 已 IsWindow 时直接前置,未创建则
 *  Create。设计稿 design/群资料.png。
 *
 *  交互(本轮端到端):
 *    · 消息免打扰 / 置顶群聊:两个 DuiSwitch,翻转即乐观更新 + 发
 *      cmd_set_conversation(1513);
 *    · 我在本群的昵称:整行点击就地编辑,失焦提交 cmd_set_member_card(1315);
 *    · 退出群聊:二次确认后发 cmd_leave_group(1309);
 *    · 查看消息记录:本轮占位(点击提示开发中)。
 *
 *  数据通路:主面板先用缓存(群名 / 成员数 / 免打扰 / 置顶)组 seed 打开,再发
 *  cmd_get_group_info(1303) + cmd_get_group_members(1304) 拉全(公告 / 群主显示名 /
 *  我的群名片),到达后主面板组更全的 GroupInfoViewData 调 ApplyData 刷进来。
 *
 *  本文件遵循 balloonui 头文件约定:include 它之前调用方须已引入 ATL/WTL
 *  (Flamingo 侧由 stdafx.h 保证)。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#pragma once

#include "BalloonGroupInfoLayout.h"   // GroupInfoViewData / GroupInfoCtrlId
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

class CFlamingoClient;
class CBalloonMainPanel;   // 写操作回主面板的宿主(更新会话缓存 / 退群清理);前置声明免引头

namespace balloonmain {

// 群名片"失焦提交"的去抖自投消息。DUIN_KILLFOCUS 不立即提交 —— 用户在编辑框内
// 重新点击会让 EDIT 短暂失焦再得焦,若此刻立即提交就会退回查看态、打不了字。改为
// PostMessage 本消息延后一拍,在 OnCardCommitDeferred 里检查焦点:若焦点又回到 EDIT
// 说明仍在编辑、跳过;只有焦点真正离开才提交。值取 WM_APP 段,与系统消息不冲突。
#define WM_GROUPINFO_CARD_COMMIT   (WM_APP + 0x21)

/**
 *  CBalloonGroupInfoDlg —— 「群资料」对话框。
 */
class CBalloonGroupInfoDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonGroupInfoDlg();
    ~CBalloonGroupInfoDlg();

    /**
     *  打开 / 前置群资料对话框。
     *    hParent     父窗口(通常主面板 HWND);用于窗口居中参照。
     *    pFMGClient  数据源 / 发协议入口;NULL 则不显示,本类<u>不持有</u>所有权。
     *    pHost       写操作回投的宿主主面板(更新会话缓存 / 退群清理);NULL=无宿主
     *                (调试入口),此时开关 / 退群退化为直接发协议、不更新缓存。本类不持有。
     *    seed        初始展示数据(主面板用缓存组好);随后 1303/1304 到达再 ApplyData 刷新。
     *  已 IsWindow 时:从最小化还原 + 前置,并用 seed 重刷;否则 Create 并装填 UI。
     */
    void OpenForGroup(HWND hParent, CFlamingoClient* pFMGClient,
                      CBalloonMainPanel* pHost, const GroupInfoViewData& seed);

    /**
     *  用更完整的数据刷新展示(主面板在 cmd_get_group_info / cmd_get_group_members
     *  应答到达后,组好 GroupInfoViewData 调本方法)。窗口未创建时只更新内部副本。
     *    data  新的展示数据;本类整份替换内部副本并重绘。
     */
    void ApplyData(const GroupInfoViewData& data);

    /**
     *  是否有一次"改群名片"正在等服务端应答。主面板据此把 cmd_set_member_card 应答
     *  交给本对话框处理(失败回退)。
     */
    bool HasPendingCardSave() const { return m_cardSavePending; }

    /**
     *  处理"改群名片"的服务端应答(由主面板转交)。成功:乐观更新已生效,清 pending;
     *  失败:从快照回退我的群名片、刷新查看态、弹错误提示。
     *    code  cmd_set_member_card 通用 {code,msg} 应答的结果码;0 为成功。
     *    msg   结果描述(UTF-16;失败时可展示给用户)。
     */
    void OnCardSaveResult(int code, const CString& msg);

    /**
     *  当前对话框所属群 id;0=未打开。主面板据此判断 1303/1304 应答是否要刷进本对话框。
     */
    __int64 GroupId() const { return m_data.groupid; }

    // WM_DUI_NOTIFY 自处理(开关 / 名片编辑 / 按钮)。其余消息经 CHAIN 交回
    // DuiFrameWindow。开关滑动动画不需要本对话框驱动,DuiAnimMgr 自带脉冲定时器。
    BEGIN_MSG_MAP(CBalloonGroupInfoDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_GROUPINFO_CARD_COMMIT, OnCardCommitDeferred)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    /**
     *  WM_DUI_NOTIFY 处理:两开关 VALUECHANGED、名片行 CLICK 进编辑、名片 edit
     *  KILLFOCUS 提交、退群 / 查看记录按钮 CLICK。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_LBUTTONDOWN:点击宿主客户区(DUI 区域)。群名片编辑态下,点子 EDIT 之外的任何
     *  地方都会到这里(点子 EDIT 子窗不经宿主) —— 此时把焦点移到宿主窗口逼子 EDIT 失焦,
     *  触发 KILLFOCUS→去抖提交,实现"点别处自动退出编辑并提交"。处理后 bHandled=FALSE,
     *  让 DuiFrameWindow/DuiHost 继续正常处理这次点击(如切换开关)。
     */
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_GROUPINFO_CARD_COMMIT(去抖自投):群名片编辑框失焦后延后一拍的提交检查。
     *  若焦点又回到了 EDIT 子窗(用户在框内重新点击)则跳过,仍留在编辑态;否则提交。
     */
    LRESULT OnCardCommitDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  进入群名片编辑态:翻转 view→edit、把 edit 文本设为当前名片、聚焦,置
     *  m_cardEditing。
     */
    void EnterCardEdit();

    /**
     *  提交群名片编辑(失焦触发):读 edit 文本,若较当前有变化则乐观更新 m_data
     *  并发 cmd_set_member_card;无论是否变化都切回查看态、清 m_cardEditing。
     */
    void CommitCardEdit();

    /**
     *  退群:二次确认(MessageBox YESNO),确认后发 cmd_leave_group 并关闭本对话框
     *  (真实列表清理 / 群聊窗关闭由主面板在退群应答到达时处理)。
     */
    void ConfirmAndLeave();

    /**
     *  Lazy 创建本对话框需要的位图资源(退群按钮浅红圆角底,三态;标题栏图标)。
     *  幂等,失败时单张 NULL,helper 内部相应跳过那一路视觉,不会崩。
     */
    void EnsureVisualResources();

    /**
     *  把退群按钮刷成"危险红"视觉:浅红圆角底 + 红字。资源未就绪时只保留红字兜底。
     */
    void ApplyQuitButtonStyle();

private:
    CFlamingoClient* m_lpFMGClient;   // 数据源 / 发协议入口,引用不持有
    CBalloonMainPanel* m_pHost;       // 写操作回投的宿主主面板(更新缓存 / 退群清理);NULL=调试入口,引用不持有
    HWND             m_hOwnerWnd;     // 父窗口(OpenForGroup 的 hParent),居中参照
    GroupInfoViewData m_data;         // 当前展示数据副本(开关 / 名片乐观更新就地改它)
    bool             m_cardEditing;   // true=群名片正处于就地编辑态(失焦提交的门控)
    bool             m_cardSavePending;  // true=有一次改群名片正在等服务端应答(OnCardSaveResult 收到后清)
    CString          m_cardSnapshot;     // 提交前的旧群名片快照(服务端拒绝时回退 m_data.myCard 用)

    HBITMAP m_titleIconBmp;     // 标题栏程序图标位图(本类持有,析构 DeleteObject)
    HICON   m_hTaskIconBig;     // 任务栏 / Alt-Tab 大图标(本类持有,析构 DestroyIcon)
    HICON   m_hTaskIconSmall;   // 任务栏 / 标题栏小图标(本类持有,析构 DestroyIcon)
    HBITMAP m_quitBg;           // 退群按钮浅红圆角底 normal(9-grid,本类持有)
    HBITMAP m_quitBgHover;      // 退群按钮浅红圆角底 hover(略深)
    HBITMAP m_quitBgPressed;    // 退群按钮浅红圆角底 pressed(更深)
    HBITMAP m_iconHistory;      // "查看消息记录"按钮左侧时钟图标(PARGB,本类持有)
    HBITMAP m_iconQuit;         // "退出群聊"按钮左侧垃圾桶图标(PARGB 红,本类持有)
};

} // namespace balloonmain
