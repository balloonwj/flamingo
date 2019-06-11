// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//  are changed infrequently
//

#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// Change these values to use different versions
#define WINVER		    0x0601
#define _WIN32_WINNT	0x0601
#define _WIN32_IE	    0x0601
#define _RICHEDIT_VER	0x0200

#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>	// ������ATL��
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>		// ������WTL��

extern CAppModule _Module;	// WTL������CComModule�汾

#include <atlwin.h>		// ATL������
#include <atlcrack.h>	// WTL��ǿ����Ϣ��
//#include <atlsplit.h>
#include <atlframe.h>	// WTL����ܴ�����
//#include <atlgdi.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atldlgs.h>
#include <atlmisc.h>	// WTLʵ�ù�����
#include "resource.h"
#include <time.h>

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define DELETE_PTR(p) \
    if (p != NULL)     \
    {                   \
        delete p;       \
        p = NULL;       \
    }

