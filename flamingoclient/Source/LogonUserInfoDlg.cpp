#include "stdafx.h"
#include "LogonUserInfoDlg.h"
#include "ChangePicHeadDlg.h"
#include "FlamingoClient.h"
#include "Utils.h"
#include "EncodeUtil.h"
#include "UserSessionData.h"
#include "Path.h"
#include "UIText.h"

CLogonUserInfoDlg::CLogonUserInfoDlg(void)
{
	m_pFMGClient = NULL;
	m_uUserID = 0;
	m_uSysFaceID = 0;
	m_hDlgIcon = NULL;
	m_hDlgSmallIcon = NULL;
	memset(m_szCustomFacePath, 0, sizeof(m_szCustomFacePath));
	m_bUseCustomThumb = FALSE;
	memset(m_szCustomFaceRemotePath, 0, sizeof(m_szCustomFaceRemotePath));
}

CLogonUserInfoDlg::~CLogonUserInfoDlg(void)
{
}

BOOL CLogonUserInfoDlg::PreTranslateMessage(MSG* pMsg)
{
	//支持Esc关闭对话框
	if (pMsg->hwnd==m_hWnd && pMsg->message==WM_KEYDOWN && pMsg->wParam==VK_ESCAPE)
 	{
		PostMessage(WM_CLOSE, 0, 0);
		return TRUE;
 	}

	return CWindow::IsDialogMessage(pMsg);
}

BOOL CLogonUserInfoDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	
	SetWindowPos(::GetForegroundWindow(), 0, 0, 420, 420, SWP_NOMOVE);

	m_hDlgIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	Init();		

	UpdateCtrlData();

	CenterWindow();

	return TRUE;
}

void CLogonUserInfoDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if(bShow)
		UpdateCtrlData();
}

void CLogonUserInfoDlg::OnClose()
{
	//EndDialog(IDCANCEL);
	ShowWindow(SW_HIDE);
}

void CLogonUserInfoDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	UnInit();	// 反初始化

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

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}

// “更改用户头像”控件
void CLogonUserInfoDlg::OnSysHead(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CChangePicHeadDlg changePicHeadDlg;
	changePicHeadDlg.SetSelection(m_pFMGClient->m_UserMgr.m_UserInfo.m_nFace);
	if(changePicHeadDlg.DoModal(m_hWnd, NULL) != IDOK)
		return;
		
	long nSelected = changePicHeadDlg.GetSelection();
	if(nSelected < 0)
		return;
	
	CString strThumbPath;
	strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, nSelected);
	m_picHead.SetBitmap(strThumbPath);
	m_picHead.Invalidate();

	m_uSysFaceID = (UINT)nSelected;
	m_bUseCustomThumb = FALSE;
}

void CLogonUserInfoDlg::OnCustomHead(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	BOOL bOpenFileDialog = TRUE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = NULL;
	DWORD dwFlags = OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_EXTENSIONDIFFERENT;
	LPCTSTR lpszFilter = _T("图像文件(*.bmp;*.jpg;*.jpeg;*.gif;*.png)\0*.bmp;*.jpg;*.jpeg;*.gif;*.png\0\0");
	HWND hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("选择图片");
	if (fileDlg.DoModal() != IDOK)
		return;
	
    //暂且将头像格式都转换成jpg
	CString strDestPath;
	CString strDumyPath;
	strDestPath.Format(_T("%s%u.png"), m_pFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), m_pFMGClient->m_UserMgr.m_UserInfo.m_uUserID);
	::CopyFile(fileDlg.m_ofn.lpstrFile, strDestPath, FALSE);
	TransformImage(strDestPath, _T(".png"), 64, 64, strDumyPath);
	
	_tcscpy_s(m_szCustomFacePath, ARRAYSIZE(m_szCustomFacePath), strDestPath);
	//上传自定义头像
	CFileItemRequest* pFileItemRequest = new CFileItemRequest();
	_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), strDestPath);
	pFileItemRequest->m_nFileType = FILE_ITEM_UPLOAD_USER_THUMB;
	pFileItemRequest->m_hwndReflection = m_hWnd;
    m_pFMGClient->m_ImageTask.AddItem(pFileItemRequest);
	m_btnOK.EnableWindow(FALSE);
	m_btnCancel.EnableWindow(FALSE);
	m_btnSysHead.EnableWindow(FALSE);
	m_btnCustomHead.EnableWindow(FALSE);
	m_SkinDlg.SetTitleText(_T("我的资料(正在上传头像，请稍等...)"));
}

void CLogonUserInfoDlg::OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString strNickName;
	m_edtNickName.GetWindowText(strNickName);
	strNickName.Trim();
	if(strNickName.GetLength()<=0 || strNickName.GetLength()>16)
	{
		::MessageBox(m_hWnd, _T("昵称必须在1～16个字符之间。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	CString strSignature;
	m_edtSign.GetWindowText(strSignature);
	strSignature.Trim();
	if(strSignature.GetLength()>128)
	{
		::MessageBox(m_hWnd, _T("个性签名必须在0～128个字符之间。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	UINT uGender = m_btnGenderFemale.GetCheck() ? 1 : 0;

	SYSTEMTIME st;
	m_dtpBirthday.GetSystemTime(&st);
	if(st.wYear < 1900)
	{
		::MessageBox(m_hWnd, _T("生日年份必须大于1990年。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}
	long nBirthday = st.wYear*10000 + st.wMonth*100 + st.wDay;

	CString strAddress;
	m_edtAddress.GetWindowText(strAddress);
	strAddress.Trim();
	if(strAddress.GetLength() > 32)
	{
		::MessageBox(m_hWnd, _T("地址长度不能超过32位。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	CString strPhone;
	m_edtPhone.GetWindowText(strPhone);
	strPhone.Trim();
	if(strPhone.GetLength() > 16)
	{
		::MessageBox(m_hWnd, _T("电话长度不能超过16位。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}
	if(strPhone.GetLength()>0 && !ValidatePhone(strPhone))
	{
		::MessageBox(m_hWnd, _T("电话号码必须是数字、-和+。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	CString strMail;
	m_edtMail.GetWindowText(strMail);
	strMail.Trim();
	if(strMail.GetLength() >= 24)
	{
		::MessageBox(m_hWnd, _T("邮箱长度不能超过23位。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	m_pFMGClient->UpdateLogonUserInfo(strNickName, strSignature, uGender, nBirthday, strAddress, strPhone, strMail, m_uSysFaceID, m_szCustomFaceRemotePath, m_bUseCustomThumb);
	
	
	ShowWindow(SW_HIDE);
}

void CLogonUserInfoDlg::OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ShowWindow(SW_HIDE);
}

LRESULT CLogonUserInfoDlg::OnUploadUserThumbResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_btnOK.EnableWindow(TRUE);
	m_btnCancel.EnableWindow(TRUE);
	m_btnSysHead.EnableWindow(TRUE);
	m_btnCustomHead.EnableWindow(TRUE);
	m_SkinDlg.SetTitleText(_T("我的资料"));
	
	CUploadFileResult* pResult = (CUploadFileResult*)lParam;
	if(pResult == NULL)
		return 0;
	
	if(wParam == UPLOAD_USER_THUMB_RESULT_FAILED)
	{
		::MessageBox(m_hWnd, _T("上传头像失败！"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR);
		
	}
	else if(wParam == UPLOAD_USER_THUMB_RESULT_SUCCESS)
	{
		m_picHead.SetBitmapWithoutCache(m_szCustomFacePath);
		m_picHead.Invalidate();
		m_bUseCustomThumb = TRUE;

		TCHAR szData[MAX_PATH] = {0};
        EncodeUtil::Utf8ToUnicode(pResult->m_szRemoteName, szData, ARRAYSIZE(szData));
		_tcscpy_s(m_szCustomFaceRemotePath, ARRAYSIZE(m_szCustomFaceRemotePath), szData);
	}

	
	return (LRESULT)1;
}

void CLogonUserInfoDlg::UpdateCtrlData()
{
	//账户名
	SetDlgItemText(IDC_ACCOUNTNAME, m_pFMGClient->m_UserMgr.m_UserInfo.m_strAccount.c_str());
	
	m_bUseCustomThumb = m_pFMGClient->m_UserMgr.m_UserInfo.m_bUseCustomFace;
	CString strThumbPath;
	if(m_bUseCustomThumb)
	{
		//_tcscpy_s(m_szCustomFaceRemotePath, ARRAYSIZE(m_szCustomFaceRemotePath), m_pFMGClient->m_UserMgr.m_UserInfo.m_strRawCustomFace.c_str());
		strThumbPath.Format(_T("%s%d.png"), m_pFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), m_pFMGClient->m_UserMgr.m_UserInfo.m_uUserID);
		if(!Hootina::CPath::IsFileExist(strThumbPath))
		{
			m_pFMGClient->m_UserMgr.m_UserInfo.m_bUseCustomFace = FALSE;
			strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, m_pFMGClient->m_UserMgr.m_UserInfo.m_nFace);
			memset(m_szCustomFaceRemotePath, 0, sizeof(m_szCustomFaceRemotePath));
		}
	}
	else
	{
		strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, m_pFMGClient->m_UserMgr.m_UserInfo.m_nFace);
		m_uSysFaceID = m_pFMGClient->m_UserMgr.m_UserInfo.m_nFace;
	}
	
	m_picHead.SetBitmap(strThumbPath);

	m_edtNickName.SetWindowText(m_pFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str());

	m_edtSign.SetWindowText(m_pFMGClient->m_UserMgr.m_UserInfo.m_strSign.c_str());

	long nGender = m_pFMGClient->m_UserMgr.m_UserInfo.m_nGender;
	if(nGender != 0)
	{
		m_btnGenderMale.SetCheck(FALSE);
		m_btnGenderFemale.SetCheck(TRUE);	
	}
	else
	{
		m_btnGenderMale.SetCheck(TRUE);
		m_btnGenderFemale.SetCheck(FALSE);	
	}

	long nBirthday = m_pFMGClient->m_UserMgr.m_UserInfo.m_nBirthday;
	long nYear = nBirthday / 10000;
	long nMonth = (nBirthday - nYear*10000) / 100;
	long nDay = (nBirthday - nYear*10000 - nMonth*100);

	SYSTEMTIME st = {0};
	if(nYear<1990 || nMonth<1 || nMonth>12 || nDay<1 || nDay>31)
	{
		st.wYear = 1990;
		st.wMonth = 6;
		st.wDay = 9;
	}
	else
	{	
		st.wYear = (WORD)nYear;
		st.wMonth = (WORD)nMonth;
		st.wDay = (WORD)nDay;
	}
	m_dtpBirthday.SetSystemTime(GDT_VALID, &st);

	m_edtAddress.SetWindowText(m_pFMGClient->m_UserMgr.m_UserInfo.m_strAddress.c_str());

	m_edtPhone.SetWindowText(m_pFMGClient->m_UserMgr.m_UserInfo.m_strMobile.c_str());

	m_edtMail.SetWindowText(m_pFMGClient->m_UserMgr.m_UserInfo.m_strEmail.c_str());

	InvalidateRect(FALSE);
}

BOOL CLogonUserInfoDlg::ValidatePhone(const CString& strPhone)
{
	long nLength = strPhone.GetLength();
	TCHAR c;
	for(long i=0; i<nLength; ++i)
	{
		c = strPhone.GetAt(i);
		if((c<L'0' || c>L'9') && c!=L'+' && c!=L'-')
			return FALSE;
	}

	return TRUE;
}

// 初始化
BOOL CLogonUserInfoDlg::Init()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\GeneralBg.png"), CRect(4, 69, 4, 33));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), 
		_T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.MoveWindow(0, 0, 420, 420, FALSE);
	m_SkinDlg.SetTitleText(_T("我的资料"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_picHead.SubclassWindow(GetDlgItem(IDC_HEAD));
	m_picHead.SetTransparent(TRUE, hDlgBgDC);
	m_picHead.SetToolTipText(_T("当前头像"));

	m_btnSysHead.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnSysHead.SetTransparent(TRUE, hDlgBgDC);
	m_btnSysHead.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnSysHead.SubclassWindow(GetDlgItem(IDC_SYSHEAD));

	m_btnCustomHead.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCustomHead.SetTransparent(TRUE, hDlgBgDC);
	m_btnCustomHead.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCustomHead.SubclassWindow(GetDlgItem(IDC_CUSTOMHEAD));

	m_edtNickName.SetTransparent(TRUE, hDlgBgDC);
	m_edtNickName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtNickName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtNickName.SubclassWindow(GetDlgItem(IDC_NICKNAME));
	
	m_edtSign.SetTransparent(TRUE, hDlgBgDC);
	m_edtSign.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtSign.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtSign.SetMultiLine(TRUE);
	m_edtSign.SubclassWindow(GetDlgItem(IDC_SIGNATURE));
	//m_edtSign.SetMultiLine(TRUE);


	m_btnGenderMale.Attach(GetDlgItem(IDC_GENDERMALE));
	m_btnGenderFemale.Attach(GetDlgItem(IDC_GENDERFEMALE));

	m_dtpBirthday.Attach(GetDlgItem(IDC_BIRTHDAY));
	m_dtpBirthday.ModifyStyle(0, DTS_UPDOWN, 0);


	m_edtAddress.SetTransparent(TRUE, hDlgBgDC);
	m_edtAddress.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtAddress.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtAddress.SubclassWindow(GetDlgItem(IDC_ADDRESS));

	m_edtPhone.SetTransparent(TRUE, hDlgBgDC);
	m_edtPhone.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtPhone.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtPhone.SubclassWindow(GetDlgItem(IDC_PHONE));

	m_edtMail.SetTransparent(TRUE, hDlgBgDC);
	m_edtMail.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtMail.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtMail.SubclassWindow(GetDlgItem(IDC_MAIL));

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"), _T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SetRound(4, 4);
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"), _T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SetRound(4, 4);
	m_btnCancel.SubclassWindow(GetDlgItem(IDCANCEL));

	return TRUE;
}

// 反初始化
void CLogonUserInfoDlg::UnInit()
{
	if(m_edtNickName.IsWindow())
		m_edtNickName.DestroyWindow();

	if(m_btnSysHead.IsWindow())
		m_btnSysHead.DestroyWindow();

	if(m_btnCustomHead.IsWindow())
		m_btnCustomHead.DestroyWindow();

	if(m_edtSign.IsWindow())
		m_edtSign.DestroyWindow();

	if(m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	if(m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	if(m_edtAddress.IsWindow())
		m_edtAddress.DestroyWindow();

	if(m_edtPhone.IsWindow())
		m_edtPhone.DestroyWindow();

	if(m_edtMail.IsWindow())
		m_edtMail.DestroyWindow();

	if(m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if(m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}