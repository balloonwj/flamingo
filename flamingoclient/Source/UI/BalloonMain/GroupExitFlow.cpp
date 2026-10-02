/**
 *  GroupExitFlow.cpp
 *
 *  退出群聊 / 解散群聊 / 转让群主流程中纯逻辑部分的实现，说明见 GroupExitFlow.h。
 *  不碰任何窗口，故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（提示文字）。两个工程均已开启 /utf-8 编译选项，无需 BOM。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "GroupExitFlow.h"
#include "../../net/GroupProtocolClient.h"     // net::GroupMemberItem
#include "../../net/ContactsProtocolClient.h"  // net::ContactItem
#include "../../net/Protocol.h"                // error_code_xxx

namespace balloonmain {

namespace {

// 网络侧"群主"角色取值，与 grouprole::kNetRoleOwner、服务端 group_role_owner 一致。
const int kNetRoleOwner = 2;

// 把名字包进书名号：「名字」。名字为空时返回空串，调用方据此改用不带名字的说法。
std::wstring Quoted(const std::wstring& name)
{
    if (name.empty())
    {
        return std::wstring();
    }
    return L"「" + name + L"」";
}

// 拼"群聊「X」"；群名为空时只返回"群聊"。
std::wstring GroupPhrase(const std::wstring& groupName)
{
    return L"群聊" + Quoted(groupName);
}

// 各操作的名称，用于通用失败提示。
const wchar_t* OpName(GroupExitOp op)
{
    switch (op)
    {
    //退出群聊
    case kGroupExitOpLeave:
        return L"退出群聊";

    //解散群聊
    case kGroupExitOpDismiss:
        return L"解散群聊";

    //转让群主
    case kGroupExitOpTransfer:
        return L"转让群主";

    //未知操作：调用方传错了类型，给一个中性的说法
    default:
        return L"操作";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------- GroupExitTracker

std::deque<PendingGroupOp>& GroupExitTracker::QueueOf(GroupExitOp op)
{
    switch (op)
    {
    //解散
    case kGroupExitOpDismiss:
        return m_dismiss;

    //转让
    case kGroupExitOpTransfer:
        return m_transfer;

    //退群；未知类型也归到这里，不会越界
    case kGroupExitOpLeave:
    default:
        return m_leave;
    }
}

const std::deque<PendingGroupOp>& GroupExitTracker::QueueOf(GroupExitOp op) const
{
    switch (op)
    {
    //解散
    case kGroupExitOpDismiss:
        return m_dismiss;

    //转让
    case kGroupExitOpTransfer:
        return m_transfer;

    //退群；未知类型也归到这里
    case kGroupExitOpLeave:
    default:
        return m_leave;
    }
}

void GroupExitTracker::Begin(GroupExitOp op, const PendingGroupOp& pending)
{
    QueueOf(op).push_back(pending);
}

bool GroupExitTracker::HasPending(GroupExitOp op) const
{
    return !QueueOf(op).empty();
}

GroupOpOutcome GroupExitTracker::OnResult(GroupExitOp op, int code)
{
    GroupOpOutcome outcome;
    std::deque<PendingGroupOp>& queue = QueueOf(op);
    if (queue.empty())
    {
        return outcome;   // 不是经本类发出的请求，m_matched 保持 false
    }

    outcome.m_matched = true;
    outcome.m_op = queue.front();
    queue.pop_front();

    outcome.m_succeeded = (code == error_code_ok);
    if (outcome.m_succeeded)
    {
        outcome.m_sendLeaveNext = (op == kGroupExitOpTransfer && outcome.m_op.m_thenLeave);
    }
    else
    {
        outcome.m_errorText = BuildGroupExitErrorText(op, code);
    }
    return outcome;
}

void GroupExitTracker::Clear()
{
    m_leave.clear();
    m_dismiss.clear();
    m_transfer.clear();
}

// ---------------------------------------------------------------- 流程判断与文字

GroupLeavePath DecideLeavePath(int myNetRole)
{
    return (myNetRole == kNetRoleOwner) ? kLeavePathTransferFirst : kLeavePathDirect;
}

std::wstring BuildLeaveConfirmText(const std::wstring& groupName)
{
    return L"确定退出" + GroupPhrase(groupName)
         + L"吗？\n\n退出后将不再接收该群消息，本地的聊天记录也会一并清除。";
}

std::wstring BuildOwnerLeaveConfirmText(const std::wstring& groupName)
{
    return L"你是" + GroupPhrase(groupName) + L"的群主，退出前需要把群主转让给其他成员。"
         + L"\n\n是否现在选择新群主？";
}

std::wstring BuildDismissConfirmText(const std::wstring& groupName)
{
    return L"确定解散" + GroupPhrase(groupName)
         + L"吗？\n\n所有成员将被移出，群聊记录将从各成员的客户端中清除，此操作不可恢复。";
}

std::wstring BuildTransferConfirmText(const std::wstring& newOwnerName, bool thenLeave)
{
    const std::wstring target = newOwnerName.empty() ? std::wstring(L"该成员") : Quoted(newOwnerName);
    if (thenLeave)
    {
        return L"确定把群主转让给" + target + L"并退出本群吗？"
             + L"\n\n转让成功后你将自动退出群聊，本地的聊天记录也会一并清除。";
    }
    return L"确定把群主转让给" + target + L"吗？"
         + L"\n\n转让后你将成为普通成员，之后只有新群主能把群主再转回给你。";
}

std::wstring BuildGroupExitErrorText(GroupExitOp op, int code)
{
    switch (code)
    {
    //成功：没有提示
    case error_code_ok:
        return std::wstring();

    //群已不存在：多半是别处已经解散了
    case error_code_group_not_exist:
        return L"该群聊已不存在。";

    //无权限：群主退群被拒，或非群主解散 / 转让
    case error_code_permission_denied:
        if (op == kGroupExitOpLeave)
        {
            return L"你是群主，不能直接退出群聊。请先转让群主，或者解散本群。";
        }
        return op == kGroupExitOpDismiss ? L"只有群主可以解散群聊。" : L"只有群主可以转让群主。";

    //不是群主或管理员：服务端拒绝非群主解散时用的是这个码
    case error_code_not_group_admin:
        return op == kGroupExitOpTransfer ? L"只有群主可以转让群主。" : L"只有群主可以解散群聊。";

    //不是群成员：退群 / 解散时是本人已不在群里，转让时是对方已不在群里
    case error_code_not_group_member:
        if (op == kGroupExitOpTransfer)
        {
            return L"对方已不在本群中，无法转让群主。";
        }
        return L"你已不在该群中。";

    //其它错误（服务器内部错误、报文格式错误等）：带上操作名的通用提示
    default:
        return std::wstring(OpName(op)) + L"失败，请稍后重试。";
    }
}

std::wstring BuildLeaveNoticeText(GroupLeaveReason reason, const std::wstring& groupName)
{
    switch (reason)
    {
    //群被群主解散
    case kLeaveReasonDismissed:
        return groupName.empty() ? std::wstring(L"该群聊已被群主解散")
                                 : GroupPhrase(groupName) + L"已被群主解散";

    //本人被移出
    case kLeaveReasonKicked:
        return groupName.empty() ? std::wstring(L"你已被移出该群聊")
                                 : L"你已被移出" + GroupPhrase(groupName);

    //本人解散成功
    case kLeaveReasonDismissedBySelf:
        return L"已解散" + GroupPhrase(groupName);

    //本人主动退群成功；未知原因也按这条说
    case kLeaveReasonSelf:
    default:
        return L"已退出" + GroupPhrase(groupName);
    }
}

std::wstring BuildTransferDoneText(const std::wstring& newOwnerName)
{
    if (newOwnerName.empty())
    {
        return L"群主已转让";
    }
    return L"已将群主转让给" + Quoted(newOwnerName);
}

std::vector<net::ContactItem> BuildTransferCandidates(const std::vector<net::GroupMemberItem>& members,
                                                      int64_t selfId)
{
    std::vector<net::ContactItem> out;
    for (size_t i = 0; i < members.size(); ++i)
    {
        const net::GroupMemberItem& m = members[i];
        if (m.m_userid <= 0 || m.m_userid == selfId)
        {
            continue;   // 无效项与本人都不进候选（转给自己没有意义）
        }
        net::ContactItem c;
        c.m_userid = m.m_userid;
        // 与群里显示的名字一致：群名片优先，空时回退账号昵称
        c.m_nickname = m.m_card.empty() ? m.m_nickname : m.m_card;
        c.m_facetype = m.m_facetype;
        c.m_customface = m.m_customface;
        out.push_back(c);
    }
    return out;
}

} // namespace balloonmain
