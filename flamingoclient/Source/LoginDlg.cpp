#include "stdafx.h"
#include "LoginDlg.h"
#include "Startup.h"
#include "Utils.h"
#include "Updater.h"
#include "IniFile.h"
#include "UpdateDlg.h"
#include "FlamingoClient.h"
#include "Path.h"
#include "UserSessionData.h"
#include "GDIFactory.h"
#include "EncodingUtil.h"
#include "IULog.h"
#include "net/Msg.h"


CLoginDlg::CLoginDlg(CFlamingoClient* pFMGClient)
{
    m_lpFMGClient = pFMGClient;
	m_pLoginAccountList = NULL;
	m_hDlgIcon = m_hDlgSmallIcon = NULL;
	memset(&m_stAccountInfo, 0, sizeof(m_stAccountInfo));
	m_stAccountInfo.nStatus = STATUS_ONLINE;
}

CLoginDlg::~CLoginDlg(void)
{
}

BOOL CLoginDlg::GetLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount)
{
	if (NULL == lpAccount)
		return FALSE;

	memcpy(lpAccount, &m_stAccountInfo, sizeof(LOGIN_ACCOUNT_INFO));
	return TRUE;
}

BOOL CLoginDlg::SetLoginAccountInfo(const LOGIN_ACCOUNT_INFO* lpAccount)
{
    if (NULL == lpAccount)
        return FALSE;

    memcpy(&m_stAccountInfo, lpAccount, sizeof(LOGIN_ACCOUNT_INFO));
    return TRUE;
}

void CLoginDlg::SetDefaultAccount(PCTSTR pszDefaultAccount)
{
	m_strDefaultAccount = pszDefaultAccount;
}

void CLoginDlg::SetDefaultPassword(PCTSTR pszDefaultPassword)
{
	m_strDefaultPassword = pszDefaultPassword;
}


BOOL CLoginDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	// set icons
	m_hDlgIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	m_bFirstModify = FALSE;
	
	memset(&m_stAccountInfo, 0, sizeof(m_stAccountInfo));
	InitUI();

	ModifyStyleEx(WS_EX_APPWINDOW, 0);

	CenterWindow2(m_hWnd, ::GetDesktopWindow());
	::SetForegroundWindow(m_hWnd);
	::SetFocus(m_hWnd);
	
	
	return TRUE;
}

void CLoginDlg::OnSysCommand(UINT nID, CPoint pt)
{
	if (nID == SC_MINIMIZE)
	{
		ShowWindow(SW_HIDE);
		return;
	}

	SetMsgHandled(FALSE);
}

void CLoginDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	m_SkinMenu.OnMeasureItem(nIDCtl, lpMeasureItemStruct);
}

void CLoginDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	m_SkinMenu.OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CLoginDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CLoginDlg::OnDestroy()
{
	// unregister message filtering and idle updates
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
}

// “UTalk帐号”组合框
void CLoginDlg::OnCbo_EditChange_UID(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString strText;
	m_cboUid.GetWindowText(strText);

	LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->Find(strText);
	if (NULL == lpAccount)
	{
		if (m_bFirstModify)
		{
			m_bFirstModify = FALSE;

			m_picHead.SetBitmap(_T("DefaultFace.png"));		// 设置用户头像
			m_picHead.Invalidate();
			
			m_btnRememberPwd.SetCheck(m_stAccountInfo.bRememberPwd ? BST_CHECKED : BST_UNCHECKED);
			m_btnAutoLogin.SetCheck(m_stAccountInfo.bAutoLogin ? BST_CHECKED : BST_UNCHECKED);
			m_stAccountInfo.nStatus = STATUS_ONLINE;
			StatusMenuBtn_SetIconPic(m_btnLoginStatus, m_stAccountInfo.nStatus);
			m_edtPwd.SetWindowText(_T(""));
			m_edtPwd.SetDefaultTextMode(TRUE);
		}
	}
	else
		SetCurUser(strText);
}

// “UTalk帐号”组合框
void CLoginDlg::OnCbo_SelChange_UID(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	int nCurSel = m_cboUid.GetCurSel();
	if (nCurSel != -1)
	{
		CString strUser;
		m_cboUid.GetLBText(nCurSel, strUser);
		SetCurUser(strUser);
	}
}

// “记住密码”复选框
void CLoginDlg::OnBtn_RememberPwd(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	// 取消“记住密码”复选框，则自动取消“自动登录”复选框
	if (m_btnRememberPwd.GetCheck() == BST_UNCHECKED)
		m_btnAutoLogin.SetCheck(BST_UNCHECKED);
}

// “自动登录”复选框
void CLoginDlg::OnBtn_AutoLogin(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	// 选中“自动登录”复选框，则自动选上“记住密码”复选框
	if (m_btnAutoLogin.GetCheck() == BST_CHECKED)
		m_btnRememberPwd.SetCheck(BST_CHECKED);
}

// “登录”按钮
void CLoginDlg::OnBtn_Login(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	//调试版就不要检测自动升级了，不然如何调试呢？
#ifndef _DEBUG
	DetectNewVersion();
#endif
	
	if (m_cboUid.IsDefaultText())
	{
		MessageBox(_T("请输入账号！"), _T("提示"), MB_OK|MB_ICONINFORMATION);
		m_cboUid.SetFocus();
		return;
	}

	if (m_edtPwd.IsDefaultText())
	{
		MessageBox(_T("请输入密码！"), _T("提示"), MB_OK|MB_ICONINFORMATION);
		m_edtPwd.SetFocus();
		return;
	}

	m_cboUid.GetWindowText(m_stAccountInfo.szUser, ARRAYSIZE(m_stAccountInfo.szUser));
	m_edtPwd.GetWindowText(m_stAccountInfo.szPwd, ARRAYSIZE(m_stAccountInfo.szPwd));
	m_stAccountInfo.bRememberPwd = (m_btnRememberPwd.GetCheck() == BST_CHECKED);
	m_stAccountInfo.bAutoLogin = (m_btnAutoLogin.GetCheck() == BST_CHECKED);

	// 记录当前用户信息
	m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount = m_stAccountInfo.szUser;

    DoLogin();

	EndDialog(IDOK);
}

// “设置”按钮
void CLoginDlg::OnBtn_Set(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CLoginSettingsDlg m_LoginSettingDlg;
	m_LoginSettingDlg.m_pClient = m_lpFMGClient;
	m_LoginSettingDlg.DoModal(m_hWnd, NULL);

	m_btnLogin.SetFocus();
}

void CLoginDlg::DetectNewVersion()
{
	//将updater.IsNeedUpdate()做成UpdateDlg成员方法
	Updater updater(&(m_lpFMGClient->m_FileTask));
	
	//需要升级
	if(CheckOnlyOneInstance() && updater.IsNeedUpdate())
	{
		if(IDYES == ::MessageBox(m_hWnd, _T("检测到新版本，是否现在就升级？"), _T("新版本升级提示"), MB_YESNO|MB_ICONQUESTION|MB_TOPMOST))
		{
			ShowWindow(SW_HIDE);
			CUpdateDlg updateDlg;
			updateDlg.m_pFMGClient = m_lpFMGClient;
			updateDlg.m_aryFileInfo = updater.m_aryUpdateFileList;
			if(updateDlg.DoModal(m_hWnd, NULL) == IDCANCEL)
			{
				ShowWindow(SW_SHOW);
				return;	
			}
		}
	}
}

void CLoginDlg::OnRegister(UINT uNotifyCode, int nID, CWindow wndCtl)
{	
	//显示注册对话框
	m_RegDlg.m_pFMGClient = m_lpFMGClient;
	if(m_RegDlg.DoModal(m_hWnd, NULL) != IDOK)
		return;
	
	//注册成功以后将注册好的账户名和密码填入登录文本框中
	m_cboUid.SetDefaultTextMode(FALSE);
	m_cboUid.SetWindowText(m_RegDlg.GetAccountName());
	m_edtPwd.SetDefaultTextMode(FALSE);
	m_edtPwd.SetWindowText(m_RegDlg.GetPassword());
	m_edtPwd.SetPasswordChar(0x25CF);
}

// “登录状态”菜单按钮
void CLoginDlg::OnMenu_LoginStatus(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_stAccountInfo.nStatus = (int)GetStatusFromMenuID(nID);
	//StatusMenuBtn_SetIconPic(m_btnLoginStatus, (UTalk_STATUS)m_stAccountInfo.nStatus);
}

BOOL CLoginDlg::InitUI()
{
	HRGN hHotRgn = NULL;
	HRGN hTemp = NULL;
	RECT rtWindow = {0};

	m_SkinDlg.SetBgPic(_T("DlgBg\\LoginDlgBg.png"));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), _T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.MoveWindow(0, 0, 550, 380, FALSE);
	m_SkinDlg.SetTitleText(_T(""));


	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	int nBtnWidth = 69;
	int nBtnHeight = 22;

	m_btnSet.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnSet.SetTransparent(TRUE, hDlgBgDC);
	m_btnSet.SetBgPic(_T("Button\\login_setting_btn_normal.png"), _T("Button\\login_setting_btn_focus.png"), _T("Button\\login_setting_btn_focus.png"), _T("Button\\login_setting_btn_focus.png"));
	m_btnSet.SubclassWindow(GetDlgItem(ID_BTN_SET));
	m_btnSet.MoveWindow(472, 0, 26, 26, FALSE);
	m_btnSet.SetToolTipText(_T("网络设置"));
	m_btnSet.GetClientRect(&rtWindow);
	hHotRgn = ::CreateRectRgnIndirect(&rtWindow);


	m_btnLogin.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnLogin.SetTransparent(TRUE, hDlgBgDC);
	m_btnLogin.SetBgPic(_T("Button\\login_btn_normal.png"), _T("Button\\login_btn_highlight.png"), _T("Button\\login_btn_down.png"), _T("Button\\login_btn_focus.png"));
	m_btnLogin.SetTextColor(RGB(255, 255, 255));
	m_btnLogin.SetTextBoldStyle(TRUE);
	m_btnLogin.SubclassWindow(GetDlgItem(ID_BTN_LOGIN));
	m_btnLogin.MoveWindow(185, 295, 180, 30, FALSE);
	m_btnLogin.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_btnRememberPwd.SetButtonType(SKIN_CHECKBOX);
	m_btnRememberPwd.SetTransparent(TRUE, hDlgBgDC);
	m_btnRememberPwd.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"), _T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnRememberPwd.SubclassWindow(GetDlgItem(ID_CHECK_REMEMBER_PWD));
	m_btnRememberPwd.MoveWindow(185, 256, 68, 15, FALSE);
	m_btnRememberPwd.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_btnAutoLogin.SetButtonType(SKIN_CHECKBOX);
	m_btnAutoLogin.SetTransparent(TRUE, hDlgBgDC);
	m_btnAutoLogin.SetCheckBoxPic(_T("CheckBox\\checkbox_normal.png"), _T("CheckBox\\checkbox_hightlight.png"),_T("CheckBox\\checkbox_tick_normal.png"), _T("CheckBox\\checkbox_tick_highlight.png"));
	m_btnAutoLogin.SubclassWindow(GetDlgItem(ID_CHECK_AUTO_LOGIN));
	m_btnAutoLogin.MoveWindow(285, 256, 68, 15, FALSE);
	m_btnAutoLogin.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_lnkRegAccount.SetTransparent(TRUE, hDlgBgDC);
	HFONT hFont = CGDIFactory::GetFont(20);
	m_lnkRegAccount.SetNormalFont(hFont);
	m_lnkRegAccount.SetLinkColor(RGB(22, 112, 235));
	m_lnkRegAccount.SetHoverLinkColor(RGB(22, 112, 235));
	m_lnkRegAccount.SetVisitedLinkColor(RGB(22, 112, 235));
	m_lnkRegAccount.SubclassWindow(GetDlgItem(ID_STATIC_REG_ACCOUNT));
	m_lnkRegAccount.SetLabel(_T("注册账号"));
	//m_lnkRegAccount.SetLinkType(SKIN_LINK_REGISTER);
	//m_lnkRegAccount.SetHyperLink(_T("http://zc.flamingo.com/chs/index.html?from=client&ptlang=2052&ADUIN=0&ADSESSION=0&ADTAG=CLIENT.UTalk.4153_NewAccount_Btn.0"));
	m_lnkRegAccount.SetToolTipText(_T("注册账号"));
	m_lnkRegAccount.MoveWindow(385, 170, 80, 15, FALSE);
	m_lnkRegAccount.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_lnkLostPwd.SetTransparent(TRUE, hDlgBgDC);
	m_lnkLostPwd.SetNormalFont(hFont);
	m_lnkLostPwd.SetLinkColor(RGB(22, 112, 235));
	m_lnkLostPwd.SetHoverLinkColor(RGB(22, 112, 235));
	m_lnkLostPwd.SetVisitedLinkColor(RGB(22, 112, 235));
	m_lnkLostPwd.SubclassWindow(GetDlgItem(ID_STATIC_LOST_PWD));
	m_lnkLostPwd.SetLabel(_T("找回密码"));
	//m_lnkLostPwd.SetHyperLink(_T("http://aq.flamingo.com/cn2/findpsw/findpsw_index?source_id=1003&ptlang=2052&aquin=123456"));
	//m_lnkLostPwd.SetToolTipText(_T("找回密码"));
	m_lnkLostPwd.MoveWindow(385, 215, 80, 15, FALSE);
	m_lnkLostPwd.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_cboUid.SubclassWindow(GetDlgItem(ID_COMBO_UID));
	m_cboUid.MoveWindow(185, 165, 181, 30, FALSE);
	m_cboUid.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_cboUid.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_cboUid.SetArrowNormalPic(_T("ComboBox\\login_inputbtn_normal.png"));
	m_cboUid.SetArrowHotPic(_T("ComboBox\\login_inputbtn_highlight.png"));
	m_cboUid.SetArrowPushedPic(_T("ComboBox\\login_inputbtn_down.png"));
	m_cboUid.SetTransparent(TRUE, hDlgBgDC);
	m_cboUid.SetArrowWidth(28);
	m_cboUid.SetDefaultText(_T("请输入账号或手机号"));
	//m_cboUid.SetDefaultTextMode(TRUE);
	m_cboUid.SetItemHeight(-1, 22);
	m_cboUid.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);


	if (m_pLoginAccountList != NULL)	// 添加登录帐号列表
	{
		for (int i = 0; i < m_pLoginAccountList->GetCount(); i++)
		{
			LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->GetAccountInfo(i);
			if (lpAccount != NULL)
				m_cboUid.AddString(lpAccount->szUser);
		}
	}
	
	m_edtPwd.SubclassWindow(GetDlgItem(ID_EDIT_PWD));
	m_edtPwd.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtPwd.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	//m_edtPwd.SetIconPic(_T("keyboard.png"));
	m_edtPwd.SetTransparent(TRUE, hDlgBgDC);
	m_edtPwd.SetPasswordChar(0x25CF);
	m_edtPwd.SetDefaultTextMode(TRUE);
	m_edtPwd.SetDefaultText(_T("请输入密码"));
	m_edtPwd.MoveWindow(185, 210, 180, 30, FALSE);
	m_edtPwd.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);


	m_picHead.SetTransparent(TRUE, hDlgBgDC);
	m_picHead.SetBitmap(_T("DefaultFace.png"));
	m_picHead.SubclassWindow(GetDlgItem(ID_STATIC_FACE));
	m_picHead.MoveWindow(40, 165, 120, 120, FALSE);
	m_picHead.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(hHotRgn, hHotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);


	m_SkinMenu.SetBgPic(_T("Menu\\menu_left_bg.png"), _T("Menu\\menu_right_bg.png"));
	m_SkinMenu.SetSelectedPic(_T("Menu\\menu_selected.png"));
	m_SkinMenu.SetSepartorPic(_T("Menu\\menu_separtor.png"));
	m_SkinMenu.SetArrowPic(_T("Menu\\menu_arrow.png"));
	m_SkinMenu.SetCheckPic(_T("Menu\\menu_check.png"));
	m_SkinMenu.SetTextColor(RGB(0, 20, 35));
	m_SkinMenu.SetSelTextColor(RGB(254, 254, 254));
	m_SkinMenu.LoadMenu(ID_MENU_LOGINSTATUS);

	DWORD dwMenuIDs[] = {ID_MENU_IMONLINE, ID_MENU_QME, 
		ID_MENU_AWAY, ID_MENU_BUSY, ID_MENU_MUTE, ID_MENU_INVISIBLE};
	CString strFileNames[] = {_T("Status\\imonline.png"), _T("Status\\Qme.png"), 
		_T("Status\\away.png"), _T("Status\\busy.png"), _T("Status\\mute.png"), 
		_T("Status\\invisible.png")};

	for (int i = 0; i < 6; i++)
	{
		m_SkinMenu.SetIcon(dwMenuIDs[i], FALSE, strFileNames[i], strFileNames[i]);
	}

	CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(0);

	m_btnLoginStatus.SetButtonType(SKIN_MENU_BUTTON);
	m_btnLoginStatus.SetTransparent(TRUE, m_picHead.GetDC());
	m_btnLoginStatus.SetBgPic(NULL , _T("menubtn_highlight.png"), _T("menubtn_highlight.png"), NULL);
	//m_btnLoginStatus.SetArrowPic(_T("MainPanel_FolderNode_expandTexture.png"));
	m_btnLoginStatus.SetIconPic(_T("Status\\imonline.png"));
	m_btnLoginStatus.SubclassWindow(GetDlgItem(ID_BTN_LOGIN_STATUS));
	m_btnLoginStatus.MoveWindow(126, 214, 35, 22, FALSE);
	m_btnLoginStatus.SetMenu(PopupMenu.m_hMenu);
	m_btnLoginStatus.ShowWindow(SW_HIDE);

	if(!m_strDefaultAccount.IsEmpty())
	{
		m_cboUid.SetDefaultTextMode(FALSE);
		m_cboUid.SetWindowText(m_strDefaultAccount);
		m_edtPwd.SetDefaultTextMode(FALSE);
		m_edtPwd.SetWindowText(m_strDefaultPassword);

		CString strPath;
		strPath.Format(_T("%sImage\\UserThumbs\\1.png"), g_szHomePath);		
		m_picHead.SetBitmap(strPath);
		m_picHead.Invalidate();
	}
	else
	{
		if (m_stAccountInfo.szUser[0] != _T('\0'))
		{
			SetCurUser(m_stAccountInfo.szUser, TRUE);
		}
		else
		{
			if (m_pLoginAccountList != NULL && m_pLoginAccountList->GetCount() > 0)
			{
				LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->GetLastLoginAccountInfo();
				if (lpAccount != NULL)
					SetCurUser(lpAccount->szUser);
			}
			else
			{
				SetCurUser(NULL);
			}
		}
	}

	m_SkinDlg.SetHotRegion(hHotRgn);

	return TRUE;
}

void CLoginDlg::SetCurUser(LPCTSTR lpszUser, BOOL bPwdInvalid/* = FALSE*/)
{
	if (NULL == lpszUser)
	{
		m_picHead.SetBitmap(_T("DefaultFace.png"));		// 设置用户头像
		m_picHead.Invalidate();

		m_btnRememberPwd.SetCheck(m_stAccountInfo.bRememberPwd ? BST_CHECKED : BST_UNCHECKED);
		m_btnAutoLogin.SetCheck(m_stAccountInfo.bAutoLogin ? BST_CHECKED : BST_UNCHECKED);
		m_stAccountInfo.nStatus = (int)STATUS_ONLINE;
		//StatusMenuBtn_SetIconPic(m_btnLoginStatus, (UTalk_STATUS)m_stAccountInfo.nStatus);
		m_cboUid.SetDefaultTextMode(TRUE);
		m_cboUid.SetWindowText(_T("请输入账号或手机号"));
		m_edtPwd.SetDefaultTextMode(TRUE);
		m_edtPwd.SetWindowText(_T("请输入密码"));
	}
	else
	{
		if (m_pLoginAccountList != NULL)
		{
			LOGIN_ACCOUNT_INFO* lpAccount = m_pLoginAccountList->Find(lpszUser);
			if (NULL == lpAccount)
			{
				m_picHead.SetBitmap(_T("DefaultFace.png"));		// 设置用户头像
				m_picHead.Invalidate();

				m_btnRememberPwd.SetCheck(m_stAccountInfo.bRememberPwd ? BST_CHECKED : BST_UNCHECKED);
				m_btnAutoLogin.SetCheck(m_stAccountInfo.bAutoLogin ? BST_CHECKED : BST_UNCHECKED);
				//StatusMenuBtn_SetIconPic(m_btnLoginStatus, (UTalk_STATUS)m_stAccountInfo.nStatus);
				CString strText;
				m_cboUid.GetWindowText(strText);
				if (strText != lpszUser)
					m_cboUid.SetWindowText(lpszUser);
				m_edtPwd.SetWindowText(_T(""));
				m_edtPwd.SetDefaultTextMode(TRUE);
			}
			else
			{
				if (m_lpFMGClient != NULL)	// 设置用户头像
				{
					CString strPath;
					strPath.Format(_T("%sUsers\\%s\\%s.cfg"), g_szHomePath, lpszUser, lpszUser);
					TCHAR szCustomFace[MAX_PATH] = {0};
					CIniFile iniFile;
					iniFile.ReadString(_T("UserInfo"), _T("CustomFace"), _T(""), szCustomFace, MAX_PATH, strPath);
					long nFace = iniFile.ReadInt(_T("UserInfo"), _T("FaceID"), 0, strPath);
					if(szCustomFace[0] == NULL)
					{
						strPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, nFace);	
					}
					else
					{
						strPath.Format(_T("%sUsers\\%s\\UserThumb\\%s"), g_szHomePath, lpszUser, szCustomFace);	
						if(!Hootina::CPath::IsFileExist(strPath))
							strPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, nFace);	

					}
					m_picHead.SetBitmap(strPath);
					m_picHead.Invalidate();
				}

				m_btnRememberPwd.SetCheck(lpAccount->bRememberPwd ? BST_CHECKED : BST_UNCHECKED);
				m_btnAutoLogin.SetCheck(lpAccount->bAutoLogin ? BST_CHECKED : BST_UNCHECKED);
				m_stAccountInfo.nStatus = (int)lpAccount->nStatus;
				//StatusMenuBtn_SetIconPic(m_btnLoginStatus, (UTalk_STATUS)m_stAccountInfo.nStatus);
				CString strText;
				m_cboUid.GetWindowText(strText);
				if (strText != lpszUser)
					m_cboUid.SetWindowText(lpszUser);
				if (!bPwdInvalid && lpAccount->bRememberPwd)
				{
					m_edtPwd.SetDefaultTextMode(FALSE);
					m_edtPwd.SetWindowText(lpAccount->szPwd);
				}
				else
				{
					m_edtPwd.SetPasswordChar(0x25CF);
					m_edtPwd.SetDefaultTextMode(TRUE);
					m_edtPwd.SetWindowText(_T(""));	
				}
				m_bFirstModify = TRUE;
			}
		}
	}
}

UINT CLoginDlg::LoginThreadProc(void* pParam)
{
    CLoginDlg* pLoginDlg = (CLoginDlg*)pParam;
    if (pLoginDlg == NULL)
        return 0;

    char szUser[64] = { 0 };
    EncodeUtil::UnicodeToUtf8(pLoginDlg->m_stAccountInfo.szUser, szUser, ARRAYSIZE(szUser));
    char szPassword[64] = { 0 };
    EncodeUtil::UnicodeToUtf8(pLoginDlg->m_stAccountInfo.szPwd, szPassword, ARRAYSIZE(szPassword));

    std::string strReturnData;
    //超时时间设置为3秒
    bool bRet = CIUSocket::GetInstance().Login(szUser, szPassword, 1, online_type_pc_online, 3, strReturnData);
    int nRet = LOGIN_FAILED;
    CLoginResult* pLoginResult = new CLoginResult();
    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
    if (bRet)
    {
        //{"code": 0, "msg": "ok", "userid": 8}
        Json::Reader JsonReader;
        Json::Value JsonRoot;
        if (JsonReader.parse(strReturnData, JsonRoot) && !JsonRoot["code"].isNull() && JsonRoot["code"].isInt())
        {
            int nRetCode = JsonRoot["code"].asInt();

            if (nRetCode == 0)
            {
                if (!JsonRoot["userid"].isInt() || !JsonRoot["username"].isString() || !JsonRoot["nickname"].isString() ||
                    !JsonRoot["facetype"].isInt() || !JsonRoot["gender"].isInt() || !JsonRoot["birthday"].isInt() ||
                    !JsonRoot["signature"].isString() || !JsonRoot["address"].isString() ||
                    !JsonRoot["customface"].isString() || !JsonRoot["phonenumber"].isString() ||
                    !JsonRoot["mail"].isString())
                {
                    LOG_ERROR(_T("login failed, login response json is invalid, json=%s"), strReturnData.c_str());
                    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
                }
                else
                {
                    pLoginResult->m_LoginResultCode = 0;
                    pLoginResult->m_uAccountID = JsonRoot["userid"].asInt();
                    strcpy_s(pLoginResult->m_szAccountName, ARRAYSIZE(pLoginResult->m_szAccountName), JsonRoot["username"].asCString());
                    strcpy_s(pLoginResult->m_szNickName, ARRAYSIZE(pLoginResult->m_szNickName), JsonRoot["nickname"].asCString());
                    //pLoginResult->m_nStatus = JsonRoot["status"].asInt();
                    pLoginResult->m_nFace = JsonRoot["facetype"].asInt();
                    pLoginResult->m_nGender = JsonRoot["gender"].asInt();
                    pLoginResult->m_nBirthday = JsonRoot["birthday"].asInt();
                    strcpy_s(pLoginResult->m_szSignature, ARRAYSIZE(pLoginResult->m_szSignature), JsonRoot["signature"].asCString());
                    strcpy_s(pLoginResult->m_szAddress, ARRAYSIZE(pLoginResult->m_szAddress), JsonRoot["address"].asCString());
                    strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace), JsonRoot["customface"].asCString());
                    strcpy_s(pLoginResult->m_szPhoneNumber, ARRAYSIZE(pLoginResult->m_szPhoneNumber), JsonRoot["phonenumber"].asCString());
                    strcpy_s(pLoginResult->m_szMail, ARRAYSIZE(pLoginResult->m_szMail), JsonRoot["mail"].asCString());
                }
            }
            else if (nRetCode == 102)
                pLoginResult->m_LoginResultCode = LOGIN_UNREGISTERED;
            else if (nRetCode == 103)
                pLoginResult->m_LoginResultCode = LOGIN_PASSWORD_ERROR;
            else
                pLoginResult->m_LoginResultCode = LOGIN_FAILED;
        }    
    }
    //m_lpUserMgr为野指针
    ::PostMessage(pLoginDlg->m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_LOGIN_RESULT, 0, (LPARAM)pLoginResult);

    return 1;
}

void CLoginDlg::UninitUI()
{
	if (m_cboUid.IsWindow())
		m_cboUid.DestroyWindow();

	if (m_edtPwd.IsWindow())
		m_edtPwd.DestroyWindow();

	if (m_btnRememberPwd.IsWindow())
		m_btnRememberPwd.DestroyWindow();

	if (m_btnAutoLogin.IsWindow())
		m_btnAutoLogin.DestroyWindow();

	if (m_lnkRegAccount.IsWindow())
		m_lnkRegAccount.DestroyWindow();

	if (m_lnkLostPwd.IsWindow())
		m_lnkLostPwd.DestroyWindow();


	if (m_btnSet.IsWindow())
		m_btnSet.DestroyWindow();

	if (m_btnLogin.IsWindow())
		m_btnLogin.DestroyWindow();

	if (m_btnLoginStatus.IsWindow())
		m_btnLoginStatus.DestroyWindow();

	if (m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	m_SkinMenu.DestroyMenu();
}

// 从菜单ID获取对应的UTalk_STATUS
long CLoginDlg::GetStatusFromMenuID(int nMenuID)
{
	switch (nMenuID)
	{
	case ID_MENU_IMONLINE:
		return STATUS_ONLINE;
	case ID_MENU_IMOFFLINE:
		return STATUS_OFFLINE;
	default:
		return STATUS_OFFLINE;
	}
}

// 根据指定状态设置状态菜单按钮的图标
void CLoginDlg::StatusMenuBtn_SetIconPic(CSkinButton& btnStatus, long nStatus)
{
	LPCTSTR lpszFileName;

	switch (nStatus)
	{
	case STATUS_ONLINE:
		lpszFileName = _T("Status\\imonline.png");
		break;
	case STATUS_OFFLINE:
		lpszFileName = _T("Status\\imoffline.png");
		break;
	default:
		return;
	}

	btnStatus.SetIconPic(lpszFileName);
	btnStatus.Invalidate();
}

void CLoginDlg::DoLogin()
{
    HANDLE hLoginThread = (HANDLE)::_beginthreadex(NULL, 0, LoginThreadProc, this, 0, NULL);
    if (hLoginThread != NULL)
        ::CloseHandle(hLoginThread);
}