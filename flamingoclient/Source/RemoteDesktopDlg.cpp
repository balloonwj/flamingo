#include "stdafx.h"
#include "RemoteDesktopDlg.h"
#include "FlamingoClient.h"
#include "UserSnapInfoDlg.h"
#include "Utils.h"
#include "EncodingUtil.h"

// 两种算法
#define ALGORITHM_SCAN	1	// 速度很快，但碎片太多
#define ALGORITHM_DIFF	2	// 速度很慢，也占CPU，但是数据量都是最小的

enum
{
    IDM_CONTROL = 0x0010,
    IDM_SEND_CTRL_ALT_DEL,
    IDM_TRACE_CURSOR,	// 跟踪显示远程鼠标
    IDM_BLOCK_INPUT,	// 锁定远程计算机输入
    IDM_BLANK_SCREEN,	// 黑屏
    IDM_CAPTURE_LAYER,	// 捕捉层
    IDM_SAVEDIB,		// 保存图片
    IDM_GET_CLIPBOARD,	// 获取剪贴板
    IDM_SET_CLIPBOARD,	// 设置剪贴板
    IDM_ALGORITHM_SCAN,	// 隔行扫描算法
    IDM_ALGORITHM_DIFF,	// 差异比较算法
    IDM_DEEP_1,			// 屏幕色彩深度.....
    IDM_DEEP_4_GRAY,
    IDM_DEEP_4_COLOR,
    IDM_DEEP_8_GRAY,
    IDM_DEEP_8_COLOR,
    IDM_DEEP_16,
    IDM_DEEP_32
};

// CRemoteDesktopDlg实现代码
CRemoteDesktopDlg::CRemoteDesktopDlg()
{
    m_pFMGClient = NULL;

    //m_iocpServer = pIOCPServer;
    //m_pContext = pContext;
    m_bIsFirst = true; // 如果是第一次打开对话框，显示提示等待信息
    m_lpScreenDIB = NULL;
    TCHAR szPath[MAX_PATH];
    ::GetSystemDirectory(szPath, MAX_PATH);
    lstrcat(szPath, _T("\\shell32.dll"));
    m_hIcon = ::ExtractIcon(::GetModuleHandle(NULL), szPath, 17/*网上邻居图标索引*/);

    UINT	nBISize = 1064;/*m_pContext->m_DeCompressionBuffer.GetBufferLen() - 1;*/
    m_lpbmi = (BITMAPINFO *) new BYTE[nBISize];
    m_lpbmi_rect = (BITMAPINFO *) new BYTE[nBISize];

    //memcpy(m_lpbmi, m_pContext->m_DeCompressionBuffer.GetBuffer(1), nBISize);
    //memcpy(m_lpbmi_rect, m_pContext->m_DeCompressionBuffer.GetBuffer(1), nBISize);

    memset(&m_MMI, 0, sizeof(MINMAXINFO));

    m_bIsCtrl = false; // 默认不控制
    m_nCount = 0;
    m_bCursorIndex = 1;
}

CRemoteDesktopDlg::~CRemoteDesktopDlg()
{
}

BOOL CRemoteDesktopDlg::PreTranslateMessage(MSG* pMsg)
{
    //支持回车键查找
    if (pMsg->hwnd == m_hWnd && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
    {
        PostMessage(WM_COMMAND, (WPARAM)IDC_BTN_ADD, 0);
        return TRUE;
    }

    if (pMsg->hwnd == m_hWnd && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
    {
        ShowWindow(SW_HIDE);
        return TRUE;
    }

    // TODO: Add your specialized code here and/or call the base class
#define MAKEDWORD(h,l)        (((unsigned long)h << 16) | l)

    CRect rect;
    GetClientRect(&rect);

    switch (pMsg->message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    {
        MSG	msg;
        memcpy(&msg, pMsg, sizeof(MSG));
        //msg.lParam = MAKEDWORD(HIWORD(pMsg->lParam) + m_VScrollPos, LOWORD(pMsg->lParam) + m_HScrollPos);
        msg.pt.x += m_HScrollPos;
        msg.pt.y += m_VScrollPos;
        SendCommand(&msg);
    }
    break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (pMsg->wParam != VK_LWIN && pMsg->wParam != VK_RWIN)
        {
            MSG	msg;
            memcpy(&msg, pMsg, sizeof(MSG));
            //msg.lParam = MAKEDWORD(HIWORD(pMsg->lParam) + m_VScrollPos, LOWORD(pMsg->lParam) + m_HScrollPos);
            msg.pt.x += m_HScrollPos;
            msg.pt.y += m_VScrollPos;
            SendCommand(&msg);
        }
        if (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE)
            return true;
        break;
    default:
        break;
    }

    //return __super::PreTranslateMessage(pMsg);

    return CWindow::IsDialogMessage(pMsg);
}

BOOL CRemoteDesktopDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
    AddMessageFilter();

    // Set the icon for this dialog.  The framework does this automatically
    // when the application's main window is not a dialog
    SetIcon(m_hIcon, TRUE);			// Set big icon
    SetIcon(m_hIcon, FALSE);		// Set small icon

    SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL, IDC_NO));
    HMENU hSysMenu = GetSystemMenu(FALSE);
    if (hSysMenu != NULL)
    {
        ::AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hSysMenu, MF_STRING, IDM_CONTROL, _T("控制屏幕(&Y)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_SEND_CTRL_ALT_DEL, _T("发送Ctrl-Alt-Del(&K)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_TRACE_CURSOR, _T("跟踪服务端鼠标(&T)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_BLOCK_INPUT, _T("锁定服务端鼠标和键盘(&L)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_BLANK_SCREEN, _T("服务端黑屏(&B)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_CAPTURE_LAYER, _T("捕捉层(导致鼠标闪烁)(&L)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_SAVEDIB, _T("保存快照(&S)"));
        ::AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hSysMenu, MF_STRING, IDM_GET_CLIPBOARD, _T("获取剪贴板(&R)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_SET_CLIPBOARD, _T("设置剪贴板(&L)"));
        ::AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hSysMenu, MF_STRING, IDM_ALGORITHM_SCAN, _T("隔行扫描算法(&S)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_ALGORITHM_DIFF, _T("差异比较算法(&X)"));
        ::AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_1, _T("1 位黑白(&A)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_4_GRAY, _T("4 位灰度(&B)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_4_COLOR, _T("4 位彩色(&C)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_8_GRAY, _T("8 位灰度(&D)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_8_COLOR, _T("8 位彩色(&E)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_16, _T("16位高彩(&F)"));
        ::AppendMenu(hSysMenu, MF_STRING, IDM_DEEP_32, _T("32位真彩(&G)"));

        ::CheckMenuRadioItem(hSysMenu, IDM_ALGORITHM_SCAN, IDM_ALGORITHM_DIFF, IDM_ALGORITHM_SCAN, MF_BYCOMMAND);
        ::CheckMenuRadioItem(hSysMenu, IDM_DEEP_4_GRAY, IDM_DEEP_32, IDM_DEEP_8_COLOR, MF_BYCOMMAND);
    }

    // TODO: Add extra initialization here
    CString str;
    str.Format(_T("\\\\%s %d * %d"), m_IPAddress, m_lpbmi->bmiHeader.biWidth, m_lpbmi->bmiHeader.biHeight);
    SetWindowText(str);

    m_HScrollPos = 0;
    m_VScrollPos = 0;
    m_hRemoteCursor = LoadCursor(NULL, IDC_ARROW);

    ICONINFO CursorInfo;
    ::GetIconInfo(m_hRemoteCursor, &CursorInfo);
    if (CursorInfo.hbmMask != NULL)
        ::DeleteObject(CursorInfo.hbmMask);
    if (CursorInfo.hbmColor != NULL)
        ::DeleteObject(CursorInfo.hbmColor);
    m_dwCursor_xHotspot = CursorInfo.xHotspot;
    m_dwCursor_yHotspot = CursorInfo.yHotspot;

    m_RemoteCursorPos.x = 0;
    m_RemoteCursorPos.x = 0;
    m_bIsTraceCursor = false;

    // 初始化窗口大小结构
    m_hDC = ::GetDC(m_hWnd);
    m_hMemDC = CreateCompatibleDC(m_hDC);
    m_hFullBitmap = CreateDIBSection(m_hDC, m_lpbmi, DIB_RGB_COLORS, &m_lpScreenDIB, NULL, NULL);
    SelectObject(m_hMemDC, m_hFullBitmap);
    SetScrollRange(SB_HORZ, 0, m_lpbmi->bmiHeader.biWidth);
    SetScrollRange(SB_VERT, 0, m_lpbmi->bmiHeader.biHeight);

    InitMMI();
    SendNext();

    InitUI();
    CenterWindow(m_pFMGClient->m_UserMgr.m_hCallBackWnd);

    return TRUE;
}

BOOL CRemoteDesktopDlg::InitUI()
{
    m_SkinDlg.SetBgPic(_T("DlgBg\\FindFriendDlgBg.png"));
    m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
    m_SkinDlg.SubclassWindow(m_hWnd);
    m_SkinDlg.SetTitleText(_T("远程桌面"));
    m_SkinDlg.MoveWindow(0, 0, 350, 200, TRUE);


    HDC hDlgBgDC = m_SkinDlg.GetBgDC();

    //m_edtAddId.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2, 2, 2, 2));
    //m_edtAddId.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2, 2, 2, 2));
    //m_edtAddId.SetTransparent(TRUE, hDlgBgDC);
    //m_edtAddId.SubclassWindow(GetDlgItem(IDC_EDIT_ADD));

    //m_btnAdd.SetButtonType(SKIN_PUSH_BUTTON);
    //m_btnAdd.SetTransparent(TRUE, hDlgBgDC);
    //m_btnAdd.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"), _T("Button\\btn_focus.png"));
    //m_btnAdd.SetRound(4, 4);
    //m_btnAdd.SubclassWindow(GetDlgItem(IDC_BTN_ADD));
    //m_btnAdd.SetWindowText(_T("查找"));
    //m_btnAdd.SetToolTipText(_T("点击按钮进行查找"));

    //m_btnFindTypeSingle.Attach(GetDlgItem(IDC_RADIO_FINDTYPE1));
    //m_btnFindTypeSingle.SetCheck(TRUE);
    ////m_btnFindTypeSingle.SetTransparent(TRUE, hDlgBgDC);

    //m_btnFindTypeGroup.Attach(GetDlgItem(IDC_RADIO_FINDTYPE2));
    ////m_btnFindTypeGroup.SetTransparent(TRUE, hDlgBgDC);

    //m_staticAddInfo.SetTransparent(TRUE, hDlgBgDC);
    //m_staticAddInfo.SetLinkColor(RGB(225, 0, 0));
    //m_staticAddInfo.SetLinkType(SKIN_LINK_ADDNEW);
    //m_staticAddInfo.SubclassWindow(GetDlgItem(IDC_STATIC_ADDINFO));

    return TRUE;
}

void CRemoteDesktopDlg::OnAddFriend(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    
}

LRESULT CRemoteDesktopDlg::OnFindFriendResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    

    return (LRESULT)1;

}

HBRUSH CRemoteDesktopDlg::OnCtlColorStatic(CDCHandle dc, CStatic wndStatic)
{
    SetMsgHandled(FALSE);

    return 0;
}

void CRemoteDesktopDlg::OnDestroy()
{
    UninitUI();
}

void CRemoteDesktopDlg::UninitUI()
{
    
}

void CRemoteDesktopDlg::AddMessageFilter()
{
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->AddMessageFilter(this);
}

void CRemoteDesktopDlg::RemoveMessageFilter()
{
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->RemoveMessageFilter(this);
}

void CRemoteDesktopDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    if (bShow)
        AddMessageFilter();
    else
        RemoveMessageFilter();
}

void CRemoteDesktopDlg::OnClose()
{
    // TODO: Add your message handler code here and/or call default
    //m_pContext->m_Dialog[0] = 0;

    //closesocket(m_pContext->m_Socket);

    ::ReleaseDC(m_hWnd, m_hDC);
    DeleteObject(m_hFullBitmap);

    if (m_lpbmi)
        delete m_lpbmi;
    if (m_lpbmi_rect)
        delete m_lpbmi_rect;
    SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL, IDC_ARROW));

    m_bIsCtrl = false;

    //PostMessage(WM_CLOSE, 0, 0);

    //CDialog::OnClose();

    ShowWindow(SW_HIDE);
}

//void CRemoteDesktopDlg::DoDataExchange(CDataExchange* pDX)
//{
//    CDialog::DoDataExchange(pDX);
//    //{{AFX_DATA_MAP(CRemoteDesktopDlg)
//    // NOTE: the ClassWizard will add DDX and DDV calls here
//    //}}AFX_DATA_MAP
//}

void CRemoteDesktopDlg::OnReceiveComplete()
{
    m_nCount++;


    /*switch (m_pContext->m_DeCompressionBuffer.GetBuffer(0)[0])
    {
    case TOKEN_FIRSTSCREEN:
        DrawFirstScreen();
        break;
    case TOKEN_NEXTSCREEN:
        if (m_pContext->m_DeCompressionBuffer.GetBuffer(0)[1] == ALGORITHM_SCAN)
            DrawNextScreenRect();
        else
            DrawNextScreenDiff();
        break;
    case TOKEN_BITMAPINFO:
        ResetScreen();
        break;
    case TOKEN_CLIPBOARD_TEXT:
        UpdateLocalClipboard((char *)m_pContext->m_DeCompressionBuffer.GetBuffer(1), m_pContext->m_DeCompressionBuffer.GetBufferLen() - 1);
        break;
    default:
        // 传输发生异常数据
        return;
    }
    */
}


bool CRemoteDesktopDlg::SaveSnapshot()
{
    //CString	strFileName = m_IPAddress + CTime::GetCurrentTime().Format(_T("_%Y-%m-%d_%H-%M-%S.bmp"));
    //CFileDialog dlg(FALSE, _T("bmp"), strFileName, OFN_OVERWRITEPROMPT, _T("位图文件(*.bmp)|*.bmp|"), this);
    //if (dlg.DoModal() != IDOK)
    //    return false;

    //BITMAPFILEHEADER	hdr;
    //LPBITMAPINFO		lpbi = m_lpbmi;
    //CFile	file;
    //if (!file.Open(dlg.GetPathName(), CFile::modeWrite | CFile::modeCreate))
    //{
    //    MessageBox(_T("文件保存失败"));
    //    return false;
    //}

    //// BITMAPINFO大小
    //int	nbmiSize = sizeof(BITMAPINFOHEADER) + (lpbi->bmiHeader.biBitCount > 16 ? 1 : (1 << lpbi->bmiHeader.biBitCount)) * sizeof(RGBQUAD);

    //// Fill in the fields of the file header
    //hdr.bfType = ((WORD)('M' << 8) | 'B');	// is always "BM"
    //hdr.bfSize = lpbi->bmiHeader.biSizeImage + sizeof(hdr);
    //hdr.bfReserved1 = 0;
    //hdr.bfReserved2 = 0;
    //hdr.bfOffBits = sizeof(hdr) + nbmiSize;
    //// Write the file header
    //file.Write(&hdr, sizeof(hdr));
    //file.Write(lpbi, nbmiSize);
    //// Write the DIB header and the bits
    //file.Write(m_lpScreenDIB, lpbi->bmiHeader.biSizeImage);
    //file.Close();

    return false;
}

void CRemoteDesktopDlg::OnReceive()
{
    //if (m_pContext == NULL)
     //   return;

    CString str;
    str.Format(_T("\\\\%s %d * %d 第%d帧 %d%%"), m_IPAddress, m_lpbmi->bmiHeader.biWidth, m_lpbmi->bmiHeader.biHeight,
        m_nCount, 9999/*m_pContext->m_nTransferProgress*/);
    SetWindowText(str);
}

void CRemoteDesktopDlg::SendResetScreen(int	nBitCount)
{
    m_nBitCount = nBitCount;

    BYTE	bBuff[2];
    //bBuff[0] = COMMAND_SCREEN_RESET;
    bBuff[1] = m_nBitCount;
    //m_iocpServer->Send(m_pContext, bBuff, sizeof(bBuff));
}

void CRemoteDesktopDlg::SendResetAlgorithm(UINT nAlgorithm)
{
    BYTE	bBuff[2];
    //bBuff[0] = COMMAND_ALGORITHM_RESET;
    bBuff[1] = nAlgorithm;
    //m_iocpServer->Send(m_pContext, bBuff, sizeof(bBuff));
}

// void CRemoteDesktopDlg::FullScreen()
// {
// 	m_bIsFullScreen = !m_bIsFullScreen; // 设置全屏显示标志 
// 	//一种更好的全屏显示
// 	LONG style = ::GetWindowLong(this->m_hWnd,GWL_STYLE);
// 	if(m_bIsFullScreen)//全屏显示
// 	{
// 		style &= ~(WS_DLGFRAME | WS_THICKFRAME);
// 		SetWindowLong(this->m_hWnd, GWL_STYLE, style);
// 		//this->ShowWindow(SW_SHOWMAXIMIZED);
// 		CRect rect;
// 		this->GetWindowRect(&rect);
// 		::SetWindowPos(this->m_hWnd,HWND_NOTOPMOST,rect.left-1, rect.top-1, rect.right-rect.left + 3, rect.bottom-rect.top + 3, SWP_FRAMECHANGED);
// 	}
// 	else
// 	{
// 		style |= WS_DLGFRAME | WS_THICKFRAME;
// 		SetWindowLong(this->m_hWnd, GWL_STYLE, style);
// 		this->ShowWindow(SW_NORMAL);
// 	}
// }

void CRemoteDesktopDlg::ResetScreen()
{
    UINT	nBISize = 1064;  /*m_pContext->m_DeCompressionBuffer.GetBufferLen() - 1;*/
    if (m_lpbmi != NULL)
    {
        int	nOldWidth = m_lpbmi->bmiHeader.biWidth;
        int	nOldHeight = m_lpbmi->bmiHeader.biHeight;

        delete[] m_lpbmi;
        delete[] m_lpbmi_rect;

        m_lpbmi = (BITMAPINFO *) new BYTE[nBISize];
        m_lpbmi_rect = (BITMAPINFO *) new BYTE[nBISize];

        //memcpy(m_lpbmi, m_pContext->m_DeCompressionBuffer.GetBuffer(1), nBISize);
        //memcpy(m_lpbmi_rect, m_pContext->m_DeCompressionBuffer.GetBuffer(1), nBISize);

        DeleteObject(m_hFullBitmap);
        m_hFullBitmap = CreateDIBSection(m_hDC, m_lpbmi, DIB_RGB_COLORS, &m_lpScreenDIB, NULL, NULL);
        SelectObject(m_hMemDC, m_hFullBitmap);

        memset(&m_MMI, 0, sizeof(MINMAXINFO));
        InitMMI();

        // 分辨率发生改变
        if (nOldWidth != m_lpbmi->bmiHeader.biWidth || nOldHeight != m_lpbmi->bmiHeader.biHeight)
        {
            RECT	rectClient, rectWindow;
            GetWindowRect(&rectWindow);
            GetClientRect(&rectClient);
            ClientToScreen(&rectClient);

            // 计算ClientRect与WindowRect的差距（标题栏，滚动条）
            rectWindow.right = m_lpbmi->bmiHeader.biWidth + rectClient.left + (rectWindow.right - rectClient.right);
            rectWindow.bottom = m_lpbmi->bmiHeader.biHeight + rectClient.top + (rectWindow.bottom - rectClient.bottom);
            MoveWindow(&rectWindow);
        }
    }
}

void CRemoteDesktopDlg::DrawFirstScreen()
{
    m_bIsFirst = false;
    //memcpy(m_lpScreenDIB, m_pContext->m_DeCompressionBuffer.GetBuffer(1), m_lpbmi->bmiHeader.biSizeImage);

    HDC hDC = ::GetDC(m_hWnd);
    OnPaint(hDC);
}

void CRemoteDesktopDlg::DrawNextScreenDiff()
{
    //// 根据鼠标是否移动和屏幕是否变化判断是否重绘鼠标，防止鼠标闪烁
    //bool	bIsReDraw = false;
    //int		nHeadLength = 1 + 1 + sizeof(POINT) + sizeof(BYTE); // 标识 + 算法 + 光标位置 + 光标类型索引
    //LPVOID	lpFirstScreen = m_lpScreenDIB;
    ////LPVOID	lpNextScreen = m_pContext->m_DeCompressionBuffer.GetBuffer(nHeadLength);
    ////DWORD	dwBytes = m_pContext->m_DeCompressionBuffer.GetBufferLen() - nHeadLength;

    //POINT	oldPoint;
    //memcpy(&oldPoint, &m_RemoteCursorPos, sizeof(POINT));
    ////memcpy(&m_RemoteCursorPos, m_pContext->m_DeCompressionBuffer.GetBuffer(2), sizeof(POINT));

    //// 鼠标移动了
    //if (memcmp(&oldPoint, &m_RemoteCursorPos, sizeof(POINT)) != 0)
    //    bIsReDraw = true;

    //// 光标类型发生变化
    //int	nOldCursorIndex = m_bCursorIndex;
    ////m_bCursorIndex = m_pContext->m_DeCompressionBuffer.GetBuffer(10)[0];
    //if (nOldCursorIndex != m_bCursorIndex)
    //{
    //    bIsReDraw = true;
    //    if (m_bIsCtrl && !m_bIsTraceCursor)
    //        SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)m_CursorInfo.getCursorHandle(m_bCursorIndex == (BYTE)-1 ? 1 : m_bCursorIndex));
    //}

    //// 屏幕是否变化
    ////if (dwBytes > 0)
    ////    bIsReDraw = true;

    //__asm
    //{
    //    mov ebx, [dwBytes]
    //        mov esi, [lpNextScreen]
    //        jmp	CopyEnd
    //    CopyNextBlock :
    //    mov edi, [lpFirstScreen]
    //        lodsd	// 把lpNextScreen的第一个双字节，放到eax中,就是DIB中改变区域的偏移
    //        add edi, eax	// lpFirstScreen偏移eax	
    //        lodsd // 把lpNextScreen的下一个双字节，放到eax中, 就是改变区域的大小
    //        mov ecx, eax
    //        sub ebx, 8 // ebx 减去 两个dword
    //        sub ebx, ecx // ebx 减去DIB数据的大小
    //        rep movsb
    //    CopyEnd :
    //    cmp ebx, 0 // 是否写入完毕
    //        jnz CopyNextBlock
    //}

    //if (bIsReDraw)
    {
        HDC hDC = ::GetDC(m_hWnd);
        OnPaint(hDC);
    }
}

void CRemoteDesktopDlg::DrawNextScreenRect()
{
    // 根据鼠标是否移动和鼠标是否在变化的区域判断是否重绘鼠标，防止鼠标闪烁
    bool	bIsReDraw = false;
    int		nHeadLength = 1 + 1 + sizeof(POINT) + sizeof(BYTE); // 标识 + 算法 + 光标位置 + 光标类型索引
    LPVOID	lpFirstScreen = m_lpScreenDIB;
    //LPVOID	lpNextScreen = m_pContext->m_DeCompressionBuffer.GetBuffer(nHeadLength);
    //DWORD	dwBytes = m_pContext->m_DeCompressionBuffer.GetBufferLen() - nHeadLength;


    // 保存上次鼠标所在的位置
    RECT	rectOldPoint;
    ::SetRect(&rectOldPoint, m_RemoteCursorPos.x, m_RemoteCursorPos.y,
        m_RemoteCursorPos.x + m_dwCursor_xHotspot, m_RemoteCursorPos.y + m_dwCursor_yHotspot);

    //memcpy(&m_RemoteCursorPos, m_pContext->m_DeCompressionBuffer.GetBuffer(2), sizeof(POINT));

    //////////////////////////////////////////////////////////////////////////
    // 判断鼠标是否移动
    if ((rectOldPoint.left != m_RemoteCursorPos.x) || (rectOldPoint.top !=
        m_RemoteCursorPos.y))
        bIsReDraw = true;

    // 光标类型发生变化
    int	nOldCursorIndex = m_bCursorIndex;
    //m_bCursorIndex = m_pContext->m_DeCompressionBuffer.GetBuffer(10)[0];
    if (nOldCursorIndex != m_bCursorIndex)
    {
        bIsReDraw = true;
        if (m_bIsCtrl && !m_bIsTraceCursor)
            SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)m_CursorInfo.getCursorHandle(m_bCursorIndex == (BYTE)-1 ? 1 : m_bCursorIndex));
    }

    // 判断鼠标所在区域是否发生变化
    //DWORD	dwOffset = 0;
    //while (dwOffset < dwBytes && !bIsReDraw)
    //{
    //    LPRECT	lpRect = (LPRECT)((LPBYTE)lpNextScreen + dwOffset);
    //    RECT rectDest;
    //    if (IntersectRect(&rectDest, &rectOldPoint, lpRect))
    //        bIsReDraw = true;
    //    dwOffset += sizeof(RECT) + m_lpbmi_rect->bmiHeader.biSizeImage;
    //}
    //bIsReDraw = bIsReDraw && m_bIsTraceCursor;
    ////////////////////////////////////////////////////////////////////////////

    //dwOffset = 0;
    //while (dwOffset < dwBytes)
    //{
    //    LPRECT	lpRect = (LPRECT)((LPBYTE)lpNextScreen + dwOffset);
    //    int	nRectWidth = lpRect->right - lpRect->left;
    //    int	nRectHeight = lpRect->bottom - lpRect->top;

    //    m_lpbmi_rect->bmiHeader.biWidth = nRectWidth;
    //    m_lpbmi_rect->bmiHeader.biHeight = nRectHeight;
    //    m_lpbmi_rect->bmiHeader.biSizeImage = (((m_lpbmi_rect->bmiHeader.biWidth * m_lpbmi_rect->bmiHeader.biBitCount + 31) & ~31) >> 3)
    //        * m_lpbmi_rect->bmiHeader.biHeight;

    //    StretchDIBits(m_hMemDC, lpRect->left, lpRect->top, nRectWidth,
    //        nRectHeight, 0, 0, nRectWidth, nRectHeight, (LPBYTE)lpNextScreen + dwOffset + sizeof(RECT),
    //        m_lpbmi_rect, DIB_RGB_COLORS, SRCCOPY);

    //    // 不需要重绘鼠标的话，直接重绘变化的部分
    //    if (!bIsReDraw)
    //        StretchDIBits(m_hDC, lpRect->left - m_HScrollPos, lpRect->top - m_VScrollPos, nRectWidth,
    //        nRectHeight, 0, 0, nRectWidth, nRectHeight, (LPBYTE)lpNextScreen + dwOffset + sizeof(RECT),
    //        m_lpbmi_rect, DIB_RGB_COLORS, SRCCOPY);

    //    dwOffset += sizeof(RECT) + m_lpbmi_rect->bmiHeader.biSizeImage;
    //}

    if (bIsReDraw)
    {
        HDC hDC = ::GetDC(m_hWnd);
        OnPaint(hDC);
    }
}

void CRemoteDesktopDlg::OnPaint(CDCHandle dc)
{
    //CPaintDC dc(this); // device context for painting

    // TODO: Add your message handler code here
    if (m_bIsFirst)
    {
        DrawTipString("Please wait - initial screen loading");
        return;
    }

    BitBlt
        (
        m_hDC,
        0,
        0,
        m_lpbmi->bmiHeader.biWidth,
        m_lpbmi->bmiHeader.biHeight,
        m_hMemDC,
        m_HScrollPos,
        m_VScrollPos,
        SRCCOPY
        );

    // (BYTE)-1 = 255;
    // Draw the cursor
    if (m_bIsTraceCursor)
        DrawIconEx(
        m_hDC,									// handle to device context 
        m_RemoteCursorPos.x - ((int)m_dwCursor_xHotspot) - m_HScrollPos,
        m_RemoteCursorPos.y - ((int)m_dwCursor_yHotspot) - m_VScrollPos,
        m_CursorInfo.getCursorHandle(m_bCursorIndex == (BYTE)-1 ? 1 : m_bCursorIndex),	// handle to icon to draw 
        0, 0,										// width of the icon 
        0,											// index of frame in animated cursor 
        NULL,										// handle to background brush 
        DI_NORMAL | DI_COMPAT						// icon-drawing flags 
        );
    // Do not call CDialog::OnPaint() for painting messages
}

void CRemoteDesktopDlg::OnSize(UINT nType, CSize size)
{
    //CDialog::OnSize(nType, cx, cy);

    // TODO: Add your message handler code here
    if (!IsWindowVisible())
        return;
    RECT rect;
    GetClientRect(&rect);

    if ((rect.right + m_HScrollPos) > m_lpbmi->bmiHeader.biWidth)
        InterlockedExchange((PLONG)&m_HScrollPos, m_lpbmi->bmiHeader.biWidth - rect.right);

    if ((rect.bottom + m_VScrollPos) > m_lpbmi->bmiHeader.biHeight)
        InterlockedExchange((PLONG)&m_VScrollPos, m_lpbmi->bmiHeader.biHeight - rect.bottom);

    SetScrollPos(SB_HORZ, m_HScrollPos);
    SetScrollPos(SB_VERT, m_VScrollPos);

    if (rect.right >= m_lpbmi->bmiHeader.biWidth && rect.bottom >= m_lpbmi->bmiHeader.biHeight)
    {
        ShowScrollBar(SB_BOTH, false);
        InterlockedExchange((PLONG)&m_HScrollPos, 0);
        InterlockedExchange((PLONG)&m_VScrollPos, 0);
        RECT	rectClient, rectWindow;
        GetWindowRect(&rectWindow);
        GetClientRect(&rectClient);
        ClientToScreen(&rectClient);
        // 边框的宽度
        int	nBorderWidth = rectClient.left - rectWindow.left;

        rectWindow.right = rectClient.left + nBorderWidth + m_lpbmi->bmiHeader.biWidth;
        rectWindow.bottom = rectClient.top + m_lpbmi->bmiHeader.biHeight + nBorderWidth;
        MoveWindow(&rectWindow);
    }
    else ShowScrollBar(SB_BOTH, true);
}

void CRemoteDesktopDlg::OnSysCommand(UINT nID, CPoint point)
{
    //CMenu* pSysMenu = GetSystemMenu(FALSE);
    //switch (nID)
    //{
    //case IDM_CONTROL:
    //{
    //    m_bIsCtrl = !m_bIsCtrl;
    //    pSysMenu->CheckMenuItem(IDM_CONTROL, m_bIsCtrl ? MF_CHECKED : MF_UNCHECKED);

    //    if (m_bIsCtrl)
    //    {
    //        if (m_bIsTraceCursor)
    //            SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)AfxGetApp()->LoadCursor(IDC_DOT));
    //        else
    //            SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)m_hRemoteCursor);
    //    }
    //    else
    //        SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL, IDC_NO));
    //}
    //break;
    //case IDM_SEND_CTRL_ALT_DEL:
    //{
    //    BYTE	bToken = COMMAND_SCREEN_CTRL_ALT_DEL;
    //    m_iocpServer->Send(m_pContext, &bToken, sizeof(bToken));
    //}
    //break;
    //case IDM_TRACE_CURSOR: // 跟踪服务端鼠标
    //{
    //    m_bIsTraceCursor = !m_bIsTraceCursor;
    //    pSysMenu->CheckMenuItem(IDM_TRACE_CURSOR, m_bIsTraceCursor ? MF_CHECKED : MF_UNCHECKED);
    //    if (m_bIsCtrl)
    //    {
    //        if (!m_bIsTraceCursor)
    //            SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)m_hRemoteCursor);
    //        else
    //            SetClassLong(m_hWnd, GCL_HCURSOR, (LONG)AfxGetApp()->LoadCursor(IDC_DOT));
    //    }
    //    // 重绘消除或显示鼠标
    //    OnPaint();
    //}
    //break;
    //case IDM_BLOCK_INPUT: // 锁定服务端鼠标和键盘
    //{
    //    bool bIsChecked = pSysMenu->GetMenuState(IDM_BLOCK_INPUT, MF_BYCOMMAND) & MF_CHECKED;
    //    pSysMenu->CheckMenuItem(IDM_BLOCK_INPUT, bIsChecked ? MF_UNCHECKED : MF_CHECKED);

    //    BYTE	bToken[2];
    //    bToken[0] = COMMAND_SCREEN_BLOCK_INPUT;
    //    bToken[1] = !bIsChecked;
    //    m_iocpServer->Send(m_pContext, bToken, sizeof(bToken));
    //}
    //break;
    //case IDM_BLANK_SCREEN: // 服务端黑屏
    //{
    //    bool bIsChecked = pSysMenu->GetMenuState(IDM_BLANK_SCREEN, MF_BYCOMMAND) & MF_CHECKED;
    //    pSysMenu->CheckMenuItem(IDM_BLANK_SCREEN, bIsChecked ? MF_UNCHECKED : MF_CHECKED);

    //    BYTE	bToken[2];
    //    bToken[0] = COMMAND_SCREEN_BLANK;
    //    bToken[1] = !bIsChecked;
    //    m_iocpServer->Send(m_pContext, bToken, sizeof(bToken));
    //}
    //break;
    //case IDM_CAPTURE_LAYER: // 捕捉层
    //{
    //    bool bIsChecked = pSysMenu->GetMenuState(IDM_CAPTURE_LAYER, MF_BYCOMMAND) & MF_CHECKED;
    //    pSysMenu->CheckMenuItem(IDM_CAPTURE_LAYER, bIsChecked ? MF_UNCHECKED : MF_CHECKED);

    //    BYTE	bToken[2];
    //    bToken[0] = COMMAND_SCREEN_CAPTURE_LAYER;
    //    bToken[1] = !bIsChecked;
    //    m_iocpServer->Send(m_pContext, bToken, sizeof(bToken));
    //}
    //break;
    //case IDM_SAVEDIB:
    //    SaveSnapshot();
    //    break;
    //case IDM_GET_CLIPBOARD: // 获取剪贴板
    //{
    //    BYTE	bToken = COMMAND_SCREEN_GET_CLIPBOARD;
    //    m_iocpServer->Send(m_pContext, &bToken, sizeof(bToken));
    //}
    //break;
    //case IDM_SET_CLIPBOARD: // 设置剪贴板
    //{
    //    SendLocalClipboard();
    //}
    //break;
    //case IDM_ALGORITHM_SCAN: // 隔行扫描算法
    //{
    //    SendResetAlgorithm(ALGORITHM_SCAN);
    //    pSysMenu->CheckMenuRadioItem(IDM_ALGORITHM_SCAN, IDM_ALGORITHM_DIFF, IDM_ALGORITHM_SCAN, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_ALGORITHM_DIFF: // 差异比较算法
    //{
    //    SendResetAlgorithm(ALGORITHM_DIFF);
    //    pSysMenu->CheckMenuRadioItem(IDM_ALGORITHM_SCAN, IDM_ALGORITHM_DIFF, IDM_ALGORITHM_DIFF, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_1:
    //{
    //    SendResetScreen(1);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_1, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_4_GRAY:
    //{
    //    SendResetScreen(3);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_4_GRAY, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_4_COLOR:
    //{
    //    SendResetScreen(4);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_4_COLOR, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_8_GRAY:
    //{
    //    SendResetScreen(7);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_8_GRAY, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_8_COLOR:
    //{
    //    SendResetScreen(8);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_8_COLOR, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_16:
    //{
    //    SendResetScreen(16);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_1, IDM_DEEP_32, IDM_DEEP_16, MF_BYCOMMAND);
    //}
    //break;
    //case IDM_DEEP_32:
    //{
    //    SendResetScreen(32);
    //    pSysMenu->CheckMenuRadioItem(IDM_DEEP_4_GRAY, IDM_DEEP_32, IDM_DEEP_32, MF_BYCOMMAND);
    //}
    //break;
    //default:
    //    //CDialog::OnSysCommand(nID, lParam);
    //}
}

void CRemoteDesktopDlg::OnGetMiniMaxInfo(LPMINMAXINFO lpMMI)
{
    // 如果m_MMI已经被赋值
    //if (m_MMI.ptMaxSize.x > 0)
     //   memcpy((void *)lparam, &m_MMI, sizeof(MINMAXINFO));

}

void CRemoteDesktopDlg::DrawTipString(CString str)
{
    RECT rect;
    GetClientRect(&rect);
    COLORREF bgcol = RGB(0x00, 0x00, 0x00);
    COLORREF oldbgcol = SetBkColor(m_hDC, bgcol);
    COLORREF oldtxtcol = SetTextColor(m_hDC, RGB(0xff, 0x00, 0x00));
    ExtTextOut(m_hDC, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);

    DrawText(m_hDC, str, -1, &rect,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SetBkColor(m_hDC, oldbgcol);
    SetTextColor(m_hDC, oldtxtcol);
    /*	InvalidateRect(NULL, FALSE);*/
}

void CRemoteDesktopDlg::InitMMI()
{
    RECT	rectClient, rectWindow;
    GetWindowRect(&rectWindow);
    GetClientRect(&rectClient);
    ClientToScreen(&rectClient);

    int	nBorderWidth = rectClient.left - rectWindow.left; // 边框宽
    int	nTitleWidth = rectClient.top - rectWindow.top; // 标题栏的高度

    int	nWidthAdd = nBorderWidth * 2 + GetSystemMetrics(SM_CYHSCROLL);
    int	nHeightAdd = nTitleWidth + nBorderWidth + GetSystemMetrics(SM_CYVSCROLL);
    int	nMinWidth = 400 + nWidthAdd;
    int	nMinHeight = 300 + nHeightAdd;
    int	nMaxWidth = m_lpbmi->bmiHeader.biWidth + nWidthAdd;
    int	nMaxHeight = m_lpbmi->bmiHeader.biHeight + nHeightAdd;


    // 最小的Track尺寸
    m_MMI.ptMinTrackSize.x = nMinWidth;
    m_MMI.ptMinTrackSize.y = nMinHeight;

    // 最大化时窗口的位置
    m_MMI.ptMaxPosition.x = 1;
    m_MMI.ptMaxPosition.y = 1;

    // 窗口最大尺寸
    m_MMI.ptMaxSize.x = nMaxWidth;
    m_MMI.ptMaxSize.y = nMaxHeight;

    // 最大的Track尺寸也要改变
    m_MMI.ptMaxTrackSize.x = nMaxWidth;
    m_MMI.ptMaxTrackSize.y = nMaxHeight;
}

void CRemoteDesktopDlg::PostNcDestroy()
{
    // TODO: Add your specialized code here and/or call the base class
    delete this;
    //__super::PostNcDestroy();
}

void CRemoteDesktopDlg::SendCommand(MSG* pMsg)
{
    if (!m_bIsCtrl)
        return;

    //LPBYTE lpData = new BYTE[sizeof(MSG) + 1];
    //lpData[0] = COMMAND_SCREEN_CONTROL;
    //memcpy(lpData + 1, pMsg, sizeof(MSG));
    //m_iocpServer->Send(m_pContext, lpData, sizeof(MSG) + 1);

    //delete[] lpData;
}

void CRemoteDesktopDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
{
    // TODO: Add your message handler code here and/or call default

    SCROLLINFO si;
    int	i;
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_HORZ, &si);

    switch (nSBCode)
    {
    case SB_LINEUP:
        i = nPos - 1;
        break;
    case SB_LINEDOWN:
        i = nPos + 1;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        i = si.nTrackPos;
        break;
    default:
        return;
    }

    i = max(i, si.nMin);
    i = min(i, (int)(si.nMax - si.nPage + 1));

    RECT rect;
    GetClientRect(&rect);

    if ((rect.right + i) > m_lpbmi->bmiHeader.biWidth)
        i = m_lpbmi->bmiHeader.biWidth - rect.right;

    InterlockedExchange((PLONG)&m_HScrollPos, i);

    SetScrollPos(SB_HORZ, m_HScrollPos);

    HDC hdc = ::GetDC(m_hWnd);
    OnPaint(hdc);
    
    //__super::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CRemoteDesktopDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
{
    // TODO: Add your message handler code here and/or call default
    SCROLLINFO si;
    int	i;
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_VERT, &si);

    switch (nSBCode)
    {
    case SB_LINEUP:
        i = nPos - 1;
        break;
    case SB_LINEDOWN:
        i = nPos + 1;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        i = si.nTrackPos;
        break;
    default:
        return;
    }

    i = max(i, si.nMin);
    i = min(i, (int)(si.nMax - si.nPage + 1));


    RECT rect;
    GetClientRect(&rect);

    if ((rect.bottom + i) > m_lpbmi->bmiHeader.biHeight)
        i = m_lpbmi->bmiHeader.biHeight - rect.bottom;

    InterlockedExchange((PLONG)&m_VScrollPos, i);

    SetScrollPos(SB_VERT, i);

    HDC hdc = ::GetDC(m_hWnd);
    OnPaint(hdc);
    
    //CDialog::OnVScroll(nSBCode, nPos, pScrollBar);
}

void CRemoteDesktopDlg::UpdateLocalClipboard(char *buf, int len)
{
    if (!::OpenClipboard(NULL))
        return;

    ::EmptyClipboard();
    HGLOBAL hglbCopy = GlobalAlloc(GPTR, len);
    if (hglbCopy != NULL) {
        // Lock the handle and copy the text to the buffer.  
        LPTSTR lptstrCopy = (LPTSTR)GlobalLock(hglbCopy);
        memcpy(lptstrCopy, buf, len);
        GlobalUnlock(hglbCopy);          // Place the handle on the clipboard.  
        SetClipboardData(CF_TEXT, hglbCopy);
        GlobalFree(hglbCopy);
    }
    CloseClipboard();
}

void CRemoteDesktopDlg::SendLocalClipboard()
{
    if (!::OpenClipboard(NULL))
        return;
    HGLOBAL hglb = GetClipboardData(CF_TEXT);
    if (hglb == NULL)
    {
        ::CloseClipboard();
        return;
    }
    int	nPacketLen = GlobalSize(hglb) + 1;
    LPSTR lpstr = (LPSTR)GlobalLock(hglb);
    LPBYTE	lpData = new BYTE[nPacketLen];
    //lpData[0] = COMMAND_SCREEN_SET_CLIPBOARD;
    memcpy(lpData + 1, lpstr, nPacketLen - 1);
    ::GlobalUnlock(hglb);
    ::CloseClipboard();
    //m_iocpServer->Send(m_pContext, lpData, nPacketLen);
    delete[] lpData;
}

void CRemoteDesktopDlg::SendNext()
{
    //BYTE	bBuff = COMMAND_NEXT;
    //m_iocpServer->Send(m_pContext, &bBuff, 1);
}