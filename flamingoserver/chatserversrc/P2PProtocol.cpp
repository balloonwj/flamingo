/**
 *  v2 P2P 连接信令编解码（服务端侧）实现，P2PProtocol.cpp
 *
 *  取字段的内部辅助函数一律先判类型再取值：JSON 是客户端可以任意伪造的输入，jsoncpp 的 asString() / asInt64()
 *  在类型不符时会抛异常，而本模块在会话层的分发路径上被调用，一旦抛出就会打断整条连接的收包流程。
 *  任何不合契约的输入都以返回失败的方式安全处理。做法与 FileShareProtocol.cpp 相同。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include "../jsoncpp1.9.0/json.h"

namespace {

//64 位整数的十进制文本最多 20 个字符（含负号），留余量
const int kInt64TextChars = 32;

//2304 的 path 合法区间（ok 为真时）
const int kFirstPath = p2p_path_lan;
const int kLastPath  = p2p_path_relay;

//2305 的 reason 合法区间
const int kFirstCloseReason = p2p_close_business_end;
const int kLastCloseReason  = p2p_close_self_logout;

//把 Json::Value 序列化为紧凑（无缩进、无换行、冒号两侧无空格）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 64 位 ID 写成十进制字符串（v2 惯例，避免客户端按双精度解析丢精度）
std::string idToString(int64_t id)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
    return std::string(buf);
}

//从一个 JSON 值里取 64 位 ID：十进制字符串与整数两种写法都接受；其余类型、空串、非数字返回 0
int64_t valueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(atoll(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//取出必填字符串字段：缺失、不是字符串、空串、超过 maxBytes 字节一律返回 false
bool getRequiredString(const Json::Value& root, const char* key, size_t maxBytes, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return !out.empty() && out.size() <= maxBytes;
}

//取出可选字符串字段：缺失按空串处理；存在但不是字符串、或超过 maxBytes 字节返回 false
bool getOptionalString(const Json::Value& root, const char* key, size_t maxBytes, std::string& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    if (!root[key].isString())
        return false;
    out = root[key].asString();
    return out.size() <= maxBytes;
}

//取出必填 32 位整数字段；缺失或不是整数返回 false。jsoncpp 把 1.5 判为非整数，把 "1" 判为字符串，都会失败
bool getRequiredInt(const Json::Value& root, const char* key, int& out)
{
    if (!root.isMember(key) || !root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取出可选 32 位整数字段；缺失按 defaultValue 处理，存在但不是整数返回 false
bool getOptionalInt(const Json::Value& root, const char* key, int defaultValue, int& out)
{
    out = defaultValue;
    if (!root.isMember(key))
        return true;
    if (!root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//把结果码与说明写进对象（各应答共用）
void writeCodeAndMsg(Json::Value& v, int code, const std::string& msg)
{
    v["code"] = code;
    v["msg"] = msg;
}

} // namespace

bool P2PProtocol::parseRegisterRequest(const std::string& json, int& version, std::vector<std::string>& purposes)
{
    purposes.clear();
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!getRequiredInt(root, "version", version))
        return false;

    //purposes 缺省：第 5 期之前的报到包体，视为不支持任何业务用途
    if (!root.isMember("purposes"))
        return true;
    const Json::Value& list = root["purposes"];
    if (!list.isArray() || list.size() > kP2PMaxPurposes)
        return false;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& item = list[i];
        if (!item.isString())
            return false;
        const std::string name = item.asString();
        if (name.empty() || name.size() > kP2PPurposeMaxBytes)
            return false;
        purposes.push_back(name);
    }
    return true;
}

std::string P2PProtocol::buildRegisterResponse(int code, const std::string& msg, bool enabled,
                                               const std::vector<P2PProbeAddress>& probes)
{
    Json::Value v(Json::objectValue);
    writeCodeAndMsg(v, code, msg);
    v["enabled"] = enabled;
    Json::Value list(Json::arrayValue);
    for (size_t i = 0; i < probes.size(); ++i)
    {
        Json::Value item(Json::objectValue);
        item["ip"] = probes[i].m_ip;
        item["port"] = probes[i].m_port;
        list.append(item);
    }
    v["probe"] = list;
    return writeCompact(v);
}

std::string P2PProtocol::buildSessionNotify(const P2PSessionNotify& notify)
{
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = notify.m_sessionId;
    v["role"] = notify.m_role;
    v["purpose"] = notify.m_purpose;
    v["featuresessionid"] = notify.m_featureSessionId;
    v["peeruid"] = idToString(notify.m_peerUid);
    v["peerdeviceid"] = notify.m_peerDeviceId;
    v["relayip"] = notify.m_relayIp;
    v["relayport"] = notify.m_relayPort;
    v["token"] = notify.m_token;
    v["connecttimeoutms"] = notify.m_connectTimeoutMs;
    v["keepalivems"] = notify.m_keepaliveMs;
    v["linktimeoutms"] = notify.m_linkTimeoutMs;
    return writeCompact(v);
}

int P2PProtocol::parseSignalRequest(const std::string& json, P2PSignalRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return error_code_bad_request;
    if (!getRequiredString(root, "p2psessionid", kP2PSessionIdMaxBytes, req.m_sessionId))
        return error_code_bad_request;
    if (!root.isMember("data") || !root["data"].isObject())
        return error_code_bad_request;

    //data 对服务端不透明：只按序列化后的长度限制它，内容原样保存、原样转发
    req.m_data = writeCompact(root["data"]);
    if (req.m_data.size() > kP2PSignalDataMaxBytes)
        return error_code_p2p_signal_too_large;
    return error_code_ok;
}

std::string P2PProtocol::buildSignalForward(const std::string& sessionId, int64_t fromUid, const std::string& dataJson)
{
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = sessionId;
    v["fromuid"] = idToString(fromUid);
    Json::Value data;
    if (!parseObject(dataJson, data))
        data = Json::Value(Json::objectValue);
    v["data"] = data;
    return writeCompact(v);
}

bool P2PProtocol::parseResultReport(const std::string& json, P2PResultReport& report)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!getRequiredString(root, "p2psessionid", kP2PSessionIdMaxBytes, report.m_sessionId))
        return false;
    if (!root.isMember("ok") || !root["ok"].isBool())
        return false;
    report.m_ok = root["ok"].asBool();

    //成功时必须给出 1~3 的路径；失败时不得带路径（没有选定任何路径）
    if (report.m_ok)
    {
        if (!getRequiredInt(root, "path", report.m_path))
            return false;
        if (report.m_path < kFirstPath || report.m_path > kLastPath)
            return false;
    }
    else
    {
        if (root.isMember("path"))
            return false;
        report.m_path = 0;
    }

    if (!getOptionalInt(root, "elapsedms", 0, report.m_elapsedMs) || report.m_elapsedMs < 0)
        return false;
    if (!getOptionalString(root, "pair", kP2PPairTextMaxBytes, report.m_pair))
        return false;

    //nat 只供日志：可省略；给了就必须是对象，序列化后限长
    report.m_nat.clear();
    if (root.isMember("nat"))
    {
        if (!root["nat"].isObject())
            return false;
        report.m_nat = writeCompact(root["nat"]);
        if (report.m_nat.size() > kP2PNatTextMaxBytes)
            return false;
    }
    return true;
}

bool P2PProtocol::parseClose(const std::string& json, P2PCloseMessage& msg)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!getRequiredString(root, "p2psessionid", kP2PSessionIdMaxBytes, msg.m_sessionId))
        return false;
    if (!getRequiredInt(root, "reason", msg.m_reason))
        return false;
    if (msg.m_reason < kFirstCloseReason || msg.m_reason > kLastCloseReason)
        return false;
    return getOptionalInt(root, "errcode", 0, msg.m_errCode);
}

std::string P2PProtocol::buildClose(const P2PCloseMessage& msg)
{
    Json::Value v(Json::objectValue);
    v["p2psessionid"] = msg.m_sessionId;
    v["reason"] = msg.m_reason;
    v["errcode"] = msg.m_errCode;
    return writeCompact(v);
}

bool P2PProtocol::parseDebugOpenRequest(const std::string& json, int64_t& peerUid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root.isMember("peeruid"))
        return false;
    peerUid = valueToId(root["peeruid"]);
    return peerUid > 0;
}

std::string P2PProtocol::buildDebugOpenResponse(int code, const std::string& msg, const std::string& sessionId)
{
    Json::Value v(Json::objectValue);
    writeCodeAndMsg(v, code, msg);
    if (!sessionId.empty())
        v["p2psessionid"] = sessionId;
    return writeCompact(v);
}

std::string P2PProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value v(Json::objectValue);
    writeCodeAndMsg(v, code, msg);
    return writeCompact(v);
}
