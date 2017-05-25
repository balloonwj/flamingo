#pragma once

int PopSkinMessage(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

class CSkinMessageBox : public CWindowImpl<CSkinMessageBox, CWindow>
{
public:
	CSkinMessageBox();
	~CSkinMessageBox();

	BEGIN_MSG_MAP_EX(CSkinMessageBox)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void ShowMaxButton(BOOL bShow = TRUE);

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnSize(UINT nType, CSize size);
	void OnDestroy();
	int HitTest(POINT pt);

private:
	BOOL m_bSizeBox;
	BOOL m_bHasMinBtn, m_bHasMaxBtn, m_bHasCloseBtn;

	CRect m_rcMinBtn, m_rcMaxBtn, m_rcCloseBtn;

	BOOL m_bMinBtnPress, m_bMaxBtnPress, m_bCloseBtnPress;
	BOOL m_bMinBtnHover, m_bMaxBtnHover, m_bCloseBtnHover;

	COLORREF m_clrBg;
	HDC m_hMemDC;
	HBITMAP m_hMemBmp, m_hOldBmp;
};