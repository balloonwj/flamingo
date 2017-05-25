#include "stdafx.h"
#include "SkinHyperLink.h"

CSkinHyperLink::CSkinHyperLink(void)
{
	m_clrLink = RGB(0, 0, 255);
	m_clrHover = RGB(255, 0, 0);
	m_clrVisited = RGB(128, 0, 128);
	
	m_hCursor = NULL;

	m_bFocus = m_bHover = m_bMouseTracking = FALSE;
	m_bVisited = FALSE;

	m_hNormalFont = m_hHoverFont = m_hVisitedFont = NULL;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;

	m_nLinkType = SKIN_LINK_DEFAULT;
}

CSkinHyperLink::~CSkinHyperLink(void)
{
}

void CSkinHyperLink::SetLinkColor(COLORREF clr)
{
	m_clrLink = clr;
}

void CSkinHyperLink::SetHoverLinkColor(COLORREF clr)
{
	m_clrHover = clr;
}

void CSkinHyperLink::SetVisitedLinkColor(COLORREF clr)
{
	m_clrVisited = clr;
}

void CSkinHyperLink::SetLabel(LPCTSTR lpszLabel)
{
	CRect rcNew(0,0,0,0);
	HDC hDC = ::GetDC(m_hWnd);
	HFONT hFont = m_hNormalFont;
	if (NULL == hFont)
		hFont = (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0L);
	HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
	::DrawText(hDC, lpszLabel, -1, &rcNew, DT_CALCRECT);
	::SelectObject(hDC, hOldFont);
	::ReleaseDC(m_hWnd, hDC);
	SetWindowPos(NULL, 0, 0, rcNew.Width(), rcNew.Height(), SWP_NOMOVE);

	SetWindowText(lpszLabel);
}

void CSkinHyperLink::SetHyperLink(LPCTSTR lpszLink)
{
	m_strHyperLink = lpszLink;
}

void CSkinHyperLink::SetToolTipText(LPCTSTR lpszText)
{
	if (!IsWindow())		// SetToolTipText函数必须在创建按钮窗口后才能调用
		return;

	if (!m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.Create(m_hWnd);

	if (m_ToolTipCtrl.IsWindow())
	{
		if (m_ToolTipCtrl.GetToolCount() <= 0)
		{
			CRect rcClient;
			GetClientRect(&rcClient);

			m_ToolTipCtrl.Activate(TRUE);
			m_ToolTipCtrl.AddTool(m_hWnd, lpszText, &rcClient, 1);
		}
		else
		{
			m_ToolTipCtrl.Activate(TRUE);
			m_ToolTipCtrl.UpdateTipText(lpszText, m_hWnd, 1);
		}
	}
}

void CSkinHyperLink::SetNormalFont(HFONT hFont)
{
	m_hNormalFont = hFont;
}

void CSkinHyperLink::SetHoverFont(HFONT hFont)
{
	m_hHoverFont = hFont;
}

void CSkinHyperLink::SetVisitedFont(HFONT hFont)
{
	m_hVisitedFont = hFont;
}

void CSkinHyperLink::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

void CSkinHyperLink::SetLinkType(SKIN_LINK_TYPE nLinkType)
{
	m_nLinkType = nLinkType;
}

BOOL CSkinHyperLink::SubclassWindow(HWND hWnd)
{
	BOOL bRet = __super::SubclassWindow(hWnd);
	if (bRet)
	{
		DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_STYLE);
		dwStyle |= SS_NOTIFY | WS_TABSTOP;
		::SetWindowLong(m_hWnd, GWL_STYLE, dwStyle);
		if (m_nLinkType == SKIN_LINK_ADDNEW)
		{
			m_hCursor = ::LoadCursor(NULL, IDC_ARROW);
		}
		else
		{
			m_hCursor = ::LoadCursor(NULL, IDC_HAND);
		}
		
	}

	return bRet;
}

int CSkinHyperLink::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return 0;
}

BOOL CSkinHyperLink::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinHyperLink::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);
	Draw(PaintDC.m_hDC);
}

void CSkinHyperLink::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	SetCapture();
	::InvalidateRect(m_hWnd, NULL, TRUE);
}

void CSkinHyperLink::OnLButtonUp(UINT nFlags, CPoint point)
{
	if(GetCapture() == m_hWnd)
	{
		ReleaseCapture();

		CRect rcClient;
		GetClientRect(&rcClient);

		if (::PtInRect(&rcClient, point))
		{
			BOOL bRet = Navigate();
			if (bRet)
				m_bVisited = TRUE;

			::InvalidateRect(m_hWnd, NULL, TRUE);
		}
	}
}

void CSkinHyperLink::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
		m_bHover = TRUE;
		if (m_nLinkType == SKIN_LINK_ADDNEW)
		{
			m_bHover = FALSE;
			m_bMouseTracking = FALSE;
			m_bVisited = FALSE;
		}
		::InvalidateRect(m_hWnd, NULL, TRUE);
	}

	SetMsgHandled(FALSE);
}

void CSkinHyperLink::OnMouseLeave()
{
	m_bMouseTracking = FALSE;
	m_bHover = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinHyperLink::OnSetFocus(CWindow wndOld)
{
	m_bFocus = TRUE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinHyperLink::OnKillFocus(CWindow wndFocus)
{
	m_bFocus = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

BOOL CSkinHyperLink::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
	if (m_hCursor)
	{
		::SetCursor(m_hCursor);
		return TRUE;
	}
	SetMsgHandled(FALSE);
	return FALSE;
}

int CSkinHyperLink::OnSetText(LPCTSTR lpstrText)
{
	int nRet = DefWindowProc();
	HDC hDC = ::GetDC(m_hWnd);
	Draw(hDC);
	::ReleaseDC(m_hWnd, hDC);
	return nRet;
}

LRESULT CSkinHyperLink::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	SetMsgHandled(FALSE);
	return 1;
}

void CSkinHyperLink::OnDestroy()
{
	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	m_hNormalFont = m_hHoverFont = m_hVisitedFont = NULL;

	SetMsgHandled(FALSE);
}

BOOL CSkinHyperLink::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

BOOL CSkinHyperLink::Navigate()
{
	if (!m_strHyperLink.IsEmpty())
	{
		DWORD_PTR dwRet = (DWORD_PTR)::ShellExecute(0, _T("open"), m_strHyperLink, 0, 0, SW_SHOWNORMAL);
		return (dwRet > 32) ? TRUE : FALSE;
	}
	else
	{
		::SendMessage(GetParent(), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
		return TRUE;
	}
}

void CSkinHyperLink::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinHyperLink::Draw(HDC hDC)
{
	if (m_bTransparent)
		DrawParentWndBg(hDC);

	CRect rtClient;
	GetClientRect(&rtClient);

	int nTextLen = GetWindowTextLength();
	if (nTextLen <= 0)
		return;

	CString strText;
	GetWindowText(strText);

	HFONT hNormalFont = NULL, hHoverFont = NULL, hVisitedFont = NULL;
	HFONT hFont, hOldFont;
	COLORREF clrText;

	hNormalFont = m_hNormalFont;
	if (NULL == hNormalFont)
		hNormalFont = (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0L);

	if (m_bHover)	// 鼠标悬停状态
	{
		hHoverFont = m_hHoverFont;
		if (NULL == hHoverFont)
		{
			CFontHandle font = hNormalFont;
			LOGFONT lf = {0};
			font.GetLogFont(&lf);

			lf.lfUnderline = TRUE;

			hHoverFont = ::CreateFontIndirect(&lf);
		}
		hFont = hHoverFont;

		clrText = m_clrHover;
	}
	else if (m_bVisited)
	{
		hVisitedFont = m_hVisitedFont;
		if (NULL == hVisitedFont)
			hVisitedFont = hNormalFont;
		hFont = hVisitedFont;
		clrText = m_clrVisited;
	}
	else	// 普通状态
	{
		hFont = hNormalFont;
		clrText = m_clrLink;
	}

	int nMode = ::SetBkMode(hDC, TRANSPARENT);

	::SetTextColor(hDC, clrText);

	hOldFont = (HFONT)::SelectObject(hDC, hFont);
	if (m_nLinkType == SKIN_LINK_ADDNEW)
		::DrawText(hDC, strText, nTextLen, &rtClient, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CENTER);
	else
		::DrawText(hDC, strText, nTextLen, &rtClient, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
	::SelectObject(hDC, hOldFont);

	::SetBkMode(hDC, nMode);

	if (hHoverFont != NULL && hHoverFont != m_hHoverFont)
	{
		::DeleteObject(hHoverFont);
		hHoverFont = NULL;
	}
	
}