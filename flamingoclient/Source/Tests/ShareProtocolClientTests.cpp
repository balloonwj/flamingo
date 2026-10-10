/**
 *  屏幕共享信令编解码（客户端侧）的单元测试（Suite SSPC，标签 [screenshare]），ShareProtocolClientTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareProtocolClient（docs/屏幕共享与录制方案.md 第 11.4 节 2c-1）：
 *    - SSPC1 八种请求与 1859 报到包体的字段、取值与 docs/协议设计-v2.md 第 17 节一致，64 位 id 一律是字符串，
 *            设置权限时「不变」的项不出现在包体里；
 *    - SSPC2 解析协议文档里的应答与推送示例（通用应答、1721、1725、1726、1727 两种、1729、1859 应答），字段一一对应；
 *    - SSPC3 缺必需字段、类型不对、不是 JSON 对象时一律解析失败；
 *    - SSPC4 房间快照按 userid 查成员；
 *    - SSMUC1 / SSFMC2（第 13 期）：按人静音的 muteuser 与 target、1726 的 elapsedms，强制模式与禁止观看者录制的字段、718；
 *    - SSRMC1（第 14 期）：移出观看者的 1732、1725 事件 10、1729 结束原因 3 / 4、719；
 *    - SSTIC5（第 15 期）：共享标题与描述，1720 带 title / desc、1733 的字段、1721 / 1725 / 1727 / 1734 的解析、720；
 *    - SSPWC3 共享密码（第 12 期）：发起与加入请求带上 password，没设密码时不带该字段；
 *            开始推送 1721 与查询应答 1727 的 locked 解析为「加入要不要密码」，缺省为不要。
 *  示例 JSON 直接取自协议文档，两边写法对不上时这里会失败。
 *
 *  典型用法：FlamingoUnit.exe "SSPC*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../net/Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace screenshare;

namespace {

bool sspcParse(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//协议文档第 17 节里的房间号示例
const char* kSspcRoom = "ss-5127-1790943882000-8";

//超过 2^53 的 userid，检查 64 位 id 以字符串原样往返
const int64_t kSspcHugeId = 9007199254740993LL;

} // namespace

TEST_CASE("SSPC1 八种请求与报到包体的字段与协议文档一致", "[screenshare]")
{
    Json::Value v;

    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false), v));
    CHECK(v["ctype"].asInt() == conversation_single);
    CHECK(v["cid"].isString());
    CHECK(v["cid"].asString() == "5308");
    CHECK(v["mic"].asInt() == 1);
    CHECK(v["camera"].asInt() == 0);
    CHECK(v["sysaudio"].asInt() == 0);
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_group, kSspcHugeId, false, true, true), v));
    CHECK(v["cid"].asString() == "9007199254740993");
    CHECK(v["camera"].asInt() == 1);
    CHECK(v["sysaudio"].asInt() == 1);

    REQUIRE(sspcParse(ShareProtocolClient::buildRoomRequest(kSspcRoom), v));
    CHECK(v["roomid"].asString() == kSspcRoom);
    CHECK(v.size() == 1);

    REQUIRE(sspcParse(ShareProtocolClient::buildQuery(conversation_group, 600001), v));
    CHECK(v["ctype"].asInt() == conversation_group);
    CHECK(v["cid"].asString() == "600001");

    REQUIRE(sspcParse(ShareProtocolClient::buildMemberMedia(kSspcRoom, true, false, true), v));
    CHECK(v["roomid"].asString() == kSspcRoom);
    CHECK(v["mic"].asInt() == 1);
    CHECK(v["camera"].asInt() == 0);
    CHECK(v["sysaudio"].asInt() == 1);

    //只收回录制权限：annotate 与 muteall 不出现
    REQUIRE(sspcParse(ShareProtocolClient::buildSetPermission(kSspcRoom, share_perm_unchanged, share_perm_revoke,
                                                              false), v));
    CHECK(v["record"].asInt() == 0);
    CHECK_FALSE(v.isMember("annotate"));
    CHECK_FALSE(v.isMember("muteall"));
    //允许标注并全员静音：record 不出现
    REQUIRE(sspcParse(ShareProtocolClient::buildSetPermission(kSspcRoom, share_perm_allow, share_perm_unchanged,
                                                              true), v));
    CHECK(v["annotate"].asInt() == 1);
    CHECK(v["muteall"].asInt() == 1);
    CHECK_FALSE(v.isMember("record"));

    REQUIRE(sspcParse(ShareProtocolClient::buildRecordState(kSspcRoom, true), v));
    CHECK(v["recording"].asInt() == 1);
    REQUIRE(sspcParse(ShareProtocolClient::buildRecordState(kSspcRoom, false), v));
    CHECK(v["recording"].asInt() == 0);

    REQUIRE(sspcParse(ShareProtocolClient::buildRelayHello("abc.def"), v));
    CHECK(v["token"].asString() == "abc.def");
}

TEST_CASE("SSPC2 解析协议文档里的应答与推送示例", "[screenshare]")
{
    ShareResult result;
    REQUIRE(ShareProtocolClient::parseResult("{\"code\":0,\"msg\":\"ok\",\"roomid\":\"ss-5127-1790943882000-8\"}",
                                             result));
    CHECK(result.m_code == 0);
    CHECK(result.m_msg == "ok");
    CHECK(result.m_roomId == kSspcRoom);
    REQUIRE(ShareProtocolClient::parseResult("{\"code\":714,\"msg\":\"already in another room\"}", result));
    CHECK(result.m_code == error_code_screenshare_in_other_room);
    CHECK(result.m_roomId.empty());

    //「张勇」的 UTF-8 编码
    const std::string zhangYong = "\xE5\xBC\xA0\xE5\x8B\x87";
    ShareStartedNotify started;
    REQUIRE(ShareProtocolClient::parseStartedNotify(
        "{\"roomid\":\"ss-5127-1790943882000-8\",\"ctype\":1,\"cid\":\"5127\",\"host\":\"5127\",\"hostname\":\"" +
            zhangYong + "\",\"starttime\":\"1790943882000\"}",
        started));
    CHECK(started.m_roomId == kSspcRoom);
    CHECK(started.m_ctype == conversation_single);
    CHECK(started.m_cid == 5127);
    CHECK(started.m_hostId == 5127);
    CHECK(started.m_hostName == zhangYong);
    CHECK(started.m_startTime == 1790943882000LL);

    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-5127-1790943882000-8\",\"event\":1,\"actor\":\"5308\",\"host\":\"5127\",\"hostonline\":1,"
        "\"annotateallowed\":1,\"recordallowed\":0,"
        "\"members\":[{\"uid\":\"5127\",\"seat\":0,\"mic\":1,\"camera\":0,\"sysaudio\":0,\"recording\":0},"
        "{\"uid\":\"5308\",\"seat\":1,\"mic\":0,\"camera\":1,\"sysaudio\":0,\"recording\":1}]}",
        state));
    CHECK(state.m_roomId == kSspcRoom);
    CHECK(state.m_event == share_event_member_joined);
    CHECK(state.m_actor == 5308);
    CHECK(state.m_hostId == 5127);
    CHECK(state.m_hostOnline);
    CHECK(state.m_annotateAllowed);
    CHECK_FALSE(state.m_recordAllowed);
    REQUIRE(state.m_members.size() == 2);
    CHECK(state.m_members[0].m_userId == 5127);
    CHECK(state.m_members[0].m_seat == 0);
    CHECK(state.m_members[0].m_mic);
    CHECK(state.m_members[1].m_userId == 5308);
    CHECK(state.m_members[1].m_seat == 1);
    CHECK(state.m_members[1].m_camera);
    CHECK(state.m_members[1].m_recording);

    ShareRelayInfo relay;
    REQUIRE(ShareProtocolClient::parseRelayInfo(
        "{\"roomid\":\"ss-5127-1790943882000-8\",\"relayip\":\"127.0.0.1\",\"relayport\":20003,\"token\":\"t.k\","
        "\"seat\":1,\"maxcameras\":9}",
        relay));
    CHECK(relay.m_roomId == kSspcRoom);
    CHECK(relay.m_relayIp == "127.0.0.1");
    CHECK(relay.m_relayPort == 20003);
    CHECK(relay.m_token == "t.k");
    CHECK(relay.m_seat == 1);
    CHECK(relay.m_maxCameras == 9);

    ShareQueryResult query;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        "{\"code\":0,\"msg\":\"ok\",\"exists\":1,\"roomid\":\"ss-5127-1790943882000-8\",\"host\":\"5127\","
        "\"membercount\":2,\"starttime\":\"1790943882000\"}",
        query));
    CHECK(query.m_code == 0);
    CHECK(query.m_exists);
    CHECK(query.m_roomId == kSspcRoom);
    CHECK(query.m_hostId == 5127);
    CHECK(query.m_memberCount == 2);
    CHECK(query.m_startTime == 1790943882000LL);
    REQUIRE(ShareProtocolClient::parseQueryResult("{\"code\":0,\"msg\":\"ok\",\"exists\":0}", query));
    CHECK_FALSE(query.m_exists);
    CHECK(query.m_roomId.empty());

    ShareEndedNotify ended;
    REQUIRE(ShareProtocolClient::parseEndedNotify(
        "{\"roomid\":\"ss-5127-1790943882000-8\",\"ctype\":1,\"cid\":\"5308\",\"host\":\"5127\",\"duration\":65,"
        "\"reason\":1}",
        ended));
    CHECK(ended.m_roomId == kSspcRoom);
    CHECK(ended.m_cid == 5308);
    CHECK(ended.m_hostId == 5127);
    CHECK(ended.m_durationSeconds == 65);
    CHECK(ended.m_reason == share_end_host_stopped);

    int code = -1;
    REQUIRE(ShareProtocolClient::parseRelayHelloAck("{\"code\":0,\"msg\":\"ok\"}", code));
    CHECK(code == 0);
}

TEST_CASE("SSPC3 缺必需字段、类型不对、不是 JSON 对象时解析失败", "[screenshare]")
{
    ShareResult result;
    CHECK_FALSE(ShareProtocolClient::parseResult("", result));
    CHECK_FALSE(ShareProtocolClient::parseResult("[1,2]", result));
    CHECK_FALSE(ShareProtocolClient::parseResult("{\"msg\":\"ok\"}", result));

    ShareStartedNotify started;
    CHECK_FALSE(ShareProtocolClient::parseStartedNotify("{\"ctype\":1,\"host\":\"5127\"}", started));
    CHECK_FALSE(ShareProtocolClient::parseStartedNotify("{\"roomid\":\"r\",\"ctype\":1}", started));

    ShareStateNotify state;
    CHECK_FALSE(ShareProtocolClient::parseStateNotify("{\"roomid\":\"r\",\"event\":1,\"members\":{}}", state));
    CHECK_FALSE(ShareProtocolClient::parseStateNotify("{\"roomid\":\"r\",\"members\":[]}", state));

    ShareRelayInfo relay;
    CHECK_FALSE(ShareProtocolClient::parseRelayInfo("{\"roomid\":\"r\",\"relayport\":20003}", relay));
    CHECK_FALSE(ShareProtocolClient::parseRelayInfo("{\"roomid\":\"r\",\"token\":\"t\"}", relay));

    ShareQueryResult query;
    CHECK_FALSE(ShareProtocolClient::parseQueryResult("{\"exists\":1}", query));

    ShareEndedNotify ended;
    CHECK_FALSE(ShareProtocolClient::parseEndedNotify("{\"reason\":1}", ended));

    int code = -1;
    CHECK_FALSE(ShareProtocolClient::parseRelayHelloAck("not json", code));
}

TEST_CASE("SSPC4 房间快照按 userid 查成员", "[screenshare]")
{
    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r\",\"event\":3,\"actor\":\"5308\",\"host\":\"5127\",\"hostonline\":0,\"annotateallowed\":0,"
        "\"recordallowed\":1,\"members\":[{\"uid\":\"5127\",\"seat\":0,\"mic\":0,\"camera\":0,\"sysaudio\":1,"
        "\"recording\":0},{\"uid\":\"5308\",\"seat\":2,\"mic\":1,\"camera\":0,\"sysaudio\":0,\"recording\":0}]}",
        state));
    CHECK_FALSE(state.m_hostOnline);
    CHECK_FALSE(state.m_annotateAllowed);
    const ShareMember* host = state.findMember(5127);
    REQUIRE(host != NULL);
    CHECK(host->m_systemAudio);
    const ShareMember* viewer = state.findMember(5308);
    REQUIRE(viewer != NULL);
    CHECK(viewer->m_seat == 2);
    CHECK(viewer->m_mic);
    CHECK(state.findMember(5001) == NULL);
}

TEST_CASE("SSPWC3 共享密码：请求带 password，推送与查询应答带 locked", "[screenshare][sharepassword]")
{
    Json::Value v;

    //设了密码：发起请求带 password（字符串，原样）
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_group, 600001, false, false, false, "482915"), v));
    CHECK(v["password"].isString());
    CHECK(v["password"].asString() == "482915");
    CHECK(v["cid"].asString() == "600001");
    //没设密码：不带 password 字段，与第 12 期之前的请求完全一样
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false, std::string()), v));
    CHECK_FALSE(v.isMember("password"));
    CHECK(ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false, std::string()) ==
          ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false));

    //加入请求：带密码时多一个 password 字段；不带时与只带房间号的请求一致
    REQUIRE(sspcParse(ShareProtocolClient::buildJoin(kSspcRoom, "000042"), v));
    CHECK(v["roomid"].asString() == kSspcRoom);
    CHECK(v["password"].asString() == "000042");
    CHECK(v.size() == 2);
    CHECK(ShareProtocolClient::buildJoin(kSspcRoom, std::string()) == ShareProtocolClient::buildRoomRequest(kSspcRoom));

    //开始推送 1721：locked 为 1 表示加入要密码；缺省（老服务端）为不要
    ShareStartedNotify started;
    REQUIRE(ShareProtocolClient::parseStartedNotify(
        "{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\",\"hostname\":\"h\","
        "\"starttime\":\"1790943882000\",\"locked\":1}",
        started));
    CHECK(started.m_locked);
    ShareStartedNotify startedOld;
    REQUIRE(ShareProtocolClient::parseStartedNotify(
        "{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\",\"hostname\":\"h\","
        "\"starttime\":\"1790943882000\"}",
        startedOld));
    CHECK_FALSE(startedOld.m_locked);

    //查询应答 1727：同上
    ShareQueryResult query;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        "{\"code\":0,\"msg\":\"ok\",\"exists\":1,\"roomid\":\"r\",\"host\":\"5127\",\"membercount\":2,"
        "\"starttime\":\"1790943882000\",\"locked\":1}",
        query));
    CHECK(query.m_locked);
    REQUIRE(ShareProtocolClient::parseQueryResult(
        "{\"code\":0,\"msg\":\"ok\",\"exists\":1,\"roomid\":\"r\",\"host\":\"5127\",\"membercount\":2,"
        "\"starttime\":\"1790943882000\",\"locked\":0}",
        query));
    CHECK_FALSE(query.m_locked);
    //没有进行中的共享时 locked 一律为 false
    REQUIRE(ShareProtocolClient::parseQueryResult("{\"code\":0,\"msg\":\"ok\",\"exists\":0,\"locked\":1}", query));
    CHECK_FALSE(query.m_locked);
}

TEST_CASE("SSP2PC1 单聊共享的 P2P 会话号：1726 与 1725 成员一项有就取出，没有时为空串（P2P 方案第 6 期）", "[screenshare][shareroom]")
{
    ShareRelayInfo relay;
    REQUIRE(ShareProtocolClient::parseRelayInfo(
        "{\"roomid\":\"r\",\"relayip\":\"127.0.0.1\",\"relayport\":20003,\"token\":\"t\",\"seat\":1,"
        "\"p2psessionid\":\"p2p-1-2-3-4\"}",
        relay));
    CHECK(relay.m_p2pSessionId == "p2p-1-2-3-4");
    REQUIRE(ShareProtocolClient::parseRelayInfo(
        "{\"roomid\":\"r\",\"relayip\":\"127.0.0.1\",\"relayport\":20003,\"token\":\"t\",\"seat\":0}", relay));
    CHECK(relay.m_p2pSessionId.empty());

    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r\",\"event\":1,\"actor\":\"5308\",\"host\":\"5127\",\"members\":["
        "{\"uid\":\"5127\",\"seat\":0},{\"uid\":\"5308\",\"seat\":1,\"p2psessionid\":\"p2p-1-2-3-4\"}]}",
        state));
    REQUIRE(state.m_members.size() == 2);
    CHECK(state.m_members[0].m_p2pSessionId.empty());
    CHECK(state.m_members[1].m_p2pSessionId == "p2p-1-2-3-4");
    CHECK(state.m_members[1].m_seat == 1);
}

TEST_CASE("SSMUC1 按人静音与共享时长：1730 带 muteuser，1725 的 target，1726 的 elapsedms", "[screenshare][shareroom]")
{
    Json::Value v;
    //按人静音：只带 muteuser（字符串 userid）
    REQUIRE(sspcParse(ShareProtocolClient::buildSetPermission(kSspcRoom, share_perm_unchanged, share_perm_unchanged,
                                                              false, 5308),
                      v));
    CHECK(v["muteuser"].isString());
    CHECK(v["muteuser"].asString() == "5308");
    CHECK(v.size() == 2);
    //不带 muteuser 时与原来完全一样
    CHECK(ShareProtocolClient::buildSetPermission(kSspcRoom, share_perm_allow, share_perm_unchanged, true, 0) ==
          ShareProtocolClient::buildSetPermission(kSspcRoom, share_perm_allow, share_perm_unchanged, true));

    //1725 事件 9：target 为被静音的人；其它事件没有 target
    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r\",\"event\":9,\"actor\":\"5127\",\"target\":\"5308\",\"host\":\"5127\",\"hostonline\":1,"
        "\"annotateallowed\":1,\"recordallowed\":1,\"forced\":0,\"members\":[]}",
        state));
    CHECK(state.m_event == share_event_member_muted);
    CHECK(state.m_target == 5308);
    REQUIRE(ShareProtocolClient::parseStateNotify("{\"roomid\":\"r\",\"event\":3,\"actor\":\"5308\",\"members\":[]}",
                                                  state));
    CHECK(state.m_target == 0);

    //1726：elapsedms；老服务端没有该字段时为 0
    ShareRelayInfo relay;
    REQUIRE(ShareProtocolClient::parseRelayInfo(
        "{\"roomid\":\"r\",\"relayip\":\"127.0.0.1\",\"relayport\":20003,\"token\":\"t\",\"seat\":1,\"maxcameras\":9,"
        "\"elapsedms\":65500}",
        relay));
    CHECK(relay.m_elapsedMs == 65500);
    REQUIRE(ShareProtocolClient::parseRelayInfo(
        "{\"roomid\":\"r\",\"relayip\":\"127.0.0.1\",\"relayport\":20003,\"token\":\"t\",\"seat\":1}", relay));
    CHECK(relay.m_elapsedMs == 0);
}

TEST_CASE("SSFMC2 强制模式与禁止观看者录制：1720 的字段，1721 / 1725 / 1727 的 forced 与 rejoin，718", "[screenshare][shareroom]")
{
    Json::Value v;
    //强制模式、禁止观看者录制：只在与缺省不同时带上
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_group, 600001, false, false, false, std::string(), true,
                                                      false),
                      v));
    CHECK(v["forced"].asInt() == 1);
    CHECK(v["recordallowed"].asInt() == 0);
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(conversation_group, 600001, false, false, false, "482915", false,
                                                      true),
                      v));
    CHECK_FALSE(v.isMember("forced"));
    CHECK_FALSE(v.isMember("recordallowed"));
    CHECK(v["password"].asString() == "482915");
    CHECK(ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false, std::string(), false, true) ==
          ShareProtocolClient::buildStart(conversation_single, 5308, true, false, false));

    //1721：forced 与 rejoin；老服务端没有时都为 false
    ShareStartedNotify started;
    REQUIRE(ShareProtocolClient::parseStartedNotify(
        "{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\",\"hostname\":\"\",\"starttime\":\"1\","
        "\"locked\":0,\"forced\":1,\"rejoin\":1}",
        started));
    CHECK(started.m_forced);
    CHECK(started.m_rejoin);
    REQUIRE(ShareProtocolClient::parseStartedNotify("{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\"}",
                                                    started));
    CHECK_FALSE(started.m_forced);
    CHECK_FALSE(started.m_rejoin);

    //1725 与 1727：forced
    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r\",\"event\":1,\"actor\":\"5308\",\"host\":\"5127\",\"forced\":1,\"members\":[]}", state));
    CHECK(state.m_forced);
    ShareQueryResult query;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        "{\"code\":0,\"msg\":\"ok\",\"exists\":1,\"roomid\":\"r\",\"host\":\"5127\",\"membercount\":2,"
        "\"starttime\":\"1\",\"locked\":0,\"forced\":1}",
        query));
    CHECK(query.m_forced);
    REQUIRE(ShareProtocolClient::parseQueryResult("{\"code\":0,\"msg\":\"ok\",\"exists\":0,\"forced\":1}", query));
    CHECK_FALSE(query.m_forced);

    CHECK(error_code_screenshare_forced_leave == 718);
}

TEST_CASE("SSRMC1 移出观看者：1732 的字段，1725 事件 10 的 target，1729 结束原因 3 / 4，719", "[screenshare][shareroom]")
{
    Json::Value v;
    REQUIRE(sspcParse(ShareProtocolClient::buildRemoveMember(kSspcRoom, 5308, true), v));
    CHECK(v["roomid"].asString() == kSspcRoom);
    CHECK(v["userid"].isString());
    CHECK(v["userid"].asString() == "5308");
    CHECK(v["allowrejoin"].isInt());
    CHECK(v["allowrejoin"].asInt() == 1);
    CHECK(v.size() == 3);
    REQUIRE(sspcParse(ShareProtocolClient::buildRemoveMember(kSspcRoom, 5308, false), v));
    CHECK(v["allowrejoin"].asInt() == 0);

    //1725 事件 10：target 为被移出的人
    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r\",\"event\":10,\"actor\":\"5127\",\"target\":\"5308\",\"host\":\"5127\",\"hostonline\":1,"
        "\"annotateallowed\":1,\"recordallowed\":1,\"members\":[{\"uid\":\"5127\",\"seat\":0,\"mic\":0,\"camera\":0,"
        "\"sysaudio\":0,\"recording\":0}]}",
        state));
    CHECK(state.m_event == share_event_member_removed);
    CHECK(state.m_target == 5308);
    CHECK(state.findMember(5308) == NULL);

    //1729 结束原因 3 / 4
    ShareEndedNotify ended;
    REQUIRE(ShareProtocolClient::parseEndedNotify(
        "{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\",\"duration\":65,\"reason\":3}", ended));
    CHECK(ended.m_reason == share_end_removed);
    REQUIRE(ShareProtocolClient::parseEndedNotify(
        "{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"host\":\"5127\",\"duration\":65,\"reason\":4}", ended));
    CHECK(ended.m_reason == share_end_removed_banned);

    CHECK(share_event_member_removed == 10);
    CHECK(share_end_removed == 3);
    CHECK(share_end_removed_banned == 4);
    CHECK(cmd_screenshare_remove_member == 1732);
    CHECK(error_code_screenshare_removed == 719);
}

TEST_CASE("SSTIC5 共享标题与描述：1720 带 title / desc，1733 的字段，1721 / 1725 / 1727 / 1734 的解析，720", "[screenshare][shareroom]")
{
    Json::Value v;
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(2, 600001, false, false, false, std::string(), false, true,
                                                      u8"周会演示", u8"第三季度汇报"),
                      v));
    CHECK(v["title"].asString() == u8"周会演示");
    CHECK(v["desc"].asString() == u8"第三季度汇报");
    //描述没填：字段照样带上，为空串
    REQUIRE(sspcParse(ShareProtocolClient::buildStart(2, 600001, false, false, false, std::string(), false, true,
                                                      u8"周会演示", ""),
                      v));
    CHECK(v.isMember("desc"));
    CHECK(v["desc"].asString().empty());

    REQUIRE(sspcParse(ShareProtocolClient::buildSetInfo(kSspcRoom, u8"新标题", ""), v));
    CHECK(v["roomid"].asString() == kSspcRoom);
    CHECK(v["title"].asString() == u8"新标题");
    CHECK(v.isMember("desc"));
    CHECK(v.size() == 3);

    ShareStartedNotify started;
    REQUIRE(ShareProtocolClient::parseStartedNotify(
        u8"{\"roomid\":\"r\",\"ctype\":1,\"cid\":\"5127\",\"host\":\"5127\",\"title\":\"周会演示\",\"desc\":\"汇报\"}", started));
    CHECK(started.m_title == u8"周会演示");
    CHECK(started.m_desc == u8"汇报");
    //老服务端不带：为空
    REQUIRE(ShareProtocolClient::parseStartedNotify("{\"roomid\":\"r\",\"host\":\"5127\"}", started));
    CHECK(started.m_title.empty());

    ShareStateNotify state;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        u8"{\"roomid\":\"r\",\"event\":1,\"actor\":\"5308\",\"host\":\"5127\",\"title\":\"周会演示\",\"desc\":\"\","
        u8"\"members\":[]}",
        state));
    CHECK(state.m_title == u8"周会演示");
    CHECK(state.m_desc.empty());

    ShareQueryResult query;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        u8"{\"code\":0,\"msg\":\"ok\",\"exists\":1,\"roomid\":\"r\",\"host\":\"5127\",\"title\":\"周会演示\",\"desc\":\"汇报\"}",
        query));
    CHECK(query.m_title == u8"周会演示");
    CHECK(query.m_desc == u8"汇报");

    ShareInfoNotify info;
    REQUIRE(ShareProtocolClient::parseInfoNotify(
        u8"{\"roomid\":\"r\",\"ctype\":2,\"cid\":\"600001\",\"title\":\"新标题\",\"desc\":\"新描述\",\"msgid\":\"9001\","
        u8"\"content\":\"{\\\"roomid\\\":\\\"r\\\"}\"}",
        info));
    CHECK(info.m_roomId == "r");
    CHECK(info.m_ctype == 2);
    CHECK(info.m_cid == 600001);
    CHECK(info.m_title == u8"新标题");
    CHECK(info.m_desc == u8"新描述");
    CHECK(info.m_cardMsgId == 9001);
    CHECK(info.m_cardContent == "{\"roomid\":\"r\"}");
    //服务端没能改写卡片：没有 msgid 与 content
    REQUIRE(ShareProtocolClient::parseInfoNotify("{\"roomid\":\"r\",\"ctype\":1,\"cid\":\"5127\",\"title\":\"t\"}", info));
    CHECK(info.m_cardMsgId == 0);
    CHECK(info.m_cardContent.empty());
    CHECK_FALSE(ShareProtocolClient::parseInfoNotify("{\"title\":\"t\"}", info));

    CHECK(cmd_screenshare_set_info == 1733);
    CHECK(cmd_screenshare_info_notify == 1734);
    CHECK(error_code_screenshare_sensitive == 720);
}
