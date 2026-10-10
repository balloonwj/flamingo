/**
 *  消息搜索文本与会话内搜索的单元测试（Suite STX），test_messagesearchtext.cpp
 *
 *  覆盖「直接对 f_content 做 LIKE 搜不到中文、搜英文又会命中 JSON 键名」这一问题的修复
 *  （docs/全局搜索增强方案.md 第 3a 步）：
 *    - STX1 BuildStoredSearchText / BuildContentSearchText 按消息类型取文字：文本（内联表情换成
 *           「[表情]」）、系统提示、文件名、文件夹名、共享名、接龙标题与说明、合并转发逐层展开；
 *           图片等类型与解析失败时为空串；
 *    - STX2 StripFourByteUtf8 去掉四字节字符与残缺序列、保留中文；TruncateUtf8 不截出半个字；
 *    - STX3 searchMessages（真的打开临时库跑 SQL）：中文关键字搜得到；英文关键字不再命中 JSON
 *           键名；已撤回的不返回；minCreateTime 之前的不返回；还没回填的存量行（f_search_text 为
 *           NULL）按 f_content 照样搜得到；关键字里的 % _ 按普通字符匹配；
 *    - STX4 backfillSearchText 按批回填 NULL 行、没有待回填的行时返回 0；updateMessageContent 改写
 *           内容后搜索文本跟着更新。
 *
 *  典型用法：Tests.exe --test-case="STX*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageSearchText.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

using namespace chatserver;

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kStxDbPath = "flamingo_sqlite_searchtext_test.db";
const char* const kStxMysqlDb = "flamingo_test_stx";

//单聊双方与一个群
const int32_t kUserA = 200001;
const int32_t kUserB = 200002;
const int32_t kGroupG = 910001;

//消息时间轴：以 kT0 为起点每隔一秒一条
const int64_t kT0 = 1700000000000LL;
const int64_t kStepMs = 1000;

//一次搜索最多取的条数，远大于本套用例里任何一个会话的消息数
const int kFetchLimit = 100;

//生成 clientmsgid 的序号
int g_stxSeq = 0;

std::string NextClientId()
{
    ++g_stxSeq;
    char buf[32] = { 0 };
    ::snprintf(buf, sizeof(buf), "stx-%d", g_stxSeq);
    return buf;
}

//按 content JSON 文本存一条消息，返回 msgid
int64_t SaveContent(int ctype, int32_t from, int32_t to, int msgtype, const std::string& content, int index)
{
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        NextClientId(), ctype, from, to, msgtype, stored, kT0 + index * kStepMs);
    REQUIRE(msgid > 0);
    return msgid;
}

//在群 G 里搜索，返回命中的 msgid（升序）
std::vector<int64_t> SearchGroup(int32_t userid, const std::string& keyword, int64_t minCreateTime)
{
    SearchMessagesRequest req;
    req.ctype = conversation_group;
    req.cid = kGroupG;
    req.keyword = keyword;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(userid, req, records, hasMore, minCreateTime));
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

//读一条消息的 f_search_text；为 NULL 时返回 "<NULL>"
std::string ReadSearchText(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    char sql[128] = { 0 };
    ::snprintf(sql, sizeof(sql), "SELECT f_search_text IS NULL, f_search_text FROM t_chatmsg WHERE f_msgid = %lld",
               (long long)msgid);
    QueryResult* pResult = pConn->query(sql);
    REQUIRE(pResult != NULL);
    std::string text = "<NONE>";
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        text = (pRow[0].getInt32() != 0) ? std::string("<NULL>") : pRow[1].getCppString();
    pResult->endQuery();
    delete pResult;
    return text;
}

//模拟升级前入库的存量消息：直接插行、不写 f_search_text（为 NULL）。text 只能是 ASCII
int64_t InsertLegacyText(int64_t msgid, const char* text, int index)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    char sql[512] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "INSERT INTO t_chatmsg(f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
               "f_recalled, f_create_time) VALUES(%lld, 'legacy%lld', %d, %d, %d, %d, "
               "'{\"content\":{\"text\":\"%s\"},\"mentions\":[],\"mentionall\":false}', 0, %lld)",
               (long long)msgid, (long long)msgid, conversation_group, kUserA, kGroupG, message_type_text,
               text, (long long)(kT0 + index * kStepMs));
    REQUIRE(pConn->execute(sql));
    return msgid;
}

} // namespace

TEST_CASE("STX1 按消息类型取搜索文本")
{
    //文本：内联表情换成占位文字，转义的 // 还原为 /
    CHECK(BuildContentSearchText(message_type_text, "{\"text\":\"周报/f[\\\"12\\\"]发了 a//b\",\"font\":{\"size\":12}}")
          == "周报[表情]发了 a/b");
    //系统提示、文件、文件夹、共享卡片
    CHECK(BuildContentSearchText(message_type_system, "{\"text\":\"张三加入了群聊\"}") == "张三加入了群聊");
    CHECK(BuildContentSearchText(message_type_file, "{\"fileid\":\"1\",\"filename\":\"季度报告.pdf\",\"filesize\":10}")
          == "季度报告.pdf");
    CHECK(BuildContentSearchText(message_type_folder, "{\"sessionid\":\"s\",\"foldername\":\"设计稿\"}") == "设计稿");
    CHECK(BuildContentSearchText(message_type_fileshare, "{\"shareid\":\"1\",\"name\":\"项目资料\"}") == "项目资料");
    //接龙：有说明时标题加空格加说明
    CHECK(BuildContentSearchText(message_type_relay, "{\"title\":\"周五聚餐\",\"note\":\"自带饮料\"}") == "周五聚餐 自带饮料");
    CHECK(BuildContentSearchText(message_type_relay, "{\"title\":\"周五聚餐\"}") == "周五聚餐");
    //合并转发：标题加子消息，嵌套一层也展开；没有文字的子消息跳过
    const std::string merged =
        "{\"title\":\"聊天记录\",\"messages\":["
        "{\"msgtype\":1,\"content\":{\"text\":\"第一句\"}},"
        "{\"msgtype\":2,\"content\":{\"fileid\":\"9\"}},"
        "{\"msgtype\":15,\"content\":{\"title\":\"内层\",\"messages\":[{\"msgtype\":3,\"content\":{\"filename\":\"a.txt\"}}]}}"
        "]}";
    CHECK(BuildContentSearchText(message_type_merged, merged) == "聊天记录 第一句 内层 a.txt");
    //图片等类型、解析失败、不是对象：空串
    CHECK(BuildContentSearchText(message_type_image, "{\"fileid\":\"1\",\"md5\":\"x\"}").empty());
    CHECK(BuildContentSearchText(message_type_text, "not json").empty());
    CHECK(BuildContentSearchText(message_type_text, "[1,2]").empty());

    //打包内容（入库时的格式）：jsoncpp 把中文写成 \uXXXX，取出来是正常的 UTF-8
    const std::string stored = MessageProtocol::buildStoredContent("{\"text\":\"中文正文\"}", std::vector<int64_t>(), false, "");
    CHECK(stored.find("\\u") != std::string::npos);
    CHECK(BuildStoredSearchText(message_type_text, stored) == "中文正文");
    CHECK(BuildStoredSearchText(message_type_text, "{}").empty());
}

TEST_CASE("STX2 去掉四字节字符与按字符截断")
{
    //表情 U+1F600（四字节）去掉，中文与 ASCII 保留
    CHECK(StripFourByteUtf8("好\xF0\x9F\x98\x80" "a") == "好a");
    //残缺序列与孤立的续字节去掉
    CHECK(StripFourByteUtf8("a\xE5\xA5" "b") == "ab");
    CHECK(StripFourByteUtf8("\x80" "c") == "c");
    CHECK(StripFourByteUtf8("中文 OK") == "中文 OK");

    //「中文」各三个字节：截到 4 字节只剩一个字，截到 6 字节两个字都在
    CHECK(TruncateUtf8("中文", 4) == "中");
    CHECK(TruncateUtf8("中文", 6) == "中文");
    CHECK(TruncateUtf8("中文", 2).empty());
    CHECK(TruncateUtf8("abc", 10) == "abc");

    //过长的文字截到上限以内
    std::string longText;
    for (size_t i = 0; i < kSearchTextMaxBytes; ++i)
        longText += "长";
    const std::string built = BuildContentSearchText(message_type_text, "{\"text\":\"" + longText + "\"}");
    CHECK(built.size() <= kSearchTextMaxBytes);
    CHECK(built.size() + 3 > kSearchTextMaxBytes);
}

TEST_CASE("STX3 会话内搜索查搜索文本：中文、键名、撤回、入群时间、存量行")
{
    testdb::resetDb(kStxDbPath, kStxMysqlDb);

    const int64_t m1 = SaveContent(conversation_group, kUserA, kGroupG, message_type_text,
                                   "{\"text\":\"明天交设计稿\",\"font\":{\"size\":12}}", 1);
    const int64_t m2 = SaveContent(conversation_group, kUserB, kGroupG, message_type_file,
                                   "{\"fileid\":\"7\",\"filename\":\"设计稿终版.psd\",\"filesize\":1}", 2);
    const int64_t m3 = SaveContent(conversation_group, kUserA, kGroupG, message_type_text,
                                   "{\"text\":\"撤回的设计稿\"}", 3);
    REQUIRE(Singleton<MessageManager>::Instance().recallMessage(m3, kUserA));
    CHECK(ReadSearchText(m1) == "明天交设计稿");

    //中文关键字命中文字与文件名；已撤回的不返回
    CHECK(SearchGroup(kUserB, "设计稿", 0) == Ids(m1, m2));
    //英文键名不再命中（此前对 f_content 搜 font 会命中所有带字体的文字消息）
    CHECK(SearchGroup(kUserB, "font", 0).empty());
    CHECK(SearchGroup(kUserB, "filename", 0).empty());
    //入群时间之前的不返回：时间下界取第二条的时间
    CHECK(SearchGroup(kUserB, "设计稿", kT0 + 2 * kStepMs) == Ids(m2));

    //还没回填的存量行按 f_content 照样搜得到（只能是 ASCII，与升级前的行为相同）
    const int64_t legacy = InsertLegacyText(m3 + 100, "legacy plan", 4);
    CHECK(ReadSearchText(legacy) == "<NULL>");
    CHECK(SearchGroup(kUserB, "plan", 0) == Ids(legacy));

    //关键字里的 % _ ! 按普通字符匹配：搜「50%」不命中「500」
    const int64_t pct = SaveContent(conversation_group, kUserA, kGroupG, message_type_text, "{\"text\":\"增长 50% 了\"}", 5);
    SaveContent(conversation_group, kUserA, kGroupG, message_type_text, "{\"text\":\"增长 500 台\"}", 6);
    CHECK(SearchGroup(kUserB, "50%", 0) == Ids(pct));
    CHECK(SearchGroup(kUserB, "增长_5", 0).empty());

    testdb::cleanupDb(kStxDbPath);
}

TEST_CASE("STX4 存量行分批回填，改写内容后搜索文本跟着更新")
{
    testdb::resetDb(kStxDbPath, kStxMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //三条存量行（f_search_text 为 NULL），一条新行（已有搜索文本，不参与回填）
    const int64_t a = InsertLegacyText(500, "alpha", 1);
    const int64_t b = InsertLegacyText(501, "beta", 2);
    const int64_t c = InsertLegacyText(502, "gamma", 3);
    const int64_t fresh = SaveContent(conversation_group, kUserA, kGroupG, message_type_text, "{\"text\":\"新消息\"}", 4);

    //每批 2 行：第一批 2 行、第二批 1 行，之后没有待回填的行
    const int kBatch = 2;
    CHECK(mgr.backfillSearchText(kBatch) == 2);
    CHECK(ReadSearchText(a) == "alpha");
    CHECK(ReadSearchText(b) == "beta");
    CHECK(ReadSearchText(c) == "<NULL>");
    CHECK(mgr.backfillSearchText(kBatch) == 1);
    CHECK(ReadSearchText(c) == "gamma");
    CHECK(mgr.backfillSearchText(kBatch) == 0);
    CHECK(ReadSearchText(fresh) == "新消息");
    CHECK(mgr.backfillSearchText(0) == -1);

    //改写内容：搜索文本换成新内容的
    const std::string stored = MessageProtocol::buildStoredContent("{\"text\":\"改写后的正文\"}", std::vector<int64_t>(), false, "");
    REQUIRE(mgr.updateMessageContent(fresh, stored));
    CHECK(ReadSearchText(fresh) == "改写后的正文");
    CHECK(SearchGroup(kUserB, "改写后", 0) == Ids(fresh));
    CHECK(SearchGroup(kUserB, "新消息", 0).empty());

    testdb::cleanupDb(kStxDbPath);
}
