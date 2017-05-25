#pragma once

#include "resource.h"
#include "ImageEx.h"
#include "SkinLib/SkinLib.h"

class CVerifyCodeDlg : public CDialogImpl<CVerifyCodeDlg>
{
public:
	CVerifyCodeDlg(void);
	~CVerifyCodeDlg(void);

	enum { IDD = IDD_VERIFYCODEDLG };

	BEGIN_MSG_MAP_EX(CVerifyCodeDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_HANDLER_EX(ID_BTN_OK, BN_CLICKED, OnBtn_Ok)
		COMMAND_HANDLER_EX(ID_BTN_CANCEL, BN_CLICKED, OnBtn_Cancel)
		END_MSG_MAP()

public:
	void SetVerifyCodePic(const BYTE* lpData, DWORD dwSize);
	CString GetVerifyCode();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnBtn_Ok(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

private:
	CSkinDialog m_SkinDlg;
	CSkinPictureBox m_picVerifyCode;
	CSkinEdit m_edtVerifyCode;
	CSkinButton m_btnOk, m_btnCancel;

	CString m_strVerifyCode;
};
