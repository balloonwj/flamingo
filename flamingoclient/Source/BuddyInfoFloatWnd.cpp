#include "stdafx.h"
#include "BuddyInfoFloatWnd.h"
#include "GDIFactory.h"

CBuddyInfoFloatWnd::CBuddyInfoFloatWnd(void)
{
	m_lpBgImg = NULL;
    m_lpHeadImg = NULL;
	m_bTransparent = FALSE;
    m_bMouseTracking = FALSE;
}

CBuddyInfoFloatWnd::~CBuddyInfoFloatWnd(void)
{
    m_bMouseTracking = FALSE;
}

BOOL CBuddyInfoFloatWnd::SetBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
    return (m_lpBgImg != NULL) ? TRUE : FALSE;
}

BOOL CBuddyInfoFloatWnd::SetHeadImg(LPCTSTR lpszFileName)
{
    CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImg);
    m_lpHeadImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
    if (m_lpHeadImg != NULL)
    {
        Invalidate();
        return TRUE;
    }

    return FALSE;
}

void CBuddyInfoFloatWnd::SetDataText(LPCTSTR pszNickName, LPCTSTR pszSignature, LPCTSTR pszMail, LPCTSTR pszAddress)
{
    m_strNickName.Empty();
    m_strSignature.Empty();
    m_strMail.Empty();
    m_strAddress.Empty();
    
    if (pszNickName != NULL && pszNickName[0] != NULL)
        m_strNickName = pszNickName;

    if (pszSignature != NULL && pszSignature[0] != NULL)
        m_strSignature = pszSignature;

    if (pszMail != NULL && pszMail[0] != NULL)
        m_strMail = pszMail;

    if (pszAddress != NULL && pszAddress[0] != NULL)
        m_strAddress = pszAddress;

    //TODO: 再好是标记一下，有没有改变的文字再决定是否刷新
    Invalidate();
}

int CBuddyInfoFloatWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.Create(m_hWnd, 1, &rc, 1, FALSE, FALSE);

	m_VScrollBar.SetBgNormalPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgHotPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgPushedPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgDisabledPic(_T("ScrollBar\\scrollbar_bkg.png"));

	m_VScrollBar.SetShowLeftUpBtn(TRUE);
	m_VScrollBar.SetLeftUpBtnNormalPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));
	m_VScrollBar.SetLeftUpBtnHotPic(_T("ScrollBar\\scrollbar_arrowup_highlight.png"));
	m_VScrollBar.SetLeftUpBtnPushedPic(_T("ScrollBar\\scrollbar_arrowup_down.png"));
	m_VScrollBar.SetLeftUpBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));

	m_VScrollBar.SetShowRightDownBtn(TRUE);
	m_VScrollBar.SetRightDownBtnNormalPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));
	m_VScrollBar.SetRightDownBtnHotPic(_T("ScrollBar\\scrollbar_arrowdown_highlight.png"));
	m_VScrollBar.SetRightDownBtnPushedPic(_T("ScrollBar\\scrollbar_arrowdown_down.png"));
	m_VScrollBar.SetRightDownBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));

	m_VScrollBar.SetThumbNormalPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbHotPic(_T("ScrollBar\\scrollbar_bar_highlight.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbPushedPic(_T("ScrollBar\\scrollbar_bar_down.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbDisabledPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));
	
	return 0;
}

BOOL CBuddyInfoFloatWnd::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
	//return FALSE;
}

void CBuddyInfoFloatWnd::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);


    MemDC.FillSolidRect(&rcClient, RGB(168, 229, 226));

    if (m_lpHeadImg != NULL && !m_lpHeadImg->IsNull())
    {
        CRect rtHeadImg(0, 0, 64, 64);
        m_lpHeadImg->Draw2(MemDC.m_hDC, rtHeadImg);
    }

    HFONT hFontNickName = CGDIFactory::GetFont(20);
    //HFONT hFontAccountName = CGDIFactory::GetFont(19);
    HFONT hFontAddress = CGDIFactory::GetFont(18);


    HFONT hOldFont = (HFONT)::SelectObject(MemDC.m_hDC, hFontNickName);

    int nOldMode = SetBkMode(MemDC.m_hDC, TRANSPARENT);

    //::SetTextColor(MemDC.m_hDC, RGB(182, 182, 182));

    //m_strNickName = _T("癞头僧");
    if (!m_strNickName.IsEmpty())
    {
        RECT rtNickName = {80, 5, 150, 30};
        ::DrawText(MemDC.m_hDC, m_strNickName, m_strNickName.GetLength(), &rtNickName, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    //m_strSignature = _T("那一些是非题，总让人伤透脑筋。");
    if (!m_strSignature.IsEmpty())
    {
        RECT rtSignature = { 80, 30, 300, 55 };
        ::DrawText(MemDC.m_hDC, m_strSignature, m_strSignature.GetLength(), &rtSignature, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    //m_strMail = _T("balloonwj@qq.com");
    if (!m_strMail.IsEmpty())
    {
        RECT rtMail = { 80, 55, 300, 80 };
        ::DrawText(MemDC.m_hDC, m_strMail, m_strMail.GetLength(), &rtMail, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }


    ::SelectObject(MemDC.m_hDC, hFontAddress);

    //m_strAddress = _T("上海市浦东新区东方路振华广场7890号9楼201");
    if (!m_strAddress.IsEmpty())
    {
        RECT rtAddress = { 5, 130, 300, 150 };
        ::DrawText(MemDC.m_hDC, m_strAddress, m_strAddress.GetLength(), &rtAddress, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }


    ::SelectObject(MemDC.m_hDC, hOldFont);
    ::SetBkMode(MemDC.m_hDC, nOldMode);
}

void CBuddyInfoFloatWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	Invalidate();

	NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), NM_DBLCLK};
	::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
}

void CBuddyInfoFloatWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_VScrollBar.OnLButtonDown(nFlags, point);
}

void CBuddyInfoFloatWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);
}

void CBuddyInfoFloatWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	
	Invalidate();

	//BLNMHDREx stNmhdr;
	//stNmhdr.hdr.hwndFrom = m_hWnd;
	//stNmhdr.hdr.idFrom = GetDlgCtrlID();
	//stNmhdr.hdr.code = NM_RCLICK;
	//
	//if(nTeamIndex!=-1 && nIndex!=-1)
	//	stNmhdr.nPostionFlag = POSITION_ON_ITEM;
	//else if(nTeamIndex!=-1 && nIndex==-1)
	//	stNmhdr.nPostionFlag = POSITION_ON_TEAM;
	//else if(nTeamIndex==-1 && nIndex==-1)
	//	stNmhdr.nPostionFlag = POSITION_ON_BLANK;

	//::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
}

void CBuddyInfoFloatWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);
	
    if (!m_bMouseTracking)
    {
        StartTrackMouseLeave();
        m_bMouseTracking = TRUE;
    }
}

void CBuddyInfoFloatWnd::OnMouseLeave()
{
    ShowWindow(SW_HIDE);

    m_bMouseTracking = FALSE;

    SetMsgHandled(FALSE);
}

void CBuddyInfoFloatWnd::OnTimer(UINT_PTR nIDEvent)
{
	m_VScrollBar.OnTimer(nIDEvent);
}

void CBuddyInfoFloatWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
}

void CBuddyInfoFloatWnd::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.SetRect(&rc);
	Invalidate();
}

UINT CBuddyInfoFloatWnd::OnGetDlgCode(LPMSG lpMsg)
{
	return DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
}

LRESULT CBuddyInfoFloatWnd::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SetMsgHandled(FALSE);
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	return 1;
}

void CBuddyInfoFloatWnd::OnDestroy()
{
	SetMsgHandled(FALSE);


	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	m_VScrollBar.Destroy();

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
    CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImg);
}

void CBuddyInfoFloatWnd::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	//::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

BOOL CBuddyInfoFloatWnd::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CBuddyInfoFloatWnd::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

void CBuddyInfoFloatWnd::HitTest(POINT pt, int& nTeamIndex, int& nIndex)
{
    //RECT rtBuddyInfoFloatWnd;
    //::GetWindowRect(m_hWnd, &rtBuddyInfoFloatWnd);

    //POINT ptCursor;
    //::GetCursorPos(&ptCursor);
    ////算上左右边距的rect
    //RECT rtBuddyInfoFloatWndPlusGap;
    //rtBuddyInfoFloatWndPlusGap.top = rtBuddyInfoFloatWnd.top;
    //rtBuddyInfoFloatWndPlusGap.bottom = rtBuddyInfoFloatWnd.bottom;
    //rtBuddyInfoFloatWndPlusGap.left = rtBuddyInfoFloatWnd.left;
    //rtBuddyInfoFloatWndPlusGap.right = rtBuddyInfoFloatWnd.right + 15;
    //if (!::PtInRect(&rtBuddyInfoFloatWndPlusGap, ptCursor))
    //    ShowWindow(SW_HIDE);
}