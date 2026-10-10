/**
 *  CreateGroupDlg.h
 *
 *  "选人类"对话框 —— 新建群聊 / 邀请入群 / 添加禁言成员 / 转让群主 / 选择共享对象
 *  几种用途共用同一套壳，用途见下面的 Purpose 枚举。类名沿用最早的"新建群聊"，
 *  职责已扩成"搜索 + 可勾选候选列表 + 右侧区 + 底部确认"这套通用骨架。
 *
 *  以下按最早的"新建群聊"用途描述 —— 对照 design/新建群.png。基于 balloonui DuiFrameWindow。
 *  左侧：搜索框 + 按部门分组的可勾选联系人列表（复选框 + 头像 + 姓名，复用
 *  balloonui DuiButton(复选框) / DuiAvatar / DuiLabel 组合，匹配设计稿）；右侧：
 *  群名输入（选填）+ 已选成员区（头像+姓名+✕，可移除）；底部：已选 N 人 + 取消 +
 *  创建群聊。点"创建群聊"经 cmd_create_group 发往服务端，应答回投后提示结果。
 *
 *  与主面板的关系：主面板"群"页底部"建群"按钮点击时创建并显示本对话框，把
 *  通讯录缓存（联系人 + 部门）与当前登录用户传入用于构建可勾选列表。建群应答
 *  由主面板转发到本对话框的 OnCreateGroupResult。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "MemberPickerTreeModel.h"   // MemberPickerNode（m_treeNodes 成员需完整类型）
#include "MemberPickerLayout.h"      // balloonmain::InviteHistoryControls（m_history 成员需完整类型）

#include <vector>
#include <map>
#include <string>
#include <stdint.h>
#include "TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

class CFlamingoClient;

namespace net { struct ContactItem; struct DepartmentItem; struct CreateGroupResponse; }
namespace balloonwjui { class DuiControl; class DuiEdit; class DuiScrollView;
                        class DuiLabel; class DuiButton; class DuiVBox; }
namespace balloonmain { class MemberPickerTreeView; }

/**
 *  选人结果的接收方（2026-10-03 起；目前只有文件共享宿主：「选择共享对象」用途确认后把已选的人交给它）。
 *  只在 UI 线程上调用。
 */
class IMemberPickSink
{
public:
    virtual ~IMemberPickSink()
    {
    }

    /**
     *  选人对话框确认：已选的人。调用时对话框已经关闭。
     *  @param userIds 已选成员的 userid，按勾选先后排列；只有 kPurposeGroupFollow（管理关注的成员）允许为空，
     *                 表示清空名单，其余用途不为空
     */
    virtual void OnMembersPicked(const std::vector<int64_t>& userIds) = 0;
};

/**
 *  CCreateGroupDlg —— 新建群聊对话框（非模态浮窗，owner = 主面板）。
 */
class CCreateGroupDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  本对话框的用途。
     *
     *  这几种场景的界面骨架是同一套 ——「搜索 + 可勾选候选列表 + 右侧区 + 底部确认」，
     *  差别只在候选来源、右侧放什么、单选还是多选、以及点确认后发哪条协议。
     *  与其各写一个几乎相同的对话框，不如把这些差异做成参数。
     */
    enum Purpose
    {
        kPurposeCreateGroup = 0,   // 新建群聊：候选=通讯录，多选，带群名输入，发 cmd_create_group
        kPurposeInvite      = 1,   // 邀请入群：候选=通讯录（排除已在群内的），多选，发 cmd_invite_to_group
        kPurposeMute        = 2,   // 添加禁言成员：候选=本群成员（排除群主与自己），多选，发 cmd_mute_member
        kPurposeTransfer    = 3,   // 转让群主：候选=本群成员（排除自己），<u>单选</u>，发 cmd_transfer_owner（群管理窗发起，结果由群管理窗提示）
        kPurposeTransferThenLeave = 4, // 转让群主并退出：群主点「退出群聊」时打开，候选同上、单选；确认后交给主面板先转让、成功后再退群
        kPurposeShareTargets = 5,      // 选择共享对象：「文件共享」窗口点「发起共享」时打开，候选=通讯录，多选，确认后把已选的人交给 IMemberPickSink
        kPurposeMemoTargets  = 6,      // 选择备忘的提醒对象：群备忘编辑对话框点「选择成员」时打开，候选=本群成员，多选，可预先勾选，确认后交给 IMemberPickSink
        kPurposeGroupCall    = 7,      // 选择群通话要振铃的人：群聊窗点「语音通话」「视频通话」时打开，候选=本群成员（不含自己），多选，
                                       // 确认后交给 IMemberPickSink 发起群通话（docs/群音视频通话方案.md 6.3）
        kPurposeGroupCallInvite = 8,   // 群通话中追加邀请：通话窗点「邀请成员」时打开，候选=本群成员去掉已在通话里的人，多选，确认后交给 IMemberPickSink
        kPurposeGroupFollow = 9,       // 管理关注的成员：群的「消息提醒设置 ▸ 管理关注的成员…」打开，候选=本群成员（不含自己），多选，
                                       // 预先勾选已关注的人，<u>一个都不选也能确认</u>（清空名单），最多 net::kGroupFollowMax 人；
                                       // 确认后交给 IMemberPickSink（docs/消息提醒模式与消息盒子方案.md §12）
        kPurposeBotSenders = 10        // 选择个人机器人规则「发送者 ▸ 指定的人」：FlamingoBot.dll 的规则编辑窗点「选择…」时打开，
                                       // 候选=通讯录，多选，可预先勾选，确认后交给 IMemberPickSink（机器人插件宿主，docs/机器人方案.md §4.3）
    };

    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；用于取
     *                当前登录用户、发起建群请求。
     */
    explicit CCreateGroupDlg(CFlamingoClient* pFMGClient);
    ~CCreateGroupDlg();

    /**
     *  创建并显示对话框，用通讯录缓存构建可勾选联系人列表。已显示则前置。
     *    owner：owner 窗口句柄（主面板）。
     *    selfUserId：当前登录用户 userid，列表里排除自己。
     *    contacts：联系人资料池（本类内部复制一份）。
     *    departments：部门列表（本类内部复制一份），用于按部门分组与算分组标题。
     */
    void OpenForCreate(HWND owner, UINT selfUserId,
                       const std::vector<net::ContactItem>& contacts,
                       const std::vector<net::DepartmentItem>& departments);

    /**
     *  收到建群应答：成功提示"群聊创建成功"并关闭；失败弹框提示错误码。由主面板
     *  的 OnCreateGroupResult 转发。
     *    resp：建群应答解析结果。
     */
    void OnCreateGroupResult(const net::CreateGroupResponse& resp);

    /**
     *  以"选人"用途打开对话框（邀请入群 / 添加禁言成员 / 转让群主共用）。
     *
     *  与 OpenForCreate 的差别：不显示群名输入框，底部按钮文案与点击后发出的
     *  协议按 purpose 决定，kPurposeTransfer / kPurposeTransferThenLeave 还会切成单选。
     *
     *    owner：       owner 窗口句柄。
     *    purpose：     用途，取值见 Purpose；传 kPurposeCreateGroup 无意义。
     *    groupId：     目标群ID，提交时随协议发出；必须为正。
     *    selfUserId：  当前登录用户 userid，候选列表里排除自己。
     *    candidates：  候选人资料池（本类内部复制一份）。调用方负责先剔除不该
     *                  出现的人（如邀请时剔除已在群内的、禁言时剔除群主），
     *                  本类不认识"群"的概念、无从判断。
     *    departments： 部门列表，用于按部门分组；候选来自群成员时传空即可，
     *                  此时全部落进"未分组"一栏。
     */
    void OpenForPick(HWND owner, Purpose purpose, int64_t groupId, UINT selfUserId,
                     const std::vector<net::ContactItem>& candidates,
                     const std::vector<net::DepartmentItem>& departments);

    /**
     *  登记选人结果的接收方（kPurposeShareTargets 用）。在 OpenForPick 之前调用。
     *    sink：接收方（借用不持有），可为 NULL；须在本对话框关闭前一直有效。
     */
    void SetPickSink(IMemberPickSink* sink);

    /**
     *  登记下一次打开时预先勾选的人（kPurposeMemoTargets 用：再次选择提醒对象时保留上次选的人）。
     *  在 OpenForPick 之前调用，只对紧接着的那一次打开生效。
     *    userIds：预先勾选的 userid；不在候选里的会被忽略。
     */
    void SetInitialSelection(const std::vector<int64_t>& userIds);

    /**
     *  登记下一次以邀请入群用途打开时「入群后可查看的聊天记录」选项组的状态（docs/邀请入群方案.md §3.2）。
     *  在 OpenForPick 之前调用，只对紧接着的那一次打开生效；不调用时按群允许看历史、未开启同意开关处理。
     *    groupAllowsHistory：目标群是否允许新成员查看入群前的历史（群设置 newseehistory）；为 false 时选项组置灰。
     *    inviteConfirm：     本企业是否开启了「邀请入群需被邀请人同意」；为 true 时说明对方同意后才入群。
     */
    void SetInviteOptions(bool groupAllowsHistory, bool inviteConfirm);

    /**
     *  登记帖子群功能此刻是否可用（docs/帖子群方案.md 7.2）。在 OpenForCreate 之前调用；可用时新建群聊的界面在群名
     *  下方多一组「群类型：普通群 / 帖子群」，不可用时整组不构建、建出来的都是普通群。
     *    available：帖子群 DLL 已就绪且本企业的帖子群开关打开（CThreadGroupHost::IsAvailable）。
     */
    void SetThreadGroupAvailable(bool available);

    BEGIN_MSG_MAP(CCreateGroupDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  OpenForCreate 与 OpenForPick 共用的打开流程：存下候选数据、按需建窗、
     *  重建列表并前置显示。两个入口的差别只在事先设好的 m_purpose / m_groupId。
     *    owner：      owner 窗口句柄（当前实现传给 Create 的是 NULL，见实现内注释）；
     *    selfUserId： 当前登录用户 userid；
     *    contacts：   候选人资料池；
     *    departments：部门列表。
     */
    void OpenInternal(HWND owner, UINT selfUserId,
                      const std::vector<net::ContactItem>& contacts,
                      const std::vector<net::DepartmentItem>& departments);

    // 构建一次静态骨架（搜索框 / 群名框 / 两个滚动区 / 同步选项组 / 已选计数 / 取消+确认按钮）。
    // 控件树由 balloonmain::BuildMemberPickerLayout 按用途搭出（与布局单测共用），本函数只取回控件指针。
    void BuildUiOnce();

    // 邀请入群：把「入群后可查看的聊天记录」选项组恢复成初始状态（勾选同步全部、条数为默认值、
    // 说明按本次的群设置与同意开关），每次打开时调用。选项组没有构建时什么都不做。
    void ResetInviteHistoryOptions();

    // 邀请入群：把选项组下方的说明恢复成平时的灰色说明（条数改对后清掉红色的错误提示）。
    void RestoreInviteHistoryHint();

    // 用 m_contacts + m_departments（按 filter 过滤）重建左侧可勾选候选树。
    //   filter：搜索词（空=不过滤）；按昵称/账号大小写不敏感子串匹配。
    // 树的层级 / 排序 / 过滤规则由 MemberPickerTreeModel 的纯函数决定，本函数只把
    // 它算出的节点序列喂给 DuiTreeView，并给每个节点挂一个 MemberPickerRow 行控件。
    void RebuildMemberList(const CString& filter);

    // 按当前 m_selected 刷新树里每个节点行控件的勾选态（成员两态、部门三态）。
    // 勾选变化后调用；不重建节点，只改行控件的显示。
    void RefreshTreeCheckStates();

    // 把树在滚动区里的高度设为它的内容总高 —— 单列 DuiTreeView 自己不滚，得靠外层
    // DuiScrollView 滚，故树必须"长到和内容一样高"。展开 / 收起部门后也要重调。
    void SyncTreeHeight();

    // 处理树里复选框被点击：成员节点切自己，部门节点按当前态全选 / 全不选其下成员。
    //   nodeId：被点中的树节点 id。
    void OnTreeCheckToggled(int nodeId);

    // 用 m_selected 重建右侧"已选成员"区，并刷新底部"已选 N 人"计数。
    void RebuildSelected();

    // 把一个 userid 加入 / 移出已选集合（去重、保序）。
    void AddSelected(UINT userId);
    void RemoveSelected(UINT userId);

    // 点底部确认按钮：按 m_purpose 决定发哪条协议。
    //   kPurposeCreateGroup —— 收集已选成员 + 群名（空则自动生成），发 cmd_create_group；
    //   其余用途 —— 见 DoPickConfirm。
    void DoCreate();

    // "选人"用途下点确认：按 m_purpose 发出对应协议并关窗。
    // 与建群不同，这三种用途都不等应答改界面 —— 服务端写成功后会推 1317/1318，
    // 由群管理窗统一刷新；失败也由它按 code 提示。
    void DoPickConfirm();

    // 当前用途下底部确认按钮该显示什么文案。
    LPCTSTR ConfirmButtonText() const;

    // 当前用途下窗口标题该显示什么。
    LPCTSTR WindowTitleText() const;

    // 这一次打开要不要显示「群类型」选项组：新建群聊且帖子群功能可用。
    bool ShowGroupType() const;

    // 在 m_contacts 缓存里按 userid 取联系人；找不到返回 NULL。
    const net::ContactItem* FindContact(UINT userId) const;

    // 取某联系人的显示名（昵称→账号→"用户<id>"）。
    CString DisplayNameOf(const net::ContactItem& c) const;

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），引用不持有
    UINT             m_selfUserId;    // 当前登录用户 userid（列表排除自己）

    // 本次打开的用途，决定界面细节与点确认后发哪条协议。取值见 Purpose。
    Purpose          m_purpose;

    // "选人"用途下的目标群ID；kPurposeCreateGroup 时为 0（那时群还不存在）。
    int64_t          m_groupId;

    // 打开本对话框的窗口（主面板）。kPurposeTransferThenLeave 确认后把转让请求投给它；
    // 其余用途不用。窗口句柄只借用，每次打开时更新。
    HWND             m_hNotifyTarget;

    // 选人结果的接收方（kPurposeShareTargets 确认后把已选的人交给它）；借用不持有，可为 NULL。
    IMemberPickSink* m_pickSink;

    std::vector<net::ContactItem>    m_contacts;     // 联系人资料池（复制）
    std::vector<net::DepartmentItem> m_departments;  // 部门列表（复制）

    // 已选成员 userid（保序，按勾选先后）。
    std::vector<UINT> m_selected;

    // 下一次打开时预先勾选的 userid（SetInitialSelection 登记，OpenInternal 用掉后清空）。
    std::vector<int64_t> m_initialSelection;

    // 控件指针（借用，控件树持有）：动态刷新时用。
    balloonwjui::DuiEdit*   m_searchInput;   // 搜索联系人输入框
    balloonwjui::DuiEdit*   m_nameInput;     // 群聊名称输入框
    balloonwjui::DuiScrollView* m_memberScroll;  // 左侧候选树的滚动视口
    // 左侧候选树（借用，滚动区的内容容器持有）。单列模式 + 每节点挂 MemberPickerRow，
    // 滚轮由它上抛给 m_memberScroll。
    balloonmain::MemberPickerTreeView* m_memberTree;
    balloonwjui::DuiScrollView* m_chipScroll;    // 右侧已选成员滚动区
    balloonwjui::DuiLabel*      m_countLabel;    // 底部"已选 N 人"标签
    balloonwjui::DuiButton*     m_confirmButton; // 底部确认按钮（借用自控件树，重建界面时重新取）；建群请求在途时显示「正在创建…」
    balloonmain::InviteHistoryControls m_history;  // 邀请入群：「入群后可查看的聊天记录」选项组的三个单选按钮、条数输入框与
                                                   // 说明标签（借用自控件树，重建界面时重新取）；其他用途全部为 NULL

    // 邀请入群时目标群是否允许新成员查看入群前的历史（群设置 newseehistory）。为 false 时选项组整组置灰，
    // 请求里不带条数（群设置优先，docs/邀请入群方案.md §3.2）。由 SetInviteOptions 设置，提交时还要用到，
    // 故邀请入群打开期间一直保留；以别的用途打开时恢复为 true
    bool m_groupAllowsHistory;
    // 本企业是否开启了「邀请入群需被邀请人同意」（登录应答与 1013 里的 inviteconfirm）。为 true 时选项组下方
    // 说明对方同意后才入群。由 SetInviteOptions 设置；以别的用途打开时恢复为 false
    bool m_inviteConfirm;

    // 当前已构建的这套界面是按哪种用途搭的。本对话框对象由主面板长期持有、几种
    // 用途轮流复用，而各用途的控件集合并不相同（建群多一个群名输入框），故换用途
    // 重开时必须把窗口销毁重建，不能沿用上一次搭好的界面。
    Purpose m_builtPurpose;

    // 帖子群功能是否可用（SetThreadGroupAvailable 登记）；决定新建群聊时显示不显示「群类型」选项组
    bool m_threadGroupAvailable;
    // 当前已构建的这套界面有没有「群类型」选项组；与这次要的不同时销毁重建（同 m_builtPurpose 的道理）
    bool m_builtShowGroupType;
    // 「群类型」的两个单选按钮（借用自控件树，重建界面时重新取）；没有该选项组时为 NULL
    balloonwjui::DuiButton* m_groupTypeNormal;
    balloonwjui::DuiButton* m_groupTypeThread;

    bool m_built;          // 骨架是否已构建
    bool m_creating;       // 是否已发出建群请求、等待应答（防重复提交）

    // 标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只
    // 借用不释放，故本类持有、析构时 DeleteObject。
    HBITMAP m_hTitleIcon;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;

    // 候选树的节点序列（BuildMemberPickerTree 的结果）。下标即"节点在树里的第几个"，
    // 与 m_nodeIdByIndex 一一对应。每次重建候选树时刷新。
    std::vector<balloonmain::MemberPickerNode> m_treeNodes;
    // 节点下标 → DuiTreeView 节点 id（AddRoot / AddChild 的返回值）。
    std::vector<int>                           m_nodeIdByIndex;
    // DuiTreeView 节点 id → 节点下标。收到复选框点击通知时靠它反查是哪个节点。
    std::map<int, int>                         m_indexByNodeId;
    // "已选区 ✕ 按钮控件 id → userid"：每次重建已选区时刷新。
    std::map<UINT, UINT>                 m_chipRemoveCtrlToUser;
};
