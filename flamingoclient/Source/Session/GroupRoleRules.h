/**
 *  GroupRoleRules.h
 *
 *  群成员角色相关的<u>共用</u>权限规则 —— 纯逻辑，不碰 HWND、不碰 socket。
 *
 *  为什么单独成模块：设 / 撤管理员这条规则有两个入口要用 —— 群管理窗口的成员卡
 *  右键菜单（BalloonGroupManage/GroupManageMapping）和群聊窗口成员侧栏 / 气泡里
 *  @某人 的右键菜单（BalloonChatWnd/GroupMentionMenu）。两处各写一份的话，
 *  日后改规则必然漏掉一处，菜单该出现的时候不出现、不该出现的时候出现。
 *
 *  与服务端的关系：服务端 chatserversrc/GroupPermission.h 的 CheckSetMemberRole
 *  才是权威，本模块只是"不让用户白点"的前置门控，两边规则须保持一致。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace grouprole {

/**
 *  群成员角色取值，与协议报文里的 role 字段、服务端 group_member_role 一致。
 *
 *  本模块<u>刻意不</u> include net/Protocol.h：原先是因为群管理窗口所在的编译单元同时
 *  包含 v1 的 net/Msg.h、两者有同名枚举冲突（v1 协议头已于 2026-08-14 删除，该限制
 *  不复存在），现在的理由是本模块要能被测试工程独立编译。故角色取值在此复制一份，
 *  <u>取值必须与 net/Protocol.h 的 group_member_role 保持一致</u>，改那边就要改这里。
 *
 *  注意与界面侧的 BalloonGroupManage/GroupManageModel.h 里 GroupRole 区分开 ——
 *  那一套的顺序<u>正好相反</u>（群主为 0、普通成员为 2），只有管理员同为 1。
 *  本模块一律使用下面这套网络侧取值，界面侧调用前须先经 UiRoleToNetRole 转换。
 */
enum NetGroupRole
{
    kNetRoleMember = 0,   //普通成员：无任何管理权限
    kNetRoleAdmin  = 1,   //管理员：由群主指派，可禁言 / 移出普通成员
    kNetRoleOwner  = 2,   //群主：本群权限的顶点，每群唯一一人
};

/**
 *  判断"设为管理员 / 撤销管理员"这一项该不该出现在针对某成员的菜单里。
 *
 *  规则与服务端 CheckSetMemberRole 对齐：
 *    · 只有群主能设 / 撤管理员，管理员和普通成员都不行；
 *    · 不能对自己操作 —— 群主卸任须走"转让群主"；
 *    · 不能对另一个群主操作 —— 群主是权限顶点（真实数据里一群只有一个群主，
 *      此条主要防御列表未刷新等异常输入）。
 *
 *    myNetRole：     登录者在本群的角色，取值见 NetGroupRole；负值表示不在本群。
 *    targetNetRole： 目标成员在本群的角色，取值同上；负值表示已不在本群。
 *    targetIsSelf：  目标成员是不是登录者本人。
 *
 *  @return true 表示该项应当出现。纯函数，无副作用。
 */
bool CanSetAdmin(int myNetRole, int targetNetRole, bool targetIsSelf);

/**
 *  在 CanSetAdmin 已为真的前提下，判断该显示"撤销管理员"还是"设为管理员"。
 *
 *    targetNetRole： 目标成员当前角色，取值见 NetGroupRole。
 *
 *  @return true 表示目标已是管理员，应显示"撤销管理员"；false 显示"设为管理员"。
 *  纯函数，无副作用。
 */
bool ShouldOfferCancelAdmin(int targetNetRole);

/**
 *  判断"移出本群"这一项该不该出现在针对某成员的菜单里。
 *
 *  规则与服务端 CanKickTarget 对齐（注意与 CanSetAdmin <u>不同</u>——
 *  设 / 撤管理员仅限群主，移出则管理员也能做）：
 *    · 群主可移出任何非群主成员（管理员与普通成员都能移）；
 *    · 管理员只能移出普通成员，移不动其他管理员，更移不动群主；
 *    · 普通成员一律不能；
 *    · 谁都不能移出自己 —— 主动退群走的是另一条路，不是"移出"。
 *
 *    myNetRole：     登录者在本群的角色，取值见 NetGroupRole；负值表示不在本群。
 *    targetNetRole： 目标成员在本群的角色，取值同上；负值表示已不在本群。
 *    targetIsSelf：  目标成员是不是登录者本人。
 *
 *  @return true 表示该项应当出现。纯函数，无副作用。
 */
bool CanKickMember(int myNetRole, int targetNetRole, bool targetIsSelf);

/**
 *  判断登录者能不能改本群的群头像（群管理窗基本信息页的「更换头像」按钮据此
 *  决定可点还是置灰）。
 *
 *  规则与服务端 ChatSession::onUpdateGroupInfo 对齐 —— 那里对 cmd_update_group_info
 *  统一要求 myrole >= group_role_admin，普通成员发上去只会拿回
 *  error_code_not_group_admin，故本端提前挡住，不让用户白点。
 *
 *    myNetRole： 登录者在本群的角色，取值见 NetGroupRole；<u>负值表示角色未知</u>
 *                （群资料还没到），此时一律返回 false —— 群资料到达后界面会重建，
 *                那时再放开，比"先给点、点了才发现没权限"稳妥。
 *
 *  @return true 表示可以改。纯函数，无副作用。
 */
bool CanChangeGroupAvatar(int myNetRole);

//群成员变更推送（cmd_group_member_changed_notify）里"角色变更"的 changetype 取值。设 / 撤
//管理员与转让群主都用它。取值必须与 net/Protocol.h 的 group_member_change_role 一致，在此
//复制一份的理由同 NetGroupRole。
const int kMemberChangeRole = 4;

/**
 *  收到群成员变更推送后，判断要不要为刷新"本人在本群的角色"补拉一次群资料（cmd_get_group_info）。
 *
 *  推送只带"谁的角色变了"，不带变成了什么；本人的角色只在群资料应答里回填（myrole 字段），
 *  客户端各处判断本人角色用的都是那份缓存。名单里有本人的角色变更（被设为 / 撤销管理员、
 *  转让群主时的新旧群主）就要补拉，否则缓存要等重新登录才更新。其它类型的变更与本人角色无关。
 *
 *    changetype：推送的变更类型，角色变更为 kMemberChangeRole。
 *    userids：   推送里本次发生变动的成员 userid 名单。
 *    selfId：    当前登录者的 userid；0 表示未登录，此时一律返回 false。
 *  @return 需要补拉返回 true。纯函数，无副作用。
 */
bool ShouldRefetchMyRoleOnMemberChange(int changetype, const std::vector<int64_t>& userids,
                                       int64_t selfId);

} // namespace grouprole
