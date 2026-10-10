/**
 *  本地视频回环（调试入口）实现，VideoLoopbackDebug.cpp
 *  balloonwj@qq.com   2026-06-16
 */
#include "VideoLoopbackDebug.h"
#include "CallVideoEngine.h"
#include "VideoFrameConvert.h"

#include <windows.h>
#include <tchar.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

#include <vector>
#include <stdio.h>
#include <string.h>

namespace call {

//回环运行时长（毫秒）：足够采集足够多帧越过解码器启动延迟、走完编解码并落首帧。
static const DWORD kRunMs = 6000;

//把 BGRA(自上而下)写成 32 位 BMP（诊断用）。
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

//回环 sink：把本端编码帧回灌给引擎解码；解码出的首帧落 BMP。
class VideoLoopbackSink : public CallVideoEngineSink
{
public:
    VideoLoopbackSink()
        : m_engine(NULL)
        , m_encodedCount(0)
        , m_decodedCount(0)
        , m_encodedAtFirstDecode(-1)
    {
    }

    CallVideoEngine* m_engine;          //回灌目标引擎（不拥有）
    int              m_encodedCount;
    int              m_decodedCount;
    int              m_encodedAtFirstDecode;   //首帧解码时已编码的帧数 = 解码器启动延迟(帧)

    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        ++m_encodedCount;
        if (m_encodedCount <= 3)
            LOG_INFO("VideoLoopback: encoded #%d len=%u key=%d.", m_encodedCount, (unsigned)len, keyframe ? 1 : 0);
        //回灌：把刚编码的帧喂回引擎解码（本机闭环）。本回调在采集线程，解码也在采集线程。
        if (m_engine != NULL)
            m_engine->OnRemoteEncodedVideo(data, len, ptsHns);
    }

    virtual void OnLocalVideoFrame(const uint8_t* /*nv12*/, int /*width*/, int /*height*/, int /*stride*/) override
    {
        //本地预览回调：纯回环测试不关心本地预览，空实现即可
    }

    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override
    {
        if (m_decodedCount == 0)
        {
            m_encodedAtFirstDecode = m_encodedCount;   //首帧解码时已编码帧数 = 启动延迟(帧)
            LOG_INFO("VideoLoopback: first decoded frame %dx%d stride=%d, 启动延迟=%d 帧.",
                     width, height, stride, m_encodedAtFirstDecode);
            std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
            Nv12ToBgra(nv12, width, height, stride, bgra.data(), width * 4);
            WriteBmp32("E:\\flamingo\\videoloop_decoded.bmp", bgra.data(), width, height);
            LOG_INFO("VideoLoopback: dumped decoded frame to E:\\flamingo\\videoloop_decoded.bmp.");
        }
        ++m_decodedCount;
    }
};

bool RunVideoLoopbackDebug()
{
    VideoLoopbackSink sink;
    CallVideoEngine engine;
    sink.m_engine = &engine;

    if (!engine.Start(&sink, 30, 2000000))
    {
        ::MessageBox(NULL, _T("视频回环启动失败：摄像头打开或编解码初始化失败，详见日志。"),
                     _T("视频回环"), MB_OK | MB_ICONWARNING);
        return false;
    }

    //让采集 / 编码 / 解码在引擎线程上跑一会儿（首帧会被落成 BMP）
    ::Sleep(kRunMs);
    engine.Stop();

    bool got = (sink.m_decodedCount > 0);
    LOG_INFO("VideoLoopback: done, encoded=%d decoded=%d, result=%s.",
             sink.m_encodedCount, sink.m_decodedCount, got ? "PASS" : "FAIL");

    //结果落盘（供离线读取对比解码器启动延迟）
    FILE* rf = fopen("E:\\flamingo\\videoloop_result.txt", "w");
    if (rf != NULL)
    {
        fprintf(rf, "encoded=%d\ndecoded=%d\nstartup_latency_frames=%d\n",
                sink.m_encodedCount, sink.m_decodedCount, sink.m_encodedAtFirstDecode);
        fclose(rf);
    }

    if (got)
        ::MessageBox(NULL,
                     _T("视频回环通过：采集→H.264 编码→解码→显示 整条管线已跑通，首个解码帧写到 E:\\flamingo\\videoloop_decoded.bmp（画面内容取决于摄像头）。"),
                     _T("视频回环"), MB_OK | MB_ICONINFORMATION);
    else
        ::MessageBox(NULL, _T("视频回环失败：未解出帧，详见日志。"),
                     _T("视频回环"), MB_OK | MB_ICONWARNING);
    return got;
}

} // namespace call
