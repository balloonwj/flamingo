/**
 *  flamingoAdmin —— 与 FlamingoLink.dll 的桥接实现，说明见 LinkBridge.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/LinkBridge.h"
#include "Util/Logger.h"
#include "Util/LoginText.h"

namespace flamingoadmin {

namespace {

// 取 exe 所在目录（不带结尾的反斜杠）
CString ExeDirectory()
{
    TCHAR path[MAX_PATH] = {};
    ::GetModuleFileName(nullptr, path, MAX_PATH);
    CString dir(path);
    const int slash = dir.ReverseFind(_T('\\'));
    return (slash > 0) ? dir.Left(slash) : dir;
}

// UTF-8 转 UTF-16
CString FromUtf8(const char* text)
{
    if (text == nullptr || text[0] == '\0')
        return CString();

    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 1)
        return CString();

    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, out.GetBuffer(len), len);
    out.ReleaseBuffer();
    return out;
}

// UTF-16 转 UTF-8
std::string ToUtf8(const CString& text)
{
    if (text.IsEmpty())
        return std::string();

    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(), nullptr, 0,
                                          nullptr, nullptr);
    if (len <= 0)
        return std::string();

    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(), &out[0], len, nullptr, nullptr);
    return out;
}

} // anonymous namespace

LinkBridge& LinkBridge::Instance()
{
    static LinkBridge instance;
    return instance;
}

LinkBridge::LinkBridge()
    : m_module(nullptr)
    , m_expiredHwnd(nullptr)
    , m_expiredMsg(0)
    , m_stateKnown(false)
{
}

bool LinkBridge::Load(HWND expiredHwnd, UINT expiredMsg)
{
    if (m_module != nullptr)
        return true;

    m_expiredHwnd = expiredHwnd;
    m_expiredMsg = expiredMsg;

    const CString path = ExeDirectory() + _T("\\") + FLAMINGO_LINK_DLL_NAME;
    FARPROC entry = nullptr;
    const PluginLoadResult r = m_lib.Load(path, FLAMINGO_LINK_FACTORY_NAME, entry);
    if (r != PluginLoadResult::Ok)
    {
        FA_LOGW(_T("FlamingoLink unavailable: %s, path: %s"), PluginLibrary::DescribeResult(r), path.GetString());
        return false;
    }

    FlamingoLink_CreateFn create = reinterpret_cast<FlamingoLink_CreateFn>(entry);
    flamingolink::ILinkModule* module = create(flamingolink::kApiVersion);
    if (module == nullptr)
    {
        FA_LOGW(_T("FlamingoLink unavailable: api version mismatch (host %u)"),
                static_cast<unsigned>(flamingolink::kApiVersion));
        m_lib.Free();
        return false;
    }

    if (!module->Init(this))
    {
        FA_LOGW(_T("FlamingoLink unavailable: module init failed"));
        m_lib.Free();
        return false;
    }

    m_module = module;
    FA_LOGI(_T("FlamingoLink loaded: %s"), path.GetString());
    return true;
}

void LinkBridge::Unload()
{
    if (m_module == nullptr)
        return;

    m_module->DestroyPage();
    m_module->Stop();
    m_module->Shutdown();
    m_module = nullptr;
    m_lib.Free();
    m_stateKnown = false;
    m_enterpriseId.Empty();
    m_enterpriseName.Empty();
    FA_LOGI(_T("FlamingoLink unloaded"));
}

void LinkBridge::Start(const CString& host, int port, const std::string& pubkey, const CString& token)
{
    if (m_module == nullptr)
        return;

    const std::string tokenUtf8 = ToUtf8(token);
    const std::string version = ToUtf8(CString(logintext::kAdminVersion));

    flamingolink::LinkStartParams p;
    p.m_host = host.GetString();
    p.m_port = port;
    p.m_pubkeyBase64 = pubkey.c_str();
    p.m_token = tokenUtf8.c_str();
    p.m_clientVersion = version.c_str();
    m_module->Start(&p);
    FA_LOGI(_T("FlamingoLink start: host %s, port %d, encrypted %d"),
            host.GetString(), port, pubkey.empty() ? 0 : 1);
}

void LinkBridge::Stop()
{
    if (m_module == nullptr)
        return;

    m_module->Stop();
}

HWND LinkBridge::CreatePage(HWND parent)
{
    if (m_module == nullptr)
        return nullptr;

    HWND page = m_module->CreatePage(parent);
    if (page == nullptr)
    {
        FA_LOGW(_T("FlamingoLink create page failed"));
    }
    return page;
}

void LinkBridge::SetPageBounds(const RECT& rc)
{
    if (m_module == nullptr)
        return;

    m_module->SetPageBounds(&rc);
}

void LinkBridge::SetEnterprise(const CString& enterpriseId, const CString& enterpriseName)
{
    if (m_module == nullptr)
        return;
    if (enterpriseId == m_enterpriseId && enterpriseName == m_enterpriseName)
        return;

    m_enterpriseId = enterpriseId;
    m_enterpriseName = enterpriseName;
    m_module->SetEnterprise(enterpriseId.GetString(), enterpriseName.GetString());
}

void LinkBridge::ApplyPageState(const LinkPageState& state)
{
    if (m_module == nullptr)
        return;

    if (!m_stateKnown || state.m_active != m_pageState.m_active)
        m_module->SetPageActive(state.m_active);
    if (!m_stateKnown || state.m_covered != m_pageState.m_covered)
        m_module->SetPageCovered(state.m_covered);

    m_pageState = state;
    m_stateKnown = true;
}

void LinkBridge::GetStatus(flamingolink::LinkStatus& out)
{
    memset(&out, 0, sizeof(out));
    out.m_state = flamingolink::link_state_stopped;
    if (m_module != nullptr)
        m_module->GetStatus(&out);
}

void LinkBridge::Log(int level, const char* text)
{
    const CString line = FromUtf8(text);
    switch (level)
    {
    // 可以自动恢复的异常
    case flamingolink::link_log_warning:
        FA_LOGW(_T("[FlamingoLink] %s"), line.GetString());
        break;

    // 需要关注的错误
    case flamingolink::link_log_error:
        FA_LOGE(_T("[FlamingoLink] %s"), line.GetString());
        break;

    // 一般信息
    default:
        FA_LOGI(_T("[FlamingoLink] %s"), line.GetString());
        break;
    }
}

void LinkBridge::OnSessionExpired()
{
    FA_LOGW(_T("FlamingoLink reports session expired"));
    if (::IsWindow(m_expiredHwnd))
    {
        ::PostMessage(m_expiredHwnd, m_expiredMsg, 0, 0);
    }
}

} // namespace flamingoadmin
