/**
 *  全局搜索（cmd_global_search 1904）的报文编解码。整体说明见 GlobalSearchProtocol.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "GlobalSearchProtocol.h"

#include "../jsoncpp1.9.0/json.h"

namespace {

// 把 UTF-8 JSON 文本解析为对象；解析失败或不是对象返回 false。
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

// 紧凑（无缩进、无换行）序列化。
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

// 去掉首尾的 ASCII 空白（空格、制表符、回车、换行）。
std::string TrimAscii(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
    {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
    {
        --e;
    }
    return s.substr(b, e - b);
}

// 把整数取值夹到 [lo, hi]。
int ClampInt(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

} // namespace

bool GlobalSearchProtocol::parseRequest(const std::string& json, GlobalSearchRequest& req)
{
    Json::Value root;
    if (!ParseObject(json, root) || !root["keyword"].isString())
    {
        return false;
    }
    req.keyword = TrimAscii(root["keyword"].asString());
    if (req.keyword.empty() || (int)req.keyword.size() > GLOBAL_SEARCH_KEYWORD_MAX_BYTES)
    {
        return false;
    }
    req.limit = root["limit"].isInt()
              ? ClampInt(root["limit"].asInt(), 1, GLOBAL_SEARCH_LIMIT_MAX)
              : GLOBAL_SEARCH_DEFAULT_LIMIT;
    req.announcementlimit = root["announcementlimit"].isInt()
                          ? ClampInt(root["announcementlimit"].asInt(), 0, GLOBAL_SEARCH_ANNOUNCEMENT_LIMIT_MAX)
                          : GLOBAL_SEARCH_ANNOUNCEMENT_DEFAULT_LIMIT;
    return true;
}

std::string GlobalSearchProtocol::buildResponse(int code, const std::string& msg, const std::string& keyword,
                                                const std::vector<GlobalSearchConversationHit>& convs,
                                                const std::vector<GlobalSearchAnnouncementHit>& anns)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"] = msg;
    if (code != 0)
    {
        return WriteCompact(root);
    }
    root["keyword"] = keyword;

    Json::Value convArr(Json::arrayValue);
    for (size_t i = 0; i < convs.size(); ++i)
    {
        Json::Value item(Json::objectValue);
        item["ctype"] = convs[i].ctype;
        item["cid"] = std::to_string((long long)convs[i].cid);
        item["hitcount"] = convs[i].hitcount;
        item["latestmsgid"] = std::to_string((long long)convs[i].latestmsgid);
        item["latesttime"] = (Json::Int64)convs[i].latesttime;
        item["latesttext"] = convs[i].latesttext;
        convArr.append(item);
    }
    root["conversations"] = convArr;

    Json::Value annArr(Json::arrayValue);
    for (size_t i = 0; i < anns.size(); ++i)
    {
        Json::Value item(Json::objectValue);
        item["groupid"] = std::to_string((long long)anns[i].groupid);
        item["id"] = std::to_string((long long)anns[i].id);
        item["title"] = anns[i].title;
        item["excerpt"] = anns[i].excerpt;
        item["publishtime"] = (Json::Int64)anns[i].publishtime;
        annArr.append(item);
    }
    root["announcements"] = annArr;
    return WriteCompact(root);
}
