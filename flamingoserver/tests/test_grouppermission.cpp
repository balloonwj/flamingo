/**
 *  群管理权限判定单元测试（Suite GP），test_grouppermission.cpp
 *  覆盖 CheckSetMemberRole 的全部判定分支与判定优先级，以及结论到协议错误码、
 *  到提示原文的两张翻译表；另有群内发言的禁言判定 IsMutedForSpeaking（FWD-S6，
 *  普通群发与转发到群共用，见 docs/聊天消息转发方案.md）。
 *
 *  这一层是"设置 / 取消管理员"里最值得测的部分：规则要在"操作者三种角色 ×
 *  目标三种角色 × 是否本人 × 请求角色是否合法 × 群是否存在"这么多格里都站得住，
 *  而它原先内联在 ChatSession 的处理函数中，夹着库查询与发包，靠手工连客户端
 *  几乎不可能覆盖全。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#include "doctest.h"
#include "../chatserversrc/GroupPermission.h"
#include "../chatserversrc/Protocol.h"

#include <string>   //GP10 用 std::string 比对提示原文

using namespace chatserver;

//"不在本群"在 GroupManager::getMemberRole 里用负值表达，测试里统一取 -1
static const int kRoleNotInGroup = -1;

TEST_CASE("GP1 请求角色非法一律拒绝")
{
    //本命令只在普通成员与管理员之间切换；请求把人设成群主要走 cmd_transfer_owner
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, group_role_owner)
          == set_role_check_bad_role);
    //越界值：负数、以及大于群主的取值
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, -1)
          == set_role_check_bad_role);
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, 3)
          == set_role_check_bad_role);
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, 99)
          == set_role_check_bad_role);

    //合法的两个取值不因角色值本身被拒
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, group_role_admin)
          == set_role_check_ok);
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_admin, false, group_role_member)
          == set_role_check_ok);
}

TEST_CASE("GP2 群不存在时拒绝")
{
    CHECK(CheckSetMemberRole(false, group_role_owner, group_role_member, false, group_role_admin)
          == set_role_check_group_not_exist);
    //群不存在时，即便操作者角色看起来是群主也一样拒绝
    CHECK(CheckSetMemberRole(false, group_role_owner, group_role_admin, false, group_role_member)
          == set_role_check_group_not_exist);
}

TEST_CASE("GP3 非群主一律无权设置或撤销管理员")
{
    //管理员不能给别人升管理员，也不能撤别人的管理员
    CHECK(CheckSetMemberRole(true, group_role_admin, group_role_member, false, group_role_admin)
          == set_role_check_not_owner);
    CHECK(CheckSetMemberRole(true, group_role_admin, group_role_admin, false, group_role_member)
          == set_role_check_not_owner);
    //普通成员同样不行
    CHECK(CheckSetMemberRole(true, group_role_member, group_role_member, false, group_role_admin)
          == set_role_check_not_owner);
    //根本不在本群的人（角色为负）也归此类，而不是报"目标不是群成员"
    CHECK(CheckSetMemberRole(true, kRoleNotInGroup, group_role_member, false, group_role_admin)
          == set_role_check_not_owner);
}

TEST_CASE("GP4 群主不能修改自己的角色")
{
    //群主要卸任须走转让群主，不能自己把自己降成管理员或普通成员
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_owner, true, group_role_admin)
          == set_role_check_self);
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_owner, true, group_role_member)
          == set_role_check_self);
}

TEST_CASE("GP5 目标不在本群时拒绝")
{
    //典型场景：目标刚被别人移出群，而操作端的成员列表还没刷新
    CHECK(CheckSetMemberRole(true, group_role_owner, kRoleNotInGroup, false, group_role_admin)
          == set_role_check_not_member);
    CHECK(CheckSetMemberRole(true, group_role_owner, kRoleNotInGroup, false, group_role_member)
          == set_role_check_not_member);
}

TEST_CASE("GP6 群主对群内他人的正常升降级放行")
{
    //设为管理员
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, group_role_admin)
          == set_role_check_ok);
    //撤销管理员
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_admin, false, group_role_member)
          == set_role_check_ok);
    //把已经是管理员的人再设一次管理员：幂等，同样放行（落库是覆盖写）
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_admin, false, group_role_admin)
          == set_role_check_ok);
    //把本就是普通成员的人再降一次：同理
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_member, false, group_role_member)
          == set_role_check_ok);
}

TEST_CASE("GP7 目标是另一个群主时按放行处理")
{
    //一个群只有一个群主，"群主对另一个群主"在真实数据里出不来；这里把该输入下的
    //行为固定成放行，与抽取前 ChatSession::onSetMemberRole 的判定完全一致 ——
    //它只查了"目标在不在群里"，没有额外拦截目标是群主的情况。
    //若日后决定要拦（返回 error_code_cannot_operate_owner），这是一处行为变更，
    //改动时本用例会失败，提醒同步修改客户端的失败提示文案。
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_owner, false, group_role_admin)
          == set_role_check_ok);
}

TEST_CASE("GP8 多条失败条件同时成立时按固定优先级返回")
{
    //角色值非法 + 群不存在 + 操作者不是群主：先报角色值非法
    CHECK(CheckSetMemberRole(false, group_role_member, kRoleNotInGroup, false, group_role_owner)
          == set_role_check_bad_role);
    //群不存在 + 操作者不是群主：先报群不存在
    CHECK(CheckSetMemberRole(false, group_role_member, kRoleNotInGroup, false, group_role_admin)
          == set_role_check_group_not_exist);
    //操作者不是群主 + 目标不在群里：先报无权限，不泄露"那个人在不在群里"
    CHECK(CheckSetMemberRole(true, group_role_member, kRoleNotInGroup, false, group_role_admin)
          == set_role_check_not_owner);
    //操作者是群主 + 目标是自己 + 请求角色合法：报"不能改自己"
    CHECK(CheckSetMemberRole(true, group_role_owner, group_role_owner, true, group_role_member)
          == set_role_check_self);
}

TEST_CASE("GP9 结论到协议错误码的翻译")
{
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_ok)              == error_code_ok);
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_bad_role)        == error_code_bad_request);
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_group_not_exist) == error_code_group_not_exist);
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_not_owner)       == error_code_permission_denied);
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_self)            == error_code_bad_request);
    CHECK(SetMemberRoleCheckToErrorCode(set_role_check_not_member)      == error_code_not_group_member);
}

TEST_CASE("GP10 结论到提示原文的翻译")
{
    //提示原文与抽取前 onSetMemberRole 里硬写的字符串逐条一致 —— 客户端
    //DescribeGroupOpFailure 拿不到对应条目时会把这段原文直接展示给用户
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_ok))              == "ok");
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_bad_role))        == "invalid role");
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_group_not_exist)) == "group not exist");
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_not_owner))       == "only group owner can set role");
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_self))            == "cannot change your own role");
    CHECK(std::string(SetMemberRoleCheckToMessage(set_role_check_not_member))      == "target is not a group member");
}

// =====================================================================
// GP11-GP15 —— 移出群成员的权限判定
// =====================================================================
//
// 与设 / 撤管理员的关键差别：移出<u>管理员也能做</u>（只是够不到其他管理员和群主），
// 而设管理员仅限群主。两者的门控不同，容易写混，故逐格钉死。

TEST_CASE("GP11 移出的前置权限：群主与管理员可发起，其余不可")
{
    //群主与管理员都够格发起移出
    CHECK(CheckKickGroupMember(true, group_role_owner) == kick_check_ok);
    CHECK(CheckKickGroupMember(true, group_role_admin) == kick_check_ok);
    //普通成员不够格
    CHECK(CheckKickGroupMember(true, group_role_member) == kick_check_not_admin);
    //根本不在本群的人（角色为负）同样不够格
    CHECK(CheckKickGroupMember(true, kRoleNotInGroup) == kick_check_not_admin);
}

TEST_CASE("GP12 群不存在时移出请求一律拒绝")
{
    //群不存在优先于角色判定 —— 群都没了，谈不上谁是群主
    CHECK(CheckKickGroupMember(false, group_role_owner) == kick_check_group_not_exist);
    CHECK(CheckKickGroupMember(false, group_role_admin) == kick_check_group_not_exist);
    CHECK(CheckKickGroupMember(false, group_role_member) == kick_check_group_not_exist);
}

TEST_CASE("GP13 群主可移出管理员与普通成员，但移不动群主和自己")
{
    CHECK(CanKickTarget(group_role_owner, group_role_member, false));
    CHECK(CanKickTarget(group_role_owner, group_role_admin, false));
    //群主是权限顶点，谁都移不动（含另一个群主这种异常输入）
    CHECK_FALSE(CanKickTarget(group_role_owner, group_role_owner, false));
    //移出自己没有意义，退群走另一条命令
    CHECK_FALSE(CanKickTarget(group_role_owner, group_role_member, true));
}

TEST_CASE("GP14 管理员只能移出普通成员")
{
    CHECK(CanKickTarget(group_role_admin, group_role_member, false));
    //管理员之间互不隶属，移不动其他管理员
    CHECK_FALSE(CanKickTarget(group_role_admin, group_role_admin, false));
    //更移不动群主
    CHECK_FALSE(CanKickTarget(group_role_admin, group_role_owner, false));
    //也移不动自己
    CHECK_FALSE(CanKickTarget(group_role_admin, group_role_member, true));
}

TEST_CASE("GP15 普通成员与群外人一律移不动任何人，目标离群则跳过")
{
    const int roles[] = { group_role_member, group_role_admin, group_role_owner };
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
    {
        //普通成员
        CHECK_FALSE(CanKickTarget(group_role_member, roles[i], false));
        //根本不在本群的人
        CHECK_FALSE(CanKickTarget(kRoleNotInGroup, roles[i], false));
    }

    //目标已不在本群（角色为负）：静默跳过，不是错误
    CHECK_FALSE(CanKickTarget(group_role_owner, kRoleNotInGroup, false));
    CHECK_FALSE(CanKickTarget(group_role_admin, kRoleNotInGroup, false));
}

TEST_CASE("GP16 移出前置结论到错误码与提示原文的翻译")
{
    CHECK(KickGroupMemberCheckToErrorCode(kick_check_ok)              == error_code_ok);
    CHECK(KickGroupMemberCheckToErrorCode(kick_check_group_not_exist) == error_code_group_not_exist);
    CHECK(KickGroupMemberCheckToErrorCode(kick_check_not_admin)       == error_code_not_group_admin);

    //提示原文与抽取前 onKickGroupMember 里硬写的字符串逐条一致
    CHECK(std::string(KickGroupMemberCheckToMessage(kick_check_ok))              == "ok");
    CHECK(std::string(KickGroupMemberCheckToMessage(kick_check_group_not_exist)) == "group not exist");
    CHECK(std::string(KickGroupMemberCheckToMessage(kick_check_not_admin))       == "only owner or admin can kick");
}

TEST_CASE("FWD-S6 群内发言的禁言判定（普通群发与转发到群共用）")
{
    //没有任何禁言：各种角色都能发言
    CHECK_FALSE(IsMutedForSpeaking(0, 0, group_role_member));
    CHECK_FALSE(IsMutedForSpeaking(0, 0, group_role_admin));
    CHECK_FALSE(IsMutedForSpeaking(0, 0, group_role_owner));

    //被单独禁言：不论角色都不能发言（与抽取前 sendGroupMessage 的判定一致）
    CHECK(IsMutedForSpeaking(1, 0, group_role_member));
    CHECK(IsMutedForSpeaking(1, 0, group_role_admin));

    //全员禁言：普通成员不能发言，群主与管理员放行
    CHECK(IsMutedForSpeaking(0, 1, group_role_member));
    CHECK_FALSE(IsMutedForSpeaking(0, 1, group_role_admin));
    CHECK_FALSE(IsMutedForSpeaking(0, 1, group_role_owner));

    //全员禁言同时被单独禁言：仍不能发言
    CHECK(IsMutedForSpeaking(1, 1, group_role_admin));
}
