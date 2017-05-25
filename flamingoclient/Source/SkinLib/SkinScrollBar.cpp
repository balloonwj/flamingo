#include "stdafx.h"
#include "SkinScrollBar.h"

CSkinScrollBar::CSkinScrollBar(void)
{
	m_hOwnerWnd = NULL;
	m_nCtrlId = 0;
	memset(&m_rcScrollBar, 0, sizeof(m_rcScrollBar));
	m_bHorizontal = TRUE;
	m_bVisible = TRUE;
	m_bEnabled = TRUE;

	m_nRange = 0;
	m_nScrollPos = 0;
	m_nLineSize = 8;

	memset(&m_ptLastMouse, 0, sizeof(m_ptLastMouse));
	m_nLastScrollPos = 0;
	m_nLastScrollOffset = 0;
	m_nScrollRepeatDelay = 0;
	m_dwUseTimerId = m_dwTimerId = 0;
	m_bMouseHover = FALSE;
	m_bCaptured = FALSE;
	m_bMouseTracking = FALSE;

	m_lpBgImgN = NULL;
	m_lpBgImgH = NULL;
	m_lpBgImgP = NULL;
	m_lpBgImgD = NULL;

	m_bShowLeftUpBtn = TRUE;
	memset(&m_rcLeftUpBtn, 0, sizeof(m_rcLeftUpBtn));
	m_dwLeftUpBtnState = 0;
	m_lpLeftUpBtnImgN = NULL;
	m_lpLeftUpBtnImgH = NULL;
	m_lpLeftUpBtnImgP = NULL;
	m_lpLeftUpBtnImgD = NULL;

	m_bShowRightDownBtn = TRUE;
	memset(&m_rcRightDownBtn, 0, sizeof(m_rcRightDownBtn));
	m_dwRightDownBtnState = 0;
	m_lpRightDownBtnImgN = NULL;
	m_lpRightDownBtnImgH = NULL;
	m_lpRightDownBtnImgP = NULL;
	m_lpRightDownBtnImgD = NULL;

	memset(&m_rcThumb, 0, sizeof(m_rcThumb));
	m_dwThumbState = 0;
	m_lpThumbImgN = NULL;
	m_lpThumbImgH = NULL;
	m_lpThumbImgP = NULL;
	m_lpThumbImgD = NULL;

	m_lpRailImgN = NULL;
	m_lpRailImgH = NULL;
	m_lpRailImgP = NULL;
	m_lpRailImgD = NULL;
}

CSkinScrollBar::~CSkinScrollBar(void)
{
}

BOOL CSkinScrollBar::SetBgNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	m_lpBgImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetBgHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	m_lpBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetBgPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgP);
	m_lpBgImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImgP != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetBgDisabledPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);
	m_lpBgImgD = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImgD != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::GetShowLeftUpBtn()
{
	return m_bShowLeftUpBtn;
}

void CSkinScrollBar::SetShowLeftUpBtn(BOOL bShow)
{
	m_bShowLeftUpBtn = bShow;
	CalcScrollBarData();
}

BOOL CSkinScrollBar::SetLeftUpBtnNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgN);
	m_lpLeftUpBtnImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpLeftUpBtnImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetLeftUpBtnHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgH);
	m_lpLeftUpBtnImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpLeftUpBtnImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetLeftUpBtnPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgP);
	m_lpLeftUpBtnImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpLeftUpBtnImgP != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetLeftUpBtnDisabledPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgD);
	m_lpLeftUpBtnImgD = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpLeftUpBtnImgD != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::GetShowRightDownBtn()
{
	return m_bShowRightDownBtn;
}

void CSkinScrollBar::SetShowRightDownBtn(BOOL bShow)
{
	m_bShowRightDownBtn = bShow;
	CalcScrollBarData();
}

BOOL CSkinScrollBar::SetRightDownBtnNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgN);
	m_lpRightDownBtnImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRightDownBtnImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRightDownBtnHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgH);
	m_lpRightDownBtnImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRightDownBtnImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRightDownBtnPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgP);
	m_lpRightDownBtnImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRightDownBtnImgP != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRightDownBtnDisabledPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgD);
	m_lpRightDownBtnImgD = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRightDownBtnImgD != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetThumbNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgN);
	m_lpThumbImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpThumbImgN != NULL && lpNinePart != NULL)
		m_lpThumbImgN->SetNinePart(lpNinePart);
	return (m_lpThumbImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetThumbHotPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgH);
	m_lpThumbImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpThumbImgH != NULL && lpNinePart != NULL)
		m_lpThumbImgH->SetNinePart(lpNinePart);
	return (m_lpThumbImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetThumbPushedPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgP);
	m_lpThumbImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpThumbImgP != NULL && lpNinePart != NULL)
		m_lpThumbImgP->SetNinePart(lpNinePart);
	return (m_lpThumbImgP != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetThumbDisabledPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgD);
	m_lpThumbImgD = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpThumbImgD != NULL && lpNinePart != NULL)
		m_lpThumbImgD->SetNinePart(lpNinePart);
	return (m_lpThumbImgD != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRailNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgN);
	m_lpRailImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRailImgN != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRailHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgH);
	m_lpRailImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRailImgH != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRailPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgP);
	m_lpRailImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRailImgP != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::SetRailDisabledPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgD);
	m_lpRailImgD = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpRailImgD != NULL) ? TRUE : FALSE;
}

BOOL CSkinScrollBar::Create(HWND hOwnerWnd, UINT nCtrlId, RECT* lpRect, 
							DWORD dwUseTimerId, BOOL bHorizontal/* = TRUE*/, BOOL bVisible/* = TRUE*/)
{
	m_hOwnerWnd = hOwnerWnd;
	m_nCtrlId = nCtrlId;
	if (lpRect != NULL)
		m_rcScrollBar =*lpRect;
	m_dwUseTimerId = dwUseTimerId;
	m_bHorizontal = bHorizontal;
	m_bVisible = bVisible;
	m_bEnabled = TRUE;
	return TRUE;
}

BOOL CSkinScrollBar::Destroy()
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgD);

	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpLeftUpBtnImgD);

	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRightDownBtnImgD);

	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpThumbImgD);

	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpRailImgD);

	m_hOwnerWnd = NULL;
	m_nCtrlId = 0;
	memset(&m_rcScrollBar, 0, sizeof(m_rcScrollBar));
	m_bHorizontal = TRUE;
	m_bVisible = TRUE;
	m_bEnabled = TRUE;

	m_nRange = 0;
	m_nScrollPos = 0;
	m_nLineSize = 8;

	memset(&m_ptLastMouse, 0, sizeof(m_ptLastMouse));
	m_nLastScrollPos = 0;
	m_nLastScrollOffset = 0;
	m_nScrollRepeatDelay = 0;
	m_dwUseTimerId = m_dwTimerId = 0;
	m_bMouseHover = FALSE;
	m_bCaptured = FALSE;
	m_bMouseTracking = FALSE;

	m_bShowLeftUpBtn = TRUE;
	memset(&m_rcLeftUpBtn, 0, sizeof(m_rcLeftUpBtn));
	m_dwLeftUpBtnState = 0;

	m_bShowRightDownBtn = TRUE;
	memset(&m_rcRightDownBtn, 0, sizeof(m_rcRightDownBtn));
	m_dwRightDownBtnState = 0;

	memset(&m_rcThumb, 0, sizeof(m_rcThumb));
	m_dwThumbState = 0;

	return TRUE;
}

void CSkinScrollBar::SetOwnerWnd(HWND hWnd)
{
	m_hOwnerWnd = hWnd;
}

HWND CSkinScrollBar::GetOwnerWnd()
{
	return m_hOwnerWnd;
}

void CSkinScrollBar::SetCtrlId(UINT nCtrlId)
{
	m_nCtrlId = nCtrlId;
}

UINT CSkinScrollBar::GetCtrlId()
{
	return m_nCtrlId;
}

void CSkinScrollBar::SetRect(RECT* lpRect)
{
	if (lpRect != NULL)
		m_rcScrollBar =*lpRect;
	CalcScrollBarData();
}

void CSkinScrollBar::GetRect(RECT* lpRect)
{
	if (lpRect != NULL)
		*lpRect = m_rcScrollBar;
}

BOOL CSkinScrollBar::IsHorizontal()
{
	return m_bHorizontal;
}

void CSkinScrollBar::SetHorizontal(BOOL bHorizontal/* = TRUE*/)
{
	if (m_bHorizontal == bHorizontal)
		return;

	m_bHorizontal = bHorizontal;
	Invalidate();
}

BOOL CSkinScrollBar::IsVisible()
{
	return m_bVisible;
}

void CSkinScrollBar::SetVisible(BOOL bVisible/* = TRUE*/)
{
	if (m_bVisible == bVisible)
		return;

	m_bVisible = bVisible;
	Invalidate();
}

BOOL CSkinScrollBar::IsEnabled()
{
	return m_bEnabled;
}

void CSkinScrollBar::SetEnabled(BOOL bEnable/* = TRUE*/)
{
	if (m_bEnabled == bEnable)
		return;

	m_bEnabled = bEnable;
	if (!m_bEnabled)
	{
		m_dwLeftUpBtnState = 0;
		m_dwRightDownBtnState = 0;
		m_dwThumbState = 0;
	}
	Invalidate();
}

int CSkinScrollBar::GetScrollRange()
{
	return m_nRange;
}

void CSkinScrollBar::SetScrollRange(int nRange)
{
	if (m_nRange == nRange)
		return;

	m_nRange = nRange;

	if (m_nRange < 0)
		m_nRange = 0;

	if (m_nScrollPos > m_nRange)
		m_nScrollPos = m_nRange;

	CalcScrollBarData();
	Invalidate();
}

int CSkinScrollBar::GetScrollPos()
{
	return m_nScrollPos;
}

void CSkinScrollBar::SetScrollPos(int nPos)
{
	if (m_nScrollPos == nPos)
		return;

	m_nScrollPos = nPos;

	if (m_nScrollPos < 0)
		m_nScrollPos = 0;

	if (m_nScrollPos > m_nRange)
		m_nScrollPos = m_nRange;

	CalcScrollBarData();
	Invalidate();
}

int CSkinScrollBar::GetLineSize()
{
	return m_nLineSize;
}

void CSkinScrollBar::SetLineSize(int nSize)
{
	m_nLineSize = nSize;
}

LRESULT CSkinScrollBar::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	BOOL bMsgHandled = FALSE;

	switch (uMsg)
	{
// 	case WM_PAINT:
// 		OnPaint((HDC)wParam);
// 		break;

	case WM_LBUTTONDOWN:
		{
			CPoint point = _WTYPES_NS::CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			if (IsVisible() && IsEnabled() && ::PtInRect(&m_rcScrollBar, point))
			{
				OnLButtonDown((UINT)wParam, point);
				bMsgHandled = TRUE;
			}
		}
		break;

	case WM_LBUTTONUP:
		{
			if (IsVisible() && IsEnabled() && IsCaptured())
			{
				OnLButtonUp((UINT)wParam, _WTYPES_NS::CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
				bMsgHandled = TRUE;
			}
		}
		break;

	case WM_MOUSEMOVE:
		{
			CPoint point = _WTYPES_NS::CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			if (IsVisible() && IsEnabled() && (IsCaptured() || IsMouseTracking() || ::PtInRect(&m_rcScrollBar, point)))
			{
				OnMouseMove((UINT)wParam, point);
				bMsgHandled = TRUE;
			}
		}
		break;

	case WM_MOUSELEAVE:
		{
			if (IsMouseTracking())
			{
				OnMouseLeave();
				bMsgHandled = TRUE;
			}
		}
		break;

	case WM_TIMER:
		{
			UINT_PTR nIDEvent = (UINT_PTR)wParam;
			if (nIDEvent == m_dwTimerId)
			{
				OnTimer(nIDEvent);
				bMsgHandled = TRUE;
			}
		}
		break;
	}

	return bMsgHandled;
}

void CSkinScrollBar::OnPaint(HDC hDC)
{
	if (!IsVisible())
		return;

	DrawBg(hDC);
	DrawLeftUpBtn(hDC);
	DrawRightDownBtn(hDC);
	DrawThumb(hDC);
	DrawRail(hDC);
}

void CSkinScrollBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (!IsVisible() || !IsEnabled() || !::PtInRect(&m_rcScrollBar, point))
		return;

	::SetFocus(m_hOwnerWnd);
	SetCapture();

	m_nLastScrollOffset = 0;
	m_nScrollRepeatDelay = 0;
	m_dwTimerId = ::SetTimer(m_hOwnerWnd, m_dwUseTimerId, 50, NULL);

	if (::PtInRect(&m_rcLeftUpBtn, point))	// 左/上箭头
	{
		m_dwLeftUpBtnState |= UISTATE_PUSHED;
		if (!m_bHorizontal)		// 垂直
		{
			if (::IsWindow(m_hOwnerWnd))
				::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_LINEUP, m_nCtrlId);
			else
				SetScrollPos(m_nScrollPos - m_nLineSize);
		}
		else	// 水平
		{
			if (::IsWindow(m_hOwnerWnd))
				::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_LINELEFT, m_nCtrlId);
			else
				SetScrollPos(m_nScrollPos - m_nLineSize);
		}
	}
	else if (::PtInRect(&m_rcRightDownBtn, point))	// 右/下箭头
	{
		m_dwRightDownBtnState |= UISTATE_PUSHED;
		if (!m_bHorizontal)
		{
			if (::IsWindow(m_hOwnerWnd))
				::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_LINEDOWN, m_nCtrlId);
			else
				SetScrollPos(m_nScrollPos + m_nLineSize);
		}
		else
		{
			if (::IsWindow(m_hOwnerWnd))
				::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_LINERIGHT, m_nCtrlId);
			else
				SetScrollPos(m_nScrollPos + m_nLineSize);
		}
	}
	else if (::PtInRect(&m_rcThumb, point))	// 滑块
	{
		m_dwThumbState |= (UISTATE_CAPTURED | UISTATE_PUSHED);
		m_ptLastMouse = point;
		m_nLastScrollPos = m_nScrollPos;
	}
	else
	{
		if (!m_bHorizontal)	// 垂直
		{
			if (point.y < m_rcThumb.top)	// 上箭头和滑块之间区域
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_PAGEUP, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos + m_rcScrollBar.top - m_rcScrollBar.bottom);
			}
			else if (point.y > m_rcThumb.bottom)	// 下箭头和滑块之间区域
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_PAGEDOWN, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos - m_rcScrollBar.top + m_rcScrollBar.bottom);
			}
		}
		else	// 水平
		{
			if (point.x < m_rcThumb.left)	// 左箭头和滑块之间区域
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_PAGELEFT, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos + m_rcScrollBar.left - m_rcScrollBar.right);
			}
			else if (point.x > m_rcThumb.right)		// 右箭头和滑块之间区域
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_PAGERIGHT, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos - m_rcScrollBar.left + m_rcScrollBar.right);
			}
		}
	}
}

void CSkinScrollBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (!IsVisible() || !IsEnabled()/* || !::PtInRect(&m_rcScrollBar, point)*/)
		return;

	ReleaseCapture();

	m_nScrollRepeatDelay = 0;
	m_nLastScrollOffset = 0;
	::KillTimer(m_hOwnerWnd, m_dwTimerId);

	if ((m_dwThumbState & UISTATE_CAPTURED) != 0)
	{
		m_dwThumbState &= ~(UISTATE_CAPTURED | UISTATE_PUSHED);
		Invalidate();
	}
	else if((m_dwLeftUpBtnState & UISTATE_PUSHED) != 0)
	{
		m_dwLeftUpBtnState &= ~UISTATE_PUSHED;
		Invalidate();
	}
	else if((m_dwRightDownBtnState & UISTATE_PUSHED) != 0)
	{
		m_dwRightDownBtnState &= ~UISTATE_PUSHED;
		Invalidate();
	}
}

void CSkinScrollBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!IsVisible() || !IsEnabled())
		return;

// 	if (::PtInRect(&m_rcScrollBar, point))
// 	{
// 		if (!m_bMouseHover)	// MouseEnter
// 			m_bMouseHover = TRUE;
// 	}
// 	else
// 	{
// 		if (m_bMouseHover)	// MouseLeave
// 			m_bMouseHover = FALSE;
// 	}

	if ((m_dwThumbState & UISTATE_CAPTURED) != 0)
	{
		if (!m_bHorizontal)
		{
			m_nLastScrollOffset = (point.y - m_ptLastMouse.y)* m_nRange / \
				(m_rcScrollBar.Height() - m_rcThumb.bottom + m_rcThumb.top - 2* m_rcScrollBar.Width());
		}
		else
		{
			m_nLastScrollOffset = (point.x - m_ptLastMouse.x)* m_nRange / \
				(m_rcScrollBar.Width() - m_rcThumb.right + m_rcThumb.left - 2* m_rcScrollBar.Height());
		}
	}

	if (::PtInRect(&m_rcScrollBar, point))
	{
		if (!m_bMouseHover)	// MouseEnter
		{
			m_bMouseHover = TRUE;
			m_bMouseTracking = TRUE;
			StartTrackMouseLeave();
		}

// 		if ((m_dwThumbState & UISTATE_CAPTURED) != 0)
// 		{
// 			if (!m_bHorizontal)
// 			{
// 				m_nLastScrollOffset = (point.y - m_ptLastMouse.y)* m_nRange / \
// 					(m_rcScrollBar.bottom - m_rcScrollBar.top - m_rcThumb.bottom + m_rcThumb.top - 2* m_rcScrollBar.Width());
// 				wchar_t c[100];
// 				wsprintf(c, _T("%d\n"), m_nLastScrollOffset);
// 				::OutputDebugString(c);
// 			}
// 			else
// 			{
// 				m_nLastScrollOffset = (point.x - m_ptLastMouse.x)* m_nRange / \
// 					(m_rcScrollBar.right - m_rcScrollBar.left - m_rcThumb.right + m_rcThumb.left - 2* m_rcScrollBar.Height());
// 			}
// 		}
		/*else*/if ((m_dwLeftUpBtnState & UISTATE_PUSHED) != 0)
		{

		}
		else if ((m_dwRightDownBtnState & UISTATE_PUSHED) != 0)
		{

		}
		else
		{
			if (::PtInRect(&m_rcLeftUpBtn, point))
			{
				if ((m_dwLeftUpBtnState & UISTATE_HOT) == 0)
				{
					m_dwLeftUpBtnState |= UISTATE_HOT;
					m_dwRightDownBtnState &= ~UISTATE_HOT;
					m_dwThumbState &= ~UISTATE_HOT;
					Invalidate();
				}
			}
			else if (::PtInRect(&m_rcRightDownBtn, point))
			{
				if ((m_dwRightDownBtnState & UISTATE_HOT) == 0)
				{
					m_dwRightDownBtnState |= UISTATE_HOT;
					m_dwLeftUpBtnState &= ~UISTATE_HOT;
					m_dwThumbState &= ~UISTATE_HOT;
					Invalidate();
				}
			}
			else if (::PtInRect(&m_rcThumb, point))
			{
				if ((m_dwThumbState & UISTATE_HOT) == 0)
				{
					m_dwThumbState |= UISTATE_HOT;
					m_dwLeftUpBtnState &= ~UISTATE_HOT;
					m_dwRightDownBtnState &= ~UISTATE_HOT;
					Invalidate();
				}
			}
			else
			{
				if ((m_dwLeftUpBtnState & UISTATE_HOT) || 
					(m_dwRightDownBtnState & UISTATE_HOT) ||
					(m_dwThumbState & UISTATE_HOT))
				{
					m_dwLeftUpBtnState &= ~UISTATE_HOT;
					m_dwRightDownBtnState &= ~UISTATE_HOT;
					m_dwThumbState &= ~UISTATE_HOT;
					Invalidate();
				}
			}
		}
	}
	else
	{
		if (m_bMouseHover)	// MouseLeave
		{
			m_bMouseHover = FALSE;
			m_bMouseTracking = FALSE;

			if ((m_dwLeftUpBtnState & UISTATE_HOT) || 
				(m_dwRightDownBtnState & UISTATE_HOT) ||
				(m_dwThumbState & UISTATE_HOT))
			{
				m_dwLeftUpBtnState &= ~UISTATE_HOT;
				m_dwRightDownBtnState &= ~UISTATE_HOT;
				m_dwThumbState &= ~UISTATE_HOT;
				Invalidate();
			}
		}
	}
}

void CSkinScrollBar::OnMouseLeave()
{
	if (!IsVisible() || !IsEnabled() || !m_bMouseHover)
		return;

	m_bMouseHover = FALSE;
	m_bMouseTracking = FALSE;

	if ((m_dwLeftUpBtnState & UISTATE_HOT) || 
		(m_dwRightDownBtnState & UISTATE_HOT) ||
		(m_dwThumbState & UISTATE_HOT))
	{
		m_dwLeftUpBtnState &= ~UISTATE_HOT;
		m_dwRightDownBtnState &= ~UISTATE_HOT;
		m_dwThumbState &= ~UISTATE_HOT;
		Invalidate();
	}
}

void CSkinScrollBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_dwTimerId)
	{
		++m_nScrollRepeatDelay;
		if ((m_dwThumbState & UISTATE_CAPTURED) != 0)	// 拖动滑块
		{
			if (!m_bHorizontal)	// 垂直
			{
				SetScrollPos(m_nLastScrollPos + m_nLastScrollOffset);
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_THUMBTRACK, m_nCtrlId);	// ？
			}
			else
			{
				SetScrollPos(m_nLastScrollPos + m_nLastScrollOffset);
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_THUMBTRACK, m_nCtrlId);	// ？
			}

			Invalidate();
		}
		else if((m_dwLeftUpBtnState & UISTATE_PUSHED) != 0)	// 长按左/上箭头
		{
			if (m_nScrollRepeatDelay <= 5)
				return;

			if (!m_bHorizontal)		// 垂直
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_LINEUP, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos - m_nLineSize);
			}
			else	// 水平
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_LINELEFT, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos - m_nLineSize);
			}
		}
		else if((m_dwRightDownBtnState & UISTATE_PUSHED) != 0)	// 长按右/下箭头
		{
			if (m_nScrollRepeatDelay <= 5)
				return;

			if (!m_bHorizontal)
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_LINEDOWN, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos + m_nLineSize);
			}
			else
			{
				if (::IsWindow(m_hOwnerWnd))
					::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_LINERIGHT, m_nCtrlId);
				else
					SetScrollPos(m_nScrollPos + m_nLineSize);
			}
		}
		else
		{
			if (m_nScrollRepeatDelay <= 5)
				return;

			POINT pt = {0};
			::GetCursorPos(&pt);
			::ScreenToClient(m_hOwnerWnd, &pt);

			if (!m_bHorizontal)	// 垂直
			{
				if (pt.y < m_rcThumb.top)	// 上箭头和滑块之间区域
				{
					if (::IsWindow(m_hOwnerWnd))
						::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_PAGEUP, m_nCtrlId);
					else
						SetScrollPos(m_nScrollPos + m_rcScrollBar.top - m_rcScrollBar.bottom);
				}
				else if (pt.y > m_rcThumb.bottom)	// 下箭头和滑块之间区域
				{
					if (::IsWindow(m_hOwnerWnd))
						::SendMessage(m_hOwnerWnd, WM_VSCROLL, SB_PAGEDOWN, m_nCtrlId);
					else
						SetScrollPos(m_nScrollPos - m_rcScrollBar.top + m_rcScrollBar.bottom);
				}
			}
			else	// 水平
			{
				if (pt.x < m_rcThumb.left)	// 左箭头和滑块之间区域
				{
					if (::IsWindow(m_hOwnerWnd))
						::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_PAGELEFT, m_nCtrlId);
					else
						SetScrollPos(m_nScrollPos + m_rcScrollBar.left - m_rcScrollBar.right);
				}
				else if (pt.x > m_rcThumb.right)	// 右箭头和滑块之间区域
				{
					if (::IsWindow(m_hOwnerWnd))
						::SendMessage(m_hOwnerWnd, WM_HSCROLL, SB_PAGERIGHT, m_nCtrlId);
					else
						SetScrollPos(m_nScrollPos - m_rcScrollBar.left + m_rcScrollBar.right);
				}
			}
		}
	}
}

void CSkinScrollBar::DrawBg(HDC hDC)
{
	if (!IsEnabled()) 
		m_dwThumbState |= UISTATE_DISABLED;
	else
		m_dwThumbState &= ~UISTATE_DISABLED;

	if ((m_dwThumbState & UISTATE_DISABLED) != 0)
	{
		if (m_lpBgImgD != NULL && !m_lpBgImgD->IsNull())
			m_lpBgImgD->Draw(hDC, m_rcScrollBar);
	}
	else if((m_dwThumbState & UISTATE_PUSHED) != 0)
	{
		if (m_lpBgImgP != NULL && !m_lpBgImgP->IsNull())
			m_lpBgImgP->Draw(hDC, m_rcScrollBar);
	}
	else if((m_dwThumbState & UISTATE_HOT) != 0)
	{
		if (m_lpBgImgH != NULL && !m_lpBgImgH->IsNull())
			m_lpBgImgH->Draw(hDC, m_rcScrollBar);
	}
	else
	{
		if (m_lpBgImgN != NULL && !m_lpBgImgN->IsNull())
			m_lpBgImgN->Draw(hDC, m_rcScrollBar);
	}
}

void CSkinScrollBar::DrawLeftUpBtn(HDC hDC)
{
	if (!m_bShowLeftUpBtn)
		return;

	if (!IsEnabled())
		m_dwLeftUpBtnState |= UISTATE_DISABLED;
	else
		m_dwLeftUpBtnState &= ~ UISTATE_DISABLED;

	if ((m_dwLeftUpBtnState & UISTATE_DISABLED) != 0)
	{
		if (m_lpLeftUpBtnImgD != NULL && !m_lpLeftUpBtnImgD->IsNull())
			m_lpLeftUpBtnImgD->Draw(hDC, m_rcLeftUpBtn);
	}
	else if((m_dwLeftUpBtnState & UISTATE_PUSHED) != 0)
	{
		if (m_lpLeftUpBtnImgP != NULL && !m_lpLeftUpBtnImgP->IsNull())
			m_lpLeftUpBtnImgP->Draw(hDC, m_rcLeftUpBtn);
	}
	else if((m_dwLeftUpBtnState & UISTATE_HOT) != 0)
	{
		if (m_lpLeftUpBtnImgH != NULL && !m_lpLeftUpBtnImgH->IsNull())
			m_lpLeftUpBtnImgH->Draw(hDC, m_rcLeftUpBtn);
	}
	else
	{
		if (m_lpLeftUpBtnImgN != NULL && !m_lpLeftUpBtnImgN->IsNull())
			m_lpLeftUpBtnImgN->Draw(hDC, m_rcLeftUpBtn);
	}
}

void CSkinScrollBar::DrawRightDownBtn(HDC hDC)
{
	if (!m_bShowRightDownBtn)
		return;

	if (!IsEnabled())
		m_dwRightDownBtnState |= UISTATE_DISABLED;
	else
		m_dwRightDownBtnState &= ~ UISTATE_DISABLED;

	if ((m_dwRightDownBtnState & UISTATE_DISABLED) != 0)
	{
		if (m_lpRightDownBtnImgD != NULL && !m_lpRightDownBtnImgD->IsNull())
			m_lpRightDownBtnImgD->Draw(hDC, m_rcRightDownBtn);
	}
	else if ((m_dwRightDownBtnState & UISTATE_PUSHED) != 0)
	{
		if (m_lpRightDownBtnImgP != NULL && !m_lpRightDownBtnImgP->IsNull())
			m_lpRightDownBtnImgP->Draw(hDC, m_rcRightDownBtn);
	}
	else if ((m_dwRightDownBtnState & UISTATE_HOT) != 0)
	{
		if (m_lpRightDownBtnImgH != NULL && !m_lpRightDownBtnImgH->IsNull())
			m_lpRightDownBtnImgH->Draw(hDC, m_rcRightDownBtn);
	}
	else
	{
		if (m_lpRightDownBtnImgN != NULL && !m_lpRightDownBtnImgN->IsNull())
			m_lpRightDownBtnImgN->Draw(hDC, m_rcRightDownBtn);
	}
}

void CSkinScrollBar::DrawThumb(HDC hDC)
{
	if (m_rcThumb.left == 0 && m_rcThumb.top == 0 && m_rcThumb.right == 0 && m_rcThumb.bottom == 0)
		return;

	if (!IsEnabled())
		m_dwThumbState |= UISTATE_DISABLED;
	else
		m_dwThumbState &= ~ UISTATE_DISABLED;

	if ((m_dwThumbState & UISTATE_DISABLED) != 0)
	{
		if (m_lpThumbImgD != NULL && !m_lpThumbImgD->IsNull())
			m_lpThumbImgD->Draw2(hDC, m_rcThumb);
	}
	else if ((m_dwThumbState & UISTATE_PUSHED) != 0 )
	{
		if (m_lpThumbImgP != NULL && !m_lpThumbImgP->IsNull())
			m_lpThumbImgP->Draw2(hDC, m_rcThumb);
	}
	else if ((m_dwThumbState & UISTATE_HOT) != 0)
	{
		if (m_lpThumbImgH != NULL && !m_lpThumbImgH->IsNull())
			m_lpThumbImgH->Draw2(hDC, m_rcThumb);
	}
	else
	{
		if (m_lpThumbImgN != NULL && !m_lpThumbImgN->IsNull())
			m_lpThumbImgN->Draw2(hDC, m_rcThumb);
	}
}

void CSkinScrollBar::DrawRail(HDC hDC)
{
	if (m_rcThumb.left == 0 && m_rcThumb.top == 0 && m_rcThumb.right == 0 && m_rcThumb.bottom == 0)
		return;

	if (!IsEnabled())
		m_dwThumbState |= UISTATE_DISABLED;
	else
		m_dwThumbState &= ~ UISTATE_DISABLED;

	CRect rcRail;
	if (!m_bHorizontal)
	{
		rcRail = CRect(0,0,0,0);
	}
	else
	{
		rcRail = CRect(0,0,0,0);
	}

	if ((m_dwThumbState & UISTATE_DISABLED) != 0)
	{
		if (m_lpRailImgD != NULL && !m_lpRailImgD->IsNull())
			m_lpRailImgD->Draw(hDC, rcRail);
	}
	else if((m_dwThumbState & UISTATE_PUSHED) != 0)
	{
		if (m_lpRailImgP != NULL && !m_lpRailImgP->IsNull())
			m_lpRailImgP->Draw(hDC, rcRail);
	}
	else if((m_dwThumbState & UISTATE_HOT) != 0)
	{
		if (m_lpRailImgH != NULL && !m_lpRailImgH->IsNull())
			m_lpRailImgH->Draw(hDC, rcRail);
	}
	else
	{
		if (m_lpRailImgN != NULL && !m_lpRailImgN->IsNull())
			m_lpRailImgN->Draw(hDC, rcRail);
	}
}

BOOL CSkinScrollBar::Invalidate(BOOL bErase/* = TRUE*/)
{
	if (::IsWindow(m_hOwnerWnd))
		return ::InvalidateRect(m_hOwnerWnd, &m_rcScrollBar, bErase);
	else
		return FALSE;
}

void CSkinScrollBar::CalcScrollBarData()
{
	if (m_bHorizontal)	// 水平
	{
		int cx = m_rcScrollBar.Width();
		int cy = m_rcScrollBar.Height();

		if (m_bShowLeftUpBtn)
			cx -= cy;
		if (m_bShowRightDownBtn)
			cx -= cy;
		if (cx > cy)
		{
			m_rcLeftUpBtn.left = m_rcScrollBar.left;	// 计算左箭头按钮区域
			m_rcLeftUpBtn.top = m_rcScrollBar.top;
			if (m_bShowLeftUpBtn)
			{
				m_rcLeftUpBtn.right = m_rcScrollBar.left + cy;
				m_rcLeftUpBtn.bottom = m_rcScrollBar.top + cy;
			}
			else
			{
				m_rcLeftUpBtn.right = m_rcLeftUpBtn.left;
				m_rcLeftUpBtn.bottom = m_rcLeftUpBtn.top;
			}

			m_rcRightDownBtn.top = m_rcScrollBar.top;	// 计算右箭头按钮区域
			m_rcRightDownBtn.right = m_rcScrollBar.right;
			if (m_bShowRightDownBtn)
			{
				m_rcRightDownBtn.left = m_rcScrollBar.right - cy;
				m_rcRightDownBtn.bottom = m_rcScrollBar.top + cy;
			}
			else
			{
				m_rcRightDownBtn.left = m_rcRightDownBtn.right;
				m_rcRightDownBtn.bottom = m_rcRightDownBtn.top;
			}

			m_rcThumb.top = m_rcScrollBar.top;
			m_rcThumb.bottom = m_rcScrollBar.top + cy;
			if (m_nRange > 0)
			{
				int cxThumb = cx* m_rcScrollBar.Width() / (m_nRange + m_rcScrollBar.Width());
				if (cxThumb < cy)
					cxThumb = cy;

				m_rcThumb.left = m_nScrollPos* (cx - cxThumb) / m_nRange + m_rcLeftUpBtn.right;
				m_rcThumb.right = m_rcThumb.left + cxThumb;
				if (m_rcThumb.right > m_rcRightDownBtn.left)
				{
					m_rcThumb.left = m_rcRightDownBtn.left - cxThumb;
					m_rcThumb.right = m_rcRightDownBtn.left;
				}
			}
			else
			{
				m_rcThumb.left = m_rcLeftUpBtn.right;
				m_rcThumb.right = m_rcRightDownBtn.left;
			}
		}
		else
		{
			int cxButton = m_rcScrollBar.Width() / 2;
			if (cxButton > cy)
				cxButton = cy;
			m_rcLeftUpBtn.left = m_rcScrollBar.left;
			m_rcLeftUpBtn.top = m_rcScrollBar.top;
			if (m_bShowLeftUpBtn)
			{
				m_rcLeftUpBtn.right = m_rcScrollBar.left + cxButton;
				m_rcLeftUpBtn.bottom = m_rcScrollBar.top + cy;
			}
			else
			{
				m_rcLeftUpBtn.right = m_rcLeftUpBtn.left;
				m_rcLeftUpBtn.bottom = m_rcLeftUpBtn.top;
			}

			m_rcRightDownBtn.top = m_rcScrollBar.top;
			m_rcRightDownBtn.right = m_rcScrollBar.right;
			if (m_bShowRightDownBtn)
			{
				m_rcRightDownBtn.left = m_rcScrollBar.right - cxButton;
				m_rcRightDownBtn.bottom = m_rcScrollBar.top + cy;
			}
			else
			{
				m_rcRightDownBtn.left = m_rcRightDownBtn.right;
				m_rcRightDownBtn.bottom = m_rcRightDownBtn.top;
			}

			memset(&m_rcThumb, 0, sizeof(m_rcThumb));
		}
	}
	else	// 垂直
	{
		int cx = m_rcScrollBar.Width();
		int cy = m_rcScrollBar.Height();

		if (m_bShowLeftUpBtn)
			cy -= cx;
		if (m_bShowRightDownBtn)
			cy -= cx;
		if (cy > cx)
		{
			m_rcLeftUpBtn.left = m_rcScrollBar.left;
			m_rcLeftUpBtn.top = m_rcScrollBar.top;
			if (m_bShowLeftUpBtn)
			{
				m_rcLeftUpBtn.right = m_rcScrollBar.left + cx;
				m_rcLeftUpBtn.bottom = m_rcScrollBar.top + cx;
			}
			else
			{
				m_rcLeftUpBtn.right = m_rcLeftUpBtn.left;
				m_rcLeftUpBtn.bottom = m_rcLeftUpBtn.top;
			}

			m_rcRightDownBtn.left = m_rcScrollBar.left;
			m_rcRightDownBtn.bottom = m_rcScrollBar.bottom;
			if (m_bShowRightDownBtn)
			{
				m_rcRightDownBtn.top = m_rcScrollBar.bottom - cx;
				m_rcRightDownBtn.right = m_rcScrollBar.left + cx;
			}
			else
			{
				m_rcRightDownBtn.top = m_rcRightDownBtn.bottom;
				m_rcRightDownBtn.right = m_rcRightDownBtn.left;
			}

			m_rcThumb.left = m_rcScrollBar.left;
			m_rcThumb.right = m_rcScrollBar.left + cx;
			if (m_nRange > 0)
			{
				// 滚动条高度* (显示大小 / 总大小)
				// 滚动条高度* 显示大小 / 总大小
				int cyThumb = cy* m_rcScrollBar.Height() / (m_nRange + m_rcScrollBar.Height());
				if (cyThumb < cx)
					cyThumb = cx;

				m_rcThumb.top = m_nScrollPos* (cy - cyThumb) / m_nRange + m_rcLeftUpBtn.bottom;
				m_rcThumb.bottom = m_rcThumb.top + cyThumb;
				if (m_rcThumb.bottom > m_rcRightDownBtn.top)
				{
					m_rcThumb.top = m_rcRightDownBtn.top - cyThumb;
					m_rcThumb.bottom = m_rcRightDownBtn.top;
				}
			}
			else
			{
				m_rcThumb.top = m_rcLeftUpBtn.bottom;
				m_rcThumb.bottom = m_rcRightDownBtn.top;
			}
		}
		else
		{
			int cyButton = m_rcScrollBar.Height() / 2;
			if (cyButton > cx)
				cyButton = cx;
			m_rcLeftUpBtn.left = m_rcScrollBar.left;
			m_rcLeftUpBtn.top = m_rcScrollBar.top;
			if (m_bShowLeftUpBtn)
			{
				m_rcLeftUpBtn.right = m_rcScrollBar.left + cx;
				m_rcLeftUpBtn.bottom = m_rcScrollBar.top + cyButton;
			}
			else
			{
				m_rcLeftUpBtn.right = m_rcLeftUpBtn.left;
				m_rcLeftUpBtn.bottom = m_rcLeftUpBtn.top;
			}

			m_rcRightDownBtn.left = m_rcScrollBar.left;
			m_rcRightDownBtn.bottom = m_rcScrollBar.bottom;
			if (m_bShowRightDownBtn)
			{
				m_rcRightDownBtn.top = m_rcScrollBar.bottom - cyButton;
				m_rcRightDownBtn.right = m_rcScrollBar.left + cx;
			}
			else
			{
				m_rcRightDownBtn.top = m_rcRightDownBtn.bottom;
				m_rcRightDownBtn.right = m_rcRightDownBtn.left;
			}

			memset(&m_rcThumb, 0, sizeof(m_rcThumb));
		}
	}
}

HWND CSkinScrollBar::SetCapture()
{
	m_bCaptured = TRUE;
	return ::SetCapture(m_hOwnerWnd);
}

BOOL CSkinScrollBar::ReleaseCapture()
{
	m_bCaptured = FALSE;
	return ::ReleaseCapture();
}

BOOL CSkinScrollBar::IsCaptured()
{
	return m_bCaptured;
}

BOOL CSkinScrollBar::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hOwnerWnd;
	return _TrackMouseEvent(&tme);
}

BOOL CSkinScrollBar::IsMouseTracking()
{
	return m_bMouseTracking;
}