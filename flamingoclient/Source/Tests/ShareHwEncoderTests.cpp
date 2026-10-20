/**
 *  屏幕共享硬件 H.264 编码器的单元测试（Suite SSHW，标签 [sharehw]），ShareHwEncoderTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareHwH264Encoder 与 ShareVideoEncoder 的硬件 / 软件选择（docs/屏幕共享与录制方案.md 第 9 期 9c）。
 *  机器上没有硬件编码器时，SSHW1~3 只打印一条提示后结束，不算失败；SSHW4 照常检查。
 *    - SSHW1 硬件编码器编 30 帧会动的纹理：每帧都有输出，第一帧是关键帧；用系统解码器解回来（解码尺寸可能按 16 对齐，
 *            如 360 解成 368，只比较左上角原尺寸的部分），亮度与原图的平均差很小；
 *    - SSHW2 运行中要求关键帧，下一帧输出就是关键帧；
 *    - SSHW3 运行中把码率从 2 Mbps 改到 200 kbps，同样的画面编出来的数据量明显变小，改码率后的第一包是关键帧
 *            （Quick Sync 运行中改码率不生效，改为按新码率重建编码器）；
 *    - SSHW4 ShareVideoEncoder 有硬件编码器时用硬件、禁用硬件时用软件，两种情况都能编出关键帧；
 *    - SSHW5 ShareVideoEncoder 换源尺寸（1080p → 2K）时报告码率范围变化、当前码率按比例换算、重建后这一帧是关键帧；
 *            编码器不接受大尺寸时逐档退到 1920，同一次推流里记住（docs/远程桌面H264与共享画质提升方案.md 4.2，2026-10-15）；
 *    - SHSQ1 共享中改清晰度（2K → 4K）：下一帧按新上限重建编码器、编成关键帧；参数没变时不重建。
 *    - RDPERF1（隐藏，[.rdperf]，2026-10-17 远程桌面 H.264 联调时补）：按远程桌面参数连续编码同一张 1080p 静止画面，
 *            硬件、软件编码器各一次，记下每帧字节数与各类 NAL 的字节数（实测，不断言）。联调发现画面完全静止时仍有约 2 Mbps，
 *            用它判断静止帧是否被 CBR 的填充数据（NAL 类型 12）补到了每帧预算。
 *  SSHW5、SHSQ1 只用软件编码器，结果与本机有没有硬件编码器无关。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <math.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareHwH264Encoder.h"
#include "../../FlamingoMedia/Share/ShareVideoEncoder.h"
#include "../../FlamingoMedia/Share/ShareCapturer.h"
#include "../../FlamingoMedia/Call/MfH264Codec.h"

#pragma comment(lib, "mfplat.lib")

using namespace screenshare;

namespace {

//测试画面的尺寸、帧率、帧数、关键帧间隔与码率
const int kHwW = 640;
const int kHwH = 360;
const int kHwFps = 15;
const int kHwFrames = 30;
const int kHwGop = 1000;
const int kHwHighBps = 2000000;
const int kHwLowBps = 200000;
//每帧时长（100 纳秒）
const int64_t kHwFrameHns = 10000000LL / kHwFps;
//解码后亮度与原图的平均绝对差上限（8 位亮度）
const double kHwMaxMeanDiff = 12.0;

//初始化本线程的 COM 与 Media Foundation
class HwMfScope
{
public:
    HwMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~HwMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//收集编码输出：每个包的数据与是否关键帧
class HwSink : public call::MfH264EncoderSink
{
public:
    HwSink()
        : m_bytes(0)
    {
    }

    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)ptsHns;
        m_packets.push_back(std::string(reinterpret_cast<const char*>(data), len));
        m_keys.push_back(keyframe);
        m_bytes += len;
    }

    std::vector<std::string> m_packets;   //每个输出包
    std::vector<bool>        m_keys;      //对应的包是否关键帧
    size_t                   m_bytes;     //累计字节数
};

//收集解码输出：最后一帧的亮度平面与解出的帧数
class HwDecodeSink : public call::MfH264DecoderSink
{
public:
    HwDecodeSink()
        : m_frames(0)
        , m_width(0)
        , m_height(0)
    {
    }

    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override
    {
        (void)ptsHns;
        ++m_frames;
        m_width = width;
        m_height = height;
        m_lastY.resize(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            memcpy(&m_lastY[static_cast<size_t>(y) * width], nv12 + static_cast<size_t>(y) * stride, width);
    }

    int                  m_frames;   //解出的帧数
    int                  m_width;    //解码宽度（可能按 16 对齐，大于编码宽度）
    int                  m_height;   //解码高度（同上）
    std::vector<uint8_t> m_lastY;    //最后一帧的亮度平面（行跨度 = m_width）
};

//画一帧会动的纹理（与 SSBR5 相同的做法，但不加噪声，便于比较解码结果）：横竖两个方向的正弦条纹，随帧号平移
void hwTextureFrame(std::vector<uint8_t>& nv12, int w, int h, int frame)
{
    //条纹的中心亮度、幅度、横竖两个方向的空间周期（像素）与每帧平移的相位
    const double kMid = 128.0;
    const double kAmp = 60.0;
    const double kPeriodX = 23.0;
    const double kPeriodY = 17.0;
    const double kPhasePerFrame = 0.3;
    const uint8_t kGrayUv = 128;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const double v = kMid + kAmp * sin(x / kPeriodX + frame * kPhasePerFrame) * cos(y / kPeriodY);
            nv12[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(static_cast<int>(v));
        }
    }
    for (size_t k = static_cast<size_t>(w) * h; k < nv12.size(); ++k)
        nv12[k] = kGrayUv;
}

//同一帧纹理的 BGRA 版本（灰度），给 ShareVideoEncoder 用
std::vector<uint8_t> hwTextureBgra(int w, int h, int frame)
{
    std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2);
    hwTextureFrame(nv12, w, h, frame);
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        bgra[i * 4] = nv12[i];
        bgra[i * 4 + 1] = nv12[i];
        bgra[i * 4 + 2] = nv12[i];
        bgra[i * 4 + 3] = 255;
    }
    return bgra;
}

//收集 ShareVideoEncoder 的输出
class HwShareSink : public ShareVideoEncoderSink
{
public:
    HwShareSink()
        : m_packets(0)
        , m_keyframes(0)
    {
    }

    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) override
    {
        (void)data;
        (void)len;
        (void)timestampUs;
        ++m_packets;
        if (keyframe)
            ++m_keyframes;
    }

    int m_packets;     //输出包数
    int m_keyframes;   //其中的关键帧数
};

//没有硬件编码器时打印提示；返回是否有
bool hwAvailable()
{
    if (ShareHwH264Encoder::CountHardwareEncoders() > 0)
        return true;
    WARN("no hardware H.264 encoder on this machine, hardware cases skipped");
    return false;
}

} // namespace

TEST_CASE("SSHW1 硬件编码器逐帧输出，首帧为关键帧，解码后与原图接近", "[sharehw]")
{
    HwMfScope mf;
    if (!hwAvailable())
        return;
    ShareHwH264Encoder hw;
    REQUIRE(hw.Init(kHwW, kHwH, kHwFps, kHwHighBps, kHwGop));
    CHECK(hw.IsReady());
    CHECK_FALSE(hw.Name().empty());
    std::vector<uint8_t> nv12(static_cast<size_t>(kHwW) * kHwH * 3 / 2);
    HwSink sink;
    int64_t pts = 0;
    for (int i = 0; i < kHwFrames; ++i)
    {
        hwTextureFrame(nv12, kHwW, kHwH, i);
        REQUIRE(hw.EncodeNv12(&nv12[0], kHwW, pts, &sink));
        pts += kHwFrameHns;
    }
    //低延迟模式下每喂一帧就出一帧（允许最后一两帧还在编码器里）
    const int kMaxPending = 2;
    CAPTURE(sink.m_packets.size());
    CHECK(static_cast<int>(sink.m_packets.size()) >= kHwFrames - kMaxPending);
    REQUIRE_FALSE(sink.m_keys.empty());
    CHECK(sink.m_keys[0]);

    //用系统解码器解回来，最后一帧与对应的原图比较
    call::MfH264Decoder decoder;
    REQUIRE(decoder.Init(kHwW, kHwH));
    HwDecodeSink decoded;
    for (size_t i = 0; i < sink.m_packets.size(); ++i)
    {
        decoder.DecodePacket(reinterpret_cast<const uint8_t*>(sink.m_packets[i].data()), sink.m_packets[i].size(),
                             static_cast<int64_t>(i) * kHwFrameHns, &decoded);
    }
    REQUIRE(decoded.m_frames > 0);
    REQUIRE(decoded.m_width >= kHwW);
    REQUIRE(decoded.m_height >= kHwH);
    hwTextureFrame(nv12, kHwW, kHwH, decoded.m_frames - 1);
    double diff = 0.0;
    for (int y = 0; y < kHwH; ++y)
    {
        for (int x = 0; x < kHwW; ++x)
        {
            diff += fabs(static_cast<double>(decoded.m_lastY[static_cast<size_t>(y) * decoded.m_width + x]) -
                         nv12[static_cast<size_t>(y) * kHwW + x]);
        }
    }
    diff /= static_cast<double>(kHwW) * kHwH;
    CAPTURE(decoded.m_frames, decoded.m_width, decoded.m_height, diff);
    CHECK(diff < kHwMaxMeanDiff);
}

TEST_CASE("SSHW2 运行中要求关键帧，下一帧输出就是关键帧", "[sharehw]")
{
    HwMfScope mf;
    if (!hwAvailable())
        return;
    ShareHwH264Encoder hw;
    REQUIRE(hw.Init(kHwW, kHwH, kHwFps, kHwHighBps, kHwGop));
    std::vector<uint8_t> nv12(static_cast<size_t>(kHwW) * kHwH * 3 / 2);
    HwSink before;
    int64_t pts = 0;
    for (int i = 0; i < kHwFrames / 2; ++i)
    {
        hwTextureFrame(nv12, kHwW, kHwH, i);
        REQUIRE(hw.EncodeNv12(&nv12[0], kHwW, pts, &before));
        pts += kHwFrameHns;
    }
    //开头之后没有别的关键帧（GOP 很长）
    for (size_t i = 1; i < before.m_keys.size(); ++i)
        CHECK_FALSE(before.m_keys[i]);
    REQUIRE(hw.ForceKeyFrame());
    HwSink after;
    for (int i = 0; i < 3; ++i)
    {
        hwTextureFrame(nv12, kHwW, kHwH, kHwFrames / 2 + i);
        REQUIRE(hw.EncodeNv12(&nv12[0], kHwW, pts, &after));
        pts += kHwFrameHns;
    }
    //编码器里可能还压着要求之前的一帧：关键帧最迟出现在第二个输出
    bool sawKey = false;
    for (size_t i = 0; i < after.m_keys.size() && i < 2; ++i)
        sawKey = sawKey || after.m_keys[i];
    CHECK(sawKey);
}

TEST_CASE("SSHW3 运行中降码率后输出明显变小", "[sharehw]")
{
    HwMfScope mf;
    if (!hwAvailable())
        return;
    //帧数多一些，让码率控制有时间收敛
    const int kFrames = 60;
    ShareHwH264Encoder hw;
    REQUIRE(hw.Init(kHwW, kHwH, kHwFps, kHwHighBps, kHwGop));
    std::vector<uint8_t> nv12(static_cast<size_t>(kHwW) * kHwH * 3 / 2);
    HwSink high;
    HwSink low;
    int64_t pts = 0;
    int frame = 0;
    srand(7);
    for (int i = 0; i < kFrames; ++i)
    {
        hwTextureFrame(nv12, kHwW, kHwH, frame++);
        //加一点噪声，让 2 Mbps 下编码器确实需要那么多数据
        for (size_t k = 0; k < static_cast<size_t>(kHwW) * kHwH; ++k)
            nv12[k] = static_cast<uint8_t>(nv12[k] + (rand() & 15));
        REQUIRE(hw.EncodeNv12(&nv12[0], kHwW, pts, &high));
        pts += kHwFrameHns;
    }
    REQUIRE(hw.SetBitrate(kHwLowBps));
    for (int i = 0; i < kFrames; ++i)
    {
        hwTextureFrame(nv12, kHwW, kHwH, frame++);
        for (size_t k = 0; k < static_cast<size_t>(kHwW) * kHwH; ++k)
            nv12[k] = static_cast<uint8_t>(nv12[k] + (rand() & 15));
        REQUIRE(hw.EncodeNv12(&nv12[0], kHwW, pts, &low));
        pts += kHwFrameHns;
    }
    CAPTURE(hw.Name(), high.m_bytes, low.m_bytes);
    REQUIRE(high.m_bytes > 0);
    CHECK(low.m_bytes * 3 < high.m_bytes);
    REQUIRE_FALSE(low.m_keys.empty());
    CHECK(low.m_keys[0]);
}

TEST_CASE("SSHW4 ShareVideoEncoder 有硬件时用硬件，禁用硬件时用软件", "[sharehw]")
{
    HwMfScope mf;
    const bool hasHw = ShareHwH264Encoder::CountHardwareEncoders() > 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool allowHw = (pass == 0);
        ShareVideoEncoder encoder;
        encoder.SetHardwareAllowed(allowHw);
        HwShareSink sink;
        std::vector<uint8_t> bgra;
        for (int i = 0; i < kHwFrames; ++i)
        {
            bgra = hwTextureBgra(kHwW, kHwH, i);
            ShareCaptureFrame frame;
            frame.m_data = &bgra[0];
            frame.m_stride = kHwW * 4;
            frame.m_width = kHwW;
            frame.m_height = kHwH;
            REQUIRE(encoder.Encode(frame, i == 0, static_cast<uint64_t>(i) * 66000, &sink));
        }
        CAPTURE(allowHw, hasHw, sink.m_packets, sink.m_keyframes);
        CHECK(encoder.IsHardware() == (allowHw && hasHw));
        CHECK(sink.m_packets > 0);
        CHECK(sink.m_keyframes >= 1);
    }
}

namespace {

//把一帧纹理交给 ShareVideoEncoder；frameNo 决定纹理的相位，时间戳按 15 fps 递增
bool hwEncodeTexture(ShareVideoEncoder& encoder, int w, int h, int frameNo, HwShareSink& sink)
{
    //15 fps 时每帧的时长（微秒）
    const uint64_t kFrameUs = 66000;
    std::vector<uint8_t> bgra = hwTextureBgra(w, h, frameNo);
    ShareCaptureFrame frame;
    frame.m_data = &bgra[0];
    frame.m_stride = w * 4;
    frame.m_width = w;
    frame.m_height = h;
    return encoder.Encode(frame, false, static_cast<uint64_t>(frameNo) * kFrameUs, &sink);
}

} // namespace

TEST_CASE("SSHW5 ShareVideoEncoder 换源尺寸时报告码率范围变化，按大尺寸建不起来时逐档退", "[sharehw]")
{
    HwMfScope mf;
    //默认是 2K 档：1080p 源编原尺寸、码率范围 2 Mbps ~ 300 kbps
    ShareVideoEncoder encoder;
    encoder.SetHardwareAllowed(false);
    HwShareSink sink;
    REQUIRE(hwEncodeTexture(encoder, 1920, 1080, 0, sink));
    CHECK(encoder.EncodeWidth() == 1920);
    CHECK(encoder.EncodeHeight() == 1080);
    CHECK(encoder.TakeRangeChanged());
    CHECK_FALSE(encoder.TakeRangeChanged());
    CHECK(encoder.MaxBps() == 2000000);
    CHECK(encoder.MinBps() == 300000);
    CHECK(encoder.BitrateBps() == 2000000);
    REQUIRE(hwEncodeTexture(encoder, 1920, 1080, 1, sink));
    CHECK_FALSE(encoder.LastWasRebuild());
    CHECK_FALSE(encoder.TakeRangeChanged());

    //码率自适应降到一半之后换成 2K 源：编码尺寸、码率范围随之变化，当前码率按比例换算，重建后这一帧是关键帧
    encoder.SetBitrate(1000000);
    const int keysBefore = sink.m_keyframes;
    REQUIRE(hwEncodeTexture(encoder, 2560, 1440, 2, sink));
    CHECK(encoder.LastWasRebuild());
    CHECK(encoder.EncodeWidth() == 2560);
    CHECK(encoder.EncodeHeight() == 1440);
    CHECK(encoder.TakeRangeChanged());
    CHECK(encoder.MaxBps() == 3600000);
    CHECK(encoder.MinBps() == 500000);
    CHECK(encoder.BitrateBps() == 1800000);
    CHECK(sink.m_keyframes == keysBefore + 1);

    //编码器只接受长边不超过 1920（模拟驱动或系统编码器拒绝大尺寸）：退到 1920，码率范围按 1080p
    ShareVideoEncoder limited;
    limited.SetHardwareAllowed(false);
    limited.SetParams(ShareVideoParamsForQuality(share_quality_4k));
    limited.SetOpenLimitForTest(1920);
    HwShareSink limitedSink;
    REQUIRE(hwEncodeTexture(limited, 2560, 1440, 0, limitedSink));
    CHECK(limited.EncodeWidth() == 1920);
    CHECK(limited.EncodeHeight() == 1080);
    CHECK(limited.MaxBps() == 2000000);
    CHECK(limitedSink.m_keyframes >= 1);
    //同一次推流里记住退过的档位：再换尺寸也不再尝试大尺寸
    REQUIRE(hwEncodeTexture(limited, 3840, 2160, 1, limitedSink));
    CHECK(limited.EncodeWidth() == 1920);
    CHECK(limited.EncodeHeight() == 1080);
}

TEST_CASE("SHSQ1 共享中改清晰度：下一帧按新上限重建编码器并编成关键帧", "[sharehw]")
{
    HwMfScope mf;
    ShareVideoEncoder encoder;
    encoder.SetHardwareAllowed(false);
    encoder.SetParams(ShareVideoParamsForQuality(share_quality_2k));
    HwShareSink sink;
    REQUIRE(hwEncodeTexture(encoder, 3840, 2160, 0, sink));
    CHECK(encoder.EncodeWidth() == 2560);
    CHECK(encoder.EncodeHeight() == 1440);
    encoder.TakeRangeChanged();
    REQUIRE(hwEncodeTexture(encoder, 3840, 2160, 1, sink));
    CHECK_FALSE(encoder.LastWasRebuild());

    //改成 4K 档：下一帧重建、原尺寸编码、编成关键帧、码率范围换成 8 Mbps
    encoder.SetParams(ShareVideoParamsForQuality(share_quality_4k));
    const int keysBefore = sink.m_keyframes;
    REQUIRE(hwEncodeTexture(encoder, 3840, 2160, 2, sink));
    CHECK(encoder.LastWasRebuild());
    CHECK(encoder.EncodeWidth() == 3840);
    CHECK(encoder.EncodeHeight() == 2160);
    CHECK(encoder.TakeRangeChanged());
    CHECK(encoder.MaxBps() == 8000000);
    CHECK(sink.m_keyframes == keysBefore + 1);

    //再设一次相同的参数：不重建
    encoder.SetParams(ShareVideoParamsForQuality(share_quality_4k));
    REQUIRE(hwEncodeTexture(encoder, 3840, 2160, 3, sink));
    CHECK_FALSE(encoder.LastWasRebuild());
    CHECK_FALSE(encoder.TakeRangeChanged());
}

namespace {

//RDPERF1：画面尺寸、连续编码的帧数与每帧的时长（30 fps，微秒）
const int kRdStaticW = 1920;
const int kRdStaticH = 1080;
const int kRdStaticFrames = 60;
const uint64_t kRdStaticFrameUs = 33333;
//H.264 NAL 类型的个数与取类型的掩码（NAL 头的低 5 位）
const int kRdNalTypes = 32;
const uint8_t kRdNalTypeMask = 0x1F;
//Annex B 起始码 00 00 01 的长度（字节）
const size_t kRdStartCodeLen = 3;

//RDPERF1：收集每个输出包的字节数、是否关键帧，以及各类 NAL 的累计字节数（含 NAL 头）
class RdStaticSink : public ShareVideoEncoderSink
{
public:
    RdStaticSink()
    {
        for (int t = 0; t < kRdNalTypes; ++t)
            m_nalBytes[t] = 0;
    }

    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) override
    {
        (void)timestampUs;
        m_sizes.push_back(len);
        m_keys.push_back(keyframe);
        countNals(data, len);
    }

    std::vector<size_t> m_sizes;                 //每个输出包的字节数
    std::vector<bool>   m_keys;                  //对应的包是否关键帧
    size_t              m_nalBytes[kRdNalTypes]; //各类 NAL 的累计字节数

private:
    //按起始码切出每个 NAL，记到它的类型下；四字节起始码前面多出的 0 计入上一个 NAL，不影响判断
    void countNals(const uint8_t* d, size_t n)
    {
        size_t nalStart = n;
        size_t i = 0;
        while (i + kRdStartCodeLen <= n)
        {
            if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
            {
                if (nalStart < n)
                    m_nalBytes[d[nalStart] & kRdNalTypeMask] += i - nalStart;
                nalStart = i + kRdStartCodeLen;
                i += kRdStartCodeLen;
                continue;
            }
            ++i;
        }
        if (nalStart < n)
            m_nalBytes[d[nalStart] & kRdNalTypeMask] += n - nalStart;
    }
};

/**
 *  RDPERF1 的一次运行：按远程桌面参数把同一张画面连续编 kRdStaticFrames 帧（第一帧要求关键帧），打印统计。
 *  @param hardware 是否允许硬件编码器
 */
void rdStaticRun(bool hardware)
{
    ShareVideoEncoder encoder;
    encoder.SetHardwareAllowed(hardware);
    encoder.SetParams(RdScreenVideoParams());
    RdStaticSink sink;
    std::vector<uint8_t> bgra = hwTextureBgra(kRdStaticW, kRdStaticH, 0);
    ShareCaptureFrame frame;
    frame.m_data = &bgra[0];
    frame.m_stride = kRdStaticW * 4;
    frame.m_width = kRdStaticW;
    frame.m_height = kRdStaticH;
    for (int i = 0; i < kRdStaticFrames; ++i)
        REQUIRE(encoder.Encode(frame, i == 0, static_cast<uint64_t>(i) * kRdStaticFrameUs, &sink));

    size_t keyBytes = 0;
    size_t deltaBytes = 0;
    size_t deltaMin = 0;
    size_t deltaMax = 0;
    int deltaCount = 0;
    for (size_t k = 0; k < sink.m_sizes.size(); ++k)
    {
        if (sink.m_keys[k])
        {
            keyBytes += sink.m_sizes[k];
            continue;
        }
        deltaBytes += sink.m_sizes[k];
        if (deltaCount == 0 || sink.m_sizes[k] < deltaMin)
            deltaMin = sink.m_sizes[k];
        if (sink.m_sizes[k] > deltaMax)
            deltaMax = sink.m_sizes[k];
        ++deltaCount;
    }
    std::string nals;
    for (int t = 0; t < kRdNalTypes; ++t)
    {
        if (sink.m_nalBytes[t] > 0)
            nals += " type" + std::to_string(t) + "=" + std::to_string(sink.m_nalBytes[t]);
    }
    WARN("RDPERF1 hardware allowed=" << (hardware ? 1 : 0) << ", hardware used=" << (encoder.IsHardware() ? 1 : 0)
         << ", " << encoder.EncodeWidth() << "x" << encoder.EncodeHeight() << ", bitrate " << encoder.BitrateBps()
         << ", packets " << sink.m_sizes.size() << ", keyframe bytes " << keyBytes << ", delta frames " << deltaCount
         << " (bytes " << deltaBytes << ", avg " << (deltaCount > 0 ? deltaBytes / deltaCount : 0) << ", min "
         << deltaMin << ", max " << deltaMax << "), NAL bytes by type:" << nals);
}

} // namespace

TEST_CASE("RDPERF1 远程桌面参数下连续编码同一张静止画面：每帧字节数与各类 NAL 的字节数（实测，不断言）", "[.rdperf]")
{
    HwMfScope mf;
    if (hwAvailable())
        rdStaticRun(true);
    rdStaticRun(false);
}

