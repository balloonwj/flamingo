/**
 *  ScreenshotModule.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ScreenshotModule.h"

#include "ShotLog.h"
#include "ShotHostServices.h"                                                // 截图界面经宿主查询、发起「识别文字」
#include "../Capture/ScreenCaptureOverlay.h"                                     // 截图界面（遮罩窗口）
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"                                  // i18n::InitFromHost：界面文字经宿主翻译
#include "../../Source/Common/I18n/BalloonUiText.h"                         // ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../../third-party/balloonui/balloonui/DuiTextResolver.h"       // SetTextResolver
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"              // SetDefaultFontFace

namespace flamingoshot {

namespace {

const uint32_t kLangCodeChars = 32;   // 语言代码缓冲区大小（字符数）
const wchar_t kAsciiLimit = 0x80;     // ASCII 的上界（不含）

// 宿主接口，供翻译函数使用（i18n::InitFromHost 只接受普通函数指针）
IScreenshotHost* volatile g_translateHost = NULL;

// 把查找转交宿主
const wchar_t* HostTranslate(const wchar_t* context, const wchar_t* source)
{
    IScreenshotHost* host = g_translateHost;
    return (host != NULL) ? host->Translate(context, source) : source;
}

// 只含 ASCII 的宽字符串（语言代码、字体名）转成窄字符串写日志；非 ASCII 字符写成 '?'
std::string AsciiForLog(const wchar_t* text)
{
    std::string out;
    for (const wchar_t* p = text; p != NULL && *p != L'\0'; ++p)
    {
        out.push_back((*p > 0 && *p < kAsciiLimit) ? static_cast<char>(*p) : '?');
    }
    return out;
}

} // namespace

CScreenshotModule& CScreenshotModule::Instance()
{
    static CScreenshotModule s_module;
    return s_module;
}

CScreenshotModule::CScreenshotModule()
    : m_host(NULL)
    , m_initialized(false)
{
}

CScreenshotModule::~CScreenshotModule()
{
}

bool CScreenshotModule::Init(IScreenshotHost* host, const ScreenshotInitParams* params)
{
    if (m_initialized)
    {
        return true;
    }
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(ScreenshotInitParams) || params->m_appDir == NULL)
    {
        return false;
    }
    m_host = host;
    ShotLogSetHost(host);
    ShotHostServicesSetHost(host);

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
        LOG_INFO("FlamingoScreenshot ui language %s, ui font %s.", AsciiForLog(language).c_str(),
                 AsciiForLog(uiFont).c_str());
    }
    else
    {
        LOG_WARNING("FlamingoScreenshot: host did not report the ui language, using built-in source text.");
    }

    m_initialized = true;
    LOG_INFO("FlamingoScreenshot initialized, api version %u.", static_cast<unsigned>(kApiVersion));
    return true;
}

void CScreenshotModule::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    LOG_INFO("FlamingoScreenshot shut down.");
    g_translateHost = NULL;
    ShotHostServicesSetHost(NULL);
    ShotLogSetHost(NULL);
    m_host = NULL;
    m_initialized = false;
}

uint32_t CScreenshotModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CScreenshotModule::IsRunning() const
{
    return balloonscreenshot::CScreenCaptureOverlay::IsRunning();
}

bool CScreenshotModule::Run(const ScreenshotRequest* request, IScreenshotSink* sink)
{
    if (!m_initialized || request == NULL || request->m_cbSize != sizeof(ScreenshotRequest))
    {
        LOG_WARNING("screenshot run rejected: initialized=%d, request=%p.", m_initialized ? 1 : 0, request);
        return false;
    }
    //同一时刻只允许一次截图：截图界面的局部消息循环里仍会派发宿主的快捷键消息，宿主可能再次调用本函数
    if (balloonscreenshot::CScreenCaptureOverlay::IsRunning())
    {
        LOG_INFO("screenshot run ignored: a capture is already in progress.");
        return false;
    }
    balloonscreenshot::CScreenCaptureOverlay overlay;
    return overlay.Run(*request, sink);
}

bool CScreenshotModule::IsInitialized() const
{
    return m_initialized;
}

IScreenshotModule* CreateScreenshotModule(uint32_t apiVersion)
{
    if (apiVersion != kApiVersion)
    {
        return NULL;
    }
    return &CScreenshotModule::Instance();
}

} // namespace flamingoshot
