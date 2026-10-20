/**
 *  音视频通话会话提示对话框实现，BalloonCallDlg.cpp
 *  balloonwj@qq.com   2026-06-11（2026-07-31 最小化 / 置顶 / 大小画面对调；2026-10-02 迁入 FlamingoMedia.dll）
 */
#include "stdafx.h"
#include "BalloonCallDlg.h"

#include <memory>

#include "../../Source/Common/Log/IULog.h"
#include "CallProtocolClient.h"   // net::call_media_audio
#include "CallMessages.h"         // call::kMsgCallStopMedia（关窗时请求通话控制器停媒体会话）
#include "../../Source/UI/BalloonMain/NotificationSound.h"   // StopLoopSound（关窗 / 接听时停回铃、来电铃声）
#include "VideoFrameConvert.h"    // call::Nv12ToBgra（远端 / 本端帧 NV12→BGRA）
#include <objidl.h>               // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>              // EnsureGdiplus：语音态画面区用 GDI+ 画听筒，须先初始化
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"   // MoveWindowToWorkAreaCorner（来电弹在工作区右下角）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标，第 11 期 11a）
#include "../../Source/resource.h"                      // IDR_MAINFRAME（任务栏 / Alt+Tab 图标，取自 exe 模块的资源）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // 「P2P」徽章（2026-10-04 起）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../Common/WindowPin.h"            // 标题栏图钉的图标、提示与固定期间的拦截判定
#include "../Common/IncomingCallLayout.h"   // 右下角来电弹窗的共用版式（420×200，左侧头像）
#include "CallingLayout.h"                 // 呼叫中态的控件树与通话窗共用的尺寸
#include "CallAudioEngine.h"               // CallAudioEngine::kVolumeDefault / kVolumeMax（音量滑块的范围）
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSlider.h"   // 音量浮层里的滑块
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 信号图标的悬停详情（多行）

// ---- 呼叫中态与接通态共用的尺寸（逻辑像素，定义与说明见 CallingLayout.h）--------
static const int kDlgTitleBarH = call::kCallDlgTitleBarH;   // 标题栏高（来电态另用 flamingomedia::kIncomingTitleBarH）

// ---- 通话窗的默认尺寸（逻辑像素；呼叫中态与接通态相同）-----------------------------
static const int kVideoDlgWidth   = call::kCallDlgWidth;     // 默认整窗宽
static const int kVideoRemoteH    = call::kCallCanvasH;      // 默认大画面区高（405，原按 720 宽取 16:9）
static const int kVideoBottomH    = call::kCallBottomBarH;   // 底部栏高（放小画面 + 挂断）
static const int kPipW            = 160;   // 小画面（画中画）宽
static const int kPipH            = 90;    // 小画面（画中画）高（16:9）
static const int kDurationLabelH  = call::kCallBtnH;         // 视频态底部栏通话时长标签的高度
static const int kVideoSideMargin = call::kCallSideMargin;   // 底部栏左右内边距（按钮与窗口边缘之间的留白）
// 视频态窗口可拖动改大小，这是能拖到的最小客户区：画面区留 16:9 的 480×270，再加底部栏。
// 比这更小时底部栏（小画面 160×90 + 挂断按钮）就要挤到一起了。
// 底部栏最宽时（视频态：小画面 24+160+24、信号图标 20+8、通话时长 52、P2P 徽章 36、麦克风 / 音量 / 摄像头 / 切回语音 /
// 共享屏幕 / 共享电脑声音六个圆形按钮各 12+48、挂断 12+56、右边距 24）需要 776。2026-10-06 加信号图标前为 696、最小宽度
// 700；加图标后通话时长标签由 60 收窄到 52，最小宽度改为 716；同日加「共享电脑声音」按钮后为 776，用户选定把默认宽度
// （call::kCallDlgWidth）一并改为 776，接通时窗口不会被最小宽度撑大
static const int kVideoMinClientW = 776;   // 视频态最小客户区宽
static const int kDurationLabelW  = 52;    // 视频态底部栏通话时长标签的宽度（第 11 期 11b；2026-10-06 由 60 收窄）
static const int kSignalIconGap   = 8;     // 信号图标与右边控件（视频态的通话时长、语音态的 P2P 徽章）之间的间距
static const int kVoiceArtMargin  = call::kCallArtMargin;    // 语音态画面区与窗口左、上、右边缘之间的留白（第 11 期 11c）
static const COLORREF kDurationColor = RGB(0x5A, 0x5F, 0x6A);   // 视频态底部栏通话时长的文字颜色（2026-10-06 前，浅色底部栏上）
// 视频态操作条浮在画面上之后（2026-10-06 起）通话时长改用白字：操作条下面是画面与半透明黑色渐变
static const COLORREF kDurationOverlayColor = RGB(0xFF, 0xFF, 0xFF);
static const int kVideoMinClientH = 270 + kVideoBottomH;   // 视频态最小客户区高

// ---- 音量浮层（2026-10-05 起）-------------------------------------------
static const int kVolumePopupW       = 240;   // 浮层整体宽（像素）
static const int kVolumePopupH       = 48;    // 浮层整体高（像素）
static const int kVolumePopupPadX    = 16;    // 浮层左右内边距
static const int kVolumeLabelW       = 48;    // 右侧百分比文字的宽度（放得下「150%」）
static const int kVolumeLabelGap     = 8;     // 滑块与百分比文字之间的间距
static const int kVolumeSliderStep   = 5;     // 滑块每次键盘 / 滚轮调整的步长（%）
static const COLORREF kVolumeLabelColor = RGB(0x5A, 0x5F, 0x6A);   // 百分比文字颜色
// 浮层关闭后这段时间内（毫秒）点音量按钮视为「关闭浮层」：按下鼠标时浮层就因失去焦点关闭，抬起鼠标才触发按钮点击，
// 两者之间隔着一次完整的点击动作，取 500 毫秒以覆盖较慢的点击
static const ULONGLONG kVolumePopupReopenGuardMs = 500;

// ---- 标题栏图标（左上角那枚小图标）--------------------------------------
static const int      kTitleIconSize  = 16;                      // 图标位图边长（像素），与标题栏绘制尺寸一致
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);     // 标题栏底色，须与 DuiFrameTitleBar 的填充色一致

// ---- 画面区头像（没有画面时显示，docs/音视频通话优化方案.md 第 5 项）---------
// 头像位图的边长（像素）：与语音态画面区圆形的最大直径一致，画进更小的区域（小画面）时按比例缩小
static const int kCanvasAvatarPx = call::kVoiceArtCircle;

// ---- 视频通话两路画面的初始占位提示（尚无画面时画在黑底上）--------------
// 让用户看得出"在等什么"，而不是对着一块黑屏猜。收到首帧后就只画画面、不再画提示。
static const TCHAR* const kLocalWaitTip  = TR_NOOP(L"正在打开摄像头…");
static const TCHAR* const kRemoteWaitTip = TR_NOOP(L"正在等待对方的画面…");
// 本端摄像头关着时本端那一路的提示（画在本人头像下面）
static const TCHAR* const kLocalCameraOffTip = TR_NOOP(L"摄像头已关闭");
// 对方画面停了（对方关了摄像头，或网络卡住）时对方那一路的提示（画在对方头像下面）
static const TCHAR* const kRemoteStaleTip = TR_NOOP(L"对方未开启摄像头");
// Debug 版无设备模式下大画面顶部的提示（2026-10-05 起）：写明开关名，开发者一眼知道怎么关掉它
static const TCHAR* const kNoAudioDevicesTip = TR_NOOP(L"调试模式：未使用麦克风和扬声器（FLAMINGO_CALL_NO_AUDIO=1）");

// ---- 控件 ctrlId（避开 DuiFrameWindow 保留的 1/2/3）---------------------
static const UINT kIdBtnLeft   = 10;   // 次按钮：来电场景的"拒绝"
static const UINT kIdBtnRight  = 11;   // 主按钮：来电"接听" / 呼叫"取消" / 通话中"挂断"
static const UINT kIdCanvasBig = 12;   // 视频通话大画面（双击对调大 / 小画面）
static const UINT kIdCanvasPip = 13;   // 视频通话小画面（单击对调大 / 小画面）
static const UINT kIdBtnSwitch = 14;   // 通话中：语音态"切到视频" / 视频态"切回语音"
static const UINT kIdBtnCamera = 15;   // 视频态：开启 / 关闭本端摄像头（纯本地，不发请求）
static const UINT kIdBtnShare  = 16;   // 通话中：共享屏幕 / 停止共享（第 8 期，交给屏幕共享发起或结束）
static const UINT kIdBtnMic    = 17;   // 通话中：打开 / 关闭本端麦克风（2026-10-05 起，纯本地，不发信令）
static const UINT kIdBtnVolume = 18;   // 通话中：弹出音量浮层（2026-10-05 起，只调本端听到的声音）
static const UINT kIdVolumeSlider = 19;   // 音量浮层里的滑块（通知经浮层的所有者窗口即本窗口送达）
static const UINT kIdSignalIcon = 20;     // 通话中：底部栏的网络信号图标（2026-10-06 起，只供查看与悬停，点了没有动作）
static const UINT kIdNetToast = 21;       // 通话中：网络变差时画面区上方的提示（2026-10-06 起，不发通知，id 供单元测试查找）
static const UINT kIdBtnSysAudio = 23;    // 通话中：共享电脑声音 / 停止共享电脑声音（2026-10-06，docs/音视频通话方案.md 第 10 节）
static const UINT kIdOverlayBar = 22;     // 视频态：浮在画面底部的操作条（2026-10-06 起，不发通知，id 供单元测试查找）

//通话时长刷新定时器：1 秒一跳。时长精度本就是秒级，再密只是白耗。
static const UINT_PTR kCallDurationTimerId = 1;
static const UINT     kCallDurationTimerMs = 1000;   // 刷新间隔（毫秒）
//来电振铃倒计时的标题刷新定时器（第 15 期），每秒一次
static const UINT_PTR kRingTimerId = 2;
//视频态操作条淡入淡出的计时器（2026-10-06 起）：淡入淡出期间逐帧，等静止时长时到点一次，保持显示时低频复查
static const UINT_PTR kBarFadeTimerId = 3;
static const UINT     kBarFadeFrameMs = 16;    // 淡入淡出期间的刷新间隔（毫秒，约每秒 60 帧）
static const UINT     kBarHoldPollMs  = 500;   // 保持显示期间复查保持条件（鼠标是否还在操作条上、浮层是否还开着）的间隔（毫秒）
//服务端没有给出剩余振铃时间时按这个时长倒数：服务端的振铃超时（CallManager::kRingingTimeoutMs，毫秒）
static const int64_t  kDefaultRingMs = 45 * 1000;

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

CBalloonCallDlg::CBalloonCallDlg()
    : m_mode(kModeCalling)
    , m_peerUserId(0)
    , m_cameraOn(false)
    , m_cameraFailed(false)
    , m_hNotifyWnd(NULL)
    , m_actions(NULL)
    , m_connectedTick(0)
    , m_taskbarIcon(NULL)
    , m_built(false)
    , m_videoCall(false)
    , m_localIsBig(false)
    , m_topmost(false)
    , m_btnLeft(NULL)
    , m_btnRight(NULL)
    , m_bigCanvas(NULL)
    , m_pipCanvas(NULL)
    , m_btnShare(NULL)
    , m_screenSharing(false)
    , m_btnSysAudio(NULL)
    , m_systemAudioOn(false)
    , m_peerSysAudio(false)
    , m_btnMic(NULL)
    , m_btnVolume(NULL)
    , m_btnCamera(NULL)
    , m_btnSwitch(NULL)
    , m_micOff(false)
    , m_noAudioDevices(false)
    , m_volumeLabel(NULL)
    , m_volumePopupClosedTick(0)
    , m_pinIconId(0)
    , m_pinIconNormal(NULL)
    , m_pinIconActive(NULL)
    , m_titleIcon(NULL)
    , m_taskbarIconSmall(NULL)
    , m_durationLabel(NULL)
    , m_p2pBadge(NULL)
    , m_linkPath(0)
    , m_host(NULL)
    , m_incomingAvatar(NULL)
    , m_lastRemoteFrameTick(0)
    , m_remoteStale(false)
    , m_signalIcon(NULL)
    , m_netToast(NULL)
    , m_shownGrade(call::kCallGradeUnknown)
    , m_overlayBar(NULL)
    , m_mouseOverBar(false)
    , m_hasMousePt(false)
{
    m_lastMousePt.x = 0;
    m_lastMousePt.y = 0;
}

void CBalloonCallDlg::SetHost(flamingomedia::IMediaHost* host)
{
    m_host = host;
    m_avatars.SetHost(host);
}

void CBalloonCallDlg::SetLinkPath(int linkPath)
{
    m_linkPath = linkPath;
    balloonmain::ApplyP2PBadgeVisibility(m_p2pBadge, linkPath);
}

void CBalloonCallDlg::SetNoAudioDevices(bool on)
{
    m_noAudioDevices = on;
    LOG_INFO("Call dialog no-audio-devices banner %s, session=%s.", on ? "shown" : "hidden", m_sessionId.c_str());
    if (m_bigCanvas != NULL)
    {
        m_bigCanvas->SetBannerText(on ? TR(kNoAudioDevicesTip) : NULL);
        m_bigCanvas->Invalidate();
    }
}

CBalloonCallDlg::~CBalloonCallDlg()
{
    //标题栏上的图标位图都由本类拥有（AddCaptionIcon / SetIcon 都只存裸指针），此处统一释放
    if (m_pinIconNormal != NULL)
        ::DeleteObject(m_pinIconNormal);
    if (m_pinIconActive != NULL)
        ::DeleteObject(m_pinIconActive);
    if (m_titleIcon != NULL)
        ::DeleteObject(m_titleIcon);
}

void CBalloonCallDlg::EnsureWindow(HWND owner)
{
    //owner 只作为各 Open* 居中的参照，窗口本身不设 owner（原因见下方创建处的说明）
    (void)owner;
    if (IsWindow())
        return;

    // owner 传 NULL（原为让控件通知发回本窗口；2026-10-14 起有无所有者，控件通知都发回本窗口，
    // 见开发约定文档 UI 约定第 11 条）。
    // 窗口名先给个中性的"通话"，真正的标题（含通话类型与对端昵称）随后由 ApplyMode 设置。
    if (Create(NULL, CWindow::rcDefault, TR(L"通话"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("CallDlg: Create failed, err=%lu.", ::GetLastError());
        return;
    }
    SetTitleBarHeight(kDlgTitleBarH);
    //各形态的标题栏按钮、能否改大小与最小尺寸在各自的 Open* 里设定（呼叫态、接通态有最小化与最大化，来电态只有关闭）
    SetButtons(false, false, true);
    SetResizable(false);

    // 新窗口的标题栏是新建的：上一次通话装过的 caption icon 与置顶态都已不复存在，须复位，
    // 否则下次 EnsurePinCaptionIcon 会拿着失效的 id 去 Remove。
    m_pinIconId  = 0;
    m_topmost    = false;
    m_localIsBig = false;

    // 双击大画面要能收到 DUIN_DBLCLK。
    // （2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);
}

void CBalloonCallDlg::ApplyMediaMode(bool video, bool openCamera)
{
    //从语音切到视频：对方画面从头等起（之前那次视频留下的收帧时刻不算数，否则一进视频就被判为停止）
    if (video && !m_videoCall)
    {
        m_lastRemoteFrameTick = 0;
        m_remoteStale = false;
        m_remoteTip = TR(kRemoteWaitTip);
        m_localTip = TR(kLocalWaitTip);
        m_cameraFailed = false;
    }
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
    m_p2pBadge  = NULL;
    m_signalIcon = NULL;
    m_netToast  = NULL;
    m_overlayBar = NULL;
    m_mouseOverBar = false;
    m_btnLeft   = NULL;
    m_btnRight  = NULL;
    m_bigCanvas = NULL;
    m_pipCanvas = NULL;
    m_btnShare  = NULL;
    m_btnSysAudio = NULL;
    m_btnMic    = NULL;
    m_btnVolume = NULL;
    m_btnCamera = NULL;
    m_btnSwitch = NULL;
    m_durationLabel = NULL;
    m_incomingAvatar = NULL;
    // 控件树要重建：音量浮层若开着先关掉（浮层里的滑块调的是这一通的音量，关掉不影响已设的值）
    if (m_volumePopup.IsShowing())
        m_volumePopup.Hide();

    // 呼叫中态：与接通后同一副骨架（2026-10-05 用户要求，见 CallingLayout.h）。画面区画对方头像与「正在呼叫，等待对方
    // 接听…」，底部栏右侧只有「取消」；视频通话呼叫时同样画对方头像，接通后才打开摄像头
    if (m_mode == kModeCalling)
    {
        call::CallingBody body = call::BuildCallingBody(kIdCanvasBig, kIdBtnRight, m_buttonArt);
        m_bigCanvas = body.m_canvas;
        SetClientContent(std::move(body.m_root));
        ApplyCanvasPlaceholders();
        m_built = true;
        return;
    }

    // 来电态：右下角 420×200 的弹窗，左边对方头像，右边两行文字（对方名字、「邀请你语音 / 视频通话」），底部右侧
    // 「拒绝」「接听」（docs/音视频通话优化方案.md 第 1 项；版式与群通话来电共用，见 Common/IncomingCallLayout）
    if (m_mode == kModeIncoming)
    {
        flamingomedia::IncomingCallBody body = flamingomedia::BuildIncomingCallBody(
            m_avatars.MakeUserAvatar(m_peerUserId, flamingomedia::kIncomingAvatarPx),
            std::wstring((LPCWSTR)m_peerName), std::wstring((LPCWSTR)call::MakeIncomingSubText(m_videoCall)),
            kIdBtnLeft, kIdBtnRight, m_buttonArt);
        m_incomingAvatar = body.m_avatar;
        SetClientContent(std::move(body.m_root));
        m_built = true;
        return;
    }

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
        //语音态没有小画面可对调：画听筒图标，不响应点击、不挂「双击切换」提示（第 10 期 10b）
        if (m_videoCall)
        {
            big->SetInteractive(true);
            big->SetTooltipText(TR(L"双击切换大 / 小画面"));
        }
        else
        {
            big->SetVoiceArt(true);
        }
        m_bigCanvas = big.get();
        //语音态画面区四周留白（底边紧接底部栏），配合画面区自身的边框与略深的底色，与按钮栏分界清楚（第 11 期 11c）
        balloonwjui::DuiLayout::Hint bigHint = balloonwjui::DuiLayout::Hint().Weight(1);
        if (!m_videoCall)
            bigHint.Margin(kVoiceArtMargin, kVoiceArtMargin, kVoiceArtMargin, 0);
        vroot->AddChild(std::move(big), bigHint);

        // 底部栏：小画面（左）+ 弹性空白 + 挂断按钮（右），按钮 / 小画面交叉轴居中。
        // 视频态（2026-10-06 起）底部栏改为浮在画面底部、鼠标静止后淡出的 CallOverlayBar，大画面铺满整个客户区；
        // 小画面不再放在底部栏里，而是单独浮在左下角（操作条淡出时仍然可见），底部栏左端留出它的位置
        std::unique_ptr<balloonwjui::DuiHBox> vbottom;
        std::unique_ptr<call::CallVideoCanvas> pip;
        if (m_videoCall)
        {
            call::CallOverlayBar* overlay = new call::CallOverlayBar(kVideoBottomH);
            overlay->SetCtrlId(kIdOverlayBar);
            m_overlayBar = overlay;
            vbottom.reset(overlay);
        }
        else
        {
            vbottom.reset(new balloonwjui::DuiHBox());
        }

        //小画面只在视频态出现：语音态没有本端画面可放。
        if (m_videoCall)
        {
        pip.reset(new call::CallVideoCanvas());
        pip->SetCtrlId(kIdCanvasPip);
        pip->SetInteractive(true);
        pip->SetTooltipText(TR(L"点击切换大 / 小画面"));
        //浮在根容器左下角，在操作条高度内上下居中（与原先放在底部栏里的位置相同）
        pip->SetFloatingBottomLeft(kVideoSideMargin, (kVideoBottomH - kPipH) / 2, kPipW, kPipH);
        m_pipCanvas = pip.get();
        //底部栏左端留出小画面及其左右边距的位置
        std::unique_ptr<balloonwjui::DuiControl> pipSlot(new balloonwjui::DuiControl());
        vbottom->AddChild(std::move(pipSlot), balloonwjui::DuiLayout::Hint().Fixed(kPipW + kVideoSideMargin * 2));

        //网络信号图标（2026-10-06 起）：紧挨小画面右边，再往右是通话时长
        AddSignalIcon(vbottom.get(), 0);

        //通话时长：紧挨信号图标右边（第 11 期 11b：从标题栏挪进窗口）。操作条浮在画面上，用白字
        std::unique_ptr<balloonwjui::DuiLabel> dur(new balloonwjui::DuiLabel());
        dur->SetTextColor(kDurationOverlayColor);
        dur->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_durationLabel = dur.get();
        vbottom->AddChild(std::move(dur),
                          balloonwjui::DuiLayout::Hint().Fixed(kDurationLabelW, kDurationLabelH)
                              .AlignC(balloonwjui::DuiLayout::AlignCenter));
        }
        else
        {
            //语音态：网络信号图标在底部栏最左（2026-10-06 起），右边紧挨「P2P」徽章
            AddSignalIcon(vbottom.get(), kVideoSideMargin);
        }

        //「P2P」徽章：音视频经直连链路传输时显示（2026-10-04 起）。视频态紧挨通话时长，语音态紧挨信号图标
        std::unique_ptr<balloonwjui::DuiBadge> badge = balloonmain::MakeP2PBadgeControl();
        badge->SetVisible(balloonmain::ShouldShowP2PBadge(m_linkPath));
        m_p2pBadge = badge.get();
        vbottom->AddChild(std::move(badge), balloonwjui::DuiLayout::Hint()
                                                .Fixed(balloonmain::kP2PBadgeControlWidth,
                                                       balloonmain::kP2PBadgeControlHeight)
                                                .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiControl> vspacer(new balloonwjui::DuiControl());
        vbottom->AddChild(std::move(vspacer), balloonwjui::DuiLayout::Hint().Weight(1));

        //右侧一排圆形图标按钮（2026-10-05 起），顺序由 CallActionBarModel 决定：麦克风、音量、摄像头（仅视频态）、
        //切换媒体、共享屏幕、挂断。视频态的「摄像头」只开关自己的画面、纯本地不发请求；「切换媒体」才请求会话升降级、
        //双方一起切换。只想关自己画面时不应把对方视频也掐掉
        const std::vector<call::CallActionItem> items =
            call::BuildActionBar(call::kBarConnected, m_videoCall, m_cameraOn, m_micOff, m_screenSharing,
                                 m_systemAudioOn);
        for (size_t i = 0; i < items.size(); ++i)
            AddActionButton(vbottom.get(), items[i], i + 1 == items.size());

        if (m_videoCall)
        {
            //视频态：操作条与小画面都以 Fixed(0) 加入、不占纵向空间，各自按根容器的矩形浮到底部 / 左下角；
            //后加入的绘制在上层，故次序为大画面、操作条、小画面
            vroot->AddChild(std::move(vbottom), balloonwjui::DuiLayout::Hint().Fixed(0));
            vroot->AddChild(std::move(pip), balloonwjui::DuiLayout::Hint().Fixed(0));
        }
        else
        {
            vroot->AddChild(std::move(vbottom), balloonwjui::DuiLayout::Hint().Fixed(kVideoBottomH));
        }

        //网络变差的提示（2026-10-06 起）：挂在根容器末尾（按子控件顺序绘制，末尾即最上层），Fixed(0) 不占纵向空间，
        //DuiToast 自己定位到客户区顶部居中，即画面区上方
        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        toast->SetCtrlId(kIdNetToast);
        m_netToast = toast.get();
        vroot->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));

        SetClientContent(std::move(vroot));
        //两块画面在收到首帧之前各自显示一句占位提示（本端"正在打开摄像头…"、对端"正在等待…"）
        ApplyCanvasPlaceholders();
        //视频态操作条：先完全显示，鼠标静止后淡出；语音态停掉淡入淡出计时器
        m_barFade.Reset(static_cast<int64_t>(::GetTickCount64()));
        ApplyBarFade();
        m_built = true;
        return;
    }

    //三种形态都已在上面各自建好控件树，走不到这里
    LOG_ERROR("CallDlg: BuildUiOnce with unknown mode=%d.", m_mode);
}

void CBalloonCallDlg::ApplyMode()
{
    // 标题栏：呼叫 / 来电态标明通话类型，通话中态写明在跟谁通话（"与 XXX 视频通话中"）；来电态后接振铃倒计时（第 15 期）
    SetTitle(call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall));
    RefreshRingTitle();
    // 标题栏左上角图标：视频通话用摄像机、纯语音用听筒。位图各建一次、跨多次通话复用。
    ApplyTitleIcon();
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
    //语音态画面区用 GDI+ 画听筒，这里确保 GDI+ 已初始化（原先由标题栏图标的绘制顺带完成）
    EnsureGdiplus();
    //标题栏用程序图标（第 11 期 11a），与语音 / 视频无关；位图建一次，之后跨「关闭再打开」复用（本类持有所有权，析构时释放）
    if (m_titleIcon == NULL)
        m_titleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBgColor);
    if (m_titleIcon != NULL)
        SetIcon(m_titleIcon);

    ApplyTaskbarIcon();
}

void CBalloonCallDlg::RefreshRingTitle()
{
    if (m_mode != kModeIncoming || !m_ringCountdown.IsRunning() || !IsWindow())
        return;
    const CString base = call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall);
    const int64_t remaining = m_ringCountdown.RemainingMs(static_cast<int64_t>(::GetTickCount64()));
    SetTitle(balloonmain::MakeCornerCountdownTitle(std::wstring((LPCWSTR)base), remaining,
                                                   balloonmain::corner_countdown_ring).c_str());
}

LRESULT CBalloonCallDlg::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                    LPARAM /*lParam*/, BOOL& bHandled)
{
    //来电振铃倒计时：只刷新标题，到 0 后写「即将自动结束」，窗口由服务端的超时推送关闭
    if ((UINT_PTR)wParam == kRingTimerId)
    {
        RefreshRingTitle();
        return 0;
    }
    //视频态操作条的淡入淡出（2026-10-06 起）
    if ((UINT_PTR)wParam == kBarFadeTimerId)
    {
        ApplyBarFade();
        return 0;
    }
    if ((UINT_PTR)wParam != kCallDurationTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    RefreshCallDuration();
    CheckRemoteVideoStale();
    RefreshNetStatus(true);
    return 0;
}

LRESULT CBalloonCallDlg::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    //只旁听，消息照常交给基类分发给控件
    bHandled = FALSE;
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    //位置没变的 WM_MOUSEMOVE（窗口显示、尺寸变化时系统补发）不算鼠标活动，否则操作条永远不会淡出
    if (m_hasMousePt && pt.x == m_lastMousePt.x && pt.y == m_lastMousePt.y)
        return 0;
    m_lastMousePt = pt;
    m_hasMousePt = true;
    if (m_overlayBar == NULL)
        return 0;
    const RECT& barRect = m_overlayBar->GetRect();
    m_mouseOverBar = (::PtInRect(&barRect, pt) != FALSE);
    m_barFade.OnActivity(static_cast<int64_t>(::GetTickCount64()));
    ApplyBarFade();
    return 0;
}

LRESULT CBalloonCallDlg::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    //上次的位置保留不清：系统之后补发的同位置 WM_MOUSEMOVE 仍要被滤掉，不算鼠标活动
    m_mouseOverBar = false;
    if (m_overlayBar != NULL)
        ApplyBarFade();
    return 0;
}

void CBalloonCallDlg::ApplyBarFade()
{
    if (!IsWindow())
        return;
    if (m_overlayBar == NULL)
    {
        ::KillTimer(m_hWnd, kBarFadeTimerId);
        return;
    }
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    //鼠标停在操作条上、或音量浮层开着（浮层是从操作条上的按钮弹出的）时保持显示
    m_barFade.SetHold(m_mouseOverBar || m_volumePopup.IsShowing(), now);
    m_overlayBar->SetBarAlpha(m_barFade.Alpha(now));

    const int64_t wait = m_barFade.MsUntilNextChange(now);
    if (wait == 0)
    {
        ::SetTimer(m_hWnd, kBarFadeTimerId, kBarFadeFrameMs, NULL);
    }
    else if (wait > 0)
    {
        ::SetTimer(m_hWnd, kBarFadeTimerId, static_cast<UINT>(wait), NULL);
    }
    else if (m_barFade.IsHeld())
    {
        //保持显示：没有鼠标消息也要复查保持条件是否已解除（如音量浮层被关掉）
        ::SetTimer(m_hWnd, kBarFadeTimerId, kBarHoldPollMs, NULL);
    }
    else
    {
        //已完全隐藏：等下一次鼠标活动
        ::KillTimer(m_hWnd, kBarFadeTimerId);
    }
}

void CBalloonCallDlg::CheckRemoteVideoStale()
{
    if (!m_videoCall || m_mode != kModeConnected || m_remoteStale || !IsWindow())
        return;
    if (!call::IsRemoteVideoStale(static_cast<int64_t>(::GetTickCount64()),
                                  static_cast<int64_t>(m_lastRemoteFrameTick), call::kRemoteVideoStaleMs))
        return;

    //一对一通话没有「对方关了摄像头」的通知：对方画面停了就清掉定格的最后一帧，露出对方头像与提示；来了新帧立刻恢复
    m_remoteStale = true;
    m_remoteTip = TR(kRemoteStaleTip);
    call::CallVideoCanvas* remoteCanvas = CanvasForFrame(false);
    if (remoteCanvas != NULL)
    {
        remoteCanvas->SetFrame(NULL, 0, 0);
        remoteCanvas->Invalidate();
    }
    ApplyCanvasPlaceholders();
    LOG_INFO("Call dialog remote video stale, show avatar, session=%s.", m_sessionId.c_str());
}

LRESULT CBalloonCallDlg::OnSysCommandMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (m_topmost && flamingomedia::IsSysCommandBlockedWhenPinned(wParam))
        return 0;
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonCallDlg::OnWindowPosChangingMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    if (m_topmost)
        flamingomedia::KeepWindowPosWhenPinned(reinterpret_cast<WINDOWPOS*>(lParam));
    //基类也要处理（最小尺寸等），交回消息链
    bHandled = FALSE;
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

    //时长显示在窗口里（第 11 期 11b）：视频态在底部栏小画面右边，语音态在画面区图标下方；标题栏只写「与 XXX 通话中」
    const CString duration = FormatCallDuration(elapsed);
    if (m_videoCall)
    {
        if (m_durationLabel != NULL)
            m_durationLabel->SetText(duration);
        //画面右上角的时长胶囊（2026-10-06 起）：操作条淡出后仍能看到通话时长
        if (m_bigCanvas != NULL)
        {
            m_bigCanvas->SetCornerText(duration);
            m_bigCanvas->Invalidate();
        }
    }
    else if (m_bigCanvas != NULL)
    {
        m_bigCanvas->SetVoiceSubText(duration);
        m_bigCanvas->Invalidate();
    }
}

void CBalloonCallDlg::AddSignalIcon(balloonwjui::DuiHBox* row, int leftMargin)
{
    std::unique_ptr<call::CallSignalIcon> icon(new call::CallSignalIcon());
    icon->SetCtrlId(kIdSignalIcon);
    //控件排版之前先按设计尺寸贴一张「正在检测」的位图（没有位图时 DuiButton 会画成默认的按钮样式），
    //之后每秒按排版后的实际像素尺寸重新取
    icon->SetSignalBitmap(m_signalArt.Get(m_shownGrade, call::kCallSignalIconW, call::kCallSignalIconH));
    m_signalIcon = icon.get();
    row->AddChild(std::move(icon), balloonwjui::DuiLayout::Hint()
                                       .Fixed(call::kCallSignalIconW, call::kCallSignalIconH)
                                       .Margin(leftMargin, 0, kSignalIconGap, 0)
                                       .AlignC(balloonwjui::DuiLayout::AlignCenter));
    //悬停详情按当前快照登记；降级提示留给计时器判断（提示控件此时还没建好）
    RefreshNetStatus(false);
}

void CBalloonCallDlg::RefreshNetStatus(bool allowHint)
{
    if (m_signalIcon == NULL || m_mode != kModeConnected)
        return;

    //取快照：没有登记操作接收方（单元测试里的独立窗口）时按「正在检测」显示
    call::CallNetSnapshot snap;
    if (m_actions != NULL)
        snap = m_actions->GetNetSnapshot();
    const call::CallNetView view = call::MakeNetView(snap, m_linkPath);

    //图标：按排版后的实际像素尺寸取位图，高 DPI 下不经拉伸；还没排版（矩形为空）时用设计尺寸
    const RECT rc = m_signalIcon->GetRect();
    int widthPx = rc.right - rc.left;
    int heightPx = rc.bottom - rc.top;
    if (widthPx <= 0 || heightPx <= 0)
    {
        widthPx = call::kCallSignalIconW;
        heightPx = call::kCallSignalIconH;
    }
    m_signalIcon->SetSignalBitmap(m_signalArt.Get(view.m_grade, widthPx, heightPx));

    //悬停详情：五行，用换行符连接（balloonui 的悬停提示 2026-10-06 起支持多行）
    const std::vector<std::wstring> lines = call::NetDetailLines(view);
    std::wstring tip;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            tip += L"\n";
        tip += lines[i];
    }
    balloonwjui::DuiToolTipMgr::Inst().Register(m_signalIcon, tip.c_str());

    if (view.m_grade != m_shownGrade)
    {
        LOG_INFO("Call dialog network grade %d -> %d, my grade=%d, peer grade=%d, rtt=%d ms, link path=%d, session=%s.",
                 m_shownGrade, view.m_grade, snap.m_myGrade, call::PeerGradeOf(snap),
                 view.m_hasRtt ? static_cast<int>(view.m_rttMs) : -1, m_linkPath, m_sessionId.c_str());
        m_shownGrade = view.m_grade;
    }

    //对方开始或停止共享电脑声音（2026-10-06）：取对方最近一份报告的标志位，变化时在画面区上方提示一次
    const bool peerSysAudio = snap.m_peerReports > 0
                              && (snap.m_peerReport.m_flags & call::kCallReportFlagSharingSystemAudio) != 0;
    if (peerSysAudio != m_peerSysAudio)
    {
        m_peerSysAudio = peerSysAudio;
        LOG_INFO("Call dialog peer system audio %d, session=%s.", peerSysAudio ? 1 : 0, m_sessionId.c_str());
        ShowNotice(call::PeerSystemAudioText(peerSysAudio));
    }

    //降级提示：同一次降级只提示一次，回到「一般」及以上后才会再次提示
    if (!allowHint)
        return;
    const int hint = m_degradeNotifier.Feed(snap.m_myGrade, call::PeerGradeOf(snap));
    if (hint != call::call_hint_none && m_netToast != NULL)
    {
        LOG_INFO("Call dialog shows network hint %d, my grade=%d, peer grade=%d, session=%s.", hint, snap.m_myGrade,
                 call::PeerGradeOf(snap), m_sessionId.c_str());
        m_netToast->Show(call::DegradeHintText(hint).c_str());
    }
}

void CBalloonCallDlg::ApplyTaskbarIcon()
{
    if (!IsWindow())
        return;

    //任务栏 / Alt+Tab 用程序图标（第 11 期 11a）。图标资源在 exe 里：本类编进 FlamingoMedia.dll，
    //故显式从 exe 模块（GetModuleHandle(NULL)）加载，与远程桌面对话框的做法一致
    if (m_taskbarIcon == NULL)
    {
        m_taskbarIcon = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                           ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
                                           LR_DEFAULTCOLOR);
    }
    if (m_taskbarIconSmall == NULL)
    {
        m_taskbarIconSmall = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                                ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
                                                LR_DEFAULTCOLOR);
    }
    if (m_taskbarIcon != NULL)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_taskbarIcon);
    if (m_taskbarIconSmall != NULL)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_taskbarIconSmall);
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

    // Debug 版无设备模式的提示条只在接通态显示，跟着大画面走（建树、对调、切换媒体后都重贴）
    m_bigCanvas->SetBannerText((m_mode == kModeConnected && m_noAudioDevices) ? TR(kNoAudioDevicesTip) : NULL);

    // 呼叫中态（语音与视频相同）：画面区画对方头像，下面一行「正在呼叫，等待对方接听…」
    if (m_mode == kModeCalling)
    {
        m_bigCanvas->SetAvatar(m_avatars.GetUserBitmap(m_peerUserId, kCanvasAvatarPx));
        m_bigCanvas->SetPlaceholderText(call::MakeOutgoingStatusText());
        return;
    }

    // 语音态没有小画面控件：大画面区画对方的头像（docs/音视频通话优化方案.md 第 5 项，原先是听筒图标），头像下面一行写
    // 「语音通话中」（第 10 期 10b；在跟谁通话已经写在标题栏「与 XXX 语音通话中」里）
    if (!m_videoCall)
    {
        m_bigCanvas->SetAvatar(m_avatars.GetUserBitmap(m_peerUserId, kCanvasAvatarPx));
        m_bigCanvas->SetPlaceholderText(TR(L"语音通话中"));
        return;
    }

    if (m_pipCanvas == NULL)
        return;

    // 本端那一路贴 m_localTip（摄像头关着时贴「摄像头已关闭」）与本人头像，对端那一路贴 m_remoteTip 与对方头像；
    // 谁在大画面由 m_localIsBig 决定。头像只在该路没有画面时画出来
    call::CallVideoCanvas* localCanvas  = CanvasForFrame(true);
    call::CallVideoCanvas* remoteCanvas = CanvasForFrame(false);
    const int64_t selfId = (m_host != NULL) ? m_host->GetSelfUserId() : 0;
    if (localCanvas != NULL)
    {
        localCanvas->SetAvatar(selfId > 0 ? m_avatars.GetUserBitmap(selfId, kCanvasAvatarPx) : NULL);
        localCanvas->SetPlaceholderText((m_cameraOn || m_cameraFailed) ? m_localTip : CString(TR(kLocalCameraOffTip)));
    }
    if (remoteCanvas != NULL)
    {
        remoteCanvas->SetAvatar(m_avatars.GetUserBitmap(m_peerUserId, kCanvasAvatarPx));
        remoteCanvas->SetPlaceholderText(m_remoteTip);
    }
}

void CBalloonCallDlg::SetScreenSharing(bool sharing)
{
    m_screenSharing = sharing;
    //控件树只在通话中态有这个按钮；其余形态只记状态，重建控件树时按它取外观。共享中呈深灰底，提示文字改为「停止共享」
    RefreshActionButton(m_btnShare, call::kActionShare, sharing);
}

void CBalloonCallDlg::SetSystemAudioOn(bool on)
{
    m_systemAudioOn = on;
    //控件树只在通话中态有这个按钮；其余形态只记状态。共享中呈深灰底，提示文字改为「停止共享电脑声音」
    RefreshActionButton(m_btnSysAudio, call::kActionSystemAudio, on);
}

void CBalloonCallDlg::ShowNotice(const std::wstring& text)
{
    if (text.empty() || m_netToast == NULL || m_mode != kModeConnected)
        return;
    m_netToast->Show(text.c_str());
}

void CBalloonCallDlg::SetCameraUnavailable(int reason)
{
    // 只有视频通话中态有画面控件可贴提示；纯语音态、以及通话已结束关窗后（事件晚到）都忽略
    if (!m_videoCall || m_mode != kModeConnected || !IsWindow())
        return;

    m_localTip = call::MakeCameraUnavailableTip(reason);
    m_cameraFailed = true;

    // 摄像头已经用不了：按钮改回「开启摄像头」，重建控件树。采集中途断掉时本端画面控件里还留着最后一帧，
    // 重建后它随之清掉，露出本人头像与失败原因
    ApplyMediaMode(true, false);

    LOG_ERROR("Call dialog show camera unavailable tip, reason=%d, session=%s.",
              reason, m_sessionId.c_str());
}

//按钮种类 → 本窗口里的 ctrlId（OnDuiNotify 按 ctrlId 分发）
static UINT CtrlIdForAction(int actionId)
{
    switch (actionId)
    {
    //麦克风
    case call::kActionMute:
        return kIdBtnMic;
    //音量
    case call::kActionVolume:
        return kIdBtnVolume;
    //摄像头（仅视频态）
    case call::kActionCamera:
        return kIdBtnCamera;
    //切换媒体
    case call::kActionSwitchMedia:
        return kIdBtnSwitch;
    //共享屏幕
    case call::kActionShare:
        return kIdBtnShare;
    //共享电脑声音
    case call::kActionSystemAudio:
        return kIdBtnSysAudio;
    //挂断与呼叫中的取消、来电的接听共用主按钮的 id，按形态区分含义
    case call::kActionHangup:
    case call::kActionCancel:
    case call::kActionAccept:
        return kIdBtnRight;
    //来电的拒绝
    case call::kActionReject:
        return kIdBtnLeft;
    //未知种类：不分配 id
    default:
        return 0;
    }
}

void CBalloonCallDlg::AddActionButton(balloonwjui::DuiHBox* row, const call::CallActionItem& item, bool last)
{
    if (row == NULL)
        return;
    //音量键的激活态在模型里恒为 false（它没有开关两态）；图标上「音量为 0」的斜杠由本窗口按当前音量决定
    const bool artActive = (item.m_id == call::kActionVolume) ? (CurrentVolume() == 0) : item.m_active;
    const call::CallButtonLook look = call::LookForAction(item.m_id, artActive, m_videoCall);
    const std::wstring tip = call::TooltipForAction(item.m_id, artActive, m_videoCall);

    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetCtrlId(CtrlIdForAction(item.m_id));
    //先设文字：位图画不出来时按钮仍显示文字，不至于变成空白；装上位图后文字会被清掉
    btn->SetText(tip.c_str());
    m_buttonArt.Apply(btn.get(), look, tip);

    //记下之后要刷新外观的按钮
    switch (item.m_id)
    {
    //麦克风：开关后刷新
    case call::kActionMute:
        m_btnMic = btn.get();
        break;
    //音量：音量跨过 0 时刷新
    case call::kActionVolume:
        m_btnVolume = btn.get();
        break;
    //摄像头：开关后整棵控件树重建，这里只记指针
    case call::kActionCamera:
        m_btnCamera = btn.get();
        break;
    //切换媒体：切换后整棵控件树重建
    case call::kActionSwitchMedia:
        m_btnSwitch = btn.get();
        break;
    //共享屏幕：共享状态变化时刷新
    case call::kActionShare:
        m_btnShare = btn.get();
        break;
    //共享电脑声音：开关或被屏幕共享接替时刷新
    case call::kActionSystemAudio:
        m_btnSysAudio = btn.get();
        break;
    //挂断
    case call::kActionHangup:
        m_btnRight = btn.get();
        break;
    //其余种类在接通态不出现
    default:
        break;
    }

    const int rightMargin = last ? kVideoSideMargin : 0;
    row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Fixed(look.m_diameter, look.m_diameter)
                                      .Margin(call::kCallRoundBtnGap, 0, rightMargin, 0)
                                      .AlignC(balloonwjui::DuiLayout::AlignCenter));
}

void CBalloonCallDlg::RefreshActionButton(balloonwjui::DuiButton* button, int actionId, bool active)
{
    if (button == NULL)
        return;
    m_buttonArt.Apply(button, call::LookForAction(actionId, active, m_videoCall),
                      call::TooltipForAction(actionId, active, m_videoCall));
}

void CBalloonCallDlg::ToggleMic()
{
    if (m_mode != kModeConnected || m_actions == NULL)
        return;
    //麦克风现在关着就打开，开着就关闭；以控制器返回的实际状态为准（打开失败时仍是关闭）
    const bool want = m_micOff;
    const bool micOn = m_actions->SetLocalMic(want);
    m_micOff = !micOn;
    RefreshActionButton(m_btnMic, call::kActionMute, m_micOff);
    LOG_INFO("Call dialog microphone %s requested, mic on=%d, session=%s.", want ? "on" : "off", micOn ? 1 : 0,
             m_sessionId.c_str());
}

int CBalloonCallDlg::CurrentVolume() const
{
    return (m_actions != NULL) ? m_actions->GetPlaybackVolume() : call::CallAudioEngine::kVolumeDefault;
}

void CBalloonCallDlg::ToggleVolumePopup()
{
    if (m_mode != kModeConnected || m_btnVolume == NULL || m_actions == NULL || !IsWindow())
        return;
    //浮层开着：这次点击关掉它。点按钮时浮层多半已先因失去焦点关闭，刚关闭不久的也按「关闭」处理，不重新打开
    if (m_volumePopup.IsShowing())
    {
        m_volumePopup.Hide();
        return;
    }
    if (m_volumePopupClosedTick != 0 && ::GetTickCount64() - m_volumePopupClosedTick < kVolumePopupReopenGuardMs)
        return;

    //每次打开都重建内容：浮层窗口可能已随上一个通话窗一起被系统销毁
    const int volume = CurrentVolume();
    std::unique_ptr<balloonwjui::DuiHBox> root(new balloonwjui::DuiHBox());
    std::unique_ptr<balloonwjui::DuiSlider> slider(new balloonwjui::DuiSlider());
    slider->SetCtrlId(kIdVolumeSlider);
    slider->SetRange(0, call::CallAudioEngine::kVolumeMax);
    slider->SetLineSize(kVolumeSliderStep);
    slider->SetPos(volume, false);
    //滑块不接收焦点：否则拖动后会画一圈虚线焦点框。滚轮按鼠标位置派发、与焦点无关，拖动与滚轮照常可用，只是不能用方向键调
    slider->SetTabStop(false);
    root->AddChild(std::move(slider), balloonwjui::DuiLayout::Hint().Weight(1).Margin(kVolumePopupPadX, 0, 0, 0));
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetTextColor(kVolumeLabelColor);
    label->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    CString text;
    text.Format(_T("%d%%"), volume);
    label->SetText(text);
    m_volumeLabel = label.get();
    root->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Fixed(kVolumeLabelW)
                                         .Margin(kVolumeLabelGap, 0, kVolumePopupPadX, 0));
    m_volumePopup.SetContent(std::move(root));
    m_volumePopup.SetSize(kVolumePopupW, kVolumePopupH);
    m_volumePopup.SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);
    m_volumePopup.SetDismissCallback(&CBalloonCallDlg::OnVolumePopupDismissed, this);

    //锚点：音量按钮在屏幕上的矩形，浮层优先弹在它上方（越界时自动翻到下方）
    RECT anchor = m_btnVolume->GetRect();
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.left));
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.right));
    m_volumePopup.Show(anchor, m_hWnd);
    LOG_INFO("Call dialog volume popup shown, volume=%d%%, session=%s.", volume, m_sessionId.c_str());
}

void CBalloonCallDlg::OnVolumeSliderChanged(int percent)
{
    if (m_actions == NULL)
        return;
    const bool wasZero = (CurrentVolume() == 0);
    m_actions->SetPlaybackVolume(percent);
    const int volume = CurrentVolume();
    if (m_volumeLabel != NULL)
    {
        CString text;
        text.Format(_T("%d%%"), volume);
        m_volumeLabel->SetText(text);
    }
    //调到 0 或从 0 调上来：音量按钮的斜杠随之出现或消失
    if (wasZero != (volume == 0))
        RefreshActionButton(m_btnVolume, call::kActionVolume, volume == 0);
}

void CBalloonCallDlg::OnVolumePopupDismissed(void* userdata, balloonwjui::DuiPopupHost::Reason reason)
{
    CBalloonCallDlg* self = static_cast<CBalloonCallDlg*>(userdata);
    if (self == NULL)
        return;
    self->m_volumePopupClosedTick = ::GetTickCount64();
    LOG_INFO("Call dialog volume popup closed, reason=%d, volume=%d%%, session=%s.", static_cast<int>(reason),
             self->CurrentVolume(), self->m_sessionId.c_str());
}

void CBalloonCallDlg::EnsurePinCaptionIcon()
{
    // 两种状态的图钉位图各生成一次，之后复用（本类持有所有权，析构时释放）
    if (m_pinIconNormal == NULL)
        m_pinIconNormal = flamingomedia::MakePinCaptionIcon(false);
    if (m_pinIconActive == NULL)
        m_pinIconActive = flamingomedia::MakePinCaptionIcon(true);

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
    m_pinIconId = AddCaptionIcon(icon, flamingomedia::PinCaptionTooltip(m_topmost));
}

void CBalloonCallDlg::TogglePinned()
{
    if (!IsWindow())
        return;

    //最大化时固定：先还原成普通大小再固定，否则固定后既不能还原也不能改大小
    if (!m_topmost && ::IsZoomed(m_hWnd))
        ShowWindow(SW_RESTORE);
    m_topmost = !m_topmost;
    const bool topmostNow = flamingomedia::ApplyWindowTopmost(m_hWnd, m_topmost);
    // 固定期间不能拖边框改大小、没有最小化与最大化按钮；拖标题栏、双击标题栏等由
    // OnSysCommandMsg / OnWindowPosChangingMsg 拦下。解除固定后恢复接通态的原样
    SetButtons(!m_topmost, !m_topmost, true);
    SetResizable(!m_topmost);

    // 图标改色 + 提示文字改措辞，让用户看得出当前是不是已固定
    EnsurePinCaptionIcon();

    LOG_INFO("Call dialog toggle pinned, pinned=%d, topmost now=%d, session=%s.",
             m_topmost ? 1 : 0, topmostNow ? 1 : 0, m_sessionId.c_str());
}

void CBalloonCallDlg::OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeCalling;
    m_peerName   = peerName;
    m_peerUserId = peerUserId;
    m_sessionId.clear();   // 会话 id 待邀请应答回来后由通话控制器 SetSessionId 回填

    //须在建控件树之前设标题栏高度（原因见 OpenIncoming）
    SetTitleBarHeight(kDlgTitleBarH);
    BuildUiOnce();
    ApplyMode();
    //呼叫中就用接通后的窗口形态（2026-10-05 用户要求）：同样的尺寸与最小尺寸，标题栏同样有图钉、最小化、最大化、
    //关闭，可拖边框改大小。对方接听后 OpenConnected 原地换控件树，不改尺寸与位置
    SetButtons(true, true, true);
    EnsurePinCaptionIcon();
    SetResizable(true);
    SetMinSize(kVideoMinClientW, kVideoMinClientH + kDlgTitleBarH);
    ResizeClient(kVideoDlgWidth, kVideoRemoteH + kVideoBottomH + kDlgTitleBarH);
    CenterWindow(owner);

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Call dialog open (calling), to=%lld.", (long long)peerUserId);
}

void CBalloonCallDlg::OpenIncoming(HWND owner, const CString& fromName,
                                   const std::string& sessionId, int64_t fromUserId, int64_t ringMs)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    m_mode       = kModeIncoming;
    m_peerName   = fromName;
    m_peerUserId = fromUserId;
    m_sessionId  = sessionId;
    //振铃倒计时（第 15 期）：先开始再 ApplyMode，标题一出来就带着剩余秒数
    m_ringCountdown.Start(ringMs >= 0 ? ringMs : kDefaultRingMs, static_cast<int64_t>(::GetTickCount64()));
    ::SetTimer(m_hWnd, kRingTimerId, balloonmain::kCornerCountdownTickMs, NULL);

    //标题栏高度必须在建控件树之前设：DuiFrameWindow::SetTitleBarHeight 在高度变化时会重建窗口骨架，已放进去的客户区
    //内容随之丢失（2026-10-05 界面联调时来电窗只剩标题栏即由此而来）
    SetTitleBarHeight(flamingomedia::kIncomingTitleBarH);
    //来电弹窗只有关闭按钮、不能改大小
    SetButtons(false, false, true);
    SetResizable(false);
    BuildUiOnce();
    ApplyMode();
    //来电弹窗的尺寸是整窗尺寸（含标题栏，见 Common/IncomingCallLayout.h）；最小尺寸随之放宽，否则会被抬到呼叫态的下限
    SetMinSize(flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH);
    ResizeClient(flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH);
    //来电弹在鼠标所在显示器工作区的右下角，置顶但不抢焦点（第 10 期 10a）：正在别的窗口打字时不被打断，
    //也不会因为恰好按了回车而误接听；接听或拒绝要用鼠标点
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX, balloonmain::kCornerPopupMarginY);

    ShowWindow(SW_SHOWNOACTIVATE);
    UpdateWindow();
    LOG_INFO("Call dialog open (incoming, bottom-right corner, topmost, not activated), from=%lld, session=%s, "
             "ring ms=%lld.", (long long)fromUserId, sessionId.c_str(), (long long)ringMs);
}

void CBalloonCallDlg::OpenConnected(HWND owner)
{
    EnsureWindow(owner);
    if (!IsWindow())
        return;

    //从来电态接听过来的：窗口原本在右下角置顶，接通后移到所在显示器工作区正中（第 10 期 10a）
    const bool fromIncoming = (m_mode == kModeIncoming);
    //从呼叫中态接通的：呼叫中用的就是接通后的窗口形态，尺寸、位置、最大化与固定状态都保持不变，只换控件树
    const bool fromCalling = (m_mode == kModeCalling);
    //呼叫时被最小化的：对方接听了，还原出来让用户看到
    if (::IsIconic(m_hWnd))
        ShowWindow(SW_RESTORE);
    m_mode = kModeConnected;
    //接通了，振铃倒计时不再需要
    ::KillTimer(m_hWnd, kRingTimerId);
    m_ringCountdown.Stop();
    // 复用呼叫 / 来电时已设好的 m_peerName 与 m_sessionId
    // 每次进通话中态都从"对方占大画面"开始（上一通电话对调过也不带到这一通）
    m_localIsBig = false;
    // 两路画面的初始占位提示：本端等摄像头打开、对端等首帧到来。摄像头出问题时
    // m_localTip 会被 SetCameraUnavailable 换成失败原因。
    m_localTip  = TR(kLocalWaitTip);
    m_remoteTip = TR(kRemoteWaitTip);
    m_lastRemoteFrameTick = 0;
    m_remoteStale = false;
    m_cameraFailed = false;
    // 每通电话从「麦克风打开」开始，与媒体会话 Start 时的复位一致；无设备模式的提示等媒体会话启动后再按事件打开
    m_micOff = false;
    m_noAudioDevices = false;
    // 网络状况：每通电话从「正在检测」开始，降级提示重新允许弹出
    m_shownGrade = call::kCallGradeUnknown;
    m_degradeNotifier.Reset();
    // 共享电脑声音：每通电话从「都没在共享」开始（2026-10-06）
    m_systemAudioOn = false;
    m_peerSysAudio = false;

    //须在建控件树之前设标题栏高度（原因见 OpenIncoming）
    SetTitleBarHeight(kDlgTitleBarH);
    BuildUiOnce();
    ApplyMode();
    // 接通后语音与视频共用同一个窗口形态（"以视频对话框为基座"，见
    // docs/音视频通话-媒体切换方案.md 5.2）：同样的大窗、同样可缩放、同样带最小化与
    // 「固定」置顶按钮。语音态只是把画面区换成对端头像 / 昵称占位。这样通话中切换媒体
    // 类型时窗口尺寸与控件位置都不变，不会出现窗口忽大忽小的跳变。2026-10-05 起另有最大化按钮。
    // 呼叫中已固定的，接通后仍固定：最小化、最大化按钮与拖边框保持关闭（与 TogglePinned 一致）
    SetButtons(!m_topmost, !m_topmost, true);
    EnsurePinCaptionIcon();
    // 允许拖四边 / 四角改大小（画面控件按新尺寸等比 letterbox 重绘，布局自适应）；
    // 最小尺寸挡住"小到底部栏放不下"的情况。
    SetResizable(!m_topmost);
    SetMinSize(kVideoMinClientW, kVideoMinClientH + kDlgTitleBarH);
    //大画面区 + 底部栏是标题栏以下的内容高度，整窗高度要加上标题栏。从呼叫中态接通的保持原尺寸
    if (!fromCalling)
        ResizeClient(kVideoDlgWidth, kVideoRemoteH + kVideoBottomH + kDlgTitleBarH);
    //接通即起表：时长用 tick 差算，不受系统时钟调整影响。每秒刷一次标题栏。
    m_connectedTick = ::GetTickCount64();
    ::SetTimer(m_hWnd, kCallDurationTimerId, kCallDurationTimerMs, NULL);
    RefreshCallDuration();

    if (fromIncoming)
    {
        //取消来电时的置顶，移到来电弹窗所在显示器的工作区正中；取不到显示器信息时退回以主面板居中
        RECT wnd = { 0, 0, 0, 0 };
        GetWindowRect(&wnd);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            const SIZE size = { wnd.right - wnd.left, wnd.bottom - wnd.top };
            const POINT origin = call::ComputeCenteredOrigin(mi.rcWork, size);
            ::SetWindowPos(m_hWnd, HWND_NOTOPMOST, origin.x, origin.y, 0, 0, SWP_NOSIZE);
        }
        else
        {
            ::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            CenterWindow(owner);
        }
    }
    else if (!fromCalling)
    {
        CenterWindow(owner);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Call dialog open (connected), session=%s, video=%d, from incoming=%d, from calling=%d, pinned=%d.",
             m_sessionId.c_str(), m_videoCall ? 1 : 0, fromIncoming ? 1 : 0, fromCalling ? 1 : 0, m_topmost ? 1 : 0);
}

void CBalloonCallDlg::RefreshAvatars()
{
    if (!IsWindow())
        return;
    //来电态的头像就地换位图；画面区（呼叫中态与接通态）重新贴头像。位图按内容缓存，旧位图仍由 m_avatars 持有，
    //不会在重画前失效
    if (m_incomingAvatar != NULL)
    {
        m_avatars.ApplyUser(m_incomingAvatar, m_peerUserId, flamingomedia::kIncomingAvatarPx);
        m_incomingAvatar->Invalidate();
    }
    ApplyCanvasPlaceholders();
    if (m_bigCanvas != NULL)
        m_bigCanvas->Invalidate();
    if (m_pipCanvas != NULL)
        m_pipCanvas->Invalidate();
}

void CBalloonCallDlg::CloseDlg()
{
    // 通话控制器主动关闭：不再发信令（关闭原因已由服务端应答 / 推送决定）。
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
    ::KillTimer(m_hWnd, kBarFadeTimerId);
    m_connectedTick = 0;
    ::KillTimer(m_hWnd, kRingTimerId);
    m_ringCountdown.Stop();
    //释放任务栏图标（本类持有所有权）
    if (m_taskbarIcon != NULL)
    {
        ::DestroyIcon(m_taskbarIcon);
        m_taskbarIcon = NULL;
    }
    if (m_taskbarIconSmall != NULL)
    {
        ::DestroyIcon(m_taskbarIconSmall);
        m_taskbarIconSmall = NULL;
    }

    m_p2pBadge  = NULL;
    m_signalIcon = NULL;
    m_netToast  = NULL;
    m_overlayBar = NULL;
    m_mouseOverBar = false;
    m_btnLeft   = NULL;
    m_btnRight  = NULL;
    m_bigCanvas = NULL;
    m_pipCanvas = NULL;
    m_btnShare  = NULL;
    m_btnSysAudio = NULL;
    m_btnMic    = NULL;
    m_btnVolume = NULL;
    m_btnCamera = NULL;
    m_btnSwitch = NULL;
    m_durationLabel = NULL;
    m_incomingAvatar = NULL;
    m_built     = false;
    // 音量浮层以本窗口为所有者，系统会随本窗口一起销毁它；先关掉，浮层里控件的指针随之作废
    if (m_volumePopup.IsShowing())
        m_volumePopup.Hide();
    m_volumeLabel = NULL;

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
    CloseByUser();
    return 0;
}

void CBalloonCallDlg::CloseByUser()
{
    const int signal = call::PickUserCloseSignal(m_mode, m_sessionId);
    if (signal != call::kCallCloseSignalNone && m_actions == NULL)
    {
        LOG_ERROR("CallDlg: no action receiver, close without signaling, mode=%d.", m_mode);
    }
    else
    {
        switch (signal)
        {
        //呼叫中：对方还没接听，发取消
        case call::kCallCloseSignalCancel:
            m_actions->CancelCall(m_sessionId);
            break;
        //来电中：发拒绝
        case call::kCallCloseSignalReject:
            m_actions->RespondCall(m_sessionId, false);
            break;
        //通话中：发挂断
        case call::kCallCloseSignalHangup:
            m_actions->HangupCall(m_sessionId);
            break;
        //还没拿到会话 id：没有可发的信令
        default:
            break;
        }
    }
    //销毁窗口前先请求通话控制器停掉媒体会话：通话窗不持有 CallMediaSession，只销毁窗口的话
    //采集线程会继续跑，摄像头灯一直亮着。服务端只把结束推送发给对方，主动关窗的这一端收不到推送，
    //只能靠这条请求收尾；对方先挂断时通话控制器会按推送再停一次，CallMediaSession::Stop 是幂等的，
    //重复调用无害。
    if (m_hNotifyWnd != NULL && ::IsWindow(m_hNotifyWnd))
        ::PostMessage(m_hNotifyWnd, call::kMsgCallStopMedia, 0, 0);
    LOG_INFO("CallDlg: closed by user, mode=%d, signal=%d, session=%s.", m_mode, signal, m_sessionId.c_str());

    if (IsWindow())
        DestroyWindow();
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

    // 音量浮层里的滑块变了（浮层以本窗口为所有者，它的通知送到这里）：extra 是新的音量百分比
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdVolumeSlider)
    {
        OnVolumeSliderChanged(static_cast<int>(pNotify->extra));
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        // 单击左下角小画面：把小画面这一路换到大画面显示
        case kIdCanvasPip:
            SwapVideoCanvas();
            return 0;

        // 麦克风按钮：打开或关闭本端麦克风（纯本地，不发信令）
        case kIdBtnMic:
            ToggleMic();
            return 0;

        // 音量按钮：弹出或关闭音量浮层
        case kIdBtnVolume:
            ToggleVolumePopup();
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
            if (m_actions != NULL)
                m_actions->SwitchCallMedia(m_sessionId, target);
            return 0;
        }

        case kIdBtnShare:
            // 共享屏幕 / 停止共享：交给通话控制器转给屏幕共享，按钮文字等它回告状态后再改
            if (m_mode == kModeConnected && m_actions != NULL)
                m_actions->ToggleScreenShare();
            return 0;

        case kIdBtnSysAudio:
        {
            // 共享电脑声音 / 停止共享电脑声音（2026-10-06）：交给通话控制器，按钮外观等它经 SetSystemAudioOn 回告；
            // 打不开时在画面区上方提示原因（对方版本不支持、采集不到电脑声音等）
            if (m_mode != kModeConnected || m_actions == NULL)
                return 0;
            const bool want = !m_systemAudioOn;
            const int result = m_actions->SetSystemAudio(want);
            LOG_INFO("Call dialog system audio button, want=%d, result=%d, session=%s.", want ? 1 : 0, result,
                     m_sessionId.c_str());
            if (want)
                ShowNotice(call::SystemAudioFailureText(result));
            return 0;
        }

        case kIdBtnCamera:
        {
            //开关自己的摄像头：经通话控制器真正启停本端采集（原先只改了界面、摄像头一直开着），
            //不发信令、不影响会话媒体类型与对端画面。打开失败时控制器另经 SetCameraUnavailable 告知原因
            if (!m_videoCall)
            {
                return 0;
            }
            const bool want = !m_cameraOn;
            if (want)
            {
                m_cameraFailed = false;
                m_localTip = TR(kLocalWaitTip);
            }
            const bool cameraOn = (m_actions != NULL) ? m_actions->SetLocalCamera(want) : false;
            ApplyMediaMode(true, cameraOn);
            return 0;
        }

        case kIdBtnLeft:
            // 来电场景的"拒绝"：与标题栏关闭按钮走同一个出口
            CloseByUser();
            return 0;

        case kIdBtnRight:
            if (m_mode == kModeCalling)
            {
                // 呼叫中"取消"：与标题栏关闭按钮走同一个出口
                CloseByUser();
            }
            else if (m_mode == kModeIncoming)
            {
                // 来电"接听"：发应答但不关窗，待服务端推来接通信令后通话控制器切通话中态。
                //
                // 铃声要在按下的这一刻就停，不能等接通信令 —— 信令要等服务端走一圈才回来，
                // 中间这段时间用户已经按了接听却还在响铃，体感就是"按了没反应"；万一信令因
                // 网络问题迟迟不到，铃声更是无限期地响下去。
                balloonmain::StopLoopSound();
                if (!m_sessionId.empty() && m_actions != NULL)
                    m_actions->RespondCall(m_sessionId, true);
            }
            else
            {
                // 通话中"挂断"：与标题栏关闭按钮走同一个出口。原先这里只发挂断信令就 CloseDlg，不请求停媒体会话，
                // 而服务端只通知对方，挂断的一方采集线程与摄像头一直开着（2026-10-04 界面联调查出）
                CloseByUser();
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
            //对方画面恢复：记下收帧时刻，之前判为停止的话改回等待提示（有画面时提示与头像都不画）
            m_lastRemoteFrameTick = ::GetTickCount64();
            if (m_remoteStale)
            {
                m_remoteStale = false;
                m_remoteTip = TR(kRemoteWaitTip);
                ApplyCanvasPlaceholders();
                LOG_INFO("Call dialog remote video resumed, session=%s.", m_sessionId.c_str());
            }
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
