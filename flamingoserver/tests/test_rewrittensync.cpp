/**
 *  消息改写离线同步的服务端单元测试（Suite RW），test_rewrittensync.cpp
 *
 *  方案见 docs/消息改写离线同步方案.md 第 6 节。服务端改写已发出的消息（updateMessageContent）与撤回
 *  （recallMessage）时，给这条消息分配一个全局递增的改写序号 f_rev；客户端登录后用
 *  cmd_sync_rewritten_messages（1526）按序号拉取离线期间被改写过的消息。本文件覆盖：
 *    RW1  新消息的 f_rev 为 0；改写之后大于 0；同一条消息再改写一次，序号更大
 *    RW2  不同消息先后改写，序号严格递增；模拟服务端重启（新建一个 MessageManager，重新读库里的最大值）
 *         之后再改写，序号接着增长、不与旧值重复
 *    RW3  撤回分配序号，经按序号同步取出的条目 recalled 为 1；f_search_text 的存量回填不分配序号
 *    RW4  1526 请求解析：since 缺省、不是字符串或为负时取 0；limit 缺省或不大于 0 时取上限 200，超过时截到 200
 *    RW5  只返回序号大于 since 的消息、按序号升序；limit 生效，more 与应答的 rev 正确；空页时 rev 等于 since、
 *         more 为 false
 *    RW6  可见范围：单聊只返回本人是收发一方的；群聊只返回本人当前所在群的；退群后不再返回那个群的改写；
 *         f_audience 为 1 且本人不在名单里的不返回
 *    RW7  条目与 1502 的消息条目逐字段一致（同一条消息分别经两种应答组装，去掉 rev 后比较），另带 rev
 *    RW8  currev 等于库里当前的最大序号
 *  （RW9 随第 3a 步加入，在 test_threadgroup.cpp）
 *
 *  每条用例真的打开临时库跑 SQL（默认 SQLite；设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL，见 test_dbbackend.h）。
 *
 *  典型用法：Tests.exe --test-case="RW*"
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include <stdint.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kRwDbPath = "flamingo_sqlite_rewrittensync_test.db";
const char* const kRwMysqlDb = "flamingo_test_rewrittensync";

//单聊双方 A、B；C、D 是另一段与 A 无关的单聊，E 是群里的另一名成员
const int32_t kUserA = 100101;
const int32_t kUserB = 100102;
const int32_t kUserC = 100103;
const int32_t kUserD = 100104;
const int32_t kUserE = 100105;
//消息时间（Unix 毫秒），固定取值保证结果可复现；逐条加一，让消息按时间先后排列
const int64_t kBaseTimeMs = 1785000000000LL;
//存量回填一批处理的条数，远大于本套用例里的消息数
const int kBackfillBatch = 100;
//按序号同步一次取的条数，远大于本套用例里的消息数
const int kFetchAll = 100;
//RW5 分页用的页大小，小于被改写的消息数，才能翻出多页
const int kSmallPage = 2;
//RW5 被改写的消息条数
const int kRewrittenCount = 5;
//RW4 里超过上限的 limit 取值
const int kOverLimit = 500;
//组装应答时的体积预算，远大于本套用例里的应答，不会截断
const size_t kNoTrimBudget = 1024 * 1024;

//解析 JSON 对象，失败或不是对象返回 false
bool ParseObject(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && out.isObject();
}

//以 userid 的身份按序号同步一页：可见范围取自 GroupManager::getUserHistoryScopes，与 ChatSession 的做法相同
std::vector<RewrittenMessage> FetchRewritten(int32_t userid, int64_t since, int limit, bool& more, int64_t& currev)
{
    std::vector<GroupSearchScope> scopes;
    REQUIRE(Singleton<GroupManager>::Instance().getUserHistoryScopes(userid, scopes));
    std::vector<RewrittenMessage> items;
    REQUIRE(Singleton<MessageManager>::Instance().getRewrittenMessages(userid, scopes, since, limit, items, more,
                                                                        currev));
    return items;
}

//同上，只取消息 id，便于比较
std::vector<int64_t> FetchRewrittenIds(int32_t userid)
{
    bool more = false;
    int64_t currev = 0;
    const std::vector<RewrittenMessage> items = FetchRewritten(userid, 0, kFetchAll, more, currev);
    std::vector<int64_t> ids;
    for (size_t i = 0; i < items.size(); ++i)
        ids.push_back(items[i].record.msgid);
    return ids;
}

//msgid 是否在列表里
bool Contains(const std::vector<int64_t>& ids, int64_t msgid)
{
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] == msgid)
            return true;
    }
    return false;
}

//在群 groupid 里以 from 的名义发一条文字消息，返回 msgid
//  audience：可见名单（借用）；NULL 表示全员可见
int64_t SaveGroupText(int64_t groupid, int32_t from, const std::string& clientMsgId, int seq,
                      const std::vector<int32_t>* audience = NULL)
{
    const std::string content = "{\"text\":\"group\"}";
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_group, from, static_cast<int32_t>(groupid), message_type_text, stored,
        kBaseTimeMs + seq, audience);
    REQUIRE(msgid > 0);
    return msgid;
}

//以 from 的名义给 to 发一条单聊文字消息，返回 msgid
int64_t SaveSingleText(int32_t from, int32_t to, const std::string& clientMsgId, int seq)
{
    const std::string content = "{\"text\":\"single\"}";
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_single, from, to, message_type_text, stored, kBaseTimeMs + seq);
    REQUIRE(msgid > 0);
    return msgid;
}

//打开一条测试库连接；失败时终止当前用例
IDatabase* OpenConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

//以 A 的名义给 B 发一条文字消息，返回 msgid
//  clientMsgId：客户端消息 id，各条用例里取不同的值；text：正文；seq：第几条，用来错开消息时间
int64_t SaveText(const std::string& clientMsgId, const std::string& text, int seq)
{
    const std::string content = "{\"text\":\"" + text + "\"}";
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_single, kUserA, kUserB, message_type_text, stored, kBaseTimeMs + seq);
    REQUIRE(msgid > 0);
    return msgid;
}

//把一条消息改写成新正文（与各改写场景一样经 buildStoredContent 打包），经指定的 MessageManager 写库
bool Rewrite(MessageManager& mgr, int64_t msgid, const std::string& text)
{
    const std::string content = "{\"text\":\"" + text + "\"}";
    return mgr.updateMessageContent(msgid, MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, ""));
}

//直接查库读一条消息的 f_rev；查不到返回 -1
int64_t ReadRev(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(OpenConn());
    std::ostringstream os;
    os << "SELECT f_rev FROM t_chatmsg WHERE f_msgid = " << msgid;
    QueryResult* pResult = pConn->query(os.str().c_str());
    REQUIRE(pResult != NULL);
    int64_t rev = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        rev = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return rev;
}

//直接查库：一条消息的 f_search_text 是不是 NULL（尚未提取）
bool SearchTextIsNull(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(OpenConn());
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_chatmsg WHERE f_msgid = " << msgid << " AND f_search_text IS NULL";
    QueryResult* pResult = pConn->query(os.str().c_str());
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    const bool isNull = (pRow != NULL) && pRow[0].getUInt64() > 0;
    pResult->endQuery();
    delete pResult;
    return isNull;
}

} // namespace

TEST_CASE("RW1 新消息的改写序号为 0，改写后大于 0，再改写一次序号更大")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    const int64_t msgid = SaveText("rw1-a", "first", 1);
    CHECK(ReadRev(msgid) == 0);

    REQUIRE(Rewrite(mgr, msgid, "second"));
    const int64_t rev1 = ReadRev(msgid);
    CHECK(rev1 > 0);

    REQUIRE(Rewrite(mgr, msgid, "third"));
    const int64_t rev2 = ReadRev(msgid);
    CHECK(rev2 > rev1);

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW2 不同消息先后改写序号严格递增；重启后接着库里的最大值继续，不重复")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    const int64_t m1 = SaveText("rw2-1", "one", 1);
    const int64_t m2 = SaveText("rw2-2", "two", 2);
    const int64_t m3 = SaveText("rw2-3", "three", 3);

    REQUIRE(Rewrite(mgr, m1, "one-edited"));
    REQUIRE(Rewrite(mgr, m2, "two-edited"));
    const int64_t r1 = ReadRev(m1);
    const int64_t r2 = ReadRev(m2);
    CHECK(r1 > 0);
    CHECK(r2 > r1);

    //模拟服务端重启：新建一个 MessageManager，它第一次分配序号时重新读库里的 MAX(f_rev)。
    //单例里缓存的序号在 resetDb 之后可能比库里的大，所以这里只能用新对象验证「从库里接着来」
    MessageManager restarted;
    REQUIRE(Rewrite(restarted, m3, "three-edited"));
    const int64_t r3 = ReadRev(m3);
    CHECK(r3 == r2 + 1);

    //重启后的对象继续分配：仍然递增，且不与重启前的任何一个序号重复
    REQUIRE(Rewrite(restarted, m1, "one-edited-again"));
    const int64_t r4 = ReadRev(m1);
    CHECK(r4 > r3);
    CHECK(r4 != r1);
    CHECK(r4 != r2);

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW3 撤回分配改写序号；搜索文本的存量回填不分配")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    const int64_t edited = SaveText("rw3-edit", "edit me", 1);
    REQUIRE(Rewrite(mgr, edited, "edited"));
    const int64_t editRev = ReadRev(edited);
    REQUIRE(editRev > 0);

    //撤回：f_rev 取一个比此前所有序号都大的新值
    const int64_t recalled = SaveText("rw3-recall", "recall me", 2);
    CHECK(ReadRev(recalled) == 0);
    REQUIRE(mgr.recallMessage(recalled, kUserA));
    CHECK(ReadRev(recalled) > editRev);

    //按序号同步取出的这一条带着撤回状态与撤回者（B 是收方，同样看得到）
    bool more = false;
    int64_t currev = 0;
    const std::vector<RewrittenMessage> ofB = FetchRewritten(kUserB, editRev, kFetchAll, more, currev);
    REQUIRE(ofB.size() == 1);
    CHECK(ofB[0].record.msgid == recalled);
    CHECK(ofB[0].record.recalled);
    CHECK(ofB[0].record.recalledby == kUserA);

    //存量回填：把一条消息的 f_search_text 置回 NULL（模拟加列之前的存量消息），回填之后搜索文本有了，
    //f_rev 仍为 0 —— 回填不改变消息内容，客户端不需要重新同步它
    const int64_t legacy = SaveText("rw3-legacy", "legacy text", 3);
    {
        std::unique_ptr<IDatabase> pConn(OpenConn());
        std::ostringstream os;
        os << "UPDATE t_chatmsg SET f_search_text = NULL WHERE f_msgid = " << legacy;
        REQUIRE(pConn->execute(os.str().c_str()));
    }
    REQUIRE(SearchTextIsNull(legacy));
    CHECK(mgr.backfillSearchText(kBackfillBatch) >= 1);
    CHECK_FALSE(SearchTextIsNull(legacy));
    CHECK(ReadRev(legacy) == 0);

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW4 1526 请求解析：since 与 limit 的缺省值和边界")
{
    SyncRewrittenRequest req;
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{}", req));
    CHECK(req.since == 0);
    CHECK(req.limit == REWRITTEN_SYNC_LIMIT_MAX);

    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{\"since\":\"1234\",\"limit\":50}", req));
    CHECK(req.since == 1234);
    CHECK(req.limit == 50);

    //since 为负、或不是字符串（数字）时都按 0 处理：协议约定 64 位的值以字符串编码
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{\"since\":\"-5\"}", req));
    CHECK(req.since == 0);
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{\"since\":77}", req));
    CHECK(req.since == 0);

    //limit 不大于 0 时取上限，超过上限时截到上限
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{\"limit\":0}", req));
    CHECK(req.limit == REWRITTEN_SYNC_LIMIT_MAX);
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest("{\"limit\":-3}", req));
    CHECK(req.limit == REWRITTEN_SYNC_LIMIT_MAX);
    std::ostringstream over;
    over << "{\"limit\":" << kOverLimit << "}";
    REQUIRE(MessageProtocol::parseSyncRewrittenRequest(over.str(), req));
    CHECK(req.limit == REWRITTEN_SYNC_LIMIT_MAX);

    //不是 JSON 对象：解析失败
    CHECK_FALSE(MessageProtocol::parseSyncRewrittenRequest("not json", req));
    CHECK_FALSE(MessageProtocol::parseSyncRewrittenRequest("[1,2]", req));
}

TEST_CASE("RW5 只返回序号大于 since 的消息，按序号升序分页，more 与应答的 rev 正确")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //先发 kRewrittenCount 条，再<u>倒序</u>改写：序号顺序与 msgid 顺序相反，确认是按序号而不是按 msgid 排
    std::vector<int64_t> ids;
    for (int i = 0; i < kRewrittenCount; ++i)
    {
        std::ostringstream cid;
        cid << "rw5-" << i;
        ids.push_back(SaveText(cid.str(), "before", i + 1));
    }
    SaveText("rw5-untouched", "never rewritten", kRewrittenCount + 1);
    for (int i = kRewrittenCount - 1; i >= 0; --i)
        REQUIRE(Rewrite(mgr, ids[i], "after"));

    //逐页取完：每页至多 kSmallPage 条，序号严格递增，最后一页 more 为 false
    std::vector<int64_t> seen;
    int64_t since = 0;
    int64_t lastRev = 0;
    for (int page = 0; page < kRewrittenCount; ++page)
    {
        bool more = false;
        int64_t currev = 0;
        std::vector<RewrittenMessage> items = FetchRewritten(kUserA, since, kSmallPage, more, currev);
        REQUIRE_FALSE(items.empty());
        CHECK(static_cast<int>(items.size()) <= kSmallPage);
        for (size_t k = 0; k < items.size(); ++k)
        {
            CHECK(items[k].rev > lastRev);
            lastRev = items[k].rev;
            seen.push_back(items[k].record.msgid);
        }

        //应答的 rev 是本页最后一项的序号，more 原样带出
        size_t trimmed = 0;
        bool moreOut = more;
        Json::Value resp;
        REQUIRE(ParseObject(MessageProtocol::buildSyncRewrittenResponseWithinBudget(
                                0, "ok", kUserA, since, items, moreOut, currev, kNoTrimBudget, trimmed),
                            resp));
        CHECK(trimmed == 0);
        CHECK(resp["rev"].asString() == std::to_string(items.back().rev));
        CHECK(resp["more"].asBool() == more);
        CHECK(resp["items"].size() == items.size());

        since = items.back().rev;
        if (!more)
            break;
    }
    //没被改写过的那条不出现；被改写的按改写先后（即 msgid 倒序）出现
    REQUIRE(static_cast<int>(seen.size()) == kRewrittenCount);
    for (int i = 0; i < kRewrittenCount; ++i)
        CHECK(seen[i] == ids[kRewrittenCount - 1 - i]);

    //空页：since 已是最大序号，没有条目，more 为 false，应答的 rev 等于 since
    bool more = true;
    int64_t currev = 0;
    std::vector<RewrittenMessage> empty = FetchRewritten(kUserA, since, kSmallPage, more, currev);
    CHECK(empty.empty());
    CHECK_FALSE(more);
    size_t trimmed = 0;
    Json::Value resp;
    REQUIRE(ParseObject(MessageProtocol::buildSyncRewrittenResponseWithinBudget(
                            0, "ok", kUserA, since, empty, more, currev, kNoTrimBudget, trimmed),
                        resp));
    CHECK(resp["rev"].asString() == std::to_string(since));
    CHECK_FALSE(resp["more"].asBool());
    CHECK(resp["items"].size() == 0);

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW6 可见范围：本人的单聊、当前所在的群；退群后不再返回；受限可见且不在名单里的不返回")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();
    GroupManager& groups = Singleton<GroupManager>::Instance();

    //A 在 G1 里、不在 G2 里
    std::vector<int64_t> g1Members;
    g1Members.push_back(kUserA);
    g1Members.push_back(kUserE);
    const int64_t g1 = groups.createGroup("rw6-g1", kUserB, g1Members, kBaseTimeMs);
    REQUIRE(g1 > 0);
    std::vector<int64_t> g2Members;
    g2Members.push_back(kUserE);
    const int64_t g2 = groups.createGroup("rw6-g2", kUserB, g2Members, kBaseTimeMs);
    REQUIRE(g2 > 0);

    const int64_t mineSingle = SaveSingleText(kUserB, kUserA, "rw6-ab", 1);
    const int64_t otherSingle = SaveSingleText(kUserC, kUserD, "rw6-cd", 2);
    const int64_t inG1 = SaveGroupText(g1, kUserB, "rw6-g1", 3);
    const int64_t inG2 = SaveGroupText(g2, kUserB, "rw6-g2", 4);
    //G1 里一条只给 E 看的消息（文件共享卡片那一类），A 不在名单里
    std::vector<int32_t> onlyE;
    onlyE.push_back(kUserE);
    const int64_t restricted = SaveGroupText(g1, kUserB, "rw6-g1-restricted", 5, &onlyE);

    REQUIRE(Rewrite(mgr, mineSingle, "x"));
    REQUIRE(Rewrite(mgr, otherSingle, "x"));
    REQUIRE(Rewrite(mgr, inG1, "x"));
    REQUIRE(Rewrite(mgr, inG2, "x"));
    REQUIRE(Rewrite(mgr, restricted, "x"));

    std::vector<int64_t> ofA = FetchRewrittenIds(kUserA);
    CHECK(Contains(ofA, mineSingle));
    CHECK_FALSE(Contains(ofA, otherSingle));
    CHECK(Contains(ofA, inG1));
    CHECK_FALSE(Contains(ofA, inG2));
    CHECK_FALSE(Contains(ofA, restricted));
    CHECK(ofA.size() == 2);

    //名单里的 E 看得到受限的那条
    CHECK(Contains(FetchRewrittenIds(kUserE), restricted));

    //A 退出 G1 之后，G1 里的改写不再返回给 A，单聊照常
    std::vector<int64_t> leaving;
    leaving.push_back(kUserA);
    REQUIRE(groups.removeMembers(g1, leaving));
    ofA = FetchRewrittenIds(kUserA);
    CHECK(Contains(ofA, mineSingle));
    CHECK_FALSE(Contains(ofA, inG1));

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW7 条目与 1502 的消息条目逐字段一致，另带字符串编码的 rev")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //B 发给 A 的一条单聊消息，被改写过一次
    const int64_t msgid = SaveSingleText(kUserB, kUserA, "rw7", 1);
    REQUIRE(Rewrite(mgr, msgid, "rewritten"));

    //1526：以 A 的视角取出
    bool more = false;
    int64_t currev = 0;
    std::vector<RewrittenMessage> items = FetchRewritten(kUserA, 0, kFetchAll, more, currev);
    REQUIRE(items.size() == 1);
    size_t trimmed = 0;
    Json::Value rewritten;
    REQUIRE(ParseObject(MessageProtocol::buildSyncRewrittenResponseWithinBudget(
                            0, "ok", kUserA, 0, items, more, currev, kNoTrimBudget, trimmed),
                        rewritten));
    Json::Value item = rewritten["items"][0];
    REQUIRE(item.isObject());
    CHECK(item["rev"].asString() == std::to_string(items[0].rev));
    CHECK(item["cid"].asString() == std::to_string(kUserB));

    //1502：同一条消息以 A 的视角经同步消息的应答组装（单聊的会话 id 是对方 B）
    SyncMessagesRequest req;
    req.ctype = conversation_single;
    req.cid = kUserB;
    req.limit = kFetchAll;
    std::vector<MessageRecord> synced;
    bool hasMore = false;
    REQUIRE(mgr.getConversationMessages(kUserA, req, synced, hasMore));
    REQUIRE(synced.size() == 1);
    Json::Value syncResp;
    REQUIRE(ParseObject(MessageProtocol::buildSyncMessagesResponse(0, "ok", conversation_single, kUserB, false,
                                                                   synced),
                        syncResp));
    const Json::Value& fromSync = syncResp["messages"][0];
    REQUIRE(fromSync.isObject());

    //去掉 rev 之后两边完全相同
    item.removeMember("rev");
    CHECK(item == fromSync);

    testdb::cleanupDb(kRwDbPath);
}

TEST_CASE("RW8 currev 等于库里当前的最大序号")
{
    testdb::resetDb(kRwDbPath, kRwMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    const int64_t m1 = SaveText("rw8-1", "one", 1);
    const int64_t m2 = SaveText("rw8-2", "two", 2);
    REQUIRE(Rewrite(mgr, m1, "one-edited"));
    REQUIRE(Rewrite(mgr, m2, "two-edited"));
    const int64_t maxRev = ReadRev(m2);

    //与 A 无关的一段单聊里也有改写：currev 不限可见范围，取的是全库最大值
    const int64_t other = SaveSingleText(kUserC, kUserD, "rw8-cd", 3);
    REQUIRE(Rewrite(mgr, other, "x"));
    const int64_t otherRev = ReadRev(other);
    REQUIRE(otherRev > maxRev);

    bool more = false;
    int64_t currev = 0;
    FetchRewritten(kUserA, 0, kFetchAll, more, currev);
    CHECK(currev == otherRev);

    testdb::cleanupDb(kRwDbPath);
}
