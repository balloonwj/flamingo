/**
 *  GroupExitFlowTests.cpp
 *
 *  退群 / 解散群 / 转让群主流程里的纯逻辑（UI/BalloonMain/GroupExitFlow.h）的单元测试，
 *  标签 [groupexit]：
 *
 *    GE1  点「退出群聊」走哪条路：群主先转让再退出，其余角色（含角色未知）直接退群
 *    GE2  各个确认框的文字：带群名或新群主名；群名为空时不出现空的书名号；解散强调不可恢复，
 *         转让不再说不可恢复、而是说明只有新群主能转回
 *    GE3  按操作与错误码给出的失败提示
 *    GE4  本人离开一个群时的提示文字（主动退出 / 被解散 / 被移出 / 本人解散）
 *    GE5  等待应答的登记：应答按发出顺序对上请求；没有登记过的应答不认领（交给群管理窗）
 *    GE6  三种操作各排各的队，互不串号
 *    GE7  转让后退出：转让成功的下一步是发退群；转让失败不发退群并给出提示；单纯转让成功不发退群
 *    GE8  转让候选名单：排除本人与无效 userid，显示名群名片优先、空时回退账号昵称
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonMain/GroupExitFlow.h"
#include "../net/GroupProtocolClient.h"      // net::GroupMemberItem
#include "../net/ContactsProtocolClient.h"   // net::ContactItem
#include "../net/Protocol.h"                 // error_code_xxx

using namespace balloonmain;

namespace {

// 网络侧角色取值（与 grouprole::NetGroupRole 一致）：0 普通成员 1 管理员 2 群主；负值表示未知。
const int kRoleMember  = 0;
const int kRoleAdmin   = 1;
const int kRoleOwner   = 2;
const int kRoleUnknown = -1;

// 测试用的群 ID 与 userid，取值无特殊含义。
const int64_t kGroupA = 268435460LL;
const int64_t kGroupB = 268435461LL;
const int64_t kSelf   = 2001;
const int64_t kOther  = 2002;

// 文本里是否包含某个子串。
bool Contains(const std::wstring& text, const wchar_t* part)
{
    return text.find(part) != std::wstring::npos;
}

// 造一条等待应答的登记。
PendingGroupOp MakePending(int64_t groupId, const wchar_t* groupName)
{
    PendingGroupOp p;
    p.m_groupId = groupId;
    p.m_groupName = groupName;
    return p;
}

// 造一名群成员。
net::GroupMemberItem MakeMember(int64_t userid, int role, const char* card, const char* nickname)
{
    net::GroupMemberItem m;
    m.m_userid = userid;
    m.m_role = role;
    m.m_card = card;
    m.m_nickname = nickname;
    return m;
}

} // anonymous namespace

TEST_CASE("GE1 点退出群聊：群主先转让再退出，其余角色直接退群", "[groupexit]")
{
    CHECK(DecideLeavePath(kRoleOwner) == kLeavePathTransferFirst);
    CHECK(DecideLeavePath(kRoleAdmin) == kLeavePathDirect);
    CHECK(DecideLeavePath(kRoleMember) == kLeavePathDirect);
    //角色还没拉到：按直接退群处理，万一其实是群主，服务端会拒绝并给出明确提示
    CHECK(DecideLeavePath(kRoleUnknown) == kLeavePathDirect);
}

TEST_CASE("GE2 确认框文字：带名字，空名不留空书名号；解散强调不可恢复，转让说明谁能转回", "[groupexit]")
{
    const std::wstring leave = BuildLeaveConfirmText(L"项目组");
    CHECK(Contains(leave, L"「项目组」"));
    CHECK(Contains(leave, L"聊天记录"));

    const std::wstring ownerLeave = BuildOwnerLeaveConfirmText(L"项目组");
    CHECK(Contains(ownerLeave, L"「项目组」"));
    CHECK(Contains(ownerLeave, L"群主"));
    CHECK(Contains(ownerLeave, L"转让"));

    const std::wstring dismiss = BuildDismissConfirmText(L"项目组");
    CHECK(Contains(dismiss, L"「项目组」"));
    CHECK(Contains(dismiss, L"不可恢复"));
    //解散后服务端保留群消息，只是从各成员客户端清除；不能再说"全部删除"
    CHECK_FALSE(Contains(dismiss, L"全部删除"));

    const std::wstring transfer = BuildTransferConfirmText(L"乙", false);
    CHECK(Contains(transfer, L"「乙」"));
    CHECK_FALSE(Contains(transfer, L"不可恢复"));
    CHECK(Contains(transfer, L"转回"));

    const std::wstring transferLeave = BuildTransferConfirmText(L"乙", true);
    CHECK(Contains(transferLeave, L"「乙」"));
    CHECK(Contains(transferLeave, L"退出"));

    //名字为空：不出现「」
    CHECK_FALSE(Contains(BuildLeaveConfirmText(L""), L"「」"));
    CHECK_FALSE(Contains(BuildOwnerLeaveConfirmText(L""), L"「」"));
    CHECK_FALSE(Contains(BuildDismissConfirmText(L""), L"「」"));
    CHECK_FALSE(Contains(BuildTransferConfirmText(L"", false), L"「」"));
    CHECK_FALSE(Contains(BuildTransferConfirmText(L"", true), L"「」"));
}

TEST_CASE("GE3 失败提示随操作与错误码变化", "[groupexit]")
{
    //成功时没有提示
    CHECK(BuildGroupExitErrorText(kGroupExitOpLeave, error_code_ok).empty());

    //群主退群被拒：告诉用户该怎么办
    const std::wstring ownerLeave = BuildGroupExitErrorText(kGroupExitOpLeave, error_code_permission_denied);
    CHECK(Contains(ownerLeave, L"转让"));
    CHECK(Contains(ownerLeave, L"解散"));

    //非群主解散 / 转让被拒
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpDismiss, error_code_not_group_admin), L"只有群主"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpDismiss, error_code_permission_denied), L"只有群主"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpTransfer, error_code_permission_denied), L"只有群主"));

    //群已不存在
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpLeave, error_code_group_not_exist), L"不存在"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpDismiss, error_code_group_not_exist), L"不存在"));

    //不是群成员：退群时说"你已不在"，转让时说"对方已不在"
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpLeave, error_code_not_group_member), L"你已"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpTransfer, error_code_not_group_member), L"对方"));

    //其它错误：带上操作名的通用提示
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpLeave, error_code_server_internal), L"退出群聊"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpDismiss, error_code_server_internal), L"解散群聊"));
    CHECK(Contains(BuildGroupExitErrorText(kGroupExitOpTransfer, error_code_server_internal), L"转让群主"));
}

TEST_CASE("GE4 本人离开一个群时的提示文字", "[groupexit]")
{
    CHECK(BuildLeaveNoticeText(kLeaveReasonSelf, L"项目组") == std::wstring(L"已退出群聊「项目组」"));
    CHECK(BuildLeaveNoticeText(kLeaveReasonDismissed, L"项目组") == std::wstring(L"群聊「项目组」已被群主解散"));
    CHECK(BuildLeaveNoticeText(kLeaveReasonKicked, L"项目组") == std::wstring(L"你已被移出群聊「项目组」"));
    CHECK(BuildLeaveNoticeText(kLeaveReasonDismissedBySelf, L"项目组") == std::wstring(L"已解散群聊「项目组」"));

    //群名为空：不出现「」
    CHECK_FALSE(Contains(BuildLeaveNoticeText(kLeaveReasonSelf, L""), L"「」"));
    CHECK_FALSE(Contains(BuildLeaveNoticeText(kLeaveReasonDismissed, L""), L"「」"));
    CHECK_FALSE(Contains(BuildLeaveNoticeText(kLeaveReasonKicked, L""), L"「」"));
    CHECK_FALSE(Contains(BuildLeaveNoticeText(kLeaveReasonDismissedBySelf, L""), L"「」"));

    //转让成功的提示
    CHECK(BuildTransferDoneText(L"乙") == std::wstring(L"已将群主转让给「乙」"));
    CHECK_FALSE(Contains(BuildTransferDoneText(L""), L"「」"));
}

TEST_CASE("GE5 应答按发出顺序对上请求，没有登记过的应答不认领", "[groupexit]")
{
    GroupExitTracker tracker;

    //没有登记：不认领，交给群管理窗处理
    GroupOpOutcome none = tracker.OnResult(kGroupExitOpDismiss, error_code_ok);
    CHECK_FALSE(none.m_matched);

    //登记一次退群，成功
    tracker.Begin(kGroupExitOpLeave, MakePending(kGroupA, L"项目组"));
    CHECK(tracker.HasPending(kGroupExitOpLeave));
    GroupOpOutcome ok = tracker.OnResult(kGroupExitOpLeave, error_code_ok);
    CHECK(ok.m_matched);
    CHECK(ok.m_succeeded);
    CHECK(ok.m_op.m_groupId == kGroupA);
    CHECK(ok.m_op.m_groupName == std::wstring(L"项目组"));
    CHECK(ok.m_errorText.empty());
    CHECK_FALSE(tracker.HasPending(kGroupExitOpLeave));

    //同一种操作连发两次：先发的先对上
    tracker.Begin(kGroupExitOpDismiss, MakePending(kGroupA, L"项目组"));
    tracker.Begin(kGroupExitOpDismiss, MakePending(kGroupB, L"兴趣组"));
    GroupOpOutcome first = tracker.OnResult(kGroupExitOpDismiss, error_code_server_internal);
    CHECK(first.m_matched);
    CHECK_FALSE(first.m_succeeded);
    CHECK(first.m_op.m_groupId == kGroupA);
    CHECK_FALSE(first.m_errorText.empty());
    GroupOpOutcome second = tracker.OnResult(kGroupExitOpDismiss, error_code_ok);
    CHECK(second.m_op.m_groupId == kGroupB);
    CHECK(second.m_succeeded);

    //退出登录时清空
    tracker.Begin(kGroupExitOpLeave, MakePending(kGroupA, L"项目组"));
    tracker.Clear();
    CHECK_FALSE(tracker.HasPending(kGroupExitOpLeave));
}

TEST_CASE("GE6 三种操作各排各的队，互不串号", "[groupexit]")
{
    GroupExitTracker tracker;
    tracker.Begin(kGroupExitOpLeave, MakePending(kGroupA, L"项目组"));
    tracker.Begin(kGroupExitOpDismiss, MakePending(kGroupB, L"兴趣组"));

    //解散的应答只认解散的登记
    GroupOpOutcome d = tracker.OnResult(kGroupExitOpDismiss, error_code_ok);
    CHECK(d.m_op.m_groupId == kGroupB);
    CHECK(tracker.HasPending(kGroupExitOpLeave));
    CHECK_FALSE(tracker.HasPending(kGroupExitOpDismiss));

    //转让没有登记：不认领
    CHECK_FALSE(tracker.OnResult(kGroupExitOpTransfer, error_code_ok).m_matched);
    CHECK(tracker.OnResult(kGroupExitOpLeave, error_code_ok).m_op.m_groupId == kGroupA);
}

TEST_CASE("GE7 转让后退出：成功才接着退群，失败不退群并给出提示", "[groupexit]")
{
    GroupExitTracker tracker;

    PendingGroupOp thenLeave = MakePending(kGroupA, L"项目组");
    thenLeave.m_newOwner = kOther;
    thenLeave.m_newOwnerName = L"乙";
    thenLeave.m_thenLeave = true;

    //转让成功：下一步是发退群
    tracker.Begin(kGroupExitOpTransfer, thenLeave);
    GroupOpOutcome ok = tracker.OnResult(kGroupExitOpTransfer, error_code_ok);
    CHECK(ok.m_matched);
    CHECK(ok.m_succeeded);
    CHECK(ok.m_sendLeaveNext);
    CHECK(ok.m_op.m_newOwner == kOther);

    //转让失败：不发退群，给出提示
    tracker.Begin(kGroupExitOpTransfer, thenLeave);
    GroupOpOutcome fail = tracker.OnResult(kGroupExitOpTransfer, error_code_not_group_member);
    CHECK(fail.m_matched);
    CHECK_FALSE(fail.m_succeeded);
    CHECK_FALSE(fail.m_sendLeaveNext);
    CHECK(Contains(fail.m_errorText, L"对方"));

    //单纯转让（不退群）成功：不发退群
    PendingGroupOp plain = thenLeave;
    plain.m_thenLeave = false;
    tracker.Begin(kGroupExitOpTransfer, plain);
    GroupOpOutcome plainOk = tracker.OnResult(kGroupExitOpTransfer, error_code_ok);
    CHECK(plainOk.m_succeeded);
    CHECK_FALSE(plainOk.m_sendLeaveNext);
}

TEST_CASE("GE8 转让候选名单：排除本人与无效 userid，群名片优先", "[groupexit]")
{
    std::vector<net::GroupMemberItem> members;
    members.push_back(MakeMember(kSelf, kRoleOwner, "", u8"甲"));
    members.push_back(MakeMember(kOther, kRoleAdmin, u8"小乙", u8"乙"));
    members.push_back(MakeMember(2003, kRoleMember, "", u8"丙"));
    members.push_back(MakeMember(0, kRoleMember, "", u8"无效"));

    const std::vector<net::ContactItem> c = BuildTransferCandidates(members, kSelf);
    REQUIRE(c.size() == 2);
    CHECK(c[0].m_userid == kOther);
    CHECK(c[0].m_nickname == std::string(u8"小乙"));   //有群名片用群名片
    CHECK(c[1].m_userid == 2003);
    CHECK(c[1].m_nickname == std::string(u8"丙"));     //没有群名片回退账号昵称

    //只有自己一个人：没有候选
    std::vector<net::GroupMemberItem> alone(1, MakeMember(kSelf, kRoleOwner, "", u8"甲"));
    CHECK(BuildTransferCandidates(alone, kSelf).empty());
}
