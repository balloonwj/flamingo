/**
 *  v2 屏幕共享信令编解码（客户端侧），ShareProtocolClient.cpp
 *  字段约定见 ShareProtocolClient.h 与 docs/协议设计-v2.md 第 17 节。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareProtocolClient.h"

#include <stdio.h>
#include <stdlib.h>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace screenshare {

namespace {

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

} // namespace

ShareMember::ShareMember()
    : m_userId(0)
    , m_seat(0)
    , m_mic(false)
    , m_camera(false)
    , m_systemAudio(false)
    , m_recording(false)
    , m_controlledBy(0)
    , m_sharing(false)
    , m_screenSeat(-1)
    , m_requesting(false)
    , m_boardRequesting(false)
    , m_viaLink(false)
{
}

ShareLinkResult::ShareLinkResult()
    : m_code(-1)
    , m_noPassword(false)
{
}

ShareLinkSummary::ShareLinkSummary()
    : m_code(-1)
    , m_status(0)
    , m_hostId(0)
    , m_locked(false)
    , m_forced(false)
    , m_member(false)
    , m_ctype(0)
    , m_cid(0)
{
}

ShareResult::ShareResult()
    : m_code(-1)
{
}

ShareStartedNotify::ShareStartedNotify()
    : m_ctype(0)
    , m_cid(0)
    , m_hostId(0)
    , m_startTime(0)
    , m_locked(false)
    , m_forced(false)
    , m_rejoin(false)
{
}

ShareWhiteboardInfo::ShareWhiteboardInfo()
    : m_seat(-1)
    , m_ownerId(0)
{
}

ShareStateNotify::ShareStateNotify()
    : m_event(0)
    , m_actor(0)
    , m_hostId(0)
    , m_hostOnline(true)
    , m_annotateAllowed(true)
    , m_recordAllowed(true)
    , m_forced(false)
    , m_target(0)
    , m_maxSharers(1)
    , m_cloudRecordId(0)
    , m_cloudRecordOwner(0)
    , m_cloudRecordPaused(false)
    , m_cloudRecordSince(0)
    , m_cloudRecordCode(0)
{
}

ShareCloudRecordResult::ShareCloudRecordResult()
    : m_code(0)
    , m_action(0)
    , m_recordId(0)
{
}

const ShareMember* ShareStateNotify::findMember(int64_t userId) const
{
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_userId == userId)
            return &m_members[i];
    }
    return NULL;
}

ShareRelayInfo::ShareRelayInfo()
    : m_relayPort(0)
    , m_seat(-1)
    , m_maxCameras(0)
    , m_elapsedMs(0)
{
}

ShareQueryResult::ShareQueryResult()
    : m_code(-1)
    , m_exists(false)
    , m_hostId(0)
    , m_memberCount(0)
    , m_startTime(0)
    , m_locked(false)
    , m_forced(false)
{
}

ShareEndedNotify::ShareEndedNotify()
    : m_ctype(0)
    , m_cid(0)
    , m_hostId(0)
    , m_durationSeconds(0)
    , m_reason(0)
{
}

ShareInfoNotify::ShareInfoNotify()
    : m_ctype(0)
    , m_cid(0)
    , m_cardMsgId(0)
{
}

ShareControlNotify::ShareControlNotify()
    : m_action(0)
    , m_owner(0)
    , m_actor(0)
{
}

ShareSharingNotify::ShareSharingNotify()
    : m_action(0)
    , m_actor(0)
    , m_relayPort(0)
    , m_screenSeat(-1)
    , m_kind(share_kind_screen)
{
}

bool ShareSharingNotify::HasPublishInfo() const
{
    return !m_token.empty() && m_relayPort > 0 && m_screenSeat >= 0;
}

std::string ShareProtocolClient::buildSharing(const std::string& roomId, int action, int64_t target, int kind)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    //白板（第 23 期）带 kind；屏幕不带，与第 22 期的请求相同
    if (kind != share_kind_screen)
        root["kind"] = kind;
    //同意、拒绝与主持人停别人的共享才有对象；其余动作不带 target
    if (target > 0)
        root["target"] = int64ToString(target);
    return writeCompact(root);
}

std::string ShareProtocolClient::buildTransferHost(const std::string& roomId, int64_t target)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["target"] = int64ToString(target);
    return writeCompact(root);
}

bool ShareProtocolClient::parseSharingNotify(const std::string& json, ShareSharingNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString() || !root["action"].isInt())
        return false;
    const int action = root["action"].asInt();
    if (action < share_sharing_request || action > share_sharing_stop)
        return false;
    out = ShareSharingNotify();
    out.m_roomId = root["roomid"].asString();
    out.m_action = action;
    out.m_actor = toInt64(root["actor"]);
    out.m_actorName = toStr(root["actorname"]);
    //同意时才有中转参数
    out.m_token = toStr(root["token"]);
    out.m_relayIp = toStr(root["relayip"]);
    out.m_relayPort = toInt(root["relayport"], 0);
    out.m_screenSeat = toInt(root["screenseat"], -1);
    //共享的是什么（第 23 期）；不带或不认识时按屏幕
    out.m_kind = (toInt(root["kind"], share_kind_screen) == share_kind_whiteboard) ? share_kind_whiteboard : share_kind_screen;
    return true;
}

std::string ShareProtocolClient::buildStart(int ctype, int64_t cid, bool mic, bool camera, bool sysAudio,
                                            const std::string& password, bool forced, bool recordAllowed,
                                            const std::string& title, const std::string& desc)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"] = int64ToString(cid);
    root["mic"] = mic ? 1 : 0;
    root["camera"] = camera ? 1 : 0;
    root["sysaudio"] = sysAudio ? 1 : 0;
    //没设密码时不带该字段，与第 12 期之前的请求完全一样
    if (!password.empty())
        root["password"] = password;
    //强制模式与禁止观看者录制（第 13 期）：只在与缺省不同时带上，普通共享的请求与之前完全一样
    if (forced)
        root["forced"] = 1;
    if (!recordAllowed)
        root["recordallowed"] = 0;
    //共享标题与描述（第 15 期）：标题必填，两个字段总是带上
    root["title"] = title;
    root["desc"] = desc;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildRoomRequest(const std::string& roomId)
{
    Json::Value root;
    root["roomid"] = roomId;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildJoin(const std::string& roomId, const std::string& password)
{
    Json::Value root;
    root["roomid"] = roomId;
    if (!password.empty())
        root["password"] = password;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildJoin(const std::string& roomId, const std::string& password,
                                           const std::string& linkCode)
{
    Json::Value root;
    root["roomid"] = roomId;
    if (!password.empty())
        root["password"] = password;
    //凭分享链接加入（第 25 期）
    if (!linkCode.empty())
        root["linkcode"] = linkCode;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildLink(const std::string& roomId, int action, bool noPassword)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    if (action == share_link_set_nopassword)
        root["nopassword"] = noPassword ? 1 : 0;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildLinkResolve(const std::string& linkCode)
{
    Json::Value root;
    root["linkcode"] = linkCode;
    return writeCompact(root);
}

bool ShareProtocolClient::parseLinkResult(const std::string& json, ShareLinkResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_roomId = toStr(root["roomid"]);
    out.m_linkCode = toStr(root["linkcode"]);
    out.m_noPassword = toFlag(root["nopassword"], false);
    return true;
}

std::string ShareProtocolClient::buildCloudRecordControl(const std::string& roomId, int action)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    return writeCompact(root);
}

bool ShareProtocolClient::parseCloudRecordResult(const std::string& json, ShareCloudRecordResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_roomId = toStr(root["roomid"]);
    out.m_action = toInt(root["action"], 0);
    out.m_recordId = toInt64(root["recordid"]);
    return true;
}

bool ShareProtocolClient::parseLinkSummary(const std::string& json, ShareLinkSummary& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_status = toInt(root["status"], share_link_status_invalid);
    out.m_roomId = toStr(root["roomid"]);
    out.m_title = toStr(root["title"]);
    out.m_desc = toStr(root["desc"]);
    out.m_hostId = toInt64(root["host"]);
    out.m_hostName = toStr(root["hostname"]);
    out.m_locked = toFlag(root["locked"], false);
    out.m_forced = toFlag(root["forced"], false);
    out.m_member = toFlag(root["member"], false);
    out.m_ctype = toInt(root["ctype"], 0);
    out.m_cid = toInt64(root["cid"]);
    return true;
}

std::string ShareProtocolClient::buildQuery(int ctype, int64_t cid)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"] = int64ToString(cid);
    return writeCompact(root);
}

std::string ShareProtocolClient::buildMemberMedia(const std::string& roomId, bool mic, bool camera, bool sysAudio)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["mic"] = mic ? 1 : 0;
    root["camera"] = camera ? 1 : 0;
    root["sysaudio"] = sysAudio ? 1 : 0;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildSetPermission(const std::string& roomId, int annotate, int record, bool muteAll,
                                                    int64_t muteUser)
{
    Json::Value root;
    root["roomid"] = roomId;
    //「不变」的项不出现在包体里，服务端据此只改给出的项
    if (annotate != share_perm_unchanged)
        root["annotate"] = (annotate == share_perm_allow) ? 1 : 0;
    if (record != share_perm_unchanged)
        root["record"] = (record == share_perm_allow) ? 1 : 0;
    if (muteAll)
        root["muteall"] = 1;
    //按人静音（第 13 期）：64 位 userid 按字符串编码
    if (muteUser > 0)
        root["muteuser"] = int64ToString(muteUser);
    return writeCompact(root);
}

std::string ShareProtocolClient::buildRecordState(const std::string& roomId, bool recording)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["recording"] = recording ? 1 : 0;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildRemoveMember(const std::string& roomId, int64_t userId, bool allowRejoin)
{
    Json::Value root;
    root["roomid"] = roomId;
    //64 位 userid 按字符串编码
    root["userid"] = int64ToString(userId);
    root["allowrejoin"] = allowRejoin ? 1 : 0;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildSetInfo(const std::string& roomId, const std::string& title, const std::string& desc)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["title"] = title;
    root["desc"] = desc;
    return writeCompact(root);
}

std::string ShareProtocolClient::buildControl(const std::string& roomId, int action, int64_t owner, int64_t target)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    //64 位 userid 按字符串编码
    root["owner"] = int64ToString(owner);
    //授予、拒绝才有对象；其余动作不带 target
    if (target > 0)
        root["target"] = int64ToString(target);
    return writeCompact(root);
}

bool ShareProtocolClient::parseControlNotify(const std::string& json, ShareControlNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString())
        return false;
    out = ShareControlNotify();
    out.m_roomId = root["roomid"].asString();
    out.m_action = toInt(root["action"], 0);
    out.m_owner = toInt64(root["owner"]);
    out.m_actor = toInt64(root["actor"]);
    out.m_actorName = toStr(root["actorname"]);
    return out.m_action >= share_control_request && out.m_action <= share_control_revoke && out.m_owner > 0;
}

bool ShareProtocolClient::parseInfoNotify(const std::string& json, ShareInfoNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString())
        return false;
    out = ShareInfoNotify();
    out.m_roomId = root["roomid"].asString();
    out.m_ctype = toInt(root["ctype"], 0);
    out.m_cid = toInt64(root["cid"]);
    out.m_title = toStr(root["title"]);
    out.m_desc = toStr(root["desc"]);
    //改写后的卡片：服务端没能改写时这两项都不带
    out.m_cardMsgId = toInt64(root["msgid"]);
    out.m_cardContent = toStr(root["content"]);
    return true;
}

std::string ShareProtocolClient::buildRelayHello(const std::string& token)
{
    Json::Value root;
    root["token"] = token;
    return writeCompact(root);
}

bool ShareProtocolClient::parseResult(const std::string& json, ShareResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = toStr(root["msg"]);
    out.m_roomId = toStr(root["roomid"]);
    return true;
}

bool ShareProtocolClient::parseStartedNotify(const std::string& json, ShareStartedNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString() || root["host"].isNull())
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_ctype = toInt(root["ctype"], 0);
    out.m_cid = toInt64(root["cid"]);
    out.m_hostId = toInt64(root["host"]);
    out.m_hostName = toStr(root["hostname"]);
    out.m_startTime = toInt64(root["starttime"]);
    out.m_locked = toFlag(root["locked"], false);
    out.m_forced = toFlag(root["forced"], false);
    out.m_rejoin = toFlag(root["rejoin"], false);
    //共享标题与描述（第 15 期）；老服务端不带时为空
    out.m_title = toStr(root["title"]);
    out.m_desc = toStr(root["desc"]);
    return true;
}

bool ShareProtocolClient::parseStateNotify(const std::string& json, ShareStateNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString() || !root["event"].isInt() ||
        !root["members"].isArray())
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_event = root["event"].asInt();
    out.m_actor = toInt64(root["actor"]);
    out.m_hostId = toInt64(root["host"]);
    out.m_hostOnline = toFlag(root["hostonline"], true);
    out.m_annotateAllowed = toFlag(root["annotateallowed"], true);
    out.m_recordAllowed = toFlag(root["recordallowed"], true);
    out.m_forced = toFlag(root["forced"], false);
    out.m_target = toInt64(root["target"]);
    //当前的共享标题与描述（第 15 期）；老服务端不带时为空
    out.m_title = toStr(root["title"]);
    out.m_desc = toStr(root["desc"]);
    //同时共享屏幕的人数上限（第 22 期）；老服务端不带时只能一人共享
    out.m_maxSharers = toInt(root["maxsharers"], 1);
    out.m_members.clear();
    //老服务端的成员项没有 sharing 字段：解析完之后视为只有主持人在共享
    bool hasSharingField = false;
    const Json::Value& members = root["members"];
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        const Json::Value& m = members[i];
        if (!m.isObject())
            continue;
        ShareMember member;
        member.m_userId = toInt64(m["uid"]);
        member.m_seat = toInt(m["seat"], 0);
        member.m_mic = toFlag(m["mic"], false);
        member.m_camera = toFlag(m["camera"], false);
        member.m_systemAudio = toFlag(m["sysaudio"], false);
        member.m_recording = toFlag(m["recording"], false);
        member.m_p2pSessionId = toStr(m["p2psessionid"]);
        //正在控制此人屏幕的人（第 18 期）；没人控制或老服务端不带时为 0
        member.m_controlledBy = toInt64(m["controlledby"]);
        //多人共享（第 22 期）：是否在共享、画面座位、是否申请中
        if (m.isMember("sharing"))
            hasSharingField = true;
        member.m_sharing = toFlag(m["sharing"], false);
        member.m_screenSeat = member.m_sharing ? toInt(m["screenseat"], member.m_seat) : -1;
        member.m_requesting = toFlag(m["requesting"], false);
        //申请了新建白板（第 23 期）
        member.m_boardRequesting = toFlag(m["boardrequesting"], false);
        //凭分享链接加入的链接访客（第 25 期）
        member.m_viaLink = toFlag(m["vialink"], false);
        out.m_members.push_back(member);
    }
    //白板（第 23 期）：画面座位与建立者
    //云端录制（第 26 期）：正在录时带 cloudrecord 对象；被动结束的原因只随事件 15 带 cloudrecordcode
    out.m_cloudRecordId = 0;
    out.m_cloudRecordOwner = 0;
    out.m_cloudRecordPaused = false;
    out.m_cloudRecordSince = 0;
    const Json::Value& cloud = root["cloudrecord"];
    if (cloud.isObject())
    {
        out.m_cloudRecordId = toInt64(cloud["recordid"]);
        out.m_cloudRecordOwner = toInt64(cloud["owner"]);
        out.m_cloudRecordPaused = toFlag(cloud["paused"], false);
        out.m_cloudRecordSince = toInt64(cloud["since"]);
    }
    out.m_cloudRecordCode = toInt(root["cloudrecordcode"], 0);
    out.m_whiteboards.clear();
    const Json::Value& boards = root["whiteboards"];
    for (Json::ArrayIndex i = 0; boards.isArray() && i < boards.size(); ++i)
    {
        const Json::Value& b = boards[i];
        if (!b.isObject())
            continue;
        ShareWhiteboardInfo info;
        info.m_seat = toInt(b["seat"], -1);
        info.m_ownerId = toInt64(b["owner"]);
        if (info.m_seat >= 0 && info.m_ownerId > 0)
            out.m_whiteboards.push_back(info);
    }
    if (!hasSharingField)
    {
        for (size_t i = 0; i < out.m_members.size(); ++i)
        {
            if (out.m_members[i].m_userId == out.m_hostId)
            {
                out.m_members[i].m_sharing = true;
                out.m_members[i].m_screenSeat = out.m_members[i].m_seat;
            }
        }
    }
    return true;
}

bool ShareProtocolClient::parseRelayInfo(const std::string& json, ShareRelayInfo& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString() || !root["token"].isString() ||
        !root["relayport"].isInt())
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_relayIp = toStr(root["relayip"]);
    out.m_relayPort = root["relayport"].asInt();
    out.m_token = root["token"].asString();
    out.m_seat = toInt(root["seat"], -1);
    out.m_maxCameras = toInt(root["maxcameras"], 0);
    out.m_elapsedMs = root["elapsedms"].isIntegral() ? root["elapsedms"].asInt64() : 0;
    //可选字段：单聊共享里服务端为本人建了 P2P 会话时才有（P2P 方案第 6 期）
    out.m_p2pSessionId = toStr(root["p2psessionid"]);
    return true;
}

bool ShareProtocolClient::parseQueryResult(const std::string& json, ShareQueryResult& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = toStr(root["msg"]);
    out.m_exists = toFlag(root["exists"], false);
    out.m_roomId = out.m_exists ? toStr(root["roomid"]) : std::string();
    out.m_hostId = out.m_exists ? toInt64(root["host"]) : 0;
    out.m_memberCount = out.m_exists ? toInt(root["membercount"], 0) : 0;
    out.m_startTime = out.m_exists ? toInt64(root["starttime"]) : 0;
    out.m_locked = out.m_exists && toFlag(root["locked"], false);
    out.m_forced = out.m_exists && toFlag(root["forced"], false);
    //共享标题与描述（第 15 期）；没有进行中的共享或老服务端时为空
    out.m_title = out.m_exists ? toStr(root["title"]) : std::string();
    out.m_desc = out.m_exists ? toStr(root["desc"]) : std::string();
    return true;
}

bool ShareProtocolClient::parseEndedNotify(const std::string& json, ShareEndedNotify& out)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString())
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_ctype = toInt(root["ctype"], 0);
    out.m_cid = toInt64(root["cid"]);
    out.m_hostId = toInt64(root["host"]);
    out.m_durationSeconds = toInt64(root["duration"]);
    out.m_reason = toInt(root["reason"], 0);
    return true;
}

bool ShareProtocolClient::parseRelayHelloAck(const std::string& json, int& code)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    code = root["code"].asInt();
    return true;
}

} // namespace screenshare
