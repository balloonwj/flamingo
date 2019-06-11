/**
 * �������촰��, BuddyChatDlg.cpp
 * zhangyl 2017.05.26
 */
#include "stdafx.h"
#include "BuddyChatDlg.h"
#include "MessageLogger.h"
#include "File2.h"
#include "MessageLogger.h"
#include "UserSessionData.h"
#include "Time.h"
#include "ChatDlgCommon.h"
#include "Utils.h"
#include "GDIFactory.h"
#include "EncodingUtil.h"
#include "net/protocolstream.h"
#include "net/Msg.h"
#include "UIText.h"
#include <time.h>
#include <UtilTime.h>


#define CHAT_BG_IMAGE_NAME			_T("BuddyChatDlgBg.png")
#define CHAT_EXPAND_BG_IMAGE_NAME   _T("BuddyChatDlgExpandBg.png")
//#define CHAT_BG_IMAGE_NAME			_T("1449046375909.png")
//#define CHAT_EXPAND_BG_IMAGE_NAME   _T("1449046375909.png")


const long CHATDLG_WIDTH        = 587;
const long CHATDLG_HEIGHT       = 535;
const long CHATDLG_EXPAND_WIDTH = 960;
const long RIGHT_CHAT_WINDOW_WIDTH = CHATDLG_EXPAND_WIDTH-CHATDLG_WIDTH;


BOOL CBuddyChatDlg::HandleText(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strTemp = GetBetweenString(p+2, _T("[\""), _T("\"]")).c_str();

	if (!strTemp.empty())
	{
		LPCTSTR lpFontFmt = _T("%[^,],%d,%[^,],%d,%d,%d");
		TCHAR szName[32] = _T("����");
		TCHAR szColor[32] = _T("000000");
		int nSize = 9;
		BOOL bBold = FALSE, bItalic = FALSE, bUnderLine = FALSE;

		int nCount = _stscanf(strTemp.c_str(), lpFontFmt, szName, 
			&nSize, &szColor, &bBold, &bItalic, &bUnderLine);

		strText = strTemp;
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
			lpContent->m_nType = CONTENT_TYPE_FONT_INFO;
			lpContent->m_FontInfo.m_nSize = nSize;
			lpContent->m_FontInfo.m_clrText = HexStrToRGB(szColor);
			lpContent->m_FontInfo.m_strName = szName;
			lpContent->m_FontInfo.m_bBold = bBold;
			lpContent->m_FontInfo.m_bItalic = bItalic;				
			lpContent->m_FontInfo.m_bUnderLine = bUnderLine;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CBuddyChatDlg::HandleShake(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strTemp = GetBetweenString(p, _T("["), _T("]")).c_str();
	if (!strTemp.empty())
	{
		if(strTemp == _T("\"1\""))
		{	
			strText = _T("������һ�����ڶ�����");
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

BOOL CBuddyChatDlg::HandleFontInfo(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strTemp = GetBetweenString(p+2, _T("[\""), _T("\"]")).c_str();
	if (!strTemp.empty())
	{
		LPCTSTR lpFontFmt = _T("%[^,],%d,%[^,],%d,%d,%d");
		TCHAR szName[32] = _T("����");
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

BOOL CBuddyChatDlg::HandleSysFaceId(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
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

BOOL CBuddyChatDlg::HandleCustomPic(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
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

BOOL CBuddyChatDlg::HandleFile(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
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
			lpContent->m_nType = CONTENT_TYPE_FILE;
			lpContent->m_CFaceInfo.m_strFilePath = strFileName;
			lpContent->m_CFaceInfo.m_strName = Hootina::CPath::GetFileName(strFileName.c_str());
			lpContent->m_CFaceInfo.m_strFileName = lpContent->m_CFaceInfo.m_strName;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}



// "/f["ϵͳ����id"] /c["�Զ�������ļ���"] /o[�������ƣ���С����ɫ���Ӵ֣���б���»���]"
void CBuddyChatDlg::AnalyseContent(tstring& strContent, HWND hRichWnd/*=NULL*/)
{
	//CONTENT_TYPE m_nType;			// ��������
	//CFontInfo m_FontInfo;			// ������Ϣ
	//tstring m_strText;			// �ı���Ϣ
	//int m_nFaceId;				// ϵͳ����Id
	//CCustomFaceInfo m_CFaceInfo;	// �Զ��������Ϣ

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
			else if (*(p+1) == _T('s'))						//���ڶ���
			{
				//arrContent.clear();
				if (HandleShake(p, strText, arrContent))
				{
					if(hRichWnd == NULL)
						AddMsgToMsgLogEdit(arrContent);
					else
						AddMsgToRecvEdit(arrContent);
					continue;
				}
			}
			else if (*(p+1) == _T('i'))					   //�ļ�
			{
				if (HandleFile(p, strText, arrContent))
				{
					if(hRichWnd == NULL)
						AddMsgToMsgLogEdit(arrContent);
					else
						AddMsgToRecvEdit(arrContent);
					continue;
				}
			}
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

CBuddyChatDlg::CBuddyChatDlg(void)
{
	//m_lpFMGClient = NULL;
	m_lpFaceList = NULL;
	m_lpCascadeWinManager = NULL;
	m_hMainDlg = NULL;
	m_nUTalkUin = NULL;

	m_hDlgIcon = m_hDlgSmallIcon = NULL;
	m_hRBtnDownWnd = NULL;
	memset(&m_ptRBtnDown, 0, sizeof(m_ptRBtnDown));
	m_pLastImageOle = NULL;
	m_cxPicBarDlg = 122;
	m_cyPicBarDlg = 24;

	m_nMsgLogIndexInToolbar = -1;

	m_nUTalkNumber = 0;
	m_nUserNumber = 0;
	m_strBuddyName = _T("�����ǳ�");


	m_bPressEnterToSendMessage = TRUE; 
	m_bMsgLogWindowVisible = FALSE;
	m_bFileTransferVisible = FALSE;

	m_HotRgn = NULL;

	m_bEnableAutoReply = FALSE;
	m_nMsgLogRecordOffset = 1;	
	m_nMsgLogCurrentPageIndex = 0;

	m_bDraged = FALSE;

	::SetRectEmpty(&m_rtRichRecv);
	::SetRectEmpty(&m_rtMidToolBar);
	::SetRectEmpty(&m_rtSplitter);
	::SetRectEmpty(&m_rtRichSend);

    m_nLastSendShakeWindowTime = 0;
}

CBuddyChatDlg::~CBuddyChatDlg(void)
{

}

BOOL CBuddyChatDlg::PreTranslateMessage(MSG* pMsg)
{
	//if (::GetForegroundWindow() == m_hWnd && !m_Accelerator.IsNull() && 
	//	m_Accelerator.TranslateAccelerator(m_hWnd, pMsg))
	//	return TRUE;

	if (pMsg->hwnd == m_richRecv.m_hWnd || pMsg->hwnd == m_richSend.m_hWnd || pMsg->hwnd == m_richMsgLog.m_hWnd)
	{
		if (pMsg->message == WM_MOUSEMOVE)			// ����/�����ı��������ƶ���Ϣ
		{
			if (OnRichEdit_MouseMove(pMsg))
				return TRUE;
		}
		else if (pMsg->message == WM_LBUTTONDBLCLK) // ����/�����ı�������˫����Ϣ
		{
			if (OnRichEdit_LBtnDblClk(pMsg))
				return TRUE;
		}
		else if (pMsg->message == WM_RBUTTONDOWN)	// ����/�����ı��������Ҽ�������Ϣ
		{
			if (OnRichEdit_RBtnDown(pMsg))
				return TRUE;
		}

		if ( (pMsg->hwnd == m_richSend.m_hWnd) && (pMsg->message == WM_KEYDOWN) 
			&& (pMsg->wParam == 'V') && (::GetAsyncKeyState(VK_CONTROL)&0x8000) )	// �����ı����Ctrl+V��Ϣ
		{
			m_richSend.PasteSpecial(CF_TEXT);
			return TRUE;
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

void CBuddyChatDlg::OnRecvMsg(UINT nUTalkUin, UINT nMsgId)
{
	if (NULL == m_lpFMGClient/* || m_nUTalkUin != nUTalkUin*/)
		return;

	if (::GetForegroundWindow() != m_hWnd)	
		FlashWindowEx(m_hWnd, 3);

	if (nMsgId == 0)
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
			if (lpMsgSender != NULL)
			{
				int nMsgCnt = lpMsgSender->GetMsgCount();
				for (int i = 0; i < nMsgCnt; i++)
				{
					CBuddyMessage* lpBuddyMsg = lpMsgSender->GetBuddyMsg(i);
					if (lpBuddyMsg != NULL)
					{
						AddMsgToRecvEdit(lpBuddyMsg);
					}
				}
				lpMsgList->DelMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
				::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_BUDDY, nUTalkUin);
			}
		}
	}
	else
	{
		CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
		if (lpMsgList != NULL)
		{
			CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
			if (lpMsgSender != NULL)
			{
				CBuddyMessage* lpBuddyMsg = lpMsgSender->GetBuddyMsgById(nMsgId);
				if (lpBuddyMsg != NULL)
				{
					AddMsgToRecvEdit(lpBuddyMsg);
					lpMsgSender->DelMsgById(nMsgId);
				}

				if (lpMsgSender->GetMsgCount() <= 0)
				{
					lpMsgList->DelMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
					::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_BUDDY, nUTalkUin);
				}
			}
		}
	}
}

// ���º��Ѻ���֪ͨ
void CBuddyChatDlg::OnUpdateBuddyNumber()
{
	UpdateData();
	UpdateBuddyNameCtrl();
}

// ���º���ǩ��֪ͨ
void CBuddyChatDlg::OnUpdateBuddySign()
{
	UpdateData();
	UpdateBuddySignCtrl();
}

// ���º���ͷ��֪ͨ
void CBuddyChatDlg::OnUpdateBuddyHeadPic()
{
	long nFaceID = 0;
	BOOL bGray = FALSE;
	BOOL bMobile = FALSE;
	CString strThumbPath;
	CBuddyInfo* pBuddyInfo = GetBuddyInfoPtr();
	if(pBuddyInfo != NULL)
	{
		nFaceID = pBuddyInfo->m_nFace;
		if(pBuddyInfo->m_nStatus==STATUS_INVISIBLE || pBuddyInfo->m_nStatus==STATUS_OFFLINE)
			bGray = TRUE;

		if(pBuddyInfo->m_nStatus == STATUS_MOBILE_ONLINE)
			bMobile = TRUE;
	}
	
	//if(lpBuddyInfo == NULL)
	//{
	//	::MessageBox(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, _T("��������һ�����صĴ����´�����Ի���ʧ�ܣ�"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR); 
	//	return;
	//}
	if(pBuddyInfo!=NULL && pBuddyInfo->m_bUseCustomFace && pBuddyInfo->m_bCustomFaceAvailable)
	{
		strThumbPath.Format(_T("%s%d.png"), m_lpFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), pBuddyInfo->m_uUserID);
		if(!Hootina::CPath::IsFileExist(strThumbPath))
			strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, pBuddyInfo->m_nFace);
	}
	else
		strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, pBuddyInfo->m_nFace);
	
	m_picHead.SetBitmap(strThumbPath, FALSE, bGray);
	
	CString strMobileImagePath;
	strMobileImagePath.Format(_T("%sImage\\mobile_online.png"), g_szHomePath);
	if(bMobile)
	{
		m_picHead.SetMobileBitmap(strMobileImagePath, TRUE);
	}
	else
	{
		m_picHead.SetMobileBitmap(strMobileImagePath, FALSE);
	}

	m_hDlgIcon = ExtractIcon(strThumbPath, 64);
	m_hDlgSmallIcon = m_hDlgIcon;
	if(m_hDlgIcon == NULL)
	{
		m_hDlgIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_32, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
		m_hDlgSmallIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	}
	SetIcon(m_hDlgIcon, TRUE);
	SetIcon(m_hDlgSmallIcon, FALSE);


	if(m_picHead.IsWindow())
		m_picHead.Invalidate(FALSE);
}

BOOL CBuddyChatDlg::IsFilesTransferring()
{
	return m_FileTransferCtrl.GetItemCount() > 0;
}

BOOL CBuddyChatDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	m_lpCascadeWinManager->Add(m_hWnd, CHATDLG_WIDTH, CHATDLG_HEIGHT);

	CString strThumbPath;
	CBuddyInfo* lpBuddyInfo = m_lpFMGClient->m_UserMgr.m_BuddyList.GetBuddy(m_nUTalkUin);
	if(lpBuddyInfo == NULL)
	{
		::MessageBox(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, _T("��������һ�����صĴ����´�����Ի���ʧ�ܣ�"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR); 
		return FALSE;
	}
	if(lpBuddyInfo->m_bUseCustomFace && !lpBuddyInfo->m_strCustomFace.empty() && lpBuddyInfo->m_bCustomFaceAvailable)
	{
		strThumbPath.Format(_T("%s%s"), m_lpFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), lpBuddyInfo->m_strCustomFace.c_str());
		if(!Hootina::CPath::IsFileExist(strThumbPath))
			strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, lpBuddyInfo->m_nFace);
	}
	else
		strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, lpBuddyInfo->m_nFace);

	m_hDlgIcon = ExtractIcon(strThumbPath, 64);
	m_hDlgSmallIcon = m_hDlgIcon;
	if(m_hDlgIcon == NULL)
	{
		m_hDlgIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_32, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
		m_hDlgSmallIcon = AtlLoadIconImage(IDI_BUDDYCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	}
	SetIcon(m_hDlgIcon, TRUE);
	SetIcon(m_hDlgSmallIcon, FALSE);

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);

	UpdateData();

	m_FontSelDlg.m_pFMGClient = m_lpFMGClient;
	m_bPressEnterToSendMessage = m_lpFMGClient->m_UserConfig.IsEnablePressEnterToSend();

	
	//if (lpBuddyInfo != NULL)
	//{
	//	if (!lpBuddyInfo->IsHasUTalkNum())		// ���º��Ѻ���
	//	{
	//		m_lpFMGClient->UpdateBuddyNum(m_nUTalkUin);
	//	}
	//	else								// ���º���ͷ��
	//	{
	//		if (m_lpFMGClient->IsNeedUpdateBuddyHeadPic(m_nUTalkNumber))
	//			m_lpFMGClient->UpdateBuddyHeadPic(m_nUTalkUin, m_nUTalkNumber);
	//	}

	//	if (!lpBuddyInfo->IsHasUTalkSign())	// ���¸���ǩ��
	//		m_lpFMGClient->UpdateBuddySign(m_nUTalkUin);
	//}

	Init();		// ��ʼ��

	SetHotRgn();

    //ModifyStyle(0, WS_CLIPCHILDREN);

    //FIXME: win7�ù���ԱȨ������,���͸��ı�����û�����մ�����ק,�������ڿ���
    if (IsWindowsVistaOrGreater())
    {
        //win 7����ԱȨ������ Ĭ���Ǵ�UIPI���û�������Ȩ���롿�Ƴ�WM_DROPFILES��Ϣ��,
        //����ȥ����������
        //see: http://blog.csdn.net/whatday/article/details/44278605
        //see: https://msdn.microsoft.com/EN-US/library/windows/desktop/ms632675(v=vs.85).aspx
        typedef BOOL(WINAPI* ChangeWindowMessageFilterOkFn)(HWND, UINT, DWORD, PCHANGEFILTERSTRUCT);

        HMODULE hModUser32 = LoadLibrary(_T("user32.dll"));
        if (hModUser32 != NULL) 
        { 
            ChangeWindowMessageFilterOkFn pfnChangeWindowMessageFilter = (ChangeWindowMessageFilterOkFn)GetProcAddress(hModUser32, "ChangeWindowMessageFilterEx");
            if (pfnChangeWindowMessageFilter != NULL)
            {
                //MSGFLT_ADD = 1
                //WM_COPYGLOBALDATA = 0x0049
                pfnChangeWindowMessageFilter(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, NULL);
                pfnChangeWindowMessageFilter(m_hWnd, 0x0049, MSGFLT_ALLOW, NULL);

                //pfnChangeWindowMessageFilter(m_richSend.m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, NULL);
                //pfnChangeWindowMessageFilter(m_richSend.m_hWnd, 0x0049, MSGFLT_ALLOW, NULL);
            }
            FreeLibrary(hModUser32);       
        }
    }

	//������ק�ļ�������
	::DragAcceptFiles(m_hWnd, TRUE); 
    //::DragAcceptFiles(m_richSend.m_hWnd, TRUE);
	//PostMessage(WM_SETDLGINITFOCUS, 0, 0);		// ���öԻ����ʼ����
	SetTimer(1001, 300, NULL);
	
	CalculateMsgLogCountAndOffset();

	if(m_lpFMGClient->m_UserConfig.IsEnableShowLastMsgInChatDlg())
		ShowLastMsgInRecvRichEdit();


	return TRUE;
}

void CBuddyChatDlg::CalculateMsgLogCountAndOffset()
{
	//��ȡ��Ϣ��¼����
	CString strMsgFile = m_lpFMGClient->GetMsgLogFullName().c_str();
	strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);
	long nTotal = m_MsgLogger.GetBuddyMsgLogCount(m_nUTalkUin);
	
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

BOOL CBuddyChatDlg::OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct)
{
	if (NULL == pCopyDataStruct)
		return FALSE;

	switch (pCopyDataStruct->dwData)
	{
	case IPC_CODE_MSGLOG_PASTE:			// ��Ϣ��¼�������ճ����Ϣ
		{
			if (pCopyDataStruct->lpData != NULL && pCopyDataStruct->cbData > 0)
				AddMsgToSendEdit((LPCTSTR)pCopyDataStruct->lpData);
		}
		break;

	case IPC_CODE_MSGLOG_EXIT:			// ��Ϣ��¼��������˳���Ϣ
		{
			m_tbMid.SetItemCheckState(13, FALSE);
			m_tbMid.Invalidate();
		}
		break;
	}

	return TRUE;
}

void CBuddyChatDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	m_SkinMenu.OnMeasureItem(nIDCtl, lpMeasureItemStruct);
}

void CBuddyChatDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	m_SkinMenu.OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CBuddyChatDlg::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	if (m_bMsgLogWindowVisible)
	{
		lpMMI->ptMinTrackSize.x = CHATDLG_EXPAND_WIDTH;
		lpMMI->ptMinTrackSize.y = CHATDLG_HEIGHT;
	}
	else
	{
		lpMMI->ptMinTrackSize.x = CHATDLG_WIDTH;
		lpMMI->ptMinTrackSize.y = CHATDLG_HEIGHT;
	}
}

void CBuddyChatDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (::GetCapture() == m_SplitterCtrl.m_hWnd)
	{
		ReCaculateCtrlPostion(point.y);
	}
}

void CBuddyChatDlg::OnMove(CPoint ptPos)
{
	SetMsgHandled(FALSE);

	m_lpCascadeWinManager->SetPos(m_hWnd, ptPos.x, ptPos.y);
}

void CBuddyChatDlg::OnSize(UINT nType, CSize size)
{
	//��������е�ӷ�ף�����˼·�ǣ�
	//����û�ͨ����ק���Ϳ�����˴��ڳߴ磬��ָ���Ĵ��ڸ��ؼ���λ������Ϊ�������λ��
	//��֮�����ؼ�ʹ��Ĭ��λ��
	if (!m_bMsgLogWindowVisible && !m_bFileTransferVisible)
	{
		if (m_tbTop.IsWindow())
			m_tbTop.MoveWindow(3, 70, size.cx-5, 32, TRUE);

		if(m_staPicUploadProgress.IsWindow())
			m_staPicUploadProgress.MoveWindow(10, size.cy-25, 380, 25, FALSE);
		
		if (m_btnClose.IsWindow())
			m_btnClose.MoveWindow(size.cx-190, size.cy-30, 77, 25, TRUE);

		if (m_btnSend.IsWindow())
			m_btnSend.MoveWindow(size.cx-110, size.cy-30, 77, 25, TRUE);

		if (m_btnArrow.IsWindow())
			m_btnArrow.MoveWindow(size.cx-33, size.cy-30, 28, 25, TRUE);


		if (m_richRecv.IsWindow())
		{
			if(m_bDraged)
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-8, m_rtRichRecv.bottom-m_rtRichRecv.top-32);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-8, m_rtRichRecv.bottom-m_rtRichRecv.top);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, m_rtRichRecv.bottom-32, size.cx-20, 32, TRUE);
			}
			else
			{
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-8, size.cy-305, TRUE);
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-8, size.cy-273, TRUE);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, size.cy-197, size.cx-20, 32, TRUE);
			}
		}

		if (m_tbMid.IsWindow())
		{
			if(m_bDraged)
				m_tbMid.MoveWindow(3, m_rtMidToolBar.top, size.cx-5, 31, TRUE);
			else
				m_tbMid.MoveWindow(3, size.cy-167, size.cx-5, 31, TRUE);
			//��Ϣ��¼��ťʼ�տ���
			m_tbMid.SetItemMargin(m_nMsgLogIndexInToolbar, size.cx-240, 0);
		}
		
		if(m_SplitterCtrl.IsWindow())
		{
			if(m_bDraged)
				m_SplitterCtrl.MoveWindow(6, m_rtSplitter.top, size.cx-8, 5, TRUE);
			else
				m_SplitterCtrl.MoveWindow(6, size.cy-135, size.cx-8, 5, TRUE);
		}


		if (m_richSend.IsWindow())
		{
			if(m_bDraged)
				m_richSend.MoveWindow(6, m_rtRichSend.top, size.cx-8, size.cy-m_rtRichSend.top-35, FALSE);
			else
				m_richSend.MoveWindow(6, size.cy-130, size.cx-8, 95, FALSE);
		}

	}
	else
	{	
		if(m_staPicUploadProgress.IsWindow())
			m_staPicUploadProgress.MoveWindow(10, size.cy-25, 380, 25, TRUE);
		
		if (m_btnClose.IsWindow())
			m_btnClose.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH-190, size.cy-30, 77, 25, TRUE);

		if (m_btnSend.IsWindow())
			m_btnSend.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH-110, size.cy-30, 77, 25, TRUE);

		if (m_btnArrow.IsWindow())
			m_btnArrow.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH-33, size.cy-30, 28, 25, TRUE);
		
		//�����¼��ҳ�ĸ���ť
		if (m_btnFirstMsgLog.IsWindow())
			m_btnFirstMsgLog.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+110, size.cy-30, 28, 25, TRUE);

		if (m_btnPrevMsgLog.IsWindow())
			m_btnPrevMsgLog.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+140, size.cy-30, 28, 25, TRUE);

		if (m_staMsgLogPage.IsWindow())
			m_staMsgLogPage.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+180, size.cy-24, 60, 25, TRUE);

		if (m_btnNextMsgLog.IsWindow())
			m_btnNextMsgLog.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+240, size.cy-30, 28, 25, TRUE);

		if (m_btnLastMsgLog.IsWindow())
			m_btnLastMsgLog.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+270, size.cy-30, 28, 25, TRUE);

		if (m_tbTop.IsWindow())
			m_tbTop.MoveWindow(3, 70, size.cx-RIGHT_CHAT_WINDOW_WIDTH-5, 32, TRUE);

		if (m_richRecv.IsWindow())
		{
			if(m_bDraged)
			{		
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, m_rtRichRecv.bottom-m_rtRichRecv.top-32);	
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, m_rtRichRecv.bottom-m_rtRichRecv.top);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, m_rtRichRecv.bottom-32, size.cx-RIGHT_CHAT_WINDOW_WIDTH-20, 32);
	
			}
			else
			{	
				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_richRecv.MoveWindow(6, 106, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, size.cy-305);	
				else if((m_FontSelDlg.IsWindow()&&!m_FontSelDlg.IsWindowVisible()) || !m_FontSelDlg.IsWindow())
					m_richRecv.MoveWindow(6, 106, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, size.cy-273);

				if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
					m_FontSelDlg.MoveWindow(2, size.cy-RIGHT_CHAT_WINDOW_WIDTH-197, size.cx-20, 32, TRUE);
			}
		}

		if (m_tbMid.IsWindow())
		{
			if(m_bDraged)
				m_tbMid.MoveWindow(3, m_rtMidToolBar.top, size.cx-RIGHT_CHAT_WINDOW_WIDTH-5, 31, TRUE);
			else
				m_tbMid.MoveWindow(3, size.cy-167, size.cx-RIGHT_CHAT_WINDOW_WIDTH-5, 31, TRUE);
			//��Ϣ��¼��ťʼ�տ���
			m_tbMid.SetItemMargin(m_nMsgLogIndexInToolbar, size.cx-RIGHT_CHAT_WINDOW_WIDTH-240, 0);
		}

		if(m_SplitterCtrl.IsWindow())
		{
			if(m_bDraged)
				m_SplitterCtrl.MoveWindow(6, m_rtSplitter.top, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, 5, TRUE);
			else
				m_SplitterCtrl.MoveWindow(6, size.cy-135, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, 5, TRUE);
		}

		if (m_richSend.IsWindow()) 
		{
			if(m_bDraged)
				m_richSend.MoveWindow(6, m_rtRichSend.top, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, size.cy-m_rtRichSend.top-35, TRUE);
			else
				m_richSend.MoveWindow(6, size.cy-130, size.cx-RIGHT_CHAT_WINDOW_WIDTH-8, 95, TRUE);
		}

		if(m_TabMgr.IsWindow())
		{
			//CRect rcTabMgr(CHATDLG_WIDTH-1, 69, CHATDLG_WIDTH+RIGHT_CHAT_WINDOW_WIDTH-2, 101);
			m_TabMgr.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH, 69, RIGHT_CHAT_WINDOW_WIDTH-2, 32);
		}
		
		if (m_richMsgLog.IsWindow())
		{
			m_richMsgLog.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+4, 106, RIGHT_CHAT_WINDOW_WIDTH-6, size.cy-140);	
		}

		if(m_bFileTransferVisible)
		{
			if (m_FileTransferCtrl.IsWindow())
			{
				m_FileTransferCtrl.MoveWindow(size.cx-RIGHT_CHAT_WINDOW_WIDTH+4, 106, RIGHT_CHAT_WINDOW_WIDTH-6, size.cy-140);	
			}
		}
		
	}
	
	ResizeImageInRecvRichEdit();


	SetMsgHandled(TRUE);
}

void CBuddyChatDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (1001 == nIDEvent)
	{
		OnRecvMsg(m_nUTalkUin, NULL);	// ��ʾ��Ϣ
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

void CBuddyChatDlg::OnDropFiles(HDROP hDropInfo)
{ 
	UINT nFileNum = ::DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0); // ��ק�ļ�����  
    TCHAR szFileName[MAX_PATH];  
    for (UINT i=0; i<nFileNum; ++i)    
    {  
		::DragQueryFile(hDropInfo, i, szFileName, MAX_PATH);//�����ҷ���ļ���  
        HandleFileDragResult(szFileName);    
    }  
    DragFinish(hDropInfo);      //�ͷ�hDropInfo  

    //InvalidateRect(hwnd, NULL, TRUE); 
}

BOOL CBuddyChatDlg::HandleFileDragResult(PCTSTR lpszFileName)
{
	if(lpszFileName == NULL) 
		return FALSE;
	
	//������ļ��У������ļ���
	if(Hootina::CPath::IsDirectory(lpszFileName))
	{
		//TODO: �����ļ���
		return TRUE;
	}

	CString strFileExtension(Hootina::CPath::GetExtension(lpszFileName).c_str());
	strFileExtension.MakeLower();

	//�����ͼƬ��ʽ�������ͼƬ
	if( strFileExtension==_T("jpg")  ||
		strFileExtension==_T("jpeg") ||
	    strFileExtension==_T("png")  ||
	    strFileExtension==_T("bmp")  ||
		strFileExtension==_T("gif") )
	{
		//UINT64 nFileSize = IUGetFileSize2(lpszFileName);
		//if(nFileSize > MAX_CHAT_IMAGE_SIZE)
		//{
		//	::MessageBox(m_hWnd, _T("ͼƬ��С����10M����ʹ���ļ����͡�"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		//	return FALSE;
		//}
		
		_RichEdit_InsertFace(m_richSend.m_hWnd, lpszFileName, -1, -1);
		m_richSend.SetFocus();
		return TRUE;
	}
	else
	{
		return SendOfflineFile(lpszFileName);
	}


	return FALSE;
}

void CBuddyChatDlg::OnClose()
{
	long nFileItemCount = m_FileTransferCtrl.GetItemCount();
	if(nFileItemCount > 0)
	{
		//TODO: ϵͳ�Ի���̫���Լ���һ���Ի���
		ShowWindow(SW_RESTORE);
        if (IDNO == ::MessageBox(m_hWnd, _T("���ļ����ڴ��䣬�رմ��ڽ���ֹ���䣬ȷʵҪ�ر���"), g_strAppTitle.c_str(), MB_YESNO | MB_ICONQUESTION))
			return;

		//ֹͣ���ڴ�����ļ�
		CFileItemRequest* pFileItemRequest = NULL;
		FILE_TARGET_TYPE nTargetType = SEND_TYPE;
		CString strRecvFileResultMsgText;
		for(long i=0; i<nFileItemCount; ++i)
		{
			//ע�⣺����ļ���û�н��д��䣬��ôpFileItemRequest����ֵΪNULL������Ҳ��Ӱ��ʹ�á�
			pFileItemRequest = m_FileTransferCtrl.GetFileItemRequestByIndex((size_t)i);
			nTargetType = m_FileTransferCtrl.GetItemTargetTypeByIndex((size_t)i);
			if(pFileItemRequest != NULL)
			{
				//m_lpFMGClient->m_FileTask.RemoveItem(pFileItemRequest);
			}
			
			//��ʾ�Է���ȡ�����ļ��Ľ���
			if(nTargetType == RECV_TYPE)
			{
				strRecvFileResultMsgText.Format(_T("%sȡ���˽����ļ�[%s]��"), m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str(), m_FileTransferCtrl.GetItemFileNameByIndex((size_t)i));
				m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), time(NULL), strRecvFileResultMsgText.GetString(), m_hWnd);
			}		
		}
	}
	

	//ģ��ر���Ϣ��¼���ڶ�������ʹ�ٴδ򿪸ô���ʱ�ߴ�����
	//m_bMsgLogWindowVisible = FALSE;
	//PostMessage(WM_COMMAND, IDC_BTN_MSGLOG, 0);

	RecordWindowSize();

	//֪ͨ�����ڹرյ�ǰ����Ի���
	::PostMessage(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, WM_CLOSE_BUDDYCHATDLG, 0, (LPARAM)m_nUTalkUin);
}

void CBuddyChatDlg::OnDestroy()
{
	SetMsgHandled(FALSE);
	
	CloseMsgLogBrowser();

	m_lpCascadeWinManager->Del(m_hWnd);

	UnInit();	// ����ʼ���ؼ�

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

// ���������ơ������ӿؼ�
void CBuddyChatDlg::OnLnk_BuddyName(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	::PostMessage(m_hMainDlg, WM_SHOW_BUDDYINFODLG, NULL, m_nUTalkUin);
}

void CBuddyChatDlg::OnBtn_RemoteDesktop(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    //TODO: Զ������Ĵ�������ע�͵� 
    if (m_lpFMGClient->IsOffline())
    {
        MessageBox(_T("���Ѿ���������״̬���޷�����Զ�����棬�����ߺ��ٴγ��ԡ�"), g_strAppTitle.c_str());
        return;
    }

    CString strInfo(_T("                                            ��������Զ�������\r\n"));
    time_t nNow = time(NULL);
    //TODO: ����Ѿ�����Զ�����棬����ʾ��
    //if (nNow - m_nLastSendShakeWindowTime <= 5)
    //{
     //   strInfo = _T("                                        �������͵Ĵ��ڶ�������Ƶ�������Ժ��ٷ�����\r\n");
    //}
   // else
    //{
        //m_nLastSendShakeWindowTime = nNow;
        //ShakeWindow(m_hWnd, 1);
        m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nNow, _T("/r[\"1\"]"), m_hWnd);
    //}

    RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
    RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10, RGB(0, 0, 0), FALSE, FALSE, FALSE, FALSE, 0);
    m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);

    ::PostMessage(m_hMainDlg, FMG_MSG_SCREENSHOT, 0, 0);

    //HANDLE hRemoteDesktopThread = (HANDLE)::_beginthreadex(NULL, 0, RemoteDesktopProc, this, 0, NULL);
    //::CloseHandle(hRemoteDesktopThread);
}

// ������ѡ�񹤾�������ť
void CBuddyChatDlg::OnBtn_Font(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	RECT rtRichRecv;
	::GetWindowRect(m_richRecv, &rtRichRecv);
	::ScreenToClient(m_hWnd, rtRichRecv);	
	
	if (BN_PUSHED == uNotifyCode)
	{
		m_FontSelDlg.ShowWindow(SW_SHOW);
		m_richRecv.MoveWindow(6, 106, rtRichRecv.right-rtRichRecv.left, rtRichRecv.bottom-rtRichRecv.top-32, TRUE);
		m_FontSelDlg.MoveWindow(6, rtRichRecv.bottom-32, rtRichRecv.right-rtRichRecv.left, 32, TRUE);	
	}
	else if (BN_UNPUSHED == uNotifyCode)
	{
		m_richRecv.MoveWindow(6, 106, rtRichRecv.right-rtRichRecv.left, rtRichRecv.bottom-rtRichRecv.top+32);
		m_FontSelDlg.ShowWindow(SW_HIDE);
	}
}

// �����顱��ť
void CBuddyChatDlg::OnBtn_Face(UINT uNotifyCode, int nID, CWindow wndCtl)
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

//�ο���http://www.rupeng.com/forum/thread-6423-1-1.html
void CBuddyChatDlg::OnShakeWindow(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (m_lpFMGClient->IsOffline())
	{
        MessageBox(_T("���Ѿ���������״̬���޷����ʹ��ڶ����������ߺ��ٴγ��ԡ�"), g_strAppTitle.c_str());
		return;
	}
	
    CString strInfo(_T("                                            ����������һ�����ڶ�����\r\n"));
    time_t nNow = time(NULL);
    //�������ڶ�������Сʱ������5��
    if (nNow - m_nLastSendShakeWindowTime <= 5)
    {
        strInfo = _T("                                        �������͵Ĵ��ڶ�������Ƶ�������Ժ��ٷ�����\r\n");
    }
    else
    {
        m_nLastSendShakeWindowTime = nNow;
        ShakeWindow(m_hWnd, 1);
        m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nNow, _T("/s[\"1\"]"), m_hWnd);
    }

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
    RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	
}

// ������ͼƬ����ť
void CBuddyChatDlg::OnBtn_Image(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	BOOL bOpenFileDialog = TRUE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = NULL;
	DWORD dwFlags = OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_EXTENSIONDIFFERENT;
	LPCTSTR lpszFilter = _T("ͼ���ļ�(*.bmp;*.jpg;*.jpeg;*.gif;*.png)\0*.bmp;*.jpg;*.jpeg;*.gif;*.png\0\0");
	HWND hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("��ͼƬ");
	if (fileDlg.DoModal() == IDOK)
	{
		UINT64 nFileSize = IUGetFileSize2(fileDlg.m_ofn.lpstrFile);
		if(nFileSize > MAX_CHAT_IMAGE_SIZE)
		{
            ::MessageBox(m_hWnd, _T("ͼƬ��С����10M����ʹ���ļ���ʽ���ͻ�ʹ�ý�ͼ���ߡ�"), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
			return;
		}
		
		_RichEdit_InsertFace(m_richSend.m_hWnd, fileDlg.m_ofn.lpstrFile, -1, -1);
		m_richSend.SetFocus();
	}
}

void CBuddyChatDlg::OnBtn_ScreenShot(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DWORD dwSucceedExitCode = 2;
	CString strCatchScreen;
	strCatchScreen.Format(_T("%sCatchScreen.exe %u"), g_szHomePath, dwSucceedExitCode);
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	if(!CreateProcess(NULL, strCatchScreen.GetBuffer(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
        ::MessageBox(m_hWnd, _T("������ͼ����ʧ�ܣ�"), g_strAppTitle.c_str(), MB_OK | MB_ICONERROR);
	}
	if(pi.hProcess != NULL)
	{
		::WaitForSingleObject(pi.hProcess, INFINITE);
		
		dwSucceedExitCode = 0;

		if(::GetExitCodeProcess(pi.hProcess, &dwSucceedExitCode) && dwSucceedExitCode==2)
			m_richSend.PasteSpecial(CF_TEXT);
	}
}

// ����Ϣ��¼����ť
void CBuddyChatDlg::OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bMsgLogWindowVisible = !m_bMsgLogWindowVisible;

	CRect rtWindow;
	GetWindowRect(&rtWindow);

	//��ȡ��ǰ��������Ļ��λ��	
	if(m_bMsgLogWindowVisible)
	{
		if(m_TabMgr.GetItemCount() == 0)
		{
			m_SkinDlg.SetBgPic(CHAT_EXPAND_BG_IMAGE_NAME, CRect(4, 100, 445, 32));
			::SetWindowPos(m_SkinDlg.m_hWnd, NULL, 0, 0, rtWindow.Width()+RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(), SWP_NOMOVE|SWP_NOZORDER);
		}
		
		m_TabMgr.AddItem(_T("��Ϣ��¼"), m_richMsgLog.m_hWnd);
		OpenMsgLogBrowser();

		m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T("<<"));
		
	}
	else
	{
		CloseMsgLogBrowser();

		m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T(">>"));
		m_TabMgr.RemoveItem(m_richMsgLog.m_hWnd);
		
		//TabMgr�������Ϊ0��ȥ�����Ҳര��
		if(m_TabMgr.GetItemCount() == 0)
		{
			m_SkinDlg.SetBgPic(CHAT_BG_IMAGE_NAME, CRect(4, 100, 4, 32));
			::SetWindowPos(m_SkinDlg.m_hWnd, NULL, 0, 0, rtWindow.Width()-RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(), SWP_NOMOVE|SWP_NOZORDER);
		}			
	}
}

//��ʾ�ļ�����ؼ�
void CBuddyChatDlg::DisplayFileTransfer(BOOL bShow)
{
	if(bShow)
		m_TabMgr.Active(m_FileTransferCtrl.m_hWnd);
	//�ļ���������Ѿ������ˣ��򼤻��ļ����䴰��
	if(bShow && m_bFileTransferVisible)	
		return;

	m_bFileTransferVisible = bShow;

	//��ȡ��ǰ��������Ļ��λ��	
	CRect rtWindow;
	GetWindowRect(&rtWindow);

	if(m_bFileTransferVisible)
	{
		m_SkinDlg.SetBgPic(CHAT_EXPAND_BG_IMAGE_NAME, CRect(4, 100, 445, 32));
		
		if(m_TabMgr.GetItemCount() <= 0)
			::SetWindowPos(m_SkinDlg.m_hWnd, NULL, 0, 0, rtWindow.Width()+RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(), SWP_NOMOVE|SWP_NOZORDER);
		
		//m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T("<<"));
		m_TabMgr.AddItem(_T("�����ļ�"), m_FileTransferCtrl.m_hWnd, FALSE);
		//m_TabMgr.ShowWindow(SW_SHOW);
		ShowFileTransferCtrl(TRUE);
	}
	else
	{
		//m_tbMid.SetItemText(m_nMsgLogIndexInToolbar, _T(">>"));
		//����ļ��������Ѿ�û���ļ��ڴ����ˣ��������ļ�����ؼ�
		if(m_FileTransferCtrl.GetItemCount() == 0)
		{
			m_TabMgr.RemoveItem(m_FileTransferCtrl.m_hWnd);
			ShowFileTransferCtrl(FALSE);
		}

		m_SkinDlg.SetBgPic(CHAT_BG_IMAGE_NAME, CRect(4, 100, 4, 32));
		//AtlTrace(_T("rtWindow.Width()-RIGHT_CHAT_WINDOW_WIDTH=%d\n"), rtWindow.Width()-RIGHT_CHAT_WINDOW_WIDTH);
		if(m_TabMgr.GetItemCount() <= 0)
		{
			m_bMsgLogWindowVisible = TRUE;	
			SendMessage(WM_COMMAND, 214, 0);
			//::SetWindowPos(m_SkinDlg.m_hWnd, NULL, 0, 0, rtWindow.Width()-RIGHT_CHAT_WINDOW_WIDTH, rtWindow.Height(), SWP_NOMOVE|SWP_NOZORDER);
			m_bMsgLogWindowVisible = FALSE;
		}

	}
}

// ��������Ϊ����ť
void CBuddyChatDlg::OnBtn_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	OnMenu_SaveAs(uNotifyCode, nID, wndCtl);
}

void CBuddyChatDlg::OnMsgLogPage(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	long nTotalCount = m_MsgLogger.GetBuddyMsgLogCount(m_nUTalkUin);
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

//�س���������Ϣ
void CBuddyChatDlg::OnPressEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bPressEnterToSendMessage = TRUE;
	m_lpFMGClient->m_UserConfig.EnablePressEnterToSend(m_bPressEnterToSendMessage);
}

//ctrl+�س���������Ϣ
void CBuddyChatDlg::OnPressCtrlEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bPressEnterToSendMessage = FALSE;
	m_lpFMGClient->m_UserConfig.EnablePressEnterToSend(m_bPressEnterToSendMessage);
}

void CBuddyChatDlg::OnAutoReply(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_bEnableAutoReply = !m_bEnableAutoReply;
}

// ���رա���ť
void CBuddyChatDlg::OnBtn_Close(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	PostMessage(WM_CLOSE);
}

// �����͡���ť
void CBuddyChatDlg::OnBtn_Send(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    if (m_lpFMGClient->IsOffline())
	{
        MessageBox(_T("���Ѿ���������״̬���޷�������Ϣ�������ߺ��ٴγ��ԡ�"), g_strAppTitle.c_str());
		return;
	}

	int nCustomPicCnt = RichEdit_GetCustomPicCount(m_richSend.m_hWnd);
	if (nCustomPicCnt > 1)
	{
        MessageBox(_T("ÿ����Ϣ������1��ͼƬ������ͼƬ��������͡�"), g_strAppTitle.c_str());
		return;
	}

	tstring strText;
	RichEdit_GetText(m_richSend.m_hWnd, strText);
	if (strText.empty())
	{
		::MessageBox(m_hWnd, _T("�������ݲ���Ϊ�գ�"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

	if(strText.length() > 1800)
	{
		::MessageBox(m_hWnd, _T("�����͵�����̫������������ͣ�"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		return;
	}

    time_t nMsgTime = time(NULL);
    AddMsgToRecvEdit(nMsgTime, strText.c_str());

	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();

	TCHAR szColor[32] = {0};
	RGBToHexStr(fontInfo.m_clrText, szColor, sizeof(szColor)/sizeof(TCHAR));

	//������Ϣ��ʽ�ǣ�/0["������,�ֺ�,��ɫ,����,б��,�»���"]
	TCHAR szFontInfo[1024] = {0};
	LPCTSTR lpFontFmt = _T("/o[\"%s,%d,%s,%d,%d,%d\"]");
	wsprintf(szFontInfo, lpFontFmt, fontInfo.m_strName.c_str(), fontInfo.m_nSize, szColor, fontInfo.m_bBold, fontInfo.m_bItalic, fontInfo.m_bUnderLine);
	strText += szFontInfo;
	
	//��windows���з�\r\n����\n
	//\r 0x0A \n 0x0D
    m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nMsgTime, strText.c_str(), m_hWnd);

	m_richSend.SetWindowText(_T(""));
	m_richSend.SetFocus();

}

// ����ͷ����ť
void CBuddyChatDlg::OnBtn_Arrow(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(8);
	if (PopupMenu.IsMenu())
	{
		CRect rc;
		m_btnArrow.GetClientRect(&rc);
		m_btnArrow.ClientToScreen(&rc);
		m_bPressEnterToSendMessage = m_lpFMGClient->m_UserConfig.IsEnablePressEnterToSend();
		if(m_bPressEnterToSendMessage)
		{
			PopupMenu.CheckMenuItem(ID_32852, MF_CHECKED);
			PopupMenu.CheckMenuItem(ID_32853, MF_UNCHECKED);
		}
		else
		{
			PopupMenu.CheckMenuItem(ID_32852, MF_UNCHECKED);
			PopupMenu.CheckMenuItem(ID_32853, MF_CHECKED);
		}

		
		PopupMenu.CheckMenuItem(IDM_AUTOREPLY, m_bEnableAutoReply ? MF_CHECKED : MF_UNCHECKED);
		

		PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, 
			rc.left, rc.bottom + 4, m_hWnd, &rc);
	}
}

void CBuddyChatDlg::OnOpenTransferFileItem(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(m_strLinkUrl.IsEmpty())
		return;

	if(nID == IDM_OPENFILE)
		::ShellExecute(NULL, _T("open"), m_strLinkUrl, NULL, NULL, SW_SHOWNORMAL);
	else if(nID == IDM_OPENDIRECTORY)
	{
		//�ο���http://zhidao.baidu.com/link?url=uEc51-5yf52fP2PHVl7mvAx2CpA8s1R5j8cyCWhYR8AaNE7KQaswGoguJtCI-ZlXMtiD02Iq6K_hBrbCvXoHfGy8ez3t3KtdHeIydox8wDu
		CString strCmdLine;
		strCmdLine.Format(_T("/n,/select,%s"), m_strLinkUrl);
		::ShellExecute(NULL, _T("open"), _T("explorer.exe"), strCmdLine, NULL, SW_SHOWNORMAL);
	}
}


LRESULT CBuddyChatDlg::OnToolbarDropDown(LPNMHDR pnmh)
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

// ����������Ϣ
LRESULT CBuddyChatDlg::OnUpdateFontInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();
	RichEdit_SetDefFont(m_richSend.m_hWnd, fontInfo.m_strName.c_str(),
		fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold,
		fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE);
	return 0;
}

// �����顱�ؼ�ѡȡ��Ϣ
LRESULT CBuddyChatDlg::OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam)
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

// ���öԻ����ʼ����
LRESULT CBuddyChatDlg::OnSetDlgInitFocus(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	m_richSend.SetFocus();
	return 0;
}

//	��������Ϣ�����ı������ӵ����Ϣ
LRESULT CBuddyChatDlg::OnRichEdit_Recv_Link(LPNMHDR pnmh)
{
	//CString strUrl;

	if (pnmh->code == EN_LINK)
	{
		ENLINK*pLink = (ENLINK*)pnmh;
		if (pLink->msg == WM_LBUTTONUP)
		{
			m_richRecv.SetSel(pLink->chrg);
			m_richRecv.GetSelText(m_strLinkUrl);

			if (m_strLinkUrl.Left(7).MakeLower()==_T("http://") || m_strLinkUrl.Left(8).MakeLower()==_T("https://") ||
				(m_strLinkUrl.GetLength() >= 7 && m_strLinkUrl.Left(4).MakeLower()==_T("www.")))
			{
				::ShellExecute(NULL, _T("open"), m_strLinkUrl, NULL, NULL, SW_SHOWNORMAL);
			}
			else if(::PathFileExists(m_strLinkUrl))
			{
				DWORD dwPos = GetMessagePos();
				CPoint point(GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos));

				CSkinMenu PopupMenu = m_SkinMenu.GetSubMenu(12);
				PopupMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, m_hWnd);
			}
		}
	}
	return 0;
}

LRESULT CBuddyChatDlg::OnRichEdit_Send_Paste(LPNMHDR pnmh)
{
	NMRICHEDITOLECALLBACK* lpOleNotify = (NMRICHEDITOLECALLBACK*)pnmh;
	if (lpOleNotify != NULL && lpOleNotify->hdr.code == EN_PASTE
		&& lpOleNotify->hdr.hwndFrom == m_richSend.m_hWnd)
	{
		AddMsgToSendEdit(lpOleNotify->lpszText);
	}
	return 0;
}

// �����С��˵�
void CBuddyChatDlg::OnMenu_Cut(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.Cut();
}

// �����ơ��˵�
void CBuddyChatDlg::OnMenu_Copy(UINT uNotifyCode, int nID, CWindow wndCtl)
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
	else if(hWnd == m_richMsgLog.m_hWnd)
	{
		m_richMsgLog.Copy();
	}
}

// ��ճ�����˵�
void CBuddyChatDlg::OnMenu_Paste(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richSend.PasteSpecial(CF_TEXT);
}

// ��ȫ��ѡ�񡱲˵�
void CBuddyChatDlg::OnMenu_SelAll(UINT uNotifyCode, int nID, CWindow wndCtl)
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

// ���������˵�
void CBuddyChatDlg::OnMenu_Clear(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_richRecv.SetWindowText(_T(""));
}

// ����ʾ�������˵�
void CBuddyChatDlg::OnMenu_ZoomRatio(UINT uNotifyCode, int nID, CWindow wndCtl)
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

// �����Ϊ���˵�
void CBuddyChatDlg::OnMenu_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl)
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
		lpszFilter = _T("ͼ���ļ�(*.jpg)\0*.jpg\0ͼ���ļ�(*.bmp)\0*.bmp\0\0");
	}
	else if (InlineIsEqualGUID(guid, Gdiplus::ImageFormatPNG))
	{
		lpszDefExt = _T(".png");
		lpszFilter = _T("ͼ���ļ�(*.png)\0*.png\0\0");
	}
	else if (InlineIsEqualGUID(guid, Gdiplus::ImageFormatGIF))
	{
		lpszDefExt = _T(".gif");
		lpszFilter = _T("ͼ���ļ�(*.gif)\0*.gif\0ͼ���ļ�(*.jpg)\0*.jpg\0ͼ���ļ�(*.bmp)\0*.bmp\0\0");
	}
	else
	{
		lpszDefExt = _T(".jpg");
		lpszFilter = _T("ͼ���ļ�(*.jpg)\0*.jpg\0ͼ���ļ�(*.bmp)\0*.bmp\0\0");
	}

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, strFileNamePrefix, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("����ͼƬ");
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

void CBuddyChatDlg::OnMenu_ExportMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	//������Ϣ��¼������Ϊword��txt�ļ�
	//����Ϊword���ں�������ͼƬ���鵼��
	tstring strText;
	RichEdit_GetText(m_richMsgLog.m_hWnd, strText);
	TCHAR	cFileName[MAX_PATH] = {0};
	BOOL	bOpenFileDialog = FALSE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = _T("δ����");
	LPCTSTR lpszFilter = _T("Microsoft Office(*.doc)\0*.doc\0Microsoft Office(*.docx)\0*.docx\0��ɽWPS(*.wps)\0*.wps\0\0");
	DWORD	dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXTENSIONDIFFERENT;
	HWND	hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	fileDlg.m_ofn.lpstrTitle = _T("�����ļ�");
	if (fileDlg.DoModal() == IDOK)
	{
		CString strSavePath = fileDlg.m_ofn.lpstrFile;
		CString strExtName = (_T(".") + Hootina::CPath::GetExtension(strSavePath)).c_str();
		CFile file;
		if(!file.Open(strSavePath, TRUE))
			return;

		char* pBuffer = new char[strText.size() / 2 + 1];
        EncodeUtil::UnicodeToAnsi(strText.c_str(), pBuffer, strlen(pBuffer) * 2);
		file.Write(pBuffer, strlen(pBuffer));
	}
}

void CBuddyChatDlg::OnMenu_FindInMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	//������Ϣ��¼
}

// ɾ��ѡ����Ϣ��¼
void CBuddyChatDlg::OnMenu_DeleteSelectMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(IDYES != ::MessageBox(m_hWnd, _T("ɾ������Ϣ��¼�޷��ָ���ȷʵҪɾ��ѡ�е���Ϣ��¼��"), _T("ɾ��ȷ��"), MB_YESNO|MB_ICONWARNING))
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

	//�жϼ���������ݸ�ʽ�Ƿ���Դ���
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
			//��ʾ�����
			::OutputDebugString(lpStr);
			//�ͷ����ڴ档
			::GlobalUnlock(hMem);
		}
	}
	::CloseClipboard();
	//��sql���ȥɾ��SQLite���ݿ��ж�Ӧ����Ϣ��¼

	//UINT nMsgLogID = m_MsgLogger.DelBuddyMsgLogByText(lpStr);
}

void CBuddyChatDlg::OnMenu_ClearMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(IDYES != ::MessageBox(m_hWnd, _T("ɾ������Ϣ��¼�޷��ָ���ȷʵҪɾ����ú��ѵ�������Ϣ��¼��"), _T("ɾ��ȷ��"), MB_YESNO|MB_ICONWARNING))
		return;
	
	m_richMsgLog.SetWindowText(_T(""));
	m_richRecv.SetWindowText(_T(""));
	m_staMsgLogPage.SetWindowText(_T("0/0"));
	//tstring strChatMsgDBPath = m_lpFMGClient->GetMsgLogFullName();
	//::DeleteFile(strChatMsgDBPath.c_str());
	//UINT nUTalkNum = m_lpFMGClient;
	m_MsgLogger.DelBuddyMsgLog(m_nUTalkUin);
}

//�����ļ�
void CBuddyChatDlg::OnMenu_SendFile(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	TCHAR	cFileName[MAX_PATH] = {0};
	BOOL	bOpenFileDialog = TRUE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = _T("ѡ���ļ�");
	LPCTSTR lpszFilter = _T("�����ļ�(*.)\0\0");
	DWORD	dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXTENSIONDIFFERENT;
	HWND	hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	if (fileDlg.DoModal() == IDOK)
	{
		CString strSavePath = fileDlg.m_ofn.lpstrFile;
		CString strExtName = (Hootina::CPath::GetExtension(strSavePath)).c_str();
		
		CString strFileTypeThumbs = Hootina::CPath::GetAppPath().c_str();
		strFileTypeThumbs += _T("\\Skins\\Skin0\\fileTypeThumbs\\");
		strFileTypeThumbs += strExtName;
		strFileTypeThumbs += _T(".png");
		m_SendFileThumbPicture.SetBitmap(strFileTypeThumbs);
		if (!m_bMsgLogWindowVisible)
		{
			::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
			m_bMsgLogWindowVisible = TRUE;
		}
		//::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
		m_RightTabCtrl.SetCurSel(1);
		m_richMsgLog.ShowWindow(SW_HIDE);
		ShowFileTransferCtrl(TRUE);
		m_staSendFileDesc.SetWindowText(Hootina::CPath::GetFileName(strSavePath).c_str());
	}
}

//�����������ļ����˵�
void CBuddyChatDlg::OnMenu_SendOfflineFile(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	TCHAR	cFileName[MAX_PATH] = {0};
	BOOL	bOpenFileDialog = TRUE;
	LPCTSTR lpszDefExt = NULL;
	LPCTSTR lpszFileName = _T("ѡ���ļ�");
	LPCTSTR lpszFilter = _T("�����ļ�(*.)\0\0");
	DWORD	dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXTENSIONDIFFERENT;
	HWND	hWndParent = m_hWnd;

	CFileDialog fileDlg(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent);
	if (fileDlg.DoModal() != IDOK)
		return;

	SendOfflineFile(fileDlg.m_szFileName);
}

//�������ļ��С��˵�
void CBuddyChatDlg::OnMenu_SendDirectory(UINT uNotifyCode, int nID, CWindow wndCtl)
{

}

//�������ļ����á��˵�
void CBuddyChatDlg::OnMenu_SendFileSettings(UINT uNotifyCode, int nID, CWindow wndCtl)
{

}

BOOL CBuddyChatDlg::SendOfflineFile(PCTSTR pszFileName)
{
	if(pszFileName==NULL)
		return FALSE;

	UINT64 nFileSize = IUGetFileSize2(pszFileName);
	//if(nFileSize > (UINT64)MAX_OFFLINE_FILE_SIZE)
	//{
	//	::MessageBox(m_hWnd, _T("�����ļ����Ҳ�֧�ִ�С����2G���ļ���"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
	//	return FALSE;
	//}

	CString strSavePath(pszFileName);
	CString strExtName(Hootina::CPath::GetExtension(strSavePath).c_str());

	CString strFileTypeThumbs(Hootina::CPath::GetAppPath().c_str());
	strFileTypeThumbs += _T("\\Skins\\Skin0\\fileTypeThumbs\\");
	strFileTypeThumbs += strExtName;
	strFileTypeThumbs += _T(".png");
	//m_SendFileThumbPicture.SetBitmap(strFileTypeThumbs);

	////���ݵ�ǰ�Ƿ���ж��Ƿ�����Ϣ
	//if (!m_bMsgLogWindowVisible)
	//{
	//	::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
	//	m_bMsgLogWindowVisible = TRUE;
	//}
	//::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
	//TODO: ���Ϊʲô���ڴ��ļ������ǲ��ܼ����ļ����Tab����������ļ����䰴ť�޷���Ӧ�����
	//if(m_bMsgLogWindowVisible)
		m_richMsgLog.ShowWindow(SW_HIDE);
	DisplayFileTransfer(TRUE);
	
	CString strImage;
	long nItemID = m_FileTransferCtrl.AddItem();
	tstring strFileName(Hootina::CPath::GetFileName(strSavePath));
	m_FileTransferCtrl.SetItemFileFullNameByID(nItemID, strSavePath);
	m_FileTransferCtrl.SetItemFileNameByID(nItemID, strFileName.c_str());
	if(!Hootina::CPath::IsFileExist(strFileTypeThumbs))
		strFileTypeThumbs.Format(_T("%sSkins\\Skin0\\fileTypeThumbs\\unknown.png"), g_szHomePath);
	m_FileTransferCtrl.SetItemFileTypePicByID(nItemID, strFileTypeThumbs);
	m_FileTransferCtrl.SetItemFileSizeByID(nItemID, Hootina::CPath::GetFileSize(pszFileName));
	m_FileTransferCtrl.SetItemTargetTypeByID(nItemID, SEND_TYPE);

	m_FileTransferCtrl.Invalidate(FALSE);

	m_mapSendFileInfo.insert(std::pair<CString, long>(pszFileName, -1));

	CFileItemRequest* pFileItemRequest = new CFileItemRequest();
	pFileItemRequest->m_nID = nItemID;
	_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), strSavePath);
	pFileItemRequest->m_hwndReflection = m_hWnd;
	pFileItemRequest->m_nFileType = FILE_ITEM_UPLOAD_CHAT_OFFLINE_FILE;
	//CreateEventʧ�ܻ᷵��NULL������INVALID_HANDLE_VALUE�����Կ�����ôд
	pFileItemRequest->m_hCancelEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);

	m_FileTransferCtrl.SetFileItemRequestByID(nItemID, pFileItemRequest);

	m_lpFMGClient->m_FileTask.AddItem(pFileItemRequest);

	return TRUE;
}

BOOL CBuddyChatDlg::RecvOfflineFile(PCTSTR lpszDownloadName, PCTSTR pszFileName, long nFileSize)
{
	if(pszFileName==NULL || nFileSize==NULL)
		return FALSE;

	CString strExtName(Hootina::CPath::GetExtension(pszFileName).c_str());
	CString strFileTypeThumbs(Hootina::CPath::GetAppPath().c_str());
	strFileTypeThumbs += _T("\\Skins\\Skin0\\fileTypeThumbs\\");
	strFileTypeThumbs += strExtName;
	strFileTypeThumbs += _T(".png");
	if(!Hootina::CPath::IsFileExist(strFileTypeThumbs))
		strFileTypeThumbs.Format(_T("%sSkins\\Skin0\\fileTypeThumbs\\unknown.png"), g_szHomePath);

	//���ݵ�ǰ�Ƿ���ж��Ƿ�����Ϣ
	//if (!m_bMsgLogWindowVisible)
	//{
	//	::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
	//	m_bMsgLogWindowVisible = TRUE;
	//}
	//::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
	if(!m_bMsgLogWindowVisible)
		m_richMsgLog.ShowWindow(SW_HIDE);
	DisplayFileTransfer(TRUE);
	
	CString strImage;
	long nItemID = m_FileTransferCtrl.AddItem();
	m_FileTransferCtrl.SetItemFileNameByID(nItemID, pszFileName);
	
	m_FileTransferCtrl.SetItemFileTypePicByID(nItemID, strFileTypeThumbs);
	m_FileTransferCtrl.SetItemFileSizeByID(nItemID, nFileSize);
	m_FileTransferCtrl.SetItemTargetTypeByID(nItemID, RECV_TYPE);
	char szDownloadName[MAX_PATH] = {0};
    EncodeUtil::UnicodeToUtf8(lpszDownloadName, szDownloadName, ARRAYSIZE(szDownloadName));
	m_FileTransferCtrl.SetItemDownloadNameByID(nItemID, szDownloadName);

	m_FileTransferCtrl.Invalidate(FALSE);

	return TRUE;
}

// ����/�����ı��������ƶ���Ϣ
BOOL CBuddyChatDlg::OnRichEdit_MouseMove(MSG* pMsg)
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

// ����/�����ı�������˫����Ϣ
BOOL CBuddyChatDlg::OnRichEdit_LBtnDblClk(MSG* pMsg)
{
	POINT pt = {GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam)};

	IImageOle* pImageOle = NULL;
	BOOL bRet = RichEdit_GetImageOle(pMsg->hwnd, pt, &pImageOle);
	if (bRet && pImageOle != NULL)					// ˫������ͼƬ
	{
		LONG nFaceId = -1, nFaceIndex = -1;
		pImageOle->GetFaceId(&nFaceId);
		pImageOle->GetFaceIndex(&nFaceIndex);
		if (-1 == nFaceId && -1 == nFaceIndex)		// ��ϵͳ����
		{
			BSTR bstrFileName = NULL;				// ��ȡͼƬ�ļ���
			HRESULT hr = pImageOle->GetFileName(&bstrFileName);
			if (SUCCEEDED(hr))						// ����ͼƬ����������ͼƬ
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

// ����/�����ı��������Ҽ�������Ϣ
BOOL CBuddyChatDlg::OnRichEdit_RBtnDown(MSG* pMsg)
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
	else if(pMsg->hwnd == m_richMsgLog.m_hWnd)
	{
		CSkinMenu menuPopup = m_SkinMenu.GetSubMenu(11);
		menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pMsg->pt.x, pMsg->pt.y, m_hWnd, NULL);
	}

	return FALSE;
}

// ��ȡ������Ϣָ��
CBuddyInfo* CBuddyChatDlg::GetBuddyInfoPtr()
{
	if (m_lpFMGClient != NULL)
	{
		CBuddyList* lpBuddyList = m_lpFMGClient->GetBuddyList();
		if (lpBuddyList != NULL)
			return lpBuddyList->GetBuddy(m_nUTalkUin);
	}
	return NULL;
}

// ��ȡ�û���Ϣָ��
CBuddyInfo* CBuddyChatDlg::GetUserInfoPtr()
{
	if (m_lpFMGClient != NULL)
		return m_lpFMGClient->GetUserInfo();
	else
		return NULL;
}

// ��������
void CBuddyChatDlg::UpdateData()
{
	CBuddyInfo* lpBuddyInfo = GetBuddyInfoPtr();
	if (lpBuddyInfo != NULL)
	{
		m_nUTalkNumber = lpBuddyInfo->m_uUserID;
		m_strBuddyName = lpBuddyInfo->m_strNickName.c_str();
		
		m_strBuddySign = lpBuddyInfo->m_strSign.c_str();
		
	}

	CBuddyInfo* lpUserInfo = GetUserInfoPtr();
	if (lpUserInfo != NULL)
	{
		/*m_nUserNumber = lpUserInfo->m_nUTalkNum;*/
		m_strUserName = lpUserInfo->m_strNickName.c_str();
	}
}

// ���¶Ի��������
void CBuddyChatDlg::UpdateDlgTitle()
{
	SetWindowText(m_strBuddyName);
}

// ���º������ƿؼ�
void CBuddyChatDlg::UpdateBuddyNameCtrl()
{
	CString strText;
	strText.Format(_T("%s(%s)"), m_strBuddyName, m_lpFMGClient->m_UserMgr.GetAccountName(m_nUTalkUin).c_str());
	m_lnkBuddyName.SetLabel(strText);
}

// ���º���ǩ���ؼ�
void CBuddyChatDlg::UpdateBuddySignCtrl()
{
	m_staBuddySign.SetWindowText(m_strBuddySign);
	//TODO: ��һ��tooltip
	//m_staBuddySign.SetTooltipText(m_strBuddySign);
}

// ��ʼ��Top������
BOOL CBuddyChatDlg::InitTopToolBar()
{
	int nIndex = 0;
	//int nIndex = m_tbTop.AddItem(101, STBI_STYLE_DROPDOWN);
	//m_tbTop.SetItemSize(nIndex, 52, 40, 42, 10);
	//m_tbTop.SetItemPadding(nIndex, 1);
	//m_tbTop.SetItemToolTipText(nIndex, _T("��ʼ��Ƶ�Ự"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
	//	_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	//m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	//m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\video.png"));

	//nIndex = m_tbTop.AddItem(102, STBI_STYLE_DROPDOWN);
	//m_tbTop.SetItemSize(nIndex, 52, 40, 42, 10);
	//m_tbTop.SetItemPadding(nIndex, 1);
	//m_tbTop.SetItemToolTipText(nIndex, _T("��ʼ�����Ự"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
	//	_T("aio_toolbar_leftdown.png"), CRect(0,0,0,0));
	//m_tbTop.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,0,0));
	//m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\audio.png"));

	nIndex = m_tbTop.AddItem(103, STBI_STYLE_DROPDOWN);
	m_tbTop.SetItemSize(nIndex, 38, 28, 28, 10);
	m_tbTop.SetItemPadding(nIndex, 1);
	m_tbTop.SetItemToolTipText(nIndex, _T("�����ļ�"));
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
	m_tbTop.SetItemToolTipText(nIndex, _T("Զ������"));
	m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\remote_desktop.png"));

	//nIndex = m_tbTop.AddItem(105, STBI_STYLE_BUTTON);
	//m_tbTop.SetItemSize(nIndex, 36, 40);
	//m_tbTop.SetItemPadding(nIndex, 2);
	//m_tbTop.SetItemToolTipText(nIndex, _T("����������"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\create_disc_group.png"));

	//nIndex = m_tbTop.AddItem(106, STBI_STYLE_WHOLEDROPDOWN);
	//m_tbTop.SetItemSize(nIndex, 44, 40, 34, 10);
	//m_tbTop.SetItemPadding(nIndex, 2);
	//m_tbTop.SetItemToolTipText(nIndex, _T("�ٱ�"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\report.png"));

	//nIndex = m_tbTop.AddItem(107, STBI_STYLE_WHOLEDROPDOWN);
	//m_tbTop.SetItemSize(nIndex, 44, 40, 34, 10);
	//m_tbTop.SetItemPadding(nIndex, 2);
	//m_tbTop.SetItemToolTipText(nIndex, _T("Ӧ��"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\app.png"));

	//nIndex = m_tbTop.AddItem(108, STBI_STYLE_BUTTON);
	//m_tbTop.SetItemSize(nIndex, 36, 40);
	//m_tbTop.SetItemPadding(nIndex, 2);
	//m_tbTop.SetItemToolTipText(nIndex, _T("Զ��Э��"));
	//m_tbTop.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbTop.SetItemIconPic(nIndex, _T("BuddyTopToolBar\\remote_assistance.png"));

	m_tbTop.SetLeftTop(2, 2);
	m_tbTop.SetTransparent(TRUE, m_SkinDlg.GetBgDC());
	//m_tbTop.SetBgPic(_T("BuddyTopToolBar\\buddyChatDlg_tbTopBg.png"), CRect(0, 0, 0, 0));

	CRect rcTopToolBar(3, 70, CHATDLG_WIDTH-3, 102);
	m_tbTop.Create(m_hWnd, rcTopToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_TOP);
	
	return TRUE;
}

// ��ʼ��Middle������
BOOL CBuddyChatDlg::InitMidToolBar()
{
	int nIndex = m_tbMid.AddItem(201, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("����ѡ�񹤾���"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_font.png"));

	nIndex = m_tbMid.AddItem(202, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("ѡ�����"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_face.png"));

	//nIndex = m_tbMid.AddItem(203, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("��Աħ������/��������/Ϳѻ����/������"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_richface.png"));

	nIndex = m_tbMid.AddItem(204, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("����ѷ��ʹ��ڶ���"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_twitter.png"));

	//nIndex = m_tbMid.AddItem(205, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("ѡ��һ�±���"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_flirtationface.png"));

	//nIndex = m_tbMid.AddItem(206, STBI_STYLE_BUTTON|STBI_STYLE_CHECK);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 2);
	//m_tbMid.SetItemToolTipText(nIndex, _T("�๦�ܸ�������"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\UTalkIme.png"));

	//nIndex = m_tbMid.AddItem(207, STBI_STYLE_SEPARTOR);
	//m_tbMid.SetItemSize(nIndex, 4, 27);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemSepartorPic(nIndex, _T("aio_qzonecutline_normal.png"));

	nIndex = m_tbMid.AddItem(208, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("����ͼƬ"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_sendpic.png"));


	//nIndex = m_tbMid.AddItem(209, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("���"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\musicsharebtn20.png"));

	//nIndex = m_tbMid.AddItem(210, STBI_STYLE_BUTTON);
	//m_tbMid.SetItemSize(nIndex, 24, 20);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemToolTipText(nIndex, _T("�������ͷ�����ͷݾ�ϲ"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\present.png"));

	nIndex = m_tbMid.AddItem(211, STBI_STYLE_BUTTON);
	m_tbMid.SetItemSize(nIndex, 30, 27, 27, 0);
	//m_tbMid.SetItemSize(nIndex, 30, 27);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemToolTipText(nIndex, _T("��Ļ��ͼ"));
	m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
		_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
	//	_T("aio_toolbar_leftdown.png"), CRect(1,0,0,0));
	//m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	//m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_cut.png"));

	//nIndex = m_tbMid.AddItem(212, STBI_STYLE_DROPDOWN);
	//m_tbMid.SetItemSize(nIndex, 31, 20, 23, 8);
	//m_tbMid.SetItemPadding(nIndex, 2);
	//m_tbMid.SetItemToolTipText(nIndex, _T("��������"));
	//m_tbMid.SetItemBgPic(nIndex, NULL, _T("aio_toolbar_highligh.png"), 
	//	_T("aio_toolbar_down.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemLeftBgPic(nIndex, _T("aio_toolbar_leftnormal.png"), 
	//	_T("aio_toolbar_leftdown.png"), CRect(1,0,0,0));
	//m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	//m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	//m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\SoSo.png"));

	//nIndex = m_tbMid.AddItem(213, STBI_STYLE_SEPARTOR);
	//m_tbMid.SetItemSize(nIndex, 4, 27);
	//m_tbMid.SetItemPadding(nIndex, 1);
	//m_tbMid.SetItemSepartorPic(nIndex, _T("aio_qzonecutline_normal.png"));

	nIndex = m_tbMid.AddItem(214, STBI_STYLE_BUTTON);
	m_nMsgLogIndexInToolbar = nIndex;
	m_tbMid.SetItemSize(nIndex, 90, 27, 27, 0);
	m_tbMid.SetItemPadding(nIndex, 1);
	m_tbMid.SetItemMargin(nIndex, CHATDLG_WIDTH-235, 0);
	m_tbMid.SetItemText(nIndex, _T(">>"));
	m_tbMid.SetItemToolTipText(nIndex, _T("����鿴��Ϣ��¼"));
	m_tbMid.SetItemIconPic(nIndex, _T("MidToolBar\\aio_quickbar_msglog.png"));
	//m_tbMid.SetItemLeftBgPic(nIndex, _T("MidToolBar\\aio_quickbar_msglog.png"), _T("MidToolBar\\aio_quickbar_msglog.png"), CRect(3,3,3,3));
	//m_tbMid.SetItemLeftBgPic(nIndex, _T("Button\\btn_msglog_down.png"), _T("Button\\btn_msglog_down.png"), CRect(1,0,0,0));
	//m_tbMid.SetItemRightBgPic(nIndex, _T("aio_toolbar_rightnormal.png"), 
	//	_T("aio_toolbar_rightdown.png"), CRect(0,0,1,0));
	//m_tbMid.SetItemArrowPic(nIndex, _T("aio_littletoolbar_arrow.png"));
	

	m_tbMid.SetLeftTop(2, 2);
	m_tbMid.SetBgPic(_T("MidToolBar\\bg.png"), CRect(0,0,0,0));

	CRect rcMidToolBar(2, CHATDLG_HEIGHT-168, CHATDLG_WIDTH-2, CHATDLG_HEIGHT-136);
	m_tbMid.Create(m_hWnd, rcMidToolBar, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_TOOLBAR_MID);

	return TRUE;
}

// ��ʼ��IRichEditOleCallback�ӿ�
BOOL CBuddyChatDlg::InitRichEditOleCallback()
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


// ��ʼ��Tab��
BOOL CBuddyChatDlg::InitRightTabWindow()
{
	CRect rcRightTabCtrl(CHATDLG_WIDTH, 70, CHATDLG_WIDTH-3, 102);
	//m_RightTabCtrl.Create(m_hWnd, rcRightTabCtrl, NULL, WS_CHILD | WS_VISIBLE, NULL, ID_TABCTRL_CHAT, NULL);
	//m_RightTabCtrl.SetTransparent(TRUE, m_SkinDlg.GetBgDC());
	//m_RightTabCtrl.ShowWindow(SW_HIDE);


	//long nWidth = 80;
	//long nWidthClose = 21;

	//int nIndex = m_RightTabCtrl.AddItem(301, STCI_STYLE_DROPDOWN);
	//m_RightTabCtrl.SetItemSize(nIndex, nWidth, 21, nWidth - nWidthClose, 20);
	//m_RightTabCtrl.SetItemText(nIndex, _T("��Ϣ��¼"));
	//m_RightTabCtrl.SetCurSel(0);

	//nIndex = m_RightTabCtrl.AddItem(302, STCI_STYLE_DROPDOWN);
	//m_RightTabCtrl.SetItemSize(nIndex, nWidth, 21, nWidth - nWidthClose, 20);
	//m_RightTabCtrl.SetItemText(nIndex, _T("�����ļ�"));
	//m_RightTabCtrl.SetItemVisible(nIndex, FALSE);

	// ��Ϣ��¼���ı��Ի���
	CRect rcMsgLog;
	rcMsgLog.left = rcRightTabCtrl.left;
	rcMsgLog.top = rcRightTabCtrl.top + 30;
	rcMsgLog.right = rcRightTabCtrl.right;
	rcMsgLog.bottom = rcRightTabCtrl.bottom+300;
	DWORD dwStyle = WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
	m_richMsgLog.Create(m_hWnd, rcMsgLog, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_MSGLOG);
	m_richMsgLog.SetTransparent(FALSE, m_SkinDlg.GetBgDC());
	m_richMsgLog.SetAutoURLDetect();
	m_richMsgLog.SetReadOnly();
	//m_richMsgLog.ShowWindow(SW_HIDE);
	
	CRect rcTabMgr(CHATDLG_WIDTH-1, 69, CHATDLG_WIDTH+RIGHT_CHAT_WINDOW_WIDTH-2, 101);
	m_TabMgr.Create(m_hWnd, rcTabMgr, NULL, WS_CHILD | WS_VISIBLE, NULL, ID_CHAT_TAB_MGR, NULL);
	m_TabMgr.ShowWindow(SW_HIDE);

	return TRUE;
}

void CBuddyChatDlg::ShowMsgLogButtons(BOOL bShow)
{
	DWORD dwFlag = (bShow ? SW_SHOW : SW_HIDE); 
	
	m_btnFirstMsgLog.ShowWindow(dwFlag);
	m_btnPrevMsgLog.ShowWindow(dwFlag);
	m_staMsgLogPage.ShowWindow(dwFlag);
	m_btnNextMsgLog.ShowWindow(dwFlag);
	m_btnLastMsgLog.ShowWindow(dwFlag);
}

void CBuddyChatDlg::InitFileTransferCtrl()
{
	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	CRect rcTabMgr;
	m_TabMgr.GetClientRect(&rcTabMgr);

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcFileTransferCtrl(rcTabMgr.left, rcTabMgr.bottom, rcTabMgr.right, rcClient.bottom);
	m_FileTransferCtrl.Create(m_hWnd, rcFileTransferCtrl, NULL, WS_CHILD|WS_VISIBLE, NULL, ID_FILE_TRANSFER);
	m_FileTransferCtrl.SetTransparent(TRUE, hDlgBgDC);
	
	
	ShowFileTransferCtrl(FALSE);
}

void CBuddyChatDlg::ShowFileTransferCtrl(BOOL bShow)
{
	DWORD dwFlag = (bShow ? SW_SHOW : SW_HIDE);

	if(bShow)
	{
		CRect rcTabMgr;
		m_TabMgr.GetClientRect(&rcTabMgr);

		CRect rcClient;
		GetClientRect(&rcClient);

		CRect rcFileTransferCtrl(rcClient.right-rcTabMgr.Width()-6, 100, rcClient.right-2, rcClient.bottom-32);
		m_FileTransferCtrl.MoveWindow(&rcFileTransferCtrl, FALSE); 
	}

	m_FileTransferCtrl.ShowWindow(dwFlag);
	m_bFileTransferVisible = bShow;
	//m_RightTabCtrl.SetItemVisible(1, bShow);
}

void CBuddyChatDlg::DestroyFileTransferCtrl()
{
	if(m_SendFileThumbPicture.IsWindow())
		m_SendFileThumbPicture.DestroyWindow();

	if(m_staSendFileDesc.IsWindow())
		m_staSendFileDesc.DestroyWindow();

	if(m_staSendFileDesc.IsWindow())
		m_staSendFileDesc.DestroyWindow();

	if(m_ProgressSendFile.IsWindow())
		m_ProgressSendFile.DestroyWindow();

	if(m_lnkSendOffline.IsWindow())
		m_lnkSendOffline.DestroyWindow();

	if(m_lnkSendFileCancel.IsWindow())
		m_lnkSendFileCancel.DestroyWindow();
}

LRESULT CBuddyChatDlg::OnTabCtrlDropDown(LPNMHDR pnmh)
{
	int nCurSel = m_RightTabCtrl.GetCurSel();
	//switch (nCurSel)
	//{
	//case 0:
	//	::SendMessage(m_btnMsgLog.m_hWnd, BM_CLICK, 0, 0);
	//	InvalidateRect(NULL, TRUE);
	//	break;

	//case 1:
	//	m_richMsgLog.ShowWindow(SW_HIDE);
	//	ShowFileTransferCtrl(TRUE);
	//	break;

	//default:
	//	break;
	//}

	return 1;
}

LRESULT CBuddyChatDlg::OnTabCtrlSelChange(LPNMHDR pnmh)
{
	int nCurSel = m_RightTabCtrl.GetCurSel();
	switch (nCurSel)
	{
	case 0:
		ShowMsgLogButtons(TRUE);
		m_richMsgLog.ShowWindow(SW_SHOW);
		m_richMsgLog.SetFocus();
		ShowFileTransferCtrl(FALSE);
		OpenMsgLogBrowser();
		m_bFileTransferVisible = FALSE;
		break;

	case 1:
		ShowMsgLogButtons(FALSE);
		ShowFileTransferCtrl(TRUE);
		m_richMsgLog.ShowWindow(SW_HIDE);
		break;

	default:
		break;
	}

	return 1;
}

LRESULT CBuddyChatDlg::OnClickTabMgr(LPNMHDR pnmh)
{
	TWI_HEADER* pTwi = (TWI_HEADER*)pnmh;
	if(pTwi==NULL || pTwi->nmhdr.hwndFrom!=m_TabMgr.m_hWnd)
		return 0;

	if(pTwi->hwndItem == m_richMsgLog.m_hWnd)
	{
		//������Ϣ��¼����
		if(pTwi->nClickType == CLICK_TYPE_ACTIVATE)
		{
			ShowMsgLogButtons(TRUE);
			m_richMsgLog.ShowWindow(SW_SHOW);
			m_richMsgLog.SetFocus();
			ShowFileTransferCtrl(FALSE);
			OpenMsgLogBrowser();
			m_bFileTransferVisible = FALSE;
		}
		//�ر���Ϣ��¼����
		else if(pTwi->nClickType == CLICK_TYPE_CLOSE)
		{
			//m_TabMgr.RemoveItem(m_richMsgLog.m_hWnd);
			//m_richMsgLog.ShowWindow(SW_HIDE);
			//ShowMsgLogButtons(FALSE);
			PostMessage(WM_COMMAND, 214, 0);
		}
	}
	else if(pTwi->hwndItem == m_FileTransferCtrl.m_hWnd)
	{
		//�����ļ����䴰��
		if(pTwi->nClickType == CLICK_TYPE_ACTIVATE)
		{
			ShowMsgLogButtons(FALSE);
			ShowFileTransferCtrl(TRUE);
			m_richMsgLog.ShowWindow(SW_HIDE);
		}
		//�ر��ļ����䴰��
		else if(pTwi->nClickType == CLICK_TYPE_CLOSE)
		{
			m_TabMgr.RemoveItem(m_FileTransferCtrl.m_hWnd);
		}
	}

	return 1;
}

LRESULT CBuddyChatDlg::OnBtn_FileTransfer(LPNMHDR pnmh)
{
	PFILE_TRANSFER_NMHDREX pNMHDREx = (PFILE_TRANSFER_NMHDREX)pnmh;
	if(pNMHDREx== NULL || pNMHDREx->btnArea==BTN_NONE || pNMHDREx->nID<0)
		return 0;

	CString strFileName( m_FileTransferCtrl.GetItemFileNameByID(pNMHDREx->nID));
	if(strFileName == "")
	{
		m_FileTransferCtrl.RemoveItemByID(pNMHDREx->nID);
		return 0;
	}
	
	CString strFileExtension(Hootina::CPath::GetExtension(strFileName).c_str());
	if(strFileExtension == "")
		strFileExtension = _T("*");

	if(pNMHDREx->btnArea == BTN_CANCEL)
	{
		CFileItemRequest* pItemRequest = m_FileTransferCtrl.GetFileItemRequestByID(pNMHDREx->nID);
        //TODO: �����ļ�����û��ʼ���ص��ļ����������ΪNULL
        if (pItemRequest != NULL)
        {
            m_lpFMGClient->m_FileTask.RemoveItem(pItemRequest);
        }
		
		if(pNMHDREx->nTargetType == RECV_TYPE)
		{
			CString strRecvFileResultMsgText;
			strRecvFileResultMsgText.Format(_T("%sȡ���˽����ļ�[%s]��"), m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str(), strFileName);
            m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), time(NULL), strRecvFileResultMsgText.GetString(), m_hWnd);
			
			CString strInfo;
			strInfo.Format(_T("                                            ����ȡ���˽����ļ�[%s]����\r\n"), strFileName);
			RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
		}
		else if(pNMHDREx->nTargetType == SEND_TYPE)
		{
			CString strInfo;
			strInfo.Format(_T("                                            ����ȡ���˷����ļ�[%s]����\r\n"), strFileName);
			RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
		}
		
		m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
		m_FileTransferCtrl.RemoveItemByID(pNMHDREx->nID);
		DisplayFileTransfer(FALSE);	
		return 1;
	}
	
	CStringA strDownloadName(m_FileTransferCtrl.GetItemDownloadNameByID(pNMHDREx->nID));
	CFileItemRequest* pFileItemRequest = NULL;
	pFileItemRequest = new CFileItemRequest();
	pFileItemRequest->m_hCancelEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if(pNMHDREx->btnArea == BTN_ACCEPT)
	{
		CString strDefaultPath(m_lpFMGClient->m_UserMgr.GetDefaultRecvFilePath().c_str());
		if(strDefaultPath == "")
		{
			::MessageBox(m_hWnd, _T("�޷������ļ����ҵ��ĵ�����ʹ��[���Ϊ]��ť����ָ���ļ�����·����"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR);
			delete pFileItemRequest;
			return 1;
		}
		CString strFilePath;
		strFilePath.Format(_T("%s\\%s"), strDefaultPath, strFileName);
		if(Hootina::CPath::IsFileExist(strFilePath))
		{
			CString strInfo;
			strInfo.Format(_T("[%s]�ļ��Ѵ��ڣ��Ƿ񸲸ǣ�"), strFilePath);
			if(IDNO == ::MessageBox(m_hWnd, strInfo, g_strAppTitle.c_str(), MB_YESNO|MB_ICONQUESTION))
			{
				delete pFileItemRequest;
				return 1;
			}
		}
		_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), strFilePath);	
		strcpy_s(pFileItemRequest->m_szUtfFilePath, ARRAYSIZE(pFileItemRequest->m_szUtfFilePath), strDownloadName);
	}
	else if(pNMHDREx->btnArea == BTN_SAVEAS)
	{
		DWORD dwFlags = OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_EXTENSIONDIFFERENT;
		CFileDialog fileDlg(FALSE, NULL, strFileName, dwFlags, _T("��������(*.*)\0*.*\0\0"), m_hWnd);
		fileDlg.m_ofn.lpstrTitle = _T("���Ϊ");
		if (fileDlg.DoModal() != IDOK)
		{
			delete pFileItemRequest;
			return 1;
		}
		_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), fileDlg.m_ofn.lpstrFile);
		strcpy_s(pFileItemRequest->m_szUtfFilePath, ARRAYSIZE(pFileItemRequest->m_szUtfFilePath), strDownloadName);
	}

	m_FileTransferCtrl.SetItemSaveNameByID(pNMHDREx->nID, pFileItemRequest->m_szFilePath);
	m_FileTransferCtrl.SetFileItemRequestByID(pNMHDREx->nID, pFileItemRequest);

	pFileItemRequest->m_nID = pNMHDREx->nID;
	pFileItemRequest->m_hwndReflection = m_hWnd;
	pFileItemRequest->m_nFileType = FILE_ITEM_DOWNLOAD_CHAT_OFFLINE_FILE;

	m_lpFMGClient->m_FileTask.AddItem(pFileItemRequest);

	m_mapRecvFileInfo.insert(std::pair<CString, long>(pFileItemRequest->m_szFilePath, -1));

	m_FileTransferCtrl.SetAcceptButtonVisibleByID(pNMHDREx->nID, FALSE);
	m_FileTransferCtrl.SetSaveAsButtonVisibleByID(pNMHDREx->nID, FALSE);

	m_FileTransferCtrl.Invalidate(FALSE);
	
	return 1;
}

// ��ʼ���ؼ�
BOOL CBuddyChatDlg::Init()
{
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetBgPic(CHAT_BG_IMAGE_NAME, CRect(CHATDLG_LEFT_FIXED_WIDTH, CHATDLG_TOP_FIXED_HEIGHT, CHATDLG_RIGHT_FIXED_WIDTH, CHATDLG_BOTTOM_FIXED_HEIGHT));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), _T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetMaxSysBtnPic(_T("SysBtn\\btn_max_normal.png"), _T("SysBtn\\btn_max_highlight.png"), _T("SysBtn\\btn_max_down.png"));
	m_SkinDlg.SetRestoreSysBtnPic(_T("SysBtn\\btn_restore_normal.png"), _T("SysBtn\\btn_restore_highlight.png"), _T("SysBtn\\btn_restore_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	
	m_picHead.SubclassWindow(GetDlgItem(ID_PIC_HEAD));
	m_picHead.SetTransparent(TRUE, hDlgBgDC);
	m_picHead.SetShowCursor(TRUE);
	m_picHead.SetBgPic(_T("HeadCtrl\\Padding4Normal.png"), _T("HeadCtrl\\Padding4Hot.png"), _T("HeadCtrl\\Padding4Hot.png"));
	m_picHead.MoveWindow(10, 10, 54, 54, FALSE);
	
	tstring strNickName(m_lpFMGClient->m_UserMgr.GetNickName(m_nUTalkUin));
	CString strTooltip;
	strTooltip.Format(_T("����鿴%s������"), strNickName.c_str());
	m_picHead.SetToolTipText(strTooltip);

	m_lnkBuddyName.SubclassWindow(GetDlgItem(ID_LINK_BUDDYNAME));
	m_lnkBuddyName.SetTransparent(TRUE, hDlgBgDC);
	m_lnkBuddyName.SetLinkColor(RGB(0,0,0));
	m_lnkBuddyName.SetHoverLinkColor(RGB(0,0,0));
	m_lnkBuddyName.SetVisitedLinkColor(RGB(0,0,0));
	m_lnkBuddyName.MoveWindow(70, 12, 60, 14, FALSE);

	HFONT hFontBuddyNameLink = CGDIFactory::GetFont(22);
	m_lnkBuddyName.SetNormalFont(hFontBuddyNameLink);
	
	m_staBuddySign.SubclassWindow(GetDlgItem(ID_STATIC_BUDDYSIGN));
	m_staBuddySign.SetTransparent(TRUE, hDlgBgDC);
	m_staBuddySign.MoveWindow(70, 38, CHATDLG_WIDTH-50, 20, FALSE);

	HFONT hFontBuddySignature = CGDIFactory::GetFont(19);
	m_staBuddySign.SetFont(hFontBuddySignature);

	//ͼƬ�ϴ�������Ϣ�ı�
	m_staPicUploadProgress.SubclassWindow(GetDlgItem(IDC_STATIC_PICPROGRESS));
	m_staPicUploadProgress.SetTransparent(TRUE, hDlgBgDC);
	m_staPicUploadProgress.MoveWindow(10, CHATDLG_HEIGHT-25, 380, 25, FALSE);
	m_staPicUploadProgress.ShowWindow(SW_HIDE);

	m_btnClose.SubclassWindow(GetDlgItem(ID_BTN_CLOSE));
	m_btnClose.SetTransparent(TRUE, hDlgBgDC);
	m_btnClose.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnClose.SetBgPic(_T("Button\\btn_close_normal.png"), _T("Button\\btn_close_highlight.png"),_T("Button\\btn_close_down.png"), _T("Button\\btn_close_focus.png"));
	m_btnClose.MoveWindow(CHATDLG_WIDTH-190, CHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnSend.SubclassWindow(GetDlgItem(ID_BTN_SEND));
	m_btnSend.SetTransparent(TRUE, hDlgBgDC);
	m_btnSend.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnSend.SetTextColor(RGB(255, 255, 255));
	m_btnSend.SetBgPic(_T("Button\\btn_send_normal.png"), _T("Button\\btn_send_highlight.png"),_T("Button\\btn_send_down.png"), _T("Button\\btn_send_focus.png"));
	m_btnSend.MoveWindow(CHATDLG_WIDTH-110, CHATDLG_HEIGHT-30, 77, 25, FALSE);

	m_btnArrow.SubclassWindow(GetDlgItem(ID_BTN_ARROW));
	m_btnArrow.SetTransparent(TRUE, hDlgBgDC);
	m_btnArrow.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnArrow.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnArrow.MoveWindow(CHATDLG_WIDTH-33, CHATDLG_HEIGHT-30, 28, 25, FALSE);

	//��Ϣ��¼���ĸ���ť
	m_btnFirstMsgLog.SubclassWindow(GetDlgItem(IDC_FIRSTMSGLOG));
	m_btnFirstMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnFirstMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnFirstMsgLog.SetToolTipText(_T("��һҳ"));
	//m_btnFirstMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnFirstMsgLog.MoveWindow(CHATDLG_WIDTH+110, CHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_btnPrevMsgLog.SubclassWindow(GetDlgItem(IDC_PREVMSGLOG));
	m_btnPrevMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnPrevMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnPrevMsgLog.SetToolTipText(_T("��һҳ"));
	//m_btnPrevMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnPrevMsgLog.MoveWindow(CHATDLG_WIDTH+140, CHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_staMsgLogPage.SubclassWindow(GetDlgItem(IDC_STATIC_MSGLOGPAGE));
	m_staMsgLogPage.SetTransparent(TRUE, hDlgBgDC);
	m_staMsgLogPage.MoveWindow(CHATDLG_WIDTH+170, CHATDLG_HEIGHT-24, 60, 25, FALSE);

	m_btnNextMsgLog.SubclassWindow(GetDlgItem(IDC_NEXTMSGLOG));
	m_btnNextMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnNextMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnNextMsgLog.SetToolTipText(_T("��һҳ"));
	//m_btnNextMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnNextMsgLog.MoveWindow(CHATDLG_WIDTH+240, CHATDLG_HEIGHT-30, 28, 25, FALSE);

	m_btnLastMsgLog.SubclassWindow(GetDlgItem(IDC_LASTMSGLOG));
	m_btnLastMsgLog.SetTransparent(TRUE, hDlgBgDC);
	m_btnLastMsgLog.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnLastMsgLog.SetToolTipText(_T("���ҳ"));
	//m_btnLastMsgLog.SetBgPic(_T("Button\\btnright_normal.png"), _T("Button\\btnright_highlight.png"),_T("Button\\btnright_down.png"), _T("Button\\btnright_fouce.png"));
	m_btnLastMsgLog.MoveWindow(CHATDLG_WIDTH+270, CHATDLG_HEIGHT-30, 28, 25, FALSE);

	ShowMsgLogButtons(FALSE);

	m_SkinMenu.LoadMenu(ID_MENU_BUDDYCHAT);
	m_SkinMenu.SetBgPic(_T("Menu\\menu_left_bg.png"), _T("Menu\\menu_right_bg.png"));
	m_SkinMenu.SetSelectedPic(_T("Menu\\menu_selected.png"));
	m_SkinMenu.SetSepartorPic(_T("Menu\\menu_separtor.png"));
	m_SkinMenu.SetArrowPic(_T("Menu\\menu_arrow.png"));
	m_SkinMenu.SetCheckPic(_T("Menu\\menu_check.png"));
	m_SkinMenu.SetTextColor(RGB(0, 20, 35));
	m_SkinMenu.SetSelTextColor(RGB(254, 254, 254));
	
	InitTopToolBar();				// ��ʼ��Top������
	InitMidToolBar();				// ��ʼ��Middle������
	m_PicBarDlg.Create(m_hWnd);		// ����ͼƬ����������

	// ������Ϣ���ı���ؼ�
	CRect rcRecv(6, 107, 583, 430);
	DWORD dwStyle = WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
    m_richRecv.Create(m_hWnd, rcRecv, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_RECV);
	m_richRecv.SetTransparent(TRUE, hDlgBgDC);	
	DWORD dwMask = m_richRecv.GetEventMask();
	dwMask = dwMask | ENM_LINK  | ENM_MOUSEEVENTS | ENM_SCROLLEVENTS | ENM_KEYEVENTS;
	m_richRecv.SetEventMask(dwMask);
	m_richRecv.SetAutoURLDetect();
	m_richRecv.SetReadOnly();

	CRect rcSend(6, 405, 603, 495);
    m_richSend.Create(m_hWnd, rcSend, NULL, dwStyle, WS_EX_TRANSPARENT, ID_RICHEDIT_SEND);
	m_richSend.SetTransparent(TRUE, hDlgBgDC);

	//����richedit��midToolbar֮��ķָ���
	CRect rcSplitter(6, 400, 603, 405);
	m_SplitterCtrl.Create(m_hWnd, rcSplitter, NULL, WS_CHILD|WS_VISIBLE, 0, ID_SPLITTER_CTRL);

	if(!m_FontSelDlg.IsWindow())
	{
		m_FontSelDlg.Create(m_hWnd);
		m_FontSelDlg.ShowWindow(SW_HIDE);
	}
	
	InitRightTabWindow();


	//��ʼ�����ļ�������صĿؼ�
	InitFileTransferCtrl();
	ShowFileTransferCtrl(FALSE);
	
	// ������Ϣ���ı���ؼ�
	//CFontInfo fontInfo = m_FontSelDlg.GetPublicFontInfo();
	CFontInfo fontInfo;
	std::vector<tstring> arrSysFont;
	EnumSysFont(&arrSysFont);
	long nCustomFontNameIndex = -1;
	if(arrSysFont.empty())
	{
		::MessageBox(m_hWnd, _T("��ʼ������Ի���ʧ�ܣ�"), g_strAppTitle.c_str(), MB_OK|MB_ICONERROR);
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
			strCustomFontName = _T("΢���ź�");
	}
	else
		strCustomFontName = _T("΢���ź�");

	
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



	UpdateDlgTitle();
	UpdateBuddyNameCtrl();
	UpdateBuddySignCtrl();
	OnUpdateBuddyHeadPic();

	m_Accelerator.LoadAccelerators(ID_ACCE_CHATDLG);

	InitRichEditOleCallback();	// ��ʼ��IRichEditOleCallback�ӿ�

	
	return TRUE;
}


void CBuddyChatDlg::SetHotRgn()
{
	RECT rtWindow;
	HRGN hTemp;
	
	CRect rtClient;
	GetClientRect(&rtClient);
	rtWindow.left = 0;
	rtWindow.top = 0;
	rtWindow.right = rtClient.right;
	rtWindow.bottom = 106;
	m_HotRgn = ::CreateRectRgnIndirect(&rtWindow);

	m_picHead.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_DIFF);
	::DeleteObject(hTemp);

	m_lnkBuddyName.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_DIFF);
	::DeleteObject(hTemp);

	m_staBuddySign.GetClientRect(&rtWindow);
	hTemp = ::CreateRectRgnIndirect(&rtWindow);
	::CombineRgn(m_HotRgn, m_HotRgn, hTemp, RGN_DIFF);
	::DeleteObject(hTemp);
	
	m_SkinDlg.SetDragRegion(m_HotRgn);
}

//void CBuddyChatDlg::CalcTitleBarRect()
//{
//	CRect rcClient;
//	GetClientRect(&rcClient);
//
//	m_rcTitleBar = CRect(0, 0, rcClient.Width(), 27);
//
//	if (m_bHasCloseBtn)
//	{
//		m_rcCloseBtn = CRect(rcClient.Width() - 39, 0, rcClient.Width(), 20);
//
//		if (m_bHasMaxBtn)
//		{
//			m_rcMaxBtn = CRect(rcClient.Width() - 39 - 28, 0, rcClient.Width() - 39, 20);
//		}
//	}
//
//	if (m_bHasMinBtn)
//	{
//		if (!m_bHasMaxBtn)
//		{
//			m_rcMinBtn = CRect(rcClient.Width() - 39 - 28, 0, rcClient.Width() - 39, 20);
//		}
//		else
//		{
//			m_rcMinBtn = CRect(rcClient.Width() - 39 - 28 - 28, 0, rcClient.Width() - 39 - 28, 20);
//		}
//	}
//}

// ����ʼ���ؼ�
BOOL CBuddyChatDlg::UnInit()
{
	if (m_PicBarDlg.IsWindow())
		m_PicBarDlg.DestroyWindow();

	if (m_picHead.IsWindow())
		m_picHead.DestroyWindow();

	if (m_lnkBuddyName.IsWindow())
		m_lnkBuddyName.DestroyWindow();

	if (m_staBuddySign.IsWindow())
		m_staBuddySign.DestroyWindow();

	//if (m_picAD_1.IsWindow())
	//	m_picAD_1.DestroyWindow();

	//if (m_picAD_2.IsWindow())
	//	m_picAD_2.DestroyWindow();

	//if (m_picAD_3.IsWindow())
	//	m_picAD_3.DestroyWindow();

	if (m_btnMsgLog.IsWindow())
	{
		m_btnMsgLog.DestroyWindow();
	}

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

	if (m_richMsgLog.IsWindow())
		m_richMsgLog.DestroyWindow();

	m_Accelerator.DestroyObject();
	//	m_menuRichEdit.DestroyMenu();

	DestroyFileTransferCtrl();

	return TRUE;
}

void CBuddyChatDlg::_RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText)
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

BOOL CBuddyChatDlg::_RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId, int nFaceIndex)
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

BOOL CBuddyChatDlg::HandleSysFaceId(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

BOOL CBuddyChatDlg::HandleSysFaceIndex(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

BOOL CBuddyChatDlg::HandleCustomPic(HWND hRichEditWnd, LPCTSTR& p, CString& strText)
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

// "/f["ϵͳ����id"]/s["ϵͳ����index"]/c["�Զ���ͼƬ·��"]"
void CBuddyChatDlg::AddMsg(HWND hRichEditWnd, LPCTSTR lpText)
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

void CBuddyChatDlg::AddMsgToSendEdit(LPCTSTR lpText)
{
	AddMsg(m_richSend.m_hWnd, lpText);
	m_richSend.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CBuddyChatDlg::AddMsgToRecvEdit(time_t nTime, LPCTSTR lpText)
{
	if (NULL == lpText || NULL ==*lpText)
		return;

	TCHAR cTime[32] = {0};
	FormatTime(nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	CString strText;
	strText.Format(_T("%s  %s\r\n"), m_strUserName, cTime);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, 
		_T("΢���ź�"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
	strText = _T("");
 
	AddMsg(m_richRecv.m_hWnd, lpText);

	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	
	//CRect rtRichRecv;
	//::GetWindowRect(m_richRecv.m_hWnd, &rtRichRecv);
	//CRect rtLastImage;
	//IImageOle* pImageOle = NULL;
	//if(!RichEdit_GetImageOle(m_richRecv.m_hWnd, rtLastImage, &pImageOle))
	//	return;
	//CRect rtUploadInfo;
	//::GetClientRect(m_staPicUploadProgress.m_hWnd, &rtUploadInfo);
	//m_staPicUploadProgress.SetWindowText(_T("100%11111111111111111111111111111111111111111111111111111111111111"));
	//::SetWindowPos(m_staPicUploadProgress.m_hWnd, m_richRecv.m_hWnd, rtRichRecv.left+rtLastImage.right-rtUploadInfo.Width(), rtRichRecv.top+rtLastImage.bottom-rtUploadInfo.Height(), 500, 300/*rtUploadInfo.Width(), rtUploadInfo.Height()*/, SWP_SHOWWINDOW);
	//if(pImageOle != NULL)
	//	pImageOle->Release();
}

void CBuddyChatDlg::AddMsgToRecvEdit(CBuddyMessage* lpBuddyMsg)
{
	if (NULL == lpBuddyMsg || NULL == m_lpFMGClient)
		return;

	UINT nMsgType = lpBuddyMsg->m_nMsgType;
	tstring strMsgText(_T("                                       ��"));
	
	TCHAR cTime[32] = {0};
	if (IsToday(lpBuddyMsg->m_nTime))
		FormatTime(lpBuddyMsg->m_nTime, _T("%H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));
	else
		FormatTime(lpBuddyMsg->m_nTime, _T("%Y-%m-%d %H:%M:%S"), cTime, sizeof(cTime)/sizeof(TCHAR));

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);

	//���ڶ�����Ϣ����ͼƬ�ϴ�ȷ����Ϣ������ʾ��������Ϣ
	if(!lpBuddyMsg->IsShakeWindowMsg() && nMsgType!=CONTENT_TYPE_IMAGE_CONFIRM)
	{
		CString strText;
		RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
		if(lpBuddyMsg->m_nFromUin == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
		{
			strText.Format(_T("%s  %s\r\n"), m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str(), cTime);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("΢���ź�"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
		}
		else
		{
			strText.Format(_T("%s  %s\r\n"), m_strBuddyName, cTime);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("΢���ź�"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
		}
	}

	CString strInfo;
	tstring	strFileName;
	tstring strFileNameWithoutPath;
	
	//����������Ϣ��������ı���Ϣ��ǰ��
	CFontInfo fontInfo;
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

			case CONTENT_TYPE_SHAKE_WINDOW:
				{
					strMsgText += m_strBuddyName;
					strMsgText += _T("����������һ�����ڶ�����") ;
					RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, strMsgText.c_str(), _T("΢���ź�"), 10,  0, FALSE, FALSE, FALSE, FALSE, 300);
				}
				break;

			case CONTENT_TYPE_FILE:
				{
					strInfo.Format(_T("%s���������ļ�["), m_strBuddyName);
					RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10,  0, FALSE, FALSE, FALSE, FALSE, 300);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, lpContent->m_CFaceInfo.m_strFileName.c_str(), _T("΢���ź�"), 10,  RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 300);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("]\r\n"), _T("΢���ź�"), 10,  0, FALSE, FALSE, FALSE, FALSE, 300);
					ShowRequestRecvFile(lpContent->m_CFaceInfo.m_bOnline, lpContent->m_CFaceInfo.m_strFilePath.c_str(), lpContent->m_CFaceInfo.m_strFileName.c_str(), (long)lpContent->m_CFaceInfo.m_dwFileSize);
				}
				break;

			case CONTENT_TYPE_CHAT_IMAGE:
				{
					if(nMsgType == CONTENT_TYPE_TEXT)
					{
						long lStartChar = 0, lEndChar = 0;
						RichEdit_GetSel(m_richRecv.m_hWnd, lStartChar, lEndChar);
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
						if(!Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str())<=0)
						{
							m_mapRecvFileInfo.insert(std::pair<CString, long>(strFileName.c_str(), lStartChar));						
							strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadImageProgress.gif");
						}
						
						_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
					}
					else if(nMsgType == CONTENT_TYPE_IMAGE_CONFIRM)
					{
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
                        //strFileName += _T(".jpg");
						std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(strFileName.c_str());
						if(iter != m_mapRecvFileInfo.end())
						{
							if(lpContent->m_CFaceInfo.m_strFilePath.empty() || !Hootina::CPath::IsFileExist(strFileName.c_str()) || IUGetFileSize(strFileName.c_str())==0)
								strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");
							
							RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
							_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
						}
					}
					else if(nMsgType == CONTENT_TYPE_MOBILE_IMAGE)
					{
						strFileName = m_lpFMGClient->m_UserMgr.GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str());
						if(!Hootina::CPath::IsFileExist(strFileName.c_str()))
							strFileName = Hootina::CPath::GetAppPath() + _T("Image\\DownloadFailed.gif");

						RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
						_RichEdit_InsertFace(m_richRecv.m_hWnd, strFileName.c_str(), -1, -1);
					}
				}
				break;

            case CONTENT_TYPE_REMOTE_DESKTOP:
                {
                    int k = 0;
                }
                break;
			}

			if( lpContent->m_nType!=CONTENT_TYPE_FONT_INFO && nMsgType!=CONTENT_TYPE_IMAGE_CONFIRM)
			{
				if(m_bEnableAutoReply)
				{
					InsertAutoReplyContent();
				}
			}
		}
	}
	
	
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	
}

void CBuddyChatDlg::InsertAutoReplyContent()
{
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
	CString strAutoReplyContent(m_lpFMGClient->m_UserConfig.GetAutoReplyContent());
	if(strAutoReplyContent == "")
		strAutoReplyContent = _T("[�Զ��ظ�]���ã����������²��ڣ��Ժ�����ϵ����");
	else
		strAutoReplyContent.Insert(0, _T("[�Զ��ظ�]"));

	TCHAR szTime[32] = {0};
	time_t nAutoReplyTime = time(NULL);
	FormatTime(nAutoReplyTime, _T("%H:%M:%S"), szTime, ARRAYSIZE(szTime));

	CString strText;
	strText.Format(_T("%s  %s\r\n"), m_strUserName, szTime);

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("����"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);
	
	CFontInfo fontInfo = m_FontSelDlg.GetFontInfo();
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strAutoReplyContent, _T("΢���ź�"), fontInfo.m_nSize, fontInfo.m_clrText, fontInfo.m_bBold, fontInfo.m_bItalic, fontInfo.m_bUnderLine, FALSE, 300);
	//������Ϣ��ʽ�ǣ�/0["������,�ֺ�,��ɫ,����,б��,�»���"]
	TCHAR szFontInfo[1024] = {0};
	TCHAR szColor[32] = {0};
	RGBToHexStr(fontInfo.m_clrText, szColor, ARRAYSIZE(szColor));
	LPCTSTR lpFontFmt = _T("/o[\"%s,%d,%s,%d,%d,%d\"]");
	wsprintf(szFontInfo, lpFontFmt, fontInfo.m_strName.c_str(), fontInfo.m_nSize, szColor, fontInfo.m_bBold, fontInfo.m_bItalic, fontInfo.m_bUnderLine);

	strAutoReplyContent += szFontInfo;
    m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nAutoReplyTime, strAutoReplyContent.GetString(), m_hWnd);
}
void CBuddyChatDlg::AddMsgToMsgLogEdit(std::vector<CContent*>& arrContent)
{
	//����������Ϣ��������ı���Ϣ��ǰ��
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
					RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, _T("������һ�¡�"), _T("΢���ź�"), 10,  0, TRUE, FALSE, FALSE, FALSE, 300);
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
					//��׿���͹�����·������������/storage/sdcard/windows/BstSharedFolder/]FHGGRBAA@85{PP{W3S]8C52.jpg
					//�ȼ���Ƿ���������C:\dd\xx.png��·��
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
void CBuddyChatDlg::AddMsgToMsgLogEdit(std::vector<BUDDY_MSG_LOG*>& arrMsgLog)
{
	UINT	nID;
	UINT	nUTalkNum;
	tstring strNickName;
	UINT64	nTime;
	BOOL	bSendFlag;
	tstring strContent;
	TCHAR	szTime[32];
	CString strText;
	
	COLORREF clrNickName(RGB(0, 0, 0));
	size_t nSize = arrMsgLog.size();
	for (size_t i = 0; i < nSize; ++i)
	{
		nID = arrMsgLog[i]->nID;
		nUTalkNum = arrMsgLog[i]->nUTalkNum;
		strNickName = arrMsgLog[i]->strNickName;
		nTime = arrMsgLog[i]->nTime;
		bSendFlag = arrMsgLog[i]->bSendFlag;
		strContent = arrMsgLog[i]->strContent;

		
		memset(szTime, 0, sizeof(szTime));
		if(IsToday(nTime))	
			FormatTime(nTime, _T("%H:%M:%S"), szTime, ARRAYSIZE(szTime));
		else
			FormatTime(nTime, _T("%Y-%m-%d %H:%M:%S"), szTime, ARRAYSIZE(szTime));

		strText.Format(_T("%s  %s\r\n"), strNickName.c_str(), szTime);

		RichEdit_SetSel(m_richMsgLog.m_hWnd, -1, -1);
		if(bSendFlag)
			clrNickName = RGB(0, 128, 64);
		else
			clrNickName = RGB(0, 0, 255);
		RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, strText, _T("΢���ź�"), 10, clrNickName, FALSE, FALSE, FALSE, FALSE, 0);
		//strText = _T("");

		AnalyseContent(strContent);
		
		//AddMsg(m_richMsgLog.m_hWnd, strContent.c_str());
		//���һ�в�Ҫ���뻻�з�
		if(i != nSize-1)
		{
			RichEdit_ReplaceSel(m_richMsgLog.m_hWnd, _T("\r\n"));
			RichEdit_SetStartIndent(m_richMsgLog.m_hWnd, 0);
		}
		
		//m_richMsgLog.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	}
	
	m_richMsgLog.Invalidate();
	//TODO: ����������������ʵ�����ݳ��Ȳ�ƥ�������
	//m_richMsgLog.UpdateWindow();
	//m_richMsgLog.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	//m_richMsgLog.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);	
}

void CBuddyChatDlg::AddMsgToRecvEdit(std::vector<BUDDY_MSG_LOG*>& arrMsgLog)
{
	UINT	nID;
	UINT	nUTalkNum;
	tstring strNickName;
	UINT64	nTime;
	BOOL	bSendFlag;
	tstring strContent;
	TCHAR	szTime[32];
	CString strText;
	
	size_t nSize = arrMsgLog.size();
	for (size_t i = 0; i < nSize; ++i)
	{
		nID = arrMsgLog[i]->nID;
		nUTalkNum = arrMsgLog[i]->nUTalkNum;
		strNickName = arrMsgLog[i]->strNickName;
		nTime = arrMsgLog[i]->nTime;
		bSendFlag = arrMsgLog[i]->bSendFlag;
		strContent = arrMsgLog[i]->strContent;

		
		memset(szTime, 0, sizeof(szTime));
		if(IsToday(nTime))	
			FormatTime(nTime, _T("%H:%M:%S"), szTime, ARRAYSIZE(szTime));
		else
			FormatTime(nTime, _T("%Y-%m-%d %H:%M:%S"), szTime, ARRAYSIZE(szTime));

		
		strText.Format(_T("%s  %s\r\n"), strNickName.c_str(), szTime);

		RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
		if(bSendFlag)
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("΢���ź�"), 10, RGB(0,128,64), FALSE, FALSE, FALSE, FALSE, 0);
		else
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, strText, _T("΢���ź�"), 10, RGB(0, 0, 255), FALSE, FALSE, FALSE, FALSE, 0);

		//strText = _T("");

		AnalyseContent(strContent, m_richRecv.m_hWnd);
		
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\r\n"));
		RichEdit_SetStartIndent(m_richRecv.m_hWnd, 0);
		m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
		
	}

	if(nSize > 0)
	{
		RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
		RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("                                            ����������������ʷ��Ϣ��������\r\n"), _T("΢���ź�"), 9, RGB(128,128,128), FALSE, FALSE, FALSE, FALSE, 0);
		m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	}
	//RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("\n"));
	////TODO: ��֣�������Ϊʲô���ܹ����ͣ�
	//m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CBuddyChatDlg::AddMsgToRecvEdit(std::vector<CContent*>& arrContent)
{
	//����������Ϣ��������ı���Ϣ��ǰ��
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
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("������һ�¡�"), _T("΢���ź�"), 10,  0, TRUE, FALSE, FALSE, FALSE, 300);
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
					//��׿���͹�����·������������/storage/sdcard/windows/BstSharedFolder/]FHGGRBAA@85{PP{W3S]8C52.jpg
					//�ȼ���Ƿ���������C:\dd\xx.png��·��
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

// ����Ϣ��¼�������
void CBuddyChatDlg::OpenMsgLogBrowser()
{
	CString strMsgFile = m_lpFMGClient->GetMsgLogFullName().c_str();
	strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);

	CString strChatPicDir = m_lpFMGClient->GetChatPicFolder().c_str();
	strChatPicDir.Replace(_T("\\"), _T("/"));

	
	std::vector<BUDDY_MSG_LOG*> arrMsgLog;
	UINT nRows = 10;
	UINT nOffset = 0;
	if(m_nMsgLogRecordOffset > 1)
		nOffset = m_nMsgLogRecordOffset-1;
	//����Ϣ��¼�ļ��л�ȡ��Ϣ��¼
	long cntArrMsgLog = m_MsgLogger.ReadBuddyMsgLog(m_nUTalkUin, nOffset, nRows, arrMsgLog);
	
	m_richMsgLog.SetWindowText(_T(""));
	//��ӵ���Ϣ��¼���ı��ؼ���
	AddMsgToMsgLogEdit(arrMsgLog);
	//m_richMsgLog.Invalidate(TRUE);

	ShowMsgLogButtons(TRUE);
	m_richMsgLog.SetFocus();
}

// �ر���Ϣ��¼�������
void CBuddyChatDlg::CloseMsgLogBrowser()
{
	::SendMessage(m_richMsgLog.m_hWnd, WM_SETTEXT, 0, 0L);
	//if (m_dwThreadId != NULL)
	//{
	//	::PostThreadMessage(m_dwThreadId, WM_CLOSE_MSGLOGDLG, 0, 0);
	//	m_dwThreadId = NULL;
	//}

	ShowMsgLogButtons(FALSE);

	CalculateMsgLogCountAndOffset();
}

BOOL CBuddyChatDlg::GetImageDisplaySizeInRichEdit(PCTSTR pszFileName, HWND hWnd, long& nWidth, long& nHeight)
{
	//1����Լ����20�
	const long TWIPS = 20;
	long nWidthImage = 0;
	long nHeightImage = 0;
	GetImageWidthAndHeight(pszFileName, nWidthImage, nHeightImage);

	if(hWnd == m_richSend.m_hWnd)
	{
		CRect rtRichSend;
		::GetClientRect(hWnd, &rtRichSend);
		//ͼƬ̫С��������
		if(nHeightImage <= rtRichSend.Height())
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//����
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//����������
		nWidth = rtRichSend.Height()*nWidthImage/nHeightImage*TWIPS;
		nHeight = rtRichSend.Height()*TWIPS;
	}
	else if(hWnd==m_richRecv.m_hWnd)
	{
		CRect rtRecv;
		::GetClientRect(hWnd, &rtRecv);

		//ͼƬ̫С��������
		if(nHeightImage <= rtRecv.Height())
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//����
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//����������
		//ͼƬ���Ϊ���ڵ��ķ�֮��
		nWidth = rtRecv.Height()*3/4*nWidthImage/nHeightImage*TWIPS;
		nHeight = rtRecv.Height()*3/4*TWIPS;
	}
	else if(hWnd == m_richMsgLog.m_hWnd)
	{
		CRect rtMsgLog;
		::GetClientRect(hWnd, &rtMsgLog);

		//ͼƬ̫С��������
		if(nWidthImage <= rtMsgLog.Width()-20)
		{
			nWidth = 0;
			nHeight = 0;
			return TRUE;
		}
		//����
		if(nHeightImage == 0)
		{
			nWidth = 0;
			nHeight = 0;
			return FALSE;
		}
		//����������
		//ͼƬ���Ϊ���ڵ��ķ�֮һ
		nWidth = (rtMsgLog.Width()/4)*TWIPS;
		nHeight = nWidth*nHeightImage/nWidthImage;
	}

	return TRUE;
}

void CBuddyChatDlg::ResizeImageInRecvRichEdit()
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

LRESULT CBuddyChatDlg::OnSendChatMsgResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CString strInfo;
	if(wParam == SEND_WHOLE_MSG_FAILED)
	{
		strInfo = _T("                                            ����Ϣ����ʧ�ܣ������ԣ���\r\n");
	}
	else if(wParam == SEND_IMGAE_FAILED)
	{
		strInfo = _T("                                            ��ͼƬ����ʧ�ܣ������ԣ���\r\n");
	}
	else if(wParam == SEND_FILE_FAILED)
	{
		strInfo = _T("                                            ���ļ�����ʧ�ܣ������ԣ���\r\n");
	}

	RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
	RichEdit_ReplaceSel(m_richRecv.m_hWnd, strInfo, _T("΢���ź�"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);

	return (LRESULT)1;
}

BOOL CBuddyChatDlg::ShowRequestRecvFile(BOOL bOnlineFile, PCTSTR lpszDownloadName, PCTSTR pszFileName, long nFileSize)
{
	if(lpszDownloadName==NULL || pszFileName==NULL || nFileSize<=0)
		return FALSE;
	
	if(!bOnlineFile)
		RecvOfflineFile(lpszDownloadName, pszFileName, nFileSize);

	
	return (LRESULT)1;
}

void CBuddyChatDlg::ShowLastMsgInRecvRichEdit()
{
	CString strMsgFile(m_lpFMGClient->GetMsgLogFullName().c_str());
	//strMsgFile.Replace(_T("\\"), _T("/"));
	m_MsgLogger.SetMsgLogFileName(strMsgFile);

	//CString strChatPicDir = m_lpFMGClient->GetChatPicFolder().c_str();
	//strChatPicDir.Replace(_T("\\"), _T("/"));

	//��Ϣ��¼���û�δ������Ϣ����
	long nMsgCntUnread = 0;
	CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
	if(lpMsgList != NULL)
	{
		CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_BUDDY, m_nUTalkUin);
		if (lpMsgSender != NULL)
			nMsgCntUnread = lpMsgSender->GetMsgCount();
	}
	
	long nTotalCount = (long)m_MsgLogger.GetBuddyMsgLogCount(m_nUTalkUin);
	//��δ����Ϣ����ʷ��¼��ȥ��
	nTotalCount -= nMsgCntUnread;
	if(nTotalCount < 0)
		nTotalCount = 0;

	if(nTotalCount == 0)
		return;

	UINT nRows = 5;
	if((UINT)nTotalCount < nRows)
		nRows = (UINT)nTotalCount;

	std::vector<BUDDY_MSG_LOG*> arrMsgLog;
	//����Ϣ��¼�ļ��л�ȡ��Ϣ��¼
	long cntArrMsgLog = 0;
	if(nTotalCount > 5)
		cntArrMsgLog = m_MsgLogger.ReadBuddyMsgLog(m_nUTalkUin, nTotalCount-5, nRows, arrMsgLog);
	else
		cntArrMsgLog = m_MsgLogger.ReadBuddyMsgLog(m_nUTalkUin, 0, nRows, arrMsgLog);
	
	//��ӵ���Ϣ��¼���ı��ؼ���
	AddMsgToRecvEdit(arrMsgLog);
}

LRESULT CBuddyChatDlg::OnSendFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	FileProgress* pFileProgress = (FileProgress*)lParam;
	if(pFileProgress == NULL)
		return (LRESULT)0;

	long nPercent = pFileProgress->nPercent;
	
	std::map<CString, long>::const_iterator iter=m_mapSendFileInfo.find(pFileProgress->szDestPath);
	//��������ͼƬ�Ľ���
	if(iter == m_mapSendFileInfo.end())
	{
		if(nPercent != -1)
		{
			m_staPicUploadProgress.ShowWindow(SW_SHOW);
			TCHAR szProgressInfo[MAX_PATH] = {0};
			_stprintf_s(szProgressInfo, ARRAYSIZE(szProgressInfo), _T("���ڷ���ͼƬ%s��%d%%."), Hootina::CPath::GetFileName(pFileProgress->szDestPath).c_str(), nPercent);

			m_staPicUploadProgress.SetWindowText(szProgressInfo);

			if(nPercent >=100 )
				m_staPicUploadProgress.ShowWindow(SW_HIDE);
		}
	}
	//�ļ��ϴ�����
	else
	{
		if(iter->second != -1)
		{
			delete pFileProgress;
			return 0;
		}
		long nFileItemID = m_FileTransferCtrl.GetItemIDByFullName(pFileProgress->szDestPath);
		if(nFileItemID == -1)
		{
			delete pFileProgress;
			return 0;
		}

		//AtlTrace(_T("Progress: %d\n"), nPercent);
		m_FileTransferCtrl.SetItemProgressPercentByID(nFileItemID, nPercent);
		m_FileTransferCtrl.SetItemVerificationPercentByID(nFileItemID, pFileProgress->nVerificationPercent);
	}
	
	delete pFileProgress;

	return (LRESULT)1;
}

LRESULT CBuddyChatDlg::OnSendFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CUploadFileResult* pResult = (CUploadFileResult*)lParam;
	if(pResult == NULL)
		return (LRESULT)0;
	
	//�ϴ�ͼƬ���
	std::map<CString, long>::const_iterator iter=m_mapSendFileInfo.find(pResult->m_szLocalName);
	if(iter == m_mapSendFileInfo.end())
	{
		if(pResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_IMAGE)
		{
			if(wParam == SEND_FILE_FAILED)
			{
				//AtlTrace(_T("Fail to send file:%s.\n"), pResult->m_szLocalName);
				TCHAR szInfo[MAX_PATH] = {0};
				_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ���ͼƬ[%s]ʧ�ܣ������ԣ���\r\n"), ::PathFindFileName(pResult->m_szLocalName));
				RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
				RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			}
            //ͼƬ���ͳɹ���׷���ϴ�ͼƬ�ɹ���Ϣ
            else
            {
                SendConfirmMessage(pResult);
            }
		}
	}
	//�ϴ��ļ����
	else
	{
		if(iter->second == -1)
		{
			if(pResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_OFFLINE_FILE)
			{
				long nFileItemID = m_FileTransferCtrl.GetItemIDByFullName(pResult->m_szLocalName);
				if(nFileItemID == -1)
				{
					delete pResult;
					return 0;
				}
				
				if(wParam == SEND_FILE_SUCCESS)
				{
					TCHAR szRemoteName[MAX_PATH] = {0};
                    EncodeUtil::Utf8ToUnicode(pResult->m_szRemoteName, szRemoteName, ARRAYSIZE(szRemoteName));
					//׷��һ��������Ϣ
					time_t nMsgTime(time(NULL));
					CString strFileInfo;
					strFileInfo.Format(_T("/i[\"%s,%s,%d,0\"]"), ::PathFindFileName(pResult->m_szLocalName), szRemoteName, m_FileTransferCtrl.GetItemFileSizeByID(nFileItemID));
                    m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nMsgTime, strFileInfo.GetString(), m_hWnd);

					
					//��ʾ�û��ļ����ͳɹ�
					TCHAR szInfo[MAX_PATH] = {0};
					//_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ���%s�ɹ���\r\n"), ::PathFindFileName(pResult->m_szLocalName));
					RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("                                            ��ɹ�����["), _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
					_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("%s"), ::PathFindFileName(pResult->m_szLocalName));
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(0,0,255), FALSE, FALSE, FALSE, FALSE, 0);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("]��\r\n"), _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
					//RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
				}
				else if(wParam == SEND_FILE_FAILED)
				{
					TCHAR szInfo[MAX_PATH] = {0};
					_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ���%sʧ�ܣ����Ժ����ԣ���\r\n"), ::PathFindFileName(pResult->m_szLocalName));
					RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
					RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);
				}

				m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
				m_FileTransferCtrl.RemoveItemByID(nFileItemID);
				m_mapSendFileInfo.erase(iter);
				DisplayFileTransfer(FALSE);
			}
		}
	}
	
	delete pResult;

	return 1;
}

LRESULT CBuddyChatDlg::OnRecvFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	FileProgress* pFileProgress = (FileProgress*)lParam;
	if(pFileProgress == NULL)
		return 0;

	long nPercent = pFileProgress->nPercent;
	std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(pFileProgress->szDestPath);
	if(iter != m_mapRecvFileInfo.end())
	{
		//�����ļ����ؽ���
		if(iter->second == -1)
		{
			long nFileItemID = m_FileTransferCtrl.GetItemIDBySaveName(pFileProgress->szDestPath);
			if(nFileItemID < 0)
				return 0;
			
			m_FileTransferCtrl.SetItemProgressPercentByID(nFileItemID, nPercent);

		}
		//����ͼƬ���ؽ���
		else
		{
			long nIndex = nPercent / 10;
			RichEdit_SetSel(m_richRecv.m_hWnd, iter->second, iter->second+1);
			CString strProgressFile;
			strProgressFile.Format(_T("%sImage\\FileProgress\\percent%d.png"), g_szHomePath, nIndex);
			_RichEdit_InsertFace(m_richRecv.m_hWnd, strProgressFile, -1, -1);
		}
	}

	delete pFileProgress;
	
	return 1;
}

LRESULT CBuddyChatDlg::OnRecvFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CFileItemRequest* pFileItem = (CFileItemRequest*)lParam;
	if(pFileItem == NULL)
		return 0;

	std::map<CString, long>::const_iterator iter = m_mapRecvFileInfo.find(pFileItem->m_szFilePath);
	if(iter == m_mapRecvFileInfo.end())
		return 0;
	
	//����ͼƬ���ؽ��
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
	//�����ļ����ؽ��
	else
	{
		long nFileItemID = pFileItem->m_nID;
		if(wParam == RECV_FILE_SUCCESS)
		{
			m_richRecv.SetAutoURLDetect(FALSE);
			TCHAR szInfo[MAX_PATH] = {0};
			RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("                                            ��ɹ������ļ�["), _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, ::PathFindFileName(pFileItem->m_szFilePath), _T("΢���ź�"), 10, RGB(0,0,255), FALSE, FALSE, FALSE, FALSE, 0);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, _T("]����\r\n"), _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			memset(szInfo, 0, sizeof(szInfo));
			_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ���ļ�����λ�ã�"));
			RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			memset(szInfo, 0, sizeof(szInfo));
			_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("%s"), pFileItem->m_szFilePath);
			//RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, TRUE, TRUE, 0);
			memset(szInfo, 0, sizeof(szInfo));
			_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("����\r\n"), pFileItem->m_szFilePath);
			//RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(0,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			m_richRecv.SetAutoURLDetect(TRUE);

			time_t nTime = time(NULL);
			CString strSuccessfulInfo;
			strSuccessfulInfo.Format(_T("%s�ѳɹ������ļ�[%s]."), m_strUserName, ::PathFindFileName(pFileItem->m_szFilePath));
            m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nTime, strSuccessfulInfo.GetString(), m_hWnd);
		}
		else if(wParam == RECV_FILE_FAILED)
		{
			TCHAR szInfo[MAX_PATH] = {0};
			_stprintf_s(szInfo, ARRAYSIZE(szInfo), _T("                                            ������ļ�%sʧ�ܣ�����ϵ�Է��ش�����\r\n"), ::PathFindFileName(pFileItem->m_szFilePath));
			RichEdit_SetSel(m_richRecv.m_hWnd, -1, -1);
			RichEdit_ReplaceSel(m_richRecv.m_hWnd, szInfo, _T("΢���ź�"), 10, RGB(255,0,0), FALSE, FALSE, FALSE, FALSE, 0);
			time_t nTime = time(NULL);
			CString strFailedInfo;
			strFailedInfo.Format(_T("%s�ѽ����ļ�[%s]ʧ��."), m_strUserName, ::PathFindFileName(pFileItem->m_szFilePath));
            m_lpFMGClient->SendBuddyMsg(m_LoginUserId, m_strUserName.GetString(), m_UserId, m_strBuddyName.GetString(), nTime, strFailedInfo.GetString(), m_hWnd);
		}

		m_FileTransferCtrl.RemoveItemByID(nFileItemID);
		//��Ҫ���������Ƿ��������ļ��ڴ����Ծ����Ƿ������Ҳര��
		DisplayFileTransfer(FALSE);	
	}

	m_richRecv.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
	m_mapRecvFileInfo.erase(iter);
	
	return 1;
}


void CBuddyChatDlg::SendConfirmMessage(const CUploadFileResult* pUploadFileResult)
{
    if (pUploadFileResult == NULL)
        return;

    //�ϴ�ͼƬ���
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
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%lld,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_nFileSize, nWidth, nHeight);
        else
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%lld,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_nFileSize, nWidth, nHeight);

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

void CBuddyChatDlg::RecordWindowSize()
{	
	if(IsZoomed() || IsIconic())
		return;

	CRect rtWindow;
	GetWindowRect(&rtWindow);
	if(m_bMsgLogWindowVisible || m_bFileTransferVisible)
		m_lpFMGClient->m_UserConfig.SetChatDlgWidth(rtWindow.Width()-RIGHT_CHAT_WINDOW_WIDTH);
	else
		m_lpFMGClient->m_UserConfig.SetChatDlgWidth(rtWindow.Width());

	m_lpFMGClient->m_UserConfig.SetChatDlgHeight(rtWindow.Height());	
}

void CBuddyChatDlg::ReCaculateCtrlPostion(long nMouseY)
{
	CRect rtClient;
	::GetClientRect(m_hWnd, &rtClient);
	
	//���������Ϳ�ߴ�����̫�����̫С��������Ӱ��ĳЩ�ؼ��ڲ��Ļ�����ĳЩ�ؼ��ڲ�����Ҫ���������һ���ĳߴ磩
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
	//���տ�
    if (m_FontSelDlg.IsWindow() && m_FontSelDlg.IsWindowVisible())
	{
		//AtlTrace(_T("nMouseY-ptBase.y-3*CHATDLG_TOOLBAR_HEIGHT: %d\n"), nMouseY-ptBase.y-3*CHATDLG_TOOLBAR_HEIGHT);
		//TODO: nMouseY-ptBase.y-2*CHATDLG_TOOLBAR_HEIGHTΪʲô���������أ�
		::DeferWindowPos(hdwp, m_richRecv, NULL, 0, 0, rtRichRecv.Width(), nMouseY-ptBase.y-2*CHATDLG_TOOLBAR_HEIGHT, SWP_NOMOVE|SWP_NOZORDER);
		::DeferWindowPos(hdwp, m_FontSelDlg, NULL, 0, ptBase.y+rtRichRecv.Height()-CHATDLG_TOOLBAR_HEIGHT, 0, CHATDLG_TOOLBAR_HEIGHT, SWP_NOSIZE|SWP_NOZORDER);
	}
	else
		::DeferWindowPos(hdwp, m_richRecv, NULL, 0, 0, rtRichRecv.Width(), nMouseY-ptBase.y-CHATDLG_TOOLBAR_HEIGHT, SWP_NOMOVE|SWP_NOZORDER);
	
	//MidToolBar
	::GetClientRect(m_SplitterCtrl, &rtSplitter);
	//AtlTrace(_T("ptBase.y+rtRichRecv.Height()+rtSplitter.Height(): %d\n"), ptBase.y+rtRichRecv.Height()+rtSplitter.Height());
	::DeferWindowPos(hdwp, m_tbMid, NULL, 3, ptBase.y+rtRichRecv.Height(), 0, 0, SWP_NOSIZE|SWP_NOZORDER);
	//�ָ���
	::GetClientRect(m_richRecv, &rtRichRecv);
	//AtlTrace(_T("ptBase.y+rtRichRecv.Height(): %d\n"), ptBase.y+rtRichRecv.Height());
	::DeferWindowPos(hdwp, m_SplitterCtrl, NULL, 6, ptBase.y+rtRichRecv.Height()+rtMidToolbar.Height(), 0, 0, SWP_NOSIZE|SWP_NOZORDER);
	//���Ϳ�
	long nHeightRichSend = rtClient.Height()-100-44-(rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height());
	::GetClientRect(m_tbMid, &rtMidToolbar);
	//AtlTrace(_T("RichSend top: %d\n"), ptBase.y+rtRichRecv.bottom-rtRichRecv.top+rtSplitter.top-rtSplitter.bottom+rtMidToolbar.bottom-rtMidToolbar.top);
	if(m_bMsgLogWindowVisible || m_bFileTransferVisible)
		::DeferWindowPos(hdwp, m_richSend, NULL, 6, ptBase.y+rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height(), rtClient.Width()-8-RIGHT_CHAT_WINDOW_WIDTH, nHeightRichSend, SWP_NOZORDER);
	else
		::DeferWindowPos(hdwp, m_richSend, NULL, 6, ptBase.y+rtRichRecv.Height()+rtSplitter.Height()+rtMidToolbar.Height(), rtClient.Width()-8, nHeightRichSend, SWP_NOZORDER);
	::EndDeferWindowPos(hdwp);

	ResizeImageInRecvRichEdit();

	//��¼�����µĽ����ı���midTooBar�ͷ����ı��������λ��
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

UINT CBuddyChatDlg::RemoteDesktopProc(void* p)
{
    CBuddyChatDlg* pThis = (CBuddyChatDlg*)p;
    if (pThis == NULL)
        return 0;
    
    SIZE screenSize;
    HDC hScrDC = ::CreateDC(_T("DISPLAY"), NULL, NULL, NULL);
    screenSize.cx = ::GetDeviceCaps(hScrDC, HORZRES);
    screenSize.cy = ::GetDeviceCaps(hScrDC, VERTRES);

    int nSeq = 0;
    while (true)
    {
        //HDC hDesktopDC = ::GetDC(NULL);
        //HDC hTmpDC = CreateCompatibleDC(hDesktopDC);
        //HBITMAP hBmp = CreateCompatibleBitmap(hDesktopDC, screenSize.cx, screenSize.cy);	//351x250, ʾ������
        //SelectObject(hTmpDC, hBmp);
        //BitBlt(hTmpDC, 0, 0, screenSize.cx, screenSize.cy, hDesktopDC, 0, 0, SRCCOPY);
        //DeleteObject(hTmpDC);

        //BITMAP bm;
        //PBITMAPINFO bmpInf;
        //if (GetObject(hBmp, sizeof(bm), &bm) == 0)
        //{
        //    ::ReleaseDC(NULL, hDesktopDC);
        //    return 0;
        //}

        //int nPaletteSize = 0;
        //if (bm.bmBitsPixel < 16)
        //    nPaletteSize = (int)pow(2, bm.bmBitsPixel);

        //int nSize = sizeof(BITMAPINFOHEADER) +
        //    sizeof(RGBQUAD)*nPaletteSize + (bm.bmWidth + 7) / 8 * bm.bmHeight*bm.bmBitsPixel;
        //bmpInf = (PBITMAPINFO)LocalAlloc(LPTR, nSize);

        //BYTE* buf = ((BYTE*)bmpInf) +
        //    sizeof(BITMAPINFOHEADER) +
        //    sizeof(RGBQUAD)*nPaletteSize;

        ////-----------------------------------------------
        //bmpInf->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        //bmpInf->bmiHeader.biWidth = bm.bmWidth;
        //bmpInf->bmiHeader.biHeight = bm.bmHeight;
        //bmpInf->bmiHeader.biPlanes = bm.bmPlanes;
        //bmpInf->bmiHeader.biBitCount = bm.bmBitsPixel;
        //bmpInf->bmiHeader.biCompression = BI_RGB;
        //bmpInf->bmiHeader.biSizeImage = (bm.bmWidth + 7) / 8 * bm.bmHeight*bm.bmBitsPixel;
        ////-----------------------------------------------

        //if (!::GetDIBits(hDesktopDC, hBmp, 0, (UINT)bm.bmHeight, buf, bmpInf, DIB_RGB_COLORS))
        //{
        //    ::ReleaseDC(NULL, hDesktopDC);
        //    LocalFree(bmpInf);
        //    return 0;
        //}

        //::ReleaseDC(NULL, hDesktopDC);

        //std::string outbuf;
        //balloon::BinaryWriteStream writeStream(&outbuf);
        //writeStream.WriteInt32(msg_type_screenshot);
        //writeStream.WriteInt32(nSeq);

        ////Ϊ�˱���ͳһ������߼�������Ӹ�ռλ������
        //std::string strDummyData;
        //writeStream.WriteString(strDummyData);

        ////λͼ�ļ�ͷ����Ϣ
        //writeStream.WriteCString((const char*)bmpInf, nSize);
        ////λͼ�ļ�����
        //writeStream.WriteString(strDummyData);
        ////Ŀ��userid
        //writeStream.WriteInt32(pThis->m_nUTalkNumber);
        //writeStream.Flush();

        //CIUSocket::GetInstance().Send(outbuf);

        //CString sMsg;
        //sMsg.Format("BitsPixel:%d,width:%d,height:%d",
        //    bm.bmBitsPixel, bm.bmWidth, bm.bmHeight);
        //AfxMessageBox(sMsg);

        ::Sleep(1000);
        //nSeq++;
    }

    //CClientDC dc(pThis->m_hWnd);

    //int nOffset;
    //BYTE r, g, b;
    //int nWidth = bm.bmWidth*bm.bmBitsPixel / 8;
    //nWidth = ((nWidth + 3) / 4) * 4; //4�ֽڶ���

    //if (bmpInf->bmiHeader.biBitCount == 8)
    //{
    //    for (int i = 0; i<bm.bmHeight; i++)
    //    {
    //        for (int j = 0; j<bm.bmWidth; j++)
    //        {
    //            RGBQUAD rgbQ;
    //            rgbQ = bmpInf->bmiColors[buf[i*nWidth + j]];
    //            dc.SetPixel(j, bm.bmHeight - i, RGB(rgbQ.rgbRed, rgbQ.rgbGreen, rgbQ.rgbBlue)); //������ʾ
    //        }
    //    }
    //}
    //else if (bmpInf->bmiHeader.biBitCount == 16)
    //{
    //    for (int i = 0; i<bm.bmHeight; i++)
    //    {
    //        nOffset = i*nWidth;
    //        for (int j = 0; j<bm.bmWidth; j++)
    //        {
    //            b = buf[nOffset + j * 2] & 0x1F;
    //            g = buf[nOffset + j * 2] >> 5;
    //            g |= (buf[nOffset + j * 2 + 1] & 0x03) << 3;
    //            r = (buf[nOffset + j * 2 + 1] >> 2) & 0x1F;

    //            r *= 8;
    //            b *= 8;
    //            g *= 8;

    //            dc.SetPixel(j, bm.bmHeight - i, RGB(r, g, b)); //������ʾ
    //        }
    //    }
    //}
    //else if (bmpInf->bmiHeader.biBitCount == 24)
    //{
    //    for (int i = 0; i<bm.bmHeight; i++)
    //    {
    //        nOffset = i*nWidth;
    //        for (int j = 0; j<bm.bmWidth; j++)
    //        {
    //            b = buf[nOffset + j * 3];
    //            g = buf[nOffset + j * 3 + 1];
    //            r = buf[nOffset + j * 3 + 2];

    //            dc.SetPixel(j, bm.bmHeight - i, RGB(r, g, b)); //������ʾ
    //        }
    //    }
    //}
    //else if (bmpInf->bmiHeader.biBitCount == 32)
    //{
    //    for (int i = 0; i<bm.bmHeight; i++)
    //    {
    //        nOffset = i*nWidth;
    //        for (int j = 0; j<bm.bmWidth; j++)
    //        {
    //            b = buf[nOffset + j * 4];
    //            g = buf[nOffset + j * 4 + 1];
    //            r = buf[nOffset + j * 4 + 2];

    //            dc.SetPixel(j, bm.bmHeight - i, RGB(r, g, b)); //������ʾ
    //        }
    //    }
    //}

    //DeleteObject(hBmp);
    //LocalFree(bmpInf);

    return 0;
}