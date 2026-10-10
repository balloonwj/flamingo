/**
 *  主程序一侧 P2P 宿主的实现，见 P2PHost.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "P2PHost.h"

#include "../../Common/Log/IULog.h"

namespace balloonp2p {

namespace {

//写进 exe 日志文件时，DLL 日志行的「调用处」一栏：DLL 的行里已带有它自己的调用处，这里只标明来源模块
const char* const kP2PLogSource = "FlamingoP2P";

} // namespace

CP2PHost& CP2PHost::GetInstance()
{
    static CP2PHost s_instance;
    return s_instance;
}

CP2PHost::CP2PHost()
    : m_network(NULL)
    , m_ready(false)
{
}

CP2PHost::~CP2PHost()
{
}

bool CP2PHost::Load(const wchar_t* dllPath, const wchar_t* appDir, IP2PNetwork* network)
{
    if (dllPath == NULL || appDir == NULL || network == NULL)
        return false;
    if (m_ready)
        return true;

    balloonapp::PluginLoadResult result = m_loader.Load(dllPath, FLAMINGO_P2P_FACTORY_NAME, p2p::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("p2p module unavailable: %s, api version %u.", balloonapp::CPluginLibrary::DescribeResult(result),
                    static_cast<unsigned>(p2p::kApiVersion));
        return false;
    }

    //先登记网络层：模块在 Init 里就可能经宿主取公钥或写日志
    m_network.store(network);
    p2p::P2PInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("p2p module loaded but Init failed, unloading it.");
        m_network.store(NULL);
        m_loader.Unload();
        return false;
    }

    m_ready = true;
    m_router.Attach(m_loader.GetModule());
    LOG_INFO("p2p module loaded, api version %u.", static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

bool CP2PHost::IsModuleReady() const
{
    return m_ready;
}

void CP2PHost::OnLoginSucceeded()
{
    if (!m_ready)
        return;
    m_loader.GetModule()->OnLoggedIn();
}

void CP2PHost::OnLoggingOut()
{
    if (!m_ready)
        return;
    m_loader.GetModule()->OnLoggingOut();
}

void CP2PHost::Shutdown()
{
    if (!m_loader.IsLoaded())
        return;

    //先解除帧路由的挂接：返回之后网络线程不会再调用模块，随后才能关闭模块、卸载 DLL
    m_router.Detach();
    if (m_ready)
        m_loader.GetModule()->Shutdown();
    m_ready = false;
    m_loader.Unload();
    m_network.store(NULL);
    LOG_INFO("p2p module unloaded.");
}

void CP2PHost::OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    if (m_router.Route(cmd, seq, body.data(), static_cast<uint32_t>(body.size())))
        return;
    LOG_WARNING("p2p frame dropped, cmd=%u, seq=%u, len=%u: module not loaded or frame rejected.",
                static_cast<unsigned>(cmd), static_cast<unsigned>(seq), static_cast<unsigned>(body.size()));
}

p2p::IP2PService* CP2PHost::GetService()
{
    if (!m_ready)
        return NULL;
    return m_loader.GetModule()->GetService();
}

uint32_t CP2PHost::GetDroppedFrameCount() const
{
    return m_router.GetDroppedCount();
}

void CP2PHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;

    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case p2p::p2p_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;
    //DLL 报告的错误
    case p2p::p2p_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;
    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kP2PLogSource, 0, "%s", text);
}

uint32_t CP2PHost::SendP2PCommand(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL && len != 0)
    {
        LOG_WARNING("p2p command %u rejected: null body with length %u.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(len));
        return 0;
    }
    IP2PNetwork* network = m_network.load();
    if (network == NULL)
        return 0;
    //放行规则（号段、包体非空、已连接）由网络层统一判定（exe 里是 CIUSocket::SendP2PCommand）
    return network->SendP2PCommand(cmd, std::string(json != NULL ? json : "", len));
}

const unsigned char* CP2PHost::GetServerPublicKey()
{
    IP2PNetwork* network = m_network.load();
    if (network == NULL)
        return NULL;
    return network->GetServerPublicKey();
}

} // namespace balloonp2p
