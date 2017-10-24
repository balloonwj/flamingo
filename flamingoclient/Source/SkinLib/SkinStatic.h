#pragma once

#include "SkinManager.h"

class CSkinStatic : public CWindowImpl<CSkinStatic, CStatic>
{
public:
	CSkinStatic(void);
	~CSkinStatic(void);

	BEGIN_MSG_MAP_EX(CSkinStatic)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_SETTEXT(OnSetText)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	BOOL SetNormalBgPic(LPCTSTR lpszFileName);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	BOOL SubclassWindow(HWND hWnd);
	void SetTextColor(COLORREF clrText);
	void SetFont(HFONT hFont);

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	int OnSetText(LPCTSTR lpstrText);
	void OnDestroy();

	void DrawParentWndBg(HDC hDC);
	void Draw(HDC hDC);

private:
	CImageEx*	m_lpBgImgN;
	BOOL		m_bTransparent;
	HDC			m_hBgDC;
	COLORREF    m_clrText;
    //COLORREF    m_clrBgColor;
	HFONT		m_hFont;
};
