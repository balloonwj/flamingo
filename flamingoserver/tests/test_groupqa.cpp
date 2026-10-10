/**
 *  群问答的单元测试（Suite QA-S），test_groupqa.cpp（docs/群问答方案.md）
 *
 *    QA-S1  本群问答的增删改：只能改删本群的；内容检查（空、超长）；条数
 *    QA-S2  企业问答的可见性：应用到全部群、所选的群；本群同一问题优先；企业问答之间去重
 *    QA-S3  协议：1337 / 1338 的解析、1336 应答；管理接口的字段解析与范围校验
 *
 *  典型用法：Tests.exe --test-case="QA-S*"
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupQaManager.h"
#include "../chatserversrc/GroupQaProtocol.h"
#include "../chatserversrc/QaAdminProtocol.h"
#include "../base/Singleton.h"

namespace {

//本套用例的临时库
const char* const kQaDbPath  = "flamingo_sqlite_groupqa_test.db";
const char* const kQaMysqlDb = "flamingo_test_groupqa";

//两个群、两个企业、一位编辑
const int64_t kQaGroupA = 268435801LL;
const int64_t kQaGroupB = 268435802LL;
const int64_t kQaEnterprise = 7001;
const int64_t kQaOtherEnterprise = 7002;
const int64_t kQaEditor = 5001;

//时间戳（Unix 毫秒）
const int64_t kQaTime = 1700000000000LL;

//把 JSON 文本解析成 Json::Value
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs) && root.isObject();
}

//造一条企业问答
EnterpriseQaRecord MakeEntQa(int64_t enterpriseId, const char* q, int scope, int64_t groupid)
{
    EnterpriseQaRecord r;
    r.enterpriseid = enterpriseId;
    r.question = q;
    r.answer = u8"企业答案";
    r.scope = scope;
    if (groupid > 0)
        r.groupids.push_back(groupid);
    r.creatorid = 1;
    r.creatorname = "admin";
    return r;
}

} // anonymous namespace

TEST_CASE("QA-S1 本群问答增删改，只能改删本群的；内容检查")
{
    testdb::resetDb(kQaDbPath, kQaMysqlDb);
    GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();

    int64_t id1 = 0;
    int64_t id2 = 0;
    REQUIRE(mgr.addGroupQa(kQaGroupA, u8"几点下班", u8"18:00", kQaEditor, kQaTime, id1));
    REQUIRE(mgr.addGroupQa(kQaGroupA, u8"报销流程", u8"见 https://oa.example.com/claim", kQaEditor, kQaTime + 1, id2));
    CHECK(id1 > 0);
    CHECK(id2 > id1);
    CHECK(mgr.countGroupQa(kQaGroupA) == 2);
    CHECK(mgr.countGroupQa(kQaGroupB) == 0);

    //按修改时间倒序
    std::vector<GroupQaRecord> items;
    REQUIRE(mgr.listGroupQa(kQaGroupA, items));
    REQUIRE(items.size() == 2);
    CHECK(items[0].id == id2);

    //修改：只改本群的那一条
    REQUIRE(mgr.updateGroupQa(kQaGroupA, id1, u8"几点下班", u8"17:30", kQaEditor, kQaTime + 5));
    GroupQaRecord r;
    REQUIRE(mgr.getGroupQa(id1, r));
    CHECK(r.answer == u8"17:30");
    REQUIRE(mgr.updateGroupQa(kQaGroupB, id1, u8"改错群", u8"x", kQaEditor, kQaTime + 6));
    REQUIRE(mgr.getGroupQa(id1, r));
    CHECK(r.question == u8"几点下班");   //别的群的请求不生效

    //删除：别的群删不掉
    REQUIRE(mgr.deleteGroupQa(kQaGroupB, id1));
    CHECK(mgr.getGroupQa(id1, r));
    REQUIRE(mgr.deleteGroupQa(kQaGroupA, id1));
    CHECK_FALSE(mgr.getGroupQa(id1, r));

    //内容检查
    CHECK(GroupQaProtocol::checkContent(u8"问", u8"答") == group_qa_check_ok);
    CHECK(GroupQaProtocol::checkContent("  ", u8"答") == group_qa_check_empty);
    CHECK(GroupQaProtocol::checkContent(u8"问", "") == group_qa_check_empty);
    std::string longQ;
    for (int i = 0; i <= kGroupQaQuestionMaxChars; ++i)
        longQ += u8"问";
    CHECK(GroupQaProtocol::checkContent(longQ, u8"答") == group_qa_check_question_long);
    std::string longA(static_cast<size_t>(kGroupQaAnswerMaxChars) + 1, 'a');
    CHECK(GroupQaProtocol::checkContent(u8"问", longA) == group_qa_check_answer_long);
    testdb::cleanupDb(kQaDbPath);
}

TEST_CASE("QA-S2 企业问答的可见性与合并：全部群、所选的群、本群同一问题优先")
{
    testdb::resetDb(kQaDbPath, kQaMysqlDb);
    GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();

    int64_t allId = 0;
    int64_t onlyBId = 0;
    int64_t otherEntId = 0;
    REQUIRE(mgr.addEnterpriseQa(MakeEntQa(kQaEnterprise, u8"WiFi 密码", enterprise_qa_scope_all, 0), kQaTime, allId));
    REQUIRE(mgr.addEnterpriseQa(MakeEntQa(kQaEnterprise, u8"只给 B 群", enterprise_qa_scope_selected, kQaGroupB),
                                kQaTime + 1, onlyBId));
    REQUIRE(mgr.addEnterpriseQa(MakeEntQa(kQaOtherEnterprise, u8"别的企业", enterprise_qa_scope_all, 0),
                                kQaTime + 2, otherEntId));

    std::vector<GroupQaRecord> forA;
    REQUIRE(mgr.listEnterpriseQaForGroup(kQaEnterprise, kQaGroupA, forA));
    REQUIRE(forA.size() == 1);
    CHECK(forA[0].id == allId);
    std::vector<GroupQaRecord> forB;
    REQUIRE(mgr.listEnterpriseQaForGroup(kQaEnterprise, kQaGroupB, forB));
    CHECK(forB.size() == 2);
    std::vector<GroupQaRecord> none;
    REQUIRE(mgr.listEnterpriseQaForGroup(0, kQaGroupA, none));
    CHECK(none.empty());

    //管理端读回：应用到的群
    EnterpriseQaRecord e;
    REQUIRE(mgr.getEnterpriseQa(onlyBId, e));
    REQUIRE(e.groupids.size() == 1);
    CHECK(e.groupids[0] == kQaGroupB);
    //改成全部群：应用到的群清空
    e.scope = enterprise_qa_scope_all;
    REQUIRE(mgr.updateEnterpriseQa(e, kQaTime + 10));
    REQUIRE(mgr.getEnterpriseQa(onlyBId, e));
    CHECK(e.groupids.empty());
    REQUIRE(mgr.listEnterpriseQaForGroup(kQaEnterprise, kQaGroupA, forA));
    CHECK(forA.size() == 2);

    //合并：本群有同一问题（大小写、首尾空白不同）时只留本群的；企业问答之间重复留第一条
    std::vector<GroupQaRecord> groupItems(1);
    groupItems[0].id = 1;
    groupItems[0].question = u8"  wifi 密码 ";
    std::vector<GroupQaRecord> entItems(3);
    entItems[0].id = 10;
    entItems[0].question = u8"WiFi 密码";
    entItems[1].id = 11;
    entItems[1].question = u8"打印机";
    entItems[2].id = 12;
    entItems[2].question = u8"打印机";
    const std::vector<GroupQaRecord> merged = GroupQaProtocol::mergeForGroup(groupItems, entItems);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0].id == 1);
    CHECK(merged[0].source == group_qa_source_group);
    CHECK(merged[1].id == 11);
    CHECK(merged[1].source == group_qa_source_enterprise);

    //删除：只能删本企业的
    REQUIRE(mgr.deleteEnterpriseQa(kQaOtherEnterprise, allId));
    CHECK(mgr.getEnterpriseQa(allId, e));
    REQUIRE(mgr.deleteEnterpriseQa(kQaEnterprise, allId));
    CHECK_FALSE(mgr.getEnterpriseQa(allId, e));
    CHECK(mgr.countEnterpriseQa(kQaEnterprise) == 1);
    testdb::cleanupDb(kQaDbPath);
}

TEST_CASE("QA-S3 协议与管理接口的字段解析、范围校验")
{
    SaveGroupQaRequest req;
    REQUIRE(GroupQaProtocol::parseSaveRequest(
        u8"{\"groupid\":\"268435801\",\"id\":\"0\",\"question\":\"  几点下班 \",\"answer\":\"18:00\"}", req));
    CHECK(req.groupid == kQaGroupA);
    CHECK(req.id == 0);
    CHECK(req.question == u8"几点下班");
    CHECK_FALSE(GroupQaProtocol::parseSaveRequest("{\"groupid\":\"268435801\",\"question\":\"q\"}", req));
    int64_t gid = 0;
    int64_t id = 0;
    REQUIRE(GroupQaProtocol::parseDeleteRequest("{\"groupid\":\"268435801\",\"id\":\"7\"}", gid, id));
    CHECK(id == 7);
    CHECK_FALSE(GroupQaProtocol::parseDeleteRequest("{\"groupid\":\"268435801\"}", gid, id));

    std::vector<GroupQaRecord> items(1);
    items[0].id = 9;
    items[0].question = "q";
    items[0].answer = "a";
    items[0].source = group_qa_source_enterprise;
    Json::Value resp;
    REQUIRE(ParseJson(GroupQaProtocol::buildGetResponse(0, "ok", kQaGroupA, items), resp));
    REQUIRE(resp["items"].size() == 1);
    CHECK(resp["items"][0]["source"].asString() == "enterprise");
    CHECK(resp["items"][0]["id"].asString() == "9");
    Json::Value notify;
    REQUIRE(ParseJson(GroupQaProtocol::buildChangedNotify(kQaGroupA), notify));
    CHECK(notify["groupid"].asString() == std::to_string(kQaGroupA));

    //管理接口：字段解析与应用范围校验
    Json::Value body;
    REQUIRE(ParseJson(u8"{\"question\":\"q\",\"answer\":\"a\",\"scope\":1,\"groupids\":[\"268435801\",268435801]}", body));
    EnterpriseQaRecord rec;
    REQUIRE(qaadmin::ParseQaFields(body, rec));
    CHECK(rec.scope == enterprise_qa_scope_selected);
    CHECK(rec.groupids.size() == 1);   //重复的群只算一次
    std::set<int64_t> mine;
    mine.insert(kQaGroupA);
    CHECK(qaadmin::CheckQa(rec, mine) == 0);
    std::set<int64_t> other;
    other.insert(kQaGroupB);
    CHECK(qaadmin::CheckQa(rec, other) != 0);
    rec.groupids.clear();
    CHECK(qaadmin::CheckQa(rec, mine) != 0);   //所选的群为空
    Json::Value bad;
    REQUIRE(ParseJson("{\"question\":\"q\",\"answer\":\"a\",\"scope\":3}", bad));
    CHECK_FALSE(qaadmin::ParseQaFields(bad, rec));
}
