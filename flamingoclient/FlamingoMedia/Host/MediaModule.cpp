/**
 *  IMediaModule 的实现，见 MediaModule.h。
 *  balloonwj@qq.com   2026-10-02（1c 起持有通话控制器，承接 1701~1709 信令、发起呼叫与通话调试入口；
 *  1d 起持有远程桌面控制器与调试入口，承接 1710~1716 信令、发起远程桌面与远程桌面调试入口）
 */
#include "stdafx.h"

#include "MediaModule.h"
#include "MediaLog.h"
#include "MediaLockState.h"   //SetMediaScreenLocked：锁屏状态的全局标志
#include "../Common/OsVersionPolicy.h"
#include "../Call/CallController.h"
#include "../Call/CallDebugEntries.h"
#include "../RemoteDesktop/RdController.h"
#include "../RemoteDesktop/RdDebugEntries.h"
#include "../Share/ShareController.h"
#include "../Share/ShareDebugEntries.h"
#include "../Share/ShareRecordMutex.h"   //CheckScreenStartConflict：录制与共享互斥（2026-10-07）
#include "../Record/RecordController.h"
#include "../Voice/VoiceController.h"   //语音消息的录音与播放
#include "../GroupCall/GroupCallController.h"   //群通话
#include "../Record/CloudRecordCenter.h"   //云端录像：合成、上传、打开、发到会话（第 27 期）
#include "../Record/CloudExitDialog.h"     //退出、登出、关机前的「云端录像」对话框（2026-10-07）
#include "../Caption/MediaCaptionBridge.h"   //语音字幕：登记宿主、转发可用状态变化
#include "../../Source/UI/BalloonMain/NotificationSound.h"   //StopLoopSound：锁屏时停止本 DLL 那份循环铃声
#include <string.h>   //memset
#include <sodium.h>   //sodium_init：初始化本 DLL 自己那一份 libsodium
#include "../../Source/Common/Log/IULog.h"   //LOG_INFO 等宏；DLL 内由 IULogBridge.cpp 实现，经宿主写日志
#include "../../Source/Common/I18n/I18n.h"   //i18n::InitFromHost：DLL 的界面文字经宿主的语言模块翻译
#include "../../../third-party/balloonui/balloonui/DuiTextResolver.h"   //SetTextResolver：本 DLL 那份 balloonui 的内置文字转换
#include "../../Source/Common/I18n/BalloonUiText.h"   //ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"   //SetDefaultFontFace：本 DLL 那份 balloonui 的默认字体随界面语言

namespace flamingomedia {

namespace {

//语言代码缓冲区的容量（字符），足够放下 "zh-CN"、"qps-ploc" 一类的代码
const uint32_t kLangCodeChars = 32;

//DLL 里 TR 查找时转交的宿主对象；Init 时登记，Shutdown 时清空。可能在任意线程读取
IMediaHost* volatile g_translateHost = NULL;

//交给 i18n::InitFromHost 的查找函数：转给宿主的 Translate；宿主已解除登记时返回原文
const wchar_t* HostTranslate(const wchar_t* context, const wchar_t* source)
{
    IMediaHost* host = g_translateHost;
    return (host != NULL) ? host->Translate(context, source) : source;
}

//把只含 ASCII 的宽字符串（语言代码、字体名）转成窄字符串写日志；非 ASCII 字符写成 '?'
std::string AsciiForLog(const wchar_t* text)
{
    std::string out;
    for (const wchar_t* p = text; p != NULL && *p != L'\0'; ++p)
    {
        out.push_back((*p > 0 && *p < 0x80) ? static_cast<char>(*p) : '?');
    }
    return out;
}

} // namespace

CMediaModule& CMediaModule::Instance()
{
    static CMediaModule s_instance;
    return s_instance;
}

/**
 *  语音消息的桥接对象：持有语音控制器，替它回答「麦克风是否被占用」（问通话与共享控制器），并把语音事件转给宿主。
 *  只由 CMediaModule 使用，所有方法都在 UI 线程上调用。
 */
class CVoiceBridge : public voicemsg::IVoiceMicBusyQuery, public voicemsg::IVoiceControllerSink
{
public:
    /**
     *  @param host  宿主接口（不拥有），须存活到本对象析构
     *  @param call  通话控制器（不拥有），可以为 NULL（通话不可用）
     *  @param share 屏幕共享控制器（不拥有），可以为 NULL（屏幕共享不可用）
     *  @param groupCall 群通话控制器（不拥有），可以为 NULL（群通话不可用）
     */
    CVoiceBridge(IMediaHost* host, call::CCallController* call, screenshare::CShareController* share,
                 groupcall::CGroupCallController* groupCall)
        : m_host(host)
        , m_call(call)
        , m_share(share)
        , m_groupCall(groupCall)
        , m_controller(this, this, NULL, NULL)
    {
    }

    //语音控制器（录音、播放与消息窗口）
    voicemsg::CVoiceController& Controller()
    {
        return m_controller;
    }

    //一对一通话的媒体会话在运行、屏幕共享开着麦克风、或群通话开着麦克风时返回 true
    virtual bool IsMicrophoneBusy() override
    {
        return (m_call != NULL && m_call->IsMicrophoneInUse()) || (m_share != NULL && m_share->IsMicrophoneInUse()) ||
               (m_groupCall != NULL && m_groupCall->IsMicrophoneInUse());
    }

    //转给宿主
    virtual void OnVoiceRecordLevel(int level) override
    {
        m_host->OnVoiceRecordLevel(level);
    }

    //转给宿主
    virtual void OnVoiceRecordLimitReached() override
    {
        m_host->OnVoiceRecordLimitReached();
    }

    //转给宿主
    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) override
    {
        m_host->OnVoicePlayProgress(token, playedMs, totalMs);
    }

    //转给宿主
    virtual void OnVoicePlayFinished(uint32_t token) override
    {
        m_host->OnVoicePlayFinished(token);
    }

private:
    IMediaHost*                    m_host;         //宿主接口，不拥有
    call::CCallController*         m_call;         //通话控制器，不拥有，可以为 NULL
    screenshare::CShareController*  m_share;        //屏幕共享控制器，不拥有，可以为 NULL
    groupcall::CGroupCallController* m_groupCall;   //群通话控制器，不拥有，可以为 NULL
    voicemsg::CVoiceController     m_controller;   //语音控制器（本对象拥有）
};

CMediaModule::CMediaModule()
    : m_host(NULL)
    , m_initialized(false)
{
}

CMediaModule::~CMediaModule()
{
    //正常流程下 exe 在卸载前已调用 Shutdown；这里兜底，避免通话窗等 ATL 窗口带着 HWND 析构
    Shutdown();
}

uint32_t CMediaModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CMediaModule::Init(IMediaHost* host, const MediaInitParams* params)
{
    //已初始化：直接返回成功，不重复初始化（调用方可能在重连等路径上再次调用）
    if (m_initialized)
    {
        return true;
    }
    //参数校验：结构体大小不符说明 exe 与 DLL 的接口头文件不是同一版，拒绝初始化
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(MediaInitParams) || params->m_appDir == NULL)
    {
        return false;
    }

    m_host = host;
    m_initialized = true;
    MediaLogSetHost(host);
    //本 DLL 静态链接了自己的一份 libsodium，全局状态与 exe 那一份互不相干，exe 启动时的 sodium_init 初始化不到这里。
    //不初始化时随机数源与 ChaCha20、Poly1305 等算法的实现没有按 CPU 特性选择，中转加解密慢一倍多。
    //须在创建任何会加解密的控制器之前调用；可重复调用。返回值写进日志：0 为本次完成初始化，1 为此前已初始化，-1 为失败
    const int sodiumRet = sodium_init();
    LOG_INFO("FlamingoMedia: sodium_init returned %d.", sodiumRet);
    //语音字幕：各控制器经衔接对象询问能否使用、取字幕模块，须在创建控制器之前登记宿主
    CMediaCaptionBridge::Instance().Attach(host);

    //多语言（第 3a 步）：界面文字经宿主的语言模块翻译，界面字体取宿主当前生效的字体；须在创建任何窗口之前
    wchar_t language[kLangCodeChars] = { 0 };
    wchar_t uiFont[LF_FACESIZE] = { 0 };
    g_translateHost = host;
    if (host->GetUiLanguage(language, kLangCodeChars, uiFont, LF_FACESIZE))
    {
        i18n::InitFromHost(&HostTranslate, language, uiFont);
        //本 DLL 静态链接了自己的一份 balloonui，它的默认字体也要换成界面字体（第 3e 步）
        balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
        balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);   //内置文字（右键菜单等）同样经宿主翻译
        LOG_INFO("FlamingoMedia ui language %s, ui font %s.", AsciiForLog(language).c_str(),
                 AsciiForLog(uiFont).c_str());
    }
    else
    {
        LOG_WARNING("FlamingoMedia: host did not report the ui language, using built-in source text.");
    }

    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    bool gotVersion = GetOsVersion(major, minor, build);
    bool captureOk = gotVersion && IsScreenCaptureSupportedBuild(major, minor, build);
    LOG_INFO("FlamingoMedia initialized, api version %u, os %u.%u.%u (%s), screen capture %s.", kApiVersion,
             major, minor, build, gotVersion ? "ok" : "unknown", captureOk ? "supported" : "unsupported");

    //一对一通话控制器：建不出消息窗口时通话不可用，但模块本身照常初始化（记错误日志）
    m_call.reset(new call::CCallController());
    if (!m_call->Init(host))
    {
        LOG_ERROR("FlamingoMedia: call controller init failed, one-to-one call is unavailable.");
        m_call.reset();
    }
    //远程桌面控制器：同上，建不出消息窗口时远程桌面不可用，模块照常初始化
    m_rd.reset(new remotedesktop::CRdController());
    if (!m_rd->Init(host))
    {
        LOG_ERROR("FlamingoMedia: remote desktop controller init failed, remote desktop is unavailable.");
        m_rd.reset();
    }
    //远程桌面调试入口：只在命令行带对应开关时才建窗口，这里只创建对象
    m_rdDebug.reset(new remotedesktop::CRdDebugEntries());
    //录屏控制器：先于屏幕共享控制器创建，共享录制要用它；建不出消息窗口时录屏不可用
    m_record.reset(new screenrecord::CRecordController());
    if (!m_record->Init(host))
    {
        LOG_ERROR("FlamingoMedia: record controller init failed, screen recording is unavailable.");
        m_record.reset();
    }
    //屏幕共享控制器：同上，建不出消息窗口时屏幕共享不可用，模块照常初始化
    m_share.reset(new screenshare::CShareController());
    if (!m_share->Init(host))
    {
        LOG_ERROR("FlamingoMedia: screen share controller init failed, screen share is unavailable.");
        m_share.reset();
    }
    else
    {
        m_share->SetRecorder(m_record.get());
        //通话中共享屏幕（第 8 期）：通话控制器与共享控制器互相登记
        if (m_call)
        {
            m_call->SetShareLink(m_share.get());
            m_share->SetCallObserver(m_call.get());
        }
    }
    //群通话控制器：建不出消息窗口时群通话不可用，模块照常初始化
    m_groupCall.reset(new groupcall::CGroupCallController());
    if (!m_groupCall->Init(host))
    {
        LOG_ERROR("FlamingoMedia: group call controller init failed, group call is unavailable.");
        m_groupCall.reset();
    }
    //云端录像中心（第 27 期）：后台合成与上传、打开、发到会话
    m_cloud.reset(new screenrecord::CCloudRecordCenter());
    m_cloud->Init(host);
    //独立录制「保存到云端」经云端录像中心上传
    if (m_record)
        m_record->SetCloudCenter(m_cloud.get());
    //语音消息：问通话、共享与群通话控制器麦克风是否被占用；建不出消息窗口时语音消息不可用，模块照常初始化
    m_voice.reset(new CVoiceBridge(host, m_call.get(), m_share.get(), m_groupCall.get()));
    if (!m_voice->Controller().Init())
    {
        LOG_ERROR("FlamingoMedia: voice controller init failed, voice message is unavailable.");
        m_voice.reset();
    }
    return true;
}

void CMediaModule::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    //语音消息最先关闭：停止录音（不写文件）与播放，销毁消息窗口；它要问通话与共享控制器麦克风是否被占用
    if (m_voice)
    {
        m_voice->Controller().Shutdown();
        m_voice.reset();
    }
    //云端录像中心：取消合成（等合成线程退出）与传输，关提示
    if (m_cloud)
    {
        //录屏控制器在后面才关闭，先解除它对云端录像中心的引用
        if (m_record)
            m_record->SetCloudCenter(NULL);
        m_cloud->Shutdown();
        m_cloud.reset();
    }
    //群通话：发离开或拒绝信令，停媒体、关窗口（此时日志仍能写出）
    if (m_groupCall)
    {
        m_groupCall->Shutdown();
        m_groupCall.reset();
    }
    //通话与共享两个控制器互相登记过，先解除，免得先销毁的一方在另一方收尾时被回调
    if (m_call)
    {
        m_call->SetShareLink(NULL);
    }
    if (m_share)
    {
        m_share->SetCallObserver(NULL);
    }
    //先收尾通话（停铃声、停媒体、关窗）与远程桌面（停推流、关观看窗与提示条），此时日志仍能写出
    if (m_call)
    {
        m_call->Shutdown();
        m_call.reset();
    }
    if (m_rd)
    {
        m_rd->Shutdown();
        m_rd.reset();
    }
    //屏幕共享：发结束或离开信令，停推流、关闭全部窗口
    if (m_share)
    {
        m_share->Shutdown();
        m_share.reset();
    }
    //录屏：停止并保存进行中的录制，关闭全部窗口（共享控制器已先关闭，不会再用它）
    if (m_record)
    {
        m_record->Shutdown();
        m_record.reset();
    }
    //调试入口留下的预览窗口：窗口过程在本 DLL 里，DLL 卸载之前必须销毁
    if (m_rdDebug)
    {
        m_rdDebug->DestroyWindows();
        m_rdDebug.reset();
    }
    LOG_INFO("FlamingoMedia shutdown.");
    //控制器都已关闭，解除字幕衔接对象的宿主登记（字幕模块本身由 exe 在本 DLL 卸载之后关闭）
    CMediaCaptionBridge::Instance().Detach();
    //先解除日志登记再清空宿主：此后 DLL 内任何线程写的日志都会被丢弃，不再触碰宿主
    MediaLogSetHost(NULL);
    //同理解除翻译的登记：此后 DLL 内的 TR 返回原文，不再触碰宿主
    g_translateHost = NULL;
    m_host = NULL;
    m_initialized = false;
}

bool CMediaModule::OnSignal(uint16_t cmd, const char* json, uint32_t len)
{
    if (!m_initialized || json == NULL || len == 0)
    {
        return false;
    }
    //一对一通话：1701~1709
    if (m_call && call::CCallController::IsCallSignal(cmd))
    {
        return m_call->OnSignal(cmd, std::string(json, len));
    }
    //远程桌面：1710~1716
    if (m_rd && remotedesktop::CRdController::IsRdSignal(cmd))
    {
        return m_rd->OnSignal(cmd, std::string(json, len));
    }
    //屏幕共享：1720~1731
    if (m_share && screenshare::CShareController::IsShareSignal(cmd))
    {
        return m_share->OnSignal(cmd, std::string(json, len));
    }
    //群通话：1740~1751
    if (m_groupCall && groupcall::CGroupCallController::IsGroupCallSignal(cmd))
    {
        return m_groupCall->OnSignal(cmd, std::string(json, len));
    }
    //云端录像：1755~1757（1754 归屏幕共享）
    if (m_cloud && screenrecord::CCloudRecordCenter::IsCloudRecordSignal(cmd))
    {
        return m_cloud->OnSignal(cmd, std::string(json, len));
    }
    LOG_WARNING("media signal not handled, cmd=%u, len=%u.", static_cast<unsigned>(cmd), static_cast<unsigned>(len));
    return false;
}

void CMediaModule::OnConnectionStateChanged(bool online)
{
    if (!m_initialized)
    {
        return;
    }
    LOG_INFO("connection state changed, online=%d.", online ? 1 : 0);
    //云端录像：登录后列一次本人的录像，补做没合成的
    if (m_cloud)
    {
        m_cloud->OnConnectionStateChanged(online);
    }
}

bool CMediaModule::IsFeatureSupported(int feature)
{
    switch (feature)
    {
    //屏幕共享：按系统版本号判定
    case media_feature_screen_share:
    //屏幕录制：系统要求与屏幕共享相同，共用同一判定
    case media_feature_screen_record:
        {
            uint32_t major = 0;
            uint32_t minor = 0;
            uint32_t build = 0;
            return GetOsVersion(major, minor, build) && IsScreenCaptureSupportedBuild(major, minor, build);
        }
    //不认识的功能：一律视为不可用
    default:
        return false;
    }
}

void CMediaModule::OnLogout()
{
    if (!m_initialized)
    {
        return;
    }
    LOG_INFO("media module: user logged out, end active sessions.");
    if (m_call)
    {
        m_call->ResetForLogout();
    }
    if (m_rd)
    {
        m_rd->ResetForLogout();
    }
    if (m_share)
    {
        m_share->ResetForLogout();
    }
    if (m_groupCall)
    {
        m_groupCall->ResetForLogout();
    }
    //云端录像：没做完的合成与上传丢弃，下次登录后重新做
    if (m_cloud)
    {
        m_cloud->OnLogout();
    }
}

bool CMediaModule::StartCall(int64_t peerUserId, const wchar_t* peerName, int callType)
{
    if (!m_initialized || !m_call)
    {
        LOG_WARNING("StartCall rejected: module not ready, peer=%lld.", static_cast<long long>(peerUserId));
        return false;
    }
    if (RejectIfInGroupCall())
    {
        return false;
    }
    if (callType != media_call_audio && callType != media_call_video)
    {
        LOG_WARNING("StartCall rejected: invalid call type %d, peer=%lld.", callType, static_cast<long long>(peerUserId));
        return false;
    }
    CString name(peerName != NULL ? peerName : L"");
    return m_call->StartCall(peerUserId, name, callType == media_call_video);
}

bool CMediaModule::StartRemoteDesktop(int64_t peerUserId, const wchar_t* peerName)
{
    if (!m_initialized || !m_rd)
    {
        LOG_WARNING("StartRemoteDesktop rejected: module not ready, peer=%lld.", static_cast<long long>(peerUserId));
        return false;
    }
    CString name(peerName != NULL ? peerName : L"");
    return m_rd->StartRemoteDesktop(peerUserId, name);
}

bool CMediaModule::StartScreenShare(int ctype, int64_t cid, const wchar_t* conversationName)
{
    if (!m_initialized || !m_share)
    {
        LOG_WARNING("StartScreenShare rejected: module not ready, ctype=%d, cid=%lld.", ctype, static_cast<long long>(cid));
        return false;
    }
    if (RejectIfInGroupCall())
    {
        return false;
    }
    return m_share->StartShare(ctype, cid, conversationName != NULL ? conversationName : L"");
}

bool CMediaModule::JoinScreenShare(int ctype, int64_t cid, const char* roomId, const wchar_t* hostName, bool forced)
{
    if (!m_initialized || !m_share || roomId == NULL || roomId[0] == '\0')
    {
        LOG_WARNING("JoinScreenShare rejected: module not ready or empty room, ctype=%d, cid=%lld.", ctype,
                    static_cast<long long>(cid));
        return false;
    }
    if (RejectIfInGroupCall())
    {
        return false;
    }
    return m_share->JoinShare(ctype, cid, roomId, hostName != NULL ? hostName : L"", forced);
}

void CMediaModule::OnCloudTransferProgress(uint32_t id, int64_t doneBytes, int64_t totalBytes)
{
    if (m_initialized && m_cloud)
    {
        m_cloud->OnTransferProgress(id, doneBytes, totalBytes);
    }
}

void CMediaModule::OnCloudTransferFinished(uint32_t id, int result)
{
    if (m_initialized && m_cloud)
    {
        m_cloud->OnTransferFinished(id, result);
    }
}

bool CMediaModule::HasCloudRecordWork()
{
    return m_initialized && m_cloud && m_cloud->HasWork();
}

void CMediaModule::OpenCloudRecord(int64_t recordId, const wchar_t* title)
{
    if (m_initialized && m_cloud)
    {
        m_cloud->OpenRecord(recordId, title != NULL ? std::wstring(title) : std::wstring());
    }
}

void CMediaModule::OnConversationsPicked(uint32_t token, const MediaConversationRef* refs, uint32_t count)
{
    if (m_initialized && m_cloud)
    {
        m_cloud->OnConversationsPicked(token, refs, count);
    }
}

int CMediaModule::ConfirmCloudRecordExit(int kind)
{
    const int keep = (kind == media_cloud_exit_shutdown) ? media_cloud_exit_shutdown_keep : media_cloud_exit_keep;
    //没有未完成的云端录像：不弹框，直接放行
    if (!m_initialized || !m_cloud || !m_cloud->HasWork())
    {
        return keep;
    }
    const std::vector<std::pair<int64_t, std::wstring> > records = m_cloud->UnfinishedRecords();
    std::vector<std::wstring> titles;
    for (size_t i = 0; i < records.size(); ++i)
    {
        titles.push_back(records[i].second);
    }
    screenrecord::CCloudExitDialog dlg;
    const int choice = dlg.RunModal(m_host != NULL ? m_host->GetMainWindow() : NULL, kind, titles);
    //选择放弃：取消本机的合成与上传、请服务端删除这些录像，返回之前就发出请求
    if (screenrecord::CloudExitChoiceAbandons(choice))
    {
        m_cloud->AbandonUnfinished();
    }
    LOG_INFO("FlamingoMedia: cloud record exit confirm, kind %d, %u records, choice %d.", kind,
             static_cast<unsigned>(records.size()), choice);
    return choice;
}

bool CMediaModule::JoinScreenShareByLink(const wchar_t* url)
{
    if (!m_initialized || !m_share || url == NULL)
    {
        LOG_WARNING("JoinScreenShareByLink rejected: module not ready or no url.");
        return false;
    }
    if (RejectIfInGroupCall())
    {
        return false;
    }
    return m_share->JoinShareByLink(url);
}

void CMediaModule::QueryScreenShare(int ctype, int64_t cid)
{
    if (!m_initialized || !m_share)
    {
        return;
    }
    m_share->QueryShare(ctype, cid);
}

bool CMediaModule::StartRecording(int ctype, int64_t cid, const wchar_t* conversationName)
{
    if (!m_initialized || !m_record)
    {
        LOG_WARNING("StartRecording rejected: module not ready.");
        return false;
    }
    //本人正在共享屏幕（含共享选择窗开着）时不能发起独立录制（2026-10-07，录制与共享互斥）；共享控制条上的
    //「录下这场共享」不走这里。只是在观看别人的共享时不限制
    const bool shareBusy = m_share && m_share->IsLocalShareBusy();
    const int conflict = screenshare::CheckScreenStartConflict(screenshare::screen_start_record, false, shareBusy);
    if (conflict != screenshare::screen_start_ok)
    {
        LOG_INFO("StartRecording rejected: the user is sharing the screen.");
        m_record->ShowStartRejected(screenshare::ScreenStartConflictText(conflict));
        return false;
    }
    return m_record->StartRecording(ctype, cid, conversationName != NULL ? conversationName : L"");
}

void CMediaModule::StopRecording()
{
    if (!m_initialized || !m_record)
    {
        return;
    }
    m_record->StopRecording();
}

bool CMediaModule::IsRecording()
{
    return m_initialized && m_record && m_record->IsRecording();
}

void CMediaModule::SetRecordSettings(int quality, const wchar_t* folder)
{
    if (!m_initialized || !m_record)
    {
        return;
    }
    m_record->SetSettings(quality, folder != NULL ? folder : L"");
}

void CMediaModule::SetShareQuality(int quality)
{
    if (!m_initialized)
    {
        return;
    }
    if (m_share)
    {
        m_share->SetQuality(quality);
    }
}

void CMediaModule::SetMouseEffectSettings(const char* settingsText)
{
    if (!m_initialized)
    {
        return;
    }
    const screenshare::ShareMouseEffectSettings settings =
        screenshare::ParseMouseEffectSettings(settingsText != NULL ? settingsText : "");
    if (m_share)
    {
        m_share->SetMouseEffects(settings);
    }
    if (m_record)
    {
        m_record->SetMouseEffects(settings);
    }
}

int CMediaModule::StartVoiceRecord(const wchar_t* path)
{
    if (!m_initialized || !m_voice || path == NULL || path[0] == L'\0')
        return media_voice_record_not_ready;
    //voicemsg::VoiceRecordStartResult 与 MediaVoiceRecordStart 取值一一对应
    return m_voice->Controller().StartRecord(path);
}

bool CMediaModule::StopVoiceRecord(MediaVoiceRecordInfo* info)
{
    if (!m_initialized || !m_voice || info == NULL)
        return false;
    voicemsg::VoiceRecordResult result;
    if (!m_voice->Controller().StopRecord(result))
        return false;
    info->m_durationMs = result.m_durationMs;
    info->m_fileBytes = result.m_fileBytes;
    info->m_waveCount = 0;
    memset(info->m_wave, 0, sizeof(info->m_wave));
    for (size_t i = 0; i < result.m_wave.size() && i < static_cast<size_t>(kMediaVoiceWaveBars); ++i)
    {
        info->m_wave[i] = result.m_wave[i];
        ++info->m_waveCount;
    }
    return true;
}

void CMediaModule::CancelVoiceRecord()
{
    if (!m_initialized || !m_voice)
        return;
    m_voice->Controller().CancelRecord();
}

bool CMediaModule::PlayVoice(const wchar_t* path, uint32_t token)
{
    if (!m_initialized || !m_voice || path == NULL || path[0] == L'\0' || token == 0)
        return false;
    return m_voice->Controller().Play(path, token);
}

void CMediaModule::StopVoicePlay()
{
    if (!m_initialized || !m_voice)
        return;
    m_voice->Controller().StopPlay();
}

bool CMediaModule::PauseVoiceRecord()
{
    if (!m_initialized || !m_voice)
        return false;
    return m_voice->Controller().PauseRecord();
}

int CMediaModule::ResumeVoiceRecord()
{
    if (!m_initialized || !m_voice)
        return media_voice_record_not_ready;
    //voicemsg::VoiceRecordStartResult 与 MediaVoiceRecordStart 取值一一对应（含第 6d 步新增的 not_paused）
    return m_voice->Controller().ResumeRecord();
}

bool CMediaModule::SnapshotVoiceRecord(const wchar_t* path, int64_t* outDurationMs)
{
    if (outDurationMs != NULL)
        *outDurationMs = 0;
    if (!m_initialized || !m_voice || path == NULL || outDurationMs == NULL)
        return false;
    return m_voice->Controller().SnapshotPausedRecord(path, *outDurationMs);
}

bool CMediaModule::StartGroupCall(int64_t groupId, const wchar_t* groupName, int media, const int64_t* invitees,
                                  uint32_t count)
{
    if (!m_initialized || !m_groupCall || groupId <= 0)
    {
        LOG_WARNING("StartGroupCall rejected: module not ready or bad group, group=%lld.", static_cast<long long>(groupId));
        return false;
    }
    if (media != groupcall::gc_media_voice && media != groupcall::gc_media_video)
    {
        LOG_WARNING("StartGroupCall rejected: invalid media %d, group=%lld.", media, static_cast<long long>(groupId));
        return false;
    }
    std::vector<int64_t> ids;
    if (invitees != NULL)
        ids.assign(invitees, invitees + count);
    return m_groupCall->StartCall(groupId, groupName != NULL ? groupName : L"", media, ids);
}

bool CMediaModule::JoinGroupCall(int64_t groupId, const wchar_t* groupName, const char* roomId)
{
    if (!m_initialized || !m_groupCall || groupId <= 0 || roomId == NULL || roomId[0] == '\0')
    {
        LOG_WARNING("JoinGroupCall rejected: module not ready or empty room, group=%lld.", static_cast<long long>(groupId));
        return false;
    }
    return m_groupCall->JoinCall(groupId, groupName != NULL ? groupName : L"", roomId);
}

void CMediaModule::QueryGroupCall(int64_t groupId)
{
    if (!m_initialized || !m_groupCall || groupId <= 0)
        return;
    m_groupCall->Query(groupId);
}

bool CMediaModule::InviteToGroupCall(int64_t groupId, const int64_t* userIds, uint32_t count)
{
    if (!m_initialized || !m_groupCall || userIds == NULL || count == 0)
        return false;
    return m_groupCall->InviteMembers(groupId, std::vector<int64_t>(userIds, userIds + count));
}

void CMediaModule::OnAvatarsUpdated()
{
    if (!m_initialized)
        return;
    if (m_call)
        m_call->OnAvatarsUpdated();
    if (m_groupCall)
        m_groupCall->OnAvatarsUpdated();
}

void CMediaModule::ShowRecordManager()
{
    //录制管理窗口归录屏控制器持有（录制完成写入记录后由它刷新已打开的窗口）
    if (!m_initialized || !m_record)
        return;
    LOG_INFO("media module: record manager requested.");
    m_record->ShowManager();
}

void CMediaModule::SetScreenLocked(bool locked)
{
    //先登记全局标志：控制器在之后收到的信令里据此决定静默（来电、群通话邀请）或自动拒绝（远程桌面请求）
    SetMediaScreenLocked(locked);
    if (!m_initialized)
        return;
    const bool recording = locked && m_record && m_record->IsRecording();
    LOG_INFO("media module: screen %s, recording %d.", locked ? "locked" : "unlocked", recording ? 1 : 0);
    if (m_call)
        m_call->OnScreenLockChanged(locked);
    if (m_rd)
        m_rd->OnScreenLockChanged(locked);
    if (m_share)
        m_share->OnScreenLockChanged(locked);
    if (!locked)
        return;
    //正在录屏的停止并照常保存（锁屏画面不应录进录像）
    if (recording)
        m_record->StopRecording();
    //语音消息外放会在无人值守时连续播放下一条，锁屏时停止
    if (m_voice)
        m_voice->Controller().StopPlay();
    //本 DLL 那份循环铃声（群通话来电、回铃）停止；一对一通话的铃声已由通话控制器停止，重复调用无害
    balloonmain::StopLoopSound();
}

void CMediaModule::OnCaptionAvailabilityChanged(bool available)
{
    if (!m_initialized)
        return;
    LOG_INFO("media module: caption availability %d.", available ? 1 : 0);
    CMediaCaptionBridge::Instance().SetAvailable(available);
}

bool CMediaModule::RejectIfInGroupCall()
{
    if (!m_groupCall || !m_groupCall->IsBusy())
        return false;
    LOG_INFO("media module: rejected because a group call is in progress.");
    HWND owner = (m_host != NULL) ? m_host->GetMainWindow() : NULL;
    ::MessageBoxW(owner, TR(L"你正在群通话中，请先结束群通话。"), TR(L"提示"), MB_OK | MB_ICONINFORMATION);
    return true;
}

int CMediaModule::RunDebugEntry(const wchar_t* commandLine, HWND hwndHost)
{
    //先查通话的 7 个入口，再查远程桌面的 8 个；两组开关互不包含对方的子串，先后顺序不影响命中
    int result = call::RunCallDebugEntry(commandLine, hwndHost, m_host);
    if (result != media_debug_not_handled)
    {
        return result;
    }
    //屏幕共享的调试入口（开关以 --share 开头，与前两组互不包含子串）
    result = screenshare::RunShareDebugEntry(commandLine, hwndHost, m_host);
    if (result != media_debug_not_handled || !m_rdDebug)
    {
        return result;
    }
    return m_rdDebug->Run(commandLine, hwndHost, m_host);
}

} // namespace flamingomedia
