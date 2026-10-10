/**
 *  规则编辑窗的控件树（docs/机器人方案.md 3.5、4.2、4.3）。
 *
 *  表单按当前的界面字段（BotRuleForm）一次建出，只建当前触发方式与动作用得到的控件：
 *    - 规则名称、启用、触发方式（按归属与企业开关列出可用的几种）；
 *    - 关键词与收到单聊：关键词、匹配方式；群里的关键词另有「必须同时 @ 机器人」；
 *    - 定时：种类、时刻，每周时选星期，每月时填几号；
 *    - 收到单聊：主人在线状态（可多选）、发送者范围（所有人 / 指定的人，可「选择…」）、冷却分钟、主人正在交谈分钟；
 *    - 被 @、关键词、入群、收到单聊：可限定时间段；
 *    - 动作（外部系统推送没有动作）：回复文字（可按 Markdown 显示，提示可用的变量）、用问答作答（兜底文字）、
 *      呼叫外部服务（地址与兜底文字）。
 *  触发方式、动作、定时种类、发送者范围或「时间段」复选框变了时，窗口先把各控件的值读回界面字段（HarvestRuleForm），
 *  再清空内容区重建（BuildRuleForm）。控件树与窗口分开建，便于单元测试按设计尺寸排版。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      RuleFormControls controls;
 *      flamingobot::BuildRuleForm(frame.m_content, form, ctx, font, kRuleContentWidth, controls);
 *      ...
 *      flamingobot::HarvestRuleForm(controls, form);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Core/BotRuleForm.h"
#include "BotWidgets.h"

namespace flamingobot {

//---- 窗口尺寸，单位：像素（DuiFrameWindow 的整窗尺寸，含标题栏，UI 约定第 7 条） ----
const int kRuleWindowWidth = 600;    //窗口宽
const int kRuleWindowHeight = 640;   //窗口高
//内容区可用的宽度：窗口宽减两侧内边距与滚动条留白
const int kRuleContentWidth = kRuleWindowWidth - 2 * kBotPadX - kBotScrollGutter;

//---- 控件 id ----
const UINT kIdRuleName = 8801;          //规则名称
const UINT kIdRuleEnabled = 8802;       //「启用这条规则」
const UINT kIdRuleTrigger = 8803;       //触发方式（DUIN_VALUECHANGED：重建表单）
const UINT kIdRuleKeywords = 8804;      //关键词
const UINT kIdRuleMatch = 8805;         //匹配方式
const UINT kIdRuleNeedAt = 8806;        //「必须同时 @ 机器人」
const UINT kIdRuleScheduleKind = 8807;  //定时种类（DUIN_VALUECHANGED：重建表单）
const UINT kIdRuleScheduleTime = 8808;  //定时的时刻
const UINT kIdRuleWeekdayFirst = 8810;  //星期复选框：周一为 8810 … 周日为 8816
const UINT kIdRuleMonthDay = 8820;      //每月几号
const UINT kIdRulePresenceFirst = 8821; //在线状态复选框：离线 8821、在线 8822、离开 8823、忙碌 8824
const UINT kIdRuleSenderKind = 8826;    //发送者范围（DUIN_VALUECHANGED：重建表单）
const UINT kIdRulePickSenders = 8827;   //「选择…」：指定的人时弹出选择联系人对话框，指定的部门时弹出选择部门对话框
const UINT kIdRuleCooldown = 8828;      //冷却分钟
const UINT kIdRuleOwnerQuiet = 8829;    //主人正在交谈分钟
const UINT kIdRuleWindowOn = 8830;      //「只在以下时间段生效」（DUIN_VALUECHANGED：重建表单）
const UINT kIdRuleWindowDays = 8831;    //时间段适用的日子
const UINT kIdRuleWindowFrom = 8832;    //时间段开始
const UINT kIdRuleWindowTo = 8833;      //时间段结束
const UINT kIdRuleAction = 8834;        //动作（DUIN_VALUECHANGED：重建表单）
const UINT kIdRuleText = 8835;          //回复文字 / 兜底文字
const UINT kIdRuleMarkdown = 8836;      //「按 Markdown 显示」
const UINT kIdRuleUrl = 8837;           //外部服务地址
const UINT kIdRuleCancel = 8838;        //「取消」
const UINT kIdRuleOk = 8839;            //「确定」

//建表单时需要的上下文
struct RuleFormContext
{
    int          m_ownerType;      //机器人的归属，取值见 BotOwnerType
    bool         m_allowExternal;  //企业是否允许外部服务
    std::wstring m_senderNames;    //「指定的人」或「指定的部门」的名字（已拼好，如「张三、李四等 5 人」）；没有选时为空

    RuleFormContext()
        : m_ownerType(bot_owner_group)
        , m_allowExternal(false)
    {
    }
};

//建好的表单里窗口要用到的控件（都归控件树所有，借用；当前触发方式用不到的为 NULL）
struct RuleFormControls
{
    balloonwjui::DuiEdit*      m_name;                       //规则名称
    balloonwjui::DuiButton*    m_enabled;                    //「启用这条规则」
    balloonwjui::DuiComboBox*  m_trigger;                    //触发方式
    balloonwjui::DuiEdit*      m_keywords;                   //关键词
    balloonwjui::DuiComboBox*  m_match;                      //匹配方式
    balloonwjui::DuiButton*    m_needAt;                     //「必须同时 @ 机器人」
    balloonwjui::DuiComboBox*  m_scheduleKind;               //定时种类
    balloonwjui::DuiEdit*      m_scheduleTime;               //定时的时刻
    balloonwjui::DuiButton*    m_weekdays[kDaysPerWeek];     //星期复选框（周一 … 周日）
    balloonwjui::DuiEdit*      m_monthDay;                   //每月几号
    balloonwjui::DuiButton*    m_presence[kPresenceKinds];   //在线状态复选框（下标为 BotPresence）
    balloonwjui::DuiComboBox*  m_senderKind;                 //发送者范围
    balloonwjui::DuiButton*    m_pickSenders;                //「选择…」
    balloonwjui::DuiEdit*      m_cooldown;                   //冷却分钟
    balloonwjui::DuiEdit*      m_ownerQuiet;                 //主人正在交谈分钟
    balloonwjui::DuiButton*    m_windowOn;                   //「只在以下时间段生效」
    balloonwjui::DuiComboBox*  m_windowDays;                 //时间段适用的日子
    balloonwjui::DuiEdit*      m_windowFrom;                 //时间段开始
    balloonwjui::DuiEdit*      m_windowTo;                   //时间段结束
    balloonwjui::DuiComboBox*  m_action;                     //动作
    balloonwjui::DuiEdit*      m_text;                       //回复文字 / 兜底文字
    balloonwjui::DuiButton*    m_markdown;                   //「按 Markdown 显示」
    balloonwjui::DuiEdit*      m_url;                        //外部服务地址
    std::vector<int>           m_triggerValues;              //触发方式下拉框各项对应的取值
    std::vector<int>           m_actionValues;               //动作下拉框各项对应的取值
    std::vector<int>           m_senderValues;               //发送者范围下拉框各项对应的取值

    RuleFormControls();
};

/**
 *  按界面字段在内容区里建表单（调用方先清空内容区）。
 *  @param content      内容区的竖排容器（借用）
 *  @param form         界面字段
 *  @param ctx          归属、企业开关与已选的人
 *  @param font         字体（借用）
 *  @param contentWidth 内容区宽度，单位：像素（多行说明按它算高度）
 *  @param out          [出参] 窗口要用到的控件
 */
void BuildRuleForm(balloonwjui::DuiVBox* content, const BotRuleForm& form, const RuleFormContext& ctx, HFONT font,
                   int contentWidth, RuleFormControls& out);

/**
 *  把表单里各控件的当前值读回界面字段；没有建出来的控件对应的字段保持不变。
 */
void HarvestRuleForm(const RuleFormControls& controls, BotRuleForm& form);

/**
 *  建规则编辑窗标题栏以下的骨架：可滚动的表单区，底部「出错说明 | 取消 | 确定」。
 *  @param out [出参] 骨架里的控件（m_buttons 依次为取消、确定）
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildRuleWindowBody(HFONT font, ScrollFrameControls& out);

} // namespace flamingobot
