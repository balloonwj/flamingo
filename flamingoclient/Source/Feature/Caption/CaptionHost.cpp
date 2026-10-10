/**
 *  主程序一侧的语音字幕接入，见 CaptionHost.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionHost.h"

#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"

extern TCHAR g_szHomePath[MAX_PATH];

namespace ballooncaption {

namespace {

//字幕 DLL 日志行的函数签名一栏（与 FlamingoMedia 的日志区分开）
const char* const kCaptionLogSource = "FlamingoCaption";

} // namespace

CCaptionHost& CCaptionHost::GetInstance()
{
    static CCaptionHost s_instance;
    return s_instance;
}

CCaptionHost::CCaptionHost()
    : m_initialized(false)
    , m_notifiedValue(false)
    , m_mediaModule(NULL)
    , m_mediaHost(NULL)
{
}

CCaptionHost::~CCaptionHost()
{
}

void CCaptionHost::SetMediaModule(flamingomedia::IMediaModule* module)
{
    m_mediaModule = module;
    if (m_mediaModule != NULL)
        NotifyAvailability(true);
}

void CCaptionHost::SetMediaHost(flamingomedia::IMediaHost* host)
{
    m_mediaHost = host;
}

void CCaptionHost::OnLoginSucceeded(bool featureEnabled, const std::wstring& personalDir)
{
    m_state.m_loggedIn = true;
    m_state.m_featureEnabled = featureEnabled;
    m_settingsPath = captionhost::SettingsFilePath(personalDir);
    m_settingsJson.clear();
    const bool read = captionhost::ReadSettingsFile(m_settingsPath, m_settingsJson);
    LOG_INFO("caption host: login, feature=%d, settings %s (%u bytes), available=%d.", featureEnabled ? 1 : 0,
             read ? "loaded" : "not found", static_cast<unsigned>(m_settingsJson.size()), IsAvailable() ? 1 : 0);
    //DLL 已加载（重新登录或切换账号）时换成新账号的设置；空串表示按默认设置
    if (m_initialized)
        m_loader.GetModule()->SetSettings(m_settingsJson.c_str());
    NotifyAvailability(false);
}

bool CCaptionHost::OnFeaturesChanged(bool enabled)
{
    const bool before = IsAvailable();
    m_state.m_featureEnabled = enabled;
    const bool after = IsAvailable();
    LOG_INFO("caption host: feature changed to %d, available %d -> %d.", enabled ? 1 : 0, before ? 1 : 0, after ? 1 : 0);
    NotifyAvailability(false);
    return before != after;
}

void CCaptionHost::OnLoggingOut()
{
    m_state.m_loggedIn = false;
    m_settingsPath.clear();
    m_settingsJson.clear();
    LOG_INFO("caption host: logging out, caption unavailable.");
    NotifyAvailability(false);
}

void CCaptionHost::Shutdown()
{
    if (m_initialized)
    {
        m_loader.GetModule()->Shutdown();
        m_initialized = false;
    }
    if (m_loader.IsLoaded())
    {
        m_loader.Unload();
        LOG_INFO("caption host: module unloaded.");
    }
    m_mediaModule = NULL;
    m_mediaHost = NULL;
}

bool CCaptionHost::IsAvailable() const
{
    return captionhost::IsCaptionAvailable(m_state);
}

caption::ICaptionModule* CCaptionHost::AcquireModule()
{
    if (!IsAvailable())
        return NULL;
    if (!captionhost::ShouldLoadCaptionDll(m_state, m_initialized))
        return m_loader.GetModule();

    //g_szHomePath 是程序目录，以反斜杠结尾；两个字幕 DLL 与 exe 放在同一目录
    std::wstring dllPath(g_szHomePath);
    dllPath += FLAMINGO_CAPTION_DLL_NAME;
    const balloonapp::PluginLoadResult result =
        m_loader.Load(dllPath.c_str(), FLAMINGO_CAPTION_FACTORY_NAME, caption::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("caption host: module unavailable: %s, api version %u.",
                    balloonapp::CPluginLibrary::DescribeResult(result), caption::kApiVersion);
        m_state.m_loadFailed = true;
        NotifyAvailability(false);
        return NULL;
    }

    //初始化参数里的程序目录不带结尾反斜杠
    std::wstring appDir(g_szHomePath);
    while (!appDir.empty() && appDir[appDir.size() - 1] == L'\\')
        appDir.erase(appDir.size() - 1);
    caption::CaptionInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir.c_str();
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("caption host: module loaded but Init failed, unloading it.");
        m_loader.Unload();
        m_state.m_loadFailed = true;
        NotifyAvailability(false);
        return NULL;
    }
    m_initialized = true;
    m_loader.GetModule()->SetSettings(m_settingsJson.c_str());
    LOG_INFO("caption host: module loaded, api version %u.", m_loader.GetModule()->GetApiVersion());
    return m_loader.GetModule();
}

void CCaptionHost::NotifyAvailability(bool force)
{
    const bool available = IsAvailable();
    if (!force && available == m_notifiedValue)
        return;
    m_notifiedValue = available;
    if (m_mediaModule == NULL)
        return;
    LOG_INFO("caption host: notify media module, caption available=%d.", available ? 1 : 0);
    m_mediaModule->OnCaptionAvailabilityChanged(available);
}

void CCaptionHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //警告：可以继续运行的异常情况
    case caption::caption_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;
    //错误：功能无法继续
    case caption::caption_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;
    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kCaptionLogSource, 0, "%s", text);
}

const wchar_t* CCaptionHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CCaptionHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
{
    if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
        return false;
    //写不下时截断，结果始终以 '\0' 结尾
    wcsncpy_s(langBuf, langChars, i18n::GetLanguage().c_str(), _TRUNCATE);
    wcsncpy_s(fontBuf, fontChars, i18n::GetUiFontFace().c_str(), _TRUNCATE);
    return true;
}

void CCaptionHost::SaveSettings(const char* settingsJson)
{
    m_settingsJson = (settingsJson != NULL) ? settingsJson : "";
    if (m_settingsPath.empty())
    {
        LOG_WARNING("caption host: settings changed while logged out, not saved.");
        return;
    }
    const bool ok = captionhost::WriteSettingsFile(m_settingsPath, m_settingsJson);
    if (ok)
        LOG_INFO("caption host: settings saved, %u bytes.", static_cast<unsigned>(m_settingsJson.size()));
    else
        LOG_ERROR("caption host: save settings failed, error=%u.", static_cast<unsigned>(::GetLastError()));
}

bool CCaptionHost::PickConversationAndSendFile(const wchar_t* path)
{
    if (m_mediaHost == NULL)
        return false;
    return m_mediaHost->PickConversationAndSendFile(path);
}

HWND CCaptionHost::GetMainWindow()
{
    if (m_mediaHost == NULL)
        return NULL;
    return m_mediaHost->GetMainWindow();
}

} // namespace ballooncaption
