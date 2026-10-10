/**
 *  群机器人管理窗，见 BotManagerWindow.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotManagerWindow.h"

#include "../Core/BotCodec.h"
#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/Log/IULog.h"

using balloonwjui::DuiButton;

namespace flamingobot {

namespace {

//确认框的正文（文件作用域的字符串只做标记，使用处再调 TR）
const wchar_t* const kConfirmDelete = TR_NOOP(L"确定删除机器人「{0}」吗？删除后群里会出现一条提示，它的规则不能恢复。");

//一行按钮的 id
UINT RowButtonId(size_t index, int button)
{
    return kIdManagerRowFirst + static_cast<UINT>(index) * kManagerRowIdStride + static_cast<UINT>(button);
}

//从按钮 id 解出下标与按钮；不是列表按钮时返回 false
bool ParseRowButtonId(UINT id, size_t& index, int& button)
{
    if (id < kIdManagerRowFirst || id > kIdManagerRowLast)
        return false;
    index = (id - kIdManagerRowFirst) / kManagerRowIdStride;
    button = static_cast<int>((id - kIdManagerRowFirst) % kManagerRowIdStride);
    return true;
}

} // namespace

CBotManagerWindow::CBotManagerWindow(int64_t groupId, const std::wstring& groupName, IBotWindowOwner* owner)
    : m_groupId(groupId)
    , m_groupName(groupName)
    , m_owner(owner)
    , m_clientId(0)
    , m_hasList(false)
    , m_pendingEditId(0)
    , m_editor(owner, this)
    , m_closingByHost(false)
{
}

CBotManagerWindow::~CBotManagerWindow()
{
    Close();
}

//======================== IBotTopWindow ========================

bool CBotManagerWindow::Open(HWND owner)
{
    if (IsWindow())
    {
        //编辑窗开着时提到前面的是编辑窗（管理窗此时被禁用）
        if (m_editor.IsOpen())
            m_editor.BringToFront();
        else
            BringToFront();
        return true;
    }
    if (!CreateFramed(owner, WindowTitle(), true))
        return false;
    if (m_owner != NULL)
        m_clientId = m_owner->RegisterClient(this);
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec refresh;
    refresh.m_text = TR(L"刷新");
    refresh.m_id = kIdManagerRefresh;
    refresh.m_variant = DuiButton::Variant::Default;
    buttons.push_back(refresh);
    FooterButtonSpec create;
    create.m_text = TR(L"新建群机器人");
    create.m_id = kIdManagerNew;
    create.m_variant = DuiButton::Variant::Primary;
    buttons.push_back(create);
    FooterButtonSpec close;
    close.m_text = TR(L"关闭");
    close.m_id = kIdManagerClose;
    close.m_variant = DuiButton::Variant::Default;
    buttons.push_back(close);
    std::unique_ptr<balloonwjui::DuiVBox> body = BuildScrollFrame(Font(), buttons, m_frame);
    FinishFramed(std::unique_ptr<balloonwjui::DuiControl>(body.release()), kListWindowWidth, kListWindowHeight, owner);
    m_details.clear();
    m_detailSeqs.clear();
    m_opSeqs.clear();
    m_pendingEditId = 0;
    Reload();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBotManagerWindow::Close()
{
    m_editor.Close();
    if (!IsWindow())
        return;
    m_closingByHost = true;
    DestroyWindow();
    m_closingByHost = false;
}

void CBotManagerWindow::SetGroupName(const std::wstring& groupName)
{
    if (groupName.empty() || groupName == m_groupName)
        return;
    m_groupName = groupName;
    UpdateTitle(WindowTitle());
}

void CBotManagerWindow::OnGroupBotsChanged()
{
    if (!IsWindow())
        return;
    Reload();
}

void CBotManagerWindow::OnFeaturesChanged()
{
    if (!IsWindow())
        return;
    RebuildList();
}

void CBotManagerWindow::OnReconnected()
{
    if (!IsWindow())
        return;
    //断线前发出的请求已作废：清掉登记，重新拉列表与详情
    m_detailSeqs.clear();
    m_opSeqs.clear();
    m_pendingEditId = 0;
    m_details.clear();
    if (m_owner != NULL)
        m_owner->RequestGroupBots(m_groupId);
    Reload();
}

//======================== 应答 ========================

void CBotManagerWindow::OnBotReply(const BotReply& reply)
{
    if (!IsWindow())
        return;
    //机器人详情
    std::map<uint32_t, int64_t>::iterator detail = m_detailSeqs.find(reply.m_seq);
    if (reply.m_cmd == bot_cmd_get_detail && detail != m_detailSeqs.end())
    {
        const int64_t botId = detail->second;
        m_detailSeqs.erase(detail);
        if (reply.m_parsed && reply.m_code == bot_error_ok && reply.m_hasBot)
        {
            m_details[botId] = reply.m_bot;
            if (m_pendingEditId == botId)
            {
                m_pendingEditId = 0;
                SetStatusText(m_frame.m_status, std::wstring(), false);
                OpenEditor(reply.m_bot);
            }
        }
        else
        {
            LOG_WARNING("bot detail failed, groupid=%lld, botid=%lld, code=%d.", static_cast<long long>(m_groupId),
                        static_cast<long long>(botId), reply.m_code);
            if (m_pendingEditId == botId)
            {
                m_pendingEditId = 0;
                SetStatusText(m_frame.m_status, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true);
            }
        }
        RebuildList();
        return;
    }
    //删除、在本群关闭 / 打开、停用 / 启用
    std::map<uint32_t, int64_t>::iterator op = m_opSeqs.find(reply.m_seq);
    if (op == m_opSeqs.end())
        return;
    m_opSeqs.erase(op);
    if (reply.m_parsed && reply.m_code == bot_error_ok)
    {
        SetStatusText(m_frame.m_status, (reply.m_cmd == bot_cmd_delete_group_bot) ? TR(L"已删除。") : TR(L"已更新。"),
                      false);
        if (reply.m_cmd == bot_cmd_delete_group_bot)
            m_details.erase(reply.m_botId);
        //服务端随后向群成员推 2820，exe 重拉 2801；这里也请宿主重拉一次，不依赖推送是否到达
        if (m_owner != NULL)
            m_owner->RequestGroupBots(m_groupId);
    }
    else
    {
        SetStatusText(m_frame.m_status, ErrorText(reply.m_parsed ? reply.m_code : bot_error_bad_request), true);
    }
    RebuildList();
}

void CBotManagerWindow::OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds)
{
    //管理窗不选人
    (void)requestId;
    (void)userIds;
}

void CBotManagerWindow::OnBotSaved(const BotRecord& bot)
{
    SetStatusText(m_frame.m_status, TRF(L"已保存「{0}」。", Utf8ToWide(bot.m_name)), false);
    //详情过时了：下次列表刷新时重拉
    m_details.erase(bot.m_botId);
    if (m_owner != NULL)
        m_owner->RequestGroupBots(m_groupId);
}

void CBotManagerWindow::OnEditWindowClosed()
{
    BringToFront();
}

//======================== 消息 ========================

LRESULT CBotManagerWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    CloseByUser();
    return 0;
}

LRESULT CBotManagerWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    m_editor.Close();
    if (m_owner != NULL && m_clientId != 0)
        m_owner->UnregisterClient(m_clientId);
    m_clientId = 0;
    m_frame = ScrollFrameControls();
    m_detailSeqs.clear();
    m_opSeqs.clear();
    if (!m_closingByHost && m_owner != NULL)
    {
        LOG_INFO("bot manager closed by user, groupid=%lld.", static_cast<long long>(m_groupId));
        m_owner->OnTopWindowClosed(bot_window_group_manager, m_groupId);
    }
    return 0;
}

LRESULT CBotManagerWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
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
        static const int kDeferOf[] = { manager_defer_edit, manager_defer_toggle, manager_defer_delete, 0 };
        if (kDeferOf[button] != 0)
            PostMessage(kMsgBotManagerDeferred, static_cast<WPARAM>(kDeferOf[button]), static_cast<LPARAM>(index));
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //「新建群机器人」
    case kIdManagerNew:
        PostMessage(kMsgBotManagerDeferred, manager_defer_new, 0);
        return 0;
    //「刷新」
    case kIdManagerRefresh:
        PostMessage(kMsgBotManagerDeferred, manager_defer_refresh, 0);
        return 0;
    //「关闭」
    case kIdManagerClose:
        PostMessage(kMsgBotManagerDeferred, manager_defer_close, 0);
        return 0;
    //其它控件的单击不处理
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CBotManagerWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (!IsWindow())
        return 0;
    const size_t index = static_cast<size_t>(lParam);
    switch (static_cast<int>(wParam))
    {
    //编辑
    case manager_defer_edit:
        EditBot(index);
        break;
    //在本群关闭 / 打开、停用 / 启用
    case manager_defer_toggle:
        ToggleBot(index);
        break;
    //删除
    case manager_defer_delete:
        DeleteBot(index);
        break;
    //新建
    case manager_defer_new:
        NewBot();
        break;
    //刷新：请宿主重拉 2801，结果到达后经 OnGroupBotsChanged 重读
    case manager_defer_refresh:
        m_details.clear();
        if (m_owner != NULL)
            m_owner->RequestGroupBots(m_groupId);
        SetStatusText(m_frame.m_status, TR(L"正在刷新…"), false);
        break;
    //关闭
    case manager_defer_close:
        CloseByUser();
        break;
    //不认识的动作
    default:
        break;
    }
    return 0;
}

//======================== 操作 ========================

void CBotManagerWindow::Reload()
{
    if (m_owner == NULL)
        return;
    m_hasList = m_owner->GetGroupBots(m_groupId, m_list);
    if (!m_hasList)
    {
        m_owner->RequestGroupBots(m_groupId);
        LOG_INFO("bot manager waits for group bots from host, groupid=%lld.", static_cast<long long>(m_groupId));
    }
    else
    {
        RequestDetails();
        if (m_frame.m_status != NULL && m_opSeqs.empty() && m_pendingEditId == 0)
            SetStatusText(m_frame.m_status, std::wstring(), false);
    }
    RebuildList();
}

void CBotManagerWindow::RequestDetails()
{
    if (m_owner == NULL || !m_list.m_caps.m_isOwner)
        return;
    for (size_t i = 0; i < m_list.m_bots.size(); ++i)
    {
        const GroupBotItem& item = m_list.m_bots[i];
        if (!item.m_isGroupBot || m_details.find(item.m_botId) != m_details.end())
            continue;
        bool pending = false;
        for (std::map<uint32_t, int64_t>::const_iterator it = m_detailSeqs.begin(); it != m_detailSeqs.end(); ++it)
        {
            if (it->second == item.m_botId)
                pending = true;
        }
        if (pending)
            continue;
        const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_get_detail, BotCodec::BuildGetDetailRequest(item.m_botId));
        if (seq != 0)
            m_detailSeqs[seq] = item.m_botId;
    }
}

void CBotManagerWindow::RebuildList()
{
    balloonwjui::DuiVBox* content = m_frame.m_content;
    if (content == NULL)
        return;
    HFONT font = Font();
    const BotFeatures features = (m_owner != NULL) ? m_owner->Features() : BotFeatures();
    const GroupBotCaps& caps = m_list.m_caps;
    const bool owner = m_hasList && caps.m_isOwner;
    const bool groupBotAllowed = caps.m_groupBotAllowed && features.m_groupBot;
    RemoveAllChildren(content);

    if (!m_hasList)
    {
        AddListNote(content, TR(L"正在读取本群的机器人…"), kBotSubText, font, kListContentWidth);
    }
    else
    {
        //说明：权限与上限
        if (!caps.m_isOwner)
            AddListNote(content, TR(L"只有群主能管理群机器人，你可以在这里查看本群有哪些机器人。"), kBotSubText, font,
                        kListContentWidth);
        else if (!groupBotAllowed)
            AddListNote(content, TR(L"企业没有开放群机器人：不能新建或修改群机器人，已有的可以停用或删除。"), kBotError, font,
                        kListContentWidth);
        else
            AddListNote(content, TRF(L"每个群最多 {0} 个群机器人。添加、删除机器人或在本群关闭企业机器人时，群里会出现一条提示。",
                                     caps.m_groupBotMax),
                        kBotSubText, font, kListContentWidth);

        //企业机器人
        std::vector<size_t> enterprise;
        std::vector<size_t> group;
        for (size_t i = 0; i < m_list.m_bots.size(); ++i)
        {
            if (m_list.m_bots[i].m_isGroupBot)
                group.push_back(i);
            else
                enterprise.push_back(i);
        }
        AddListSection(content, TRF(L"企业机器人（{0}）", static_cast<int>(enterprise.size())), font);
        if (enterprise.empty())
            AddListNote(content, TR(L"本群没有企业机器人。"), kBotFaint, font, kListContentWidth);
        for (size_t k = 0; k < enterprise.size(); ++k)
        {
            const size_t i = enterprise[k];
            const GroupBotItem& item = m_list.m_bots[i];
            BotRowView row;
            row.m_name = item.m_name;
            row.m_glyph = item.m_glyph;
            row.m_color = item.m_color;
            row.m_state = GroupBotStateText(item);
            row.m_stateColor = item.m_active ? kBotOk : kBotFaint;
            row.m_desc = item.m_desc;
            row.m_detail = item.m_canOff ? TR(L"来自企业，管理员允许在本群关闭。") : TR(L"来自企业，管理员不允许在本群关闭。");
            if (owner && item.m_canOff)
            {
                row.m_buttons.push_back(BotRowButton(item.m_off ? TR(L"在本群打开") : TR(L"在本群关闭"),
                                                     RowButtonId(i, manager_row_toggle), !IsBusy(item.m_botId)));
            }
            AddBotRow(content, row, font, kListContentWidth);
        }

        //本群的群机器人
        AddListSection(content, TRF(L"本群的群机器人（{0} / {1}）", static_cast<int>(group.size()), caps.m_groupBotMax),
                       font);
        if (group.empty())
            AddListNote(content, owner && groupBotAllowed ? TR(L"还没有群机器人，点「新建群机器人」开始。")
                                                         : TR(L"本群还没有群机器人。"),
                        kBotFaint, font, kListContentWidth);
        for (size_t k = 0; k < group.size(); ++k)
        {
            const size_t i = group[k];
            const GroupBotItem& item = m_list.m_bots[i];
            const bool busy = IsBusy(item.m_botId);
            BotRowView row;
            row.m_name = item.m_name;
            row.m_glyph = item.m_glyph;
            row.m_color = item.m_color;
            row.m_state = GroupBotStateText(item);
            row.m_stateColor = item.m_active ? kBotOk : kBotFaint;
            row.m_desc = item.m_desc;
            std::map<int64_t, BotRecord>::const_iterator detail = m_details.find(item.m_botId);
            if (detail != m_details.end())
            {
                const std::wstring usage = UsageText(detail->second);
                row.m_detail = TRF(L"共 {0} 条规则", static_cast<int>(detail->second.m_rules.size()));
                if (!usage.empty())
                    row.m_detail += L"\n" + usage;
            }
            else if (owner)
            {
                row.m_detail = TR(L"正在读取规则…");
            }
            if (owner)
            {
                row.m_buttons.push_back(BotRowButton(TR(L"编辑"), RowButtonId(i, manager_row_edit), groupBotAllowed && !busy));
                row.m_buttons.push_back(BotRowButton(item.m_status == bot_status_active ? TR(L"停用") : TR(L"启用"),
                                                     RowButtonId(i, manager_row_toggle), !busy));
                row.m_buttons.push_back(BotRowButton(TR(L"删除"), RowButtonId(i, manager_row_delete), !busy));
            }
            AddBotRow(content, row, font, kListContentWidth);
        }

        //外部系统推送：管理窗拿不到对外地址（随 2802 详情下发），这里只说明用法，地址与令牌到各机器人的编辑窗口里看
        if (owner && ExternalAllowed())
        {
            AddListSection(content, TR(L"外部系统推送（Webhook）"), font);
            AddListNote(content,
                        TR(L"企业允许群机器人使用外部服务。外部系统带上令牌向 Webhook 地址发 POST 请求，即可把消息发到本群；每个机器人的 Webhook 地址、令牌与签名密钥在它的编辑窗口里查看。"),
                        kBotSubText, font, kListContentWidth);
        }
    }

    //底部「新建群机器人」：群主、企业允许、数量没满时可用
    if (m_frame.m_buttons.size() > 1 && m_frame.m_buttons[1] != NULL)
        m_frame.m_buttons[1]->SetEnabled(owner && groupBotAllowed && GroupBotCount() < caps.m_groupBotMax);
    RelayoutScroll(m_frame.m_scroll);
}

void CBotManagerWindow::EditBot(size_t index)
{
    if (index >= m_list.m_bots.size() || m_owner == NULL)
        return;
    const GroupBotItem& item = m_list.m_bots[index];
    if (!item.m_isGroupBot)
        return;
    std::map<int64_t, BotRecord>::const_iterator detail = m_details.find(item.m_botId);
    if (detail != m_details.end())
    {
        OpenEditor(detail->second);
        return;
    }
    //还没有详情：先拉，到达后再打开
    m_pendingEditId = item.m_botId;
    const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_get_detail, BotCodec::BuildGetDetailRequest(item.m_botId));
    if (seq == 0)
    {
        m_pendingEditId = 0;
        SetStatusText(m_frame.m_status, NotSentText(), true);
        return;
    }
    m_detailSeqs[seq] = item.m_botId;
    SetStatusText(m_frame.m_status, TR(L"正在读取机器人的规则…"), false);
}

void CBotManagerWindow::ToggleBot(size_t index)
{
    if (index >= m_list.m_bots.size() || m_owner == NULL)
        return;
    const GroupBotItem& item = m_list.m_bots[index];
    //企业机器人：在本群关闭 / 打开；群机器人：启用中则停用，停用中则启用
    const bool off = item.m_isGroupBot ? (item.m_status == bot_status_active) : !item.m_off;
    const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_set_group_bot_off,
                                              BotCodec::BuildSetGroupBotOffRequest(m_groupId, item.m_botId, off));
    if (seq == 0)
    {
        SetStatusText(m_frame.m_status, NotSentText(), true);
        return;
    }
    m_opSeqs[seq] = item.m_botId;
    SetStatusText(m_frame.m_status, TR(L"正在提交…"), false);
    LOG_INFO("bot set off sent, groupid=%lld, botid=%lld, off=%d.", static_cast<long long>(m_groupId),
             static_cast<long long>(item.m_botId), off ? 1 : 0);
    RebuildList();
}

void CBotManagerWindow::DeleteBot(size_t index)
{
    if (index >= m_list.m_bots.size() || m_owner == NULL)
        return;
    const GroupBotItem item = m_list.m_bots[index];
    if (!item.m_isGroupBot)
        return;
    const std::wstring text = TRF(kConfirmDelete, item.m_name);
    if (::MessageBoxW(m_hWnd, text.c_str(), WindowTitle().c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
        return;
    const uint32_t seq = m_owner->SendCommand(m_clientId, bot_cmd_delete_group_bot,
                                              BotCodec::BuildDeleteGroupBotRequest(m_groupId, item.m_botId));
    if (seq == 0)
    {
        SetStatusText(m_frame.m_status, NotSentText(), true);
        return;
    }
    m_opSeqs[seq] = item.m_botId;
    SetStatusText(m_frame.m_status, TR(L"正在删除…"), false);
    LOG_INFO("bot delete sent, groupid=%lld, botid=%lld.", static_cast<long long>(m_groupId),
             static_cast<long long>(item.m_botId));
    RebuildList();
}

void CBotManagerWindow::NewBot()
{
    if (!m_hasList || !m_list.m_caps.m_isOwner)
        return;
    if (GroupBotCount() >= m_list.m_caps.m_groupBotMax)
    {
        SetStatusText(m_frame.m_status, TRF(L"每个群最多 {0} 个群机器人。", m_list.m_caps.m_groupBotMax), true);
        return;
    }
    BotRecord bot;
    bot.m_ownerType = bot_owner_group;
    bot.m_ownerId = m_groupId;
    bot.m_status = bot_status_active;
    OpenEditor(bot);
}

void CBotManagerWindow::OpenEditor(const BotRecord& bot)
{
    if (m_editor.IsOpen())
    {
        m_editor.BringToFront();
        return;
    }
    m_editor.Open(m_hWnd, bot, m_groupId, ExternalAllowed());
}

bool CBotManagerWindow::IsBusy(int64_t botId) const
{
    for (std::map<uint32_t, int64_t>::const_iterator it = m_opSeqs.begin(); it != m_opSeqs.end(); ++it)
    {
        if (it->second == botId)
            return true;
    }
    return false;
}

int CBotManagerWindow::GroupBotCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_list.m_bots.size(); ++i)
    {
        if (m_list.m_bots[i].m_isGroupBot)
            ++count;
    }
    return count;
}

bool CBotManagerWindow::ExternalAllowed() const
{
    const BotFeatures features = (m_owner != NULL) ? m_owner->Features() : BotFeatures();
    return m_list.m_caps.m_externalAllowed && features.m_groupExternal;
}

std::wstring CBotManagerWindow::WindowTitle() const
{
    if (m_groupName.empty())
        return TR(L"群机器人");
    return TRF(L"群机器人 · {0}", m_groupName);
}

void CBotManagerWindow::CloseByUser()
{
    m_editor.Close();
    if (IsWindow())
        DestroyWindow();
}

} // namespace flamingobot
