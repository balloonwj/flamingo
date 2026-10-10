/**
 *  「我的机器人」窗口，见 MyBotsWindow.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "MyBotsWindow.h"

#include "../Core/BotCodec.h"
#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/Log/IULog.h"

using balloonwjui::DuiButton;

namespace flamingobot {

namespace {

//确认框的正文（文件作用域的字符串只做标记，使用处再调 TR）
const wchar_t* const kConfirmDelete = TR_NOOP(L"确定删除机器人「{0}」吗？删除后它不再代答，规则不能恢复。");

//一行按钮的 id
UINT RowButtonId(size_t index, int button)
{
    return kIdMyBotsRowFirst + static_cast<UINT>(index) * kMyBotsRowIdStride + static_cast<UINT>(button);
}

//从按钮 id 解出下标与按钮；不是列表按钮时返回 false
bool ParseRowButtonId(UINT id, size_t& index, int& button)
{
    if (id < kIdMyBotsRowFirst || id > kIdMyBotsRowLast)
        return false;
    index = (id - kIdMyBotsRowFirst) / kMyBotsRowIdStride;
    button = static_cast<int>((id - kIdMyBotsRowFirst) % kMyBotsRowIdStride);
    return true;
}

} // namespace

CMyBotsWindow::CMyBotsWindow(IBotWindowOwner* owner)
    : m_owner(owner)
    , m_clientId(0)
    , m_loaded(false)
    , m_allowed(false)
    , m_max(kPersonalBotMax)
    , m_loadSeq(0)
    , m_editor(owner, this)
    , m_closingByHost(false)
{
}

CMyBotsWindow::~CMyBotsWindow()
{
    Close();
}

//======================== IBotTopWindow ========================

bool CMyBotsWindow::Open(HWND owner)
{
    if (IsWindow())
    {
        if (m_editor.IsOpen())
            m_editor.BringToFront();
        else
            BringToFront();
        return true;
    }
    if (!CreateFramed(owner, TR(L"我的机器人"), true))
        return false;
    if (m_owner != NULL)
        m_clientId = m_owner->RegisterClient(this);
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec create;
    create.m_text = TR(L"新建机器人");
    create.m_id = kIdMyBotsNew;
    create.m_variant = DuiButton::Variant::Primary;
    buttons.push_back(create);
    FooterButtonSpec close;
    close.m_text = TR(L"关闭");
    close.m_id = kIdMyBotsClose;
    close.m_variant = DuiButton::Variant::Default;
    buttons.push_back(close);
    std::unique_ptr<balloonwjui::DuiVBox> body = BuildScrollFrame(Font(), buttons, m_frame);
    FinishFramed(std::unique_ptr<balloonwjui::DuiControl>(body.release()), kListWindowWidth, kListWindowHeight, owner);
    m_loaded = false;
    m_bots.clear();
    m_opSeqs.clear();
    Reload();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CMyBotsWindow::Close()
{
    m_editor.Close();
    if (!IsWindow())
        return;
    m_closingByHost = true;
    DestroyWindow();
    m_closingByHost = false;
}

void CMyBotsWindow::SetGroupName(const std::wstring& groupName)
{
    //「我的机器人」窗口与群无关
    (void)groupName;
}

void CMyBotsWindow::OnGroupBotsChanged()
{
    //「我的机器人」窗口与群无关
}

void CMyBotsWindow::OnFeaturesChanged()
{
    if (IsWindow())
        RebuildList();
}

void CMyBotsWindow::OnReconnected()
{
    if (!IsWindow())
        return;
    m_loadSeq = 0;
    m_opSeqs.clear();
    Reload();
}

//======================== 应答 ========================

void CMyBotsWindow::OnBotReply(const BotReply& reply)
{
    if (!IsWindow())
        return;
    //个人机器人列表
    if (reply.m_cmd == bot_cmd_get_my_bots && reply.m_seq == m_loadSeq)
    {
        m_loadSeq = 0;
        if (reply.m_parsed && reply.m_code == bot_error_ok)
        {
            m_loaded = true;
            m_allowed = reply.m_allowed;
            m_max = (reply.m_max > 0) ? reply.m_max : kPersonalBotMax;
            m_bots = reply.m_bots;
            SetStatusText(m_frame.m_status, std::wstring(), false);
            LOG_INFO("my bots loaded, count=%u, allowed=%d.", static_cast<unsigned>(m_bots.size()), m_allowed ? 1 : 0);
        }
        else
        {
            SetStatusText(m_frame.m_status, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true);
        }
        RebuildList();
        return;
    }
    //停用 / 启用、删除
    std::map<uint32_t, int64_t>::iterator op = m_opSeqs.find(reply.m_seq);
    if (op == m_opSeqs.end())
        return;
    m_opSeqs.erase(op);
    if (reply.m_parsed && reply.m_code == bot_error_ok)
    {
        SetStatusText(m_frame.m_status, (reply.m_cmd == bot_cmd_delete_my_bot) ? TR(L"已删除。") : TR(L"已更新。"), false);
        Reload();
        return;
    }
    SetStatusText(m_frame.m_status, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true);
    RebuildList();
}

void CMyBotsWindow::OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds)
{
    //本窗口不选人（选人由规则编辑窗发起）
    (void)requestId;
    (void)userIds;
}

void CMyBotsWindow::OnBotSaved(const BotRecord& bot)
{
    SetStatusText(m_frame.m_status, TRF(L"已保存「{0}」。", Utf8ToWide(bot.m_name)), false);
    Reload();
}

void CMyBotsWindow::OnEditWindowClosed()
{
    BringToFront();
}

//======================== 消息 ========================

LRESULT CMyBotsWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    CloseByUser();
    return 0;
}

LRESULT CMyBotsWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    m_editor.Close();
    if (m_owner != NULL && m_clientId != 0)
        m_owner->UnregisterClient(m_clientId);
    m_clientId = 0;
    m_loadSeq = 0;
    m_opSeqs.clear();
    m_frame = ScrollFrameControls();
    if (!m_closingByHost && m_owner != NULL)
    {
        LOG_INFO("my bots window closed by user.");
        m_owner->OnTopWindowClosed(bot_window_my_bots, 0);
    }
    return 0;
}

LRESULT CMyBotsWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    size_t index = 0;
    int button = 0;
    //列表一行上的按钮：会重建列表（按钮自己就在列表里），延后执行（UI 约定第 13 条）
    if (ParseRowButtonId(pNotify->ctrlId, index, button))
    {
        static const int kDeferOf[] = { mybots_defer_edit, mybots_defer_toggle, mybots_defer_delete, 0 };
        if (kDeferOf[button] != 0)
            PostMessage(kMsgBotMyBotsDeferred, static_cast<WPARAM>(kDeferOf[button]), static_cast<LPARAM>(index));
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //「新建机器人」
    case kIdMyBotsNew:
        PostMessage(kMsgBotMyBotsDeferred, mybots_defer_new, 0);
        return 0;
    //「关闭」
    case kIdMyBotsClose:
        PostMessage(kMsgBotMyBotsDeferred, mybots_defer_close, 0);
        return 0;
    //其它控件的单击不处理
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CMyBotsWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (!IsWindow())
        return 0;
    const size_t index = static_cast<size_t>(lParam);
    switch (static_cast<int>(wParam))
    {
    //编辑
    case mybots_defer_edit:
        EditBot(index);
        break;
    //停用 / 启用
    case mybots_defer_toggle:
        ToggleBot(index);
        break;
    //删除
    case mybots_defer_delete:
        DeleteBot(index);
        break;
    //新建
    case mybots_defer_new:
        NewBot();
        break;
    //关闭
    case mybots_defer_close:
        CloseByUser();
        break;
    //不认识的动作
    default:
        break;
    }
    return 0;
}

//======================== 操作 ========================

void CMyBotsWindow::Reload()
{
    if (m_owner == NULL || m_loadSeq != 0)
        return;
    m_loadSeq = m_owner->SendCommand(m_clientId, bot_cmd_get_my_bots, BotCodec::BuildGetMyBotsRequest());
    if (m_loadSeq == 0)
        SetStatusText(m_frame.m_status, NotSentText(), true);
    RebuildList();
}

bool CMyBotsWindow::Allowed() const
{
    const BotFeatures features = (m_owner != NULL) ? m_owner->Features() : BotFeatures();
    return m_allowed && features.m_personal;
}

void CMyBotsWindow::RebuildList()
{
    balloonwjui::DuiVBox* content = m_frame.m_content;
    if (content == NULL)
        return;
    HFONT font = Font();
    const bool allowed = Allowed();
    RemoveAllChildren(content);
    AddListNote(content,
                TRF(L"个人机器人只在别人发给你的单聊里、满足条件时以你的身份代答，对方会看到「机器人代答」的标记。每人最多 {0} 个。",
                    m_max),
                kBotSubText, font, kListContentWidth);
    AddListNote(content, TR(L"「离开 / 忙碌自动回复」在系统设置的「聊天」页设置，不在这里。"), kBotSubText, font,
                kListContentWidth);
    if (!m_loaded)
    {
        AddListNote(content, (m_loadSeq != 0) ? TR(L"正在读取你的机器人…") : TR(L"没有读取到你的机器人。"), kBotFaint, font,
                    kListContentWidth);
    }
    else
    {
        if (!allowed)
            AddListNote(content, TR(L"企业关闭了个人机器人：暂时不能新建、修改或启用，已有的机器人也不会代答。"), kBotError,
                        font, kListContentWidth);
        AddListSection(content, TRF(L"我的机器人（{0} / {1}）", static_cast<int>(m_bots.size()), m_max), font);
        if (m_bots.empty())
            AddListNote(content, allowed ? TR(L"还没有个人机器人，点「新建机器人」开始。") : TR(L"还没有个人机器人。"),
                        kBotFaint, font, kListContentWidth);
        for (size_t i = 0; i < m_bots.size(); ++i)
        {
            const BotRecord& bot = m_bots[i];
            bool busy = false;
            for (std::map<uint32_t, int64_t>::const_iterator it = m_opSeqs.begin(); it != m_opSeqs.end(); ++it)
            {
                if (it->second == bot.m_botId)
                    busy = true;
            }
            const bool active = (bot.m_status == bot_status_active);
            BotRowView row;
            row.m_name = Utf8ToWide(bot.m_name);
            row.m_glyph = Utf8ToWide(bot.m_glyph);
            row.m_color = Utf8ToWide(bot.m_color);
            row.m_state = active ? TR(L"已启用") : TR(L"已停用");
            row.m_stateColor = (active && allowed) ? kBotOk : kBotFaint;
            row.m_desc = Utf8ToWide(bot.m_desc);
            const std::wstring usage = UsageText(bot);
            row.m_detail = TRF(L"共 {0} 条规则", static_cast<int>(bot.m_rules.size()));
            if (!usage.empty())
                row.m_detail += L"\n" + usage;
            row.m_buttons.push_back(BotRowButton(TR(L"编辑"), RowButtonId(i, mybots_row_edit), allowed && !busy));
            row.m_buttons.push_back(BotRowButton(active ? TR(L"停用") : TR(L"启用"), RowButtonId(i, mybots_row_toggle),
                                                 allowed && !busy));
            row.m_buttons.push_back(BotRowButton(TR(L"删除"), RowButtonId(i, mybots_row_delete), !busy));
            AddBotRow(content, row, font, kListContentWidth);
        }
    }
    if (!m_frame.m_buttons.empty() && m_frame.m_buttons[0] != NULL)
        m_frame.m_buttons[0]->SetEnabled(m_loaded && allowed && static_cast<int>(m_bots.size()) < m_max);
    RelayoutScroll(m_frame.m_scroll);
}

void CMyBotsWindow::EditBot(size_t index)
{
    if (index >= m_bots.size() || !Allowed())
        return;
    if (m_editor.IsOpen())
    {
        m_editor.BringToFront();
        return;
    }
    m_editor.Open(m_hWnd, m_bots[index], 0, false);
}

void CMyBotsWindow::ToggleBot(size_t index)
{
    if (index >= m_bots.size() || m_owner == NULL || !Allowed())
        return;
    //启用 / 停用没有单独的命令：改 status 后用 2807 保存整份
    BotRecord bot = m_bots[index];
    bot.m_status = (bot.m_status == bot_status_active) ? bot_status_disabled : bot_status_active;
    const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_save_my_bot, BotCodec::BuildSaveMyBotRequest(bot));
    if (seq == 0)
    {
        SetStatusText(m_frame.m_status, NotSentText(), true);
        return;
    }
    m_opSeqs[seq] = bot.m_botId;
    SetStatusText(m_frame.m_status, TR(L"正在提交…"), false);
    LOG_INFO("my bot status change sent, botid=%lld, status=%d.", static_cast<long long>(bot.m_botId), bot.m_status);
    RebuildList();
}

void CMyBotsWindow::DeleteBot(size_t index)
{
    if (index >= m_bots.size() || m_owner == NULL)
        return;
    const BotRecord bot = m_bots[index];
    const std::wstring text = TRF(kConfirmDelete, Utf8ToWide(bot.m_name));
    if (::MessageBoxW(m_hWnd, text.c_str(), TR(L"我的机器人"), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
        return;
    const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_delete_my_bot, BotCodec::BuildDeleteMyBotRequest(bot.m_botId));
    if (seq == 0)
    {
        SetStatusText(m_frame.m_status, NotSentText(), true);
        return;
    }
    m_opSeqs[seq] = bot.m_botId;
    SetStatusText(m_frame.m_status, TR(L"正在删除…"), false);
    LOG_INFO("my bot delete sent, botid=%lld.", static_cast<long long>(bot.m_botId));
    RebuildList();
}

void CMyBotsWindow::NewBot()
{
    if (!Allowed() || static_cast<int>(m_bots.size()) >= m_max)
        return;
    if (m_editor.IsOpen())
    {
        m_editor.BringToFront();
        return;
    }
    BotRecord bot;
    bot.m_ownerType = bot_owner_personal;
    bot.m_ownerId = (m_owner != NULL) ? m_owner->MyUserId() : 0;
    bot.m_status = bot_status_active;
    m_editor.Open(m_hWnd, bot, 0, false);
}

void CMyBotsWindow::CloseByUser()
{
    m_editor.Close();
    if (IsWindow())
        DestroyWindow();
}

} // namespace flamingobot
