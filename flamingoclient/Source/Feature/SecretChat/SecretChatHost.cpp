/**
 *  主程序一侧的单聊密聊接入 CSecretChatHost 的实现，见 SecretChatHost.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "SecretChatHost.h"
#include "SecretChatGate.h"
#include "../../Common/Log/IULog.h"
#include "../../Session/UserSessionData.h"   // g_szHomePath（程序目录，DLL 与 exe 同目录）
#include "../../net/IUSocket.h"              // CIUSocket::SendSecretCommand

namespace balloonsecret {

namespace {

//DLL 写来的日志在日志行「函数签名」一栏显示的名字，便于按模块筛选；DLL 已在正文前加了 "[secret] "
const char* const kSecretLogSource = "FlamingoSecretChat";

} // namespace

CSecretChatHost& CSecretChatHost::GetInstance()
{
    static CSecretChatHost s_instance;
    return s_instance;
}

CSecretChatHost::CSecretChatHost()
    : m_loadAttempted(false)
    , m_ready(false)
    , m_featureEnabled(false)
    , m_serverEnabled(false)
    , m_loggedIn(false)
    , m_myUserId(0)
    , m_sink(NULL)
    , m_routeTarget(NULL)
{
}

CSecretChatHost::~CSecretChatHost()
{
}

void CSecretChatHost::SetSink(ISecretChatSink* sink)
{
    m_sink = sink;
}

void CSecretChatHost::SetFeatureEnabled(bool enabled)
{
    m_featureEnabled = enabled;
}

void CSecretChatHost::OnLoginSucceeded(int64_t myUserId)
{
    m_myUserId = myUserId;
    m_loggedIn = true;
    m_serverEnabled = false;
    if (!m_featureEnabled)
    {
        LOG_INFO("secret chat is disabled for this enterprise, module not activated.");
        return;
    }
    if (!EnsureModuleReady())
        return;
    m_loader.GetModule()->OnLoggedIn();
}

bool CSecretChatHost::OnFeaturesChanged(bool enabled)
{
    const bool before = IsEntryAvailable();
    const bool wasEnabled = m_featureEnabled;
    m_featureEnabled = enabled;
    //由关变开：补加载并报到（报到的应答到达后入口才出现）
    if (enabled && !wasEnabled && m_loggedIn && EnsureModuleReady())
        m_loader.GetModule()->OnLoggedIn();
    const bool after = IsEntryAvailable();
    LOG_INFO("secret chat feature switch changed: enabled=%d, entry %d -> %d.", enabled ? 1 : 0, before ? 1 : 0,
             after ? 1 : 0);
    return before != after;
}

void CSecretChatHost::OnLoggingOut()
{
    m_loggedIn = false;
    m_serverEnabled = false;
    m_myUserId = 0;
    if (m_ready)
        m_loader.GetModule()->OnLoggingOut();
    //模块已结束全部会话（不再回调），登记簿随之清空
    m_book.Clear();
}

void CSecretChatHost::Shutdown()
{
    if (!m_loader.IsLoaded())
        return;
    //先解除帧路由：返回之后网络线程不会再调用模块，随后才能关闭模块、卸载 DLL
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = NULL;
    }
    if (m_ready)
        m_loader.GetModule()->Shutdown();
    m_ready = false;
    m_loader.Unload();
    m_sink = NULL;
    LOG_INFO("secret chat module unloaded.");
}

bool CSecretChatHost::IsEntryAvailable() const
{
    //对方是不是本人由调用方另判，这里按「不是本人」代入
    return m_loggedIn && ShowSecretEntry(m_ready, m_featureEnabled, m_serverEnabled, false);
}

secretchat::ISecretChatModule* CSecretChatHost::Module() const
{
    return m_ready ? m_loader.GetModule() : NULL;
}

bool CSecretChatHost::Invite(int64_t peerId)
{
    secretchat::ISecretChatModule* module = Module();
    if (module == NULL || !m_loggedIn)
        return false;
    return module->Invite(peerId);
}

bool CSecretChatHost::Respond(uint64_t sessionId, bool accept)
{
    secretchat::ISecretChatModule* module = Module();
    if (module == NULL)
        return false;
    const bool ok = module->Respond(sessionId, accept);
    //不论模块是否受理，这条邀请都不再待应答：受理失败说明会话已不在了
    if (accept && ok)
        m_book.OnResponded(sessionId);
    else
        m_book.Forget(sessionId);
    return ok;
}

bool CSecretChatHost::Cancel(uint64_t sessionId)
{
    secretchat::ISecretChatModule* module = Module();
    const bool ok = (module != NULL) && module->Cancel(sessionId);
    m_book.Forget(sessionId);
    return ok;
}

bool CSecretChatHost::Close(uint64_t sessionId)
{
    secretchat::ISecretChatModule* module = Module();
    const bool ok = (module != NULL) && module->Close(sessionId);
    m_book.Forget(sessionId);
    return ok;
}

int CSecretChatHost::ProbeProtect()
{
    secretchat::ISecretChatModule* module = Module();
    return (module != NULL) ? module->ProbeProtect() : secretchat::kProtectNone;
}

int CSecretChatHost::ProtectWindow(HWND hwnd, bool on)
{
    secretchat::ISecretChatModule* module = Module();
    if (module == NULL || hwnd == NULL)
        return secretchat::kProtectNone;
    return module->ProtectWindow(hwnd, on);
}

bool CSecretChatHost::GetSafetyCode(uint64_t sessionId, std::wstring& out)
{
    out.clear();
    secretchat::ISecretChatModule* module = Module();
    if (module == NULL)
        return false;
    //4 组 5 位数字加 3 个空格与结尾 0 共 24 个字符，留出余量
    const uint32_t kSafetyCodeChars = 32;
    wchar_t buf[kSafetyCodeChars] = { 0 };
    if (!module->GetSafetyCode(sessionId, buf, kSafetyCodeChars))
        return false;
    out = buf;
    return true;
}

bool CSecretChatHost::GetIncomingInvite(int64_t fromId, PendingSecretInvite& out)
{
    return m_book.GetIncoming(fromId, ::GetTickCount64(), out);
}

int64_t CSecretChatHost::PeerOfSession(uint64_t sessionId) const
{
    return m_book.PeerOf(sessionId);
}

bool CSecretChatHost::EnsureModuleReady()
{
    if (m_loadAttempted)
        return m_ready;
    m_loadAttempted = true;
    //传完整路径，不依赖系统的 DLL 搜索顺序
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_SECRETCHAT_DLL_NAME;
    balloonapp::PluginLoadResult result =
        m_loader.Load(dllPath, FLAMINGO_SECRETCHAT_FACTORY_NAME, secretchat::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("secret chat module unavailable: %s, api version %u.",
                    balloonapp::CPluginLibrary::DescribeResult(result), static_cast<unsigned>(secretchat::kApiVersion));
        return false;
    }
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    secretchat::SecretInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("secret chat module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }
    m_ready = true;
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = m_loader.GetModule();
    }
    LOG_INFO("secret chat module loaded, api version %u.", static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

void CSecretChatHost::OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    //持锁转交：Shutdown 解除路由之后，网络线程不会再拿着旧模块指针调用
    std::lock_guard<std::mutex> guard(m_routeMutex);
    if (m_routeTarget == NULL)
    {
        LOG_WARNING("secret frame dropped, module not loaded, cmd=%u.", static_cast<unsigned>(cmd));
        return;
    }
    m_routeTarget->OnServerFrame(cmd, seq, body.data(), static_cast<uint32_t>(body.size()));
}

//======================== secretchat::ISecretChatHost ========================

void CSecretChatHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case secretchat::secret_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case secretchat::secret_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kSecretLogSource, 0, "%s", text);
}

uint32_t CSecretChatHost::SendSecretCommand(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL || len == 0)
        return 0;
    return CIUSocket::GetInstance().SendSecretCommand(cmd, std::string(json, len));
}

int64_t CSecretChatHost::GetMyUserId()
{
    return m_myUserId;
}

void CSecretChatHost::OnRegistered(bool enabled)
{
    m_serverEnabled = enabled;
    if (m_sink != NULL)
        m_sink->OnSecretRegistered(enabled);
}

void CSecretChatHost::OnInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code)
{
    if (code == 0)
        m_book.OnInviteSent(sessionId, peerId);
    if (m_sink != NULL)
        m_sink->OnSecretInviteSent(sessionId, peerId, remainMs, code);
}

void CSecretChatHost::OnInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect)
{
    m_book.OnInviteArrived(sessionId, fromId, ::GetTickCount64() + remainMs, peerProtect);
    if (m_sink != NULL)
        m_sink->OnSecretInviteArrived(sessionId, fromId, remainMs, peerProtect);
}

void CSecretChatHost::OnSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect)
{
    switch (state)
    {
    //已建立：邀请不再待应答，会话登记保留到结束
    case secretchat::session_state_active:
        m_book.OnResponded(sessionId);
        break;

    //对方的防截图级别变化：会话状态不变
    case secretchat::session_state_peer_protect:
        break;

    //未接受、已撤回、已结束、已在其他设备处理、本机出错：模块已结束会话，登记一并删除
    default:
        m_book.Forget(sessionId);
        break;
    }
    if (m_sink != NULL)
        m_sink->OnSecretSessionState(sessionId, peerId, state, reason, peerProtect);
}

void CSecretChatHost::OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                                   int64_t serverTimeMs)
{
    if (m_sink != NULL)
        m_sink->OnSecretText(sessionId, peerId, msgId, text, serverTimeMs);
}

void CSecretChatHost::OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                         uint32_t width, uint32_t height, int64_t serverTimeMs)
{
    if (m_sink != NULL)
        m_sink->OnSecretImageBegin(sessionId, peerId, imageId, totalBytes, width, height, serverTimeMs);
}

void CSecretChatHost::OnSecretImageProgress(uint64_t sessionId, uint32_t imageId, uint32_t receivedBytes)
{
    if (m_sink != NULL)
        m_sink->OnSecretImageProgress(sessionId, m_book.PeerOf(sessionId), imageId, receivedBytes);
}

void CSecretChatHost::OnSecretImage(uint64_t sessionId, uint32_t imageId, const void* bytes, uint32_t len, bool ok)
{
    if (m_sink != NULL)
        m_sink->OnSecretImage(sessionId, m_book.PeerOf(sessionId), imageId, bytes, len, ok);
}

void CSecretChatHost::OnSendResult(uint64_t sessionId, uint32_t msgId, int code)
{
    if (m_sink != NULL)
        m_sink->OnSecretSendResult(sessionId, m_book.PeerOf(sessionId), msgId, code);
}

void CSecretChatHost::OnImageSendProgress(uint64_t sessionId, uint32_t imageId, uint32_t sentBytes, uint32_t totalBytes,
                                          int code)
{
    if (m_sink != NULL)
        m_sink->OnSecretImageSendProgress(sessionId, m_book.PeerOf(sessionId), imageId, sentBytes, totalBytes, code);
}

} // namespace balloonsecret
