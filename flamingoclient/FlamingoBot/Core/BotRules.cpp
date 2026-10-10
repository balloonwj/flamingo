/**
 *  机器人与规则的本地校验，见 BotRules.h。检查的顺序与服务端 chatserversrc/BotRules.cpp 的 ValidateRule / ValidateBot
 *  相同，返回的结果类别也相同（单元测试 BOTC 系列逐项核对），说明文字按界面语言给出。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotRules.h"

#include <string.h>

#include "BotStrings.h"
#include "BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：说明文字的多语言查找

namespace flamingobot {

namespace {

//外部服务地址允许的两种开头
const char* const kHttpPrefix = "http://";
const char* const kHttpsPrefix = "https://";

//写出不合法的原因（why 可为 NULL），返回 check
int Fail(int check, const std::wstring& text, std::wstring* why)
{
    if (why != NULL)
        *why = text;
    return check;
}

//文字是否非空（去掉首尾空白之后）且不超过字数上限
bool IsNonEmptyWithin(const std::string& text, int maxChars)
{
    return !TrimUtf8(text).empty() && Utf8CharCount(text) <= maxChars;
}

//是否以某个前缀开头（不区分大小写，前缀为小写 ASCII）
bool StartsWithNoCase(const std::string& text, const char* prefix)
{
    size_t i = 0;
    for (; prefix[i] != '\0'; ++i)
    {
        if (i >= text.size())
            return false;
        char ch = text[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        if (ch != prefix[i])
            return false;
    }
    return true;
}

//在线状态的取值是否认识
bool IsKnownPresence(int presence)
{
    return presence == bot_presence_offline || presence == bot_presence_online || presence == bot_presence_away
           || presence == bot_presence_busy;
}

} // namespace

bool TriggerAllowed(int ownerType, int trigger, bool allowExternal)
{
    switch (ownerType)
    {
    //企业机器人：群里的全部触发方式
    case bot_owner_enterprise:
        return trigger >= bot_trigger_at && trigger <= bot_trigger_webhook && trigger != bot_trigger_single;
    //群机器人：外部系统推送须企业允许
    case bot_owner_group:
        if (trigger == bot_trigger_webhook)
            return allowExternal;
        return trigger >= bot_trigger_at && trigger <= bot_trigger_schedule;
    //个人机器人：只能收到单聊
    case bot_owner_personal:
        return trigger == bot_trigger_single;
    //不认识的归属
    default:
        return false;
    }
}

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
    //外部系统推送：不使用动作
    case bot_trigger_webhook:
        return true;
    //入群、定时、收到单聊：只能回复文字
    default:
        return action == bot_action_reply;
    }
}

std::vector<int> AllowedTriggers(int ownerType, bool allowExternal)
{
    std::vector<int> out;
    for (int t = bot_trigger_at; t <= bot_trigger_webhook; ++t)
    {
        if (TriggerAllowed(ownerType, t, allowExternal))
            out.push_back(t);
    }
    return out;
}

std::vector<int> AllowedActions(int ownerType, int trigger, bool allowExternal)
{
    std::vector<int> out;
    if (!TriggerHasAction(trigger))
        return out;
    for (int a = bot_action_reply; a <= bot_action_external; ++a)
    {
        if (ActionAllowed(ownerType, trigger, a, allowExternal))
            out.push_back(a);
    }
    return out;
}

bool TriggerHasAction(int trigger)
{
    return trigger != bot_trigger_webhook;
}

bool TriggerUsesWindow(int trigger)
{
    return trigger == bot_trigger_at || trigger == bot_trigger_keyword || trigger == bot_trigger_join
           || trigger == bot_trigger_single;
}

BotRule MakeDefaultRule(int ownerType)
{
    BotRule rule;
    rule.m_enabled = true;
    rule.m_action.m_type = bot_action_reply;
    if (ownerType == bot_owner_personal)
    {
        rule.m_trigger.m_type = bot_trigger_single;
        rule.m_trigger.m_needAt = false;
        rule.m_trigger.m_cooldownMinutes = kBotDefaultCooldownMinutes;
        rule.m_trigger.m_ownerQuietMinutes = kBotDefaultOwnerQuietMinutes;
    }
    else
    {
        rule.m_trigger.m_type = bot_trigger_at;
        rule.m_trigger.m_needAt = true;
    }
    return rule;
}

int ValidateRule(const BotRule& rule, int ownerType, bool allowExternal, std::wstring* why)
{
    const BotTrigger& t = rule.m_trigger;
    const BotAction& a = rule.m_action;
    if (Utf8CharCount(rule.m_name) > kBotRuleNameMaxChars)
        return Fail(bot_check_invalid, TRF(L"规则名称最多 {0} 字。", kBotRuleNameMaxChars), why);
    if (!TriggerAllowed(ownerType, t.m_type, allowExternal))
        return Fail(bot_check_not_allowed, TRF(L"这种机器人不能使用「{0}」触发方式。", TriggerName(t.m_type)), why);
    if (!ActionAllowed(ownerType, t.m_type, a.m_type, allowExternal))
    {
        return Fail(bot_check_not_allowed,
                    TRF(L"「{0}」不能搭配「{1}」动作。", TriggerName(t.m_type), ActionName(a.m_type)), why);
    }
    if (t.m_match < bot_match_contains || t.m_match > bot_match_prefix)
        return Fail(bot_check_invalid, TR(L"关键词的匹配方式不正确。"), why);
    if (static_cast<int>(t.m_keywords.size()) > kBotKeywordMax)
        return Fail(bot_check_full, TRF(L"关键词最多 {0} 个。", kBotKeywordMax), why);
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
    {
        if (!IsNonEmptyWithin(t.m_keywords[i], kBotKeywordMaxChars))
            return Fail(bot_check_invalid, TRF(L"关键词不能为空，每个最多 {0} 字。", kBotKeywordMaxChars), why);
    }
    if (t.m_type == bot_trigger_keyword && t.m_keywords.empty())
        return Fail(bot_check_invalid, TR(L"请至少填写一个关键词。"), why);
    if (t.m_type == bot_trigger_schedule)
    {
        const BotSchedule& s = t.m_schedule;
        if (s.m_minuteOfDay < 0 || s.m_minuteOfDay >= kMinutesPerDay || s.m_kind < bot_schedule_daily
            || s.m_kind > bot_schedule_monthly)
            return Fail(bot_check_invalid, TR(L"定时的时刻不正确，请写成 09:00 这样的格式。"), why);
        if (s.m_kind == bot_schedule_weekly)
        {
            if (s.m_weekdays.empty())
                return Fail(bot_check_invalid, TR(L"每周定时请至少选择一天。"), why);
            for (size_t i = 0; i < s.m_weekdays.size(); ++i)
            {
                if (s.m_weekdays[i] < 1 || s.m_weekdays[i] > kDaysPerWeek)
                    return Fail(bot_check_invalid, TR(L"星期的取值不正确。"), why);
            }
        }
        if (s.m_kind == bot_schedule_monthly && (s.m_monthDay < 1 || s.m_monthDay > kMaxMonthDay))
            return Fail(bot_check_invalid, TRF(L"每月几号应在 1～{0} 之间。", kMaxMonthDay), why);
    }
    if (static_cast<int>(t.m_scheduleGroups.size()) > kBotIdListMax)
        return Fail(bot_check_full, TRF(L"定时发往的群最多 {0} 个。", kBotIdListMax), why);
    for (size_t i = 0; i < t.m_presence.size(); ++i)
    {
        if (!IsKnownPresence(t.m_presence[i]))
            return Fail(bot_check_invalid, TR(L"在线状态的取值不正确。"), why);
    }
    if (t.m_window.m_enabled)
    {
        if (t.m_window.m_fromMinute < 0 || t.m_window.m_fromMinute >= kMinutesPerDay || t.m_window.m_toMinute < 0
            || t.m_window.m_toMinute >= kMinutesPerDay || t.m_window.m_days < bot_days_every
            || t.m_window.m_days > bot_days_weekend)
            return Fail(bot_check_invalid, TR(L"时间段不正确，请写成 18:00 这样的格式。"), why);
    }
    if (t.m_senders.m_kind < bot_sender_all || t.m_senders.m_kind > bot_sender_depts)
        return Fail(bot_check_invalid, TR(L"发送者范围不正确。"), why);
    if (static_cast<int>(t.m_senders.m_users.size()) > kBotIdListMax
        || static_cast<int>(t.m_senders.m_depts.size()) > kBotIdListMax)
        return Fail(bot_check_full, TRF(L"指定的人或部门最多 {0} 个。", kBotIdListMax), why);
    if ((t.m_senders.m_kind == bot_sender_users && t.m_senders.m_users.empty())
        || (t.m_senders.m_kind == bot_sender_depts && t.m_senders.m_depts.empty()))
        return Fail(bot_check_invalid, TR(L"选择了「指定的人」，请至少选择一个人。"), why);
    if (t.m_cooldownMinutes < 0 || t.m_cooldownMinutes > kBotMinutesMax || t.m_ownerQuietMinutes < 0
        || t.m_ownerQuietMinutes > kBotMinutesMax)
        return Fail(bot_check_invalid, TRF(L"分钟数应在 0～{0} 之间。", kBotMinutesMax), why);

    //动作：外部系统推送的规则不使用动作
    if (t.m_type == bot_trigger_webhook)
        return bot_check_ok;
    if (Utf8CharCount(a.m_text) > kBotReplyMaxChars)
        return Fail(bot_check_invalid, TRF(L"回复文字最多 {0} 字。", kBotReplyMaxChars), why);
    if (a.m_type == bot_action_reply && TrimUtf8(a.m_text).empty())
        return Fail(bot_check_invalid, TR(L"请填写回复文字。"), why);
    if (a.m_type == bot_action_external && !IsUrlShapeValid(a.m_url))
    {
        return Fail(bot_check_not_allowed,
                    TRF(L"外部服务地址须以 http:// 或 https:// 开头，最长 {0} 个字符，域名须在企业的白名单内。",
                        kBotUrlMaxBytes),
                    why);
    }
    return bot_check_ok;
}

int ValidateBot(const BotRecord& bot, bool allowExternal, std::wstring* why)
{
    if (why != NULL)
        why->clear();
    if (!IsNonEmptyWithin(bot.m_name, kBotNameMaxChars))
        return Fail(bot_check_invalid, TRF(L"请填写名称，最多 {0} 字。", kBotNameMaxChars), why);
    if (Utf8CharCount(bot.m_desc) > kBotDescMaxChars)
        return Fail(bot_check_invalid, TRF(L"简介最多 {0} 字。", kBotDescMaxChars), why);
    if (Utf8CharCount(bot.m_glyph) > kBotGlyphMaxChars)
        return Fail(bot_check_invalid, TRF(L"头像字形最多 {0} 个字。", kBotGlyphMaxChars), why);
    if (!IsColorValid(bot.m_color))
        return Fail(bot_check_invalid, TR(L"头像底色不正确。"), why);
    if (bot.m_status != bot_status_active && bot.m_status != bot_status_disabled)
        return Fail(bot_check_invalid, TR(L"机器人的状态不正确。"), why);
    if (static_cast<int>(bot.m_rules.size()) > kBotRuleMax)
        return Fail(bot_check_full, TRF(L"每个机器人最多 {0} 条规则。", kBotRuleMax), why);
    for (size_t i = 0; i < bot.m_rules.size(); ++i)
    {
        std::wstring ruleWhy;
        const int rc = ValidateRule(bot.m_rules[i], bot.m_ownerType, allowExternal, &ruleWhy);
        if (rc != bot_check_ok)
            return Fail(rc, TRF(L"第 {0} 条规则：{1}", static_cast<int>(i + 1), ruleWhy), why);
    }
    return bot_check_ok;
}

int CheckToErrorCode(int check)
{
    switch (check)
    {
    //合法
    case bot_check_ok:
        return bot_error_ok;
    //数量超出上限
    case bot_check_full:
        return bot_error_full;
    //用了不允许的触发方式或动作
    case bot_check_not_allowed:
        return bot_error_disabled;
    //其余字段不合法
    default:
        return bot_error_invalid;
    }
}

bool IsUrlShapeValid(const std::string& url)
{
    if (url.empty() || static_cast<int>(url.size()) > kBotUrlMaxBytes)
        return false;
    std::string host;
    if (StartsWithNoCase(url, kHttpsPrefix))
        host = url.substr(strlen(kHttpsPrefix));
    else if (StartsWithNoCase(url, kHttpPrefix))
        host = url.substr(strlen(kHttpPrefix));
    else
        return false;
    //开头之后必须还有主机名
    return !host.empty() && host[0] != '/';
}

bool IsColorValid(const std::string& color)
{
    if (color.empty())
        return true;
    uint32_t rgb = 0;
    return ParseHexColor(color, rgb);
}

bool IsAtable(const BotRecord& bot)
{
    for (size_t i = 0; i < bot.m_rules.size(); ++i)
    {
        const BotRule& r = bot.m_rules[i];
        if (!r.m_enabled)
            continue;
        if (r.m_trigger.m_type == bot_trigger_at)
            return true;
        if (r.m_trigger.m_type == bot_trigger_keyword && r.m_trigger.m_needAt)
            return true;
    }
    return false;
}

} // namespace flamingobot
