/**
 *  入群必读的单元测试（Suite MR-S），test_mustread.cpp（docs/群公告接入方案.md §14）
 *
 *    MR-S1  设置与取消：群资料带 mustreadid；取消时待确认的人一并清零并记进 cleared
 *    MR-S2  标记待确认：新成员标为待确认、老成员不受影响；成员列表带 mustread
 *    MR-S3  发言判定的数据：getMemberRoleAndMuted 取出 mustread；确认后清零，重复确认不再计入
 *    MR-S4  协议：1332 的解析；1335 应答；必读卡片 content 带 mustread
 *
 *  典型用法：Tests.exe --test-case="MR-S*"
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/IDatabase.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例的临时库
const char* const kMrDbPath  = "flamingo_sqlite_mustread_test.db";
const char* const kMrMysqlDb = "flamingo_test_mustread";

//统一时间戳（Unix 毫秒）
const int64_t kMrTime = 1700000000000LL;

//测试用户：甲是群主，乙、丙是老成员，丁、戊是新成员
const int32_t kMrJia  = 4001;
const int32_t kMrYi   = 4002;
const int32_t kMrBing = 4003;
const int32_t kMrDing = 4004;
const int32_t kMrWu   = 4005;

//公告优先级：通知
const int kMrSeverity = 0;

//把 JSON 文本解析成 Json::Value
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs) && root.isObject();
}

//插入一名用户
void AddUser(int32_t userid)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    std::ostringstream os;
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    os << "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time) VALUES("
       << userid << ", 'mr" << userid << "', 'mr" << userid << "', '', '2026-10-07 10:00:00')";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//重建测试库，建一个群（甲为群主，乙丙为成员）并发一条公告
GroupManager& ResetMrDb(int64_t& gid, int64_t& annId)
{
    testdb::resetDb(kMrDbPath, kMrMysqlDb);
    const int32_t users[] = { kMrJia, kMrYi, kMrBing, kMrDing, kMrWu };
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); ++i)
        AddUser(users[i]);
    GroupManager& mgr = Singleton<GroupManager>::Instance();
    REQUIRE(mgr.init());
    std::vector<int64_t> members;
    members.push_back(kMrYi);
    members.push_back(kMrBing);
    gid = mgr.createGroup(u8"必读测试组", kMrJia, members, kMrTime);
    REQUIRE(gid != 0);
    annId = mgr.publishAnnouncement(gid, u8"入群须知", u8"请先读完群规", kMrSeverity, kMrJia, kMrTime);
    REQUIRE(annId > 0);
    return mgr;
}

//取某位成员的 mustread 标记；不是成员返回 -1
int MustReadOf(GroupManager& mgr, int64_t gid, int32_t userid)
{
    int role = 0;
    int muted = 0;
    int mustRead = 0;
    if (!mgr.getMemberRoleAndMuted(gid, userid, role, muted, NULL, &mustRead))
        return -1;
    return mustRead;
}

} // anonymous namespace

TEST_CASE("MR-S1 设置与取消入群必读：群资料带 mustreadid，取消时待确认的人一并清零")
{
    int64_t gid = 0;
    int64_t annId = 0;
    GroupManager& mgr = ResetMrDb(gid, annId);

    std::vector<int64_t> cleared;
    REQUIRE(mgr.setMustRead(gid, annId, cleared));
    CHECK(cleared.empty());
    GroupRecord g;
    REQUIRE(mgr.getGroup(gid, g));
    CHECK(g.mustreadid == annId);

    //群资料 JSON 带 mustreadid（字符串）
    Json::Value notify;
    REQUIRE(ParseJson(GroupProtocol::buildGroupInfoChangedNotify(g, group_info_change_updated), notify));
    CHECK(notify.toStyledString().find(std::to_string(annId)) != std::string::npos);

    //有两位待确认时取消：两人都清零并记进 cleared
    std::vector<int64_t> newcomers;
    newcomers.push_back(kMrDing);
    newcomers.push_back(kMrWu);
    REQUIRE(mgr.addMembers(gid, newcomers, kMrTime));
    REQUIRE(mgr.markMustReadPending(gid, newcomers));
    REQUIRE(mgr.setMustRead(gid, 0, cleared));
    CHECK(cleared.size() == 2);
    CHECK(MustReadOf(mgr, gid, kMrDing) == 0);
    CHECK(MustReadOf(mgr, gid, kMrWu) == 0);
    REQUIRE(mgr.getGroup(gid, g));
    CHECK(g.mustreadid == 0);
    testdb::cleanupDb(kMrDbPath);
}

TEST_CASE("MR-S2 新成员标为待确认，老成员不受影响，成员列表带 mustread")
{
    int64_t gid = 0;
    int64_t annId = 0;
    GroupManager& mgr = ResetMrDb(gid, annId);
    std::vector<int64_t> cleared;
    REQUIRE(mgr.setMustRead(gid, annId, cleared));

    std::vector<int64_t> newcomers(1, kMrDing);
    REQUIRE(mgr.addMembers(gid, newcomers, kMrTime));
    REQUIRE(mgr.markMustReadPending(gid, newcomers));
    CHECK(MustReadOf(mgr, gid, kMrDing) == 1);
    CHECK(MustReadOf(mgr, gid, kMrYi) == 0);
    CHECK(MustReadOf(mgr, gid, kMrJia) == 0);

    std::vector<GroupMemberRecord> members;
    REQUIRE(mgr.getGroupMembers(gid, members));
    Json::Value resp;
    REQUIRE(ParseJson(GroupProtocol::buildGroupMembersResponse(0, "ok", gid, members), resp));
    int pendingRows = 0;
    for (Json::ArrayIndex i = 0; i < resp["members"].size(); ++i)
    {
        if (resp["members"][i]["mustread"].asInt() == 1)
        {
            ++pendingRows;
            CHECK(resp["members"][i]["userid"].asString() == std::to_string(kMrDing));
        }
    }
    CHECK(pendingRows == 1);
    testdb::cleanupDb(kMrDbPath);
}

TEST_CASE("MR-S3 确认后清零；重复确认、给不是待确认的人放行都不计入")
{
    int64_t gid = 0;
    int64_t annId = 0;
    GroupManager& mgr = ResetMrDb(gid, annId);
    std::vector<int64_t> cleared;
    REQUIRE(mgr.setMustRead(gid, annId, cleared));
    std::vector<int64_t> newcomers;
    newcomers.push_back(kMrDing);
    newcomers.push_back(kMrWu);
    REQUIRE(mgr.addMembers(gid, newcomers, kMrTime));
    REQUIRE(mgr.markMustReadPending(gid, newcomers));

    //丁本人确认
    std::vector<int64_t> self(1, kMrDing);
    REQUIRE(mgr.clearMustReadPending(gid, self, cleared));
    REQUIRE(cleared.size() == 1);
    CHECK(cleared[0] == kMrDing);
    CHECK(MustReadOf(mgr, gid, kMrDing) == 0);
    //再确认一次：不再计入
    REQUIRE(mgr.clearMustReadPending(gid, self, cleared));
    CHECK(cleared.empty());

    //群主代为放行戊与老成员乙：只有戊计入
    std::vector<int64_t> release;
    release.push_back(kMrWu);
    release.push_back(kMrYi);
    REQUIRE(mgr.clearMustReadPending(gid, release, cleared));
    REQUIRE(cleared.size() == 1);
    CHECK(cleared[0] == kMrWu);
    CHECK(MustReadOf(mgr, gid, kMrWu) == 0);

    //换一条必读公告不会把已确认的人重新标为待确认
    const int64_t ann2 = mgr.publishAnnouncement(gid, u8"新须知", u8"第二版", kMrSeverity, kMrJia, kMrTime + 1);
    REQUIRE(ann2 > 0);
    REQUIRE(mgr.setMustRead(gid, ann2, cleared));
    CHECK(MustReadOf(mgr, gid, kMrDing) == 0);
    testdb::cleanupDb(kMrDbPath);
}

TEST_CASE("MR-S4 协议：1332 解析、1335 应答、必读卡片 content")
{
    int64_t groupid = 0;
    int64_t annId = 0;
    REQUIRE(GroupProtocol::parseSetMustReadRequest("{\"groupid\":\"268435999\",\"announcementid\":\"42\"}", groupid, annId));
    CHECK(groupid == 268435999LL);
    CHECK(annId == 42);
    REQUIRE(GroupProtocol::parseSetMustReadRequest("{\"groupid\":\"268435999\"}", groupid, annId));
    CHECK(annId == 0);   //缺省表示取消
    CHECK_FALSE(GroupProtocol::parseSetMustReadRequest("{\"announcementid\":\"42\"}", groupid, annId));
    CHECK_FALSE(GroupProtocol::parseSetMustReadRequest("{\"groupid\":\"268435999\",\"announcementid\":-1}", groupid, annId));

    GroupAnnouncementRecord item;
    item.id = 42;
    item.groupid = 268435999LL;
    item.title = u8"入群须知";
    item.content = u8"请先读完群规";
    item.publisher = kMrJia;
    item.publishtime = kMrTime;
    Json::Value resp;
    REQUIRE(ParseJson(GroupProtocol::buildGetMustReadResponse(0, "ok", item.groupid, item.id, true, &item), resp));
    CHECK(resp["pending"].asBool());
    CHECK(resp["mustreadid"].asString() == "42");
    CHECK(resp["announcement"]["title"].asString() == item.title);
    Json::Value none;
    REQUIRE(ParseJson(GroupProtocol::buildGetMustReadResponse(0, "ok", item.groupid, 0, false, NULL), none));
    CHECK_FALSE(none.isMember("announcement"));

    Json::Value card;
    REQUIRE(ParseJson(GroupProtocol::buildMustReadCardContent(item), card));
    CHECK(card["mustread"].asBool());
    CHECK(card["title"].asString() == item.title);
}
