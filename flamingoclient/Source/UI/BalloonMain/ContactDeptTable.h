/**
 *  联系人部门信息表：一次扫描全部部门，得出每个人的职务与部门路径（如"研发中心 / 客户端组"）。
 *
 *  主面板原先对每个联系人单独计算一次：每次都把全部部门重建一遍 deptid 索引、再逐个部门
 *  扫描成员找这个人。5000 人、120 个部门时，灌一次通讯录要为此花一秒多。本函数改为扫描
 *  一遍、每个部门的路径只拼一次，结果与逐人计算完全一致（Tests/ContactDeptTableTests.cpp
 *  的 CDT7 以原算法为参照逐人比对）。
 *
 *  选部门的规则（与原算法相同）：按部门在数组中的先后顺序，取此人第一次作为"主部门成员"
 *  （m_isprimary == 1）出现的部门，职务取那一条成员记录；从未作为主部门成员出现时，取他
 *  第一次出现的部门及那一条的职务。
 *
 *  纯逻辑，只依赖 STL，不依赖 stdafx.h 与 ATL，与 FlamingoUnit 测试工程共用同一份源码。
 *
 *  典型用法：
 *      std::map<unsigned int, balloonmain::ContactDeptInfo> table;
 *      balloonmain::BuildContactDeptTable(result.m_departments, table);
 *      std::map<unsigned int, balloonmain::ContactDeptInfo>::const_iterator it = table.find(uid);
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../net/ContactsProtocolClient.h"   // net::DepartmentItem

namespace balloonmain {

// 部门路径最多包含的层数。上级关系数据异常成环时，据它保证路径计算一定结束。
const int kDeptPathMaxDepth = 32;

// 部门路径中各级部门名之间的分隔符（UTF-8）。
extern const char* const kDeptPathSeparator;

/**
 *  一个人的部门信息，字符串均为 UTF-8。
 */
struct ContactDeptInfo
{
    std::string m_position;   // 在所取部门中的职务，可能为空串
    std::string m_deptPath;   // 从顶级部门到所取部门的路径，各级之间用 kDeptPathSeparator 分隔
};

/**
 *  扫描全部部门，得出每个部门成员的职务与部门路径。
 *    depts：部门数组（扁平存储，靠 m_parentdeptid 还原成树），顺序即选部门时的先后顺序。
 *    out：  输出，键为成员 userid 的低 32 位（与主面板按 UINT 传递 userid 的口径一致）；
 *           原有内容被清空。不在任何部门中的人不出现在表里。
 */
void BuildContactDeptTable(const std::vector<net::DepartmentItem>& depts,
                           std::map<unsigned int, ContactDeptInfo>& out);

} // namespace balloonmain
