/**
 *  全局搜索（cmd_global_search 1904）报文编解码（客户端侧）实现，GlobalSearchProtocolClient.cpp
 *  整体说明见 GlobalSearchProtocolClient.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GlobalSearchProtocolClient.h"
#include <stdlib.h>                     // atoll
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompactGs(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象；非法 JSON 或顶层不是对象返回 false
static bool parseObjectGs(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 64 位整数：整型直接取，字符串按十进制解析（大整数 id 以字符串编码），其它返回 0
static int64_t jsonToInt64Gs(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    return 0;
}

//取字符串字段；缺失或不是字符串时返回空串
static std::string jsonToStringGs(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

std::string GlobalSearchProtocolClient::buildRequest(const std::string& keywordUtf8, int limit, int announcementLimit)
{
    Json::Value root;
    root["keyword"] = keywordUtf8;
    root["limit"] = limit;
    root["announcementlimit"] = announcementLimit;
    return writeCompactGs(root);
}

bool GlobalSearchProtocolClient::parseResponse(const std::string& jsonbody, GlobalSearchResult& out)
{
    Json::Value root;
    if (!parseObjectGs(jsonbody, root))
        return false;
    if (!root["code"].isIntegral())
        return false;

    out.m_code    = root["code"].asInt();
    out.m_msg     = jsonToStringGs(root["msg"]);
    out.m_keyword = jsonToStringGs(root["keyword"]);

    out.m_conversations.clear();
    const Json::Value& convs = root["conversations"];
    if (convs.isArray())
    {
        for (Json::ArrayIndex i = 0; i < convs.size(); ++i)
        {
            const Json::Value& jc = convs[i];
            if (!jc.isObject())
                continue;
            GlobalSearchConversation c;
            c.m_ctype       = jc["ctype"].isInt() ? jc["ctype"].asInt() : 1;
            c.m_cid         = jsonToInt64Gs(jc["cid"]);
            c.m_hitCount    = jc["hitcount"].isInt() ? jc["hitcount"].asInt() : 0;
            c.m_latestMsgid = jsonToInt64Gs(jc["latestmsgid"]);
            c.m_latestTime  = jsonToInt64Gs(jc["latesttime"]);
            c.m_latestText  = jsonToStringGs(jc["latesttext"]);
            //会话 id 不合法的条目跳过，不影响其余结果
            if (c.m_cid <= 0)
                continue;
            out.m_conversations.push_back(c);
        }
    }

    out.m_announcements.clear();
    const Json::Value& anns = root["announcements"];
    if (anns.isArray())
    {
        for (Json::ArrayIndex i = 0; i < anns.size(); ++i)
        {
            const Json::Value& ja = anns[i];
            if (!ja.isObject())
                continue;
            GlobalSearchAnnouncement a;
            a.m_groupid     = jsonToInt64Gs(ja["groupid"]);
            a.m_id          = jsonToInt64Gs(ja["id"]);
            a.m_title       = jsonToStringGs(ja["title"]);
            a.m_excerpt     = jsonToStringGs(ja["excerpt"]);
            a.m_publishTime = jsonToInt64Gs(ja["publishtime"]);
            if (a.m_groupid <= 0)
                continue;
            out.m_announcements.push_back(a);
        }
    }
    return true;
}

} // namespace net
