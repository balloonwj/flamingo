/**
 *  flamingoAdmin —— 企业机器人的规则文字与本地校验实现，说明见 BotRuleText.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Util/BotRuleText.h"

#include <set>

namespace flamingoadmin { namespace botrule {

using namespace flamingoadmin::botapi;

namespace {

// ---- 时刻 ----
const int kHoursPerDay    = 24;   // 一天的小时数：小时取值 0～23
const int kMinutesPerHour = 60;   // 一小时的分钟数：分钟取值 0～59
const int kMaxHourDigits  = 2;    // 小时最多两位数字
const int kMinuteDigits   = 2;    // 分钟必须是两位数字
const int kDecimalBase    = 10;   // 十进制

// ---- 颜色 ----
const int kColorTextLen  = 7;     // "#RRGGBB" 的长度
const int kHexBase       = 16;    // 十六进制
const int kColorByteMask = 0xFF;  // 取一个颜色分量（8 位）
const int kRedShift      = 16;    // "#RRGGBB" 解析成整数后红色分量的位移
const int kGreenShift    = 8;     // 绿色分量的位移

// ---- 外部服务地址 ----
const TCHAR kHttpsPrefix[] = _T("https://");   // https 地址的前缀
const TCHAR kHttpPrefix[]  = _T("http://");    // http 地址的前缀

// 头像底色的候选：第 0 个是新建机器人的默认底色（协议文档 25.1 的示例色），其余与服务号的配色一致
const TCHAR* const kPalette[kPaletteCount] =
{
    _T("#1E88E5"), _T("#FF7563"), _T("#1F9D55"), _T("#8E5BD9"),
    _T("#0EA5A4"), _T("#D88A17"), _T("#D6342A"), _T("#1C2230"),
};

// 星期在定时文字里的简称，下标为星期取值减一（「每周一、三」）
const TCHAR* const kWeekdayShort[kDaysPerWeek] =
{
    _T("一"), _T("二"), _T("三"), _T("四"), _T("五"), _T("六"), _T("日"),
};

// 关键词的分隔符：半角 / 全角逗号、顿号、半角 / 全角分号、换行
const TCHAR kKeywordSeparators[] = _T(",，、;；\r\n");

// 拼接关键词时用的分隔符
const TCHAR kKeywordJoiner[] = _T("，");

// 名单里各项之间的分隔（群名、星期、关键词在摘要里）
const TCHAR kListJoiner[] = _T("、");

// 文字是否全是十进制数字（空串返回 false）
bool AllDigits(const CString& text)
{
    if (text.IsEmpty())
        return false;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        if (text[i] < _T('0') || text[i] > _T('9'))
            return false;
    }
    return true;
}

// 文字按 UTF-8 编码的字节数
int Utf8Bytes(const CString& text)
{
    if (text.IsEmpty())
        return 0;
    return ::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), nullptr, 0, nullptr, nullptr);
}

// 一段文字的摘要：换行换成空格，超过 maxChars 个字时截断并加「…」
CString Digest(const CString& text, int maxChars)
{
    CString digest = text;
    digest.Replace(_T("\r\n"), _T(" "));
    digest.Replace(_T('\n'), _T(' '));
    digest.Replace(_T('\r'), _T(' '));
    digest.Trim();
    if (CharCount(digest) <= maxChars)
        return digest;
    //按 Unicode 字符截断，不把代理对拆开
    const wchar_t kHighFirst = 0xD800;
    const wchar_t kHighLast = 0xDBFF;
    int count = 0;
    int i = 0;
    while (i < digest.GetLength() && count < maxChars)
    {
        if (digest[i] >= kHighFirst && digest[i] <= kHighLast && i + 1 < digest.GetLength())
            ++i;
        ++i;
        ++count;
    }
    return digest.Left(i) + _T("…");
}

// 时间段的文字：「工作日 18:00～09:00」，起止相同时为「工作日全天」
CString WindowText(const BotWindow& window)
{
    const CString days = WindowDaysName(window.m_days);
    if (window.m_from == window.m_to)
        return days + _T("全天");
    return days + _T(" ") + window.m_from + _T("～") + window.m_to;
}

// 校验结果：写出原因并返回 false；原因为空串时返回 true
bool Finish(const CString& reason, CString* error)
{
    if (error != nullptr)
        *error = reason;
    return reason.IsEmpty();
}

} // anonymous namespace

int CharCount(const CString& text)
{
    const wchar_t kHighFirst = 0xD800;
    const wchar_t kHighLast = 0xDBFF;
    int count = 0;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        if (text[i] >= kHighFirst && text[i] <= kHighLast && i + 1 < text.GetLength())
            ++i;
        ++count;
    }
    return count;
}

std::vector<CString> SplitKeywords(const CString& text)
{
    std::vector<CString> out;
    std::set<CString> seen;
    int start = 0;
    const int len = text.GetLength();
    for (int i = 0; i <= len; ++i)
    {
        const bool atEnd = (i == len);
        if (!atEnd && _tcschr(kKeywordSeparators, text[i]) == nullptr)
            continue;
        CString word = text.Mid(start, i - start);
        word.Trim();
        start = i + 1;
        if (word.IsEmpty() || seen.count(word) != 0)
            continue;
        seen.insert(word);
        out.push_back(word);
    }
    return out;
}

CString JoinKeywords(const std::vector<CString>& keywords)
{
    CString text;
    for (size_t i = 0; i < keywords.size(); ++i)
    {
        if (i > 0)
            text += kKeywordJoiner;
        text += keywords[i];
    }
    return text;
}

bool ParseHhMm(const CString& text, int* minuteOfDay)
{
    CString t = text;
    t.Trim();
    t.Replace(_T('：'), _T(':'));
    const int colon = t.Find(_T(':'));
    if (colon <= 0 || colon > kMaxHourDigits)
        return false;
    const CString hourText = t.Left(colon);
    const CString minuteText = t.Mid(colon + 1);
    if (minuteText.GetLength() != kMinuteDigits || !AllDigits(hourText) || !AllDigits(minuteText))
        return false;
    const int hour = static_cast<int>(_tcstol(hourText, nullptr, kDecimalBase));
    const int minute = static_cast<int>(_tcstol(minuteText, nullptr, kDecimalBase));
    if (hour >= kHoursPerDay || minute >= kMinutesPerHour)
        return false;
    if (minuteOfDay != nullptr)
        *minuteOfDay = hour * kMinutesPerHour + minute;
    return true;
}

CString FormatHhMm(int minuteOfDay)
{
    if (minuteOfDay < 0 || minuteOfDay >= kMinutesPerDay)
        return CString();
    CString text;
    text.Format(_T("%02d:%02d"), minuteOfDay / kMinutesPerHour, minuteOfDay % kMinutesPerHour);
    return text;
}

CString NormalizeHhMm(const CString& text)
{
    int minute = 0;
    if (!ParseHhMm(text, &minute))
        return CString();
    return FormatHhMm(minute);
}

int WeekdayMask(const std::vector<int>& weekdays)
{
    int mask = 0;
    for (size_t i = 0; i < weekdays.size(); ++i)
    {
        if (weekdays[i] >= 1 && weekdays[i] <= kDaysPerWeek)
            mask |= (1 << (weekdays[i] - 1));
    }
    return mask;
}

std::vector<int> WeekdaysFromMask(int mask)
{
    std::vector<int> days;
    for (int d = 1; d <= kDaysPerWeek; ++d)
    {
        if ((mask & (1 << (d - 1))) != 0)
            days.push_back(d);
    }
    return days;
}

CString WeekdayName(int day)
{
    if (day < 1 || day > kDaysPerWeek)
        return CString();
    return CString(_T("周")) + kWeekdayShort[day - 1];
}

CString TriggerName(int type)
{
    switch (type)
    {
    //群里有人 @ 机器人
    case kTriggerAt:
        return CString(_T("被 @"));
    //正文命中关键词
    case kTriggerKeyword:
        return CString(_T("关键词"));
    //新成员入群
    case kTriggerJoin:
        return CString(_T("新成员入群"));
    //定时
    case kTriggerSchedule:
        return CString(_T("定时"));
    //收到单聊（个人机器人）
    case kTriggerSingle:
        return CString(_T("收到单聊"));
    //外部系统推送
    case kTriggerWebhook:
        return CString(_T("外部系统推送"));
    //不认识的取值
    default:
        return CString(_T("未知触发方式"));
    }
}

CString ActionName(int type)
{
    switch (type)
    {
    //回复文字
    case kActionReply:
        return CString(_T("回复文字"));
    //用问答作答
    case kActionQa:
        return CString(_T("用问答作答"));
    //呼叫外部服务
    case kActionExternal:
        return CString(_T("呼叫外部服务"));
    //不认识的取值
    default:
        return CString(_T("未知动作"));
    }
}

CString MatchName(int match)
{
    switch (match)
    {
    //正文与关键词完全相同
    case kMatchExact:
        return CString(_T("完全相同"));
    //正文以关键词开头
    case kMatchPrefix:
        return CString(_T("以……开头"));
    //正文包含关键词（含不认识的取值）
    case kMatchContains:
    default:
        return CString(_T("包含"));
    }
}

CString ScheduleKindName(int kind)
{
    switch (kind)
    {
    //工作日
    case kScheduleWorkdays:
        return CString(_T("工作日"));
    //每周所选的几天
    case kScheduleWeekly:
        return CString(_T("每周"));
    //每月几号
    case kScheduleMonthly:
        return CString(_T("每月"));
    //每天（含不认识的取值）
    case kScheduleDaily:
    default:
        return CString(_T("每天"));
    }
}

CString WindowDaysName(int days)
{
    switch (days)
    {
    //工作日
    case kWindowWorkdays:
        return CString(_T("工作日"));
    //周末
    case kWindowWeekend:
        return CString(_T("周末"));
    //每天（含不认识的取值）
    case kWindowEveryDay:
    default:
        return CString(_T("每天"));
    }
}

CString StatusName(int status)
{
    return CString(status == kBotStatusDisabled ? _T("停用") : _T("启用"));
}

std::vector<int> AllowedTriggers()
{
    std::vector<int> triggers;
    triggers.push_back(kTriggerAt);
    triggers.push_back(kTriggerKeyword);
    triggers.push_back(kTriggerJoin);
    triggers.push_back(kTriggerSchedule);
    triggers.push_back(kTriggerWebhook);
    return triggers;
}

std::vector<int> AllowedActions(int trigger)
{
    std::vector<int> actions;
    switch (trigger)
    {
    //被 @ 与关键词：回复文字、用问答作答、呼叫外部服务
    case kTriggerAt:
    case kTriggerKeyword:
        actions.push_back(kActionReply);
        actions.push_back(kActionQa);
        actions.push_back(kActionExternal);
        break;
    //外部系统推送：没有动作
    case kTriggerWebhook:
        break;
    //入群、定时：只能回复文字
    default:
        actions.push_back(kActionReply);
        break;
    }
    return actions;
}

bool IsTriggerAllowed(int trigger)
{
    const std::vector<int> triggers = AllowedTriggers();
    for (size_t i = 0; i < triggers.size(); ++i)
    {
        if (triggers[i] == trigger)
            return true;
    }
    return false;
}

bool IsActionAllowed(int trigger, int action)
{
    if (trigger == kTriggerWebhook)
        return true;
    const std::vector<int> actions = AllowedActions(trigger);
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (actions[i] == action)
            return true;
    }
    return false;
}

bool HasWindow(int trigger)
{
    return trigger == kTriggerAt || trigger == kTriggerKeyword || trigger == kTriggerJoin;
}

bool IsReadOnlySource(int source)
{
    return source == kBotSourceGithub || source == kBotSourceGitlab;
}

CString PaletteColor(int index)
{
    if (index < 0 || index >= kPaletteCount)
        index = 0;
    return CString(kPalette[index]);
}

int PaletteIndexOf(const CString& color)
{
    for (int i = 0; i < kPaletteCount; ++i)
    {
        if (color.CompareNoCase(kPalette[i]) == 0)
            return i;
    }
    return -1;
}

bool IsValidColor(const CString& color)
{
    if (color.IsEmpty())
        return true;
    if (color.GetLength() != kColorTextLen || color[0] != _T('#'))
        return false;
    for (int i = 1; i < color.GetLength(); ++i)
    {
        if (!_istxdigit(color[i]))
            return false;
    }
    return true;
}

COLORREF ColorFromHex(const CString& color, COLORREF fallback)
{
    if (color.IsEmpty() || !IsValidColor(color))
        return fallback;
    const long value = _tcstol(color.Mid(1), nullptr, kHexBase);
    return RGB((value >> kRedShift) & kColorByteMask, (value >> kGreenShift) & kColorByteMask, value & kColorByteMask);
}

CString AvatarGlyph(const BotItem& bot)
{
    CString glyph = bot.m_glyph;
    glyph.Trim();
    if (!glyph.IsEmpty())
        return glyph;
    CString name = bot.m_name;
    name.Trim();
    if (name.IsEmpty())
        return CString();
    //取第一个字；第一个字是代理对时连同低位一起取
    const wchar_t kHighFirst = 0xD800;
    const wchar_t kHighLast = 0xDBFF;
    const int kSurrogatePairLen = 2;
    if (name[0] >= kHighFirst && name[0] <= kHighLast && name.GetLength() >= kSurrogatePairLen)
        return name.Left(kSurrogatePairLen);
    return name.Left(1);
}

CString ScopeText(const BotItem& bot, const std::vector<BotGroup>& groups)
{
    if (bot.m_scope != kBotScopeSelected)
        return CString(_T("全部群"));
    CString names;
    const size_t limit = static_cast<size_t>(kScopeNamesMax);
    const size_t shown = (bot.m_groupIds.size() < limit) ? bot.m_groupIds.size() : limit;
    for (size_t i = 0; i < shown; ++i)
    {
        CString name;
        for (size_t k = 0; k < groups.size(); ++k)
        {
            if (groups[k].m_id == bot.m_groupIds[i])
            {
                name = groups[k].m_name;
                break;
            }
        }
        if (name.IsEmpty())
            name.Format(_T("%lld"), bot.m_groupIds[i]);
        if (!names.IsEmpty())
            names += kListJoiner;
        names += name;
    }
    if (bot.m_groupIds.size() > shown)
        names += _T("…");
    CString text;
    text.Format(_T("%u 个群：%s"), static_cast<unsigned>(bot.m_groupIds.size()), names.GetString());
    return text;
}

CString ScheduleText(const BotSchedule& schedule)
{
    CString text;
    switch (schedule.m_kind)
    {
    //每周所选的几天：「每周一、三 09:00」
    case kScheduleWeekly:
    {
        CString days;
        const std::vector<int> sorted = WeekdaysFromMask(WeekdayMask(schedule.m_weekdays));
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (i > 0)
                days += kListJoiner;
            days += kWeekdayShort[sorted[i] - 1];
        }
        text.Format(_T("每周%s %s"), days.GetString(), schedule.m_time.GetString());
        break;
    }
    //每月几号：「每月 1 号 09:00」
    case kScheduleMonthly:
        text.Format(_T("每月 %d 号 %s"), schedule.m_monthDay, schedule.m_time.GetString());
        break;
    //每天、工作日：「每天 09:00」「工作日 09:00」
    default:
        text.Format(_T("%s %s"), ScheduleKindName(schedule.m_kind).GetString(), schedule.m_time.GetString());
        break;
    }
    return text;
}

CString RuleSummary(const BotRule& rule)
{
    const BotTrigger& t = rule.m_trigger;
    CString text;
    if (!rule.m_enabled)
        text += _T("[停用] ");
    CString name = rule.m_name;
    name.Trim();
    if (!name.IsEmpty())
        text += name + _T("：");

    //触发条件
    switch (t.m_type)
    {
    //关键词：「关键词「VPN、密码」（包含，须同时 @）」
    case kTriggerKeyword:
    {
        CString words;
        for (size_t i = 0; i < t.m_keywords.size(); ++i)
        {
            if (i > 0)
                words += kListJoiner;
            words += t.m_keywords[i];
        }
        text += _T("关键词「") + words + _T("」（") + MatchName(t.m_match);
        if (t.m_needAt)
            text += _T("，须同时 @");
        text += _T("）");
        break;
    }
    //定时：「每天 09:00」，指定了目标群时注明群数
    case kTriggerSchedule:
    {
        text += ScheduleText(t.m_schedule);
        if (!t.m_groupIds.empty())
        {
            CString groups;
            groups.Format(_T("，发往 %u 个群"), static_cast<unsigned>(t.m_groupIds.size()));
            text += groups;
        }
        break;
    }
    //被 @：「被 @ 时」
    case kTriggerAt:
        text += _T("被 @ 时");
        break;
    //其余触发方式：触发方式的名称
    default:
        text += TriggerName(t.m_type);
        break;
    }
    if (HasWindow(t.m_type) && t.m_window.m_on)
        text += _T("（") + WindowText(t.m_window) + _T("）");

    //动作
    if (t.m_type == kTriggerWebhook)
    {
        text += _T(" → 把推来的内容发到群里");
        return text;
    }
    switch (rule.m_action.m_type)
    {
    //用问答作答
    case kActionQa:
        text += _T(" → 用问答作答");
        break;
    //呼叫外部服务：写出主机
    case kActionExternal:
    {
        CString host;
        if (!ParseUrlHost(rule.m_action.m_url, &host))
            host = rule.m_action.m_url;
        text += _T(" → 呼叫外部服务 ") + host;
        break;
    }
    //回复文字（含不认识的取值）
    default:
        text += _T(" → 回复「") + Digest(rule.m_action.m_text, kSummaryTextMaxChars) + _T("」");
        break;
    }
    return text;
}

CString TryResultText(const BotTryResult& result)
{
    CString ruleName = result.m_ruleName;
    ruleName.Trim();
    const CString ruleText = ruleName.IsEmpty() ? CString(_T("一条未命名的规则")) : (_T("规则「") + ruleName + _T("」"));
    CString text;
    switch (result.m_kind)
    {
    //命中，回复文字
    case kTryKindReply:
        text = _T("命中") + ruleText + _T("，会回复：") + result.m_text;
        if (result.m_markdown)
            text += _T("（按 Markdown 显示）");
        return text;
    //命中，呼叫外部服务（试一试只返回地址，不实际调用）
    case kTryKindExternal:
        return _T("命中") + ruleText + _T("，会呼叫外部服务：") + result.m_url + _T("（试一试不会真正调用）");
    //没有命中：按原因说明
    default:
        break;
    }
    switch (result.m_reason)
    {
    //没有规则命中
    case kTryReasonNoRule:
        return CString(_T("没有规则命中，机器人不会回复"));
    //冷却中
    case kTryReasonCooldown:
        return CString(_T("冷却中，这次不会回复"));
    //主人正在交谈（个人机器人）
    case kTryReasonOwnerTalking:
        return CString(_T("主人正在交谈，这次不会代答"));
    //这种消息不触发机器人
    case kTryReasonNotTrigger:
        return CString(_T("这种消息不会触发机器人"));
    //其余：命中了规则却没有内容
    default:
        return CString(_T("机器人不会回复"));
    }
}

bool ParseUrlHost(const CString& url, CString* host)
{
    CString lower = url;
    lower.Trim();
    lower.MakeLower();
    int hostStart = 0;
    if (lower.Find(kHttpsPrefix) == 0)
        hostStart = static_cast<int>(_tcslen(kHttpsPrefix));
    else if (lower.Find(kHttpPrefix) == 0)
        hostStart = static_cast<int>(_tcslen(kHttpPrefix));
    else
        return false;
    int hostEnd = lower.GetLength();
    const TCHAR kHostEnders[] = _T("/?#");
    for (int i = hostStart; i < lower.GetLength(); ++i)
    {
        if (_tcschr(kHostEnders, lower[i]) != nullptr)
        {
            hostEnd = i;
            break;
        }
    }
    CString h = lower.Mid(hostStart, hostEnd - hostStart);
    //不接受带用户名的写法（user@host），免得看上去是一个域名、实际访问的是另一个
    if (h.Find(_T('@')) >= 0)
        return false;
    const int colon = h.Find(_T(':'));
    if (colon >= 0)
        h = h.Left(colon);
    if (h.IsEmpty())
        return false;
    if (host != nullptr)
        *host = h;
    return true;
}

bool IsUrlInWhitelist(const CString& url, const std::vector<CString>& domains)
{
    CString host;
    if (!ParseUrlHost(url, &host))
        return false;
    for (size_t i = 0; i < domains.size(); ++i)
    {
        CString d = domains[i];
        d.Trim();
        d.MakeLower();
        if (d.IsEmpty())
            continue;
        if (host == d)
            return true;
        //子域名：主机以「.域名」结尾
        if (host.GetLength() > d.GetLength() && host.Right(d.GetLength() + 1) == _T(".") + d)
            return true;
    }
    return false;
}

bool IsValidDomain(const CString& domain)
{
    if (domain.IsEmpty() || Utf8Bytes(domain) > kBotDomainMaxBytes)
        return false;
    const int last = domain.GetLength() - 1;
    if (domain[0] == _T('.') || domain[last] == _T('.') || domain[0] == _T('-'))
        return false;
    bool hasDot = false;
    for (int i = 0; i <= last; ++i)
    {
        const TCHAR c = domain[i];
        const bool ok = (c >= _T('a') && c <= _T('z')) || (c >= _T('A') && c <= _T('Z')) || (c >= _T('0') && c <= _T('9'))
                        || c == _T('.') || c == _T('-');
        if (!ok)
            return false;
        if (c == _T('.'))
        {
            if (hasDot && domain[i - 1] == _T('.'))
                return false;
            hasDot = true;
        }
    }
    return hasDot;
}

std::vector<CString> SplitDomains(const CString& text)
{
    std::vector<CString> out;
    std::set<CString> seen;
    int start = 0;
    const int len = text.GetLength();
    for (int i = 0; i <= len; ++i)
    {
        const bool atEnd = (i == len);
        if (!atEnd && text[i] != _T('\n') && text[i] != _T('\r'))
            continue;
        CString line = text.Mid(start, i - start);
        line.Trim();
        line.MakeLower();
        start = i + 1;
        if (line.IsEmpty() || seen.count(line) != 0)
            continue;
        seen.insert(line);
        out.push_back(line);
    }
    return out;
}

CString JoinDomains(const std::vector<CString>& domains)
{
    CString text;
    for (size_t i = 0; i < domains.size(); ++i)
    {
        if (i > 0)
            text += _T("\r\n");
        text += domains[i];
    }
    return text;
}

bool ValidateDomains(const std::vector<CString>& domains, CString* error)
{
    CString reason;
    if (static_cast<int>(domains.size()) > kBotDomainMax)
    {
        reason.Format(_T("域名白名单最多 %d 条"), kBotDomainMax);
        return Finish(reason, error);
    }
    for (size_t i = 0; i < domains.size(); ++i)
    {
        if (!IsValidDomain(domains[i]))
        {
            reason.Format(_T("「%s」不是合法的域名：只能包含字母、数字、点号与连字符，如 example.com"),
                          domains[i].GetString());
            return Finish(reason, error);
        }
    }
    return Finish(reason, error);
}

bool ValidateRule(const BotRule& rule, const std::vector<CString>* domains, CString* error)
{
    const BotTrigger& t = rule.m_trigger;
    const BotAction& a = rule.m_action;
    CString reason;
    if (CharCount(rule.m_name) > kBotRuleNameMaxChars)
    {
        reason.Format(_T("规则名称不能超过 %d 个字"), kBotRuleNameMaxChars);
        return Finish(reason, error);
    }
    if (!IsTriggerAllowed(t.m_type))
        return Finish(CString(_T("企业机器人不能使用这种触发方式")), error);
    if (!IsActionAllowed(t.m_type, a.m_type))
        return Finish(TriggerName(t.m_type) + _T("规则不能搭配「") + ActionName(a.m_type) + _T("」"), error);

    //关键词
    if (t.m_type == kTriggerKeyword)
    {
        if (t.m_match < kMatchContains || t.m_match > kMatchPrefix)
            return Finish(CString(_T("关键词的匹配方式不对")), error);
        if (t.m_keywords.empty())
            return Finish(CString(_T("请至少填写一个关键词")), error);
    }
    if (static_cast<int>(t.m_keywords.size()) > kBotKeywordMax)
    {
        reason.Format(_T("关键词不能超过 %d 个"), kBotKeywordMax);
        return Finish(reason, error);
    }
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
    {
        CString word = t.m_keywords[i];
        word.Trim();
        if (word.IsEmpty() || CharCount(word) > kBotKeywordMaxChars)
        {
            reason.Format(_T("关键词不能为空，每个最多 %d 个字"), kBotKeywordMaxChars);
            return Finish(reason, error);
        }
    }

    //定时
    if (t.m_type == kTriggerSchedule)
    {
        const BotSchedule& s = t.m_schedule;
        if (s.m_kind < kScheduleDaily || s.m_kind > kScheduleMonthly)
            return Finish(CString(_T("定时的种类不对")), error);
        if (!ParseHhMm(s.m_time, nullptr))
            return Finish(CString(_T("定时的时刻须为「时:分」，如 09:00")), error);
        if (s.m_kind == kScheduleWeekly && WeekdayMask(s.m_weekdays) == 0)
            return Finish(CString(_T("每周定时请至少选择一天")), error);
        if (s.m_kind == kScheduleMonthly && (s.m_monthDay < 1 || s.m_monthDay > kMaxMonthDay))
        {
            reason.Format(_T("每月定时的日期须为 1～%d 号"), kMaxMonthDay);
            return Finish(reason, error);
        }
    }
    if (static_cast<int>(t.m_groupIds.size()) > kBotIdListMax)
    {
        reason.Format(_T("定时消息的目标群不能超过 %d 个"), kBotIdListMax);
        return Finish(reason, error);
    }

    //时间段
    if (t.m_window.m_on)
    {
        if (t.m_window.m_days < kWindowEveryDay || t.m_window.m_days > kWindowWeekend)
            return Finish(CString(_T("时间段的日子取值不对")), error);
        if (!ParseHhMm(t.m_window.m_from, nullptr) || !ParseHhMm(t.m_window.m_to, nullptr))
            return Finish(CString(_T("时间段的起止时刻须为「时:分」，如 18:00")), error);
    }

    //动作：外部系统推送的规则没有动作
    if (t.m_type == kTriggerWebhook)
        return Finish(reason, error);
    const CString textName = (a.m_type == kActionReply) ? CString(_T("回复文字")) : CString(_T("兜底文字"));
    if (CharCount(a.m_text) > kBotReplyMaxChars)
    {
        reason.Format(_T("%s不能超过 %d 个字"), textName.GetString(), kBotReplyMaxChars);
        return Finish(reason, error);
    }
    CString trimmed = a.m_text;
    trimmed.Trim();
    if (a.m_type == kActionReply && trimmed.IsEmpty())
        return Finish(CString(_T("请填写回复文字")), error);
    if (a.m_type == kActionExternal)
    {
        CString url = a.m_url;
        url.Trim();
        if (url.IsEmpty())
            return Finish(CString(_T("请填写外部服务地址")), error);
        if (!ParseUrlHost(url, nullptr))
            return Finish(CString(_T("外部服务地址须以 http:// 或 https:// 开头，并写明主机")), error);
        if (Utf8Bytes(url) > kBotUrlMaxBytes)
        {
            reason.Format(_T("外部服务地址不能超过 %d 个字符"), kBotUrlMaxBytes);
            return Finish(reason, error);
        }
        if (domains != nullptr && domains->empty())
            return Finish(CString(_T("还没有配置外部服务域名白名单，请先在列表页的「外部服务域名白名单」里添加")), error);
        if (domains != nullptr && !IsUrlInWhitelist(url, *domains))
            return Finish(CString(_T("外部服务地址的主机不在域名白名单内")), error);
    }
    return Finish(reason, error);
}

bool ValidateBot(const BotItem& bot, const std::vector<CString>* domains, CString* error)
{
    CString reason;
    CString name = bot.m_name;
    name.Trim();
    if (name.IsEmpty())
        return Finish(CString(_T("请填写名称")), error);
    if (CharCount(name) > kBotNameMaxChars)
    {
        reason.Format(_T("名称不能超过 %d 个字"), kBotNameMaxChars);
        return Finish(reason, error);
    }
    if (CharCount(bot.m_desc) > kBotDescMaxChars)
    {
        reason.Format(_T("简介不能超过 %d 个字"), kBotDescMaxChars);
        return Finish(reason, error);
    }
    if (CharCount(bot.m_glyph) > kBotGlyphMaxChars)
    {
        reason.Format(_T("头像文字不能超过 %d 个字"), kBotGlyphMaxChars);
        return Finish(reason, error);
    }
    if (!IsValidColor(bot.m_color))
        return Finish(CString(_T("头像底色须为 #RRGGBB 格式")), error);
    if (bot.m_scope == kBotScopeSelected && bot.m_groupIds.empty())
        return Finish(CString(_T("请至少选择一个群")), error);
    if (static_cast<int>(bot.m_groupIds.size()) > kBotIdListMax)
    {
        reason.Format(_T("所选的群不能超过 %d 个"), kBotIdListMax);
        return Finish(reason, error);
    }
    if (static_cast<int>(bot.m_rules.size()) > kBotRuleMax)
    {
        reason.Format(_T("规则不能超过 %d 条"), kBotRuleMax);
        return Finish(reason, error);
    }
    for (size_t i = 0; i < bot.m_rules.size(); ++i)
    {
        CString ruleError;
        if (!ValidateRule(bot.m_rules[i], domains, &ruleError))
        {
            reason.Format(_T("第 %u 条规则：%s"), static_cast<unsigned>(i + 1), ruleError.GetString());
            return Finish(reason, error);
        }
    }
    return Finish(reason, error);
}

}} // namespace flamingoadmin::botrule
