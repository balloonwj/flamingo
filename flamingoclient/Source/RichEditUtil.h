#pragma once

#include <string>
#include "Utils.h"
#include "IImageOle.h"

#if defined(UNICODE) || defined(_UNICODE)
	typedef std::wstring tstring;
#else
	typedef std::string tstring;
#endif

struct ImageInfo
{
	long  nStartPos;
	long  nEndPos;
	TCHAR szPath[MAX_PATH];
};

void RegisterCom_ImageOleCtrl();

IRichEditOle* RichEdit_GetOleInterface(HWND hWnd);
int RichEdit_GetWindowTextLength(HWND hWnd);
int RichEdit_GetWindowText(HWND hWnd, LPTSTR lpszStringBuf, int nMaxCount);
int RichEdit_GetWindowText(HWND hWnd, tstring& strText);
int RichEdit_GetTextRange(HWND hWnd, CHARRANGE* lpchrg, tstring& strText);
DWORD RichEdit_GetDefaultCharFormat(HWND hWnd, CHARFORMAT& cf);
BOOL RichEdit_SetDefaultCharFormat(HWND hWnd, CHARFORMAT& cf);
DWORD RichEdit_GetSelectionCharFormat(HWND hWnd, CHARFORMAT& cf);
BOOL RichEdit_SetSelectionCharFormat(HWND hWnd, CHARFORMAT& cf);
void RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText, BOOL bCanUndo = FALSE);
void RichEdit_GetSel(HWND hWnd, LONG& nStartChar, LONG& nEndChar);
int RichEdit_SetSel(HWND hWnd, LONG nStartChar, LONG nEndChar);

void RichEdit_SetDefFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize,	// 设置默认字体
						 COLORREF clrText, BOOL bBold, BOOL bItalic, 
						 BOOL bUnderLine, BOOL bIsLink);
void RichEdit_SetDefFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize);	// 设置默认字体名称和大小
void RichEdit_SetDefTextColor(HWND hWnd, COLORREF clrText);				// 设置默认字体颜色
void RichEdit_SetDefLinkText(HWND hWnd, BOOL bEnable);					// 设置默认超链接
void RichEdit_SetFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize,		// 设置字体
					  COLORREF clrText, BOOL bBold, BOOL bItalic, 
					  BOOL bUnderLine, BOOL bIsLink);	
void RichEdit_SetFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize);	// 设置字体名称和大小
void RichEdit_SetTextColor(HWND hWnd, COLORREF clrText);				// 设置字体颜色
void RichEdit_SetLinkText(HWND hWnd, BOOL bEnable);						// 设置超链接
BOOL RichEdit_SetStartIndent(HWND hWnd, int nSize);						// 设置左缩进(单位:缇)
BOOL RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId,	// 插入表情图片
						 int nFaceIndex, COLORREF clrBg, BOOL bAutoScale, int nReservedWidth);
void RichEdit_GetText(HWND hWnd, tstring& strText);						// 获取文本
void RichEdit_GetImageInfo(HWND hWnd, std::vector<ImageInfo*>& arrImageInfo);
void RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText, LPCTSTR lpFontName,	// 替换选中文本
						 int nFontSize,	COLORREF clrText, BOOL bBold, BOOL bItalic, 
						 BOOL bUnderLine, BOOL bIsLink, int nStartIndent, BOOL bCanUndo = FALSE);
BOOL RichEdit_GetImageOle(HWND hWnd, POINT pt, IImageOle** pImageOle);
BOOL RichEdit_GetImageOle(HWND hWnd, CRect& rcRect, IImageOle** pImageOle);
int RichEdit_GetCustomPicCount(HWND hWnd);

IRichEditOle* RichEdit_GetOleInterface(ITextServices* pTextServices);
int RichEdit_GetWindowTextLength(ITextServices* pTextServices);
int RichEdit_GetWindowText(ITextServices* pTextServices, LPTSTR lpszStringBuf, int nMaxCount);
int RichEdit_GetWindowText(ITextServices* pTextServices, tstring& strText);
int RichEdit_GetTextRange(ITextServices* pTextServices, CHARRANGE* lpchrg, tstring& strText);
DWORD RichEdit_GetDefaultCharFormat(ITextServices* pTextServices, CHARFORMAT& cf);
BOOL RichEdit_SetDefaultCharFormat(ITextServices* pTextServices, CHARFORMAT& cf);
DWORD RichEdit_GetSelectionCharFormat(ITextServices* pTextServices, CHARFORMAT& cf);
BOOL RichEdit_SetSelectionCharFormat(ITextServices* pTextServices, CHARFORMAT& cf);
void RichEdit_ReplaceSel(ITextServices* pTextServices, LPCTSTR lpszNewText, BOOL bCanUndo = FALSE);
void RichEdit_GetSel(ITextServices* pTextServices, LONG& nStartChar, LONG& nEndChar);
int RichEdit_SetSel(ITextServices* pTextServices, LONG nStartChar, LONG nEndChar);

void RichEdit_SetDefFont(ITextServices* pTextServices, LPCTSTR lpFontName,		// 设置默认字体
						 int nFontSize,	COLORREF clrText, BOOL bBold, 
						 BOOL bItalic, BOOL bUnderLine, BOOL bIsLink);
void RichEdit_SetFont(ITextServices* pTextServices, LPCTSTR lpFontName, int nFontSize,	// 设置字体
					  COLORREF clrText, BOOL bBold, BOOL bItalic, BOOL bUnderLine, BOOL bIsLink);
BOOL RichEdit_SetStartIndent(ITextServices* pTextServices, int nSize);			// 设置左缩进(单位:缇)
BOOL RichEdit_InsertFace(ITextServices*pTextServices, ITextHost*pTextHost,	// 插入表情图片
						 LPCTSTR lpszFileName, int nFaceId,	int nFaceIndex, 
						 COLORREF clrBg, BOOL bAutoScale, int nReservedWidth,  long cxImage=0, long cyImage=0);	//cxImage和cyImage单位为缇
void RichEdit_GetText(ITextServices* pTextServices, tstring& strText);			// 获取文本
void RichEdit_ReplaceSel(ITextServices* pTextServices, LPCTSTR lpszNewText,	// 替换选中文本
						 LPCTSTR lpFontName, int nFontSize,	COLORREF clrText, 
						 BOOL bBold, BOOL bItalic, BOOL bUnderLine, BOOL bIsLink, 
						 int nStartIndent, BOOL bCanUndo = FALSE);
BOOL RichEdit_GetImageOle(ITextServices* pTextServices, POINT pt, IImageOle** pImageOle);
int RichEdit_GetCustomPicCount(ITextServices* pTextServices);
