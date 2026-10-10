/**
 *  敏感词管理后台接口报文编解码单元测试（Suite SWA），test_sensitiveadminprotocol.cpp
 *
 *  覆盖 chatserversrc/SensitiveAdminProtocol：写操作结果到管理端错误码的换算（SWA-1）、
 *  企业范围的确定（SWA-2）、请求里 ids 与 lines 数组的解析（SWA-3、SWA-4）、应答 JSON 的
 *  拼装（SWA-5）。用例清单见 docs/敏感词方案.md 第 9 节 1c。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "doctest.h"
#include "../chatserversrc/SensitiveAdminProtocol.h"
#include "../chatserversrc/AdminApiCodes.h"

#include <string>
#include <vector>

using namespace chatserver;

namespace {

// 平台超级管理员的企业ID恒为 0；两个任取的企业ID。
const int64_t kPlatformAdmin = 0;
const int64_t kEntA = 9001;
const int64_t kEntB = 9002;

// 测试 ParseIds / ParseLines 时用的上限。
const size_t kMaxIds   = 3;
const size_t kMaxLines = 2;

} // anonymous namespace

TEST_CASE("SWA-1 写操作结果换成管理端错误码")
{
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_ok) == error_code_ok);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_empty) == error_code_sensitive_word_invalid);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_too_long) == error_code_sensitive_word_toolong);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_exist) == error_code_sensitive_word_exist);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_full) == error_code_sensitive_word_full);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_not_exist) == error_code_sensitive_word_notexist);
    CHECK(sensitiveadmin::ToAdminCode(sensitive_word_op_db_error) == error_code_unknown);
    CHECK(sensitiveadmin::ToAdminCode(99) == error_code_unknown);

    //五个新错误码的取值固定，flamingoAdmin 照抄一份
    CHECK(error_code_sensitive_word_invalid == 120);
    CHECK(error_code_sensitive_word_toolong == 121);
    CHECK(error_code_sensitive_word_exist == 122);
    CHECK(error_code_sensitive_word_full == 123);
    CHECK(error_code_sensitive_word_notexist == 124);
}

TEST_CASE("SWA-2 平台超级管理员须指定企业，企业管理员一律是本企业")
{
    int64_t ent = -1;
    CHECK_FALSE(sensitiveadmin::ResolveEnterprise(kPlatformAdmin, 0, ent));
    CHECK(ent == 0);

    REQUIRE(sensitiveadmin::ResolveEnterprise(kPlatformAdmin, kEntB, ent));
    CHECK(ent == kEntB);

    //企业管理员传了别的企业，也只能操作本企业
    REQUIRE(sensitiveadmin::ResolveEnterprise(kEntA, kEntB, ent));
    CHECK(ent == kEntA);
    REQUIRE(sensitiveadmin::ResolveEnterprise(kEntA, 0, ent));
    CHECK(ent == kEntA);
}

TEST_CASE("SWA-3 解析 ids：整数数组，非整数元素跳过，空、超限或不是数组时失败")
{
    std::vector<int64_t> ids;

    Json::Value arr(Json::arrayValue);
    arr.append(Json::Int64(5));
    arr.append("x");
    arr.append(Json::Int64(9000000000LL));
    REQUIRE(sensitiveadmin::ParseIds(arr, kMaxIds, ids));
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 5);
    CHECK(ids[1] == 9000000000LL);

    CHECK_FALSE(sensitiveadmin::ParseIds(Json::Value(Json::arrayValue), kMaxIds, ids));

    Json::Value tooMany(Json::arrayValue);
    for (size_t i = 0; i <= kMaxIds; ++i)
        tooMany.append(Json::Int64(i + 1));
    CHECK_FALSE(sensitiveadmin::ParseIds(tooMany, kMaxIds, ids));

    CHECK_FALSE(sensitiveadmin::ParseIds(Json::Value("1,2"), kMaxIds, ids));
    CHECK_FALSE(sensitiveadmin::ParseIds(Json::Value(), kMaxIds, ids));
}

TEST_CASE("SWA-4 解析 lines：字符串数组，含非字符串元素、空、超限或不是数组时失败")
{
    std::vector<std::string> lines;

    Json::Value arr(Json::arrayValue);
    arr.append(u8"词一");
    arr.append("");
    REQUIRE(sensitiveadmin::ParseLines(arr, kMaxLines, lines));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == std::string(u8"词一"));
    CHECK(lines[1].empty());

    Json::Value mixed(Json::arrayValue);
    mixed.append("a");
    mixed.append(Json::Int64(1));
    CHECK_FALSE(sensitiveadmin::ParseLines(mixed, kMaxLines, lines));

    Json::Value tooMany(Json::arrayValue);
    for (size_t i = 0; i <= kMaxLines; ++i)
        tooMany.append("w");
    CHECK_FALSE(sensitiveadmin::ParseLines(tooMany, kMaxLines, lines));

    CHECK_FALSE(sensitiveadmin::ParseLines(Json::Value(Json::arrayValue), kMaxLines, lines));
    CHECK_FALSE(sensitiveadmin::ParseLines(Json::Value("a"), kMaxLines, lines));
}

TEST_CASE("SWA-5 应答 JSON：词条、拦截记录与导入结果的各字段")
{
    SensitiveWordRecord w;
    w.m_id = 11;
    w.m_enterpriseId = kEntA;
    w.m_word = u8"敏感";
    w.m_category = sensitive_category_illegal;
    w.m_remark = "r";
    w.m_creatorId = 7;
    w.m_creatorName = "admin";
    w.m_createTime = 1700000000000LL;
    w.m_updateTime = 1700000000001LL;
    const Json::Value jw = sensitiveadmin::WordToJson(w);
    CHECK(jw["id"].asInt64() == 11);
    CHECK(jw["word"].asString() == std::string(u8"敏感"));
    CHECK(jw["category"].asInt() == sensitive_category_illegal);
    CHECK(jw["remark"].asString() == "r");
    CHECK(jw["creatorname"].asString() == "admin");
    CHECK(jw["createtime"].asInt64() == 1700000000000LL);
    CHECK(jw["updatetime"].asInt64() == 1700000000001LL);

    SensitiveHitRecord h;
    h.m_id = 21;
    h.m_enterpriseId = kEntA;
    h.m_userId = 101;
    h.m_userName = u8"张三";
    h.m_source = sensitive_source_profile;
    h.m_ctype = 2;
    h.m_cid = 4000000001LL;
    h.m_targetName = u8"项目组";
    h.m_wordId = 11;
    h.m_word = u8"敏感";
    h.m_category = sensitive_category_illegal;
    h.m_snippet = u8"前文敏感后文";
    h.m_createTime = 1700000000002LL;
    const Json::Value jh = sensitiveadmin::HitToJson(h);
    CHECK(jh["id"].asInt64() == 21);
    CHECK(jh["time"].asInt64() == 1700000000002LL);
    CHECK(jh["userid"].asInt64() == 101);
    CHECK(jh["username"].asString() == std::string(u8"张三"));
    CHECK(jh["source"].asInt() == sensitive_source_profile);
    CHECK(jh["ctype"].asInt() == 2);
    CHECK(jh["cid"].asInt64() == 4000000001LL);
    CHECK(jh["targetname"].asString() == std::string(u8"项目组"));
    CHECK(jh["word"].asString() == std::string(u8"敏感"));
    CHECK(jh["category"].asInt() == sensitive_category_illegal);
    CHECK(jh["snippet"].asString() == std::string(u8"前文敏感后文"));

    std::vector<SensitiveImportSkip> skipped;
    SensitiveImportSkip s;
    s.m_line = 6;
    s.m_word = "!!!";
    s.m_reason = sensitive_word_op_empty;
    skipped.push_back(s);
    const Json::Value ji = sensitiveadmin::ImportResultToJson(3, skipped);
    CHECK(ji["code"].asInt() == error_code_ok);
    CHECK(ji["msg"].asString() == "ok");
    CHECK(ji["added"].asInt() == 3);
    REQUIRE(ji["skipped"].isArray());
    REQUIRE(ji["skipped"].size() == 1);
    CHECK(ji["skipped"][0]["line"].asInt() == 6);
    CHECK(ji["skipped"][0]["word"].asString() == "!!!");
    CHECK(ji["skipped"][0]["reason"].asInt() == error_code_sensitive_word_invalid);

    //没有被跳过的行时，skipped 仍是空数组而不是缺字段
    const Json::Value jEmpty = sensitiveadmin::ImportResultToJson(0, std::vector<SensitiveImportSkip>());
    REQUIRE(jEmpty["skipped"].isArray());
    CHECK(jEmpty["skipped"].size() == 0);
}
