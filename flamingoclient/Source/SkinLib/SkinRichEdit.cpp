#include "stdafx.h"
#include "SkinRichEdit.h"

#ifdef MACPORT //MACPORT change
#include "wlmimm.h"
#else
#include "imm.h"
#endif

#define ibPed 0

LONG CTxtWinHost::_xWidthSys = 0;			// Average char width of system font
LONG CTxtWinHost::_yHeightSys = 0;			// Height of system font

LONG xPerInchScreenDC = 0;
LONG yPerInchScreenDC = 0;

COLORREF crAuto = 0;

INT cxBorder = 0;
INT cyBorder = 0;

BOOL fHaveIMMProcs = FALSE;

INT cyHScroll;
INT cxVScroll;

INT icr3DDarkShadow;		// value to use for COLOR_3DDKSHADOW

LONG dxSelBar = 0;

DWORD dwPlatformId;

#define GET_WM_COMMAND_MPS(id, hwnd, cmd)    \
	(WPARAM)MAKELONG(id, cmd), (LPARAM)(hwnd)
#define VER_PLATFORM_WIN32_MACINTOSH	0x8001

EXTERN_C const IID IID_ITextServices = { // 8d33f740-cf58-11ce-a89d-00aa006cadc5
	0x8d33f740,
	0xcf58,
	0x11ce,
	{0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}
};


void GetSysParms(void)
{
	crAuto		= GetSysColor(COLOR_WINDOWTEXT);
	cxBorder	= GetSystemMetrics(SM_CXBORDER);	// Unsizable window border
	cyBorder	= GetSystemMetrics(SM_CYBORDER);	//  widths
	//cxHScroll	= GetSystemMetrics(SM_CXHSCROLL);	// Scrollbar-arrow bitmap 
	cxVScroll	= GetSystemMetrics(SM_CXVSCROLL);	//  dimensions
	cyHScroll	= GetSystemMetrics(SM_CYHSCROLL);	//
	//cyVScroll	= GetSystemMetrics(SM_CYVSCROLL);	//
//	cxDoubleClk	= GetSystemMetrics(SM_CXDOUBLECLK);
//	cyDoubleClk	= GetSystemMetrics(SM_CYDOUBLECLK);
	//dct			= GetDoubleClickTime();

// 	nScrollInset =
// 		GetProfileIntA( "windows", "ScrollInset", DD_DEFSCROLLINSET );
}

// Convert Himetric along the X axis to X pixels
inline LONG	HimetricXtoDX(LONG xHimetric, LONG xPerInch)
{
	// This formula is rearranged to get rid of the need for floating point
	// arithmetic. The real way to understand the formula is to use 
	// (xHimetric / HIMETRIC_PER_INCH) to get the inches and then multiply
	// the inches by the number of x pixels per inch to get the pixels.
	return (LONG) MulDiv(xHimetric, xPerInch, HIMETRIC_PER_INCH);
}

// Convert Himetric along the Y axis to Y pixels
inline LONG HimetricYtoDY(LONG yHimetric, LONG yPerInch)
{
	// This formula is rearranged to get rid of the need for floating point
	// arithmetic. The real way to understand the formula is to use 
	// (xHimetric / HIMETRIC_PER_INCH) to get the inches and then multiply
	// the inches by the number of y pixels per inch to get the pixels.
	return (LONG) MulDiv(yHimetric, yPerInch, HIMETRIC_PER_INCH);
}

// Convert Pixels on the X axis to Himetric
inline LONG DXtoHimetricX(LONG dx, LONG xPerInch)
{
	// This formula is rearranged to get rid of the need for floating point
	// arithmetic. The real way to understand the formula is to use 
	// (dx / x pixels per inch) to get the inches and then multiply
	// the inches by the number of himetric units per inch to get the
	// count of himetric units.
	return (LONG) MulDiv(dx, HIMETRIC_PER_INCH, xPerInch);
}

// Convert Pixels on the Y axis to Himetric
inline LONG DYtoHimetricY(LONG dy, LONG yPerInch)
{
	// This formula is rearranged to get rid of the need for floating point
	// arithmetic. The real way to understand the formula is to use 
	// (dy / y pixels per inch) to get the inches and then multiply
	// the inches by the number of himetric units per inch to get the
	// count of himetric units.
	return (LONG) MulDiv(dy, HIMETRIC_PER_INCH, yPerInch);
}

BOOL GetIconic(HWND hwnd) 
{
	while(hwnd)
	{
		if(::IsIconic(hwnd))
			return TRUE;
		hwnd = GetParent(hwnd);
	}
	return FALSE;
}

//////////////// CTxtWinHost Creation/Initialization/Destruction ///////////////////////

/*
*	CTxtWinHost::OnCreate (hwnd, pcs)
*
*	@mfunc
*		Static global method to handle WM_NCCREATE message (see remain.c)
*/
CTxtWinHost* CTxtWinHost::OnNCCreate(CSkinRichEdit* lpRichEdit, const CREATESTRUCT*pcs)
{
#if defined DEBUG 
	GdiSetBatchLimit(1);
#endif
	
	CTxtWinHost*phost = new CTxtWinHost();
	if(!phost)
		return NULL;
	
	if (!phost->Init(lpRichEdit, pcs))				// Stores phost in associated
	{											//  window data
		phost->Shutdown();
		delete phost;
		return NULL;
	}
	return phost;
}

/*
*	CTxtWinHost::OnCreate (hwnd, pcs)
*
*	@mfunc
*		Static global method to handle WM_CREATE message
*
*	@devnote
*		phost ptr is stored in window data (GetWindowLong())
*/
void CTxtWinHost::OnNCDestroy(
	CTxtWinHost*phost)
{
	if (phost != NULL)
	{
		phost->Shutdown();
		phost->Release();
	}
}

/*
*	CTxtWinHost::CTxtWinHost()
*
*	@mfunc
*		constructor
*/
CTxtWinHost::CTxtWinHost()
{
	_pserv = NULL;
	_fRegisteredForDrop = FALSE;
	_crefs = 1;	
	_fDisabled = 0;
	_fNotSysBkgnd = 0;
	if ( !_fNotSysBkgnd)
		_crBackground = GetSysColor(COLOR_WINDOW);
	::SetRectEmpty(&m_rcClient);
	m_lpRichEdit = NULL;
}

/*
*	CTxtWinHost::~CTxtWinHost()
*
*	@mfunc
*		destructor
*/
CTxtWinHost::~CTxtWinHost()
{
	if( _pserv )
	{
		Shutdown();
	}
}

/*
*	CTxtWinHost::Shutdown()
*
*	@mfunc	shuts down this object, but doesn't delete memory
*/
void CTxtWinHost::Shutdown()
{	
	RevokeDragDrop();						// Revoke our drop target

	if( _pserv )
	{
		_pserv->OnTxInPlaceDeactivate();
		_pserv->Release();
		_pserv = NULL;
	}

#ifdef MACPORT
	if ( fHaveIMMProcs )
	{
		ImmTerminateForMac();					// terminate IMM for the mac
	}
#endif //MACPORT
}

/*
*	CTxtWinHost::Init (hwnd, pcs)
*
*	@mfunc
*		Initialize this CTxtWinHost
*/
BOOL CTxtWinHost::Init(
	CSkinRichEdit* lpRichEdit,	//@parm Window handle for this control
	const CREATESTRUCT*pcs)	//@parm Corresponding CREATESTRUCT
{
	HDC			hdc;
	HFONT		hfontOld;
	HRESULT		hr;
	IUnknown*	pUnk;
	TEXTMETRIC	tm;

	if( pcs->lpszClass == NULL )
	{
		return FALSE;
	}
	
	m_lpRichEdit = lpRichEdit;
	_hwnd = lpRichEdit->m_hWnd;

 	// Edit controls created without a window are multiline by default
	// so that paragraph formats can be
	_dwStyle = ES_MULTILINE;
	_fHidden = TRUE;
	
	if(pcs)
	{
		_hwndParent = pcs->hwndParent;
		_dwExStyle	= pcs->dwExStyle;
		_dwStyle	= pcs->style;

		// According to the edit control documentation WS_HSCROLL implies that
		// ES_AUTOSCROLL is set and WS_VSCROLL implies that ES_AUTOVSCROLL is
		// set. Here, we make this so.
		if ((_dwStyle & WS_HSCROLL) != 0)
		{
			_dwStyle |= ES_AUTOHSCROLL;
		}

		if ((_dwStyle & WS_VSCROLL) != 0)
		{
			_dwStyle |= ES_AUTOVSCROLL;
		}

		_fBorder = !!(_dwStyle & WS_BORDER);

		if((_dwStyle & ES_SUNKEN) || (_dwExStyle & WS_EX_CLIENTEDGE) )
		{
			_fBorder = TRUE;
		}

		// handle default passwords
		if( (_dwStyle & ES_PASSWORD) )
		{
			_chPassword = TEXT('*');
		}

		// handle default disabled
		if( (_dwStyle & WS_DISABLED) )
		{
			_fDisabled = TRUE;
		}

		// On Win95 ES_SUNKEN and WS_BORDER get mapped to WS_EX_CLIENTEDGE
		if(_fBorder/* && (dwMajorVersion >= VERS4)*/)
        {
			_dwExStyle |= WS_EX_CLIENTEDGE;
			SetWindowLong(_hwnd, GWL_EXSTYLE, _dwExStyle);
        }
	}

	if (0 == _xWidthSys)
	{
		// Init system metrics
		hdc = GetDC(_hwnd);

		if(!hdc)
			return FALSE;

   		hfontOld = (HFONT)SelectObject(hdc, GetStockObject(SYSTEM_FONT));

		if(!hfontOld)
			return FALSE;

		GetTextMetrics(hdc, &tm);
		SelectObject(hdc, hfontOld);

		_xWidthSys = (INT) tm.tmAveCharWidth;
		_yHeightSys = (INT) tm.tmHeight;

		ReleaseDC(_hwnd, hdc);
	}

	_xInset = _xWidthSys / 2;
	_yInset = _yHeightSys / 4;

	// At this point the border flag is set and so is the pixels per inch
	// so we can initalize the inset.
	SetDefaultInset();

	// Create Text Services component
	if(FAILED(CreateTextServices(NULL, this, &pUnk)))
		return FALSE;
    /*typedef HRESULT(_stdcall*CTSFunc)(IUnknown*punkOuter, ITextHost*pITextHost, IUnknown**ppUnk);
    CTSFunc ctsFunc = NULL;
    auto hRiched20 = LoadLibrary(_T("Riched20.dll"));

    if (NULL == hRiched20)
    return FALSE;
    else
    {
    ctsFunc = (CTSFunc)GetProcAddress(hRiched20, "CreateTextServices");

    if (NULL == ctsFunc)
    return FALSE;
    }

    if (FAILED(ctsFunc(NULL, this, &pUnk)))
    return FALSE;

    FreeLibrary(hRiched20);*/

	// Get text services interface
	hr = pUnk->QueryInterface(IID_ITextServices, (void**)&_pserv);

	// Regardless of whether the previous call succeeded or failed, we are
	// done with the private interface.
	pUnk->Release();

	if(FAILED(hr))
		return FALSE;

	// If the class name is "RichEdit", then we need to turn on the
	// RichEdit 1.0 compatibility bit.  Otherwise, we can use the normal
	// version.
	
// #ifndef MACPORT
//     extern ATOM g_atomRICHEDIT_CLASSW;
//     extern ATOM g_atomRICHEDIT_CLASSA;
//     extern ATOM g_atomRICHEDIT_CLASS10A;
//     BOOL bSet = FALSE;
//     if ((int)pcs->lpszClass > ((ATOM)-1)) {
//         if( wcsicmp(pcs->lpszClass, L"RICHEDIT") == 0 ) {
//             bSet = TRUE;
//         }
//     } else {
//         if ((int)pcs->lpszClass == g_atomRICHEDIT_CLASS10A) {
//             bSet = TRUE;
//         }
//     }
//     if (bSet) {
//         ((CTxtEdit*)_pserv)->Set10Mode();
//     }
// #endif

	// Set window text
	if(pcs && pcs->lpszName)
	{
		if(FAILED(_pserv->TxSetText((TCHAR*)pcs->lpszName)))
		{
			_pserv->Release();
			return FALSE;
		}
	}

// 	PARAFORMAT PF;							// If center or right alignment,
// 	if(_dwStyle & (ES_CENTER | ES_RIGHT))	//  tell text services
// 	{
// 		PF.cbSize = sizeof(PARAFORMAT);
// 		PF.dwMask = PFM_ALIGNMENT;
// 		PF.wAlignment = (_dwStyle & ES_RIGHT) ? PFA_RIGHT : PFA_CENTER;
// 		_pserv->TxSendMessage(EM_SETPARAFORMAT, SPF_SETDEFAULT, (LPARAM)&PF, NULL);
// 	}

#ifdef MACPORT
	if ( fHaveIMMProcs )
	{

		if( !ImmInitializeForMac(hinstRE) )
		{
			#ifdef DEBUG
			OutputDebugStringA("Could not register Imm ImmInitializeForMac.\r\n");
			#endif	// DEBUG
		}
	}
#endif //MACPORT


	return TRUE;
}

/*
*	CTxtWinHost::OnCreate (hwnd, pcs)
*
*	@mfunc
*		Handle WM_CREATE message
*
*	@rdesc
*		LRESULT = -1 if failed to in-place activate; else 0
*/
LRESULT CTxtWinHost::OnCreate(
	const CREATESTRUCT*pcs)
{
	RECT rcClient;

	// sometimes, these values are -1 (from windows itself); just treat them
	// as zero in that case
	LONG cy = ( pcs->cy < 0 ) ? 0 : pcs->cy;
	LONG cx = ( pcs->cx < 0 ) ? 0 : pcs->cx;


	rcClient.top = pcs->y;
	rcClient.bottom = rcClient.top + cy;
	rcClient.left = pcs->x;
	rcClient.right = rcClient.left + cx;
	
	// Notify Text Services that we are in place active
	if(FAILED(_pserv->OnTxInPlaceActivate(&rcClient)))
		return -1;

	DWORD dwStyle = GetWindowLong(_hwnd, GWL_STYLE);
	
	// Hide all scrollbars to start
	if(_hwnd && !(_dwStyle & ES_DISABLENOSCROLL))
	{
		SetScrollRange(_hwnd, SB_VERT, 0, 0, TRUE);
		SetScrollRange(_hwnd, SB_HORZ, 0, 0, TRUE);

		dwStyle &= ~(WS_VSCROLL | WS_HSCROLL);
		SetWindowLong(_hwnd, GWL_STYLE, dwStyle);
	}

	if (!(dwStyle & (ES_READONLY | ES_NOOLEDRAGDROP)) )
	{
		// This isn't a read only window or a no drop window,
		// so we need a drop target.
		RegisterDragDrop();
	}

	_usIMEMode = 0;	
	if (dwStyle & ES_NOIME)
		_usIMEMode = ES_NOIME;
	else if (dwStyle & ES_SELFIME)
		_usIMEMode = ES_SELFIME;

	// turn off IME mode if necessary
	if ((dwStyle & ES_READONLY) || (_usIMEMode == ES_NOIME))		 
		ImmAssociateNULLContext (TRUE);
#ifdef MACPORT
	else if ( fHaveIMMProcs )
	{ // will have to switch to proc pointers when we dynamically link.
		HIMC hIMC;
		hIMC	= ImmCreateContext();
		/* hOldIMC =*/ ::ImmAssociateContext(_hwnd, hIMC);
	}
#endif

	return 0;
}


/////////////////////////////////  IUnknown ////////////////////////////////

HRESULT CTxtWinHost::QueryInterface(REFIID riid, void**ppv)
{ 
  	if( IsEqualIID(riid, IID_IUnknown) )
	{
		*ppv = (IUnknown*)this;
	}
	else if( IsEqualIID(riid, IID_ITextHost) )
	{
		*ppv = (ITextHost*)this;
	}
	else
	{
		*ppv = NULL;
	}

	if(*ppv )
	{
		AddRef();
		return NOERROR;
	}

	return E_NOINTERFACE;
}

ULONG CTxtWinHost::AddRef(void)
{
	return ++_crefs;
}

ULONG CTxtWinHost::Release(void)
{
	--_crefs;

	if( _crefs == 0 )
	{
		delete this;
		return 0;
	}

	return _crefs;
}


//////////////////////////////// Activation ////////////////////////////////

//////////////////////////////// Properties ////////////////////////////////


TXTEFFECT CTxtWinHost::TxGetEffects() const
{
	if( (_dwStyle & ES_SUNKEN) || (_dwExStyle & WS_EX_CLIENTEDGE) )
	{
		return TXTEFFECT_SUNKEN;
	}
	else
	{
		return TXTEFFECT_NONE;
	}
}



//////////////////////////// System API wrapper ////////////////////////////



///////////////////////  Windows message dispatch methods  ///////////////////////////////


/*
*	CTxtWinHost::TxWindowProc (hwnd, msg, wparam, lparam)
*
*	@mfunc
*		Handle window messages pertinent to the host and pass others on to
*		text services. Called by RichEditWndProc() in remain.c.
*
*	#rdesc
*		LRESULT = (code processed) ? 0 : 1
*/
LRESULT CTxtWinHost::TxWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	LRESULT	lres = 0;
	HRESULT hr;

	// stabilize ourselves
	AddRef();

	// Handle mouse/keyboard message filter notifications
	if( _fKeyMaskSet || _fMouseMaskSet || _fScrollMaskSet )
	{
		// We may need to fire a MSGFILTER notification.  In
		// the tests below, we check to see if either mouse
		// or key events are hit.  If so, then we fire the msgfilter
		// notification.  The list of events was generated from
		// RichEdit 1.0 sources.

		MSGFILTER msgfltr;

		/* RichEdit Compatibility issue: The following code gets all
		* keyboard and mouse actions, but the RichEdit 1.0 code only got
		* WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_SYSKEYDOWN, WM_SYSKEYUP,
		* WM_MOUSEACTIVATE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE,
		* WM_RBUTTONDBLCLK, WM_RBUTTONDOWN, WM_RBUTTONUP
		*/

		if( (_fKeyMaskSet && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) ||
			(_fMouseMaskSet && ((msg >= WM_MOUSEFIRST && 
				msg <= WM_MOUSELAST) || msg == WM_MOUSEACTIVATE)) ||
			(_fScrollMaskSet &&((msg == WM_HSCROLL) || (msg == WM_VSCROLL))))
		{
			ZeroMemory(&msgfltr, sizeof(MSGFILTER));
			msgfltr.msg = msg;
			msgfltr.wParam = wparam;
			msgfltr.lParam = lparam;

			//
			// The MSDN document on MSGFILTER is wrong, if the
			// send message returns 0 (NOERROR via TxNotify in this
			// case), it means process the event.  Otherwise, return.
			//
			// The documentation states the reverse.
			//
			if( TxNotify(EN_MSGFILTER, &msgfltr) == NOERROR)
			{
				// Since client is allowed to modify the contents of
				// msgfltr, we must use the returned values.
				msg	   = msgfltr.msg;
				wparam = msgfltr.wParam;
				lparam = msgfltr.lParam;
			}
			else
			{
				lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
				goto Exit;
			}
		}
	}

	BOOL bMsgHandled = m_lpRichEdit->m_VScrollBar.WindowProc(msg, wparam, lparam);
	if (bMsgHandled)
		return 0;

	bMsgHandled = m_lpRichEdit->m_HScrollBar.WindowProc(msg, wparam, lparam);
	if (bMsgHandled)
		return 0;

	switch(msg)
	{
	case WM_VSCROLL:
		if (SB_THUMBTRACK == (int)LOWORD(wparam))
		{
			int nPos = m_lpRichEdit->m_VScrollBar.GetScrollPos();
			wparam = MAKEWPARAM(SB_THUMBPOSITION, nPos);
		}
		goto serv;

	case WM_HSCROLL:
		if (SB_THUMBTRACK == (int)LOWORD(wparam))
		{
			int nPos = m_lpRichEdit->m_HScrollBar.GetScrollPos();
			wparam = MAKEWPARAM(SB_THUMBPOSITION, nPos);
		}
		goto serv;

    case EM_SETEVENTMASK:
		_fKeyMaskSet = !!(lparam & ENM_KEYEVENTS);
		_fMouseMaskSet = !!(lparam & ENM_MOUSEEVENTS);
		_fScrollMaskSet = !!(lparam & ENM_SCROLLEVENTS);
		goto serv;

        break;

	case EM_SETSEL:

		// When we are in a dialog box that is empty, EM_SETSEL will not select
		// the final always existing EOP if the control is rich.
// 		if (_fUseSpecialSetSel
// 			&& (((CTxtEdit*)_pserv)->GetAdjustedTextLength() == 0)
// 			&& (wparam != (WPARAM) -1))
// 		{
// 			lparam = 0;
// 
// 			if (wparam != 0)
// 			{
// 				wparam = 0;
// 			}
// 		}

		goto serv;

	case WM_CREATE:
		lres = OnCreate((CREATESTRUCT*)lparam);
		break;
	
	case WM_KEYDOWN:
		if ( (WORD) wparam == VK_PROCESSKEY )
		{
			// get ready to cumulate the DBC coming via 2 WM_CHAR messages
			SetAccumulateDBCMode ( TRUE );
			_chLeadByte = 0;
		}
		else
		{
			lres = OnKeyDown((WORD) wparam, (DWORD) lparam);
		}

		if(lres)							// Didn't process code:
			goto serv;						//  give it to text services
		
		break;		   

	case WM_CHAR:

		// check if we are waiting for 2 consecutive WM_CHAR messages
		if ( IsAccumulateDBCMode() )
		{
			if ( !_chLeadByte )
			{
				// This is the first WM_CHAR message, 

				if ( !IsDBCSLeadByte ( (BYTE)(WORD)wparam ) )
					// this is not a Lead byte, terminate this special mode for any other message					
					SetAccumulateDBCMode ( FALSE );
				else
				{
					// save the Lead Byte and don't process this message
					_chLeadByte = (WORD)wparam << 8 ;
					goto Exit;
				}
			}
			else
			{
				// This is the second WM_CHAR message,
				// combine the current byte with previous byte.
				// This DBC will be handled as WM_IME_CHAR.
				wparam |= _chLeadByte;
				msg = WM_IME_CHAR;

				// terminate this special mode
				SetAccumulateDBCMode ( FALSE );
				goto serv;						//  give it to text services
			}
		}

		lres = OnChar((WORD) wparam, (DWORD) lparam);
		if(lres)							// Didn't process code:
			goto serv;						//  give it to text services
		break;

	case WM_ENABLE:

		if ((wparam == 0) != (_fDisabled != 0))
		{
			// Stated of window changed so invalidate it so it will
			// get redrawn.
			InvalidateRect(_hwnd, NULL, TRUE);
			SetScrollBarsForWmEnable(wparam);
		}

		// Set the disabled flag
		_fDisabled = (0 == wparam);

		// Return value for message
		lres = 0;

	case WM_SYSCOLORCHANGE:
		OnSysColorChange();
		goto serv;							// Notify text services that
											//  system colors have changed
	case WM_GETDLGCODE:
		lres = OnGetDlgCode(wparam, lparam);
		break;

	case EM_GETOPTIONS:
		lres = OnGetOptions();
		break;

	case EM_GETPASSWORDCHAR:
		lres = _chPassword;
		break;

	case EM_GETRECT:
		OnGetRect((LPRECT)lparam);
		break;

	case EM_HIDESELECTION:
		if((BOOL)lparam)
		{
			DWORD dwPropertyBits = 0;

			if((BOOL)wparam)
			{
				_dwStyle &= ~(DWORD) ES_NOHIDESEL;
				dwPropertyBits = TXTBIT_HIDESELECTION;
			}
			else
				_dwStyle |= ES_NOHIDESEL;

			// Notify text services of change in status.
			_pserv->OnTxPropertyBitsChange(TXTBIT_HIDESELECTION, 
				dwPropertyBits);
		}
		goto serv;

	case EM_SETBKGNDCOLOR:
		lres = (LRESULT) _crBackground;
		_fNotSysBkgnd = !wparam;
		_crBackground = (COLORREF) lparam;

		if(wparam)
			_crBackground = GetSysColor(COLOR_WINDOW);

		if(lres != (LRESULT) _crBackground)
		{
			// Notify text services that color has changed
			LRESULT	lres1 = 0;
			_pserv->TxSendMessage(WM_SYSCOLORCHANGE, 0, 0, &lres1);
			TxInvalidateRect(NULL, TRUE);
		}

		break;

    case WM_STYLECHANGING:
		// Just pass this one to the default window proc
		lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
		break;

	case WM_STYLECHANGED:
		//
		// For now, we only interested in GWL_EXSTYLE Transparent mode changed.
		// This is to fix Bug 753 since Window95 is not passing us
		// the WS_EX_TRANSPARENT.
		// 
		lres = 1;
		if (GWL_EXSTYLE == wparam)
		{
			LPSTYLESTRUCT	lpss = (LPSTYLESTRUCT) lparam;
			if ( IsTransparentMode() != (BOOL)(lpss->styleNew & WS_EX_TRANSPARENT) )
			{
				_dwExStyle = lpss->styleNew;
				//((CTxtEdit*)_pserv)->OnTxBackStyleChange(TRUE);
				_pserv->OnTxPropertyBitsChange(TXTBIT_BACKSTYLECHANGE, 0);

				// return 0 to indicate we have handled this
				// message
				lres = 0;
			}
		}
		break;

	case EM_SETOPTIONS:
		OnSetOptions((WORD) wparam, (DWORD) lparam);
		lres = (_dwStyle & ECO_STYLES);
		if(_fEnableAutoWordSel)
			lres |= ECO_AUTOWORDSELECTION;
		break;

	case EM_SETPASSWORDCHAR:
		if(_chPassword != (TCHAR)wparam)
		{
			_chPassword = (TCHAR)wparam;
			_pserv->OnTxPropertyBitsChange(TXTBIT_USEPASSWORD,
				_chPassword ? TXTBIT_USEPASSWORD : 0);
		}
		break;

	case EM_SETREADONLY:
		ImmAssociateNULLContext (BOOL(wparam) || (_usIMEMode == ES_NOIME));
		OnSetReadOnly(BOOL(wparam));
		lres = 1;
		break;

	case EM_SETRECTNP:						// Can implement: identical to
		break;								//  EM_SETRECT, except that edit
											//  control window isn't redrawn
	case EM_SETRECT:
		OnSetRect((LPRECT)lparam, wparam == 1);
		break;
		
	case WM_SIZE:
		_pserv->TxSendMessage(msg, wparam, lparam, &lres);
		lres = OnSize(hwnd, wparam, LOWORD(lparam), HIWORD(lparam));
		break;

	case WM_WINDOWPOSCHANGING:
#ifdef DBCS
		dwStyleOrg = OnRemoveFeStyles();
		lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
		if (dwStyleOrg!=0xffffffff)
			SetWindowLong(_hwnd, GWL_STYLE, dwStyleOrg);
#else
		lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
#endif						// not DBCS
		if(TxGetEffects() == TXTEFFECT_SUNKEN)
			OnSunkenWindowPosChanging(hwnd, (WINDOWPOS*) lparam);
		break;

	case WM_SETCURSOR:
		//			Only set cursor when over us rather than a child; this
		//			helps prevent us from fighting it out with an inplace child
		if((HWND)wparam == hwnd)
		{
			//TODO: 如果不注释这一行，光标会不停地闪烁，为什么？？
			//if(!(lres = ::DefWindowProc(hwnd, msg, wparam, lparam)))
			//{
				POINT pt;
				GetCursorPos(&pt);
				::ScreenToClient(hwnd, &pt);
				_pserv->OnTxSetCursor(
					DVASPECT_CONTENT,	
					-1,
					NULL,
					NULL,
					NULL,
					NULL,
					NULL,			// Client rect - no redraw 
					pt.x, 
					pt.y);
				lres = TRUE;
			//}
		}
		break;

	case WM_SHOWWINDOW:
		hr = OnTxVisibleChange((BOOL)wparam);
		break;

	case WM_NCPAINT:

#ifdef DBCS
		dwStyleOrg = RemoveFeStyle();
		lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
		if (dwStyleOrg!=0xffffffff)
			SetWindowLong(hwnd, GWL_STYLE, dwStyleOrg);
#else
		lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
#endif					// NOT DBCS
		if ((TxGetEffects() == TXTEFFECT_SUNKEN) 
			/*&& (dwMajorVersion < VERS4)*/)
		{
			HDC hdc = GetDC(hwnd);

			if(hdc)
			{
				DrawSunkenBorder(hwnd, hdc);
				ReleaseDC(hwnd, hdc);
			}
		}
		break;

	case WM_PAINT:
		{
			//PAINTSTRUCT ps;
			HPALETTE hpalOld = NULL;
			//HDC hdc = BeginPaint(hwnd, &ps);
			RECT rcClient = m_rcClient;

			//GetClientRect(hwnd, &rcClient);
 			CPaintDC PaintDC(hwnd);
 			CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

// #ifdef DEBUG
// 			// We only want to paint if we are not asserting because
// 			// the assert might be in the paint code itself.
// 			if (!fInAssert)
// 			{
// #endif // DEBUG

			// Set up the palette for drawing our data
			if (_hpal != NULL)
			{
				hpalOld = SelectPalette(MemDC.m_hDC, _hpal, FALSE);
				RealizePalette(MemDC.m_hDC);
			}

			// Since we are using the CS_PARENTDC style, make sure
			// the clip region is limited to our client window.
			//GetClientRect(hwnd, &rcClient);
			SaveDC(MemDC.m_hDC);
			IntersectClipRect(MemDC.m_hDC, rcClient.left, rcClient.top, rcClient.right,
				rcClient.bottom);

			if (IsTransparentMode())
			{
				if (m_lpRichEdit != NULL)
					m_lpRichEdit->DrawParentWndBg(MemDC.m_hDC);
			}

			TxGetClientRect(&rcClient);

			_pserv->TxDraw(
				DVASPECT_CONTENT,  		// Draw Aspect
				-1,						// Lindex
				NULL,					// Info for drawing optimazation
				NULL,					// target device information
				MemDC.m_hDC,			// Draw device HDC
				NULL, 				   	// Target device HDC
				(const RECTL*) &rcClient,// Bounding client rectangle
				NULL, 					// Clipping rectangle for metafiles
				&PaintDC.m_ps.rcPaint,	// Update rectangle
				NULL, 	   				// Call back function
				NULL,					// Call back parameter
				TXTVIEW_ACTIVE);		// What view - the active one!

			m_lpRichEdit->m_VScrollBar.OnPaint(MemDC.m_hDC);
			m_lpRichEdit->m_HScrollBar.OnPaint(MemDC.m_hDC);

			// Restore the palette if there is one.
			if (hpalOld != NULL)
			{
				SelectPalette(MemDC.m_hDC, hpalOld, TRUE);
			}

			if ((TxGetEffects() == TXTEFFECT_SUNKEN)
				/*&& (dwMajorVersion < VERS4)*/)
				DrawSunkenBorder(hwnd, MemDC.m_hDC);

// #ifdef DEBUG
// 			// See above comment
// 			}
// #endif // DEBUG

			RestoreDC(MemDC.m_hDC, -1);

			//EndPaint(hwnd, &ps);
		}
		break;

	case EM_SETMARGINS:

		OnSetMargins(wparam, LOWORD(lparam), HIWORD(lparam));
		break;

	case EM_SETPALETTE:

		// Application is setting a palette for us to use.
		_hpal = (HPALETTE) wparam;

		// Invalidate the window & repaint to reflect the new palette.
		InvalidateRect(hwnd, NULL, FALSE);

		break;

	default:
serv:
		{
			if (_usIMEMode == ES_SELFIME)
			{
				// host is doing IME, ignore IME related messages.
				switch(msg)
				{
				case WM_IME_STARTCOMPOSITION:	// IME has begun interpreting user input
				case WM_IME_COMPOSITION:		// State of user's input has changed
				case WM_IME_ENDCOMPOSITION:		// User has OK'd IME conversions
				case WM_IME_NOTIFY:				// Candidate window state change info, etc.
				case WM_IME_COMPOSITIONFULL:	// Level 2 comp string about to overflow.
					lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
					goto Exit;
				}
			}

			hr = _pserv->TxSendMessage(msg, wparam, lparam, &lres);

			if (hr == S_FALSE)
			{
				// Message was not processed by text services so send it
				// to the default window proc.
				lres = ::DefWindowProc(hwnd, msg, wparam, lparam);
			}
		}
	}

	// Special border processing. The inset changes based on the size of the
	// defautl character set. So if we got a message that changes the default
	// character set, we need to update the inset.
	if (_fBorder && ((msg == WM_SETFONT) 
		|| ((msg == EM_SETCHARFORMAT) && (wparam == 0))))
	{
		ResizeInset();
	}

Exit:
	Release();

	return lres;
}
	

///////////////////////////////  Keyboard Messages  //////////////////////////////////

/*
*	CTxtWinHost::OnKeyDown (vkey, dwFlags)
*
*	@mfunc
*		Handle WM_KEYDOWN messages that need to send a message to the parent
*		window (may happen when control is in a dialog box)
*
*	#rdesc
*		LRESULT = (code processed) ? 0 : 1
*/
LRESULT CTxtWinHost::OnKeyDown(
	WORD	vkey,			//@parm WM_KEYDOWN wparam (virtual key code)
	DWORD	dwFlags)		//@parm WM_KEYDOWN flags
{
	if(!_fInDialogBox) 					// Not in a dialog box
		return 1;						// Signal key-down msg not processed

	switch(vkey)
	{
	case VK_ESCAPE:
		PostMessage(_hwndParent, WM_CLOSE, 0, 0);
		return 0;
	
	case VK_RETURN:
		if(!(GetKeyState(VK_CONTROL) & 0x8000) && !(_dwStyle & ES_WANTRETURN))
		{
			// Send to default button
			HWND	hwndT;
			LRESULT id = SendMessage(_hwndParent, DM_GETDEFID, 0, 0);

			if(LOWORD(id) &&
				(hwndT = GetDlgItem(_hwndParent, LOWORD(id))))
			{
				SendMessage(_hwndParent, WM_NEXTDLGCTL, (WPARAM) hwndT, (LPARAM) 1);
				if(GetFocus() != _hwnd)
					PostMessage(hwndT, WM_KEYDOWN, (WPARAM) VK_RETURN, 0);
			}
			return 0;
		}
		break;

	case VK_TAB:
		if( !(GetKeyState(VK_CONTROL) & 0x8000) )
		{
			SendMessage(_hwndParent, WM_NEXTDLGCTL, 
								!!(GetKeyState(VK_SHIFT) & 0x8000), 0);
			return 0;
		}
		break;
	}

	return 1;
}

/*
*	CTxtWinHost::OnChar (vkey, dwFlags)
*
*	@mfunc
*		Eat some WM_CHAR messages for a control in a dialog box
*
*	#rdesc
*		LRESULT = (code processed) ? 0 : 1
*/
LRESULT CTxtWinHost::OnChar(
	WORD	vkey,			//@parm WM_CHAR wparam (translated key code)
	DWORD	dwFlags)		//@parm WM_CHAR flags
{
	if(!_fInDialogBox || (GetKeyState(VK_CONTROL) & 0x8000)) 
		return 1;
	
	switch(vkey)
	{
	case 'J' - 0x40:					// Ctrl-Return generates Ctrl-J (LF):
	case VK_RETURN:						//  treat it as an ordinary return
		if(!(_dwStyle & ES_WANTRETURN))
			return 0;					// Signal char processed (eaten)
		break;

	case VK_TAB:
		return 0;
	}
	
	return 1;							// Signal char not processed
}


/////////////////////////////////  View rectangle //////////////////////////////////////

void CTxtWinHost::OnGetRect(
	LPRECT prc)
{
	RECT rcInset;
	LONG lSelBarWidth = 0;

	if (_fEmSetRectCalled)
	{
		// Get the selection bar width and add it back to the view inset so
		// we return the rectangle that the application set.
		TxGetSelectionBarWidth(&lSelBarWidth);
	}

	// Get view inset (in HIMETRIC)
	TxGetViewInset(&rcInset);

	// Get client rect in pixels
	TxGetClientRect(prc);

	// Modify the client rect by the inset converted to pixels
	prc->left	+= HimetricXtoDX(rcInset.left + lSelBarWidth, xPerInchScreenDC);
	prc->top	+= HimetricYtoDY(rcInset.top,	 yPerInchScreenDC);
	prc->right	-= HimetricXtoDX(rcInset.right,	 xPerInchScreenDC);
	prc->bottom -= HimetricYtoDY(rcInset.bottom, yPerInchScreenDC);
}

void CTxtWinHost::OnSetRect(
	LPRECT prc,
	BOOL fNewBehavior)
{
	RECT rcClient;
	LONG lSelBarWidth;
	
	// Assuming this is not set to the default, turn on special EM_SETRECT 
	// processing. The important part of this is that we subtract the selection 
	// bar from the view inset because the EM_SETRECT rectangle does not 
	// include the selection bar.
	_fEmSetRectCalled = TRUE;

	if(!prc)
	{
		// We are back to the default so turn off special EM_SETRECT procesing.
		_fEmSetRectCalled = FALSE;
		SetDefaultInset();
	}
	else	
	{
		// For screen display, the following intersects new view RECT
		// with adjusted client area RECT
		TxGetClientRect(&rcClient);

		// Adjust client rect
		// Factors in space for borders
		if(_fBorder)
		{																					  
			rcClient.top		+= _yInset;
			rcClient.bottom 	-= _yInset - 1;
			rcClient.left		+= _xInset;
			rcClient.right		-= _xInset;
		}
	
		if (!fNewBehavior)
		{

			// Intersect the new view rectangle with the 
			// adjusted client area rectangle
			if(!IntersectRect(&_rcViewInset, &rcClient, prc))
				_rcViewInset = rcClient;
		}
		else
		{
			_rcViewInset =*prc;
		}

		// Get the selection bar width 
		TxGetSelectionBarWidth(&lSelBarWidth);

		// compute inset in pixels and convert to HIMETRIC.
		_rcViewInset.left = DXtoHimetricX(_rcViewInset.left - rcClient.left, xPerInchScreenDC)
			- lSelBarWidth;
		_rcViewInset.top = DYtoHimetricY(_rcViewInset.top - rcClient.top, yPerInchScreenDC);
		_rcViewInset.right = DXtoHimetricX(rcClient.right 
			- _rcViewInset.right, xPerInchScreenDC);
		_rcViewInset.bottom = DYtoHimetricY(rcClient.bottom 
			- _rcViewInset.bottom, yPerInchScreenDC);
	}

	_pserv->OnTxPropertyBitsChange(TXTBIT_VIEWINSETCHANGE, 
		TXTBIT_VIEWINSETCHANGE);
}


///////////////////////////////  System notifications  //////////////////////////////////

void CTxtWinHost::OnSysColorChange()
{
	crAuto = GetSysColor(COLOR_WINDOWTEXT);

	if(!_fNotSysBkgnd)
		_crBackground = GetSysColor(COLOR_WINDOW);
	TxInvalidateRect(NULL, TRUE);
}

/*
*	CTxtWinHost::OnGetDlgCode (wparam, lparam)
*
*	@mfunc
*		Handle some WM_GETDLGCODE messages
*
*	#rdesc
*		LRESULT = dialog code
*/
LRESULT CTxtWinHost::OnGetDlgCode(
	WPARAM wparam,
	LPARAM lparam)
{
	LRESULT lres = DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;

	if(_dwStyle & ES_MULTILINE)
		lres |= DLGC_WANTALLKEYS;

	if(!(_dwStyle & ES_SAVESEL))
		lres |= DLGC_HASSETSEL;

	// HACK: If we get one of these messages then we turn on the special
	// EM_SETSEL behavior. The problem is that _fInDialogBox gets turned
	// on after the EM_SETSEL has occurred.
	_fUseSpecialSetSel = TRUE;

	/*
	** -------------------------------------------- JEFFBOG HACK ----
	** Only set Dialog Box Flag if GETDLGCODE message is generated by
	** IsDialogMessage -- if so, the lParam will be a pointer to the
	** message structure passed to IsDialogMessage; otherwise, lParam
	** will be NULL.  Reason for the HACK alert:  the wParam & lParam
	** for GETDLGCODE is still not clearly defined and may end up
	** changing in a way that would throw this off
	** -------------------------------------------- JEFFBOG HACK ----
	*/
	if(lparam)
		_fInDialogBox = TRUE;

	/*
	** If this is a WM_SYSCHAR message generated by the UNDO keystroke
	** we want this message so we can EAT IT in remain.c, case WM_SYSCHAR:
	*/
// 	if(lparam &&
// 		(((LPMSG) lparam)->message == WM_SYSCHAR) &&
// 		((DWORD) ((LPMSG) lparam)->lParam & SYS_ALTERNATE) &&	
// 		((WORD) wparam == VK_BACK))
// 	{
// 		lres |= DLGC_WANTMESSAGE;
// 	}

	return lres;
}


/////////////////////////////////  Other messages  //////////////////////////////////////

LRESULT CTxtWinHost::OnGetOptions() const
{
	LRESULT lres = (_dwStyle & ECO_STYLES);

	if(_fEnableAutoWordSel)
		lres |= ECO_AUTOWORDSELECTION;
	
	return lres;
}

void CTxtWinHost::OnSetOptions(
	WORD  wOp,
	DWORD eco)
{
	DWORD		dwChangeMask = 0;
	DWORD		dwProp = 0;
	DWORD		dwStyle;
	DWORD		dwStyleNew = _dwStyle;
	const BOOL	fAutoWordSel = !!(eco & ECO_AUTOWORDSELECTION);

	// We keep track of the bits changed and then if any have changed we
	// query for all of our property bits and then send them. This simplifies
	// the code because we don't have to set all the bits specially. If the
	// code is changed to make the properties more in line with the new 
	// model, we want to look at this code again.

	// Single line controls can't have a selection bar or do vertical writing
	if(!(_dwStyle & ES_MULTILINE))
	{
#ifdef DBCS
		eco &= ~(ECO_SELECTIONBAR | ECO_VERTICAL);
#else
		eco &= ~ECO_SELECTIONBAR;
#endif
	}
	//Assert((DWORD)fAutoWordSel <= 1);			// Make sure that BOOL is 1/0
	dwStyle = (eco & ECO_STYLES);

	switch(wOp)
	{
	case ECOOP_SET:
		dwStyleNew			= (dwStyleNew & ~ECO_STYLES) | dwStyle;
		_fEnableAutoWordSel = fAutoWordSel;
		break;

	case ECOOP_OR:
		dwStyleNew |= dwStyle;					// Setting a :1 flag = TRUE
		if(fAutoWordSel)						//  or FALSE is 1 instruction
			_fEnableAutoWordSel = TRUE;			// Setting it to a BOOL
		break;									//  averages 9 instructions!

	case ECOOP_AND:
		dwStyleNew &= (dwStyle | ~ECO_STYLES);
		if(!fAutoWordSel)
			_fEnableAutoWordSel = FALSE;
		break;

	case ECOOP_XOR:
		dwStyleNew ^= dwStyle;
		if(fAutoWordSel)
			_fEnableAutoWordSel ^= 1;
		break;
	}

	if(_fEnableAutoWordSel != (unsigned)fAutoWordSel)
	{
		dwChangeMask |= TXTBIT_AUTOWORDSEL; 
	}

	if(dwStyleNew != _dwStyle)
	{
		DWORD dwChange = dwStyleNew ^ _dwStyle;

		//AssertSz(!(dwChange & ~ECO_STYLES), "non-eco style changed");
		_dwStyle = dwStyleNew;
		SetWindowLong(_hwnd, GWL_STYLE, dwStyleNew);

		if(dwChange & ES_NOHIDESEL)	
		{
			dwChangeMask |= TXTBIT_HIDESELECTION;
		}

		// these two local variables to use to keep track of
		// previous setting of ES_READONLY
		BOOL bNeedToSetIME = FALSE;
		BOOL bReadOnly = (_dwStyle & ES_READONLY);

		if(dwChange & ES_READONLY)
		{
			dwChangeMask |= TXTBIT_READONLY;

			// Change drop target state as appropriate.
			if (dwStyleNew & ES_READONLY)
			{
				RevokeDragDrop();
			}
			else
			{
				RegisterDragDrop();
			}
			
			// setup bReadOnly so we will enable/disable IME
			// input mode according to the new ES_READONLY style
			bNeedToSetIME = TRUE;
			bReadOnly = (dwStyleNew & ES_READONLY);
		}

		if(dwChange & ES_VERTICAL)
		{
			dwChangeMask |= TXTBIT_VERTICAL;
		}

		if (dwChange & ES_NOIME)
		{
			_usIMEMode = (dwStyleNew & ES_NOIME) ? 0 : ES_NOIME;
			bNeedToSetIME = TRUE;
		}
		else if (dwChange & ES_SELFIME)
		{
			_usIMEMode = (dwStyleNew & ES_SELFIME) ? 0 : ES_SELFIME;
		}

		// we want to turn off IME if the new style is ES_READONLY or
		// ES_NOIME
		if (bNeedToSetIME)
			ImmAssociateNULLContext (bReadOnly || (_usIMEMode == ES_NOIME));
		

		// no action require for ES_WANTRETURN
		// no action require for ES_SAVESEL
		// do this last
		if(dwChange & ES_SELECTIONBAR)
		{
			dwChangeMask |= TXTBIT_SELBARCHANGE;
		}
	}

	if (dwChangeMask)
	{
		TxGetPropertyBits(dwChangeMask, &dwProp);
		_pserv->OnTxPropertyBitsChange(dwChangeMask, dwProp);
	}
}

void CTxtWinHost::OnSetReadOnly(
	BOOL fReadOnly)
{
	DWORD dwT = GetWindowLong(_hwnd, GWL_STYLE);
	DWORD dwUpdatedBits = 0;

	if(fReadOnly)
	{
		dwT |= ES_READONLY;
		_dwStyle |= ES_READONLY;

		// Turn off Drag Drop 
		RevokeDragDrop();
		dwUpdatedBits |= TXTBIT_READONLY;
	}
	else
	{
		dwT &= ~(DWORD) ES_READONLY;
		_dwStyle &= ~(DWORD) ES_READONLY;

		// Turn drag drop back on
		RegisterDragDrop();	
	}

	_pserv->OnTxPropertyBitsChange(TXTBIT_READONLY, dwUpdatedBits);

	SetWindowLong(_hwnd, GWL_STYLE, dwT);
}


////////////////////////////////////  Helpers  /////////////////////////////////////////

void CTxtWinHost::SetDefaultInset()
{
	// Generate default view rect from client rect
	if(_fBorder)
	{
		// Factors in space for borders
  		_rcViewInset.top	= DYtoHimetricY(_yInset, yPerInchScreenDC);
   		_rcViewInset.bottom	= DYtoHimetricY(_yInset - 1, yPerInchScreenDC);
   		_rcViewInset.left	= DXtoHimetricX(_xInset, xPerInchScreenDC);
   		_rcViewInset.right	= DXtoHimetricX(_xInset, xPerInchScreenDC);
	}
	else
	{
		// Default the top and bottom inset to 0 and the left and right
		// to the size of the border.
		_rcViewInset.top = 0;
		_rcViewInset.bottom = 0;
		_rcViewInset.left = DXtoHimetricX(cxBorder, xPerInchScreenDC);
		_rcViewInset.right = DXtoHimetricX(cxBorder, xPerInchScreenDC);
	}
}


/////////////////////////////////  Far East Support  //////////////////////////////////////

//#ifdef WIN95_IME
HIMC CTxtWinHost::TxImmGetContext()
{
	HIMC himc;

	//Assert(fHaveIMMProcs);

	//Assert( _hwnd );
	himc = ::ImmGetContext( _hwnd );
	//Assert( himc != NULL );
	return himc;
}

void CTxtWinHost::TxImmReleaseContext(
	HIMC himc)
{
	//Assert(fHaveIMMProcs);
	//Assert( _hwnd );
	::ImmReleaseContext( _hwnd, himc );
}

//#endif

void CTxtWinHost::RevokeDragDrop()
{
	if (_fRegisteredForDrop)
	{
		// Note that if the revoke fails we want to know about this in debug
		// builds so we can fix any problems. In retail, we can't really do 
		// so we just ignore it.
// #ifdef DEBUG
// 		HRESULT hr = 
// #endif // DEBUG

			::RevokeDragDrop(_hwnd);

// #ifdef DEBUG 
// 		TESTANDTRACEHR(hr);
// #endif // DEBUG

		_fRegisteredForDrop = FALSE;
	}
}
void CTxtWinHost::RegisterDragDrop()
{
	IDropTarget*pdt;

	if(!_fRegisteredForDrop && _pserv->TxGetDropTarget(&pdt) == NOERROR)
	{
		// The most likely reason for RegisterDragDrop to fail is some kind of
		// bug in our program.

		HRESULT hr = ::RegisterDragDrop(_hwnd, pdt);

		if(hr == NOERROR)
		{	
			_fRegisteredForDrop = TRUE;
		}
		pdt->Release();
	}
}

/*
*	void IMMAssociateNULLContext ( BOOL fReadOnly )
*
*	@func
*		Enable/disable the IME Composition
*
*	@comm
*		Called from the message loop to handle EM_SETREADONLY
*		or when the style ES_NOIME is changed.
*	
*/
void CTxtWinHost::ImmAssociateNULLContext(BOOL fReadOnly)
{
	if ( !fHaveIMMProcs )
		return;

	// Notify text services that read only attribute has changed 
	_pserv->TxSendMessage(EM_SETREADONLY, fReadOnly, 0, NULL);	

	//Assert(	_hwnd );
	if ( fReadOnly && !_oldhimc )
	{
		// terminate IME Composition and save old himc
		_oldhimc = ::ImmAssociateContext( _hwnd, (HIMC) NULL );
	}
	else if ( fReadOnly == FALSE && _oldhimc )
	{
		HIMC	prevhime;
		// restore previous himc and enable IME Composition
		prevhime = ::ImmAssociateContext( _hwnd, _oldhimc );
		//Assert( !prevhime );

		_oldhimc = (HIMC) NULL;
	}

	return;
}

void DrawRectFn(
	HDC hdc, 
	RECT*prc, 
	INT icrTL, 
	INT icrBR,
	BOOL fBot, 
	BOOL fRght)
{
	COLORREF cr;
	COLORREF crSave;
	RECT rc;

	cr = GetSysColor(icrTL);
	crSave = SetBkColor(hdc, cr);

	// top
	rc =*prc;
	rc.bottom = rc.top + 1;
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);

	// left
	rc.bottom = prc->bottom;
	rc.right = rc.left + 1;
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);

	if(icrTL != icrBR)
	{
		cr = GetSysColor(icrBR);
		SetBkColor(hdc, cr);
	}

	// right
	rc.right = prc->right;
	rc.left = rc.right - 1;
	if(!fBot)
		rc.bottom -= cyHScroll;
	if(fRght)
	{
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
	}

	// bottom
	if(fBot)
	{
		rc.left = prc->left;
		rc.top = rc.bottom - 1;
		if(!fRght)
			rc.right -= cxVScroll;
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
	}
	SetBkColor(hdc, crSave);
}

#define cmultBorder 1

void CTxtWinHost::OnSunkenWindowPosChanging(
	HWND hwnd,
	WINDOWPOS*pwndpos)
{
	if(_fVisible)
	{
		RECT rc;
		HWND hwndParent;

		GetWindowRect(hwnd, &rc);
		InflateRect(&rc, cxBorder* cmultBorder, cyBorder* cmultBorder);
		hwndParent = GetParent(hwnd);
		MapWindowPoints(HWND_DESKTOP, hwndParent, (POINT*) &rc, 2);
		InvalidateRect(hwndParent, &rc, FALSE);
	}
}

void CTxtWinHost::DrawSunkenBorder(
	HWND hwnd,
	HDC hdc)
{
	BOOL fVScroll = (_dwStyle & WS_VSCROLL);
	BOOL fHScroll = (_dwStyle & WS_HSCROLL);

	RECT rc;
	RECT rcParent;
	HWND hwndParent;

	GetWindowRect(hwnd, &rc);
	hwndParent = GetParent(hwnd);
	rcParent = rc;
	MapWindowPoints(HWND_DESKTOP, hwndParent, (POINT*)&rcParent, 2);
	InflateRect(&rcParent, cxBorder, cyBorder);
	OffsetRect(&rc, -rc.left, -rc.top);

	if (_pserv)
	{
		// If we have a text control then get whether it thinks there are 
		// scroll bars.
		_pserv->TxGetHScroll(NULL, NULL, NULL, NULL, &fHScroll);
		_pserv->TxGetVScroll(NULL, NULL, NULL, NULL, &fVScroll);
	}


	// draw inner rect
	DrawRectFn(hdc, &rc, icr3DDarkShadow, COLOR_BTNFACE,
		!fHScroll, !fVScroll);

	// draw outer rect
	hwndParent = GetParent(hwnd);
	hdc = GetDC(hwndParent);
	DrawRectFn(hdc, &rcParent, COLOR_BTNSHADOW, COLOR_BTNHIGHLIGHT,
		TRUE, TRUE);
	ReleaseDC(hwndParent, hdc);
}

LRESULT CTxtWinHost::OnSize(
	HWND hwnd,
	WORD fwSizeType,
	int  nWidth,
	int  nHeight)
{
	m_lpRichEdit->AdjustScrollBarSize();

	RECT rcClient = {0};
	::GetClientRect(hwnd, &rcClient);
	if (!::IsRectEmpty(&rcClient))
		m_rcClient = rcClient;
	
	if(!_fVisible)
	{
		_fIconic = GetIconic(hwnd);
		if(!_fIconic)
			_fResized = TRUE;
	}
	else
	{
		if(GetIconic(hwnd))
		{
			//TRACEINFOSZ("Iconicizing");
			_fIconic = TRUE;
		}
		else
		{
			// We use this property because this will force a recalc.
			// We don't actually recalc on a client rect change because
			// most of the time it is pointless. We force one here because
			// some applications use size changes to calculate the optimal 
			// size of the window.
			_pserv->OnTxPropertyBitsChange(TXTBIT_EXTENTCHANGE, 
				TXTBIT_EXTENTCHANGE);

			if(_fIconic)
			{
				//TRACEINFOSZ("Restoring from iconic");
				InvalidateRect(hwnd, NULL, FALSE);
				_fIconic = FALSE;
			}
			
			if ((TxGetEffects() == TXTEFFECT_SUNKEN)	// Draw borders
				/*&& (dwMajorVersion < VERS4)*/)
				DrawSunkenBorder(hwnd, NULL);
		}
	}

	return 0;
}

HRESULT CTxtWinHost::OnTxVisibleChange(
	BOOL fVisible)
{
	_fVisible = fVisible;

	if(!_fVisible && _fResized)
	{
		RECT rc;
		// Control was resized while hidden,
		// need to really resize now
		TxGetClientRect(&rc);
		_fResized = FALSE;
		_pserv->OnTxPropertyBitsChange(TXTBIT_CLIENTRECTCHANGE, 
				TXTBIT_CLIENTRECTCHANGE);
	}
	return S_OK;
}


//////////////////////////// ITextHost Interface  ////////////////////////////

// @doc EXTERNAL 
/* 
*	CTxtWinHost::TxGetDC()
*
*	@mfunc
*		Abstracts GetDC so Text Services does not need a window handle.
*
*	@rdesc
*		A DC or NULL in the event of an error.
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
HDC CTxtWinHost::TxGetDC()
{
	return ::GetDC(_hwnd);
}

/* 
*	CTxtWinHost::TxReleaseDC (hdc)
*
*	@mfunc
*		Release DC gotten by TxGetDC. 
*
*	@rdesc	
*		1 - HDC was released. <nl>
*		0 - HDC was not released. <nl>
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
int CTxtWinHost::TxReleaseDC(
	HDC hdc )				//@parm	DC to release
{
	return ::ReleaseDC (_hwnd, hdc);
}

/* 
*	CTxtWinHost::TxShowScrollBar (fnBar, fShow)
*
*	@mfunc
*		Shows or Hides scroll bar in Text Host window 
*
*	@rdesc
*		TRUE on success, FALSE otherwise
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxShowScrollBar(
	INT  fnBar, 		//@parm	Specifies scroll bar(s) to be shown or hidden
	BOOL fShow )		//@parm	Specifies whether scroll bar is shown or hidden
{
	//return ::ShowScrollBar(_hwnd, fnBar, fShow);

	if (SB_VERT == fnBar)
	{
		m_lpRichEdit->m_VScrollBar.SetVisible(fShow);
	}
	else if (SB_HORZ == fnBar)
	{
		m_lpRichEdit->m_HScrollBar.SetVisible(fShow);
	}
	else if (SB_BOTH == fnBar)
	{
		m_lpRichEdit->m_VScrollBar.SetVisible(fShow);
		m_lpRichEdit->m_HScrollBar.SetVisible(fShow);
	}
	m_lpRichEdit->AdjustScrollBarSize();
	return TRUE;
}

/* 
*	CTxtWinHost::TxEnableScrollBar (fuSBFlags, fuArrowflags)
*
*	@mfunc
*		Enables or disables one or both scroll bar arrows 
*		in Text Host window.
*
*	@rdesc
*		If the arrows are enabled or disabled as specified, the return 
*		value is TRUE. If the arrows are already in the requested state or an 
*		error occurs, the return value is FALSE. 
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.	
*/
BOOL CTxtWinHost::TxEnableScrollBar (
	INT fuSBFlags, 		//@parm Specifies scroll bar type	
	INT fuArrowflags )	//@parm	Specifies whether and which scroll bar arrows
						//		are enabled or disabled
{
	//return ::EnableScrollBar(_hwnd, fuSBFlags, fuArrowflags);

	BOOL bEnable = (fuArrowflags == ESB_ENABLE_BOTH) ? TRUE : FALSE;

	if (SB_VERT == fuSBFlags)
	{
		m_lpRichEdit->m_VScrollBar.SetEnabled(bEnable);
	}
	else if (SB_HORZ == fuSBFlags)
	{
		m_lpRichEdit->m_HScrollBar.SetVisible(bEnable);
	}
	else if (SB_BOTH == fuSBFlags)
	{
		m_lpRichEdit->m_VScrollBar.SetVisible(bEnable);
		m_lpRichEdit->m_HScrollBar.SetVisible(bEnable);
	}
	return TRUE;
}

/* 
*	CTxtWinHost::TxSetScrollRange (fnBar, nMinPos, nMaxPos, fRedraw)
*
*	@mfunc
*		Sets the minimum and maximum position values for the specified 
*		scroll bar in the text host window.
*
*	@rdesc
*		If the arrows are enabled or disabled as specified, the return value
*		is TRUE. If the arrows are already in the requested state or an error
*		occurs, the return value is FALSE. 
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxSetScrollRange(
	INT	 fnBar, 		//@parm	Scroll bar flag
	LONG nMinPos, 		//@parm	Minimum scrolling position
	INT  nMaxPos, 		//@parm	Maximum scrolling position
	BOOL fRedraw )		//@parm	Specifies whether scroll bar should be redrawn
{						//		to reflect change
	//if (NULL == _pserv)
	//{
	//	// We are initializing so do this instead of callback
	//	return ::SetScrollRange(_hwnd, fnBar, nMinPos, nMaxPos, fRedraw);
	//}

	//SetScrollInfo(fnBar, fRedraw);

	//return TRUE;

	RECT rcClient;
	TxGetClientRect(&rcClient);

	if (SB_VERT == fnBar)
	{
		m_lpRichEdit->m_VScrollBar.SetScrollRange(nMaxPos - nMinPos - rcClient.bottom + rcClient.top);
	}
	else if (SB_HORZ == fnBar)
	{
		m_lpRichEdit->m_HScrollBar.SetScrollRange(nMaxPos - nMinPos - rcClient.right + rcClient.left);
	}
	return TRUE;
}

/* 
*	CTxtWinHost::TxSetScrollPos (fnBar, nPos, fRedraw)
*
*	@mfunc
*		Tells Text host to set the position of the scroll box (thumb) in the 
*		specified scroll bar and, if requested, redraws the scroll bar to
*		reflect the new position of the scroll box.
*
*	@rdesc
*		TRUE on success; FALSE otherwise.
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxSetScrollPos (
	INT		fnBar, 		//@parm	Scroll bar flag
	INT		nPos, 		//@parm	New position in scroll box
	BOOL	fRedraw )	//@parm	Redraw flag
{
// 	if (NULL == _pserv)
// 	{
// 		// We are initializing so do this instead of callback
// 		return ::SetScrollPos(_hwnd, fnBar, nPos, fRedraw);
// 	}
// 
// 	SetScrollInfo(fnBar, fRedraw);
// 
// 	return TRUE;

	if (SB_VERT == fnBar)
	{
		m_lpRichEdit->m_VScrollBar.SetScrollPos(nPos);
	}
	else if (SB_HORZ == fnBar)
	{
		m_lpRichEdit->m_HScrollBar.SetScrollPos(nPos);
	}
	return TRUE;
}

/* 
*	CTxtWinHost::TxInvalidateRect (prc, fMode)
*
*	@mfunc
*		Adds a rectangle to the Text Host window's update region
*
*	@comm
*		This function may be called while inactive; however the host
*		implementation is free to invalidate an area greater than
*		the requested rect.
*/
void CTxtWinHost::TxInvalidateRect(
	LPCRECT	prc, 		//@parm	Address of rectangle coordinates
	BOOL	fMode )		//@parm	Erase background flag
{
	if (!_fVisible)
	{
		// There doesn't seem to be a deterministic way to determine whether
		// our window is visible or not via message notifications. Therefore,
		// we check this each time incase it might have changed.
		_fVisible = IsWindowVisible(_hwnd);

		if (_fVisible)
		{
			OnTxVisibleChange(TRUE);
		}
	}

	// Don't bother with invalidating rect if we aren't visible
	if (_fVisible)
	{
// 		if (IsTransparentMode())
// 		{
// 			RECT	rcParent;
// 			HWND	hParent = ::GetParent (_hwnd);
// 
// 	 		// For transparent mode, we need to invalidate the parent
// 			// so it will paint the background.
// 			if (prc)
// 			{
// 				rcParent =*prc;
// 			}
// 			else
// 			{
// 				TxGetClientRect(&rcParent);	
// 			}
// 
// 			::MapWindowPoints(_hwnd, hParent, (LPPOINT)&rcParent, 2);
// 
// 			::InvalidateRect(hParent, &rcParent, fMode);
// //			::HideCaret(_hwnd);
// 		}

		::InvalidateRect(_hwnd, prc, fMode);
	}
}

/* 
*	CTxtWinHost::TxViewChange (fUpdate)
*
*	@mfunc
*		Notify Text Host that update region should be repainted. 
*	
*	@comm
*		It is the responsibility of the text services to call 
*		TxViewChanged every time it decides that it's visual representation 
*		has changed, regardless of whether the control is active or 
*		not. If the control is active, the text services has the additional
*		responsibility of making sure the controls window is updated.
*		It can do this in a number of ways: 1) get a DC for the control's 
*		window and start blasting pixels (TxGetDC and TxReleaseDC), 2) 
*		invalidate the control's window (TxInvalidate), or 3) scroll 
*		the control's window (TxScrollWindowEx).
*
*		Text services can choose to call TxViewChange after it has
*		performed any operations to update the active view and pass a
*		true along with the call.  By passing true, the text host
*		calls UpdateWindow to make sure any unpainted areas of the
*		active control are repainted.
*/
void CTxtWinHost::TxViewChange(
	BOOL fUpdate )		//@parm TRUE = call update window
{
	// Don't bother with paint since we aren't visible
	if (_fVisible)
	{
		// For updates requests that are FALSE, we will let the next WM_PAINT 
		// message pick up the draw.
		if (fUpdate)
		{
// 			if (IsTransparentMode())
// 			{
// 				HWND	hParent = GetParent (_hwnd);
// 
// 	 			// For transparent mode, we need to update the parent first
// 				// before we can update ourself.  Otherwise, what we painted will
// 				// be erased by the parent's background later.
// 				::UpdateWindow (hParent);
// 			}

			::UpdateWindow (_hwnd);
		}
	}
}

/* 
*	CTxtWinHost::TxCreateCaret (hbmp, xWidth, yHeight)
*
*	@mfunc
*		Create new shape for Text Host's caret
*
*	@rdesc
*		TRUE on success, FALSE otherwise.
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxCreateCaret(
	HBITMAP hbmp, 		//@parm Handle of bitmap for caret shape	
	INT xWidth, 		//@parm	Caret width
	INT yHeight )		//@parm	Caret height
{
	return ::CreateCaret (_hwnd, hbmp, xWidth, yHeight);
}

/* 
*	CTxtWinHost::TxShowCaret (fShow)
*
*	@mfunc
*		Make caret visible/invisible at caret position in Text Host window. 
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxShowCaret(
	BOOL fShow )			//@parm Flag whether caret is visible
{
	return fShow ? ::ShowCaret(_hwnd)  :  ::HideCaret(_hwnd);
}

/* 
*	CTxtWinHost::TxSetCaretPos (x, y)
*
*	@mfunc
*		Move caret position to specified coordinates in Text Host window. 
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
BOOL CTxtWinHost::TxSetCaretPos(
	INT x, 				//@parm	Horizontal position (in client coordinates)
	INT y )				//@parm	Vertical position (in client coordinates)
{
	return ::SetCaretPos(x, y);
}

/* 
*	CTxtWinHost::TxSetTimer (idTimer, uTimeout)
*
*	@mfunc
*		Request Text Host to creates a timer with specified time out.
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*/
BOOL CTxtWinHost::TxSetTimer(
	UINT idTimer, 		//@parm Timer identifier	
	UINT uTimeout )		//@parm	Timeout in msec
{
	return ::SetTimer(_hwnd, idTimer, uTimeout, NULL);
}

/* 
*	CTxtWinHost::TxKillTimer (idTimer)
*
*	@mfunc
*		Destroy specified timer
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*
*	@comm
*		This method may be called at any time irrespective of active versus
*		inactive state.
*/
void CTxtWinHost::TxKillTimer(
	UINT idTimer )		//@parm	id of timer
{
	::KillTimer(_hwnd, idTimer);
}

/* 
*	CTxtWinHost::TxScrollWindowEx (dx, dy, lprcScroll, lprcClip, hrgnUpdate,
*									lprcUpdate, fuScroll)
*	@mfunc
*		Request Text Host to scroll the content of the specified client area
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
void CTxtWinHost::TxScrollWindowEx (
	INT		dx, 			//@parm	Amount of horizontal scrolling
	INT		dy, 			//@parm	Amount of vertical scrolling
	LPCRECT lprcScroll, 	//@parm	Scroll rectangle
	LPCRECT lprcClip,		//@parm	Clip rectangle
	HRGN	hrgnUpdate, 	//@parm	Handle of update region
	LPRECT	lprcUpdate,		//@parm	Update rectangle
	UINT	fuScroll )		//@parm	Scrolling flags
{
	//::ScrollWindowEx(_hwnd, dx, dy, lprcScroll, lprcClip, hrgnUpdate, lprcUpdate, fuScroll);
}

/* 
*	CTxtWinHost::TxSetCapture (fCapture)
*
*	@mfunc
*		Set mouse capture in Text Host's window.
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may do nothing.
*/
void CTxtWinHost::TxSetCapture(
	BOOL fCapture )		//@parm	Whether to get or release capture
{
	if (fCapture)
		::SetCapture(_hwnd);
	else
		::ReleaseCapture();
}

/* 
*	CTxtWinHost::TxSetFocus ()
*
*	@mfunc
*		Set focus in text host window.
*
*	@comm
*		This method is only valid when the control is in-place active;
*		calls while inactive may fail.
*/
void CTxtWinHost::TxSetFocus()
{
	::SetFocus(_hwnd);
}

/* 
*	CTxtWinHost::TxSetCursor (hcur, fText)
*
*	@mfunc
*		Establish a new cursor shape in the Text Host's window.
*
*	@comm
*		This method may be called at any time, irrespective of 
*		active vs. inactive state.
*
*		ITextHost::TxSetCursor should be called back by the Text Services 
*		to actually set the mouse cursor. If the fText parameter is TRUE, 
*		Text Services is trying to set the "text" cursor (cursor over text 
*		that is not selected, currently an IBEAM). In that case, the host 
*		can set it to whatever the control MousePointer property is. This is 
*		required by VB compatibility since, via the MousePointer property, 
*		the VB programmer has control over the shape of the mouse cursor, 
*		whenever it would normally be set to an IBEAM.
*/
void CTxtWinHost::TxSetCursor(
	HCURSOR hcur,		//@parm	Handle to cursor
	BOOL	fText )		//@parm Indicates caller wants to set text cursor
						//		(IBeam) if true.
{
	::SetCursor(hcur);
}

/* 
*	CTxtWinHost::TxScreenToClient (lppt)
*
*	@mfunc
*		Convert screen coordinates to Text Host window coordinates.
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*/
BOOL CTxtWinHost::TxScreenToClient(
	LPPOINT lppt )		//@parm	Coordinates for point
{
	return ::ScreenToClient(_hwnd, lppt);	
}

/* 
*	CTxtWinHost::TxClientToScreen (lppt)
*
*	@mfunc
*		Convert Text Host coordinates to screen coordinates 
*
*	@rdesc	
*		TRUE - call succeeded <nl>
*		FALSE - call failed <nl>
*
*	@comm
*		This call is valid at any time, although it is allowed to
*		fail.  In general, if text services has coordinates it needs
*		to translate from client coordinates (e.g. for TOM's 
*		PointFromRange method) the text services will actually be 
*		visible.
*
*		However, if no conversion is possible, then the method will fail.
*/
BOOL CTxtWinHost::TxClientToScreen(
	LPPOINT lppt )		//@parm	Client coordinates to convert.
{
	return ::ClientToScreen(_hwnd, lppt);
}

/* 
*	CTxtWinHost::TxActivate (plOldState)
*
*	@mfunc
*		Notify Text Host that control is active
*
*	@rdesc	
*		S_OK 	- call succeeded. <nl>
*		E_FAIL	- activation is not possible at this time
*
*	@comm
*		It is legal for the host to refuse an activation request;
*		the control may be minimized and thus invisible, for instance.
*
*		The caller should be able to gracefully handle failure to activate.
*
*		Calling this method more than once does not cumulate; only
*		once TxDeactivate call is necessary to deactive.
*
*		The callee will will in <p plOldState> with an arbitrary
*		value.  The caller (Text Services) should hang onto this
*		handle and return it in a subsequent call to TxDeactivate.
*/
HRESULT CTxtWinHost::TxActivate(
	LONG*plOldState )	//@parm Where to put previous activation state
{
	return S_OK;
}

/* 
*	CTxtWinHost::TxDeactivate (lNewState)
*
*	@mfunc
*		Notify Text Host that control is now inactive
*
*	@rdesc	
*		S_OK - call succeeded. <nl>
*		E_FAIL				   <nl>
*
*	@comm
*		Calling this method more than once does not cumulate
*/
HRESULT CTxtWinHost::TxDeactivate(
	LONG lNewState )	//@parm	New state (typically value returned by
						//		TxActivate
{
	return S_OK;
}
	
/* 
*	CTxtWinHost::TxGetClientRect (prc)
*
*	@mfunc
*		Retrive client coordinates of Text Host's client area.
*
*	@rdesc
*		HRESULT = (success) ? S_OK : E_FAIL
*/
HRESULT CTxtWinHost::TxGetClientRect(
	LPRECT prc )		//@parm	Where to put client coordinates
{
	//return ::GetClientRect(_hwnd, prc) ? S_OK : E_FAIL;

	if (NULL == prc)
		return E_FAIL;
	*prc = m_rcClient;
	if (!::IsRectEmpty(prc))
	{
		if (m_lpRichEdit->m_VScrollBar.IsVisible())
		{
			CRect rcVScrollBar;
			m_lpRichEdit->m_VScrollBar.GetRect(&rcVScrollBar);
			prc->right -= rcVScrollBar.Width();
		}

		if (m_lpRichEdit->m_HScrollBar.IsVisible())
		{
			CRect rcHScrollBar;
			m_lpRichEdit->m_HScrollBar.GetRect(&rcHScrollBar);
			prc->bottom -= rcHScrollBar.Height();
		}
	}
	return S_OK;
}

/* 
*	CTxtWinHost::TxGetViewInset	(prc)
*
*	@mfunc
*		Get inset for Text Host window.  Inset is the "white space"
*		around text.
*
*	@rdesc
*		HRESULT = NOERROR
*
*	@comm
*		The Inset rectangle is not strictly a rectangle.  The top, bottom,
*		left, and right fields of the rect structure indicate how far in
*		each direction drawing should be inset. Inset sizes are in client
*		coordinates.
*/
HRESULT CTxtWinHost::TxGetViewInset(
	LPRECT prc )			//@parm Where to put inset rectangle	
{
	*prc = _rcViewInset;
	
	return NOERROR;	
}

/* 
*	CTxtWinHost::TxGetCharFormat (ppCF)
*
*	@mfunc
*		Get Text Host's default character format
*
*	@rdesc
*		HRESULT = E_NOTIMPL (not needed in simple Windows host, since text
*		services provides desired default)
*
*	@comm
*		The callee retains ownwership of the charformat returned.  However,
*		the pointer returned must remain valid until the callee notifies
*		Text Services via OnTxPropertyBitsChange that the default character
*		format has changed.
*/
HRESULT CTxtWinHost::TxGetCharFormat(
	const CHARFORMAT**ppCF) 		//@parm	Where to put ptr to default
									//		character format
{
	return E_NOTIMPL;
}

/* 
*	CTxtWinHost::TxGetParaFormat (ppPF)
*
*	@mfunc
*		Get Text Host default paragraph format
*
*	@rdesc
*		HRESULT = E_NOTIMPL (not needed in simple Windows host, since text
*		services provides desired default)
*
*	@comm
*		The callee retains ownwership of the charformat returned.  However,
*		the pointer returned must remain valid until the callee notifies
*		Text Services via OnTxPropertyBitsChange that the default paragraph
*		format has changed.
*/
HRESULT CTxtWinHost::TxGetParaFormat(
	const PARAFORMAT**ppPF) 	//@parm Where to put ptr to default
								//		paragraph format
{
	return E_NOTIMPL;
}

/* 
*	CTxtWinHost::TxGetSysColor (nIndex)
*
*	@mfunc
*		Get specified color identifer from Text Host.
*
*	@rdesc
*		Color identifier
*
*	@comm
*		Note that the color returned may be*different* than the
*		color that would be returned from a call to GetSysColor.
*		This allows hosts to override default system behavior.
*
*		Needless to say, hosts should be very careful about overriding
*		normal system behavior as it could result in inconsistent UI
*		(particularly with respect to Accessibility	options).
*/
COLORREF CTxtWinHost::TxGetSysColor(
	int nIndex)			//@parm Color to get, same parameter as
						//		GetSysColor Win32 API
{
	if (!_fDisabled 
		|| ((nIndex != COLOR_WINDOW) && (nIndex != COLOR_WINDOWTEXT)))
	{
		// This window is not disabled or the color is not interesting
		// in the disabled case.
		return (nIndex == COLOR_WINDOW && _fNotSysBkgnd)
			? _crBackground : GetSysColor(nIndex);
	}

	// Disabled case
	if (COLOR_WINDOWTEXT == nIndex)
	{
		// Color of text for disabled window
		return GetSysColor(COLOR_GRAYTEXT);
	}

	// Background color for disabled window
	return GetSysColor(COLOR_3DFACE);
	
}

/* 
*	CTxtWinHost::TxGetBackStyle	(pstyle)
*
*	@mfunc
*		Get Text Host background style.
*
*	@rdesc
*		HRESULT = S_OK
*
*	@xref	<e TXTBACKSTYLE>
*/
HRESULT CTxtWinHost::TxGetBackStyle(
	TXTBACKSTYLE*pstyle)  //@parm Where to put background style
{
	*pstyle = (_dwExStyle & WS_EX_TRANSPARENT)
			? TXTBACK_TRANSPARENT : TXTBACK_OPAQUE;
	return NOERROR;
}

/* 
*	CTxtWinHost::TxGetMaxLength	(pLength)
*
*	@mfunc
*		Get Text Host's maximum allowed length.
*
*	@rdesc
*		HRESULT = S_OK	
*
*	@comm
*		This method parallels the EM_LIMITTEXT message.
*		If INFINITE (0xFFFFFFFF) is returned, then text services
*		will use as much memory as needed to store any given text.
*
*		If the limit returned is less than the number of characters
*		currently in the text engine, no data is lost.  Instead,
*		no edits will be allowed to the text*other* than deletion
*		until the text is reduced to below the limit.
*/
HRESULT CTxtWinHost::TxGetMaxLength(
	DWORD*pLength) 	//@parm Maximum allowed length, in number of 
						//		characters
{
	return NOERROR;
}

/* 
*	CTxtWinHost::TxGetScrollBars (pdwScrollBar)
*
*	@mfunc
*		Get Text Host's scroll bars supported.
*
*	@rdesc
*		HRESULT = S_OK
*
*	@comm
*		<p pdwScrollBar> is filled with a boolean combination of the 
*		window styles related to scroll bars.  Specifically, these are:
*
*			WS_VSCROLL	<nl>
*			WS_HSCROLL	<nl>
*			ES_AUTOVSCROLL	<nl>
*			ES_AUTOHSCROLL	<nl>
*			ES_DISABLENOSCROLL	<nl>
*/
HRESULT CTxtWinHost::TxGetScrollBars(
	DWORD*pdwScrollBar) 	//@parm Where to put scrollbar information
{
	*pdwScrollBar =  _dwStyle & (WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | 
						ES_AUTOHSCROLL | ES_DISABLENOSCROLL);
	return NOERROR;
}

/* 
*	CTxtWinHost::TxGetPasswordChar (pch)
*
*	@mfunc
*		Get Text Host's password character.
*
*	@rdesc
*		HRESULT = (password character not enabled) ? S_FALSE : S_OK
*
*	@comm
*		The password char will only be shown if the TXTBIT_USEPASSWORD bit
*		is enabled in TextServices.  If the password character changes,
*		re-enable the TXTBIT_USEPASSWORD bit via 
*		ITextServices::OnTxPropertyBitsChange.
*/
HRESULT CTxtWinHost::TxGetPasswordChar(
	TCHAR*pch)		//@parm Where to put password character
{
	*pch = _chPassword;
	return NOERROR;
}

/* 
*	CTxtWinHost::TxGetAcceleratorPos (pcp)
*
*	@mfunc
*		Get special character to use for underlining accelerator character.
*
*	@rdesc
*		Via <p pcp>, returns character position at which underlining 
*		should occur.  -1 indicates that no character should be underlined. 
*		Return value is an HRESULT (usually S_OK).
*
*	@comm
*		Accelerators allow keyboard shortcuts to various UI elements (like
*		buttons.  Typically, the shortcut character is underlined.
*
*		This function tells Text Services which character is the accelerator
*		and thus should be underlined.  Note that Text Services will*not*
*		process accelerators; that is the responsiblity of the host.
*
*		This method will typically only be called if the TXTBIT_SHOWACCELERATOR
*		bit is set in text services.  
*
*		Note that*any* change to the text in text services will result in the
*		invalidation of the accelerator underlining.  In this case, it is the 
*		host's responsibility to recompute the appropriate character position 
*		and inform text services that a new accelerator is available.
*/
HRESULT CTxtWinHost::TxGetAcceleratorPos(
	LONG*pcp) 		//@parm Out parm to receive cp of character to underline
{
	*pcp = -1;
	return S_OK;
} 										   

/* 
*	CTxtWinHost::OnTxCharFormatChange
*
*	@mfunc
*		Set default character format for the Text Host.
*
*	@rdesc
*		S_OK - call succeeded.	<nl>
*		E_INVALIDARG			<nl>
*		E_FAIL					<nl>
*/
HRESULT CTxtWinHost::OnTxCharFormatChange(
	const CHARFORMAT*pcf) //@parm New default character format	
{
	return S_OK;
}

/* 
*	CTxtWinHost::OnTxParaFormatChange
*
*	@mfunc
*		Set default paragraph format for the Text Host.
*
*	@rdesc
*		S_OK - call succeeded.	<nl>
*		E_INVALIDARG			<nl>
*		E_FAIL					<nl>
*/
HRESULT CTxtWinHost::OnTxParaFormatChange(
	const PARAFORMAT*ppf) //@parm New default paragraph format	
{
	return S_OK;
}

/* 
*	CTxtWinHost::TxGetPropertyBits (dwMask, dwBits)
*
*	@mfunc
*		Get the bit property settings for Text Host.
*
*	@rdesc
*		S_OK
*
*	@comm
*		This call is valid at any time, for any combination of
*		requested property bits.  <p dwMask> may be used by the
*		caller to request specific properties.	
*/
HRESULT CTxtWinHost::TxGetPropertyBits(
	DWORD dwMask,		//@parm	Mask of bit properties to get
	DWORD*pdwBits )	//@parm Where to put bit values
{
// FUTURE: Obvious optimization is to save bits in host the same way that
// they are returned and just return them instead of this mess.

	// Note: this RichEdit host never sets TXTBIT_SHOWACCELERATOR or
	// TXTBIT_SAVESELECTION. They are currently only used by Forms^3 host.
	// This host is always rich text.

	DWORD dwProperties = TXTBIT_RICHTEXT | TXTBIT_ALLOWBEEP;

#ifdef DEBUG
	// make sure that TS doesn't think it's plain text mode when
	// we return TXTBIT_RICHTEXT
	if( (dwMask & TXTBIT_RICHTEXT) && _pserv )
	{
		DWORD mode;
		mode = _pserv->TxSendMessage(EM_GETTEXTMODE, 0, 0, NULL);
		//Assert(mode == TM_RICHTEXT);
	}
#endif // DEBUG

	if (_dwStyle & ES_MULTILINE)
	{
		dwProperties |= TXTBIT_MULTILINE;
	}

	if (_dwStyle & ES_READONLY)
	{
		dwProperties |= TXTBIT_READONLY;
	}

	if (_dwStyle & ES_PASSWORD)
	{
		dwProperties |= TXTBIT_USEPASSWORD;
	}

	if (!(_dwStyle & ES_NOHIDESEL))
	{
		dwProperties |= TXTBIT_HIDESELECTION;
	}

	if (_fEnableAutoWordSel)
	{
		dwProperties |= TXTBIT_AUTOWORDSEL;
	}

	if (!(_dwStyle & (ES_AUTOHSCROLL | WS_HSCROLL)))
	{
		dwProperties |= TXTBIT_WORDWRAP;
	}

	if (_dwStyle & ES_NOOLEDRAGDROP) 
	{
		dwProperties |= TXTBIT_DISABLEDRAG;
	}

	*pdwBits = dwProperties & dwMask; 
	return NOERROR;
}

/* 
*	CTxtWinHost::TxNotify (iNotify,	pv)
*
*	@mfunc
*		Notify Text Host of various events.  Note that there are
*		two basic categories of events, "direct" events and 
*		"delayed" events.  Direct events are sent immediately as
*		they need some processing: EN_PROTECTED is a canonical
*		example.  Delayed events are sent after all processing
*		has occurred; the control is thus in a "stable" state.
*		EN_CHANGE, EN_ERRSPACE, EN_SELCHANGED are examples
*		of delayed notifications.
*
*
*	@rdesc	
*		S_OK - call succeeded <nl>
*		S_FALSE	-- success, but do some different action
*		depending on the event type (see below).
*
*	@comm
*		The notification events are the same as the notification
*		messages sent to the parent window of a richedit window.
*		The firing of events may be controlled with a mask set via
*		the EM_SETEVENTMASK message.
*
*		In general, is legal to make any calls to text services while
*		processing this method; however, implementors are cautioned
*		to avoid excessive recursion. 
*
*		Here is the complete list of notifications that may be
*		sent and a brief description of each:
*
*		<p EN_CHANGE>		Sent when some data in the edit control
*		changes (such as text or formatting).  Controlled by the
*		ENM_CHANGE event mask.  This notification is sent _after_
*		any screen updates have been requested. 
*
*		<p EN_CORRECTTEXT>	PenWin only; currently unused.
*
*		<p EN_DROPFILES>	If the client registered the edit
*		control via DragAcceptFiles, this event will be sent when
*		a WM_DROPFILES or DragEnter( CF_HDROP ) message is received.
*		If S_FALSE is returned, the drop will be ignored, otherwise,
*		the drop will be processed.  The ENM_DROPFILES mask
*		controls this event notification.
*
*		<p EN_ERRSPACE>		Sent when the edit control cannot
*		allocate enough memory.  No additional data is sent and
*		there is no mask for this event.
*
*		<p EN_HSCROLL>		Sent when the user clicks on an edit
*		control's horizontal scroll bar, but before the screen
*		is updated.  No additional data is sent.  The ENM_SCROLL
*		mask controls this event.
*
*		<p EN_IMECHANGE>	unused
*
*		<p EN_KILLFOCUS>	Sent when the edit control looses focus.
*		No additional data is sent and there is no mask.
*
*		<p EN_MAXTEXT>	Sent when the current text insertion
*		has exceeded the specified number of characters for the
*		edit control.  The text insertion has been truncated.  
*		There is no mask for this event.
*
*		<p EN_MSGFILTER>	NB!!! THIS MESSAGE IS NOT SENT TO
*		TxNotify, but is included here for completeness.  With
*		ITextServices::TxSendMessage, client have complete
*		flexibility in filtering all window messages.
*	
*		Sent on a keyboard or mouse event
*		in the control.  A MSGFILTER data structure is sent, 
*		containing the msg, wparam and lparam.  If S_FALSE is
*		returned from this notification, the msg is processed by
*		TextServices, otherwise, the message is ignored.  Note
*		that in this case, the callee has the opportunity to modify
*		the msg, wparam, and lparam before TextServices continues
*		processing.  The ENM_KEYEVENTS and ENM_MOUSEEVENTS masks
*		control this event for the respective event types.
*
*		<p EN_OLEOPFAILED> 	Sent when an OLE call fails.  The
*		ENOLEOPFAILED struct is passed with the index of the object
*		and the error code.  Mask value is nothing.
*		
*		<p EN_PROTECTED>	Sent when the user is taking an
*		action that would change a protected range of text.  An
*		ENPROTECTED data structure is sent, indicating the range
*		of text affected and the window message (if any) affecting
*		the change.  If S_FALSE is returned, the edit will fail.
*		The ENM_PROTECTED mask controls this event.
*
*		<p EN_REQUESTRESIZE>	Sent when a control's contents are
*		either smaller or larger than the control's window size.
*		The client is responsible for resizing the control.  A
*		REQRESIZE structure is sent, indicating the new size of
*		the control.  NB!  Only the size is indicated in this
*		structure; it is the host's responsibility to do any 
*		translation necessary to generate a new client rectangle.
*		The ENM_REQUESTRESIZE mask controls this event.
*
*		<p EN_SAVECLIPBOARD> Sent when an edit control is being
*		destroyed, the callee should indicate whether or not 
*		OleFlushClipboard should be called.  Data indicating the
*		number of characters and objects to be flushed is sent
*		in the ENSAVECLIPBOARD data structure.
*		Mask value is nothing.
*
*		<p EN_SELCHANGE>	Sent when the current selection has
*		changed.  A SELCHANGE data structure is also sent, which
*		indicates the new selection range at the type of data
*		the selection is currently over.  Controlled via the
*		ENM_SELCHANGE mask.
*
*		<p EN_SETFOCUS>	Sent when the edit control receives the
*		keyboard focus.  No extra data is sent; there is no mask.
*
*		<p EN_STOPNOUNDO>	Sent when an action occurs for which
*		the control cannot allocate enough memory to maintain the
*		undo state.  If S_FALSE is returned, the action will be
*		stopped; otherwise, the action will continue.
*
*		<p EN_UPDATE>	Sent before an edit control requests a
*		redraw of altered data or text.  No additional data is
*		sent.  This event is controlled via the ENM_UPDATE mask.
*
*		<p EN_VSCROLL>	Sent when the user clicks an edit control's
*		vertical scrollbar bar before the screen is updated.
*		Controlled via the ENM_SCROLL mask; no extra data is sent.
*
*		<p EN_LINK>		Sent when a mouse event (or WM_SETCURSOR) happens
*		over a range of text that has the EN_LINK mask bit set.
*		An ENLINK data structure will be sent with relevant info.
*/
HRESULT CTxtWinHost::TxNotify(
	DWORD iNotify,		//@parm	Event to notify host of.  One of the
						//		EN_XXX values from Win32, e.g., EN_CHANGE
	void*pv )			//@parm In-only parameter with extra data.  Type
						//		dependent on <p iNotify>
{
	HRESULT		hr = NOERROR;
	LONG		nId;
	NMHDR*		phdr;
	REQRESIZE*	preq;
	RECT		rcOld;

	// We assume here that TextServices has already masked out notifications,
	// so if we get one here, it should be sent
	if(_hwndParent)
	{
		nId = GetWindowLong(_hwnd, GWL_ID);
		// First, handle WM_NOTIFY style notifications
		switch(iNotify)
		{
		case EN_REQUESTRESIZE:
			// Need to map new size into correct rectangle
			//Assert(pv);
			GetWindowRect(_hwnd, &rcOld);
			MapWindowPoints(HWND_DESKTOP, _hwndParent, (POINT*) &rcOld, 2);
			
			preq = (REQRESIZE*)pv;
			preq->rc.top	= rcOld.top;
			preq->rc.left	= rcOld.left;
			preq->rc.right	+= rcOld.left;
			preq->rc.bottom += rcOld.top;

			// FALL-THROUGH!!
					
		case EN_DROPFILES:
		case EN_MSGFILTER:
		case EN_OLEOPFAILED:
		case EN_PROTECTED:
		case EN_SAVECLIPBOARD:
		case EN_SELCHANGE:
		case EN_STOPNOUNDO:
		case EN_LINK:
		case EN_OBJECTPOSITIONS:
		case EN_DRAGDROPDONE:
	
			if( pv )						// Fill out NMHDR portion of pv
			{
				phdr = (NMHDR*)pv;
  				phdr->hwndFrom = _hwnd;
				phdr->idFrom = nId;
				phdr->code = iNotify;
			}

			if(SendMessage(_hwndParent, WM_NOTIFY, (WPARAM) nId, (LPARAM) pv))
			{
				hr = S_FALSE;
			}
			break;

		default:
			SendMessage(_hwndParent, WM_COMMAND, 
					GET_WM_COMMAND_MPS(nId, _hwnd, iNotify));
		}
	}

	return hr;
}

/*
*	CTxtWinHost::TxGetExtent (lpExtent)
*
*	@mfunc
*		Return native size of the control in HIMETRIC
*
*	@rdesc
*		S_OK	<nl>
*		some failure code <nl>
*
*	@comm
*		This method is used by Text Services to implement zooming.
*		Text Services would derive the zoom factor from the ratio between 
*		the himetric and device pixel extent of the client rectangle.
*	
*		[vertical zoom factor] = [pixel height of the client rect]* 2540
*		/ [himetric vertical extent]* [pixel per vertical inch (from DC)]
*	
*		If the vertical and horizontal zoom factors are not the same, Text 
*		Services could ignore the horizontal zoom factor and assume it is 
*		the same as the vertical one.
*/
HRESULT CTxtWinHost::TxGetExtent(
	LPSIZEL lpExtent) 	//@parm  Extent in himetric
{
	// We could implement the TxGetExtent in the following way. However, the
	// call to this in ITextServices is implemented in such a way that it 
	// does something sensible in the face of an error in this call. That 
	// something sensible is that it sets the extent equal to the current
	// client rectangle which is what the following does in a rather convoluted
	// manner. Therefore, we dump the following and just return an error.


#if 0
	// The window's host extent is always the same as the client
	// rectangle.
	RECT rc;
	HRESULT hr = TxGetClientRect(&rc);

	// Get our client rectangle
	if(SUCCEEDED(hr))
	{
		// Calculate the length & convert to himetric
		lpExtent->cx = DXtoHimetricX(rc.right - rc.left, xPerInchScreenDC);
		lpExtent->cy = DYtoHimetricY(rc.bottom - rc.top, yPerInchScreenDC);
	}

	return hr;
#endif // 0

	return E_NOTIMPL;
}

/*
*	CTxtWinHost::TxGetSelectionBarWidth (lSelBarWidth)
*
*	@mfunc
*		Returns size of selection bar in HIMETRIC
*
*	@rdesc
*		S_OK	<nl>
*/
HRESULT	CTxtWinHost::TxGetSelectionBarWidth (
	LONG*lSelBarWidth)		//@parm  Where to return selection bar width 
							// in HIMETRIC
{
	*lSelBarWidth = (_dwStyle & ES_SELECTIONBAR) ? dxSelBar : 0;
	return S_OK;
}


// Helper function in edit.cpp
// LONG GetECDefaultHeightAndWidth(
// 	ITextServices*pts,
// 	HDC hdc,
// 	LONG lZoomNumerator,
// 	LONG lZoomDenominator,
// 	LONG yPixelsPerInch,
// 	LONG*pxAveWidth,
// 	LONG*pxOverhang,
// 	LONG*pxUnderhang);


/*
*	CTxtWinHost::ResizeInset
*
*	@mfunc	Recalculates the view inset for a change in the 
*			default character set.
*
*	@rdesc	None.
*/
void CTxtWinHost::ResizeInset()
{
	// Create a DC
	HDC hdc = GetDC(_hwnd);
	LONG dxRemoveFromSunkenBorder = 0;
	LONG dyRemoveFromSunkenBorder = 0;

	// Get the inset information
	LONG xAveCharWidth = 0;
// 	LONG yCharHeight = GetECDefaultHeightAndWidth(_pserv, hdc, 1, 1,
// 		yPerInchScreenDC, &xAveCharWidth, NULL, NULL);
	LONG yCharHeight = 0;

	ReleaseDC(_hwnd, hdc);

// 	if (dwMajorVersion >= VERS4)
// 	{
// 		// This takes into account the size of the sunken borders in Win95. This
// 		// was lifted directly from richedit 1.0 sources.
// 		dxRemoveFromSunkenBorder = cxBorder* 2;
//         dyRemoveFromSunkenBorder = cyBorder* 2;
// 	}

	// Update the inset
	_xInset = min(xAveCharWidth, _xWidthSys) / 2 - dxRemoveFromSunkenBorder;

	_yInset = min(yCharHeight, _yHeightSys) / 4 - dyRemoveFromSunkenBorder;

	SetDefaultInset();

	_pserv->OnTxPropertyBitsChange(TXTBIT_VIEWINSETCHANGE, 
		TXTBIT_VIEWINSETCHANGE);
}



/*
*	CTxtWinHost::OnSetMargins
*
*	@mfunc	Handle EM_SETMARGINS message
*
*	@rdesc	None.
*/
void CTxtWinHost::OnSetMargins(
	DWORD fwMargin,		//@parm Type of requested operation
	DWORD xLeft,		//@parm Where to put left margin
	DWORD xRight)		//@parm Where to put right margin
{
	LONG xLeftMargin = -1;
	LONG xRightMargin = -1;
	HDC hdc;

	if (EC_USEFONTINFO == fwMargin)
	{
		// Get the DC since it is needed for the call
		hdc = GetDC(_hwnd);

		// Multiline behaves differently than single line
// 		if (_dwStyle & ES_MULTILINE)
// 		{
// 			// Multiline - over/underhange controls margin
// 			GetECDefaultHeightAndWidth(_pserv, hdc, 1, 1,
// 				yPerInchScreenDC, NULL, &xLeftMargin, &xRightMargin);
// 		}
// 		else
// 		{
// 			// Single line edit controls set the margins to 
// 			// the average character width on both left and
// 			// right.
// 			GetECDefaultHeightAndWidth(_pserv, hdc, 1, 1,
// 				yPerInchScreenDC, &xLeftMargin, NULL, NULL);
// 
// 			xRightMargin = xRightMargin;
// 		}

		ReleaseDC(_hwnd, hdc);
	}
	else
	{
		// The request is for setting exact pixels.
		if (EC_LEFTMARGIN & fwMargin)
		{
			xLeftMargin = xLeft;
		}

		if (EC_RIGHTMARGIN & fwMargin)
		{
			xRightMargin = xRight;
		}
	}

	// Set left margin if so requested
	if (xLeftMargin != -1)
	{
		_rcViewInset.left =	DXtoHimetricX(xLeftMargin, xPerInchScreenDC);
	}

	// Set right margin if so requested
	if (xRightMargin != -1)
	{
		_rcViewInset.right = DXtoHimetricX(xRightMargin, xPerInchScreenDC);
	}
}



/*
*	CTxtWinHost::SetScrollInfo
*
*	@mfunc	Set scrolling information for the scroll bar.
*
*	@rdesc	None.
*/
void CTxtWinHost::SetScrollInfo(
	INT fnBar,			//@parm	Specifies scroll bar to be updated
	BOOL fRedraw)		//@parm whether redraw is necessary
{
	// Set up the basic structure for the call
	SCROLLINFO si;
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_ALL;

	// Call back to the control to get the parameters
	if (fnBar == SB_HORZ)
	{
		_pserv->TxGetHScroll((LONG*) &si.nMin, (LONG*) &si.nMax, 
			(LONG*) &si.nPos, (LONG*) &si.nPage, NULL);
	}
	else
	{
		_pserv->TxGetVScroll((LONG*) &si.nMin, 
			(LONG*) &si.nMax, (LONG*) &si.nPos, (LONG*) &si.nPage, NULL);
	}

	// Do the call
	::SetScrollInfo(_hwnd, fnBar, &si, fRedraw);
}

/*
*	CTxtWinHost::SetScrollBarsForWmEnable
*
*	@mfunc	Enable/Disable scroll bars
*
*	@rdesc	None.
*/
void CTxtWinHost::SetScrollBarsForWmEnable(
	BOOL fEnable)		//@parm Whether scrollbars s/b enabled or disabled.
{
	if (NULL == _pserv)
	{
		// If we don't have an edit object there can't be any scroll bars.
		return;
	}

	BOOL fHoriz = FALSE;
	BOOL fVert = FALSE;
	UINT wArrows = fEnable ? ESB_ENABLE_BOTH : ESB_DISABLE_BOTH;

	_pserv->TxGetHScroll(NULL, NULL, NULL, NULL, &fHoriz);

	_pserv->TxGetVScroll(NULL, NULL, NULL, NULL, &fVert);

	if (fHoriz)
	{
		// There is a vertical scroll bar
		EnableScrollBar(_hwnd, SB_HORZ, wArrows);
	}

	if (fVert)
	{
		// There is a horizontal scroll bar
		EnableScrollBar(_hwnd, SB_VERT, wArrows);
	}
}

CSkinRichEdit::CSkinRichEdit(void)
{
	m_pTxtWinHost = NULL;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
}

CSkinRichEdit::~CSkinRichEdit(void)
{
}

LRESULT CSkinRichEdit::RichEditWndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
	HWND hwnd = m_hWnd;

	switch(msg)
	{
// 	case WM_CHAR:
// // 		TRACEERRSZSC("WM_CHAR > 256 on Win95; assumed to be Unicode",

// // 			VER_PLATFORM_WIN32_WINDOWS == dwPlatformId && wparam >= 256);

// 
// 		if ( !ped->IsAccumulateDBCMode() && 
// 			(VER_PLATFORM_WIN32_MACINTOSH == dwPlatformId || 
// 			VER_PLATFORM_WIN32_WINDOWS == dwPlatformId) &&
// 			wparam > 127 && wparam < 256)
// 		{
// 			WPARAM wparamNew;
// 
// 			// WM_CHAR messages that come directly from Win95 are single
// 			// byte. Therefore, we convert all WM_CHAR messages with wparam
// 			// between 128 and 255 to Unicode.
// 			if(UnicodeFromMbcs((LPWSTR)&wparamNew, 1, (char*)&wparam, 1,
// 				GetKeyboardCodePage()) == 1 )
// 			{
// 				wparam = wparamNew;
// 				break;
// 			}
// 			return 0;
// 		}
// 		break;

	case WM_KILLFOCUS:
		{
			HRESULT hr = 0;
			if (m_pTxtWinHost != NULL)
				hr = m_pTxtWinHost->TxWindowProc(hwnd, msg, wparam, lparam);
			::InvalidateRect(m_hWnd, NULL, TRUE);
			return hr;
		}

	case WM_GETTEXT:
		// EVIL HACK ALERT: on Win95, WM_GETTEXT should always be treated
		// as an ANSI message.
		if( m_pTxtWinHost && VER_PLATFORM_WIN32_WINDOWS == dwPlatformId )
		{
			GETTEXTEX gt;

			gt.cb = wparam;
			gt.flags = GT_USECRLF;
			gt.codepage = CP_ACP;
			gt.lpDefaultChar = NULL;
			gt.lpUsedDefChar = NULL;

			return m_pTxtWinHost->TxWindowProc(hwnd, EM_GETTEXTEX, (WPARAM)&gt, lparam);
		}
		break;

	case WM_GETTEXTLENGTH:
		// EVIL HACK ALERT: on Win95, WM_GETEXTLENGTH should always
		// be treated an ANSI message
		if( VER_PLATFORM_WIN32_WINDOWS == dwPlatformId )
		{
			GETTEXTLENGTHEX gtl;

			gtl.flags = GTL_NUMBYTES | GTL_PRECISE | GTL_USECRLF;
			gtl.codepage = CP_ACP;

			return m_pTxtWinHost->TxWindowProc(hwnd, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 
				0);
		}
		break;


	case WM_NCCALCSIZE:
		// we can't rely on WM_WININICHANGE so we use WM_NCCALCSIZE since
		// changing any of these should trigger a WM_NCCALCSIZE
		GetSysParms();
		break;

	case WM_NCCREATE:
		m_pTxtWinHost = CTxtWinHost::OnNCCreate(this, (CREATESTRUCT*) lparam);
		return m_pTxtWinHost != NULL ? TRUE : FALSE;

	case WM_NCDESTROY:
		CTxtWinHost::OnNCDestroy(m_pTxtWinHost);
		m_pTxtWinHost = NULL;
		SetMsgHandled(FALSE);
		return 0;

	case WM_ERASEBKGND:
		return TRUE;

	case WM_CREATE:
		OnCreate((LPCREATESTRUCT)lparam);
		goto Ret0;
	}

Ret0:
	if (m_pTxtWinHost != NULL)
		return m_pTxtWinHost->TxWindowProc(hwnd, msg, wparam, lparam);
	else
	{
		SetMsgHandled(FALSE);
		return 0;
	}
}

int CSkinRichEdit::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.right-14, rcClient.top, rcClient.right, rcClient.bottom);
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

	rc = CRect(rcClient.left, rcClient.bottom - 14, rcClient.right, rcClient.bottom);
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

	return 0;
}

void CSkinRichEdit::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CSkinRichEdit::AdjustScrollBarSize()
{
	CRect rcClient;
	GetClientRect(&rcClient);
	if (::IsRectEmpty(&rcClient))
		return;

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

void CSkinRichEdit::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}
