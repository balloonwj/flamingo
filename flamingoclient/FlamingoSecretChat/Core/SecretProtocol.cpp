/**
 *  密聊信令报文编解码的实现，见 SecretProtocol.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace secretchat {

namespace {

//64 位整数的十进制文本最多 20 个字符，留余量
const int kInt64TextChars = 32;

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//十进制字符串或整数 → 无符号 64 位；其它写法返回 0
uint64_t valueToU64(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty() || text.size() >= static_cast<size_t>(kInt64TextChars))
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<uint64_t>(_strtoui64(text.c_str(), NULL, 10));
    }
    if (v.isUInt64())
        return v.asUInt64();
    return 0;
}

//取整数字段；缺失时用缺省值
int intOr(const Json::Value& root, const char* key, int def)
{
    if (root.isMember(key) && root[key].isInt())
        return root[key].asInt();
    return def;
}

//取字符串字段；缺失或类型不对时为空串
std::string stringOr(const Json::Value& root, const char* key)
{
    if (root.isMember(key) && root[key].isString())
        return root[key].asString();
    return std::string();
}

} // namespace

std::string SecretProtocol::SessionIdText(uint64_t sessionId)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(sessionId));
    return std::string(buf);
}

std::string SecretProtocol::BuildRegister(int version, int protect)
{
    Json::Value v(Json::objectValue);
    v["version"] = version;
    v["protect"] = protect;
    return writeCompact(v);
}

std::string SecretProtocol::BuildInvite(int64_t peerId, const std::string& pubkey)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(peerId));
    Json::Value v(Json::objectValue);
    v["peerid"] = buf;
    v["pubkey"] = pubkey;
    return writeCompact(v);
}

std::string SecretProtocol::BuildRespond(uint64_t sessionId, bool accept, const std::string& pubkey)
{
    Json::Value v(Json::objectValue);
    v["sessionid"] = SessionIdText(sessionId);
    v["accept"] = accept ? 1 : 0;
    if (accept)
        v["pubkey"] = pubkey;
    return writeCompact(v);
}

std::string SecretProtocol::BuildSessionOnly(uint64_t sessionId)
{
    Json::Value v(Json::objectValue);
    v["sessionid"] = SessionIdText(sessionId);
    return writeCompact(v);
}

std::string SecretProtocol::BuildMessage(uint64_t sessionId, uint32_t seq, const std::string& nonce,
                                         const std::string& data)
{
    Json::Value v(Json::objectValue);
    v["sessionid"] = SessionIdText(sessionId);
    v["seq"] = seq;
    v["nonce"] = nonce;
    v["data"] = data;
    return writeCompact(v);
}

bool SecretProtocol::ParseCode(const std::string& json, int& code)
{
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("code") || !v["code"].isInt())
        return false;
    code = v["code"].asInt();
    return true;
}

bool SecretProtocol::ParseRegisterResponse(const std::string& json, int& code, bool& enabled)
{
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("code") || !v["code"].isInt())
        return false;
    code = v["code"].asInt();
    enabled = intOr(v, "enabled", 0) != 0;
    return true;
}

bool SecretProtocol::ParseInviteResponse(const std::string& json, int& code, uint64_t& sessionId, uint32_t& expireMs)
{
    sessionId = 0;
    expireMs = 0;
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("code") || !v["code"].isInt())
        return false;
    code = v["code"].asInt();
    if (code != 0)
        return true;
    sessionId = v.isMember("sessionid") ? valueToU64(v["sessionid"]) : 0;
    if (v.isMember("expirems") && v["expirems"].isUInt())
        expireMs = v["expirems"].asUInt();
    return sessionId != 0;
}

bool SecretProtocol::ParseMessageResponse(const std::string& json, int& code, uint32_t& seq)
{
    seq = 0;
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("code") || !v["code"].isInt())
        return false;
    code = v["code"].asInt();
    if (v.isMember("seq") && v["seq"].isUInt())
        seq = v["seq"].asUInt();
    return true;
}

bool SecretProtocol::ParseInviteNotify(const std::string& json, InviteNotify& out)
{
    out = InviteNotify();
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("sessionid") || !v.isMember("fromid"))
        return false;
    out.sessionId = valueToU64(v["sessionid"]);
    out.fromId = static_cast<int64_t>(valueToU64(v["fromid"]));
    out.pubkey = stringOr(v, "pubkey");
    if (v.isMember("expirems") && v["expirems"].isUInt())
        out.expireMs = v["expirems"].asUInt();
    out.peerProtect = intOr(v, "peerprotect", 0);
    return out.sessionId != 0 && out.fromId > 0 && !out.pubkey.empty();
}

bool SecretProtocol::ParseStateNotify(const std::string& json, StateNotify& out)
{
    out = StateNotify();
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("sessionid") || !v.isMember("state") || !v["state"].isInt())
        return false;
    out.sessionId = valueToU64(v["sessionid"]);
    out.peerId = v.isMember("peerid") ? static_cast<int64_t>(valueToU64(v["peerid"])) : 0;
    out.state = v["state"].asInt();
    out.reason = intOr(v, "reason", 0);
    out.pubkey = stringOr(v, "pubkey");
    out.peerProtect = intOr(v, "peerprotect", 0);
    return out.sessionId != 0;
}

bool SecretProtocol::ParseMessageNotify(const std::string& json, MessageNotify& out)
{
    out = MessageNotify();
    Json::Value v;
    if (!parseObject(json, v) || !v.isMember("sessionid") || !v.isMember("seq") || !v["seq"].isUInt())
        return false;
    out.sessionId = valueToU64(v["sessionid"]);
    out.seq = v["seq"].asUInt();
    out.nonce = stringOr(v, "nonce");
    out.data = stringOr(v, "data");
    if (v.isMember("servertime") && v["servertime"].isInt64())
        out.serverTime = v["servertime"].asInt64();
    return out.sessionId != 0 && out.seq > 0 && !out.nonce.empty() && !out.data.empty();
}

} // namespace secretchat
