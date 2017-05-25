#pragma once 
#include "resource.h"
#include "SkinLib/SkinLib.h"

class CMobileStatusDlg : public CDialogImpl<CMobileStatusDlg>
{
public:
	CMobileStatusDlg(void);
	virtual ~CMobileStatusDlg(void);

	enum { IDD = IDD_MOBILESTATUS };

	BEGIN_MSG_MAP_EX(CMobileStatusDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void SetWindowTitle(PCTSTR pszTitle);
	void SetInfoText(PCTSTR pszInfoText);
	void EnableAutoDisappear(BOOL bEnable);

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnTimer(UINT_PTR nIDEvent);
	
	void OnClose();
	void OnDestroy();
	
	void InitUI();
	void UninitUI();

private:
	CSkinDialog			m_SkinDlg;
	CSkinStatic			m_staInfo;

	TCHAR				m_szWindowTitle[32];
	TCHAR				m_szInfoText[128];

	BOOL				m_bEnableAutoDisappear;		//是否自动消失
	UINT_PTR			m_TimerID;
};
