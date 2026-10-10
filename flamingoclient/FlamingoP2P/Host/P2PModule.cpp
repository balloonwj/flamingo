/**
 *  FlamingoP2P.dll 模块对象的实现，见 P2PModule.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PModule.h"

#include <sodium.h>

#include "../../Source/Common/Log/IULog.h"
#include "P2PLog.h"

namespace p2p {

CP2PModule& CP2PModule::Instance()
{
    static CP2PModule s_instance;
    return s_instance;
}

CP2PModule::EngineHost::EngineHost()
    : m_host(NULL)
    , m_service(NULL)
{
}

void CP2PModule::EngineHost::Attach(IP2PHost* host, CP2PService* service)
{
    m_host = host;
    m_service = service;
}

void CP2PModule::EngineHost::SendCommand(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL)
        return;
    m_host->SendP2PCommand(cmd, body.data(), static_cast<uint32_t>(body.size()));
}

IP2PLinkHandler* CP2PModule::EngineHost::FindHandler(const std::string& purpose)
{
    if (m_service == NULL)
        return NULL;
    return m_service->FindHandler(purpose.c_str());
}

const unsigned char* CP2PModule::EngineHost::GetServerPublicKey()
{
    if (m_host == NULL)
        return NULL;
    return m_host->GetServerPublicKey();
}

void CP2PModule::EngineHost::ListPurposes(std::vector<std::string>& purposes)
{
    purposes.clear();
    if (m_service == NULL)
        return;
    m_service->ListPurposes(purposes);
}

CP2PModule::CP2PModule()
    : m_initialized(false)
    , m_host(NULL)
    , m_engine(NULL)
{
}

uint32_t CP2PModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CP2PModule::Init(IP2PHost* host, const P2PInitParams* params)
{
    if (host == NULL || params == NULL)
        return false;
    if (params->m_cbSize != sizeof(P2PInitParams) || params->m_appDir == NULL)
        return false;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_initialized)
            return true;
    }

    //本 DLL 静态链接了自己的一份 libsodium，exe 里的 sodium_init 只初始化 exe 那一份，
    //这里必须自己调用：它负责初始化随机数源，并按 CPU 特性为各算法挑选最快的实现。
    //返回 0 表示本次完成初始化，1 表示此前已初始化过（模块关闭后再次 Init），-1 表示失败
    int sodiumRet = sodium_init();
    if (sodiumRet < 0)
        return false;

    m_appDir = params->m_appDir;
    P2PLogSetHost(host);
    m_service.SetObserver(this);
    m_service.SetEnabled(true);

    //引擎启动失败（winsock 初始化或建唤醒 socket 失败，极少见）不影响模块初始化：此后的 P2P 请求都不会建链，
    //业务方照常走各自原有的中转路径
    m_engineHost.Attach(host, &m_service);
    P2PEngine* engine = new P2PEngine(m_engineHost);
    const bool engineStarted = engine->Start();
    if (!engineStarted)
    {
        delete engine;
        engine = NULL;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_host = host;
        m_engine = engine;
        m_initialized = true;
    }

    LOG_INFO(L"FlamingoP2P initialized, api version: %u, libsodium ready (sodium_init returned %d), app dir: %s.",
             static_cast<unsigned>(kApiVersion), sodiumRet, m_appDir.c_str());
    if (!engineStarted)
        LOG_ERROR("p2p engine failed to start, p2p links are unavailable in this process.");
    return true;
}

void CP2PModule::Shutdown()
{
    P2PEngine* engine = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_initialized)
            return;
        //先置为未初始化：此后网络接收线程送来的帧一律拒收
        m_initialized = false;
        engine = m_engine;
        m_engine = NULL;
    }

    LOG_INFO("FlamingoP2P shutting down.");

    //先停止引擎：关闭全部会话（经宿主发 2305）并结束 IO 线程，此后引擎不再调用宿主与业务方
    if (engine != NULL)
    {
        engine->Stop();
        delete engine;
    }
    m_engineHost.Attach(NULL, NULL);

    //关闭服务并清空业务方登记：此后不会再回调任何业务方
    m_service.SetEnabled(false);

    //最后解除宿主：此后的日志被丢弃，DLL 不再调用任何宿主接口
    P2PLogSetHost(NULL);
    std::lock_guard<std::mutex> guard(m_mutex);
    m_host = NULL;
}

void CP2PModule::OnLoggedIn()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized)
        return;

    LOG_INFO("p2p module notified: logged in.");
    //只投递给 IO 线程，不在本线程上发信令，持锁调用不会与 Shutdown 互相等待
    if (m_engine != NULL)
        m_engine->OnLoggedIn();
}

void CP2PModule::OnLoggingOut()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized)
        return;

    LOG_INFO("p2p module notified: logging out.");
    if (m_engine != NULL)
        m_engine->OnLoggingOut();
}

bool CP2PModule::OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    if (!IsP2PCommand(cmd))
        return false;
    //P2P 信令都带 JSON 包体：空指针或长度为 0 一律视为非法
    if (body == NULL || len == 0)
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized)
        return false;

    LOG_INFO("p2p frame received, cmd: %u, seq: %u, body bytes: %u.", static_cast<unsigned>(cmd),
             static_cast<unsigned>(seq), static_cast<unsigned>(len));
    //复制包体后投递给 IO 线程；引擎未能启动时照样接收，只是不处理
    if (m_engine != NULL)
        m_engine->OnServerFrame(cmd, seq, std::string(body, len));
    return true;
}

IP2PService* CP2PModule::GetService()
{
    return &m_service;
}

void CP2PModule::OnHandlersChanged()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized)
        return;

    //只投递给 IO 线程：已登录时引擎重新报到，还没登录时等登录后的那次报到（届时带上全部已登记的用途）
    if (m_engine != NULL)
        m_engine->OnPurposesChanged();
}

} // namespace p2p
