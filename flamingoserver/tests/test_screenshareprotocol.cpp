/**
 *  屏幕共享信令编解码单元测试（Suite SCP），test_screenshareprotocol.cpp
 *
 *  覆盖 ScreenShareProtocol（docs/屏幕共享与录制方案.md 第 11.4 节 2a-1，2026-10-02 与用户确认的用例）：
 *    - SCP1 八种请求（发起、加入、离开、结束、查询、成员媒体、设置权限、录制状态）的解析，以及缺字段、
 *           会话类型非法、会话 id 非正、房间号为空、字段类型不对时一律拒绝；
 *    - SCP2 六种应答与推送（通用应答、开始推送、状态推送、中转参数、查询应答、结束推送）构造后再解析，
 *           字段一一对应，中文昵称不出错，64 位 id 一律是字符串。
 *  另有 SCP3：协议常量取值与方案一致（两份 Protocol.h 的一致性由 PP 套件守卫）。
 *  第 12 期补 SSPW1、SSPW3（加入密码）；第 13 期补 SSMU2（muteuser 与 1725 的 target、事件 9）、SSDUR1（1726 的 elapsedms）、
 *  SSFM1（1720 的 forced / recordallowed，1721 / 1725 / 1727 / 卡片的 forced，718）；第 14 期补 SSRM2（1732 的解析、
 *  1725 事件 10、1729 结束原因 3 / 4、719）；第 15 期补 SSTI1（共享标题与描述：1720 的解析，1721 / 1725 / 1727 / 卡片带上，720）、
 *  SSTI3（修改标题与描述：1733 的解析、1734 的字段）；第 18 期补 SSCT4（控制键鼠：1735 的解析、1736 的字段、1725 事件 11 / 12
 *  与成员的 controlledby、729 / 730）；2026-10-08 补 SSTI5（移交主持人后的 1734 带 hostid 与 hostname）。
 *
 *  典型用法：Tests.exe --test-case="SCP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>

#include "doctest.h"
#include "../chatserversrc/ScreenShareProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
bool scpParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//「张三」的 UTF-8 编码，用来检查中文昵称原样往返
const char* kZhangSanUtf8 = "\xE5\xBC\xA0\xE4\xB8\x89";

} // namespace

TEST_CASE("SCP1 八种请求的解析与非法输入拒绝")
{
    SUBCASE("发起共享")
    {
        int ctype = 0;
        int64_t cid = 0;
        bool mic = false;
        bool camera = true;
        bool sysAudio = false;
        REQUIRE(ScreenShareProtocol::parseStartRequest(
            "{\"ctype\":1,\"cid\":\"100001\",\"mic\":1,\"camera\":0,\"sysaudio\":1}", ctype, cid, mic, camera, sysAudio));
        CHECK(ctype == conversation_single);
        CHECK(cid == 100001);
        CHECK(mic);
        CHECK_FALSE(camera);
        CHECK(sysAudio);

        //三个开关缺省按 false
        REQUIRE(ScreenShareProtocol::parseStartRequest("{\"ctype\":2,\"cid\":\"600001\"}", ctype, cid, mic, camera,
                                                       sysAudio));
        CHECK(ctype == conversation_group);
        CHECK_FALSE(mic);
        CHECK_FALSE(camera);
        CHECK_FALSE(sysAudio);

        CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":3,\"cid\":\"1\"}", ctype, cid, mic, camera,
                                                           sysAudio));
        CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"0\"}", ctype, cid, mic, camera,
                                                           sysAudio));
        CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1}", ctype, cid, mic, camera, sysAudio));
        //会话 id 必须是字符串编码
        CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":100001}", ctype, cid, mic, camera,
                                                           sysAudio));
        CHECK_FALSE(ScreenShareProtocol::parseStartRequest("not json", ctype, cid, mic, camera, sysAudio));
    }

    SUBCASE("只带房间号的请求：加入、离开、结束")
    {
        std::string roomId;
        REQUIRE(ScreenShareProtocol::parseRoomRequest("{\"roomid\":\"ss-1001-1\"}", roomId));
        CHECK(roomId == "ss-1001-1");
        CHECK_FALSE(ScreenShareProtocol::parseRoomRequest("{\"roomid\":\"\"}", roomId));
        CHECK_FALSE(ScreenShareProtocol::parseRoomRequest("{}", roomId));
        CHECK_FALSE(ScreenShareProtocol::parseRoomRequest("{\"roomid\":12}", roomId));
        CHECK_FALSE(ScreenShareProtocol::parseRoomRequest("[]", roomId));
    }

    SUBCASE("查询")
    {
        int ctype = 0;
        int64_t cid = 0;
        REQUIRE(ScreenShareProtocol::parseQueryRequest("{\"ctype\":2,\"cid\":\"600001\"}", ctype, cid));
        CHECK(ctype == conversation_group);
        CHECK(cid == 600001);
        CHECK_FALSE(ScreenShareProtocol::parseQueryRequest("{\"ctype\":0,\"cid\":\"600001\"}", ctype, cid));
        CHECK_FALSE(ScreenShareProtocol::parseQueryRequest("{\"ctype\":2,\"cid\":\"-5\"}", ctype, cid));
    }

    SUBCASE("成员媒体：三项都必须给出")
    {
        std::string roomId;
        bool mic = false;
        bool camera = false;
        bool sysAudio = true;
        REQUIRE(ScreenShareProtocol::parseMemberMediaRequest(
            "{\"roomid\":\"r1\",\"mic\":1,\"camera\":1,\"sysaudio\":0}", roomId, mic, camera, sysAudio));
        CHECK(roomId == "r1");
        CHECK(mic);
        CHECK(camera);
        CHECK_FALSE(sysAudio);
        CHECK_FALSE(ScreenShareProtocol::parseMemberMediaRequest("{\"roomid\":\"r1\",\"mic\":1,\"sysaudio\":0}", roomId,
                                                                 mic, camera, sysAudio));
        CHECK_FALSE(ScreenShareProtocol::parseMemberMediaRequest("{\"mic\":1,\"camera\":1,\"sysaudio\":0}", roomId, mic,
                                                                 camera, sysAudio));
    }

    SUBCASE("设置权限：省略即不变，至少一项操作")
    {
        std::string roomId;
        int annotate = 9;
        int record = 9;
        bool muteAll = true;
        REQUIRE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"r1\",\"annotate\":0}", roomId, annotate,
                                                            record, muteAll));
        CHECK(annotate == 0);
        CHECK(record == -1);
        CHECK_FALSE(muteAll);

        REQUIRE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"r1\",\"record\":1,\"muteall\":1}", roomId,
                                                            annotate, record, muteAll));
        CHECK(annotate == -1);
        CHECK(record == 1);
        CHECK(muteAll);

        REQUIRE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"r1\",\"muteall\":1}", roomId, annotate, record,
                                                            muteAll));
        CHECK(muteAll);

        CHECK_FALSE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"r1\"}", roomId, annotate, record, muteAll));
        CHECK_FALSE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"r1\",\"muteall\":0}", roomId, annotate,
                                                                record, muteAll));
        CHECK_FALSE(ScreenShareProtocol::parsePermissionRequest("{\"annotate\":1}", roomId, annotate, record, muteAll));
    }

    SUBCASE("录制状态")
    {
        std::string roomId;
        bool recording = false;
        REQUIRE(ScreenShareProtocol::parseRecordStateRequest("{\"roomid\":\"r1\",\"recording\":1}", roomId, recording));
        CHECK(roomId == "r1");
        CHECK(recording);
        REQUIRE(ScreenShareProtocol::parseRecordStateRequest("{\"roomid\":\"r1\",\"recording\":0}", roomId, recording));
        CHECK_FALSE(recording);
        CHECK_FALSE(ScreenShareProtocol::parseRecordStateRequest("{\"roomid\":\"r1\"}", roomId, recording));
    }
}

TEST_CASE("SCP2 六种应答与推送构造后再解析，字段一一对应")
{
    Json::Value root;

    SUBCASE("通用应答")
    {
        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildResult(0, "ok", "r1"), root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["roomid"].asString() == "r1");

        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildResult(error_code_screenshare_not_member, "not member", ""),
                                 root));
        CHECK(root["code"].asInt() == error_code_screenshare_not_member);
        CHECK_FALSE(root.isMember("roomid"));
    }

    SUBCASE("开始共享推送")
    {
        REQUIRE(scpParseJsonText(
            ScreenShareProtocol::buildStartedNotify("r1", conversation_single, 1001, 1001, kZhangSanUtf8, 1790000000123LL),
            root));
        CHECK(root["roomid"].asString() == "r1");
        CHECK(root["ctype"].asInt() == conversation_single);
        CHECK(root["cid"].asString() == "1001");
        CHECK(root["host"].asString() == "1001");
        CHECK(root["hostname"].asString() == kZhangSanUtf8);
        CHECK(root["starttime"].asString() == "1790000000123");
    }

    SUBCASE("状态推送附完整房间快照")
    {
        ScreenShareRoom room;
        room.m_roomId = "r1";
        room.m_hostId = 1001;
        room.m_annotateAllowed = false;
        room.m_recordAllowed = true;
        room.m_hostOfflineSince = 5;
        ScreenShareMember host;
        host.m_userId = 1001;
        host.m_seat = 0;
        host.m_mic = true;
        ScreenShareMember viewer;
        viewer.m_userId = 1002;
        viewer.m_seat = 1;
        viewer.m_camera = true;
        viewer.m_recording = true;
        room.m_members.push_back(host);
        room.m_members.push_back(viewer);

        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, 1002, room), root));
        CHECK(root["roomid"].asString() == "r1");
        CHECK(root["event"].asInt() == screenshare_event_member_joined);
        CHECK(root["actor"].asString() == "1002");
        CHECK(root["host"].asString() == "1001");
        CHECK(root["hostonline"].asInt() == 0);
        CHECK(root["annotateallowed"].asInt() == 0);
        CHECK(root["recordallowed"].asInt() == 1);
        REQUIRE(root["members"].isArray());
        REQUIRE(root["members"].size() == 2);
        const Json::Value& m0 = root["members"][0];
        const Json::Value& m1 = root["members"][1];
        CHECK(m0["uid"].asString() == "1001");
        CHECK(m0["seat"].asInt() == 0);
        CHECK(m0["mic"].asInt() == 1);
        CHECK(m0["camera"].asInt() == 0);
        CHECK(m1["uid"].asString() == "1002");
        CHECK(m1["seat"].asInt() == 1);
        CHECK(m1["camera"].asInt() == 1);
        CHECK(m1["recording"].asInt() == 1);
        CHECK(m1["sysaudio"].asInt() == 0);
    }

    SUBCASE("中转参数")
    {
        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildRelayInfo("r1", "10.0.0.5", 20003, "tok.sig", 3, 9), root));
        CHECK(root["roomid"].asString() == "r1");
        CHECK(root["relayip"].asString() == "10.0.0.5");
        CHECK(root["relayport"].asInt() == 20003);
        CHECK(root["token"].asString() == "tok.sig");
        CHECK(root["seat"].asInt() == 3);
        CHECK(root["maxcameras"].asInt() == 9);
    }

    SUBCASE("查询应答：有共享与没有共享")
    {
        ScreenShareRoom room;
        room.m_roomId = "r1";
        room.m_hostId = 1001;
        room.m_startTime = 1790000000000LL;
        room.m_members.resize(3);
        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", &room), root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["exists"].asInt() == 1);
        CHECK(root["roomid"].asString() == "r1");
        CHECK(root["host"].asString() == "1001");
        CHECK(root["membercount"].asInt() == 3);
        CHECK(root["starttime"].asString() == "1790000000000");

        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", NULL), root));
        CHECK(root["exists"].asInt() == 0);
        CHECK_FALSE(root.isMember("roomid"));
    }

    SUBCASE("结束推送")
    {
        REQUIRE(scpParseJsonText(ScreenShareProtocol::buildEndedNotify("r1", conversation_group, 600001, 1001, 125,
                                                                      screenshare_end_host_timeout), root));
        CHECK(root["roomid"].asString() == "r1");
        CHECK(root["ctype"].asInt() == conversation_group);
        CHECK(root["cid"].asString() == "600001");
        CHECK(root["host"].asString() == "1001");
        CHECK(root["duration"].asInt() == 125);
        CHECK(root["reason"].asInt() == screenshare_end_host_timeout);
    }
}

TEST_CASE("SCP3 协议常量取值与方案一致")
{
    CHECK(cmd_screenshare_start == 1720);
    CHECK(cmd_screenshare_record_state == 1731);
    CHECK(cmd_screenshare_relay_hello == 1859);
    CHECK(cmd_screenshare_relay_data == 1860);
    CHECK(error_code_screenshare_not_exist == 708);
    CHECK(error_code_screenshare_not_host == 716);
    CHECK(message_type_screenshare == 19);
}

TEST_CASE("SSPW1 共享密码：发起与加入请求的解析，密码须正好是 6 位数字")
{
    int ctype = 0;
    int64_t cid = 0;
    bool mic = false;
    bool camera = false;
    bool sysAudio = false;
    std::string password = "x";
    //不带密码：解析成功，密码为空
    REQUIRE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0}", ctype, cid,
                                                   mic, camera, sysAudio, password));
    CHECK(password.empty());
    //6 位数字：解析成功
    REQUIRE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":\"482915\"}", ctype, cid, mic, camera,
        sysAudio, password));
    CHECK(password == "482915");
    //空串等同不设密码
    REQUIRE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":\"\"}", ctype, cid, mic, camera, sysAudio,
        password));
    CHECK(password.empty());
    //5 位、7 位、带字母、不是字符串：整个请求按非法处理
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":\"48291\"}", ctype, cid, mic, camera, sysAudio,
        password));
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":\"4829150\"}", ctype, cid, mic, camera,
        sysAudio, password));
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":\"48a915\"}", ctype, cid, mic, camera,
        sysAudio, password));
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":1,\"cid\":\"5308\",\"mic\":0,\"camera\":0,\"password\":482915}", ctype, cid, mic, camera, sysAudio,
        password));

    //加入请求：房间号必填，密码可选
    std::string roomId;
    REQUIRE(ScreenShareProtocol::parseJoinRequest("{\"roomid\":\"ss-1\"}", roomId, password));
    CHECK(roomId == "ss-1");
    CHECK(password.empty());
    REQUIRE(ScreenShareProtocol::parseJoinRequest("{\"roomid\":\"ss-1\",\"password\":\"000123\"}", roomId, password));
    CHECK(password == "000123");
    CHECK_FALSE(ScreenShareProtocol::parseJoinRequest("{\"password\":\"000123\"}", roomId, password));
}

TEST_CASE("SSPW3 共享密码：开始推送、查询应答、共享卡片带「需要密码」标记，不带密码本身")
{
    //开始共享推送
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStartedNotify("ss-1", 1, 5308, 5127, "a", 1700000000000LL, true),
                             root));
    CHECK(root["locked"].asInt() == 1);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStartedNotify("ss-1", 1, 5308, 5127, "a", 1700000000000LL, false),
                             root));
    CHECK(root["locked"].asInt() == 0);

    //共享卡片
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildCardContent("ss-1", 5127, "a", 1700000000000LL, true), root));
    CHECK(root["locked"].asInt() == 1);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildCardContent("ss-1", 5127, "a", 1700000000000LL, false), root));
    CHECK(root["locked"].asInt() == 0);

    //查询应答：按房间是否有密码
    ScreenShareRoom room;
    room.m_roomId = "ss-2";
    room.m_ctype = 1;
    room.m_hostId = 5127;
    room.m_password = "482915";
    const std::string query = ScreenShareProtocol::buildQueryResponse(0, "ok", &room);
    REQUIRE(scpParseJsonText(query, root));
    CHECK(root["locked"].asInt() == 1);
    room.m_password.clear();
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", &room), root));
    CHECK(root["locked"].asInt() == 0);

    //密码本身不出现在任何推送、卡片、应答里
    room.m_password = "482915";
    CHECK(ScreenShareProtocol::buildQueryResponse(0, "ok", &room).find("482915") == std::string::npos);
    CHECK(ScreenShareProtocol::buildStateNotify(1, 5127, room).find("482915") == std::string::npos);

    //错误码取值
    CHECK(error_code_screenshare_bad_password == 717);
}

TEST_CASE("SSMU2 按人静音：1730 的 muteuser 解析，1725 带 target 与新事件 9")
{
    std::string roomId;
    int annotate = 0;
    int record = 0;
    bool muteAll = true;
    int64_t muteUser = 0;
    //只带 muteuser 也算一项操作
    REQUIRE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"ss-1\",\"muteuser\":\"5308\"}", roomId, annotate,
                                                        record, muteAll, muteUser));
    CHECK(roomId == "ss-1");
    CHECK(muteUser == 5308);
    CHECK(annotate == -1);
    CHECK(record == -1);
    CHECK_FALSE(muteAll);
    //不带 muteuser 时为 0
    REQUIRE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"ss-1\",\"muteall\":1}", roomId, annotate, record,
                                                        muteAll, muteUser));
    CHECK(muteUser == 0);
    CHECK(muteAll);
    //muteuser 不是数字字符串、或不大于 0：整个请求非法
    CHECK_FALSE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"ss-1\",\"muteuser\":\"abc\"}", roomId,
                                                            annotate, record, muteAll, muteUser));
    CHECK_FALSE(ScreenShareProtocol::parsePermissionRequest("{\"roomid\":\"ss-1\",\"muteuser\":\"0\"}", roomId, annotate,
                                                            record, muteAll, muteUser));

    //1725：带 target 时下发，不带时没有该字段
    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_muted, 5127, room, 5308), root));
    CHECK(root["event"].asInt() == 9);
    CHECK(root["target"].asString() == "5308");
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_media_changed, 5308, room), root));
    CHECK_FALSE(root.isMember("target"));
    CHECK(screenshare_event_member_muted == 9);
}

TEST_CASE("SSP2P1 单聊共享的 P2P 会话号：1726 与快照里的成员给了才带，没给时与原来相同（P2P 方案第 6 期）")
{
    const std::string withP2P = ScreenShareProtocol::buildRelayInfo("ss-1", "127.0.0.1", 20003, "tok", 1, 9, 0, "p2p-1-2-3-4");
    Json::Value root;
    REQUIRE(scpParseJsonText(withP2P, root));
    CHECK(root["p2psessionid"].asString() == "p2p-1-2-3-4");
    CHECK(root["seat"].asInt() == 1);
    const std::string plain = ScreenShareProtocol::buildRelayInfo("ss-1", "127.0.0.1", 20003, "tok", 1, 9, 0);
    REQUIRE(scpParseJsonText(plain, root));
    CHECK_FALSE(root.isMember("p2psessionid"));
    CHECK(plain == ScreenShareProtocol::buildRelayInfo("ss-1", "127.0.0.1", 20003, "tok", 1, 9, 0, std::string()));

    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_hostId = 5127;
    ScreenShareMember host;
    host.m_userId = 5127;
    host.m_seat = 0;
    ScreenShareMember viewer;
    viewer.m_userId = 5308;
    viewer.m_seat = 1;
    viewer.m_p2pSessionId = "p2p-1-2-3-4";
    room.m_members.push_back(host);
    room.m_members.push_back(viewer);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, 5308, room), root));
    REQUIRE(root["members"].size() == 2);
    CHECK_FALSE(root["members"][0].isMember("p2psessionid"));
    CHECK(root["members"][1]["p2psessionid"].asString() == "p2p-1-2-3-4");
}

TEST_CASE("SSDUR1 共享时长：1726 带 elapsedms，负数按 0")
{
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildRelayInfo("ss-1", "127.0.0.1", 20003, "t", 1, 9, 65000), root));
    CHECK(root["elapsedms"].asInt64() == 65000);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildRelayInfo("ss-1", "127.0.0.1", 20003, "t", 1, 9, -5), root));
    CHECK(root["elapsedms"].asInt64() == 0);
}

TEST_CASE("SSFM1 强制模式与禁止观看者录制：1720 的解析，1721 / 1725 / 1727 / 卡片带 forced，718 取值")
{
    ScreenShareStartRequest req;
    REQUIRE(ScreenShareProtocol::parseStartRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"mic\":1,\"password\":\"482915\",\"forced\":1,\"recordallowed\":0}", req));
    CHECK(req.m_ctype == conversation_group);
    CHECK(req.m_cid == 600001);
    CHECK(req.m_mic);
    CHECK(req.m_password == "482915");
    CHECK(req.m_forced);
    CHECK_FALSE(req.m_recordAllowed);
    //缺省：普通模式、允许录制
    REQUIRE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\"}", req));
    CHECK_FALSE(req.m_forced);
    CHECK(req.m_recordAllowed);
    CHECK(req.m_password.empty());
    //开关字段不是整数：非法
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\",\"forced\":\"yes\"}", req));
    //密码非法时同样拒绝
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\",\"password\":\"12\"}", req));

    Json::Value root;
    //1721：forced 与 rejoin（强制模式下观看者掉线后重新登录时推送）
    REQUIRE(scpParseJsonText(
        ScreenShareProtocol::buildStartedNotify("ss-1", 2, 600001, 5127, "a", 1700000000000LL, false, true, true), root));
    CHECK(root["forced"].asInt() == 1);
    CHECK(root["rejoin"].asInt() == 1);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStartedNotify("ss-1", 2, 600001, 5127, "a", 1700000000000LL), root));
    CHECK(root["forced"].asInt() == 0);
    CHECK(root["rejoin"].asInt() == 0);

    //卡片
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildCardContent("ss-1", 5127, "a", 1700000000000LL, false, true), root));
    CHECK(root["forced"].asInt() == 1);

    //查询应答与房间状态按房间是否强制模式
    ScreenShareRoom room;
    room.m_roomId = "ss-2";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    room.m_forced = true;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", &room), root));
    CHECK(root["forced"].asInt() == 1);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, 5308, room), root));
    CHECK(root["forced"].asInt() == 1);
    room.m_forced = false;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, 5308, room), root));
    CHECK(root["forced"].asInt() == 0);

    CHECK(error_code_screenshare_forced_leave == 718);
}

TEST_CASE("SSRM2 移出观看者：1732 的解析，1725 事件 10 带 target，1729 结束原因 3 / 4，719 取值")
{
    std::string roomId;
    int64_t userId = 0;
    bool allowRejoin = true;
    REQUIRE(ScreenShareProtocol::parseRemoveMemberRequest(
        "{\"roomid\":\"ss-1\",\"userid\":\"5308\",\"allowrejoin\":0}", roomId, userId, allowRejoin));
    CHECK(roomId == "ss-1");
    CHECK(userId == 5308);
    CHECK_FALSE(allowRejoin);
    //allowrejoin 缺省为允许
    REQUIRE(ScreenShareProtocol::parseRemoveMemberRequest("{\"roomid\":\"ss-1\",\"userid\":\"5308\"}", roomId,
                                                          userId, allowRejoin));
    CHECK(allowRejoin);
    //缺 userid、userid 不是大于 0 的数字字符串、allowrejoin 不是整数、缺 roomid：非法
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest("{\"roomid\":\"ss-1\"}", roomId, userId, allowRejoin));
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest("{\"roomid\":\"ss-1\",\"userid\":\"0\"}", roomId,
                                                              userId, allowRejoin));
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest("{\"roomid\":\"ss-1\",\"userid\":\"abc\"}", roomId,
                                                              userId, allowRejoin));
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest(
        "{\"roomid\":\"ss-1\",\"userid\":\"5308\",\"allowrejoin\":\"no\"}", roomId, userId, allowRejoin));
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest("{\"userid\":\"5308\"}", roomId, userId, allowRejoin));
    CHECK_FALSE(ScreenShareProtocol::parseRemoveMemberRequest("not json", roomId, userId, allowRejoin));

    //1725 事件 10：actor 为发起人，target 为被移出的人
    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_removed, 5127, room, 5308),
                             root));
    CHECK(root["event"].asInt() == 10);
    CHECK(root["actor"].asString() == "5127");
    CHECK(root["target"].asString() == "5308");

    //1729 结束原因 3 / 4
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildEndedNotify("ss-1", 2, 600001, 5127, 65, screenshare_end_removed),
                             root));
    CHECK(root["reason"].asInt() == 3);
    REQUIRE(scpParseJsonText(
        ScreenShareProtocol::buildEndedNotify("ss-1", 2, 600001, 5127, 65, screenshare_end_removed_banned), root));
    CHECK(root["reason"].asInt() == 4);

    CHECK(cmd_screenshare_remove_member == 1732);
    CHECK(error_code_screenshare_removed == 719);
    CHECK(screenshare_event_member_removed == 10);
}

TEST_CASE("SSTI1 共享标题与描述：1720 的解析，1721 / 1725 / 1727 / 卡片带 title 与 desc，720 取值")
{
    ScreenShareStartRequest req;
    REQUIRE(ScreenShareProtocol::parseStartRequest(
        u8"{\"ctype\":2,\"cid\":\"600001\",\"title\":\"周会演示\",\"desc\":\"第三季度汇报\"}", req));
    CHECK(req.m_title == u8"周会演示");
    CHECK(req.m_desc == u8"第三季度汇报");
    //没带时为空串：是否必填由 ScreenShareRules::checkShareInfo 判断
    REQUIRE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\"}", req));
    CHECK(req.m_title.empty());
    CHECK(req.m_desc.empty());
    //出现了却不是字符串：整个请求非法
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\",\"title\":1}", req));
    CHECK_FALSE(ScreenShareProtocol::parseStartRequest("{\"ctype\":1,\"cid\":\"5308\",\"title\":\"t\",\"desc\":[]}",
                                                       req));

    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStartedNotify("ss-1", 2, 600001, 5127, u8"张勇", 1790943882000LL,
                                                                     false, false, false, u8"周会演示", u8"第三季度汇报"),
                             root));
    CHECK(root["title"].asString() == u8"周会演示");
    CHECK(root["desc"].asString() == u8"第三季度汇报");

    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    room.m_title = u8"周会演示";
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_member_joined, 5308, room), root));
    CHECK(root["title"].asString() == u8"周会演示");
    //没填描述时字段照样带上，为空串
    CHECK(root.isMember("desc"));
    CHECK(root["desc"].asString().empty());
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", &room), root));
    CHECK(root["title"].asString() == u8"周会演示");
    CHECK(root.isMember("desc"));
    //没有进行中的共享时不带
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildQueryResponse(0, "ok", NULL), root));
    CHECK_FALSE(root.isMember("title"));
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildCardContent("ss-1", 5127, u8"张勇", 1790943882000LL, false, false,
                                                                   u8"周会演示", u8"第三季度汇报"),
                             root));
    CHECK(root["title"].asString() == u8"周会演示");
    CHECK(root["desc"].asString() == u8"第三季度汇报");

    CHECK(error_code_screenshare_sensitive == 720);
}

TEST_CASE("SSTI3 修改共享标题与描述：1733 的解析，1734 的字段，卡片没写成功时不带 msgid 与 content")
{
    std::string roomId;
    std::string title;
    std::string desc;
    REQUIRE(ScreenShareProtocol::parseSetInfoRequest(u8"{\"roomid\":\"ss-1\",\"title\":\"周会演示\",\"desc\":\"汇报\"}",
                                                     roomId, title, desc));
    CHECK(roomId == "ss-1");
    CHECK(title == u8"周会演示");
    CHECK(desc == u8"汇报");
    //没带描述：为空串
    REQUIRE(ScreenShareProtocol::parseSetInfoRequest("{\"roomid\":\"ss-1\",\"title\":\"t\"}", roomId, title, desc));
    CHECK(desc.empty());
    //缺 title、title 不是字符串、desc 不是字符串、缺 roomid：非法
    CHECK_FALSE(ScreenShareProtocol::parseSetInfoRequest("{\"roomid\":\"ss-1\"}", roomId, title, desc));
    CHECK_FALSE(ScreenShareProtocol::parseSetInfoRequest("{\"roomid\":\"ss-1\",\"title\":5}", roomId, title, desc));
    CHECK_FALSE(ScreenShareProtocol::parseSetInfoRequest("{\"roomid\":\"ss-1\",\"title\":\"t\",\"desc\":1}", roomId,
                                                         title, desc));
    CHECK_FALSE(ScreenShareProtocol::parseSetInfoRequest("{\"title\":\"t\"}", roomId, title, desc));

    //1734：带改写后的卡片内容与卡片 msgid（字符串编码）
    const std::string card = ScreenShareProtocol::buildCardContent("ss-1", 5127, u8"张勇", 1790943882000LL, false, false,
                                                                   u8"新标题", "");
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildInfoNotify("ss-1", 2, 600001, u8"新标题", "", 9001, card), root));
    CHECK(root["roomid"].asString() == "ss-1");
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "600001");
    CHECK(root["title"].asString() == u8"新标题");
    CHECK(root.isMember("desc"));
    CHECK(root["msgid"].asString() == "9001");
    CHECK(root["content"].asString() == card);
    //卡片没写成功：不带 msgid 与 content
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildInfoNotify("ss-1", 2, 600001, u8"新标题", "", 0, card), root));
    CHECK_FALSE(root.isMember("msgid"));
    CHECK_FALSE(root.isMember("content"));

    CHECK(cmd_screenshare_set_info == 1733);
    CHECK(cmd_screenshare_info_notify == 1734);
}

TEST_CASE("SSTI5 移交主持人后的 1734：带 hostid（字符串编码）与 hostname、不带卡片；主持人为 0 时与原格式完全相同")
{
    //2026-10-08：移交后经 1734 告诉不在房间里的会话成员新主持人是谁
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildInfoNotify("ss-1", 2, 600001, u8"张勇的屏幕共享", "", 0, "",
                                                                  5308, u8"韩晓燕888"),
                             root));
    CHECK(root["roomid"].asString() == "ss-1");
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "600001");
    CHECK(root["title"].asString() == u8"张勇的屏幕共享");
    CHECK(root["hostid"].isString());
    CHECK(root["hostid"].asString() == "5308");
    CHECK(root["hostname"].asString() == u8"韩晓燕888");
    CHECK_FALSE(root.isMember("msgid"));
    CHECK_FALSE(root.isMember("content"));

    //主持人为 0（修改标题与描述的那种 1734）：不带 hostid 与 hostname，与不传这两个参数的结果逐字相同
    const std::string card = ScreenShareProtocol::buildCardContent("ss-1", 5127, u8"张勇", 1790943882000LL, false, false,
                                                                   u8"新标题", "");
    const std::string plain = ScreenShareProtocol::buildInfoNotify("ss-1", 2, 600001, u8"新标题", "", 9001, card);
    CHECK(ScreenShareProtocol::buildInfoNotify("ss-1", 2, 600001, u8"新标题", "", 9001, card, 0, u8"无关") == plain);
    REQUIRE(scpParseJsonText(plain, root));
    CHECK_FALSE(root.isMember("hostid"));
    CHECK_FALSE(root.isMember("hostname"));
}

TEST_CASE("SSCT4 控制键鼠：1735 的解析，1736 的字段，1725 事件 11 / 12 与成员的 controlledby，729 / 730 取值")
{
    std::string roomId;
    int action = 0;
    int64_t owner = 0;
    int64_t target = 0;
    //授予：带 target
    REQUIRE(ScreenShareProtocol::parseControlRequest(
        "{\"roomid\":\"ss-1\",\"action\":3,\"owner\":\"5127\",\"target\":\"5308\"}", roomId, action, owner, target));
    CHECK(roomId == "ss-1");
    CHECK(action == screenshare_control_grant);
    CHECK(owner == 5127);
    CHECK(target == 5308);
    //申请、撤回、收回不需要 target，没带为 0
    REQUIRE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":1,\"owner\":\"5127\"}", roomId,
                                                     action, owner, target));
    CHECK(action == screenshare_control_request);
    CHECK(target == 0);
    REQUIRE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":5,\"owner\":\"5127\"}", roomId,
                                                     action, owner, target));
    CHECK(action == screenshare_control_revoke);
    //非法：动作越界、缺 owner、授予与拒绝缺 target、缺 roomid、不是 JSON
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":0,\"owner\":\"5127\"}",
                                                         roomId, action, owner, target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":6,\"owner\":\"5127\"}",
                                                         roomId, action, owner, target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":1}", roomId, action, owner,
                                                         target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":3,\"owner\":\"5127\"}",
                                                         roomId, action, owner, target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"roomid\":\"ss-1\",\"action\":4,\"owner\":\"5127\"}",
                                                         roomId, action, owner, target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("{\"action\":1,\"owner\":\"5127\"}", roomId, action, owner,
                                                         target));
    CHECK_FALSE(ScreenShareProtocol::parseControlRequest("not json", roomId, action, owner, target));

    //1736：动作、画面主人、发起动作的人与其昵称，id 为字符串
    Json::Value root;
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildControlNotify("ss-1", screenshare_control_request, 5127, 5308,
                                                                     kZhangSanUtf8),
                             root));
    CHECK(root["roomid"].asString() == "ss-1");
    CHECK(root["action"].asInt() == 1);
    CHECK(root["owner"].asString() == "5127");
    CHECK(root["actor"].asString() == "5308");
    CHECK(root["actorname"].asString() == kZhangSanUtf8);

    //1725 事件 11：画面主人的成员项带 controlledby，没人控制的成员不带
    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    ScreenShareMember host;
    host.m_userId = 5127;
    host.m_seat = 0;
    host.m_controlledBy = 5308;
    ScreenShareMember viewer;
    viewer.m_userId = 5308;
    viewer.m_seat = 1;
    room.m_members.push_back(host);
    room.m_members.push_back(viewer);
    REQUIRE(scpParseJsonText(ScreenShareProtocol::buildStateNotify(screenshare_event_control_granted, 5127, room, 5308),
                             root));
    CHECK(root["event"].asInt() == 11);
    CHECK(root["target"].asString() == "5308");
    REQUIRE(root["members"].size() == 2);
    CHECK(root["members"][0]["controlledby"].asString() == "5308");
    CHECK_FALSE(root["members"][1].isMember("controlledby"));

    CHECK(cmd_screenshare_control == 1735);
    CHECK(cmd_screenshare_control_notify == 1736);
    CHECK(error_code_screenshare_not_sharing == 729);
    CHECK(error_code_screenshare_not_owner == 730);
    CHECK(screenshare_event_control_granted == 11);
    CHECK(screenshare_event_control_revoked == 12);
    CHECK(screenshare_control_input == 4);
}
