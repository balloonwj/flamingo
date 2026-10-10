/**
 *  邀请入群时「同步最近 N 条」的历史边界单元测试（Suite GH），test_grouphistory.cpp
 *
 *  背景（docs/邀请入群方案.md 第 2 期）：邀请人可以选择新成员入群后能看到多少条入群前的消息。服务端在入群那一刻
 *  按条数算出一个 msgid 下界，记进 t_group_member.f_history_from；拉历史、会话内搜索、全局搜索、Pin 列表都以它
 *  为下界。原有的群级开关「新成员不可查看入群前的历史消息」（按入群时间截断）照旧生效，两者同时套用。
 *
 *    GH1  解析 1305 的 historylimit：缺省为 -1，-1 / 0 / 100 / 10000 合法，-2、10001、非整数判为请求非法
 *    GH2  计算历史边界：全部为 0、不同步为最大 msgid + 1、最近 N 条为倒数第 N 条、不足 N 条为 0、空群为 0
 *    GH3  addMembers 带上历史边界写库，getGroupMembers / getMember / getUserMemberships 都能读回
 *    GH4  拉历史带 msgid 下界：往前翻、往后补都拿不到边界之前的消息，翻到边界时 hasmore 为假
 *    GH5  会话内搜索带 msgid 下界
 *    GH6  全局搜索的群范围带 msgid 下界
 *    GH7  Pin 列表带下界：钉在边界之前的消息不返回，total 只计看得到的
 *    GH8  时间下界与 msgid 下界同时生效，取更严格的；computeHistoryBound 对群主与管理员不设下界
 *    GH9  加群申请记下同步条数，读回一致；不传时为 -1（全部）
 *    GH10 1305 应答带 added / invited / pending 三项统计
 *
 *  GH2 ~ GH9 会打开一个临时数据库跑 SQL（默认 SQLite，设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL，见 test_dbbackend.h）。
 *
 *  用法：Tests.exe --test-case="GH*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessagePinManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/IDatabase.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时数据库文件与 MySQL 测试库名，与别的套分开，避免互相干扰
const char* const GH_DB_PATH  = "flamingo_sqlite_grouphistory_test.db";
const char* const GH_MYSQL_DB = "flamingo_test_gh";

//造消息用的基准时间与相邻两条消息的间隔（Unix 毫秒），固定取值保证结果可复现
const int64_t GH_BASE_TIME = 1700000000000LL;
const int64_t GH_STEP_MS = 1000;

//测试群、群里的老成员、新成员
const int64_t GH_GROUP_ID = 900201;
const int32_t GH_OLD_MEMBER = 400001;
const int32_t GH_NEW_MEMBER = 400002;

//另一个群：GH3 用来确认 getUserMemberships 按群分开返回
const int64_t GH_OTHER_GROUP_ID = 900202;

//每条测试消息都带的搜索关键字
const char* const GH_KEYWORD = "ghkeyword";

//一次拉取 / 搜索的条数上限，比本套用例造的消息多
const int GH_LIMIT = 20;

//造的消息条数
const int GH_MSG_COUNT = 5;

//同一测试库里 clientmsgid 的序号，保证每条消息的去重键不同
int g_ghSeq = 0;

//存一条带关键字的群文字消息，返回服务端分配的 msgid
int64_t saveGroupText(int index)
{
    Json::Value content(Json::objectValue);
    content["text"] = std::string(GH_KEYWORD) + " " + std::to_string(index);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string stored = MessageProtocol::buildStoredContent(Json::writeString(wb, content),
                                                                   std::vector<int64_t>(), false, "");
    ++g_ghSeq;
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        "gh-" + std::to_string(g_ghSeq), conversation_group, GH_OLD_MEMBER, static_cast<int32_t>(GH_GROUP_ID),
        message_type_text, stored, GH_BASE_TIME + index * GH_STEP_MS);
    REQUIRE(msgid > 0);
    return msgid;
}

//重建测试库并在测试群里造 GH_MSG_COUNT 条消息，按时间先后返回它们的 msgid
std::vector<int64_t> resetWithMessages()
{
    testdb::resetDb(GH_DB_PATH, GH_MYSQL_DB);
    std::vector<int64_t> ids;
    for (int i = 0; i < GH_MSG_COUNT; ++i)
        ids.push_back(saveGroupText(i));
    return ids;
}

//以新成员身份拉历史
std::vector<MessageRecord> sync(bool afterMode, int64_t anchor, int limit, int64_t minCreateTime, int64_t minMsgId,
                                bool& hasMore)
{
    SyncMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = GH_GROUP_ID;
    req.isAfterMode = afterMode;
    if (afterMode)
        req.aftermsgid = anchor;
    else
        req.beforemsgid = anchor;
    req.limit = limit;
    std::vector<MessageRecord> out;
    hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationMessages(GH_NEW_MEMBER, req, out, hasMore,
                                                                          minCreateTime, minMsgId));
    return out;
}

//取一批消息的 msgid，便于整体比较
std::vector<int64_t> idsOf(const std::vector<MessageRecord>& msgs)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < msgs.size(); ++i)
        ids.push_back(msgs[i].msgid);
    return ids;
}

//由若干 msgid 拼一个期望列表
std::vector<int64_t> ids2(int64_t a, int64_t b)
{
    std::vector<int64_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

//解析 1305 请求，返回是否成功
bool parseInvite(const std::string& json, GroupMembersOpRequest& req)
{
    return GroupProtocol::parseGroupMembersOpRequest(json, req);
}

//1305 请求的固定前缀：群与一位被邀请人
const char* const GH_INVITE_HEAD = "{\"groupid\":\"900201\",\"userids\":[\"400002\"]";

} // namespace

TEST_CASE("GH1 解析 1305 的 historylimit")
{
    GroupMembersOpRequest req;
    REQUIRE(parseInvite(std::string(GH_INVITE_HEAD) + "}", req));
    CHECK(req.historylimit == kInviteHistoryLimitAll);

    const int valid[] = { -1, 0, 100, kInviteHistoryLimitMax };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
    {
        GroupMembersOpRequest r;
        REQUIRE(parseInvite(std::string(GH_INVITE_HEAD) + ",\"historylimit\":" + std::to_string(valid[i]) + "}", r));
        CHECK(r.historylimit == valid[i]);
    }

    //超出范围或类型不对：判为请求非法
    GroupMembersOpRequest bad;
    CHECK_FALSE(parseInvite(std::string(GH_INVITE_HEAD) + ",\"historylimit\":-2}", bad));
    CHECK_FALSE(parseInvite(std::string(GH_INVITE_HEAD) + ",\"historylimit\":"
                            + std::to_string(kInviteHistoryLimitMax + 1) + "}", bad));
    CHECK_FALSE(parseInvite(std::string(GH_INVITE_HEAD) + ",\"historylimit\":\"5\"}", bad));
}

TEST_CASE("GH2 计算历史边界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    MessageManager& mgr = Singleton<MessageManager>::Instance();
    int64_t from = -1;

    //同步全部：不设下界
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, kInviteHistoryLimitAll, from));
    CHECK(from == 0);
    //不同步：本群最大 msgid + 1，之后插入的入群提示一定看得到
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, 0, from));
    CHECK(from == ids.back() + 1);
    //最近 2 条：倒数第 2 条
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, 2, from));
    CHECK(from == ids[GH_MSG_COUNT - 2]);
    //最近 5 条：恰好是第一条
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, GH_MSG_COUNT, from));
    CHECK(from == ids[0]);
    //最近 6 条：本群不足 6 条，全部可见
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, GH_MSG_COUNT + 1, from));
    CHECK(from == 0);

    //空群：无论选多少条都不设下界
    testdb::resetDb(GH_DB_PATH, GH_MYSQL_DB);
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, 0, from));
    CHECK(from == 0);
    REQUIRE(mgr.computeGroupHistoryFrom(GH_GROUP_ID, 3, from));
    CHECK(from == 0);

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH3 addMembers 带上历史边界写库，三种读法都能读回")
{
    testdb::resetDb(GH_DB_PATH, GH_MYSQL_DB);
    GroupManager& gm = Singleton<GroupManager>::Instance();
    const int64_t kHistoryFrom = 1234;

    std::vector<int64_t> joiners;
    joiners.push_back(GH_NEW_MEMBER);
    REQUIRE(gm.addMembers(GH_GROUP_ID, joiners, GH_BASE_TIME, kHistoryFrom));
    //另一个群不设边界（默认参数）
    REQUIRE(gm.addMembers(GH_OTHER_GROUP_ID, joiners, GH_BASE_TIME));

    std::vector<GroupMemberRecord> members;
    REQUIRE(gm.getGroupMembers(GH_GROUP_ID, members));
    REQUIRE(members.size() == 1);
    CHECK(members[0].historyfrom == kHistoryFrom);

    GroupMemberRecord one;
    REQUIRE(gm.getMember(GH_GROUP_ID, GH_NEW_MEMBER, one));
    CHECK(one.historyfrom == kHistoryFrom);
    CHECK(one.jointime == GH_BASE_TIME);
    //不在群里：返回 false
    GroupMemberRecord none;
    CHECK_FALSE(gm.getMember(GH_GROUP_ID, GH_OLD_MEMBER, none));

    std::map<int64_t, GroupMemberRecord> mine;
    REQUIRE(gm.getUserMemberships(GH_NEW_MEMBER, mine));
    REQUIRE(mine.size() == 2);
    CHECK(mine[GH_GROUP_ID].historyfrom == kHistoryFrom);
    CHECK(mine[GH_OTHER_GROUP_ID].historyfrom == 0);

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH4 拉历史带 msgid 下界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    //下界取倒数第 2 条：只有最后两条可见
    const int64_t bound = ids[GH_MSG_COUNT - 2];
    bool hasMore = true;

    //从最新往前翻
    CHECK(idsOf(sync(false, 0, GH_LIMIT, 0, bound, hasMore)) == ids2(ids[3], ids[4]));
    CHECK_FALSE(hasMore);
    //从最后一条往前翻 1 条：拿到边界那条，再往前没有了
    CHECK(idsOf(sync(false, ids[4], 1, 0, bound, hasMore)) == std::vector<int64_t>(1, ids[3]));
    CHECK_FALSE(hasMore);
    //从头往后补
    CHECK(idsOf(sync(true, 0, GH_LIMIT, 0, bound, hasMore)) == ids2(ids[3], ids[4]));
    //不设下界时全部可见
    CHECK(sync(false, 0, GH_LIMIT, 0, 0, hasMore).size() == static_cast<size_t>(GH_MSG_COUNT));

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH5 会话内搜索带 msgid 下界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    SearchMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = GH_GROUP_ID;
    req.keyword = GH_KEYWORD;
    req.limit = GH_LIMIT;

    std::vector<MessageRecord> out;
    bool hasMore = true;
    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(GH_NEW_MEMBER, req, out, hasMore, 0,
                                                                 ids[GH_MSG_COUNT - 2]));
    CHECK(idsOf(out) == ids2(ids[3], ids[4]));
    CHECK_FALSE(hasMore);

    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(GH_NEW_MEMBER, req, out, hasMore, 0, 0));
    CHECK(out.size() == static_cast<size_t>(GH_MSG_COUNT));

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH6 全局搜索的群范围带 msgid 下界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    std::vector<GroupSearchScope> scopes(1);
    scopes[0].groupid = GH_GROUP_ID;
    scopes[0].minMsgId = ids[GH_MSG_COUNT - 2];

    std::vector<GlobalSearchConversationHit> hits;
    REQUIRE(Singleton<MessageManager>::Instance().globalSearchConversations(GH_NEW_MEMBER, GH_KEYWORD, scopes,
                                                                            GH_LIMIT, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].hitcount == 2);
    CHECK(hits[0].latestmsgid == ids[4]);

    //不设下界：五条都命中
    scopes[0].minMsgId = 0;
    REQUIRE(Singleton<MessageManager>::Instance().globalSearchConversations(GH_NEW_MEMBER, GH_KEYWORD, scopes,
                                                                            GH_LIMIT, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].hitcount == GH_MSG_COUNT);

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH7 Pin 列表带下界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    MessagePinManager& pm = Singleton<MessagePinManager>::Instance();
    //钉住第 2 条与第 5 条
    REQUIRE(pm.pinMessage(ids[1], GH_OLD_MEMBER, GH_BASE_TIME));
    REQUIRE(pm.pinMessage(ids[4], GH_OLD_MEMBER, GH_BASE_TIME));

    std::vector<MessageRecord> msgs;
    std::vector<PinRecord> pins;
    int64_t total = -1;
    REQUIRE(pm.getPinnedMessages(conversation_group, GH_GROUP_ID, GH_NEW_MEMBER, GH_LIMIT, msgs, pins, total, 0,
                                 ids[GH_MSG_COUNT - 2]));
    CHECK(total == 1);
    CHECK(idsOf(msgs) == std::vector<int64_t>(1, ids[4]));

    //不设下界：两条都在
    REQUIRE(pm.getPinnedMessages(conversation_group, GH_GROUP_ID, GH_NEW_MEMBER, GH_LIMIT, msgs, pins, total));
    CHECK(total == 2);

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH8 时间下界与 msgid 下界同时生效，取更严格的；群主与管理员不设下界")
{
    const std::vector<int64_t> ids = resetWithMessages();
    bool hasMore = true;
    //时间下界只放行最后一条，msgid 下界放行最后三条：结果只有最后一条
    const int64_t lastTime = GH_BASE_TIME + (GH_MSG_COUNT - 1) * GH_STEP_MS;
    CHECK(idsOf(sync(false, 0, GH_LIMIT, lastTime, ids[2], hasMore)) == std::vector<int64_t>(1, ids[4]));
    //反过来：时间下界放行最后三条，msgid 下界只放行最后一条
    const int64_t thirdTime = GH_BASE_TIME + 2 * GH_STEP_MS;
    CHECK(idsOf(sync(false, 0, GH_LIMIT, thirdTime, ids[4], hasMore)) == std::vector<int64_t>(1, ids[4]));
    testdb::cleanupDb(GH_DB_PATH);

    //computeHistoryBound：普通成员按群设置与本人的边界；群主与管理员一律不设下界
    const int64_t kJoin = 1700000500000LL;
    const int64_t kFrom = 77;
    GroupHistoryBound b = GroupProtocol::computeHistoryBound(group_role_member, 0, kJoin, kFrom);
    CHECK(b.m_minCreateTime == kJoin);
    CHECK(b.m_minMsgId == kFrom);
    b = GroupProtocol::computeHistoryBound(group_role_member, 1, kJoin, kFrom);
    CHECK(b.m_minCreateTime == 0);
    CHECK(b.m_minMsgId == kFrom);
    b = GroupProtocol::computeHistoryBound(group_role_admin, 0, kJoin, kFrom);
    CHECK(b.m_minCreateTime == 0);
    CHECK(b.m_minMsgId == 0);
    b = GroupProtocol::computeHistoryBound(group_role_owner, 0, kJoin, kFrom);
    CHECK(b.m_minCreateTime == 0);
    CHECK(b.m_minMsgId == 0);
}

TEST_CASE("GH9 加群申请记下同步条数")
{
    testdb::resetDb(GH_DB_PATH, GH_MYSQL_DB);
    GroupManager& gm = Singleton<GroupManager>::Instance();
    const int kLimit = 5;

    //由邀请转来的申请：记邀请人选择的条数
    const int64_t invited = gm.addJoinRequest(GH_GROUP_ID, GH_NEW_MEMBER, "", GH_BASE_TIME,
                                              group_join_source_friend, GH_OLD_MEMBER, kLimit);
    REQUIRE(invited > 0);
    GroupJoinRequestRecord rec;
    REQUIRE(gm.getJoinRequestById(invited, rec));
    CHECK(rec.historylimit == kLimit);

    //本人主动申请：不传，按全部处理
    const int64_t applied = gm.addJoinRequest(GH_GROUP_ID, GH_OLD_MEMBER, "hi", GH_BASE_TIME);
    REQUIRE(applied > 0);
    REQUIRE(gm.getJoinRequestById(applied, rec));
    CHECK(rec.historylimit == kInviteHistoryLimitAll);

    testdb::cleanupDb(GH_DB_PATH);
}

TEST_CASE("GH10 1305 应答带三项统计")
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    const std::string text = GroupProtocol::buildInviteToGroupResponse(0, "ok", 1, 2, 3);
    REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == std::string("ok"));
    CHECK(root["added"].asInt() == 1);
    CHECK(root["invited"].asInt() == 2);
    CHECK(root["pending"].asInt() == 3);
}
