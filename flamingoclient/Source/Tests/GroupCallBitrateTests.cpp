/**
 *  群通话摄像头码率自适应的单元测试（Suite GCB，标签 [groupcall]），GroupCallBitrateTests.cpp
 *  群通话最多 9 路摄像头，每个人要收其余人的画面。开摄像头的人越多，每一路的码率越低，下行总量才不会随人数线性上涨
 *  （docs/群音视频通话方案.md 第 5 期）。覆盖：
 *    - GCB1 码率策略：1~2 路 400 kbps，3~4 路 300 kbps，5 路及以上 200 kbps；0 与负数按 1 路处理；
 *    - GCB2 开着摄像头的人数：宫格与小条里已加入、开着摄像头的都算，振铃中的不算；
 *    - GCB3 摄像头发送端：目标码率在编码器创建时生效；运行中改目标码率，下一帧编码前交给编码器；不大于 0 时回到默认码率。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <mfapi.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoMedia/GroupCall/GroupCallLayout.h"
#include "../../FlamingoMedia/Share/ShareCameraSender.h"
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"

#pragma comment(lib, "mfplat.lib")

namespace {

//合成画面的尺寸与亮度（NV12）
const int kGcbSrcW = 1280;
const int kGcbSrcH = 720;
const uint8_t kGcbLuma = 120;
const uint8_t kGcbChroma = 128;
//测试用的目标码率（bit/s）
const int kGcbLowBitrate = 250000;
const int kGcbOtherBitrate = 180000;

//初始化本线程的 COM 与 Media Foundation（编码器在调用 FeedFrame 的线程上创建）
class GcbMfScope
{
public:
    GcbMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~GcbMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//丢弃发出的包体，只计数
class GcbTransport : public screenshare::ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_count++;
        return true;
    }

    size_t Count()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_count;
    }

private:
    std::mutex m_mutex;      //保护 m_count
    size_t     m_count = 0;  //发出的包体个数
};

std::vector<uint8_t> gcbFrame()
{
    std::vector<uint8_t> f(static_cast<size_t>(kGcbSrcW) * kGcbSrcH * 3 / 2, kGcbLuma);
    for (size_t i = static_cast<size_t>(kGcbSrcW) * kGcbSrcH; i < f.size(); ++i)
        f[i] = kGcbChroma;
    return f;
}

groupcall::GroupCallTile gcbTile(int64_t uid, bool camera, bool ringing)
{
    groupcall::GroupCallTile t;
    t.m_userId = uid;
    t.m_seat = ringing ? 0 : static_cast<int>(uid % 100);
    t.m_camera = camera;
    t.m_ringing = ringing;
    return t;
}

} // namespace

TEST_CASE("GCB1 群通话摄像头码率随开摄像头的人数降低", "[groupcall]")
{
    using groupcall::GroupCallCameraBitrateFor;
    CHECK(GroupCallCameraBitrateFor(0) == 400000);
    CHECK(GroupCallCameraBitrateFor(-1) == 400000);
    CHECK(GroupCallCameraBitrateFor(1) == 400000);
    CHECK(GroupCallCameraBitrateFor(2) == 400000);
    CHECK(GroupCallCameraBitrateFor(3) == 300000);
    CHECK(GroupCallCameraBitrateFor(4) == 300000);
    CHECK(GroupCallCameraBitrateFor(5) == 200000);
    CHECK(GroupCallCameraBitrateFor(9) == 200000);
    CHECK(GroupCallCameraBitrateFor(16) == 200000);
    //一路时与屏幕共享的摄像头码率相同
    CHECK(GroupCallCameraBitrateFor(1) == screenshare::kShareCameraBitrateBps);
}

TEST_CASE("GCB2 开着摄像头的人数：宫格与小条都算，振铃中的不算", "[groupcall]")
{
    std::vector<groupcall::GroupCallTile> grid;
    std::vector<groupcall::GroupCallTile> strip;
    CHECK(groupcall::CountGroupCallCameras(grid, strip) == 0);
    grid.push_back(gcbTile(5301, true, false));
    grid.push_back(gcbTile(5302, false, false));
    grid.push_back(gcbTile(5303, true, false));
    strip.push_back(gcbTile(5304, true, false));
    strip.push_back(gcbTile(5305, true, true));
    CHECK(groupcall::CountGroupCallCameras(grid, strip) == 3);
}

TEST_CASE("GCB3 摄像头发送端按目标码率编码", "[groupcall]")
{
    GcbMfScope mf;
    GcbTransport transport;
    screenshare::ShareCameraSender sender;
    //还没有编码器
    CHECK(sender.AppliedBitrate() == 0);
    sender.SetTargetBitrate(kGcbLowBitrate);
    REQUIRE(sender.StartWithoutCamera(&transport, 3));
    const std::vector<uint8_t> f = gcbFrame();
    sender.FeedFrame(&f[0], kGcbSrcW, kGcbSrcH, kGcbSrcW);
    //编码器创建时就用目标码率
    CHECK(sender.AppliedBitrate() == kGcbLowBitrate);
    CHECK(transport.Count() > 0);

    //运行中改：下一帧编码前生效
    sender.SetTargetBitrate(kGcbOtherBitrate);
    sender.FeedFrame(&f[0], kGcbSrcW, kGcbSrcH, kGcbSrcW);
    CHECK(sender.AppliedBitrate() == kGcbOtherBitrate);

    //不大于 0：回到默认码率
    sender.SetTargetBitrate(0);
    sender.FeedFrame(&f[0], kGcbSrcW, kGcbSrcH, kGcbSrcW);
    CHECK(sender.AppliedBitrate() == screenshare::kShareCameraBitrateBps);
    sender.Stop();
}
