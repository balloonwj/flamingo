/**
 *  规则与规则编辑窗界面字段的互相转换（docs/机器人方案.md 3.5）。
 *
 *  规则编辑窗里的输入框都是文字（关键词一行一个、时刻写成「09:00」、分钟数写成数字），复选框是布尔，下拉框是取值；
 *  BotRuleForm 就是这一组界面字段。RuleToForm 把规则摊开成界面字段，FormToRule 把界面字段解析、校验后拼回规则：
 *  解析失败（时刻格式不对、几号不是数字……）或本地校验不通过（BotRules::ValidateRule）时返回 false 并给出说明文字。
 *  界面不编辑的字段（定时发往的群、自动回复的默认文字标记）原样带回，保存时不会丢；指定的部门 2026-10-08 起经宿主的选择部门对话框编辑。
 *
 *  纯逻辑，单元测试直接调用（Tests/BotCoreTests.cpp）。
 *
 *  典型用法：
 *      flamingobot::BotRuleForm form = flamingobot::RuleToForm(rule);
 *      form.m_keywords = L"VPN\n密码";
 *      std::wstring why;
 *      if (!flamingobot::FormToRule(form, bot_owner_group, allowExternal, rule, &why))
 *          ShowError(why);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "BotTypes.h"

namespace flamingobot {

//在线状态复选框的个数（离线、在线、离开、忙碌，下标即 BotPresence 的取值）
const int kPresenceKinds = 4;

//规则编辑窗的界面字段
struct BotRuleForm
{
    int64_t              m_ruleId;                   //规则 id（新建为 0），原样带回
    std::wstring         m_name;                     //规则名称输入框
    bool                 m_enabled;                  //「启用这条规则」复选框
    int                  m_trigger;                  //「触发方式」下拉框，取值见 BotTriggerType
    std::wstring         m_keywords;                 //关键词输入框：一行一个，或用逗号、分号、顿号分隔
    int                  m_match;                    //「匹配方式」下拉框，取值见 BotMatchMode
    bool                 m_needAt;                   //「必须同时 @ 机器人」复选框（只用于群里的关键词）
    int                  m_scheduleKind;             //「定时」下拉框，取值见 BotScheduleKind
    std::wstring         m_scheduleTime;             //定时的时刻输入框，如「09:00」
    bool                 m_weekdays[kDaysPerWeek];   //每周所选的几天：下标 0 为周一 … 6 为周日
    std::wstring         m_monthDay;                 //每月几号输入框
    std::vector<int64_t> m_scheduleGroups;           //定时发往的群（企业机器人才有，界面不编辑），原样带回
    bool                 m_windowOn;                 //「只在以下时间段生效」复选框
    int                  m_windowDays;               //时间段适用的日子下拉框，取值见 BotDaysKind
    std::wstring         m_windowFrom;               //时间段开始时刻输入框
    std::wstring         m_windowTo;                 //时间段结束时刻输入框
    bool                 m_presence[kPresenceKinds]; //主人在线状态复选框：下标为 BotPresence 的取值；都不选表示不限
    int                  m_senderKind;               //「发送者」下拉框，取值见 BotSenderKind
    std::vector<int64_t> m_senderUsers;              //指定的人（选人对话框选出来的 userid）
    std::vector<int64_t> m_senderDepts;              //指定的部门（含下级部门）；经宿主的选择部门对话框编辑（2026-10-08 起）
    std::wstring         m_cooldown;                 //冷却分钟数输入框
    std::wstring         m_ownerQuiet;               //「主人最近 N 分钟内发过言则不代答」输入框
    int                  m_action;                   //「动作」下拉框，取值见 BotActionType
    std::wstring         m_text;                     //回复文字（或兜底文字）输入框
    bool                 m_markdown;                 //「按 Markdown 显示」复选框
    bool                 m_defaultText;              //自动回复的默认文字标记（界面不编辑），原样带回
    std::wstring         m_url;                      //外部服务地址输入框

    BotRuleForm();
};

/**
 *  把规则摊开成界面字段。
 */
BotRuleForm RuleToForm(const BotRule& rule);

/**
 *  把界面字段解析、校验后拼成规则。只保留当前触发方式用得到的字段：例如定时规则不带关键词，
 *  不是收到单聊的规则不带在线状态、发送者与冷却。
 *  @param form          界面字段
 *  @param ownerType     机器人的归属
 *  @param allowExternal 企业是否允许外部服务
 *  @param out           [出参] 规则；返回 false 时内容不确定
 *  @param why           [出参，可为 NULL] 返回 false 时的说明文字（已翻译）
 *  @return 解析成功且本地校验通过返回 true
 */
bool FormToRule(const BotRuleForm& form, int ownerType, bool allowExternal, BotRule& out, std::wstring* why);

/**
 *  触发方式变了之后把界面字段调整到合法的组合：触发方式不可用时换成第一个可用的；动作不可用时换成第一个可用的；
 *  个人机器人不要求 @。规则编辑窗在用户换了「触发方式」之后调用。
 *  @param form          [入出参] 界面字段
 *  @param ownerType     机器人的归属
 *  @param allowExternal 企业是否允许外部服务
 */
void NormalizeForm(BotRuleForm& form, int ownerType, bool allowExternal);

} // namespace flamingobot
