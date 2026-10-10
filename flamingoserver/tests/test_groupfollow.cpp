/**
 *  群内关注名单与提醒模式 5 的单元测试（Suite MF-S），test_groupfollow.cpp
 *
 *  提醒模式 5「只提醒 @我和关注的人」只对群聊开放；每人在每个群有一份「关注的成员」名单，
 *  存 t_group_follow，经 cmd_set_group_follow（1330）整份覆盖，改完经
 *  cmd_group_follow_changed_notify（1331）推给本人全部在线设备，登录时随 cmd_sync_conversations
 *  （1501）的顶层 groupfollows 下发。方案见 docs/消息提醒模式与消息盒子方案.md §12。
 *
 *    MF-S1  模式 5 只对群聊合法，群会话可以存取；单聊不行
 *    MF-S2  1330 解析与整份覆盖：设两人读回两人、再设一人只剩一人、设空为空；超过上限、
 *           填非本群成员、填自己都被拒，本人不在群里时返回 not_group_member
 *    MF-S3  1501 的 groupfollows 只含有名单的群；1331 包体与请求同形
 *    MF-S4  退群 / 被移出时删掉本人在该群的名单以及别人名单里的本人；解散群删掉该群全部名单；
 *           其它群的名单不受影响
 *
 *  典型用法：Tests.exe --test-case="MF-S*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupFollowManager.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/IDatabase.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例的临时库：SQLite 文件名与 MySQL 库名（会被整个 DROP 重建）
const char* const kMfDbPath  = "flamingo_sqlite_groupfollow_test.db";
const char* const kMfMysqlDb = "flamingo_test_groupfollow";

//本套用例统一的时间戳（Unix 毫秒）
const int64_t kMfTime = 1700000000000LL;

//测试用户：甲是群主，乙、丙、丁是成员，戊不在群里
const int32_t kMfJia  = 3001;
const int32_t kMfYi   = 3002;
const int32_t kMfBing = 3003;
const int32_t kMfDing = 3004;
const int32_t kMfWu   = 3005;

//解析用例与「空名单的群」用到的群 id，不对应库里的任何群
const int64_t kMfParseGroupId = 268435701LL;

//把 JSON 文本解析成 Json::Value，失败返回 false
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs) && root.isObject();
}

//执行一条 SQL，失败时中止用例
void ExecSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

//插入一名用户；t_user 上没有默认值的 NOT NULL 列都要给
void AddUser(int32_t userid)
{
    std::ostringstream os;
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    os << "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time) VALUES("
       << userid << ", 'mf" << userid << "', 'mf" << userid << "', '', '2026-10-06 10:00:00')";
    ExecSql(os.str());
}

//重建测试库，写入五名用户，再建两个群：都以甲为群主、乙丙丁为成员
//  gid：  [出参] 主要操作的群
//  other：[出参] 用来确认「不受影响」的另一个群
GroupManager& ResetMfDb(int64_t& gid, int64_t& other)
{
    testdb::resetDb(kMfDbPath, kMfMysqlDb);
    AddUser(kMfJia);
    AddUser(kMfYi);
    AddUser(kMfBing);
    AddUser(kMfDing);
    AddUser(kMfWu);

    GroupManager& mgr = Singleton<GroupManager>::Instance();
    REQUIRE(mgr.init());

    std::vector<int64_t> members;
    members.push_back(kMfYi);
    members.push_back(kMfBing);
    members.push_back(kMfDing);
    gid = mgr.createGroup(u8"关注测试组", kMfJia, members, kMfTime);
    REQUIRE(gid != 0);
    other = mgr.createGroup(u8"另一个组", kMfJia, members, kMfTime);
    REQUIRE(other != 0);
    return mgr;
}

//造名单
std::vector<int32_t> Targets(int32_t a)
{
    return std::vector<int32_t>(1, a);
}

std::vector<int32_t> Targets(int32_t a, int32_t b)
{
    std::vector<int32_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

//读回某人在某群的名单
std::vector<int32_t> GetFollows(int32_t userid, int64_t groupid)
{
    std::vector<int32_t> out;
    REQUIRE(Singleton<GroupFollowManager>::Instance().getFollows(userid, groupid, out));
    return out;
}

//校验通过后写入，并要求成功
void SetFollowsOk(int32_t userid, int64_t groupid, const std::vector<int32_t>& targets)
{
    GroupFollowManager& mgr = Singleton<GroupFollowManager>::Instance();
    REQUIRE(mgr.checkTargets(userid, groupid, targets) == error_code_ok);
    REQUIRE(mgr.setFollows(userid, groupid, targets, kMfTime));
}

} // anonymous namespace

TEST_CASE("MF-S1 模式 5 只对群聊合法，群会话可以存取，单聊不行")
{
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_mention_follow));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_mention_follow));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_mention_follow + 1));

    int64_t gid = 0;
    int64_t other = 0;
    ResetMfDb(gid, other);

    SetConversationRequest req;
    req.ctype          = conversation_group;
    req.cid            = gid;
    req.has_notifymode = true;
    req.notifymode     = notify_mode_mention_follow;
    int errorCode = error_code_ok;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(kMfYi, req, kMfTime, errorCode));
    CHECK(errorCode == error_code_ok);

    bool pinned = false;
    int mode = notify_mode_allow;
    bool deleted = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationState(kMfYi, conversation_group, gid,
                                                                       pinned, mode, deleted));
    CHECK(mode == notify_mode_mention_follow);
    testdb::cleanupDb(kMfDbPath);
}

TEST_CASE("MF-S2a 1330 请求解析：字符串或整数元素、去重保序、非法元素与缺字段整单拒绝")
{
    GroupFollowList list;
    REQUIRE(GroupProtocol::parseSetGroupFollowRequest(
        "{\"groupid\":\"268435701\",\"targets\":[\"3003\",3002,\"3003\"]}", list));
    CHECK(list.groupid == kMfParseGroupId);
    REQUIRE(list.targets.size() == 2);
    CHECK(list.targets[0] == kMfBing);
    CHECK(list.targets[1] == kMfYi);

    //空数组表示清空
    GroupFollowList empty;
    REQUIRE(GroupProtocol::parseSetGroupFollowRequest("{\"groupid\":\"268435701\",\"targets\":[]}", empty));
    CHECK(empty.targets.empty());

    //缺 targets、缺 groupid、元素非法
    GroupFollowList bad;
    CHECK_FALSE(GroupProtocol::parseSetGroupFollowRequest("{\"groupid\":\"268435701\"}", bad));
    CHECK_FALSE(GroupProtocol::parseSetGroupFollowRequest("{\"targets\":[\"3002\"]}", bad));
    CHECK_FALSE(GroupProtocol::parseSetGroupFollowRequest("{\"groupid\":\"268435701\",\"targets\":[\"abc\"]}", bad));
    CHECK_FALSE(GroupProtocol::parseSetGroupFollowRequest("{\"groupid\":\"268435701\",\"targets\":[0]}", bad));
    CHECK_FALSE(GroupProtocol::parseSetGroupFollowRequest("{\"groupid\":\"268435701\",\"targets\":\"3002\"}", bad));
}

TEST_CASE("MF-S2b 整份覆盖与校验：上限、非本群成员、自己、本人不在群里")
{
    int64_t gid = 0;
    int64_t other = 0;
    ResetMfDb(gid, other);
    GroupFollowManager& mgr = Singleton<GroupFollowManager>::Instance();

    //设两人读回两人（升序），再设一人只剩一人，设空为空
    SetFollowsOk(kMfYi, gid, Targets(kMfDing, kMfBing));
    std::vector<int32_t> got = GetFollows(kMfYi, gid);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == kMfBing);
    CHECK(got[1] == kMfDing);

    SetFollowsOk(kMfYi, gid, Targets(kMfJia));
    got = GetFollows(kMfYi, gid);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == kMfJia);

    SetFollowsOk(kMfYi, gid, std::vector<int32_t>());
    CHECK(GetFollows(kMfYi, gid).empty());

    //超过上限：条数判断在成员判断之前，填什么 id 都一样
    std::vector<int32_t> tooMany;
    for (size_t i = 0; i <= kGroupFollowMax; ++i)
        tooMany.push_back(kMfJia + static_cast<int32_t>(i));
    CHECK(mgr.checkTargets(kMfYi, gid, tooMany) == error_code_bad_request);

    //填非本群成员、填自己
    CHECK(mgr.checkTargets(kMfYi, gid, Targets(kMfWu)) == error_code_bad_request);
    CHECK(mgr.checkTargets(kMfYi, gid, Targets(kMfYi)) == error_code_bad_request);
    CHECK(mgr.checkTargets(kMfYi, gid, Targets(kMfBing, kMfYi)) == error_code_bad_request);

    //本人不在群里
    CHECK(mgr.checkTargets(kMfWu, gid, Targets(kMfYi)) == error_code_not_group_member);

    //合规的名单照常通过（只校验，不写库）
    CHECK(mgr.checkTargets(kMfYi, gid, Targets(kMfJia, kMfDing)) == error_code_ok);
    testdb::cleanupDb(kMfDbPath);
}

TEST_CASE("MF-S3 1501 的 groupfollows 只含有名单的群；1331 包体与请求同形")
{
    int64_t gid = 0;
    int64_t other = 0;
    ResetMfDb(gid, other);

    SetFollowsOk(kMfYi, gid, Targets(kMfBing, kMfDing));
    SetFollowsOk(kMfYi, other, Targets(kMfJia));
    //丙的名单不该出现在乙的结果里
    SetFollowsOk(kMfBing, gid, Targets(kMfYi));

    std::vector<GroupFollowList> all;
    REQUIRE(Singleton<GroupFollowManager>::Instance().getFollowsAll(kMfYi, all));
    REQUIRE(all.size() == 2);

    //补一个空名单的群：构造 1501 时应被跳过
    GroupFollowList emptyGroup;
    emptyGroup.groupid = kMfParseGroupId;
    all.push_back(emptyGroup);

    Json::Value sync;
    REQUIRE(ParseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", kMfTime,
                                                                      std::vector<ConversationBrief>(),
                                                                      std::vector<NotifyModeItem>(), all), sync));
    const Json::Value& follows = sync["groupfollows"];
    REQUIRE(follows.isArray());
    REQUIRE(follows.size() == 2);
    bool sawGid = false;
    bool sawOther = false;
    for (Json::ArrayIndex i = 0; i < follows.size(); ++i)
    {
        const std::string groupid = follows[i]["groupid"].asString();
        if (groupid == std::to_string(gid))
        {
            sawGid = true;
            REQUIRE(follows[i]["targets"].size() == 2);
            CHECK(follows[i]["targets"][0].asString() == std::to_string(kMfBing));
            CHECK(follows[i]["targets"][1].asString() == std::to_string(kMfDing));
        }
        else if (groupid == std::to_string(other))
        {
            sawOther = true;
            REQUIRE(follows[i]["targets"].size() == 1);
            CHECK(follows[i]["targets"][0].asString() == std::to_string(kMfJia));
        }
    }
    CHECK(sawGid);
    CHECK(sawOther);

    //老的调用方式（不传名单）仍输出空数组
    Json::Value plain;
    REQUIRE(ParseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", kMfTime,
                                                                      std::vector<ConversationBrief>()), plain));
    CHECK(plain["groupfollows"].isArray());
    CHECK(plain["groupfollows"].size() == 0);

    //1331：与请求同形，解析回来一致
    GroupFollowList saved;
    saved.groupid = gid;
    saved.targets = GetFollows(kMfYi, gid);
    GroupFollowList parsed;
    REQUIRE(GroupProtocol::parseSetGroupFollowRequest(GroupProtocol::buildGroupFollowChangedNotify(saved), parsed));
    CHECK(parsed.groupid == gid);
    CHECK(parsed.targets == saved.targets);
    testdb::cleanupDb(kMfDbPath);
}

TEST_CASE("MF-S4 退群删本人名单与别人名单里的本人，解散群删全部，其它群不受影响")
{
    int64_t gid = 0;
    int64_t other = 0;
    GroupManager& groupMgr = ResetMfDb(gid, other);

    SetFollowsOk(kMfYi, gid, Targets(kMfBing, kMfDing));
    SetFollowsOk(kMfBing, gid, Targets(kMfYi, kMfDing));
    SetFollowsOk(kMfDing, gid, Targets(kMfJia));
    SetFollowsOk(kMfYi, other, Targets(kMfBing));

    //乙退群：乙的名单没了，丙名单里的乙也没了；丁的名单与乙在另一个群的名单都还在
    REQUIRE(groupMgr.removeMembers(gid, std::vector<int64_t>(1, kMfYi)));
    CHECK(GetFollows(kMfYi, gid).empty());
    std::vector<int32_t> bing = GetFollows(kMfBing, gid);
    REQUIRE(bing.size() == 1);
    CHECK(bing[0] == kMfDing);
    CHECK(GetFollows(kMfDing, gid).size() == 1);
    CHECK(GetFollows(kMfYi, other).size() == 1);

    //解散群：该群剩下的名单全部删除，另一个群不受影响
    REQUIRE(groupMgr.dismissGroup(gid));
    CHECK(GetFollows(kMfBing, gid).empty());
    CHECK(GetFollows(kMfDing, gid).empty());
    CHECK(GetFollows(kMfYi, other).size() == 1);
    testdb::cleanupDb(kMfDbPath);
}
