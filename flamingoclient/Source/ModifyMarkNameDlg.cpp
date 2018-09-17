#include "stdafx.h"
#include "ModifyMarkNameDlg.h"
#include "FlamingoClient.h"
#include "UIText.h"


CModifyMarkNameDlg::CModifyMarkNameDlg()
{
	m_pFMGClient = NULL;
	m_uUserID = 0;
}

CModifyMarkNameDlg::~CModifyMarkNameDlg()
{

}


BOOL CModifyMarkNameDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitUI();
	CenterWindow(m_pFMGClient->m_UserMgr.m_hCallBackWnd);
	return TRUE;
}

BOOL CModifyMarkNameDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\AddFriendDlgBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("修改备注姓名"));	
	m_SkinDlg.MoveWindow(0, 0, 300, 135, TRUE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_staMarkName.SetTransparent(TRUE, hDlgBgDC);
	m_staMarkName.SubclassWindow(GetDlgItem(IDC_STATIC_MARKNAME));

	m_edtMarkName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtMarkName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtMarkName.SetTransparent(TRUE, hDlgBgDC);
	m_edtMarkName.SubclassWindow(GetDlgItem(IDC_EDIT_MARKNAME));

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SetRound(4, 4);
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));
	
	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SetRound(4, 4);
	m_btnCancel.SubclassWindow(GetDlgItem(IDCANCEL));
	
	
	return TRUE;
}


void CModifyMarkNameDlg::UninitUI()
{
	if(m_staMarkName.IsWindow())
		m_staMarkName.DestroyWindow();

	if(m_edtMarkName.IsWindow())
		m_edtMarkName.DestroyWindow();

	if(m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if(m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}

void CModifyMarkNameDlg::OnClose()
{
	EndDialog(IDCANCEL);
}
	
void CModifyMarkNameDlg::OnDestroy()
{
	UninitUI();
}

void CModifyMarkNameDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString strMarkName;
	m_edtMarkName.GetWindowText(strMarkName);
	strMarkName.Trim();
	if(strMarkName.IsEmpty())
	{
		::MessageBox(m_hWnd, _T("备注姓名不能为空。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}
	else if(strMarkName.GetLength() >= 16)
	{
		::MessageBox(m_hWnd, _T("备注姓名不能超过15个字符。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}
	
	//CBuddyInfo* pBuddyInfo = m_pFMGClient->m_UserMgr.m_BuddyList.GetBuddy(m_uUserID);
	//if(pBuddyInfo != NULL)
	//	pBuddyInfo->m_strMarkName = strMarkName;

    m_pFMGClient->ModifyFriendMarkName(m_uUserID, strMarkName);

	EndDialog(IDOK);
}

void CModifyMarkNameDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}