/**
 *  v2「联系人标签」报文编解码（客户端侧）实现，ContactTagProtocolClient.cpp
 *  设计与约定见 ContactTagProtocolClient.h。
 *  balloonwj@qq.com   2026-08-14
 */
#include "ContactTagProtocolClient.h"
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
//v2 约定 64 位 ID（userid 等）以字符串编码进 JSON，避免精度丢失。
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

std::string ContactTagProtocolClient::buildSetContactTagsRequest(
    int64_t userid, const std::vector<std::string>& tags)
{
    Json::Value root;
    //64 位 ID 一律以字符串编码，与服务端 ContactTagProtocol::parseSetContactTagsRequest 对齐
    root["userid"] = std::to_string(userid);

    //tags 必须<u>无条件写出</u>，哪怕是空数组：服务端把"字段缺失"判为报文非法而不是
    //按空数组处理（漏发字段会把用户贴好的标签整份清空，两者语义差得太远）。
    //Json::arrayValue 显式指定类型，否则空数组会被序列化成 null。
    Json::Value jtags(Json::arrayValue);
    for (size_t i = 0; i < tags.size(); ++i)
    {
        jtags.append(tags[i]);
    }
    root["tags"] = jtags;

    return writeCompact(root);
}

bool ContactTagProtocolClient::parseSetContactTagsResponse(const std::string& jsonbody,
                                                           SetContactTagsResult& out)
{
    //先复位出参：解析中途返回 false 时，调用方即便误用了 out 也拿不到上一次的残留值。
    out = SetContactTagsResult();

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

    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();

    //userid 缺失时留 0：老服务端可能不回带它。调用方拿到 0 就无从判断这条应答对应
    //哪一次请求，此时宁可不回滚，也不要退错人的标签。
    out.m_userid = jsonToInt64(root["userid"]);

    //tags 缺失时留空数组。数组里混进非字符串元素时只跳过那一个元素，不整条作废 ——
    //应答里的 code 已经拿到了，为一条脏数据把整个应答判为解析失败得不偿失。
    if (root["tags"].isArray())
    {
        const Json::Value& jtags = root["tags"];
        for (Json::ArrayIndex i = 0; i < jtags.size(); ++i)
        {
            if (jtags[i].isString())
                out.m_tags.push_back(jtags[i].asString());
        }
    }

    return true;
}

} // namespace net
