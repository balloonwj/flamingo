#pragma once

#include "resource.h"
#include "CustomMsgDef.h"
#include "SkinLib/SkinLib.h"
#include "ImageEx.h"
#include "UserSessionData.h"

class CFlamingoClient;

class CMsgTipDlg : public CDialogImpl<CMsgTipDlg>
{
public:
	CMsgTipDlg(void);
	~CMsgTipDlg(void);

	enum { IDD = IDD_MSGTIPDLG };

	BEGIN_MSG_MAP_EX(CMsgTipDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		//MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_SIZE(OnSize)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		NOTIFY_HANDLER_EX(1000, LVN_ITEMCHANGED, OnList_ItemChange)
		COMMAND_HANDLER_EX(ID_LINK_CANCEL_FLASH, BN_CLICKED, OnLnk_CancelFlash)
		COMMAND_HANDLER_EX(ID_LINK_SHOW_ALL, BN_CLICKED, OnLnk_ShowAll)
	END_MSG_MAP()

public:
	BOOL StartTrackMouseLeave();
	int FindMsgSender(long nType, UINT nSenderId);
	void AddMsgSender(long nType, UINT nSenderId);
	void DelMsgSender(long nType, UINT nSenderId);

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnTimer(UINT_PTR nIDEvent);
	void OnSize(UINT nType, CSize size);
	void OnClose();
	void OnDestroy();
	LRESULT OnList_Click(LPNMHDR pnmh);
	LRESULT OnList_ItemChange(LPNMHDR pnmh);
	void OnLnk_CancelFlash(UINT uNotifyCode, int nID, CWindow wndCtl);// “取消闪烁”超链接控件
	void OnLnk_ShowAll(UINT uNotifyCode, int nID, CWindow wndCtl);// “显示全部”超链接控件

	BOOL InitCtrls();			// 初始化控件
	BOOL UnInitCtrls();			// 反初始化控件
	void SetDlgAutoSize();		// 自动调整对话框大小
	void SetCtrlsAutoSize();	// 自动调整控件大小
	void _AddMsgSender(int nIndex, CMessageSender* lpMsgSender);
	tstring GetHeadPicFullName(UINT nGroupCode, UINT nUTalkUin);
	void GetNumber(UINT nGroupCode, UINT nUTalkUin, UINT& nGroupNum, UINT& nUTalkNum);

public:
	CFlamingoClient*	m_lpFMGClient;
	HWND			m_hMainDlg;
	CRect			m_rcTrayIcon;
	CRect			m_rcTrayIcon2;		//外部传进来的任务栏图标区域

private:
	CSkinDialog m_SkinDlg;
	CSkinListCtrl m_ListCtrl;
	CSkinHyperLink m_lnkCancelFlash, m_lnkShowAll;
	DWORD m_dwTimerId;
	int m_nListItemHeight;
};
