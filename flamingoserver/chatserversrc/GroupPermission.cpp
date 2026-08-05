/**
 *  群管理操作权限判定的实现。判定顺序与 GroupPermission.h 里 enum 的取值顺序
 *  一致，改动顺序等于改动用户看到的提示，请同步更新 tests/test_grouppermission.cpp。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#include "GroupPermission.h"

#include "Protocol.h"   //group_member_role / error_code

namespace chatserver {

set_member_role_check CheckSetMemberRole(bool groupExists, int operatorRole,
                                         int targetRole, bool targetIsSelf,
                                         int requestedRole)
{
    //先看请求本身合不合法，再去看群与人 —— 一个连角色值都填错的请求，
    //不该因为"群恰好不存在"而得到"群不存在"这种误导性提示。
    if (requestedRole != group_role_member && requestedRole != group_role_admin)
    {
        return set_role_check_bad_role;
    }

    if (!groupExists)
    {
        return set_role_check_group_not_exist;
    }

    //仅群主可设置 / 撤销管理员。不在本群的人 operatorRole 为负，同样落进这里。
    if (operatorRole != group_role_owner)
    {
        return set_role_check_not_owner;
    }

    //群主改自己没有意义：升不上去，也不能自我降级（卸任须走 cmd_transfer_owner）。
    //这条排在"目标是否群成员"之前 —— 操作者既然是群主，他自己必然在群里，
    //先答"不能改自己"比答"目标不是群成员"更贴近他的实际意图。
    if (targetIsSelf)
    {
        return set_role_check_self;
    }

    //负值表示目标不在本群，见 GroupManager::getMemberRole 的约定
    if (targetRole < 0)
    {
        return set_role_check_not_member;
    }

    return set_role_check_ok;
}

int SetMemberRoleCheckToErrorCode(set_member_role_check result)
{
    switch (result)
    {
    case set_role_check_ok:
        return error_code_ok;
    case set_role_check_bad_role:
        return error_code_bad_request;
    case set_role_check_group_not_exist:
        return error_code_group_not_exist;
    case set_role_check_not_owner:
        return error_code_permission_denied;
    case set_role_check_self:
        return error_code_bad_request;
    case set_role_check_not_member:
        return error_code_not_group_member;
    default:
        //枚举被扩充却漏了这张表时兜底：报服务器内部错误，而不是静默当成功
        return error_code_server_internal;
    }
}

kick_member_check CheckKickGroupMember(bool groupExists, int operatorRole)
{
    if (!groupExists)
    {
        return kick_check_group_not_exist;
    }

    //群主与管理员都能移出成员。不在本群的人 operatorRole 为负，同样落进这里。
    //注意与 CheckSetMemberRole 的差别：设管理员仅限群主，移出则管理员也可以。
    if (operatorRole < group_role_admin)
    {
        return kick_check_not_admin;
    }

    return kick_check_ok;
}

int KickGroupMemberCheckToErrorCode(kick_member_check result)
{
    switch (result)
    {
    case kick_check_ok:
        return error_code_ok;
    case kick_check_group_not_exist:
        return error_code_group_not_exist;
    case kick_check_not_admin:
        return error_code_not_group_admin;
    default:
        //枚举被扩充却漏了这张表时兜底：报服务器内部错误，而不是静默当成功
        return error_code_server_internal;
    }
}

const char* KickGroupMemberCheckToMessage(kick_member_check result)
{
    switch (result)
    {
    case kick_check_ok:
        return "ok";
    case kick_check_group_not_exist:
        return "group not exist";
    case kick_check_not_admin:
        return "only owner or admin can kick";
    default:
        return "unknown error";
    }
}

bool CanKickTarget(int operatorRole, int targetRole, bool targetIsSelf)
{
    //操作者本身没资格：前置的 CheckKickGroupMember 已经拦过一道，这里再判一次
    //是为了让本函数单独拿出来用也自洽，不依赖调用顺序。
    if (operatorRole < group_role_admin)
    {
        return false;
    }

    //移出自己没有意义：主动退群走 cmd_quit_group，不是移出
    if (targetIsSelf)
    {
        return false;
    }

    //目标已不在本群（角色为负）
    if (targetRole < 0)
    {
        return false;
    }

    //群主是本群权限的顶点，谁都不能把他移出去
    if (targetRole == group_role_owner)
    {
        return false;
    }

    //管理员之间互不隶属：管理员只能移出普通成员，移不动其他管理员
    if (operatorRole == group_role_admin && targetRole >= group_role_admin)
    {
        return false;
    }

    return true;
}

const char* SetMemberRoleCheckToMessage(set_member_role_check result)
{
    switch (result)
    {
    case set_role_check_ok:
        return "ok";
    case set_role_check_bad_role:
        return "invalid role";
    case set_role_check_group_not_exist:
        return "group not exist";
    case set_role_check_not_owner:
        return "only group owner can set role";
    case set_role_check_self:
        return "cannot change your own role";
    case set_role_check_not_member:
        return "target is not a group member";
    default:
        return "unknown error";
    }
}

} // namespace chatserver
