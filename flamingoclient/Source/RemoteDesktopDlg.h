/**
 * 远程桌面窗口
 * zhangyl 2017.09.07
 */
#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "UserSessionData.h"


class CFlamingoClient;

//查找还有并添加好友对话框
class CRemoteDesktopDlg : public CDialogImpl<CRemoteDesktopDlg>, public CMessageFilter
{
public:
    CRemoteDesktopDlg();
    virtual ~CRemoteDesktopDlg();

    virtual BOOL PreTranslateMessage(MSG* pMsg);

    //在资源文件里面增加对话框资源模板
    enum { IDD = IDD_REMOTEDESKTOP };

    BEGIN_MSG_MAP_EX(CRemoteDesktopDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_SHOWWINDOW(OnShowWindow)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
        COMMAND_ID_HANDLER_EX(IDC_BTN_ADD, OnAddFriend)
        MESSAGE_HANDLER(FMG_MSG_FINDFREIND, OnFindFriendResult)
        END_MSG_MAP()

protected:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
    void OnShowWindow(BOOL bShow, UINT nStatus);
    void OnClose();
    void OnDestroy();
    void OnAddFriend(UINT uNotifyCode, int nID, CWindow wndCtl);
    LRESULT OnFindFriendResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    HBRUSH OnCtlColorStatic(CDCHandle dc, CStatic wndStatic);

    BOOL InitUI();
    void UninitUI();

    void AddMessageFilter();
    void RemoveMessageFilter();

public:
    CFlamingoClient*		m_pFMGClient;

private:
    CSkinDialog			m_SkinDlg;
};