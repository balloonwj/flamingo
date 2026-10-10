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

ProfileUpdateFields ProfileProtocolClient::diffProfileValues(const ProfileEditableValues& before,
                                                             const ProfileEditableValues& after)
{
    ProfileUpdateFields fields;
    fields.m_values       = after;
    fields.m_hasNickname  = (before.m_nickname != after.m_nickname);
    fields.m_hasSignature = (before.m_signature != after.m_signature);
    fields.m_hasGender    = (before.m_gender != after.m_gender);
    fields.m_hasBirthday  = (before.m_birthday != after.m_birthday);
    fields.m_hasAddress   = (before.m_address != after.m_address);
    fields.m_hasMail      = (before.m_mail != after.m_mail);
    fields.m_hasLandline  = (before.m_landline != after.m_landline);
    fields.m_hasPost      = (before.m_post != after.m_post);
    fields.m_hasPhoneVisible    = (before.m_phoneVisible != after.m_phoneVisible);
    fields.m_hasLandlineVisible = (before.m_landlineVisible != after.m_landlineVisible);
    return fields;
}

std::string ProfileProtocolClient::buildUpdateProfileRequest(const ProfileUpdateFields& fields)
{
    //空对象而不是 null：一项都不提交时仍是合法的 {}
    Json::Value root(Json::objectValue);
    //key 严格对齐服务端 parseUpdateProfileRequest（邮箱键名是 "mail"）；手机号不允许本人修改，从不放进请求
    const ProfileEditableValues& v = fields.m_values;
    if (fields.m_hasNickname)
        root["nickname"] = v.m_nickname;
    if (fields.m_hasSignature)
        root["signature"] = v.m_signature;
    if (fields.m_hasGender)
        root["gender"] = v.m_gender;
    if (fields.m_hasBirthday)
        root["birthday"] = v.m_birthday;
    if (fields.m_hasAddress)
        root["address"] = v.m_address;
    if (fields.m_hasMail)
        root["mail"] = v.m_mail;
    if (fields.m_hasLandline)
        root["landline"] = v.m_landline;
    if (fields.m_hasPost)
        root["post"] = v.m_post;
    if (fields.m_hasPhoneVisible)
        root["phonevisible"] = v.m_phoneVisible;
    if (fields.m_hasLandlineVisible)
        root["landlinevisible"] = v.m_landlineVisible;
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
