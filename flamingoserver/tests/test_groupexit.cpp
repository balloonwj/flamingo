/**
 *  退群、解散群、转让群主的存储层单元测试（Suite GX），test_groupexit.cpp
 *
 *  背景：解散群原先只删 t_group 与 t_group_member 两张表，群公告、加群申请、群文件条目，
 *  以及各成员对该群的收藏行、会话设置行都留在库里；退群 / 被移出时，当事人对该群的收藏行与会话
 *  设置行也不删；转让群主的三条更新语句不在一个事务里，后两条的失败还被直接忽略。本套用例覆盖
 *  修正后的行为：
 *
 *    GX1  解散：该群在七张表里的行全部删除，群消息保留，其它群与单聊的数据不受影响
 *         （原为八张表，群敏感词表 t_group_sensitive_word 已于 2026-10-02 随群级敏感词删除）
 *    GX2  解散中途失败：整体回滚，群、成员与各附属行都保持原样
 *    GX3  退群 / 移出：删除当事人的成员行、对该群的收藏行与会话设置行，其余数据不动
 *    GX4  移出中途失败：整体回滚
 *    GX5  转让：t_group 的群主、新群主的角色、原群主的角色三处一致
 *    GX6  转让中途失败：整体回滚，三处都保持原样
 *    GX7  解散群号最大的群并重启服务后，新建的群不会复用这个群号。解散后群消息保留在 t_chatmsg
 *         里，若群号被新群复用，新群成员就能翻到旧群的聊天记录
 *    GX8  （已删除）退群系统提示的正文——2026-10-04 起主动退群不再插提示，组装函数随之删除
 *    GX9  转让群主系统提示的正文：两端昵称齐全、缺一端、两端都缺
 *
 *  "中途失败"用 SQLite 触发器模拟：在事务里最后执行的那条语句所在的表上挂一个 RAISE(ABORT) 触发器，
 *  使它必然失败，再检查前面已经执行的语句是否都被撤销。MySQL 的触发器写法不同，GX2 / GX4 / GX6
 *  只在 SQLite 模式下运行。
 *
 *  用法：Tests.exe --test-case="GX*"
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "doctest.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"  //buildOwnerTransferTip
#include "../chatserversrc/Protocol.h"       //group_role_xxx / conversation_group
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"
#include "test_dbbackend.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）。
const char* const kGxSqliteFile = "flamingo_sqlite_groupexit_test.db";
const char* const kGxMysqlDb    = "flamingo_test_groupexit";

// 本套用例统一的基准时间戳（Unix 毫秒）。
const int64_t kGxTime = 1700000000000LL;

// 测试用户：群主甲与三名成员。
const int64_t kOwner    = 2001;   // 甲，两个测试群的群主
const int64_t kUserYi   = 2002;   // 乙
const int64_t kUserBing = 2003;   // 丙
const int64_t kUserDing = 2004;   // 丁，不在群里，只用来提交加群申请

// t_group_file.f_fileid 引用的文件 ID。测试库不校验外键，取任意正数即可。
const int64_t kGxFileId = 9001;

// seedGroupRows 给一个群写入的数据量：3 名成员（甲、乙、丙），四张附属表各 1 行，
// 3 名成员各 1 行收藏与会话设置，1 条群消息。
const int64_t kSeedMembers   = 3;
const int64_t kSeedSatellite = 1;
const int64_t kSeedPersonal  = 3;
const int64_t kSeedMessages  = 1;

// 执行一条 SQL，失败时中止用例。
void execSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

// 数一张表里满足条件的行数；查询失败返回 -1。
//   table： 表名
//   where： WHERE 之后的条件（不含 WHERE 关键字）
int64_t countRows(const char* table, const std::string& where)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
    {
        return -1;
    }
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM " << table << " WHERE " << where;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        return -1;
    }
    int64_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

// 条件：f_groupid 等于给定群。
std::string byGroup(int64_t groupid)
{
    std::ostringstream os;
    os << "f_groupid = " << groupid;
    return os.str();
}

// 条件：某个用户对某个对象（群或联系人）的那一行，用于 t_favorite / t_conversation。
//   userid：行的归属用户
//   ctype： 对象类型，取 conversation_type 的值
//   cid：   对象 ID（群 ID 或对端 userid）
std::string byOwnerTarget(int64_t userid, int ctype, int64_t cid)
{
    std::ostringstream os;
    os << "f_userid = " << userid << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    return os.str();
}

// 条件：所有用户对某个群的行，用于 t_favorite / t_conversation。
std::string byGroupTarget(int64_t groupid)
{
    std::ostringstream os;
    os << "f_ctype = " << conversation_group << " AND f_cid = " << groupid;
    return os.str();
}

// 插入一名用户；t_user 上没有默认值的 NOT NULL 列都要给。
void addUser(int64_t userid, const std::string& nickname)
{
    std::ostringstream os;
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    os << "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time) VALUES("
       << userid << ", 'u" << userid << "', '" << nickname << "', '', '2026-10-01 10:00:00')";
    execSql(os.str());
}

// 重建测试库、写入四名用户，并让 GroupManager 按新库 seed 群 ID。
GroupManager& resetGroupExitDb()
{
    testdb::resetDb(kGxSqliteFile, kGxMysqlDb);
    addUser(kOwner, u8"甲");
    addUser(kUserYi, u8"乙");
    addUser(kUserBing, u8"丙");
    addUser(kUserDing, u8"丁");

    GroupManager& mgr = Singleton<GroupManager>::Instance();
    REQUIRE(mgr.init());
    return mgr;
}

// 造只有一名成员的列表。
std::vector<int64_t> Ids(int64_t a)
{
    return std::vector<int64_t>(1, a);
}

// 造有两名成员的列表。
std::vector<int64_t> Ids(int64_t a, int64_t b)
{
    std::vector<int64_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

// 给一个用户写入他对某个对象的收藏行与会话设置行。
void addFavoriteAndConversation(int64_t userid, int ctype, int64_t cid)
{
    std::ostringstream fav;
    fav << "INSERT INTO t_favorite(f_userid, f_ctype, f_cid, f_create_time) VALUES("
        << userid << ", " << ctype << ", " << cid << ", " << kGxTime << ")";
    execSql(fav.str());

    std::ostringstream conv;
    conv << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_pinned) VALUES("
         << userid << ", " << ctype << ", " << cid << ", 1)";
    execSql(conv.str());
}

// 给一个群写满各张附属表：一条公告、一条加群申请（丁提交）、一个群文件条目、
// 一条群消息，以及三名成员（甲、乙、丙）各自对该群的收藏行与会话设置行。
void seedGroupRows(int64_t groupid)
{
    std::ostringstream ann;
    ann << "INSERT INTO t_group_announcement(f_groupid, f_title, f_publisher, f_publish_time) VALUES("
        << groupid << ", 'notice', " << kOwner << ", " << kGxTime << ")";
    execSql(ann.str());

    std::ostringstream req;
    req << "INSERT INTO t_group_join_request(f_groupid, f_userid, f_create_time) VALUES("
        << groupid << ", " << kUserDing << ", " << kGxTime << ")";
    execSql(req.str());

    std::ostringstream file;
    file << "INSERT INTO t_group_file(f_groupid, f_fileid, f_uploaderid, f_uploadtime) VALUES("
         << groupid << ", " << kGxFileId << ", " << kOwner << ", " << kGxTime << ")";
    execSql(file.str());

    std::ostringstream msg;
    msg << "INSERT INTO t_chatmsg(f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_create_time) VALUES("
        << "'gx-" << groupid << "', " << conversation_group << ", " << kOwner << ", " << groupid
        << ", 1, '{}', " << kGxTime << ")";
    execSql(msg.str());

    addFavoriteAndConversation(kOwner, conversation_group, groupid);
    addFavoriteAndConversation(kUserYi, conversation_group, groupid);
    addFavoriteAndConversation(kUserBing, conversation_group, groupid);
}

// 写入与群无关的单聊数据：甲收藏了丙、乙收藏了甲，各带一行会话设置。
void seedSingleChatRows()
{
    addFavoriteAndConversation(kOwner, conversation_single, kUserBing);
    addFavoriteAndConversation(kUserYi, conversation_single, kOwner);
}

// 建两个群：都以甲为群主，成员都是乙、丙；并给两个群写满附属表、写入单聊数据。
//   gid：  [出参] 要操作的那个群
//   other：[出参] 用来确认"不受影响"的另一个群
void buildTwoGroups(GroupManager& mgr, int64_t& gid, int64_t& other)
{
    gid = mgr.createGroup(u8"项目组", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGxTime);
    REQUIRE(gid != 0);
    other = mgr.createGroup(u8"兴趣组", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGxTime);
    REQUIRE(other != 0);
    seedGroupRows(gid);
    seedGroupRows(other);
    seedSingleChatRows();
}

// 断言某个群在各张表里的行数：
//   members：成员行数；satellite：公告、加群申请、群文件每张表的行数；
//   personal：t_favorite 与 t_conversation 里指向该群的行数；messages：群消息行数。
void checkGroupRows(int64_t groupid, int64_t groupRow, int64_t members, int64_t satellite,
                    int64_t personal, int64_t messages)
{
    INFO("groupid " << groupid);
    CHECK(countRows("t_group", byGroup(groupid)) == groupRow);
    CHECK(countRows("t_group_member", byGroup(groupid)) == members);
    CHECK(countRows("t_group_announcement", byGroup(groupid)) == satellite);
    CHECK(countRows("t_group_join_request", byGroup(groupid)) == satellite);
    CHECK(countRows("t_group_file", byGroup(groupid)) == satellite);
    CHECK(countRows("t_favorite", byGroupTarget(groupid)) == personal);
    CHECK(countRows("t_conversation", byGroupTarget(groupid)) == personal);

    std::ostringstream msgWhere;
    msgWhere << "f_ctype = " << conversation_group << " AND f_to = " << groupid;
    CHECK(countRows("t_chatmsg", msgWhere.str()) == messages);
}

// 断言单聊数据完好：甲对丙、乙对甲的收藏行与会话设置行都还在。
void checkSingleChatRowsIntact()
{
    CHECK(countRows("t_favorite", byOwnerTarget(kOwner, conversation_single, kUserBing)) == 1);
    CHECK(countRows("t_conversation", byOwnerTarget(kOwner, conversation_single, kUserBing)) == 1);
    CHECK(countRows("t_favorite", byOwnerTarget(kUserYi, conversation_single, kOwner)) == 1);
    CHECK(countRows("t_conversation", byOwnerTarget(kUserYi, conversation_single, kOwner)) == 1);
}

// 读回一个群，读不到时中止用例。
GroupRecord loadGroup(GroupManager& mgr, int64_t groupid)
{
    GroupRecord g;
    REQUIRE(mgr.getGroup(groupid, g));
    return g;
}

} // anonymous namespace

TEST_CASE("GX1 解散群删除该群在各表的行，群消息保留，其它群与单聊不受影响")
{
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);
    checkGroupRows(gid, 1, kSeedMembers, kSeedSatellite, kSeedPersonal, kSeedMessages);

    REQUIRE(mgr.dismissGroup(gid));

    GroupRecord g;
    CHECK_FALSE(mgr.getGroup(gid, g));
    //群消息保留，供企业日后审计追溯
    checkGroupRows(gid, 0, 0, 0, 0, kSeedMessages);
    checkGroupRows(other, 1, kSeedMembers, kSeedSatellite, kSeedPersonal, kSeedMessages);
    checkSingleChatRowsIntact();
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX2 解散中途失败时整体回滚")
{
    if (testdb::useMysql())
    {
        MESSAGE("GX2 only runs on SQLite");
        return;
    }
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);

    //删除 t_group 行是解散的最后一步；让它失败，前面删掉的附属行与成员行都应当恢复
    execSql("CREATE TRIGGER gx_block_group_delete BEFORE DELETE ON t_group "
            "BEGIN SELECT RAISE(ABORT, 'gx test'); END");
    CHECK_FALSE(mgr.dismissGroup(gid));
    execSql("DROP TRIGGER gx_block_group_delete");

    checkGroupRows(gid, 1, kSeedMembers, kSeedSatellite, kSeedPersonal, kSeedMessages);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX3 退群与移出删除当事人的成员行及其对该群的收藏与会话设置，其余数据不动")
{
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);

    //退群：只有乙一人
    REQUIRE(mgr.removeMembers(gid, Ids(kUserYi)));
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserYi)) < 0);
    CHECK(countRows("t_favorite", byOwnerTarget(kUserYi, conversation_group, gid)) == 0);
    CHECK(countRows("t_conversation", byOwnerTarget(kUserYi, conversation_group, gid)) == 0);
    //群主与丙对该群的行还在
    CHECK(countRows("t_favorite", byGroupTarget(gid)) == kSeedPersonal - 1);
    CHECK(countRows("t_conversation", byGroupTarget(gid)) == kSeedPersonal - 1);
    //乙对另一个群、对甲的单聊数据不受影响
    CHECK(countRows("t_favorite", byOwnerTarget(kUserYi, conversation_group, other)) == 1);
    CHECK(countRows("t_conversation", byOwnerTarget(kUserYi, conversation_group, other)) == 1);
    checkSingleChatRowsIntact();

    //一次移出多人：丙，连同已经不在群里的乙（重复移出不应报错）
    REQUIRE(mgr.removeMembers(gid, Ids(kUserYi, kUserBing)));
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserBing)) < 0);
    CHECK(countRows("t_favorite", byGroupTarget(gid)) == 1);
    CHECK(countRows("t_conversation", byGroupTarget(gid)) == 1);
    CHECK(countRows("t_favorite", byOwnerTarget(kOwner, conversation_group, gid)) == 1);

    //群本身与附属表都还在，群消息也在
    checkGroupRows(gid, 1, 1, kSeedSatellite, 1, kSeedMessages);
    checkGroupRows(other, 1, kSeedMembers, kSeedSatellite, kSeedPersonal, kSeedMessages);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX4 移出中途失败时整体回滚")
{
    if (testdb::useMysql())
    {
        MESSAGE("GX4 only runs on SQLite");
        return;
    }
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);

    //删除成员行是移出的最后一步；让它失败，前面删掉的收藏行与会话设置行都应当恢复
    execSql("CREATE TRIGGER gx_block_member_delete BEFORE DELETE ON t_group_member "
            "BEGIN SELECT RAISE(ABORT, 'gx test'); END");
    CHECK_FALSE(mgr.removeMembers(gid, Ids(kUserYi)));
    execSql("DROP TRIGGER gx_block_member_delete");

    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserYi)) == group_role_member);
    CHECK(countRows("t_favorite", byOwnerTarget(kUserYi, conversation_group, gid)) == 1);
    CHECK(countRows("t_conversation", byOwnerTarget(kUserYi, conversation_group, gid)) == 1);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX5 转让群主后，群主、新群主角色、原群主角色三处一致")
{
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);

    REQUIRE(mgr.transferOwner(gid, static_cast<int32_t>(kOwner), static_cast<int32_t>(kUserYi)));

    CHECK(loadGroup(mgr, gid).ownerid == kUserYi);
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserYi)) == group_role_owner);
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kOwner)) == group_role_member);
    //丙的角色与另一个群都不受影响
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserBing)) == group_role_member);
    CHECK(loadGroup(mgr, other).ownerid == kOwner);
    CHECK(mgr.getMemberRole(other, static_cast<int32_t>(kOwner)) == group_role_owner);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX6 转让中途失败时整体回滚")
{
    if (testdb::useMysql())
    {
        MESSAGE("GX6 only runs on SQLite");
        return;
    }
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);

    //把原群主降为普通成员是转让的最后一步；让它失败，前两步（改群主、新群主升为群主）都应当撤销
    std::ostringstream trigger;
    trigger << "CREATE TRIGGER gx_block_owner_demote BEFORE UPDATE ON t_group_member "
            << "WHEN NEW.f_userid = " << kOwner << " "
            << "BEGIN SELECT RAISE(ABORT, 'gx test'); END";
    execSql(trigger.str());
    CHECK_FALSE(mgr.transferOwner(gid, static_cast<int32_t>(kOwner), static_cast<int32_t>(kUserYi)));
    execSql("DROP TRIGGER gx_block_owner_demote");

    CHECK(loadGroup(mgr, gid).ownerid == kOwner);
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kOwner)) == group_role_owner);
    CHECK(mgr.getMemberRole(gid, static_cast<int32_t>(kUserYi)) == group_role_member);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX7 解散群号最大的群并重启后，新建的群不复用这个群号")
{
    GroupManager& mgr = resetGroupExitDb();
    int64_t gid = 0;
    int64_t other = 0;
    buildTwoGroups(mgr, gid, other);
    //后建的群群号更大；解散它，群消息仍留在 t_chatmsg 里
    REQUIRE(other > gid);
    REQUIRE(mgr.dismissGroup(other));

    //模拟服务重启：群号分配器按库里的数据重新确定起点
    REQUIRE(mgr.init());
    const int64_t fresh = mgr.createGroup(u8"新群", static_cast<int32_t>(kOwner), Ids(kUserYi), kGxTime);
    REQUIRE(fresh != 0);
    CHECK(fresh > other);
    testdb::cleanupDb(kGxSqliteFile);
}

TEST_CASE("GX9 转让群主系统提示：两端昵称齐全、缺一端、两端都缺")
{
    CHECK(GroupProtocol::buildOwnerTransferTip(u8"甲", u8"乙") == std::string(u8"「甲」已将群主转让给「乙」"));
    CHECK(GroupProtocol::buildOwnerTransferTip("", u8"乙") == std::string(u8"群主已转让给「乙」"));
    CHECK(GroupProtocol::buildOwnerTransferTip(u8"甲", "") == std::string(u8"「甲」已转让群主"));
    CHECK(GroupProtocol::buildOwnerTransferTip("", "") == std::string(u8"群主已变更"));
}
