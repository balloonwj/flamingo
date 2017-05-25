#pragma once

#include "SkinManager.h"

#define		SKIN_MENU_MAGIC		0x2010		// 皮肤菜单项目信息标识

struct SKIN_MENU_ITEM_INFO					// 皮肤菜单项目信息结构
{
	UINT nMagic;							// 皮肤菜单项目信息标识
	UINT nID;								// 菜单ID
	UINT nState;							// 菜单状态
	UINT nType;								// 类型
	HMENU hSubMenu;							// 子弹菜(即弹出式菜单)句柄
	TCHAR szText[256];						// 菜单项文本
	CImageEx* lpIconImgN;					// 未选中图标
	CImageEx* lpIconImgH;					// 选中图标
};

class CSkinMenu								// 皮肤菜单封装类
{
public:
	CSkinMenu(HMENU hMenu = NULL);
	~CSkinMenu(void);

public:
	BOOL SetBgPic(LPCTSTR lpszLeftBg, LPCTSTR lpszRightBg);	// 设置菜单项背景图片
	BOOL SetSelectedPic(LPCTSTR lpszFileName);		// 设置菜单项选中图片
	BOOL SetSepartorPic(LPCTSTR lpszFileName);		// 设置分隔符图片
	BOOL SetArrowPic(LPCTSTR lpszFileName);			// 设置弹出式菜单箭头图片
	BOOL SetCheckPic(LPCTSTR lpszFileName);			// 设置Check状态图片
	BOOL SetIcon(UINT nItem, BOOL bByPosition, LPCTSTR lpszIconN, LPCTSTR lpszIconH);	// 设置指定菜单项所使用的图标

	void SetTextColor(COLORREF clrText);			// 设置普通菜单文本颜色
	void SetSelTextColor(COLORREF clrSelText);		// 设置选中菜单文本颜色
	void SetGrayedTextColor(COLORREF clrGrayedText);// 设置灰化菜单文本颜色

public:
	BOOL CreateMenu();
	BOOL CreatePopupMenu();
	BOOL LoadMenu(LPCWSTR lpszResourceName);
	BOOL LoadMenu(UINT nIDResource);
	BOOL DestroyMenu();

	void OnInitMenuPopup(HMENU hMenuPopup, UINT nIndex, BOOL bSysMenu);
	void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

	CSkinMenu& operator =(HMENU hMenu);

	BOOL Attach(HMENU hMenu);
	HMENU Detach();

	BOOL DeleteMenu(UINT nPosition, UINT nFlags);
	BOOL TrackPopupMenu(UINT nFlags, int x, int y, HWND hWnd, LPCRECT lpRect = 0);
	BOOL TrackPopupMenuEx(UINT fuFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm);

	BOOL AppendMenu(UINT nFlags, UINT_PTR nIDNewItem = 0,
		LPCTSTR lpszNewItem = NULL);
	UINT CheckMenuItem(UINT nIDCheckItem, UINT nCheck);
	UINT EnableMenuItem(UINT nIDEnableItem, UINT nEnable);
	UINT GetMenuItemCount() const;
	UINT GetMenuItemID(int nPos) const;
	UINT GetMenuState(UINT nID, UINT nFlags);
	int GetMenuString(UINT nIDItem, LPTSTR lpString, int nMaxCount, UINT nFlags);
	CSkinMenu GetSubMenu(int nPos) const;
	BOOL InsertMenu(UINT nPosition, UINT nFlags, 
		UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL);
	BOOL ModifyMenu(UINT nPosition, UINT nFlags, 
		UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL);
	BOOL RemoveMenu(UINT nPosition, UINT nFlags);
	BOOL CheckMenuRadioItem(UINT nIDFirst, UINT nIDLast, UINT nIDItem, UINT nFlags);

	BOOL IsMenu();

private:
	SKIN_MENU_ITEM_INFO* GetMenuItemInfo(UINT nItem, BOOL bByPosition);	// 获取指定菜单项信息数据函数
	BOOL SetMenuItemInfo(UINT nItem, BOOL bByPosition, SKIN_MENU_ITEM_INFO* lpItemInfo);	// 设置指定菜单顶信息数据函数

	BOOL EnableOwnerDraw(HMENU hMenu, BOOL bEnable);			// 使能菜单自绘风格

	SIZE GetTextExtent(LPCTSTR lpszText);						// 获取指定文字的宽高度

	void DrawBg(HDC hDC, CRect& rect);							// 绘制菜单项背景
	void DrawSepartor(HDC hDC, CRect& rect);					// 绘制分隔符
	void DrawSelRect(HDC hDC, CRect& rect);						// 填充选中区域
	void DrawIcon(HDC hDC, CRect& rect, CImageEx* lpIconImg);	// 绘制菜单图标
	void DrawCheckState(HDC hDC, CRect& rect);					// 绘制Check状态
	void DrawText(HDC hDC, CRect& rect, BOOL bGrayed,			// 绘制菜单文本
		BOOL bSelected, BOOL bIsSubMenu, LPCTSTR lpText);	
	void DrawArrow(HDC hDC, CRect& rect);						// 绘制弹出式菜单箭头

public:
	HMENU m_hMenu;							// 菜单句柄

private:
	CImageEx* m_lpBgImgL,* m_lpBgImgR;	// 菜单项背景图片
	CImageEx* m_lpSelectedImg;				// 菜单项选中图片
	CImageEx* m_lpSepartorImg;				// 分隔符图片
	CImageEx* m_lpArrowImg;				// 弹出式菜单箭头图片
	CImageEx* m_lpCheckImg;				// Check状态图片

	COLORREF m_clrText;						// 普通菜单文本颜色
	COLORREF m_clrSelText;					// 选中菜单文本颜色
	COLORREF m_clrGrayedText;				// 灰化菜单文本颜色

	HFONT m_hFont;							// 菜单字体
	int m_nLMargin;							// 左边栏宽度
};