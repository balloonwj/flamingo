/**
 *  GroupExitFlow.h
 *
 *  退出群聊 / 解散群聊 / 转让群主三条流程里不碰窗口的那部分逻辑：
 *    · 点「退出群聊」后走哪条路（群主须先转让群主再退出）；
 *    · 各个确认框、失败提示、离开群之后的提示文字；
 *    · GroupExitTracker：登记"本端发出、正在等服务端应答"的写操作，应答到达时对上是哪个群。
 *      这三条协议的应答体只有 { code, msg }、不带群号，同一种操作的应答又按发出顺序回来，
 *      故按操作类型各排一个先进先出的队列即可对上；
 *    · 转让候选名单的生成（群主退群前选新群主用）。
 *
 *  由 CBalloonMainPanel 使用；纯函数与纯数据，不创建窗口，故可被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用（在 Flamingo.vcxproj 里 .cpp 需标记 PrecompiledHeader=NotUsing）。
 *
 *  典型用法：
 *      if (DecideLeavePath(myRole) == kLeavePathTransferFirst) { ...先选新群主... }
 *      tracker.Begin(kGroupExitOpLeave, pending);       // 发出 cmd_leave_group 之后
 *      GroupOpOutcome r = tracker.OnResult(kGroupExitOpLeave, code);   // 收到应答时
 *      if (r.m_matched && !r.m_succeeded) { 提示 r.m_errorText; }
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

namespace net {
struct GroupMemberItem;
struct ContactItem;
}

namespace balloonmain {

/**
 *  GroupExitOp —— 本流程涉及的三种写操作。用作 GroupExitTracker 的队列选择与失败提示的分支。
 */
enum GroupExitOp
{
    kGroupExitOpLeave    = 1,   // 退出群聊（cmd_leave_group 1309）
    kGroupExitOpDismiss  = 2,   // 解散群聊（cmd_dismiss_group 1302），仅群主
    kGroupExitOpTransfer = 3,   // 转让群主（cmd_transfer_owner 1316），仅群主
};

/**
 *  GroupLeavePath —— 点「退出群聊」之后走哪条路。
 */
enum GroupLeavePath
{
    kLeavePathDirect        = 1,   // 确认后直接发退群请求：管理员、普通成员，以及本人角色还不知道时
    kLeavePathTransferFirst = 2,   // 本人是群主：服务端不允许群主直接退群，须先选新群主、转让成功后再退群
};

/**
 *  GroupLeaveReason —— 本人离开一个群的原因，决定离开之后给用户的提示文字。
 */
enum GroupLeaveReason
{
    kLeaveReasonSelf            = 1,   // 本人主动退群，服务端已确认
    kLeaveReasonDismissed       = 2,   // 群被群主解散（收到解散推送，且不是本人发起的）
    kLeaveReasonKicked          = 3,   // 本人被群主或管理员移出
    kLeaveReasonDismissedBySelf = 4,   // 本人（群主）解散了这个群，服务端已确认
};

/**
 *  PendingGroupOp —— 一条已发出、正在等服务端应答的写操作。
 */
struct PendingGroupOp
{
    int64_t      m_groupId;        // 目标群ID
    std::wstring m_groupName;      // 发出时的群名，用于提示文字（应答到达时群可能已从缓存移除）
    int64_t      m_newOwner;       // 仅转让：新群主 userid；其余操作为 0
    std::wstring m_newOwnerName;   // 仅转让：新群主的显示名，用于提示文字
    bool         m_thenLeave;      // 仅转让：转让成功后是否接着发退群请求（群主退群的流程）

    PendingGroupOp()
        : m_groupId(0)
        , m_newOwner(0)
        , m_thenLeave(false)
    {
    }
};

/**
 *  TransferOwnerRequest —— 请主面板转让群主的消息载荷（FMG_MSG_TRANSFER_GROUP_OWNER 的 lParam）。
 *
 *  群聊窗成员右键「转让群主」与选人对话框「转让群主并退出」两处发起：发起窗口先弹确认框，用户确认后
 *  在堆上 new 出本结构投给主面板，由主面板发协议、登记等待应答并在应答到达后提示结果。主面板处理完
 *  负责 delete；投递失败（窗口已销毁）时由投递方自行 delete。
 */
struct TransferOwnerRequest
{
    int64_t      m_groupId;        // 目标群ID
    int64_t      m_newOwner;       // 新群主 userid
    std::wstring m_newOwnerName;   // 新群主的显示名，用于结果提示
    bool         m_thenLeave;      // 转让成功后是否接着退群（群主退群的流程）

    TransferOwnerRequest()
        : m_groupId(0)
        , m_newOwner(0)
        , m_thenLeave(false)
    {
    }
};

/**
 *  GroupOpOutcome —— GroupExitTracker::OnResult 对一条应答给出的处理结论。
 */
struct GroupOpOutcome
{
    bool           m_matched;        // 是否对上了本端登记过的请求；false 表示不是经本类发出的（如群管理窗发起的），调用方应交给原处理者
    bool           m_succeeded;      // 服务端是否执行成功（code == 0）
    bool           m_sendLeaveNext;  // 仅转让：转让成功且登记时要求接着退群，调用方应立即发退群请求
    std::wstring   m_errorText;      // 失败时给用户的提示；成功时为空
    PendingGroupOp m_op;             // 对上的那条登记（m_matched 为 false 时无意义）

    GroupOpOutcome()
        : m_matched(false)
        , m_succeeded(false)
        , m_sendLeaveNext(false)
    {
    }
};

/**
 *  GroupExitTracker —— 登记本端发出的退群 / 解散 / 转让请求，应答到达时对上是哪个群。
 *
 *  三种操作各排一个先进先出的队列：同一条 TCP 连接上，同一种请求的应答按发出顺序回来。
 *  非线程安全，只在 UI 线程使用。
 */
class GroupExitTracker
{
public:
    /**
     *  登记一条刚发出的请求。
     *    op：      操作类型；
     *    pending： 请求的上下文（群ID、群名、转让目标等），按值复制保存。
     */
    void Begin(GroupExitOp op, const PendingGroupOp& pending);

    /**
     *  该种操作是否还有没等到应答的请求。
     */
    bool HasPending(GroupExitOp op) const;

    /**
     *  收到一条应答：取出该种操作最早登记的那条请求，给出处理结论。
     *    op：  应答对应的操作类型（由协议号决定）；
     *    code：应答里的结果码，0 为成功，其余取值见 net/Protocol.h 的 error_code。
     *  @return 处理结论；该种操作没有登记中的请求时 m_matched 为 false。
     */
    GroupOpOutcome OnResult(GroupExitOp op, int code);

    /**
     *  丢弃全部登记（退出登录、断线重登时调用：旧连接上的应答不会再来）。
     */
    void Clear();

private:
    // 取某种操作对应的队列。
    std::deque<PendingGroupOp>& QueueOf(GroupExitOp op);
    const std::deque<PendingGroupOp>& QueueOf(GroupExitOp op) const;

    std::deque<PendingGroupOp> m_leave;     // 等待应答的退群请求，按发出顺序
    std::deque<PendingGroupOp> m_dismiss;   // 等待应答的解散请求，按发出顺序
    std::deque<PendingGroupOp> m_transfer;  // 等待应答的转让请求，按发出顺序
};

/**
 *  点「退出群聊」之后走哪条路。
 *    myNetRole：本人在该群的角色，网络侧取值（0 成员 1 管理员 2 群主）；负值表示还不知道。
 *  @return 群主返回 kLeavePathTransferFirst，其余（含未知）返回 kLeavePathDirect。纯函数。
 */
GroupLeavePath DecideLeavePath(int myNetRole);

/**
 *  普通成员 / 管理员退群前的确认框正文。
 *    groupName：群名；为空时文字里不带群名。
 */
std::wstring BuildLeaveConfirmText(const std::wstring& groupName);

/**
 *  群主点「退出群聊」时的确认框正文：说明须先转让群主，询问是否现在选择新群主（是 / 否）。
 *    groupName：群名；为空时文字里不带群名。
 */
std::wstring BuildOwnerLeaveConfirmText(const std::wstring& groupName);

/**
 *  解散群聊前的确认框正文。
 *    groupName：群名；为空时文字里不带群名。
 */
std::wstring BuildDismissConfirmText(const std::wstring& groupName);

/**
 *  转让群主前的确认框正文。
 *    newOwnerName：新群主的显示名；为空时文字里不带名字。
 *    thenLeave：   true 表示转让成功后接着退群（群主退群的流程），正文会说明这一点。
 */
std::wstring BuildTransferConfirmText(const std::wstring& newOwnerName, bool thenLeave);

/**
 *  写操作失败时给用户的提示。
 *    op：  操作类型；
 *    code：应答里的结果码，取值见 net/Protocol.h 的 error_code。
 *  @return 提示文字；code 为 0（成功）时返回空串。纯函数。
 */
std::wstring BuildGroupExitErrorText(GroupExitOp op, int code);

/**
 *  本人离开一个群之后的提示文字（轻提示或窗口内提示框用）。
 *    reason：   离开的原因；
 *    groupName：群名；为空时文字里不带群名。
 */
std::wstring BuildLeaveNoticeText(GroupLeaveReason reason, const std::wstring& groupName);

/**
 *  转让群主成功后的轻提示文字，形如"已将群主转让给「乙」"。
 *    newOwnerName：新群主的显示名；为空时文字里不带名字。
 */
std::wstring BuildTransferDoneText(const std::wstring& newOwnerName);

/**
 *  由群成员列表生成转让群主的候选名单（交给选人对话框）。
 *    members：群成员列表（cmd_get_group_members 应答）；
 *    selfId： 本人 userid，不进候选；
 *  @return 候选，保持成员列表原顺序；userid 不大于 0 的项跳过。显示名（m_nickname，UTF-8）
 *          取群名片，群名片为空时取账号昵称。纯函数。
 */
std::vector<net::ContactItem> BuildTransferCandidates(const std::vector<net::GroupMemberItem>& members,
                                                      int64_t selfId);

} // namespace balloonmain
