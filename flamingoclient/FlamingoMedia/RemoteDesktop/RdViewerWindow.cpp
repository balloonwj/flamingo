/**
 *  远程桌面控制端观看窗实现，RdViewerWindow.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "RdViewerWindow.h"

#include "../../Source/Common/Log/IULog.h"               // LOG_INFO / LOG_ERROR
#include "RdEngineDef.h"     // remotedesktop::rd_frame_request_keyframe
#include "RdInputEncoder.h"  // RdInputEncoder::encodeMouse / encodeKey
#include "RdVideoFrames.h"   // 4～6 号帧的解析、带能力位的关键帧请求与回执（2026-10-15）
#include "../Share/ShareVideoPolicy.h"   // kRdVideoMaxLongSide：远程桌面编码长边上限，试建解码器用（2026-10-15）
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
// 控制申请被拒、被收回等提示在浮动工具条状态区停留的时长（毫秒）
static const ULONGLONG kNoticeShowMs = 5000;
// 回执定时器（2026-10-15，方案 4.3）：收 H.264 期间每 500 毫秒回执一次；id 与帧率定时器错开
static const UINT_PTR kAckTimerId = 102;
static const UINT     kAckTimerMs = 500;

// ============================ RdViewerCanvas ============================

RdViewerCanvas::RdViewerCanvas()
    : m_picture(NULL)
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

RECT RdViewerCanvas::PictureRect() const
{
    const RECT& rc = GetRect();
    if (m_picture == NULL || !m_picture->ready())
        return rc;
    return remotedesktop::ComputeRdFitRect(rc, m_picture->width(), m_picture->height());
}

void RdViewerCanvas::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT& rc = GetRect();

    //先整块填黑：还没有画面时就是黑底，有画面后是画面四周的黑边
    ::FillRect(hdc, &rc, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    if (m_picture == NULL || !m_picture->ready())
        return;

    //缩放用半色调模式，画面更平滑；按源尺寸的宽高比放进控件、居中绘制对方整屏画面，不拉伸变形
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    const RECT picture = PictureRect();
    m_picture->drawTo(hdc, picture);

    //画面本身不含指针：按帧里带的位置与类型画出被控端的指针
    drawRemoteCursor(hdc, picture);

    //数据经直连链路传输时，画面左上角叠一枚「P2P」徽章（全屏时同样可见）
    if (balloonmain::ShouldShowP2PBadge(m_linkPath))
        balloonmain::DrawP2PBadgeOverlay(hdc, picture);
}

void RdViewerCanvas::drawRemoteCursor(HDC hdc, const RECT& picture)
{
    POINT at;
    if (!remotedesktop::MapRdRemoteToView(m_picture->cursorPos(), m_picture->width(), m_picture->height(), picture, at))
        return;
    //类型索引不认识（被控端取不到类型时为 255）：画普通箭头
    HCURSOR cursor = m_cursors.getCursorHandle(m_picture->cursorType());
    if (cursor == NULL)
        cursor = ::LoadCursor(NULL, IDC_ARROW);
    if (cursor == NULL)
        return;
    //指针图标的热点对准指针位置
    ICONINFO info;
    memset(&info, 0, sizeof(info));
    int hotX = 0;
    int hotY = 0;
    if (::GetIconInfo(cursor, &info))
    {
        hotX = static_cast<int>(info.xHotspot);
        hotY = static_cast<int>(info.yHotspot);
        if (info.hbmMask != NULL)
            ::DeleteObject(info.hbmMask);
        if (info.hbmColor != NULL)
            ::DeleteObject(info.hbmColor);
    }
    ::DrawIconEx(hdc, at.x - hotX, at.y - hotY, cursor, 0, 0, 0, NULL, DI_NORMAL);
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
    , m_noticeUntil(0)
    , m_actions(NULL)
    , m_connectCancel(false)
    , m_connectSeq(0)
    , m_capsPacer(false)
    , m_canDecodeH264(false)
    , m_decodeMaxLongSide(0)
    , m_keyRequests(0)
    , m_videoFramesSinceTick(0)
    , m_videoLastBytes(0)
    , m_videoFramesTotal(0)
{
}

CRdViewerWindow::~CRdViewerWindow()
{
    //先停连接线程、断开传输再析构，避免接收线程回调悬空 this
    StopTransport();
    m_bar.Hide();
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
    //试建 H.264 解码器（2026-10-15，方案 4.3）：得出本机能否解与能解的最大长边，连上后随关键帧请求告诉被控端。
    //在连接线程上做，不占 UI 线程；结果在投递连接结果之前写好
    int maxLongSide = 0;
    const bool canDecode = remotedesktop::RdVideoPicture::ProbeDecoder(screenshare::kRdVideoMaxLongSide, maxLongSide);
    m_canDecodeH264.store(canDecode);
    m_decodeMaxLongSide.store(maxLongSide);
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
}

void CRdViewerWindow::BuildUi()
{
    //客户区就是一个填满的自绘画面控件，承载解码画面；画面由 applySelectedPicture 按最近就绪的一种设置
    std::unique_ptr<RdViewerCanvas> canvas(new RdViewerCanvas());
    m_canvas = canvas.get();
    SetClientContent(std::move(canvas));
    applySelectedPicture();
}

bool CRdViewerWindow::openWindow(const std::string& sessionId)
{
    m_sessionId = sessionId;
    //控制权限每次接通都从未授权开始，只能查看
    m_control.Reset();
    m_notice.clear();
    m_noticeUntil = 0;

    EnsureWindow();
    if (!IsWindow())
        return false;

    //画面从「还没有」开始；H.264 解码线程在传输连上之前启动，接收线程从第一帧起就能交帧（2026-10-15）
    m_selector.Reset();
    m_video.Start(m_hWnd, WM_RD_VIEWER_PICTURE, WM_RD_VIEWER_KEYREQ);
    BuildUi();
    ResizeClient(kViewerWidth, kViewerHeight);
    CenterWindow(NULL);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //浮动工具条：以本窗口为所有者，浮在画面区域顶部居中
    m_bar.SetCaptionText(TR(L"远程桌面"));
    refreshBar();
    m_bar.Show(m_hWnd, m_hWnd, WM_RD_VIEWER_BAR_CMD, barAnchor());
    return true;
}

bool CRdViewerWindow::OpenForTest(const std::string& sessionId)
{
    return openWindow(sessionId);
}

bool CRdViewerWindow::Start(const remotedesktop::RdTransportParams& params)
{
    //上一次的连接线程与传输先收尾（控制器在 Start 之前已调用 CloseViewer，这里再保证一次）
    StopTransport();
    m_params = params;
    if (!openWindow(params.m_sessionId))
        return false;

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
    m_videoFramesTotal.store(0);
    m_connectedTick = ::GetTickCount64();
    m_lastNoPictureLogTick = m_connectedTick;

    //连上后立即向被控端请求一帧关键帧：即便错过了被控端最初发的关键帧（在本端连上之前发出、被中转丢弃），
    //也能马上让被控端补发，从而尽快就绪出画面。2026-10-15 起请求带能力位（本机能否解 H.264、能解的最大长边），
    //被控端据此选格式；旧被控端只看首字节，多出的负载被忽略
    m_capsPacer = remotedesktop::RdCapsResendPacer(m_canDecodeH264.load());
    sendKeyframeRequest("connected");

    //启动标题栏帧率 / 帧大小刷新定时器（每秒一次）与回执定时器（每 500 毫秒）
    SetTimer(kFpsTimerId, kFpsTimerMs, NULL);
    SetTimer(kAckTimerId, kAckTimerMs, NULL);
    return 0;
}

void CRdViewerWindow::sendKeyframeRequest(const char* reason)
{
    const bool canDecode = m_canDecodeH264.load();
    const int maxLongSide = m_decodeMaxLongSide.load();
    std::vector<unsigned char> req;
    remotedesktop::RdVideoFrames::BuildKeyframeRequest(canDecode, maxLongSide, req);
    const bool sent = m_transport.SendEngineFrame(req.data(), req.size());
    ++m_keyRequests;
    //任何途径发出的请求都登记给能力帧的重发判断：一秒内到达的能力帧不再触发重发
    m_capsPacer.OnRequestSent(static_cast<int64_t>(::GetTickCount64()));
    LOG_INFO("RdViewerWindow: keyframe request (%s), h264=%d, max long side=%d, sent=%d, session=%s.", reason,
             canDecode ? 1 : 0, maxLongSide, sent ? 1 : 0, m_sessionId.c_str());
}

const remotedesktop::IRdPicture* CRdViewerWindow::activePicture() const
{
    switch (m_selector.Active())
    {
    //旧格式：16 位 DIB + DIFF
    case remotedesktop::rd_picture_legacy:
        return &m_decoder;
    //H.264：解码线程解出的最新一帧
    case remotedesktop::rd_picture_video:
        return &m_video;
    //还没有画面
    default:
        return NULL;
    }
}

void CRdViewerWindow::applySelectedPicture()
{
    if (m_canvas == NULL)
        return;
    m_canvas->SetPicture(activePicture());
    m_canvas->Invalidate();
}

LRESULT CRdViewerWindow::OnRdPictureMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //H.264 解出了新画面：第一次（或从旧格式切过来）时画布换到 H.264 画面
    if (m_selector.OnVideoFrame())
    {
        applySelectedPicture();
        const ULONGLONG elapsed = (m_connectedTick != 0) ? ::GetTickCount64() - m_connectedTick : 0;
        LOG_INFO("RdViewerWindow: h264 picture active, session=%s, source %dx%d, %llu ms after connect, video frames=%d, "
                 "path=%d.", m_sessionId.c_str(), m_video.width(), m_video.height(), (unsigned long long)elapsed,
                 m_videoFramesTotal.load(), m_transport.Path());
        return 0;
    }
    if (m_canvas != NULL)
        m_canvas->Invalidate();
    return 0;
}

LRESULT CRdViewerWindow::OnRdKeyReqMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //解码跟不上、解码器建不起来：每秒最多请求一次；被挡下的由回执定时器在队列仍等关键帧时补发
    if (m_keyLimiter.Allow(static_cast<int64_t>(::GetTickCount64())))
        sendKeyframeRequest("h264 needs keyframe");
    return 0;
}

bool CRdViewerWindow::handleVideoSideFrame(const std::vector<unsigned char>& frame)
{
    const unsigned char type = frame[0];
    if (type == static_cast<unsigned char>(remotedesktop::rd_frame_cursor))
    {
        //指针帧：位置或类型变了才发，收到就重画（画面里的指针是本地画的）
        int32_t x = 0;
        int32_t y = 0;
        uint8_t cursorType = 0;
        if (remotedesktop::RdVideoFrames::ParseCursor(frame.data(), frame.size(), x, y, cursorType))
        {
            m_video.SetCursor(x, y, cursorType);
            if (m_selector.Active() == remotedesktop::rd_picture_video && m_canvas != NULL)
                m_canvas->Invalidate();
        }
        return true;
    }
    if (type == static_cast<unsigned char>(remotedesktop::rd_frame_caps))
    {
        //能力帧：被控端在每个关键帧之前发。仍在收旧格式、对方能编、本机能解时重发带能力位的请求（最初那条可能被中转丢掉）
        uint8_t caps = 0;
        if (remotedesktop::RdVideoFrames::ParseCaps(frame.data(), frame.size(), caps)
            && m_capsPacer.OnCapsFrame(caps, m_selector.Active() == remotedesktop::rd_picture_video,
                                       static_cast<int64_t>(::GetTickCount64())))
            sendKeyframeRequest("peer can encode h264");
        return true;
    }
    return false;
}

void CRdViewerWindow::onAckTimer()
{
    //只在收 H.264 期间回执；回送「发送时刻 + 在本端停留的时长」，画面静止时被控端算出的延迟也不会变大
    if (m_selector.Active() == remotedesktop::rd_picture_video)
    {
        uint32_t echo = 0;
        if (m_video.AckEcho(static_cast<int64_t>(::GetTickCount64()), echo))
        {
            std::vector<unsigned char> ack;
            remotedesktop::RdVideoFrames::BuildVideoAck(echo, ack);
            m_transport.SendEngineFrame(ack.data(), ack.size());
        }
    }
    //收帧队列溢出后在等关键帧，而当时的请求被限频挡下了：补发
    if (m_video.WaitingForKeyframe() && m_keyLimiter.Allow(static_cast<int64_t>(::GetTickCount64())))
        sendKeyframeRequest("still waiting for keyframe");
}

void CRdViewerWindow::CloseViewer()
{
    m_bar.Hide();
    m_control.Reset();
    m_mouse.Reset();
    StopTransport();
    //传输断开、接收线程不再交帧之后才停 H.264 解码线程
    m_video.Stop();
    if (IsWindow())
        DestroyWindow();
}

void CRdViewerWindow::OnRelayEngineFrame(const unsigned char* data, size_t len)
{
    //接收线程：把屏幕帧拷到堆，PostMessage 交 UI 线程解码 / 重绘
    if (!IsWindow())
        return;

    //H.264 视频帧（2026-10-15）：直接交给解码线程，不经 UI 线程（帧多、关键帧大，解码也不在 UI 线程上做）
    if (len > 0 && data[0] == static_cast<unsigned char>(remotedesktop::rd_frame_video))
    {
        m_videoFramesSinceTick.fetch_add(1);
        m_videoFramesTotal.fetch_add(1);
        m_videoLastBytes.store(static_cast<unsigned>(len));
        m_video.PushFrame(data, len);
        return;
    }

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
    //H.264 格式的指针帧与能力帧（2026-10-15）：不交给旧格式解码器
    if (frame != NULL && !frame->empty() && handleVideoSideFrame(*frame))
    {
        delete frame;
        return 0;
    }
    if (frame != NULL)
    {
        const bool wasReady = m_decoder.ready();
        const POINT oldCursor = m_decoder.cursorPos();
        const BYTE oldCursorType = m_decoder.cursorType();
        //apply 返回 true 表示画面有变化（首屏 / 增量），触发画面控件重绘
        bool changed = m_decoder.apply(frame->data(), frame->size());
        //只有指针动了、画面没变时也要重画（画面里的远端指针是本地画的，2026-10-12）
        const POINT newCursor = m_decoder.cursorPos();
        if (newCursor.x != oldCursor.x || newCursor.y != oldCursor.y || m_decoder.cursorType() != oldCursorType)
            changed = true;
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
        //旧格式的首屏到了且已就绪：被控端在发旧格式（含编码器中途失败、从 H.264 退回），画布换回旧格式画面
        if (!frame->empty() && (*frame)[0] == remotedesktop::rd_frame_first && m_decoder.ready()
            && m_selector.OnLegacyKeyframe())
        {
            LOG_INFO("RdViewerWindow: legacy picture active, session=%s, %dx%d.", m_sessionId.c_str(),
                     m_decoder.width(), m_decoder.height());
            applySelectedPicture();
            changed = false;
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
                "first=%d), video frames=%d (decoded=%llu, h264=%d), bytes=%llu, path=%d, p2p=%d, rtt=%u ms, "
                "loss=%u permille, queued=%llu, sent=%llu, received=%llu.", m_sessionId.c_str(),
                (unsigned long long)(now - m_connectedTick), m_framesTotal, m_bitmapInfoFrames, m_firstFrames,
                m_videoFramesTotal.load(), (unsigned long long)m_video.DecodedCount(), m_canDecodeH264.load() ? 1 : 0,
                m_bytesTotal, m_transport.Path(), onP2P ? 1 : 0, stats.m_rttMs, stats.m_lossPermille,
                (unsigned long long)stats.m_queuedBytes, (unsigned long long)stats.m_bytesSent,
                (unsigned long long)stats.m_bytesReceived);
}

LRESULT CRdViewerWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //回执定时器（2026-10-15）
    if (wParam == kAckTimerId)
    {
        onAckTimer();
        return 0;
    }
    //非本窗口的定时器：交基类处理（balloonui 动画等可能用别的定时器 id）
    if (wParam != kFpsTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    //本统计周期（1 秒）收到的帧数即帧率；读后清零。H.264 的视频帧在接收线程上另计（2026-10-15）
    const int legacyFps = m_framesSinceTick;
    m_framesSinceTick = 0;
    const int videoFps = m_videoFramesSinceTick.exchange(0);

    CString title;
    const remotedesktop::IRdPicture* picture = activePicture();
    if (picture != NULL && picture->ready())
    {
        //标题栏显示：画面分辨率 + 帧率 + 最近一帧大小（KB），按当前画面的格式取对应的统计
        const bool video = (m_selector.Active() == remotedesktop::rd_picture_video);
        const int fps = video ? videoFps : legacyFps;
        const size_t lastBytes = video ? static_cast<size_t>(m_videoLastBytes.load()) : m_lastFrameBytes;
        title.Format(TR(L"远程桌面  %d×%d   %d fps   帧 %u KB"),
                     picture->width(), picture->height(), fps,
                     static_cast<unsigned>(lastBytes / 1024));
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
    //提示到时：浮动工具条的状态区恢复为控制状态
    if (!m_notice.empty() && ::GetTickCount64() >= m_noticeUntil)
    {
        m_notice.clear();
        refreshBar();
    }
    return 0;
}

LRESULT CRdViewerWindow::OnMoveSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //照常交给基类（DuiFrameWindow 在 WM_SIZE 里重排控件）
    bHandled = FALSE;
    //最小化时窗口矩形没有意义，工具条随所有者一起隐藏，不必挪
    if (m_bar.IsWindow() && !IsIconic())
        m_bar.MoveTo(barAnchor());
    return 0;
}

LRESULT CRdViewerWindow::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_F11)
    {
        //F11：在窗口 / 全屏间切换，浮动工具条随之换位置
        toggleFullscreen();
        return 0;
    }
    if (wParam == VK_ESCAPE && IsFullscreen())
    {
        //ESC：仅在全屏态退出全屏（不转发给被控端）
        toggleFullscreen();
        return 0;
    }
    //其余按键：已获授权时编码成键盘命令发给被控端，只能查看时不发
    bHandled = TRUE;
    if (canSendInput())
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
    if (canSendInput())
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
    //只能查看（没有获授权）时不发送鼠标
    if (m_canvas == NULL || !canSendInput())
        return 0;

    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    //滚轮消息的 lParam 是屏幕坐标，其余是客户区坐标
    if (uMsg == WM_MOUSEWHEEL)
        ::ScreenToClient(m_hWnd, &pt);

    //扣除黑边换算成被控端屏幕坐标：黑边上的移动、按下与滚轮不发送；在画面内按下、拖到画面外松开时夹到画面边缘照发
    //按源尺寸换算（H.264 的编码尺寸可能比源小，与坐标无关，2026-10-15）
    const remotedesktop::IRdPicture* picture = activePicture();
    int remoteX = 0;
    int remoteY = 0;
    if (picture == NULL
        || !m_mouse.Map(uMsg, pt, m_canvas->PictureRect(), picture->width(), picture->height(), remoteX, remoteY))
        return 0;

    int wheel = (uMsg == WM_MOUSEWHEEL) ? GET_WHEEL_DELTA_WPARAM(wParam) : 0;

    std::vector<BYTE> cmd;
    remotedesktop::RdInputEncoder::encodeMouse(cmd, uMsg, remoteX, remoteY, wheel);
    m_transport.SendEngineFrame(cmd.data(), cmd.size());
    return 0;
}

LRESULT CRdViewerWindow::OnBarCmd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    switch (static_cast<UINT>(wParam))
    {
    //申请控制 / 取消申请 / 结束控制：结束控制时本地立即停止发送键鼠
    case kRdViewBarControl:
        LOG_INFO("RdViewerWindow: control button clicked, state=%d, session=%s.", m_control.State(), m_sessionId.c_str());
        applyControlEffects(m_control.OnControlButton());
        break;
    //全屏 / 退出全屏
    case kRdViewBarFullscreen:
        toggleFullscreen();
        break;
    //断开：发 cmd_rd_close 结束会话、断中转关窗（同标题栏关闭逻辑）
    case kRdViewBarDisconnect:
        if (!m_sessionId.empty() && m_actions != NULL)
            m_actions->CloseSession(m_sessionId);
        CloseViewer();
        break;
    //其余编号：工具条只有这三个使用方按钮，不会走到这里
    default:
        break;
    }
    return 0;
}

void CRdViewerWindow::toggleFullscreen()
{
    ToggleFullscreen();
    refreshBar();
    if (m_bar.IsWindow())
        m_bar.MoveTo(barAnchor());
}

RECT CRdViewerWindow::barAnchor() const
{
    return screenshare::ComputeViewerBarAnchor(*this);
}

void CRdViewerWindow::refreshBar()
{
    m_bar.SetStatusText(m_notice.empty() ? remotedesktop::RdViewerStatusText(m_control.State()) : m_notice);
    std::vector<screenshare::ViewerBarButtonSpec> buttons;
    buttons.push_back(remotedesktop::RdViewerControlButton(kRdViewBarControl, m_control.State()));
    //「全屏 / 退出全屏」：悬停提示按开关取默认文字
    buttons.push_back(screenshare::ViewerBarButtonSpec(
        kRdViewBarFullscreen, screenshare::share_bar_fullscreen,
        IsWindow() && IsFullscreen() ? screenshare::share_bar_tri_on : screenshare::share_bar_tri_off, std::wstring()));
    buttons.push_back(screenshare::ViewerBarButtonSpec(kRdViewBarDisconnect, screenshare::share_bar_leave,
                                                       screenshare::share_bar_tri_off, TR(L"断开")));
    m_bar.SetBarButtons(buttons);
}

void CRdViewerWindow::applyControlEffects(const remotedesktop::RdViewerEffects& effects)
{
    //不再能发送键鼠：忘掉按下的按钮（被控端关闭把关时会补发松开）
    if (!m_control.CanSendInput())
        m_mouse.Reset();
    if (effects.m_sendAction != 0 && !m_sessionId.empty() && m_actions != NULL)
        m_actions->SendControl(m_sessionId, effects.m_sendAction);
    if (effects.m_notice != remotedesktop::rd_viewer_notice_none)
    {
        m_notice = remotedesktop::RdViewerNoticeText(effects.m_notice);
        m_noticeUntil = ::GetTickCount64() + kNoticeShowMs;
    }
    refreshBar();
}

bool CRdViewerWindow::canSendInput() const
{
    const remotedesktop::IRdPicture* picture = activePicture();
    return picture != NULL && picture->ready() && m_control.CanSendInput();
}

void CRdViewerWindow::OnControlResponse(const net::RDControlResponse& resp)
{
    if (resp.m_sessionId != m_sessionId || !IsWindow())
        return;
    applyControlEffects(m_control.OnResponse(resp.m_action, resp.m_code, resp.m_control));
}

void CRdViewerWindow::OnControlNotify(const net::RDControlNotify& notify)
{
    if (notify.m_sessionId != m_sessionId || !IsWindow())
        return;
    LOG_INFO("RdViewerWindow: control notify, action=%d, control=%d, session=%s.", notify.m_action, notify.m_control,
             m_sessionId.c_str());
    applyControlEffects(m_control.OnNotify(notify.m_action, notify.m_control));
}

HWND CRdViewerWindow::BarWindowForTest() const
{
    return m_bar.IsWindow() ? m_bar.m_hWnd : NULL;
}

bool CRdViewerWindow::BarButtonRectForTest(UINT id, RECT& out) const
{
    return m_bar.GetButtonScreenRect(id, out);
}

std::wstring CRdViewerWindow::BarStatusForTest() const
{
    return m_bar.StatusTextForTest();
}

bool CRdViewerWindow::IsInputAllowedForTest() const
{
    return m_control.CanSendInput();
}

int CRdViewerWindow::ActivePictureForTest() const
{
    return m_selector.Active();
}

SIZE CRdViewerWindow::PictureSourceSizeForTest() const
{
    SIZE size = { 0, 0 };
    const remotedesktop::IRdPicture* picture = activePicture();
    if (picture != NULL && picture->ready())
    {
        size.cx = picture->width();
        size.cy = picture->height();
    }
    return size;
}

RECT CRdViewerWindow::PictureRectForTest() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (m_canvas != NULL)
        rc = m_canvas->PictureRect();
    return rc;
}

bool CRdViewerWindow::MapMouseMoveForTest(int x, int y, int& remoteX, int& remoteY)
{
    const remotedesktop::IRdPicture* picture = activePicture();
    if (m_canvas == NULL || picture == NULL || !picture->ready())
        return false;
    POINT pt;
    pt.x = x;
    pt.y = y;
    return m_mouse.Map(WM_MOUSEMOVE, pt, m_canvas->PictureRect(), picture->width(), picture->height(), remoteX, remoteY);
}

void CRdViewerWindow::SetDecodeCapsForTest(bool canDecode, int maxLongSide)
{
    m_canDecodeH264.store(canDecode);
    m_decodeMaxLongSide.store(maxLongSide);
    m_capsPacer = remotedesktop::RdCapsResendPacer(canDecode);
}
