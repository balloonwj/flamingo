#include "stdafx.h"
#include "MsgTipDlg.h"
#include "UserSessionData.h"
#include "FlamingoClient.h"
#include "Path.h"
#include "Utils.h"

CMsgTipDlg::CMsgTipDlg(void)
{
	m_lpFMGClient = NULL;
	m_hMainDlg = NULL;
	m_rcTrayIcon.SetRectEmpty();
	m_rcTrayIcon2.SetRectEmpty();
	m_dwTimerId = NULL;
	m_nListItemHeight = 20;
}

CMsgTipDlg::~CMsgTipDlg(void)
{
}

BOOL CMsgTipDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	InitCtrls();			// 初始化控件
	SetDlgAutoSize();		// 自动调整对话框大小
	SetCtrlsAutoSize();		// 自动调整控件大小

	ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE);

	return FALSE;
}

BOOL CMsgTipDlg::OnEraseBkgnd(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	return TRUE;
}

void CMsgTipDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_dwTimerId)
	{
		RECT rcWindow;
		GetWindowRect(&rcWindow);

		POINT pt = {0};
		::GetCursorPos(&pt);

		if (!::PtInRect(&rcWindow, pt) && !::PtInRect(&m_rcTrayIcon2, pt))
		{
			KillTimer(m_dwTimerId);
			m_dwTimerId = NULL;
			DestroyWindow();
		}
	}
}

void CMsgTipDlg::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	SIZE szRoundCorner = {4,4};
	if (!IsIconic()) 
	{
		if (szRoundCorner.cx != 0 || szRoundCorner.cy != 0)
		{
			RECT rcWindow;
			::GetWindowRect(m_hWnd, &rcWindow);
			::OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
			rcWindow.right++;
			rcWindow.bottom++;
			HRGN hRgn = ::CreateRoundRectRgn(rcWindow.left, 
				rcWindow.top, rcWindow.right, rcWindow.bottom, 
				szRoundCorner.cx, szRoundCorner.cy);
			::SetWindowRgn(m_hWnd, hRgn, TRUE);
			::DeleteObject(hRgn);
		}
	}
}

void CMsgTipDlg::OnClose()
{
	DestroyWindow();
}

void CMsgTipDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	UnInitCtrls();			// 反初始化控件

	if (m_dwTimerId != NULL)
	{
		KillTimer(m_dwTimerId);
		m_dwTimerId = NULL;
	}
}

LRESULT CMsgTipDlg::OnList_Click(LPNMHDR pnmh)
{
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pnmh;
	if (pNMListView->iItem != -1)
	{
		CString strType = m_ListCtrl.GetItemText(pNMListView->iItem, 3);
		UINT nType = _tcstol(strType, NULL, 10);
		UINT nSenderId = m_ListCtrl.GetItemData(pNMListView->iItem, 0);

		switch (nType)
		{
		case FMG_MSG_TYPE_BUDDY:
			::SendMessage(m_hMainDlg, WM_SHOW_BUDDYCHATDLG, 0, nSenderId);
			break;
		case FMG_MSG_TYPE_GROUP:
			::SendMessage(m_hMainDlg, WM_SHOW_GROUPCHATDLG, nSenderId, 0);
			break;
		case FMG_MSG_TYPE_SESS:
			{
				if (m_lpFMGClient != NULL)
				{
					CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
					if (lpMsgList != NULL)
					{
						CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_SESS, nSenderId);
						if (lpMsgSender != NULL)
							::SendMessage(m_hMainDlg, WM_SHOW_SESSCHATDLG, lpMsgSender->GetGroupCode(), nSenderId);
					}
				}	
			}
			break;
		}
	}
	return 0;
}

LRESULT CMsgTipDlg::OnList_ItemChange(LPNMHDR pnmh)
{
	int nIndex = m_ListCtrl.GetCurSelItemIndex();
	if (nIndex != -1)
	{
		CString strType = m_ListCtrl.GetItemText(nIndex, 3);
		UINT nType = _tcstol(strType, NULL, 10);
		UINT nSenderId = m_ListCtrl.GetItemData(nIndex, 0);

		switch (nType)
		{
		case FMG_MSG_TYPE_BUDDY:
			::SendMessage(m_hMainDlg, WM_SHOW_BUDDYCHATDLG, 0, nSenderId);
			break;
		case FMG_MSG_TYPE_GROUP:
			::SendMessage(m_hMainDlg, WM_SHOW_GROUPCHATDLG, nSenderId, 0);
			break;
		case FMG_MSG_TYPE_SESS:
			{
				if (m_lpFMGClient != NULL)
				{
					CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
					if (lpMsgList != NULL)
					{
						CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_SESS, nSenderId);
						if (lpMsgSender != NULL)
							::SendMessage(m_hMainDlg, WM_SHOW_SESSCHATDLG, lpMsgSender->GetGroupCode(), nSenderId);
					}
				}	
			}
			break;
		//case FMG_MSG_TYPE_SYSGROUP:
		//	::SendMessage(m_hMainDlg, WM_SHOW_SYSGROUPCHATDLG, nSenderId, 0);
		//	break;
		}
	}
	return 0;
}

// “取消闪烁”超链接控件
void CMsgTipDlg::OnLnk_CancelFlash(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	::PostMessage(m_hMainDlg, WM_CANCEL_FLASH, 0, 0);
	//SendMessage(WM_CLOSE);
}

// “显示全部”超链接控件
void CMsgTipDlg::OnLnk_ShowAll(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	int nCount = m_ListCtrl.GetItemCount();
	for (int i = 0; i < nCount; i++)
	{
		CString strType = m_ListCtrl.GetItemText(i, 3);
		UINT nType = _tcstol(strType, NULL, 10);
		UINT nSenderId = m_ListCtrl.GetItemData(i, 0);

		switch (nType)
		{
		case FMG_MSG_TYPE_BUDDY:
			::SendMessage(m_hMainDlg, WM_SHOW_BUDDYCHATDLG, 0, nSenderId);
			break;
		case FMG_MSG_TYPE_GROUP:
			::SendMessage(m_hMainDlg, WM_SHOW_GROUPCHATDLG, nSenderId, 0);
			break;
		case FMG_MSG_TYPE_SESS:
			{
				if (m_lpFMGClient != NULL)
				{
					CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
					if (lpMsgList != NULL)
					{
						CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_SESS, nSenderId);
						if (lpMsgSender != NULL)
							::SendMessage(m_hMainDlg, WM_SHOW_SESSCHATDLG, lpMsgSender->GetGroupCode(), nSenderId);
					}
				}	
			}
			break;
		//case FMG_MSG_TYPE_SYSGROUP:
		//	::SendMessage(m_hMainDlg, WM_SHOW_SYSGROUPCHATDLG, nSenderId, 0);
		//	break;
		}
	}
}

BOOL CMsgTipDlg::StartTrackMouseLeave()
{
	m_dwTimerId = SetTimer(990, 160, NULL);
	return m_dwTimerId != NULL ? TRUE : FALSE;
}

int CMsgTipDlg::FindMsgSender(long nType, UINT nSenderId)
{
	CString strType;
	UINT nType2, nSenderId2;

	int nCount = m_ListCtrl.GetItemCount();
	for (int i = 0; i < nCount; i++)
	{
		strType = m_ListCtrl.GetItemText(i, 3);
		nType2 = _tcstol(strType, NULL, 10);
		nSenderId2 = m_ListCtrl.GetItemData(i, 0);

		if (nType == nType2 && nSenderId == nSenderId2)
			return i;
	}
	return -1;
}

void CMsgTipDlg::AddMsgSender(long nType, UINT nSenderId)
{
	CMessageSender* lpMsgSender = NULL;
	if (m_lpFMGClient != NULL)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
			lpMsgSender = lpMsgList->GetMsgSender(nType, nSenderId);
	}
	if (NULL == lpMsgSender)
		return;

	int nItemIndex = FindMsgSender(nType, nSenderId);
	if (nItemIndex != -1)
	{
		int nMsgCnt = lpMsgSender->GetMsgCount();
		CString strMsgCnt;
		strMsgCnt.Format(_T("(%d)"), nMsgCnt);
		m_ListCtrl.SetItemText(nItemIndex, 2, strMsgCnt);
	}
	else
	{
		SetDlgAutoSize();		// 自动调整对话框大小
		SetCtrlsAutoSize();		// 自动调整控件大小
		_AddMsgSender(0, lpMsgSender);
	}
}

void CMsgTipDlg::DelMsgSender(long nType, UINT nSenderId)
{
	int nItemIndex = FindMsgSender(nType, nSenderId);
	if (nItemIndex != -1)
		m_ListCtrl.DeleteItem(nItemIndex);
	SetDlgAutoSize();	// 自动调整对话框大小
	SetCtrlsAutoSize();		// 自动调整控件大小
}

// 初始化控件
BOOL CMsgTipDlg::InitCtrls()
{
	m_SkinDlg.SubclassWindow(m_hWnd);
	tstring strFileName = Hootina::CPath::GetAppPath() + _T("Image\\MsgTipBg.png");	// 加载背景图片
	m_SkinDlg.SetBgPic(strFileName.c_str(), CRect(8,28,8,8));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	HFONT hFont = (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0L);

	m_lnkCancelFlash.SubclassWindow(GetDlgItem(ID_LINK_CANCEL_FLASH));
	m_lnkCancelFlash.SetTransparent(TRUE, hDlgBgDC);
	m_lnkCancelFlash.SetLinkColor(RGB(0,114,193));
	m_lnkCancelFlash.SetHoverLinkColor(RGB(0,114,193));
	m_lnkCancelFlash.SetVisitedLinkColor(RGB(0,114,193));
	m_lnkCancelFlash.SetLabel(_T("取消闪烁"));
	m_lnkCancelFlash.SetHoverFont(hFont);

	m_lnkShowAll.SubclassWindow(GetDlgItem(ID_LINK_SHOW_ALL));
	m_lnkShowAll.SetTransparent(TRUE, hDlgBgDC);
	m_lnkShowAll.SetLinkColor(RGB(0,114,193));
	m_lnkShowAll.SetHoverLinkColor(RGB(0,114,193));
	m_lnkShowAll.SetVisitedLinkColor(RGB(0,114,193));
	m_lnkShowAll.SetLabel(_T("查看全部"));
	m_lnkShowAll.SetHoverFont(hFont);

	m_ListCtrl.Create(m_hWnd, CRect(0,0,1,1), NULL, 
		WS_CHILD|WS_VISIBLE, NULL, 1000, NULL);
	m_ListCtrl.SetTransparent(TRUE, hDlgBgDC);
	m_ListCtrl.SetItemHoverBgPic(_T("MsgTipListItemBg.png"));
	m_ListCtrl.SetItemTextColor(RGB(0,0,0));
	m_ListCtrl.SetSelItemTextColor(RGB(0,0,0));
	m_ListCtrl.SetHeadVisible(FALSE);
	m_ListCtrl.SetItemImageSize(16, 16);

//	m_ListCtrl.AddColumn(_T("空格"), NULL, DT_LEFT, 7);
	m_ListCtrl.AddColumn(_T("图标"), NULL, DT_LEFT, 30);
	m_ListCtrl.AddColumn(_T("发送者"), NULL, DT_LEFT, 136);
	m_ListCtrl.AddColumn(_T("消息数"), NULL, DT_RIGHT, 38);
	m_ListCtrl.AddColumn(_T("类型"), NULL, DT_RIGHT, 0);

	m_ListCtrl.DeleteAllItems();
	if (m_lpFMGClient != NULL)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			int nMsgSenderCnt = lpMsgList->GetMsgSenderCount();
			for (int i = 0; i < nMsgSenderCnt; i++)
			{
				CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(i);
				if (lpMsgSender != NULL)
				{
					_AddMsgSender(0, lpMsgSender);
				}
			}
		}
	}

	return TRUE;
}

// 反初始化控件
BOOL CMsgTipDlg::UnInitCtrls()
{
	if (m_ListCtrl.IsWindow())
		m_ListCtrl.DestroyWindow();

	if (m_lnkCancelFlash.IsWindow())
		m_lnkCancelFlash.DestroyWindow();

	if (m_lnkShowAll.IsWindow())
		m_lnkShowAll.DestroyWindow();

	return TRUE;
}

// 自动调整对话框大小
void CMsgTipDlg::SetDlgAutoSize()
{
	int nMsgSenderCnt = 0;
	if (m_lpFMGClient != NULL)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
			nMsgSenderCnt = lpMsgList->GetMsgSenderCount();
	}

	int cyListCtrl = nMsgSenderCnt* m_nListItemHeight + 5;

	int cxDlg = 212;
	int cyDlg = cyListCtrl + 36 + 22;

	int cxScreen = ::GetSystemMetrics(SM_CXFULLSCREEN);
	int cyScreen = ::GetSystemMetrics(SM_CYFULLSCREEN);

	CRect rcTrayWnd;
	GetTrayWndRect(&rcTrayWnd);

	int nLeft, nTop;

	if (rcTrayWnd.top == rcTrayWnd.left && rcTrayWnd.bottom > rcTrayWnd.right)	// 左边
	{
		nLeft = rcTrayWnd.right + 2;
		nTop = m_rcTrayIcon.top - (cyDlg - m_rcTrayIcon.Width()) / 2;
		if (nTop + cyDlg > cyScreen)
			nTop = cyScreen - cyDlg;
	}
	else if (rcTrayWnd.top == rcTrayWnd.left && rcTrayWnd.bottom < rcTrayWnd.right)	// 上边
	{
		nLeft = m_rcTrayIcon.left - (cxDlg - m_rcTrayIcon.Width()) / 2;
		nTop = rcTrayWnd.bottom + 2;
		if (nLeft + cxDlg > cxScreen)
			nLeft = cxScreen - cxDlg;
	}
	else if (rcTrayWnd.top > rcTrayWnd.left)	// 下边
	{
		nLeft = m_rcTrayIcon.left - (cxDlg - m_rcTrayIcon.Width()) / 2;
		nTop = rcTrayWnd.top - cyDlg - 2;
		if (nLeft + cxDlg > cxScreen)
			nLeft = cxScreen - cxDlg;
	}
	else	// 右边
	{
		nLeft = rcTrayWnd.left - cxDlg - 2;
		nTop = m_rcTrayIcon.top - (cyDlg - m_rcTrayIcon.Width()) / 2;
		if (nTop + cyDlg > cyScreen)
			nTop = cyScreen - cyDlg;
	}

	SetWindowPos(HWND_TOPMOST, nLeft, nTop, cxDlg, cyDlg, SWP_NOACTIVATE);
	Invalidate();
}

// 自动调整控件大小
void CMsgTipDlg::SetCtrlsAutoSize()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	int nMsgSenderCnt = 0;
	if (m_lpFMGClient != NULL)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
			nMsgSenderCnt = lpMsgList->GetMsgSenderCount();
	}

	int cxListCtrl = 204;
	int cyListCtrl = nMsgSenderCnt* m_nListItemHeight + 5;

	if (m_ListCtrl.IsWindow())
		m_ListCtrl.MoveWindow(4, 36, cxListCtrl, cyListCtrl);
	if (m_lnkCancelFlash.IsWindow())
		m_lnkCancelFlash.MoveWindow(12, rcClient.bottom - 22, 48, 14);
	if (m_lnkShowAll.IsWindow())
		m_lnkShowAll.MoveWindow(156, rcClient.bottom - 22, 48, 14);
}

void CMsgTipDlg::_AddMsgSender(int nIndex, CMessageSender* lpMsgSender)
{
	if (NULL == lpMsgSender || NULL == m_lpFMGClient)
		return;

	CString strHeadPicFileName;
	CString strSenderName, strMsgCnt, strMsgType;

	long nMsgType = lpMsgSender->GetMsgType();
	UINT nMsgCnt = lpMsgSender->GetMsgCount();
	UINT nSenderId = lpMsgSender->GetSenderId();
	UINT nGroupCode = lpMsgSender->GetGroupCode();

	switch (nMsgType)
	{
	case FMG_MSG_TYPE_BUDDY:
		{
			//strHeadPicFileName = GetHeadPicFullName(0, nSenderId);
			//if (!Hootina::CPath::IsFileExist(strHeadPicFileName.c_str()))
			//	strHeadPicFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefBuddyHeadPic.png");
			CBuddyInfo* pBuddyInfo = m_lpFMGClient->m_UserMgr.m_BuddyList.GetBuddy(nSenderId);
			if(pBuddyInfo != NULL)
			{
				if(pBuddyInfo!=NULL && pBuddyInfo->m_bUseCustomFace && pBuddyInfo->m_bCustomFaceAvailable)
				{
					strHeadPicFileName.Format(_T("%s%d.png"), m_lpFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), pBuddyInfo->m_uUserID);
					if(!Hootina::CPath::IsFileExist(strHeadPicFileName))
						strHeadPicFileName.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, pBuddyInfo->m_nFace);
				}
				else
					strHeadPicFileName.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, pBuddyInfo->m_nFace);
			}

			CBuddyList* lpBuddyList = m_lpFMGClient->GetBuddyList();
			if (lpBuddyList != NULL)
			{
				CBuddyInfo* lpBuddyInfo = lpBuddyList->GetBuddy(nSenderId);
				if (lpBuddyInfo != NULL)
				{
					CString strBuddyName;

					if (!lpBuddyInfo->m_strMarkName.empty())
						strBuddyName = lpBuddyInfo->m_strMarkName.c_str();
					else
						strBuddyName = lpBuddyInfo->m_strNickName.c_str();

					if (lpBuddyInfo->m_uUserID != 0)
						strSenderName.Format(_T("%s"), strBuddyName);
				}
			}
		}
		break;
	case FMG_MSG_TYPE_GROUP:
		{
			//strHeadPicFileName = GetHeadPicFullName(nSenderId, 0);
			//if (!Hootina::CPath::IsFileExist(strHeadPicFileName.c_str()))
				//strHeadPicFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefGroupHeadPic.png");
			strHeadPicFileName.Format(_T("%sImage\\DefGroupHeadPic.png"), g_szHomePath);

			CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
			if (lpGroupList != NULL)
			{
				CGroupInfo* lpGroupInfo = lpGroupList->GetGroupByCode(nSenderId);
				if (lpGroupInfo != NULL)
				{
					if (lpGroupInfo->m_nGroupCode != 0)
						strSenderName.Format(_T("%s"), lpGroupInfo->m_strName.c_str());
				}
			}
		}
		break;
	case FMG_MSG_TYPE_SESS:
		{
			//strHeadPicFileName = GetHeadPicFullName(nGroupCode, nSenderId);
			//if (!Hootina::CPath::IsFileExist(strHeadPicFileName.c_str()))
			//	strHeadPicFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefBuddyHeadPic.png");

			strHeadPicFileName.Format(_T("%sImage\\DefBuddyHeadPic.png"), g_szHomePath);

			CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
			if (lpGroupList != NULL)
			{
				CBuddyInfo* lpBuddyInfo = lpGroupList->GetGroupMemberByCode(nGroupCode, nSenderId);
				if (lpBuddyInfo != NULL)
				{
					CString strBuddyName;

					strBuddyName = lpBuddyInfo->m_strNickName.c_str();

					if (lpBuddyInfo->m_uUserID != 0)
						strSenderName.Format(_T("%s(%u)"), strBuddyName, lpBuddyInfo->m_uUserID);
					else
						strSenderName.Format(_T("%s"), strBuddyName);
				}
			}
		}
		break;
	}
	long nMsgDisplayCnt = lpMsgSender->GetDisplayMsgCount();
	strMsgCnt.Format(_T("(%d)"), nMsgDisplayCnt);
	strMsgType.Format(_T("%d"), nMsgType);

	int nItem = m_ListCtrl.InsertItem(nIndex, _T(""), _T(""), FALSE, DT_LEFT, nSenderId);
	m_ListCtrl.SetItemFormat(nIndex, 0, DT_LEFT);
	m_ListCtrl.SetItemFormat(nIndex, 1, DT_LEFT);
	m_ListCtrl.SetItemFormat(nIndex, 2, DT_RIGHT);
	m_ListCtrl.SetItemFormat(nIndex, 3, DT_RIGHT);
	m_ListCtrl.SetItemImage(nIndex, 0, strHeadPicFileName);
	m_ListCtrl.SetItemText(nIndex, 1, strSenderName);
	m_ListCtrl.SetItemText(nIndex, 2, strMsgCnt);
	m_ListCtrl.SetItemText(nIndex, 3, strMsgType);
}

tstring CMsgTipDlg::GetHeadPicFullName(UINT nGroupCode, UINT nUTalkUin)
{
	UINT nGroupNum, nUTalkNum;
	GetNumber(nGroupCode, nUTalkUin, nGroupNum, nUTalkNum);

	if (nGroupCode != 0 && nUTalkUin != 0)	// 群成员
		return m_lpFMGClient->GetSessHeadPicFullName(nUTalkNum).c_str();
	else if (nGroupCode != 0)			// 群
		return m_lpFMGClient->GetGroupHeadPicFullName(nGroupNum).c_str();
	else								// 好友
		return m_lpFMGClient->GetBuddyHeadPicFullName(nUTalkNum).c_str();
}

void CMsgTipDlg::GetNumber(UINT nGroupCode, UINT nUTalkUin, UINT& nGroupNum, UINT& nUTalkNum)
{
	nGroupNum = nUTalkNum = 0;

	if (nGroupCode != 0 && nUTalkUin != 0)
	{
		CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
		if (lpGroupList != NULL)
		{
			CBuddyInfo* lpBuddyInfo = lpGroupList->GetGroupMemberByCode(nGroupCode, nUTalkUin);
			if (lpBuddyInfo != NULL)
				nUTalkNum = lpBuddyInfo->m_uUserID;
		}
	}
	else if (nGroupCode != 0)
	{
		CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
		if (lpGroupList != NULL)
		{
			CGroupInfo* lpGroupInfo = lpGroupList->GetGroupByCode(nGroupCode);
			if (lpGroupInfo != NULL)
				nGroupNum = lpGroupInfo->m_nGroupNumber;
		}
	}
	else if (nUTalkUin != 0)
	{
		CBuddyList* lpBuddyList = m_lpFMGClient->GetBuddyList();
		if (lpBuddyList != NULL)
		{
			CBuddyInfo* lpBuddyInfo = lpBuddyList->GetBuddy(nUTalkUin);
			if (lpBuddyInfo != NULL)
				nUTalkNum = lpBuddyInfo->m_uUserID;
		}
	}
}