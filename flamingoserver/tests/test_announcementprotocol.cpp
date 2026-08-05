/**
 *  平台公告投递报文编解码单元测试（Suite PA），test_announcementprotocol.cpp
 *
 *  覆盖 cmd_get_announcements / cmd_mark_announcement_read / cmd_announcement_notify
 *  三条报文的解析与构造。
 *
 *  注意与<u>群公告</u>区分：test_groupprotocol.cpp 里也有名为 parseGetAnnouncementsRequest
 *  的用例，那些测的是群公告(cmd_get_group_announcements)，与本文件测的平台公告是两套
 *  完全独立的报文。本文件的用例一律以「PA」前缀 + 「平台公告」字样标识。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "doctest.h"
#include "../chatserversrc/AnnouncementProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

#include <string.h>
#include <time.h>

//把 JSON 文本解析成 Json::Value，供断言检查。
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("PA1 平台公告 parseGetAnnouncementsRequest 两个字段均可缺省")
{
    GetAnnouncementsReqData req;

    //空对象是合法请求：客户端首次登录时就这么发，表示「全量拉取、用默认条数」。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest("{}", req));
    CHECK(req.sinceId == 0);
    CHECK(req.limit == ANNOUNCEMENT_FETCH_LIMIT_DEFAULT);

    //非法 JSON 与非对象顶层一律拒绝。
    CHECK_FALSE(AnnouncementProtocol::parseGetAnnouncementsRequest("not json", req));
    CHECK_FALSE(AnnouncementProtocol::parseGetAnnouncementsRequest("[1,2,3]", req));
    CHECK_FALSE(AnnouncementProtocol::parseGetAnnouncementsRequest("", req));
}

TEST_CASE("PA2 平台公告 sinceid 兼容字符串与数字两种写法")
{
    GetAnnouncementsReqData req;

    //v2 约定 64 位 ID 用字符串承载，这是主用法。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest(
        "{\"sinceid\":\"10023\"}", req));
    CHECK(req.sinceId == 10023);

    //也兼容直接写数字（Android 端历史实现有过这种写法）。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest(
        "{\"sinceid\":10024}", req));
    CHECK(req.sinceId == 10024);

    //负数按 0 处理，不因此拒绝整个请求。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest(
        "{\"sinceid\":-5}", req));
    CHECK(req.sinceId == 0);
}

TEST_CASE("PA3 平台公告 limit 被规整到 [1, 上限]")
{
    GetAnnouncementsReqData req;

    //正常值原样保留。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest("{\"limit\":20}", req));
    CHECK(req.limit == 20);

    //0 与负数都退回默认值。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest("{\"limit\":0}", req));
    CHECK(req.limit == ANNOUNCEMENT_FETCH_LIMIT_DEFAULT);
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest("{\"limit\":-1}", req));
    CHECK(req.limit == ANNOUNCEMENT_FETCH_LIMIT_DEFAULT);

    //超过硬上限时截断——这是防止一次拉取产生过大包的兜底。
    REQUIRE(AnnouncementProtocol::parseGetAnnouncementsRequest("{\"limit\":99999}", req));
    CHECK(req.limit == ANNOUNCEMENT_FETCH_LIMIT_MAX);
}

TEST_CASE("PA4 平台公告 parseMarkReadRequest 要求 announcementid 为正")
{
    int64_t id = 0;

    REQUIRE(AnnouncementProtocol::parseMarkReadRequest("{\"announcementid\":\"10023\"}", id));
    CHECK(id == 10023);

    //缺失、零、负数都要拒绝——这三种情况下写库没有意义。
    CHECK_FALSE(AnnouncementProtocol::parseMarkReadRequest("{}", id));
    CHECK_FALSE(AnnouncementProtocol::parseMarkReadRequest("{\"announcementid\":\"0\"}", id));
    CHECK_FALSE(AnnouncementProtocol::parseMarkReadRequest("{\"announcementid\":-3}", id));
    CHECK_FALSE(AnnouncementProtocol::parseMarkReadRequest("bad json", id));
}

TEST_CASE("PA5 平台公告 buildGetAnnouncementsResponse 空列表")
{
    Json::Value root;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildGetAnnouncementsResponse(0, "ok", false, {}), root));

    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["hasmore"].asBool() == false);
    //即便一条都没有，announcements 也必须是数组而不是 null——客户端会无条件遍历它。
    REQUIRE(root["announcements"].isArray());
    CHECK(root["announcements"].size() == 0);
}

TEST_CASE("PA6 平台公告 buildGetAnnouncementsResponse 多条且字段齐全")
{
    std::vector<AnnouncementPushItem> items;

    AnnouncementPushItem a;
    a.announcementId = 10023;
    a.title          = "系统维护通知";
    a.body           = "本周日 02:00 至 04:00 停机维护。";
    a.severity       = announcement_severity_warning;
    a.senderName     = "平台运营";
    a.pinned         = 1;
    a.sentTime       = 1754300000000LL;
    a.read           = 0;
    items.push_back(a);

    AnnouncementPushItem b;
    b.announcementId = 10024;
    b.title          = "新版本已发布";
    b.body           = "请到官网下载。";
    b.severity       = announcement_severity_info;
    b.senderName     = "产品团队";
    b.pinned         = 0;
    b.sentTime       = 1754300100000LL;
    b.read           = 1;
    items.push_back(b);

    Json::Value root;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildGetAnnouncementsResponse(0, "ok", true, items), root));

    CHECK(root["hasmore"].asBool() == true);
    REQUIRE(root["announcements"].size() == 2);

    const Json::Value& j0 = root["announcements"][0];
    //64 位 ID 必须编成字符串，否则 JavaScript 端会丢精度。
    CHECK(j0["announcementid"].asString() == "10023");
    CHECK(j0["title"].asString() == "系统维护通知");
    CHECK(j0["body"].asString() == "本周日 02:00 至 04:00 停机维护。");
    CHECK(j0["severity"].asInt() == announcement_severity_warning);
    CHECK(j0["sendername"].asString() == "平台运营");
    CHECK(j0["pinned"].asInt() == 1);
    //时间戳是数字（Unix 毫秒），不是字符串。
    CHECK(j0["senttime"].asInt64() == 1754300000000LL);
    CHECK(j0["read"].asInt() == 0);

    const Json::Value& j1 = root["announcements"][1];
    CHECK(j1["announcementid"].asString() == "10024");
    CHECK(j1["pinned"].asInt() == 0);
    CHECK(j1["read"].asInt() == 1);
}

TEST_CASE("PA7 平台公告 buildAnnouncementNotify 新发布携带完整内容")
{
    AnnouncementPushItem item;
    item.announcementId = 10025;
    item.title          = "安全提醒";
    item.body           = "请尽快修改初始密码。";
    item.severity       = announcement_severity_notice;
    item.senderName     = "安全团队";
    item.pinned         = 0;
    item.sentTime       = 1754300200000LL;
    item.read           = 0;

    Json::Value root;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildAnnouncementNotify(announcement_change_published, item), root));

    CHECK(root["changetype"].asInt() == announcement_change_published);
    const Json::Value& jo = root["announcement"];
    CHECK(jo["announcementid"].asString() == "10025");
    CHECK(jo["title"].asString() == "安全提醒");
    CHECK(jo["body"].asString() == "请尽快修改初始密码。");
    CHECK(jo["severity"].asInt() == announcement_severity_notice);
    CHECK(jo["sendername"].asString() == "安全团队");
    CHECK(jo["senttime"].asInt64() == 1754300200000LL);
}

TEST_CASE("PA8 平台公告 buildAnnouncementNotify 撤回只带公告ID")
{
    AnnouncementPushItem item;
    item.announcementId = 10025;
    //即便调用方把正文也填上了，撤回通知也不该把它编进去——撤回不需要重复传一遍内容。
    item.title = "安全提醒";
    item.body  = "请尽快修改初始密码。";

    Json::Value root;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildAnnouncementNotify(announcement_change_recalled, item), root));

    CHECK(root["changetype"].asInt() == announcement_change_recalled);
    const Json::Value& jo = root["announcement"];
    CHECK(jo["announcementid"].asString() == "10025");
    CHECK_FALSE(jo.isMember("title"));
    CHECK_FALSE(jo.isMember("body"));
    CHECK_FALSE(jo.isMember("senttime"));
}

TEST_CASE("PA10 平台公告 dateTimeToMs 无法识别的输入一律返回 0")
{
    //空串、纯垃圾、缺字段（只到小时）都识别不了。
    CHECK(AnnouncementProtocol::dateTimeToMs("") == 0);
    CHECK(AnnouncementProtocol::dateTimeToMs("not a date") == 0);
    CHECK(AnnouncementProtocol::dateTimeToMs("2026-08-05") == 0);
    CHECK(AnnouncementProtocol::dateTimeToMs("2026-08-05 14") == 0);

    //datetime 为 NULL 时某些驱动给出的全零串：必须判为 0，
    //否则转出来是个负数时间戳，会让客户端按时间排序时把它排到最前面。
    CHECK(AnnouncementProtocol::dateTimeToMs("0000-00-00 00:00:00") == 0);
}

TEST_CASE("PA11 平台公告 dateTimeToMs 秒部分可以缺省")
{
    //管理控制台传来的定时时间是分钟精度的，库里存的是秒精度的，两种都要能认，
    //且秒为 00 时两者必须得到同一个结果。
    const int64_t withSeconds    = AnnouncementProtocol::dateTimeToMs("2026-08-05 14:30:00");
    const int64_t withoutSeconds = AnnouncementProtocol::dateTimeToMs("2026-08-05 14:30");

    REQUIRE(withSeconds > 0);
    CHECK(withoutSeconds == withSeconds);
}

TEST_CASE("PA12 平台公告 dateTimeToMs 与本地时区往返一致")
{
    //不硬编码期望的时间戳——那样在别的时区跑就会挂。改用往返法：
    //先把一个已知时刻按本地时区格式化成文本，再转回毫秒，应当还原成同一时刻。
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = 2026 - 1900;
    t.tm_mon   = 8 - 1;
    t.tm_mday  = 5;
    t.tm_hour  = 14;
    t.tm_min   = 30;
    t.tm_sec   = 45;
    t.tm_isdst = -1;

    const time_t expectSecs = mktime(&t);
    REQUIRE(expectSecs != static_cast<time_t>(-1));

    CHECK(AnnouncementProtocol::dateTimeToMs("2026-08-05 14:30:45")
          == static_cast<int64_t>(expectSecs) * 1000);
}

TEST_CASE("PA13 平台公告 dateTimeToMs 同日内两个时刻的间隔正确")
{
    //取同一天内相隔一小时的两个时刻：无论本地时区是什么，日内间隔都不受影响
    //（夏令时切换发生在特定日期的凌晨，这里刻意避开）。
    const int64_t at1400 = AnnouncementProtocol::dateTimeToMs("2026-08-05 14:00:00");
    const int64_t at1500 = AnnouncementProtocol::dateTimeToMs("2026-08-05 15:00:00");

    REQUIRE(at1400 > 0);
    REQUIRE(at1500 > 0);
    CHECK(at1500 - at1400 == 3600 * 1000);
}

TEST_CASE("PA9 平台公告 buildSimpleResponse 原样回带结果码与描述")
{
    Json::Value root;
    REQUIRE(parseJson(AnnouncementProtocol::buildSimpleResponse(0, "ok"), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");

    Json::Value bad;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildSimpleResponse(error_code_bad_request, "bad request"), bad));
    CHECK(bad["code"].asInt() == error_code_bad_request);
    CHECK(bad["msg"].asString() == "bad request");
}

TEST_CASE("PA14 平台公告 buildMarkReadResponse 回带公告ID")
{
    //已读应答比通用应答多回带 announcementid：用户连点几条公告时会有多条上报在途，
    //不带ID的话客户端没法知道是哪条失败了。ID 同样以字符串编码。
    Json::Value root;
    REQUIRE(parseJson(AnnouncementProtocol::buildMarkReadResponse(0, "ok", 10023), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["announcementid"].asString() == "10023");

    //请求解析失败时传 0，客户端读到 0 就知道对不上任何一条在途上报。
    Json::Value bad;
    REQUIRE(parseJson(
        AnnouncementProtocol::buildMarkReadResponse(error_code_bad_request, "bad request", 0), bad));
    CHECK(bad["code"].asInt() == error_code_bad_request);
    CHECK(bad["announcementid"].asString() == "0");
}
