/**
 *  单聊文件夹传输的 P2P 通道实现，见 FolderP2PTransport.h。
 *  balloonwj@qq.com   2026-10-04
 */
//本文件不使用预编译头：FolderP2PTransport.h 带进 <winsock2.h>，必须先于 <windows.h>
#include "FolderP2PTransport.h"
#include "FolderP2PRegistration.h"
#include "../FolderTransfer/DirectFileTransfer.h"   //kDirectFileP2PPurpose（单文件直传的用途名）

#include <string.h>
#include <memory>

#include "../../Common/Log/IULog.h"

using foldertransfer::FolderRelayClientHandler;
using foldertransfer::IFolderLaneTransport;
using foldertransfer::FolderRelayLaneTransport;

namespace balloonp2p {

namespace {

//诊断开关：默认登记文件夹传输的业务方（文件夹传输优先走 P2P）；环境变量 FLAMINGO_FOLDER_P2P 为 0 时不登记，
//文件夹传输照旧全部走 fileserver 中转，引擎也会关掉没人认领的 folder 链路
const char* const kFolderP2PEnvName = "FLAMINGO_FOLDER_P2P";

//诊断开关：环境变量 FLAMINGO_DIRECT_FILE_P2P 为 0 时不登记单文件直传的业务方，2301 不报到 file，服务端判定不能直传，
//单聊发文件一律上传服务器（2026-10-04 起）
const char* const kDirectFileP2PEnvName = "FLAMINGO_DIRECT_FILE_P2P";
const char* const kEnvOff = "0";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
const DWORD kEnvValueChars = 8;

//某个诊断开关是否被设成了 0
bool DisabledByEnv(const char* envName)
{
    char value[kEnvValueChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(envName, value, kEnvValueChars);
    return n > 0 && n < kEnvValueChars && strcmp(value, kEnvOff) == 0;
}

//是否按诊断开关关闭了文件夹传输走 P2P
bool FolderP2PDisabledByEnv()
{
    return DisabledByEnv(kFolderP2PEnvName);
}

} // namespace

/**
 *  P2P 上的一条通道：链路上通道号等于 lane 的帧通道（P2PStreamChannel），单帧上限是中转的应用帧上限。本类只把
 *  文件夹传输的通道接口转成帧通道的接口：帧通道的转交线程把应用帧交给会话（会话的回调可以阻塞）。
 */
class FolderP2PLane : public IFolderLaneTransport, public IP2PStreamHandler
{
public:
    /**
     *  @param slot 链路槽（共享所有权：本对象存续期间链路不会被关闭）
     */
    explicit FolderP2PLane(const std::shared_ptr<FolderP2PLinkSlot>& slot)
        : m_channel(slot, foldertransfer::kFolderRelayMaxAppFrameBytes, kFolderP2PSendWaitMs)
        , m_handler(NULL)
    {
    }

    ~FolderP2PLane()
    {
        Close();
    }

    //打开链路上同号的帧通道
    virtual bool Connect(int lane, FolderRelayClientHandler* handler, int timeoutSec) override
    {
        (void)timeoutSec;
        if (handler == NULL || lane < 0 || lane > foldertransfer::kFolderMaxLaneIndex)
            return false;
        m_handler = handler;
        return m_channel.Open((uint16_t)lane, this);
    }

    //发一个应用帧：队列满时等可写通知，最多 kFolderP2PSendWaitMs
    virtual bool SendAppFrame(const unsigned char* data, size_t len) override
    {
        return m_channel.SendFrame(data, len);
    }

    //注销通道、停转交线程（幂等）；之后不再回调会话
    virtual void Close() override
    {
        m_channel.Close();
    }

    //通道是否连着
    virtual bool IsConnected() const override
    {
        return m_channel.IsOpen();
    }

    //以下为 IP2PStreamHandler 的实现，在帧通道的转交线程上被调用

    //收到一个应用帧：交给会话
    virtual void OnStreamFrame(const unsigned char* data, size_t len) override
    {
        m_handler->OnFolderRelayAppFrame(data, len);
    }

    //链路断开：报告会话
    virtual void OnStreamClosed() override
    {
        m_handler->OnFolderRelayClosed();
    }

private:
    FolderP2PLane(const FolderP2PLane&);
    FolderP2PLane& operator=(const FolderP2PLane&);

private:
    P2PStreamChannel          m_channel;   //链路上的帧通道
    FolderRelayClientHandler* m_handler;   //会话的回调（借用）；Connect 之前为 NULL
};

/**
 *  工厂交给会话的通道对象：Connect 时按工厂的决定走 P2P 或退回中转，之后的方法转调选中的那个实现。
 */
class FolderHybridLane : public IFolderLaneTransport
{
public:
    explicit FolderHybridLane(FolderP2PTransportFactory* factory)
        : m_factory(factory)
    {
    }

    //决定走哪条路，再按选中的实现连接
    virtual bool Connect(int lane, FolderRelayClientHandler* handler, int timeoutSec) override
    {
        const bool p2p = m_factory->DecideP2P();
        if (!p2p && !m_factory->RelayFallbackAllowed())
        {
            //单聊单文件直传不退回 fileserver 中转：P2P 链路（含 imgserver 一对一中转）建不起来就连接失败，
            //由发送端改为上传服务器
            LOG_WARNING("Folder lane %d: p2p link unavailable and relay fallback is disabled, session=%s.", lane,
                        m_factory->Slot()->FeatureSessionId().c_str());
            return false;
        }
        if (p2p)
            m_impl.reset(new FolderP2PLane(m_factory->Slot()));
        else
            m_impl.reset(new FolderRelayLaneTransport(m_factory->RelayIp(), m_factory->RelayPort(), m_factory->Token(),
                                                      m_factory->ServerPubKey()));
        return m_impl->Connect(lane, handler, timeoutSec);
    }

    //发一个应用帧
    virtual bool SendAppFrame(const unsigned char* data, size_t len) override
    {
        return m_impl ? m_impl->SendAppFrame(data, len) : false;
    }

    //关闭
    virtual void Close() override
    {
        if (m_impl)
            m_impl->Close();
    }

    //是否连着
    virtual bool IsConnected() const override
    {
        return m_impl ? m_impl->IsConnected() : false;
    }

private:
    FolderP2PTransportFactory*            m_factory;   //所属工厂（借用，生命周期长于本对象）
    std::unique_ptr<IFolderLaneTransport> m_impl;      //选中的实现；Connect 之前为空
};

// ======================== FolderP2PTransportFactory ========================

FolderP2PTransportFactory::FolderP2PTransportFactory(const std::shared_ptr<FolderP2PLinkSlot>& slot,
                                                     const std::string& relayIp, int relayPort, const std::string& token,
                                                     const unsigned char* serverPubKey)
    : m_slot(slot)
    , m_relayIp(relayIp)
    , m_relayPort(relayPort)
    , m_token(token)
    , m_serverPubKey(serverPubKey)
    , m_decision(decision_none)
    , m_relayFallback(true)
{
}

IFolderLaneTransport* FolderP2PTransportFactory::CreateLane(int lane)
{
    (void)lane;
    return new FolderHybridLane(this);
}

int FolderP2PTransportFactory::LinkPath()
{
    //只读决定、不等待，也不拿 m_mutex（DecideP2P 持着它等链路，最长 kFolderP2PLinkWaitMs，界面线程不能陪着等）。
    //还没决定时数据一个字节都没走；退回中转之后链路即便迟到建成，数据实际走的也是中转
    if (m_decision.load() != decision_p2p)
        return 0;
    return m_slot->LinkPath();
}

bool FolderP2PTransportFactory::DecidedP2P() const
{
    return m_decision.load() == decision_p2p;
}

void FolderP2PTransportFactory::SetRelayFallback(bool allowed)
{
    m_relayFallback = allowed;
}

bool FolderP2PTransportFactory::RelayFallbackAllowed() const
{
    return m_relayFallback.load();
}

bool FolderP2PTransportFactory::DecideP2P()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_decision == decision_none)
    {
        const int64_t start = P2PSteadyNowMs();
        const int state = m_slot->Wait(kFolderP2PLinkWaitMs);
        m_decision = (state == FolderP2PLinkSlot::slot_ready) ? decision_p2p : decision_relay;
        LOG_INFO("Folder transfer path decided: session=%s, path=%s, slot state=%d, waited=%lld ms.",
                 m_slot->FeatureSessionId().c_str(), m_decision == decision_p2p ? "p2p" : "relay", state,
                 (long long)(P2PSteadyNowMs() - start));
    }
    return m_decision == decision_p2p;
}

// ======================== FolderP2PLinkBroker ========================

FolderP2PLinkBroker& FolderP2PLinkBroker::GetInstance()
{
    static FolderP2PLinkBroker s_instance;
    return s_instance;
}

FolderP2PLinkBroker& DirectFileLinkBroker()
{
    static FolderP2PLinkBroker s_instance(foldertransfer::kDirectFileP2PPurpose);
    return s_instance;
}

FolderP2PLinkBroker::FolderP2PLinkBroker()
    : P2PLinkBroker(kFolderP2PPurpose)
{
}

FolderP2PLinkBroker::FolderP2PLinkBroker(const std::string& purpose)
    : P2PLinkBroker(purpose)
{
}

std::shared_ptr<FolderP2PTransportFactory> FolderP2PLinkBroker::CreateFactory(const std::string& featureSessionId,
                                                                              const std::string& relayIp, int relayPort,
                                                                              const std::string& token,
                                                                              const unsigned char* serverPubKey)
{
    return std::shared_ptr<FolderP2PTransportFactory>(
        new FolderP2PTransportFactory(ClaimSlot(featureSessionId), relayIp, relayPort, token, serverPubKey));
}

// ======================== FolderP2PRegistration.h ========================

bool RegisterFolderP2P(p2p::IP2PService* service)
{
    if (FolderP2PDisabledByEnv())
    {
        LOG_INFO("Folder p2p is off (%s is 0), folder transfers use the fileserver relay.", kFolderP2PEnvName);
        return false;
    }
    return FolderP2PLinkBroker::GetInstance().Register(service);
}

void UnregisterFolderP2P()
{
    FolderP2PLinkBroker::GetInstance().Unregister();
}

bool RegisterDirectFileP2P(p2p::IP2PService* service)
{
    if (DisabledByEnv(kDirectFileP2PEnvName))
    {
        LOG_INFO("Direct file p2p is off (%s is 0), single-chat files are always uploaded to the server.",
                 kDirectFileP2PEnvName);
        return false;
    }
    return DirectFileLinkBroker().Register(service);
}

void UnregisterDirectFileP2P()
{
    DirectFileLinkBroker().Unregister();
}

bool IsDirectFileP2PRegistered()
{
    return DirectFileLinkBroker().IsRegistered();
}

} // namespace balloonp2p
