#include "stdafx.h"
#include "ClosePromptDlg.h"
#include "FlamingoClient.h"
#include "UserSessionData.h"

CClosePromptDlg::CClosePromptDlg()
{
	m_pFMGClient = NULL;
}

CClosePromptDlg::~CClosePromptDlg()
{

}

BOOL CClosePromptDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitUI();
	CenterWindow(::GetDesktopWindow());

	return TRUE;
}

BOOL CClosePromptDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\MsgBoxDlgBg.png"), CRect(4, 69, 4, 33));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("退出提示"));
	m_SkinDlg.MoveWindow(0, 0, 330, 160, FALSE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_picInfoIcon.SetTransparent(TRUE, hDlgBgDC);
	CString strPath;
	strPath.Format(_T("%sSkins\\Skin0\\MsgBoxIcon\\question.png"), g_szHomePath);
	m_picInfoIcon.SetBitmap(strPath);
	m_picInfoIcon.SubclassWindow(GetDlgItem(IDC_INFOICON));
	m_picInfoIcon.MoveWindow(50, 35, 32, 32, FALSE);
	
	m_staInfoText.SetTransparent(TRUE, hDlgBgDC);
	m_staInfoText.SubclassWindow(GetDlgItem(IDC_INFOTEXT));
	m_staInfoText.SetTextColor(RGB(0, 0, 0));
	m_staInfoText.MoveWindow(85, 40, 270, 20, FALSE);
	
	m_btnRememberChoice.SetButtonType(SKIN_CHECKBOX);
	m_btnRememberChoice.SetTransparent(TRUE, hDlgBgDC);
	m_btnRememberChoice.SubclassWindow(GetDlgItem(IDC_REMEMBERCHOICE));
	m_btnRememberChoice.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnRememberChoice.SetCheck(!m_pFMGClient->m_UserConfig.IsEnableExitPrompt());

	m_btnMinimize.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnMinimize.SetTransparent(TRUE, hDlgBgDC);
	m_btnMinimize.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnMinimize.SetTextColor(RGB(0, 0, 0));
	m_btnMinimize.SubclassWindow(GetDlgItem(IDC_MINIMIZE));
	m_btnMinimize.SetWindowText(_T("最小化"));

	m_btnExit.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnExit.SetTransparent(TRUE, hDlgBgDC);
	m_btnExit.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"),_T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
	m_btnExit.SetTextColor(RGB(0, 0, 0));
	m_btnExit.SubclassWindow(GetDlgItem(IDC_EXIT));
	m_btnExit.SetWindowText(_T("退出"));

	return TRUE;
}

void CClosePromptDlg::UninitUI()
{
	if(m_picInfoIcon.IsWindow())
		m_picInfoIcon.DestroyWindow();
}

void CClosePromptDlg::OnMinimize(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_pFMGClient->m_UserConfig.EnableExitPrompt(!m_btnRememberChoice.GetCheck());
	m_pFMGClient->m_UserConfig.EnableExitWhenCloseMainDlg(FALSE);
	EndDialog(IDC_MINIMIZE);
}
	
void CClosePromptDlg::OnExit(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_pFMGClient->m_UserConfig.EnableExitPrompt(!m_btnRememberChoice.GetCheck());
	m_pFMGClient->m_UserConfig.EnableExitWhenCloseMainDlg(TRUE);
	EndDialog(IDC_EXIT);
}


void CClosePromptDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CClosePromptDlg::OnDestroy()
{
	UninitUI();
}

