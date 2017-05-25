#include "stdafx.h"
#include "AddFriendConfirmDlg.h"

// CAddFriendConfirmDlgÊµÏÖ´úÂë
CAddFriendConfirmDlg::CAddFriendConfirmDlg()
{
	m_pFMGClient = NULL;
	m_bShowAgreeButton = TRUE;
	m_bShowRefuseButton = TRUE;
	m_bShowOKButton = FALSE;
}

CAddFriendConfirmDlg::~CAddFriendConfirmDlg()
{
}

void CAddFriendConfirmDlg::SetWindowTitle(PCTSTR pszWindowTitle)
{
	m_strWindowTitle = pszWindowTitle;
}

void CAddFriendConfirmDlg::SetWindowInfo(PCTSTR pszWindowInfo)
{
	m_strWindowInfo = pszWindowInfo;
}
	

void CAddFriendConfirmDlg::ShowAgreeButton(BOOL bShow)
{
	m_bShowAgreeButton = bShow;
}

void CAddFriendConfirmDlg::ShowRefuseButton(BOOL bShow)
{
	m_bShowRefuseButton = bShow;
}

void CAddFriendConfirmDlg::ShowOKButton(BOOL bShow)
{
	m_bShowOKButton = bShow;
}

BOOL CAddFriendConfirmDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitUI();
	SetWindowText(m_strWindowTitle);

	CenterWindow(m_pFMGClient->m_UserMgr.m_hCallBackWnd);
	//::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);

	return TRUE;
}

BOOL CAddFriendConfirmDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\MsgBoxDlgBg.png"), CRect(4, 32, 4, 33));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(m_strWindowTitle);
	m_SkinDlg.MoveWindow(0, 0, 400, 160, TRUE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_btnAgree.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnAgree.SetTransparent(TRUE, hDlgBgDC);
	m_btnAgree.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnAgree.SetRound(4, 4);
	m_btnAgree.SubclassWindow(GetDlgItem(ID_ADDCONFIRM_AGREE));
	m_btnAgree.ShowWindow(m_bShowAgreeButton? SW_SHOW : SW_HIDE);

	m_btnRefuse.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnRefuse.SetTransparent(TRUE, hDlgBgDC);
	m_btnRefuse.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnRefuse.SubclassWindow(GetDlgItem(ID_ADDCONFIRM_REFUSE));
	m_btnRefuse.ShowWindow(m_bShowRefuseButton? SW_SHOW : SW_HIDE);

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));
	m_btnOK.ShowWindow(m_bShowOKButton? SW_SHOW : SW_HIDE);

	m_staticAddConfirmInfo.SetTransparent(TRUE, hDlgBgDC);
	m_staticAddConfirmInfo.SetLinkColor(RGB(225, 0, 0));
	m_staticAddConfirmInfo.SetLinkType(SKIN_LINK_ADDNEW);
	m_staticAddConfirmInfo.SubclassWindow(GetDlgItem(IDC_ADDCONFIRM));
	
	m_staticAddConfirmInfo.SetWindowText(m_strWindowInfo);

	return TRUE;
}

void CAddFriendConfirmDlg::OnClose()
{
	EndDialog(IDOK);
}

void CAddFriendConfirmDlg::OnDestroy()
{
	UninitUI();
}

void CAddFriendConfirmDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDOK);
}

void CAddFriendConfirmDlg::OnAgreeOrReject(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(nID);
}

void CAddFriendConfirmDlg::UninitUI()
{
	if (m_btnAgree.IsWindow())
		m_btnAgree.DestroyWindow();

	if (m_btnRefuse.IsWindow())
		m_btnRefuse.DestroyWindow();

	if (m_staticAddConfirmInfo.IsWindow())
		m_staticAddConfirmInfo.DestroyWindow();	
}