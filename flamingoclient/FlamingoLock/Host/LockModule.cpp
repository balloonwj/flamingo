/**
 *  LockModule.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockModule.h"

#include "LockLog.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"                                  // i18n::InitFromHost：界面文字经宿主翻译
#include "../../Source/Common/I18n/BalloonUiText.h"                         // ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../../third-party/balloonui/balloonui/DuiTextResolver.h"       // SetTextResolver
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"              // SetDefaultFontFace

namespace flamingolock {

namespace {

const uint32_t kLangCodeChars = 32;   // 语言代码缓冲区大小（字符数）

// 宿主接口，供翻译函数使用（i18n::InitFromHost 只接受普通函数指针）
ILockHost* volatile g_translateHost = NULL;

// 把查找转交宿主
const wchar_t* HostTranslate(const wchar_t* context, const wchar_t* source)
{
    ILockHost* host = g_translateHost;
    return (host != NULL) ? host->Translate(context, source) : source;
}

// 只含 ASCII 的宽字符串（语言代码、字体名）转成窄字符串写日志；非 ASCII 字符写成 '?'
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

CLockModule& CLockModule::Instance()
{
    static CLockModule s_module;
    return s_module;
}

CLockModule::CLockModule()
    : m_host(NULL)
    , m_initialized(false)
{
}

CLockModule::~CLockModule()
{
}

bool CLockModule::Init(ILockHost* host, const LockInitParams* params)
{
    if (m_initialized)
    {
        return true;
    }
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(LockInitParams) || params->m_appDir == NULL)
    {
        return false;
    }
    m_host = host;
    LockLogSetHost(host);

    //多语言：界面文字经宿主的语言模块翻译，界面字体取宿主当前生效的字体；须在创建任何窗口之前
    wchar_t language[kLangCodeChars] = { 0 };
    wchar_t uiFont[LF_FACESIZE] = { 0 };
    g_translateHost = host;
    if (host->GetUiLanguage(language, kLangCodeChars, uiFont, LF_FACESIZE))
    {
        i18n::InitFromHost(&HostTranslate, language, uiFont);
        //本 DLL 静态链接了自己的一份 balloonui，它的默认字体与内置文字也要跟随界面语言
        balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
        balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);
        LOG_INFO("FlamingoLock ui language %s, ui font %s.", AsciiForLog(language).c_str(), AsciiForLog(uiFont).c_str());
    }
    else
    {
        LOG_WARNING("FlamingoLock: host did not report the ui language, using built-in source text.");
    }

    m_thread.SetHost(host);
    m_initialized = true;
    LOG_INFO("FlamingoLock initialized, api version %u.", static_cast<unsigned>(kApiVersion));
    return true;
}

void CLockModule::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    m_thread.Stop();
    LOG_INFO("FlamingoLock shut down.");
    g_translateHost = NULL;
    LockLogSetHost(NULL);
    m_host = NULL;
    m_initialized = false;
}

uint32_t CLockModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CLockModule::RequestLock()
{
    if (!m_initialized)
    {
        return false;
    }
    const bool started = m_thread.RequestLock();
    LOG_INFO("lock requested by host, %s.", started ? "opening the setup dialog" : "already busy");
    return started;
}

bool CLockModule::IsLocked() const
{
    return m_thread.IsLocked();
}

} // namespace flamingolock
