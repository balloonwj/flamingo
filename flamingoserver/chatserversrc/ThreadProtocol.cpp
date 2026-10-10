/**
 *  帖子群协议与规则 ThreadProtocol 的实现，见 ThreadProtocol.h 与 docs/帖子群方案.md 第 4~6 节。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include "Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//64 位整数的十进制文本最多 20 个字符（含负号），留余量
const int kInt64TextChars = 32;

//卡片标题在没有标题时取正文的前多少个字符
const int kDisplayTitleChars = 30;

//atToJson 的 maxBytes 取这个值时不按字节数筛选片段（正文没有截取）
const int kAtNoByteLimit = -1;

//@ 片段必须以这个字节开头（「@名字」的 '@'）
const char kMentionSign = '@';

//把 Json::Value 序列化为紧凑（无缩进、无换行、冒号两侧无空格）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 64 位 ID 写成十进制字符串（v2 惯例）
std::string idToString(int64_t id)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
    return std::string(buf);
}

//从一个 JSON 值里取 64 位 ID：十进制字符串与整数两种写法都接受；其余类型、空串、非数字返回 0
int64_t valueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty() || text.size() >= static_cast<size_t>(kInt64TextChars))
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(atoll(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//取出必填的正整数 ID 字段
bool getRequiredId(const Json::Value& root, const char* key, int64_t& out)
{
    out = 0;
    if (!root.isMember(key))
        return false;
    out = valueToId(root[key]);
    return out > 0;
}

//取出可选的非负整数 ID 字段：缺失为 0；存在但不是合法 ID（含 "0"）时，"0" 与 0 视为 0，其余返回 false
bool getOptionalId(const Json::Value& root, const char* key, int64_t& out)
{
    out = 0;
    if (!root.isMember(key))
        return true;
    const Json::Value& v = root[key];
    if ((v.isString() && v.asString() == "0") || (v.isInt() && v.asInt() == 0))
        return true;
    out = valueToId(v);
    return out > 0;
}

//取出可选字符串字段：缺失为空串；存在但不是字符串返回 false
bool getOptionalString(const Json::Value& root, const char* key, std::string& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    if (!root[key].isString())
        return false;
    out = root[key].asString();
    return true;
}

//取出必填的去重标识：非空、不超过 kThreadMaxIdTextBytes 字节
bool getClientId(const Json::Value& root, const char* key, std::string& out)
{
    if (!getOptionalString(root, key, out))
        return false;
    return !out.empty() && out.size() <= static_cast<size_t>(kThreadMaxIdTextBytes);
}

//取出可选的分页条数：缺失为缺省值；不大于 0 按缺省值；超过上限按上限
bool getLimit(const Json::Value& root, int& out)
{
    out = kThreadDefaultPageSize;
    if (!root.isMember("limit"))
        return true;
    if (!root["limit"].isInt())
        return false;
    const int v = root["limit"].asInt();
    if (v <= 0)
        out = kThreadDefaultPageSize;
    else if (v > kThreadMaxPageSize)
        out = kThreadMaxPageSize;
    else
        out = v;
    return true;
}

//解析 mentions 数组：元素为十进制字符串或整数，非法项按报文非法处理
bool parseMentions(const Json::Value& root, std::vector<int64_t>& out)
{
    out.clear();
    if (!root.isMember("mentions"))
        return true;
    const Json::Value& arr = root["mentions"];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = valueToId(arr[i]);
        if (id <= 0)
            return false;
        out.push_back(id);
    }
    return true;
}

//JSON 值是否明确写的是 0（字符串 "0" 或整数 0）。「@所有人」的片段 uid 为 0，要与写错的 uid 区分开：
//valueToId 遇到非法值同样返回 0
bool isExplicitZeroId(const Json::Value& v)
{
    if (v.isString())
        return v.asString() == "0";
    return v.isInt64() && v.asInt64() == 0;
}

//解析 at 数组（@ 片段，docs/帖子群方案.md 14.4）：字段缺失为空；存在但不是数组按报文非法处理。
//数组里不是对象、uid 不合法、off / len 不是整数的项直接丢弃，不判整个报文非法：片段只影响显示，
//不能因为一段位置写错就拒收整条内容。uid 明确为 0 的是「@所有人」的片段（§15），保留。
//范围、重叠以及「@所有人」片段是否有效由 sanitizeMentionSpans 再整理
bool parseAt(const Json::Value& root, std::vector<ThreadMentionSpan>& out)
{
    out.clear();
    if (!root.isMember("at"))
        return true;
    const Json::Value& arr = root["at"];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject() || !item["off"].isInt() || !item["len"].isInt())
            continue;
        ThreadMentionSpan span;
        span.uid = valueToId(item["uid"]);
        span.off = item["off"].asInt();
        span.len = item["len"].asInt();
        if (span.uid < 0 || (span.uid == 0 && !isExplicitZeroId(item["uid"])))
            continue;
        out.push_back(span);
    }
    return true;
}

//解析 mentionall（是否 @ 所有人，docs/帖子群方案.md §15）：字段缺失为 false；存在但不是布尔值按报文非法处理
bool parseMentionAll(const Json::Value& root, bool& out)
{
    out = false;
    if (!root.isMember("mentionall"))
        return true;
    const Json::Value& v = root["mentionall"];
    if (!v.isBool())
        return false;
    out = v.asBool();
    return true;
}

//解析一张图片
bool parseImage(const Json::Value& v, ThreadImage& img)
{
    if (!v.isObject())
        return false;
    if (!getOptionalString(v, "fileid", img.fileid) || !getOptionalString(v, "md5", img.md5)
        || !getOptionalString(v, "thumbfileid", img.thumbfileid))
        return false;
    if (v.isMember("width"))
    {
        if (!v["width"].isInt())
            return false;
        img.width = v["width"].asInt();
    }
    if (v.isMember("height"))
    {
        if (!v["height"].isInt())
            return false;
        img.height = v["height"].asInt();
    }
    if (v.isMember("size"))
    {
        if (!v["size"].isInt64())
            return false;
        img.size = v["size"].asInt64();
    }
    return true;
}

//解析内容字段（title / text / images / mentions），只检查类型；长度与数量由 validateBody 检查
bool parseBodyFields(const Json::Value& root, ThreadBody& body)
{
    if (!getOptionalString(root, "title", body.title) || !getOptionalString(root, "text", body.text))
        return false;
    body.images.clear();
    if (root.isMember("images"))
    {
        const Json::Value& arr = root["images"];
        if (!arr.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            ThreadImage img;
            if (!parseImage(arr[i], img))
                return false;
            body.images.push_back(img);
        }
    }
    return parseMentions(root, body.mentions) && parseAt(root, body.at) && parseMentionAll(root, body.mentionall);
}

//一张图片 → JSON
Json::Value imageToJson(const ThreadImage& img)
{
    Json::Value v(Json::objectValue);
    v["fileid"] = img.fileid;
    v["md5"] = img.md5;
    v["width"] = img.width;
    v["height"] = img.height;
    v["size"] = static_cast<Json::Int64>(img.size);
    v["thumbfileid"] = img.thumbfileid;
    return v;
}

//一组图片 → JSON 数组（卡片用：帖子与补充最多 kThreadMaxImages 张，校验时已保证）
Json::Value imagesToJson(const std::vector<ThreadImage>& images)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < images.size() && i < static_cast<size_t>(kThreadMaxImages); ++i)
        arr.append(imageToJson(images[i]));
    return arr;
}

//一组卡片状态 → [{msgid, msgtype, content}]，content 解析成 JSON 对象放入；解析失败的卡片跳过
Json::Value cardStatesToJson(const std::vector<ThreadCardState>& cards)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < cards.size(); ++i)
    {
        Json::Value content;
        if (!parseObject(cards[i].content, content))
            continue;
        Json::Value v(Json::objectValue);
        v["msgid"] = idToString(cards[i].msgid);
        v["msgtype"] = cards[i].msgtype;
        v["content"] = content;
        arr.append(v);
    }
    return arr;
}

//ID 列表 → 字符串数组
Json::Value idsToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(idToString(ids[i]));
    return arr;
}

//@ 片段 → [{uid,off,len}]；maxBytes 不小于 0 时只保留整段落在正文前 maxBytes 个字节内的片段
//（帖子列表里的正文是截取过的，越出截取范围的片段对不上文字）
Json::Value atToJson(const std::vector<ThreadMentionSpan>& spans, int maxBytes)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < spans.size(); ++i)
    {
        const ThreadMentionSpan& s = spans[i];
        if (maxBytes >= 0 && s.off + s.len > maxBytes)
            continue;
        Json::Value v(Json::objectValue);
        v["uid"] = idToString(s.uid);
        v["off"] = s.off;
        v["len"] = s.len;
        arr.append(v);
    }
    return arr;
}

//互动汇总 → [{r,n,me}]
Json::Value reactionsToJson(const std::vector<ThreadReaction>& reactions)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < reactions.size(); ++i)
    {
        Json::Value v(Json::objectValue);
        v["r"] = reactions[i].reaction;
        v["n"] = reactions[i].count;
        v["me"] = reactions[i].mine ? 1 : 0;
        arr.append(v);
    }
    return arr;
}

//楼主补充 → JSON
Json::Value addendumToJson(const ThreadAddendum& a)
{
    Json::Value v(Json::objectValue);
    v["addid"] = idToString(a.addid);
    v["postid"] = idToString(a.postid);
    v["author"] = idToString(a.author);
    v["text"] = a.body.text;
    Json::Value images(Json::arrayValue);
    for (size_t i = 0; i < a.body.images.size(); ++i)
        images.append(imageToJson(a.body.images[i]));
    v["images"] = images;
    v["mentions"] = idsToJson(a.body.mentions);
    v["at"] = atToJson(a.body.at, kAtNoByteLimit);
    //@ 了所有人时才写，没有时与老版本的输出相同
    if (a.body.mentionall)
        v["mentionall"] = true;
    v["createtime"] = static_cast<Json::Int64>(a.createTime);
    return v;
}

//帖子 → JSON；full 为 false 时正文截取 kThreadListTextChars 个字符、只带前 kThreadListImages 张图、不带补充
Json::Value postToJson(const ThreadPost& p, bool full)
{
    Json::Value v(Json::objectValue);
    v["postid"] = idToString(p.postid);
    v["groupid"] = idToString(p.groupid);
    v["author"] = idToString(p.author);
    v["title"] = p.body.title;
    bool cut = false;
    const std::string text = full ? p.body.text : ThreadProtocol::utf8Prefix(p.body.text, kThreadListTextChars, cut);
    v["text"] = text;
    v["texttruncated"] = cut ? 1 : 0;
    //截取过的正文只带落在截取范围内的 @ 片段
    v["at"] = atToJson(p.body.at, cut ? static_cast<int>(text.size()) : kAtNoByteLimit);
    Json::Value images(Json::arrayValue);
    const size_t imageLimit = full ? p.body.images.size()
                                   : std::min(p.body.images.size(), static_cast<size_t>(kThreadListImages));
    for (size_t i = 0; i < imageLimit; ++i)
        images.append(imageToJson(p.body.images[i]));
    v["images"] = images;
    v["imagecount"] = static_cast<int>(p.body.images.size());
    v["mentions"] = idsToJson(p.body.mentions);
    if (p.body.mentionall)
        v["mentionall"] = true;
    v["createtime"] = static_cast<Json::Int64>(p.createTime);
    v["edittime"] = static_cast<Json::Int64>(p.editTime);
    v["activetime"] = static_cast<Json::Int64>(p.activeTime);
    v["commentcount"] = p.commentCount;
    v["pintime"] = static_cast<Json::Int64>(p.pinTime);
    v["addendumcount"] = p.addendumCount;
    v["reactions"] = reactionsToJson(p.reactions);
    if (full)
    {
        Json::Value addenda(Json::arrayValue);
        for (size_t i = 0; i < p.addenda.size(); ++i)
            addenda.append(addendumToJson(p.addenda[i]));
        v["addenda"] = addenda;
    }
    return v;
}

//留言或回复 → JSON；已删除的留言不带正文与 @
Json::Value commentToJson(const ThreadComment& c)
{
    Json::Value v(Json::objectValue);
    v["commentid"] = idToString(c.commentid);
    v["postid"] = idToString(c.postid);
    v["author"] = idToString(c.author);
    v["parentid"] = idToString(c.parentid);
    v["replyto"] = idToString(c.replyTo);
    v["text"] = c.deleted ? std::string() : c.body.text;
    v["mentions"] = c.deleted ? Json::Value(Json::arrayValue) : idsToJson(c.body.mentions);
    v["at"] = c.deleted ? Json::Value(Json::arrayValue) : atToJson(c.body.at, kAtNoByteLimit);
    if (!c.deleted && c.body.mentionall)
        v["mentionall"] = true;
    v["createtime"] = static_cast<Json::Int64>(c.createTime);
    v["deleted"] = c.deleted ? 1 : 0;
    v["replycount"] = c.replyCount;
    v["reactions"] = reactionsToJson(c.reactions);
    if (!c.previewReplies.empty())
    {
        Json::Value replies(Json::arrayValue);
        for (size_t i = 0; i < c.previewReplies.size(); ++i)
            replies.append(commentToJson(c.previewReplies[i]));
        v["replies"] = replies;
    }
    return v;
}

//判断文本是否只有空白（ASCII 空白与全角空格 U+3000）
bool isBlank(const std::string& text)
{
    size_t i = 0;
    while (i < text.size())
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            ++i;
            continue;
        }
        //全角空格 E3 80 80
        if (c == 0xE3 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80
            && static_cast<unsigned char>(text[i + 2]) == 0x80)
        {
            i += 3;
            continue;
        }
        return false;
    }
    return true;
}

//只含数字的文本解析为非负整数；不是时返回 false
bool parseDigits(const std::string& text, int64_t& out)
{
    out = 0;
    if (text.empty() || text.size() >= static_cast<size_t>(kInt64TextChars))
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    out = static_cast<int64_t>(atoll(text.c_str()));
    return true;
}

} // namespace

//======================== 解析 ========================

bool ThreadProtocol::parseListPostsRequest(const std::string& json, ListPostsRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "groupid", req.groupid))
        return false;
    req.sort = thread_sort_activity;
    if (root.isMember("sort"))
    {
        if (!root["sort"].isInt())
            return false;
        req.sort = root["sort"].asInt();
        if (req.sort != thread_sort_activity && req.sort != thread_sort_publish)
            return false;
    }
    if (!getOptionalString(root, "cursor", req.cursor))
        return false;
    return getLimit(root, req.limit);
}

bool ThreadProtocol::parsePostIdRequest(const std::string& json, int64_t& postid)
{
    Json::Value root;
    return parseObject(json, root) && getRequiredId(root, "postid", postid);
}

bool ThreadProtocol::parsePublishPostRequest(const std::string& json, PostWriteRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "groupid", req.groupid)
        || !getClientId(root, "clientpostid", req.clientId))
        return false;
    return parseBodyFields(root, req.body);
}

bool ThreadProtocol::parseEditPostRequest(const std::string& json, PostWriteRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "postid", req.postid))
        return false;
    return parseBodyFields(root, req.body);
}

bool ThreadProtocol::parsePinRequest(const std::string& json, int64_t& postid, bool& pin)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "postid", postid) || !root.isMember("pin")
        || !root["pin"].isInt())
        return false;
    pin = root["pin"].asInt() != 0;
    return true;
}

bool ThreadProtocol::parseListCommentsRequest(const std::string& json, ListCommentsRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "postid", req.postid)
        || !getOptionalId(root, "cursor", req.cursor))
        return false;
    return getLimit(root, req.limit);
}

bool ThreadProtocol::parseListRepliesRequest(const std::string& json, ListCommentsRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "commentid", req.commentid)
        || !getOptionalId(root, "cursor", req.cursor))
        return false;
    return getLimit(root, req.limit);
}

bool ThreadProtocol::parseAddCommentRequest(const std::string& json, AddCommentRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "postid", req.postid)
        || !getOptionalId(root, "parentid", req.parentid) || !getClientId(root, "clientcommentid", req.clientId))
        return false;
    //留言与回复的正文同样支持 at 片段（docs/帖子群方案.md 14.4），与帖子、补充走同一个解析函数
    if (!getOptionalString(root, "text", req.body.text) || !parseMentions(root, req.body.mentions)
        || !parseAt(root, req.body.at) || !parseMentionAll(root, req.body.mentionall))
        return false;
    //留言与回复不带标题与图片：带了按报文非法处理，避免客户端以为发出去了
    if (root.isMember("title") || root.isMember("images"))
        return false;
    return true;
}

bool ThreadProtocol::parseCommentIdRequest(const std::string& json, int64_t& commentid)
{
    Json::Value root;
    return parseObject(json, root) && getRequiredId(root, "commentid", commentid);
}

bool ThreadProtocol::parseReactRequest(const std::string& json, ReactRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !root.isMember("targettype") || !root["targettype"].isInt()
        || !getRequiredId(root, "targetid", req.targetid) || !root.isMember("reaction") || !root["reaction"].isInt()
        || !root.isMember("on") || !root["on"].isInt())
        return false;
    req.targettype = root["targettype"].asInt();
    req.reaction = root["reaction"].asInt();
    req.on = root["on"].asInt() != 0;
    return req.targettype == thread_target_post || req.targettype == thread_target_comment;
}

bool ThreadProtocol::parseGetReactorsRequest(const std::string& json, ReactRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !root.isMember("targettype") || !root["targettype"].isInt()
        || !getRequiredId(root, "targetid", req.targetid) || !root.isMember("reaction") || !root["reaction"].isInt())
        return false;
    req.targettype = root["targettype"].asInt();
    req.reaction = root["reaction"].asInt();
    req.limit = kThreadMaxReactors;
    if (root.isMember("limit"))
    {
        if (!root["limit"].isInt())
            return false;
        const int v = root["limit"].asInt();
        if (v > 0 && v < kThreadMaxReactors)
            req.limit = v;
    }
    return req.targettype == thread_target_post || req.targettype == thread_target_comment;
}

bool ThreadProtocol::parseWatchRequest(const std::string& json, int64_t& groupid, bool& watch)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "groupid", groupid) || !root.isMember("watch")
        || !root["watch"].isInt())
        return false;
    watch = root["watch"].asInt() != 0;
    return true;
}

bool ThreadProtocol::parseAddAddendumRequest(const std::string& json, PostWriteRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "postid", req.postid)
        || !getClientId(root, "clientaddid", req.clientId))
        return false;
    return parseBodyFields(root, req.body);
}

bool ThreadProtocol::parseAddendumIdRequest(const std::string& json, int64_t& addid)
{
    Json::Value root;
    return parseObject(json, root) && getRequiredId(root, "addid", addid);
}

bool ThreadProtocol::parseCheckCardsRequest(const std::string& json, int64_t& groupid, std::vector<int64_t>& msgids)
{
    msgids.clear();
    Json::Value root;
    if (!parseObject(json, root) || !getRequiredId(root, "groupid", groupid))
        return false;
    const Json::Value& arr = root["msgids"];
    if (!arr.isArray() || arr.empty() || arr.size() > static_cast<Json::ArrayIndex>(kThreadMaxCheckCards))
        return false;
    std::set<int64_t> seen;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = valueToId(arr[i]);
        if (id <= 0)
            return false;
        if (seen.insert(id).second)
            msgids.push_back(id);
    }
    return true;
}

//======================== 校验与规则 ========================

int ThreadProtocol::validateBody(const ThreadBody& body, int kind)
{
    int maxText = kThreadPostTextMaxChars;
    bool allowTitle = false;
    bool allowImages = true;
    switch (kind)
    {
    //帖子：可有标题，正文 5000 字
    case kBodyPost:
        allowTitle = true;
        break;

    //楼主补充：不能有标题，正文 2000 字
    case kBodyAddendum:
        maxText = kThreadAddendumTextMaxChars;
        break;

    //留言与回复：不能有标题与图片，正文 1000 字
    case kBodyComment:
        maxText = kThreadCommentTextMaxChars;
        allowImages = false;
        break;

    //调用方传错了种类：按不合法处理
    default:
        return error_code_thread_invalid;
    }

    if (!allowTitle && !body.title.empty())
        return error_code_thread_invalid;
    if (utf8Chars(body.title) > kThreadTitleMaxChars)
        return error_code_thread_invalid;
    if (utf8Chars(body.text) > maxText)
        return error_code_thread_invalid;
    if (!allowImages && !body.images.empty())
        return error_code_thread_invalid;
    if (static_cast<int>(body.images.size()) > kThreadMaxImages)
        return error_code_thread_invalid;
    if (static_cast<int>(body.mentions.size()) > kThreadMaxMentions)
        return error_code_thread_invalid;
    for (size_t i = 0; i < body.images.size(); ++i)
    {
        const ThreadImage& img = body.images[i];
        if (img.fileid.empty() || img.fileid.size() > static_cast<size_t>(kThreadMaxIdTextBytes)
            || img.md5.size() > static_cast<size_t>(kThreadMaxIdTextBytes)
            || img.thumbfileid.size() > static_cast<size_t>(kThreadMaxIdTextBytes) || img.width < 0 || img.height < 0
            || img.size < 0)
            return error_code_thread_invalid;
    }
    //空内容：没有文字（只有空白也算没有）也没有图片
    if (isBlank(body.text) && body.images.empty())
        return error_code_thread_invalid;
    return error_code_ok;
}

bool ThreadProtocol::isValidReaction(int reaction)
{
    return reaction == kThreadReactionLike
           || (reaction >= kThreadReactionFaceBase && reaction <= kThreadReactionFaceBase + kThreadMaxFaceId);
}

int ThreadProtocol::utf8Chars(const std::string& text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //后续字节（10xxxxxx）不单独计数
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            ++count;
    }
    return count;
}

std::string ThreadProtocol::utf8Prefix(const std::string& text, int maxChars, bool& cut)
{
    cut = false;
    int chars = 0;
    size_t i = 0;
    while (i < text.size())
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
        {
            if (chars == maxChars)
            {
                cut = true;
                return text.substr(0, i);
            }
            ++chars;
        }
        ++i;
    }
    return text;
}

std::string ThreadProtocol::makeDigest(const std::string& text, int maxChars)
{
    //先把空白折成单个空格，再截取，最后去掉首尾空格
    std::string folded;
    folded.reserve(text.size());
    bool lastSpace = true;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (!lastSpace)
                folded.push_back(' ');
            lastSpace = true;
            continue;
        }
        folded.push_back(c);
        lastSpace = false;
    }
    bool cut = false;
    std::string out = utf8Prefix(folded, maxChars, cut);
    while (!out.empty() && out[out.size() - 1] == ' ')
        out.erase(out.size() - 1);
    return out;
}

bool ThreadProtocol::canEditPost(int64_t authorId, int64_t userId)
{
    return authorId == userId;
}

bool ThreadProtocol::canDeleteContent(int64_t authorId, int64_t userId, int role)
{
    return authorId == userId || role >= group_role_admin;
}

bool ThreadProtocol::canPin(int role)
{
    return role >= group_role_admin;
}

bool ThreadProtocol::canAddAddendum(int64_t postAuthorId, int64_t userId)
{
    return postAuthorId == userId;
}

void ThreadProtocol::replyCardAudience(int kind, int64_t postAuthor, int64_t replyTo,
                                       const std::vector<int64_t>& commenters, const std::vector<int64_t>& mentions,
                                       int64_t operatorId, std::vector<int64_t>& out)
{
    std::set<int64_t> ids;
    switch (kind)
    {
    //留言：楼主
    case thread_reply_comment:
        ids.insert(postAuthor);
        break;

    //回复：楼主与被回复的人
    case thread_reply_reply:
        ids.insert(postAuthor);
        ids.insert(replyTo);
        break;

    //补充：在该帖下留过言或回复过的人
    case thread_reply_addendum:
        ids.insert(commenters.begin(), commenters.end());
        break;

    //未知种类：只按 @ 计算
    default:
        break;
    }
    ids.insert(mentions.begin(), mentions.end());
    ids.erase(operatorId);
    ids.erase(0);
    out.assign(ids.begin(), ids.end());
}

void ThreadProtocol::mergeMentionAllAudience(const std::vector<int64_t>& members, int64_t operatorId,
                                             std::vector<int64_t>& audience)
{
    std::set<int64_t> ids(audience.begin(), audience.end());
    ids.insert(members.begin(), members.end());
    ids.erase(operatorId);
    ids.erase(0);
    audience.assign(ids.begin(), ids.end());
}

void ThreadProtocol::filterMentions(std::vector<int64_t>& mentions, const std::vector<int64_t>& members)
{
    std::set<int64_t> memberSet(members.begin(), members.end());
    std::set<int64_t> seen;
    std::vector<int64_t> kept;
    for (size_t i = 0; i < mentions.size(); ++i)
    {
        if (memberSet.count(mentions[i]) > 0 && seen.insert(mentions[i]).second)
            kept.push_back(mentions[i]);
    }
    mentions.swap(kept);
}

//按 off 升序排列 @ 片段的比较函数
static bool spanOffLess(const ThreadMentionSpan& a, const ThreadMentionSpan& b)
{
    return a.off < b.off;
}

void ThreadProtocol::sanitizeMentionSpans(ThreadBody& body)
{
    const std::set<int64_t> mentioned(body.mentions.begin(), body.mentions.end());
    const int textBytes = static_cast<int>(body.text.size());
    std::vector<ThreadMentionSpan> valid;
    for (size_t i = 0; i < body.at.size(); ++i)
    {
        const ThreadMentionSpan& s = body.at[i];
        if (s.uid < 0 || s.off < 0 || s.len <= 0 || s.off > textBytes - s.len)
            continue;
        if (body.text[static_cast<size_t>(s.off)] != kMentionSign)
            continue;
        //uid 为 0 的是「@所有人」：只在本条内容确实 @ 了所有人时保留；其余 uid 必须在 mentions 里
        if (s.uid == 0 ? !body.mentionall : mentioned.count(s.uid) == 0)
            continue;
        valid.push_back(s);
    }
    std::stable_sort(valid.begin(), valid.end(), spanOffLess);
    std::vector<ThreadMentionSpan> kept;
    int lastEnd = 0;
    for (size_t i = 0; i < valid.size() && static_cast<int>(kept.size()) < kThreadMaxMentionSpans; ++i)
    {
        //与前一段重叠的丢弃：同一段文字不能同时属于两个人
        if (!kept.empty() && valid[i].off < lastEnd)
            continue;
        kept.push_back(valid[i]);
        lastEnd = valid[i].off + valid[i].len;
    }
    body.at.swap(kept);
}

//======================== 游标 ========================

std::string ThreadProtocol::makeActivityCursor(int64_t activeTime, int64_t postid)
{
    return "a" + idToString(activeTime) + "_" + idToString(postid);
}

bool ThreadProtocol::parseActivityCursor(const std::string& cursor, int64_t& activeTime, int64_t& postid)
{
    activeTime = 0;
    postid = 0;
    if (cursor.size() < 4 || cursor[0] != 'a')
        return false;
    const size_t sep = cursor.find('_');
    if (sep == std::string::npos)
        return false;
    return parseDigits(cursor.substr(1, sep - 1), activeTime) && parseDigits(cursor.substr(sep + 1), postid);
}

std::string ThreadProtocol::makePublishCursor(int64_t postid)
{
    return "p" + idToString(postid);
}

bool ThreadProtocol::parsePublishCursor(const std::string& cursor, int64_t& postid)
{
    postid = 0;
    if (cursor.size() < 2 || cursor[0] != 'p')
        return false;
    return parseDigits(cursor.substr(1), postid);
}

//======================== 内容 JSON ========================

std::string ThreadProtocol::bodyToJson(const ThreadBody& body)
{
    Json::Value v(Json::objectValue);
    v["title"] = body.title;
    v["text"] = body.text;
    Json::Value images(Json::arrayValue);
    for (size_t i = 0; i < body.images.size(); ++i)
        images.append(imageToJson(body.images[i]));
    v["images"] = images;
    v["mentions"] = idsToJson(body.mentions);
    //没有片段时不写，老数据与新数据的落库内容保持相同
    if (!body.at.empty())
        v["at"] = atToJson(body.at, kAtNoByteLimit);
    //没有 @ 所有人时同样不写
    if (body.mentionall)
        v["mentionall"] = true;
    return writeCompact(v);
}

bool ThreadProtocol::bodyFromJson(const std::string& json, ThreadBody& body)
{
    body = ThreadBody();
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!parseBodyFields(root, body))
    {
        body = ThreadBody();
        return false;
    }
    return true;
}

//======================== 应答与推送 ========================

std::string ThreadProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    return writeCompact(v);
}

std::string ThreadProtocol::buildListPostsResponse(int code, const std::string& msg, const std::vector<ThreadPost>& posts,
                                                   const std::string& nextCursor, bool hasMore)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < posts.size(); ++i)
        arr.append(postToJson(posts[i], false));
    v["posts"] = arr;
    v["nextcursor"] = nextCursor;
    v["hasmore"] = hasMore ? 1 : 0;
    return writeCompact(v);
}

std::string ThreadProtocol::buildPostResponse(int code, const std::string& msg, const ThreadPost* post, bool full)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    if (post != NULL)
        v["post"] = postToJson(*post, full);
    return writeCompact(v);
}

std::string ThreadProtocol::buildCommentsResponse(int code, const std::string& msg, int64_t ownerId,
                                                  const std::vector<ThreadComment>& comments, int64_t nextCursor,
                                                  bool hasMore)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v["ownerid"] = idToString(ownerId);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < comments.size(); ++i)
        arr.append(commentToJson(comments[i]));
    v["comments"] = arr;
    v["nextcursor"] = idToString(nextCursor);
    v["hasmore"] = hasMore ? 1 : 0;
    return writeCompact(v);
}

std::string ThreadProtocol::buildCommentResponse(int code, const std::string& msg, const ThreadComment* comment)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    if (comment != NULL)
        v["comment"] = commentToJson(*comment);
    return writeCompact(v);
}

std::string ThreadProtocol::buildReactResponse(int code, const std::string& msg, int targettype, int64_t targetid,
                                               const std::vector<ThreadReaction>& reactions)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v["targettype"] = targettype;
    v["targetid"] = idToString(targetid);
    v["reactions"] = reactionsToJson(reactions);
    return writeCompact(v);
}

std::string ThreadProtocol::buildReactorsResponse(int code, const std::string& msg, int targettype, int64_t targetid,
                                                  int reaction, const std::vector<int64_t>& users, int total)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v["targettype"] = targettype;
    v["targetid"] = idToString(targetid);
    v["reaction"] = reaction;
    v["users"] = idsToJson(users);
    v["total"] = total;
    return writeCompact(v);
}

std::string ThreadProtocol::buildAddendumResponse(int code, const std::string& msg, const ThreadAddendum* addendum)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    if (addendum != NULL)
        v["addendum"] = addendumToJson(*addendum);
    return writeCompact(v);
}

std::string ThreadProtocol::buildIdResponse(int code, const std::string& msg, const char* key, int64_t id)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v[key] = idToString(id);
    return writeCompact(v);
}

std::string ThreadProtocol::buildChangedNotify(int64_t groupid, int changetype, int64_t operatorId,
                                               const ThreadPost* post, int64_t postid, const ThreadComment* comment,
                                               int64_t commentid, const ThreadAddendum* addendum, int64_t addid,
                                               int commentCount, int64_t activeTime, const ReactChange* react)
{
    Json::Value v(Json::objectValue);
    v["groupid"] = idToString(groupid);
    v["changetype"] = changetype;
    v["operator"] = idToString(operatorId);
    v["postid"] = idToString(post != NULL ? post->postid : postid);
    if (post != NULL)
        v["post"] = postToJson(*post, false);
    if (comment != NULL || commentid > 0)
        v["commentid"] = idToString(comment != NULL ? comment->commentid : commentid);
    if (comment != NULL)
        v["comment"] = commentToJson(*comment);
    if (addendum != NULL || addid > 0)
        v["addid"] = idToString(addendum != NULL ? addendum->addid : addid);
    if (addendum != NULL)
        v["addendum"] = addendumToJson(*addendum);
    if (commentCount >= 0)
        v["commentcount"] = commentCount;
    if (activeTime > 0)
        v["activetime"] = static_cast<Json::Int64>(activeTime);
    if (react != NULL && react->targettype != 0)
    {
        v["targettype"] = react->targettype;
        v["targetid"] = idToString(react->targetid);
        v["reaction"] = react->reaction;
        v["on"] = react->on ? 1 : 0;
        v["reactions"] = reactionsToJson(react->summary);
    }
    return writeCompact(v);
}

//======================== 卡片 ========================

std::string ThreadProtocol::buildPostCardContent(const ThreadPost& post)
{
    Json::Value v(Json::objectValue);
    v["postid"] = idToString(post.postid);
    v["title"] = post.body.title;
    v["digest"] = makeDigest(post.body.text, kThreadDigestChars);
    v["imagecount"] = static_cast<int>(post.body.images.size());
    if (!post.body.mentions.empty())
        v["mentions"] = idsToJson(post.body.mentions);
    if (!post.body.images.empty())
        v["images"] = imagesToJson(post.body.images);
    //@ 了所有人：客户端据此把这张卡片当作 @ 到了每个成员（发布者除外）
    if (post.body.mentionall)
        v["mentionall"] = true;
    return writeCompact(v);
}

bool ThreadProtocol::buildDeletedPostCardContent(const std::string& content, std::string& out)
{
    out.clear();
    Json::Value root;
    if (!parseObject(content, root))
        return false;
    root.removeMember("title");
    root.removeMember("digest");
    root.removeMember("mentions");
    root.removeMember("mentionall");
    root.removeMember("images");
    root["imagecount"] = 0;
    root["deleted"] = 1;
    out = writeCompact(root);
    return true;
}

std::string ThreadProtocol::buildReplyCardContent(int64_t postid, int64_t commentid, int64_t addid,
                                                  const std::string& postTitle, const std::string& digest, int kind,
                                                  const std::vector<int64_t>& mentions,
                                                  const std::vector<ThreadImage>& images, bool mentionAll)
{
    Json::Value v(Json::objectValue);
    v["postid"] = idToString(postid);
    v["commentid"] = idToString(commentid);
    v["addid"] = idToString(addid);
    v["posttitle"] = postTitle;
    v["digest"] = digest;
    v["kind"] = kind;
    if (!mentions.empty())
        v["mentions"] = idsToJson(mentions);
    if (!images.empty())
        v["images"] = imagesToJson(images);
    if (mentionAll)
        v["mentionall"] = true;
    return writeCompact(v);
}

bool ThreadProtocol::buildDeletedReplyCardContent(const std::string& content, std::string& out)
{
    out.clear();
    Json::Value root;
    if (!parseObject(content, root))
        return false;
    root.removeMember("posttitle");
    root.removeMember("digest");
    root.removeMember("mentions");
    root.removeMember("mentionall");
    root.removeMember("images");
    root["deleted"] = 1;
    out = writeCompact(root);
    return true;
}

bool ThreadProtocol::isDeletedCardContent(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return false;
    const Json::Value& deleted = root["deleted"];
    if (deleted.isBool())
        return deleted.asBool();
    return deleted.isInt() && deleted.asInt() != 0;
}

bool ThreadProtocol::isDeletedCardOfGroup(int ctype, int64_t to, int msgtype, const std::string& content,
                                          int64_t groupid)
{
    if (ctype != conversation_group || to != groupid)
        return false;
    if (msgtype != message_type_thread_post && msgtype != message_type_thread_reply)
        return false;
    return isDeletedCardContent(content);
}

std::string ThreadProtocol::buildCheckCardsResponse(int code, const std::string& msg, int64_t groupid,
                                                    const std::vector<ThreadCardState>& cards)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v["groupid"] = idToString(groupid);
    v["cards"] = cardStatesToJson(cards);
    return writeCompact(v);
}

std::string ThreadProtocol::buildCardsNotify(int64_t groupid, const std::vector<ThreadCardState>& cards)
{
    Json::Value v(Json::objectValue);
    v["groupid"] = idToString(groupid);
    v["cards"] = cardStatesToJson(cards);
    return writeCompact(v);
}

std::string ThreadProtocol::postCardClientMsgId(int64_t postid)
{
    return "threadpost-" + idToString(postid);
}

std::string ThreadProtocol::commentCardClientMsgId(int64_t commentid)
{
    return "threadcomment-" + idToString(commentid);
}

std::string ThreadProtocol::addendumCardClientMsgId(int64_t addid)
{
    return "threadadd-" + idToString(addid);
}

std::string ThreadProtocol::displayTitle(const ThreadPost& post)
{
    if (!isBlank(post.body.title))
        return post.body.title;
    return makeDigest(post.body.text, kDisplayTitleChars);
}
