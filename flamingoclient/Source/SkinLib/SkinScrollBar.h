#pragma once

#include "SkinManager.h"

// Flags used for controlling the paint
#define UISTATE_FOCUSED      0x00000001
#define UISTATE_SELECTED     0x00000002
#define UISTATE_DISABLED     0x00000004
#define UISTATE_HOT          0x00000008
#define UISTATE_PUSHED       0x00000010
#define UISTATE_READONLY     0x00000020
#define UISTATE_CAPTURED     0x00000040

// 滚动块大小/滚动条长度 ≈ 页面大小/滚动条范围 ≈ 一屏显示的文档量/文档总量

class CSkinScrollBar
{
public:
	CSkinScrollBar(void);
	~CSkinScrollBar(void);

	BOOL SetBgNormalPic(LPCTSTR lpszFileName);
	BOOL SetBgHotPic(LPCTSTR lpszFileName);
	BOOL SetBgPushedPic(LPCTSTR lpszFileName);
	BOOL SetBgDisabledPic(LPCTSTR lpszFileName);

	BOOL GetShowLeftUpBtn();
	void SetShowLeftUpBtn(BOOL bShow);
	BOOL SetLeftUpBtnNormalPic(LPCTSTR lpszFileName);
	BOOL SetLeftUpBtnHotPic(LPCTSTR lpszFileName);
	BOOL SetLeftUpBtnPushedPic(LPCTSTR lpszFileName);
	BOOL SetLeftUpBtnDisabledPic(LPCTSTR lpszFileName);

	BOOL GetShowRightDownBtn();
	void SetShowRightDownBtn(BOOL bShow);
	BOOL SetRightDownBtnNormalPic(LPCTSTR lpszFileName);
	BOOL SetRightDownBtnHotPic(LPCTSTR lpszFileName);
	BOOL SetRightDownBtnPushedPic(LPCTSTR lpszFileName);
	BOOL SetRightDownBtnDisabledPic(LPCTSTR lpszFileName);

	BOOL SetThumbNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetThumbHotPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetThumbPushedPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetThumbDisabledPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);

	BOOL SetRailNormalPic(LPCTSTR lpszFileName);
	BOOL SetRailHotPic(LPCTSTR lpszFileName);
	BOOL SetRailPushedPic(LPCTSTR lpszFileName);
	BOOL SetRailDisabledPic(LPCTSTR lpszFileName);

	BOOL Create(HWND hOwnerWnd, UINT nCtrlId, RECT* lpRect, 
		DWORD dwUseTimerId, BOOL bHorizontal = TRUE, BOOL bVisible = TRUE);
	BOOL Destroy();

	void SetOwnerWnd(HWND hWnd);
	HWND GetOwnerWnd();

	void SetCtrlId(UINT nCtrlId);
	UINT GetCtrlId();

	void SetRect(RECT* lpRect);
	void GetRect(RECT* lpRect);

	BOOL IsHorizontal();
	void SetHorizontal(BOOL bHorizontal = TRUE);

	BOOL IsVisible();
	void SetVisible(BOOL bVisible = TRUE);

	BOOL IsEnabled();
	void SetEnabled(BOOL bEnable = TRUE);

	int GetScrollRange();
	void SetScrollRange(int nRange);
	int GetScrollPos();
	void SetScrollPos(int nPos);
	int GetLineSize();
	void SetLineSize(int nSize);

	LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnPaint(HDC hDC);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnTimer(UINT_PTR nIDEvent);

private:
	void DrawBg(HDC hDC);
	void DrawLeftUpBtn(HDC hDC);
	void DrawRightDownBtn(HDC hDC);
	void DrawThumb(HDC hDC);
	void DrawRail(HDC hDC);

	BOOL Invalidate(BOOL bErase = TRUE);
	void CalcScrollBarData();

	HWND SetCapture();
	BOOL ReleaseCapture();
	BOOL IsCaptured();

	BOOL StartTrackMouseLeave();
	BOOL IsMouseTracking();

protected:
	HWND m_hOwnerWnd;
	UINT m_nCtrlId;
	CRect m_rcScrollBar;
	BOOL m_bHorizontal;
	BOOL m_bVisible;
	BOOL m_bEnabled;

	int m_nRange;
	int m_nScrollPos;
	int m_nLineSize;

	POINT m_ptLastMouse;
	int m_nLastScrollPos;
	int m_nLastScrollOffset;
	int m_nScrollRepeatDelay;
	DWORD m_dwUseTimerId, m_dwTimerId;
	BOOL m_bMouseHover;
	BOOL m_bCaptured;
	BOOL m_bMouseTracking;

	CImageEx* m_lpBgImgN;
	CImageEx* m_lpBgImgH;
	CImageEx* m_lpBgImgP;
	CImageEx* m_lpBgImgD;

	BOOL m_bShowLeftUpBtn;
	CRect m_rcLeftUpBtn;
	DWORD m_dwLeftUpBtnState;
	CImageEx* m_lpLeftUpBtnImgN;
	CImageEx* m_lpLeftUpBtnImgH;
	CImageEx* m_lpLeftUpBtnImgP;
	CImageEx* m_lpLeftUpBtnImgD;

	BOOL m_bShowRightDownBtn;
	CRect m_rcRightDownBtn;
	DWORD m_dwRightDownBtnState;
	CImageEx* m_lpRightDownBtnImgN;
	CImageEx* m_lpRightDownBtnImgH;
	CImageEx* m_lpRightDownBtnImgP;
	CImageEx* m_lpRightDownBtnImgD;

	CRect m_rcThumb;
	DWORD m_dwThumbState;
	CImageEx* m_lpThumbImgN;
	CImageEx* m_lpThumbImgH;
	CImageEx* m_lpThumbImgP;
	CImageEx* m_lpThumbImgD;

	CImageEx* m_lpRailImgN;
	CImageEx* m_lpRailImgH;
	CImageEx* m_lpRailImgP;
	CImageEx* m_lpRailImgD;
};
