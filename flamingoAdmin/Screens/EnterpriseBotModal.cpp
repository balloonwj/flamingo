/**
 *  flamingoAdmin —— 新建 / 编辑企业机器人的编辑窗实现，说明见 EnterpriseBotModal.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/EnterpriseBotModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/List/DuiListBox.h"

#include <algorithm>

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 编辑窗几何（96 dpi 逻辑像素），与企业问答的编辑窗同一套风格 ----
const int kPanelW        = 940;  // 卡片宽度：左右两栏
const int kPanelH        = 640;  // 卡片高度（窗口够高时）
const int kMinPanelH     = 560;  // 窗口很矮时卡片最低高度，再矮就超出窗口
const int kModalMargin   = 16;   // 卡片与窗口上下边缘至少留出的距离
const int kHeaderH       = 56;   // 标题栏高度
const int kFooterH       = 64;   // 按钮栏高度
const int kPadH          = 24;   // 卡片左右内边距
const int kPadV          = 14;   // 主体区上下内边距
const int kColGap        = 28;   // 左右两栏之间的间距
const int kBackdropAlpha = 110;  // 遮罩不透明度（0-255）
const int kTitlePt       = 13;   // 标题字号（磅）
const int kLabelH        = 20;   // 字段标题行高
const int kEditH         = 32;   // 单行输入框与下拉框高度
const int kFieldGap      = 12;   // 字段之间的纵向间距
const int kListGap       = 8;    // 下拉框与列表、列表与按钮之间的间距
const int kGlyphW        = 64;   // 头像文字输入框宽度
const int kSwatchLead    = 16;   // 头像文字输入框与第一个色块之间的间距
const int kSwatchSize    = 22;   // 色块边长
const int kSwatchGap     = 8;    // 色块之间的间距
const int kSwatchRing    = 3;    // 选中色块外圈比色块大出的像素
const float kRingWidth   = 2.0f; // 选中色块外圈的线宽
const int kStatusComboW  = 120;  // 状态下拉框宽度
const int kCheckGap      = 16;   // 状态下拉框与勾选框之间的间距
const int kScopeComboW   = 200;  // 应用范围下拉框宽度
const int kMinGroupListH = 60;   // 群列表最低高度
const int kRuleListH     = 156;  // 规则列表高度
const int kSmallBtnH     = 30;   // 规则按钮、「试一试」一行的高度
const int kRuleBtnW      = 64;   // 规则按钮宽度
const int kSmallBtnGap   = 8;    // 规则按钮之间的间距
const int kSectionGap    = 14;   // 右栏各区之间的间距
const int kWebhookHeadH  = 26;   // Webhook 区标题行（含「复制」「重新生成」）的高度
const int kWebhookTopGap = 4;    // Webhook 标题行与第一行文字之间的间距
const int kWebhookRowH   = 20;   // Webhook 每行文字的高度
const int kCopyBtnW      = 64;   // 「复制」按钮宽度
const int kResetBtnW     = 84;   // 「重新生成」按钮宽度
const int kWebhookKeyW   = 72;   // Webhook 每行左侧「地址」「令牌」「签名密钥」的宽度
const int kTryAtW        = 110;  // 「@ 机器人」勾选框宽度
const int kTryBtnW       = 80;   // 「试一试」按钮宽度
const int kMinTryResultH = 40;   // 「试一试」结果区最低高度
const int kBtnH          = 34;   // 底部按钮高度
const int kBtnGap        = 8;    // 底部按钮间距
const int kBtnWCancel    = 72;   // 「取消」按钮宽度
const int kBtnWSave      = 88;   // 「保存」按钮宽度
const int kCloseBtnSize  = 28;   // 右上角关闭按钮边长

// 「试一试」群下拉框第一项（不指定群）的下标
const int kTryGroupNone = 0;

// 规则列表里每行前面的序号从 1 起
const int kRuleNumberBase = 1;

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

} // namespace

EnterpriseBotModal::EnterpriseBotModal()
    : m_editingId(0)
    , m_busy(false)
    , m_colorIndex(0)
    , m_editingRuleIndex(-1)
    , m_domainsKnown(false)
    , m_tryIsError(false)
    , m_name(nullptr)
    , m_glyph(nullptr)
    , m_desc(nullptr)
    , m_status(nullptr)
    , m_allowOff(nullptr)
    , m_scope(nullptr)
    , m_groupList(nullptr)
    , m_ruleList(nullptr)
    , m_btnRuleAdd(nullptr)
    , m_btnRuleEdit(nullptr)
    , m_btnRuleUp(nullptr)
    , m_btnRuleDown(nullptr)
    , m_btnRuleDelete(nullptr)
    , m_btnCopy(nullptr)
    , m_btnReset(nullptr)
    , m_tryText(nullptr)
    , m_tryAt(nullptr)
    , m_tryGroup(nullptr)
    , m_btnTry(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSave(nullptr)
{
    SetCtrlId(kIdBotModal);
    ::SetRectEmpty(&m_panelRect);
    ::SetRectEmpty(&m_tryResultRect);
    ::SetRectEmpty(&m_errorRect);
    for (int i = 0; i < botrule::kPaletteCount; ++i)
        ::SetRectEmpty(&m_swatchRects[i]);
    for (int i = 0; i < kWebhookRowCount; ++i)
        ::SetRectEmpty(&m_webhookRows[i]);

    // ---- 左栏 ----
    std::unique_ptr<DuiEdit> name = MakeEdit(kIdBotModalName, _T("例如：IT 小助手"), botapi::kBotNameMaxChars);
    m_name = name.get();
    AddChild(std::move(name));

    std::unique_ptr<DuiEdit> glyph = MakeEdit(kIdBotModalGlyph, _T("IT"), botapi::kBotGlyphMaxChars);
    m_glyph = glyph.get();
    AddChild(std::move(glyph));

    std::unique_ptr<DuiEdit> desc = MakeEdit(kIdBotModalDesc, _T("一句话说明这个机器人能做什么"), botapi::kBotDescMaxChars);
    m_desc = desc.get();
    AddChild(std::move(desc));

    std::unique_ptr<DuiComboBox> status(new DuiComboBox());
    status->SetCtrlId(kIdBotModalStatus);
    //下标与状态取值一致：0 启用、1 停用
    status->AddString(_T("启用"));
    status->AddString(_T("停用"));
    status->SetCurSel(botapi::kBotStatusActive, false);
    m_status = status.get();
    AddChild(std::move(status));

    std::unique_ptr<DuiButton> allowOff = MakeCheckbox(kIdBotModalAllowOff, _T("允许群主在本群关闭"));
    m_allowOff = allowOff.get();
    AddChild(std::move(allowOff));

    std::unique_ptr<DuiComboBox> scope(new DuiComboBox());
    scope->SetCtrlId(kIdBotModalScope);
    //下标与应用范围取值一致：0 全部群、1 所选的群
    scope->AddString(_T("全部群"));
    scope->AddString(_T("所选的群"));
    scope->SetCurSel(botapi::kBotScopeAll, false);
    m_scope = scope.get();
    AddChild(std::move(scope));

    std::unique_ptr<DuiListBox> groups(new DuiListBox());
    groups->SetCtrlId(kIdBotModalGroups);
    groups->SetShowCheckboxes(true);
    groups->SetEnabled(false);
    m_groupList = groups.get();
    AddChild(std::move(groups));

    // ---- 右栏：规则 ----
    std::unique_ptr<DuiListBox> rules(new DuiListBox());
    rules->SetCtrlId(kIdBotModalRules);
    m_ruleList = rules.get();
    AddChild(std::move(rules));

    std::unique_ptr<DuiButton> ruleAdd = MakeButton(kIdBotModalRuleAdd, _T("添加"), DuiButton::Variant::Default);
    m_btnRuleAdd = ruleAdd.get();
    AddChild(std::move(ruleAdd));
    std::unique_ptr<DuiButton> ruleEdit = MakeButton(kIdBotModalRuleEdit, _T("编辑"), DuiButton::Variant::Default);
    m_btnRuleEdit = ruleEdit.get();
    AddChild(std::move(ruleEdit));
    std::unique_ptr<DuiButton> ruleUp = MakeButton(kIdBotModalRuleUp, _T("上移"), DuiButton::Variant::Default);
    m_btnRuleUp = ruleUp.get();
    AddChild(std::move(ruleUp));
    std::unique_ptr<DuiButton> ruleDown = MakeButton(kIdBotModalRuleDown, _T("下移"), DuiButton::Variant::Default);
    m_btnRuleDown = ruleDown.get();
    AddChild(std::move(ruleDown));
    std::unique_ptr<DuiButton> ruleDelete = MakeButton(kIdBotModalRuleDelete, _T("删除"), DuiButton::Variant::Default);
    m_btnRuleDelete = ruleDelete.get();
    AddChild(std::move(ruleDelete));

    // ---- 右栏：Webhook ----
    std::unique_ptr<DuiButton> copy = MakeButton(kIdBotModalCopy, _T("复制"), DuiButton::Variant::Default);
    m_btnCopy = copy.get();
    AddChild(std::move(copy));
    std::unique_ptr<DuiButton> reset = MakeButton(kIdBotModalResetToken, _T("重新生成"), DuiButton::Variant::Default);
    m_btnReset = reset.get();
    AddChild(std::move(reset));

    // ---- 右栏：试一试 ----
    std::unique_ptr<DuiEdit> tryText = MakeEdit(kIdBotModalTryText, _T("输入一句群消息，例如：VPN 连不上怎么办"), 0);
    m_tryText = tryText.get();
    AddChild(std::move(tryText));

    std::unique_ptr<DuiButton> tryAt = MakeCheckbox(kIdBotModalTryAt, _T("@ 机器人"));
    tryAt->SetCheck(true, false);
    m_tryAt = tryAt.get();
    AddChild(std::move(tryAt));

    std::unique_ptr<DuiComboBox> tryGroup(new DuiComboBox());
    tryGroup->SetCtrlId(kIdBotModalTryGroup);
    tryGroup->AddString(_T("不指定群（不取群问答）"));
    tryGroup->SetCurSel(kTryGroupNone, false);
    m_tryGroup = tryGroup.get();
    AddChild(std::move(tryGroup));

    std::unique_ptr<DuiButton> tryRun = MakeButton(kIdBotModalTryRun, _T("试一试"), DuiButton::Variant::Default);
    m_btnTry = tryRun.get();
    AddChild(std::move(tryRun));

    // ---- 标题栏与按钮栏 ----
    std::unique_ptr<DuiButton> closeBtn(new DuiButton());
    closeBtn->SetCtrlId(kIdBotModalClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancelBtn = MakeButton(kIdBotModalCancel, _T("取消"), DuiButton::Variant::Default);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    std::unique_ptr<DuiButton> saveBtn = MakeButton(kIdBotModalSave, _T("保存"), DuiButton::Variant::Primary);
    m_btnSave = saveBtn.get();
    AddChild(std::move(saveBtn));

    UpdateRuleButtons();
    UpdateSecretButtons();
}

void EnterpriseBotModal::ShowForAdd(const std::vector<botapi::BotGroup>& groups)
{
    botapi::BotItem fresh;
    fresh.m_color = botrule::PaletteColor(0);
    ShowForEdit(fresh, groups);
}

void EnterpriseBotModal::ShowForEdit(const botapi::BotItem& bot, const std::vector<botapi::BotGroup>& groups)
{
    m_editingId = bot.m_botId;
    m_original = bot;
    if (m_name != nullptr)
        m_name->SetText(bot.m_name);
    if (m_glyph != nullptr)
        m_glyph->SetText(bot.m_glyph);
    if (m_desc != nullptr)
        m_desc->SetText(bot.m_desc);
    //头像底色：在色块里就选中那一块；不在（如服务端另行设置过）时原样保留，不选任何色块
    m_colorIndex = bot.m_color.IsEmpty() ? 0 : botrule::PaletteIndexOf(bot.m_color);
    m_customColor = (m_colorIndex < 0) ? bot.m_color : CString();
    if (m_status != nullptr)
        m_status->SetCurSel(bot.m_status == botapi::kBotStatusDisabled ? botapi::kBotStatusDisabled
                                                                        : botapi::kBotStatusActive, false);
    if (m_allowOff != nullptr)
        m_allowOff->SetCheck(bot.m_allowGroupOff, false);
    m_groups = groups;
    FillGroups(std::set<long long>(bot.m_groupIds.begin(), bot.m_groupIds.end()), 0);
    SetScope(bot.m_scope);
    m_rules = bot.m_rules;
    m_editingRuleIndex = -1;
    FillRules(m_rules.empty() ? -1 : 0);
    m_webhookPath = bot.m_webhookPath;
    m_webhookUrl = bot.m_webhookUrl;
    m_token = bot.m_webhookToken;
    m_secret = bot.m_secret;
    if (m_tryText != nullptr)
        m_tryText->SetText(_T(""));
    if (m_tryAt != nullptr)
        m_tryAt->SetCheck(true, false);
    m_tryResult.Empty();
    m_tryIsError = false;
    m_error.Empty();
    SetBusy(false);
    UpdateSecretButtons();
    Invalidate();
}

void EnterpriseBotModal::SetGroups(const std::vector<botapi::BotGroup>& groups)
{
    const std::set<long long> checked = CheckedGroups();
    const long long tryGroup = TryGroupId();
    m_groups = groups;
    FillGroups(checked, tryGroup);
}

void EnterpriseBotModal::SetDomains(const std::vector<CString>& domains, bool known)
{
    m_domains = domains;
    m_domainsKnown = known;
}

void EnterpriseBotModal::SetServer(const ServerConfig& server)
{
    m_server = server;
    Invalidate();
}

void EnterpriseBotModal::FillGroups(const std::set<long long>& checked, long long tryGroupId)
{
    if (m_groupList != nullptr)
    {
        m_groupList->DeleteAllItems();
        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            const int row = m_groupList->AddItem(m_groups[i].m_name, static_cast<LPARAM>(i));
            if (checked.count(m_groups[i].m_id) != 0)
                m_groupList->SetItemChecked(row, true, false);
        }
        m_groupList->Invalidate();
    }
    if (m_tryGroup != nullptr)
    {
        //第一项「不指定群」保留，其余按群重填
        while (m_tryGroup->GetCount() > kTryGroupNone + 1)
            m_tryGroup->DeleteString(m_tryGroup->GetCount() - 1);
        int selected = kTryGroupNone;
        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            m_tryGroup->AddString(m_groups[i].m_name);
            if (m_groups[i].m_id == tryGroupId)
                selected = static_cast<int>(i) + kTryGroupNone + 1;
        }
        m_tryGroup->SetCurSel(selected, false);
    }
}

std::set<long long> EnterpriseBotModal::CheckedGroups() const
{
    std::set<long long> checked;
    if (m_groupList == nullptr)
        return checked;
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_groupList->IsItemChecked(static_cast<int>(i)))
            checked.insert(m_groups[i].m_id);
    }
    return checked;
}

long long EnterpriseBotModal::TryGroupId() const
{
    if (m_tryGroup == nullptr)
        return 0;
    const int index = m_tryGroup->GetCurSel() - (kTryGroupNone + 1);
    if (index < 0 || index >= static_cast<int>(m_groups.size()))
        return 0;
    return m_groups[static_cast<size_t>(index)].m_id;
}

void EnterpriseBotModal::SetScope(int scope)
{
    if (m_scope != nullptr)
        m_scope->SetCurSel(scope == botapi::kBotScopeSelected ? botapi::kBotScopeSelected : botapi::kBotScopeAll, false);
    OnScopeChanged();
}

void EnterpriseBotModal::OnScopeChanged()
{
    const bool selected = (m_scope != nullptr && m_scope->GetCurSel() == botapi::kBotScopeSelected);
    if (m_groupList != nullptr)
    {
        m_groupList->SetEnabled(selected);
        m_groupList->Invalidate();
    }
}

int EnterpriseBotModal::GroupRowCount() const
{
    return static_cast<int>(m_groups.size());
}

bool EnterpriseBotModal::IsGroupRowChecked(int index) const
{
    if (m_groupList == nullptr || index < 0 || index >= GroupRowCount())
        return false;
    return m_groupList->IsItemChecked(index);
}

void EnterpriseBotModal::SetGroupRowChecked(int index, bool checked)
{
    if (m_groupList == nullptr || index < 0 || index >= GroupRowCount())
        return;
    m_groupList->SetItemChecked(index, checked, false);
}

botapi::BotItem EnterpriseBotModal::GetBot() const
{
    botapi::BotItem bot = m_original;
    bot.m_botId = m_editingId;
    bot.m_name = (m_name != nullptr) ? m_name->GetText() : CString();
    bot.m_name.Trim();
    bot.m_glyph = (m_glyph != nullptr) ? m_glyph->GetText() : CString();
    bot.m_glyph.Trim();
    bot.m_desc = (m_desc != nullptr) ? m_desc->GetText() : CString();
    bot.m_desc.Trim();
    bot.m_color = (m_colorIndex >= 0) ? botrule::PaletteColor(m_colorIndex) : m_customColor;
    bot.m_status = (m_status != nullptr && m_status->GetCurSel() == botapi::kBotStatusDisabled)
                   ? botapi::kBotStatusDisabled : botapi::kBotStatusActive;
    bot.m_allowGroupOff = (m_allowOff != nullptr && m_allowOff->IsChecked());
    bot.m_scope = (m_scope != nullptr && m_scope->GetCurSel() == botapi::kBotScopeSelected)
                  ? botapi::kBotScopeSelected : botapi::kBotScopeAll;
    bot.m_groupIds.clear();
    if (bot.m_scope == botapi::kBotScopeSelected)
    {
        const std::set<long long> checked = CheckedGroups();
        //按群列表的顺序给出，便于核对
        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            if (checked.count(m_groups[i].m_id) != 0)
                bot.m_groupIds.push_back(m_groups[i].m_id);
        }
    }
    bot.m_rules = m_rules;
    bot.m_hasRules = true;
    bot.m_ruleCount = static_cast<int>(m_rules.size());
    bot.m_webhookToken = m_token;
    bot.m_secret = m_secret;
    return bot;
}

bool EnterpriseBotModal::Validate(CString* error) const
{
    return botrule::ValidateBot(GetBot(), m_domainsKnown ? &m_domains : nullptr, error);
}

void EnterpriseBotModal::SetError(const CString& text)
{
    if (m_error == text)
        return;
    m_error = text;
    Invalidate();
}

void EnterpriseBotModal::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSave != nullptr)
    {
        m_btnSave->SetEnabled(!busy);
        m_btnSave->SetText(busy ? _T("保存中…") : _T("保存"));
    }
}

int EnterpriseBotModal::SelectedRuleIndex() const
{
    if (m_ruleList == nullptr)
        return -1;
    const int sel = m_ruleList->GetCurSel();
    return (sel >= 0 && sel < RuleCount()) ? sel : -1;
}

void EnterpriseBotModal::SelectRule(int index)
{
    if (m_ruleList != nullptr)
        m_ruleList->SetCurSel((index >= 0 && index < RuleCount()) ? index : -1, false);
    UpdateRuleButtons();
}

void EnterpriseBotModal::OnRuleSelectionChanged()
{
    UpdateRuleButtons();
}

void EnterpriseBotModal::FillRules(int selected)
{
    if (m_ruleList != nullptr)
    {
        m_ruleList->DeleteAllItems();
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            CString text;
            text.Format(_T("%u. %s"), static_cast<unsigned>(i + kRuleNumberBase),
                        botrule::RuleSummary(m_rules[i]).GetString());
            m_ruleList->AddItem(text, static_cast<LPARAM>(i));
        }
        if (selected >= 0 && selected < RuleCount())
        {
            m_ruleList->SetCurSel(selected, false);
            m_ruleList->EnsureVisible(selected);
        }
        m_ruleList->Invalidate();
    }
    UpdateRuleButtons();
}

void EnterpriseBotModal::UpdateRuleButtons()
{
    const int sel = SelectedRuleIndex();
    if (m_btnRuleAdd != nullptr)
        m_btnRuleAdd->SetEnabled(RuleCount() < botapi::kBotRuleMax);
    if (m_btnRuleEdit != nullptr)
        m_btnRuleEdit->SetEnabled(sel >= 0);
    if (m_btnRuleDelete != nullptr)
        m_btnRuleDelete->SetEnabled(sel >= 0);
    if (m_btnRuleUp != nullptr)
        m_btnRuleUp->SetEnabled(sel > 0);
    if (m_btnRuleDown != nullptr)
        m_btnRuleDown->SetEnabled(sel >= 0 && sel + 1 < RuleCount());
}

bool EnterpriseBotModal::IsRuleButtonEnabled(UINT ctrlId) const
{
    const DuiButton* const buttons[] = { m_btnRuleAdd, m_btnRuleEdit, m_btnRuleUp, m_btnRuleDown, m_btnRuleDelete };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        if (buttons[i] != nullptr && buttons[i]->GetCtrlId() == ctrlId)
            return buttons[i]->IsEnabled();
    }
    return false;
}

bool EnterpriseBotModal::BeginAddRule(botapi::BotRule* ruleOut)
{
    if (RuleCount() >= botapi::kBotRuleMax)
    {
        CString text;
        text.Format(_T("规则最多 %d 条"), botapi::kBotRuleMax);
        SetError(text);
        return false;
    }
    m_editingRuleIndex = -1;
    if (ruleOut != nullptr)
        *ruleOut = botapi::BotRule();
    return true;
}

bool EnterpriseBotModal::BeginEditRule(botapi::BotRule* ruleOut)
{
    const int sel = SelectedRuleIndex();
    if (sel < 0)
    {
        SetError(_T("请先在规则列表里选中一条规则"));
        return false;
    }
    m_editingRuleIndex = sel;
    if (ruleOut != nullptr)
        *ruleOut = m_rules[static_cast<size_t>(sel)];
    return true;
}

void EnterpriseBotModal::ApplyRuleEdit(const botapi::BotRule& rule)
{
    int selected = 0;
    if (m_editingRuleIndex >= 0 && m_editingRuleIndex < RuleCount())
    {
        m_rules[static_cast<size_t>(m_editingRuleIndex)] = rule;
        selected = m_editingRuleIndex;
    }
    else
    {
        m_rules.push_back(rule);
        selected = RuleCount() - 1;
    }
    m_editingRuleIndex = -1;
    SetError(CString());
    FillRules(selected);
}

bool EnterpriseBotModal::MoveSelectedRule(int delta)
{
    const int sel = SelectedRuleIndex();
    const int target = sel + delta;
    if (sel < 0 || target < 0 || target >= RuleCount() || delta == 0)
        return false;
    std::swap(m_rules[static_cast<size_t>(sel)], m_rules[static_cast<size_t>(target)]);
    FillRules(target);
    return true;
}

bool EnterpriseBotModal::DeleteSelectedRule()
{
    const int sel = SelectedRuleIndex();
    if (sel < 0)
        return false;
    m_rules.erase(m_rules.begin() + sel);
    const int next = (sel < RuleCount()) ? sel : RuleCount() - 1;
    FillRules(next);
    return true;
}

void EnterpriseBotModal::SetSecrets(long long botId, const CString& token, const CString& secret)
{
    if (botId <= 0 || botId != m_editingId)
        return;
    m_token = token;
    m_secret = secret;
    UpdateSecretButtons();
    Invalidate();
}

CString EnterpriseBotModal::WebhookUrlText() const
{
    if (!IsEditing())
        return CString();
    return botapi::WebhookUrl(m_server, m_webhookPath, m_webhookUrl);
}

CString EnterpriseBotModal::WebhookCopyText() const
{
    if (!IsEditing())
        return CString();
    return _T("Webhook 地址：") + WebhookUrlText() + _T("\r\n令牌：") + m_token + _T("\r\n签名密钥：") + m_secret;
}

void EnterpriseBotModal::UpdateSecretButtons()
{
    if (m_btnCopy != nullptr)
        m_btnCopy->SetEnabled(HasSecrets());
    if (m_btnReset != nullptr)
        m_btnReset->SetEnabled(IsEditing());
}

botapi::BotTryInput EnterpriseBotModal::GetTryInput() const
{
    botapi::BotTryInput input;
    input.m_text = (m_tryText != nullptr) ? m_tryText->GetText() : CString();
    input.m_text.Trim();
    input.m_at = (m_tryAt != nullptr && m_tryAt->IsChecked());
    input.m_groupId = TryGroupId();
    return input;
}

void EnterpriseBotModal::SetTryResult(const CString& text, bool isError)
{
    m_tryResult = text;
    m_tryIsError = isError;
    Invalidate();
}

void EnterpriseBotModal::SelectColor(int index)
{
    if (index < 0 || index >= botrule::kPaletteCount)
        return;
    m_colorIndex = index;
    m_customColor.Empty();
    Invalidate();
}

void EnterpriseBotModal::AddLabel(const RECT& rc, const CString& text, COLORREF color, bool smallFont)
{
    LabelSpot spot;
    spot.m_rc = rc;
    spot.m_text = text;
    spot.m_color = color;
    spot.m_smallFont = smallFont;
    m_labels.push_back(spot);
}

void EnterpriseBotModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    m_labels.clear();

    //卡片高度取设计高度与窗口可用高度中较小的一个，但不低于 kMinPanelH
    const int availH = (rcAvail.bottom - rcAvail.top) - 2 * kModalMargin;
    int panelH = (availH < kPanelH) ? availH : kPanelH;
    if (panelH < kMinPanelH)
        panelH = kMinPanelH;
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect = RECT{ cx - kPanelW / 2, cy - panelH / 2, cx + kPanelW / 2, cy - panelH / 2 + panelH };

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;
    const int colW = (R - L - kColGap) / 2;
    const int leftR = L + colW;
    const int rightL = leftR + kColGap;
    const int top = m_panelRect.top + kHeaderH + kPadV;
    const int bottom = m_panelRect.bottom - kFooterH - kPadV;

    if (m_btnClose != nullptr)
    {
        m_btnClose->ForceLayout(RECT{ R - kCloseBtnSize, m_panelRect.top + (kHeaderH - kCloseBtnSize) / 2,
                                      R, m_panelRect.top + (kHeaderH + kCloseBtnSize) / 2 });
    }

    // ---- 左栏：名称 → 头像 → 简介 → 状态 → 应用范围 ----
    int y = top;
    CString nameLabel;
    nameLabel.Format(_T("名称（最多 %d 个字）"), botapi::kBotNameMaxChars);
    AddLabel(RECT{ L, y, leftR, y + kLabelH }, nameLabel, theme::kInk400, true);
    y += kLabelH;
    if (m_name != nullptr)
        m_name->ForceLayout(RECT{ L, y, leftR, y + kEditH });
    y += kEditH + kFieldGap;

    CString glyphLabel;
    glyphLabel.Format(_T("头像（文字最多 %d 个字，留空时取名称的第一个字；点色块选底色）"), botapi::kBotGlyphMaxChars);
    AddLabel(RECT{ L, y, leftR, y + kLabelH }, glyphLabel, theme::kInk400, true);
    y += kLabelH;
    if (m_glyph != nullptr)
        m_glyph->ForceLayout(RECT{ L, y, L + kGlyphW, y + kEditH });
    {
        const int sx = L + kGlyphW + kSwatchLead;
        const int sy = y + (kEditH - kSwatchSize) / 2;
        for (int i = 0; i < botrule::kPaletteCount; ++i)
        {
            const int left = sx + i * (kSwatchSize + kSwatchGap);
            m_swatchRects[i] = RECT{ left, sy, left + kSwatchSize, sy + kSwatchSize };
        }
    }
    y += kEditH + kFieldGap;

    CString descLabel;
    descLabel.Format(_T("简介（最多 %d 个字，显示在群里的机器人资料卡上）"), botapi::kBotDescMaxChars);
    AddLabel(RECT{ L, y, leftR, y + kLabelH }, descLabel, theme::kInk400, true);
    y += kLabelH;
    if (m_desc != nullptr)
        m_desc->ForceLayout(RECT{ L, y, leftR, y + kEditH });
    y += kEditH + kFieldGap;

    AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("状态"), theme::kInk400, true);
    y += kLabelH;
    if (m_status != nullptr)
        m_status->ForceLayout(RECT{ L, y, L + kStatusComboW, y + kEditH });
    if (m_allowOff != nullptr)
        m_allowOff->ForceLayout(RECT{ L + kStatusComboW + kCheckGap, y, leftR, y + kEditH });
    y += kEditH + kFieldGap;

    AddLabel(RECT{ L, y, leftR, y + kLabelH }, _T("应用范围（机器人在哪些群里响应）"), theme::kInk400, true);
    y += kLabelH;
    if (m_scope != nullptr)
        m_scope->ForceLayout(RECT{ L, y, L + kScopeComboW, y + kEditH });
    y += kEditH + kListGap;
    const int groupBottom = (bottom > y + kMinGroupListH) ? bottom : y + kMinGroupListH;
    if (m_groupList != nullptr)
        m_groupList->ForceLayout(RECT{ L, y, leftR, groupBottom });

    // ---- 右栏：规则 → Webhook → 试一试 ----
    y = top;
    CString rulesLabel;
    rulesLabel.Format(_T("规则（最多 %d 条，按顺序判定，只执行第一条命中的；双击可编辑）"), botapi::kBotRuleMax);
    AddLabel(RECT{ rightL, y, R, y + kLabelH }, rulesLabel, theme::kInk400, true);
    y += kLabelH;
    if (m_ruleList != nullptr)
        m_ruleList->ForceLayout(RECT{ rightL, y, R, y + kRuleListH });
    y += kRuleListH + kListGap;
    {
        DuiButton* const buttons[] = { m_btnRuleAdd, m_btnRuleEdit, m_btnRuleUp, m_btnRuleDown, m_btnRuleDelete };
        int x = rightL;
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            if (buttons[i] != nullptr)
                buttons[i]->ForceLayout(RECT{ x, y, x + kRuleBtnW, y + kSmallBtnH });
            x += kRuleBtnW + kSmallBtnGap;
        }
    }
    y += kSmallBtnH + kSectionGap;

    const int resetLeft = R - kResetBtnW;
    const int copyLeft = resetLeft - kSmallBtnGap - kCopyBtnW;
    AddLabel(RECT{ rightL, y, copyLeft - kSmallBtnGap, y + kWebhookHeadH }, _T("外部系统推送（Webhook）"), theme::kInk400,
             true);
    if (m_btnCopy != nullptr)
        m_btnCopy->ForceLayout(RECT{ copyLeft, y, copyLeft + kCopyBtnW, y + kWebhookHeadH });
    if (m_btnReset != nullptr)
        m_btnReset->ForceLayout(RECT{ resetLeft, y, R, y + kWebhookHeadH });
    y += kWebhookHeadH + kWebhookTopGap;
    for (int i = 0; i < kWebhookRowCount; ++i)
    {
        m_webhookRows[i] = RECT{ rightL, y, R, y + kWebhookRowH };
        y += kWebhookRowH;
    }
    y += kSectionGap;

    AddLabel(RECT{ rightL, y, R, y + kLabelH }, _T("试一试（按当前编辑的内容判定，不会发出任何消息）"), theme::kInk400, true);
    y += kLabelH;
    if (m_tryText != nullptr)
        m_tryText->ForceLayout(RECT{ rightL, y, R, y + kEditH });
    y += kEditH + kListGap;
    if (m_tryAt != nullptr)
        m_tryAt->ForceLayout(RECT{ rightL, y, rightL + kTryAtW, y + kSmallBtnH });
    if (m_tryGroup != nullptr)
        m_tryGroup->ForceLayout(RECT{ rightL + kTryAtW + kSmallBtnGap, y, R - kTryBtnW - kSmallBtnGap, y + kSmallBtnH });
    if (m_btnTry != nullptr)
        m_btnTry->ForceLayout(RECT{ R - kTryBtnW, y, R, y + kSmallBtnH });
    y += kSmallBtnH + kListGap;
    m_tryResultRect = RECT{ rightL, y, R, (bottom > y + kMinTryResultH) ? bottom : y + kMinTryResultH };

    // ---- 按钮栏：自右向左依次是 保存 / 取消，左侧是错误提示 ----
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    const int saveLeft = R - kBtnWSave;
    const int cancelLeft = saveLeft - kBtnGap - kBtnWCancel;
    if (m_btnSave != nullptr)
        m_btnSave->ForceLayout(RECT{ saveLeft, by, R, by + kBtnH });
    if (m_btnCancel != nullptr)
        m_btnCancel->ForceLayout(RECT{ cancelLeft, by, cancelLeft + kBtnWCancel, by + kBtnH });
    m_errorRect = RECT{ L, by, cancelLeft - kBtnGap, by + kBtnH };
}

void EnterpriseBotModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;
    const UINT lineFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

    ui::DrawTextLine(hdc, IsEditing() ? _T("编辑企业机器人") : _T("新建企业机器人"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    //字段标题与说明：位置在 Layout 里算好
    for (size_t i = 0; i < m_labels.size(); ++i)
    {
        const LabelSpot& spot = m_labels[i];
        ui::DrawTextLine(hdc, spot.m_text, spot.m_rc, spot.m_color,
                         ui::UiFont(spot.m_smallFont ? theme::kFontSizeSmall : theme::kFontSizeNormal, false), lineFlags);
    }

    //头像底色色块：选中的一块外面画一圈深色描边
    for (int i = 0; i < botrule::kPaletteCount; ++i)
    {
        const RECT& sw = m_swatchRects[i];
        ui::FillRoundRect(hdc, sw, botrule::ColorFromHex(botrule::PaletteColor(i), theme::kInk300), kSwatchSize / 2);
        if (i == m_colorIndex)
        {
            RECT ring = sw;
            ::InflateRect(&ring, kSwatchRing, kSwatchRing);
            ui::StrokeRoundRect(hdc, ring, theme::kInk700, (kSwatchSize + 2 * kSwatchRing) / 2, kRingWidth);
        }
    }

    //Webhook：已保存的机器人显示地址、令牌、签名密钥；新建的提示保存后生成
    HFONT valueFont = ui::UiFont(theme::kFontSizeSmall, false);
    if (!IsEditing())
    {
        ui::DrawTextLine(hdc, _T("保存后生成 Webhook 地址、令牌与签名密钥；外部系统按协议文档 25.4 推送"),
                         m_webhookRows[0], theme::kInk400, valueFont, lineFlags);
    }
    else
    {
        const LPCTSTR keys[kWebhookRowCount] = { _T("地址"), _T("令牌"), _T("签名密钥") };
        const CString values[kWebhookRowCount] =
        {
            WebhookUrlText(),
            m_token.IsEmpty() ? CString(_T("（没有令牌，可点「重新生成」）")) : m_token,
            m_secret.IsEmpty() ? CString(_T("（没有签名密钥）")) : m_secret,
        };
        for (int i = 0; i < kWebhookRowCount; ++i)
        {
            const RECT& row = m_webhookRows[i];
            ui::DrawTextLine(hdc, keys[i], RECT{ row.left, row.top, row.left + kWebhookKeyW, row.bottom }, theme::kInk400,
                             valueFont, lineFlags);
            ui::DrawTextLine(hdc, values[i], RECT{ row.left + kWebhookKeyW, row.top, row.right, row.bottom },
                             theme::kInk700, valueFont, lineFlags);
        }
    }

    //「试一试」的结果：可换行
    if (!m_tryResult.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_tryResult, m_tryResultRect, m_tryIsError ? theme::kColorDanger : theme::kInk600,
                         ui::UiFont(theme::kFontSizeNormal, false), DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }

    //按钮栏左侧的红字错误提示
    if (!m_error.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_error, m_errorRect, theme::kColorDanger, ui::UiFont(theme::kFontSizeNormal, false),
                         lineFlags);
    }

    //子控件（输入框、下拉框、列表、按钮）画在最上层
    DuiControl::OnPaint(hdc, rcDirty);
}

bool EnterpriseBotModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    //点中色块：选中这个底色；其余位置（遮罩、卡片空白）吞掉点击，不关闭
    for (int i = 0; i < botrule::kPaletteCount; ++i)
    {
        if (::PtInRect(&m_swatchRects[i], pt))
        {
            SelectColor(i);
            return true;
        }
    }
    return true;
}

} // namespace flamingoadmin
