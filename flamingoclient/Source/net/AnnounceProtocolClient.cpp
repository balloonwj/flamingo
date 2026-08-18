/**
 *  v2 平台公告报文编解码（客户端侧）实现，AnnounceProtocolClient.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "AnnounceProtocolClient.h"
#include <stdlib.h>                     // strtoll
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID 以字符串编码进 JSON，避免精度丢失。
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

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象时返回 false。
static bool parseObject(const std::string& jsonbody, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//从一个 JSON 对象里读出一条公告。缺字段按默认值填，不判失败——服务端将来加字段、
//或个别数据不全时，不该让整批公告都收不到。
static void readAnnouncementItem(const Json::Value& jo, AnnouncementItem& out)
{
    out.m_announcementId = jsonToInt64(jo["announcementid"]);
    out.m_title          = jo["title"].isString()      ? jo["title"].asString()      : std::string();
    out.m_body           = jo["body"].isString()       ? jo["body"].asString()       : std::string();
    out.m_severity       = jo["severity"].isInt()      ? jo["severity"].asInt()      : 0;
    out.m_senderName     = jo["sendername"].isString() ? jo["sendername"].asString() : std::string();
    out.m_pinned         = jo["pinned"].isInt()        ? jo["pinned"].asInt()        : 0;
    out.m_sentTimeMs     = jsonToInt64(jo["senttime"]);
    out.m_read           = jo["read"].isInt()          ? jo["read"].asInt()          : 0;
}

std::string AnnounceProtocolClient::buildGetAnnouncementsRequest(int64_t sinceId, int limit)
{
    Json::Value root;
    //64 位 ID 以字符串编码，与服务端 AnnouncementProtocol::parseGetAnnouncementsRequest 对齐。
    root["sinceid"] = std::to_string(sinceId);
    root["limit"]   = limit;
    return writeCompact(root);
}

std::string AnnounceProtocolClient::buildMarkReadRequest(int64_t announcementId)
{
    Json::Value root;
    root["announcementid"] = std::to_string(announcementId);
    return writeCompact(root);
}

bool AnnounceProtocolClient::parseGetAnnouncementsResponse(const std::string& jsonbody,
                                                           AnnouncementsResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 是判定成败的唯一依据，缺了就没法用，直接判失败。
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    //hasmore 服务端编的是 JSON bool；缺失时按「没有更多」处理，宁可少拉一轮也不要死循环。
    out.m_hasMore = root["hasmore"].isBool() ? root["hasmore"].asBool() : false;

    out.m_items.clear();
    const Json::Value& arr = root["announcements"];
    if (!arr.isArray())
        return true;   //没有 announcements 字段也算解析成功，只是一条都没有

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isObject())
            continue;

        AnnouncementItem item;
        readAnnouncementItem(arr[i], item);

        //公告ID是本端去重与增量拉取的依据，拿不到就没法用，跳过这一条。
        if (item.m_announcementId <= 0)
            continue;

        out.m_items.push_back(item);
    }

    return true;
}

bool AnnounceProtocolClient::parseAnnouncementNotify(const std::string& jsonbody,
                                                     AnnouncementNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["changetype"].isInt())
        return false;

    out.m_changeType = root["changetype"].asInt();

    const Json::Value& jo = root["announcement"];
    if (!jo.isObject())
        return false;

    //撤回推送只带 announcementid，其余字段本来就没有，readAnnouncementItem 会填默认值。
    readAnnouncementItem(jo, out.m_item);

    //两种 changetype 都必须带上有效的公告ID，否则这条推送没法处理。
    return out.m_item.m_announcementId > 0;
}

bool AnnounceProtocolClient::parseMarkReadResponse(const std::string& jsonbody,
                                                   AnnounceMarkReadResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 是判定成败的唯一依据，缺了就没法用，直接判失败。
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    //服务端原样回带，用来把应答对上是哪一条上报；缺失时为 0，不因此判整条应答失败。
    out.m_announcementId = jsonToInt64(root["announcementid"]);

    return true;
}

} // namespace net
