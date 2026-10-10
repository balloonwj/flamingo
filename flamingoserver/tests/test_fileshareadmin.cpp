/**
 *  文件共享管理后台接口的纯函数单元测试（Suite FSA），test_fileshareadmin.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 3 期第 3 步（6.5 的接口约定）：
 *    - FSA1 企业范围：读写设置时企业管理员一律本企业、平台管理员必须指定；列表与审计的过滤；单个共享是否在范围内；
 *    - FSA2 分页换算：页码与每页条数的缺省、下限与上限；
 *    - FSA3 请求解析：共享列表的关键字（超长时按 UTF-8 字符边界截断）与状态；审计的动作、时间、共享 id（数字与字符串都认）；
 *    - FSA4 应答拼装：设置、共享列表、成员、审计列表的字段。
 *  全部是纯函数，不碰数据库与网络。
 *
 *  典型用法：Tests.exe --test-case="FSA*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../jsoncpp1.9.0/json.h"
#include "../chatserversrc/FileShareAdminProtocol.h"

using namespace fileshareadmin;

namespace {

//两个企业
const int64_t kEnterpriseA = 7;
const int64_t kEnterpriseB = 8;

//解析一段 JSON
Json::Value Parse(const std::string& text)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::Value out;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    reader->parse(text.data(), text.data() + text.size(), &out, &errs);
    return out;
}

} // anonymous namespace

TEST_CASE("FSA1 企业范围")
{
    int64_t eid = -1;
    //企业管理员：一律本企业，请求里写了别的企业也不理会
    CHECK(ResolveTargetEnterprise(kEnterpriseA, kEnterpriseB, eid));
    CHECK(eid == kEnterpriseA);
    CHECK(ResolveTargetEnterprise(kEnterpriseA, 0, eid));
    CHECK(eid == kEnterpriseA);
    //平台管理员：必须指定
    CHECK(ResolveTargetEnterprise(0, kEnterpriseB, eid));
    CHECK(eid == kEnterpriseB);
    CHECK_FALSE(ResolveTargetEnterprise(0, 0, eid));
    CHECK(eid == 0);

    //列表与审计的过滤
    CHECK(ResolveListEnterprise(kEnterpriseA, kEnterpriseB) == kEnterpriseA);
    CHECK(ResolveListEnterprise(0, kEnterpriseB) == kEnterpriseB);
    CHECK(ResolveListEnterprise(0, 0) == 0);
    CHECK(ResolveListEnterprise(0, -3) == 0);

    //单个共享的操作
    CHECK(ShareInScope(0, kEnterpriseB));
    CHECK(ShareInScope(kEnterpriseA, kEnterpriseA));
    CHECK_FALSE(ShareInScope(kEnterpriseA, kEnterpriseB));
}

TEST_CASE("FSA2 分页换算")
{
    int offset = -1;
    int limit = -1;
    PageToRange(1, 20, offset, limit);
    CHECK(offset == 0);
    CHECK(limit == 20);
    PageToRange(3, 20, offset, limit);
    CHECK(offset == 40);
    //页码小于 1 按 1；每页条数缺省与上限
    PageToRange(0, 0, offset, limit);
    CHECK(offset == 0);
    CHECK(limit == kDefaultPageSize);
    PageToRange(2, kMaxPageSize + 1, offset, limit);
    CHECK(limit == kMaxPageSize);
    CHECK(offset == kMaxPageSize);
}

TEST_CASE("FSA3 请求解析")
{
    ShareAdminQuery q = ParseShareQuery(Parse("{\"page\":2,\"pagesize\":10,\"keyword\":\"张\",\"state\":2}"), kEnterpriseA);
    CHECK(q.m_enterpriseId == kEnterpriseA);
    CHECK(q.m_keyword == "张");
    CHECK(q.m_state == FileShareManager::kShareRecordCanceled);
    CHECK(q.m_offset == 10);
    CHECK(q.m_limit == 10);
    //不认识的状态按不限；页码写成字符串也认
    q = ParseShareQuery(Parse("{\"state\":9,\"page\":\"3\"}"), 0);
    CHECK(q.m_state == 0);
    CHECK(q.m_offset == 2 * kDefaultPageSize);
    //超长关键字：按 UTF-8 字符边界截到 kKeywordMaxBytes 以内
    std::string longKw;
    for (int i = 0; i < 30; ++i)
        longKw += "中";
    q = ParseShareQuery(Parse("{\"keyword\":\"" + longKw + "\"}"), 0);
    CHECK(q.m_keyword.size() <= kKeywordMaxBytes);
    CHECK(q.m_keyword.size() % 3 == 0);
    CHECK(q.m_keyword == longKw.substr(0, q.m_keyword.size()));

    ShareAuditQuery a = ParseAuditQuery(Parse("{\"actorkeyword\":\"lisi\",\"action\":8,\"starttime\":1000,\"endtime\":2000,"
                                              "\"shareid\":\"42\",\"pagesize\":5}"), kEnterpriseB);
    CHECK(a.m_enterpriseId == kEnterpriseB);
    CHECK(a.m_actorKeyword == "lisi");
    CHECK(a.m_action == 8);
    CHECK(a.m_startMs == 1000);
    CHECK(a.m_endMs == 2000);
    CHECK(a.m_shareId == 42);
    CHECK(a.m_limit == 5);
    a = ParseAuditQuery(Parse("{\"action\":-1,\"starttime\":-5,\"shareid\":\"x\"}"), 0);
    CHECK(a.m_action == 0);
    CHECK(a.m_startMs == 0);
    CHECK(a.m_shareId == 0);

    CHECK(ParseId(Parse("{\"shareid\":9007}"), "shareid") == 9007);
    CHECK(ParseId(Parse("{\"shareid\":\"9007\"}"), "shareid") == 9007);
    CHECK(ParseId(Parse("{}"), "shareid") == 0);
}

TEST_CASE("FSA4 应答拼装")
{
    std::vector<FeatureSettingView> settings(1);
    settings[0].m_key = "fileshare.enabled";
    settings[0].m_value = "0";
    settings[0].m_isDefault = false;
    Json::Value s = BuildSettings(settings);
    REQUIRE(s["settings"].size() == 1);
    CHECK(s["settings"][0]["key"].asString() == "fileshare.enabled");
    CHECK(s["settings"][0]["value"].asString() == "0");
    CHECK(s["settings"][0]["isdefault"].asInt() == 0);

    std::vector<ShareAdminView> shares(1);
    shares[0].m_share.m_shareId = 9001;
    shares[0].m_share.m_enterpriseId = kEnterpriseA;
    shares[0].m_share.m_name = "Projects";
    shares[0].m_share.m_state = FileShareManager::kShareRecordActive;
    shares[0].m_share.m_createTime = 1700000000000LL;
    shares[0].m_ownerName = "张三";
    shares[0].m_conversationName = "项目组";
    shares[0].m_memberCount = 3;
    Json::Value l = BuildShareList(shares, 21);
    CHECK(l["total"].asInt64() == 21);
    REQUIRE(l["list"].size() == 1);
    CHECK(l["list"][0]["shareid"].asInt64() == 9001);
    CHECK(l["list"][0]["ownername"].asString() == "张三");
    CHECK(l["list"][0]["conversationname"].asString() == "项目组");
    CHECK(l["list"][0]["membercount"].asInt() == 3);
    CHECK(l["list"][0]["createtime"].asInt64() == 1700000000000LL);

    std::vector<ShareMemberView> members(1);
    members[0].m_userId = 5308;
    members[0].m_name = "李四";
    members[0].m_perms = 31;
    Json::Value m = BuildMembers(members);
    REQUIRE(m["members"].size() == 1);
    CHECK(m["members"][0]["userid"].asInt64() == 5308);
    CHECK(m["members"][0]["perms"].asInt() == 31);

    std::vector<ShareAuditView> audits(1);
    audits[0].m_record.m_id = 77;
    audits[0].m_record.m_action = 11;
    audits[0].m_record.m_createTime = 1700000001000LL;
    audits[0].m_actorName = "管理员甲";
    audits[0].m_actorIsAdmin = true;
    Json::Value a = BuildAuditList(audits, 1);
    CHECK(a["total"].asInt64() == 1);
    REQUIRE(a["list"].size() == 1);
    CHECK(a["list"][0]["id"].asInt64() == 77);
    CHECK(a["list"][0]["time"].asInt64() == 1700000001000LL);
    CHECK(a["list"][0]["actorname"].asString() == "管理员甲");
    CHECK(a["list"][0]["actorisadmin"].asInt() == 1);
    //空列表也是数组
    CHECK(BuildAuditList(std::vector<ShareAuditView>(), 0)["list"].isArray());
}
