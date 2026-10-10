/**
 *  加群链接的单元测试（Suite GL-S），test_grouplink.cpp（docs/邀请入群方案.md §12）
 *
 *    GL-S1  随机码：长度与字符表；有效期的天数与到期时间
 *    GL-S2  有效性：作废、过期、不存在；永久链接不过期
 *    GL-S3  列表：只列可用的；按生成者筛选；作废只改本群的
 *    GL-S4  协议：1340~1344 的解析与构造；随机码大小写
 *
 *  典型用法：Tests.exe --test-case="GL-S*"
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupLinkManager.h"
#include "../chatserversrc/GroupLinkProtocol.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例的临时库
const char* const kGlDbPath  = "flamingo_sqlite_grouplink_test.db";
const char* const kGlMysqlDb = "flamingo_test_grouplink";

//两个群、两位成员、一个企业
const int64_t kGlGroupA = 268435901LL;
const int64_t kGlGroupB = 268435902LL;
const int64_t kGlUserA = 6001;
const int64_t kGlUserB = 6002;
const int64_t kGlEnterprise = 8001;

//时间戳（Unix 毫秒）
const int64_t kGlTime = 1700000000000LL;

//把 JSON 文本解析成 Json::Value
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs) && root.isObject();
}

//生成一条链接并要求成功
GroupLinkRecord CreateLink(int64_t groupid, int64_t creator, int days, int64_t nowMs)
{
    GroupLinkRecord rec;
    rec.groupid = groupid;
    rec.creator = creator;
    rec.enterpriseid = kGlEnterprise;
    rec.expiretime = GroupLinkProtocol::computeExpire(nowMs, days);
    REQUIRE(Singleton<GroupLinkManager>::Instance().create(rec, nowMs));
    return rec;
}

} // anonymous namespace

TEST_CASE("GL-S1 随机码的长度与字符表，有效期的换算")
{
    std::set<std::string> codes;
    for (int i = 0; i < 50; ++i)
    {
        const std::string code = GroupLinkProtocol::makeRandomCode();
        CHECK(code.size() == kGroupLinkCodeLength);
        CHECK(GroupLinkProtocol::isValidCode(code));
        codes.insert(code);
    }
    CHECK(codes.size() > 45);   //随机码基本不重复
    //固定字节：对字符表长度取模
    std::vector<unsigned char> bytes(kGroupLinkCodeLength, 0);
    CHECK(GroupLinkProtocol::makeCodeFromBytes(bytes) == std::string(kGroupLinkCodeLength, kGroupLinkCodeAlphabet[0]));
    CHECK(GroupLinkProtocol::makeCodeFromBytes(std::vector<unsigned char>(3, 0)).empty());
    CHECK_FALSE(GroupLinkProtocol::isValidCode("abc"));
    CHECK_FALSE(GroupLinkProtocol::isValidCode("abcdefghijk0"));   //含 i 与 0

    CHECK(GroupLinkProtocol::isValidDays(1));
    CHECK(GroupLinkProtocol::isValidDays(7));
    CHECK(GroupLinkProtocol::isValidDays(0));
    CHECK_FALSE(GroupLinkProtocol::isValidDays(3));
    CHECK(GroupLinkProtocol::computeExpire(kGlTime, 0) == 0);
    CHECK(GroupLinkProtocol::computeExpire(kGlTime, 7) == kGlTime + 7 * kGroupLinkMillisPerDay);
    CHECK(GroupLinkProtocol::buildUrl("abc") == std::string("flamingo://joingroup?code=abc"));
}

TEST_CASE("GL-S2 有效性：作废、过期、不存在；永久链接不过期")
{
    testdb::resetDb(kGlDbPath, kGlMysqlDb);
    GroupLinkManager& mgr = Singleton<GroupLinkManager>::Instance();
    GroupLinkRecord oneDay = CreateLink(kGlGroupA, kGlUserA, 1, kGlTime);
    GroupLinkRecord forever = CreateLink(kGlGroupA, kGlUserA, 0, kGlTime);
    CHECK(oneDay.id > 0);
    CHECK(GroupLinkProtocol::isValidCode(oneDay.code));

    GroupLinkRecord got;
    REQUIRE(mgr.getByCode(oneDay.code, got));
    CHECK(got.groupid == kGlGroupA);
    CHECK(GroupLinkProtocol::isUsable(got, kGlTime + 1));
    CHECK_FALSE(GroupLinkProtocol::isUsable(got, kGlTime + kGroupLinkMillisPerDay));   //到期那一刻起不可用
    REQUIRE(mgr.getByCode(forever.code, got));
    CHECK(GroupLinkProtocol::isUsable(got, kGlTime + 1000 * kGroupLinkMillisPerDay));

    REQUIRE(mgr.revoke(kGlGroupA, forever.id));
    REQUIRE(mgr.getByCode(forever.code, got));
    CHECK_FALSE(GroupLinkProtocol::isUsable(got, kGlTime));
    CHECK_FALSE(mgr.getByCode("zzzzzzzzzzzz", got));
    testdb::cleanupDb(kGlDbPath);
}

TEST_CASE("GL-S3 列表只列可用的，可按生成者筛选；作废只改本群的")
{
    testdb::resetDb(kGlDbPath, kGlMysqlDb);
    GroupLinkManager& mgr = Singleton<GroupLinkManager>::Instance();
    GroupLinkRecord a1 = CreateLink(kGlGroupA, kGlUserA, 7, kGlTime);
    GroupLinkRecord a2 = CreateLink(kGlGroupA, kGlUserB, 7, kGlTime + 1);
    GroupLinkRecord expired = CreateLink(kGlGroupA, kGlUserA, 1, kGlTime - 2 * kGroupLinkMillisPerDay);
    GroupLinkRecord b1 = CreateLink(kGlGroupB, kGlUserA, 0, kGlTime);
    (void)expired;
    (void)b1;

    std::vector<GroupLinkRecord> links;
    REQUIRE(mgr.listUsable(kGlGroupA, 0, kGlTime + 10, links));
    REQUIRE(links.size() == 2);
    CHECK(links[0].id == a2.id);   //按生成时间倒序
    REQUIRE(mgr.listUsable(kGlGroupA, kGlUserA, kGlTime + 10, links));
    REQUIRE(links.size() == 1);
    CHECK(links[0].id == a1.id);

    //用别的群的群号作废不生效
    REQUIRE(mgr.revoke(kGlGroupB, a1.id));
    REQUIRE(mgr.listUsable(kGlGroupA, 0, kGlTime + 10, links));
    CHECK(links.size() == 2);
    REQUIRE(mgr.revoke(kGlGroupA, a1.id));
    REQUIRE(mgr.listUsable(kGlGroupA, 0, kGlTime + 10, links));
    CHECK(links.size() == 1);
    testdb::cleanupDb(kGlDbPath);
}

TEST_CASE("GL-S4 协议：1340~1344 的解析与构造")
{
    int64_t gid = 0;
    int days = 0;
    REQUIRE(GroupLinkProtocol::parseCreateRequest("{\"groupid\":\"268435901\",\"validdays\":1}", gid, days));
    CHECK(gid == kGlGroupA);
    CHECK(days == 1);
    REQUIRE(GroupLinkProtocol::parseCreateRequest("{\"groupid\":\"268435901\"}", gid, days));
    CHECK(days == 7);   //缺省 7 天
    CHECK_FALSE(GroupLinkProtocol::parseCreateRequest("{\"groupid\":\"268435901\",\"validdays\":30}", gid, days));

    int64_t id = 0;
    REQUIRE(GroupLinkProtocol::parseRevokeRequest("{\"groupid\":\"268435901\",\"id\":\"5\"}", gid, id));
    CHECK(id == 5);

    std::string code;
    REQUIRE(GroupLinkProtocol::parseCodeRequest("{\"code\":\"ABCDEFGHJKMN\"}", code));
    CHECK(code == "abcdefghjkmn");   //大写转小写
    CHECK_FALSE(GroupLinkProtocol::parseCodeRequest("{\"code\":\"short\"}", code));
    CHECK_FALSE(GroupLinkProtocol::parseCodeRequest("{}", code));

    GroupLinkRecord rec;
    rec.id = 3;
    rec.code = "abcdefghjkmn";
    rec.groupid = kGlGroupA;
    rec.creator = kGlUserA;
    rec.creatorname = "A";
    rec.expiretime = kGlTime;
    Json::Value created;
    REQUIRE(ParseJson(GroupLinkProtocol::buildCreateResponse(0, "ok", &rec), created));
    CHECK(created["link"]["url"].asString() == "flamingo://joingroup?code=abcdefghjkmn");
    CHECK(created["link"]["id"].asString() == "3");
    Json::Value failed;
    REQUIRE(ParseJson(GroupLinkProtocol::buildCreateResponse(3, "bad", NULL), failed));
    CHECK_FALSE(failed.isMember("link"));

    std::vector<GroupLinkRecord> links(1, rec);
    Json::Value list;
    REQUIRE(ParseJson(GroupLinkProtocol::buildListResponse(0, "ok", kGlGroupA, links), list));
    CHECK(list["links"].size() == 1);

    GroupLinkInfo info;
    info.groupid = kGlGroupA;
    info.name = "G";
    info.membercount = 3;
    info.joinmode = 1;
    info.member = true;
    Json::Value infoJson;
    REQUIRE(ParseJson(GroupLinkProtocol::buildInfoResponse(0, "ok", &info), infoJson));
    CHECK(infoJson["member"].asBool());
    CHECK(infoJson["joinmode"].asInt() == 1);
    Json::Value join;
    REQUIRE(ParseJson(GroupLinkProtocol::buildJoinResponse(0, "ok", kGlGroupA, false), join));
    CHECK_FALSE(join["joined"].asBool());
}
