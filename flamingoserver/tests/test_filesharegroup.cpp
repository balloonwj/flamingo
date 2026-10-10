/**
 *  群成员变动与文件共享联动的单元测试（Suite SHG），test_filesharegroup.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 9 步的 9a（2026-10-02 与用户确认的用例）：
 *    - SHG1 有人离开群（退群或被踢）时的处理计划；
 *    - SHG2 按会话列出有效共享（解散群时据此全部取消）；
 *    - SHG3 chatserver 启动时一致性清扫的计划；
 *    - SHG4 启动清扫落库（从库里读出群与成员、算计划、落库），再执行一次不再有变化。
 *  SHG1、SHG3 是纯函数；SHG2、SHG4 打开临时库跑 SQL（默认 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL）。
 *
 *  典型用法：Tests.exe --test-case="SHG*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../base/Singleton.h"
#include "../chatserversrc/FileShareManager.h"
#include "../chatserversrc/FileSharePolicy.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"

namespace {

//A、B、C、D 四个人；两个群
const int64_t kUserA = 100001;
const int64_t kUserB = 100002;
const int64_t kUserC = 100003;
const int64_t kUserD = 100004;
const int64_t kGroupG = 900001;
const int64_t kGroupG2 = 900002;

//本套数据库用例专用的临时库文件与 MySQL 测试库名
const char* const kShgDbPath = "flamingo_sqlite_filesharegroup_test.db";
const char* const kShgMysqlDb = "flamingo_test_shg";

//清扫时写入的取消时间
const int64_t kNowMs = 1759370000000LL;

ShareMemberSpec Member(int64_t userId, int perms)
{
    ShareMemberSpec m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

//一条有效的共享记录（纯函数用例用，shareid 由调用方指定）
ShareRecord Share(int64_t shareId, int64_t owner, int ctype, int64_t cid)
{
    ShareRecord rec;
    rec.m_shareId = shareId;
    rec.m_ownerId = owner;
    rec.m_enterpriseId = 7;
    rec.m_deviceId = "PC-0001";
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_kind = share_kind_folder;
    rec.m_name = "n";
    rec.m_state = FileShareManager::kShareRecordActive;
    return rec;
}

std::vector<int64_t> Ids(int64_t a)
{
    return std::vector<int64_t>(1, a);
}

std::vector<int64_t> Ids(int64_t a, int64_t b)
{
    std::vector<int64_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

//在临时库里建一个共享并要求成功，返回 shareid
int64_t CreateShare(int64_t owner, int ctype, int64_t cid, const char* clientShareId,
                    const std::vector<ShareMemberSpec>& members)
{
    ShareRecord rec = Share(0, owner, ctype, cid);
    rec.m_clientShareId = clientShareId;
    rec.m_createTime = kNowMs;
    int64_t shareId = 0;
    bool created = false;
    REQUIRE(Singleton<FileShareManager>::Instance().createShare(rec, members, shareId, created) == error_code_ok);
    REQUIRE(created);
    return shareId;
}

//某个共享当前的成员 userid（升序）
std::vector<int64_t> MemberIdsOf(int64_t shareId)
{
    std::vector<ShareMemberSpec> members;
    REQUIRE(Singleton<FileShareManager>::Instance().getMembers(shareId, members));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < members.size(); ++i)
        ids.push_back(members[i].m_userId);
    return ids;
}

//在临时库里建一个群及其成员（只写必填列）
void InsertGroup(int64_t groupId, int64_t ownerId, const std::vector<int64_t>& members)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    std::string sql = "INSERT INTO t_group(f_groupid, f_name, f_ownerid) VALUES(" + std::to_string(static_cast<long long>(groupId))
        + ", 'g', " + std::to_string(static_cast<long long>(ownerId)) + ")";
    REQUIRE(pConn->execute(sql.c_str()));
    for (size_t i = 0; i < members.size(); ++i)
    {
        sql = "INSERT INTO t_group_member(f_groupid, f_userid) VALUES(" + std::to_string(static_cast<long long>(groupId))
            + ", " + std::to_string(static_cast<long long>(members[i])) + ")";
        REQUIRE(pConn->execute(sql.c_str()));
    }
}

//某个共享当前的状态与取消原因
void StateOf(int64_t shareId, int& state, int& reason)
{
    ShareRecord rec;
    REQUIRE(Singleton<FileShareManager>::Instance().getShare(shareId, rec));
    state = rec.m_state;
    reason = rec.m_cancelReason;
}

} // anonymous namespace

TEST_CASE("SHG1 有人离开群时的处理计划")
{
    //群 G：A 共享的 1（成员 B、C）、B 共享的 2（成员 A、C）、C 共享的 3（成员 A）、B 共享且已取消的 4（成员 C）
    //群 G2：B 共享的 5（成员 C）
    std::vector<ShareRecord> shares;
    shares.push_back(Share(1, kUserA, conversation_group, kGroupG));
    shares.push_back(Share(2, kUserB, conversation_group, kGroupG));
    shares.push_back(Share(3, kUserC, conversation_group, kGroupG));
    ShareRecord canceled = Share(4, kUserB, conversation_group, kGroupG);
    canceled.m_state = FileShareManager::kShareRecordCanceled;
    shares.push_back(canceled);
    shares.push_back(Share(5, kUserB, conversation_group, kGroupG2));

    std::map<int64_t, std::vector<ShareMemberSpec> > membersOf;
    membersOf[1].push_back(Member(kUserB, 0));
    membersOf[1].push_back(Member(kUserC, 0));
    membersOf[2].push_back(Member(kUserA, 0));
    membersOf[2].push_back(Member(kUserC, 0));
    membersOf[3].push_back(Member(kUserA, 0));
    membersOf[4].push_back(Member(kUserC, 0));
    membersOf[5].push_back(Member(kUserC, 0));

    //B 离开群 G：他共享的 2 取消（已取消的 4 与群 G2 的 5 不动），他所在的 1 把他移出
    ShareLeavePlan plan = FileSharePolicy::planUserLeftGroup(shares, membersOf, kGroupG, kUserB);
    CHECK(plan.m_cancelShareIds == Ids(2));
    CHECK(plan.m_removeFromShareIds == Ids(1));

    //C 离开群 G：他共享的 3 取消；他所在的 1、2 把他移出（已取消的 4 不动）
    plan = FileSharePolicy::planUserLeftGroup(shares, membersOf, kGroupG, kUserC);
    CHECK(plan.m_cancelShareIds == Ids(3));
    CHECK(plan.m_removeFromShareIds == Ids(1, 2));

    //D 既没共享也不是成员：什么都不做
    plan = FileSharePolicy::planUserLeftGroup(shares, membersOf, kGroupG, kUserD);
    CHECK(plan.m_cancelShareIds.empty());
    CHECK(plan.m_removeFromShareIds.empty());
}

TEST_CASE("SHG2 按会话列出有效共享：只返回这个群里仍然有效的")
{
    testdb::resetDb(kShgDbPath, kShgMysqlDb);
    FileShareManager& mgr = Singleton<FileShareManager>::Instance();

    const int64_t s1 = CreateShare(kUserA, conversation_group, kGroupG, "u1", std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    const int64_t s2 = CreateShare(kUserB, conversation_group, kGroupG, "u2", std::vector<ShareMemberSpec>(1, Member(kUserA, 0)));
    const int64_t s3 = CreateShare(kUserA, conversation_group, kGroupG, "u3", std::vector<ShareMemberSpec>(1, Member(kUserC, 0)));
    CreateShare(kUserA, conversation_group, kGroupG2, "u4", std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    CreateShare(kUserA, conversation_single, kUserB, "u5", std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    REQUIRE(mgr.cancelShare(s3, share_invalid_owner_canceled, kUserA, kNowMs) == error_code_ok);

    std::vector<ShareRecord> list;
    REQUIRE(mgr.listActiveConversationShares(conversation_group, kGroupG, list));
    REQUIRE(list.size() == 2);
    CHECK(list[0].m_shareId == s1);
    CHECK(list[1].m_shareId == s2);

    REQUIRE(mgr.listActiveConversationShares(conversation_group, 900099, list));
    CHECK(list.empty());

    testdb::cleanupDb(kShgDbPath);
}

TEST_CASE("SHG3 启动一致性清扫的计划")
{
    //群 G 现有成员 A、B、C；群 G2 已不存在
    std::map<int64_t, std::set<int64_t> > groupMembersOf;
    groupMembersOf[kGroupG].insert(kUserA);
    groupMembersOf[kGroupG].insert(kUserB);
    groupMembersOf[kGroupG].insert(kUserC);

    std::vector<ShareRecord> shares;
    shares.push_back(Share(1, kUserA, conversation_group, kGroupG));    //一切正常
    shares.push_back(Share(2, kUserD, conversation_group, kGroupG));    //分享者 D 已不在群里
    shares.push_back(Share(3, kUserB, conversation_group, kGroupG));    //成员 D 已不在群里
    shares.push_back(Share(4, kUserA, conversation_group, kGroupG2));   //群已不存在
    shares.push_back(Share(5, kUserA, conversation_single, kUserD));    //单聊共享不参与清扫
    ShareRecord canceled = Share(6, kUserD, conversation_group, kGroupG2);
    canceled.m_state = FileShareManager::kShareRecordCanceled;          //已取消的不参与
    shares.push_back(canceled);

    std::map<int64_t, std::vector<ShareMemberSpec> > membersOf;
    membersOf[1].push_back(Member(kUserB, 0));
    membersOf[2].push_back(Member(kUserA, 0));
    membersOf[3].push_back(Member(kUserC, 0));
    membersOf[3].push_back(Member(kUserD, 0));
    membersOf[4].push_back(Member(kUserB, 0));
    membersOf[5].push_back(Member(kUserD, 0));

    const ShareSweepPlan plan = FileSharePolicy::planGroupSweep(shares, membersOf, groupMembersOf);
    REQUIRE(plan.m_cancels.size() == 2);
    CHECK(plan.m_cancels[0].m_shareId == 2);
    CHECK(plan.m_cancels[0].m_reason == share_invalid_owner_left_group);
    CHECK(plan.m_cancels[1].m_shareId == 4);
    CHECK(plan.m_cancels[1].m_reason == share_invalid_group_dismissed);
    REQUIRE(plan.m_removeMembers.size() == 1);
    REQUIRE(plan.m_removeMembers.count(3) == 1);
    CHECK(plan.m_removeMembers.find(3)->second == Ids(kUserD));
}

TEST_CASE("SHG4 启动清扫落库，再执行一次不再有变化")
{
    testdb::resetDb(kShgDbPath, kShgMysqlDb);
    FileShareManager& mgr = Singleton<FileShareManager>::Instance();

    std::vector<ShareMemberSpec> bc;
    bc.push_back(Member(kUserB, 0));
    bc.push_back(Member(kUserC, 0));
    std::vector<ShareMemberSpec> cd;
    cd.push_back(Member(kUserC, 0));
    cd.push_back(Member(kUserD, 0));
    const int64_t ok = CreateShare(kUserA, conversation_group, kGroupG, "u1", bc);
    const int64_t ownerGone = CreateShare(kUserD, conversation_group, kGroupG, "u2", std::vector<ShareMemberSpec>(1, Member(kUserA, 0)));
    const int64_t memberGone = CreateShare(kUserB, conversation_group, kGroupG, "u3", cd);
    const int64_t groupGone = CreateShare(kUserA, conversation_group, kGroupG2, "u4", std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    const int64_t single = CreateShare(kUserA, conversation_single, kUserD, "u5", std::vector<ShareMemberSpec>(1, Member(kUserD, 0)));

    //群 G 存在，成员 A、B、C；群 G2 不存在
    std::vector<int64_t> abc;
    abc.push_back(kUserA);
    abc.push_back(kUserB);
    abc.push_back(kUserC);
    InsertGroup(kGroupG, kUserA, abc);

    //从库里读出有效群共享涉及的群与成员：G 有成员表，G2 不在结果里
    std::set<int64_t> groupIds;
    REQUIRE(mgr.listActiveShareGroupIds(groupIds));
    CHECK(groupIds.size() == 2);
    CHECK(groupIds.count(kGroupG) == 1);
    CHECK(groupIds.count(kGroupG2) == 1);
    std::map<int64_t, std::set<int64_t> > groupMembersOf;
    REQUIRE(mgr.loadGroupMembership(groupIds, groupMembersOf));
    REQUIRE(groupMembersOf.size() == 1);
    CHECK(groupMembersOf[kGroupG].size() == 3);

    ShareSweepPlan applied;
    REQUIRE(mgr.sweepGroupShares(groupMembersOf, kNowMs, applied));
    CHECK(applied.m_cancels.size() == 2);
    CHECK(applied.m_removeMembers.size() == 1);

    int state = 0;
    int reason = 0;
    StateOf(ok, state, reason);
    CHECK(state == FileShareManager::kShareRecordActive);
    CHECK(MemberIdsOf(ok) == Ids(kUserB, kUserC));
    StateOf(ownerGone, state, reason);
    CHECK(state == FileShareManager::kShareRecordCanceled);
    CHECK(reason == share_invalid_owner_left_group);
    StateOf(memberGone, state, reason);
    CHECK(state == FileShareManager::kShareRecordActive);
    CHECK(MemberIdsOf(memberGone) == Ids(kUserC));
    StateOf(groupGone, state, reason);
    CHECK(state == FileShareManager::kShareRecordCanceled);
    CHECK(reason == share_invalid_group_dismissed);
    StateOf(single, state, reason);
    CHECK(state == FileShareManager::kShareRecordActive);

    //再执行一次：没有任何变化
    ShareSweepPlan again;
    REQUIRE(mgr.sweepGroupShares(groupMembersOf, kNowMs, again));
    CHECK(again.m_cancels.empty());
    CHECK(again.m_removeMembers.empty());

    testdb::cleanupDb(kShgDbPath);
}
