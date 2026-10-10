/**
 *  远程桌面传输（P2P 链路优先、imgserver 中转兜底）的单元测试（Suite RDT，标签 [rdtransport]），RdTransportTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 6 期远程桌面一项（FlamingoMedia/RemoteDesktop/RdTransport.h），链路用假链路：
 *    - RDT1 链路已就绪：走 P2P，在通道号 kRdP2PChannelId 上打开可靠通道；比中转分片大的引擎帧整帧加 4 字节长度一次
 *           交给链路；收到的帧交给上层；链路断开时上层收到一次断开，之后发不出；
 *    - RDT2 本端关闭：立即以「业务结束」关闭 P2P 链路，之后发不出、不再回调；链路的引用保留到传输对象析构；
 *    - RDT3 链路在等待期间才就绪：Connect 等到它，再走 P2P；
 *    - RDT4 建链失败或没有链路槽：不等满 20 秒，立即改连中转；中转不通时连接失败，路径为 none；
 *    - RDT5 等链路期间置取消标志：放弃等待并返回 false，不再连中转（中转端口只监听不应答，连上去要等满握手超时，
 *           用返回耗时区分）；
 *    - RDT6 周期关键帧只在中转路径上需要，走 P2P 时推流器不再定时重发关键帧（见 docs/P2P通信方案.md 第 14.26 节）；
 *    - RDT7 链路实际路径（2026-10-04 P2P 徽章）：走 P2P 时报链路统计里的路径，未连接与关闭后为 0。
 *
 *  框架 Catch2 v3。本文件不使用预编译头：RdTransport.h 带进 <winsock2.h>，必须先于 <windows.h>。
 *
 *  典型用法：FlamingoUnit.exe "[rdtransport]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <windows.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../FlamingoMedia/RemoteDesktop/RdTransport.h"
#include "../net/Protocol.h"
#include "P2PTestFakes.h"

using namespace remotedesktop;
using balloonp2p::P2PLinkBroker;
using balloonp2p::P2PLinkSlot;
using balloonp2p::P2PSteadyNowMs;
using p2ptest::FakeLink;
using p2ptest::WithLength;

namespace {

//中转参数：本机地址与令牌（端口按用例取本机没人监听或只监听不应答的端口）
const char* const kRelayIp = "127.0.0.1";
const char* const kRelayToken = "rd-token-text";

//等待各类事件的最长时间（毫秒）与轮询间隔（毫秒）
const int kWaitMs = 5000;
const int kPollMs = 10;

//RDT1：一帧模拟关键帧的大小（字节），比 imgserver 中转的分片大
const size_t kKeyframeBytes = 3 * 1024 * 1024;

//可辨认数据的步长与模数
const size_t kPatternStep = 7;
const size_t kPatternModulo = 251;

//RDT3：链路在 Connect 开始多久之后才就绪（毫秒），与判断确实等过的下限（毫秒）
const int kLinkLateMs = 200;
const int kLinkLateMinMs = 150;

//RDT5：多久之后置取消标志（毫秒），与取消之后 Connect 必须返回的期限（毫秒）。期限远小于中转握手超时
//kRdRelayConnectTimeoutSec（5 秒）：真的去连了那个只监听不应答的端口，就要等满握手超时
const int kCancelAfterMs = 200;
const int kCancelReturnMs = 2000;

//RDT2：确认没有回调时等待的时长（毫秒）
const int kQuietMs = 100;

//一段内容可辨认的数据
std::string Pattern(size_t n)
{
    std::string s(n, ' ');
    for (size_t i = 0; i < n; ++i)
        s[i] = (char)((i * kPatternStep) % kPatternModulo);
    return s;
}

/**
 *  上层回调：记录收到的引擎帧与断开。
 */
class EngineFrameRecorder : public RdRelayClientHandler
{
public:
    EngineFrameRecorder()
        : m_closed(0)
    {
    }

    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_frames.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnRelayClosed() override
    {
        ++m_closed;
    }

    size_t FrameCount()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_frames.size();
    }

    std::string FrameAt(size_t i)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_frames[i];
    }

    std::mutex               m_mutex;    //保护 m_frames
    std::vector<std::string> m_frames;   //收到的引擎帧
    std::atomic<int>         m_closed;   //报告断开的次数
};

bool WaitFrames(EngineFrameRecorder& r, size_t count)
{
    for (int waited = 0; waited <= kWaitMs; waited += kPollMs)
    {
        if (r.FrameCount() >= count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

bool WaitClosed(EngineFrameRecorder& r, int atLeast)
{
    for (int waited = 0; waited <= kWaitMs; waited += kPollMs)
    {
        if (r.m_closed >= atLeast)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//本机一个当前没有监听的 TCP 端口（调用方已 WSAStartup）
int UnusedTcpPort()
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int len = sizeof(addr);
    ::bind(s, (const sockaddr*)&addr, sizeof(addr));
    ::getsockname(s, (sockaddr*)&addr, &len);
    ::closesocket(s);
    return ntohs(addr.sin_port);
}

/**
 *  只监听、从不应答的 TCP 端口：连接能建立（进了监听队列），但握手帧永远等不到回应。
 */
class SilentListener
{
public:
    SilentListener()
        : m_sock(INVALID_SOCKET)
        , m_port(0)
    {
        m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        int len = sizeof(addr);
        ::bind(m_sock, (const sockaddr*)&addr, sizeof(addr));
        ::listen(m_sock, SOMAXCONN);
        ::getsockname(m_sock, (sockaddr*)&addr, &len);
        m_port = ntohs(addr.sin_port);
    }

    ~SilentListener()
    {
        if (m_sock != INVALID_SOCKET)
            ::closesocket(m_sock);
    }

    int Port() const { return m_port; }

private:
    SOCKET m_sock;   //监听套接字
    int    m_port;   //监听端口
};

//按链路槽与中转端口填传输参数（服务端公钥为空：中转明文）
RdTransportParams MakeParams(const std::shared_ptr<P2PLinkSlot>& slot, int relayPort)
{
    RdTransportParams p;
    p.SetRelay("rd-unit", kRelayIp, relayPort, kRelayToken, NULL);
    p.m_slot = slot;
    return p;
}

//在 RDT3 / RDT5 里延迟执行的动作
void LinkReadyLater(P2PLinkBroker* broker, const char* sessionId, FakeLink* link)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kLinkLateMs));
    broker->OnLinkReady(sessionId, link);
}

void CancelLater(std::atomic<bool>* cancel)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kCancelAfterMs));
    cancel->store(true);
}

/**
 *  测试期间的 Winsock 初始化。
 */
class WinsockScope
{
public:
    WinsockScope()
    {
        WSADATA data;
        m_ok = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }

    ~WinsockScope()
    {
        if (m_ok)
            ::WSACleanup();
    }

    bool m_ok;   //WSAStartup 是否成功
};

} // namespace

TEST_CASE("RDT1 链路已就绪：走 P2P 帧通道，整帧收发，链路断开时报告断开", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    //假链路在业务方之前声明：链路槽在析构时关闭它持有的链路，假链路必须比所有槽活得久
    FakeLink link;
    P2PLinkBroker broker(kRdP2PPurpose);
    broker.OnLinkReady("rd-s1", &link);
    RdTransportParams params = MakeParams(broker.ClaimSlot("rd-s1"), UnusedTcpPort());
    EngineFrameRecorder rec;
    RdTransport transport;
    REQUIRE(transport.Connect(params, &rec, NULL));
    CHECK(transport.Path() == RdTransport::rd_path_p2p);
    CHECK(transport.IsConnected());
    REQUIRE(link.m_channels.size() == 1);
    CHECK(link.m_channels[0] == kRdP2PChannelId);
    CHECK(link.m_kinds[0] == p2p::p2p_channel_reliable);

    //发送：比中转分片大的关键帧整帧加 4 字节长度，一次交给链路
    const std::string key = Pattern(kKeyframeBytes);
    CHECK(transport.SendEngineFrame(reinterpret_cast<const unsigned char*>(key.data()), key.size()));
    CHECK(link.SentCopy() == WithLength(key));
    CHECK(link.m_sentChannels.size() == 1);

    //接收：对端发来的控制命令交给上层
    const std::string input = "mouse-input";
    const std::string wire = WithLength(input);
    params.m_slot->OnData(kRdP2PChannelId, reinterpret_cast<const uint8_t*>(wire.data()), (uint32_t)wire.size());
    REQUIRE(WaitFrames(rec, 1));
    CHECK(rec.FrameAt(0) == input);

    //链路断开：上层收到一次断开，之后发不出
    broker.OnLinkClosed("rd-s1", p2p_close_link_timeout);
    REQUIRE(WaitClosed(rec, 1));
    CHECK_FALSE(transport.IsConnected());
    CHECK_FALSE(transport.SendEngineFrame(reinterpret_cast<const unsigned char*>(input.data()), input.size()));
    transport.Close();
    CHECK(transport.Path() == RdTransport::rd_path_none);
    CHECK(rec.m_closed == 1);
}

TEST_CASE("RDT2 本端关闭：立即以业务结束关闭 P2P 链路，之后不再回调", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    FakeLink link;
    P2PLinkBroker broker(kRdP2PPurpose);
    broker.OnLinkReady("rd-s2", &link);
    std::shared_ptr<P2PLinkSlot> slot = broker.ClaimSlot("rd-s2");
    EngineFrameRecorder rec;
    {
        RdTransport transport;
        REQUIRE(transport.Connect(MakeParams(slot, UnusedTcpPort()), &rec, NULL));
        REQUIRE(transport.Path() == RdTransport::rd_path_p2p);
        transport.Close();
        CHECK(link.m_closes == 1);
        CHECK(link.m_closeReason == p2p_close_business_end);
        CHECK(slot->State() == P2PLinkSlot::slot_closed);
        const std::string frame = "after-close";
        CHECK_FALSE(transport.SendEngineFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));

        //关闭之后到达的数据不再交给上层
        const std::string wire = WithLength(frame);
        slot->OnData(kRdP2PChannelId, reinterpret_cast<const uint8_t*>(wire.data()), (uint32_t)wire.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(kQuietMs));
        CHECK(rec.FrameCount() == 0);
        CHECK(rec.m_closed == 0);

        //链路的引用保留到槽的最后一个持有者释放（传输对象还持有它）
        slot.reset();
        CHECK(link.m_refs == 1);
    }
    //传输对象析构：放掉引用，不再重复关闭
    CHECK(link.m_refs == 0);
    CHECK(link.m_closes == 1);
}

TEST_CASE("RDT3 链路在等待期间才就绪：Connect 等到它再走 P2P", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    FakeLink link;
    P2PLinkBroker broker(kRdP2PPurpose);
    //业务先认领：槽在等链路
    RdTransportParams params = MakeParams(broker.ClaimSlot("rd-s3"), UnusedTcpPort());
    REQUIRE(params.m_slot->State() == P2PLinkSlot::slot_waiting);
    EngineFrameRecorder rec;
    RdTransport transport;
    std::thread later(&LinkReadyLater, &broker, "rd-s3", &link);
    const int64_t start = P2PSteadyNowMs();
    const bool ok = transport.Connect(params, &rec, NULL);
    const int64_t elapsed = P2PSteadyNowMs() - start;
    later.join();
    REQUIRE(ok);
    CHECK(transport.Path() == RdTransport::rd_path_p2p);
    CHECK(elapsed >= kLinkLateMinMs);
    CHECK(elapsed < kWaitMs);
    transport.Close();
}

TEST_CASE("RDT4 建链失败或没有链路槽：立即改连中转，中转不通时连接失败", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    P2PLinkBroker broker(kRdP2PPurpose);
    EngineFrameRecorder rec;

    //建链失败：不等满 kRdP2PLinkWaitMs，立即改连中转；中转端口没人监听，连接失败
    {
        RdTransportParams params = MakeParams(broker.ClaimSlot("rd-s4"), UnusedTcpPort());
        broker.OnLinkFailed("rd-s4", error_code_p2p_peer_offline);
        RdTransport transport;
        const int64_t start = P2PSteadyNowMs();
        CHECK_FALSE(transport.Connect(params, &rec, NULL));
        CHECK(P2PSteadyNowMs() - start < kRdP2PLinkWaitMs);
        CHECK(transport.Path() == RdTransport::rd_path_none);
        CHECK_FALSE(transport.IsConnected());
    }

    //没有链路槽：直接连中转（与第 6 期之前相同）
    {
        RdTransportParams params = MakeParams(std::shared_ptr<P2PLinkSlot>(), UnusedTcpPort());
        RdTransport transport;
        CHECK_FALSE(transport.Connect(params, &rec, NULL));
        CHECK(transport.Path() == RdTransport::rd_path_none);
    }
    CHECK(rec.m_closed == 0);
}

TEST_CASE("RDT5 等链路期间取消：放弃等待，不再连中转", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    P2PLinkBroker broker(kRdP2PPurpose);
    SilentListener silent;
    //链路一直不来；中转端口只监听不应答，真去连就要等满握手超时
    RdTransportParams params = MakeParams(broker.ClaimSlot("rd-s5"), silent.Port());
    EngineFrameRecorder rec;
    RdTransport transport;
    std::atomic<bool> cancel(false);
    std::thread canceller(&CancelLater, &cancel);
    const int64_t start = P2PSteadyNowMs();
    const bool ok = transport.Connect(params, &rec, &cancel);
    const int64_t elapsed = P2PSteadyNowMs() - start;
    canceller.join();
    CHECK_FALSE(ok);
    CHECK(transport.Path() == RdTransport::rd_path_none);
    CHECK(elapsed >= kCancelAfterMs);
    CHECK(elapsed < kCancelReturnMs);
}

TEST_CASE("RDT6 周期关键帧只在中转路径上需要：P2P 帧通道可靠有序，不再定时重发 4 MB 的关键帧", "[rdtransport]")
{
    //中转路径：控制端晚连上中转时，之前的帧被中转丢掉，要靠周期关键帧重新对齐
    CHECK(RdTransport::NeedsPeriodicKeyframes(RdTransport::rd_path_relay));
    //P2P 帧通道可靠有序，未打开前的数据也会暂存补交；控制端连上后还会主动请求一次关键帧。
    //定时重发的 4 MB 关键帧只会白占带宽，每次让其后的增量帧晚到 1 秒多（2026-10-04 实测，见 docs/P2P通信方案.md 第 14.26 节）
    CHECK_FALSE(RdTransport::NeedsPeriodicKeyframes(RdTransport::rd_path_p2p));
    //还没连上：与原来一样按周期重发（推流器只在连上后才发帧，取值不影响行为，只保证判断有确定结果）
    CHECK(RdTransport::NeedsPeriodicKeyframes(RdTransport::rd_path_none));
}

TEST_CASE("RDT7 链路实际路径（P2P 徽章）：走 P2P 时报链路统计里的路径，未连接与关闭后为 0", "[rdtransport]")
{
    WinsockScope ws;
    REQUIRE(ws.m_ok);
    FakeLink link;
    link.m_statsPath = p2p::p2p_link_path_punched;
    P2PLinkBroker broker(kRdP2PPurpose);
    broker.OnLinkReady("rd-s7", &link);
    RdTransportParams params = MakeParams(broker.ClaimSlot("rd-s7"), UnusedTcpPort());
    EngineFrameRecorder rec;
    RdTransport transport;
    CHECK(transport.LinkPath() == 0);
    REQUIRE(transport.Connect(params, &rec, NULL));
    CHECK(transport.LinkPath() == p2p::p2p_link_path_punched);
    link.m_statsPath = p2p::p2p_link_path_relay;
    CHECK(transport.LinkPath() == p2p::p2p_link_path_relay);
    transport.Close();
    CHECK(transport.LinkPath() == 0);
}
