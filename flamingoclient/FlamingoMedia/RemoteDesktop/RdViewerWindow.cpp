/**
 *  远程桌面控制端观看窗实现，RdViewerWindow.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "RdViewerWindow.h"

#include "../../Source/Common/Log/IULog.h"               // LOG_INFO / LOG_ERROR
#include "RdEngineDef.h"     // remotedesktop::rd_frame_request_keyframe
#include "RdInputEncoder.h"  // RdInputEncoder::encodeMouse / encodeKey / mapClientToRemote
#include <windowsx.h>                         // GET_X_LPARAM / GET_Y_LPARAM
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // 「P2P」徽章（2026-10-04 起）

// ---- 固定尺寸（逻辑像素）----
// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复「ResizeClient 使窗口比设计尺寸大一圈」的问题时，
// 由 1024×720 加上修复前 ResizeClient 多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
static const int kViewerWidth  = 1042;   // 初始宽
static const int kViewerHeight = 767;    // 初始高
static const int kTitleBarH    = 44;     // 标题栏高
static const int kMinW         = 640;    // 最小客户区宽
static const int kMinH         = 480;    // 最小客户区高
// 标题栏帧率 / 帧大小刷新定时器：每秒一次；id 取较大值避开 balloonui 基类可能占用的小 id。
static const UINT_PTR kFpsTimerId = 101;
static const UINT     kFpsTimerMs = 1000;
// 连上之后迟迟没有画面时，每隔多久写一条诊断日志（毫秒）：带上收到的帧数、字节数与链路统计，据此判断数据停在哪一段
static const ULONGLONG kNoPictureLogIntervalMs = 5000;

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
    , m_linkPath(0)
{
}

void RdViewerCanvas::SetLinkPath(int linkPath)
{
    if (linkPath == m_linkPath)
        return;
    m_linkPath = linkPath;
    Invalidate();
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

    //数据经直连链路传输时，画面左上角叠一枚「P2P」徽章（全屏时同样可见）
    if (balloonmain::ShouldShowP2PBadge(m_linkPath))
        balloonmain::DrawP2PBadgeOverlay(hdc, rc);
}

// ============================ CRdViewerWindow ============================

CRdViewerWindow::CRdViewerWindow()
    : m_canvas(NULL)
    , m_framesSinceTick(0)
    , m_lastFrameBytes(0)
    , m_framesTotal(0)
    , m_bitmapInfoFrames(0)
    , m_firstFrames(0)
    , m_bytesTotal(0)
    , m_connectedTick(0)
    , m_lastNoPictureLogTick(0)
    , m_fullscreenIconId(0)
    , m_fullscreenIcon(NULL)
    , m_actions(NULL)
    , m_connectCancel(false)
    , m_connectSeq(0)
{
}

CRdViewerWindow::~CRdViewerWindow()
{
    //先停连接线程、断开传输再析构，避免接收线程回调悬空 this
    StopTransport();
    if (m_fullscreenIcon != NULL)
        ::DeleteObject(m_fullscreenIcon);
}

void CRdViewerWindow::StopTransport()
{
    m_connectCancel.store(true);
    if (m_connectThread.joinable())
        m_connectThread.join();
    m_transport.Close();
}

void CRdViewerWindow::ConnectProc(unsigned int seq)
{
    const bool ok = m_transport.Connect(m_params, this, &m_connectCancel);
    //窗口已关（CloseViewer 正在等本线程结束）时投递失败，结果随之丢弃
    if (IsWindow())
        ::PostMessage(m_hWnd, WM_RD_VIEWER_TRANSPORT, (WPARAM)seq, ok ? 1 : 0);
}

void CRdViewerWindow::EnsureWindow()
{
    if (IsWindow())
        return;

    //owner 传 NULL：作为独立顶层窗口显示（任务栏可见、可最大化全屏看对方桌面）
    if (Create(NULL, CWindow::rcDefault, TR(L"远程桌面"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("远程桌面观看窗 Create 失败。"));
        return;
    }
    SetTitle(TR(L"远程桌面 - 对方屏幕"));
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    //没有所有者、会出现在任务栏：任务栏与 Alt-Tab 也用程序图标
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetTitleBarHeight(kTitleBarH);
    SetButtons(true, true, true);    // 最小化 + 最大化 + 关闭
    SetResizable(true);
    SetMinSize(kMinW, kMinH);

    //在最小化按钮左侧加全屏图标按钮（标题栏 caption icon），点击 / F11 切换全屏
    if (m_fullscreenIcon == NULL)
        m_fullscreenIcon = MakeFullscreenIcon();
    if (m_fullscreenIcon != NULL && m_fullscreenIconId == 0)
        m_fullscreenIconId = AddCaptionIcon(m_fullscreenIcon, TR(L"全屏 (F11)"));
}

void CRdViewerWindow::BuildUi()
{
    //客户区就是一个填满的自绘画面控件，承载解码画面
    std::unique_ptr<RdViewerCanvas> canvas(new RdViewerCanvas());
    canvas->SetDecoder(&m_decoder);
    m_canvas = canvas.get();
    SetClientContent(std::move(canvas));
}

bool CRdViewerWindow::Start(const remotedesktop::RdTransportParams& params)
{
    //上一次的连接线程与传输先收尾（控制器在 Start 之前已调用 CloseViewer，这里再保证一次）
    StopTransport();
    m_params = params;
    m_sessionId = params.m_sessionId;

    EnsureWindow();
    if (!IsWindow())
        return false;

    BuildUi();
    ResizeClient(kViewerWidth, kViewerHeight);
    CenterWindow(NULL);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //在连接线程上建立传输（有 P2P 链路槽时先等链路，最多 20 秒），结果经 WM_RD_VIEWER_TRANSPORT 交回本线程；
    //连上后被控端的屏幕帧经 OnRelayEngineFrame 异步到来
    m_connectCancel.store(false);
    ++m_connectSeq;
    m_connectThread = std::thread(&CRdViewerWindow::ConnectProc, this, m_connectSeq);
    LOG_INFO("RdViewerWindow: started, relay %s:%d, session=%s, p2p offered=%d.", params.m_relayIp.c_str(),
             params.m_relayPort, params.m_sessionId.c_str(), params.m_slot ? 1 : 0);
    return true;
}

LRESULT CRdViewerWindow::OnRdTransportMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //上一次会话迟到的结果：丢弃
    if ((unsigned int)wParam != m_connectSeq)
        return 0;
    if (lParam == 0)
    {
        LOG_ERROR("RdViewerWindow: connect failed, session=%s.", m_sessionId.c_str());
        ::MessageBox(m_hWnd, TR(L"连接远程桌面中转失败。"), TR(L"远程桌面"),
                     MB_OK | MB_ICONWARNING);
        //连不上就结束会话：被控端随之停止推流（服务端广播状态推送）
        if (!m_sessionId.empty() && m_actions != NULL)
            m_actions->CloseSession(m_sessionId);
        //提示框开着期间控制器可能已经收尾并关窗，见 OnRdClosedMsg 的说明
        if (IsWindow())
            CloseViewer();
        return 0;
    }
    LOG_INFO("RdViewerWindow: connected, session=%s, path=%d.", m_sessionId.c_str(), m_transport.Path());
    //诊断统计从连上这一刻算起
    m_framesTotal = 0;
    m_bitmapInfoFrames = 0;
    m_firstFrames = 0;
    m_bytesTotal = 0;
    m_connectedTick = ::GetTickCount64();
    m_lastNoPictureLogTick = m_connectedTick;

    //连上后立即向被控端请求一帧关键帧：即便错过了被控端最初发的 BITMAPINFO + 首屏
    //（在本端连上之前发出、被中转丢弃），也能马上让被控端补发，从而尽快就绪出画面。
    unsigned char req = static_cast<unsigned char>(remotedesktop::rd_frame_request_keyframe);
    m_transport.SendEngineFrame(&req, 1);

    //启动标题栏帧率 / 帧大小刷新定时器（每秒一次）
    SetTimer(kFpsTimerId, kFpsTimerMs, NULL);
    return 0;
}

void CRdViewerWindow::CloseViewer()
{
    m_toolbar.Hide();
    StopTransport();
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
        const bool wasReady = m_decoder.ready();
        //apply 返回 true 表示画面有变化（首屏 / 增量），触发画面控件重绘
        bool changed = m_decoder.apply(frame->data(), frame->size());
        //统计帧率与帧大小，供标题栏每秒刷新
        ++m_framesSinceTick;
        m_lastFrameBytes = frame->size();
        //诊断统计：按类型计数，首帧就绪时记一次耗时
        ++m_framesTotal;
        m_bytesTotal += frame->size();
        if (!frame->empty() && (*frame)[0] == remotedesktop::rd_frame_bitmapinfo)
            ++m_bitmapInfoFrames;
        else if (!frame->empty() && (*frame)[0] == remotedesktop::rd_frame_first)
            ++m_firstFrames;
        if (!wasReady && m_decoder.ready())
        {
            const ULONGLONG elapsed = (m_connectedTick != 0) ? ::GetTickCount64() - m_connectedTick : 0;
            LOG_INFO("RdViewerWindow: first picture ready, session=%s, %dx%d, %llu ms after connect, frames=%d, "
                     "bytes=%llu, path=%d.", m_sessionId.c_str(), m_decoder.width(), m_decoder.height(),
                     (unsigned long long)elapsed, m_framesTotal, m_bytesTotal, m_transport.Path());
        }
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
        ::MessageBox(m_hWnd, TR(L"远程桌面连接已断开。"), TR(L"远程桌面"),
                     MB_OK | MB_ICONINFORMATION);
        //提示框是模态的，它开着期间界面线程照常派发消息：服务端随后推来的「已断开」状态会经远程桌面控制器
        //调用 CloseViewer 先把本窗口销毁（提示框作为本窗口的从属窗口随之关闭）。所以提示框返回后要重新判断，
        //否则会对已销毁的窗口再次 DestroyWindow，Debug 构建触发 ATL 断言（2026-10-02 双账号联调实测）。
        if (IsWindow())
            DestroyWindow();
    }
    return 0;
}

LRESULT CRdViewerWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //用户点标题栏关闭：发 cmd_rd_close 结束会话（服务端会广播 state_notify 给被控端收尾），
    //再断中转、关窗
    if (!m_sessionId.empty() && m_actions != NULL)
        m_actions->CloseSession(m_sessionId);
    CloseViewer();
    return 0;
}

void CRdViewerWindow::LogNoPicture(ULONGLONG now)
{
    //链路统计只在走 P2P 时有：收到的业务字节数与本端交出的帧数对照，可以分清数据是没到本机，
    //还是到了本机却没拼成帧
    p2p::P2PLinkStats stats;
    const bool onP2P = m_transport.GetP2PStats(stats);
    LOG_WARNING("RdViewerWindow: no picture yet, session=%s, %llu ms after connect, frames=%d (bitmapinfo=%d, "
                "first=%d), bytes=%llu, path=%d, p2p=%d, rtt=%u ms, loss=%u permille, queued=%llu, sent=%llu, "
                "received=%llu.", m_sessionId.c_str(), (unsigned long long)(now - m_connectedTick), m_framesTotal,
                m_bitmapInfoFrames, m_firstFrames, m_bytesTotal, m_transport.Path(), onP2P ? 1 : 0, stats.m_rttMs,
                stats.m_lossPermille, (unsigned long long)stats.m_queuedBytes, (unsigned long long)stats.m_bytesSent,
                (unsigned long long)stats.m_bytesReceived);
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
        title.Format(TR(L"远程桌面  %d×%d   %d fps   帧 %u KB"),
                     m_decoder.width(), m_decoder.height(), fps,
                     static_cast<unsigned>(m_lastFrameBytes / 1024));
    }
    else
    {
        title = TR(L"远程桌面 - 正在连接…");
        //连上之后迟迟没有画面：按间隔写诊断日志
        const ULONGLONG now = ::GetTickCount64();
        if (m_connectedTick != 0 && now - m_lastNoPictureLogTick >= kNoPictureLogIntervalMs)
        {
            m_lastNoPictureLogTick = now;
            LogNoPicture(now);
        }
    }
    SetTitle(title);
    //链路实际路径：直连时画面上显示「P2P」徽章
    if (m_canvas != NULL)
        m_canvas->SetLinkPath(m_transport.LinkPath());
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
        m_transport.SendEngineFrame(cmd.data(), cmd.size());
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
        m_transport.SendEngineFrame(cmd.data(), cmd.size());
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
    m_transport.SendEngineFrame(cmd.data(), cmd.size());
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
        if (!m_sessionId.empty() && m_actions != NULL)
            m_actions->CloseSession(m_sessionId);
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
