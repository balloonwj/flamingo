/**
 *  v2 音视频通话信令编解码（服务端侧）实现，CallProtocol.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallProtocol.h"
#include <stdlib.h>                  // atoll
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
//一对一通话卡片 content 里 kind 的取值（群通话卡片为 groupcall，见 GroupCallProtocol）
static const char* const kCardKindCall = "call";
//毫秒换算成秒（卡片时长按秒记）
static const int64_t kMsPerSecond = 1000;

static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

bool CallProtocol::parseInviteRequest(const std::string& json, int64_t& peerid, int& media)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    peerid = getStringId(root, "peerid");
    //media 缺省按纯语音处理（兼容只发 peerid 的请求；第三期视频置 call_media_video）
    media = root["media"].isInt() ? root["media"].asInt() : call_media_audio;
    //被叫 userid 必须为正
    return peerid > 0;
}

bool CallProtocol::parseResponseRequest(const std::string& json,
                                        std::string& sessionid, int& accept)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    sessionid = root["sessionid"].asString();
    //缺会话 id 视为非法
    if (sessionid.empty())
        return false;

    accept = root["accept"].isInt() ? root["accept"].asInt() : 0;
    return true;
}

bool CallProtocol::parseSessionRequest(const std::string& json, std::string& sessionid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    sessionid = root["sessionid"].asString();
    //缺会话 id 视为非法
    return !sessionid.empty();
}

bool CallProtocol::parseSwitchMediaRequest(const std::string& json, std::string& sessionid, int& media)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    sessionid = root["sessionid"].asString();
    //缺会话 id 无法定位是哪通通话，视为非法请求
    if (sessionid.empty())
        return false;

    media = root["media"].isInt() ? root["media"].asInt() : 0;
    //媒体类型必须在 enum call_media 之内：放行未知类型会让会话记录进入不可名状的状态
    if (media != call_media_audio && media != call_media_video)
        return false;

    return true;
}

std::string CallProtocol::buildMediaChangedNotify(const std::string& sessionid, int media,
                                                  int64_t operatorId)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["media"]     = media;
    //operator 以字符串编码，与其它 64 位 id 同口径，避免 JSON 数值精度丢失
    root["operator"]  = std::to_string(operatorId);
    return writeCompact(root);
}

std::string CallProtocol::buildInviteResponse(int code, const std::string& msg,
                                              const std::string& sessionid, const std::string& p2pSessionId)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //仅成功且会话 id 非空时下发会话 id
    if (code == 0 && !sessionid.empty())
        root["sessionid"] = sessionid;
    //建了 P2P 会话才写这个字段，没建时与第 6 期之前的应答逐字相同
    if (code == 0 && !p2pSessionId.empty())
        root["p2psessionid"] = p2pSessionId;
    return writeCompact(root);
}

std::string CallProtocol::buildInviteNotify(const std::string& sessionid, int64_t fromid,
                                            const std::string& fromname, int media, int64_t ringMs,
                                            const std::string& p2pSessionId)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    //fromid 以字符串编码，避免大整数精度丢失
    root["fromid"]    = std::to_string(fromid);
    root["fromname"]  = fromname;
    root["media"]     = media;
    //剩余振铃毫秒数（第 15 期）：被叫的来电弹窗据此显示倒计时
    if (ringMs >= 0)
        root["ringms"] = static_cast<Json::Int64>(ringMs);
    //建了 P2P 会话才写这个字段（P2P 方案第 6 期）
    if (!p2pSessionId.empty())
        root["p2psessionid"] = p2pSessionId;
    return writeCompact(root);
}

std::string CallProtocol::buildSignaling(const std::string& sessionid, int role,
                                         const std::string& relayip, int relayport,
                                         const std::string& token)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["role"]      = role;
    root["relayip"]   = relayip;
    root["relayport"] = relayport;
    root["token"]     = token;
    return writeCompact(root);
}

std::string CallProtocol::buildStateNotify(const std::string& sessionid, int state,
                                           const std::string& msg)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["state"]     = state;
    root["msg"]       = msg;
    return writeCompact(root);
}

std::string CallProtocol::buildCardContent(const std::string& sessionid, int media, int64_t callerId,
                                           const std::string& callerName, int64_t calleeId, int64_t startTime)
{
    Json::Value root;
    root["kind"] = kCardKindCall;
    root["sessionid"] = sessionid;
    root["media"] = media;
    root["caller"] = std::to_string(callerId);
    root["callername"] = callerName;
    root["callee"] = std::to_string(calleeId);
    root["starttime"] = std::to_string(startTime);
    return writeCompact(root);
}

std::string CallProtocol::buildEndedCardContent(const std::string& oldContent, const std::string& sessionid,
                                                int media, int64_t callerId, const std::string& callerName,
                                                int64_t calleeId, int64_t startTime, int64_t connectTime, int result,
                                                int64_t endTime)
{
    Json::Value root;
    //原 content 不可用（解析失败或不是一对一通话卡片）时按会话信息重建
    if (!parseObject(oldContent, root) || !root["kind"].isString() || root["kind"].asString() != kCardKindCall)
    {
        Json::Value rebuilt;
        parseObject(buildCardContent(sessionid, media, callerId, callerName, calleeId, startTime), rebuilt);
        root = rebuilt;
    }
    root["media"] = media;
    root["result"] = result;
    root["endtime"] = std::to_string(endTime);
    //接通过才有时长；结束时刻早于接通时刻（时钟回拨）按 0 秒处理
    if (connectTime > 0)
    {
        const int64_t seconds = (endTime > connectTime) ? (endTime - connectTime) / kMsPerSecond : 0;
        root["duration"] = static_cast<Json::Int64>(seconds);
    }
    return writeCompact(root);
}

int CallProtocol::cardMedia(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root) || !root["kind"].isString() || root["kind"].asString() != kCardKindCall)
        return 0;
    if (!root["media"].isInt())
        return 0;
    const int media = root["media"].asInt();
    if (media != call_media_audio && media != call_media_video)
        return 0;
    return media;
}

std::string CallProtocol::buildCardNotify(const std::string& sessionid, int64_t msgId, int64_t cid,
                                          const std::string& content)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["msgid"] = std::to_string(msgId);
    root["cid"] = std::to_string(cid);
    Json::Value card;
    if (parseObject(content, card))
        root["content"] = card;
    else
        root["content"] = Json::Value(Json::objectValue);
    return writeCompact(root);
}
