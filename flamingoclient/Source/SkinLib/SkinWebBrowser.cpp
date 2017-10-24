#include "stdafx.h"
#include "SkinWebBrowser.h"


CSkinWebBrowser::CSkinWebBrowser()
{
    m_bInitOK = FALSE;
    m_URL.bstrVal = NULL;
}

CSkinWebBrowser::~CSkinWebBrowser()
{
    
}

BOOL CSkinWebBrowser::Navigate2(PCTSTR pszURL)
{
    if (!m_bInitOK)
        return FALSE;
    
    if (m_URL.bstrVal != NULL)
        ::SysFreeString(m_URL.bstrVal);
    
    VARIANT v;
    m_URL.vt = VT_BSTR;
    m_URL.bstrVal = ::SysAllocString(pszURL);
    HRESULT hr = m_spWebBrowser->Navigate2(&m_URL, &v, &v, &v, &v);

    if (FAILED(hr))
        return FALSE;

    return TRUE;
}

int CSkinWebBrowser::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    RECT rtClient;
    ::GetClientRect(m_hWnd, &rtClient);

    m_WinContainer.Create(m_hWnd, rtClient, _T("shell.Explorer.2"), WS_CHILD | WS_VISIBLE);
    HRESULT hr = m_WinContainer.QueryControl(__uuidof(IWebBrowser2), (void**)&m_spWebBrowser);
    if (FAILED(hr))
        return 0;

    m_bInitOK = TRUE;

    return 1;
}

void CSkinWebBrowser::OnSize(UINT nType, CSize size)
{
    RECT rtClient;
    ::GetClientRect(m_hWnd, &rtClient);
    m_WinContainer.MoveWindow(0, 0, rtClient.right - 2, rtClient.bottom - 2, TRUE);
}

BOOL CSkinWebBrowser::OnEraseBkgnd(CDCHandle dc)
{
    return TRUE;
}

void CSkinWebBrowser::OnPaint(CDCHandle dc)
{
    CRect rcClient;
    GetClientRect(&rcClient);

    CPaintDC PaintDC(m_hWnd);

    CMemoryDC MemDC(PaintDC.m_hDC, rcClient);
    MemDC.FillRect(&rcClient, RGB(255, 255, 255));
}

void CSkinWebBrowser::OnDestroy()
{
    if (m_URL.bstrVal != NULL)
        ::SysFreeString(m_URL.bstrVal);
    
    m_spWebBrowser.Release();
    m_WinContainer.DestroyWindow();
}