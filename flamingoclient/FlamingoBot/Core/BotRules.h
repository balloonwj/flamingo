/**
 *  机器人与规则的本地校验、各种归属可用的触发方式与动作（与服务端 chatserversrc/BotRules.cpp 的 ValidateBot / ValidateRule、
 *  TriggerAllowed / ActionAllowed 一致，改一边要同步改另一边）。
 *
 *  本地校验只为在点「保存」之前就说明问题，最终以服务端为准：外部服务地址的域名白名单只有服务端知道，这里只检查
 *  http / https 开头与长度；企业开关以服务端为准（应答 1405）。
 *
 *  可用的触发方式与动作（docs/机器人方案.md 3.5）：
 *    - 个人机器人：只有「收到单聊」，动作只有「回复文字」；
 *    - 群机器人：被 @、关键词、新成员入群、定时；企业允许外部服务时另有「外部系统推送」与动作「呼叫外部服务」；
 *      被 @ 与关键词可以「用问答作答」，入群与定时只能回复文字；
 *    - 企业机器人：本 DLL 不编辑，规则同群机器人（外部服务总是允许）。
 *
 *  典型用法：
 *      std::wstring why;
 *      if (flamingobot::ValidateBot(bot, allowExternal, &why) != flamingobot::bot_check_ok)
 *          ShowError(why);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <string>
#include <vector>

#include "BotTypes.h"

namespace flamingobot {

/**
 *  某种归属的机器人能否使用某种触发方式。
 *  @param ownerType     归属，取值见 BotOwnerType
 *  @param trigger       触发方式，取值见 BotTriggerType
 *  @param allowExternal 企业是否允许外部服务（群机器人看企业开关，企业机器人总是允许）
 */
bool TriggerAllowed(int ownerType, int trigger, bool allowExternal);

/**
 *  某种触发方式能否搭配某种动作。
 *  @param ownerType     归属
 *  @param trigger       触发方式
 *  @param action        动作，取值见 BotActionType
 *  @param allowExternal 企业是否允许外部服务
 */
bool ActionAllowed(int ownerType, int trigger, int action, bool allowExternal);

/**
 *  某种归属可用的全部触发方式，按编号从小到大（规则编辑窗的「触发方式」下拉框用）。
 */
std::vector<int> AllowedTriggers(int ownerType, bool allowExternal);

/**
 *  某种触发方式可用的全部动作，按编号从小到大（规则编辑窗的「动作」下拉框用）；外部系统推送没有动作，返回空。
 */
std::vector<int> AllowedActions(int ownerType, int trigger, bool allowExternal);

/**
 *  某种触发方式是否需要设置动作（外部系统推送的规则不需要）。
 */
bool TriggerHasAction(int trigger);

/**
 *  某种触发方式能否限定时间段（被 @、关键词、新成员入群、收到单聊可以；定时与外部系统推送不用）。
 */
bool TriggerUsesWindow(int trigger);

/**
 *  新建规则时的默认值：个人机器人为「收到单聊 → 回复文字」（冷却 30 分钟、主人最近 5 分钟发过言不代答）；
 *  群机器人为「被 @ → 回复文字」。
 *  @param ownerType 归属
 */
BotRule MakeDefaultRule(int ownerType);

/**
 *  校验一条规则（与服务端 ValidateRule 相同的检查，外部服务地址只检查开头与长度）。
 *  @param rule          规则
 *  @param ownerType     归属
 *  @param allowExternal 企业是否允许外部服务
 *  @param why           [出参，可为 NULL] 不合法时说明原因的界面文字（已翻译）
 *  @return 取值见 BotCheckResult
 */
int ValidateRule(const BotRule& rule, int ownerType, bool allowExternal, std::wstring* why);

/**
 *  校验一个机器人（基本信息、规则条数，以及逐条规则）。
 *  @param bot           机器人（m_ownerType 决定可用的触发方式）
 *  @param allowExternal 企业是否允许外部服务
 *  @param why           [出参，可为 NULL] 不合法时说明原因的界面文字；规则不合法时以「第 n 条规则：」开头
 *  @return 取值见 BotCheckResult
 */
int ValidateBot(const BotRecord& bot, bool allowExternal, std::wstring* why);

/**
 *  校验结果换成服务端会回的错误码（与服务端 ChatSessionBot.cpp 的 MapCheck 相同）。
 *  @return bot_check_ok 为 0，其余为 1403 / 1404 / 1405
 */
int CheckToErrorCode(int check);

/**
 *  外部服务地址是否以 http:// 或 https:// 开头且不超过 kBotUrlMaxBytes 字节（不检查域名白名单）。
 */
bool IsUrlShapeValid(const std::string& url);

/**
 *  头像底色是否为空或 "#RRGGBB"。
 */
bool IsColorValid(const std::string& color);

/**
 *  一个机器人能否被 @（有启用的被 @ 规则，或要求同时 @ 的关键词规则），与服务端 IsAtable 相同。
 */
bool IsAtable(const BotRecord& bot);

} // namespace flamingobot
