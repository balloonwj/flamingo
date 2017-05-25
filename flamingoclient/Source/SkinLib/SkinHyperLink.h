#pragma once

#include "SkinManager.h"


enum SKIN_LINK_TYPE
{
	SKIN_LINK_DEFAULT,		//默认超级链接打开一个网址
	SKIN_LINK_ADDNEW		//添加新好友对话框的提示信息
};

class CSkinHyperLink : public CWindowImpl<CSkinHyperLink, CStatic>
{
public:
	CSkinHyperLink(void);
	~CSkinHyperLink(void);

	BEGIN_MSG_MAP_EX(CSkinHyperLink)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_WM_SETCURSOR(OnSetCursor)
		MSG_WM_SETTEXT(OnSetText)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void SetLinkColor(COLORREF clr);
	void SetHoverLinkColor(COLORREF clr);
	void SetVisitedLinkColor(COLORREF clr);
	void SetLabel(LPCTSTR lpszLabel);
	void SetHyperLink(LPCTSTR lpszLink);
	void SetToolTipText(LPCTSTR lpszText);
	void SetNormalFont(HFONT hFont);
	void SetHoverFont(HFONT hFont);
	void SetVisitedFont(HFONT hFont);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void SetLinkType(SKIN_LINK_TYPE nLinkType);

	BOOL SubclassWindow(HWND hWnd);

protected:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSetFocus(CWindow wndOld);
	void OnKillFocus(CWindow wndFocus);
	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);
	int OnSetText(LPCTSTR lpstrText);
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();

	BOOL StartTrackMouseLeave();
	BOOL Navigate();
	void DrawParentWndBg(HDC hDC);
	void Draw(HDC hDC);

private:
	COLORREF		m_clrLink;
	COLORREF		m_clrHover;
	COLORREF		m_clrVisited;

	CString			m_strHyperLink;

	HCURSOR			m_hCursor;

	CToolTipCtrl	m_ToolTipCtrl;

	BOOL			m_bFocus;
	BOOL			m_bHover;
	BOOL			m_bMouseTracking;
	BOOL			m_bVisited;

	HFONT			m_hNormalFont;
	HFONT			m_hHoverFont;
	HFONT			m_hVisitedFont;
	
	BOOL			m_bTransparent;
	HDC				m_hBgDC;

	SKIN_LINK_TYPE	m_nLinkType;
};
