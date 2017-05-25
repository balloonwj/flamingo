#include "stdafx.h"
#include "SkinListCtrl.h"

void Gray(Gdiplus::Bitmap* lpSrcBmp)
{
	UINT nWidth = lpSrcBmp->GetWidth();
	UINT nHeight = lpSrcBmp->GetHeight();

	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, nWidth, nHeight);
	lpSrcBmp->LockBits(&rect, Gdiplus::ImageLockModeRead|Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

	// 	LPRGBQUAD p = (LPRGBQUAD)bmpData.Scan0;
	// 	INT offset = bmpData.Stride - bmpData.Width* sizeof(LPRGBQUAD);
	// 
	// 	for (UINT y = 0; y < bmpData.Height; y ++, ((BYTE*)p += offset))
	// 	{
	// 		for (UINT x = 0; x < bmpData.Width; x ++, p ++)
	// 			p->rgbBlue = p->rgbGreen = p->rgbRed = 
	// 			(UINT)(p->rgbBlue* 29 + p->rgbGreen* 150 + p->rgbRed* 77 + 128) >> 8;
	// 
	// 	}

	int nValue = 0;
	BYTE* pArray = (BYTE*)bmpData.Scan0;
	UINT nBytes = (UINT)((nWidth* nHeight) << 2);

	for (UINT i = 0; i < nBytes; i += 4)
	{
		nValue = (int)(pArray[i]* 0.1 + pArray[i+1]* 0.2 + pArray[i+2]* 0.7);

		pArray[i] = nValue;
		pArray[i+1] = nValue;
		pArray[i+2] = nValue;
	}

	lpSrcBmp->UnlockBits(&bmpData);
}

CLVColumn::CLVColumn(void)
{
	m_lpImage = NULL;
	m_dwFmt = 0;
	m_nWidth = 0;
}

CLVColumn::~CLVColumn(void)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpImage);
}

CLVItem::CLVItem(void)
{
	m_lpImage = NULL;
	m_dwFmt = 0;
	m_lParam = 0;
}

CLVItem::~CLVItem(void)
{
	if (m_lpImage != NULL)
	{
		delete m_lpImage;
		m_lpImage = NULL;
	}
}

CLVRow::CLVRow(int nCols)
{
	for (int i = 0; i < nCols; i++)
	{
		AddItem();
	}
}

CLVRow::~CLVRow(void)
{
	DeleteAllItems();
}

BOOL CLVRow::AddItem()
{
	CLVItem* lpItem = new CLVItem;
	if (NULL == lpItem)
		return FALSE;
	m_arrItems.push_back(lpItem);
	return TRUE;
}

BOOL CLVRow::InsertItem(int nCol)
{
	CLVItem* lpItem = new CLVItem;
	if (NULL == lpItem)
		return FALSE;
	m_arrItems.insert(m_arrItems.begin() + nCol, lpItem);
	return TRUE;
}

BOOL CLVRow::DeleteItem(int nCol)
{
	if (nCol < 0 || nCol >= (int)m_arrItems.size())
		return FALSE;

	CLVItem* lpItem = m_arrItems[nCol];
	if (lpItem != NULL)
	{
		delete lpItem;
		lpItem = NULL;
	}
	m_arrItems.erase(m_arrItems.begin() + nCol);
	return TRUE;
}

BOOL CLVRow::SetItem(int nCol, LPCTSTR lpszText, LPCTSTR lpszImage, 
					 int cx, int cy, BOOL bGray, DWORD dwFmt, LPARAM lParam)
{
	if (nCol < 0 || nCol >= (int)m_arrItems.size())
		return FALSE;

	CLVItem* lpItem = m_arrItems[nCol];
	if (NULL == lpItem)
		return FALSE;

	lpItem->m_strText = lpszText;
	SetItemImage(nCol, lpszImage, cx, cy, bGray);
	lpItem->m_dwFmt = dwFmt;
	lpItem->m_lParam = lParam;
	return TRUE;
}

BOOL CLVRow::SetItemText(int nCol,  LPCTSTR lpszText)
{
	CLVItem* lpItem = GetItem(nCol);
	if (NULL == lpItem)
		return FALSE;
	lpItem->m_strText = lpszText;
	return TRUE;
}

BOOL CLVRow::SetItemImage(int nCol,  LPCTSTR lpszImage, int cx, int cy, BOOL bGray/* = FALSE*/)
{
	CLVItem* lpItem = GetItem(nCol);
	if (NULL == lpItem)
		return FALSE;

	if (lpItem->m_lpImage != NULL)
		delete lpItem->m_lpImage;

	lpItem->m_lpImage = new Gdiplus::Bitmap(lpszImage);
	if (NULL == lpItem->m_lpImage || lpItem->m_lpImage->GetLastStatus() != Gdiplus::Ok)
	{
		delete lpItem->m_lpImage;
		lpItem->m_lpImage = NULL;
		return FALSE;
	}

	if (lpItem->m_lpImage->GetWidth() != cx || lpItem->m_lpImage->GetHeight() != cy)
	{
		Gdiplus::Bitmap* pThumbnail = (Gdiplus::Bitmap*)lpItem->m_lpImage->GetThumbnailImage(cx, cy);
		if (NULL == pThumbnail)
		{
			delete lpItem->m_lpImage;
			lpItem->m_lpImage = NULL;
			return FALSE;
		}
		delete lpItem->m_lpImage;
		lpItem->m_lpImage = pThumbnail;
	}

	if (bGray)
		Gray(lpItem->m_lpImage);
	
	return TRUE;
}

BOOL CLVRow::SetItemFormat(int nCol,  DWORD dwFmt)
{
	CLVItem* lpItem = GetItem(nCol);
	if (NULL == lpItem)
		return FALSE;
	lpItem->m_dwFmt = dwFmt;
	return TRUE;
}

BOOL CLVRow::SetItemData(int nCol,  LPARAM lParam)
{
	CLVItem* lpItem = GetItem(nCol);
	if (NULL == lpItem)
		return FALSE;
	lpItem->m_lParam = lParam;
	return TRUE;
}

CString CLVRow::GetItemText(int nCol)
{
	CLVItem* lpItem = GetItem(nCol);
	return (NULL == lpItem) ? _T("") : lpItem->m_strText;
}

Gdiplus::Bitmap* CLVRow::GetItemImage(int nCol)
{
	CLVItem* lpItem = GetItem(nCol);
	return (NULL == lpItem) ? NULL : lpItem->m_lpImage;
}

DWORD CLVRow::GetItemFormat(int nCol)
{
	CLVItem* lpItem = GetItem(nCol);
	return (NULL == lpItem) ? 0 : lpItem->m_dwFmt;
}

LPARAM CLVRow::GetItemData(int nCol)
{
	CLVItem* lpItem = GetItem(nCol);
	return (NULL == lpItem) ? 0 : lpItem->m_lParam;
}

int CLVRow::GetItemCount()
{
	return (int)m_arrItems.size();
}

BOOL CLVRow::DeleteAllItems()
{
	for (int i = 0; i < (int)m_arrItems.size(); i++)
	{
		CLVItem* lpItem = m_arrItems[i];
		if (lpItem != NULL)
		{
			delete lpItem;
			lpItem = NULL;
		}
	}
	m_arrItems.clear();
	return TRUE;
}

CLVItem* CLVRow::GetItem(int nCol)
{
	if (nCol < 0 || nCol >= (int)m_arrItems.size())
		return NULL;
	else
		return m_arrItems[nCol];
}

CSkinListCtrl::CSkinListCtrl(void)
{
	m_lpBgImg = NULL;
	m_lpHeadSeparatedImg = NULL;
	m_lpHeadImgN = NULL;
	m_lpHeadImgH = NULL;
	m_lpHeadImgP = NULL;
	m_lpOddItemBgImgN = NULL;
	m_lpEvenItemBgImgN = NULL;
	m_lpItemBgImgH = NULL;
	m_lpItemBgImgS = NULL;

	m_bTransparent = FALSE;
	m_hBgDC = NULL;
	m_bMouseTracking = FALSE;
	m_bHeadVisible = TRUE;
	m_nHeadHeight = 20;
	m_nItemHeight = 20;
	m_szItemImage.cx = 16;
	m_szItemImage.cy = 16;

	m_nPressHeadIndex = -1;
	m_nHoverHeadIndex = -1;

	m_nPressItemIndex = -1;
	m_nHoverItemIndex = -1;
	m_nSelItemIndex = -1;

	m_nLeft = m_nTop = 0;

	m_clrItemText = RGB(0,0,0);
	m_clrSelItemText = RGB(255,255,255);
}

CSkinListCtrl::~CSkinListCtrl(void)
{
	DeleteAllItems();
	DeleteAllColumns();
}

// 设置列表背景图片
BOOL CSkinListCtrl::SetBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImg != NULL) ? TRUE : FALSE;
}

// 设置表头分隔符图片
BOOL CSkinListCtrl::SetHeadSeparatedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadSeparatedImg);
	m_lpHeadSeparatedImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpHeadSeparatedImg != NULL) ? TRUE : FALSE;
}

// 设置表头背景图片(普通状态)
BOOL CSkinListCtrl::SetHeadNormalPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgN);
	m_lpHeadImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpHeadImgN != NULL) ? TRUE : FALSE;
}

// 设置表头背景图片(高亮状态)
BOOL CSkinListCtrl::SetHeadHotPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgH);
	m_lpHeadImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpHeadImgH != NULL) ? TRUE : FALSE;
}

// 设置表头背景图片(按下状态)
BOOL CSkinListCtrl::SetHeadPushedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgP);
	m_lpHeadImgP = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpHeadImgP != NULL) ? TRUE : FALSE;
}

// 设置奇行背景图片(普通状态)
BOOL CSkinListCtrl::SetOddItemBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpOddItemBgImgN);
	m_lpOddItemBgImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpOddItemBgImgN != NULL) ? TRUE : FALSE;
}

// 设置双行背景图片(普通状态)
BOOL CSkinListCtrl::SetEvenItemBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpEvenItemBgImgN);
	m_lpEvenItemBgImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpEvenItemBgImgN != NULL) ? TRUE : FALSE;
}

// 设置行背景图片(高亮状态)
BOOL CSkinListCtrl::SetItemHotBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgH);
	m_lpItemBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpItemBgImgH != NULL) ? TRUE : FALSE;
}

// 设置行背景图片(选中状态)
BOOL CSkinListCtrl::SetItemSelBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgS);
	m_lpItemBgImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpItemBgImgS != NULL) ? TRUE : FALSE;
}

// 设置列表背景是否透明
void CSkinListCtrl::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

// 设置表头是否可见
void CSkinListCtrl::SetHeadVisible(BOOL bVisible)
{
	m_bHeadVisible = bVisible;
}

// 判断表头是否可见
BOOL CSkinListCtrl::IsHeadVisible()
{
	return m_bHeadVisible;
}

// 设置表头高度
void CSkinListCtrl::SetHeadHeight(int nHeight)
{
	m_nHeadHeight = nHeight;
}

// 设置行高度
void CSkinListCtrl::SetItemHeight(int nHeight)
{
	m_nItemHeight = nHeight;
}

// 设置行图像大小
void CSkinListCtrl::SetItemImageSize(int cx, int cy)
{
	m_szItemImage.cx = cx;
	m_szItemImage.cy = cy;
}

// 添加列
int CSkinListCtrl::AddColumn(LPCTSTR lpszText, LPCTSTR lpszImage, DWORD dwFmt, int nWidth)
{
	CLVColumn* lpCol = new CLVColumn;
	if (NULL == lpCol)
		return -1;

	lpCol->m_strText = lpszText;
	lpCol->m_lpImage = CSkinManager::GetInstance()->GetImage(lpszImage);
	lpCol->m_dwFmt = dwFmt;
	lpCol->m_nWidth = nWidth;
	m_arrCols.push_back(lpCol);

	for (int i = 0; i < (int)m_arrRows.size(); i++)
	{
		CLVRow* lpRow = m_arrRows[i];
		if (lpRow != NULL)
			lpRow->AddItem();
	}

	return m_arrCols.size() - 1;
}

// 插入列
int CSkinListCtrl::InsertColumn(int nCol, LPCTSTR lpszText, LPCTSTR lpszImage, DWORD dwFmt, int nWidth)
{
	if (nCol < 0)
		return -1;

	CLVColumn* lpCol = new CLVColumn;
	if (NULL == lpCol)
		return -1;

	lpCol->m_strText = lpszText;
	lpCol->m_lpImage = CSkinManager::GetInstance()->GetImage(lpszImage);
	lpCol->m_dwFmt = dwFmt;
	lpCol->m_nWidth = nWidth;
	m_arrCols.insert(m_arrCols.begin() + nCol, lpCol);

	for (int i = 0; i < (int)m_arrRows.size(); i++)
	{
		CLVRow* lpRow = m_arrRows[i];
		if (lpRow != NULL)
			lpRow->InsertItem(nCol);
	}

	return nCol;
}

// 删除列
BOOL CSkinListCtrl::DeleteColumn(int nCol)
{
	if (nCol < 0 || nCol >= (int)m_arrCols.size())
		return FALSE;

	CLVColumn* lpCol = m_arrCols[nCol];
	if (lpCol != NULL)
	{
		delete lpCol;
		lpCol = NULL;
	}
	m_arrCols.erase(m_arrCols.begin() + nCol);

	for (int i = 0; i < (int)m_arrRows.size(); i++)
	{
		CLVRow* lpRow = m_arrRows[i];
		if (lpRow != NULL)
			lpRow->DeleteItem(nCol);
	}

	return TRUE;
}

// 添加行
int CSkinListCtrl::AddItem(LPCTSTR lpszText, LPCTSTR lpszImage, BOOL bGray, DWORD dwFmt, LPARAM lParam)
{
	int nItemCnt = GetItemCount();
	return InsertItem(nItemCnt, lpszText, lpszImage, bGray, dwFmt, lParam);
}

// 插入行
int CSkinListCtrl::InsertItem(int nItem, LPCTSTR lpszText, LPCTSTR lpszImage, 
							  BOOL bGray,DWORD dwFmt, LPARAM lParam)
{
	int nCols = GetColumnCount();
	if (nCols <= 0)
		return -1;

	CLVRow* lpRow = new CLVRow(nCols);
	if (NULL == lpRow)
		return -1;

	BOOL bRet = lpRow->SetItem(0, lpszText, lpszImage, 
		m_szItemImage.cx, m_szItemImage.cy, bGray, dwFmt, lParam);
	if (!bRet)
	{
		delete lpRow;
		return -1;
	}

	m_arrRows.insert(m_arrRows.begin() + nItem, lpRow);

	if (IsWindowVisible())
		Invalidate();

	return nItem;
}

// 删除行
BOOL CSkinListCtrl::DeleteItem(int nItem)
{
	if (nItem < 0 || nItem >= (int)m_arrRows.size())
		return FALSE;

	CLVRow* lpRow = m_arrRows[nItem];
	if (lpRow != NULL)
	{
		delete lpRow;
		lpRow = NULL;
	}
	m_arrRows.erase(m_arrRows.begin() + nItem);
	if (nItem == m_nSelItemIndex)
		m_nSelItemIndex = -1;
	else if (m_nSelItemIndex > nItem)
		m_nSelItemIndex--;

	//m_nHoverItemIndex = -1;

	if (::IsWindowVisible(m_hWnd))
		Invalidate();
		
	return TRUE;
}

// 获取总列数
int CSkinListCtrl::GetColumnCount()
{
	return (int)m_arrCols.size();
}

// 删除所有列
BOOL CSkinListCtrl::DeleteAllColumns()
{
	DeleteAllItems();
	for (int i = 0; i < (int)m_arrCols.size(); i++)
	{
		CLVColumn* lpCol = m_arrCols[i];
		if (lpCol != NULL)
		{
			delete lpCol;
			lpCol = NULL;
		}
	}
	m_arrCols.clear();
	return TRUE;
}

// 获取总行数
int CSkinListCtrl::GetItemCount()
{
	return (int)m_arrRows.size();
}

// 删除所有行
BOOL CSkinListCtrl::DeleteAllItems()
{
	for (int i = 0; i < (int)m_arrRows.size(); i++)
	{
		CLVRow* lpRow = m_arrRows[i];
		if (lpRow != NULL)
		{
			delete lpRow;
			lpRow = NULL;
		}
	}
	m_arrRows.clear();
	return TRUE;
}

// 设置列文本
BOOL CSkinListCtrl::SetColumnText(int nCol, LPCTSTR lpszText)
{
	CLVColumn* lpCol = GetColumn(nCol);
	if (NULL == lpCol)
		return FALSE;
	lpCol->m_strText = lpszText;
	if (IsWindowVisible())
		Invalidate();
	return TRUE;
}

// 设置列图像
BOOL CSkinListCtrl::SetColumnImage(int nCol, LPCTSTR lpszImage)
{
	CLVColumn* lpCol = GetColumn(nCol);
	if (NULL == lpCol)
		return FALSE;
	CSkinManager::GetInstance()->ReleaseImage(lpCol->m_lpImage);
	lpCol->m_lpImage = CSkinManager::GetInstance()->GetImage(lpszImage);
	if (IsWindowVisible())
		Invalidate();
	return TRUE;
}

// 设置列格式
BOOL CSkinListCtrl::SetColumnFormat(int nCol, DWORD dwFmt)
{
	CLVColumn* lpCol = GetColumn(nCol);
	if (NULL == lpCol)
		return FALSE;
	lpCol->m_dwFmt = dwFmt;
	if (IsWindowVisible())
		Invalidate();
	return TRUE;
}

// 设置列宽度
BOOL CSkinListCtrl::SetColumnWidth(int nCol, int nWidth)
{
	CLVColumn* lpCol = GetColumn(nCol);
	if (NULL == lpCol)
		return FALSE;
	lpCol->m_nWidth = nWidth;
	if (IsWindowVisible())
		Invalidate();
	return TRUE;
}

// 获取列文本
CString CSkinListCtrl::GetColumnText(int nCol)
{
	CLVColumn* lpCol = GetColumn(nCol);
	return (NULL == lpCol) ? _T("") : lpCol->m_strText;
}

// 获取列格式
DWORD CSkinListCtrl::GetColumnFormat(int nCol)
{
	CLVColumn* lpCol = GetColumn(nCol);
	return (NULL == lpCol) ? 0 : lpCol->m_dwFmt;
}

// 获取列宽度
int CSkinListCtrl::GetColumnWidth(int nCol)
{
	CLVColumn* lpCol = GetColumn(nCol);
	return (NULL == lpCol) ? 0 : lpCol->m_nWidth;
}

// 设置行文本
BOOL CSkinListCtrl::SetItemText(int nItem, int nSubItem, LPCTSTR lpszText)
{
	CLVRow* lpRow = GetItem(nItem);
	if (NULL == lpRow)
		return FALSE;
	BOOL bRet = lpRow->SetItemText(nSubItem, lpszText);
	if (bRet)
	{
		if (IsWindowVisible())
			Invalidate();
	}
	return bRet;
}

// 设置行图像
BOOL CSkinListCtrl::SetItemImage(int nItem, int nSubItem, LPCTSTR lpszImage, BOOL bGray/* = FALSE*/)
{
	CLVRow* lpRow = GetItem(nItem);
	if (NULL == lpRow)
		return FALSE;
	BOOL bRet = lpRow->SetItemImage(nSubItem, lpszImage, m_szItemImage.cx, m_szItemImage.cy, bGray);
	if (bRet)
	{
		if (IsWindowVisible())
			Invalidate();
	}
	return bRet;
}

// 设置行格式
BOOL CSkinListCtrl::SetItemFormat(int nItem, int nSubItem, DWORD dwFmt)
{
	CLVRow* lpRow = GetItem(nItem);
	if (NULL == lpRow)
		return FALSE;
	BOOL bRet = lpRow->SetItemFormat(nSubItem, dwFmt);
	if (bRet)
	{
		if (IsWindowVisible())
			Invalidate();
	}
	return bRet;
}

// 设置行数据
BOOL CSkinListCtrl::SetItemData(int nItem, int nSubItem, LPARAM lParam)
{
	CLVRow* lpRow = GetItem(nItem);
	if (NULL == lpRow)
		return FALSE;
	return lpRow->SetItemData(nSubItem, lParam);
}

// 获取行文本
CString CSkinListCtrl::GetItemText(int nItem, int nSubItem)
{
	CLVRow* lpRow = GetItem(nItem);
	return (NULL == lpRow) ? _T("") : lpRow->GetItemText(nSubItem);
}

// 获取行格式
DWORD CSkinListCtrl::GetItemFormat(int nItem, int nSubItem)
{
	CLVRow* lpRow = GetItem(nItem);
	return (NULL == lpRow) ? 0 : lpRow->GetItemFormat(nSubItem);
}

// 获取行数据
LPARAM CSkinListCtrl::GetItemData(int nItem, int nSubItem)
{
	CLVRow* lpRow = GetItem(nItem);
	return (NULL == lpRow) ? 0 : lpRow->GetItemData(nSubItem);
}

//设置当前所选项
void CSkinListCtrl::SetCurSelIndex(int nIndex)
{
	m_nSelItemIndex = nIndex;
}

// 获取当前选中行索引
int CSkinListCtrl::GetCurSelItemIndex()
{
	return m_nSelItemIndex;
}

// 设置行文本颜色(普通状态)
void CSkinListCtrl::SetItemTextColor(COLORREF clr)
{
	m_clrItemText = clr;
}

// 行文本颜色(选中状态)
void CSkinListCtrl::SetSelItemTextColor(COLORREF clr)
{
	m_clrSelItemText = clr;
}

int CSkinListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.Create(m_hWnd, 1, &rc, 1, FALSE, FALSE);

	m_VScrollBar.SetBgNormalPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgHotPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgPushedPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgDisabledPic(_T("ScrollBar\\scrollbar_bkg.png"));

	m_VScrollBar.SetShowLeftUpBtn(TRUE);
	m_VScrollBar.SetLeftUpBtnNormalPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));
	m_VScrollBar.SetLeftUpBtnHotPic(_T("ScrollBar\\scrollbar_arrowup_highlight.png"));
	m_VScrollBar.SetLeftUpBtnPushedPic(_T("ScrollBar\\scrollbar_arrowup_down.png"));
	m_VScrollBar.SetLeftUpBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));

	m_VScrollBar.SetShowRightDownBtn(TRUE);
	m_VScrollBar.SetRightDownBtnNormalPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));
	m_VScrollBar.SetRightDownBtnHotPic(_T("ScrollBar\\scrollbar_arrowdown_highlight.png"));
	m_VScrollBar.SetRightDownBtnPushedPic(_T("ScrollBar\\scrollbar_arrowdown_down.png"));
	m_VScrollBar.SetRightDownBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));

	m_VScrollBar.SetThumbNormalPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbHotPic(_T("ScrollBar\\scrollbar_bar_highlight.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbPushedPic(_T("ScrollBar\\scrollbar_bar_down.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbDisabledPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));

	rc = CRect(0, rcClient.Height() - 14, rcClient.Width(), rcClient.Height());
	m_HScrollBar.Create(m_hWnd, 2, &rc, 2, TRUE, FALSE);

	m_HScrollBar.SetBgNormalPic(_T("ScrollBar\\scrollbar_horz_bkg.png"));
	m_HScrollBar.SetBgHotPic(_T("ScrollBar\\scrollbar_horz_bkg.png"));
	m_HScrollBar.SetBgPushedPic(_T("ScrollBar\\scrollbar_horz_bkg.png"));
	m_HScrollBar.SetBgDisabledPic(_T("ScrollBar\\scrollbar_horz_bkg.png"));

	m_HScrollBar.SetShowLeftUpBtn(TRUE);
	m_HScrollBar.SetLeftUpBtnNormalPic(_T("ScrollBar\\scrollbar_arrowleft_normal.png"));
	m_HScrollBar.SetLeftUpBtnHotPic(_T("ScrollBar\\scrollbar_arrowleft_highlight.png"));
	m_HScrollBar.SetLeftUpBtnPushedPic(_T("ScrollBar\\scrollbar_arrowleft_down.png"));
	m_HScrollBar.SetLeftUpBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowleft_normal.png"));

	m_HScrollBar.SetShowRightDownBtn(TRUE);
	m_HScrollBar.SetRightDownBtnNormalPic(_T("ScrollBar\\scrollbar_arrowright_normal.png"));
	m_HScrollBar.SetRightDownBtnHotPic(_T("ScrollBar\\scrollbar_arrowright_highlight.png"));
	m_HScrollBar.SetRightDownBtnPushedPic(_T("ScrollBar\\scrollbar_arrowright_down.png"));
	m_HScrollBar.SetRightDownBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowright_normal.png"));

	m_HScrollBar.SetThumbNormalPic(_T("ScrollBar\\scrollbar_horzbar_normal.png"), CRect(1,0,1,0));
	m_HScrollBar.SetThumbHotPic(_T("ScrollBar\\scrollbar_horzbar_highlight.png"), CRect(1,0,1,0));
	m_HScrollBar.SetThumbPushedPic(_T("ScrollBar\\scrollbar_horzbar_down.png"), CRect(1,0,1,0));
	m_HScrollBar.SetThumbDisabledPic(_T("ScrollBar\\scrollbar_horzbar_normal.png"), CRect(1,0,1,0));

	m_bMouseTracking = FALSE;

	m_nPressHeadIndex = -1;
	m_nHoverHeadIndex = -1;

	m_nPressItemIndex = -1;
	m_nHoverItemIndex = -1;
	m_nSelItemIndex = -1;

	m_nLeft = m_nTop = 0;

	return 0;
}

BOOL CSkinListCtrl::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSkinListCtrl::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

	CheckScrollBarStatus();

	if (m_bTransparent)
		DrawParentWndBg(MemDC.m_hDC);

	if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())
		m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);

	DrawHeadPart(MemDC.m_hDC);
	DrawItemPart(MemDC.m_hDC);

	m_HScrollBar.OnPaint(MemDC.m_hDC);
	m_VScrollBar.OnPaint(MemDC.m_hDC);
}

void CSkinListCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	if (rcItemPart.PtInRect(point))
	{
		m_nSelItemIndex = ItemHitTest(point);
		EnsureVisible(m_nSelItemIndex);
		Invalidate();

		NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), NM_DBLCLK};	// 鼠标左键双击事件
		::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
	}
}

void CSkinListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	::SetCapture(m_hWnd);

	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	if (rcItemPart.PtInRect(point))
	{
		int nOldSelItemIndex = m_nSelItemIndex;

		m_nSelItemIndex = ItemHitTest(point);
		m_nPressItemIndex = m_nSelItemIndex;
		m_nPressHeadIndex = -1;
		EnsureVisible(m_nSelItemIndex);
		Invalidate();

		if (m_nSelItemIndex != nOldSelItemIndex)
		{
			NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), LVN_ITEMCHANGED};	// 行选择改变事件
			::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
		}
	}
	else
	{
		if (IsHeadVisible())
		{
			CRect rcHeadPart;
			GetHeadPartRect(rcHeadPart);

			if (rcHeadPart.PtInRect(point))
			{
				m_nPressHeadIndex = HeadHitTest(point);
				m_nPressItemIndex = -1;
				Invalidate();
			}
		}
	}

	m_VScrollBar.OnLButtonDown(nFlags, point);
	m_HScrollBar.OnLButtonDown(nFlags, point);
}

void CSkinListCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	HWND hWnd = ::GetCapture();
	if (m_hWnd == hWnd)
		::ReleaseCapture();

	if (m_nPressHeadIndex != -1)
	{
		if (HeadHitTest(point) == m_nPressHeadIndex)
		{
			// 表头点击事件
		}
		m_nPressHeadIndex = -1;
		Invalidate();
	}
	else if (m_nPressItemIndex != -1)
	{
		if (ItemHitTest(point) == m_nPressItemIndex)
		{
			// 行点击事件
		}
		m_nPressItemIndex = -1;
		Invalidate();
	}

	m_VScrollBar.OnLButtonUp(nFlags, point);
	m_HScrollBar.OnLButtonUp(nFlags, point);
}

void CSkinListCtrl::OnRButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	if (rcItemPart.PtInRect(point))
	{
		int nOldSelItemIndex = m_nSelItemIndex;

		m_nSelItemIndex = ItemHitTest(point);
		m_nPressItemIndex = m_nSelItemIndex;
		m_nPressHeadIndex = -1;
		EnsureVisible(m_nSelItemIndex);
		Invalidate();

		NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), NM_RCLICK};	// 鼠标右键单击事件
		::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
	}
}

void CSkinListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
	}

	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	if (rcItemPart.PtInRect(point))
	{
		int nHoverItemIndex = ItemHitTest(point);
		if (nHoverItemIndex != m_nHoverItemIndex)
		{
			m_nHoverItemIndex = nHoverItemIndex;
			Invalidate();
		}
		if (m_nHoverHeadIndex != -1)
		{
			m_nHoverHeadIndex = -1;
			Invalidate();
		}
	}
	else
	{
		if (IsHeadVisible())
		{
			CRect rcHeadPart;
			GetHeadPartRect(rcHeadPart);

			if (rcHeadPart.PtInRect(point) && -1 == m_nPressHeadIndex)
			{
				int nHoverHeadIndex = HeadHitTest(point);
				if (nHoverHeadIndex != m_nHoverHeadIndex)
				{
					m_nHoverHeadIndex = nHoverHeadIndex;
					Invalidate();
				}
				if (m_nHoverItemIndex != -1)
				{
					m_nHoverItemIndex = -1;
					Invalidate();
				}
			}
		}
	}

	m_VScrollBar.OnMouseMove(nFlags, point);
	m_HScrollBar.OnMouseMove(nFlags, point);
}

void CSkinListCtrl::OnMouseLeave()
{
	SetMsgHandled(FALSE);

	m_bMouseTracking = FALSE;
	if (-1 != m_nHoverHeadIndex || -1 != m_nHoverItemIndex)
	{
		m_nHoverHeadIndex = -1;
		m_nHoverItemIndex = -1;
		Invalidate();
	}

	m_VScrollBar.OnMouseLeave();
	m_HScrollBar.OnMouseLeave();
}

void CSkinListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	m_VScrollBar.OnTimer(nIDEvent);
	m_HScrollBar.OnTimer(nIDEvent);
}

void CSkinListCtrl::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
{
	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	int nLineSize = 20;
	int nPageSize = rcItemPart.Width();

	switch (nSBCode)
	{
	case SB_LINELEFT:
		Scroll(-nLineSize, 0);
		break;

	case SB_LINERIGHT:
		Scroll(nLineSize, 0);
		break;

	case SB_PAGELEFT:
		Scroll(-nPageSize, 0);
		break;

	case SB_PAGERIGHT:
		Scroll(nPageSize, 0);
		break;

	case SB_THUMBTRACK:
		Scroll(0, 0);
		break;

	default:
		return;
	}
	Invalidate();
}

void CSkinListCtrl::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
{
	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	int nLineSize = 20;
	int nPageSize = rcItemPart.Height();

	switch (nSBCode)
	{
	case SB_LINEUP:
		Scroll(0, -nLineSize);
		break;

	case SB_LINEDOWN:
		Scroll(0, nLineSize);
		break;

	case SB_PAGEUP:
		Scroll(0, -nPageSize);
		break;

	case SB_PAGEDOWN:
		Scroll(0, nPageSize);
		break;

	case SB_THUMBTRACK:
		Scroll(0, 0);
		break;

	default:
		return;
	}
	Invalidate();
}

BOOL CSkinListCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (!m_VScrollBar.IsVisible())
		return TRUE;

	int nLineSize = 20;

	if (zDelta < 0)		// SB_LINEDOWN
		Scroll(0, nLineSize);
	else	// SB_LINEUP
		Scroll(0, -nLineSize);

	Invalidate();

	return TRUE;
}

void CSkinListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (GetItemCount() <= 0)
		return;

	switch (nChar)
	{
	case VK_UP:	// 向上光标键
		{
			if (0 == m_nSelItemIndex)
				return;

			m_nSelItemIndex--;
		}
		break;

	case VK_DOWN:	// 向下光标键
		{
			if (GetItemCount() - 1 == m_nSelItemIndex)
				return;

			m_nSelItemIndex++;
		}
		break;

	case VK_LEFT:	// 向左光标键
		{

		}
		break;

	case VK_RIGHT:	// 向右光标键
		{

		}
		break;

	case VK_PRIOR:	// PgUp键
		{

		}
		return;

	case VK_NEXT:	// PgDn键
		{
		}
		return;

	case VK_HOME:	// Home键
		{
		}
		break;

	case VK_END:	// End键
		{

		}
		break;

	case VK_RETURN:	// 回车键
		{

		}
		return;

	default:
		return;
	}

	EnsureVisible(m_nSelItemIndex);
	Invalidate();
}

void CSkinListCtrl::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);
	AdjustScrollBarSize();
	Invalidate();
}

UINT CSkinListCtrl::OnGetDlgCode(LPMSG lpMsg)
{
	return DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
}

// LRESULT CSkinListCtrl::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
// {
// 	SetMsgHandled(FALSE);
// 	MSG msg = { m_hWnd, uMsg, wParam, lParam };
// 	if (m_ToolTipCtrl.IsWindow())
// 		m_ToolTipCtrl.RelayEvent(&msg);
// 	return 1;
// }

void CSkinListCtrl::OnDestroy()
{
	m_VScrollBar.Destroy();
	m_HScrollBar.Destroy();

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadSeparatedImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadImgP);
	CSkinManager::GetInstance()->ReleaseImage(m_lpOddItemBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpEvenItemBgImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpItemBgImgS);

	DeleteAllItems();
	DeleteAllColumns();
}

CLVColumn* CSkinListCtrl::GetColumn(int nCol)
{
	if (nCol < 0 || nCol >= (int)m_arrCols.size())
		return NULL;
	else
		return m_arrCols[nCol];
}

CLVRow* CSkinListCtrl::GetItem(int nItem)
{
	if (nItem < 0 || nItem >= (int)m_arrRows.size())
		return NULL;
	else
		return m_arrRows[nItem];
}

void CSkinListCtrl::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinListCtrl::DrawHeadPart(HDC hDC)
{
	if (!IsHeadVisible())
		return;

	CRect rect;
	GetHeadPartRect(rect);

	if (m_lpHeadImgN != NULL)	
		m_lpHeadImgN->Draw2(hDC, rect);

	int cxSeparated = 0, cySeparated = 0;
	if (m_lpHeadSeparatedImg != NULL)
	{
		cxSeparated = m_lpHeadSeparatedImg->GetWidth();
		cySeparated = m_lpHeadSeparatedImg->GetHeight();
	}

	CRect rcCol, rcSeparated, rcText;
	int nLeft = m_nLeft, nTop = 0;
	for (int i = 0; i < (int)m_arrCols.size(); i++)
	{
		CLVColumn* lpCol = m_arrCols[i];
		if (lpCol != NULL)
		{
			rcCol = CRect(nLeft, nTop, nLeft + lpCol->m_nWidth, nTop + m_nHeadHeight);

			if (i == m_nPressHeadIndex)
			{
				if (m_lpHeadImgP != NULL)
					m_lpHeadImgP->Draw2(hDC, rcCol);
			}
			else if (i == m_nHoverHeadIndex)
			{
				if (m_lpHeadImgH != NULL)
					m_lpHeadImgH->Draw2(hDC, rcCol);
			}

			if (m_lpHeadSeparatedImg != NULL)
			{
				if (cySeparated >= m_nHeadHeight)
					rcSeparated = CRect(rcCol.right-cxSeparated, rcCol.top, rcCol.right, rcCol.bottom);
				else
				{
					int x = (m_nHeadHeight - cySeparated + 1) / 2;
					rcSeparated = CRect(rcCol.right-cxSeparated, rcCol.top+x, rcCol.right, rcCol.top+x+cySeparated);
				}
				m_lpHeadSeparatedImg->Draw2(hDC, rcSeparated);
			}

			rcText = CRect(rcCol.left+4, rcCol.top, rcCol.right-4, rcCol.bottom);

			UINT nFormat = lpCol->m_dwFmt | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

			::SetTextColor(hDC, RGB(0, 0, 0));
			int nMode = ::SetBkMode(hDC, TRANSPARENT);
			HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
			if (NULL == hFont)
				hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
			HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
			::DrawText(hDC, lpCol->m_strText, -1, &rcText, nFormat);
			::SelectObject(hDC, hOldFont);
			::SetBkMode(hDC, nMode);

			nLeft += lpCol->m_nWidth;
		}
	}
}

void CSkinListCtrl::DrawItemPart(HDC hDC)
{
	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	RECT rcClip = {0};
	::GetClipBox(hDC, &rcClip);
	HRGN hOldRgn = ::CreateRectRgnIndirect(&rcClip);
	HRGN hRgn = ::CreateRectRgnIndirect(&rcItemPart);
	::ExtSelectClipRgn(hDC, hRgn, RGN_AND);

	int nLeft, nTop = m_nTop + rcItemPart.top;

	CRect rcClient;
	GetClientRect(&rcClient);
	int nItemWidth = rcClient.Width();

	int nStart = ::abs(m_nTop) / m_nItemHeight;
	nTop = (m_nTop + nStart* m_nItemHeight) + rcItemPart.top;

	for (int i = nStart; i < (int)m_arrRows.size(); i++)
	{
		CLVRow* lpRow = m_arrRows[i];
		if (lpRow != NULL)
		{
			nLeft = 0;
			CRect rcItem = CRect(nLeft, nTop, nLeft+nItemWidth, nTop+m_nItemHeight);

			if (!::RectVisible(hDC, &rcItem))
				break;
			//if (::RectVisible(hDC, &rcItem))
			{
				if (i == m_nSelItemIndex)
				{
					if (m_lpItemBgImgS != NULL)
						m_lpItemBgImgS->Draw2(hDC, rcItem);
				}
				else if (i == m_nHoverItemIndex)
				{
					if (m_lpItemBgImgH != NULL)
						m_lpItemBgImgH->Draw2(hDC, rcItem);
				}
				else
				{
					if (i % 2 == 0)
					{
						if (m_lpOddItemBgImgN != NULL)
							m_lpOddItemBgImgN->Draw2(hDC, rcItem);
					}
					else
					{
						if (m_lpEvenItemBgImgN != NULL)
							m_lpEvenItemBgImgN->Draw2(hDC, rcItem);
					}
				}

				nLeft = m_nLeft;
				for (int j = 0; j < (int)lpRow->GetItemCount(); j++)
				{
					CLVColumn* lpCol = m_arrCols[j];
					if (lpCol != NULL)
					{
						CRect rcCell, rcImage, rcText;

						rcCell = CRect(nLeft, nTop, nLeft+lpCol->m_nWidth, nTop+m_nItemHeight);

						Gdiplus::Bitmap* lpImage = lpRow->GetItemImage(j);
						if (lpImage != NULL)
						{
							rcImage = rcCell;
							rcImage.left += 8;
							rcImage.right = rcImage.left + m_szItemImage.cx;

							int x = (rcImage.Height() - m_szItemImage.cy + 1) / 2;
							rcImage.top += x;
							rcImage.bottom = rcImage.top + m_szItemImage.cy;

							Gdiplus::Graphics graphics(hDC);
							graphics.DrawImage(lpImage, rcImage.left, rcImage.top, rcImage.Width(), rcImage.Height());
						}
						
						rcText = rcCell;
						if (lpImage != NULL)
							rcText.left += rcImage.right+2;
						rcText.right -= 4;

						UINT nFormat = lpRow->GetItemFormat(j) | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

						if (i == m_nSelItemIndex)
							::SetTextColor(hDC, m_clrSelItemText);
						else
							::SetTextColor(hDC, m_clrItemText);
						int nMode = ::SetBkMode(hDC, TRANSPARENT);
						HFONT hFont = ::CreateFont( 18,						// nHeight 
													 0,							// nWidth 
													 0,							// nEscapement 
													 0,							// nOrientation 
													 FW_NORMAL,					// nWeight 
													 FALSE,						// bItalic 
													 FALSE,						// bUnderline 
													 0,							// cStrikeOut 
													 DEFAULT_CHARSET,			// nCharSet 
													 OUT_DEFAULT_PRECIS,		// nOutPrecision 
													 CLIP_DEFAULT_PRECIS,		// nClipPrecision 
													 DEFAULT_QUALITY,			// nQuality 
													 DEFAULT_PITCH | FF_SWISS,	// nPitchAndFamily 
													 _T("微软雅黑"));;
						if (NULL == hFont)
							hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
						HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
						::DrawText(hDC, lpRow->GetItemText(j), -1, &rcText, nFormat);
						::SelectObject(hDC, hOldFont);
						::DeleteObject(hFont);
						::SetBkMode(hDC, nMode);

						nLeft += lpCol->m_nWidth;
					}
				}
			}

			nTop += m_nItemHeight;
		}
	}

	::SelectClipRgn(hDC, hOldRgn);
	::DeleteObject(hOldRgn);
	::DeleteObject(hRgn);
}

BOOL CSkinListCtrl::GetHeadPartRect(CRect& rect)
{
	if (!IsHeadVisible())
	{
		rect = CRect(0, 0, 0, 0);
	}
	else
	{
		CRect rcClient;
		GetClientRect(&rcClient);

		rect = CRect(0, 0, rcClient.Width(), m_nHeadHeight);

		if (m_VScrollBar.IsVisible())
		{
			CRect rcVScrollBar;
			m_VScrollBar.GetRect(&rcVScrollBar);
			rect.right -= rcVScrollBar.Width();
		}
	}
	return TRUE;
}

BOOL CSkinListCtrl::GetItemPartRect(CRect& rect)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	if (!IsHeadVisible())
		rect = CRect(0, 0, rcClient.Width(), rcClient.Height());
	else
		rect = CRect(0, m_nHeadHeight, rcClient.Width(), rcClient.Height());

	if (m_VScrollBar.IsVisible())
	{
		CRect rcVScrollBar;
		m_VScrollBar.GetRect(&rcVScrollBar);
		rect.right -= rcVScrollBar.Width();
	}

	if (m_HScrollBar.IsVisible())
	{
		CRect rcHScrollBar;
		m_HScrollBar.GetRect(&rcHScrollBar);
		rect.bottom -= rcHScrollBar.Height();
	}

	return TRUE;
}

BOOL CSkinListCtrl::GetItemRect(int nIndex, CRect& rect)
{
	rect = CRect(0,0,0,0);

	if (nIndex < 0 || nIndex >= GetItemCount())
		return FALSE;

	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	int nLeft = 0, nTop = m_nTop + rcItemPart.top;

	CRect rcClient;
	GetClientRect(rcClient);
	int nItemWidth = rcClient.Width();
	nTop = m_nTop + rcItemPart.top + nIndex*m_nItemHeight;
	rect = CRect(nLeft, nTop, nLeft+nItemWidth, nTop+m_nItemHeight);
	return TRUE;

// 	for (int i = 0; i < (int)m_arrRows.size(); i++)
// 	{
// 		if (i == nIndex)
// 		{
// 			rect = CRect(nLeft, nTop, nLeft+nItemWidth, nTop+m_nItemHeight);
// 			return TRUE;
// 		}
// 		nTop += m_nItemHeight;
// 	}
// 
// 	return FALSE;
}

int CSkinListCtrl::HeadHitTest(POINT pt)
{
	if (!IsHeadVisible())
		return -1;

	CRect rcHeadPart;
	GetHeadPartRect(rcHeadPart);

	if (!rcHeadPart.PtInRect(pt))
		return -1;

	CRect rcCol;
	int nLeft = 0, nTop = 0;

	for (int i = 0; i < (int)m_arrCols.size(); i++)
	{
		CLVColumn* lpCol = m_arrCols[i];
		if (lpCol != NULL)
		{
			rcCol = CRect(nLeft, nTop, nLeft + lpCol->m_nWidth, nTop + m_nHeadHeight);
			if (rcCol.PtInRect(pt))
				return i;

			nLeft += lpCol->m_nWidth;
		}
	}

	return -1;
}

int CSkinListCtrl::ItemHitTest(POINT pt)
{
	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	if (!rcItemPart.PtInRect(pt))
		return -1;
	
	int nLeft = 0, nTop = m_nTop + rcItemPart.top;

	CRect rcClient;
	GetClientRect(&rcClient);
	int nItemWidth = rcClient.Width();

	int nStart = ::abs(m_nTop) / m_nItemHeight;
	nTop = (m_nTop + nStart* m_nItemHeight) + rcItemPart.top;

	for (int i = nStart; i < (int)m_arrRows.size(); i++)
	{
		CRect rcItem = CRect(nLeft, nTop, nLeft+nItemWidth, nTop+m_nItemHeight);
		if (rcItem.PtInRect(pt))
			return i;

		nTop += m_nItemHeight;
		if (nTop > rcClient.bottom)
			break;
	}
	
	return -1;
}

BOOL CSkinListCtrl::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CSkinListCtrl::AdjustScrollBarSize()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	if (m_VScrollBar.IsVisible() && m_HScrollBar.IsVisible())
	{
		CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height()-14);
		m_VScrollBar.SetRect(&rc);

		rc = CRect(0, rcClient.Height()-14, rcClient.Width()-14, rcClient.Height());
		m_HScrollBar.SetRect(&rc);
	}
	else if (m_VScrollBar.IsVisible())
	{
		CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
		m_VScrollBar.SetRect(&rc);
	}
	else if (m_HScrollBar.IsVisible())
	{
		CRect rc = CRect(0, rcClient.Height()-14, rcClient.Width(), rcClient.Height());
		m_HScrollBar.SetRect(&rc);
	}
}

void CSkinListCtrl::CheckScrollBarStatus()
{
	if (!IsWindow())
		return;

	CRect rcItemPart;								// 获取显示视图宽高
	GetItemPartRect(rcItemPart);
	int cxDisplayView = rcItemPart.Width();
	int cyDisplayView = rcItemPart.Height();

	int cxContentView = GetColumnsWidth();			// 获取内容视图宽高
	int cyContentView = GetItemCount()* m_nItemHeight;

	CRect rcVScrollBar;
	m_VScrollBar.GetRect(&rcVScrollBar);
	int cxVScrollBar = rcVScrollBar.Width();

	CRect rcHScrollBar;
	m_HScrollBar.GetRect(&rcHScrollBar);
	int cyHScrollBar = rcHScrollBar.Height();

	BOOL bVisibleH = m_HScrollBar.IsVisible();
	BOOL bVisibleV = m_VScrollBar.IsVisible();
	BOOL bChange2;

	int nnn = 0;
	do 
	{
		nnn++;
		bChange2 = FALSE;

		if (cxContentView > cxDisplayView)
		{
			if (!bVisibleH)
			{
				bVisibleH = TRUE;
				cyDisplayView -= cyHScrollBar;
			}
		}
		else
		{
			if (bVisibleH)
			{
				bVisibleH = FALSE;
				cyDisplayView += cyHScrollBar;
			}
		}

		if (cyContentView > cyDisplayView)
		{
			if (!bVisibleV)
			{
				bVisibleV = TRUE;
				bChange2 = TRUE;
				cxDisplayView -= cxVScrollBar;
			}
		}
		else
		{
			if (bVisibleV)
			{
				bVisibleV = FALSE;
				bChange2 = TRUE;
				cxDisplayView += cxVScrollBar;
			}
		}

		if (bChange2)
			continue;
	} while (0);

	if (cxContentView > cxDisplayView)
	{
		m_HScrollBar.SetScrollRange(cxContentView - cxDisplayView);
		Scroll(0, 0);
		m_HScrollBar.SetVisible(TRUE);
	}
	else
	{
		m_nLeft = 0;
		m_HScrollBar.SetScrollPos(0);
		m_HScrollBar.SetVisible(FALSE);
	}

	if (cyContentView > cyDisplayView)
	{
		m_VScrollBar.SetScrollRange(cyContentView - cyDisplayView);
		Scroll(0, 0);
		m_VScrollBar.SetVisible(TRUE);
	}
	else
	{
		m_nTop = 0;
		m_VScrollBar.SetScrollPos(0);
		m_VScrollBar.SetVisible(FALSE);
	}

	AdjustScrollBarSize();
}

void CSkinListCtrl::Scroll(int cx, int cy)
{
	if (m_HScrollBar.IsVisible() &&
		m_HScrollBar.IsEnabled())
	{
		int nPos = m_HScrollBar.GetScrollPos();
		m_HScrollBar.SetScrollPos(nPos + cx);
		nPos = m_HScrollBar.GetScrollPos();
		m_nLeft = 0 - nPos;
	}

	if (m_VScrollBar.IsVisible() &&
		m_VScrollBar.IsEnabled())
	{
		int nPos = m_VScrollBar.GetScrollPos();
		m_VScrollBar.SetScrollPos(nPos + cy);
		nPos = m_VScrollBar.GetScrollPos();
		m_nTop = 0 - nPos;
	}
}

void CSkinListCtrl::EnsureVisible(int nIndex)
{
	CRect rcItemPart;
	GetItemPartRect(rcItemPart);

	CRect rcItem;
	GetItemRect(nIndex, rcItem);

	if (rcItem.top < rcItemPart.top || rcItem.bottom >= rcItemPart.bottom)
	{
		int cy = 0;
		if (rcItem.top < rcItemPart.top)
			cy = rcItem.top - rcItemPart.top;
		if (rcItem.bottom > rcItemPart.bottom)
			cy = rcItem.bottom - rcItemPart.bottom;
		Scroll(0, cy);
	}
}

int CSkinListCtrl::GetColumnsWidth()
{
	int nWidth = 0;
	for (int i = 0; i < (int)m_arrCols.size(); i++)
	{
		CLVColumn* lpCol = m_arrCols[i];
		if (lpCol != NULL)
		{
			nWidth += lpCol->m_nWidth;
		}
	}
	return nWidth;
}

BOOL CSkinListCtrl::Init()
{
	return TRUE;
}

void CSkinListCtrl::UnInit()
{

}