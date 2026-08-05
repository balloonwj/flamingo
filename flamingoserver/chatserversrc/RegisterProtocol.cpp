/**
 *  v2 注册与找回密码报文编解码实现，RegisterProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "RegisterProtocol.h"
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//去掉字符串首尾的空白字符（空格、制表、回车换行）。
//用于归一化用户手打的公司全称：后续要拿它做精确匹配，多敲的空格会让匹配落空。
//只处理 ASCII 空白，全角空格属于名字的一部分，不做删除。
static std::string trimSpaces(const std::string& src)
{
    const char* kWhitespace = " \t\r\n";
    std::string::size_type begin = src.find_first_not_of(kWhitespace);
    if (begin == std::string::npos)
        return std::string();

    std::string::size_type end = src.find_last_not_of(kWhitespace);
    return src.substr(begin, end - begin + 1);
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

bool RegisterProtocol::parseSendSmsCodeRequest(const std::string& json, std::string& phone, int& scene)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["phone"].isString())
        return false;

    phone = root["phone"].asString();
    if (phone.empty())
        return false;

    scene = root["scene"].isInt() ? root["scene"].asInt() : 0;
    return true;
}

bool RegisterProtocol::parseRegisterRequest(const std::string& json, RegisterRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    //不检查 username 与 smscode：前者由服务端自动生成、客户端发什么都不作数，
    //后者注册流程已不再使用。老客户端仍带着这两个字段发上来时，它们只是被忽略
    //的多余字段，不该因此把整个请求判为非法。
    if (!root["nickname"].isString()
        || !root["password"].isString() || !root["phone"].isString()
        || !root["company"].isString())
        return false;

    req.nickname = root["nickname"].asString();
    req.password = root["password"].asString();
    req.phone    = root["phone"].asString();
    req.company  = trimSpaces(root["company"].asString());

    if (req.nickname.empty() || req.password.empty()
        || req.phone.empty() || req.company.empty())
        return false;

    return true;
}

bool RegisterProtocol::parseResetPasswordRequest(const std::string& json, ResetPasswordRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["phone"].isString() || !root["smscode"].isString()
        || !root["newpassword"].isString())
        return false;

    req.phone       = root["phone"].asString();
    req.smscode     = root["smscode"].asString();
    req.newpassword = root["newpassword"].asString();

    if (req.phone.empty() || req.smscode.empty() || req.newpassword.empty())
        return false;

    return true;
}

std::string RegisterProtocol::buildRegisterResponse(int code, const std::string& msg,
                                                    int64_t userid, const std::string& username,
                                                    int64_t enterpriseId, const std::string& enterpriseName)
{
    Json::Value root;
    root["code"]           = code;
    root["msg"]            = msg;
    root["userid"]         = std::to_string(userid);
    root["username"]       = username;
    //企业ID与 userid 一样按 v2 约定用字符串编码，避免 JS 侧大整数精度丢失。
    root["enterpriseid"]   = std::to_string(enterpriseId);
    root["enterprisename"] = enterpriseName;
    return writeCompact(root);
}

std::string RegisterProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
