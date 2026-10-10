/**
 *  v2 群通话信令编解码（客户端侧），GroupCallProtocolClient.cpp
 *  字段约定见 GroupCallProtocolClient.h 与 docs/协议设计-v2.md 第 21 节。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallProtocolClient.h"

#include <stdio.h>
#include <stdlib.h>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace groupcall {

namespace {

//群通话卡片 content 里 kind 字段的取值
const char* const kCardKindGroupCall = "groupcall";

//64 位整数转十进制字符串（JSON 里 64 位 id 一律以字符串编码）
std::string int64ToString(int64_t v)
{
    char buf[32] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", static_cast<long long>(v));
    return std::string(buf);
}

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析为 JSON 对象；不是合法 JSON 或不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    if (json.empty())
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//取 64 位整数：字符串形式（服务端的 id 编码）与数值形式都接受；缺失或非法时为 0
int64_t toInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(_strtoi64(v.asCString(), NULL, 10));
    if (v.isInt64())
        return static_cast<int64_t>(v.asInt64());
    if (v.isUInt64())
        return static_cast<int64_t>(v.asUInt64());
    return 0;
}

//取整数；缺失或类型不对时返回 defaultValue
int toInt(const Json::Value& v, int defaultValue)
{
    return v.isInt() ? v.asInt() : defaultValue;
}

//取 0 / 1 开关；缺失时返回 defaultValue
bool toFlag(const Json::Value& v, bool defaultValue)
{
    if (v.isInt())
        return v.asInt() != 0;
    if (v.isBool())
        return v.asBool();
    return defaultValue;
}

std::string toStr(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

//id 列表转字符串数组
Json::Value idsToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(int64ToString(ids[i]));
    return arr;
}

//字符串数组转 id 列表；不是数组时返回空
std::vector<int64_t> idsFromJson(const Json::Value& arr)
{
    std::vector<int64_t> ids;
    if (!arr.isArray())
        return ids;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = toInt64(arr[i]);
        if (id > 0)
            ids.push_back(id);
    }
    return ids;
}

/**
 *  解析 1741 / 1745 共有的房间字段与成员数组。
 *  @return roomid 非空、groupid > 0 且 members 是数组时返回 true
 */
bool parseRoomBasics(const Json::Value& root, GroupCallState& out)
{
    out = GroupCallState();
    out.m_roomId = toStr(root["roomid"]);
    out.m_groupId = toInt64(root["groupid"]);
    if (out.m_roomId.empty() || out.m_groupId <= 0 || !root["members"].isArray())
        return false;
    out.m_media = toInt(root["media"], gc_media_voice);
    out.m_initiatorId = toInt64(root["initiator"]);
    out.m_startTime = toInt64(root["starttime"]);
    out.m_allMuted = (toInt(root["allmuted"], 0) != 0);
    const Json::Value& members = root["members"];
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        const Json::Value& m = members[i];
        GroupCallMember member;
        member.m_userId = toInt64(m["uid"]);
        if (member.m_userId <= 0)
            continue;
        member.m_seat = toInt(m["seat"], 0);
        member.m_state = toInt(m["state"], gc_member_ringing);
        member.m_mic = toFlag(m["mic"], false);
        member.m_camera = toFlag(m["camera"], false);
        member.m_online = toFlag(m["online"], true);
        out.m_members.push_back(member);
    }
    return true;
}

} // namespace

GroupCallMember::GroupCallMember()
    : m_userId(0)
    , m_seat(0)
    , m_state(gc_member_ringing)
    , m_mic(false)
    , m_camera(false)
    , m_online(true)
{
}

GroupCallState::GroupCallState()
    : m_groupId(0)
    , m_media(gc_media_voice)
    , m_initiatorId(0)
    , m_startTime(0)
    , m_event(0)
    , m_actor(0)
    , m_target(0)
    , m_allMuted(false)
{
}

const GroupCallMember* GroupCallState::findMember(int64_t userId) const
{
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_userId == userId)
            return &m_members[i];
    }
    return NULL;
}

int GroupCallState::joinedCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_state == gc_member_joined)
            ++count;
    }
    return count;
}

GroupCallInvite::GroupCallInvite()
    : m_inviterId(0)
    , m_ringMs(0)
{
}

GroupCallResult::GroupCallResult()
    : m_code(-1)
{
}

GroupCallRelayInfo::GroupCallRelayInfo()
    : m_relayPort(0)
    , m_seat(0)
    , m_maxCameras(0)
    , m_elapsedMs(0)
{
}

GroupCallQueryResult::GroupCallQueryResult()
    : m_code(-1)
    , m_exists(false)
    , m_media(gc_media_voice)
    , m_initiatorId(0)
    , m_startTime(0)
    , m_count(0)
{
}

GroupCallStatus::GroupCallStatus()
    : m_groupId(0)
    , m_active(false)
    , m_media(gc_media_voice)
    , m_initiatorId(0)
    , m_startTime(0)
    , m_count(0)
    , m_durationSeconds(0)
    , m_cardMsgId(0)
{
}

GroupCallCard::GroupCallCard()
    : m_media(gc_media_voice)
    , m_initiatorId(0)
    , m_startTime(0)
    , m_ended(false)
    , m_durationSeconds(0)
{
}

std::string GroupCallProtocolClient::buildStart(int64_t groupId, int media, const std::vector<int64_t>& invitees,
                                                bool mic, bool camera)
{
    Json::Value root;
    root["groupid"] = int64ToString(groupId);
    root["media"] = media;
    root["invitees"] = idsToJson(invitees);
    root["mic"] = mic ? 1 : 0;
    root["camera"] = camera ? 1 : 0;
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildJoin(const std::string& roomId, bool mic, bool camera)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["mic"] = mic ? 1 : 0;
    root["camera"] = camera ? 1 : 0;
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildRoomRequest(const std::string& roomId)
{
    Json::Value root;
    root["roomid"] = roomId;
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildInviteMore(const std::string& roomId, const std::vector<int64_t>& invitees)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["invitees"] = idsToJson(invitees);
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildMemberMedia(const std::string& roomId, bool mic, bool camera)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["mic"] = mic ? 1 : 0;
    root["camera"] = camera ? 1 : 0;
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildQuery(int64_t groupId)
{
    Json::Value root;
    root["groupid"] = int64ToString(groupId);
    return writeCompact(root);
}

std::string GroupCallProtocolClient::buildManage(const std::string& roomId, int action, int64_t target)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    if (target > 0)
        root["target"] = int64ToString(target);
    return writeCompact(root);
}

bool GroupCallProtocolClient::parseResult(const std::string& json, GroupCallResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out = GroupCallResult();
    out.m_code = root["code"].asInt();
    out.m_msg = toStr(root["msg"]);
    out.m_roomId = toStr(root["roomid"]);
    out.m_busy = idsFromJson(root["busy"]);
    out.m_offline = idsFromJson(root["offline"]);
    return true;
}

bool GroupCallProtocolClient::parseInvite(const std::string& json, GroupCallInvite& out)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    out = GroupCallInvite();
    if (!parseRoomBasics(root, out.m_room))
        return false;
    out.m_groupName = toStr(root["groupname"]);
    out.m_inviterId = toInt64(root["inviter"]);
    out.m_inviterName = toStr(root["invitername"]);
    out.m_ringMs = toInt64(root["ringms"]);
    return true;
}

bool GroupCallProtocolClient::parseStateNotify(const std::string& json, GroupCallState& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["event"].isInt())
        return false;
    if (!parseRoomBasics(root, out))
        return false;
    out.m_event = root["event"].asInt();
    out.m_actor = toInt64(root["actor"]);
    out.m_target = toInt64(root["target"]);
    return true;
}

bool GroupCallProtocolClient::parseRelayInfo(const std::string& json, GroupCallRelayInfo& out)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    out = GroupCallRelayInfo();
    out.m_roomId = toStr(root["roomid"]);
    out.m_token = toStr(root["token"]);
    out.m_relayIp = toStr(root["relayip"]);
    out.m_relayPort = toInt(root["relayport"], 0);
    if (out.m_roomId.empty() || out.m_token.empty() || out.m_relayPort <= 0)
        return false;
    out.m_seat = toInt(root["seat"], 0);
    out.m_maxCameras = toInt(root["maxcameras"], 0);
    out.m_elapsedMs = toInt64(root["elapsedms"]);
    return true;
}

bool GroupCallProtocolClient::parseQueryResult(const std::string& json, GroupCallQueryResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out = GroupCallQueryResult();
    out.m_code = root["code"].asInt();
    out.m_exists = toFlag(root["exists"], false);
    if (!out.m_exists)
        return true;
    out.m_roomId = toStr(root["roomid"]);
    out.m_media = toInt(root["media"], gc_media_voice);
    out.m_initiatorId = toInt64(root["initiator"]);
    out.m_startTime = toInt64(root["starttime"]);
    out.m_count = toInt(root["count"], 0);
    out.m_members = idsFromJson(root["members"]);
    //说是有、却没有房间号：按没有处理
    if (out.m_roomId.empty())
        out.m_exists = false;
    return true;
}

bool GroupCallProtocolClient::parseStatus(const std::string& json, GroupCallStatus& out)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    out = GroupCallStatus();
    out.m_roomId = toStr(root["roomid"]);
    out.m_groupId = toInt64(root["groupid"]);
    if (out.m_roomId.empty() || out.m_groupId <= 0 || !root["active"].isInt())
        return false;
    out.m_active = root["active"].asInt() != 0;
    out.m_media = toInt(root["media"], gc_media_voice);
    out.m_initiatorId = toInt64(root["initiator"]);
    out.m_startTime = toInt64(root["starttime"]);
    out.m_count = toInt(root["count"], 0);
    out.m_durationSeconds = toInt64(root["duration"]);
    out.m_cardMsgId = toInt64(root["msgid"]);
    out.m_cardContent = toStr(root["content"]);
    if (out.m_cardContent.empty())
        out.m_cardMsgId = 0;
    return true;
}

bool GroupCallProtocolClient::parseCard(const std::string& content, GroupCallCard& out)
{
    Json::Value root;
    if (!parseObject(content, root) || toStr(root["kind"]) != kCardKindGroupCall)
        return false;
    out = GroupCallCard();
    out.m_roomId = toStr(root["roomid"]);
    if (out.m_roomId.empty())
        return false;
    out.m_media = toInt(root["media"], gc_media_voice);
    out.m_initiatorId = toInt64(root["initiator"]);
    out.m_initiatorName = toStr(root["initiatorname"]);
    out.m_startTime = toInt64(root["starttime"]);
    out.m_ended = root.isMember("duration");
    out.m_durationSeconds = toInt64(root["duration"]);
    return true;
}

} // namespace groupcall
