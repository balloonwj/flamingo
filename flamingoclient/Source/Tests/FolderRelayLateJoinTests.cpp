/**
 *  文件夹传输经 fileserver 中转时「接收端晚报到」的测试（Suite FRL，标签 [folderrelay]）。
 *
 *  fileserver 的文件夹中转在两端的同序号通道都报到之前收到的应用帧直接丢弃（FileSession::onFolderRelayData），而发送端
 *  连好通道就发清单。接收端晚一步报到（界面线程正忙、迟迟没处理 1816）时，清单就丢了，发送端等满 300 秒回执后整笔失败。
 *
 *    FRL1 真实的 FolderSendSession / FolderRecvSession 经一个模拟 fileserver 中转的假通道工厂传一个文件夹，接收端控制通道
 *         晚 1.5 秒报到：发送端第一次发出的清单被丢弃，之后的重发让传输在限定时间内完成，落地文件逐个与源文件一致；
 *    FRL2 清单重发的间隔：1、2、4、8、16 秒，之后一直是 16 秒。
 *
 *  清单重组器容忍重发的规则另见 FolderRelayFramingTests.cpp 的 FTF15。
 *
 *  框架 Catch2 v3。本文件不使用预编译头：会话头文件带进 <winsock2.h>，必须先于 <windows.h>。
 *
 *  典型用法：FlamingoUnit.exe "[folderrelay]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../Feature/FolderTransfer/FolderSendSession.h"
#include "../Feature/FolderTransfer/FolderRecvSession.h"
#include "../Feature/FolderTransfer/FolderManifest.h"
#include "../Feature/FolderTransfer/FolderLaneTransport.h"

using namespace foldertransfer;

namespace {

//等待传输完成的最长时间（毫秒）。修好之后接收端晚报到 1.5 秒、发送端第二次重发（发出后第 3 秒）就能送达，
//留足余量；修之前发送端要等满 300 秒回执，必然超过这个时限
const int kWaitTransferMs = 20000;
//轮询间隔（毫秒）
const int kPollMs = 10;
//接收端控制通道晚报到的时长（毫秒），长于发送端第一次重发前的等待（1 秒），保证第一次重发同样被丢弃
const int kReceiverLane0DelayMs = 1500;
//数据通道条数
const int kDataLanes = 2;
//发送端与接收端在假中转里的编号
const int kSideSender = 0;
const int kSideReceiver = 1;
//测试文件内容的生成参数：第 i 个字节为 (i * kPatternStep + salt) % kPatternModulo
const size_t kPatternStep = 7;
const size_t kPatternModulo = 251;
//测试文件的字节数：一个跨多个分片（512 KiB 一片）的大文件、一个小文件
const size_t kBigFileBytes = 1300 * 1024;
const size_t kSmallFileBytes = 1000;
//接收会话号（日志与接收状态用）
const char* const kSessionId = "ft-relay-latejoin-1";
//会话参数要求的中转地址、端口与令牌：两端都不为空才肯启动；通道由假中转工厂创建，不会去连这个地址
const char* const kRelayIp = "127.0.0.1";
const int kRelayPort = 29999;
const char* const kRelayToken = "token-latejoin";

/**
 *  模拟 fileserver 的文件夹中转：发送端与接收端的同序号通道互相转发应用帧；对端同序号通道还没报到时直接丢弃本帧并
 *  计数，与 FileSession::onFolderRelayData 相同。每条已报到的通道有一条投递线程，按到达顺序回调会话交来的 handler，
 *  与真实中转连接在接收线程上回调相同。
 */
class FakeRelayHub
{
public:
    FakeRelayHub()
        : m_dropped(0)
    {
    }

    ~FakeRelayHub()
    {
        std::vector<std::pair<int, int> > keys;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            for (std::map<std::pair<int, int>, std::shared_ptr<Endpoint> >::iterator it = m_endpoints.begin();
                 it != m_endpoints.end(); ++it)
                keys.push_back(it->first);
        }
        for (size_t i = 0; i < keys.size(); ++i)
            Unregister(keys[i].first, keys[i].second);
    }

    //一条通道报到：此后发给它的帧由投递线程回调 handler
    void Register(int side, int lane, FolderRelayClientHandler* handler)
    {
        std::shared_ptr<Endpoint> ep(new Endpoint());
        ep->m_handler = handler;
        ep->m_thread = std::thread(&FakeRelayHub::DeliverProc, ep);
        std::lock_guard<std::mutex> guard(m_mutex);
        m_endpoints[std::make_pair(side, lane)] = ep;
    }

    //一条通道关闭：停掉它的投递线程，之后发给它的帧一律丢弃
    void Unregister(int side, int lane)
    {
        std::shared_ptr<Endpoint> ep;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            std::map<std::pair<int, int>, std::shared_ptr<Endpoint> >::iterator it =
                m_endpoints.find(std::make_pair(side, lane));
            if (it == m_endpoints.end())
                return;
            ep = it->second;
            m_endpoints.erase(it);
        }
        {
            std::lock_guard<std::mutex> guard(ep->m_mutex);
            ep->m_stop = true;
        }
        ep->m_cv.notify_all();
        if (ep->m_thread.get_id() == std::this_thread::get_id())
            ep->m_thread.detach();
        else if (ep->m_thread.joinable())
            ep->m_thread.join();
    }

    //一端在某条通道上发出一帧：转给对端同序号的通道，对端还没报到时丢弃
    void Deliver(int fromSide, int lane, const unsigned char* data, size_t len)
    {
        std::shared_ptr<Endpoint> peer;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            std::map<std::pair<int, int>, std::shared_ptr<Endpoint> >::iterator it =
                m_endpoints.find(std::make_pair(1 - fromSide, lane));
            if (it != m_endpoints.end())
                peer = it->second;
        }
        if (!peer)
        {
            ++m_dropped;
            return;
        }
        {
            std::lock_guard<std::mutex> guard(peer->m_mutex);
            peer->m_queue.push_back(std::string(reinterpret_cast<const char*>(data), len));
        }
        peer->m_cv.notify_all();
    }

    std::atomic<int> m_dropped;   //因对端还没报到而丢弃的帧数

private:
    //一条已报到的通道
    struct Endpoint
    {
        FolderRelayClientHandler* m_handler;   //会话交来的回调（借用）
        std::thread               m_thread;    //投递线程
        std::mutex                m_mutex;     //保护 m_queue 与 m_stop
        std::condition_variable   m_cv;        //有新帧或要停止时通知投递线程
        std::deque<std::string>   m_queue;     //待投递的帧
        bool                      m_stop;      //投递线程是否该退出

        Endpoint()
            : m_handler(NULL)
            , m_stop(false)
        {
        }
    };

    //投递线程：按序把帧交给 handler
    static void DeliverProc(std::shared_ptr<Endpoint> ep)
    {
        for (;;)
        {
            std::string frame;
            {
                std::unique_lock<std::mutex> lock(ep->m_mutex);
                while (!ep->m_stop && ep->m_queue.empty())
                    ep->m_cv.wait(lock);
                if (ep->m_stop)
                    return;
                frame.swap(ep->m_queue.front());
                ep->m_queue.pop_front();
            }
            ep->m_handler->OnFolderRelayAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
        }
    }

    std::mutex                                                m_mutex;       //保护 m_endpoints
    std::map<std::pair<int, int>, std::shared_ptr<Endpoint> > m_endpoints;   //(端, 通道序号) → 已报到的通道
};

/**
 *  假中转上的一条通道：Connect 时（按需延迟后）向假中转报到，发帧交给假中转转发。
 */
class FakeRelayLane : public IFolderLaneTransport
{
public:
    //side：本端编号；connectDelayMs：报到前等多久（毫秒），模拟接收端晚报到
    FakeRelayLane(FakeRelayHub& hub, int side, int connectDelayMs)
        : m_hub(hub)
        , m_side(side)
        , m_connectDelayMs(connectDelayMs)
        , m_lane(-1)
        , m_connected(false)
    {
    }

    virtual ~FakeRelayLane()
    {
        Close();
    }

    virtual bool Connect(int lane, FolderRelayClientHandler* handler, int timeoutSec) override
    {
        (void)timeoutSec;
        if (m_connectDelayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(m_connectDelayMs));
        m_lane = lane;
        m_hub.Register(m_side, lane, handler);
        m_connected = true;
        return true;
    }

    virtual bool SendAppFrame(const unsigned char* data, size_t len) override
    {
        if (!m_connected)
            return false;
        //与真实中转相同：帧交给中转即算发出，对端没报到时被中转丢弃，发送方并不知道
        m_hub.Deliver(m_side, m_lane, data, len);
        return true;
    }

    virtual void Close() override
    {
        if (m_connected.exchange(false))
            m_hub.Unregister(m_side, m_lane);
    }

    virtual bool IsConnected() const override
    {
        return m_connected;
    }

private:
    FakeRelayHub&     m_hub;              //假中转（借用）
    int               m_side;             //本端编号
    int               m_connectDelayMs;   //报到前的等待（毫秒）
    int               m_lane;             //通道序号，Connect 时记下
    std::atomic<bool> m_connected;        //是否已报到且没关闭
};

/**
 *  一端的假中转通道工厂：控制通道（lane 0）可指定晚报到。
 */
class FakeRelayFactory : public IFolderLaneTransportFactory
{
public:
    FakeRelayFactory(FakeRelayHub& hub, int side, int lane0DelayMs)
        : m_hub(hub)
        , m_side(side)
        , m_lane0DelayMs(lane0DelayMs)
    {
    }

    virtual IFolderLaneTransport* CreateLane(int lane) override
    {
        return new FakeRelayLane(m_hub, m_side, lane == 0 ? m_lane0DelayMs : 0);
    }

private:
    FakeRelayHub& m_hub;            //假中转（借用）
    int           m_side;           //本端编号
    int           m_lane0DelayMs;   //控制通道报到前的等待（毫秒）
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

//最多等 timeoutMs 毫秒，直到计数达到 atLeast
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
    swprintf_s(name, L"%sfrl_%s_%lu_%llu", tmp, tag, ::GetCurrentProcessId(), (unsigned long long)::GetTickCount64());
    ::CreateDirectoryW(name, NULL);
    return name;
}

} // namespace

TEST_CASE("FRL1 接收端晚报到：第一次发出的清单被中转丢弃，重发之后传输照常完成", "[folderrelay]")
{
    //源文件夹：根目录下一个大文件、一个空文件，子目录里一个小文件
    const std::wstring srcParent = MakeTempDir(L"src");
    const std::wstring root = srcParent + L"\\latejoin";
    REQUIRE(::CreateDirectoryW(root.c_str(), NULL));
    REQUIRE(::CreateDirectoryW((root + L"\\sub").c_str(), NULL));
    WriteFile(root + L"\\big.bin", kBigFileBytes, 1);
    WriteFile(root + L"\\empty.txt", 0, 2);
    WriteFile(root + L"\\sub\\small.txt", kSmallFileBytes, 3);
    FolderScanResult scan;
    REQUIRE(ScanFolderTree(root, scan));
    const std::wstring dstParent = MakeTempDir(L"dst");

    FakeRelayHub hub;
    FakeRelayFactory senderFactory(hub, kSideSender, 0);
    FakeRelayFactory receiverFactory(hub, kSideReceiver, kReceiverLane0DelayMs);

    FolderSendSessionParams sp;
    sp.m_relayIp = kRelayIp;
    sp.m_relayPort = kRelayPort;
    sp.m_relayToken = kRelayToken;
    sp.m_dataLanes = kDataLanes;
    sp.m_rootPath = root;
    sp.m_manifest = scan.m_manifest;
    sp.m_transportFactory = &senderFactory;
    FolderRecvSessionParams rp;
    rp.m_sessionId = kSessionId;
    rp.m_relayIp = kRelayIp;
    rp.m_relayPort = kRelayPort;
    rp.m_relayToken = kRelayToken;
    rp.m_dataLanes = kDataLanes;
    rp.m_saveParentDir = dstParent;
    rp.m_transportFactory = &receiverFactory;

    SendRecorder sendRec;
    RecvRecorder recvRec;
    std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
    FolderSendSession sender;
    FolderRecvSession receiver;
    //发送端先启动：它的通道立即报到并发出清单；接收端的控制通道晚 1.5 秒才报到
    REQUIRE(sender.Start(sp, &sendRec));
    REQUIRE(receiver.Start(rp, state, &recvRec));
    const bool sendDone = WaitAtomic(sendRec.m_completed, 1, kWaitTransferMs);
    const bool recvDone = WaitAtomic(recvRec.m_completed, 1, kWaitTransferMs);
    INFO("send failed " << (int)sendRec.m_failed << " code " << (int)sendRec.m_errcode << ", recv failed "
         << (int)recvRec.m_failed << " code " << (int)recvRec.m_errcode << ", dropped " << (int)hub.m_dropped);
    //确实复现了：至少有一份清单被中转丢弃
    CHECK(hub.m_dropped > 0);
    CHECK(sendDone);
    CHECK(recvDone);
    CHECK(sendRec.m_failed == 0);
    CHECK(recvRec.m_failed == 0);

    if (recvDone)
    {
        std::wstring landed;
        {
            std::lock_guard<std::mutex> guard(recvRec.m_mutex);
            landed = recvRec.m_rootPath;
        }
        CHECK(ReadFile(landed + L"\\big.bin") == ReadFile(root + L"\\big.bin"));
        CHECK(ReadFile(landed + L"\\empty.txt") == ReadFile(root + L"\\empty.txt"));
        CHECK(ReadFile(landed + L"\\sub\\small.txt") == ReadFile(root + L"\\sub\\small.txt"));
    }

    sender.Stop();
    receiver.Stop();
}

TEST_CASE("FRL2 清单重发的间隔：1、2、4、8、16 秒，之后一直是 16 秒", "[folderrelay]")
{
    CHECK(FolderManifestResendDelayMs(0) == kFolderManifestResendFirstMs);
    CHECK(FolderManifestResendDelayMs(1) == 2000);
    CHECK(FolderManifestResendDelayMs(2) == 4000);
    CHECK(FolderManifestResendDelayMs(3) == 8000);
    CHECK(FolderManifestResendDelayMs(4) == kFolderManifestResendMaxMs);
    CHECK(FolderManifestResendDelayMs(5) == kFolderManifestResendMaxMs);
    CHECK(FolderManifestResendDelayMs(100) == kFolderManifestResendMaxMs);
    //负数按 0 处理
    CHECK(FolderManifestResendDelayMs(-1) == kFolderManifestResendFirstMs);
}
