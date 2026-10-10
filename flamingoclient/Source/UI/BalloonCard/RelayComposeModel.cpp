/**
 *  RelayComposeModel.cpp —— 「发起接龙」窗口的纯逻辑，见 RelayComposeModel.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-02
 */

// ---- ATL 头文件要求的前置依赖，与 ChatRelayCard.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <string.h>   // memset
#include <time.h>

#include "RelayComposeModel.h"
#include "../../net/CardProtocolClient.h"   // buildRelayCardContent / normalizeRelayText / 长度上限
#include "../../net/Protocol.h"             // error_code 的具体取值
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace ballooncard {

namespace {

//Unix 毫秒与秒的换算
const int64_t kMsPerSecond = 1000;
//一分钟的毫秒数：提示文字里把最短提前量写成分钟
const int64_t kMsPerMinute = 60LL * kMsPerSecond;
//截止时间初始值比当前时间晚多少：1 小时，单位毫秒
const int64_t kDefaultDeadlineLeadMs = 3600LL * kMsPerSecond;
//struct tm 与日历取值之间的偏移：tm_year 从 1900 年起算，tm_mon 从 0 起算
const int kTmYearBase  = 1900;
const int kTmMonthBase = 1;
//年月日时分秒各自的合法范围（年的下限取 Unix 纪元，更早的时间 mktime 换算不了）
const int kMinYear   = 1970;
const int kMinMonth  = 1;
const int kMaxMonth  = 12;
const int kMinDay    = 1;
const int kMaxDay    = 31;
const int kMaxHour   = 23;
const int kMaxMinute = 59;
const int kMaxSecond = 59;

//判断一段 UTF-8 文字是否只由空白组成（半角空格、制表符、回车、换行、全角空格 U+3000），空串也算
bool IsBlankUtf8(const std::string& text)
{
    //全角空格 U+3000 的 UTF-8 编码：E3 80 80
    const size_t kIdeographicSpaceLen = 3;
    const unsigned char kIdeographicSpace[kIdeographicSpaceLen] = { 0xE3, 0x80, 0x80 };
    size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            ++i;
            continue;
        }
        if (i + kIdeographicSpaceLen <= text.size()
            && (unsigned char)text[i] == kIdeographicSpace[0]
            && (unsigned char)text[i + 1] == kIdeographicSpace[1]
            && (unsigned char)text[i + 2] == kIdeographicSpace[2])
        {
            i += kIdeographicSpaceLen;
            continue;
        }
        return false;
    }
    return true;
}

} // anonymous namespace

RelayDeadlineFields DefaultRelayDeadlineFields(int64_t nowMs)
{
    RelayDeadlineFields fields;
    const time_t secs = (time_t)((nowMs + kDefaultDeadlineLeadMs) / kMsPerSecond);
    struct tm t;
    if (localtime_s(&t, &secs) != 0)
    {
        return fields;
    }
    fields.m_year   = t.tm_year + kTmYearBase;
    fields.m_month  = t.tm_mon + kTmMonthBase;
    fields.m_day    = t.tm_mday;
    fields.m_hour   = t.tm_hour;
    fields.m_minute = t.tm_min;
    fields.m_second = 0;   //秒取 0：微调框里看到的是整分钟，「早于 5 分钟」的检查也更直观
    return fields;
}

int64_t RelayDeadlineFieldsToMs(const RelayDeadlineFields& fields)
{
    if (fields.m_year < kMinYear
        || fields.m_month < kMinMonth || fields.m_month > kMaxMonth
        || fields.m_day < kMinDay || fields.m_day > kMaxDay
        || fields.m_hour < 0 || fields.m_hour > kMaxHour
        || fields.m_minute < 0 || fields.m_minute > kMaxMinute
        || fields.m_second < 0 || fields.m_second > kMaxSecond)
    {
        return 0;
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = fields.m_year - kTmYearBase;
    t.tm_mon   = fields.m_month - kTmMonthBase;
    t.tm_mday  = fields.m_day;
    t.tm_hour  = fields.m_hour;
    t.tm_min   = fields.m_minute;
    t.tm_sec   = fields.m_second;
    t.tm_isdst = -1;
    const time_t at = mktime(&t);
    if (at == (time_t)-1)
    {
        return 0;
    }
    //mktime 会把 2 月 30 日顺延成 3 月 2 日：年月日被改过就说明这一天不存在
    if (t.tm_year + kTmYearBase != fields.m_year || t.tm_mon + kTmMonthBase != fields.m_month
        || t.tm_mday != fields.m_day)
    {
        return 0;
    }
    return (int64_t)at * kMsPerSecond;
}

int CheckRelayCompose(const std::string& title, const std::string& note, int64_t deadlineMs, int64_t nowMs,
                      const std::string& myEntry, std::string& outContentJson, std::string& outEntry)
{
    //标题、说明先按 1 毫秒这个占位截止时间检查：截止时间要按窗口里的顺序排在它们之后检查，
    //而 buildRelayCardContent 对截止时间只拒绝负数，用占位值不会影响这一步的结论
    const int64_t kProbeDeadlineMs = 1;
    std::string content;
    const int code = net::CardProtocolClient::buildRelayCardContent(title, note, kProbeDeadlineMs, content);
    if (code == error_code_bad_request)
    {
        //标题为空或含换行
        return IsBlankUtf8(title) ? kRelayComposeTitleEmpty : kRelayComposeTitleMultiline;
    }
    if (code == error_code_msg_too_large)
    {
        //标题或说明超长：用一个合法的短标题再试一次，仍然超长就是说明的问题
        std::string probe;
        const int noteCode = net::CardProtocolClient::buildRelayCardContent("x", note, kProbeDeadlineMs, probe);
        return (noteCode == error_code_msg_too_large) ? kRelayComposeNoteTooLong : kRelayComposeTitleTooLong;
    }
    if (code != 0)
    {
        return kRelayComposeTitleEmpty;
    }

    //截止时间：必须设置（换算失败时是 0），且不能早于当前时间 + 5 分钟，与服务端同一规则
    if (deadlineMs <= 0)
    {
        return kRelayComposeDeadlineInvalid;
    }
    if (deadlineMs < nowMs + net::kRelayDeadlineMinLeadMs)
    {
        return kRelayComposeDeadlineTooEarly;
    }
    content.clear();
    if (net::CardProtocolClient::buildRelayCardContent(title, note, deadlineMs, content) != 0)
    {
        return kRelayComposeDeadlineInvalid;
    }

    //「我的接龙」选填：只含空白按没填处理
    std::string entry;
    if (!IsBlankUtf8(myEntry))
    {
        const int entryCode = net::CardProtocolClient::normalizeRelayText(myEntry, entry);
        if (entryCode == error_code_msg_too_large)
        {
            return kRelayComposeEntryTooLong;
        }
        if (entryCode != 0)
        {
            return kRelayComposeEntryMultiline;
        }
    }

    outContentJson = content;
    outEntry = entry;
    return kRelayComposeOk;
}

CString RelayComposeCheckText(int check)
{
    CString text;
    switch (check)
    {
    //标题必填
    case kRelayComposeTitleEmpty:
        text = TR(L"请填写接龙标题。");
        break;

    //标题只能一行
    case kRelayComposeTitleMultiline:
        text = TR(L"接龙标题不能换行。");
        break;

    //标题超长
    case kRelayComposeTitleTooLong:
        text.Format(TR(L"标题太长，最多 %u 字。"), (unsigned)net::kRelayTitleMaxChars);
        break;

    //说明超长
    case kRelayComposeNoteTooLong:
        text.Format(TR(L"说明太长，最多 %u 字。"), (unsigned)net::kRelayNoteMaxChars);
        break;

    //「我的接龙」只能一行
    case kRelayComposeEntryMultiline:
        text = TR(L"「我的接龙」不能换行。");
        break;

    //「我的接龙」超长
    case kRelayComposeEntryTooLong:
        text.Format(TR(L"「我的接龙」太长，最多 %u 字。"), (unsigned)net::kRelayTextMaxChars);
        break;

    //截止时间不是有效的日期（如 2 月 30 日）
    case kRelayComposeDeadlineInvalid:
        text = TR(L"截止时间不是有效的日期，请重新填写。");
        break;

    //截止时间太早
    case kRelayComposeDeadlineTooEarly:
        text.Format(TR(L"截止时间须晚于当前时间 %d 分钟以上。"), (int)(net::kRelayDeadlineMinLeadMs / kMsPerMinute));
        break;

    //可以发起，或认不出的取值：不显示提示
    case kRelayComposeOk:
    default:
        break;
    }
    return text;
}

CString RelaySendFailureText(int code)
{
    CString text;
    switch (code)
    {
    //被禁言：服务端按发消息的规则拒绝
    case error_code_member_muted:
        text = TR(L"你已被禁言，不能发起接龙。");
        break;

    //已不在群里
    case error_code_not_group_member:
        text = TR(L"你已不在该群聊中。");
        break;

    //标题或说明超长。发送前已经检查过，这里是服务端兜底拦截的结果
    case error_code_msg_too_large:
        text = TR(L"标题或说明太长，未能发起。");
        break;

    //内容不合规范。发送前已经检查过，这里是服务端兜底拦截的结果
    case error_code_bad_request:
        text = TR(L"内容不合规范，未能发起。");
        break;

    //截止时间太早：发送前按本机时间检查过，本机时钟比服务器慢、或窗口开着时过了时间才会走到这里
    case error_code_relay_deadline_invalid:
        text.Format(TR(L"截止时间须晚于当前时间 %d 分钟以上（以服务器时间为准），未能发起。"),
                    (int)(net::kRelayDeadlineMinLeadMs / kMsPerMinute));
        break;

    //标题或说明命中发送者所属企业的敏感词库（docs/敏感词方案.md 8.6）
    case error_code_sensitive_word:
        text = TR(L"内容包含敏感词，未能发起。");
        break;

    //其余错误码：带上错误码，便于排查
    default:
        text.Format(TR(L"发起接龙失败（错误码 %d）。"), code);
        break;
    }
    return text;
}

} // namespace ballooncard
