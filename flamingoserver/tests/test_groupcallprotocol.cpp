/**
 *  群通话信令编解码单元测试（Suite GCP），test_groupcallprotocol.cpp
 *
 *  覆盖 GroupCallProtocol（docs/协议设计-v2.md 第 21 节）：
 *    GCP1 发起请求：必填字段、invitees 的格式与上限、mic / camera 缺省值（群视频默认开摄像头）、非法输入
 *    GCP2 加入、拒绝 / 离开、追加邀请、成员媒体、查询、管理请求的解析与非法输入
 *    GCP3 通用应答与带 busy / offline 名单的应答
 *    GCP4 邀请推送、房间状态推送（target 只在给出时出现、成员项各字段）、中转参数
 *    GCP5 查询应答（有 / 没有）、群内状态推送（进行中 / 结束带卡片）
 *    GCP6 通话卡片 content、结束改写（原 content 不可用时重建）、cardMedia
 *
 *  典型用法：Tests.exe --test-case="GCP*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/GroupCallProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 JSON 文本解析为对象；失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    REQUIRE(root.isObject());
    return root;
}

//造一个房间快照：kInitiator 已加入（1 号座位、开麦克风），kRinging 振铃中，kAway 已加入但掉线
GroupCallRoom makeRoom()
{
    GroupCallRoom room;
    room.m_roomId = "gc-600001-1790943882000-3";
    room.m_groupId = 600001;
    room.m_initiatorId = 5127;
    room.m_initiatorName = "Alice";
    room.m_media = groupcall_media_video;
    room.m_startTime = 1790943882000LL;
    GroupCallMember a;
    a.m_userId = 5127;
    a.m_state = groupcall_member_joined;
    a.m_seat = 1;
    a.m_mic = true;
    a.m_camera = true;
    room.m_members.push_back(a);
    GroupCallMember c;
    c.m_userId = 5309;
    c.m_state = groupcall_member_joined;
    c.m_seat = 2;
    c.m_offlineSince = 1790943890000LL;
    room.m_members.push_back(c);
    GroupCallMember b;
    b.m_userId = 5308;
    b.m_state = groupcall_member_ringing;
    room.m_members.push_back(b);
    return room;
}

} // namespace

TEST_CASE("GCP1 发起请求的解析：必填字段、被邀请人、缺省值与非法输入")
{
    GroupCallStartRequest req;
    REQUIRE(GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":2,\"invitees\":[\"5308\",\"5309\"]}",
                                                 req));
    CHECK(req.m_groupId == 600001);
    CHECK(req.m_media == groupcall_media_video);
    REQUIRE(req.m_invitees.size() == 2);
    CHECK(req.m_invitees[0] == 5308);
    CHECK(req.m_mic);
    //群视频默认开摄像头
    CHECK(req.m_camera);

    //群语音默认不开摄像头；没带 invitees 时为空；mic / camera 明确给出时按给出的值
    REQUIRE(GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":1}", req));
    CHECK(req.m_media == groupcall_media_voice);
    CHECK(req.m_invitees.empty());
    CHECK(!req.m_camera);
    REQUIRE(GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":2,\"mic\":0,\"camera\":0}", req));
    CHECK(!req.m_mic);
    CHECK(!req.m_camera);

    //非法：不是 JSON、缺 groupid、groupid 不是字符串、media 越界或缺失、invitees 不是数组或项不是字符串 id、开关不是整数
    CHECK(!GroupCallProtocol::parseStartRequest("not json", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"media\":1}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":600001,\"media\":1}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":3}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\"}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":1,\"invitees\":\"5308\"}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":1,\"invitees\":[5308]}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":1,\"invitees\":[\"0\"]}", req));
    CHECK(!GroupCallProtocol::parseStartRequest("{\"groupid\":\"600001\",\"media\":1,\"mic\":\"1\"}", req));

    //被邀请人超过一次请求的上限
    std::string many = "{\"groupid\":\"600001\",\"media\":1,\"invitees\":[";
    for (size_t i = 0; i <= GroupCallProtocol::kMaxInviteesPerRequest; ++i)
    {
        if (i > 0)
            many += ",";
        many += "\"" + std::to_string(10000 + i) + "\"";
    }
    many += "]}";
    CHECK(!GroupCallProtocol::parseStartRequest(many, req));
}

TEST_CASE("GCP2 加入、拒绝 / 离开、追加邀请、成员媒体、查询、管理请求的解析")
{
    std::string roomId;
    bool mic = false;
    bool camera = true;
    REQUIRE(GroupCallProtocol::parseJoinRequest("{\"roomid\":\"gc-1\"}", roomId, mic, camera));
    CHECK(roomId == "gc-1");
    CHECK(mic);
    CHECK(!camera);
    REQUIRE(GroupCallProtocol::parseJoinRequest("{\"roomid\":\"gc-1\",\"mic\":0,\"camera\":1}", roomId, mic, camera));
    CHECK(!mic);
    CHECK(camera);
    CHECK(!GroupCallProtocol::parseJoinRequest("{\"roomid\":\"\"}", roomId, mic, camera));
    CHECK(!GroupCallProtocol::parseJoinRequest("{\"roomid\":\"gc-1\",\"camera\":true}", roomId, mic, camera));

    REQUIRE(GroupCallProtocol::parseRoomRequest("{\"roomid\":\"gc-2\"}", roomId));
    CHECK(roomId == "gc-2");
    CHECK(!GroupCallProtocol::parseRoomRequest("{\"roomid\":2}", roomId));
    CHECK(!GroupCallProtocol::parseRoomRequest("[]", roomId));

    std::vector<int64_t> invitees;
    REQUIRE(GroupCallProtocol::parseInviteMoreRequest("{\"roomid\":\"gc-3\",\"invitees\":[\"5311\"]}", roomId,
                                                      invitees));
    CHECK(roomId == "gc-3");
    REQUIRE(invitees.size() == 1);
    CHECK(invitees[0] == 5311);
    //追加邀请必须至少一人
    CHECK(!GroupCallProtocol::parseInviteMoreRequest("{\"roomid\":\"gc-3\",\"invitees\":[]}", roomId, invitees));
    CHECK(!GroupCallProtocol::parseInviteMoreRequest("{\"roomid\":\"gc-3\"}", roomId, invitees));

    REQUIRE(GroupCallProtocol::parseMemberMediaRequest("{\"roomid\":\"gc-4\",\"mic\":1,\"camera\":0}", roomId, mic,
                                                       camera));
    CHECK(mic);
    CHECK(!camera);
    //两项都必须给出
    CHECK(!GroupCallProtocol::parseMemberMediaRequest("{\"roomid\":\"gc-4\",\"mic\":1}", roomId, mic, camera));

    int64_t groupId = 0;
    REQUIRE(GroupCallProtocol::parseQueryRequest("{\"groupid\":\"600001\"}", groupId));
    CHECK(groupId == 600001);
    CHECK(!GroupCallProtocol::parseQueryRequest("{\"groupid\":\"0\"}", groupId));

    int action = 0;
    int64_t target = 0;
    REQUIRE(GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":1,\"target\":\"5308\"}", roomId,
                                                  action, target));
    CHECK(action == groupcall_manage_mute);
    CHECK(target == 5308);
    REQUIRE(GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":2,\"target\":\"5308\"}", roomId,
                                                  action, target));
    CHECK(action == groupcall_manage_mute_all);
    CHECK(target == 0);
    REQUIRE(GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":3,\"target\":\"5309\"}", roomId,
                                                  action, target));
    CHECK(action == groupcall_manage_remove);
    //解除全员静音：不需要对象，带了也忽略
    REQUIRE(GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":4,\"target\":\"5308\"}", roomId,
                                                  action, target));
    CHECK(action == groupcall_manage_unmute_all);
    CHECK(target == 0);
    //静音某人、移出须带 target；未知操作非法
    CHECK(!GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":1}", roomId, action, target));
    CHECK(!GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":3}", roomId, action, target));
    CHECK(!GroupCallProtocol::parseManageRequest("{\"roomid\":\"gc-5\",\"action\":9,\"target\":\"1\"}", roomId, action,
                                                 target));
}

TEST_CASE("GCP3 通用应答与带忙线 / 离线名单的应答")
{
    Json::Value r = parseJson(GroupCallProtocol::buildResult(error_code_ok, "ok", "gc-1"));
    CHECK(r["code"].asInt() == 0);
    CHECK(r["msg"].asString() == "ok");
    CHECK(r["roomid"].asString() == "gc-1");
    r = parseJson(GroupCallProtocol::buildResult(error_code_groupcall_busy, "busy", ""));
    CHECK(r["code"].asInt() == error_code_groupcall_busy);
    CHECK(!r.isMember("roomid"));

    std::vector<int64_t> busy(1, 5309);
    std::vector<int64_t> offline;
    offline.push_back(5310);
    offline.push_back(5311);
    r = parseJson(GroupCallProtocol::buildInviteResult(error_code_ok, "ok", "gc-1", busy, offline));
    CHECK(r["roomid"].asString() == "gc-1");
    REQUIRE(r["busy"].isArray());
    REQUIRE(r["busy"].size() == 1);
    CHECK(r["busy"][0].asString() == "5309");
    REQUIRE(r["offline"].size() == 2);
    CHECK(r["offline"][1].asString() == "5311");
}

TEST_CASE("GCP4 邀请推送、房间状态推送与中转参数")
{
    const GroupCallRoom room = makeRoom();
    Json::Value v = parseJson(GroupCallProtocol::buildInviteNotify(room, 5127, "Alice", "产品部", 45000));
    CHECK(v["roomid"].asString() == room.m_roomId);
    CHECK(v["groupid"].asString() == "600001");
    CHECK(v["groupname"].asString() == "产品部");
    CHECK(v["inviter"].asString() == "5127");
    CHECK(v["invitername"].asString() == "Alice");
    CHECK(v["media"].asInt() == groupcall_media_video);
    CHECK(v["ringms"].asInt64() == 45000);
    CHECK(v["initiator"].asString() == "5127");
    CHECK(v["starttime"].asString() == "1790943882000");
    CHECK(v["allmuted"].asInt() == 0);
    REQUIRE(v["members"].size() == 3);
    //负的剩余毫秒数按 0
    v = parseJson(GroupCallProtocol::buildInviteNotify(room, 5127, "Alice", "", -5));
    CHECK(v["ringms"].asInt64() == 0);

    v = parseJson(GroupCallProtocol::buildStateNotify(groupcall_event_member_joined, 5309, room));
    CHECK(v["event"].asInt() == groupcall_event_member_joined);
    CHECK(v["actor"].asString() == "5309");
    CHECK(!v.isMember("target"));
    const Json::Value& members = v["members"];
    REQUIRE(members.size() == 3);
    CHECK(members[0]["uid"].asString() == "5127");
    CHECK(members[0]["seat"].asInt() == 1);
    CHECK(members[0]["state"].asInt() == groupcall_member_joined);
    CHECK(members[0]["mic"].asInt() == 1);
    CHECK(members[0]["camera"].asInt() == 1);
    CHECK(members[0]["online"].asInt() == 1);
    //掉线中的已加入成员 online 为 0
    CHECK(members[1]["online"].asInt() == 0);
    CHECK(members[2]["state"].asInt() == groupcall_member_ringing);
    CHECK(members[2]["seat"].asInt() == 0);
    v = parseJson(GroupCallProtocol::buildStateNotify(groupcall_event_member_removed, 5127, room, 5308));
    CHECK(v["target"].asString() == "5308");
    //全员静音状态随快照下发（2026-10-05）
    GroupCallRoom mutedRoom = room;
    mutedRoom.m_allMuted = true;
    v = parseJson(GroupCallProtocol::buildStateNotify(groupcall_event_muted_all, 5127, mutedRoom));
    CHECK(v["allmuted"].asInt() == 1);

    v = parseJson(GroupCallProtocol::buildRelayInfo("gc-1", "10.0.0.1", 20003, "tok", 2, 9, 36500));
    CHECK(v["roomid"].asString() == "gc-1");
    CHECK(v["relayip"].asString() == "10.0.0.1");
    CHECK(v["relayport"].asInt() == 20003);
    CHECK(v["token"].asString() == "tok");
    CHECK(v["seat"].asInt() == 2);
    CHECK(v["maxcameras"].asInt() == 9);
    CHECK(v["elapsedms"].asInt64() == 36500);
}

TEST_CASE("GCP5 查询应答与群内状态推送")
{
    const GroupCallRoom room = makeRoom();
    Json::Value v = parseJson(GroupCallProtocol::buildQueryResponse(error_code_ok, "ok", &room));
    CHECK(v["code"].asInt() == 0);
    CHECK(v["exists"].asInt() == 1);
    CHECK(v["roomid"].asString() == room.m_roomId);
    CHECK(v["media"].asInt() == groupcall_media_video);
    CHECK(v["initiator"].asString() == "5127");
    //已加入人数与名单不含振铃中的人
    CHECK(v["count"].asInt() == 2);
    REQUIRE(v["members"].size() == 2);
    CHECK(v["members"][1].asString() == "5309");

    v = parseJson(GroupCallProtocol::buildQueryResponse(error_code_ok, "ok", NULL));
    CHECK(v["exists"].asInt() == 0);
    CHECK(!v.isMember("roomid"));

    v = parseJson(GroupCallProtocol::buildStatusNotify(room, true, 0, 0, ""));
    CHECK(v["active"].asInt() == 1);
    CHECK(v["count"].asInt() == 2);
    CHECK(v["groupid"].asString() == "600001");
    CHECK(!v.isMember("duration"));
    CHECK(!v.isMember("msgid"));

    v = parseJson(GroupCallProtocol::buildStatusNotify(room, false, 754, 9102, "{\"kind\":\"groupcall\"}"));
    CHECK(v["active"].asInt() == 0);
    CHECK(v["count"].asInt() == 0);
    CHECK(v["duration"].asInt64() == 754);
    CHECK(v["msgid"].asString() == "9102");
    CHECK(v["content"].asString() == "{\"kind\":\"groupcall\"}");
    //卡片没改写成功时不带 msgid 与 content
    v = parseJson(GroupCallProtocol::buildStatusNotify(room, false, 754, 0, "{}"));
    CHECK(!v.isMember("msgid"));
    CHECK(!v.isMember("content"));
}

TEST_CASE("GCP6 通话卡片 content、结束改写与 cardMedia")
{
    const std::string card = GroupCallProtocol::buildCardContent("gc-1", groupcall_media_voice, 5127, "张勇",
                                                                 1790943882000LL);
    Json::Value v = parseJson(card);
    CHECK(v["kind"].asString() == "groupcall");
    CHECK(v["roomid"].asString() == "gc-1");
    CHECK(v["media"].asInt() == groupcall_media_voice);
    CHECK(v["initiator"].asString() == "5127");
    CHECK(v["initiatorname"].asString() == "张勇");
    CHECK(v["starttime"].asString() == "1790943882000");
    CHECK(!v.isMember("duration"));
    CHECK(GroupCallProtocol::cardMedia(card) == groupcall_media_voice);

    const GroupCallRoom room = makeRoom();
    v = parseJson(GroupCallProtocol::buildEndedCardContent(card, room, "Alice", 1790943900000LL, 18));
    //原 content 可用：保留原字段，加上结束时间与时长
    CHECK(v["initiatorname"].asString() == "张勇");
    CHECK(v["endtime"].asString() == "1790943900000");
    CHECK(v["duration"].asInt64() == 18);
    //原 content 不可用：按房间重建
    v = parseJson(GroupCallProtocol::buildEndedCardContent("garbage", room, "Alice", 1790943900000LL, -3));
    CHECK(v["kind"].asString() == "groupcall");
    CHECK(v["roomid"].asString() == room.m_roomId);
    CHECK(v["initiatorname"].asString() == "Alice");
    CHECK(v["media"].asInt() == groupcall_media_video);
    CHECK(v["duration"].asInt64() == 0);

    CHECK(GroupCallProtocol::cardMedia("{\"kind\":\"other\",\"media\":1}") == 0);
    CHECK(GroupCallProtocol::cardMedia("{\"kind\":\"groupcall\",\"media\":7}") == 0);
    CHECK(GroupCallProtocol::cardMedia("nope") == 0);
}
