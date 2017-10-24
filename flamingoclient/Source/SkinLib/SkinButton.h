#pragma once

#include "SkinManager.h"

enum SKIN_BUTTON_TYPE
{
	SKIN_PUSH_BUTTON,
	SKIN_CHECKBOX,
	SKIN_RADIO_BUTTON,
	SKIN_GROUPBOX,
	SKIN_ICON_BUTTON,
	SKIN_MENU_BUTTON,
};

class CSkinButton : public CWindowImpl<CSkinButton, CButton>
{
public:
	CSkinButton(void);
	~CSkinButton(void);

	BEGIN_MSG_MAP_EX(CSkinButton)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDown)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_WM_SETTEXT(OnSetText)
		//MSG_WM_KEYDOWN(OnKeyDown)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void SetButtonType(SKIN_BUTTON_TYPE nType);
	BOOL SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
		LPCTSTR lpDown, LPCTSTR lpFocus);
	BOOL SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
		LPCTSTR lpDown, LPCTSTR lpFocus, const CRect& rcNinePart);
	BOOL SetCheckBoxPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, 
		LPCTSTR lpTickNormal, LPCTSTR lpTickHighlight);
	BOOL SetArrowPic(LPCTSTR lpszFileName);
	BOOL SetIconPic(LPCTSTR lpszFileName);
	void SetMenu(HMENU hMenu);
	void SetToolTipText(LPCTSTR lpszText);
	void SetRound(int cxRound, int cyRound);
	void SetTextAlign(DWORD dwTextAlign);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void SetTextColor(COLORREF clrText);
	void SetTextBoldStyle(BOOL bBold);

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
	int OnSetText(LPCTSTR lpstrText);
	//void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();

	BOOL StartTrackMouseLeave();
	void DrawPushButton(HDC hDC);
	void DrawCheckBox(HDC hDC);
	void DrawRadioButton(HDC hDC);
	void DrawGroupBox(HDC hDC);
	void DrawIconButton(HDC hDC);
	void DrawMenuButton(HDC hDC);
	void DrawParentWndBg(HDC hDC);
	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);
	
private:
	CImageEx*        m_lpBgImgN,* m_lpBgImgH,* m_lpBgImgD,* m_lpBgImgF;
	CImageEx*        m_lpCheckBoxImgN,* m_lpCheckBoxImgH;
	CImageEx*        m_lpCheckBoxImgTickN,* m_lpCheckBoxImgTickH;
	CImageEx*        m_lpArrowImg,* m_lpIconImg;
	BOOL             m_bFocus, m_bPress, m_bHover, m_bMouseTracking;
	SKIN_BUTTON_TYPE m_nBtnType;
	HMENU            m_hMenu;
	CToolTipCtrl     m_ToolTipCtrl;
	int              m_cxRound, m_cyRound;
	DWORD            m_dwTextAlign;
	BOOL             m_bTransparent;
	HDC              m_hBgDC;
	COLORREF         m_clrText;
    COLORREF         m_clrBgColor;
	BOOL	         m_bTextBold;
};
