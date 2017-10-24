#include "stdafx.h"
#include "CreateNewGroupDlg.h"
#include "FlamingoClient.h"
#include "net/IUProtocolData.h"
#include "EncodingUtil.h"
#include "UIText.h"

// CCreateNewGroupDlg实现代码
CCreateNewGroupDlg::CCreateNewGroupDlg()
{
	m_pFMGClient = NULL;
}

CCreateNewGroupDlg::~CCreateNewGroupDlg()
{
}

BOOL CCreateNewGroupDlg::PreTranslateMessage(MSG* pMsg)
{
	//支持回车键查找
	if (pMsg->hwnd==m_hWnd && pMsg->message==WM_KEYDOWN && pMsg->wParam==VK_RETURN)
 	{
		PostMessage(WM_COMMAND, (WPARAM)IDOK, 0);
		return TRUE;
 	}

	if (pMsg->hwnd==m_hWnd && pMsg->message==WM_KEYDOWN && pMsg->wParam==VK_ESCAPE)
 	{
		PostMessage(WM_COMMAND, (WPARAM)IDCANCEL, 0);
		return TRUE;
 	}

	return CWindow::IsDialogMessage(pMsg);
}

BOOL CCreateNewGroupDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	AddMessageFilter();

	InitUI();

	CenterWindow();
	
	return TRUE;
}

BOOL CCreateNewGroupDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\CreateNewGroupBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("创建群"));	
	m_SkinDlg.MoveWindow(0, 0, 360, 255, TRUE);


	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_edtGroupName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtGroupName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtGroupName.SetTransparent(TRUE, hDlgBgDC);
	m_edtGroupName.SubclassWindow(GetDlgItem(IDC_EDIT_GROUPNAME));

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

void CCreateNewGroupDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString strGroupName;
	m_edtGroupName.GetWindowText(strGroupName);
	strGroupName.Trim();
	if(strGroupName.IsEmpty() || strGroupName.GetLength()<=0 || strGroupName.GetLength()>=16)
	{
		::MessageBox(m_hWnd, _T("群名称必须在1～16个字符之间。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		m_edtGroupName.SetFocus();
		return;
	}
	
	m_pFMGClient->SetCreateNewGroupWindow(m_hWnd);
	m_pFMGClient->CreateNewGroup(strGroupName);
	m_edtGroupName.EnableWindow(FALSE);
	m_btnOK.EnableWindow(FALSE);
	m_SkinDlg.SetTitleText(_T("创建群（正在向服务器发送数据，请稍候...）"));
}

void CCreateNewGroupDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}

LRESULT CCreateNewGroupDlg::OnCreateNewGroupResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_edtGroupName.EnableWindow(TRUE);
	m_btnOK.EnableWindow(TRUE);

	CCreateNewGroupResult* pResult = (CCreateNewGroupResult*)lParam;
	if(pResult==NULL || pResult->m_uError!=0)
	{
		::MessageBox(m_hWnd, _T("创建群失败。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
	}
	else
	{
		TCHAR szGroupName[64] = {0};
        EncodeUtil::Utf8ToUnicode(pResult->m_szGroupName, szGroupName, ARRAYSIZE(szGroupName));
		//TCHAR szAccountName[64] = {0};
		//EncodeUtil::Utf8ToUnicode(pResult->m_szAccount, szAccountName, ARRAYSIZE(szAccountName));
		CString strInfo;
        strInfo.Format(_T("成功创建群[%s], 群号是[%d]。\r\n你现在可以邀请其他人加入了。"), szGroupName, pResult->m_uAccountID);
		::MessageBox(m_hWnd, strInfo, g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);

		EndDialog(IDOK);
	}
	
	m_SkinDlg.SetTitleText(_T("创建群"));
	delete pResult;
		
	return (LRESULT)1;
}

void CCreateNewGroupDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CCreateNewGroupDlg::OnDestroy()
{
	UninitUI();
	RemoveMessageFilter();
}

void CCreateNewGroupDlg::UninitUI()
{
	if (m_edtGroupName.IsWindow())
		m_edtGroupName.DestroyWindow();

	if (m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if (m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();	
}

void CCreateNewGroupDlg::AddMessageFilter()
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
}

void CCreateNewGroupDlg::RemoveMessageFilter()
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}