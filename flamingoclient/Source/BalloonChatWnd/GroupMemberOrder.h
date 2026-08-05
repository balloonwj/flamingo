/**
 *  GroupMemberOrder.h
 *
 *  群成员侧栏的<u>显示顺序</u>——纯逻辑部分。不碰 HWND / balloonui / 业务对象，
 *  只吃一串角色值、吐出显示下标，便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  规则：群主排最前，其次管理员，最后普通成员；<u>同一角色内保持服务端返回的
 *  原有顺序</u>（稳定排序）。之所以强调稳定：群成员应答会在开窗、成员变更等
 *  多个时机重复到达，若同角色内的次序每次都可能变，用户眼里就是"列表自己在跳"。
 *
 *  为什么按下标而不是直接排成员结构：调用方手上是 net::GroupMemberItem 的数组，
 *  本模块若引入那个类型就沾上了协议层依赖，纯逻辑也就不纯了。返回下标既够用，
 *  又让本模块只依赖 <vector>。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <vector>
#include <cstddef>

namespace balloonchatwnd {

/**
 *  按角色算出群成员的显示顺序。
 *
 *    roles：各成员的角色值，下标与调用方的成员数组一一对应。取值见
 *           net/Protocol.h 的 group_member_role（0 普通成员 / 1 管理员 / 2 群主）；
 *           出现未知值时按"普通成员"处理，排在最后，绝不丢弃 —— 服务端将来加了
 *           新角色，客户端也不该让那个人从列表里凭空消失。
 *
 *  @return 显示顺序的下标序列，长度必与 roles 相同；roles 为空则返回空。
 *          例：roles = [0, 2, 0, 1] → 返回 [1, 3, 0, 2]（群主、管理员、其余按原序）。
 */
std::vector<std::size_t> OrderMemberIndicesByRole(const std::vector<int>& roles);

} // namespace balloonchatwnd
