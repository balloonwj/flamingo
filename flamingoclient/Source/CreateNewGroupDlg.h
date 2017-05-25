#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "UserSessionData.h"


class CFlamingoClient;

//查找还有并添加好友对话框
class CCreateNewGroupDlg : public CDialogImpl<CCreateNewGroupDlg>, public CMessageFilter
{
public:
	CCreateNewGroupDlg();
	virtual ~CCreateNewGroupDlg();

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_CREATENEWGROUP };

	BEGIN_MSG_MAP_EX(CCreateNewGroupDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		MESSAGE_HANDLER_EX(FMG_MSG_CREATE_NEW_GROUP_RESULT, OnCreateNewGroupResult)
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
	END_MSG_MAP()

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnCreateNewGroupResult(UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL InitUI();
	void UninitUI();

	void AddMessageFilter();
	void RemoveMessageFilter();

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
	CSkinEdit			m_edtGroupName;
	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;
};