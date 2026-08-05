/**
 *  v2「我的收藏」报文编解码（客户端侧）实现，FavoriteProtocolClient.cpp
 *  balloonwj@qq.com   2026-08-03
 */
#include "FavoriteProtocolClient.h"
#include <stdlib.h>                     // strtoll
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（cid 等）以字符串编码进 JSON，避免精度丢失。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string FavoriteProtocolClient::buildSetFavoriteRequest(int ctype, int64_t cid, bool favorite)
{
    Json::Value root;
    root["ctype"] = ctype;
    //64 位 ID 一律以字符串编码，与服务端 FavoriteProtocol::parseSetFavoriteRequest 对齐
    root["cid"] = std::to_string(cid);
    //服务端读的是整型 0/1（parseSetFavoriteRequest 里 root["favorite"].isInt()），
    //这里必须写成整数而不是 JSON bool —— 写成 true/false 会被判为格式非法。
    root["favorite"] = favorite ? 1 : 0;
    return writeCompact(root);
}

bool FavoriteProtocolClient::parseSetFavoriteResponse(const std::string& jsonbody,
                                                      SetFavoriteResult& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 是判定成败的唯一依据，缺了就没法用，直接判失败。
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //ctype / cid / favorite 由服务端原样回带，用来把应答对上是哪一条请求。
    //即使缺失也不判整条应答失败 —— code 已经拿到了，调用方至少知道成没成。
    out.m_ctype    = root["ctype"].isInt() ? root["ctype"].asInt() : 0;
    out.m_cid      = jsonToInt64(root["cid"]);
    out.m_favorite = root["favorite"].isInt() ? root["favorite"].asInt() : 0;

    return true;
}

} // namespace net
