#pragma once
#include "resource.h"
#include "SkinLib/SkinLib.h"
#include <vector>

//class CIUProtocol;
class CFlamingoClient;

//自动解压下载下来的升级包文件，成功返回true，失败返回false
bool Unzip();

class CUpdateDlg : public CDialogImpl<CUpdateDlg>
{
public:
	CUpdateDlg();
	virtual ~CUpdateDlg();

	enum {IDD = IDD_UPDATEDLG};

	BEGIN_MSG_MAP_EX(CUpdateDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	BOOL InitUI();
	void UninitUI();
	
	void ParseFileInfo();	//解析升级包文件信息

public:
	CFlamingoClient*	     m_pFMGClient;
	std::vector<CString>	 m_aryFileInfo;

	std::vector<CString>	 m_aryFileDesc;		//升级包中文描述
	std::vector<CString>	 m_aryFileName;		//升级包名
	std::vector<CString>	 m_aryFileVersion;	//升级包版本号
	

public:
	CSkinDialog			m_SkinDlg;
	CSkinStatic			m_UpdateFileName, m_UpdateRate;
	CProgressBarCtrl	m_UpdateProgressBar;
	CSkinListCtrl		m_UpdateListCtrl;

	HANDLE				m_hDownloadThread;
	HANDLE				m_hExitEvent;					//对话框退出事件

	static	DWORD WINAPI DownloadThread(LPVOID lpParameter);
};