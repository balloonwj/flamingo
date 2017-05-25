#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"


class CFlamingoClient;

class CBuddyInfoDlg : public CDialogImpl<CBuddyInfoDlg>, public CMessageFilter
{
public:
	CBuddyInfoDlg(void);
	~CBuddyInfoDlg(void);

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	enum { IDD = IDD_BUDDYINFODLG };

	BEGIN_MSG_MAP_EX(CBuddyInfoDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnBtnOK)
	END_MSG_MAP()

public:
	void SetWindowTitle(PCTSTR pszWindowTitle);
	void UpdateCtrls();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnBtnOK(UINT uNotifyCode, int nID, CWindow wndCtl);	// “更新”按钮

	BOOL Init();		// 初始化
	void UnInit();		// 反初始化

public:
	CFlamingoClient*		m_lpFMGClient;
	UINT				m_nUTalkUin;

private:
	CSkinDialog			m_SkinDlg;
	HICON				m_hDlgIcon;
	HICON				m_hDlgSmallIcon;

	CSkinButton			m_btnOK;

	CString				m_strWindowTitle;
};
