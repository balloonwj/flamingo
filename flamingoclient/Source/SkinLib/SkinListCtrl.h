#pragma once

#include <vector>
#include "SkinManager.h"
#include "SkinScrollBar.h"

//滚动条位置
#define WM_SCROLLBAR_POS    WM_USER + 0x00FF

struct NMHDREx
{
    NMHDR   hdr;
    LPARAM  lParam;
};

class CLVColumn
{
public:
	CLVColumn(void);
	~CLVColumn(void);

public:
	CString     m_strText;  //列头文字
	CImageEx*   m_lpImage;
	DWORD       m_dwFmt;    //可取值：DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
	int         m_nWidth;
};

class CLVItem
{
public:
	CLVItem(void);
	~CLVItem(void);

public:
	CString             m_strText;
    COLORREF            m_clrColor;
	Gdiplus::Bitmap*    m_lpImage;
	DWORD               m_dwFmt;
	LPARAM              m_lParam;
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

    //TODO: 函数名称要改！！
	BOOL SetItemText(int nCol,  LPCTSTR lpszText);
    BOOL SetItemColor(int nCol, COLORREF clrColor);
	BOOL SetItemImage(int nCol,  LPCTSTR lpszImage, int cx, int cy, BOOL bGray = FALSE);
	BOOL SetItemFormat(int nCol,  DWORD dwFmt);
	BOOL SetItemData(int nCol,  LPARAM lParam);

	CString GetItemText(int nCol);
	Gdiplus::Bitmap* GetItemImage(int nCol);
    COLORREF GetItemColor(int nCol);
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

    DECLARE_WND_CLASS(_T("__SkinListCtrl__"))


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
	BOOL SetBgPic(LPCTSTR lpszFileName);				// 设置列表背景图片, 背景图片在背景透明时不生效
    void SetHeaderBgColor(COLORREF clrBgColor);         // 设置列表表头背景颜色，默认颜色为白色，设置背景透明时，背景颜色不生效
    void SetBodyBgColor(COLORREF clrBgColor);           // 设置列表表体背景颜色，默认颜色为白色，设置背景透明时，背景颜色不生效
	BOOL SetHeadSeparatedPic(LPCTSTR lpszFileName);		// 设置表头分隔符图片

	BOOL SetHeadNormalPic(LPCTSTR lpszFileName);		// 设置表头背景图片(普通状态)
	BOOL SetHeadHotPic(LPCTSTR lpszFileName);			// 设置表头背景图片(高亮状态)
	BOOL SetHeadPushedPic(LPCTSTR lpszFileName);		// 设置表头背景图片(按下状态)

    void SetGridLineVisible(BOOL bVisible);             // 网格线是否可见，默认不可见
    void SetGridLineWidth(int nWidth);                  // 设置网格线粗细，默认1px
    void SetGridLineColor(COLORREF clr);                // 设置网格线颜色，默认

	BOOL SetOddItemBgPic(LPCTSTR lpszFileName);			// 设置奇行背景图片(普通状态)
	BOOL SetEvenItemBgPic(LPCTSTR lpszFileName);		// 设置双行背景图片(普通状态)
	BOOL SetItemHoverBgPic(LPCTSTR lpszFileName);		// 设置行背景图片(高亮状态)
	BOOL SetItemSelBgPic(LPCTSTR lpszFileName);			// 设置行背景图片(选中状态)

	void SetTransparent(BOOL bTransparent, HDC hBgDC);	// 设置列表背景是否透明
	void SetHeadVisible(BOOL bVisible);					// 设置表头是否可见
	BOOL IsHeadVisible();								// 判断表头是否可见
	void SetHeadHeight(int nHeight);					// 设置表头高度
	void SetItemHeight(int nHeight);					// 设置行高度
	void SetItemImageSize(int cx, int cy);				// 设置行图像大小

	int AddColumn(LPCTSTR lpszText,						// 添加列，相当于给所有行增加一个单元格
		LPCTSTR lpszImage, DWORD dwFmt, int nWidth);
    //int AddColumns()
	int InsertColumn(int nCol, LPCTSTR lpszText,		// 插入列，原理同上
		LPCTSTR lpszImage, DWORD dwFmt, int nWidth);
	BOOL DeleteColumn(int nCol);						// 删除列，原理同上

	int AddItem(LPCTSTR lpszText,						// 添加行，同时设置该行第一个单元格的文字
		LPCTSTR lpszImage, BOOL bGray, DWORD dwFmt, LPARAM lParam);
	int InsertItem(int nItem, LPCTSTR lpszText,			// 插入行，同时设置该行第一个单元格的文字，nItem标识的前一行必须已经存在，否则程序会出错
		LPCTSTR lpszImage, BOOL bGray, DWORD dwFmt, LPARAM lParam);
	BOOL DeleteItem(int nItem);							// 删除行

	int GetColumnCount();								// 获取总列数
	BOOL DeleteAllColumns();							// 删除所有列
	int GetItemCount();									// 获取总行数
	BOOL DeleteAllItems();								// 删除所有行

	BOOL SetColumnText(int nCol, LPCTSTR lpszText);		// 设置列头文本
	BOOL SetColumnImage(int nCol, LPCTSTR lpszImage);	// 设置列头图像
	BOOL SetColumnFormat(int nCol, DWORD dwFmt);		// 设置列文字格式
	BOOL SetColumnWidth(int nCol, int nWidth);			// 设置列宽度

	CString GetColumnText(int nCol);					// 获取列文本
	DWORD GetColumnFormat(int nCol);					// 获取列格式
	int GetColumnWidth(int nCol);						// 获取列宽度

	BOOL SetItemText(int nItem, int nSubItem, LPCTSTR lpszText);	// 设置行文本，如果对应的单元格不存在，则不会产生任何效果
    BOOL SetCellTextColor(int nItem, int nSubItem, COLORREF clrColor);	// 设置单元格文本颜色，如果对应的单元格不存在，则不会产生任何效果
	BOOL SetItemImage(int nItem, int nSubItem,						// 设置行图像
		LPCTSTR lpszImage, BOOL bGray = FALSE);
	BOOL SetItemFormat(int nItem, int nSubItem, DWORD dwFmt);		// 设置行格式
    //BOOL SetRowFormat(int nItem, DWORD dwFmt);		                // 设置行格式, TODO: 这个函数要优化(太慢！！)
	BOOL SetItemData(int nItem, int nSubItem, LPARAM lParam);		// 设置行数据

	CString GetItemText(int nItem, int nSubItem);		// 获取行文本
	DWORD GetItemFormat(int nItem, int nSubItem);		// 获取行格式
	LPARAM GetItemData(int nItem, int nSubItem);		// 获取行数据


    //TODO: 这些函数名称要改，明明是设置行不是设置单元格
	void SetCurSelIndex(int nIndex);
	int GetCurSelItemIndex();							// 获取当前选中行索引
	void SetItemTextColor(COLORREF clr);				// 设置行文本颜色(普通状态)
	void SetSelItemTextColor(COLORREF clr);				// 行文本颜色(选中状态)
    void SetSelItemBkColor(COLORREF clr);               // 行选中状态背景颜色

    //设置滚动条的位置
    void SetVScrollBarPos(int nPos);
    void SetHScrollBarPos(int nPos);

    //是否开启向父窗口汇报实时垂直滚动条的位置
    void EnableVScrollBarRealPostion(BOOL bEnable);
    //是否开启点击表头显示排序
    void EnableClickHeaderSort(BOOL bEnable);
    //是否开启表格行可选模式
    void EnableRowSelectable(BOOL bEnable);

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
	void DrawHeader(HDC hDC);
    void DrawTopFixedRow(HDC hDC);
    void DrawBody(HDC hDC);

    //获取列表头部区域大小
	BOOL GetHeaderRect(CRect& rect);
    //获取顶部固定行的位置
    BOOL GetTopFixedRowRect(CRect& rect);
    //获取列表表体部分区域，如果有滚动条则减去滚动条对应的宽或高
	BOOL GetBodyRect(CRect& rect);
	BOOL GetItemRect(int nIndex, CRect& rect);
	int HeadHitTest(POINT pt);
	int ItemHitTest(POINT pt);
	BOOL StartTrackMouseLeave();
	void AdjustScrollBarSize();
	void CheckScrollBarStatus();
	void Scroll(int cx, int cy);
	void EnsureVisible(int nIndex);
	int GetColumnsWidth();

    //发送垂直滚动条实时位置至父窗口
    void PostVScrollBarRealPostion(int nVScrollBarPos);
    //画表头上下排序样式箭头
    void DrawHeaderItemSortStyle(int nSortStyleIndex);

	BOOL Init();
	void UnInit();

private:
	//CToolTipCtrl m_ToolTipCtrl;
	CSkinScrollBar          m_HScrollBar;
	CSkinScrollBar          m_VScrollBar;

	std::vector<CLVColumn*> m_arrCols;
	std::vector<CLVRow*>    m_arrRows;

	CImageEx*               m_lpBgImg;					// 列表背景图片
    COLORREF                m_clrHeaderBgColor;         // 表头背景颜色
    COLORREF                m_clrBodyBgColor;           // 表体背景颜色
	CImageEx*               m_lpHeadSeparatedImg;		// 表头分隔符图片
	CImageEx*               m_lpHeadImgN;				// 表头背景图片(普通状态)
	CImageEx*               m_lpHeadImgH;				// 表头背景图片(高亮状态)
	CImageEx*               m_lpHeadImgP;				// 表头背景图片(按下状态)
	CImageEx*               m_lpOddItemBgImgN;			// 奇行背景图片(普通状态)
	CImageEx*               m_lpEvenItemBgImgN;			// 双行背景图片(普通状态)
	CImageEx*               m_lpItemBgImgH;				// 行背景图片(高亮状态)
	CImageEx*               m_lpItemBgImgS;				// 行背景图片(选中状态)

	BOOL                    m_bTransparent;			    // 列表背景是否透明标志
	HDC                     m_hBgDC;					// 父窗口背景DC
	BOOL                    m_bMouseTracking;			// 鼠标跟踪标志
	BOOL                    m_bHeadVisible;				// 表头是否可见标志
	int                     m_nHeadHeight;				// 表头高度
	int                     m_nRowHeight;				// 行高度
	CSize                   m_szItemImage;				// 行图像大小
    BOOL                    m_bGridLineVisible;         // 是否画网格线
    int                     m_nGridLineWidth;           // 网格线的宽度

	int                     m_nPressHeadIndex;			// 鼠标按下表头项索引
	int                     m_nHoverHeadIndex;			// 鼠标悬停表头项索引

	int                     m_nPressItemIndex;			// 鼠标按下行项索引
	int                     m_nHoverItemIndex;			// 鼠标悬停行项索引
	int                     m_nSelItemIndex;			// 鼠标选中行项索引

	int                     m_nLeft, m_nTop;			// 视图原点坐标

	COLORREF                m_clrItemText;				// 行文本颜色(普通状态)
	COLORREF                m_clrSelItemText;           // 选中行文本颜色
    COLORREF                m_clrSelItemBkColor;        // 选中行背景颜色
    COLORREF                m_clrGridLine;              // 网格线颜色

    int                     m_nTopFixedRow;             //顶部固定行

    int                     m_nStartVisibleIndex;       //TODO: 变量名称要改
    int                     m_nEndVisibleIndex;         //TODO: 变量名称要改
    BOOL                    m_bEnablePostVScrollBarPos; //是否向父窗口报告实时滚动条位置，默认为FALSE
    BOOL                    m_bEnableClickHeaderSort;   //点击表头，是否支持排序，默认不开启（FALSE）
    BOOL                    m_bEnableRowSelectable;     //表格行是否可以选中，默认可以（TRUE）
};
