/**
 *  ScreenshotHost.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ScreenshotHost.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "../Ocr/OcrLauncher.h"       // IsOcrEntryVisible / LaunchOcrForImage：「识别文字」交给 exe 的识别结果窗口
#include "../Ocr/OcrRecognizer.h"     // OcrBgraImage

namespace balloonscreenshot {

namespace {

const char* const kShotLogSource = "FlamingoScreenshot";   // DLL 转来的日志行在 exe 日志里的「调用处」一栏
const uint32_t kBytesPerPixel = 4;                         // 32 位 BGRA 每像素字节数

} // namespace

CScreenshotHost& CScreenshotHost::GetInstance()
{
    static CScreenshotHost s_instance;
    return s_instance;
}

CScreenshotHost::CScreenshotHost()
    : m_ready(false)
{
}

CScreenshotHost::~CScreenshotHost()
{
}

bool CScreenshotHost::Load(const wchar_t* dllPath, const wchar_t* appDir)
{
    if (dllPath == NULL || appDir == NULL)
    {
        return false;
    }
    if (m_ready)
    {
        return true;
    }
    balloonapp::PluginLoadResult result =
        m_loader.Load(dllPath, FLAMINGO_SCREENSHOT_FACTORY_NAME, flamingoshot::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("screenshot module unavailable: %s, api version %u; screenshot entries will be hidden.",
                    balloonapp::CPluginLibrary::DescribeResult(result), static_cast<unsigned>(flamingoshot::kApiVersion));
        return false;
    }
    flamingoshot::ScreenshotInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("screenshot module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }
    m_ready = true;
    LOG_INFO("screenshot module loaded, api version %u.", static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

bool CScreenshotHost::IsModuleReady() const
{
    return m_ready;
}

bool CScreenshotHost::IsRunning() const
{
    if (!m_ready)
    {
        return false;
    }
    return m_loader.GetModule()->IsRunning();
}

bool CScreenshotHost::Run(const flamingoshot::ScreenshotRequest& request, flamingoshot::IScreenshotSink* sink)
{
    if (!m_ready)
    {
        LOG_WARNING("screenshot requested but the screenshot module is not loaded.");
        return false;
    }
    return m_loader.GetModule()->Run(&request, sink);
}

void CScreenshotHost::Shutdown()
{
    if (!m_ready)
    {
        return;
    }
    m_loader.GetModule()->Shutdown();
    m_loader.Unload();
    m_ready = false;
    LOG_INFO("screenshot module unloaded.");
}

void CScreenshotHost::Log(int level, const char* utf8)
{
    if (utf8 == NULL)
    {
        return;
    }
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingoshot::shot_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case flamingoshot::shot_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kShotLogSource, 0, "%s", utf8);
}

const wchar_t* CScreenshotHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CScreenshotHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
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

bool CScreenshotHost::IsOcrAvailable()
{
    return ocr::IsOcrEntryVisible();
}

bool CScreenshotHost::LaunchOcrForImage(HWND owner, const uint8_t* bgra, uint32_t width, uint32_t height)
{
    if (bgra == NULL || width == 0 || height == 0)
    {
        LOG_WARNING("screenshot ocr: invalid image from the screenshot module (%ux%u).", width, height);
        return false;
    }
    //像素只在本次调用期间有效，拷进 exe 自己的缓冲再交给识别（识别在后台线程上进行）
    ocr::OcrBgraImage image;
    image.m_width = width;
    image.m_height = height;
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * kBytesPerPixel;
    image.m_pixels.assign(bgra, bgra + bytes);
    return ocr::LaunchOcrForImage(owner, image);
}

} // namespace balloonscreenshot
