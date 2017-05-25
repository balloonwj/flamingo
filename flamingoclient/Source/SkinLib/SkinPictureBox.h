#pragma once

#include "SkinManager.h"
#include "../GifImage.h"

enum DRAW_MODE	// 绘图模式
{
	DRAW_MODE_STRETCH,	// 拉伸
	DRAW_MODE_CENTER	// 居中
};

class CSkinPictureBox : public CWindowImpl<CSkinPictureBox, CStatic>
{
public:
	CSkinPictureBox(void);
	~CSkinPictureBox(void);

	BEGIN_MSG_MAP_EX(CSkinPictureBox)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_WM_SETCURSOR(OnSetCursor)
		MSG_WM_TIMER(OnTimer)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	BOOL SetBgPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown);
	BOOL SetBitmap(LPCTSTR lpszFileName, BOOL bIsGif = FALSE, BOOL bGray=FALSE);	// 设置Gif需要绝对路径
	BOOL SetBitmapWithoutCache(LPCTSTR lpszFileName, BOOL bIsGif = FALSE, BOOL bGray=FALSE);
	BOOL SetBitmap(const BYTE* lpData, DWORD dwSize);
	BOOL SetMobileBitmap(LPCTSTR lpszFileName, BOOL bShow = FALSE);
	void SetToolTipText(LPCTSTR lpszText);
	void SetDrawMode(DRAW_MODE nMode = DRAW_MODE_CENTER);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void SetShowCursor(BOOL bShow);

	BOOL SubclassWindow(HWND hWnd);
	inline BOOL GetPress() { return m_bPress; }
	void   SetSelectedStatus(BOOL bSelectedStatus);					//设置选中状态

private:
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSetFocus(CWindow wndOld);
	void OnKillFocus(CWindow wndFocus);
	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);
	void OnTimer(UINT_PTR nIDEvent);
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();

	BOOL StartTrackMouseLeave();
	void DrawParentWndBg(HDC hDC);
	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);

private:
	CImageEx*		m_lpBgImgN,* m_lpBgImgH,* m_lpBgImgD;
	CImageEx*		m_lpImage;
	CImageEx*		m_lpImageGray;
	CGifImage*		m_lpGifImage;
	CImageEx*		m_lpMobileImage;

	CToolTipCtrl	m_ToolTipCtrl;

	HCURSOR			m_hCursor;

	BOOL			m_bFocus, m_bPress, m_bHover, m_bMouseTracking;

	BOOL			m_bSelectedStatus;

	BOOL			m_bGray;			//图片是否以灰度模式显示(只对静态图形起作用，不能针对gif图像)
	BOOL			m_bUseCache;
	BOOL			m_bShowMobileImage;

	DRAW_MODE		m_nDrawMode;
	DWORD			m_dwTimerId;
	BOOL			m_bTransparent;
	HDC				m_hBgDC;
};
