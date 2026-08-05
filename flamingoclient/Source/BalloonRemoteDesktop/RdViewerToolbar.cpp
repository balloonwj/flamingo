/**
 *  远程桌面控制端全屏工具条实现，RdViewerToolbar.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "stdafx.h"
#include "RdViewerToolbar.h"

#include "../IULog.h"              // LOG_ERROR
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiVBox / DuiHBox / DuiLayout::Hint

#include <memory>

// ---- 外观常量（逻辑像素）----
static const int kBarWidth        = 420;   //工具条宽
static const int kBarTitleH       = 18;    //标题栏高（balloonui 最小，仅显示"远程桌面"标题、不放按钮）
static const int kBarContentH     = 44;    //深色操作横条高
static const int kExitFsBtnWidth  = 92;    //"退出全屏"按钮宽
static const int kDisconnBtnWidth = 80;    //"断开"按钮宽
static const int kTopMargin       = 6;     //距显示器顶部边距

//工具条深色底（近似播放器控制条）与状态文字色（近白，保证深底上可读）。
static const COLORREF kBarBgColor   = RGB(45, 45, 48);
static const COLORREF kBarTextColor = RGB(235, 235, 235);

//生成一张纯色 32 位 DIB（供 DuiFrameWindow::SetBgImage 以 9-grid 拉伸铺满窗口背景，
//配合透明标题栏让 18px 标题栏与下方内容融成无缝一整条）。调用方持有所有权、用完 DeleteObject。
static HBITMAP makeSolidBitmap(COLORREF c)
{
    const int sz = 8;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sz;
    bi.bmiHeader.biHeight      = -sz;   //top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;

    BYTE* p = static_cast<BYTE*>(bits);
    for (int i = 0; i < sz * sz; ++i)
    {
        p[i * 4 + 0] = GetBValue(c);
        p[i * 4 + 1] = GetGValue(c);
        p[i * 4 + 2] = GetRValue(c);
        p[i * 4 + 3] = 255;             //不透明（premultiplied 下 alpha=255 即原色）
    }
    return bmp;
}

CRdViewerToolbar::CRdViewerToolbar()
    : m_notifyWnd(NULL)
    , m_cmdMsg(0)
    , m_statusLabel(NULL)
    , m_bgBitmap(NULL)
{
}

CRdViewerToolbar::~CRdViewerToolbar()
{
    if (m_bgBitmap != NULL)
    {
        ::DeleteObject(m_bgBitmap);
        m_bgBitmap = NULL;
    }
}

void CRdViewerToolbar::EnsureWindow()
{
    if (IsWindow())
        return;

    //WS_POPUP 无边框小窗；置顶 + 不抢焦点（看屏时不打断别处输入）+ 不进任务栏
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, _T("远程桌面"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR(_T("远程桌面全屏工具条 Create 失败。"));
        return;
    }
    SetTitle(_T("远程桌面"));
    SetTitleBarHeight(kBarTitleH);
    SetButtons(false, false, false);   //无最小化 / 最大化 / 关闭按钮
    SetResizable(false);               //固定尺寸
    SetBorderPx(0);                    //不留 resize 抓握边

    //铺纯色背景图 + 标题栏透明：让 18px 标题栏露出与下方深灰内容条同色的背景，
    //两段融成无缝一整条（DuiFrameWindow.h 文档明确支持此搭配）。标题文字改成内容同色近白。
    if (m_bgBitmap == NULL)
        m_bgBitmap = makeSolidBitmap(kBarBgColor);
    if (m_bgBitmap != NULL)
    {
        RECT insets = { 0, 0, 0, 0 };   //insets 全 0：整图拉伸铺满（无固定边）
        SetBgImage(m_bgBitmap, insets);
        SetTitleBarTransparent(true);
        SetTitleTextColor(kBarTextColor);
    }
}

void CRdViewerToolbar::BuildUi()
{
    //客户区根：DuiVBox 卡片底色铺满深灰；其内一行横向内容
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);

    //一行：左侧状态文字（占满剩余宽），右侧"退出全屏""断开"两个按钮
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(12, 0, 10, 0);
    row->SetGap(8);

    std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
    status->SetTextColor(kBarTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    status->SetText(_T("远程桌面"));
    m_statusLabel = status.get();
    row->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Weight(1));

    //"退出全屏"：白底深字次操作按钮（上下留 8px 不顶满条高）
    std::unique_ptr<balloonwjui::DuiButton> exitBtn(new balloonwjui::DuiButton());
    exitBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    exitBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
    exitBtn->SetText(_T("退出全屏"));
    exitBtn->SetCtrlId(kExitFsBtnId);
    row->AddChild(std::move(exitBtn),
                  balloonwjui::DuiLayout::Hint().Fixed(kExitFsBtnWidth).Margin(0, 8, 0, 8));

    //"断开"：危险红实心，结束会话语义醒目
    std::unique_ptr<balloonwjui::DuiButton> disconnBtn(new balloonwjui::DuiButton());
    disconnBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    disconnBtn->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    disconnBtn->SetText(_T("断开"));
    disconnBtn->SetCtrlId(kDisconnectBtnId);
    row->AddChild(std::move(disconnBtn),
                  balloonwjui::DuiLayout::Hint().Fixed(kDisconnBtnWidth).Margin(0, 8, 0, 8));

    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
}

void CRdViewerToolbar::ShowOnMonitor(HMONITOR mon, HWND notifyWnd, UINT cmdMsg)
{
    m_notifyWnd = notifyWnd;
    m_cmdMsg    = cmdMsg;

    EnsureWindow();
    if (!IsWindow())
        return;

    BuildUi();

    //取目标显示器整屏矩形（全屏盖任务栏，用 rcMonitor 而非 rcWork）
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT rc;
    if (mon != NULL && ::GetMonitorInfo(mon, &mi))
        rc = mi.rcMonitor;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);

    int barH = kBarTitleH + kBarContentH;
    int x = rc.left + ((rc.right - rc.left) - kBarWidth) / 2;
    int y = rc.top + kTopMargin;
    //不激活地摆放并显示（置顶 + 不抢焦点）
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, kBarWidth, barH,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(SW_SHOWNA);
    UpdateWindow();
}

void CRdViewerToolbar::Hide()
{
    m_statusLabel = NULL;
    m_notifyWnd   = NULL;
    m_cmdMsg      = 0;
    if (IsWindow())
        DestroyWindow();
}

void CRdViewerToolbar::SetStatusText(const std::wstring& text)
{
    if (m_statusLabel != NULL)
        m_statusLabel->SetText(text.c_str());
}

LRESULT CRdViewerToolbar::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        //把按钮点击翻译成命令码发回宿主观看窗，由它统一执行退出全屏 / 断开
        if (n->ctrlId == static_cast<UINT>(kExitFsBtnId))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, m_cmdMsg, kCmdExitFullscreen, 0);
            return 0;
        }
        if (n->ctrlId == static_cast<UINT>(kDisconnectBtnId))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, m_cmdMsg, kCmdDisconnect, 0);
            return 0;
        }
    }
    //其余 DUI 通知交基类处理
    bHandled = FALSE;
    return 0;
}
