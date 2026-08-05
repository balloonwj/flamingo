/**
 *  v2 历史消息本地库——查询纯逻辑单元测试（Suite V2Q），V2MsgQueryTests.cpp
 *
 *  覆盖：URL 识别、关键字搜索文本构造、内容 JSON 归一、时间区间换算、类型筛选到
 *  SQL 片段、COUNT/分页 SQL 拼装、MessageNotify→入库记录转换。纯逻辑、不触数据库。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../BalloonHistory/V2MsgQuery.h"
#include "../net/MessageProtocolClient.h"   // net::MessageNotify
#include "../net/Protocol.h"                // ::message_type_*
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace balloonhistory;

//把 JSON 文本解析成 Json::Value，供断言检查 BuildContentJson 产物。
static bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("V2Q1 IsLink 识别 http/https 且大小写不敏感")
{
    CHECK(IsLink("see http://a.com"));
    CHECK(IsLink("https://b.com/x"));
    CHECK(IsLink("访问 HTTPS://C.com 看看"));
    CHECK_FALSE(IsLink("没有链接的普通文本"));
    CHECK_FALSE(IsLink(""));
}

TEST_CASE("V2Q2 BuildSearchText 文本取正文/文件取文件名/图片为空")
{
    net::MessageNotify t;
    t.m_msgtype = ::message_type_text;
    t.m_text = "hello world";
    CHECK(BuildSearchText(t) == "hello world");

    //文本里的内联表情 /f["id"] 净化成 [表情]，不暴露原始标记
    net::MessageNotify face;
    face.m_msgtype = ::message_type_text;
    face.m_text = "你好/f[\"3\"]";
    CHECK(BuildSearchText(face) == "你好[表情]");

    net::MessageNotify f;
    f.m_msgtype = ::message_type_file;
    f.m_fileName = "季度报告.docx";
    CHECK(BuildSearchText(f) == "季度报告.docx");

    net::MessageNotify img;
    img.m_msgtype = ::message_type_image;
    CHECK(BuildSearchText(img).empty());
}

TEST_CASE("V2Q8 DigestFaceTokens 表情净化与反转义")
{
    CHECK(DigestFaceTokens("改了发你看下/f[\"5\"]好") == "改了发你看下[表情]好");
    CHECK(DigestFaceTokens("/f[\"0\"]/f[\"142\"]") == "[表情][表情]");
    CHECK(DigestFaceTokens("a//b") == "a/b");   // 转义斜杠还原
    CHECK(DigestFaceTokens("no token") == "no token");
    CHECK(DigestFaceTokens("") == "");
    //残缺 / 非数字标记当普通文本，不吞正文
    CHECK(DigestFaceTokens("/f[\"x\"]end") == "/f[\"x\"]end");
}

TEST_CASE("V2Q3 BuildContentJson 文本/图片/文件三类字段完整且可解析")
{
    //文本带字体
    net::MessageNotify t;
    t.m_msgtype = ::message_type_text;
    t.m_text = "hi";
    t.m_font.m_valid = true;
    t.m_font.m_size = 18;
    t.m_font.m_bold = true;
    t.m_font.m_colorRgb = 0x112233;
    Json::Value rt;
    REQUIRE(ParseJson(BuildContentJson(t), rt));
    CHECK(rt["text"].asString() == "hi");
    CHECK(rt["font"]["size"].asInt() == 18);
    CHECK(rt["font"]["bold"].asBool());
    CHECK(rt["font"]["color"].asInt() == 0x112233);

    //图片：64 位 size 以字符串编码，不丢精度
    net::MessageNotify img;
    img.m_msgtype = ::message_type_image;
    img.m_imageFileId = "fid";
    img.m_imageMd5 = "md5";
    img.m_imageWidth = 100;
    img.m_imageHeight = 200;
    img.m_imageSize = 123456789012LL;
    Json::Value ri;
    REQUIRE(ParseJson(BuildContentJson(img), ri));
    CHECK(ri["image"]["fileid"].asString() == "fid");
    CHECK(ri["image"]["w"].asInt() == 100);
    CHECK(ri["image"]["h"].asInt() == 200);
    CHECK(ri["image"]["size"].asString() == "123456789012");

    //文件
    net::MessageNotify f;
    f.m_msgtype = ::message_type_file;
    f.m_fileFileId = "ffid";
    f.m_fileName = "a.zip";
    f.m_fileSize = 2048;
    f.m_fileMd5 = "m";
    Json::Value rf;
    REQUIRE(ParseJson(BuildContentJson(f), rf));
    CHECK(rf["file"]["name"].asString() == "a.zip");
    CHECK(rf["file"]["size"].asString() == "2048");
    CHECK(rf["file"]["fileid"].asString() == "ffid");
}

TEST_CASE("V2Q4 TimeRangeToBounds 各档边界正确")
{
    const int64_t now = 1000000000000LL;
    const int64_t msPerDay = 24LL * 60LL * 60LL * 1000LL;
    int64_t mn = 0;
    int64_t mx = 0;

    TimeRangeToBounds(kHistoryTimeAll, now, 0, 0, mn, mx);
    CHECK(mn == 0);
    CHECK(mx == INT64_MAX);

    TimeRangeToBounds(kHistoryTime7Days, now, 0, 0, mn, mx);
    CHECK(mx == now);
    CHECK(mn == now - 7 * msPerDay);

    TimeRangeToBounds(kHistoryTime30Days, now, 0, 0, mn, mx);
    CHECK(mn == now - 30 * msPerDay);

    //“3个月/半年/一年”按固定天数折算（90/180/365），不做日历意义上的月份加减 ——
    //口径与“近 30 天”一致（30 天 ≠ 一个自然月），见 V2MsgQuery.cpp 里那几个常量的注释。
    TimeRangeToBounds(kHistoryTime3Months, now, 0, 0, mn, mx);
    CHECK(mx == now);
    CHECK(mn == now - 90 * msPerDay);

    TimeRangeToBounds(kHistoryTime6Months, now, 0, 0, mn, mx);
    CHECK(mx == now);
    CHECK(mn == now - 180 * msPerDay);

    TimeRangeToBounds(kHistoryTime1Year, now, 0, 0, mn, mx);
    CHECK(mx == now);
    CHECK(mn == now - 365 * msPerDay);

    //各档回溯的时间必须严格由近及远，顺序错了界面上“越选越久”的直觉就断了。
    int64_t mn7 = 0, mn30 = 0, mn90 = 0, mn180 = 0, mn365 = 0, ignored = 0;
    TimeRangeToBounds(kHistoryTime7Days,   now, 0, 0, mn7,   ignored);
    TimeRangeToBounds(kHistoryTime30Days,  now, 0, 0, mn30,  ignored);
    TimeRangeToBounds(kHistoryTime3Months, now, 0, 0, mn90,  ignored);
    TimeRangeToBounds(kHistoryTime6Months, now, 0, 0, mn180, ignored);
    TimeRangeToBounds(kHistoryTime1Year,   now, 0, 0, mn365, ignored);
    CHECK(mn7 > mn30);
    CHECK(mn30 > mn90);
    CHECK(mn90 > mn180);
    CHECK(mn180 > mn365);

    TimeRangeToBounds(kHistoryTimeCustom, now, 111, 222, mn, mx);
    CHECK(mn == 111);
    CHECK(mx == 222);
}

TEST_CASE("V2Q5 TypeFilterToSqlFragment 各胶囊映射正确")
{
    CHECK(TypeFilterToSqlFragment(kHistoryTypeAll).empty());
    CHECK(TypeFilterToSqlFragment(kHistoryTypeText) == "msgtype=1 AND is_link=0");
    CHECK(TypeFilterToSqlFragment(kHistoryTypeImage) == "msgtype IN (2,9)");
    CHECK(TypeFilterToSqlFragment(kHistoryTypeFile) == "msgtype=3");
    CHECK(TypeFilterToSqlFragment(kHistoryTypeVoice) == "msgtype=4");
    CHECK(TypeFilterToSqlFragment(kHistoryTypeLink) == "is_link=1");
    //Pin 分类只看 pinned 列，不带任何内容类型条件 —— 胶囊是单选的，选了它就是
    //"本会话全部被钉的消息，不分类型"
    CHECK(TypeFilterToSqlFragment(kHistoryTypePinned) == "pinned=1");
}

TEST_CASE("V2Q6 BuildCountSql/BuildPageSql 拼装正确")
{
    //基础：仅会话条件
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 123;
    f.m_page = 1;
    f.m_pageSize = 20;
    CHECK(BuildCountSql(f, 0) ==
        "SELECT COUNT(*) FROM t_msg WHERE ctype=1 AND cid=123 AND recalled=0");
    CHECK(BuildPageSql(f, 0) ==
        "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,"
        "pinned,pin_time "
        "FROM t_msg WHERE ctype=1 AND cid=123 AND recalled=0 "
        "ORDER BY timestamp DESC, msgid DESC LIMIT 20 OFFSET 0");

    //类型 + 关键字 + 分页偏移
    HistoryFilter g;
    g.m_ctype = 2;
    g.m_cid = 777;
    g.m_type = kHistoryTypeImage;
    g.m_keyword = "cat";
    g.m_page = 3;
    g.m_pageSize = 10;
    CHECK(BuildCountSql(g, 0) ==
        "SELECT COUNT(*) FROM t_msg WHERE ctype=2 AND cid=777 AND recalled=0 "
        "AND msgtype IN (2,9) AND search_text LIKE ?");
    CHECK(BuildPageSql(g, 0) ==
        "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,"
        "pinned,pin_time "
        "FROM t_msg WHERE ctype=2 AND cid=777 AND recalled=0 AND msgtype IN (2,9) AND search_text LIKE ? "
        "ORDER BY timestamp DESC, msgid DESC LIMIT 10 OFFSET 20");

    //近7天时间范围内联进 SQL
    HistoryFilter h;
    h.m_ctype = 1;
    h.m_cid = 5;
    h.m_range = kHistoryTime7Days;
    h.m_page = 1;
    h.m_pageSize = 20;
    const int64_t now = 1000000000000LL;
    const int64_t minMs = now - 7LL * 24 * 60 * 60 * 1000;
    std::string expect = "SELECT COUNT(*) FROM t_msg WHERE ctype=1 AND cid=5 AND recalled=0 "
        "AND timestamp>=" + std::to_string(minMs) + " AND timestamp<=" + std::to_string(now);
    CHECK(BuildCountSql(h, now) == expect);
}

TEST_CASE("V2Q7 RecordFromNotify 字段与会话归属正确")
{
    //单聊文本（含URL）：cid=对端、is_link=true、searchText=正文
    net::MessageNotify n;
    n.m_msgid = 9;
    n.m_clientmsgid = "c";
    n.m_from = 100;
    n.m_to = 200;
    n.m_msgtype = ::message_type_text;
    n.m_text = "hi http://x.com";
    n.m_timestamp = 42;
    n.m_recalled = false;
    HistoryRecord r = RecordFromNotify(n, 1, 200);
    CHECK(r.m_msgid == 9);
    CHECK(r.m_ctype == 1);
    CHECK(r.m_cid == 200);
    CHECK(r.m_fromid == 100);
    CHECK(r.m_msgtype == ::message_type_text);
    CHECK(r.m_isLink);
    //搜索文本经 DigestFaceTokens 净化，与气泡 ParseSendBody 解码一致（转义斜杠 //→/）。
    CHECK(r.m_searchText == "hi http:/x.com");

    //群聊图片：cid=群id、非链接
    net::MessageNotify g;
    g.m_msgid = 5;
    g.m_from = 300;
    g.m_msgtype = ::message_type_image;
    HistoryRecord rg = RecordFromNotify(g, 2, 9999);
    CHECK(rg.m_ctype == 2);
    CHECK(rg.m_cid == 9999);
    CHECK_FALSE(rg.m_isLink);
}

// ---------------------------------------------------------------------------
// V2Q9-V2Q11  Pin 分类的 SQL 拼装。方案见 docs/消息Pin接入方案.md §6.6。
// ---------------------------------------------------------------------------

TEST_CASE("V2Q9 Pin 分类的 WHERE 同时含 pinned=1 与 recalled=0")
{
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 123;
    f.m_type = kHistoryTypePinned;
    f.m_page = 1;
    f.m_pageSize = 20;

    //recalled=0 是所有查询的恒定条件：被撤回的消息即便本地还留着 pinned=1，
    //也不该出现在 Pin 分类里。服务端撤回时会顺手删掉 Pin 行，两边口径一致
    CHECK(BuildCountSql(f, 0) ==
        "SELECT COUNT(*) FROM t_msg WHERE ctype=1 AND cid=123 AND recalled=0 AND pinned=1");
    CHECK(BuildPageSql(f, 0) ==
        "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,"
        "pinned,pin_time "
        "FROM t_msg WHERE ctype=1 AND cid=123 AND recalled=0 AND pinned=1 "
        "ORDER BY timestamp DESC, msgid DESC LIMIT 20 OFFSET 0");
}

TEST_CASE("V2Q10 Pin 分类可与关键字、自定义时间区间叠加")
{
    HistoryFilter f;
    f.m_ctype = 2;
    f.m_cid = 777;
    f.m_type = kHistoryTypePinned;
    f.m_range = kHistoryTimeCustom;
    f.m_customMinMs = 1000;
    f.m_customMaxMs = 2000;
    f.m_keyword = "cat";
    f.m_page = 2;
    f.m_pageSize = 10;

    //类型胶囊与关键字 / 时间是两个维度，选了 Pin 之后搜索框与时间下拉照常生效
    CHECK(BuildCountSql(f, 0) ==
        "SELECT COUNT(*) FROM t_msg WHERE ctype=2 AND cid=777 AND recalled=0 "
        "AND pinned=1 AND timestamp>=1000 AND timestamp<=2000 AND search_text LIKE ?");
    CHECK(BuildPageSql(f, 0) ==
        "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,"
        "pinned,pin_time "
        "FROM t_msg WHERE ctype=2 AND cid=777 AND recalled=0 "
        "AND pinned=1 AND timestamp>=1000 AND timestamp<=2000 AND search_text LIKE ? "
        "ORDER BY timestamp DESC, msgid DESC LIMIT 10 OFFSET 10");
}

TEST_CASE("V2Q11 Pin 分类下 Count 与 Page 的 WHERE 完全一致，排序仍是消息时间倒序")
{
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 55;
    f.m_type = kHistoryTypePinned;
    f.m_page = 1;
    f.m_pageSize = 5;

    //两条 SQL 的 WHERE 一旦对不上，底部"共 N 条"就会与列表里实际的行数不符
    const std::string countSql = BuildCountSql(f, 0);
    const std::string pageSql = BuildPageSql(f, 0);
    const std::string marker = " WHERE ";
    const std::string countWhere = countSql.substr(countSql.find(marker) + marker.size());
    const std::string pageTail = pageSql.substr(pageSql.find(marker) + marker.size());
    const std::string pageWhere = pageTail.substr(0, pageTail.find(" ORDER BY "));
    CHECK(countWhere == pageWhere);

    //排序沿用消息时间倒序，与其余六个分类一致 —— 不是"最近钉的排最前"。
    //切换胶囊时排序规则不该突然变一个样
    CHECK(pageSql.find("ORDER BY timestamp DESC, msgid DESC") != std::string::npos);
    CHECK(pageSql.find("pin_time DESC") == std::string::npos);
}
