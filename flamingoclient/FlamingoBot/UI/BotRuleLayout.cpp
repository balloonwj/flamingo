/**
 *  规则编辑窗的控件树，见 BotRuleLayout.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotRuleLayout.h"

#include "../Core/BotRules.h"
#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace flamingobot {

namespace {

typedef DuiLayout::Hint Hint;

//---- 尺寸，单位：像素 ----
const int kKeywordsHeight = 60;    //关键词输入框（多行）高
const int kReplyHeight = 96;       //回复文字输入框（多行）高
const int kFallbackHeight = 60;    //兜底文字输入框（多行）高
const int kShortEditWidth = 80;    //时刻、几号、分钟数输入框宽
const int kDaysComboWidth = 110;   //时间段「每天 / 工作日 / 周末」下拉框宽
const int kSenderComboWidth = 150; //发送者范围下拉框宽
const int kCheckRowHeight = 24;    //复选框一行高
const int kCheckGap = 12;          //一行里多个复选框之间的距离

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kLabelName = TR_NOOP(L"规则名称");
const wchar_t* const kLabelTrigger = TR_NOOP(L"触发方式");
const wchar_t* const kLabelKeywords = TR_NOOP(L"关键词");
const wchar_t* const kLabelMatch = TR_NOOP(L"匹配方式");
const wchar_t* const kLabelSchedule = TR_NOOP(L"定时");
const wchar_t* const kLabelTime = TR_NOOP(L"时刻");
const wchar_t* const kLabelWeekdays = TR_NOOP(L"星期");
const wchar_t* const kLabelMonthDay = TR_NOOP(L"几号");
const wchar_t* const kLabelPresence = TR_NOOP(L"你的在线状态");
const wchar_t* const kLabelSenders = TR_NOOP(L"发送者");
const wchar_t* const kLabelCooldown = TR_NOOP(L"冷却（分钟）");
const wchar_t* const kLabelOwnerQuiet = TR_NOOP(L"正在交谈（分钟）");
const wchar_t* const kLabelWindow = TR_NOOP(L"时间段");
const wchar_t* const kLabelAction = TR_NOOP(L"动作");
const wchar_t* const kLabelReply = TR_NOOP(L"回复文字");
const wchar_t* const kLabelFallback = TR_NOOP(L"兜底文字");
const wchar_t* const kLabelUrl = TR_NOOP(L"服务地址");

//在内容区里加一行固定高度的控件
void AddRow(DuiVBox* content, std::unique_ptr<DuiControl> row, int height)
{
    content->AddChild(std::move(row), Hint().Fixed(height));
}

//加一行「说明文字 + 控件」
void AddField(DuiVBox* content, const std::wstring& label, HFONT font, int labelWidth,
              std::unique_ptr<DuiControl> field, int height)
{
    std::unique_ptr<DuiHBox> row = MakeFieldRow(label, font, labelWidth, std::move(field));
    AddRow(content, std::unique_ptr<DuiControl>(row.release()), height);
}

//加一段灰色的提示：与右侧控件左对齐，按宽度自动换行
void AddHint(DuiVBox* content, const std::wstring& text, HFONT font, int indent, int contentWidth)
{
    const int width = contentWidth - indent;
    const int height = MeasureWrappedHeight(font, text, width);
    std::unique_ptr<DuiLabel> label = MakeLabel(text, kBotFaint, font, DT_LEFT | DT_TOP | DT_WORDBREAK);
    content->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(height).Margin(indent, 0, 0, 0));
}

//建一行「固定宽度的输入框 + 后面一段说明」，返回输入框
std::unique_ptr<DuiHBox> MakeShortEditRow(std::unique_ptr<DuiEdit> edit, const std::wstring& trailing, HFONT font,
                                          DuiEdit** outEdit)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kBotRowGap);
    *outEdit = edit.get();
    row->AddChild(std::unique_ptr<DuiControl>(edit.release()), Hint().Fixed(kShortEditWidth));
    row->AddChild(std::unique_ptr<DuiControl>(MakeLabel(trailing, kBotFaint, font,
                                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                  Hint().Weight(1));
    return row;
}

//一组取值里某个值的下标；不在里面时为 0
int IndexOf(const std::vector<int>& values, int value)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] == value)
            return static_cast<int>(i);
    }
    return 0;
}

//下拉框当前选中项对应的取值；没有选中时为 def
int ComboValue(const DuiComboBox* combo, const std::vector<int>& values, int def)
{
    if (combo == NULL)
        return def;
    const int sel = combo->GetCurSel();
    if (sel < 0 || sel >= static_cast<int>(values.size()))
        return def;
    return values[sel];
}

//触发方式的说明（被 @、入群、外部系统推送没有别的字段，用一句话说明）
std::wstring TriggerHint(int trigger)
{
    switch (trigger)
    {
    //被 @
    case bot_trigger_at:
        return TR(L"有人在群里 @ 这个机器人时触发；@所有人不算。");
    //关键词
    case bot_trigger_keyword:
        return TR(L"群消息命中关键词时触发；比较前去掉首尾空白，不区分大小写与全角半角。");
    //新成员入群
    case bot_trigger_join:
        return TR(L"有新成员入群时触发，常用来发欢迎消息。");
    //定时
    case bot_trigger_schedule:
        return TR(L"到点时在群里发一条消息；服务停机期间错过的不补发。");
    //收到单聊
    case bot_trigger_single:
        return TR(L"别人给你发单聊消息、且满足下面的条件时，以你的身份代答，气泡上会标明「机器人代答」。");
    //外部系统推送
    case bot_trigger_webhook:
        return TR(L"外部系统经 Webhook 地址推送内容时，由机器人发到群里；这种规则不需要设置动作。地址与令牌在机器人编辑窗里查看。");
    //不认识的取值
    default:
        return std::wstring();
    }
}

} // namespace

RuleFormControls::RuleFormControls()
    : m_name(NULL)
    , m_enabled(NULL)
    , m_trigger(NULL)
    , m_keywords(NULL)
    , m_match(NULL)
    , m_needAt(NULL)
    , m_scheduleKind(NULL)
    , m_scheduleTime(NULL)
    , m_monthDay(NULL)
    , m_senderKind(NULL)
    , m_pickSenders(NULL)
    , m_cooldown(NULL)
    , m_ownerQuiet(NULL)
    , m_windowOn(NULL)
    , m_windowDays(NULL)
    , m_windowFrom(NULL)
    , m_windowTo(NULL)
    , m_action(NULL)
    , m_text(NULL)
    , m_markdown(NULL)
    , m_url(NULL)
{
    for (int i = 0; i < kDaysPerWeek; ++i)
        m_weekdays[i] = NULL;
    for (int i = 0; i < kPresenceKinds; ++i)
        m_presence[i] = NULL;
}

void BuildRuleForm(DuiVBox* content, const BotRuleForm& form, const RuleFormContext& ctx, HFONT font, int contentWidth,
                   RuleFormControls& out)
{
    out = RuleFormControls();
    if (content == NULL)
        return;
    const bool personal = (ctx.m_ownerType == bot_owner_personal);
    std::vector<std::wstring> labels;
    labels.push_back(TR(kLabelName));
    labels.push_back(TR(kLabelTrigger));
    labels.push_back(TR(kLabelKeywords));
    labels.push_back(TR(kLabelMatch));
    labels.push_back(TR(kLabelSchedule));
    labels.push_back(TR(kLabelTime));
    labels.push_back(TR(kLabelWeekdays));
    labels.push_back(TR(kLabelMonthDay));
    labels.push_back(TR(kLabelPresence));
    labels.push_back(TR(kLabelSenders));
    labels.push_back(TR(kLabelCooldown));
    labels.push_back(TR(kLabelOwnerQuiet));
    labels.push_back(TR(kLabelWindow));
    labels.push_back(TR(kLabelAction));
    labels.push_back(TR(kLabelReply));
    labels.push_back(TR(kLabelFallback));
    labels.push_back(TR(kLabelUrl));
    const int labelWidth = FieldLabelWidth(font, labels);
    const int indent = labelWidth + kBotRowGap;
    int width = 0;

    //规则名称与启用
    std::unique_ptr<DuiEdit> name = MakeEdit(kIdRuleName, TRF(L"可不填，最多 {0} 字", kBotRuleNameMaxChars), form.m_name,
                                             font, false, kBotRuleNameMaxChars);
    out.m_name = name.get();
    AddField(content, TR(kLabelName), font, labelWidth, std::unique_ptr<DuiControl>(name.release()), kBotEditHeight);
    std::unique_ptr<DuiButton> enabled = MakeCheckBox(TR(L"启用这条规则"), kIdRuleEnabled, form.m_enabled, font, width);
    out.m_enabled = enabled.get();
    content->AddChild(std::unique_ptr<DuiControl>(enabled.release()),
                      Hint().Fixed(kCheckRowHeight).Margin(indent, 0, 0, 0));

    //触发方式
    out.m_triggerValues = AllowedTriggers(ctx.m_ownerType, ctx.m_allowExternal);
    std::vector<std::wstring> triggerNames;
    for (size_t i = 0; i < out.m_triggerValues.size(); ++i)
        triggerNames.push_back(TriggerName(out.m_triggerValues[i]));
    std::unique_ptr<DuiComboBox> trigger = MakeCombo(kIdRuleTrigger, triggerNames,
                                                     IndexOf(out.m_triggerValues, form.m_trigger));
    out.m_trigger = trigger.get();
    AddField(content, TR(kLabelTrigger), font, labelWidth, std::unique_ptr<DuiControl>(trigger.release()), kBotEditHeight);
    AddHint(content, TriggerHint(form.m_trigger), font, indent, contentWidth);

    //关键词（关键词规则必填；收到单聊可不填）
    if (form.m_trigger == bot_trigger_keyword || form.m_trigger == bot_trigger_single)
    {
        const std::wstring placeholder = (form.m_trigger == bot_trigger_single)
                                             ? TR(L"可不填：不填表示任何消息都代答。一行一个，或用逗号分隔")
                                             : TR(L"一行一个，或用逗号分隔");
        std::unique_ptr<DuiEdit> keywords = MakeEdit(kIdRuleKeywords, placeholder, form.m_keywords, font, true, 0);
        out.m_keywords = keywords.get();
        AddField(content, TR(kLabelKeywords), font, labelWidth, std::unique_ptr<DuiControl>(keywords.release()),
                 kKeywordsHeight);
        AddHint(content, TRF(L"最多 {0} 个关键词，每个最多 {1} 字。", kBotKeywordMax, kBotKeywordMaxChars), font, indent,
                contentWidth);
        std::vector<std::wstring> matches;
        matches.push_back(MatchName(bot_match_contains));
        matches.push_back(MatchName(bot_match_exact));
        matches.push_back(MatchName(bot_match_prefix));
        std::unique_ptr<DuiComboBox> match = MakeCombo(kIdRuleMatch, matches, form.m_match - bot_match_contains);
        out.m_match = match.get();
        AddField(content, TR(kLabelMatch), font, labelWidth, std::unique_ptr<DuiControl>(match.release()), kBotEditHeight);
        if (form.m_trigger == bot_trigger_keyword && !personal)
        {
            std::unique_ptr<DuiButton> needAt = MakeCheckBox(TR(L"必须同时 @ 机器人（避免机器人在群聊中频繁插话）"),
                                                             kIdRuleNeedAt, form.m_needAt, font, width);
            out.m_needAt = needAt.get();
            content->AddChild(std::unique_ptr<DuiControl>(needAt.release()),
                              Hint().Fixed(kCheckRowHeight).Margin(indent, 0, 0, 0));
        }
    }

    //定时：种类、时刻，每周选星期、每月填几号
    if (form.m_trigger == bot_trigger_schedule)
    {
        std::vector<std::wstring> kinds;
        for (int k = bot_schedule_daily; k <= bot_schedule_monthly; ++k)
            kinds.push_back(ScheduleKindName(k));
        std::unique_ptr<DuiComboBox> kind = MakeCombo(kIdRuleScheduleKind, kinds, form.m_scheduleKind - bot_schedule_daily);
        out.m_scheduleKind = kind.get();
        AddField(content, TR(kLabelSchedule), font, labelWidth, std::unique_ptr<DuiControl>(kind.release()), kBotEditHeight);
        std::unique_ptr<DuiHBox> timeRow = MakeShortEditRow(
            MakeEdit(kIdRuleScheduleTime, L"09:00", form.m_scheduleTime, font, false, 0), TR(L"24 小时制，如 09:00"), font,
            &out.m_scheduleTime);
        AddField(content, TR(kLabelTime), font, labelWidth, std::unique_ptr<DuiControl>(timeRow.release()), kBotEditHeight);
        if (form.m_scheduleKind == bot_schedule_weekly)
        {
            std::unique_ptr<DuiHBox> days(new DuiHBox());
            days->SetGap(kCheckGap);
            for (int d = 0; d < kDaysPerWeek; ++d)
            {
                std::unique_ptr<DuiButton> box = MakeCheckBox(WeekdayName(d + 1), kIdRuleWeekdayFirst + d,
                                                              form.m_weekdays[d], font, width);
                out.m_weekdays[d] = box.get();
                days->AddChild(std::unique_ptr<DuiControl>(box.release()), Hint().Fixed(width));
            }
            AddField(content, TR(kLabelWeekdays), font, labelWidth, std::unique_ptr<DuiControl>(days.release()),
                     kCheckRowHeight);
        }
        else if (form.m_scheduleKind == bot_schedule_monthly)
        {
            std::unique_ptr<DuiHBox> dayRow = MakeShortEditRow(
                MakeEdit(kIdRuleMonthDay, L"1", form.m_monthDay, font, false, 0),
                TRF(L"1～{0}，该月没有这一天时不发", kMaxMonthDay), font, &out.m_monthDay);
            AddField(content, TR(kLabelMonthDay), font, labelWidth, std::unique_ptr<DuiControl>(dayRow.release()),
                     kBotEditHeight);
        }
    }

    //收到单聊的附加条件
    if (form.m_trigger == bot_trigger_single)
    {
        std::unique_ptr<DuiHBox> states(new DuiHBox());
        states->SetGap(kCheckGap);
        //按「在线、离开、忙碌、离线」的顺序显示（与客户端状态菜单一致）；控件 id 仍按状态取值，读回时不受顺序影响
        const int kPresenceDisplayOrder[kPresenceKinds] = { bot_presence_online, bot_presence_away, bot_presence_busy,
                                                            bot_presence_offline };
        for (int k = 0; k < kPresenceKinds; ++k)
        {
            const int p = kPresenceDisplayOrder[k];
            std::unique_ptr<DuiButton> box = MakeCheckBox(PresenceName(p), kIdRulePresenceFirst + p, form.m_presence[p],
                                                          font, width);
            out.m_presence[p] = box.get();
            states->AddChild(std::unique_ptr<DuiControl>(box.release()), Hint().Fixed(width));
        }
        AddField(content, TR(kLabelPresence), font, labelWidth, std::unique_ptr<DuiControl>(states.release()),
                 kCheckRowHeight);
        AddHint(content, TR(L"你处于所选的状态之一时才代答；都不选表示不限。"), font, indent, contentWidth);

        std::vector<std::wstring> senderNames;
        out.m_senderValues.push_back(bot_sender_all);
        senderNames.push_back(SenderKindName(bot_sender_all));
        out.m_senderValues.push_back(bot_sender_users);
        senderNames.push_back(SenderKindName(bot_sender_users));
        out.m_senderValues.push_back(bot_sender_depts);
        senderNames.push_back(SenderKindName(bot_sender_depts));
        std::unique_ptr<DuiHBox> senderRow(new DuiHBox());
        senderRow->SetGap(kBotRowGap);
        std::unique_ptr<DuiComboBox> senderKind = MakeCombo(kIdRuleSenderKind, senderNames,
                                                            IndexOf(out.m_senderValues, form.m_senderKind));
        out.m_senderKind = senderKind.get();
        senderRow->AddChild(std::unique_ptr<DuiControl>(senderKind.release()), Hint().Fixed(kSenderComboWidth));
        //指定的人、指定的部门：右边一个「选择…」，分别弹出宿主的选择联系人、选择部门对话框
        if (form.m_senderKind == bot_sender_users || form.m_senderKind == bot_sender_depts)
        {
            std::unique_ptr<DuiButton> pick = MakeButton(TR(L"选择…"), kIdRulePickSenders, DuiButton::Variant::Default,
                                                         font, width);
            out.m_pickSenders = pick.get();
            senderRow->AddChild(std::unique_ptr<DuiControl>(pick.release()), Hint().Fixed(width));
        }
        senderRow->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        AddField(content, TR(kLabelSenders), font, labelWidth, std::unique_ptr<DuiControl>(senderRow.release()),
                 kBotEditHeight);
        if (form.m_senderKind == bot_sender_users)
        {
            const std::wstring names = ctx.m_senderNames.empty() ? std::wstring(TR(L"还没有选择，点「选择…」从通讯录里选。"))
                                                                  : TRF(L"已选：{0}", ctx.m_senderNames);
            AddHint(content, names, font, indent, contentWidth);
        }
        else if (form.m_senderKind == bot_sender_depts)
        {
            const std::wstring names = ctx.m_senderNames.empty()
                                           ? std::wstring(TR(L"还没有选择，点「选择…」从组织架构里选部门。"))
                                           : TRF(L"已选：{0}（含下级部门）", ctx.m_senderNames);
            AddHint(content, names, font, indent, contentWidth);
        }

        std::unique_ptr<DuiHBox> cooldownRow = MakeShortEditRow(
            MakeEdit(kIdRuleCooldown, L"30", form.m_cooldown, font, false, 0),
            //不写上限：原来带「最多 N」时一行放不下被截断（2026-10-08 实机核对），超出上限的值保存时由 ValidateRule 提示
            TR(L"同一个人在这段时间内只代答一次；0 表示不限"), font, &out.m_cooldown);
        AddField(content, TR(kLabelCooldown), font, labelWidth, std::unique_ptr<DuiControl>(cooldownRow.release()),
                 kBotEditHeight);
        std::unique_ptr<DuiHBox> quietRow = MakeShortEditRow(
            MakeEdit(kIdRuleOwnerQuiet, L"5", form.m_ownerQuiet, font, false, 0),
            TR(L"你最近在这个会话里发过言就不代答；0 表示不检查"), font, &out.m_ownerQuiet);
        AddField(content, TR(kLabelOwnerQuiet), font, labelWidth, std::unique_ptr<DuiControl>(quietRow.release()),
                 kBotEditHeight);
    }

    //时间段
    if (TriggerUsesWindow(form.m_trigger))
    {
        std::unique_ptr<DuiButton> windowOn = MakeCheckBox(TR(L"只在以下时间段生效"), kIdRuleWindowOn, form.m_windowOn,
                                                           font, width);
        out.m_windowOn = windowOn.get();
        content->AddChild(std::unique_ptr<DuiControl>(windowOn.release()),
                          Hint().Fixed(kCheckRowHeight).Margin(indent, 0, 0, 0));
        if (form.m_windowOn)
        {
            std::unique_ptr<DuiHBox> windowRow(new DuiHBox());
            windowRow->SetGap(kBotRowGap);
            std::vector<std::wstring> days;
            for (int d = bot_days_every; d <= bot_days_weekend; ++d)
                days.push_back(DaysKindName(d));
            std::unique_ptr<DuiComboBox> daysCombo = MakeCombo(kIdRuleWindowDays, days, form.m_windowDays);
            out.m_windowDays = daysCombo.get();
            windowRow->AddChild(std::unique_ptr<DuiControl>(daysCombo.release()), Hint().Fixed(kDaysComboWidth));
            std::unique_ptr<DuiEdit> from = MakeEdit(kIdRuleWindowFrom, L"18:00", form.m_windowFrom, font, false, 0);
            out.m_windowFrom = from.get();
            windowRow->AddChild(std::unique_ptr<DuiControl>(from.release()), Hint().Fixed(kShortEditWidth));
            const std::wstring toText = TR(L"至");
            windowRow->AddChild(std::unique_ptr<DuiControl>(MakeLabel(toText, kBotSubText, font,
                                                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                                Hint().Fixed(MeasureTextWidth(font, toText)));
            std::unique_ptr<DuiEdit> to = MakeEdit(kIdRuleWindowTo, L"09:00", form.m_windowTo, font, false, 0);
            out.m_windowTo = to.get();
            windowRow->AddChild(std::unique_ptr<DuiControl>(to.release()), Hint().Fixed(kShortEditWidth));
            windowRow->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
            AddField(content, TR(kLabelWindow), font, labelWidth, std::unique_ptr<DuiControl>(windowRow.release()),
                     kBotEditHeight);
            AddHint(content, TR(L"按服务器的本地时间计算；结束早于开始表示跨零点（如 18:00 至 09:00），两者相同表示全天。"),
                    font, indent, contentWidth);
        }
    }

    //动作
    if (!TriggerHasAction(form.m_trigger))
        return;
    out.m_actionValues = AllowedActions(ctx.m_ownerType, form.m_trigger, ctx.m_allowExternal);
    std::vector<std::wstring> actionNames;
    for (size_t i = 0; i < out.m_actionValues.size(); ++i)
        actionNames.push_back(ActionName(out.m_actionValues[i]));
    std::unique_ptr<DuiComboBox> action = MakeCombo(kIdRuleAction, actionNames, IndexOf(out.m_actionValues, form.m_action));
    out.m_action = action.get();
    AddField(content, TR(kLabelAction), font, labelWidth, std::unique_ptr<DuiControl>(action.release()), kBotEditHeight);
    switch (form.m_action)
    {
    //用问答作答：说明 + 兜底文字
    case bot_action_qa:
    {
        AddHint(content, TR(L"用本群的群问答与企业问答作答；没有找到答案时回复下面的兜底文字，不填则不回复。"), font, indent,
                contentWidth);
        std::unique_ptr<DuiEdit> fallback = MakeEdit(kIdRuleText, TR(L"可不填"), form.m_text, font, true, kBotReplyMaxChars);
        out.m_text = fallback.get();
        AddField(content, TR(kLabelFallback), font, labelWidth, std::unique_ptr<DuiControl>(fallback.release()),
                 kFallbackHeight);
        break;
    }
    //呼叫外部服务：地址 + 说明 + 兜底文字
    case bot_action_external:
    {
        std::unique_ptr<DuiEdit> url = MakeEdit(kIdRuleUrl, L"https://", form.m_url, font, false, kBotUrlMaxBytes);
        out.m_url = url.get();
        AddField(content, TR(kLabelUrl), font, labelWidth, std::unique_ptr<DuiControl>(url.release()), kBotEditHeight);
        AddHint(content,
                TR(L"会把触发的消息发给这个地址，再把它的应答发到群里。地址须以 http:// 或 https:// 开头，域名须在企业管理员维护的白名单内；呼叫失败时回复兜底文字。"),
                font, indent, contentWidth);
        std::unique_ptr<DuiEdit> fallback = MakeEdit(kIdRuleText, TR(L"可不填"), form.m_text, font, true, kBotReplyMaxChars);
        out.m_text = fallback.get();
        AddField(content, TR(kLabelFallback), font, labelWidth, std::unique_ptr<DuiControl>(fallback.release()),
                 kFallbackHeight);
        break;
    }
    //回复文字：正文 + Markdown + 可用的变量
    default:
    {
        std::unique_ptr<DuiEdit> text = MakeEdit(kIdRuleText, TRF(L"必填，最多 {0} 字", kBotReplyMaxChars), form.m_text,
                                                 font, true, kBotReplyMaxChars);
        out.m_text = text.get();
        AddField(content, TR(kLabelReply), font, labelWidth, std::unique_ptr<DuiControl>(text.release()), kReplyHeight);
        std::unique_ptr<DuiButton> markdown = MakeCheckBox(TR(L"按 Markdown 显示"), kIdRuleMarkdown, form.m_markdown,
                                                           font, width);
        out.m_markdown = markdown.get();
        content->AddChild(std::unique_ptr<DuiControl>(markdown.release()),
                          Hint().Fixed(kCheckRowHeight).Margin(indent, 0, 0, 0));
        AddHint(content, VariableHint(ctx.m_ownerType, form.m_trigger), font, indent, contentWidth);
        break;
    }
    }
}

void HarvestRuleForm(const RuleFormControls& c, BotRuleForm& form)
{
    if (c.m_name != NULL)
        form.m_name = EditText(c.m_name);
    if (c.m_enabled != NULL)
        form.m_enabled = c.m_enabled->IsChecked();
    form.m_trigger = ComboValue(c.m_trigger, c.m_triggerValues, form.m_trigger);
    if (c.m_keywords != NULL)
        form.m_keywords = EditText(c.m_keywords);
    if (c.m_match != NULL && c.m_match->GetCurSel() >= 0)
        form.m_match = bot_match_contains + c.m_match->GetCurSel();
    if (c.m_needAt != NULL)
        form.m_needAt = c.m_needAt->IsChecked();
    if (c.m_scheduleKind != NULL && c.m_scheduleKind->GetCurSel() >= 0)
        form.m_scheduleKind = bot_schedule_daily + c.m_scheduleKind->GetCurSel();
    if (c.m_scheduleTime != NULL)
        form.m_scheduleTime = EditText(c.m_scheduleTime);
    for (int d = 0; d < kDaysPerWeek; ++d)
    {
        if (c.m_weekdays[d] != NULL)
            form.m_weekdays[d] = c.m_weekdays[d]->IsChecked();
    }
    if (c.m_monthDay != NULL)
        form.m_monthDay = EditText(c.m_monthDay);
    for (int p = 0; p < kPresenceKinds; ++p)
    {
        if (c.m_presence[p] != NULL)
            form.m_presence[p] = c.m_presence[p]->IsChecked();
    }
    form.m_senderKind = ComboValue(c.m_senderKind, c.m_senderValues, form.m_senderKind);
    if (c.m_cooldown != NULL)
        form.m_cooldown = EditText(c.m_cooldown);
    if (c.m_ownerQuiet != NULL)
        form.m_ownerQuiet = EditText(c.m_ownerQuiet);
    if (c.m_windowOn != NULL)
        form.m_windowOn = c.m_windowOn->IsChecked();
    if (c.m_windowDays != NULL && c.m_windowDays->GetCurSel() >= 0)
        form.m_windowDays = bot_days_every + c.m_windowDays->GetCurSel();
    if (c.m_windowFrom != NULL)
        form.m_windowFrom = EditText(c.m_windowFrom);
    if (c.m_windowTo != NULL)
        form.m_windowTo = EditText(c.m_windowTo);
    form.m_action = ComboValue(c.m_action, c.m_actionValues, form.m_action);
    if (c.m_text != NULL)
        form.m_text = EditText(c.m_text);
    if (c.m_markdown != NULL)
        form.m_markdown = c.m_markdown->IsChecked();
    if (c.m_url != NULL)
        form.m_url = EditText(c.m_url);
}

std::unique_ptr<DuiVBox> BuildRuleWindowBody(HFONT font, ScrollFrameControls& out)
{
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec cancel;
    cancel.m_text = TR(L"取消");
    cancel.m_id = kIdRuleCancel;
    cancel.m_variant = DuiButton::Variant::Default;
    buttons.push_back(cancel);
    FooterButtonSpec ok;
    ok.m_text = TR(L"确定");
    ok.m_id = kIdRuleOk;
    ok.m_variant = DuiButton::Variant::Primary;
    buttons.push_back(ok);
    return BuildScrollFrame(font, buttons, out);
}

} // namespace flamingobot
