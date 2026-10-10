/**
 *  chatserver 一侧云端录像的规则、协议与存储单元测试，test_cloudrecordserver.cpp
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）：
 *    - CRR1~6 规则（CloudRecordRules）：到期时间与提醒、配额、标题、默认标题、谁能看、谁能下载、录制结束后的状态；
 *    - CRP1~4 协议（CloudRecordProtocol）：1754 / 1755 / 1756 的解析与构造、1757 推送、一条录像的 JSON；
 *    - CRFN1 云端录像目录的文件名（utils/CloudRecordCtlMsg）：生成与解析、各种不合法的写法；
 *    - CRS1~4 存储（CloudRecordStore，SQLite）：新建与读回、分页、录制结束只改录制中的、上传完成、已用空间、
 *      到期与提醒、按状态列出、分享记录、删除连同分享记录；
 *    - SSCR1~2 屏幕共享房间的云端录制状态（ScreenShareManager）与 1725 快照里的 cloudrecord、事件 15 的 cloudrecordcode。
 *
 *  典型用法：Tests.exe --test-case="CRR*,CRP*,CRFN*,CRS*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdint.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/CloudRecordProtocol.h"
#include "../chatserversrc/CloudRecordRules.h"
#include "../chatserversrc/CloudRecordStore.h"
#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/ScreenShareManager.h"
#include "../chatserversrc/ScreenShareProtocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "../utils/CloudRecordCtlMsg.h"

namespace {

//测试用的当前时刻，Unix 毫秒
const int64_t kCrsNow = 1790000000000LL;
//一天的毫秒数
const int64_t kCrsDayMs = 24LL * 60 * 60 * 1000;
//1 GB 的字节数
const int64_t kCrsGb = 1024LL * 1024 * 1024;
//两个企业、三个人、一个群
const int64_t kCrsEnterpriseA = 7;
const int64_t kCrsEnterpriseB = 8;
const int64_t kCrsOwner = 5127;
const int64_t kCrsPeer = 5308;
const int64_t kCrsOther = 6001;
const int64_t kCrsGroup = 900;
//存储用例的数据库
const char* const kCrsDbPath = "flamingo_sqlite_cloudrecord_test.db";
const char* const kCrsMysqlDb = "flamingo_test_crs";

bool crsParse(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs) && out.isObject();
}

CloudRecordRow crsRow(int64_t owner, int status)
{
    CloudRecordRow row;
    row.m_enterpriseId = kCrsEnterpriseA;
    row.m_owner = owner;
    row.m_source = cloudrecord_source_share;
    row.m_roomId = "ss-5127-1000-1";
    row.m_ctype = conversation_group;
    row.m_cid = kCrsGroup;
    row.m_title = u8"周会";
    row.m_startTime = kCrsNow;
    row.m_status = status;
    row.m_createTime = kCrsNow;
    return row;
}

CloudRecordShareRow crsShare(int64_t recordId, int ctype, int64_t cid)
{
    CloudRecordShareRow s;
    s.m_recordId = recordId;
    s.m_ctype = ctype;
    s.m_cid = cid;
    s.m_sender = kCrsOwner;
    s.m_sendTime = kCrsNow;
    return s;
}

} // namespace

TEST_CASE("CRR1 expire time and warning")
{
    CHECK(CloudRecordRules::expireTimeFor(kCrsNow, 30) == kCrsNow + 30 * kCrsDayMs);
    CHECK(CloudRecordRules::expireTimeFor(kCrsNow, 0) == 0);
    CHECK(CloudRecordRules::expireTimeFor(0, 30) == 0);
    CHECK(CloudRecordRules::expireTimeFor(kCrsNow, -1) == 0);
    const int64_t expire = kCrsNow + 10 * kCrsDayMs;
    //还有 10 天：不提醒；还有 3 天整：提醒；提醒过：不再提醒；已到期：不提醒（直接删除）
    CHECK_FALSE(CloudRecordRules::shouldWarnExpire(expire, false, kCrsNow));
    CHECK(CloudRecordRules::shouldWarnExpire(expire, false, expire - 3 * kCrsDayMs));
    CHECK(CloudRecordRules::shouldWarnExpire(expire, false, expire - 1));
    CHECK_FALSE(CloudRecordRules::shouldWarnExpire(expire, true, expire - 1));
    CHECK_FALSE(CloudRecordRules::shouldWarnExpire(expire, false, expire));
    CHECK_FALSE(CloudRecordRules::shouldWarnExpire(0, false, kCrsNow));
    CHECK(CloudRecordRules::isExpired(expire, expire));
    CHECK_FALSE(CloudRecordRules::isExpired(expire, expire - 1));
    CHECK_FALSE(CloudRecordRules::isExpired(0, kCrsNow));
}

TEST_CASE("CRR2 quota")
{
    CHECK(CloudRecordRules::quotaBytes(50) == 50 * kCrsGb);
    CHECK(CloudRecordRules::quotaBytes(-1) == 0);
    //开始录制（新增 0）：已用低于配额才行
    CHECK(CloudRecordRules::quotaAllows(0, 50, 0));
    CHECK(CloudRecordRules::quotaAllows(50 * kCrsGb - 1, 50, 0));
    CHECK_FALSE(CloudRecordRules::quotaAllows(50 * kCrsGb, 50, 0));
    //上传：已用 + 新增不超过配额
    CHECK(CloudRecordRules::quotaAllows(49 * kCrsGb, 50, kCrsGb));
    CHECK_FALSE(CloudRecordRules::quotaAllows(49 * kCrsGb, 50, kCrsGb + 1));
    //配额 0：一律不行
    CHECK_FALSE(CloudRecordRules::quotaAllows(0, 0, 0));
}

TEST_CASE("CRR3 titles")
{
    std::string t;
    CHECK(CloudRecordRules::normalizeTitle(u8"  周会录像 ", t));
    CHECK(t == u8"周会录像");
    CHECK_FALSE(CloudRecordRules::normalizeTitle("   ", t));
    CHECK_FALSE(CloudRecordRules::normalizeTitle("", t));
    CHECK(CloudRecordRules::normalizeTitle(std::string(50, 'a'), t));
    CHECK_FALSE(CloudRecordRules::normalizeTitle(std::string(51, 'a'), t));
    std::string fifty;
    for (int i = 0; i < 50; ++i)
        fifty += u8"字";
    CHECK(CloudRecordRules::normalizeTitle(fifty, t));
    CHECK_FALSE(CloudRecordRules::normalizeTitle(fifty + u8"字", t));
    //不是合法的 UTF-8
    CHECK_FALSE(CloudRecordRules::normalizeTitle(std::string("\xE5\x91"), t));
    //默认标题
    CHECK(CloudRecordRules::defaultTitle("") == u8"云端录像");
    CHECK(CloudRecordRules::defaultTitle(u8" 产品评审 ") == u8"产品评审");
    CHECK(CloudRecordRules::defaultTitle(fifty + u8"多出来的") == fifty);
}

TEST_CASE("CRR4 who can view")
{
    std::vector<CloudRecordShareRow> shares;
    std::set<int64_t> groups;
    //开启者本人
    CHECK(CloudRecordRules::canView(kCrsOwner, kCrsOwner, shares, groups));
    //没有分享记录：别人不能看
    CHECK_FALSE(CloudRecordRules::canView(kCrsPeer, kCrsOwner, shares, groups));
    CHECK_FALSE(CloudRecordRules::canView(0, kCrsOwner, shares, groups));
    //发到单聊：接收方能看，第三人不能
    shares.push_back(crsShare(1, conversation_single, kCrsPeer));
    CHECK(CloudRecordRules::canView(kCrsPeer, kCrsOwner, shares, groups));
    CHECK_FALSE(CloudRecordRules::canView(kCrsOther, kCrsOwner, shares, groups));
    //发到群：调用方查到他是该群成员才能看
    shares.push_back(crsShare(1, conversation_group, kCrsGroup));
    CHECK_FALSE(CloudRecordRules::canView(kCrsOther, kCrsOwner, shares, groups));
    groups.insert(kCrsGroup);
    CHECK(CloudRecordRules::canView(kCrsOther, kCrsOwner, shares, groups));
}

TEST_CASE("CRR5 who can download what")
{
    CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_pending);
    row.m_frecFile = "rec_1.frec";
    //原始帧文件：只有开启者
    CHECK(CloudRecordRules::canDownload(cloudrecord_file_frec, true, true, row));
    CHECK_FALSE(CloudRecordRules::canDownload(cloudrecord_file_frec, false, true, row));
    //最终文件：还没可观看时谁都不行
    CHECK_FALSE(CloudRecordRules::canDownload(cloudrecord_file_final, true, true, row));
    row.m_status = cloudrecord_status_ready;
    row.m_file = "rec_1.mp4";
    row.m_frecFile.clear();
    CHECK(CloudRecordRules::canDownload(cloudrecord_file_final, true, true, row));
    CHECK(CloudRecordRules::canDownload(cloudrecord_file_final, false, true, row));
    CHECK_FALSE(CloudRecordRules::canDownload(cloudrecord_file_final, false, false, row));
    //原始帧文件已删除
    CHECK_FALSE(CloudRecordRules::canDownload(cloudrecord_file_frec, true, true, row));
    CHECK_FALSE(CloudRecordRules::canDownload(9, true, true, row));
    CHECK(CloudRecordRules::canSend(row));
    row.m_status = cloudrecord_status_pending;
    CHECK_FALSE(CloudRecordRules::canSend(row));
}

TEST_CASE("CRR6 status after imgserver finished")
{
    CHECK(CloudRecordRules::statusAfterFinished(1) == cloudrecord_status_pending);
    CHECK(CloudRecordRules::statusAfterFinished(0) == cloudrecord_status_failed);
}

TEST_CASE("CRP1 control request and response")
{
    std::string roomId;
    int action = 0;
    REQUIRE(CloudRecordProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":1}", roomId, action));
    CHECK(roomId == "ss-1");
    CHECK(action == cloudrecord_control_start);
    CHECK(CloudRecordProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":4}", roomId, action));
    CHECK_FALSE(CloudRecordProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":5}", roomId, action));
    CHECK_FALSE(CloudRecordProtocol::parseControlRequest("{\"roomid\":\"\",\"action\":1}", roomId, action));
    CHECK_FALSE(CloudRecordProtocol::parseControlRequest("{\"action\":1}", roomId, action));
    CHECK_FALSE(CloudRecordProtocol::parseControlRequest("not json", roomId, action));
    Json::Value v;
    REQUIRE(crsParse(CloudRecordProtocol::buildControlResponse(0, "", "ss-1", 1, 42), v));
    CHECK(v["code"].asInt() == 0);
    CHECK(v["roomid"].asString() == "ss-1");
    CHECK(v["action"].asInt() == 1);
    CHECK(v["recordid"].asInt64() == 42);
    REQUIRE(crsParse(CloudRecordProtocol::buildControlResponse(error_code_cloudrecord_disabled, "off", "ss-1", 1, 0), v));
    CHECK(v["code"].asInt() == error_code_cloudrecord_disabled);
    CHECK_FALSE(v.isMember("recordid"));
}

TEST_CASE("CRP2 list request and response")
{
    int64_t before = -1;
    int limit = 0;
    REQUIRE(CloudRecordProtocol::parseListRequest("", before, limit));
    CHECK(before == 0);
    CHECK(limit == CloudRecordRules::kListPageDefault);
    REQUIRE(CloudRecordProtocol::parseListRequest("{\"beforeid\":100,\"limit\":500}", before, limit));
    CHECK(before == 100);
    CHECK(limit == CloudRecordRules::kListPageMax);
    REQUIRE(CloudRecordProtocol::parseListRequest("{\"beforeid\":-5,\"limit\":0}", before, limit));
    CHECK(before == 0);
    CHECK(limit == CloudRecordRules::kListPageDefault);
    CHECK_FALSE(CloudRecordProtocol::parseListRequest("[1]", before, limit));

    std::vector<CloudRecordRow> rows;
    CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_ready);
    row.m_id = 42;
    row.m_file = "rec_42.mp4";
    row.m_bytes = 5000000000LL;
    row.m_audioOnly = true;
    row.m_expireTime = kCrsNow + kCrsDayMs;
    rows.push_back(row);
    Json::Value v;
    REQUIRE(crsParse(CloudRecordProtocol::buildListResponse(0, "", rows, true, false, 123, 50 * kCrsGb, 30), v));
    REQUIRE(v["records"].isArray());
    REQUIRE(v["records"].size() == 1);
    const Json::Value& r = v["records"][0];
    CHECK(r["recordid"].asInt64() == 42);
    CHECK(r["owner"].asInt64() == kCrsOwner);
    CHECK(r["source"].asInt() == cloudrecord_source_share);
    CHECK(r["title"].asString() == u8"周会");
    CHECK(r["bytes"].asInt64() == 5000000000LL);
    CHECK(r["audioonly"].asInt() == 1);
    CHECK(r["status"].asInt() == cloudrecord_status_ready);
    CHECK(r["expiretime"].asInt64() == kCrsNow + kCrsDayMs);
    CHECK(r["ctype"].asInt() == conversation_group);
    CHECK(r["cid"].asInt64() == kCrsGroup);
    CHECK(v["hasmore"].asInt() == 1);
    CHECK(v["enabled"].asInt() == 0);
    CHECK(v["usedbytes"].asInt64() == 123);
    CHECK(v["quotabytes"].asInt64() == 50 * kCrsGb);
    CHECK(v["retaindays"].asInt64() == 30);
    //失败时只有 code 与 msg
    REQUIRE(crsParse(CloudRecordProtocol::buildListResponse(error_code_server_internal, "db", rows, false, true, 0, 0, 0), v));
    CHECK_FALSE(v.isMember("records"));
}

TEST_CASE("CRP3 op requests")
{
    CloudRecordOpRequest req;
    REQUIRE(CloudRecordProtocol::parseOpRequest("{\"op\":1,\"recordid\":3,\"title\":\"x\"}", req));
    CHECK(req.m_op == cloudrecord_op_rename);
    CHECK(req.m_title == "x");
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":1,\"recordid\":3}", req));
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":1,\"title\":\"x\"}", req));
    REQUIRE(CloudRecordProtocol::parseOpRequest("{\"op\":2,\"recordid\":3}", req));
    CHECK(req.m_op == cloudrecord_op_delete);
    REQUIRE(CloudRecordProtocol::parseOpRequest("{\"op\":3,\"recordid\":3,\"kind\":1}", req));
    CHECK(req.m_kind == cloudrecord_file_frec);
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":3,\"recordid\":3,\"kind\":3}", req));
    //取上传令牌：新建（recordid 省略）与合成后上传（recordid 为已有录像）
    REQUIRE(CloudRecordProtocol::parseOpRequest(
        "{\"op\":4,\"bytes\":1000,\"audioonly\":1,\"title\":\"t\",\"durationms\":5000,\"starttime\":1790000000000}", req));
    CHECK(req.m_recordId == 0);
    CHECK(req.m_bytes == 1000);
    CHECK(req.m_audioOnly);
    CHECK(req.m_durationMs == 5000);
    CHECK(req.m_startTime == kCrsNow);
    REQUIRE(CloudRecordProtocol::parseOpRequest("{\"op\":4,\"recordid\":9,\"bytes\":1}", req));
    CHECK(req.m_recordId == 9);
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":4,\"bytes\":0}", req));
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":4,\"recordid\":-1,\"bytes\":1}", req));
    //发到会话
    REQUIRE(CloudRecordProtocol::parseOpRequest("{\"op\":5,\"recordid\":3,\"ctype\":2,\"cid\":900}", req));
    CHECK(req.m_ctype == conversation_group);
    CHECK(req.m_cid == kCrsGroup);
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":5,\"recordid\":3,\"ctype\":3,\"cid\":900}", req));
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":5,\"recordid\":3,\"ctype\":1}", req));
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("{\"op\":9,\"recordid\":3}", req));
    CHECK_FALSE(CloudRecordProtocol::parseOpRequest("x", req));
}

TEST_CASE("CRP4 op responses and notifies")
{
    CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_pending);
    row.m_id = 42;
    CloudRecordTokenInfo token;
    token.m_token = "tok";
    token.m_host = "10.0.0.2";
    token.m_port = 20001;
    token.m_fileName = "rec_42.frec";
    token.m_bytes = 777;
    Json::Value v;
    REQUIRE(crsParse(CloudRecordProtocol::buildOpResponse(0, "", cloudrecord_op_download_token, 42, NULL, &token), v));
    CHECK(v["op"].asInt() == cloudrecord_op_download_token);
    CHECK(v["recordid"].asInt64() == 42);
    CHECK(v["token"].asString() == "tok");
    CHECK(v["host"].asString() == "10.0.0.2");
    CHECK(v["port"].asInt() == 20001);
    CHECK(v["filename"].asString() == "rec_42.frec");
    CHECK(v["bytes"].asInt64() == 777);
    CHECK_FALSE(v.isMember("record"));
    REQUIRE(crsParse(CloudRecordProtocol::buildOpResponse(0, "", cloudrecord_op_rename, 42, &row, NULL), v));
    CHECK(v["record"]["recordid"].asInt64() == 42);
    CHECK_FALSE(v.isMember("token"));
    REQUIRE(crsParse(CloudRecordProtocol::buildRecordNotify(cloudrecord_notify_expire, row), v));
    CHECK(v["type"].asInt() == cloudrecord_notify_expire);
    CHECK(v["recordid"].asInt64() == 42);
    CHECK(v["record"]["status"].asInt() == cloudrecord_status_pending);
    REQUIRE(crsParse(CloudRecordProtocol::buildDeleteNotify(42), v));
    CHECK(v["type"].asInt() == cloudrecord_notify_delete);
    CHECK(v["recordid"].asInt64() == 42);
    CHECK_FALSE(v.isMember("record"));
}

TEST_CASE("CRFN1 cloud record file names")
{
    CHECK(CloudRecordFileName(42, cloudrec_file_type_frec) == "rec_42.frec");
    CHECK(CloudRecordFileName(42, cloudrec_file_type_mp4) == "rec_42.mp4");
    CHECK(CloudRecordFileName(42, cloudrec_file_type_m4a) == "rec_42.m4a");
    CHECK(CloudRecordFileName(0, cloudrec_file_type_mp4).empty());
    CHECK(CloudRecordFileName(42, 9).empty());
    uint64_t id = 0;
    int type = 0;
    REQUIRE(ParseCloudRecordFileName("rec_42.mp4", id, type));
    CHECK(id == 42);
    CHECK(type == cloudrec_file_type_mp4);
    REQUIRE(ParseCloudRecordFileName("rec_18446744073709551615.frec", id, type));
    CHECK(id == 18446744073709551615ULL);
    CHECK_FALSE(ParseCloudRecordFileName("rec_18446744073709551616.frec", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_0.mp4", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_042.mp4", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_.mp4", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_4a2.mp4", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_42.avi", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_42.mp4.part", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("../rec_42.mp4", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("rec_42", id, type));
    CHECK_FALSE(ParseCloudRecordFileName("", id, type));

    //scene 6 令牌的 md5 字段：文件名里的点换成下划线，令牌里不能有点
    CHECK(CloudRecordTokenKey("rec_42.mp4") == "rec_42_mp4");
    CHECK(CloudRecordTokenKey("rec_42.frec") == "rec_42_frec");
    CHECK(CloudRecordTokenKey("x.mp4").empty());
    std::string name;
    REQUIRE(ParseCloudRecordTokenKey("rec_42_m4a", name));
    CHECK(name == "rec_42.m4a");
    CHECK_FALSE(ParseCloudRecordTokenKey("rec_42_avi", name));
    CHECK_FALSE(ParseCloudRecordTokenKey("rec_42.mp4", name));
    CHECK_FALSE(ParseCloudRecordTokenKey("0123456789abcdef0123456789abcdef", name));
    CHECK_FALSE(ParseCloudRecordTokenKey("", name));
}

TEST_CASE("CRS1 insert, get, list pages")
{
    testdb::resetDb(kCrsDbPath, kCrsMysqlDb);
    CloudRecordStore store;
    std::vector<int64_t> ids;
    for (int i = 0; i < 5; ++i)
    {
        CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_recording);
        row.m_title = std::string(u8"录像") + std::to_string(i);
        REQUIRE(store.insert(row));
        CHECK(row.m_id > 0);
        ids.push_back(row.m_id);
    }
    CloudRecordRow other = crsRow(kCrsPeer, cloudrecord_status_recording);
    REQUIRE(store.insert(other));

    CloudRecordRow got;
    bool found = false;
    REQUIRE(store.get(ids[2], got, found));
    REQUIRE(found);
    CHECK(got.m_owner == kCrsOwner);
    CHECK(got.m_title == u8"录像2");
    CHECK(got.m_roomId == "ss-5127-1000-1");
    CHECK(got.m_ctype == conversation_group);
    CHECK(got.m_cid == kCrsGroup);
    CHECK(got.m_status == cloudrecord_status_recording);
    CHECK(got.m_startTime == kCrsNow);
    REQUIRE(store.get(999999, got, found));
    CHECK_FALSE(found);

    //分页：每页 2 条，按 id 从大到小，只列本人的
    std::vector<CloudRecordRow> page;
    bool hasMore = false;
    REQUIRE(store.listByOwner(kCrsOwner, 0, 2, page, hasMore));
    REQUIRE(page.size() == 2);
    CHECK(page[0].m_id == ids[4]);
    CHECK(page[1].m_id == ids[3]);
    CHECK(hasMore);
    REQUIRE(store.listByOwner(kCrsOwner, page[1].m_id, 2, page, hasMore));
    REQUIRE(page.size() == 2);
    CHECK(page[0].m_id == ids[2]);
    REQUIRE(store.listByOwner(kCrsOwner, page[1].m_id, 2, page, hasMore));
    REQUIRE(page.size() == 1);
    CHECK(page[0].m_id == ids[0]);
    CHECK_FALSE(hasMore);
    testdb::cleanupDb(kCrsDbPath);
}

TEST_CASE("CRS2 finish, ready, used bytes, rename, by status")
{
    testdb::resetDb(kCrsDbPath, kCrsMysqlDb);
    CloudRecordStore store;
    CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_recording);
    row.m_frecFile = "rec_x.frec";
    REQUIRE(store.insert(row));
    std::vector<CloudRecordRow> recording;
    REQUIRE(store.listByStatus(cloudrecord_status_recording, recording));
    REQUIRE(recording.size() == 1);

    //录制结束：只改录制中的；重复的回报不改动
    bool changed = false;
    const int64_t expire = kCrsNow + 30 * kCrsDayMs;
    REQUIRE(store.finishRecording(row.m_id, cloudrecord_status_pending, kCrsNow + 60000, 55000, 3 * kCrsGb, expire, changed));
    CHECK(changed);
    REQUIRE(store.finishRecording(row.m_id, cloudrecord_status_failed, kCrsNow + 99999, 1, 1, 0, changed));
    CHECK_FALSE(changed);
    CloudRecordRow got;
    bool found = false;
    REQUIRE(store.get(row.m_id, got, found));
    CHECK(got.m_status == cloudrecord_status_pending);
    CHECK(got.m_endTime == kCrsNow + 60000);
    CHECK(got.m_durationMs == 55000);
    CHECK(got.m_frecBytes == 3 * kCrsGb);
    CHECK(got.m_expireTime == expire);

    //已用空间：原始帧 3 GB；另一企业的不算
    CloudRecordRow otherEnt = crsRow(kCrsPeer, cloudrecord_status_ready);
    otherEnt.m_enterpriseId = kCrsEnterpriseB;
    otherEnt.m_bytes = 7 * kCrsGb;
    REQUIRE(store.insert(otherEnt));
    int64_t used = 0;
    REQUIRE(store.usedBytes(kCrsEnterpriseA, used));
    CHECK(used == 3 * kCrsGb);
    REQUIRE(store.usedBytes(999, used));
    CHECK(used == 0);

    //合成后的 MP4 上传完：可观看；删掉原始帧后只计 MP4
    REQUIRE(store.markReady(row.m_id, "rec_x.mp4", kCrsGb, 54000, 0));
    REQUIRE(store.usedBytes(kCrsEnterpriseA, used));
    CHECK(used == 4 * kCrsGb);
    REQUIRE(store.clearFrec(row.m_id));
    REQUIRE(store.usedBytes(kCrsEnterpriseA, used));
    CHECK(used == kCrsGb);
    REQUIRE(store.get(row.m_id, got, found));
    CHECK(got.m_status == cloudrecord_status_ready);
    CHECK(got.m_file == "rec_x.mp4");
    CHECK(got.m_bytes == kCrsGb);
    CHECK(got.m_durationMs == 54000);
    CHECK(got.m_expireTime == expire);
    CHECK(got.m_frecFile.empty());
    CHECK(got.m_frecBytes == 0);

    //上传中预留的大小也计入已用
    CloudRecordRow up = crsRow(kCrsOwner, cloudrecord_status_uploading);
    up.m_source = cloudrecord_source_upload;
    REQUIRE(store.insert(up));
    REQUIRE(store.setFiles(up.m_id, "", "rec_up.mp4", 2 * kCrsGb));
    REQUIRE(store.usedBytes(kCrsEnterpriseA, used));
    CHECK(used == 3 * kCrsGb);

    //重命名：引号也能存
    REQUIRE(store.rename(row.m_id, u8"它's \"评审\""));
    REQUIRE(store.get(row.m_id, got, found));
    CHECK(got.m_title == u8"它's \"评审\"");
    testdb::cleanupDb(kCrsDbPath);
}

TEST_CASE("CRS3 expired and to-warn lists")
{
    testdb::resetDb(kCrsDbPath, kCrsMysqlDb);
    CloudRecordStore store;
    CloudRecordRow forever = crsRow(kCrsOwner, cloudrecord_status_ready);
    REQUIRE(store.insert(forever));
    CloudRecordRow expired = crsRow(kCrsOwner, cloudrecord_status_ready);
    expired.m_expireTime = kCrsNow - 1;
    REQUIRE(store.insert(expired));
    CloudRecordRow soon = crsRow(kCrsOwner, cloudrecord_status_ready);
    soon.m_expireTime = kCrsNow + 2 * kCrsDayMs;
    REQUIRE(store.insert(soon));
    CloudRecordRow later = crsRow(kCrsOwner, cloudrecord_status_ready);
    later.m_expireTime = kCrsNow + 10 * kCrsDayMs;
    REQUIRE(store.insert(later));

    std::vector<CloudRecordRow> rows;
    REQUIRE(store.listExpired(kCrsNow, 10, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_id == expired.m_id);
    REQUIRE(store.listToWarn(kCrsNow, CloudRecordRules::kExpireWarnBeforeMs, 10, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_id == soon.m_id);
    REQUIRE(store.markWarned(soon.m_id));
    REQUIRE(store.listToWarn(kCrsNow, CloudRecordRules::kExpireWarnBeforeMs, 10, rows));
    CHECK(rows.empty());
    testdb::cleanupDb(kCrsDbPath);
}

TEST_CASE("CRS4 shares and remove")
{
    testdb::resetDb(kCrsDbPath, kCrsMysqlDb);
    CloudRecordStore store;
    CloudRecordRow row = crsRow(kCrsOwner, cloudrecord_status_ready);
    REQUIRE(store.insert(row));
    REQUIRE(store.addShare(crsShare(row.m_id, conversation_single, kCrsPeer)));
    REQUIRE(store.addShare(crsShare(row.m_id, conversation_group, kCrsGroup)));
    //同一会话再发一次：只留一行，时间更新
    CloudRecordShareRow again = crsShare(row.m_id, conversation_group, kCrsGroup);
    again.m_sendTime = kCrsNow + 5;
    REQUIRE(store.addShare(again));
    std::vector<CloudRecordShareRow> shares;
    REQUIRE(store.listShares(row.m_id, shares));
    REQUIRE(shares.size() == 2);
    bool groupFound = false;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        if (shares[i].m_ctype == conversation_group)
        {
            groupFound = true;
            CHECK(shares[i].m_sendTime == kCrsNow + 5);
            CHECK(shares[i].m_sender == kCrsOwner);
        }
    }
    CHECK(groupFound);
    //删除录像连同分享记录
    REQUIRE(store.remove(row.m_id));
    CloudRecordRow got;
    bool found = true;
    REQUIRE(store.get(row.m_id, got, found));
    CHECK_FALSE(found);
    REQUIRE(store.listShares(row.m_id, shares));
    CHECK(shares.empty());
    testdb::cleanupDb(kCrsDbPath);
}

TEST_CASE("SSCR1 room cloud record state: begin, pause, end")
{
    ScreenShareManager mgr;
    std::string roomId;
    REQUIRE(mgr.startShare(kCrsOwner, conversation_group, kCrsGroup, false, false, false, kCrsNow, roomId) == 0);
    int seat = 0;
    REQUIRE(mgr.joinShare(roomId, kCrsPeer, kCrsNow + 1, seat) == 0);
    ScreenShareRoom room;
    //只有主持人能开；房间不存在
    CHECK(mgr.beginCloudRecord(roomId, kCrsPeer, 42, kCrsNow, room) == error_code_screenshare_not_host);
    CHECK(mgr.beginCloudRecord("no-room", kCrsOwner, 42, kCrsNow, room) == error_code_screenshare_not_exist);
    REQUIRE(mgr.beginCloudRecord(roomId, kCrsOwner, 42, kCrsNow + 5, room) == error_code_ok);
    CHECK(room.m_cloudRecordId == 42);
    CHECK(room.m_cloudRecordOwner == kCrsOwner);
    CHECK(room.m_cloudRecordSince == kCrsNow + 5);
    CHECK_FALSE(room.m_cloudRecordPaused);
    //同一房间只有一份
    CHECK(mgr.beginCloudRecord(roomId, kCrsOwner, 43, kCrsNow, room) == error_code_cloudrecord_busy);
    //暂停、继续
    int64_t recordId = 0;
    CHECK(mgr.setCloudRecordPaused(roomId, kCrsPeer, true, recordId, room) == error_code_screenshare_not_host);
    REQUIRE(mgr.setCloudRecordPaused(roomId, kCrsOwner, true, recordId, room) == error_code_ok);
    CHECK(recordId == 42);
    CHECK(room.m_cloudRecordPaused);
    REQUIRE(mgr.setCloudRecordPaused(roomId, kCrsOwner, false, recordId, room) == error_code_ok);
    CHECK_FALSE(room.m_cloudRecordPaused);
    //服务端按录像 id 结束：id 不符时不动
    CHECK(mgr.endCloudRecord(roomId, 0, 41, error_code_cloudrecord_unavailable, recordId, room) ==
          error_code_cloudrecord_busy);
    REQUIRE(mgr.endCloudRecord(roomId, 0, 42, error_code_cloudrecord_unavailable, recordId, room) == error_code_ok);
    CHECK(recordId == 42);
    CHECK(room.m_cloudRecordId == 0);
    CHECK(room.m_cloudRecordEndCode == error_code_cloudrecord_unavailable);
    //没有在录：暂停、停止都回 737
    CHECK(mgr.setCloudRecordPaused(roomId, kCrsOwner, true, recordId, room) == error_code_cloudrecord_busy);
    CHECK(mgr.endCloudRecord(roomId, kCrsOwner, 0, 0, recordId, room) == error_code_cloudrecord_busy);
    //再开一次，主持人停止：原因为 0
    REQUIRE(mgr.beginCloudRecord(roomId, kCrsOwner, 44, kCrsNow, room) == error_code_ok);
    CHECK(room.m_cloudRecordEndCode == 0);
    CHECK(mgr.endCloudRecord(roomId, kCrsPeer, 0, 0, recordId, room) == error_code_screenshare_not_host);
    REQUIRE(mgr.endCloudRecord(roomId, kCrsOwner, 0, 0, recordId, room) == error_code_ok);
    CHECK(recordId == 44);
    CHECK(room.m_cloudRecordEndCode == 0);
}

TEST_CASE("SSCR2 1725 snapshot carries cloudrecord")
{
    ScreenShareManager mgr;
    std::string roomId;
    REQUIRE(mgr.startShare(kCrsOwner, conversation_single, kCrsPeer, false, false, false, kCrsNow, roomId) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    Json::Value v;
    REQUIRE(crsParse(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, kCrsOwner, room), v));
    CHECK_FALSE(v.isMember("cloudrecord"));
    REQUIRE(mgr.beginCloudRecord(roomId, kCrsOwner, 42, kCrsNow + 9, room) == error_code_ok);
    REQUIRE(crsParse(ScreenShareProtocol::buildStateNotify(screenshare_event_cloudrecord_changed, kCrsOwner, room), v));
    CHECK(v["event"].asInt() == screenshare_event_cloudrecord_changed);
    REQUIRE(v["cloudrecord"].isObject());
    CHECK(v["cloudrecord"]["recordid"].asString() == "42");
    CHECK(v["cloudrecord"]["owner"].asString() == std::to_string(static_cast<long long>(kCrsOwner)));
    CHECK(v["cloudrecord"]["paused"].asInt() == 0);
    CHECK(v["cloudrecord"]["since"].asInt64() == kCrsNow + 9);
    CHECK_FALSE(v.isMember("cloudrecordcode"));
    int64_t recordId = 0;
    REQUIRE(mgr.endCloudRecord(roomId, 0, 42, error_code_cloudrecord_unavailable, recordId, room) == error_code_ok);
    REQUIRE(crsParse(ScreenShareProtocol::buildStateNotify(screenshare_event_cloudrecord_changed, 0, room), v));
    CHECK_FALSE(v.isMember("cloudrecord"));
    CHECK(v["cloudrecordcode"].asInt() == error_code_cloudrecord_unavailable);
    //别的事件不带原因
    REQUIRE(crsParse(ScreenShareProtocol::buildStateNotify(screenshare_event_member_left, 0, room), v));
    CHECK_FALSE(v.isMember("cloudrecordcode"));
}
