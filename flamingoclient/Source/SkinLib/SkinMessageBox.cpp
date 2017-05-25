#include "stdafx.h"
#include "SkinMessageBox.h"

int PopSkinMessage(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	return 0;
}

CSkinMessageBox::CSkinMessageBox()
{
	m_bHasMinBtn = m_bHasMaxBtn = m_bHasCloseBtn = TRUE;
	m_bSizeBox = TRUE;

	m_rcMinBtn = m_rcMaxBtn = m_rcCloseBtn = CRect(0,0,0,0);

	m_bMinBtnPress = m_bMinBtnHover = FALSE;
	m_bMaxBtnPress = m_bMaxBtnHover = FALSE;
	m_bCloseBtnPress = m_bCloseBtnHover = FALSE;

	m_clrBg = ::GetSysColor(COLOR_BTNFACE);

	m_hMemDC = NULL;
	m_hMemBmp = m_hOldBmp = NULL;
}

CSkinMessageBox::~CSkinMessageBox()
{

}

void CSkinMessageBox::ShowMaxButton(BOOL bShow)
{
	m_bHasMaxBtn = bShow;
}

int CSkinMessageBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	DWORD dwStyle = GetStyle();

	SetWindowLong(GWL_STYLE, dwStyle);

	DWORD dwStyleEx = GetExStyle();
	SetWindowLong(GWL_EXSTYLE, dwStyleEx);
	SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED);

	return TRUE;	
}

void CSkinMessageBox::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);
	CRect rcClient;
	GetClientRect(&rcClient);
}

void CSkinMessageBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	int nHitTest = HitTest(point);
	
	if (nHitTest == HTMINBUTTON || nHitTest == HTCLOSE)
	{
		::SetCapture(m_hWnd);
		CRect rcSysBtn(0, 0, 0, 0);
		rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
		rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);
		m_bCloseBtnPress = TRUE;

		if (nHitTest == HTMINBUTTON)
		{
			m_bMinBtnPress = TRUE;
			InvalidateRect(&rcSysBtn);
		}
		else if (nHitTest == HTCLOSE)
		{
			m_bCloseBtnPress = TRUE;
			InvalidateRect(&rcSysBtn);
		}
	}

	SetMsgHandled(FALSE);
}

void CSkinMessageBox::OnLButtonUp(UINT nFlags, CPoint point)
{
	int nHitTest = HitTest(point);
	HWND hWnd = ::GetCapture();
	if (m_hWnd == hWnd)
	{
		::ReleaseCapture();
	}

	CRect rcSysBtn(0, 0, 0, 0);
	rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
	rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);

	if (nHitTest == HTMINBUTTON && m_bMinBtnPress)
	{
		m_bMinBtnPress = FALSE;
		InvalidateRect(&rcSysBtn);
		SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
	}
	else if (nHitTest == HTCLOSE && m_bCloseBtnPress)
	{
		m_bCloseBtnPress = FALSE;
		InvalidateRect(&rcSysBtn);
		SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
	}
	SetMsgHandled(FALSE);
}

void CSkinMessageBox::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);
}

void CSkinMessageBox::OnDestroy()
{
	SetMsgHandled(FALSE);
}


int CSkinMessageBox::HitTest(POINT pt)
{
	if (!IsZoomed() && m_bSizeBox)
	{
		CRect rcClient;
		GetClientRect(&rcClient);

		RECT rcSizeBox = {4,4,4,4};
		if (pt.y < rcClient.top + rcSizeBox.top)
		{
			if (pt.x < rcClient.left + rcSizeBox.left)
				return HTTOPLEFT;
			if (pt.x > rcClient.right - rcSizeBox.right)
				return HTTOPRIGHT;
			return HTTOP;
		}
		else if (pt.y > rcClient.bottom - rcSizeBox.bottom)
		{
			if (pt.x < rcClient.left + rcSizeBox.left)
				return HTBOTTOMLEFT;
			if (pt.x > rcClient.right - rcSizeBox.right)
				return HTBOTTOMRIGHT;
			return HTBOTTOM;
		}
		if (pt.x < rcClient.left + rcSizeBox.left)
			return HTLEFT;
		if (pt.x > rcClient.right - rcSizeBox.right)
			return HTRIGHT;
	}

	if (m_bHasMinBtn && m_rcMinBtn.PtInRect(pt))
		return HTMINBUTTON;

	if (m_bHasMaxBtn && m_rcMaxBtn.PtInRect(pt))
		return HTMAXBUTTON;

	if (m_bHasCloseBtn && m_rcCloseBtn.PtInRect(pt))
		return HTCLOSE;

	return HTCAPTION;
}