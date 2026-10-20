/**
 *  群机器人管理窗 CBotManagerWindow（docs/机器人方案.md 4.2）：群主在这里管理本群的机器人。
 *
 *  内容：
 *    - 企业机器人（来源「企业」）：只读；管理员允许时（canoff）可以「在本群关闭 / 在本群打开」（2805）；
 *    - 本群的群机器人：「编辑」（先用 2802 取详情，再打开机器人编辑窗）、「停用 / 启用」（2805）、「删除」（2804，先确认）；
 *    - 「新建群机器人」：企业允许群机器人（caps.groupbot）且数量没满时可用；
 *    - 企业允许外部服务时，底部说明 Webhook 地址的写法（令牌与密钥在编辑窗里看）。
 *  列表数据来自 exe 缓存的 2801 结果（IBotWindowOwner::GetGroupBots）；每个群机器人的规则用 2802 取来，显示用法说明。
 *  exe 有了新的 2801 结果时模块调 OnGroupBotsChanged，本窗口重读列表并重拉详情。
 *
 *  窗口有所有者（群聊窗），用 WS_OVERLAPPEDWINDOW 建窗，控件通知发回本窗口（开发约定文档 UI 约定第 11 条）；
 *  标题栏有程序图标（第 10 条）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Core/BotTypes.h"
#include "../Host/BotWindowApi.h"
#include "BotDialogBase.h"
#include "BotEditWindow.h"
#include "BotListLayout.h"

namespace flamingobot {

//群机器人管理窗投给自己、留到当前控件通知处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x1B2）：
//wParam 为 BotManagerDeferred，lParam 为机器人在列表里的下标
const UINT kMsgBotManagerDeferred = WM_APP + 0x1B2;

//kMsgBotManagerDeferred 的 wParam
enum BotManagerDeferred
{
    manager_defer_edit    = 1,   //点了某个群机器人的「编辑」
    manager_defer_toggle  = 2,   //点了「在本群关闭 / 打开」或「停用 / 启用」
    manager_defer_delete  = 3,   //点了「删除」：先确认
    manager_defer_new     = 4,   //点了「新建群机器人」
    manager_defer_refresh = 5,   //点了「刷新」
    manager_defer_close   = 6,   //点了「关闭」
};

//---- 控件 id ----
const UINT kIdManagerRefresh = 8101;   //「刷新」
const UINT kIdManagerNew = 8102;       //「新建群机器人」
const UINT kIdManagerClose = 8103;     //「关闭」
const UINT kIdManagerRowFirst = 9200;  //列表第 i 个机器人的按钮：kIdManagerRowFirst + i * kManagerRowIdStride + ManagerRowButton
const UINT kIdManagerRowLast = 9599;   //列表按钮 id 的上限（100 个机器人）
const UINT kManagerRowIdStride = 4;    //每个机器人占的 id 个数

//列表一行上的按钮（id 偏移）
enum ManagerRowButton
{
    manager_row_edit   = 0,   //编辑（群机器人）
    manager_row_toggle = 1,   //在本群关闭 / 打开（企业机器人）、停用 / 启用（群机器人）
    manager_row_delete = 2,   //删除（群机器人）
};

/**
 *  CBotManagerWindow —— 群机器人管理窗。只在界面线程上使用。
 */
class CBotManagerWindow : public CBotDialogBase, public IBotTopWindow, public IBotReplySink, public IBotEditHost
{
public:
    /**
     *  @param groupId   群号
     *  @param groupName 群名（标题用），可为空
     *  @param owner     模块（借用）
     */
    CBotManagerWindow(int64_t groupId, const std::wstring& groupName, IBotWindowOwner* owner);
    virtual ~CBotManagerWindow();

    //======== IBotTopWindow ========
    virtual bool Open(HWND owner) override;
    virtual void Close() override;
    virtual void SetGroupName(const std::wstring& groupName) override;
    virtual void OnGroupBotsChanged() override;
    virtual void OnFeaturesChanged() override;
    virtual void OnReconnected() override;

    //======== IBotReplySink ========
    virtual void OnBotReply(const BotReply& reply) override;
    virtual void OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds) override;

    //======== IBotEditHost ========
    virtual void OnBotSaved(const BotRecord& bot) override;
    virtual void OnEditWindowClosed() override;

    BEGIN_MSG_MAP(CBotManagerWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgBotManagerDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：关闭窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：关掉编辑窗、撤销应答接收方，用户关闭时通知模块；不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮单击（会重建列表或打开窗口的动作一律延后执行）
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgBotManagerDeferred：执行投给自己的动作
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //重读宿主缓存的列表；没有时请宿主拉取；有时为每个群机器人拉详情。最后重建列表
    void Reload();

    //为还没有详情的群机器人发 2802（只有群主拿得到）
    void RequestDetails();

    //按当前数据重建列表与底部按钮的可用状态
    void RebuildList();

    //点了某个群机器人的「编辑」：有详情时直接打开编辑窗，没有时先拉详情
    void EditBot(size_t index);

    //在本群关闭 / 打开（企业机器人）或停用 / 启用（群机器人）
    void ToggleBot(size_t index);

    //删除群机器人（先确认）
    void DeleteBot(size_t index);

    //新建群机器人
    void NewBot();

    //打开编辑窗
    void OpenEditor(const BotRecord& bot);

    //某个机器人是否有删除、关闭 / 打开的请求还没有应答（应答到达前该行按钮置灰）
    bool IsBusy(int64_t botId) const;

    //本群群机器人的个数
    int GroupBotCount() const;

    //企业是否允许本群的群机器人用外部服务（2801 的 caps 与企业开关都打开）
    bool ExternalAllowed() const;

    //窗口标题：「群机器人 · 群名」
    std::wstring WindowTitle() const;

    //关闭窗口（用户操作）
    void CloseByUser();

private:
    int64_t                      m_groupId;         //群号
    std::wstring                 m_groupName;       //群名（标题用）
    IBotWindowOwner*             m_owner;           //模块（借用）
    uint32_t                     m_clientId;        //在模块登记的应答接收方编号；窗口没有打开时为 0
    bool                         m_hasList;         //宿主有这个群的 2801 结果
    GroupBotList                 m_list;            //本群的机器人（宿主缓存的 2801 结果）
    std::map<int64_t, BotRecord> m_details;         //群机器人的详情（2802）：机器人 id → 机器人（含规则与密钥）
    std::map<uint32_t, int64_t>  m_detailSeqs;      //发出的 2802：seq → 机器人 id
    std::map<uint32_t, int64_t>  m_opSeqs;          //发出的 2804 / 2805：seq → 机器人 id（应答到达前该行按钮置灰）
    int64_t                      m_pendingEditId;   //点了「编辑」、等详情到达后打开编辑窗的机器人 id；没有时为 0
    ScrollFrameControls          m_frame;           //骨架里的控件（m_buttons 依次为刷新、新建、关闭；窗口销毁后失效）
    CBotEditWindow               m_editor;          //机器人编辑窗，本对象持有
    bool                         m_closingByHost;   //正在由模块关闭（Close）：WM_DESTROY 不再通知
};

} // namespace flamingobot
