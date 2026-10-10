/**
 *  建群与批量加人的写库测试（Suite GB），test_groupbulk.cpp
 *
 *  2026-10-08 用户反馈：新建群时选的成员一多（166 人），建群要等约 9 秒（开发服务端日志：15:41:42.906 收到请求，
 *  15:41:51.862 建完）。据推测原因是 GroupManager::createGroup 逐个成员单独 INSERT、每条各自提交，SQLite 每次提交
 *  都要刷盘；addMembers（邀请多人入群）同样如此。本套用例先在改动前量出耗时作为证据，改成一个事务内多行合并写入
 *  之后再量一次对比；同时检查写入结果在改动前后完全一致：
 *    GB1  客户端建群（createGroup 五参版本）：166 名成员，名单里混入重复的 userid 与群主本人；群主一行、成员 166 行，
 *         角色与入群时间正确，重复与群主本人被跳过
 *    GB2  管理后台建群（createGroup 含管理员的版本）：群主一行、管理员与普通成员各自的角色正确
 *    GB3  addMembers 一次加 200 人：全部写入、历史边界正确；同一批再加一次时幂等，已在群里的人的入群时间与历史边界不变
 *  耗时用 MESSAGE 打印，不作断言（取决于机器与磁盘）。
 *
 *  典型用法：Tests.exe --test-case="GB*"
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include <stdint.h>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../base/Singleton.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kGbDbPath = "flamingo_sqlite_groupbulk_test.db";
const char* const kGbMysqlDb = "flamingo_test_groupbulk";

//群主
const int32_t kOwner = 300001;
//成员 userid 的起点；成员依次取 kFirstMember、kFirstMember + 1 ……
const int64_t kFirstMember = 310001;
//GB1 的成员数，与用户反馈时的规模相同
const int kCreateMembers = 166;
//GB2 的管理员数与普通成员数
const int kAdminCount = 3;
const int kPlainCount = 160;
//GB3 一次加的人数，以及他们的入群时间、能看到的最早一条消息 msgid
const int kAddCount = 200;
const int64_t kHistoryFrom = 5000;
//建群时间与加人时间（Unix 毫秒），固定取值保证结果可复现
const int64_t kCreateTime = 1791000000000LL;
const int64_t kAddTime = 1791000100000LL;
//GB3 第二次加同一批人时用的入群时间与历史边界，用来确认已在群里的人不被改动
const int64_t kLaterAddTime = 1791000200000LL;
const int64_t kLaterHistoryFrom = 9000;

//连续 count 个 userid，从 first 开始
std::vector<int64_t> MakeIds(int64_t first, int count)
{
    std::vector<int64_t> ids;
    for (int i = 0; i < count; ++i)
        ids.push_back(first + i);
    return ids;
}

//从调用到现在过了多少毫秒
long long ElapsedMs(const std::chrono::steady_clock::time_point& start)
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

//取一个群的全部成员，按 userid 建索引
std::map<int64_t, GroupMemberRecord> LoadMembers(int64_t groupid)
{
    std::vector<GroupMemberRecord> rows;
    REQUIRE(Singleton<GroupManager>::Instance().getGroupMembers(groupid, rows));
    std::map<int64_t, GroupMemberRecord> byId;
    for (size_t i = 0; i < rows.size(); ++i)
        byId[rows[i].userid] = rows[i];
    REQUIRE(byId.size() == rows.size());
    return byId;
}

} // namespace

TEST_CASE("GB1 客户端建群 166 人：成员行与角色正确，重复的 userid 与群主本人被跳过，打印耗时")
{
    testdb::resetDb(kGbDbPath, kGbMysqlDb);
    GroupManager& mgr = Singleton<GroupManager>::Instance();

    //名单：166 名成员，外加一个重复的 userid 与群主本人（真实请求在 ChatSession 里已去重，这里确认写库层自己也挡得住）
    std::vector<int64_t> ids = MakeIds(kFirstMember, kCreateMembers);
    ids.push_back(kFirstMember);
    ids.push_back(kOwner);

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const int64_t groupid = mgr.createGroup("", kOwner, ids, kCreateTime);
    const long long ms = ElapsedMs(start);
    MESSAGE("GB1 createGroup with " << kCreateMembers << " members took " << ms << " ms");
    REQUIRE(groupid > 0);

    std::map<int64_t, GroupMemberRecord> members = LoadMembers(groupid);
    CHECK(members.size() == static_cast<size_t>(kCreateMembers + 1));
    REQUIRE(members.count(kOwner) == 1);
    CHECK(members[kOwner].role == group_role_owner);
    for (int i = 0; i < kCreateMembers; ++i)
    {
        const int64_t uid = kFirstMember + i;
        REQUIRE(members.count(uid) == 1);
        CHECK(members[uid].role == group_role_member);
        CHECK(members[uid].jointime == kCreateTime);
    }

    testdb::cleanupDb(kGbDbPath);
}

TEST_CASE("GB2 管理后台建群：群主、管理员与普通成员的角色各自正确，打印耗时")
{
    testdb::resetDb(kGbDbPath, kGbMysqlDb);
    GroupManager& mgr = Singleton<GroupManager>::Instance();

    const std::vector<int64_t> admins = MakeIds(kFirstMember, kAdminCount);
    const std::vector<int64_t> plain = MakeIds(kFirstMember + kAdminCount, kPlainCount);

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const int64_t groupid = mgr.createGroup("gb2", kOwner, admins, plain, kCreateTime);
    const long long ms = ElapsedMs(start);
    MESSAGE("GB2 createGroup(admin) with " << (kAdminCount + kPlainCount) << " members took " << ms << " ms");
    REQUIRE(groupid > 0);

    std::map<int64_t, GroupMemberRecord> members = LoadMembers(groupid);
    CHECK(members.size() == static_cast<size_t>(1 + kAdminCount + kPlainCount));
    CHECK(members[kOwner].role == group_role_owner);
    for (size_t i = 0; i < admins.size(); ++i)
        CHECK(members[admins[i]].role == group_role_admin);
    for (size_t i = 0; i < plain.size(); ++i)
    {
        REQUIRE(members.count(plain[i]) == 1);
        CHECK(members[plain[i]].role == group_role_member);
    }

    testdb::cleanupDb(kGbDbPath);
}

TEST_CASE("GB3 一次加 200 人：全部写入、历史边界正确；同一批再加一次时幂等，打印耗时")
{
    testdb::resetDb(kGbDbPath, kGbMysqlDb);
    GroupManager& mgr = Singleton<GroupManager>::Instance();
    const int64_t groupid = mgr.createGroup("gb3", kOwner, std::vector<int64_t>(), kCreateTime);
    REQUIRE(groupid > 0);

    const std::vector<int64_t> ids = MakeIds(kFirstMember, kAddCount);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    REQUIRE(mgr.addMembers(groupid, ids, kAddTime, kHistoryFrom));
    const long long ms = ElapsedMs(start);
    MESSAGE("GB3 addMembers with " << kAddCount << " users took " << ms << " ms");

    std::map<int64_t, GroupMemberRecord> members = LoadMembers(groupid);
    CHECK(members.size() == static_cast<size_t>(kAddCount + 1));
    for (size_t i = 0; i < ids.size(); ++i)
    {
        REQUIRE(members.count(ids[i]) == 1);
        CHECK(members[ids[i]].role == group_role_member);
        CHECK(members[ids[i]].jointime == kAddTime);
        CHECK(members[ids[i]].historyfrom == kHistoryFrom);
    }

    //同一批人再加一次：成功返回，行数不变，已在群里的人的入群时间与历史边界保持原样
    REQUIRE(mgr.addMembers(groupid, ids, kLaterAddTime, kLaterHistoryFrom));
    members = LoadMembers(groupid);
    CHECK(members.size() == static_cast<size_t>(kAddCount + 1));
    CHECK(members[ids[0]].jointime == kAddTime);
    CHECK(members[ids[0]].historyfrom == kHistoryFrom);
    CHECK(members[ids[kAddCount - 1]].jointime == kAddTime);

    //空名单：直接成功，什么也不写
    CHECK(mgr.addMembers(groupid, std::vector<int64_t>(), kAddTime, kHistoryFrom));
    CHECK(LoadMembers(groupid).size() == static_cast<size_t>(kAddCount + 1));

    testdb::cleanupDb(kGbDbPath);
}
