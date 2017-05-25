#pragma once

#include "SkinManager.h"

// 不支持滚动条皮肤
// 图片背景支持不完整
// Edit控件必须是ES_MULTILINE风格，SetMarginsEx才能生效，ES_MULTILINE风格好像不能动态修改，只能在生成控件时指定

class CSkinEdit : public CWindowImpl<CSkinEdit, CEdit>
{
public:
	CSkinEdit(void);
	~CSkinEdit(void);

	BEGIN_MSG_MAP_EX(CSkinEdit)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_NCCALCSIZE(OnNcCalcSize)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_NCPAINT(OnNcPaint)
		MSG_OCM_CTLCOLOREDIT(OnCtlColor)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_KILLFOCUS(OnKillFocus)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

public:
	BOOL SetBgNormalPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetBgHotPic(LPCTSTR lpszFileName, RECT* lpNinePart = NULL);
	BOOL SetIconPic(LPCTSTR lpszFileName);
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void SetDefaultText(LPCTSTR lpszText);
	BOOL IsDefaultText();
	void SetDefaultTextMode(BOOL bIsDefText);
	void SetMarginsEx(int nLeft, int nTop, int nRight, int nBottom);	// 设置上下左右边距函数
	void SetMultiLine(BOOL bMultiLine = TRUE);
	BOOL SubclassWindow(HWND hWnd);

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	LRESULT OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnNcPaint(CRgnHandle rgn);
	HBRUSH OnCtlColor(CDCHandle dc, CEdit edit);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnSetFocus(CWindow wndOld);
	void OnKillFocus(CWindow wndFocus);
	void OnSize(UINT nType, CSize size);
	void OnDestroy();

	BOOL StartTrackMouseLeave();
	void CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter);
	void SetCenterInSingleLine();

	void DrawParentWndBg(HDC hDC);

private:
	CImageEx* m_lpBgImgN;
	CImageEx* m_lpBgImgH;
	CImageEx* m_lpIconImg;
	int m_nIconWidth;
	BOOL m_bFocus, m_bPress, m_bHover, m_bMouseTracking;
	BOOL m_bTransparent;
	HDC m_hBgDC;
	BOOL m_bIsDefText;
	CString m_strDefText;
	TCHAR m_cPwdChar;
	BOOL m_bMultiLine;
};
