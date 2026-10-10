/**
 *  P2P 引擎的端到端联调用例（Suite P2PE，标签 [.live][p2pengine]），P2PEngineLiveTests.cpp
 *
 *  docs/P2P通信方案.md 第 3 期 3f：两个账号用 LiveChatClient 登录隔离的测试服务端，各挂一个真实的 P2PEngine（编进
 *  FlamingoUnit 的同一份源码）。宿主把引擎要发的 2300 段命令经各自的连接发给 chatserver，一条收帧线程把连接上收到的
 *  2300 段帧交给引擎。会话由 A 的引擎发 2306（联调建会话，测试服务端 p2pdebugopen=1）建立，chatserver 给两端各推
 *  一份 2302，之后的 2303 / 2304 / 2305 都经 chatserver 真实转发：
 *    - P2PE1 局域网直连：两端登录后发 2301，应答报告已开启；2306 建会话后两端就绪，路径为局域网直连；数据报往返；
 *            A 的业务方关闭后 B 报告断开（业务结束），两端会话随后被清理；
 *    - P2PE2 强制中转：两端都不打洞，经测试服务端的 imgserver（29903，带协议加密握手，令牌由 chatserver 签发）
 *            就绪，路径为中转；数据报往返；A 退出登录后 B 报告断开（本方退出），两端会话随后被清理。
 *  chatserver 收到的 2304（结果与路径）由运行脚本 tmp/p2ptest/check_server_side.py 从服务端日志核对。
 *
 *  默认不参与常规测试：用例带 [.live] 标签。跑之前用 tmp/p2ptest/setup_p2ptest.py 建环境、start_server.ps1 起测试服务端。
 *
 *      FlamingoUnit.exe "[p2pengine]"
 *
 *  账号：MY0127（5127，作 A）、MY0308（5308，作 B），密码都是 perf1234，同属企业 1。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LiveChatClient.h"
#include "../../FlamingoP2P/Io/P2PEngine.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;
using namespace p2p;

namespace {

//tmp/p2ptest 测试服务端的 chatserver 端口
const unsigned short kP2PTestServerPort = 29900;

//两个人的账号与密码
const char* const kAccountA = "MY0127";
const char* const kAccountB = "MY0308";
const char* const kPassword = "perf1234";

//联调建会话的用途：引擎按它找业务方
const char* const kPurposeTest = "test";

//等待各类事件的最长时间（毫秒）：报到应答；建链（打洞超时 2.5 秒 + 中转最多 10 秒，留余量）；其它
const int kWaitEnabledMs = 5000;
const int kWaitReadyMs = 20000;
const int kWaitEventMs = 5000;

//收帧线程一次等待的时长（毫秒）：单次读超时由 TestConn 决定，这里只是上限
const int kRecvSliceMs = 200;

//轮询间隔（毫秒）
const int kPollMs = 10;

//数据报通道号
const uint16_t kChannel = 1;

//业务方：记录回调，并作为数据报通道的接收端。回调在引擎的 IO 线程上发生，读写一律加锁
class LiveHandler : public IP2PLinkHandler, public IP2PChannelSink
{
public:
    LiveHandler()
        : m_link(NULL)
        , m_ready(0)
        , m_failed(0)
        , m_closed(0)
        , m_closeReason(0)
    {
    }

    ~LiveHandler()
    {
        ReleaseLink();
    }

    virtual void OnLinkReady(const char* featureSessionId, IP2PLink* link) override
    {
        (void)featureSessionId;
        std::lock_guard<std::mutex> guard(m_mutex);
        link->AddRef();
        if (m_link != NULL)
            m_link->Release();
        m_link = link;
        ++m_ready;
    }

    virtual void OnLinkFailed(const char* featureSessionId, int code) override
    {
        (void)featureSessionId;
        (void)code;
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_failed;
    }

    virtual void OnLinkClosed(const char* featureSessionId, int reason) override
    {
        (void)featureSessionId;
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closeReason = reason;
        ++m_closed;
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (channelId == kChannel)
            m_received.push_back(std::string((const char*)data, len));
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    //就绪时交来的链路（归本对象持有）；还没有就绪时为 NULL
    IP2PLink* Link()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_link;
    }

    //放掉持有的链路引用
    void ReleaseLink()
    {
        IP2PLink* link = NULL;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            link = m_link;
            m_link = NULL;
        }
        if (link != NULL)
            link->Release();
    }

    int ReadyCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_ready;
    }

    int FailedCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_failed;
    }

    int ClosedCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_closed;
    }

    int ReceivedCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (int)m_received.size();
    }

    int CloseReason() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_closeReason;
    }

    std::string ReceivedAt(int index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_received[index];
    }

    //最多等 timeoutMs 毫秒，直到 getter 返回的计数达到 atLeast
    bool WaitCount(int (LiveHandler::*getter)() const, int atLeast, int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if ((this->*getter)() >= atLeast)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

private:
    mutable std::mutex       m_mutex;         //保护以下全部成员
    IP2PLink*                m_link;          //就绪时交来的链路（持有一个引用）
    int                      m_ready;         //OnLinkReady 的次数
    int                      m_failed;        //OnLinkFailed 的次数
    int                      m_closed;        //OnLinkClosed 的次数
    int                      m_closeReason;   //最近一次 OnLinkClosed 的原因
    std::vector<std::string> m_received;      //通道 kChannel 上收到的数据块
};

/**
 *  一端：已登录的连接 + 引擎 + 收帧线程。引擎发的命令经连接发给 chatserver（发送加锁：IO 线程与测试线程都会发），
 *  收帧线程把 2300 段的帧交给引擎，其余帧丢弃。
 */
class LiveSide : public IP2PEngineHost
{
public:
    LiveSide()
        : m_engine(*this)
        , m_stop(false)
    {
        memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    }

    ~LiveSide()
    {
        Shutdown();
    }

    //登录、启动引擎与收帧线程，并通知引擎已登录（发 2301）
    void Start(const char* account)
    {
        std::string reason;
        const bool ok = m_client.ConnectAndLogin(kP2PTestServerPort, kTestServerPubKey, account, kPassword, reason);
        INFO("login " << account << " failed: " << reason);
        REQUIRE(ok);
        REQUIRE(net::parseServerPublicKey(kTestServerPubKey, m_serverPubKey));
        REQUIRE(m_engine.Start());
        m_recvThread = std::thread(&LiveSide::RecvProc, this);
        m_engine.OnLoggedIn();
    }

    //停止引擎（发 2305）、断开连接、等收帧线程结束。可重复调用
    void Shutdown()
    {
        m_engine.Stop();
        m_stop = true;
        m_client.Close();
        if (m_recvThread.joinable())
            m_recvThread.join();
        m_handler.ReleaseLink();
    }

    //最多等 timeoutMs 毫秒，直到引擎报告已开启
    bool WaitEnabled(int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (m_engine.IsEnabled())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    //最多等 timeoutMs 毫秒，直到引擎的会话个数等于 count
    bool WaitSessionCount(size_t count, int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (m_engine.SessionCount() == count)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    //以下为 IP2PEngineHost 的实现

    //经本端的连接发一个 2300 段命令
    virtual void SendCommand(uint16_t cmd, const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        m_client.Send(cmd, body);
    }

    //用途 test 交给本端的业务方
    virtual IP2PLinkHandler* FindHandler(const std::string& purpose) override
    {
        return (purpose == kPurposeTest) ? &m_handler : NULL;
    }

    //测试服务端的身份公钥：中转连接据此做协议加密握手
    virtual const unsigned char* GetServerPublicKey() override
    {
        return m_serverPubKey;
    }

    //报到的用途：不报任何业务用途，本组用例只建联调会话（用途 test，服务端不检查）
    virtual void ListPurposes(std::vector<std::string>& purposes) override
    {
        (void)purposes;
    }

    LiveChatClient m_client;    //已登录的连接
    LiveHandler    m_handler;   //用途 test 的业务方
    P2PEngine      m_engine;    //被测引擎

private:
    //收帧线程：2300 段的帧交给引擎
    void RecvProc()
    {
        while (!m_stop)
        {
            uint16_t cmd = 0;
            uint32_t seq = 0;
            std::string body;
            if (!m_client.WaitAny(cmd, seq, body, kRecvSliceMs))
                continue;
            if (cmd >= kP2PCmdFirst && cmd <= kP2PCmdLast)
                m_engine.OnServerFrame(cmd, seq, body);
        }
    }

    unsigned char     m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //测试服务端的身份公钥
    std::mutex        m_sendMutex;          //保护 m_client.Send：加密与写 socket 须在同一把锁内
    std::thread       m_recvThread;         //收帧线程
    std::atomic<bool> m_stop;               //是否要求收帧线程退出
};

//两端登录、报到，A 发 2306 与 B 建会话，等两端就绪
void OpenAndWaitReady(LiveSide& a, LiveSide& b, bool forceRelay)
{
    a.Start(kAccountA);
    b.Start(kAccountB);
    REQUIRE(a.WaitEnabled(kWaitEnabledMs));
    REQUIRE(b.WaitEnabled(kWaitEnabledMs));
    a.m_engine.SetForceRelay(forceRelay);
    b.m_engine.SetForceRelay(forceRelay);
    a.m_engine.DebugOpen(b.m_client.UserId());
    REQUIRE(a.m_handler.WaitCount(&LiveHandler::ReadyCount, 1, kWaitReadyMs));
    REQUIRE(b.m_handler.WaitCount(&LiveHandler::ReadyCount, 1, kWaitReadyMs));
    CHECK(a.m_handler.FailedCount() == 0);
    CHECK(b.m_handler.FailedCount() == 0);
}

//核对路径并做一次数据报往返
void CheckPathAndExchange(LiveSide& a, LiveSide& b, int expectedPath)
{
    IP2PLink* linkA = a.m_handler.Link();
    IP2PLink* linkB = b.m_handler.Link();
    REQUIRE(linkA != NULL);
    REQUIRE(linkB != NULL);
    P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    linkA->GetStats(&stats);
    CHECK(stats.m_path == expectedPath);
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    linkB->GetStats(&stats);
    CHECK(stats.m_path == expectedPath);

    REQUIRE(linkA->OpenChannel(kChannel, p2p_channel_datagram, &a.m_handler) == p2p_send_ok);
    REQUIRE(linkB->OpenChannel(kChannel, p2p_channel_datagram, &b.m_handler) == p2p_send_ok);
    const std::string ping = "live ping from A";
    const std::string pong = "live pong from B";
    CHECK(linkA->Send(kChannel, (const uint8_t*)ping.data(), (uint32_t)ping.size()) == p2p_send_ok);
    REQUIRE(b.m_handler.WaitCount(&LiveHandler::ReceivedCount, 1, kWaitEventMs));
    CHECK(b.m_handler.ReceivedAt(0) == ping);
    CHECK(linkB->Send(kChannel, (const uint8_t*)pong.data(), (uint32_t)pong.size()) == p2p_send_ok);
    REQUIRE(a.m_handler.WaitCount(&LiveHandler::ReceivedCount, 1, kWaitEventMs));
    CHECK(a.m_handler.ReceivedAt(0) == pong);
}

} // namespace

TEST_CASE("P2PE1 真实服务端：2306 建会话，局域网直连就绪、数据报往返、业务方关闭", "[.live][p2pengine]")
{
    LiveSide a;
    LiveSide b;
    OpenAndWaitReady(a, b, false);
    CheckPathAndExchange(a, b, p2p_link_path_lan);

    a.m_handler.Link()->Close(p2p_close_business_end);
    REQUIRE(b.m_handler.WaitCount(&LiveHandler::ClosedCount, 1, kWaitEventMs));
    CHECK(b.m_handler.CloseReason() == p2p_close_business_end);
    CHECK(a.WaitSessionCount(0, kWaitEventMs));
    CHECK(b.WaitSessionCount(0, kWaitEventMs));
    CHECK(a.m_handler.ClosedCount() == 0);
}

TEST_CASE("P2PE2 真实服务端：强制中转经 imgserver 就绪、数据报往返、退出登录", "[.live][p2pengine]")
{
    LiveSide a;
    LiveSide b;
    OpenAndWaitReady(a, b, true);
    CheckPathAndExchange(a, b, p2p_link_path_relay);

    a.m_engine.OnLoggingOut();
    REQUIRE(b.m_handler.WaitCount(&LiveHandler::ClosedCount, 1, kWaitEventMs));
    CHECK(b.m_handler.CloseReason() == p2p_close_self_logout);
    CHECK(a.WaitSessionCount(0, kWaitEventMs));
    CHECK(b.WaitSessionCount(0, kWaitEventMs));
}
