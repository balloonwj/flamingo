#include "stdafx.h"
#include "ChangePicHead.h"


CChangePicHead::CChangePicHead()
{

};

CChangePicHead::~CChangePicHead()
{

};


BOOL CChangePicHead::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);
	Init();
	return TRUE;
}

BOOL CChangePicHead::Init()
{

	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetBgPic(_T("BuddyChatDlgBg.png"), CRect(4, 69 + 32, 4, 33));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), 
		_T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetMaxSysBtnPic(_T("SysBtn\\btn_max_normal.png"), 
		_T("SysBtn\\btn_max_highlight.png"), _T("SysBtn\\btn_max_down.png"));
	m_SkinDlg.SetRestoreSysBtnPic(_T("SysBtn\\btn_restore_normal.png"), 
		_T("SysBtn\\btn_restore_highlight.png"), _T("SysBtn\\btn_restore_down.png"));
	m_SkinDlg.MoveWindow(0, 0, 587, 535, TRUE);
	CenterWindow();
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), 
		_T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	CRect rcClient;
	GetClientRect(&rcClient);

	InitTabCtrl();			// 初始化Tab栏

	m_btnChange.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnChange.SetTransparent(TRUE, hDlgBgDC);
	m_btnChange.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnChange.SubclassWindow(GetDlgItem(IDOK));
	m_btnChange.MoveWindow(rcClient.right-158-27-4, rcClient.bottom-4-27, 77, 27, FALSE);

	m_btnUnChange.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnUnChange.SetTransparent(TRUE, hDlgBgDC);
	m_btnUnChange.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnUnChange.SubclassWindow(GetDlgItem(IDCANCEL));
	m_btnUnChange.MoveWindow(rcClient.right-158-27-4+77+20, rcClient.bottom-4-27, 77, 27, FALSE);

	return TRUE;
}


// 初始化Tab栏
BOOL CChangePicHead::InitTabCtrl()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	//int nWidth = (rcClient.Width()-2) / 3;
	//int nRemainder = (rcClient.Width()-2) % 3;

	int nWidth = (rcClient.Width()-2) / 2;
	int nRemainder = (rcClient.Width()-2) % 2;

	int nIndex = m_TabCtrl.AddItem(1, STCI_STYLE_DROPDOWN);
	m_TabCtrl.SetItemSize(nIndex, nWidth, 48, nWidth-19, 32);
	m_TabCtrl.SetItemToolTipText(nIndex, _T("自定义头像"));
	m_TabCtrl.SetItemText(1, _T("自定义头像"));
	//m_TabCtrl.SetItemIconPic(nIndex, _T("MainTabCtrl\\icon_contacts_normal.png"), _T("MainTabCtrl\\icon_contacts_selected.png"));

	nIndex = m_TabCtrl.AddItem(2, STCI_STYLE_DROPDOWN);
	m_TabCtrl.SetItemSize(nIndex, nWidth, 48, nWidth-19, 32);
	m_TabCtrl.SetItemToolTipText(nIndex, _T("经典头像"));
	m_TabCtrl.SetItemText(2, _T("经典头像"));
	//m_TabCtrl.SetItemIconPic(nIndex, _T("MainTabCtrl\\icon_group_normal.png"), _T("MainTabCtrl\\icon_group_selected.png"));

	//nIndex = m_TabCtrl.AddItem(3, STCI_STYLE_DROPDOWN);
	//m_TabCtrl.SetItemSize(nIndex, nWidth+nRemainder, 48, nWidth+nRemainder-19, 32);
	//m_TabCtrl.SetItemToolTipText(nIndex, _T("大客户头像"));
	//m_TabCtrl.SetItemText(3, _T("大客户头像"));
	//m_TabCtrl.SetItemIconPic(nIndex, _T("MainTabCtrl\\icon_last_normal.png"), _T("MainTabCtrl\\icon_last_selected.png"));

	//m_TabCtrl.SetBgPic(_T("MainTabCtrl\\main_tab_bkg.png"), CRect(5,1,5,1));
	m_TabCtrl.SetItemsBgPic(NULL, _T("MainTabCtrl\\main_tab_highlight.png"), _T("MainTabCtrl\\main_tab_check.png"), CRect(5,1,5,1));
	//m_TabCtrl.SetItemsArrowPic(_T("MainTabCtrl\\main_tabbtn_highlight.png"), _T("MainTabCtrl\\main_tabbtn_down.png"));

	m_TabCtrl.SetTransparent(TRUE, m_SkinDlg.GetBgDC());

	CRect rcTabCtrl(2, 69, rcClient.right - 2, 69 + 32);
	m_TabCtrl.Create(m_hWnd, rcTabCtrl, NULL, WS_CHILD | WS_VISIBLE, NULL, ID_TABCTRL_MAIN, NULL);

	m_TabCtrl.SetCurSel(0);

	return TRUE;
}


void CChangePicHead::OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther)
{
	SetMsgHandled(FALSE);

	if (WA_INACTIVE == nState)
	{
		PostMessage(WM_CLOSE, NULL, NULL);
	}
}

void CChangePicHead::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);

	HDC hDC = PaintDC.m_hDC;

	CRect rcClient;
	GetClientRect(&rcClient);

	HPEN hPen = ::CreatePen(PS_SOLID, 1, RGB(72,121,184));
	HBRUSH hBrush = ::CreateSolidBrush(RGB(233,246,254));
	HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
	HBRUSH hOldBrush = (HBRUSH)::SelectObject(hDC, hBrush);
	::Rectangle(hDC, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
	::SelectObject(hDC, hOldBrush);
	::SelectObject(hDC, hOldPen);
	::DeleteObject(hPen);
	::DeleteObject(hBrush);
}

void CChangePicHead::OnClose()
{
	//::PostMessage(GetParent(), FACE_CTRL_SEL, NULL, NULL);
	EndDialog(IDCANCEL);
}

void CChangePicHead::OnDestroy()
{
	SetMsgHandled(FALSE);

	//m_FaceCtrl.DestroyWindow();
}