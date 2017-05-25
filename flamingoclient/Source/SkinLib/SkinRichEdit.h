#pragma once

#include <textserv.h>
#include <richedit.h>
#include "SkinScrollBar.h"

class CSkinRichEdit;

/*
*	TXTEFFECT
*
*	@enum	Defines different background styles control
*/
enum TXTEFFECT {
	TXTEFFECT_NONE = 0,				//@emem	no special backgoround effect
	TXTEFFECT_SUNKEN,				//@emem	draw a "sunken 3-D" look
};


// @doc EXTERNAL 

// ============================  CTxtWinHost  ================================================
// Implement the windowed version of the Plain Text control

/*
*	CTxtWinHost
*	
* 	@class	Text Host for Window's Rich Edit Control implementation class
*
*
*/
class CTxtWinHost : public ITextHost
{
protected:
    static LONG _xWidthSys;
    static LONG _yHeightSys;

protected:
	HWND		_hwnd;					// control window
	HWND		_hwndParent;			// parent window

	ITextServices	*_pserv;			// pointer to Text Services object

	ULONG		_crefs;					// reference count

// Properties

	DWORD		_dwStyle;				// style bits
	DWORD		_dwExStyle;				// extended style bits

	unsigned	_fBorder			:1;	// control has border
	unsigned	_fInBottomless		:1;	// inside bottomless callback
	unsigned	_fInDialogBox		:1;	// control is in a dialog box
	unsigned	_fEnableAutoWordSel	:1;	// enable Word style auto word selection?
	unsigned	_fIconic			:1;	// control window is iconic
	unsigned	_fHidden			:1;	// control window is hidden
	unsigned	_fNotSysBkgnd		:1;	// not using system background color
	unsigned	_fWindowLocked		:1;	// window is locked (no update)
	unsigned	_fRegisteredForDrop	:1; // whether host has registered for drop
	unsigned	_fVisible			:1;	// Whether window is visible or not.
	unsigned	_fResized			:1;	// resized while hidden
	unsigned	_fDisabled			:1;	// Window is disabled.
	unsigned	_fKeyMaskSet		:1;	// if ENM_KEYEVENTS has been set
	unsigned	_fMouseMaskSet		:1;	// if ENM_MOUSEEVENTS has been set
	unsigned	_fScrollMaskSet		:1;	// if ENM_SCROLLEVENTS has been set
	unsigned	_fUseSpecialSetSel	:1; // TRUE = use EM_SETSEL hack to not select
										// empty controls to make dialog boxes work.
	unsigned	_fEmSetRectCalled	:1;	// TRUE - application called EM_SETRECT
	unsigned	_fAccumulateDBC		:1;	// TRUE - need to cumulate ytes from 2 WM_CHAR msgs
										// we are in this mode when we receive VK_PROCESSKEY

	TCHAR		_chPassword;			// Password char. If null, no password
	COLORREF 	_crBackground;			// background color
    RECT        _rcViewInset;           // view rect inset /r client rect

	HIMC		_oldhimc;				// previous IME Context
	DWORD		_usIMEMode;				// mode of IME operation
										// either 0 or ES_SELFIME or ES_NOIME
	LONG		_yInset;
	LONG		_xInset;

	HPALETTE	_hpal;					// Logical palette to use.

	TCHAR		_chLeadByte;			// use when we are in _fAccumulateDBC mode

	RECT m_rcClient;
	CSkinRichEdit* m_lpRichEdit;

protected:
	// Initialization
	BOOL	Init(CSkinRichEdit* lpRichEdit, const CREATESTRUCT*pcs);

	void	ResizeInset();

	void	SetScrollBarsForWmEnable(BOOL fEnable);


	void	OnSetMargins(
				DWORD fwMargin,
				DWORD xLeft,
				DWORD xRight);

	void	SetScrollInfo(
				INT fnBar,
				BOOL fRedraw);

	// helpers
	void*	CreateNmhdr(UINT uiCode, LONG cb);
	void	RevokeDragDrop(void);
	void	RegisterDragDrop(void);
	void	DrawSunkenBorder(HWND hwnd, HDC hdc);
	VOID    OnSunkenWindowPosChanging(HWND hwnd, WINDOWPOS*pwndpos);
	LRESULT OnSize(HWND hwnd, WORD fwSizeType, int nWidth, int nHeight);
	TXTEFFECT TxGetEffects() const;
	HRESULT	OnTxVisibleChange(BOOL fVisible);
	void	SetDefaultInset();
	void	ImmAssociateNULLContext(BOOL fReadOnly);
	BOOL	IsTransparentMode() 
			{
				return (_dwExStyle & WS_EX_TRANSPARENT);
			}


	// Keyboard messages
	LRESULT	OnKeyDown(WORD vKey, DWORD dwFlags);
	LRESULT	OnChar(WORD vKey, DWORD dwFlags);
	
	// System notifications
	void 	OnSysColorChange();
	LRESULT OnGetDlgCode(WPARAM wparam, LPARAM lparam);

	// Other messages
	LRESULT OnGetOptions() const;
	void	OnSetOptions(WORD wOp, DWORD eco);
	void	OnSetReadOnly(BOOL fReadOnly);
	void	OnGetRect(LPRECT prc);
	void	OnSetRect(LPRECT prc, BOOL fNewBehavior);


public:
	CTxtWinHost();
	~CTxtWinHost();
	void Shutdown();

	ITextHost* GetTextHost()
	{
		AddRef();
		return this;
	}

	ITextServices* GetTextServices()
	{
		if (NULL == _pserv)
			return NULL;
		_pserv->AddRef();
		return _pserv;
	}

	// Window creation/destruction
	static 	CTxtWinHost* OnNCCreate(CSkinRichEdit* lpRichEdit, const CREATESTRUCT*pcs);
	static 	void 	OnNCDestroy(CTxtWinHost*ped);
			LRESULT OnCreate(const CREATESTRUCT*pcs);

	// Window proc
	virtual LRESULT	TxWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

 	// Accumulate two WM_CHARs for ANSI DBC 
	BOOL	IsAccumulateDBCMode() { return _fAccumulateDBC; }

	// Set/Clear Cumulate mode
	void	SetAccumulateDBCMode(WORD fSetClearMode) { _fAccumulateDBC = fSetClearMode; }

	// -----------------------------
	//	IUnknown interface
	// -----------------------------

    virtual HRESULT 		_stdcall QueryInterface(REFIID riid, void**ppvObject);
    virtual ULONG 			_stdcall AddRef(void);
    virtual ULONG 			_stdcall Release(void);

	// -----------------------------
	//	ITextHost interface
	// -----------------------------
	//@cmember Get the DC for the host
	virtual HDC 		TxGetDC();

	//@cmember Release the DC gotten from the host
	virtual INT			TxReleaseDC(HDC hdc);
	
	//@cmember Show the scroll bar
	virtual BOOL 		TxShowScrollBar(INT fnBar, BOOL fShow);

	//@cmember Enable the scroll bar
	virtual BOOL 		TxEnableScrollBar (INT fuSBFlags, INT fuArrowflags);

	//@cmember Set the scroll range
	virtual BOOL 		TxSetScrollRange(
							INT fnBar, 
							LONG nMinPos, 
							INT nMaxPos, 
							BOOL fRedraw);

	//@cmember Set the scroll position
	virtual BOOL 		TxSetScrollPos (INT fnBar, INT nPos, BOOL fRedraw);

	//@cmember InvalidateRect
	virtual void		TxInvalidateRect(LPCRECT prc, BOOL fMode);

	//@cmember Send a WM_PAINT to the window
	virtual void 		TxViewChange(BOOL fUpdate);
	
	//@cmember Create the caret
	virtual BOOL		TxCreateCaret(HBITMAP hbmp, INT xWidth, INT yHeight);

	//@cmember Show the caret
	virtual BOOL		TxShowCaret(BOOL fShow);

	//@cmember Set the caret position
	virtual BOOL		TxSetCaretPos(INT x, INT y);

	//@cmember Create a timer with the specified timeout
	virtual BOOL 		TxSetTimer(UINT idTimer, UINT uTimeout);

	//@cmember Destroy a timer
	virtual void 		TxKillTimer(UINT idTimer);

	//@cmember Scroll the content of the specified window's client area
	virtual void		TxScrollWindowEx (
							INT dx, 
							INT dy, 
							LPCRECT lprcScroll, 
							LPCRECT lprcClip,
							HRGN hrgnUpdate, 
							LPRECT lprcUpdate, 
							UINT fuScroll);
	
	//@cmember Get mouse capture
	virtual void		TxSetCapture(BOOL fCapture);

	//@cmember Set the focus to the text window
	virtual void		TxSetFocus();

	//@cmember Establish a new cursor shape
	virtual void 		TxSetCursor(HCURSOR hcur, BOOL fText);

	//@cmember Converts screen coordinates of a specified point to the client coordinates 
	virtual BOOL 		TxScreenToClient (LPPOINT lppt);

	//@cmember Converts the client coordinates of a specified point to screen coordinates
	virtual BOOL		TxClientToScreen (LPPOINT lppt);

	//@cmember Request host to activate text services
	virtual HRESULT		TxActivate( LONG* plOldState );

	//@cmember Request host to deactivate text services
   	virtual HRESULT		TxDeactivate( LONG lNewState );

	//@cmember Retrieves the coordinates of a window's client area
	virtual HRESULT		TxGetClientRect(LPRECT prc);

	//@cmember Get the view rectangle relative to the inset
	virtual HRESULT		TxGetViewInset(LPRECT prc);

	//@cmember Get the default character format for the text
	virtual HRESULT 	TxGetCharFormat(const CHARFORMAT**ppCF );

	//@cmember Get the default paragraph format for the text
	virtual HRESULT		TxGetParaFormat(const PARAFORMAT**ppPF);

	//@cmember Get the background color for the window
	virtual COLORREF	TxGetSysColor(int nIndex);

	//@cmember Get the background (either opaque or transparent)
	virtual HRESULT		TxGetBackStyle(TXTBACKSTYLE*pstyle);

	//@cmember Get the maximum length for the text
	virtual HRESULT		TxGetMaxLength(DWORD*plength);

	//@cmember Get the bits representing requested scroll bars for the window
	virtual HRESULT		TxGetScrollBars(DWORD*pdwScrollBar);

	//@cmember Get the character to display for password input
	virtual HRESULT		TxGetPasswordChar(TCHAR*pch);

	//@cmember Get the accelerator character
	virtual HRESULT		TxGetAcceleratorPos(LONG*pcp);

	//@cmember Get the native size
    virtual HRESULT		TxGetExtent(LPSIZEL lpExtent);

	//@cmember Notify host that default character format has changed
	virtual HRESULT 	OnTxCharFormatChange (const CHARFORMAT* pcf);

	//@cmember Notify host that default paragraph format has changed
	virtual HRESULT		OnTxParaFormatChange (const PARAFORMAT* ppf);

	//@cmember Bulk access to bit properties
	virtual HRESULT		TxGetPropertyBits(DWORD dwMask, DWORD*pdwBits);

	//@cmember Notify host of events
	virtual HRESULT		TxNotify(DWORD iNotify, void*pv);

	// FE Support Routines for handling the Input Method Context
//#ifdef WIN95_IME
	virtual HIMC		TxImmGetContext(void);
	virtual void		TxImmReleaseContext(HIMC himc);
//#endif

	//@cmember Returns HIMETRIC size of the control bar.
	virtual HRESULT		TxGetSelectionBarWidth (LONG*lSelBarWidth);
};


#ifndef DEBUG
#define AttCheckRunTotals(_fCF)
#define AttCheckPFRuns(_fCF)
#endif

#ifdef DBCS
#define ECO_STYLES (ECO_AUTOVSCROLL | ECO_AUTOHSCROLL | ECO_NOHIDESEL | \
						ECO_READONLY | ECO_WANTRETURN | ECO_SAVESEL | \
						ECO_VERTICAL | \
						ECO_SELECTIONBAR)
#else
#define ECO_STYLES (ECO_AUTOVSCROLL | ECO_AUTOHSCROLL | ECO_NOHIDESEL | \
						ECO_READONLY | ECO_WANTRETURN | ECO_SAVESEL | \
						ECO_SELECTIONBAR)
#endif

class CSkinRichEdit : public CWindowImpl<CSkinRichEdit, CWindow>
{
public:
	CSkinRichEdit(void);
	~CSkinRichEdit(void);

	BEGIN_MSG_MAP_EX(CSkinRichEdit)
		MESSAGE_RANGE_HANDLER_EX(WM_CREATE, 0xFFFF, RichEditWndProc)
	END_MSG_MAP()

public:
	void AdjustScrollBarSize();
	void SetTransparent(BOOL bTransparent, HDC hBgDC);
	void DrawParentWndBg(HDC hDC);

	DWORD GetDefaultCharFormat(CHARFORMAT& cf) const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		cf.cbSize = sizeof(CHARFORMAT);
		return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 0, (LPARAM)&cf);
	}

	BOOL SetDefaultCharFormat(CHARFORMAT& cf)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		cf.cbSize = sizeof(CHARFORMAT);
		return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, 0, (LPARAM)&cf);
	}

	DWORD GetEventMask() const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (DWORD)::SendMessage(m_hWnd, EM_GETEVENTMASK, 0, 0L);
	}

	DWORD SetEventMask(DWORD dwEventMask)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (DWORD)::SendMessage(m_hWnd, EM_SETEVENTMASK, 0, dwEventMask);
	}

	BOOL SetAutoURLDetect(BOOL bAutoDetect = TRUE)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return !(BOOL)::SendMessage(m_hWnd, EM_AUTOURLDETECT, bAutoDetect, 0L);
	}

	BOOL SetReadOnly(BOOL bReadOnly = TRUE)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (BOOL)::SendMessage(m_hWnd, EM_SETREADONLY, bReadOnly, 0L);
	}

	BOOL SetCharFormat(CHARFORMAT2& cf, WORD wFlags)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		cf.cbSize = sizeof(CHARFORMAT2);
		return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, (WPARAM)wFlags, (LPARAM)&cf);
	}

	int SetSel(LONG nStartChar, LONG nEndChar)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		CHARRANGE cr = { nStartChar, nEndChar };
		return (int)::SendMessage(m_hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
	}

	int SetSel(CHARRANGE &cr)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (int)::SendMessage(m_hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
	}

	int SetSelAll()
	{
		return SetSel(0, -1);
	}

	void ReplaceSel(LPCTSTR lpszNewText, BOOL bCanUndo = FALSE)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, EM_REPLACESEL, (WPARAM) bCanUndo, (LPARAM)lpszNewText);
	}

	int InsertText(long nInsertAfterChar, LPCTSTR lpstrText, BOOL bCanUndo = FALSE)
	{
		int nRet = SetSel(nInsertAfterChar, nInsertAfterChar);
		ReplaceSel(lpstrText, bCanUndo);
		return nRet;
	}

	int AppendText(LPCTSTR lpstrText, BOOL bCanUndo = FALSE)
	{
		return InsertText(GetWindowTextLength(), lpstrText, bCanUndo);
	}

	DWORD GetSelectionCharFormat(CHARFORMAT& cf) const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		cf.cbSize = sizeof(CHARFORMAT);
		return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 1, (LPARAM)&cf);
	}

	BOOL SetSelectionCharFormat(CHARFORMAT& cf)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		cf.cbSize = sizeof(CHARFORMAT);
		return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
	}

	BOOL SetParaFormat(PARAFORMAT& pf)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		pf.cbSize = sizeof(PARAFORMAT);
		return (BOOL)::SendMessage(m_hWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
	}

	// Clipboard operations
	BOOL Undo()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (BOOL)::SendMessage(m_hWnd, EM_UNDO, 0, 0L);
	}

	void Clear()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, WM_CLEAR, 0, 0L);
	}

	void Copy()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, WM_COPY, 0, 0L);
	}

	void Cut()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, WM_CUT, 0, 0L);
	}

	void Paste()
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, WM_PASTE, 0, 0L);
	}

	void HideSelection(BOOL bHide = TRUE, BOOL bChangeStyle = FALSE)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(m_hWnd, EM_HIDESELECTION, bHide, bChangeStyle);
	}

	void PasteSpecial(UINT uClipFormat, DWORD dwAspect = 0, HMETAFILE hMF = 0)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		REPASTESPECIAL reps = { dwAspect, (DWORD_PTR)hMF };
		::SendMessage(m_hWnd, EM_PASTESPECIAL, uClipFormat, (LPARAM)&reps);
	}

	// OLE support
	IRichEditOle* GetOleInterface() const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		IRichEditOle*pRichEditOle = NULL;
		::SendMessage(m_hWnd, EM_GETOLEINTERFACE, 0, (LPARAM)&pRichEditOle);
		return pRichEditOle;
	}

#if (_RICHEDIT_VER >= 0x0200)
	LONG GetSelText(LPTSTR lpstrBuff) const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (LONG)::SendMessage(m_hWnd, EM_GETSELTEXT, 0, (LPARAM)lpstrBuff);
	}
#else // !(_RICHEDIT_VER >= 0x0200)
	// RichEdit 1.0 EM_GETSELTEXT is ANSI only
	LONG GetSelText(LPSTR lpstrBuff) const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (LONG)::SendMessage(m_hWnd, EM_GETSELTEXT, 0, (LPARAM)lpstrBuff);
	}
#endif // !(_RICHEDIT_VER >= 0x0200)
	
#if defined(_WTL_USE_CSTRING) || defined(__ATLSTR_H__)
	LONG GetSelText(_CSTRING_NS::CString& strText) const
	{
		ATLASSERT(::IsWindow(m_hWnd));

		CHARRANGE cr = { 0, 0 };
		::SendMessage(m_hWnd, EM_EXGETSEL, 0, (LPARAM)&cr);

#if (_RICHEDIT_VER >= 0x0200)
		LONG lLen = 0;
		LPTSTR lpstrText = strText.GetBufferSetLength(cr.cpMax - cr.cpMin);
		if(lpstrText != NULL)
		{
			lLen = (LONG)::SendMessage(m_hWnd, EM_GETSELTEXT, 0, (LPARAM)lpstrText);
			strText.ReleaseBuffer();
		}
#else // !(_RICHEDIT_VER >= 0x0200)
		CTempBuffer<char, _WTL_STACK_ALLOC_THRESHOLD> buff;
		LPSTR lpstrText = buff.Allocate(cr.cpMax - cr.cpMin + 1);
		if(lpstrText == NULL)
			return 0;
		LONG lLen = (LONG)::SendMessage(m_hWnd, EM_GETSELTEXT, 0, (LPARAM)lpstrText);
		if(lLen == 0)
			return 0;

		USES_CONVERSION;
		strText = A2T(lpstrText);
#endif // !(_RICHEDIT_VER >= 0x0200)

		return lLen;
	}
#endif // defined(_WTL_USE_CSTRING) || defined(__ATLSTR_H__)

	WORD GetSelectionType() const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (WORD)::SendMessage(m_hWnd, EM_SELECTIONTYPE, 0, 0L);
	}

	BOOL CanPaste(UINT nFormat = 0) const
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (BOOL)::SendMessage(m_hWnd, EM_CANPASTE, nFormat, 0L);
	}

	COLORREF SetBackgroundColor(COLORREF cr)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (COLORREF)::SendMessage(m_hWnd, EM_SETBKGNDCOLOR, 0, cr);
	}

	COLORREF SetBackgroundColor()   // sets to system background
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (COLORREF)::SendMessage(m_hWnd, EM_SETBKGNDCOLOR, 1, 0);
	}

	BOOL SetOleCallback(IRichEditOleCallback* pCallback)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return (BOOL)::SendMessage(m_hWnd, EM_SETOLECALLBACK, 0, (LPARAM)pCallback);
	}

	ITextHost* GetTextHost()
	{
		if (NULL == m_pTxtWinHost)
			return NULL;
		return m_pTxtWinHost->GetTextHost();
	}

	ITextServices* GetTextServices()
	{
		if (NULL == m_pTxtWinHost)
			return NULL;
		return m_pTxtWinHost->GetTextServices();
	}

private:
	LRESULT RichEditWndProc(UINT msg, WPARAM wparam, LPARAM lparam);
 	int OnCreate(LPCREATESTRUCT lpCreateStruct);

public:
	CSkinScrollBar m_HScrollBar;
	CSkinScrollBar m_VScrollBar;
	CTxtWinHost* m_pTxtWinHost;

private:
	BOOL m_bTransparent;
	HDC m_hBgDC;
};
