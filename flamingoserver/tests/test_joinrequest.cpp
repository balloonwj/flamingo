/**
 *  加群申请的审批与「申请已处理」推送的单元测试（Suite JQ），test_joinrequest.cpp
 *
 *  背景（docs/邀请入群方案.md 第 4 期）：审批原先分两步——先读申请、确认仍是待处理，再按申请 id 改状态，改状态的 SQL
 *  不带「仍是待处理」的条件。两位管理员同时处理同一条申请时，两个请求都可能通过第一步，后写的覆盖先写的。
 *
 *    JQ1  已处理的申请再次改状态不生效：返回「没改到」，状态、处理时间、处理人都保持第一次的结果（改之前这条用例失败，
 *         复现了后写的覆盖先写的）
 *    JQ2  待处理的申请改状态生效：返回「改到了」，状态、处理时间、处理人写入
 *    JQ3  「申请已处理」推送（1329）的正文：群、申请、结果、处理人与昵称，id 写成字符串
 *
 *  JQ1、JQ2 会打开一个临时数据库跑 SQL（默认 SQLite，设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL，见 test_dbbackend.h）。
 *
 *  用法：Tests.exe --test-case="JQ*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <memory>
#include <string>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时数据库文件与 MySQL 测试库名
const char* const JQ_DB_PATH  = "flamingo_sqlite_joinrequest_test.db";
const char* const JQ_MYSQL_DB = "flamingo_test_jq";

//样例：群、申请人、两位处理人（群主与管理员）
const int64_t kGroupId   = 268435463;
const int32_t kApplicant = 5308;
const int32_t kOwner     = 5127;
const int32_t kAdmin     = 5010;

//样例时间：申请时间、第一次处理时间、第二次处理时间（Unix 毫秒）
const int64_t kCreate      = 1760000000000LL;
const int64_t kFirstHandle = kCreate + 1000;
const int64_t kLateHandle  = kCreate + 2000;

//申请状态：与 t_group_join_request.f_status 的取值一致
const int kStatusPending  = 0;   // 待处理
const int kStatusAccepted = 1;   // 已通过
const int kStatusRejected = 2;   // 已拒绝

//JQ3：申请 id 与处理人昵称
const int64_t kRequestId = 5001;
const char* const kOwnerName = u8"张勇";

//把 JSON 文本解析成对象；失败时让用例直接失败
Json::Value parse(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs));
    REQUIRE(root.isObject());
    return root;
}

} // namespace

TEST_CASE("JQ1 已处理的申请再次改状态不生效")
{
    testdb::resetDb(JQ_DB_PATH, JQ_MYSQL_DB);
    GroupManager& gm = Singleton<GroupManager>::Instance();

    const int64_t requestId = gm.addJoinRequest(kGroupId, kApplicant, "hi", kCreate);
    REQUIRE(requestId > 0);

    //群主先同意：改到了
    bool changed = false;
    REQUIRE(gm.updateJoinRequestStatus(requestId, kStatusAccepted, kFirstHandle, kOwner, changed));
    CHECK(changed);

    //管理员随后（或同时、但晚一步写库）拒绝：没改到，不覆盖群主的结果
    REQUIRE(gm.updateJoinRequestStatus(requestId, kStatusRejected, kLateHandle, kAdmin, changed));
    CHECK_FALSE(changed);

    GroupJoinRequestRecord rec;
    REQUIRE(gm.getJoinRequestById(requestId, rec));
    CHECK(rec.status == kStatusAccepted);
    CHECK(rec.handletime == kFirstHandle);
    CHECK(rec.handler == kOwner);

    testdb::cleanupDb(JQ_DB_PATH);
}

TEST_CASE("JQ2 待处理的申请改状态生效")
{
    testdb::resetDb(JQ_DB_PATH, JQ_MYSQL_DB);
    GroupManager& gm = Singleton<GroupManager>::Instance();

    const int64_t requestId = gm.addJoinRequest(kGroupId, kApplicant, "hi", kCreate);
    REQUIRE(requestId > 0);
    GroupJoinRequestRecord rec;
    REQUIRE(gm.getJoinRequestById(requestId, rec));
    CHECK(rec.status == kStatusPending);

    bool changed = false;
    REQUIRE(gm.updateJoinRequestStatus(requestId, kStatusRejected, kFirstHandle, kAdmin, changed));
    CHECK(changed);
    REQUIRE(gm.getJoinRequestById(requestId, rec));
    CHECK(rec.status == kStatusRejected);
    CHECK(rec.handletime == kFirstHandle);
    CHECK(rec.handler == kAdmin);

    testdb::cleanupDb(JQ_DB_PATH);
}

TEST_CASE("JQ3 「申请已处理」推送的正文")
{
    const Json::Value accepted = parse(GroupProtocol::buildJoinRequestHandledNotify(
        kGroupId, kRequestId, kStatusAccepted, kOwner, kOwnerName));
    CHECK(accepted["groupid"].asString() == "268435463");
    CHECK(accepted["requestid"].asString() == "5001");
    CHECK(accepted["status"].asInt() == kStatusAccepted);
    CHECK(accepted["handler"].asString() == "5127");
    CHECK(accepted["handlername"].asString() == std::string(kOwnerName));

    //拒绝；处理人昵称取不到时为空串，字段照样在
    const Json::Value rejected = parse(GroupProtocol::buildJoinRequestHandledNotify(
        kGroupId, kRequestId, kStatusRejected, kAdmin, ""));
    CHECK(rejected["status"].asInt() == kStatusRejected);
    CHECK(rejected["handler"].asString() == "5010");
    CHECK(rejected.isMember("handlername"));
    CHECK(rejected["handlername"].asString().empty());
}
