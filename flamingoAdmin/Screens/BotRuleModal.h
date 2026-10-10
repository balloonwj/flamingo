/**
 *  flamingoAdmin —— 添加 / 编辑一条机器人规则的规则编辑窗 BotRuleModal（docs/机器人方案.md 3.5、4.1）。
 *
 *  在机器人编辑窗里点规则的「添加」「编辑」（或双击规则列表的一行）时弹出，左右两栏：
 *    - 左栏：规则名称与「启用这条规则」；触发方式（企业机器人可用：被 @、关键词、新成员入群、定时、外部系统推送）；
 *      按触发方式显示的条件：关键词与匹配方式、「必须同时 @ 机器人」（关键词），定时种类、时刻、星期、几号与
 *      发往的群（定时），时间段（被 @、关键词、入群）；
 *    - 右栏：动作（被 @ 与关键词可选回复文字、用问答作答、呼叫外部服务；入群与定时只能回复文字）、回复文字或兜底
 *      文字、「按 Markdown 显示」（回复文字）、外部服务地址（呼叫外部服务）；外部系统推送没有动作，右栏只有说明。
 *  触发方式、动作、定时种类变化时显隐相关控件，随后对本窗口（这些控件的父容器）ForceLayout 重新排版
 *  （开发约定文档 UI 约定第 5 条：SetVisible 不会重排）。点「确定」时先在本地校验（botrule::ValidateRule），
 *  通过后由 AdminFrame 写回机器人编辑窗（EnterpriseBotModal::ApplyRuleEdit），规则要在机器人编辑窗点「保存」后才生效。
 *
 *  实现与机器人编辑窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层。弹窗层同一时刻只显示一个弹窗，
 *  所以打开本窗口时机器人编辑窗被隐藏（内容保留），关闭后再显示回来。单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      botapi::BotRule rule;
 *      if (m_botModal->BeginEditRule(&rule))
 *      {
 *          m_ruleModal->ShowForRule(rule, false, screen->Groups());
 *          m_root->ShowModal(m_ruleModal);
 *      }
 *      //点「确定」时
 *      if (m_ruleModal->Validate(&error)) { m_botModal->ApplyRuleEdit(m_ruleModal->GetRule()); m_root->ShowModal(m_botModal); }
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <set>
#include <vector>

#include "DuiControl.h"
#include "Util/BotAdminApi.h"

namespace balloonwjui { class DuiEdit; class DuiButton; class DuiComboBox; class DuiListBox; }

namespace flamingoadmin {

// 添加 / 编辑一条机器人规则的规则编辑窗。
class BotRuleModal : public balloonwjui::DuiControl
{
public:
    BotRuleModal();

    // 打开：用 rule 预填表单。
    //   rule：要编辑的规则（添加时为默认规则）；isNew：true 为添加（标题「添加规则」），false 为编辑；
    //   groups：本企业的群（定时消息的目标群候选，可能为空）。
    void ShowForRule(const botapi::BotRule& rule, bool isNew, const std::vector<botapi::BotGroup>& groups);

    // 群列表到了（或刷新了）：按 groups 重填定时目标群列表，保留已勾选的群。
    void SetGroups(const std::vector<botapi::BotGroup>& groups);

    // 外部服务的域名白名单：本地校验外部服务地址时用。known 为 false 时本地不检查白名单（交给服务端）。
    void SetDomains(const std::vector<CString>& domains, bool known);

    // 触发方式下拉框变了（kIdBotRuleTrigger 的 DUIN_VALUECHANGED）：重填动作下拉框，显隐相关控件并重新排版。
    void OnTriggerChanged();

    // 动作下拉框变了（kIdBotRuleAction 的 DUIN_VALUECHANGED）：显隐「按 Markdown 显示」与外部服务地址并重新排版。
    void OnActionChanged();

    // 定时种类下拉框变了（kIdBotRuleSchedKind 的 DUIN_VALUECHANGED）：显隐星期与几号并重新排版。
    void OnScheduleKindChanged();

    // 「只在以下时间段响应」勾选变了（kIdBotRuleWindowOn 的 DUIN_VALUECHANGED）：时间段的三个控件随之可用或不可用。
    void OnWindowToggled();

    // 表单当前的内容：从打开时的规则出发（保留界面上不编辑的字段），按当前触发方式填好各字段；与当前触发方式无关的
    // 关键词、定时目标群清空，时间段只在被 @、关键词、入群时保留；时刻规范成 HH:MM（不合法时原样保留，由校验报错）。
    botapi::BotRule GetRule() const;

    // 本地校验（botrule::ValidateRule；白名单已拉到时一并检查外部服务地址）。
    //   error：[出参，可为 NULL] 不通过时的中文原因。
    //   返回：通过返回 true。
    bool Validate(CString* error) const;

    // 设置按钮栏左侧的红字错误提示并重绘；空串表示不显示。
    void SetError(const CString& text);

    // 当前的错误提示。
    const CString& ErrorText() const { return m_error; }

    // 是否为添加（false 为编辑）。
    bool IsNew() const { return m_isNew; }

    // 当前选中的触发方式（取值见 botapi::BotTriggerType）。
    int TriggerType() const;

    // 选中某种触发方式并按 OnTriggerChanged 处理（单元测试用）；不是企业机器人可用的触发方式时不做任何事。
    void SetTriggerType(int type);

    // 当前选中的动作（取值见 botapi::BotActionType）；外部系统推送时返回 0。
    int ActionType() const;

    // 选中某种动作并按 OnActionChanged 处理（单元测试用）；当前触发方式不能搭配它时不做任何事。
    void SetActionType(int type);

    // 选中某种定时种类并按 OnScheduleKindChanged 处理（单元测试用）。kind 取值见 botapi::BotScheduleKind。
    void SetScheduleKind(int kind);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    // Layout 算出、OnPaint 照着画的一处文字（字段标题、说明等）
    struct LabelSpot
    {
        RECT     m_rc;         // 文字所在矩形
        CString  m_text;       // 文字
        COLORREF m_color;      // 文字颜色
        bool     m_wrap;       // 是否自动换行（说明段落）
    };

    // 记下一处要画的文字
    void AddLabel(const RECT& rc, const CString& text, COLORREF color, bool wrap);

    // 按当前触发方式重填动作下拉框，尽量保留 keepType 这个动作；不能搭配时选第一项
    void RefillActions(int keepType);

    // 按触发方式、动作、定时种类显隐控件，按「只在以下时间段响应」设置时间段控件是否可用，然后重新排版
    void ApplyVisibility();

    // 按 m_groups 重填定时目标群列表，勾选 checked 里的群
    void FillGroups(const std::set<long long>& checked);

    // 定时目标群列表里当前勾选的群，按列表顺序
    std::vector<long long> CheckedGroups() const;

    bool                           m_isNew;          // 是否为添加
    botapi::BotRule                m_original;       // 打开时的规则（保留界面上不编辑的字段，如 ID、个人机器人用的条件）
    CString                        m_error;          // 红字错误提示，空串表示不显示
    RECT                           m_panelRect;      // 卡片矩形
    RECT                           m_errorRect;      // 按钮栏左侧错误提示的矩形
    std::vector<LabelSpot>         m_labels;         // Layout 算出的字段标题与说明
    std::vector<botapi::BotGroup>  m_groups;         // 定时目标群列表里的群，下标与列表行一致
    std::vector<CString>           m_domains;        // 外部服务的域名白名单
    bool                           m_domainsKnown;   // 白名单是否已经拉到
    std::vector<int>               m_triggerTypes;   // 触发方式下拉框各项对应的触发方式
    std::vector<int>               m_actionTypes;    // 动作下拉框各项对应的动作（随触发方式变化）
    balloonwjui::DuiEdit*          m_name;           // 规则名称输入框；所有权在子控件列表
    balloonwjui::DuiButton*        m_enabled;        // 「启用这条规则」勾选框
    balloonwjui::DuiComboBox*      m_trigger;        // 触发方式下拉框
    balloonwjui::DuiEdit*          m_keywords;       // 关键词输入框
    balloonwjui::DuiComboBox*      m_match;          // 匹配方式下拉框
    balloonwjui::DuiButton*        m_needAt;         // 「必须同时 @ 机器人」勾选框
    balloonwjui::DuiComboBox*      m_schedKind;      // 定时种类下拉框
    balloonwjui::DuiEdit*          m_schedTime;      // 定时时刻输入框
    balloonwjui::DuiEdit*          m_schedDay;       // 每月几号输入框
    balloonwjui::DuiButton*        m_weekdays[botapi::kDaysPerWeek]; // 每周定时的星期勾选框，下标为星期取值减一
    balloonwjui::DuiListBox*       m_schedGroups;    // 定时发往的群（带勾选框）
    balloonwjui::DuiButton*        m_windowOn;       // 「只在以下时间段响应」勾选框
    balloonwjui::DuiComboBox*      m_windowDays;     // 时间段的日子下拉框
    balloonwjui::DuiEdit*          m_windowFrom;     // 时间段起始时刻输入框
    balloonwjui::DuiEdit*          m_windowTo;       // 时间段结束时刻输入框
    balloonwjui::DuiComboBox*      m_action;         // 动作下拉框
    balloonwjui::DuiEdit*          m_text;           // 回复文字 / 兜底文字输入框（多行）
    balloonwjui::DuiButton*        m_markdown;       // 「按 Markdown 显示」勾选框
    balloonwjui::DuiEdit*          m_url;            // 外部服务地址输入框
    balloonwjui::DuiButton*        m_btnClose;       // 右上角关闭按钮
    balloonwjui::DuiButton*        m_btnCancel;      // 「取消」按钮
    balloonwjui::DuiButton*        m_btnSave;        // 「确定」按钮
};

} // namespace flamingoadmin
