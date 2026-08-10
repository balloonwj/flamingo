/**
 * 远程桌面窗口
 * zhangyl 2017.09.07
 */
#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "UserSessionData.h"
#include "RemoteDesktop/CursorInfo.h"


class CFlamingoClient;

//远程桌面窗口（早期是从"查找并添加好友"对话框抄下来改的，故仍沿用 IDD_REMOTEDESKTOP
//之外的一些旧命名与皮肤图 DlgBg\FindFriendDlgBg.png；好友那套逻辑已随 v1 好友体系删除）
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
        MSG_WM_SYSCOMMAND(OnSysCommand)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_GETMINMAXINFO(OnGetMiniMaxInfo)
        MSG_WM_HSCROLL(OnHScroll)
        MSG_WM_VSCROLL(OnVScroll)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
    END_MSG_MAP()

protected:
    //virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    //

protected:    
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
    void OnGetMiniMaxInfo(LPMINMAXINFO lpMMI);
    void OnSysCommand(UINT nID, CPoint point);
    void OnPaint(CDCHandle dc);
    void OnSize(UINT nType, CSize size);
    void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
    void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
    void OnShowWindow(BOOL bShow, UINT nStatus);
    void OnClose();
    void OnDestroy();
    HBRUSH OnCtlColorStatic(CDCHandle dc, CStatic wndStatic);

    virtual void PostNcDestroy();

    BOOL InitUI();
    void UninitUI();

    void AddMessageFilter();
    void RemoveMessageFilter();

private:
    void DrawTipString(CString str);
    void ResetScreen();
    void DrawFirstScreen();
    void DrawNextScreenDiff();	// 差异法
    void DrawNextScreenRect();	// 隔行扫描法
    void SendResetScreen(int nBitCount);
    void SendResetAlgorithm(UINT nAlgorithm);
    bool SaveSnapshot();
    void UpdateLocalClipboard(char *buf, int len);
    void SendLocalClipboard();

    void SendNext();
    void SendCommand(MSG* pMsg);
    void InitMMI();

    void OnReceiveComplete();
    void OnReceive();
    

public:
    CFlamingoClient*		m_pFMGClient;

private:
    CSkinDialog			    m_SkinDlg;

private:
    HICON                   m_hIcon;
    MINMAXINFO              m_MMI;
    
    HDC                     m_hDC;
    HDC                     m_hMemDC;
    HDC                     m_hPaintDC;
    HBITMAP	                m_hFullBitmap;
    LPVOID                  m_lpScreenDIB;
    LPBITMAPINFO            m_lpbmi;
    LPBITMAPINFO            m_lpbmi_rect;
    UINT                    m_nCount;
    UINT                    m_HScrollPos;
    UINT                    m_VScrollPos;
    HCURSOR	                m_hRemoteCursor;
    DWORD	                m_dwCursor_xHotspot;
    DWORD                   m_dwCursor_yHotspot;
    POINT	                m_RemoteCursorPos;
    BYTE	                m_bCursorIndex;
    CCursorInfo	            m_CursorInfo;
   
    int	                    m_nBitCount;
    bool                    m_bIsFirst;
    bool                    m_bIsTraceCursor;
    //ClientContext*          m_pContext;
    //CIOCPServer*            m_iocpServer;
    CString                 m_IPAddress;
    bool                    m_bIsCtrl;  
};
