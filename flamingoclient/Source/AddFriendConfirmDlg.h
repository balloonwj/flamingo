#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "FlamingoClient.h"

class CAddFriendConfirmDlg : public CDialogImpl<CAddFriendConfirmDlg>
{
public:
	CAddFriendConfirmDlg();
	virtual ~CAddFriendConfirmDlg();

	void SetWindowTitle(PCTSTR pszWindowTitle);
	void SetWindowInfo(PCTSTR pszWindowInfo);
	void ShowAgreeButton(BOOL bShow);
	void ShowRefuseButton(BOOL bShow);
	void ShowOKButton(BOOL bShow);

	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_ADDFRIENDCONFIRMDLG };

	BEGIN_MSG_MAP_EX(CFindFriendDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(ID_ADDCONFIRM_AGREE, OnAgreeOrReject)
		COMMAND_ID_HANDLER_EX(ID_ADDCONFIRM_REFUSE, OnAgreeOrReject)
	END_MSG_MAP()

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();

	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnAgreeOrReject(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
	CSkinButton			m_btnAgree;
	CSkinButton			m_btnRefuse;
	CSkinButton			m_btnOK;
	CSkinHyperLink		m_staticAddConfirmInfo;

	CString				m_strWindowTitle;
	CString				m_strWindowInfo;
	BOOL				m_bShowAgreeButton;
	BOOL				m_bShowRefuseButton;
	BOOL				m_bShowOKButton;
};