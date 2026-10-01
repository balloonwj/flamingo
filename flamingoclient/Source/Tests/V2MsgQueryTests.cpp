/**
 *  v2 历史消息本地库——查询纯逻辑单元测试（Suite V2Q），V2MsgQueryTests.cpp
 *
 *  覆盖：URL 识别、关键字搜索文本构造、内容 JSON 归一、时间区间换算、类型筛选到
 *  SQL 片段、COUNT/分页 SQL 拼装、MessageNotify→入库记录转换；合并转发的聊天记录
 *  （msgtype=15）的内容归一与搜索文本（FWD-C5，见 docs/聊天消息转发方案.md）。
 *  纯逻辑、不触数据库。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../net/MessageProtocolClient.h"   // net::MessageNotify
#include "../net/Protocol.h"                // ::message_type_*
#include "../third-party/jsoncpp-1.9.0/json.h"

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

// ===================================================================
// 文件夹传输记录条（msgtype = message_type_folder = 18）落进本地历史库的那一段
// ===================================================================

namespace {

//造一条文件夹传输记录条的 MessageNotify（服务端在传输走到终态后推下来的那种）。
static net::MessageNotify MakeFolderNotify(const std::string& sessionId,
                                           const std::string& folderName,
                                           long long dirs, long long files,
                                           long long bytes, int result, int errCode)
{
    net::MessageNotify notify;
    notify.m_msgid   = 90001;
    notify.m_ctype   = 1;
    notify.m_cid     = 20002;
    notify.m_from    = 10001;
    notify.m_to      = 20002;
    notify.m_msgtype = ::message_type_folder;
    notify.m_timestamp = 1755100000000LL;

    notify.m_folderSessionId  = sessionId;
    notify.m_folderName       = folderName;
    notify.m_folderTotalDirs  = dirs;
    notify.m_folderTotalFiles = files;
    notify.m_folderTotalSize  = bytes;
    notify.m_folderResult     = result;
    notify.m_folderErrCode    = errCode;
    return notify;
}

} // namespace

TEST_CASE("V2Q12 BuildSearchText 对文件夹记录返回文件夹名")
{
    //用户回头找的是"我给谁发过那个叫 xxx 的文件夹"，故搜索文本取文件夹名
    const net::MessageNotify notify =
        MakeFolderNotify("sess-1", u8"三期设计稿", 12, 128, 2576980378LL,
                         ::folder_state_completed, 0);
    CHECK(BuildSearchText(notify) == u8"三期设计稿");

    //文件夹名为空时退化成空串，不至于把别的字段拿去当搜索文本
    const net::MessageNotify noName =
        MakeFolderNotify("sess-2", "", 0, 0, 0, ::folder_state_completed, 0);
    CHECK(BuildSearchText(noName).empty());
}

TEST_CASE("V2Q13 BuildContentJson 对文件夹记录写 folder 子对象、且不含文件清单")
{
    const net::MessageNotify notify =
        MakeFolderNotify("sess-8811", u8"项目资料", 12, 128, 2576980378LL,
                         ::folder_state_completed, 0);

    Json::Value root;
    REQUIRE(ParseJson(BuildContentJson(notify), root));
    REQUIRE(root.isMember("folder"));
    const Json::Value& folder = root["folder"];
    REQUIRE(folder.isObject());

    //字段名须与线上那份逐字相同 —— 历史行要按 sessionid 去查本地落点登记
    CHECK(folder["sessionid"].asString() == "sess-8811");
    CHECK(folder["foldername"].asString() == u8"项目资料");
    CHECK(folder["totaldirs"].asInt64() == 12);
    CHECK(folder["totalfiles"].asInt64() == 128);
    CHECK(folder["totalsize"].asInt64() == 2576980378LL);
    CHECK(folder["result"].asInt() == ::folder_state_completed);
    CHECK(folder["errcode"].asInt() == 0);

    //"记动作、不记内容"：落库这一份同样不许出现文件清单与 MD5
    CHECK(folder.getMemberNames().size() == 7);
    CHECK_FALSE(folder.isMember("files"));
    CHECK_FALSE(folder.isMember("md5"));
}

TEST_CASE("V2Q14 文件夹记录的失败原因与超大字节数落库不丢失")
{
    const long long kFiftyGiB = 53687091200LL;
    const net::MessageNotify failed =
        MakeFolderNotify("sess-fail", u8"归档", 3, 40, kFiftyGiB,
                         ::folder_state_failed, ::error_code_folder_write_fail);

    Json::Value root;
    REQUIRE(ParseJson(BuildContentJson(failed), root));
    CHECK(root["folder"]["totalsize"].asInt64() == kFiftyGiB);
    CHECK(root["folder"]["result"].asInt() == ::folder_state_failed);
    CHECK(root["folder"]["errcode"].asInt() == ::error_code_folder_write_fail);
}

TEST_CASE("V2Q15 文件夹记录 content 不可用时退化成空的 folder 子对象而不是崩掉")
{
    //m_folderResult 为 0（推送里的 content 当初就没解析出来）：构造侧会拒绝，
    //本函数据此写一个空对象，历史行退化成只显示 "[文件夹]"
    net::MessageNotify broken;
    broken.m_msgid   = 1;
    broken.m_from    = 10001;
    broken.m_msgtype = ::message_type_folder;

    Json::Value root;
    REQUIRE(ParseJson(BuildContentJson(broken), root));
    REQUIRE(root.isMember("folder"));
    CHECK(root["folder"].isObject());
    CHECK(root["folder"].getMemberNames().empty());
}

TEST_CASE("V2Q16 RecordFromNotify 对文件夹记录：类型 / 搜索文本正确且不算链接")
{
    const net::MessageNotify notify =
        MakeFolderNotify("sess-1", u8"三期设计稿", 12, 128, 1024,
                         ::folder_state_completed, 0);
    const HistoryRecord rec = RecordFromNotify(notify, 1, 10001);

    CHECK(rec.m_msgtype == ::message_type_folder);
    CHECK(rec.m_searchText == u8"三期设计稿");
    //"链接"只对文本消息有意义，文件夹名里即便出现 http:// 也不该被算成链接
    CHECK_FALSE(rec.m_isLink);
    CHECK(rec.m_msgid == notify.m_msgid);
    CHECK(rec.m_timestamp == notify.m_timestamp);
}

TEST_CASE("V2Q17 类型筛选未新增「文件夹」一档，SQL 片段维持原样")
{
    //本轮<u>刻意不加</u>文件夹胶囊（理由见 docs/单聊文件夹传输方案.md 7.4）：
    //记录条一次传输才一条、频次极低，而胶囊行是单选且宽度按"两字"排的，
    //加一档三字胶囊要改整行布局，代价与收益不成比例。文件夹记录仍可经「全部」
    //浏览、并按文件夹名被关键字搜到。
    //这条用例守的是"没有人顺手把它塞进已有的某一档"——尤其别混进「文件」，
    //那会让文件筛选里冒出一堆下载不了的行。
    const std::string fileFrag = TypeFilterToSqlFragment(kHistoryTypeFile);
    CHECK(fileFrag == "msgtype=3");
    CHECK(fileFrag.find("18") == std::string::npos);
    CHECK(TypeFilterToSqlFragment(kHistoryTypeAll).empty());
}

TEST_CASE("V2Q18 显示下界：设了下界时多出 msgid>=N，Pin 分类不加，为 0 时与原来相同", "[coverage]")
{
    const int64_t kNowMs = 1700000000000LL;

    HistoryFilter base;
    base.m_ctype = 1;
    base.m_cid = 10001;
    const std::string original = BuildCountSql(base, kNowMs);
    CHECK(original.find("msgid>=") == std::string::npos);

    //下界为 0：与不设下界完全相同，现有行为不变
    HistoryFilter zero = base;
    zero.m_minMsgid = 0;
    CHECK(BuildCountSql(zero, kNowMs) == original);

    //设了下界：计数与分页两条 SQL 都要带上
    HistoryFilter bounded = base;
    bounded.m_minMsgid = 500;
    CHECK(BuildCountSql(bounded, kNowMs).find(" AND msgid>=500") != std::string::npos);
    CHECK(BuildPageSql(bounded, kNowMs).find(" AND msgid>=500") != std::string::npos);

    //Pin 分类：服务端单独给的全集，不受下界限制
    HistoryFilter pinned = bounded;
    pinned.m_type = kHistoryTypePinned;
    CHECK(BuildCountSql(pinned, kNowMs).find("msgid>=") == std::string::npos);
    CHECK(BuildPageSql(pinned, kNowMs).find("msgid>=") == std::string::npos);
}

//构造一条合并记录（msgtype=15）：内含文字（带内联表情）、文件、以及一条嵌套的合并记录
static net::MessageNotify MakeMergedNotify()
{
    net::MessageNotify n;
    n.m_msgid = 9007199254741100LL;
    n.m_from = 100001;
    n.m_msgtype = ::message_type_merged;
    n.m_timestamp = 1716268800000LL;
    n.m_mergedContent =
        u8"{\"title\":\"项目周会的聊天记录\",\"messages\":["
        u8"{\"msgid\":\"501\",\"from\":\"100003\",\"msgtype\":1,"
        u8"\"content\":{\"text\":\"排期定了/f[\\\"12\\\"]\"},\"timestamp\":1,\"fromname\":\"张三\"},"
        u8"{\"msgid\":\"502\",\"from\":\"100004\",\"msgtype\":3,"
        u8"\"content\":{\"fileid\":\"77\",\"filename\":\"排期表.xlsx\",\"filesize\":10,\"md5\":\"m\"},\"timestamp\":2},"
        u8"{\"msgid\":\"503\",\"from\":\"100001\",\"msgtype\":15,\"timestamp\":3,"
        u8"\"content\":{\"title\":\"内层记录\",\"messages\":["
        u8"{\"msgid\":\"401\",\"from\":\"100005\",\"msgtype\":1,\"content\":{\"text\":\"内层的话\"},\"timestamp\":0}]}}]}";
    return n;
}

TEST_CASE("FWD-C5 类型 15 落本地库：content 存为 merged 子对象，搜索文本含标题与子消息文字", "[forward]")
{
    const net::MessageNotify n = MakeMergedNotify();

    //content：{"merged": 原 content}，标题与子消息原样保留
    Json::Value root;
    REQUIRE(ParseJson(BuildContentJson(n), root));
    REQUIRE(root["merged"].isObject());
    CHECK(root["merged"]["title"].asString() == u8"项目周会的聊天记录");
    REQUIRE(root["merged"]["messages"].isArray());
    CHECK(root["merged"]["messages"].size() == 3);
    CHECK(root["merged"]["messages"][0]["fromname"].asString() == u8"张三");

    //搜索文本：标题、文字子消息（内联表情已净化）、文件名、嵌套记录的标题与文字都在里面
    const std::string text = BuildSearchText(n);
    CHECK(text.find(u8"项目周会的聊天记录") != std::string::npos);
    CHECK(text.find(u8"排期定了[表情]") != std::string::npos);
    CHECK(text.find("/f[") == std::string::npos);
    CHECK(text.find(u8"排期表.xlsx") != std::string::npos);
    CHECK(text.find(u8"内层记录") != std::string::npos);
    CHECK(text.find(u8"内层的话") != std::string::npos);

    //RecordFromNotify：类型正确、不算链接
    const HistoryRecord rec = RecordFromNotify(n, 1, 100002);
    CHECK(rec.m_msgtype == ::message_type_merged);
    CHECK_FALSE(rec.m_isLink);
    CHECK(rec.m_searchText == text);

    //content 不可用时退化成空的 merged 子对象、搜索文本为空，而不是崩掉
    net::MessageNotify broken = n;
    broken.m_mergedContent = "not json";
    Json::Value brokenRoot;
    REQUIRE(ParseJson(BuildContentJson(broken), brokenRoot));
    CHECK(brokenRoot["merged"].isObject());
    CHECK(brokenRoot["merged"].size() == 0);
    CHECK(BuildSearchText(broken).empty());
}
