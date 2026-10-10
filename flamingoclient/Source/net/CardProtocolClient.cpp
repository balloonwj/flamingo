/**
 *  v2 群互动卡片报文编解码（客户端侧）实现，CardProtocolClient.cpp
 *  与服务端 chatserversrc/CardProtocol.cpp 互为镜像，字段名与校验规则逐字对应。
 *  balloonwj@qq.com   2026-10-02
 */
#include "CardProtocolClient.h"

#include <stdlib.h>                     // strtoll

#include "Protocol.h"   //conversation_type、relay_state 与 error_code_* 的具体取值
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

//UTF-8 续字节的判定：高两位为 10 的字节是多字节字符的后续字节，不单独构成一个字符
static const unsigned char kUtf8ContinuationMask = 0xC0;
static const unsigned char kUtf8ContinuationBits = 0x80;

//全角空格 U+3000 的 UTF-8 编码及其字节数，与半角空格一样在首尾去掉
static const char* const kFullwidthSpaceUtf8 = "\xE3\x80\x80";
static const size_t kFullwidthSpaceUtf8Len = 3;

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或不是对象时返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 JSON 里以十进制字符串编码的 64 位 ID；字段缺失、不是字符串时返回 0
static int64_t getStringId(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    if (!v.isString())
        return 0;
    return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
}

//取 JSON 里的 64 位整数（时间等）；字段缺失或不是整数时返回 0
static int64_t getInt64(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    if (!v.isInt64())
        return 0;
    return static_cast<int64_t>(v.asInt64());
}

//判断单个字节是否为可在首尾去掉的半角空白：空格、制表符、回车、换行
static bool isAsciiBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//text 从 pos 开始若是一个首尾可去掉的空白字符，返回它的字节数，否则返回 0
static size_t blankLengthAt(const std::string& text, size_t pos)
{
    if (isAsciiBlank(text[pos]))
        return 1;
    if (text.compare(pos, kFullwidthSpaceUtf8Len, kFullwidthSpaceUtf8) == 0)
        return kFullwidthSpaceUtf8Len;
    return 0;
}

//text 在区间 [begin, end) 内、紧挨 end 之前若是一个首尾可去掉的空白字符，返回它的字节数，
//否则返回 0。调用方保证 begin < end
static size_t blankLengthBefore(const std::string& text, size_t begin, size_t end)
{
    if (isAsciiBlank(text[end - 1]))
        return 1;
    if (end - begin >= kFullwidthSpaceUtf8Len
        && text.compare(end - kFullwidthSpaceUtf8Len, kFullwidthSpaceUtf8Len, kFullwidthSpaceUtf8) == 0)
        return kFullwidthSpaceUtf8Len;
    return 0;
}

//去掉 raw 首尾的空白（规则同服务端 CardProtocol 的 trimRelayBlank），返回剩下的部分
static std::string trimRelayBlank(const std::string& raw)
{
    size_t begin = 0;
    while (begin < raw.size())
    {
        const size_t blank = blankLengthAt(raw, begin);
        if (blank == 0)
            break;
        begin += blank;
    }

    size_t end = raw.size();
    while (end > begin)
    {
        const size_t blank = blankLengthBefore(raw, begin, end);
        if (blank == 0)
            break;
        end -= blank;
    }
    return raw.substr(begin, end - begin);
}

//判断文本中间是否含回车或换行。接龙文本与卡片标题都只允许一行
static bool containsLineBreak(const std::string& text)
{
    return text.find_first_of("\r\n") != std::string::npos;
}

size_t CardProtocolClient::utf8CharCount(const std::string& text)
{
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if ((b & kUtf8ContinuationMask) != kUtf8ContinuationBits)
            ++chars;
    }
    return chars;
}

int CardProtocolClient::buildRelayCardContent(const std::string& title, const std::string& note,
                                              int64_t deadline, std::string& out)
{
    //标题：必填的单行文本
    const std::string trimmedTitle = trimRelayBlank(title);
    if (trimmedTitle.empty() || containsLineBreak(trimmedTitle))
        return error_code_bad_request;
    if (utf8CharCount(trimmedTitle) > kRelayTitleMaxChars)
        return error_code_msg_too_large;

    //说明：选填的多行文本，中间的换行保留
    const std::string trimmedNote = trimRelayBlank(note);
    if (utf8CharCount(trimmedNote) > kRelayNoteMaxChars)
        return error_code_msg_too_large;

    //截止时间：0 表示不限；是否晚于当前时间由服务端按它的时钟判断
    if (deadline < 0)
        return error_code_bad_request;

    Json::Value content(Json::objectValue);
    content["title"]    = trimmedTitle;
    content["note"]     = trimmedNote;
    content["deadline"] = static_cast<Json::Int64>(deadline);
    out = writeCompact(content);
    return error_code_ok;
}

int CardProtocolClient::normalizeRelayText(const std::string& raw, std::string& out)
{
    const std::string trimmed = trimRelayBlank(raw);
    if (trimmed.empty())
        return error_code_bad_request;
    //每条接龙只有一行：中间的回车、换行一律拒绝（与服务端一致，2026-10-02 用户确认）
    if (containsLineBreak(trimmed))
        return error_code_bad_request;
    if (utf8CharCount(trimmed) > kRelayTextMaxChars)
        return error_code_msg_too_large;

    out = trimmed;
    return error_code_ok;
}

std::string CardProtocolClient::buildJoinRelayRequest(int ctype, int64_t cid, int64_t msgid,
                                                      const std::string& text)
{
    if (cid <= 0 || msgid <= 0 || text.empty())
        return std::string();

    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    root["text"]  = text;
    return writeCompact(root);
}

std::string CardProtocolClient::buildRelayTargetRequest(int ctype, int64_t cid, int64_t msgid)
{
    if (cid <= 0 || msgid <= 0)
        return std::string();

    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    return writeCompact(root);
}

bool CardProtocolClient::parseRelayOpResponse(const std::string& jsonbody, RelayOpResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    if (!root["code"].isInt())
        return false;

    out.m_code  = root["code"].asInt();
    out.m_msg   = root["msg"].isString() ? root["msg"].asString() : std::string();
    //请求格式错误时服务端只回 { code, msg }，三个定位字段缺失时取 0
    out.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 0;
    out.m_cid   = getStringId(root, "cid");
    out.m_msgid = getStringId(root, "msgid");
    return true;
}

//把 items 数组解析成名单：格式不对的个别条目（不是对象、userid 缺失或非正、text 不是字符串）
//跳过、其余照常（2026-10-02 用户确认），下次同步或推送时更正。推送与卡片内容共用
static void parseRelayItems(const Json::Value& items, std::vector<RelayItem>& out)
{
    out.clear();
    for (Json::ArrayIndex i = 0; i < items.size(); ++i)
    {
        const Json::Value& item = items[i];
        if (!item.isObject() || !item["text"].isString())
            continue;
        RelayItem entry;
        entry.m_userid = getStringId(item, "userid");
        if (entry.m_userid <= 0)
            continue;
        entry.m_text = item["text"].asString();
        out.push_back(entry);
    }
}

bool CardProtocolClient::parseRelayChangedNotify(const std::string& jsonbody, RelayChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    const int64_t cid = getStringId(root, "cid");
    const int64_t msgid = getStringId(root, "msgid");
    if (cid <= 0 || msgid <= 0)
        return false;

    const Json::Value& items = root["items"];
    if (!items.isArray())
        return false;

    out.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 0;
    out.m_cid   = cid;
    out.m_msgid = msgid;
    parseRelayItems(items, out.m_items);

    //状态缺失时按进行中处理：只有比 2026-10-02 更早的服务端才不带这三个字段
    out.m_state     = root["state"].isInt() ? root["state"].asInt() : relay_state_open;
    out.m_closedBy  = getStringId(root, "closedby");
    out.m_closeTime = getInt64(root, "closetime");
    return true;
}

bool CardProtocolClient::parseRelayCardContent(const std::string& contentJson, RelayCardContent& out)
{
    Json::Value root;
    if (!parseObject(contentJson, root))
        return false;

    out.m_title    = root["title"].isString() ? root["title"].asString() : std::string();
    out.m_note     = root["note"].isString() ? root["note"].asString() : std::string();
    out.m_deadline = getInt64(root, "deadline");

    //实时推送的新卡片不带 items：名单为空
    out.m_items.clear();
    if (root["items"].isArray())
        parseRelayItems(root["items"], out.m_items);

    out.m_state     = root["state"].isInt() ? root["state"].asInt() : relay_state_open;
    out.m_closedBy  = getStringId(root, "closedby");
    out.m_closeTime = getInt64(root, "closetime");
    return true;
}

std::string CardProtocolClient::serializeRelayCardContent(const RelayCardContent& card)
{
    Json::Value items(Json::arrayValue);
    for (size_t i = 0; i < card.m_items.size(); ++i)
    {
        Json::Value item;
        item["userid"] = std::to_string(card.m_items[i].m_userid);
        item["text"]   = card.m_items[i].m_text;
        items.append(item);
    }

    //字段与服务端 CardProtocol::augmentRelayContent 的输出同构
    Json::Value root;
    root["title"]     = card.m_title;
    root["note"]      = card.m_note;
    root["deadline"]  = static_cast<Json::Int64>(card.m_deadline);
    root["items"]     = items;
    root["state"]     = card.m_state;
    root["closedby"]  = std::to_string(card.m_closedBy);
    root["closetime"] = static_cast<Json::Int64>(card.m_closeTime);
    return writeCompact(root);
}

} // namespace net
