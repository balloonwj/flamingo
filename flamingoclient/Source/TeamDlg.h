#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"

enum TEAM_OPERATION_TYPE
{
	TEAM_OPERATION_ADDTEAM,
	TEAM_OPERATION_DELETETEAM,
	TEAM_OPERATION_MODIFYTEAMNAME
};

class CFlamingoClient;

//新建分组对话框
class CTeamDlg : public CDialogImpl<CTeamDlg>
{
public:
	CTeamDlg();
	virtual ~CTeamDlg();

	//在资源文件里面增加对话框资源模板
	enum { IDD = IDD_TEAM };

	BEGIN_MSG_MAP_EX(CTeamDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
	END_MSG_MAP()

public:
	void SetType(TEAM_OPERATION_TYPE nType);
private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);

	BOOL InitUI();
	void UninitUI();

public:
	CFlamingoClient*		m_pFMGClient;
	long				m_nTeamIndex;

private:
	CSkinDialog			m_SkinDlg;
	CSkinStatic			m_staTeamName;
	CSkinEdit			m_edtTeamName;
	CSkinButton			m_btnOK;
	CSkinButton			m_btnCancel;

	long				m_nOperationType;
};