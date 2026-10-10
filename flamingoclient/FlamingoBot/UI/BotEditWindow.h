/**
 *  机器人编辑窗 CBotEditWindow（docs/机器人方案.md 4.2、4.3），群机器人与个人机器人共用；布局见 BotEditLayout.h。
 *
 *  流程：
 *    - 群机器人管理窗或「我的机器人」窗口调 Open 打开（带入机器人，新建时传空的机器人），打开期间禁用打开它的窗口；
 *    - 名称、字形、底色变了：刷新头像预览；
 *    - 规则：添加、编辑（规则编辑窗 CBotRuleWindow）、上移、下移、删除（先确认），都只改本窗口里的内容；
 *    - 「试一试」：用编辑中的内容发 2809，显示命中的规则与回复预览，或没有回复的原因；
 *    - 「保存」：本地校验（ValidateBot）后，群机器人发 2803、个人机器人发 2807；成功时交给 IBotEditHost::OnBotSaved
 *      并关闭，失败时在底部说明原因；
 *    - 关闭前内容有改动且没有保存时先确认。
 *  会重建控件树的点击（规则卡片上的按钮、「保存」成功后关闭）一律投给自己延后执行（UI 约定第 13 条）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Core/BotTypes.h"
#include "../Host/BotWindowApi.h"
#include "BotDialogBase.h"
#include "BotEditLayout.h"
#include "BotRuleWindow.h"

namespace flamingobot {

//机器人编辑窗投给自己、留到当前控件通知处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x1B3）：
//wParam 为 BotEditDeferred，lParam 为规则下标（规则卡片上的按钮）
const UINT kMsgBotEditDeferred = WM_APP + 0x1B3;

//kMsgBotEditDeferred 的 wParam
enum BotEditDeferred
{
    edit_defer_add_rule    = 1,   //点了「添加规则」：打开规则编辑窗
    edit_defer_edit_rule   = 2,   //点了规则卡片上的「编辑」
    edit_defer_rule_up     = 3,   //点了「上移」
    edit_defer_rule_down   = 4,   //点了「下移」
    edit_defer_delete_rule = 5,   //点了「删除」：先确认
    edit_defer_try         = 6,   //点了「试一试」
    edit_defer_save        = 7,   //点了「保存」
    edit_defer_cancel      = 8,   //点了「取消」：有改动时先确认再关闭
    edit_defer_close_saved = 9,   //保存成功：关闭窗口
};

/**
 *  机器人编辑窗的结果接收方（群机器人管理窗、「我的机器人」窗口实现）。只在界面线程上调用。
 */
class IBotEditHost
{
public:
    /**
     *  保存成功（服务端已应答成功）。调用时编辑窗还开着，随后关闭。
     *  @param bot   保存的内容（m_botId 为保存后的 id，新建时为服务端新分配的 id）
     */
    virtual void OnBotSaved(const BotRecord& bot) = 0;

    //编辑窗被用户关闭了（保存成功或取消之后），窗口已销毁
    virtual void OnEditWindowClosed() = 0;

protected:
    ~IBotEditHost()
    {
    }
};

/**
 *  CBotEditWindow —— 机器人编辑窗（所有者为打开它的窗口，打开期间禁用所有者）。只在界面线程上使用。
 */
class CBotEditWindow : public CBotDialogBase, public IBotReplySink, public IBotRuleHost
{
public:
    /**
     *  @param owner 模块（借用）：发命令、选人
     *  @param host  结果接收方（借用，打开它的窗口保证比本对象存活久）
     */
    CBotEditWindow(IBotWindowOwner* owner, IBotEditHost* host);
    virtual ~CBotEditWindow();

    /**
     *  打开窗口。已经打开时只把窗口提到前面并返回 false。
     *  @param ownerWnd      所有者窗口
     *  @param bot           机器人（m_ownerType 为 2 群机器人或 3 个人机器人；m_botId 为 0 表示新建）
     *  @param groupId       群机器人所在的群；个人机器人传 0
     *  @param allowExternal 企业是否允许外部服务（群机器人看 2801 的 caps 与企业开关；个人机器人传 false）
     *  @return 窗口按参数打开返回 true
     */
    bool Open(HWND ownerWnd, const BotRecord& bot, int64_t groupId, bool allowExternal);

    //关闭窗口，不确认、不回调 OnEditWindowClosed（打开它的窗口关闭时用）
    void Close();

    //窗口是否开着
    bool IsOpen() const;

    //正在编辑的机器人 id（新建为 0）；没有打开时为 -1
    int64_t EditingBotId() const;

    //======== IBotReplySink ========
    virtual void OnBotReply(const BotReply& reply) override;
    virtual void OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds) override;

    //======== IBotRuleHost ========
    virtual void OnRuleEdited(int index, const BotRule& rule) override;
    virtual void OnRuleWindowClosed() override;

    BEGIN_MSG_MAP(CBotEditWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgBotEditDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4，按取消处理（有改动先确认）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：关掉规则编辑窗、撤销应答接收方、恢复所有者窗口，用户关闭时通知接收方；不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮单击与输入框、下拉框的变化
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgBotEditDeferred：执行投给自己的动作
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //把名称、简介、字形、底色读回 m_bot
    void HarvestBasics();

    //按名称、字形、底色刷新头像预览
    void RefreshAvatar();

    //打开规则编辑窗：index 为 -1 时添加
    void OpenRuleEditor(int index);

    //上移 / 下移一条规则
    void MoveRule(size_t index, bool up);

    //删除一条规则（先确认）
    void DeleteRule(size_t index);

    //发「试一试」
    void TryBot();

    //校验并保存
    void Save();

    //内容是否有改动（与打开时相比）
    bool IsDirty();

    //「取消」或关闭：有改动时先确认，确认后关闭
    void RequestClose();

    //关闭窗口（用户操作）：恢复所有者后销毁
    void CloseByUser();

    //窗口标题：「新建群机器人」「编辑群机器人 · 名称」等
    std::wstring WindowTitle() const;

    //请求发出中：「保存」「试一试」置灰
    void SetBusy(bool saving, bool trying);

private:
    IBotWindowOwner* m_owner;          //模块（借用）
    IBotEditHost*    m_host;           //结果接收方（借用）
    uint32_t         m_clientId;       //在模块登记的应答接收方编号；窗口没有打开时为 0
    BotRecord        m_bot;            //正在编辑的机器人（规则的改动直接改它；基本信息在用到时读回）
    std::string      m_original;       //打开时的内容（BotCodec::BotToSaveJson），判断有没有改动
    int64_t          m_groupId;        //群机器人所在的群；个人机器人为 0
    bool             m_allowExternal;  //企业是否允许外部服务
    uint32_t         m_saveSeq;        //发出的保存请求的 seq；没有在保存时为 0
    uint32_t         m_trySeq;         //发出的「试一试」请求的 seq；没有在判定时为 0
    EditControls     m_controls;       //控件树里的控件（归控件树所有，窗口销毁后失效）
    CBotRuleWindow   m_ruleWindow;     //规则编辑窗，本对象持有
    bool             m_closingByHost;  //正在由打开它的窗口关闭（Close）：WM_DESTROY 不再通知
};

} // namespace flamingobot
