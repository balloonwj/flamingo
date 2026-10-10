/**
 *  flamingoAdmin —— 机器人规则编辑窗实现，说明见 BotRuleModal.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/BotRuleModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Util/BotRuleText.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/List/DuiListBox.h"

using namespace balloonwjui;

namespace flamingoadmin {

using namespace flamingoadmin::botapi;

namespace {

// ---- 规则编辑窗几何（96 dpi 逻辑像素），与机器人编辑窗同一套风格 ----
const int kPanelW         = 820;  // 卡片宽度：左右两栏
const int kPanelH         = 600;  // 卡片高度（窗口够高时）
const int kMinPanelH      = 520;  // 窗口很矮时卡片最低高度
const int kModalMargin    = 16;   // 卡片与窗口上下边缘至少留出的距离
const int kHeaderH        = 56;   // 标题栏高度
const int kFooterH        = 64;   // 按钮栏高度
const int kPadH           = 24;   // 卡片左右内边距
const int kPadV           = 14;   // 主体区上下内边距
const int kLeftColW       = 360;  // 左栏宽度
const int kColGap         = 28;   // 左右两栏之间的间距
const int kBackdropAlpha  = 110;  // 遮罩不透明度（0-255）
const int kTitlePt        = 13;   // 标题字号（磅）
const int kLabelH         = 20;   // 字段标题行高
const int kEditH          = 32;   // 单行输入框与下拉框高度
const int kCheckH         = 28;   // 单独一行的勾选框高度
const int kFieldGap       = 12;   // 字段之间的纵向间距
const int kInlineGap      = 8;    // 同一行里控件之间的间距
const int kEnabledW       = 130;  // 「启用这条规则」勾选框宽度
const int kTriggerComboW  = 200;  // 触发方式下拉框宽度
const int kMatchComboW    = 140;  // 匹配方式下拉框宽度
const int kSchedKindW     = 100;  // 定时种类下拉框宽度
const int kTimeEditW      = 72;   // 时刻输入框宽度
const int kDayEditW       = 56;   // 几号输入框宽度
const int kUnitLabelW     = 24;   // 「号」「至」文字的宽度
const int kWeekdayW       = 46;   // 每个星期勾选框的宽度
const int kMinGroupListH  = 72;   // 定时目标群列表最低高度
const int kWindowDaysW    = 100;  // 时间段日子下拉框宽度
const int kHintH          = 18;   // 小字说明一行的高度
const int kActionComboW   = 200;  // 动作下拉框宽度
const int kMinTextH       = 96;   // 回复文字输入框最低高度
const int kNoteH          = 140;  // 外部系统推送说明段落的高度
const int kBtnH           = 34;   // 底部按钮高度
const int kBtnGap         = 8;    // 底部按钮间距
const int kBtnWCancel     = 72;   // 「取消」按钮宽度
const int kBtnWSave       = 88;   // 「确定」按钮宽度
const int kCloseBtnSize   = 28;   // 右上角关闭按钮边长

// 时间段没有设置过时输入框里预填的起止时刻
const TCHAR kDefaultWindowFrom[] = _T("09:00");
const TCHAR kDefaultWindowTo[]   = _T("18:00");

// 每周定时的星期勾选框上的字，下标为星期取值减一
const TCHAR* const kWeekdayTexts[kDaysPerWeek] =
{
    _T("一"), _T("二"), _T("三"), _T("四"), _T("五"), _T("六"), _T("日"),
};

std::unique_ptr<DuiEdit> MakeEdit(UINT id, LPCTSTR placeholder, int maxLength)
{
    std::unique_ptr<DuiEdit> edit(new DuiEdit());
    edit->SetCtrlId(id);
    edit->SetPlaceholder(placeholder);
    edit->SetShowBorder(true);
    edit->SetBgColor(theme::kInk0);
    if (maxLength > 0)
        edit->SetMaxLength(maxLength);
    return edit;
}

std::unique_ptr<DuiButton> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetVariant(variant);
    button->SetTextPointSize(theme::kFontSizeNormal);
    return button;
}

std::unique_ptr<DuiButton> MakeCheckbox(UINT id, LPCTSTR text)
{
    std::unique_ptr<DuiButton> box(new DuiButton());
    box->SetButtonType(DuiButton::StyleCheckbox);
    //Variant 必须选一个透明的，否则会按普通按钮那套填充与边框来画（见 ClientVersionModal 的「强制升级」）
    box->SetVariant(DuiButton::Variant::Text);
    box->SetCtrlId(id);
    box->SetText(text);
    box->SetTextPointSize(theme::kFontSizeNormal);
    return box;
}

std::unique_ptr<DuiComboBox> MakeCombo(UINT id)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(id);
    return combo;
}

// 下拉框选中第 index 项；越界时选第一项
void SelectClamped(DuiComboBox* combo, int index)
{
    if (combo == nullptr)
        return;
    if (index < 0 || index >= combo->GetCount())
        index = 0;
    combo->SetCurSel(index, false);
}

// 输入框里的时刻：合法时规范成 HH:MM，不合法时原样保留（去掉首尾空白），由校验报错
CString TimeFromEdit(const DuiEdit* edit)
{
    if (edit == nullptr)
        return CString();
    CString text = edit->GetText();
    text.Trim();
    const CString normalized = botrule::NormalizeHhMm(text);
    return normalized.IsEmpty() ? text : normalized;
}

// 设置控件显隐（控件为空时不做任何事）
void ShowCtrl(DuiControl* ctrl, bool visible)
{
    if (ctrl != nullptr)
        ctrl->SetVisible(visible);
}

// 把控件摆到 rc（控件为空时不做任何事）。用 ForceLayout 而不是 SetRect：矩形没变时也要重排控件内部
void PlaceCtrl(DuiControl* ctrl, const RECT& rc)
{
    if (ctrl != nullptr)
        ctrl->ForceLayout(rc);
}

} // namespace

BotRuleModal::BotRuleModal()
    : m_isNew(true)
    , m_domainsKnown(false)
    , m_name(nullptr)
    , m_enabled(nullptr)
    , m_trigger(nullptr)
    , m_keywords(nullptr)
    , m_match(nullptr)
    , m_needAt(nullptr)
    , m_schedKind(nullptr)
    , m_schedTime(nullptr)
    , m_schedDay(nullptr)
    , m_schedGroups(nullptr)
    , m_windowOn(nullptr)
    , m_windowDays(nullptr)
    , m_windowFrom(nullptr)
    , m_windowTo(nullptr)
    , m_action(nullptr)
    , m_text(nullptr)
    , m_markdown(nullptr)
    , m_url(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSave(nullptr)
{
    SetCtrlId(kIdBotRule);
    ::SetRectEmpty(&m_panelRect);
    ::SetRectEmpty(&m_errorRect);
    for (int i = 0; i < kDaysPerWeek; ++i)
        m_weekdays[i] = nullptr;
    m_triggerTypes = botrule::AllowedTriggers();

    // ---- 左栏：名称、触发方式与条件 ----
    std::unique_ptr<DuiEdit> name = MakeEdit(kIdBotRuleName, _T("例如：VPN 问题"), kBotRuleNameMaxChars);
    m_name = name.get();
    AddChild(std::move(name));

    std::unique_ptr<DuiButton> enabled = MakeCheckbox(kIdBotRuleEnabled, _T("启用这条规则"));
    m_enabled = enabled.get();
    AddChild(std::move(enabled));

    std::unique_ptr<DuiComboBox> trigger = MakeCombo(kIdBotRuleTrigger);
    for (size_t i = 0; i < m_triggerTypes.size(); ++i)
        trigger->AddString(m_triggerTypes[i] == kTriggerAt ? CString(_T("被 @ 时")) : botrule::TriggerName(m_triggerTypes[i]));
    m_trigger = trigger.get();
    AddChild(std::move(trigger));

    std::unique_ptr<DuiEdit> keywords = MakeEdit(kIdBotRuleKeywords, _T("例如：VPN，密码"), 0);
    m_keywords = keywords.get();
    AddChild(std::move(keywords));

    std::unique_ptr<DuiComboBox> match = MakeCombo(kIdBotRuleMatch);
    //下标加一即匹配方式取值：1 包含、2 完全相同、3 以……开头
    match->AddString(botrule::MatchName(kMatchContains));
    match->AddString(botrule::MatchName(kMatchExact));
    match->AddString(botrule::MatchName(kMatchPrefix));
    m_match = match.get();
    AddChild(std::move(match));

    std::unique_ptr<DuiButton> needAt = MakeCheckbox(kIdBotRuleNeedAt, _T("必须同时 @ 机器人"));
    m_needAt = needAt.get();
    AddChild(std::move(needAt));

    std::unique_ptr<DuiComboBox> schedKind = MakeCombo(kIdBotRuleSchedKind);
    //下标加一即定时种类取值：1 每天、2 工作日、3 每周、4 每月
    schedKind->AddString(botrule::ScheduleKindName(kScheduleDaily));
    schedKind->AddString(botrule::ScheduleKindName(kScheduleWorkdays));
    schedKind->AddString(botrule::ScheduleKindName(kScheduleWeekly));
    schedKind->AddString(botrule::ScheduleKindName(kScheduleMonthly));
    m_schedKind = schedKind.get();
    AddChild(std::move(schedKind));

    std::unique_ptr<DuiEdit> schedTime = MakeEdit(kIdBotRuleSchedTime, _T("09:00"), 0);
    m_schedTime = schedTime.get();
    AddChild(std::move(schedTime));

    std::unique_ptr<DuiEdit> schedDay = MakeEdit(kIdBotRuleSchedDay, _T("1"), 0);
    m_schedDay = schedDay.get();
    AddChild(std::move(schedDay));

    for (int i = 0; i < kDaysPerWeek; ++i)
    {
        std::unique_ptr<DuiButton> day = MakeCheckbox(static_cast<UINT>(kIdBotRuleWeekdayFirst + i), kWeekdayTexts[i]);
        m_weekdays[i] = day.get();
        AddChild(std::move(day));
    }

    std::unique_ptr<DuiListBox> schedGroups(new DuiListBox());
    schedGroups->SetCtrlId(kIdBotRuleSchedGroups);
    schedGroups->SetShowCheckboxes(true);
    m_schedGroups = schedGroups.get();
    AddChild(std::move(schedGroups));

    std::unique_ptr<DuiButton> windowOn = MakeCheckbox(kIdBotRuleWindowOn, _T("只在以下时间段响应"));
    m_windowOn = windowOn.get();
    AddChild(std::move(windowOn));

    std::unique_ptr<DuiComboBox> windowDays = MakeCombo(kIdBotRuleWindowDays);
    //下标即时间段日子取值：0 每天、1 工作日、2 周末
    windowDays->AddString(botrule::WindowDaysName(kWindowEveryDay));
    windowDays->AddString(botrule::WindowDaysName(kWindowWorkdays));
    windowDays->AddString(botrule::WindowDaysName(kWindowWeekend));
    m_windowDays = windowDays.get();
    AddChild(std::move(windowDays));

    std::unique_ptr<DuiEdit> windowFrom = MakeEdit(kIdBotRuleWindowFrom, kDefaultWindowFrom, 0);
    m_windowFrom = windowFrom.get();
    AddChild(std::move(windowFrom));

    std::unique_ptr<DuiEdit> windowTo = MakeEdit(kIdBotRuleWindowTo, kDefaultWindowTo, 0);
    m_windowTo = windowTo.get();
    AddChild(std::move(windowTo));

    // ---- 右栏：动作 ----
    std::unique_ptr<DuiComboBox> action = MakeCombo(kIdBotRuleAction);
    m_action = action.get();
    AddChild(std::move(action));

    std::unique_ptr<DuiEdit> text = MakeEdit(kIdBotRuleText, _T("例如：VPN 连不上时请先重启客户端（回复被 @ 或关键词的消息时会自动 @ 发送者）"),
                                             kBotReplyMaxChars);
    text->SetMultiLine(true);
    text->SetWordWrap(true);
    m_text = text.get();
    AddChild(std::move(text));

    std::unique_ptr<DuiButton> markdown = MakeCheckbox(kIdBotRuleMarkdown, _T("按 Markdown 显示"));
    m_markdown = markdown.get();
    AddChild(std::move(markdown));

    std::unique_ptr<DuiEdit> url = MakeEdit(kIdBotRuleUrl, _T("https://api.example.com/bot"), kBotUrlMaxBytes);
    m_url = url.get();
    AddChild(std::move(url));

    // ---- 标题栏与按钮栏 ----
    std::unique_ptr<DuiButton> closeBtn(new DuiButton());
    closeBtn->SetCtrlId(kIdBotRuleClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancelBtn = MakeButton(kIdBotRuleCancel, _T("取消"), DuiButton::Variant::Default);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    std::unique_ptr<DuiButton> saveBtn = MakeButton(kIdBotRuleSave, _T("确定"), DuiButton::Variant::Primary);
    m_btnSave = saveBtn.get();
    AddChild(std::move(saveBtn));

    ShowForRule(BotRule(), true, std::vector<BotGroup>());
}

void BotRuleModal::ShowForRule(const BotRule& rule, bool isNew, const std::vector<BotGroup>& groups)
{
    m_original = rule;
    m_isNew = isNew;
    const BotTrigger& t = rule.m_trigger;
    const BotAction& a = rule.m_action;

    if (m_name != nullptr)
        m_name->SetText(rule.m_name);
    if (m_enabled != nullptr)
        m_enabled->SetCheck(rule.m_enabled, false);

    int triggerIndex = 0;
    for (size_t i = 0; i < m_triggerTypes.size(); ++i)
    {
        if (m_triggerTypes[i] == t.m_type)
            triggerIndex = static_cast<int>(i);
    }
    SelectClamped(m_trigger, triggerIndex);

    if (m_keywords != nullptr)
        m_keywords->SetText(botrule::JoinKeywords(t.m_keywords));
    SelectClamped(m_match, t.m_match - kMatchContains);
    if (m_needAt != nullptr)
        m_needAt->SetCheck(t.m_needAt, false);

    SelectClamped(m_schedKind, t.m_schedule.m_kind - kScheduleDaily);
    if (m_schedTime != nullptr)
        m_schedTime->SetText(t.m_schedule.m_time);
    if (m_schedDay != nullptr)
    {
        CString day;
        day.Format(_T("%d"), t.m_schedule.m_monthDay);
        m_schedDay->SetText(day);
    }
    const int mask = botrule::WeekdayMask(t.m_schedule.m_weekdays);
    for (int i = 0; i < kDaysPerWeek; ++i)
    {
        if (m_weekdays[i] != nullptr)
            m_weekdays[i]->SetCheck((mask & (1 << i)) != 0, false);
    }
    m_groups = groups;
    FillGroups(std::set<long long>(t.m_groupIds.begin(), t.m_groupIds.end()));

    if (m_windowOn != nullptr)
        m_windowOn->SetCheck(t.m_window.m_on, false);
    SelectClamped(m_windowDays, t.m_window.m_days - kWindowEveryDay);
    if (m_windowFrom != nullptr)
        m_windowFrom->SetText(t.m_window.m_from.IsEmpty() ? CString(kDefaultWindowFrom) : t.m_window.m_from);
    if (m_windowTo != nullptr)
        m_windowTo->SetText(t.m_window.m_to.IsEmpty() ? CString(kDefaultWindowTo) : t.m_window.m_to);

    RefillActions(a.m_type);
    if (m_text != nullptr)
        m_text->SetText(a.m_text);
    if (m_markdown != nullptr)
        m_markdown->SetCheck(a.m_markdown, false);
    if (m_url != nullptr)
        m_url->SetText(a.m_url);

    m_error.Empty();
    ApplyVisibility();
}

void BotRuleModal::SetGroups(const std::vector<BotGroup>& groups)
{
    const std::vector<long long> checked = CheckedGroups();
    m_groups = groups;
    FillGroups(std::set<long long>(checked.begin(), checked.end()));
}

void BotRuleModal::SetDomains(const std::vector<CString>& domains, bool known)
{
    m_domains = domains;
    m_domainsKnown = known;
}

void BotRuleModal::FillGroups(const std::set<long long>& checked)
{
    if (m_schedGroups == nullptr)
        return;
    m_schedGroups->DeleteAllItems();
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        const int row = m_schedGroups->AddItem(m_groups[i].m_name, static_cast<LPARAM>(i));
        if (checked.count(m_groups[i].m_id) != 0)
            m_schedGroups->SetItemChecked(row, true, false);
    }
    m_schedGroups->Invalidate();
}

std::vector<long long> BotRuleModal::CheckedGroups() const
{
    std::vector<long long> checked;
    if (m_schedGroups == nullptr)
        return checked;
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_schedGroups->IsItemChecked(static_cast<int>(i)))
            checked.push_back(m_groups[i].m_id);
    }
    return checked;
}

int BotRuleModal::TriggerType() const
{
    const int index = (m_trigger != nullptr) ? m_trigger->GetCurSel() : -1;
    if (index < 0 || index >= static_cast<int>(m_triggerTypes.size()))
        return kTriggerAt;
    return m_triggerTypes[static_cast<size_t>(index)];
}

void BotRuleModal::SetTriggerType(int type)
{
    for (size_t i = 0; i < m_triggerTypes.size(); ++i)
    {
        if (m_triggerTypes[i] == type)
        {
            SelectClamped(m_trigger, static_cast<int>(i));
            OnTriggerChanged();
            return;
        }
    }
}

int BotRuleModal::ActionType() const
{
    if (TriggerType() == kTriggerWebhook)
        return 0;
    const int index = (m_action != nullptr) ? m_action->GetCurSel() : -1;
    if (index < 0 || index >= static_cast<int>(m_actionTypes.size()))
        return kActionReply;
    return m_actionTypes[static_cast<size_t>(index)];
}

void BotRuleModal::SetActionType(int type)
{
    for (size_t i = 0; i < m_actionTypes.size(); ++i)
    {
        if (m_actionTypes[i] == type)
        {
            SelectClamped(m_action, static_cast<int>(i));
            OnActionChanged();
            return;
        }
    }
}

void BotRuleModal::SetScheduleKind(int kind)
{
    SelectClamped(m_schedKind, kind - kScheduleDaily);
    OnScheduleKindChanged();
}

void BotRuleModal::RefillActions(int keepType)
{
    m_actionTypes = botrule::AllowedActions(TriggerType());
    if (m_action == nullptr)
        return;
    m_action->ResetContent();
    int selected = 0;
    for (size_t i = 0; i < m_actionTypes.size(); ++i)
    {
        m_action->AddString(botrule::ActionName(m_actionTypes[i]));
        if (m_actionTypes[i] == keepType)
            selected = static_cast<int>(i);
    }
    if (!m_actionTypes.empty())
        m_action->SetCurSel(selected, false);
}

void BotRuleModal::OnTriggerChanged()
{
    RefillActions(ActionType());
    ApplyVisibility();
}

void BotRuleModal::OnActionChanged()
{
    ApplyVisibility();
}

void BotRuleModal::OnScheduleKindChanged()
{
    ApplyVisibility();
}

void BotRuleModal::OnWindowToggled()
{
    ApplyVisibility();
}

void BotRuleModal::ApplyVisibility()
{
    const int trigger = TriggerType();
    const int action = ActionType();
    const int kind = (m_schedKind != nullptr) ? m_schedKind->GetCurSel() + kScheduleDaily : kScheduleDaily;
    const bool keyword = (trigger == kTriggerKeyword);
    const bool schedule = (trigger == kTriggerSchedule);
    const bool weekly = schedule && kind == kScheduleWeekly;
    const bool monthly = schedule && kind == kScheduleMonthly;
    const bool window = botrule::HasWindow(trigger);
    const bool hasAction = (trigger != kTriggerWebhook);

    ShowCtrl(m_keywords, keyword);
    ShowCtrl(m_match, keyword);
    ShowCtrl(m_needAt, keyword);
    ShowCtrl(m_schedKind, schedule);
    ShowCtrl(m_schedTime, schedule);
    ShowCtrl(m_schedDay, monthly);
    for (int i = 0; i < kDaysPerWeek; ++i)
        ShowCtrl(m_weekdays[i], weekly);
    ShowCtrl(m_schedGroups, schedule);
    ShowCtrl(m_windowOn, window);
    ShowCtrl(m_windowDays, window);
    ShowCtrl(m_windowFrom, window);
    ShowCtrl(m_windowTo, window);
    ShowCtrl(m_action, hasAction);
    ShowCtrl(m_text, hasAction);
    ShowCtrl(m_markdown, hasAction && action == kActionReply);
    ShowCtrl(m_url, hasAction && action == kActionExternal);

    //时间段的三个控件只在勾选「只在以下时间段响应」时可用
    const bool windowOn = (m_windowOn != nullptr && m_windowOn->IsChecked());
    if (m_windowDays != nullptr)
        m_windowDays->SetEnabled(windowOn);
    if (m_windowFrom != nullptr)
        m_windowFrom->SetEnabled(windowOn);
    if (m_windowTo != nullptr)
        m_windowTo->SetEnabled(windowOn);

    //SetVisible 不会重新排版：对本窗口（这些控件的父容器）按当前矩形重新排一次（开发约定文档 UI 约定第 5 条）
    if (m_rcItem.right > m_rcItem.left)
        ForceLayout(m_rcItem);
    Invalidate();
}

BotRule BotRuleModal::GetRule() const
{
    BotRule r = m_original;
    r.m_name = (m_name != nullptr) ? m_name->GetText() : CString();
    r.m_name.Trim();
    r.m_enabled = (m_enabled != nullptr && m_enabled->IsChecked());

    BotTrigger& t = r.m_trigger;
    t.m_type = TriggerType();
    t.m_keywords = (t.m_type == kTriggerKeyword && m_keywords != nullptr) ? botrule::SplitKeywords(m_keywords->GetText())
                                                                           : std::vector<CString>();
    t.m_match = ((m_match != nullptr) ? m_match->GetCurSel() : 0) + kMatchContains;
    t.m_needAt = (m_needAt != nullptr && m_needAt->IsChecked());

    if (t.m_type == kTriggerSchedule)
    {
        t.m_schedule.m_kind = ((m_schedKind != nullptr) ? m_schedKind->GetCurSel() : 0) + kScheduleDaily;
        t.m_schedule.m_time = TimeFromEdit(m_schedTime);
        std::vector<int> days;
        for (int i = 0; i < kDaysPerWeek; ++i)
        {
            if (m_weekdays[i] != nullptr && m_weekdays[i]->IsChecked())
                days.push_back(i + 1);
        }
        t.m_schedule.m_weekdays = (t.m_schedule.m_kind == kScheduleWeekly) ? days : std::vector<int>();
        //几号：只接受纯数字，其余按 0 处理，由校验报错
        CString dayText = (m_schedDay != nullptr) ? m_schedDay->GetText() : CString();
        dayText.Trim();
        const bool digits = !dayText.IsEmpty() && dayText.SpanIncluding(_T("0123456789")) == dayText;
        const int kDayDigitsMax = 2;
        t.m_schedule.m_monthDay = (digits && dayText.GetLength() <= kDayDigitsMax) ? _ttoi(dayText) : 0;
        if (t.m_schedule.m_kind != kScheduleMonthly && t.m_schedule.m_monthDay <= 0)
            t.m_schedule.m_monthDay = 1;
        t.m_groupIds = CheckedGroups();
    }
    else
    {
        t.m_groupIds.clear();
    }

    if (botrule::HasWindow(t.m_type))
    {
        t.m_window.m_on = (m_windowOn != nullptr && m_windowOn->IsChecked());
        t.m_window.m_days = ((m_windowDays != nullptr) ? m_windowDays->GetCurSel() : 0) + kWindowEveryDay;
        t.m_window.m_from = TimeFromEdit(m_windowFrom);
        t.m_window.m_to = TimeFromEdit(m_windowTo);
    }
    else
    {
        t.m_window.m_on = false;
    }

    if (t.m_type != kTriggerWebhook)
    {
        BotAction& a = r.m_action;
        a.m_type = ActionType();
        a.m_text = (m_text != nullptr) ? m_text->GetText() : CString();
        a.m_text.Trim();
        a.m_markdown = (a.m_type == kActionReply && m_markdown != nullptr && m_markdown->IsChecked());
        a.m_url = (a.m_type == kActionExternal && m_url != nullptr) ? m_url->GetText() : CString();
        a.m_url.Trim();
    }
    return r;
}

bool BotRuleModal::Validate(CString* error) const
{
    return botrule::ValidateRule(GetRule(), m_domainsKnown ? &m_domains : nullptr, error);
}

void BotRuleModal::SetError(const CString& text)
{
    if (m_error == text)
        return;
    m_error = text;
    Invalidate();
}

void BotRuleModal::AddLabel(const RECT& rc, const CString& text, COLORREF color, bool wrap)
{
    LabelSpot spot;
    spot.m_rc = rc;
    spot.m_text = text;
    spot.m_color = color;
    spot.m_wrap = wrap;
    m_labels.push_back(spot);
}

void BotRuleModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    m_labels.clear();

    const int availH = (rcAvail.bottom - rcAvail.top) - 2 * kModalMargin;
    int panelH = (availH < kPanelH) ? availH : kPanelH;
    if (panelH < kMinPanelH)
        panelH = kMinPanelH;
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect = RECT{ cx - kPanelW / 2, cy - panelH / 2, cx + kPanelW / 2, cy - panelH / 2 + panelH };

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;
    const int leftR = L + kLeftColW;
    const int rightL = leftR + kColGap;
    const int top = m_panelRect.top + kHeaderH + kPadV;
    const int bottom = m_panelRect.bottom - kFooterH - kPadV;
    const int trigger = TriggerType();
    const int action = ActionType();
    const COLORREF labelColor = theme::kInk400;

    PlaceCtrl(m_btnClose, RECT{ R - kCloseBtnSize, m_panelRect.top + (kHeaderH - kCloseBtnSize) / 2,
                                R, m_panelRect.top + (kHeaderH + kCloseBtnSize) / 2 });

    // ---- 左栏 ----
    int y = top;
    CString nameLabel;
    nameLabel.Format(_T("规则名称（可留空，最多 %d 个字）"), kBotRuleNameMaxChars);
    AddLabel(RECT{ L, y, leftR, y + kLabelH }, nameLabel, labelColor, false);
    y += kLabelH;
    PlaceCtrl(m_name, RECT{ L, y, leftR - kEnabledW - kInlineGap, y + kEditH });
    PlaceCtrl(m_enabled, RECT{ leftR - kEnabledW, y, leftR, y + kEditH });
    y += kEditH + kFieldGap;

    AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("触发方式"), labelColor, false);
    y += kLabelH;
    PlaceCtrl(m_trigger, RECT{ L, y, L + kTriggerComboW, y + kEditH });
    y += kEditH + kFieldGap;

    if (trigger == kTriggerKeyword)
    {
        CString kwLabel;
        kwLabel.Format(_T("关键词（逗号分隔，最多 %d 个，每个最多 %d 个字）"), kBotKeywordMax, kBotKeywordMaxChars);
        AddLabel(RECT{ L, y, leftR, y + kLabelH }, kwLabel, labelColor, false);
        y += kLabelH;
        PlaceCtrl(m_keywords, RECT{ L, y, leftR, y + kEditH });
        y += kEditH + kFieldGap;
        AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("匹配方式（比较前去掉首尾空白、不分大小写与全半角）"), labelColor, false);
        y += kLabelH;
        PlaceCtrl(m_match, RECT{ L, y, L + kMatchComboW, y + kEditH });
        PlaceCtrl(m_needAt, RECT{ L + kMatchComboW + kInlineGap * 2, y, leftR, y + kEditH });
        y += kEditH + kFieldGap;
    }
    else if (trigger == kTriggerSchedule)
    {
        const bool monthly = (m_schedKind != nullptr && m_schedKind->GetCurSel() + kScheduleDaily == kScheduleMonthly);
        const bool weekly = (m_schedKind != nullptr && m_schedKind->GetCurSel() + kScheduleDaily == kScheduleWeekly);
        AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("定时（服务器所在时区；时刻如 09:00）"), labelColor, false);
        y += kLabelH;
        int x = L;
        PlaceCtrl(m_schedKind, RECT{ x, y, x + kSchedKindW, y + kEditH });
        x += kSchedKindW + kInlineGap;
        if (monthly)
        {
            PlaceCtrl(m_schedDay, RECT{ x, y, x + kDayEditW, y + kEditH });
            x += kDayEditW;
            AddLabel(RECT{ x, y, x + kUnitLabelW, y + kEditH }, _T(" 号"), theme::kInk600, false);
            x += kUnitLabelW + kInlineGap;
        }
        PlaceCtrl(m_schedTime, RECT{ x, y, x + kTimeEditW, y + kEditH });
        y += kEditH + kInlineGap;
        if (weekly)
        {
            for (int i = 0; i < kDaysPerWeek; ++i)
                PlaceCtrl(m_weekdays[i], RECT{ L + i * kWeekdayW, y, L + (i + 1) * kWeekdayW, y + kCheckH });
            y += kCheckH + kInlineGap;
        }
        y += kFieldGap - kInlineGap;
        AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("发往的群（都不勾表示应用范围内的全部群）"), labelColor, false);
        y += kLabelH;
        const int listBottom = (bottom > y + kMinGroupListH) ? bottom : y + kMinGroupListH;
        PlaceCtrl(m_schedGroups, RECT{ L, y, leftR, listBottom });
        y = listBottom + kFieldGap;
    }

    if (botrule::HasWindow(trigger))
    {
        PlaceCtrl(m_windowOn, RECT{ L, y, leftR, y + kCheckH });
        y += kCheckH + kInlineGap;
        int x = L;
        PlaceCtrl(m_windowDays, RECT{ x, y, x + kWindowDaysW, y + kEditH });
        x += kWindowDaysW + kInlineGap;
        PlaceCtrl(m_windowFrom, RECT{ x, y, x + kTimeEditW, y + kEditH });
        x += kTimeEditW;
        AddLabel(RECT{ x, y, x + kUnitLabelW, y + kEditH }, _T(" 至"), theme::kInk600, false);
        x += kUnitLabelW + kInlineGap;
        PlaceCtrl(m_windowTo, RECT{ x, y, x + kTimeEditW, y + kEditH });
        y += kEditH + kInlineGap;
        AddLabel(RECT{ L, y, leftR, y + kHintH }, _T("结束早于开始表示跨零点，两者相同表示全天"), labelColor, false);
    }

    // ---- 右栏 ----
    y = top;
    if (trigger == kTriggerWebhook)
    {
        AddLabel(RECT{ rightL, y, R, y + kNoteH },
                 _T("外部系统推送的规则没有动作：外部系统向机器人的 Webhook 地址推送消息（地址、令牌与签名密钥在机器人")
                 _T("编辑窗里查看），机器人把推来的内容原样发到群里。机器人应用到多个群时，推送须指定群；")
                 _T("每个机器人每分钟最多 20 条，内容会经过企业敏感词检查。"),
                 theme::kInk500, true);
    }
    else
    {
        AddLabel(RECT{ rightL, y, R, y + kLabelH }, _T("动作"), labelColor, false);
        y += kLabelH;
        PlaceCtrl(m_action, RECT{ rightL, y, rightL + kActionComboW, y + kEditH });
        y += kEditH + kFieldGap;

        CString textLabel;
        if (action == kActionQa)
            textLabel = _T("兜底文字（问答里找不到答案时回复，可留空）");
        else if (action == kActionExternal)
            textLabel = _T("兜底文字（外部服务连不上、超时或出错时回复，可留空）");
        else
            textLabel.Format(_T("回复文字（最多 %d 个字）"), kBotReplyMaxChars);
        AddLabel(RECT{ rightL, y, R, y + kLabelH }, textLabel, labelColor, false);
        y += kLabelH;

        //回复文字输入框占满剩余高度，给下面的说明、勾选框与外部服务地址留出位置
        int reserved = kInlineGap + kHintH;
        if (action == kActionReply)
            reserved += kInlineGap + kCheckH;
        if (action == kActionExternal)
            reserved += kFieldGap + kLabelH + kEditH;
        int textBottom = bottom - reserved;
        if (textBottom < y + kMinTextH)
            textBottom = y + kMinTextH;
        PlaceCtrl(m_text, RECT{ rightL, y, R, textBottom });
        y = textBottom + kInlineGap;
        AddLabel(RECT{ rightL, y, R, y + kHintH }, _T("可用变量：{发送者}、{日期}、{新成员}（新成员入群时）"), labelColor, false);
        y += kHintH;
        if (action == kActionReply)
        {
            y += kInlineGap;
            PlaceCtrl(m_markdown, RECT{ rightL, y, R, y + kCheckH });
            y += kCheckH;
        }
        if (action == kActionExternal)
        {
            y += kFieldGap;
            AddLabel(RECT{ rightL, y, R, y + kLabelH }, _T("外部服务地址（http 或 https，主机须在域名白名单内）"), labelColor,
                     false);
            y += kLabelH;
            PlaceCtrl(m_url, RECT{ rightL, y, R, y + kEditH });
        }
    }

    // ---- 按钮栏：自右向左依次是 确定 / 取消，左侧是错误提示 ----
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    const int saveLeft = R - kBtnWSave;
    const int cancelLeft = saveLeft - kBtnGap - kBtnWCancel;
    PlaceCtrl(m_btnSave, RECT{ saveLeft, by, R, by + kBtnH });
    PlaceCtrl(m_btnCancel, RECT{ cancelLeft, by, cancelLeft + kBtnWCancel, by + kBtnH });
    m_errorRect = RECT{ L, by, cancelLeft - kBtnGap, by + kBtnH };
}

void BotRuleModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;
    const UINT lineFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

    ui::DrawTextLine(hdc, m_isNew ? _T("添加规则") : _T("编辑规则"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    //字段标题与说明：位置在 Layout 里算好
    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);
    for (size_t i = 0; i < m_labels.size(); ++i)
    {
        const LabelSpot& spot = m_labels[i];
        ui::DrawTextLine(hdc, spot.m_text, spot.m_rc, spot.m_color,
                         spot.m_wrap ? ui::UiFont(theme::kFontSizeNormal, false) : labelFont,
                         spot.m_wrap ? (DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX) : lineFlags);
    }

    //按钮栏左侧：有错误时显示红字，没有时提示规则要在机器人编辑窗保存后才生效
    if (!m_error.IsEmpty())
        ui::DrawTextLine(hdc, m_error, m_errorRect, theme::kColorDanger, ui::UiFont(theme::kFontSizeNormal, false), lineFlags);
    else
        ui::DrawTextLine(hdc, _T("点「确定」后回到机器人编辑窗，在那里点「保存」才会生效"), m_errorRect, theme::kInk400,
                         labelFont, lineFlags);

    //子控件画在最上层
    DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
