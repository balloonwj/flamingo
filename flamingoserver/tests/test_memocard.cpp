/**
 *  群备忘卡片（message_type_memo）的服务端单元测试（Suite MC-S），test_memocard.cpp
 *
 *  对应 docs/备忘与提醒方案.md §7.6 的服务端用例：
 *    MC-S1  卡片内容构造：字段齐全，change 取值正确，text 为「[群备忘] 标题」
 *    MC-S2  （真的打开临时库跑 SQL）只提醒 B 的群备忘：卡片的可见名单为 B 加群主、管理员；
 *           名单外的普通成员 C 同步不到、未读不计、搜索不到
 *    MC-S3  提醒全体成员时卡片全员可见（不限定范围）
 *    MC-S4  （临时库）把提醒对象从 B 改成 C：新卡片 C 看得到、B 看不到；B 仍看得到修改之前那张
 *    MC-S5  个人备忘、删除、保存失败都不发卡片：发卡片的条件是「群备忘保存成功」，由 ChatSessionMemo 的
 *           onMemoSave 在落库成功之后判断。本套用例够不到会话层，只核对其中的纯规则：个人备忘的可见者只有
 *           本人一人（会话层据 ctype 跳过），删除推送与卡片无关；会话层的判断由实机联调 MC-L4 核对
 *    MC-S6  客户端不可操作：不得发送、撤回、置顶，转发时跳过；摘要为「[群备忘] 标题」
 *
 *  典型用法：Tests.exe --test-case="MC-S*"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MemoProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/MessageSearchText.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kMcDbPath = "flamingo_sqlite_memocard_test.db";
const char* const kMcMysqlDb = "flamingo_test_memocard";

//群 G 的成员：群主 O、管理员 D、普通成员 B 与 C
const int32_t kOwnerO = 200001;
const int32_t kAdminD = 200002;
const int32_t kMemberB = 200003;
const int32_t kMemberC = 200004;
const int64_t kGroupG = 268435463LL;

//备忘 id、开始时间（十进制 yyyymmddhhmm）与两次保存的时间（Unix 毫秒）
const int64_t kMemoId = 12;
const int64_t kStart = 202610050930LL;
const int64_t kCreateTimeMs = 1790000000000LL;
const int64_t kModifyTimeMs = 1790000060000LL;

//提前 10 分钟、每周一、永不结束（count 为 0），取值见 §3 的备忘对象
const int kAdvanceMinutes = 10;
const int kWeekdayMonday = 1;
const int kCountForever = 0;

//同步与搜索一次最多取的条数，远大于本套用例里的消息数
const int kFetchLimit = 100;

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

//群 G 的成员与角色
std::vector<MemoMemberRole> GroupMembers()
{
    std::vector<MemoMemberRole> members;
    members.push_back(MemoMemberRole(kOwnerO, group_role_owner));
    members.push_back(MemoMemberRole(kAdminD, group_role_admin));
    members.push_back(MemoMemberRole(kMemberB, group_role_member));
    members.push_back(MemoMemberRole(kMemberC, group_role_member));
    return members;
}

//一条由群主新建、只提醒 target 的群备忘
MemoRecord MakeGroupMemo(int64_t target)
{
    MemoRecord m;
    m.memoid = kMemoId;
    m.ctype = conversation_group;
    m.owner = kGroupG;
    m.creator = kOwnerO;
    m.editor = kOwnerO;
    m.title = u8"周会";
    m.note = u8"带上周报";
    m.start = kStart;
    m.advance = kAdvanceMinutes;
    m.repeat = memo_repeat_weekly;
    m.weekdays = kWeekdayMonday;
    m.count = kCountForever;
    m.style = memo_style_popup;
    m.targetAll = false;
    m.targets.push_back(target);
    m.createTime = kCreateTimeMs;
    m.updateTime = kCreateTimeMs;
    return m;
}

//按 ChatSession::sendMemoCard 的参数把一张卡片落库，返回 msgid
int64_t SaveMemoCard(const MemoRecord& memo, int change)
{
    std::vector<int64_t> viewers;
    MemoProtocol::collectViewers(memo, GroupMembers(), viewers);
    std::vector<int32_t> audience;
    const bool limited = MemoProtocol::memoCardAudience(memo, viewers, audience);

    const std::string content = MemoProtocol::buildMemoCardContent(memo, change, u8"群主");
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        MemoProtocol::memoCardClientMsgId(memo.memoid, memo.updateTime), conversation_group,
        static_cast<int32_t>(memo.editor), static_cast<int32_t>(memo.owner), message_type_memo, stored,
        memo.updateTime, limited ? &audience : NULL);
    REQUIRE(msgid > 0);
    return msgid;
}

//以 userid 的身份同步群 G，返回 msgid 列表（升序）
std::vector<int64_t> SyncIds(int32_t userid)
{
    SyncMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = kGroupG;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationMessages(userid, req, records, hasMore));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//以 userid 的身份在群 G 里按关键字搜索，返回命中条数
size_t SearchCount(int32_t userid, const std::string& keyword)
{
    SearchMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = kGroupG;
    req.keyword = keyword;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(userid, req, records, hasMore));
    return records.size();
}

//userid 在群 G 的未读数；会话摘要里没有群 G 时返回 -1
int UnreadOf(int32_t userid)
{
    std::vector<int64_t> groupids(1, kGroupG);
    std::vector<ConversationBrief> briefs;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(userid, groupids, briefs));
    for (size_t i = 0; i < briefs.size(); ++i)
    {
        if (briefs[i].cid == kGroupG)
            return briefs[i].unread;
    }
    return -1;
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

} // anonymous namespace

TEST_CASE("MC-S1 卡片内容构造：字段齐全，change 取值，text 兜底")
{
    const MemoRecord memo = MakeGroupMemo(kMemberB);
    Json::Value card;
    REQUIRE(ParseObject(MemoProtocol::buildMemoCardContent(memo, memo_card_created, u8"群主"), card));
    CHECK(card["memoid"].asString() == "12");
    CHECK(card["groupid"].asString() == "268435463");
    CHECK(card["change"].asInt() == memo_card_created);
    CHECK(card["title"].asString() == u8"周会");
    CHECK(card["note"].asString() == u8"带上周报");
    CHECK(card["start"].asString() == "2026-10-05 09:30");
    CHECK(card["advance"].asInt() == kAdvanceMinutes);
    CHECK(card["repeat"].asInt() == memo_repeat_weekly);
    CHECK(card["weekdays"].asInt() == kWeekdayMonday);
    CHECK(card["count"].asInt() == kCountForever);
    CHECK_FALSE(card["targetall"].asBool());
    CHECK(card["targetcount"].asInt() == 1);
    CHECK(card["editorid"].asString() == "200001");
    CHECK(card["editor"].asString() == u8"群主");
    CHECK(card["text"].asString() == u8"[群备忘] 周会");
    //卡片只写人数，不带名单
    CHECK_FALSE(card.isMember("targets"));

    Json::Value modified;
    REQUIRE(ParseObject(MemoProtocol::buildMemoCardContent(memo, memo_card_modified, ""), modified));
    CHECK(modified["change"].asInt() == memo_card_modified);
    CHECK(modified["editor"].asString().empty());

    //提醒全体成员时 targetcount 为 0
    MemoRecord all = memo;
    all.targetAll = true;
    all.targets.clear();
    Json::Value allCard;
    REQUIRE(ParseObject(MemoProtocol::buildMemoCardContent(all, memo_card_created, ""), allCard));
    CHECK(allCard["targetall"].asBool());
    CHECK(allCard["targetcount"].asInt() == 0);

    CHECK(MemoProtocol::memoCardClientMsgId(kMemoId, kCreateTimeMs) == "memo-card-12-1790000000000");
}

TEST_CASE("MC-S2 只提醒 B 的群备忘：可见名单为 B 加群主、管理员，C 同步不到、未读不计、搜索不到")
{
    testdb::resetDb(kMcDbPath, kMcMysqlDb);

    const MemoRecord memo = MakeGroupMemo(kMemberB);
    std::vector<int64_t> viewers;
    MemoProtocol::collectViewers(memo, GroupMembers(), viewers);
    std::vector<int32_t> audience;
    REQUIRE(MemoProtocol::memoCardAudience(memo, viewers, audience));
    REQUIRE(audience.size() == 3);
    CHECK(audience[0] == kOwnerO);
    CHECK(audience[1] == kAdminD);
    CHECK(audience[2] == kMemberB);

    const int64_t card = SaveMemoCard(memo, memo_card_created);
    CHECK(SyncIds(kOwnerO) == Ids(card));
    CHECK(SyncIds(kAdminD) == Ids(card));
    CHECK(SyncIds(kMemberB) == Ids(card));
    CHECK(SyncIds(kMemberC).empty());

    //未读：B 与管理员各 1 条，发送者本人 0 条，名单外的 C 不计
    CHECK(UnreadOf(kMemberB) == 1);
    CHECK(UnreadOf(kAdminD) == 1);
    CHECK(UnreadOf(kOwnerO) == 0);
    CHECK(UnreadOf(kMemberC) <= 0);

    //搜索：标题与备注都能命中，C 搜不到
    CHECK(SearchCount(kMemberB, u8"周会") == 1);
    CHECK(SearchCount(kMemberB, u8"周报") == 1);
    CHECK(SearchCount(kMemberC, u8"周会") == 0);

    testdb::cleanupDb(kMcDbPath);
}

TEST_CASE("MC-S3 提醒全体成员：卡片全员可见，不限定范围")
{
    MemoRecord memo = MakeGroupMemo(kMemberB);
    memo.targetAll = true;
    memo.targets.clear();
    std::vector<int64_t> viewers;
    MemoProtocol::collectViewers(memo, GroupMembers(), viewers);
    CHECK(viewers.size() == GroupMembers().size());
    std::vector<int32_t> audience(1, kMemberC);
    CHECK_FALSE(MemoProtocol::memoCardAudience(memo, viewers, audience));
    CHECK(audience.empty());

    testdb::resetDb(kMcDbPath, kMcMysqlDb);
    const int64_t card = SaveMemoCard(memo, memo_card_created);
    CHECK(SyncIds(kMemberC) == Ids(card));
    CHECK(SyncIds(kMemberB) == Ids(card));
    testdb::cleanupDb(kMcDbPath);
}

TEST_CASE("MC-S4 提醒对象从 B 改成 C：新卡片 C 看得到、B 看不到，B 仍看得到修改之前那张")
{
    testdb::resetDb(kMcDbPath, kMcMysqlDb);

    const MemoRecord created = MakeGroupMemo(kMemberB);
    const int64_t first = SaveMemoCard(created, memo_card_created);

    MemoRecord modified = created;
    modified.targets.clear();
    modified.targets.push_back(kMemberC);
    modified.updateTime = kModifyTimeMs;
    const int64_t second = SaveMemoCard(modified, memo_card_modified);
    CHECK(second != first);

    CHECK(SyncIds(kMemberB) == Ids(first));
    CHECK(SyncIds(kMemberC) == Ids(second));
    CHECK(SyncIds(kOwnerO) == Ids(first, second));
    CHECK(SyncIds(kAdminD) == Ids(first, second));

    testdb::cleanupDb(kMcDbPath);
}

TEST_CASE("MC-S5 个人备忘的可见者只有本人，会话层据 ctype 不发卡片")
{
    MemoRecord personal;
    personal.memoid = kMemoId;
    personal.ctype = conversation_single;
    personal.owner = kMemberB;
    personal.editor = kMemberB;
    std::vector<int64_t> viewers;
    MemoProtocol::collectViewers(personal, std::vector<MemoMemberRole>(), viewers);
    REQUIRE(viewers.size() == 1);
    CHECK(viewers[0] == kMemberB);

    //删除推送只带 memoid，与卡片无关
    Json::Value del;
    REQUIRE(ParseObject(MemoProtocol::buildDeleteNotify(kMemoId), del));
    CHECK(del["changetype"].asInt() == memo_change_delete);
    CHECK_FALSE(del.isMember("memo"));
}

TEST_CASE("MC-S6 群备忘卡片客户端不可操作，摘要带标题")
{
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_memo));
    MessageRecord card;
    card.msgtype = message_type_memo;
    CHECK_FALSE(MessageProtocol::isForwardableSource(card));

    const std::string content = MemoProtocol::buildMemoCardContent(MakeGroupMemo(kMemberB), memo_card_created, "");
    CHECK(MessageProtocol::buildMessageDigest(message_type_memo, content) == std::string(u8"[群备忘] 周会"));
    CHECK(MessageProtocol::buildMessageDigest(message_type_memo, "{}") == std::string(u8"[群备忘]"));

    std::vector<ConversationBrief> items(1);
    items[0].ctype = conversation_group;
    items[0].cid = kGroupG;
    items[0].lastmsgid = 1;
    items[0].lastfrom = kOwnerO;
    items[0].lastmsgtype = message_type_memo;
    items[0].lastcontent = content;
    Json::Value root;
    REQUIRE(ParseObject(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items), root));
    CHECK(root["conversations"][0]["lastmsg"]["digestarg"].asString() == u8"周会");

    const std::string text = chatserver::BuildContentSearchText(message_type_memo, content);
    CHECK(text.find(u8"周会") != std::string::npos);
    CHECK(text.find(u8"周报") != std::string::npos);
}
