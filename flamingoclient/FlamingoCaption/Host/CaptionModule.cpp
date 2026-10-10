/**
 *  FlamingoCaption.dll 的模块对象，实现见 CaptionModule.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionModule.h"

#include <objidl.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <string.h>

#include "../../Source/Common/I18n/I18n.h"
#include "../../Source/Common/Log/IULog.h"
#include "../Engine/CaptionEngine.h"
#include "../Engine/CaptionWorker.h"
#include "../Model/CaptionFiles.h"
#include "../Record/CaptionRecording.h"
#include "../Render/CaptionRenderer.h"
#include "../Editor/CaptionEditorWindow.h"
#include "../../Source/Common/I18n/BalloonUiText.h"   //ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../../third-party/balloonui/balloonui/DuiTextResolver.h"   //SetTextResolver
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"   //SetDefaultFontFace：本 DLL 那份 balloonui 的默认字体
#include "CaptionLog.h"

#pragma comment(lib, "mfplat.lib")

namespace caption {

namespace {

//取界面语言与字体时缓冲区的字符数
const uint32_t kLangChars = 32;
const uint32_t kFontChars = 64;
//观看窗叠加图层里字幕底边离图层底边的距离与字号之比的倒数（字号的三分之一）
const int kOverlayMarginDivisor = 3;

//状态位与接口枚举一致（两处定义，编译期核对）
static_assert(kCaptionFileDraft == caption_file_draft && kCaptionFileHard == caption_file_hard
              && kCaptionFileTrack == caption_file_track && kCaptionFileSrt == caption_file_srt,
              "CaptionFiles.h status bits must match CaptionFileStatus in CaptionApi.h");

} // namespace

CCaptionModule& CCaptionModule::Instance()
{
    static CCaptionModule instance;
    return instance;
}

CCaptionModule::CCaptionModule()
    : m_host(NULL)
    , m_gdiplusToken(0)
    , m_inited(false)
    , m_mfStarted(false)
{
}

uint32_t CCaptionModule::GetApiVersion() const
{
    return kApiVersion;
}

const wchar_t* CCaptionModule::TranslateThunk(const wchar_t* context, const wchar_t* source)
{
    ICaptionHost* host = Instance().m_host;
    if (host == NULL)
        return source;
    return host->Translate(context, source);
}

bool CCaptionModule::Init(ICaptionHost* host, const CaptionInitParams* params)
{
    if (m_inited)
        return true;
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(CaptionInitParams) || params->m_appDir == NULL)
        return false;
    m_host = host;
    m_appDir = params->m_appDir;
    CaptionLogSetHost(host);

    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gsi, NULL) != Gdiplus::Ok)
        m_gdiplusToken = 0;

    wchar_t lang[kLangChars] = { 0 };
    wchar_t font[kFontChars] = { 0 };
    host->GetUiLanguage(lang, kLangChars, font, kFontChars);
    i18n::InitFromHost(&CCaptionModule::TranslateThunk, lang, font);
    //本 DLL 静态链接了自己的一份 balloonui：默认字体随界面语言，内置文字（右键菜单等）同样经宿主翻译
    balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
    balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);
    //字幕编辑器的预览（媒体引擎）与导出用 Media Foundation；MFStartup 按调用次数计数，与 FlamingoMedia 的互不影响
    m_mfStarted = SUCCEEDED(MFStartup(MF_VERSION));

    CaptionEngine::Instance().SetModelPath(m_appDir + L"\\" + FLAMINGO_CAPTION_MODEL_DLL_NAME);
    m_inited = true;
    LOG_INFO("caption: module initialized, engine %s.", CaptionEngine::HasEngine() ? "built in" : "not built");
    return true;
}

void CCaptionModule::Shutdown()
{
    if (!m_inited)
        return;
    //先关闭全部编辑器（各自保存草稿、停掉预览与事后识别），再停识别线程
    for (size_t i = 0; i < m_editors.size(); ++i)
    {
        m_editors[i]->CloseEditor();
        delete m_editors[i];
    }
    m_editors.clear();
    CaptionWorker::Instance().Shutdown();
    CaptionEngine::Instance().Shutdown();
    if (m_gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    if (m_mfStarted)
    {
        MFShutdown();
        m_mfStarted = false;
    }
    LOG_INFO("caption: module shut down.");
    CaptionLogSetHost(NULL);
    m_host = NULL;
    m_inited = false;
}

void CCaptionModule::SetSettings(const char* settingsJson)
{
    CaptionSettings s;
    if (!s.FromJson(settingsJson != NULL ? settingsJson : ""))
        LOG_WARNING("caption: settings json is not valid, defaults used.");
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_settings = s;
    }
    CaptionWorker::Instance().SetReplaceTable(s.m_replace);
}

CaptionSettings CCaptionModule::GetSettings()
{
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    return m_settings;
}

void CCaptionModule::UpdateSettings(const CaptionSettings& settings)
{
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_settings = settings;
    }
    CaptionWorker::Instance().SetReplaceTable(settings.m_replace);
    if (m_host != NULL)
        m_host->SaveSettings(settings.ToJson().c_str());
}

namespace {

//偏好在设置结构里对应的成员；不认识的取值返回 NULL
bool* PreferenceField(CaptionSettings& s, int pref)
{
    switch (pref)
    {
    //录制选项「生成字幕」
    case caption_pref_record_caption:
        return &s.m_recordMic;
    //原录制选项「为电脑声音生成字幕」：2026-10-06 起录屏不再使用（「生成字幕」同时识别电脑声音），读写照常保留
    case caption_pref_record_system:
        return &s.m_recordSystem;
    //屏幕共享「为我的发言生成字幕」
    case caption_pref_live_self:
        return &s.m_liveSelf;
    //观看窗显示别人的字幕
    case caption_pref_live_show:
        return &s.m_liveShow;
    //不认识的偏好
    default:
        return NULL;
    }
}

} // namespace

bool CCaptionModule::GetPreference(int pref)
{
    CaptionSettings s = GetSettings();
    const bool* field = PreferenceField(s, pref);
    return field != NULL && *field;
}

void CCaptionModule::SetPreference(int pref, bool value)
{
    CaptionSettings s = GetSettings();
    bool* field = PreferenceField(s, pref);
    if (field == NULL || *field == value)
        return;
    *field = value;
    LOG_INFO("caption: preference %d set to %d.", pref, value ? 1 : 0);
    UpdateSettings(s);
}

ICaptionHost* CCaptionModule::Host() const
{
    return m_host;
}

const std::wstring& CCaptionModule::AppDir() const
{
    return m_appDir;
}

void CCaptionModule::Preload()
{
    if (m_inited)
        CaptionEngine::Instance().Preload();
}

int CCaptionModule::GetEngineState()
{
    return CaptionEngine::Instance().State();
}

ICaptionStream* CCaptionModule::CreateStream(int purpose, ICaptionStreamListener* listener)
{
    if (!m_inited || listener == NULL)
        return NULL;
    return CaptionWorker::Instance().CreateStream(purpose, listener);
}

ICaptionRecording* CCaptionModule::BeginRecording(const CaptionRecordingParams* params)
{
    if (!m_inited)
        return NULL;
    CaptionReplaceTable replace;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        replace = m_settings.m_replace;
    }
    return CaptionRecording::Create(params, replace);
}

bool CCaptionModule::OpenEditor(HWND owner, const wchar_t* videoPath)
{
    if (!m_inited || videoPath == NULL || videoPath[0] == 0)
        return false;
    //先清掉已经关闭的窗口
    for (size_t i = 0; i < m_editors.size();)
    {
        if (!m_editors[i]->IsOpen())
        {
            delete m_editors[i];
            m_editors.erase(m_editors.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    //同一个录像已经开着：激活它（路径不区分大小写）
    for (size_t i = 0; i < m_editors.size(); ++i)
    {
        if (_wcsicmp(m_editors[i]->VideoPath().c_str(), videoPath) == 0)
        {
            m_editors[i]->Activate();
            LOG_INFO("caption: editor already open, activated.");
            return true;
        }
    }
    CaptionEditorWindow* editor = new CaptionEditorWindow();
    if (!editor->Open(owner, videoPath))
    {
        delete editor;
        return false;
    }
    m_editors.push_back(editor);
    return true;
}

int CCaptionModule::GetFileStatus(const wchar_t* videoPath)
{
    if (videoPath == NULL || videoPath[0] == 0)
        return caption_file_none;
    return QueryCaptionFileStatus(videoPath);
}

void CCaptionModule::OnVideoRenamed(const wchar_t* oldPath, const wchar_t* newPath)
{
    if (oldPath == NULL || newPath == NULL)
        return;
    if (RenameCaptionDraft(oldPath, newPath))
        LOG_INFO("caption: draft renamed together with its video.");
}

void CCaptionModule::OnVideoDeleted(const wchar_t* videoPath)
{
    if (videoPath == NULL)
        return;
    if (DeleteCaptionDraft(videoPath))
        LOG_INFO("caption: draft deleted together with its video.");
}

bool CCaptionModule::RenderOverlay(const CaptionOverlayLine* lines, uint32_t count, uint32_t width, uint32_t height,
                                   uint32_t* bgra, int fontPx)
{
    if (lines == NULL || bgra == NULL || width == 0 || height == 0 || fontPx <= 0)
        return false;
    memset(bgra, 0, static_cast<size_t>(width) * height * sizeof(uint32_t));
    std::vector<CaptionPaintLine> paint;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (lines[i].m_text == NULL)
            continue;
        CaptionPaintLine l;
        l.m_speaker = lines[i].m_speaker != NULL ? lines[i].m_speaker : L"";
        l.m_text = lines[i].m_text;
        l.m_partial = lines[i].m_partial;
        paint.push_back(l);
    }
    CaptionPaintParams p;
    p.m_width = static_cast<int>(width);
    p.m_height = static_cast<int>(height);
    p.m_refHeight = kCaptionStyleBaseHeight;
    p.m_style.m_fontPx = fontPx;
    p.m_style.m_bottomMarginPx = fontPx / kOverlayMarginDivisor;
    p.m_style.m_outlinePx = p.m_style.m_outlinePx * fontPx / CaptionStyle().m_fontPx;
    return CaptionRenderer::Paint(paint, p, bgra, NULL);
}

} // namespace caption
