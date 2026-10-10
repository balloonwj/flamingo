/**
 *  从消息内容生成用于关键字搜索的纯文本。整体说明见 MessageSearchText.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "MessageSearchText.h"

#include "Protocol.h"                  // message_type 各取值
#include "../jsoncpp1.9.0/json.h"

namespace chatserver {

namespace {

// 内联表情换成的文字，与客户端搜索文本里的占位一致（客户端中文界面下也是这四个字）。
const char* const kFacePlaceholder = "[表情]";

// UTF-8 首字节的判别掩码与取值。
const unsigned char kAsciiMask        = 0x80;   // 最高位为 0 的是单字节 ASCII
const unsigned char kLeadMask2        = 0xE0;   // 110xxxxx：两字节字符的首字节
const unsigned char kLeadBits2        = 0xC0;
const unsigned char kLeadMask3        = 0xF0;   // 1110xxxx：三字节字符的首字节
const unsigned char kLeadBits3        = 0xE0;
const unsigned char kLeadMask4        = 0xF8;   // 11110xxx：四字节字符的首字节
const unsigned char kLeadBits4        = 0xF0;
const unsigned char kContinuationMask = 0xC0;   // 10xxxxxx：续字节
const unsigned char kContinuationBits = 0x80;

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

// 取对象里的字符串字段；缺失或不是字符串时返回空串。
std::string GetString(const Json::Value& obj, const char* key)
{
    const Json::Value& v = obj[key];
    return v.isString() ? v.asString() : std::string();
}

// 从一条消息的 content 对象生成搜索文本（未去四字节字符、未截断）。depth 为合并转发的嵌套层数。
std::string RawSearchText(int32_t msgtype, const Json::Value& content, int depth)
{
    if (!content.isObject())
    {
        return std::string();
    }
    switch (msgtype)
    {
    //文本：正文，内联表情换成占位文字
    case message_type_text:
        return DigestFaceTokensForSearch(GetString(content, "text"));
    //系统提示：提示文字
    case message_type_system:
        return GetString(content, "text");
    //文件：文件名
    case message_type_file:
        return GetString(content, "filename");
    //文件夹传输记录：文件夹名
    case message_type_folder:
        return GetString(content, "foldername");
    //文件共享卡片：共享名
    case message_type_fileshare:
        return GetString(content, "name");
    //云端录像卡片：录像标题
    case message_type_cloudrecord:
        return GetString(content, "title");
    //群接龙卡片：标题与说明
    case message_type_relay:
    {
        const std::string title = GetString(content, "title");
        const std::string note = GetString(content, "note");
        if (note.empty())
        {
            return title;
        }
        return title + " " + note;
    }
    //群公告卡片：标题与正文。已删除形态没有 body，只剩标题，正文也就搜不到了
    case message_type_announcement:
    {
        const std::string title = GetString(content, "title");
        const std::string body = GetString(content, "body");
        if (body.empty())
        {
            return title;
        }
        return title + " " + body;
    }
    //群备忘卡片：标题与备注
    case message_type_memo:
    {
        const std::string title = GetString(content, "title");
        const std::string note = GetString(content, "note");
        if (note.empty())
        {
            return title;
        }
        return title + " " + note;
    }
    //合并转发：标题加上每条子消息的搜索文本，嵌套的记录逐层展开
    case message_type_merged:
    {
        std::string text = GetString(content, "title");
        const Json::Value& messages = content["messages"];
        if (!messages.isArray() || depth >= kSearchTextMaxMergedDepth)
        {
            return text;
        }
        for (Json::ArrayIndex i = 0; i < messages.size(); ++i)
        {
            const Json::Value& item = messages[i];
            if (!item.isObject() || !item["msgtype"].isInt())
            {
                continue;
            }
            const std::string part = RawSearchText(item["msgtype"].asInt(), item["content"], depth + 1);
            if (part.empty())
            {
                continue;
            }
            if (!text.empty())
            {
                text += " ";
            }
            text += part;
            //已经超过上限就不再展开，结果最后统一截断
            if (text.size() > kSearchTextMaxBytes)
            {
                break;
            }
        }
        return text;
    }
    //其余类型（图片、语音、通话记录、屏幕共享卡片等）没有可搜的文字
    default:
        return std::string();
    }
}

// 去掉四字节字符并截断：两个函数都要的收尾。
std::string Finish(const std::string& raw)
{
    return TruncateUtf8(StripFourByteUtf8(raw), kSearchTextMaxBytes);
}

} // namespace

std::string BuildStoredSearchText(int32_t msgtype, const std::string& storedContent)
{
    Json::Value root;
    if (!ParseObject(storedContent, root))
    {
        return std::string();
    }
    return Finish(RawSearchText(msgtype, root["content"], 0));
}

std::string BuildContentSearchText(int32_t msgtype, const std::string& contentJson)
{
    Json::Value content;
    if (!ParseObject(contentJson, content))
    {
        return std::string();
    }
    return Finish(RawSearchText(msgtype, content, 0));
}

std::string DigestFaceTokensForSearch(const std::string& body)
{
    std::string out;
    out.reserve(body.size());
    const size_t n = body.size();
    size_t i = 0;
    while (i < n)
    {
        //转义斜杠「//」→ 字面「/」
        if (body[i] == '/' && i + 1 < n && body[i + 1] == '/')
        {
            out.push_back('/');
            i += 2;
            continue;
        }
        //系统表情 /f["<数字>"] → 占位文字
        if (body[i] == '/' && i + 3 < n && body[i + 1] == 'f' && body[i + 2] == '[' && body[i + 3] == '"')
        {
            size_t j = i + 4;
            const size_t digitsStart = j;
            while (j < n && body[j] >= '0' && body[j] <= '9')
            {
                ++j;
            }
            if (j > digitsStart && j + 1 < n && body[j] == '"' && body[j + 1] == ']')
            {
                out += kFacePlaceholder;
                i = j + 2;
                continue;
            }
        }
        //其余字符（含中文多字节，其字节不含 '/'）原样保留；残缺的 /x... 当普通文字
        out.push_back(body[i]);
        ++i;
    }
    return out;
}

std::string StripFourByteUtf8(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    const size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        const unsigned char ch = (unsigned char)text[i];
        size_t len = 0;
        if ((ch & kAsciiMask) == 0)
        {
            len = 1;
        }
        else if ((ch & kLeadMask2) == kLeadBits2)
        {
            len = 2;
        }
        else if ((ch & kLeadMask3) == kLeadBits3)
        {
            len = 3;
        }
        else if ((ch & kLeadMask4) == kLeadBits4)
        {
            len = 4;
        }
        //非法首字节（孤立的续字节等）：丢掉这一个字节
        if (len == 0)
        {
            ++i;
            continue;
        }
        //续字节不够或不对：丢掉首字节，从下一个字节重新判断
        bool complete = (i + len <= n);
        for (size_t k = 1; complete && k < len; ++k)
        {
            if (((unsigned char)text[i + k] & kContinuationMask) != kContinuationBits)
            {
                complete = false;
            }
        }
        if (!complete)
        {
            ++i;
            continue;
        }
        const size_t kMaxStoredLen = 3;
        if (len <= kMaxStoredLen)
        {
            out.append(text, i, len);
        }
        i += len;
    }
    return out;
}

std::string TruncateUtf8(const std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes)
    {
        return text;
    }
    size_t end = maxBytes;
    //截断点落在续字节上时往前退到这个字符的首字节
    while (end > 0 && ((unsigned char)text[end] & kContinuationMask) == kContinuationBits)
    {
        --end;
    }
    return text.substr(0, end);
}

std::string MakeLikeContainsPattern(const std::string& keyword)
{
    std::string pattern = "%";
    for (size_t i = 0; i < keyword.size(); ++i)
    {
        const char ch = keyword[i];
        //UTF-8 多字节字符的每个字节都不小于 0x80，不会与这三个 ASCII 字符混淆，逐字节处理即可
        if (ch == '!' || ch == '%' || ch == '_')
        {
            pattern.push_back('!');
        }
        pattern.push_back(ch);
    }
    pattern += "%";
    return pattern;
}

std::string MakeSearchExcerpt(const std::string& text, const std::string& keyword, size_t maxBytes)
{
    const char* const kEllipsis = "\xE2\x80\xA6";   //「…」U+2026 的 UTF-8
    const size_t kEllipsisBytes = 3;
    const size_t kLeadDivisor = 4;   //关键字前面保留整段的四分之一
    if (maxBytes < kExcerptMinBytes)
    {
        maxBytes = kExcerptMinBytes;
    }

    //找关键字：ASCII 字母不区分大小写，其余字节逐个比较
    size_t pos = std::string::npos;
    if (!keyword.empty() && keyword.size() <= text.size())
    {
        for (size_t i = 0; i + keyword.size() <= text.size() && pos == std::string::npos; ++i)
        {
            bool same = true;
            for (size_t k = 0; k < keyword.size() && same; ++k)
            {
                char a = text[i + k];
                char b = keyword[k];
                if (a >= 'A' && a <= 'Z')
                {
                    a = (char)(a - 'A' + 'a');
                }
                if (b >= 'A' && b <= 'Z')
                {
                    b = (char)(b - 'A' + 'a');
                }
                same = (a == b);
            }
            if (same)
            {
                pos = i;
            }
        }
    }

    //起点：关键字前面留一段，落在续字节上时往后挪到下一个字符的首字节
    size_t start = 0;
    const size_t lead = maxBytes / kLeadDivisor;
    if (pos != std::string::npos && pos > lead)
    {
        start = pos - lead;
        while (start < text.size() && ((unsigned char)text[start] & kContinuationMask) == kContinuationBits)
        {
            ++start;
        }
    }

    std::string out;
    size_t budget = maxBytes;
    if (start > 0)
    {
        out = kEllipsis;
        budget -= kEllipsisBytes;
    }
    const std::string rest = text.substr(start);
    if (rest.size() <= budget)
    {
        out += rest;
    }
    else
    {
        out += TruncateUtf8(rest, budget - kEllipsisBytes);
        out += kEllipsis;
    }

    //换行等控制字符换成空格，结果行只显示一行
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '\r' || out[i] == '\n' || out[i] == '\t')
        {
            out[i] = ' ';
        }
    }
    return out;
}

} // namespace chatserver
