#include "stdafx.h"
#include "SkinPictureBox.h"

CSkinPictureBox::CSkinPictureBox(void)
{
	m_lpBgImgN = m_lpBgImgH = m_lpBgImgD = NULL;
	m_lpImage = NULL;
	m_lpImageGray = NULL;
	m_lpGifImage = NULL;
	m_lpMobileImage = NULL;
	m_hCursor = NULL;
	m_bFocus = m_bPress = m_bHover = m_bMouseTracking = FALSE;
	m_bSelectedStatus = FALSE;
	m_bGray = FALSE;
	m_bUseCache = FALSE;
	m_nDrawMode = DRAW_MODE_CENTER;
	m_dwTimerId = NULL;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
	m_bShowMobileImage = FALSE;
}

CSkinPictureBox::~CSkinPictureBox(void)
{
}

BOOL CSkinPictureBox::SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	
	m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpBgImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (NULL == m_lpBgImgN || NULL == m_lpBgImgH || NULL == m_lpBgImgD)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinPictureBox::SetBitmap(LPCTSTR lpszFileName, BOOL bIsGif/* = FALSE*/, BOOL bGray/*=FALSE*/)
{
	if (m_lpGifImage != NULL)
	{
		delete m_lpGifImage;
		m_lpGifImage = NULL;
	}

	if (m_lpImage != NULL)
	{
		CSkinManager::GetInstance()->ReleaseImage(m_lpImage);
		m_lpImage = NULL;
	}

	if(m_lpImageGray == NULL)
	{
		m_lpImageGray = new CImageEx();
		BOOL bRet = m_lpImageGray->LoadFromFile(lpszFileName);
		if (!bRet)
			return FALSE;

		m_lpImageGray->GrayScale();
	}

	if (m_dwTimerId != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;
	}

	if (NULL == lpszFileName)
		return FALSE;
	
	if (!bIsGif)
	{
		m_bGray = bGray;
		m_lpImage = CSkinManager::GetInstance()->GetImage(lpszFileName);
		return (m_lpImage != NULL) ? TRUE : FALSE;
	}
	else
	{
		m_lpGifImage = new CGifImage;
		if (NULL == m_lpGifImage)
			return FALSE;
		BOOL bRet = m_lpGifImage->LoadFromFile(lpszFileName);
		if (!bRet)
			return FALSE;
		if (m_lpGifImage->IsAnimatedGif())
			m_dwTimerId = SetTimer(1, m_lpGifImage->GetFrameDelay(), NULL);
		return TRUE;
	}
}

BOOL CSkinPictureBox::SetBitmapWithoutCache(LPCTSTR lpszFileName, BOOL bIsGif/* = FALSE*/, BOOL bGray/*=FALSE*/)
{
	if (m_lpGifImage != NULL)
	{
		delete m_lpGifImage;
		m_lpGifImage = NULL;
	}

	if (m_lpImage != NULL)
	{
		CSkinManager::GetInstance()->ReleaseImage(m_lpImage);
		m_lpImage = NULL;
	}

	if(m_lpImageGray != NULL)
	{
		delete m_lpImageGray;
		m_lpImageGray = NULL;
	}
	m_lpImageGray = new CImageEx();
	BOOL bRet = m_lpImageGray->LoadFromFile(lpszFileName);
	if (!bRet)
		return FALSE;

	m_lpImageGray->GrayScale();
	

	if (m_dwTimerId != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;
	}

	if (NULL == lpszFileName)
		return FALSE;
	
	if (!bIsGif)
	{
		m_bUseCache = TRUE;
		m_bGray = bGray;
		m_lpImage = new CImageEx();
		BOOL bRet = m_lpImage->LoadFromFile(lpszFileName);
		if (!bRet)
			return FALSE;
	}
	else
	{
		m_lpGifImage = new CGifImage;
		if (NULL == m_lpGifImage)
			return FALSE;
		BOOL bRet = m_lpGifImage->LoadFromFile(lpszFileName);
		if (!bRet)
			return FALSE;
		if (m_lpGifImage->IsAnimatedGif())
			m_dwTimerId = SetTimer(1, m_lpGifImage->GetFrameDelay(), NULL);
		return TRUE;
	}

	return TRUE;
}

BOOL CSkinPictureBox::SetBitmap(const BYTE* lpData, DWORD dwSize)
{
	if (m_lpGifImage != NULL)
	{
		delete m_lpGifImage;
		m_lpGifImage = NULL;
	}

	if (m_lpImage != NULL)
	{
		CSkinManager::GetInstance()->ReleaseImage(m_lpImage);
		m_lpImage = NULL;
	}

	if (m_dwTimerId != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;
	}

	if (NULL == lpData || dwSize <= 0)
		return FALSE;

	m_lpGifImage = new CGifImage;
	if (NULL == m_lpGifImage)
		return FALSE;
	BOOL bRet = m_lpGifImage->LoadFromBuffer(lpData, dwSize);
	if (!bRet)
		return FALSE;
	if (m_lpGifImage->IsAnimatedGif())
		m_dwTimerId = SetTimer(1, m_lpGifImage->GetFrameDelay(), NULL);
	return TRUE;
}

BOOL CSkinPictureBox::SetMobileBitmap(LPCTSTR lpszFileName, BOOL bShow/* = FALSE*/)
{
	if (NULL == lpszFileName)
		return FALSE;
	
	if (m_lpMobileImage != NULL)
	{
		CSkinManager::GetInstance()->ReleaseImage(m_lpMobileImage);
		m_lpMobileImage = NULL;
	} 

	m_bShowMobileImage = bShow;

	m_lpMobileImage = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpMobileImage != NULL) ? TRUE : FALSE;
}

void CSkinPictureBox::SetToolTipText(LPCTSTR lpszText)
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

void CSkinPictureBox::SetDrawMode(DRAW_MODE nMode/* = DRAW_MODE_CENTER*/)
{
	m_nDrawMode = nMode;
}

void CSkinPictureBox::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

void CSkinPictureBox::SetShowCursor(BOOL bShow)
{
	if (bShow)
	{
		if (NULL == m_hCursor)
			m_hCursor = ::LoadCursor(NULL, IDC_HAND);
	}
	else
	{
		if (m_hCursor != NULL)
		{
			::DeleteObject(m_hCursor);
			m_hCursor = NULL;
		}
	}
}

BOOL CSkinPictureBox::SubclassWindow(HWND hWnd)
{
	BOOL bRet = __super::SubclassWindow(hWnd);
	if (bRet)
	{
		DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_STYLE);
		dwStyle |= SS_NOTIFY | SS_BITMAP;
		::SetWindowLong(m_hWnd, GWL_STYLE, dwStyle);
	}
	return bRet;
}

void CSkinPictureBox::SetSelectedStatus(BOOL bSelectedStatus)
{
	m_bSelectedStatus = bSelectedStatus;
	::InvalidateRect(m_hWnd, NULL, TRUE);
}

BOOL CSkinPictureBox::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinPictureBox::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);

	CRect rcClient;
	GetClientRect(&rcClient);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

	if (m_bTransparent)
		DrawParentWndBg(MemDC.m_hDC);

	int nMode = ::SetStretchBltMode(MemDC.m_hDC, HALFTONE);
	if (m_lpImage != NULL && !m_lpImage->IsNull())
	{
		if (DRAW_MODE_CENTER == m_nDrawMode)
		{
			int cx = m_lpImage->GetWidth();
			int cy = m_lpImage->GetHeight();

			CRect rcCenter;
			CalcCenterRect(rcClient, cx, cy, rcCenter);

			if(m_bGray)
				m_lpImageGray->Draw(MemDC.m_hDC, rcCenter);
			else
				m_lpImage->Draw(MemDC.m_hDC, rcCenter);
		}
		else if (DRAW_MODE_STRETCH == m_nDrawMode)
		{
			m_lpImage->Draw(MemDC.m_hDC, rcClient);
		}
	}
	else if (m_lpGifImage != NULL)
	{
		if (DRAW_MODE_CENTER == m_nDrawMode)
		{
			int cx = m_lpGifImage->GetWidth();
			int cy = m_lpGifImage->GetHeight();

			CRect rcCenter;
			CalcCenterRect(rcClient, cx, cy, rcCenter);

			m_lpGifImage->Draw(MemDC.m_hDC, rcCenter);
		}
		else if (DRAW_MODE_STRETCH == m_nDrawMode)
		{
			m_lpGifImage->Draw(MemDC.m_hDC, rcClient);
		}
	}

	if(m_bShowMobileImage && m_lpMobileImage!=NULL && !m_lpImage->IsNull())
	{
		CRect rcMobileImage;
		rcMobileImage.right = rcClient.right-2;
		rcMobileImage.left = rcMobileImage.right-16;
		rcMobileImage.bottom = rcClient.bottom-4;
		rcMobileImage.top = rcMobileImage.bottom-16;
		m_lpMobileImage->Draw(MemDC.m_hDC, rcMobileImage);
	}


	::SetStretchBltMode(MemDC.m_hDC, nMode);

	if(m_bSelectedStatus)
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw(MemDC.m_hDC, rcClient);
	}
	if (m_bPress)	// 鼠标左键按下状态
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw(MemDC.m_hDC, rcClient);
	}
	else if (m_bHover)	// 鼠标悬停状态
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
			m_lpBgImgH->Draw(MemDC.m_hDC, rcClient);
	}
	else	// 普通状态
	{
		if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
			m_lpBgImgN->Draw(MemDC.m_hDC, rcClient);
	}
}

void CSkinPictureBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_bPress = TRUE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinPictureBox::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bPress)
	{
		m_bPress = FALSE;
		::InvalidateRect(m_hWnd, NULL, TRUE);
	}

	SetMsgHandled(FALSE);
}

void CSkinPictureBox::OnMouseMove(UINT nFlags, CPoint point)
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

void CSkinPictureBox::OnMouseLeave()
{
	m_bMouseTracking = FALSE;
	m_bHover = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinPictureBox::OnSetFocus(CWindow wndOld)
{
	m_bFocus = TRUE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

void CSkinPictureBox::OnKillFocus(CWindow wndFocus)
{
	m_bFocus = FALSE;
	::InvalidateRect(m_hWnd, NULL, TRUE);
	SetMsgHandled(FALSE);
}

BOOL CSkinPictureBox::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
	if (m_hCursor != NULL)
	{
		::SetCursor(m_hCursor);
		return TRUE;
	}
	SetMsgHandled(FALSE);
	return FALSE;
}

void CSkinPictureBox::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_dwTimerId && m_lpGifImage != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;

		m_dwTimerId = SetTimer(1, m_lpGifImage->GetFrameDelay(), NULL);

		m_lpGifImage->ActiveNextFrame();

		if (IsWindowVisible())
			Invalidate();
	}
}

LRESULT CSkinPictureBox::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	SetMsgHandled(FALSE);
	return 1;
}

void CSkinPictureBox::OnDestroy()
{
	if (m_dwTimerId != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;
	}

	if (m_hCursor != NULL)
	{
		::DeleteObject(m_hCursor);
		m_hCursor = NULL;
	}

	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpImage);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMobileImage);

	if(m_lpImageGray != NULL)
	{
		delete m_lpImageGray;
		m_lpImageGray = NULL;
	}

	if(m_bUseCache)
	{
		delete m_lpImage;
		m_lpImage = NULL;
	}

	if (m_lpGifImage != NULL)
	{
		delete m_lpGifImage;
		m_lpGifImage = NULL;
	}
}

BOOL CSkinPictureBox::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinPictureBox::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinPictureBox::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int cx2 = rcDest.Width();
	int cy2 = rcDest.Height();

	int x, y;
	if (cx2 <= cx)
	{
		x = 3;
		cx = cx2-6;
	}
	else
		x = (cx2 - cx + 1) / 2;

	if (cy2 <= cy)
	{
		y = 3;
		cy = cy2-6;
	}
	else
		y = (cy2 - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}