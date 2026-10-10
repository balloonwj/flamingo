/**
 *  v2 群通话信令编解码（服务端侧）实现，GroupCallProtocol.cpp
 *  字段约定见 GroupCallProtocol.h 与 docs/协议设计-v2.md 第 21 节。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallProtocol.h"

#include <stdlib.h>                  // atoll
#include <sstream>
#include "../jsoncpp1.9.0/json.h"

const size_t GroupCallProtocol::kMaxInviteesPerRequest;

namespace {

//通话卡片 content 里 kind 字段的取值：标明这是一张群通话卡片（message_type_rtc 将来也可能承载一对一通话记录）
const char* kCardKindGroupCall = "groupcall";

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里字符串编码的 64 位 id 取出；字段缺失或不是字符串返回 0
int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
    {
        return 0;
    }
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

//64 位整数转十进制字符串（JSON 里的 id 与毫秒时间一律按字符串下发）
std::string int64ToString(int64_t v)
{
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

//取非空的字符串房间号；缺失、不是字符串或为空返回 false
bool getRoomId(const Json::Value& root, std::string& roomId)
{
    if (!root.isMember("roomid") || !root["roomid"].isString())
    {
        return false;
    }
    roomId = root["roomid"].asString();
    return !roomId.empty();
}

/**
 *  取可选的 0 / 1 开关字段。
 *  @param value [出参] 字段存在且是整数时回填，否则保持调用方给的缺省值
 *  @return 字段缺失或是整数返回 true；字段存在但不是整数返回 false
 */
bool getOptionalSwitch(const Json::Value& root, const char* key, bool& value)
{
    if (!root.isMember(key))
    {
        return true;
    }
    if (!root[key].isInt())
    {
        return false;
    }
    value = (root[key].asInt() != 0);
    return true;
}

/**
 *  取字符串 id 数组（被邀请人）。
 *  @return 是数组、不超过上限、每项都是 >0 的字符串 id 时返回 true
 */
bool getIdArray(const Json::Value& arr, std::vector<int64_t>& out)
{
    out.clear();
    if (!arr.isArray() || arr.size() > GroupCallProtocol::kMaxInviteesPerRequest)
    {
        return false;
    }
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isString())
        {
            return false;
        }
        const int64_t uid = static_cast<int64_t>(atoll(arr[i].asString().c_str()));
        if (uid <= 0)
        {
            return false;
        }
        out.push_back(uid);
    }
    return true;
}

//bool 转 JSON 里的 0 / 1
int toFlag(bool v)
{
    return v ? 1 : 0;
}

//id 列表转字符串数组
Json::Value idsToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        arr.append(int64ToString(ids[i]));
    }
    return arr;
}

//房间快照里的成员数组：[{uid, seat, state, mic, camera, online}, ...]
Json::Value membersToJson(const GroupCallRoom& room)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        const GroupCallMember& m = room.m_members[i];
        Json::Value jo;
        jo["uid"] = int64ToString(m.m_userId);
        jo["seat"] = m.m_seat;
        jo["state"] = m.m_state;
        jo["mic"] = toFlag(m.m_mic);
        jo["camera"] = toFlag(m.m_camera);
        jo["online"] = toFlag(m.m_offlineSince == 0);
        arr.append(jo);
    }
    return arr;
}

//几种推送共有的房间字段：roomid、groupid、media、initiator、starttime、allmuted
void fillRoomBasics(Json::Value& root, const GroupCallRoom& room)
{
    root["roomid"] = room.m_roomId;
    root["groupid"] = int64ToString(room.m_groupId);
    root["media"] = room.m_media;
    root["initiator"] = int64ToString(room.m_initiatorId);
    root["starttime"] = int64ToString(room.m_startTime);
    //全员静音状态：1 表示开启中，客户端据此给菜单打勾、给普通成员的麦克风按钮置灰
    root["allmuted"] = toFlag(room.m_allMuted);
}

//媒体类型取值是否合法
bool isValidMedia(int media)
{
    return media == groupcall_media_voice || media == groupcall_media_video;
}

} // namespace

GroupCallStartRequest::GroupCallStartRequest()
    : m_groupId(0)
    , m_media(groupcall_media_voice)
    , m_mic(true)
    , m_camera(false)
{
}

bool GroupCallProtocol::parseStartRequest(const std::string& json, GroupCallStartRequest& out)
{
    Json::Value root;
    if (!parseObject(json, root))
    {
        return false;
    }
    out = GroupCallStartRequest();
    out.m_groupId = getStringId(root, "groupid");
    if (out.m_groupId <= 0 || !root["media"].isInt())
    {
        return false;
    }
    out.m_media = root["media"].asInt();
    if (!isValidMedia(out.m_media))
    {
        return false;
    }
    if (root.isMember("invitees") && !getIdArray(root["invitees"], out.m_invitees))
    {
        return false;
    }
    //摄像头缺省按媒体类型：群视频默认打开
    out.m_camera = (out.m_media == groupcall_media_video);
    return getOptionalSwitch(root, "mic", out.m_mic) && getOptionalSwitch(root, "camera", out.m_camera);
}

bool GroupCallProtocol::parseJoinRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    mic = true;
    camera = false;
    return getOptionalSwitch(root, "mic", mic) && getOptionalSwitch(root, "camera", camera);
}

bool GroupCallProtocol::parseRoomRequest(const std::string& json, std::string& roomId)
{
    Json::Value root;
    return parseObject(json, root) && getRoomId(root, roomId);
}

bool GroupCallProtocol::parseInviteMoreRequest(const std::string& json, std::string& roomId,
                                               std::vector<int64_t>& invitees)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    return getIdArray(root["invitees"], invitees) && !invitees.empty();
}

bool GroupCallProtocol::parseMemberMediaRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    if (!root["mic"].isInt() || !root["camera"].isInt())
    {
        return false;
    }
    mic = (root["mic"].asInt() != 0);
    camera = (root["camera"].asInt() != 0);
    return true;
}

bool GroupCallProtocol::parseQueryRequest(const std::string& json, int64_t& groupId)
{
    Json::Value root;
    if (!parseObject(json, root))
    {
        return false;
    }
    groupId = getStringId(root, "groupid");
    return groupId > 0;
}

bool GroupCallProtocol::parseManageRequest(const std::string& json, std::string& roomId, int& action, int64_t& target)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId) || !root["action"].isInt())
    {
        return false;
    }
    action = root["action"].asInt();
    target = getStringId(root, "target");
    switch (action)
    {
    //静音某人、移出某人：须指明对象
    case groupcall_manage_mute:
    case groupcall_manage_remove:
        return target > 0;

    //开启 / 解除全员静音：不需要对象，带了也忽略
    case groupcall_manage_mute_all:
    case groupcall_manage_unmute_all:
        target = 0;
        return true;

    //未知操作
    default:
        return false;
    }
}

std::string GroupCallProtocol::buildResult(int code, const std::string& msg, const std::string& roomId)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!roomId.empty())
    {
        root["roomid"] = roomId;
    }
    return writeCompact(root);
}

std::string GroupCallProtocol::buildInviteResult(int code, const std::string& msg, const std::string& roomId,
                                                 const std::vector<int64_t>& busy, const std::vector<int64_t>& offline)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!roomId.empty())
    {
        root["roomid"] = roomId;
    }
    root["busy"] = idsToJson(busy);
    root["offline"] = idsToJson(offline);
    return writeCompact(root);
}

std::string GroupCallProtocol::buildInviteNotify(const GroupCallRoom& room, int64_t inviterId,
                                                 const std::string& inviterName, const std::string& groupName,
                                                 int64_t ringMs)
{
    Json::Value root;
    fillRoomBasics(root, room);
    root["groupname"] = groupName;
    root["inviter"] = int64ToString(inviterId);
    root["invitername"] = inviterName;
    root["ringms"] = static_cast<Json::Int64>(ringMs > 0 ? ringMs : 0);
    root["members"] = membersToJson(room);
    return writeCompact(root);
}

std::string GroupCallProtocol::buildStateNotify(int event, int64_t actor, const GroupCallRoom& room, int64_t target)
{
    Json::Value root;
    fillRoomBasics(root, room);
    root["event"] = event;
    root["actor"] = int64ToString(actor);
    if (target > 0)
    {
        root["target"] = int64ToString(target);
    }
    root["members"] = membersToJson(room);
    return writeCompact(root);
}

std::string GroupCallProtocol::buildRelayInfo(const std::string& roomId, const std::string& relayIp, int relayPort,
                                              const std::string& token, int seat, int maxCameras, int64_t elapsedMs)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["relayip"] = relayIp;
    root["relayport"] = relayPort;
    root["token"] = token;
    root["seat"] = seat;
    root["maxcameras"] = maxCameras;
    root["elapsedms"] = static_cast<Json::Int64>(elapsedMs > 0 ? elapsedMs : 0);
    return writeCompact(root);
}

std::string GroupCallProtocol::buildQueryResponse(int code, const std::string& msg, const GroupCallRoom* room)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["exists"] = toFlag(room != NULL);
    if (room != NULL)
    {
        root["roomid"] = room->m_roomId;
        root["media"] = room->m_media;
        root["initiator"] = int64ToString(room->m_initiatorId);
        root["starttime"] = int64ToString(room->m_startTime);
        root["count"] = room->joinedCount();
        root["members"] = idsToJson(room->joinedIds());
    }
    return writeCompact(root);
}

std::string GroupCallProtocol::buildStatusNotify(const GroupCallRoom& room, bool active, int64_t durationSeconds,
                                                 int64_t cardMsgId, const std::string& cardContent)
{
    Json::Value root;
    fillRoomBasics(root, room);
    root["active"] = toFlag(active);
    //结束时的快照可能还剩振铃中的人，已加入人数按 0 算
    root["count"] = active ? room.joinedCount() : 0;
    if (!active)
    {
        root["duration"] = static_cast<Json::Int64>(durationSeconds > 0 ? durationSeconds : 0);
        if (cardMsgId > 0)
        {
            root["msgid"] = int64ToString(cardMsgId);
            root["content"] = cardContent;
        }
    }
    return writeCompact(root);
}

std::string GroupCallProtocol::buildCardContent(const std::string& roomId, int media, int64_t initiatorId,
                                                const std::string& initiatorName, int64_t startTime)
{
    Json::Value root;
    root["kind"] = kCardKindGroupCall;
    root["roomid"] = roomId;
    root["media"] = media;
    root["initiator"] = int64ToString(initiatorId);
    root["initiatorname"] = initiatorName;
    root["starttime"] = int64ToString(startTime);
    return writeCompact(root);
}

std::string GroupCallProtocol::buildEndedCardContent(const std::string& oldContent, const GroupCallRoom& room,
                                                     const std::string& initiatorName, int64_t endTime,
                                                     int64_t durationSeconds)
{
    Json::Value root;
    //原 content 不可用（解析失败或不是群通话卡片）时按房间快照重建
    if (!parseObject(oldContent, root) || !root["kind"].isString() || root["kind"].asString() != kCardKindGroupCall)
    {
        Json::Value rebuilt;
        parseObject(buildCardContent(room.m_roomId, room.m_media, room.m_initiatorId, initiatorName, room.m_startTime),
                    rebuilt);
        root = rebuilt;
    }
    root["endtime"] = int64ToString(endTime);
    root["duration"] = static_cast<Json::Int64>(durationSeconds > 0 ? durationSeconds : 0);
    return writeCompact(root);
}

int GroupCallProtocol::cardMedia(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root) || !root["kind"].isString() || root["kind"].asString() != kCardKindGroupCall)
    {
        return 0;
    }
    if (!root["media"].isInt() || !isValidMedia(root["media"].asInt()))
    {
        return 0;
    }
    return root["media"].asInt();
}
