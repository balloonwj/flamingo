#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"

class CFlamingoClient;

class CSystemSettingDlg : public CDialogImpl<CSystemSettingDlg>
{
public:
	CSystemSettingDlg(void);
	virtual ~CSystemSettingDlg(void);

	enum { IDD = IDD_SYSTEMSETTINGS };

	BEGIN_MSG_MAP_EX(CSystemSettingDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		
		COMMAND_ID_HANDLER_EX(IDC_AUTOREPLY, OnCheckAutoReply)
		COMMAND_ID_HANDLER_EX(IDC_DESTROYAFTERREAD, OnCheckDestroyAfterRead)
		COMMAND_ID_HANDLER_EX(IDOK, OnBtnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnBtnCancel)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	
	void OnCheckAutoReply(UINT uNotifyCode, int nID, CWindow wndCtl);			
	void OnCheckDestroyAfterRead(UINT uNotifyCode, int nID, CWindow wndCtl);
	
	void OnBtnOK(UINT uNotifyCode, int nID, CWindow wndCtl);					
	void OnBtnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);						
	
	BOOL InitUI();
	void UninitUI();
public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
		
	CSkinButton			m_btnMute;					//关闭所有声音
	CSkinButton			m_btnAutoReply;				//自动回复
	CSkinEdit			m_edtAutoReply;				//自动回复内容
	CSkinButton			m_btnDestroyAfterRead;		//阅后即焚
	CSkinComboBox		m_cboDurationRead;			//阅后即焚持续时间
	CSkinButton			m_btnRevokeChatMsg;			//消息撤回功能
	CSkinButton			m_btnExitPrompt;			//关闭主对话框时弹出提示对话框
	CSkinButton			m_btnExitWhenClose;			//关闭主对话框时直接退出
	CSkinButton			m_btnShowLastMsg;			//聊天窗口显示最近聊天记录

	CSkinButton			m_btnOK;					//确定
	CSkinButton			m_btnCancel;				//取消
	
	HFONT				m_hFont;

	HICON m_hDlgIcon,	m_hDlgSmallIcon;
};
