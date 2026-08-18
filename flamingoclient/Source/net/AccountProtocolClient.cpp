/**
 *  v2 账号相关报文编解码（客户端侧）实现，AccountProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "AccountProtocolClient.h"

//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出（与 LoginProtocolClient.cpp 同处理）。
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

std::string AccountProtocolClient::buildChangePasswordRequest(const std::string& oldPwdSha256Hex,
                                                              const std::string& newPwdSha256Hex)
{
    Json::Value root;
    root["oldpassword"] = oldPwdSha256Hex;
    root["newpassword"] = newPwdSha256Hex;
    return writeCompact(root);
}

bool AccountProtocolClient::parseChangePasswordResponse(const std::string& jsonbody,
                                                        ChangePasswordResponse& resp)
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
