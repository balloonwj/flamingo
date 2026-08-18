/**
 *  v2 注册报文编解码（客户端侧）实现，RegisterProtocolClient.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "RegisterProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"

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

std::string RegisterProtocolClient::buildRegisterRequest(const RegisterRequestParams& params)
{
    //不发 username 与 smscode：登录账号由服务端自动生成，短信验证码只用于
    //找回密码，服务端这两个字段都不看。
    Json::Value root;
    root["nickname"] = params.nickname;
    root["password"] = params.password;
    root["phone"]    = params.phone;
    root["company"]  = params.company;
    return writeCompact(root);
}

bool RegisterProtocolClient::parseRegisterResponse(const std::string& jsonbody, RegisterResponse& resp)
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

    //失败应答到此为止：服务端的失败应答只有 code 与 msg 两个字段
    if (resp.code != 0)
        return true;

    //成功应答：解析新账号与其企业归属
    resp.userid         = jsonToInt64(root["userid"]);
    resp.username       = root["username"].isString()       ? root["username"].asString()       : std::string();
    resp.enterpriseid   = jsonToInt64(root["enterpriseid"]);
    resp.enterprisename = root["enterprisename"].isString() ? root["enterprisename"].asString() : std::string();

    return true;
}

} // namespace net
