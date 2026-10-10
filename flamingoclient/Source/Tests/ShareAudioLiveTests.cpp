/**
 *  屏幕共享房间语音的联调用例（Suite SSAULIVE，标签 [.live][screenshare]），ShareAudioLiveTests.cpp
 *
 *  三名成员各用一条真实的房间中转连接（ShareRelayClient，令牌在测试里自己签）和一个无设备模式的语音会话
 *  （ShareAudioSession），经 tmp/calltest 的 imgserver 互相收发声音帧，不打开麦克风与扬声器、不出声：
 *    - SSAULIVE1 发起人与一名观看者同时说话（1 kHz、2.5 kHz），第三名成员听到两个声音混在一起；发起人关麦后只剩
 *               观看者的声音；第三名成员判定在说话的座位是观看者；发起人那边也听得到观看者。
 *  默认不运行：跑之前先起 tmp/calltest 的测试服务端，然后 FlamingoUnit.exe "SSAULIVE*"。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "RoomRelayTestKit.h"
#include "../net/FrameCrypto.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/Share/ShareAudioSession.h"
#include "../../FlamingoMedia/Share/ShareRelayClient.h"
#include "../../../flamingoserver/utils/RelayToken.h"

using namespace screenshare;

namespace {

const int kSaRate = 48000;
const int kSaFrame = 960;
const double kSaPi = 3.14159265358979323846;
const double kSaAmp = 8000.0;
//每次喂入的帧数、等待中转送达的最长时间（毫秒）与轮询间隔（毫秒）
const int kSaFrames = 25;
const int kSaWaitMs = 5000;
const DWORD kSaPollMs = 20;
//连接中转的超时（秒）
const int kSaConnectTimeoutSec = 5;
//两个测试频率与一个对照频率（Hz）
const double kSaToneA = 1000.0;
const double kSaToneB = 2500.0;
const double kSaNoiseHz = 4000.0;

std::vector<int16_t> saTone(int samples, double hz, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(kSaAmp * sin(2.0 * kSaPi * hz * (i + offset) / kSaRate));
    return v;
}

double saGoertzel(const int16_t* p, int n, double hz)
{
    const double k = 2.0 * cos(2.0 * kSaPi * hz / kSaRate);
    double s1 = 0;
    double s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s0 = p[i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

//一名成员：中转连接 + 无设备模式的语音会话
class SaMember : public ShareRelayClientHandler, public ShareAudioTransport
{
public:
    SaMember()
        : m_seat(0)
    {
    }

    /**
     *  连上房间中转并启动语音会话。
     *  @param reason [出参] 失败原因
     */
    bool Join(const std::string& room, int role, int seat, const unsigned char* pubKey, bool micOn,
              std::string& reason)
    {
        m_seat = seat;
        const std::string token = roomrelaytest::makeToken(room, role, seat, roomrelaytest::kRoomTokenKey);
        if (!m_relay.Connect("127.0.0.1", roomrelaytest::kRoomRelayPort, token, pubKey, this, kSaConnectTimeoutSec))
        {
            reason = "relay connect failed, hello code " + std::to_string(m_relay.LastHelloCode());
            return false;
        }
        if (!m_audio.Start(this, seat, micOn, true))
        {
            reason = "audio session start failed";
            return false;
        }
        return true;
    }

    void Leave()
    {
        m_relay.Close();
        m_audio.Stop();
    }

    //喂 frames 帧单音
    void Speak(double hz, int frames)
    {
        for (int f = 0; f < frames; ++f)
        {
            const std::vector<int16_t> pcm = saTone(kSaFrame, hz, f * kSaFrame);
            m_audio.FeedMic(&pcm[0], kSaFrame);
        }
    }

    //收到过某个座位的声音帧数
    int Received(int seat)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_received[seat];
    }

    //等某个座位的声音帧数达到 count
    bool WaitReceived(int seat, int count)
    {
        const ULONGLONG deadline = ::GetTickCount64() + kSaWaitMs;
        while (::GetTickCount64() < deadline)
        {
            if (Received(seat) >= count)
                return true;
            ::Sleep(kSaPollMs);
        }
        return false;
    }

    virtual void OnRelayBody(const char* body, size_t len) override
    {
        ShareMediaFrame frame;
        if (!m_reassembler.push(body, len, frame))
            return;
        m_audio.OnRemoteFrame(frame);
        if (frame.m_track == screenshare_track_mic)
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_received[frame.m_senderSeat];
        }
    }

    virtual void OnRelayClosed() override
    {
    }

    virtual bool SendRoomBody(const std::string& body) override
    {
        return m_relay.SendBody(body);
    }

    int                   m_seat;          //本人座位
    ShareRelayClient      m_relay;         //房间中转连接
    ShareAudioSession     m_audio;         //语音会话（无设备模式）
    ShareFrameReassembler m_reassembler;   //接收线程上的分片重组
    std::mutex            m_mutex;         //保护 m_received
    std::map<int, int>    m_received;      //座位 → 收到的麦克风帧数
};

} // namespace

TEST_CASE("SSAULIVE1 三名成员经房间中转收发声音：混音、关麦、发言判定", "[.live][screenshare]")
{
    //ShareRelayClient 自己不初始化 winsock（产品里由 exe 初始化），测试进程里先初始化
    livetest::EnsureWinsockReady();
    unsigned char pubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(livetest::kTestServerPubKey, pubKey));
    const std::string room = roomrelaytest::makeRoomId();
    SaMember host;
    SaMember viewerB;
    SaMember viewerC;
    std::string reason;
    const bool hostOk = host.Join(room, RelayToken::role_screenshare_host, 0, pubKey, true, reason);
    INFO("host: " << reason);
    REQUIRE(hostOk);
    const bool bOk = viewerB.Join(room, RelayToken::role_screenshare_member, 1, pubKey, true, reason);
    INFO("viewer B: " << reason);
    REQUIRE(bOk);
    const bool cOk = viewerC.Join(room, RelayToken::role_screenshare_member, 2, pubKey, false, reason);
    INFO("viewer C: " << reason);
    REQUIRE(cOk);

    //发起人与观看者 B 同时说话
    host.Speak(kSaToneA, kSaFrames);
    viewerB.Speak(kSaToneB, kSaFrames);
    REQUIRE(viewerC.WaitReceived(0, kSaFrames));
    REQUIRE(viewerC.WaitReceived(1, kSaFrames));
    CHECK(viewerC.Received(2) == 0);
    std::vector<int16_t> out(static_cast<size_t>(kSaFrame) * kSaFrames);
    viewerC.m_audio.PullPlayback(&out[0], static_cast<int>(out.size()));
    const int half = static_cast<int>(out.size()) / 2;
    double noise = saGoertzel(&out[half], half, kSaNoiseHz);
    CHECK(saGoertzel(&out[half], half, kSaToneA) > 10.0 * noise);
    CHECK(saGoertzel(&out[half], half, kSaToneB) > 10.0 * noise);
    //发起人那边听得到观看者 B
    REQUIRE(host.WaitReceived(1, kSaFrames));
    viewerB.m_audio.PullPlayback(&out[0], static_cast<int>(out.size()));
    CHECK(saGoertzel(&out[half], half, kSaToneA) > 10.0 * saGoertzel(&out[half], half, kSaNoiseHz));

    //发起人关麦：之后只剩 B 的声音
    REQUIRE(host.m_audio.SetMicEnabled(false));
    const int beforeA = viewerC.Received(0);
    const int beforeB = viewerC.Received(1);
    host.Speak(kSaToneA, kSaFrames);
    viewerB.Speak(kSaToneB, kSaFrames);
    REQUIRE(viewerC.WaitReceived(1, beforeB + kSaFrames));
    CHECK(viewerC.Received(0) == beforeA);
    viewerC.m_audio.PullPlayback(&out[0], static_cast<int>(out.size()));
    CHECK(saGoertzel(&out[half], half, kSaToneB) > 100.0 * saGoertzel(&out[half], half, kSaToneA));
    const std::vector<int> talking = viewerC.m_audio.UpdateSpeaking(static_cast<int64_t>(::GetTickCount64()));
    REQUIRE(talking.size() == 1);
    CHECK(talking[0] == 1);

    viewerC.Leave();
    viewerB.Leave();
    host.Leave();
}
