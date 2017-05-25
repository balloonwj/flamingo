#include "stdafx.h"
#include "SkinComboBox.h"

CSkinComboBox_Edit::CSkinComboBox_Edit(void)
{
	m_hOwnerWnd = NULL;
	m_bMouseTracking = FALSE;
	m_bIsDefText = FALSE;
	m_hBrush = NULL;
}

CSkinComboBox_Edit::~CSkinComboBox_Edit(void)
{
	if (m_hBrush != NULL)
	{
		::DeleteObject(m_hBrush);
		m_hBrush = NULL;
	}
}

void CSkinComboBox_Edit::SetOwnerWnd(HWND hWnd)
{
	m_hOwnerWnd = hWnd;
}

void CSkinComboBox_Edit::SetDefaultText(LPCTSTR lpszText)
{
	m_strDefText = lpszText;
}

BOOL CSkinComboBox_Edit::IsDefaultText()
{
	return m_bIsDefText;
}

void CSkinComboBox_Edit::SetDefaultTextMode(BOOL bIsDefText)
{
	if (bIsDefText == m_bIsDefText)
		return;

	m_bIsDefText = bIsDefText;
	if (m_bIsDefText)
	{
		SetWindowText(m_strDefText);
	}
	else
	{
		SetWindowText(_T(""));
	}
}

BOOL CSkinComboBox_Edit::SubclassWindow(HWND hWnd)
{
	__super::SubclassWindow(hWnd);

	SetDefaultText(m_strDefText);

// 	CRect rcClient;
// 	GetClientRect(&rcClient);
// 	rcClient.left += 200;
// 	this->SetRect(rcClient);
// 	SetMarginsEx(30, 0, 0, 0);

	return TRUE;
}

BOOL CSkinComboBox_Edit::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinComboBox_Edit::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	if (!m_bMouseTracking)
	{
		m_bMouseTracking = TRUE;
		StartTrackMouseLeave();

		if (::IsWindow(m_hOwnerWnd))
			::SendMessage(m_hOwnerWnd, WM_CBO_EDIT_MOUSE_HOVER, 0, 0);
	}
}

void CSkinComboBox_Edit::OnMouseLeave()
{
	SetMsgHandled(FALSE);

	m_bMouseTracking = FALSE;
	if (::IsWindow(m_hOwnerWnd))
		::SendMessage(m_hOwnerWnd, WM_CBO_EDIT_MOUSE_LEAVE, 0, 0);
}

void CSkinComboBox_Edit::OnSetFocus(CWindow wndOld)
{
	SetMsgHandled(FALSE);

	if (m_bIsDefText)
	{
		m_bIsDefText = FALSE;
		SetWindowText(_T(""));
	}
}

void CSkinComboBox_Edit::OnKillFocus(CWindow wndFocus)
{
	SetMsgHandled(FALSE);

	if (GetWindowTextLength() <= 0 && !m_strDefText.IsEmpty())
	{
		m_bIsDefText = TRUE;
		SetWindowText(m_strDefText);
	}
}

HBRUSH CSkinComboBox_Edit::OnCtlColor(CDCHandle dc, CEdit edit)
{
	CRect rcWindow;
	GetWindowRect(&rcWindow);

	CRect rcClient;
	GetClientRect(&rcClient);

	ClientToScreen(&rcClient);
	rcClient.OffsetRect(-rcWindow.left, -rcWindow.top);

	rcWindow.OffsetRect(-rcWindow.left, -rcWindow.top);
	
	if (NULL == m_hBrush)
		m_hBrush = ::CreateSolidBrush(RGB(255,255,255));
	
	if (m_bIsDefText)
		::SetTextColor(dc, RGB(128,128,128));
	else
		::SetTextColor(dc, RGB(0,0,0));

	return (HBRUSH)m_hBrush;
}

BOOL CSkinComboBox_Edit::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

// 设置上下左右边距函数
void CSkinComboBox_Edit::SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom)
{
	CRect rtClient;
	GetClientRect(rtClient);

	CRect rt(rtClient.left + nLeft, rtClient.top + nTop, 
		rtClient.right - nRight, rtClient.bottom - nBottom);
	SetRectNP(rt);
}

CSkinComboBox_ListBox::CSkinComboBox_ListBox(void)
{
	m_hOwnerWnd = NULL;
}

CSkinComboBox_ListBox::~CSkinComboBox_ListBox(void)
{

}

void CSkinComboBox_ListBox::SetOwnerWnd(HWND hWnd)
{
	m_hOwnerWnd = hWnd;
}

void CSkinComboBox_ListBox::OnShowWindow(BOOL bShow, UINT nStatus)
{
	SetMsgHandled(FALSE);

	if (!bShow)
	{
		::SendMessage(m_hOwnerWnd, WM_CBO_LIST_HIDE, 0, 0);
	}
}

CSkinComboBox::CSkinComboBox(void)
{
	m_lpBgImgN = NULL;
	m_lpBgImgH = NULL;
	m_lpArrowImgN = NULL;
	m_lpArrowImgH = NULL;
	m_lpArrowImgP = NULL;
	m_bFocus = m_bPress = m_bHover = m_bMouseTracking = FALSE;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
	m_bArrowPress = FALSE;
	m_bArrowHover = FALSE;
	m_nArrowWidth = 17;
	m_rcArrow.SetRectEmpty();
}

CSkinComboBox::~CSkinComboBox(void)
{
}

BOOL CSkinComboBox::SetBgNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBgImgN != NULL)
		m_lpBgImgN->SetNinePart(lpNinePart);
	return (m_lpBgImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinComboBox::SetBgHotPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBgImgH != NULL)
		m_lpBgImgH->SetNinePart(lpNinePart);
	return (m_lpBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinComboBox::SetArrowNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgN);
	m_lpArrowImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinComboBox::SetArrowHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	m_lpArrowImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinComboBox::SetArrowPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgP);
	m_lpArrowImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgP != NULL) ? TRUE : FALSE;
}

void CSkinComboBox::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

void CSkinComboBox::SetDefaultText(LPCTSTR lpszText)
{
	m_Edit.SetDefaultText(lpszText);
}

void CSkinComboBox::SetDefaultTextMode(BOOL bDefaultMode)
{
	m_Edit.SetDefaultTextMode(bDefaultMode);
}

BOOL CSkinComboBox::IsDefaultText()
{
	return m_Edit.IsDefaultText();
}

// 设置上下左右边距函数
void CSkinComboBox::SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom)
{
// 	CRect rtClient;
// 	GetClientRect(rtClient);
// 
// 	CRect rt(rtClient.left + nLeft, rtClient.top + nTop, 
// 		rtClient.right - nRight, rtClient.bottom - nBottom);
// 	SetRectNP(rt);
}

void CSkinComboBox::SetArrowWidth(int nWidth)
{
	m_nArrowWidth = nWidth;
}

BOOL CSkinComboBox::SubclassWindow(HWND hWnd)
{
	BOOL bRet = __super::SubclassWindow(hWnd);

 	COMBOBOXINFO stComboBoxInfo;
 	stComboBoxInfo.cbSize = sizeof(stComboBoxInfo);
 	GetComboBoxInfo(&stComboBoxInfo);

	m_Edit.SetOwnerWnd(m_hWnd);
	m_Edit.SubclassWindow(stComboBoxInfo.hwndItem);

	m_ListBox.SetOwnerWnd(m_hWnd);
	m_ListBox.SubclassWindow(stComboBoxInfo.hwndList);

	return TRUE;
}

void CSkinComboBox::SetReadOnly(BOOL bReadOnly)
{
	m_Edit.SetReadOnly(bReadOnly);
}

int CSkinComboBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	SetMsgHandled(FALSE);
	return 0;
}

BOOL CSkinComboBox::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinComboBox::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);

	HDC hDC = PaintDC.m_hDC;

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcArrow;
	HRGN hRgn2 = NULL;
	if (m_lpArrowImgN != NULL && !m_lpArrowImgN->IsNull())
	{
		int cxIcon = m_nArrowWidth;
		int cyIcon = m_lpArrowImgN->GetHeight();

		CalcCenterRect(rcClient, cxIcon, cyIcon, rcArrow);
		rcArrow.right = rcClient.right - 2;
		rcArrow.left = rcArrow.right - cxIcon;
	}

	//PaintDC.FillSolidRect(rcClient, RGB(255, 255, 255));

	if (m_bTransparent)
		DrawParentWndBg(hDC);

	if (m_bHover)
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
		{
			m_lpBgImgH->Draw2(hDC, rcClient);
		}
		else
		{
			if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
				m_lpBgImgN->Draw2(hDC, rcClient);
		}
	}
	else
	{
		if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
			m_lpBgImgN->Draw2(hDC, rcClient);
	}

	if (m_bArrowPress)
	{
		if (m_lpArrowImgP != NULL && !m_lpArrowImgP->IsNull())
			m_lpArrowImgP->Draw2(hDC, rcArrow);
	}
	else if (m_bArrowHover)
	{
		if (m_lpArrowImgH != NULL && !m_lpArrowImgH->IsNull())
			m_lpArrowImgH->Draw2(hDC, rcArrow);
	}
	else
	{
		if (m_lpArrowImgN != NULL && !m_lpArrowImgN->IsNull())
			m_lpArrowImgN->Draw2(hDC, rcArrow);
	}
}

void CSkinComboBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	//SetMsgHandled(FALSE);

	if (m_rcArrow.PtInRect(point))
	{
		if (!m_ListBox.IsWindowVisible())
		{
			m_bArrowPress = TRUE;
			Invalidate();
			ShowDropDown(TRUE);
		}
	}
}

void CSkinComboBox::OnLButtonUp(UINT nFlags, CPoint point)
{
	//SetMsgHandled(FALSE);
	
// 	if (m_bPress)
// 	{
// 		m_bPress = FALSE;
// 		::InvalidateRect(m_hWnd, NULL, TRUE);
// 	}
}

void CSkinComboBox::OnMouseMove(UINT nFlags, CPoint point)
{
	//SetMsgHandled(FALSE);

	BOOL bRePaint = FALSE;

	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
		m_bHover = TRUE;
		bRePaint = TRUE;
	}

	if (m_rcArrow.PtInRect(point))
	{
		if (!m_bArrowHover)
		{
			m_bArrowHover = TRUE;
			bRePaint = TRUE;
		}
	}
	else
	{
		if (m_bArrowHover)
		{
			m_bArrowHover = FALSE;
			bRePaint = TRUE;
		}
	}

	if (bRePaint)
		Invalidate();
}

void CSkinComboBox::OnMouseLeave()
{
	//SetMsgHandled(FALSE);

	m_bMouseTracking = FALSE;

	if (!m_ListBox.IsWindowVisible())
	{
		CPoint pt;
		GetCursorPos(&pt);

		CRect rcWindow;
		GetWindowRect(&rcWindow);

		if (!rcWindow.PtInRect(pt))
			m_bHover = FALSE;

		m_bArrowHover = FALSE;
		Invalidate();
	}
}

void CSkinComboBox::OnDestroy()
{
	SetMsgHandled(FALSE);

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgP);
}

void CSkinComboBox::OnSize(UINT nType, CSize size)
{
	DefWindowProc();

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcEdit;
	m_Edit.GetWindowRect(&rcEdit);
	ScreenToClient(&rcEdit);

	CDCHandle dc = m_Edit.GetDC();
	TEXTMETRIC tm = {0};
	dc.GetTextMetrics(&tm);
	int nFontHeight = tm.tmHeight + tm.tmExternalLeading;
	int nMargin = (rcEdit.Height() - nFontHeight) / 2;
	m_Edit.ReleaseDC(dc);

	rcEdit.DeflateRect(0, nMargin);
	rcEdit.right = rcClient.right - 2 - m_nArrowWidth;

	m_Edit.MoveWindow(&rcEdit, FALSE);

	m_rcArrow.left = rcClient.right - 2 - m_nArrowWidth;
	m_rcArrow.right = m_rcArrow.left + m_nArrowWidth;
	m_rcArrow.top = rcClient.top;
	m_rcArrow.bottom = rcClient.bottom;
}

LRESULT CSkinComboBox::OnEditMouseHover(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (!m_bHover)
	{
		m_bHover = TRUE;
		Invalidate();
	}

	return 0;
}

LRESULT CSkinComboBox::OnEditMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CPoint pt;
	GetCursorPos(&pt);

	CRect rcWindow;
	GetWindowRect(&rcWindow);

	if (!rcWindow.PtInRect(pt))
	{
		if (m_bHover)
		{
			m_bHover = FALSE;
			Invalidate();
		}
	}

	return 0;
}

LRESULT CSkinComboBox::OnListHide(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_bHover = FALSE;
	m_bArrowHover = FALSE;
	m_bArrowPress = FALSE;
	Invalidate();
	return 0;
}

BOOL CSkinComboBox::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinComboBox::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

void CSkinComboBox::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}