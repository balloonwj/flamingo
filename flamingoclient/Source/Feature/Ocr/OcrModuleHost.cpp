/**
 *  图片文字识别模块宿主的实现，说明见 OcrModuleHost.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrModuleHost.h"

#include <objbase.h>
#include <string.h>

#include "../../Common/Log/IULog.h"

namespace ocr {

namespace {

//DLL 经回调写日志时，日志里「函数名」一栏填写的来源名
const char* const kOcrLogSource = "FlamingoOcr";

//DLL 的日志回调：按级别转写进客户端日志
void __cdecl OcrDllLogSink(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingoocr::ocr_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;
    //DLL 报告的错误
    case flamingoocr::ocr_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;
    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kOcrLogSource, 0, "%s", text);
}

//宽字符串里的 ASCII 部分转成窄字符串（语言代码、引擎名都是 ASCII），写日志用；非 ASCII 字符记为 '?'
std::string Narrow(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
        out += (text[i] < 0x80) ? static_cast<char>(text[i]) : '?';
    return out;
}

//把 DLL 返回的字符串复制成 std::wstring；NULL 视为空串
std::wstring Copy(const wchar_t* text)
{
    return (text != NULL) ? std::wstring(text) : std::wstring();
}

} // namespace

OcrHostSnapshot::OcrHostSnapshot()
    : m_state(ocr_host_not_started)
    , m_availability(flamingoocr::ocr_engine_failed)
{
}

COcrModuleHost::COcrModuleHost()
    : m_started(false)
    , m_thread(NULL)
    , m_provider(NULL)
    , m_context(NULL)
    , m_api(NULL)
{
    ::InitializeSRWLock(&m_lock);
}

COcrModuleHost::~COcrModuleHost()
{
    if (m_thread != NULL)
    {
        ::WaitForSingleObject(m_thread, kDestroyWaitMs);
        ::CloseHandle(m_thread);
        m_thread = NULL;
    }
}

COcrModuleHost& COcrModuleHost::GetInstance()
{
    //有意不释放：进程退出时不等待可能仍在初始化的引擎
    static COcrModuleHost* s_instance = new COcrModuleHost();
    return *s_instance;
}

void COcrModuleHost::StartPreload(const std::wstring& dllPath, const std::wstring& uiLanguage)
{
    Start(NULL, NULL, dllPath, uiLanguage);
}

void COcrModuleHost::StartPreloadWithProvider(OcrApiProviderFn provider, void* context, const std::wstring& uiLanguage)
{
    if (provider == NULL)
        return;
    Start(provider, context, std::wstring(), uiLanguage);
}

void COcrModuleHost::Start(OcrApiProviderFn provider, void* context, const std::wstring& dllPath,
                           const std::wstring& uiLanguage)
{
    ::AcquireSRWLockExclusive(&m_lock);
    if (m_started)
    {
        ::ReleaseSRWLockExclusive(&m_lock);
        return;
    }
    m_started = true;
    m_provider = provider;
    m_context = context;
    m_dllPath = dllPath;
    m_uiLanguage = uiLanguage;
    m_snapshot.m_state = ocr_host_loading;
    ::ReleaseSRWLockExclusive(&m_lock);

    HANDLE thread = ::CreateThread(NULL, 0, PreloadThreadProc, this, 0, NULL);
    if (thread == NULL)
    {
        LOG_ERROR("ocr preload: CreateThread failed, error %u.", static_cast<unsigned>(::GetLastError()));
        OcrHostSnapshot failed;
        failed.m_state = ocr_host_unavailable;
        failed.m_failure = "cannot create the preload thread";
        Finish(ocr_host_unavailable, NULL, failed);
        return;
    }
    ::AcquireSRWLockExclusive(&m_lock);
    m_thread = thread;
    ::ReleaseSRWLockExclusive(&m_lock);
}

DWORD WINAPI COcrModuleHost::PreloadThreadProc(LPVOID param)
{
    //引擎在初始化时要用 WinRT，本线程先进入多线程套间
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    static_cast<COcrModuleHost*>(param)->RunPreload();
    if (SUCCEEDED(hr))
        ::CoUninitialize();
    return 0;
}

void COcrModuleHost::RunPreload()
{
    const DWORD start = ::GetTickCount();
    OcrHostSnapshot snapshot;
    snapshot.m_state = ocr_host_unavailable;

    //第一步：取函数表（加载 DLL，或测试提供的来源）
    const flamingoocr::OcrApi* api = NULL;
    if (m_provider != NULL)
    {
        const char* failure = "provider returned no api table";
        api = m_provider(m_context, &failure);
        if (api == NULL)
            snapshot.m_failure = (failure != NULL) ? failure : "provider returned no api table";
    }
    else
    {
        FARPROC entry = NULL;
        balloonapp::PluginLoadResult result = m_library.Load(m_dllPath.c_str(), FLAMINGO_OCR_ENTRY_NAME, entry);
        if (result != balloonapp::plugin_load_ok)
        {
            snapshot.m_failure = balloonapp::CPluginLibrary::DescribeResult(result);
        }
        else
        {
            FlamingoOcrGetApiFn getApi = reinterpret_cast<FlamingoOcrGetApiFn>(entry);
            api = getApi();
            if (api == NULL)
                snapshot.m_failure = "FlamingoOcr_GetApi returned NULL";
        }
    }

    //第二步：检查函数表大小（旧 DLL 缺字段时拒绝使用）
    if (api != NULL && api->m_cbSize < flamingoocr::kOcrApiMinSize)
    {
        LOG_WARNING("ocr preload: api table too small (%u bytes, need %u).", static_cast<unsigned>(api->m_cbSize),
                    static_cast<unsigned>(flamingoocr::kOcrApiMinSize));
        snapshot.m_failure = "api table too small";
        api = NULL;
    }

    //第三步：初始化
    if (api != NULL)
    {
        flamingoocr::OcrHostInfo host;
        host.m_cbSize = sizeof(host);
        host.m_log = OcrDllLogSink;
        host.m_uiLanguage = m_uiLanguage.empty() ? NULL : m_uiLanguage.c_str();
        const int initCode = api->m_init(&host);
        if (initCode != flamingoocr::ocr_ok)
        {
            LOG_WARNING("ocr preload: m_init failed with code %d.", initCode);
            snapshot.m_failure = "m_init failed";
            api = NULL;
        }
    }

    if (api == NULL)
    {
        LOG_WARNING("ocr module unavailable: %s, entries hidden (%u ms).", snapshot.m_failure.c_str(),
                    static_cast<unsigned>(::GetTickCount() - start));
        Finish(ocr_host_unavailable, NULL, snapshot);
        return;
    }

    //第四步：查询并缓存可用性、识别语言
    snapshot.m_state = ocr_host_ready;
    flamingoocr::OcrAvailabilityInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    if (api->m_queryAvailability(&info) == flamingoocr::ocr_ok)
    {
        snapshot.m_availability = info.m_availability;
        snapshot.m_settingsUri = Copy(info.m_settingsUri);
    }
    const uint32_t count = api->m_getLanguageCount();
    for (uint32_t i = 0; i < count; ++i)
    {
        OcrLanguageOption option;
        option.m_tag = Copy(api->m_getLanguageTag(i));
        option.m_name = Copy(api->m_getLanguageName(i));
        if (!option.m_tag.empty())
            snapshot.m_languages.push_back(option);
    }
    snapshot.m_defaultLanguage = Copy(api->m_getDefaultLanguage());
    snapshot.m_engineName = Copy(api->m_getEngineName());
    snapshot.m_engineVersion = Copy(api->m_getEngineVersion());

    LOG_INFO("ocr module ready: engine %s %s, availability %d, %u languages, default %s (%u ms).",
             Narrow(snapshot.m_engineName).c_str(), Narrow(snapshot.m_engineVersion).c_str(), snapshot.m_availability,
             static_cast<unsigned>(snapshot.m_languages.size()), Narrow(snapshot.m_defaultLanguage).c_str(),
             static_cast<unsigned>(::GetTickCount() - start));
    Finish(ocr_host_ready, api, snapshot);
}

void COcrModuleHost::Finish(int state, const flamingoocr::OcrApi* api, const OcrHostSnapshot& snapshot)
{
    ::AcquireSRWLockExclusive(&m_lock);
    m_snapshot = snapshot;
    m_snapshot.m_state = state;
    m_api = (state == ocr_host_ready) ? api : NULL;
    ::ReleaseSRWLockExclusive(&m_lock);
}

bool COcrModuleHost::WaitForPreload(DWORD timeoutMs)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        const int state = GetState();
        if (state == ocr_host_ready || state == ocr_host_unavailable)
            return true;
        if (state == ocr_host_not_started || ::GetTickCount() - start >= timeoutMs)
            return false;
        ::Sleep(1);
    }
}

int COcrModuleHost::GetState() const
{
    ::AcquireSRWLockShared(&m_lock);
    const int state = m_snapshot.m_state;
    ::ReleaseSRWLockShared(&m_lock);
    return state;
}

bool COcrModuleHost::CanShowEntry() const
{
    return GetState() == ocr_host_ready;
}

OcrHostSnapshot COcrModuleHost::GetSnapshot() const
{
    ::AcquireSRWLockShared(&m_lock);
    OcrHostSnapshot copy = m_snapshot;
    ::ReleaseSRWLockShared(&m_lock);
    return copy;
}

const flamingoocr::OcrApi* COcrModuleHost::GetApi() const
{
    ::AcquireSRWLockShared(&m_lock);
    const flamingoocr::OcrApi* api = m_api;
    ::ReleaseSRWLockShared(&m_lock);
    return api;
}

} // namespace ocr
