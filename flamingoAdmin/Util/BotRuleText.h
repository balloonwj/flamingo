/**
 *  flamingoAdmin —— 企业机器人的规则文字与本地校验（纯逻辑，docs/机器人方案.md 第 7 节）。
 *
 *  机器人编辑窗、规则编辑窗与列表页用到的全部「数据 ↔ 界面文字」转换都在这里：关键词的逗号 / 换行分隔与数组互转、
 *  HH:MM 时刻的解析与格式化、星期勾选与数组互转、触发方式 / 动作 / 匹配方式等的中文名称、应用范围一栏的文字、
 *  规则摘要（规则列表每行一条）、「试一试」结果的说明，以及提交前的本地校验（与服务端 BotRules::ValidateBot
 *  的规则一致，服务端仍会再校验一次）。不依赖窗口，单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法：
 *      rule.m_trigger.m_keywords = botrule::SplitKeywords(editText);
 *      CString error;
 *      if (!botrule::ValidateBot(bot, &domains, &error)) { modal->SetError(error); return; }
 *      list->AddItem(botrule::RuleSummary(rule));
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <vector>

#include "Util/BotAdminApi.h"

namespace flamingoadmin { namespace botrule {

// 一天的分钟数：时刻按「当天第几分钟」表示，取值 0～kMinutesPerDay-1
const int kMinutesPerDay = 1440;

// 规则摘要里回复文字最多显示多少个字，超出部分以「…」代替。取 10 是为了让常见的摘要在机器人编辑窗的规则列表里
// 一行放得下：原来的 16 会被列表再截一次，末尾显示成「……」再加「...」（2026-10-08 实机核对）
const int kSummaryTextMaxChars = 10;

// 应用范围一栏最多列出几个群名
const int kScopeNamesMax = 3;

// 头像底色的候选个数（编辑窗里的色块数，见 PaletteColor）
const int kPaletteCount = 8;

// ---- 关键词 ----

/**
 *  把输入框里的关键词拆成数组：以半角 / 全角逗号、顿号、分号与换行分隔，去掉每个关键词首尾的空白，
 *  丢掉空项与完全重复的项，保持原顺序。
 */
std::vector<CString> SplitKeywords(const CString& text);

/**
 *  把关键词数组拼成输入框里的文字：以全角逗号「，」连接（再经 SplitKeywords 拆开时与原数组相同）。
 */
CString JoinKeywords(const std::vector<CString>& keywords);

// ---- 时刻 HH:MM ----

/**
 *  解析时刻文字：接受 "H:MM" 与 "HH:MM"，冒号可为全角「：」，首尾空白忽略。
 *    text：输入；minuteOfDay：[出参，可为 NULL] 成功时为当天第几分钟（0～1439）。
 *  @return 格式正确且小时为 0～23、分钟为 0～59 时返回 true
 */
bool ParseHhMm(const CString& text, int* minuteOfDay);

/**
 *  把当天第几分钟格式化成 "HH:MM"（两位小时、两位分钟）；超出 0～1439 时返回空串。
 */
CString FormatHhMm(int minuteOfDay);

/**
 *  把时刻文字规范成 "HH:MM"（如 "9:5" 不合法、"9:05" 变成 "09:05"）；不合法时返回空串。
 */
CString NormalizeHhMm(const CString& text);

// ---- 星期 ----

/**
 *  把星期数组（1 周一 … 7 周日）换成位掩码：第 d 天对应第 d-1 位。超出 1～7 的取值忽略。
 */
int WeekdayMask(const std::vector<int>& weekdays);

/**
 *  把位掩码换回星期数组：按 1～7 升序列出置位的那几天。
 */
std::vector<int> WeekdaysFromMask(int mask);

/**
 *  星期的中文名：1「周一」… 7「周日」；超出范围返回空串。
 */
CString WeekdayName(int day);

// ---- 中文名称 ----

// 触发方式的中文名（取值见 botapi::BotTriggerType），如「被 @」「关键词」；不认识的取值返回「未知触发方式」
CString TriggerName(int type);

// 动作的中文名（取值见 botapi::BotActionType），如「回复文字」「用问答作答」「呼叫外部服务」
CString ActionName(int type);

// 关键词匹配方式的中文名（取值见 botapi::BotMatchMode）：「包含」「完全相同」「以……开头」
CString MatchName(int match);

// 定时种类的中文名（取值见 botapi::BotScheduleKind）：「每天」「工作日」「每周」「每月」
CString ScheduleKindName(int kind);

// 时间段日子的中文名（取值见 botapi::BotWindowDays）：「每天」「工作日」「周末」
CString WindowDaysName(int days);

// 机器人状态的中文名：kBotStatusActive「启用」，kBotStatusDisabled「停用」
CString StatusName(int status);

// ---- 触发方式与动作的搭配（企业机器人，与服务端 TriggerAllowed / ActionAllowed 一致）----

/**
 *  企业机器人可用的触发方式，按界面下拉框的顺序：被 @、关键词、新成员入群、定时、外部系统推送。
 */
std::vector<int> AllowedTriggers();

/**
 *  某种触发方式可搭配的动作，按界面下拉框的顺序：被 @ 与关键词为回复文字、用问答作答、呼叫外部服务；
 *  入群与定时只能回复文字；外部系统推送没有动作（返回空数组）。
 */
std::vector<int> AllowedActions(int trigger);

// 企业机器人能否使用某种触发方式
bool IsTriggerAllowed(int trigger);

// 某种触发方式能否搭配某种动作；外部系统推送不使用动作，恒为 true
bool IsActionAllowed(int trigger, int action);

// 某种触发方式是否可设时间段（被 @、关键词、新成员入群）
bool HasWindow(int trigger);

// 来源为 GitHub / GitLab 仓库通知的机器人在本页只读（不给编辑、删除）
bool IsReadOnlySource(int source);

// ---- 头像底色 ----

/**
 *  编辑窗里第 index 个色块的颜色 "#RRGGBB"；index 越界时返回第 0 个（新建机器人的默认底色）。
 */
CString PaletteColor(int index);

/**
 *  颜色在色块里的下标（不区分大小写）；不在色块里返回 -1。
 */
int PaletteIndexOf(const CString& color);

/**
 *  "#RRGGBB" 换成 COLORREF；为空或格式不对时返回 fallback。
 */
COLORREF ColorFromHex(const CString& color, COLORREF fallback);

// 头像底色是否合法：为空，或 "#" 加 6 位十六进制数
bool IsValidColor(const CString& color);

/**
 *  头像上画的字：有头像文字时取头像文字，否则取名称的第一个字；都没有时返回空串。
 */
CString AvatarGlyph(const botapi::BotItem& bot);

// ---- 界面文字 ----

/**
 *  列表里「应用范围」一栏的文字：全部群时为「全部群」；所选的群时为「N 个群：甲、乙、丙…」（最多列出 3 个名字，
 *  名字取不到的群写群号）。
 *    bot：机器人；groups：本企业的群（可能还没拉到，为空）。
 */
CString ScopeText(const botapi::BotItem& bot, const std::vector<botapi::BotGroup>& groups);

/**
 *  定时的文字：「每天 09:00」「工作日 09:00」「每周一、三 09:00」「每月 1 号 09:00」。
 */
CString ScheduleText(const botapi::BotSchedule& schedule);

/**
 *  规则摘要（规则列表里的一行）：「[停用] 名称：触发条件（时间段）→ 动作」，例如
 *  「VPN 问题：关键词「VPN、密码」（包含，须同时 @）→ 回复「重启客户端…」」。
 */
CString RuleSummary(const botapi::BotRule& rule);

/**
 *  「试一试」结果的说明：命中时写明规则名称与会回复的内容（或会呼叫的外部服务地址），没有命中时按 reason 说明原因。
 */
CString TryResultText(const botapi::BotTryResult& result);

// ---- 外部服务地址与域名白名单 ----

/**
 *  取外部服务地址的主机（小写、去掉端口）：地址须以 http:// 或 https:// 开头，主机不能为空、不能带用户名（@）。
 *    url：地址；host：[出参，可为 NULL] 主机。
 *  @return 地址格式可用时返回 true
 */
bool ParseUrlHost(const CString& url, CString* host);

/**
 *  地址的主机是否在域名白名单内：等于其中一项，或是其中一项的子域名（与服务端 IsUrlAllowed 一致）。
 */
bool IsUrlInWhitelist(const CString& url, const std::vector<CString>& domains);

/**
 *  域名是否合法（与服务端 IsValidDomain 一致）：只含字母、数字、点号与连字符，至少一个点，
 *  不以点号或连字符开头、不以点号结尾、没有连续的点号，最多 128 字节。
 */
bool IsValidDomain(const CString& domain);

/**
 *  把多行输入框里的白名单拆成数组：一行一个，去掉首尾空白，转小写，丢掉空行与重复项，保持原顺序。
 */
std::vector<CString> SplitDomains(const CString& text);

/**
 *  把白名单数组拼成多行输入框里的文字：每项一行，以 "\r\n" 分隔。
 */
CString JoinDomains(const std::vector<CString>& domains);

/**
 *  保存白名单前的本地校验：最多 50 条，每条都是合法域名。
 *    domains：SplitDomains 的结果；error：[出参，可为 NULL] 不通过时的原因。
 *  @return 通过返回 true
 */
bool ValidateDomains(const std::vector<CString>& domains, CString* error);

// ---- 提交前的本地校验（与服务端 BotRules::ValidateBot 一致）----

/**
 *  校验一条规则：规则名称最多 32 字；触发方式与动作的搭配；关键词 1～20 个、每个最多 32 字；定时的时刻为 HH:MM、
 *  每周至少一天、每月 1～31 号；时间段的起止时刻；回复文字最多 2048 字且「回复文字」动作不能为空；外部服务地址为
 *  http / https。
 *    rule：要校验的规则；domains：外部服务域名白名单，传 NULL 表示不在本地检查白名单（交给服务端）；
 *    error：[出参，可为 NULL] 不通过时的中文原因。
 *  @return 通过返回 true
 */
bool ValidateRule(const botapi::BotRule& rule, const std::vector<CString>* domains, CString* error);

/**
 *  校验整个机器人：名称 1～20 字、简介最多 100 字、头像文字最多 2 字、头像底色为空或 #RRGGBB、选了「所选的群」时
 *  至少一个群、规则最多 50 条，再逐条 ValidateRule（原因前加「第 N 条规则：」）。
 *    bot：要校验的机器人；domains、error：同 ValidateRule。
 *  @return 通过返回 true
 */
bool ValidateBot(const botapi::BotItem& bot, const std::vector<CString>* domains, CString* error);

/**
 *  按 Unicode 字符计数（代理对算一个），与服务端按 UTF-8 字符计数一致。
 */
int CharCount(const CString& text);

}} // namespace flamingoadmin::botrule
