/**
 *  P2P 引擎的实现，见 P2PEngine.h。
 *  balloonwj@qq.com   2026-10-04
 */
//select 能等待的 socket 上限：Windows 默认 64，每个 P2P 会话占一个 socket，调大以免会话多时放不下。
//必须在包含 winsock2.h 之前定义
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>

#include "P2PEngine.h"

#include <string.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"
#include "../Chan/P2PReliableChannel.h"   //kP2PReliableWritableBytes
#include "../Link/P2PLinkSession.h"
#include "../Link/P2PSignalCodec.h"
#include "P2PAdapters.h"
#include "P2PRelayConn.h"
#include "P2PUdpPort.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

namespace p2p {

namespace {

//接收缓冲（字节）：大于链路报文的上限（1200 字节），STUN 应答更短
const int kRecvBufBytes = 2048;

//一个 socket 一次可读事件里最多收的报文个数，避免一条链路占住 IO 线程
const int kMaxReceivesPerWakeup = 64;

//唤醒 socket 收发的那一个字节
const char kWakeByte = 1;

//winsock 的版本：2.2
const BYTE kWsaMajor = 2;
const BYTE kWsaMinor = 2;

//毫秒与微秒的换算
const int64_t kMillisPerSecond = 1000;
const int64_t kMicrosPerMilli = 1000;

//联调用的强制中转开关：环境变量名，值为 kForceRelayOn 时开启
const char* const kForceRelayEnvName = "FLAMINGO_P2P_FORCE_RELAY";
const char* const kForceRelayOn = "1";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
const DWORD kEnvValueChars = 8;

//联调用途（2306 建的会话）：没有业务方登记时也保留链路，供联调用例直接使用
const char* const kDebugPurpose = "test";

//忙时要求的系统定时器精度（毫秒）：Windows 默认约 15.6 毫秒，KCP 每 2 毫秒刷新一次，select 按 2 毫秒等待时实际要睡
//十几毫秒，可靠通道的吞吐会掉到几十 KB/s（第 4f 步实测：提高精度前约 45 KB/s，提高后约 50 MiB/s）
const UINT kHiResTimerMs = 1;

//有会话的下一次定时在这么久之内（毫秒）就算「忙」，需要高精度定时器
const int64_t kHiResDemandMs = 50;

//不忙之后保持高精度多久再恢复（毫秒）：避免收发间歇时反复切换
const int64_t kHiResHoldMs = 1000;

//单调时钟的当前时间（毫秒）
int64_t NowMs()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

//投递给 IO 线程的任务种类
enum engine_task_kind
{
    engine_task_frame       = 1,   //收到一个 2300 段的帧
    engine_task_logged_in   = 2,   //登录成功：发 2301
    engine_task_logging_out = 3,   //即将退出登录：关闭全部会话
    engine_task_purposes_changed = 4,   //业务方登记或注销了用途：已登录时重新发 2301
};

//一个投递给 IO 线程的任务
struct EngineTask
{
    int         m_kind;   //任务种类，取值见 engine_task_kind
    uint16_t    m_cmd;    //帧的命令号（m_kind 为 engine_task_frame 时有效）
    uint32_t    m_seq;    //帧序号
    std::string m_body;   //帧的包体

    EngineTask()
        : m_kind(0)
        , m_cmd(0)
        , m_seq(0)
    {
    }
};

//一个会话的存储：UDP socket、回落中转连接与状态机，由引擎与交给业务方的链路对象共同持有（shared_ptr），
//业务方持有链路比引擎活得久时，会话已处于终态，不会再访问引擎
struct SessionBox
{
    P2PUdpPort                      m_udp;             //本会话的 UDP socket
    std::unique_ptr<P2PRelayConn>   m_relay;           //回落中转连接；会话请求中转之前为空
    bool                            m_relayReported;   //中转的连接结果（就绪或失败）是否已交给会话
    bool                            m_relayBroken;     //就绪之后中转连接断开是否已交给会话
    std::unique_ptr<P2PLinkSession> m_session;         //状态机（声明在最后：先于中转连接与 socket 销毁）

    SessionBox()
        : m_relayReported(false)
        , m_relayBroken(false)
    {
    }
};

//交给业务方的链路：方法都转调会话；引用计数，最后一个引用释放时销毁
class LinkImpl : public IP2PLink
{
public:
    explicit LinkImpl(const std::shared_ptr<SessionBox>& box)
        : m_box(box)
        , m_refs(1)
    {
    }

    //以下为 IP2PLink 的实现，语义见 FlamingoP2PApi.h

    //打开一条通道
    virtual int OpenChannel(uint16_t channelId, int kind, IP2PChannelSink* sink) override
    {
        return m_box->m_session->OpenChannel(channelId, kind, sink);
    }

    //在通道上发送一块数据
    virtual int Send(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        return m_box->m_session->SendData(channelId, data, len, NowMs());
    }

    //取统计
    virtual void GetStats(P2PLinkStats* out) override
    {
        m_box->m_session->GetStats(out);
    }

    //关闭链路
    virtual void Close(int reason) override
    {
        m_box->m_session->Close(reason, NowMs());
    }

    //增加一个引用
    virtual void AddRef() override
    {
        ++m_refs;
    }

    //释放一个引用；最后一个引用释放时销毁
    virtual void Release() override
    {
        if (--m_refs == 0)
            delete this;
    }

private:
    ~LinkImpl()
    {
    }

private:
    std::shared_ptr<SessionBox> m_box;    //会话的存储（与引擎共同持有）
    std::atomic<long>           m_refs;   //引用计数
};

} // namespace

/**
 *  引擎的实现：IO 线程与全部状态。也是各会话的观察者。
 */
struct P2PEngine::Impl : public IP2PSessionObserver, public IP2PRelayWaker
{
    explicit Impl(IP2PEngineHost& host)
        : m_host(host)
        , m_forceRelay(false)
        , m_hiRes(false)
        , m_lastBusyMs(0)
        , m_stop(false)
        , m_started(false)
        , m_wsaStarted(false)
        , m_wake(INVALID_SOCKET)
        , m_wakePort(0)
        , m_enabled(false)
        , m_sessionCount(0)
        , m_loggedIn(false)
    {
    }

    //投递一个任务并唤醒 IO 线程
    void Post(const EngineTask& task)
    {
        {
            std::lock_guard<std::mutex> guard(m_taskMutex);
            m_tasks.push_back(task);
        }
        Wake();
    }

    //给唤醒 socket 发一个字节，使 select 立即返回
    void Wake()
    {
        if (m_wake == INVALID_SOCKET)
            return;
        sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        to.sin_port = htons(m_wakePort);
        ::sendto(m_wake, &kWakeByte, 1, 0, (const sockaddr*)&to, sizeof(to));
    }

    //IO 线程的主循环
    void ThreadMain()
    {
        LOG_INFO("p2p engine io thread started.");
        while (!m_stop)
        {
            ServiceRelays();
            fd_set readable;
            fd_set writable;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            FD_SET(m_wake, &readable);
            std::vector<std::shared_ptr<SessionBox> > boxes;
            std::vector<std::shared_ptr<SessionBox> > relays;
            //已放进集合的 socket 个数：唤醒 socket 占一个
            size_t inSet = 1;
            int64_t now = NowMs();
            int64_t next = now + kP2PEngineMaxWaitMs;
            for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin();
                 it != m_sessions.end(); ++it)
            {
                if (it->second->m_udp.IsOpen() && inSet < FD_SETSIZE)
                {
                    FD_SET((SOCKET)it->second->m_udp.Handle(), &readable);
                    boxes.push_back(it->second);
                    ++inSet;
                }
                if (it->second->m_relay && it->second->m_relayReported
                    && it->second->m_relay->State() == p2p_relay_ready && inSet < FD_SETSIZE)
                {
                    FD_SET((SOCKET)it->second->m_relay->Handle(), &readable);
                    //有积压时等 socket 可写，继续把发送缓冲写进去
                    if (it->second->m_relay->QueuedBytes() > 0)
                        FD_SET((SOCKET)it->second->m_relay->Handle(), &writable);
                    relays.push_back(it->second);
                    ++inSet;
                }
                const int64_t due = it->second->m_session->NextTimerMs();
                if (due < next)
                    next = due;
            }
            UpdateTimerResolution(now, next - now < kHiResDemandMs);
            int64_t waitMs = next - now;
            if (waitMs < 0)
                waitMs = 0;
            if (waitMs > kP2PEngineMaxWaitMs)
                waitMs = kP2PEngineMaxWaitMs;
            timeval tv;
            tv.tv_sec = (long)(waitMs / kMillisPerSecond);
            tv.tv_usec = (long)((waitMs % kMillisPerSecond) * kMicrosPerMilli);
            ::select(0, &readable, &writable, NULL, &tv);
            if (m_stop)
                break;

            if (FD_ISSET(m_wake, &readable))
                DrainWake();
            RunTasks();
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                if (boxes[i]->m_udp.IsOpen() && FD_ISSET((SOCKET)boxes[i]->m_udp.Handle(), &readable))
                    ReceiveAll(*boxes[i]);
            }
            for (size_t i = 0; i < relays.size(); ++i)
            {
                if (relays[i]->m_relay->State() == p2p_relay_ready
                    && FD_ISSET((SOCKET)relays[i]->m_relay->Handle(), &writable))
                    FlushRelay(*relays[i]);
                if (relays[i]->m_relay->State() == p2p_relay_ready
                    && FD_ISSET((SOCKET)relays[i]->m_relay->Handle(), &readable))
                    ReceiveRelay(*relays[i]);
            }
            RunTimers();
            Cleanup();
        }
        UpdateTimerResolution(NowMs(), false);
        if (m_hiRes)
        {
            ::timeEndPeriod(kHiResTimerMs);
            m_hiRes = false;
        }
        LOG_INFO("p2p engine io thread stopped.");
    }

    /**
     *  忙时提高系统定时器精度，不忙 kHiResHoldMs 之后恢复（Win10 2004 起只影响本进程）。只在 IO 线程上调用。
     *  @param now  当前时间（毫秒）
     *  @param busy 是否有会话需要很快再次驱动
     */
    void UpdateTimerResolution(int64_t now, bool busy)
    {
        if (busy)
        {
            m_lastBusyMs = now;
            if (!m_hiRes)
            {
                m_hiRes = (::timeBeginPeriod(kHiResTimerMs) == TIMERR_NOERROR);
                if (m_hiRes)
                    LOG_INFO("p2p engine: high resolution timer on.");
            }
            return;
        }
        if (m_hiRes && now - m_lastBusyMs >= kHiResHoldMs)
        {
            ::timeEndPeriod(kHiResTimerMs);
            m_hiRes = false;
            LOG_INFO("p2p engine: high resolution timer off.");
        }
    }

    //把唤醒 socket 里积攒的字节读空
    void DrainWake()
    {
        char buf[kRecvBufBytes];
        while (::recv(m_wake, buf, sizeof(buf), 0) > 0)
        {
        }
    }

    //发 2301 报到：带上宿主已登记业务方的用途名，服务端只为两端都报到了的用途建会话
    void SendRegister()
    {
        std::vector<std::string> purposes;
        m_host.ListPurposes(purposes);
        SendSignal(cmd_p2p_register, P2PSignalCodec::BuildRegister(purposes));
        std::string joined;
        for (size_t i = 0; i < purposes.size(); ++i)
        {
            if (i > 0)
                joined += ",";
            joined += purposes[i];
        }
        LOG_INFO("p2p register sent, purposes: [%s].", joined.c_str());
    }

    //执行投递来的全部任务
    void RunTasks()
    {
        std::deque<EngineTask> tasks;
        {
            std::lock_guard<std::mutex> guard(m_taskMutex);
            tasks.swap(m_tasks);
        }
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            const EngineTask& t = tasks[i];
            switch (t.m_kind)
            {
            //收到一个 2300 段的帧
            case engine_task_frame:
                HandleFrame(t);
                break;

            //登录成功：报到，等应答再记下开关与探测服务器
            case engine_task_logged_in:
                m_loggedIn = true;
                m_enabled = false;
                SendRegister();
                break;

            //即将退出登录：关闭全部会话，忘掉报到结果
            case engine_task_logging_out:
                m_loggedIn = false;
                CloseAll(p2p_close_self_logout);
                m_enabled = false;
                m_probes.clear();
                break;

            //业务方登记或注销了用途：已登录时重新报到，服务端以最后一次报到为准；未登录时等登录后的那次报到
            case engine_task_purposes_changed:
                if (m_loggedIn)
                    SendRegister();
                break;

            //不会出现的种类
            default:
                break;
            }
        }
    }

    //分发一个 2300 段的帧
    void HandleFrame(const EngineTask& t)
    {
        const int64_t now = NowMs();
        switch (t.m_cmd)
        {
        //2301 的应答：本企业是否开启、地址探测服务器
        case cmd_p2p_register:
        {
            P2PRegisterResult r;
            if (!P2PSignalCodec::ParseRegisterResponse(t.m_body, r))
            {
                LOG_WARNING("p2p register response unparsable: %s", t.m_body.c_str());
                return;
            }
            m_probes.clear();
            for (size_t i = 0; i < r.m_probes.size(); ++i)
            {
                P2PProbeServer s;
                if (ResolveIpv4(r.m_probes[i].m_host, s.m_ip))
                {
                    s.m_port = r.m_probes[i].m_port;
                    m_probes.push_back(s);
                }
                else
                {
                    LOG_WARNING("p2p probe host cannot be resolved: %s", r.m_probes[i].m_host.c_str());
                }
            }
            m_enabled = (r.m_code == 0 && r.m_enabled);
            LOG_INFO("p2p register response: code %d, enabled %d, probes %u.", r.m_code, m_enabled ? 1 : 0,
                     (unsigned)m_probes.size());
            break;
        }

        //服务端推来一个新会话
        case cmd_p2p_session_notify:
            HandleSessionNotify(t.m_body, now);
            break;

        //对方的 2303 转发（带 data）或本方 2303 的应答（只有 code）
        case cmd_p2p_signal:
        {
            P2PPeerSignal sig;
            if (!P2PSignalCodec::ParseSignalForward(t.m_body, sig))
            {
                LOG_INFO("p2p signal response: %s", t.m_body.c_str());
                return;
            }
            std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.find(sig.m_sessionId);
            if (it == m_sessions.end())
            {
                LOG_WARNING("p2p signal for unknown session %s dropped.", sig.m_sessionId.c_str());
                return;
            }
            LOG_INFO("p2p peer signal: session %s, candidates %u.", sig.m_sessionId.c_str(),
                     (unsigned)sig.m_candidates.size());
            it->second->m_session->OnPeerSignal(sig.m_publicKey, sig.m_candidates, now);
            break;
        }

        //服务端推来的 2305（带会话号）或本方 2305 的应答（只有 code）
        case cmd_p2p_close:
        {
            P2PCloseInfo close;
            if (!P2PSignalCodec::ParseClose(t.m_body, close))
                return;
            std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.find(close.m_sessionId);
            if (it == m_sessions.end())
                return;
            LOG_INFO("p2p close pushed: session %s, reason %d, errcode %d.", close.m_sessionId.c_str(), close.m_reason,
                     close.m_errCode);
            it->second->m_session->OnServerClose(close.m_reason, close.m_errCode, now);
            break;
        }

        //2304 与 2306 的应答：只记日志
        case cmd_p2p_result:
        case cmd_p2p_debug_open:
            LOG_INFO("p2p response cmd %u: %s", (unsigned)t.m_cmd, t.m_body.c_str());
            break;

        //2300 段里本引擎不认识的命令
        default:
            LOG_WARNING("p2p frame cmd %u not handled.", (unsigned)t.m_cmd);
            break;
        }
    }

    //收到 2302：开 socket、取本机候选、开始建链
    void HandleSessionNotify(const std::string& body, int64_t now)
    {
        P2PSessionInfo info;
        if (!P2PSignalCodec::ParseSessionNotify(body, info))
        {
            LOG_WARNING("p2p session notify unparsable: %s", body.c_str());
            return;
        }
        if (m_sessions.count(info.m_sessionId) != 0)
            return;
        std::shared_ptr<SessionBox> box(new SessionBox());
        if (!box->m_udp.Open())
        {
            LOG_ERROR("p2p session %s: udp socket open failed, error %d.", info.m_sessionId.c_str(), ::WSAGetLastError());
            SendSignal(cmd_p2p_close, P2PSignalCodec::BuildClose(info.m_sessionId, p2p_close_connect_fail, 0));
            return;
        }
        box->m_session.reset(new P2PLinkSession(info, box->m_udp, *this));
        box->m_session->SetForceRelay(m_forceRelay);
        m_sessions[info.m_sessionId] = box;
        m_sessionCount = m_sessions.size();
        const std::vector<P2PCandidate> hosts = BuildHostCandidates(EnumerateAdapters(), box->m_udp.Port());
        LOG_INFO("p2p session %s begins: role %d, purpose %s, udp port %u, host candidates %u, probes %u, force relay %d.",
                 info.m_sessionId.c_str(), info.m_role, info.m_purpose.c_str(), (unsigned)box->m_udp.Port(),
                 (unsigned)hosts.size(), (unsigned)m_probes.size(), m_forceRelay ? 1 : 0);
        box->m_session->Begin(hosts, m_probes, now);
    }

    //把一个会话 socket 上的报文收完（最多 kMaxReceivesPerWakeup 个）
    void ReceiveAll(SessionBox& box)
    {
        uint8_t buf[kRecvBufBytes];
        for (int i = 0; i < kMaxReceivesPerWakeup && box.m_udp.IsOpen(); ++i)
        {
            uint32_t fromIp = 0;
            uint16_t fromPort = 0;
            const int n = box.m_udp.Receive(buf, sizeof(buf), fromIp, fromPort);
            if (n < 0)
                break;
            box.m_session->OnDatagram(fromIp, fromPort, buf, (size_t)n, NowMs());
        }
    }

    //把中转连接线程的结果（就绪或失败）交给会话，每条连接只交一次
    void ServiceRelays()
    {
        const int64_t now = NowMs();
        std::vector<std::shared_ptr<SessionBox> > boxes;
        for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin();
             it != m_sessions.end(); ++it)
        {
            if (it->second->m_relay && !it->second->m_relayReported)
                boxes.push_back(it->second);
        }
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            SessionBox& box = *boxes[i];
            const int state = box.m_relay->State();
            if (state == p2p_relay_ready)
            {
                box.m_relayReported = true;
                LOG_INFO("p2p session %s: relay connected.", box.m_session->Info().m_sessionId.c_str());
                box.m_session->OnRelayConnected(*box.m_relay, now);
            }
            else if (state == p2p_relay_failed || state == p2p_relay_closed)
            {
                box.m_relayReported = true;
                LOG_INFO("p2p session %s: relay failed.", box.m_session->Info().m_sessionId.c_str());
                box.m_session->OnRelayFailed(now);
            }
        }
        //就绪之后在发送时出错的连接（状态已变为关闭，不再进 select）：告诉会话
        for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin();
             it != m_sessions.end(); ++it)
        {
            SessionBox& box = *it->second;
            if (box.m_relay && box.m_relayReported && !box.m_relayBroken && box.m_relay->State() == p2p_relay_closed)
            {
                box.m_relayBroken = true;
                LOG_INFO("p2p session %s: relay connection closed.", it->first.c_str());
                box.m_session->OnRelayBroken(now);
            }
        }
    }

    //socket 可写：把中转连接的积压继续写进去；积压降下来时通知会话（可靠通道据此通知业务方可写）
    void FlushRelay(SessionBox& box)
    {
        if (!box.m_relay->Flush())
        {
            box.m_relayBroken = true;
            LOG_INFO("p2p session %s: relay send failed.", box.m_session->Info().m_sessionId.c_str());
            box.m_session->OnRelayBroken(NowMs());
            return;
        }
        if (box.m_relay->QueuedBytes() < kP2PReliableWritableBytes)
            box.m_session->OnTransportDrained(NowMs());
    }

    //读中转连接上收到的报文交给会话；连接断开时告诉会话
    void ReceiveRelay(SessionBox& box)
    {
        std::vector<std::string> packets;
        const bool alive = box.m_relay->ReadPackets(packets);
        for (size_t i = 0; i < packets.size(); ++i)
        {
            box.m_session->OnRelayPacket(reinterpret_cast<const uint8_t*>(packets[i].data()), packets[i].size(),
                                         NowMs());
        }
        if (!alive && !box.m_relayBroken)
        {
            box.m_relayBroken = true;
            LOG_INFO("p2p session %s: relay connection broken.", box.m_session->Info().m_sessionId.c_str());
            box.m_session->OnRelayBroken(NowMs());
        }
    }

    //驱动到时刻的会话定时器
    void RunTimers()
    {
        const int64_t now = NowMs();
        std::vector<std::shared_ptr<SessionBox> > boxes;
        for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin();
             it != m_sessions.end(); ++it)
            boxes.push_back(it->second);
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            if (now >= boxes[i]->m_session->NextTimerMs())
                boxes[i]->m_session->OnTimer(now);
        }
    }

    //清理已到终态的会话：关闭 socket，放掉引擎对链路的引用
    void Cleanup()
    {
        for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin(); it != m_sessions.end();)
        {
            if (!it->second->m_session->IsFinished())
            {
                ++it;
                continue;
            }
            LOG_INFO("p2p session %s finished, state %d.", it->first.c_str(), it->second->m_session->State());
            it->second->m_udp.Close();
            if (it->second->m_relay)
                it->second->m_relay->Close();
            std::map<std::string, LinkImpl*>::iterator link = m_links.find(it->first);
            if (link != m_links.end())
            {
                link->second->Release();
                m_links.erase(link);
            }
            it = m_sessions.erase(it);
        }
        m_sessionCount = m_sessions.size();
    }

    //关闭全部会话（业务方关闭的语义：发关闭报文与 2305，不再回调业务方），再清理
    void CloseAll(int reason)
    {
        const int64_t now = NowMs();
        for (std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.begin();
             it != m_sessions.end(); ++it)
            it->second->m_session->Close(reason, now);
        Cleanup();
    }

    //按会话对象找它的业务方
    IP2PLinkHandler* HandlerOf(P2PLinkSession* session)
    {
        return m_host.FindHandler(session->Info().m_purpose);
    }

    //以下为 IP2PSessionObserver 的实现，都在 IO 线程上被调用

    //经宿主发一个 2300 段命令
    virtual void SendSignal(uint16_t cmd, const std::string& body) override
    {
        m_host.SendCommand(cmd, body);
    }

    //链路就绪：交出 IP2PLink；没有业务方登记时（如联调的 test）链路照样保留，直到关闭
    virtual void OnSessionReady(P2PLinkSession* session) override
    {
        const std::string& id = session->Info().m_sessionId;
        std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.find(id);
        if (it == m_sessions.end())
            return;
        LinkImpl* link = new LinkImpl(it->second);
        m_links[id] = link;
        IP2PLinkHandler* handler = HandlerOf(session);
        LOG_INFO("p2p session %s ready, path %d, handler %s.", id.c_str(), session->Path(),
                 handler != NULL ? "found" : "none");
        if (handler != NULL)
        {
            handler->OnLinkReady(session->Info().m_featureSessionId.c_str(), link);
            return;
        }
        //没有业务方登记这个用途（如这一端的程序版本还不支持该业务走 P2P）：链路没人用，立即关闭，不让它一直保活。
        //联调用途 test 例外
        if (session->Info().m_purpose != kDebugPurpose)
        {
            LOG_INFO("p2p session %s: no handler for purpose %s, closing.", id.c_str(), session->Info().m_purpose.c_str());
            session->Close(p2p_close_business_end, NowMs());
        }
    }

    //建链失败
    virtual void OnSessionFailed(P2PLinkSession* session, int code) override
    {
        LOG_INFO("p2p session %s failed, code %d.", session->Info().m_sessionId.c_str(), code);
        IP2PLinkHandler* handler = HandlerOf(session);
        if (handler != NULL)
            handler->OnLinkFailed(session->Info().m_featureSessionId.c_str(), code);
    }

    //就绪后断开
    virtual void OnSessionClosed(P2PLinkSession* session, int reason) override
    {
        LOG_INFO("p2p session %s closed, reason %d.", session->Info().m_sessionId.c_str(), reason);
        IP2PLinkHandler* handler = HandlerOf(session);
        if (handler != NULL)
            handler->OnLinkClosed(session->Info().m_featureSessionId.c_str(), reason);
    }

    //请求回落中转：建中转连接并起连接线程，结果由 ServiceRelays 交回会话
    virtual void StartRelay(P2PLinkSession* session) override
    {
        const P2PSessionInfo& info = session->Info();
        std::map<std::string, std::shared_ptr<SessionBox> >::iterator it = m_sessions.find(info.m_sessionId);
        if (it == m_sessions.end() || it->second->m_relay)
            return;
        LOG_INFO("p2p session %s: falling back to relay %s:%u.", info.m_sessionId.c_str(), info.m_relay.m_host.c_str(),
                 (unsigned)info.m_relay.m_port);
        it->second->m_relay.reset(new P2PRelayConn());
        it->second->m_relay->Start(info.m_relay.m_host, info.m_relay.m_port, info.m_token, m_host.GetServerPublicKey(),
                                   this);
    }

    //可靠通道收下了业务方的数据：唤醒 IO 线程重新计算定时（任意线程上调用）
    virtual void RequestWake() override
    {
        Wake();
    }

    //以下为 IP2PRelayWaker 的实现

    //中转连接线程完成：唤醒 IO 线程（任意线程上调用）
    virtual void WakeIo() override
    {
        Wake();
    }

    IP2PEngineHost&                                     m_host;          //宿主（借用）
    std::atomic<bool>                                   m_forceRelay;    //联调用：新建的会话不打洞、直接走中转
    bool                                                m_hiRes;         //是否已提高系统定时器精度（只在 IO 线程上读写）
    int64_t                                             m_lastBusyMs;    //最近一次「忙」的时刻（毫秒，只在 IO 线程上读写）
    std::thread                                         m_thread;        //IO 线程
    std::atomic<bool>                                   m_stop;          //是否要求 IO 线程退出
    bool                                                m_started;       //是否已启动（只在调用 Start / Stop 的线程上读写）
    bool                                                m_wsaStarted;    //是否已调用 WSAStartup
    SOCKET                                              m_wake;          //唤醒 socket（UDP，绑定 127.0.0.1 的随机端口）
    uint16_t                                            m_wakePort;      //唤醒 socket 的端口
    std::mutex                                          m_taskMutex;     //保护 m_tasks
    std::deque<EngineTask>                              m_tasks;         //投递给 IO 线程的任务
    std::atomic<bool>                                   m_enabled;       //2301 应答报告本企业开启了 P2P
    std::atomic<size_t>                                 m_sessionCount;  //当前的会话个数
    bool                                                m_loggedIn;      //是否已登录（处理过登录通知、还没处理退出登录；只在 IO 线程上读写）
    std::vector<P2PProbeServer>                         m_probes;        //地址探测服务器（只在 IO 线程上读写）
    std::map<std::string, std::shared_ptr<SessionBox> > m_sessions;      //会话号 → 会话（只在 IO 线程上读写）
    std::map<std::string, LinkImpl*>                    m_links;         //会话号 → 引擎持有的链路引用（只在 IO 线程上读写）
};

P2PEngine::P2PEngine(IP2PEngineHost& host)
    : m_impl(new Impl(host))
{
}

P2PEngine::~P2PEngine()
{
    Stop();
}

bool P2PEngine::Start()
{
    if (m_impl->m_started)
        return true;
    WSADATA data;
    if (::WSAStartup(MAKEWORD(kWsaMajor, kWsaMinor), &data) != 0)
        return false;
    m_impl->m_wsaStarted = true;

    SOCKET wake = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wake == INVALID_SOCKET)
    {
        ::WSACleanup();
        m_impl->m_wsaStarted = false;
        return false;
    }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int len = sizeof(addr);
    u_long nonBlocking = 1;
    if (::bind(wake, (const sockaddr*)&addr, sizeof(addr)) != 0 || ::getsockname(wake, (sockaddr*)&addr, &len) != 0
        || ::ioctlsocket(wake, FIONBIO, &nonBlocking) != 0)
    {
        ::closesocket(wake);
        ::WSACleanup();
        m_impl->m_wsaStarted = false;
        return false;
    }
    m_impl->m_wake = wake;
    m_impl->m_wakePort = ntohs(addr.sin_port);
    //联调用的强制中转：环境变量只在启动时读一次；单测改用 SetForceRelay
    char env[kEnvValueChars] = { 0 };
    const DWORD envLen = ::GetEnvironmentVariableA(kForceRelayEnvName, env, kEnvValueChars);
    if (envLen > 0 && envLen < kEnvValueChars && strcmp(env, kForceRelayOn) == 0)
    {
        m_impl->m_forceRelay = true;
        LOG_INFO("p2p engine: %s is set, sessions go through the relay directly.", kForceRelayEnvName);
    }
    m_impl->m_stop = false;
    m_impl->m_thread = std::thread(&Impl::ThreadMain, m_impl.get());
    m_impl->m_started = true;
    return true;
}

void P2PEngine::Stop()
{
    if (!m_impl->m_started)
        return;
    m_impl->m_stop = true;
    m_impl->Wake();
    if (m_impl->m_thread.joinable())
        m_impl->m_thread.join();
    //IO 线程已结束：在本线程上关闭剩下的会话（发 2305），之后会话都处于终态，业务方手里的链路不再访问引擎
    m_impl->CloseAll(p2p_close_self_logout);
    {
        std::lock_guard<std::mutex> guard(m_impl->m_taskMutex);
        m_impl->m_tasks.clear();
    }
    ::closesocket(m_impl->m_wake);
    m_impl->m_wake = INVALID_SOCKET;
    if (m_impl->m_wsaStarted)
    {
        ::WSACleanup();
        m_impl->m_wsaStarted = false;
    }
    m_impl->m_started = false;
}

void P2PEngine::OnLoggedIn()
{
    EngineTask t;
    t.m_kind = engine_task_logged_in;
    m_impl->Post(t);
}

void P2PEngine::OnLoggingOut()
{
    EngineTask t;
    t.m_kind = engine_task_logging_out;
    m_impl->Post(t);
}

void P2PEngine::OnPurposesChanged()
{
    EngineTask t;
    t.m_kind = engine_task_purposes_changed;
    m_impl->Post(t);
}

void P2PEngine::OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    EngineTask t;
    t.m_kind = engine_task_frame;
    t.m_cmd = cmd;
    t.m_seq = seq;
    t.m_body = body;
    m_impl->Post(t);
}

void P2PEngine::DebugOpen(int64_t peerUid)
{
    m_impl->SendSignal(cmd_p2p_debug_open, P2PSignalCodec::BuildDebugOpen(peerUid));
}

void P2PEngine::SetForceRelay(bool force)
{
    m_impl->m_forceRelay = force;
}

bool P2PEngine::IsEnabled() const
{
    return m_impl->m_enabled;
}

size_t P2PEngine::SessionCount() const
{
    return m_impl->m_sessionCount;
}

} // namespace p2p
