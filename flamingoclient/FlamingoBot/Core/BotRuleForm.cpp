/**
 *  规则与规则编辑窗界面字段的互相转换，见 BotRuleForm.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotRuleForm.h"

#include "BotRules.h"
#include "BotStrings.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：说明文字的多语言查找

namespace flamingobot {

namespace {

//一小时的分钟数
const int kMinutesPerHour = 60;
//新建定时规则的默认时刻、新建时间段的默认结束时刻：上午 9 点
const int kDefaultMorningHour = 9;
//新建时间段的默认开始时刻：下午 6 点（下班之后到第二天上班之前，个人机器人代答最常见的时间段）
const int kDefaultEveningHour = 18;
//上面两个时刻换算成一天中的第几分钟
const int kDefaultMorningMinute = kDefaultMorningHour * kMinutesPerHour;
const int kDefaultEveningMinute = kDefaultEveningHour * kMinutesPerHour;

//写出说明（why 可为 NULL），返回 false
bool FailWith(const std::wstring& text, std::wstring* why)
{
    if (why != NULL)
        *why = text;
    return false;
}

//解析一个时刻输入框；空串或格式不对返回 false
bool ParseTimeField(const std::wstring& text, int& minute)
{
    return ParseHhMm(WideToUtf8(TrimWide(text)), minute);
}

//解析一个分钟数输入框：空串按 0；不是非负整数返回 false
bool ParseMinutesField(const std::wstring& text, int& minutes)
{
    if (TrimWide(text).empty())
    {
        minutes = 0;
        return true;
    }
    return ParseNonNegativeInt(text, minutes);
}

//一组值里是否含有某个值
bool ContainsValue(const std::vector<int>& values, int value)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] == value)
            return true;
    }
    return false;
}

} // namespace

BotRuleForm::BotRuleForm()
    : m_ruleId(0)
    , m_enabled(true)
    , m_trigger(bot_trigger_at)
    , m_match(bot_match_contains)
    , m_needAt(true)
    , m_scheduleKind(bot_schedule_workdays)
    , m_windowOn(false)
    , m_windowDays(bot_days_every)
    , m_senderKind(bot_sender_all)
    , m_action(bot_action_reply)
    , m_markdown(false)
    , m_defaultText(false)
{
    for (int i = 0; i < kDaysPerWeek; ++i)
        m_weekdays[i] = false;
    for (int i = 0; i < kPresenceKinds; ++i)
        m_presence[i] = false;
    m_scheduleTime = Utf8ToWide(FormatHhMm(kDefaultMorningMinute));
    m_monthDay = L"1";
    m_windowFrom = Utf8ToWide(FormatHhMm(kDefaultEveningMinute));
    m_windowTo = Utf8ToWide(FormatHhMm(kDefaultMorningMinute));
    m_cooldown = L"0";
    m_ownerQuiet = L"0";
}

BotRuleForm RuleToForm(const BotRule& rule)
{
    BotRuleForm form;
    const BotTrigger& t = rule.m_trigger;
    const BotAction& a = rule.m_action;
    form.m_ruleId = rule.m_id;
    form.m_name = Utf8ToWide(rule.m_name);
    form.m_enabled = rule.m_enabled;
    form.m_trigger = t.m_type;
    form.m_keywords = JoinKeywords(t.m_keywords);
    form.m_match = t.m_match;
    form.m_needAt = t.m_needAt;
    form.m_scheduleKind = t.m_schedule.m_kind;
    if (t.m_schedule.m_minuteOfDay >= 0)
        form.m_scheduleTime = Utf8ToWide(FormatHhMm(t.m_schedule.m_minuteOfDay));
    for (size_t i = 0; i < t.m_schedule.m_weekdays.size(); ++i)
    {
        const int d = t.m_schedule.m_weekdays[i];
        if (d >= 1 && d <= kDaysPerWeek)
            form.m_weekdays[d - 1] = true;
    }
    form.m_monthDay = std::to_wstring(static_cast<long long>(t.m_schedule.m_monthDay));
    form.m_scheduleGroups = t.m_scheduleGroups;
    form.m_windowOn = t.m_window.m_enabled;
    form.m_windowDays = t.m_window.m_days;
    if (t.m_window.m_fromMinute >= 0)
        form.m_windowFrom = Utf8ToWide(FormatHhMm(t.m_window.m_fromMinute));
    if (t.m_window.m_toMinute >= 0)
        form.m_windowTo = Utf8ToWide(FormatHhMm(t.m_window.m_toMinute));
    for (size_t i = 0; i < t.m_presence.size(); ++i)
    {
        const int p = t.m_presence[i];
        if (p >= 0 && p < kPresenceKinds)
            form.m_presence[p] = true;
    }
    form.m_senderKind = t.m_senders.m_kind;
    form.m_senderUsers = t.m_senders.m_users;
    form.m_senderDepts = t.m_senders.m_depts;
    form.m_cooldown = std::to_wstring(static_cast<long long>(t.m_cooldownMinutes));
    form.m_ownerQuiet = std::to_wstring(static_cast<long long>(t.m_ownerQuietMinutes));
    form.m_action = a.m_type;
    form.m_text = Utf8ToWide(a.m_text);
    form.m_markdown = a.m_markdown;
    form.m_defaultText = a.m_defaultText;
    form.m_url = Utf8ToWide(a.m_url);
    return form;
}

bool FormToRule(const BotRuleForm& form, int ownerType, bool allowExternal, BotRule& out, std::wstring* why)
{
    out = BotRule();
    out.m_id = form.m_ruleId;
    out.m_name = WideToUtf8(TrimWide(form.m_name));
    out.m_enabled = form.m_enabled;
    BotTrigger& t = out.m_trigger;
    BotAction& a = out.m_action;
    t.m_type = form.m_trigger;
    t.m_match = form.m_match;
    t.m_scheduleGroups = form.m_scheduleGroups;

    //关键词：只有关键词与收到单聊用得到
    if (t.m_type == bot_trigger_keyword || t.m_type == bot_trigger_single)
        t.m_keywords = SplitKeywords(form.m_keywords);
    //「必须同时 @」：只用于群里的关键词，个人机器人恒为不要求
    t.m_needAt = (ownerType != bot_owner_personal) && form.m_needAt;

    //定时
    t.m_schedule.m_kind = form.m_scheduleKind;
    int minute = 0;
    if (t.m_type == bot_trigger_schedule)
    {
        if (!ParseTimeField(form.m_scheduleTime, minute))
            return FailWith(TR(L"定时的时刻不正确，请写成 09:00 这样的格式。"), why);
        t.m_schedule.m_minuteOfDay = minute;
        if (form.m_scheduleKind == bot_schedule_weekly)
        {
            for (int d = 0; d < kDaysPerWeek; ++d)
            {
                if (form.m_weekdays[d])
                    t.m_schedule.m_weekdays.push_back(d + 1);
            }
        }
        int monthDay = 1;
        if (form.m_scheduleKind == bot_schedule_monthly)
        {
            if (!ParseNonNegativeInt(form.m_monthDay, monthDay))
                return FailWith(TRF(L"每月几号应在 1～{0} 之间。", kMaxMonthDay), why);
        }
        t.m_schedule.m_monthDay = monthDay;
    }
    else
    {
        t.m_schedule.m_minuteOfDay = ParseTimeField(form.m_scheduleTime, minute) ? minute : 0;
    }

    //时间段：只有被 @、关键词、入群与收到单聊可以限定
    t.m_window.m_days = form.m_windowDays;
    int from = 0;
    int to = 0;
    const bool fromOk = ParseTimeField(form.m_windowFrom, from);
    const bool toOk = ParseTimeField(form.m_windowTo, to);
    t.m_window.m_enabled = form.m_windowOn && TriggerUsesWindow(t.m_type);
    if (t.m_window.m_enabled && (!fromOk || !toOk))
        return FailWith(TR(L"时间段不正确，请写成 18:00 这样的格式。"), why);
    t.m_window.m_fromMinute = fromOk ? from : 0;
    t.m_window.m_toMinute = toOk ? to : 0;

    //收到单聊的附加条件：在线状态、发送者、冷却、主人正在交谈
    if (t.m_type == bot_trigger_single)
    {
        for (int p = 0; p < kPresenceKinds; ++p)
        {
            if (form.m_presence[p])
                t.m_presence.push_back(p);
        }
        t.m_senders.m_kind = form.m_senderKind;
        if (form.m_senderKind == bot_sender_users)
            t.m_senders.m_users = form.m_senderUsers;
        else if (form.m_senderKind == bot_sender_depts)
            t.m_senders.m_depts = form.m_senderDepts;
        if (!ParseMinutesField(form.m_cooldown, t.m_cooldownMinutes)
            || !ParseMinutesField(form.m_ownerQuiet, t.m_ownerQuietMinutes))
            return FailWith(TRF(L"分钟数应在 0～{0} 之间。", kBotMinutesMax), why);
    }

    //动作：外部系统推送不使用动作（服务端仍要求有一个合法的 type，写成回复文字、正文为空）
    if (TriggerHasAction(t.m_type))
    {
        a.m_type = form.m_action;
        a.m_text = WideToUtf8(form.m_text);
        a.m_markdown = (form.m_action == bot_action_reply) && form.m_markdown;
        a.m_defaultText = form.m_defaultText;
        if (form.m_action == bot_action_external)
            a.m_url = WideToUtf8(TrimWide(form.m_url));
    }
    else
    {
        a.m_type = bot_action_reply;
    }

    return ValidateRule(out, ownerType, allowExternal, why) == bot_check_ok;
}

void NormalizeForm(BotRuleForm& form, int ownerType, bool allowExternal)
{
    const std::vector<int> triggers = AllowedTriggers(ownerType, allowExternal);
    if (!triggers.empty() && !ContainsValue(triggers, form.m_trigger))
        form.m_trigger = triggers[0];
    const std::vector<int> actions = AllowedActions(ownerType, form.m_trigger, allowExternal);
    if (!actions.empty() && !ContainsValue(actions, form.m_action))
        form.m_action = actions[0];
    if (ownerType == bot_owner_personal)
        form.m_needAt = false;
    if (form.m_match < bot_match_contains || form.m_match > bot_match_prefix)
        form.m_match = bot_match_contains;
    if (form.m_scheduleKind < bot_schedule_daily || form.m_scheduleKind > bot_schedule_monthly)
        form.m_scheduleKind = bot_schedule_daily;
    if (form.m_windowDays < bot_days_every || form.m_windowDays > bot_days_weekend)
        form.m_windowDays = bot_days_every;
    if (form.m_senderKind < bot_sender_all || form.m_senderKind > bot_sender_depts)
        form.m_senderKind = bot_sender_all;
}

} // namespace flamingobot
