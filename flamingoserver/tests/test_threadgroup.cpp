/**
 *  帖子群服务端单元测试（Suite TG），test_threadgroup.cpp
 *  覆盖 docs/帖子群方案.md 第 8 节的服务端用例（2026-10-07 按用户要求自行确定）：
 *    纯逻辑（ThreadProtocol / ThreadWatchRegistry）：
 *    - TG1 内容校验：标题、正文按字符计的上限，图片数，空内容，补充不能有标题，留言不能有图片，@ 人数；
 *    - TG2 互动编号；TG3 UTF-8 计数、截取与摘要；TG4 权限；TG5 帖子动态卡片的可见者；TG6 @ 只保留本群成员；
 *    - TG7 游标；TG8 请求解析；TG9 内容 JSON 往返（含 4 字节字符）；TG10 卡片内容；TG11「正在查看」登记表；
 *    - TG12 卡片的会话摘要（MessageProtocol::buildMessageDigest）与会话列表 lastmsg 里带的参数（第 3c 期）；
 *    - 第 5 期（docs/帖子群方案.md 第 14 节，2026-10-08 用户授权自行确定用例）：TG13 正文的 @ 片段；TG14 卡片带图片；
 *      TG15 动态卡片的已删除形态与摘要；TG16 2716 请求、筛选与 2716 / 2721 组包；
 *    存储（ThreadManager，临时库）：
 *    - TG-S1 群类型：建帖子群写入 1，读群资料带回；
 *    - TG-S2 发帖、读回、去重、修改、置顶计数；
 *    - TG-S3 列表：置顶在前、最新动态排序随留言变化、两种游标翻页不重复不遗漏；
 *    - TG-S4 留言与回复：计数、最新动态时间、留言人名单、两级挂靠；
 *    - TG-S5 删除留言：有回复时保留占位，最后一条回复删除后占位一并删除；
 *    - TG-S6 互动：重复添加只算一次、取消、多种并存、mine、名单；
 *    - TG-S7 补充：新增刷新最新动态、按时间排列、计数、删除；
 *    - TG-S8 删帖级联删除补充、留言、互动；
 *    - TG-S9 解散群删除帖子群四张表的数据（GroupManager::dismissGroup）；
 *    - TG-S10 留言与补充记下动态卡片 msgid，删帖前取得出、删帖后不再有（第 5 期）；
 *    - TG-S11 2716 的取数与筛选：只得到本群、本人看得到、已改写为已删除形态的卡片（第 5 期）。
 *    消息改写离线同步（docs/消息改写离线同步方案.md，其余用例 RW1 ~ RW8 在 test_rewrittensync.cpp）：
 *    - RW9 编辑帖子：卡片被改写并分配改写序号，推给群成员的 2721 带新标题、新摘要，按改写序号同步也取得到。
 *  balloonwj@qq.com   2026-10-07
 */
#include "doctest.h"
#include "../chatserversrc/ThreadProtocol.h"
#include "../chatserversrc/ThreadManager.h"
#include "../chatserversrc/ThreadWatchRegistry.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../jsoncpp1.9.0/json.h"
#include "test_dbbackend.h"

#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

//本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）
const char* const kTgSqliteFile = "flamingo_sqlite_thread_test.db";
const char* const kTgMysqlDb = "flamingo_test_thread";

//测试用户与帖子群
const int64_t kAuthor = 4101;
const int64_t kAdminU = 4102;
const int64_t kMemberU = 4103;
const int64_t kOtherU = 4104;
const int64_t kThreadGroup = 268436601LL;
const int64_t kOtherGroup = 268436602LL;

//基准时间戳（Unix 毫秒）
const int64_t kTgTime = 1791000000000LL;

//把文本解析成 JSON 对象，失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//把 JSON 对象写回紧凑文本（用于把改过的卡片 content 再交给被测函数）
std::string writeCompactForTest(const Json::Value& v)
{
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, v);
}

//生成 n 个汉字（每个 3 字节）
std::string hanzi(int n)
{
    std::string s;
    for (int i = 0; i < n; ++i)
        s += u8"字";
    return s;
}

//一张合法的图片
ThreadImage image(const char* fileid)
{
    ThreadImage img;
    img.fileid = fileid;
    img.md5 = "0123456789abcdef0123456789abcdef";
    img.width = 640;
    img.height = 480;
    img.size = 12345;
    return img;
}

//一份只有正文的内容
ThreadBody textBody(const std::string& text)
{
    ThreadBody b;
    b.text = text;
    return b;
}

//重建临时库
void resetThreadDb()
{
    testdb::resetDb(kTgSqliteFile, kTgMysqlDb);
}

//写一条帖子并返回它
ThreadPost addPost(ThreadManager& mgr, int64_t groupid, int64_t author, const std::string& clientId, int64_t time)
{
    ThreadPost p;
    p.groupid = groupid;
    p.author = author;
    p.clientPostId = clientId;
    p.body.title = u8"标题" + clientId;
    p.body.text = u8"正文" + clientId;
    p.createTime = time;
    p.activeTime = time;
    REQUIRE(mgr.insertPost(p));
    REQUIRE(p.postid > 0);
    return p;
}

//写一条留言或回复并返回它
ThreadComment addComment(ThreadManager& mgr, const ThreadPost& post, int64_t author, int64_t parentid, int64_t replyTo,
                         const std::string& clientId, int64_t time)
{
    ThreadComment c;
    c.postid = post.postid;
    c.groupid = post.groupid;
    c.author = author;
    c.parentid = parentid;
    c.replyTo = replyTo;
    c.clientCommentId = clientId;
    c.body.text = u8"留言" + clientId;
    c.createTime = time;
    REQUIRE(mgr.insertComment(c));
    REQUIRE(c.commentid > 0);
    return c;
}

//统计某张表里某个条件的行数
int64_t countRows(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    QueryResult* pResult = pConn->query(sql.c_str());
    REQUIRE(pResult != NULL);
    int64_t value = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return value;
}

} // namespace

TEST_CASE("TG1 内容校验：长度、图片数、空内容与各种类的限制")
{
    ThreadBody post = textBody(hanzi(kThreadPostTextMaxChars));
    post.title = hanzi(kThreadTitleMaxChars);
    CHECK(ThreadProtocol::validateBody(post, ThreadProtocol::kBodyPost) == error_code_ok);
    post.title = hanzi(kThreadTitleMaxChars + 1);
    CHECK(ThreadProtocol::validateBody(post, ThreadProtocol::kBodyPost) == error_code_thread_invalid);
    post.title.clear();
    post.text = hanzi(kThreadPostTextMaxChars + 1);
    CHECK(ThreadProtocol::validateBody(post, ThreadProtocol::kBodyPost) == error_code_thread_invalid);

    //只有图片也算有内容；图片最多 9 张
    ThreadBody pics;
    for (int i = 0; i < kThreadMaxImages; ++i)
        pics.images.push_back(image("100"));
    CHECK(ThreadProtocol::validateBody(pics, ThreadProtocol::kBodyPost) == error_code_ok);
    pics.images.push_back(image("101"));
    CHECK(ThreadProtocol::validateBody(pics, ThreadProtocol::kBodyPost) == error_code_thread_invalid);
    ThreadBody noId;
    noId.images.push_back(image(""));
    CHECK(ThreadProtocol::validateBody(noId, ThreadProtocol::kBodyPost) == error_code_thread_invalid);

    //空内容：没有文字也没有图片，或者只有空白（含全角空格）
    CHECK(ThreadProtocol::validateBody(textBody(""), ThreadProtocol::kBodyPost) == error_code_thread_invalid);
    CHECK(ThreadProtocol::validateBody(textBody(u8" \n\t　"), ThreadProtocol::kBodyPost)
          == error_code_thread_invalid);

    //补充：不能有标题，正文 2000 字
    ThreadBody add = textBody(hanzi(kThreadAddendumTextMaxChars));
    CHECK(ThreadProtocol::validateBody(add, ThreadProtocol::kBodyAddendum) == error_code_ok);
    add.title = "t";
    CHECK(ThreadProtocol::validateBody(add, ThreadProtocol::kBodyAddendum) == error_code_thread_invalid);
    add.title.clear();
    add.text = hanzi(kThreadAddendumTextMaxChars + 1);
    CHECK(ThreadProtocol::validateBody(add, ThreadProtocol::kBodyAddendum) == error_code_thread_invalid);

    //留言：不能有图片，正文 1000 字
    ThreadBody comment = textBody(hanzi(kThreadCommentTextMaxChars));
    CHECK(ThreadProtocol::validateBody(comment, ThreadProtocol::kBodyComment) == error_code_ok);
    comment.images.push_back(image("1"));
    CHECK(ThreadProtocol::validateBody(comment, ThreadProtocol::kBodyComment) == error_code_thread_invalid);
    comment.images.clear();
    comment.text = hanzi(kThreadCommentTextMaxChars + 1);
    CHECK(ThreadProtocol::validateBody(comment, ThreadProtocol::kBodyComment) == error_code_thread_invalid);

    //@ 人数上限
    ThreadBody many = textBody("hi");
    for (int i = 0; i <= kThreadMaxMentions; ++i)
        many.mentions.push_back(1000 + i);
    CHECK(ThreadProtocol::validateBody(many, ThreadProtocol::kBodyPost) == error_code_thread_invalid);
    CHECK(ThreadProtocol::validateBody(textBody("hi"), 99) == error_code_thread_invalid);
}

TEST_CASE("TG2 互动编号：0 为赞，1000~1999 为表情回应")
{
    CHECK(ThreadProtocol::isValidReaction(kThreadReactionLike));
    CHECK(ThreadProtocol::isValidReaction(kThreadReactionFaceBase));
    CHECK(ThreadProtocol::isValidReaction(kThreadReactionFaceBase + 142));
    CHECK(ThreadProtocol::isValidReaction(kThreadReactionFaceBase + kThreadMaxFaceId));
    CHECK_FALSE(ThreadProtocol::isValidReaction(-1));
    CHECK_FALSE(ThreadProtocol::isValidReaction(1));
    CHECK_FALSE(ThreadProtocol::isValidReaction(999));
    CHECK_FALSE(ThreadProtocol::isValidReaction(kThreadReactionFaceBase + kThreadMaxFaceId + 1));
}

TEST_CASE("TG3 UTF-8 计数、截取与摘要")
{
    CHECK(ThreadProtocol::utf8Chars(u8"中文ab") == 4);
    CHECK(ThreadProtocol::utf8Chars("") == 0);
    bool cut = false;
    CHECK(ThreadProtocol::utf8Prefix(u8"中文ab", 2, cut) == u8"中文");
    CHECK(cut);
    CHECK(ThreadProtocol::utf8Prefix(u8"中文", 5, cut) == u8"中文");
    CHECK_FALSE(cut);
    //4 字节字符不被切断
    CHECK(ThreadProtocol::utf8Prefix(u8"a\U0001F600b", 2, cut) == u8"a\U0001F600");
    CHECK(ThreadProtocol::makeDigest("  line1\n\n  line2 \t end  ", 100) == "line1 line2 end");
    CHECK(ThreadProtocol::makeDigest(u8"一二三四五", 3) == u8"一二三");
}

TEST_CASE("TG4 权限：修改、删除、置顶、追加补充")
{
    CHECK(ThreadProtocol::canEditPost(kAuthor, kAuthor));
    CHECK_FALSE(ThreadProtocol::canEditPost(kAuthor, kAdminU));
    CHECK(ThreadProtocol::canDeleteContent(kAuthor, kAuthor, group_role_member));
    CHECK(ThreadProtocol::canDeleteContent(kAuthor, kAdminU, group_role_admin));
    CHECK(ThreadProtocol::canDeleteContent(kAuthor, kOtherU, group_role_owner));
    CHECK_FALSE(ThreadProtocol::canDeleteContent(kAuthor, kMemberU, group_role_member));
    CHECK(ThreadProtocol::canPin(group_role_admin));
    CHECK(ThreadProtocol::canPin(group_role_owner));
    CHECK_FALSE(ThreadProtocol::canPin(group_role_member));
    CHECK(ThreadProtocol::canAddAddendum(kAuthor, kAuthor));
    CHECK_FALSE(ThreadProtocol::canAddAddendum(kAuthor, kAdminU));
}

TEST_CASE("TG5 帖子动态卡片的可见者")
{
    std::vector<int64_t> out;
    const std::vector<int64_t> none;
    //留言：楼主；楼主自己留言时为空
    ThreadProtocol::replyCardAudience(thread_reply_comment, kAuthor, 0, none, none, kMemberU, out);
    CHECK(out == std::vector<int64_t>(1, kAuthor));
    ThreadProtocol::replyCardAudience(thread_reply_comment, kAuthor, 0, none, none, kAuthor, out);
    CHECK(out.empty());
    //回复：楼主与被回复的人，去掉回复者本人
    ThreadProtocol::replyCardAudience(thread_reply_reply, kAuthor, kMemberU, none, none, kOtherU, out);
    std::vector<int64_t> expect;
    expect.push_back(kAuthor);
    expect.push_back(kMemberU);
    CHECK(out == expect);
    ThreadProtocol::replyCardAudience(thread_reply_reply, kAuthor, kAuthor, none, none, kMemberU, out);
    CHECK(out == std::vector<int64_t>(1, kAuthor));
    //补充：留过言的人，去掉楼主本人，另加 @ 的人，去重
    std::vector<int64_t> commenters;
    commenters.push_back(kMemberU);
    commenters.push_back(kAuthor);
    commenters.push_back(kMemberU);
    std::vector<int64_t> mentions(1, kOtherU);
    ThreadProtocol::replyCardAudience(thread_reply_addendum, kAuthor, 0, commenters, mentions, kAuthor, out);
    expect.clear();
    expect.push_back(kMemberU);
    expect.push_back(kOtherU);
    CHECK(out == expect);
}

TEST_CASE("TG6 @ 只保留本群成员并去重")
{
    std::vector<int64_t> mentions;
    mentions.push_back(kMemberU);
    mentions.push_back(9999);
    mentions.push_back(kMemberU);
    mentions.push_back(kAdminU);
    std::vector<int64_t> members;
    members.push_back(kAuthor);
    members.push_back(kAdminU);
    members.push_back(kMemberU);
    ThreadProtocol::filterMentions(mentions, members);
    std::vector<int64_t> expect;
    expect.push_back(kMemberU);
    expect.push_back(kAdminU);
    CHECK(mentions == expect);
}

TEST_CASE("TG7 游标往返与非法游标")
{
    int64_t t = 0;
    int64_t id = 0;
    CHECK(ThreadProtocol::parseActivityCursor(ThreadProtocol::makeActivityCursor(kTgTime, 77), t, id));
    CHECK(t == kTgTime);
    CHECK(id == 77);
    CHECK_FALSE(ThreadProtocol::parseActivityCursor("a123", t, id));
    CHECK_FALSE(ThreadProtocol::parseActivityCursor("p77", t, id));
    CHECK(ThreadProtocol::parsePublishCursor(ThreadProtocol::makePublishCursor(88), id));
    CHECK(id == 88);
    CHECK_FALSE(ThreadProtocol::parsePublishCursor("px", id));
    CHECK_FALSE(ThreadProtocol::parsePublishCursor("", id));
}

TEST_CASE("TG8 请求解析")
{
    ListPostsRequest list;
    CHECK(ThreadProtocol::parseListPostsRequest("{\"groupid\":\"268436601\"}", list));
    CHECK(list.groupid == kThreadGroup);
    CHECK(list.sort == thread_sort_activity);
    CHECK(list.limit == kThreadDefaultPageSize);
    CHECK(ThreadProtocol::parseListPostsRequest("{\"groupid\":\"268436601\",\"sort\":2,\"limit\":500}", list));
    CHECK(list.sort == thread_sort_publish);
    CHECK(list.limit == kThreadMaxPageSize);
    CHECK_FALSE(ThreadProtocol::parseListPostsRequest("{\"groupid\":\"268436601\",\"sort\":3}", list));
    CHECK_FALSE(ThreadProtocol::parseListPostsRequest("{}", list));

    PostWriteRequest pub;
    CHECK_FALSE(ThreadProtocol::parsePublishPostRequest("{\"groupid\":\"268436601\",\"text\":\"a\"}", pub));
    CHECK(ThreadProtocol::parsePublishPostRequest(
        "{\"groupid\":\"268436601\",\"clientpostid\":\"c1\",\"title\":\"t\",\"text\":\"a\","
        "\"images\":[{\"fileid\":\"12\",\"md5\":\"m\",\"width\":1,\"height\":2,\"size\":3}],\"mentions\":[\"4103\",4104]}",
        pub));
    CHECK(pub.clientId == "c1");
    CHECK(pub.body.images.size() == 1);
    CHECK(pub.body.images[0].fileid == "12");
    CHECK(pub.body.mentions.size() == 2);
    CHECK_FALSE(ThreadProtocol::parsePublishPostRequest(
        "{\"groupid\":\"268436601\",\"clientpostid\":\"c1\",\"mentions\":[\"x\"]}", pub));

    AddCommentRequest add;
    CHECK(ThreadProtocol::parseAddCommentRequest("{\"postid\":\"5\",\"clientcommentid\":\"k\",\"text\":\"hi\"}", add));
    CHECK(add.parentid == 0);
    CHECK(ThreadProtocol::parseAddCommentRequest(
        "{\"postid\":\"5\",\"parentid\":\"9\",\"clientcommentid\":\"k\",\"text\":\"hi\"}", add));
    CHECK(add.parentid == 9);
    CHECK_FALSE(ThreadProtocol::parseAddCommentRequest(
        "{\"postid\":\"5\",\"clientcommentid\":\"k\",\"text\":\"hi\",\"images\":[]}", add));

    ReactRequest react;
    CHECK(ThreadProtocol::parseReactRequest("{\"targettype\":1,\"targetid\":\"5\",\"reaction\":0,\"on\":1}", react));
    CHECK(react.on);
    CHECK_FALSE(ThreadProtocol::parseReactRequest("{\"targettype\":3,\"targetid\":\"5\",\"reaction\":0,\"on\":1}", react));

    int64_t gid = 0;
    bool watch = false;
    CHECK(ThreadProtocol::parseWatchRequest("{\"groupid\":\"268436601\",\"watch\":1}", gid, watch));
    CHECK(watch);
}

TEST_CASE("TG9 内容 JSON 往返（含 4 字节字符、图片与 @）")
{
    ThreadBody b;
    b.title = u8"标题\U0001F600";
    b.text = u8"正文 it's \"quoted\"";
    b.images.push_back(image("321"));
    b.mentions.push_back(kMemberU);
    ThreadBody back;
    REQUIRE(ThreadProtocol::bodyFromJson(ThreadProtocol::bodyToJson(b), back));
    CHECK(back.title == b.title);
    CHECK(back.text == b.text);
    REQUIRE(back.images.size() == 1);
    CHECK(back.images[0].fileid == "321");
    CHECK(back.images[0].width == 640);
    CHECK(back.mentions == b.mentions);
    CHECK_FALSE(ThreadProtocol::bodyFromJson("not json", back));
    CHECK(back.text.empty());
}

TEST_CASE("TG10 卡片内容与 clientmsgid")
{
    ThreadPost p;
    p.postid = 55;
    p.body.title = u8"周五团建";
    p.body.text = u8"报名\n截止周四";
    p.body.images.push_back(image("1"));
    Json::Value card = parseJson(ThreadProtocol::buildPostCardContent(p));
    CHECK(card["postid"].asString() == "55");
    CHECK(card["title"].asString() == u8"周五团建");
    CHECK(card["digest"].asString() == u8"报名 截止周四");
    CHECK(card["imagecount"].asInt() == 1);
    //没有 @ 人时不带 mentions；@ 了人时带上（客户端据此给被 @ 的人 @ 提醒，2026-10-07 实机联调发现缺失）
    CHECK_FALSE(card.isMember("mentions"));
    p.body.mentions.push_back(5308);
    Json::Value mentioned = parseJson(ThreadProtocol::buildPostCardContent(p));
    REQUIRE(mentioned["mentions"].size() == 1);
    CHECK(mentioned["mentions"][0].asString() == "5308");
    std::string mentionedDeleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(writeCompactForTest(mentioned), mentionedDeleted));
    CHECK_FALSE(parseJson(mentionedDeleted).isMember("mentions"));
    p.body.mentions.clear();

    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(ThreadProtocol::buildPostCardContent(p), deleted));
    Json::Value d = parseJson(deleted);
    CHECK(d["deleted"].asInt() == 1);
    CHECK_FALSE(d.isMember("title"));
    CHECK_FALSE(d.isMember("digest"));
    CHECK(d["postid"].asString() == "55");
    CHECK_FALSE(ThreadProtocol::buildDeletedPostCardContent("[1]", deleted));

    Json::Value reply = parseJson(ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"我也去",
                                                                       thread_reply_reply));
    CHECK(reply["commentid"].asString() == "66");
    CHECK(reply["addid"].asString() == "0");
    CHECK(reply["kind"].asInt() == thread_reply_reply);
    CHECK_FALSE(reply.isMember("mentions"));
    std::vector<int64_t> replyMentions(1, 5127);
    Json::Value replyAt = parseJson(ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"我也去",
                                                                         thread_reply_reply, replyMentions));
    REQUIRE(replyAt["mentions"].size() == 1);
    CHECK(replyAt["mentions"][0].asString() == "5127");
    CHECK(ThreadProtocol::postCardClientMsgId(55) == "threadpost-55");
    CHECK(ThreadProtocol::commentCardClientMsgId(66) == "threadcomment-66");
    CHECK(ThreadProtocol::addendumCardClientMsgId(77) == "threadadd-77");

    //没有标题时，提醒里的「帖子标题」取正文摘要
    p.body.title.clear();
    CHECK(ThreadProtocol::displayTitle(p) == u8"报名 截止周四");
}

TEST_CASE("TG12 卡片的会话摘要与会话列表里带的参数")
{
    ThreadPost p;
    p.postid = 55;
    p.body.title = u8"周五团建";
    p.body.text = u8"报名\n截止周四";
    const std::string postCard = ThreadProtocol::buildPostCardContent(p);
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_post, postCard) == u8"[帖子] 周五团建");
    //没有标题：取正文摘要；已删除：只写类型
    p.body.title.clear();
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_post, ThreadProtocol::buildPostCardContent(p))
          == u8"[帖子] 报名 截止周四");
    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(postCard, deleted));
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_post, deleted) == u8"[帖子]");

    //动态卡片按 kind 写类型，接内容摘要；内容为空时接帖子标题
    const std::string comment = ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"我也去", thread_reply_comment);
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply, comment) == u8"[留言] 我也去");
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply,
                                              ThreadProtocol::buildReplyCardContent(55, 67, 0, u8"周五团建", u8"可以",
                                                                                    thread_reply_reply))
          == u8"[回复] 可以");
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply,
                                              ThreadProtocol::buildReplyCardContent(55, 0, 77, u8"周五团建", "",
                                                                                    thread_reply_addendum))
          == u8"[补充] 周五团建");

    //会话列表的 lastmsg 带 digestarg、threadpostid、threadkind、threaddeleted
    std::vector<ConversationBrief> items;
    ConversationBrief b;
    b.ctype = conversation_group;
    b.cid = 600006;
    b.lastmsgtype = message_type_thread_reply;
    b.lastcontent = comment;
    items.push_back(b);
    b.cid = 600007;
    b.lastmsgtype = message_type_thread_post;
    b.lastcontent = deleted;
    items.push_back(b);
    Json::Value root = parseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items));
    REQUIRE(root["conversations"].size() == 2);
    const Json::Value& last0 = root["conversations"][0]["lastmsg"];
    CHECK(last0["digest"].asString() == u8"[留言] 我也去");
    CHECK(last0["digestarg"].asString() == u8"我也去");
    CHECK(last0["threadpostid"].asString() == "55");
    CHECK(last0["threadkind"].asInt() == thread_reply_comment);
    const Json::Value& last1 = root["conversations"][1]["lastmsg"];
    CHECK(last1["threaddeleted"].asInt() == 1);
    CHECK_FALSE(last1.isMember("digestarg"));
    CHECK(last1["threadpostid"].asString() == "55");
}

TEST_CASE("TG11 正在查看登记表")
{
    ThreadWatchRegistry reg;
    reg.watch(kThreadGroup, 1, kAuthor);
    reg.watch(kThreadGroup, 2, kMemberU);
    reg.watch(kThreadGroup, 2, kMemberU);
    reg.watch(kOtherGroup, 2, kMemberU);
    CHECK(reg.watchers(kThreadGroup).size() == 2);
    reg.unwatch(kThreadGroup, 1);
    CHECK(reg.watchers(kThreadGroup) == std::vector<uint64_t>(1, 2));
    CHECK(reg.removeConn(2) == 2);
    CHECK(reg.watchers(kThreadGroup).empty());
    CHECK(reg.watchers(kOtherGroup).empty());

    reg.watch(kThreadGroup, 3, kAuthor);
    reg.watch(kThreadGroup, 4, kMemberU);
    CHECK(reg.removeUsers(kThreadGroup, std::vector<int64_t>(1, kMemberU)) == 1);
    CHECK(reg.watchers(kThreadGroup) == std::vector<uint64_t>(1, 3));
    reg.removeGroup(kThreadGroup);
    CHECK(reg.watchers(kThreadGroup).empty());
}

TEST_CASE("TG-S1 群类型：建帖子群写入 1，读群资料带回")
{
    resetThreadDb();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    REQUIRE(groupMgr.init());
    const int64_t normal = groupMgr.createGroup(u8"普通群", static_cast<int32_t>(kAuthor),
                                                std::vector<int64_t>(1, kMemberU), kTgTime);
    const int64_t thread = groupMgr.createGroup(u8"帖子群", static_cast<int32_t>(kAuthor),
                                                std::vector<int64_t>(1, kMemberU), kTgTime, group_type_thread);
    REQUIRE(normal > 0);
    REQUIRE(thread > 0);
    GroupRecord g;
    REQUIRE(groupMgr.getGroup(normal, g));
    CHECK(g.grouptype == group_type_normal);
    REQUIRE(groupMgr.getGroup(thread, g));
    CHECK(g.grouptype == group_type_thread);

    //建群请求：不带 grouptype 为普通群，带 1 为帖子群，带别的值为报文非法
    CreateGroupRequest req;
    CHECK(GroupProtocol::parseCreateGroupRequest("{\"groupname\":\"a\"}", req));
    CHECK(req.grouptype == group_type_normal);
    CreateGroupRequest req2;
    CHECK(GroupProtocol::parseCreateGroupRequest("{\"groupname\":\"a\",\"grouptype\":1}", req2));
    CHECK(req2.grouptype == group_type_thread);
    CreateGroupRequest req3;
    CHECK_FALSE(GroupProtocol::parseCreateGroupRequest("{\"groupname\":\"a\",\"grouptype\":7}", req3));
}

TEST_CASE("TG-S2 帖子：发帖、读回、去重、修改、置顶计数")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadPost back;
    bool found = false;
    REQUIRE(mgr.getPost(p.postid, back, found));
    REQUIRE(found);
    CHECK(back.body.title == p.body.title);
    CHECK(back.activeTime == kTgTime);
    CHECK(back.editTime == 0);

    int64_t dup = 0;
    REQUIRE(mgr.findPostByClientId(kAuthor, "c1", dup));
    CHECK(dup == p.postid);
    REQUIRE(mgr.findPostByClientId(kMemberU, "c1", dup));
    CHECK(dup == 0);

    ThreadBody edited = textBody(u8"改过的正文");
    REQUIRE(mgr.updatePostBody(p.postid, edited, kTgTime + 5));
    REQUIRE(mgr.getPost(p.postid, back, found));
    CHECK(back.body.text == u8"改过的正文");
    CHECK(back.editTime == kTgTime + 5);

    int pins = -1;
    REQUIRE(mgr.countPins(kThreadGroup, pins));
    CHECK(pins == 0);
    REQUIRE(mgr.setPostPin(p.postid, kTgTime + 9));
    REQUIRE(mgr.countPins(kThreadGroup, pins));
    CHECK(pins == 1);
    REQUIRE(mgr.setPostCardMsgId(p.postid, 777));
    REQUIRE(mgr.getPost(p.postid, back, found));
    CHECK(back.cardMsgId == 777);
    CHECK(back.pinTime == kTgTime + 9);

    REQUIRE(mgr.getPost(999999, back, found));
    CHECK_FALSE(found);
}

TEST_CASE("TG-S3 列表：置顶在前、最新动态排序与两种游标翻页")
{
    resetThreadDb();
    ThreadManager mgr;
    std::vector<ThreadPost> posts;
    for (int i = 0; i < 5; ++i)
    {
        char id[8];
        snprintf(id, sizeof(id), "p%d", i);
        posts.push_back(addPost(mgr, kThreadGroup, kAuthor, id, kTgTime + i * 1000));
    }
    addPost(mgr, kOtherGroup, kAuthor, "x", kTgTime);
    //第 0 条置顶；第 1 条有新留言，最新动态排到最前
    REQUIRE(mgr.setPostPin(posts[0].postid, kTgTime + 1));
    addComment(mgr, posts[1], kMemberU, 0, 0, "k1", kTgTime + 100000);

    std::vector<ThreadPost> page;
    std::string next;
    bool more = false;
    REQUIRE(mgr.listPosts(kThreadGroup, thread_sort_activity, "", 2, page, next, more));
    REQUIRE(page.size() == 3);   //置顶 1 条 + 2 条
    CHECK(page[0].postid == posts[0].postid);
    CHECK(page[1].postid == posts[1].postid);
    CHECK(page[2].postid == posts[4].postid);
    CHECK(more);
    REQUIRE(mgr.listPosts(kThreadGroup, thread_sort_activity, next, 2, page, next, more));
    REQUIRE(page.size() == 2);
    CHECK(page[0].postid == posts[3].postid);
    CHECK(page[1].postid == posts[2].postid);
    CHECK_FALSE(more);
    CHECK(next.empty());

    //最新发布：按帖子 ID 倒序，置顶仍在第一页最前
    REQUIRE(mgr.listPosts(kThreadGroup, thread_sort_publish, "", 3, page, next, more));
    REQUIRE(page.size() == 4);
    CHECK(page[0].postid == posts[0].postid);
    CHECK(page[1].postid == posts[4].postid);
    CHECK(page[3].postid == posts[2].postid);
    CHECK(more);
    REQUIRE(mgr.listPosts(kThreadGroup, thread_sort_publish, next, 3, page, next, more));
    REQUIRE(page.size() == 1);
    CHECK(page[0].postid == posts[1].postid);
    CHECK_FALSE(more);
}

TEST_CASE("TG-S4 留言与回复：计数、最新动态、留言人、两级挂靠")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadComment top = addComment(mgr, p, kMemberU, 0, 0, "k1", kTgTime + 10);
    ThreadComment reply = addComment(mgr, p, kOtherU, top.commentid, kMemberU, "k2", kTgTime + 20);
    addComment(mgr, p, kAuthor, top.commentid, kOtherU, "k3", kTgTime + 30);

    int count = 0;
    int64_t active = 0;
    REQUIRE(mgr.getPostCounters(p.postid, count, active));
    CHECK(count == 3);
    CHECK(active == kTgTime + 30);

    ThreadComment back;
    bool found = false;
    REQUIRE(mgr.getComment(top.commentid, back, found));
    CHECK(back.replyCount == 2);
    REQUIRE(mgr.getComment(reply.commentid, back, found));
    CHECK(back.parentid == top.commentid);
    CHECK(back.replyTo == kMemberU);

    std::vector<int64_t> commenters;
    REQUIRE(mgr.commentersOfPost(p.postid, commenters));
    CHECK(std::set<int64_t>(commenters.begin(), commenters.end()) == std::set<int64_t>{kMemberU, kOtherU, kAuthor});

    std::vector<ThreadComment> list;
    int64_t next = 0;
    bool more = false;
    REQUIRE(mgr.listComments(p.postid, 0, 20, list, next, more));
    REQUIRE(list.size() == 1);
    CHECK(list[0].previewReplies.size() == 2);
    REQUIRE(mgr.listReplies(top.commentid, 0, 1, list, next, more));
    REQUIRE(list.size() == 1);
    CHECK(more);
    REQUIRE(mgr.listReplies(top.commentid, next, 1, list, next, more));
    REQUIRE(list.size() == 1);
    CHECK_FALSE(more);

    int64_t dup = 0;
    REQUIRE(mgr.findCommentByClientId(kOtherU, "k2", dup));
    CHECK(dup == reply.commentid);
}

TEST_CASE("TG-S5 删除留言：有回复时保留占位，最后一条回复删除后占位一并删除")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadComment top = addComment(mgr, p, kMemberU, 0, 0, "k1", kTgTime + 10);
    ThreadComment reply = addComment(mgr, p, kOtherU, top.commentid, kMemberU, "k2", kTgTime + 20);
    ThreadComment lone = addComment(mgr, p, kOtherU, 0, 0, "k3", kTgTime + 30);

    //没有回复的留言：直接删除
    ThreadComment back;
    bool found = false;
    REQUIRE(mgr.getComment(lone.commentid, back, found));
    bool kept = true;
    REQUIRE(mgr.deleteComment(back, kept));
    CHECK_FALSE(kept);
    REQUIRE(mgr.getComment(lone.commentid, back, found));
    CHECK_FALSE(found);

    //有回复的留言：保留占位
    REQUIRE(mgr.getComment(top.commentid, back, found));
    REQUIRE(mgr.deleteComment(back, kept));
    CHECK(kept);
    REQUIRE(mgr.getComment(top.commentid, back, found));
    REQUIRE(found);
    CHECK(back.deleted);
    CHECK(back.body.text.empty());
    int count = 0;
    int64_t active = 0;
    REQUIRE(mgr.getPostCounters(p.postid, count, active));
    CHECK(count == 1);

    //最后一条回复删除后，占位一并删除
    REQUIRE(mgr.getComment(reply.commentid, back, found));
    REQUIRE(mgr.deleteComment(back, kept));
    REQUIRE(mgr.getComment(top.commentid, back, found));
    CHECK_FALSE(found);
    REQUIRE(mgr.getPostCounters(p.postid, count, active));
    CHECK(count == 0);
}

TEST_CASE("TG-S6 互动：重复添加只算一次、取消、多种并存、mine、名单")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, kThreadReactionLike, true, kTgTime));
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, kThreadReactionLike, true, kTgTime + 1));
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kOtherU, kThreadReactionLike, true, kTgTime + 2));
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, 1005, true, kTgTime + 3));

    std::map<int64_t, std::vector<ThreadReaction> > summary;
    REQUIRE(mgr.loadReactions(thread_target_post, std::vector<int64_t>(1, p.postid), kMemberU, summary));
    const std::vector<ThreadReaction>& rs = summary[p.postid];
    REQUIRE(rs.size() == 2);
    CHECK(rs[0].reaction == kThreadReactionLike);
    CHECK(rs[0].count == 2);
    CHECK(rs[0].mine);
    CHECK(rs[1].reaction == 1005);
    CHECK(rs[1].count == 1);

    std::vector<int64_t> users;
    int total = 0;
    REQUIRE(mgr.listReactors(thread_target_post, p.postid, kThreadReactionLike, 1, users, total));
    CHECK(total == 2);
    REQUIRE(users.size() == 1);
    CHECK(users[0] == kMemberU);

    //取消；取消不存在的也算成功
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, kThreadReactionLike, false, kTgTime));
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, kThreadReactionLike, false, kTgTime));
    REQUIRE(mgr.loadReactions(thread_target_post, std::vector<int64_t>(1, p.postid), kMemberU, summary));
    CHECK(summary[p.postid][0].count == 1);
    CHECK_FALSE(summary[p.postid][0].mine);
}

TEST_CASE("TG-S7 补充：新增刷新最新动态、按时间排列、计数、删除")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadAddendum a1;
    a1.postid = p.postid;
    a1.groupid = kThreadGroup;
    a1.author = kAuthor;
    a1.body = textBody(u8"补充一");
    a1.createTime = kTgTime + 100;
    REQUIRE(mgr.insertAddendum(a1));
    ThreadAddendum a2 = a1;
    a2.body = textBody(u8"补充二");
    a2.createTime = kTgTime + 200;
    REQUIRE(mgr.insertAddendum(a2));
    CHECK(a2.addid > a1.addid);

    int count = 0;
    int64_t active = 0;
    REQUIRE(mgr.getPostCounters(p.postid, count, active));
    CHECK(active == kTgTime + 200);
    std::vector<ThreadAddendum> list;
    REQUIRE(mgr.listAddenda(p.postid, list));
    REQUIRE(list.size() == 2);
    CHECK(list[0].body.text == u8"补充一");
    std::map<int64_t, int> counts;
    REQUIRE(mgr.countAddenda(std::vector<int64_t>(1, p.postid), counts));
    CHECK(counts[p.postid] == 2);

    REQUIRE(mgr.deleteAddendum(a1.addid));
    ThreadAddendum back;
    bool found = true;
    REQUIRE(mgr.getAddendum(a1.addid, back, found));
    CHECK_FALSE(found);
    REQUIRE(mgr.getAddendum(a2.addid, back, found));
    CHECK(found);
}

TEST_CASE("TG-S8 删帖级联删除补充、留言与互动")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadPost keep = addPost(mgr, kThreadGroup, kAuthor, "c2", kTgTime);
    ThreadComment c = addComment(mgr, p, kMemberU, 0, 0, "k1", kTgTime + 10);
    addComment(mgr, keep, kMemberU, 0, 0, "k2", kTgTime + 10);
    ThreadAddendum a;
    a.postid = p.postid;
    a.groupid = kThreadGroup;
    a.author = kAuthor;
    a.body = textBody("add");
    a.createTime = kTgTime;
    REQUIRE(mgr.insertAddendum(a));
    REQUIRE(mgr.setReaction(thread_target_post, p.postid, kThreadGroup, kMemberU, 0, true, kTgTime));
    REQUIRE(mgr.setReaction(thread_target_comment, c.commentid, kThreadGroup, kAuthor, 0, true, kTgTime));
    REQUIRE(mgr.setReaction(thread_target_post, keep.postid, kThreadGroup, kMemberU, 0, true, kTgTime));

    REQUIRE(mgr.deletePost(p.postid));
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_thread_comment WHERE f_postid = " << p.postid;
    CHECK(countRows(os.str()) == 0);
    std::ostringstream os2;
    os2 << "SELECT COUNT(*) FROM t_thread_addendum WHERE f_postid = " << p.postid;
    CHECK(countRows(os2.str()) == 0);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_reaction") == 1);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_post") == 1);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_comment") == 1);
}

TEST_CASE("TG-S9 解散群删除帖子群四张表的数据")
{
    resetThreadDb();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    REQUIRE(groupMgr.init());
    const int64_t gid = groupMgr.createGroup(u8"帖子群", static_cast<int32_t>(kAuthor),
                                             std::vector<int64_t>(1, kMemberU), kTgTime, group_type_thread);
    REQUIRE(gid > 0);
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, gid, kAuthor, "c1", kTgTime);
    ThreadComment c = addComment(mgr, p, kMemberU, 0, 0, "k1", kTgTime + 10);
    REQUIRE(mgr.setReaction(thread_target_comment, c.commentid, gid, kAuthor, 0, true, kTgTime));
    ThreadAddendum a;
    a.postid = p.postid;
    a.groupid = gid;
    a.author = kAuthor;
    a.body = textBody("add");
    a.createTime = kTgTime;
    REQUIRE(mgr.insertAddendum(a));
    //别的群的帖子不受影响
    addPost(mgr, kOtherGroup, kAuthor, "x", kTgTime);

    REQUIRE(groupMgr.dismissGroup(gid));
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_post") == 1);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_comment") == 0);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_addendum") == 0);
    CHECK(countRows("SELECT COUNT(*) FROM t_thread_reaction") == 0);
    testdb::cleanupDb(kTgSqliteFile);
}

namespace {

//一段 @ 片段的 JSON
Json::Value spanJson(const char* uid, int off, int len)
{
    Json::Value v(Json::objectValue);
    v["uid"] = uid;
    v["off"] = off;
    v["len"] = len;
    return v;
}

//把一张卡片的 content 套上落库外壳后写进群里，返回 msgid；audience 为 NULL 时全员可见
int64_t saveThreadCard(int64_t groupid, int64_t from, int msgtype, const std::string& content, int64_t time,
                       const std::string& clientId, const std::vector<int32_t>* audience)
{
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        clientId, conversation_group, static_cast<int32_t>(from), static_cast<int32_t>(groupid), msgtype, stored, time,
        audience);
    REQUIRE(msgid > 0);
    return msgid;
}

//以 viewer 的身份取一批 msgid，按 2716 的筛选留下 groupid 群里已删除的卡片，返回它们的 msgid（升序）
std::vector<int64_t> deletedCardsSeenBy(int64_t viewer, const std::vector<int64_t>& msgids, int64_t groupid)
{
    std::vector<MessageRecord> records;
    REQUIRE(Singleton<MessageManager>::Instance().getMessagesByIds(static_cast<int32_t>(viewer), msgids, records));
    std::set<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
    {
        const MessageRecord& r = records[i];
        if (ThreadProtocol::isDeletedCardOfGroup(r.ctype, r.to, r.msgtype, r.content, groupid))
            ids.insert(r.msgid);
    }
    return std::vector<int64_t>(ids.begin(), ids.end());
}

} // namespace

TEST_CASE("TG13 正文的 @ 片段：解析、整理与下发")
{
    //「请看 」7 字节，「@张三」7 字节从 7 开始；「 和 」5 字节，「@李四」从 19 开始
    const std::string text = u8"请看 @张三 和 @李四 好的";
    Json::Value root(Json::objectValue);
    root["groupid"] = "1";
    root["clientpostid"] = "c";
    root["text"] = text;
    root["mentions"].append("4103");
    root["mentions"].append("4104");
    root["at"].append(spanJson("4103", 7, 7));
    root["at"].append(spanJson("4104", 19, 7));
    root["at"].append(spanJson("4104", 100, 7));   //越出正文
    root["at"].append(spanJson("x1", 0, 1));       //uid 不合法：解析时丢弃（uid "0" 是「@所有人」的片段，见 TG17）
    root["at"].append("bad");                      //不是对象：解析时丢弃
    root["at"].append(spanJson("4103", 8, 3));     //首字节不是 '@'
    root["at"].append(spanJson("9999", 0, 1));     //不在 mentions 里
    root["at"].append(spanJson("4103", 7, 4));     //与第一段重叠
    PostWriteRequest req;
    REQUIRE(ThreadProtocol::parsePublishPostRequest(writeCompactForTest(root), req));
    CHECK(req.body.at.size() == 6);
    ThreadProtocol::sanitizeMentionSpans(req.body);
    REQUIRE(req.body.at.size() == 2);
    CHECK(req.body.at[0].uid == 4103);
    CHECK(req.body.at[0].off == 7);
    CHECK(req.body.at[0].len == 7);
    CHECK(req.body.at[1].uid == 4104);
    CHECK(req.body.at[1].off == 19);

    //at 存在但不是数组：报文非法
    Json::Value bad = root;
    bad["at"] = "x";
    PostWriteRequest badReq;
    CHECK_FALSE(ThreadProtocol::parsePublishPostRequest(writeCompactForTest(bad), badReq));

    //编辑、补充、留言与回复的请求同样解析 at（2026-10-08 实机发现留言的解析漏了 at，补上这四种请求的断言）
    Json::Value edit = root;
    edit.removeMember("groupid");
    edit.removeMember("clientpostid");
    edit["postid"] = "9";
    PostWriteRequest editReq;
    REQUIRE(ThreadProtocol::parseEditPostRequest(writeCompactForTest(edit), editReq));
    CHECK(editReq.body.at.size() == 6);
    Json::Value addendum = edit;
    addendum["clientaddid"] = "a";
    PostWriteRequest addReq;
    REQUIRE(ThreadProtocol::parseAddAddendumRequest(writeCompactForTest(addendum), addReq));
    CHECK(addReq.body.at.size() == 6);
    Json::Value comment(Json::objectValue);
    comment["postid"] = "9";
    comment["parentid"] = "0";
    comment["clientcommentid"] = "c1";
    comment["text"] = text;
    comment["mentions"] = root["mentions"];
    comment["at"] = root["at"];
    AddCommentRequest commentReq;
    REQUIRE(ThreadProtocol::parseAddCommentRequest(writeCompactForTest(comment), commentReq));
    REQUIRE(commentReq.body.at.size() == 6);
    ThreadProtocol::sanitizeMentionSpans(commentReq.body);
    REQUIRE(commentReq.body.at.size() == 2);
    CHECK(commentReq.body.at[0].off == 7);
    CHECK(commentReq.body.at[1].off == 19);
    //留言的 at 不是数组同样按报文非法处理
    comment["at"] = "x";
    AddCommentRequest badComment;
    CHECK_FALSE(ThreadProtocol::parseAddCommentRequest(writeCompactForTest(comment), badComment));

    //落库往返保留片段；没有片段时不写 at
    ThreadBody back;
    REQUIRE(ThreadProtocol::bodyFromJson(ThreadProtocol::bodyToJson(req.body), back));
    REQUIRE(back.at.size() == 2);
    CHECK(back.at[1].off == 19);
    CHECK_FALSE(parseJson(ThreadProtocol::bodyToJson(textBody("x"))).isMember("at"));

    //帖子列表里正文截取到 300 字：越出截取范围的片段不下发，完整帖子全部下发
    ThreadPost p;
    p.postid = 9;
    p.body.text = u8"@张三 " + hanzi(400) + u8" @李四";
    p.body.mentions.push_back(4103);
    p.body.mentions.push_back(4104);
    ThreadMentionSpan first;
    first.uid = 4103;
    first.off = 0;
    first.len = 7;
    ThreadMentionSpan last;
    last.uid = 4104;
    last.off = static_cast<int>(p.body.text.size()) - 7;
    last.len = 7;
    p.body.at.push_back(first);
    p.body.at.push_back(last);
    ThreadProtocol::sanitizeMentionSpans(p.body);
    REQUIRE(p.body.at.size() == 2);
    Json::Value list = parseJson(ThreadProtocol::buildListPostsResponse(0, "ok", std::vector<ThreadPost>(1, p), "", false));
    REQUIRE(list["posts"].size() == 1);
    REQUIRE(list["posts"][0]["at"].size() == 1);
    CHECK(list["posts"][0]["at"][0]["uid"].asString() == "4103");
    Json::Value full = parseJson(ThreadProtocol::buildPostResponse(0, "ok", &p, true));
    CHECK(full["post"]["at"].size() == 2);

    //已删除的留言不带片段
    ThreadComment c;
    c.commentid = 3;
    c.body = p.body;
    c.deleted = true;
    Json::Value cj = parseJson(ThreadProtocol::buildCommentResponse(0, "ok", &c));
    CHECK(cj["comment"]["at"].size() == 0);
}

TEST_CASE("TG14 帖子卡片与补充卡片带图片")
{
    ThreadPost p;
    p.postid = 55;
    p.body.title = u8"周五团建";
    p.body.text = u8"报名";
    //没有图片时不写 images
    CHECK_FALSE(parseJson(ThreadProtocol::buildPostCardContent(p)).isMember("images"));
    p.body.images.push_back(image("11"));
    p.body.images.push_back(image("12"));
    p.body.images[1].thumbfileid = "t12";
    Json::Value card = parseJson(ThreadProtocol::buildPostCardContent(p));
    REQUIRE(card["images"].size() == 2);
    CHECK(card["images"][0]["fileid"].asString() == "11");
    CHECK(card["images"][0]["md5"].asString() == "0123456789abcdef0123456789abcdef");
    CHECK(card["images"][0]["width"].asInt() == 640);
    CHECK(card["images"][0]["height"].asInt() == 480);
    CHECK(card["images"][1]["thumbfileid"].asString() == "t12");
    CHECK(card["imagecount"].asInt() == 2);

    //卡片最多带 9 张
    for (int i = 0; i < 8; ++i)
        p.body.images.push_back(image("x"));
    CHECK(parseJson(ThreadProtocol::buildPostCardContent(p))["images"].size() == static_cast<Json::ArrayIndex>(kThreadMaxImages));

    //已删除形态不带图片
    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(ThreadProtocol::buildPostCardContent(p), deleted));
    Json::Value d = parseJson(deleted);
    CHECK_FALSE(d.isMember("images"));
    CHECK(d["imagecount"].asInt() == 0);

    //补充卡片带图片，留言卡片不带
    std::vector<ThreadImage> images(1, image("21"));
    Json::Value add = parseJson(ThreadProtocol::buildReplyCardContent(55, 0, 7, u8"周五团建", u8"改到东门",
                                                                     thread_reply_addendum, std::vector<int64_t>(), images));
    REQUIRE(add["images"].size() == 1);
    CHECK(add["images"][0]["fileid"].asString() == "21");
    CHECK_FALSE(parseJson(ThreadProtocol::buildReplyCardContent(55, 6, 0, u8"周五团建", u8"收到", thread_reply_comment))
                    .isMember("images"));
}

TEST_CASE("TG15 动态卡片的已删除形态与摘要")
{
    std::vector<int64_t> mentions(1, kMemberU);
    std::vector<ThreadImage> images(1, image("21"));
    const std::string comment = ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"收到",
                                                                      thread_reply_comment, mentions);
    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedReplyCardContent(comment, deleted));
    Json::Value d = parseJson(deleted);
    CHECK(d["postid"].asString() == "55");
    CHECK(d["commentid"].asString() == "66");
    CHECK(d["addid"].asString() == "0");
    CHECK(d["kind"].asInt() == thread_reply_comment);
    CHECK(d["deleted"].asInt() == 1);
    CHECK_FALSE(d.isMember("posttitle"));
    CHECK_FALSE(d.isMember("digest"));
    CHECK_FALSE(d.isMember("mentions"));
    //内容不是 JSON 对象时返回失败（用单独的出参：失败时出参会被清空）
    std::string notObject;
    CHECK_FALSE(ThreadProtocol::buildDeletedReplyCardContent("[1]", notObject));

    //补充卡片的图片一并去掉
    std::string addDeleted;
    REQUIRE(ThreadProtocol::buildDeletedReplyCardContent(
        ThreadProtocol::buildReplyCardContent(55, 0, 7, u8"周五团建", u8"改到东门", thread_reply_addendum,
                                              std::vector<int64_t>(), images),
        addDeleted));
    CHECK_FALSE(parseJson(addDeleted).isMember("images"));

    //已删除的判断
    CHECK(ThreadProtocol::isDeletedCardContent(deleted));
    CHECK_FALSE(ThreadProtocol::isDeletedCardContent(comment));
    CHECK_FALSE(ThreadProtocol::isDeletedCardContent("not json"));

    //摘要：「[留言已删除]」等，不带原文
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply, deleted) == u8"[留言已删除]");
    std::string replyDeleted;
    REQUIRE(ThreadProtocol::buildDeletedReplyCardContent(
        ThreadProtocol::buildReplyCardContent(55, 67, 0, u8"周五团建", u8"可以", thread_reply_reply), replyDeleted));
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply, replyDeleted) == u8"[回复已删除]");
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply, addDeleted) == u8"[补充已删除]");

    //会话列表的 lastmsg：已删除的动态卡片带 threaddeleted，不带 digestarg
    std::vector<ConversationBrief> items;
    ConversationBrief b;
    b.ctype = conversation_group;
    b.cid = 600008;
    b.lastmsgtype = message_type_thread_reply;
    b.lastcontent = deleted;
    items.push_back(b);
    Json::Value root = parseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items));
    const Json::Value& last = root["conversations"][0]["lastmsg"];
    CHECK(last["threaddeleted"].asInt() == 1);
    CHECK_FALSE(last.isMember("digestarg"));
    CHECK(last["threadkind"].asInt() == thread_reply_comment);
}

TEST_CASE("TG16 2716 请求解析、卡片筛选与 2716 / 2721 组包")
{
    int64_t groupid = 0;
    std::vector<int64_t> msgids;
    REQUIRE(ThreadProtocol::parseCheckCardsRequest(u8"{\"groupid\":\"5\",\"msgids\":[\"1\",2,\"1\"]}", groupid, msgids));
    CHECK(groupid == 5);
    REQUIRE(msgids.size() == 2);
    CHECK(msgids[0] == 1);
    CHECK(msgids[1] == 2);
    CHECK_FALSE(ThreadProtocol::parseCheckCardsRequest("{\"msgids\":[\"1\"]}", groupid, msgids));
    CHECK_FALSE(ThreadProtocol::parseCheckCardsRequest("{\"groupid\":\"5\",\"msgids\":[]}", groupid, msgids));
    CHECK_FALSE(ThreadProtocol::parseCheckCardsRequest("{\"groupid\":\"5\",\"msgids\":\"1\"}", groupid, msgids));
    CHECK_FALSE(ThreadProtocol::parseCheckCardsRequest("{\"groupid\":\"5\",\"msgids\":[\"0\"]}", groupid, msgids));
    //最多 500 个
    Json::Value many(Json::objectValue);
    many["groupid"] = "5";
    for (int i = 1; i <= kThreadMaxCheckCards; ++i)
        many["msgids"].append(i);
    CHECK(ThreadProtocol::parseCheckCardsRequest(writeCompactForTest(many), groupid, msgids));
    CHECK(msgids.size() == static_cast<size_t>(kThreadMaxCheckCards));
    many["msgids"].append(kThreadMaxCheckCards + 1);
    CHECK_FALSE(ThreadProtocol::parseCheckCardsRequest(writeCompactForTest(many), groupid, msgids));

    //筛选：本群、25 或 26、已删除形态
    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(u8"{\"postid\":\"1\",\"title\":\"t\"}", deleted));
    CHECK(ThreadProtocol::isDeletedCardOfGroup(conversation_group, 7, message_type_thread_post, deleted, 7));
    CHECK_FALSE(ThreadProtocol::isDeletedCardOfGroup(conversation_group, 8, message_type_thread_post, deleted, 7));
    CHECK_FALSE(ThreadProtocol::isDeletedCardOfGroup(conversation_single, 7, message_type_thread_post, deleted, 7));
    CHECK_FALSE(ThreadProtocol::isDeletedCardOfGroup(conversation_group, 7, message_type_text, deleted, 7));
    CHECK_FALSE(ThreadProtocol::isDeletedCardOfGroup(conversation_group, 7, message_type_thread_post,
                                                     u8"{\"postid\":\"1\"}", 7));

    //组包：content 作为 JSON 对象放入，解析不了的跳过
    std::vector<ThreadCardState> cards;
    ThreadCardState s;
    s.msgid = 1424;
    s.msgtype = message_type_thread_post;
    s.content = deleted;
    cards.push_back(s);
    s.msgid = 1425;
    s.content = "not json";
    cards.push_back(s);
    Json::Value resp = parseJson(ThreadProtocol::buildCheckCardsResponse(0, "ok", 7, cards));
    CHECK(resp["code"].asInt() == 0);
    CHECK(resp["groupid"].asString() == "7");
    REQUIRE(resp["cards"].size() == 1);
    CHECK(resp["cards"][0]["msgid"].asString() == "1424");
    CHECK(resp["cards"][0]["msgtype"].asInt() == message_type_thread_post);
    CHECK(resp["cards"][0]["content"]["deleted"].asInt() == 1);
    Json::Value notify = parseJson(ThreadProtocol::buildCardsNotify(7, cards));
    CHECK(notify["groupid"].asString() == "7");
    CHECK(notify["cards"].size() == 1);
}

TEST_CASE("TG-S10 留言与补充记下动态卡片 msgid，删帖前取得出")
{
    resetThreadDb();
    ThreadManager mgr;
    ThreadPost p = addPost(mgr, kThreadGroup, kAuthor, "c1", kTgTime);
    ThreadPost other = addPost(mgr, kThreadGroup, kAuthor, "c2", kTgTime);
    ThreadComment c1 = addComment(mgr, p, kMemberU, 0, 0, "k1", kTgTime + 10);
    ThreadComment c2 = addComment(mgr, p, kAdminU, 0, 0, "k2", kTgTime + 20);   //没有写卡片
    ThreadComment c3 = addComment(mgr, other, kMemberU, 0, 0, "k3", kTgTime + 30);
    REQUIRE(mgr.setCommentCardMsgId(c1.commentid, 901));
    REQUIRE(mgr.setCommentCardMsgId(c3.commentid, 903));
    ThreadAddendum a;
    a.postid = p.postid;
    a.groupid = kThreadGroup;
    a.author = kAuthor;
    a.body = textBody("add");
    a.createTime = kTgTime + 40;
    REQUIRE(mgr.insertAddendum(a));
    REQUIRE(mgr.setAddendumCardMsgId(a.addid, 902));

    ThreadComment back;
    bool found = false;
    REQUIRE(mgr.getComment(c1.commentid, back, found));
    REQUIRE(found);
    CHECK(back.cardMsgId == 901);
    REQUIRE(mgr.getComment(c2.commentid, back, found));
    CHECK(back.cardMsgId == 0);
    ThreadAddendum addBack;
    REQUIRE(mgr.getAddendum(a.addid, addBack, found));
    CHECK(addBack.cardMsgId == 902);
    std::vector<ThreadAddendum> list;
    REQUIRE(mgr.listAddenda(p.postid, list));
    REQUIRE(list.size() == 1);
    CHECK(list[0].cardMsgId == 902);

    //只取本帖的、大于 0 的，升序
    std::vector<int64_t> ids;
    REQUIRE(mgr.replyCardMsgIdsOfPost(p.postid, ids));
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 901);
    CHECK(ids[1] == 902);

    //删帖后这些行都没了，必须在删帖之前取
    REQUIRE(mgr.deletePost(p.postid));
    REQUIRE(mgr.replyCardMsgIdsOfPost(p.postid, ids));
    CHECK(ids.empty());
    REQUIRE(mgr.replyCardMsgIdsOfPost(other.postid, ids));
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 903);
}

TEST_CASE("TG-S11 2716 的取数与筛选")
{
    resetThreadDb();
    ThreadPost p;
    p.postid = 31;
    p.body.title = u8"标题";
    p.body.text = u8"正文";
    const std::string postCard = ThreadProtocol::buildPostCardContent(p);
    const std::string replyCard = ThreadProtocol::buildReplyCardContent(31, 41, 0, u8"标题", u8"留言内容",
                                                                        thread_reply_comment);
    //动态卡片只给楼主看
    const std::vector<int32_t> onlyAuthor(1, static_cast<int32_t>(kAuthor));
    const int64_t mPost = saveThreadCard(kThreadGroup, kAuthor, message_type_thread_post, postCard, kTgTime, "s11a", NULL);
    const int64_t mReply = saveThreadCard(kThreadGroup, kMemberU, message_type_thread_reply, replyCard, kTgTime + 1,
                                          "s11b", &onlyAuthor);
    const int64_t mAlive = saveThreadCard(kThreadGroup, kAuthor, message_type_thread_post, postCard, kTgTime + 2, "s11c",
                                          NULL);
    const int64_t mOther = saveThreadCard(kOtherGroup, kAuthor, message_type_thread_post, postCard, kTgTime + 3, "s11d",
                                          NULL);
    const std::string text = MessageProtocol::buildStoredContent(u8"{\"text\":\"普通消息\"}", std::vector<int64_t>(),
                                                                 false, "");
    const int64_t mText = Singleton<MessageManager>::Instance().saveMessage(
        "s11e", conversation_group, static_cast<int32_t>(kAuthor), static_cast<int32_t>(kThreadGroup), message_type_text,
        text, kTgTime + 4);
    REQUIRE(mText > 0);

    //改写三张：本群的帖子卡片、本群的动态卡片、别的群的帖子卡片
    std::string deletedPost;
    std::string deletedReply;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(postCard, deletedPost));
    REQUIRE(ThreadProtocol::buildDeletedReplyCardContent(replyCard, deletedReply));
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    const std::vector<int64_t> noMentions;
    REQUIRE(msgMgr.updateMessageContent(mPost, MessageProtocol::buildStoredContent(deletedPost, noMentions, false, "")));
    REQUIRE(msgMgr.updateMessageContent(mReply, MessageProtocol::buildStoredContent(deletedReply, noMentions, false, "")));
    REQUIRE(msgMgr.updateMessageContent(mOther, MessageProtocol::buildStoredContent(deletedPost, noMentions, false, "")));

    std::vector<int64_t> all;
    all.push_back(mPost);
    all.push_back(mReply);
    all.push_back(mAlive);
    all.push_back(mOther);
    all.push_back(mText);
    //楼主看得到动态卡片：本群两张已删除的
    const std::vector<int64_t> byAuthor = deletedCardsSeenBy(kAuthor, all, kThreadGroup);
    REQUIRE(byAuthor.size() == 2);
    CHECK(byAuthor[0] == mPost);
    CHECK(byAuthor[1] == mReply);
    //别的成员看不到只给楼主的动态卡片：只有帖子卡片
    const std::vector<int64_t> byOther = deletedCardsSeenBy(kOtherU, all, kThreadGroup);
    REQUIRE(byOther.size() == 1);
    CHECK(byOther[0] == mPost);
    testdb::cleanupDb(kTgSqliteFile);
}

TEST_CASE("RW9 编辑帖子：卡片被改写并分配改写序号，2721 带新标题与新摘要")
{
    resetThreadDb();
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    ThreadPost p;
    p.postid = 51;
    p.groupid = kThreadGroup;
    p.body.title = u8"旧标题";
    p.body.text = u8"旧正文";
    const int64_t cardMsgId = saveThreadCard(kThreadGroup, kAuthor, message_type_thread_post,
                                             ThreadProtocol::buildPostCardContent(p), kTgTime, "rw9", NULL);

    //与 ChatSession::onThreadEditPost 相同的步骤：按编辑后的帖子组卡片内容、改写卡片、组 2721 推送
    p.body.title = u8"新标题";
    p.body.text = u8"新正文";
    const std::string edited = ThreadProtocol::buildPostCardContent(p);
    REQUIRE(msgMgr.updateMessageContent(cardMsgId,
                                        MessageProtocol::buildStoredContent(edited, std::vector<int64_t>(), false, "")));
    ThreadCardState card;
    card.msgid = cardMsgId;
    card.msgtype = message_type_thread_post;
    card.content = edited;
    Json::Value notify = parseJson(ThreadProtocol::buildCardsNotify(kThreadGroup, std::vector<ThreadCardState>(1, card)));
    CHECK(notify["groupid"].asString() == std::to_string(kThreadGroup));
    REQUIRE(notify["cards"].size() == 1);
    const Json::Value& pushed = notify["cards"][0];
    CHECK(pushed["msgid"].asString() == std::to_string(cardMsgId));
    CHECK(pushed["msgtype"].asInt() == message_type_thread_post);
    CHECK(pushed["content"]["title"].asString() == u8"新标题");
    CHECK(pushed["content"]["digest"].asString().find(u8"新正文") != std::string::npos);
    CHECK_FALSE(pushed["content"].isMember("deleted"));

    //改写分配了序号：群成员按改写序号同步取得到这张卡片，内容是新的
    std::vector<GroupSearchScope> scopes(1);
    scopes[0].groupid = kThreadGroup;
    std::vector<RewrittenMessage> items;
    bool more = false;
    int64_t currev = 0;
    REQUIRE(msgMgr.getRewrittenMessages(static_cast<int32_t>(kMemberU), scopes, 0, REWRITTEN_SYNC_LIMIT_MAX, items,
                                        more, currev));
    REQUIRE(items.size() == 1);
    CHECK(items[0].record.msgid == cardMsgId);
    CHECK(items[0].rev > 0);
    CHECK(parseJson(items[0].record.content)["title"].asString() == u8"新标题");
}

TEST_CASE("TG17 @所有人：请求解析、「@所有人」片段与内容 JSON 往返")
{
    //「@所有人」10 字节从 0 开始；「 和 」5 字节，「@张三」7 字节从 15 开始
    const std::string text = u8"@所有人 和 @张三 看一下";
    Json::Value root(Json::objectValue);
    root["groupid"] = "1";
    root["clientpostid"] = "c";
    root["text"] = text;
    root["mentions"].append("4103");
    root["at"].append(spanJson("0", 0, 10));
    root["at"].append(spanJson("4103", 15, 7));

    //不带 mentionall：缺省为 false；uid 为 "0" 的片段解析时保留，整理时因为没有 @ 所有人而丢弃
    PostWriteRequest plain;
    REQUIRE(ThreadProtocol::parsePublishPostRequest(writeCompactForTest(root), plain));
    CHECK_FALSE(plain.body.mentionall);
    REQUIRE(plain.body.at.size() == 2);
    CHECK(plain.body.at[0].uid == 0);
    ThreadProtocol::sanitizeMentionSpans(plain.body);
    REQUIRE(plain.body.at.size() == 1);
    CHECK(plain.body.at[0].uid == 4103);

    //带 "mentionall": true：解析出来，整理后「@所有人」片段保留在最前
    root["mentionall"] = true;
    PostWriteRequest req;
    REQUIRE(ThreadProtocol::parsePublishPostRequest(writeCompactForTest(root), req));
    CHECK(req.body.mentionall);
    ThreadProtocol::sanitizeMentionSpans(req.body);
    REQUIRE(req.body.at.size() == 2);
    CHECK(req.body.at[0].uid == 0);
    CHECK(req.body.at[0].off == 0);
    CHECK(req.body.at[1].uid == 4103);

    //mentionall 不是布尔值：报文非法
    Json::Value bad = root;
    bad["mentionall"] = 1;
    PostWriteRequest badReq;
    CHECK_FALSE(ThreadProtocol::parsePublishPostRequest(writeCompactForTest(bad), badReq));

    //编辑、补充、留言与回复的请求同样解析 mentionall
    Json::Value edit = root;
    edit.removeMember("groupid");
    edit.removeMember("clientpostid");
    edit["postid"] = "9";
    PostWriteRequest editReq;
    REQUIRE(ThreadProtocol::parseEditPostRequest(writeCompactForTest(edit), editReq));
    CHECK(editReq.body.mentionall);
    Json::Value addendum = edit;
    addendum["clientaddid"] = "a";
    PostWriteRequest addReq;
    REQUIRE(ThreadProtocol::parseAddAddendumRequest(writeCompactForTest(addendum), addReq));
    CHECK(addReq.body.mentionall);
    Json::Value comment(Json::objectValue);
    comment["postid"] = "9";
    comment["parentid"] = "0";
    comment["clientcommentid"] = "c1";
    comment["text"] = text;
    comment["mentions"] = root["mentions"];
    comment["at"] = root["at"];
    AddCommentRequest noAll;
    REQUIRE(ThreadProtocol::parseAddCommentRequest(writeCompactForTest(comment), noAll));
    CHECK_FALSE(noAll.body.mentionall);
    comment["mentionall"] = true;
    AddCommentRequest commentReq;
    REQUIRE(ThreadProtocol::parseAddCommentRequest(writeCompactForTest(comment), commentReq));
    CHECK(commentReq.body.mentionall);
    comment["mentionall"] = "yes";
    AddCommentRequest badComment;
    CHECK_FALSE(ThreadProtocol::parseAddCommentRequest(writeCompactForTest(comment), badComment));

    //落库往返保留 mentionall 与「@所有人」片段；没有 @ 所有人时不写该字段
    ThreadBody back;
    REQUIRE(ThreadProtocol::bodyFromJson(ThreadProtocol::bodyToJson(req.body), back));
    CHECK(back.mentionall);
    REQUIRE(back.at.size() == 2);
    CHECK(back.at[0].uid == 0);
    CHECK_FALSE(parseJson(ThreadProtocol::bodyToJson(textBody("x"))).isMember("mentionall"));

    //下发：帖子、补充、留言带 "mentionall": true，片段的 uid 为 "0"；已删除的留言不带
    ThreadPost p;
    p.postid = 9;
    p.body = req.body;
    Json::Value full = parseJson(ThreadProtocol::buildPostResponse(0, "ok", &p, true));
    CHECK(full["post"]["mentionall"].asBool());
    REQUIRE(full["post"]["at"].size() == 2);
    CHECK(full["post"]["at"][0]["uid"].asString() == "0");
    p.body.mentionall = false;
    CHECK_FALSE(parseJson(ThreadProtocol::buildPostResponse(0, "ok", &p, true))["post"].isMember("mentionall"));
    ThreadAddendum a;
    a.addid = 5;
    a.body = req.body;
    CHECK(parseJson(ThreadProtocol::buildAddendumResponse(0, "ok", &a))["addendum"]["mentionall"].asBool());
    ThreadComment c;
    c.commentid = 3;
    c.body = commentReq.body;
    CHECK(parseJson(ThreadProtocol::buildCommentResponse(0, "ok", &c))["comment"]["mentionall"].asBool());
    c.deleted = true;
    CHECK_FALSE(parseJson(ThreadProtocol::buildCommentResponse(0, "ok", &c))["comment"].isMember("mentionall"));
}

TEST_CASE("TG18 @所有人的权限与普通群相同")
{
    //群主、管理员不受群设置限制
    CHECK(GroupProtocol::canMentionAll(group_at_all_owner_admin, group_role_owner));
    CHECK(GroupProtocol::canMentionAll(group_at_all_owner_admin, group_role_admin));
    CHECK(GroupProtocol::canMentionAll(group_at_all_everyone, group_role_owner));
    CHECK(GroupProtocol::canMentionAll(group_at_all_everyone, group_role_admin));
    //普通成员：只有群设为「所有人都可 @ 全体」时可以（普通群发消息原来的判定：仅群主和管理员 + 成员 → 拒绝）
    CHECK_FALSE(GroupProtocol::canMentionAll(group_at_all_owner_admin, group_role_member));
    CHECK(GroupProtocol::canMentionAll(group_at_all_everyone, group_role_member));
}

TEST_CASE("TG19 @所有人的卡片与可见者")
{
    ThreadPost p;
    p.postid = 55;
    p.body.title = u8"周五团建";
    p.body.text = u8"@所有人 报名";
    const std::string plainCard = ThreadProtocol::buildPostCardContent(p);
    CHECK_FALSE(parseJson(plainCard).isMember("mentionall"));
    p.body.mentionall = true;
    const std::string allCard = ThreadProtocol::buildPostCardContent(p);
    CHECK(parseJson(allCard)["mentionall"].asBool());
    //会话摘要不受 mentionall 影响（「[有人@我]」由客户端按 mentionall 自己加）
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_post, allCard)
          == MessageProtocol::buildMessageDigest(message_type_thread_post, plainCard));
    //已删除形态去掉 mentionall
    std::string deleted;
    REQUIRE(ThreadProtocol::buildDeletedPostCardContent(allCard, deleted));
    CHECK_FALSE(parseJson(deleted).isMember("mentionall"));

    //动态卡片：mentionAll 为真时带，为假（缺省）时不带；已删除形态去掉
    const std::vector<int64_t> noMentions;
    const std::vector<ThreadImage> noImages;
    const std::string replyPlain = ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"我也去",
                                                                         thread_reply_comment);
    CHECK_FALSE(parseJson(replyPlain).isMember("mentionall"));
    const std::string replyAll = ThreadProtocol::buildReplyCardContent(55, 66, 0, u8"周五团建", u8"我也去",
                                                                       thread_reply_comment, noMentions, noImages, true);
    CHECK(parseJson(replyAll)["mentionall"].asBool());
    CHECK(MessageProtocol::buildMessageDigest(message_type_thread_reply, replyAll)
          == MessageProtocol::buildMessageDigest(message_type_thread_reply, replyPlain));
    std::string replyDeleted;
    REQUIRE(ThreadProtocol::buildDeletedReplyCardContent(replyAll, replyDeleted));
    CHECK_FALSE(parseJson(replyDeleted).isMember("mentionall"));

    //可见者：并入全体成员，去掉发起人本人与 0，去重升序
    std::vector<int64_t> audience;
    audience.push_back(kAuthor);
    std::vector<int64_t> members;
    members.push_back(kMemberU);
    members.push_back(kAuthor);
    members.push_back(kOtherU);
    members.push_back(kAdminU);
    members.push_back(0);
    ThreadProtocol::mergeMentionAllAudience(members, kOtherU, audience);
    REQUIRE(audience.size() == 3);
    CHECK(audience[0] == kAuthor);
    CHECK(audience[1] == kAdminU);
    CHECK(audience[2] == kMemberU);
}
