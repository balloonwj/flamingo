/**
 *  v2 音视频通话信令编解码（客户端侧）实现，CallProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（userid 等）以字符串编码进 JSON，以避免 JSON 数值精度
//丢失，故解析时需经本函数还原成整型。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false。
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

std::string CallProtocolClient::buildInviteRequest(int64_t peerUserId, int media)
{
    Json::Value root;
    //peerid 以字符串编码，避免大整数精度丢失
    root["peerid"] = std::to_string(peerUserId);
    root["media"]  = media;
    return writeCompact(root);
}

std::string CallProtocolClient::buildResponse(const std::string& sessionId, bool accept)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    root["accept"]    = accept ? 1 : 0;
    return writeCompact(root);
}

std::string CallProtocolClient::buildCancel(const std::string& sessionId)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

std::string CallProtocolClient::buildHangup(const std::string& sessionId)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

std::string CallProtocolClient::buildSwitchMedia(const std::string& sessionId, int media)
{
    //会话 id 为空、或目标媒体类型不在 enum call_media 之内时，服务端必判非法请求；
    //这里提前挡住，返回空串让调用方放弃发送，省一次必然失败的往返。
    if (sessionId.empty())
    {
        return std::string();
    }
    if (media != call_media_audio && media != call_media_video)
    {
        return std::string();
    }

    Json::Value root;
    root["sessionid"] = sessionId;
    root["media"]     = media;
    return writeCompact(root);
}

bool CallProtocolClient::parseSwitchMediaResponse(const std::string& jsonbody,
                                                  CallSwitchMediaResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功。
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

bool CallProtocolClient::parseMediaChangedNotify(const std::string& jsonbody,
                                                 CallMediaChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = root["sessionid"].isString() ? root["sessionid"].asString() : std::string();
    //缺会话 id 无法定位是哪通通话，整条推送不可用。
    if (out.m_sessionId.empty())
        return false;

    out.m_media = static_cast<int>(jsonToInt64(root["media"]));
    //媒体类型非法时同样整条丢弃：落地一个未知类型会让 UI 进入不可名状的中间态。
    if (out.m_media != call_media_audio && out.m_media != call_media_video)
        return false;

    //operator 用于区分本端是否为发起方（决定要不要自动开摄像头）；缺失取 0，
    //上层按"非本端发起"退化处理，即不自动开摄像头，语义上是安全的一侧。
    out.m_operator = jsonToInt64(root["operator"]);
    return true;
}

std::string CallProtocolClient::buildRelayHello(const std::string& token)
{
    Json::Value root;
    root["token"] = token;
    return writeCompact(root);
}

bool CallProtocolClient::parseInviteResponse(const std::string& jsonbody, CallInviteResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //没有整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].asString();
    //仅成功时才有会话 id；建了 P2P 会话时另有 P2P 会话号
    if (out.m_code == 0)
    {
        out.m_sessionId = root["sessionid"].asString();
        out.m_p2pSessionId = root["p2psessionid"].asString();
    }
    return true;
}

bool CallProtocolClient::parseInviteNotify(const std::string& jsonbody, CallInviteNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = root["sessionid"].asString();
    out.m_fromId    = jsonToInt64(root["fromid"]);
    out.m_fromName  = root["fromname"].asString();
    //media 缺省按纯语音处理
    out.m_media     = root["media"].isIntegral() ? root["media"].asInt() : call_media_audio;
    //剩余振铃毫秒数（第 15 期）：老服务端不带、或不是非负整数时为 -1，来电弹窗按缺省时长倒数
    out.m_ringMs    = (root["ringms"].isIntegral() && root["ringms"].asInt64() >= 0) ? root["ringms"].asInt64() : -1;
    //可选字段：服务端建了 P2P 会话时才有（P2P 方案第 6 期）
    out.m_p2pSessionId = root["p2psessionid"].asString();

    //缺会话 id 或发起方 userid 视为非法推送（userid 恒大于 0）
    if (out.m_sessionId.empty() || out.m_fromId <= 0)
        return false;
    return true;
}

bool CallProtocolClient::parseSignaling(const std::string& jsonbody, CallSignalingInfo& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = root["sessionid"].asString();
    out.m_role      = static_cast<int>(jsonToInt64(root["role"]));
    out.m_relayIp   = root["relayip"].asString();
    out.m_relayPort = static_cast<int>(jsonToInt64(root["relayport"]));
    out.m_token     = root["token"].asString();

    //缺会话 id 或令牌视为非法推送
    if (out.m_sessionId.empty() || out.m_token.empty())
        return false;
    return true;
}

bool CallProtocolClient::parseStateNotify(const std::string& jsonbody, CallStateNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = root["sessionid"].asString();
    out.m_state     = static_cast<int>(jsonToInt64(root["state"]));
    out.m_msg       = root["msg"].asString();

    //缺会话 id 视为非法推送
    if (out.m_sessionId.empty())
        return false;
    return true;
}

bool CallProtocolClient::parseRelayHelloAck(const std::string& jsonbody, CallRelayHelloAck& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //没有整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].asString();
    return true;
}

} // namespace net
