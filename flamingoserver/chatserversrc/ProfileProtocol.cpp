/**
 *  v2 账号资料报文编解码实现，ProfileProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "ProfileProtocol.h"
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

//把 UserProfile 各字段写入一个 Json::Value 对象（get_user_info 与 notify 共用）
static Json::Value makeProfileJson(const UserProfile& p)
{
    Json::Value jp;
    jp["userid"]       = std::to_string(p.userid);
    jp["usertype"]     = p.usertype;
    jp["username"]     = p.username;
    jp["nickname"]     = p.nickname;
    jp["facetype"]     = p.facetype;
    jp["customface"]   = p.customface;
    jp["gender"]       = p.gender;
    jp["birthday"]     = p.birthday;
    jp["signature"]    = p.signature;
    jp["address"]      = p.address;
    jp["phonenumber"]  = p.phonenumber;
    jp["mail"]         = p.mail;
    jp["enterpriseid"] = std::to_string(p.enterpriseid);
    jp["presence"]     = p.presence;
    jp["clienttype"]   = p.clienttype;
    return jp;
}

bool ProfileProtocol::parseGetUserInfoRequest(const std::string& json, int64_t& userid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["userid"].isString())
        return false;

    userid = static_cast<int64_t>(atoll(root["userid"].asString().c_str()));
    return userid > 0;
}

bool ProfileProtocol::parseUpdateProfileRequest(const std::string& json, UpdateProfileRequest& req)
{
    req.has_nickname  = false;
    req.has_signature = false;
    req.has_gender    = false;
    req.has_birthday  = false;
    req.has_address   = false;
    req.has_mail      = false;

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //字段全可选：出现且类型正确才纳入更新，其余忽略
    if (root["nickname"].isString())
    {
        req.has_nickname = true;
        req.nickname = root["nickname"].asString();
    }
    if (root["signature"].isString())
    {
        req.has_signature = true;
        req.signature = root["signature"].asString();
    }
    if (root["gender"].isInt())
    {
        req.has_gender = true;
        req.gender = root["gender"].asInt();
    }
    if (root["birthday"].isInt())
    {
        req.has_birthday = true;
        req.birthday = root["birthday"].asInt();
    }
    if (root["address"].isString())
    {
        req.has_address = true;
        req.address = root["address"].asString();
    }
    if (root["mail"].isString())
    {
        req.has_mail = true;
        req.mail = root["mail"].asString();
    }

    return true;
}

bool ProfileProtocol::parseChangePasswordRequest(const std::string& json, ChangePasswordRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["oldpassword"].isString() || !root["newpassword"].isString())
        return false;

    req.oldpassword = root["oldpassword"].asString();
    req.newpassword = root["newpassword"].asString();
    if (req.oldpassword.empty() || req.newpassword.empty())
        return false;

    return true;
}

bool ProfileProtocol::parseChangeAvatarRequest(const std::string& json, ChangeAvatarRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["facetype"].isInt())
        return false;

    req.facetype = root["facetype"].asInt();
    req.customface = root["customface"].isString() ? root["customface"].asString() : "";
    return true;
}

bool ProfileProtocol::parseChangePresenceRequest(const std::string& json, ChangePresenceRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["presence"].isInt())
        return false;

    req.presence = root["presence"].asInt();
    return true;
}

std::string ProfileProtocol::buildGetUserInfoResponse(int code, const std::string& msg,
                                                      const UserProfile& profile)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["profile"] = makeProfileJson(profile);
    return writeCompact(root);
}

std::string ProfileProtocol::buildUserInfoChangedNotify(const UserProfile& profile)
{
    Json::Value root;
    root["userid"]  = std::to_string(profile.userid);
    root["profile"] = makeProfileJson(profile);
    return writeCompact(root);
}

std::string ProfileProtocol::buildPresenceChangedNotify(int64_t userid, int presence, int clienttype)
{
    Json::Value root;
    root["userid"]     = std::to_string(userid);
    root["presence"]   = presence;
    root["clienttype"] = clienttype;
    return writeCompact(root);
}

std::string ProfileProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
