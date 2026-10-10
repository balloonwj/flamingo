/**
 *  云端录制服务端处理流程（CloudRecordService）单元测试（Suite CRV），test_cloudrecordservice.cpp
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）。用假环境（CrvEnv）与假控制连接（CrvSender），存储用真的 CloudRecordStore（SQLite）：
 *    - CRV1 开始前的检查：没开通、控制连接断开、配额已满、可以开始；
 *    - CRV2 建录像记录与通知 imgserver 开始；imgserver 开始失败时删记录并通知房间；
 *    - CRV3 录制结束：状态改为等待合成并推 1757；重复的回报不再处理；imgserver 自己停的通知房间；一帧都没有的为出错并删原始帧文件；
 *    - CRV4 合成后的 MP4 上传完：可观看、删原始帧文件；文件名不对、录像已删除的情形；
 *    - CRV5 控制连接恢复：只给房间已不在录的「录制中」录像补发 stop；
 *    - CRV6 1755 列表；
 *    - CRV7 1756 重命名与删除；
 *    - CRV8 1756 下载令牌：开启者取原始帧与最终文件，别人要等发到他所在的会话；
 *    - CRV9 1756 上传令牌：新建上传录像（开关、配额、预留）、续传、合成后的上传，上传完成后可观看；
 *    - CRV10 1756 发到会话：卡片与分享记录；
 *    - CRV11 每小时维护：到期前提醒一次、到期删除。
 *
 *  典型用法：Tests.exe --test-case="CRV*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdint.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/CloudRecordProtocol.h"
#include "../chatserversrc/CloudRecordService.h"
#include "../chatserversrc/CloudRecordStore.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/MessageSearchText.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "../utils/CloudRecordCtlMsg.h"
#include "../utils/FileToken.h"

namespace {

const int64_t kCrvNow = 1790000000000LL;
const int64_t kCrvDayMs = 24LL * 60 * 60 * 1000;
const int64_t kCrvGb = 1024LL * 1024 * 1024;
const int64_t kCrvEnterprise = 7;
const int64_t kCrvOwner = 5127;
const int64_t kCrvPeer = 5308;
const int64_t kCrvOther = 6001;
const int64_t kCrvGroup = 900;
const char* const kCrvTokenKey = "crv-file-token-key";
const char* const kCrvDbPath = "flamingo_sqlite_cloudrecordservice_test.db";
const char* const kCrvMysqlDb = "flamingo_test_crv";

//假控制连接：记下发出的消息
class CrvSender : public ICloudRecordCtlSender
{
public:
    CrvSender()
        : m_up(true)
    {
    }

    virtual bool isUp() const override
    {
        return m_up;
    }

    virtual bool send(const std::string& body) override
    {
        if (!m_up)
            return false;
        CloudRecordCtlMsg msg;
        REQUIRE(ParseCloudRecordCtlMsg(body, msg));
        m_sent.push_back(msg);
        return true;
    }

    //某种 op 发了几条
    int count(int op) const
    {
        int n = 0;
        for (size_t i = 0; i < m_sent.size(); ++i)
        {
            if (m_sent[i].m_op == op)
                ++n;
        }
        return n;
    }

    bool                           m_up;     //连接是否可用
    std::vector<CloudRecordCtlMsg> m_sent;   //发出的消息
};

//假环境
class CrvEnv : public ICloudRecordEnv
{
public:
    CrvEnv()
        : m_enabled(true)
        , m_quotaGb(50)
        , m_retainDays(30)
        , m_cardOk(true)
        , m_nextMsgId(100)
    {
    }

    virtual int64_t enterpriseOf(int64_t userId) override
    {
        return userId > 0 ? kCrvEnterprise : 0;
    }

    virtual bool isEnabled(int64_t /*enterpriseId*/) override
    {
        return m_enabled;
    }

    virtual int64_t quotaGb(int64_t /*enterpriseId*/) override
    {
        return m_quotaGb;
    }

    virtual int64_t retainDays(int64_t /*enterpriseId*/) override
    {
        return m_retainDays;
    }

    virtual bool isGroupMember(int64_t groupId, int64_t userId) override
    {
        return m_groupMembers.count(std::make_pair(groupId, userId)) > 0;
    }

    virtual void push(int64_t userId, uint16_t cmd, const std::string& body) override
    {
        Json::Value v;
        Json::CharReaderBuilder b;
        JSONCPP_STRING errs;
        std::unique_ptr<Json::CharReader> reader(b.newCharReader());
        REQUIRE(reader->parse(body.c_str(), body.c_str() + body.size(), &v, &errs));
        CHECK(cmd == cmd_cloudrecord_notify);
        m_pushes.push_back(std::make_pair(userId, v));
    }

    virtual void onRoomRecordEnded(const std::string& roomId, int64_t recordId, int code) override
    {
        m_ended.push_back(std::make_pair(recordId, code));
        m_recordingRooms.erase(roomId);
    }

    virtual bool isRoomRecording(const std::string& roomId, int64_t recordId) override
    {
        std::map<std::string, int64_t>::const_iterator it = m_recordingRooms.find(roomId);
        return it != m_recordingRooms.end() && it->second == recordId;
    }

    virtual int64_t sendCard(int64_t fromId, int ctype, int64_t cid, const std::string& content, int64_t /*nowMs*/) override
    {
        if (!m_cardOk)
            return 0;
        m_cards.push_back(std::make_pair(std::make_pair(fromId, ctype), std::make_pair(cid, content)));
        return ++m_nextMsgId;
    }

    virtual void imgServerAddr(std::string& host, int& port) override
    {
        host = "10.0.0.2";
        port = 20001;
    }

    bool                                       m_enabled;          //开关
    int64_t                                    m_quotaGb;          //配额
    int64_t                                    m_retainDays;       //保留天数
    bool                                       m_cardOk;           //发卡片是否成功
    int64_t                                    m_nextMsgId;        //下一条卡片的消息 id
    std::set<std::pair<int64_t, int64_t> >     m_groupMembers;     //(群, 人)
    std::map<std::string, int64_t>             m_recordingRooms;   //房间 → 正在录的录像 id
    std::vector<std::pair<int64_t, Json::Value> > m_pushes;        //(收件人, 1757 包体)
    std::vector<std::pair<int64_t, int> >      m_ended;            //(录像 id, 原因)
    std::vector<std::pair<std::pair<int64_t, int>, std::pair<int64_t, std::string> > > m_cards;   //((发送者, 类型), (会话, 内容))
};

//装好一套服务
struct CrvFixture
{
    CrvFixture()
        : m_service(m_store, m_env, m_sender, kCrvTokenKey)
    {
        testdb::resetDb(kCrvDbPath, kCrvMysqlDb);
    }

    ~CrvFixture()
    {
        testdb::cleanupDb(kCrvDbPath);
    }

    //建一条共享录像并让房间处于录制中
    int64_t startShare(const std::string& roomId)
    {
        CloudRecordRoomInfo info;
        info.m_roomId = roomId;
        info.m_ctype = conversation_group;
        info.m_cid = kCrvGroup;
        info.m_title = u8"周会";
        int64_t id = 0;
        REQUIRE(m_service.createShareRecord(kCrvOwner, info, kCrvNow, id) == error_code_ok);
        REQUIRE(m_service.sendStart(id, roomId));
        m_env.m_recordingRooms[roomId] = id;
        return id;
    }

    std::string finished(int64_t id, uint64_t frames, int reason, int64_t bytes)
    {
        CloudRecordCtlMsg msg;
        msg.m_op = cloudrecctl_op_finished;
        msg.m_recordId = static_cast<uint64_t>(id);
        msg.m_file = CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_frec);
        msg.m_bytes = static_cast<uint64_t>(bytes);
        msg.m_durationMs = 60000;
        msg.m_frames = frames;
        msg.m_reason = reason;
        return BuildCloudRecordCtlMsg(msg);
    }

    std::string uploaded(int64_t id, const std::string& file, int64_t bytes)
    {
        CloudRecordCtlMsg msg;
        msg.m_op = cloudrecctl_op_uploaded;
        msg.m_recordId = static_cast<uint64_t>(id);
        msg.m_file = file;
        msg.m_bytes = static_cast<uint64_t>(bytes);
        return BuildCloudRecordCtlMsg(msg);
    }

    CloudRecordRow get(int64_t id)
    {
        CloudRecordRow row;
        bool found = false;
        REQUIRE(m_store.get(id, row, found));
        REQUIRE(found);
        return row;
    }

    Json::Value op(int64_t userId, const std::string& body, ICloudRecordTitleChecker* checker = NULL)
    {
        const std::string text = m_service.handleOp(userId, body, kCrvNow, checker);
        Json::Value v;
        Json::CharReaderBuilder b;
        JSONCPP_STRING errs;
        std::unique_ptr<Json::CharReader> reader(b.newCharReader());
        REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &v, &errs));
        return v;
    }

    CloudRecordStore   m_store;     //真的存储（SQLite）
    CrvEnv             m_env;       //假环境
    CrvSender          m_sender;    //假控制连接
    CloudRecordService m_service;   //被测对象
};

//1 / 2 / 5 种动作的冒烟：标题检查器
class CrvChecker : public ICloudRecordTitleChecker
{
public:
    virtual bool hitTitle(const std::string& title) override
    {
        return title.find("bad") != std::string::npos;
    }
};

} // namespace

TEST_CASE("CRV1 start checks")
{
    CrvFixture f;
    CHECK(f.m_service.checkStart(kCrvOwner) == error_code_ok);
    CHECK(f.m_service.checkStart(0) == error_code_cloudrecord_disabled);
    f.m_env.m_enabled = false;
    CHECK(f.m_service.checkStart(kCrvOwner) == error_code_cloudrecord_disabled);
    f.m_env.m_enabled = true;
    f.m_sender.m_up = false;
    CHECK(f.m_service.checkStart(kCrvOwner) == error_code_cloudrecord_unavailable);
    f.m_sender.m_up = true;
    f.m_env.m_quotaGb = 0;
    CHECK(f.m_service.checkStart(kCrvOwner) == error_code_storage_quota);
}

TEST_CASE("CRV2 create record, start, start failure")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    CloudRecordRow row = f.get(id);
    CHECK(row.m_status == cloudrecord_status_recording);
    CHECK(row.m_owner == kCrvOwner);
    CHECK(row.m_enterpriseId == kCrvEnterprise);
    CHECK(row.m_title == u8"周会");
    CHECK(row.m_frecFile == CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_frec));
    REQUIRE(f.m_sender.count(cloudrecctl_op_start) == 1);
    CHECK(f.m_sender.m_sent[0].m_roomId == "room-a");
    //开始成功的回报：什么也不改
    CloudRecordCtlMsg started;
    started.m_op = cloudrecctl_op_started;
    started.m_recordId = static_cast<uint64_t>(id);
    f.m_service.onCtlMessage(BuildCloudRecordCtlMsg(started), kCrvNow);
    CHECK(f.get(id).m_status == cloudrecord_status_recording);
    CHECK(f.m_env.m_ended.empty());
    //另一条开始失败（磁盘不足）：删记录，通知房间
    const int64_t id2 = f.startShare("room-b");
    started.m_recordId = static_cast<uint64_t>(id2);
    started.m_code = 2;
    f.m_service.onCtlMessage(BuildCloudRecordCtlMsg(started), kCrvNow);
    CloudRecordRow gone;
    bool found = true;
    REQUIRE(f.m_store.get(id2, gone, found));
    CHECK_FALSE(found);
    REQUIRE(f.m_env.m_ended.size() == 1);
    CHECK(f.m_env.m_ended[0].first == id2);
    CHECK(f.m_env.m_ended[0].second == error_code_cloudrecord_unavailable);
    //撤销
    f.m_service.discardRecord(id);
    REQUIRE(f.m_store.get(id, gone, found));
    CHECK_FALSE(found);
}

TEST_CASE("CRV3 finished")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    //主持人停止后 imgserver 回报：等待合成，到期时间从结束时算
    f.m_env.m_recordingRooms.erase("room-a");
    f.m_service.onCtlMessage(f.finished(id, 100, 1, 3 * kCrvGb), kCrvNow + 1000);
    CloudRecordRow row = f.get(id);
    CHECK(row.m_status == cloudrecord_status_pending);
    CHECK(row.m_frecBytes == 3 * kCrvGb);
    CHECK(row.m_durationMs == 60000);
    CHECK(row.m_endTime == kCrvNow + 1000);
    CHECK(row.m_expireTime == kCrvNow + 1000 + 30 * kCrvDayMs);
    CHECK(f.m_env.m_ended.empty());
    REQUIRE(f.m_env.m_pushes.size() == 1);
    CHECK(f.m_env.m_pushes[0].first == kCrvOwner);
    CHECK(f.m_env.m_pushes[0].second["type"].asInt() == cloudrecord_notify_status);
    CHECK(f.m_env.m_pushes[0].second["record"]["status"].asInt() == cloudrecord_status_pending);
    //重复的回报：不再处理
    f.m_service.onCtlMessage(f.finished(id, 0, 1, 1), kCrvNow + 2000);
    CHECK(f.get(id).m_status == cloudrecord_status_pending);
    CHECK(f.m_env.m_pushes.size() == 1);

    //imgserver 因磁盘不足自己停：通知房间
    const int64_t id2 = f.startShare("room-b");
    f.m_service.onCtlMessage(f.finished(id2, 5, 2, kCrvGb), kCrvNow + 3000);
    REQUIRE(f.m_env.m_ended.size() == 1);
    CHECK(f.m_env.m_ended[0].first == id2);
    CHECK(f.get(id2).m_status == cloudrecord_status_pending);

    //一帧都没有：出错，删原始帧文件
    const int64_t id3 = f.startShare("room-c");
    f.m_service.onCtlMessage(f.finished(id3, 0, 1, 40), kCrvNow + 4000);
    row = f.get(id3);
    CHECK(row.m_status == cloudrecord_status_failed);
    CHECK(row.m_frecFile.empty());
    CHECK(row.m_frecBytes == 0);
    REQUIRE(f.m_sender.count(cloudrecctl_op_delete) == 1);
    CHECK(f.m_sender.m_sent.back().m_files[0] == CloudRecordFileName(static_cast<uint64_t>(id3), cloudrec_file_type_frec));
    //录像已不存在：删 imgserver 上的文件
    f.m_service.onCtlMessage(f.finished(999999, 1, 1, 1), kCrvNow);
    CHECK(f.m_sender.count(cloudrecctl_op_delete) == 2);
}

TEST_CASE("CRV4 composed mp4 uploaded")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    f.m_service.onCtlMessage(f.finished(id, 100, 1, 3 * kCrvGb), kCrvNow);
    const int64_t expire = f.get(id).m_expireTime;
    const std::string mp4 = CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_mp4);
    //文件名对不上：忽略
    //另一条录像的文件名（id 加 1000，避免与新库里从 1 开始的 id 相同）
    f.m_service.onCtlMessage(f.uploaded(id, CloudRecordFileName(static_cast<uint64_t>(id + 1000), cloudrec_file_type_mp4), 100),
                             kCrvNow);
    f.m_service.onCtlMessage(f.uploaded(id, "evil.mp4", 100), kCrvNow);
    CHECK(f.get(id).m_status == cloudrecord_status_pending);
    //正确的：可观看、删原始帧
    f.m_service.onCtlMessage(f.uploaded(id, mp4, kCrvGb), kCrvNow + 5);
    CloudRecordRow row = f.get(id);
    CHECK(row.m_status == cloudrecord_status_ready);
    CHECK(row.m_file == mp4);
    CHECK(row.m_bytes == kCrvGb);
    CHECK(row.m_frecFile.empty());
    CHECK(row.m_expireTime == expire);
    REQUIRE(f.m_sender.count(cloudrecctl_op_delete) == 1);
    CHECK(f.m_sender.m_sent.back().m_files[0] == CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_frec));
    CHECK(f.m_env.m_pushes.back().second["record"]["status"].asInt() == cloudrecord_status_ready);
    //重复的上传回报：忽略
    f.m_service.onCtlMessage(f.uploaded(id, mp4, kCrvGb), kCrvNow + 6);
    CHECK(f.m_sender.count(cloudrecctl_op_delete) == 1);
    //录像已删除：删传上来的文件
    f.m_service.onCtlMessage(f.uploaded(888888, "rec_888888.mp4", 1), kCrvNow);
    CHECK(f.m_sender.count(cloudrecctl_op_delete) == 2);
    CHECK(f.m_sender.m_sent.back().m_files[0] == "rec_888888.mp4");
}

TEST_CASE("CRV5 link up resends stop only for orphaned recordings")
{
    CrvFixture f;
    const int64_t active = f.startShare("room-a");
    const int64_t orphan = f.startShare("room-b");
    f.m_env.m_recordingRooms.erase("room-b");
    f.m_sender.m_sent.clear();
    f.m_service.onLinkUp();
    REQUIRE(f.m_sender.count(cloudrecctl_op_stop) == 1);
    CHECK(static_cast<int64_t>(f.m_sender.m_sent[0].m_recordId) == orphan);
    CHECK(active != orphan);
}

TEST_CASE("CRV6 list")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 2 * kCrvGb), kCrvNow);
    const std::string text = f.m_service.handleList(kCrvOwner, "{}");
    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &v, &errs));
    CHECK(v["code"].asInt() == error_code_ok);
    REQUIRE(v["records"].size() == 1);
    CHECK(v["records"][0]["recordid"].asInt64() == id);
    CHECK(v["enabled"].asInt() == 1);
    CHECK(v["usedbytes"].asInt64() == 2 * kCrvGb);
    CHECK(v["quotabytes"].asInt64() == 50 * kCrvGb);
    CHECK(v["retaindays"].asInt64() == 30);
    //别人的列表里没有
    const std::string other = f.m_service.handleList(kCrvPeer, "");
    REQUIRE(reader->parse(other.c_str(), other.c_str() + other.size(), &v, &errs));
    CHECK(v["records"].size() == 0);
    CHECK(f.m_service.handleList(kCrvOwner, "[").find("\"code\":") != std::string::npos);
}

TEST_CASE("CRV7 rename and delete")
{
    CrvFixture f;
    CrvChecker checker;
    const int64_t id = f.startShare("room-a");
    const std::string idText = std::to_string(static_cast<long long>(id));
    //别人改：不存在
    CHECK(f.op(kCrvPeer, "{\"op\":1,\"recordid\":" + idText + ",\"title\":\"x\"}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    CHECK(f.op(kCrvOwner, "{\"op\":1,\"recordid\":" + idText + ",\"title\":\"  \"}")["code"].asInt() ==
          error_code_bad_request);
    CHECK(f.op(kCrvOwner, "{\"op\":1,\"recordid\":" + idText + ",\"title\":\"bad word\"}", &checker)["code"].asInt() ==
          error_code_sensitive_word);
    Json::Value v = f.op(kCrvOwner, "{\"op\":1,\"recordid\":" + idText + ",\"title\":\" new \"}", &checker);
    CHECK(v["code"].asInt() == error_code_ok);
    CHECK(v["record"]["title"].asString() == "new");
    CHECK(f.get(id).m_title == "new");
    //录制中不能删
    CHECK(f.op(kCrvOwner, "{\"op\":2,\"recordid\":" + idText + "}")["code"].asInt() == error_code_cloudrecord_busy);
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 10), kCrvNow);
    f.m_sender.m_sent.clear();
    CHECK(f.op(kCrvPeer, "{\"op\":2,\"recordid\":" + idText + "}")["code"].asInt() == error_code_cloudrecord_not_found);
    CHECK(f.op(kCrvOwner, "{\"op\":2,\"recordid\":" + idText + "}")["code"].asInt() == error_code_ok);
    CloudRecordRow gone;
    bool found = true;
    REQUIRE(f.m_store.get(id, gone, found));
    CHECK_FALSE(found);
    REQUIRE(f.m_sender.count(cloudrecctl_op_delete) == 1);
    CHECK(f.m_sender.m_sent[0].m_files.size() == 1);
    CHECK(f.m_env.m_pushes.back().second["type"].asInt() == cloudrecord_notify_delete);
}

TEST_CASE("CRV8 download tokens")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    const std::string idText = std::to_string(static_cast<long long>(id));
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 5000), kCrvNow);
    //开启者取原始帧：令牌可校验，scene 6，键为文件名
    Json::Value v = f.op(kCrvOwner, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":1}");
    REQUIRE(v["code"].asInt() == error_code_ok);
    CHECK(v["host"].asString() == "10.0.0.2");
    CHECK(v["port"].asInt() == 20001);
    CHECK(v["bytes"].asInt64() == 5000);
    const std::string frec = CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_frec);
    CHECK(v["filename"].asString() == frec);
    FileTokenPayload payload;
    REQUIRE(FileToken::verify(v["token"].asString(), kCrvTokenKey, kCrvNow, payload));
    CHECK(payload.type == FileToken::token_type_download);
    CHECK(payload.scene == file_scene_cloud_record);
    std::string fileName;
    REQUIRE(ParseCloudRecordTokenKey(payload.md5, fileName));
    CHECK(fileName == frec);
    CHECK(payload.size == 5000);
    CHECK(payload.uid == kCrvOwner);
    CHECK(payload.expire == kCrvNow + CloudRecordRules::kTokenTtlMs);
    //最终文件还没有
    CHECK(f.op(kCrvOwner, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":2}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    //别人取原始帧：不行
    CHECK(f.op(kCrvOther, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":1}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    //合成上传完、发到群之后：群成员能取最终文件，非成员不能
    const std::string mp4 = CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_mp4);
    f.m_service.onCtlMessage(f.uploaded(id, mp4, 7000), kCrvNow);
    CHECK(f.op(kCrvOther, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":2}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    f.m_env.m_groupMembers.insert(std::make_pair(kCrvGroup, kCrvOwner));
    f.m_env.m_groupMembers.insert(std::make_pair(kCrvGroup, kCrvOther));
    REQUIRE(f.op(kCrvOwner, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":2,\"cid\":900}")["code"].asInt() ==
            error_code_ok);
    v = f.op(kCrvOther, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":2}");
    REQUIRE(v["code"].asInt() == error_code_ok);
    CHECK(v["filename"].asString() == mp4);
    CHECK(v["bytes"].asInt64() == 7000);
    CHECK(f.op(kCrvPeer, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":2}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
}

TEST_CASE("CRV9 upload tokens")
{
    CrvFixture f;
    //没开通
    f.m_env.m_enabled = false;
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"bytes\":100}")["code"].asInt() == error_code_cloudrecord_disabled);
    f.m_env.m_enabled = true;
    //配额不够
    f.m_env.m_quotaGb = 1;
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"bytes\":2147483648}")["code"].asInt() == error_code_storage_quota);
    //新建一条只有声音的上传录像
    Json::Value v = f.op(kCrvOwner, "{\"op\":4,\"bytes\":1000,\"audioonly\":1,\"title\":\"m\",\"durationms\":9000,"
                                    "\"starttime\":1789999990000}");
    REQUIRE(v["code"].asInt() == error_code_ok);
    const int64_t id = v["recordid"].asInt64();
    const std::string m4a = CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_m4a);
    CHECK(v["filename"].asString() == m4a);
    CHECK(v["record"]["status"].asInt() == cloudrecord_status_uploading);
    CHECK(v["record"]["audioonly"].asInt() == 1);
    FileTokenPayload payload;
    REQUIRE(FileToken::verify(v["token"].asString(), kCrvTokenKey, kCrvNow, payload));
    CHECK(payload.type == FileToken::token_type_upload);
    CHECK(payload.size == 1000);
    CloudRecordRow row = f.get(id);
    CHECK(row.m_source == cloudrecord_source_upload);
    CHECK(row.m_file == m4a);
    CHECK(row.m_bytes == 1000);
    CHECK(row.m_durationMs == 9000);
    CHECK(row.m_startTime == 1789999990000LL);
    //预留的大小计入已用：再申请超出配额的不行
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"bytes\":1073741000}")["code"].asInt() == error_code_storage_quota);
    //续传：大小一致才行
    const std::string idText = std::to_string(static_cast<long long>(id));
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"recordid\":" + idText + ",\"bytes\":1000}")["code"].asInt() == error_code_ok);
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"recordid\":" + idText + ",\"bytes\":999}")["code"].asInt() ==
          error_code_cloudrecord_busy);
    CHECK(f.op(kCrvPeer, "{\"op\":4,\"recordid\":" + idText + ",\"bytes\":1000}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    //上传完：可观看，到期时间从上传完成时算
    f.m_service.onCtlMessage(f.uploaded(id, m4a, 1000), kCrvNow + 10);
    row = f.get(id);
    CHECK(row.m_status == cloudrecord_status_ready);
    CHECK(row.m_expireTime == kCrvNow + 10 + 30 * kCrvDayMs);
    //标题命中敏感词：用默认标题
    CrvChecker checker;
    f.m_env.m_quotaGb = 50;
    v = f.op(kCrvOwner, "{\"op\":4,\"bytes\":10,\"title\":\"bad\"}", &checker);
    REQUIRE(v["code"].asInt() == error_code_ok);
    CHECK(v["record"]["title"].asString() == u8"云端录像");
    CHECK(v["filename"].asString().find(".mp4") != std::string::npos);
    //合成后的上传：只认等待合成的共享录像，不查配额
    const int64_t share = f.startShare("room-a");
    const std::string shareText = std::to_string(static_cast<long long>(share));
    CHECK(f.op(kCrvOwner, "{\"op\":4,\"recordid\":" + shareText + ",\"bytes\":5}")["code"].asInt() ==
          error_code_cloudrecord_busy);
    f.m_service.onCtlMessage(f.finished(share, 1, 1, 10), kCrvNow);
    f.m_env.m_quotaGb = 0;
    v = f.op(kCrvOwner, "{\"op\":4,\"recordid\":" + shareText + ",\"bytes\":99999}");
    REQUIRE(v["code"].asInt() == error_code_ok);
    CHECK(v["filename"].asString() == CloudRecordFileName(static_cast<uint64_t>(share), cloudrec_file_type_mp4));
}

TEST_CASE("CRV10 send to conversation")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    const std::string idText = std::to_string(static_cast<long long>(id));
    //还没可观看
    CHECK(f.op(kCrvOwner, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":1,\"cid\":5308}")["code"].asInt() ==
          error_code_cloudrecord_busy);
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 10), kCrvNow);
    f.m_service.onCtlMessage(f.uploaded(id, CloudRecordFileName(static_cast<uint64_t>(id), cloudrec_file_type_mp4), 20),
                             kCrvNow);
    //不是群成员不能发到群
    CHECK(f.op(kCrvOwner, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":2,\"cid\":900}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    //发到单聊：卡片与分享记录；对方随后能看
    Json::Value v = f.op(kCrvOwner, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":1,\"cid\":5308}");
    REQUIRE(v["code"].asInt() == error_code_ok);
    REQUIRE(f.m_env.m_cards.size() == 1);
    CHECK(f.m_env.m_cards[0].first.first == kCrvOwner);
    CHECK(f.m_env.m_cards[0].first.second == conversation_single);
    CHECK(f.m_env.m_cards[0].second.first == kCrvPeer);
    Json::Value card;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    const std::string& content = f.m_env.m_cards[0].second.second;
    REQUIRE(reader->parse(content.c_str(), content.c_str() + content.size(), &card, &errs));
    CHECK(card["recordid"].asString() == idText);
    CHECK(card["title"].asString() == u8"周会");
    CHECK(card["bytes"].asInt64() == 20);
    //会话列表的摘要带标题；content 不可用时只写类型
    CHECK(MessageProtocol::buildMessageDigest(message_type_cloudrecord, content) == std::string(u8"[云端录像] 周会"));
    CHECK(MessageProtocol::buildMessageDigest(message_type_cloudrecord, "{}") == std::string(u8"[云端录像]"));
    //卡片由服务端生成：客户端不得自行发送、撤回、置顶、转发
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_cloudrecord));
    //会话列表 lastmsg 带 digestarg（标题），客户端按界面语言生成摘要；服务端搜索按标题
    std::vector<ConversationBrief> items(1);
    items[0].ctype = conversation_single;
    items[0].cid = kCrvPeer;
    items[0].lastmsgid = 1;
    items[0].lastfrom = kCrvOwner;
    items[0].lastmsgtype = message_type_cloudrecord;
    items[0].lastcontent = content;
    Json::Value sync;
    const std::string syncText = MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items);
    REQUIRE(reader->parse(syncText.c_str(), syncText.c_str() + syncText.size(), &sync, &errs));
    CHECK(sync["conversations"][0]["lastmsg"]["digestarg"].asString() == u8"周会");
    CHECK(chatserver::BuildContentSearchText(message_type_cloudrecord, content) == u8"周会");
    CHECK(f.op(kCrvPeer, "{\"op\":3,\"recordid\":" + idText + ",\"kind\":2}")["code"].asInt() == error_code_ok);
    //别人不能替开启者发
    CHECK(f.op(kCrvPeer, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":1,\"cid\":5127}")["code"].asInt() ==
          error_code_cloudrecord_not_found);
    //发卡片失败
    f.m_env.m_cardOk = false;
    CHECK(f.op(kCrvOwner, "{\"op\":5,\"recordid\":" + idText + ",\"ctype\":1,\"cid\":6001}")["code"].asInt() ==
          error_code_server_internal);
}

TEST_CASE("CRV12 enterprise used bytes for the admin console")
{
    CrvFixture f;
    int64_t used = -1;
    REQUIRE(f.m_service.enterpriseUsedBytes(kCrvEnterprise, used));
    CHECK(used == 0);
    const int64_t id = f.startShare("room-a");
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 4096), kCrvNow);
    REQUIRE(f.m_service.enterpriseUsedBytes(kCrvEnterprise, used));
    CHECK(used == 4096);
    //别的企业不受影响
    REQUIRE(f.m_service.enterpriseUsedBytes(kCrvEnterprise + 1, used));
    CHECK(used == 0);
}

TEST_CASE("CRV11 maintenance warns then removes")
{
    CrvFixture f;
    const int64_t id = f.startShare("room-a");
    f.m_service.onCtlMessage(f.finished(id, 1, 1, 10), kCrvNow);
    const int64_t expire = f.get(id).m_expireTime;
    REQUIRE(expire == kCrvNow + 30 * kCrvDayMs);
    //一个录制中的录像不受影响
    f.startShare("room-b");
    f.m_env.m_pushes.clear();
    f.m_service.runMaintenance(kCrvNow + kCrvDayMs);
    CHECK(f.m_env.m_pushes.empty());
    //到期前 3 天：提醒一次
    f.m_service.runMaintenance(expire - 2 * kCrvDayMs);
    REQUIRE(f.m_env.m_pushes.size() == 1);
    CHECK(f.m_env.m_pushes[0].second["type"].asInt() == cloudrecord_notify_expire);
    f.m_service.runMaintenance(expire - kCrvDayMs);
    CHECK(f.m_env.m_pushes.size() == 1);
    //到期：删除记录与文件，推 1757 删除
    f.m_sender.m_sent.clear();
    f.m_service.runMaintenance(expire);
    CloudRecordRow gone;
    bool found = true;
    REQUIRE(f.m_store.get(id, gone, found));
    CHECK_FALSE(found);
    REQUIRE(f.m_sender.count(cloudrecctl_op_delete) == 1);
    CHECK(f.m_env.m_pushes.back().second["type"].asInt() == cloudrecord_notify_delete);
}
