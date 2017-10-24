/**
 * 可显示网页的控件，SkinWebBrowser.h
 * zhangyl 2017.08.02
 */
#pragma once

class CSkinWebBrowser : public CWindowImpl<CSkinWebBrowser, CWindow>
{
public:
    CSkinWebBrowser();
    ~CSkinWebBrowser();

    BOOL Navigate2(PCTSTR pszURL);

    DECLARE_WND_CLASS(_T("__SkinWebBrowserCtrl__"))

    BEGIN_MSG_MAP_EX(CSkinWebBrowser)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_PAINT(OnPaint)
        //MSG_WM_LBUTTONDOWN(OnLButtonDown)
        //MSG_WM_LBUTTONUP(OnLButtonUp)
        //MSG_WM_MOUSEMOVE(OnMouseMove)
        //MSG_WM_MOUSELEAVE(OnMouseLeave)
        MSG_WM_DESTROY(OnDestroy)
    END_MSG_MAP()

private:
    int OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnSize(UINT nType, CSize size);
    BOOL OnEraseBkgnd(CDCHandle dc);
    void OnPaint(CDCHandle dc);
    void OnDestroy();

private:
    CAxWindow               m_WinContainer;
    CComPtr<IWebBrowser2>   m_spWebBrowser;
    BOOL                    m_bInitOK;      //是否初始化成功

    VARIANT                 m_URL;
};