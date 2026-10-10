/**
 *  BalloonNotifyManageWindow.cpp
 *
 *  批量管理窗口「消息提醒管理」的实现：建窗、列表重建、勾选、提交、单独设置、应答标红。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"
#include "BalloonNotifyManageWindow.h"
#include "NotifyManageLayout.h"
#include "../../App/BrandText.h"                       // g_strAppTitle（窗口标题与提示框标题）
#include "../BalloonMain/BalloonMainPanel.h"           // 读模式表、提交设置
#include "../BalloonMain/MemberPickerTreeView.h"       // 列表控件：复选框点击判定
#include "../BalloonMain/MemberPickerRow.h"            // 行内容：复选框 + 头像 + 名字 + 右端的当前模式
#include "../BalloonMain/MemberPickerTreeModel.h"      // kPickerChecked / kPickerUnchecked
#include "../BalloonMain/MainPanelScrollView.h"        // 滚动条动画推进、边框让位
#include "../BalloonMain/OverlayScrollBarGeometry.h"   // ComputeResizeBorderInsetPx
#include "../BalloonMain/MainPanelIcons.h"             // 右端模式的小图标
#include "../BalloonMain/MainPanelUserData.h"          // GetUserAvatarColor：头像底色
#include "../BalloonMain/NotifyModeMenu.h"             // 右键菜单的选项与文字
#include "../BalloonMain/PopupMenuPlacement.h"         // ClampPopupMenuToWorkArea：菜单整体落在工作区内
#include "../BalloonMain/TitleBarIcon.h"               // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../net/ConversationsProtocolClient.h"     // net::SetNotifyModesResult
#include "../../net/Protocol.h"                        // conversation_single / notify_mode_*
#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace {

const int kTitleIconSize = 16;                              // 标题栏左侧图标边长（逻辑像素）
const COLORREF kCaptionColor   = RGB(0x33, 0x33, 0x33);     // 标题文字与最小化 / 最大化 / 关闭字形
const COLORREF kTitleBarBg     = RGB(0xFF, 0xFF, 0xFF);     // 标题栏透明后穿透显示的底色（白）
const COLORREF kModeTextColor  = RGB(0x99, 0x99, 0x99);     // 行右端的当前模式：灰，与会话行的徽章同色
const COLORREF kStarredColor   = RGB(0xF5, 0xA6, 0x23);     // 特别关注：金，与名字后面的星标同色
const COLORREF kFailedColor    = RGB(0xE5, 0x48, 0x4D);     // 设置失败的行：红

// 某种模式在行右端画的小图标；允许提醒不画
int ModeIconKind(int mode)
{
    switch (mode)
    {
    //接收消息但不提醒；只提醒 @我和关注的人平时同样不提醒，沿用同一个图标
    case notify_mode_silent:
    case notify_mode_mention_follow:
        return balloonmain::kIconBellSlash;

    //收进消息盒子且不提醒
    case notify_mode_msgbox:
        return balloonmain::kIconDrawer;

    //屏蔽消息
    case notify_mode_block:
        return balloonmain::kIconBlock;

    //特别关注
    case notify_mode_starred:
        return balloonmain::kIconStar;

    //允许提醒与不认识的值：不画
    default:
        return -1;
    }
}

// 去掉首尾空白
std::wstring Trim(const CString& s)
{
    CString t(s);
    t.Trim();
    return std::wstring((LPCWSTR)t);
}

} // anonymous namespace

CBalloonNotifyManageWindow::CBalloonNotifyManageWindow()
    : m_pHost(NULL)
    , m_hTitleIcon(NULL)
    , m_truncated(false)
{
}

CBalloonNotifyManageWindow::~CBalloonNotifyManageWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CBalloonNotifyManageWindow::Open(CBalloonMainPanel* pHost, int initialCtype,
                                      const balloonnotifymode::ManagePools& pools)
{
    m_pHost = pHost;
    m_pools = pools;
    const int tab = (initialCtype == conversation_single) ? balloonnotifymode::kManageTabSingle
                                                          : balloonnotifymode::kManageTabGroup;

    if (!IsWindow())
    {
        if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("notify manage window Create failed.");
            return false;
        }
        SetTitle(TR(L"消息提醒管理"));
        SetTitleBarHeight(balloonnotifymode::kManageTitleBarH);
        SetTitleBarTransparent(true);
        SetTitleTextColor(kCaptionColor);
        SetCaptionGlyphColor(kCaptionColor);
        if (m_hTitleIcon == NULL)
        {
            m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
        }
        if (m_hTitleIcon != NULL)
        {
            SetIcon(m_hTitleIcon);
        }
        //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
        m_taskIcons.ApplyTaskIcons(m_hWnd);
        SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
        SetResizable(true);
        SetMinSize(balloonnotifymode::kManageMinWidth, balloonnotifymode::kManageMinHeight);
        SetClientContent(balloonnotifymode::BuildNotifyManageBody());

        //整窗尺寸，含标题栏（开发约定文档 UI 约定第 7 条）
        ResizeClient(balloonnotifymode::kManageWindowWidth, balloonnotifymode::kManageWindowHeight);
        CenterWindow();
        ShowWindow(SW_SHOW);
        UpdateWindow();
        SyncScrollBarEdgeInset();
        LOG_INFO("notify manage window created, tab=%d.", tab);
    }
    else if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }

    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiTab* tabs = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTab*>(content->FindCtrlById(balloonnotifymode::kIdManageTabs))
        : NULL;
    if (tabs != NULL)
    {
        tabs->SetCurSel(tab, /*notify*/false);
    }
    balloonnotifymode::SetManageTargetChoices(content, CurrentCtype() == conversation_group);
    RebuildList();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

int CBalloonNotifyManageWindow::CurrentCtype() const
{
    balloonwjui::DuiControl* content = const_cast<CBalloonNotifyManageWindow*>(this)->GetClientContent();
    balloonwjui::DuiTab* tabs = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTab*>(content->FindCtrlById(balloonnotifymode::kIdManageTabs))
        : NULL;
    if (tabs != NULL && tabs->GetCurSel() == balloonnotifymode::kManageTabSingle)
    {
        return conversation_single;
    }
    return conversation_group;
}

void CBalloonNotifyManageWindow::RebuildList()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MemberPickerTreeView* tree = dynamic_cast<balloonmain::MemberPickerTreeView*>(
        content->FindCtrlById(balloonnotifymode::kIdManageTree));
    balloonwjui::DuiEdit* search = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(balloonnotifymode::kIdManageSearch));
    if (tree == NULL)
    {
        return;
    }

    const std::wstring filter = (search != NULL) ? Trim(search->GetText()) : std::wstring();
    m_visible = balloonnotifymode::BuildVisibleItems(m_pools, CurrentCtype(), filter, m_truncated);

    tree->Clear();
    m_nodeIndex.clear();
    for (size_t i = 0; i < m_visible.size(); ++i)
    {
        const int nodeId = tree->AddRoot(_T(""));
        m_nodeIndex[nodeId] = i;
        const CString name(m_visible[i].m_name.c_str());
        std::unique_ptr<balloonmain::MemberPickerRow> row(new balloonmain::MemberPickerRow());
        row->SetRowData(name, name.Left(1), balloonmain::GetUserAvatarColor((UINT)m_visible[i].m_cid));
        tree->SetItemCustomControl(nodeId, std::unique_ptr<balloonwjui::DuiControl>(row.release()));
        ApplyRow(nodeId);
    }
    ApplyListState();
    LOG_INFO("notify manage list rebuilt: ctype=%d, rows=%u, truncated=%d, filterLen=%u.",
             CurrentCtype(), (unsigned)m_visible.size(), m_truncated ? 1 : 0, (unsigned)filter.size());
}

void CBalloonNotifyManageWindow::ApplyRow(int nodeId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonmain::MemberPickerTreeView* tree = (content != NULL)
        ? dynamic_cast<balloonmain::MemberPickerTreeView*>(content->FindCtrlById(balloonnotifymode::kIdManageTree))
        : NULL;
    std::map<int, size_t>::const_iterator it = m_nodeIndex.find(nodeId);
    if (tree == NULL || it == m_nodeIndex.end() || m_pHost == NULL)
    {
        return;
    }
    balloonmain::MemberPickerRow* row = dynamic_cast<balloonmain::MemberPickerRow*>(tree->GetItemCustomControl(nodeId));
    if (row == NULL)
    {
        return;
    }

    const balloonnotifymode::ManageItem& item = m_visible[it->second];
    row->SetCheckState(m_selection.IsChecked(item.m_ctype, item.m_cid) ? balloonmain::kPickerChecked
                                                                         : balloonmain::kPickerUnchecked);

    //右端：当前模式（灰；特别关注用金色）；上次提交失败的标红并注明
    const int mode = m_pHost->GetNotifyModeTable().GetMode(item.m_ctype, item.m_cid);
    CString text(balloonmain::GetNotifyModeLabel(mode));
    COLORREF color = (mode == notify_mode_starred) ? kStarredColor : kModeTextColor;
    if (m_failed.count(std::make_pair(item.m_ctype, item.m_cid)) != 0)
    {
        text = TRF(L"设置失败 · {0}", text).c_str();
        color = kFailedColor;
    }
    row->SetTrailing(text, ModeIconKind(mode), color);
}

void CBalloonNotifyManageWindow::ApplyListState()
{
    const int ctype = CurrentCtype();
    balloonnotifymode::ApplyManageListState(GetClientContent(), (int)m_visible.size(), m_truncated,
                                            m_selection.CountOf(ctype),
                                            m_selection.StateOf(m_visible) == balloonnotifymode::kManageCheckAll);
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonNotifyManageWindow::RefreshModes()
{
    if (!IsWindow())
    {
        return;
    }
    for (std::map<int, size_t>::const_iterator it = m_nodeIndex.begin(); it != m_nodeIndex.end(); ++it)
    {
        ApplyRow(it->first);
    }
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CBalloonNotifyManageWindow::ToggleRow(int nodeId)
{
    std::map<int, size_t>::const_iterator it = m_nodeIndex.find(nodeId);
    if (it == m_nodeIndex.end())
    {
        return;
    }
    const balloonnotifymode::ManageItem& item = m_visible[it->second];
    m_selection.Toggle(item.m_ctype, item.m_cid);
    ApplyRow(nodeId);
    ApplyListState();
}

void CBalloonNotifyManageWindow::ApplySelection()
{
    if (m_pHost == NULL)
    {
        return;
    }
    const int ctype = CurrentCtype();
    const int target = balloonnotifymode::GetManageTargetMode(GetClientContent(), ctype == conversation_group);
    int skipped = 0;
    const std::vector<net::NotifyModeEntry> entries =
        balloonnotifymode::BuildApplyEntries(m_selection, ctype, target, m_pHost->GetNotifyModeTable(), skipped);
    LOG_INFO("notify manage apply: ctype=%d, target=%d, entries=%u, skipped=%d.",
             ctype, target, (unsigned)entries.size(), skipped);

    if (entries.empty())
    {
        //勾选的会话已经都是这个模式：取消勾选，不发请求
        const std::vector<int64_t> ids = m_selection.CheckedIds(ctype);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            m_selection.SetChecked(ctype, ids[i], false);
        }
        RefreshModes();
        ApplyListState();
        return;
    }
    if (balloonnotifymode::WouldExceedStarredLimit(m_pHost->GetNotifyModeTable(), entries))
    {
        CString tip;
        tip.Format(TR(L"特别关注最多 %d 个（单聊与群聊合计），请减少勾选或先取消一些特别关注。"),
                   balloonmain::kNotifyModeStarredMax);
        ::MessageBox(m_hWnd, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    m_lastSubmitted.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        m_lastSubmitted.push_back(std::make_pair(entries[i].m_ctype, entries[i].m_cid));
        m_failed.erase(std::make_pair(entries[i].m_ctype, entries[i].m_cid));
    }
    m_pHost->SubmitNotifyModes(entries);
}

void CBalloonNotifyManageWindow::OnApplyResult(const net::SetNotifyModesResult& result)
{
    if (!IsWindow())
    {
        return;
    }
    if (result.m_code != 0)
    {
        //整单失败：最近一次提交的全部标红、保留勾选
        for (size_t i = 0; i < m_lastSubmitted.size(); ++i)
        {
            m_failed.insert(m_lastSubmitted[i]);
        }
    }
    for (size_t i = 0; i < result.m_results.size(); ++i)
    {
        const net::NotifyModeResult& r = result.m_results[i];
        const std::pair<int, int64_t> key(r.m_ctype, r.m_cid);
        if (r.m_code == 0)
        {
            //成功：取消勾选，清掉失败标记
            m_failed.erase(key);
            m_selection.SetChecked(r.m_ctype, r.m_cid, false);
        }
        else
        {
            m_failed.insert(key);
            m_selection.SetChecked(r.m_ctype, r.m_cid, true);
        }
    }
    RefreshModes();
    ApplyListState();
}

void CBalloonNotifyManageWindow::ShowRowMenu(int nodeId)
{
    std::map<int, size_t>::const_iterator it = m_nodeIndex.find(nodeId);
    if (it == m_nodeIndex.end() || m_pHost == NULL)
    {
        return;
    }
    const balloonnotifymode::ManageItem item = m_visible[it->second];
    const bool isGroup = (item.m_ctype == conversation_group);
    const int mode = m_pHost->GetNotifyModeTable().GetMode(item.m_ctype, item.m_cid);

    //与右键「消息提醒设置」二级菜单相同的模式项，去掉分隔条与「批量管理消息提醒」（本窗口就是）
    balloonwjui::DuiMenu menu;
    const std::vector<balloonmain::NotifyModeMenuEntry> rows = balloonmain::BuildNotifyModeSubMenu(isGroup, mode);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_checkable)
        {
            menu.AppendChecked(rows[i].m_id, rows[i].m_label, rows[i].m_checked);
        }
    }
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    const POINT cpos = balloonmain::ClampPopupMenuToWorkArea(pt, menu.MeasureSize());
    const UINT chosen = menu.TrackPopup(cpos.x, cpos.y, m_hWnd);   // 同步阻塞
    const int target = balloonmain::NotifyModeFromMenuId(chosen);
    if (target < 0 || target == mode)
    {
        return;
    }
    LOG_INFO("notify manage row menu: ctype=%d, cid=%lld, mode=%d -> %d.",
             item.m_ctype, (long long)item.m_cid, mode, target);
    m_failed.erase(std::make_pair(item.m_ctype, item.m_cid));
    m_pHost->SetNotifyModeFromManage(item.m_ctype, item.m_cid, target);
}

LRESULT CBalloonNotifyManageWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    DestroyWindow();
    m_visible.clear();
    m_nodeIndex.clear();
    m_selection.Clear();
    m_failed.clear();
    m_lastSubmitted.clear();
    LOG_INFO("notify manage window closed.");
    return 0;
}

LRESULT CBalloonNotifyManageWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    using namespace balloonnotifymode;

    //页签切换：下拉框换成该页签的选项，重建列表（勾选保留，按页签分开计）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdManageTabs)
    {
        SetManageTargetChoices(GetClientContent(), CurrentCtype() == conversation_group);
        RebuildList();
        return 0;
    }
    //搜索框文字变了：按新搜索词重建列表
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdManageSearch)
    {
        RebuildList();
        return 0;
    }
    //「全选」：勾上或取消当前列出的全部行
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdManageSelectAll)
    {
        const bool allChecked = (m_selection.StateOf(m_visible) == kManageCheckAll);
        m_selection.SetAll(m_visible, !allChecked);
        RefreshModes();
        ApplyListState();
        return 0;
    }
    //行的复选框
    if (pNotify->code == (UINT)balloonmain::MemberPickerTreeView::DUIN_PICKER_CHECK_TOGGLE
        && pNotify->ctrlId == kIdManageTree)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonmain::MemberPickerTreeView* tree = (content != NULL)
            ? dynamic_cast<balloonmain::MemberPickerTreeView*>(content->FindCtrlById(kIdManageTree))
            : NULL;
        if (tree != NULL)
        {
            ToggleRow(tree->GetToggledNodeId());
        }
        return 0;
    }
    //点在行身上：同样切换勾选，比只能点小方框好用
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdManageTree)
    {
        ToggleRow((int)pNotify->extra);
        return 0;
    }
    //行右键：单独设置这一个
    if (pNotify->code == (UINT)balloonwjui::DuiTreeView::DUITVN_RCLICK && pNotify->ctrlId == kIdManageTree)
    {
        ShowRowMenu((int)pNotify->extra);
        return 0;
    }
    //「应用」
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdManageApply)
    {
        ApplySelection();
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonNotifyManageWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& bHandled)
{
    //只旁听：DuiFrameWindow 还要靠它做客户区重排
    bHandled = FALSE;
    SyncScrollBarEdgeInset();
    return 0;
}

void CBalloonNotifyManageWindow::SyncScrollBarEdgeInset()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonmain::MainPanelScrollView* sv = (content != NULL)
        ? dynamic_cast<balloonmain::MainPanelScrollView*>(content->FindCtrlById(balloonnotifymode::kIdManageScroll))
        : NULL;
    if (sv == NULL)
    {
        return;
    }
    //列表视口横向占满客户区（根容器没有左右内边距），右缘就是窗口右缘，整条缩放边框都要让出
    sv->SetEdgeInsetPx(balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(), GetDpi(), IsResizable()));
}
