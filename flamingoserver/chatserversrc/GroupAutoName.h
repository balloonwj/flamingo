/**
 *  自动群名的生成规则，GroupAutoName.h
 *
 *  建群时没填名字、或群主把群名清空时，群名由服务端按当前成员的昵称生成（t_group
 *  f_name_custom = 0），成员增减、转让群主、成员改昵称后重算。本文件只放生成规则
 *  这一个纯函数，不碰数据库，便于单元测试；取成员、写库、推送在 GroupManager 与
 *  ChatSession 里。
 *
 *  典型用法：
 *      std::vector<GroupNameMember> members = ...;   //群内全部成员（含群主）
 *      std::string name = BuildAutoGroupName(ownerid, members);
 *
 *  balloonwj@qq.com   2026-09-30
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace chatserver {

/**
 *  生成自动群名时用到的一名成员的信息。
 */
struct GroupNameMember
{
    int64_t     userid;     // 成员 userid
    std::string nickname;   // 成员昵称（UTF-8）；为空的成员不出现在群名里
    int64_t     jointime;   // 入群时间（Unix 毫秒），决定成员在群名里的先后
};

/**
 *  按规则生成自动群名：
 *    · 不含群主，其余成员按入群时间先后排序（时间相同按 userid 升序），跳过昵称为空的；
 *    · 取前 4 个昵称用"、"连接，有昵称的成员多于 4 人时末尾加"等"；
 *    · 除群主外没有可用昵称的成员时，为"群聊"；
 *    · 结果最长 64 个字（与 t_group.f_name 的 varchar(64) 一致），超出时按字符截断，
 *      不会截出半个汉字。
 *
 *    ownerid：群主 userid。
 *    members：群内全部成员（可含群主，顺序任意）。
 *  @return 自动群名（UTF-8）。纯函数，无副作用。
 */
std::string BuildAutoGroupName(int64_t ownerid, const std::vector<GroupNameMember>& members);

} // namespace chatserver
