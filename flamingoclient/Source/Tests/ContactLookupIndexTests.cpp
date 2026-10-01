/**
 *  联系人资料池 userid 索引的单元测试（Suite CIX），ContactLookupIndexTests.cpp
 *
 *  覆盖 balloonmain::ContactLookupIndex：
 *    CIX1  按 userid 查找 —— 存在的返回正确下标，不存在的返回 -1；
 *    CIX2  资料池里出现重复 userid —— 返回第一次出现的位置，与原先逐个比对的结果一致；
 *    CIX3  用另一份数据重建 —— 旧的 userid 查不到，新的查得到；
 *    CIX4  空资料池 —— 任何 userid 都返回 -1。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonMain/ContactLookupIndex.h"

using balloonmain::ContactLookupIndex;

namespace {

// 造一个只有 userid 的联系人，其余字段用默认值。
net::ContactItem MakeContact(int64_t userid)
{
    net::ContactItem c;
    c.m_userid = userid;
    return c;
}

// 原先主面板的查找方式（逐个比对、找到即返回），作为各用例的参照。
int LinearFind(const std::vector<net::ContactItem>& pool, unsigned int userId)
{
    for (size_t i = 0; i < pool.size(); ++i)
    {
        if ((unsigned int)pool[i].m_userid == userId)
        {
            return (int)i;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("CIX1 按 userid 查找:存在的返回正确下标,不存在的返回 -1", "[contactindex]")
{
    std::vector<net::ContactItem> pool;
    pool.push_back(MakeContact(100001));
    pool.push_back(MakeContact(100002));
    pool.push_back(MakeContact(100003));

    ContactLookupIndex index;
    index.Rebuild(pool);

    CHECK(index.Find(100001) == 0);
    CHECK(index.Find(100002) == 1);
    CHECK(index.Find(100003) == 2);
    CHECK(index.Find(999999) == -1);
    CHECK(index.Find(0) == -1);
    CHECK(index.Count() == 3);
}

TEST_CASE("CIX2 资料池里出现重复 userid:返回第一次出现的位置", "[contactindex]")
{
    std::vector<net::ContactItem> pool;
    pool.push_back(MakeContact(100001));
    pool.push_back(MakeContact(100002));
    pool.push_back(MakeContact(100001));   // 重复

    ContactLookupIndex index;
    index.Rebuild(pool);

    CHECK(index.Find(100001) == 0);
    CHECK(index.Find(100001) == LinearFind(pool, 100001));
    CHECK(index.Count() == 2);
}

TEST_CASE("CIX3 用另一份数据重建:旧的 userid 查不到,新的查得到", "[contactindex]")
{
    std::vector<net::ContactItem> first;
    first.push_back(MakeContact(100001));
    first.push_back(MakeContact(100002));

    std::vector<net::ContactItem> second;
    second.push_back(MakeContact(200001));
    second.push_back(MakeContact(100002));

    ContactLookupIndex index;
    index.Rebuild(first);
    REQUIRE(index.Find(100001) == 0);

    index.Rebuild(second);
    CHECK(index.Find(100001) == -1);
    CHECK(index.Find(200001) == 0);
    CHECK(index.Find(100002) == 1);   // 同一个人在新资料池里的下标变了
    CHECK(index.Count() == 2);

    index.Clear();
    CHECK(index.Find(200001) == -1);
    CHECK(index.Count() == 0);
}

TEST_CASE("CIX4 空资料池:任何 userid 都返回 -1", "[contactindex]")
{
    std::vector<net::ContactItem> pool;
    ContactLookupIndex index;
    index.Rebuild(pool);

    CHECK(index.Find(100001) == -1);
    CHECK(index.Count() == 0);

    // 从未 Rebuild 过的索引同样查不到任何人。
    ContactLookupIndex fresh;
    CHECK(fresh.Find(100001) == -1);
}
