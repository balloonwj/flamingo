/**
 *  屏幕共享房间中转客户端的端到端联调用例（Suite SSRC，标签 [.live][screenshare]），ShareRelayClientLiveTests.cpp
 *
 *  用产品代码 FlamingoMedia/Share 下的 ShareProtocolClient、ShareFraming、ShareRelayClient 走一遍真实流程
 *  （docs/屏幕共享与录制方案.md 第 11.4 节 2c-2）：A 经 chatserver 发起单聊共享、拿到 1726 中转参数后连上
 *  imgserver；B 加入后同样连上。A 发一个约 200 KB、需要分片的关键帧，B 重组后逐字节相同；B 发一帧麦克风声音，
 *  A 原样收到；A 再发帧之前 B 加入时 A 收到关键帧请求。chatserver 信令一侧用 LiveChatClient 收发。
 *
 *  默认不参与常规测试。跑之前先起 tmp/calltest 的测试服务端，然后：FlamingoUnit.exe "[screenshare]"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "LiveChatClient.h"
#include "../net/FrameCrypto.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareRelayClient.h"

using namespace livetest;
using namespace screenshare;

namespace {

//tmp/calltest 测试服务端的 chatserver 端口
const unsigned short kSsrcChatPort = 26000;

//两个测试账号（互为单聊对象）与密码
const char* kSsrcAccountA = "MY0127";
const char* kSsrcAccountB = "MY0308";
const char* kSsrcPassword = "perf1234";
const int64_t kSsrcUserB = 5308;

//等应答、推送、中转帧的最长时间，单位毫秒；连接中转的超时，单位秒
const int kSsrcWaitMs = 5000;
const int kSsrcConnectTimeoutSec = 5;

//关键帧的大小：约 200 KB，按默认 64 KiB 分片会拆成 4 片
const size_t kSsrcKeyFrameBytes = 200 * 1024;

//把接收线程交来的包体重组成媒体帧，供测试线程按超时等待
class FrameCollector : public ShareRelayClientHandler
{
public:
    FrameCollector()
        : m_closed(false)
    {
    }

    virtual void OnRelayBody(const char* body, size_t len)
    {
        ShareMediaFrame frame;
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_reassembler.push(body, len, frame))
        {
            m_frames.push_back(frame);
            m_cv.notify_all();
        }
    }

    virtual void OnRelayClosed()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closed = true;
        m_cv.notify_all();
    }

    //等一帧，超时返回 false
    bool WaitFrame(ShareMediaFrame& out, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (m_frames.empty())
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout && m_frames.empty())
                return false;
        }
        out = m_frames.front();
        m_frames.pop_front();
        return true;
    }

private:
    std::mutex                  m_mutex;         //保护以下成员
    std::condition_variable     m_cv;            //有新帧或连接断开时通知
    ShareFrameReassembler       m_reassembler;   //分片重组
    std::deque<ShareMediaFrame> m_frames;        //已重组好的帧
    bool                        m_closed;        //连接是否已异常断开
};

void ssrcLogin(LiveChatClient& c, const char* account)
{
    std::string reason;
    const bool ok = c.ConnectAndLogin(kSsrcChatPort, kTestServerPubKey, account, kSsrcPassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

//发一条信令并等它的应答
std::string ssrcRequest(LiveChatClient& c, uint16_t cmd, const std::string& body)
{
    REQUIRE(c.Send(cmd, body));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kSsrcWaitMs));
    return resp;
}

//等本人的中转参数推送
ShareRelayInfo ssrcWaitRelayInfo(LiveChatClient& c)
{
    std::string body;
    REQUIRE(c.WaitFor(cmd_screenshare_relay_info, body, kSsrcWaitMs));
    ShareRelayInfo info;
    REQUIRE(ShareProtocolClient::parseRelayInfo(body, info));
    return info;
}

//若 A、B 之间残留着上次的共享，先结束掉
void ssrcEnsureNoShare(LiveChatClient& a)
{
    ShareQueryResult q;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        ssrcRequest(a, cmd_screenshare_query, ShareProtocolClient::buildQuery(conversation_single, kSsrcUserB)), q));
    if (q.m_exists)
        ssrcRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(q.m_roomId));
    //上一条用例中途失败时留下的房间：宽限期内重新登录会收到旧房间的中转参数，清掉免得被本用例误取
    a.ClearBacklog();
}

std::string ssrcData(size_t len, int salt)
{
    std::string s(len, '\0');
    for (size_t i = 0; i < len; ++i)
        s[i] = static_cast<char>((i * 13 + salt) & 0xFF);
    return s;
}

} // namespace

TEST_CASE("SSRC1 经 chatserver 拿到中转参数后连上房间中转，分片关键帧与声音帧双向送达", "[.live][screenshare]")
{
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));

    LiveChatClient a;
    LiveChatClient b;
    ssrcLogin(a, kSsrcAccountA);
    ssrcLogin(b, kSsrcAccountB);
    ssrcEnsureNoShare(a);

    ShareResult started;
    REQUIRE(ShareProtocolClient::parseResult(
        ssrcRequest(a, cmd_screenshare_start,
                    ShareProtocolClient::buildStart(conversation_single, kSsrcUserB, false, false, false)),
        started));
    REQUIRE(started.m_code == 0);
    const ShareRelayInfo infoA = ssrcWaitRelayInfo(a);
    CHECK(infoA.m_seat == 0);

    FrameCollector collectorA;
    ShareRelayClient relayA;
    REQUIRE(relayA.Connect(infoA.m_relayIp, infoA.m_relayPort, infoA.m_token, serverPubKey, &collectorA,
                           kSsrcConnectTimeoutSec));
    CHECK(relayA.IsConnected());
    CHECK(relayA.LastHelloCode() == 0);

    //A 先发一帧小的屏幕画面，房间里于是有了一路已知视频；B 加入后 A 应收到关键帧请求
    std::vector<std::string> bodies;
    ShareFraming::fragmentFrame(screenshare_track_screen, infoA.m_seat, true, 1, "warmup", 6, 0, bodies);
    REQUIRE(relayA.SendBodies(bodies));

    ShareResult joined;
    REQUIRE(ShareProtocolClient::parseResult(
        ssrcRequest(b, cmd_screenshare_join, ShareProtocolClient::buildRoomRequest(started.m_roomId)), joined));
    REQUIRE(joined.m_code == 0);
    const ShareRelayInfo infoB = ssrcWaitRelayInfo(b);
    CHECK(infoB.m_seat == 1);
    FrameCollector collectorB;
    ShareRelayClient relayB;
    REQUIRE(relayB.Connect(infoB.m_relayIp, infoB.m_relayPort, infoB.m_token, serverPubKey, &collectorB,
                           kSsrcConnectTimeoutSec));

    ShareMediaFrame frame;
    REQUIRE(collectorA.WaitFrame(frame, kSsrcWaitMs));
    int requestedTrack = 0;
    CHECK(ShareFraming::parseKeyFrameRequest(frame, requestedTrack));
    CHECK(requestedTrack == screenshare_track_screen);

    //A 发一个约 200 KB 的关键帧：拆成多片，B 重组后逐字节相同
    const std::string keyFrame = ssrcData(kSsrcKeyFrameBytes, 5);
    ShareFraming::fragmentFrame(screenshare_track_screen, infoA.m_seat, true, 1000, keyFrame.data(), keyFrame.size(),
                                0, bodies);
    REQUIRE(bodies.size() > 1);
    REQUIRE(relayA.SendBodies(bodies));
    REQUIRE(collectorB.WaitFrame(frame, kSsrcWaitMs));
    CHECK(frame.m_track == screenshare_track_screen);
    CHECK(frame.m_senderSeat == 0);
    CHECK(frame.m_keyframe);
    CHECK(frame.m_timestamp == 1000);
    CHECK(frame.m_data == keyFrame);

    //B 发一帧麦克风声音：A 原样收到，发送者座位为 1
    const std::string voice = ssrcData(160, 9);
    ShareFraming::fragmentFrame(screenshare_track_mic, infoB.m_seat, false, 2000, voice.data(), voice.size(), 0,
                                bodies);
    REQUIRE(relayB.SendBodies(bodies));
    REQUIRE(collectorA.WaitFrame(frame, kSsrcWaitMs));
    CHECK(frame.m_track == screenshare_track_mic);
    CHECK(frame.m_senderSeat == 1);
    CHECK(frame.m_data == voice);

    relayB.Close();
    relayA.Close();
    CHECK_FALSE(relayA.IsConnected());
    ssrcRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(started.m_roomId));
}

TEST_CASE("SSRC2 令牌不对时报到失败并给出服务端的错误码", "[.live][screenshare]")
{
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));
    //tmp/calltest 的中转端口
    const int kRelayPort = 26003;

    FrameCollector collector;
    ShareRelayClient relay;
    CHECK_FALSE(relay.Connect("127.0.0.1", kRelayPort, "not-a-valid-token", serverPubKey, &collector,
                              kSsrcConnectTimeoutSec));
    CHECK_FALSE(relay.IsConnected());
    CHECK(relay.LastHelloCode() == error_code_session_expired);
}
