/**
 *  v2 远程桌面信令编解码（客户端侧）实现，RemoteDesktopProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RemoteDesktopProtocolClient.h"
#include <stdlib.h>                     // strtoll
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

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

std::string RemoteDesktopProtocolClient::buildInviteRequest(int64_t peerUserId)
{
    Json::Value root;
    //peerid 以字符串编码，避免大整数精度丢失
    root["peerid"] = std::to_string(peerUserId);
    return writeCompact(root);
}

std::string RemoteDesktopProtocolClient::buildResponse(const std::string& sessionId, bool accept)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    root["accept"]    = accept ? 1 : 0;
    return writeCompact(root);
}

std::string RemoteDesktopProtocolClient::buildCancel(const std::string& sessionId)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

std::string RemoteDesktopProtocolClient::buildClose(const std::string& sessionId)
{
    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

std::string RemoteDesktopProtocolClient::buildRelayHello(const std::string& token)
{
    Json::Value root;
    root["token"] = token;
    return writeCompact(root);
}

bool RemoteDesktopProtocolClient::parseInviteResponse(const std::string& jsonbody, RDInviteResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //没有整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].asString();
    //仅成功时才有会话 id
    if (out.m_code == 0)
        out.m_sessionId = root["sessionid"].asString();
    return true;
}

bool RemoteDesktopProtocolClient::parseInviteNotify(const std::string& jsonbody, RDInviteNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = root["sessionid"].asString();
    out.m_fromId    = jsonToInt64(root["fromid"]);
    out.m_fromName  = root["fromname"].asString();

    //缺会话 id 或发起方 userid 视为非法推送（userid 恒大于 0）
    if (out.m_sessionId.empty() || out.m_fromId <= 0)
        return false;
    return true;
}

bool RemoteDesktopProtocolClient::parseRelayInfo(const std::string& jsonbody, RDRelayInfo& out)
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

bool RemoteDesktopProtocolClient::parseStateNotify(const std::string& jsonbody, RDStateNotify& out)
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

bool RemoteDesktopProtocolClient::parseRelayHelloAck(const std::string& jsonbody, RDRelayHelloAck& out)
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
