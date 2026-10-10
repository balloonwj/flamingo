/**
 *  规则编辑窗，见 BotRuleWindow.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotRuleWindow.h"

#include "../Core/BotRules.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/Log/IULog.h"

namespace flamingobot {

namespace {

//「已选」里最多写出几个名字，其余写成「等 N 人」
const size_t kMaxShownNames = 5;

} // namespace

CBotRuleWindow::CBotRuleWindow(IBotWindowOwner* owner, IBotRuleHost* host)
    : m_owner(owner)
    , m_host(host)
    , m_clientId(0)
    , m_pickRequest(0)
    , m_index(-1)
    , m_ownerType(bot_owner_group)
    , m_allowExternal(false)
    , m_closingByHost(false)
{
}

CBotRuleWindow::~CBotRuleWindow()
{
    Close();
}

bool CBotRuleWindow::Open(HWND ownerWnd, int index, const BotRule& rule, int ownerType, bool allowExternal)
{
    if (IsOpen())
    {
        BringToFront();
        return false;
    }
    m_index = index;
    m_ownerType = ownerType;
    m_allowExternal = allowExternal;
    m_form = RuleToForm(rule);
    NormalizeForm(m_form, m_ownerType, m_allowExternal);
    m_pickRequest = 0;
    const std::wstring title = (index < 0) ? std::wstring(TR(L"添加规则")) : std::wstring(TR(L"编辑规则"));
    if (!CreateFramed(ownerWnd, title, false))
        return false;
    if (m_owner != NULL)
        m_clientId = m_owner->RegisterClient(this);
    std::unique_ptr<balloonwjui::DuiVBox> body = BuildRuleWindowBody(Font(), m_frame);
    Rebuild();
    FinishFramed(std::unique_ptr<balloonwjui::DuiControl>(body.release()), kRuleWindowWidth, kRuleWindowHeight, ownerWnd);
    ShowWindow(SW_SHOW);
    DisableOwner(ownerWnd);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("bot rule window opened, index=%d, trigger=%d.", index, m_form.m_trigger);
    return true;
}

void CBotRuleWindow::Close()
{
    if (!IsWindow())
        return;
    m_closingByHost = true;
    RestoreOwner();
    DestroyWindow();
    m_closingByHost = false;
}

bool CBotRuleWindow::IsOpen() const
{
    return IsWindow() != FALSE;
}

void CBotRuleWindow::OnBotReply(const BotReply& reply)
{
    //规则编辑窗不发 2802～2809 命令，不会有应答交到这里
    (void)reply;
}

void CBotRuleWindow::OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds)
{
    if (!IsOpen() || requestId != m_pickRequest)
        return;
    m_pickRequest = 0;
    HarvestRuleForm(m_controls, m_form);
    m_form.m_senderKind = bot_sender_users;
    m_form.m_senderUsers = userIds;
    Rebuild();
    BringToFront();
}

void CBotRuleWindow::OnDepartmentsPicked(uint32_t requestId, const std::vector<int64_t>& deptIds)
{
    if (!IsOpen() || requestId != m_pickRequest)
        return;
    m_pickRequest = 0;
    HarvestRuleForm(m_controls, m_form);
    m_form.m_senderKind = bot_sender_depts;
    m_form.m_senderDepts = deptIds;
    Rebuild();
    BringToFront();
}

LRESULT CBotRuleWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    CloseByUser();
    return 0;
}

LRESULT CBotRuleWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    RestoreOwner();
    if (m_owner != NULL && m_clientId != 0)
        m_owner->UnregisterClient(m_clientId);
    m_clientId = 0;
    m_pickRequest = 0;
    m_frame = ScrollFrameControls();
    m_controls = RuleFormControls();
    if (!m_closingByHost && m_host != NULL)
        m_host->OnRuleWindowClosed();
    return 0;
}

LRESULT CBotRuleWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //换了触发方式、动作、定时种类、发送者范围或勾了「时间段」：重建表单（通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (pNotify->ctrlId == kIdRuleTrigger || pNotify->ctrlId == kIdRuleAction || pNotify->ctrlId == kIdRuleScheduleKind
            || pNotify->ctrlId == kIdRuleSenderKind || pNotify->ctrlId == kIdRuleWindowOn))
    {
        PostMessage(kMsgBotRuleDeferred, rule_defer_rebuild, 0);
        return 0;
    }
    //「选择…」
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdRulePickSenders)
    {
        PostMessage(kMsgBotRuleDeferred, rule_defer_pick, 0);
        return 0;
    }
    //「确定」：成功时会关闭窗口，延后到点击通知处理完再做（UI 约定第 13 条）
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdRuleOk)
    {
        PostMessage(kMsgBotRuleDeferred, rule_defer_ok, 0);
        return 0;
    }
    //「取消」
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdRuleCancel)
    {
        PostMessage(kMsgBotRuleDeferred, rule_defer_cancel, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBotRuleWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    if (!IsOpen())
        return 0;
    switch (static_cast<int>(wParam))
    {
    //重建表单
    case rule_defer_rebuild:
        HarvestRuleForm(m_controls, m_form);
        Rebuild();
        break;
    //弹出选择联系人对话框
    case rule_defer_pick:
        PickSenders();
        break;
    //校验并交回
    case rule_defer_ok:
        Submit();
        break;
    //取消
    case rule_defer_cancel:
        CloseByUser();
        break;
    //不认识的动作
    default:
        break;
    }
    return 0;
}

void CBotRuleWindow::Rebuild()
{
    if (m_frame.m_content == NULL)
        return;
    NormalizeForm(m_form, m_ownerType, m_allowExternal);
    RuleFormContext ctx;
    ctx.m_ownerType = m_ownerType;
    ctx.m_allowExternal = m_allowExternal;
    ctx.m_senderNames = SenderNamesText();
    RemoveAllChildren(m_frame.m_content);
    BuildRuleForm(m_frame.m_content, m_form, ctx, Font(), kRuleContentWidth, m_controls);
    RelayoutScroll(m_frame.m_scroll);
}

void CBotRuleWindow::PickSenders()
{
    if (m_owner == NULL)
        return;
    HarvestRuleForm(m_controls, m_form);
    //按当前选的发送者范围弹出对应的选择对话框
    if (m_form.m_senderKind == bot_sender_depts)
    {
        m_pickRequest = m_owner->PickDepartments(m_clientId, m_hWnd, m_form.m_senderDepts);
        if (m_pickRequest == 0)
            SetStatusText(m_frame.m_status, TR(L"暂时无法打开选择部门的窗口（组织架构还没有加载完，或没有连上服务器）。"), true);
        else
            SetStatusText(m_frame.m_status, std::wstring(), false);
        return;
    }
    m_pickRequest = m_owner->PickContacts(m_clientId, m_hWnd, m_form.m_senderUsers);
    if (m_pickRequest == 0)
        SetStatusText(m_frame.m_status, TR(L"暂时无法打开选择联系人的窗口（通讯录还没有加载完，或没有连上服务器）。"), true);
    else
        SetStatusText(m_frame.m_status, std::wstring(), false);
}

void CBotRuleWindow::Submit()
{
    HarvestRuleForm(m_controls, m_form);
    BotRule rule;
    std::wstring why;
    if (!FormToRule(m_form, m_ownerType, m_allowExternal, rule, &why))
    {
        SetStatusText(m_frame.m_status, why, true);
        LOG_INFO("bot rule rejected locally, trigger=%d, action=%d.", m_form.m_trigger, m_form.m_action);
        return;
    }
    LOG_INFO("bot rule edited, index=%d, trigger=%d, action=%d.", m_index, rule.m_trigger.m_type, rule.m_action.m_type);
    if (m_host != NULL)
        m_host->OnRuleEdited(m_index, rule);
    CloseByUser();
}

void CBotRuleWindow::CloseByUser()
{
    if (!IsWindow())
        return;
    RestoreOwner();
    DestroyWindow();
}

std::wstring CBotRuleWindow::SenderNamesText()
{
    if (m_owner == NULL)
        return std::wstring();
    std::vector<std::wstring> names;
    //指定的部门：显示部门名称；其余显示指定的人
    if (m_form.m_senderKind == bot_sender_depts)
    {
        for (size_t i = 0; i < m_form.m_senderDepts.size(); ++i)
            names.push_back(m_owner->DeptName(m_form.m_senderDepts[i]));
    }
    else
    {
        for (size_t i = 0; i < m_form.m_senderUsers.size(); ++i)
            names.push_back(m_owner->UserName(m_form.m_senderUsers[i]));
    }
    if (names.empty())
        return std::wstring();
    return JoinNames(names, kMaxShownNames);
}

} // namespace flamingobot
