/**
 *  远程桌面被控端受控提示条实现，RdControlledBar.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "stdafx.h"
#include "RdControlledBar.h"

#include "../UserSessionData.h"    // FMG_MSG_RD_CONTROLLED_END_BY_USER
#include "../IULog.h"              // LOG_ERROR
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiVBox / DuiHBox / DuiLayout::Hint

#include <memory>

// ---- 外观常量（逻辑像素）----
static const int kBarWidth    = 380;   //提示条宽
static const int kBarTitleH   = 18;    //标题栏高（balloonui 最小，仅显示"远程桌面"标题、不放按钮）
static const int kBarContentH = 48;    //橙色警示内容横条高
static const int kEndBtnWidth = 96;    //"结束控制"按钮宽
static const int kTopMargin   = 8;     //距工作区顶部边距

//受控提示条醒目底色（琥珀橙）与提示文字色（深棕，保证对比度可读）。
static const COLORREF kBarBgColor   = RGB(244, 176, 66);
static const COLORREF kBarTextColor = RGB(60, 40, 0);

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

CRdControlledBar::CRdControlledBar()
    : m_notifyWnd(NULL)
    , m_tipLabel(NULL)
    , m_bgBitmap(NULL)
{
}

CRdControlledBar::~CRdControlledBar()
{
    if (m_bgBitmap != NULL)
    {
        ::DeleteObject(m_bgBitmap);
        m_bgBitmap = NULL;
    }
}

void CRdControlledBar::EnsureWindow()
{
    if (IsWindow())
        return;

    //WS_POPUP 无边框小窗；置顶 + 不抢焦点（避免从被控端正操作的窗口偷焦点）+ 不进任务栏
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, _T("远程桌面"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR(_T("远程桌面受控提示条 Create 失败。"));
        return;
    }
    SetTitle(_T("远程桌面"));
    SetTitleBarHeight(kBarTitleH);
    SetButtons(false, false, false);   //无最小化 / 最大化 / 关闭按钮
    SetResizable(false);               //固定尺寸
    SetBorderPx(0);                    //不留 resize 抓握边

    //铺纯色背景图 + 标题栏透明：让 18px 标题栏露出与下方橙色内容条同色的背景，
    //两段融成无缝一整条（DuiFrameWindow.h 文档明确支持此搭配）。标题文字改成内容同色深棕。
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

void CRdControlledBar::BuildUi()
{
    //客户区根：用 DuiVBox 的卡片底色铺满醒目橙；其内只放一行横向内容
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);

    //一行：左侧受控提示文字（占满剩余宽），右侧"结束控制"按钮
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(12, 0, 10, 0);
    row->SetGap(10);

    std::unique_ptr<balloonwjui::DuiLabel> tip(new balloonwjui::DuiLabel());
    tip->SetTextColor(kBarTextColor);
    tip->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    tip->SetText(_T("您的桌面正在被远程控制"));
    m_tipLabel = tip.get();
    row->AddChild(std::move(tip), balloonwjui::DuiLayout::Hint().Weight(1));

    //"结束控制"：危险红实心按钮，醒目且语义清晰（按钮上下留 9px 让它不顶满条高）
    std::unique_ptr<balloonwjui::DuiButton> endBtn(new balloonwjui::DuiButton());
    endBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    endBtn->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    endBtn->SetText(_T("结束控制"));
    endBtn->SetCtrlId(kEndBtnId);
    row->AddChild(std::move(endBtn),
                  balloonwjui::DuiLayout::Hint().Fixed(kEndBtnWidth).Margin(0, 9, 0, 9));

    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
}

void CRdControlledBar::MoveToTopCenter()
{
    if (!IsWindow())
        return;

    //取本窗口所在显示器的工作区（多显示器安全；取不到时退回主显示器工作区）
    HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT rcWork;
    if (::GetMonitorInfo(mon, &mi))
        rcWork = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

    int barH = kBarTitleH + kBarContentH;
    int x = rcWork.left + ((rcWork.right - rcWork.left) - kBarWidth) / 2;
    int y = rcWork.top + kTopMargin;
    //不激活地摆放并显示（HWND_TOPMOST 保证置顶；SWP_NOACTIVATE 不抢焦点）
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, kBarWidth, barH,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CRdControlledBar::Show(const std::wstring& peerName, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    m_peerName  = peerName;

    EnsureWindow();
    if (!IsWindow())
        return;

    BuildUi();

    //刷新文案：有昵称则"正在被 XXX 远程控制"，否则退化为通用文案
    if (m_tipLabel != NULL)
    {
        CString tip;
        if (!m_peerName.empty())
            tip.Format(_T("您的桌面正在被 %s 远程控制"), m_peerName.c_str());
        else
            tip = _T("您的桌面正在被远程控制");
        m_tipLabel->SetText(tip);
    }

    MoveToTopCenter();
    //SW_SHOWNA：显示但不激活，保持被控端原有输入焦点不被打断
    ShowWindow(SW_SHOWNA);
    UpdateWindow();
}

void CRdControlledBar::Hide()
{
    m_tipLabel  = NULL;
    m_notifyWnd = NULL;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CRdControlledBar::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL
        && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK)
        && n->ctrlId == static_cast<UINT>(kEndBtnId))
    {
        //用户点"结束控制"：通知宿主统一收尾（发 cmd_rd_close + 停推流 + 关本条）
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, FMG_MSG_RD_CONTROLLED_END_BY_USER, 0, 0);
        return 0;
    }
    //其余 DUI 通知交基类处理
    bHandled = FALSE;
    return 0;
}
