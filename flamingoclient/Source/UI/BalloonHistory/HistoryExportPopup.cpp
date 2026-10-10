/**
 *  HistoryExportPopup.cpp —— 聊天记录管理器导出选项浮层的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "HistoryExportPopup.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "HistoryExportLayout.h"        // 内容骨架与控件编号
#include "HistoryWindowLayout.h"        // BuildHistoryScopePopupRow：会话勾选列表的一行
#include "HistoryConversationList.h"    // NormalizeManagerScope / ToggleManagerScopeKey / IsInManagerScope

using balloonwjui::DuiControl;

namespace balloonhistory {

CHistoryExportPopup::CHistoryExportPopup()
    : m_scroll(NULL)
    , m_info(NULL)
    , m_built(false)
    , m_committed(false)
    , m_submitCb(NULL)
    , m_submitUd(NULL)
{
}

CHistoryExportPopup::~CHistoryExportPopup()
{
}

void CHistoryExportPopup::SetSubmitCallback(SubmitCallback cb, void* userdata)
{
    m_submitCb = cb;
    m_submitUd = userdata;
}

void CHistoryExportPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }
    std::unique_ptr<DuiControl> content = BuildHistoryExportPopupContent(m_opts);
    DuiControl* root = content.get();   //借用；所有权随即交给浮层
    m_scroll = dynamic_cast<balloonwjui::DuiScrollView*>(root->FindCtrlById(kIdExpConvScroll));
    m_info = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdExpConvInfo));
    SetContent(std::move(content));
    m_built = (m_scroll != NULL && m_info != NULL);
    if (!m_built)
    {
        LOG_ERROR("history export popup: content tree is missing the list or the info label.");
    }
}

void CHistoryExportPopup::OpenAbove(const RECT& anchorScreen, const std::vector<CHistoryScopePopup::Entry>& entries,
                                    const HistoryExportOptions& opts)
{
    m_opts = opts;
    m_opts.m_conversations = NormalizeManagerScope(opts.m_conversations);
    BuildContentOnce();
    if (!m_built)
    {
        return;
    }
    m_entries = entries;
    m_committed = false;

    //各控件按这一次的初始选项复位（内容树只建一次，上次打开留下的状态要改回来）
    DuiControl* root = GetRoot();
    balloonwjui::DuiComboBox* range = (root != NULL)
        ? dynamic_cast<balloonwjui::DuiComboBox*>(root->FindCtrlById(kIdExpRangeCombo)) : NULL;
    if (range != NULL)
    {
        range->SetCurSel((m_opts.m_range >= 0 && m_opts.m_range < kHistoryExportRangeCount) ? m_opts.m_range : 0, false);
    }
    const UINT checkIds[] = { kIdExpImages, kIdExpFiles, kIdExpZip };
    const bool checks[] = { m_opts.m_includeImages, m_opts.m_includeFiles, m_opts.m_zip };
    for (size_t i = 0; i < sizeof(checkIds) / sizeof(checkIds[0]); ++i)
    {
        balloonwjui::DuiButton* box = (root != NULL)
            ? dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(checkIds[i])) : NULL;
        if (box != NULL)
        {
            box->SetCheck(checks[i], false);
        }
    }

    SetSize(kHistoryExportPopupW, kHistoryExportPopupH);
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);
    //owner 传 NULL：理由同搜索范围浮层（子控件的通知要发给本浮层，而不是消息记录窗口）
    Show(anchorScreen, NULL);
    ApplyHistoryExportPopupState(GetRoot(), m_opts);
    RebuildRows();
    LOG_INFO("history export popup: opened, %u conversations listed, scope %d.", (unsigned)m_entries.size(),
             m_opts.m_scope);
}

void CHistoryExportPopup::RebuildRows()
{
    if (!m_built)
    {
        return;
    }
    std::unique_ptr<balloonwjui::DuiVBox> list(new balloonwjui::DuiVBox());
    list->SetGap(0);
    m_rowByCheckId.clear();
    for (size_t i = 0; i < m_entries.size() && (int)i < kIdExpConvCapacity; ++i)
    {
        const CHistoryScopePopup::Entry& e = m_entries[i];
        const UINT checkId = (UINT)(kIdExpConvCheckBase + i);
        list->AddChild(BuildHistoryScopePopupRow(checkId, IsInManagerScope(m_opts.m_conversations, e.m_key),
                                                 CString(e.m_name.c_str()), e.m_avatar, e.m_key.m_cid),
                       balloonwjui::DuiLayout::Hint().Fixed(kHistoryExportConvRowH));
        m_rowByCheckId[checkId] = i;
    }
    if (m_entries.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> empty(new balloonwjui::DuiLabel());
        empty->SetText(TR(L"还没有会话"));
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        list->AddChild(std::move(empty), balloonwjui::DuiLayout::Hint().Fixed(kHistoryExportConvRowH));
    }
    m_scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    m_scroll->SetScrollPos(0);
    m_scroll->ForceLayout(m_scroll->GetRect());
    m_scroll->Invalidate();
    UpdateInfo(false);
}

void CHistoryExportPopup::UpdateInfo(bool warn)
{
    if (m_info == NULL)
    {
        return;
    }
    if (warn)
    {
        m_info->SetText(TR(L"请至少勾选一个会话"));
    }
    else
    {
        m_info->SetText(TRF(L"已选 {0} 个会话", (int)m_opts.m_conversations.size()).c_str());
    }
    m_info->Invalidate();
}

void CHistoryExportPopup::ReadControls()
{
    DuiControl* root = GetRoot();
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiComboBox* range = dynamic_cast<balloonwjui::DuiComboBox*>(root->FindCtrlById(kIdExpRangeCombo));
    if (range != NULL && range->GetCurSel() >= 0)
    {
        m_opts.m_range = range->GetCurSel();
    }
    balloonwjui::DuiButton* images = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdExpImages));
    balloonwjui::DuiButton* files = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdExpFiles));
    balloonwjui::DuiButton* zip = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdExpZip));
    m_opts.m_includeImages = (images != NULL) && images->IsChecked();
    m_opts.m_includeFiles = (files != NULL) && files->IsChecked();
    m_opts.m_zip = (zip != NULL) && zip->IsChecked();
}

LRESULT CHistoryExportPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    const UINT id = pNotify->ctrlId;
    //范围胶囊：编号连续，与 HistoryExportScope 一一对应
    if (id == (UINT)kIdExpScopeCurrent || id == (UINT)kIdExpScopeSelected)
    {
        m_opts.m_scope = (int)(id - kIdExpScopeCurrent);
        ApplyHistoryExportPopupState(GetRoot(), m_opts);
        return 0;
    }
    //格式胶囊
    if (id == (UINT)kIdExpFmtHtml || id == (UINT)kIdExpFmtText)
    {
        m_opts.m_html = (id == (UINT)kIdExpFmtHtml);
        ApplyHistoryExportPopupState(GetRoot(), m_opts);
        return 0;
    }
    //「取消」：只关闭
    if (id == (UINT)kIdExpCancel)
    {
        Hide();
        return 0;
    }
    //「导出」：读出其余控件的状态，选项有效时回调宿主并关闭
    if (id == (UINT)kIdExpOk)
    {
        if (m_committed)
        {
            return 0;
        }
        ReadControls();
        if (m_opts.m_scope == kHistoryExportScopeSelected && m_opts.m_conversations.empty())
        {
            UpdateInfo(true);
            return 0;
        }
        //先置位再回调：回调里宿主会弹保存对话框，期间若再有点击送来，不会重复提交
        m_committed = true;
        LOG_INFO("history export popup: submit scope %d, %u conversations, range %d, html %d, images %d, files %d, "
                 "zip %d.", m_opts.m_scope, (unsigned)m_opts.m_conversations.size(), m_opts.m_range,
                 (int)m_opts.m_html, (int)m_opts.m_includeImages, (int)m_opts.m_includeFiles, (int)m_opts.m_zip);
        Hide();
        if (m_submitCb != NULL)
        {
            m_submitCb(m_submitUd, m_opts);
        }
        return 0;
    }
    //会话行的复选框：复选框自己已经翻转了勾选状态，这里只同步勾选集合，不重建列表
    std::map<UINT, size_t>::const_iterator it = m_rowByCheckId.find(id);
    if (it != m_rowByCheckId.end() && it->second < m_entries.size())
    {
        ToggleManagerScopeKey(m_opts.m_conversations, m_entries[it->second].m_key);
        UpdateInfo(false);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace balloonhistory
