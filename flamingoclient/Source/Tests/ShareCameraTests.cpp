/**
 *  屏幕共享房间摄像头的单元测试（Suite SSCAM，标签 [sharecamera]），ShareCameraTests.cpp
 *
 *  覆盖 FlamingoMedia/Share 的 ShareCameraSender / ShareCameraReceiver（docs/屏幕共享与录制方案.md 第 5 期 5a / 5b），
 *  不打开真实摄像头（无摄像头模式喂合成画面）：
 *    - SSCAM1 发送端：1280×720 的画面缩成 640×360 编码，按摄像头轨、本人座位发出，载荷带宽高；第一帧是关键帧，
 *             收到关键帧请求后下一帧是关键帧；本人预览是 640×360；
 *    - SSCAM2 接收端：把发送端发出的帧当成座位 3 的摄像头喂进去，解出 640×360 的画面、颜色对得上；删除座位后取不到；
 *    - SSCAM3 路数上限：10 个座位同时开摄像头，只解码 9 个，第 10 个记为超出上限；删掉一个后它从下一个关键帧开始解码。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareCameraSender.h"
#include "../../FlamingoMedia/Share/ShareCameraReceiver.h"
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"
#include "../net/Protocol.h"

#pragma comment(lib, "mfplat.lib")

using namespace screenshare;

namespace {

//合成画面的尺寸与颜色（NV12：Y、U、V）
const int kCamSrcW = 1280;
const int kCamSrcH = 720;
const uint8_t kCamY = 120;
const uint8_t kCamU = 90;
const uint8_t kCamV = 200;
//等解码线程的最长时间与轮询间隔（毫秒）
const ULONGLONG kCamWaitMs = 5000;
const DWORD kCamPollMs = 10;

//初始化本线程的 COM 与 Media Foundation（编码器在调用 FeedFrame 的线程上创建）
class CamMfScope
{
public:
    CamMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~CamMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//收集发出的包体
class CamTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_bodies.push_back(body);
        return true;
    }

    std::mutex               m_mutex;    //保护 m_bodies
    std::vector<std::string> m_bodies;   //发出的包体
};

std::vector<uint8_t> camFrame(int w, int h, uint8_t y, uint8_t u, uint8_t v)
{
    std::vector<uint8_t> f(static_cast<size_t>(w) * h * 3 / 2, y);
    for (size_t i = static_cast<size_t>(w) * h; i + 1 < f.size(); i += 2)
    {
        f[i] = u;
        f[i + 1] = v;
    }
    return f;
}

//把包体重组成帧
std::vector<ShareMediaFrame> camFrames(const std::vector<std::string>& bodies)
{
    std::vector<ShareMediaFrame> frames;
    ShareFrameReassembler reassembler;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        ShareMediaFrame f;
        if (reassembler.push(bodies[i].data(), bodies[i].size(), f))
            frames.push_back(f);
    }
    return frames;
}

//让发送端编出 count 帧，返回重组后的帧（座位改写为 seat，模拟服务端改写）
std::vector<ShareMediaFrame> camEncode(int count, int seat, uint8_t y)
{
    CamTransport transport;
    ShareCameraSender sender;
    if (!sender.StartWithoutCamera(&transport, seat))
        return std::vector<ShareMediaFrame>();
    const std::vector<uint8_t> f = camFrame(kCamSrcW, kCamSrcH, y, kCamU, kCamV);
    for (int i = 0; i < count; ++i)
        sender.FeedFrame(&f[0], kCamSrcW, kCamSrcH, kCamSrcW);
    sender.Stop();
    return camFrames(transport.m_bodies);
}

bool camWait(const ShareCameraReceiver& r, int seat, std::vector<uint8_t>& bgra, int& w, int& h)
{
    const ULONGLONG deadline = ::GetTickCount64() + kCamWaitMs;
    uint64_t serial = 0;
    while (::GetTickCount64() < deadline)
    {
        if (r.GetFrame(seat, bgra, w, h, serial))
            return true;
        ::Sleep(kCamPollMs);
    }
    return false;
}

} // namespace

TEST_CASE("SSCAM1 摄像头发送端缩放、编码、按摄像头轨发出", "[sharecamera]")
{
    CamMfScope mf;
    CamTransport transport;
    ShareCameraSender sender;
    REQUIRE(sender.StartWithoutCamera(&transport, 4));
    CHECK(sender.IsRunning());
    const std::vector<uint8_t> f = camFrame(kCamSrcW, kCamSrcH, kCamY, kCamU, kCamV);
    sender.FeedFrame(&f[0], kCamSrcW, kCamSrcH, kCamSrcW);
    sender.FeedFrame(&f[0], kCamSrcW, kCamSrcH, kCamSrcW);
    sender.RequestKeyFrame();
    sender.FeedFrame(&f[0], kCamSrcW, kCamSrcH, kCamSrcW);
    const std::vector<ShareMediaFrame> frames = camFrames(transport.m_bodies);
    REQUIRE(frames.size() == 3);
    for (size_t i = 0; i < frames.size(); ++i)
    {
        INFO("frame " << i);
        CHECK(frames[i].m_track == screenshare_track_camera);
        CHECK(frames[i].m_senderSeat == 4);
        int w = 0;
        int h = 0;
        const char* h264 = NULL;
        size_t len = 0;
        REQUIRE(ShareFraming::parseVideoPayload(frames[i].m_data, w, h, h264, len));
        CHECK(w == kShareCameraMaxW);
        CHECK(h == kShareCameraMaxH);
        CHECK(len > 0);
    }
    CHECK(frames[0].m_keyframe);
    CHECK_FALSE(frames[1].m_keyframe);
    CHECK(frames[2].m_keyframe);

    std::vector<uint8_t> preview;
    int pw = 0;
    int ph = 0;
    uint64_t serial = 0;
    REQUIRE(sender.GetPreview(preview, pw, ph, serial));
    CHECK(pw == kShareCameraMaxW);
    CHECK(ph == kShareCameraMaxH);
    CHECK(serial == 3);
    sender.Stop();
    CHECK_FALSE(sender.IsRunning());
}

TEST_CASE("SSCAM2 接收端按座位解码", "[sharecamera]")
{
    std::vector<ShareMediaFrame> frames;
    {
        CamMfScope mf;
        frames = camEncode(5, 3, kCamY);
    }
    REQUIRE(frames.size() == 5);
    ShareCameraReceiver receiver;
    receiver.Start();
    for (size_t i = 0; i < frames.size(); ++i)
        receiver.OnRemoteFrame(frames[i]);
    std::vector<uint8_t> bgra;
    int w = 0;
    int h = 0;
    REQUIRE(camWait(receiver, 3, bgra, w, h));
    CHECK(w == kShareCameraMaxW);
    CHECK(h == kShareCameraMaxH);
    REQUIRE(bgra.size() == static_cast<size_t>(w) * h * 4);
    //画面中心的颜色：Y=120、U=90、V=200 按 BT.601 limited range 约为 B≈44、G≈77、R≈236（容差 25，编码有损）
    const size_t center = (static_cast<size_t>(h / 2) * w + w / 2) * 4;
    CHECK(abs(static_cast<int>(bgra[center + 0]) - 44) < 25);
    CHECK(abs(static_cast<int>(bgra[center + 1]) - 77) < 25);
    CHECK(abs(static_cast<int>(bgra[center + 2]) - 236) < 25);
    const std::vector<int> seats = receiver.Seats();
    REQUIRE(seats.size() == 1);
    CHECK(seats[0] == 3);

    receiver.RemoveSeat(3);
    uint64_t serial = 0;
    const ULONGLONG deadline = ::GetTickCount64() + kCamWaitMs;
    while (::GetTickCount64() < deadline && receiver.GetFrame(3, bgra, w, h, serial))
        ::Sleep(kCamPollMs);
    CHECK_FALSE(receiver.GetFrame(3, bgra, w, h, serial));
    CHECK(receiver.Seats().empty());
    receiver.Stop();
}

TEST_CASE("SSCAM3 同屏最多解码 9 路", "[sharecamera]")
{
    std::vector<ShareMediaFrame> base;
    {
        CamMfScope mf;
        base = camEncode(2, 0, kCamY);
    }
    REQUIRE(base.size() == 2);
    ShareCameraReceiver receiver;
    receiver.Start();
    const int kSeats = kShareMaxCameraTiles + 1;
    for (int seat = 1; seat <= kSeats; ++seat)
    {
        for (size_t i = 0; i < base.size(); ++i)
        {
            ShareMediaFrame f = base[i];
            f.m_senderSeat = seat;
            receiver.OnRemoteFrame(f);
        }
    }
    std::vector<uint8_t> bgra;
    int w = 0;
    int h = 0;
    REQUIRE(camWait(receiver, kShareMaxCameraTiles, bgra, w, h));
    const ULONGLONG deadline = ::GetTickCount64() + kCamWaitMs;
    while (::GetTickCount64() < deadline && receiver.PendingFrames() > 0)
        ::Sleep(kCamPollMs);
    CHECK(receiver.Seats().size() == static_cast<size_t>(kShareMaxCameraTiles));
    const std::vector<int> overflow = receiver.OverflowSeats();
    REQUIRE(overflow.size() == 1);
    CHECK(overflow[0] == kSeats);
    uint64_t serial = 0;
    CHECK_FALSE(receiver.GetFrame(kSeats, bgra, w, h, serial));

    //腾出一路：第 10 个座位从下一个关键帧开始解码
    receiver.RemoveSeat(1);
    for (size_t i = 0; i < base.size(); ++i)
    {
        ShareMediaFrame f = base[i];
        f.m_senderSeat = kSeats;
        receiver.OnRemoteFrame(f);
    }
    REQUIRE(camWait(receiver, kSeats, bgra, w, h));
    CHECK(receiver.OverflowSeats().empty());
    receiver.Stop();
}
