#include "stdafx.h"
#include "SessChatDlg.h"
#include "Time.h"

const long SESSIONCHATDLG_WIDTH   = 587;
const long SESSIONCHATDLG_HEIGHT  = 535;

CSessChatDlg::CSessChatDlg(void)
{
	m_lpFMGClient = NULL;
	m_lpFaceList = NULL;
	m_lpCascadeWinManager = NULL;
	m_hMainDlg = NULL;
	m_nGroupCode = 0;
	m_nUTalkUin = 0;

	m_hDlgIcon = m_hDlgSmallIcon = NULL;
	m_fontBuddyNameLink = NULL;
	m_hRBtnDownWnd = NULL;
	memset(&m_ptRBtnDown, 0, sizeof(m_ptRBtnDown));
	m_pLastImageOle = NULL;
	m_cxPicBarDlg = 122;
	m_cyPicBarDlg = 24;

	m_nGroupId = 0;
	m_nUTalkNumber = 0;
	m_nUserNumber = 0;
	m_strBuddyName = _T("");
	m_strGroupName = _T("");
	m_strUserName = _T("");

	m_dwThreadId = 0;
}

CSessChatDlg::~CSessChatDlg(void)
{
}

BOOL CSessChatDlg::PreTranslateMessage(MSG* pMsg)
{
	if (::GetForegroundWindow() == m_hWnd && !m_Accelerator.IsNull() && 
		m_Accelerator.TranslateAccelerator(m_hWnd, pMsg))
		return TRUE;

	if (pMsg->hwnd == m_richRecv.m_hWnd || pMsg->hwnd == m_richSend.m_hWnd)
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
			&& (pMsg->wParam == 'V') && (pMsg->lParam & VK_CONTROL))	// 发送文本框的Ctrl+V消息
		{
			m_richSend.PasteSpecial(CF_TEXT);
			return TRUE;
		}
	}

	return CWindow::IsDialogMessage(pMsg);
}

void CSessChatDlg::OnRecvMsg(UINT nUTalkUin, UINT nMsgId)
{
	if (NULL == m_lpFMGClient || m_nUTalkUin != nUTalkUin)
		return;

	if (::GetForegroundWindow() != m_hWnd)
		FlashWindowEx(m_hWnd, 3);

	if (nMsgId == 0)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_SESS, nUTalkUin);
			if (lpMsgSender != NULL)
			{
				int nMsgCnt = lpMsgSender->GetMsgCount();
				for (int i = 0; i < nMsgCnt; i++)
				{
					CSessMessage* lpSessMsg = lpMsgSender->GetSessMsg(i);
					if (lpSessMsg != NULL)
					{
						AddMsgToRecvEdit(lpSessMsg);
					}
				}
				lpMsgList->DelMsgSender(FMG_MSG_TYPE_SESS, nUTalkUin);
				::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_SESS, nUTalkUin);
			}
		}
	}
	else
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_SESS, nUTalkUin);
			if (lpMsgSender != NULL)
			{
				CSessMessage* lpSessMsg = lpMsgSender->GetSessMsgById(nMsgId);
				if (lpSessMsg != NULL)
				{
					AddMsgToRecvEdit(lpSessMsg);
					lpMsgSender->DelMsgById(nMsgId);
				}

				if (lpMsgSender->GetMsgCount() <= 0)
					lpMsgList->DelMsgSender(FMG_MSG_TYPE_SESS, nUTalkUin);
			}
		}
	}
}

// 更新群成员号码
void CSessChatDlg::OnUpdateGMemberNumber()
{
	UpdateData();					// 更新信息
	UpdateBuddyNameCtrl();			// 更新好友名称控件

	if (m_lpFMGClient->IsNeedUpdateSessHeadPic(m_nUTalkNumber))	// 更新头像
		m_lpFMGClient->UpdateGroupMemberHeadPic(m_nGroupCode, m_nUTalkUin, m_nUTalkNumber);
}

// 更新群成员头像
void CSessChatDlg::OnUpdateGMemberHeadPic()
{
	tstring strFileName;
	if (m_lpFMGClient != NULL && m_nUTalkNumber != 0)
		strFileName = m_lpFMGClient->GetSessHeadPicFullName(m_nUTalkNumber);
	if (!Hootina::CPath::IsFileExist(strFileName.c_str()))
		strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefBuddyHeadPic.png");
	m_picHead.SetBitmap(strFileName.c_str());
	m_picHead.Invalidate();
}

BOOL CSessChatDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	m_lpCascadeWinManager->Add(m_hWnd, SESSIONCHATDLG_WIDTH, SESSIONCHATDLG_HEIGHT);

	// set icons
	m_hDlgIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_32, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(m_hDlgIcon, TRUE);
	m_hDlgSmallIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(m_hDlgSmallIcon, FALSE);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);

	CBuddyInfo* lpBuddyInfo = GetBuddyInfoPtr();
	if (lpBuddyInfo != NULL)
	{
		if (m_lpFMGClient->IsNeedUpdateSessHeadPic(lpBuddyInfo->m_uUserID))
			m_lpFMGClient->UpdateGroupMemberHeadPic(m_nGroupCode, lpBuddyInfo->m_uUserID, lpBuddyInfo->m_uUserID);
		
	}

	Init();		// 初始化

	m_FontSelDlg.m_pFMGClient = m_lpFMGClient;
	//CalcTitleBarRect();
	PostMessage(WM_SETDLGINITFOCUS, 0, 0);		// 设置对话框初始焦点
	SetTimer(1001, 300, NULL);

	return TRUE;
}

BOOL CSessChatDlg::OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct)
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
			m_tbMid.SetItemCheckState(13, FALSE);
			m_tbMid.Invalidate();
		}
		break;
	}

	return TRUE;
}

void CSessChatDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	m_SkinMenu.OnMeasureItem(nIDCtl, lpMeasureItemStruct);
}

void CSessChatDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	m_SkinMenu.OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CSessChatDlg::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	lpMMI->ptMinTrackSize.x = 540;
	lpMMI->ptMinTrackSize.y = 510;
}

void CSessChatDlg::OnMove(CPoint ptPos)
{
	SetMsgHandled(FALSE);

	m_lpCascadeWinManager->SetPos(m_hWnd, ptPos.x, ptPos.y);
}

void CSessChatDlg::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	CRect rcClient;
	GetClientRect(&rcClient);

	if (m_staGroupName.IsWindow())
		m_staGroupName.MoveWindow(44, 22, rcClient.right-2, 14);

	//if (m_picAD_1.IsWindow())
	//	m_picAD_1.MoveWindow(rcClient.right-144, rcClient.top+43, 142, 42);

	//if (m_picAD_2.IsWindow())
	//	m_picAD_2.MoveWindow(rcClient.right-143, rcClient.top+85, 140, 224);

	//if (m_picAD_3.IsWindow())
	//	m_picAD_3.MoveWindow(rcClient.right-143, rcClient.bottom-145, 140, 141);

	if (m_btnClose.IsWindow())
		m_btnClose.MoveWindow(rcClient.right-310, rcClient.bottom-5-22, 69, 22);

	if (m_btnSend.IsWindow())
		m_btnSend.MoveWindow(rcClient.right-236, rcClient.bottom-5-22, 64, 22);

	if (m_btnArrow.IsWindow())
		m_btnArrow.MoveWindow(rcClient.right-172, rcClient.bottom-5-22, 19, 22);

	//if (m_tbTop.IsWindow())
	//	m_tbTop.MoveWindow(3, 45, 386, 44);

	if (m_tbMid.IsWindow())
		m_tbMid.MoveWindow(1, rcClient.bottom-155, rcClient.Width()-1-152, 27);

	if (m_richRecv.IsWindow())
	{
		if (!::IsWindowVisible(m_FontSelDlg.m_hWnd))
			m_richRecv.MoveWindow(6, rcClient.top+44+44, rcClient.Width()-6-152, rcClient.Height()-243);
		else
			m_richRecv.MoveWindow(6, rcClient.top+44+44, rcClient.Width()-6-152, rcClient.Height()-243-32);
	}

	if (m_richSend.IsWindow())
		m_richSend.MoveWindow(6, rcClient.bottom-128, rcClient.Width()-6-152, 95);

	if (::IsWindowVisible(m_FontSelDlg.m_hWnd))
		m_FontSelDlg.MoveWindow(1, rcClient.bottom-187, rcClient.Width()-1-152, 32);
}

void CSessChatDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (1001 == nIDEvent)
	{
		OnRecvMsg(m_nUTalkUin, NULL);	// 显示消息
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

void CSessChatDlg::OnClose()
{
	//::PostMessage(m_hMainDlg, WM_CLOSE_SESSCHATDLG, m_nGroupCode, m_nUTalkUin);
	DestroyWindow();
}

void CSessChatDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	CloseMsgLogBrowser();
	::PostMessage(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, WM_CLOSE_SESSCHATDLG, 0, (LPARAM)m_nUTalkUin);

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

	if (m_fontBuddyNameLink != NULL)
	{
		BOOL bRet = ::DeleteObject(m_fontBuddyNameLink);
		m_fontBuddyNameLink = NULL;
	}

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}

// “好友名称”超链接控件
void CSessChatDlg::OnLnk_BuddyName(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	::PostMessage(m_hMainDlg, WM_SHOW_GMEMBERINFODLG, m_nGroupCode, m_nUTalkUin);
}

// “字体选择工具栏”按钮
void CSessChatDlg::OnBtn_Font(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (BN_PUSHED == uNotifyCode)
	{
		if (!m_FontSelDlg.IsWindow())
			m_FontSelDlg.Create(m_hWnd);

		CRect rcClient;
		GetClientRect(&rcClient);
		m_richRecv.MoveWindow(6, rcClient.top+44+44, rcClient.Width()-6-152, rcClient.Height()-243-32);
		m_FontSelDlg.MoveWindow(1, rcClient.bottom-187, rcClient.Width()-1-152, 32);
		m_FontSelDlg.ShowWindow(SW_SHOW);
	}
	else if (BN_UNPUSHED == uNotifyCode)
	{
		m_FontSelDlg.ShowWindow(SW_HIDE);
		CRect rcClient;
		GetClientRect(&rcClient);
		m_richRecv.MoveWindow(6, rcClient.top+44+44, rcClient.Width()-6-152, rcClient.Height()-243);
	}
}

// “表情”按钮
void CSessChatDlg::OnBtn_Face(UINT uNotifyCode, int nID, CWindow wndCtl)
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
			int cy = 236;
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
void CSessChatDlg::OnBtn_Image(UINT uNotifyCode, int nID, CWindow wndCtl)
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
		_RichEdit_InsertFace(m_richSend.m_hWnd, fileDlg.m_ofn.lpstrFile, -1, -1);
		m_richSend.SetFocus();
	}
}

// “消息记录”按钮
void CSessChatDlg::OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (uNotifyCode == BN_CLICKED)
	{
		BOOL bChecked = m_tbMid.GetItemCheckState(13);
		if (!bChecked)
		{
			CloseMsgLogBrowser();
			return;
		}

		OpenMsgLogBrowser();
	}
}

// “点击另存为”按钮
void CSessChatDlg::OnBtn_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	OnMenu_SaveAs(uNotifyCode, nID, wndCtl);
}

// “关闭”按钮
void CSessChatDlg::OnBtn_Close(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	PostMessage(WM_CLOSE);
}

// “发送”按钮
void CSessChatDlg::OnBtn_Send(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (NULL == m_lpFMGClient)
		return;

	int nCustomPicCnt = RichEdit_GetCustomPicCount(m_richSend.m_hWnd);
	if (nCustomPicCnt > 0)
	{
		MessageBox(_T("目前暂不支持向群成员发送图片！"), _T("提示"));
		return;
	}

	tstring strText;
	RichEdit_GetText(m_richSend.m_hWnd, strText);

	if (strText.size() <= 0)
		return;

	time_t nTime = m_lpFMGClient->GetCurrentTime();

	AddMsgToRecvEdit(nTime, strText.c_str());

	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();

	TCHAR szColor[32] = {0};
	RGBToHexStr(fontInfo.m_clrText, szColor, sizeof(szColor)/sizeof(TCHAR));

	TCHAR szFontInfo[1024] = {0};
	LPCTSTR lpFontFmt = _T("/o[\"%s,%d,%s,%d,%d,%d\"]");
	wsprintf(szFontInfo, lpFontFmt, fontInfo.m_strName.c_str(), fontInfo.m_nSize, 
		szColor, fontInfo.m_bBold, fontInfo.m_bItalic, fontInfo.m_bUnderLine);

	strText += szFontInfo;

	m_lpFMGClient->SendSessMsg(m_nGroupId, m_nUTalkUin, nTime, strText.c_str());

	m_richSend.SetWindowText(_T(""));
	m_richSend.SetFocus();
}

// “箭头”按钮
void CSessChatDlg::OnBtn_Arrow(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(8);
	if (PopupMenu.IsMenu())
	{
		CRect rc;
		m_btnArrow.GetClientRect(&rc);
		m_btnArrow.ClientToScreen(&rc);
		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rc.left, rc.bottom + 4, m_hWnd, &rc);
	}
}

LRESULT CSessChatDlg::OnToolbarDropDown(LPNMHDR pnmh)
{
	NMTOOLBAR* pnmtb = (NMTOOLBAR*)pnmh;
	CSkinMenu PopupMenu;
	int nIndex = -1;
	CRect rc(pnmtb->rcButton);

	switch (pnmtb->iItem)
	{
	case 101:
		nIndex = 0;
		m_tbTop.ClientToScreen(&rc);
		break;

	case 102:
		nIndex = 1;
		m_tbTop.ClientToScreen(&rc);
		break;

	case 103:
		nIndex = 2;
		m_tbTop.ClientToScreen(&rc);
		break;

	case 106:
		nIndex = 3;
		m_tbTop.ClientToScreen(&rc);
		break;

	case 107:
		nIndex = 4;
		m_tbTop.ClientToScreen(&rc);
		break;

	case 211:
		nIndex = 5;
		m_tbMid.ClientToScreen(&rc);
		break;

	case 212:
		nIndex = 6;
		m_tbMid.ClientToScreen(&rc);
		break;

	case 214:
		nIndex = 7;
		m_tbMid.ClientToScreen(&rc);
		break;

	default:
		return 0;
	}

	PopupMenu = m_SkinMenu.GetSubMenu(nIndex);
	if (PopupMenu.IsMenu())
	{
		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rc.left, rc.bottom + 4, m_hWnd, &rc);
	}

	return 0;
}

// 更新字体信息
LRESULT CSessChatDlg::OnUpdateFontInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();
	RichEdit_SetDefFont(m_richSend.m_hWnd, fontInfo.m_strName.c_str(),
		fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold,
		fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE);
	return 0;
}

// “表情”控件选取消息
LRESULT CSessChatDlg::OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam)
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
LRESULT CSessChatDlg::OnSetDlgInitFocus(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_richSend.SetFocus();
	return 0;
}

//	“接收消息”富文本框链接点击消息
LRESULT CSessChatDlg::OnRichEdit_Recv_Link(LPNMHDR pnmh)
{
	CString strUrl;

	if (pnmh->code == EN_LINK)
	{
		ENLINK*pLink = (ENLINK*)pnmh;
		if (pLink->msg == WM_LBUTTONUP)
		{
			m_richRecv.SetSel(pLink->chrg);
			m_richRecv.GetSelText(strUrl);

			if (strUrl.Left(7).MakeLower() == _T("http://"))
			{
				::ShellExecute(NULL, _T("open"), strUrl, NULL, NULL, SW_SHOWNORMAL);
			}
		}
	}
	return 0;
}

LRESULT CSessChatDlg::OnRichEdit_Send_Paste(LPNMHDR pnmh)
{
	NMRICHEDITOLECALLBACK* lpOleNotify = (NMRICHEDITOLECALLBACK*)pnmh;
	if (lpOleNotify != NULL && lpOleNotify->hdr.code == EN_PASTE
		&& lpOleNotify->hdr.hwndFrom == m_richSend.m_hWnd)
	{
		AddMsgToSendEdit(lpOleNotify->lpszText);
	}
	return 0;
}

// “剪切”菜单
void CSessChatDlg::OnMenu_Cut(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.Cut();
}

// “复制”菜单
void CSessChatDlg::OnMenu_Copy(UINT uNotifyCode, int nID, CWindow wndCtl)
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
}

// “粘贴”菜单
void CSessChatDlg::OnMenu_Paste(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.PasteSpecial(CF_TEXT);
}

// “全部选择”菜单
void CSessChatDlg::OnMenu_SelAll(UINT uNotifyCode, int nID, CWindow wndCtl)
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
void CSessChatDlg::OnMenu_Clear(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richRecv.SetWindowText(_T(""));
}

// “显示比例”菜单
void CSessChatDlg::OnMenu_ZoomRatio(UINT uNotifyCode, int nID, CWindow wndCtl)
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

	CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(10);
	for (int i = ID_MENU_ZOOMRATIO_400; i <= ID_MENU_ZOOMRATIO_50; i++)
	{
		if (i != nID)
			menuPopup.CheckMenuItem(i, MF_BYCOMMAND|MF_UNCHECKED);
		else
			menuPopup.CheckMenuItem(i, MF_BYCOMMAND|MF_CHECKED);
	}	
}

// “另存为”菜单
void CSessChatDlg::OnMenu_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
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
void CSessChatDlg::CalcTitleBarRect()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	m_rcTitleBar = CRect(0, 0, rcClient.Width(), 27);

	if (m_bHasCloseBtn)
	{
		m_rcCloseBtn = CRect(rcClient.Width() - 39, 0, rcClient.Width(), 20);

		if (m_bHasMaxBtn)
		{
			m_rcMaxBtn = CRect(rcClient.Width() - 39 - 28, 0, rcClient.Width() - 39, 20);
		}
	}

	if (m_bHasMinBtn)
	{
		if (!m_bHasMaxBtn)
		{
			m_rcMinBtn = CRect(rcClient.Width() - 39 - 28, 0, rcClient.Width() - 39, 20);
		}
		else
		{
			m_rcMinBtn = CRect(rcClient.Width() - 39 - 28 - 28, 0, rcClient.Width() - 39 - 28, 20);
		}
	}
}
// 发送/接收文本框的鼠标移动消息
BOOL CSessChatDlg::OnRichEdit_MouseMove(MSG* pMsg)
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
BOOL CSessChatDlg::OnRichEdit_LBtnDblClk(MSG* pMsg)
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
BOOL CSessChatDlg::OnRichEdit_RBtnDown(MSG* pMsg)
{
	if (pMsg->hwnd == m_richSend.m_hWnd)
	{
		m_hRBtnDownWnd = pMsg->hwnd;
		m_ptRBtnDown.x = GET_X_LPARAM(pMsg->lParam);
		m_ptRBtnDown.y = GET_Y_LPARAM(pMsg->lParam);

		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(9);

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

		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(10);

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
	return FALSE;
}

// 获取群信息指针
CGroupInfo* CSessChatDlg::GetGroupInfoPtr()
{
	if (m_lpFMGClient != NULL)
	{
		CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
		if (lpGroupList != NULL)
			return lpGroupList->GetGroupByCode(m_nGroupCode);
	}
	return NULL;
}

// 获取好友信息指针
CBuddyInfo* CSessChatDlg::GetBuddyInfoPtr()
{
	if (m_lpFMGClient != NULL)
	{
		CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
		if (lpGroupInfo != NULL)
			return lpGroupInfo->GetMemberByUin(m_nUTalkUin);
	}
	return NULL;
}

// 获取用户信息指针
CBuddyInfo* CSessChatDlg::GetUserInfoPtr()
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

// 更新数据
void CSessChatDlg::UpdateData()
{
	CBuddyInfo* lpBuddyInfo = GetBuddyInfoPtr();
	if (lpBuddyInfo != NULL)
	{
		m_nUTalkNumber = lpBuddyInfo->m_uUserID;
		if (!lpBuddyInfo->m_strMarkName.empty())
			m_strBuddyName = lpBuddyInfo->m_strMarkName.c_str();
		else
			m_strBuddyName = lpBuddyInfo->m_strNickName.c_str();
	}

	CGroupInfo* lpGroupInfo = GetGroupInfoPtr();
	if (lpGroupInfo != NULL)
	{
		m_nGroupId = lpGroupInfo->m_nGroupId;
		m_strGroupName = lpGroupInfo->m_strName.c_str();
	}
	
	CBuddyInfo* lpUserInfo = GetUserInfoPtr();
	if (lpUserInfo != NULL)
	{
		m_nUserNumber = lpUserInfo->m_uUserID;
		m_strUserName = lpUserInfo->m_strNickName.c_str();
	}
}

// 更新对话框标题栏
void CSessChatDlg::UpdateDlgTitle()
{
	SetWindowText(m_strBuddyName);
}

// 更新好友名称控件
void CSessChatDlg::UpdateBuddyNameCtrl()
{
	CString strText;
	if (m_nUTalkNumber != 0)
		strText.Format(_T("%s(%u)"), m_strBuddyName, m_nUTalkNumber);
	else
		strText.Format(_T("%s"), m_strBuddyName);
	m_lnkBuddyName.SetLabel(strText);
}

// 更新群名称控件
void CSessChatDlg::UpdateGroupNameCtrl()
{
	CString strText;
	strText.Format(_T("临时会话-来自群:%s"), m_strGroupName);
	m_staGroupName.SetWindowText(strText);
}

// 初始化Top工具栏
BOOL CSessChatDlg::InitTopToolBar()
{
	int nIndex = m_tbTop.AddItem(101, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("开始视频会话"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\video.png"));

	nIndex = m_tbTop.AddItem(102, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("开始语音会话"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\audio.png"));

	nIndex = m_tbTop.AddItem(103, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("传送文件"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\sendfile.png"));

	nIndex = m_tbTop.AddItem(104, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("发送短信"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\sendsms.png"));

	nIndex = m_tbTop.AddItem(105, STBI_STYLE_BUTTON);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("创建讨论组"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\create_disc_group.png"));

	nIndex = m_tbTop.AddItem(106, STBI_STYLE_WHOLEDROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("举报"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\report.png"));

	nIndex = m_tbTop.AddItem(107, STBI_STYLE_WHOLEDROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 2);
	m_tbTop.SetItemToolTipText(nIndex, _T("应用"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\app.png"));

	//nIndex = m_tbTop.AddItem(108, STBI_STYLE_BUTTON);
	//m_tbTop.SetItemSize(nIndex, 36, 40);
	//m_tbTop.SetItemPadding(nIndex, 2);
	//m_tbTop.SetItemToolTipText(nIndex, _T("远程协助"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\remote_assistance.png"));

	m_tbTop.SetLeftTop(2, 2);
	m_tbTop.SetTransparent(TRUE, m_SkinDlg.GetBgDC());

	CRect rcTopToolBar(3, 70, SESSIONCHATDLG_WIDTH-3, 102);
	m_tbTop.Create(m_hWnd, rcTopToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_TOP);

	return TRUE;
}

// 初始化Middle工具栏
BOOL CSessChatDlg::InitMidToolBar()
{
	int nIndex = m_tbMid.AddItem(201, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 24, 20);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("字体选择工具栏"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_font.png"));

	nIndex = m_tbMid.AddItem(202, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 24, 20);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("选择表情"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_face.png"));

	//nIndex = m_tbMid.AddItem(203, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("会员魔法表情/超级表情/涂鸦表情/宠物炫"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_richface.png"));

	nIndex = m_tbMid.AddItem(204, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 24, 20);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("向好友发送窗口抖动"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_twitter.png"));

	//nIndex = m_tbMid.AddItem(205, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("选择动一下表情"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_flirtationface.png"));

	//nIndex = m_tbMid.AddItem(206, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 2);
	//m_tbMid.SetItemToolTipText(nIndex, _T("多功能辅助输入"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\UTalkIme.png"));

	//nIndex = m_tbMid.AddItem(207, STBI_STYLE_SEPARTOR);
	//m_tbMid.SetItemSize(nIndex, 2, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemSepartorPic(nIndex, _T("aio_qzonecutline_normal.png"));

	nIndex = m_tbMid.AddItem(208, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 24, 20);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("发送图片"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_sendpic.png"));

	//nIndex = m_tbMid.AddItem(209, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("点歌"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\musicsharebtn20.png"));

	//nIndex = m_tbMid.AddItem(210, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("给好友送份礼物，送份惊喜"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\present.png"));

	nIndex = m_tbMid.AddItem(211, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 31, 20, 23, 8);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("屏幕截图"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(1,0,0,0));
	m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_cut.png"));

	//nIndex = m_tbMid.AddItem(212, STBI_STYLE_DROPDOWN);
	//m_tbMid.SetItemSize(nIndex, 31, 20, 23, 8);
	//m_tbMid.SetItemPadding(nIndex, 2);
	//m_tbMid.SetItemToolTipText(nIndex, _T("划词搜索"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
	//	_T("aio_toolbar_leftdown.png"), CRect(1,0,0,0));
	//m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	//m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\SoSo.png"));

	//nIndex = m_tbMid.AddItem(213, STBI_STYLE_SEPARTOR);
	//m_tbMid.SetItemSize(nIndex, 2, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemSepartorPic(nIndex, _T("aio_qzonecutline_normal.png"));

	nIndex = m_tbMid.AddItem(214, STBI_STYLE_DROPDOWN|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 82, 20, 74, 8);
	m_tbMid.SetItemPadding(nIndex, 2);
	m_tbMid.SetItemText(nIndex, _T("消息记录"));
	m_tbMid.SetItemToolTipText(nIndex, _T("显示消息记录"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
		_T("aio_toolbar_leftdown.png"), CRect(1,0,0,0));
	m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
		_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_register.png"));

	m_tbMid.SetLeftTop(2, 4);
	m_tbMid.SetBgPic(_T("MidToolBar\\bg.png"), CRect(0,0,0,0));

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcMidToolBar(1, rcClient.bottom-155, 1+(rcClient.Width()-1-152), (rcClient.bottom-155)+27);
	m_tbMid.Create(m_hWnd, rcMidToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_MID);

	return TRUE;
}

// 初始化IRichEditOleCallback接口
BOOL CSessChatDlg::InitRichEditOleCallback()
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

	return SUCCEEDED(hr);
}

// 初始化控件
BOOL CSessChatDlg::Init()
{
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetBgPic(_T("BuddyChatDlgBg.png"), CRect(4, 100, 4, 32));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), _T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetMaxSysBtnPic(_T("SysBtn\\btn_max_normal.png"), _T("SysBtn\\btn_max_highlight.png"), _T("SysBtn\\btn_max_down.png"));
	m_SkinDlg.SetRestoreSysBtnPic(_T("SysBtn\\btn_restore_normal.png"), _T("SysBtn\\btn_restore_highlight.png"), _T("SysBtn\\btn_restore_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	CRect rcClient;
	GetClientRect(&rcClient);

	m_picHead.SubclassWindow(GetDlgItem(ID_PIC_HEAD));
	m_picHead.SetTransparent(TRUE, hDlgBgDC);
	m_picHead.SetShowCursor(TRUE);
	m_picHead.SetBgPic(_T("HeadCtrl\\Padding4Normal.png"), _T("HeadCtrl\\Padding4Hot.png"), _T("HeadCtrl\\Padding4Hot.png"));
	m_picHead.MoveWindow(10, 10, 54, 54, FALSE);

	tstring strFileName;
	if (m_lpFMGClient != NULL && m_nUTalkNumber != 0)
		strFileName = m_lpFMGClient->GetSessHeadPicFullName(m_nUTalkNumber);
	if (!Hootina::CPath::IsFileExist(strFileName.c_str()))
		strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefBuddyHeadPic.png");
	m_picHead.SetBitmap(strFileName.c_str());

	m_lnkBuddyName.SubclassWindow(GetDlgItem(ID_LINK_BUDDYNAME));
	m_lnkBuddyName.MoveWindow(70, 12, 60, 14, FALSE);
	m_lnkBuddyName.SetTransparent(TRUE, hDlgBgDC);
	m_lnkBuddyName.SetLinkColor(RGB(0,0,0));
	m_lnkBuddyName.SetHoverLinkColor(RGB(0,0,0));
	m_lnkBuddyName.SetVisitedLinkColor(RGB(0,0,0));

	CFontHandle font = (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0L);
	LOGFONT lf = {0};
	font.GetLogFont(&lf);
	lf.lfHeight = 13;
	lf.lfWeight = FW_BOLD;
	m_fontBuddyNameLink = ::CreateFontIndirect(&lf); 
	m_lnkBuddyName.SetNormalFont(m_fontBuddyNameLink);

	m_staGroupName.SubclassWindow(GetDlgItem(ID_STATIC_BUDDYSIGN));
	m_staGroupName.MoveWindow(70, 38, SESSIONCHATDLG_WIDTH-50, 20, FALSE);
	m_staGroupName.SetTransparent(TRUE, hDlgBgDC);

	m_btnClose.SubclassWindow(GetDlgItem(ID_BTN_CLOSE));
	m_btnClose.SetTransparent(TRUE, hDlgBgDC);
	m_btnClose.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnClose.SetBgPic(_T("Button\\btn_close_normal.png"), _T("Button\\btn_close_highlight.png"),_T("Button\\btn_close_down.png"), _T("Button\\btn_close_focus.png"));
	m_btnClose.MoveWindow(SESSIONCHATDLG_WIDTH-190, SESSIONCHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnSend.SubclassWindow(GetDlgItem(ID_BTN_SEND));
	m_btnSend.SetTransparent(TRUE, hDlgBgDC);
	m_btnSend.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnSend.SetTextColor(RGB(255, 255, 255));
	m_btnSend.SetBgPic(_T("Button\\btn_send_normal.png"), _T("Button\\btn_send_highlight.png"),_T("Button\\btn_send_down.png"), _T("Button\\btn_send_focus.png"));
	m_btnSend.MoveWindow(SESSIONCHATDLG_WIDTH-110, SESSIONCHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnArrow.SubclassWindow(GetDlgItem(ID_BTN_ARROW));
	m_btnArrow.SetTransparent(TRUE, hDlgBgDC);
	m_btnArrow.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnArrow.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnArrow.MoveWindow(SESSIONCHATDLG_WIDTH-33, SESSIONCHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_SkinMenu.LoadMenu(ID_MENU_BUDDYCHAT);
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

	CRect rcRecv(6, 107, 583, 366);
	DWORD dwStyle = WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
	m_richRecv.Create(m_hWnd, rcRecv, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_RECV);
	m_richRecv.SetTransparent(TRUE, hDlgBgDC);	
	DWORD dwMask = m_richRecv.GetEventMask();
	dwMask = dwMask | ENM_LINK  | ENM_MOUSEEVENTS | ENM_SCROLLEVENTS | ENM_KEYEVENTS;
	m_richRecv.SetEventMask(dwMask);
	m_richRecv.SetAutoURLDetect();
	m_richRecv.SetReadOnly();

	CRect rcSend(6, 402, 583, 502);
	m_richSend.Create(m_hWnd, rcSend, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_SEND);
	m_richSend.SetTransparent(TRUE, hDlgBgDC);

	UpdateData();
	UpdateDlgTitle();
	UpdateBuddyNameCtrl();
	UpdateGroupNameCtrl();		// 更新群名称控件

	m_Accelerator.LoadAccelerators(ID_ACCE_CHATDLG);

	InitRichEditOleCallback();	// 初始化IRichEditOleCallback接口

	return TRUE;
}

// 反初始化控件
BOOL CSessChatDlg::UnInit()
{
	if (m_PicBarDlg.IsWindow())
		m_PicBarDlg.DestroyWindow();

	if (m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	if (m_lnkBuddyName.IsWindow())
		m_lnkBuddyName.DestroyWindow();

	if (m_staGroupName.IsWindow())
		m_staGroupName.DestroyWindow();

	//if (m_picAD_1.IsWindow())
	//	m_picAD_1.DestroyWindow();

	//if (m_picAD_2.IsWindow())
	//	m_picAD_2.DestroyWindow();

	//if (m_picAD_3.IsWindow())
	//	m_picAD_3.DestroyWindow();

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

	m_Accelerator.DestroyObject();
	//	m_menuRichEdit.DestroyMenu();

	return TRUE;
}

void CSessChatDlg::_RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText)
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

BOOL CSessChatDlg::_RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId, int nFaceIndex)
{
	ITextServices* pTextServices;
	ITextHost* pTextHost;
	BOOL bRet;

	if (hWnd == m_richRecv.m_hWnd)
	{
		pTextServices = m_richRecv.GetTextServices();
		pTextHost = m_richRecv.GetTextHost();

		long lStartChar = 0, lEndChar = 0;
		RichEdit_GetSel(hWnd, lStartChar, lEndChar);
		bRet = RichEdit_InsertFace(pTextServices, pTextHost, 
			lpszFileName, nFaceId, nFaceIndex, RGB(255,255,255), TRUE, 40);
		if (bRet)
		{
			lEndChar = lStartChar + 1;
			RichEdit_SetSel(hWnd, lStartChar, lEndChar);
			RichEdit_SetStartIndent(hWnd, 300);
			RichEdit_SetSel(hWnd, lEndChar, lEndChar);
		}
	}
	else
	{
		pTextServices = m_richSend.GetTextServices();
		pTextHost = m_richSend.GetTextHost();

		bRet = RichEdit_InsertFace(pTextServices, pTextHost, 
			lpszFileName, nFaceId, nFaceIndex, RGB(255,255,255), TRUE, 40);
	}

	if (pTextServices != NULL)
		pTextServices->Release();
	if (pTextHost != NULL)
		pTextHost->Release();

	return bRet;
}

BOOL CSessChatDlg::HandleSysFaceId(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

BOOL CSessChatDlg::HandleSysFaceIndex(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

BOOL CSessChatDlg::HandleCustomPic(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

// "/f["系统表情id"]/s["系统表情index"]/c["自定义图片路径"]"
void CSessChatDlg::AddMsg(HWND hRichEditWnd, LPCTSTR lpText)
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
		_RichEdit_ReplaceSel(hRichEditWnd, strText);
}

void CSessChatDlg::AddMsgToSendEdit(LPCTSTR lpText)
{
	AddMsg(m_richSend.m_hWnd, lpText);
	m_richSend.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CSessChatDlg::AddMsgToRecvEdit(time_t nTime, LPCTSTR lpText)
{
	if (NULL == lpText || NULL ==*lpText)
		return;

	TCHAR cTime[32] = {0};
	FormatTime(nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	CString strText;
	strText.Format(_T("%s  %s\r\n"), m_strUserName, cTime);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
		_T("宋体"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
	strText = _T("");

	AddMsg(m_richRecv.m_hWnd, lpText);

	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CSessChatDlg::AddMsgToRecvEdit(CSessMessage* lpSessMsg)
{
	if (NULL == lpSessMsg || NULL == m_lpFMGClient)
		return;

	TCHAR cTime[32] = {0};
	if (IsToday(lpSessMsg->m_nTime))
		FormatTime(lpSessMsg->m_nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));
	else
		FormatTime(lpSessMsg->m_nTime, _T("%Y-%m-%d %H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	CString strText;
	strText.Format(_T("%s  %s\r\n"), m_strBuddyName, cTime);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
		_T("宋体"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);

	CFontInfo fontInfo;
	for (int i = 0; i < (int)lpSessMsg->m_arrContent.size(); i++)
	{
		CContent* lpContent = lpSessMsg->m_arrContent[i];
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
			}
		}
	}

	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

// 打开消息记录浏览窗口
void CSessChatDlg::OpenMsgLogBrowser()
{
	CString strExeName = Hootina::CPath::GetAppPath().c_str();
	strExeName += _T("MsgLogBrowser.exe");

	if (!Hootina::CPath::IsFileExist(strExeName))
		return;

	CString strMsgFile = m_lpFMGClient->GetMsgLogFullName().c_str();
	strMsgFile.Replace(_T("\\"), _T("/"));

	CString strChatPicDir = m_lpFMGClient->GetChatPicFolder().c_str();
	strChatPicDir.Replace(_T("\\"), _T("/"));

	CString strCmdLine;
	strCmdLine.Format(_T("\"%s\" %u %u %u %u %u \"%s\" \"%s\""), 
		strExeName, m_hWnd, 0, 0, m_nUTalkNumber, m_nUserNumber, strMsgFile, strChatPicDir);
	LPTSTR lpszCmdLine = _tcsdup(strCmdLine.GetBuffer());
	if (NULL == lpszCmdLine)
		return;

	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};

	si.cb = sizeof(si);

	BOOL bRet = ::CreateProcess(NULL, lpszCmdLine, NULL,
		NULL, FALSE, 0, NULL, NULL, &si, &pi);
	free(lpszCmdLine);
	if (!bRet)
		return;

	m_dwThreadId = pi.dwThreadId;

	::CloseHandle(pi.hProcess);
	::CloseHandle(pi.hThread);
}

// 关闭消息记录浏览窗口
void CSessChatDlg::CloseMsgLogBrowser()
{
	if (m_dwThreadId != NULL)
	{
		::PostThreadMessage(m_dwThreadId, WM_CLOSE_MSGLOGDLG, 0, 0);
		m_dwThreadId = NULL;
	}
}