#include "stdafx.h"
#include "SkinTabCtrl.h"

CSkinTabCtrlItem::CSkinTabCtrlItem(void)
{
	m_nID = 0;
	m_dwStyle = STCI_STYLE_BUTTON;
	m_nWidth = m_nHeight = 0;
	m_nLeftWidth = m_nRightWidth = 0;
	m_nPadding = 0;
	m_strText = _T("");
	m_strToolTipText = _T("");
	m_lpBgImgN = m_lpBgImgH = m_lpBgImgD = NULL;
	m_lpArrowImgH = m_lpArrowImgD = NULL;
	m_lpIconImg = m_lpSelIconImg = NULL;
	m_bVisible = TRUE;
}

CSkinTabCtrlItem::~CSkinTabCtrlItem(void)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpSelIconImg);
}

CSkinTabCtrl::CSkinTabCtrl(void)
{
	m_lpBgImg = NULL;
	m_lpItemBgImgN = m_lpItemBgImgH = m_lpItemBgImgD = NULL;
	m_lpArrowImgH = m_lpArrowImgD = NULL;
	m_nSelIndex = m_nHoverIndex = -1;
	m_bPressArrow = FALSE;
	m_bMouseTracking = FALSE;
	m_nLeft = m_nTop = 0;
	m_bAutoSize = FALSE;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
    m_clrBgColor = RGB(255, 255, 255);
}

CSkinTabCtrl::~CSkinTabCtrl(void)
{
}

void CSkinTabCtrl::SetLeftTop(int nLeft, int nTop)
{
	m_nLeft = nLeft;
	m_nTop = nTop;
}

void CSkinTabCtrl::SetAutoSize(BOOL bAutoSize)
{
	m_bAutoSize = bAutoSize;
}

void CSkinTabCtrl::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

BOOL CSkinTabCtrl::SetBgPic(LPCTSTR lpszFileName, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);

	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (m_lpBgImg != NULL)
		m_lpBgImg->SetNinePart(rcNinePart);

	if (NULL == m_lpBgImg)
		return FALSE;
	else
		return TRUE;
}

void CSkinTabCtrl::SetBgColor(COLORREF clrBgColor)
{
    m_clrBgColor = clrBgColor;
}

BOOL CSkinTabCtrl::SetItemsBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
				   LPCTSTR lpDown, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgD);

	m_lpItemBgImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpItemBgImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpItemBgImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (m_lpItemBgImgN != NULL)
		m_lpItemBgImgN->SetNinePart(rcNinePart);

	if (m_lpItemBgImgH != NULL)
		m_lpItemBgImgH->SetNinePart(rcNinePart);

	if (m_lpItemBgImgD != NULL)
		m_lpItemBgImgD->SetNinePart(rcNinePart);

	if ((lpNormal != NULL && NULL == m_lpItemBgImgN) || 
		(lpHighlight != NULL && NULL == m_lpItemBgImgH) ||
		(lpDown != NULL && NULL == m_lpItemBgImgD))
		return FALSE;
	else
		return TRUE;
}

void CSkinTabCtrl::SetPressArrow(BOOL bPressArrow)
{
	m_bPressArrow = bPressArrow;
}

BOOL CSkinTabCtrl::SetItemsArrowPic(LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgD);

	m_lpArrowImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpArrowImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if ((lpHighlight != NULL && NULL == m_lpArrowImgH) ||
		(lpDown != NULL && NULL == m_lpArrowImgD))
		return FALSE;
	else
		return TRUE;
}

int CSkinTabCtrl::GetItemCount()
{
	return m_arrItems.size();
}

int CSkinTabCtrl::GetCurSel()
{
	return m_nSelIndex;
}

void CSkinTabCtrl::SetCurSel(int nCurSel)
{
	m_nSelIndex = nCurSel;
	if (IsWindow())
		Invalidate();
}

int CSkinTabCtrl::AddItem(int nID, DWORD dwStyle)
{
	CSkinTabCtrlItem* lpItem = new CSkinTabCtrlItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	lpItem->m_dwStyle = dwStyle;
	m_arrItems.push_back(lpItem);
	return m_arrItems.size() - 1;
}

void CSkinTabCtrl::SetItemID(int nIndex, int nID)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CSkinTabCtrl::SetItemStyle(int nIndex, int dwStyle)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_dwStyle = dwStyle;
}

void CSkinTabCtrl::SetItemSize(int nIndex, int nWidth, int nHeight, 
				 int nLeftWidth/* = 0*/, int nRightWidth/* = 0*/)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_nWidth = nWidth;
		lpItem->m_nHeight = nHeight;
		lpItem->m_nLeftWidth = nLeftWidth;
		lpItem->m_nRightWidth = nRightWidth;
	}
}

void CSkinTabCtrl::SetItemPadding(int nIndex, int nPadding)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_nPadding = nPadding;
}

void CSkinTabCtrl::SetItemText(int nIndex, LPCTSTR lpszText)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_strText = lpszText;
}

void CSkinTabCtrl::SetItemToolTipText(int nIndex, LPCTSTR lpszText)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_strToolTipText = lpszText;
}

BOOL CSkinTabCtrl::SetItemBgPic(int nIndex, LPCTSTR lpNormal, 
				  LPCTSTR lpHighlight, LPCTSTR lpDown, const CRect& rcNinePart)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpBgImgD);

	lpItem->m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	lpItem->m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	lpItem->m_lpBgImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (lpItem->m_lpBgImgN != NULL)
		lpItem->m_lpBgImgN->SetNinePart(rcNinePart);

	if (lpItem->m_lpBgImgH != NULL)
		lpItem->m_lpBgImgH->SetNinePart(rcNinePart);

	if (lpItem->m_lpBgImgD != NULL)
		lpItem->m_lpBgImgD->SetNinePart(rcNinePart);

	if ((lpNormal != NULL && NULL == lpItem->m_lpBgImgN) || 
		(lpHighlight != NULL && NULL == lpItem->m_lpBgImgH) ||
		(lpDown != NULL && NULL == lpItem->m_lpBgImgD))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinTabCtrl::SetItemArrowPic(int nIndex, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpArrowImgD);

	lpItem->m_lpArrowImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	lpItem->m_lpArrowImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if ((lpHighlight != NULL && NULL == lpItem->m_lpArrowImgH) ||
		(lpDown != NULL && NULL == lpItem->m_lpArrowImgD))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinTabCtrl::SetItemIconPic(int nIndex, LPCTSTR lpIcon, LPCTSTR lpSelIcon)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpIconImg);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpSelIconImg);

	lpItem->m_lpIconImg = CSkinManager::GetInstance()->GetImage(lpIcon);
	lpItem->m_lpSelIconImg = CSkinManager::GetInstance()->GetImage(lpSelIcon);

	if ((lpIcon != NULL && NULL == lpItem->m_lpIconImg) ||
		(lpSelIcon != NULL && NULL == lpItem->m_lpSelIconImg))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinTabCtrl::SetItemVisible(int nIndex, BOOL bVisible)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	lpItem->m_bVisible = bVisible;
	return TRUE;
}

BOOL CSkinTabCtrl::GetItemRectByIndex(int nIndex, CRect& rect)
{
	CSkinTabCtrlItem* lpItem;
	int nLeft = m_nLeft, nTop = m_nTop;

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			if (i == nIndex)
			{
				rect = CRect(nLeft, nTop, nLeft+lpItem->m_nWidth, nTop+lpItem->m_nHeight);
				return TRUE;
			}
			nLeft += lpItem->m_nWidth;
			nLeft += lpItem->m_nPadding;
		}
	}

	return FALSE;
}

BOOL CSkinTabCtrl::GetItemRectByID(int nID, CRect& rect)
{
	CSkinTabCtrlItem* lpItem;
	int nLeft = m_nLeft, nTop = m_nTop;

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			if (lpItem->m_nID == nID)
			{
				rect = CRect(nLeft, nTop, nLeft+lpItem->m_nWidth, nTop+lpItem->m_nHeight);
				return TRUE;
			}
			nLeft += lpItem->m_nWidth;
			nLeft += lpItem->m_nPadding;
		}
	}

	return FALSE;
}

int CSkinTabCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    ModifyStyle(0, WS_CLIPSIBLINGS);
    return 1;
}

BOOL CSkinTabCtrl::OnEraseBkgnd(CDCHandle dc)
{ 
	return TRUE;
}

void CSkinTabCtrl::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

    if (m_bTransparent)
        DrawParentWndBg(MemDC.m_hDC);
    else
        MemDC.FillSolidRect(&rcClient, m_clrBgColor);

	if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())
		m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		DrawItem(MemDC.m_hDC, i);
	}
}

void CSkinTabCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nIndex = HitTest(point);
	if (nIndex != -1)
	{
		if (nIndex != m_nSelIndex)
		{
			CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
			if(lpItem!=NULL && !lpItem->m_bVisible)
				return;
			
			m_nSelIndex = nIndex;
			Invalidate();

			NMHDR nmhdr = {m_hWnd, GetDlgCtrlID(), TCN_SELCHANGE};
			::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&nmhdr);
		}
		else
		{
			CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
			if (lpItem != NULL && lpItem->m_dwStyle & STCI_STYLE_DROPDOWN)
			{
				if(!lpItem->m_bVisible)
					return;
				
				CRect rcItem;
				GetItemRectByIndex(nIndex, rcItem);

				CRect rcArrow(rcItem);
				rcArrow.left += lpItem->m_nLeftWidth;
				rcArrow.right = rcArrow.left + lpItem->m_nRightWidth;

				if (rcArrow.PtInRect(point))
					m_bPressArrow = TRUE;
				else
					m_bPressArrow = FALSE;

				if (m_bPressArrow)
				{
					Invalidate();

					NMHDR nmhdr = {0};
					nmhdr.hwndFrom = m_hWnd;
					nmhdr.idFrom = GetDlgCtrlID();
					nmhdr.code = TCN_DROPDOWN;
					::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&nmhdr);

					m_bPressArrow = FALSE;
					Invalidate();
				}
			}
		}
	}
}

void CSkinTabCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);
}

void CSkinTabCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
	}

	int nIndex;

	nIndex = HitTest(point);
	if (nIndex != -1)
	{
		CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
		if (lpItem != NULL)
		{
			CRect rcItem;
			GetItemRectByIndex(nIndex, rcItem);

			if (nIndex != m_nHoverIndex && lpItem->m_strToolTipText.GetLength() > 0)
			{
				if (!m_ToolTipCtrl.IsWindow())
				{
					m_ToolTipCtrl.Create(m_hWnd);
					m_ToolTipCtrl.SetMaxTipWidth(200);
				}

				if (m_ToolTipCtrl.IsWindow())
				{
					if (m_ToolTipCtrl.GetToolCount() <= 0)
					{
						m_ToolTipCtrl.Activate(TRUE);
						m_ToolTipCtrl.AddTool(m_hWnd, lpItem->m_strToolTipText.GetBuffer(), &rcItem, 1);
					}
					else
					{
						m_ToolTipCtrl.Activate(TRUE);
						m_ToolTipCtrl.UpdateTipText(lpItem->m_strToolTipText.GetBuffer(), m_hWnd, 1);
						m_ToolTipCtrl.SetToolRect(m_hWnd, 1, &rcItem);
					}
				}
			}
		}
	}

	if (nIndex != m_nHoverIndex)
	{
		m_nHoverIndex = nIndex;
		Invalidate();
	}
}

void CSkinTabCtrl::OnMouseLeave()
{
	SetMsgHandled(FALSE);

	m_bMouseTracking = FALSE;
	int nIndex = -1;
	if (nIndex != m_nHoverIndex)
	{
		m_nHoverIndex = nIndex;
		Invalidate();
	}
}

LRESULT CSkinTabCtrl::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SetMsgHandled(FALSE);
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	return 1;
}

void CSkinTabCtrl::OnDestroy()
{
	SetMsgHandled(FALSE);

	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	CSkinTabCtrlItem* lpItem;
	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
			delete lpItem;
	}
	m_arrItems.clear();

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgD);

	m_nSelIndex = m_nHoverIndex = -1;
	m_bPressArrow = FALSE;
	m_bMouseTracking = FALSE;
	m_nLeft = m_nTop = 0;
	m_bAutoSize = FALSE;
	m_bTransparent = FALSE;
}


BOOL CSkinTabCtrl::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinTabCtrl::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

CSkinTabCtrlItem* CSkinTabCtrl::GetItemByIndex(int nIndex)
{
	if (nIndex >= 0 && nIndex < (int)m_arrItems.size())
		return m_arrItems[nIndex];
	else
		return NULL;
}

CSkinTabCtrlItem* CSkinTabCtrl::GetItemByID(int nID)
{
	CSkinTabCtrlItem* lpItem;
	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL && lpItem->m_nID == nID)
			return lpItem;
	}

	return NULL;
}

int CSkinTabCtrl::HitTest(POINT pt)
{
	CSkinTabCtrlItem* lpItem;
	int nLeft = m_nLeft, nTop = m_nTop;
	CRect rcItem;

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			rcItem = CRect(nLeft, nTop, nLeft+lpItem->m_nWidth, nTop+lpItem->m_nHeight);
			if (rcItem.PtInRect(pt))
				return i;
			nLeft += lpItem->m_nWidth;
			nLeft += lpItem->m_nPadding;
		}
	}

	return -1;
}

void CSkinTabCtrl::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinTabCtrl::DrawItem(HDC hDC, int nIndex)
{
	CSkinTabCtrlItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return;

	if(!lpItem->m_bVisible)
		return;

	CRect rcItem;
	GetItemRectByIndex(nIndex, rcItem);

	if (m_nSelIndex == nIndex)
	{
		if (lpItem->m_lpBgImgD != NULL && !lpItem->m_lpBgImgD->IsNull())
			lpItem->m_lpBgImgD->Draw2(hDC, rcItem);
		else if (m_lpItemBgImgD != NULL && !m_lpItemBgImgD->IsNull())
			m_lpItemBgImgD->Draw2(hDC, rcItem);
	}
	else if (m_nHoverIndex == nIndex)
	{
		if (lpItem->m_lpBgImgH != NULL && !lpItem->m_lpBgImgH->IsNull())
			lpItem->m_lpBgImgH->Draw2(hDC, rcItem);
		else if (m_lpItemBgImgH != NULL && !m_lpItemBgImgH->IsNull())
			m_lpItemBgImgH->Draw2(hDC, rcItem);
	}
	else
	{
		if (lpItem->m_lpBgImgN != NULL && !lpItem->m_lpBgImgN->IsNull())
			lpItem->m_lpBgImgN->Draw2(hDC, rcItem);
		else if (m_lpItemBgImgN != NULL && !m_lpItemBgImgN->IsNull())
			m_lpItemBgImgN->Draw2(hDC, rcItem);
	}

	CImageEx* lpIconImg;
	if (m_nSelIndex == nIndex)
		lpIconImg = lpItem->m_lpSelIconImg;
	else
		lpIconImg = lpItem->m_lpIconImg;

	BOOL bHasText = FALSE;
	if (lpItem->m_strText.GetLength() > 0)
		bHasText = TRUE;

	BOOL bHasIcon = FALSE;
	if (lpIconImg != NULL && !lpIconImg->IsNull())
		bHasIcon = TRUE;

	if (bHasIcon && bHasText)	// 带图标和文字
	{
		int cxIcon = lpIconImg->GetWidth();
		int cyIcon = lpIconImg->GetHeight();

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		if (NULL == hFont)
			hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);

		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, lpItem->m_strText, lpItem->m_strText.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		int cx = cxIcon+1+rcText.Width();
		int cy = cyIcon;

		CRect rcCenter;
		CalcCenterRect(rcItem, cx, cy, rcCenter);

		CRect rcIcon(rcCenter.left, rcCenter.top, rcCenter.left+cxIcon, rcCenter.bottom);
		lpIconImg->Draw2(hDC, rcIcon);

		UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
		rcText = CRect(rcIcon.right+1, rcItem.top, rcIcon.right+1+rcText.Width(), rcItem.bottom);
		::DrawText(hDC, lpItem->m_strText, lpItem->m_strText.GetLength(), &rcText, nFormat);

		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
	else if (bHasIcon)	// 仅图标
	{
		int cxIcon = lpIconImg->GetWidth();
		int cyIcon = lpIconImg->GetHeight();

		CRect rcIcon;
		CalcCenterRect(rcItem, cxIcon, cyIcon, rcIcon);

		lpIconImg->Draw2(hDC, rcIcon);
	}
	else if (bHasText)	// 仅文字
	{
		UINT nFormat = DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		if (NULL == hFont)
			hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		::DrawText(hDC, lpItem->m_strText, lpItem->m_strText.GetLength(), &rcItem, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}

	if (m_nSelIndex == nIndex)
	{
		if (lpItem->m_dwStyle & STCI_STYLE_DROPDOWN)
		{
			CRect rcArrow(rcItem);
			rcArrow.left += lpItem->m_nLeftWidth;
			rcArrow.right = rcArrow.left + lpItem->m_nRightWidth;

			if (m_bPressArrow)
			{
				if (lpItem->m_lpArrowImgD != NULL && !lpItem->m_lpArrowImgD->IsNull())
					lpItem->m_lpArrowImgD->Draw2(hDC, rcArrow);
				else if (m_lpArrowImgD != NULL && !m_lpArrowImgD->IsNull())
					m_lpArrowImgD->Draw2(hDC, rcArrow);
			}
			else if (m_nHoverIndex == nIndex)
			{
				if (lpItem->m_lpArrowImgH != NULL && !lpItem->m_lpArrowImgH->IsNull())
					lpItem->m_lpArrowImgH->Draw2(hDC, rcArrow);
				else if (m_lpArrowImgH != NULL && !m_lpArrowImgH->IsNull())
					m_lpArrowImgH->Draw2(hDC, rcArrow);
			}
		}
	}
}