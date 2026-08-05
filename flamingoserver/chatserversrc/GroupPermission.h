/**
 *  群管理操作的权限判定 —— <u>纯逻辑</u>部分：只回答"这次操作准不准"，
 *  不碰数据库、不碰连接、不发包。
 *
 *  为什么单独成文件：原先这些判定内联在 ChatSession 的各个 onXxx 处理函数里，
 *  夹在 GroupManager 的库查询与 send() 发包之间，没法单独构造输入去断言，
 *  于是"仅群主可设管理员""不能改自己的角色"这类规则一直没有测试兜底。
 *  把判定摘成纯函数后，ChatSession 只负责取数据、按结论回错误码，规则本身
 *  可以在 tests/test_grouppermission.cpp 里穷举所有角色组合。
 *
 *  典型用法（ChatSession::onSetMemberRole）：
 *      set_member_role_check r = CheckSetMemberRole(
 *          groupMgr.getGroup(groupid, group),          //群存不存在
 *          groupMgr.getMemberRole(groupid, myid),      //操作者角色
 *          groupMgr.getMemberRole(groupid, targetid),  //目标角色
 *          targetid == myid,                           //目标是不是自己
 *          req.value);                                 //请求设成什么角色
 *      if (r != set_role_check_ok)
 *          send(..., SetMemberRoleCheckToErrorCode(r), ...);
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

namespace chatserver {

/**
 *  设置 / 取消管理员（cmd_set_member_role）的权限判定结论。
 *
 *  取值顺序即判定优先级：同时踩中多条时返回靠前的那一条，好让同一种误操作
 *  每次都得到同一句提示，不会因为库里数据先后变化而飘。
 */
enum set_member_role_check
{
    set_role_check_ok = 0,           //允许执行：操作者是群主，目标是群内的另一个人，请求角色合法
    set_role_check_bad_role,         //请求的目标角色非法：本命令只能在普通成员与管理员之间切换，转让群主走 cmd_transfer_owner
    set_role_check_group_not_exist,  //群不存在：群已被解散，或客户端传了个不存在的群ID
    set_role_check_not_owner,        //操作者不是群主：管理员、普通成员、以及根本不在本群的人都归此类
    set_role_check_self,             //操作者试图修改自己的角色：群主要卸任须走转让群主
    set_role_check_not_member,       //目标不是本群成员：多半是他刚被移出群、而操作端的成员列表还没刷新
};

/**
 *  判定一次"设置 / 取消管理员"该不该放行。
 *
 *    groupExists：   目标群是否存在。调用方用 GroupManager::getGroup 的返回值填。
 *    operatorRole：  操作者在该群的角色，取值见 group_member_role；<u>负值表示他不在本群</u>
 *                    （与 GroupManager::getMemberRole 的约定一致）。
 *    targetRole：    目标用户在该群的角色，取值与 operatorRole 同，负值表示不在本群。
 *    targetIsSelf：  目标用户是不是操作者本人。
 *    requestedRole： 请求把目标设成的角色，取值见 group_member_role。
 *
 *  @return 判定结论；set_role_check_ok 表示放行，其余均为拒绝原因。
 *  本函数无副作用。
 */
set_member_role_check CheckSetMemberRole(bool groupExists, int operatorRole,
                                         int targetRole, bool targetIsSelf,
                                         int requestedRole);

/**
 *  把判定结论翻译成回给客户端的协议错误码（取值见 Protocol.h 的 error_code）。
 *
 *    result： CheckSetMemberRole 的返回值。
 *
 *  @return 对应的协议错误码；传入 set_role_check_ok 时返回 error_code_ok(0)。
 *  本函数无副作用。
 */
int SetMemberRoleCheckToErrorCode(set_member_role_check result);

/**
 *  把判定结论翻译成回给客户端的英文提示原文。
 *
 *    result： CheckSetMemberRole 的返回值。
 *
 *  @return 指向进程级静态常量串的指针，调用方不得释放；传入 set_role_check_ok 时返回 "ok"。
 *  本函数无副作用。
 */
const char* SetMemberRoleCheckToMessage(set_member_role_check result);

/**
 *  移出群成员（cmd_kick_group_member）的<u>前置</u>权限判定结论。
 *
 *  移出是批量操作（一次请求可带多个 userid），故判定分两层：本枚举回答
 *  "这个人有没有资格发起移出"，逐个目标能不能移出由 CanKickTarget 回答。
 */
enum kick_member_check
{
    kick_check_ok = 0,           //允许发起：群存在，且操作者是群主或管理员
    kick_check_group_not_exist,  //群不存在：群已被解散，或客户端传了个不存在的群ID
    kick_check_not_admin,        //操作者既不是群主也不是管理员，无权移出任何人
};

/**
 *  判定一次"移出群成员"请求该不该受理（前置层，与具体移出谁无关）。
 *
 *    groupExists：  目标群是否存在。调用方用 GroupManager::getGroup 的返回值填。
 *    operatorRole： 操作者在该群的角色，取值见 group_member_role；负值表示不在本群。
 *
 *  @return 判定结论；kick_check_ok 表示可以继续逐个筛选目标。本函数无副作用。
 */
kick_member_check CheckKickGroupMember(bool groupExists, int operatorRole);

/**
 *  把移出请求的前置判定结论翻译成协议错误码（取值见 Protocol.h 的 error_code）。
 *
 *    result： CheckKickGroupMember 的返回值。
 *
 *  @return 对应的协议错误码；传入 kick_check_ok 时返回 error_code_ok(0)。无副作用。
 */
int KickGroupMemberCheckToErrorCode(kick_member_check result);

/**
 *  把移出请求的前置判定结论翻译成回给客户端的英文提示原文。
 *
 *    result： CheckKickGroupMember 的返回值。
 *
 *  @return 指向进程级静态常量串的指针，调用方不得释放；kick_check_ok 时返回 "ok"。无副作用。
 */
const char* KickGroupMemberCheckToMessage(kick_member_check result);

/**
 *  判定某个具体目标能不能被移出本群（逐目标层）。
 *
 *  规则：
 *    · 不能移出自己 —— 主动退群走的是另一条命令，不是移出；
 *    · 目标须仍在本群（角色为负表示已不在，静默跳过而不是报错）；
 *    · 任何人都不能移出群主 —— 群主是本群权限的顶点；
 *    · 管理员只能移出普通成员，不能移出其他管理员；群主则可移出任何非群主成员。
 *
 *    operatorRole： 操作者在该群的角色，取值见 group_member_role；负值表示不在本群。
 *    targetRole：   目标在该群的角色，取值同上；负值表示已不在本群。
 *    targetIsSelf： 目标是不是操作者本人。
 *
 *  @return true 表示该目标可以被移出。本函数无副作用。
 *  批量请求里被本函数否掉的目标应当<u>静默跳过</u>（与抽取前的实现一致），
 *  而不是让整个请求失败 —— 一次踢多人时，个别人刚好已退群不该拖垮其余人。
 */
bool CanKickTarget(int operatorRole, int targetRole, bool targetIsSelf);

} // namespace chatserver
