/**
 *  P2P 信令转发通道单元测试（Suite P2PC，标签 [p2phost]），P2PChannelTests.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 1 期第 1c 步，2026-10-03 与用户确认的用例）：
 *    - P2PC1 主程序代 DLL 发送 P2P 信令的放行规则（P2PChannelPolicy）：只放行 2300~2399 段、包体非空、
 *            主连接已连上的命令；
 *    - P2PC2 服务端帧从网络线程送到 DLL 的路由（CP2PFrameRouter）：未挂接时丢弃并计数、挂接后只转交 2300 段的帧、
 *            解除挂接时等正在进行的转交结束、此后不再转交；多线程同时送帧与反复挂接解除时计数一致。
 *  做法与文件共享的 ShareChannelTests（FSC1、FSC2）相同。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../net/Protocol.h"
#include "../Feature/P2P/P2PChannel.h"

using namespace balloonp2p;

namespace {

//P2P 段的首尾
const uint16_t kP2PFirstCmd = p2p::kP2PCmdFirst;
const uint16_t kP2PLastCmd = p2p::kP2PCmdLast;

//紧邻 P2P 段两侧的命令
const uint16_t kBelowP2PCmd = p2p::kP2PCmdFirst - 1;
const uint16_t kAboveP2PCmd = p2p::kP2PCmdLast + 1;

//P2PC2 送帧用的 seq 与包体
const uint32_t kNotifySeq = 0;
const uint32_t kSignalSeq = 41;
const char* const kSignalBody = "{\"p2psessionid\":\"p2p-1-2-3-4\",\"data\":{}}";

//P2PC2 多线程压测：送帧线程数、每条线程送的帧数、挂接解除的轮数
const int kRouteThreads = 4;
const int kFramesPerThread = 2000;
const int kAttachRounds = 500;

//P2PC2 「解除挂接会等正在进行的转交结束」：模块在 OnServerFrame 里停留的毫秒数
const DWORD kSlowFrameMs = 200;

//P2PC2 等送帧线程进入模块的最长时间（毫秒）；超时说明路由没有把帧交给模块，直接判失败而不是一直等
const DWORD kEnterFrameTimeoutMs = 2000;

//只记录收到哪些帧的假模块；OnServerFrame 可能在多条线程上同时被调用
class RecordingModule : public p2p::IP2PModule
{
public:
    RecordingModule()
        : m_received(0)
        , m_lastCmd(0)
        , m_lastSeq(0)
        , m_slow(false)
        , m_inFrame(false)
    {
    }

    virtual uint32_t GetApiVersion() const override
    {
        return p2p::kApiVersion;
    }

    virtual bool Init(p2p::IP2PHost* host, const p2p::P2PInitParams* params) override
    {
        (void)host;
        (void)params;
        return true;
    }

    virtual void Shutdown() override
    {
    }

    virtual void OnLoggedIn() override
    {
    }

    virtual void OnLoggingOut() override
    {
    }

    virtual bool OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override
    {
        m_inFrame = true;
        if (m_slow)
            ::Sleep(kSlowFrameMs);
        {
            std::lock_guard<std::mutex> guard(m_lastMutex);
            m_lastCmd = cmd;
            m_lastSeq = seq;
            m_lastBody.assign(body != NULL ? body : "", len);
        }
        ++m_received;
        m_inFrame = false;
        return true;
    }

    virtual p2p::IP2PService* GetService() override
    {
        return NULL;
    }

    std::atomic<int>  m_received;    //收到的帧数
    std::mutex        m_lastMutex;   //保护下面三个记录最近一帧的成员
    uint16_t          m_lastCmd;     //最近一帧的命令号
    uint32_t          m_lastSeq;     //最近一帧的 seq
    std::string       m_lastBody;    //最近一帧的包体
    std::atomic<bool> m_slow;        //为 true 时 OnServerFrame 停留 kSlowFrameMs 毫秒
    std::atomic<bool> m_inFrame;     //是否正有线程在 OnServerFrame 里
};

//送帧线程：反复送同一帧，统计被模块接收的次数
void RouteWorker(CP2PFrameRouter* router, std::atomic<int>* delivered)
{
    const uint32_t len = static_cast<uint32_t>(strlen(kSignalBody));
    for (int i = 0; i < kFramesPerThread; ++i)
    {
        if (router->Route(cmd_p2p_signal, kSignalSeq, kSignalBody, len))
            ++(*delivered);
    }
}

//在别的线程上送一帧（P2PC2 「解除挂接会等转交结束」用）
void RouteOnce(CP2PFrameRouter* router)
{
    router->Route(cmd_p2p_signal, kSignalSeq, kSignalBody, static_cast<uint32_t>(strlen(kSignalBody)));
}

} // namespace

TEST_CASE("P2PC1 代 DLL 发送 P2P 信令的放行规则", "[p2phost]")
{
    const uint32_t kBodyLen = 2;

    //段内、包体非空、已连接：放行
    CHECK(P2PChannelPolicy::CanSendP2PCommand(kP2PFirstCmd, kBodyLen, true));
    CHECK(P2PChannelPolicy::CanSendP2PCommand(cmd_p2p_register, kBodyLen, true));
    CHECK(P2PChannelPolicy::CanSendP2PCommand(kP2PLastCmd, kBodyLen, true));

    //号段外：拒绝（DLL 不能借这个口子发任意命令），含文件共享的命令与回落中转的 1861
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(kBelowP2PCmd, kBodyLen, true));
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(kAboveP2PCmd, kBodyLen, true));
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(cmd_share_create, kBodyLen, true));
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(cmd_p2p_relay_hello, kBodyLen, true));

    //包体为空、未连接：拒绝
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(cmd_p2p_register, 0, true));
    CHECK_FALSE(P2PChannelPolicy::CanSendP2PCommand(cmd_p2p_register, kBodyLen, false));
}

TEST_CASE("P2PC2 服务端帧的路由：挂接前后、号段外、解除时等待、解除后、多线程", "[p2phost]")
{
    CP2PFrameRouter router;
    RecordingModule module;
    const uint32_t bodyLen = static_cast<uint32_t>(strlen(kSignalBody));

    SECTION("未挂接模块时丢弃，并计入丢弃数")
    {
        CHECK_FALSE(router.Route(cmd_p2p_signal, kSignalSeq, kSignalBody, bodyLen));
        CHECK(router.GetDroppedCount() == 1);
        CHECK(module.m_received.load() == 0);
    }

    SECTION("挂接后 cmd、seq、包体原样送到；号段外的帧不送也不计丢弃；解除挂接后再来的帧丢弃")
    {
        router.Attach(&module);
        REQUIRE(router.Route(cmd_p2p_signal, kSignalSeq, kSignalBody, bodyLen));
        CHECK(module.m_received.load() == 1);
        CHECK(module.m_lastCmd == cmd_p2p_signal);
        CHECK(module.m_lastSeq == kSignalSeq);
        CHECK(module.m_lastBody == kSignalBody);
        CHECK(router.Route(cmd_p2p_session_notify, kNotifySeq, "{}", 2));
        CHECK(module.m_received.load() == 2);

        CHECK_FALSE(router.Route(cmd_share_create, 1, "{}", 2));
        CHECK_FALSE(router.Route(kAboveP2PCmd, 1, "{}", 2));
        CHECK_FALSE(router.Route(kBelowP2PCmd, 1, "{}", 2));
        CHECK(module.m_received.load() == 2);
        CHECK(router.GetDroppedCount() == 0);

        router.Detach();
        CHECK_FALSE(router.Route(cmd_p2p_signal, kSignalSeq, kSignalBody, bodyLen));
        CHECK(module.m_received.load() == 2);
        CHECK(router.GetDroppedCount() == 1);
    }

    SECTION("解除挂接时，正在模块里处理的那一帧结束之后 Detach 才返回")
    {
        router.Attach(&module);
        module.m_slow = true;
        std::thread sender(RouteOnce, &router);
        //等送帧线程进入模块；超时（路由没把帧交给模块）就先收回线程再判失败
        const DWORD start = ::GetTickCount();
        while (!module.m_inFrame.load() && ::GetTickCount() - start < kEnterFrameTimeoutMs)
            ::Sleep(1);
        if (!module.m_inFrame.load())
        {
            sender.join();
            FAIL("the routed frame never reached the module");
        }
        router.Detach();
        //Detach 返回时，那一帧必须已经处理完
        CHECK_FALSE(module.m_inFrame.load());
        CHECK(module.m_received.load() == 1);
        sender.join();
    }

    SECTION("多条线程送帧的同时反复挂接与解除：送到的帧数与模块收到的一致，解除后不再送到")
    {
        std::atomic<int> delivered(0);
        std::vector<std::thread> threads;
        for (int i = 0; i < kRouteThreads; ++i)
            threads.push_back(std::thread(RouteWorker, &router, &delivered));
        for (int round = 0; round < kAttachRounds; ++round)
        {
            router.Attach(&module);
            router.Detach();
        }
        for (size_t i = 0; i < threads.size(); ++i)
            threads[i].join();

        CHECK(delivered.load() == module.m_received.load());
        CHECK(delivered.load() + static_cast<int>(router.GetDroppedCount()) == kRouteThreads * kFramesPerThread);

        //最后一次是解除：此后的帧一律丢弃
        const int before = module.m_received.load();
        CHECK_FALSE(router.Route(cmd_p2p_signal, 0, kSignalBody, bodyLen));
        CHECK(module.m_received.load() == before);
    }
}
