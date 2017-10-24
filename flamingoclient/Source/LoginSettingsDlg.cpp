#include "stdafx.h"
#include "UserSessionData.h"
#include "FlamingoClient.h"
#include "../IniFile.h"
#include "UIText.h"
#include "LoginSettingsDlg.h"

enum PROXY_TYPE
{
	NOT_USE_PROXY     = 0,
	USE_BROWSER_PROXY = 1,
	USE_HTTP_PROXY    = 2,
	USE_SOCKS5_PROXY  = 3
};

CLoginSettingsDlg::CLoginSettingsDlg()
{
	memset(m_szSrvAddr, 0, sizeof(m_szSrvAddr));
	memset(m_szFileSrvAddr, 0, sizeof(m_szFileSrvAddr));
    memset(m_szImgSrvAddr, 0, sizeof(m_szImgSrvAddr));

	memset(m_szSrvPort, 0, sizeof(m_szSrvPort));
    memset(m_szFileSrvAddr, 0, sizeof(m_szFileSrvAddr));
    memset(m_szImgPort, 0, sizeof(m_szImgPort));

	memset(m_szProxyAddr, 0, sizeof(m_szProxyAddr));
	memset(m_szProxyPort, 0, sizeof(m_szProxyPort));
	m_pClient = NULL;
};

CLoginSettingsDlg::~CLoginSettingsDlg()
{

};

BOOL CLoginSettingsDlg::PreTranslateMessage(MSG* pMsg)
{
	//TODO: 奇怪为什么只有焦点在控件上才走这个逻辑？
	//支持Esc键关闭对话框
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam==VK_ESCAPE)
 	{
		PostMessage(WM_COMMAND, (WPARAM)ID_LOGINSETTINGCANCEL, 0);
		return TRUE;
 	}

	return CWindow::IsDialogMessage(pMsg);
}


BOOL CLoginSettingsDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	
	// center the dialog on the screen
	InitUI();
	//ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);
	CenterWindow();
	return TRUE;
}


BOOL CLoginSettingsDlg::InitUI()
{
	m_SkinDlg.SetBgPic(_T("DlgBg\\LoginSettingDlgBg.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), _T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("网络设置"));
	m_SkinDlg.MoveWindow(0, 0, 550, 380, TRUE);
	

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();
	//m_staticSrvAddr.SubclassWindow(GetDlgItem(IDC_STATIC_SERADDRESS));
	//m_staticSrvAddr.SetTransparent(TRUE, hDlgBgDC);
	//m_staticSrvAddr.MoveWindow(60, 65, 60, 25, TRUE);
	//m_staticSrvAddr.SetFocus();

	//m_staticSrvPort.SubclassWindow(GetDlgItem(IDC_STATIC_SERPORT));
	//m_staticSrvPort.SetTransparent(TRUE, hDlgBgDC);
	//m_staticSrvPort.MoveWindow(215, 65, 60, 25, TRUE);

	//m_staticFileSrvAddr.SubclassWindow(GetDlgItem(IDC_STATIC_FILESERVER));
	//m_staticFileSrvAddr.SetTransparent(TRUE, hDlgBgDC);
	//m_staticFileSrvAddr.MoveWindow(250, 65, 60, 25, TRUE);

	//m_staticFilePort.SubclassWindow(GetDlgItem(IDC_STATIC_FILEPORT));
	//m_staticFilePort.SetTransparent(TRUE, hDlgBgDC);
	//m_staticFilePort.MoveWindow(370, 65, 60, 25, TRUE);

	m_comboProxyType.SubclassWindow(GetDlgItem(IDC_COMBO_PROTYPE));
	const TCHAR szProxyType[][10] = 
	{
		_T("不使用代理"),
		_T("使用浏览器设置"),
		_T("HTTP代理"),
		_T("SOCKS5代理")	
	};
	for(long i=0; i<ARRAYSIZE(szProxyType); ++i)
	{
		m_comboProxyType.InsertString(i, szProxyType[i]);
	}

	m_comboProxyType.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_comboProxyType.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_comboProxyType.SetArrowNormalPic(_T("ComboBox\\inputbtn_normal.png"));
	m_comboProxyType.SetArrowHotPic(_T("ComboBox\\inputbtn_highlight.png"));
	m_comboProxyType.SetArrowPushedPic(_T("ComboBox\\inputbtn_down.png"));
	
	m_comboProxyType.SetArrowWidth(28);
	m_comboProxyType.SetTransparent(TRUE, hDlgBgDC);
	//m_comboProxyType.MoveWindow(55, 180, 127, 30, FALSE);
	m_comboProxyType.SetItemHeight(-1, 26);
	//TODO: 设为只读以后背景上就会出现一个账号（奇怪的bug！）
	//m_comboProxyType.SetReadOnly(TRUE);

	CIniFile iniFile;
	CString strIniPath(g_szHomePath);
	strIniPath += _T("config\\flamingo.ini");
	long nSel = iniFile.ReadInt(_T("server"), _T("proxyType"), 0, strIniPath);
	if(nSel == -1)
		nSel = 0;
	m_comboProxyType.SetCurSel(nSel);
	
	
	m_editSrvAddr.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_editSrvAddr.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_editSrvAddr.SetTransparent(TRUE, hDlgBgDC);
	m_editSrvAddr.SubclassWindow(GetDlgItem(IDC_EDIT_SERADDRESS));
	//m_editSrvAddr.MoveWindow(55, 90, 127, 30, TRUE);
	CString strTemp;
	iniFile.ReadString(_T("server"), _T("server"), _T("flamingo.hootina.org"), strTemp.GetBuffer(64), 64, strIniPath);
	strTemp.ReleaseBuffer();
	m_editSrvAddr.SetWindowText(strTemp);

	m_editFileSrvAddr.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_editFileSrvAddr.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_editFileSrvAddr.SetTransparent(TRUE, hDlgBgDC);
	m_editFileSrvAddr.SubclassWindow(GetDlgItem(IDC_EDIT_FILESERVER));
	iniFile.ReadString(_T("server"), _T("fileserver"), _T("flamingo.hootina.org"), strTemp.GetBuffer(64), 64, strIniPath);
	strTemp.ReleaseBuffer();
	m_editFileSrvAddr.SetWindowText(strTemp);

    m_editImgSrvAddr.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2, 2, 2, 2));
    m_editImgSrvAddr.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2, 2, 2, 2));
    m_editImgSrvAddr.SetTransparent(TRUE, hDlgBgDC);
    m_editImgSrvAddr.SubclassWindow(GetDlgItem(IDC_EDIT_IMGSERVER));
    iniFile.ReadString(_T("server"), _T("imgserver"), _T("flamingo.hootina.org"), strTemp.GetBuffer(64), 64, strIniPath);
    strTemp.ReleaseBuffer();
    m_editImgSrvAddr.SetWindowText(strTemp);

	m_editSrvPort.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_editSrvPort.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_editSrvPort.SubclassWindow(GetDlgItem(IDC_EDIT_SERPORT));
	//m_edtSrvPort.MoveWindow(215, 90, 126, 30, TRUE);
	iniFile.ReadString(_T("server"), _T("port"), _T("20000"), strTemp.GetBuffer(32), 32, strIniPath);
	strTemp.ReleaseBuffer();
	m_editSrvPort.SetWindowText(strTemp);

	m_editFilePort.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
    m_editFilePort.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2, 2, 2, 2));
    m_editFilePort.SubclassWindow(GetDlgItem(IDC_EDIT_FILEPORT));
	//m_edtFilePort.MoveWindow(370, 90, 126, 30, TRUE);
	iniFile.ReadString(_T("server"), _T("fileport"), _T("20001"), strTemp.GetBuffer(32), 32, strIniPath);
	strTemp.ReleaseBuffer();
	m_editFilePort.SetWindowText(strTemp);

    m_editImgPort.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2, 2, 2, 2));
    m_editImgPort.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2, 2, 2, 2));
    m_editImgPort.SubclassWindow(GetDlgItem(IDC_EDIT_IMGPORT));
    iniFile.ReadString(_T("server"), _T("imgport"), _T("20002"), strTemp.GetBuffer(32), 32, strIniPath);
    strTemp.ReleaseBuffer();
    m_editImgPort.SetWindowText(strTemp);
	
	m_editProxyAddr.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_editProxyAddr.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_editProxyAddr.SubclassWindow(GetDlgItem(IDC_EDIT_PROADD));
	m_editProxyAddr.SetTransparent(TRUE, hDlgBgDC);
	//m_editProxyAddr.MoveWindow(212, 180, 126, 30, TRUE);
	if(nSel > USE_BROWSER_PROXY)
	{
		iniFile.ReadString(_T("server"), _T("proxyserver"), _T(""), strTemp.GetBuffer(32), 32, strIniPath);
		strTemp.ReleaseBuffer();
		m_editProxyAddr.SetWindowText(strTemp);
	}

	m_editProxyPort.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_editProxyPort.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));
	m_editProxyPort.SubclassWindow(GetDlgItem(IDC_EDIT_PROPORT));
	//m_edtProxyPort.MoveWindow(368, 180, 127, 30, TRUE);
	if(nSel > USE_BROWSER_PROXY)
	{
		iniFile.ReadString(_T("server"), _T("proxyport"), _T(""), strTemp.GetBuffer(32), 32, strIniPath);
		strTemp.ReleaseBuffer();
		m_editProxyPort.SetWindowText(strTemp);
	}

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"), _T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SetRound(4, 4);
	m_btnOK.SubclassWindow(GetDlgItem(ID_LOGINSETTING));
	//m_btnOK.MoveWindow(165, 265, 94, 30, TRUE);

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"), _T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SetRound(4, 4);
	m_btnCancel.SubclassWindow(GetDlgItem(ID_LOGINSETTINGCANCEL));
	//m_btnCancel.MoveWindow(290, 265, 94, 30, TRUE);

	BOOL bEnabled = (m_comboProxyType.GetCurSel() > USE_BROWSER_PROXY ? TRUE : FALSE);
	m_editProxyAddr.EnableWindow(bEnabled);
	m_editProxyPort.EnableWindow(bEnabled);

	return TRUE;
}

void CLoginSettingsDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CLoginSettingsDlg::OnDestroy()
{
	UninitUI();

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
}

void CLoginSettingsDlg::OnComboBox_Select(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	BOOL bEnabled = (m_comboProxyType.GetCurSel() > USE_BROWSER_PROXY ? TRUE : FALSE);
	m_editProxyAddr.EnableWindow(bEnabled);
	m_editProxyPort.EnableWindow(bEnabled);
}

void CLoginSettingsDlg::UninitUI()
{
	//if (m_editSrvAddr.IsWindow())
	//	m_editSrvAddr.DestroyWindow();

	//if (m_editFileSrvAddr.IsWindow())
	//	m_editFileSrvAddr.DestroyWindow();

	//if (m_editSrvPort.IsWindow())
	//	m_editSrvPort.DestroyWindow();

	//if (m_editProxyAddr.IsWindow())
	//	m_editProxyAddr.DestroyWindow();

	//if (m_editProxyPort.IsWindow())
	//	m_editProxyPort.DestroyWindow();

	//if (m_btnOK.IsWindow())
	//	m_btnOK.DestroyWindow();

	//if (m_btnCancel.IsWindow())
	//	m_btnCancel.DestroyWindow();

}

void CLoginSettingsDlg::OnBtn_OK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_editSrvAddr.GetWindowText(m_szSrvAddr, MAX_SRV_ADDR);
	if(m_szSrvAddr[0]==NULL || m_szSrvAddr[0]==_T(' '))
	{
		::MessageBox(m_hWnd, _T("聊天服务地址不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		m_editSrvAddr.SetFocus();
		return;
	}

	m_editFileSrvAddr.GetWindowText(m_szFileSrvAddr, MAX_SRV_ADDR);
	if(m_szFileSrvAddr[0]==NULL || m_szFileSrvAddr[0]==_T(' '))
	{
		::MessageBox(m_hWnd, _T("文件服务地址不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		m_editFileSrvAddr.SetFocus();
		return;
	}

    m_editImgSrvAddr.GetWindowText(m_szImgSrvAddr, MAX_SRV_ADDR);
    if (m_szImgSrvAddr[0] == NULL || m_szImgSrvAddr[0] == _T(' '))
    {
        ::MessageBox(m_hWnd, _T("图片服务地址不能为空！"), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        m_editImgSrvAddr.SetFocus();
        return;
    }

	m_editSrvPort.GetWindowText(m_szSrvPort, MAX_SRV_PORT);
	if(m_szSrvPort[0]==NULL || m_szSrvPort[0]==_T(' '))
	{
		::MessageBox(m_hWnd, _T("聊天服务端口号不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		m_editSrvPort.SetFocus();
		return;
	}

	m_editFilePort.GetWindowText(m_szFilePort, MAX_SRV_PORT);
	if(m_szFilePort[0]==NULL || m_szFilePort[0]==_T(' '))
	{
		::MessageBox(m_hWnd, _T("文件服务端口号不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
		m_editFilePort.SetFocus();
		return;
	}

    m_editImgPort.GetWindowText(m_szImgPort, MAX_SRV_PORT);
    if (m_szImgPort[0] == NULL || m_szImgPort[0] == _T(' '))
    {
        ::MessageBox(m_hWnd, _T("图片服务端口号不能为空！"), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        m_editImgPort.SetFocus();
        return;
    }
	
	long nCurSel = m_comboProxyType.GetCurSel();
	if(nCurSel > USE_BROWSER_PROXY)			
	{
		m_editProxyAddr.GetWindowText(m_szProxyAddr, MAX_SRV_ADDR);
		if(m_szProxyAddr[0]==NULL || m_szProxyAddr[0]==_T(' '))
		{
			::MessageBox(m_hWnd, _T("代理服务器地址不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
			m_editProxyAddr.SetFocus();
			return;
		}

		m_editProxyPort.GetWindowText(m_szProxyPort, MAX_SRV_PORT);
		if(m_szProxyPort[0]==NULL || m_szProxyPort[0]==_T(' '))
		{
			::MessageBox(m_hWnd, _T("代理端口号不能为空！"), g_strAppTitle.c_str(), MB_OK|MB_ICONINFORMATION);
			m_editProxyPort.SetFocus();
			return;
		}
	}

	CIniFile iniFile;
	CString strIniPath(g_szHomePath);
	strIniPath += _T("config\\flamingo.ini");
	iniFile.WriteString(_T("server"), _T("server"), m_szSrvAddr, strIniPath); 
	iniFile.WriteString(_T("server"), _T("fileserver"), m_szFileSrvAddr, strIniPath); 
    iniFile.WriteString(_T("server"), _T("imgserver"), m_szImgSrvAddr, strIniPath);
	iniFile.WriteString(_T("server"), _T("port"), m_szSrvPort, strIniPath); 
	iniFile.WriteString(_T("server"), _T("fileport"), m_szFilePort, strIniPath);
    iniFile.WriteString(_T("server"), _T("imgport"), m_szImgPort, strIniPath);
	
	iniFile.WriteInt(_T("server"), _T("proxytype"), nCurSel, strIniPath);
	if(nCurSel > USE_BROWSER_PROXY)
	{
		iniFile.WriteString(_T("server"), _T("proxyserver"), m_szProxyAddr, strIniPath); 
		iniFile.WriteString(_T("server"), _T("proxyport"), m_szProxyPort, strIniPath);
	}

	m_pClient->SetServer(m_szSrvAddr);
	m_pClient->SetFileServer(m_szFileSrvAddr);
    m_pClient->SetImgServer(m_szImgSrvAddr);
	m_pClient->SetPort((short)_wtol(m_szSrvPort));
    m_pClient->SetFilePort((short)_wtol(m_szFilePort));
    m_pClient->SetImgPort((short)_wtol(m_szImgPort));
	//m_pClient->m_IUProtocol.SetProxyType(nCurSel);
	//if(nCurSel > USE_BROWSER_PROXY)
	//{
	//	m_pClient->m_IUProtocol.SetProxyServer(m_szProxyAddr);
	//	m_pClient->m_IUProtocol.SetProxyPort(_wtol(m_szProxyPort));
	//}
	

	EndDialog(IDOK);
}

void CLoginSettingsDlg::OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl)	
{
	EndDialog(IDCANCEL);
}