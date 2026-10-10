/**
 *  IMediaHost 实现，见 MediaHostAdapter.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "MediaHostAdapter.h"
#include "FlamingoClient.h"
#include "../UI/BalloonMain/BalloonMainPanel.h"
#include "../Common/Log/IULog.h"
#include "../net/IUSocket.h"
#include "../Feature/ScreenShare/ScreenShareHub.h"
#include "../Feature/Voice/VoiceHub.h"   //语音消息：录音的两个回调转给语音消息中心
#include "../Feature/GroupCall/GroupCallHub.h"   //群通话：状态回报记进群通话中心
#include "../Common/I18n/I18n.h"   // i18n::Translate / GetLanguage / GetUiFontFace：宿主接口的多语言部分
#include "../Feature/P2P/P2PHost.h"   // CP2PHost::GetService：交给媒体 DLL 的 P2P 服务
#include "MediaRecordingBridge.h"   // 录像记录：宿主接口的四个录像记录方法转调到本地库
#include "../UI/BalloonHistory/RecordLocalStore.h"   // 录像记录本地库：按当前账号的个人目录打开
#include "../Feature/Lock/LockHost.h"   // IsScreenLocked：锁屏期间媒体 DLL 的提示音一律静音
#include "../Feature/Caption/CaptionHost.h"   // 语音字幕：可用状态与按需加载字幕 DLL
#include "../Feature/CloudRecord/CloudTransferRunner.h"   // 云端录像文件的上传与下载（第 27 期）

namespace balloonapp {

//DLL 写来的日志在日志行「函数签名」一栏显示的名字，便于在日志里按模块筛选
static const char* kMediaLogSource = "FlamingoMedia";

//客户端用户号是 32 位无符号数（UINT），超出该范围的用户号在 exe 里没有对应的联系人
static const int64_t kMaxClientUserId = 0xFFFFFFFFLL;

CMediaHostAdapter::CMediaHostAdapter()
    : m_pClient(NULL)
    , m_pPanel(NULL)
{
}

CMediaHostAdapter::~CMediaHostAdapter()
{
}

void CMediaHostAdapter::Attach(CFlamingoClient* pClient, CBalloonMainPanel* pPanel)
{
    m_pClient = pClient;
    m_pPanel = pPanel;
}

void CMediaHostAdapter::Log(int level, const char* text)
{
    if (text == NULL)
    {
        return;
    }
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingomedia::media_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;
    //DLL 报告的错误
    case flamingomedia::media_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;
    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kMediaLogSource, 0, "%s", text);
}

bool CMediaHostAdapter::SendSignal(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL || len == 0)
    {
        LOG_WARNING("media module asked to send signal cmd=%u with an empty body.", static_cast<unsigned>(cmd));
        return false;
    }
    return CIUSocket::GetInstance().SendMediaSignal(cmd, std::string(json, len));
}

const unsigned char* CMediaHostAdapter::GetServerPublicKey()
{
    return CIUSocket::GetInstance().GetServerPublicKey();
}

int64_t CMediaHostAdapter::GetSelfUserId()
{
    if (m_pClient == NULL)
    {
        return 0;
    }
    return static_cast<int64_t>(m_pClient->m_UserMgr.m_UserInfo.m_uUserID);
}

bool CMediaHostAdapter::IsSoundMuted()
{
    //锁屏期间不响提示音（docs/锁屏方案.md 第 4.9 节）；只影响铃声与提示音，通话里的实时声音不经过这里
    if (balloonlock::IsScreenLocked())
    {
        return true;
    }
    if (m_pClient == NULL)
    {
        return false;
    }
    return m_pClient->m_UserConfig.IsEnableMute() ? true : false;
}

void CMediaHostAdapter::AppendCallTip(int64_t peerUserId, const wchar_t* tip)
{
    if (m_pPanel == NULL || tip == NULL || peerUserId <= 0 || peerUserId > kMaxClientUserId)
    {
        return;
    }
    m_pPanel->AppendCallTipToChat(static_cast<UINT>(peerUserId), CString(tip));
}

HWND CMediaHostAdapter::GetMainWindow()
{
    if (m_pPanel == NULL || !m_pPanel->IsWindow())
    {
        return NULL;
    }
    return m_pPanel->m_hWnd;
}

bool CMediaHostAdapter::GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t bufChars)
{
    if (buf == NULL || bufChars == 0)
    {
        return false;
    }
    buf[0] = L'\0';
    if (m_pPanel == NULL || userId <= 0)
    {
        return false;
    }
    //主面板实现了文件共享宿主的取名接口，口径与会话列表一致，这里复用同一份
    balloonfileshare::IFileShareDirectory* directory = m_pPanel;
    const std::wstring name = directory->GetShareUserDisplayName(userId);
    wcsncpy_s(buf, bufChars, name.c_str(), _TRUNCATE);
    return true;
}

bool CMediaHostAdapter::SendFileToConversation(int ctype, int64_t cid, const wchar_t* path)
{
    if (m_pPanel == NULL || path == NULL)
    {
        return false;
    }
    return m_pPanel->SendLocalFileToConversation(ctype, cid, path);
}

bool CMediaHostAdapter::SendImageToConversation(int ctype, int64_t cid, const wchar_t* path)
{
    if (m_pPanel == NULL || path == NULL)
    {
        return false;
    }
    return m_pPanel->SendLocalImageToConversation(ctype, cid, path);
}

bool CMediaHostAdapter::PickConversationAndSendFile(const wchar_t* path)
{
    if (m_pPanel == NULL || path == NULL)
    {
        return false;
    }
    return m_pPanel->OpenSendFilePicker(path);
}

void CMediaHostAdapter::OnScreenShareStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId,
                                            const wchar_t* hostName, bool active, bool locked, bool forced,
                                            const wchar_t* title)
{
    LOG_INFO("screenshare status: ctype=%d, cid=%lld, room=%s, host=%lld, active=%d, locked=%d, forced=%d.", ctype,
             static_cast<long long>(cid), roomId != NULL ? roomId : "", static_cast<long long>(hostId),
             active ? 1 : 0, locked ? 1 : 0, forced ? 1 : 0);
    //先记进屏幕共享中心，再让已打开的聊天窗按新状态重填卡片
    balloonscreenshare::CScreenShareHub::GetInstance().OnStatus(ctype, cid, roomId, hostId, hostName, active, locked,
                                                                forced, title);
    if (m_pPanel != NULL)
    {
        m_pPanel->OnScreenShareStatusChanged(ctype, cid);
    }
}

const wchar_t* CMediaHostAdapter::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CMediaHostAdapter::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
{
    if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
    {
        return false;
    }
    //写不下时截断，结果始终以 '\0' 结尾
    wcsncpy_s(langBuf, langChars, i18n::GetLanguage().c_str(), _TRUNCATE);
    wcsncpy_s(fontBuf, fontChars, i18n::GetUiFontFace().c_str(), _TRUNCATE);
    return true;
}

void CMediaHostAdapter::SaveMouseEffectSettings(const char* settingsText)
{
    if (m_pClient == NULL)
    {
        return;
    }
    const char* text = (settingsText != NULL) ? settingsText : "";
    //设置文本只有 ASCII，按本机代码页转成宽字符不会出错
    const CString wide(text);
    m_pClient->m_UserConfig.SetMouseEffects(wide);
    m_pClient->SaveUserConfig();
    LOG_INFO("media host: mouse effects saved: %s.", text);
    //推回模块：共享与录屏用同一份设置
    balloonscreenshare::CScreenShareHub::GetInstance().SetMouseEffectSettings(wide);
}

void CMediaHostAdapter::OnVoiceRecordLevel(int level)
{
    //转给正在录音的录音条（模块的回调不带会话，由语音消息中心记住是哪个聊天窗在录）
    balloonvoice::CVoiceHub::GetInstance().OnRecordLevel(level);
}

void CMediaHostAdapter::OnVoiceRecordLimitReached()
{
    LOG_INFO("media host: voice record reached the time limit.");
    balloonvoice::CVoiceHub::GetInstance().OnRecordLimitReached();
}

void CMediaHostAdapter::OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs)
{
    //转给语音消息中心：它按编号核对后交给发起播放的聊天窗（模块的回调不带会话）
    balloonvoice::CVoiceHub::GetInstance().OnPlayProgress(token, playedMs, totalMs);
}

void CMediaHostAdapter::OnVoicePlayFinished(uint32_t token)
{
    LOG_INFO("media host: voice play finished, token %u.", token);
    balloonvoice::CVoiceHub::GetInstance().OnPlayFinished(token);
}

p2p::IP2PService* CMediaHostAdapter::GetP2PService()
{
    //P2P 模块在程序启动时先于媒体模块加载、在退出时最后卸载，交出去的服务对象在媒体模块 Shutdown 之前一直有效
    return balloonp2p::CP2PHost::GetInstance().GetService();
}

void CMediaHostAdapter::OnGroupCallStatus(int64_t groupId, const char* roomId, bool active, int media, int count,
                                          int64_t initiatorId, int64_t startTime)
{
    LOG_INFO("groupcall status: group=%lld, room=%s, active=%d, media=%d, count=%d, initiator=%lld.",
             static_cast<long long>(groupId), roomId != NULL ? roomId : "", active ? 1 : 0, media, count,
             static_cast<long long>(initiatorId));
    //先记进群通话中心，再让已打开的群聊窗刷新横幅与通话卡片
    balloongroupcall::CGroupCallHub::GetInstance().OnStatus(groupId, roomId, active, media, count, initiatorId,
                                                            startTime);
    if (m_pPanel != NULL)
    {
        m_pPanel->OnGroupCallStatusChanged(groupId);
    }
}

void CMediaHostAdapter::OnGroupCallCardChanged(int64_t groupId, int64_t msgId, const char* contentJson)
{
    LOG_INFO("groupcall card changed: group=%lld, msgid=%lld, bytes=%u.", static_cast<long long>(groupId),
             static_cast<long long>(msgId), contentJson != NULL ? static_cast<unsigned>(strlen(contentJson)) : 0u);
    if (m_pPanel == NULL || contentJson == NULL)
    {
        return;
    }
    m_pPanel->ApplyGroupCallCardChanged(groupId, msgId, std::string(contentJson));
}

void CMediaHostAdapter::RequestGroupCallInvitees(int64_t groupId, const int64_t* excludeIds, uint32_t excludeCount)
{
    LOG_INFO("groupcall invitees requested: group=%lld, exclude=%u.", static_cast<long long>(groupId), excludeCount);
    if (m_pPanel == NULL)
    {
        return;
    }
    std::vector<int64_t> exclude;
    if (excludeIds != NULL)
    {
        exclude.assign(excludeIds, excludeIds + excludeCount);
    }
    m_pPanel->OpenGroupCallInvitePicker(groupId, exclude);
}

int CMediaHostAdapter::GetGroupMemberRole(int64_t groupId, int64_t userId)
{
    if (m_pPanel == NULL)
    {
        return -1;
    }
    return m_pPanel->GetGroupMemberRole(groupId, userId);
}

//把头像描述（字形、底色、照片路径）填进 DLL 传来的结构体；结构体大小不符时不填
static bool FillMediaAvatar(const balloonfileshare::ShareUserAvatar& avatar, flamingomedia::MediaAvatarInfo* out)
{
    if (out == NULL || out->m_cbSize != sizeof(flamingomedia::MediaAvatarInfo))
    {
        return false;
    }
    wcsncpy_s(out->m_glyph, flamingomedia::kMediaAvatarGlyphChars, avatar.m_glyph.c_str(), _TRUNCATE);
    out->m_color = static_cast<uint32_t>(avatar.m_color);
    wcsncpy_s(out->m_imagePath, flamingomedia::kMediaAvatarPathChars, avatar.m_imagePath.c_str(), _TRUNCATE);
    return true;
}

bool CMediaHostAdapter::GetUserAvatar(int64_t userId, flamingomedia::MediaAvatarInfo* out)
{
    if (m_pPanel == NULL || userId <= 0)
    {
        return false;
    }
    return FillMediaAvatar(m_pPanel->GetShareUserAvatar(userId), out);
}

bool CMediaHostAdapter::GetGroupAvatar(int64_t groupId, flamingomedia::MediaAvatarInfo* out)
{
    if (m_pPanel == NULL || groupId <= 0)
    {
        return false;
    }
    balloonfileshare::ShareUserAvatar avatar;
    if (!m_pPanel->GetGroupAvatarForMedia(groupId, avatar))
    {
        return false;
    }
    return FillMediaAvatar(avatar, out);
}

void CMediaHostAdapter::OnCallPresence(int ctype, int64_t cid, int media, bool inCall)
{
    LOG_INFO("call presence: ctype=%d, cid=%lld, media=%d, incall=%d.", ctype, static_cast<long long>(cid), media,
             inCall ? 1 : 0);
    if (m_pPanel != NULL)
    {
        m_pPanel->OnCallPresence(ctype, cid, media, inCall);
    }
}

void CMediaHostAdapter::ensureRecordStore()
{
    if (m_pClient == NULL)
    {
        return;
    }
    const std::wstring personal = m_pClient->m_UserMgr.GetPersonalFolder();
    const std::wstring dbPath = balloonhistory::CRecordLocalStore::DbPathIn(personal);
    if (dbPath.empty())
    {
        return;
    }
    //ChatLog 目录通常已由消息库建好；已存在时这一步失败也无妨
    ::CreateDirectoryW((personal + L"ChatLog").c_str(), NULL);
    if (!balloonhistory::CRecordLocalStore::GetInstance().Open(dbPath.c_str()))
    {
        LOG_ERROR("recording record store open failed.");
    }
}

bool CMediaHostAdapter::AddRecording(const flamingomedia::MediaRecordingInfo* info)
{
    ensureRecordStore();
    const bool ok = AddRecordingToStore(info);
    LOG_INFO("recording record added: ok=%d, duration=%lld ms.", ok ? 1 : 0,
             (info != NULL) ? static_cast<long long>(info->m_durationMs) : 0LL);
    return ok;
}

uint32_t CMediaHostAdapter::ListRecordings(flamingomedia::MediaRecordingInfo* out, uint32_t capacity)
{
    ensureRecordStore();
    return ListRecordingsFromStore(out, capacity);
}

bool CMediaHostAdapter::UpdateRecordingPath(int64_t id, const wchar_t* newPath)
{
    ensureRecordStore();
    const bool ok = UpdateRecordingPathInStore(id, newPath);
    LOG_INFO("recording record path updated: id=%lld, ok=%d.", static_cast<long long>(id), ok ? 1 : 0);
    return ok;
}

bool CMediaHostAdapter::RemoveRecording(int64_t id)
{
    ensureRecordStore();
    const bool ok = RemoveRecordingFromStore(id);
    LOG_INFO("recording record removed: id=%lld, ok=%d.", static_cast<long long>(id), ok ? 1 : 0);
    return ok;
}

bool CMediaHostAdapter::IsCaptionAvailable()
{
    return ballooncaption::CCaptionHost::GetInstance().IsAvailable();
}

caption::ICaptionModule* CMediaHostAdapter::GetCaptionService()
{
    return ballooncaption::CCaptionHost::GetInstance().AcquireModule();
}

bool CMediaHostAdapter::PickConversationsAndSendText(const wchar_t* text)
{
    if (m_pPanel == NULL || text == NULL || text[0] == L'\0')
    {
        return false;
    }
    return m_pPanel->OpenSendTextPicker(text);
}

bool CMediaHostAdapter::IsCloudRecordEnabled()
{
    return m_pClient != NULL && m_pClient->m_UserMgr.m_bCloudRecordFeature;
}

uint32_t CMediaHostAdapter::StartCloudTransfer(const flamingomedia::MediaCloudTransfer* req)
{
    if (req == NULL)
    {
        return 0;
    }
    return ballooncloud::CCloudTransferRunner::GetInstance().Start(*req);
}

void CMediaHostAdapter::CancelCloudTransfer(uint32_t id)
{
    ballooncloud::CCloudTransferRunner::GetInstance().Cancel(id);
}

bool CMediaHostAdapter::PickConversations(const wchar_t* title, uint32_t token)
{
    if (m_pPanel == NULL)
    {
        return false;
    }
    return m_pPanel->OpenMediaConversationPicker(title != NULL ? std::wstring(title) : std::wstring(), token);
}

} // namespace balloonapp
