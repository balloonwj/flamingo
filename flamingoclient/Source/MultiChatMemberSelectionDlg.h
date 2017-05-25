#pragma once

#include <set>
#include "resource.h"
#include "SkinLib/SkinLib.h"


class CFlamingoClient;

//群发成员选择对话框
class CMultiChatMemberSelectionDlg : public CDialogImpl<CMultiChatMemberSelectionDlg>, public CMessageFilter
{
public:
	CMultiChatMemberSelectionDlg();
	virtual ~CMultiChatMemberSelectionDlg();

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	enum { IDD = IDD_SELECTMULTICHATTARGET };

	BEGIN_MSG_MAP_EX(CMultiChatMemberSelectionDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		NOTIFY_HANDLER_EX(IDC_TREE_MEMBERLIST, TVN_SELCHANGED, OnBuddyTreeSelectedChanged)
		NOTIFY_HANDLER_EX(IDC_LIST_SELECTED, NM_DBLCLK, OnSelectTargetListDblClk)
		COMMAND_ID_HANDLER_EX(IDOK, OnBtn_OK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnBtn_Cancel)
	END_MSG_MAP()


private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	LRESULT OnBuddyTreeSelectedChanged(LPNMHDR pnmh);
	LRESULT OnSelectTargetListDblClk(LPNMHDR pnmh);
	void OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

	void InitBuddyList();

	void AddMessageFilter();
	void RemoveMessageFilter();

public:
	CFlamingoClient*		m_pFMGClient;

	std::set<UINT>		m_setSelectedIDs;

private:
	CSkinDialog			m_SkinDlg;
	CTreeViewCtrlEx		m_treeBuddyList;
	CListViewCtrl		m_listSelectedBuddyList;
	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;
};