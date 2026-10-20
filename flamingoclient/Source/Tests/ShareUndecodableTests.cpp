/**
 *  观看端「画面分辨率超出本机系统解码能力」的判定与提示（标签 [sharewin7]），ShareUndecodableTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 4.2「Win7 观看者」，Q6 取乙（2026-10-15）：屏幕共享的编码长边提到 2560 / 3840 后，
 *  Win7 自带的 H.264 解码器（据推测最大 1920×1088）解不了大尺寸的码流，原先只有错误日志、画面停住。改为在观看窗中央提示。
 *    - SSUD1 判定：画面宽或高超过 1920×1088、且连续收到 2 个关键帧仍一帧也没解出时，判为超出本机解码能力；
 *            不超过 1920×1088 的画面解不出来不算（那是别的原因），关键帧不够 2 个也不算；
 *    - SSUD2 接线：ShareScreenDecoders 收到两个标为 2560×1440 的坏关键帧（解码器解不出）后，这一路标记为超出解码能力，
 *            并通知监听者一次；随后解出一帧真实画面时标记清除，再通知一次。
 *  本机（Win10 以上）解得了 2560 的码流，所以 SSUD2 用解不出来的坏数据模拟。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareScreenDecoders.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Call/MfH264Codec.h"
#include "../net/Protocol.h"

#pragma comment(lib, "mfplat.lib")

using namespace screenshare;

namespace {

//用例里的座位号、坏数据的长度与填充值
const int kUdSeat = 3;
const size_t kUdGarbageBytes = 4096;
const char kUdGarbageByte = 0x5A;
//真实画面的尺寸、帧率、码率、帧数
const int kUdW = 640;
const int kUdH = 360;
const int kUdFps = 15;
const int kUdBps = 1000000;
const int kUdFrames = 5;
//每帧时长（100 纳秒）
const int64_t kUdFrameHns = 10000000LL / kUdFps;
//NV12 的中灰亮度与色度
const uint8_t kUdGray = 128;

//初始化本线程的 COM 与 Media Foundation
class UdMfScope
{
public:
    UdMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~UdMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//记录监听者收到的回调
class UdListener : public ShareScreenDecodersListener
{
public:
    UdListener()
        : m_decoded(0)
        , m_undecodableOn(0)
        , m_undecodableOff(0)
    {
    }

    virtual void OnScreenDecoded(int seat) override
    {
        (void)seat;
        ++m_decoded;
    }

    virtual void OnScreenUndecodable(int seat, bool undecodable) override
    {
        (void)seat;
        if (undecodable)
            ++m_undecodableOn;
        else
            ++m_undecodableOff;
    }

    int m_decoded;          //解出的帧数
    int m_undecodableOn;    //收到「超出解码能力」的次数
    int m_undecodableOff;   //收到「恢复」的次数
};

//收编码输出
class UdEncodeSink : public call::MfH264EncoderSink
{
public:
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)ptsHns;
        m_units.push_back(std::string(reinterpret_cast<const char*>(data), len));
        m_keys.push_back(keyframe);
    }

    std::vector<std::string> m_units;   //访问单元
    std::vector<bool>        m_keys;    //是否关键帧
};

//组一帧屏幕画面（视频载荷：编码宽高 + H.264）
ShareMediaFrame udFrame(int w, int h, const std::string& h264, bool keyframe, uint64_t ts)
{
    ShareMediaFrame f;
    f.m_track = screenshare_track_screen;
    f.m_senderSeat = kUdSeat;
    f.m_keyframe = keyframe;
    f.m_timestamp = ts;
    f.m_data = ShareFraming::buildVideoPayload(w, h, h264.data(), h264.size());
    return f;
}

} // namespace

TEST_CASE("SSUD1 超出本机解码能力的判定：大于 1920×1088 且连续 2 个关键帧没解出", "[sharewin7]")
{
    CHECK(ShareLooksUndecodable(2560, 1440, 2));
    CHECK(ShareLooksUndecodable(3840, 2160, 3));
    //只超出一边也算（竖屏、1920×1200 的显示器）
    CHECK(ShareLooksUndecodable(1080, 1920, 2));
    CHECK(ShareLooksUndecodable(1920, 1200, 2));
    //关键帧不够：可能只是刚开始，还没到能解出来的时候
    CHECK_FALSE(ShareLooksUndecodable(2560, 1440, 1));
    CHECK_FALSE(ShareLooksUndecodable(2560, 1440, 0));
    //不超过 1920×1088 的画面解不出来是别的原因，不提示分辨率
    CHECK_FALSE(ShareLooksUndecodable(1920, 1088, 5));
    CHECK_FALSE(ShareLooksUndecodable(1920, 1080, 5));
    CHECK_FALSE(ShareLooksUndecodable(0, 0, 5));
}

TEST_CASE("SSUD2 多路解码：大尺寸的关键帧解不出时标记并通知，解出画面后清除", "[sharewin7]")
{
    UdMfScope mf;
    UdListener listener;
    ShareScreenDecoders screens;
    screens.SetListener(&listener);
    const std::string garbage(kUdGarbageBytes, kUdGarbageByte);
    //第一个坏关键帧：还不判定
    screens.OnScreenFrame(udFrame(2560, 1440, garbage, true, 0));
    CHECK_FALSE(screens.IsUndecodable(kUdSeat));
    //增量帧不计数
    screens.OnScreenFrame(udFrame(2560, 1440, garbage, false, 1));
    CHECK_FALSE(screens.IsUndecodable(kUdSeat));
    //第二个坏关键帧：判为超出解码能力，通知一次
    screens.OnScreenFrame(udFrame(2560, 1440, garbage, true, 2));
    CHECK(screens.IsUndecodable(kUdSeat));
    CHECK(screens.AnyUndecodable());
    CHECK(listener.m_undecodableOn == 1);
    //再来一个也不重复通知
    screens.OnScreenFrame(udFrame(2560, 1440, garbage, true, 3));
    CHECK(listener.m_undecodableOn == 1);

    //换成一段真实的小尺寸码流：解出画面后标记清除，通知一次
    call::MfH264Encoder enc;
    REQUIRE(enc.Init(kUdW, kUdH, kUdFps, kUdBps));
    UdEncodeSink sink;
    std::vector<uint8_t> nv12(static_cast<size_t>(kUdW) * kUdH * 3 / 2, kUdGray);
    for (int i = 0; i < kUdFrames; ++i)
    {
        if (i == 0)
            enc.ForceKeyFrame();
        nv12[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        enc.EncodeNv12(&nv12[0], kUdW, static_cast<int64_t>(i) * kUdFrameHns, &sink);
    }
    REQUIRE_FALSE(sink.m_units.empty());
    for (size_t i = 0; i < sink.m_units.size(); ++i)
        screens.OnScreenFrame(udFrame(kUdW, kUdH, sink.m_units[i], sink.m_keys[i], 10 + i));
    CAPTURE(listener.m_decoded);
    REQUIRE(listener.m_decoded > 0);
    CHECK_FALSE(screens.IsUndecodable(kUdSeat));
    CHECK_FALSE(screens.AnyUndecodable());
    CHECK(listener.m_undecodableOff == 1);
}
