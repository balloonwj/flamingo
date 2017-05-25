#pragma once

#include "Path.h"
#include "GifImage.h"
#include "FaceList.h"

#define		FACE_CTRL_SEL		WM_USER + 1

class CFaceCtrl : public CWindowImpl<CFaceCtrl, CWindow>
{
public:
	CFaceCtrl(void);
	~CFaceCtrl(void);

	DECLARE_WND_CLASS(_T("__FaceCtrl__"))

	BEGIN_MSG_MAP_EX(CFaceCtrl)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void SetBgColor(COLORREF color);
	void SetLineColor(COLORREF color);
	void SetFocusBorderColor(COLORREF color);
	void SetZoomBorderColor(COLORREF color);
	void SetRowAndCol(int nRow, int nCol);
	void SetItemSize(int nWidth, int nHeight);
	void SetZoomSize(int nWidth, int nHeight);
	void SetFaceList(CFaceList* lpFaceList);
	void SetCurPage(int nPageIndex);

	CFaceInfo* GetFaceInfo(int nItemIndex);

private:
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnTimer(UINT_PTR nIDEvent);
	void OnDestroy();

 	BOOL StartTrackMouseLeave();
 	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);
 	int HitTest(POINT pt);
	void CalcZoomRect(CPoint point);
	BOOL GetItemRect(int nItemIndex, CRect& rect);
	CGifImage* GetZoomImage();
	void SetItemToolTip(int nItemIndex);
	void DrawLine(HDC hDC);
	void DrawFocusBorder(HDC hDC, const CRect& rect);
	void DrawZoomImage(HDC hDC);

	BOOL LoadImage(int nPageIndex);
	void DestroyImage();

private:
	COLORREF m_clrBg;
	COLORREF m_clrLine;
	COLORREF m_clrFocusBorder;
	COLORREF m_clrZoomBorder;
	int m_nRow, m_nCol;
	int m_nItemWidth, m_nItemHeight;
	int m_nZoomWidth, m_nZoomHeight;
	int m_nCurPage, m_nPageCnt;
	BOOL m_bMouseTracking;
	int m_nHoverIndex;
	int m_nFramePos;
	CRect m_rcZoomImg;
	CFaceList* m_lpFaceList;
	std::vector<CGifImage*> m_arrImage;
	CToolTipCtrl m_ToolTipCtrl;
	DWORD m_dwTimerId;
};
