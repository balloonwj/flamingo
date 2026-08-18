/**
 *  v2 登录报文编解码（客户端侧）实现，LoginProtocolClient.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "LoginProtocolClient.h"
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
//v2 约定 64 位 ID（userid / enterpriseid）以字符串编码进 JSON，以避免
//JSON 数值精度丢失，故解析时需经本函数还原成整型。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

std::string LoginProtocolClient::buildLoginRequest(const LoginRequestParams& params)
{
    Json::Value root;
    root["clientversion"] = params.clientversion;
    root["account"]       = params.account;
    root["password"]      = params.password;
    root["clienttype"]    = params.clienttype;
    root["presence"]      = params.presence;
    root["deviceid"]      = params.deviceid;
    root["devicename"]    = params.devicename;
    return writeCompact(root);
}

bool LoginProtocolClient::parseLoginResponse(const std::string& jsonbody, LoginResponse& resp)
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

    //失败应答到此为止，userid / servertime / profile 不再解析
    if (resp.code != 0)
        return true;

    //成功应答：解析 userid、servertime 与 profile
    resp.userid     = jsonToInt64(root["userid"]);
    resp.servertime = root["servertime"].isNumeric() ? root["servertime"].asInt64() : 0;

    const Json::Value& p = root["profile"];
    if (p.isObject())
    {
        resp.profile.usertype     = p["usertype"].isInt()     ? p["usertype"].asInt()        : 0;
        resp.profile.username     = p["username"].isString()  ? p["username"].asString()     : std::string();
        resp.profile.nickname     = p["nickname"].isString()  ? p["nickname"].asString()     : std::string();
        resp.profile.facetype     = p["facetype"].isInt()     ? p["facetype"].asInt()        : 0;
        resp.profile.customface   = p["customface"].isString()? p["customface"].asString()   : std::string();
        resp.profile.gender       = p["gender"].isInt()       ? p["gender"].asInt()          : 0;
        resp.profile.birthday     = p["birthday"].isInt()     ? p["birthday"].asInt()        : 0;
        resp.profile.signature    = p["signature"].isString() ? p["signature"].asString()    : std::string();
        resp.profile.address      = p["address"].isString()   ? p["address"].asString()      : std::string();
        resp.profile.phonenumber  = p["phonenumber"].isString()? p["phonenumber"].asString() : std::string();
        resp.profile.mail         = p["mail"].isString()      ? p["mail"].asString()         : std::string();
        resp.profile.enterpriseid = jsonToInt64(p["enterpriseid"]);
    }

    return true;
}

bool LoginProtocolClient::parseKickoutNotify(const std::string& jsonbody, KickoutNotify& notify)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //reason 缺失或非整型时留 0（未知原因），不当作解析失败：见头文件说明。
    notify.m_reason = root["reason"].isInt() ? root["reason"].asInt() : 0;
    notify.m_msg    = root["msg"].isString() ? root["msg"].asString() : std::string();

    return true;
}

} // namespace net
