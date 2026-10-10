/**
 *  机器人管理界面的文字（都经 TR 系列取当前界面语言的译文，docs/多语言方案.md）：
 *  错误码与「试一试」原因的说明、触发方式 / 动作 / 匹配方式 / 定时种类 / 在线状态等取值的名字、一条规则的摘要、
 *  一个机器人的用法说明、「试一试」结果的说明、回复文字里可用的变量。
 *
 *  纯逻辑（只依赖 Source/Common/I18n），单元测试在没有语言包时直接比较简体中文原文。
 *
 *  典型用法：
 *      label->SetText(flamingobot::RuleSummary(rule, bot.m_ownerType).c_str());
 *      error->SetText(flamingobot::ErrorText(reply.m_code).c_str());
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <string>
#include <vector>

#include "BotTypes.h"

namespace flamingobot {

/**
 *  应答错误码的说明（1401～1406 与几个通用错误码；其余写成「操作失败（错误码 n）。」）。
 */
std::wstring ErrorText(int code);

//请求没能发出（没有连上服务器）时的说明
std::wstring NotSentText();

/**
 *  「试一试」没有回复的原因（2809 的 reason）。
 */
std::wstring ReasonText(int reason);

//触发方式的名字，如「被 @」「关键词」
std::wstring TriggerName(int trigger);

//动作的名字，如「回复文字」「用问答作答」
std::wstring ActionName(int action);

//关键词匹配方式的名字，如「包含」
std::wstring MatchName(int match);

//定时种类的名字，如「每天」「工作日」
std::wstring ScheduleKindName(int kind);

//时间段适用的日子，如「每天」「周末」
std::wstring DaysKindName(int days);

//在线状态的名字，如「离开」
std::wstring PresenceName(int presence);

//星期几的名字（1 周一 … 7 周日）
std::wstring WeekdayName(int weekday);

//发送者范围的名字，如「所有人」「指定的人」
std::wstring SenderKindName(int kind);

/**
 *  一条规则的摘要（规则列表的一行），如「关键词「VPN」（包含，须 @）→ 回复文字」「收到单聊（离开、忙碌时）→ 回复文字」。
 *  @param rule      规则
 *  @param ownerType 归属（群里的关键词才显示「须 @」）
 */
std::wstring RuleSummary(const BotRule& rule, int ownerType);

/**
 *  一个机器人的用法说明：每条启用的规则一行，如「在群里 @我 并输入问题，我会用问答作答」；没有启用的规则时为空串。
 *  @param bot 机器人（须带规则）
 */
std::wstring UsageText(const BotRecord& bot);

/**
 *  「试一试」结果的说明：命中时写出规则名与会回复的文字（或会呼叫的外部服务地址），没有命中时写出原因。
 */
std::wstring TryResultText(const BotTryResult& result);

/**
 *  回复文字里可用的变量的提示，如「可用变量：{发送者} {主人} {日期}，发送时替换成实际内容。」。
 *  @param ownerType 归属（个人机器人没有 {新成员}）
 *  @param trigger   触发方式（入群规则才有 {新成员}）
 */
std::wstring VariableHint(int ownerType, int trigger);

/**
 *  把多个名字用顿号连成一串；超过 maxShown 个时写成「张三、李四等 5 人」。
 *  @param names    名字，按显示顺序
 *  @param maxShown 最多写出几个名字（大于 0）
 */
std::wstring JoinNames(const std::vector<std::wstring>& names, size_t maxShown);

/**
 *  机器人在本群的状态文字（管理窗一行里的小字）：「生效中」「已停用」「已在本群关闭」。
 */
std::wstring GroupBotStateText(const GroupBotItem& item);

} // namespace flamingobot
