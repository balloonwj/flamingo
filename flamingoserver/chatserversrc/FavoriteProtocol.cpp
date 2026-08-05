/**
 *  v2「我的收藏」报文编解码实现，FavoriteProtocol.cpp
 *  balloonwj@qq.com   2026-08-03
 */
#include "FavoriteProtocol.h"
#include "Protocol.h"                 //conversation_type（ctype 取值的单一事实来源）
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

bool FavoriteProtocol::parseSetFavoriteRequest(const std::string& json, SetFavoriteRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!root["ctype"].isInt())
        return false;
    req.ctype = root["ctype"].asInt();

    //收藏只认联系人与群两种。conversation_service(3) 是"服务号会话"，
    //而服务号作为<u>联系人</u>被收藏时走的是 ctype=1，故这里不接受 3。
    if (req.ctype != conversation_single && req.ctype != conversation_group)
        return false;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    if (!root["favorite"].isInt())
        return false;
    req.favorite = root["favorite"].asInt();

    //只有"加入"与"移出"两种意图，其余取值一律判非法，免得把 2、-1 之类
    //稀里糊涂当成"加入"处理。
    if (req.favorite != 0 && req.favorite != 1)
        return false;

    return true;
}

std::string FavoriteProtocol::buildSetFavoriteResponse(int code, const std::string& msg,
                                                       int ctype, int64_t cid, int favorite)
{
    Json::Value root;
    root["code"]     = code;
    root["msg"]      = msg;
    root["ctype"]    = ctype;
    root["cid"]      = std::to_string(cid);   //64 位 ID 一律以字符串编码进 JSON
    root["favorite"] = favorite;
    return writeCompact(root);
}
