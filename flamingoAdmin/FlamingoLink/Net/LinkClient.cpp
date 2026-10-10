/**
 *  FlamingoLink.dll —— 长连接工作线程的实现，说明见 LinkClient.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Net/LinkClient.h"

#include <chrono>
#include <functional>

#include "FlamingoLink/FlamingoLinkApi.h"
#include "FlamingoLink/LinkLog.h"
#include "FlamingoLink/Net/LinkConnection.h"
#include "../../../flamingoclient/Source/net/Protocol.h"

namespace flamingolink {

namespace {

//收发循环里每次等待可读的时长（毫秒）：也是检查停止标志与订阅变化的间隔
const int kPollSliceMs = 200;

//一秒折合的毫秒数
const ULONGLONG kMsPerSecond = 1000;

//鉴权请求的序列号；之后的请求从它的下一个数开始递增
const uint32_t kAuthSeq = 1;

//重连间隔随机浮动所用的均匀分布区间
const double kRandomLow = 0.0;
const double kRandomHigh = 1.0;

} // anonymous namespace

LinkClient::LinkClient()
    : m_stop(false)
    , m_desired(0)
    , m_generation(0)
    , m_notifyPending(false)
    , m_notifyHwnd(NULL)
    , m_notifyMsg(0)
    , m_random(static_cast<unsigned int>(::GetTickCount()))
{
}

LinkClient::~LinkClient()
{
    Stop();
}

void LinkClient::SetNotifyTarget(HWND hwnd, UINT msg)
{
    std::lock_guard<std::mutex> guard(m_eventMutex);
    m_notifyHwnd = hwnd;
    m_notifyMsg = msg;
}

void LinkClient::Start(const LinkConfig& cfg)
{
    Stop();

    m_stop.store(false);
    m_thread = std::thread(std::bind(&LinkClient::ThreadMain, this, cfg));
}

void LinkClient::Stop()
{
    if (m_thread.joinable())
    {
        m_stop.store(true);
        {
            std::lock_guard<std::mutex> guard(m_waitMutex);
            m_waitCv.notify_all();
        }
        m_thread.join();
    }

    std::lock_guard<std::mutex> guard(m_eventMutex);
    m_events.clear();
    m_notifyPending = false;
}

void LinkClient::SetDesiredEnterprise(int64_t enterpriseId)
{
    m_desired.store(enterpriseId);
}

void LinkClient::RequestResubscribe()
{
    ++m_generation;
}

void LinkClient::DrainEvents(std::vector<LinkEvent>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_eventMutex);
    out.swap(m_events);
    m_notifyPending = false;
}

void LinkClient::PushEvent(const LinkEvent& ev)
{
    HWND hwnd = NULL;
    UINT msg = 0;
    {
        std::lock_guard<std::mutex> guard(m_eventMutex);
        m_events.push_back(ev);
        if (m_notifyPending)
            return;
        m_notifyPending = true;
        hwnd = m_notifyHwnd;
        msg = m_notifyMsg;
    }

    //PostMessage 不会阻塞；窗口已销毁时投递失败，事件留在队列里，Stop 时清掉
    if (::IsWindow(hwnd))
        ::PostMessage(hwnd, msg, 0, 0);
}

void LinkClient::PushState(int state, int attempt, int delayMs)
{
    LinkEvent ev;
    ev.m_kind = link_event_state;
    ev.m_state = state;
    ev.m_attempt = attempt;
    ev.m_delayMs = delayMs;
    PushEvent(ev);
}

bool LinkClient::WaitRetry(int delayMs)
{
    std::unique_lock<std::mutex> lock(m_waitMutex);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    while (!m_stop.load())
    {
        if (m_waitCv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    return !m_stop.load();
}

void LinkClient::ThreadMain(LinkConfig cfg)
{
    LINK_LOG_INFO("adminlink thread started, port: %d, encrypted: %d", cfg.m_port, cfg.m_encrypt ? 1 : 0);

    std::uniform_real_distribution<double> dist(kRandomLow, kRandomHigh);
    int attempt = 0;
    while (!m_stop.load())
    {
        bool authed = false;
        const RunResult r = RunConnection(cfg, authed);
        if (r == run_stopped)
            break;

        if (r == run_expired)
        {
            LINK_LOG_WARNING("adminlink token invalid, stop reconnecting");
            PushState(link_state_expired, 0, 0);
            LinkEvent ev;
            ev.m_kind = link_event_expired;
            PushEvent(ev);
            break;
        }

        //这次连上并鉴权成功过：重连次数从头算
        if (authed)
            attempt = 0;
        ++attempt;

        const int delay = ComputeRetryDelayMs(attempt, dist(m_random));
        LINK_LOG_INFO("adminlink retry #%d in %d ms", attempt, delay);
        PushState(link_state_waiting, attempt, delay);
        if (!WaitRetry(delay))
            break;
    }

    LINK_LOG_INFO("adminlink thread exit");
}

LinkClient::RunResult LinkClient::RunConnection(const LinkConfig& cfg, bool& outAuthed)
{
    outAuthed = false;
    PushState(link_state_connecting, 0, 0);

    LinkConnection conn;
    std::string err;
    if (!conn.Connect(cfg.m_host, cfg.m_port, kConnectTimeoutMs, m_stop, err))
    {
        if (m_stop.load())
            return run_stopped;
        LINK_LOG_WARNING("adminlink connect failed: %s", err.c_str());
        return run_failed;
    }

    if (cfg.m_encrypt && !conn.Handshake(cfg.m_pubkey, err))
    {
        LINK_LOG_ERROR("adminlink handshake failed: %s", err.c_str());
        return run_failed;
    }

    if (!conn.SendFrame(cmd_adminlink_auth, kAuthSeq, BuildAuthRequest(cfg.m_token, cfg.m_clientVersion), err))
    {
        LINK_LOG_WARNING("adminlink send auth failed: %s", err.c_str());
        return run_failed;
    }

    AuthResult auth;
    if (!WaitAuthReply(conn, kAuthSeq, auth))
        return m_stop.load() ? run_stopped : run_failed;

    switch (ClassifyAuth(auth.m_code))
    {
    //鉴权成功：进入下面的收发循环
    case auth_ok:
        break;

    //token 已失效：不再重连
    case auth_expired:
        return run_expired;

    //其余失败：按退避间隔重连
    default:
        LINK_LOG_WARNING("adminlink auth failed, code: %d", auth.m_code);
        return run_failed;
    }

    outAuthed = true;
    LINK_LOG_INFO("adminlink connected, adminid: %lld, enterpriseid: %lld, heartbeat: %d s, timeout: %d s",
                  (long long)auth.m_adminId, (long long)auth.m_enterpriseId, auth.m_heartbeatSec, auth.m_timeoutSec);
    PushState(link_state_connected, 0, 0);

    const ULONGLONG heartbeatMs = static_cast<ULONGLONG>(auth.m_heartbeatSec) * kMsPerSecond;
    const ULONGLONG timeoutMs = static_cast<ULONGLONG>(auth.m_timeoutSec) * kMsPerSecond;
    ULONGLONG lastSend = ::GetTickCount64();
    ULONGLONG lastRecv = lastSend;
    uint32_t seq = kAuthSeq;
    int64_t sent = 0;   //已发出订阅、尚未退订的企业；新连接从 0 开始，于是会按想订阅的企业重新订阅
    uint32_t sentGeneration = m_generation.load();   //上次发订阅时的订阅代次
    std::map<uint32_t, int64_t> pendingSubscribes;

    while (true)
    {
        if (m_stop.load())
            return run_stopped;

        //订阅：想订阅的企业与已发出的不一致、或界面要求过重新订阅，就补发
        const int64_t desired = m_desired.load();
        const uint32_t generation = m_generation.load();
        const SubscribeAction action = DecideSubscription(desired, sent, generation != sentGeneration);
        if (action == subscribe_send)
        {
            ++seq;
            if (!conn.SendFrame(cmd_adminlink_subscribe_online, seq, BuildSubscribeRequest(desired), err))
            {
                LINK_LOG_WARNING("adminlink send subscribe failed: %s", err.c_str());
                return run_failed;
            }
            pendingSubscribes[seq] = desired;
            sent = desired;
            sentGeneration = generation;
            lastSend = ::GetTickCount64();
            LINK_LOG_INFO("adminlink subscribe enterprise %lld", (long long)desired);
        }
        else if (action == subscribe_unsubscribe)
        {
            ++seq;
            if (!conn.SendFrame(cmd_adminlink_unsubscribe_online, seq, BuildUnsubscribeRequest(), err))
            {
                LINK_LOG_WARNING("adminlink send unsubscribe failed: %s", err.c_str());
                return run_failed;
            }
            sent = 0;
            sentGeneration = generation;
            lastSend = ::GetTickCount64();
            LINK_LOG_INFO("adminlink unsubscribe");
            LinkEvent ev;
            ev.m_kind = link_event_unsubscribed;
            PushEvent(ev);
        }

        //心跳：距上次发送满一个心跳间隔就发一次
        if (::GetTickCount64() - lastSend >= heartbeatMs)
        {
            ++seq;
            if (!conn.SendFrame(cmd_heartbeat, seq, std::string(), err))
            {
                LINK_LOG_WARNING("adminlink send heartbeat failed: %s", err.c_str());
                return run_failed;
            }
            lastSend = ::GetTickCount64();
        }

        const int polled = conn.PollRecv(kPollSliceMs, err);
        if (polled < 0)
        {
            LINK_LOG_WARNING("adminlink disconnected: %s", err.c_str());
            return run_failed;
        }

        if (polled > 0)
        {
            lastRecv = ::GetTickCount64();
            uint16_t cmd = 0;
            uint32_t frameSeq = 0;
            std::string body;
            int got = 0;
            while ((got = conn.NextFrame(cmd, frameSeq, body, err)) == 1)
                HandleFrame(cmd, frameSeq, body, pendingSubscribes);
            if (got < 0)
            {
                LINK_LOG_ERROR("adminlink bad frame: %s", err.c_str());
                return run_failed;
            }
        }

        //这么久收不到服务端任何数据（连心跳应答都没有）：连接已断
        if (::GetTickCount64() - lastRecv >= timeoutMs)
        {
            LINK_LOG_WARNING("adminlink no data from server for %d s, reconnect", auth.m_timeoutSec);
            return run_failed;
        }
    }
}

bool LinkClient::WaitAuthReply(LinkConnection& conn, uint32_t seq, AuthResult& out)
{
    const ULONGLONG start = ::GetTickCount64();
    std::string err;
    while (::GetTickCount64() - start < static_cast<ULONGLONG>(kAuthReplyTimeoutMs))
    {
        if (m_stop.load())
            return false;

        const int polled = conn.PollRecv(kPollSliceMs, err);
        if (polled < 0)
        {
            LINK_LOG_WARNING("adminlink disconnected before auth reply: %s", err.c_str());
            return false;
        }

        uint16_t cmd = 0;
        uint32_t frameSeq = 0;
        std::string body;
        int got = 0;
        while ((got = conn.NextFrame(cmd, frameSeq, body, err)) == 1)
        {
            if (cmd != cmd_adminlink_auth || frameSeq != seq)
                continue;
            if (!ParseAuthResponse(body, out))
            {
                LINK_LOG_ERROR("adminlink bad auth reply: %s", body.c_str());
                return false;
            }
            return true;
        }
        if (got < 0)
        {
            LINK_LOG_ERROR("adminlink bad frame before auth reply: %s", err.c_str());
            return false;
        }
    }

    LINK_LOG_WARNING("adminlink auth reply timeout");
    return false;
}

void LinkClient::HandleFrame(uint16_t cmd, uint32_t seq, const std::string& body,
                             std::map<uint32_t, int64_t>& pendingSubscribes)
{
    switch (cmd)
    {
    //心跳应答：收到即说明连接正常，收包时间已在调用方刷新
    case cmd_heartbeat:
        break;

    //订阅应答：按序列号找回请求订阅的企业
    case cmd_adminlink_subscribe_online:
        {
            int code = -1;
            int64_t ent = 0;
            if (!ParseCodeResponse(body, code, ent))
            {
                LINK_LOG_ERROR("adminlink bad subscribe reply: %s", body.c_str());
                break;
            }
            std::map<uint32_t, int64_t>::iterator it = pendingSubscribes.find(seq);
            LinkEvent ev;
            ev.m_kind = link_event_subscribe;
            ev.m_enterpriseId = (it != pendingSubscribes.end()) ? it->second : ent;
            ev.m_code = code;
            if (it != pendingSubscribes.end())
                pendingSubscribes.erase(it);
            if (code != 0)
                LINK_LOG_WARNING("adminlink subscribe enterprise %lld rejected, code: %d", (long long)ev.m_enterpriseId, code);
            PushEvent(ev);
        }
        break;

    //退订应答：界面已在发出退订时清掉了名单，这里不需要动作
    case cmd_adminlink_unsubscribe_online:
        break;

    //在线名单推送
    case cmd_adminlink_online_notify:
        {
            LinkEvent ev;
            ev.m_kind = link_event_online;
            if (!ParseOnlineNotify(body, ev.m_notify))
            {
                LINK_LOG_ERROR("adminlink bad online notify, %d bytes", (int)body.size());
                break;
            }
            PushEvent(ev);
        }
        break;

    //管理端口不会发其它命令；收到就记一条日志后忽略
    default:
        LINK_LOG_WARNING("adminlink unexpected cmd %u", (unsigned)cmd);
        break;
    }
}

} // namespace flamingolink
