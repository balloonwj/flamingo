/**
 *  v2 登录报文编解码实现，LoginProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "LoginProtocol.h"
#include <vector>
#include "../jsoncpp1.9.0/json.h"

//presence 字段缺省值：1 即 presence_online（在线）
constexpr int DEFAULT_PRESENCE = 1;

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把点分版本号字符串解析为各段整数；非数字、非点的字符忽略
static void parseVersionSegments(const std::string& v, std::vector<int>& out)
{
    int cur = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        char c = v[i];
        if (c >= '0' && c <= '9')
            cur = cur * 10 + (c - '0');
        else if (c == '.')
        {
            out.push_back(cur);
            cur = 0;
        }
    }
    out.push_back(cur);
}

bool LoginProtocol::parseLoginRequest(const std::string& jsonbody, LoginRequest& req)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //必填字段：clientversion / account / password / clienttype
    if (!root["clientversion"].isString()
        || !root["account"].isString()
        || !root["password"].isString()
        || !root["clienttype"].isInt())
        return false;

    req.clientversion = root["clientversion"].asString();
    req.account       = root["account"].asString();
    req.password      = root["password"].asString();
    req.clienttype    = root["clienttype"].asInt();

    //账号与口令哈希不得为空
    if (req.account.empty() || req.password.empty())
        return false;

    //选填字段，缺失或类型不符时取默认值
    req.platform   = root["platform"].isInt() ? root["platform"].asInt() : req.clienttype;
    req.presence   = root["presence"].isInt() ? root["presence"].asInt() : DEFAULT_PRESENCE;
    req.deviceid   = root["deviceid"].isString() ? root["deviceid"].asString() : "";
    req.devicename = root["devicename"].isString() ? root["devicename"].asString() : "";

    return true;
}

std::string LoginProtocol::buildLoginSuccessResponse(int64_t userid,
                                                     const LoginProfile& profile,
                                                     int64_t servertime)
{
    Json::Value root;
    root["code"]       = 0;
    root["msg"]        = "ok";
    //64 位 ID 按 v2 约定以字符串编码，避免 JSON 数值精度丢失
    root["userid"]     = std::to_string(userid);
    root["servertime"] = static_cast<Json::Int64>(servertime);

    Json::Value p;
    p["usertype"]     = profile.usertype;
    p["username"]     = profile.username;
    p["nickname"]     = profile.nickname;
    p["facetype"]     = profile.facetype;
    p["customface"]   = profile.customface;
    p["gender"]       = profile.gender;
    p["birthday"]     = profile.birthday;
    p["signature"]    = profile.signature;
    p["address"]      = profile.address;
    p["phonenumber"]  = profile.phonenumber;
    p["mail"]         = profile.mail;
    p["enterpriseid"] = std::to_string(profile.enterpriseid);
    root["profile"]   = p;

    return writeCompact(root);
}

std::string LoginProtocol::buildLoginFailResponse(int32_t code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

bool LoginProtocol::isVersionAtLeast(const std::string& ver, const std::string& minver)
{
    std::vector<int> a;
    std::vector<int> b;
    parseVersionSegments(ver, a);
    parseVersionSegments(minver, b);

    size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
    {
        int av = i < a.size() ? a[i] : 0;
        int bv = i < b.size() ? b[i] : 0;
        if (av != bv)
            return av > bv;
    }
    //逐段完全相等，视为满足"不低于"
    return true;
}

bool LoginProtocol::parseKickDeviceRequest(const std::string& json, int& clienttype)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::Value root;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    if (!root["clienttype"].isInt())
        return false;
    clienttype = root["clienttype"].asInt();
    return clienttype > 0;
}

std::string LoginProtocol::buildGetDevicesResponse(int code, const std::string& msg,
                                                   const std::vector<DeviceInfo>& devices)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < devices.size(); ++i)
    {
        Json::Value jo;
        jo["clienttype"] = devices[i].clienttype;
        jo["status"]     = devices[i].status;
        jo["iscurrent"]  = devices[i].iscurrent;
        arr.append(jo);
    }
    root["devices"] = arr;
    return writeCompact(root);
}

std::string LoginProtocol::buildDeviceChangedNotify(int changetype, int clienttype)
{
    Json::Value root;
    root["changetype"] = changetype;
    root["clienttype"] = clienttype;
    return writeCompact(root);
}

std::string LoginProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
