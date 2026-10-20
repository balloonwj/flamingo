/**
 *  DeptPickDialog.cpp
 *
 *  「选择部门」小窗，说明见 DeptPickDialog.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "DeptPickDialog.h"

#include <algorithm>   // std::remove：取消勾选时从有序列表里删除

#include "DeptPickLayout.h"          // BuildDeptPickBody / ApplyDeptPickState / SyncDeptPickTreeHeight 与控件 id
#include "DeptPickModel.h"           // BuildDeptPickTree / NormalizeDeptSelection
#include "MemberPickerTreeView.h"    // 部门树（复选框点击判定）
#include "MemberPickerRow.h"         // 行内容（复选框 + 名称）
#include "MemberPickerTreeModel.h"   // kPickerChecked / kPickerUnchecked
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"  // TR：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace balloonmain {

namespace {

// 部门行不画头像，头像底色不会用到；给一个固定值即可
const COLORREF kUnusedAvatarColor = RGB(0xFF, 0x7A, 0x45);

} // anonymous namespace

CDeptPickDialog::CDeptPickDialog()
    : m_ownerHwnd(NULL)
    , m_sink(NULL)
{
}

CDeptPickDialog::~CDeptPickDialog()
{
    CloseDialog();
}

bool CDeptPickDialog::Open(HWND owner, const std::vector<net::DepartmentItem>& departments,
                           const std::vector<int64_t>& preselected, IDeptPickSink* sink)
{
    m_ownerHwnd = owner;
    m_sink = sink;
    m_departments = departments;
    //预先勾选：去掉重复的与已删除的部门（被上级包含的仍然勾上，与上次保存的一致，确定时再整理）
    m_checked.clear();
    m_checkedSet.clear();
    for (size_t i = 0; i < preselected.size(); ++i)
    {
        if (!DeptNameOf(m_departments, preselected[i]).empty() && m_checkedSet.insert(preselected[i]).second)
            m_checked.push_back(preselected[i]);
    }

    const LPCTSTR title = TR(L"选择部门");
    bool created = false;
    if (!IsWindow())
    {
        //以规则编辑窗为 owner：本窗口恒在它之上。控件通知发回本窗口（开发约定文档 UI 约定第 11 条：
        //只有 WS_CHILD 宿主的控件通知发给父窗口）
        if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("dept pick dialog: Create failed.");
            return false;
        }
        SetTitleBarHeight(kDeptPickTitleBarH);
        SetIcon(m_programIcons.TitleBitmap(kProgramTitleIconSize, kOpaqueTitleBarColor));
        SetButtons(false, false, true);
        SetResizable(false);
        SetMinSize(kDeptPickWindowWidth, kDeptPickWindowHeight);
        created = true;
    }
    SetTitle(title);
    SetWindowText(title);
    SetClientContent(BuildDeptPickBody());
    ResizeClient(kDeptPickWindowWidth, kDeptPickWindowHeight);
    if (created)
    {
        CenterWindow(owner);
    }
    RebuildTree();

    //打开期间禁用所有者：避免在规则编辑窗里再点一次「选择…」又打开一份
    const bool ownerIsWindow = (owner != NULL) && (::IsWindow(owner) != FALSE);
    const bool ownerIsEnabled = ownerIsWindow && (::IsWindowEnabled(owner) != FALSE);
    if (m_ownerGuard.OnOpen(ownerIsWindow, ownerIsEnabled))
    {
        ::EnableWindow(owner, FALSE);
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    DuiControl* root = GetClientContent();
    DuiEdit* search = (root != NULL) ? dynamic_cast<DuiEdit*>(root->FindCtrlById(kIdDeptPickSearch)) : NULL;
    if (search != NULL)
    {
        SetDuiFocus(search);
    }
    LOG_INFO("dept pick dialog: opened, departments=%u, preselected=%u.", (unsigned)m_departments.size(),
             (unsigned)m_checked.size());
    return true;
}

void CDeptPickDialog::CloseDialog()
{
    m_sink = NULL;
    const bool ownerIsWindow = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    if (m_ownerGuard.OnClose(ownerIsWindow))
    {
        ::EnableWindow(m_ownerHwnd, TRUE);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
}

void CDeptPickDialog::RebuildTree()
{
    DuiControl* root = GetClientContent();
    if (root == NULL)
    {
        return;
    }
    MemberPickerTreeView* tree = dynamic_cast<MemberPickerTreeView*>(root->FindCtrlById(kIdDeptPickTree));
    DuiEdit* search = dynamic_cast<DuiEdit*>(root->FindCtrlById(kIdDeptPickSearch));
    if (tree == NULL)
    {
        return;
    }
    const std::wstring filter = (search != NULL) ? std::wstring((LPCWSTR)search->GetText()) : std::wstring();
    const std::vector<DeptPickNode> nodes = BuildDeptPickTree(m_departments, filter);

    tree->Clear();
    m_nodeDept.clear();
    //节点数组按显示顺序排列，父节点一定排在子节点前面，逐个建时父节点的树 id 必已就绪
    std::vector<int> nodeIds(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const DeptPickNode& n = nodes[i];
        const int nodeId = (n.m_parent < 0) ? tree->AddRoot(n.m_name.c_str())
                                            : tree->AddChild(nodeIds[n.m_parent], n.m_name.c_str());
        if (nodeId < 0)
        {
            continue;
        }
        nodeIds[i] = nodeId;
        m_nodeDept[nodeId] = n.m_deptId;
        //默认全展开：一眼看到全部部门，不必先逐级点开
        tree->SetExpanded(nodeId, true);
        std::unique_ptr<MemberPickerRow> row(new MemberPickerRow());
        row->SetRowData(n.m_name.c_str(), _T(""), kUnusedAvatarColor);
        row->SetCheckState(m_checkedSet.count(n.m_deptId) != 0 ? kPickerChecked : kPickerUnchecked);
        tree->SetItemCustomControl(nodeId, std::unique_ptr<DuiControl>(row.release()));
    }
    ApplyDeptPickState(root, static_cast<int>(nodes.size()), static_cast<int>(m_checked.size()));
    SyncDeptPickTreeHeight(root);
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CDeptPickDialog::Toggle(int nodeId)
{
    std::map<int, int64_t>::const_iterator it = m_nodeDept.find(nodeId);
    DuiControl* root = GetClientContent();
    if (it == m_nodeDept.end() || root == NULL)
    {
        return;
    }
    const int64_t deptId = it->second;
    const bool nowChecked = (m_checkedSet.count(deptId) == 0);
    if (nowChecked)
    {
        m_checkedSet.insert(deptId);
        m_checked.push_back(deptId);
    }
    else
    {
        m_checkedSet.erase(deptId);
        m_checked.erase(std::remove(m_checked.begin(), m_checked.end(), deptId), m_checked.end());
    }
    MemberPickerTreeView* tree = dynamic_cast<MemberPickerTreeView*>(root->FindCtrlById(kIdDeptPickTree));
    MemberPickerRow* row = (tree != NULL) ? dynamic_cast<MemberPickerRow*>(tree->GetItemCustomControl(nodeId)) : NULL;
    if (row != NULL)
    {
        row->SetCheckState(nowChecked ? kPickerChecked : kPickerUnchecked);
    }
    ApplyDeptPickState(root, static_cast<int>(m_nodeDept.size()), static_cast<int>(m_checked.size()));
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CDeptPickDialog::OnOk()
{
    const std::vector<int64_t> ids = NormalizeDeptSelection(m_checked, m_departments);
    if (ids.empty())
    {
        return;
    }
    LOG_INFO("dept pick dialog: confirmed, checked=%u, after normalize=%u.", (unsigned)m_checked.size(),
             (unsigned)ids.size());
    IDeptPickSink* sink = m_sink;
    m_sink = NULL;
    if (sink != NULL)
    {
        sink->OnDepartmentsPicked(ids);
    }
    PostMessage(WM_CLOSE);
}

LRESULT CDeptPickDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CloseDialog();
    return 0;
}

LRESULT CDeptPickDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //搜索框文字变了：按新搜索词重建树（勾选保留）
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kIdDeptPickSearch)
    {
        RebuildTree();
        return 0;
    }
    //行的复选框或行身上（树设置了 SetToggleOnRowClick，两者都发这个通知）
    if (pNotify->code == (UINT)MemberPickerTreeView::DUIN_PICKER_CHECK_TOGGLE && pNotify->ctrlId == kIdDeptPickTree)
    {
        DuiControl* root = GetClientContent();
        MemberPickerTreeView* tree = (root != NULL)
            ? dynamic_cast<MemberPickerTreeView*>(root->FindCtrlById(kIdDeptPickTree)) : NULL;
        if (tree != NULL)
        {
            Toggle(tree->GetToggledNodeId());
        }
        return 0;
    }
    //展开或收起了某个部门：树的内容总高变了，同步滚动范围
    if (pNotify->code == (UINT)MemberPickerTreeView::DUIN_PICKER_HEIGHT_CHANGED && pNotify->ctrlId == kIdDeptPickTree)
    {
        SyncDeptPickTreeHeight(GetClientContent());
        return 0;
    }
    //「确定」
    if (pNotify->code == DUIN_CLICK && pNotify->ctrlId == kIdDeptPickOk)
    {
        OnOk();
        return 0;
    }
    //「取消」：投 WM_CLOSE，等这次点击通知处理完再关闭
    if (pNotify->code == DUIN_CLICK && pNotify->ctrlId == kIdDeptPickCancel)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace balloonmain
