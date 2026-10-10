/**
 *  机器人编辑窗，见 BotEditWindow.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotEditWindow.h"

#include "../Core/BotCodec.h"
#include "../Core/BotRules.h"
#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/Log/IULog.h"

namespace flamingobot {

namespace {

//确认框的标题与正文（文件作用域的字符串只做标记，使用处再调 TR）
const wchar_t* const kConfirmDiscard = TR_NOOP(L"修改还没有保存，确定关闭吗？");
const wchar_t* const kConfirmDeleteRule = TR_NOOP(L"确定删除第 {0} 条规则吗？");

} // namespace

CBotEditWindow::CBotEditWindow(IBotWindowOwner* owner, IBotEditHost* host)
    : m_owner(owner)
    , m_host(host)
    , m_clientId(0)
    , m_groupId(0)
    , m_allowExternal(false)
    , m_saveSeq(0)
    , m_trySeq(0)
    , m_ruleWindow(owner, this)
    , m_closingByHost(false)
{
}

CBotEditWindow::~CBotEditWindow()
{
    Close();
}

bool CBotEditWindow::Open(HWND ownerWnd, const BotRecord& bot, int64_t groupId, bool allowExternal)
{
    if (IsOpen())
    {
        BringToFront();
        return false;
    }
    m_bot = bot;
    m_groupId = groupId;
    m_allowExternal = (bot.m_ownerType == bot_owner_group) && allowExternal;
    m_saveSeq = 0;
    m_trySeq = 0;
    m_original = BotCodec::BotToSaveJson(m_bot);
    if (!CreateFramed(ownerWnd, WindowTitle(), false))
        return false;
    if (m_owner != NULL)
        m_clientId = m_owner->RegisterClient(this);
    EditView view;
    view.m_ownerType = m_bot.m_ownerType;
    view.m_showWebhook = m_allowExternal;
    view.m_name = Utf8ToWide(m_bot.m_name);
    view.m_desc = Utf8ToWide(m_bot.m_desc);
    view.m_glyph = Utf8ToWide(m_bot.m_glyph);
    view.m_color = Utf8ToWide(m_bot.m_color);
    view.m_webhookToken = Utf8ToWide(m_bot.m_webhookToken);
    view.m_secret = Utf8ToWide(m_bot.m_secret);
    view.m_webhookUrl = Utf8ToWide(m_bot.m_webhookUrl);
    std::unique_ptr<balloonwjui::DuiVBox> body = BuildEditBody(view, Font(), m_controls);
    FillRuleList(m_controls, m_bot.m_rules, m_bot.m_ownerType, Font());
    FinishFramed(std::unique_ptr<balloonwjui::DuiControl>(body.release()), kEditWindowWidth, kEditWindowHeight, ownerWnd);
    RelayoutScroll(m_controls.m_frame.m_scroll);
    ShowWindow(SW_SHOW);
    DisableOwner(ownerWnd);
    ::SetForegroundWindow(m_hWnd);
    if (m_controls.m_name != NULL)
        m_controls.m_name->SetFocus();
    LOG_INFO("bot edit window opened, botid=%lld, ownertype=%d, rules=%u.", static_cast<long long>(m_bot.m_botId),
             m_bot.m_ownerType, static_cast<unsigned>(m_bot.m_rules.size()));
    return true;
}

void CBotEditWindow::Close()
{
    m_ruleWindow.Close();
    if (!IsWindow())
        return;
    m_closingByHost = true;
    RestoreOwner();
    DestroyWindow();
    m_closingByHost = false;
}

bool CBotEditWindow::IsOpen() const
{
    return IsWindow() != FALSE;
}

int64_t CBotEditWindow::EditingBotId() const
{
    return IsOpen() ? m_bot.m_botId : -1;
}

//======================== 应答 ========================

void CBotEditWindow::OnBotReply(const BotReply& reply)
{
    if (!IsOpen())
        return;
    //「试一试」的结果
    if (reply.m_cmd == bot_cmd_try && reply.m_seq == m_trySeq)
    {
        m_trySeq = 0;
        SetBusy(m_saveSeq != 0, false);
        if (reply.m_parsed && reply.m_code == bot_error_ok)
            SetTryResult(m_controls, TryResultText(reply.m_try), false, Font());
        else
            SetTryResult(m_controls, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true, Font());
        return;
    }
    //保存的结果
    const bool isSave = (reply.m_cmd == bot_cmd_save_group_bot || reply.m_cmd == bot_cmd_save_my_bot);
    if (!isSave || reply.m_seq != m_saveSeq)
        return;
    m_saveSeq = 0;
    if (!reply.m_parsed || reply.m_code != bot_error_ok)
    {
        SetBusy(false, m_trySeq != 0);
        SetStatusText(m_controls.m_frame.m_status, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true);
        LOG_WARNING("bot save failed, botid=%lld, code=%d.", static_cast<long long>(m_bot.m_botId), reply.m_code);
        return;
    }
    if (reply.m_botId > 0)
        m_bot.m_botId = reply.m_botId;
    LOG_INFO("bot saved, botid=%lld, ownertype=%d.", static_cast<long long>(m_bot.m_botId), m_bot.m_ownerType);
    m_original = BotCodec::BotToSaveJson(m_bot);
    if (m_host != NULL)
        m_host->OnBotSaved(m_bot);
    PostMessage(kMsgBotEditDeferred, edit_defer_close_saved, 0);
}

void CBotEditWindow::OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds)
{
    //编辑窗本身不选人（选人由规则编辑窗发起）
    (void)requestId;
    (void)userIds;
}

void CBotEditWindow::OnRuleEdited(int index, const BotRule& rule)
{
    if (index < 0)
    {
        m_bot.m_rules.push_back(rule);
    }
    else if (static_cast<size_t>(index) < m_bot.m_rules.size())
    {
        m_bot.m_rules[index] = rule;
    }
    FillRuleList(m_controls, m_bot.m_rules, m_bot.m_ownerType, Font());
    SetStatusText(m_controls.m_frame.m_status, std::wstring(), false);
}

void CBotEditWindow::OnRuleWindowClosed()
{
    BringToFront();
}

//======================== 消息 ========================

LRESULT CBotEditWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    RequestClose();
    return 0;
}

LRESULT CBotEditWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    m_ruleWindow.Close();
    RestoreOwner();
    if (m_owner != NULL && m_clientId != 0)
        m_owner->UnregisterClient(m_clientId);
    m_clientId = 0;
    m_saveSeq = 0;
    m_trySeq = 0;
    m_controls = EditControls();
    if (!m_closingByHost && m_host != NULL)
        m_host->OnEditWindowClosed();
    return 0;
}

LRESULT CBotEditWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //名称、字形、底色变了：刷新头像预览（不改动控件树，直接做；通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (pNotify->ctrlId == kIdEditName || pNotify->ctrlId == kIdEditGlyph || pNotify->ctrlId == kIdEditColor))
    {
        RefreshAvatar();
        return 0;
    }
    if (pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    size_t index = 0;
    int button = 0;
    //规则卡片上的按钮：会重建规则列表（按钮自己就在列表里），延后执行（UI 约定第 13 条）
    if (ParseEditRuleButtonId(pNotify->ctrlId, index, button))
    {
        static const int kDeferOf[] = { edit_defer_edit_rule, edit_defer_rule_up, edit_defer_rule_down,
                                        edit_defer_delete_rule };
        PostMessage(kMsgBotEditDeferred, static_cast<WPARAM>(kDeferOf[button]), static_cast<LPARAM>(index));
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //「添加规则」
    case kIdEditAddRule:
        PostMessage(kMsgBotEditDeferred, edit_defer_add_rule, 0);
        return 0;
    //「试一试」
    case kIdEditTry:
        PostMessage(kMsgBotEditDeferred, edit_defer_try, 0);
        return 0;
    //「保存」
    case kIdEditSave:
        PostMessage(kMsgBotEditDeferred, edit_defer_save, 0);
        return 0;
    //「取消」
    case kIdEditCancel:
        PostMessage(kMsgBotEditDeferred, edit_defer_cancel, 0);
        return 0;
    //其它控件的单击不处理
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CBotEditWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (!IsOpen())
        return 0;
    const size_t index = static_cast<size_t>(lParam);
    switch (static_cast<int>(wParam))
    {
    //添加规则
    case edit_defer_add_rule:
        OpenRuleEditor(-1);
        break;
    //编辑规则
    case edit_defer_edit_rule:
        OpenRuleEditor(static_cast<int>(index));
        break;
    //上移
    case edit_defer_rule_up:
        MoveRule(index, true);
        break;
    //下移
    case edit_defer_rule_down:
        MoveRule(index, false);
        break;
    //删除规则
    case edit_defer_delete_rule:
        DeleteRule(index);
        break;
    //试一试
    case edit_defer_try:
        TryBot();
        break;
    //保存
    case edit_defer_save:
        Save();
        break;
    //取消
    case edit_defer_cancel:
        RequestClose();
        break;
    //保存成功之后关闭
    case edit_defer_close_saved:
        CloseByUser();
        break;
    //不认识的动作
    default:
        break;
    }
    return 0;
}

//======================== 操作 ========================

void CBotEditWindow::HarvestBasics()
{
    m_bot.m_name = WideToUtf8(TrimWide(EditText(m_controls.m_name)));
    m_bot.m_desc = WideToUtf8(TrimWide(EditText(m_controls.m_desc)));
    m_bot.m_glyph = WideToUtf8(TrimWide(EditText(m_controls.m_glyph)));
    if (m_controls.m_color != NULL)
    {
        size_t count = 0;
        const BotColorPreset* presets = ColorPresets(count);
        const int sel = m_controls.m_color->GetCurSel();
        if (sel >= 0 && static_cast<size_t>(sel) < count)
            m_bot.m_color = presets[sel].m_hex;
    }
}

void CBotEditWindow::RefreshAvatar()
{
    if (m_controls.m_avatar == NULL)
        return;
    std::wstring color;
    if (m_controls.m_color != NULL)
    {
        size_t count = 0;
        const BotColorPreset* presets = ColorPresets(count);
        const int sel = m_controls.m_color->GetCurSel();
        if (sel >= 0 && static_cast<size_t>(sel) < count)
            color = Utf8ToWide(presets[sel].m_hex);
    }
    ApplyBotAvatar(m_controls.m_avatar, EditText(m_controls.m_glyph), EditText(m_controls.m_name), color);
}

void CBotEditWindow::OpenRuleEditor(int index)
{
    if (index >= 0 && static_cast<size_t>(index) >= m_bot.m_rules.size())
        return;
    if (index < 0 && static_cast<int>(m_bot.m_rules.size()) >= kBotRuleMax)
    {
        SetStatusText(m_controls.m_frame.m_status, TRF(L"每个机器人最多 {0} 条规则。", kBotRuleMax), true);
        return;
    }
    const BotRule rule = (index < 0) ? MakeDefaultRule(m_bot.m_ownerType) : m_bot.m_rules[index];
    m_ruleWindow.Open(m_hWnd, index, rule, m_bot.m_ownerType, m_allowExternal);
}

void CBotEditWindow::MoveRule(size_t index, bool up)
{
    if (index >= m_bot.m_rules.size())
        return;
    if (up && index > 0)
        std::swap(m_bot.m_rules[index], m_bot.m_rules[index - 1]);
    else if (!up && index + 1 < m_bot.m_rules.size())
        std::swap(m_bot.m_rules[index], m_bot.m_rules[index + 1]);
    else
        return;
    FillRuleList(m_controls, m_bot.m_rules, m_bot.m_ownerType, Font());
}

void CBotEditWindow::DeleteRule(size_t index)
{
    if (index >= m_bot.m_rules.size())
        return;
    const std::wstring text = TRF(kConfirmDeleteRule, static_cast<int>(index + 1));
    if (::MessageBoxW(m_hWnd, text.c_str(), WindowTitle().c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
        return;
    m_bot.m_rules.erase(m_bot.m_rules.begin() + static_cast<std::ptrdiff_t>(index));
    FillRuleList(m_controls, m_bot.m_rules, m_bot.m_ownerType, Font());
}

void CBotEditWindow::TryBot()
{
    if (m_owner == NULL || m_trySeq != 0)
        return;
    HarvestBasics();
    BotTryInput input;
    input.m_text = WideToUtf8(EditText(m_controls.m_tryText));
    if (TrimUtf8(input.m_text).empty())
    {
        SetTryResult(m_controls, TR(L"请先输入一段文字。"), true, Font());
        return;
    }
    input.m_at = (m_controls.m_tryAt != NULL) && m_controls.m_tryAt->IsChecked();
    if (m_controls.m_tryPresence != NULL && m_controls.m_tryPresence->GetCurSel() >= 0)
        input.m_presence = m_controls.m_tryPresence->GetCurSel();
    BotRecord draft = m_bot;
    //草稿的名称为空时服务端也照常判定；这里补一个占位名，免得判定前就因名称不合法被拒
    if (TrimUtf8(draft.m_name).empty())
        draft.m_name = WideToUtf8(TR(L"机器人"));
    const int64_t groupId = (m_bot.m_ownerType == bot_owner_group) ? m_groupId : 0;
    const std::string body = BotCodec::BuildTryRequest(groupId, draft, input);
    m_trySeq = m_owner->SendCommand(m_clientId, bot_cmd_try, body);
    if (m_trySeq == 0)
    {
        SetTryResult(m_controls, NotSentText(), true, Font());
        return;
    }
    SetBusy(m_saveSeq != 0, true);
    SetTryResult(m_controls, TR(L"正在判定…"), false, Font());
    LOG_INFO("bot try sent, botid=%lld, seq=%u.", static_cast<long long>(m_bot.m_botId), static_cast<unsigned>(m_trySeq));
}

void CBotEditWindow::Save()
{
    if (m_owner == NULL || m_saveSeq != 0)
        return;
    HarvestBasics();
    std::wstring why;
    if (ValidateBot(m_bot, m_allowExternal, &why) != bot_check_ok)
    {
        SetStatusText(m_controls.m_frame.m_status, why, true);
        return;
    }
    const bool group = (m_bot.m_ownerType == bot_owner_group);
    const uint16_t cmd = group ? static_cast<uint16_t>(bot_cmd_save_group_bot) : static_cast<uint16_t>(bot_cmd_save_my_bot);
    const std::string body = group ? BotCodec::BuildSaveGroupBotRequest(m_groupId, m_bot) : BotCodec::BuildSaveMyBotRequest(m_bot);
    m_saveSeq = m_owner->SendCommand(m_clientId, cmd, body);
    if (m_saveSeq == 0)
    {
        SetStatusText(m_controls.m_frame.m_status, NotSentText(), true);
        return;
    }
    SetBusy(true, m_trySeq != 0);
    SetStatusText(m_controls.m_frame.m_status, TR(L"正在保存…"), false);
    LOG_INFO("bot save sent, botid=%lld, cmd=%u, rules=%u, seq=%u.", static_cast<long long>(m_bot.m_botId),
             static_cast<unsigned>(cmd), static_cast<unsigned>(m_bot.m_rules.size()), static_cast<unsigned>(m_saveSeq));
}

bool CBotEditWindow::IsDirty()
{
    if (!IsOpen())
        return false;
    HarvestBasics();
    return BotCodec::BotToSaveJson(m_bot) != m_original;
}

void CBotEditWindow::RequestClose()
{
    if (!IsOpen())
        return;
    if (m_ruleWindow.IsOpen())
    {
        m_ruleWindow.BringToFront();
        return;
    }
    if (IsDirty())
    {
        if (::MessageBoxW(m_hWnd, TR(kConfirmDiscard), WindowTitle().c_str(),
                          MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
            return;
    }
    CloseByUser();
}

void CBotEditWindow::CloseByUser()
{
    m_ruleWindow.Close();
    if (!IsWindow())
        return;
    RestoreOwner();
    DestroyWindow();
}

std::wstring CBotEditWindow::WindowTitle() const
{
    const std::wstring name = Utf8ToWide(m_bot.m_name);
    if (m_bot.m_ownerType == bot_owner_personal)
        return (m_bot.m_botId == 0) ? std::wstring(TR(L"新建个人机器人")) : TRF(L"编辑个人机器人 · {0}", name);
    return (m_bot.m_botId == 0) ? std::wstring(TR(L"新建群机器人")) : TRF(L"编辑群机器人 · {0}", name);
}

void CBotEditWindow::SetBusy(bool saving, bool trying)
{
    if (m_controls.m_save != NULL)
        m_controls.m_save->SetEnabled(!saving);
    if (m_controls.m_tryButton != NULL)
        m_controls.m_tryButton->SetEnabled(!trying);
}

} // namespace flamingobot
