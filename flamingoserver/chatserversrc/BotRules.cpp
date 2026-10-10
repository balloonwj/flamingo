/**
 *  机器人的纯逻辑实现，BotRules.cpp。设计与约定见 BotRules.h 与 docs/机器人方案.md。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotRules.h"

#include <ctype.h>   //isspace：去掉 Webhook 根地址首尾的空白
#include <time.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sodium.h>   //randombytes_buf、crypto_auth_hmacsha256：令牌与签名

#include "Protocol.h" //presence_type、message_type

namespace bot {

namespace {

//======== UTF-8 编解码 ========

//把 UTF-8 文字解成码点；非法字节原样当作一个码点（取字节值），保证不丢字符
void DecodeUtf8(const std::string& s, std::vector<uint32_t>& out)
{
    out.clear();
    size_t i = 0;
    const size_t n = s.size();
    while (i < n)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        size_t len = 1;
        if (c >= 0xF0 && c <= 0xF7)
        {
            len = 4;
            cp = c & 0x07;
        }
        else if (c >= 0xE0)
        {
            len = 3;
            cp = c & 0x0F;
        }
        else if (c >= 0xC0)
        {
            len = 2;
            cp = c & 0x1F;
        }
        //后续字节不足或不是 10xxxxxx 时按单字节处理
        bool ok = (len > 1) && (i + len <= n);
        for (size_t k = 1; ok && k < len; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                ok = false;
            else
                cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok)
        {
            out.push_back(c);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
}

//把码点编回 UTF-8
std::string EncodeUtf8(const std::vector<uint32_t>& cps)
{
    std::string out;
    out.reserve(cps.size() * 3);
    for (size_t i = 0; i < cps.size(); ++i)
    {
        const uint32_t cp = cps[i];
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

//全角 ASCII 区段（U+FF01～U+FF5E）与半角之间的偏移
const uint32_t kFullwidthFirst = 0xFF01;
const uint32_t kFullwidthLast = 0xFF5E;
const uint32_t kFullwidthOffset = 0xFEE0;
//全角空格
const uint32_t kIdeographicSpace = 0x3000;

//是否为空白（半角空格、制表、回车、换行、全角空格）
bool IsSpace(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == kIdeographicSpace;
}

//是否为标点：ASCII 标点、中文标点（U+3000～U+303F）、全角标点（转半角前的 U+FF00 段已由归一化处理）、
//常见的弯引号、省略号与间隔号
bool IsPunct(uint32_t cp)
{
    if (cp < 0x80)
        return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x60)
               || (cp >= 0x7B && cp <= 0x7E);
    if (cp >= 0x3000 && cp <= 0x303F)
        return true;
    if (cp >= 0xFF00 && cp <= 0xFF0F)
        return true;
    if (cp >= 0x2010 && cp <= 0x2027)
        return true;
    return cp == 0x00B7 || cp == 0xFF1F || cp == 0xFF01 || cp == 0xFF0C || cp == 0xFF1A || cp == 0xFF1B;
}

//归一化一个码点：全角 ASCII 转半角、全角空格转半角空格、ASCII 大写转小写
uint32_t NormalizeCp(uint32_t cp)
{
    if (cp >= kFullwidthFirst && cp <= kFullwidthLast)
        cp -= kFullwidthOffset;
    else if (cp == kIdeographicSpace)
        cp = ' ';
    if (cp >= 'A' && cp <= 'Z')
        cp = cp - 'A' + 'a';
    return cp;
}

//归一化并去掉空白与标点之后的码点序列（问答比较、相似度用）
void Canonical(const std::string& s, std::vector<uint32_t>& out)
{
    std::vector<uint32_t> cps;
    DecodeUtf8(s, cps);
    out.clear();
    for (size_t i = 0; i < cps.size(); ++i)
    {
        const uint32_t cp = NormalizeCp(cps[i]);
        if (IsSpace(cp) || IsPunct(cp))
            continue;
        out.push_back(cp);
    }
}

//去掉首尾空白（含全角空格）
std::string TrimText(const std::string& s)
{
    std::vector<uint32_t> cps;
    DecodeUtf8(s, cps);
    size_t b = 0;
    size_t e = cps.size();
    while (b < e && IsSpace(cps[b]))
        ++b;
    while (e > b && IsSpace(cps[e - 1]))
        --e;
    std::vector<uint32_t> mid(cps.begin() + b, cps.begin() + e);
    return EncodeUtf8(mid);
}

//======== JSON ========

//把文本解析成 JSON 对象
bool ParseJsonObject(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs))
        return false;
    return root.isObject();
}

//JSON 紧凑输出
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, v);
}

//读一个整数：数字或数字字符串都接受；缺失或不合法时取默认值
int64_t ReadInt64(const Json::Value& v, int64_t def)
{
    if (v.isInt64())
        return v.asInt64();
    if (v.isUInt64())
        return static_cast<int64_t>(v.asUInt64());
    if (v.isDouble())
        return static_cast<int64_t>(v.asDouble());
    if (v.isString())
    {
        const std::string s = v.asString();
        if (s.empty())
            return def;
        char* end = NULL;
        const long long n = strtoll(s.c_str(), &end, 10);
        if (end == NULL || *end != '\0')
            return def;
        return static_cast<int64_t>(n);
    }
    if (v.isBool())
        return v.asBool() ? 1 : 0;
    return def;
}

//读一个 int
int ReadInt(const Json::Value& v, int def)
{
    return static_cast<int>(ReadInt64(v, def));
}

//读一个布尔：true / 1 / "1" 为真
bool ReadBool(const Json::Value& v, bool def)
{
    if (v.isNull())
        return def;
    return ReadInt64(v, def ? 1 : 0) != 0;
}

//读一个字符串；不是字符串时取空
std::string ReadString(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

//读一组 id（数字或字符串）
void ReadIdArray(const Json::Value& v, std::vector<int64_t>& out)
{
    out.clear();
    if (!v.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
    {
        const int64_t id = ReadInt64(v[i], 0);
        if (id != 0)
            out.push_back(id);
    }
}

//写一组 id（字符串编码，避免大整数精度丢失）
Json::Value IdArrayToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(std::to_string(static_cast<long long>(ids[i])));
    return arr;
}

//读一组整数
void ReadIntArray(const Json::Value& v, std::vector<int>& out)
{
    out.clear();
    if (!v.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
        out.push_back(ReadInt(v[i], -1));
}

//写一组整数
Json::Value IntArrayToJson(const std::vector<int>& vals)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < vals.size(); ++i)
        arr.append(vals[i]);
    return arr;
}

//字符串转小写（ASCII）
std::string ToLowerAscii(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

//把字节转成十六进制小写
std::string ToHex(const unsigned char* data, size_t len)
{
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

//替换全部出现的子串
void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

//一个数是否在列表里
template <typename T>
bool Contains(const std::vector<T>& v, const T& x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

//日期文字 YYYY-MM-DD
std::string FormatDate(const LocalClock& clock)
{
    char buf[16] = { 0 };
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", clock.m_year, clock.m_month, clock.m_day);
    return buf;
}

//一周的工作日最后一天（周五）
const int kLastWorkday = 5;
//一周天数
const int kDaysPerWeek = 7;
//每分钟的毫秒数
const int64_t kMsPerMinute = 60000;
//每小时分钟数
const int kMinutesPerHour = 60;
//小时上限（不含）
const int kHoursPerDay = 24;
//一个月最多天数
const int kMaxMonthDay = 31;
//令牌与密钥的随机字节数（转成十六进制后 32 位）
const size_t kRandomBytes = 16;

//某个星期几是否属于给定的日子
bool DayMatches(int days, int weekday)
{
    switch (days)
    {
    //工作日：周一至周五
    case bot_days_workdays:
        return weekday >= 1 && weekday <= kLastWorkday;
    //周末：周六、周日
    case bot_days_weekend:
        return weekday > kLastWorkday;
    //每天，以及不认识的取值
    default:
        return true;
    }
}

//前一天是星期几
int PrevWeekday(int weekday)
{
    return weekday == 1 ? kDaysPerWeek : weekday - 1;
}

//规则是否有该触发方式的启用规则
bool HasEnabledTrigger(const BotRecord& b, int type)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        if (b.m_rules[i].m_enabled && b.m_rules[i].m_trigger.m_type == type)
            return true;
    }
    return false;
}

//默认文字
const char* const kDefaultQaNotFound = u8"没有找到相关问答。";
const char* const kQaSuggestHead = u8"没有找到相关问答，你可以试试：";
const char* const kQaQuestionPrefix = u8"问：";
const char* const kQaAnswerPrefix = u8"答：";
const char* const kPrefixRobot = u8"[机器人] ";
const char* const kPrefixAutoReply = u8"[自动回复] ";
const char* const kDefaultAwayText = u8"您好，我现在不在，稍后回复您。";
const char* const kDefaultBusyText = u8"您好，我正在忙，稍后回复您。";
const char* const kMemberSeparator = u8"、";
const char* const kVarSender = u8"{发送者}";
const char* const kVarOwner = u8"{主人}";
const char* const kVarDate = u8"{日期}";
const char* const kVarNewMembers = u8"{新成员}";
//预置规则的内部名称（不显示给用户）
const char* const kAutoReplyAwayName = "away";
const char* const kAutoReplyBusyName = "busy";

} // anonymous namespace

//======== 文字 ========

int Utf8Length(const std::string& s)
{
    std::vector<uint32_t> cps;
    DecodeUtf8(s, cps);
    return static_cast<int>(cps.size());
}

std::string NormalizeText(const std::string& s)
{
    std::vector<uint32_t> cps;
    DecodeUtf8(s, cps);
    for (size_t i = 0; i < cps.size(); ++i)
        cps[i] = NormalizeCp(cps[i]);
    size_t b = 0;
    size_t e = cps.size();
    while (b < e && IsSpace(cps[b]))
        ++b;
    while (e > b && IsSpace(cps[e - 1]))
        --e;
    std::vector<uint32_t> mid(cps.begin() + b, cps.begin() + e);
    return EncodeUtf8(mid);
}

bool ParseHhMm(const std::string& hhmm, int& minute)
{
    minute = 0;
    const size_t colon = hhmm.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= hhmm.size())
        return false;
    const std::string hs = hhmm.substr(0, colon);
    const std::string ms = hhmm.substr(colon + 1);
    if (hs.size() > 2 || ms.size() != 2)
        return false;
    for (size_t i = 0; i < hs.size(); ++i)
    {
        if (hs[i] < '0' || hs[i] > '9')
            return false;
    }
    for (size_t i = 0; i < ms.size(); ++i)
    {
        if (ms[i] < '0' || ms[i] > '9')
            return false;
    }
    const int h = atoi(hs.c_str());
    const int m = atoi(ms.c_str());
    if (h >= kHoursPerDay || m >= kMinutesPerHour)
        return false;
    minute = h * kMinutesPerHour + m;
    return true;
}

std::string FormatHhMm(int minute)
{
    if (minute < 0)
        minute = 0;
    minute %= kMinutesPerDay;
    char buf[8] = { 0 };
    snprintf(buf, sizeof(buf), "%02d:%02d", minute / kMinutesPerHour, minute % kMinutesPerHour);
    return buf;
}

//======== 消息内容 ========

bool ParseMessageContent(const std::string& contentJson, MessageView& out)
{
    out = MessageView();
    Json::Value root;
    if (!ParseJsonObject(contentJson, root))
        return false;
    out.m_text = ReadString(root["text"]);
    out.m_botMarked = root.isMember("bot");
    out.m_autoReply = root["autoreply"].isBool() && root["autoreply"].asBool();

    //content.at：{"all":0|1, "users":[{"uid":"…","off":字节偏移,"len":字节数}]}，偏移按 UTF-8 字节计
    std::vector<std::pair<size_t, size_t> > spans;
    const Json::Value& at = root["at"];
    if (at.isObject())
    {
        out.m_atAll = ReadInt(at["all"], 0) != 0;
        const Json::Value& users = at["users"];
        if (users.isArray())
        {
            for (Json::ArrayIndex i = 0; i < users.size(); ++i)
            {
                const Json::Value& u = users[i];
                if (!u.isObject())
                    continue;
                const int64_t uid = ReadInt64(u["uid"], 0);
                if (uid > 0 && !Contains(out.m_atUsers, uid))
                    out.m_atUsers.push_back(uid);
                const int64_t off = ReadInt64(u["off"], -1);
                const int64_t len = ReadInt64(u["len"], 0);
                if (off >= 0 && len > 0 && static_cast<size_t>(off + len) <= out.m_text.size())
                    spans.push_back(std::make_pair(static_cast<size_t>(off), static_cast<size_t>(len)));
            }
        }
    }

    //按偏移从后往前剪，前面的偏移不受影响；片段重叠时跳过后来的那段
    std::sort(spans.begin(), spans.end());
    std::string rest = out.m_text;
    size_t lastStart = rest.size() + 1;
    for (size_t i = spans.size(); i > 0; --i)
    {
        const size_t off = spans[i - 1].first;
        const size_t len = spans[i - 1].second;
        if (off + len > lastStart)
            continue;
        rest.erase(off, len);
        lastStart = off;
    }
    out.m_textWithoutAt = rest;
    return true;
}

bool StripBotMarker(std::string& contentJson)
{
    Json::Value root;
    if (!ParseJsonObject(contentJson, root))
        return false;
    if (!root.isMember("bot"))
        return false;
    root.removeMember("bot");
    contentJson = WriteCompact(root);
    return true;
}

bool IsTriggerableMsgType(int msgtype)
{
    switch (msgtype)
    {
    //用户自己发出的内容：文字、图片、文件、语音、短视频、表情、位置、名片、截图、抖一抖、合并转发
    case message_type_text:
    case message_type_image:
    case message_type_file:
    case message_type_voice:
    case message_type_video:
    case message_type_emoji:
    case message_type_location:
    case message_type_card:
    case message_type_screenshot:
    case message_type_shake:
    case message_type_merged:
        return true;
    //服务端生成的卡片、系统提示、通话与文件夹记录、投票接龙待办卡片、红包等一律不触发
    default:
        return false;
    }
}

//======== 关键词与问答 ========

bool MatchKeyword(const std::string& normalizedText, const std::string& keyword, int mode)
{
    const std::string kw = NormalizeText(keyword);
    if (kw.empty())
        return false;
    switch (mode)
    {
    //完全相同
    case bot_match_exact:
        return normalizedText == kw;
    //以关键词开头
    case bot_match_prefix:
        return normalizedText.compare(0, kw.size(), kw) == 0;
    //包含，以及不认识的取值
    default:
        return normalizedText.find(kw) != std::string::npos;
    }
}

bool MatchAnyKeyword(const std::string& text, const std::vector<std::string>& keywords, int mode)
{
    const std::string norm = NormalizeText(text);
    for (size_t i = 0; i < keywords.size(); ++i)
    {
        if (MatchKeyword(norm, keywords[i], mode))
            return true;
    }
    return false;
}

double TextSimilarity(const std::string& a, const std::string& b)
{
    std::vector<uint32_t> ca;
    std::vector<uint32_t> cb;
    Canonical(a, ca);
    Canonical(b, cb);
    if (ca.empty() || cb.empty())
        return 0;
    //任一方只有一个字时按单字比较，否则按相邻两字组成的二元组比较
    const bool unigram = ca.size() < 2 || cb.size() < 2;
    std::map<uint64_t, int> grams;
    size_t countA = 0;
    if (unigram)
    {
        for (size_t i = 0; i < ca.size(); ++i)
            ++grams[ca[i]];
        countA = ca.size();
    }
    else
    {
        for (size_t i = 0; i + 1 < ca.size(); ++i)
            ++grams[(static_cast<uint64_t>(ca[i]) << 32) | ca[i + 1]];
        countA = ca.size() - 1;
    }
    size_t countB = 0;
    size_t common = 0;
    const size_t gramsB = unigram ? cb.size() : cb.size() - 1;
    for (size_t i = 0; i < gramsB; ++i)
    {
        const uint64_t key = unigram ? cb[i] : ((static_cast<uint64_t>(cb[i]) << 32) | cb[i + 1]);
        ++countB;
        std::map<uint64_t, int>::iterator it = grams.find(key);
        if (it != grams.end() && it->second > 0)
        {
            --it->second;
            ++common;
        }
    }
    return 2.0 * static_cast<double>(common) / static_cast<double>(countA + countB);
}

QaAnswer AnswerFromQa(const std::string& question, const std::vector<QaItem>& items)
{
    QaAnswer ans;
    std::vector<uint32_t> cq;
    Canonical(question, cq);
    if (cq.empty() || items.empty())
        return ans;

    //先找完全相同（归一化、去掉空白与标点之后）的问题
    for (size_t i = 0; i < items.size(); ++i)
    {
        std::vector<uint32_t> ci;
        Canonical(items[i].m_question, ci);
        if (!ci.empty() && ci == cq)
        {
            ans.m_kind = qa_answer_found;
            ans.m_index = i;
            ans.m_score = 1;
            return ans;
        }
    }

    //再按相似度打分，同分时下标小的（本群问答在前）优先
    std::vector<std::pair<double, size_t> > scored;
    for (size_t i = 0; i < items.size(); ++i)
        scored.push_back(std::make_pair(TextSimilarity(question, items[i].m_question), i));
    size_t best = 0;
    for (size_t i = 1; i < scored.size(); ++i)
    {
        if (scored[i].first > scored[best].first)
            best = i;
    }
    if (scored[best].first >= kQaAnswerThreshold)
    {
        ans.m_kind = qa_answer_found;
        ans.m_index = scored[best].second;
        ans.m_score = scored[best].first;
        return ans;
    }

    //都不够时列出最相近的几个问题；排序按得分从高到低、同分按下标
    std::vector<std::pair<double, size_t> > cands;
    for (size_t i = 0; i < scored.size(); ++i)
    {
        if (scored[i].first >= kQaSuggestThreshold)
            cands.push_back(std::make_pair(-scored[i].first, scored[i].second));
    }
    std::sort(cands.begin(), cands.end());
    for (size_t i = 0; i < cands.size() && ans.m_suggestions.size() < kQaSuggestMax; ++i)
        ans.m_suggestions.push_back(cands[i].second);
    if (!ans.m_suggestions.empty())
        ans.m_kind = qa_answer_suggestions;
    return ans;
}

std::string BuildQaReplyText(const QaAnswer& answer, const std::vector<QaItem>& items, const std::string& fallback)
{
    if (answer.m_kind == qa_answer_found && answer.m_index < items.size())
    {
        const QaItem& item = items[answer.m_index];
        return std::string(kQaQuestionPrefix) + item.m_question + "\n" + kQaAnswerPrefix + item.m_answer;
    }
    if (answer.m_kind == qa_answer_suggestions)
    {
        std::string text = kQaSuggestHead;
        for (size_t i = 0; i < answer.m_suggestions.size(); ++i)
        {
            const size_t idx = answer.m_suggestions[i];
            if (idx >= items.size())
                continue;
            text += "\n" + std::to_string(static_cast<long long>(i + 1)) + ". " + items[idx].m_question;
        }
        return text;
    }
    const std::string fb = TrimText(fallback);
    return fb.empty() ? std::string(kDefaultQaNotFound) : fallback;
}

//======== 时间 ========

LocalClock MakeLocalClock(int64_t unixMs)
{
    const time_t t = static_cast<time_t>(unixMs / 1000);
    struct tm tmv;
#ifdef WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    LocalClock c;
    c.m_year = tmv.tm_year + 1900;
    c.m_month = tmv.tm_mon + 1;
    c.m_day = tmv.tm_mday;
    c.m_weekday = tmv.tm_wday == 0 ? kDaysPerWeek : tmv.tm_wday;
    c.m_minuteOfDay = tmv.tm_hour * kMinutesPerHour + tmv.tm_min;
    return c;
}

bool ScheduleDue(const BotSchedule& schedule, const LocalClock& clock)
{
    if (schedule.m_minuteOfDay != clock.m_minuteOfDay)
        return false;
    switch (schedule.m_kind)
    {
    //每天
    case bot_schedule_daily:
        return true;
    //工作日
    case bot_schedule_workdays:
        return clock.m_weekday >= 1 && clock.m_weekday <= kLastWorkday;
    //每周所选的几天
    case bot_schedule_weekly:
        return Contains(schedule.m_weekdays, clock.m_weekday);
    //每月某一号
    case bot_schedule_monthly:
        return schedule.m_monthDay == clock.m_day;
    //不认识的种类不发
    default:
        return false;
    }
}

std::string ScheduleMinuteKey(const LocalClock& clock)
{
    char buf[16] = { 0 };
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d", clock.m_year, clock.m_month, clock.m_day,
             clock.m_minuteOfDay / kMinutesPerHour, clock.m_minuteOfDay % kMinutesPerHour);
    return buf;
}

bool InTimeWindow(const BotTimeWindow& window, const LocalClock& clock)
{
    if (!window.m_enabled)
        return true;
    const int now = clock.m_minuteOfDay;
    //起止相同表示全天
    if (window.m_fromMinute == window.m_toMinute)
        return DayMatches(window.m_days, clock.m_weekday);
    if (window.m_fromMinute < window.m_toMinute)
        return now >= window.m_fromMinute && now < window.m_toMinute && DayMatches(window.m_days, clock.m_weekday);
    //跨零点：开始时刻之后算当天的时间段，结束时刻之前算前一天的时间段（如周五 18:00～周六 09:00 属于周五）
    if (now >= window.m_fromMinute)
        return DayMatches(window.m_days, clock.m_weekday);
    if (now < window.m_toMinute)
        return DayMatches(window.m_days, PrevWeekday(clock.m_weekday));
    return false;
}

int MergePresence(const std::vector<int>& presences)
{
    //在线 > 忙碌 > 离开 > 离线
    int best = presence_offline;
    int bestRank = 0;
    for (size_t i = 0; i < presences.size(); ++i)
    {
        int rank = 0;
        switch (presences[i])
        {
        //在线
        case presence_online:
            rank = 3;
            break;
        //忙碌（与请勿打扰同值）
        case presence_busy:
            rank = 2;
            break;
        //离开
        case presence_away:
            rank = 1;
            break;
        //离线与不认识的取值
        default:
            rank = 0;
            break;
        }
        if (rank > bestRank)
        {
            bestRank = rank;
            best = presences[i];
        }
    }
    return best;
}

bool SenderInScope(const BotSenderScope& scope, int64_t senderId, const std::vector<int64_t>& senderDepts)
{
    switch (scope.m_kind)
    {
    //指定的人
    case bot_sender_users:
        return Contains(scope.m_users, senderId);
    //指定的部门：发送者所在部门或其上级部门在列表里
    case bot_sender_depts:
        for (size_t i = 0; i < senderDepts.size(); ++i)
        {
            if (Contains(scope.m_depts, senderDepts[i]))
                return true;
        }
        return false;
    //所有人，以及不认识的取值
    default:
        return true;
    }
}

bool MentionsOtherBot(const MessageView& view, int64_t selfUserId, const std::vector<int64_t>& groupBotUserIds)
{
    bool other = false;
    for (size_t i = 0; i < view.m_atUsers.size(); ++i)
    {
        const int64_t uid = view.m_atUsers[i];
        //@ 了自己：这条消息本来就是问自己的，照常判定
        if (uid == selfUserId)
            return false;
        if (std::find(groupBotUserIds.begin(), groupBotUserIds.end(), uid) != groupBotUserIds.end())
            other = true;
    }
    return other;
}

std::string ExpandVariables(const std::string& text, const std::string& sender, const std::string& owner,
                            const std::string& date, const std::string& newMembers)
{
    std::string out = text;
    ReplaceAll(out, kVarSender, sender);
    ReplaceAll(out, kVarOwner, owner);
    ReplaceAll(out, kVarDate, date);
    ReplaceAll(out, kVarNewMembers, newMembers);
    return out;
}

//======== 判定 ========

BotDecision EvaluateGroupMessage(const BotRecord& b, const MessageView& msg, const LocalClock& clock,
                                 const std::vector<QaItem>* qa, const std::string& senderName)
{
    BotDecision d;
    if (msg.m_botMarked || msg.m_autoReply)
    {
        d.m_reason = bot_reason_not_triggerable;
        return d;
    }
    const bool atMe = b.m_userId > 0 && Contains(msg.m_atUsers, static_cast<int64_t>(b.m_userId));
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (!r.m_enabled)
            continue;
        const BotTrigger& t = r.m_trigger;
        if (t.m_type != bot_trigger_at && t.m_type != bot_trigger_keyword)
            continue;
        if (!InTimeWindow(t.m_window, clock))
            continue;
        if (t.m_type == bot_trigger_at && !atMe)
            continue;
        if (t.m_type == bot_trigger_keyword)
        {
            if (t.m_needAt && !atMe)
                continue;
            if (!MatchAnyKeyword(msg.m_textWithoutAt, t.m_keywords, t.m_match))
                continue;
        }

        d.m_ruleId = r.m_id;
        d.m_ruleIndex = i;
        d.m_ruleName = r.m_name;
        d.m_reason = bot_reason_ok;
        d.m_question = TrimText(msg.m_textWithoutAt);
        d.m_markdown = r.m_action.m_markdown;
        switch (r.m_action.m_type)
        {
        //用问答作答
        case bot_action_qa:
            d.m_kind = bot_decision_reply;
            if (qa == NULL || qa->empty())
                d.m_text = BuildQaReplyText(QaAnswer(), std::vector<QaItem>(), r.m_action.m_text);
            else
                d.m_text = BuildQaReplyText(AnswerFromQa(d.m_question, *qa), *qa, r.m_action.m_text);
            d.m_markdown = false;
            break;
        //呼叫外部服务：回复由外部服务决定，这里只带上地址与兜底文字
        case bot_action_external:
            d.m_kind = bot_decision_external;
            d.m_url = r.m_action.m_url;
            d.m_text = r.m_action.m_text;
            break;
        //回复文字，以及不认识的取值
        default:
            d.m_kind = bot_decision_reply;
            d.m_text = ExpandVariables(r.m_action.m_text, senderName, std::string(), FormatDate(clock), std::string());
            break;
        }
        return d;
    }
    return d;
}

BotDecision EvaluateSingleMessage(const BotRecord& b, const PersonalInput& in)
{
    BotDecision d;
    if (!IsTriggerableMsgType(in.m_msgtype) || in.m_msg.m_botMarked || in.m_msg.m_autoReply)
    {
        d.m_reason = bot_reason_not_triggerable;
        return d;
    }
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (!r.m_enabled || r.m_trigger.m_type != bot_trigger_single)
            continue;
        const BotTrigger& t = r.m_trigger;
        if (!t.m_presence.empty() && !Contains(t.m_presence, in.m_ownerPresence))
            continue;
        if (!InTimeWindow(t.m_window, in.m_clock))
            continue;
        if (!SenderInScope(t.m_senders, in.m_senderId, in.m_senderDepts))
            continue;
        if (!t.m_keywords.empty())
        {
            const std::string& text = in.m_msg.m_text;
            if (!MatchAnyKeyword(text, t.m_keywords, t.m_match))
                continue;
        }

        //命中第一条规则之后，冷却与「主人正在交谈」只决定这次回不回，不再去看后面的规则
        d.m_ruleId = r.m_id;
        d.m_ruleIndex = i;
        d.m_ruleName = r.m_name;
        if (t.m_cooldownMinutes > 0 && in.m_lastReplyMs > 0
            && in.m_nowMs - in.m_lastReplyMs < static_cast<int64_t>(t.m_cooldownMinutes) * kMsPerMinute)
        {
            d.m_reason = bot_reason_cooldown;
            return d;
        }
        if (t.m_ownerQuietMinutes > 0 && in.m_ownerLastSpokeMs > 0
            && in.m_nowMs - in.m_ownerLastSpokeMs < static_cast<int64_t>(t.m_ownerQuietMinutes) * kMsPerMinute)
        {
            d.m_reason = bot_reason_owner_speaking;
            return d;
        }
        d.m_kind = bot_decision_reply;
        d.m_reason = bot_reason_ok;
        std::string text = r.m_action.m_text;
        if (TrimText(text).empty())
        {
            //自动回复的预置规则没有文字时按规则的内部名称取默认文字
            text = (r.m_name == kAutoReplyBusyName) ? kDefaultBusyText : kDefaultAwayText;
        }
        d.m_text = ExpandVariables(text, in.m_senderName, in.m_ownerName, FormatDate(in.m_clock), std::string());
        return d;
    }
    return d;
}

BotDecision EvaluateJoin(const BotRecord& b, const LocalClock& clock, const std::vector<std::string>& memberNames)
{
    BotDecision d;
    std::string names;
    for (size_t i = 0; i < memberNames.size(); ++i)
    {
        if (i > 0)
            names += kMemberSeparator;
        names += memberNames[i];
    }
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (!r.m_enabled || r.m_trigger.m_type != bot_trigger_join)
            continue;
        if (!InTimeWindow(r.m_trigger.m_window, clock))
            continue;
        d.m_kind = bot_decision_reply;
        d.m_reason = bot_reason_ok;
        d.m_ruleId = r.m_id;
        d.m_ruleIndex = i;
        d.m_ruleName = r.m_name;
        d.m_markdown = r.m_action.m_markdown;
        d.m_text = ExpandVariables(r.m_action.m_text, std::string(), std::string(), FormatDate(clock), names);
        return d;
    }
    return d;
}

std::vector<size_t> DueScheduleRules(const BotRecord& b, const LocalClock& clock)
{
    std::vector<size_t> due;
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (r.m_enabled && r.m_trigger.m_type == bot_trigger_schedule && ScheduleDue(r.m_trigger.m_schedule, clock))
            due.push_back(i);
    }
    return due;
}

bool HasWebhookRule(const BotRecord& b)
{
    return HasEnabledTrigger(b, bot_trigger_webhook);
}

bool IsAtable(const BotRecord& b)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (!r.m_enabled)
            continue;
        if (r.m_trigger.m_type == bot_trigger_at)
            return true;
        if (r.m_trigger.m_type == bot_trigger_keyword && r.m_trigger.m_needAt)
            return true;
    }
    return false;
}

Json::Value BuildUsageJson(const BotRecord& b)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const BotRule& r = b.m_rules[i];
        if (!r.m_enabled)
            continue;
        const BotTrigger& t = r.m_trigger;
        Json::Value item;
        item["t"] = t.m_type;
        switch (t.m_type)
        {
        //被 @：注明是否用问答作答
        case bot_trigger_at:
            item["qa"] = r.m_action.m_type == bot_action_qa ? 1 : 0;
            break;
        //关键词：关键词、匹配方式、是否要求 @
        case bot_trigger_keyword:
        {
            Json::Value kws(Json::arrayValue);
            for (size_t k = 0; k < t.m_keywords.size(); ++k)
                kws.append(t.m_keywords[k]);
            item["kw"] = kws;
            item["m"] = t.m_match;
            item["at"] = t.m_needAt ? 1 : 0;
            break;
        }
        //定时：种类、时刻、星期、几号
        case bot_trigger_schedule:
            item["k"] = t.m_schedule.m_kind;
            item["time"] = FormatHhMm(t.m_schedule.m_minuteOfDay);
            item["wd"] = IntArrayToJson(t.m_schedule.m_weekdays);
            item["md"] = t.m_schedule.m_monthDay;
            break;
        //新成员入群、外部系统推送：只需要类型
        case bot_trigger_join:
        case bot_trigger_webhook:
            break;
        //收到单聊（个人机器人）不在群里展示
        default:
            continue;
        }
        arr.append(item);
    }
    return arr;
}

//======== 消息 content ========

std::string BuildGroupReplyContent(int64_t botId, const std::string& text, bool markdown, int64_t atUserId,
                                   const std::string& atName, const std::string& botName)
{
    Json::Value content;
    std::string body = text;
    if (atUserId > 0)
    {
        const std::string atText = "@" + atName;
        body = atText + " " + text;
        Json::Value at;
        at["all"] = 0;
        Json::Value users(Json::arrayValue);
        Json::Value one;
        one["uid"] = std::to_string(static_cast<long long>(atUserId));
        one["off"] = 0;
        one["len"] = static_cast<int>(atText.size());
        users.append(one);
        at["users"] = users;
        content["at"] = at;
    }
    content["text"] = body;
    if (markdown)
        content["format"] = "markdown";
    Json::Value b;
    b["id"] = std::to_string(static_cast<long long>(botId));
    b["name"] = botName;
    content["bot"] = b;
    return WriteCompact(content);
}

std::string BuildPersonalReplyContent(const BotRecord& b, const std::string& text)
{
    const bool autoReply = b.m_source == bot_source_autoreply;
    Json::Value content;
    content["text"] = std::string(autoReply ? kPrefixAutoReply : kPrefixRobot) + text;
    content["autoreply"] = true;
    Json::Value mark;
    mark["id"] = std::to_string(static_cast<long long>(b.m_botId));
    mark["name"] = b.m_name;
    mark["kind"] = autoReply ? "autoreply" : "personal";
    content["bot"] = mark;
    return WriteCompact(content);
}

//======== 序列化 ========

Json::Value TriggerToJson(const BotTrigger& t)
{
    Json::Value v;
    v["type"] = t.m_type;
    Json::Value kws(Json::arrayValue);
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
        kws.append(t.m_keywords[i]);
    v["keywords"] = kws;
    v["match"] = t.m_match;
    v["needat"] = t.m_needAt ? 1 : 0;
    Json::Value sch;
    sch["kind"] = t.m_schedule.m_kind;
    sch["time"] = FormatHhMm(t.m_schedule.m_minuteOfDay);
    sch["weekdays"] = IntArrayToJson(t.m_schedule.m_weekdays);
    sch["monthday"] = t.m_schedule.m_monthDay;
    v["schedule"] = sch;
    v["groupids"] = IdArrayToJson(t.m_scheduleGroups);
    v["presence"] = IntArrayToJson(t.m_presence);
    Json::Value win;
    win["on"] = t.m_window.m_enabled ? 1 : 0;
    win["days"] = t.m_window.m_days;
    win["from"] = FormatHhMm(t.m_window.m_fromMinute);
    win["to"] = FormatHhMm(t.m_window.m_toMinute);
    v["window"] = win;
    Json::Value snd;
    snd["kind"] = t.m_senders.m_kind;
    snd["users"] = IdArrayToJson(t.m_senders.m_users);
    snd["depts"] = IdArrayToJson(t.m_senders.m_depts);
    v["senders"] = snd;
    v["cooldown"] = t.m_cooldownMinutes;
    v["ownerquiet"] = t.m_ownerQuietMinutes;
    return v;
}

bool TriggerFromJson(const Json::Value& v, BotTrigger& t)
{
    t = BotTrigger();
    if (!v.isObject())
        return false;
    t.m_type = ReadInt(v["type"], 0);
    if (t.m_type < bot_trigger_at || t.m_type > bot_trigger_webhook)
        return false;
    const Json::Value& kws = v["keywords"];
    if (kws.isArray())
    {
        for (Json::ArrayIndex i = 0; i < kws.size(); ++i)
        {
            if (kws[i].isString())
                t.m_keywords.push_back(kws[i].asString());
        }
    }
    t.m_match = ReadInt(v["match"], bot_match_contains);
    t.m_needAt = ReadBool(v["needat"], true);
    const Json::Value& sch = v["schedule"];
    if (sch.isObject())
    {
        t.m_schedule.m_kind = ReadInt(sch["kind"], bot_schedule_daily);
        int minute = -1;
        if (!ParseHhMm(ReadString(sch["time"]), minute))
            minute = -1;
        t.m_schedule.m_minuteOfDay = minute;
        ReadIntArray(sch["weekdays"], t.m_schedule.m_weekdays);
        t.m_schedule.m_monthDay = ReadInt(sch["monthday"], 1);
    }
    ReadIdArray(v["groupids"], t.m_scheduleGroups);
    ReadIntArray(v["presence"], t.m_presence);
    const Json::Value& win = v["window"];
    if (win.isObject())
    {
        t.m_window.m_enabled = ReadBool(win["on"], false);
        t.m_window.m_days = ReadInt(win["days"], bot_days_every);
        int from = -1;
        int to = -1;
        if (!ParseHhMm(ReadString(win["from"]), from))
            from = -1;
        if (!ParseHhMm(ReadString(win["to"]), to))
            to = -1;
        t.m_window.m_fromMinute = from;
        t.m_window.m_toMinute = to;
    }
    const Json::Value& snd = v["senders"];
    if (snd.isObject())
    {
        t.m_senders.m_kind = ReadInt(snd["kind"], bot_sender_all);
        ReadIdArray(snd["users"], t.m_senders.m_users);
        ReadIdArray(snd["depts"], t.m_senders.m_depts);
    }
    t.m_cooldownMinutes = ReadInt(v["cooldown"], 0);
    t.m_ownerQuietMinutes = ReadInt(v["ownerquiet"], 0);
    return true;
}

Json::Value ActionToJson(const BotAction& a)
{
    Json::Value v;
    v["type"] = a.m_type;
    v["text"] = a.m_text;
    v["markdown"] = a.m_markdown ? 1 : 0;
    v["url"] = a.m_url;
    if (a.m_defaultText)
        v["defaulttext"] = 1;
    return v;
}

bool ActionFromJson(const Json::Value& v, BotAction& a)
{
    a = BotAction();
    if (!v.isObject())
        return false;
    a.m_type = ReadInt(v["type"], 0);
    if (a.m_type < bot_action_reply || a.m_type > bot_action_external)
        return false;
    a.m_text = ReadString(v["text"]);
    a.m_markdown = ReadBool(v["markdown"], false);
    a.m_url = ReadString(v["url"]);
    a.m_defaultText = ReadBool(v["defaulttext"], false);
    return true;
}

Json::Value RuleToJson(const BotRule& r)
{
    Json::Value v;
    v["id"] = std::to_string(static_cast<long long>(r.m_id));
    v["name"] = r.m_name;
    v["enabled"] = r.m_enabled ? 1 : 0;
    v["trigger"] = TriggerToJson(r.m_trigger);
    v["action"] = ActionToJson(r.m_action);
    return v;
}

bool RuleFromJson(const Json::Value& v, BotRule& r)
{
    r = BotRule();
    if (!v.isObject())
        return false;
    r.m_id = ReadInt64(v["id"], 0);
    r.m_name = ReadString(v["name"]);
    r.m_enabled = ReadBool(v["enabled"], true);
    if (!TriggerFromJson(v["trigger"], r.m_trigger))
        return false;
    //外部系统推送的规则没有动作；其余规则必须带动作
    if (!ActionFromJson(v["action"], r.m_action))
    {
        if (r.m_trigger.m_type != bot_trigger_webhook)
            return false;
        r.m_action = BotAction();
    }
    return true;
}

std::string BuildWebhookUrl(const std::string& baseUrl)
{
    size_t begin = 0;
    size_t end = baseUrl.size();
    while (begin < end && isspace(static_cast<unsigned char>(baseUrl[begin])))
        ++begin;
    while (end > begin && (isspace(static_cast<unsigned char>(baseUrl[end - 1])) || baseUrl[end - 1] == '/'))
        --end;
    if (begin == end)
        return std::string();
    return baseUrl.substr(begin, end - begin) + kWebhookPath;
}

Json::Value BotToJson(const BotRecord& b, bool withRules, bool withSecrets)
{
    Json::Value v;
    v["botid"] = std::to_string(static_cast<long long>(b.m_botId));
    v["userid"] = std::to_string(static_cast<long long>(b.m_userId));
    v["enterpriseid"] = std::to_string(static_cast<long long>(b.m_enterpriseId));
    v["ownertype"] = b.m_ownerType;
    v["ownerid"] = std::to_string(static_cast<long long>(b.m_ownerId));
    v["name"] = b.m_name;
    v["desc"] = b.m_desc;
    v["glyph"] = b.m_glyph;
    v["color"] = b.m_color;
    v["scope"] = b.m_scope;
    v["groupids"] = IdArrayToJson(b.m_groupIds);
    v["allowgroupoff"] = b.m_allowGroupOff ? 1 : 0;
    v["status"] = b.m_status;
    v["source"] = b.m_source;
    v["creatorname"] = b.m_creatorName;
    v["createtime"] = static_cast<Json::Int64>(b.m_createTime);
    v["updatetime"] = static_cast<Json::Int64>(b.m_updateTime);
    v["lastfire"] = static_cast<Json::Int64>(b.m_lastFireTime);
    v["rulecount"] = static_cast<int>(b.m_rules.size());
    if (withRules)
    {
        Json::Value rules(Json::arrayValue);
        for (size_t i = 0; i < b.m_rules.size(); ++i)
            rules.append(RuleToJson(b.m_rules[i]));
        v["rules"] = rules;
    }
    if (withSecrets)
    {
        v["webhooktoken"] = b.m_webhookToken;
        v["secret"] = b.m_secret;
    }
    return v;
}

bool BotFromJson(const Json::Value& v, BotRecord& b)
{
    if (!v.isObject())
        return false;
    b.m_botId = ReadInt64(v["botid"], 0);
    b.m_name = ReadString(v["name"]);
    b.m_desc = ReadString(v["desc"]);
    b.m_glyph = ReadString(v["glyph"]);
    b.m_color = ReadString(v["color"]);
    b.m_status = ReadInt(v["status"], bot_status_active);
    b.m_scope = ReadInt(v["scope"], bot_scope_all);
    ReadIdArray(v["groupids"], b.m_groupIds);
    b.m_allowGroupOff = ReadBool(v["allowgroupoff"], false);
    b.m_rules.clear();
    const Json::Value& rules = v["rules"];
    if (rules.isArray())
    {
        for (Json::ArrayIndex i = 0; i < rules.size(); ++i)
        {
            BotRule r;
            if (!RuleFromJson(rules[i], r))
                return false;
            r.m_seq = static_cast<int>(i);
            b.m_rules.push_back(r);
        }
    }
    return true;
}

//======== 校验 ========

namespace {

//颜色是否为空或 "#RRGGBB"
bool IsValidColor(const std::string& c)
{
    if (c.empty())
        return true;
    const size_t kColorLen = 7;
    if (c.size() != kColorLen || c[0] != '#')
        return false;
    for (size_t i = 1; i < c.size(); ++i)
    {
        const char ch = c[i];
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!hex)
            return false;
    }
    return true;
}

//文字是否非空且不超过字数上限
bool IsNonEmptyWithin(const std::string& s, int maxChars)
{
    return !TrimText(s).empty() && Utf8Length(s) <= maxChars;
}

//某种归属的机器人能否使用某种触发方式
bool TriggerAllowed(int ownerType, int trigger, bool allowExternal)
{
    switch (ownerType)
    {
    //企业机器人：群里的全部触发方式
    case bot_owner_enterprise:
        return trigger != bot_trigger_single;
    //群机器人：外部系统推送须企业允许
    case bot_owner_group:
        if (trigger == bot_trigger_webhook)
            return allowExternal;
        return trigger != bot_trigger_single;
    //个人机器人：只能收到单聊
    case bot_owner_personal:
        return trigger == bot_trigger_single;
    //不认识的归属
    default:
        return false;
    }
}

//某种触发方式能否搭配某种动作
bool ActionAllowed(int ownerType, int trigger, int action, bool allowExternal)
{
    switch (trigger)
    {
    //被 @ 与关键词：回复、问答、外部服务（外部服务须允许；个人机器人用不到这两种触发）
    case bot_trigger_at:
    case bot_trigger_keyword:
        if (action == bot_action_external)
            return allowExternal && ownerType != bot_owner_personal;
        return action == bot_action_reply || action == bot_action_qa;
    //外部系统推送：动作不使用
    case bot_trigger_webhook:
        return true;
    //入群、定时、收到单聊：只能回复文字
    default:
        return action == bot_action_reply;
    }
}

//校验一条规则
int ValidateRule(const BotRule& r, int ownerType, bool allowExternal, const std::vector<std::string>& domains,
                 std::string& why)
{
    const BotTrigger& t = r.m_trigger;
    const BotAction& a = r.m_action;
    if (Utf8Length(r.m_name) > kBotRuleNameMaxChars)
    {
        why = "rule name too long";
        return bot_check_invalid;
    }
    if (!TriggerAllowed(ownerType, t.m_type, allowExternal))
    {
        why = "trigger type not allowed";
        return bot_check_not_allowed;
    }
    if (!ActionAllowed(ownerType, t.m_type, a.m_type, allowExternal))
    {
        why = "action type not allowed";
        return bot_check_not_allowed;
    }
    if (t.m_match < bot_match_contains || t.m_match > bot_match_prefix)
    {
        why = "bad match mode";
        return bot_check_invalid;
    }
    if (static_cast<int>(t.m_keywords.size()) > kBotKeywordMax)
    {
        why = "too many keywords";
        return bot_check_full;
    }
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
    {
        if (!IsNonEmptyWithin(t.m_keywords[i], kBotKeywordMaxChars))
        {
            why = "bad keyword";
            return bot_check_invalid;
        }
    }
    if (t.m_type == bot_trigger_keyword && t.m_keywords.empty())
    {
        why = "keyword rule without keywords";
        return bot_check_invalid;
    }
    if (t.m_type == bot_trigger_schedule)
    {
        const BotSchedule& s = t.m_schedule;
        if (s.m_minuteOfDay < 0 || s.m_minuteOfDay >= kMinutesPerDay || s.m_kind < bot_schedule_daily
            || s.m_kind > bot_schedule_monthly)
        {
            why = "bad schedule";
            return bot_check_invalid;
        }
        if (s.m_kind == bot_schedule_weekly)
        {
            if (s.m_weekdays.empty())
            {
                why = "weekly schedule without weekdays";
                return bot_check_invalid;
            }
            for (size_t i = 0; i < s.m_weekdays.size(); ++i)
            {
                if (s.m_weekdays[i] < 1 || s.m_weekdays[i] > kDaysPerWeek)
                {
                    why = "bad weekday";
                    return bot_check_invalid;
                }
            }
        }
        if (s.m_kind == bot_schedule_monthly && (s.m_monthDay < 1 || s.m_monthDay > kMaxMonthDay))
        {
            why = "bad month day";
            return bot_check_invalid;
        }
    }
    if (static_cast<int>(t.m_scheduleGroups.size()) > kBotIdListMax)
    {
        why = "too many schedule groups";
        return bot_check_full;
    }
    for (size_t i = 0; i < t.m_presence.size(); ++i)
    {
        const int p = t.m_presence[i];
        if (p != presence_offline && p != presence_online && p != presence_away && p != presence_busy)
        {
            why = "bad presence";
            return bot_check_invalid;
        }
    }
    if (t.m_window.m_enabled)
    {
        if (t.m_window.m_fromMinute < 0 || t.m_window.m_fromMinute >= kMinutesPerDay || t.m_window.m_toMinute < 0
            || t.m_window.m_toMinute >= kMinutesPerDay || t.m_window.m_days < bot_days_every
            || t.m_window.m_days > bot_days_weekend)
        {
            why = "bad time window";
            return bot_check_invalid;
        }
    }
    if (t.m_senders.m_kind < bot_sender_all || t.m_senders.m_kind > bot_sender_depts)
    {
        why = "bad sender scope";
        return bot_check_invalid;
    }
    if (static_cast<int>(t.m_senders.m_users.size()) > kBotIdListMax
        || static_cast<int>(t.m_senders.m_depts.size()) > kBotIdListMax)
    {
        why = "too many senders";
        return bot_check_full;
    }
    if ((t.m_senders.m_kind == bot_sender_users && t.m_senders.m_users.empty())
        || (t.m_senders.m_kind == bot_sender_depts && t.m_senders.m_depts.empty()))
    {
        why = "empty sender scope";
        return bot_check_invalid;
    }
    if (t.m_cooldownMinutes < 0 || t.m_cooldownMinutes > kBotMinutesMax || t.m_ownerQuietMinutes < 0
        || t.m_ownerQuietMinutes > kBotMinutesMax)
    {
        why = "bad cooldown";
        return bot_check_invalid;
    }

    //动作
    if (t.m_type == bot_trigger_webhook)
        return bot_check_ok;
    if (Utf8Length(a.m_text) > kBotReplyMaxChars)
    {
        why = "reply too long";
        return bot_check_invalid;
    }
    if (a.m_type == bot_action_reply && TrimText(a.m_text).empty())
    {
        why = "empty reply";
        return bot_check_invalid;
    }
    if (a.m_type == bot_action_external)
    {
        if (static_cast<int>(a.m_url.size()) > kBotUrlMaxBytes || !IsUrlAllowed(a.m_url, domains))
        {
            why = "url not allowed";
            return bot_check_not_allowed;
        }
    }
    return bot_check_ok;
}

} // anonymous namespace

int ValidateBot(const BotRecord& b, bool allowExternal, const std::vector<std::string>& domains, std::string& why)
{
    why.clear();
    if (!IsNonEmptyWithin(b.m_name, kBotNameMaxChars))
    {
        why = "bad name";
        return bot_check_invalid;
    }
    if (Utf8Length(b.m_desc) > kBotDescMaxChars || Utf8Length(b.m_glyph) > kBotGlyphMaxChars)
    {
        why = "bad desc or glyph";
        return bot_check_invalid;
    }
    if (static_cast<int>(b.m_color.size()) > kBotColorMaxBytes || !IsValidColor(b.m_color))
    {
        why = "bad color";
        return bot_check_invalid;
    }
    if (b.m_status != bot_status_active && b.m_status != bot_status_disabled)
    {
        why = "bad status";
        return bot_check_invalid;
    }
    if (b.m_ownerType == bot_owner_enterprise)
    {
        if (b.m_scope != bot_scope_all && b.m_scope != bot_scope_selected)
        {
            why = "bad scope";
            return bot_check_invalid;
        }
        if (b.m_scope == bot_scope_selected && b.m_groupIds.empty())
        {
            why = "selected scope without groups";
            return bot_check_invalid;
        }
        if (static_cast<int>(b.m_groupIds.size()) > kBotIdListMax)
        {
            why = "too many groups";
            return bot_check_full;
        }
    }
    if (static_cast<int>(b.m_rules.size()) > kBotRuleMax)
    {
        why = "too many rules";
        return bot_check_full;
    }
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const int rc = ValidateRule(b.m_rules[i], b.m_ownerType, allowExternal, domains, why);
        if (rc != bot_check_ok)
        {
            why = "rule " + std::to_string(static_cast<long long>(i)) + ": " + why;
            return rc;
        }
    }
    return bot_check_ok;
}

bool IsUrlAllowed(const std::string& url, const std::vector<std::string>& domains)
{
    const std::string lower = ToLowerAscii(url);
    size_t hostStart = 0;
    if (lower.compare(0, 8, "https://") == 0)
        hostStart = 8;
    else if (lower.compare(0, 7, "http://") == 0)
        hostStart = 7;
    else
        return false;
    size_t hostEnd = lower.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos)
        hostEnd = lower.size();
    std::string host = lower.substr(hostStart, hostEnd - hostStart);
    //不接受带用户名的写法（user@host），免得看上去是一个域名、实际访问的是另一个
    if (host.find('@') != std::string::npos)
        return false;
    const size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);
    if (host.empty())
        return false;
    for (size_t i = 0; i < domains.size(); ++i)
    {
        const std::string d = ToLowerAscii(domains[i]);
        if (d.empty())
            continue;
        if (host == d)
            return true;
        if (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0
            && host[host.size() - d.size() - 1] == '.')
            return true;
    }
    return false;
}

bool IsValidDomain(const std::string& domain)
{
    if (domain.empty() || static_cast<int>(domain.size()) > kBotDomainMaxBytes)
        return false;
    if (domain[0] == '.' || domain[domain.size() - 1] == '.' || domain[0] == '-')
        return false;
    bool hasDot = false;
    for (size_t i = 0; i < domain.size(); ++i)
    {
        const char c = domain[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
                        || c == '-';
        if (!ok)
            return false;
        if (c == '.')
        {
            if (hasDot && domain[i - 1] == '.')
                return false;
            hasDot = true;
        }
    }
    return hasDot;
}

std::string RandomHex32()
{
    unsigned char buf[kRandomBytes];
    randombytes_buf(buf, sizeof(buf));
    return ToHex(buf, sizeof(buf));
}

std::string ComputeSignature(const std::string& secret, const std::string& timestamp, const std::string& body)
{
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, reinterpret_cast<const unsigned char*>(secret.data()), secret.size());
    crypto_auth_hmacsha256_update(&st, reinterpret_cast<const unsigned char*>(timestamp.data()), timestamp.size());
    const unsigned char nl = '\n';
    crypto_auth_hmacsha256_update(&st, &nl, 1);
    crypto_auth_hmacsha256_update(&st, reinterpret_cast<const unsigned char*>(body.data()), body.size());
    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256_final(&st, mac);
    return ToHex(mac, sizeof(mac));
}

//======== 自动回复 ========

std::vector<BotRule> AutoReplyToRules(const AutoReplySettings& s)
{
    std::vector<BotRule> rules;
    BotRule away;
    away.m_name = kAutoReplyAwayName;
    away.m_enabled = s.m_awayOn;
    away.m_seq = 0;
    away.m_trigger.m_type = bot_trigger_single;
    away.m_trigger.m_presence.push_back(presence_away);
    away.m_action.m_type = bot_action_reply;
    away.m_action.m_text = TrimText(s.m_awayText).empty() ? std::string(kDefaultAwayText) : s.m_awayText;
    away.m_action.m_defaultText = s.m_awayDefault;
    rules.push_back(away);

    BotRule busy;
    busy.m_name = kAutoReplyBusyName;
    busy.m_enabled = s.m_busyOn;
    busy.m_seq = 1;
    busy.m_trigger.m_type = bot_trigger_single;
    busy.m_trigger.m_presence.push_back(presence_busy);
    busy.m_action.m_type = bot_action_reply;
    busy.m_action.m_text = TrimText(s.m_busyText).empty() ? std::string(kDefaultBusyText) : s.m_busyText;
    busy.m_action.m_defaultText = s.m_busyDefault;
    rules.push_back(busy);
    return rules;
}

AutoReplySettings RulesToAutoReply(const std::vector<BotRule>& rules)
{
    AutoReplySettings s;
    for (size_t i = 0; i < rules.size(); ++i)
    {
        const BotRule& r = rules[i];
        if (r.m_name == kAutoReplyAwayName)
        {
            s.m_awayOn = r.m_enabled;
            s.m_awayText = r.m_action.m_text;
            s.m_awayDefault = r.m_action.m_defaultText;
        }
        else if (r.m_name == kAutoReplyBusyName)
        {
            s.m_busyOn = r.m_enabled;
            s.m_busyText = r.m_action.m_text;
            s.m_busyDefault = r.m_action.m_defaultText;
        }
    }
    return s;
}

} // namespace bot
