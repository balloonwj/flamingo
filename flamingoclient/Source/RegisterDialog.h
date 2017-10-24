#pragma once
#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "UserSessionData.h"

class CFlamingoClient;

class CRegisterDialog : public CDialogImpl<CRegisterDialog>
{
public:
	CRegisterDialog();
	virtual ~CRegisterDialog();

	
	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_REGISTER };

	BEGIN_MSG_MAP_EX(CRegisterDialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(ID_BTN_REGIST, OnBtn_Regist)
		COMMAND_ID_HANDLER_EX(ID_BTN_CANCEL, OnBtn_Cancel)
		MESSAGE_HANDLER(FMG_MSG_REGISTER, OnRegisterResult);
	END_MSG_MAP()

public:
	PCTSTR GetAccountName();
	PCTSTR GetPassword();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnBtn_Regist(UINT uNotifyCode, int nID, CWindow wndCtl);		// “注册”按钮
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);		// “取消注册”按钮
	LRESULT OnRegisterResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

	BOOL InitUI();
	void UninitUI();

    static UINT WINAPI RegisterThreadProc(void* pParam);

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_RegDlg;
	CSkinEdit			m_edtRegId, m_edtRegName, m_edtPwd, m_edtCheckPwd;
	CSkinButton			m_btnRegist, m_btnCancel;

	CString				m_strMobile;
	CString				m_strAccount;
	CString				m_strNickName;
	CString				m_strPassword;
	CString				m_strCheckPassword;
};