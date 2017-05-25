#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "UserSessionData.h"

class CFlamingoClient;

//修改密码对话框类
class CModifyPasswordDlg : public CDialogImpl<CModifyPasswordDlg>, public CMessageFilter
{
public:
	CModifyPasswordDlg();
	virtual ~CModifyPasswordDlg();

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	enum { IDD = IDD_MODIFYPASSWORD };

	BEGIN_MSG_MAP_EX(CModifyPasswordDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_SHOWWINDOW(OnShowWindow)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(IDOK, OnBtn_OK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnBtn_Cancel)
		MESSAGE_HANDLER(FMG_MSG_MODIFY_PASSWORD_RESULT, OnModifyPasswordResult);
	END_MSG_MAP()


private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnShowWindow(BOOL bShow, UINT nStatus);
	void OnClose();
	void OnDestroy();
	void OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnModifyPasswordResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

	BOOL InitUI();
	void UninitUI();

	void AddMessageFilter();
	void Reset();
	void RemoveMessageFilter();

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
	CSkinEdit			m_edtOldPassword;
	CSkinEdit			m_edtNewPassword;
	CSkinEdit			m_edtNewPasswordConfirm;
	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;
};