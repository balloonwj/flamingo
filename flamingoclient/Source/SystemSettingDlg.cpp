#include "stdafx.h"
#include "SystemSettingDlg.h"
#include "FlamingoClient.h"
#include <assert.h>

CSystemSettingDlg::CSystemSettingDlg(void)
{
	m_pFMGClient = NULL;
	m_hDlgIcon = m_hDlgSmallIcon = NULL;
	m_hFont = NULL;
}

CSystemSettingDlg::~CSystemSettingDlg(void)
{
}

BOOL CSystemSettingDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	SetWindowPos(NULL, 0, 0, 480, 450, SWP_NOMOVE);
	::SetForegroundWindow(m_hWnd);

	// set icons
	m_hDlgIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	InitUI();

	CenterWindow(::GetDesktopWindow());

	return TRUE;
}


void CSystemSettingDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CSystemSettingDlg::OnDestroy()
{
	if (m_hDlgIcon != NULL)
	{
		::DestroyIcon(m_hDlgIcon);
		m_hDlgIcon = NULL;
	}

	if (m_hDlgSmallIcon != NULL)
	{
		::DestroyIcon(m_hDlgSmallIcon);
		m_hDlgSmallIcon = NULL;
	}

	UninitUI();

	if(m_hFont != NULL)
		::DeleteObject(m_hFont);
}

BOOL CSystemSettingDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\GeneralBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("œµÕ≥…Ë÷√"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_btnMute.SetButtonType(SKIN_CHECKBOX);
	m_btnMute.SetTransparent(TRUE, hDlgBgDC);
	m_btnMute.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnMute.SubclassWindow(GetDlgItem(IDC_MUTE));
    m_btnMute.SetCheck(m_pFMGClient->m_UserConfig.IsEnableMute());

	m_btnAutoReply.SetButtonType(SKIN_CHECKBOX);
	m_btnAutoReply.SetTransparent(TRUE, hDlgBgDC);
	m_btnAutoReply.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnAutoReply.SubclassWindow(GetDlgItem(IDC_AUTOREPLY));
	m_btnAutoReply.SetCheck(m_pFMGClient->m_UserConfig.IsEnableAutoReply());

	m_edtAutoReply.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtAutoReply.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtAutoReply.SetTransparent(TRUE, hDlgBgDC);
	m_edtAutoReply.SubclassWindow(GetDlgItem(IDC_AUTOREPLYCONTENT));
	m_edtAutoReply.SetWindowText(m_pFMGClient->m_UserConfig.GetAutoReplyContent());
	m_edtAutoReply.EnableWindow(m_btnAutoReply.GetCheck());

	m_btnDestroyAfterRead.SetButtonType(SKIN_CHECKBOX);
	m_btnDestroyAfterRead.SetTransparent(TRUE, hDlgBgDC);
	m_btnDestroyAfterRead.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnDestroyAfterRead.SubclassWindow(GetDlgItem(IDC_DESTROYAFTERREAD));

	
	m_cboDurationRead.SetTransparent(TRUE, hDlgBgDC);
	m_cboDurationRead.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_cboDurationRead.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_cboDurationRead.SubclassWindow(GetDlgItem(IDC_DESTROYDURATION));
	m_cboDurationRead.SetArrowNormalPic(_T("ComboBox\\inputbtn_normal_bak.png"));
	m_cboDurationRead.SetArrowHotPic(_T("ComboBox\\inputbtn_highlight.png"));
	m_cboDurationRead.SetArrowPushedPic(_T("ComboBox\\inputbtn_down.png"));
	m_cboDurationRead.SetArrowWidth(20);
	m_cboDurationRead.SetTransparent(TRUE, hDlgBgDC);
	m_cboDurationRead.SetItemHeight(-1, 18);
	m_cboDurationRead.SetReadOnly(TRUE);
	::EnableWindow(GetDlgItem(IDC_DESTRORAFTERREADLABEL), m_btnDestroyAfterRead.GetCheck());
	m_cboDurationRead.EnableWindow(m_btnDestroyAfterRead.GetCheck());
	
	const TCHAR szDuration[][8] = {_T("5√Î"), _T("6√Î"), _T("7√Î"), _T("8√Î"), _T("9√Î"), _T("10√Î")};
	for (long i = 0; i < ARRAYSIZE(szDuration); ++i)
	{
		m_cboDurationRead.InsertString(i, szDuration[i]);
	}
	m_cboDurationRead.SetCurSel(0);
	
	m_btnRevokeChatMsg.SetButtonType(SKIN_CHECKBOX);
	m_btnRevokeChatMsg.SetTransparent(TRUE, hDlgBgDC);
	m_btnRevokeChatMsg.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnRevokeChatMsg.SubclassWindow(GetDlgItem(IDC_ENABLEREVOKECHATMSG));

	m_btnExitPrompt.SetButtonType(SKIN_CHECKBOX);
	m_btnExitPrompt.SetTransparent(TRUE, hDlgBgDC);
	m_btnExitPrompt.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnExitPrompt.SubclassWindow(GetDlgItem(IDC_EXITPROMP));
	m_btnExitPrompt.SetCheck(m_pFMGClient->m_UserConfig.IsEnableExitPrompt());

	m_btnExitWhenClose.SetButtonType(SKIN_CHECKBOX);
	m_btnExitWhenClose.SetTransparent(TRUE, hDlgBgDC);
	m_btnExitWhenClose.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnExitWhenClose.SubclassWindow(GetDlgItem(IDC_MINIMIZEWHENCLOSE));
	m_btnExitWhenClose.SetCheck(!m_pFMGClient->m_UserConfig.IsEnableExitWhenCloseMainDlg());

	m_btnShowLastMsg.SetButtonType(SKIN_CHECKBOX);
	m_btnShowLastMsg.SetTransparent(TRUE, hDlgBgDC);
	m_btnShowLastMsg.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnShowLastMsg.SubclassWindow(GetDlgItem(IDC_CHECK_SHOWLASTMSG));
	m_btnShowLastMsg.SetCheck(m_pFMGClient->m_UserConfig.IsEnableShowLastMsgInChatDlg());

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\login_btn_normal.png"), _T("Button\\login_btn_highlight.png"), _T("Button\\login_btn_down.png"), _T("Button\\login_btn_focus.png"));
	m_btnOK.SetTextColor(RGB(255, 255, 255));
	m_btnOK.SetTextBoldStyle(TRUE);
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\login_btn_normal.png"), _T("Button\\login_btn_highlight.png"), _T("Button\\login_btn_down.png"), _T("Button\\login_btn_focus.png"));
	m_btnCancel.SetTextColor(RGB(255, 255, 255));
	m_btnCancel.SetTextBoldStyle(TRUE);
	m_btnCancel.SubclassWindow(GetDlgItem(IDCANCEL));
	
	return TRUE;
}

void CSystemSettingDlg::OnCheckAutoReply(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_edtAutoReply.EnableWindow(m_btnAutoReply.GetCheck());
}

void CSystemSettingDlg::OnCheckDestroyAfterRead(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	::EnableWindow(GetDlgItem(IDC_DESTRORAFTERREADLABEL), m_btnDestroyAfterRead.GetCheck());
	m_cboDurationRead.EnableWindow(m_btnDestroyAfterRead.GetCheck());
}
	
void CSystemSettingDlg::OnBtnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	//assert(m_pFMGClient != NULL);
    m_pFMGClient->m_UserConfig.EnableMute(m_btnMute.GetCheck());
	m_pFMGClient->m_UserConfig.EnableExitPrompt(m_btnExitPrompt.GetCheck());
	m_pFMGClient->m_UserConfig.EnableExitWhenCloseMainDlg(!m_btnExitWhenClose.GetCheck());

	m_pFMGClient->m_UserConfig.EnableAutoReply(m_btnAutoReply.GetCheck());
	
	CString strAutoReplyContent;
	m_edtAutoReply.GetWindowText(strAutoReplyContent);
	strAutoReplyContent.Trim();
	if(strAutoReplyContent != "")
		m_pFMGClient->m_UserConfig.SetAutoReplyContent(strAutoReplyContent);

	m_pFMGClient->m_UserConfig.EnableShowLastMsgInChatDlg(m_btnShowLastMsg.GetCheck());
	
	EndDialog(IDOK);
}

void CSystemSettingDlg::OnBtnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}

void CSystemSettingDlg::UninitUI()
{
	if (m_btnMute.IsWindow())
		m_btnMute.DestroyWindow();

	if (m_btnAutoReply.IsWindow())
		m_btnAutoReply.DestroyWindow();

	if (m_edtAutoReply.IsWindow())
		m_edtAutoReply.DestroyWindow();

	if (m_btnDestroyAfterRead.IsWindow())
		m_btnDestroyAfterRead.DestroyWindow();

	if (m_cboDurationRead.IsWindow())
		m_cboDurationRead.DestroyWindow();

	if (m_btnRevokeChatMsg.IsWindow())
		m_btnRevokeChatMsg.DestroyWindow();

	if(m_btnExitPrompt.IsWindow())
		m_btnExitPrompt.DestroyWindow();

	if(m_btnExitWhenClose.IsWindow())
		m_btnExitWhenClose.DestroyWindow();

	if (m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if (m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}