/**
 *  2K / 4K 屏幕画面编解码的性能实测（隐藏标签 [.shareperf]，SSPERF1），ShareVideoPerfTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 第 5 节 A0（2026-10-15）。屏幕共享的编码长边上限由 1920 提到 2560 / 3840、
 *  远程桌面改用 H.264（2560、30 fps）之前，先在本机实测下面几项，结果写进方案文档，据此决定接收端优化做到哪一步：
 *    ① 系统软件编码器与本机硬件编码器能否按 2560×1440、3840×2160 建起来，码流里的 H.264 级别（SPS 的 level_idc），
 *       15 fps 与 30 fps 两种设置下每帧编码的平均与最长耗时（码率按 ComputeScreenVideoProfile 的最高码率）；
 *    ② 解码器按 1920×1080 初始化后解 2560×1440、3840×2160 的码流：能否随码流尺寸重新协商、解出的尺寸、
 *       每帧解码与 NV12→BGRA 转换（Release 版走 SSE2）的平均耗时；
 *    ③ 发送端把 3840×2160 缩到 2560×1440 与 1920×1080：逐像素实现、SSE2 实现与整帧 BGRA→NV12（含缩放）的每帧耗时；
 *    ④ 观看窗的 HALFTONE 绘制：2560×1440、3840×2160 的画面画到常见窗口尺寸的每帧耗时；同一行另列 A7 之后的
 *       ShareFrameDrawer（缩小且不小于一半时 SSE2 缩放后原样贴上）的耗时与所走的路径（2026-10-15）；
 *    ⑥ 远程桌面的参数（2560、30 fps、约 5.3 Mbps）下编码加解码每帧能否在 33 毫秒之内（由 ① ② 的 2560 / 30 fps 一行得出）。
 *  ⑤ 云端合成的速度由 CloudComposeBenchTests.cpp 的 CCB2（隐藏标签 [.cloudcomposesizes]，依次用 1080p、2K、4K 输入）测。
 *  结果只用 WARN 打印，不断言快慢（Debug 版与机器负载都会影响）；应在 Release 版、整机正常负载下运行：
 *      FlamingoUnit.exe "[.shareperf]"
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <windows.h>
#include <sstream>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"
#include "../../FlamingoMedia/Share/ShareFrameDrawer.h"   // ShareFrameDrawer：④ 的对比（A7）
#include "../../FlamingoMedia/Share/ShareHwH264Encoder.h"
#include "../../FlamingoMedia/Call/MfH264Codec.h"
#include "../../FlamingoMedia/Call/VideoFrameConvert.h"

#pragma comment(lib, "mfplat.lib")

using namespace screenshare;

namespace {

//每种设置编码的帧数（前几帧含编码器预热，一并计入平均值；最长耗时单独列出）
const int kPerfFrames = 60;
//缩放、绘制各跑的遍数
const int kPerfRuns = 10;
//关键帧间隔（帧）：只在第一帧出关键帧，之后都是增量帧，与屏幕共享的实际情况接近
const int kPerfGop = 1000;
//每秒的 100 纳秒数（Media Foundation 的时间戳单位）
const int64_t kPerfHnsPerSecond = 10000000;
//合成画面：底色亮度、文字笔画亮度、「文字行」的高度与字宽、笔画的宽高、移动色块的边长与每帧位移
const uint8_t kPerfBackY = 235;
const uint8_t kPerfInkY = 40;
const int kPerfLineH = 24;
const int kPerfCharW = 10;
const int kPerfGlyphW = 7;
const int kPerfGlyphH = 14;
const int kPerfBlockSide = 240;
const int kPerfBlockStep = 17;
//色度的中性值，色块的 U / V
const uint8_t kPerfGrayUv = 128;
const uint8_t kPerfBlockU = 90;
const uint8_t kPerfBlockV = 200;
//文字笔画的伪随机：哈希乘数与「这个字格有字」的比例（低 2 位不全为 0，约四分之三）
const uint32_t kPerfHashMul = 2654435761u;
const uint32_t kPerfHashMask = 3u;
//H.264 起始码之后的 NAL 类型掩码与 SPS 的类型；SPS 里 level_idc 相对 NAL 头的偏移
const int kNalTypeMask = 0x1F;
const int kNalSps = 7;
const size_t kSpsLevelOffset = 3;
//每毫秒的计时器计数（QueryPerformanceFrequency 换算）
const double kPerfMsPerSecond = 1000.0;

//初始化本线程的 COM 与 Media Foundation
class PerfMfScope
{
public:
    PerfMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~PerfMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//高精度计时
class PerfClock
{
public:
    PerfClock()
    {
        LARGE_INTEGER f;
        ::QueryPerformanceFrequency(&f);
        m_ticksPerMs = static_cast<double>(f.QuadPart) / kPerfMsPerSecond;
    }

    int64_t Now() const
    {
        LARGE_INTEGER t;
        ::QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    double Ms(int64_t from, int64_t to) const
    {
        return static_cast<double>(to - from) / m_ticksPerMs;
    }

private:
    double m_ticksPerMs;   //每毫秒的计数
};

//画一帧「像屏幕」的 NV12：浅色底上一行行伪随机的字形笔画，加一个随帧号移动的色块
void perfFrameNv12(int w, int h, int frame, std::vector<uint8_t>& nv12)
{
    nv12.assign(static_cast<size_t>(w) * h * 3 / 2, kPerfGrayUv);
    uint8_t* yPlane = &nv12[0];
    uint8_t* uvPlane = yPlane + static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y)
    {
        uint8_t* row = yPlane + static_cast<size_t>(y) * w;
        const int line = y / kPerfLineH;
        const int inLine = y % kPerfLineH;
        for (int x = 0; x < w; ++x)
        {
            const int cell = x / kPerfCharW;
            const uint32_t hash = static_cast<uint32_t>(line * w + cell) * kPerfHashMul;
            const bool inGlyph = (x % kPerfCharW) < kPerfGlyphW && inLine < kPerfGlyphH;
            //字格里按哈希的若干位决定画横笔还是竖笔，形成密集的细线条（与文字类似，压缩难度高于大色块）
            const bool stroke = inGlyph && (hash & kPerfHashMask) != 0 &&
                                ((((hash >> 8) & 1u) != 0 && inLine % 4 == 0) || (((hash >> 9) & 1u) != 0 && x % 3 == 0));
            row[x] = stroke ? kPerfInkY : kPerfBackY;
        }
    }
    //移动的色块：亮度渐变、带颜色
    const int left = (frame * kPerfBlockStep) % (w - kPerfBlockSide);
    const int top = (frame * kPerfBlockStep / 2) % (h - kPerfBlockSide);
    for (int y = top; y < top + kPerfBlockSide; ++y)
    {
        uint8_t* row = yPlane + static_cast<size_t>(y) * w;
        for (int x = left; x < left + kPerfBlockSide; ++x)
            row[x] = static_cast<uint8_t>((x + y + frame) & 0xFF);
    }
    for (int y = top / 2; y < (top + kPerfBlockSide) / 2; ++y)
    {
        uint8_t* row = uvPlane + static_cast<size_t>(y) * w;
        for (int x = left & ~1; x < left + kPerfBlockSide; x += 2)
        {
            row[x] = kPerfBlockU;
            row[x + 1] = kPerfBlockV;
        }
    }
}

//找出访问单元里 SPS 的 level_idc（如 51 表示 Level 5.1）；没有 SPS 时返回 -1
int perfLevelIdc(const std::string& au)
{
    for (size_t i = 0; i + 2 < au.size(); ++i)
    {
        if (au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 1)
        {
            const size_t nal = i + 3;
            if (nal + kSpsLevelOffset < au.size() && (static_cast<uint8_t>(au[nal]) & kNalTypeMask) == kNalSps)
                return static_cast<uint8_t>(au[nal + kSpsLevelOffset]);
        }
    }
    return -1;
}

//收集编码输出
class PerfEncodeSink : public call::MfH264EncoderSink
{
public:
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)keyframe;
        (void)ptsHns;
        m_units.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    std::vector<std::string> m_units;   //按产出顺序的访问单元
};

//一种编码设置的实测结果
struct PerfEncodeResult
{
    bool   m_ok;          //编码器建起来了
    int    m_level;       //SPS 的 level_idc；没有输出时为 -1
    double m_avgMs;       //每帧编码的平均耗时（毫秒）
    double m_maxMs;       //每帧编码的最长耗时（毫秒）
    double m_kbps;        //实际输出码率（kbit/s）
    std::vector<std::string> m_units;   //编出的访问单元（供解码实测）
};

/**
 *  按一种设置编 kPerfFrames 帧并计时。
 *  @param hardware 用硬件编码器（ShareHwH264Encoder）还是系统软件编码器（call::MfH264Encoder）
 */
PerfEncodeResult perfEncode(int w, int h, int fps, int bitrateBps, bool hardware)
{
    PerfEncodeResult r;
    r.m_ok = false;
    r.m_level = -1;
    r.m_avgMs = 0.0;
    r.m_maxMs = 0.0;
    r.m_kbps = 0.0;
    call::MfH264Encoder sw;
    ShareHwH264Encoder hw;
    r.m_ok = hardware ? hw.Init(w, h, fps, bitrateBps, kPerfGop) : sw.Init(w, h, fps, bitrateBps, kPerfGop);
    if (!r.m_ok)
        return r;
    PerfClock clock;
    PerfEncodeSink sink;
    std::vector<uint8_t> nv12;
    double total = 0.0;
    size_t bytes = 0;
    for (int i = 0; i < kPerfFrames; ++i)
    {
        perfFrameNv12(w, h, i, nv12);
        const int64_t pts = static_cast<int64_t>(i) * kPerfHnsPerSecond / fps;
        const int64_t t0 = clock.Now();
        if (hardware)
            hw.EncodeNv12(&nv12[0], w, pts, &sink);
        else
            sw.EncodeNv12(&nv12[0], w, pts, &sink);
        const double ms = clock.Ms(t0, clock.Now());
        total += ms;
        if (ms > r.m_maxMs)
            r.m_maxMs = ms;
    }
    r.m_avgMs = total / kPerfFrames;
    for (size_t i = 0; i < sink.m_units.size(); ++i)
        bytes += sink.m_units[i].size();
    //实际码率：总字节 × 8 ÷ 时长（帧数 ÷ 帧率），单位 kbit/s
    const double seconds = static_cast<double>(kPerfFrames) / fps;
    r.m_kbps = static_cast<double>(bytes) * 8.0 / seconds / kPerfMsPerSecond;
    if (!sink.m_units.empty())
        r.m_level = perfLevelIdc(sink.m_units[0]);
    r.m_units.swap(sink.m_units);
    return r;
}

//解码输出：记下尺寸，并把每帧转成 BGRA 计时
class PerfDecodeSink : public call::MfH264DecoderSink
{
public:
    PerfDecodeSink()
        : m_frames(0)
        , m_width(0)
        , m_height(0)
        , m_convertMs(0.0)
    {
    }

    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override
    {
        (void)ptsHns;
        ++m_frames;
        m_width = width;
        m_height = height;
        m_bgra.resize(static_cast<size_t>(width) * height * 4);
        const int64_t t0 = m_clock.Now();
        call::Nv12ToBgra(nv12, width, height, stride, &m_bgra[0], width * 4);
        m_convertMs += m_clock.Ms(t0, m_clock.Now());
    }

    int                  m_frames;      //解出的帧数
    int                  m_width;       //解出的宽
    int                  m_height;      //解出的高
    double               m_convertMs;   //NV12→BGRA 的总耗时（毫秒）
    std::vector<uint8_t> m_bgra;        //转换输出
    PerfClock            m_clock;       //计时
};

//造一张 BGRA 画面（缩放与绘制实测用）：内容不影响耗时，填伪随机字节
std::vector<uint8_t> perfBgra(int w, int h)
{
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    uint32_t seed = static_cast<uint32_t>(w * h);
    for (size_t i = 0; i < img.size(); ++i)
    {
        seed = seed * kPerfHashMul + 1u;
        img[i] = static_cast<uint8_t>(seed >> 24);
    }
    return img;
}

} // namespace

TEST_CASE("SSPERF1 2K / 4K screen video encode, decode, scale and draw timings", "[.shareperf]")
{
    PerfMfScope mf;
    PerfClock clock;
    std::ostringstream os;
#ifdef _DEBUG
    os << "SSPERF1 (Debug build: timings are NOT representative, run the Release build)\n";
#else
    os << "SSPERF1 (Release build)\n";
#endif
    const int hwCount = ShareHwH264Encoder::CountHardwareEncoders();
    os << "hardware H.264 encoders: " << hwCount << "\n";

    //① ⑥ 编码：两种尺寸 × 两种帧率 × 软件 / 硬件；15 fps 用共享 4K 档的码率，30 fps 用远程桌面的码率
    const int sizes[][2] = { { 2560, 1440 }, { 3840, 2160 } };
    const int fpsList[] = { 15, 30 };
    std::vector<std::string> swUnits[2];
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
    {
        const int w = sizes[s][0];
        const int h = sizes[s][1];
        for (size_t f = 0; f < sizeof(fpsList) / sizeof(fpsList[0]); ++f)
        {
            const int fps = fpsList[f];
            //15 fps 用共享的参数、30 fps 用远程桌面的参数；远程桌面的长边上限是 2560，3840 的源实际会编成 2560，
            //这里按原尺寸实测，码率按原尺寸的像素数换算
            ScreenVideoProfile full;
            ScreenVideoParams unlimited = (fps == kRdVideoMaxFps) ? RdScreenVideoParams()
                                                                  : ShareVideoParamsForQuality(share_quality_4k);
            unlimited.m_maxLongSide = 0;
            ComputeScreenVideoProfile(w, h, unlimited, full);
            const int bps = full.m_maxBps;
            for (int hwPass = 0; hwPass < 2; ++hwPass)
            {
                const bool hardware = (hwPass == 1);
                if (hardware && hwCount <= 0)
                    continue;
                PerfEncodeResult r = perfEncode(w, h, fps, bps, hardware);
                os << "encode " << w << "x" << h << " " << fps << "fps " << (bps / 1000) << "kbps "
                   << (hardware ? "hw" : "sw") << ": ";
                if (!r.m_ok)
                {
                    os << "INIT FAILED\n";
                    continue;
                }
                os << "level_idc " << r.m_level << ", avg " << r.m_avgMs << " ms, max " << r.m_maxMs << " ms, output "
                   << r.m_kbps << " kbps\n";
                if (!hardware && fps == fpsList[0])
                    swUnits[s].swap(r.m_units);
            }
        }
    }

    //② 解码：解码器按 1920×1080 初始化，解大尺寸的码流（软件编码器 15 fps 的输出）
    const int kInitW = 1920;
    const int kInitH = 1080;
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
    {
        if (swUnits[s].empty())
            continue;
        call::MfH264Decoder dec;
        if (!dec.Init(kInitW, kInitH))
        {
            os << "decode " << sizes[s][0] << "x" << sizes[s][1] << ": INIT FAILED\n";
            continue;
        }
        PerfDecodeSink sink;
        double total = 0.0;
        double maxMs = 0.0;
        for (size_t i = 0; i < swUnits[s].size(); ++i)
        {
            const std::string& u = swUnits[s][i];
            const int64_t t0 = clock.Now();
            dec.DecodePacket(reinterpret_cast<const uint8_t*>(u.data()), u.size(),
                             static_cast<int64_t>(i) * kPerfHnsPerSecond / fpsList[0], &sink);
            const double ms = clock.Ms(t0, clock.Now());
            total += ms;
            if (ms > maxMs)
                maxMs = ms;
        }
        const int frames = (sink.m_frames > 0) ? sink.m_frames : 1;
        //解码耗时里含了回调里的颜色转换，单独扣出来
        const double decodeOnly = (total - sink.m_convertMs) / frames;
        os << "decode " << sizes[s][0] << "x" << sizes[s][1] << " (decoder init " << kInitW << "x" << kInitH
           << "): frames " << sink.m_frames << "/" << swUnits[s].size() << ", decoded size " << sink.m_width << "x"
           << sink.m_height << ", decode avg " << decodeOnly << " ms, NV12->BGRA avg " << (sink.m_convertMs / frames)
           << " ms, max packet " << maxMs << " ms\n";
    }

    //③ 发送端缩放：4K → 2K、4K → 1080p
    {
        const int srcW = 3840;
        const int srcH = 2160;
        const std::vector<uint8_t> src = perfBgra(srcW, srcH);
        const int targets[][2] = { { 2560, 1440 }, { 1920, 1080 } };
        for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t)
        {
            const int dstW = targets[t][0];
            const int dstH = targets[t][1];
            std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * 4);
            std::vector<uint8_t> nv12(static_cast<size_t>(dstW) * dstH * 3 / 2);
            BgraScaler scaler;
            int64_t t0 = clock.Now();
            for (int r = 0; r < kPerfRuns; ++r)
                scaler.ScaleReference(&src[0], srcW * 4, srcW, srcH, &dst[0], dstW * 4, dstW, dstH);
            const double refMs = clock.Ms(t0, clock.Now()) / kPerfRuns;
            t0 = clock.Now();
            for (int r = 0; r < kPerfRuns; ++r)
                scaler.ScaleSse2(&src[0], srcW * 4, srcW, srcH, &dst[0], dstW * 4, dstW, dstH);
            const double sseMs = clock.Ms(t0, clock.Now()) / kPerfRuns;
            Nv12FrameConverter converter;
            t0 = clock.Now();
            for (int r = 0; r < kPerfRuns; ++r)
                converter.Convert(&src[0], srcW * 4, srcW, srcH, &nv12[0], dstW, dstH);
            const double convMs = clock.Ms(t0, clock.Now()) / kPerfRuns;
            os << "scale " << srcW << "x" << srcH << " -> " << dstW << "x" << dstH << ": reference " << refMs
               << " ms, sse2 " << sseMs << " ms, full BGRA->NV12 with scaling " << convMs << " ms\n";
        }
    }

    //④ 观看窗的 HALFTONE 绘制：大画面画到常见窗口尺寸
    {
        const int windows[][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 } };
        HDC screen = ::GetDC(NULL);
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        {
            const int srcW = sizes[s][0];
            const int srcH = sizes[s][1];
            const std::vector<uint8_t> src = perfBgra(srcW, srcH);
            BITMAPINFO bmi;
            memset(&bmi, 0, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = srcW;
            bmi.bmiHeader.biHeight = -srcH;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            for (size_t wi = 0; wi < sizeof(windows) / sizeof(windows[0]); ++wi)
            {
                const int dstW = windows[wi][0];
                const int dstH = windows[wi][1];
                HDC mem = ::CreateCompatibleDC(screen);
                HBITMAP bmp = ::CreateCompatibleBitmap(screen, dstW, dstH);
                HGDIOBJ old = ::SelectObject(mem, bmp);
                ::SetStretchBltMode(mem, HALFTONE);
                ::SetBrushOrgEx(mem, 0, 0, NULL);
                const int64_t t0 = clock.Now();
                for (int r = 0; r < kPerfRuns; ++r)
                {
                    ::StretchDIBits(mem, 0, 0, dstW, dstH, 0, 0, srcW, srcH, &src[0], &bmi, DIB_RGB_COLORS,
                                    SRCCOPY);
                }
                const double ms = clock.Ms(t0, clock.Now()) / kPerfRuns;
                //同样的尺寸改用 ShareFrameDrawer（A7，2026-10-15）：缩小且不小于一半时 SSE2 缩放后原样贴上
                ShareFrameDrawer drawer;
                const RECT dest = { 0, 0, dstW, dstH };
                int path = share_draw_none;
                const int64_t t1 = clock.Now();
                for (int r = 0; r < kPerfRuns; ++r)
                    path = drawer.Draw(mem, dest, &src[0], srcW, srcH, srcW * 4);
                ::GdiFlush();
                const double drawerMs = clock.Ms(t1, clock.Now()) / kPerfRuns;
                ::SelectObject(mem, old);
                ::DeleteObject(bmp);
                ::DeleteDC(mem);
                os << "halftone " << srcW << "x" << srcH << " -> " << dstW << "x" << dstH << ": " << ms << " ms, drawer "
                   << drawerMs << " ms (path " << path << ")\n";
            }
        }
        ::ReleaseDC(NULL, screen);
    }
    WARN(os.str());
    CHECK(true);
}
