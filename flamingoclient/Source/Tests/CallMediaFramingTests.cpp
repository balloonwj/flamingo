/**
 *  通话中转媒体复用 / 分片单元测试（Suite CMF），CallMediaFramingTests.cpp
 *  覆盖：
 *    - 音频封装→解复用 round-trip（含时间戳）；
 *    - 视频整帧(≤分片)封装→解复用 round-trip；
 *    - 视频大帧分片→逐片解复用重组还原原帧；中间片返回 none、末片返回完整帧；
 *    - 音频帧夹在视频分片之间：音频即时交付、视频仍正确重组（互不干扰）；
 *    - 畸形 body（过短 / 非法标志）安全返回 error；
 *    - 时间戳 round-trip：音频 / 视频整帧 / 视频分片重组后都带出正确时间戳。
 *  框架 Catch2 v3。纯逻辑，不触及 socket/设备。
 *  balloonwj@qq.com   2026-06-16
 */
#include "catch_amalgamated.hpp"
#include "../Call/CallMediaFraming.h"

#include <vector>
#include <stdint.h>

using namespace call;

namespace {

//body 头部字节数（与实现一致：[kind][chunk][ts(8)]）。
static const size_t kHdr = 10;

//造一段确定性字节序列（值随下标变化，便于逐字节比对）。
static std::vector<uint8_t> makeBytes(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>((i * 31 + seed) & 0xFF);
    return v;
}

} // namespace

TEST_CASE("CMF1 音频封装→解复用 round-trip")
{
    std::vector<uint8_t> opus = makeBytes(80, 7);
    std::vector<uint8_t> body;
    BuildAudioBody(opus.data(), opus.size(), 12345, body);
    CHECK(body.size() == opus.size() + kHdr);

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    CHECK(demux.Feed(body.data(), body.size(), out, ts) == CallMediaDemux::call_demux_audio);
    CHECK(out == opus);
    CHECK(ts == 12345u);
}

TEST_CASE("CMF2 视频整帧(≤分片)封装→解复用 round-trip")
{
    std::vector<uint8_t> frame = makeBytes(200, 3);
    std::vector<std::vector<uint8_t> > bodies;
    BuildVideoBodies(frame.data(), frame.size(), 512, 999, bodies);
    REQUIRE(bodies.size() == 1);   //≤ chunkSize → 单个 whole

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    CHECK(demux.Feed(bodies[0].data(), bodies[0].size(), out, ts) == CallMediaDemux::call_demux_video);
    CHECK(out == frame);
    CHECK(ts == 999u);
}

TEST_CASE("CMF3 视频大帧分片→重组还原")
{
    std::vector<uint8_t> frame = makeBytes(1000, 11);
    std::vector<std::vector<uint8_t> > bodies;
    BuildVideoBodies(frame.data(), frame.size(), 300, 0, bodies);
    REQUIRE(bodies.size() == 4);   //300+300+300+100

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    //前 3 片返回 none
    for (int i = 0; i < 3; ++i)
        CHECK(demux.Feed(bodies[i].data(), bodies[i].size(), out, ts) == CallMediaDemux::call_demux_none);
    //末片返回完整帧
    CHECK(demux.Feed(bodies[3].data(), bodies[3].size(), out, ts) == CallMediaDemux::call_demux_video);
    CHECK(out == frame);
}

TEST_CASE("CMF4 音频帧夹在视频分片之间：互不干扰")
{
    std::vector<uint8_t> frame = makeBytes(700, 5);
    std::vector<std::vector<uint8_t> > vbodies;
    BuildVideoBodies(frame.data(), frame.size(), 300, 4000, vbodies);
    REQUIRE(vbodies.size() == 3);   //300+300+100 (more,more,last)

    std::vector<uint8_t> opus = makeBytes(50, 9);
    std::vector<uint8_t> abody;
    BuildAudioBody(opus.data(), opus.size(), 4001, abody);

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;

    //视频片1(more)
    CHECK(demux.Feed(vbodies[0].data(), vbodies[0].size(), out, ts) == CallMediaDemux::call_demux_none);
    //中间插一个完整音频帧——应即时交付，且不破坏视频重组
    CHECK(demux.Feed(abody.data(), abody.size(), out, ts) == CallMediaDemux::call_demux_audio);
    CHECK(out == opus);
    CHECK(ts == 4001u);
    //视频片2(more) + 片3(last)
    CHECK(demux.Feed(vbodies[1].data(), vbodies[1].size(), out, ts) == CallMediaDemux::call_demux_none);
    CHECK(demux.Feed(vbodies[2].data(), vbodies[2].size(), out, ts) == CallMediaDemux::call_demux_video);
    CHECK(out == frame);
    CHECK(ts == 4000u);   //视频帧时间戳取同帧各片的戳（相同）
}

TEST_CASE("CMF5 畸形 body 安全返回 error")
{
    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;

    //过短（< 10 字节头）
    uint8_t tooShort[5] = { 1, 0, 0, 0, 0 };
    CHECK(demux.Feed(tooShort, 5, out, ts) == CallMediaDemux::call_demux_error);
    CHECK(demux.Feed(NULL, 0, out, ts) == CallMediaDemux::call_demux_error);

    //未知媒体类型（头够长，kind 非法）
    uint8_t badKind[kHdr + 1] = { 99, 0, 0,0,0,0,0,0,0,0, 0x55 };
    CHECK(demux.Feed(badKind, sizeof(badKind), out, ts) == CallMediaDemux::call_demux_error);

    //视频非法分片标志（头够长，chunk 非法）
    uint8_t badChunk[kHdr + 1] = { (uint8_t)call_media_kind_video, 77, 0,0,0,0,0,0,0,0, 0x55 };
    CHECK(demux.Feed(badChunk, sizeof(badChunk), out, ts) == CallMediaDemux::call_demux_error);
}

TEST_CASE("CMF6 边界：帧长恰等于分片大小→单个 whole；空帧")
{
    //恰等于 chunkSize：仍是单个 whole（条件 len <= chunkSize）
    std::vector<uint8_t> frame = makeBytes(256, 1);
    std::vector<std::vector<uint8_t> > bodies;
    BuildVideoBodies(frame.data(), frame.size(), 256, 7, bodies);
    CHECK(bodies.size() == 1);

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    CHECK(demux.Feed(bodies[0].data(), bodies[0].size(), out, ts) == CallMediaDemux::call_demux_video);
    CHECK(out == frame);
    CHECK(ts == 7u);

    //空帧：单个 whole、负载为空
    std::vector<std::vector<uint8_t> > empties;
    BuildVideoBodies(NULL, 0, 256, 8, empties);
    REQUIRE(empties.size() == 1);
    std::vector<uint8_t> out2;
    uint64_t ts2 = 0;
    CHECK(demux.Feed(empties[0].data(), empties[0].size(), out2, ts2) == CallMediaDemux::call_demux_video);
    CHECK(out2.empty());
    CHECK(ts2 == 8u);
}

TEST_CASE("CMF7 时间戳大值 round-trip（64 位不截断）")
{
    //取一个超过 32 位的大时间戳，验证 8 字节小端编解码不丢高位
    uint64_t bigTs = 0x0000000123456789ULL;
    std::vector<uint8_t> opus = makeBytes(40, 2);
    std::vector<uint8_t> abody;
    BuildAudioBody(opus.data(), opus.size(), bigTs, abody);

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    CHECK(demux.Feed(abody.data(), abody.size(), out, ts) == CallMediaDemux::call_demux_audio);
    CHECK(ts == bigTs);
}
