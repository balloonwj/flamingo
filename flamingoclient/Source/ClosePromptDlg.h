#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"

class CFlamingoClient;

//退出提示对话框
class CClosePromptDlg : public CDialogImpl<CClosePromptDlg>
{
public:
	CClosePromptDlg();
	virtual ~CClosePromptDlg();

	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_CLOSEPROMPT };

	BEGIN_MSG_MAP_EX(CClosePromptDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		
		COMMAND_ID_HANDLER_EX(IDC_MINIMIZE, OnMinimize)
		COMMAND_ID_HANDLER_EX(IDC_EXIT, OnExit)
	END_MSG_MAP()

protected:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();

	void OnMinimize(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnExit(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
	
	CSkinPictureBox		m_picInfoIcon;
	CSkinStatic			m_staInfoText;
	CSkinButton			m_btnRememberChoice;
	CSkinButton			m_btnMinimize;
	CSkinButton			m_btnExit;
};