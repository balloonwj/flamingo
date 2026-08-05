/**
 *  GroupRoleRules.cpp
 *
 *  群成员角色共用权限规则的实现。说明见 GroupRoleRules.h。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 GroupMentionMenu.cpp 同例。
#include "GroupRoleRules.h"

namespace grouprole {

bool CanSetAdmin(int myNetRole, int targetNetRole, bool targetIsSelf)
{
    // 只有群主能动管理员身份。不在本群的人（角色为负）自然也不行。
    if (myNetRole != kNetRoleOwner)
    {
        return false;
    }

    // 群主不能改自己：升无可升，降级须走"转让群主"。
    if (targetIsSelf)
    {
        return false;
    }

    // 目标已不在本群（角色为负）：菜单里给出这一项也点不动，服务端会回
    // "该成员已不在本群"。不如不显示。
    if (targetNetRole < 0)
    {
        return false;
    }

    // 目标是另一个群主：一群只有一个群主，正常数据里出不来；真出现了也不该
    // 让本端发出这个请求。
    if (targetNetRole == kNetRoleOwner)
    {
        return false;
    }

    return true;
}

bool ShouldOfferCancelAdmin(int targetNetRole)
{
    return targetNetRole == kNetRoleAdmin;
}

bool CanKickMember(int myNetRole, int targetNetRole, bool targetIsSelf)
{
    // 普通成员没有管理权；不在本群的人（角色为负）同样落进这里。
    // 与 CanSetAdmin 的差别就在这一行：那里要求必须是群主，这里管理员也放行。
    if (myNetRole != kNetRoleOwner && myNetRole != kNetRoleAdmin)
    {
        return false;
    }

    // 移出自己没有意义：主动退群走的是另一条路。
    if (targetIsSelf)
    {
        return false;
    }

    // 目标已不在本群：菜单里给出这一项也点不动，服务端会静默跳过。
    if (targetNetRole < 0)
    {
        return false;
    }

    // 群主是本群权限的顶点，谁都移不动。
    if (targetNetRole == kNetRoleOwner)
    {
        return false;
    }

    // 管理员之间互不隶属：管理员移不动其他管理员，只能移普通成员。
    if (myNetRole == kNetRoleAdmin && targetNetRole == kNetRoleAdmin)
    {
        return false;
    }

    return true;
}

bool CanChangeGroupAvatar(int myNetRole)
{
    // 群主与管理员可改；普通成员不行，角色未知（负值，群资料还没到）同样不行。
    return myNetRole == kNetRoleOwner || myNetRole == kNetRoleAdmin;
}

} // namespace grouprole
