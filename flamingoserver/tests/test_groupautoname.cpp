/**
 *  自动群名单元测试（Suite GN / GM），test_groupautoname.cpp
 *
 *  背景：建群时没填名字，客户端曾把所选成员的昵称拼成群名、当作普通群名存进服务端，
 *  之后成员增减群名也不变 —— 于是出现"群名列着四个人、群里只剩群主一个"的情况。
 *  现在由服务端区分"用户设定的群名"与"自动群名"（t_group.f_name_custom），自动群名
 *  在成员增减、转让群主、成员改昵称后重算。
 *
 *    GN 组：生成规则本身（纯函数 BuildAutoGroupName）。
 *    GM 组：GroupManager 在临时 SQLite 库上的建群、重算、改名行为。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "doctest.h"
#include "../chatserversrc/GroupAutoName.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"
#include "test_dbbackend.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace chatserver;

namespace {

// 群主 userid。
const int64_t kOwner = 1001;

// 造一名成员。
GroupNameMember MakeMember(int64_t userid, const std::string& nickname, int64_t jointime)
{
    GroupNameMember m;
    m.userid   = userid;
    m.nickname = nickname;
    m.jointime = jointime;
    return m;
}

// 数一段 UTF-8 文本里的字符数（只数首字节，跳过 10xxxxxx 形式的后续字节）。
int CountUtf8Chars(const std::string& s)
{
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
        {
            ++n;
        }
    }
    return n;
}

} // anonymous namespace

// =====================================================================
// GN 组：生成规则
// =====================================================================

TEST_CASE("GN1 不含群主，按入群先后用顿号连接")
{
    std::vector<GroupNameMember> ms;
    ms.push_back(MakeMember(kOwner, u8"张勇", 100));
    ms.push_back(MakeMember(1003, u8"江怡", 300));
    ms.push_back(MakeMember(1002, u8"莫梅", 200));
    CHECK(BuildAutoGroupName(kOwner, ms) == std::string(u8"莫梅、江怡"));

    //入群时间相同（同一次建群拉进来的）按 userid 升序，结果与传入顺序无关
    std::vector<GroupNameMember> same;
    same.push_back(MakeMember(1005, u8"丙", 500));
    same.push_back(MakeMember(1004, u8"乙", 500));
    same.push_back(MakeMember(kOwner, u8"甲", 500));
    CHECK(BuildAutoGroupName(kOwner, same) == std::string(u8"乙、丙"));
}

TEST_CASE("GN2 超过 4 人时取前 4 个并在末尾加等")
{
    std::vector<GroupNameMember> ms;
    ms.push_back(MakeMember(kOwner, u8"群主", 100));
    ms.push_back(MakeMember(1002, u8"一", 200));
    ms.push_back(MakeMember(1003, u8"二", 300));
    ms.push_back(MakeMember(1004, u8"三", 400));
    ms.push_back(MakeMember(1005, u8"四", 500));
    //恰好 4 人：不加"等"
    CHECK(BuildAutoGroupName(kOwner, ms) == std::string(u8"一、二、三、四"));

    ms.push_back(MakeMember(1006, u8"五", 600));
    CHECK(BuildAutoGroupName(kOwner, ms) == std::string(u8"一、二、三、四等"));
}

TEST_CASE("GN3 除群主外没有别人时为「群聊」")
{
    std::vector<GroupNameMember> onlyOwner;
    onlyOwner.push_back(MakeMember(kOwner, u8"张勇", 100));
    CHECK(BuildAutoGroupName(kOwner, onlyOwner) == std::string(u8"群聊"));

    //成员列表为空（异常数据）同样兜底
    std::vector<GroupNameMember> empty;
    CHECK(BuildAutoGroupName(kOwner, empty) == std::string(u8"群聊"));
}

TEST_CASE("GN4 超长时按字符截断到 64 个字，不截出半个汉字")
{
    //每人 20 个汉字，4 人加 3 个顿号共 83 个字，超出上限
    const std::string longNick(u8"一二三四五六七八九十一二三四五六七八九十");
    REQUIRE(CountUtf8Chars(longNick) == 20);

    std::vector<GroupNameMember> ms;
    ms.push_back(MakeMember(kOwner, u8"群主", 100));
    for (int i = 0; i < 4; ++i)
    {
        ms.push_back(MakeMember(1002 + i, longNick, 200 + i));
    }
    const std::string name = BuildAutoGroupName(kOwner, ms);
    CHECK(CountUtf8Chars(name) == 64);
    //全是三字节字符（汉字与顿号），截得正好的话字节数是字符数的 3 倍
    CHECK(name.size() == 64u * 3u);
    //截断后仍是完整结果的前缀
    const std::string full = longNick + u8"、" + longNick + u8"、" + longNick + u8"、" + longNick;
    CHECK(full.compare(0, name.size(), name) == 0);
}

TEST_CASE("GN5 昵称为空的成员跳过，也不计入人数")
{
    std::vector<GroupNameMember> ms;
    ms.push_back(MakeMember(kOwner, u8"群主", 100));
    ms.push_back(MakeMember(1002, "", 200));
    ms.push_back(MakeMember(1003, u8"一", 300));
    ms.push_back(MakeMember(1004, u8"二", 400));
    ms.push_back(MakeMember(1005, u8"三", 500));
    ms.push_back(MakeMember(1006, u8"四", 600));
    //有昵称的恰好 4 人：不加"等"
    CHECK(BuildAutoGroupName(kOwner, ms) == std::string(u8"一、二、三、四"));

    //只有一个无昵称的成员：按"除群主外没有别人"处理
    std::vector<GroupNameMember> noName;
    noName.push_back(MakeMember(kOwner, u8"群主", 100));
    noName.push_back(MakeMember(1002, "", 200));
    CHECK(BuildAutoGroupName(kOwner, noName) == std::string(u8"群聊"));
}

// =====================================================================
// GM 组：GroupManager 在临时库上的建群、重算、改名
// =====================================================================
//
// 这些用例真的打开一个临时库跑 SQL（默认 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL，
// 见 test_dbbackend.h）。GroupManager 是单例、用例之间共享，唯一能隔离的就是底下这个
// 库，故每个用例开头都重建一次库并重新 seed 群ID分配器。

namespace {

// 本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）。
const char* const kGmSqliteFile = "flamingo_sqlite_groupautoname_test.db";
const char* const kGmMysqlDb    = "flamingo_test_groupautoname";

// GM9 自己手工造老库用的 SQLite 文件，与上面的分开。
const char* const kGmOldSqliteFile = "flamingo_sqlite_groupautoname_old_test.db";

// GM9 老库里那个群的群ID（位于群ID空间内即可，具体值无含义）。
const int64_t kGmOldGroupId = 4000000001LL;

// 本套用例统一的基准时间戳（Unix 毫秒）；需要区分入群先后时在其上加偏移。
const int64_t kGmTime = 1700000000000LL;

// 除群主 kOwner（甲）外的几名测试用户。
const int64_t kUserYi   = 1002;   // 乙
const int64_t kUserBing = 1003;   // 丙
const int64_t kUserDing = 1004;   // 丁

// 执行一条 SQL，失败时中止用例。
void execSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

// 插入一名用户；t_user 上没有默认值的 NOT NULL 列都要给。
void addUser(int64_t userid, const std::string& nickname)
{
    std::ostringstream os;
    os << "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) VALUES("
       << userid << ", 'u" << userid << "', '" << nickname << "', '2026-09-30 10:00:00')";
    execSql(os.str());
}

// 重建测试库、写入四名用户，并让 GroupManager 按新库 seed 群ID。
GroupManager& resetGroupNameDb()
{
    testdb::resetDb(kGmSqliteFile, kGmMysqlDb);
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

// 读回一个群，读不到时中止用例。
GroupRecord LoadGroup(GroupManager& mgr, int64_t groupid)
{
    GroupRecord g;
    REQUIRE(mgr.getGroup(groupid, g));
    return g;
}

// 构造一个只改群名的 cmd_update_group_info 请求。
UpdateGroupInfoRequest RenameRequest(int64_t groupid, const std::string& name)
{
    UpdateGroupInfoRequest req;
    req.groupid  = groupid;
    req.has_name = true;
    req.name     = name;
    return req;
}

} // anonymous namespace

TEST_CASE("GM1 建群时群名为空，得到自动群名")
{
    GroupManager& mgr = resetGroupNameDb();

    const int64_t gid = mgr.createGroup("", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    REQUIRE(gid != 0);

    const GroupRecord g = LoadGroup(mgr, gid);
    CHECK(g.name == std::string(u8"乙、丙"));
    CHECK(g.namecustom == 0);
    CHECK(g.membercount == 3);
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM2 建群时给了群名，按给定的名字建群并标记为自定义")
{
    GroupManager& mgr = resetGroupNameDb();

    const int64_t gid = mgr.createGroup(u8"项目组", static_cast<int32_t>(kOwner), Ids(kUserYi), kGmTime);
    REQUIRE(gid != 0);

    const GroupRecord g = LoadGroup(mgr, gid);
    CHECK(g.name == std::string(u8"项目组"));
    CHECK(g.namecustom == 1);
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM3 自动命名的群在加人、踢人、退群后重算，并告知调用方群名变了")
{
    GroupManager& mgr = resetGroupNameDb();
    const int64_t gid = mgr.createGroup("", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    REQUIRE(gid != 0);

    //加人：新成员入群时间最晚，排在最后
    REQUIRE(mgr.addMembers(gid, Ids(kUserDing), kGmTime + 1));
    std::string newName;
    CHECK(mgr.refreshAutoGroupName(gid, newName));
    CHECK(newName == std::string(u8"乙、丙、丁"));
    CHECK(LoadGroup(mgr, gid).name == newName);

    //成员没变时再算一次：结果相同，不写库，也不告知调用方
    std::string unchanged;
    CHECK_FALSE(mgr.refreshAutoGroupName(gid, unchanged));
    CHECK(unchanged.empty());

    //踢人与退群在存储层都是 removeMembers
    REQUIRE(mgr.removeMembers(gid, Ids(kUserYi)));
    CHECK(mgr.refreshAutoGroupName(gid, newName));
    CHECK(newName == std::string(u8"丙、丁"));

    //其余成员都走了，只剩群主
    REQUIRE(mgr.removeMembers(gid, Ids(kUserBing, kUserDing)));
    CHECK(mgr.refreshAutoGroupName(gid, newName));
    CHECK(newName == std::string(u8"群聊"));
    CHECK(LoadGroup(mgr, gid).name == std::string(u8"群聊"));
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM4 自定义群名的群，成员变化后不改名")
{
    GroupManager& mgr = resetGroupNameDb();
    const int64_t gid = mgr.createGroup(u8"项目组", static_cast<int32_t>(kOwner), Ids(kUserYi), kGmTime);
    REQUIRE(gid != 0);

    REQUIRE(mgr.addMembers(gid, Ids(kUserBing), kGmTime + 1));
    std::string newName;
    CHECK_FALSE(mgr.refreshAutoGroupName(gid, newName));

    REQUIRE(mgr.removeMembers(gid, Ids(kUserYi, kUserBing)));
    CHECK_FALSE(mgr.refreshAutoGroupName(gid, newName));

    const GroupRecord g = LoadGroup(mgr, gid);
    CHECK(g.name == std::string(u8"项目组"));
    CHECK(g.namecustom == 1);
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM5 自动命名的群改名为非空后变为自定义，此后不再重算")
{
    GroupManager& mgr = resetGroupNameDb();
    const int64_t gid = mgr.createGroup("", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    REQUIRE(gid != 0);

    REQUIRE(mgr.updateGroupInfo(RenameRequest(gid, u8"新名字")));
    const GroupRecord g = LoadGroup(mgr, gid);
    CHECK(g.name == std::string(u8"新名字"));
    CHECK(g.namecustom == 1);

    REQUIRE(mgr.addMembers(gid, Ids(kUserDing), kGmTime + 1));
    std::string newName;
    CHECK_FALSE(mgr.refreshAutoGroupName(gid, newName));
    CHECK(LoadGroup(mgr, gid).name == std::string(u8"新名字"));
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM6 改名为空串时恢复为自动群名并立即重算")
{
    GroupManager& mgr = resetGroupNameDb();
    const int64_t gid = mgr.createGroup(u8"项目组", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    REQUIRE(gid != 0);

    REQUIRE(mgr.updateGroupInfo(RenameRequest(gid, "")));
    const GroupRecord g = LoadGroup(mgr, gid);
    CHECK(g.name == std::string(u8"乙、丙"));
    CHECK(g.namecustom == 0);

    //恢复之后照常随成员重算
    REQUIRE(mgr.addMembers(gid, Ids(kUserDing), kGmTime + 1));
    std::string newName;
    CHECK(mgr.refreshAutoGroupName(gid, newName));
    CHECK(newName == std::string(u8"乙、丙、丁"));
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM7 转让群主后重算：新群主移出群名，原群主进入群名")
{
    GroupManager& mgr = resetGroupNameDb();
    const int64_t gid = mgr.createGroup("", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    REQUIRE(gid != 0);

    REQUIRE(mgr.transferOwner(gid, static_cast<int32_t>(kOwner), static_cast<int32_t>(kUserYi)));
    std::string newName;
    CHECK(mgr.refreshAutoGroupName(gid, newName));
    //三人入群时间相同，按 userid 升序：甲(1001)、丙(1003)
    CHECK(newName == std::string(u8"甲、丙"));
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM8 成员改昵称后，他所在的自动命名群重算，自定义群不变")
{
    GroupManager& mgr = resetGroupNameDb();
    //g1：自动群名，丙是普通成员，应当重算
    const int64_t g1 = mgr.createGroup("", static_cast<int32_t>(kOwner), Ids(kUserYi, kUserBing), kGmTime);
    //g2：自定义群名，丙是普通成员，群名不变
    const int64_t g2 = mgr.createGroup(u8"项目组", static_cast<int32_t>(kOwner), Ids(kUserBing), kGmTime);
    //g3：自动群名，丙是群主；群主的昵称不在群名里，重算结果不变
    const int64_t g3 = mgr.createGroup("", static_cast<int32_t>(kUserBing), Ids(kUserYi), kGmTime);
    REQUIRE(g1 != 0);
    REQUIRE(g2 != 0);
    REQUIRE(g3 != 0);

    std::ostringstream os;
    os << "UPDATE t_user SET f_nickname = '" << u8"丙丙" << "' WHERE f_user_id = " << kUserBing;
    execSql(os.str());

    std::vector<std::pair<int64_t, std::string> > changes;
    REQUIRE(mgr.refreshAutoGroupNamesOfMember(static_cast<int32_t>(kUserBing), changes));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].first == g1);
    CHECK(changes[0].second == std::string(u8"乙、丙丙"));

    CHECK(LoadGroup(mgr, g1).name == std::string(u8"乙、丙丙"));
    CHECK(LoadGroup(mgr, g2).name == std::string(u8"项目组"));
    CHECK(LoadGroup(mgr, g3).name == std::string(u8"乙"));

    //不在任何自动命名群里的人改昵称：成功返回，没有任何群改名
    std::vector<std::pair<int64_t, std::string> > none;
    CHECK(mgr.refreshAutoGroupNamesOfMember(static_cast<int32_t>(kUserDing), none));
    CHECK(none.empty());
    testdb::cleanupDb(kGmSqliteFile);
}

TEST_CASE("GM9 老库升级后，已有群的群名标记默认为自定义")
{
    //要手工造一个缺列的老库，只有 SQLite 模式能这么做；MySQL 的补列另有 m_mapField 清单
    if (testdb::useMysql())
    {
        MESSAGE("GM9 only runs on SQLite");
        return;
    }

    testdb::cleanupDb(kGmOldSqliteFile);
    {
        //建表语句只含三列，没有这次新增的群名标记列，并写入一个老群
        CDatabaseSqlite db;
        REQUIRE(db.initialize(kGmOldSqliteFile));
        REQUIRE(db.execute("CREATE TABLE t_group(f_groupid INTEGER NOT NULL, "
                           "f_name VARCHAR(64) NOT NULL, f_ownerid INTEGER NOT NULL, "
                           "PRIMARY KEY (f_groupid))"));
        std::ostringstream os;
        os << "INSERT INTO t_group(f_groupid, f_name, f_ownerid) VALUES(" << kGmOldGroupId
           << ", '" << u8"莫梅、黄涛波" << "', " << kOwner << ")";
        REQUIRE(db.execute(os.str().c_str()));
    }

    //CSqliteManager::init 会补上缺失的列
    REQUIRE(Singleton<CSqliteManager>::Instance().init(kGmOldSqliteFile));
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL, NULL, kGmOldSqliteFile);

    GroupManager& mgr = Singleton<GroupManager>::Instance();
    const GroupRecord g = LoadGroup(mgr, kGmOldGroupId);
    CHECK(g.namecustom == 1);
    CHECK(g.name == std::string(u8"莫梅、黄涛波"));

    //按自定义处理，重算不会改动它
    std::string newName;
    CHECK_FALSE(mgr.refreshAutoGroupName(kGmOldGroupId, newName));
    testdb::cleanupDb(kGmOldSqliteFile);
}
