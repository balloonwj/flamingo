/**
 *  HistoryScopePopup.cpp —— 聊天记录管理器搜索范围弹层的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "HistoryScopePopup.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/Log/IULog.h"
#include "HistoryWindowLayout.h"        // BuildHistoryScopePopupContent / BuildHistoryScopePopupRow / 控件编号
#include "HistoryConversationList.h"    // NormalizeManagerScope / ToggleManagerScopeKey / 文字
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using balloonwjui::DuiControl;

namespace balloonhistory {

namespace {

//去掉首尾空白（空格、制表符、全角空格）
std::wstring TrimWide(const std::wstring& s)
{
    const wchar_t* const kSpaces = L" \t\r\n\x3000";
    const std::wstring::size_type first = s.find_first_not_of(kSpaces);
    if (first == std::wstring::npos)
    {
        return std::wstring();
    }
    const std::wstring::size_type last = s.find_last_not_of(kSpaces);
    return s.substr(first, last - first + 1);
}

} // anonymous namespace

CHistoryScopePopup::CHistoryScopePopup()
    : m_scroll(NULL)
    , m_info(NULL)
    , m_built(false)
    , m_committed(false)
    , m_submitCb(NULL)
    , m_submitUd(NULL)
{
}

CHistoryScopePopup::~CHistoryScopePopup()
{
}

void CHistoryScopePopup::SetSubmitCallback(SubmitCallback cb, void* userdata)
{
    m_submitCb = cb;
    m_submitUd = userdata;
}

void CHistoryScopePopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }
    std::unique_ptr<DuiControl> content = BuildHistoryScopePopupContent();
    DuiControl* root = content.get();   //借用；所有权随即交给弹层
    m_scroll = dynamic_cast<balloonwjui::DuiScrollView*>(root->FindCtrlById(kIdScopePopupScroll));
    m_info = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdScopePopupInfo));
    SetContent(std::move(content));
    m_built = (m_scroll != NULL && m_info != NULL);
    if (!m_built)
    {
        LOG_ERROR("history scope popup: content tree is missing the list or the info label.");
    }
}

void CHistoryScopePopup::OpenBelow(const RECT& anchorScreen, const std::vector<Entry>& entries,
                                   const std::vector<HistoryConversationKey>& selected)
{
    BuildContentOnce();
    if (!m_built)
    {
        return;
    }
    m_entries = entries;
    m_selected = NormalizeManagerScope(selected);
    m_query.clear();
    m_committed = false;

    //上次打开时搜索框里留下的文字清掉（不发通知，免得再触发一次重建）
    DuiControl* content = GetRoot();
    balloonwjui::DuiEdit* search = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(kIdScopePopupSearch)) : NULL;
    if (search != NULL)
    {
        search->SetTextNoNotify(_T(""));
    }

    SetSize(kHistoryScopePopupW, kHistoryScopePopupH);
    SetEdge(balloonwjui::DuiPopupHost::EdgeBelow);
    //owner 传 NULL（与「选日期」弹层同一做法）。原为避免复选框的点击通知被发到消息记录窗口：当时
    //DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，而有所有者的 WS_POPUP 窗口的 GetParent 返回所有者。
    //2026-10-14 起浮层内容的通知先到浮层自身，本类没有处理的才转给所有者（DuiPopupHost::OnPopupNotify，
    //见开发约定文档 UI 约定第 11 条），这一原因已不存在
    Show(anchorScreen, NULL);
    RebuildRows();
    if (search != NULL)
    {
        search->SetFocus();
    }
    LOG_INFO("history scope popup: opened, %u conversations, %u selected.",
             (unsigned)m_entries.size(), (unsigned)m_selected.size());
}

void CHistoryScopePopup::RebuildRows()
{
    if (!m_built)
    {
        return;
    }
    //名字筛选沿用左栏的规则（名字子串、拼音首字母、全拼）
    std::vector<ManagerConversationItem> items;
    items.reserve(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        ManagerConversationItem item;
        item.m_ctype = m_entries[i].m_key.m_ctype;
        item.m_cid = m_entries[i].m_key.m_cid;
        item.m_name = m_entries[i].m_name;
        items.push_back(item);
    }
    const std::vector<HistoryConversationKey> noScope;
    const std::vector<int> shown = FilterManagerConversations(items, kManagerTypeAll, m_query, noScope);

    std::unique_ptr<balloonwjui::DuiVBox> list(new balloonwjui::DuiVBox());
    list->SetGap(0);
    m_rowByCheckId.clear();
    for (size_t k = 0; k < shown.size(); ++k)
    {
        const Entry& e = m_entries[(size_t)shown[k]];
        const UINT checkId = (UINT)(kIdScopePopupCheckBase + k);
        list->AddChild(BuildHistoryScopePopupRow(checkId, IsInManagerScope(m_selected, e.m_key),
                                                 CString(e.m_name.c_str()), e.m_avatar, e.m_key.m_cid),
                       balloonwjui::DuiLayout::Hint().Fixed(kHistoryScopePopupRowH));
        m_rowByCheckId[checkId] = (size_t)shown[k];
    }
    if (shown.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> empty(new balloonwjui::DuiLabel());
        empty->SetText(m_query.empty() ? TR(L"还没有会话") : TR(L"没有匹配的会话"));
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        list->AddChild(std::move(empty), balloonwjui::DuiLayout::Hint().Fixed(kHistoryScopePopupRowH));
    }
    m_scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    m_scroll->SetScrollPos(0);
    m_scroll->ForceLayout(m_scroll->GetRect());
    m_scroll->Invalidate();
    UpdateSelectedInfo();
}

void CHistoryScopePopup::UpdateSelectedInfo()
{
    if (m_info == NULL)
    {
        return;
    }
    const std::wstring text = BuildManagerScopeSelectedText(m_selected.size());
    m_info->SetText(text.c_str());
    m_info->Invalidate();
}

void CHistoryScopePopup::DoSubmit(const std::vector<HistoryConversationKey>& keys)
{
    if (m_committed)
    {
        return;
    }
    //先置位再回调：回调里宿主会重填左栏，期间若再有点击送来，不会重复提交
    m_committed = true;
    LOG_INFO("history scope popup: submit %u conversations.", (unsigned)keys.size());
    if (m_submitCb != NULL)
    {
        m_submitCb(m_submitUd, keys);
    }
    Hide();
}

LRESULT CHistoryScopePopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //搜索框文字变化：按新文字重建列表（列表只有几百行以内，不必延迟）。重建的是列表，不是搜索框本身
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdScopePopupSearch)
    {
        DuiControl* content = GetRoot();
        balloonwjui::DuiEdit* search = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(kIdScopePopupSearch)) : NULL;
        m_query = (search != NULL) ? TrimWide(std::wstring((LPCTSTR)search->GetText())) : std::wstring();
        RebuildRows();
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        //「确定」：以勾选的会话提交
        if (pNotify->ctrlId == kIdScopePopupOk)
        {
            DoSubmit(m_selected);
            return 0;
        }
        //「全部会话」：以空集合提交
        if (pNotify->ctrlId == kIdScopePopupAll)
        {
            DoSubmit(std::vector<HistoryConversationKey>());
            return 0;
        }
        //某一行的复选框：复选框自己已经翻转了勾选状态，这里只同步勾选集合，不重建列表
        //（重建会销毁正在发通知的这个复选框）
        std::map<UINT, size_t>::const_iterator it = m_rowByCheckId.find(pNotify->ctrlId);
        if (it != m_rowByCheckId.end() && it->second < m_entries.size())
        {
            const bool nowIn = ToggleManagerScopeKey(m_selected, m_entries[it->second].m_key);
            LOG_INFO("history scope popup: toggle ctype=%d, cid=%lld -> %d.",
                     m_entries[it->second].m_key.m_ctype, (long long)m_entries[it->second].m_key.m_cid,
                     nowIn ? 1 : 0);
            UpdateSelectedInfo();
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

} // namespace balloonhistory
