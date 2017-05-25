#pragma once

#include "SkinManager.h"

#define		WM_CBO_EDIT_MOUSE_HOVER		WM_USER + 1
#define		WM_CBO_EDIT_MOUSE_LEAVE		WM_USER + 2
#define		WM_CBO_LIST_HIDE			WM_USER + 3

class CSkinComboBox_Edit : public CWindowImpl<CSkinComboBox_Edit, CEdit>
{
public:
	CSkinComboBox_Edit(void);
	~CSkinComboBox_Edit(void);

	BEGIN_MSG_MAP_EX(CSkinComboBox_Edit)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_OCM_CTLCOLOREDIT(OnCtlColor)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

public:
	void SetOwnerWnd(HWND hWnd);
	void SetDefaultText(LPCTSTR lpszText);
	BOOL IsDefaultText();
	void SetDefaultTextMode(BOOL bIsDefText);
	

	BOOL SubclassWindow(HWND hWnd);
	HBRUSH OnCtlColor(CDCHandle dc, CEdit edit);
private:
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSetFocus(CWindow wndOld);
	void OnKillFocus(CWindow wndFocus);
	//HBRUSH OnCtlColor(CDCHandle dc, CEdit edit);

	BOOL StartTrackMouseLeave();
	void SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom);	// 设置上下左右边距函数

private:
	HWND m_hOwnerWnd;
	BOOL m_bMouseTracking;
	BOOL m_bIsDefText;
	CString m_strDefText;
	HBRUSH m_hBrush;
};


class CSkinComboBox_Static : public CWindowImpl<CSkinComboBox_Static, CStatic>
{
public:
	CSkinComboBox_Static(void);
	~CSkinComboBox_Static(void);

	BEGIN_MSG_MAP_EX(CSkinComboBox_Static)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_OCM_CTLCOLOREDIT(OnCtlColor)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

public:
	void SetOwnerWnd(HWND hWnd);
	void SetDefaultText(LPCTSTR lpszText);
	BOOL IsDefaultText();
	void SetDefaultTextMode(BOOL bIsDefText);

	BOOL SubclassWindow(HWND hWnd);
	HBRUSH OnCtlColor(CDCHandle dc, CEdit edit);
private:
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSetFocus(CWindow wndOld);
	void OnKillFocus(CWindow wndFocus);
	//HBRUSH OnCtlColor(CDCHandle dc, CEdit edit);

	BOOL StartTrackMouseLeave();
	void SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom);	// 设置上下左右边距函数

private:
	HWND m_hOwnerWnd;
	BOOL m_bMouseTracking;
	BOOL m_bIsDefText;
	CString m_strDefText;
	HBRUSH m_hBrush;
};

class CSkinComboBox_ListBox : public CWindowImpl<CSkinComboBox_ListBox, CListBox>
{
public:
	CSkinComboBox_ListBox(void);
	~CSkinComboBox_ListBox(void);

	BEGIN_MSG_MAP_EX(CSkinComboBox_ListBox)
		MSG_WM_SHOWWINDOW(OnShowWindow)
	END_MSG_MAP()

public:
	void SetOwnerWnd(HWND hWnd);

private:
	void OnShowWindow(BOOL bShow, UINT nStatus);

private:
	HWND m_hOwnerWnd;
};

//TODO: m_comboProxyType.AddString()方法默认会排序，需要改掉
class CSkinComboBox : public CWindowImpl<CSkinComboBox, CComboBox>
{
public:
	CSkinComboBox(void);
	~CSkinComboBox(void);

	BEGIN_MSG_MAP_EX(CSkinComboBox)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_SIZE(OnSize)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_DESTROY(OnDestroy)
		MESSAGE_HANDLER_EX(WM_CBO_EDIT_MOUSE_HOVER, OnEditMouseHover)
		MESSAGE_HANDLER_EX(WM_CBO_EDIT_MOUSE_LEAVE, OnEditMouseLeave)
		MESSAGE_HANDLER_EX(WM_CBO_LIST_HIDE, OnListHide)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

public:
	BOOL SetBgNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetBgHotPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetArrowNormalPic(LPCTSTR lpszFileName);
	BOOL SetArrowHotPic(LPCTSTR lpszFileName);
	BOOL SetArrowPushedPic(LPCTSTR lpszFileName);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void SetDefaultText(LPCTSTR lpszText);
	void SetDefaultTextMode(BOOL bDefaultMode);
	BOOL IsDefaultText();
	void SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom);	// 设置上下左右边距函数
	void SetArrowWidth(int nWidth);
	BOOL SubclassWindow(HWND hWnd);
	void SetReadOnly(BOOL bReadOnly);

public:
	CSkinComboBox_Edit m_Edit;
	CSkinComboBox_ListBox m_ListBox;	
private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSize(UINT nType, CSize size);
	void OnDestroy();
	LRESULT OnEditMouseHover(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnEditMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnListHide(UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL StartTrackMouseLeave();
	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);

	void DrawParentWndBg(HDC hDC);

private:
	CImageEx* m_lpBgImgN;
	CImageEx* m_lpBgImgH;
	CImageEx* m_lpArrowImgN;
	CImageEx* m_lpArrowImgH;
	CImageEx* m_lpArrowImgP;
	BOOL m_bFocus, m_bPress, m_bHover, m_bMouseTracking;
	BOOL m_bArrowPress, m_bArrowHover;
	BOOL m_bTransparent;
	HDC m_hBgDC;
	int m_nArrowWidth;
	CRect m_rcArrow;
};
