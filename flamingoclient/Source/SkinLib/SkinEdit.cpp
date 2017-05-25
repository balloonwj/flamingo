#include "stdafx.h"
#include "SkinEdit.h"

CSkinEdit::CSkinEdit(void)
{
	m_lpBgImgN = NULL;
	m_lpBgImgH = NULL;
	m_lpIconImg = NULL;
	m_nIconWidth = 0;
	m_bFocus = m_bPress = m_bHover = m_bMouseTracking = FALSE;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
	m_bIsDefText = FALSE;
	m_cPwdChar = 0;
	m_bMultiLine = FALSE;
}

CSkinEdit::~CSkinEdit(void)
{
}

BOOL CSkinEdit::SetBgNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBgImgN != NULL)
		m_lpBgImgN->SetNinePart(lpNinePart);
	return (m_lpBgImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinEdit::SetBgHotPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBgImgH != NULL)
		m_lpBgImgH->SetNinePart(lpNinePart);
	return (m_lpBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinEdit::SetIconPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);
	m_lpIconImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpIconImg != NULL)
		m_nIconWidth = m_lpIconImg->GetWidth();
	return (m_lpIconImg != NULL) ? TRUE : FALSE;
}

void CSkinEdit::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

void CSkinEdit::SetDefaultText(LPCTSTR lpszText)
{
	m_strDefText = lpszText;
}

BOOL CSkinEdit::IsDefaultText()
{
	return m_bIsDefText;
}

void CSkinEdit::SetDefaultTextMode(BOOL bIsDefText)
{
	if (bIsDefText == m_bIsDefText)
		return;

	m_bIsDefText = bIsDefText;
	if (m_bIsDefText)
	{
		m_cPwdChar = GetPasswordChar();
		SetPasswordChar(0);
		SetWindowText(m_strDefText);
	}
	else
	{
		SetPasswordChar(m_cPwdChar);
		SetWindowText(_T(""));
	}
}

// 设置上下左右边距函数
void CSkinEdit::SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom)
{
	CRect rtClient;
	GetClientRect(rtClient);

	CRect rt(rtClient.left + nLeft, rtClient.top + nTop, 
		rtClient.right - nRight, rtClient.bottom - nBottom);
	SetRectNP(rt);
}

void CSkinEdit::SetMultiLine(BOOL bMultiLine/* = TRUE*/)
{
	m_bMultiLine = bMultiLine;
	if (!m_bMultiLine && IsWindow())		// 单行模式居中
		SetCenterInSingleLine();
}

BOOL CSkinEdit::SubclassWindow(HWND hWnd)
{
	__super::SubclassWindow(hWnd);

	SetDefaultText(m_strDefText);

	if (!m_bMultiLine)	// 单行模式居中
		SetCenterInSingleLine();

	return TRUE;
}

int CSkinEdit::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	SetMsgHandled(FALSE);
	return 0;
}

LRESULT CSkinEdit::OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam)
{
	SetMsgHandled(FALSE);

 	if (NULL == m_lpIconImg || m_lpIconImg->IsNull())
 		return 0;

	if (bCalcValidRects)
	{
		LPNCCALCSIZE_PARAMS pParam = (LPNCCALCSIZE_PARAMS)lParam;
		pParam->rgrc[0].right -= m_nIconWidth;
		pParam->rgrc[1] = pParam->rgrc[0];
	}   

	return 0;
}

BOOL CSkinEdit::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinEdit::OnNcPaint(CRgnHandle rgn)
{
	CRect rcWindow;
	GetWindowRect(&rcWindow);

	CRect rcClient;
	GetClientRect(&rcClient);

	ClientToScreen(&rcClient);
	rcClient.OffsetRect(-rcWindow.left, -rcWindow.top);

	rcWindow.OffsetRect(-rcWindow.left, -rcWindow.top);

	HDC hDC = ::GetWindowDC(m_hWnd);

// 	RECT rcClip = {0};
// 	::GetClipBox(hDC, &rcClip);
// 	HRGN hOldRgn = ::CreateRectRgnIndirect(&rcClip);
// 	HRGN hRgn = ::CreateRectRgnIndirect(&rcClient);
// 	::ExtSelectClipRgn(hDC, hRgn, RGN_DIFF);

	CRect rcIcon;
	HRGN hRgn2 = NULL;
	if (m_lpIconImg != NULL && !m_lpIconImg->IsNull())
	{
		int cxIcon = m_lpIconImg->GetWidth();
		int cyIcon = m_lpIconImg->GetHeight();

		CalcCenterRect(rcWindow, cxIcon, cyIcon, rcIcon);
		rcIcon.right = rcWindow.right - 2;
		rcIcon.left = rcIcon.right - cxIcon;

		//hRgn2 = ::CreateRectRgnIndirect(&rcIcon);
		//::ExtSelectClipRgn(hDC, hRgn2, RGN_OR);
	}

	if (m_bTransparent)
		DrawParentWndBg(hDC);

	if (m_bHover)
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
		{
			m_lpBgImgH->Draw2(hDC, rcWindow);
		}
		else
		{
			if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
				m_lpBgImgN->Draw2(hDC, rcWindow);
		}
	}
	else
	{
		if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
			m_lpBgImgN->Draw2(hDC, rcWindow);
	}

	if (m_lpIconImg != NULL && !m_lpIconImg->IsNull())
		m_lpIconImg->Draw2(hDC, rcIcon);

// 	::SelectClipRgn(hDC, hOldRgn);
// 	::DeleteObject(hOldRgn);
// 	::DeleteObject(hRgn);
// 	if (hRgn2 != NULL)
// 		::DeleteObject(hRgn2);

	::ReleaseDC(m_hWnd, hDC);




// 	HPEN hPen, hOldPen;
// 	HBRUSH hBrush, hOldBrush;
// 
// 	hPen = ::CreatePen(PS_INSIDEFRAME | PS_SOLID, 1, RGB(163, 194, 212));
// 	hBrush = ::CreateSolidBrush(RGB(255, 255, 255));
// 
// 	hOldPen = (HPEN)::SelectObject(hDC, hPen);
// 	hOldBrush = (HBRUSH)::SelectObject(hDC, hBrush);
// 	::Rectangle(hDC, rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.bottom);
// 	::SelectObject(hDC, hOldPen);
// 	::SelectObject(hDC, hOldBrush);
// 
// 	::DeleteObject(hPen);
// 	::DeleteObject(hBrush);
}

// void CSkinEdit::OnPaint(CDCHandle dc)
// {
// 	CPaintDC PaintDC(m_hWnd);
// 
// 	CRect rcClient;
// 	GetClientRect(&rcClient);
// 
// 	PaintDC.FillSolidRect(&rcClient, RGB(255,0,0));
// 
// 	SetMsgHandled(FALSE);
// }

HBRUSH CSkinEdit::OnCtlColor(CDCHandle dc, CEdit edit)
{
	//::SetBkMode(dc, TRANSPARENT);
	if (m_bIsDefText)
		::SetTextColor(dc, RGB(128,128,128));
	else
		::SetTextColor(dc, RGB(0,0,0));

	return (HBRUSH)NULL_BRUSH;
}

void CSkinEdit::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_bPress = TRUE;
	::InvalidateRect(m_hWnd, NULL, FALSE);
	SetMsgHandled(FALSE);
}

void CSkinEdit::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bPress)
	{
		m_bPress = FALSE;
		::InvalidateRect(m_hWnd, NULL, FALSE);
	}

	SetMsgHandled(FALSE);
}

void CSkinEdit::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
		m_bHover = TRUE;
		::InvalidateRect(m_hWnd, NULL, FALSE);
	}

	SetMsgHandled(FALSE);
}

void CSkinEdit::OnMouseLeave()
{
	m_bMouseTracking = FALSE;
	m_bHover = FALSE;
	::InvalidateRect(m_hWnd, NULL, FALSE);
	SetMsgHandled(FALSE);
}

void CSkinEdit::OnSetFocus(CWindow wndOld)
{
	if (m_bIsDefText)
	{
		m_bIsDefText = FALSE;
		SetPasswordChar(m_cPwdChar);
		SetWindowText(_T(""));
	}

	m_bFocus = TRUE;
	::InvalidateRect(m_hWnd, NULL, FALSE);
	SetMsgHandled(FALSE);
}

void CSkinEdit::OnKillFocus(CWindow wndFocus)
{
	if (GetWindowTextLength() <= 0 && !m_strDefText.IsEmpty())
	{
		m_bIsDefText = TRUE;
		m_cPwdChar = GetPasswordChar();
		SetPasswordChar(0);
		SetWindowText(m_strDefText);
	}

	m_bFocus = FALSE;
	::InvalidateRect(m_hWnd, NULL, FALSE);
	SetMsgHandled(FALSE);
}

void CSkinEdit::OnSize(UINT nType, CSize size)
{
	DefWindowProc();

	if (!m_bMultiLine)			// 单行模式居中
		SetCenterInSingleLine();
}

void CSkinEdit::OnDestroy()
{
	SetMsgHandled(FALSE);

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);
}

BOOL CSkinEdit::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinEdit::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

void CSkinEdit::SetCenterInSingleLine()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CDCHandle dc = ::GetDC(m_hWnd);
	TEXTMETRIC tm = {0};
	dc.GetTextMetrics(&tm);
	int nFontHeight = tm.tmHeight + tm.tmExternalLeading;
	int nMargin = (rcClient.Height() - nFontHeight) / 2;
	::ReleaseDC(m_hWnd, dc);

	rcClient.DeflateRect(0, nMargin);
	SetRectNP(&rcClient);
}

void CSkinEdit::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}