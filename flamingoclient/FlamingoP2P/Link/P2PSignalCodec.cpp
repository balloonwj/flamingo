/**
 *  客户端一侧 P2P 信令编解码的实现，见 P2PSignalCodec.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PSignalCodec.h"

#include <stdlib.h>
#include <memory>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../FlamingoP2PApi.h"
#include "../Ice/P2PAddressProber.h"

namespace p2p {

namespace {

//角色：发起方与应答方（Protocol.h 的 p2p_role）
const int kRoleControlling = 1;
const int kRoleControlled = 2;

//端口的取值上限
const int kMaxPort = 65535;

//十进制
const int kDecimalBase = 10;

//解析一段 JSON 对象
bool ParseObject(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &errs))
        return false;
    return root.isObject();
}

//紧凑地序列化
std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, v);
}

//取 64 位 id：十进制字符串或整数；其它类型返回 0
int64_t ToId(const Json::Value& v)
{
    if (v.isString())
        return (int64_t)strtoll(v.asString().c_str(), NULL, kDecimalBase);
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//取字符串字段：不存在或不是字符串时返回 false
bool GetString(const Json::Value& root, const char* key, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return true;
}

//取正整数字段（毫秒）：不存在、不是整数或不为正时返回 false
bool GetPositive(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key) || !root[key].isIntegral())
        return false;
    out = root[key].asInt64();
    return out > 0;
}

//解析一个服务器地址：{ "ip": "…", "port": … }
bool ParseEndpoint(const Json::Value& ipValue, const Json::Value& portValue, P2PServerEndpoint& out)
{
    if (!ipValue.isString() || ipValue.asString().empty() || !portValue.isInt())
        return false;
    const int port = portValue.asInt();
    if (port <= 0 || port > kMaxPort)
        return false;
    out.m_host = ipValue.asString();
    out.m_port = (uint16_t)port;
    return true;
}

} // namespace

bool P2PSessionInfo::IsControlling() const
{
    return m_role == kRoleControlling;
}

std::string P2PSignalCodec::BuildRegister(const std::vector<std::string>& purposes)
{
    Json::Value v(Json::objectValue);
    v["version"] = kP2PClientProtocolVersion;
    Json::Value list(Json::arrayValue);
    for (size_t i = 0; i < purposes.size(); ++i)
        list.append(purposes[i]);
    v["purposes"] = list;
    return Compact(v);
}

bool P2PSignalCodec::ParseRegisterResponse(const std::string& body, P2PRegisterResult& out)
{
    out = P2PRegisterResult();
    Json::Value root;
    if (!ParseObject(body, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    //出错时一律视为关闭：不收集地址、不建会话
    if (out.m_code != 0)
        return true;
    out.m_enabled = root["enabled"].isBool() && root["enabled"].asBool();
    if (!out.m_enabled || !root["probe"].isArray())
        return true;
    const Json::Value& list = root["probe"];
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        P2PServerEndpoint ep;
        if (list[i].isObject() && ParseEndpoint(list[i]["ip"], list[i]["port"], ep))
            out.m_probes.push_back(ep);
    }
    return true;
}

bool P2PSignalCodec::ParseSessionNotify(const std::string& body, P2PSessionInfo& out)
{
    out = P2PSessionInfo();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    if (!GetString(root, "p2psessionid", out.m_sessionId) || out.m_sessionId.empty())
        return false;
    if (!root["role"].isInt())
        return false;
    out.m_role = root["role"].asInt();
    if (out.m_role != kRoleControlling && out.m_role != kRoleControlled)
        return false;
    GetString(root, "purpose", out.m_purpose);
    GetString(root, "featuresessionid", out.m_featureSessionId);
    GetString(root, "peerdeviceid", out.m_peerDeviceId);
    GetString(root, "token", out.m_token);
    out.m_peerUid = ToId(root["peeruid"]);
    if (out.m_peerUid <= 0)
        return false;
    //中转参数缺失时只是无法回落，会话本身仍可尝试直连
    ParseEndpoint(root["relayip"], root["relayport"], out.m_relay);
    return GetPositive(root, "connecttimeoutms", out.m_connectTimeoutMs)
        && GetPositive(root, "keepalivems", out.m_keepaliveMs)
        && GetPositive(root, "linktimeoutms", out.m_linkTimeoutMs);
}

std::string P2PSignalCodec::BuildSignal(const std::string& sessionId, const std::string& publicKey,
                                        const std::vector<P2PCandidate>& candidates)
{
    Json::Value data(Json::objectValue);
    data["pubkey"] = publicKey;
    Json::Value list;
    CandidatesToJson(candidates, list);
    data["candidates"] = list;
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = sessionId;
    v["data"] = data;
    return Compact(v);
}

bool P2PSignalCodec::ParseSignalForward(const std::string& body, P2PPeerSignal& out)
{
    out = P2PPeerSignal();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    if (!GetString(root, "p2psessionid", out.m_sessionId) || out.m_sessionId.empty())
        return false;
    out.m_fromUid = ToId(root["fromuid"]);
    const Json::Value& data = root["data"];
    if (!data.isObject() || !GetString(data, "pubkey", out.m_publicKey) || out.m_publicKey.empty())
        return false;
    CandidatesFromJson(data["candidates"], out.m_candidates);
    return true;
}

std::string P2PSignalCodec::BuildResult(const std::string& sessionId, bool ok, int path, int64_t elapsedMs,
                                        const std::string& pair, int natMapping)
{
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = sessionId;
    v["ok"] = ok;
    //服务端要求：成功时必须带路径，失败时不得带路径
    if (ok)
        v["path"] = path;
    v["elapsedms"] = (Json::Int64)((elapsedMs > 0) ? elapsedMs : 0);
    if (!pair.empty())
        v["pair"] = pair;
    if (natMapping != p2p_nat_mapping_unknown)
    {
        Json::Value nat(Json::objectValue);
        nat["samemapping"] = (natMapping == p2p_nat_mapping_independent);
        v["nat"] = nat;
    }
    return Compact(v);
}

std::string P2PSignalCodec::BuildClose(const std::string& sessionId, int reason, int errCode)
{
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = sessionId;
    v["reason"] = reason;
    v["errcode"] = errCode;
    return Compact(v);
}

bool P2PSignalCodec::ParseClose(const std::string& body, P2PCloseInfo& out)
{
    out = P2PCloseInfo();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    if (!GetString(root, "p2psessionid", out.m_sessionId) || out.m_sessionId.empty() || !root["reason"].isInt())
        return false;
    out.m_reason = root["reason"].asInt();
    out.m_errCode = root["errcode"].isInt() ? root["errcode"].asInt() : 0;
    return true;
}

std::string P2PSignalCodec::BuildDebugOpen(int64_t peerUid)
{
    Json::Value v(Json::objectValue);
    v["peeruid"] = std::to_string(peerUid);
    return Compact(v);
}

} // namespace p2p
