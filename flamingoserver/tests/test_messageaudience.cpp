/**
 *  消息可见范围的单元测试（Suite AUD），test_messageaudience.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 6 步（2026-10-02 与用户确认的用例）：
 *    - AUD1 saveMessage 写入可见范围：不带范围时 f_audience 为 0；带范围时置 1 并去重写入
 *           t_message_audience；空范围表示只有发送者本人可见；
 *    - AUD2~AUD8 读取过滤（真的打开临时库跑 SQL，默认 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL，
 *           见 test_dbbackend.h）：同步、会话列表的最后一条与未读、搜索、按 ID 取消息、与逐条删除
 *           叠加、删除会话后能否重新出现，都只让发送者与可见范围内的人看到受限消息。
 *    - AUD9 纯函数判定：共享卡片不允许客户端发送（含群发）、置顶、撤回，其余类型不受影响；
 *    - AUD10 转发取源的判定：共享卡片与已撤回的消息不能作为转发源，其余照常。
 *
 *  典型用法：Tests.exe --test-case="AUD*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kAudDbPath = "flamingo_sqlite_audience_test.db";
const char* const kAudMysqlDb = "flamingo_test_aud";

//A 是发卡片的人（分享者），B 在可见范围内，C 与 A、B 同群但不在可见范围内
const int32_t kUserA = 100001;
const int32_t kUserB = 100002;
const int32_t kUserC = 100003;

//两个群：G 里有普通消息与卡片，G2 里只有一张卡片
const int32_t kGroupG = 900001;
const int32_t kGroupG2 = 900002;

//消息时间轴：以 kT0 为起点每隔一秒一条，固定取值保证结果可复现
const int64_t kT0 = 1700000000000LL;
const int64_t kStepMs = 1000;

//一次同步 / 搜索最多取的条数，远大于本套用例里任何一个会话的消息数
const int kFetchLimit = 100;

//AUD2 分页用的每页条数：恰好等于 C 能看到的消息数
const int kPageLimit = 2;

//写入删除会话标记时用的时间（Unix 毫秒）
const int64_t kUpdateTimeMs = 1754400000000LL;

//生成 clientmsgid 的序号，保证本进程内每条消息不重复
int g_clientSeq = 0;

void ResetAudDb()
{
    testdb::resetDb(kAudDbPath, kAudMysqlDb);
}

void CleanupAudDb()
{
    testdb::cleanupDb(kAudDbPath);
}

//生成一个不重复的 clientmsgid
std::string NextClientId()
{
    ++g_clientSeq;
    char buf[32] = { 0 };
    ::snprintf(buf, sizeof(buf), "aud-%d", g_clientSeq);
    return buf;
}

//第 index 条消息的时间
int64_t TimeAt(int index)
{
    return kT0 + index * kStepMs;
}

//存一条全员可见的文本消息，返回 msgid
int64_t SaveText(int ctype, int32_t from, int32_t to, const char* text, int index)
{
    char content[256] = { 0 };
    ::snprintf(content, sizeof(content), "{\"text\":\"%s\"}", text);
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        NextClientId(), ctype, from, to, message_type_text, stored, TimeAt(index));
    REQUIRE(msgid > 0);
    return msgid;
}

//存一张只给 audience 里的人看的共享卡片，返回 msgid。name 写进正文，搜索用例靠它命中关键字
int64_t SaveCard(int ctype, int32_t from, int32_t to, const char* name, int index,
                 const std::vector<int32_t>& audience)
{
    char content[256] = { 0 };
    ::snprintf(content, sizeof(content),
               "{\"shareid\":\"1\",\"kind\":2,\"name\":\"%s\",\"displaypath\":\"D:\\\\%s\",\"ownerid\":\"%d\"}",
               name, name, from);
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        NextClientId(), ctype, from, to, message_type_fileshare, stored, TimeAt(index), &audience);
    REQUIRE(msgid > 0);
    return msgid;
}

//只含一个人的可见范围
std::vector<int32_t> AudienceOf(int32_t userid)
{
    return std::vector<int32_t>(1, userid);
}

//以 userid 的身份同步某会话，返回 msgid 列表（升序）；hasMore 为出参
std::vector<int64_t> SyncIds(int32_t userid, int ctype, int64_t cid, int limit, bool& hasMore)
{
    SyncMessagesRequest req;
    req.ctype = ctype;
    req.cid = cid;
    req.beforemsgid = 0;
    req.limit = limit;
    std::vector<MessageRecord> records;
    hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationMessages(userid, req, records, hasMore));

    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//以 userid 的身份同步某会话的全部消息
std::vector<int64_t> SyncIds(int32_t userid, int ctype, int64_t cid)
{
    bool hasMore = false;
    return SyncIds(userid, ctype, cid, kFetchLimit, hasMore);
}

//以 userid 的身份在某会话里按关键字搜索，返回命中的 msgid 列表（升序）
std::vector<int64_t> SearchIds(int32_t userid, int ctype, int64_t cid, const char* keyword)
{
    SearchMessagesRequest req;
    req.ctype = ctype;
    req.cid = cid;
    req.keyword = keyword;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(userid, req, records, hasMore));

    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//以 userid 的身份按 ID 取消息（转发取源所用），返回取到的 msgid 列表（升序）
std::vector<int64_t> FetchByIds(int32_t userid, const std::vector<int64_t>& msgids)
{
    std::vector<MessageRecord> records;
    REQUIRE(Singleton<MessageManager>::Instance().getMessagesByIds(userid, msgids, records));

    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//由若干 msgid 拼出期望列表
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

std::vector<int64_t> Ids(int64_t a, int64_t b, int64_t c)
{
    std::vector<int64_t> v = Ids(a, b);
    v.push_back(c);
    return v;
}

//在会话摘要列表里按 cid 找一条；找不到返回 NULL
const ConversationBrief* FindBrief(const std::vector<ConversationBrief>& list, int64_t cid)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].cid == cid)
            return &list[i];
    }
    return NULL;
}

//userid 的单聊会话摘要
std::vector<ConversationBrief> SingleBriefsOf(int32_t userid)
{
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationBriefs(userid, out));
    return out;
}

//userid 在 G、G2 两个群里的会话摘要
std::vector<ConversationBrief> GroupBriefsOf(int32_t userid)
{
    std::vector<int64_t> groupids;
    groupids.push_back(kGroupG);
    groupids.push_back(kGroupG2);
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(userid, groupids, out));
    return out;
}

//执行一条只返回一个整数的查询
int64_t QueryInt(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    QueryResult* pResult = pConn->query(sql.c_str());
    REQUIRE(pResult != NULL);
    int64_t value = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return value;
}

//某条消息的 f_audience
int64_t AudienceFlagOf(int64_t msgid)
{
    char sql[128] = { 0 };
    ::snprintf(sql, sizeof(sql), "SELECT f_audience FROM t_chatmsg WHERE f_msgid = %lld", (long long)msgid);
    return QueryInt(sql);
}

//某条消息在可见范围表里的行数
int64_t AudienceRowsOf(int64_t msgid)
{
    char sql[128] = { 0 };
    ::snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_message_audience WHERE f_msgid = %lld", (long long)msgid);
    return QueryInt(sql);
}

//某条消息的可见范围里是否有 userid
bool AudienceHas(int64_t msgid, int32_t userid)
{
    char sql[160] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "SELECT COUNT(*) FROM t_message_audience WHERE f_msgid = %lld AND f_user_id = %d",
               (long long)msgid, userid);
    return QueryInt(sql) == 1;
}

//以 userid 的身份把某个群会话从列表删除
void DeleteGroupConversation(int32_t userid, int64_t groupid)
{
    SetConversationRequest req;
    req.ctype = conversation_group;
    req.cid = groupid;
    req.has_deleted = true;
    req.deleted = true;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(userid, req, kUpdateTimeMs));
}

} // anonymous namespace

TEST_CASE("AUD1 saveMessage 写入可见范围：不带范围为 0，带范围置 1 并去重，空范围只有发送者可见")
{
    ResetAudDb();

    //不带范围：与改动前完全一样
    const int64_t plain = SaveText(conversation_group, kUserA, kGroupG, "hello", 1);
    CHECK(AudienceFlagOf(plain) == 0);
    CHECK(AudienceRowsOf(plain) == 0);

    //带范围 {B, B, C}：重复的 B 只写一行
    std::vector<int32_t> audience;
    audience.push_back(kUserB);
    audience.push_back(kUserB);
    audience.push_back(kUserC);
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, audience);
    CHECK(AudienceFlagOf(card) == 1);
    CHECK(AudienceRowsOf(card) == 2);
    CHECK(AudienceHas(card, kUserB));
    CHECK(AudienceHas(card, kUserC));

    //空范围：标记为受限、范围表为空，只有发送者本人能看到
    const int64_t onlySelf = SaveCard(conversation_group, kUserA, kGroupG, "Secret", 3, std::vector<int32_t>());
    CHECK(AudienceFlagOf(onlySelf) == 1);
    CHECK(AudienceRowsOf(onlySelf) == 0);
    CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(plain, card, onlySelf));
    CHECK(SyncIds(kUserB, conversation_group, kGroupG) == Ids(plain, card));

    CleanupAudDb();
}

TEST_CASE("AUD2 群同步：只有发送者与范围内的人拿得到卡片，分页不受看不到的卡片干扰")
{
    ResetAudDb();
    const int64_t m1 = SaveText(conversation_group, kUserA, kGroupG, "g1", 1);
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, AudienceOf(kUserB));
    const int64_t m3 = SaveText(conversation_group, kUserC, kGroupG, "g3", 3);

    CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(m1, card, m3));
    CHECK(SyncIds(kUserB, conversation_group, kGroupG) == Ids(m1, card, m3));
    CHECK(SyncIds(kUserC, conversation_group, kGroupG) == Ids(m1, m3));

    //C 每页取 2 条：恰好取完能看到的两条，没有更多
    bool hasMore = true;
    CHECK(SyncIds(kUserC, conversation_group, kGroupG, kPageLimit, hasMore) == Ids(m1, m3));
    CHECK_FALSE(hasMore);

    //B 每页取 2 条：取到最新的两条，还有更多
    CHECK(SyncIds(kUserB, conversation_group, kGroupG, kPageLimit, hasMore) == Ids(card, m3));
    CHECK(hasMore);

    CleanupAudDb();
}

TEST_CASE("AUD3 群会话列表：最后一条与未读按各自能看到的消息计算，只有卡片的群对范围外的人不出现")
{
    ResetAudDb();
    const int64_t m1 = SaveText(conversation_group, kUserA, kGroupG, "g1", 1);
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, AudienceOf(kUserB));
    const int64_t onlyCard = SaveCard(conversation_group, kUserA, kGroupG2, "Docs", 3, AudienceOf(kUserB));

    const std::vector<ConversationBrief> ofB = GroupBriefsOf(kUserB);
    const ConversationBrief* gB = FindBrief(ofB, kGroupG);
    REQUIRE(gB != NULL);
    CHECK(gB->lastmsgid == card);
    CHECK(gB->lastmsgtype == message_type_fileshare);
    CHECK(gB->unread == 2);
    const ConversationBrief* g2B = FindBrief(ofB, kGroupG2);
    REQUIRE(g2B != NULL);
    CHECK(g2B->lastmsgid == onlyCard);

    const std::vector<ConversationBrief> ofC = GroupBriefsOf(kUserC);
    const ConversationBrief* gC = FindBrief(ofC, kGroupG);
    REQUIRE(gC != NULL);
    CHECK(gC->lastmsgid == m1);
    CHECK(gC->lastmsgtype == message_type_text);
    CHECK(gC->unread == 1);
    CHECK(FindBrief(ofC, kGroupG2) == NULL);

    //发送者本人：最后一条是自己发的卡片，自己发的不算未读
    const std::vector<ConversationBrief> ofA = GroupBriefsOf(kUserA);
    const ConversationBrief* gA = FindBrief(ofA, kGroupG);
    REQUIRE(gA != NULL);
    CHECK(gA->lastmsgid == card);
    CHECK(gA->unread == 0);

    CleanupAudDb();
}

TEST_CASE("AUD4 单聊的同步、会话列表与未读同样过滤受限消息")
{
    ResetAudDb();
    const int64_t m1 = SaveText(conversation_single, kUserA, kUserB, "s1", 1);
    const int64_t hidden = SaveCard(conversation_single, kUserA, kUserB, "Secret", 2, std::vector<int32_t>());

    CHECK(SyncIds(kUserB, conversation_single, kUserA) == Ids(m1));
    CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(m1, hidden));

    const std::vector<ConversationBrief> briefsB = SingleBriefsOf(kUserB);
    const ConversationBrief* ofB = FindBrief(briefsB, kUserA);
    REQUIRE(ofB != NULL);
    CHECK(ofB->lastmsgid == m1);
    CHECK(ofB->unread == 1);

    const std::vector<ConversationBrief> briefsA = SingleBriefsOf(kUserA);
    const ConversationBrief* ofA = FindBrief(briefsA, kUserB);
    REQUIRE(ofA != NULL);
    CHECK(ofA->lastmsgid == hidden);

    CleanupAudDb();
}

TEST_CASE("AUD5 搜索：范围外的人按共享名搜不到卡片")
{
    ResetAudDb();
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 1, AudienceOf(kUserB));
    const int64_t talk = SaveText(conversation_group, kUserC, kGroupG, "Projects plan", 2);

    CHECK(SearchIds(kUserB, conversation_group, kGroupG, "Projects") == Ids(card, talk));
    CHECK(SearchIds(kUserA, conversation_group, kGroupG, "Projects") == Ids(card, talk));
    CHECK(SearchIds(kUserC, conversation_group, kGroupG, "Projects") == Ids(talk));

    CleanupAudDb();
}

TEST_CASE("AUD6 按 ID 取消息：范围外的人取不到卡片")
{
    ResetAudDb();
    const int64_t m1 = SaveText(conversation_group, kUserA, kGroupG, "g1", 1);
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, AudienceOf(kUserB));

    const std::vector<int64_t> wanted = Ids(m1, card);
    CHECK(FetchByIds(kUserB, wanted) == Ids(m1, card));
    CHECK(FetchByIds(kUserA, wanted) == Ids(m1, card));
    CHECK(FetchByIds(kUserC, wanted) == Ids(m1));

    CleanupAudDb();
}

TEST_CASE("AUD7 与逐条删除叠加：B 删除卡片后自己看不到，A 不受影响")
{
    ResetAudDb();
    const int64_t m1 = SaveText(conversation_group, kUserA, kGroupG, "g1", 1);
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, AudienceOf(kUserB));

    REQUIRE(Singleton<MessageManager>::Instance().deleteMessageForUser(kUserB, card));
    CHECK(SyncIds(kUserB, conversation_group, kGroupG) == Ids(m1));
    CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(m1, card));

    CleanupAudDb();
}

TEST_CASE("AUD8 删除会话后，一张看不到的卡片不会让会话重新出现，看得到的新消息照常让它出现")
{
    ResetAudDb();
    SaveText(conversation_group, kUserA, kGroupG, "g1", 1);

    //B、C 都把群会话从列表删除
    DeleteGroupConversation(kUserB, kGroupG);
    DeleteGroupConversation(kUserC, kGroupG);
    CHECK(FindBrief(GroupBriefsOf(kUserB), kGroupG) == NULL);
    CHECK(FindBrief(GroupBriefsOf(kUserC), kGroupG) == NULL);

    //来了一张只给 B 看的卡片：B 的会话重新出现，C 的继续隐藏
    const int64_t card = SaveCard(conversation_group, kUserA, kGroupG, "Projects", 2, AudienceOf(kUserB));
    const std::vector<ConversationBrief> ofB = GroupBriefsOf(kUserB);
    const ConversationBrief* gB = FindBrief(ofB, kGroupG);
    REQUIRE(gB != NULL);
    CHECK(gB->lastmsgid == card);
    CHECK(FindBrief(GroupBriefsOf(kUserC), kGroupG) == NULL);

    //再来一条全员可见的消息：C 的会话也重新出现
    const int64_t m3 = SaveText(conversation_group, kUserA, kGroupG, "g3", 3);
    const std::vector<ConversationBrief> ofC = GroupBriefsOf(kUserC);
    const ConversationBrief* gC = FindBrief(ofC, kGroupG);
    REQUIRE(gC != NULL);
    CHECK(gC->lastmsgid == m3);

    CleanupAudDb();
}

TEST_CASE("AUD9 共享卡片不允许客户端发送、置顶、撤回，其余类型不受影响")
{
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_fileshare));

    CHECK(MessageProtocol::isClientOperableMsgType(message_type_text));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_image));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_file));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_poll));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_relay));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_merged));
}

TEST_CASE("AUD10 转发取源：共享卡片与已撤回的消息不能转发，其余照常")
{
    MessageRecord text;
    text.msgtype = message_type_text;
    text.recalled = false;
    CHECK(MessageProtocol::isForwardableSource(text));

    MessageRecord merged;
    merged.msgtype = message_type_merged;
    merged.recalled = false;
    CHECK(MessageProtocol::isForwardableSource(merged));

    MessageRecord card;
    card.msgtype = message_type_fileshare;
    card.recalled = false;
    CHECK_FALSE(MessageProtocol::isForwardableSource(card));

    MessageRecord recalled;
    recalled.msgtype = message_type_text;
    recalled.recalled = true;
    CHECK_FALSE(MessageProtocol::isForwardableSource(recalled));
}
