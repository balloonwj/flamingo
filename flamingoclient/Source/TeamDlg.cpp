#include "stdafx.h"
#include "TeamDlg.h"
#include "FlamingoClient.h"
#include "UIText.h"


CTeamDlg::CTeamDlg()
{
	m_pFMGClient = NULL;
	m_nOperationType = TEAM_OPERATION_ADDTEAM;
	m_nTeamIndex = -1;
}

CTeamDlg::~CTeamDlg()
{

}

void CTeamDlg::SetType(TEAM_OPERATION_TYPE nType)
{
	m_nOperationType = nType;
}

BOOL CTeamDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitUI();
	CenterWindow(m_pFMGClient->m_UserMgr.m_hCallBackWnd);
	return TRUE;
}

BOOL CTeamDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\AddFriendDlgBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	if(m_nOperationType == TEAM_OPERATION_ADDTEAM)
	{
		m_SkinDlg.SetTitleText(_T("新建分组"));	
	}
	else if(m_nOperationType == TEAM_OPERATION_MODIFYTEAMNAME)
	{
		m_SkinDlg.SetTitleText(_T("修改分组名称"));	
	}
	m_SkinDlg.MoveWindow(0, 0, 250, 135, TRUE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_staTeamName.SetTransparent(TRUE, hDlgBgDC);
	m_staTeamName.SubclassWindow(GetDlgItem(IDC_STATIC_TEAMNAME));
	if(m_nOperationType == TEAM_OPERATION_ADDTEAM)
	{
		m_staTeamName.SetWindowText(_T("新分组名称"));
	}
	else if(m_nOperationType == TEAM_OPERATION_MODIFYTEAMNAME)
	{
		m_SkinDlg.SetTitleText(_T("分组新名称"));	
	}

	m_edtTeamName.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtTeamName.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_edtTeamName.SetTransparent(TRUE, hDlgBgDC);
	m_edtTeamName.SubclassWindow(GetDlgItem(IDC_EDIT_TEAMNAME));
	if(m_nOperationType == TEAM_OPERATION_MODIFYTEAMNAME)
	{
		CBuddyTeamInfo* pBuddyTeamInfo = m_pFMGClient->m_UserMgr.m_BuddyList.GetBuddyTeamByIndex(m_nTeamIndex);
		if(pBuddyTeamInfo != NULL)
		{		
			m_edtTeamName.SetWindowText(pBuddyTeamInfo->m_strName.c_str());
			m_edtTeamName.SetSel(0, pBuddyTeamInfo->m_strName.length()-1);
		}
	}
	
	

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


void CTeamDlg::UninitUI()
{
	if(m_staTeamName.IsWindow())
		m_staTeamName.DestroyWindow();

	if(m_edtTeamName.IsWindow())
		m_edtTeamName.DestroyWindow();

	if(m_btnOK.IsWindow())
		m_btnOK.DestroyWindow();

	if(m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}

void CTeamDlg::OnClose()
{
	EndDialog(IDCANCEL);
}
	
void CTeamDlg::OnDestroy()
{
	UninitUI();
}

void CTeamDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString strTeamName;
	m_edtTeamName.GetWindowText(strTeamName);
	strTeamName.Trim();
    if (strTeamName == DEFAULT_TEAMNAME)
    {
        ::MessageBox(m_hWnd, _T("[My Friends]是默认分组，不能修改。"), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

	if(strTeamName.IsEmpty())
	{
		::MessageBox(m_hWnd, _T("分组名不能为空。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}
	else if(strTeamName.GetLength() >= 16)
	{
		::MessageBox(m_hWnd, _T("分组名不能超过15个字符。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	if(m_pFMGClient->m_UserMgr.m_BuddyList.IsTeamNameExist(strTeamName))
	{
		::MessageBox(m_hWnd, _T("分组名已经存在！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	//添加分组
	if(m_nOperationType == TEAM_OPERATION_ADDTEAM)
	{
        m_pFMGClient->AddNewTeam(strTeamName);
	}
	else if(m_nOperationType == TEAM_OPERATION_MODIFYTEAMNAME)
	{
		if(m_nTeamIndex >= 0)
		{
			CBuddyTeamInfo* pTeamInfo = m_pFMGClient->m_UserMgr.m_BuddyList.GetBuddyTeamByIndex(m_nTeamIndex);
			if(pTeamInfo != NULL)
			{
                m_pFMGClient->ModifyTeamName(strTeamName, pTeamInfo->m_strName.c_str());
                //pTeamInfo->m_strName = strTeamName;
			}
		}
	}
	

	EndDialog(IDOK);
}

void CTeamDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}