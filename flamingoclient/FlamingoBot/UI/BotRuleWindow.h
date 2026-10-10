/**
 *  规则编辑窗 CBotRuleWindow（docs/机器人方案.md 3.5）：添加或编辑机器人的一条规则，群机器人与个人机器人共用。
 *
 *  流程：
 *    - 机器人编辑窗调 Open 打开（带入规则、归属与企业是否允许外部服务），打开期间禁用机器人编辑窗；
 *    - 换了触发方式、动作、定时种类、发送者范围或勾了「时间段」：先把各控件的值读回界面字段，再重建表单（延后执行，
 *      下拉框的通知是在浮层的消息里发出的，UI 约定第 13 条）；
 *    - 「选择…」：经模块请 exe 弹出选择联系人对话框，选好的人回到 OnContactsPicked；
 *    - 「确定」：界面字段解析、校验（FormToRule），不通过时在底部说明原因；通过后交给 IBotRuleHost::OnRuleEdited 并关闭。
 *  规则只在机器人编辑窗里改动，点机器人编辑窗的「保存」时才发给服务端。
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
#include "../Core/BotRuleForm.h"
#include "../Core/BotTypes.h"
#include "../Host/BotWindowApi.h"
#include "BotDialogBase.h"
#include "BotRuleLayout.h"

namespace flamingobot {

//规则编辑窗投给自己、留到当前控件通知处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x1B4）：wParam 为 BotRuleDeferred
const UINT kMsgBotRuleDeferred = WM_APP + 0x1B4;

//kMsgBotRuleDeferred 的 wParam
enum BotRuleDeferred
{
    rule_defer_rebuild = 1,   //换了触发方式、动作、定时种类、发送者范围或勾了「时间段」：读回字段后重建表单
    rule_defer_pick    = 2,   //点了「选择…」：弹出选择联系人对话框
    rule_defer_ok      = 3,   //点了「确定」：校验并交回
    rule_defer_cancel  = 4,   //点了「取消」：直接关闭
};

/**
 *  规则编辑窗的结果接收方（机器人编辑窗实现）。只在界面线程上调用。
 */
class IBotRuleHost
{
public:
    /**
     *  规则编辑好了（已通过本地校验）。调用时规则编辑窗还没关闭，接收方不要在这里关闭它。
     *  @param index 规则在列表里的下标；-1 表示新加的规则
     *  @param rule  规则
     */
    virtual void OnRuleEdited(int index, const BotRule& rule) = 0;

    //规则编辑窗被用户关闭了（确定或取消之后），窗口已销毁
    virtual void OnRuleWindowClosed() = 0;

protected:
    ~IBotRuleHost()
    {
    }
};

/**
 *  CBotRuleWindow —— 规则编辑窗（所有者为机器人编辑窗，打开期间禁用所有者）。只在界面线程上使用。
 */
class CBotRuleWindow : public CBotDialogBase, public IBotReplySink
{
public:
    /**
     *  @param owner 模块（借用）：选人
     *  @param host  结果接收方（借用，机器人编辑窗保证比本对象存活久）
     */
    CBotRuleWindow(IBotWindowOwner* owner, IBotRuleHost* host);
    virtual ~CBotRuleWindow();

    /**
     *  打开窗口。已经打开时只把窗口提到前面并返回 false。
     *  @param ownerWnd      所有者窗口（机器人编辑窗）
     *  @param index         规则在列表里的下标；-1 表示新加
     *  @param rule          规则（新加时传默认规则）
     *  @param ownerType     机器人的归属
     *  @param allowExternal 企业是否允许外部服务
     *  @return 窗口按参数打开返回 true
     */
    bool Open(HWND ownerWnd, int index, const BotRule& rule, int ownerType, bool allowExternal);

    //关闭窗口，不回调 OnRuleWindowClosed（机器人编辑窗关闭时用）
    void Close();

    //窗口是否开着
    bool IsOpen() const;

    //======== IBotReplySink ========
    virtual void OnBotReply(const BotReply& reply) override;
    virtual void OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds) override;

    BEGIN_MSG_MAP(CBotRuleWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgBotRuleDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4，按取消处理
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：撤销应答接收方、恢复所有者窗口，用户关闭时通知接收方；不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮单击与下拉框、复选框的变化（会改动控件树的动作一律延后执行）
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgBotRuleDeferred：执行投给自己的动作
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //读回各控件的值，按新的组合重建表单
    void Rebuild();

    //请模块弹出选择联系人对话框
    void PickSenders();

    //校验并交回；不通过时在底部说明原因
    void Submit();

    //关闭窗口（用户操作）：恢复所有者后销毁
    void CloseByUser();

    //「指定的人」的名字（已拼好）
    std::wstring SenderNamesText();

    //选择部门对话框确认了（IBotReplySink）：发送者范围改为这些部门，重建表单
    virtual void OnDepartmentsPicked(uint32_t requestId, const std::vector<int64_t>& deptIds) override;

private:
    IBotWindowOwner*    m_owner;          //模块（借用）
    IBotRuleHost*       m_host;           //结果接收方（借用）
    uint32_t            m_clientId;       //在模块登记的应答接收方编号；窗口没有打开时为 0
    uint32_t            m_pickRequest;    //等待中的选人请求号；没有时为 0
    int                 m_index;          //规则在列表里的下标；-1 表示新加
    int                 m_ownerType;      //机器人的归属
    bool                m_allowExternal;  //企业是否允许外部服务
    BotRuleForm         m_form;           //界面字段（重建表单之间的数据来源）
    ScrollFrameControls m_frame;          //骨架里的控件（归控件树所有，窗口销毁后失效）
    RuleFormControls    m_controls;       //表单里的控件（归控件树所有，重建或窗口销毁后失效）
    bool                m_closingByHost;  //正在由机器人编辑窗关闭（Close）：WM_DESTROY 不再通知
};

} // namespace flamingobot
