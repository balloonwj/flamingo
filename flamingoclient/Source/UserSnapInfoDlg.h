#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"

//查找联系人或者群时弹出的用户快照信息对话框
class CUserSnapInfoDlg : public CDialogImpl<CUserSnapInfoDlg>
{
public:
	CUserSnapInfoDlg();
	~CUserSnapInfoDlg();
	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_USERSNAPINFO };

	BEGIN_MSG_MAP_EX(CUserSnapInfoDlg)
		MSG_WM_INITDIALOG(OnInitDialog)

		MSG_WM_CLOSE(OnClose)

		COMMAND_ID_HANDLER_EX(IDOK, OnBtn_OK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnBtn_Cancel)
	END_MSG_MAP()

public:
	void SetAccountID(UINT uAccountID);
	void SetUserFaceID(UINT uFaceID);
	void SetAccountName(PCTSTR pszAccountName);
	void SetNickName(PCTSTR pszNickName);
	void SetOperationDescription(PCTSTR pszDesc);
	void SetOKButtonText(PCTSTR pszOKButtonText);
	void EnableOKButton(BOOL bEnable);

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	
	void OnClose();
	void OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl);		
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();

private:
	CSkinDialog			m_SkinDlg;
	CSkinPictureBox		m_picUserThumb;
	CSkinStatic			m_staUserAccountName;
	CSkinStatic			m_staUserNickName;
	CSkinStatic			m_staOperationDesc;		//操作描述

	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;
	
	UINT				m_uAccountID;
	UINT				m_uFaceID;
	CString				m_strAccountName;
	CString				m_strNickName;
	CString				m_strOperationDesc;

	CString				m_strOKButtonText;

	BOOL				m_bEnableOKButton;
	
};