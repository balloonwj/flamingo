#pragma once
#include "resource.h"
#include "SkinLib/SkinLib.h"

#define MAX_SRV_ADDR	64
#define MAX_SRV_PORT	8

class CFlamingoClient;

class CLoginSettingsDlg : public CDialogImpl<CLoginSettingsDlg>, public CMessageFilter
{
public:
	CLoginSettingsDlg();
	virtual ~CLoginSettingsDlg();

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_LOGSETDLG };

	BEGIN_MSG_MAP_EX(CLoginSettingsDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(IDC_COMBO_PROTYPE, OnComboBox_Select)
		COMMAND_ID_HANDLER_EX(ID_LOGINSETTING, OnBtn_OK)
		COMMAND_ID_HANDLER_EX(ID_LOGINSETTINGCANCEL, OnBtn_Cancel)
		//REFLECT_NOTIFICATIONS()      // 消息反射通知宏
	END_MSG_MAP()

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnComboBox_Select(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl);		
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

public:
	CFlamingoClient*	m_pClient;

private:
	CSkinDialog		m_SkinDlg;
	//CSkinStatic		m_staticSrvAddr;
	//CSkinStatic		m_staticFileSrvAddr;
	//CSkinStatic		m_staticSrvPort;
	//CSkinStatic		m_staticFilePort;

	CSkinEdit		m_editSrvAddr;
	CSkinEdit		m_editFileSrvAddr;
    CSkinEdit		m_editImgSrvAddr;
	CSkinEdit		m_editSrvPort;
	CSkinEdit		m_editFilePort;
    CSkinEdit		m_editImgPort;

	CSkinComboBox	m_comboProxyType;
	CSkinEdit		m_editProxyAddr;
	CSkinEdit		m_editProxyPort;

	CSkinButton		m_btnOK;
	CSkinButton		m_btnCancel;
	
	TCHAR			m_szSrvAddr[MAX_SRV_ADDR];	
	TCHAR			m_szFileSrvAddr[MAX_SRV_ADDR];
    TCHAR			m_szImgSrvAddr[MAX_SRV_ADDR];
	TCHAR			m_szSrvPort[MAX_SRV_PORT];	
	TCHAR			m_szFilePort[MAX_SRV_PORT];
    TCHAR			m_szImgPort[MAX_SRV_PORT];
	TCHAR			m_szProxyAddr[MAX_SRV_ADDR];
	TCHAR			m_szProxyPort[MAX_SRV_PORT];
};