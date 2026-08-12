/**
 *  v2「特别关注」报文编解码实现，StarredProtocol.cpp
 *  设计与约定见 StarredProtocol.h。
 *  balloonwj@qq.com   2026-08-12
 */
#include "StarredProtocol.h"
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

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

bool StarredProtocol::parseSetStarredRequest(const std::string& json, SetStarredRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.target = getStringId(root, "target");
    if (req.target <= 0)
        return false;

    if (!root["starred"].isInt())
        return false;
    req.starred = root["starred"].asInt();

    //只有"标记"与"取消"两种意图，其余取值一律判非法，免得把 2、-1 之类
    //稀里糊涂当成"标记"处理。
    if (req.starred != 0 && req.starred != 1)
        return false;

    return true;
}

std::string StarredProtocol::buildSetStarredResponse(int code, const std::string& msg,
                                                     int64_t target, int starred)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["target"]  = std::to_string(target);   //64 位 ID 一律以字符串编码进 JSON
    root["starred"] = starred;
    return writeCompact(root);
}
