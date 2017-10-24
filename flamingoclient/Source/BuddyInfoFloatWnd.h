/** 
 * 用户信息浮动窗口，BuddyInfoFloatWnd.h
 * zhangyl 2017.09.30
 */
#pragma once

#include "SkinLib/SkinLib.h"

class CBuddyInfoFloatWnd : public CWindowImpl<CBuddyInfoFloatWnd, CWindow>
{
public:
    CBuddyInfoFloatWnd(void);
    ~CBuddyInfoFloatWnd(void);

    DECLARE_WND_CLASS(_T("__FlamingoBuddyInfoFloatWnd__"))

    BEGIN_MSG_MAP_EX(CBuddyInfoFloatWnd)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_RBUTTONUP(OnRButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_TIMER(OnTimer)
		//MSG_WM_VSCROLL(OnVScroll)
		//MSG_WM_MOUSEWHEEL(OnMouseWheel)
		MSG_WM_KEYDOWN(OnKeyDown)
		MSG_WM_SIZE(OnSize)
		MSG_WM_GETDLGCODE(OnGetDlgCode)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	BOOL SetBgPic(LPCTSTR lpszFileName);
    BOOL SetHeadImg(LPCTSTR lpszFileName);
	
    void SetDataText(LPCTSTR pszNickName, LPCTSTR pszSignature, LPCTSTR pszMail, LPCTSTR pszAddress);

	void SetTransparent(BOOL bTransparent, HDC hBgDC);

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnRButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnTimer(UINT_PTR nIDEvent);
	void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
	BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnSize(UINT nType, CSize size);
	UINT OnGetDlgCode(LPMSG lpMsg);
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();

	void DrawParentWndBg(HDC hDC);

	BOOL StartTrackMouseLeave();
	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);
	void HitTest(POINT pt, int& nTeamIndex, int& nIndex);


private:
	CToolTipCtrl			m_ToolTipCtrl;
	CSkinScrollBar			m_VScrollBar;

	CImageEx*				m_lpBgImg;
    CImageEx*               m_lpHeadImg;            //头像
	
	BOOL					m_bTransparent;

    BOOL                    m_bMouseTracking;


    CString                 m_strNickName;
    CString                 m_strSignature;
    CString                 m_strMail;
    CString                 m_strAddress;
};
