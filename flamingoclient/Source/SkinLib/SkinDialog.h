#pragma once

#include <gdiplus.h>
#include "SkinManager.h"

//注意：SkinDlg默认没有开启裁剪子窗口，如果界面改变大小时闪烁，记得在外部加上WS_CLIPCHILDREN风格
class CSkinDialog : public CWindowImpl<CSkinDialog, CWindow>
{
public:
	CSkinDialog(void);
	~CSkinDialog(void);

	BEGIN_MSG_MAP_EX(CSkinDialog)
		MSG_WM_CREATE(OnCreate)
		//MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
		MSG_WM_NCCREATE(OnNcCreate)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_NCHITTEST(OnNcHitTest)
		MSG_WM_NCCALCSIZE(OnNcCalcSize)
		MSG_WM_NCACTIVATE(OnNcActivate)
		MSG_WM_NCPAINT(OnNcPaint)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)

		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
	END_MSG_MAP()

public:
	void SetBgColor(COLORREF clrBg);
	BOOL SetBgPic(LPCTSTR lpFileName);
	BOOL SetBgPic(LPCTSTR lpFileName, const CRect& rcNinePart);
	BOOL SetTitleBarBgPic(LPCTSTR lpFileName);
	BOOL SetTitleBarBgPic(LPCTSTR lpFileName, const CRect& rcNinePart);
	BOOL SetMinSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown);
	BOOL SetMaxSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown);
	BOOL SetRestoreSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown);
	BOOL SetCloseSysBtnPic(LPCTSTR lpNormal, LPCTSTR lpHighlight, LPCTSTR lpDown);
	BOOL SetTitleText(LPCTSTR lpszText);
	CString GetTitleText();
	HDC GetBgDC();
	
	BOOL SubclassWindow(HWND hWnd);
	void SetHotRegion(HRGN hrgnHot);				//设置热点区域
	void SetDragRegion(HRGN hrgnDrag);				//设置拖拽区域(TODO: 改成名字为SetTitleRect， Region在内部实现，不要在外部创建区域gdi对象了)

	//void MoveWindow2(int X,int Y,int nWidth,int nHeight,BOOL bRedraw);

private:
	BOOL OnNcCreate(LPCREATESTRUCT lpCreateStruct);
    //TODO: 其实这个OnCreate是不会被调用的
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	//HBRUSH OnCtlColorStatic(CDCHandle dc, CStatic wndStatic);
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	BOOL OnNcActivate(BOOL bActive);
	UINT OnNcHitTest(CPoint point);
	LRESULT OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam);
	void OnNcPaint(CRgnHandle rgn);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnGetMinMaxInfo(LPMINMAXINFO lpMMI);
	void OnSize(UINT nType, CSize size);
	void OnDestroy();

	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

	void CalcTitleBarRect();
	int HitTest(POINT pt);
	BOOL StartTrackMouseLeave();

	void UpdateTooltip(BOOL bRemove, PCTSTR lpszTipText);

private:
	BOOL		m_bHasTitleBar;
	BOOL		m_bHasMinBtn, m_bHasMaxBtn, m_bHasCloseBtn;
	BOOL		m_bSizeBox;

	CRect		m_rcTitleBar;
	CRect		m_rcMinBtn, m_rcMaxBtn, m_rcCloseBtn;

	BOOL		m_bMouseTracking;
	BOOL		m_bMinBtnPress, m_bMinBtnHover;
	BOOL		m_bMaxBtnPress, m_bMaxBtnHover;
	BOOL		m_bCloseBtnPress, m_bCloseBtnHover;

	COLORREF	m_clrBg;
	CImageEx*	m_lpBgImg;
	CImageEx*	m_lpTitleBarBgImg;
	CImageEx*	m_lpMinSysBtnImgN,* m_lpMinSysBtnImgH,* m_lpMinSysBtnImgD;
	CImageEx*	m_lpMaxSysBtnImgN,* m_lpMaxSysBtnImgH,* m_lpMaxSysBtnImgD;
	CImageEx*	m_lpRestoreSysBtnImgN,* m_lpRestoreSysBtnImgH,* m_lpRestoreSysBtnImgD;
	CImageEx*	m_lpCloseSysBtnImgN,* m_lpCloseSysBtnImgH,* m_lpCloseSysBtnImgD;

	CString		m_strTitleText;

	HDC			m_hMemDC;
	HBITMAP		m_hMemBmp, m_hOldBmp;

	HRGN		m_hHotRgn;				//可以的拖拽区域
	HRGN		m_hDragRgn;

	HRGN		m_hWndRgn;				//窗口形状

	CToolTipCtrl m_ToolTipCtrl;
};
