/**
 *  v2 登录报文编解码（客户端侧）实现，LoginProtocolClient.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "LoginProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

const char* const kFeatureFileShare = "fileshare";
const char* const kFeatureInviteConfirm = "inviteconfirm";
const char* const kFeatureGroupCall = "groupcall";
const char* const kFeatureCaption = "caption";
const char* const kFeatureCloudRecord = "cloudrecord";
const char* const kFeatureSecretChat = "secretchat";
const char* const kFeatureThreadGroup = "threadgroup";
const char* const kFeatureBot = "bot";
const char* const kFeatureBotGroup = "botgroup";
const char* const kFeatureBotPersonal = "botpersonal";
const char* const kFeatureBotGroupExternal = "botgroupexternal";

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

//features 对象：功能名 → 0 / 1；不是对象时为空，不是整数的项略去
static std::map<std::string, int> parseFeatureObject(const Json::Value& value)
{
    std::map<std::string, int> out;
    if (!value.isObject())
        return out;
    const Json::Value::Members names = value.getMemberNames();
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (value[names[i]].isInt())
            out[names[i]] = value[names[i]].asInt();
    }
    return out;
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
        resp.profile.landline     = p["landline"].isString()  ? p["landline"].asString()     : std::string();
        resp.profile.post         = p["post"].isString()      ? p["post"].asString()         : std::string();
        resp.profile.enterpriseid = jsonToInt64(p["enterpriseid"]);
        //公开范围（2026-10-06 起下发）：缺字段时保留构造时的默认值，与服务端两列的默认值一致
        if (p["phonevisible"].isInt())
            resp.profile.phonevisible = p["phonevisible"].asInt();
        if (p["landlinevisible"].isInt())
            resp.profile.landlinevisible = p["landlinevisible"].asInt();
    }

    //功能开关：老服务端没有这个字段，按全部关闭处理（features 留空）
    resp.features = parseFeatureObject(root["features"]);
    return true;
}

bool LoginProtocolClient::parseFeaturesNotify(const std::string& jsonbody, FeaturesNotify& notify)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;
    notify.m_features = parseFeatureObject(root);
    return true;
}

bool LoginProtocolClient::isFeatureOn(const std::map<std::string, int>& features, const char* feature)
{
    if (feature == NULL)
        return false;
    std::map<std::string, int>::const_iterator it = features.find(feature);
    return it != features.end() && it->second == 1;
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
