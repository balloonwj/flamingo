#include "stdafx.h"
#include "MultiChatMemberSelectionDlg.h"
#include "FlamingoClient.h"

CMultiChatMemberSelectionDlg::CMultiChatMemberSelectionDlg() : m_pFMGClient(NULL)
{

}

CMultiChatMemberSelectionDlg::~CMultiChatMemberSelectionDlg()
{

}

BOOL CMultiChatMemberSelectionDlg::PreTranslateMessage(MSG* pMsg)
{
	//支持Esc关闭对话框
	if (pMsg->hwnd==m_hWnd && pMsg->message==WM_KEYDOWN && pMsg->wParam==VK_ESCAPE)
 	{
		PostMessage(WM_CLOSE, 0, 0);
		return TRUE;
 	}
	else if(pMsg->hwnd==m_hWnd && pMsg->message==WM_KEYDOWN && pMsg->wParam==VK_RETURN)
	{
		PostMessage(WM_COMMAND, IDOK, 0);
		return TRUE;
	}

	return CWindow::IsDialogMessage(pMsg);
}

BOOL CMultiChatMemberSelectionDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	AddMessageFilter();
	InitUI();

	CenterWindow(::GetDesktopWindow());

	InitBuddyList();

	return TRUE;
}

BOOL CMultiChatMemberSelectionDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\GeneralBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("消息群发"));
	m_SkinDlg.MoveWindow(0, 0, 640, 430, TRUE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_treeBuddyList.Attach(GetDlgItem(IDC_TREE_MEMBERLIST));
	m_treeBuddyList.ModifyStyle(0, TVS_HASBUTTONS|TVS_HASLINES|TVS_LINESATROOT);

	m_listSelectedBuddyList.Attach(GetDlgItem(IDC_LIST_SELECTED));
	//m_listSelectedBuddyList.ModifyStyle(0, LVS_REPORT|LVS_EX_GRIDLINES);
	m_listSelectedBuddyList.AddColumn(_T("昵称"), 0);
	m_listSelectedBuddyList.AddColumn(_T("昵称账号"), 1);
	m_listSelectedBuddyList.SetColumnWidth(0, 110);
	m_listSelectedBuddyList.SetColumnWidth(1, 140);
	//m_listSelectedBuddyList.ModifyStyleEx(0, LVS_EX_FULLROWSELECT);

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SetRound(4, 4);
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));
	//m_btnOK.MoveWindow(165, 265 + 30, 94, 30, TRUE);

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SetRound(4, 4);
	m_btnCancel.SubclassWindow(GetDlgItem(IDCANCEL));
	//m_btnCancel.MoveWindow(290, 265 + 30, 94, 30, TRUE);

	return TRUE;
}

void CMultiChatMemberSelectionDlg::InitBuddyList()
{
	if(m_pFMGClient == NULL)
		return;

	const std::vector<CBuddyTeamInfo*>& arrBuddyTeamInfo = m_pFMGClient->m_UserMgr.m_BuddyList.m_arrBuddyTeamInfo;
	size_t nSize = arrBuddyTeamInfo.size();
	CBuddyTeamInfo* pTeamInfo = NULL;
	HTREEITEM hTreeItem = NULL;
	HTREEITEM hTreeItemRoot = NULL;
	CString strTemp;
	int nItemIndex = -1;
	for(size_t i=0; i<nSize; ++i)
	{
		pTeamInfo = arrBuddyTeamInfo[i];
		if(pTeamInfo == NULL)
			continue;
		hTreeItemRoot = m_treeBuddyList.InsertItem(pTeamInfo->m_strName.c_str(), 0, 0, NULL, NULL);
		m_treeBuddyList.SetItemData(hTreeItemRoot, (DWORD_PTR)(-1));
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			if(pTeamInfo->m_arrBuddyInfo[j]->m_strNickName.empty())
				continue;
			strTemp.Format(_T("%s(%s)"), pTeamInfo->m_arrBuddyInfo[j]->m_strNickName.c_str(), pTeamInfo->m_arrBuddyInfo[j]->m_strAccount.c_str());
			hTreeItem = m_treeBuddyList.InsertItem(strTemp, 0, 0, hTreeItemRoot, NULL);
			m_treeBuddyList.SetItemData(hTreeItem, (DWORD_PTR)pTeamInfo->m_arrBuddyInfo[j]->m_uUserID);
		}
	}
	
	hTreeItemRoot = m_treeBuddyList.InsertItem(_T("我的群组"), 0, 0, NULL, hTreeItemRoot);
	m_treeBuddyList.SetItemData(hTreeItemRoot, (DWORD_PTR)(-1)); 
	const std::vector<CGroupInfo*>& arrGroupInfo = m_pFMGClient->m_UserMgr.m_GroupList.m_arrGroupInfo;
	nSize = arrGroupInfo.size();
	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<nSize; ++i)
	{
		pGroupInfo = arrGroupInfo[i];
		if(pGroupInfo==NULL || pGroupInfo->m_strName.empty())
			continue;
		strTemp.Format(_T("群-%s(%s)"), pGroupInfo->m_strName.c_str(), pGroupInfo->m_strAccount.c_str());
		hTreeItem = m_treeBuddyList.InsertItem(strTemp, 0, 0, hTreeItemRoot, NULL);
		m_treeBuddyList.SetItemData(hTreeItem, (DWORD_PTR)pGroupInfo->m_nGroupCode);
	}
}

void CMultiChatMemberSelectionDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CMultiChatMemberSelectionDlg::OnDestroy()
{
	UninitUI();
	RemoveMessageFilter();
}

LRESULT CMultiChatMemberSelectionDlg::OnBuddyTreeSelectedChanged(LPNMHDR pnmh)
{
	HTREEITEM hTreeItem = m_treeBuddyList.GetSelectedItem();
	UINT uUserID = m_treeBuddyList.GetItemData(hTreeItem);
	if(uUserID == -1)
		return 0;
	
	if(m_setSelectedIDs.find(uUserID) != m_setSelectedIDs.end())
		return 0;

	m_setSelectedIDs.insert(uUserID);
	
	tstring strNickName;
	tstring strAccount;
	if(IsGroupTarget(uUserID))
	{
		strNickName = m_pFMGClient->m_UserMgr.GetGroupName(uUserID);
		strAccount = m_pFMGClient->m_UserMgr.GetGroupAccount(uUserID);
	}
	else
	{
		strNickName = m_pFMGClient->m_UserMgr.GetNickName(uUserID);
		strAccount = m_pFMGClient->m_UserMgr.GetAccountName(uUserID);
	}
	
	int nCount = m_listSelectedBuddyList.GetItemCount();
	int nIndex = m_listSelectedBuddyList.AddItem(nCount, 0, strNickName.c_str());
	m_listSelectedBuddyList.AddItem(nCount, 1, strAccount.c_str());
	m_listSelectedBuddyList.SetItemData(nIndex, (DWORD_PTR)uUserID);
	
	return 1;
}

LRESULT CMultiChatMemberSelectionDlg::OnSelectTargetListDblClk(LPNMHDR pnmh)
{
	int nIndex = m_listSelectedBuddyList.GetSelectedIndex();
	if(nIndex < 0)
		return 0;

	UINT uUserID = m_listSelectedBuddyList.GetItemData(nIndex);
	if(uUserID == 0)
		return 0;

	m_listSelectedBuddyList.DeleteItem(nIndex);
	std::set<UINT>::iterator iter = m_setSelectedIDs.find(uUserID);
	if(iter == m_setSelectedIDs.end())
		return 0;

	m_setSelectedIDs.erase(iter);

	return 1;
}

void CMultiChatMemberSelectionDlg::UninitUI()
{
	if (m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if (m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();

}

void CMultiChatMemberSelectionDlg::AddMessageFilter()
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
}

void CMultiChatMemberSelectionDlg::RemoveMessageFilter()
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}

void CMultiChatMemberSelectionDlg::OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDOK);
	
}

void CMultiChatMemberSelectionDlg::OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl)	
{
	EndDialog(IDCANCEL);
}
