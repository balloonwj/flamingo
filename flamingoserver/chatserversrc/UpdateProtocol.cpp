/**
 *  v2 检查客户端更新报文编解码实现，UpdateProtocol.cpp
 *  balloonwj@qq.com   2026-08-11
 */
#include "UpdateProtocol.h"
#include "Protocol.h"                 //enum platform_type / enum error_code
#include "../jsoncpp1.9.0/json.h"

//应答里成功时固定下发的提示串，与其它协议模块保持一致
static const char* const RESPONSE_MSG_OK = "ok";

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool UpdateProtocol::parseCheckUpdateRequest(const std::string& jsonbody, CheckUpdateRequest& req)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //version 是唯一的必填字段：没有它就无从判断该不该升级
    if (!root["version"].isString())
        return false;

    const std::string version = root["version"].asString();
    if (version.empty())
        return false;

    //platform 选填：不带则按 PC 处理；带了就必须是合法的 platform_type 取值
    int32_t platform = platform_pc;
    if (root.isMember("platform"))
    {
        if (!root["platform"].isInt())
            return false;

        platform = static_cast<int32_t>(root["platform"].asInt());
        if (platform < platform_pc || platform > platform_web)
            return false;
    }

    req.version  = version;
    req.platform = platform;
    return true;
}

std::string UpdateProtocol::buildHasUpdateResponse(const ClientVersionInfo& info,
                                                   bool forced,
                                                   const std::string& downloadToken,
                                                   const std::string& host,
                                                   int port,
                                                   bool tls)
{
    Json::Value root;
    root["code"]          = error_code_ok;
    root["msg"]           = RESPONSE_MSG_OK;
    root["hasupdate"]     = true;
    root["latestversion"] = info.version;
    root["minversion"]    = info.minVersion;
    root["forced"]        = forced;
    root["releasenote"]   = info.releaseNote;
    root["md5"]           = info.packageMd5;
    //包大小显式按 64 位下发：升级包超过 2GiB 时 32 位会溢出成负数
    root["size"]          = static_cast<Json::Int64>(info.packageSize);
    root["packagename"]   = info.packageName;
    root["downloadtoken"] = downloadToken;

    //server 子对象的字段与 cmd_request_download 应答保持同构，客户端解析代码可复用
    Json::Value server;
    server["host"] = host;
    server["port"] = port;
    server["tls"]  = tls;
    root["server"] = server;

    return writeCompact(root);
}

std::string UpdateProtocol::buildNoUpdateResponse(const std::string& latestVersion)
{
    Json::Value root;
    root["code"]      = error_code_ok;
    root["msg"]       = RESPONSE_MSG_OK;
    root["hasupdate"] = false;

    //服务器一个版本都没发布过时不下发该字段，免得客户端把空串显示成"最新版本 "
    if (!latestVersion.empty())
        root["latestversion"] = latestVersion;

    return writeCompact(root);
}

std::string UpdateProtocol::buildSimpleResponse(int32_t code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
