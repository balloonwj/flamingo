/**
 *  云端录像离线合成的计时（隐藏标签 [.cloudcomposebench]），CloudComposeBenchTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 27a 步、第 14.6 节。合成器（CCloudRecordComposeTask）把 imgserver 录下的原始帧文件
 *  合成为 MP4，此前只合成过 6 秒的录像（约 5 秒），长录像的合成速度与内存占用一直是「据推测」。本用例用程序生成一份
 *  长录像（默认 1 小时）的原始帧文件，在本机离线合成，记录耗时、CPU 与内存峰值；不占用桌面、不录真实屏幕：
 *    1. 用屏幕共享的画面编码器（ShareVideoEncoder）把 10 秒 1920×1080、每秒 15 帧的合成画面（滚动的色块与条纹）编成 H.264，
 *       用 Opus 编码器把 1 秒的合成声音编成 50 包；
 *    2. 按真实的帧格式（ShareFraming 的视频载荷与分片、声音帧）循环写满整个时长，写成 .frec（座位 0 一个人共享、开着麦克风）；
 *    3. 交给合成器合成，期间每 200 毫秒采一次本进程的内存，结束后输出耗时、合成速度（录像时长 ÷ 合成耗时）、
 *       进程 CPU、工作集与私有内存的峰值（相对合成开始前的增量）、输出 MP4 的大小；
 *    4. 核对画面内容（2026-10-08，合成提速后补）：从输出 MP4 的第 kCcbCheckSeconds 秒取一帧，与生成时画的对应原始画面
 *       逐像素比较平均差值（两次有损编码有小误差；画面错位、全黑、上下颠倒时差值会大得多），要求低于 kCcbMaxMeanDiff。
 *  时长可用环境变量 CCB_SECONDS 改（秒），便于先跑一段短的核对流程；环境变量 CCB_CONTENT 为 static 时画面改为「接近真实」的
 *  内容（条纹不动、只有一个色块在动），否则为最吃力的整屏运动画面。临时文件写在系统临时目录，用完删除。
 *  本用例要跑十几分钟以上，挂隐藏标签，只在显式指定时运行：FlamingoUnit.exe "[.cloudcomposebench]"。
 *  只断言合成成功、输出文件非空，不断言具体快慢。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <psapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>   // IMFSourceReader：核对输出画面（2026-10-08）
#include <propvarutil.h>

#include "../../FlamingoMedia/Call/OpusAudioCodec.h"
#include "../../FlamingoMedia/Record/CloudRecordComposer.h"
#include "../../FlamingoMedia/Record/CloudRecordCompose.h"
#include "../../FlamingoMedia/Record/CloudRecordReader.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareVideoEncoder.h"
#include "../net/Protocol.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

using namespace screenrecord;

namespace {

//合成画面：尺寸、帧率、编码的片段长度（帧）、码率
const int kCcbWidth = 1920;
const int kCcbHeight = 1080;
const int kCcbFps = 15;
const int kCcbClipFrames = 150;
//默认录像时长（秒）与环境变量名
const int kCcbDefaultSeconds = 3600;
const char* const kCcbSecondsEnv = "CCB_SECONDS";
//画面内容的环境变量：值为 static 时用「接近真实」的画面（条纹不动、只有一个色块在动，像在静止的文档上移动鼠标），
//否则用最吃力的画面（整屏条纹滚动、十几个色块同时移动）
const char* const kCcbContentEnv = "CCB_CONTENT";
const char* const kCcbContentStatic = "static";
//合成声音：采样率、每包样点、码率；正弦的频率与幅度、调制周期（包）
const int kCcbAudioRate = 48000;
const int kCcbAudioFrameSamples = 960;
const int kCcbAudioBitrate = 32000;
const int kCcbAudioPackets = 50;
const double kCcbToneHz = 220.0;
const double kCcbToneAmplitude = 6000.0;
const int kCcbAudioPacketMs = 20;
//画面里的色块：个数、边长、每帧移动的像素；条纹的周期与每帧位移
const int kCcbBlocks = 12;
const int kCcbBlockSide = 160;
const int kCcbBlockStep = 9;
const int kCcbStripePeriod = 24;
const int kCcbStripeStep = 3;
//像素：每像素字节数、底色灰度、条纹灰度
const int kCcbBpp = 4;
const uint8_t kCcbBackground = 230;
const uint8_t kCcbStripe = 200;
//色块颜色随序号变化的步长（红、绿、蓝三个分量各自的步长；蓝色另随帧号变化）与绿色的起点
const int kCcbRedStep = 20;
const int kCcbGreenStep = 15;
const int kCcbBlueStep = 70;
const int kCcbGreenTop = 255;
const int kCcbByteMask = 0xFF;
//声音幅度的起伏：增益在 kCcbGainBase ± kCcbGainSwing 之间
const double kCcbGainBase = 0.5;
const double kCcbGainSwing = 0.5;
//原始帧文件的字段：录像 id、开始时刻、房间号、共享人
const uint64_t kCcbRecordId = 271;
const int64_t kCcbStart = 1790000000000LL;
const char* const kCcbRoom = "room-ccb";
const int64_t kCcbUserId = 5127;
const int kCcbSeat = 0;
//合成器的回报消息（只投给测试自己的通知窗口）
const UINT kCcbMsgNames = WM_APP + 0x7FE0;
const UINT kCcbMsgDone = WM_APP + 0x7FE1;
//采样内存的间隔（毫秒）与合成的等待上限（毫秒，3 小时）
const DWORD kCcbSampleMs = 200;
const ULONGLONG kCcbComposeTimeoutMs = 3ULL * 3600 * 1000;
//换算
const double kCcbMs = 1000.0;
const double kCcbMb = 1024.0 * 1024.0;
const uint64_t kCcbUsPerSecond = 1000000;
const double kCcbTwoPi = 6.283185307179586;
//核对画面：取输出的第几秒、平均差值的上限（每个颜色分量 0～255 的平均绝对差）、为找到这一秒最多往后读几帧
//（跳转只落到之前的关键帧上，之后要逐帧往后读）
const int kCcbCheckSeconds = 5;
const int kCcbMaxReadFrames = 600;
const double kCcbMaxMeanDiff = 24.0;
//100 纳秒为单位的时间每秒的个数（Media Foundation 的时间戳）
const int64_t kCcbHnsPerSecond = 10000000;

void ccbPutLe(std::string& out, uint64_t v, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        v >>= 8;
    }
}

//.frec 文件头：魔数、版本、保留、录像 id、开始时刻、房间号
std::string ccbHeader()
{
    std::string h("FREC");
    ccbPutLe(h, kFrecVersion, sizeof(uint16_t));
    ccbPutLe(h, 0, sizeof(uint16_t));
    ccbPutLe(h, kCcbRecordId, sizeof(uint64_t));
    ccbPutLe(h, static_cast<uint64_t>(kCcbStart), sizeof(uint64_t));
    ccbPutLe(h, strlen(kCcbRoom), sizeof(uint16_t));
    h += kCcbRoom;
    return h;
}

//一条记录：类型、相对毫秒、长度、内容
void ccbRecord(FILE* f, int type, uint32_t relMs, const std::string& payload)
{
    std::string r;
    r.push_back(static_cast<char>(type));
    ccbPutLe(r, relMs, sizeof(uint32_t));
    ccbPutLe(r, payload.size(), sizeof(uint32_t));
    r += payload;
    fwrite(r.data(), 1, r.size(), f);
}

//画第 index 帧：浅灰底、斜条纹、沿不同方向移动的色块。staticContent 为真时条纹不动、只有第一个色块在动
void ccbDrawFrame(int index, bool staticContent, std::vector<uint8_t>& bgra)
{
    const int stripeShift = staticContent ? 0 : index * kCcbStripeStep;
    const int movingBlocks = staticContent ? 1 : kCcbBlocks;
    bgra.assign(static_cast<size_t>(kCcbWidth) * kCcbHeight * kCcbBpp, kCcbBackground);
    for (int y = 0; y < kCcbHeight; ++y)
    {
        uint8_t* row = &bgra[static_cast<size_t>(y) * kCcbWidth * kCcbBpp];
        for (int x = 0; x < kCcbWidth; ++x)
        {
            if (((x + y + stripeShift) / (kCcbStripePeriod / 2)) % 2 == 0)
            {
                row[x * kCcbBpp] = kCcbStripe;
                row[x * kCcbBpp + 1] = kCcbStripe;
                row[x * kCcbBpp + 2] = kCcbStripe;
            }
        }
    }
    for (int b = 0; b < movingBlocks; ++b)
    {
        const int range = kCcbWidth - kCcbBlockSide;
        const int left = (b * kCcbBlockSide + index * kCcbBlockStep * (b + 1)) % range;
        const int top = (b * (kCcbHeight / kCcbBlocks)) % (kCcbHeight - kCcbBlockSide);
        const uint8_t red = static_cast<uint8_t>(b * kCcbRedStep);
        const uint8_t green = static_cast<uint8_t>(kCcbGreenTop - b * kCcbGreenStep);
        const uint8_t blue = static_cast<uint8_t>((b * kCcbBlueStep + index) & kCcbByteMask);
        for (int y = top; y < top + kCcbBlockSide; ++y)
        {
            uint8_t* row = &bgra[static_cast<size_t>(y) * kCcbWidth * kCcbBpp];
            for (int x = left; x < left + kCcbBlockSide; ++x)
            {
                row[x * kCcbBpp] = blue;
                row[x * kCcbBpp + 1] = green;
                row[x * kCcbBpp + 2] = red;
            }
        }
    }
}

//编好的一个 H.264 访问单元
struct CcbUnit
{
    std::string m_data;       //字节流格式，含起始码
    bool        m_keyframe;   //是否关键帧
};

//收编码器的输出
class CcbEncodedSink : public screenshare::ShareVideoEncoderSink
{
public:
    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) override
    {
        (void)timestampUs;
        CcbUnit u;
        u.m_data.assign(reinterpret_cast<const char*>(data), len);
        u.m_keyframe = keyframe;
        m_units.push_back(u);
    }

    std::vector<CcbUnit> m_units;   //按产出顺序
};

//在一条 MTA 线程上编码画面片段（编码器要求所在线程已 CoInitializeEx 与 MFStartup）；返回编出的访问单元与编码尺寸。
//staticContent 见 ccbDrawFrame
void ccbEncodeClip(bool staticContent, std::vector<CcbUnit>& units, int& encW, int& encH)
{
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    {
        screenshare::ShareVideoEncoder enc;
        CcbEncodedSink sink;
        std::vector<uint8_t> bgra;
        for (int i = 0; i < kCcbClipFrames; ++i)
        {
            ccbDrawFrame(i, staticContent, bgra);
            screenshare::ShareCaptureFrame frame;
            frame.m_data = &bgra[0];
            frame.m_stride = kCcbWidth * kCcbBpp;
            frame.m_width = kCcbWidth;
            frame.m_height = kCcbHeight;
            enc.Encode(frame, i == 0, static_cast<uint64_t>(i) * kCcbUsPerSecond / kCcbFps, &sink);
        }
        encW = enc.EncodeWidth();
        encH = enc.EncodeHeight();
        enc.Close();
        units.swap(sink.m_units);
    }
    ::MFShutdown();
    ::CoUninitialize();
}

//编 1 秒的声音：220 Hz 正弦，幅度按包缓慢起伏
void ccbEncodeAudio(std::vector<std::string>& packets)
{
    call::OpusAudioCodec codec;
    REQUIRE(codec.Init(kCcbAudioRate, kCcbAudioFrameSamples, kCcbAudioBitrate));
    std::vector<int16_t> pcm(kCcbAudioFrameSamples);
    for (int p = 0; p < kCcbAudioPackets; ++p)
    {
        const double gain = kCcbGainBase + kCcbGainSwing * sin(kCcbTwoPi * p / kCcbAudioPackets);
        for (int i = 0; i < kCcbAudioFrameSamples; ++i)
        {
            const double t = static_cast<double>(p * kCcbAudioFrameSamples + i) / kCcbAudioRate;
            pcm[i] = static_cast<int16_t>(kCcbToneAmplitude * gain * sin(kCcbTwoPi * kCcbToneHz * t));
        }
        std::vector<unsigned char> out;
        REQUIRE(codec.Encode(&pcm[0], kCcbAudioFrameSamples, out));
        packets.push_back(std::string(reinterpret_cast<const char*>(out.data()), out.size()));
    }
}

std::wstring ccbTempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

uint64_t ccbFileBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
        return 0;
    return (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

double ccbProcessCpuSeconds()
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    ULARGE_INTEGER k;
    ULARGE_INTEGER u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    //FILETIME 的单位是 100 纳秒
    const double kHundredNsPerSecond = 10000000.0;
    return static_cast<double>(k.QuadPart + u.QuadPart) / kHundredNsPerSecond;
}

//本进程此刻的工作集与私有内存（字节）
void ccbMemory(uint64_t& workingSet, uint64_t& privateBytes)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    workingSet = 0;
    privateBytes = 0;
    if (!::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return;
    workingSet = pmc.WorkingSetSize;
    privateBytes = pmc.PrivateUsage;
}

//是否用「接近真实」的画面（环境变量 CCB_CONTENT 为 static）
bool ccbStaticContent()
{
    char buf[32] = { 0 };
    size_t len = 0;
    return getenv_s(&len, buf, sizeof(buf), kCcbContentEnv) == 0 && len > 0 && strcmp(buf, kCcbContentStatic) == 0;
}

//从 MP4 的 atSeconds 秒处读出一帧（RGB32，经系统视频处理器转换）；返回自上而下的 BGRA 与它的时间戳
bool ccbReadMp4Frame(const std::wstring& path, int atSeconds, std::vector<uint8_t>& bgra, int& w, int& h,
                     int64_t& sampleHns)
{
    IMFAttributes* attrs = NULL;
    if (FAILED(::MFCreateAttributes(&attrs, 1)))
        return false;
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    IMFSourceReader* reader = NULL;
    const HRESULT hrOpen = ::MFCreateSourceReaderFromURL(path.c_str(), attrs, &reader);
    attrs->Release();
    if (FAILED(hrOpen))
        return false;
    bool ok = false;
    IMFMediaType* want = NULL;
    if (SUCCEEDED(::MFCreateMediaType(&want)))
    {
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, want)))
        {
            IMFMediaType* actual = NULL;
            UINT32 fw = 0;
            UINT32 fh = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual)))
            {
                ::MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &fw, &fh);
                actual->Release();
            }
            PROPVARIANT pos;
            ::InitPropVariantFromInt64(static_cast<LONGLONG>(atSeconds) * kCcbHnsPerSecond, &pos);
            reader->SetCurrentPosition(GUID_NULL, pos);
            ::PropVariantClear(&pos);
            //逐帧往后读，直到时间戳到达目标时刻（差半帧以内）
            const LONGLONG target =
                static_cast<LONGLONG>(atSeconds) * kCcbHnsPerSecond - kCcbHnsPerSecond / kCcbFps / 2;
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = NULL;
            for (int n = 0; n < kCcbMaxReadFrames; ++n)
            {
                if (sample != NULL)
                {
                    sample->Release();
                    sample = NULL;
                }
                if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, &ts, &sample)) ||
                    (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                    break;
                if (sample != NULL && ts >= target)
                    break;
            }
            if (sample != NULL && ts >= target && fw > 0 && fh > 0)
            {
                IMFMediaBuffer* buf = NULL;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
                {
                    BYTE* data = NULL;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, NULL, &len)))
                    {
                        if (len >= fw * fh * kCcbBpp)
                        {
                            bgra.assign(data, data + static_cast<size_t>(fw) * fh * kCcbBpp);
                            w = static_cast<int>(fw);
                            h = static_cast<int>(fh);
                            sampleHns = ts;
                            ok = true;
                        }
                        buf->Unlock();
                    }
                    buf->Release();
                }
            }
            if (sample != NULL)
                sample->Release();
        }
        want->Release();
    }
    reader->Release();
    return ok;
}

//两幅画面左上角 cmpW × cmpH 范围内 RGB 分量的平均绝对差；flip 为真时把 a 按自下而上读
double ccbMeanDiff(const std::vector<uint8_t>& a, int aw, int ah, bool flip, const std::vector<uint8_t>& b, int bw,
                   int cmpW, int cmpH)
{
    const int kChannels = 3;
    double sum = 0.0;
    for (int y = 0; y < cmpH; ++y)
    {
        const int ay = flip ? (ah - 1 - y) : y;
        const uint8_t* ra = &a[static_cast<size_t>(ay) * aw * kCcbBpp];
        const uint8_t* rb = &b[static_cast<size_t>(y) * bw * kCcbBpp];
        for (int x = 0; x < cmpW; ++x)
        {
            for (int c = 0; c < kChannels; ++c)
            {
                const int d = static_cast<int>(ra[x * kCcbBpp + c]) - static_cast<int>(rb[x * kCcbBpp + c]);
                sum += (d < 0) ? -d : d;
            }
        }
    }
    return sum / (static_cast<double>(cmpW) * cmpH * kChannels);
}

//在一条 MTA 线程上读出输出 MP4 的一帧，与生成时的原始画面比较（取前后各一帧、正反两种行序里最小的差值）
void ccbCheckOutput(const std::wstring& mp4Path, bool staticContent, int clipFrames, double& bestDiff, int64_t& sampleHns)
{
    bestDiff = -1.0;
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    {
        std::vector<uint8_t> out;
        int w = 0;
        int h = 0;
        if (ccbReadMp4Frame(mp4Path, kCcbCheckSeconds, out, w, h, sampleHns) && clipFrames > 0)
        {
            const int cmpW = (w < kCcbWidth) ? w : kCcbWidth;
            const int cmpH = (h < kCcbHeight) ? h : kCcbHeight;
            const int64_t frameIndex = sampleHns * kCcbFps / kCcbHnsPerSecond;
            std::vector<uint8_t> expect;
            for (int64_t d = -1; d <= 1; ++d)
            {
                const int64_t idx = frameIndex + d;
                if (idx < 0)
                    continue;
                ccbDrawFrame(static_cast<int>(idx % clipFrames), staticContent, expect);
                for (int flip = 0; flip < 2; ++flip)
                {
                    const double diff = ccbMeanDiff(out, w, h, flip != 0, expect, kCcbWidth, cmpW, cmpH);
                    if (bestDiff < 0 || diff < bestDiff)
                        bestDiff = diff;
                }
            }
        }
    }
    ::MFShutdown();
    ::CoUninitialize();
}

int ccbSeconds()
{
    char buf[32] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, buf, sizeof(buf), kCcbSecondsEnv) == 0 && len > 0)
    {
        const int v = atoi(buf);
        if (v > 0)
            return v;
    }
    return kCcbDefaultSeconds;
}

} // namespace

TEST_CASE("CCB1 compose a synthetic long recording", "[.cloudcomposebench]")
{
    const int seconds = ccbSeconds();
    const bool staticContent = ccbStaticContent();
    //1. 编画面片段与声音
    const ULONGLONG gen0 = ::GetTickCount64();
    std::vector<CcbUnit> clip;
    int encW = 0;
    int encH = 0;
    std::thread encoder(&ccbEncodeClip, staticContent, std::ref(clip), std::ref(encW), std::ref(encH));
    encoder.join();
    REQUIRE_FALSE(clip.empty());
    REQUIRE(clip[0].m_keyframe);
    std::vector<std::string> audio;
    ccbEncodeAudio(audio);
    uint64_t clipBytes = 0;
    for (size_t i = 0; i < clip.size(); ++i)
        clipBytes += clip[i].m_data.size();

    //2. 写原始帧文件：座位记录，然后按时间交错写画面与声音
    const std::wstring frecPath = ccbTempPath(L"flamingo_ccb1.frec");
    const std::wstring mp4Path = ccbTempPath(L"flamingo_ccb1.mp4");
    FILE* f = NULL;
    REQUIRE(_wfopen_s(&f, frecPath.c_str(), L"wb") == 0);
    const std::string header = ccbHeader();
    fwrite(header.data(), 1, header.size(), f);
    std::string seat;
    seat.push_back(static_cast<char>(kCcbSeat));
    ccbPutLe(seat, static_cast<uint64_t>(kCcbUserId), sizeof(uint64_t));
    ccbRecord(f, kFrecRecordSeat, 0, seat);
    const int totalFrames = seconds * kCcbFps;
    const int totalPackets = seconds * (static_cast<int>(kCcbMs) / kCcbAudioPacketMs);
    int frameIndex = 0;
    int packetIndex = 0;
    while (frameIndex < totalFrames || packetIndex < totalPackets)
    {
        const int64_t frameMs = static_cast<int64_t>(frameIndex) * static_cast<int64_t>(kCcbMs) / kCcbFps;
        const int64_t packetMs = static_cast<int64_t>(packetIndex) * kCcbAudioPacketMs;
        const bool videoFirst = frameIndex < totalFrames && (packetIndex >= totalPackets || frameMs <= packetMs);
        if (videoFirst)
        {
            const CcbUnit& u = clip[frameIndex % clip.size()];
            const std::string payload =
                screenshare::ShareFraming::buildVideoPayload(encW, encH, u.m_data.data(), u.m_data.size());
            std::vector<std::string> bodies;
            screenshare::ShareFraming::fragmentFrame(screenshare_track_screen, kCcbSeat, u.m_keyframe,
                                                     static_cast<uint64_t>(frameMs) * static_cast<uint64_t>(kCcbMs),
                                                     payload.data(), payload.size(), 0, bodies);
            for (size_t i = 0; i < bodies.size(); ++i)
                ccbRecord(f, kFrecRecordFrame, static_cast<uint32_t>(frameMs), bodies[i]);
            ++frameIndex;
        }
        else
        {
            const std::string& pkt = audio[packetIndex % audio.size()];
            screenshare::ShareFrameHeader h;
            h.m_track = screenshare_track_mic;
            h.m_senderSeat = kCcbSeat;
            h.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
            h.m_timestamp = static_cast<uint64_t>(packetMs) * static_cast<uint64_t>(kCcbMs);
            ccbRecord(f, kFrecRecordFrame, static_cast<uint32_t>(packetMs),
                      screenshare::ShareFraming::encodeBody(h, pkt.data(), pkt.size()));
            ++packetIndex;
        }
    }
    fclose(f);
    const double genSeconds = (::GetTickCount64() - gen0) / kCcbMs;
    const uint64_t frecBytes = ccbFileBytes(frecPath);

    //3. 合成：名字请求立即答复，期间采样内存
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(notify != NULL);
    uint64_t baseWs = 0;
    uint64_t basePrivate = 0;
    ccbMemory(baseWs, basePrivate);
    uint64_t peakWs = baseWs;
    uint64_t peakPrivate = basePrivate;
    const double cpu0 = ccbProcessCpuSeconds();
    const ULONGLONG t0 = ::GetTickCount64();
    std::unique_ptr<CCloudRecordComposeTask> task(
        new CCloudRecordComposeTask(static_cast<int64_t>(kCcbRecordId), frecPath, mp4Path, notify, kCcbMsgNames,
                                    kCcbMsgDone, L"Segoe UI"));
    task->Start();
    int result = -100;
    bool done = false;
    while (!done && ::GetTickCount64() - t0 < kCcbComposeTimeoutMs)
    {
        MSG msg;
        while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
        {
            if (msg.message == kCcbMsgNames)
            {
                std::map<int64_t, std::wstring> names;
                names[kCcbUserId] = L"Bench";
                task->SetNames(names);
            }
            else if (msg.message == kCcbMsgDone)
            {
                result = static_cast<int>(msg.lParam);
                done = true;
            }
        }
        uint64_t ws = 0;
        uint64_t priv = 0;
        ccbMemory(ws, priv);
        if (ws > peakWs)
            peakWs = ws;
        if (priv > peakPrivate)
            peakPrivate = priv;
        if (!done)
            ::Sleep(kCcbSampleMs);
    }
    if (!done)
        task->Cancel();
    task->Join();
    const double composeSeconds = (::GetTickCount64() - t0) / kCcbMs;
    const CloudComposeStageTimes stages = task->StageTimes();
    const double cpu = ccbProcessCpuSeconds() - cpu0;
    const uint64_t mp4Bytes = ccbFileBytes(mp4Path);
    ::DestroyWindow(notify);

    std::ostringstream os;
    os << "CCB1 synthetic recording " << seconds << " s, " << (staticContent ? "static" : "full-motion")
       << " content (" << totalFrames << " frames at " << encW << "x" << encH
       << ", clip " << clip.size() << " units / " << (clipBytes / kCcbMb) << " MB, " << totalPackets
       << " audio packets)\n"
       << "  frec " << (frecBytes / kCcbMb) << " MB, generated in " << genSeconds << " s\n"
       << "  compose result " << result << ", took " << composeSeconds << " s, speed x"
       << (composeSeconds > 0 ? seconds / composeSeconds : 0.0) << ", process cpu " << cpu << " s\n"
       << "  working set peak +" << ((peakWs - baseWs) / kCcbMb) << " MB (peak " << (peakWs / kCcbMb)
       << " MB), private bytes peak +" << ((peakPrivate - basePrivate) / kCcbMb) << " MB (peak "
       << (peakPrivate / kCcbMb) << " MB)\n"
       << "  stages (s): decode " << (stages.m_decodeMs / kCcbMs) << ", draw " << (stages.m_drawMs / kCcbMs)
       << ", compose " << (stages.m_composeMs / kCcbMs) << ", video write " << (stages.m_videoWriteMs / kCcbMs)
       << ", audio " << (stages.m_audioMs / kCcbMs) << ", frames " << stages.m_videoFrames << " (reused canvas "
       << stages.m_reusedFrames << ", direct " << stages.m_directFrames << ")\n"
       << "  mp4 " << (mp4Bytes / kCcbMb) << " MB";
    //核对画面内容：录像不足 kCcbCheckSeconds 秒时不核对
    double meanDiff = -1.0;
    int64_t checkHns = 0;
    if (result == kCloudComposeOk && seconds > kCcbCheckSeconds)
    {
        std::thread checker(&ccbCheckOutput, std::cref(mp4Path), staticContent, static_cast<int>(clip.size()),
                            std::ref(meanDiff), std::ref(checkHns));
        checker.join();
        os << "\n  output frame at " << (checkHns / static_cast<double>(kCcbHnsPerSecond)) << " s, mean abs diff "
           << meanDiff << " (limit " << kCcbMaxMeanDiff << ")";
    }
    WARN(os.str());
    CHECK(done);
    CHECK(result == kCloudComposeOk);
    CHECK(mp4Bytes > 0);
    if (seconds > kCcbCheckSeconds)
    {
        CHECK(meanDiff >= 0.0);
        CHECK(meanDiff < kCcbMaxMeanDiff);
    }
    ::DeleteFileW(frecPath.c_str());
    ::DeleteFileW(mp4Path.c_str());
}
