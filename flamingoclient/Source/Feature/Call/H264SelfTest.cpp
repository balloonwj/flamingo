/**
 *  H.264 编解码自测（调试入口）实现，H264SelfTest.cpp
 *  balloonwj@qq.com   2026-06-15
 */
#define _CRT_SECURE_NO_WARNINGS
#include "H264SelfTest.h"
#include "MfH264Codec.h"
#include "VideoFrameConvert.h"

#include <windows.h>
#include <tchar.h>
#include <mfapi.h>

#include "../../Common/Log/IULog.h"   //须在 windows.h 之后

#include <vector>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "mfplat.lib")

namespace call {

//自测画面尺寸 / 帧率 / 码率。
static const int kTestW = 640;
static const int kTestH = 480;
static const int kTestFps = 30;
static const int kTestBitrate = 1500000;
//最多喂多少帧直到解出一帧（编/解码器有少量缓冲，几帧内即可出帧）。
static const int kMaxFeedFrames = 60;

//把 BGRA(自上而下)写成 32 位 BMP（仅诊断用）。
static void WriteBmp32(const char* path, const uint8_t* bgra, int w, int h)
{
    if (bgra == NULL || w <= 0 || h <= 0)
        return;
    FILE* f = fopen(path, "wb");
    if (f == NULL)
        return;
    int imgSize = w * h * 4;
    int fileSize = 54 + imgSize;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    *reinterpret_cast<int*>(&hdr[2]) = fileSize;
    *reinterpret_cast<int*>(&hdr[10]) = 54;
    *reinterpret_cast<int*>(&hdr[14]) = 40;
    *reinterpret_cast<int*>(&hdr[18]) = w;
    *reinterpret_cast<int*>(&hdr[22]) = -h;
    *reinterpret_cast<short*>(&hdr[26]) = 1;
    *reinterpret_cast<short*>(&hdr[28]) = 32;
    *reinterpret_cast<int*>(&hdr[34]) = imgSize;
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(bgra, 1, imgSize, f);
    fclose(f);
}

//造一帧合成 NV12：Y 随 x 从 16(暗)线性升到 235(亮)的灰度渐变，UV 全 128(无色)。
static std::vector<uint8_t> makeGradientNv12(int w, int h)
{
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 3 / 2);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
            buf[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(16 + 219 * x / (w - 1));
    }
    uint8_t* uv = buf.data() + static_cast<size_t>(w) * h;
    memset(uv, 128, static_cast<size_t>(w) * h / 2);
    return buf;
}

//自测编排：编码输出回灌解码；解码首帧转 BGRA 落 BMP。
class H264SelfTestRunner : public MfH264EncoderSink, public MfH264DecoderSink
{
public:
    H264SelfTestRunner()
        : m_decoder(NULL)
        , m_decodedCount(0)
        , m_encodedCount(0)
    {
    }

    MfH264Decoder* m_decoder;     //编码包回灌的解码器（不拥有）
    int            m_decodedCount; //已解出帧数
    int            m_encodedCount; //已编出包数

    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        ++m_encodedCount;
        if (m_encodedCount <= 3)
            LOG_INFO("H264SelfTest: encoded packet #%d len=%u key=%d.", m_encodedCount, (unsigned)len, keyframe ? 1 : 0);
        if (m_decoder != NULL)
            m_decoder->DecodePacket(data, len, ptsHns, this);
    }

    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t /*ptsHns*/) override
    {
        if (m_decodedCount == 0)
        {
            LOG_INFO("H264SelfTest: first decoded frame %dx%d stride=%d.", width, height, stride);
            std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
            Nv12ToBgra(nv12, width, height, stride, bgra.data(), width * 4);
            WriteBmp32("E:\\flamingo\\h264_selftest_decoded.bmp", bgra.data(), width, height);
            LOG_INFO("H264SelfTest: dumped decoded frame to E:\\flamingo\\h264_selftest_decoded.bmp.");
        }
        ++m_decodedCount;
    }
};

bool RunH264SelfTest()
{
    HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
    bool mfInited = SUCCEEDED(hr);

    bool got = false;
    do
    {
        MfH264Encoder encoder;
        MfH264Decoder decoder;
        if (!encoder.Init(kTestW, kTestH, kTestFps, kTestBitrate) || !decoder.Init(kTestW, kTestH))
        {
            LOG_ERROR("H264SelfTest: encoder/decoder init failed.");
            break;
        }

        std::vector<uint8_t> nv12 = makeGradientNv12(kTestW, kTestH);

        H264SelfTestRunner runner;
        runner.m_decoder = &decoder;

        for (int f = 0; f < kMaxFeedFrames && runner.m_decodedCount < 1; ++f)
        {
            int64_t pts = static_cast<int64_t>(f) * (10000000LL / kTestFps);
            encoder.EncodeNv12(nv12.data(), kTestW, pts, &runner);
        }

        got = (runner.m_decodedCount > 0);
        LOG_INFO("H264SelfTest: done, encoded=%d decoded=%d, result=%s.",
                 runner.m_encodedCount, runner.m_decodedCount, got ? "PASS" : "FAIL");
    } while (false);

    if (mfInited)
        ::MFShutdown();

    if (got)
        ::MessageBox(NULL,
                     _T("H.264 编解码自测通过：已把解码出的首帧写到 E:\\flamingo\\h264_selftest_decoded.bmp，可打开核对（应为左暗右亮的灰度渐变）。"),
                     _T("H264 自测"), MB_OK | MB_ICONINFORMATION);
    else
        ::MessageBox(NULL, _T("H.264 编解码自测失败，详见日志。"),
                     _T("H264 自测"), MB_OK | MB_ICONWARNING);
    return got;
}

} // namespace call
