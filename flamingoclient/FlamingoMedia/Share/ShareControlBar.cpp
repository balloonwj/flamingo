/**
 *  屏幕共享发起人的控制条，ShareControlBar.cpp
 *  说明见 ShareControlBar.h。2026-10-14 起建窗、摆位与收起 / 隐藏由公共基类 CShareTopBar（ShareTopBar.h）负责。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareControlBar.h"
#include "ShareMouseEffectMenu.h"   // AppendMouseEffectMenu（「更多」菜单里的鼠标效果，第 16 期）

#include <memory>
#include "ShareMessages.h"
#include "ShareRoomPolicy.h"   // ShareViewerCountText（第 13 期）
#include "ShareSources.h"
#include "../../Source/net/Protocol.h"   // cloudrecord_control_action（云端录制一组的命令，第 27b 期）
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr：录制红点的悬停提示
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // 「P2P」徽章（2026-10-04 起）
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForTexts：译文较长时加宽按钮

namespace screenshare {

namespace {

//整条宽在 BuildUi 里按有哪些按钮累加（2026-10-07 起按钮改成方形图标按钮，整条由原来的 1310 收窄到九百多像素）；
//标题条高、内容横条高、内边距、底色与文字色取顶部浮动条的公共常量（ShareBarButtonArt.h，2026-10-14）
//左侧状态文字的最小宽度（像素）：「正在共享 12:34 · 密码 482915 · 强制模式」放得下；P2P 徽章、「收回控制」出现时从它这里分宽度
const int kTipMinW = 360;
//状态点、录制红点所占的宽（像素）
const int kDotW = kShareTopBarDotW;
//「N 人观看 ▾」按钮宽（第 13 期），2026-10-07 起前面加了人形图标
const int kViewersBtnWidth = 104;
//观看者按钮前面人形图标的边长与图标后的间距（像素）
const int kViewersIconPx = 16;
const int kViewersIconGap = 4;
//本条自己总有的图标按钮个数：标注、录制、麦克风、电脑声音、摄像头、暂停、切换内容、更多、结束共享；
//最右端的「隐藏」「收起 / 固定」由基类加，宽度见 CShareTopBar::OwnButtonsWidth
const int kFixedIconButtons = 9;
//按钮上下留白，让按钮不顶满条高
const int kBtnVMargin = kShareTopBarBtnVMargin;
//量「N 人观看 ▾」按钮宽度时代入的观看人数（两位数），人数更多时文字以省略号结尾
const int kViewerCountSample = 99;
//控件间距
const int kRowGap = kShareBarBtnGap;
//底色（深灰，与被共享的内容区分开）与文字色（白）
const COLORREF kBarBgColor = kShareTopBarBgColor;
const COLORREF kBarTextColor = kShareTopBarTextColor;
//状态点的颜色：共享中为绿色，暂停时为灰色
const COLORREF kLiveDotColor = RGB(76, 175, 80);
const COLORREF kPausedDotColor = RGB(150, 155, 165);
//录制红点闪烁的定时器 id（与收起检查的 kTopBarTimerId 错开）
const UINT_PTR kBlinkTimerId = 0x5244;

} // namespace

CShareControlBar::CShareControlBar()
    : CShareTopBar(kIdHide, kIdToggle)
    , m_notifyWnd(NULL)
    , m_tipLabel(NULL)
    , m_p2pBadge(NULL)
    , m_linkPath(0)
    , m_forced(false)
    , m_viewersButton(NULL)
    , m_pauseButton(NULL)
    , m_micButton(NULL)
    , m_micOn(false)
    , m_sysAudioButton(NULL)
    , m_sysAudioOn(false)
    , m_cameraButton(NULL)
    , m_cameraOn(false)
    , m_annotButton(NULL)
    , m_annotating(false)
    , m_recordButton(NULL)
    , m_recording(false)
    , m_recordAllowed(true)
    , m_moreButton(NULL)
    , m_viewerCount(0)
    , m_paused(false)
    , m_revokeButton(NULL)
    , m_mineStopped(false)
    , m_otherScreens(0)
    , m_boardOpen(false)
    , m_screenBoardAvailable(false)
    , m_screenBoardActive(false)
    , m_captionAvailable(false)
    , m_captionSelf(false)
    , m_captionShow(true)
    , m_rebuildAfterMenu(false)
    , m_linkActive(false)
    , m_cloudRecordAvailable(false)
    , m_cloudRecording(false)
    , m_cloudRecordPaused(false)
    , m_linkNoPassword(false)
    , m_statusDot(NULL)
    , m_recordDot(NULL)
    , m_recordDotState(share_record_dot_hidden)
    , m_captionButton(NULL)
    , m_switchButton(NULL)
    , m_stopButton(NULL)
    , m_peopleIcon(NULL)
{
    memset(&m_sourceRect, 0, sizeof(m_sourceRect));
}

CShareControlBar::~CShareControlBar()
{
    Hide();
    //对象析构时不能再推迟：Hide 在菜单弹出期间只隐藏了窗口，这里直接销毁
    if (IsWindow())
        DestroyWindow();
    //人形图标：按钮随窗口销毁之后才释放
    if (m_peopleIcon != NULL)
    {
        ::DeleteObject(m_peopleIcon);
        m_peopleIcon = NULL;
    }
}

void CShareControlBar::EnsureWindow()
{
    //窗口还在：包括「更多」菜单弹出期间被隐藏、等待销毁的窗口，直接复用并取消推迟的销毁（否则 m_hWnd 不为空，再 Create
    //会触发 atlwin.h 的断言）
    if (IsWindow())
    {
        if (m_popup.TakePendingDestroy())
            LOG_INFO("screenshare: control bar reused while its menu is closing.");
        return;
    }
    //没有所有者的置顶小窗：不抢焦点、不进任务栏，每次摆放都放回最上层
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (!CreateTopBar(NULL, TR(L"屏幕共享"), WS_POPUP, exStyle))
        return;
    SetTopBarTopmost(true);
    //控制条本身默认不能被采进共享画面；「允许截图」打开时（2026-10-08）允许
    ApplyBarCaptureState(m_hWnd);
}

void CShareControlBar::BuildUi()
{
    //按当前状态整体重建，此前记下的「菜单关闭后重建」随之完成
    m_rebuildAfterMenu = false;
    //2026-10-07 起与录制条统一样式：左侧状态区（状态点、录制这场共享时闪烁的红点、状态文字），右侧一排方形图标按钮，
    //原来的按钮文字改作悬停提示（ShareBarButtonArt）。观看者按钮保留文字「N 人观看 ▾」，前面加人形图标；
    //「收回控制」是少见而要紧的操作，保留文字按钮
    const int viewersW = i18n::DefaultButtonWidthForTexts(ShareViewerCountText(0).c_str(),
                                                          ShareViewerCountText(kViewerCountSample).c_str(),
                                                          kViewersBtnWidth) + kViewersIconPx + kViewersIconGap;
    //「收回控制」平时隐藏，出现时从提示文字里分走宽度，不计入整条宽
    const int revokeW = i18n::DefaultButtonWidthForText(TR(L"收回控制"), kShareRevokeBtnMinW);
    //图标按钮：固定的 kFixedIconButtons 个，字幕能用时再加字幕
    const int iconButtons = m_captionAvailable ? kFixedIconButtons + 1 : kFixedIconButtons;
    SetTopBarWidth(kShareTopBarRowPadLeft + kShareTopBarRowPadRight + 2 * (kDotW + kRowGap) + kTipMinW + kRowGap +
                   viewersW + iconButtons * (kShareBarBtnPx + kRowGap) + OwnButtonsWidth());

    std::unique_ptr<balloonwjui::DuiHBox> row = MakeTopBarRow();

    //状态点：共享中为绿色，暂停时为灰色
    std::unique_ptr<balloonwjui::DuiLabel> statusDot(new balloonwjui::DuiLabel());
    statusDot->SetText(L"●");
    statusDot->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    m_statusDot = statusDot.get();
    row->AddChild(std::move(statusDot), balloonwjui::DuiLayout::Hint().Fixed(kDotW));

    //录制红点：正在「录下这场共享」时显示并闪烁；隐藏时提示文字占去它的位置，整条宽度不变
    std::unique_ptr<balloonwjui::DuiLabel> recDot(new balloonwjui::DuiLabel());
    recDot->SetText(L"●");
    recDot->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    recDot->SetVisible(m_recording);
    balloonwjui::DuiToolTipMgr::Inst().Register(recDot.get(), TR(L"正在录制这场共享"));
    m_recordDot = recDot.get();
    m_recordDotState = share_record_dot_hidden;
    row->AddChild(std::move(recDot), balloonwjui::DuiLayout::Hint().Fixed(kDotW));

    std::unique_ptr<balloonwjui::DuiLabel> tip(new balloonwjui::DuiLabel());
    tip->SetTextColor(kBarTextColor);
    tip->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_tipLabel = tip.get();
    row->AddChild(std::move(tip), balloonwjui::DuiLayout::Hint().Weight(1));

    //「P2P」徽章：画面经直连链路发给观看者时显示（2026-10-04 起）
    std::unique_ptr<balloonwjui::DuiBadge> badge = balloonmain::MakeP2PBadgeControl();
    badge->SetVisible(balloonmain::ShouldShowP2PBadge(m_linkPath));
    m_p2pBadge = badge.get();
    row->AddChild(std::move(badge), balloonwjui::DuiLayout::Hint()
                                        .Fixed(balloonmain::kP2PBadgeControlWidth, balloonmain::kP2PBadgeControlHeight)
                                        .AlignC(balloonwjui::DuiLayout::AlignCenter));

    //「收回控制」（第 18 期）：只在有人控制本人屏幕时可见；隐藏时提示文字占去它的位置，整条宽度不变
    std::unique_ptr<balloonwjui::DuiButton> revoke(new balloonwjui::DuiButton());
    revoke->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    revoke->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    revoke->SetText(TR(L"收回控制"));
    revoke->SetCtrlId(kIdRevokeControl);
    revoke->SetVisible(!m_controllerName.empty());
    m_revokeButton = revoke.get();
    row->AddChild(std::move(revoke),
                  balloonwjui::DuiLayout::Hint().Fixed(revokeW).Margin(0, kBtnVMargin, 0, kBtnVMargin));

    //「N 人观看 ▾」：点开观看者名单（第 13 期）；前面是人形图标
    std::unique_ptr<balloonwjui::DuiButton> viewers(new balloonwjui::DuiButton());
    viewers->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    viewers->SetCtrlId(kIdViewers);
    if (m_peopleIcon == NULL)
        m_peopleIcon = MakeShareBarIconBitmap(share_bar_icon_people, kViewersIconPx, kBarTextColor);
    viewers->SetLeadingIcon(m_peopleIcon);
    viewers->SetLeadingIconSize(kViewersIconPx);
    viewers->SetLeadingIconGap(kViewersIconGap);
    m_viewersButton = viewers.get();
    row->AddChild(std::move(viewers),
                  balloonwjui::DuiLayout::Hint().Fixed(viewersW).Margin(0, kBtnVMargin, 0, kBtnVMargin));

    //图标按钮（外观与提示由 refreshText 装上）
    m_annotButton = AddTopBarIconButton(row.get(), kIdAnnotate);
    m_recordButton = AddTopBarIconButton(row.get(), kIdRecord);
    m_micButton = AddTopBarIconButton(row.get(), kIdMic);
    m_sysAudioButton = AddTopBarIconButton(row.get(), kIdSysAudio);
    m_cameraButton = AddTopBarIconButton(row.get(), kIdCamera);
    //字幕（语音字幕）：「为我的发言生成字幕」的开关，字幕能使用时才有
    m_captionButton = m_captionAvailable ? AddTopBarIconButton(row.get(), kIdCaption) : NULL;
    m_pauseButton = AddTopBarIconButton(row.get(), kIdPause);
    m_switchButton = AddTopBarIconButton(row.get(), kIdSwitch);
    m_moreButton = AddTopBarIconButton(row.get(), kIdMore);
    m_stopButton = AddTopBarIconButton(row.get(), kIdStop);
    //「隐藏」（2026-10-07）与「收起 / 固定」（第 10 期 10c）：最右端，由基类加
    AddTopBarOwnButtons(row.get());

    SetClientContent(MakeTopBarRoot(std::move(row)));
    refreshText();
}

void CShareControlBar::applyArt(balloonwjui::DuiButton* button, int action, bool on)
{
    if (button == NULL)
        return;
    Art().Apply(button, ShareBarLookFor(action, on), ShareBarTooltipFor(action, on, false));
}

void CShareControlBar::refreshText()
{
    if (m_tipLabel != NULL)
    {
        //观看人数挪到了「N 人观看 ▾」按钮上（第 13 期），这里写状态、时长、密码、强制模式与说话人；共享标题写在标题栏
        std::wstring tip = MakeShareBarStatusText(m_paused, m_duration, m_password, m_forced, m_speaking);
        //正在云端录制（第 27b 期）：房间里的人都要知道
        if (m_cloudRecording)
            tip = std::wstring(m_cloudRecordPaused ? TR(L"云端录制已暂停") : TR(L"云端录制中")) + L" · " + tip;
        //录下这场共享（2026-10-07）：录制时长紧跟在左边的录制红点后面，不再另弹录制条
        if (m_recording && !m_recordDuration.empty())
            tip = MakeShareRecordingText(m_recordDuration, false, true) + L" · " + tip;
        //有人在控制本人屏幕（第 18 期）：写在最前面，比时长、密码更要紧
        if (!m_controllerName.empty())
            tip = MakeShareControlledByText(m_controllerName) + L" · " + tip;
        m_tipLabel->SetText(tip.c_str());
    }
    //状态点：共享中为绿色，暂停时为灰色
    if (m_statusDot != NULL)
        m_statusDot->SetTextColor(m_paused ? kPausedDotColor : kLiveDotColor);
    //标题栏：展开时写「屏幕共享 · 共享标题」；收起成小把手时写「▼ 屏幕共享」（基类按收起状态选用）
    SetTopBarTitles(MakeShareBarCaption(m_shareTitle), TR(L"屏幕共享"));
    if (m_viewersButton != NULL)
    {
        //观看者按钮保留文字，底图与其余按钮同一样式
        Art().Apply(m_viewersButton, ShareBarLookFor(share_bar_viewers, false),
                    ShareBarTooltipFor(share_bar_viewers, false, false), true);
        m_viewersButton->SetText(ShareViewerCountText(m_viewerCount).c_str());
    }
    applyArt(m_annotButton, share_bar_annotate, m_annotating);
    applyArt(m_recordButton, share_bar_record, m_recording);
    applyArt(m_micButton, share_bar_mic, m_micOn);
    applyArt(m_sysAudioButton, share_bar_system_audio, m_sysAudioOn);
    applyArt(m_cameraButton, share_bar_camera, m_cameraOn);
    applyArt(m_captionButton, share_bar_caption, m_captionSelf);
    applyArt(m_pauseButton, share_bar_pause, m_paused);
    applyArt(m_switchButton, share_bar_switch, false);
    applyArt(m_moreButton, share_bar_more, false);
    applyArt(m_stopButton, share_bar_stop_share, false);
    ApplyOwnButtonLooks();
    TickBlink(static_cast<int64_t>(::GetTickCount64()));
}

int CShareControlBar::RecordDotState() const
{
    return m_recordDotState;
}

void CShareControlBar::TickBlink(int64_t nowMs)
{
    //暂停参数固定传 false：共享录制没有暂停（录制控制器对共享录制只有开始与结束，见 RecordController.h 的
    //StartShareRecording），主持人暂停共享时录制仍在进行，红点照常闪烁。独立录制的录制条才有暂停态（红点变灰）
    const int state = ShareRecordDotStateAt(m_recording, false, nowMs);
    if (state == m_recordDotState)
        return;
    m_recordDotState = state;
    if (m_recordDot != NULL)
        m_recordDot->SetTextColor(ShareRecordDotColor(state, kBarBgColor));
}

void CShareControlBar::Show(const RECT& sourceRect, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    EnsureWindow();
    if (!IsWindow())
        return;
    //每次开始共享都从一直展开开始
    ResetCollapse();
    BuildUi();
    MoveToSource(sourceRect);
    ShowWindow(SW_SHOWNA);
    UpdateWindow();
    //正在录制这场共享（重新显示控制条时）：红点继续闪烁
    if (m_recording)
        SetTimer(kBlinkTimerId, kShareRecordBlinkMs / 2, NULL);
}

void CShareControlBar::MoveToSource(const RECT& sourceRect)
{
    m_sourceRect = sourceRect;
    if (!IsWindow())
        return;
    HMONITOR mon = ::MonitorFromRect(&sourceRect, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    RECT monitor;
    if (::GetMonitorInfo(mon, &mi))
    {
        work = mi.rcWork;
        monitor = mi.rcMonitor;
    }
    else
    {
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        monitor = work;
    }
    //摆在工作区顶部居中（按钮按译文加宽后也不超过工作区宽度，放不下时由左侧的提示文字让出宽度），隐藏后的感应区取
    //整块显示器的顶边；按当前收起状态摆放：收起时只挪小把手、不把控制条重新展开（切换内容、进入标注都会走到这里）
    PlaceTopBar(work, monitor);
}

LRESULT CShareControlBar::OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //录制红点闪烁；收起检查的定时器交给基类
    if (wParam == kBlinkTimerId)
    {
        TickBlink(static_cast<int64_t>(::GetTickCount64()));
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void CShareControlBar::Hide()
{
    m_tipLabel = NULL;
    m_pauseButton = NULL;
    m_micButton = NULL;
    m_sysAudioButton = NULL;
    m_cameraButton = NULL;
    m_annotButton = NULL;
    m_recordButton = NULL;
    m_moreButton = NULL;
    m_viewersButton = NULL;
    m_revokeButton = NULL;
    m_p2pBadge = NULL;
    m_statusDot = NULL;
    m_recordDot = NULL;
    m_captionButton = NULL;
    m_switchButton = NULL;
    m_stopButton = NULL;
    m_notifyWnd = NULL;
    //停掉收起检查、回到一直展开（基类）
    StopTopBar();
    if (!IsWindow())
        return;
    KillTimer(kBlinkTimerId);
    //「更多」菜单正在弹出：菜单的嵌套消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd，
    //菜单也不会随之关闭。改为先关菜单、只隐藏窗口，等 TrackPopup 返回后由 OnShowMoreMenu 销毁
    if (ShareBarShouldDeferDestroy(m_popup.IsActive()))
    {
        m_popup.Close();
        m_popup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("screenshare: control bar hidden while its menu is open, destroy deferred.");
        return;
    }
    DestroyWindow();
}

bool CShareControlBar::IsPopupActive() const
{
    return m_popup.IsActive();
}

LRESULT CShareControlBar::OnShowMoreMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁或已结束共享）：不再弹
    if (m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible())
        return 0;
    showMoreMenu();
    //菜单期间控制器隐藏了控制条：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_popup.TakePendingDestroy())
    {
        LOG_INFO("screenshare: control bar destroyed after its menu closed.");
        DestroyWindow();
        return 0;
    }
    //菜单期间字幕可用性变了：现在重建界面并重新摆位（字幕按钮出现或消失，整条宽度随之变化）
    if (m_rebuildAfterMenu && IsWindowVisible())
    {
        LOG_INFO("screenshare: control bar rebuilt after its menu closed (caption availability changed).");
        BuildUi();
        MoveToSource(m_sourceRect);
    }
    return 0;
}

void CShareControlBar::SetViewerCount(int count)
{
    m_viewerCount = count;
    refreshText();
}

void CShareControlBar::SetPaused(bool paused)
{
    m_paused = paused;
    refreshText();
}

void CShareControlBar::SetMicOn(bool on)
{
    m_micOn = on;
    refreshText();
}

void CShareControlBar::SetSystemAudioOn(bool on)
{
    m_sysAudioOn = on;
    refreshText();
}

void CShareControlBar::SetCameraOn(bool on)
{
    m_cameraOn = on;
    refreshText();
}

void CShareControlBar::SetAnnotating(bool on)
{
    m_annotating = on;
    refreshText();
}

void CShareControlBar::SetRecordDuration(const std::wstring& text)
{
    //没在录制时不显示录制时长（控制器在停止之后可能还会交来最后一次的时长）
    const std::wstring shown = m_recording ? text : std::wstring();
    if (shown == m_recordDuration)
        return;
    m_recordDuration = shown;
    refreshText();
}

std::wstring CShareControlBar::TipTextForTest() const
{
    if (m_tipLabel == NULL)
        return std::wstring();
    return std::wstring(m_tipLabel->GetText().GetString());
}

void CShareControlBar::SetRecording(bool on)
{
    const bool changed = (on != m_recording);
    m_recording = on;
    if (!on)
        m_recordDuration.clear();
    refreshText();
    if (!changed || !IsWindow())
        return;
    //录制红点：录制中显示并闪烁。SetVisible 不会重排（开发约定文档 UI 约定第 5 条）：对红点所在的横条按它当前的矩形强制重排
    if (m_recordDot != NULL)
    {
        m_recordDot->SetVisible(on);
        balloonwjui::DuiControl* row = m_recordDot->GetParent();
        if (row != NULL)
            row->ForceLayout(row->GetRect());
    }
    if (on)
        SetTimer(kBlinkTimerId, kShareRecordBlinkMs / 2, NULL);
    else
        KillTimer(kBlinkTimerId);
}

void CShareControlBar::SetRecordAllowed(bool allowed)
{
    m_recordAllowed = allowed;
}

void CShareControlBar::SetMineStopped(bool stopped)
{
    m_mineStopped = stopped;
}

void CShareControlBar::SetOtherScreenCount(int count)
{
    m_otherScreens = count > 0 ? count : 0;
}

void CShareControlBar::SetBoardOpen(bool open)
{
    m_boardOpen = open;
}

void CShareControlBar::SetScreenBoard(bool available, bool active)
{
    m_screenBoardAvailable = available;
    m_screenBoardActive = active;
}

void CShareControlBar::SetCaptionState(bool available, bool selfOn, bool showOn)
{
    const bool availabilityChanged = (available != m_captionAvailable);
    m_captionAvailable = available;
    m_captionSelf = selfOn;
    m_captionShow = showOn;
    //字幕按钮出现或消失（2026-10-07 起控制条上有「为我的发言生成字幕」按钮）：整条宽度跟着变，重建界面并重新摆位。
    //「更多」菜单开着时不能重建（菜单的嵌套消息循环还在本窗口的消息处理里），记下来，菜单关闭后由 OnShowMoreMenu 重建
    if (availabilityChanged && IsWindow() && IsWindowVisible())
    {
        if (m_popup.IsActive())
        {
            m_rebuildAfterMenu = true;
            LOG_INFO("screenshare: caption availability changed to %d while the more menu is open, rebuild deferred.",
                     available ? 1 : 0);
            return;
        }
        BuildUi();
        MoveToSource(m_sourceRect);
        return;
    }
    applyArt(m_captionButton, share_bar_caption, m_captionSelf);
}

void CShareControlBar::SetMouseEffects(const ShareMouseEffectSettings& settings)
{
    m_mouseSettings = settings;
}

void CShareControlBar::showMoreMenu()
{
    if (m_moreButton == NULL || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd))
        return;
    balloonwjui::DuiMenu menu;
    menu.AppendItem(kMenuEditInfo, TR(L"修改标题与描述"));
    menu.AppendItem(kMenuMuteAll, TR(L"全员静音"));
    menu.AppendItem(kMenuRecordPerm, m_recordAllowed ? TR(L"禁止他人录制") : TR(L"允许他人录制"));
    //多人共享（第 22 期）：只停自己的画面（仍是主持人），有别人在共享时可重新打开「共享画面」窗口（2026-10-12 前叫「其他人的屏幕」）
    menu.AppendSeparator();
    menu.AppendItem(kMenuMine, m_mineStopped ? TR(L"恢复我的共享") : TR(L"停止我的共享"));
    if (m_otherScreens > 0)
        menu.AppendItem(kMenuOthers, TR(L"共享画面"));
    //白板（第 23 期）：新建一块共享白板；只共享一个窗口时可以把它所在的整块屏幕当白板
    menu.AppendItem(kMenuBoard, m_boardOpen ? TR(L"关闭我的白板") : TR(L"新建白板"));
    if (m_screenBoardAvailable || m_screenBoardActive)
        menu.AppendItem(kMenuScreenBoard, m_screenBoardActive ? TR(L"退出屏幕白板") : TR(L"屏幕当白板"));
    //关闭别人的白板（2026-10-08）：子菜单列出别人的白板；按打开菜单这一刻的清单处理选择，期间清单变了也不会错位
    const std::vector<ShareBoardCloseItem> closeBoards = m_otherBoards;
    std::unique_ptr<balloonwjui::DuiMenu> closeBoardMenu;
    if (!closeBoards.empty())
    {
        closeBoardMenu.reset(new balloonwjui::DuiMenu());
        for (size_t i = 0; i < closeBoards.size(); ++i)
            closeBoardMenu->AppendItem(static_cast<UINT>(kMenuCloseBoardBase + i), closeBoards[i].m_text.c_str());
        menu.AppendSubMenu(kMenuCloseBoardRoot, TR(L"关闭白板"), closeBoardMenu.get());
    }
    //实时字幕（语音字幕）：字幕能使用时才有这两项
    if (m_captionAvailable)
    {
        menu.AppendSeparator();
        menu.AppendChecked(kMenuCaptionSelf, TR(L"为我的发言生成字幕"), m_captionSelf);
        menu.AppendChecked(kMenuCaptionShow, TR(L"显示字幕"), m_captionShow);
    }
    //分享链接（第 25 期）：复制、发送邀请；房间设了密码时可选免输密码；已有链接时可以重置、停用
    menu.AppendSeparator();
    menu.AppendItem(kMenuLinkCopy, TR(L"复制分享链接"));
    menu.AppendItem(kMenuLinkSend, TR(L"发送邀请到…"));
    if (!m_password.empty())
        menu.AppendChecked(kMenuLinkNoPassword, TR(L"点链接加入免输密码"), m_linkNoPassword);
    if (m_linkActive)
    {
        menu.AppendItem(kMenuLinkReset, TR(L"重置分享链接"));
        menu.AppendItem(kMenuLinkDisable, TR(L"停用分享链接"));
    }
    //云端录制（第 27b 期）：开通了才能开始；在录时可以暂停 / 继续、停止（没开通也能停掉已在录的）
    if (m_cloudRecordAvailable || m_cloudRecording)
    {
        menu.AppendSeparator();
        if (!m_cloudRecording)
        {
            menu.AppendItem(kMenuCloudStart, TR(L"开始云端录制"));
        }
        else
        {
            menu.AppendItem(kMenuCloudPause, m_cloudRecordPaused ? TR(L"继续云端录制") : TR(L"暂停云端录制"));
            menu.AppendItem(kMenuCloudStop, TR(L"停止云端录制"));
        }
    }
    //鼠标效果（第 16 期）：三个开关与「光圈颜色」「轨迹颜色」两个子菜单，子菜单在 TrackPopup 返回之后才释放
    menu.AppendSeparator();
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;
    //「关闭白板 ▸」的子菜单同样交给弹出状态管理，菜单关闭之后才释放
    if (closeBoardMenu)
        subMenus.push_back(std::move(closeBoardMenu));
    std::vector<std::unique_ptr<CImageEx> > icons;   //颜色项的图标，菜单关闭之后才释放
    AppendMouseEffectMenu(menu, BuildMouseEffectMenu(m_mouseSettings, kMenuMouseBase), subMenus, icons);
    //允许截图（2026-10-08）：本机的工具条与控制条能否被截图，只对这一场共享有效
    menu.AppendSeparator();
    menu.AppendChecked(kMenuAllowCapture, TR(L"允许截图"), IsBarCaptureAllowed());
    //菜单弹在「更多」按钮下方
    const RECT& rc = m_moreButton->GetRect();
    POINT pt = { rc.left, rc.bottom };
    ClientToScreen(&pt);
    //记下正在弹出的菜单与子菜单：期间控制条被隐藏或点了条上的按钮时据此关掉它们
    m_popup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_popup.End();
    //菜单期间控制条被隐藏过（结束了共享，可能已经开始了下一场）或通知窗口没了：丢弃这次选择
    if (!ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(), m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    if (chosen == kMenuEditInfo)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdEditInfo, 0);
    else if (chosen == kMenuMuteAll)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdMuteAll, 0);
    else if (chosen == kMenuRecordPerm)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdRecordPermission, m_recordAllowed ? 0 : 1);
    else if (chosen == kMenuMine)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_mineStopped ? kCmdResumeMine : kCmdStopMine, 0);
    else if (chosen == kMenuOthers)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdOtherScreens, 0);
    else if (chosen == kMenuBoard)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdBoard, 0);
    else if (chosen == kMenuScreenBoard)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdScreenBoard, 0);
    else if (chosen == kMenuCaptionSelf)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCaptionSelf, m_captionSelf ? 0 : 1);
    else if (chosen == kMenuCaptionShow)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCaptionShow, m_captionShow ? 0 : 1);
    else if (chosen == kMenuLinkCopy)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdLink, kLinkCopy);
    else if (chosen == kMenuLinkSend)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdLink, kLinkSendInvite);
    else if (chosen == kMenuLinkNoPassword)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdLink, kLinkNoPassword);
    else if (chosen == kMenuLinkReset)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdLink, kLinkReset);
    else if (chosen == kMenuLinkDisable)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdLink, kLinkDisable);
    else if (chosen == kMenuCloudStart)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCloudRecord, cloudrecord_control_start);
    else if (chosen == kMenuCloudPause)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCloudRecord,
                      m_cloudRecordPaused ? cloudrecord_control_resume : cloudrecord_control_pause);
    else if (chosen == kMenuCloudStop)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCloudRecord, cloudrecord_control_stop);
    else if (chosen >= kMenuCloseBoardBase && chosen < kMenuCloseBoardBase + closeBoards.size())
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCloseBoard,
                      static_cast<LPARAM>(closeBoards[chosen - kMenuCloseBoardBase].m_ownerId));
    else if (chosen == kMenuAllowCapture)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdAllowCapture, IsBarCaptureAllowed() ? 0 : 1);
    else if (chosen >= kMenuMouseBase && chosen < kMenuMouseBase + kMenuMouseSpan)
        ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdMouseEffect, static_cast<LPARAM>(chosen));
}

void CShareControlBar::SetLinkState(bool active, bool noPassword)
{
    m_linkActive = active;
    m_linkNoPassword = noPassword;
}

void CShareControlBar::SetCloudRecordState(bool available, bool recording, bool paused)
{
    const bool textChanged = (recording != m_cloudRecording) || (paused != m_cloudRecordPaused);
    m_cloudRecordAvailable = available;
    m_cloudRecording = recording;
    m_cloudRecordPaused = recording && paused;
    if (textChanged)
        refreshText();
}

void CShareControlBar::SetCloudRecordAvailable(bool available)
{
    m_cloudRecordAvailable = available;
}

void CShareControlBar::SetOtherBoards(const std::vector<ShareBoardCloseItem>& items)
{
    m_otherBoards = items;
}

void CShareControlBar::SetPassword(const std::wstring& password)
{
    if (password == m_password)
        return;
    m_password = password;
    refreshText();
}

void CShareControlBar::SetP2PLinkPath(int linkPath)
{
    m_linkPath = linkPath;
    balloonmain::ApplyP2PBadgeVisibility(m_p2pBadge, linkPath);
}

void CShareControlBar::SetDurationText(const std::wstring& text)
{
    if (text == m_duration)
        return;
    m_duration = text;
    refreshText();
}

void CShareControlBar::SetForced(bool forced)
{
    if (forced == m_forced)
        return;
    m_forced = forced;
    refreshText();
}

void CShareControlBar::SetShareTitle(const std::wstring& title)
{
    if (title == m_shareTitle)
        return;
    m_shareTitle = title;
    refreshText();
}

bool CShareControlBar::GetViewersButtonScreenRect(RECT& out) const
{
    if (!IsWindow() || !IsWindowVisible() || m_viewersButton == NULL)
        return false;
    out = m_viewersButton->GetRect();
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

void CShareControlBar::SetSpeakingText(const std::wstring& text)
{
    if (text == m_speaking)
        return;
    m_speaking = text;
    refreshText();
}

void CShareControlBar::SetControlledBy(const std::wstring& controllerName)
{
    if (controllerName == m_controllerName)
        return;
    const bool wasShown = !m_controllerName.empty();
    m_controllerName = controllerName;
    refreshText();
    const bool show = !m_controllerName.empty();
    if (m_revokeButton != NULL && show != wasShown)
    {
        m_revokeButton->SetVisible(show);
        //SetVisible 不会重排（开发约定文档 UI 约定第 5 条）：对按钮所在的横条按它当前的矩形强制重排
        balloonwjui::DuiControl* row = m_revokeButton->GetParent();
        if (row != NULL)
            row->ForceLayout(row->GetRect());
    }
}

LRESULT CShareControlBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //「更多」菜单开着时点了条上的按钮：控制条不抢焦点，点它不会让菜单失去焦点，这里先关掉菜单；点的就是「更多」时
    //只关菜单，不再重新弹出
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && m_popup.IsActive())
    {
        m_popup.Close();
        if (n->ctrlId == static_cast<UINT>(kIdMore))
            return 0;
    }
    //「收起 / 固定」与「隐藏」（2026-10-07）：本条自己处理（基类），不发给控制器
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && HandleTopBarClick(n->ctrlId))
        return 0;
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && m_notifyWnd != NULL &&
        ::IsWindow(m_notifyWnd))
    {
        //「N 人观看 ▾」：打开或关闭观看者名单（第 13 期）
        if (n->ctrlId == static_cast<UINT>(kIdViewers))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdViewers, 0);
            return 0;
        }
        //收回控制（第 18 期）
        if (n->ctrlId == static_cast<UINT>(kIdRevokeControl))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdRevokeControl, 0);
            return 0;
        }
        //结束共享
        if (n->ctrlId == static_cast<UINT>(kIdStop))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdStop, 0);
            return 0;
        }
        //暂停 / 继续
        if (n->ctrlId == static_cast<UINT>(kIdPause))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_paused ? kCmdResume : kCmdPause, 0);
            return 0;
        }
        //打开 / 关闭麦克风
        if (n->ctrlId == static_cast<UINT>(kIdMic))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_micOn ? kCmdMicOff : kCmdMicOn, 0);
            return 0;
        }
        //录制 / 停止录制
        if (n->ctrlId == static_cast<UINT>(kIdRecord))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_recording ? kCmdRecordStop : kCmdRecordStart, 0);
            return 0;
        }
        //更多：投给自己，等这次鼠标事件处理完再弹菜单
        if (n->ctrlId == static_cast<UINT>(kIdMore))
        {
            PostMessage(kMsgShowMoreMenu, 0, 0);
            return 0;
        }
        //标注 / 退出标注
        if (n->ctrlId == static_cast<UINT>(kIdAnnotate))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdAnnotate, 0);
            return 0;
        }
        //打开 / 关闭摄像头
        if (n->ctrlId == static_cast<UINT>(kIdCamera))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_cameraOn ? kCmdCameraOff : kCmdCameraOn, 0);
            return 0;
        }
        //共享 / 停止电脑声音
        if (n->ctrlId == static_cast<UINT>(kIdSysAudio))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, m_sysAudioOn ? kCmdSysAudioOff : kCmdSysAudioOn, 0);
            return 0;
        }
        //切换共享内容
        if (n->ctrlId == static_cast<UINT>(kIdSwitch))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdSwitch, 0);
            return 0;
        }
        //为我的发言生成字幕（2026-10-07 起控制条上的按钮）：请求切换到相反的状态，与「更多」菜单里的勾选项同一个命令
        if (n->ctrlId == static_cast<UINT>(kIdCaption))
        {
            ::PostMessage(m_notifyWnd, kMsgShareBarCommand, kCmdCaptionSelf, m_captionSelf ? 0 : 1);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
