#include "stdafx.h"
#include "SkinButton.h"

CSkinButton::CSkinButton(void)
{
	m_lpBgImgN = m_lpBgImgH = m_lpBgImgD = m_lpBgImgF = NULL;

	m_lpCheckBoxImgN = m_lpCheckBoxImgH = NULL;
	m_lpCheckBoxImgTickN = m_lpCheckBoxImgTickH = NULL;

	m_lpArrowImg = m_lpIconImg = NULL;

	m_bFocus = m_bPress = m_bHover = m_bMouseTracking = FALSE;

	m_nBtnType = SKIN_PUSH_BUTTON;

	m_hMenu = NULL;

	m_cxRound = m_cyRound = 0;

	m_dwTextAlign = DT_CENTER;

	m_bTransparent = FALSE;
	m_hBgDC = NULL;

	m_clrText = RGB(0, 0, 0);
	m_bTextBold = FALSE;

    m_clrBgColor = RGB(255, 255, 255);
}

CSkinButton::~CSkinButton(void)
{
}

void CSkinButton::SetButtonType(SKIN_BUTTON_TYPE nType)
{
	m_nBtnType = nType;
}

BOOL CSkinButton::SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
			  LPCTSTR lpDown, LPCTSTR lpFocus)
{
	return SetBgPic(lpNormal, lpHighlight, lpDown, lpFocus, CRect(0,0,0,0));
}

BOOL CSkinButton::SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
			  LPCTSTR lpDown, LPCTSTR lpFocus, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgF);

	m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpBgImgD = CSkinManager::GetInstance()->GetImage(lpDown);
	m_lpBgImgF = CSkinManager::GetInstance()->GetImage(lpFocus);
	
	if (m_lpBgImgN != NULL)
		m_lpBgImgN->SetNinePart(rcNinePart);

	if (m_lpBgImgH != NULL)
		m_lpBgImgH->SetNinePart(rcNinePart);

	if (m_lpBgImgD != NULL)
		m_lpBgImgD->SetNinePart(rcNinePart);

	if (m_lpBgImgF != NULL)
		m_lpBgImgF->SetNinePart(rcNinePart);

	if ((lpNormal != NULL && NULL == m_lpBgImgN) || 
		(lpHighlight != NULL && NULL == m_lpBgImgH) ||
		(lpDown != NULL && NULL == m_lpBgImgD) ||
		(lpFocus != NULL && NULL == m_lpBgImgF))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinButton::SetCheckBoxPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
					LPCTSTR lpTickNormal, LPCTSTR lpTickHighlight)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgTickN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgTickH);

	m_lpCheckBoxImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpCheckBoxImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpCheckBoxImgTickN = CSkinManager::GetInstance()->GetImage(lpTickNormal);
	m_lpCheckBoxImgTickH = CSkinManager::GetInstance()->GetImage(lpTickHighlight);

	if (NULL == m_lpCheckBoxImgN || NULL == m_lpCheckBoxImgH
		|| NULL == m_lpCheckBoxImgTickN || NULL == m_lpCheckBoxImgTickH)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinButton::SetArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImg);

	m_lpArrowImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpArrowImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinButton::SetIconPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);

	m_lpIconImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpIconImg)
		return FALSE;
	else
		return TRUE;
}

void CSkinButton::SetMenu(HMENU hMenu)
{
	m_hMenu = hMenu;
}

void CSkinButton::SetToolTipText(LPCTSTR lpszText)
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

void CSkinButton::SetRound(int cxRound, int cyRound)
{
	m_cxRound = cxRound;
	m_cyRound = cyRound;
}

void CSkinButton::SetTextAlign(DWORD dwTextAlign)
{
	m_dwTextAlign = dwTextAlign;
}

void CSkinButton::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

void CSkinButton::SetTextColor(COLORREF clrText)
{
	m_clrText = clrText;
}

void CSkinButton::SetTextBoldStyle(BOOL bBold)
{
	m_bTextBold = bBold;
}

BOOL CSkinButton::SubclassWindow(HWND hWnd)
{
	__super::SubclassWindow(hWnd);

// 	DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_STYLE);
// 	dwStyle |= BS_OWNERDRAW;
// 	::SetWindowLong(m_hWnd, GWL_STYLE, dwStyle);

// 	DWORD dwClassStyle = ::GetClassLong(m_hWnd, GCL_STYLE);
// 	::SetClassLong(m_hWnd, GCL_STYLE, dwClassStyle & ~CS_PARENTDC); 

	if (m_cxRound != 0 || m_cyRound != 0)
	{
		CRect rcClient;
		GetClientRect(&rcClient);

		HRGN hRgn = ::CreateRoundRectRgn(rcClient.left, rcClient.top, 
			rcClient.right, rcClient.bottom, m_cxRound, m_cyRound);
		::SetWindowRgn(m_hWnd, hRgn, TRUE);
		::DeleteObject(hRgn);
	}

	return TRUE;
}

int CSkinButton::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
// 	DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_STYLE);
// 	dwStyle |= BS_OWNERDRAW;
// 	::SetWindowLong(m_hWnd, GWL_STYLE, dwStyle);
// 
// 	DWORD dwClassStyle = ::GetClassLong(m_hWnd, GCL_STYLE);
// 	::SetClassLong(m_hWnd, GCL_STYLE, dwClassStyle & ~CS_PARENTDC); 

    ModifyStyle(0, WS_CLIPSIBLINGS);

	CRect rcClient;
	GetClientRect(&rcClient);

	HRGN hRgn = ::CreateRoundRectRgn(rcClient.left, rcClient.top, 
		rcClient.right, rcClient.bottom, 4, 4);
	::SetWindowRgn(m_hWnd, hRgn, TRUE);
	::DeleteObject(hRgn);

	return 0;
}

BOOL CSkinButton::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinButton::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

    MemDC.FillSolidRect(&rcClient, m_clrBgColor);

	switch (m_nBtnType)
	{
	case SKIN_PUSH_BUTTON:
		DrawPushButton(MemDC.m_hDC);
		break;

	case SKIN_CHECKBOX:
		DrawCheckBox(MemDC.m_hDC);
		break;

	case SKIN_RADIO_BUTTON:
		DrawRadioButton(MemDC.m_hDC);
		break;

	case SKIN_GROUPBOX:
		DrawGroupBox(MemDC.m_hDC);
		break;

	case SKIN_ICON_BUTTON:
		DrawIconButton(MemDC.m_hDC);
		break;

	case SKIN_MENU_BUTTON:
		DrawMenuButton(MemDC.m_hDC);
		break;
	}
}

void CSkinButton::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_bPress = TRUE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinButton::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bPress)
	{
		m_bPress = FALSE;
		::InvalidateRect(m_hWnd, NULL, TRUE);

		if (m_nBtnType == SKIN_MENU_BUTTON)
		{
			if (::IsMenu(m_hMenu))
			{
				CRect rc;
				GetClientRect(&rc);
				ClientToScreen(&rc);
				::TrackPopupMenuEx(m_hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL,
					rc.left, rc.bottom, ::GetParent(m_hWnd), NULL);
			}
		}
	}

	SetMsgHandled(FALSE);
}

void CSkinButton::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
		m_bHover = TRUE;
		::InvalidateRect(m_hWnd, NULL, TRUE);
	}

	SetMsgHandled(FALSE);
}

void CSkinButton::OnMouseLeave()
{
	m_bMouseTracking = FALSE;
	m_bHover = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinButton::OnSetFocus(CWindow wndOld)
{
	m_bFocus = TRUE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinButton::OnKillFocus(CWindow wndFocus)
{
	m_bFocus = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

int CSkinButton::OnSetText(LPCTSTR lpstrText)
{
	::SendMessage(m_hWnd, WM_SETREDRAW, (WPARAM)FALSE, 0);
	int nRet = ::DefWindowProc(m_hWnd, WM_SETTEXT, 0, (LPARAM)lpstrText);
	::SendMessage(m_hWnd, WM_SETREDRAW, (WPARAM)TRUE, 0);
	Invalidate();
	return nRet;
}

//void CSkinButton::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
//{
//	if(nChar == VK_ESCAPE)
//		::SendMessage(::GetParent(m_hWnd), WM_KEYDOWN, (WPARAM)nChar, 0);
//
//	SetMsgHandled(FALSE);
//}

LRESULT CSkinButton::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	SetMsgHandled(FALSE);
	return 1;
}

void CSkinButton::OnDestroy()
{
	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgF);

	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgTickN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckBoxImgTickH);

	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);

	m_bFocus = m_bPress = m_bHover = m_bMouseTracking = FALSE;
	m_nBtnType = SKIN_PUSH_BUTTON;
	m_hMenu = NULL;
	m_cxRound = m_cyRound = 0;
	m_dwTextAlign = DT_CENTER;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;

	SetMsgHandled(FALSE);
}

BOOL CSkinButton::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinButton::DrawPushButton(HDC hDC)
{
	if (m_bTransparent)
		DrawParentWndBg(hDC);

	CRect rcClient;
	GetClientRect(&rcClient);

	if (m_bPress)	// 鼠标左键按下状态
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw(hDC, rcClient);
	}
	else if (m_bHover)	// 鼠标悬停状态
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
			m_lpBgImgH->Draw(hDC, rcClient);
	}
	else if (m_bFocus)	// 焦点状态
	{
		if (m_lpBgImgF != NULL && !m_lpBgImgF->IsNull())
			m_lpBgImgF->Draw(hDC, rcClient);
	}
	else	// 普通状态
	{
		if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
			m_lpBgImgN->Draw(hDC, rcClient);
	}

	if (m_bPress)
		rcClient.OffsetRect(1, 1);

	CString strText;
	GetWindowText(strText);

	BOOL bHasText = FALSE;
	if (strText.GetLength() > 0)
		bHasText = TRUE;

	BOOL bHasIcon = FALSE;
	if (m_lpIconImg != NULL && !m_lpIconImg->IsNull())
		bHasIcon = TRUE;

	::SetTextColor(hDC, m_clrText);

	if (bHasIcon && bHasText)	// 带图标和文字
	{
		int cxIcon = m_lpIconImg->GetWidth();
		int cyIcon = m_lpIconImg->GetHeight();

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);

		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strText, strText.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		int cx = cxIcon+3+rcText.Width();
		int cy = cyIcon;

		CRect rcCenter;
		CalcCenterRect(rcClient, cx, cy, rcCenter);

		CRect rcIcon(rcCenter.left, rcCenter.top, rcCenter.left+cxIcon, rcCenter.bottom);
		m_lpIconImg->Draw(hDC, rcIcon);

		UINT nFormat = m_dwTextAlign | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
		rcText = CRect(rcIcon.right+3, rcClient.top, rcIcon.right+3+rcText.Width(), rcClient.bottom);
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);

		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
	else if (bHasIcon)	// 仅图标
	{
		int cxIcon = m_lpIconImg->GetWidth();
		int cyIcon = m_lpIconImg->GetHeight();

		CRect rcIcon;
		CalcCenterRect(rcClient, cxIcon, cyIcon, rcIcon);

		m_lpIconImg->Draw(hDC, rcIcon);
	}
	else if (bHasText)	// 仅文字
	{
		UINT nFormat = m_dwTextAlign | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		if(m_bTextBold)
		{
			LOGFONT lf = {0};
			::GetObject(hFont, sizeof(LOGFONT), &lf);
			lf.lfWeight = FW_BOLD;
			hFont = ::CreateFontIndirect(&lf);
		}

		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		::DrawText(hDC, strText, strText.GetLength(), &rcClient, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
		if(m_bTextBold)
			::DeleteObject(hFont);
	}
}

void CSkinButton::DrawCheckBox(HDC hDC)
{
	if (m_bTransparent)
		DrawParentWndBg(hDC);

	int nWidth = 15, nHeight = 15;
	if (m_lpCheckBoxImgN != NULL && !m_lpCheckBoxImgN->IsNull())
	{
		nWidth = m_lpCheckBoxImgN->GetWidth();
		nHeight = m_lpCheckBoxImgN->GetHeight();
	}

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcCheck, rcText;

	rcCheck.left = rcClient.left;
	rcCheck.right = rcCheck.left + nWidth;
	rcCheck.top = (rcClient.Height() - nHeight) / 2;
	rcCheck.bottom = rcCheck.top + nHeight;

	rcText = rcClient;
	rcText.left = rcCheck.right + 4;

	BOOL bChecked = (GetCheck() == 1) ? TRUE : FALSE;

	if (m_bHover)	// 鼠标悬停状态
	{
		if (bChecked)
		{
			if (m_lpCheckBoxImgTickH != NULL && !m_lpCheckBoxImgTickH->IsNull())
				m_lpCheckBoxImgTickH->Draw(hDC, rcCheck);
		}
		else
		{
			if (m_lpCheckBoxImgH != NULL && !m_lpCheckBoxImgH->IsNull())
				m_lpCheckBoxImgH->Draw(hDC, rcCheck);
		}
	}
	else	// 普通状态
	{
		if (bChecked)
		{
			if (m_lpCheckBoxImgTickN != NULL && !m_lpCheckBoxImgTickN->IsNull())
				m_lpCheckBoxImgTickN->Draw(hDC, rcCheck);
		}
		else
		{
			if (m_lpCheckBoxImgN != NULL && !m_lpCheckBoxImgN->IsNull())
				m_lpCheckBoxImgN->Draw(hDC, rcCheck);
		}
	}

	CString strText;
	GetWindowText(strText);

	if (strText.GetLength() > 0)
	{
		UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
}

void CSkinButton::DrawRadioButton(HDC hDC)
{
	
}

void CSkinButton::DrawGroupBox(HDC hDC)
{

}

void CSkinButton::DrawIconButton(HDC hDC)
{
	if (m_bTransparent)
		DrawParentWndBg(hDC);

	CRect rcClient;
	GetClientRect(&rcClient);

	if (m_bPress)	// 鼠标左键按下状态
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw2(hDC, rcClient);
	}
	else if (m_bHover)	// 鼠标悬停状态
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
			m_lpBgImgH->Draw2(hDC, rcClient);
	}

	if (m_bPress)
		rcClient.OffsetRect(1, 1);

	CString strText;
	GetWindowText(strText);

	BOOL bHasText = FALSE;
	if (strText.GetLength() > 0)
		bHasText = TRUE;

	BOOL bHasIcon = FALSE;
	if (m_lpIconImg != NULL && !m_lpIconImg->IsNull())
		bHasIcon = TRUE;

	if (bHasIcon && bHasText)	// 带图标和文字
	{
		int cxIcon = m_lpIconImg->GetWidth();
		int cyIcon = m_lpIconImg->GetHeight();

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);

		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strText, strText.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		int cx = cxIcon+3+rcText.Width();
		int cy = cyIcon;

		CRect rcCenter;
		CalcCenterRect(rcClient, cx, cy, rcCenter);

		CRect rcIcon(rcCenter.left, rcCenter.top, rcCenter.left+cxIcon, rcCenter.bottom);
		m_lpIconImg->Draw(hDC, rcIcon);

		UINT nFormat = m_dwTextAlign | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
		rcText = CRect(rcIcon.right+3, rcClient.top, rcIcon.right+3+rcText.Width(), rcClient.bottom);
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);

		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
	else if (bHasIcon)	// 仅图标
	{
		int cxIcon = m_lpIconImg->GetWidth();
		int cyIcon = m_lpIconImg->GetHeight();

		CRect rcIcon;
		CalcCenterRect(rcClient, cxIcon, cyIcon, rcIcon);

		m_lpIconImg->Draw(hDC, rcIcon);
	}
	else if (bHasText)	// 仅文字
	{
		CRect rcText(rcClient);
		rcText.left += 2;
		rcText.right -= 2;

		UINT nFormat = m_dwTextAlign | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
}

void CSkinButton::DrawMenuButton(HDC hDC)
{
	if (m_bTransparent)
		DrawParentWndBg(hDC);

	CRect rcClient;
	GetClientRect(&rcClient);

	if (m_bPress)	// 鼠标左键按下状态
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw(hDC, rcClient);
	}
	else if (m_bHover)	// 鼠标悬停状态
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
			m_lpBgImgH->Draw(hDC, rcClient);
	}

	CRect rcArrow(0, 0, 0, 0);

	if (m_lpArrowImg != NULL && !m_lpArrowImg->IsNull())
	{
		int cx = m_lpArrowImg->GetWidth();
		int cy = m_lpArrowImg->GetHeight();
		int x = rcClient.right - 3 - cx;
		int y = (rcClient.Height() - cy + 1) / 2;
		rcArrow = CRect(x, y, x+cx, y+cy);
		m_lpArrowImg->Draw(hDC, rcArrow);
	}

	if (m_lpIconImg != NULL && !m_lpIconImg->IsNull())
	{
		int cx = m_lpIconImg->GetWidth();
		int cy = m_lpIconImg->GetHeight();

		int nRight;
		if (rcArrow.left > 0)
			nRight = rcArrow.left;
		else
			nRight = rcClient.right;

		CRect rcIcon(rcClient.left, rcClient.top, nRight, rcClient.bottom);
		CalcCenterRect(rcIcon, cx, cy, rcIcon);
		if (m_bPress)
			rcIcon.OffsetRect(1, 1);
		m_lpIconImg->Draw(hDC, rcIcon);
	}
}

void CSkinButton::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinButton::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}