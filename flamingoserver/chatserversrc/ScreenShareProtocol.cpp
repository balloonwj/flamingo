/**
 *  v2 屏幕共享信令编解码（服务端侧）实现，ScreenShareProtocol.cpp
 *  balloonwj@qq.com   2026-10-02
 */
#include "ScreenShareProtocol.h"

#include <stdlib.h>                  // atoll
#include <sstream>
#include "Protocol.h"                // conversation_single / conversation_group
#include "ScreenShareRules.h"        // isValidPassword（共享密码格式，第 12 期）
#include "../jsoncpp1.9.0/json.h"

namespace {

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

//会话类型是否是屏幕共享支持的两种（单聊、群聊）
bool isSupportedCtype(int ctype)
{
    return ctype == conversation_single || ctype == conversation_group;
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

//取 0 / 1 开关字段；字段缺失时返回 defaultValue 并把 present 置 false，不是整数时 present 置 false、返回 defaultValue
bool getSwitch(const Json::Value& root, const char* key, bool defaultValue, bool& present)
{
    present = root.isMember(key) && root[key].isInt();
    if (!present)
    {
        return defaultValue;
    }
    return root[key].asInt() != 0;
}

//bool 转 JSON 里的 0 / 1
int toFlag(bool v)
{
    return v ? 1 : 0;
}

} // namespace

bool ScreenShareProtocol::parseStartRequest(const std::string& json, int& ctype, int64_t& cid, bool& mic, bool& camera,
                                            bool& sysAudio)
{
    Json::Value root;
    if (!parseObject(json, root))
    {
        return false;
    }
    if (!root["ctype"].isInt())
    {
        return false;
    }
    ctype = root["ctype"].asInt();
    cid = getStringId(root, "cid");
    if (!isSupportedCtype(ctype) || cid <= 0)
    {
        return false;
    }
    bool present = false;
    mic = getSwitch(root, "mic", false, present);
    camera = getSwitch(root, "camera", false, present);
    sysAudio = getSwitch(root, "sysaudio", false, present);
    return true;
}

bool ScreenShareProtocol::parseStartRequest(const std::string& json, int& ctype, int64_t& cid, bool& mic, bool& camera,
                                            bool& sysAudio, std::string& password)
{
    if (!parseStartRequest(json, ctype, cid, mic, camera, sysAudio))
        return false;
    password.clear();
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root.isMember("password") || root["password"].isNull())
        return true;
    if (!root["password"].isString())
        return false;
    password = root["password"].asString();
    //空串等同不设密码；不是空串就必须是 6 位数字
    return password.empty() || ScreenShareRules::isValidPassword(password);
}

ScreenShareLinkSummary::ScreenShareLinkSummary()
    : m_status(0)
    , m_hostId(0)
    , m_locked(false)
    , m_forced(false)
    , m_member(false)
    , m_ctype(0)
    , m_cid(0)
{
}

ScreenShareStartRequest::ScreenShareStartRequest()
    : m_ctype(0)
    , m_cid(0)
    , m_mic(false)
    , m_camera(false)
    , m_sysAudio(false)
    , m_forced(false)
    , m_recordAllowed(true)
{
}

bool ScreenShareProtocol::parseStartRequest(const std::string& json, ScreenShareStartRequest& out)
{
    out = ScreenShareStartRequest();
    if (!parseStartRequest(json, out.m_ctype, out.m_cid, out.m_mic, out.m_camera, out.m_sysAudio, out.m_password))
        return false;
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    //两个开关出现时必须是整数，避免把写错的请求当成默认值静默放过
    if ((root.isMember("forced") && !root["forced"].isInt()) ||
        (root.isMember("recordallowed") && !root["recordallowed"].isInt()))
        return false;
    bool present = false;
    out.m_forced = getSwitch(root, "forced", false, present);
    out.m_recordAllowed = getSwitch(root, "recordallowed", true, present);
    //共享标题与描述（第 15 期）：出现时必须是字符串；是否必填、长度与敏感词由 ScreenShareRules::checkShareInfo 判断
    if ((root.isMember("title") && !root["title"].isString()) || (root.isMember("desc") && !root["desc"].isString()))
        return false;
    out.m_title = root.isMember("title") ? root["title"].asString() : std::string();
    out.m_desc = root.isMember("desc") ? root["desc"].asString() : std::string();
    return true;
}

bool ScreenShareProtocol::parseJoinRequest(const std::string& json, std::string& roomId, std::string& password)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    password.clear();
    if (!root.isMember("password") || root["password"].isNull())
        return true;
    if (!root["password"].isString())
        return false;
    password = root["password"].asString();
    return true;
}

bool ScreenShareProtocol::parseJoinRequest(const std::string& json, std::string& roomId, std::string& password,
                                           std::string& linkCode)
{
    if (!parseJoinRequest(json, roomId, password))
        return false;
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    linkCode.clear();
    //分享链接码（第 25 期）：可选；出现时须为字符串
    if (!root.isMember("linkcode") || root["linkcode"].isNull())
        return true;
    if (!root["linkcode"].isString())
        return false;
    linkCode = root["linkcode"].asString();
    return true;
}

bool ScreenShareProtocol::parseLinkRequest(const std::string& json, std::string& roomId, int& action, bool& noPassword)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    if (!root.isMember("action") || !root["action"].isInt())
        return false;
    action = root["action"].asInt();
    if (action < screenshare_link_get || action > screenshare_link_set_nopassword)
        return false;
    bool present = false;
    noPassword = getSwitch(root, "nopassword", false, present);
    return true;
}

std::string ScreenShareProtocol::buildLinkResponse(int code, const std::string& msg, const std::string& roomId,
                                                   const std::string& linkCode, bool noPassword)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!roomId.empty())
        root["roomid"] = roomId;
    if (code == 0)
    {
        root["linkcode"] = linkCode;
        root["nopassword"] = toFlag(noPassword);
    }
    return writeCompact(root);
}

bool ScreenShareProtocol::parseLinkResolveRequest(const std::string& json, std::string& linkCode)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root.isMember("linkcode") || !root["linkcode"].isString())
        return false;
    linkCode = root["linkcode"].asString();
    return !linkCode.empty();
}

std::string ScreenShareProtocol::buildLinkResolveResponse(int code, const std::string& msg,
                                                          const ScreenShareLinkSummary& summary)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (code != 0)
        return writeCompact(root);
    root["status"] = summary.m_status;
    //已结束的共享只告诉状态，不再带房间信息
    if (summary.m_roomId.empty())
        return writeCompact(root);
    root["roomid"] = summary.m_roomId;
    root["title"] = summary.m_title;
    root["desc"] = summary.m_desc;
    root["host"] = int64ToString(summary.m_hostId);
    root["hostname"] = summary.m_hostName;
    root["locked"] = toFlag(summary.m_locked);
    root["forced"] = toFlag(summary.m_forced);
    root["member"] = toFlag(summary.m_member);
    //会话成员才知道是哪个会话（链接访客不需要也不该知道）
    if (summary.m_member)
    {
        root["ctype"] = summary.m_ctype;
        root["cid"] = int64ToString(summary.m_cid);
    }
    return writeCompact(root);
}

bool ScreenShareProtocol::parseRoomRequest(const std::string& json, std::string& roomId)
{
    Json::Value root;
    if (!parseObject(json, root))
    {
        return false;
    }
    return getRoomId(root, roomId);
}

bool ScreenShareProtocol::parseQueryRequest(const std::string& json, int& ctype, int64_t& cid)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["ctype"].isInt())
    {
        return false;
    }
    ctype = root["ctype"].asInt();
    cid = getStringId(root, "cid");
    return isSupportedCtype(ctype) && cid > 0;
}

bool ScreenShareProtocol::parseMemberMediaRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera,
                                                  bool& sysAudio)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    //客户端每次上报完整状态，三项缺一项即视为非法
    bool micPresent = false;
    bool cameraPresent = false;
    bool sysAudioPresent = false;
    mic = getSwitch(root, "mic", false, micPresent);
    camera = getSwitch(root, "camera", false, cameraPresent);
    sysAudio = getSwitch(root, "sysaudio", false, sysAudioPresent);
    return micPresent && cameraPresent && sysAudioPresent;
}

bool ScreenShareProtocol::parsePermissionRequest(const std::string& json, std::string& roomId, int& annotate,
                                                 int& record, bool& muteAll)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    bool annotatePresent = false;
    bool recordPresent = false;
    bool mutePresent = false;
    bool annotateValue = getSwitch(root, "annotate", false, annotatePresent);
    bool recordValue = getSwitch(root, "record", false, recordPresent);
    muteAll = getSwitch(root, "muteall", false, mutePresent);
    //省略即不变，用 -1 表示
    annotate = annotatePresent ? toFlag(annotateValue) : -1;
    record = recordPresent ? toFlag(recordValue) : -1;
    //三项至少要有一项表达了操作（muteall:0 不算操作）
    return annotatePresent || recordPresent || muteAll;
}

bool ScreenShareProtocol::parsePermissionRequest(const std::string& json, std::string& roomId, int& annotate,
                                                 int& record, bool& muteAll, int64_t& muteUser)
{
    muteUser = 0;
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    const bool mutePresent = root.isMember("muteuser") && !root["muteuser"].isNull();
    if (mutePresent)
    {
        muteUser = getStringId(root, "muteuser");
        if (muteUser <= 0)
        {
            return false;
        }
    }
    //另外三项沿用原来的解析；只带 muteuser 时原解析会因「没有操作」返回 false，此时按 muteuser 算一项操作
    const bool others = parsePermissionRequest(json, roomId, annotate, record, muteAll);
    if (!others)
    {
        bool present = false;
        annotate = -1;
        record = -1;
        muteAll = getSwitch(root, "muteall", false, present);
    }
    return others || mutePresent;
}

bool ScreenShareProtocol::parseRemoveMemberRequest(const std::string& json, std::string& roomId, int64_t& userId,
                                                   bool& allowRejoin)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    userId = getStringId(root, "userid");
    if (userId <= 0)
    {
        return false;
    }
    //allowrejoin 缺省为允许；出现了却不是整数时整个请求非法
    if (root.isMember("allowrejoin") && !root["allowrejoin"].isInt())
    {
        return false;
    }
    bool present = false;
    allowRejoin = getSwitch(root, "allowrejoin", true, present);
    return true;
}

bool ScreenShareProtocol::parseRecordStateRequest(const std::string& json, std::string& roomId, bool& recording)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
    {
        return false;
    }
    bool present = false;
    recording = getSwitch(root, "recording", false, present);
    return present;
}

bool ScreenShareProtocol::parseSetInfoRequest(const std::string& json, std::string& roomId, std::string& title,
                                              std::string& desc)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    //标题必须带上；描述可以不带，带了必须是字符串
    if (!root.isMember("title") || !root["title"].isString())
        return false;
    if (root.isMember("desc") && !root["desc"].isString())
        return false;
    title = root["title"].asString();
    desc = root.isMember("desc") ? root["desc"].asString() : std::string();
    return true;
}

bool ScreenShareProtocol::parseSharingRequest(const std::string& json, std::string& roomId, int& action, int64_t& target)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    if (!root.isMember("action") || !root["action"].isInt())
        return false;
    action = root["action"].asInt();
    if (action < screenshare_sharing_request || action > screenshare_sharing_stop)
        return false;
    target = getStringId(root, "target");
    //同意与拒绝必须指明对象；停止可选（不带表示停自己的）；申请、撤回不用 target，带了也忽略
    if (action == screenshare_sharing_approve || action == screenshare_sharing_reject)
        return target > 0;
    if (action != screenshare_sharing_stop || target < 0)
        target = 0;
    return true;
}

bool ScreenShareProtocol::parseTransferHostRequest(const std::string& json, std::string& roomId, int64_t& target)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    target = getStringId(root, "target");
    return target > 0;
}

int ScreenShareProtocol::parseSharingKind(const std::string& json)
{
    Json::Value root;
    if (!parseObject(json, root) || !root.isMember("kind") || !root["kind"].isInt())
        return screenshare_kind_screen;
    return (root["kind"].asInt() == screenshare_kind_whiteboard) ? screenshare_kind_whiteboard : screenshare_kind_screen;
}

std::string ScreenShareProtocol::buildBoardNotify(const std::string& roomId, int action, int64_t actor,
                                                  const std::string& actorName, int boardSeat)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["kind"] = screenshare_kind_whiteboard;
    root["action"] = action;
    root["actor"] = int64ToString(actor);
    root["actorname"] = actorName;
    //同意（含主持人自己直接新建）：带白板的画面座位，建立者据此打开白板窗口
    if (boardSeat >= 0)
    {
        root["screenseat"] = boardSeat;
    }
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildSharingNotify(const std::string& roomId, int action, int64_t actor,
                                                    const std::string& actorName, const std::string& token,
                                                    const std::string& relayIp, int relayPort, int screenSeat)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    root["actor"] = int64ToString(actor);
    root["actorname"] = actorName;
    //同意：带「只推流」令牌、中转地址与画面座位，被同意的人据此另连一条中转连接推屏幕
    if (!token.empty())
    {
        root["token"] = token;
        root["relayip"] = relayIp;
        root["relayport"] = relayPort;
        root["screenseat"] = screenSeat;
    }
    return writeCompact(root);
}

bool ScreenShareProtocol::parseControlRequest(const std::string& json, std::string& roomId, int& action,
                                              int64_t& owner, int64_t& target)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRoomId(root, roomId))
        return false;
    if (!root.isMember("action") || !root["action"].isInt())
        return false;
    action = root["action"].asInt();
    if (action < screenshare_control_request || action > screenshare_control_revoke)
        return false;
    owner = getStringId(root, "owner");
    if (owner <= 0)
        return false;
    target = getStringId(root, "target");
    //授予与拒绝必须指明对象；其余动作不用 target，带了也忽略
    if (action == screenshare_control_grant || action == screenshare_control_deny)
        return target > 0;
    target = 0;
    return true;
}

std::string ScreenShareProtocol::buildControlNotify(const std::string& roomId, int action, int64_t owner,
                                                    int64_t actor, const std::string& actorName)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["action"] = action;
    root["owner"] = int64ToString(owner);
    root["actor"] = int64ToString(actor);
    root["actorname"] = actorName;
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildInfoNotify(const std::string& roomId, int ctype, int64_t cid,
                                                 const std::string& title, const std::string& desc, int64_t cardMsgId,
                                                 const std::string& cardContent, int64_t hostId,
                                                 const std::string& hostName)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["ctype"] = ctype;
    root["cid"] = int64ToString(cid);
    root["title"] = title;
    root["desc"] = desc;
    //改写后的卡片：客户端按 msgid 替换本地保存的那张卡片；卡片没写成功时不带
    if (cardMsgId > 0)
    {
        root["msgid"] = int64ToString(cardMsgId);
        root["content"] = cardContent;
    }
    //移交主持人后的那一条：带新主持人；主持人没变时不带，格式与第 15 期相同
    if (hostId > 0)
    {
        root["hostid"] = int64ToString(hostId);
        root["hostname"] = hostName;
    }
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildResult(int code, const std::string& msg, const std::string& roomId)
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

std::string ScreenShareProtocol::buildStartedNotify(const std::string& roomId, int ctype, int64_t cid, int64_t hostId,
                                                    const std::string& hostName, int64_t startTime, bool locked,
                                                    bool forced, bool rejoin, const std::string& title,
                                                    const std::string& desc)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["ctype"] = ctype;
    root["cid"] = int64ToString(cid);
    root["host"] = int64ToString(hostId);
    root["hostname"] = hostName;
    root["starttime"] = int64ToString(startTime);
    //加入是否需要密码（第 12 期）：只告诉客户端要不要先问密码，不带密码本身
    root["locked"] = toFlag(locked);
    //强制模式（第 13 期）；rejoin 表示让强制模式下掉线的观看者回到观看，客户端直接加入、不弹确认
    root["forced"] = toFlag(forced);
    root["rejoin"] = toFlag(rejoin);
    //共享标题与描述（第 15 期）：描述没填时为空串，字段照样带上
    root["title"] = title;
    root["desc"] = desc;
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildStateNotify(int event, int64_t actor, const ScreenShareRoom& room, int64_t target)
{
    Json::Value root;
    root["roomid"] = room.m_roomId;
    root["event"] = event;
    root["actor"] = int64ToString(actor);
    root["host"] = int64ToString(room.m_hostId);
    root["hostonline"] = toFlag(room.m_hostOfflineSince == 0);
    root["annotateallowed"] = toFlag(room.m_annotateAllowed);
    root["recordallowed"] = toFlag(room.m_recordAllowed);
    //强制模式（第 13 期）：观看端据此隐藏「离开」「退出全屏」
    root["forced"] = toFlag(room.m_forced);
    //共享标题与描述（第 15 期）：发起人中途修改后，房间里的人据此刷新观看窗标题与名单
    root["title"] = room.m_title;
    root["desc"] = room.m_desc;
    //同时共享屏幕的人数上限（第 22 期）：观看端据此决定「共享我的屏幕」是否可用
    root["maxsharers"] = room.m_maxSharers;
    //事件针对的成员（第 13 期，事件 9 为被静音的人）
    if (target > 0)
    {
        root["target"] = int64ToString(target);
    }
    //云端录制（第 26 期）：正在录时带状态；客户端据此显示「正在云端录制」，单聊共享还据此改回房间中转（不走 P2P）
    if (room.m_cloudRecordId > 0)
    {
        Json::Value cloud(Json::objectValue);
        cloud["recordid"] = int64ToString(room.m_cloudRecordId);
        cloud["owner"] = int64ToString(room.m_cloudRecordOwner);
        cloud["paused"] = toFlag(room.m_cloudRecordPaused);
        cloud["since"] = static_cast<Json::Int64>(room.m_cloudRecordSince);
        root["cloudrecord"] = cloud;
    }
    //云端录制被动结束的原因：只随事件 15 下发
    if (event == screenshare_event_cloudrecord_changed && room.m_cloudRecordEndCode != 0)
    {
        root["cloudrecordcode"] = room.m_cloudRecordEndCode;
    }
    Json::Value members(Json::arrayValue);
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        const ScreenShareMember& m = room.m_members[i];
        Json::Value item;
        item["uid"] = int64ToString(m.m_userId);
        item["seat"] = m.m_seat;
        item["mic"] = toFlag(m.m_mic);
        item["camera"] = toFlag(m.m_camera);
        item["sysaudio"] = toFlag(m.m_systemAudio);
        item["recording"] = toFlag(m.m_recording);
        //单聊共享里这名观看者与发起人之间的 P2P 会话号（P2P 方案第 6 期）：发起人据此认领链路槽；没建时不带
        if (!m.m_p2pSessionId.empty())
        {
            item["p2psessionid"] = m.m_p2pSessionId;
        }
        //正在控制此人屏幕的人（第 18 期控制键鼠）；没人控制时不带
        if (m.m_controlledBy > 0)
        {
            item["controlledby"] = int64ToString(m.m_controlledBy);
        }
        //凭分享链接加入、不是会话成员的人（第 25 期链接访客）；不是时不带
        if (m.m_viaLink)
        {
            item["vialink"] = 1;
        }
        //多人共享（第 22 期）：是否在共享；在共享时带画面座位（观看端按它认出这一路屏幕画面是谁的）；申请中时带 requesting
        item["sharing"] = toFlag(m.m_sharing);
        if (m.m_sharing && m.m_screenSeat >= 0)
        {
            item["screenseat"] = m.m_screenSeat;
        }
        if (m.m_shareRequested)
        {
            item["requesting"] = 1;
        }
        //申请了新建白板、等主持人处理（第 23 期）
        if (m.m_boardRequested)
        {
            item["boardrequesting"] = 1;
        }
        members.append(item);
    }
    root["members"] = members;
    //白板（第 23 期）：画面座位与建立者；没有白板时不带
    if (!room.m_whiteboards.empty())
    {
        Json::Value boards(Json::arrayValue);
        for (size_t i = 0; i < room.m_whiteboards.size(); ++i)
        {
            Json::Value b;
            b["seat"] = room.m_whiteboards[i].m_seat;
            b["owner"] = int64ToString(room.m_whiteboards[i].m_ownerId);
            boards.append(b);
        }
        root["whiteboards"] = boards;
    }
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildRelayInfo(const std::string& roomId, const std::string& relayIp, int relayPort,
                                                const std::string& token, int seat, int maxCameras, int64_t elapsedMs,
                                                const std::string& p2pSessionId)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["relayip"] = relayIp;
    root["relayport"] = relayPort;
    root["token"] = token;
    root["seat"] = seat;
    root["maxcameras"] = maxCameras;
    //共享已进行的毫秒数（第 13 期），客户端据此显示共享时长
    root["elapsedms"] = static_cast<Json::Int64>(elapsedMs > 0 ? elapsedMs : 0);
    //单聊共享里服务端为这名观看者建的 P2P 会话号（P2P 方案第 6 期）；没建时不带，与原来逐字相同
    if (!p2pSessionId.empty())
    {
        root["p2psessionid"] = p2pSessionId;
    }
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildQueryResponse(int code, const std::string& msg, const ScreenShareRoom* room)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["exists"] = toFlag(room != NULL);
    if (room != NULL)
    {
        root["roomid"] = room->m_roomId;
        root["host"] = int64ToString(room->m_hostId);
        root["membercount"] = static_cast<int>(room->m_members.size());
        root["starttime"] = int64ToString(room->m_startTime);
        root["locked"] = toFlag(!room->m_password.empty());
        root["forced"] = toFlag(room->m_forced);
        //共享标题与描述（第 15 期）：打开聊天窗时据此显示横幅
        root["title"] = room->m_title;
        root["desc"] = room->m_desc;
    }
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildEndedNotify(const std::string& roomId, int ctype, int64_t cid, int64_t hostId,
                                                  int64_t durationSeconds, int reason)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["ctype"] = ctype;
    root["cid"] = int64ToString(cid);
    root["host"] = int64ToString(hostId);
    root["duration"] = static_cast<Json::Int64>(durationSeconds);
    root["reason"] = reason;
    return writeCompact(root);
}

std::string ScreenShareProtocol::buildCardContent(const std::string& roomId, int64_t hostId, const std::string& hostName,
                                                  int64_t startTime, bool locked, bool forced, const std::string& title,
                                                  const std::string& desc)
{
    Json::Value root;
    root["roomid"] = roomId;
    root["host"] = int64ToString(hostId);
    //共享标题与描述（第 15 期）：卡片的粗体标题行与描述行
    root["title"] = title;
    root["desc"] = desc;
    root["hostname"] = hostName;
    root["starttime"] = int64ToString(startTime);
    //加入是否需要密码（第 12 期），卡片据此显示「需要密码」
    root["locked"] = toFlag(locked);
    //强制模式（第 13 期），卡片据此显示「强制观看」
    root["forced"] = toFlag(forced);
    return writeCompact(root);
}
