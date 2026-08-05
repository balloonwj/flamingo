/**
 *  平台公告投递报文编解码实现，AnnouncementProtocol.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "AnnouncementProtocol.h"
#include "Protocol.h"
#include "../jsoncpp1.9.0/json.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象时返回 false。
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

/**
 *  从 JSON 里取一个 64 位 ID。
 *
 *  v2 协议约定 64 位 ID 在 JSON 里一律用<u>字符串</u>承载（避免 JavaScript 端精度丢失），
 *  但这里也兼容数字写法：Android 端历史上有过直接写数字的实现，宽进严出。
 *  字段缺失或类型不对时返回 0。
 */
static int64_t getId(const Json::Value& root, const char* key)
{
    if (root[key].isString())
        return static_cast<int64_t>(atoll(root[key].asString().c_str()));

    if (root[key].isIntegral())
        return root[key].asInt64();

    return 0;
}

//把一条公告的全部字段写进 Json 对象，应答与推送共用。
static void fillAnnouncementItem(Json::Value& jo, const AnnouncementPushItem& item)
{
    jo["announcementid"] = std::to_string(item.announcementId);
    jo["title"]          = item.title;
    jo["body"]           = item.body;
    jo["severity"]       = item.severity;
    jo["sendername"]     = item.senderName;
    jo["pinned"]         = item.pinned;
    jo["senttime"]       = static_cast<Json::Int64>(item.sentTime);
    jo["read"]           = item.read;
}

bool AnnouncementProtocol::parseGetAnnouncementsRequest(const std::string& json,
                                                        GetAnnouncementsReqData& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //sinceid 缺失即为 0，表示全量拉取；负数按 0 处理（客户端不该发负数，但不必因此拒绝整个请求）。
    req.sinceId = getId(root, "sinceid");
    if (req.sinceId < 0)
        req.sinceId = 0;

    //limit 缺失或非正时取默认值，超过硬上限时截断。
    req.limit = root["limit"].isIntegral() ? root["limit"].asInt() : 0;
    if (req.limit <= 0)
        req.limit = ANNOUNCEMENT_FETCH_LIMIT_DEFAULT;
    if (req.limit > ANNOUNCEMENT_FETCH_LIMIT_MAX)
        req.limit = ANNOUNCEMENT_FETCH_LIMIT_MAX;

    return true;
}

bool AnnouncementProtocol::parseMarkReadRequest(const std::string& json, int64_t& announcementId)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    announcementId = getId(root, "announcementid");
    return announcementId > 0;
}

std::string AnnouncementProtocol::buildGetAnnouncementsResponse(
    int code, const std::string& msg, bool hasMore,
    const std::vector<AnnouncementPushItem>& items)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["hasmore"] = hasMore;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value jo;
        fillAnnouncementItem(jo, items[i]);
        arr.append(jo);
    }
    root["announcements"] = arr;

    return writeCompact(root);
}

std::string AnnouncementProtocol::buildAnnouncementNotify(int changeType,
                                                          const AnnouncementPushItem& item)
{
    Json::Value root;
    root["changetype"] = changeType;

    Json::Value jo;
    if (changeType == announcement_change_recalled)
    {
        //撤回通知只带ID：客户端凭ID把本地那条移除即可，不需要重复传一遍正文。
        jo["announcementid"] = std::to_string(item.announcementId);
    }
    else
    {
        fillAnnouncementItem(jo, item);
    }
    root["announcement"] = jo;

    return writeCompact(root);
}

std::string AnnouncementProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

std::string AnnouncementProtocol::buildMarkReadResponse(int code, const std::string& msg,
                                                        int64_t announcementId)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //原样回带，供客户端把应答对上是哪一条已读上报。
    root["announcementid"] = std::to_string(announcementId);
    return writeCompact(root);
}

int64_t AnnouncementProtocol::dateTimeToMs(const std::string& text)
{
    if (text.empty())
        return 0;

    int year   = 0;
    int month  = 0;
    int day    = 0;
    int hour   = 0;
    int minute = 0;
    int second = 0;

    //秒可以缺省：库里多数是完整的 "YYYY-MM-DD HH:MM:SS"，但管理控制台传来的定时时间
    //是分钟精度的 "YYYY-MM-DD HH:MM"，两种都要能认。
    int matched = sscanf(text.c_str(), "%d-%d-%d %d:%d:%d",
                         &year, &month, &day, &hour, &minute, &second);
    if (matched < 5)
        return 0;

    //年份明显不合理时判为无法识别 —— 库里 datetime 为 NULL 时某些驱动会给出
    //"0000-00-00 00:00:00"，那种值转出来是个负数时间戳，会让客户端的排序错乱。
    if (year < 1970)
        return 0;

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;   //tm_year 以 1900 为基准
    t.tm_mon   = month - 1;     //tm_mon 取值 0~11
    t.tm_mday  = day;
    t.tm_hour  = hour;
    t.tm_min   = minute;
    t.tm_sec   = second;
    t.tm_isdst = -1;            //-1 = 让运行库自行判断当地是否处于夏令时

    //按本地时区解释，与 NOW() 写进库时的时区保持一致。
    time_t secs = mktime(&t);
    if (secs == static_cast<time_t>(-1))
        return 0;

    //毫秒的换算基数：数据库里的 datetime 只精确到秒，协议一律用 Unix 毫秒。
    const int64_t MS_PER_SECOND = 1000;
    return static_cast<int64_t>(secs) * MS_PER_SECOND;
}
