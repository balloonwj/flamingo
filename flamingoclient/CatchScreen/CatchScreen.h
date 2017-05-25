// CatchScreen.h : main header file for the CATCHSCREEN application
//

#if !defined(AFX_CATCHSCREEN_H__2E35F34E_1325_4316_936B_E66020BEE5DD__INCLUDED_)
#define AFX_CATCHSCREEN_H__2E35F34E_1325_4316_936B_E66020BEE5DD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CCatchScreenApp:
// See CatchScreen.cpp for the implementation of this class
//

class CCatchScreenApp : public CWinApp
{
public:
//*******************************
	HWND	m_hwndDlg;

	long	m_nSucceedExitCode;		//程序截图成功后退出码
//*******************************

public:
	CCatchScreenApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCatchScreenApp)
	public:
	virtual BOOL InitInstance();
	virtual BOOL ProcessMessageFilter(int code, LPMSG lpMsg);
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CCatchScreenApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CATCHSCREEN_H__2E35F34E_1325_4316_936B_E66020BEE5DD__INCLUDED_)
