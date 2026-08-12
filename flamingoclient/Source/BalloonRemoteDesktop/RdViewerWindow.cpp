/**
 *  远程桌面控制端观看窗实现，RdViewerWindow.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "RdViewerWindow.h"

#include "../IULog.h"               // LOG_INFO / LOG_ERROR
#include "../net/IUSocket.h"        // CIUSocket::GetInstance().SendRemoteDesktopClose
#include "../RemoteDesktop/RdEngineDef.h"     // remotedesktop::rd_frame_request_keyframe
#include "../RemoteDesktop/RdInputEncoder.h"  // RdInputEncoder::encodeMouse / encodeKey / mapClientToRemote
#include <windowsx.h>                         // GET_X_LPARAM / GET_Y_LPARAM

// ---- 固定尺寸（逻辑像素）----
static const int kViewerWidth  = 1024;   // 初始客户区宽
static const int kViewerHeight = 720;    // 初始客户区高
static const int kTitleBarH    = 44;     // 标题栏高
static const int kMinW         = 640;    // 最小客户区宽
static const int kMinH         = 480;    // 最小客户区高
// 标题栏帧率 / 帧大小刷新定时器：每秒一次；id 取较大值避开 balloonui 基类可能占用的小 id。
static const UINT_PTR kFpsTimerId = 101;
static const UINT     kFpsTimerMs = 1000;

//把 (x,y) 像素写成不透明深灰（32 位 BGRA，premultiplied alpha=255）。供生成全屏图标用。
static void setIconPixel(unsigned char* px, int sz, int x, int y, unsigned char ink)
{
    if (x < 0 || y < 0 || x >= sz || y >= sz)
        return;
    unsigned char* p = px + (static_cast<size_t>(y) * sz + x) * 4;
    p[0] = ink;   //B
    p[1] = ink;   //G
    p[2] = ink;   //R
    p[3] = 255;   //A（深灰已是最终色，premultiplied 下 alpha 取 255）
}

//生成 16x16 全屏图标位图（32 位 premultiplied alpha，透明底 + 四角 L 形角标），表示
//"扩张到四角 = 全屏"。调用方持有所有权、用完 DeleteObject；CaptionIconButton 对 32 位
//图走 AlphaBlend，透明底自然融入标题栏 / hover 高亮。
static HBITMAP MakeFullscreenIcon()
{
    const int sz = 16;
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

    unsigned char* px = static_cast<unsigned char*>(bits);
    memset(px, 0, static_cast<size_t>(sz) * sz * 4);   //全透明

    const unsigned char ink = 80;   //深灰
    const int arm   = 5;            //每条角臂长度
    const int inset = 3;            //角标距图标边缘的距离
    int t;
    for (t = 0; t < arm; ++t)
    {
        //左上角：水平臂 + 垂直臂（各 2px 粗）
        setIconPixel(px, sz, inset + t, inset,     ink);
        setIconPixel(px, sz, inset + t, inset + 1, ink);
        setIconPixel(px, sz, inset,     inset + t, ink);
        setIconPixel(px, sz, inset + 1, inset + t, ink);
        //右上角
        setIconPixel(px, sz, sz - 1 - inset - t, inset,     ink);
        setIconPixel(px, sz, sz - 1 - inset - t, inset + 1, ink);
        setIconPixel(px, sz, sz - 1 - inset,     inset + t, ink);
        setIconPixel(px, sz, sz - 2 - inset,     inset + t, ink);
        //左下角
        setIconPixel(px, sz, inset + t, sz - 1 - inset,     ink);
        setIconPixel(px, sz, inset + t, sz - 2 - inset,     ink);
        setIconPixel(px, sz, inset,     sz - 1 - inset - t, ink);
        setIconPixel(px, sz, inset + 1, sz - 1 - inset - t, ink);
        //右下角
        setIconPixel(px, sz, sz - 1 - inset - t, sz - 1 - inset,     ink);
        setIconPixel(px, sz, sz - 1 - inset - t, sz - 2 - inset,     ink);
        setIconPixel(px, sz, sz - 1 - inset,     sz - 1 - inset - t, ink);
        setIconPixel(px, sz, sz - 2 - inset,     sz - 1 - inset - t, ink);
    }
    return bmp;
}

// ============================ RdViewerCanvas ============================

RdViewerCanvas::RdViewerCanvas()
    : m_decoder(NULL)
{
}

void RdViewerCanvas::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT& rc = GetRect();

    //未就绪（尚未收到 BITMAPINFO + 首屏）：填黑底，等待首帧
    if (m_decoder == NULL || !m_decoder->ready())
    {
        HBRUSH br = ::CreateSolidBrush(RGB(0, 0, 0));
        ::FillRect(hdc, &rc, br);
        ::DeleteObject(br);
        return;
    }

    //16 位色缩放用半色调模式，画面更平滑；按控件矩形缩放绘制对方整屏画面
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    m_decoder->drawTo(hdc, rc);
}

// ============================ CRdViewerWindow ============================

CRdViewerWindow::CRdViewerWindow()
    : m_canvas(NULL)
    , m_framesSinceTick(0)
    , m_lastFrameBytes(0)
    , m_fullscreenIconId(0)
    , m_fullscreenIcon(NULL)
{
}

CRdViewerWindow::~CRdViewerWindow()
{
    //先断中转再析构，避免接收线程回调悬空 this
    m_relay.Close();
    if (m_fullscreenIcon != NULL)
        ::DeleteObject(m_fullscreenIcon);
}

void CRdViewerWindow::EnsureWindow()
{
    if (IsWindow())
        return;

    //owner 传 NULL：作为独立顶层窗口显示（任务栏可见、可最大化全屏看对方桌面）
    if (Create(NULL, CWindow::rcDefault, _T("远程桌面"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("远程桌面观看窗 Create 失败。"));
        return;
    }
    SetTitle(_T("远程桌面 - 对方屏幕"));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(true, true, true);    // 最小化 + 最大化 + 关闭
    SetResizable(true);
    SetMinSize(kMinW, kMinH);

    //在最小化按钮左侧加全屏图标按钮（标题栏 caption icon），点击 / F11 切换全屏
    if (m_fullscreenIcon == NULL)
        m_fullscreenIcon = MakeFullscreenIcon();
    if (m_fullscreenIcon != NULL && m_fullscreenIconId == 0)
        m_fullscreenIconId = AddCaptionIcon(m_fullscreenIcon, _T("全屏 (F11)"));
}

void CRdViewerWindow::BuildUi()
{
    //客户区就是一个填满的自绘画面控件，承载解码画面
    std::unique_ptr<RdViewerCanvas> canvas(new RdViewerCanvas());
    canvas->SetDecoder(&m_decoder);
    m_canvas = canvas.get();
    SetClientContent(std::move(canvas));
}

bool CRdViewerWindow::Start(const std::string& ip, int port, const std::string& token,
                            const std::string& sessionId)
{
    m_sessionId = sessionId;

    EnsureWindow();
    if (!IsWindow())
        return false;

    BuildUi();
    ResizeClient(kViewerWidth, kViewerHeight);
    CenterWindow(NULL);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //连中转（阻塞握手）；连上后被控端开始推流，屏幕帧经 OnRelayEngineFrame 异步到来
    if (!m_relay.Connect(ip, port, token, this))
    {
        LOG_ERROR("RdViewerWindow: connect relay %s:%d failed.", ip.c_str(), port);
        ::MessageBox(m_hWnd, _T("连接远程桌面中转失败。"), _T("远程桌面"),
                     MB_OK | MB_ICONWARNING);
        DestroyWindow();
        return false;
    }
    LOG_INFO("RdViewerWindow: started, relay %s:%d, session=%s.",
             ip.c_str(), port, sessionId.c_str());

    //连上后立即向被控端请求一帧关键帧：即便错过了被控端最初发的 BITMAPINFO + 首屏
    //（在本端连上中转之前发出、被中转丢弃），也能马上让被控端补发，从而尽快就绪出画面。
    unsigned char req = static_cast<unsigned char>(remotedesktop::rd_frame_request_keyframe);
    m_relay.SendEngineFrame(&req, 1);

    //启动标题栏帧率 / 帧大小刷新定时器（每秒一次）
    SetTimer(kFpsTimerId, kFpsTimerMs, NULL);
    return true;
}

void CRdViewerWindow::CloseViewer()
{
    m_toolbar.Hide();
    m_relay.Close();
    if (IsWindow())
        DestroyWindow();
}

void CRdViewerWindow::OnRelayEngineFrame(const unsigned char* data, size_t len)
{
    //接收线程：把屏幕帧拷到堆，PostMessage 交 UI 线程解码 / 重绘
    if (!IsWindow())
        return;

    std::vector<unsigned char>* frame = new std::vector<unsigned char>(data, data + len);
    if (!::PostMessage(m_hWnd, WM_RD_VIEWER_FRAME, 0, reinterpret_cast<LPARAM>(frame)))
        delete frame;   //窗口已销毁，PostMessage 失败则就地释放，避免泄漏
}

void CRdViewerWindow::OnRelayClosed()
{
    //接收线程：通知 UI 线程会话已断
    if (IsWindow())
        ::PostMessage(m_hWnd, WM_RD_VIEWER_CLOSED, 0, 0);
}

LRESULT CRdViewerWindow::OnRdFrameMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    std::vector<unsigned char>* frame = reinterpret_cast<std::vector<unsigned char>*>(lParam);
    if (frame != NULL)
    {
        //apply 返回 true 表示画面有变化（首屏 / 增量），触发画面控件重绘
        bool changed = m_decoder.apply(frame->data(), frame->size());
        //统计帧率与帧大小，供标题栏每秒刷新
        ++m_framesSinceTick;
        m_lastFrameBytes = frame->size();
        if (changed && m_canvas != NULL)
            m_canvas->Invalidate();
        delete frame;
    }
    return 0;
}

LRESULT CRdViewerWindow::OnRdClosedMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //中转断开：提示并关窗（会话已结束）
    if (IsWindow())
    {
        ::MessageBox(m_hWnd, _T("远程桌面连接已断开。"), _T("远程桌面"),
                     MB_OK | MB_ICONINFORMATION);
        DestroyWindow();
    }
    return 0;
}

LRESULT CRdViewerWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //用户点标题栏关闭：发 cmd_rd_close 结束会话（服务端会广播 state_notify 给被控端收尾），
    //再断中转、关窗
    if (!m_sessionId.empty())
        CIUSocket::GetInstance().SendRemoteDesktopClose(m_sessionId);
    CloseViewer();
    return 0;
}

LRESULT CRdViewerWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //非本窗口的帧率定时器：交基类处理（balloonui 动画等可能用别的定时器 id）
    if (wParam != kFpsTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    //本统计周期（1 秒）收到的帧数即帧率；读后清零
    int fps = m_framesSinceTick;
    m_framesSinceTick = 0;

    CString title;
    if (m_decoder.ready())
    {
        //标题栏显示：画面分辨率 + 帧率 + 最近一帧大小（KB）
        title.Format(_T("远程桌面  %d×%d   %d fps   帧 %u KB"),
                     m_decoder.width(), m_decoder.height(), fps,
                     static_cast<unsigned>(m_lastFrameBytes / 1024));
    }
    else
    {
        title = _T("远程桌面 - 正在连接…");
    }
    SetTitle(title);
    //全屏态标题栏被隐藏，把同一份状态文字同步给浮动工具条
    if (IsFullscreen())
        m_toolbar.SetStatusText((LPCTSTR)title);
    return 0;
}

LRESULT CRdViewerWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //标题栏图标的通知不带 ctrlId（caption icon 不是客户区控件），只能靠 extra 里回带的
    //captionId 区分，故必须先确认 m_fullscreenIconId 已经分配过。自定义通知码是按控件
    //各自编号的（都从 DUIN_CUSTOM 起算），DUIFW_CAPTION_ICON_CLICK 与别的控件的第一个
    //自定义码同值；漏掉这道判定的话，图标尚未创建（m_fullscreenIconId 为 0）时，别的
    //控件发来的 extra 恰为 0 的同值通知会被误判成"点了全屏按钮"。
    if (n != NULL
        && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_fullscreenIconId != 0
        && static_cast<int>(n->extra) == m_fullscreenIconId)
    {
        //标题栏全屏按钮点击：切换全屏并同步全屏工具条
        SyncFullscreenToolbar();
        return 0;
    }
    //其余 DUI 通知（min / max / close 等）交基类处理
    bHandled = FALSE;
    return 0;
}

LRESULT CRdViewerWindow::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_F11)
    {
        //F11：在窗口 / 全屏间切换并同步全屏工具条
        SyncFullscreenToolbar();
        return 0;
    }
    if (wParam == VK_ESCAPE && IsFullscreen())
    {
        //ESC：仅在全屏态退出全屏（不转发给被控端），并隐藏全屏工具条
        ExitFullscreen();
        m_toolbar.Hide();
        return 0;
    }
    //其余按键编码成键盘命令发给被控端
    bHandled = TRUE;
    if (m_decoder.ready())
    {
        std::vector<BYTE> cmd;
        remotedesktop::RdInputEncoder::encodeKey(cmd, WM_KEYDOWN, static_cast<uint32_t>(wParam));
        m_relay.SendEngineFrame(cmd.data(), cmd.size());
    }
    return 0;
}

LRESULT CRdViewerWindow::OnKeyUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //全屏快捷键不转发；其余按键抬起编码发给被控端
    if (wParam == VK_F11 || wParam == VK_ESCAPE)
        return 0;
    if (m_decoder.ready())
    {
        std::vector<BYTE> cmd;
        remotedesktop::RdInputEncoder::encodeKey(cmd, WM_KEYUP, static_cast<uint32_t>(wParam));
        m_relay.SendEngineFrame(cmd.data(), cmd.size());
    }
    return 0;
}

LRESULT CRdViewerWindow::OnMouseInput(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    //采集是旁路：不拦截鼠标，让标题栏按钮 / DuiHost 照常工作
    bHandled = FALSE;
    if (m_canvas == NULL || !m_decoder.ready())
        return 0;

    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    //滚轮消息的 lParam 是屏幕坐标，其余是客户区坐标
    if (uMsg == WM_MOUSEWHEEL)
        ::ScreenToClient(m_hWnd, &pt);

    //仅采集落在画面控件矩形内的鼠标（标题栏区交给 DuiHost 处理按钮）
    const RECT& rc = m_canvas->GetRect();
    if (!::PtInRect(&rc, pt))
        return 0;

    //画面控件坐标 → 被控端屏幕坐标（等比映射并夹取到屏幕范围）
    int remoteX = 0;
    int remoteY = 0;
    remotedesktop::RdInputEncoder::mapClientToRemote(
        pt.x - rc.left, pt.y - rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        m_decoder.width(), m_decoder.height(),
        remoteX, remoteY);

    int wheel = (uMsg == WM_MOUSEWHEEL) ? GET_WHEEL_DELTA_WPARAM(wParam) : 0;

    std::vector<BYTE> cmd;
    remotedesktop::RdInputEncoder::encodeMouse(cmd, uMsg, remoteX, remoteY, wheel);
    m_relay.SendEngineFrame(cmd.data(), cmd.size());
    return 0;
}

LRESULT CRdViewerWindow::OnToolbarCmd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam == CRdViewerToolbar::kCmdExitFullscreen)
    {
        //工具条"退出全屏"：退出全屏并隐藏工具条
        ExitFullscreen();
        m_toolbar.Hide();
    }
    else if (wParam == CRdViewerToolbar::kCmdDisconnect)
    {
        //工具条"断开"：发 cmd_rd_close 结束会话、断中转关窗（同标题栏关闭逻辑）
        if (!m_sessionId.empty())
            CIUSocket::GetInstance().SendRemoteDesktopClose(m_sessionId);
        CloseViewer();
    }
    return 0;
}

void CRdViewerWindow::SyncFullscreenToolbar()
{
    //先切换全屏态，再据新状态显示 / 隐藏工具条
    ToggleFullscreen();
    if (IsFullscreen())
    {
        //进全屏：在观看窗所在显示器顶部居中显示工具条
        HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        m_toolbar.ShowOnMonitor(mon, m_hWnd, WM_RD_VIEWER_TOOLBAR_CMD);
    }
    else
    {
        //出全屏：隐藏工具条
        m_toolbar.Hide();
    }
}
