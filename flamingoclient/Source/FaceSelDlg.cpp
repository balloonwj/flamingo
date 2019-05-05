#include "stdafx.h"
#include "FaceSelDlg.h"

CFaceSelDlg::CFaceSelDlg(void)
{
	m_lpFaceList = NULL;
	m_nSelFaceId = -1;
	m_nSelFaceIndex = -1;
	m_strSelFaceFileName = _T("");
}

CFaceSelDlg::~CFaceSelDlg(void)
{
}

void CFaceSelDlg::SetFaceList(CFaceList* lpFaceList)
{
	m_lpFaceList = lpFaceList;
}

int CFaceSelDlg::GetSelFaceId()
{
	return m_nSelFaceId;
}

int CFaceSelDlg::GetSelFaceIndex()
{
	return m_nSelFaceIndex;
}

CString CFaceSelDlg::GetSelFaceFileName()
{
	return m_strSelFaceFileName;
}

BOOL CFaceSelDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	m_FaceCtrl.SetBgColor(RGB(255, 255, 255));
	m_FaceCtrl.SetLineColor(RGB(223, 230, 246));
	m_FaceCtrl.SetFocusBorderColor(RGB(0, 0, 255));
	m_FaceCtrl.SetZoomBorderColor(RGB(0, 138, 255));
	m_FaceCtrl.SetRowAndCol(10, 15);
	m_FaceCtrl.SetItemSize(28, 28);
	m_FaceCtrl.SetZoomSize(84, 84);
	m_FaceCtrl.SetFaceList(m_lpFaceList);
	m_FaceCtrl.SetCurPage(0);

	CRect rcFaceCtrl(rcClient);
	rcFaceCtrl.left += 6;
	rcFaceCtrl.right = rcFaceCtrl.left + 420;
	rcFaceCtrl.top += 6;
	rcFaceCtrl.bottom = rcFaceCtrl.top + 280;
	m_FaceCtrl.Create(m_hWnd, rcFaceCtrl, NULL, WS_CHILD | WS_VISIBLE, 
		NULL, ID_FACE_CTRL, NULL);

	m_nSelFaceId = -1;
	m_nSelFaceIndex = -1;
	m_strSelFaceFileName = _T("");

	ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);

	return TRUE;
}

void CFaceSelDlg::OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther)
{
	SetMsgHandled(FALSE);

	if (WA_INACTIVE == nState)
	{
		PostMessage(WM_CLOSE, NULL, NULL);
	}
}

void CFaceSelDlg::OnPaint(CDCHandle dc)
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

void CFaceSelDlg::OnClose()
{
	::PostMessage(GetParent(), FACE_CTRL_SEL, NULL, NULL);
	DestroyWindow();
}

void CFaceSelDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	m_FaceCtrl.DestroyWindow();
}

LRESULT CFaceSelDlg::OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int nSelIndex = (int)lParam;
	CFaceInfo* lpFaceInfo = m_FaceCtrl.GetFaceInfo(nSelIndex);
	if (lpFaceInfo != NULL)
	{
		m_nSelFaceId = lpFaceInfo->m_nId;
		m_nSelFaceIndex = lpFaceInfo->m_nIndex;
		m_strSelFaceFileName = lpFaceInfo->m_strFileName.c_str();
	}
	PostMessage(WM_CLOSE, NULL, NULL);
	return 0;
}