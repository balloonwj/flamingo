/**
 *  单聊文件夹传输走 P2P 的单元测试（Suite FP2P，标签 [folderp2p]），FolderP2PTransportTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 5 期 5b、5c（10.1 节）：
 *    - FP2P1 链路槽的认领规则（假链路）：链路先到时传输认领它、立即决定走 P2P；传输先认领时等链路到达；同一会话号
 *            重试时新建槽，上一轮的槽释放时关闭它的链路；新链路的结果不会落到上一轮还没释放、已就绪的旧槽上；建链
 *            失败时决定退回中转；
 *    - FP2P2 P2P 通道的收发（假链路）：应用帧加 4 字节长度后发出；收到的字节流按长度拆成应用帧，按序交给会话，跨两次
 *            OnData 的帧也能拼齐；长度字段不合理时报告断开；链路断开时报告断开；本端关闭之后不再回调；
 *    - FP2P3 建链失败退回中转：各条通道改用中转实现，连不上没人监听的端口时 Connect 失败；
 *    - FP2P4 真实会话经真实链路传文件夹：同一进程里开两个 P2P 引擎（本机 UDP 互连，chatserver 的转发由测试代码模拟），
 *            各配一个文件夹传输的业务方；用真实的 FolderSendSession / FolderRecvSession 经 P2P 通道（1 条控制 + 2 条
 *            数据）传一个含子目录、空文件与跨多个分片的大文件的文件夹，两端都报告完成，落地文件逐个与源文件一致；
 *    - FP2P5 业务方的用途（第 6 期）：默认构造登记 folder，按用途构造登记给定的用途（文件共享 DLL 用 fileshare）；
 *            重复登记只登记一次，注销时注销同一个用途。
 *    - FP2P6 各业务共用的链路层 P2PLinkBroker.h（第 6 期从本功能抽出）：流通道的单帧上限由业务指定，比文件夹中转上限
 *            大的帧（远程桌面的关键帧）可以整帧收发，超过业务上限的帧发不出、收到时报告断开；一条流通道只能打开
 *            一次；同一通道号的第二条流通道打不开，它关闭时不影响第一条的登记；同一会话号上未认领的就绪槽被新链路
 *            替换时，旧槽的链路被关闭；业务调用 CloseLink 立即关闭链路（之后 Link 为 NULL、状态为已断开），引用保留到
 *            槽析构，析构时不再重复关闭。
 *    - FP2P7 共用链路层的数据报通道（第 6 期，一对一通话用）：在链路上打开数据报类型的通道；一条消息原样整条交给链路
 *            （不加长度字段）；与帧通道在同一条链路上并存、互不干扰；收到的消息按序整条交出；业务回调阻塞时排队超过
 *            上限丢最旧的并计数；链路断开时报告一次、之后发送返回已关闭；本端关闭之后不再回调。
 *    - FP2P8 业务放弃一个会话号（一对一通话没接通就结束，第 6 期）：已认领、已就绪的链路立即关闭；之后 DLL 才交来的
 *            链路直接关闭、不进链路槽；没被认领的就绪链路随之关闭；建链失败的结果只清掉记录；重新认领同一会话号后
 *            恢复正常。
 *
 *    - FP2P10 单聊单文件直传（2026-10-04）：单文件清单 + 直传的收发扩展点，经真实 P2P 链路传一个跨多个分片的文件，
 *            文件落在 direct-<会话号> 目录、内容一致；两端扩展点各自报出的 MD5 与文件内容的 MD5 一致，接收端报出落地
 *            路径；两端通道工厂报局域网直连；
 *    - FP2P11 直传禁止回落 fileserver 中转（2026-10-04）：建链失败时通道不连 1816 给的中转地址、Connect 直接失败；
 *            允许回落的工厂（文件夹传输的默认）照旧去连中转地址；
 *    - FP2P12 直传的业务方与「业务正在用的链路路径」（2026-10-04）：直传业务方的用途是 file；业务没在用 P2P、
 *            没有链路槽、链路没就绪时报 0，正在用时报链路槽的路径（远程桌面、通话、屏幕共享的徽章都经它判断）。
 *
 *  框架 Catch2 v3。本文件不使用预编译头：会话头文件带进 <winsock2.h>，必须先于 <windows.h>。
 *
 *  典型用法：FlamingoUnit.exe "[folderp2p]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../Feature/FolderTransfer/FolderSendSession.h"
#include "../Feature/FolderTransfer/FolderRecvSession.h"
#include "../Feature/FolderTransfer/FolderManifest.h"
#include "../Feature/P2P/FolderP2PTransport.h"
#include "../Feature/FolderTransfer/DirectFileTransfer.h"   //单文件直传（FP2P10）
#include "../Common/Util/MD5Core.h"                         //核对直传扩展点报出的 MD5（FP2P10）
#include "../net/Protocol.h"
#include "../Common/Log/IULog.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../FlamingoP2P/Io/P2PEngine.h"
#include "P2PTestFakes.h"

using namespace balloonp2p;
using namespace foldertransfer;
using p2ptest::FakeLink;
using p2ptest::WithLength;

namespace {

//等待各类事件的最长时间（毫秒）与轮询间隔（毫秒）
const int kWaitMs = 5000;
const int kWaitTransferMs = 60000;
const int kPollMs = 10;

//中转参数（退回中转时用；FP2P4 不会真的用到）
const char* const kRelayIp = "127.0.0.1";
const int kRelayPort = 29903;
const char* const kRelayToken = "token-text";

//可辨认数据的步长与模数
const size_t kPatternStep = 7;
const size_t kPatternModulo = 251;

//FP2P4 的文件大小（字节）：一个跨 3 个分片的大文件（分片 512 KiB）、一个小文件、一个空文件
const size_t kBigFileBytes = 1300 * 1024;
const size_t kSmallFileBytes = 1000;

//FP2P4 的数据通道条数
const int kDataLanes = 2;

//单调时钟的当前时间（毫秒）
int64_t NowMs()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 *  会话一侧的回调：记录收到的应用帧与断开。
 */
class FrameRecorder : public FolderRelayClientHandler
{
public:
    FrameRecorder()
        : m_closed(0)
    {
    }

    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_frames.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnFolderRelayClosed() override
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
    std::vector<std::string> m_frames;   //收到的应用帧
    std::atomic<int>         m_closed;   //报告断开的次数
};

//最多等 timeoutMs 毫秒，直到 pred 指向的计数达到 atLeast
bool WaitAtomic(const std::atomic<int>& value, int atLeast, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (value >= atLeast)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

bool WaitFrames(FrameRecorder& r, size_t count, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (r.FrameCount() >= count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//FP2P2：50 毫秒之后通知某条通道可写（模拟 DLL 在发送队列降下来时的回调）
const int kWritableDelayMs = 50;

void NotifyWritableLater(FolderP2PLinkSlot* slot, uint16_t channelId)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kWritableDelayMs));
    slot->OnWritable(channelId);
}

//本机一个当前没有监听的 TCP 端口
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

} // namespace

TEST_CASE("FP2P1 链路槽的认领规则", "[folderp2p]")
{
    //假链路都在业务方与工厂之前声明：链路槽在析构时关闭它持有的链路，假链路必须比所有槽活得久
    FakeLink link1;
    FakeLink link2;
    FakeLink link3;
    FakeLink link4;
    FolderP2PLinkBroker broker;

    //链路先到：传输认领它，立即决定走 P2P
    broker.OnLinkReady("s1", &link1);
    CHECK(link1.m_refs == 1);
    {
        std::shared_ptr<FolderP2PTransportFactory> f = broker.CreateFactory("s1", kRelayIp, kRelayPort, kRelayToken, NULL);
        CHECK(f->Slot()->State() == FolderP2PLinkSlot::slot_ready);
        const int64_t start = NowMs();
        CHECK(f->DecideP2P());
        CHECK(NowMs() - start < kWaitMs);
    }
    //工厂释放：槽随之释放，链路被关闭（业务结束）并放掉引用
    CHECK(link1.m_closes == 1);
    CHECK(link1.m_closeReason == p2p_close_business_end);
    CHECK(link1.m_refs == 0);

    //传输先认领：等链路到达
    std::shared_ptr<FolderP2PTransportFactory> f2 = broker.CreateFactory("s2", kRelayIp, kRelayPort, kRelayToken, NULL);
    CHECK(f2->Slot()->State() == FolderP2PLinkSlot::slot_waiting);
    broker.OnLinkReady("s2", &link2);
    CHECK(f2->Slot()->State() == FolderP2PLinkSlot::slot_ready);
    CHECK(f2->DecideP2P());

    //同一会话号重试：上一轮的工厂还没释放、它的槽已就绪；新一轮认领新槽，新链路的结果落在新槽上
    std::shared_ptr<FolderP2PTransportFactory> f2b = broker.CreateFactory("s2", kRelayIp, kRelayPort, kRelayToken, NULL);
    CHECK(f2b->Slot() != f2->Slot());
    CHECK(f2b->Slot()->State() == FolderP2PLinkSlot::slot_waiting);
    broker.OnLinkReady("s2", &link3);
    CHECK(f2b->Slot()->Link() == &link3);
    CHECK(f2->Slot()->Link() == &link2);
    f2.reset();
    CHECK(link2.m_closes == 1);
    CHECK(link3.m_closes == 0);

    //建链失败：决定退回中转
    std::shared_ptr<FolderP2PTransportFactory> f3 = broker.CreateFactory("s3", kRelayIp, kRelayPort, kRelayToken, NULL);
    broker.OnLinkFailed("s3", error_code_p2p_peer_offline);
    CHECK(f3->Slot()->State() == FolderP2PLinkSlot::slot_failed);
    CHECK_FALSE(f3->DecideP2P());
    //决定只做一次：之后链路就算到了也不改（这条链路进了新的未认领槽，由业务方析构时关闭）
    broker.OnLinkReady("s3", &link4);
    CHECK_FALSE(f3->DecideP2P());
}

TEST_CASE("FP2P2 P2P 通道的收发、拆帧与断开", "[folderp2p]")
{
    FolderP2PLinkBroker broker;
    FakeLink link;
    broker.OnLinkReady("s1", &link);
    std::shared_ptr<FolderP2PTransportFactory> f = broker.CreateFactory("s1", kRelayIp, kRelayPort, kRelayToken, NULL);
    FrameRecorder rec;
    std::unique_ptr<IFolderLaneTransport> lane(f->CreateLane(1));
    REQUIRE(lane->Connect(1, &rec, 1));
    CHECK(lane->IsConnected());
    REQUIRE(link.m_channels.size() == 1);
    CHECK(link.m_channels[0] == 1);
    CHECK(link.m_kinds[0] == p2p::p2p_channel_reliable);

    //发送：加 4 字节长度；队列满时等可写通知后重发
    const std::string frame = "hello frame";
    link.m_blockSends = 1;
    std::thread notifier(&NotifyWritableLater, f->Slot().get(), (uint16_t)1);
    CHECK(lane->SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
    notifier.join();
    CHECK(link.m_sent == WithLength(frame));

    //接收：两帧，第二帧跨两次 OnData
    const std::string a = "first";
    const std::string b(3000, 'b');
    const std::string stream = WithLength(a) + WithLength(b);
    const size_t cut = WithLength(a).size() + 10;
    f->Slot()->OnData(1, reinterpret_cast<const uint8_t*>(stream.data()), (uint32_t)cut);
    f->Slot()->OnData(1, reinterpret_cast<const uint8_t*>(stream.data()) + cut, (uint32_t)(stream.size() - cut));
    REQUIRE(WaitFrames(rec, 2, kWaitMs));
    CHECK(rec.FrameAt(0) == a);
    CHECK(rec.FrameAt(1) == b);
    //没登记的通道上的数据丢弃
    f->Slot()->OnData(2, reinterpret_cast<const uint8_t*>(stream.data()), (uint32_t)stream.size());

    //链路断开：报告一次断开
    broker.OnLinkClosed("s1", p2p_close_link_timeout);
    REQUIRE(WaitAtomic(rec.m_closed, 1, kWaitMs));
    CHECK_FALSE(lane->IsConnected());
    CHECK_FALSE(lane->SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
    lane->Close();
    CHECK(rec.m_closed == 1);

    //长度字段不合理：报告断开
    FolderP2PLinkBroker broker2;
    FakeLink link2;
    broker2.OnLinkReady("s2", &link2);
    std::shared_ptr<FolderP2PTransportFactory> f2 = broker2.CreateFactory("s2", kRelayIp, kRelayPort, kRelayToken, NULL);
    FrameRecorder rec2;
    std::unique_ptr<IFolderLaneTransport> lane2(f2->CreateLane(0));
    REQUIRE(lane2->Connect(0, &rec2, 1));
    const uint8_t bad[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    f2->Slot()->OnData(0, bad, sizeof(bad));
    REQUIRE(WaitAtomic(rec2.m_closed, 1, kWaitMs));

    //本端关闭之后：不再回调
    FolderP2PLinkBroker broker3;
    FakeLink link3;
    broker3.OnLinkReady("s3", &link3);
    std::shared_ptr<FolderP2PTransportFactory> f3 = broker3.CreateFactory("s3", kRelayIp, kRelayPort, kRelayToken, NULL);
    FrameRecorder rec3;
    std::unique_ptr<IFolderLaneTransport> lane3(f3->CreateLane(0));
    REQUIRE(lane3->Connect(0, &rec3, 1));
    lane3->Close();
    f3->Slot()->OnData(0, reinterpret_cast<const uint8_t*>(stream.data()), (uint32_t)stream.size());
    broker3.OnLinkClosed("s3", p2p_close_link_timeout);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(rec3.FrameCount() == 0);
    CHECK(rec3.m_closed == 0);
}

TEST_CASE("FP2P3 建链失败时各条通道退回中转", "[folderp2p]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        FolderP2PLinkBroker broker;
        const int port = UnusedTcpPort();
        std::shared_ptr<FolderP2PTransportFactory> f = broker.CreateFactory("s1", kRelayIp, port, kRelayToken, NULL);
        broker.OnLinkFailed("s1", error_code_p2p_peer_offline);
        FrameRecorder rec;
        std::unique_ptr<IFolderLaneTransport> lane(f->CreateLane(0));
        //中转实现连没人监听的端口：失败
        CHECK_FALSE(lane->Connect(0, &rec, 2));
        CHECK_FALSE(lane->IsConnected());
        lane->Close();
    }
    ::WSACleanup();
}

namespace {

//FP2P4：两端的 userid
const int64_t kUidA = 7201;
const int64_t kUidB = 7202;

//FP2P4：文件夹传输的会话号（也是 2302 的业务会话号）与 P2P 会话号
const char* const kFolderSessionId = "ft-p2p-unit-1";
const char* const kP2PSessionId = "p2p-7201-7202-fp2p4";

//解析 JSON
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.data(), text.data() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//一个 2302 包体：用途 folder，业务会话号为文件夹传输的会话号
std::string NotifyJson(int role, int64_t peerUid)
{
    Json::Value v;
    v["p2psessionid"] = kP2PSessionId;
    v["role"] = role;
    v["purpose"] = "folder";
    v["featuresessionid"] = kFolderSessionId;
    v["peeruid"] = std::to_string(peerUid);
    v["peerdeviceid"] = "fp2p-peer";
    v["relayip"] = kRelayIp;
    v["relayport"] = kRelayPort;
    v["token"] = kRelayToken;
    v["connecttimeoutms"] = 3000;
    v["keepalivems"] = 5000;
    v["linktimeoutms"] = 15000;
    return Compact(v);
}

/**
 *  一端的引擎宿主：2303 / 2305 转给对端引擎；用途 folder 交给本端的文件夹业务方。
 */
class FolderHost : public p2p::IP2PEngineHost
{
public:
    FolderHost(int64_t uid, FolderP2PLinkBroker* broker)
        : m_uid(uid)
        , m_broker(broker)
        , m_peer(NULL)
    {
    }

    void SetPeer(p2p::P2PEngine* peer)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_peer = peer;
    }

    virtual void SendCommand(uint16_t cmd, const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_peer == NULL)
            return;
        if (cmd == cmd_p2p_signal)
        {
            Json::Value v;
            if (ParseJson(body, v))
            {
                v["fromuid"] = std::to_string(m_uid);
                m_peer->OnServerFrame(cmd_p2p_signal, 0, Compact(v));
            }
        }
        else if (cmd == cmd_p2p_close)
        {
            m_peer->OnServerFrame(cmd_p2p_close, 0, body);
        }
    }

    virtual p2p::IP2PLinkHandler* FindHandler(const std::string& purpose) override
    {
        return (purpose == "folder") ? m_broker : NULL;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    //报到的用途：folder（本组用例的 2301 不经过真实服务端，只为实现接口）
    virtual void ListPurposes(std::vector<std::string>& purposes) override
    {
        purposes.push_back("folder");
    }

private:
    std::mutex           m_mutex;    //保护 m_peer
    int64_t              m_uid;      //本端 userid
    FolderP2PLinkBroker* m_broker;   //本端的文件夹业务方（借用）
    p2p::P2PEngine*      m_peer;     //对端引擎（借用）
};

//发送端回调：记录完成与失败
class SendRecorder : public IFolderSendSessionHandler
{
public:
    SendRecorder()
        : m_completed(0)
        , m_failed(0)
        , m_errcode(0)
    {
    }

    virtual void OnFolderSendProgress(const FolderSendProgress& progress) override
    {
        (void)progress;
    }

    virtual void OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes) override
    {
        (void)totalFiles;
        (void)totalBytes;
        ++m_completed;
    }

    virtual void OnFolderSendFailed(int errcode, const std::string& errmsg) override
    {
        (void)errmsg;
        m_errcode = errcode;
        ++m_failed;
    }

    std::atomic<int> m_completed;   //完成次数
    std::atomic<int> m_failed;      //失败次数
    std::atomic<int> m_errcode;     //最近一次失败的错误码
};

//接收端回调：记录完成、失败与落地根目录
class RecvRecorder : public IFolderRecvSessionHandler
{
public:
    RecvRecorder()
        : m_completed(0)
        , m_failed(0)
        , m_errcode(0)
    {
    }

    virtual void OnFolderRecvProgress(const FolderRecvProgress& progress) override
    {
        (void)progress;
    }

    virtual void OnFolderRecvCompleted(const std::wstring& rootPath, int64_t totalFiles, int64_t totalBytes) override
    {
        (void)totalFiles;
        (void)totalBytes;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_rootPath = rootPath;
        }
        ++m_completed;
    }

    virtual void OnFolderRecvFailed(int errcode, const std::string& errmsg) override
    {
        (void)errmsg;
        m_errcode = errcode;
        ++m_failed;
    }

    std::mutex       m_mutex;       //保护 m_rootPath
    std::wstring     m_rootPath;    //落地根目录
    std::atomic<int> m_completed;   //完成次数
    std::atomic<int> m_failed;      //失败次数
    std::atomic<int> m_errcode;     //最近一次失败的错误码
};

//写一个文件：内容按偏移生成
void WriteFile(const std::wstring& path, size_t bytes, int salt)
{
    FILE* fp = NULL;
    REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
    std::string data(bytes, '\0');
    for (size_t i = 0; i < bytes; ++i)
        data[i] = (char)((i * kPatternStep + (size_t)salt) % kPatternModulo);
    if (bytes > 0)
        REQUIRE(fwrite(data.data(), 1, bytes, fp) == bytes);
    fclose(fp);
}

//读整个文件
std::string ReadFile(const std::wstring& path)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
        return std::string("<missing>");
    std::string out;
    char buf[65536];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    fclose(fp);
    return out;
}

//本次运行的临时目录（%TEMP% 下按进程号与时间区分）
std::wstring MakeTempDir(const wchar_t* tag)
{
    wchar_t tmp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tmp);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sfp2p_%s_%lu_%llu", tmp, tag, ::GetCurrentProcessId(), (unsigned long long)::GetTickCount64());
    ::CreateDirectoryW(name, NULL);
    return name;
}

} // namespace

namespace {

//假的 P2P 服务：记录登记与注销的用途
class PurposeRecorder : public p2p::IP2PService
{
public:
    virtual bool RegisterHandler(const char* purpose, p2p::IP2PLinkHandler* handler) override
    {
        (void)handler;
        m_registered.push_back(purpose);
        return true;
    }

    virtual bool UnregisterHandler(const char* purpose) override
    {
        m_unregistered.push_back(purpose);
        return true;
    }

    std::vector<std::string> m_registered;     //登记过的用途，按调用顺序
    std::vector<std::string> m_unregistered;   //注销过的用途，按调用顺序
};

} // namespace

TEST_CASE("FP2P5 业务方按构造时给定的用途登记与注销", "[folderp2p]")
{
    PurposeRecorder service;
    {
        FolderP2PLinkBroker folder;
        CHECK(folder.Purpose() == kFolderP2PPurpose);
        REQUIRE(folder.Register(&service));
        //已登记时再登记：直接成功，不重复登记
        REQUIRE(folder.Register(&service));
        CHECK(folder.IsRegistered());
        folder.Unregister();
        CHECK_FALSE(folder.IsRegistered());
    }
    {
        FolderP2PLinkBroker share(kFileShareP2PPurpose);
        CHECK(share.Purpose() == kFileShareP2PPurpose);
        REQUIRE(share.Register(&service));
        share.Unregister();
        //未登记时注销：没有效果
        share.Unregister();
    }
    REQUIRE(service.m_registered.size() == 2);
    CHECK(service.m_registered[0] == "folder");
    CHECK(service.m_registered[1] == "fileshare");
    REQUIRE(service.m_unregistered.size() == 2);
    CHECK(service.m_unregistered[0] == "folder");
    CHECK(service.m_unregistered[1] == "fileshare");
}

TEST_CASE("FP2P4 真实会话经真实 P2P 链路传文件夹", "[folderp2p]")
{
    //源文件夹：根目录下一个大文件、一个空文件，子目录里一个小文件
    const std::wstring srcParent = MakeTempDir(L"src");
    const std::wstring root = srcParent + L"\\p2pfolder";
    REQUIRE(::CreateDirectoryW(root.c_str(), NULL));
    REQUIRE(::CreateDirectoryW((root + L"\\sub").c_str(), NULL));
    WriteFile(root + L"\\big.bin", kBigFileBytes, 1);
    WriteFile(root + L"\\empty.txt", 0, 2);
    WriteFile(root + L"\\sub\\small.txt", kSmallFileBytes, 3);
    FolderScanResult scan;
    REQUIRE(ScanFolderTree(root, scan));
    const std::wstring dstParent = MakeTempDir(L"dst");
    //两端会话与引擎的日志写到源目录旁，失败时据此排查（用例结束时关闭）
    CIULog::Init(true, false, (srcParent + L"\\fp2p4").c_str());

    //两个引擎，各配一个文件夹业务方
    FolderP2PLinkBroker brokerA;
    FolderP2PLinkBroker brokerB;
    FolderHost hostA(kUidA, &brokerA);
    FolderHost hostB(kUidB, &brokerB);
    p2p::P2PEngine engineA(hostA);
    p2p::P2PEngine engineB(hostB);
    hostA.SetPeer(&engineB);
    hostB.SetPeer(&engineA);
    REQUIRE(engineA.Start());
    REQUIRE(engineB.Start());

    //两端先认领（1816 先于 2302 处理），再推 2302
    std::shared_ptr<FolderP2PTransportFactory> fa = brokerA.CreateFactory(kFolderSessionId, kRelayIp, kRelayPort,
                                                                          kRelayToken, NULL);
    std::shared_ptr<FolderP2PTransportFactory> fb = brokerB.CreateFactory(kFolderSessionId, kRelayIp, kRelayPort,
                                                                          kRelayToken, NULL);
    engineA.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(p2p_role_controlling, kUidB));
    engineB.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(p2p_role_controlled, kUidA));

    FolderSendSessionParams sp;
    sp.m_relayIp = kRelayIp;
    sp.m_relayPort = kRelayPort;
    sp.m_relayToken = kRelayToken;
    sp.m_dataLanes = kDataLanes;
    sp.m_rootPath = root;
    sp.m_manifest = scan.m_manifest;
    sp.m_transportFactory = fa.get();
    FolderRecvSessionParams rp;
    rp.m_sessionId = kFolderSessionId;
    rp.m_relayIp = kRelayIp;
    rp.m_relayPort = kRelayPort;
    rp.m_relayToken = kRelayToken;
    rp.m_dataLanes = kDataLanes;
    rp.m_saveParentDir = dstParent;
    rp.m_transportFactory = fb.get();

    SendRecorder sendRec;
    RecvRecorder recvRec;
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    FolderSendSession sender;
    FolderRecvSession receiver;
    REQUIRE(receiver.Start(rp, state, &recvRec));
    REQUIRE(sender.Start(sp, &sendRec));
    const bool sendDone = WaitAtomic(sendRec.m_completed, 1, kWaitTransferMs);
    const bool recvDone = WaitAtomic(recvRec.m_completed, 1, kWaitTransferMs);
    INFO("send failed " << (int)sendRec.m_failed << " code " << (int)sendRec.m_errcode << ", recv failed "
         << (int)recvRec.m_failed << " code " << (int)recvRec.m_errcode);
    CHECK(sendDone);
    CHECK(recvDone);
    //两端走的都是 P2P
    CHECK(fa->DecideP2P());
    CHECK(fb->DecideP2P());

    std::wstring landed;
    {
        std::lock_guard<std::mutex> guard(recvRec.m_mutex);
        landed = recvRec.m_rootPath;
    }
    CHECK(ReadFile(landed + L"\\big.bin") == ReadFile(root + L"\\big.bin"));
    CHECK(ReadFile(landed + L"\\empty.txt") == ReadFile(root + L"\\empty.txt"));
    CHECK(ReadFile(landed + L"\\sub\\small.txt") == ReadFile(root + L"\\sub\\small.txt"));

    sender.Stop();
    receiver.Stop();
    hostA.SetPeer(NULL);
    hostB.SetPeer(NULL);
    fa.reset();
    fb.reset();
    engineA.Stop();
    engineB.Stop();
    CIULog::Uninit();
}

namespace {

//FP2P6：业务自定的单帧上限（字节），比文件夹中转的应用帧上限大，模拟远程桌面的关键帧
const size_t kBigFrameLimit = 8 * 1024 * 1024;

//FP2P6：比文件夹中转上限大、不超过业务上限的一帧（字节）
const size_t kBigFrameBytes = 5 * 1024 * 1024;

//FP2P6：流通道等可写通知的最长时间（毫秒）：假链路不会拒绝发送，用不到
const int kStreamSendWaitMs = 1000;

//FP2P6：长度字段为 16 MiB 的帧头，超过业务上限
const uint8_t kOverLimitHeader[kP2PStreamFrameHeaderBytes] = { 0x01, 0x00, 0x00, 0x00 };

/**
 *  流通道一侧的回调：记录收到的帧与断开。
 */
class StreamRecorder : public IP2PStreamHandler
{
public:
    StreamRecorder()
        : m_closed(0)
    {
    }

    virtual void OnStreamFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_frames.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnStreamClosed() override
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
    std::vector<std::string> m_frames;   //收到的帧
    std::atomic<int>         m_closed;   //报告断开的次数
};

bool WaitStreamFrames(StreamRecorder& r, size_t count, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (r.FrameCount() >= count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//把一段字节交给链路槽的某条通道
void FeedSlot(P2PLinkSlot& slot, uint16_t channelId, const std::string& bytes)
{
    slot.OnData(channelId, reinterpret_cast<const uint8_t*>(bytes.data()), (uint32_t)bytes.size());
}

} // namespace

TEST_CASE("FP2P6 共用链路层：业务自定单帧上限、通道只登记一次、未认领槽被替换时关闭旧链路", "[folderp2p]")
{
    //假链路都在业务方之前声明：链路槽在析构时关闭它持有的链路，假链路必须比所有槽活得久
    FakeLink link;
    FakeLink oldLink;
    FakeLink newLink;
    P2PLinkBroker broker("rd");
    CHECK(broker.Purpose() == "rd");
    broker.OnLinkReady("rd-1", &link);
    std::shared_ptr<P2PLinkSlot> slot = broker.ClaimSlot("rd-1");
    REQUIRE(slot->Wait(0) == P2PLinkSlot::slot_ready);

    StreamRecorder rec;
    P2PStreamChannel channel(slot, kBigFrameLimit, kStreamSendWaitMs);
    REQUIRE(channel.Open(1, &rec));
    CHECK(channel.ChannelId() == 1);
    CHECK(channel.IsOpen());
    //只能打开一次
    CHECK_FALSE(channel.Open(2, &rec));

    //比文件夹中转上限大的帧：发出时带长度字段，收进来整帧交出
    REQUIRE(kBigFrameBytes > kFolderRelayMaxAppFrameBytes);
    std::string big(kBigFrameBytes, '\0');
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = (char)((i * kPatternStep) % kPatternModulo);
    CHECK(channel.SendFrame(reinterpret_cast<const unsigned char*>(big.data()), big.size()));
    CHECK(link.m_sent == WithLength(big));
    FeedSlot(*slot, 1, WithLength(big));
    REQUIRE(WaitStreamFrames(rec, 1, kWaitMs));
    CHECK(rec.FrameAt(0) == big);
    //超过业务上限：发不出
    const std::string tooBig(kBigFrameLimit + 1, 'x');
    CHECK_FALSE(channel.SendFrame(reinterpret_cast<const unsigned char*>(tooBig.data()), tooBig.size()));

    //同一通道号的第二条流通道打不开；它关闭时不影响第一条的登记
    {
        StreamRecorder other;
        P2PStreamChannel dup(slot, kBigFrameLimit, kStreamSendWaitMs);
        CHECK_FALSE(dup.Open(1, &other));
        dup.Close();
    }
    const std::string afterDup = "after duplicate";
    FeedSlot(*slot, 1, WithLength(afterDup));
    REQUIRE(WaitStreamFrames(rec, 2, kWaitMs));
    CHECK(rec.FrameAt(1) == afterDup);

    //收到的长度超过业务上限：报告断开
    slot->OnData(1, kOverLimitHeader, sizeof(kOverLimitHeader));
    REQUIRE(WaitAtomic(rec.m_closed, 1, kWaitMs));
    CHECK_FALSE(channel.IsOpen());
    channel.Close();
    CHECK(rec.m_closed == 1);

    //同一会话号上未认领的就绪槽被新链路替换：旧槽的链路被关闭，新链路留给认领者
    broker.OnLinkReady("rd-2", &oldLink);
    broker.OnLinkReady("rd-2", &newLink);
    CHECK(oldLink.m_closes == 1);
    CHECK(oldLink.m_refs == 0);
    std::shared_ptr<P2PLinkSlot> slot2 = broker.ClaimSlot("rd-2");
    CHECK(slot2->Link() == &newLink);

    //业务结束时立即关闭链路：引用保留到槽析构，析构时不再重复关闭
    slot2->CloseLink();
    CHECK(newLink.m_closes == 1);
    CHECK(newLink.m_closeReason == p2p_close_business_end);
    CHECK(newLink.m_refs == 1);
    CHECK(slot2->Link() == NULL);
    CHECK(slot2->State() == P2PLinkSlot::slot_closed);
    slot2->CloseLink();
    CHECK(newLink.m_closes == 1);
    slot2.reset();
    CHECK(newLink.m_closes == 1);
    CHECK(newLink.m_refs == 0);
}

namespace {

//FP2P7：数据报通道排队的消息数上限
const size_t kDatagramQueueLimit = 3;

//FP2P7：在回调里被拦住时，后面再到达的消息条数（比上限多一条，最旧的一条被丢弃）
const int kDatagramBurst = 4;

/**
 *  数据报通道一侧的回调：记录收到的消息与断开；可以让回调停在里面不返回，用来把转交线程拦住。
 */
class DatagramRecorder : public IP2PDatagramHandler
{
public:
    DatagramRecorder()
        : m_closed(0)
        , m_hold(false)
    {
    }

    virtual void OnDatagram(const unsigned char* data, size_t len) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_messages.push_back(std::string(reinterpret_cast<const char*>(data), len));
        while (m_hold)
            m_cv.wait(lock);
    }

    virtual void OnDatagramClosed() override
    {
        ++m_closed;
    }

    //拦住或放行转交线程
    void SetHold(bool hold)
    {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_hold = hold;
        }
        m_cv.notify_all();
    }

    size_t Count()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_messages.size();
    }

    std::string At(size_t i)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_messages[i];
    }

    std::mutex               m_mutex;      //保护 m_messages 与 m_hold
    std::condition_variable  m_cv;         //放行时唤醒停在回调里的转交线程
    std::vector<std::string> m_messages;   //收到的消息
    std::atomic<int>         m_closed;     //报告断开的次数
    bool                     m_hold;       //是否让回调停在里面不返回
};

bool WaitDatagrams(DatagramRecorder& r, size_t count, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
    {
        if (r.Count() >= count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return false;
}

//把一条消息交给链路槽的某条通道
void FeedText(P2PLinkSlot& slot, uint16_t channelId, const std::string& text)
{
    slot.OnData(channelId, reinterpret_cast<const uint8_t*>(text.data()), (uint32_t)text.size());
}

} // namespace

TEST_CASE("FP2P7 共用链路层的数据报通道：整条收发、与帧通道并存、排队上限丢最旧、断开与关闭", "[folderp2p]")
{
    FakeLink link;
    P2PLinkBroker broker("call");
    broker.OnLinkReady("call-1", &link);
    std::shared_ptr<P2PLinkSlot> slot = broker.ClaimSlot("call-1");
    REQUIRE(slot->Wait(0) == P2PLinkSlot::slot_ready);

    DatagramRecorder rec;
    P2PDatagramChannel datagram(slot, kDatagramQueueLimit);
    REQUIRE(datagram.Open(1, &rec));
    CHECK(datagram.IsOpen());
    CHECK_FALSE(datagram.Open(3, &rec));
    REQUIRE(link.m_channels.size() == 1);
    CHECK(link.m_channels[0] == 1);
    CHECK(link.m_kinds[0] == p2p::p2p_channel_datagram);

    //发送：消息原样整条交给链路，不加长度字段
    const std::string audio = "opus-packet";
    CHECK(datagram.Send(reinterpret_cast<const unsigned char*>(audio.data()), audio.size()) == p2p::p2p_send_ok);
    CHECK(link.SentCopy() == audio);

    //同一条链路上的帧通道：互不干扰
    StreamRecorder streamRec;
    P2PStreamChannel stream(slot, kBigFrameLimit, kStreamSendWaitMs);
    REQUIRE(stream.Open(2, &streamRec));
    REQUIRE(link.m_kinds.size() == 2);
    CHECK(link.m_kinds[1] == p2p::p2p_channel_reliable);
    FeedText(*slot, 1, "m1");
    FeedText(*slot, 2, WithLength("frame-on-stream"));
    FeedText(*slot, 1, "m2");
    REQUIRE(WaitDatagrams(rec, 2, kWaitMs));
    REQUIRE(WaitStreamFrames(streamRec, 1, kWaitMs));
    CHECK(rec.At(0) == "m1");
    CHECK(rec.At(1) == "m2");
    CHECK(streamRec.FrameAt(0) == "frame-on-stream");

    //回调停住时继续到达的消息：排队超过上限丢最旧的
    rec.SetHold(true);
    FeedText(*slot, 1, "held");
    REQUIRE(WaitDatagrams(rec, 3, kWaitMs));
    for (int i = 0; i < kDatagramBurst; ++i)
        FeedText(*slot, 1, "burst-" + std::to_string(i));
    CHECK(datagram.DroppedCount() == (uint64_t)(kDatagramBurst - (int)kDatagramQueueLimit));
    rec.SetHold(false);
    REQUIRE(WaitDatagrams(rec, 3 + kDatagramQueueLimit, kWaitMs));
    CHECK(rec.At(3) == "burst-1");
    CHECK(rec.At(5) == "burst-3");

    //链路断开：报告一次，之后发送返回已关闭
    broker.OnLinkClosed("call-1", p2p_close_link_timeout);
    REQUIRE(WaitAtomic(rec.m_closed, 1, kWaitMs));
    CHECK_FALSE(datagram.IsOpen());
    CHECK(datagram.Send(reinterpret_cast<const unsigned char*>(audio.data()), audio.size()) == p2p::p2p_send_closed);
    datagram.Close();
    stream.Close();
    CHECK(rec.m_closed == 1);

    //本端关闭之后：不再回调
    FakeLink link2;
    P2PLinkBroker broker2("call");
    broker2.OnLinkReady("call-2", &link2);
    std::shared_ptr<P2PLinkSlot> slot2 = broker2.ClaimSlot("call-2");
    DatagramRecorder rec2;
    P2PDatagramChannel datagram2(slot2, kDatagramQueueLimit);
    REQUIRE(datagram2.Open(1, &rec2));
    datagram2.Close();
    FeedText(*slot2, 1, "after-close");
    broker2.OnLinkClosed("call-2", p2p_close_link_timeout);
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs * 10));
    CHECK(rec2.Count() == 0);
    CHECK(rec2.m_closed == 0);
}

TEST_CASE("FP2P8 业务放弃会话号：已就绪的链路立即关闭，之后才到的链路直接关闭", "[folderp2p]")
{
    FakeLink ready;
    FakeLink late;
    FakeLink unclaimedLink;
    FakeLink again;
    P2PLinkBroker broker("call");

    //已认领、已就绪：放弃时立即以业务结束关闭
    broker.OnLinkReady("c1", &ready);
    std::shared_ptr<P2PLinkSlot> slot = broker.ClaimSlot("c1");
    broker.Abandon("c1");
    CHECK(ready.m_closes == 1);
    CHECK(ready.m_closeReason == p2p_close_business_end);
    CHECK(slot->Link() == NULL);
    slot.reset();
    CHECK(ready.m_refs == 0);

    //链路还没建好就放弃：之后 DLL 交来时直接关闭，不增加引用，也不进链路槽
    std::shared_ptr<P2PLinkSlot> waiting = broker.ClaimSlot("c2");
    broker.Abandon("c2");
    waiting.reset();
    broker.OnLinkReady("c2", &late);
    CHECK(late.m_closes == 1);
    CHECK(late.m_refs == 0);
    std::shared_ptr<P2PLinkSlot> fresh = broker.ClaimSlot("c2");
    CHECK(fresh->State() == P2PLinkSlot::slot_waiting);

    //没被认领的就绪链路：放弃时随之关闭
    broker.OnLinkReady("c3", &unclaimedLink);
    broker.Abandon("c3");
    CHECK(unclaimedLink.m_closes == 1);
    CHECK(unclaimedLink.m_refs == 0);

    //建链失败的结果：只清掉记录；之后重新认领、链路到达，恢复正常
    broker.Abandon("c4");
    broker.OnLinkFailed("c4", error_code_p2p_peer_offline);
    std::shared_ptr<P2PLinkSlot> retry = broker.ClaimSlot("c4");
    broker.OnLinkReady("c4", &again);
    CHECK(retry->Link() == &again);
    CHECK(again.m_closes == 0);
}

TEST_CASE("FP2P9 链路实际路径（2026-10-04 P2P 徽章）：链路槽按统计报路径，没有链路或断开时为 0；文件夹通道工厂只在决定走 P2P 后才报", "[folderp2p]")
{
    //假链路都在业务方之前声明：链路槽在析构时关闭它持有的链路，假链路必须比所有槽活得久
    FakeLink lan;
    FakeLink relayed;
    lan.m_statsPath = p2p::p2p_link_path_lan;
    relayed.m_statsPath = p2p::p2p_link_path_relay;
    {
        P2PLinkBroker broker("rd");
        //还在等链路：0
        std::shared_ptr<P2PLinkSlot> waiting = broker.ClaimSlot("w-1");
        CHECK(waiting->LinkPath() == 0);
        //就绪：按链路统计报路径，路径变化时跟着变
        broker.OnLinkReady("r-1", &lan);
        std::shared_ptr<P2PLinkSlot> ready = broker.ClaimSlot("r-1");
        REQUIRE(ready->Wait(0) == P2PLinkSlot::slot_ready);
        CHECK(ready->LinkPath() == p2p::p2p_link_path_lan);
        lan.m_statsPath = p2p::p2p_link_path_punched;
        CHECK(ready->LinkPath() == p2p::p2p_link_path_punched);
        //断开之后：0
        broker.OnLinkClosed("r-1", 1);
        CHECK(ready->LinkPath() == 0);
        //建链失败：0
        std::shared_ptr<P2PLinkSlot> failed = broker.ClaimSlot("f-1");
        broker.OnLinkFailed("f-1", error_code_p2p_peer_offline);
        CHECK(failed->LinkPath() == 0);
    }
    {
        FolderP2PLinkBroker broker;
        //还没决定：即使链路已经就绪也报 0，数据还没开始走
        std::shared_ptr<FolderP2PTransportFactory> f = broker.CreateFactory("d-1", kRelayIp, 1, kRelayToken, NULL);
        CHECK(f->LinkPath() == 0);
        broker.OnLinkReady("d-1", &relayed);
        CHECK(f->LinkPath() == 0);
        //决定走 P2P 之后：报链路的实际路径（这里是经中转的 3，徽章据此不显示）
        REQUIRE(f->DecideP2P());
        CHECK(f->LinkPath() == p2p::p2p_link_path_relay);
        //建链失败、退回 fileserver 中转：0
        std::shared_ptr<FolderP2PTransportFactory> g = broker.CreateFactory("d-2", kRelayIp, 1, kRelayToken, NULL);
        broker.OnLinkFailed("d-2", error_code_p2p_peer_offline);
        CHECK_FALSE(g->DecideP2P());
        CHECK(g->LinkPath() == 0);
    }
}

namespace {

//FP2P10：直传的文件名与内容种子
const wchar_t* const kDirectFileNameW = L"one.bin";
const int kDirectFileSalt = 5;

//FP2P11：建链失败后看中转地址有没有被连上的等待时长（微秒）与通道连接超时（秒）
const long kNoConnectWaitUs = 200000;
const int kLaneConnectTimeoutSec = 2;

//MD5 的字节数
const int kFp2pMd5Bytes = 16;

//一个文件内容的 MD5（32 位小写十六进制）；读不到时返回空串
std::string Md5OfFile(const std::wstring& path)
{
    const std::string data = ReadFile(path);
    if (data == "<missing>")
        return std::string();
    MD5_CTX ctx;
    MD5Init(&ctx);
    if (!data.empty())
        MD5Update(&ctx, reinterpret_cast<const unsigned char*>(data.data()), (unsigned int)data.size());
    unsigned char digest[kFp2pMd5Bytes];
    MD5Final(digest, &ctx);
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (int i = 0; i < kFp2pMd5Bytes; ++i)
    {
        hex += kHex[digest[i] >> 4];
        hex += kHex[digest[i] & 0x0F];
    }
    return hex;
}

} // namespace

TEST_CASE("FP2P10 单文件直传经真实 P2P 链路：落在 direct 目录，两端扩展点报出与内容一致的 MD5，路径为局域网直连", "[folderp2p]")
{
    const std::wstring srcDir = MakeTempDir(L"dsrc");
    const std::wstring file = srcDir + L"\\" + kDirectFileNameW;
    WriteFile(file, kBigFileBytes, kDirectFileSalt);
    const std::wstring dstBase = MakeTempDir(L"ddst");
    CIULog::Init(true, false, (srcDir + L"\\fp2p10").c_str());

    FolderP2PLinkBroker brokerA;
    FolderP2PLinkBroker brokerB;
    FolderHost hostA(kUidA, &brokerA);
    FolderHost hostB(kUidB, &brokerB);
    p2p::P2PEngine engineA(hostA);
    p2p::P2PEngine engineB(hostB);
    hostA.SetPeer(&engineB);
    hostB.SetPeer(&engineA);
    REQUIRE(engineA.Start());
    REQUIRE(engineB.Start());

    std::shared_ptr<FolderP2PTransportFactory> fa = brokerA.CreateFactory(kFolderSessionId, kRelayIp, kRelayPort,
                                                                          kRelayToken, NULL);
    std::shared_ptr<FolderP2PTransportFactory> fb = brokerB.CreateFactory(kFolderSessionId, kRelayIp, kRelayPort,
                                                                          kRelayToken, NULL);
    //直传与正式代码一样不允许退回 fileserver 中转
    fa->SetRelayFallback(false);
    fb->SetRelayFallback(false);
    engineA.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(p2p_role_controlling, kUidB));
    engineB.OnServerFrame(cmd_p2p_session_notify, 0, NotifyJson(p2p_role_controlled, kUidA));

    foldertransfer::DirectFileSendHooks sendHooks;
    FolderSendSessionParams sp;
    sp.m_relayIp = kRelayIp;
    sp.m_relayPort = kRelayPort;
    sp.m_relayToken = kRelayToken;
    sp.m_dataLanes = kDataLanes;
    REQUIRE(foldertransfer::BuildSingleFileManifest(file, sp.m_rootPath, sp.m_manifest));
    sp.m_hooks = &sendHooks;
    sp.m_transportFactory = fa.get();

    const std::wstring saveDir = foldertransfer::DirectFileSaveDir(dstBase, kFolderSessionId);
    foldertransfer::DirectFileRecvHooks recvHooks(saveDir);
    FolderRecvSessionParams rp;
    rp.m_sessionId = kFolderSessionId;
    rp.m_relayIp = kRelayIp;
    rp.m_relayPort = kRelayPort;
    rp.m_relayToken = kRelayToken;
    rp.m_dataLanes = kDataLanes;
    rp.m_saveParentDir = saveDir;
    rp.m_hooks = &recvHooks;
    rp.m_transportFactory = fb.get();

    SendRecorder sendRec;
    RecvRecorder recvRec;
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    FolderSendSession sender;
    FolderRecvSession receiver;
    REQUIRE(receiver.Start(rp, state, &recvRec));
    REQUIRE(sender.Start(sp, &sendRec));
    const bool sendDone = WaitAtomic(sendRec.m_completed, 1, kWaitTransferMs);
    const bool recvDone = WaitAtomic(recvRec.m_completed, 1, kWaitTransferMs);
    INFO("send failed " << (int)sendRec.m_failed << " code " << (int)sendRec.m_errcode << ", recv failed "
         << (int)recvRec.m_failed << " code " << (int)recvRec.m_errcode);
    CHECK(sendDone);
    CHECK(recvDone);

    const std::wstring landed = saveDir + kDirectFileNameW;
    CHECK(ReadFile(landed) == ReadFile(file));
    const std::string md5 = Md5OfFile(file);
    REQUIRE(md5.size() == (size_t)foldertransfer::kFolderRecvMd5HexChars);
    CHECK(sendHooks.Md5() == md5);
    CHECK(recvHooks.Md5() == md5);
    CHECK(recvHooks.FinalPath() == landed);
    CHECK(fa->LinkPath() == p2p::p2p_link_path_lan);
    CHECK(fb->LinkPath() == p2p::p2p_link_path_lan);
    CHECK(fa->DecidedP2P());
    CHECK(fb->DecidedP2P());

    sender.Stop();
    receiver.Stop();
    hostA.SetPeer(NULL);
    hostB.SetPeer(NULL);
    fa.reset();
    fb.reset();
    engineA.Stop();
    engineB.Stop();
    CIULog::Uninit();
}

TEST_CASE("FP2P11 直传禁止回落 fileserver 中转：建链失败时 Connect 直接失败、不连中转地址", "[folderp2p]")
{
    WSADATA data;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    {
        //一个只监听、从不应答的端口，充当 1816 给的中转地址：被连上就能从 select 看出来
        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listener != INVALID_SOCKET);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(listener, (const sockaddr*)&addr, sizeof(addr)) == 0);
        REQUIRE(::listen(listener, SOMAXCONN) == 0);
        int len = sizeof(addr);
        REQUIRE(::getsockname(listener, (sockaddr*)&addr, &len) == 0);
        const int port = ntohs(addr.sin_port);

        FolderP2PLinkBroker broker;
        std::shared_ptr<FolderP2PTransportFactory> f = broker.CreateFactory("nf-1", kRelayIp, port, kRelayToken, NULL);
        CHECK(f->RelayFallbackAllowed());
        f->SetRelayFallback(false);
        CHECK_FALSE(f->RelayFallbackAllowed());
        broker.OnLinkFailed("nf-1", error_code_p2p_peer_offline);
        FrameRecorder rec;
        std::unique_ptr<IFolderLaneTransport> lane(f->CreateLane(0));
        CHECK_FALSE(lane->Connect(0, &rec, kLaneConnectTimeoutSec));
        CHECK_FALSE(lane->IsConnected());
        CHECK(f->LinkPath() == 0);
        //从没走上 P2P 链路：管理器据此把失败原因报成 622，发送端改为离线文件
        CHECK_FALSE(f->DecidedP2P());
        lane->Close();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listener, &fds);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = kNoConnectWaitUs;
        CHECK(::select(0, &fds, NULL, NULL, &tv) == 0);

        //对照：允许回落的工厂建链失败后去连中转地址（对方不应答，握手超时，Connect 同样失败）
        std::shared_ptr<FolderP2PTransportFactory> g = broker.CreateFactory("nf-2", kRelayIp, port, kRelayToken, NULL);
        broker.OnLinkFailed("nf-2", error_code_p2p_peer_offline);
        std::unique_ptr<IFolderLaneTransport> lane2(g->CreateLane(0));
        CHECK_FALSE(lane2->Connect(0, &rec, kLaneConnectTimeoutSec));
        lane2->Close();
        FD_ZERO(&fds);
        FD_SET(listener, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = kNoConnectWaitUs;
        CHECK(::select(0, &fds, NULL, NULL, &tv) == 1);

        ::closesocket(listener);
    }
    ::WSACleanup();
}

TEST_CASE("FP2P12 直传的业务方用途为 file；业务正在用 P2P 时才报链路槽的路径", "[folderp2p]")
{
    CHECK(DirectFileLinkBroker().Purpose() == foldertransfer::kDirectFileP2PPurpose);
    CHECK(DirectFileLinkBroker().Purpose() != kFolderP2PPurpose);

    FakeLink link;
    link.m_statsPath = p2p::p2p_link_path_punched;
    {
        P2PLinkBroker broker("rd");
        std::shared_ptr<P2PLinkSlot> empty;
        CHECK(LinkPathWhenUsing(true, empty) == 0);
        std::shared_ptr<P2PLinkSlot> waiting = broker.ClaimSlot("lw-1");
        CHECK(LinkPathWhenUsing(true, waiting) == 0);
        broker.OnLinkReady("lw-2", &link);
        std::shared_ptr<P2PLinkSlot> ready = broker.ClaimSlot("lw-2");
        //业务没在用 P2P（走着中转）：0
        CHECK(LinkPathWhenUsing(false, ready) == 0);
        //正在用：链路槽的路径
        CHECK(LinkPathWhenUsing(true, ready) == p2p::p2p_link_path_punched);
    }
}
