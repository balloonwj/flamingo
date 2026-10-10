/**
 *  Markdown 消息渲染模块宿主的实现，说明见 MarkdownModuleHost.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MarkdownModuleHost.h"

#include <stddef.h>

#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

namespace markdown {

namespace {

//DLL 经回调写日志时，日志里「函数名」一栏填写的来源名
const char* const kMarkdownLogSource = "FlamingoMarkdown";

//DLL 的日志回调：按级别转写进客户端日志
void __cdecl MarkdownDllLogSink(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingomd::md_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case flamingomd::md_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kMarkdownLogSource, 0, "%s", text);
}

} // namespace

CMarkdownModuleHost::CMarkdownModuleHost()
    : m_started(false)
    , m_thread(NULL)
    , m_provider(NULL)
    , m_context(NULL)
    , m_state(md_host_not_started)
    , m_api(NULL)
{
    ::InitializeSRWLock(&m_lock);
}

CMarkdownModuleHost::~CMarkdownModuleHost()
{
    if (m_thread != NULL)
    {
        ::WaitForSingleObject(m_thread, kDestroyWaitMs);
        ::CloseHandle(m_thread);
        m_thread = NULL;
    }
}

CMarkdownModuleHost& CMarkdownModuleHost::GetInstance()
{
    //有意不释放：气泡持有的文档要用到进程退出，DLL 与函数表不能先于它们消失
    static CMarkdownModuleHost* s_instance = new CMarkdownModuleHost();
    return *s_instance;
}

void CMarkdownModuleHost::StartPreload(const std::wstring& dllPath)
{
    Start(NULL, NULL, dllPath);
}

void CMarkdownModuleHost::StartPreloadWithProvider(MarkdownApiProviderFn provider, void* context)
{
    if (provider == NULL)
        return;
    Start(provider, context, std::wstring());
}

void CMarkdownModuleHost::Start(MarkdownApiProviderFn provider, void* context, const std::wstring& dllPath)
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
    m_state = md_host_loading;
    ::ReleaseSRWLockExclusive(&m_lock);

    HANDLE thread = ::CreateThread(NULL, 0, PreloadThreadProc, this, 0, NULL);
    if (thread == NULL)
    {
        LOG_ERROR("markdown preload: CreateThread failed, error %u.", static_cast<unsigned>(::GetLastError()));
        Finish(md_host_unavailable, NULL, "cannot create the preload thread");
        return;
    }
    ::AcquireSRWLockExclusive(&m_lock);
    m_thread = thread;
    ::ReleaseSRWLockExclusive(&m_lock);
}

DWORD WINAPI CMarkdownModuleHost::PreloadThreadProc(LPVOID param)
{
    static_cast<CMarkdownModuleHost*>(param)->RunPreload();
    return 0;
}

void CMarkdownModuleHost::RunPreload()
{
    const DWORD start = ::GetTickCount();
    std::string failure;

    //第一步：取函数表（加载 DLL，或测试提供的来源）
    const flamingomd::MdApi* api = NULL;
    if (m_provider != NULL)
    {
        const char* reason = "provider returned no api table";
        api = m_provider(m_context, &reason);
        if (api == NULL)
            failure = (reason != NULL) ? reason : "provider returned no api table";
    }
    else
    {
        FARPROC entry = NULL;
        balloonapp::PluginLoadResult result = m_library.Load(m_dllPath.c_str(), FLAMINGO_MARKDOWN_ENTRY_NAME, entry);
        if (result != balloonapp::plugin_load_ok)
        {
            failure = balloonapp::CPluginLibrary::DescribeResult(result);
        }
        else
        {
            FlamingoMarkdownGetApiFn getApi = reinterpret_cast<FlamingoMarkdownGetApiFn>(entry);
            api = getApi();
            if (api == NULL)
                failure = "FlamingoMarkdown_GetApi returned NULL";
        }
    }

    //第二步：检查函数表大小（旧 DLL 缺字段时拒绝使用）
    if (api != NULL && api->m_cbSize < flamingomd::kMdApiMinSize)
    {
        LOG_WARNING("markdown preload: api table too small (%u bytes, need %u).", static_cast<unsigned>(api->m_cbSize),
                    static_cast<unsigned>(flamingomd::kMdApiMinSize));
        failure = "api table too small";
        api = NULL;
    }

    //第三步：初始化
    if (api != NULL)
    {
        flamingomd::MdHostInfo host;
        host.m_cbSize = sizeof(host);
        host.m_log = MarkdownDllLogSink;
        const int initCode = api->m_init(&host);
        if (initCode != flamingomd::md_ok)
        {
            LOG_WARNING("markdown preload: m_init failed with code %d.", initCode);
            failure = "m_init failed";
            api = NULL;
        }
    }

    if (api == NULL)
    {
        LOG_WARNING("markdown module unavailable: %s, entries hidden, markdown messages shown as plain text (%u ms).",
                    failure.c_str(), static_cast<unsigned>(::GetTickCount() - start));
        Finish(md_host_unavailable, NULL, failure);
        return;
    }

    //第四步：把警告框（> [!NOTE] 等）的标题译文交给 DLL。第五版函数表才有这个函数；更旧的 DLL 不认得警告框，
    //无需设置。界面语言在启动时已经确定（Startup.cpp），Translate 可以在本线程调用
    if (flamingomd::MdApiHasField(api, offsetof(flamingomd::MdApi, m_setAlertTitles) + sizeof(void*))
        && api->m_setAlertTitles != NULL)
    {
        const wchar_t* const titles[] =
        {
            TRC(L"Markdown 警告框", L"提示"),
            TRC(L"Markdown 警告框", L"技巧"),
            TRC(L"Markdown 警告框", L"重要"),
            TRC(L"Markdown 警告框", L"警告"),
            TRC(L"Markdown 警告框", L"注意"),
        };
        const int rc = api->m_setAlertTitles(titles, static_cast<uint32_t>(sizeof(titles) / sizeof(titles[0])));
        if (rc != flamingomd::md_ok)
            LOG_WARNING("markdown preload: m_setAlertTitles failed with code %d, alert titles fall back to keywords.", rc);
    }

    LOG_INFO("markdown module ready (%u ms).", static_cast<unsigned>(::GetTickCount() - start));
    Finish(md_host_ready, api, std::string());
}

void CMarkdownModuleHost::Finish(int state, const flamingomd::MdApi* api, const std::string& failure)
{
    ::AcquireSRWLockExclusive(&m_lock);
    m_state = state;
    m_api = (state == md_host_ready) ? api : NULL;
    m_failure = failure;
    ::ReleaseSRWLockExclusive(&m_lock);
}

bool CMarkdownModuleHost::WaitForPreload(DWORD timeoutMs)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        const int state = GetState();
        if (state == md_host_ready || state == md_host_unavailable)
            return true;
        if (state == md_host_not_started || ::GetTickCount() - start >= timeoutMs)
            return false;
        ::Sleep(1);
    }
}

int CMarkdownModuleHost::GetState() const
{
    ::AcquireSRWLockShared(&m_lock);
    const int state = m_state;
    ::ReleaseSRWLockShared(&m_lock);
    return state;
}

bool CMarkdownModuleHost::CanUse() const
{
    return GetState() == md_host_ready;
}

const flamingomd::MdApi* CMarkdownModuleHost::GetApi() const
{
    ::AcquireSRWLockShared(&m_lock);
    const flamingomd::MdApi* api = m_api;
    ::ReleaseSRWLockShared(&m_lock);
    return api;
}

std::string CMarkdownModuleHost::GetFailure() const
{
    ::AcquireSRWLockShared(&m_lock);
    const std::string failure = m_failure;
    ::ReleaseSRWLockShared(&m_lock);
    return failure;
}

} // namespace markdown
