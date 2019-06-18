#pragma once

#include "SkinLib/SkinLib.h"
#include "RegisterDialog.h"
#include "LoginSettingsDlg.h"
#include "LoginAccountList.h"

class CFlamingoClient;
struct LOGIN_ACCOUNT_INFO;

class CLoginDlg : public CDialogImpl<CLoginDlg>
{
public:
    CLoginDlg(CFlamingoClient*	pFMGClient);
	virtual ~CLoginDlg(void);

	enum { IDD = IDD_LOGINDLG };

    BEGIN_MSG_MAP_EX(CLoginDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_SYSCOMMAND(OnSysCommand)
        MSG_WM_MEASUREITEM(OnMeasureItem)
        MSG_WM_DRAWITEM(OnDrawItem)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
        COMMAND_HANDLER_EX(ID_COMBO_UID, CBN_EDITCHANGE, OnCbo_EditChange_UID)
        COMMAND_HANDLER_EX(ID_COMBO_UID, CBN_SELCHANGE, OnCbo_SelChange_UID)
        COMMAND_ID_HANDLER_EX(ID_CHECK_REMEMBER_PWD, OnBtn_RememberPwd)
        COMMAND_ID_HANDLER_EX(ID_CHECK_AUTO_LOGIN, OnBtn_AutoLogin)
        COMMAND_ID_HANDLER_EX(ID_BTN_LOGIN, OnBtn_Login)
        COMMAND_ID_HANDLER_EX(ID_BTN_SET, OnBtn_Set)
        COMMAND_ID_HANDLER_EX(ID_STATIC_REG_ACCOUNT, OnRegister)
        COMMAND_RANGE_HANDLER_EX(ID_MENU_IMONLINE, ID_MENU_INVISIBLE, OnMenu_LoginStatus)
		REFLECT_NOTIFICATIONS()														// 消息反射通知宏
	END_MSG_MAP()

public:
	BOOL GetLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount);
    BOOL SetLoginAccountInfo(const LOGIN_ACCOUNT_INFO* lpAccount);

	void SetDefaultAccount(PCTSTR pszDefaultAccount);
	void SetDefaultPassword(PCTSTR pszDefaultPassword);

    void DoLogin();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnSysCommand(UINT nID, CPoint pt);
	void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	void OnClose();
	void OnDestroy();
	void OnCbo_EditChange_UID(UINT uNotifyCode, int nID, CWindow wndCtl);			// “UTalk帐号”组合框
	void OnCbo_SelChange_UID(UINT uNotifyCode, int nID, CWindow wndCtl);			// “UTalk帐号”组合框
	void OnBtn_RememberPwd(UINT uNotifyCode, int nID, CWindow wndCtl);				// “记住密码”复选框
	void OnBtn_AutoLogin(UINT uNotifyCode, int nID, CWindow wndCtl);				// “自动登录”复选框
	void OnBtn_Login(UINT uNotifyCode, int nID, CWindow wndCtl);					// “登录”按钮
	void OnBtn_Set(UINT uNotifyCode, int nID, CWindow wndCtl);						// “设置”按钮
	void OnMenu_LoginStatus(UINT uNotifyCode, int nID, CWindow wndCtl);				// “登录状态”菜单按钮

	void DetectNewVersion();														//检测新版本
	void OnRegister(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

	long GetStatusFromMenuID(int nMenuID);									// 从菜单ID获取对应的UTalk_STATUS
	void StatusMenuBtn_SetIconPic(CSkinButton& btnStatus, long nStatus);	// 根据指定状态设置状态菜单按钮的图标
	void SetCurUser(LPCTSTR lpszUser, BOOL bPwdInvalid = FALSE);

    static UINT WINAPI LoginThreadProc(void* pParam);

public:
	CFlamingoClient*	m_lpFMGClient;
	CLoginAccountList*  m_pLoginAccountList;

private:
	CSkinDialog			m_SkinDlg;
	CSkinComboBox		m_cboUid;
	CSkinEdit			m_edtPwd;
	CSkinButton			m_btnRememberPwd;
	CSkinButton			m_btnAutoLogin;
	CSkinHyperLink		m_lnkRegAccount;											//注册链接按钮
	CSkinHyperLink		m_lnkLostPwd;

	CSkinButton			m_btnSet;
	CSkinButton			m_btnLogin;
	CSkinButton			m_btnLoginStatus;
	CSkinPictureBox		m_picHead;
	CSkinMenu			m_SkinMenu;

	HICON m_hDlgIcon,	m_hDlgSmallIcon;
	LOGIN_ACCOUNT_INFO	m_stAccountInfo;
	BOOL				m_bFirstModify;

	CLoginSettingsDlg	m_LoginSettingDlg;											// 设置对话框
	CRegisterDialog		m_RegDlg;													// 注册对话框
	CString				m_strDefaultAccount;
	CString				m_strDefaultPassword;
};
