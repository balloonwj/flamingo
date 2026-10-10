/**
 *  语音消息 exe 侧协议层与本地库的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM8  发送请求的字段：msgtype=4，content 带 fileid（字符串）、md5、duration、size，有波形时带 wave；
 *    - VM9  收消息解析：语音字段齐全、fileid 写成数字、没有 wave 三种情况；
 *    - VM10 本地库：BuildContentJson 的 voice 子对象；played 列的取值——对方发的为 0、本人发的为 1、标记已听后为 1、
 *           重新同步不改已听状态、存量库补列后加列之前的历史语音为 1；
 *    - VM15 波形的 Base64 编码与解码，没有或不合法时得到均匀波形；时长换算成整秒（第 3a 步）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "sqlite3.h"
#include "../net/Protocol.h"
#include "../net/MessageProtocolClient.h"
#include "../net/VoiceContent.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace {

//测试用的本人与对方 userid、会话
const int64_t kVcSelf = 100001;
const int64_t kVcPeer = 100002;

//解析 JSON 文本
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return root;
}

//一条语音消息推送的 JSON（content 由调用方给出）
std::string voiceNotifyJson(int64_t msgid, int64_t from, const std::string& content)
{
    return "{\"msgid\":\"" + std::to_string(msgid) + "\",\"clientmsgid\":\"C1\",\"ctype\":1,\"cid\":\""
        + std::to_string(kVcPeer) + "\",\"from\":\"" + std::to_string(from) + "\",\"to\":\""
        + std::to_string(kVcSelf) + "\",\"msgtype\":4,\"timestamp\":1700000000000,\"content\":" + content + "}";
}

//每个进程独立的临时库路径，先删掉旧文件
std::wstring voiceTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    const std::wstring path = std::wstring(dir) + prefix + name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//一条语音消息的 MessageNotify
net::MessageNotify voiceNotify(int64_t msgid, int64_t from)
{
    net::MessageNotify n;
    n.m_msgid = msgid;
    n.m_from = from;
    n.m_ctype = conversation_single;
    n.m_cid = kVcPeer;
    n.m_msgtype = message_type_voice;
    n.m_timestamp = 1700000000000LL + msgid;
    n.m_voiceFileId = "998877";
    n.m_voiceMd5 = "d41d8cd98f00b204e9800998ecf8427e";
    n.m_voiceDuration = 8;
    n.m_voiceSize = 25600;
    n.m_voiceWave = "AAECAw==";
    return n;
}

//按「加 played 列之前」的结构建一个库并放一条对方发来的语音，用来验证存量库补列后的默认值。
//刻意手写旧建表语句：产品代码的建表语句已经带上新列了
void createLegacyVoiceDb(const std::wstring& path)
{
    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &db) == SQLITE_OK);
    const char* legacySql =
        "CREATE TABLE IF NOT EXISTS t_msg ("
        "msgid INTEGER NOT NULL,"
        "clientmsgid TEXT,"
        "ctype INTEGER NOT NULL,"
        "cid INTEGER NOT NULL,"
        "fromid INTEGER NOT NULL,"
        "msgtype INTEGER NOT NULL,"
        "content TEXT,"
        "search_text TEXT,"
        "is_link INTEGER NOT NULL DEFAULT 0,"
        "timestamp INTEGER NOT NULL,"
        "recalled INTEGER NOT NULL DEFAULT 0,"
        "pinned INTEGER NOT NULL DEFAULT 0,"
        "pin_time INTEGER NOT NULL DEFAULT 0,"
        "raw TEXT NOT NULL DEFAULT '',"
        "PRIMARY KEY (ctype,cid,msgid))";
    REQUIRE(sqlite3_exec(db, legacySql, NULL, NULL, NULL) == SQLITE_OK);
    const char* seedSql =
        "INSERT INTO t_msg (msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled) "
        "VALUES (5,'old-voice',1,100002,100002,4,'{}','',0,100,0)";
    REQUIRE(sqlite3_exec(db, seedSql, NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
}

} // namespace

TEST_CASE("VM8 语音消息的发送请求：msgtype=4，content 字段齐全，没有波形时不写 wave", "[voicemsg]")
{
    const std::string body = net::MessageProtocolClient::buildSendVoiceRequest(
        conversation_group, 300001, "CMID-1", "998877", "d41d8cd98f00b204e9800998ecf8427e", 8, 25600, "AAECAw==");
    const Json::Value root = parseJson(body);
    REQUIRE(root.isObject());
    CHECK(root["clientmsgid"].asString() == "CMID-1");
    CHECK(root["ctype"].asInt() == conversation_group);
    CHECK(root["cid"].asString() == "300001");
    CHECK(root["msgtype"].asInt() == message_type_voice);
    const Json::Value& content = root["content"];
    REQUIRE(content.isObject());
    CHECK(content["fileid"].isString());
    CHECK(content["fileid"].asString() == "998877");
    CHECK(content["md5"].asString() == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(content["duration"].asInt() == 8);
    CHECK(content["size"].asInt64() == 25600);
    CHECK(content["wave"].asString() == "AAECAw==");

    const Json::Value noWave = parseJson(net::MessageProtocolClient::buildSendVoiceRequest(
        conversation_single, kVcPeer, "CMID-2", "998877", "d41d8cd98f00b204e9800998ecf8427e", 1, 3000, ""));
    REQUIRE(noWave["content"].isObject());
    CHECK_FALSE(noWave["content"].isMember("wave"));
    CHECK(noWave["content"]["duration"].asInt() == 1);
}

TEST_CASE("VM9 收到语音消息：字段齐全、fileid 写成数字、没有 wave 都能解析", "[voicemsg]")
{
    net::MessageNotify full;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(voiceNotifyJson(7001, kVcPeer,
        "{\"fileid\":\"998877\",\"md5\":\"abc\",\"duration\":12,\"size\":40000,\"wave\":\"AAECAw==\"}"), full));
    CHECK(full.m_msgtype == message_type_voice);
    CHECK(full.m_voiceFileId == "998877");
    CHECK(full.m_voiceMd5 == "abc");
    CHECK(full.m_voiceDuration == 12);
    CHECK(full.m_voiceSize == 40000);
    CHECK(full.m_voiceWave == "AAECAw==");

    net::MessageNotify numeric;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(voiceNotifyJson(7002, kVcPeer,
        "{\"fileid\":998877,\"md5\":\"abc\",\"duration\":3,\"size\":9000}"), numeric));
    CHECK(numeric.m_voiceFileId == "998877");
    CHECK(numeric.m_voiceDuration == 3);
    CHECK(numeric.m_voiceWave.empty());
}

TEST_CASE("VM10 本地库：voice 子对象，played 列按发送方取初值、标记已听、重新同步不改、存量库补列为已听", "[voicemsg]")
{
    SECTION("BuildContentJson 写出 voice 子对象")
    {
        const Json::Value root = parseJson(balloonhistory::BuildContentJson(voiceNotify(1, kVcPeer)));
        REQUIRE(root["voice"].isObject());
        CHECK(root["voice"]["fileid"].asString() == "998877");
        CHECK(root["voice"]["md5"].asString() == "d41d8cd98f00b204e9800998ecf8427e");
        CHECK(root["voice"]["duration"].asInt() == 8);
        CHECK(root["voice"]["wave"].asString() == "AAECAw==");
    }
    SECTION("对方发的为未听，本人发的为已听；标记已听后不再出现；重新同步不改已听状态")
    {
        const std::wstring path = voiceTempDb(L"FlamingoUnit_VM10.db");
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(11, kVcPeer), conversation_single,
                                                              kVcPeer, kVcSelf)));
        REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(12, kVcSelf), conversation_single,
                                                              kVcPeer, kVcSelf)));
        REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(13, kVcPeer), conversation_single,
                                                              kVcPeer, kVcSelf)));
        std::vector<int64_t> unplayed;
        REQUIRE(store.LoadUnplayedVoiceIds(conversation_single, kVcPeer, unplayed));
        REQUIRE(unplayed.size() == 2);
        CHECK(unplayed[0] == 11);
        CHECK(unplayed[1] == 13);

        REQUIRE(store.MarkVoicePlayed(conversation_single, kVcPeer, 11));
        //重新同步同一条（例如再次拉历史）：已听状态保持
        REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(11, kVcPeer), conversation_single,
                                                              kVcPeer, kVcSelf)));
        REQUIRE(store.LoadUnplayedVoiceIds(conversation_single, kVcPeer, unplayed));
        REQUIRE(unplayed.size() == 1);
        CHECK(unplayed[0] == 13);
        //已撤回的语音不算未听
        REQUIRE(store.MarkRecalled(conversation_single, kVcPeer, 13));
        REQUIRE(store.LoadUnplayedVoiceIds(conversation_single, kVcPeer, unplayed));
        CHECK(unplayed.empty());
        store.Close();
        ::DeleteFileW(path.c_str());
    }
    SECTION("三参数版本（不知道本人 userid）一律记为已听")
    {
        const balloonhistory::HistoryRecord rec =
            balloonhistory::RecordFromNotify(voiceNotify(21, kVcPeer), conversation_single, kVcPeer);
        CHECK(rec.m_played);
        const balloonhistory::HistoryRecord peer =
            balloonhistory::RecordFromNotify(voiceNotify(21, kVcPeer), conversation_single, kVcPeer, kVcSelf);
        CHECK_FALSE(peer.m_played);
    }
    SECTION("存量库补上 played 列，加列之前已在库里的历史语音算已听")
    {
        const std::wstring path = voiceTempDb(L"FlamingoUnit_VM10_legacy.db");
        createLegacyVoiceDb(path);
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        std::vector<int64_t> unplayed;
        REQUIRE(store.LoadUnplayedVoiceIds(conversation_single, kVcPeer, unplayed));
        CHECK(unplayed.empty());
        //补列之后新来的对方语音照常记为未听
        REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(31, kVcPeer), conversation_single,
                                                              kVcPeer, kVcSelf)));
        REQUIRE(store.LoadUnplayedVoiceIds(conversation_single, kVcPeer, unplayed));
        REQUIRE(unplayed.size() == 1);
        CHECK(unplayed[0] == 31);
        store.Close();
        ::DeleteFileW(path.c_str());
    }
}

TEST_CASE("VM15 波形的 Base64 编码与解码，没有或不合法时得到均匀波形；时长换算成整秒", "[voicemsg]")
{
    std::vector<uint8_t> wave;
    for (int i = 0; i < net::kVoiceWaveBars; ++i)
        wave.push_back(static_cast<uint8_t>(i * 6));
    const std::string text = net::EncodeVoiceWave(wave);
    //40 字节的标准 Base64 是 56 个字符（带填充）
    CHECK(text.size() == 56);
    std::vector<uint8_t> back;
    REQUIRE(net::DecodeVoiceWave(text, back));
    CHECK(back == wave);
    CHECK(net::VoiceWaveForDisplay(text) == wave);
    CHECK(net::EncodeVoiceWave(std::vector<uint8_t>()).empty());

    //没有、非法、过长：解码失败，显示均匀波形
    const std::vector<uint8_t> uniform(static_cast<size_t>(net::kVoiceWaveBars), net::kVoiceUniformWaveLevel);
    CHECK_FALSE(net::DecodeVoiceWave("", back));
    CHECK_FALSE(net::DecodeVoiceWave("@@@@", back));
    CHECK_FALSE(net::DecodeVoiceWave(net::EncodeVoiceWave(std::vector<uint8_t>(65, 1)), back));
    CHECK(net::VoiceWaveForDisplay("") == uniform);
    CHECK(net::VoiceWaveForDisplay("not base64!") == uniform);

    //时长：毫秒向上取整到秒，限制在 1~120
    CHECK(net::VoiceDurationSeconds(1000) == 1);
    CHECK(net::VoiceDurationSeconds(1001) == 2);
    CHECK(net::VoiceDurationSeconds(7400) == 8);
    CHECK(net::VoiceDurationSeconds(0) == 1);
    CHECK(net::VoiceDurationSeconds(120000) == 120);
    CHECK(net::VoiceDurationSeconds(125000) == 120);
}
