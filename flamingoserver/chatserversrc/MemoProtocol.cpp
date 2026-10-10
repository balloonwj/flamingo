/**
 *  「备忘与提醒」报文编解码与规则实现，MemoProtocol.cpp
 *  balloonwj@qq.com   2026-10-03
 */
#include "MemoProtocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <iterator>
#include "../jsoncpp1.9.0/json.h"
#include "Protocol.h"   //memo_repeat_type / memo_style / memo_change_type、group_member_role、error_code_*

//UTF-8 续字节的判定：高两位为 10 的字节是多字节字符的后续字节，不单独构成一个字符。
//取值与 CardProtocol.cpp 里的同名常量相同（那两个常量是该文件私有的）
constexpr unsigned char UTF8_CONTINUATION_MASK = 0xC0;
constexpr unsigned char UTF8_CONTINUATION_BITS = 0x80;

//全角空格 U+3000 的 UTF-8 编码及其字节数。中文输入法下标题首尾容易带上它，与半角空格一样去掉
static const char* const FULLWIDTH_SPACE_UTF8 = "\xE3\x80\x80";
constexpr size_t FULLWIDTH_SPACE_UTF8_LEN = 3;

//开始时间文本 "yyyy-mm-dd hh:mm" 的长度与各分隔符所在的下标
constexpr size_t MEMO_START_TEXT_LEN = 16;
constexpr size_t MEMO_START_DASH1_POS = 4;
constexpr size_t MEMO_START_DASH2_POS = 7;
constexpr size_t MEMO_START_SPACE_POS = 10;
constexpr size_t MEMO_START_COLON_POS = 13;

//十进制 yyyymmddhhmm 里各段的位权：年在 10^8 位上，月在 10^6，日在 10^4，时在 10^2
constexpr int64_t MEMO_START_YEAR_UNIT  = 100000000LL;
constexpr int64_t MEMO_START_MONTH_UNIT = 1000000LL;
constexpr int64_t MEMO_START_DAY_UNIT   = 10000LL;
constexpr int64_t MEMO_START_HOUR_UNIT  = 100LL;

//每一段（月、日、时、分）都是两位十进制数，取模用
constexpr int64_t MEMO_START_PART_BASE = 100LL;

//一年的月数、一天的小时数、一小时的分钟数
constexpr int MEMO_MONTHS_PER_YEAR = 12;
constexpr int MEMO_HOURS_PER_DAY = 24;
constexpr int MEMO_MINUTES_PER_HOUR = 60;

//二月在闰年与平年的天数
constexpr int MEMO_FEB_DAYS_LEAP = 29;
constexpr int MEMO_FEB_DAYS_COMMON = 28;

//格式化开始时间用的缓冲区大小："yyyy-mm-dd hh:mm" 加结尾的 '\0'，多留一些余量
constexpr size_t MEMO_START_FORMAT_BUF = 32;

//判断单个字节是否为可在首尾去掉的半角空白：空格、制表符、回车、换行
static bool isAsciiBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//text 从 pos 开始若是一个可去掉的空白字符，返回它的字节数，否则返回 0
static size_t blankLengthAt(const std::string& text, size_t pos)
{
    if (isAsciiBlank(text[pos]))
        return 1;
    if (text.compare(pos, FULLWIDTH_SPACE_UTF8_LEN, FULLWIDTH_SPACE_UTF8) == 0)
        return FULLWIDTH_SPACE_UTF8_LEN;
    return 0;
}

//text 在区间 [begin, end) 内、紧挨 end 之前若是一个可去掉的空白字符，返回它的字节数，
//否则返回 0。调用方保证 begin < end
static size_t blankLengthBefore(const std::string& text, size_t begin, size_t end)
{
    if (isAsciiBlank(text[end - 1]))
        return 1;
    if (end - begin >= FULLWIDTH_SPACE_UTF8_LEN
        && text.compare(end - FULLWIDTH_SPACE_UTF8_LEN, FULLWIDTH_SPACE_UTF8_LEN,
                        FULLWIDTH_SPACE_UTF8) == 0)
        return FULLWIDTH_SPACE_UTF8_LEN;
    return 0;
}

//去掉首尾空白（半角空白与全角空格）。trimHead 为 false 时只去末尾：备注允许以缩进开头
static std::string trimBlank(const std::string& text, bool trimHead)
{
    size_t begin = 0;
    size_t end = text.size();
    while (trimHead && begin < end)
    {
        const size_t n = blankLengthAt(text, begin);
        if (n == 0)
            break;
        begin += n;
    }
    while (end > begin)
    {
        const size_t n = blankLengthBefore(text, begin, end);
        if (n == 0)
            break;
        end -= n;
    }
    return text.substr(begin, end - begin);
}

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取字符串型 64 位 ID。字段缺失时按 0 处理并返回 true；不是字符串、不是十进制数或为负数时返回 false
static bool readStringId(const Json::Value& root, const char* key, int64_t& out)
{
    out = 0;
    if (!root.isMember(key))
        return true;
    if (!root[key].isString())
        return false;

    const std::string text = root[key].asString();
    if (text.empty())
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    out = static_cast<int64_t>(atoll(text.c_str()));
    return out >= 0;
}

//取可选的整数字段：缺失时取缺省值并返回 true；存在但不是整数时返回 false
static bool readOptionalInt(const Json::Value& root, const char* key, int defaultValue, int& out)
{
    out = defaultValue;
    if (!root.isMember(key))
        return true;
    if (!root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//某年是否为闰年（公历规则）
static bool isLeapYear(int year)
{
    const int kLeapCycle = 4;
    const int kCenturyCycle = 100;
    const int kQuadCenturyCycle = 400;
    return (year % kLeapCycle == 0 && year % kCenturyCycle != 0) || year % kQuadCenturyCycle == 0;
}

//某年某月的天数；month 取 1～12
static int daysInMonth(int year, int month)
{
    //1～12 月的天数，二月按平年，闰年另行处理
    static const int kDays[MEMO_MONTHS_PER_YEAR] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const int kFebruary = 2;
    if (month == kFebruary)
        return isLeapYear(year) ? MEMO_FEB_DAYS_LEAP : MEMO_FEB_DAYS_COMMON;
    return kDays[month - 1];
}

//把一条备忘写成协议里的备忘对象
static Json::Value memoToJson(const MemoRecord& memo)
{
    Json::Value v(Json::objectValue);
    v["memoid"] = std::to_string(memo.memoid);
    v["ctype"] = memo.ctype;
    v["groupid"] = (memo.ctype == conversation_group) ? std::to_string(memo.owner) : std::string("0");
    v["creator"] = std::to_string(memo.creator);
    v["editor"] = std::to_string(memo.editor);
    v["title"] = memo.title;
    v["note"] = memo.note;
    v["start"] = MemoProtocol::formatStart(memo.start);
    v["advance"] = memo.advance;
    v["repeat"] = memo.repeat;
    v["weekdays"] = memo.weekdays;
    v["count"] = memo.count;
    v["style"] = memo.style;
    v["targetall"] = memo.targetAll;

    Json::Value targets(Json::arrayValue);
    for (size_t i = 0; i < memo.targets.size(); ++i)
        targets.append(std::to_string(memo.targets[i]));
    v["targets"] = targets;

    v["createtime"] = static_cast<Json::Int64>(memo.createTime);
    v["updatetime"] = static_cast<Json::Int64>(memo.updateTime);
    return v;
}

bool MemoProtocol::parseListRequest(const std::string& json, int64_t& outGroupid)
{
    outGroupid = 0;
    Json::Value root;
    //包体为空串时等同 {}：老的调用方式只发命令号、不带包体
    if (json.empty())
        return true;
    if (!parseObject(json, root))
        return false;
    return readStringId(root, "groupid", outGroupid);
}

bool MemoProtocol::parseSaveRequest(const std::string& json, MemoRecord& out)
{
    out = MemoRecord();
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!readStringId(root, "memoid", out.memoid))
        return false;

    if (!root["ctype"].isInt())
        return false;
    out.ctype = root["ctype"].asInt();
    if (out.ctype != conversation_single && out.ctype != conversation_group)
        return false;

    int64_t groupid = 0;
    if (!readStringId(root, "groupid", groupid))
        return false;
    if (out.ctype == conversation_group)
    {
        if (groupid <= 0)
            return false;
        out.owner = groupid;
    }

    if (!root["title"].isString())
        return false;
    out.title = root["title"].asString();

    if (root.isMember("note"))
    {
        if (!root["note"].isString())
            return false;
        out.note = root["note"].asString();
    }

    if (!root["start"].isString() || !parseStart(root["start"].asString(), out.start))
        return false;

    if (!readOptionalInt(root, "advance", 0, out.advance))
        return false;
    if (!readOptionalInt(root, "repeat", memo_repeat_none, out.repeat))
        return false;
    if (!readOptionalInt(root, "weekdays", 0, out.weekdays))
        return false;
    if (!readOptionalInt(root, "count", 1, out.count))
        return false;
    if (!readOptionalInt(root, "style", memo_style_popup, out.style))
        return false;

    if (root.isMember("targetall"))
    {
        if (!root["targetall"].isBool())
            return false;
        out.targetAll = root["targetall"].asBool();
    }

    if (root.isMember("targets"))
    {
        const Json::Value& arr = root["targets"];
        if (!arr.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].isString())
                return false;
            const int64_t uid = static_cast<int64_t>(atoll(arr[i].asString().c_str()));
            if (uid <= 0)
                return false;
            out.targets.push_back(uid);
        }
    }
    return true;
}

int MemoProtocol::normalizeMemo(MemoRecord& memo)
{
    memo.title = trimBlank(memo.title, true);
    if (memo.title.empty() || utf8CharCount(memo.title) > kMemoTitleMaxChars)
        return error_code_memo_invalid;
    //标题在列表、提醒弹窗里都按单行显示，不允许换行
    if (memo.title.find('\n') != std::string::npos || memo.title.find('\r') != std::string::npos)
        return error_code_memo_invalid;

    memo.note = trimBlank(memo.note, false);
    if (utf8CharCount(memo.note) > kMemoNoteMaxChars)
        return error_code_memo_invalid;

    if (!isValidStart(memo.start))
        return error_code_memo_invalid;

    if (memo.advance < 0 || memo.advance > kMemoAdvanceMaxMinutes)
        return error_code_memo_invalid;

    if (memo.repeat < memo_repeat_none || memo.repeat > memo_repeat_yearly)
        return error_code_memo_invalid;

    if (memo.repeat == memo_repeat_weekly)
    {
        //每周重复至少要选一天，且只能用到低 7 位
        if (memo.weekdays == 0 || (memo.weekdays & ~kMemoWeekdayAllMask) != 0)
            return error_code_memo_invalid;
    }
    else
    {
        memo.weekdays = 0;
    }

    if (memo.repeat == memo_repeat_none)
    {
        memo.count = 1;
    }
    else if (memo.count < 0 || memo.count > kMemoCountMax)
    {
        return error_code_memo_invalid;
    }

    if (memo.style != memo_style_popup && memo.style != memo_style_fullscreen)
        return error_code_memo_invalid;

    if (memo.ctype == conversation_single)
    {
        memo.targetAll = false;
        memo.targets.clear();
        return error_code_ok;
    }

    if (memo.targetAll)
    {
        memo.targets.clear();
        return error_code_ok;
    }

    std::sort(memo.targets.begin(), memo.targets.end());
    memo.targets.erase(std::unique(memo.targets.begin(), memo.targets.end()), memo.targets.end());
    if (memo.targets.empty())
        return error_code_memo_target_invalid;
    return error_code_ok;
}

bool MemoProtocol::parseDeleteRequest(const std::string& json, int64_t& outMemoid)
{
    outMemoid = 0;
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["memoid"].isString())
        return false;
    if (!readStringId(root, "memoid", outMemoid))
        return false;
    return outMemoid > 0;
}

bool MemoProtocol::parseStart(const std::string& text, int64_t& outStart)
{
    outStart = 0;
    if (text.size() != MEMO_START_TEXT_LEN)
        return false;

    int64_t value = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (i == MEMO_START_DASH1_POS || i == MEMO_START_DASH2_POS)
        {
            if (c != '-')
                return false;
            continue;
        }
        if (i == MEMO_START_SPACE_POS)
        {
            if (c != ' ')
                return false;
            continue;
        }
        if (i == MEMO_START_COLON_POS)
        {
            if (c != ':')
                return false;
            continue;
        }
        if (c < '0' || c > '9')
            return false;
        const int64_t kDecimal = 10;
        value = value * kDecimal + (c - '0');
    }

    if (!isValidStart(value))
        return false;
    outStart = value;
    return true;
}

std::string MemoProtocol::formatStart(int64_t start)
{
    const int year   = static_cast<int>(start / MEMO_START_YEAR_UNIT);
    const int month  = static_cast<int>((start / MEMO_START_MONTH_UNIT) % MEMO_START_PART_BASE);
    const int day    = static_cast<int>((start / MEMO_START_DAY_UNIT) % MEMO_START_PART_BASE);
    const int hour   = static_cast<int>((start / MEMO_START_HOUR_UNIT) % MEMO_START_PART_BASE);
    const int minute = static_cast<int>(start % MEMO_START_PART_BASE);

    char buf[MEMO_START_FORMAT_BUF] = { 0 };
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return std::string(buf);
}

bool MemoProtocol::isValidStart(int64_t start)
{
    if (start <= 0)
        return false;

    const int64_t year64 = start / MEMO_START_YEAR_UNIT;
    if (year64 < kMemoMinYear || year64 > kMemoMaxYear)
        return false;

    const int year   = static_cast<int>(year64);
    const int month  = static_cast<int>((start / MEMO_START_MONTH_UNIT) % MEMO_START_PART_BASE);
    const int day    = static_cast<int>((start / MEMO_START_DAY_UNIT) % MEMO_START_PART_BASE);
    const int hour   = static_cast<int>((start / MEMO_START_HOUR_UNIT) % MEMO_START_PART_BASE);
    const int minute = static_cast<int>(start % MEMO_START_PART_BASE);

    if (month < 1 || month > MEMO_MONTHS_PER_YEAR)
        return false;
    if (day < 1 || day > daysInMonth(year, month))
        return false;
    if (hour < 0 || hour >= MEMO_HOURS_PER_DAY)
        return false;
    if (minute < 0 || minute >= MEMO_MINUTES_PER_HOUR)
        return false;
    return true;
}

bool MemoProtocol::isVisibleTo(const MemoRecord& memo, int64_t userid, int groupRole)
{
    if (memo.ctype == conversation_single)
        return memo.owner == userid;

    //不是群成员（含已退群、被踢）一律看不到
    if (groupRole < group_role_member)
        return false;
    if (groupRole >= group_role_admin)
        return true;
    if (memo.targetAll)
        return true;
    return std::find(memo.targets.begin(), memo.targets.end(), userid) != memo.targets.end();
}

bool MemoProtocol::canEdit(const MemoRecord& memo, int64_t userid, int groupRole)
{
    if (memo.ctype == conversation_single)
        return memo.owner == userid;
    return groupRole >= group_role_admin;
}

void MemoProtocol::collectViewers(const MemoRecord& memo, const std::vector<MemoMemberRole>& members,
                                  std::vector<int64_t>& out)
{
    out.clear();
    if (memo.ctype == conversation_single)
    {
        out.push_back(memo.owner);
        return;
    }

    for (size_t i = 0; i < members.size(); ++i)
    {
        if (isVisibleTo(memo, members[i].userid, members[i].role))
            out.push_back(members[i].userid);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void MemoProtocol::collectRemovedViewers(const std::vector<int64_t>& before, const std::vector<int64_t>& after,
                                         std::vector<int64_t>& out)
{
    out.clear();
    std::set_difference(before.begin(), before.end(), after.begin(), after.end(), std::back_inserter(out));
}

void MemoProtocol::collectSensitiveTexts(const MemoRecord& memo, std::vector<std::string>& out)
{
    out.clear();
    if (memo.ctype != conversation_group)
        return;
    out.push_back(memo.title);
    if (!memo.note.empty())
        out.push_back(memo.note);
}

std::string MemoProtocol::buildListResponse(int code, const std::string& msg, const std::vector<MemoRecord>& memos)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    if (code == error_code_ok)
    {
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < memos.size(); ++i)
            arr.append(memoToJson(memos[i]));
        v["memos"] = arr;
    }
    return writeCompact(v);
}

std::string MemoProtocol::buildSaveResponse(int code, const std::string& msg, const MemoRecord* memo)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    if (memo != NULL)
        v["memo"] = memoToJson(*memo);
    return writeCompact(v);
}

std::string MemoProtocol::buildDeleteResponse(int code, const std::string& msg, int64_t memoid)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = msg;
    v["memoid"] = std::to_string(memoid);
    return writeCompact(v);
}

std::string MemoProtocol::buildUpsertNotify(const MemoRecord& memo)
{
    Json::Value v(Json::objectValue);
    v["changetype"] = memo_change_upsert;
    v["memo"] = memoToJson(memo);
    return writeCompact(v);
}

std::string MemoProtocol::buildDeleteNotify(int64_t memoid)
{
    Json::Value v(Json::objectValue);
    v["changetype"] = memo_change_delete;
    v["memoid"] = std::to_string(memoid);
    return writeCompact(v);
}

std::string MemoProtocol::memoCardClientMsgId(int64_t memoid, int64_t updateTime)
{
    return std::string(kMemoCardClientMsgIdPrefix) + std::to_string(memoid) + "-" + std::to_string(updateTime);
}

std::string MemoProtocol::buildMemoCardContent(const MemoRecord& memo, int change, const std::string& editorName)
{
    Json::Value v(Json::objectValue);
    v["memoid"] = std::to_string(memo.memoid);
    v["groupid"] = std::to_string(memo.owner);
    v["change"] = change;
    v["title"] = memo.title;
    v["note"] = memo.note;
    v["start"] = formatStart(memo.start);
    v["advance"] = memo.advance;
    v["repeat"] = memo.repeat;
    v["weekdays"] = memo.weekdays;
    v["count"] = memo.count;
    v["targetall"] = memo.targetAll;
    //卡片上只写人数，不带名单：名单会随修改变化，点开卡片时客户端看的是备忘簿里的最新版本
    v["targetcount"] = memo.targetAll ? 0 : static_cast<int>(memo.targets.size());
    v["editorid"] = std::to_string(memo.editor);
    v["editor"] = editorName;
    //老客户端不认识本类型时取 text 画成文字气泡
    v["text"] = std::string(u8"[群备忘] ") + memo.title;
    return writeCompact(v);
}

bool MemoProtocol::memoCardAudience(const MemoRecord& memo, const std::vector<int64_t>& viewers,
                                    std::vector<int32_t>& out)
{
    out.clear();
    //提醒全体成员：全员可见。之后入群的成员也在提醒范围内，不必为他们补写可见范围
    if (memo.targetAll)
        return false;
    for (size_t i = 0; i < viewers.size(); ++i)
        out.push_back(static_cast<int32_t>(viewers[i]));
    return true;
}

size_t MemoProtocol::utf8CharCount(const std::string& text)
{
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if ((b & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_BITS)
            ++chars;
    }
    return chars;
}
