/**
 *  屏幕共享推流与收流管线的端到端联调用例（Suite SSPIPE，标签 [.live][screenshare]），SharePipelineLiveTests.cpp
 *
 *  用产品代码 ShareHostStreamer（发起人）与 ShareViewerReceiver（观看者）走一遍完整链路（docs/屏幕共享与录制方案.md
 *  第 11.4 节 2f）：A 经 chatserver 发起单聊共享，用注入的合成画面采集器（640×360 纯红，高不是 16 的倍数）推流；
 *  B 加入后收流。核对：B 解出的画面尺寸正好是 640×360（解码补齐的 8 行已裁掉）、颜色是红色；B 加入之后能马上看到
 *  画面（服务端在 B 加入时请求关键帧，A 立即重编）；A 暂停 / 继续时 B 收到共享内容状态的变化。
 *
 *  默认不参与常规测试。跑之前先起 tmp/calltest 的测试服务端，然后：FlamingoUnit.exe "[screenshare]"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <stdlib.h>
#include <string>
#include <vector>

#include "LiveChatClient.h"
#include "../net/FrameCrypto.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/Share/ShareHostStreamer.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareViewerReceiver.h"

using namespace livetest;
using namespace screenshare;

namespace {

//tmp/calltest 测试服务端的 chatserver 端口
const unsigned short kPipeChatPort = 26000;
const char* kPipeAccountA = "MY0127";
const char* kPipeAccountB = "MY0308";
const char* kPipePassword = "perf1234";
const int64_t kPipeUserB = 5308;
//等信令应答的时间，单位毫秒
const int kPipeWaitMs = 5000;
//合成画面的尺寸：高 360 不是 16 的倍数，解码器会补齐到 368，用来检查裁剪
const int kPipeW = 640;
const int kPipeH = 360;
//合成画面每帧之间的间隔，单位毫秒（约 30 fps）
const UINT kPipeFrameIntervalMs = 33;
//颜色比对的容差（每通道），H.264 有损压缩后的纯色会有小偏差
const int kPipeColorTol = 30;
//每像素字节数
const int kPipeBpp = 4;
//轮询等待事件的间隔与上限，单位毫秒
const DWORD kPipePollMs = 50;
const DWORD kPipeEventWaitMs = 8000;

//合成画面采集器：每次都给一帧 640×360 的纯红画面
class SolidCapturer : public IShareCapturer
{
public:
    SolidCapturer()
        : m_pixels(static_cast<size_t>(kPipeW) * kPipeH * kPipeBpp)
    {
        for (size_t i = 0; i < m_pixels.size(); i += kPipeBpp)
        {
            m_pixels[i] = 0;
            m_pixels[i + 1] = 0;
            m_pixels[i + 2] = 255;
            m_pixels[i + 3] = 255;
        }
    }

    virtual bool Start()
    {
        return true;
    }

    virtual int Capture(UINT timeoutMs, ShareCaptureFrame& out)
    {
        ::Sleep(timeoutMs < kPipeFrameIntervalMs ? timeoutMs : kPipeFrameIntervalMs);
        out.m_data = &m_pixels[0];
        out.m_stride = kPipeW * kPipeBpp;
        out.m_width = kPipeW;
        out.m_height = kPipeH;
        return share_capture_ok;
    }

    virtual void Stop()
    {
    }

    virtual bool IsMinimized() const
    {
        return false;
    }

    virtual RECT SourceRect() const
    {
        RECT rc = { 0, 0, kPipeW, kPipeH };
        return rc;
    }

private:
    std::vector<uint8_t> m_pixels;   //合成画面
};

//记录推流管线的事件
class HostEvents : public ShareHostStreamerListener
{
public:
    HostEvents()
        : m_connected(false)
        , m_failed(false)
    {
    }

    virtual void OnHostStreamEvent(int event, int value)
    {
        (void)value;
        if (event == share_host_relay_connected)
            m_connected.store(true);
        if (event == share_host_relay_failed || event == share_host_capture_failed)
            m_failed.store(true);
    }

    std::atomic<bool> m_connected;   //已连上中转
    std::atomic<bool> m_failed;      //连接或采集失败
};

//记录收流管线的事件
class ViewerEvents : public ShareViewerSink
{
public:
    ViewerEvents()
        : m_frames(0)
        , m_lastState(-1)
    {
    }

    virtual void OnViewerFrame()
    {
        m_frames.fetch_add(1);
    }

    virtual void OnViewerEvent(int event, int value)
    {
        if (event == share_viewer_source_state)
            m_lastState.store(value);
    }

    std::atomic<int> m_frames;      //收到的画面通知次数
    std::atomic<int> m_lastState;   //最近一次共享内容状态
};

void pipeLogin(LiveChatClient& c, const char* account)
{
    std::string reason;
    const bool ok = c.ConnectAndLogin(kPipeChatPort, kTestServerPubKey, account, kPipePassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

std::string pipeRequest(LiveChatClient& c, uint16_t cmd, const std::string& body)
{
    REQUIRE(c.Send(cmd, body));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kPipeWaitMs));
    return resp;
}

ShareRelayInfo pipeRelayInfo(LiveChatClient& c)
{
    std::string body;
    REQUIRE(c.WaitFor(cmd_screenshare_relay_info, body, kPipeWaitMs));
    ShareRelayInfo info;
    REQUIRE(ShareProtocolClient::parseRelayInfo(body, info));
    return info;
}

//等某个条件成立，超时返回 false
template <typename Pred>
bool pipeWaitUntil(Pred pred)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kPipeEventWaitMs)
    {
        if (pred())
            return true;
        ::Sleep(kPipePollMs);
    }
    return pred();
}

struct ConnectedPred
{
    const HostEvents* m_events;
    bool operator()() const
    {
        return m_events->m_connected.load() || m_events->m_failed.load();
    }
};

struct FramesPred
{
    const ViewerEvents* m_events;
    bool operator()() const
    {
        return m_events->m_frames.load() > 0;
    }
};

struct StatePred
{
    const ViewerEvents* m_events;
    int                 m_state;
    bool operator()() const
    {
        return m_events->m_lastState.load() == m_state;
    }
};

} // namespace

TEST_CASE("SSPIPE1 推流到收流：后加入的观看者马上看到画面，尺寸裁剪正确、颜色正确，暂停与继续状态送达",
          "[.live][screenshare]")
{
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));

    LiveChatClient a;
    LiveChatClient b;
    pipeLogin(a, kPipeAccountA);
    pipeLogin(b, kPipeAccountB);
    ShareQueryResult q;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        pipeRequest(a, cmd_screenshare_query, ShareProtocolClient::buildQuery(conversation_single, kPipeUserB)), q));
    if (q.m_exists)
        pipeRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(q.m_roomId));
    a.ClearBacklog();

    ShareResult started;
    REQUIRE(ShareProtocolClient::parseResult(
        pipeRequest(a, cmd_screenshare_start,
                    ShareProtocolClient::buildStart(conversation_single, kPipeUserB, false, false, false)),
        started));
    REQUIRE(started.m_code == 0);

    HostEvents hostEvents;
    ShareHostStreamer streamer;
    ShareSourceSpec spec;
    REQUIRE(streamer.Start(pipeRelayInfo(a), serverPubKey, spec, new SolidCapturer(), &hostEvents));
    ConnectedPred connected = { &hostEvents };
    REQUIRE(pipeWaitUntil(connected));
    REQUIRE_FALSE(hostEvents.m_failed.load());

    //让发起人先独自推一会儿，B 加入时服务端会请求关键帧
    ::Sleep(500);
    ShareResult joined;
    REQUIRE(ShareProtocolClient::parseResult(
        pipeRequest(b, cmd_screenshare_join, ShareProtocolClient::buildRoomRequest(started.m_roomId)), joined));
    REQUIRE(joined.m_code == 0);

    ViewerEvents viewerEvents;
    ShareViewerReceiver receiver;
    REQUIRE(receiver.Start(pipeRelayInfo(b), serverPubKey, &viewerEvents));
    FramesPred gotFrame = { &viewerEvents };
    REQUIRE(pipeWaitUntil(gotFrame));

    std::vector<uint8_t> bgra;
    int w = 0;
    int h = 0;
    uint64_t serial = 0;
    REQUIRE(receiver.GetLatestFrame(bgra, w, h, serial));
    CHECK(w == kPipeW);
    CHECK(h == kPipeH);
    const uint8_t* center = &bgra[(static_cast<size_t>(h / 2) * w + w / 2) * kPipeBpp];
    CHECK(abs(center[2] - 255) <= kPipeColorTol);
    CHECK(center[1] <= kPipeColorTol);
    CHECK(center[0] <= kPipeColorTol);
    //最后一行也是红色：说明裁掉的是补齐的部分而不是画面本身
    const uint8_t* lastRow = &bgra[(static_cast<size_t>(h - 1) * w + w / 2) * kPipeBpp];
    CHECK(abs(lastRow[2] - 255) <= kPipeColorTol);
    CHECK(streamer.SentKeyFrames() >= 2);

    //暂停与继续：观看端收到状态
    streamer.SetPaused(true);
    StatePred pausedPred = { &viewerEvents, screenshare_source_paused };
    CHECK(pipeWaitUntil(pausedPred));
    streamer.SetPaused(false);
    StatePred sharingPred = { &viewerEvents, screenshare_source_sharing };
    CHECK(pipeWaitUntil(sharingPred));

    receiver.Stop();
    streamer.Stop();
    pipeRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(started.m_roomId));
}
