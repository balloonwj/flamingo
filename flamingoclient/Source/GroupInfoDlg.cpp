#include "stdafx.h"
#include "GroupInfoDlg.h"

CGroupInfoDlg::CGroupInfoDlg(void)
{
	m_lpFMGClient = NULL;
	m_hMainDlg = NULL;
	m_nGroupCode = 0;
	m_hDlgIcon = m_hDlgSmallIcon = NULL;
}

CGroupInfoDlg::~CGroupInfoDlg(void)
{
}

BOOL CGroupInfoDlg::PreTranslateMessage(MSG* pMsg)
{
	//支持回车键查找
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam==VK_ESCAPE)
 	{
		PostMessage(WM_CLOSE, 0, 0);
		return TRUE;
 	}

	return CWindow::IsDialogMessage(pMsg);
}

BOOL CGroupInfoDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	
	SetWindowPos(NULL, 0, 0, 400, 470, SWP_NOMOVE);

	CenterWindow();

	// set icons
	m_hDlgIcon = AtlLoadIconImage(IDI_GROUPCHATDLG_32, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDI_GROUPCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	Init();		// 初始化

	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		if (!lpGroupInfo->IsHasGroupInfo())
			m_lpFMGClient->UpdateGroupInfo(m_nGroupCode);
		if (!lpGroupInfo->IsHasGroupNumber())
			m_lpFMGClient->UpdateGroupNum(m_nGroupCode);

		CString strTitle;
		strTitle.Format(_T("群设置 - %s"), lpGroupInfo->m_strName.c_str());
		m_SkinDlg.SetTitleText(strTitle);
	}

	UpdateCtrls();

	return TRUE;
}

void CGroupInfoDlg::OnClose()
{
	::PostMessage(m_hMainDlg, WM_CLOSE_GROUPINFODLG, m_nGroupCode, NULL);
}

void CGroupInfoDlg::OnDestroy()
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

// “确定”按钮
void CGroupInfoDlg::OnBtn_Ok(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	SendMessage(WM_CLOSE);
}

// “取消”按钮
void CGroupInfoDlg::OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	SendMessage(WM_CLOSE);
}

// 更新群信息
void CGroupInfoDlg::OnUpdateGroupInfo()
{
	UpdateCtrls();
}

// 更新群号码
void CGroupInfoDlg::OnUpdateGroupNumber()
{
	UpdateCtrls();
}

CGroupInfo* CGroupInfoDlg::GetGroupInfoPtr()
{
	if (NULL == m_lpFMGClient)
		return NULL;

	CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
	if (lpGroupList != NULL)
		return lpGroupList->GetGroupByCode(m_nGroupCode);
	else
		return NULL;
}

void CGroupInfoDlg::UpdateCtrls()
{
	CString strText;

	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		SetDlgItemText(ID_EDIT_NAME, lpGroupInfo->m_strName.c_str());
		strText.Format(_T("%s"), lpGroupInfo->m_strAccount.c_str());
		SetDlgItemText(ID_EDIT_NUMBER, strText);
		CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByUin(lpGroupInfo->m_nOwnerUin);
		//if (lpBuddyInfo != NULL)
		//	SetDlgItemText(ID_EDIT_CREATER, lpBuddyInfo->m_strNickName.c_str());
		SetDlgItemText(ID_EDIT_CREATER, _T("zhangyl"));
		TCHAR cTime[32] = {0};
		FormatTime(lpGroupInfo->m_nCreateTime, _T("%Y-%m-%d"), cTime, sizeof(cTime)/sizeof(TCHAR));
		//SetDlgItemText(ID_EDIT_CREATETIME, cTime);
		SetDlgItemText(ID_EDIT_CREATETIME, _T("2015年12月24日"));
		strText.Format(_T("%u"), lpGroupInfo->m_nClass);
		SetDlgItemText(ID_EDIT_CLASS, _T(""));
		SetDlgItemText(ID_EDIT_REMARK, _T(""));
		SetDlgItemText(ID_EDIT_MEMO, lpGroupInfo->m_strMemo.c_str());
		SetDlgItemText(ID_EDIT_FINGERMEMO, lpGroupInfo->m_strFingerMemo.c_str());

		lpBuddyInfo = m_lpFMGClient->GetUserInfo();
		if (lpBuddyInfo != NULL)
		{
			lpBuddyInfo = lpGroupInfo->GetMemberByUin(lpBuddyInfo->m_uUserID);
			if (lpBuddyInfo != NULL)
			{
				SetDlgItemText(ID_EDIT_CARDNAME, lpBuddyInfo->m_strNickName.c_str());
				SetDlgItemText(ID_EDIT_GENDER, lpBuddyInfo->m_nGender==0? _T("男"):_T("女"));
				SetDlgItemText(ID_EDIT_PHONE, lpBuddyInfo->m_strMobile.c_str());
				SetDlgItemText(ID_EDIT_EMAIL, lpBuddyInfo->m_strEmail.c_str());
				SetDlgItemText(ID_EDIT_REMARK2, _T(""));
			}
		}
	}
}

// 初始化
BOOL CGroupInfoDlg::Init()
{
	m_SkinDlg.SetBgPic(_T("LoginPanel_window_windowBkg.png"), CRect(40, 86, 152, 155));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), 
		_T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), 
		_T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_staName.SetTransparent(TRUE, hDlgBgDC);
	m_staName.SubclassWindow(GetDlgItem(ID_STATIC_NAME));
	m_staCreater.SetTransparent(TRUE, hDlgBgDC);
	m_staCreater.SubclassWindow(GetDlgItem(ID_STATIC_CREATER));
	m_staClass.SetTransparent(TRUE, hDlgBgDC);
	m_staClass.SubclassWindow(GetDlgItem(ID_STATIC_CLASS));
	m_staNumber.SetTransparent(TRUE, hDlgBgDC);
	m_staNumber.SubclassWindow(GetDlgItem(ID_STATIC_NUMBER));
	m_staCreateTime.SetTransparent(TRUE, hDlgBgDC);
	m_staCreateTime.SubclassWindow(GetDlgItem(ID_STATIC_CREATETIME));
	m_staRemark.SetTransparent(TRUE, hDlgBgDC);
	m_staRemark.SubclassWindow(GetDlgItem(ID_STATIC_REMARK));
	m_staMemo.SetTransparent(TRUE, hDlgBgDC);
	m_staMemo.SubclassWindow(GetDlgItem(ID_STATIC_MEMO));
	m_staFingerMemo.SetTransparent(TRUE, hDlgBgDC);
	m_staFingerMemo.SubclassWindow(GetDlgItem(ID_STATIC_FINGERMEMO));
	m_staCardName.SetTransparent(TRUE, hDlgBgDC);
	m_staCardName.SubclassWindow(GetDlgItem(ID_STATIC_CARDNAME));
	m_staPhone.SetTransparent(TRUE, hDlgBgDC);
	m_staPhone.SubclassWindow(GetDlgItem(ID_STATIC_PHONE));
	m_staGender.SetTransparent(TRUE, hDlgBgDC);
	m_staGender.SubclassWindow(GetDlgItem(ID_STATIC_GENDER));
	m_staEmail.SetTransparent(TRUE, hDlgBgDC);
	m_staEmail.SubclassWindow(GetDlgItem(ID_STATIC_EMAIL));
	m_staRemark2.SetTransparent(TRUE, hDlgBgDC);
	m_staRemark2.SubclassWindow(GetDlgItem(ID_STATIC_REMARK2));

	m_edtName.SetTransparent(TRUE, hDlgBgDC);
	m_edtName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtName.SubclassWindow(GetDlgItem(ID_EDIT_NAME));
	m_edtCreater.SetTransparent(TRUE, hDlgBgDC);
	m_edtCreater.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtCreater.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtCreater.SubclassWindow(GetDlgItem(ID_EDIT_CREATER));
	m_edtClass.SetTransparent(TRUE, hDlgBgDC);
	m_edtClass.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtClass.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtClass.SubclassWindow(GetDlgItem(ID_EDIT_CLASS));
	m_edtNumber.SetTransparent(TRUE, hDlgBgDC);
	m_edtNumber.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtNumber.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtNumber.SubclassWindow(GetDlgItem(ID_EDIT_NUMBER));
	m_edtCreateTime.SetTransparent(TRUE, hDlgBgDC);
	m_edtCreateTime.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtCreateTime.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtCreateTime.SubclassWindow(GetDlgItem(ID_EDIT_CREATETIME));
	m_edtRemark.SetTransparent(TRUE, hDlgBgDC);
	m_edtRemark.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtRemark.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtRemark.SubclassWindow(GetDlgItem(ID_EDIT_REMARK));
	m_edtMemo.SetTransparent(TRUE, hDlgBgDC);
	m_edtMemo.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtMemo.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtMemo.SetMultiLine(TRUE);
	m_edtMemo.SubclassWindow(GetDlgItem(ID_EDIT_MEMO));
	m_edtFingerMemo.SetTransparent(TRUE, hDlgBgDC);
	m_edtFingerMemo.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtFingerMemo.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtFingerMemo.SetMultiLine(TRUE);
	m_edtFingerMemo.SubclassWindow(GetDlgItem(ID_EDIT_FINGERMEMO));
	m_edtCardName.SetTransparent(TRUE, hDlgBgDC);
	m_edtCardName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtCardName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtCardName.SubclassWindow(GetDlgItem(ID_EDIT_CARDNAME));
	m_edtPhone.SetTransparent(TRUE, hDlgBgDC);
	m_edtPhone.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtPhone.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtPhone.SubclassWindow(GetDlgItem(ID_EDIT_PHONE));
	m_edtGender.SetTransparent(TRUE, hDlgBgDC);
	m_edtGender.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtGender.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtGender.SubclassWindow(GetDlgItem(ID_EDIT_GENDER));
	m_edtEmail.SetTransparent(TRUE, hDlgBgDC);
	m_edtEmail.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtEmail.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtEmail.SubclassWindow(GetDlgItem(ID_EDIT_EMAIL));
	m_edtRemark2.SetTransparent(TRUE, hDlgBgDC);
	m_edtRemark2.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtRemark2.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtRemark2.SetMultiLine(TRUE);
	m_edtRemark2.SubclassWindow(GetDlgItem(ID_EDIT_REMARK2));

	m_btnOk.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOk.SetTransparent(TRUE, hDlgBgDC);
	m_btnOk.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnOk.SubclassWindow(GetDlgItem(ID_BTN_OK));

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SubclassWindow(GetDlgItem(ID_BTN_CANCEL));

	return TRUE;
}

// 反初始化
void CGroupInfoDlg::UnInit()
{
	if (m_staName.IsWindow())
		m_staName.DestroyWindow();
	if (m_staCreater.IsWindow())
		m_staCreater.DestroyWindow();
	if (m_staClass.IsWindow())
		m_staClass.DestroyWindow();
	if (m_staNumber.IsWindow())
		m_staNumber.DestroyWindow();
	if (m_staCreateTime.IsWindow())
		m_staCreateTime.DestroyWindow();
	if (m_staRemark.IsWindow())
		m_staRemark.DestroyWindow();
	if (m_staMemo.IsWindow())
		m_staMemo.DestroyWindow();
	if (m_staFingerMemo.IsWindow())
		m_staFingerMemo.DestroyWindow();
	if (m_staCardName.IsWindow())
		m_staCardName.DestroyWindow();
	if (m_staPhone.IsWindow())
		m_staPhone.DestroyWindow();
	if (m_staGender.IsWindow())
		m_staGender.DestroyWindow();
	if (m_staEmail.IsWindow())
		m_staEmail.DestroyWindow();
	if (m_staRemark2.IsWindow())
		m_staRemark2.DestroyWindow();

	if (m_edtName.IsWindow())
		m_edtName.DestroyWindow();
	if (m_edtCreater.IsWindow())
		m_edtCreater.DestroyWindow();
	if (m_edtClass.IsWindow())
		m_edtClass.DestroyWindow();
	if (m_edtNumber.IsWindow())
		m_edtNumber.DestroyWindow();
	if (m_edtCreateTime.IsWindow())
		m_edtCreateTime.DestroyWindow();
	if (m_edtRemark.IsWindow())
		m_edtRemark.DestroyWindow();
	if (m_edtMemo.IsWindow())
		m_edtMemo.DestroyWindow();
	if (m_edtFingerMemo.IsWindow())
		m_edtFingerMemo.DestroyWindow();
	if (m_edtCardName.IsWindow())
		m_edtCardName.DestroyWindow();
	if (m_edtPhone.IsWindow())
		m_edtPhone.DestroyWindow();
	if (m_edtGender.IsWindow())
		m_edtGender.DestroyWindow();
	if (m_edtEmail.IsWindow())
		m_edtEmail.DestroyWindow();
	if (m_edtRemark2.IsWindow())
		m_edtRemark2.DestroyWindow();

	if (m_btnOk.IsWindow())
		m_btnOk.DestroyWindow();
	if (m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}