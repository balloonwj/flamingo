#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"


class CFlamingoClient;

//修改备注对话框
class CModifyMarkNameDlg : public CDialogImpl<CModifyMarkNameDlg>
{
public:
	CModifyMarkNameDlg();
	virtual ~CModifyMarkNameDlg();

	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_MODIFYMARKNAME };

	BEGIN_MSG_MAP_EX(CModifyMarkNameDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
	END_MSG_MAP()

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

public:
	CFlamingoClient*		m_pFMGClient;
	UINT				m_uUserID;

private:
	CSkinDialog			m_SkinDlg;
	CSkinStatic			m_staMarkName;
	CSkinEdit			m_edtMarkName;
	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;
};