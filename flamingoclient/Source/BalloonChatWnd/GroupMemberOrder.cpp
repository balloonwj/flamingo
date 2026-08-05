/**
 *  GroupMemberOrder.cpp
 *
 *  群成员侧栏显示顺序的纯逻辑实现。说明见 GroupMemberOrder.h。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 GroupMentionModel.cpp 同例。
#include "GroupMemberOrder.h"

namespace balloonchatwnd {

namespace {

// 角色值。刻意<u>不</u> include net/Protocol.h —— 那会把协议层依赖带进这个纯逻辑
// 模块。两处数值必须一致，改动时一起改（Protocol.h 的 group_member_role）。
const int kRoleMember = 0;   // 普通成员
const int kRoleAdmin  = 1;   // 管理员
const int kRoleOwner  = 2;   // 群主

// 排序档位：数值越小排得越靠前。直接拿角色值当档位是不行的 —— 角色值里
// 群主(2) 比普通成员(0) 大，而显示上群主要排在最前面，顺序恰好相反；
// 何况未知角色也得有个确定的落点。
const int kBucketOwner   = 0;
const int kBucketAdmin   = 1;
const int kBucketMember  = 2;

// 把角色值映射成排序档位。未知角色一律当普通成员处理，排在最后但绝不丢弃。
int BucketOfRole(int role)
{
    if (role == kRoleOwner)
    {
        return kBucketOwner;
    }
    if (role == kRoleAdmin)
    {
        return kBucketAdmin;
    }
    if (role != kRoleMember)
    {
        // 服务端将来加了新角色时会走到这里。按普通成员排，不让人从列表里消失。
        return kBucketMember;
    }
    return kBucketMember;
}

} // anonymous namespace

std::vector<std::size_t> OrderMemberIndicesByRole(const std::vector<int>& roles)
{
    std::vector<std::size_t> out;
    out.reserve(roles.size());

    // 分三趟按档位收集下标。这么写而不是调 std::stable_sort 加比较器，是因为
    // 档位只有三个、趟数固定，顺序扫描天然稳定，也省得为一个三值比较写谓词
    // （仓库约定尽量不用 lambda）。
    for (int bucket = kBucketOwner; bucket <= kBucketMember; ++bucket)
    {
        for (std::size_t i = 0; i < roles.size(); ++i)
        {
            if (BucketOfRole(roles[i]) == bucket)
            {
                out.push_back(i);
            }
        }
    }
    return out;
}

} // namespace balloonchatwnd
