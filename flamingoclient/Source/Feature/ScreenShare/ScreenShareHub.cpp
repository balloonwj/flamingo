/**
 *  ScreenShareHub.cpp —— exe 侧的屏幕共享中心，见 ScreenShareHub.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ScreenShareHub.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"   // TR：凭分享链接加入时的提示文字（第 25 期）
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonscreenshare {

CScreenShareHub& CScreenShareHub::GetInstance()
{
    static CScreenShareHub s_instance;
    return s_instance;
}

CScreenShareHub::CScreenShareHub()
    : m_module(NULL)
{
}

void CScreenShareHub::AttachModule(flamingomedia::IMediaModule* module)
{
    m_module = module;
}

bool CScreenShareHub::IsModuleReady() const
{
    return m_module != NULL;
}

bool CScreenShareHub::StartShare(int ctype, int64_t cid, const CString& conversationName)
{
    if (m_module == NULL)
    {
        LOG_WARNING("screenshare start rejected: media module not loaded, ctype=%d, cid=%lld.", ctype, (long long)cid);
        return false;
    }
    const bool ok = m_module->StartScreenShare(ctype, cid, conversationName.GetString());
    LOG_INFO("screenshare start requested, ctype=%d, cid=%lld, ok=%d.", ctype, (long long)cid, ok ? 1 : 0);
    return ok;
}

bool CScreenShareHub::StartRecording(int ctype, int64_t cid, const CString& conversationName)
{
    if (m_module == NULL)
    {
        LOG_WARNING("record start rejected: media module not loaded.");
        return false;
    }
    const bool ok = m_module->StartRecording(ctype, cid, conversationName.GetString());
    LOG_INFO("record start requested, ctype=%d, cid=%lld, ok=%d.", ctype, (long long)cid, ok ? 1 : 0);
    return ok;
}

void CScreenShareHub::StopRecording()
{
    if (m_module != NULL)
        m_module->StopRecording();
}

void CScreenShareHub::ShowRecordManager()
{
    if (m_module != NULL)
        m_module->ShowRecordManager();
}

bool CScreenShareHub::IsRecording() const
{
    return m_module != NULL && m_module->IsRecording();
}

bool CScreenShareHub::IsRecordSupported() const
{
    return m_module != NULL && m_module->IsFeatureSupported(flamingomedia::media_feature_screen_record);
}

void CScreenShareHub::SetRecordSettings(int quality, const CString& folder)
{
    if (m_module != NULL)
        m_module->SetRecordSettings(quality, folder.GetString());
}

void CScreenShareHub::SetMouseEffectSettings(const CString& text)
{
    if (m_module == NULL)
        return;
    //设置文本只有 ASCII，按本机代码页转回窄字符不会出错
    const std::string narrow = std::string(CT2A(text.GetString()));
    m_module->SetMouseEffectSettings(narrow.c_str());
}

bool CScreenShareHub::JoinShare(int ctype, int64_t cid, const std::string& roomId, const CString& hostName,
                                bool forced)
{
    if (m_module == NULL || roomId.empty())
    {
        LOG_WARNING("screenshare join rejected: module not loaded or empty room, ctype=%d, cid=%lld.", ctype,
                    (long long)cid);
        return false;
    }
    const bool ok = m_module->JoinScreenShare(ctype, cid, roomId.c_str(), hostName.GetString(), forced);
    LOG_INFO("screenshare join requested, ctype=%d, cid=%lld, room=%s, forced=%d, ok=%d.", ctype, (long long)cid,
             roomId.c_str(), forced ? 1 : 0, ok ? 1 : 0);
    return ok;
}

bool CScreenShareHub::JoinByLink(const std::wstring& url)
{
    if (m_module == NULL)
    {
        LOG_WARNING("screenshare join by link rejected: media module not loaded.");
        ::MessageBoxW(NULL, TR(L"屏幕共享组件没有加载，无法加入。"), TR(L"屏幕共享"), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    //日志不记链接本身：持有链接码即可加入
    const bool ok = m_module->JoinScreenShareByLink(url.c_str());
    LOG_INFO("screenshare join by link requested, ok=%d.", ok ? 1 : 0);
    return ok;
}

bool CScreenShareHub::OpenCloudRecord(HWND owner, int64_t recordId, const std::wstring& title)
{
    if (recordId <= 0)
        return false;
    if (m_module == NULL)
    {
        LOG_WARNING("cloud record open rejected: media module not loaded, record=%lld.", (long long)recordId);
        ::MessageBoxW(owner, TR(L"录制组件没有加载，无法播放云端录像。"), TR(L"云端录像"), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    m_module->OpenCloudRecord(recordId, title.c_str());
    LOG_INFO("cloud record open requested, record=%lld.", (long long)recordId);
    return true;
}

void CScreenShareHub::Query(int ctype, int64_t cid)
{
    if (m_module == NULL || cid == 0)
        return;
    m_module->QueryScreenShare(ctype, cid);
}

void CScreenShareHub::OnStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId, const wchar_t* hostName,
                               bool active, bool locked, bool forced, const wchar_t* title)
{
    m_cache.Update(ctype, cid, roomId != NULL ? std::string(roomId) : std::string(), hostId,
                   hostName != NULL ? std::wstring(hostName) : std::wstring(), active, locked, forced,
                   title != NULL ? std::wstring(title) : std::wstring());
}

int CScreenShareHub::RoomState(int ctype, int64_t cid, const std::string& roomId) const
{
    return m_cache.RoomState(ctype, cid, roomId);
}

bool CScreenShareHub::GetActive(int ctype, int64_t cid, ScreenShareActiveInfo& out) const
{
    return m_cache.GetActive(ctype, cid, out);
}

bool CScreenShareHub::ApplyInfo(int ctype, int64_t cid, const std::string& roomId, const std::wstring& title,
                                int64_t hostId, const std::wstring& hostName)
{
    return m_cache.ApplyInfo(ctype, cid, roomId, title, hostId, hostName);
}

void CScreenShareHub::ClearStatus()
{
    m_cache.Clear();
}

} // namespace balloonscreenshare
