#include "stdafx.h"
#include "SkinDialog.h"


#ifdef DrawEclosionText

// 绘制羽化文字
void DrawEclosionText(HDC hDC, LPCTSTR lpszText, LPCTSTR lpszFontName, int nFontSize)
{
	int nWidth = 56, nHeight = 16;

	Gdiplus::Bitmap bmp(nWidth, nHeight, 0, PixelFormat32bppARGB, NULL);
	Gdiplus::Graphics graphics(&bmp);
	Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
	Gdiplus::FontFamily fontFamily(lpszFontName, NULL);
	Gdiplus::StringFormat strFmt(0, 0);

	Gdiplus::RectF rc(0.0f, 0.0f, nWidth, nHeight);
	path.AddString(lpszText, -1, &fontFamily, Gdiplus::FontStyleBold, nFontSize, rc, &strFmt);

	Gdiplus::Matrix matrix(1.0f/* / 5.0f*/, 0.0f, 0.0f, 1.0f/* / 5.0f*/, -1.0f/* / 5.0f*/, -1.0f/* / 5.0f*/);
	
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetTransform(&matrix);

	Gdiplus::Color color((Gdiplus::ARGB)0x55CCE0EE);
	Gdiplus::Pen pen(color, 3);

	graphics.DrawPath(&pen, &path);

	Gdiplus::Color color2((Gdiplus::ARGB)0x55CCE0EE);
	Gdiplus::SolidBrush brush(color2);

	graphics.FillPath(&brush, &path);

 	Gdiplus::Bitmap bmp2(300, 40, 0, PixelFormat32bppARGB, NULL);
 	Gdiplus::Graphics graphics2(&bmp2);
 
 	graphics2.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
 	graphics2.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
 	graphics2.DrawImage(&bmp, 0, 0, nWidth, nHeight);
 
 	Gdiplus::Color color3((Gdiplus::ARGB)0xFF000000);
 	Gdiplus::SolidBrush brush2(color3);
 
 	graphics2.FillPath(&brush2, &path);

	CLSID clsid;
	::CLSIDFromString(_T("{557CF406-1A04-11D3-9A73-0000F81EF32E}"), &clsid);
	bmp2.Save(_T("c:\\CoolText.png"), &clsid, NULL);

	Gdiplus::Graphics graphics3(hDC);

	graphics3.DrawImage(&bmp2, 8, 4);
}
#endif

CSkinDialog::CSkinDialog(void)/*:m_hWndRgn(NULL)*/
{
	m_bHasTitleBar = TRUE;
	m_bHasMinBtn = m_bHasMaxBtn = m_bHasCloseBtn = TRUE;
	m_bSizeBox = TRUE;

	m_rcTitleBar = CRect(0,0,0,0);
	m_rcMinBtn = m_rcMaxBtn = m_rcCloseBtn = CRect(0,0,0,0);

	m_bMouseTracking = FALSE;
	m_bMinBtnPress = m_bMinBtnHover = FALSE;
	m_bMaxBtnPress = m_bMaxBtnHover = FALSE;
	m_bCloseBtnPress = m_bCloseBtnHover = FALSE;

	//m_clrBg = ::GetSysColor(COLOR_BTNFACE);
    m_clrBg = RGB(255, 255, 255);

	m_lpBgImg = NULL;
	m_lpTitleBarBgImg = NULL;
	m_lpMinSysBtnImgN = m_lpMinSysBtnImgH = m_lpMinSysBtnImgD = NULL;
	m_lpMaxSysBtnImgN = m_lpMaxSysBtnImgH = m_lpMaxSysBtnImgD = NULL;
	m_lpRestoreSysBtnImgN = m_lpRestoreSysBtnImgH = m_lpRestoreSysBtnImgD = NULL;
	m_lpCloseSysBtnImgN = m_lpCloseSysBtnImgH = m_lpCloseSysBtnImgD = NULL;

	m_hMemDC = NULL;
	m_hMemBmp = m_hOldBmp = NULL;

	m_hHotRgn = NULL;

	m_hDragRgn = NULL;
}

CSkinDialog::~CSkinDialog(void)
{
	if(m_hHotRgn)
		::DeleteObject(m_hHotRgn);
	//if(m_hWndRgn)
	//	::DeleteObject(m_hWndRgn);
}

void CSkinDialog::SetBgColor(COLORREF clrBg)
{
	m_clrBg = clrBg;
}

BOOL CSkinDialog::SetBgPic(LPCTSTR lpFileName)
{
	return SetBgPic(lpFileName, CRect(0,0,0,0));
}

BOOL CSkinDialog::SetBgPic(LPCTSTR lpFileName, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);

	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpFileName);

	if (m_lpBgImg != NULL)
		m_lpBgImg->SetNinePart(rcNinePart);

	return (NULL == m_lpBgImg) ? FALSE : TRUE;
}

BOOL CSkinDialog::SetTitleBarBgPic(LPCTSTR lpFileName)
{
	return SetTitleBarBgPic(lpFileName, CRect(0,0,0,0));
}

BOOL CSkinDialog::SetTitleBarBgPic(LPCTSTR lpFileName, const CRect& rcNinePart)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpTitleBarBgImg);

	m_lpTitleBarBgImg = CSkinManager::GetInstance()->GetImage(lpFileName);

	if (m_lpTitleBarBgImg != NULL)
		m_lpTitleBarBgImg->SetNinePart(rcNinePart);

	return (NULL == m_lpTitleBarBgImg) ? FALSE : TRUE;
}

BOOL CSkinDialog::SetMinSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgD);

	m_lpMinSysBtnImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpMinSysBtnImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpMinSysBtnImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (NULL == m_lpMinSysBtnImgN || 
		NULL == m_lpMinSysBtnImgH || NULL == m_lpMinSysBtnImgD)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinDialog::SetMaxSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgD);

	m_lpMaxSysBtnImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpMaxSysBtnImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpMaxSysBtnImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (NULL == m_lpMaxSysBtnImgN || 
		NULL == m_lpMaxSysBtnImgH || NULL == m_lpMaxSysBtnImgD)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinDialog::SetRestoreSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgD);

	m_lpRestoreSysBtnImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpRestoreSysBtnImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpRestoreSysBtnImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (NULL == m_lpRestoreSysBtnImgN || 
		NULL == m_lpRestoreSysBtnImgH || NULL == m_lpRestoreSysBtnImgD)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinDialog::SetCloseSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgD);

	m_lpCloseSysBtnImgN = CSkinManager::GetInstance()->GetImage(lpNormal);
	m_lpCloseSysBtnImgH = CSkinManager::GetInstance()->GetImage(lpHighlight);
	m_lpCloseSysBtnImgD = CSkinManager::GetInstance()->GetImage(lpDown);

	if (NULL == m_lpCloseSysBtnImgN || 
		NULL == m_lpCloseSysBtnImgH || NULL == m_lpCloseSysBtnImgD)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinDialog::SetTitleText(LPCTSTR lpszText)
{
	m_strTitleText = lpszText;
	SetWindowText(lpszText);
	if (IsWindowVisible())
		Invalidate();
	return TRUE;
}

CString CSkinDialog::GetTitleText()
{
	return m_strTitleText;
}

HDC CSkinDialog::GetBgDC()
{
	return m_hMemDC;
}

BOOL CSkinDialog::SubclassWindow(HWND hWnd)
{
	__super::SubclassWindow(hWnd);

	DWORD dwStyle = GetStyle();

	m_bHasMinBtn = dwStyle & WS_MINIMIZEBOX;
	m_bHasMaxBtn = dwStyle & WS_MAXIMIZEBOX;
	m_bHasCloseBtn = dwStyle & WS_CAPTION;
	m_bSizeBox = dwStyle & WS_SIZEBOX;

	dwStyle &= ~WS_CAPTION;
	dwStyle &= ~DS_3DLOOK;
	dwStyle &= ~DS_FIXEDSYS;
	dwStyle &= ~WS_CLIPCHILDREN;	// WS_CLIPCHILDREN：裁剪子窗口样式，如果设置了该样式，在父窗口重绘时就不会绘制子窗口区域
//	dwStyle |= WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
	SetWindowLong(GWL_STYLE, dwStyle);

	DWORD dwStyleEx = GetExStyle();
	dwStyleEx = WS_EX_STATICEDGE | WS_EX_APPWINDOW;
	SetWindowLong(GWL_EXSTYLE, dwStyleEx);

	SetWindowPos(NULL, 0, 0, 0, 0, 
		SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

	m_bMouseTracking = FALSE;
	m_bMinBtnPress = m_bMinBtnHover = FALSE;
	m_bMaxBtnPress = m_bMaxBtnHover = FALSE;
	m_bCloseBtnPress = m_bCloseBtnHover = FALSE;

	CalcTitleBarRect();

	CRect rcClient;
	GetClientRect(&rcClient);

	HDC hDC = ::GetDC(m_hWnd);
	m_hMemDC = ::CreateCompatibleDC(hDC);
	m_hMemBmp = ::CreateCompatibleBitmap(hDC, rcClient.Width(), rcClient.Height());
	m_hOldBmp = (HBITMAP)::SelectObject(m_hMemDC, m_hMemBmp);
	::ReleaseDC(m_hWnd, hDC);

	return TRUE;
}

void CSkinDialog::SetHotRegion(HRGN hrgnDrag)
{
	m_hHotRgn = hrgnDrag;
}

void CSkinDialog::SetDragRegion(HRGN hrgnDrag)
{
	m_hDragRgn = hrgnDrag;
}

BOOL CSkinDialog::OnNcCreate(LPCREATESTRUCT lpCreateStruct)
{
	return 1;
}

int CSkinDialog::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	DWORD dwStyle = GetStyle();

	m_bHasMinBtn = dwStyle & WS_MINIMIZEBOX;
	m_bHasMaxBtn = dwStyle & WS_MAXIMIZEBOX;
	m_bHasCloseBtn = dwStyle & WS_CAPTION;
	m_bSizeBox = dwStyle & WS_SIZEBOX;

	dwStyle &= ~WS_CAPTION;
	dwStyle &= ~DS_3DLOOK;
	dwStyle &= ~DS_FIXEDSYS;
	dwStyle |= WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
	SetWindowLong(GWL_STYLE, dwStyle);

	DWORD dwStyleEx = GetExStyle();
	dwStyleEx = WS_EX_STATICEDGE | WS_EX_APPWINDOW;
	SetWindowLong(GWL_EXSTYLE, dwStyleEx);
	
	SetWindowPos(NULL, 0, 0, 0, 0, 
		SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

	CalcTitleBarRect();

	return TRUE;
}

//HBRUSH CSkinDialog::OnCtlColorStatic(CDCHandle dc, CStatic wndStatic)
//{
//	dc.SetBkMode(TRANSPARENT);
//	return (HBRUSH)::GetStockObject(NULL_BRUSH);
//}

BOOL CSkinDialog::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	DWORD dwStyle = GetStyle();

	m_bHasMinBtn = dwStyle & WS_MINIMIZEBOX;
	m_bHasMaxBtn = dwStyle & WS_MAXIMIZEBOX;
	m_bHasCloseBtn = dwStyle & WS_CAPTION;
	m_bSizeBox = dwStyle & WS_SIZEBOX;

	dwStyle &= ~WS_CAPTION;
	dwStyle &= ~DS_3DLOOK;
	dwStyle &= ~DS_FIXEDSYS;
	dwStyle |= WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
	SetWindowLong(GWL_STYLE, dwStyle);

	DWORD dwStyleEx = GetExStyle();
	dwStyleEx = WS_EX_STATICEDGE | WS_EX_APPWINDOW;
	SetWindowLong(GWL_EXSTYLE, dwStyleEx);

	SetWindowPos(NULL, 0, 0, 0, 0, 
		SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

	CalcTitleBarRect();

	return TRUE;
}

BOOL CSkinDialog::OnNcActivate(BOOL bActive)
{
	if (IsIconic())
		SetMsgHandled(FALSE);
	return (bActive == FALSE) ? TRUE : FALSE;
}

UINT CSkinDialog::OnNcHitTest(CPoint point)
{
	ScreenToClient(&point);

	int nHitTest = HitTest(point);
	if (nHitTest != HTMINBUTTON && nHitTest != HTMAXBUTTON
		&& nHitTest != HTCLOSE && nHitTest != HTCAPTION)
		return nHitTest;
	else
		return HTCLIENT;
}

LRESULT CSkinDialog::OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam)
{
	if (bCalcValidRects)
	{
		CRect rcWindow;

		GetWindowRect(rcWindow);

		LPNCCALCSIZE_PARAMS pParam = (LPNCCALCSIZE_PARAMS)lParam;

		if (SWP_NOSIZE == (SWP_NOSIZE & pParam->lppos->flags))
			return 0;

		if (0 == (SWP_NOMOVE & pParam->lppos->flags))
		{
			rcWindow.left = pParam->lppos->x;
			rcWindow.top = pParam->lppos->y;
		}

		rcWindow.right = rcWindow.left + pParam->lppos->cx;
		rcWindow.bottom = rcWindow.top + pParam->lppos->cy;
		pParam->rgrc[0] = rcWindow;
		pParam->rgrc[1] = pParam->rgrc[0];
	}

	return 0;
}

void CSkinDialog::OnNcPaint(CRgnHandle rgn)
{
	return;
}

BOOL CSkinDialog::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinDialog::OnPaint(CDCHandle dc)
{
	CPaintDC PaintDC(m_hWnd);

	CRect rcClient;
	GetClientRect(&rcClient);

	int cx = 0, cy = 0;

	if (m_hMemBmp != NULL)
	{
		BITMAP bmpInfo = {0};
		::GetObject(m_hMemBmp, sizeof(BITMAP), &bmpInfo);
		cx = bmpInfo.bmWidth;
		cy = bmpInfo.bmHeight;
	}

	if (rcClient.Width() != cx || rcClient.Height() != cy)
	{
		if (m_hMemDC != NULL && m_hMemBmp != NULL)
		{
			::SelectObject(m_hMemDC, m_hOldBmp);
			::DeleteObject(m_hMemBmp);
		}

		m_hMemBmp = ::CreateCompatibleBitmap(PaintDC.m_hDC, rcClient.Width(), rcClient.Height());
		m_hOldBmp = (HBITMAP)::SelectObject(m_hMemDC, m_hMemBmp);
	}

	CDCHandle MemDC(m_hMemDC);
    MemDC.FillSolidRect(&rcClient, m_clrBg);

	//m_clrBg = RGB(255, 0, 0);
	if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())	// 填充背景图片
	{
		MemDC.FillSolidRect(&rcClient, m_clrBg);
		m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);
	}
	//else	// 填充背景颜色
	//{
	//	;
	//}

	if (m_lpTitleBarBgImg != NULL && !m_lpTitleBarBgImg->IsNull())	// 填充标题栏背景图片
	{
		m_lpTitleBarBgImg->Draw2(MemDC.m_hDC, m_rcTitleBar);
	}

// 	HPEN hPen = ::CreatePen(PS_SOLID | PS_INSIDEFRAME, 1, RGB(62, 114, 132));
// 	HPEN hOldPen = MemDC.SelectPen(hPen);
// 	HBRUSH hOldBrush = MemDC.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
// 	MemDC.RoundRect(rcClient.left, rcClient.top, 
// 		rcClient.right, rcClient.bottom, 4, 4);
// 	MemDC.SelectPen(hOldPen);
// 	MemDC.SelectBrush(hOldBrush);
// 	::DeleteObject(hPen);

	if (m_bHasMinBtn)	// 绘制最小化按钮
	{
		if (m_bMinBtnPress)
		{
			if (m_lpMinSysBtnImgD != NULL && !m_lpMinSysBtnImgD->IsNull())
				m_lpMinSysBtnImgD->Draw(MemDC.m_hDC, m_rcMinBtn);
		}
		else if(m_bMinBtnHover)
		{
			if (m_lpMinSysBtnImgH != NULL && !m_lpMinSysBtnImgH->IsNull())
				m_lpMinSysBtnImgH->Draw(MemDC.m_hDC, m_rcMinBtn);
		}
		else
		{
			if (m_lpMinSysBtnImgN != NULL && !m_lpMinSysBtnImgN->IsNull())
				m_lpMinSysBtnImgN->Draw(MemDC.m_hDC, m_rcMinBtn);
		}
	}

	if (m_bHasMaxBtn)	// 绘制最大化按钮
	{
		if (IsZoomed())		// 窗口最大化标志
		{
			if (m_bMaxBtnPress)
			{
				if (m_lpRestoreSysBtnImgD != NULL && !m_lpRestoreSysBtnImgD->IsNull())
					m_lpRestoreSysBtnImgD->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
			else if(m_bMaxBtnHover)
			{
				if (m_lpRestoreSysBtnImgH != NULL && !m_lpRestoreSysBtnImgH->IsNull())
					m_lpRestoreSysBtnImgH->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
			else
			{
				if (m_lpRestoreSysBtnImgN != NULL && !m_lpRestoreSysBtnImgN->IsNull())
					m_lpRestoreSysBtnImgN->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
		}
		else
		{
			if (m_bMaxBtnPress)
			{
				if (m_lpMaxSysBtnImgD != NULL && !m_lpMaxSysBtnImgD->IsNull())
					m_lpMaxSysBtnImgD->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
			else if(m_bMaxBtnHover)
			{
				if (m_lpMaxSysBtnImgH != NULL && !m_lpMaxSysBtnImgH->IsNull())
					m_lpMaxSysBtnImgH->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
			else
			{
				if (m_lpMaxSysBtnImgN != NULL && !m_lpMaxSysBtnImgN->IsNull())
					m_lpMaxSysBtnImgN->Draw(MemDC.m_hDC, m_rcMaxBtn);
			}
		}
	}

	if (m_bHasCloseBtn)	// 绘制关闭按钮
	{
		if (m_bCloseBtnPress)
		{
			if (m_lpCloseSysBtnImgD != NULL && !m_lpCloseSysBtnImgD->IsNull())
				m_lpCloseSysBtnImgD->Draw(MemDC.m_hDC, m_rcCloseBtn);
		}
		else if(m_bCloseBtnHover)
		{
			if (m_lpCloseSysBtnImgH != NULL && !m_lpCloseSysBtnImgH->IsNull())
				m_lpCloseSysBtnImgH->Draw(MemDC.m_hDC, m_rcCloseBtn);
		}
		else
		{
			if (m_lpCloseSysBtnImgN != NULL && !m_lpCloseSysBtnImgN->IsNull())
				m_lpCloseSysBtnImgN->Draw(MemDC.m_hDC, m_rcCloseBtn);
		}
	}

	int nTitleTextLen = m_strTitleText.GetLength();
	if (nTitleTextLen > 0)
	{
		//DrawEclosionText(MemDC.m_hDC, m_strTitleText, _T("微软雅黑"), 12);

 		CRect rcText(m_rcTitleBar);
 		rcText.left += 8;
 
 		NONCLIENTMETRICS nif;						//创建标题栏字体对象
 		nif.cbSize = sizeof(nif);
 		::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &nif, 0);
 		HFONT hFont = ::CreateFontIndirect(&nif.lfSmCaptionFont);
 
 		int nMode = ::SetBkMode(MemDC.m_hDC, TRANSPARENT);
 		HFONT hOldFont = (HFONT)::SelectObject(MemDC.m_hDC, hFont);
 		::DrawText(MemDC.m_hDC, m_strTitleText, nTitleTextLen, 
 			&rcText, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
 		::SelectObject(MemDC.m_hDC, hOldFont);
 		::SetBkMode(MemDC.m_hDC, nMode);
 
 		::DeleteObject(hFont);
	}

	::BitBlt(PaintDC.m_hDC, 0, 0, rcClient.Width(), rcClient.Height(), m_hMemDC, 0, 0, SRCCOPY);
}

void CSkinDialog::OnLButtonDown(UINT nFlags, CPoint point)
{
	int nHitTest = HitTest(point);

	if (nHitTest == HTMINBUTTON || nHitTest == HTMAXBUTTON
		|| nHitTest == HTCLOSE)
	{
		::SetCapture(m_hWnd);

		CRect rcSysBtn(0,0,0,0);
		rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
		rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);
		
		if (nHitTest == HTMINBUTTON)
		{
			m_bMinBtnPress = TRUE;
			InvalidateRect(&rcSysBtn);
		}
		else if (nHitTest == HTMAXBUTTON)
		{
			m_bMaxBtnPress = TRUE;
			InvalidateRect(&rcSysBtn);
		}
		else if (nHitTest == HTCLOSE)
		{
			m_bCloseBtnPress = TRUE;
			InvalidateRect(&rcSysBtn);
		}
	}
	else if (nHitTest == HTCAPTION)
	{
		if (!IsZoomed())
			SendMessage(WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
	}

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnLButtonUp(UINT nFlags, CPoint point)
{
	int nHitTest = HitTest(point);

	HWND hWnd = ::GetCapture();
	if (m_hWnd == hWnd)
		::ReleaseCapture();

	CRect rcSysBtn(0,0,0,0);
	rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
	rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);

	if (nHitTest == HTMINBUTTON && m_bMinBtnPress)
	{
		m_bMinBtnPress = FALSE;
		InvalidateRect(&rcSysBtn);
		SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
	}
	else if (nHitTest == HTMAXBUTTON && m_bMaxBtnPress)
	{
		m_bMaxBtnPress = FALSE;
		InvalidateRect(&rcSysBtn);

		if (IsZoomed())
			SendMessage(WM_SYSCOMMAND, SC_RESTORE, 0);
		else
			SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE, 0);
	}
	else if (nHitTest == HTCLOSE && m_bCloseBtnPress)
	{
		m_bCloseBtnPress = FALSE;
		InvalidateRect(&rcSysBtn);

		SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
	}
	else
	{
		if (m_bMinBtnPress || m_bMaxBtnPress || m_bCloseBtnPress)
		{
			m_bMinBtnPress = FALSE;
			m_bMaxBtnPress = FALSE;
			m_bCloseBtnPress = FALSE;
			InvalidateRect(&rcSysBtn);
		}
	}

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnMouseMove(UINT nFlags, CPoint point)
{
	CRect rcSysBtn(0,0,0,0);
	rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
	rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);

	int nHitTest = HitTest(point);
	if (nHitTest == HTMINBUTTON || nHitTest == HTMAXBUTTON
		|| nHitTest == HTCLOSE)
	{
		if (!(m_bMinBtnPress || m_bMaxBtnPress
			|| m_bCloseBtnPress))
		{
			if (!m_bMouseTracking)
			{
				StartTrackMouseLeave();
				m_bMouseTracking = TRUE;
			}

			if (nHitTest == HTMINBUTTON)
			{
				if (!m_bMinBtnHover)
				{
					m_bMinBtnHover = TRUE;
					m_bMaxBtnHover = FALSE;
					m_bCloseBtnHover = FALSE;
					InvalidateRect(&rcSysBtn);

					UpdateTooltip(FALSE, _T("最小化"));
				}
			}
			else if (nHitTest == HTMAXBUTTON)
			{
				if (!m_bMaxBtnHover)
				{
					m_bMinBtnHover = FALSE;
					m_bMaxBtnHover = TRUE;
					m_bCloseBtnHover = FALSE;
					InvalidateRect(&rcSysBtn);

					if(IsZoomed())
						UpdateTooltip(FALSE, _T("还原"));
					else
						UpdateTooltip(FALSE, _T("最大化"));
				}
			}
			else if (nHitTest == HTCLOSE)
			{
				if (!m_bCloseBtnHover)
				{
					m_bMinBtnHover = FALSE;
					m_bMaxBtnHover = FALSE;
					m_bCloseBtnHover = TRUE;
					InvalidateRect(&rcSysBtn);

					UpdateTooltip(FALSE, _T("关闭"));
				}
			}
		}
	}
	else
	{
		if (m_bMinBtnHover || m_bMaxBtnHover || m_bCloseBtnHover)
		{
			m_bMinBtnHover = FALSE;
			m_bMaxBtnHover = FALSE;
			m_bCloseBtnHover = FALSE;
			InvalidateRect(&rcSysBtn);
			UpdateTooltip(TRUE, NULL);
		}
	}

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnMouseLeave()
{
	CRect rcSysBtn(0,0,0,0);
	rcSysBtn.UnionRect(&m_rcMinBtn, &m_rcMaxBtn);
	rcSysBtn.UnionRect(&rcSysBtn, &m_rcCloseBtn);

	if (m_bMinBtnHover || m_bMaxBtnHover || m_bCloseBtnHover)
	{
		m_bMinBtnHover = FALSE;
		m_bMaxBtnHover = FALSE;
		m_bCloseBtnHover = FALSE;
		InvalidateRect(&rcSysBtn);
	}

	m_bMouseTracking = FALSE;

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (m_bHasMaxBtn)
	{
		int nHitTest = HitTest(point);
		if (nHitTest == HTCAPTION)
		{
			if (IsZoomed())
				SendMessage(WM_SYSCOMMAND, SC_RESTORE | HTCAPTION, NULL);
			else
				SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE | HTCAPTION, NULL);
			Invalidate();
		}
	}

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	HMONITOR hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);

	MONITORINFO stMonitorInfo = {0};
	stMonitorInfo.cbSize = sizeof(stMonitorInfo);
	::GetMonitorInfo(hMonitor, &stMonitorInfo);
	RECT rcWork = stMonitorInfo.rcWork;
	::OffsetRect(&rcWork, -rcWork.left, -rcWork.top);

	lpMMI->ptMaxPosition.x = rcWork.left;
	lpMMI->ptMaxPosition.y = rcWork.top;
	lpMMI->ptMaxTrackSize.x = rcWork.right;
	lpMMI->ptMaxTrackSize.y = rcWork.bottom;

	SetMsgHandled(FALSE);
}

LRESULT CSkinDialog::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	SetMsgHandled(FALSE);
	return 1;
}

void CSkinDialog::OnSize(UINT nType, CSize size)
{
	SIZE szRoundCorner = {4,4};
	if (!IsIconic()) 
	{
		//if (szRoundCorner.cx != 0 || szRoundCorner.cy != 0)
		//{
			//RECT rcWindow;
			//::GetWindowRect(m_hWnd, &rcWindow);
			//::OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
			//rcWindow.right++;
			//rcWindow.bottom++;
			//HRGN hRgn = ::CreateRoundRectRgn(rcWindow.left, 
			//	rcWindow.top, rcWindow.right, rcWindow.bottom, 
			//	szRoundCorner.cx, szRoundCorner.cy);
			//::SetWindowRgn(m_hWnd, hRgn, TRUE);
			//::DeleteObject(hRgn);
		//}

		CalcTitleBarRect();
		
		Invalidate();
	}

	SetMsgHandled(FALSE);
}

void CSkinDialog::OnDestroy()
{
	if (m_ToolTipCtrl.IsWindow())	//TODO: ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;
	
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpTitleBarBgImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMinSysBtnImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpMaxSysBtnImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRestoreSysBtnImgD);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCloseSysBtnImgD);

	if (m_hMemDC != NULL && m_hMemBmp != NULL)
	{
		::SelectObject(m_hMemDC, m_hOldBmp);
		::DeleteObject(m_hMemBmp);
		::DeleteDC(m_hMemDC);
	}
	m_hMemDC = NULL;
	m_hMemBmp = m_hOldBmp = NULL;

	SetMsgHandled(FALSE);
}

void CSkinDialog::CalcTitleBarRect()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	m_rcTitleBar = CRect(0, 0, rcClient.Width(), 26);

	if (m_bHasCloseBtn)
	{
		m_rcCloseBtn = CRect(rcClient.Width() - 26, 0, rcClient.Width(), 26);

		if (m_bHasMaxBtn)
		{
			m_rcMaxBtn = CRect(rcClient.Width() - 26 - 26, 0, rcClient.Width() - 26, 26);
		}
	}

	if (m_bHasMinBtn)
	{
		if (!m_bHasMaxBtn)
		{
			m_rcMinBtn = CRect(rcClient.Width() - 26 - 26, 0, rcClient.Width() - 26, 26);
		}
		else
		{
			m_rcMinBtn = CRect(rcClient.Width() - 26 - 26 - 26, 0, rcClient.Width() - 26 - 26, 26);
		}
	}
}

int CSkinDialog::HitTest(POINT pt)
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
	
	if(m_hDragRgn!=NULL && ::PtInRegion(m_hDragRgn, pt.x, pt.y))
		return HTCAPTION;
	if(m_hHotRgn!=NULL && !::PtInRegion(m_hHotRgn, pt.x, pt.y))
		return HTCAPTION;
	else if (m_bHasTitleBar && m_rcTitleBar.PtInRect(pt))
		return HTCAPTION;


	return HTCLIENT;
}

BOOL CSkinDialog::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinDialog::UpdateTooltip(BOOL bRemove, PCTSTR lpszTipText)
{
	if (!m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.Create(m_hWnd);

	if(bRemove)
	{
		m_ToolTipCtrl.DelTool(m_hWnd, 1);
		return;
	}

	if (m_ToolTipCtrl.GetToolCount() <= 0)
	{
		m_ToolTipCtrl.Activate(TRUE);
		//TODO: 貌似m_rcTitleBar根本不起作用，无论设置m_rcTitleBar为多大，tooltip均会显示在下方
		m_ToolTipCtrl.AddTool(m_hWnd, lpszTipText, &m_rcTitleBar, 1);
	}
	else
	{
		m_ToolTipCtrl.Activate(TRUE);
		//m_ToolTipCtrl.SetToolRect(
		m_ToolTipCtrl.UpdateTipText(lpszTipText, m_hWnd, 1);
	}
}

//void CSkinDialog::MoveWindow2(int X,int Y,int nWidth,int nHeight,BOOL bRedraw)
//{
//	__super::MoveWindow(X,Y,nWidth,nHeight,bRedraw);
//
//	//AtlTrace(__T("X:%d,Y:%d,nWidth:%d,nHeight:%d.\n"), X, Y, nWidth, nHeight);
//	//if(m_hWndRgn)
//	//	::DeleteObject(m_hWndRgn);
//	//if(m_hWndRgn=::CreateRectRgn(0,0,nWidth,nHeight))
//	//	::SetWindowRgn(m_hWnd,m_hWndRgn,TRUE);
//}
