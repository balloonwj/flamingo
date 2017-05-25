#pragma once

#include <vector>
#include "SkinManager.h"
#include "SkinScrollBar.h"

class CLVColumn
{
public:
	CLVColumn(void);
	~CLVColumn(void);

public:
	CString m_strText;
	CImageEx* m_lpImage;
	DWORD m_dwFmt;
	int m_nWidth;
};

class CLVItem
{
public:
	CLVItem(void);
	~CLVItem(void);

public:
	CString m_strText;
	Gdiplus::Bitmap* m_lpImage;
	DWORD m_dwFmt;
	LPARAM m_lParam;
};

class CLVRow
{
public:
	CLVRow(int nCols);
	~CLVRow(void);

public:
	BOOL AddItem();
	BOOL InsertItem(int nCol);
	BOOL DeleteItem(int nCol);

	BOOL SetItem(int nCol, LPCTSTR lpszText, LPCTSTR lpszImage, 
		int cx, int cy, BOOL bGray, DWORD dwFmt, LPARAM lParam);

	BOOL SetItemText(int nCol,  LPCTSTR lpszText);
	BOOL SetItemImage(int nCol,  LPCTSTR lpszImage, int cx, int cy, BOOL bGray = FALSE);
	BOOL SetItemFormat(int nCol,  DWORD dwFmt);
	BOOL SetItemData(int nCol,  LPARAM lParam);

	CString GetItemText(int nCol);
	Gdiplus::Bitmap* GetItemImage(int nCol);
	DWORD GetItemFormat(int nCol);
	LPARAM GetItemData(int nCol);

	int GetItemCount();
	BOOL DeleteAllItems();

private:
	CLVItem* GetItem(int nCol);

private:
	std::vector<CLVItem*> m_arrItems;
};

class CSkinListCtrl : public CWindowImpl<CSkinListCtrl, CWindow>
{
public:
	CSkinListCtrl(void);
	~CSkinListCtrl(void);

	BEGIN_MSG_MAP_EX(CSkinListCtrl)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_RBUTTONDOWN(OnRButtonDown)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_HSCROLL(OnHScroll)
		MSG_WM_VSCROLL(OnVScroll)
		MSG_WM_MOUSEWHEEL(OnMouseWheel)
		MSG_WM_KEYDOWN(OnKeyDown)
		MSG_WM_SIZE(OnSize)
		MSG_WM_GETDLGCODE(OnGetDlgCode)
		//MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	BOOL SetBgPic(LPCTSTR lpszFileName);				// 设置列表背景图片
	BOOL SetHeadSeparatedPic(LPCTSTR lpszFileName);		// 设置表头分隔符图片

	BOOL SetHeadNormalPic(LPCTSTR lpszFileName);		// 设置表头背景图片(普通状态)
	BOOL SetHeadHotPic(LPCTSTR lpszFileName);			// 设置表头背景图片(高亮状态)
	BOOL SetHeadPushedPic(LPCTSTR lpszFileName);		// 设置表头背景图片(按下状态)

	BOOL SetOddItemBgPic(LPCTSTR lpszFileName);			// 设置奇行背景图片(普通状态)
	BOOL SetEvenItemBgPic(LPCTSTR lpszFileName);		// 设置双行背景图片(普通状态)
	BOOL SetItemHotBgPic(LPCTSTR lpszFileName);			// 设置行背景图片(高亮状态)
	BOOL SetItemSelBgPic(LPCTSTR lpszFileName);			// 设置行背景图片(选中状态)

	void SetTransparent(BOOL bTransparent, HDC hBgDC);	// 设置列表背景是否透明
	void SetHeadVisible(BOOL bVisible);					// 设置表头是否可见
	BOOL IsHeadVisible();								// 判断表头是否可见
	void SetHeadHeight(int nHeight);					// 设置表头高度
	void SetItemHeight(int nHeight);					// 设置行高度
	void SetItemImageSize(int cx, int cy);				// 设置行图像大小

	int AddColumn(LPCTSTR lpszText,						// 添加列
		LPCTSTR lpszImage, DWORD dwFmt, int nWidth);
	int InsertColumn(int nCol, LPCTSTR lpszText,		// 插入列
		LPCTSTR lpszImage, DWORD dwFmt, int nWidth);
	BOOL DeleteColumn(int nCol);						// 删除列

	int AddItem(LPCTSTR lpszText,						// 添加行
		LPCTSTR lpszImage, BOOL bGray, DWORD dwFmt, LPARAM lParam);
	int InsertItem(int nItem, LPCTSTR lpszText,			// 插入行
		LPCTSTR lpszImage, BOOL bGray, DWORD dwFmt, LPARAM lParam);
	BOOL DeleteItem(int nItem);							// 删除行

	int GetColumnCount();								// 获取总列数
	BOOL DeleteAllColumns();							// 删除所有列
	int GetItemCount();									// 获取总行数
	BOOL DeleteAllItems();								// 删除所有行

	BOOL SetColumnText(int nCol, LPCTSTR lpszText);		// 设置列文本
	BOOL SetColumnImage(int nCol, LPCTSTR lpszImage);	// 设置列图像
	BOOL SetColumnFormat(int nCol, DWORD dwFmt);		// 设置列格式
	BOOL SetColumnWidth(int nCol, int nWidth);			// 设置列宽度

	CString GetColumnText(int nCol);					// 获取列文本
	DWORD GetColumnFormat(int nCol);					// 获取列格式
	int GetColumnWidth(int nCol);						// 获取列宽度

	BOOL SetItemText(int nItem, int nSubItem, LPCTSTR lpszText);	// 设置行文本
	BOOL SetItemImage(int nItem, int nSubItem,						// 设置行图像
		LPCTSTR lpszImage, BOOL bGray = FALSE);
	BOOL SetItemFormat(int nItem, int nSubItem, DWORD dwFmt);		// 设置行格式
	BOOL SetItemData(int nItem, int nSubItem, LPARAM lParam);		// 设置行数据

	CString GetItemText(int nItem, int nSubItem);		// 获取行文本
	DWORD GetItemFormat(int nItem, int nSubItem);		// 获取行格式
	LPARAM GetItemData(int nItem, int nSubItem);		// 获取行数据

	void SetCurSelIndex(int nIndex);
	int GetCurSelItemIndex();							// 获取当前选中行索引
	void SetItemTextColor(COLORREF clr);				// 设置行文本颜色(普通状态)
	void SetSelItemTextColor(COLORREF clr);				// 行文本颜色(选中状态)

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnRButtonDown(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnTimer(UINT_PTR nIDEvent);
	void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
	void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
	BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnSize(UINT nType, CSize size);
	UINT OnGetDlgCode(LPMSG lpMsg);
	//LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();

	CLVColumn* GetColumn(int nCol);
	CLVRow* GetItem(int nItem);

	void DrawParentWndBg(HDC hDC);
	void DrawHeadPart(HDC hDC);
	void DrawItemPart(HDC hDC);

	BOOL GetHeadPartRect(CRect& rect);
	BOOL GetItemPartRect(CRect& rect);
	BOOL GetItemRect(int nIndex, CRect& rect);
	int HeadHitTest(POINT pt);
	int ItemHitTest(POINT pt);
	BOOL StartTrackMouseLeave();
	void AdjustScrollBarSize();
	void CheckScrollBarStatus();
	void Scroll(int cx, int cy);
	void EnsureVisible(int nIndex);
	int GetColumnsWidth();

	BOOL Init();
	void UnInit();

private:
	//CToolTipCtrl m_ToolTipCtrl;
	CSkinScrollBar m_HScrollBar;
	CSkinScrollBar m_VScrollBar;

	std::vector<CLVColumn*> m_arrCols;
	std::vector<CLVRow*> m_arrRows;

	CImageEx* m_lpBgImg;					// 列表背景图片
	CImageEx* m_lpHeadSeparatedImg;		// 表头分隔符图片
	CImageEx* m_lpHeadImgN;				// 表头背景图片(普通状态)
	CImageEx* m_lpHeadImgH;				// 表头背景图片(高亮状态)
	CImageEx* m_lpHeadImgP;				// 表头背景图片(按下状态)
	CImageEx* m_lpOddItemBgImgN;			// 奇行背景图片(普通状态)
	CImageEx* m_lpEvenItemBgImgN;			// 双行背景图片(普通状态)
	CImageEx* m_lpItemBgImgH;				// 行背景图片(高亮状态)
	CImageEx* m_lpItemBgImgS;				// 行背景图片(选中状态)

	BOOL m_bTransparent;					// 列表背景是否透明标志
	HDC m_hBgDC;							// 父窗口背景DC
	BOOL m_bMouseTracking;					// 鼠标跟踪标志
	BOOL m_bHeadVisible;					// 表头是否可见标志
	int m_nHeadHeight;						// 表头高度
	int m_nItemHeight;						// 行高度
	CSize m_szItemImage;					// 行图像大小

	int m_nPressHeadIndex;					// 鼠标按下表头项索引
	int m_nHoverHeadIndex;					// 鼠标悬停表头项索引

	int m_nPressItemIndex;					// 鼠标按下行项索引
	int m_nHoverItemIndex;					// 鼠标悬停行项索引
	int m_nSelItemIndex;					// 鼠标选中行项索引

	int m_nLeft, m_nTop;					// 视图原点坐标

	COLORREF m_clrItemText;					// 行文本颜色(普通状态)
	COLORREF m_clrSelItemText;				// 行文本颜色(选中状态)
};
