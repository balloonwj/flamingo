/**
 *  v2 账号资料报文编解码（客户端侧）实现，ProfileProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "ProfileProtocolClient.h"

#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string ProfileProtocolClient::buildUpdateSignatureRequest(const std::string& signatureUtf8)
{
    Json::Value root;
    //只放 signature 一项：服务端按"字段是否出现"决定更新项，故此请求只改签名。
    root["signature"] = signatureUtf8;
    return writeCompact(root);
}

std::string ProfileProtocolClient::buildUpdateProfileRequest(const std::string& nicknameUtf8,
                                                             const std::string& signatureUtf8,
                                                             int gender, long birthday,
                                                             const std::string& addressUtf8,
                                                             const std::string& mailUtf8)
{
    Json::Value root;
    //6 字段全放；key 严格对齐服务端 parseUpdateProfileRequest（邮箱键名是 "mail"）。
    //birthday 范围 YYYYMMDD（≤ 99991231）落在 32 位 int 内，强转 int 即可。
    root["nickname"]  = nicknameUtf8;
    root["signature"] = signatureUtf8;
    root["gender"]    = gender;
    root["birthday"]  = (int)birthday;
    root["address"]   = addressUtf8;
    root["mail"]      = mailUtf8;
    return writeCompact(root);
}

std::string ProfileProtocolClient::buildChangeAvatarRequest(int facetype,
                                                            const std::string& customfaceUtf8)
{
    Json::Value root;
    //facetype + customface 两项即可：服务端据此落库并广播资料变更通知。
    root["facetype"]   = facetype;
    root["customface"] = customfaceUtf8;
    return writeCompact(root);
}

bool ProfileProtocolClient::parseUpdateProfileResponse(const std::string& jsonbody,
                                                       UpdateProfileResponse& resp)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    resp.code = root["code"].asInt();
    resp.msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

std::string ProfileProtocolClient::buildChangePresenceRequest(int presence)
{
    Json::Value root;
    //请求体只含目标在线状态一项：{"presence":N}
    root["presence"] = presence;
    return writeCompact(root);
}

bool ProfileProtocolClient::parseChangePresenceResponse(const std::string& jsonbody,
                                                        ChangePresenceResponse& resp)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    resp.code = root["code"].asInt();
    resp.msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

} // namespace net
