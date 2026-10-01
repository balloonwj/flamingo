/**
 *  SyncArrivalFlowTests.cpp
 *
 *  一页同步应答到达后主面板各项处理的先后顺序（balloonmain::RunSyncArrival）的测试（标签 [syncarrival]）。
 *
 *  覆盖：
 *    ORD1 单聊、聊天窗开着：渲染 → 应用已读位点 → 立即绘制 → 写库 → 对账 → 刷新消息记录窗
 *    ORD2 群聊、聊天窗开着：同 ORD1，但不应用已读位点
 *    ORD3 消息记录窗自己拉的更早页：不渲染、不绘制；写库 → 对账 → 刷新照常；单聊仍应用已读位点
 *    ORD4 聊天窗没开：不渲染、不绘制；其余照常
 *    ORD5 写库失败：渲染与绘制已经完成，对账与刷新照常执行，返回值为 false
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonMain/SyncArrivalFlow.h"

using namespace balloonmain;

namespace {

//记录各项被调用的顺序的替身实现
class RecordingSteps : public ISyncArrivalSteps
{
public:
    //聊天窗是否打开
    bool m_bChatWindowOpen;
    //写库是否成功
    bool m_bStoreOk;
    //按调用顺序记下的各项名称
    std::vector<std::string> m_calls;

    RecordingSteps(bool bChatWindowOpen, bool bStoreOk)
        : m_bChatWindowOpen(bChatWindowOpen)
        , m_bStoreOk(bStoreOk)
    {
    }

    //以下各项只记下自己被调用过；含义见 ISyncArrivalSteps
    virtual bool HasOpenChatWindow()
    {
        return m_bChatWindowOpen;
    }
    virtual void RenderToChatWindow()
    {
        m_calls.push_back("render");
    }
    virtual void ApplyPeerRead()
    {
        m_calls.push_back("peerread");
    }
    virtual void PaintChatWindowNow()
    {
        m_calls.push_back("paint");
    }
    virtual bool StoreBatch()
    {
        m_calls.push_back("store");
        return m_bStoreOk;
    }
    virtual void ReconcileCoverage()
    {
        m_calls.push_back("reconcile");
    }
    virtual void RefreshHistoryWindow()
    {
        m_calls.push_back("refresh");
    }
};

//把调用顺序拼成一行，失败时便于阅读
std::string Joined(const std::vector<std::string>& calls)
{
    std::string s;
    for (size_t i = 0; i < calls.size(); ++i)
    {
        if (i > 0)
            s += " ";
        s += calls[i];
    }
    return s;
}

SyncArrivalContext MakeContext(bool bHistoryOwnFetch, bool bSingle)
{
    SyncArrivalContext ctx;
    ctx.m_historyOwnFetch = bHistoryOwnFetch;
    ctx.m_isSingleChat = bSingle;
    return ctx;
}

} // namespace

TEST_CASE("ORD1 单聊、聊天窗开着：先渲染并绘制，再写库、对账、刷新消息记录窗", "[syncarrival]")
{
    RecordingSteps steps(true, true);
    CHECK(RunSyncArrival(steps, MakeContext(false, true)));
    CHECK(Joined(steps.m_calls) == "render peerread paint store reconcile refresh");
}

TEST_CASE("ORD2 群聊、聊天窗开着：同单聊，但不应用已读位点", "[syncarrival]")
{
    RecordingSteps steps(true, true);
    CHECK(RunSyncArrival(steps, MakeContext(false, false)));
    CHECK(Joined(steps.m_calls) == "render paint store reconcile refresh");
}

TEST_CASE("ORD3 消息记录窗自己拉的更早页：不交给聊天窗，单聊仍应用已读位点", "[syncarrival]")
{
    RecordingSteps single(true, true);
    RunSyncArrival(single, MakeContext(true, true));
    CHECK(Joined(single.m_calls) == "peerread store reconcile refresh");

    RecordingSteps group(true, true);
    RunSyncArrival(group, MakeContext(true, false));
    CHECK(Joined(group.m_calls) == "store reconcile refresh");
}

TEST_CASE("ORD4 聊天窗没开：不渲染、不绘制，其余照常", "[syncarrival]")
{
    RecordingSteps single(false, true);
    RunSyncArrival(single, MakeContext(false, true));
    CHECK(Joined(single.m_calls) == "peerread store reconcile refresh");

    RecordingSteps group(false, true);
    RunSyncArrival(group, MakeContext(false, false));
    CHECK(Joined(group.m_calls) == "store reconcile refresh");
}

TEST_CASE("ORD5 写库失败：渲染与绘制已完成，对账与刷新照常执行", "[syncarrival]")
{
    RecordingSteps steps(true, false);
    CHECK_FALSE(RunSyncArrival(steps, MakeContext(false, true)));
    CHECK(Joined(steps.m_calls) == "render peerread paint store reconcile refresh");
}
