/**
 *  机器人管理界面的文字，见 BotText.h。用法说明的措辞与 exe 的 Source/UI/BalloonChatWnd/GroupBotModel.cpp 保持一致，
 *  同一句原文在两边共用一条译文。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotText.h"

#include "BotRules.h"     // TriggerUsesWindow
#include "BotStrings.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找

namespace flamingobot {

namespace {

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kWeekdayNames[kDaysPerWeek] =
{
    TR_NOOP(L"周一"), TR_NOOP(L"周二"), TR_NOOP(L"周三"), TR_NOOP(L"周四"), TR_NOOP(L"周五"), TR_NOOP(L"周六"),
    TR_NOOP(L"周日"),
};
const wchar_t* const kListSeparator = TR_NOOP(L"、");
const wchar_t* const kArrowPattern = TR_NOOP(L"{0} → {1}");

//回复文字里可用的变量：服务端按这几个写法原样替换，不能翻译
const wchar_t* const kVarSender = L"{发送者}";        //i18n-ignore: 服务端识别的变量名
const wchar_t* const kVarOwner = L"{主人}";           //i18n-ignore: 服务端识别的变量名
const wchar_t* const kVarDate = L"{日期}";            //i18n-ignore: 服务端识别的变量名
const wchar_t* const kVarNewMembers = L"{新成员}";    //i18n-ignore: 服务端识别的变量名

//用顿号把一组宽字符串连起来
std::wstring JoinList(const std::vector<std::wstring>& items)
{
    std::wstring out;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            out += TR(kListSeparator);
        out += items[i];
    }
    return out;
}

//关键词用顿号连起来
std::wstring JoinKeywordList(const std::vector<std::string>& keywords)
{
    std::vector<std::wstring> items;
    for (size_t i = 0; i < keywords.size(); ++i)
        items.push_back(Utf8ToWide(keywords[i]));
    return JoinList(items);
}

//星期几的列表（1 周一 … 7 周日），不认识的取值跳过
std::wstring JoinWeekdays(const std::vector<int>& weekdays)
{
    std::vector<std::wstring> items;
    for (size_t i = 0; i < weekdays.size(); ++i)
    {
        if (weekdays[i] >= 1 && weekdays[i] <= kDaysPerWeek)
            items.push_back(WeekdayName(weekdays[i]));
    }
    return JoinList(items);
}

//定时设置写成「工作日 09:00」「每周的周一、周三 09:00」「每月 1 号 09:00」
std::wstring ScheduleText(const BotSchedule& s)
{
    const std::wstring time = Utf8ToWide(FormatHhMm(s.m_minuteOfDay));
    switch (s.m_kind)
    {
    //每周所选的几天
    case bot_schedule_weekly:
        return TRF(L"每周的{0} {1}", JoinWeekdays(s.m_weekdays), time);
    //每月某一号
    case bot_schedule_monthly:
        return TRF(L"每月 {0} 号 {1}", s.m_monthDay, time);
    //每天、工作日
    default:
        return TRF(L"{0} {1}", ScheduleKindName(s.m_kind), time);
    }
}

//时间段写成「工作日 18:00～09:00」
std::wstring WindowText(const BotTimeWindow& w)
{
    return TRF(L"{0} {1}～{2}", DaysKindName(w.m_days), Utf8ToWide(FormatHhMm(w.m_fromMinute)),
               Utf8ToWide(FormatHhMm(w.m_toMinute)));
}

//触发部分的摘要
std::wstring TriggerSummary(const BotRule& rule, int ownerType)
{
    const BotTrigger& t = rule.m_trigger;
    std::wstring text;
    switch (t.m_type)
    {
    //关键词：关键词、匹配方式、群里是否须 @
    case bot_trigger_keyword:
    {
        std::wstring detail = MatchName(t.m_match);
        if (t.m_needAt && ownerType != bot_owner_personal)
            detail = TRF(L"{0}，须 @", detail);
        text = TRF(L"关键词「{0}」（{1}）", JoinKeywordList(t.m_keywords), detail);
        break;
    }
    //定时：种类与时刻
    case bot_trigger_schedule:
        text = TRF(L"定时：{0}", ScheduleText(t.m_schedule));
        break;
    //收到单聊：有关键词时写出关键词，再写附加条件
    case bot_trigger_single:
    {
        text = TriggerName(t.m_type);
        std::vector<std::wstring> conditions;
        if (!t.m_keywords.empty())
            conditions.push_back(TRF(L"{0}「{1}」", MatchName(t.m_match), JoinKeywordList(t.m_keywords)));
        if (!t.m_presence.empty())
        {
            std::vector<std::wstring> states;
            for (size_t i = 0; i < t.m_presence.size(); ++i)
                states.push_back(PresenceName(t.m_presence[i]));
            conditions.push_back(TRF(L"{0}时", JoinList(states)));
        }
        if (t.m_senders.m_kind == bot_sender_users)
            conditions.push_back(TRF(L"指定的 {0} 人", static_cast<int>(t.m_senders.m_users.size())));
        else if (t.m_senders.m_kind == bot_sender_depts)
            conditions.push_back(TRF(L"指定的 {0} 个部门", static_cast<int>(t.m_senders.m_depts.size())));
        if (t.m_window.m_enabled)
            conditions.push_back(WindowText(t.m_window));
        if (!conditions.empty())
            text = TRF(L"{0}（{1}）", text, JoinList(conditions));
        break;
    }
    //被 @、新成员入群、外部系统推送：只写名字
    default:
        text = TriggerName(t.m_type);
        break;
    }
    //群里的规则限定了时间段时补在后面（收到单聊已经写在括号里了）
    if (t.m_type != bot_trigger_single && t.m_window.m_enabled && TriggerUsesWindow(t.m_type))
        text = TRF(L"{0}（{1}）", text, WindowText(t.m_window));
    return text;
}

} // namespace

std::wstring ErrorText(int code)
{
    switch (code)
    {
    //成功
    case bot_error_ok:
        return std::wstring();
    //服务器内部错误
    case bot_error_internal:
        return TR(L"服务器内部错误，请稍后再试。");
    //登录态失效
    case bot_error_not_login:
        return TR(L"尚未登录或登录已失效，请重新登录。");
    //报文格式错误
    case bot_error_bad_request:
        return TR(L"请求的格式不正确。");
    //通用的没有权限
    case bot_error_permission:
        return TR(L"没有权限执行这个操作。");
    //群不存在
    case bot_error_group_not_exist:
        return TR(L"群不存在或已被解散。");
    //不是群成员
    case bot_error_not_member:
        return TR(L"你已经不是这个群的成员。");
    //机器人不存在
    case bot_error_not_exist:
        return TR(L"机器人不存在或已被删除。");
    //没有权限管理这个机器人
    case bot_error_no_permission:
        return TR(L"没有权限：群机器人只有群主能管理，个人机器人只有本人能管理；有的企业机器人不允许在本群关闭。");
    //数量超出上限
    case bot_error_full:
        return TRF(L"数量超出上限：每群最多 {0} 个群机器人，每人最多 {1} 个个人机器人，每个机器人最多 {2} 条规则。",
                   kGroupBotMax, kPersonalBotMax, kBotRuleMax);
    //内容不合法
    case bot_error_invalid:
        return TR(L"机器人或规则的内容不合法，请检查名称、关键词、回复文字与时间的格式。");
    //企业关闭了相关功能
    case bot_error_disabled:
        return TR(L"企业关闭了这项功能（机器人、群机器人、个人机器人或外部服务）。");
    //含敏感词
    case bot_error_sensitive:
        return TR(L"名称、简介或回复文字含有企业禁止使用的词语。");
    //其余错误码
    default:
        return TRF(L"操作失败（错误码 {0}）。", code);
    }
}

std::wstring NotSentText()
{
    return TR(L"没有连上服务器，请稍后再试。");
}

std::wstring ReasonText(int reason)
{
    switch (reason)
    {
    //命中了规则
    case bot_reason_ok:
        return TR(L"命中了规则");
    //没有规则命中
    case bot_reason_no_rule:
        return TR(L"没有规则命中这段文字");
    //冷却中
    case bot_reason_cooldown:
        return TR(L"命中的规则在冷却中：最近已经代答过这位发送者");
    //主人正在交谈
    case bot_reason_owner_speaking:
        return TR(L"你最近在这个会话里发过言，说明正在交谈");
    //这种消息不触发机器人
    case bot_reason_not_triggerable:
        return TR(L"这种消息不会触发机器人");
    //不认识的原因
    default:
        return TRF(L"原因 {0}", reason);
    }
}

std::wstring TriggerName(int trigger)
{
    switch (trigger)
    {
    //群里被 @
    case bot_trigger_at:
        return TR(L"被 @");
    //关键词
    case bot_trigger_keyword:
        return TR(L"关键词");
    //新成员入群
    case bot_trigger_join:
        return TR(L"新成员入群");
    //定时
    case bot_trigger_schedule:
        return TR(L"定时");
    //收到单聊
    case bot_trigger_single:
        return TR(L"收到单聊");
    //外部系统推送
    case bot_trigger_webhook:
        return TR(L"外部系统推送");
    //不认识的取值
    default:
        return TRF(L"触发方式 {0}", trigger);
    }
}

std::wstring ActionName(int action)
{
    switch (action)
    {
    //回复文字
    case bot_action_reply:
        return TR(L"回复文字");
    //用问答作答
    case bot_action_qa:
        return TR(L"用问答作答");
    //呼叫外部服务
    case bot_action_external:
        return TR(L"呼叫外部服务");
    //不认识的取值
    default:
        return TRF(L"动作 {0}", action);
    }
}

std::wstring MatchName(int match)
{
    switch (match)
    {
    //完全相同
    case bot_match_exact:
        return TR(L"完全相同");
    //以……开头
    case bot_match_prefix:
        return TR(L"以……开头");
    //包含（含不认识的取值）
    default:
        return TR(L"包含");
    }
}

std::wstring ScheduleKindName(int kind)
{
    switch (kind)
    {
    //工作日
    case bot_schedule_workdays:
        return TR(L"工作日");
    //每周所选的几天
    case bot_schedule_weekly:
        return TR(L"每周所选的几天");
    //每月几号
    case bot_schedule_monthly:
        return TR(L"每月几号");
    //每天（含不认识的取值）
    default:
        return TR(L"每天");
    }
}

std::wstring DaysKindName(int days)
{
    switch (days)
    {
    //工作日
    case bot_days_workdays:
        return TR(L"工作日");
    //周末
    case bot_days_weekend:
        return TR(L"周末");
    //每天（含不认识的取值）
    default:
        return TR(L"每天");
    }
}

std::wstring PresenceName(int presence)
{
    switch (presence)
    {
    //离线
    case bot_presence_offline:
        return TR(L"离线");
    //在线
    case bot_presence_online:
        return TR(L"在线");
    //离开
    case bot_presence_away:
        return TR(L"离开");
    //忙碌
    case bot_presence_busy:
        return TR(L"忙碌");
    //不认识的取值
    default:
        return TRF(L"状态 {0}", presence);
    }
}

std::wstring WeekdayName(int weekday)
{
    if (weekday < 1 || weekday > kDaysPerWeek)
        return std::wstring();
    return TR(kWeekdayNames[weekday - 1]);
}

std::wstring SenderKindName(int kind)
{
    switch (kind)
    {
    //指定的人
    case bot_sender_users:
        return TR(L"指定的人");
    //指定的部门
    case bot_sender_depts:
        return TR(L"指定的部门");
    //所有人（含不认识的取值）
    default:
        return TR(L"所有人");
    }
}

std::wstring RuleSummary(const BotRule& rule, int ownerType)
{
    const std::wstring trigger = TriggerSummary(rule, ownerType);
    if (rule.m_trigger.m_type == bot_trigger_webhook)
        return trigger;
    return TRF(kArrowPattern, trigger, ActionName(rule.m_action.m_type));
}

std::wstring UsageText(const BotRecord& bot)
{
    std::wstring out;
    for (size_t i = 0; i < bot.m_rules.size(); ++i)
    {
        const BotRule& r = bot.m_rules[i];
        if (!r.m_enabled)
            continue;
        const BotTrigger& t = r.m_trigger;
        std::wstring line;
        switch (t.m_type)
        {
        //被 @：注明是否用问答作答
        case bot_trigger_at:
            line = (r.m_action.m_type == bot_action_qa) ? std::wstring(TR(L"在群里 @我 并输入问题，我会从群问答里找答案"))
                                                       : std::wstring(TR(L"在群里 @我"));
            break;
        //关键词：按匹配方式写，群里要求 @ 时补一句
        case bot_trigger_keyword:
        {
            const std::wstring kws = JoinKeywordList(t.m_keywords);
            if (t.m_match == bot_match_exact)
                line = TRF(L"发送「{0}」", kws);
            else if (t.m_match == bot_match_prefix)
                line = TRF(L"发送以「{0}」开头的消息", kws);
            else
                line = TRF(L"发送包含「{0}」的消息", kws);
            if (t.m_needAt && bot.m_ownerType != bot_owner_personal)
                line += TR(L"（需要同时 @我）");
            break;
        }
        //新成员入群
        case bot_trigger_join:
            line = TR(L"新成员入群时发送欢迎消息");
            break;
        //定时
        case bot_trigger_schedule:
            line = TRF(L"{0} 发送消息", ScheduleText(t.m_schedule));
            break;
        //收到单聊（个人机器人）
        case bot_trigger_single:
            line = TR(L"别人给你发单聊消息时代你回复");
            break;
        //外部系统推送
        case bot_trigger_webhook:
            line = TR(L"把外部系统推送来的消息发到群里");
            break;
        //不认识的触发方式不写
        default:
            break;
        }
        if (line.empty())
            continue;
        if (!out.empty())
            out += L"\n";
        out += line;
    }
    return out;
}

std::wstring TryResultText(const BotTryResult& result)
{
    const std::wstring ruleName = Utf8ToWide(result.m_ruleName);
    switch (result.m_kind)
    {
    //命中，回复文字
    case bot_decision_reply:
    {
        std::wstring text = ruleName.empty() ? TRF(L"命中了规则，会回复：\n{0}", Utf8ToWide(result.m_text))
                                             : TRF(L"命中规则「{0}」，会回复：\n{1}", ruleName, Utf8ToWide(result.m_text));
        if (result.m_markdown)
            text += TR(L"\n（按 Markdown 显示）");
        return text;
    }
    //命中，呼叫外部服务
    case bot_decision_external:
        return ruleName.empty() ? TRF(L"命中了规则，会把消息发给外部服务：{0}", Utf8ToWide(result.m_url))
                                : TRF(L"命中规则「{0}」，会把消息发给外部服务：{1}", ruleName, Utf8ToWide(result.m_url));
    //没有命中
    default:
        return TRF(L"不会回复：{0}。", ReasonText(result.m_reason));
    }
}

std::wstring VariableHint(int ownerType, int trigger)
{
    //只列出这种规则真正会替换的变量，与服务端 BotRules.cpp 里各处 ExpandVariables 的调用一致：
    //{发送者} 只在被 @、关键词与单聊代答时有值（入群欢迎、定时消息没有发送者）；{主人} 只在个人机器人代答时有值；
    //{新成员} 只在入群欢迎时有值。2026-10-08 实机核对时群机器人的提示里出现了用不上的 {主人}
    std::wstring vars;
    if (trigger == bot_trigger_at || trigger == bot_trigger_keyword || trigger == bot_trigger_single)
        vars = kVarSender;
    if (ownerType == bot_owner_personal)
        vars += (vars.empty() ? L"" : L" ") + std::wstring(kVarOwner);
    vars += (vars.empty() ? L"" : L" ") + std::wstring(kVarDate);
    if (ownerType != bot_owner_personal && trigger == bot_trigger_join)
        vars += std::wstring(L" ") + kVarNewMembers;
    return TRF(L"可用变量：{0}，发送时替换成实际内容。", vars);
}

std::wstring JoinNames(const std::vector<std::wstring>& names, size_t maxShown)
{
    if (maxShown == 0 || names.size() <= maxShown)
        return JoinList(names);
    const std::vector<std::wstring> shown(names.begin(), names.begin() + maxShown);
    return TRF(L"{0}等 {1} 人", JoinList(shown), static_cast<int>(names.size()));
}

std::wstring GroupBotStateText(const GroupBotItem& item)
{
    if (item.m_status != bot_status_active)
        return TR(L"已停用");
    if (item.m_off)
        return TR(L"已在本群关闭");
    if (!item.m_active)
        return TR(L"未生效");
    return TR(L"生效中");
}

} // namespace flamingobot
