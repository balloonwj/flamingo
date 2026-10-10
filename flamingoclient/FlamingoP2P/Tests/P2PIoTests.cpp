/**
 *  P2P IO 层的单元测试（Suite P2PIO，标签 [p2pio]），P2PIoTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 3 期 3d 的 IO 部分：同一进程里开两个 P2PEngine，用真实 UDP 在本机互连，
 *  chatserver 的转发由测试代码模拟（假宿主把一端发出的 2303 / 2305 改成服务端转发的格式投给另一端）：
 *    - P2PIO1 两端收到 2301 应答后报告已开启；收到 2302 后建链就绪（本机网卡地址，路径为局域网直连），各发一次
 *             成功的 2304；打开同一数据报通道后双向收发；一端业务方关闭后对方报告断开（业务结束），本方不报告，
 *             两端的会话随后都被清理；
 *    - P2PIO2 对方不应答（2303 不转发）：按信令时限判失败，业务方收到 OnLinkFailed，发一次 2305（建链失败）；
 *    - P2PIO3 退出登录：就绪后一端 OnLoggingOut，发 2305（本方退出），对方报告断开，本端会话清零；
 *    - P2PIO4 停止引擎时业务方仍持有链路：Stop 发 2305，之后经链路发送返回已关闭，释放链路安全；
 *    - P2PIO5 网卡枚举与地址解析：本机至少有一个可用的本机候选地址，端口取传入的值；IP 文本与 localhost 能解析，
 *             空串不能；
 *    - P2PIO6 UDP socket：绑定随机端口，经 127.0.0.1 互发，没有数据时立即返回 -1，关闭后发送失败。
 *  回落中转（第 3e 步）接在本机假中转服务上（明文、不握手；按 1861 报到配对，两端都报到后才转发 1862，与 imgserver
 *  的规则相同）：
 *    - P2PIO7 两个引擎强制中转：经假中转建链就绪，路径为中转；假中转收到的两个报到带着各自 2302 里的令牌；数据报
 *             往返；一端业务方关闭后对方报告断开，两端会话随后被清理；
 *    - P2PIO8 中转端口无人监听：会话按建链失败处理（OnLinkFailed，至少一端发出原因为建链失败的 2305）；
 *    - P2PIO9 报到被拒（假中转回 code 3）：同样按建链失败处理；
 *    - P2PIO10 就绪之后中转服务关闭连接：两端报告断开（链路超时）。
 *  可靠通道（第 4b 步）：
 *    - P2PIO11 本机 UDP 直连：可靠通道传 32 MiB，逐字节校验、按序送达；发送队列满时等可写通知再发；记下吞吐；
 *    - P2PIO12 强制中转经假中转：可靠通道传 16 MiB，同样逐字节校验（中转连接的非阻塞发送缓冲与可写通知）。
 *  成批发送（第 4e 步）：
 *    - P2PIO14 （第 5 期）用途没有业务方登记（如对端程序版本还不支持该业务走 P2P）：链路就绪后两端立即关闭，
 *              发原因为业务结束的 2305，会话随后被清理；联调用途 test 不受影响（P2PIO1 等）。
 *    - P2PIO13 UDP socket 的 SendBatch：8 个满载报文加 1 个短报文一次交出，接收端收到 9 个独立的报文，长度与内容
 *              都与发送时一致；本机支持发送分段卸载时 BatchSegmentBytes 为 1200（不支持时为 0，SendBatch 逐个发送）。
 *
 *  需要本机至少有一块已连接的网卡（不含回环与链路本地地址），否则 P2PIO1、P2PIO3、P2PIO4 无法建链。
 *  典型用法：FlamingoUnit.exe "[p2pio]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../../Source/net/FrameCodec.h"
#include "../../Source/net/Protocol.h"
#include "../Io/P2PAdapters.h"
#include "../Io/P2PEngine.h"
#include "../Io/P2PUdpPort.h"

using namespace p2p;

namespace {

//两端的 userid 与会话号
const int64_t kUidA = 7101;
const int64_t kUidB = 7102;
const char* const kSessionIo1 = "p2p-7101-7102-io1";
const char* const kSessionIo2 = "p2p-7101-7102-io2";
const char* const kSessionIo3 = "p2p-7101-7102-io3";
const char* const kSessionIo4 = "p2p-7101-7102-io4";

//测试用的用途与业务会话号
const char* const kPurpose = "test";
const char* const kFeatureSessionId = "io-feature";

//会话参数：打洞超时；保活间隔与断开判定取小一些，让用例跑得快
const int64_t kConnectTimeoutMs = 3000;
const int64_t kKeepaliveMs = 1000;
const int64_t kLinkTimeoutMs = 5000;

//P2PIO2 的打洞超时：信令时限为探测预留 + 打洞超时 + 应答方额外等待，取小值让用例尽快结束
const int64_t kShortConnectTimeoutMs = 500;

//等待事件的最长时间（毫秒）：建链与断开；P2PIO2 的失败（信令时限约 2.5 秒）
const int kWaitReadyMs = 5000;
const int kWaitEventMs = 3000;
const int kWaitFailMs = 6000;

//轮询间隔（毫秒）
const int kPollMs = 10;

//数据报通道号
const uint16_t kChannel = 3;

//P2PIO5 用的端口
const uint16_t kAnyPort = 5000;

//127.0.0.1（主机字节序）
const uint32_t kLoopbackIp = 0x7F000001;

//P2PIO6 一次收包的缓冲（字节）
const int kRecvBufBytes = 2048;

//2302 里默认的中转端口（不走中转的用例用不到）
const uint16_t kDefaultRelayPort = 29903;

//P2PIO7~10 的会话号
const char* const kSessionIo7 = "p2p-7101-7102-io7";
const char* const kSessionIo8 = "p2p-7101-7102-io8";
const char* const kSessionIo9 = "p2p-7101-7102-io9";
const char* const kSessionIo10 = "p2p-7101-7102-io10";

//两端的中转令牌（假中转不验签，只原样记下）
const char* const kTokenA = "token-of-a";
const char* const kTokenB = "token-of-b";

//假中转：每次 select 的等待（毫秒）与一次读取的字节数
const int kFakeRelayPollMs = 20;

//毫秒与微秒的换算（select 的超时以微秒计）
const int kMicrosPerMilli = 1000;
const int kFakeRelayChunkBytes = 4096;

//报到被拒时假中转回的错误码（报文非法）
const int kHelloRejectCode = 3;

//等待中转失败的最长时间（毫秒）：连接被拒在本机约 1 秒，中转连接本身最多 kP2PRelayConnectTimeoutMs
const int kWaitRelayFailMs = 8000;

//P2PIO11 / P2PIO12 的会话号、可靠通道号、传输量与每次交给 Send 的块大小
const char* const kSessionIo11 = "p2p-7101-7102-io11";
const char* const kSessionIo12 = "p2p-7101-7102-io12";
const uint16_t kReliableChannel = 5;
const size_t kUdpTransferBytes = 32 * 1024 * 1024;
const size_t kRelayTransferBytes = 16 * 1024 * 1024;
const size_t kSendChunkBytes = 256 * 1024;

//等一次传输完成的最长时间（毫秒）：Debug 版在本机也要好几秒，留足余量
const int kWaitTransferMs = 120000;

//传输数据的内容：按偏移生成，接收端逐字节核对
const uint64_t kPatternMul = 31;
const uint64_t kPatternAdd = 7;
const uint64_t kPatternMask = 0xFF;

//一兆字节与一秒的换算
const double kBytesPerMiB = 1024.0 * 1024.0;
const double kMillisPerSecondF = 1000.0;

//解析一段 JSON 对象
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//紧凑格式的 JSON 文本
std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//一个按服务端格式写的 2302 包体
std::string NotifyJson(const char* sessionId, int role, int64_t peerUid, int64_t connectTimeoutMs,
                       uint16_t relayPort = kDefaultRelayPort, const char* token = "token-text",
                       const char* purpose = kPurpose)
{
    Json::Value v;
    v["p2psessionid"] = sessionId;
    v["role"] = role;
    v["purpose"] = purpose;
    v["featuresessionid"] = kFeatureSessionId;
    v["peeruid"] = std::to_string(peerUid);
    v["peerdeviceid"] = "iotest-peer";
    v["relayip"] = "127.0.0.1";
    v["relayport"] = relayPort;
    v["token"] = token;
    v["connecttimeoutms"] = (Json::Int64)connectTimeoutMs;
    v["keepalivems"] = (Json::Int64)kKeepaliveMs;
    v["linktimeoutms"] = (Json::Int64)kLinkTimeoutMs;
    return Compact(v);
}

//2301 的应答：本企业开启，没有地址探测服务器（本机互连只用本机网卡地址）
const char* const kRegisterResponse = "{\"code\":0,\"enabled\":true}";

//业务方：记录回调，并作为数据报通道的接收端。回调在引擎的 IO 线程上发生，读写一律加锁
class RecordingHandler : public IP2PLinkHandler, public IP2PChannelSink
{
public:
    RecordingHandler()
        : m_link(NULL)
        , m_ready(0)
        , m_failed(0)
        , m_closed(0)
        , m_failCode(0)
        , m_closeReason(0)
    {
    }

    ~RecordingHandler()
    {
        ReleaseLink();
    }

    virtual void OnLinkReady(const char* featureSessionId, IP2PLink* link) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_featureSessionId = featureSessionId;
        link->AddRef();
        if (m_link != NULL)
            m_link->Release();
        m_link = link;
        ++m_ready;
    }

    virtual void OnLinkFailed(const char* featureSessionId, int code) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_featureSessionId = featureSessionId;
        m_failCode = code;
        ++m_failed;
    }

    virtual void OnLinkClosed(const char* featureSessionId, int reason) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_featureSessionId = featureSessionId;
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

    //就绪时交来的链路（已 AddRef，归本对象持有）；还没有就绪时为 NULL
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

    int FailCode() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_failCode;
    }

    int CloseReason() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_closeReason;
    }

    std::string FeatureSessionId() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_featureSessionId;
    }

    //第 index 个收到的数据块（下标须有效）
    std::string ReceivedAt(int index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_received[index];
    }

    //最多等 timeoutMs 毫秒，直到 getter 返回的计数达到 atLeast
    bool WaitCount(int (RecordingHandler::*getter)() const, int atLeast, int timeoutMs) const
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
    mutable std::mutex       m_mutex;              //保护以下全部成员
    IP2PLink*                m_link;               //就绪时交来的链路（持有一个引用）
    int                      m_ready;              //OnLinkReady 的次数
    int                      m_failed;             //OnLinkFailed 的次数
    int                      m_closed;             //OnLinkClosed 的次数
    int                      m_failCode;           //最近一次 OnLinkFailed 的错误码
    int                      m_closeReason;        //最近一次 OnLinkClosed 的原因
    std::string              m_featureSessionId;   //最近一次回调带的业务会话号
    std::vector<std::string> m_received;           //通道 kChannel 上收到的数据块
};

//记录下来的一个信令
struct SentSignal
{
    uint16_t    m_cmd;    //命令号
    std::string m_body;   //包体
};

/**
 *  假宿主：记录引擎发出的信令，按服务端的做法把 2303 加上 fromuid 后投给对端引擎、把 2305 原样投给对端引擎；
 *  按用途 test 交出业务方。SendCommand 在引擎的 IO 线程或测试线程上调用，一律加锁。
 */
class FakeEngineHost : public IP2PEngineHost
{
public:
    FakeEngineHost(int64_t uid, RecordingHandler* handler)
        : m_uid(uid)
        , m_handler(handler)
        , m_peer(NULL)
        , m_forwardSignals(true)
    {
    }

    //设置对端引擎（借用）；传 NULL 断开转发
    void SetPeer(P2PEngine* peer)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_peer = peer;
    }

    //是否把 2303 转给对端（P2PIO2 模拟对方不应答时关掉）
    void SetForwardSignals(bool forward)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_forwardSignals = forward;
    }

    virtual void SendCommand(uint16_t cmd, const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        SentSignal s;
        s.m_cmd = cmd;
        s.m_body = body;
        m_sent.push_back(s);
        if (m_peer == NULL)
            return;
        //2303：服务端校验后加上发送者 userid 转给对方
        if (cmd == cmd_p2p_signal && m_forwardSignals)
        {
            Json::Value v;
            if (ParseJson(body, v))
            {
                v["fromuid"] = std::to_string(m_uid);
                m_peer->OnServerFrame(cmd_p2p_signal, 0, Compact(v));
            }
        }
        //2305：服务端结束会话并推给对方
        else if (cmd == cmd_p2p_close)
        {
            m_peer->OnServerFrame(cmd_p2p_close, 0, body);
        }
    }

    virtual IP2PLinkHandler* FindHandler(const std::string& purpose) override
    {
        return (purpose == kPurpose) ? m_handler : NULL;
    }

    //假中转不握手：返回 NULL，中转连接全程明文
    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    //报到的用途：本组用例只用 test（联调用途，服务端不检查），照样报上
    virtual void ListPurposes(std::vector<std::string>& purposes) override
    {
        purposes.push_back(kPurpose);
    }

    //命令号为 cmd 的信令条数
    int CountSent(uint16_t cmd) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        int n = 0;
        for (size_t i = 0; i < m_sent.size(); ++i)
        {
            if (m_sent[i].m_cmd == cmd)
                ++n;
        }
        return n;
    }

    //最后一条命令号为 cmd 的信令解析出的 JSON；没有时返回 false
    bool LastSent(uint16_t cmd, Json::Value& out) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = m_sent.size(); i > 0; --i)
        {
            if (m_sent[i - 1].m_cmd == cmd)
                return ParseJson(m_sent[i - 1].m_body, out);
        }
        return false;
    }

    //最多等 timeoutMs 毫秒，直到命令号为 cmd 的信令达到 atLeast 条
    bool WaitSent(uint16_t cmd, int atLeast, int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (CountSent(cmd) >= atLeast)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

private:
    mutable std::mutex      m_mutex;            //保护以下全部成员
    int64_t                 m_uid;              //本端 userid
    RecordingHandler*       m_handler;          //用途 test 的业务方（借用）
    P2PEngine*              m_peer;             //对端引擎（借用）
    bool                    m_forwardSignals;   //是否把 2303 转给对端
    std::vector<SentSignal> m_sent;             //引擎发出的全部信令
};

//最多等 timeoutMs 毫秒，直到引擎的会话个数等于 count
bool WaitSessionCount(const P2PEngine& engine, size_t count, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (engine.SessionCount() == count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//最多等 timeoutMs 毫秒，直到引擎报告已开启
bool WaitEnabled(const P2PEngine& engine, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (engine.IsEnabled())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

/**
 *  一对本机互连的引擎：A 为发起方，B 为应答方。构造时启动两端并互设对端，析构时先断开转发再停止。
 */
struct EnginePair
{
    RecordingHandler m_handlerA;   //A 端业务方
    RecordingHandler m_handlerB;   //B 端业务方
    FakeEngineHost   m_hostA;      //A 端宿主
    FakeEngineHost   m_hostB;      //B 端宿主
    P2PEngine        m_engineA;    //A 端引擎
    P2PEngine        m_engineB;    //B 端引擎

    EnginePair()
        : m_hostA(kUidA, &m_handlerA)
        , m_hostB(kUidB, &m_handlerB)
        , m_engineA(m_hostA)
        , m_engineB(m_hostB)
    {
        m_hostA.SetPeer(&m_engineB);
        m_hostB.SetPeer(&m_engineA);
    }

    ~EnginePair()
    {
        m_hostA.SetPeer(NULL);
        m_hostB.SetPeer(NULL);
        m_engineA.Stop();
        m_engineB.Stop();
        m_handlerA.ReleaseLink();
        m_handlerB.ReleaseLink();
    }

    //启动两端并各收一次 2301 应答
    void StartBoth()
    {
        REQUIRE(m_engineA.Start());
        REQUIRE(m_engineB.Start());
        m_engineA.OnServerFrame(cmd_p2p_register, 1, kRegisterResponse);
        m_engineB.OnServerFrame(cmd_p2p_register, 1, kRegisterResponse);
    }

    //给两端推同一个会话的 2302
    void NotifyBoth(const char* sessionId)
    {
        m_engineA.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(sessionId, p2p_role_controlling, kUidB,
                                                                       kConnectTimeoutMs));
        m_engineB.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(sessionId, p2p_role_controlled, kUidA,
                                                                       kConnectTimeoutMs));
    }

    //推 2302 并等两端就绪
    void ConnectBoth(const char* sessionId)
    {
        NotifyBoth(sessionId);
        REQUIRE(m_handlerA.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        REQUIRE(m_handlerB.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        REQUIRE(m_handlerA.Link() != NULL);
        REQUIRE(m_handlerB.Link() != NULL);
    }
};

/**
 *  本机假中转服务：监听 127.0.0.1 的随机端口，最多接受两条连接；连接发来 1861 报到时记下令牌并回应答（code 为
 *  m_helloCode），两条连接都报到成功之后把一端的 1862 原样转给另一端，报到之前的 1862 丢弃；一条连接断开时关闭另一条。
 *  明文、不握手（引擎的宿主不给服务端公钥）。一个线程用 select 驱动全部 socket。
 */
class FakeRelayServer
{
public:
    explicit FakeRelayServer(int helloCode)
        : m_listen(INVALID_SOCKET)
        , m_port(0)
        , m_helloCode(helloCode)
        , m_stop(false)
        , m_dataForwarded(0)
    {
        for (int i = 0; i < kSlots; ++i)
        {
            m_clients[i] = INVALID_SOCKET;
            m_joined[i] = false;
        }
    }

    ~FakeRelayServer()
    {
        Stop();
    }

    //开始监听并起线程
    bool Start()
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen == INVALID_SOCKET)
            return false;
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(kLoopbackIp);
        addr.sin_port = 0;
        int len = sizeof(addr);
        if (::bind(m_listen, (const sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(m_listen, kSlots) != 0
            || ::getsockname(m_listen, (sockaddr*)&addr, &len) != 0)
            return false;
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread(&FakeRelayServer::Run, this);
        return true;
    }

    //停止：关闭全部连接与监听 socket，等线程结束
    void Stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        for (int i = 0; i < kSlots; ++i)
            CloseSlot(i);
        if (m_listen != INVALID_SOCKET)
        {
            ::closesocket(m_listen);
            m_listen = INVALID_SOCKET;
        }
    }

    //监听的端口
    uint16_t Port() const
    {
        return m_port;
    }

    //收到的报到令牌（按报到先后）
    std::vector<std::string> Tokens()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_tokens;
    }

    //转发的 1862 帧个数
    int DataForwarded() const
    {
        return m_dataForwarded;
    }

private:
    //可接受的连接数：一对一中转只需要两条
    static const int kSlots = 2;

    void Run()
    {
        while (!m_stop)
        {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(m_listen, &readable);
            for (int i = 0; i < kSlots; ++i)
            {
                if (m_clients[i] != INVALID_SOCKET)
                    FD_SET(m_clients[i], &readable);
            }
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = kFakeRelayPollMs * kMicrosPerMilli;
            if (::select(0, &readable, NULL, NULL, &tv) <= 0)
                continue;
            if (FD_ISSET(m_listen, &readable))
                Accept();
            for (int i = 0; i < kSlots; ++i)
            {
                if (m_clients[i] != INVALID_SOCKET && FD_ISSET(m_clients[i], &readable))
                    ReadSlot(i);
            }
        }
    }

    void Accept()
    {
        SOCKET c = ::accept(m_listen, NULL, NULL);
        if (c == INVALID_SOCKET)
            return;
        for (int i = 0; i < kSlots; ++i)
        {
            if (m_clients[i] == INVALID_SOCKET)
            {
                m_clients[i] = c;
                m_buffers[i].clear();
                m_joined[i] = false;
                return;
            }
        }
        ::closesocket(c);
    }

    void CloseSlot(int i)
    {
        if (m_clients[i] != INVALID_SOCKET)
        {
            ::closesocket(m_clients[i]);
            m_clients[i] = INVALID_SOCKET;
        }
        m_joined[i] = false;
    }

    void SendFrame(int i, uint16_t cmd, const std::string& body)
    {
        std::string frame;
        if (m_clients[i] == INVALID_SOCKET || !net::encodeFrame(cmd, 0, body, frame))
            return;
        ::send(m_clients[i], frame.data(), (int)frame.size(), 0);
    }

    void ReadSlot(int i)
    {
        char buf[kFakeRelayChunkBytes];
        const int n = ::recv(m_clients[i], buf, sizeof(buf), 0);
        if (n <= 0)
        {
            //一端断开：两条一起关闭（imgserver 的做法）
            for (int k = 0; k < kSlots; ++k)
                CloseSlot(k);
            return;
        }
        m_buffers[i].append(buf, (size_t)n);
        while (m_clients[i] != INVALID_SOCKET)
        {
            frame_header header;
            std::string body;
            size_t consumed = 0;
            const net::frame_decode_result r = net::decodeFrame(m_buffers[i].data(), m_buffers[i].size(), header, body,
                                                                consumed, MAX_BODY_FILESERVER);
            if (r != net::frame_decode_ok)
                return;
            m_buffers[i].erase(0, consumed);
            if (header.cmd == cmd_p2p_relay_hello)
            {
                Json::Value hello;
                if (ParseJson(body, hello))
                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    m_tokens.push_back(hello["token"].asString());
                }
                Json::Value ack;
                ack["code"] = m_helloCode;
                ack["msg"] = "";
                SendFrame(i, cmd_p2p_relay_hello, Compact(ack));
                if (m_helloCode == 0)
                    m_joined[i] = true;
                else
                    CloseSlot(i);
            }
            else if (header.cmd == cmd_p2p_relay_data)
            {
                const int other = 1 - i;
                if (m_joined[i] && m_joined[other])
                {
                    SendFrame(other, cmd_p2p_relay_data, body);
                    ++m_dataForwarded;
                }
            }
        }
    }

    SOCKET                   m_listen;              //监听 socket
    uint16_t                 m_port;                //监听端口
    int                      m_helloCode;           //报到应答的 code
    std::atomic<bool>        m_stop;                //是否要求线程退出
    std::thread              m_thread;              //服务线程
    SOCKET                   m_clients[kSlots];     //两条连接
    std::string              m_buffers[kSlots];     //两条连接尚未拆成完整帧的字节
    bool                     m_joined[kSlots];      //两条连接是否已报到成功
    std::mutex               m_mutex;               //保护 m_tokens
    std::vector<std::string> m_tokens;              //收到的报到令牌
    std::atomic<int>         m_dataForwarded;       //转发的 1862 帧个数
};

//按偏移生成的传输内容
uint8_t PatternAt(uint64_t offset)
{
    return (uint8_t)((offset * kPatternMul + kPatternAdd) & kPatternMask);
}

/**
 *  可靠通道的接收端：逐字节核对收到的字节流，记录可写通知。回调在引擎的 IO 线程上，计数用原子变量。
 */
class StreamCheckSink : public IP2PChannelSink
{
public:
    StreamCheckSink()
        : m_received(0)
        , m_mismatch(false)
        , m_writable(0)
    {
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        uint64_t offset = m_received;
        for (uint32_t i = 0; i < len; ++i)
        {
            if (data[i] != PatternAt(offset + i))
            {
                m_mismatch = true;
                break;
            }
        }
        m_received += len;
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
        ++m_writable;
    }

    std::atomic<uint64_t> m_received;   //收到的字节数
    std::atomic<bool>     m_mismatch;   //是否出现过内容不符
    std::atomic<int>      m_writable;   //可写通知的次数
};

/**
 *  在可靠通道上把 total 字节发完：队列满时等可写通知。返回是否在时限内全部交给了 Send。
 *  @param link   发送端的链路
 *  @param sink   发送端通道的接收端（收可写通知）
 *  @param total  传输量（字节）
 */
bool SendStream(IP2PLink* link, StreamCheckSink& sink, size_t total)
{
    std::string chunk(kSendChunkBytes, '\0');
    size_t sent = 0;
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kWaitTransferMs);
    while (sent < total && std::chrono::steady_clock::now() < deadline)
    {
        const size_t n = (total - sent < kSendChunkBytes) ? total - sent : kSendChunkBytes;
        for (size_t i = 0; i < n; ++i)
            chunk[i] = (char)PatternAt(sent + i);
        const int writableBefore = sink.m_writable;
        const int result = link->Send(kReliableChannel, reinterpret_cast<const uint8_t*>(chunk.data()), (uint32_t)n);
        if (result == p2p_send_ok)
        {
            sent += n;
            continue;
        }
        if (result != p2p_send_would_block)
            return false;
        //队列满：等可写通知（这一块下次重发）
        while (sink.m_writable == writableBefore && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return sent == total;
}

//最多等 timeoutMs 毫秒，直到接收端收满 total 字节
bool WaitReceived(const StreamCheckSink& sink, uint64_t total, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (sink.m_received >= total)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//在建好的一对链路上打开可靠通道、从 A 传 total 字节给 B 并核对；返回吞吐（MiB/s）
double TransferAndCheck(EnginePair& pair, size_t total)
{
    StreamCheckSink sinkA;
    StreamCheckSink sinkB;
    IP2PLink* linkA = pair.m_handlerA.Link();
    IP2PLink* linkB = pair.m_handlerB.Link();
    REQUIRE(linkA->OpenChannel(kReliableChannel, p2p_channel_reliable, &sinkA) == p2p_send_ok);
    REQUIRE(linkB->OpenChannel(kReliableChannel, p2p_channel_reliable, &sinkB) == p2p_send_ok);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    REQUIRE(SendStream(linkA, sinkA, total));
    REQUIRE(WaitReceived(sinkB, total, kWaitTransferMs));
    const double ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK_FALSE(sinkB.m_mismatch);
    CHECK(sinkB.m_received == total);
    P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    linkA->GetStats(&stats);
    CHECK(stats.m_bytesSent == total);
    return (ms > 0.0) ? (double)total / kBytesPerMiB / (ms / kMillisPerSecondF) : 0.0;
}

//本机一个当前没有监听的 TCP 端口：绑定随机端口取得端口号后立即关闭
uint16_t UnusedTcpPort()
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(kLoopbackIp);
    addr.sin_port = 0;
    int len = sizeof(addr);
    ::bind(s, (const sockaddr*)&addr, sizeof(addr));
    ::getsockname(s, (sockaddr*)&addr, &len);
    ::closesocket(s);
    return ntohs(addr.sin_port);
}

//给两端推同一个会话的 2302，中转指向 relayPort，两端都强制中转
void NotifyBothViaRelay(EnginePair& pair, const char* sessionId, uint16_t relayPort)
{
    pair.m_engineA.SetForceRelay(true);
    pair.m_engineB.SetForceRelay(true);
    pair.m_engineA.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(sessionId, p2p_role_controlling, kUidB,
                                                                       kConnectTimeoutMs, relayPort, kTokenA));
    pair.m_engineB.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(sessionId, p2p_role_controlled, kUidA,
                                                                       kConnectTimeoutMs, relayPort, kTokenB));
}

} // namespace

TEST_CASE("P2PIO1 本机两引擎：建链就绪、数据报往返、业务方关闭后对方报告断开", "[p2pio]")
{
    EnginePair pair;
    pair.StartBoth();
    CHECK(WaitEnabled(pair.m_engineA, kWaitEventMs));
    CHECK(WaitEnabled(pair.m_engineB, kWaitEventMs));

    pair.ConnectBoth(kSessionIo1);
    CHECK(pair.m_handlerA.FeatureSessionId() == kFeatureSessionId);
    CHECK(pair.m_handlerB.FeatureSessionId() == kFeatureSessionId);
    CHECK(pair.m_engineA.SessionCount() == 1);
    CHECK(pair.m_engineB.SessionCount() == 1);

    //两端各发一次 2303 与一次成功的 2304，路径为局域网直连（本机网卡地址）
    CHECK(pair.m_hostA.CountSent(cmd_p2p_signal) == 1);
    CHECK(pair.m_hostB.CountSent(cmd_p2p_signal) == 1);
    REQUIRE(pair.m_hostA.WaitSent(cmd_p2p_result, 1, kWaitEventMs));
    REQUIRE(pair.m_hostB.WaitSent(cmd_p2p_result, 1, kWaitEventMs));
    Json::Value result;
    REQUIRE(pair.m_hostA.LastSent(cmd_p2p_result, result));
    CHECK(result["ok"].asBool());
    CHECK(result["path"].asInt() == p2p_path_lan);
    P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    pair.m_handlerA.Link()->GetStats(&stats);
    CHECK(stats.m_path == p2p_link_path_lan);

    //打开同一数据报通道后双向收发
    IP2PLink* linkA = pair.m_handlerA.Link();
    IP2PLink* linkB = pair.m_handlerB.Link();
    REQUIRE(linkA->OpenChannel(kChannel, p2p_channel_datagram, &pair.m_handlerA) == p2p_send_ok);
    REQUIRE(linkB->OpenChannel(kChannel, p2p_channel_datagram, &pair.m_handlerB) == p2p_send_ok);
    const std::string ping = "ping from A";
    const std::string pong = "pong from B";
    CHECK(linkA->Send(kChannel, (const uint8_t*)ping.data(), (uint32_t)ping.size()) == p2p_send_ok);
    REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ReceivedCount, 1, kWaitEventMs));
    CHECK(pair.m_handlerB.ReceivedAt(0) == ping);
    CHECK(linkB->Send(kChannel, (const uint8_t*)pong.data(), (uint32_t)pong.size()) == p2p_send_ok);
    REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ReceivedCount, 1, kWaitEventMs));
    CHECK(pair.m_handlerA.ReceivedAt(0) == pong);

    //A 的业务方关闭：B 收到关闭报文后报告断开（业务结束）；A 不报告；两端会话随后被清理
    linkA->Close(p2p_close_business_end);
    REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
    CHECK(pair.m_handlerB.CloseReason() == p2p_close_business_end);
    CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
    CHECK(WaitSessionCount(pair.m_engineB, 0, kWaitEventMs));
    CHECK(pair.m_handlerA.ClosedCount() == 0);
    CHECK(pair.m_handlerA.FailedCount() == 0);
    CHECK(pair.m_hostA.CountSent(cmd_p2p_close) == 1);
    CHECK(linkA->Send(kChannel, (const uint8_t*)ping.data(), (uint32_t)ping.size()) == p2p_send_closed);
}

TEST_CASE("P2PIO2 对方不应答：按信令时限判失败并发 2305", "[p2pio]")
{
    EnginePair pair;
    pair.StartBoth();
    //只给 A 推 2302，且 A 的 2303 不转发：A 一直收不到对方的 2303
    pair.m_hostA.SetForwardSignals(false);
    pair.m_hostA.SetPeer(NULL);
    pair.m_engineA.OnServerFrame(cmd_p2p_session_notify, 0,
                                 NotifyJson(kSessionIo2, p2p_role_controlling, kUidB, kShortConnectTimeoutMs));
    REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::FailedCount, 1, kWaitFailMs));
    CHECK(pair.m_handlerA.FeatureSessionId() == kFeatureSessionId);
    CHECK(pair.m_handlerA.ReadyCount() == 0);
    CHECK(pair.m_hostA.CountSent(cmd_p2p_close) == 1);
    Json::Value close;
    REQUIRE(pair.m_hostA.LastSent(cmd_p2p_close, close));
    CHECK(close["p2psessionid"].asString() == kSessionIo2);
    CHECK(close["reason"].asInt() == p2p_close_connect_fail);
    CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
}

TEST_CASE("P2PIO3 退出登录：关闭全部会话，对方报告断开", "[p2pio]")
{
    EnginePair pair;
    pair.StartBoth();
    pair.ConnectBoth(kSessionIo3);

    pair.m_engineA.OnLoggingOut();
    REQUIRE(pair.m_hostA.WaitSent(cmd_p2p_close, 1, kWaitEventMs));
    Json::Value close;
    REQUIRE(pair.m_hostA.LastSent(cmd_p2p_close, close));
    CHECK(close["reason"].asInt() == p2p_close_self_logout);
    REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
    CHECK(pair.m_handlerB.CloseReason() == p2p_close_self_logout);
    CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
    CHECK(WaitSessionCount(pair.m_engineB, 0, kWaitEventMs));
    CHECK(pair.m_handlerA.ClosedCount() == 0);
    //退出登录之后忘掉报到结果
    CHECK_FALSE(pair.m_engineA.IsEnabled());
}

TEST_CASE("P2PIO4 停止引擎时业务方仍持有链路：发 2305，之后发送返回已关闭", "[p2pio]")
{
    EnginePair pair;
    pair.StartBoth();
    pair.ConnectBoth(kSessionIo4);
    IP2PLink* linkA = pair.m_handlerA.Link();
    REQUIRE(linkA->OpenChannel(kChannel, p2p_channel_datagram, &pair.m_handlerA) == p2p_send_ok);

    pair.m_engineA.Stop();
    CHECK(pair.m_hostA.CountSent(cmd_p2p_close) == 1);
    CHECK(pair.m_engineA.SessionCount() == 0);
    const std::string data = "after stop";
    CHECK(linkA->Send(kChannel, (const uint8_t*)data.data(), (uint32_t)data.size()) == p2p_send_closed);
    //重复停止安全；业务方随后释放链路（EnginePair 析构时）也安全
    pair.m_engineA.Stop();
    REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
}

TEST_CASE("P2PIO5 网卡枚举与地址解析", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);

    const std::vector<P2PAdapterInfo> adapters = EnumerateAdapters();
    CHECK_FALSE(adapters.empty());
    const std::vector<P2PCandidate> hosts = BuildHostCandidates(adapters, kAnyPort);
    REQUIRE_FALSE(hosts.empty());
    for (size_t i = 0; i < hosts.size(); ++i)
    {
        CHECK(hosts[i].m_type == p2p_candidate_host);
        CHECK(hosts[i].m_port == kAnyPort);
        CHECK(hosts[i].m_ip != kLoopbackIp);
    }

    uint32_t ip = 0;
    CHECK(ResolveIpv4("127.0.0.1", ip));
    CHECK(ip == kLoopbackIp);
    ip = 0;
    CHECK(ResolveIpv4("localhost", ip));
    CHECK(ip != 0);
    CHECK_FALSE(ResolveIpv4("", ip));

    ::WSACleanup();
}

TEST_CASE("P2PIO6 UDP socket：随机端口、本机互发、非阻塞、关闭后发送失败", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        P2PUdpPort a;
        P2PUdpPort b;
        REQUIRE(a.Open());
        REQUIRE(b.Open());
        CHECK(a.IsOpen());
        CHECK(a.Port() != 0);
        CHECK(b.Port() != 0);
        CHECK(a.Port() != b.Port());

        //没有数据时立即返回 -1（非阻塞）
        uint8_t buf[kRecvBufBytes];
        uint32_t fromIp = 0;
        uint16_t fromPort = 0;
        CHECK(b.Receive(buf, sizeof(buf), fromIp, fromPort) == -1);

        const std::string text = "udp hello";
        REQUIRE(a.SendTo(kLoopbackIp, b.Port(), (const uint8_t*)text.data(), text.size()));
        int n = -1;
        for (int waited = 0; waited <= kWaitEventMs && n < 0; waited += kPollMs)
        {
            n = b.Receive(buf, sizeof(buf), fromIp, fromPort);
            if (n < 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        REQUIRE(n == (int)text.size());
        CHECK(std::string((const char*)buf, n) == text);
        CHECK(fromIp == kLoopbackIp);
        CHECK(fromPort == a.Port());

        a.Close();
        CHECK_FALSE(a.IsOpen());
        CHECK(a.Port() == 0);
        CHECK_FALSE(a.SendTo(kLoopbackIp, b.Port(), (const uint8_t*)text.data(), text.size()));
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO7 两个引擎强制中转：经本机假中转建链、数据报往返、关闭后对方报告断开", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        FakeRelayServer relay(0);
        REQUIRE(relay.Start());
        EnginePair pair;
        pair.StartBoth();
        NotifyBothViaRelay(pair, kSessionIo7, relay.Port());
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));

        //路径为中转；两端的 2304 都报告成功、路径为中转
        P2PLinkStats stats;
        memset(&stats, 0, sizeof(stats));
        stats.m_cbSize = sizeof(stats);
        pair.m_handlerA.Link()->GetStats(&stats);
        CHECK(stats.m_path == p2p_link_path_relay);
        REQUIRE(pair.m_hostA.WaitSent(cmd_p2p_result, 1, kWaitEventMs));
        REQUIRE(pair.m_hostB.WaitSent(cmd_p2p_result, 1, kWaitEventMs));
        Json::Value result;
        REQUIRE(pair.m_hostB.LastSent(cmd_p2p_result, result));
        CHECK(result["ok"].asBool());
        CHECK(result["path"].asInt() == p2p_path_relay);
        //假中转收到的两个报到带着各自 2302 里的令牌
        std::vector<std::string> tokens = relay.Tokens();
        REQUIRE(tokens.size() == 2);
        CHECK(((tokens[0] == kTokenA && tokens[1] == kTokenB) || (tokens[0] == kTokenB && tokens[1] == kTokenA)));

        //数据报往返
        IP2PLink* linkA = pair.m_handlerA.Link();
        IP2PLink* linkB = pair.m_handlerB.Link();
        REQUIRE(linkA->OpenChannel(kChannel, p2p_channel_datagram, &pair.m_handlerA) == p2p_send_ok);
        REQUIRE(linkB->OpenChannel(kChannel, p2p_channel_datagram, &pair.m_handlerB) == p2p_send_ok);
        const std::string ping = "ping via relay";
        const std::string pong = "pong via relay";
        CHECK(linkA->Send(kChannel, (const uint8_t*)ping.data(), (uint32_t)ping.size()) == p2p_send_ok);
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ReceivedCount, 1, kWaitEventMs));
        CHECK(pair.m_handlerB.ReceivedAt(0) == ping);
        CHECK(linkB->Send(kChannel, (const uint8_t*)pong.data(), (uint32_t)pong.size()) == p2p_send_ok);
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ReceivedCount, 1, kWaitEventMs));
        CHECK(pair.m_handlerA.ReceivedAt(0) == pong);
        CHECK(relay.DataForwarded() > 0);

        //A 的业务方关闭：B 经中转收到关闭报文，报告断开（业务结束）；两端会话随后被清理
        linkA->Close(p2p_close_business_end);
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
        CHECK(pair.m_handlerB.CloseReason() == p2p_close_business_end);
        CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
        CHECK(WaitSessionCount(pair.m_engineB, 0, kWaitEventMs));
        CHECK(pair.m_handlerA.ClosedCount() == 0);
    }
    ::WSACleanup();
}

namespace {

/**
 *  最多等 timeoutMs 毫秒，直到两端中任一端发出过 2305，取出那一端最后一条 2305 的内容。
 *  两端几乎同时建链失败时，先失败的一端发出 2305，另一端可能先收到经服务端（这里是假宿主）转来的那条 2305，按「对端
 *  已关闭」收尾而不再自己发（P2PLinkSession::OnServerClose），所以不能要求某一端一定发出。
 */
bool WaitEitherSentClose(const FakeEngineHost& a, const FakeEngineHost& b, int timeoutMs, Json::Value& close)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (a.LastSent(cmd_p2p_close, close) || b.LastSent(cmd_p2p_close, close))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

} // namespace

TEST_CASE("P2PIO8 中转端口无人监听：按建链失败处理", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const uint16_t port = UnusedTcpPort();
    {
        EnginePair pair;
        pair.StartBoth();
        NotifyBothViaRelay(pair, kSessionIo8, port);
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::FailedCount, 1, kWaitRelayFailMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::FailedCount, 1, kWaitRelayFailMs));
        CHECK(pair.m_handlerA.ReadyCount() == 0);
        //至少一端发出原因为「建链失败」的 2305（哪一端发出取决于谁先失败，见 WaitEitherSentClose 的说明）
        Json::Value close;
        REQUIRE(WaitEitherSentClose(pair.m_hostA, pair.m_hostB, kWaitEventMs, close));
        CHECK(close["reason"].asInt() == p2p_close_connect_fail);
        CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO9 中转报到被拒：按建链失败处理", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        FakeRelayServer relay(kHelloRejectCode);
        REQUIRE(relay.Start());
        EnginePair pair;
        pair.StartBoth();
        NotifyBothViaRelay(pair, kSessionIo9, relay.Port());
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::FailedCount, 1, kWaitRelayFailMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::FailedCount, 1, kWaitRelayFailMs));
        CHECK(pair.m_handlerA.ReadyCount() == 0);
        //两端各自连中转报到。先被拒的一端发出 2305，另一端可能在报到之前就收到转来的这条而结束（见 WaitEitherSentClose
        //的说明），机器负载高时实测中转只收到 1 份报到，所以要求 1~2 份
        CHECK(relay.Tokens().size() >= 1);
        CHECK(relay.Tokens().size() <= 2);
        CHECK(relay.DataForwarded() == 0);
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO10 就绪之后中转服务关闭连接：两端报告断开", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        FakeRelayServer relay(0);
        REQUIRE(relay.Start());
        EnginePair pair;
        pair.StartBoth();
        NotifyBothViaRelay(pair, kSessionIo10, relay.Port());
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        relay.Stop();
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ClosedCount, 1, kWaitEventMs));
        CHECK(pair.m_handlerA.CloseReason() == p2p_close_link_timeout);
        CHECK(pair.m_handlerB.CloseReason() == p2p_close_link_timeout);
        CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO11 本机 UDP 直连的可靠通道：32 MiB 按序原样送达", "[p2pio]")
{
    EnginePair pair;
    pair.StartBoth();
    pair.ConnectBoth(kSessionIo11);
    const double mibps = TransferAndCheck(pair, kUdpTransferBytes);
    printf("P2PIO11 reliable over local UDP: %.1f MiB/s\n", mibps);
}

TEST_CASE("P2PIO12 强制中转的可靠通道：16 MiB 经假中转按序原样送达", "[p2pio]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        FakeRelayServer relay(0);
        REQUIRE(relay.Start());
        EnginePair pair;
        pair.StartBoth();
        NotifyBothViaRelay(pair, kSessionIo12, relay.Port());
        REQUIRE(pair.m_handlerA.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        REQUIRE(pair.m_handlerB.WaitCount(&RecordingHandler::ReadyCount, 1, kWaitReadyMs));
        const double mibps = TransferAndCheck(pair, kRelayTransferBytes);
        printf("P2PIO12 reliable over fake relay: %.1f MiB/s\n", mibps);
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO13 UDP socket 成批发送：一次交出多个报文，接收端逐个收到", "[p2pio]")
{
    const size_t kFullSegments = 8;
    const size_t kShortTail = 500;
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        P2PUdpPort a;
        P2PUdpPort b;
        REQUIRE(a.Open());
        REQUIRE(b.Open());
        printf("P2PIO13 UDP send offload segment: %u bytes\n", (unsigned)a.BatchSegmentBytes());
        CHECK((a.BatchSegmentBytes() == kP2PMaxDatagramBytes || a.BatchSegmentBytes() == 0));

        std::string batch;
        for (size_t i = 0; i < kFullSegments; ++i)
            batch += std::string(kP2PMaxDatagramBytes, (char)('a' + i));
        batch += std::string(kShortTail, 'z');
        REQUIRE(a.SendBatch(kLoopbackIp, b.Port(), reinterpret_cast<const uint8_t*>(batch.data()), batch.size()));

        std::vector<std::string> got;
        uint8_t buf[kRecvBufBytes];
        for (int waited = 0; waited <= kWaitEventMs && got.size() < kFullSegments + 1; waited += kPollMs)
        {
            uint32_t fromIp = 0;
            uint16_t fromPort = 0;
            int n = b.Receive(buf, sizeof(buf), fromIp, fromPort);
            while (n >= 0)
            {
                got.push_back(std::string((const char*)buf, (size_t)n));
                n = b.Receive(buf, sizeof(buf), fromIp, fromPort);
            }
            if (got.size() < kFullSegments + 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        REQUIRE(got.size() == kFullSegments + 1);
        for (size_t i = 0; i < kFullSegments; ++i)
            CHECK(got[i] == std::string(kP2PMaxDatagramBytes, (char)('a' + i)));
        CHECK(got[kFullSegments] == std::string(kShortTail, 'z'));
    }
    ::WSACleanup();
}

TEST_CASE("P2PIO14 用途没有业务方登记：链路就绪后两端立即关闭", "[p2pio]")
{
    const char* const kSessionIo14 = "p2p-7101-7102-io14";
    const char* const kUnhandledPurpose = "folder";
    EnginePair pair;
    pair.StartBoth();
    pair.m_engineA.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(kSessionIo14, p2p_role_controlling, kUidB,
                                                                       kConnectTimeoutMs, kDefaultRelayPort, "t",
                                                                       kUnhandledPurpose));
    pair.m_engineB.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(kSessionIo14, p2p_role_controlled, kUidA,
                                                                       kConnectTimeoutMs, kDefaultRelayPort, "t",
                                                                       kUnhandledPurpose));
    //两端都就绪过（各发一次成功的 2304），随后会话被清理
    REQUIRE(pair.m_hostA.WaitSent(cmd_p2p_result, 1, kWaitReadyMs));
    REQUIRE(pair.m_hostB.WaitSent(cmd_p2p_result, 1, kWaitReadyMs));
    CHECK(WaitSessionCount(pair.m_engineA, 0, kWaitEventMs));
    CHECK(WaitSessionCount(pair.m_engineB, 0, kWaitEventMs));
    //先关闭的一端发 2305（业务结束）；另一端若先收到对方的关闭报文就不再发，所以只要求至少一端发过
    Json::Value close;
    const bool sentA = pair.m_hostA.LastSent(cmd_p2p_close, close);
    if (!sentA)
        REQUIRE(pair.m_hostB.LastSent(cmd_p2p_close, close));
    CHECK(close["reason"].asInt() == p2p_close_business_end);
    //没有业务方，不会有任何回调
    CHECK(pair.m_handlerA.ReadyCount() == 0);
    CHECK(pair.m_handlerB.ReadyCount() == 0);
}
