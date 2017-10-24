#include "stdafx.h"
#include "RemoteDesktopDlg.h"
#include "FlamingoClient.h"
#include "UserSnapInfoDlg.h"
#include "Utils.h"
#include "EncodingUtil.h"

// CRemoteDesktopDlg实现代码
CRemoteDesktopDlg::CRemoteDesktopDlg()
{
    m_pFMGClient = NULL;
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

    return CWindow::IsDialogMessage(pMsg);
}

BOOL CRemoteDesktopDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
    AddMessageFilter();

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

void CRemoteDesktopDlg::OnClose()
{
    ShowWindow(SW_HIDE);
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