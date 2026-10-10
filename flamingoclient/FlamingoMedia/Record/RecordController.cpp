/**
 *  屏幕录制控制器，职责见 RecordController.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "RecordController.h"

#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>

#include "RecordPolicy.h"
#include "../Share/ShareSources.h"   // SetBarCaptureAllowed、ApplyBarCaptureState：「允许截图」（2026-10-08）
#include "RecordManagerModel.h"   // MakeRecordingInfo、RecordFileTimeToUnixMs：录制完成时写录像记录
#include "CloudRecordCenter.h"    // 保存到云端：录好后上传（第 27 期）
#include "../Common/OsVersionPolicy.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

#pragma comment(lib, "shell32.lib")

namespace screenrecord {

namespace {

//录制条与边框的刷新定时器：每 500 毫秒一次；id 取较大值避开基类可能占用的小 id
const UINT_PTR kTickTimerId = 601;
const UINT kTickMs = 500;
//只录声音时录制条电平条的刷新定时器（第 24 期）：每 100 毫秒一次
const UINT_PTR kLevelTimerId = 602;
const UINT kLevelMs = 100;
//同一秒里重名时最多尝试的序号
const int kMaxNameSeq = 99;
//消息框标题
const wchar_t* const kCaption = TR_NOOP(L"屏幕录制");

std::wstring defaultFolder()
{
    PWSTR videos = NULL;
    std::wstring dir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Videos, 0, NULL, &videos)) && videos != NULL)
        dir = videos;
    if (videos != NULL)
        ::CoTaskMemFree(videos);
    //「视频」文件夹下的 FlamingoRecords；取不到「视频」文件夹时为空
    return MakeDefaultRecordFolder(dir);
}

bool fileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

uint64_t fileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

} // namespace

CRecordController::CRecordController()
    : m_host(NULL)
    , m_quality(record_quality_standard)
    , m_ctype(0)
    , m_cid(0)
    , m_wantMic(false)
    , m_wantSystem(false)
    , m_micOn(false)
    , m_systemOn(false)
    , m_cameraOn(false)
    , m_wantCamera(false)
    , m_wantCaption(false)
    , m_wantCloud(false)
    , m_cloud(NULL)
    , m_captionActive(false)
    , m_lastHasCaption(false)
    , m_shareCaptionTried(false)
    , m_captionOn(true)
    , m_picking(false)
    , m_recording(false)
    , m_audioOnly(false)
    , m_shareMode(false)
    , m_shareOwner(NULL)
    , m_penOverlay(&m_pen.Session())
    , m_penMode(false)
    , m_recordingBoard(false)
{
    memset(&m_recordRegion, 0, sizeof(m_recordRegion));
    memset(&m_penRect, 0, sizeof(m_penRect));
}

CRecordController::~CRecordController()
{
    Shutdown();
}

bool CRecordController::Init(flamingomedia::IMediaHost* host)
{
    if (host == NULL)
        return false;
    m_host = host;
    if (!IsWindow() && Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("record: create controller message window failed.");
        return false;
    }
    //语音字幕：字幕变为不可用时停止进行中的识别
    flamingomedia::CMediaCaptionBridge::Instance().AddObserver(this);
    //录屏时屏幕上的实时字幕（14.12）
    m_captionLive.Init();
    LOG_INFO("RecordController: initialized.");
    return true;
}

void CRecordController::Shutdown()
{
    flamingomedia::CMediaCaptionBridge::Instance().RemoveObserver(this);
    if (m_recording)
    {
        m_session.Stop();
        //录像已保存，已识别的字幕照常收尾
        endCaption(true, m_session.RecordedMs());
        cleanupSources();
        m_recording = false;
        m_shareMode = false;
        m_shareOwner = NULL;
    }
    m_picker.CloseDialog();
    m_region.Close();
    m_done.Hide();
    m_manager.CloseWindow();
    m_picking = false;
    m_captionLive.Shutdown();
    //轻量提示（2026-10-07）：窗口过程在本 DLL 里，DLL 卸载之前必须销毁
    m_noticeToast.Destroy();
    if (IsWindow())
        DestroyWindow();
    m_host = NULL;
}

void CRecordController::ShowManager()
{
    if (m_host == NULL)
        return;
    m_manager.Open(mainWindow(), m_host);
}

void CRecordController::SetCloudCenter(CCloudRecordCenter* center)
{
    m_cloud = center;
    //录制管理窗口的「云端」页同样经它取数据与操作（第 27 期 27d）
    m_manager.SetCloudCenter(center);
}

void CRecordController::SetSettings(int quality, const std::wstring& folder)
{
    m_quality = (quality == record_quality_high) ? record_quality_high : record_quality_standard;
    m_folder = folder;
    LOG_INFO("record: settings updated, quality=%d.", m_quality);
}

bool CRecordController::StartRecording(int ctype, int64_t cid, const std::wstring& conversationName)
{
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    if (!flamingomedia::GetOsVersion(major, minor, build)
        || !flamingomedia::IsScreenCaptureSupportedBuild(major, minor, build))
    {
        showError(TR(L"当前系统版本不支持屏幕录制，需要 Windows 10 2004 或更高版本。"));
        return false;
    }
    if (m_recording)
    {
        showError(TR(L"正在录制中，请先停止当前的录制。"));
        return false;
    }
    if (m_picking)
    {
        //选择窗已经开着：恢复（最小化了的）并提到前面，不另开一个（2026-10-05 用户要求）
        m_picker.BringToFront();
        return true;
    }
    m_ctype = ctype;
    m_cid = cid;
    m_convName = conversationName;
    m_picking = true;
    //语音字幕（docs/语音字幕方案.md 6.1）：可用时在选择窗里提供「生成字幕」，默认值沿用上次的选择（D3）；
    //默认勾着时提前在后台加载模型，开始录制时多半已经加载好（5.3）
    bool offerCaption = false;
    bool captionOn = false;
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule != NULL)
    {
        offerCaption = true;
        captionOn = captionModule->GetPreference(caption::caption_pref_record_caption);
        if (captionOn)
            captionModule->Preload();
    }
    m_picker.SetCaptionOptions(offerCaption, captionOn);
    //保存到云端（第 27 期）：企业开关打开、且云端录像中心可用时才能勾
    m_picker.SetCloudSaveOption(m_cloud != NULL && m_host != NULL && m_host->IsCloudRecordEnabled());
    m_picker.Open(mainWindow(), m_hWnd, screenshare::share_picker_record, std::wstring());
    LOG_INFO("record: start requested, ctype=%d, cid=%lld.", ctype, static_cast<long long>(cid));
    return true;
}

bool CRecordController::StartShareRecording(IRecordVideoSource* video, IRecordVideoSource* pip, int srcW, int srcH,
                                            const RECT& anchor, int ctype, int64_t cid,
                                            const std::wstring& conversationName, IShareRecordOwner* owner)
{
    if (m_recording || m_picking)
    {
        showError(TR(L"正在进行另一个录制，请先停止。"));
        return false;
    }
    const std::wstring path = makeOutputPath();
    if (path.empty())
    {
        showError(TR(L"无法创建录制文件，请检查保存目录。"));
        return false;
    }
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = srcW;
    opt.m_sourceHeight = srcH;
    opt.m_quality = m_quality;
    m_session.Audio().SetEnabled(record_audio_mic, true);
    m_session.Audio().SetEnabled(record_audio_others, true);
    m_session.Audio().SetEnabled(record_audio_system, true);
    //共享录制取的是推流线程或观看窗已经画好鼠标效果的画面，不再装饰
    m_session.SetDecorator(NULL);
    if (!m_session.Start(opt, video, pip, this))
    {
        showError(m_session.FailReason() == record_fail_disk_full ? TR(L"磁盘剩余空间不足，无法开始录制。")
                                                                   : TR(L"无法开始录制。"));
        return false;
    }
    m_session.SetPipEnabled(false);
    m_ctype = ctype;
    m_cid = cid;
    m_convName = conversationName;
    m_recording = true;
    m_shareMode = true;
    m_shareOwner = owner;
    //语音字幕：草稿会话等收到第一句实时字幕时再建（ShareCaptionRecording）
    m_sharePath = path;
    m_shareCaptionTried = false;
    m_captionActive = false;
    //共享中的录制不再弹出单独的录制条（2026-10-07，docs/屏幕共享与录制方案.md 第 14.2 节）：红点、时长与「停止录制」
    //都在主工具条上（主持人的共享控制条、观看者的观看窗），时长由共享控制器每秒经 RecordedMs 取去显示
    (void)anchor;
    SetTimer(kTickTimerId, kTickMs, NULL);
    LOG_INFO("record: share recording started, %dx%d, shown on the share toolbar.", srcW, srcH);
    return true;
}

int64_t CRecordController::RecordedMs() const
{
    return m_recording ? m_session.RecordedMs() : 0;
}

void CRecordController::StopRecording()
{
    if (!m_recording)
        return;
    //入口与出口各记一行：卡死时能分清是卡在等写入线程退出，还是根本没有走到这里（bugs.md BUG-120）
    LOG_INFO("record: StopRecording begin, shareMode=%d, captionActive=%d.", m_shareMode ? 1 : 0,
             m_captionActive ? 1 : 0);
    //写入线程结束前已投递 kMsgRecordStopped，收尾在那里做
    m_session.Stop();
    LOG_INFO("record: StopRecording end.");
}

bool CRecordController::IsRecording() const
{
    return m_recording;
}

caption::ICaptionRecording* CRecordController::ShareCaptionRecording(int64_t& recordedMs)
{
    recordedMs = 0;
    if (!m_recording || !m_shareMode)
        return NULL;
    if (m_captionTap.Current() == NULL && !m_shareCaptionTried)
    {
        m_shareCaptionTried = true;
        beginShareCaption(m_sharePath);
    }
    caption::ICaptionRecording* recording = m_captionTap.Current();
    if (recording != NULL)
        recordedMs = m_session.RecordedMs();
    return recording;
}

bool CRecordController::IsShareRecording() const
{
    return m_recording && m_shareMode;
}

bool CRecordController::IsStandaloneBusy() const
{
    //m_picking 从打开选择窗一直到框选结束（或取消）都为 true
    return (m_recording && !m_shareMode) || m_picking;
}

void CRecordController::ShowStartRejected(const std::wstring& text)
{
    m_noticeToast.ShowNearActiveWindow(text, flamingomedia::media_toast_error);
}

RecordAudioMixer* CRecordController::ShareAudio()
{
    return (m_recording && m_shareMode) ? &m_session.Audio() : NULL;
}

void CRecordController::SetShareAnnotations(const std::vector<screenshare::AnnotationStroke>& strokes)
{
    if (m_recording && m_shareMode)
        m_session.SetAnnotations(strokes);
}

void CRecordController::SetSharePipEnabled(bool on)
{
    if (m_recording && m_shareMode)
        m_session.SetPipEnabled(on);
}

//==================== 选择与开始 ====================

LRESULT CRecordController::OnPickerResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    std::unique_ptr<screenshare::SharePickerResult> result(reinterpret_cast<screenshare::SharePickerResult*>(lParam));
    if (wParam == 0 || !result)
    {
        m_picking = false;
        return 0;
    }
    m_wantMic = result->m_mic;
    m_wantSystem = result->m_systemAudio;
    m_wantCamera = result->m_camera;
    //语音字幕：记下这次的勾选，并按账号记住，下次打开选择窗沿用（D3）
    m_wantCaption = result->m_captionOffered && result->m_caption;
    m_wantCloud = result->m_saveToCloud;
    if (result->m_captionOffered)
    {
        caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
        if (captionModule != NULL)
            captionModule->SetPreference(caption::caption_pref_record_caption, result->m_caption);
    }
    //白板（第 21 期）：打开一块白板并录下它
    if (result->m_whiteboard)
    {
        m_picking = false;
        startWhiteboard();
        return 0;
    }
    //只录声音（第 24 期）：不选画面，按勾选的麦克风与电脑声音开始
    if (result->m_audioOnly)
    {
        m_picking = false;
        startAudioOnly();
        return 0;
    }
    if (result->m_region)
    {
        //接着在那块显示器上框选；框选的结果要配上这块显示器
        m_pendingMonitor = result->m_source;
        if (!m_region.Open(result->m_source.m_monitorRect, m_hWnd))
            m_picking = false;
        return 0;
    }
    m_picking = false;
    const RECT none = { 0, 0, 0, 0 };
    startStandalone(result->m_source, none);
    return 0;
}

LRESULT CRecordController::OnRegionResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    std::unique_ptr<RECT> rc(reinterpret_cast<RECT*>(lParam));
    m_picking = false;
    if (wParam == 0 || !rc)
        return 0;
    startStandalone(m_pendingMonitor, *rc);
    return 0;
}

void CRecordController::startStandalone(const screenshare::ShareSourceSpec& spec, const RECT& region)
{
    const std::wstring path = makeOutputPath();
    if (path.empty())
    {
        showError(TR(L"无法创建录制文件，请检查保存目录。"));
        return;
    }
    if (!m_capture.Start(spec, region))
    {
        showError(TR(L"无法采集所选的内容。"));
        return;
    }
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = m_capture.Width();
    opt.m_sourceHeight = m_capture.Height();
    opt.m_quality = m_quality;
    m_session.Audio().SetEnabled(record_audio_mic, m_wantMic);
    m_session.Audio().SetEnabled(record_audio_others, false);
    m_session.Audio().SetEnabled(record_audio_system, m_wantSystem);
    bool cameraOk = false;
    if (m_wantCamera)
    {
        cameraOk = m_camera.Start();
        if (!cameraOk)
            showError(TR(L"无法打开摄像头，这次录制不带摄像头画中画。"));
    }
    //鼠标效果（第 16 期）：按录制区域换算光标，录制线程写每一帧前画上
    m_mouseDecorator.Reset();
    m_mouseDecorator.SetGeometry(&m_capture);
    m_session.SetDecorator(&m_mouseDecorator);
    //摄像头来源总是交给会话：录制中途打开摄像头时不必重启录制，画不画由 SetPipEnabled 决定（2026-10-05）
    m_session.SetPipPlacement(RecordPipPlacement());
    //语音字幕：要在写入线程开始取声音之前接上旁路，录像开头的话才不会漏掉
    beginCaption(path);
    if (!m_session.Start(opt, &m_capture, &m_camera, this))
    {
        endCaption(false, 0);
        m_capture.Stop();
        m_camera.Stop();
        showError(m_session.FailReason() == record_fail_disk_full ? TR(L"磁盘剩余空间不足，无法开始录制。")
                                                                   : TR(L"无法开始录制。"));
        return;
    }
    m_session.SetPipEnabled(cameraOk);
    m_cameraOn = cameraOk;
    m_micOn = m_wantMic && m_mic.Start(&m_session.Audio());
    if (m_wantMic && !m_micOn)
    {
        LOG_WARNING("record: microphone unavailable, recording without it.");
        m_session.Audio().SetEnabled(record_audio_mic, false);
    }
    m_systemOn = m_wantSystem && m_system.Start(&m_session.Audio());
    if (m_wantSystem && !m_systemOn)
    {
        LOG_WARNING("record: system audio unavailable, recording without it.");
        m_session.Audio().SetEnabled(record_audio_system, false);
    }
    m_recording = true;
    m_shareMode = false;
    m_shareOwner = NULL;
    //录制条摆在被录内容所在的显示器上；自选区域另画边框
    RECT anchor = spec.m_monitorRect;
    if (spec.m_kind == screenshare::share_source_window)
        anchor = screenshare::GetShareWindowBounds(spec.m_hwnd);
    const bool hasRegion = (region.right > region.left && region.bottom > region.top);
    if (hasRegion)
    {
        anchor = region;
        m_border.Show(region, false);
    }
    //画笔（第 21 期）：记下录的内容，进入画笔模式时据此摆放显示层
    m_recordSpec = spec;
    m_recordRegion = region;
    m_penMode = false;
    //录白板时不需要画笔（白板自带工具）
    m_bar.SetPenAvailable(!m_recordingBoard);
    //录制中可随时开关麦克风、电脑声音与摄像头（2026-10-05）
    m_bar.SetAudioToggles(true, m_micOn, m_systemOn);
    m_bar.SetCameraToggle(true, m_cameraOn);
    //字幕开关（14.12）：只在这次录制建起了字幕草稿会话时出现，开始时为打开
    m_bar.SetCaptionToggle(m_captionActive, m_captionOn);
    m_bar.SetPenActive(false);
    m_bar.Show(anchor, m_hWnd);
    m_bar.SetState(0, false);
    //屏幕上的实时字幕：按声音实际打开的情况标注说话人，立即知道录制区域
    if (m_captionLive.IsActive())
    {
        syncCaptionLiveSources();
        m_captionLive.Tick(recordedRect(), recordedAreaVisible());
    }
    //摄像头开着：被录内容右下角出现可拖动的小窗，录像里的画中画跟着它走（2026-10-05）
    if (m_cameraOn)
        showCameraWindow();
    syncCaptionPip();
    SetTimer(kTickTimerId, kTickMs, NULL);
    LOG_INFO("record: standalone recording started, kind=%d, region=%d, mic=%d, system=%d, camera=%d, caption=%d.",
             spec.m_kind, hasRegion ? 1 : 0, m_wantMic ? 1 : 0, m_wantSystem ? 1 : 0, cameraOk ? 1 : 0,
             m_captionActive ? 1 : 0);
}

//==================== 录制中 ====================

void CRecordController::SetMouseEffects(const screenshare::ShareMouseEffectSettings& settings)
{
    m_mouseDecorator.SetSettings(settings);
    m_bar.SetMouseEffects(settings);
}

LRESULT CRecordController::OnBarCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (!m_recording)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //暂停
    case CRecordBar::kCmdPause:
        m_session.SetPaused(true);
        m_bar.SetState(m_session.RecordedMs(), true);
        LOG_INFO("record: paused.");
        break;
    //继续
    case CRecordBar::kCmdResume:
        m_session.SetPaused(false);
        m_bar.SetState(m_session.RecordedMs(), false);
        LOG_INFO("record: resumed.");
        break;
    //停止
    case CRecordBar::kCmdStop:
        StopRecording();
        break;
    //允许截图（2026-10-08）：录制条与画笔工具条按开关重新设置能否被采集，只对这一次录制有效（结束时在 cleanupSources 里关掉）
    case CRecordBar::kCmdAllowCapture:
        screenshare::SetBarCaptureAllowed(lParam != 0);
        if (m_bar.IsWindow())
            screenshare::ApplyBarCaptureState(m_bar.m_hWnd);
        m_pen.ReapplyBarCapture();
        break;
    //鼠标效果的开关或颜色（第 16 期）：改设置、立即生效，并交给 exe 按账号保存（exe 保存后会把同一份设置推回来）
    case CRecordBar::kCmdMouseEffect:
    {
        screenshare::ShareMouseEffectSettings settings = m_mouseDecorator.Settings();
        if (screenshare::ApplyMouseEffectMenuCommand(settings, static_cast<UINT>(lParam), CRecordBar::kMenuMouseBase))
        {
            SetMouseEffects(settings);
            if (m_host != NULL)
                m_host->SaveMouseEffectSettings(screenshare::SerializeMouseEffectSettings(settings).c_str());
        }
        break;
    }
    //摄像头开关（2026-10-05）：lParam 为想要的新状态
    case CRecordBar::kCmdCamera:
        setStandaloneCamera(lParam != 0);
        break;
    //麦克风开关（2026-10-05）：lParam 为想要的新状态
    case CRecordBar::kCmdMic:
        setStandaloneAudio(record_audio_mic, lParam != 0);
        break;
    //电脑声音开关（2026-10-05）
    case CRecordBar::kCmdSystemAudio:
        setStandaloneAudio(record_audio_system, lParam != 0);
        break;
    //字幕开关（14.12）：lParam 为想要的新状态
    case CRecordBar::kCmdCaption:
        setCaptionOn(lParam != 0);
        break;
    //画笔（第 21 期）：进入或退出画笔模式
    case CRecordBar::kCmdPen:
        if (!m_shareMode)
            setPenMode(!m_penMode);
        break;
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

void CRecordController::startWhiteboard()
{
    if (!m_board.Open(mainWindow()))
    {
        showError(TR(L"无法打开白板。"));
        return;
    }
    screenshare::ShareSourceSpec spec;
    spec.m_kind = screenshare::share_source_window;
    spec.m_hwnd = m_board.m_hWnd;
    m_recordingBoard = true;
    const RECT none = { 0, 0, 0, 0 };
    startStandalone(spec, none);
    //没能开始录制：关掉白板
    if (!m_recording)
    {
        m_recordingBoard = false;
        m_board.Close();
        return;
    }
    LOG_INFO("record: whiteboard recording started.");
}

RECT CRecordController::recordedRect() const
{
    if (m_recordRegion.right > m_recordRegion.left && m_recordRegion.bottom > m_recordRegion.top)
        return m_recordRegion;
    if (m_recordSpec.m_kind == screenshare::share_source_window)
        return screenshare::GetShareWindowBounds(m_recordSpec.m_hwnd);
    return m_recordSpec.m_monitorRect;
}

void CRecordController::setPenMode(bool on)
{
    //只录声音（第 24 期）时没有画面，不提供画笔
    if (!m_recording || m_shareMode || m_audioOnly || on == m_penMode)
        return;
    if (on && !m_pen.IsRunning() && !m_pen.Start(this, 1.0))
        return;
    m_penMode = on;
    if (on)
    {
        //显示层盖在被录的内容上（排除在采集之外，笔迹由录制合成画进视频），输入层接住鼠标
        m_penRect = recordedRect();
        m_penOverlay.Place(m_penRect, true);
        m_penOverlay.SetInputMode(true, &m_pen);
        m_pen.ShowBar(m_penRect);
        //输入层在置顶窗口的最上面：把录制条与摄像头小窗提上来，免得点不到、拖不动
        m_bar.BringToTop();
        if (m_cameraWindow.IsShown())
            ::SetWindowPos(m_cameraWindow.m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        m_session.SetAnnotations(m_pen.Session().Model().Strokes());
    }
    else
    {
        //放开鼠标、隐藏工具条；笔迹留在显示层上，照常录进视频
        m_penOverlay.SetInputMode(false, NULL);
        m_pen.HideBar();
    }
    m_bar.SetPenActive(on);
    LOG_INFO("record: pen mode %s, strokes=%u.", on ? "on" : "off",
             static_cast<unsigned>(m_pen.Session().Model().Strokes().size()));
}

void CRecordController::OnLocalAnnotationsChanged()
{
    m_penOverlay.Redraw();
    if (m_recording && !m_shareMode)
        m_session.SetAnnotations(m_pen.Session().Model().Strokes());
}

void CRecordController::OnLocalAnnotatorExit()
{
    setPenMode(false);
}

void CRecordController::OnLocalAnnotatorPointerMode(bool on)
{
    if (!m_penMode)
        return;
    //「鼠标」：撤掉输入层，点击穿透到被录的程序；笔迹与工具条保留。回到画图时重新盖上输入层
    m_penOverlay.SetInputMode(!on, on ? NULL : &m_pen);
    if (!on)
    {
        //输入层到了置顶窗口的最上面：把画笔工具条、录制条与摄像头小窗提上来
        m_pen.BringBarToTop();
        m_bar.BringToTop();
        if (m_cameraWindow.IsShown())
            ::SetWindowPos(m_cameraWindow.m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    LOG_INFO("record: pen pointer mode %s.", on ? "on" : "off");
}

LRESULT CRecordController::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //只录声音的电平条（第 24 期）
    if (wParam == kLevelTimerId)
    {
        if (!m_recording || !m_audioOnly)
            KillTimer(kLevelTimerId);
        else
            updateLevels();
        return 0;
    }
    if (wParam != kTickTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (!m_recording)
    {
        KillTimer(kTickTimerId);
        return 0;
    }
    m_bar.SetState(m_session.RecordedMs(), m_session.IsPaused());
    //屏幕上的实时字幕（14.12）：清掉过期的字幕，跟随被录的窗口
    if (!m_shareMode && m_captionLive.IsActive())
        m_captionLive.Tick(recordedRect(), recordedAreaVisible());
    //被录的窗口可能移动了：画中画按摄像头小窗相对它的新位置重算（2026-10-05）
    if (!m_shareMode && m_cameraOn)
        updatePipPlacement();
    //画笔（第 21 期）：录的是窗口时显示层与工具条跟着窗口走，窗口最小化时隐藏显示层
    if (!m_shareMode && m_pen.IsRunning() && m_recordSpec.m_kind == screenshare::share_source_window)
    {
        const RECT rc = recordedRect();
        const bool hidden = !::IsWindow(m_recordSpec.m_hwnd) || ::IsIconic(m_recordSpec.m_hwnd);
        if (hidden || rc.left != m_penRect.left || rc.top != m_penRect.top || rc.right != m_penRect.right ||
            rc.bottom != m_penRect.bottom)
        {
            m_penRect = rc;
            m_penOverlay.Place(rc, !hidden);
            m_pen.MoveBar(rc);
        }
    }
    //被录的窗口关闭了（或采集器失效）：停止并保存。只录声音（第 24 期）时没有画面采集
    if (!m_shareMode && !m_audioOnly && m_capture.IsFailed())
    {
        LOG_WARNING("record: capture source lost, stopping.");
        StopRecording();
    }
    return 0;
}

void CRecordController::OnRecordStopped(int reason, const std::wstring& path, int64_t durationMs)
{
    //写入线程上：转投 UI 线程
    if (!IsWindow())
        return;
    StoppedInfo* info = new StoppedInfo();
    info->m_path = path;
    info->m_durationMs = durationMs;
    if (!PostMessage(kMsgRecordStopped, static_cast<WPARAM>(reason), reinterpret_cast<LPARAM>(info)))
        delete info;
}

LRESULT CRecordController::OnStoppedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    std::unique_ptr<StoppedInfo> info(reinterpret_cast<StoppedInfo*>(lParam));
    //写入线程已经结束（或马上结束）：等它退出后再收尾
    m_session.Stop();
    const bool captureLost = (!m_shareMode && !m_audioOnly && m_capture.IsFailed());
    //保存到云端只对独立录制：先记下这次是否共享录制、是否只录声音，下面会清除
    const bool wasShare = m_shareMode;
    const bool wasAudioOnly = m_audioOnly;
    const bool wantCloud = m_wantCloud && !wasShare;
    m_wantCloud = false;
    IShareRecordOwner* owner = m_shareOwner;
    cleanupSources();
    m_recording = false;
    m_shareMode = false;
    m_shareOwner = NULL;
    if (owner != NULL)
        owner->OnShareRecordingEnded();
    //语音字幕：这次录制建起过草稿会话（字幕中途变为不可用时它已提前收尾，这里只是把标志带到完成提示）
    const bool hadCaption = m_captionActive;
    m_captionActive = false;
    m_lastHasCaption = false;
    m_sharePath.clear();
    m_shareCaptionTried = false;
    if (!info)
    {
        endCaption(false, 0);
        return 0;
    }
    m_lastPath = info->m_path;
    const uint64_t bytes = fileSize(info->m_path);
    const int reason = static_cast<int>(wParam);
    if (bytes == 0)
    {
        endCaption(false, 0);
        showError(TR(L"录制失败，没有生成文件。"));
        return 0;
    }
    //录像已生成：让字幕草稿把还没定稿的话识别完、保存（异步，完成后草稿会话自行释放）
    endCaption(true, info->m_durationMs);
    m_lastHasCaption = hadCaption;
    addRecordingRecord(info->m_path, info->m_durationMs, bytes);
    std::wstring title = TR(L"录制完成");
    if (reason == record_stop_disk_full)
        title = TR(L"磁盘剩余空间不足，录制已自动停止");
    else if (reason == record_stop_error)
        title = TR(L"录制出错，已保存录到的部分");
    else if (captureLost)
        title = TR(L"录制的窗口已关闭，录制已停止");
    //保存到云端（第 27 期）：本机文件照常保留，后台上传；上传完成后云端录像中心提示「已可观看」
    if (wantCloud)
    {
        FILETIME now;
        ::GetSystemTimeAsFileTime(&now);
        const int64_t startTime = RecordFileTimeToUnixMs(now) - info->m_durationMs;
        const bool uploading = (m_cloud != NULL) &&
                               m_cloud->UploadLocalRecording(info->m_path, wasAudioOnly, info->m_durationMs,
                                                             startTime, std::wstring());
        title = uploading ? TRF(L"{0}，正在保存到云端", title) : TRF(L"{0}，没能保存到云端", title);
        LOG_INFO("record: save to cloud requested, uploading=%d.", uploading ? 1 : 0);
    }
    //会话名可能很长，放在标题行（超长时省略），按钮上只写「发送」
    std::wstring sendText = TR(L"发送到会话…");
    if (m_ctype != 0)
    {
        sendText = TR(L"发送");
        if (!m_convName.empty())
            title = TRF(L"{0}，可发送给 {1}", title, m_convName);
    }
    //第二行写文件名、第三行写时长与大小（第 14 期）
    //有字幕草稿、且字幕功能仍可用时，完成提示里多一个「编辑字幕」（docs/语音字幕方案.md 第 2 节第 9 条）
    const bool showCaption = m_lastHasCaption && flamingomedia::CMediaCaptionBridge::Instance().IsAvailable();
    m_done.Show(title, RecordFileNameOf(info->m_path), FormatRecordDoneDetail(info->m_durationMs, bytes), sendText,
                m_hWnd, showCaption);
    LOG_INFO("record: done, reason=%d, %llu bytes.", reason, static_cast<unsigned long long>(bytes));
    return 0;
}

LRESULT CRecordController::OnDoneCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //打开我的录制（2026-10-06）：与这一次的录像文件无关，文件已被移走或删除时照样打开录制管理窗口
    if (static_cast<int>(wParam) == CRecordDoneToast::kCmdOpenManager)
    {
        LOG_INFO("record: done toast asks to open the recording manager.");
        ShowManager();
        return 0;
    }
    if (m_lastPath.empty() || !fileExists(m_lastPath))
        return 0;
    switch (static_cast<int>(wParam))
    {
    //用系统默认的播放器打开
    case CRecordDoneToast::kCmdPlay:
        ::ShellExecuteW(NULL, L"open", m_lastPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        break;
    //打开所在文件夹并选中文件
    case CRecordDoneToast::kCmdOpenFolder:
        {
            const std::wstring args = L"/select,\"" + m_lastPath + L"\"";
            ::ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
        }
        break;
    //编辑字幕：打开这次录像的字幕编辑器（字幕模块里的窗口）
    case CRecordDoneToast::kCmdEditCaption:
        {
            caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
            const bool opened = captionModule != NULL && captionModule->OpenEditor(mainWindow(), m_lastPath.c_str());
            LOG_INFO("record: done toast asks to edit captions, opened=%d.", opened ? 1 : 0);
            if (!opened)
                showError(TR(L"无法打开字幕编辑器。"));
        }
        break;
    //发送：从聊天窗发起的发到该会话，否则请宿主让用户选会话
    case CRecordDoneToast::kCmdSend:
        if (m_host == NULL)
            break;
        if (m_ctype != 0)
            m_host->SendFileToConversation(m_ctype, m_cid, m_lastPath.c_str());
        else
            m_host->PickConversationAndSendFile(m_lastPath.c_str());
        break;
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

//==================== 内部 ====================

void CRecordController::setStandaloneCamera(bool on)
{
    if (!m_recording || m_shareMode || on == m_cameraOn)
        return;
    if (on)
    {
        if (!m_camera.Start())
        {
            LOG_WARNING("record: camera unavailable while recording.");
            showError(TR(L"无法打开摄像头，它可能正被其他程序使用。"));
            m_bar.SetCameraToggle(true, false);
            return;
        }
        m_cameraOn = true;
        m_session.SetPipEnabled(true);
        showCameraWindow();
    }
    else
    {
        //先关小窗（它引用着摄像头）、停画中画，再停摄像头
        m_cameraOn = false;
        m_cameraWindow.Hide();
        m_session.SetPipEnabled(false);
        m_camera.Stop();
        syncCaptionPip();
    }
    LOG_INFO("record: camera %s while recording.", m_cameraOn ? "on" : "off");
    m_bar.SetCameraToggle(true, m_cameraOn);
}

void CRecordController::showCameraWindow()
{
    if (!m_cameraWindow.Show(DefaultPipWindowRect(recordedRect()), &m_camera, this))
        return;
    updatePipPlacement();
}

void CRecordController::updatePipPlacement()
{
    if (!m_cameraOn || !m_cameraWindow.IsShown())
        return;
    m_session.SetPipPlacement(MapPipWindowToContent(m_cameraWindow.ScreenRect(), recordedRect()));
    syncCaptionPip();
}

//==================== 语音字幕 ====================

void CRecordController::beginCaption(const std::wstring& path)
{
    m_captionActive = false;
    if (!m_wantCaption)
        return;
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule == NULL)
    {
        LOG_WARNING("record: caption wanted but caption module unavailable, recording without captions.");
        return;
    }
    const std::wstring selfName = selfDisplayName();
    caption::CaptionRecordingParams params;
    params.m_cbSize = sizeof(params);
    params.m_videoPath = path.c_str();
    params.m_selfName = selfName.empty() ? NULL : selfName.c_str();
    //麦克风与电脑声音两路都建识别流（2026-10-06 起）：录制中途才打开的那一路也能马上识别；没在录的一路不送声音，不占识别时间
    params.m_mic = true;
    params.m_system = true;
    caption::ICaptionRecording* recording = captionModule->BeginRecording(&params);
    if (recording == NULL)
    {
        LOG_WARNING("record: caption recording could not start, recording without captions.");
        return;
    }
    m_captionTap.Attach(recording, true, true);
    m_captionTap.SetSuspended(false);
    m_session.Audio().SetTap(&m_captionTap);
    m_captionActive = true;
    m_captionOn = true;
    //录制时屏幕上的实时字幕（14.12）：开始显示，并登记为草稿会话的实时结果观察者；两路声音的实际状态在录制开始后同步
    m_captionLive.Start(selfName, m_wantMic, m_wantSystem);
    recording->SetObserver(&m_captionLive);
    LOG_INFO("record: caption recording started (mic and system audio), mic=%d, system=%d.", m_wantMic ? 1 : 0,
             m_wantSystem ? 1 : 0);
}

void CRecordController::beginShareCaption(const std::wstring& path)
{
    if (path.empty())
        return;
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule == NULL)
    {
        LOG_WARNING("record: share caption arrived but caption module unavailable, recording without captions.");
        return;
    }
    const std::wstring selfName = selfDisplayName();
    caption::CaptionRecordingParams params;
    params.m_cbSize = sizeof(params);
    params.m_videoPath = path.c_str();
    params.m_selfName = selfName.empty() ? NULL : selfName.c_str();
    params.m_mic = false;
    params.m_system = false;
    caption::ICaptionRecording* recording = captionModule->BeginRecording(&params);
    if (recording == NULL)
    {
        LOG_WARNING("record: share caption recording could not start, recording without captions.");
        return;
    }
    //只用旁路对象持有草稿会话：两路识别都不开，不登记到混音器
    m_captionTap.Attach(recording, false, false);
    m_captionActive = true;
    LOG_INFO("record: share caption recording started.");
}

void CRecordController::endCaption(bool keep, int64_t durationMs)
{
    m_session.Audio().SetTap(NULL);
    caption::ICaptionRecording* recording = m_captionTap.Detach();
    m_captionTap.SetSuspended(false);
    //屏幕上的实时字幕：先取消观察者登记（返回后不再有回调），再清掉显示
    if (recording != NULL)
        recording->SetObserver(NULL);
    m_captionLive.Stop();
    if (recording == NULL)
        return;
    LOG_INFO("record: caption recording %s, duration=%lld ms.", keep ? "finishing" : "cancelled",
             static_cast<long long>(durationMs));
    if (keep)
        recording->Finish(durationMs);
    else
        recording->Cancel();
}

void CRecordController::syncCaptionPip()
{
    //草稿会话只由 UI 线程上的 endCaption 解除，这里也在 UI 线程，取到的指针在本函数内一直有效
    caption::ICaptionRecording* recording = m_captionTap.Current();
    if (recording == NULL)
        return;
    if (!m_cameraOn)
    {
        recording->SetPip(false, 0.0, 0.0, 0.0, 0.0);
    }
    else
    {
        const RecordPipPlacement placement = m_session.PipPlacement();
        if (placement.m_custom)
        {
            recording->SetPip(true, placement.m_x, placement.m_y, placement.m_x + placement.m_w,
                              placement.m_y + placement.m_h);
        }
        else
        {
            //默认摆在右下角：按画面来源的尺寸换算成比例
            const int w = m_capture.Width();
            const int h = m_capture.Height();
            if (w > 0 && h > 0)
            {
                const RECT rc = ComputePipRect(w, h);
                recording->SetPip(true, static_cast<double>(rc.left) / w, static_cast<double>(rc.top) / h,
                                  static_cast<double>(rc.right) / w, static_cast<double>(rc.bottom) / h);
            }
        }
    }
}

std::wstring CRecordController::selfDisplayName() const
{
    if (m_host == NULL)
        return std::wstring();
    const uint32_t kNameChars = 128;
    wchar_t name[kNameChars] = { 0 };
    if (!m_host->GetUserDisplayName(m_host->GetSelfUserId(), name, kNameChars))
        return std::wstring();
    return name;
}

void CRecordController::OnCaptionAvailabilityChanged(bool available)
{
    if (available || !m_captionTap.IsAttached())
        return;
    //管理员关闭了字幕或已退出登录：停止识别，已识别的部分收尾保存；录像照常继续（D5）
    LOG_INFO("record: caption became unavailable while recording, finishing caption recognition.");
    endCaption(true, m_session.RecordedMs());
    //录制条上的字幕开关随之显示为关闭（录制条按钮不在录制中途增删）
    m_captionOn = false;
    m_bar.SetCaptionToggle(m_captionActive && !m_shareMode, false);
}

void CRecordController::setCaptionOn(bool on)
{
    if (!m_recording || m_shareMode || !m_captionTap.IsAttached())
    {
        //没有进行中的字幕草稿（字幕已变为不可用）：开关保持关闭
        m_bar.SetCaptionToggle(m_captionActive && !m_shareMode, false);
        return;
    }
    if (on == m_captionOn)
        return;
    m_captionOn = on;
    //关闭期间旁路送静音：关闭前说到一半的那句照常定稿保留，关闭期间不产生新的字幕
    m_captionTap.SetSuspended(!on);
    m_captionLive.SetEnabled(on);
    m_bar.SetCaptionToggle(true, on);
    LOG_INFO("record: captions %s while recording.", on ? "on" : "off");
}

void CRecordController::syncCaptionLiveSources()
{
    //哪一路在录就识别哪一路
    m_captionLive.SetSources(m_micOn, m_systemOn);
}

bool CRecordController::recordedAreaVisible() const
{
    if (m_recordRegion.right > m_recordRegion.left && m_recordRegion.bottom > m_recordRegion.top)
        return true;
    if (m_recordSpec.m_kind == screenshare::share_source_window)
        return ::IsWindow(m_recordSpec.m_hwnd) && !::IsIconic(m_recordSpec.m_hwnd);
    return true;
}

void CRecordController::OnCameraWindowMoved(const RECT& screenRect)
{
    (void)screenRect;
    updatePipPlacement();
}

void CRecordController::setStandaloneAudio(int source, bool on)
{
    if (!m_recording || m_shareMode)
        return;
    const bool isMic = (source == record_audio_mic);
    bool& state = isMic ? m_micOn : m_systemOn;
    if (on == state)
        return;
    RecordAudioMixer& mixer = m_session.Audio();
    if (on)
    {
        //先打开混音器里的这一路，再启动采集：采集线程一开始交来的声音就能收下
        mixer.SetEnabled(source, true);
        const bool ok = isMic ? m_mic.Start(&mixer) : m_system.Start(&mixer);
        if (!ok)
        {
            mixer.SetEnabled(source, false);
            showError(isMic ? TR(L"无法打开麦克风，请检查麦克风是否接好、是否被其他程序占用。")
                            : TR(L"无法采集电脑声音，请检查播放设备。"));
        }
        state = ok;
    }
    else
    {
        if (isMic)
            m_mic.Stop();
        else
            m_system.Stop();
        //关掉这一路：之后写进录像的这一路是静音，时间轴不断
        mixer.SetEnabled(source, false);
        state = false;
    }
    LOG_INFO("record: %s %s while recording.", isMic ? "microphone" : "system audio", state ? "on" : "off");
    m_bar.SetAudioToggles(true, m_micOn, m_systemOn);
    //屏幕上的实时字幕：两路都在录时才标注说话人
    syncCaptionLiveSources();
}

void CRecordController::cleanupSources()
{
    //白板（第 21 期）：录的是白板时一并关掉
    if (m_recordingBoard)
    {
        m_recordingBoard = false;
        m_board.Close();
    }
    //「允许截图」只对这一次录制有效（2026-10-08）：结束时恢复不允许
    screenshare::SetBarCaptureAllowed(false);
    //画笔（第 21 期）：关显示层与工具条、清空笔迹
    m_penMode = false;
    m_penOverlay.Close();
    m_pen.Stop();
    m_bar.SetPenActive(false);
    if (IsWindow())
    {
        KillTimer(kTickTimerId);
        KillTimer(kLevelTimerId);
    }
    m_mic.Stop();
    m_system.Stop();
    //摄像头小窗引用着摄像头，先关小窗再停摄像头
    m_cameraWindow.Hide();
    m_cameraOn = false;
    m_session.SetPipEnabled(false);
    m_camera.Stop();
    m_capture.Stop();
    m_bar.Hide();
    //只录声音（第 24 期）：录制条恢复成录像的样子，下次录像时不会带着电平条
    m_bar.SetAudioOnly(false);
    m_audioOnly = false;
    m_border.Hide();
    m_session.Audio().SetEnabled(record_audio_mic, false);
    m_session.Audio().SetEnabled(record_audio_others, false);
    m_session.Audio().SetEnabled(record_audio_system, false);
}

screenshare::ShareSourceSpec CRecordController::mainMonitorSpec() const
{
    screenshare::ShareSourceSpec spec;
    spec.m_kind = screenshare::share_source_monitor;
    const HMONITOR mon = ::MonitorFromWindow(mainWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
        spec.m_monitorRect = mi.rcMonitor;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &spec.m_monitorRect, 0);
    return spec;
}

void CRecordController::startAudioOnly()
{
    if (!m_wantMic && !m_wantSystem)
    {
        //选择窗在两项都没勾时不让点「只录声音」，这里再判断一次
        showError(TR(L"请至少勾选「录制麦克风」或「录制电脑声音」中的一项。"));
        return;
    }
    const std::wstring path = makeOutputPath(true);
    if (path.empty())
    {
        showError(TR(L"无法创建录制文件，请检查保存目录。"));
        return;
    }
    //先打开两路采集：两路都打不开就没有可录的声音，不开始录制，也不留下空文件
    RecordAudioMixer& mixer = m_session.Audio();
    mixer.SetEnabled(record_audio_others, false);
    mixer.SetEnabled(record_audio_mic, m_wantMic);
    mixer.SetEnabled(record_audio_system, m_wantSystem);
    m_micOn = m_wantMic && m_mic.Start(&mixer);
    if (m_wantMic && !m_micOn)
    {
        LOG_WARNING("record: microphone unavailable for audio-only recording.");
        mixer.SetEnabled(record_audio_mic, false);
    }
    m_systemOn = m_wantSystem && m_system.Start(&mixer);
    if (m_wantSystem && !m_systemOn)
    {
        LOG_WARNING("record: system audio unavailable for audio-only recording.");
        mixer.SetEnabled(record_audio_system, false);
    }
    if (!m_micOn && !m_systemOn)
    {
        showError(TR(L"无法打开麦克风，也无法采集电脑声音，录音没有开始。"));
        return;
    }
    //采集比写入线程先开始：丢掉开始之前攒下的一点声音，录音从写入线程打开文件时算起
    mixer.Clear();
    mixer.TakePeak(record_audio_mic);
    mixer.TakePeak(record_audio_system);
    RecordOptions opt;
    opt.m_path = path;
    opt.m_audioOnly = true;
    m_session.SetDecorator(NULL);
    //语音字幕：录音同样可以边录边识别（第 54 条），要在写入线程开始取声音之前接上旁路
    beginCaption(path);
    if (!m_session.Start(opt, NULL, NULL, this))
    {
        endCaption(false, 0);
        m_mic.Stop();
        m_system.Stop();
        mixer.SetEnabled(record_audio_mic, false);
        mixer.SetEnabled(record_audio_system, false);
        showError(m_session.FailReason() == record_fail_disk_full ? TR(L"磁盘剩余空间不足，无法开始录制。")
                                                                   : TR(L"无法开始录制。"));
        return;
    }
    m_recording = true;
    m_audioOnly = true;
    m_shareMode = false;
    m_shareOwner = NULL;
    m_cameraOn = false;
    //录制条与实时字幕都摆在主面板所在的那块显示器上
    m_recordSpec = mainMonitorSpec();
    memset(&m_recordRegion, 0, sizeof(m_recordRegion));
    m_penMode = false;
    m_bar.SetAudioOnly(true);
    m_bar.SetPenAvailable(false);
    m_bar.SetAudioToggles(true, m_micOn, m_systemOn);
    m_bar.SetCameraToggle(false, false);
    m_bar.SetCaptionToggle(m_captionActive, m_captionOn);
    m_bar.SetPenActive(false);
    m_bar.Show(m_recordSpec.m_monitorRect, m_hWnd);
    m_bar.SetState(0, false);
    if (m_captionLive.IsActive())
    {
        syncCaptionLiveSources();
        m_captionLive.Tick(recordedRect(), recordedAreaVisible());
    }
    SetTimer(kTickTimerId, kTickMs, NULL);
    SetTimer(kLevelTimerId, kLevelMs, NULL);
    LOG_INFO("record: audio-only recording started, mic=%d, system=%d, caption=%d.", m_micOn ? 1 : 0,
             m_systemOn ? 1 : 0, m_captionActive ? 1 : 0);
}

void CRecordController::updateLevels()
{
    //关着的一路显示 0；开着的一路取上次以来的峰值换算成电平
    RecordAudioMixer& mixer = m_session.Audio();
    const int micPeak = mixer.TakePeak(record_audio_mic);
    const int systemPeak = mixer.TakePeak(record_audio_system);
    m_bar.SetLevels(m_micOn ? RecordLevelPercent(micPeak) : 0, m_systemOn ? RecordLevelPercent(systemPeak) : 0);
}

std::wstring CRecordController::makeOutputPath(bool audioOnly)
{
    std::wstring dir = m_folder.empty() ? defaultFolder() : m_folder;
    if (dir.empty())
        return std::wstring();
    while (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/'))
        dir.erase(dir.size() - 1);
    const int r = ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS)
    {
        LOG_ERROR("record: create folder failed, error=%d.", r);
        return std::wstring();
    }
    SYSTEMTIME now;
    ::GetLocalTime(&now);
    for (int seq = 0; seq <= kMaxNameSeq; ++seq)
    {
        const std::wstring path = dir + L"\\" + MakeRecordFileName(now, seq, audioOnly);
        if (!fileExists(path))
            return path;
    }
    return std::wstring();
}

void CRecordController::addRecordingRecord(const std::wstring& path, int64_t durationMs, uint64_t bytes)
{
    if (m_host == NULL)
        return;
    FILETIME now;
    ::GetSystemTimeAsFileTime(&now);
    flamingomedia::MediaRecordingInfo info;
    if (!MakeRecordingInfo(path, durationMs, bytes, RecordFileTimeToUnixMs(now), info))
    {
        LOG_WARNING("record: recording record not added, path empty or too long (%u chars).",
                    static_cast<unsigned>(path.size()));
        return;
    }
    const bool ok = m_host->AddRecording(&info);
    LOG_INFO("record: recording record added=%d, duration=%lld ms, %llu bytes.", ok ? 1 : 0,
             static_cast<long long>(durationMs), static_cast<unsigned long long>(bytes));
    //录制管理窗口开着时把新录像列出来
    m_manager.ReloadIfOpen();
}

void CRecordController::showError(const wchar_t* text)
{
    ::MessageBoxW(mainWindow(), text, TR(kCaption), MB_OK | MB_ICONINFORMATION);
}

HWND CRecordController::mainWindow() const
{
    return m_host != NULL ? m_host->GetMainWindow() : NULL;
}

} // namespace screenrecord
