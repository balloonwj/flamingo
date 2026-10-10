/**
 *  RelayDetailWindow.h
 *
 *  接龙详情窗：在群聊窗里点接龙卡片、卡片底部按钮或接龙系统提示行后打开（docs/群接龙方案.md §3.3）。
 *  显示标题、说明、状态行与完整名单；进行中时底部有本人那一条的输入框与「提交 / 修改」按钮、
 *  「撤回我的」按钮；发起人、群主、管理员另有「结束接龙」按钮；「复制为文本」始终可用。
 *
 *  每张卡片最多一个详情窗，由主面板按卡片 msgid 持有（CBalloonMainPanel::m_relayDetailWindows）。
 *  名单的变化一律等服务端推送 cmd_relay_changed_notify：主面板收到后调用 OnRelayChanged；操作应答
 *  由主面板按 msgid 转给 OnOpResult，失败时在窗口里写出原因。窗口只用 balloonui 现成控件组合
 *  （DuiLabel / DuiEdit / DuiButton / DuiScrollView / DuiVBox / DuiHBox / DuiSeparator），不改 balloonui。
 *
 *  显示什么由纯逻辑 ballooncard::BuildRelayDetailView 决定，本类只负责把结果放进控件、
 *  处理按钮与发请求。
 *
 *  典型用法（主面板收到 FMG_MSG_OPEN_RELAY_DETAIL 时）：
 *      std::unique_ptr<CRelayDetailWindow> wnd(new CRelayDetailWindow());
 *      wnd->Open(request, selfId, std::move(names));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由
 *  stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <memory>
#include <string>
#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "../../net/CardProtocolClient.h"                        // net::RelayCardContent / RelayChangedNotify
#include "RelayDetailModel.h"                                    // RelayDetailRequest / RelayDetailView
#include "../BalloonMain/ClosePromptDecision.h"                  // ClosePromptHostGuard（模态时禁用 / 恢复群聊窗）

namespace balloonwjui { class DuiLabel; class DuiEdit; class DuiButton; class DuiScrollView; class DuiVBox; class DuiHBox; }

//名单重建请求：窗口尺寸变化后投给自己，在事件解栈之后按新宽度重新折行。不在 WM_SIZE 里直接
//重建，理由同聊天记录查看窗的 WM_MERGED_VIEWER_REBUILD
#define WM_RELAY_DETAIL_REBUILD  (WM_APP + 0x67)

/**
 *  接龙详情窗。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 *  一个实例对应一张接龙卡片。
 */
class CRelayDetailWindow : public balloonwjui::DuiFrameWindow
{
public:
    CRelayDetailWindow();
    ~CRelayDetailWindow();

    /**
     *  打开详情窗；本窗口已打开时换成新内容并调到前台。新建窗口时以 request.m_ownerHwnd（发起打开的
     *  群聊窗）为 owner，打开期间禁用它，关闭或销毁时恢复，即该群聊窗的模态窗口。
     *  @param request 卡片定位信息与内容（contentJson 须非空、creatorId 须已知，由主面板补齐）
     *  @param selfId  当前登录用户 userid
     *  @param names   名字解析接口，所有权转交本窗口；不能为空
     *  @return 卡片内容解析成功、窗口已显示返回 true；解析失败时不建窗口，返回 false
     *  副作用：request.m_focusInput 为 true 且输入框可用时把焦点放到输入框上
     */
    bool Open(const ballooncard::RelayDetailRequest& request, int64_t selfId,
              std::unique_ptr<balloonchatwnd::IRelayNameResolver> names);

    /**
     *  收到本卡片的接龙变更推送：合并名单与状态并重画。msgid 不是本卡片时忽略。
     *  @param notify 已解析的推送；调用期间有效
     */
    void OnRelayChanged(const net::RelayChangedNotify& notify);

    /**
     *  本窗口发出的参与 / 撤回 / 结束请求有了应答（主面板按 msgid 转来）。
     *  成功时只清掉等待状态与错误提示，名单等推送再改；失败时在窗口里写出原因，
     *  被禁言（306）时另外把输入区切换为置灰状态。
     *  @param cmd  应答对应的命令：cmd_join_relay / cmd_withdraw_relay / cmd_close_relay
     *  @param code 服务端回的错误码，0 为成功
     */
    void OnOpResult(int cmd, int code);

    /**
     *  @return 本窗口对应的卡片 msgid；未打开过时为 0
     */
    int64_t CardMsgid() const;

    BEGIN_MSG_MAP(CRelayDetailWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        //尺寸变化只旁听、不消费：DuiFrameWindow 照常排版，本窗口另投一条重建请求
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        MESSAGE_HANDLER(WM_RELAY_DETAIL_REBUILD, OnRebuildMsg)
        //销毁时兜底恢复群聊窗，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口（owner 为群聊窗）、配置标题栏、搭好客户区。成功返回 true。
    bool BuildWindow(HWND owner);

    //模态化：禁用 owner（群聊窗）。owner 无效、或已被别的窗口禁用时不接管，判定见 ClosePromptHostGuard。
    void DisableOwner();

    //恢复 owner 为可操作。幂等；须在 DestroyWindow 之前调，理由同 CRelayComposeDlg::RestoreOwner。
    void RestoreOwner();

    //搭客户区控件树，捕获各控件指针。
    void BuildClient();

    //按 m_card 与当前时间重算 m_view，再把结果放进各控件（文字、显隐、可用状态），并重排。
    //  keepInput：true 时不改输入框里的文字（用户可能正在输入）；false 时预填本人那一条
    void ApplyView(bool keepInput);

    //按当前宽度重建名单：每条一个可折行的 DuiLabel，本人那条画成主题色。
    void RebuildEntries();

    //按标签当前宽度量出折行后的高度，写回它在父布局里的固定高度。标签须已排过一次版。
    //  label：要调整的标签；minHeight：最小高度（像素）
    void FitLabelHeight(balloonwjui::DuiLabel* label, int minHeight);

    //点「提交 / 修改」：检查输入后发 cmd_join_relay。
    void OnSubmit();

    //点「撤回我的」：发 cmd_withdraw_relay。
    void OnWithdraw();

    //点「结束接龙」：确认后发 cmd_close_relay。
    void OnCloseRelayClicked();

    //点「复制为文本」：把 BuildRelayCopyText 的结果放进剪贴板。
    void OnCopy();

    //在输入区下方写一行提示（错误原因、检查不通过的原因）；传空串清掉。
    void ShowError(const CString& text);

    //按卡片截止时间安排一次性定时器，到点把窗口重画为已截止；不需要时关掉定时器。
    void ScheduleExpiryTimer();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRebuildMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    // ---- 卡片与当前用户（Open 时设定，推送到达时更新 m_card）----
    ballooncard::RelayDetailRequest m_request;   // 定位信息：ctype / cid / msgid / 发起人 / 本人角色；内容另存 m_card
    net::RelayCardContent  m_card;               // 卡片当前内容，已合并收到过的推送
    int64_t                m_selfId;             // 当前登录用户 userid
    bool                   m_selfMuted;          // 本人是否被禁言。打开时不知道，按 false；收到 306 拒绝后置为 true
    std::unique_ptr<balloonchatwnd::IRelayNameResolver> m_names;   // 名字解析，本窗口持有
    ballooncard::RelayDetailView m_view;         // 最近一次 ApplyView 算出的显示内容

    // 已发出、还没收到应答的请求命令号（cmd_join_relay 等）；0=没有。期间三个操作按钮置灰，
    // 避免连点重复提交。收到应答（OnOpResult）后清零
    int                    m_pendingCmd;
    // 名单重建请求是否已投出、尚未处理。同一轮拖动里只投一条
    bool                   m_rebuildPending;
    // owner（群聊窗）的句柄；新建窗口时取自 request.m_ownerHwnd，窗口打开期间它被禁用
    HWND                   m_ownerHwnd;
    // 是否由本窗口禁用了 owner、关闭时要不要恢复
    balloonmain::ClosePromptHostGuard m_ownerGuard;

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiLabel*      m_pTitle;        // 标题
    balloonwjui::DuiLabel*      m_pNote;         // 说明，可折行；为空时隐藏
    balloonwjui::DuiLabel*      m_pStatus;       // 状态行
    balloonwjui::DuiScrollView* m_pScroll;       // 名单滚动视口
    balloonwjui::DuiLabel*      m_pHint;         // 输入区上方的说明（被禁言、已满）；为空时隐藏
    balloonwjui::DuiHBox*       m_pInputRow;     // 输入框 + 提交按钮这一行；接龙不在进行中时隐藏
    balloonwjui::DuiEdit*       m_pInput;        // 本人那一条的输入框
    balloonwjui::DuiButton*     m_pSubmit;       // 「提交 / 修改」
    balloonwjui::DuiLabel*      m_pError;        // 输入区下方的错误提示；为空时隐藏
    balloonwjui::DuiButton*     m_pWithdraw;     // 「撤回我的」
    balloonwjui::DuiButton*     m_pCloseRelay;   // 「结束接龙」

    // 标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只借用不释放，
    // 故本类持有、析构时 DeleteObject
    HBITMAP                m_hTitleIcon;
};
