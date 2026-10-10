/**
 *  主程序一侧锁屏宿主的实现，见 LockHost.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "LockHost.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/Util/IniFile.h"

namespace balloonlock {

namespace {

//写进 exe 日志文件时，DLL 日志行的「调用处」一栏：DLL 的行里已带有它自己的调用处，这里只标明来源模块
const char* const kLockLogSource = "FlamingoLock";
const wchar_t* const kLockSection = L"lock";          // flamingo.ini 里锁屏的节名
const wchar_t* const kAnimationKey = L"animation";    // 上次选择的锁屏画面
const DWORD kIniValueChars = 64;                       // 读配置值的缓冲区大小（字符数）
const wchar_t kMaxAsciiChar = 0x7F;                    // ASCII 的上界（不含）

} // namespace

CLockHost& CLockHost::GetInstance()
{
    static CLockHost s_instance;
    return s_instance;
}

CLockHost::CLockHost()
    : m_ready(false)
    , m_locked(false)
    , m_notify(NULL)
{
}

CLockHost::~CLockHost()
{
}

bool CLockHost::Load(const wchar_t* dllPath, const wchar_t* appDir, const wchar_t* iniPath)
{
    if (dllPath == NULL || appDir == NULL || iniPath == NULL)
    {
        return false;
    }
    if (m_ready)
    {
        return true;
    }
    m_iniPath = iniPath;
    balloonapp::PluginLoadResult result = m_loader.Load(dllPath, FLAMINGO_LOCK_FACTORY_NAME, flamingolock::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("lock module unavailable: %s, api version %u; the tray menu will not offer screen lock.",
                    balloonapp::CPluginLibrary::DescribeResult(result), static_cast<unsigned>(flamingolock::kApiVersion));
        return false;
    }
    flamingolock::LockInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("lock module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }
    m_ready = true;
    LOG_INFO("lock module loaded, api version %u.", static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

void CLockHost::SetNotifyWindow(HWND hwnd)
{
    m_notify.store(hwnd);
}

bool CLockHost::IsModuleReady() const
{
    return m_ready;
}

bool CLockHost::RequestLock()
{
    if (!m_ready)
    {
        return false;
    }
    return m_loader.GetModule()->RequestLock();
}

bool CLockHost::IsLocked() const
{
    return m_locked.load();
}

void CLockHost::Shutdown()
{
    if (!m_ready)
    {
        return;
    }
    m_loader.GetModule()->Shutdown();
    m_loader.Unload();
    m_ready = false;
    m_locked.store(false);
    m_notify.store(NULL);
    LOG_INFO("lock module unloaded.");
}

void CLockHost::Log(int level, const char* utf8)
{
    if (utf8 == NULL)
    {
        return;
    }
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingolock::lock_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case flamingolock::lock_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kLockLogSource, 0, "%s", utf8);
}

const wchar_t* CLockHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CLockHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
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

bool CLockHost::ReadAnimationChoice(char* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0)
    {
        return false;
    }
    buf[0] = '\0';
    if (m_iniPath.empty())
    {
        return false;
    }
    wchar_t value[kIniValueChars] = { 0 };
    CIniFile ini;
    ini.ReadString(kLockSection, kAnimationKey, L"", value, kIniValueChars, m_iniPath.c_str());
    //配置值只有 ASCII（如 "honor"），逐字符收窄；遇到非 ASCII 视为没有配置
    uint32_t n = 0;
    for (; value[n] != L'\0' && n + 1 < chars; ++n)
    {
        if (value[n] >= kMaxAsciiChar)
        {
            buf[0] = '\0';
            return false;
        }
        buf[n] = static_cast<char>(value[n]);
    }
    buf[n] = '\0';
    return n > 0;
}

void CLockHost::WriteAnimationChoice(const char* token)
{
    if (token == NULL || m_iniPath.empty())
    {
        return;
    }
    std::wstring value;
    for (const char* p = token; *p != '\0'; ++p)
    {
        value.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    CIniFile ini;
    ini.WriteString(kLockSection, kAnimationKey, value.c_str(), m_iniPath.c_str());
}

void CLockHost::OnLockStateChanged(bool locked)
{
    m_locked.store(locked);
    LOG_INFO("lock state changed: %s.", locked ? "locked" : "unlocked");
    HWND hwnd = m_notify.load();
    if (hwnd != NULL && ::IsWindow(hwnd))
    {
        ::PostMessage(hwnd, kMsgLockStateChanged, locked ? 1 : 0, 0);
    }
}

bool IsScreenLocked()
{
    return CLockHost::GetInstance().IsLocked();
}

} // namespace balloonlock
