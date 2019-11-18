#include "stdafx.h"
#include "GroupChatDlg.h"
#include "UserSessionData.h"
#include "FlamingoClient.h"
#include "Time.h"
#include "ChatDlgCommon.h"
#include "GDIFactory.h"
#include "IULog.h"
#include "File2.h"
#include "EncodeUtil.h"
#include "LoginSettingsDlg.h"
#include "UIText.h"

#define CHAT_BG_IMAGE_NAME			_T("BuddyChatDlgBg.png")
#define CHAT_EXPAND_BG_IMAGE_NAME   _T("BuddyChatDlgExpandBg.png")

const long GROUPCHATDLG_WIDTH			= 670/*587*/;
const long GROUPCHATDLG_HEIGHT			= 600/*535*/;
const long GROUP_MEMBER_LIST_WIDTH		= 200;
const long GROUPCHATDLG_EXTEND_WIDTH	= 1043;
const long GROUP_MSG_LOG_WIDTH			= 373;

CGroupChatDlg::CGroupChatDlg(void)
{
	m_lpFMGClient = NULL;
	m_lpFaceList = NULL;
	m_lpCascadeWinManager = NULL;
	m_hMainDlg = NULL;
	m_nGroupCode = 0;

	m_hDlgIcon = m_hDlgSmallIcon = NULL;
	m_hRBtnDownWnd = NULL;
	memset(&m_ptRBtnDown, 0, sizeof(m_ptRBtnDown));
	m_pLastImageOle = NULL;
	m_cxPicBarDlg = 122;
	m_cyPicBarDlg = 24;

	m_nGroupId = m_nGroupNumber = 0;
	m_strGroupName = _T("群名称");
	m_strUserName = _T("");
	m_nMemberCnt = m_nOnlineMemberCnt = 0;


	m_HotRgn = NULL;

	m_bPressEnterToSendMessage = TRUE;
	m_bMsgLogWindowVisible = FALSE;
	m_nMsgLogIndexInToolbar = -1;
	
	m_nMsgLogRecordOffset = 1;	
	m_nMsgLogCurrentPageIndex = 0;

	m_bDraged = FALSE;

	::SetRectEmpty(&m_rtRichRecv);
	::SetRectEmpty(&m_rtMidToolBar);
	::SetRectEmpty(&m_rtSplitter);
	::SetRectEmpty(&m_rtRichSend);
}

CGroupChatDlg::~CGroupChatDlg(void)
{
}

BOOL CGroupChatDlg::PreTranslateMessage(MSG* pMsg)
{
	//if (::GetForegroundWindow() == m_hWnd && !m_Accelerator.IsNull() && 
	//	m_Accelerator.TranslateAccelerator(m_hWnd, pMsg))
	//	return TRUE;

	if (pMsg->hwnd == m_richRecv.m_hWnd || pMsg->hwnd == m_richSend.m_hWnd || pMsg->hwnd == m_richMsgLog.m_hWnd)
	{
		if (pMsg->message == WM_MOUSEMOVE)			// 发送/接收文本框的鼠标移动消息
		{
			if (OnRichEdit_MouseMove(pMsg))
				return TRUE;
		}
		else if (pMsg->message == WM_LBUTTONDBLCLK) // 发送/接收文本框的鼠标双击消息
		{
			if (OnRichEdit_LBtnDblClk(pMsg))
				return TRUE;
		}
		else if (pMsg->message == WM_RBUTTONDOWN)	// 发送/接收文本框的鼠标右键按下消息
		{
			if (OnRichEdit_RBtnDown(pMsg))
				return TRUE;
		}
		
		if ((pMsg->hwnd == m_richSend.m_hWnd) && (pMsg->message == WM_KEYDOWN) 
			&& (pMsg->wParam == 'V'))	// 发送文本框的Ctrl+V消息
		{
			BOOL bCtrlPressed = ::GetAsyncKeyState(VK_CONTROL) & 0x8000;
			if(bCtrlPressed)
			{
				m_richSend.PasteSpecial(CF_TEXT);
				return TRUE;
			}
		}

		if ((pMsg->hwnd == m_richSend.m_hWnd) && (pMsg->message == WM_KEYDOWN) && (pMsg->wParam == VK_RETURN) )	
		{
			BOOL bCtrlPressed = ::GetAsyncKeyState(VK_CONTROL) & 0x8000;
			if(m_bPressEnterToSendMessage && !bCtrlPressed)
			{
				::SendMessage(m_hWnd, WM_COMMAND, ID_BTN_SEND, 0);
				return TRUE;
			}
			else if(m_bPressEnterToSendMessage && bCtrlPressed)
			{
				::SendMessage(m_richSend.m_hWnd, WM_KEYDOWN, VK_RETURN, 0);
				return TRUE;
			}
			else if(!m_bPressEnterToSendMessage && bCtrlPressed)
			{
				::SendMessage(m_hWnd, WM_COMMAND, ID_BTN_SEND, 0);
				return TRUE;
			}
			else if(!m_bPressEnterToSendMessage && !bCtrlPressed)
			{
				::SendMessage(m_richSend.m_hWnd, WM_KEYDOWN, VK_RETURN, 0);
				return TRUE;
			}
		}

	}

	return CWindow::IsDialogMessage(pMsg);
}

// 接收群消息
void CGroupChatDlg::OnRecvMsg(UINT nGroupCode, UINT nMsgId)
{
	if (NULL == m_lpFMGClient || m_nGroupCode != nGroupCode)
		return;

	if (::GetForegroundWindow() != m_hWnd)
		FlashWindowEx(m_hWnd, 3);

	if (nMsgId == 0)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
			if (lpMsgSender != NULL)
			{
				int nMsgCnt = lpMsgSender->GetMsgCount();
				for (int i = 0; i < nMsgCnt; i++)
				{
					//CGroupMessage* lpGroupMsg = lpMsgSender->GetGroupMsg(i);
					CBuddyMessage* lpGroupMsg = lpMsgSender->GetGroupMsg(i);
					if (lpGroupMsg != NULL)
					{
						AddMsgToRecvEdit(lpGroupMsg);
					}
				}
				lpMsgList->DelMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
				::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_GROUP, nGroupCode);
			}
		}
	}
	else
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
			if (lpMsgSender != NULL)
			{
				CBuddyMessage* lpGroupMsg = lpMsgSender->GetGroupMsgById(nMsgId);
				if (lpGroupMsg != NULL)
				{
					AddMsgToRecvEdit(lpGroupMsg);
					lpMsgSender->DelMsgById(nMsgId);
				}

				if (lpMsgSender->GetMsgCount() <= 0)
				{
					lpMsgList->DelMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
					::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_GROUP, nGroupCode);
				}
			}
		}
	}
}

// 更新群信息
void CGroupChatDlg::OnUpdateGroupInfo()
{
	UpdateData();						// 更新信息

	UpdateDlgTitle();					// 更新对话框标题
	UpdateGroupNameCtrl();				// 更新群名称控件
	UpdateGroupMemo();					// 更新群公告
	UpdateGroupMemberList();			// 更新群成员列表
}

// 更新群号码
void CGroupChatDlg::OnUpdateGroupNumber()
{
	UpdateData();						// 更新信息
	UpdateGroupNameCtrl();				// 更新群名称控件
}

// 更新群头像
void CGroupChatDlg::OnUpdateGroupHeadPic()
{
	tstring strFileName;
	if (m_lpFMGClient != NULL && m_nGroupNumber != 0)
		strFileName = m_lpFMGClient->GetGroupHeadPicFullName(m_nGroupNumber);
	if (!Hootina::CPath::IsFileExist(strFileName.c_str()))
		strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefGroupHeadPic.png");
	m_picHead.SetBitmap(strFileName.c_str());
	m_picHead.Invalidate();
}

// 更新群成员号码
void CGroupChatDlg::OnUpdateGMemberNumber(WPARAM wParam, LPARAM lParam)
{
	UINT nGroupCode = (UINT)wParam;
	UINT nUTalkUin = (UINT)lParam;

	CBuddyInfo* lpBuddyInfo = GetGMemberInfoPtr(nUTalkUin);
	if (NULL == lpBuddyInfo || 0 == lpBuddyInfo->m_uUserID)
		return;

	CString strNickName, strText, strFileName;

	strNickName = lpBuddyInfo->m_strNickName.c_str();

	strText.Format(_T("%s(%u)"), strNickName, lpBuddyInfo->m_uUserID);

	BOOL bGray = FALSE;
	if(lpBuddyInfo->m_nStatus==STATUS_OFFLINE || lpBuddyInfo->m_nStatus==STATUS_INVISIBLE)
		bGray = TRUE;
	strFileName = m_lpFMGClient->GetSessHeadPicFullName(lpBuddyInfo->m_uUserID).c_str();

	int nItem = FindMemberListByUin(nUTalkUin);
	if (nItem != -1)
	{
		m_ListCtrl.SetItemText(nItem, 0, strText);
		if (Hootina::CPath::IsFileExist(strFileName))
			m_ListCtrl.SetItemImage(nItem, 0, strFileName, bGray);
	}
}

// 更新群成员头像
void CGroupChatDlg::OnUpdateGMemberHeadPic(WPARAM wParam, LPARAM lParam)
{
	UINT nGroupCode = (UINT)wParam;
	UINT nUTalkUin = (UINT)lParam;

	CBuddyInfo* lpBuddyInfo = GetGMemberInfoPtr(nUTalkUin);
	if (NULL == lpBuddyInfo || 0 == lpBuddyInfo->m_uUserID)
		return;

	BOOL bGray = FALSE;
	if(lpBuddyInfo->m_nStatus==STATUS_OFFLINE || lpBuddyInfo->m_nStatus==STATUS_INVISIBLE)
		bGray = TRUE;

	CString strFileName = m_lpFMGClient->GetSessHeadPicFullName(lpBuddyInfo->m_uUserID).c_str();
	if (!Hootina::CPath::IsFileExist(strFileName))
		return;
	
	int nItem = FindMemberListByUin(nUTalkUin);
	if (nItem != -1)
	{
		m_ListCtrl.SetItemImage(nItem, 0, strFileName, bGray);
	}
}

BOOL CGroupChatDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	m_lpCascadeWinManager->Add(m_hWnd, GROUPCHATDLG_WIDTH, GROUPCHATDLG_HEIGHT);

	// set icons
	m_hDlgIcon = AtlLoadIconImage(IDI_GROUPCHATDLG_32, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDI_GROUPCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);

	m_FontSelDlg.m_pFMGClient = m_lpFMGClient;

	UpdateData();
	Init();

	//允许拖拽文件进窗口
	::DragAcceptFiles(m_hWnd, TRUE); 

	SetHotRgn();

	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		//if (!lpGroupInfo->IsHasGroupInfo())		// 更新群信息
			OnUpdateGroupInfo();

		if (!lpGroupInfo->IsHasGroupNumber())	// 更新群号码
		{
			m_lpFMGClient->UpdateGroupNum(m_nGroupCode);
		}
		else									// 更新群头像
		{
			if (m_lpFMGClient->IsNeedUpdateGroupHeadPic(lpGroupInfo->m_nGroupNumber))
				m_lpFMGClient->UpdateGroupHeadPic(m_nGroupCode, lpGroupInfo->m_nGroupNumber);
		}
	}

	PostMessage(WM_SETDLGINITFOCUS, 0, 0);		// 设置对话框初始焦点
	SetTimer(1001, 300, NULL);

	m_bPressEnterToSendMessage = m_lpFMGClient->m_UserConfig.IsEnablePressEnterToSend();

	CalculateMsgLogCountAndOffset();

	if(m_lpFMGClient->m_UserConfig.IsEnableShowLastMsgInChatDlg())
		ShowLastMsgInRecvRichEdit();

	return TRUE;
}

BOOL CGroupChatDlg::OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct)
{
	if (NULL == pCopyDataStruct)
		return FALSE;

	switch (pCopyDataStruct->dwData)
	{
	case IPC_CODE_MSGLOG_PASTE:			// 消息记录浏览窗口粘贴消息
		{
			if (pCopyDataStruct->lpData != NULL && pCopyDataStruct->cbData > 0)
				AddMsgToSendEdit((LPCTSTR)pCopyDataStruct->lpData);
		}
		break;

	case IPC_CODE_MSGLOG_EXIT:			// 消息记录浏览窗口退出消息
		{
			m_tbMid.SetItemCheckState(11, FALSE);
			m_tbMid.Invalidate();
		}
		break;
	}

	return TRUE;
}

void CGroupChatDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	m_SkinMenu.OnMeasureItem(nIDCtl, lpMeasureItemStruct);
}

void CGroupChatDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	m_SkinMenu.OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CGroupChatDlg::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	if(m_bMsgLogWindowVisible)
	{
		lpMMI->ptMinTrackSize.x = GROUPCHATDLG_EXTEND_WIDTH;
		lpMMI->ptMinTrackSize.y = GROUPCHATDLG_HEIGHT;
	}
	else
	{
		lpMMI->ptMinTrackSize.x = GROUPCHATDLG_WIDTH;
		lpMMI->ptMinTrackSize.y = GROUPCHATDLG_HEIGHT;
	}
}

void CGroupChatDlg::OnMove(CPoint ptPos)
{
	SetMsgHandled(FALSE);

	m_lpCascadeWinManager->SetPos(m_hWnd, ptPos.x, ptPos.y);
}

void CGroupChatDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (::GetCapture() == m_SplitterCtrl.m_hWnd)
	{
		ReCaculateCtrlPostion(point.y);
	}
}

void CGroupChatDlg::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	CRect rcClient;
	GetClientRect(&rcClient);
	
	if(!m_bMsgLogWindowVisible)
	{
		if (m_tbTop.IsWindow())
			m_tbTop.MoveWindow(3, 70, size.cx-1, 32, TRUE);

		if (m_staMemoTitle.IsWindow())
			m_staMemoTitle.MoveWindow(rcClient.right-195, 98, 193, 28, FALSE);

		if (m_staMemberTitle.IsWindow())
			m_staMemberTitle.MoveWindow(rcClient.right-195, 267, 193, 30, FALSE);

		if (m_edtMemo.IsWindow())
			m_edtMemo.MoveWindow(rcClient.right-195, 124, 193, 145, FALSE);

		if (m_btnClose.IsWindow())
			m_btnClose.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-208, size.cy-30, 77, 25, TRUE);

		if (m_btnSend.IsWindow())
			m_btnSend.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-118, size.cy-30, 77, 25, TRUE);

		if (m_btnArrow.IsWindow())
			m_btnArrow.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-42, size.cy-30, 28, 25, TRUE);

		if (m_richRecv.IsWindow())
		{
			if(m_bDraged)
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2, m_rtRichRecv.bottom-m_rtRichRecv.top-32);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2, m_rtRichRecv.bottom-m_rtRichRecv.top, TRUE);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, m_rtRichRecv.bottom-32, size.cx-20, 32, TRUE);
			}
			else
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-20, size.cy-305);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-20, size.cy-273, TRUE);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
                    m_FontSelDlg.MoveWindow(2, size.cy - 197, size.cx-GROUP_MEMBER_LIST_WIDTH/*-40*/, 32, TRUE);
			}
		}
		
		if (m_tbMid.IsWindow())
		{
			if(m_bDraged)
				m_tbMid.MoveWindow(3, m_rtMidToolBar.top, size.cx-GROUP_MEMBER_LIST_WIDTH, 31, TRUE);
			else
				m_tbMid.MoveWindow(3, size.cy-167, size.cx-GROUP_MEMBER_LIST_WIDTH, 31, TRUE);
			//消息记录按钮始终靠边
			m_tbMid.SetItemMargin(m_nMsgLogIndexInToolbar, size.cx-GROUP_MEMBER_LIST_WIDTH-200, 0);
		}

		if(m_SplitterCtrl.IsWindow())
		{
			if(m_bDraged)
				m_SplitterCtrl.MoveWindow(6, m_rtSplitter.top, size.cx-GROUP_MEMBER_LIST_WIDTH-5, 5, TRUE);	
			else
				m_SplitterCtrl.MoveWindow(6, size.cy-133, size.cx-GROUP_MEMBER_LIST_WIDTH-5, 5, TRUE);	
		}

		if (m_richSend.IsWindow())
		{
			if(m_bDraged)
				m_richSend.MoveWindow(6, m_rtRichSend.top, size.cx-GROUP_MEMBER_LIST_WIDTH-5, size.cy-m_rtRichSend.top-35, TRUE);
			else
				m_richSend.MoveWindow(6, size.cy-128, size.cx-GROUP_MEMBER_LIST_WIDTH-5, 95, TRUE);
		}

		if (m_ListCtrl.IsWindow())
		{
			CRect rcListCtrl(rcClient.right-194, 298, rcClient.right-3, rcClient.bottom-4);
			m_ListCtrl.MoveWindow(&rcListCtrl);
		}

		if(m_RightTabCtrl.IsWindow())
		{
			m_RightTabCtrl.ShowWindow(SW_HIDE);
		}
	}
	else
	{
		if (m_tbTop.IsWindow())
			m_tbTop.MoveWindow(3, 70, size.cx-1, 32, TRUE);

		if (m_staMemoTitle.IsWindow())
			m_staMemoTitle.MoveWindow(rcClient.right-195-GROUP_MSG_LOG_WIDTH, 98, 192, 30, FALSE);

		if (m_staMemberTitle.IsWindow())
			m_staMemberTitle.MoveWindow(rcClient.right-195-GROUP_MSG_LOG_WIDTH, 267, 192, 30, FALSE);

		if (m_edtMemo.IsWindow())
			m_edtMemo.MoveWindow(rcClient.right-195-GROUP_MSG_LOG_WIDTH, 124, 190, 145, FALSE);

		if (m_btnClose.IsWindow())
			m_btnClose.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-208-GROUP_MSG_LOG_WIDTH, size.cy-30, 77, 25, TRUE);

		if (m_btnSend.IsWindow())
			m_btnSend.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-118-GROUP_MSG_LOG_WIDTH, size.cy-30, 77, 25, TRUE);

		if (m_btnArrow.IsWindow())
			m_btnArrow.MoveWindow(size.cx-GROUP_MEMBER_LIST_WIDTH-42-GROUP_MSG_LOG_WIDTH, size.cy-30, 28, 25, TRUE);

		if (m_richRecv.IsWindow())
		{
			if(m_bDraged)
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, m_rtRichRecv.bottom-m_rtRichRecv.top-32);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, m_rtRichRecv.bottom-m_rtRichRecv.top);
			
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, m_rtRichRecv.bottom-32, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH - 20, 32, TRUE);
			}
			else
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, size.cy-305);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, size.cy-273);
			
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, size.cy-197, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH-20, 32, TRUE);
			}
		}

		if (m_tbMid.IsWindow())
		{
			if(m_bDraged)
				m_tbMid.MoveWindow(3, m_rtMidToolBar.top, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH, 31, TRUE);
			else
				m_tbMid.MoveWindow(3, size.cy-167, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH, 31, TRUE);
			//消息记录按钮始终靠边
			m_tbMid.SetItemMargin(m_nMsgLogIndexInToolbar, size.cx-GROUP_MEMBER_LIST_WIDTH-200-GROUP_MSG_LOG_WIDTH, 0);
		}

		if(m_SplitterCtrl.IsWindow())
		{
			if(m_bDraged)
				m_SplitterCtrl.MoveWindow(3, m_rtSplitter.top, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH, 5, FALSE);
			else
				m_SplitterCtrl.MoveWindow(3, size.cy-133, size.cx-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH, 5, FALSE);
		}

		if (m_richSend.IsWindow())
		{
			if(m_bDraged)
				m_richSend.MoveWindow(6, m_rtRichSend.top, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, size.cy-m_rtRichSend.top-35, FALSE);
			else
				m_richSend.MoveWindow(6, size.cy-128, size.cx-GROUP_MEMBER_LIST_WIDTH-2-GROUP_MSG_LOG_WIDTH, 95, FALSE);
		}
		
		if (m_ListCtrl.IsWindow())
		{
			CRect rcListCtrl(rcClient.right-194-GROUP_MSG_LOG_WIDTH, 298, rcClient.right-3-GROUP_MSG_LOG_WIDTH, rcClient.bottom-4);
			m_ListCtrl.MoveWindow(&rcListCtrl);
		}

		if(m_RightTabCtrl.IsWindow())
		{
			m_RightTabCtrl.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+4, 74, GROUP_MSG_LOG_WIDTH-6, 20);
		}

		if (m_richMsgLog.IsWindow())
		{
			m_richMsgLog.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+4, 106, GROUP_MSG_LOG_WIDTH-6, size.cy-140);	
		}

		//聊天记录翻页四个按钮
		if (m_btnFirstMsgLog.IsWindow())
			m_btnFirstMsgLog.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+110, size.cy-30, 28, 25, TRUE);

		if (m_btnPrevMsgLog.IsWindow())
			m_btnPrevMsgLog.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+140, size.cy-30, 28, 25, TRUE);

		if (m_staMsgLogPage.IsWindow())
			m_staMsgLogPage.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+180, size.cy-24, 60, 25, TRUE);

		if (m_btnNextMsgLog.IsWindow())
			m_btnNextMsgLog.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+240, size.cy-30, 28, 25, TRUE);

		if (m_btnLastMsgLog.IsWindow())
			m_btnLastMsgLog.MoveWindow(size.cx-GROUP_MSG_LOG_WIDTH+270, size.cy-30, 28, 25, TRUE);
		
	}

	ResizeImageInRecvRichEdit();
}

void CGroupChatDlg::SetHotRgn()
{
	RECT rtWindow;
	HRGN hTemp;
	m_picHead.GetClientRect(&rtWindow);
	m_HotRgn = ::CreateRectRgnIndirect(&rtWindow);

	m_lnkGroupName.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_staGroupCategory.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_richRecv.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_richSend.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	//m_richMsgLog.GetClientRect(&rtWindow);
	//hTemp = ::CreateRectRgnIndirect(&rtWindow);
	//::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	//::DeleteObject(hTemp);

	//m_btnMsgLog.GetClientRect(&rtWindow);
	//hTemp = ::CreateRectRgnIndirect(&rtWindow);
	//::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	//::DeleteObject(hTemp);

	m_btnClose.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_btnSend.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_btnArrow.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);
	
	m_staMemberTitle.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);
	
	m_ListCtrl.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_staMemoTitle.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_edtMemo.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_tbMid.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_AND);
	::DeleteObject(hTemp);

	m_SkinDlg.SetHotRegion(m_HotRgn);
}

//回车键发送消息
void CGroupChatDlg::OnPressEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bPressEnterToSendMessage = TRUE;
	m_lpFMGClient->m_UserConfig.EnablePressEnterToSend(m_bPressEnterToSendMessage);
}

//ctrl+回车键发送消息
void CGroupChatDlg::OnPressCtrlEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bPressEnterToSendMessage = FALSE;
	m_lpFMGClient->m_UserConfig.EnablePressEnterToSend(m_bPressEnterToSendMessage);
}

void CGroupChatDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1001)
	{
		UpdateGroupMemberList();		// 更新群成员列表
		OnRecvMsg(m_nGroupCode, NULL);	// 显示消息
		KillTimer(nIDEvent);
		SetTimer(1002, 300, NULL);
	}
	else if (nIDEvent == 1002)
	{
		if (!m_FontSelDlg.IsWindow())
			m_FontSelDlg.Create(m_hWnd);
		KillTimer(nIDEvent);
	}
}

void CGroupChatDlg::OnClose()
{
	RecordWindowSize();
	::PostMessage(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, WM_CLOSE_GROUPCHATDLG, (WPARAM)m_nGroupCode, 0);
	//关闭聊天窗口时，保持主窗口为前景窗口。
	//::SetForegroundWindow(m_lpFMGClient->m_UserMgr.m_hCallBackWnd);
	DestroyWindow();
}

void CGroupChatDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	CloseMsgLogBrowser();

	m_lpCascadeWinManager->Del(m_hWnd);

	UnInit();	// 反初始化控件

	if (m_hDlgIcon != NULL)
	{
		::DestroyIcon(m_hDlgIcon);
		m_hDlgIcon = NULL;
	}

	if (m_hDlgSmallIcon != NULL)
	{
		::DestroyIcon(m_hDlgSmallIcon);
		m_hDlgSmallIcon = NULL;
	}


	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}

// “群名称”超链接控件
void CGroupChatDlg::OnLnk_GroupName(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	::PostMessage(m_hMainDlg, WM_SHOW_GROUPINFODLG, m_nGroupCode, NULL);
}

// “字体选择工具栏”按钮
void CGroupChatDlg::OnBtn_Font(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	RECT rtRichRecv;
	::GetWindowRect(m_richRecv, &rtRichRecv);
	::ScreenToClient(m_hWnd, rtRichRecv);	
	
	if (BN_PUSHED == uNotifyCode)
	{
		m_FontSelDlg.ShowWindow(SW_SHOW);
		m_richRecv.MoveWindow(6, 106, rtRichRecv.right-rtRichRecv.left, rtRichRecv.bottom-rtRichRecv.top-32, TRUE);
		m_FontSelDlg.MoveWindow(6, rtRichRecv.bottom-32, rtRichRecv.right-rtRichRecv.left/*+20*/, 32, TRUE);	
	}
	else if (BN_UNPUSHED == uNotifyCode)
	{
		m_richRecv.MoveWindow(6, 106, rtRichRecv.right-rtRichRecv.left, rtRichRecv.bottom-rtRichRecv.top+32);
		m_FontSelDlg.ShowWindow(SW_HIDE);
	}
}

// “表情”按钮
void CGroupChatDlg::OnBtn_Face(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (BN_PUSHED == uNotifyCode)
	{
		m_FaceSelDlg.SetFaceList(m_lpFaceList);
		if (!m_FaceSelDlg.IsWindow())
		{
			m_FaceSelDlg.Create(m_hWnd);

			CRect rcBtn;
			m_tbMid.GetItemRectByIndex(1, rcBtn);
			m_tbMid.ClientToScreen(&rcBtn);

			int cx = 432;
			//int cy = 236;
            int cy = 306;
			int x = rcBtn.left - cx / 2;
			int y = rcBtn.top - cy;

			m_FaceSelDlg.SetWindowPos(NULL, x, y, cx, cy, NULL);
			m_FaceSelDlg.ShowWindow(SW_SHOW);
		}
	}
	else if (BN_UNPUSHED == uNotifyCode)
	{

	}
}

// “发送图片”按钮
void CGroupChatDlg::OnBtn_Image(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	BOOL bOpenFileDialog = TRUE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = NULL;
	DWORD dwFlags = OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_EXTENSIONDIFFERENT;
	LPCTSTR lpszFilter = _T("图像文件(*.bmp;*.jpg;*.jpeg;*.gif;*.png)\0*.bmp;*.jpg;*.jpeg;*.gif;*.png\0\0");;
	HWND hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("打开图片");
	if (fileDlg.DoModal() == IDOK)
	{
		UINT64 nFileSize = IUGetFileSize2(fileDlg.m_ofn.lpstrFile);
		if(nFileSize > MAX_CHAT_IMAGE_SIZE)
		{
			::MessageBox(m_hWnd, _T("图片大小超过10M，请使用截图工具。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
			return;
		}

		_RichEdit_InsertFace(m_richSend.m_hWnd, fileDlg.m_ofn.lpstrFile, -1, -1);
		m_richSend.SetFocus();
	}
}

// “来消息不提示”按钮
void CGroupChatDlg::OnBtn_MsgNotPrompt(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSkinMenu PopupMenu = PopupMenu = m_SkinMenu.GetSubMenu(1).GetSubMenu(5);
	if (PopupMenu.IsMenu())
	{
		CRect rcBtn;
		m_tbMid.GetItemRectByIndex(10, rcBtn);
		m_tbMid.ClientToScreen(&rcBtn);
		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rcBtn.left, rcBtn.bottom + 4, m_hWnd, &rcBtn);
	}
}

//截图工具
void CGroupChatDlg::OnBtn_ScreenShot(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DWORD dwSucceedExitCode = 2;
	CString strCatchScreen;
	strCatchScreen.Format(_T("%sCatchScreen.exe %u"), g_szHomePath, dwSucceedExitCode);
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	if(!CreateProcess(NULL, strCatchScreen.GetBuffer(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		::MessageBox(m_hWnd, _T("启动截图工具失败！"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR);
	}
	if(pi.hProcess != NULL)
	{
		::WaitForSingleObject(pi.hProcess, INFINITE);
		
		dwSucceedExitCode = 0;

		if(::GetExitCodeProcess(pi.hProcess, &dwSucceedExitCode) && dwSucceedExitCode==2)
			m_richSend.PasteSpecial(CF_TEXT);
	}
}

// “消息记录”按钮
//void CGroupChatDlg::OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
//{
//	if (uNotifyCode == BN_CLICKED)
//	{
//		BOOL bChecked = m_tbMid.GetItemCheckState(11);
//		if (!bChecked)
//		{
//			CloseMsgLogBrowser();
//			return;
//		}
//
//		OpenMsgLogBrowser();
//	}
//}

// “点击另存为”按钮
void CGroupChatDlg::OnBtn_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	OnMenu_SaveAs(uNotifyCode, nID, wndCtl);
}

// “关闭”按钮
void CGroupChatDlg::OnBtn_Close(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	PostMessage(WM_CLOSE);
}

// “发送”按钮
void CGroupChatDlg::OnBtn_Send(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (NULL == m_lpFMGClient)
		return;

	int nCustomPicCnt = RichEdit_GetCustomPicCount(m_richSend.m_hWnd);
	if (nCustomPicCnt > 1)
	{
		MessageBox(_T("每条消息最多包含1张图片，多张图片请分条发送。"), _T("提示"));
		return;
	}

	tstring strText;
	RichEdit_GetText(m_richSend.m_hWnd, strText);

	if (strText.size() <= 0)
		return;
	
	//time_t nMsgTime = m_lpFMGClient->GetCurrentTime();
	time_t nMsgTime = time(NULL);
	AddMsgToRecvEdit(nMsgTime, strText.c_str());

	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();

	TCHAR szColor[32] = {0};
	RGBToHexStr(fontInfo.m_clrText, szColor, sizeof(szColor)/sizeof(TCHAR));

	TCHAR szFontInfo[1024] = {0};
	LPCTSTR lpFontFmt = _T("/o[\"%s,%d,%s,%d,%d,%d\"]");
	wsprintf(szFontInfo, lpFontFmt, fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
		szColor, fontInfo.m_bBold, fontInfo.m_bItalic, fontInfo.m_bUnderLine);

	strText += szFontInfo;

	m_lpFMGClient->SendGroupMsg(m_nGroupCode, nMsgTime, strText.c_str(), m_hWnd);

	m_richSend.SetWindowText(_T(""));
	m_richSend.SetFocus();
}

// “箭头”按钮
void CGroupChatDlg::OnBtn_Arrow(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(5);
	if (PopupMenu.IsMenu())
	{
		CRect rc;
		m_btnArrow.GetClientRect(&rc);
		m_btnArrow.ClientToScreen(&rc);
		m_bPressEnterToSendMessage = m_lpFMGClient->m_UserConfig.IsEnablePressEnterToSend();
		if(m_bPressEnterToSendMessage)
		{
			PopupMenu.CheckMenuItem(ID_32901, MF_CHECKED);
			PopupMenu.CheckMenuItem(ID_32902, MF_UNCHECKED);
		}
		else
		{
			PopupMenu.CheckMenuItem(ID_32901, MF_UNCHECKED);
			PopupMenu.CheckMenuItem(ID_32902, MF_CHECKED);
		}

		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rc.left, rc.bottom + 4, m_hWnd, &rc);
	}
}

LRESULT CGroupChatDlg::OnToolbarDropDown(LPNMHDR pnmh)
{
	NMTOOLBAR* pnmtb = (NMTOOLBAR*)pnmh;
	CSkinMenu PopupMenu;
	CRect rc(pnmtb->rcButton);

	switch (pnmtb->iItem)
	{
	case 101:
		m_tbTop.ClientToScreen(&rc);
		PopupMenu = m_SkinMenu.GetSubMenu(0);
		break;

	case 108:
		m_tbTop.ClientToScreen(&rc);
		PopupMenu = m_SkinMenu.GetSubMenu(1);
		break;

	case 208:
		m_tbMid.ClientToScreen(&rc);
		PopupMenu = m_SkinMenu.GetSubMenu(2);
		break;

	case 209:
		m_tbMid.ClientToScreen(&rc);
		PopupMenu = m_SkinMenu.GetSubMenu(3);
		break;

	case 212:
		m_tbMid.ClientToScreen(&rc);
		PopupMenu = m_SkinMenu.GetSubMenu(4);
		break;

	default:
		return 0;
	}

	if (PopupMenu.IsMenu())
	{
		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rc.left, rc.bottom + 4, m_hWnd, &rc);
	}

	return 0;
}

// 更新字体信息
LRESULT CGroupChatDlg::OnUpdateFontInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();
	RichEdit_SetDefFont(m_richSend.m_hWnd, fontInfo.m_strName.c_str(),
		fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold,
		fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE);
	return 0;
}

// “表情”控件选取消息
LRESULT CGroupChatDlg::OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int nFaceId = m_FaceSelDlg.GetSelFaceId();
	int nFaceIndex = m_FaceSelDlg.GetSelFaceIndex();
	CString strFileName = m_FaceSelDlg.GetSelFaceFileName();
	if (!strFileName.IsEmpty())
	{
		_RichEdit_InsertFace(m_richSend.m_hWnd, strFileName, nFaceId, nFaceIndex);
		m_richSend.SetFocus();
	}

	m_tbMid.SetItemCheckState(1, FALSE);
	m_tbMid.Invalidate();

	return 0;
}

// 设置对话框初始焦点
LRESULT CGroupChatDlg::OnSetDlgInitFocus(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_richSend.SetFocus();
	return 0;
}

//	“接收消息”富文本框链接点击消息
LRESULT CGroupChatDlg::OnRichEdit_Recv_Link(LPNMHDR pnmh)
{
	if (pnmh->code == EN_LINK)
	{
		ENLINK*pLink = (ENLINK*)pnmh;
		if (pLink->msg == WM_LBUTTONUP)
		{
			m_richRecv.SetSel(pLink->chrg);
			m_richRecv.GetSelText(m_strCurLink);

			if (m_strCurLink.Left(7).MakeLower()==_T("http://") || m_strCurLink.Left(8).MakeLower()==_T("https://") || 
				(m_strCurLink.GetLength() >= 7 && m_strCurLink.Left(4).MakeLower()==_T("www.")))
				::ShellExecute(NULL, _T("open"), m_strCurLink, NULL, NULL, SW_SHOWNORMAL);
			else
			{
				CGroupInfo* pGroupInfo = GetGroupInfoPtr();
				if(pGroupInfo == NULL)
					return 0;
				CBuddyInfo* pBuddyInfo = pGroupInfo->GetMemberByAccount(m_strCurLink);
				if(pBuddyInfo == NULL)
					return 0;
				DWORD dwPos = GetMessagePos();
				CPoint point(LOWORD(dwPos), HIWORD(dwPos));

				CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(6);
				PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, m_hWnd);
			}
		}
	}
	return 0;
}

LRESULT CGroupChatDlg::OnRichEdit_Send_Paste(LPNMHDR pnmh)
{
	NMRICHEDITOLECALLBACK* lpOleNotify = (NMRICHEDITOLECALLBACK*)pnmh;
	if (lpOleNotify != NULL && lpOleNotify->hdr.code == EN_PASTE
		&& lpOleNotify->hdr.hwndFrom == m_richSend.m_hWnd)
	{
		AddMsgToSendEdit(lpOleNotify->lpszText);
	}
	return 0;
}

// “群成员”列表双击消息
LRESULT CGroupChatDlg::OnGMemberList_DblClick(LPNMHDR pnmh)
{
	int nCurSel = m_ListCtrl.GetCurSelItemIndex();
	if (nCurSel != -1)
	{
		UINT nUTalkUin = (UINT)m_ListCtrl.GetItemData(nCurSel, 0);
		if(nUTalkUin == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
		{
			::MessageBox(m_hWnd, _T("不能和自己聊天。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
			return 0;
		}
		
		if(m_lpFMGClient->m_UserMgr.IsFriend(nUTalkUin))
			::SendMessage(m_hMainDlg, WM_SHOW_BUDDYCHATDLG, 0, nUTalkUin);
		else
			::MessageBox(m_hWnd, _T("暂且不支持临时会话，您必须加对方为好友以后才能与之会话。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
	}
	return 0;
}

// “群成员”列表右键单击消息
LRESULT CGroupChatDlg::OnGMemberList_RClick(LPNMHDR pnmh)
{
	int nCurSel = m_ListCtrl.GetCurSelItemIndex();
	if (nCurSel != -1)
	{
		DWORD dwPos = GetMessagePos();
		CPoint point(LOWORD(dwPos), HIWORD(dwPos));

		CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(6);
		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, m_hWnd);
	}
	return 0;
}

// “剪切”菜单
void CGroupChatDlg::OnMenu_Cut(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.Cut();
}

// “复制”菜单
void CGroupChatDlg::OnMenu_Copy(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	HWND hWnd = GetFocus();
	if (hWnd == m_richSend.m_hWnd)
	{
		m_richSend.Copy();
	}
	else if (hWnd == m_richRecv.m_hWnd)
	{
		m_richRecv.Copy();
	}
	else if (hWnd == m_richMsgLog.m_hWnd)
	{
		m_richMsgLog.Copy();
	}
}

// “粘贴”菜单
void CGroupChatDlg::OnMenu_Paste(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.PasteSpecial(CF_TEXT);
}

// “全部选择”菜单
void CGroupChatDlg::OnMenu_SelAll(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	HWND hWnd = GetFocus();
	if (hWnd == m_richSend.m_hWnd)
	{
		m_richSend.SetSel(0, -1);
	}
	else if (hWnd == m_richRecv.m_hWnd)
	{
		m_richRecv.SetSel(0, -1);
	}
}

// “清屏”菜单
void CGroupChatDlg::OnMenu_Clear(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richRecv.SetWindowText(_T(""));
}

// “显示比例”菜单
void CGroupChatDlg::OnMenu_ZoomRatio(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	switch (nID)
	{
	case ID_MENU_ZOOMRATIO_400:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 16, 4);
		break;
	case ID_MENU_ZOOMRATIO_200:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 8, 4);
		break;
	case ID_MENU_ZOOMRATIO_150:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 6, 4);
		break;
	case ID_MENU_ZOOMRATIO_125:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 5, 4);
		break;
	case ID_MENU_ZOOMRATIO_100:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 0, 0);
		break;
	case ID_MENU_ZOOMRATIO_75:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 3, 4);
		break;
	case ID_MENU_ZOOMRATIO_50:
		::SendMessage(m_richRecv.m_hWnd, EM_SETZOOM, 1, 2);
		break;
	default:
		return;
	}

	CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(8);
	for (int i = ID_MENU_ZOOMRATIO_400; i <= ID_MENU_ZOOMRATIO_50; i++)
	{
		if (i != nID)
			menuPopup.CheckMenuItem(i, MF_BYCOMMAND|MF_UNCHECKED);
		else
			menuPopup.CheckMenuItem(i, MF_BYCOMMAND|MF_CHECKED);
	}	
}

// “另存为”菜单
void CGroupChatDlg::OnMenu_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	IImageOle* pImageOle = NULL;
	BOOL bRet = RichEdit_GetImageOle(m_hRBtnDownWnd, m_ptRBtnDown, &pImageOle);
	if (!bRet || NULL == pImageOle)
		return;

	CString strFileName;

	BSTR bstrFileName = NULL;
	HRESULT hr = pImageOle->GetFileName(&bstrFileName);
	if (SUCCEEDED(hr))
		strFileName = bstrFileName;
	if (bstrFileName != NULL)
		::SysFreeString(bstrFileName);

	TCHAR cFileName[MAX_PATH] = {0};
	BOOL bOpenFileDialog = FALSE;
	LPCTSTR lpszDefExt;
	CString strFileNamePrefix;
	GenerateChatImageSavedName(strFileNamePrefix);
	DWORD dwFlags = OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_EXTENSIONDIFFERENT;
	LPCTSTR lpszFilter;
	HWND hWndParent = m_hWnd;

	GUID guid = {0};
	hr = pImageOle->GetRawFormat(&guid);

	if (InlineIsEqualGUID(guid, Gdiplus::ImageFormatJPEG))
	{
		lpszDefExt = _T(".jpg");
		lpszFilter = _T("图像文件(*.jpg)\0*.jpg\0图像文件(*.bmp)\0*.bmp\0\0");
	}
	else if (InlineIsEqualGUID(guid, Gdiplus::ImageFormatPNG))
	{
		lpszDefExt = _T(".png");
		lpszFilter = _T("图像文件(*.png)\0*.png\0\0");
	}
	else if (InlineIsEqualGUID(guid, Gdiplus::ImageFormatGIF))
	{
		lpszDefExt = _T(".gif");
		lpszFilter = _T("图像文件(*.gif)\0*.gif\0图像文件(*.jpg)\0*.jpg\0图像文件(*.bmp)\0*.bmp\0\0");
	}
	else
	{
		lpszDefExt = _T(".jpg");
		lpszFilter = _T("图像文件(*.jpg)\0*.jpg\0图像文件(*.bmp)\0*.bmp\0\0");
	}

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, strFileNamePrefix, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("保存图片");
	if (fileDlg.DoModal() == IDOK)
	{
		CString strSavePath = fileDlg.m_ofn.lpstrFile;
		CString strExtName = (_T(".") + Hootina::CPath::GetExtension(strSavePath)).c_str();
		GUID guid2 = GetFileTypeGuidByExtension(strExtName);

		if (InlineIsEqualGUID(guid, guid2))
		{
			CopyFile(strFileName, strSavePath, FALSE);
		}
		else
		{
			BSTR bstrSavePath = ::SysAllocString(strSavePath);
			if (bstrSavePath != NULL)
			{
				pImageOle->SaveAsFile(bstrSavePath);
				::SysFreeString(bstrSavePath);
			}
		}
	}

	if (pImageOle != NULL)
		pImageOle->Release();
}

// “查看资料”菜单
void CGroupChatDlg::OnMenu_ViewInfo(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DWORD dwPos = GetMessagePos();
	CPoint point(LOWORD(dwPos), HIWORD(dwPos));

	HWND hWnd = ::WindowFromPoint(point);
	UINT nUTalkUin = 0;
	if (hWnd == m_richRecv.m_hWnd)
	{
		CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
		if (lpGroupInfo != NULL)
		{
			CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByAccount(m_strCurLink);
			if (lpBuddyInfo == NULL)
				return;

			nUTalkUin = lpBuddyInfo->m_uUserID;		
		}
	}
	else if (hWnd == m_ListCtrl.m_hWnd)
	{
		int nCurSel = m_ListCtrl.GetCurSelItemIndex();
		if (nCurSel < 0)
			return;
		
		nUTalkUin = (UINT)m_ListCtrl.GetItemData(nCurSel, 0);	
	}

	if(nUTalkUin == 0)
		return;

	if(m_lpFMGClient->m_UserMgr.IsFriend(nUTalkUin) || m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID==nUTalkUin)
		::PostMessage(m_hMainDlg, WM_SHOW_BUDDYINFODLG, 0, nUTalkUin);
	else
	{
		::MessageBox(m_hWnd, _T("暂不支持查看非好友的资料。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		//::PostMessage(m_hMainDlg, WM_SHOW_GMEMBERINFODLG, m_nGroupCode, nUTalkUin);
	}
}

// “发送消息”菜单
void CGroupChatDlg::OnMenu_SendMsg(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DWORD dwPos = GetMessagePos();
	CPoint point(LOWORD(dwPos), HIWORD(dwPos));

	HWND hWnd = ::WindowFromPoint(point);
	UINT nUTalkUin = 0;
	if (hWnd == m_richRecv.m_hWnd)
	{
		CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
		if (lpGroupInfo != NULL)
		{
			CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByAccount(m_strCurLink);
			if (lpBuddyInfo == NULL)
				return;

			nUTalkUin = lpBuddyInfo->m_uUserID;
		}
	}
	else if (hWnd == m_ListCtrl.m_hWnd)
	{
		int nCurSel = m_ListCtrl.GetCurSelItemIndex();
		if (nCurSel < 0)
			return;
		
		nUTalkUin = (UINT)m_ListCtrl.GetItemData(nCurSel, 0);	
	}
	
	if(nUTalkUin == 0)
		return;

	if(m_lpFMGClient->m_UserMgr.IsFriend(nUTalkUin))
		::SendMessage(m_hMainDlg, WM_SHOW_BUDDYCHATDLG, 0, nUTalkUin);
	else if(nUTalkUin == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
		::MessageBox(m_hWnd, _T("不能和自己聊天。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
	else
		::MessageBox(m_hWnd, _T("暂且不支持临时会话，您必须加对方为好友以后才能与之会话。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
}

// 发送/接收文本框的鼠标移动消息
BOOL CGroupChatDlg::OnRichEdit_MouseMove(MSG* pMsg)
{
	IImageOle* pNewImageOle = NULL;
	RECT rc = {0};

	POINT pt = {LOWORD(pMsg->lParam), HIWORD(pMsg->lParam)};
	IImageOle* pImageOle = NULL;
	BOOL bRet = RichEdit_GetImageOle(pMsg->hwnd, pt, &pImageOle);
	if (bRet && pImageOle != NULL)
	{
		pNewImageOle = pImageOle;
		pImageOle->GetObjectRect(&rc);
	}
	if (pImageOle != NULL)
		pImageOle->Release();

	if (m_pLastImageOle != pNewImageOle)
	{
		m_pLastImageOle = pNewImageOle;
		if (m_pLastImageOle != NULL)
		{
			m_hRBtnDownWnd = pMsg->hwnd;
			m_ptRBtnDown = pt;

			if (!m_PicBarDlg.IsWindow())
				m_PicBarDlg.Create(m_hWnd);

			RECT rc2 = {0};
			::GetClientRect(pMsg->hwnd, &rc2);
			POINT pt = {rc.right, rc.bottom-m_cyPicBarDlg};
			if (pt.x < rc2.left)
				pt.x = rc2.left;
			if (pt.x > rc2.right)
				pt.x = rc2.right;
			if (pt.y > rc2.bottom-m_cyPicBarDlg)
				pt.y = rc2.bottom-m_cyPicBarDlg;
			::ClientToScreen(pMsg->hwnd, &pt);

			::SetWindowPos(m_PicBarDlg.m_hWnd, NULL, pt.x, pt.y, m_cxPicBarDlg, m_cyPicBarDlg, SWP_NOACTIVATE|SWP_SHOWWINDOW);
		}
		else
		{
			::ShowWindow(m_PicBarDlg.m_hWnd, SW_HIDE);
		}
	}
	return FALSE;
}

// 发送/接收文本框的鼠标双击消息
BOOL CGroupChatDlg::OnRichEdit_LBtnDblClk(MSG* pMsg)
{
	POINT pt = {GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam)};

	IImageOle* pImageOle = NULL;
	BOOL bRet = RichEdit_GetImageOle(pMsg->hwnd, pt, &pImageOle);
	if (bRet && pImageOle != NULL)					// 双击的是图片
	{
		LONG nFaceId = -1, nFaceIndex = -1;
		pImageOle->GetFaceId(&nFaceId);
		pImageOle->GetFaceIndex(&nFaceIndex);
		if (-1 == nFaceId && -1 == nFaceIndex)		// 非系统表情
		{
			BSTR bstrFileName = NULL;				// 获取图片文件名
			HRESULT hr = pImageOle->GetFileName(&bstrFileName);
			if (SUCCEEDED(hr))						// 调用图片浏览器程序打开图片
			{
				CString strExeName = Hootina::CPath::GetAppPath().c_str();
				strExeName += _T("picView.exe");

				CString strArg = _T("\"");
				strArg += bstrFileName;
				strArg += _T("\"");

				if (Hootina::CPath::IsFileExist(strExeName))
				{
					HWND hWnd = ::FindWindow(_T("DUI_WINDOW"), _T("ImageView"));
					if (::IsWindow(hWnd))
						::SendMessage(hWnd, WM_CLOSE, 0, 0);
					::ShellExecute(NULL, NULL, strExeName, strArg, NULL, SW_SHOWNORMAL);
				}
				else
					::ShellExecute(NULL, _T("open"), bstrFileName, NULL, NULL, SW_SHOWNORMAL);
			}
			if (bstrFileName != NULL)
				::SysFreeString(bstrFileName);
		}
	}
	if (pImageOle != NULL)
		pImageOle->Release();

	return bRet;
}

// 发送/接收文本框的鼠标右键按下消息
BOOL CGroupChatDlg::OnRichEdit_RBtnDown(MSG* pMsg)
{
	if (pMsg->hwnd == m_richSend.m_hWnd)
	{
		m_hRBtnDownWnd = pMsg->hwnd;
		m_ptRBtnDown.x = GET_X_LPARAM(pMsg->lParam);
		m_ptRBtnDown.y = GET_Y_LPARAM(pMsg->lParam);

		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(7);

		UINT nSel = ((m_richSend.GetSelectionType() != SEL_EMPTY) ? 0 : MF_GRAYED);
		menuPopup.EnableMenuItem(ID_MENU_CUT, MF_BYCOMMAND|nSel);
		menuPopup.EnableMenuItem(ID_MENU_COPY, MF_BYCOMMAND|nSel);

		UINT nPaste = (m_richSend.CanPaste() ? 0 : MF_GRAYED) ;
		menuPopup.EnableMenuItem(ID_MENU_PASTE, MF_BYCOMMAND|nPaste);

		IImageOle* pImageOle = NULL;
		BOOL bRet = RichEdit_GetImageOle(pMsg->hwnd, m_ptRBtnDown, &pImageOle);
		UINT nSaveAs = ((bRet && pImageOle != NULL) ? 0 : MF_GRAYED) ;
		menuPopup.EnableMenuItem(ID_MENU_SAVEAS, MF_BYCOMMAND|nSaveAs);
		if (pImageOle != NULL)
			pImageOle->Release();

		menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON,
			pMsg->pt.x, pMsg->pt.y, m_hWnd, NULL);
	}
	else if (pMsg->hwnd == m_richRecv.m_hWnd)
	{
		m_hRBtnDownWnd = pMsg->hwnd;
		m_ptRBtnDown.x = GET_X_LPARAM(pMsg->lParam);
		m_ptRBtnDown.y = GET_Y_LPARAM(pMsg->lParam);

		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(8);

		UINT nSel = ((m_richRecv.GetSelectionType() != SEL_EMPTY) ? 0 : MF_GRAYED);
		menuPopup.EnableMenuItem(ID_MENU_COPY, MF_BYCOMMAND|nSel);
		//menuPopup.EnableMenuItem(ID_MENU_CLEAR, MF_BYCOMMAND|nSel);

		IImageOle* pImageOle = NULL;
		BOOL bRet = RichEdit_GetImageOle(pMsg->hwnd, m_ptRBtnDown, &pImageOle);
		UINT nSaveAs = ((bRet && pImageOle != NULL) ? 0 : MF_GRAYED) ;
		menuPopup.EnableMenuItem(ID_MENU_SAVEAS, MF_BYCOMMAND|nSaveAs);
		if (pImageOle != NULL)
			pImageOle->Release();

		menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON,
			pMsg->pt.x, pMsg->pt.y, m_hWnd, NULL);
	}
	else if(pMsg->hwnd == m_richMsgLog.m_hWnd)
	{
		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(9);
		menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pMsg->pt.x, pMsg->pt.y, m_hWnd, NULL);
	}

	return FALSE;
}

// 删除选中消息记录
void CGroupChatDlg::OnMenu_DeleteSelectMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(IDYES != ::MessageBox(m_hWnd, _T("删除的消息记录无法恢复，确实要删除选中的消息记录吗？"), _T("删除确认"), MB_YESNO|MB_ICONWARNING))
		return;
	
	m_richMsgLog.SetReadOnly(FALSE);
	m_richMsgLog.Cut();
	INPUT Input={0};
	// Backspace down
	//Input.type = INPUT_KEYBOARD;
	//Input.mi.dwFlags = KEYBDINPUT;
	//SendInput(1,&Input,sizeof(INPUT));
	::SendMessage(m_richMsgLog.m_hWnd, WM_KEYDOWN, VK_BACK, 0);
	m_richMsgLog.SetReadOnly(TRUE);
	//m_richSend.PasteSpecial(CF_TEXT);

	//判断剪贴板的数据格式是否可以处理。
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
	{
		return;
	} 
	// Open the clipboard.  
	if (!::OpenClipboard(m_richMsgLog.m_hWnd))  
		return;  

	HGLOBAL hMem = ::GetClipboardData(CF_UNICODETEXT);;
	LPCTSTR lpStr = NULL;
	if(hMem != NULL)  
	{
		lpStr = (LPCTSTR)::GlobalLock(hMem);
		if (lpStr != NULL)
		{
			//显示输出。
			//::OutputDebugString(lpStr);
			//释放锁内存。
			::GlobalUnlock(hMem);
		}
	}
	::CloseClipboard();
	//用sql语句去删除SQLite数据库中对应的消息记录

	//UINT nMsgLogID = m_MsgLogger.DelBuddyMsgLogByText(lpStr);
}

void CGroupChatDlg::OnMenu_ClearMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(IDYES != ::MessageBox(m_hWnd, _T("删除的消息记录无法恢复，确实要删除该群的所有消息记录吗？"), _T("删除确认"), MB_YESNO|MB_ICONWARNING))
		return;
	
	m_richMsgLog.SetWindowText(_T(""));
	m_richRecv.SetWindowText(_T(""));
	m_staMsgLogPage.SetWindowText(_T("0/0"));
	m_MsgLogger.DelGroupMsgLog(m_nGroupCode);
}


// 获取群信息指针
CGroupInfo* CGroupChatDlg::GetGroupInfoPtr()
{
	if (m_lpFMGClient != NULL)
	{
		CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
		if (lpGroupList != NULL)
			return lpGroupList->GetGroupByCode(m_nGroupCode);
	}
	return NULL;
}

// 获取群成员信息指针
CBuddyInfo* CGroupChatDlg::GetGMemberInfoPtr(UINT nUTalkUin)
{
	if (m_lpFMGClient != NULL)
	{
		CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
		if (lpGroupList != NULL)
		{
			CGroupInfo* lpGroupInfo = lpGroupList->GetGroupByCode(m_nGroupCode);
			if (lpGroupInfo != NULL)
				return lpGroupInfo->GetMemberByUin(nUTalkUin);
		}
	}
	return NULL;
}

// 获取用户信息指针
CBuddyInfo* CGroupChatDlg::GetUserInfoPtr()
{
	if (m_lpFMGClient != NULL)
	{
		CBuddyInfo* lpUserInfo = m_lpFMGClient->GetUserInfo();
		if (lpUserInfo != NULL)
		{
			CBuddyInfo* lpGMemberInfo = NULL;
			CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
			if (lpGroupInfo != NULL)
				lpGMemberInfo = lpGroupInfo->GetMemberByUin(lpUserInfo->m_uUserID);
			if (lpGMemberInfo != NULL && 0 == lpGMemberInfo->m_uUserID)
				lpGMemberInfo->m_uUserID = lpUserInfo->m_uUserID;
			return (lpGMemberInfo != NULL) ? lpGMemberInfo : lpUserInfo;
		}
	}
	return NULL;
}

// 更新信息
void CGroupChatDlg::UpdateData()
{
	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		m_nMemberCnt = lpGroupInfo->GetMemberCount();
		m_nOnlineMemberCnt = lpGroupInfo->GetOnlineMemberCount();
		m_nGroupId = lpGroupInfo->m_nGroupId;
		m_nGroupNumber = lpGroupInfo->m_nGroupNumber;
		m_strGroupName = lpGroupInfo->m_strName.c_str();
	}

	CBuddyInfo* lpUserInfo = GetUserInfoPtr();
	if (lpUserInfo != NULL)
	{
		m_strUserName = lpUserInfo->m_strNickName.c_str();
		m_strAccount = lpUserInfo->m_strAccount.c_str();
	}
}

// 更新对话框标题栏
void CGroupChatDlg::UpdateDlgTitle()
{
	SetWindowText(m_strGroupName);
}

// 更新群名称超链接控件
BOOL CGroupChatDlg::UpdateGroupNameCtrl()
{
	CString strText;
	if (m_nGroupNumber != 0)
		strText.Format(_T("%s(%u)"), m_strGroupName, m_nGroupNumber);
	else
		strText.Format(_T("%s"), m_strGroupName);
	m_lnkGroupName.SetLabel(strText);
	return TRUE;
}

// 更新群公告
BOOL CGroupChatDlg::UpdateGroupMemo()
{
	m_edtMemo.SetWindowText(_T("暂无公告"));
	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		if (!lpGroupInfo->m_strMemo.empty())
			m_edtMemo.SetWindowText(lpGroupInfo->m_strMemo.c_str());
	}
	return TRUE;
}

// 更新群成员列表
BOOL CGroupChatDlg::UpdateGroupMemberList()
{
	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (NULL == lpGroupInfo)
		return FALSE;

	lpGroupInfo->Sort();

	CString strText, strNickName, strFileName;

 	long nOnlineMemberCnt = 0;
	long nMemberCnt = 0;

	m_ListCtrl.SetRedraw(FALSE);
	m_ListCtrl.DeleteAllItems();
	BOOL bGray = TRUE;
	LOG_INFO("Update Group Member Info: GroupID=%u, GroupAccount=%s, GroupName=%s.",
					lpGroupInfo->m_nGroupCode, lpGroupInfo->m_strAccount.c_str(), lpGroupInfo->m_strName.c_str());
	for (int i = 0; i < lpGroupInfo->GetMemberCount(); i++)
	{
		CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMember(i);
		if (lpBuddyInfo == NULL || lpBuddyInfo->m_strNickName.empty() || lpBuddyInfo->m_strAccount.empty())
			continue;
			
			
		if(lpBuddyInfo->m_uUserID == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
			strText.Format(_T("☆%s(%s)"), lpBuddyInfo->m_strNickName.c_str(), lpBuddyInfo->m_strAccount.c_str());
		else
			strText.Format(_T("%s(%s)"), lpBuddyInfo->m_strNickName.c_str(), lpBuddyInfo->m_strAccount.c_str());
		
		if(lpBuddyInfo->m_nStatus!=STATUS_OFFLINE && lpBuddyInfo->m_nStatus!=STATUS_INVISIBLE)
		{
			bGray = FALSE;
			++nOnlineMemberCnt;
		}
		else
			bGray = TRUE;

        if (lpBuddyInfo->m_bUseCustomFace && lpBuddyInfo->m_bCustomFaceAvailable)
            strFileName.Format(_T("%s%d.png"), m_lpFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), lpBuddyInfo->m_uUserID);
        else
		    strFileName.Format(_T("%sImage\\UserThumbs\\%u.png"), g_szHomePath, lpBuddyInfo->m_nFace);
		if (!Hootina::CPath::IsFileExist(strFileName))
		{
			if (!bGray)
				strFileName = (Hootina::CPath::GetAppPath() + _T("Image\\DefGMemberHeadPic16x16.png")).c_str();
			else
				strFileName = (Hootina::CPath::GetAppPath() + _T("Image\\DefGMemberGrayHeadPic16x16.png")).c_str();
			bGray = FALSE;
		}
		
		m_ListCtrl.InsertItem(nMemberCnt, strText, strFileName, bGray, DT_LEFT, 0);
		m_ListCtrl.SetItemData(nMemberCnt, 0, lpBuddyInfo->m_uUserID);

		LOG_INFO("GroupMemberInfo: AccountID=%u, AccountName=%s, NickName=%s, Gray=%d.",
					lpBuddyInfo->m_uUserID, lpBuddyInfo->m_strAccount.c_str(), lpBuddyInfo->m_strNickName.c_str(), bGray);

		++nMemberCnt;
		
	}

	strText.Format(_T("  群成员(%d/%d)"), nOnlineMemberCnt, nMemberCnt);
	m_staMemberTitle.SetWindowText(strText);
	m_ListCtrl.SetRedraw(TRUE);
	m_ListCtrl.Invalidate();

	return TRUE;
}

// 初始化Top工具栏
BOOL CGroupChatDlg::InitTopToolBar()
{
#ifdef _DEBUG
	int nIndex = m_tbTop.AddItem(101, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("群社区"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\GroupCommunity.png"));

	nIndex = m_tbTop.AddItem(102, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("群共享"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\GroupShare.png"));

	nIndex = m_tbTop.AddItem(103, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("群相册"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\GroupAlbum.png"));

	nIndex = m_tbTop.AddItem(104, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("休闲小游戏"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\Game.png"));

	nIndex = m_tbTop.AddItem(105, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("创建讨论组"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\create_disc_group.png"));

	nIndex = m_tbTop.AddItem(106, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("发起多人语音会话"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\MultiAudio.png"));

	nIndex = m_tbTop.AddItem(107, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("搜索"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\soso.png"));

	nIndex = m_tbTop.AddItem(108, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("群设置"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\GroupSet.png"));

	nIndex = m_tbTop.AddItem(109, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("举报"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("GroupTopToolBar\\Report.png"));

#endif

	m_tbTop.SetLeftTop(2, 2);
	m_tbTop.SetTransparent(TRUE, m_SkinDlg.GetBgDC());

	CRect rcTopToolBar(3, 70, GROUPCHATDLG_WIDTH-1, 102);
	m_tbTop.Create(m_hWnd, rcTopToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_TOP);

	return TRUE;
}

// 初始化Middle工具栏
BOOL CGroupChatDlg::InitMidToolBar()
{
	int nIndex = m_tbMid.AddItem(201, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("字体选择工具栏"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_font.png"));

	nIndex = m_tbMid.AddItem(202, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("选择表情"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_face.png"));

	nIndex = m_tbMid.AddItem(203, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("发送图片"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_sendpic.png"));


	nIndex = m_tbMid.AddItem(204, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 30, 27, 27, 0);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("屏幕截图"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_cut.png"));



	nIndex = m_tbMid.AddItem(205, STBI_STYLE_BUTTON);
	m_nMsgLogIndexInToolbar = nIndex;
	m_tbMid.SetItemSize(nIndex, 90, 27, 27, 0);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemMargin(nIndex, 260, 0);
	m_tbMid.SetItemText(nIndex, _T(">>"));
	m_tbMid.SetItemToolTipText(nIndex, _T("点击查看消息记录"));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_msglog.png"));

	m_tbMid.SetLeftTop(2, 4);
	m_tbMid.SetBgPic(_T("MidToolBar\\bg.png"), CRect(0,0,0,0));

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcMidToolBar(2, GROUPCHATDLG_HEIGHT-167, GROUPCHATDLG_WIDTH-GROUP_MEMBER_LIST_WIDTH, GROUPCHATDLG_HEIGHT-136);
	m_tbMid.Create(m_hWnd, rcMidToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_MID);

	return TRUE;
}

// “消息记录”按钮
void CGroupChatDlg::OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bMsgLogWindowVisible = !m_bMsgLogWindowVisible;

	CRect rtWindow;
	GetWindowRect(&rtWindow);

	//获取当前窗口在屏幕的位置	
	if(m_bMsgLogWindowVisible)
	{
		m_SkinDlg.SetBgPic(CHAT_EXPAND_BG_IMAGE_NAME, CRect(4, 100, 445, 32));
		if(IsZoomed())
		{
			OnSize(SIZE_MAXIMIZED, CSize(rtWindow.Width(), rtWindow.Height()));
		}
		else
			m_SkinDlg.MoveWindow(rtWindow.left, rtWindow.top, rtWindow.Width()+GROUP_MSG_LOG_WIDTH, rtWindow.Height(),FALSE);
			//m_SkinDlg.MoveWindow(rtWindow.left, rtWindow.top, rtWindow.Width()+RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(),FALSE);
		
		m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T("<<"));
		m_RightTabCtrl.ShowWindow(SW_SHOW);
		m_richMsgLog.ShowWindow(SW_SHOW);

		OpenMsgLogBrowser();

		//TODO: 如果不调用这一行，会消息记录会产生黑屏，为什么消息记录控件会产生黑屏？
		UpdateWindow();
	}
	else
	{
		m_SkinDlg.SetBgPic(CHAT_BG_IMAGE_NAME, CRect(4, 100, 4, 32));
		if(IsZoomed())
		{
			OnSize(SIZE_MAXIMIZED, CSize(rtWindow.Width(), rtWindow.Height()));
		}
		else
			m_SkinDlg.MoveWindow(rtWindow.left, rtWindow.top, rtWindow.Width()-GROUP_MSG_LOG_WIDTH, rtWindow.Height(), FALSE);
			//m_SkinDlg.MoveWindow2(rtWindow.left, rtWindow.top, rtWindow.Width()+RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(),FALSE);

		m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T(">>"));
		m_RightTabCtrl.ShowWindow(SW_HIDE);
		m_richMsgLog.ShowWindow(SW_HIDE);

		CloseMsgLogBrowser();
	}
}

// 初始化IRichEditOleCallback接口
BOOL CGroupChatDlg::InitRichEditOleCallback()
{
	IRichEditOleCallback2* pRichEditOleCallback2 = NULL;
	HRESULT hr = ::CoCreateInstance(CLSID_ImageOle, NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IRichEditOleCallback2), (void**)&pRichEditOleCallback2);
	if (SUCCEEDED(hr))
	{
		pRichEditOleCallback2->SetNotifyHwnd(m_hWnd);
		pRichEditOleCallback2->SetRichEditHwnd(m_richRecv.m_hWnd);
		m_richRecv.SetOleCallback(pRichEditOleCallback2);
		pRichEditOleCallback2->Release();
	}

	pRichEditOleCallback2 = NULL;
	hr = ::CoCreateInstance(CLSID_ImageOle, NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IRichEditOleCallback2), (void**)&pRichEditOleCallback2);
	if (SUCCEEDED(hr))
	{
		pRichEditOleCallback2->SetNotifyHwnd(m_hWnd);
		pRichEditOleCallback2->SetRichEditHwnd(m_richSend.m_hWnd);
		m_richSend.SetOleCallback(pRichEditOleCallback2);
		pRichEditOleCallback2->Release();
	}

	pRichEditOleCallback2 = NULL;
	hr = ::CoCreateInstance(CLSID_ImageOle, NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IRichEditOleCallback2), (void**)&pRichEditOleCallback2);
	if (SUCCEEDED(hr))
	{
		pRichEditOleCallback2->SetNotifyHwnd(m_hWnd);
		pRichEditOleCallback2->SetRichEditHwnd(m_richMsgLog.m_hWnd);
		m_richMsgLog.SetOleCallback(pRichEditOleCallback2);
		pRichEditOleCallback2->Release();
	}

	return SUCCEEDED(hr);
}

// 初始化
BOOL CGroupChatDlg::Init()
{
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetBgPic(_T("BuddyChatDlgBg.png"), CRect(4, 100, 4, 32));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), _T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetMaxSysBtnPic(_T("SysBtn\\btn_max_normal.png"), _T("SysBtn\\btn_max_highlight.png"), _T("SysBtn\\btn_max_down.png"));
	m_SkinDlg.SetRestoreSysBtnPic(_T("SysBtn\\btn_restore_normal.png"), _T("SysBtn\\btn_restore_highlight.png"), _T("SysBtn\\btn_restore_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	//m_SkinDlg.MoveWindow(0, 0, GROUPDLG_WIDTH, GROUPDLG_HEIGHT, FALSE);

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	CRect rcClient;
	GetClientRect(&rcClient);

	m_picHead.SubclassWindow(GetDlgItem(ID_PIC_HEAD));
	m_picHead.SetTransparent(TRUE, hDlgBgDC);
	m_picHead.SetShowCursor(TRUE);
	m_picHead.SetBgPic(_T("HeadCtrl\\Padding4Normal.png"), _T("HeadCtrl\\Padding4Hot.png"), _T("HeadCtrl\\Padding4Hot.png"));
	m_picHead.MoveWindow(10, 10, 54, 54, FALSE);
	m_picHead.SetToolTipText(_T("点击查看群资料"));

	tstring strFileName;
	if (m_lpFMGClient != NULL && m_nGroupNumber != 0)
		strFileName = m_lpFMGClient->GetGroupHeadPicFullName(m_nGroupNumber);
	if (!Hootina::CPath::IsFileExist(strFileName.c_str()))
		strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefGroupHeadPic.png");
	m_picHead.SetBitmap(strFileName.c_str());

	m_lnkGroupName.SubclassWindow(GetDlgItem(ID_LINK_GROUPNAME));
	m_lnkGroupName.MoveWindow(70, 12, 60, 14, FALSE);
	m_lnkGroupName.SetTransparent(TRUE, hDlgBgDC);
	m_lnkGroupName.SetLinkColor(RGB(0,0,0));
	m_lnkGroupName.SetHoverLinkColor(RGB(0,0,0));
	m_lnkGroupName.SetVisitedLinkColor(RGB(0,0,0));

	HFONT hFontGroupNameLink = CGDIFactory::GetFont(22);
	m_lnkGroupName.SetNormalFont(hFontGroupNameLink);
	
	HFONT hFontGroupCategory = CGDIFactory::GetFont(18);
	m_staGroupCategory.SubclassWindow(GetDlgItem(ID_STATIC_GROUPCATEGORY));
	m_staGroupCategory.MoveWindow(70, 38, 537, 20, FALSE);
	m_staGroupCategory.SetTransparent(TRUE, hDlgBgDC);
	m_staGroupCategory.SetFont(hFontGroupCategory);
	m_staGroupCategory.SetWindowText(_T("分类：我的群"));

	m_staMemoTitle.SubclassWindow(GetDlgItem(ID_STATIC_MEMOTITLE));
	m_staMemoTitle.MoveWindow(rcClient.right-200, 98, GROUP_MEMBER_LIST_WIDTH-2, 30, FALSE);
	m_staMemoTitle.SetTransparent(TRUE, hDlgBgDC);
	m_staMemoTitle.SetNormalBgPic(_T("MemoTitleBg.png"));

	m_edtMemo.SetMultiLine(TRUE);
	m_edtMemo.SubclassWindow(GetDlgItem(ID_EDIT_MEMO));
	m_edtMemo.MoveWindow(rcClient.right-200, 124, GROUP_MEMBER_LIST_WIDTH-2, 156, FALSE);

	m_staMemberTitle.SubclassWindow(GetDlgItem(ID_STATIC_MERBERTITLE));
	m_staMemberTitle.MoveWindow(rcClient.right-200, 267, GROUP_MEMBER_LIST_WIDTH-2, 35, FALSE);
	m_staMemberTitle.SetTransparent(TRUE, hDlgBgDC);
	m_staMemberTitle.SetNormalBgPic(_T("MemoTitleBg.png"));

	m_btnClose.SubclassWindow(GetDlgItem(ID_BTN_CLOSE));
	m_btnClose.SetTransparent(TRUE, hDlgBgDC);
	m_btnClose.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnClose.SetBgPic(_T("Button\\btn_close_normal.png"), _T("Button\\btn_close_highlight.png"),_T("Button\\btn_close_down.png"), _T("Button\\btn_close_focus.png"));
	m_btnClose.MoveWindow(GROUPCHATDLG_WIDTH-400, GROUPCHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnSend.SubclassWindow(GetDlgItem(ID_BTN_SEND));
	m_btnSend.SetTransparent(TRUE, hDlgBgDC);
	m_btnSend.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnSend.SetTextColor(RGB(255, 255, 255));
	m_btnSend.SetBgPic(_T("Button\\btn_send_normal.png"), _T("Button\\btn_send_highlight.png"),_T("Button\\btn_send_down.png"), _T("Button\\btn_send_focus.png"));
	m_btnSend.MoveWindow(GROUPCHATDLG_WIDTH-310, GROUPCHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnArrow.SubclassWindow(GetDlgItem(ID_BTN_ARROW));
	m_btnArrow.SetTransparent(TRUE, hDlgBgDC);
	m_btnArrow.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnArrow.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnArrow.MoveWindow(GROUPCHATDLG_WIDTH-235, GROUPCHATDLG_HEIGHT-30, 28, 25, FALSE);

	////图片上传进度信息文本
	//m_staPicUploadProgress.SubclassWindow(GetDlgItem(IDC_STATIC_PICPROGRESS));
	//m_staPicUploadProgress.SetTransparent(TRUE, hDlgBgDC);
	//m_staPicUploadProgress.MoveWindow(10, CHATDLG_HEIGHT-25, 380, 25, FALSE);
	//m_staPicUploadProgress.ShowWindow(SW_HIDE);
	
	//CRect rtClient;
	//GetClientRect(&rtClient);
	//消息记录的四个按钮
	m_btnFirstMsgLog.SubclassWindow(GetDlgItem(IDC_FIRSTMSGLOG));
	m_btnFirstMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnFirstMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnFirstMsgLog.SetToolTipText(_T("第一页"));
	//m_btnFirstMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnFirstMsgLog.MoveWindow(GROUPCHATDLG_WIDTH+110, GROUPCHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_btnPrevMsgLog.SubclassWindow(GetDlgItem(IDC_PREVMSGLOG));
	m_btnPrevMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnPrevMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnPrevMsgLog.SetToolTipText(_T("上一页"));
	//m_btnPrevMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnPrevMsgLog.MoveWindow(GROUPCHATDLG_WIDTH+140, GROUPCHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_staMsgLogPage.SubclassWindow(GetDlgItem(IDC_STATIC_MSGLOGPAGE));
	m_staMsgLogPage.SetTransparent(TRUE, hDlgBgDC);
	m_staMsgLogPage.MoveWindow(GROUPCHATDLG_WIDTH+170, GROUPCHATDLG_HEIGHT-24, 60, 25, FALSE);

	m_btnNextMsgLog.SubclassWindow(GetDlgItem(IDC_NEXTMSGLOG));
	m_btnNextMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnNextMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnNextMsgLog.SetToolTipText(_T("下一页"));
	//m_btnNextMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnNextMsgLog.MoveWindow(GROUPCHATDLG_WIDTH+240, GROUPCHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_btnLastMsgLog.SubclassWindow(GetDlgItem(IDC_LASTMSGLOG));
	m_btnLastMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnLastMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnLastMsgLog.SetToolTipText(_T("最后页"));
	//m_btnLastMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnLastMsgLog.MoveWindow(GROUPCHATDLG_WIDTH+270, GROUPCHATDLG_HEIGHT-30, 28, 25, FALSE);

	ShowMsgLogButtons(FALSE);

	m_SkinMenu.LoadMenu(ID_MENU_GROUPCHAT);
	m_SkinMenu.SetBgPic(_T("Menu\\menu_left_bg.png"), _T("Menu\\menu_right_bg.png"));
	m_SkinMenu.SetSelectedPic(_T("Menu\\menu_selected.png"));
	m_SkinMenu.SetSepartorPic(_T("Menu\\menu_separtor.png"));
	m_SkinMenu.SetArrowPic(_T("Menu\\menu_arrow.png"));
	m_SkinMenu.SetCheckPic(_T("Menu\\menu_check.png"));
	m_SkinMenu.SetTextColor(RGB(0, 20, 35));
	m_SkinMenu.SetSelTextColor(RGB(254, 254, 254));

	InitTopToolBar();				// 初始化Top工具栏
	InitMidToolBar();				// 初始化Middle工具栏
	m_PicBarDlg.Create(m_hWnd);		// 创建图片悬浮工具栏

	//CRect rcRecv = CRect(CPoint(6, rcClient.top+44+44), CSize(rcClient.Width()-6-209, rcClient.Height()-228));
	CRect rcRecv(6, 107, 583-GROUP_MEMBER_LIST_WIDTH-2, 366);
	DWORD dwStyle = WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
	m_richRecv.Create(m_hWnd, rcRecv, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_RECV);
	m_richRecv.SetTransparent(TRUE, hDlgBgDC);

	//接收richedit与midToolbar之间的分隔条
	CRect rcSplitter(2, GROUPCHATDLG_HEIGHT-135, GROUPCHATDLG_WIDTH-GROUP_MEMBER_LIST_WIDTH, GROUPCHATDLG_HEIGHT-130);
	m_SplitterCtrl.Create(m_hWnd, rcSplitter, NULL, WS_CHILD|WS_VISIBLE, 0, ID_SPLITTER_CTRL);
	
	CRect rcSend(6, 407, 583-GROUP_MEMBER_LIST_WIDTH-2, 502);
	m_richSend.Create(m_hWnd, rcSend, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_SEND);
	m_richSend.SetTransparent(TRUE, hDlgBgDC);

	if(!m_FontSelDlg.IsWindow())
	{
		m_FontSelDlg.Create(m_hWnd);
		m_FontSelDlg.ShowWindow(SW_HIDE);
	}

	InitRightTabWindow();

	// 发送消息富文本框控件
	//CFontInfo fontInfo = m_FontSelDlg.GetPublicFontInfo();
	//RichEdit_SetDefFont(m_richSend.m_hWnd, fontInfo.m_strName.c_str(),
	//	fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold,
	//	fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE);

	CFontInfo fontInfo;
	std::vector<tstring> arrSysFont;
	EnumSysFont(&arrSysFont);
	long nCustomFontNameIndex = -1;
	if(arrSysFont.empty())
	{
		::MessageBox(m_hWnd, _T("初始化聊天对话框失败！"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR);
		return FALSE;
	}
	
	size_t nFontCount = arrSysFont.size();

	CString strCustomFontName(m_lpFMGClient->m_UserConfig.GetFontName());
	if(!strCustomFontName.IsEmpty())
	{
		BOOL bFound = FALSE;
		for(size_t i=0; i<nFontCount; ++i)
		{
			if(strCustomFontName.CompareNoCase(arrSysFont[i].c_str()) == 0)
			{
				bFound = TRUE;
				break;
			}
		}

		if(!bFound)
			strCustomFontName = _T("微软雅黑");
	}
	else
		strCustomFontName = _T("微软雅黑");

	
	m_lpFMGClient->m_UserConfig.SetFontName(strCustomFontName);
	fontInfo.m_strName = strCustomFontName;
	fontInfo.m_nSize = m_lpFMGClient->m_UserConfig.GetFontSize();
	fontInfo.m_clrText = m_lpFMGClient->m_UserConfig.GetFontColor();
	fontInfo.m_bBold = m_lpFMGClient->m_UserConfig.IsEnableFontBold();
	fontInfo.m_bItalic = m_lpFMGClient->m_UserConfig.IsEnableFontItalic();
	fontInfo.m_bUnderLine = m_lpFMGClient->m_UserConfig.IsEnableFontUnderline();

	RichEdit_SetDefFont(m_richSend.m_hWnd, fontInfo.m_strName.c_str(),
		fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold,
		fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE);

	// 接收消息富文本框控件
	DWORD dwMask = m_richRecv.GetEventMask();
	dwMask = dwMask | ENM_LINK  | ENM_MOUSEEVENTS | ENM_SCROLLEVENTS | ENM_KEYEVENTS;
	m_richRecv.SetEventMask(dwMask);

	m_richRecv.SetReadOnly();

	CRect rcListCtrl(rcClient.right-200, 302, rcClient.right-3, rcClient.bottom-6);
	m_ListCtrl.Create(m_hWnd, rcListCtrl, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_LIST_MERBER);

	m_ListCtrl.SetBgPic(_T("ListCtrl\\listctrlbg.png"));
	m_ListCtrl.SetItemSelBgPic(_T("ListCtrl\\listitem_select.png"));
	m_ListCtrl.SetHeadVisible(FALSE);
	m_ListCtrl.AddColumn(_T("群成员"), NULL, DT_LEFT, 184);

	UpdateGroupNameCtrl();	// 更新群名称控件
	UpdateDlgTitle();		// 更新对话框标题
	UpdateGroupMemo();		// 更新群公告

	m_Accelerator.LoadAccelerators(ID_ACCE_CHATDLG);

	InitRichEditOleCallback();	// 初始化IRichEditOleCallback接口

	return TRUE;
}

// 初始化Tab栏
BOOL CGroupChatDlg::InitRightTabWindow()
{
	CRect rcRightTabCtrl(GROUPCHATDLG_WIDTH, 75, GROUPCHATDLG_WIDTH+GROUP_MSG_LOG_WIDTH-3, 102);
	m_RightTabCtrl.Create(m_hWnd, rcRightTabCtrl, NULL, WS_CHILD | WS_VISIBLE, NULL, ID_TABCTRL_CHAT, NULL);
	m_RightTabCtrl.SetTransparent(TRUE, m_SkinDlg.GetBgDC());
	//m_RightTabCtrl.ShowWindow(SW_HIDE);


	long nWidth = 80;
	long nWidthClose = 21;

	int nIndex = m_RightTabCtrl.AddItem(301, STCI_STYLE_DROPDOWN);
	m_RightTabCtrl.SetItemSize(nIndex, nWidth, 21, nWidth - nWidthClose, 20);
	m_RightTabCtrl.SetItemText(nIndex, _T("消息记录"));
	m_RightTabCtrl.SetCurSel(0);

	//nIndex = m_RightTabCtrl.AddItem(302, STCI_STYLE_DROPDOWN);
	//m_RightTabCtrl.SetItemSize(nIndex, nWidth, 21, nWidth - nWidthClose, 20);
	//m_RightTabCtrl.SetItemText(nIndex, _T("传送文件"));
	//m_RightTabCtrl.SetItemVisible(nIndex, FALSE);

	// 消息记录富文本对话框
	CRect rtClient;
	GetClientRect(&rtClient);
	CRect rcMsgLog;
	rcMsgLog.left = rcRightTabCtrl.left + 5;
	rcMsgLog.top = rcRightTabCtrl.top + 30;
	rcMsgLog.right = rcRightTabCtrl.right;
	rcMsgLog.bottom = rtClient.Height()-30;
	DWORD dwStyle = WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
	m_richMsgLog.Create(m_hWnd, rcMsgLog, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_MSGLOG);
	m_richMsgLog.ShowWindow(SW_HIDE);
	m_richMsgLog.SetTransparent(TRUE, m_SkinDlg.GetBgDC());
	m_richMsgLog.SetAutoURLDetect();
	m_richMsgLog.SetReadOnly();
	

	return TRUE;
}

void CGroupChatDlg::ShowMsgLogButtons(BOOL bShow)
{
	DWORD dwFlag = (bShow ? SW_SHOW : SW_HIDE); 
	
	m_btnFirstMsgLog.ShowWindow(dwFlag);
	m_btnPrevMsgLog.ShowWindow(dwFlag);
	m_staMsgLogPage.ShowWindow(dwFlag);
	m_btnNextMsgLog.ShowWindow(dwFlag);
	m_btnLastMsgLog.ShowWindow(dwFlag);
}

// 反初始化
BOOL CGroupChatDlg::UnInit()
{
	if (m_PicBarDlg.IsWindow())
		m_PicBarDlg.DestroyWindow();

	if (m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	if (m_lnkGroupName.IsWindow())
		m_lnkGroupName.DestroyWindow();

	if (m_staGroupCategory.IsWindow())
		m_staGroupCategory.DestroyWindow();

	if (m_staMemoTitle.IsWindow())
		m_staMemoTitle.DestroyWindow();

	if (m_staMemberTitle.IsWindow())
		m_staMemberTitle.DestroyWindow();

	if (m_edtMemo.IsWindow())
		m_edtMemo.DestroyWindow();

	if (m_btnClose.IsWindow())
		m_btnClose.DestroyWindow();

	if (m_btnSend.IsWindow())
		m_btnSend.DestroyWindow();

	if (m_btnArrow.IsWindow())
		m_btnArrow.DestroyWindow();

	m_SkinMenu.DestroyMenu();

	if (m_tbTop.IsWindow())
		m_tbTop.DestroyWindow();

	if (m_tbMid.IsWindow())
		m_tbMid.DestroyWindow();

	if (m_FontSelDlg.IsWindow())
		m_FontSelDlg.DestroyWindow();

	if (m_FaceSelDlg.IsWindow())
		m_FaceSelDlg.DestroyWindow();

	if (m_richRecv.IsWindow())
		m_richRecv.DestroyWindow();

	if (m_richSend.IsWindow())
		m_richSend.DestroyWindow();

	if (m_ListCtrl.IsWindow())
		m_ListCtrl.DestroyWindow();

	m_Accelerator.DestroyObject();
//	m_menuRichEdit.DestroyMenu();

	return TRUE;
}

int CGroupChatDlg::FindMemberListByUin(UINT nUTalkUin)
{
	int nCount = m_ListCtrl.GetItemCount();
	for (int i = 0; i < nCount; i++)
	{
		UINT nUTalkUin2 = (UINT)m_ListCtrl.GetItemData(i, 0);
		if (nUTalkUin2 == nUTalkUin)
			return i;
	}
	return -1;
}

void CGroupChatDlg::GetSenderInfo(UINT nUTalkUin, CString& strName, tstring& strAccount)
{
	strName = _T("");

	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByUin(nUTalkUin);
		if (lpBuddyInfo != NULL)
		{
			strName = lpBuddyInfo->m_strNickName.c_str();
			strAccount = lpBuddyInfo->m_strAccount;
		}
	}
}

void CGroupChatDlg::_RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText)
{
	if (hWnd == m_richRecv.m_hWnd)
	{
		CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();
		RichEdit_ReplaceSel(hWnd, lpszNewText, 
			fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
			fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
			fontInfo.m_bUnderLine, FALSE, 300);
	}
	else
	{
		RichEdit_ReplaceSel(hWnd, lpszNewText);
	}
}

BOOL CGroupChatDlg::_RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId, int nFaceIndex)
{
	ITextServices* pTextServices;
	ITextHost* pTextHost;
	BOOL bRet;
	long nImageWidth = 0;
	long nImageHeight = 0;
	GetImageDisplaySizeInRichEdit(lpszFileName, hWnd, nImageWidth, nImageHeight);

	if (hWnd == m_richRecv.m_hWnd)
	{
		pTextServices = m_richRecv.GetTextServices();
		pTextHost = m_richRecv.GetTextHost();

		long lStartChar = 0, lEndChar = 0;
		RichEdit_GetSel(hWnd, lStartChar, lEndChar);
		bRet = RichEdit_InsertFace(pTextServices, pTextHost, lpszFileName, nFaceId, nFaceIndex, RGB(255,255,255), TRUE, 40, nImageWidth, nImageHeight);
		if (bRet)
		{
			lEndChar = lStartChar + 1;
			RichEdit_SetSel(hWnd, lStartChar, lEndChar);
			RichEdit_SetStartIndent(hWnd, 300);
			RichEdit_SetSel(hWnd, lEndChar, lEndChar);
		}
	}
	else if (hWnd == m_richSend.m_hWnd)
	{
		pTextServices = m_richSend.GetTextServices();
		pTextHost = m_richSend.GetTextHost();

		long lStartChar = 0, lEndChar = 0;
		RichEdit_GetSel(hWnd, lStartChar, lEndChar);
		long nWidthImage = 0;
		long nHeightImage = 0;
		
		bRet = RichEdit_InsertFace(pTextServices, pTextHost, lpszFileName, nFaceId, nFaceIndex, RGB(255,255,255), TRUE, 40, nImageWidth, nImageHeight);
		if (bRet)
		{
			lEndChar = lStartChar + 1;
			RichEdit_SetSel(hWnd, lStartChar, lEndChar);
			//RichEdit_SetStartIndent(hWnd, 300);
			RichEdit_SetSel(hWnd, lEndChar, lEndChar);
		}
	}
	else if (hWnd == m_richMsgLog.m_hWnd)
	{
		pTextServices = m_richMsgLog.GetTextServices();
		pTextHost = m_richMsgLog.GetTextHost();

		long lStartChar = 0, lEndChar = 0;
		RichEdit_GetSel(hWnd, lStartChar, lEndChar);
		bRet = RichEdit_InsertFace(pTextServices, pTextHost, lpszFileName, nFaceId, nFaceIndex, RGB(255,255,255), TRUE, 40, nImageWidth, nImageHeight);
		if (bRet)
		{
			lEndChar = lStartChar + 1;
			RichEdit_SetSel(hWnd, lStartChar, lEndChar);
			RichEdit_SetStartIndent(hWnd, 300);
			RichEdit_SetSel(hWnd, lEndChar, lEndChar);
		}
	}

	if (pTextServices != NULL)
		pTextServices->Release();
	if (pTextHost != NULL)
		pTextHost->Release();

	return bRet;
}

BOOL CGroupChatDlg::HandleSysFaceId(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
{
	int nFaceId = GetBetweenInt(p+2, _T("[\""), _T("\"]"), -1);
	CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoById(nFaceId);
	if (lpFaceInfo != NULL)
	{
		if (!strText.IsEmpty())
		{
			_RichEdit_ReplaceSel(hRichEditWnd, strText); 
			strText = _T("");
		}

		_RichEdit_InsertFace(hRichEditWnd, lpFaceInfo->m_strFileName.c_str(), 
			lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CGroupChatDlg::HandleSysFaceIndex(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
{
	int nFaceIndex = GetBetweenInt(p+2, _T("[\""), _T("\"]"), -1);
	CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoByIndex(nFaceIndex);
	if (lpFaceInfo != NULL)
	{
		if (!strText.IsEmpty())
		{
			_RichEdit_ReplaceSel(hRichEditWnd, strText); 
			strText = _T("");
		}

		_RichEdit_InsertFace(hRichEditWnd, lpFaceInfo->m_strFileName.c_str(), 
			lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CGroupChatDlg::HandleCustomPic(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
{
	CString strFileName = GetBetweenString(p+2, _T("[\""), _T("\"]")).c_str();
	if (!strFileName.IsEmpty())
	{
		if (!strText.IsEmpty())
		{
			_RichEdit_ReplaceSel(hRichEditWnd, strText); 
			strText = _T("");
		}

		if (::PathIsRelative(strFileName))
			strFileName = m_lpFMGClient->GetChatPicFullName(strFileName).c_str();

		_RichEdit_InsertFace(hRichEditWnd, strFileName, -1, -1);

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CGroupChatDlg::HandleFontInfo(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strTemp = GetBetweenString(p+2, _T("[\""), _T("\"]")).c_str();
	if (!strTemp.empty())
	{
		LPCTSTR lpFontFmt = _T("%[^,],%d,%[^,],%d,%d,%d");
		TCHAR szName[32] = _T("宋体");
		TCHAR szColor[32] = _T("000000");
		int nSize = 9;
		BOOL bBold = FALSE, bItalic = FALSE, bUnderLine = FALSE;

		int nCount = _stscanf(strTemp.c_str(), lpFontFmt, szName, 
			&nSize, &szColor, &bBold, &bItalic, &bUnderLine);
		if (nCount != 6)
			return FALSE;

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_FONT_INFO;
			lpContent->m_FontInfo.m_nSize = nSize;
			lpContent->m_FontInfo.m_clrText = HexStrToRGB(szColor);
			lpContent->m_FontInfo.m_strName = szName;
			lpContent->m_FontInfo.m_bBold = bBold;
			lpContent->m_FontInfo.m_bItalic = bItalic;				
			lpContent->m_FontInfo.m_bUnderLine = bUnderLine;
			arrContent.push_back(lpContent);
		}

		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

// "/f["系统表情id"] /s["系统表情index"] /c["自定义图片路径"]"
void CGroupChatDlg::AddMsg(HWND hRichEditWnd, LPCTSTR lpText)
{
	if (NULL == lpText || NULL ==*lpText)
		return;

	CString strText;

	for (LPCTSTR p = lpText;*p != _T('\0'); p++)
	{
		if (*p == _T('/'))
		{
			if (*(p+1) == _T('/'))
			{
				strText +=*p;
				p++;
				continue;
			}
			else if (*(p+1) == _T('f'))
			{
				if (HandleSysFaceId(hRichEditWnd, p, strText))
					continue;
			}
			else if (*(p+1) == _T('s'))
			{
				if (HandleSysFaceIndex(hRichEditWnd, p, strText))
					continue;
			}
			else if (*(p+1) == _T('c'))
			{
				if (HandleCustomPic(hRichEditWnd, p, strText))
					continue;
			}
		}
		strText +=*p;
	}

	if (!strText.IsEmpty())
	{
		//加一个@XXX效果
		//CString strChatContent(strText);
		//long nStartIndex = -1;
		//long nEndIndex = -1;
		//long nCurrentIndex = 0;

		//CGroupInfo* pGroupInfo = GetGroupInfoPtr();
		//if(pGroupInfo != NULL)
		//{
		//	CBuddyInfo* pBuddyInfo = NULL;
		//	CString strTarget;
		//	CString strLeft;
		//	CString strRight;
		//	for(size_i i=0; pGroupInfo->m_arrMember.size(); ++i)
		//	{
		//		pBuddyInfo = pGroupInfo->m_arrMember[i];
		//		if(pBuddyInfo == NULL)
		//			continue;
		//		
		//		strTarget.Format(_T("@%s"), pBuddyInfo->m_strAccount.c_str());
		//		nStartIndex = strChatContent.Find(strTarget, nCurrentIndex)
		//		if(nStartIndex < 0)
		//			continue;
		//		
		//		strLeft = strChatContent.Left(nStartIndex);
		//		_RichEdit_ReplaceSel(hRichEditWnd, strText);
		//	}
		//}
		_RichEdit_ReplaceSel(hRichEditWnd, strText);
	}
}

void CGroupChatDlg::AddMsgToSendEdit(LPCTSTR lpText)
{
	AddMsg(m_richSend.m_hWnd, lpText);
	m_richSend.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CGroupChatDlg::AddMsgToRecvEdit(time_t nTime, LPCTSTR lpText)
{
	if (NULL == lpText || NULL ==*lpText)
		return;

	TCHAR cTime[32] = {0};
	FormatTime(nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	CString strText;
	strText.Format(_T("%s(%s)  %s\r\n"), m_strUserName, m_strAccount, cTime);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
		_T("微软雅黑"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
	strText = _T("");

	AddMsg(m_richRecv.m_hWnd, lpText);

	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

LRESULT CGroupChatDlg::OnSendChatMsgResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CString strInfo;
	if(wParam == SEND_WHOLE_MSG_FAILED)
	{
		strInfo = _T("                                            ★消息发送失败，请重试！★\r\n");
	}
	else if(wParam == SEND_IMGAE_FAILED)
	{
		strInfo = _T("                                            ★图片发送失败，请重试！★\r\n");
	}
	else if(wParam == SEND_FILE_FAILED)
	{
		strInfo = _T("                                            ★文件发送失败，请重试！★\r\n");
	}

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("微软雅黑"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);

	return (LRESULT)1;
}

//void CGroupChatDlg::AddMsgToRecvEdit(CBuddyMessage* lpGroupMsg)
//{
//	if (NULL == lpGroupMsg || NULL == m_lpFMGClient)
//		return;
//
//	CString strText, strSenderName;
//	tstring strAccountName;
//	CFontInfo fontInfo;
//
//	TCHAR cTime[32] = {0};
//	if (IsToday(lpGroupMsg->m_nTime))
//		FormatTime(lpGroupMsg->m_nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));
//	else
//		FormatTime(lpGroupMsg->m_nTime, _T("%Y-%m-%d %H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));
//
//	GetSenderInfo(lpGroupMsg->m_nFromUin, strSenderName, strAccountName);
//
//	m_richRecv.SetAutoURLDetect(FALSE);
//
//	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
//
//
//	strText.Format(_T("%s("), strSenderName);
//	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
//		_T("宋体"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
//
//	strText.Format(_T("%s"), strAccountName.c_str());
//	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
//		_T("宋体"), 10, RGB(0, 114, 193), FALSE, FALSE, TRUE, TRUE, 0);
//	
//	strText.Format(_T(")  %s\r\n"), cTime);
//	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
//		_T("宋体"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
//
//	m_richRecv.SetAutoURLDetect(TRUE);
//
//	for (int i = 0; i < (int)lpGroupMsg->m_arrContent.size(); i++)
//	{
//		CContent* lpContent = lpGroupMsg->m_arrContent[i];
//		if (lpContent != NULL)
//		{
//			switch (lpContent->m_nType)
//			{
//			case CONTENT_TYPE_FONT_INFO:
//				{
//					fontInfo.m_strName = lpContent->m_FontInfo.m_strName;
//					fontInfo.m_nSize = lpContent->m_FontInfo.m_nSize;
//					fontInfo.m_clrText = lpContent->m_FontInfo.m_clrText;
//					fontInfo.m_bBold = lpContent->m_FontInfo.m_bBold;
//					fontInfo.m_bItalic = lpContent->m_FontInfo.m_bItalic;
//					fontInfo.m_bUnderLine = lpContent->m_FontInfo.m_bUnderLine;
//				}
//				break;
//
//			case CONTENT_TYPE_TEXT:
//				{
//					RichEdit_ReplaceSel(m_richRecv.m_hWnd, lpContent->m_strText.c_str(), 
//						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
//						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
//						fontInfo.m_bUnderLine, FALSE, 300);
//				}
//				break;
//
//			case CONTENT_TYPE_FACE:
//				{
//					if (m_lpFaceList != NULL)
//					{
//						CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoById(lpContent->m_nFaceId);
//						if (lpFaceInfo != NULL)
//						{
//							_RichEdit_InsertFace(m_richRecv.m_hWnd, 
//								lpFaceInfo->m_strFileName.c_str(), lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);
//						}
//					}
//				}
//				break;
//
//			case CONTENT_TYPE_CUSTOM_FACE:
//				{
//					tstring strFileName = m_lpFMGClient->GetChatPicFullName(lpContent->m_CFaceInfo.m_strName.c_str());
//					if (!Hootina::CPath::IsFileExist(strFileName.c_str()))
//						strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
//					_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
//				}
//				break;
//			}
//		}
//	}
//
//	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
//	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
//	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
//}


void CGroupChatDlg::AddMsgToRecvEdit(CBuddyMessage* lpGroupMsg)
{
	if (NULL == lpGroupMsg || NULL == m_lpFMGClient)
		return;

	CString strText, strSenderName;
	tstring strAccountName;
	CFontInfo fontInfo;

	UINT nMsgType = lpGroupMsg->m_nMsgType;

	TCHAR cTime[32] = {0};
	if (IsToday(lpGroupMsg->m_nTime))
		FormatTime(lpGroupMsg->m_nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));
	else
		FormatTime(lpGroupMsg->m_nTime, _T("%Y-%m-%d %H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	GetSenderInfo(lpGroupMsg->m_nFromUin, strSenderName, strAccountName);

	m_richRecv.SetAutoURLDetect(FALSE);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);

	if(nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
	{
		strText.Format(_T("%s("), strSenderName);
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
			_T("微软雅黑"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);

		strText.Format(_T("%s"), strAccountName.c_str());
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
			_T("微软雅黑"), 10, RGB(0, 114, 193), FALSE, FALSE, TRUE, TRUE, 0);
		
		strText.Format(_T(")  %s\r\n"), cTime);
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
			_T("微软雅黑"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
	}

	m_richRecv.SetAutoURLDetect(TRUE);
	
	CBuddyMessage* lpBuddyMsg = lpGroupMsg;
	CString strInfo;
	tstring	strFileName;
	//！！字体信息必须放在文本信息的前面
	for (int i = 0; i < (int)lpBuddyMsg->m_arrContent.size(); i++)
	{
		CContent* lpContent = lpBuddyMsg->m_arrContent[i];
		if (lpContent != NULL)
		{
			switch (lpContent->m_nType)
			{
			case CONTENT_TYPE_FONT_INFO:
				{
					fontInfo.m_strName = lpContent->m_FontInfo.m_strName;
					fontInfo.m_nSize = lpContent->m_FontInfo.m_nSize;
					fontInfo.m_clrText = lpContent->m_FontInfo.m_clrText;
					fontInfo.m_bBold = lpContent->m_FontInfo.m_bBold;
					fontInfo.m_bItalic = lpContent->m_FontInfo.m_bItalic;
					fontInfo.m_bUnderLine = lpContent->m_FontInfo.m_bUnderLine;
				}
				break;

			case CONTENT_TYPE_TEXT:
				{
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, lpContent->m_strText.c_str(), 
						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
						fontInfo.m_bUnderLine, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FACE:
				{
					if (m_lpFaceList != NULL)
					{
						CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoById(lpContent->m_nFaceId);
						if (lpFaceInfo != NULL)
						{
							_RichEdit_InsertFace(m_richRecv.m_hWnd, 
								lpFaceInfo->m_strFileName.c_str(), lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);
						}
					}
				}
				break;

			case CONTENT_TYPE_CHAT_IMAGE:
				{
					if(nMsgType == CONTENT_TYPE_TEXT)
					{
						long lStartChar = 0, lEndChar = 0;
						RichEdit_GetSel(m_richRecv.m_hWnd, lStartChar, lEndChar);
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
                        if (!Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str()) == 0)
						{
							m_mapRecvFileInfo.insert(std::pair<CString, long>(strFileName.c_str(), lStartChar));						
							strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadImageProgress.gif");
						}
				
						_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
					}
					else if(nMsgType == CONTENT_TYPE_IMAGE_CONFIRM)
					{
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
						std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(strFileName.c_str());
						if(iter != m_mapRecvFileInfo.end())
						{
							RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
							if(!Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str())==0)
								strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
						
							_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
						}
					}
					else if(nMsgType == CONTENT_TYPE_MOBILE_IMAGE)
					{
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
						RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
						if(!Hootina::CPath::IsFileExist(strFileName.c_str()))
							strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
						
						_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
					}
				}
				break;
			}

		}
	}
	
	
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

LRESULT CGroupChatDlg::OnSendFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	FileProgress* pFileProgress = (FileProgress*)lParam;
	if(pFileProgress == NULL)
		return (LRESULT)0;

	long nPercent = pFileProgress->nPercent;
	
	//std::map<CString, long>::const_iterator iter=m_mapSendFileInfo.find(pFileProgress->szDestPath);
	////发送聊天图片的进度
	//if(iter == m_mapSendFileInfo.end())
	//{
	//	//m_staPicUploadProgress.ShowWindow(SW_SHOW);
	//	TCHAR szProgressInfo[MAX_PATH] = {0};
	//	_stprintf_s(szProgressInfo, ARRAYSIZE(szProgressInfo), _T("正在发送图片%s：%d%%."), Hootina::CPath::GetFileName(pszFileName).c_str(), nPercent);

	//	m_staPicUploadProgress.SetWindowText(szProgressInfo);

	//	if(wParam >=100 )
	//		m_staPicUploadProgress.ShowWindow(SW_HIDE);
	//}

	delete pFileProgress;
	
	return (LRESULT)1;
}

LRESULT CGroupChatDlg::OnSendFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CUploadFileResult* pResult = (CUploadFileResult*)lParam;
	if(pResult == NULL)
		return (LRESULT)0;
	
	//上传图片结果
	std::map<CString, long>::const_iterator iter=m_mapSendFileInfo.find(pResult->m_szLocalName);
	if(iter == m_mapSendFileInfo.end())
	{
		if(pResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_IMAGE)
		{
			if(wParam == SEND_FILE_FAILED)
			{
				//AtlTrace(_T("Fail to send file:%s.\n"), pResult->m_szLocalName);
				TCHAR szInfo[MAX_PATH] = {0};
				_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ☆发送图片[%s]失败，请重试！☆\r\n"), ::PathFindFileName(pResult->m_szLocalName));
				RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
				RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("微软雅黑"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			}
            //图片发送成功，追加上传图片成功消息
            else
            {
                SendConfirmMessage(pResult);
            }
		}
	}
	
	delete pResult;

	return 1;
}

LRESULT CGroupChatDlg::OnRecvFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	FileProgress* pFileProgress = (FileProgress*)lParam;
	if(pFileProgress == NULL)
		return 0;

	long nPercent = pFileProgress->nPercent;
	std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(pFileProgress->szDestPath);
	delete pFileProgress;

	if(iter == m_mapRecvFileInfo.end())
		return 0;
	
	long nIndex = nPercent / 10;
	RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
	CString strProgressFile;
	strProgressFile.Format(_T("%sImage\\FileProgress\\percent%d.png"), g_szHomePath, nIndex);
	_RichEdit_InsertFace(m_richRecv.m_hWnd, strProgressFile, -1, -1);
	
	return 1;
}

LRESULT CGroupChatDlg::OnRecvFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CFileItemRequest* pFileItem = (CFileItemRequest*)lParam;
	if(pFileItem == NULL)
		return 0;

	std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(pFileItem->m_szFilePath);
	if(iter == m_mapRecvFileInfo.end())
		return 0;
	
	//聊天图片下载结果
	if(iter->second > 0)
	{
		if(wParam == RECV_FILE_SUCCESS)
		{
			RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
			_RichEdit_InsertFace(m_richRecv.m_hWnd, pFileItem->m_szFilePath, -1, -1);
		}
		else if(wParam == RECV_FILE_FAILED)
		{
			RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
			tstring strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
			_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
		}
	}

	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	m_mapRecvFileInfo.erase(iter);
	

	return 1;
}

void CGroupChatDlg::SendConfirmMessage(const CUploadFileResult* pUploadFileResult)
{
    if (pUploadFileResult == NULL)
        return;

    //上传图片结果
    if (pUploadFileResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_IMAGE)
    {
        time_t nTime = time(NULL);
        TCHAR szMd5[64] = { 0 };
        EncodeUtil::AnsiToUnicode(pUploadFileResult->m_szMd5, szMd5, ARRAYSIZE(szMd5));
        CString strImageName;
        strImageName.Format(_T("%s.%s"), szMd5, Hootina::CPath::GetExtension(pUploadFileResult->m_szLocalName).c_str());
        long nWidth = 0;
        long nHeight = 0;
        GetImageWidthAndHeight(pUploadFileResult->m_szLocalName, nWidth, nHeight);
        char szUtf8FileName[MAX_PATH] = { 0 };
        EncodeUtil::UnicodeToUtf8(strImageName, szUtf8FileName, ARRAYSIZE(szUtf8FileName));
        CStringA strImageAcquireMsg;
        //if (pUploadFileResult->m_bSuccessful)
        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
        //else
        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);

        if (pUploadFileResult->m_bSuccessful)
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%lld,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_nFileSize, nWidth, nHeight);
        else
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%lld,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_nFileSize, nWidth, nHeight);

        long nBodyLength = strImageAcquireMsg.GetLength() + 1;
        char* pszMsgBody = new char[nBodyLength];
        memset(pszMsgBody, 0, nBodyLength);
        strcpy_s(pszMsgBody, nBodyLength, strImageAcquireMsg);
        CSentChatConfirmImageMessage* pConfirm = new CSentChatConfirmImageMessage();
        pConfirm->m_hwndChat = pUploadFileResult->m_hwndReflection;
        pConfirm->m_pszConfirmBody = pszMsgBody;
        pConfirm->m_uConfirmBodySize = nBodyLength - 1;
        pConfirm->m_uSenderID = pUploadFileResult->m_uSenderID;
        pConfirm->m_setTargetIDs = pUploadFileResult->m_setTargetIDs;
        if (pConfirm->m_setTargetIDs.size() > 1)
            pConfirm->m_nType = CHAT_CONFIRM_TYPE_MULTI;
        else
            pConfirm->m_nType = CHAT_CONFIRM_TYPE_SINGLE;

        //SendBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom/* = NULL*/)

        m_lpFMGClient->m_SendMsgThread.AddItem(pConfirm);
    }
}

// 打开消息记录浏览窗口
void CGroupChatDlg::OpenMsgLogBrowser()
{
	CString strMsgFile = m_lpFMGClient->GetMsgLogFullName().c_str();
	strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);

	CString strChatPicDir = m_lpFMGClient->GetChatPicFolder().c_str();
	strChatPicDir.Replace(_T("\\"), _T("/"));

	
	std::vector<GROUP_MSG_LOG*> arrMsgLog;
	UINT nRows = 10;
	UINT nOffset = 0;
	if(m_nMsgLogRecordOffset > 1)
		nOffset = m_nMsgLogRecordOffset-1;
	//从消息记录文件中获取消息记录
	long cntArrMsgLog = m_MsgLogger.ReadGroupMsgLog(m_nGroupCode, nOffset, nRows, arrMsgLog);
	
	//添加到消息记录富文本控件中
	AddMsgToMsgLogEdit(arrMsgLog);

	ShowMsgLogButtons(TRUE);
	m_richMsgLog.SetFocus();
}

// 关闭消息记录浏览窗口
void CGroupChatDlg::CloseMsgLogBrowser()
{
	::SendMessage(m_richMsgLog.m_hWnd, WM_SETTEXT, 0, 0L);

	ShowMsgLogButtons(FALSE);

	CalculateMsgLogCountAndOffset();
}

void CGroupChatDlg::AddMsgToMsgLogEdit(std::vector<GROUP_MSG_LOG*>& arrMsgLog)
{
	m_richMsgLog.SetWindowText(_T(""));
	
	UINT nID;
	UINT nUTalkNum;
	tstring strNickName;
	UINT64 nTime;
	//BOOL bSendFlag;
	tstring strContent;
	TCHAR szTime[32];
	COLORREF clrNickName(RGB(0, 0, 0));
	size_t nSize = arrMsgLog.size();
	for (size_t i = 0; i < nSize; ++i)
	{
		nID = arrMsgLog[i]->nID;
		nUTalkNum = arrMsgLog[i]->nUTalkNum;
		strNickName = arrMsgLog[i]->strNickName;
		nTime = arrMsgLog[i]->nTime;
		//bSendFlag = arrMsgLog[i]->bSendFlag;
		strContent = arrMsgLog[i]->strContent;

		
		memset(szTime, 0, sizeof(szTime));
		if(IsToday(nTime))	
			FormatTime(nTime, _T("%H:%M:%S"), szTime, ARRAYSIZE(szTime));
		else
			FormatTime(nTime, _T("%Y-%m-%d %H:%M:%S"), szTime, ARRAYSIZE(szTime));

		CString strText;
		strText.Format(_T("%s  %s\r\n"), strNickName.c_str(), szTime);

		RichEdit_SetSel(m_richMsgLog.m_hWnd, -1, -1);
		if(nUTalkNum == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
			clrNickName = RGB(0, 128, 64);
		else
			clrNickName = RGB(0, 0, 255);
		RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, strText, _T("微软雅黑"), 10, clrNickName, FALSE, FALSE, FALSE, FALSE, 0);
		strText = _T("");

		AnalyseContent(strContent);
		
		//AddMsg(m_richMsgLog.m_hWnd, strContent.c_str());

		RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, _T("\r\n"));
		RichEdit_SetStartIndent(m_richMsgLog.m_hWnd, 0);
		//m_richMsgLog.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	}

	//m_richMsgLog.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	//m_richMsgLog.UpdateWindow();
}

// "/f["系统表情id"] /c["自定义表情文件名"] /o[字体名称，大小，颜色，加粗，倾斜，下划线]"
void CGroupChatDlg::AnalyseContent(tstring& strContent, HWND hRichWnd/*=NULL*/)
{
	//CONTENT_TYPE m_nType;			// 内容类型
	//CFontInfo m_FontInfo;			// 字体信息
	//tstring m_strText;			// 文本信息
	//int m_nFaceId;				// 系统表情Id
	//CCustomFaceInfo m_CFaceInfo;	// 自定义表情信息

	tstring strText;
	CContent* lpContent = NULL;
	LPCTSTR lpMsg = strContent.c_str();
	std::vector<CContent*> arrContent;

	for (LPCTSTR p = lpMsg;*p != _T('\0'); p++)
	{
		arrContent.clear();
		if (*p == _T('/'))
		{
			if (*(p+1) == _T('/'))
			{
				strText +=*p;
				p++;
				continue;
			}
			else if (*(p+1) == _T('o'))
			{
				//strText +=*p;
				//arrContent.clear();
				if (HandleFontInfo(p, strText, arrContent))
				{
					if(hRichWnd == NULL)
						AddMsgToMsgLogEdit(arrContent);
					else
						AddMsgToRecvEdit(arrContent);
					continue;
				}
			}
			else if (*(p+1) == _T('f'))
			{
				//strText +=*p;
				//arrContent.clear();
				if (HandleSysFaceId(p, strText, arrContent))
				{
					if(hRichWnd == NULL)
						AddMsgToMsgLogEdit(arrContent);
					else
						AddMsgToRecvEdit(arrContent);
					continue;
				}
			}
			else if (*(p+1) == _T('c'))
			{
				//strText +=*p;
				//arrContent.clear();
				if (HandleCustomPic(p, strText, arrContent))
				{
					if(hRichWnd == NULL)
						AddMsgToMsgLogEdit(arrContent);
					else
						AddMsgToRecvEdit(arrContent);
					continue;
				}
			}
			//else if (*(p+1) == _T('s'))						//窗口抖动
			//{
			//	//arrContent.clear();
			//	if (HandleShake(p, strText, arrContent))
			//	{
			//		if(hRichWnd == NULL)
			//			AddMsgToMsgLogEdit(arrContent);
			//		else
			//			AddMsgToRecvEdit(arrContent);
			//		continue;
			//	}
			//}
			//else if (*(p+1) == _T('i'))					   //文件
			//{
			//	if (HandleFile(p, strText, arrContent))
			//	{
			//		if(hRichWnd == NULL)
			//			AddMsgToMsgLogEdit(arrContent);
			//		else
			//			AddMsgToRecvEdit(arrContent);
			//		continue;
			//	}
			//}
		}
		
		strText +=*p;
	}

	if (!strText.empty())
	{
		lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_TEXT;
			lpContent->m_strText = strText;
			arrContent.push_back(lpContent);
			if(hRichWnd == NULL)
				AddMsgToMsgLogEdit(arrContent);
			else
				AddMsgToRecvEdit(arrContent);
		}
		strText = _T("");
	}

}

BOOL CGroupChatDlg::HandleSysFaceId(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	int nFaceId = GetBetweenInt(p+2, _T("[\""), _T("\"]"), -1);
	if (nFaceId != -1)
	{
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_FACE;
			lpContent->m_nFaceId = nFaceId;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CGroupChatDlg::HandleCustomPic(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strFileName = GetBetweenString(p+2, _T("[\""), _T("\"]"));
	if (!strFileName.empty())
	{
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_CHAT_IMAGE;
			lpContent->m_CFaceInfo.m_strName = strFileName;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

void CGroupChatDlg::AddMsgToRecvEdit(std::vector<CContent*>& arrContent)
{
	//！！字体信息必须放在文本信息的前面
	static CFontInfo fontInfo;
	for (long i = 0; i < (long)arrContent.size(); i++)
	{
		CContent* lpContent = arrContent[i];
		if (lpContent != NULL)
		{
			switch (lpContent->m_nType)
			{
			case CONTENT_TYPE_SHAKE_WINDOW:
				{
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("抖了您一下。"), _T("微软雅黑"), 10,  0, TRUE, FALSE, FALSE, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FONT_INFO:
				{
					fontInfo.m_strName = lpContent->m_FontInfo.m_strName;
					fontInfo.m_nSize = lpContent->m_FontInfo.m_nSize;
					fontInfo.m_clrText = lpContent->m_FontInfo.m_clrText;
					fontInfo.m_bBold = lpContent->m_FontInfo.m_bBold;
					fontInfo.m_bItalic = lpContent->m_FontInfo.m_bItalic;
					fontInfo.m_bUnderLine = lpContent->m_FontInfo.m_bUnderLine;
				}
				break;

			case CONTENT_TYPE_TEXT:
				{
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, lpContent->m_strText.c_str(), 
						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
						fontInfo.m_bUnderLine, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FACE:
				{
					if (m_lpFaceList != NULL)
					{
						CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoById(lpContent->m_nFaceId);
						if (lpFaceInfo != NULL)
						{
							_RichEdit_InsertFace(m_richRecv.m_hWnd, 
								lpFaceInfo->m_strFileName.c_str(), lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);
						}
					}
				}
				break;

			case CONTENT_TYPE_CHAT_IMAGE:
				{
					tstring strFileName;
					//安卓发送过来的路径类似这样：/storage/sdcard/windows/BstSharedFolder/]FHGGRBAA@85{PP{W3S]8C52.jpg
					//先检测是否是类似于C:\dd\xx.png的路径
					if(::PathIsRelative(lpContent->m_CFaceInfo.m_strName.c_str()))
						strFileName	= m_lpFMGClient->m_UserMgr.GetChatPicFullName(::PathFindFileName(lpContent->m_CFaceInfo.m_strName.c_str()));
					else
						strFileName = lpContent->m_CFaceInfo.m_strName;
					if (!Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str())==0)
						strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
					_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
				}
				break;

			case CONTENT_TYPE_FILE:
				RichEdit_ReplaceSel(m_richRecv.m_hWnd, lpContent->m_CFaceInfo.m_strFileName.c_str(), 
						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
						fontInfo.m_bUnderLine, FALSE, 300);
				break;
			}
		}
	}
}

void CGroupChatDlg::AddMsgToMsgLogEdit(std::vector<CContent*>& arrContent)
{
	//！！字体信息必须放在文本信息的前面
	static CFontInfo fontInfo;
	for (long i = 0; i < (long)arrContent.size(); i++)
	{
		CContent* lpContent = arrContent[i];
		if (lpContent != NULL)
		{
			switch (lpContent->m_nType)
			{
			case CONTENT_TYPE_SHAKE_WINDOW:
				{
					RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, _T("抖了您一下。"), _T("微软雅黑"), 10,  0, TRUE, FALSE, FALSE, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FONT_INFO:
				{
					fontInfo.m_strName = lpContent->m_FontInfo.m_strName;
					fontInfo.m_nSize = lpContent->m_FontInfo.m_nSize;
					fontInfo.m_clrText = lpContent->m_FontInfo.m_clrText;
					fontInfo.m_bBold = lpContent->m_FontInfo.m_bBold;
					fontInfo.m_bItalic = lpContent->m_FontInfo.m_bItalic;
					fontInfo.m_bUnderLine = lpContent->m_FontInfo.m_bUnderLine;
				}
				break;

			case CONTENT_TYPE_TEXT:
				{
					RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, lpContent->m_strText.c_str(), 
						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
						fontInfo.m_bUnderLine, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FACE:
				{
					if (m_lpFaceList != NULL)
					{
						CFaceInfo* lpFaceInfo = m_lpFaceList->GetFaceInfoById(lpContent->m_nFaceId);
						if (lpFaceInfo != NULL)
						{
							_RichEdit_InsertFace(m_richMsgLog.m_hWnd, 
								lpFaceInfo->m_strFileName.c_str(), lpFaceInfo->m_nId, lpFaceInfo->m_nIndex);
						}
					}
				}
				break;

			case CONTENT_TYPE_CHAT_IMAGE:
				{
					tstring strFileName;
					//安卓发送过来的路径类似这样：/storage/sdcard/windows/BstSharedFolder/]FHGGRBAA@85{PP{W3S]8C52.jpg
					//先检测是否是类似于C:\dd\xx.png的路径
					if(::PathIsRelative(lpContent->m_CFaceInfo.m_strName.c_str()))
						strFileName	= m_lpFMGClient->m_UserMgr.GetChatPicFullName(::PathFindFileName(lpContent->m_CFaceInfo.m_strName.c_str()));
					else
						strFileName = lpContent->m_CFaceInfo.m_strName;
					if (!Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str())==0)
						strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
					_RichEdit_InsertFace(m_richMsgLog.m_hWnd, strFileName.c_str(), -1, -1);
				}
				break;

			case CONTENT_TYPE_FILE:
				RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, lpContent->m_CFaceInfo.m_strFileName.c_str(), 
						fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
						fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, 
						fontInfo.m_bUnderLine, FALSE, 300);
				break;
			}
		}
	}
}

void CGroupChatDlg::ResizeImageInRecvRichEdit()
{
	std::vector<ImageInfo*> arrImageInfo;
	RichEdit_GetImageInfo(m_richRecv, arrImageInfo);
	size_t nSize = arrImageInfo.size();
	if(nSize == 0)
		return;
	
	ImageInfo* pImage = NULL;
	for(size_t i=0; i<nSize; ++i)
	{
		pImage = arrImageInfo[i];
		if(pImage == NULL)
			continue;
		
		RichEdit_SetSel(m_richRecv.m_hWnd, pImage->nStartPos, pImage->nEndPos);
		_RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T(""));
		_RichEdit_InsertFace(m_richRecv.m_hWnd, pImage->szPath, -1, -1);
		delete pImage;
	}

	arrImageInfo.clear();

}

void CGroupChatDlg::RecordWindowSize()
{
	if(IsZoomed() || IsIconic())
		return;
	
	CRect rtWindow;
	GetWindowRect(&rtWindow);
	if(m_bMsgLogWindowVisible)
		m_lpFMGClient->m_UserConfig.SetGroupDlgWidth(rtWindow.Width()-GROUP_MSG_LOG_WIDTH);
	else
		m_lpFMGClient->m_UserConfig.SetGroupDlgWidth(rtWindow.Width());

	m_lpFMGClient->m_UserConfig.SetGroupDlgHeight(rtWindow.Height());		
}

BOOL CGroupChatDlg::GetImageDisplaySizeInRichEdit(PCTSTR pszFileName, HWND hWnd, long& nWidth, long& nHeight)
{
	//1像素约等于20缇
	const long TWIPS = 20;
	long nWidthImage = 0;
	long nHeightImage = 0;
	GetImageWidthAndHeight(pszFileName, nWidthImage, nHeightImage);

	if(hWnd == m_richSend.m_hWnd)
	{
		CRect rtRichSend;
		::GetClientRect(hWnd, &rtRichSend);
		//图片太小，不缩放
		if(nHeightImage <= rtRichSend.Height())
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//出错
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//按比例缩放
		nWidth = rtRichSend.Height()*nWidthImage/nHeightImage*TWIPS;
		nHeight = rtRichSend.Height()*TWIPS;
	}
	else if(hWnd==m_richRecv.m_hWnd)
	{
		CRect rtRecv;
		::GetClientRect(hWnd, &rtRecv);

		//图片太小，不缩放
		if(nHeightImage <= rtRecv.Height())
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//出错
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//按比例缩放
		//图片宽度为窗口的四分之三
		nWidth = rtRecv.Height()*3/4*nWidthImage/nHeightImage*TWIPS;
		nHeight = rtRecv.Height()*3/4*TWIPS;
	}
	else if(hWnd == m_richMsgLog.m_hWnd)
	{
		CRect rtMsgLog;
		::GetClientRect(hWnd, &rtMsgLog);

		//图片太小，不缩放
		if(nWidthImage <= rtMsgLog.Width()-20)
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//出错
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//按比例缩放
		//图片宽度为窗口的四分之一
		nWidth = (rtMsgLog.Width()/4)*TWIPS;
		nHeight = nWidth*nHeightImage/nWidthImage;
	}

	return TRUE;
}

void CGroupChatDlg::OnDropFiles(HDROP hDropInfo)
{ 
	UINT nFileNum = ::DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0); // 拖拽文件个数  
    TCHAR szFileName[MAX_PATH];  
    for (UINT i=0; i<nFileNum; ++i)    
    {  
		::DragQueryFile(hDropInfo, i, szFileName, MAX_PATH);//获得拖曳的文件名  
        HandleFileDragResult(szFileName);    
    }  
    DragFinish(hDropInfo);      //释放hDropInfo  

    //InvalidateRect(hwnd, NULL, TRUE); 
}

BOOL CGroupChatDlg::HandleFileDragResult(PCTSTR lpszFileName)
{
	if(lpszFileName == NULL) 
		return FALSE;
	
	//如果是文件夹，则发送文件夹
	if(Hootina::CPath::IsDirectory(lpszFileName))
	{
		//TODO: 发送文件夹
		return TRUE;
	}

	CString strFileExtension(Hootina::CPath::GetExtension(lpszFileName).c_str());
	strFileExtension.MakeLower();
	
	//如果是图片格式，则插入图片
	if( strFileExtension==_T("jpg")  ||
		strFileExtension==_T("jpeg") ||
	    strFileExtension==_T("png")  ||
	    strFileExtension==_T("bmp")  ||
		strFileExtension==_T("gif") )
	{
		UINT64 nFileSize = IUGetFileSize2(lpszFileName);
		if(nFileSize > MAX_CHAT_IMAGE_SIZE)
		{
			::MessageBox(m_hWnd, _T("图片大小超过10M，请使用截图工具。"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
			return FALSE;
		}
		
		_RichEdit_InsertFace(m_richSend.m_hWnd, lpszFileName, -1, -1);
		m_richSend.SetFocus();
		return TRUE;
	}
	//TODO: 将来用于支持群文件功能
	//else
	//{
	//	return SendOfflineFile(lpszFileName);
	//}


	return FALSE;
}

void CGroupChatDlg::CalculateMsgLogCountAndOffset()
{
	//读取消息记录个数
	CString strMsgFile = m_lpFMGClient->GetMsgLogFullName().c_str();
	strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);
	long nTotal = m_MsgLogger.GetGroupMsgLogCount(m_nGroupCode);
	
	long nPageCount = nTotal/10;
	if(nTotal%10 != 0)
		++nPageCount;

	m_nMsgLogRecordOffset = 1;
	m_nMsgLogCurrentPageIndex = 1;
	while(TRUE)
	{
		m_nMsgLogRecordOffset += 10;
		++m_nMsgLogCurrentPageIndex;
		if(m_nMsgLogCurrentPageIndex > nPageCount)
		{
			m_nMsgLogRecordOffset -= 10;
			--m_nMsgLogCurrentPageIndex;
			break;
		}
	}
	CString strInfo;
	if(nPageCount > 0)
		strInfo.Format(_T("%d/%d"), m_nMsgLogCurrentPageIndex, nPageCount);
	else
		strInfo = _T("0/0");

	m_staMsgLogPage.SetWindowText(strInfo);
	m_staMsgLogPage.Invalidate(FALSE);
}


void CGroupChatDlg::OnMsgLogPage(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	long nTotalCount = m_MsgLogger.GetGroupMsgLogCount(m_nGroupCode);
	long nPageCount = nTotalCount/10;
	if(nTotalCount%10 != 0)
		++nPageCount;

	if(nPageCount == 0)
		return;
	
	switch(nID)
	{
	case IDC_FIRSTMSGLOG:
		if(m_nMsgLogCurrentPageIndex == 1)
			return;
		m_nMsgLogRecordOffset = 1;
		m_nMsgLogCurrentPageIndex = 1;
		break;

	case IDC_PREVMSGLOG:
		if(m_nMsgLogCurrentPageIndex == 1)
			return;
		m_nMsgLogRecordOffset -= 10;
		--m_nMsgLogCurrentPageIndex;
		if(m_nMsgLogRecordOffset <= 0)
		{
			m_nMsgLogRecordOffset = 1;
			m_nMsgLogCurrentPageIndex = 1;
		}
		break;

	case IDC_NEXTMSGLOG:
		if(m_nMsgLogCurrentPageIndex == nPageCount)
			return;
		m_nMsgLogRecordOffset += 10;
		++m_nMsgLogCurrentPageIndex;
		if(m_nMsgLogCurrentPageIndex > nPageCount)
		{
			m_nMsgLogRecordOffset -= 10;
			--m_nMsgLogCurrentPageIndex;
		}
		break;

	case IDC_LASTMSGLOG:
		{
			if(m_nMsgLogCurrentPageIndex == nPageCount)
				return;
			while(TRUE)
			{
				m_nMsgLogRecordOffset += 10;
				++m_nMsgLogCurrentPageIndex;
				if(m_nMsgLogCurrentPageIndex > nPageCount)
				{
					m_nMsgLogRecordOffset -= 10;
					--m_nMsgLogCurrentPageIndex;
					break;
				}
			}
		}
		break;
	}
	
	//AtlTrace(_T("Offset: %d, PageIndex: %d, TotalPage: %d\n"), m_nMsgLogRecordOffset, m_nMsgLogCurrentPageIndex, nPageCount);
	CString strPageInfo;
	strPageInfo.Format(_T("%d/%d"), m_nMsgLogCurrentPageIndex, nPageCount);
	m_staMsgLogPage.SetWindowText(strPageInfo);
	m_staMsgLogPage.Invalidate(FALSE);

	OpenMsgLogBrowser();
}

void CGroupChatDlg::ShowLastMsgInRecvRichEdit()
{
	CString strMsgFile(m_lpFMGClient->GetMsgLogFullName().c_str());
	//strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);

	//CString strChatPicDir = m_lpFMGClient->GetChatPicFolder().c_str();
	//strChatPicDir.Replace(_T("\\"), _T("/"));

	//消息记录中用户未读的消息个数
	long nMsgCntUnread = 0;
	CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
	if(lpMsgList != NULL)
	{
		CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_GROUP, m_nGroupCode);
		if (lpMsgSender != NULL)
			nMsgCntUnread = lpMsgSender->GetMsgCount();
	}
	
	long nTotalCount = (long)m_MsgLogger.GetGroupMsgLogCount(m_nGroupCode);
	//将未读消息从历史记录中去除
	nTotalCount -= nMsgCntUnread;
	if(nTotalCount < 0)
		nTotalCount = 0;

	if(nTotalCount == 0)
		return;

	UINT nRows = 5;
	if((UINT)nTotalCount < nRows)
		nRows = (UINT)nTotalCount;

	std::vector<GROUP_MSG_LOG*> arrMsgLog;
	//从消息记录文件中获取消息记录
	long cntArrMsgLog = 0;
	if(nTotalCount > 5)
		cntArrMsgLog = m_MsgLogger.ReadGroupMsgLog(m_nGroupCode, nTotalCount-5, nRows, arrMsgLog);
	else
		cntArrMsgLog = m_MsgLogger.ReadGroupMsgLog(m_nGroupCode, 0, nRows, arrMsgLog);
	
	//添加到消息记录富文本控件中
	AddMsgToRecvEdit(arrMsgLog);
}

void CGroupChatDlg::AddMsgToRecvEdit(std::vector<GROUP_MSG_LOG*>& arrMsgLog)
{
	UINT nID;
	UINT nUTalkNum;
	tstring strNickName;
	UINT64 nTime;
	//BOOL bSendFlag;
	tstring strContent;
	TCHAR szTime[32];
	
	size_t nSize = arrMsgLog.size();
	for (size_t i = 0; i < nSize; ++i)
	{
		nID = arrMsgLog[i]->nID;
		nUTalkNum = arrMsgLog[i]->nUTalkNum;
		strNickName = arrMsgLog[i]->strNickName;
		nTime = arrMsgLog[i]->nTime;
		//bSendFlag = arrMsgLog[i]->bSendFlag;
		strContent = arrMsgLog[i]->strContent;

		
		memset(szTime, 0, sizeof(szTime));
		if(IsToday(nTime))	
			FormatTime(nTime, _T("%H:%M:%S"), szTime, ARRAYSIZE(szTime));
		else
			FormatTime(nTime, _T("%Y-%m-%d %H:%M:%S"), szTime, ARRAYSIZE(szTime));

		CString strText;
		strText.Format(_T("%s  %s\r\n"), strNickName.c_str(), szTime);

		RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
		if(nUTalkNum == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("微软雅黑"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
		else
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("微软雅黑"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
		strText = _T("");

		AnalyseContent(strContent, m_richRecv.m_hWnd);
		
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
		RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
		m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
		
	}

	if(nSize > 0)
	{
		RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("                                            ————以上是历史消息————\r\n"), _T("微软雅黑"), 9, RGB(128,128,128), FALSE, FALSE, FALSE, FALSE, 0);
		m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	}
	//RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\n"));
	////TODO: 奇怪，滚动条为什么不能滚到低？
	//m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CGroupChatDlg::ReCaculateCtrlPostion(long nMouseY)
{
		CRect rtClient;
	::GetClientRect(m_hWnd, &rtClient);
	
	//不允许将发送框尺寸拉的太大或者太小，那样会影响某些控件内部的画法（某些控件内部画法要求必须满足一定的尺寸）
	if(nMouseY<=280 || nMouseY>= rtClient.Height()-95)
		return;

	
	RECT rtBase;
	::GetWindowRect(m_richRecv, &rtBase);
	POINT ptBase;
	ptBase.x = rtBase.left;
	ptBase.y = rtBase.top;
	::ScreenToClient(m_hWnd, &ptBase);
	
	CRect rtRichRecv, rtSplitter, rtMidToolbar, rtRichSend;
	::GetClientRect(m_richRecv, &rtRichRecv);
	::GetClientRect(m_SplitterCtrl, &rtSplitter);
	::GetClientRect(m_tbMid, &rtMidToolbar);
	::GetClientRect(m_richSend, &rtRichSend);
	HDWP hdwp = ::BeginDeferWindowPos(5);
	//接收框
	if(m_FontSelDlg.IsWindowVisible())
	{
		//AtlTrace(_T("nMouseY-ptBase.y-3*CHATDLG_TOOLBAR_HEIGHT: %d\n"), nMouseY-ptBase.y-3*CHATDLG_TOOLBAR_HEIGHT);
		//TODO: nMouseY-ptBase.y-2*CHATDLG_TOOLBAR_HEIGHT为什么不起作用呢？
		::DeferWindowPos(hdwp, m_richRecv, NULL, 0, 0, rtRichRecv.Width(), nMouseY-ptBase.y-2*CHATDLG_TOOLBAR_HEIGHT, SWP_NOMOVE|SWP_NOZORDER);
		::DeferWindowPos(hdwp, m_FontSelDlg, NULL, 0, ptBase.y+rtRichRecv.Height()-CHATDLG_TOOLBAR_HEIGHT, 0, CHATDLG_TOOLBAR_HEIGHT, SWP_NOSIZE|SWP_NOZORDER);
	}
	else
		::DeferWindowPos(hdwp, m_richRecv, NULL, 0, 0, rtRichRecv.Width(), nMouseY-ptBase.y-CHATDLG_TOOLBAR_HEIGHT, SWP_NOMOVE|SWP_NOZORDER);
	
	//MidToolBar
	::GetClientRect(m_SplitterCtrl, &rtSplitter);
	//AtlTrace(_T("ptBase.y+rtRichRecv.Height()+rtSplitter.Height(): %d\n"), ptBase.y+rtRichRecv.Height()+rtSplitter.Height());
	::DeferWindowPos(hdwp, m_tbMid, NULL, 3, ptBase.y+rtRichRecv.Height(), 0, 0, SWP_NOSIZE|SWP_NOZORDER);
	//分割线
	::GetClientRect(m_richRecv, &rtRichRecv);
	//AtlTrace(_T("ptBase.y+rtRichRecv.Height(): %d\n"), ptBase.y+rtRichRecv.Height());
	::DeferWindowPos(hdwp, m_SplitterCtrl, NULL, 6, ptBase.y+rtRichRecv.Height()+rtMidToolbar.Height(), 0, 0, SWP_NOSIZE|SWP_NOZORDER);
	//发送框
	long nHeightRichSend = rtClient.Height()-100-44-(rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height());
	::GetClientRect(m_tbMid, &rtMidToolbar);
	//AtlTrace(_T("RichSend top: %d\n"), ptBase.y+rtRichRecv.bottom-rtRichRecv.top+rtSplitter.top-rtSplitter.bottom+rtMidToolbar.bottom-rtMidToolbar.top);
	if(m_bMsgLogWindowVisible)
		::DeferWindowPos(hdwp, m_richSend, NULL, 6, ptBase.y+rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height(), rtClient.Width()-8-GROUP_MEMBER_LIST_WIDTH-GROUP_MSG_LOG_WIDTH, nHeightRichSend, SWP_NOZORDER);
	else
		::DeferWindowPos(hdwp, m_richSend, NULL, 6, ptBase.y+rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height(), rtClient.Width()-5-GROUP_MEMBER_LIST_WIDTH, nHeightRichSend, SWP_NOZORDER);
	::EndDeferWindowPos(hdwp);

	ResizeImageInRecvRichEdit();

	//记录下最新的接受文本框、midTooBar和发送文本框的最新位置
	m_bDraged = TRUE;
	::GetWindowRect(m_richRecv, &m_rtRichRecv);
	::ScreenToClient(m_hWnd, m_rtRichRecv);

	::GetWindowRect(m_tbMid, &m_rtMidToolBar);
	::ScreenToClient(m_hWnd, m_rtMidToolBar);

	::GetWindowRect(m_SplitterCtrl, &m_rtSplitter);
	::ScreenToClient(m_hWnd, m_rtSplitter);

	::GetWindowRect(m_richSend, &m_rtRichSend);
	::ScreenToClient(m_hWnd, m_rtRichSend);
}