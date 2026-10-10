/**
 *  「我的机器人」窗口 CMyBotsWindow（docs/机器人方案.md 4.3）：本人的个人机器人，只在别人发给本人的单聊里代答。
 *
 *  内容：
 *    - 打开时发 2806 拉取本人的个人机器人（每人最多 3 个，不含「离开 / 忙碌自动回复」的预置机器人）；
 *    - 每个机器人一行：「编辑」（机器人编辑窗）、「停用 / 启用」（改 status 后用 2807 保存整份）、「删除」（2808，先确认）；
 *    - 「新建机器人」：企业允许个人机器人且数量没满时可用；
 *    - 企业关闭了个人机器人（2806 的 allowed 为 0，或企业开关 botpersonal 关闭）时显示说明，新建、编辑与启用不可用；
 *    - 提示「离开 / 忙碌自动回复」在系统设置的「聊天」页设置。
 *
 *  窗口有所有者（主面板），按 UI 约定第 11 条用 WS_OVERLAPPEDWINDOW 建窗；标题栏有程序图标（第 10 条）。
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

//「我的机器人」窗口投给自己、留到当前控件通知处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x1B5）：
//wParam 为 MyBotsDeferred，lParam 为机器人在列表里的下标
const UINT kMsgBotMyBotsDeferred = WM_APP + 0x1B5;

//kMsgBotMyBotsDeferred 的 wParam
enum MyBotsDeferred
{
    mybots_defer_edit    = 1,   //点了「编辑」
    mybots_defer_toggle  = 2,   //点了「停用 / 启用」
    mybots_defer_delete  = 3,   //点了「删除」：先确认
    mybots_defer_new     = 4,   //点了「新建机器人」
    mybots_defer_close   = 5,   //点了「关闭」
};

//---- 控件 id ----
const UINT kIdMyBotsNew = 8401;       //「新建机器人」
const UINT kIdMyBotsClose = 8402;     //「关闭」
const UINT kIdMyBotsRowFirst = 9600;  //列表第 i 个机器人的按钮：kIdMyBotsRowFirst + i * kMyBotsRowIdStride + MyBotsRowButton
const UINT kIdMyBotsRowLast = 9699;   //列表按钮 id 的上限
const UINT kMyBotsRowIdStride = 4;    //每个机器人占的 id 个数

//列表一行上的按钮（id 偏移）
enum MyBotsRowButton
{
    mybots_row_edit   = 0,   //编辑
    mybots_row_toggle = 1,   //停用 / 启用
    mybots_row_delete = 2,   //删除
};

/**
 *  CMyBotsWindow —— 「我的机器人」窗口。只在界面线程上使用。
 */
class CMyBotsWindow : public CBotDialogBase, public IBotTopWindow, public IBotReplySink, public IBotEditHost
{
public:
    /**
     *  @param owner 模块（借用）
     */
    explicit CMyBotsWindow(IBotWindowOwner* owner);
    virtual ~CMyBotsWindow();

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

    BEGIN_MSG_MAP(CMyBotsWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgBotMyBotsDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：关闭窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：关掉编辑窗、撤销应答接收方，用户关闭时通知模块；不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮单击（会重建列表或打开窗口的动作一律延后执行）
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgBotMyBotsDeferred：执行投给自己的动作
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //发 2806 重新拉取
    void Reload();

    //按当前数据重建列表与底部按钮的可用状态
    void RebuildList();

    //企业是否允许个人机器人（2806 的 allowed 与企业开关都打开）
    bool Allowed() const;

    //编辑、停用 / 启用、删除、新建
    void EditBot(size_t index);
    void ToggleBot(size_t index);
    void DeleteBot(size_t index);
    void NewBot();

    //关闭窗口（用户操作）
    void CloseByUser();

private:
    IBotWindowOwner*            m_owner;          //模块（借用）
    uint32_t                    m_clientId;       //在模块登记的应答接收方编号；窗口没有打开时为 0
    bool                        m_loaded;         //已经收到过 2806 的应答
    bool                        m_allowed;        //2806 的 allowed：企业允许个人机器人
    int                         m_max;            //2806 的 max：每人最多个人机器人数
    std::vector<BotRecord>      m_bots;           //本人的个人机器人（含规则）
    uint32_t                    m_loadSeq;        //发出的 2806 的 seq；没有在加载时为 0
    std::map<uint32_t, int64_t> m_opSeqs;         //发出的 2807（停用 / 启用）与 2808：seq → 机器人 id
    ScrollFrameControls         m_frame;          //骨架里的控件（m_buttons 依次为新建、关闭；窗口销毁后失效）
    CBotEditWindow              m_editor;         //机器人编辑窗，本对象持有
    bool                        m_closingByHost;  //正在由模块关闭（Close）：WM_DESTROY 不再通知
};

} // namespace flamingobot
