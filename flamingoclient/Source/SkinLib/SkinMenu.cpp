#include "stdafx.h"
#include "SkinMenu.h"

CSkinMenu::CSkinMenu(HMENU hMenu/* = NULL*/)
{
	m_hMenu = hMenu;

	m_lpBgImgL = m_lpBgImgR = NULL;
	m_lpSelectedImg = NULL;
	m_lpSepartorImg = NULL;
	m_lpArrowImg = NULL;
	m_lpCheckImg = NULL;

	m_clrText = RGB(0, 0 , 0);
	m_clrSelText = RGB(255, 255, 255);
	m_clrGrayedText = RGB(131, 136, 140);

	m_nLMargin = 28;

	NONCLIENTMETRICS ncm = {0};
	ncm.cbSize = sizeof(ncm);

	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, (PVOID)&ncm, FALSE);

	m_hFont = ::CreateFontIndirect(&ncm.lfMenuFont);
}

CSkinMenu::~CSkinMenu(void)
{
	if (m_hFont != NULL)
	{
		::DeleteObject(m_hFont);
		m_hFont = NULL;
	}
}

BOOL CSkinMenu::SetBgPic(LPCTSTR lpszLeftBg, LPCTSTR lpszRightBg)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgL);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgR);

	m_lpBgImgL = CSkinManager::GetInstance()->GetImage(lpszLeftBg);
	m_lpBgImgR = CSkinManager::GetInstance()->GetImage(lpszRightBg);

	if (NULL == m_lpBgImgL || NULL == m_lpBgImgR)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::SetSelectedPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpSelectedImg);

	m_lpSelectedImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpSelectedImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::SetSepartorPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpSepartorImg);

	m_lpSepartorImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpSepartorImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::SetArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImg);

	m_lpArrowImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpArrowImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::SetCheckPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckImg);

	m_lpCheckImg = CSkinManager::GetInstance()->GetImage(lpszFileName);

	if (NULL == m_lpCheckImg)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::SetIcon(UINT nItem, BOOL bByPosition, LPCTSTR lpszIconN, LPCTSTR lpszIconH)
{
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;

	lpSkinMenuItemInfo = GetMenuItemInfo(nItem, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgN);
		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgH);
		
		lpSkinMenuItemInfo->lpIconImgN = CSkinManager::GetInstance()->GetImage(lpszIconN);
		lpSkinMenuItemInfo->lpIconImgH = CSkinManager::GetInstance()->GetImage(lpszIconH);

		if (lpSkinMenuItemInfo->lpIconImgN != NULL && lpSkinMenuItemInfo->lpIconImgH != NULL)
			return TRUE;
	}

	return FALSE;
}

// 设置普通菜单文本颜色
void CSkinMenu::SetTextColor(COLORREF clrText)
{
	m_clrText = clrText;
}

// 设置选中菜单文本颜色
void CSkinMenu::SetSelTextColor(COLORREF clrSelText)
{
	m_clrSelText = clrSelText;
}

// 设置灰化菜单文本颜色
void CSkinMenu::SetGrayedTextColor(COLORREF clrGrayedText)
{
	m_clrGrayedText = clrGrayedText;
}

BOOL CSkinMenu::CreateMenu()
{
	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		::DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}

	m_hMenu = ::CreateMenu();
	if (!m_hMenu)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::CreatePopupMenu()
{
	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		::DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}

	m_hMenu = ::CreatePopupMenu();
	if (!m_hMenu)
		return FALSE;
	else
		return TRUE;
}

BOOL CSkinMenu::LoadMenu(LPCWSTR lpszResourceName)
{
	HMENU hMenu;
	BOOL bRet;

	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		::DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}

	hMenu = ::LoadMenu(ModuleHelper::GetResourceInstance(), lpszResourceName);
	if (!hMenu)
		return FALSE;

	bRet = EnableOwnerDraw(hMenu, TRUE);
	if (!bRet)
	{
		return FALSE;
	}
	else
	{
		m_hMenu = hMenu;
		return TRUE;
	}
}

BOOL CSkinMenu::LoadMenu(UINT nIDResource)
{
	return LoadMenu(MAKEINTRESOURCE(nIDResource));
}

BOOL CSkinMenu::DestroyMenu()
{
	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		::DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgL);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImgR);
	CSkinManager::GetInstance()->ReleaseImage(m_lpSelectedImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpSepartorImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpCheckImg);
	
	return TRUE;
}

void CSkinMenu::OnInitMenuPopup(HMENU hMenuPopup, UINT nIndex, BOOL bSysMenu)
{
	EnableOwnerDraw(hMenuPopup, TRUE);
}

void CSkinMenu::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	LPMEASUREITEMSTRUCT lpms;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	RECT rcText = {0};
	SIZE sz;

	lpms = lpMeasureItemStruct;
	if (!lpms || lpms->CtlType != ODT_MENU)
		return;

	lpSkinMenuItemInfo = (SKIN_MENU_ITEM_INFO*)lpms->itemData;
	if (NULL == lpSkinMenuItemInfo || lpSkinMenuItemInfo->nMagic != SKIN_MENU_MAGIC)
		return;

	if (lpSkinMenuItemInfo->nType & MFT_SEPARATOR)		// 分隔符
	{
		lpms->itemWidth = 0;
		if (m_lpSepartorImg != NULL && !m_lpSepartorImg->IsNull())
			lpms->itemHeight = m_lpSepartorImg->GetHeight();
		else
			lpms->itemHeight = 3;
	}
	else
	{
		sz = GetTextExtent(lpSkinMenuItemInfo->szText);

		lpms->itemWidth = m_nLMargin + 8 + sz.cx + 8;	// 左边栏宽度+菜单文本左边8像素+菜单文本宽度+菜单文本右边8像素
		lpms->itemHeight = 5 + sz.cy + 5;				// 菜单文本上边5像素+菜单文本高度+菜单文本下边5像素

		if (lpSkinMenuItemInfo->hSubMenu != NULL)		// 如果是子菜单
		{
			if (m_lpArrowImg != NULL && !m_lpArrowImg->IsNull())
				lpms->itemWidth += m_lpArrowImg->GetWidth() + 8;	// 原来的宽度 + 箭头图标宽度 + 箭头图标右边8像素
			else
				lpms->itemWidth += 5 + 8;
		}
	}
}

void CSkinMenu::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	LPDRAWITEMSTRUCT lpds;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	HDC hDC;
	CRect rcItem;
	BOOL bGrayed, bDisabled, bSelected, bChecked;
	BOOL bIsSeparator;
	CImageEx* lpIconImgN,* lpIconImgH;
	HDC hMemDC;
	HBITMAP hMemBmp, hOldBmp;

	lpds = lpDrawItemStruct;
	if (NULL == lpds || lpds->CtlType != ODT_MENU)
		return;

	lpSkinMenuItemInfo = (SKIN_MENU_ITEM_INFO*)lpds->itemData;
	if (NULL == lpSkinMenuItemInfo || lpSkinMenuItemInfo->nMagic != SKIN_MENU_MAGIC)
		return;

	hDC = lpds->hDC;
	rcItem = lpds->rcItem;

	bGrayed = lpds->itemState & ODS_GRAYED;
	bDisabled = lpds->itemState & ODS_DISABLED;
	bSelected = lpds->itemState & ODS_SELECTED;
	//bChecked  = lpds->itemState & ODS_CHECKED;
	bChecked  = lpSkinMenuItemInfo->nState & ODS_CHECKED;

	bIsSeparator = lpSkinMenuItemInfo->nType & MFT_SEPARATOR;

	lpIconImgN = lpSkinMenuItemInfo->lpIconImgN;
	lpIconImgH = lpSkinMenuItemInfo->lpIconImgH;

	rcItem.OffsetRect(-rcItem.left, -rcItem.top);

	hMemDC = ::CreateCompatibleDC(hDC);
	hMemBmp = ::CreateCompatibleBitmap(hDC, rcItem.Width(), rcItem.Height());
	hOldBmp = (HBITMAP)::SelectObject(hMemDC, hMemBmp);

	DrawBg(hMemDC, rcItem);					// 绘制背景

	if (bIsSeparator)						// 绘制分隔符
	{
		DrawSepartor(hMemDC, rcItem);
	}
	else
	{
		if (bSelected && !bGrayed)
			DrawSelRect(hMemDC, rcItem);	// 绘制选中区域

		if (bChecked)						// 绘制Check状态
		{
			DrawCheckState(hMemDC, rcItem);
		}
		else
		{
			if (bSelected)
				DrawIcon(hMemDC, rcItem, lpIconImgH);	// 绘制选中图标
			else
				DrawIcon(hMemDC, rcItem, lpIconImgN);	// 绘制非选中图标
		}

		DrawText(hMemDC, rcItem, bGrayed, bSelected, 
			(BOOL)lpSkinMenuItemInfo->hSubMenu, lpSkinMenuItemInfo->szText);	// 绘制菜单文本

		if (lpSkinMenuItemInfo->hSubMenu != NULL)	// 绘制子菜单的右三角箭头
			DrawArrow(hMemDC, rcItem);
	}

	rcItem = lpds->rcItem;

	::BitBlt(hDC, rcItem.left, rcItem.top, rcItem.Width(), rcItem.Height(), hMemDC, 0, 0, SRCCOPY);

	if (lpSkinMenuItemInfo->hSubMenu != NULL)	// 不让系统绘制弹出式菜单箭头
		::ExcludeClipRect(hDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom);

	::SelectObject(hMemDC, hOldBmp);
	::DeleteObject(hMemBmp);
	::DeleteDC(hMemDC);
}

CSkinMenu& CSkinMenu::operator =(HMENU hMenu)
{
	Attach(hMenu);
	return*this;
}

BOOL CSkinMenu::Attach(HMENU hMenu)
{
	BOOL bRet;

	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		::DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}

	bRet = EnableOwnerDraw(hMenu, TRUE);
	if (!bRet)
	{
		return FALSE;
	}
	else
	{
		m_hMenu = hMenu;
		return TRUE;
	}
}

HMENU CSkinMenu::Detach()
{
	HMENU hMenu = m_hMenu;

	if (m_hMenu != NULL)
	{
		EnableOwnerDraw(m_hMenu, FALSE);
		m_hMenu = NULL;
	}

	return hMenu;
}

BOOL CSkinMenu::DeleteMenu(UINT nPosition, UINT nFlags)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;

	bByPosition = nFlags & MF_BYPOSITION;
	lpSkinMenuItemInfo = GetMenuItemInfo(nPosition, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		if (lpSkinMenuItemInfo->hSubMenu != NULL)
		{
			CSkinMenu SubMenu(lpSkinMenuItemInfo->hSubMenu);
			SubMenu.DeleteMenu(nPosition, nFlags);
		}

		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgN);
		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgH);
		
		delete lpSkinMenuItemInfo;

		SetMenuItemInfo(nPosition, nFlags, NULL);
	}

	return ::DeleteMenu(m_hMenu, nPosition, nFlags);
}

BOOL CSkinMenu::TrackPopupMenu(UINT nFlags, int x, int y, HWND hWnd, LPCRECT lpRect/* = 0*/)
{
	return ::TrackPopupMenu(m_hMenu, nFlags, x, y, 0, hWnd, lpRect);
}

BOOL CSkinMenu::TrackPopupMenuEx(UINT fuFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm)
{
	return ::TrackPopupMenuEx(m_hMenu, fuFlags, x, y, hWnd, lptpm);
}

BOOL CSkinMenu::AppendMenu(UINT nFlags, UINT_PTR nIDNewItem/* = 0*/, 
						   LPCTSTR lpszNewItem/* = NULL*/)
{
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	int nCount;
	BOOL bRet;

	nFlags |= MF_OWNERDRAW;
	bRet = ::AppendMenu(m_hMenu, nFlags, nIDNewItem, lpszNewItem);
	if (bRet)
	{
		lpSkinMenuItemInfo = new SKIN_MENU_ITEM_INFO;
		if (lpSkinMenuItemInfo != NULL)
		{
			memset(lpSkinMenuItemInfo, 0, sizeof(SKIN_MENU_ITEM_INFO));

			lpSkinMenuItemInfo->nMagic = SKIN_MENU_MAGIC;

			if (nFlags & MF_POPUP)				// 弹出菜单
			{
				lpSkinMenuItemInfo->hSubMenu = (HMENU)nIDNewItem;
				if (lpszNewItem != NULL && _tcslen(lpszNewItem) > 0)
				{
					_tcsncpy(lpSkinMenuItemInfo->szText, lpszNewItem, 
						sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
				}
				bRet = EnableOwnerDraw(lpSkinMenuItemInfo->hSubMenu, TRUE);
			}
			else
			{
				if (nFlags & MF_SEPARATOR)		// 分隔符
				{
					lpSkinMenuItemInfo->nID = 0;
					lpSkinMenuItemInfo->nType = MFT_SEPARATOR;
				}
				else							// 菜单项
				{
					lpSkinMenuItemInfo->nID = nIDNewItem;
					if (lpszNewItem != NULL && _tcslen(lpszNewItem) > 0)
					{
						_tcsncpy(lpSkinMenuItemInfo->szText, lpszNewItem, 
							sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
					}
				}
			}

			nCount = ::GetMenuItemCount(m_hMenu);
			bRet = SetMenuItemInfo(nCount - 1, TRUE, lpSkinMenuItemInfo);
		}
	}

	return bRet;
}

UINT CSkinMenu::CheckMenuItem(UINT nIDCheckItem, UINT nCheck)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	UINT nRet = MF_UNCHECKED;

	bByPosition = nCheck & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nIDCheckItem, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		if (nCheck & MF_CHECKED)
		{
			lpSkinMenuItemInfo->nState |= MF_CHECKED;
			nRet = MF_CHECKED;
		}
		else
		{
			lpSkinMenuItemInfo->nState &= ~MF_CHECKED;
			nRet = MF_UNCHECKED;
		}
	}

	return nRet;
	//return ::CheckMenuItem(m_hMenu, nIDCheckItem, nCheck);
}

UINT CSkinMenu::EnableMenuItem(UINT nIDEnableItem, UINT nEnable)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;

	bByPosition = nEnable & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nIDEnableItem, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		if (nEnable & MF_DISABLED)
		{
			lpSkinMenuItemInfo->nState |= MF_DISABLED;
			lpSkinMenuItemInfo->nState &= ~MF_ENABLED;
		}
		else if (nEnable & MF_GRAYED)
		{
			lpSkinMenuItemInfo->nState |= MF_GRAYED;
			lpSkinMenuItemInfo->nState &= ~MF_ENABLED;
		}
		else
		{
			lpSkinMenuItemInfo->nState |= MF_ENABLED;
			lpSkinMenuItemInfo->nState &= ~MF_DISABLED;
			lpSkinMenuItemInfo->nState &= ~MF_GRAYED;
		}
	}

	return ::EnableMenuItem(m_hMenu, nIDEnableItem, nEnable);
}

UINT CSkinMenu::GetMenuItemCount() const
{
	return ::GetMenuItemCount(m_hMenu);
}

UINT CSkinMenu::GetMenuItemID(int nPos) const
{
	return ::GetMenuItemID(m_hMenu, nPos);
}

UINT CSkinMenu::GetMenuState(UINT nID, UINT nFlags)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	UINT nRet = 0;

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nID, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		nRet = lpSkinMenuItemInfo->nState;
	}

	return nRet;
}

int CSkinMenu::GetMenuString(UINT nIDItem, LPTSTR lpString, int nMaxCount, UINT nFlags)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	int nRet = 0;

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nIDItem, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		_tcsncpy(lpString, lpSkinMenuItemInfo->szText, nMaxCount);
		nRet = _tcslen(lpString);
	}

	return nRet;
	//return ::GetMenuString(m_hMenu, nIDItem, lpString, nMaxCount, nFlags);
}

CSkinMenu CSkinMenu::GetSubMenu(int nPos) const
{
	return CSkinMenu(::GetSubMenu(m_hMenu, nPos));
}

BOOL CSkinMenu::InsertMenu(UINT nPosition, UINT nFlags, 
						   UINT_PTR nIDNewItem/* = 0*/, LPCTSTR lpszNewItem/* = NULL*/)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	BOOL bRet;

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;

	nFlags |= MF_OWNERDRAW;
	bRet = ::InsertMenu(m_hMenu, nPosition, nFlags, nIDNewItem, lpszNewItem);
	if (bRet)
	{
		lpSkinMenuItemInfo = new SKIN_MENU_ITEM_INFO;
		if (lpSkinMenuItemInfo != NULL)
		{
			memset(lpSkinMenuItemInfo, 0, sizeof(SKIN_MENU_ITEM_INFO));

			lpSkinMenuItemInfo->nMagic = SKIN_MENU_MAGIC;
			if (nFlags & MF_POPUP)				// 弹出菜单
			{
				lpSkinMenuItemInfo->hSubMenu = (HMENU)nIDNewItem;
				if (lpszNewItem != NULL && _tcslen(lpszNewItem) > 0)
				{
					_tcsncpy(lpSkinMenuItemInfo->szText, lpszNewItem, 
						sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
				}

				bRet = EnableOwnerDraw(lpSkinMenuItemInfo->hSubMenu, TRUE);
			}
			else
			{
				if (nFlags & MF_SEPARATOR)		// 分隔符
				{
					lpSkinMenuItemInfo->nID = 0;
					lpSkinMenuItemInfo->nType = MFT_SEPARATOR;
				}
				else if (nFlags & MF_STRING)	// 菜单项
				{
					lpSkinMenuItemInfo->nID = nIDNewItem;
					if (lpszNewItem != NULL && _tcslen(lpszNewItem) > 0)
					{
						_tcsncpy(lpSkinMenuItemInfo->szText, lpszNewItem, 
							sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
					}
				}
			}

			bRet = SetMenuItemInfo(nPosition, bByPosition, lpSkinMenuItemInfo);
		}
	}

	return bRet;
}

BOOL CSkinMenu::ModifyMenu(UINT nPosition, UINT nFlags, 
						   UINT_PTR nIDNewItem/* = 0*/, LPCTSTR lpszNewItem/* = NULL*/)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	BOOL bRet = FALSE;

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nPosition, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		//SetMenuItemInfo(nPosition, bByPosition, NULL);

		nFlags |= MF_OWNERDRAW;
		bRet = ::ModifyMenu(m_hMenu, nPosition, nFlags, nIDNewItem, lpszNewItem);
		if (bRet)
		{
			if (nFlags & MF_SEPARATOR)		// 分隔符
			{
				lpSkinMenuItemInfo->nID = 0;
				lpSkinMenuItemInfo->nType = MFT_SEPARATOR;
			}
			else							// 菜单项
			{
				lpSkinMenuItemInfo->nID = nIDNewItem;
				if (lpszNewItem != NULL && _tcslen(lpszNewItem) > 0)
				{
					_tcsncpy(lpSkinMenuItemInfo->szText, lpszNewItem, 
						sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
				}
			}

			bRet = SetMenuItemInfo(nPosition, bByPosition, lpSkinMenuItemInfo);
		}
	}

	return bRet;
}

BOOL CSkinMenu::RemoveMenu(UINT nPosition, UINT nFlags)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	MENUITEMINFO stMenuItemInfo = {0};
	WCHAR cText[256] = {0};

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;
	lpSkinMenuItemInfo = GetMenuItemInfo(nPosition, bByPosition);
	if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
	{
		if (lpSkinMenuItemInfo->hSubMenu != NULL)
		{
			CSkinMenu SubMenu(lpSkinMenuItemInfo->hSubMenu);
			SubMenu.RemoveMenu(nPosition, nFlags);
		}

		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgN);
		CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgH);
		
		delete lpSkinMenuItemInfo;

		SetMenuItemInfo(nPosition, bByPosition, NULL);
	}

	return ::RemoveMenu(m_hMenu, nPosition, nFlags);
}

BOOL CSkinMenu::CheckMenuRadioItem(UINT nIDFirst, UINT nIDLast, UINT nIDItem, UINT nFlags)
{
	BOOL bByPosition;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;

	bByPosition = nFlags & MF_BYPOSITION ? TRUE : FALSE;
	for (UINT i = nIDFirst; i <= nIDLast; i++)
	{
		lpSkinMenuItemInfo = GetMenuItemInfo(i, bByPosition);
		if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
		{
			if (i == nIDItem)
				lpSkinMenuItemInfo->nState |= MF_CHECKED;
			else
				lpSkinMenuItemInfo->nState &= ~MF_CHECKED;
		}
	}

	return ::CheckMenuRadioItem(m_hMenu, nIDFirst, nIDLast, nIDItem, nFlags);
}

BOOL CSkinMenu::IsMenu()
{
	return ::IsMenu(m_hMenu);
}

SKIN_MENU_ITEM_INFO* CSkinMenu::GetMenuItemInfo(UINT nItem, BOOL bByPosition)
{
	MENUITEMINFO stMenuItemInfo = {0};
	BOOL bRet;

	stMenuItemInfo.cbSize = sizeof(MENUITEMINFO);
	stMenuItemInfo.fMask = MIIM_DATA;
	bRet = ::GetMenuItemInfo(m_hMenu, nItem, bByPosition, &stMenuItemInfo);
	if (bRet)
		return (SKIN_MENU_ITEM_INFO*)stMenuItemInfo.dwItemData;
	else
		return NULL;
}

BOOL CSkinMenu::SetMenuItemInfo(UINT nItem, BOOL bByPosition, SKIN_MENU_ITEM_INFO* lpItemInfo)
{
	MENUITEMINFO stMenuItemInfo = {0};

	stMenuItemInfo.cbSize = sizeof(MENUITEMINFO);
	stMenuItemInfo.fMask = MIIM_DATA;
	stMenuItemInfo.dwItemData = (DWORD)lpItemInfo;

	return ::SetMenuItemInfo(m_hMenu, nItem, bByPosition, &stMenuItemInfo);
}

BOOL CSkinMenu::EnableOwnerDraw(HMENU hMenu, BOOL bEnable)
{
	MENUITEMINFO stMenuItemInfo;
	SKIN_MENU_ITEM_INFO* lpSkinMenuItemInfo;
	TCHAR szText[256] = {0};
	int nMenuItemCount;
	BOOL bRet;

	nMenuItemCount = ::GetMenuItemCount(hMenu);
	for (int i = 0; i < nMenuItemCount; i++)
	{
		memset(&stMenuItemInfo, 0, sizeof(stMenuItemInfo));
		stMenuItemInfo.cbSize = sizeof(MENUITEMINFO);
		stMenuItemInfo.fMask = MIIM_SUBMENU | MIIM_DATA | MIIM_ID | MIIM_TYPE | MIIM_STATE;
		stMenuItemInfo.dwTypeData = szText;
		stMenuItemInfo.cch = sizeof(szText) / sizeof(TCHAR);
		bRet = ::GetMenuItemInfo(hMenu, i, TRUE, &stMenuItemInfo);

		if (bEnable)
		{
			if (stMenuItemInfo.hSubMenu != NULL)
				EnableOwnerDraw(stMenuItemInfo.hSubMenu, bEnable);

			if (!(stMenuItemInfo.fType & MFT_OWNERDRAW))	// 未起用菜单自绘
			{
				lpSkinMenuItemInfo = (SKIN_MENU_ITEM_INFO*)stMenuItemInfo.dwItemData;
				if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
				{
					CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgN);
					CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgH);
					
					lpSkinMenuItemInfo->nMagic = NULL;

					delete lpSkinMenuItemInfo;
				}

				lpSkinMenuItemInfo = new SKIN_MENU_ITEM_INFO;
				if (lpSkinMenuItemInfo != NULL)
				{
					lpSkinMenuItemInfo->nMagic = SKIN_MENU_MAGIC;
					lpSkinMenuItemInfo->nID = stMenuItemInfo.wID;
					lpSkinMenuItemInfo->nState = stMenuItemInfo.fState;
					lpSkinMenuItemInfo->nType = stMenuItemInfo.fType;
					if (stMenuItemInfo.dwTypeData != NULL && _tcslen(stMenuItemInfo.dwTypeData) > 0)
					{
						_tcsncpy(lpSkinMenuItemInfo->szText, stMenuItemInfo.dwTypeData, 
							sizeof(lpSkinMenuItemInfo->szText) / sizeof(TCHAR));
					}
					lpSkinMenuItemInfo->hSubMenu = stMenuItemInfo.hSubMenu;
					lpSkinMenuItemInfo->lpIconImgN = NULL;
					lpSkinMenuItemInfo->lpIconImgH = NULL;

					stMenuItemInfo.fMask = MIIM_TYPE | MIIM_DATA;
					stMenuItemInfo.fType |= MFT_OWNERDRAW;
					stMenuItemInfo.dwItemData = (DWORD)lpSkinMenuItemInfo;
					bRet = ::SetMenuItemInfo(hMenu, i, TRUE, &stMenuItemInfo);
				}
			}
		}
		else
		{
			if (stMenuItemInfo.hSubMenu != NULL)
				EnableOwnerDraw(stMenuItemInfo.hSubMenu, bEnable);

			lpSkinMenuItemInfo = (SKIN_MENU_ITEM_INFO*)stMenuItemInfo.dwItemData;
			if (lpSkinMenuItemInfo != NULL && SKIN_MENU_MAGIC == lpSkinMenuItemInfo->nMagic)
			{
				CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgN);
				CSkinManager::GetInstance()->ReleaseImage(lpSkinMenuItemInfo->lpIconImgH);
				
				delete lpSkinMenuItemInfo;

				stMenuItemInfo.fMask = MIIM_DATA;
				stMenuItemInfo.dwItemData = NULL;
				bRet = ::SetMenuItemInfo(hMenu, i, TRUE, &stMenuItemInfo);
			}
		}
	}

	return TRUE;
}

SIZE CSkinMenu::GetTextExtent(LPCTSTR lpszText)
{
	HDC hDC;
	RECT rcText = {0};
	SIZE sz = {0};
	HFONT hOldFont;

	if (NULL == lpszText || _tcslen(lpszText) <= 0)
		return sz;

	hDC = ::GetDC(NULL);
	hOldFont = (HFONT)::SelectObject(hDC, m_hFont);
	::DrawText(hDC, lpszText, _tcslen(lpszText), &rcText, DT_SINGLELINE | DT_CALCRECT);
	::SelectObject(hDC, hOldFont);
	::ReleaseDC(NULL, hDC);

	sz.cx = rcText.right - rcText.left;
	sz.cy = rcText.bottom - rcText.top;

	return sz;
}

// 绘制菜单顶背景
void CSkinMenu::DrawBg(HDC hDC, CRect& rect)
{
	if (m_lpBgImgL != NULL && !m_lpBgImgL->IsNull())
	{
		CRect rcLeft(rect);
		rcLeft.right = rcLeft.left + m_nLMargin;
		m_lpBgImgL->Draw(hDC, rcLeft);
	}

	if (m_lpBgImgR != NULL && !m_lpBgImgR->IsNull())
	{
		CRect rcRight(rect);
		rcRight.left += m_nLMargin;
		m_lpBgImgR->Draw(hDC, rcRight);
	}
}

// 绘制分隔符
void CSkinMenu::DrawSepartor(HDC hDC, CRect& rect)
{
	if (m_lpSepartorImg != NULL && !m_lpSepartorImg->IsNull())
	{
		CRect rcSepartor(rect);
		rcSepartor.left += m_nLMargin;
		m_lpSepartorImg->Draw(hDC, rcSepartor);
	}
}

// 填充选中区域
void CSkinMenu::DrawSelRect(HDC hDC, CRect& rect)
{
	if (m_lpSelectedImg != NULL && !m_lpSelectedImg->IsNull())
		m_lpSelectedImg->Draw(hDC, rect);
}

// 绘制菜单图标
void CSkinMenu::DrawIcon(HDC hDC, CRect& rect, CImageEx* lpIconImg)
{
	if (lpIconImg != NULL && !lpIconImg->IsNull())
	{
		int cxIcon = lpIconImg->GetWidth();
		int cyIcon = lpIconImg->GetHeight();

		CRect rcLeft(rect);
		rcLeft.right = rcLeft.left + m_nLMargin;

		int x = (rcLeft.Width() - cxIcon + 1) / 2;
		int y = (rcLeft.Height() - cyIcon + 1) / 2;

		CRect rcIcon(x, y, x+cxIcon, y+cyIcon);
		lpIconImg->Draw(hDC, rcIcon);
	}
}

// 绘制Check状态
void CSkinMenu::DrawCheckState(HDC hDC, CRect& rect)
{
	DrawIcon(hDC, rect, m_lpCheckImg);
}

// 绘制菜单文本
void CSkinMenu::DrawText(HDC hDC, CRect& rect, BOOL bGrayed, 
						 BOOL bSelected, BOOL bIsSubMenu, LPCTSTR lpText)
{
	int nMode;
	COLORREF clrColor;

	CRect rcText(rect);
	rcText.left = m_nLMargin + 8;
	rcText.right -= 8;

	if (bIsSubMenu)
	{
		if (m_lpArrowImg != NULL && !m_lpArrowImg->IsNull())
			rcText.right = rcText.right - m_lpArrowImg->GetWidth() - 8;
		else
			rcText.right = rcText.right - 5 - 8;
	}

	nMode = ::SetBkMode(hDC, TRANSPARENT);

	if (bGrayed)
	{
		clrColor = m_clrGrayedText;
	}
	else
	{
		if (bSelected)
			clrColor = m_clrSelText;
		else
			clrColor = m_clrText;
	}

	::SetTextColor(hDC, clrColor);
	HFONT hOldFont = (HFONT)::SelectObject(hDC, m_hFont);
	::DrawText(hDC, lpText, _tcslen(lpText), &rcText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
	::SelectObject(hDC, hOldFont);

	::SetBkMode(hDC, nMode);
}

// 绘制弹出式菜单箭头
void CSkinMenu::DrawArrow(HDC hDC, CRect& rect)
{
	if (m_lpArrowImg != NULL && !m_lpArrowImg->IsNull())
	{
		int cxArrow = m_lpArrowImg->GetWidth();
		int cyArrow = m_lpArrowImg->GetHeight();

		int x = rect.right - 8 - cxArrow;
		int y = (rect.Height() - cyArrow + 1) / 2;

		CRect rcArrow(x, y, x+cxArrow, y+cyArrow);

		m_lpArrowImg->Draw(hDC, rcArrow);
	}
}