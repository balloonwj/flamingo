#include "stdafx.h"
#include "MobileStatusDlg.h"
#include "Utils.h"
#include "GDIFactory.h"

#define ID_AUTO_DISAPPEAR_TIMER_ID 0xD0

CMobileStatusDlg::CMobileStatusDlg(void)
{
	memset(m_szWindowTitle, 0, sizeof(m_szWindowTitle));
	memset(m_szInfoText, 0, sizeof(m_szInfoText));

	m_bEnableAutoDisappear = TRUE;
	m_TimerID = -1;
}

CMobileStatusDlg::~CMobileStatusDlg(void)
{
}

void CMobileStatusDlg::SetWindowTitle(PCTSTR pszTitle)
{
	_tcscpy_s(m_szWindowTitle, ARRAYSIZE(m_szWindowTitle), pszTitle);
}

void CMobileStatusDlg::SetInfoText(PCTSTR pszInfoText)
{
	_tcscpy_s(m_szInfoText, ARRAYSIZE(m_szInfoText), pszInfoText);
}

void CMobileStatusDlg::EnableAutoDisappear(BOOL bEnable)
{
	m_bEnableAutoDisappear = bEnable;
}

BOOL CMobileStatusDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitUI();
	
	//::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE/*|SWP_SHOWWINDOW*/);

	if(m_bEnableAutoDisappear)
	{
		m_TimerID = SetTimer(ID_AUTO_DISAPPEAR_TIMER_ID, 10000, NULL);
		//修改风格使得他不在任务栏显示
		ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);
	}

	return TRUE;
}

void CMobileStatusDlg::OnTimer(UINT_PTR nIDEvent)
{
	if(m_TimerID != -1)
	{
		KillTimer(m_TimerID);
		m_TimerID = -1;
		EndDialog(IDCANCEL);
	}
}

void CMobileStatusDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CMobileStatusDlg::OnDestroy()
{
	UninitUI();
}

void CMobileStatusDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\MsgBoxDlgBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	CRect rtTaskbar;
	GetTaskbarDirectionAndRect(rtTaskbar);
	if(m_bEnableAutoDisappear)
		m_SkinDlg.MoveWindow(::GetSystemMetrics(SM_CXSCREEN)-230, ::GetSystemMetrics(SM_CYSCREEN)-rtTaskbar.Height()-155, 220, 155, FALSE);
	else
		CenterWindow(GetParent());
	m_SkinDlg.SetTitleText(m_szWindowTitle);

	HFONT hFont = CGDIFactory::GetFont(18);
	m_staInfo.SubclassWindow(GetDlgItem(IDC_STATIC_INFO));
	m_staInfo.SetTransparent(TRUE, m_SkinDlg.GetBgDC());
	m_staInfo.SetWindowText(m_szInfoText);
	m_staInfo.SetFont(hFont);

}

void CMobileStatusDlg::UninitUI()
{
	
}