#if !defined(AFX_ISIMPLEMSG_H__E66A448D_57A8_448B_B78D_E86E8A66F098__INCLUDED_)
#define AFX_ISIMPLEMSG_H__E66A448D_57A8_448B_B78D_E86E8A66F098__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <richedit.h>
#include <richole.h>
#include <textserv.h>

// {C5B3A481-21E3-4cab-8ABD-3C6A85892AD2}
_declspec(selectany) CLSID CLSID_ImageOle = 
{ 0xc5b3a481, 0x21e3, 0x4cab, { 0x8a, 0xbd, 0x3c, 0x6a, 0x85, 0x89, 0x2a, 0xd2 } };

// {E41FE3BB-7218-423d-952E-96E556AEE30F}
_declspec(selectany) IID IID_IImageOle = 
{ 0xe41fe3bb, 0x7218, 0x423d, { 0x95, 0x2e, 0x96, 0xe5, 0x56, 0xae, 0xe3, 0x0f } };

// {89FC1386-91E5-415f-AD8F-04415C15F8F5}
_declspec(selectany) IID IID_IRichEditOleCallback2 = 
{ 0x89fc1386, 0x91e5, 0x415f, { 0xad, 0x8f, 0x04, 0x41, 0x5c, 0x15, 0xf8, 0xf5 } };

#define	EN_PASTE	0x0800	// 粘贴消息

struct NMRICHEDITOLECALLBACK
{
	NMHDR hdr;
	LPCTSTR lpszText;	// 粘贴文本
};

interface __declspec(uuid("E41FE3BB-7218-423d-952E-96E556AEE30F")) 
IImageOle : public IUnknown
{
	STDMETHOD(SetRichEditHwnd)(HWND hWnd) PURE;
	STDMETHOD(GetRichEditHwnd)(HWND*hWnd) PURE;

	STDMETHOD(SetTextServices)(ITextServices*pTextServices) PURE;
	STDMETHOD(GetTextServices)(ITextServices**pTextServices) PURE;

	STDMETHOD(SetTextHost)(ITextHost*pTextHost) PURE;
	STDMETHOD(GetTextHost)(ITextHost**pTextHost) PURE;

	STDMETHOD(SetFaceId)(LONG nFaceId) PURE;
	STDMETHOD(GetFaceId)(LONG*nFaceId) PURE;

	STDMETHOD(SetFaceIndex)(LONG nFaceIndex) PURE;
	STDMETHOD(GetFaceIndex)(LONG*nFaceIndex) PURE;

	STDMETHOD(GetFileName)(BSTR*lpszFileName) PURE;
	STDMETHOD(GetObjectRect)(LPRECT lprcObjectRect) PURE;
	STDMETHOD(GetRawFormat)(GUID* lpGuid) PURE;

	STDMETHOD(SetBgColor)(COLORREF clrBg) PURE;
	STDMETHOD(SetAutoScale)(BOOL bAutoScale, int nReservedWidth) PURE;
	STDMETHOD(OnViewChange)() PURE;
	
	STDMETHOD(LoadFromFile)(BSTR bstrFileName) PURE;
	STDMETHOD(SaveAsFile)(BSTR bstrFileName) PURE;
	STDMETHOD(Destroy)(void) PURE;
};

interface __declspec(uuid("89FC1386-91E5-415f-AD8F-04415C15F8F5")) 
IRichEditOleCallback2 : public IRichEditOleCallback
{
	STDMETHOD(SetNotifyHwnd)(HWND hWnd) PURE;	// 设置通知窗口句柄
	STDMETHOD(SetRichEditHwnd)(HWND hWnd) PURE;	// 设置RichEdit窗口句柄
	STDMETHOD(SetTextServices)(ITextServices*pTextServices) PURE;	// 设置ITextServices接口指针
};

#endif // defined(AFX_ISIMPLEMSG_H__E66A448D_57A8_448B_B78D_E86E8A66F098__INCLUDED_)