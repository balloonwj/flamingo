/**
 *  音视频通话会话提示对话框实现，BalloonCallDlg.cpp
 *  balloonwj@qq.com   2026-06-11（2026-07-31 最小化 / 置顶 / 大小画面对调）
 */
#include "stdafx.h"
#include "BalloonCallDlg.h"

#include <memory>

#include "../../Common/Log/IULog.h"
#include "../../net/IUSocket.h"      // CIUSocket::GetInstance().SendCallXxx
#include "../../net/CallProtocolClient.h"   // net::call_media_audio
#include "../../Session/UserSessionData.h"   // FMG_MSG_CALL_STOP_MEDIA（关窗请求协调器停媒体会话）
#include "../../UI/BalloonMain/NotificationSound.h"   // StopLoopSound（关窗 / 接听时停回铃、来电铃声）
#include "VideoFrameConvert.h"    // call::Nv12ToBgra（远端 / 本端帧 NV12→BGRA）
#include "../../UI/BalloonChatWnd/ChatIcons.h"   // DrawChatIcon（复用单聊窗那套线性图标：摄像机 / 听筒）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

// ---- 纯语音态固定尺寸（逻辑像素）-----------------------------------------
static const int kDlgWidth     = 340;   // 客户区宽
static const int kDlgHeight    = 128;   // 客户区高
static const int kDlgTitleBarH = 44;    // 标题栏高
static const int kMsgH         = 62;    // 提示文字区高
static const int kBottomBarH   = 34;    // 底部按钮栏高
static const int kBtnWidth     = 88;    // 按钮宽
static const int kBtnGap       = 12;    // 两按钮间距
static const COLORREF kMsgColor = RGB(0x33, 0x33, 0x33);   // 提示文字色

// ---- 视频通话「通话中」态固定尺寸（逻辑像素）-----------------------------
static const int kVideoDlgWidth   = 720;   // 视频态客户区宽
static const int kVideoRemoteH    = 405;   // 大画面区高（720×405≈16:9）
static const int kVideoBottomH    = 120;   // 底部栏高（放小画面 + 挂断）
static const int kPipW            = 160;   // 小画面（画中画）宽
static const int kPipH            = 90;    // 小画面（画中画）高（16:9）
static const int kVideoBtnH       = 32;    // 底部栏按钮高
static const int kVideoSideMargin = 24;    // 底部栏左右内边距（按钮与窗口边缘之间的留白）
// 视频态窗口可拖动改大小，这是能拖到的最小客户区：画面区留 16:9 的 480×270，再加底部栏。
// 比这更小时底部栏（小画面 160×90 + 挂断按钮）就要挤到一起了。
static const int kVideoMinClientW = 480;   // 视频态最小客户区宽
static const int kVideoMinClientH = 270 + kVideoBottomH;   // 视频态最小客户区高

// ---- 标题栏图标（左上角那枚小图标）--------------------------------------
static const int      kTitleIconSize  = 16;                      // 图标位图边长（像素），与标题栏绘制尺寸一致
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);     // 标题栏底色，须与 DuiFrameTitleBar 的填充色一致
static const COLORREF kTitleIconInk    = RGB(0x18, 0x6E, 0xDC);  // 图标描边色（主题蓝，与置顶态图钉同色）
static const float    kTitleIconStroke = 2.0f;                   // 描边宽度（以 ChatIcons 的 viewBox 24 为基准）
static const float    kTitleIconInset  = 1.0f;                   // 图标四周内缩（像素），避免描边被位图边缘切掉

// ---- 标题栏「固定」图标（自绘 16×16 图钉）--------------------------------
static const int kPinIconSize   = 16;   // 图标位图边长（像素），与 caption icon 的绘制尺寸一致
// 图钉造型（图标坐标系，左上角为原点，单位：像素）
static const int kPinHeadLeft   = 4;    // 钉帽左边界
static const int kPinHeadRight  = 11;   // 钉帽右边界（含）
static const int kPinHeadTop    = 3;    // 钉帽上边界
static const int kPinHeadBottom = 5;    // 钉帽下边界（含）
static const int kPinShaftLeft  = 7;    // 钉杆左边界
static const int kPinShaftRight = 8;    // 钉杆右边界（含）
static const int kPinShaftTop   = 6;    // 钉杆上边界（接钉帽）
static const int kPinTipY       = 12;   // 钉尖所在行
// 未置顶态用深灰（与标题栏三按钮的 glyph 同色系）；置顶态用主题蓝，一眼看出"已固定"
static const COLORREF kPinInkNormal = RGB(0x50, 0x50, 0x50);
static const COLORREF kPinInkActive = RGB(0x18, 0x6E, 0xDC);

// ---- 视频通话两路画面的初始占位提示（尚无画面时画在黑底上）--------------
// 让用户看得出"在等什么"，而不是对着一块黑屏猜。收到首帧后就只画画面、不再画提示。
static const TCHAR* const kLocalWaitTip  = _T("正在打开摄像头…");
static const TCHAR* const kRemoteWaitTip = _T("正在等待对方的画面…");

// ---- 控件 ctrlId（避开 DuiFrameWindow 保留的 1/2/3）---------------------
static const UINT kIdBtnLeft   = 10;   // 次按钮：来电场景的"拒绝"
static const UINT kIdBtnRight  = 11;   // 主按钮：来电"接听" / 呼叫"取消" / 通话中"挂断"
static const UINT kIdCanvasBig = 12;   // 视频通话大画面（双击对调大 / 小画面）
static const UINT kIdCanvasPip = 13;   // 视频通话小画面（单击对调大 / 小画面）
static const UINT kIdBtnSwitch = 14;   // 通话中：语音态"切到视频" / 视频态"切回语音"
static const UINT kIdBtnCamera = 15;

//通话时长刷新定时器：1 秒一跳。时长精度本就是秒级，再密只是白耗。
static const UINT_PTR kCallDurationTimerId = 1;
static const UINT     kCallDurationTimerMs = 1000;   // 视频态：开启 / 关闭本端摄像头（纯本地，不发请求）

//把图标位图的 (x,y) 像素写成不透明的 ink 色（32 位 BGRA；ink 已是最终色，premultiplied 下 alpha 取 255）。
static void SetPinIconPixel(unsigned char* px, int x, int y, COLORREF ink)
{
    if (x < 0 || y < 0 || x >= kPinIconSize || y >= kPinIconSize)
        return;

    unsigned char* p = px + (static_cast<size_t>(y) * kPinIconSize + x) * 4;
    p[0] = GetBValue(ink);
    p[1] = GetGValue(ink);
    p[2] = GetRValue(ink);
    p[3] = 255;
}

//生成 16×16「固定（图钉）」图标位图：透明底 + 钉帽 + 钉杆 + 钉尖。调用方持有所有权、用完
//DeleteObject；CaptionIconButton 对 32 位图走 AlphaBlend，透明底自然融入标题栏 / hover 高亮。
//  active：true=已置顶态（蓝色图钉）；false=未置顶态（深灰图钉）
static HBITMAP MakePinIcon(bool active)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kPinIconSize;
    bi.bmiHeader.biHeight      = -kPinIconSize;   //top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;

    unsigned char* px = static_cast<unsigned char*>(bits);
    memset(px, 0, static_cast<size_t>(kPinIconSize) * kPinIconSize * 4);   //全透明底

    COLORREF ink = active ? kPinInkActive : kPinInkNormal;

    //钉帽：一块横向矩形
    int x = 0;
    int y = 0;
    for (y = kPinHeadTop; y <= kPinHeadBottom; ++y)
    {
        for (x = kPinHeadLeft; x <= kPinHeadRight; ++x)
            SetPinIconPixel(px, x, y, ink);
    }

    //钉杆：钉帽下方 2px 宽的竖线
    for (y = kPinShaftTop; y < kPinTipY; ++y)
    {
        for (x = kPinShaftLeft; x <= kPinShaftRight; ++x)
            SetPinIconPixel(px, x, y, ink);
    }

    //钉尖：钉杆末端收成一个点
    SetPinIconPixel(px, kPinShaftLeft, kPinTipY, ink);

    return bmp;
}

//本模块自用的 GDI+ 初始化（画标题栏图标要用）。进程内多次 GdiplusStartup 是安全的，
//与 BalloonAboutDlg / AvatarImageUtil 等处同一套写法。
static ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

static void EnsureGdiplus()
{
    if (GdipToken() != 0)
        return;

    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, NULL);
}

//生成标题栏图标位图：视频通话画摄像机、纯语音画电话听筒，复用单聊窗那套线性图标。
//DuiFrameWindow 的标题栏用 SRCCOPY 绘图标、不混 alpha，故位图须自带与标题栏一致的底色
//（与 BalloonProfileDlg 等处的 MakeTitleIconBitmap 同口径）。
//返回的 HBITMAP 所有权归调用方，用完 DeleteObject。
static HBITMAP MakeCallTitleIcon(bool video)
{
    EnsureGdiplus();

    HDC     screen = ::GetDC(NULL);
    HDC     mem    = ::CreateCompatibleDC(screen);
    HBITMAP hbmp   = ::CreateCompatibleBitmap(screen, kTitleIconSize, kTitleIconSize);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || hbmp == NULL)
    {
        if (mem != NULL)
            ::DeleteDC(mem);
        if (hbmp != NULL)
            ::DeleteObject(hbmp);
        return NULL;
    }

    HGDIOBJ old = ::SelectObject(mem, hbmp);

    //先铺标题栏底色，图标的"透明"部分就是这层底色，贴到标题栏上看不出边界
    RECT   rc  = { 0, 0, kTitleIconSize, kTitleIconSize };
    HBRUSH hbr = ::CreateSolidBrush(kTitleBarBgColor);
    ::FillRect(mem, &rc, hbr);
    ::DeleteObject(hbr);

    //在底色上描边画图标（抗锯齿；不透明背景，无需处理预乘 alpha）
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Color ink(255, GetRValue(kTitleIconInk),
                           GetGValue(kTitleIconInk), GetBValue(kTitleIconInk));
        int kind = video ? balloonchatwnd::kChatIconVideo : balloonchatwnd::kChatIconPhone;
        balloonchatwnd::DrawChatIcon(g, kind,
                                     kTitleIconInset, kTitleIconInset,
                                     kTitleIconSize - kTitleIconInset * 2,
                                     ink, kTitleIconStroke);
    }

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

CBalloonCallDlg::CBalloonCallDlg()
    : m_mode(kModeCalling)
    , m_peerUserId(0)
    , m_cameraOn(false)
    , m_hOwnerWnd(NULL)
    , m_connectedTick(0)
    , m_taskbarIcon(NULL)
    , m_built(false)
    , m_videoCall(false)
    , m_localIsBig(false)
    , m_topmost(false)
    , m_msgLabel(NULL)
    , m_btnLeft(NULL)
    , m_btnRight(NULL)
    , m_bigCanvas(NULL)
    , m_pipCanvas(NULL)
    , m_pinIconId(0)
    , m_pinIconNormal(NULL)
    , m_pinIconActive(NULL)
    , m_titleIconVideo(NULL)
    , m_titleIconAudio(NULL)
{
}

CBalloonCallDlg::~CBalloonCallDlg()
{
    //标题栏上的图标位图都由本类拥有（AddCaptionIcon / SetIcon 都只存裸指针），此处统一释放
    if (m_pinIconNormal != NULL)
        ::DeleteObject(m_pinIconNormal);
    if (m_pinIconActive != NULL)
        ::DeleteObject(m_pinIconActive);
    if (m_titleIconVideo != NULL)
        ::DeleteObject(m_titleIconVideo);
    if (m_titleIconAudio != NULL)
        ::DeleteObject(m_titleIconAudio);
}

void CBalloonCallDlg::EnsureWindow(HWND owner)
{
    //记下 owner：关窗时要经它请求协调器停媒体会话（见 OnCloseMsg）
    m_hOwnerWnd = owner;
    if (IsWindow())
        return;

    // owner 传 NULL：让子控件通知自路由回本对话框（与远程桌面 / 关闭确认对话框同处理）。
    // 窗口名先给个中性的"通话"，真正的标题（含通话类型与对端昵称）随后由 ApplyMode 设置。
    if (Create(NULL, CWindow::rcDefault, _T("通话"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("通话对话框 Create 失败。"));
        return;
    }
    SetTitleBarHeight(kDlgTitleBarH);
    SetButtons(false, false, true);   // 呼叫 / 来电态只留关闭按钮（视频通话中态另加最小化，见 OpenConnected）
    SetResizable(false);
    SetMinSize(kDlgWidth, kDlgHeight + kDlgTitleBarH);

    // 新窗口的标题栏是新建的：上一次通话装过的 caption icon 与置顶态都已不复存在，须复位，
    // 否则下次 EnsurePinCaptionIcon 会拿着失效的 id 去 Remove。
    m_pinIconId  = 0;
    m_topmost    = false;
    m_localIsBig = false;

    // 窗口类未带 CS_DBLCLKS，须开启 DuiHost 的双击合成，双击大画面才能收到 DUIN_DBLCLK。
    EnableDoubleClick(true);
}

void CBalloonCallDlg::ApplyMediaMode(bool video, bool openCamera)
{
    m_videoCall = video;
    //降级回语音时摄像头一律关闭；升级到视频时由调用方按"是否本端发起"决定
    m_cameraOn  = video ? openCamera : false;

    LOG_INFO("CallDlg: apply media mode, video=%d, cameraOn=%d, mode=%d.",
             video ? 1 : 0, m_cameraOn ? 1 : 0, m_mode);

    if (!IsWindow())
    {
        //窗口还没建（理论上不会走到）：只记状态，待 EnsureWindow 时按新状态建树
        return;
    }

    //重建媒体层：BuildUiOnce 每次调用都会清空旧控件指针并重搭控件树
    BuildUiOnce();
    ApplyMode();
    ApplyCanvasPlaceholders();
    Invalidate();
}

void CBalloonCallDlg::BuildUiOnce()
{
    // 重建控件树前先清空所有控件裸指针（旧树将被 SetClientContent 释放，指针随之失效）
    m_msgLabel  = NULL;
    m_btnLeft   = NULL;
    m_btnRight  = NULL;
    m_bigCanvas = NULL;
    m_pipCanvas = NULL;

    // 视频通话的「通话中」态用独立布局：上半部大画面铺满，底部栏左小画面、右挂断按钮。
    // 默认对方占大画面、自己在左下角小窗；用户可点小窗 / 双击大画面对调（见 SwapVideoCanvas）。
    //接通态一律用"视频对话框"这套基座（大窗 + 上方画面区 + 底部操作条）：语音态只是把
    //画面区换成对端头像 / 昵称占位，窗口尺寸与控件位置保持一致，切换时不会跳变。
    if (m_mode == kModeConnected)
    {
        std::unique_ptr<balloonwjui::DuiVBox> vroot(new balloonwjui::DuiVBox());

        // 大画面：铺满上半部（弹性占满底部栏之外的全部高度），双击对调
        std::unique_ptr<call::CallVideoCanvas> big(new call::CallVideoCanvas());
        big->SetCtrlId(kIdCanvasBig);
        big->SetInteractive(true);
        big->SetTooltipText(_T("双击切换大 / 小画面"));
        m_bigCanvas = big.get();
        vroot->AddChild(std::move(big), balloonwjui::DuiLayout::Hint().Weight(1));

        // 底部栏：小画面（左）+ 弹性空白 + 挂断按钮（右），按钮 / 小画面交叉轴居中
        std::unique_ptr<balloonwjui::DuiHBox> vbottom(new balloonwjui::DuiHBox());

        //小画面只在视频态出现：语音态没有本端画面可放。
        if (m_videoCall)
        {
        std::unique_ptr<call::CallVideoCanvas> pip(new call::CallVideoCanvas());
        pip->SetCtrlId(kIdCanvasPip);
        pip->SetInteractive(true);
        pip->SetTooltipText(_T("点击切换大 / 小画面"));
        m_pipCanvas = pip.get();
        vbottom->AddChild(std::move(pip),
                          balloonwjui::DuiLayout::Hint().Fixed(kPipW, kPipH)
                              .Margin(kVideoSideMargin).AlignC(balloonwjui::DuiLayout::AlignCenter));
        }

        std::unique_ptr<balloonwjui::DuiControl> vspacer(new balloonwjui::DuiControl());
        vbottom->AddChild(std::move(vspacer), balloonwjui::DuiLayout::Hint().Weight(1));

        if (m_videoCall)
        {
        //视频态两个控件语义不同：「摄像头」只开关自己的画面、纯本地不发请求；
        //「切回语音」才请求会话降级、双方一起回语音态。只想关自己画面时不应把对方视频也掐掉。
        std::unique_ptr<balloonwjui::DuiButton> cam(new balloonwjui::DuiButton());
        cam->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cam->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cam->SetCtrlId(kIdBtnCamera);
        cam->SetText(m_cameraOn ? _T("关闭摄像头") : _T("开启摄像头"));
        vbottom->AddChild(std::move(cam),
                          balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kVideoBtnH)
                              .Margin(kVideoSideMargin).AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> toAudio(new balloonwjui::DuiButton());
        toAudio->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        toAudio->SetVariant(balloonwjui::DuiButton::Variant::Default);
        toAudio->SetCtrlId(kIdBtnSwitch);
        toAudio->SetText(_T("切回语音"));
        vbottom->AddChild(std::move(toAudio),
                          balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kVideoBtnH)
                              .Margin(kVideoSideMargin).AlignC(balloonwjui::DuiLayout::AlignCenter));
        }
        else
        {
            //语音态：同一位置放「切到视频」，点它请求会话升级（收到推送后才真正切）。
            std::unique_ptr<balloonwjui::DuiButton> toVideo(new balloonwjui::DuiButton());
            toVideo->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            toVideo->SetVariant(balloonwjui::DuiButton::Variant::Default);
            toVideo->SetCtrlId(kIdBtnSwitch);
            toVideo->SetText(_T("切到视频"));
            vbottom->AddChild(std::move(toVideo),
                              balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kVideoBtnH)
                                  .Margin(kVideoSideMargin).AlignC(balloonwjui::DuiLayout::AlignCenter));
        }

        std::unique_ptr<balloonwjui::DuiButton> hangup(new balloonwjui::DuiButton());
        hangup->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        hangup->SetVariant(balloonwjui::DuiButton::Variant::Default);
        hangup->SetCtrlId(kIdBtnRight);
        hangup->SetText(_T("挂断"));
        m_btnRight = hangup.get();
        vbottom->AddChild(std::move(hangup),
                          balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kVideoBtnH)
                              .Margin(kVideoSideMargin).AlignC(balloonwjui::DuiLayout::AlignCenter));

        vroot->AddChild(std::move(vbottom), balloonwjui::DuiLayout::Hint().Fixed(kVideoBottomH));

        SetClientContent(std::move(vroot));
        //两块画面在收到首帧之前各自显示一句占位提示（本端"正在打开摄像头…"、对端"正在等待…"）
        ApplyCanvasPlaceholders();
        m_built = true;
        return;
    }

    // 每次 Open / 切形态按当前 m_mode 重建控件树：呼叫 / 通话中=一个按钮，来电=两个按钮，
    // 避免在不同形态间切换按钮可见性带来的布局刷新问题（与远程桌面对话框同策略）。
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());

    std::unique_ptr<balloonwjui::DuiLabel> msg(new balloonwjui::DuiLabel());
    msg->SetTextColor(kMsgColor);
    msg->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    m_msgLabel = msg.get();
    root->AddChild(std::move(msg), balloonwjui::DuiLayout::Hint().Fixed(kMsgH));

    std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());

    // 按钮组整体水平居中：左右各放一块等权重的弹性空白把按钮夹在中间，
    // 避免按钮紧贴窗口右边缘（否则观感上"挤"在右下角）。
    std::unique_ptr<balloonwjui::DuiControl> spacerLeft(new balloonwjui::DuiControl());
    bottom->AddChild(std::move(spacerLeft), balloonwjui::DuiLayout::Hint().Weight(1));

    m_btnLeft  = NULL;
    m_btnRight = NULL;


    if (m_mode == kModeIncoming)
    {
        // 拒绝（次要）
        std::unique_ptr<balloonwjui::DuiButton> reject(new balloonwjui::DuiButton());
        reject->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        reject->SetVariant(balloonwjui::DuiButton::Variant::Default);
        reject->SetCtrlId(kIdBtnLeft);
        reject->SetText(_T("拒绝"));
        m_btnLeft = reject.get();
        bottom->AddChild(std::move(reject), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kBtnGap));

        // 接听（主要）
        std::unique_ptr<balloonwjui::DuiButton> accept(new balloonwjui::DuiButton());
        accept->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        accept->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        accept->SetCtrlId(kIdBtnRight);
        accept->SetText(_T("接听"));
        m_btnRight = accept.get();
        bottom->AddChild(std::move(accept), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));
    }
    else
    {
        // 呼叫中"取消" / 通话中"挂断"：单个按钮
        std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
        btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        btn->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btn->SetCtrlId(kIdBtnRight);
        btn->SetText(m_mode == kModeConnected ? _T("挂断") : _T("取消"));
        m_btnRight = btn.get();
        bottom->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));
    }

    // 右侧弹性空白：与左侧那块等权重，二者一起把按钮组顶到水平正中
    std::unique_ptr<balloonwjui::DuiControl> spacerRight(new balloonwjui::DuiControl());
    bottom->AddChild(std::move(spacerRight), balloonwjui::DuiLayout::Hint().Weight(1));

    root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));

    SetClientContent(std::move(root));
    m_built = true;
}

void CBalloonCallDlg::ApplyMode()
{
    // 标题栏：呼叫 / 来电态标明通话类型，通话中态写明在跟谁通话（"与 XXX 视频通话中"）
    SetTitle(call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall));
    // 标题栏左上角图标：视频通话用摄像机、纯语音用听筒。位图各建一次、跨多次通话复用。
    ApplyTitleIcon();

    // 视频通话中态是满屏画面布局，没有提示文字控件
    if (m_msgLabel == NULL)
        return;

    m_msgLabel->SetText(call::MakeCallPromptText(m_mode, m_peerName, m_videoCall));
}

//把标题栏那张 32bpp BGRA 位图转成 HICON，供任务栏 / Alt+Tab 使用，使两处图案一致。
//CreateIconIndirect 要求 ICONINFO 带掩码位图；32bpp 带 alpha 的彩色位图配一张同尺寸的
//单色掩码即可（掩码内容会被 alpha 覆盖，故不必逐像素填）。返回的 HICON 由调用方 DestroyIcon。
static HICON MakeIconFromBitmap(HBITMAP hbm)
{
    if (hbm == NULL)
        return NULL;

    BITMAP bm;
    ::ZeroMemory(&bm, sizeof(bm));
    if (::GetObject(hbm, sizeof(bm), &bm) == 0)
        return NULL;

    HBITMAP hMask = ::CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
    if (hMask == NULL)
        return NULL;

    ICONINFO ii;
    ::ZeroMemory(&ii, sizeof(ii));
    ii.fIcon    = TRUE;
    ii.hbmMask  = hMask;
    ii.hbmColor = hbm;

    HICON icon = ::CreateIconIndirect(&ii);
    ::DeleteObject(hMask);
    return icon;
}

//把通话时长格式化成 "MM:SS"；超过一小时用 "H:MM:SS"，避免分钟数无限增长到看不懂。
static CString FormatCallDuration(ULONGLONG elapsedMs)
{
    ULONGLONG totalSec = elapsedMs / 1000;
    int hour = (int)(totalSec / 3600);
    int minute = (int)((totalSec % 3600) / 60);
    int second = (int)(totalSec % 60);

    CString text;
    if (hour > 0)
        text.Format(_T("%d:%02d:%02d"), hour, minute, second);
    else
        text.Format(_T("%02d:%02d"), minute, second);
    return text;
}

void CBalloonCallDlg::ApplyTitleIcon()
{
    // 两种图标各建一次即可，之后跨"关闭再打开"复用（本类持有所有权，析构时释放）
    if (m_videoCall)
    {
        if (m_titleIconVideo == NULL)
            m_titleIconVideo = MakeCallTitleIcon(true);
        if (m_titleIconVideo != NULL)
            SetIcon(m_titleIconVideo);
        ApplyTaskbarIcon();
        return;
    }

    if (m_titleIconAudio == NULL)
        m_titleIconAudio = MakeCallTitleIcon(false);
    if (m_titleIconAudio != NULL)
        SetIcon(m_titleIconAudio);

    ApplyTaskbarIcon();
}

LRESULT CBalloonCallDlg::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                    LPARAM /*lParam*/, BOOL& bHandled)
{
    if ((UINT_PTR)wParam != kCallDurationTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    RefreshCallDuration();
    return 0;
}

CString CBalloonCallDlg::GetCallDurationText() const
{
    //从未接通（呼叫被拒 / 无应答 / 对方忙）时 m_connectedTick 为 0，返回空串，
    //调用方据此决定提示里带不带时长。
    if (m_connectedTick == 0)
        return CString();
    return FormatCallDuration(::GetTickCount64() - m_connectedTick);
}

void CBalloonCallDlg::RefreshCallDuration()
{
    //只有接通态才有时长可言；呼叫中 / 来电态 m_connectedTick 为 0。
    if (m_mode != kModeConnected || m_connectedTick == 0 || !IsWindow())
        return;

    //用 tick 差而非墙上时钟：不受系统时间被调整的影响。
    ULONGLONG elapsed = ::GetTickCount64() - m_connectedTick;

    CString title = call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall);
    title += _T("  ");
    title += FormatCallDuration(elapsed);
    SetTitle(title);
}

void CBalloonCallDlg::ApplyTaskbarIcon()
{
    if (!IsWindow())
        return;

    //任务栏 / Alt+Tab 的图标与标题栏用同一张位图转出，保证两处图案一致。
    HBITMAP src = m_videoCall ? m_titleIconVideo : m_titleIconAudio;
    HICON icon = MakeIconFromBitmap(src);
    if (icon == NULL)
        return;

    //先挂上新图标再销毁旧的：反过来会让窗口短暂没有图标。
    ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG,   (LPARAM)icon);
    ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    if (m_taskbarIcon != NULL)
        ::DestroyIcon(m_taskbarIcon);
    m_taskbarIcon = icon;
}

call::CallVideoCanvas* CBalloonCallDlg::CanvasForFrame(bool frameIsLocal)
{
    // 非视频通话中态没有画面控件（两个裸指针都是空），调用方据空指针跳过绘制
    if (call::PickCanvasForFrame(m_localIsBig, frameIsLocal) == call::kCallCanvasBig)
        return m_bigCanvas;

    return m_pipCanvas;
}

void CBalloonCallDlg::SwapVideoCanvas()
{
    // 仅视频通话中态可对调
    if (!m_videoCall || m_mode != kModeConnected)
        return;
    if (m_bigCanvas == NULL || m_pipCanvas == NULL)
        return;

    m_localIsBig = !m_localIsBig;

    // 连同已持有的帧一起交换：对调瞬间画面即时互换，不必等各自下一帧到来；
    // 某一路当前无画面（如对方摄像头未开）时换过去就是黑底，与后续来帧一致。
    m_bigCanvas->SwapFrame(*m_pipCanvas);
    // 占位提示是"跟着这一路画面"的（如"摄像头不可用"始终贴在本端那一路），故对调后要重贴
    ApplyCanvasPlaceholders();
    m_bigCanvas->Invalidate();
    m_pipCanvas->Invalidate();

    LOG_INFO("Call dialog swap video canvas, localIsBig=%d, session=%s.",
             m_localIsBig ? 1 : 0, m_sessionId.c_str());
}

void CBalloonCallDlg::ApplyCanvasPlaceholders()
{
    if (m_bigCanvas == NULL)
        return;

    // 语音态没有小画面控件：大画面区当作"对端信息位"，贴对端昵称（外加一行状态说明），
    // 让用户在同一个大窗里仍看得清在跟谁通话。
    if (!m_videoCall)
    {
        CString tip = m_peerName;
        if (tip.IsEmpty())
        {
            tip = _T("语音通话中");
        }
        m_bigCanvas->SetPlaceholderText(tip);
        return;
    }

    if (m_pipCanvas == NULL)
        return;

    // 本端那一路贴 m_localTip，对端那一路贴 m_remoteTip；谁在大画面由 m_localIsBig 决定
    call::CallVideoCanvas* localCanvas  = CanvasForFrame(true);
    call::CallVideoCanvas* remoteCanvas = CanvasForFrame(false);
    if (localCanvas != NULL)
        localCanvas->SetPlaceholderText(m_localTip);
    if (remoteCanvas != NULL)
        remoteCanvas->SetPlaceholderText(m_remoteTip);
}

void CBalloonCallDlg::SetCameraUnavailable(int reason)
{
    // 只有视频通话中态有画面控件可贴提示；纯语音态、以及通话已结束关窗后（事件晚到）都忽略
    if (!m_videoCall || m_mode != kModeConnected || !IsWindow())
        return;

    m_localTip = call::MakeCameraUnavailableTip(reason);

    // 采集中途断掉时本端画面控件里还留着最后一帧，会一直定格在那里。清掉它，让提示露出来。
    call::CallVideoCanvas* localCanvas = CanvasForFrame(true);
    if (localCanvas != NULL)
    {
        localCanvas->SetFrame(NULL, 0, 0);
        localCanvas->SetPlaceholderText(m_localTip);
        localCanvas->Invalidate();
    }

    LOG_ERROR("Call dialog show camera unavailable tip, reason=%d, session=%s.",
              reason, m_sessionId.c_str());
}

void CBalloonCallDlg::EnsurePinCaptionIcon()
{
    // 两种状态的图钉位图各生成一次，之后复用（本类持有所有权，析构时释放）
    if (m_pinIconNormal == NULL)
        m_pinIconNormal = MakePinIcon(false);
    if (m_pinIconActive == NULL)
        m_pinIconActive = MakePinIcon(true);

    HBITMAP icon = m_topmost ? m_pinIconActive : m_pinIconNormal;
    if (icon == NULL)
        return;

    // caption icon 的图标与提示文字都是添加时定死的，状态变化只能"摘掉旧的、按新状态装一个"。
    // 本窗口只有这一个 caption icon，重装不会打乱标题栏图标顺序。
    if (m_pinIconId != 0)
    {
        RemoveCaptionIcon(m_pinIconId);
        m_pinIconId = 0;
    }
    m_pinIconId = AddCaptionIcon(icon, m_topmost ? _T("取消固定（不再置顶）") : _T("固定窗口（置顶显示）"));
}

void CBalloonCallDlg::TogglePinned()
{
    if (!IsWindow())
        return;

    m_topmost = !m_topmost;
    ::SetWindowPos(m_hWnd, m_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 图标改色 + 提示文字改措辞，让用户看得出当前是不是已固定
    EnsurePinCaptionIcon();

    LOG_INFO("Call dialog toggle topmost, topmost=%d, session=%s.",
             m_topmost ? 1 : 0, m_sessionId.c_str());
}

void CBalloonCallDlg::OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeCalling;
    m_peerName   = peerName;
    m_peerUserId = peerUserId;
    m_sessionId.clear();   // 会话 id 待邀请应答回来后由协调器 SetSessionId 回填

    BuildUiOnce();
    ApplyMode();
    ResizeClient(kDlgWidth, kDlgHeight);
    CenterWindow(owner);

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Call dialog open (calling), to=%lld.", (long long)peerUserId);
}

void CBalloonCallDlg::OpenIncoming(HWND owner, const CString& fromName,
                                   const std::string& sessionId, int64_t fromUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeIncoming;
    m_peerName   = fromName;
    m_peerUserId = fromUserId;
    m_sessionId  = sessionId;

    BuildUiOnce();
    ApplyMode();
    ResizeClient(kDlgWidth, kDlgHeight);
    CenterWindow(owner);

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Call dialog open (incoming), from=%lld, session=%s.",
             (long long)fromUserId, sessionId.c_str());
}

void CBalloonCallDlg::OpenConnected(HWND owner)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode = kModeConnected;
    // 复用呼叫 / 来电时已设好的 m_peerName 与 m_sessionId
    // 每次进通话中态都从"对方占大画面"开始（上一通电话对调过也不带到这一通）
    m_localIsBig = false;
    // 两路画面的初始占位提示：本端等摄像头打开、对端等首帧到来。摄像头出问题时
    // m_localTip 会被 SetCameraUnavailable 换成失败原因。
    m_localTip  = kLocalWaitTip;
    m_remoteTip = kRemoteWaitTip;

    BuildUiOnce();
    ApplyMode();
    // 接通后语音与视频共用同一个窗口形态（"以视频对话框为基座"，见
    // docs/音视频通话-媒体切换方案.md 5.2）：同样的大窗、同样可缩放、同样带最小化与
    // 「固定」置顶按钮。语音态只是把画面区换成对端头像 / 昵称占位。这样通话中切换媒体
    // 类型时窗口尺寸与控件位置都不变，不会出现窗口忽大忽小的跳变。
    SetButtons(true, false, true);
    EnsurePinCaptionIcon();
    // 允许拖四边 / 四角改大小（画面控件按新尺寸等比 letterbox 重绘，布局自适应）；
    // 最小尺寸挡住"小到底部栏放不下"的情况。
    SetResizable(true);
    SetMinSize(kVideoMinClientW, kVideoMinClientH + kDlgTitleBarH);
    ResizeClient(kVideoDlgWidth, kVideoRemoteH + kVideoBottomH);
    //接通即起表：时长用 tick 差算，不受系统时钟调整影响。每秒刷一次标题栏。
    m_connectedTick = ::GetTickCount64();
    ::SetTimer(m_hWnd, kCallDurationTimerId, kCallDurationTimerMs, NULL);
    RefreshCallDuration();

    CenterWindow(owner);

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Call dialog open (connected), session=%s, video=%d.",
             m_sessionId.c_str(), m_videoCall ? 1 : 0);
}

void CBalloonCallDlg::CloseDlg()
{
    // 协调器主动关闭：不再发信令（关闭原因已由服务端应答 / 推送决定）。
    if (IsWindow())
        DestroyWindow();
}

LRESULT CBalloonCallDlg::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                      LPARAM /*lParam*/, BOOL& bHandled)
{
    // 铃声不得活过通话窗 —— 这条不变式落在这里，是因为它是唯一一处所有关窗路径都必经的点。
    //
    // 实测踩过的坑（日志 20260801102112_19304.log）：被叫点"拒绝"走的是 kIdBtnLeft →
    // CloseDlg() → DestroyWindow()，而 CloseDlg 不发 WM_CLOSE，所以 OnCloseMsg 里那次停止
    // 碰不到；服务端又不会给拒接方回推通话状态，OnV2CallStateNotify 那次停止同样碰不到。
    // 结果被叫自己的来电铃声一直循环到进程退出。呼叫中点"取消"、通话中点"挂断"也是同一条
    // CloseDlg 路径，有同样的洞。
    //
    // 与其在拒绝 / 取消 / 挂断 / 标题栏关闭四个按钮上各补一次（漏一个就复发），不如把它绑在
    // 窗口销毁上：通话窗一没，铃声必停。无声在播时 StopLoopSound 是空操作，重复调用无害。
    // 性质与下面"关窗即停摄像头"完全一致。
    balloonmain::StopLoopSound();

    // 窗口销毁时 DuiHost 会把整棵控件树释放掉，本类持有的控件裸指针随之失效。这里先清空，
    // 避免通话结束后晚到的事件（迟到的视频帧、摄像头不可用通知）解引用悬空指针。
    //停掉时长定时器并清零起点：关窗后不再有窗口可刷，晚到的 WM_TIMER 也不该做事。
    ::KillTimer(m_hWnd, kCallDurationTimerId);
    m_connectedTick = 0;
    //释放任务栏图标（本类持有所有权）
    if (m_taskbarIcon != NULL)
    {
        ::DestroyIcon(m_taskbarIcon);
        m_taskbarIcon = NULL;
    }

    m_msgLabel  = NULL;
    m_btnLeft   = NULL;
    m_btnRight  = NULL;
    m_bigCanvas = NULL;
    m_pipCanvas = NULL;
    m_built     = false;

    // 标题栏也随窗口销毁，上面装的「固定」图标按钮与置顶态一并失效
    m_pinIconId = 0;
    m_topmost   = false;

    // 交回消息链：基类 DuiFrameWindow 要清自己的指针，DuiHost 要释放控件树
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonCallDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                    LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 用户点标题栏关闭按钮：等同取消（呼叫）/ 拒绝（来电）/ 挂断（通话中）。
    if (!m_sessionId.empty())
    {
        if (m_mode == kModeCalling)
            CIUSocket::GetInstance().SendCallCancel(m_sessionId);
        else if (m_mode == kModeIncoming)
            CIUSocket::GetInstance().SendCallResponse(m_sessionId, false);
        else
            CIUSocket::GetInstance().SendCallHangup(m_sessionId);
    }
    //销毁窗口前先请求协调器停掉媒体会话：通话窗不持有 CallMediaSession，只销毁窗口的话
    //采集线程会继续跑，摄像头灯一直亮着。正常挂断时协调器会在收到服务端 ended 推送后再停
    //一次，CallMediaSession::Stop 是幂等的，重复调用无害。
    if (m_hOwnerWnd != NULL && ::IsWindow(m_hOwnerWnd))
        ::PostMessage(m_hOwnerWnd, FMG_MSG_CALL_STOP_MEDIA, 0, 0);

    if (IsWindow())
        DestroyWindow();
    return 0;
}

LRESULT CBalloonCallDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                     LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 标题栏「固定」图标按钮：切换窗口置顶（caption icon 走 DUIFW_CAPTION_ICON_CLICK，
    // extra 是 AddCaptionIcon 返回的 captionId，与客户区控件的 ctrlId 无关）
    if (pNotify->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_pinIconId != 0
        && static_cast<int>(pNotify->extra) == m_pinIconId)
    {
        TogglePinned();
        return 0;
    }

    // 双击大画面：与单击小窗等效，都把自己 / 对方对调显示
    if (pNotify->code == balloonwjui::DUIN_DBLCLK && pNotify->ctrlId == kIdCanvasBig)
    {
        SwapVideoCanvas();
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdCanvasPip:
            // 单击左下角小画面：把小画面这一路换到大画面显示
            SwapVideoCanvas();
            return 0;

        case kIdBtnSwitch:
        {
            //切换媒体类型：只发请求，界面与引擎一律等服务端推回 media_changed 通知再改，
            //这样本端与对端看到的必然一致，也不会出现"本地已切、服务端其实拒绝了"的假象。
            if (m_mode != kModeConnected || m_sessionId.empty())
            {
                return 0;
            }
            int target = m_videoCall ? net::call_media_audio : net::call_media_video;
            CIUSocket::GetInstance().SwitchCallMedia(m_sessionId, target);
            return 0;
        }

        case kIdBtnCamera:
        {
            //纯本地：只开关自己的摄像头，不发请求、不影响会话媒体类型与对端画面。
            if (!m_videoCall)
            {
                return 0;
            }
            ApplyMediaMode(true, !m_cameraOn);
            return 0;
        }

        case kIdBtnLeft:
            // 来电场景的"拒绝"
            if (!m_sessionId.empty())
                CIUSocket::GetInstance().SendCallResponse(m_sessionId, false);
            CloseDlg();
            return 0;

        case kIdBtnRight:
            if (m_mode == kModeCalling)
            {
                // 呼叫中"取消"
                if (!m_sessionId.empty())
                    CIUSocket::GetInstance().SendCallCancel(m_sessionId);
                CloseDlg();
            }
            else if (m_mode == kModeIncoming)
            {
                // 来电"接听"：发应答但不关窗，待服务端推来接通信令后协调器切通话中态。
                //
                // 铃声要在按下的这一刻就停，不能等接通信令 —— 信令要等服务端走一圈才回来，
                // 中间这段时间用户已经按了接听却还在响铃，体感就是"按了没反应"；万一信令因
                // 网络问题迟迟不到，铃声更是无限期地响下去。
                balloonmain::StopLoopSound();
                if (!m_sessionId.empty())
                    CIUSocket::GetInstance().SendCallResponse(m_sessionId, true);
            }
            else
            {
                // 通话中"挂断"
                if (!m_sessionId.empty())
                    CIUSocket::GetInstance().SendCallHangup(m_sessionId);
                CloseDlg();
            }
            return 0;

        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}

// ============================ 视频帧交付（CallVideoDisplaySink）============================

void CBalloonCallDlg::OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride)
{
    // 视频解码线程上下文：转 BGRA、拷堆、PostMessage 转投 UI 线程画到远端画面控件
    PostNv12Frame(WM_CALL_REMOTE_FRAME, nv12, width, height, stride);
}

void CBalloonCallDlg::OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride)
{
    // 采集线程上下文：转 BGRA、拷堆、PostMessage 转投 UI 线程画到本地预览控件
    PostNv12Frame(WM_CALL_LOCAL_FRAME, nv12, width, height, stride);
}

void CBalloonCallDlg::PostNv12Frame(UINT msgId, const uint8_t* nv12, int width, int height, int stride)
{
    // 窗口未创建 / 已销毁：直接丢弃（避免 new 出来无人接收而泄漏）
    if (nv12 == NULL || width <= 0 || height <= 0 || !IsWindow())
        return;

    VideoFramePacket* pkt = new VideoFramePacket();
    pkt->m_w = width;
    pkt->m_h = height;
    pkt->m_bgra.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    // NV12→BGRA（自上而下、行紧排 stride=width*4）
    call::Nv12ToBgra(nv12, width, height, stride, pkt->m_bgra.data(), width * 4);

    if (!::PostMessage(m_hWnd, msgId, 0, reinterpret_cast<LPARAM>(pkt)))
        delete pkt;   // 投递失败（窗口已销毁）：就地释放
}

LRESULT CBalloonCallDlg::OnRemoteFrameMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    // UI 线程：远端帧按当前"谁占大画面"喂给对应画面控件并触发重绘
    VideoFramePacket* pkt = reinterpret_cast<VideoFramePacket*>(lParam);
    if (pkt != NULL)
    {
        call::CallVideoCanvas* canvas = CanvasForFrame(false);
        if (canvas != NULL && IsWindow())
        {
            canvas->SetFrame(pkt->m_bgra.data(), pkt->m_w, pkt->m_h);
            canvas->Invalidate();
        }
        delete pkt;
    }
    return 0;
}

LRESULT CBalloonCallDlg::OnLocalFrameMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    // UI 线程：本端采集帧按当前"谁占大画面"喂给对应画面控件并触发重绘
    VideoFramePacket* pkt = reinterpret_cast<VideoFramePacket*>(lParam);
    if (pkt != NULL)
    {
        call::CallVideoCanvas* canvas = CanvasForFrame(true);
        if (canvas != NULL && IsWindow())
        {
            canvas->SetFrame(pkt->m_bgra.data(), pkt->m_w, pkt->m_h);
            canvas->Invalidate();
        }
        delete pkt;
    }
    return 0;
}
