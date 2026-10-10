/**
 *  远程桌面控制端全屏工具条实现，RdViewerToolbar.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "stdafx.h"
#include "RdViewerToolbar.h"

#include "../../Source/Common/Log/IULog.h"              // LOG_ERROR
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiVBox / DuiHBox / DuiLayout::Hint

#include <memory>
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：译文较长时加宽按钮

// ---- 外观常量（逻辑像素）----
static const int kBarWidth        = 420;   //工具条宽
static const int kBarTitleH       = 18;    //标题栏高（balloonui 最小，仅显示"远程桌面"标题、不放按钮）
static const int kBarContentH     = 44;    //深色操作横条高
static const int kExitFsBtnWidth  = 92;    //"退出全屏"按钮宽
static const int kDisconnBtnWidth = 80;    //"断开"按钮宽
static const int kExtraBtnWidth   = 100;   //额外按钮宽（屏幕共享观看窗的「打开麦克风」）
static const int kRowGapPx        = 8;     //横条内控件间距
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
    , m_title(TR(L"远程桌面"))
    , m_disconnectText(TR(L"断开"))
    , m_toggleBtn(NULL)
    , m_showExitFs(true)
    , m_showDisconnect(true)
    , m_exitFsBtnW(kExitFsBtnWidth)
    , m_disconnBtnW(kDisconnBtnWidth)
    , m_extraBtnW(kExtraBtnWidth)
    , m_toggleBtnW(screenshare::kTopBarToggleBtnW)
{
    memset(&m_expandedRect, 0, sizeof(m_expandedRect));
}

void CRdViewerToolbar::SetFixedButtonsVisible(bool showExitFullscreen, bool showDisconnect)
{
    m_showExitFs = showExitFullscreen;
    m_showDisconnect = showDisconnect;
}

bool CRdViewerToolbar::GetExtraButtonScreenRect(size_t index, RECT& out) const
{
    if (!IsWindow() || !IsWindowVisible() || index >= m_extraButtons.size() || m_extraButtons[index] == NULL ||
        !m_extraButtons[index]->IsVisible())
        return false;
    out = m_extraButtons[index]->GetRect();
    POINT tl = { out.left, out.top };
    POINT br = { out.right, out.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    out.left = tl.x;
    out.top = tl.y;
    out.right = br.x;
    out.bottom = br.y;
    return true;
}

void CRdViewerToolbar::SetExtraButtons(const std::vector<std::wstring>& texts)
{
    m_extraTexts = texts;
    if (m_extraButtons.size() != texts.size())
        return;
    for (size_t i = 0; i < texts.size(); ++i)
    {
        if (m_extraButtons[i] != NULL)
            m_extraButtons[i]->SetText(texts[i].c_str());
    }
}

void CRdViewerToolbar::SetLabels(const std::wstring& title, const std::wstring& disconnectText)
{
    m_title = title;
    m_disconnectText = disconnectText;
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
    if (Create(NULL, CWindow::rcDefault, TR(L"远程桌面"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR(_T("远程桌面全屏工具条 Create 失败。"));
        return;
    }
    SetTitle(m_title.c_str());
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

void CRdViewerToolbar::ComputeButtonWidths()
{
    //简体中文界面下各函数都返回设计宽度，布局与改动之前相同
    m_exitFsBtnW = i18n::DefaultButtonWidthForText(TR(L"退出全屏"), kExitFsBtnWidth);
    m_disconnBtnW = i18n::DefaultButtonWidthForText(m_disconnectText.c_str(), kDisconnBtnWidth);
    m_extraBtnW = kExtraBtnWidth;
    for (size_t i = 0; i < m_extraTexts.size(); ++i)
    {
        const int w = i18n::DefaultButtonWidthForText(m_extraTexts[i].c_str(), kExtraBtnWidth);
        if (w > m_extraBtnW)
        {
            m_extraBtnW = w;
        }
    }
    m_toggleBtnW = i18n::DefaultButtonWidthForTexts(screenshare::TopBarCollapseText(), screenshare::TopBarPinText(),
                                                    screenshare::kTopBarToggleBtnW);
}

void CRdViewerToolbar::BuildUi()
{
    ComputeButtonWidths();

    //客户区根：DuiVBox 卡片底色铺满深灰；其内一行横向内容
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);

    //一行：左侧状态文字（占满剩余宽），右侧"退出全屏""断开"两个按钮
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(12, 0, 10, 0);
    row->SetGap(kRowGapPx);

    std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
    status->SetTextColor(kBarTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    status->SetText(m_title.c_str());
    m_statusLabel = status.get();
    row->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Weight(1));
    m_toggleBtn = NULL;

    //额外按钮（只在设了文字时才有）：与「退出全屏」同样的次操作样式
    m_extraButtons.clear();
    for (size_t i = 0; i < m_extraTexts.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> extraBtn(new balloonwjui::DuiButton());
        extraBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        extraBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
        extraBtn->SetText(m_extraTexts[i].c_str());
        extraBtn->SetCtrlId(static_cast<UINT>(kExtraBtnId + i));
        m_extraButtons.push_back(extraBtn.get());
        row->AddChild(std::move(extraBtn),
                      balloonwjui::DuiLayout::Hint().Fixed(m_extraBtnW).Margin(0, 8, 0, 8));
    }

    //"退出全屏"：白底深字次操作按钮（上下留 8px 不顶满条高）；屏幕共享强制模式下没有（第 13 期）
    if (m_showExitFs)
    {
        std::unique_ptr<balloonwjui::DuiButton> exitBtn(new balloonwjui::DuiButton());
        exitBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        exitBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
        exitBtn->SetText(TR(L"退出全屏"));
        exitBtn->SetCtrlId(kExitFsBtnId);
        row->AddChild(std::move(exitBtn),
                      balloonwjui::DuiLayout::Hint().Fixed(m_exitFsBtnW).Margin(0, 8, 0, 8));
    }

    //"断开"：危险红实心，结束会话语义醒目；屏幕共享强制模式下没有（第 13 期）
    if (m_showDisconnect)
    {
        std::unique_ptr<balloonwjui::DuiButton> disconnBtn(new balloonwjui::DuiButton());
        disconnBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        disconnBtn->SetVariant(balloonwjui::DuiButton::Variant::Danger);
        disconnBtn->SetText(m_disconnectText.c_str());
        disconnBtn->SetCtrlId(kDisconnectBtnId);
        row->AddChild(std::move(disconnBtn),
                      balloonwjui::DuiLayout::Hint().Fixed(m_disconnBtnW).Margin(0, 8, 0, 8));
    }

    //「收起 / 固定」：最右端，收起后工具条缩成顶部的小把手（第 10 期 10c）
    std::unique_ptr<balloonwjui::DuiButton> toggleBtn(new balloonwjui::DuiButton());
    toggleBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    toggleBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
    toggleBtn->SetText(m_collapse.ToggleText());
    toggleBtn->SetCtrlId(kToggleBtnId);
    m_toggleBtn = toggleBtn.get();
    row->AddChild(std::move(toggleBtn),
                  balloonwjui::DuiLayout::Hint().Fixed(m_toggleBtnW).Margin(0, 8, 0, 8));

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
    //有额外按钮时工具条相应加宽，状态文字的宽度不变；最右端还有「收起 / 固定」按钮
    //没有「退出全屏」「断开」时（屏幕共享强制模式）相应变窄。kBarWidth 按设计宽度的「退出全屏」「断开」算出，
    //两个按钮按译文加宽时（多语言第 4 期）整条加上多出来的部分
    int barW = kBarWidth + static_cast<int>(m_extraTexts.size()) * (m_extraBtnW + kRowGapPx) + m_toggleBtnW +
               kRowGapPx;
    barW += m_showExitFs ? (m_exitFsBtnW - kExitFsBtnWidth) : -(kExitFsBtnWidth + kRowGapPx);
    barW += m_showDisconnect ? (m_disconnBtnW - kDisconnBtnWidth) : -(kDisconnBtnWidth + kRowGapPx);
    //加宽后也不超过显示器宽度，放不下时由左侧的状态文字让出宽度
    if (barW > rc.right - rc.left)
        barW = rc.right - rc.left;
    int x = rc.left + ((rc.right - rc.left) - barW) / 2;
    int y = rc.top + kTopMargin;
    m_expandedRect.left = x;
    m_expandedRect.top = y;
    m_expandedRect.right = x + barW;
    m_expandedRect.bottom = y + barH;
    //每次进入全屏都从一直展开开始；不激活地摆放并显示（置顶 + 不抢焦点）
    m_collapse.Reset();
    applyCollapse();
    ShowWindow(SW_SHOWNA);
    UpdateWindow();
}

void CRdViewerToolbar::applyCollapse()
{
    if (!IsWindow())
        return;
    const int state = m_collapse.State();
    if (state == screenshare::top_bar_collapsed)
    {
        //收起：窗口缩成顶边正中的小把手，只剩标题栏，标题前加向下的箭头
        const std::wstring handleTitle = std::wstring(screenshare::kTopBarHandlePrefix) + m_title;
        SetTitle(handleTitle.c_str());
        const RECT h = screenshare::ComputeTopBarHandleRect(m_expandedRect);
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, h.left, h.top, h.right - h.left, h.bottom - h.top,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    else
    {
        SetTitle(m_title.c_str());
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_expandedRect.left, m_expandedRect.top,
                       m_expandedRect.right - m_expandedRect.left, m_expandedRect.bottom - m_expandedRect.top,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (m_toggleBtn != NULL)
        m_toggleBtn->SetText(m_collapse.ToggleText());
    //一直展开时不需要检查鼠标
    if (state == screenshare::top_bar_pinned)
        KillTimer(screenshare::kTopBarTimerId);
    else
        SetTimer(screenshare::kTopBarTimerId, screenshare::kTopBarTickMs, NULL);
}

LRESULT CRdViewerToolbar::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != screenshare::kTopBarTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //窗口矩形在收起时就是小把手、展开时就是工具条，鼠标在不在上面用同一个判断
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    RECT rc = { 0, 0, 0, 0 };
    GetWindowRect(&rc);
    const bool inside = ::PtInRect(&rc, pt) != FALSE;
    if (m_collapse.OnTick(static_cast<int64_t>(::GetTickCount64()), inside, inside))
        applyCollapse();
    return 0;
}

void CRdViewerToolbar::Hide()
{
    m_statusLabel = NULL;
    m_extraButtons.clear();
    m_toggleBtn   = NULL;
    m_notifyWnd   = NULL;
    m_cmdMsg      = 0;
    m_collapse.Reset();
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
        //「收起 / 固定」：本条自己处理，不发给宿主
        if (n->ctrlId == static_cast<UINT>(kToggleBtnId))
        {
            if (m_collapse.OnToggleClicked())
                applyCollapse();
            return 0;
        }
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
        if (n->ctrlId >= static_cast<UINT>(kExtraBtnId) &&
            n->ctrlId < static_cast<UINT>(kExtraBtnId + m_extraTexts.size()))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, m_cmdMsg, kCmdExtra + (n->ctrlId - kExtraBtnId), 0);
            return 0;
        }
    }
    //其余 DUI 通知交基类处理
    bHandled = FALSE;
    return 0;
}
