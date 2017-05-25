#pragma once
#pragma once

#define C_PAGES 3 
#include "resource.h"
#include "SkinLib/SkinLib.h"

class CChangePicHead : public CDialogImpl<CChangePicHead>
{
public:
	CChangePicHead();
	virtual ~CChangePicHead();

	enum { IDD = IDD_CHANGEPICDLG };

	BEGIN_MSG_MAP_EX(CChangePicHead)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_ACTIVATE(OnActivate)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	BOOL Init();
	BOOL InitTabCtrl();			// ≥ı ºªØTab¿∏
private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther);
	void OnPaint(CDCHandle dc);
	void OnClose();
	void OnDestroy();

private:
	CSkinDialog			m_SkinDlg;	
	CSkinTabCtrl		m_TabCtrl;	
	CSkinButton			m_btnChange, m_btnUnChange;
};