#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"


class CPicBarDlg : public CDialogImpl<CPicBarDlg>
{
public:
	CPicBarDlg(void);
	~CPicBarDlg(void);

	enum { IDD = IDD_PICBARDLG };

	BEGIN_MSG_MAP_EX(CPicBarDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(301, OnBtn_Clicked)
		COMMAND_ID_HANDLER_EX(302, OnBtn_Clicked)
		COMMAND_ID_HANDLER_EX(303, OnBtn_Clicked)
		COMMAND_ID_HANDLER_EX(304, OnBtn_Clicked)
		COMMAND_ID_HANDLER_EX(305, OnBtn_Clicked)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose()
	{
		DestroyWindow();
	}
	void OnDestroy();

	void OnBtn_Clicked(UINT uNotifyCode, int nID, CWindow wndCtl);

private:
	CSkinToolBar m_ToolBar;
};
