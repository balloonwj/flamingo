/**
 *  文件共享记录、成员与审计存储的单元测试（Suite SHM），test_filesharemanager.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 7 步的 7a（2026-10-02 与用户确认的用例）：
 *    - SHM1 发起与重发去重；
 *    - SHM2 读取记录、成员与成员权限；
 *    - SHM3 统计有效共享数；
 *    - SHM4 「我的共享」与「共享给我的」两种列表，以及按会话过滤；
 *    - SHM5 取消与重复取消；
 *    - SHM6 调整成员（追加、移除、改权限）及实际变化的汇总；
 *    - SHM7 审计写入与读回；
 *    - SHM8 一批审计记录在一个事务里写入（第 18 步联调发现逐条提交时一批 200 条要 3 秒左右）；
 *    - SHM9 后台查共享（第 3 期第 2 步）：按企业、状态、分享者关键字过滤，分页与总数，分享者与会话的名字、成员数；
 *    - SHM10 后台读成员与权限，带昵称与账号；
 *    - SHM11 后台查审计：按企业、动作、时间、共享、操作人关键字过滤，倒序分页与总数；后台强制取消的那一条
 *            操作人显示管理员姓名、能按管理员姓名查到。
 *  真的打开临时库跑 SQL：默认 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL（见 test_dbbackend.h）。
 *
 *  典型用法：Tests.exe --test-case="SHM*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../chatserversrc/FileShareManager.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kShmDbPath = "flamingo_sqlite_filesharemgr_test.db";
const char* const kShmMysqlDb = "flamingo_test_shm";

//A 是分享者，B、C 是成员候选，D 是另一位分享者
const int64_t kUserA = 100001;
const int64_t kUserB = 100002;
const int64_t kUserC = 100003;
const int64_t kUserD = 100004;

//分享者所属企业与两个群
const int64_t kEnterprise = 7;
const int64_t kGroupG = 900001;
const int64_t kGroupG2 = 900002;

//时间轴：以 kT0 为起点每隔一秒
const int64_t kT0 = 1700000000000LL;
const int64_t kStepMs = 1000;

void ResetShmDb()
{
    testdb::resetDb(kShmDbPath, kShmMysqlDb);
}

void CleanupShmDb()
{
    testdb::cleanupDb(kShmDbPath);
}

FileShareManager& Mgr()
{
    return Singleton<FileShareManager>::Instance();
}

//一名成员
ShareMemberSpec Member(int64_t userId, int perms)
{
    ShareMemberSpec m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

//构造一条共享记录：owner 在会话 (ctype, cid) 里共享名为 name 的文件夹，第 index 秒发起
ShareRecord MakeRecord(int64_t owner, int ctype, int64_t cid, const char* clientShareId, const char* name, int index)
{
    ShareRecord rec;
    rec.m_clientShareId = clientShareId;
    rec.m_ownerId = owner;
    rec.m_enterpriseId = kEnterprise;
    rec.m_deviceId = "PC-0001";
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_kind = share_kind_folder;
    rec.m_name = name;
    rec.m_displayPath = std::string("D:\\") + name;
    rec.m_includeHidden = true;
    rec.m_createTime = kT0 + index * kStepMs;
    return rec;
}

//发起一个共享并要求新建成功，返回 shareid
int64_t MustCreate(const ShareRecord& rec, const std::vector<ShareMemberSpec>& members)
{
    int64_t shareId = 0;
    bool created = false;
    REQUIRE(Mgr().createShare(rec, members, shareId, created) == error_code_ok);
    REQUIRE(created);
    REQUIRE(shareId > 0);
    return shareId;
}

//把列表里的 shareid 依次取出
std::vector<int64_t> ShareIdsOf(const std::vector<ShareRecord>& list)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < list.size(); ++i)
        ids.push_back(list[i].m_shareId);
    return ids;
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

std::vector<ShareMemberSpec> Members(const ShareMemberSpec& a)
{
    return std::vector<ShareMemberSpec>(1, a);
}

std::vector<ShareMemberSpec> Members(const ShareMemberSpec& a, const ShareMemberSpec& b)
{
    std::vector<ShareMemberSpec> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

} // anonymous namespace

TEST_CASE("SHM1 发起共享：写入记录与成员，同一分享者用同一 clientshareid 重发不新建")
{
    ResetShmDb();

    const ShareRecord rec = MakeRecord(kUserA, conversation_group, kGroupG, "uuid-1", "Projects", 1);
    const std::vector<ShareMemberSpec> members = Members(Member(kUserB, share_perm_upload), Member(kUserC, 0));
    CHECK(Mgr().findShareIdByClientShareId(kUserA, "uuid-1") == 0);
    const int64_t first = MustCreate(rec, members);
    CHECK(Mgr().findShareIdByClientShareId(kUserA, "uuid-1") == first);
    CHECK(Mgr().findShareIdByClientShareId(kUserD, "uuid-1") == 0);

    //重发：返回同一个 shareid，不新建；成员以第一次为准
    int64_t again = 0;
    bool created = true;
    REQUIRE(Mgr().createShare(rec, Members(Member(kUserD, 0)), again, created) == error_code_ok);
    CHECK(again == first);
    CHECK_FALSE(created);
    std::vector<ShareMemberSpec> got;
    REQUIRE(Mgr().getMembers(first, got));
    CHECK(got.size() == 2);

    //另一位分享者用相同的 clientshareid：互不影响，新建一条
    const int64_t other = MustCreate(MakeRecord(kUserD, conversation_group, kGroupG, "uuid-1", "Docs", 2),
                                     Members(Member(kUserB, 0)));
    CHECK(other != first);

    CleanupShmDb();
}

TEST_CASE("SHM2 读取：记录各字段一致，取得到成员与权限，不存在的共享返回 false")
{
    ResetShmDb();

    const ShareRecord rec = MakeRecord(kUserA, conversation_group, kGroupG, "uuid-1", "Projects", 1);
    const int64_t shareId = MustCreate(rec, Members(Member(kUserC, share_perm_upload | share_perm_delete),
                                                    Member(kUserB, 0)));

    ShareRecord got;
    REQUIRE(Mgr().getShare(shareId, got));
    CHECK(got.m_shareId == shareId);
    CHECK(got.m_clientShareId == "uuid-1");
    CHECK(got.m_ownerId == kUserA);
    CHECK(got.m_enterpriseId == kEnterprise);
    CHECK(got.m_deviceId == "PC-0001");
    CHECK(got.m_ctype == conversation_group);
    CHECK(got.m_cid == kGroupG);
    CHECK(got.m_kind == share_kind_folder);
    CHECK(got.m_name == "Projects");
    CHECK(got.m_displayPath == "D:\\Projects");
    CHECK(got.m_includeHidden);
    CHECK(got.m_state == FileShareManager::kShareRecordActive);
    CHECK(got.m_cancelReason == 0);
    CHECK(got.m_createTime == rec.m_createTime);

    //成员按 userid 升序
    std::vector<ShareMemberSpec> members;
    REQUIRE(Mgr().getMembers(shareId, members));
    REQUIRE(members.size() == 2);
    CHECK(members[0].m_userId == kUserB);
    CHECK(members[0].m_perms == 0);
    CHECK(members[1].m_userId == kUserC);
    CHECK(members[1].m_perms == (share_perm_upload | share_perm_delete));

    int perms = -1;
    CHECK(Mgr().getMemberPerms(shareId, kUserC, perms));
    CHECK(perms == (share_perm_upload | share_perm_delete));
    CHECK_FALSE(Mgr().getMemberPerms(shareId, kUserD, perms));
    CHECK(perms == 0);
    //分享者本人不是成员
    CHECK_FALSE(Mgr().getMemberPerms(shareId, kUserA, perms));

    ShareRecord none;
    CHECK_FALSE(Mgr().getShare(shareId + 1000, none));

    CleanupShmDb();
}

TEST_CASE("SHM3 有效共享数：已取消的不算")
{
    ResetShmDb();

    CHECK(Mgr().countActiveShares(kUserA) == 0);
    const int64_t s1 = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "u1", "P1", 1), Members(Member(kUserB, 0)));
    MustCreate(MakeRecord(kUserA, conversation_single, kUserB, "u2", "P2", 2), Members(Member(kUserB, 0)));
    MustCreate(MakeRecord(kUserD, conversation_group, kGroupG, "u3", "P3", 3), Members(Member(kUserB, 0)));
    CHECK(Mgr().countActiveShares(kUserA) == 2);

    REQUIRE(Mgr().cancelShare(s1, share_invalid_owner_canceled, kUserA, kT0) == error_code_ok);
    CHECK(Mgr().countActiveShares(kUserA) == 1);
    CHECK(Mgr().countActiveShares(kUserD) == 1);

    CleanupShmDb();
}

TEST_CASE("SHM4 列表：我的共享与共享给我的只列有效共享，带会话时只列该会话里的")
{
    ResetShmDb();

    //A 在群 G、群 G2、与 B 的单聊里各共享一个；D 在群 G 共享一个给 B
    const int64_t aG = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "u1", "G", 1),
                                  Members(Member(kUserB, 0), Member(kUserC, 0)));
    const int64_t aG2 = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG2, "u2", "G2", 2),
                                   Members(Member(kUserC, 0)));
    const int64_t aB = MustCreate(MakeRecord(kUserA, conversation_single, kUserB, "u3", "Single", 3),
                                  Members(Member(kUserB, share_perm_upload)));
    const int64_t dG = MustCreate(MakeRecord(kUserD, conversation_group, kGroupG, "u4", "D", 4),
                                  Members(Member(kUserB, 0)));
    //A 再共享一个并取消
    const int64_t canceled = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "u5", "Old", 5),
                                        Members(Member(kUserB, 0)));
    REQUIRE(Mgr().cancelShare(canceled, share_invalid_owner_canceled, kUserA, kT0) == error_code_ok);

    std::vector<ShareRecord> list;

    //A 的全部共享：按发起时间倒序，不含已取消的
    REQUIRE(Mgr().listOwnerShares(kUserA, 0, 0, list));
    std::vector<int64_t> expected;
    expected.push_back(aB);
    expected.push_back(aG2);
    expected.push_back(aG);
    CHECK(ShareIdsOf(list) == expected);

    //A 在群 G 里的、在与 B 单聊里的
    REQUIRE(Mgr().listOwnerShares(kUserA, conversation_group, kGroupG, list));
    CHECK(ShareIdsOf(list) == Ids(aG));
    REQUIRE(Mgr().listOwnerShares(kUserA, conversation_single, kUserB, list));
    CHECK(ShareIdsOf(list) == Ids(aB));

    //共享给 B 的：A 的群 G、A 的单聊、D 的群 G
    REQUIRE(Mgr().listMemberShares(kUserB, 0, 0, list));
    expected.clear();
    expected.push_back(dG);
    expected.push_back(aB);
    expected.push_back(aG);
    CHECK(ShareIdsOf(list) == expected);

    //B 在群 G 里看到的：A 与 D 各一个
    REQUIRE(Mgr().listMemberShares(kUserB, conversation_group, kGroupG, list));
    CHECK(ShareIdsOf(list) == Ids(dG, aG));

    //B 在与 A 的单聊里看到的：会话 id 取 B 的视角，也就是 A
    REQUIRE(Mgr().listMemberShares(kUserB, conversation_single, kUserA, list));
    CHECK(ShareIdsOf(list) == Ids(aB));
    REQUIRE(Mgr().listMemberShares(kUserB, conversation_single, kUserD, list));
    CHECK(list.empty());

    //C 只在 A 的群 G 与群 G2 里
    REQUIRE(Mgr().listMemberShares(kUserC, 0, 0, list));
    CHECK(ShareIdsOf(list) == Ids(aG2, aG));

    CleanupShmDb();
}

TEST_CASE("SHM5 取消：记下原因、操作人与时间，重复取消报已取消且不改动第一次的记录")
{
    ResetShmDb();

    const int64_t shareId = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "u1", "P", 1),
                                       Members(Member(kUserB, 0)));
    const int64_t cancelAt = kT0 + 10 * kStepMs;
    REQUIRE(Mgr().cancelShare(shareId, share_invalid_admin_canceled, kUserC, cancelAt) == error_code_ok);

    ShareRecord got;
    REQUIRE(Mgr().getShare(shareId, got));
    CHECK(got.m_state == FileShareManager::kShareRecordCanceled);
    CHECK(got.m_cancelReason == share_invalid_admin_canceled);
    CHECK(got.m_canceledBy == kUserC);
    CHECK(got.m_cancelTime == cancelAt);

    //重复取消：报已取消，第一次的记录不变
    CHECK(Mgr().cancelShare(shareId, share_invalid_owner_canceled, kUserA, cancelAt + kStepMs) == error_code_share_canceled);
    REQUIRE(Mgr().getShare(shareId, got));
    CHECK(got.m_cancelReason == share_invalid_admin_canceled);
    CHECK(got.m_canceledBy == kUserC);
    CHECK(got.m_cancelTime == cancelAt);

    //成员保留，供已失效的卡片按「看的人」计算状态
    int perms = 0;
    CHECK(Mgr().getMemberPerms(shareId, kUserB, perms));

    CHECK(Mgr().cancelShare(shareId + 1000, share_invalid_owner_canceled, kUserA, cancelAt) == error_code_share_not_exist);

    CleanupShmDb();
}

TEST_CASE("SHM6 调整成员：追加、移除、改权限一起完成，只汇报实际发生的变化")
{
    ResetShmDb();

    const int64_t shareId = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "u1", "P", 1),
                                       Members(Member(kUserB, 0), Member(kUserC, share_perm_upload)));

    ShareUpdateMembersRequest req;
    req.m_shareId = shareId;
    //追加 D（新人）、追加 B（已在，权限从 0 变为上传：按改权限处理）
    req.m_add.push_back(Member(kUserD, share_perm_mkdir));
    req.m_add.push_back(Member(kUserB, share_perm_upload));
    //移除 C、移除一个不在的人
    req.m_remove.push_back(kUserC);
    req.m_remove.push_back(kUserA);
    //修改一个不在的人的权限：忽略
    req.m_update.push_back(Member(100099, share_perm_delete));

    ShareMemberChanges changes;
    const int64_t nowMs = kT0 + 20 * kStepMs;
    REQUIRE(Mgr().updateMembers(req, nowMs, changes) == error_code_ok);
    REQUIRE(changes.m_added.size() == 1);
    CHECK(changes.m_added[0].m_userId == kUserD);
    CHECK(changes.m_added[0].m_perms == share_perm_mkdir);
    CHECK(changes.m_removed == Ids(kUserC));
    REQUIRE(changes.m_updated.size() == 1);
    CHECK(changes.m_updated[0].m_userId == kUserB);
    CHECK(changes.m_updated[0].m_perms == share_perm_upload);

    std::vector<ShareMemberSpec> members;
    REQUIRE(Mgr().getMembers(shareId, members));
    REQUIRE(members.size() == 2);
    CHECK(members[0].m_userId == kUserB);
    CHECK(members[0].m_perms == share_perm_upload);
    CHECK(members[1].m_userId == kUserD);
    CHECK(members[1].m_perms == share_perm_mkdir);

    //权限没有变化的修改、同时出现在追加与移除里的人（以移除为准）
    ShareUpdateMembersRequest req2;
    req2.m_shareId = shareId;
    req2.m_update.push_back(Member(kUserB, share_perm_upload));
    req2.m_add.push_back(Member(kUserD, share_perm_mkdir));
    req2.m_remove.push_back(kUserD);
    ShareMemberChanges changes2;
    REQUIRE(Mgr().updateMembers(req2, nowMs, changes2) == error_code_ok);
    CHECK(changes2.m_added.empty());
    CHECK(changes2.m_updated.empty());
    CHECK(changes2.m_removed == Ids(kUserD));
    int perms = 0;
    CHECK_FALSE(Mgr().getMemberPerms(shareId, kUserD, perms));

    CleanupShmDb();
}

TEST_CASE("SHM7 审计：写入后读回字段一致")
{
    ResetShmDb();

    ShareAuditRecord rec;
    rec.m_shareId = 42;
    rec.m_ownerId = kUserA;
    rec.m_actorId = kUserB;
    rec.m_enterpriseId = kEnterprise;
    rec.m_action = share_audit_rename;
    rec.m_source = share_audit_source_owner_client;
    rec.m_path = "docs/旧名字.txt";
    rec.m_toPath = "新名字.txt";
    rec.m_size = 1234567890123LL;
    rec.m_result = error_code_share_name_conflict;
    rec.m_accessId = "share-1-1700000000000";
    rec.m_createTime = kT0;
    REQUIRE(Mgr().addAudit(rec));

    ShareAuditRecord second = rec;
    second.m_action = share_audit_open;
    second.m_source = share_audit_source_server;
    second.m_path = "";
    second.m_toPath = "";
    second.m_size = 0;
    second.m_result = 0;
    REQUIRE(Mgr().addAudit(second));

    std::vector<ShareAuditRecord> got;
    REQUIRE(Mgr().getAudits(42, got));
    REQUIRE(got.size() == 2);
    CHECK(got[0].m_id > 0);
    CHECK(got[0].m_shareId == 42);
    CHECK(got[0].m_ownerId == kUserA);
    CHECK(got[0].m_actorId == kUserB);
    CHECK(got[0].m_enterpriseId == kEnterprise);
    CHECK(got[0].m_action == share_audit_rename);
    CHECK(got[0].m_source == share_audit_source_owner_client);
    CHECK(got[0].m_path == "docs/旧名字.txt");
    CHECK(got[0].m_toPath == "新名字.txt");
    CHECK(got[0].m_size == 1234567890123LL);
    CHECK(got[0].m_result == error_code_share_name_conflict);
    CHECK(got[0].m_accessId == "share-1-1700000000000");
    CHECK(got[0].m_createTime == kT0);
    CHECK(got[1].m_action == share_audit_open);
    CHECK(got[1].m_id > got[0].m_id);

    std::vector<ShareAuditRecord> none;
    REQUIRE(Mgr().getAudits(43, none));
    CHECK(none.empty());

    CleanupShmDb();
}

TEST_CASE("SHM8 审计批量写入：一个事务写完一批，读回条数与内容一致，空批直接成功")
{
    ResetShmDb();

    //空批：不开连接，直接成功
    CHECK(Mgr().addAudits(std::vector<ShareAuditRecord>()));

    //逐条提交时每条约十几毫秒（第 18 步联调实测 200 条约 3 秒），1000 条要十几秒；一个事务里应在一两秒之内。
    //上限放得很宽，只为发现退回逐条提交
    const size_t kBatchCount = 1000;
    const double kBatchLimitSeconds = 8.0;
    std::vector<ShareAuditRecord> batch;
    for (size_t i = 0; i < kBatchCount; ++i)
    {
        ShareAuditRecord rec;
        rec.m_shareId = 43;
        rec.m_ownerId = kUserA;
        rec.m_actorId = kUserB;
        rec.m_enterpriseId = kEnterprise;
        rec.m_action = share_audit_download_file;
        rec.m_source = share_audit_source_owner_client;
        rec.m_path = "资料/第" + std::to_string(i) + "个文件's.txt";
        rec.m_size = static_cast<int64_t>(i);
        rec.m_accessId = "share-1-1700000000000";
        rec.m_createTime = kT0;
        batch.push_back(rec);
    }
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    REQUIRE(Mgr().addAudits(batch));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("SHM8 addAudits of " << kBatchCount << " records took " << seconds << " s");
    CHECK(seconds < kBatchLimitSeconds);

    std::vector<ShareAuditRecord> got;
    REQUIRE(Mgr().getAudits(43, got));
    REQUIRE(got.size() == kBatchCount);
    CHECK(got.front().m_path == "资料/第0个文件's.txt");
    CHECK(got.back().m_path == "资料/第999个文件's.txt");
    CHECK(got.back().m_size == 999);
    CHECK(got.back().m_action == share_audit_download_file);

    CleanupShmDb();
}

namespace {

//SHM9~11：另一个企业、群 G 的名字、管理员
const int64_t kOtherEnterprise = 8;
const int64_t kAdminId = 9001;

//在测试库里执行一条 SQL
void ShmExec(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

//插入一名用户：t_user 上没有默认值的 NOT NULL 列都要给
void ShmAddUser(int64_t userId, const char* account, const char* nickname)
{
    std::ostringstream os;
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    os << "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time) VALUES(" << userId << ", '" << account
       << "', '" << nickname << "', '', '2026-10-03 10:00:00')";
    ShmExec(os.str());
}

//四名用户、群 G、一名管理员，以及几个共享：A 在群 G 的两个（一个已被管理员取消）、D 的一个单聊共享（另一个企业）
void ShmSeedAdminData(int64_t& groupShare, int64_t& canceledShare, int64_t& otherShare)
{
    ResetShmDb();
    ShmAddUser(kUserA, "zhangsan", "张三");
    ShmAddUser(kUserB, "lisi", "李四");
    ShmAddUser(kUserC, "wangwu", "王五");
    ShmAddUser(kUserD, "zhaoliu", "赵六");
    std::ostringstream group;
    group << "INSERT INTO t_group(f_groupid, f_name, f_ownerid) VALUES(" << kGroupG << ", '项目组', " << kUserA << ")";
    ShmExec(group.str());
    std::ostringstream admin;
    admin << "INSERT INTO t_admin(f_admin_id, f_email, f_password, f_salt, f_name, f_enterprise_id, f_status, f_create_time)"
          << " VALUES(" << kAdminId << ", 'admin@x.com', 'p', 's', '管理员甲', 0, 0, '2026-10-03 10:00:00')";
    ShmExec(admin.str());

    std::vector<ShareMemberSpec> members;
    members.push_back(Member(kUserB, share_perm_upload));
    members.push_back(Member(kUserC, 0));
    groupShare = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "c-1", "Projects", 1), members);
    canceledShare = MustCreate(MakeRecord(kUserA, conversation_group, kGroupG, "c-2", "Old", 2),
                               std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    REQUIRE(Mgr().cancelShare(canceledShare, share_invalid_admin_canceled, kAdminId, kT0 + 3 * kStepMs) == error_code_ok);
    ShareRecord other = MakeRecord(kUserD, conversation_single, kUserC, "c-3", "Photos", 4);
    other.m_enterpriseId = kOtherEnterprise;
    otherShare = MustCreate(other, std::vector<ShareMemberSpec>(1, Member(kUserC, 0)));
}

//一条审计记录
ShareAuditRecord ShmAudit(int64_t shareId, int64_t owner, int64_t actor, int64_t enterprise, int action, int index)
{
    ShareAuditRecord rec;
    rec.m_shareId = shareId;
    rec.m_ownerId = owner;
    rec.m_actorId = actor;
    rec.m_enterpriseId = enterprise;
    rec.m_action = action;
    rec.m_source = share_audit_source_server;
    rec.m_createTime = kT0 + index * kStepMs;
    return rec;
}

} // namespace

TEST_CASE("SHM9 后台查共享：过滤、分页、名字与成员数")
{
    int64_t groupShare = 0;
    int64_t canceledShare = 0;
    int64_t otherShare = 0;
    ShmSeedAdminData(groupShare, canceledShare, otherShare);

    //平台管理员：全部三个，新的在前
    ShareAdminQuery all;
    all.m_limit = 10;
    std::vector<ShareAdminView> got;
    int64_t total = 0;
    REQUIRE(Mgr().queryAdminShares(all, got, total));
    CHECK(total == 3);
    REQUIRE(got.size() == 3);
    CHECK(got[0].m_share.m_shareId == otherShare);
    CHECK(got[2].m_share.m_shareId == groupShare);
    CHECK(got[2].m_ownerName == "张三");
    CHECK(got[2].m_ownerAccount == "zhangsan");
    CHECK(got[2].m_conversationName == "项目组");
    CHECK(got[2].m_memberCount == 2);
    //单聊：会话名为对方昵称
    CHECK(got[0].m_conversationName == "王五");

    //企业管理员：只看本企业；按状态过滤
    ShareAdminQuery mine = all;
    mine.m_enterpriseId = kEnterprise;
    REQUIRE(Mgr().queryAdminShares(mine, got, total));
    CHECK(total == 2);
    mine.m_state = FileShareManager::kShareRecordActive;
    REQUIRE(Mgr().queryAdminShares(mine, got, total));
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_share.m_shareId == groupShare);
    mine.m_state = FileShareManager::kShareRecordCanceled;
    REQUIRE(Mgr().queryAdminShares(mine, got, total));
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_share.m_cancelReason == share_invalid_admin_canceled);

    //分享者关键字：账号或昵称包含即可
    ShareAdminQuery kw = all;
    kw.m_keyword = "赵";
    REQUIRE(Mgr().queryAdminShares(kw, got, total));
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_share.m_shareId == otherShare);
    kw.m_keyword = "zhang";
    REQUIRE(Mgr().queryAdminShares(kw, got, total));
    CHECK(total == 2);

    //分页：每页两条，第二页只剩一条，总数不变
    ShareAdminQuery page = all;
    page.m_limit = 2;
    page.m_offset = 2;
    REQUIRE(Mgr().queryAdminShares(page, got, total));
    CHECK(total == 3);
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_share.m_shareId == groupShare);

    //非法条件
    ShareAdminQuery bad;
    CHECK_FALSE(Mgr().queryAdminShares(bad, got, total));
    CleanupShmDb();
}

TEST_CASE("SHM10 后台读成员与权限")
{
    int64_t groupShare = 0;
    int64_t canceledShare = 0;
    int64_t otherShare = 0;
    ShmSeedAdminData(groupShare, canceledShare, otherShare);

    std::vector<ShareMemberView> members;
    REQUIRE(Mgr().getMemberViews(groupShare, members));
    REQUIRE(members.size() == 2);
    CHECK(members[0].m_userId == kUserB);
    CHECK(members[0].m_perms == share_perm_upload);
    CHECK(members[0].m_name == "李四");
    CHECK(members[0].m_account == "lisi");
    CHECK(members[1].m_userId == kUserC);
    CHECK(members[1].m_perms == 0);
    REQUIRE(Mgr().getMemberViews(otherShare + 100, members));
    CHECK(members.empty());
    CleanupShmDb();
}

TEST_CASE("SHM11 后台查审计：过滤、倒序分页与操作人名字")
{
    int64_t groupShare = 0;
    int64_t canceledShare = 0;
    int64_t otherShare = 0;
    ShmSeedAdminData(groupShare, canceledShare, otherShare);

    std::vector<ShareAuditRecord> recs;
    recs.push_back(ShmAudit(groupShare, kUserA, kUserA, kEnterprise, share_audit_create_share, 1));
    recs.push_back(ShmAudit(groupShare, kUserA, kUserB, kEnterprise, share_audit_open, 2));
    recs.push_back(ShmAudit(groupShare, kUserA, kUserB, kEnterprise, share_audit_request_download, 3));
    recs.push_back(ShmAudit(canceledShare, kUserA, kAdminId, kEnterprise, share_audit_cancel_share, 4));
    recs.push_back(ShmAudit(otherShare, kUserD, kUserC, kOtherEnterprise, share_audit_open, 5));
    REQUIRE(Mgr().addAudits(recs));

    //平台管理员：全部五条，新的在前；后台强制取消那一条的操作人是管理员
    ShareAuditQuery all;
    all.m_limit = 10;
    std::vector<ShareAuditView> got;
    int64_t total = 0;
    REQUIRE(Mgr().queryAdminAudits(all, got, total));
    CHECK(total == 5);
    REQUIRE(got.size() == 5);
    CHECK(got[0].m_record.m_shareId == otherShare);
    CHECK(got[0].m_actorName == "王五");
    CHECK(got[0].m_ownerName == "赵六");
    CHECK(got[0].m_shareName == "Photos");
    CHECK(got[1].m_record.m_action == share_audit_cancel_share);
    CHECK(got[1].m_actorIsAdmin);
    CHECK(got[1].m_actorName == "管理员甲");
    CHECK_FALSE(got[2].m_actorIsAdmin);
    CHECK(got[2].m_actorName == "李四");

    //企业、动作、共享、时间范围
    ShareAuditQuery q = all;
    q.m_enterpriseId = kEnterprise;
    REQUIRE(Mgr().queryAdminAudits(q, got, total));
    CHECK(total == 4);
    q.m_action = share_audit_open;
    REQUIRE(Mgr().queryAdminAudits(q, got, total));
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_record.m_actorId == kUserB);
    ShareAuditQuery byShare = all;
    byShare.m_shareId = groupShare;
    REQUIRE(Mgr().queryAdminAudits(byShare, got, total));
    CHECK(total == 3);
    ShareAuditQuery byTime = all;
    byTime.m_startMs = kT0 + 2 * kStepMs;
    byTime.m_endMs = kT0 + 4 * kStepMs;
    REQUIRE(Mgr().queryAdminAudits(byTime, got, total));
    CHECK(total == 3);

    //操作人关键字：用户按账号或昵称，管理员按姓名
    ShareAuditQuery kw = all;
    kw.m_actorKeyword = "lisi";
    REQUIRE(Mgr().queryAdminAudits(kw, got, total));
    CHECK(total == 2);
    kw.m_actorKeyword = "管理员";
    REQUIRE(Mgr().queryAdminAudits(kw, got, total));
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_actorIsAdmin);

    //分页
    ShareAuditQuery page = all;
    page.m_limit = 2;
    page.m_offset = 4;
    REQUIRE(Mgr().queryAdminAudits(page, got, total));
    CHECK(total == 5);
    REQUIRE(got.size() == 1);
    CHECK(got[0].m_record.m_action == share_audit_create_share);
    CleanupShmDb();
}
