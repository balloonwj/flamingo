#include "stdafx.h"
#include "SkinToolBar.h"

CSkinToolBarItem::CSkinToolBarItem(void)
{
	m_nID = 0;
	m_dwStyle = STBI_STYLE_BUTTON;
	m_nWidth = m_nHeight = 0;
	m_nLeftWidth = m_nRightWidth = 0;
	m_nPadding = 0;
	m_nLeftMargin = 0;
	m_nRightMargin = 0;
	m_strText = _T("");
	m_strToolTipText = _T("");
	m_lpBgImgN = m_lpBgImgH = m_lpBgImgD = NULL;
	m_lpLeftH = m_lpLeftD = NULL;
	m_lpRightH = m_lpRightD = NULL;
	m_lpSepartorImg = NULL;
	m_lpArrowImg = NULL;
	m_lpIconImg = NULL;
	m_bChecked = FALSE;
}

CSkinToolBarItem::~CSkinToolBarItem(void)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpSepartorImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpIconImg);
}

CSkinToolBar::CSkinToolBar(BOOL bVertical/* = FALSE*/)
{
    m_bVertical = bVertical;
    m_lpBgImg = NULL;
	m_nPressIndex = m_nHoverIndex = -1;
	m_bPressLorR = m_bHoverLorR = FALSE;
	m_bMouseTracking = FALSE;
	m_nLeft = m_nTop = 0;
	m_bAutoSize = FALSE;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
    m_clrBgColor = RGB(255, 255, 255);

    m_bUseGroup = FALSE;
}

CSkinToolBar::~CSkinToolBar(void)
{
}

void CSkinToolBar::SetLeftTop(int nLeft, int nTop)
{
	m_nLeft = nLeft;
	m_nTop = nTop;
}

void CSkinToolBar::SetAutoSize(BOOL bAutoSize)
{
	m_bAutoSize = bAutoSize;
}

void CSkinToolBar::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

BOOL CSkinToolBar::SetBgPic(LPCTSTR lpszFileName, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);

	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (NULL == m_lpBgImg)
		return FALSE;

	m_lpBgImg->SetNinePart(&rcNinePart);
	return TRUE;
}

void CSkinToolBar::SetBgColor(COLORREF clrBgColor)
{
    m_clrBgColor = clrBgColor;
}

int CSkinToolBar::AddItem(int nID, DWORD dwStyle)
{
	CSkinToolBarItem* lpItem = new CSkinToolBarItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	lpItem->m_dwStyle = dwStyle;
	m_arrItems.push_back(lpItem);
	return m_arrItems.size() - 1;
}

void CSkinToolBar::SetItemStyle(int nIndex, int dwStyle)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_dwStyle = dwStyle;
}

void CSkinToolBar::SetItemID(int nIndex, int nID)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CSkinToolBar::SetItemSize(int nIndex, int nWidth, int nHeight, 
							   int nLeftWidth/* = 0*/, int nRightWidth/* = 0*/)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_nWidth = nWidth;
		lpItem->m_nHeight = nHeight;
		lpItem->m_nLeftWidth = nLeftWidth;
		lpItem->m_nRightWidth = nRightWidth;
	}
}

void CSkinToolBar::SetItemPadding(int nIndex, int nPadding)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_nPadding = nPadding;
}

void CSkinToolBar::SetItemMargin(int nIndex, int nLeftMargin, int nRightMargin)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_nLeftMargin = nLeftMargin;
		lpItem->m_nRightMargin = nRightMargin;
	}
}

void CSkinToolBar::SetItemText(int nIndex, LPCTSTR lpszText)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_strText = lpszText;
}

void CSkinToolBar::SetItemToolTipText(int nIndex, LPCTSTR lpszText)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL)
		lpItem->m_strToolTipText = lpszText;
}

BOOL CSkinToolBar::SetItemBgPic(int nIndex, LPCTSTR lpNormal, 
								LPCTSTR lpHighlight, LPCTSTR lpDown, const CRect& rcNinePart)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
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

BOOL CSkinToolBar::SetItemLeftBgPic(int nIndex, LPCTSTR lpHighlight, 
									LPCTSTR lpDown, const CRect& rcNinePart)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpLeftH);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpLeftD);

	lpItem->m_lpLeftH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	lpItem->m_lpLeftD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (lpItem->m_lpLeftH != NULL)
		lpItem->m_lpLeftH->SetNinePart(rcNinePart);

	if (lpItem->m_lpLeftD != NULL)
		lpItem->m_lpLeftD->SetNinePart(rcNinePart);

	if ((lpHighlight != NULL && NULL == lpItem->m_lpLeftH) ||
		(lpDown != NULL && NULL == lpItem->m_lpLeftD))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinToolBar::SetItemRightBgPic(int nIndex, LPCTSTR lpHighlight, 
									 LPCTSTR lpDown, const CRect& rcNinePart)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpRightH);
	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpRightD);

	lpItem->m_lpRightH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	lpItem->m_lpRightD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (lpItem->m_lpRightH != NULL)
		lpItem->m_lpRightH->SetNinePart(rcNinePart);

	if (lpItem->m_lpRightD != NULL)
		lpItem->m_lpRightD->SetNinePart(rcNinePart);

	if ((lpHighlight != NULL && NULL == lpItem->m_lpRightH) ||
		(lpDown != NULL && NULL == lpItem->m_lpRightD))
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinToolBar::SetItemSepartorPic(int nIndex, LPCTSTR lpszFileName)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpSepartorImg);

	lpItem->m_lpSepartorImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == lpItem->m_lpSepartorImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinToolBar::SetItemArrowPic(int nIndex, LPCTSTR lpszFileName)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpArrowImg);

	lpItem->m_lpArrowImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == lpItem->m_lpArrowImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinToolBar::SetItemIconPic(int nIndex, LPCTSTR lpszFileName)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return FALSE;

	CSkinManager::GetInstance()->ReleaseImage(lpItem->m_lpIconImg);

	lpItem->m_lpIconImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == lpItem->m_lpIconImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinToolBar::GetItemRectByIndex(int nIndex, CRect& rect)
{
	CSkinToolBarItem* lpItem;
	int nLeft = m_nLeft, nTop = m_nTop;

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			if (i == nIndex)
			{
				rect = CRect(nLeft+lpItem->m_nLeftMargin, nTop, nLeft+lpItem->m_nLeftMargin+lpItem->m_nWidth, nTop+lpItem->m_nHeight);
				return TRUE;
			}

            //计算垂直布局
            if (m_bVertical)
            {
                nTop += lpItem->m_nHeight;
                //TODO: 加上topPadding和上下margin
                nTop += lpItem->m_nPadding;
                //nLeft += lpItem->m_nLeftMargin;
                //nLeft += lpItem->m_nRightMargin;
            }
            else
            {
                nLeft += lpItem->m_nWidth;
                nLeft += lpItem->m_nPadding;
                nLeft += lpItem->m_nLeftMargin;
                nLeft += lpItem->m_nRightMargin;
            }
		}
	}

	return FALSE;
}

BOOL CSkinToolBar::GetItemRectByID(int nID, CRect& rect)
{
	CSkinToolBarItem* lpItem;
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

BOOL CSkinToolBar::GetItemCheckState(int nIndex)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	return ((lpItem != NULL) ? lpItem->m_bChecked : FALSE);
}

void CSkinToolBar::SetItemCheckState(int nIndex, BOOL bChecked)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (lpItem != NULL && lpItem->m_bChecked != bChecked)
	{
		lpItem->m_bChecked = bChecked;
	}
}

void CSkinToolBar::SetUseGroup(bool bUseGroup)
{
    m_bUseGroup = bUseGroup;
}

BOOL CSkinToolBar::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinToolBar::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

	if (m_bTransparent)
		DrawParentWndBg(MemDC.m_hDC);
    else
    {
        if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())
            m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);
        else
        {
            MemDC.FillSolidRect(&rcClient, m_clrBgColor);
            //HBRUSH hBgBrush = ::CreateSolidBrush(m_clrBgColor);
            ////HBRUSH hOldBrush = (HBRUSH)::SelectObject(MemDC.m_hDC, hBgBrush);
            //::FillRect(MemDC.m_hDC, &rcClient, hBgBrush);
            //::DeleteObject(hBgBrush);
        }
    }

	size_t nSize = m_arrItems.size();
	for (size_t i = 0; i < nSize; ++i)
	{
		DrawItem(MemDC.m_hDC, i);
	}
}

void CSkinToolBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	m_nPressIndex = HitTest(point);
	if (m_nPressIndex != -1)
	{
		CSkinToolBarItem* lpItem = GetItemByIndex(m_nPressIndex);
		if (lpItem != NULL)
		{
			CRect rcItem;
			GetItemRectByIndex(m_nPressIndex, rcItem);

			CRect rcLeft, rcRight;
			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN ||
				lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
			{
				rcLeft = rcItem;
				rcLeft.right = rcLeft.left + lpItem->m_nLeftWidth;

				rcRight = rcItem;
				rcRight.left += lpItem->m_nLeftWidth;
				rcRight.right = rcRight.left + lpItem->m_nRightWidth;

				if (rcLeft.PtInRect(point))
					m_bPressLorR = TRUE;
				
				if (rcRight.PtInRect(point))
					m_bPressLorR = FALSE;
			}

			if (lpItem->m_dwStyle & STBI_STYLE_CHECK && !((lpItem->m_dwStyle & STBI_STYLE_DROPDOWN) && !m_bPressLorR))
			{
				lpItem->m_bChecked = !lpItem->m_bChecked;

                //启用组选项之后，一个选中其它的就要被取消选中
                if (lpItem->m_bChecked && m_bUseGroup)
                {
                    CSkinToolBarItem* lpOtherItem = NULL;
                    int nItemCount = (int)m_arrItems.size();
                    for (int i = 0; i < nItemCount; ++i)
                    {
                        lpOtherItem = GetItemByIndex(i);
                        if (lpOtherItem != NULL && lpOtherItem != lpItem)
                        {
                            lpOtherItem->m_bChecked = FALSE;
                        }
                    }
                }
                 
				Invalidate();

				if (lpItem->m_bChecked)
				{
					::SendMessage(::GetParent(m_hWnd), WM_COMMAND, 
						MAKEWPARAM(lpItem->m_nID, BN_PUSHED), 0);
				}
				else
				{
					::SendMessage(::GetParent(m_hWnd), WM_COMMAND, 
						MAKEWPARAM(lpItem->m_nID, BN_UNPUSHED), 0);
				}
			}
			else
			{
				if ((lpItem->m_dwStyle & STBI_STYLE_DROPDOWN/* && m_bPressLorR*/)
					|| (lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN))
				{
					Invalidate();

					NMTOOLBAR nmtb = {0};
					nmtb.hdr.hwndFrom = m_hWnd;
					nmtb.hdr.idFrom = GetDlgCtrlID();
					nmtb.hdr.code = TBN_DROPDOWN;
					nmtb.iItem = lpItem->m_nID;
					nmtb.rcButton = rcItem;
					::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&nmtb);

					m_nPressIndex = -1;
					Invalidate();
				}
				else
				{
					Invalidate();
					::SetCapture(m_hWnd);
				}
			}
		}
	}
}

void CSkinToolBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	HWND hWnd = ::GetCapture();
	if (m_hWnd == hWnd)
		::ReleaseCapture();

	int nIndex = HitTest(point);

	BOOL bClicked;
	if (nIndex != -1 && nIndex == m_nPressIndex)
		bClicked = TRUE;
	else
		bClicked = FALSE;

	if (bClicked)
	{
		CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
		if (lpItem != NULL)
		{
			CRect rcItem;
			GetItemRectByIndex(m_nPressIndex, rcItem);

			CRect rcLeft, rcRight;
			BOOL bClickLorR = FALSE;
			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN ||
				lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
			{
				rcLeft = rcItem;
				rcLeft.right = rcLeft.left + lpItem->m_nLeftWidth;

				rcRight = rcItem;
				rcRight.left += lpItem->m_nLeftWidth;
				rcRight.right = rcRight.left + lpItem->m_nRightWidth;

				if (rcLeft.PtInRect(point))
					bClickLorR = TRUE;

				if (rcRight.PtInRect(point))
					bClickLorR = FALSE;
			}

			if ((!(lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN))
				|| (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN && bClickLorR && bClickLorR == m_bPressLorR))
				::SendMessage(::GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(lpItem->m_nID, BN_CLICKED), 0);
		}
	}
	
	//恢复按下之前的状态
	if (m_nPressIndex != -1)
	{
		m_nPressIndex = -1;
		Invalidate();
	}
}

void CSkinToolBar::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	if (m_nPressIndex != -1)
		return;

	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
	}

	int nIndex;
	BOOL bHoverLorR = FALSE;

	nIndex = HitTest(point);
	if (nIndex != -1)
	{
		CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
		if (lpItem != NULL)
		{
			CRect rcItem;
			GetItemRectByIndex(nIndex, rcItem);

			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN ||
				lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
			{
				CRect rcLeft(rcItem);
				rcLeft.right = rcLeft.left + lpItem->m_nLeftWidth;

				CRect rcRight(rcItem);
				rcRight.left += lpItem->m_nLeftWidth;
				rcRight.right = rcRight.left + lpItem->m_nRightWidth;

				if (rcLeft.PtInRect(point))
					bHoverLorR = TRUE;

				if (rcRight.PtInRect(point))
					bHoverLorR = FALSE;
			}

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

	if (nIndex != m_nHoverIndex || m_bHoverLorR != bHoverLorR)
	{
		m_nHoverIndex = nIndex;
		m_bHoverLorR = bHoverLorR;
		Invalidate();
	}
}

void CSkinToolBar::OnMouseLeave()
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

LRESULT CSkinToolBar::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SetMsgHandled(FALSE);
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	return 1;
}

void CSkinToolBar::OnDestroy()
{
	SetMsgHandled(FALSE);

	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	CSkinToolBarItem* lpItem;
	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
			delete lpItem;
	}
	m_arrItems.clear();

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);

	m_nPressIndex = m_nHoverIndex = -1;
	m_bPressLorR = m_bHoverLorR = FALSE;
	m_bMouseTracking = FALSE;
	m_nLeft = m_nTop = 0;
	m_bAutoSize = FALSE;
	m_bTransparent = FALSE;
}

BOOL CSkinToolBar::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinToolBar::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

CSkinToolBarItem* CSkinToolBar::GetItemByIndex(int nIndex)
{
	if (nIndex >= 0 && nIndex < (int)m_arrItems.size())
		return m_arrItems[nIndex];
	else
		return NULL;
}

CSkinToolBarItem* CSkinToolBar::GetItemByID(int nID)
{
	CSkinToolBarItem* lpItem;
	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL && lpItem->m_nID == nID)
			return lpItem;
	}

	return NULL;
}

int CSkinToolBar::HitTest(POINT pt)
{
	CSkinToolBarItem* lpItem;
	int nLeft = m_nLeft, nTop = m_nTop;
	CRect rcItem;

	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			rcItem = CRect(nLeft+lpItem->m_nLeftMargin, nTop, nLeft+lpItem->m_nLeftMargin+lpItem->m_nWidth, nTop+lpItem->m_nHeight);
			if (rcItem.PtInRect(pt))
			{
				if (lpItem->m_dwStyle & STBI_STYLE_SEPARTOR)
					return -1;
				else
					return i;
			}
            if (m_bVertical)
            {
                nTop += lpItem->m_nHeight;

                //TODO: 后面加上上下padding和上下间距
                nTop += lpItem->m_nPadding;
                //nLeft += lpItem->m_nLeftMargin;
                //nLeft += lpItem->m_nRightMargin;
            }
            else
            {
                nLeft += lpItem->m_nWidth;
                nLeft += lpItem->m_nPadding;
                nLeft += lpItem->m_nLeftMargin;
                nLeft += lpItem->m_nRightMargin;
            }
		}
	}

	return -1;
}

void CSkinToolBar::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinToolBar::DrawItem(HDC hDC, int nIndex)
{
	CSkinToolBarItem* lpItem = GetItemByIndex(nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nIndex, rcItem);

	CRect rcLeft, rcRight;
	if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN
		|| lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
	{
		rcLeft = rcItem;
		rcLeft.right = rcLeft.left + lpItem->m_nLeftWidth;

		rcRight = rcItem;
		rcRight.left += lpItem->m_nLeftWidth;
		rcRight.right = rcRight.left + lpItem->m_nRightWidth;
	}

	if (lpItem->m_dwStyle & STBI_STYLE_SEPARTOR)
	{
		if (lpItem->m_lpSepartorImg != NULL && !lpItem->m_lpSepartorImg->IsNull())
			lpItem->m_lpSepartorImg->Draw2(hDC, rcItem);
		return;
	}

	if ((lpItem->m_dwStyle & STBI_STYLE_CHECK) && lpItem->m_bChecked)
	{
		if (lpItem->m_lpBgImgD != NULL && !lpItem->m_lpBgImgD->IsNull())
			lpItem->m_lpBgImgD->Draw2(hDC, rcItem);

		rcItem.OffsetRect(1, 1);

		if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN)
		{
			if (lpItem->m_lpLeftD != NULL && !lpItem->m_lpLeftD->IsNull())
				lpItem->m_lpLeftD->Draw2(hDC, rcLeft);
			rcLeft.OffsetRect(1, 1);
		}

		if (m_nPressIndex == nIndex)
		{
			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN)
			{
				if (!m_bPressLorR)
				{
					if (lpItem->m_lpRightD != NULL && !lpItem->m_lpRightD->IsNull())
						lpItem->m_lpRightD->Draw2(hDC, rcRight);

					rcRight.OffsetRect(1, 1);
				}
			}
		}
		else if (m_nHoverIndex == nIndex)
		{
			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN)
			{
				if (!m_bHoverLorR)
				{
					if (lpItem->m_lpRightH != NULL && !lpItem->m_lpRightH->IsNull())
						lpItem->m_lpRightH->Draw2(hDC, rcRight);
				}
			}
		}
	}
	else
	{
		if (m_nPressIndex == nIndex)
		{
			if (lpItem->m_lpBgImgD != NULL && !lpItem->m_lpBgImgD->IsNull())
				lpItem->m_lpBgImgD->Draw2(hDC, rcItem);

			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN)
			{
				if (m_bPressLorR)
				{
					if (lpItem->m_lpLeftD != NULL && !lpItem->m_lpLeftD->IsNull())
						lpItem->m_lpLeftD->Draw2(hDC, rcLeft);
				}
				else
				{
					if (lpItem->m_lpRightD != NULL && !lpItem->m_lpRightD->IsNull())
						lpItem->m_lpRightD->Draw2(hDC, rcRight);
				}
			}

			rcItem.OffsetRect(1, 1);

			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN
				|| lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
			{
				if (m_bPressLorR)
					rcLeft.OffsetRect(1, 1);
				else
					rcRight.OffsetRect(1, 1);
			}
		}
		else if (m_nHoverIndex == nIndex)
		{
			if (lpItem->m_lpBgImgH != NULL && !lpItem->m_lpBgImgH->IsNull())
				lpItem->m_lpBgImgH->Draw2(hDC, rcItem);

			if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN)
			{
				if (m_bHoverLorR)
				{
					if (lpItem->m_lpLeftH != NULL && !lpItem->m_lpLeftH->IsNull())
						lpItem->m_lpLeftH->Draw2(hDC, rcLeft);
				}
				else
				{
					if (lpItem->m_lpRightH != NULL && !lpItem->m_lpRightH->IsNull())
						lpItem->m_lpRightH->Draw2(hDC, rcRight);
				}
			}
		}
        //画正常状态下的背景图片
        else
        {
            if (lpItem->m_lpBgImgN != NULL && !lpItem->m_lpBgImgN->IsNull())
                lpItem->m_lpBgImgN->Draw2(hDC, rcItem);
        }
	}

	BOOL bHasText = FALSE;
	if (lpItem->m_strText.GetLength() > 0)
		bHasText = TRUE;

	BOOL bHasIcon = FALSE;
	if (lpItem->m_lpIconImg != NULL && !lpItem->m_lpIconImg->IsNull())
		bHasIcon = TRUE;

	if (!(lpItem->m_dwStyle & STBI_STYLE_DROPDOWN
		|| lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN))
		rcLeft = rcItem;

	if (bHasIcon && bHasText)	// 带图标和文字
	{
		int cxIcon = lpItem->m_lpIconImg->GetWidth();
		int cyIcon = lpItem->m_lpIconImg->GetHeight();

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
		CalcCenterRect(rcLeft, cx, cy, rcCenter);

		CRect rcIcon(rcCenter.left, rcCenter.top, rcCenter.left+cxIcon, rcCenter.bottom);
		lpItem->m_lpIconImg->Draw2(hDC, rcIcon);

		UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
		rcText = CRect(rcIcon.right+1, rcLeft.top, rcIcon.right+1+rcText.Width(), rcLeft.bottom);
		::DrawText(hDC, lpItem->m_strText, lpItem->m_strText.GetLength(), &rcText, nFormat);

		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
	else if (bHasIcon)	// 仅图标
	{
		int cxIcon = lpItem->m_lpIconImg->GetWidth();
		int cyIcon = lpItem->m_lpIconImg->GetHeight();

		CRect rcIcon;
		CalcCenterRect(rcLeft, cxIcon, cyIcon, rcIcon);

		lpItem->m_lpIconImg->Draw2(hDC, rcIcon);
	}
	else if (bHasText)	// 仅文字
	{
		UINT nFormat = DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		if (NULL == hFont)
			hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		::DrawText(hDC, lpItem->m_strText, lpItem->m_strText.GetLength(), &rcLeft, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}

	if (lpItem->m_dwStyle & STBI_STYLE_DROPDOWN
		|| lpItem->m_dwStyle & STBI_STYLE_WHOLEDROPDOWN)
	{
		if (lpItem->m_lpArrowImg != NULL && !lpItem->m_lpArrowImg->IsNull())
		{
			int cxArrow = lpItem->m_lpArrowImg->GetWidth();
			int cyArrow = lpItem->m_lpArrowImg->GetHeight();

			CRect rcArrow;
			CalcCenterRect(rcRight, cxArrow, cyArrow, rcArrow);
			rcArrow.left = rcRight.left;
			rcArrow.right = rcArrow.left + cxArrow;

			lpItem->m_lpArrowImg->Draw2(hDC, rcArrow);
		}
	}
}


