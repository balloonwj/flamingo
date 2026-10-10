/**
 *  文件共享信令转发通道单元测试（Suite FSC，标签 [fileshare-channel]），ShareChannelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 10 步，2026-10-02 与用户确认的用例）：
 *    - FSC1 主程序代 DLL 发送共享信令的放行规则（ShareChannelPolicy）；
 *    - FSC2 服务端帧从网络线程送到 DLL 的路由（CShareFrameRouter）：未挂接时丢弃、挂接后原样送到、
 *           号段外不送、解除挂接后丢弃、多线程同时挂接解除送帧时计数一致。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "../Feature/FileShare/ShareChannel.h"

using namespace balloonfileshare;

namespace {

//共享段内的几条命令：首、尾与 2205 拉取列表
const uint16_t kShareFirstCmd = 2200;
const uint16_t kShareLastCmd = 2299;
const uint16_t kShareListCmd = 2205;

//紧邻共享段两侧的命令与一条普通消息命令
const uint16_t kBelowShareCmd = 2199;
const uint16_t kAboveShareCmd = 2300;
const uint16_t kMessageNotify = 1402;

//FSC2 送帧用的 seq 与包体
const uint32_t kListSeq = 77;
const char* const kListBody = "{\"code\":0,\"shares\":[]}";

//FSC2 多线程压测：送帧线程数、每条线程送的帧数、挂接解除的轮数
const int kRouteThreads = 4;
const int kFramesPerThread = 2000;
const int kAttachRounds = 500;

//一个只记录收到哪些帧的假模块；OnServerFrame 可能在多条线程上同时被调用
class RecordingModule : public fileshare::IFileShareModule
{
public:
    RecordingModule()
        : m_received(0)
        , m_lastCmd(0)
        , m_lastSeq(0)
    {
    }

    virtual uint32_t GetApiVersion() const override
    {
        return fileshare::kApiVersion;
    }

    virtual bool Init(fileshare::IFileShareHost* host, const fileshare::FileShareInitParams* params) override
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
        m_received.fetch_add(1);
        m_lastCmd = cmd;
        m_lastSeq = seq;
        m_lastBody.assign(body != NULL ? body : "", len);
        return true;
    }

    virtual bool OpenShareCenter(HWND owner) override
    {
        (void)owner;
        return false;
    }

    virtual bool OpenShareFromCard(HWND owner, int64_t shareId) override
    {
        (void)owner;
        (void)shareId;
        return false;
    }

    virtual bool OpenCreateShare(HWND owner, int ctype, int64_t cid) override
    {
        (void)owner;
        (void)ctype;
        (void)cid;
        return false;
    }

    virtual void OnConversationMembers(int ctype, int64_t cid, int code, const fileshare::FileShareMemberInfo* members,
                                       uint32_t count) override
    {
        (void)ctype;
        (void)cid;
        (void)code;
        (void)members;
        (void)count;
    }

    virtual bool OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor) override
    {
        (void)owner;
        (void)ctype;
        (void)cid;
        (void)anchor;
        return false;
    }

    virtual bool GetShareState(int64_t shareId, fileshare::FileShareCardState* out) override
    {
        (void)shareId;
        (void)out;
        return false;
    }

    virtual void NavigateImagePreview(int delta) override
    {
        (void)delta;
    }

    virtual bool WatchConversationShares(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
        return false;
    }

    virtual void UnwatchConversationShares(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
    }

    virtual bool OpenOwnShareLocally(HWND owner, int64_t shareId) override
    {
        (void)owner;
        (void)shareId;
        return false;
    }

    virtual void OnShareTargetsPicked(const fileshare::FileShareMemberInfo* targets, uint32_t count) override
    {
        (void)targets;
        (void)count;
    }

    virtual void OnShareGroupCreated(int code, int64_t groupId) override
    {
        (void)code;
        (void)groupId;
    }

    //本桩只记录收到的帧，头像变化与它无关
    virtual void OnAvatarsUpdated() override
    {
    }

    //第 12 版：没有传输
    virtual uint32_t GetActiveTransferCount() override
    {
        return 0;
    }

    std::atomic<int> m_received;   //收到的帧数
    uint16_t         m_lastCmd;    //最后一帧的 cmd（只在单线程用例里检查）
    uint32_t         m_lastSeq;    //最后一帧的 seq
    std::string      m_lastBody;   //最后一帧的包体
};

//FSC2 的送帧线程：反复送共享段内的帧，记下路由器报告送到的帧数
void RouteWorker(CShareFrameRouter* router, std::atomic<int>* delivered)
{
    for (int i = 0; i < kFramesPerThread; ++i)
    {
        if (router->Route(kShareListCmd, static_cast<uint32_t>(i), kListBody,
                          static_cast<uint32_t>(strlen(kListBody))))
            delivered->fetch_add(1);
    }
}

} // namespace

TEST_CASE("FSC1 代 DLL 发送共享信令的放行规则", "[fileshare-channel]")
{
    const uint32_t kBodyLen = 2;

    //段内、包体非空、已连接：放行
    CHECK(ShareChannelPolicy::CanSendShareCommand(kShareFirstCmd, kBodyLen, true));
    CHECK(ShareChannelPolicy::CanSendShareCommand(kShareListCmd, kBodyLen, true));
    CHECK(ShareChannelPolicy::CanSendShareCommand(kShareLastCmd, kBodyLen, true));

    //号段外：拒绝（DLL 不能借这个口子发任意命令）
    CHECK_FALSE(ShareChannelPolicy::CanSendShareCommand(kBelowShareCmd, kBodyLen, true));
    CHECK_FALSE(ShareChannelPolicy::CanSendShareCommand(kAboveShareCmd, kBodyLen, true));
    CHECK_FALSE(ShareChannelPolicy::CanSendShareCommand(kMessageNotify, kBodyLen, true));

    //包体为空、未连接：拒绝
    CHECK_FALSE(ShareChannelPolicy::CanSendShareCommand(kShareListCmd, 0, true));
    CHECK_FALSE(ShareChannelPolicy::CanSendShareCommand(kShareListCmd, kBodyLen, false));
}

TEST_CASE("FSC2 服务端帧的路由：挂接前后、号段外、解除后、多线程", "[fileshare-channel]")
{
    CShareFrameRouter router;
    RecordingModule module;
    const uint32_t bodyLen = static_cast<uint32_t>(strlen(kListBody));

    SECTION("未挂接模块时丢弃，并计入丢弃数")
    {
        CHECK_FALSE(router.Route(kShareListCmd, kListSeq, kListBody, bodyLen));
        CHECK(router.GetDroppedCount() == 1);
    }

    SECTION("挂接后 cmd、seq、包体原样送到；号段外的帧不送；解除挂接后再来的帧丢弃")
    {
        router.Attach(&module);
        REQUIRE(router.Route(kShareListCmd, kListSeq, kListBody, bodyLen));
        CHECK(module.m_received.load() == 1);
        CHECK(module.m_lastCmd == kShareListCmd);
        CHECK(module.m_lastSeq == kListSeq);
        CHECK(module.m_lastBody == kListBody);

        CHECK_FALSE(router.Route(kMessageNotify, 1, "{}", 2));
        CHECK_FALSE(router.Route(kAboveShareCmd, 1, "{}", 2));
        CHECK(module.m_received.load() == 1);

        router.Detach();
        CHECK_FALSE(router.Route(kShareListCmd, kListSeq, kListBody, bodyLen));
        CHECK(module.m_received.load() == 1);
        CHECK(router.GetDroppedCount() == 1);
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
        CHECK_FALSE(router.Route(kShareListCmd, 0, kListBody, bodyLen));
        CHECK(module.m_received.load() == before);
    }
}
