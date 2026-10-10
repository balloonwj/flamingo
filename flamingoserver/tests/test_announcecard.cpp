/**
 *  群公告卡片（message_type_announcement）的服务端单元测试（Suite AC-S），test_announcecard.cpp
 *
 *  对应 docs/群公告接入方案.md §13.7 的服务端用例：
 *    AC-S1  卡片内容构造：字段齐全，64 位 id 以字符串编码，text 为「[群公告] 标题」，republish 取值正确
 *    AC-S2  已删除形态：没有 body，带 deleted，其余字段与原卡片一致，text 为「[群公告已删除] 标题」
 *    AC-S3  客户端不可操作：不得发送、撤回、置顶，转发时跳过
 *    AC-S4  摘要与搜索文字：摘要为「[群公告] 标题」、会话列表 digestarg 为标题；已删除形态的搜索文字不含正文
 *    AC-S5  存取（真的打开临时库跑 SQL）：按 announcementCardClientMsgId 落库的卡片能按「发布人 + clientmsgid」
 *           找回；改写为已删除形态后，同步拿到的是新内容，正文搜不到、标题仍搜得到；
 *           没有卡片的公告（本功能上线前发布的）查不到任何消息；1326 删除推送带上 cardmsgid 与改写后的卡片
 *
 *  「发布公告时不再插系统提示、改发卡片」这一条在 ChatSession 里，本套用例够不到，由实机联调 AC-L1 核对。
 *
 *  典型用法：Tests.exe --test-case="AC-S*"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/MessageSearchText.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kAcDbPath = "flamingo_sqlite_announcecard_test.db";
const char* const kAcMysqlDb = "flamingo_test_announcecard";

//发布人 A、普通成员 B；群 G。群 id 取一个超过 32 位有符号整数上限一半的值，用于确认按字符串编码
const int32_t kPublisherA = 100001;
const int32_t kMemberB = 100002;
const int64_t kGroupG = 268435463LL;

//公告 id 与发布时间（Unix 毫秒），固定取值保证结果可复现
const int64_t kAnnouncementId = 15;
const int64_t kOldAnnouncementId = 9;
const int64_t kPublishTimeMs = 1785000000000LL;

//优先级「重要」，取值见 group_announcement_severity
const int kSeverityImportant = group_announcement_important;

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

//一条测试用公告：标题、正文都带关键字，搜索用例靠它们区分
GroupAnnouncementRecord MakeAnnouncement()
{
    GroupAnnouncementRecord a;
    a.id = kAnnouncementId;
    a.groupid = kGroupG;
    a.title = u8"v3.2 发布周五上线";
    a.content = u8"请各组提前完成回归测试";
    a.severity = kSeverityImportant;
    a.publisher = kPublisherA;
    a.publishtime = kPublishTimeMs;
    a.publishername = u8"林知夏";
    return a;
}

//把一条卡片 content 以发布人的名义落库，返回 msgid（与 ChatSession::sendAnnouncementCard 的落库参数相同）
int64_t SaveCard(const GroupAnnouncementRecord& a, const std::string& content)
{
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        GroupProtocol::announcementCardClientMsgId(a.groupid, a.id), conversation_group,
        static_cast<int32_t>(a.publisher), static_cast<int32_t>(a.groupid),
        message_type_announcement, stored, a.publishtime);
    REQUIRE(msgid > 0);
    return msgid;
}

//以 userid 的身份同步群 G，返回全部消息
std::vector<MessageRecord> SyncGroup(int32_t userid)
{
    SyncMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = kGroupG;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationMessages(userid, req, records, hasMore));
    return records;
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

} // anonymous namespace

TEST_CASE("AC-S1 卡片内容构造：字段齐全，id 按字符串编码，text 兜底，republish 取值")
{
    const GroupAnnouncementRecord a = MakeAnnouncement();

    Json::Value card;
    REQUIRE(ParseObject(GroupProtocol::buildAnnouncementCardContent(a, false), card));
    CHECK(card["announcementid"].isString());
    CHECK(card["announcementid"].asString() == "15");
    CHECK(card["groupid"].asString() == "268435463");
    CHECK(card["title"].asString() == u8"v3.2 发布周五上线");
    CHECK(card["body"].asString() == u8"请各组提前完成回归测试");
    CHECK(card["severity"].asInt() == kSeverityImportant);
    CHECK(card["publisherid"].asString() == "100001");
    CHECK(card["publisher"].asString() == u8"林知夏");
    CHECK(card["publishtime"].asInt64() == kPublishTimeMs);
    CHECK(card["republish"].isBool());
    CHECK_FALSE(card["republish"].asBool());
    CHECK(card["text"].asString() == u8"[群公告] v3.2 发布周五上线");
    CHECK_FALSE(card.isMember("deleted"));

    Json::Value republished;
    REQUIRE(ParseObject(GroupProtocol::buildAnnouncementCardContent(a, true), republished));
    CHECK(republished["republish"].asBool());

    //发布人昵称取不到时照写空串，由客户端按成员表补
    GroupAnnouncementRecord noName = a;
    noName.publishername.clear();
    Json::Value anonymous;
    REQUIRE(ParseObject(GroupProtocol::buildAnnouncementCardContent(noName, false), anonymous));
    CHECK(anonymous["publisher"].asString().empty());

    CHECK(GroupProtocol::announcementCardClientMsgId(kGroupG, kAnnouncementId) == "announce-card-268435463-15");
}

TEST_CASE("AC-S2 已删除形态：去掉正文、带 deleted，其余字段不变")
{
    const std::string original = GroupProtocol::buildAnnouncementCardContent(MakeAnnouncement(), true);
    std::string deletedText;
    REQUIRE(GroupProtocol::buildDeletedAnnouncementCardContent(original, deletedText));

    Json::Value before;
    Json::Value after;
    REQUIRE(ParseObject(original, before));
    REQUIRE(ParseObject(deletedText, after));
    CHECK_FALSE(after.isMember("body"));
    CHECK(after["deleted"].asBool());
    CHECK(after["text"].asString() == u8"[群公告已删除] v3.2 发布周五上线");

    const char* const kKeptFields[] = { "announcementid", "groupid", "title", "severity", "publisherid",
                                        "publisher", "publishtime", "republish" };
    for (size_t i = 0; i < sizeof(kKeptFields) / sizeof(kKeptFields[0]); ++i)
    {
        INFO("field: " << kKeptFields[i]);
        CHECK(after[kKeptFields[i]] == before[kKeptFields[i]]);
    }

    //不是 JSON 对象时失败，出参不动
    std::string untouched = "keep";
    CHECK_FALSE(GroupProtocol::buildDeletedAnnouncementCardContent("not json", untouched));
    CHECK_FALSE(GroupProtocol::buildDeletedAnnouncementCardContent("[1,2]", untouched));
    CHECK(untouched == "keep");
}

TEST_CASE("AC-S3 群公告卡片客户端不可操作：不得发送、撤回、置顶，转发时跳过")
{
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_announcement));

    MessageRecord card;
    card.msgtype = message_type_announcement;
    CHECK_FALSE(MessageProtocol::isForwardableSource(card));

    //相邻的普通类型不受影响
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_text));
}

TEST_CASE("AC-S4 摘要与搜索文字：摘要带标题，digestarg 为标题，已删除后正文搜不到")
{
    const std::string content = GroupProtocol::buildAnnouncementCardContent(MakeAnnouncement(), false);
    CHECK(MessageProtocol::buildMessageDigest(message_type_announcement, content)
          == std::string(u8"[群公告] v3.2 发布周五上线"));
    //内容解析失败时只写类型
    CHECK(MessageProtocol::buildMessageDigest(message_type_announcement, "not json") == std::string(u8"[群公告]"));

    //会话列表 lastmsg 带 digestarg
    std::vector<ConversationBrief> items(1);
    items[0].ctype = conversation_group;
    items[0].cid = kGroupG;
    items[0].lastmsgid = 1;
    items[0].lastfrom = kPublisherA;
    items[0].lastmsgtype = message_type_announcement;
    items[0].lastcontent = content;
    Json::Value root;
    REQUIRE(ParseObject(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items), root));
    const Json::Value& last = root["conversations"][0]["lastmsg"];
    CHECK(last["digest"].asString() == u8"[群公告] v3.2 发布周五上线");
    CHECK(last["digestarg"].asString() == u8"v3.2 发布周五上线");

    //搜索文字：正常形态含标题与正文，已删除形态只剩标题
    const std::string text = chatserver::BuildContentSearchText(message_type_announcement, content);
    CHECK(text.find(u8"v3.2 发布周五上线") != std::string::npos);
    CHECK(text.find(u8"回归测试") != std::string::npos);
    std::string deleted;
    REQUIRE(GroupProtocol::buildDeletedAnnouncementCardContent(content, deleted));
    const std::string deletedText = chatserver::BuildContentSearchText(message_type_announcement, deleted);
    CHECK(deletedText.find(u8"v3.2 发布周五上线") != std::string::npos);
    CHECK(deletedText.find(u8"回归测试") == std::string::npos);
}

TEST_CASE("AC-S5 存取：卡片按发布人与 clientmsgid 找回，改写后同步拿到新内容，正文搜不到")
{
    testdb::resetDb(kAcDbPath, kAcMysqlDb);
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();

    const GroupAnnouncementRecord a = MakeAnnouncement();
    const std::string content = GroupProtocol::buildAnnouncementCardContent(a, false);
    const int64_t cardMsgId = SaveCard(a, content);

    //全员可见：发布人与普通成员同步都拿得到，内容就是卡片本体
    std::vector<MessageRecord> ofB = SyncGroup(kMemberB);
    REQUIRE(ofB.size() == 1);
    CHECK(ofB[0].msgid == cardMsgId);
    CHECK(ofB[0].msgtype == message_type_announcement);
    CHECK(ofB[0].from == kPublisherA);
    CHECK(ofB[0].content == content);
    CHECK(SyncGroup(kPublisherA).size() == 1);
    CHECK(SearchCount(kMemberB, u8"回归测试") == 1);

    //按发布人 + clientmsgid 找回；换成别人或别的公告 id 都找不到
    const std::string clientMsgId = GroupProtocol::announcementCardClientMsgId(a.groupid, a.id);
    CHECK(msgMgr.getMsgIdByClientId(kPublisherA, clientMsgId) == cardMsgId);
    CHECK(msgMgr.getMsgIdByClientId(kMemberB, clientMsgId) == 0);
    CHECK(msgMgr.getMsgIdByClientId(
              kPublisherA, GroupProtocol::announcementCardClientMsgId(a.groupid, kOldAnnouncementId)) == 0);

    //改写为已删除形态（与 ChatSession::rewriteAnnouncementCardAsDeleted 的步骤相同）
    MessageRecord stored;
    REQUIRE(msgMgr.getMessageById(cardMsgId, stored));
    std::string deleted;
    REQUIRE(GroupProtocol::buildDeletedAnnouncementCardContent(stored.content, deleted));
    REQUIRE(msgMgr.updateMessageContent(
        cardMsgId, MessageProtocol::buildStoredContent(deleted, std::vector<int64_t>(), false, "")));

    ofB = SyncGroup(kMemberB);
    REQUIRE(ofB.size() == 1);
    Json::Value synced;
    REQUIRE(ParseObject(ofB[0].content, synced));
    CHECK(synced["deleted"].asBool());
    CHECK_FALSE(synced.isMember("body"));
    CHECK(SearchCount(kMemberB, u8"回归测试") == 0);
    CHECK(SearchCount(kMemberB, u8"周五上线") == 1);

    //1326 删除推送带上卡片：cardmsgid 按字符串编码，card 为改写后的对象；发布推送不带
    Json::Value notify;
    REQUIRE(ParseObject(GroupProtocol::buildAnnouncementNotify(a, group_announcement_change_delete, cardMsgId, deleted),
                        notify));
    CHECK(notify["cardmsgid"].asString() == std::to_string(cardMsgId));
    CHECK(notify["card"]["deleted"].asBool());
    CHECK(notify["announcement"]["id"].asString() == "15");
    Json::Value publish;
    REQUIRE(ParseObject(GroupProtocol::buildAnnouncementNotify(a, group_announcement_change_publish), publish));
    CHECK_FALSE(publish.isMember("cardmsgid"));
    CHECK_FALSE(publish.isMember("card"));

    testdb::cleanupDb(kAcDbPath);
}
