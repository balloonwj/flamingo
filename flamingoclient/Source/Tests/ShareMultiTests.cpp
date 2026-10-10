/**
 *  屏幕共享多人同时共享的客户端单元测试（Suite SSMC，标签 [sharemulti]），ShareMultiTests.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 2 节第 27～30 条与第 8 节第 22 期的客户端部分：
 *    - SSMC1 协议：1725 成员项的共享字段与人数上限（老服务端不带时视为只有主持人在共享）、事件 13 / 14，1737 / 1739 的请求，1738 的解析；
 *    - SSMC2 画面清单：从房间状态取出正在共享的人与画面座位，主持人排第一、其余按画面座位；按画面座位查找；
 *    - SSMC3 多画面布局：1 块铺满、2 块左右平分、3～4 块田字格，放大一块时主画面在上、其余缩成底部居中的一排小图；点中哪一块；
 *    - SSMC4 多路解码：两个座位的屏幕画面各自解码、各存最新一帧，源状态按座位记，移除一路不影响另一路；
 *    - SSMC5 主持人退出时可移交的人（除自己以外正在共享的人）；
 *    - SSMC6 本人要看的画面（去掉自己的那一块）；
 *    - SSMC7 本人作为共享者的阶段按快照校正，「共享我的屏幕」按钮按阶段显示；
 *    - SSMC8 主持人一侧的共享申请队列；
 *    - SSMC9 观看窗画面控件选矩形：控制键鼠按被控制的那一块换算；
 *    - SSMC10 白板（第 23 期）的协议：快照里的白板与「申请白板中」、1738 的 kind 与座位、1737 带 kind；
 *    - SSMC11 白板进画面清单（排在屏幕之后）、占共享名额、不是可接任主持人的人；按白板的申请队列。
 *  SSMC4 用真实的 H.264 编码器造测试数据（与 ShareBitrateTests 相同的做法），其余为纯逻辑。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharemulti]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <mfapi.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Call/MfH264Codec.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareScreenDecoders.h"
#include "../../FlamingoMedia/Share/ShareScreens.h"
#include "../../Source/net/Protocol.h"

using namespace screenshare;

namespace {

//初始化本线程的 COM 与 Media Foundation（编码、解码要用）
class SsmcMfScope
{
public:
    SsmcMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~SsmcMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//收集编码输出的 H.264 包
class SsmcEncoded : public call::MfH264EncoderSink
{
public:
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)ptsHns;
        m_packets.push_back(std::string(reinterpret_cast<const char*>(data), len));
        m_keys.push_back(keyframe);
    }

    std::vector<std::string> m_packets;   //编码输出，按先后
    std::vector<bool>        m_keys;      //各包是否关键帧
};

//一帧纯色 NV12（亮度 y，色度 u、v）
std::vector<uint8_t> ssmcNv12(int w, int h, uint8_t y, uint8_t u, uint8_t v)
{
    std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
        nv12[i] = y;
    for (size_t i = static_cast<size_t>(w) * h; i + 1 < nv12.size(); i += 2)
    {
        nv12[i] = u;
        nv12[i + 1] = v;
    }
    return nv12;
}

//把一个 H.264 包组成某个座位发来的屏幕画面帧
ShareMediaFrame ssmcScreenFrame(int seat, int w, int h, const std::string& h264, bool key, uint64_t ts)
{
    ShareMediaFrame f;
    f.m_track = screenshare_track_screen;
    f.m_senderSeat = seat;
    f.m_keyframe = key;
    f.m_timestamp = ts;
    f.m_data = ShareFraming::buildVideoPayload(w, h, h264.data(), h264.size());
    return f;
}

//矩形各边是否等于期望值
bool ssmcRect(const RECT& r, int l, int t, int rr, int b)
{
    return r.left == l && r.top == t && r.right == rr && r.bottom == b;
}

//一个成员
ShareMember ssmcMember(int64_t uid, int seat, bool sharing, int screenSeat)
{
    ShareMember m;
    m.m_userId = uid;
    m.m_seat = seat;
    m.m_sharing = sharing;
    m.m_screenSeat = screenSeat;
    return m;
}

} // namespace

TEST_CASE("SSMC1 协议：成员的共享字段与人数上限、老服务端兼容、事件 13 / 14、1737 / 1739 请求、1738 解析", "[sharemulti]")
{
    ShareStateNotify s;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":13,\"actor\":\"2001\",\"host\":\"2001\",\"hostonline\":1,\"annotateallowed\":1,"
        "\"recordallowed\":1,\"maxsharers\":4,\"target\":\"2002\",\"members\":["
        "{\"uid\":\"2001\",\"seat\":0,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0,\"sharing\":1,\"screenseat\":0},"
        "{\"uid\":\"2002\",\"seat\":1,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0,\"sharing\":1,\"screenseat\":4},"
        "{\"uid\":\"2003\",\"seat\":2,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0,\"sharing\":0,\"requesting\":1}]}",
        s));
    CHECK(s.m_event == share_event_sharing_changed);
    CHECK(s.m_maxSharers == 4);
    CHECK(s.m_target == 2002);
    REQUIRE(s.m_members.size() == 3);
    CHECK(s.m_members[0].m_sharing);
    CHECK(s.m_members[0].m_screenSeat == 0);
    CHECK(s.m_members[1].m_sharing);
    CHECK(s.m_members[1].m_screenSeat == 4);
    CHECK_FALSE(s.m_members[1].m_requesting);
    CHECK_FALSE(s.m_members[2].m_sharing);
    CHECK(s.m_members[2].m_screenSeat == -1);
    CHECK(s.m_members[2].m_requesting);

    //老服务端不带共享字段：视为只有主持人在共享，用主持人的座位；人数上限为 1
    ShareStateNotify old;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":1,\"actor\":\"2002\",\"host\":\"2001\",\"hostonline\":1,\"annotateallowed\":1,"
        "\"recordallowed\":1,\"members\":[{\"uid\":\"2001\",\"seat\":0,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0},"
        "{\"uid\":\"2002\",\"seat\":1,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0}]}",
        old));
    CHECK(old.m_maxSharers == 1);
    CHECK(old.m_members[0].m_sharing);
    CHECK(old.m_members[0].m_screenSeat == 0);
    CHECK_FALSE(old.m_members[1].m_sharing);
    //事件 14：主持人移交
    ShareStateNotify moved;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":14,\"actor\":\"2001\",\"host\":\"2002\",\"hostonline\":1,\"annotateallowed\":1,"
        "\"recordallowed\":1,\"maxsharers\":4,\"target\":\"2002\",\"members\":["
        "{\"uid\":\"2002\",\"seat\":1,\"mic\":0,\"camera\":0,\"sysaudio\":0,\"recording\":0,\"sharing\":1,\"screenseat\":4}]}",
        moved));
    CHECK(moved.m_event == share_event_host_changed);
    CHECK(moved.m_hostId == 2002);

    //1737：申请不带对象；同意带对象
    CHECK(ShareProtocolClient::buildSharing("ss-1", share_sharing_request, 0) == "{\"action\":1,\"roomid\":\"ss-1\"}");
    CHECK(ShareProtocolClient::buildSharing("ss-1", share_sharing_approve, 2002) ==
          "{\"action\":3,\"roomid\":\"ss-1\",\"target\":\"2002\"}");
    //1739
    CHECK(ShareProtocolClient::buildTransferHost("ss-1", 2002) == "{\"roomid\":\"ss-1\",\"target\":\"2002\"}");

    //1738：同意带中转字段
    ShareSharingNotify n;
    REQUIRE(ShareProtocolClient::parseSharingNotify(
        "{\"roomid\":\"ss-1\",\"action\":3,\"actor\":\"2001\",\"actorname\":\"host\",\"token\":\"tok\",\"relayip\":\"1.2.3.4\","
        "\"relayport\":20003,\"screenseat\":4}",
        n));
    CHECK(n.m_roomId == "ss-1");
    CHECK(n.m_action == share_sharing_approve);
    CHECK(n.m_actor == 2001);
    CHECK(n.m_actorName == "host");
    CHECK(n.HasPublishInfo());
    CHECK(n.m_token == "tok");
    CHECK(n.m_relayIp == "1.2.3.4");
    CHECK(n.m_relayPort == 20003);
    CHECK(n.m_screenSeat == 4);
    ShareSharingNotify r;
    REQUIRE(ShareProtocolClient::parseSharingNotify("{\"roomid\":\"ss-1\",\"action\":1,\"actor\":\"2003\",\"actorname\":\"c\"}",
                                                    r));
    CHECK(r.m_action == share_sharing_request);
    CHECK_FALSE(r.HasPublishInfo());
    CHECK_FALSE(ShareProtocolClient::parseSharingNotify("{\"roomid\":\"ss-1\",\"action\":9,\"actor\":\"1\"}", r));
    CHECK_FALSE(ShareProtocolClient::parseSharingNotify("{\"action\":1,\"actor\":\"1\"}", r));
}

TEST_CASE("SSMC2 画面清单：主持人排第一、其余按画面座位；按画面座位查找", "[sharemulti]")
{
    ShareStateNotify s;
    s.m_hostId = 2002;
    s.m_members.push_back(ssmcMember(2001, 0, true, 5));     //原主持人已移交，现在是普通共享者
    s.m_members.push_back(ssmcMember(2002, 1, true, 1));     //主持人：用自己的座位
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    s.m_members.push_back(ssmcMember(2004, 3, true, 4));
    const std::vector<ShareScreenInfo> screens = CollectShareScreens(s);
    REQUIRE(screens.size() == 3);
    CHECK(screens[0].m_ownerId == 2002);
    CHECK(screens[0].m_isHost);
    CHECK(screens[0].m_screenSeat == 1);
    CHECK(screens[0].m_ownerSeat == 1);
    CHECK(screens[1].m_ownerId == 2004);
    CHECK(screens[1].m_screenSeat == 4);
    CHECK_FALSE(screens[1].m_isHost);
    CHECK(screens[2].m_ownerId == 2001);
    CHECK(screens[2].m_screenSeat == 5);
    CHECK(FindScreenBySeat(screens, 4) == 1);
    CHECK(FindScreenBySeat(screens, 5) == 2);
    CHECK(FindScreenBySeat(screens, 2) == -1);
    //没人在共享（主持人停掉了自己的画面）
    ShareStateNotify none;
    none.m_hostId = 2001;
    none.m_members.push_back(ssmcMember(2001, 0, false, -1));
    CHECK(CollectShareScreens(none).empty());
}

TEST_CASE("SSMC3 多画面布局：1 块铺满、2 块左右、3～4 块田字格；放大一块时其余缩成底部居中的一排小图；点中哪一块", "[sharemulti]")
{
    const RECT area = { 0, 0, 1000, 600 };
    const int gap = 4;
    CHECK(ComputeScreenTiles(area, 0, -1, gap).empty());
    std::vector<RECT> t = ComputeScreenTiles(area, 1, -1, gap);
    REQUIRE(t.size() == 1);
    CHECK(ssmcRect(t[0], 0, 0, 1000, 600));
    t = ComputeScreenTiles(area, 2, -1, gap);
    REQUIRE(t.size() == 2);
    CHECK(ssmcRect(t[0], 0, 0, 498, 600));
    CHECK(ssmcRect(t[1], 502, 0, 1000, 600));
    t = ComputeScreenTiles(area, 3, -1, gap);
    REQUIRE(t.size() == 3);
    CHECK(ssmcRect(t[0], 0, 0, 498, 298));
    CHECK(ssmcRect(t[1], 502, 0, 1000, 298));
    CHECK(ssmcRect(t[2], 0, 302, 498, 600));
    t = ComputeScreenTiles(area, 4, -1, gap);
    REQUIRE(t.size() == 4);
    CHECK(ssmcRect(t[3], 502, 302, 1000, 600));
    //放大第 2 块（下标 1）：主画面在上，底部一排小图高为区域的 kScreenStripPercent%，宽按 16:9 且不超过平分，整排居中
    t = ComputeScreenTiles(area, 3, 1, gap);
    REQUIRE(t.size() == 3);
    CHECK(ssmcRect(t[1], 0, 0, 1000, 476));
    CHECK(ssmcRect(t[0], 285, 480, 498, 600));
    CHECK(ssmcRect(t[2], 502, 480, 715, 600));
    //只有 1 块时放大没有意义：仍然铺满
    t = ComputeScreenTiles(area, 1, 0, gap);
    CHECK(ssmcRect(t[0], 0, 0, 1000, 600));
    //放大的下标越界：按不放大处理
    t = ComputeScreenTiles(area, 2, 5, gap);
    CHECK(ssmcRect(t[0], 0, 0, 498, 600));
    //点中
    t = ComputeScreenTiles(area, 4, -1, gap);
    POINT p1 = { 700, 100 };
    POINT p3 = { 700, 500 };
    POINT gapPt = { 500, 100 };
    CHECK(HitScreenTile(t, p1) == 1);
    CHECK(HitScreenTile(t, p3) == 3);
    CHECK(HitScreenTile(t, gapPt) == -1);
}

TEST_CASE("SSMC4 多路解码：两个座位的画面各自解码、各存最新一帧，源状态按座位记，移除一路不影响另一路", "[sharemulti]")
{
    SsmcMfScope mf;
    const int w = 320;
    const int h = 240;
    const int kFps = 15;
    const int kBitrate = 800000;
    //红色与蓝色两路（BT.601：红 Y=81 U=90 V=240，蓝 Y=41 U=240 V=110）
    call::MfH264Encoder red;
    call::MfH264Encoder blue;
    REQUIRE(red.Init(w, h, kFps, kBitrate));
    REQUIRE(blue.Init(w, h, kFps, kBitrate));
    SsmcEncoded redOut;
    SsmcEncoded blueOut;
    const std::vector<uint8_t> redNv12 = ssmcNv12(w, h, 81, 90, 240);
    const std::vector<uint8_t> blueNv12 = ssmcNv12(w, h, 41, 240, 110);
    const int64_t kFrameHns = 10000000LL / kFps;
    const int kFrames = 4;
    for (int i = 0; i < kFrames; ++i)
    {
        red.EncodeNv12(&redNv12[0], w, kFrameHns * i, &redOut);
        blue.EncodeNv12(&blueNv12[0], w, kFrameHns * i, &blueOut);
    }
    REQUIRE_FALSE(redOut.m_packets.empty());
    REQUIRE_FALSE(blueOut.m_packets.empty());

    ShareScreenDecoders d;
    CHECK(d.Seats().empty());
    for (size_t i = 0; i < redOut.m_packets.size(); ++i)
        d.OnScreenFrame(ssmcScreenFrame(0, w, h, redOut.m_packets[i], redOut.m_keys[i], i));
    for (size_t i = 0; i < blueOut.m_packets.size(); ++i)
        d.OnScreenFrame(ssmcScreenFrame(5, w, h, blueOut.m_packets[i], blueOut.m_keys[i], i));
    //不是屏幕画面轨的帧不理
    ShareMediaFrame camera = ssmcScreenFrame(7, w, h, redOut.m_packets[0], true, 0);
    camera.m_track = screenshare_track_camera;
    d.OnScreenFrame(camera);

    std::vector<int> seats = d.Seats();
    REQUIRE(seats.size() == 2);
    CHECK(seats[0] == 0);
    CHECK(seats[1] == 5);
    std::vector<uint8_t> bgra;
    int fw = 0;
    int fh = 0;
    uint64_t serial = 0;
    REQUIRE(d.GetLatestFrame(0, bgra, fw, fh, serial));
    CHECK(fw == w);
    CHECK(fh == h);
    const size_t mid = (static_cast<size_t>(h / 2) * w + w / 2) * 4;
    CHECK(bgra[mid + 2] > 180);   //红
    CHECK(bgra[mid + 0] < 80);
    REQUIRE(d.GetLatestFrame(5, bgra, fw, fh, serial));
    CHECK(bgra[mid + 0] > 180);   //蓝
    CHECK(bgra[mid + 2] < 80);
    CHECK_FALSE(d.GetLatestFrame(3, bgra, fw, fh, serial));
    CHECK(d.DecodedCount() > 0);

    //源状态按座位记，默认为正在共享
    CHECK(d.SourceState(5) == screenshare_source_sharing);
    d.SetSourceState(5, screenshare_source_paused);
    CHECK(d.SourceState(5) == screenshare_source_paused);
    CHECK(d.SourceState(0) == screenshare_source_sharing);
    //移除一路
    d.Remove(5);
    seats = d.Seats();
    REQUIRE(seats.size() == 1);
    CHECK(seats[0] == 0);
    CHECK(d.GetLatestFrame(0, bgra, fw, fh, serial));
    d.Clear();
    CHECK(d.Seats().empty());
}

TEST_CASE("SSMC5 主持人退出时可移交的人", "[sharemulti]")
{
    ShareStateNotify s;
    s.m_hostId = 2001;
    s.m_members.push_back(ssmcMember(2001, 0, true, 0));
    s.m_members.push_back(ssmcMember(2002, 1, true, 4));
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    s.m_members.push_back(ssmcMember(2004, 3, true, 5));
    s.m_members[2].m_requesting = true;
    //可移交的人：除自己以外正在共享的人，按画面座位
    std::vector<int64_t> c = HostTransferCandidates(s, 2001);
    REQUIRE(c.size() == 2);
    CHECK(c[0] == 2002);
    CHECK(c[1] == 2004);
    //没有别人在共享：没有可移交的人，主持人退出即结束
    ShareStateNotify alone;
    alone.m_hostId = 2001;
    alone.m_members.push_back(ssmcMember(2001, 0, true, 0));
    alone.m_members.push_back(ssmcMember(2003, 2, false, -1));
    CHECK(HostTransferCandidates(alone, 2001).empty());
}

TEST_CASE("SSMC6 本人要看的画面去掉自己的那一块", "[sharemulti]")
{
    ShareStateNotify s;
    s.m_hostId = 2001;
    s.m_members.push_back(ssmcMember(2001, 0, true, 0));
    s.m_members.push_back(ssmcMember(2002, 1, true, 4));
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    s.m_members.push_back(ssmcMember(2004, 3, true, 5));
    //共享者 2002 看到主持人与 2004，不看自己
    std::vector<ShareScreenInfo> v = CollectOtherScreens(s, 2002);
    REQUIRE(v.size() == 2);
    CHECK(v[0].m_ownerId == 2001);
    CHECK(v[1].m_ownerId == 2004);
    //主持人看到的是其他两位共享者
    v = CollectOtherScreens(s, 2001);
    REQUIRE(v.size() == 2);
    CHECK(v[0].m_screenSeat == 4);
    CHECK(v[1].m_screenSeat == 5);
    //没在共享的观看者看到全部三块
    CHECK(CollectOtherScreens(s, 2003).size() == 3);
    //主持人停掉自己的画面：观看者只看到两位共享者
    s.m_members[0].m_sharing = false;
    s.m_members[0].m_screenSeat = -1;
    CHECK(CollectOtherScreens(s, 2003).size() == 2);
}

TEST_CASE("SSMC7 共享者阶段按快照校正，「共享我的屏幕」按钮按阶段显示", "[sharemulti]")
{
    ShareStateNotify s;
    s.m_hostId = 2001;
    s.m_members.push_back(ssmcMember(2001, 0, true, 0));
    s.m_members.push_back(ssmcMember(2002, 1, true, 4));
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    //推流中、选择内容中：快照里仍在共享则不变
    CHECK(SyncPublishPhase(share_publish_streaming, s, 2002) == share_publish_streaming);
    CHECK(SyncPublishPhase(share_publish_picking, s, 2002) == share_publish_picking);
    //被主持人停掉：回到空闲
    CHECK(SyncPublishPhase(share_publish_streaming, s, 2003) == share_publish_idle);
    CHECK(SyncPublishPhase(share_publish_picking, s, 2003) == share_publish_idle);
    //已不在房间：回到空闲
    CHECK(SyncPublishPhase(share_publish_streaming, s, 9999) == share_publish_idle);
    //申请中不按快照判断（快照可能早于服务端处理申请）
    CHECK(SyncPublishPhase(share_publish_requested, s, 2003) == share_publish_requested);
    CHECK(SyncPublishPhase(share_publish_idle, s, 2002) == share_publish_idle);
    //按钮状态
    CHECK(ShareMineStateForPhase(share_publish_idle) == 0);
    CHECK(ShareMineStateForPhase(share_publish_requested) == 1);
    CHECK(ShareMineStateForPhase(share_publish_picking) == 2);
    CHECK(ShareMineStateForPhase(share_publish_streaming) == 2);
}

TEST_CASE("SSMC8 主持人一侧的共享申请队列：不重复、按到达顺序、撤回与快照清理", "[sharemulti]")
{
    ShareRequestQueue q;
    CHECK(q.FrontUser() == 0);
    CHECK(q.FrontName().empty());
    CHECK(q.Add(2002, L"B"));
    CHECK(q.Add(2003, L"C"));
    CHECK_FALSE(q.Add(2002, L"B2"));
    CHECK(q.Size() == 2);
    CHECK(q.FrontUser() == 2002);
    CHECK(q.FrontName() == L"B");
    //撤回
    CHECK(q.Remove(2002));
    CHECK_FALSE(q.Remove(2002));
    CHECK(q.FrontUser() == 2003);
    //快照清理：已离开的、已不在申请中的去掉，仍在申请的保留
    q.Add(2004, L"D");
    q.Add(2005, L"E");
    ShareStateNotify s;
    s.m_hostId = 2001;
    s.m_members.push_back(ssmcMember(2001, 0, true, 0));
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    s.m_members.push_back(ssmcMember(2005, 4, false, -1));
    s.m_members[1].m_requesting = false;   //2003 的申请已被处理
    s.m_members[2].m_requesting = true;    //2005 仍在申请
    const std::vector<int64_t> removed = q.Prune(s);
    REQUIRE(removed.size() == 2);
    CHECK(removed[0] == 2003);
    CHECK(removed[1] == 2004);
    CHECK(q.Size() == 1);
    CHECK(q.FrontUser() == 2005);
    q.Clear();
    CHECK(q.Size() == 0);
}

TEST_CASE("SSMC9 控制键鼠按被控制的那块画面的矩形换算", "[sharemulti]")
{
    std::vector<int> seats;
    seats.push_back(0);
    seats.push_back(4);
    seats.push_back(5);
    std::vector<RECT> rects;
    const RECT r0 = { 0, 0, 100, 60 };
    const RECT r4 = { 110, 0, 210, 60 };
    const RECT r5 = { 0, 70, 100, 130 };
    rects.push_back(r0);
    rects.push_back(r4);
    rects.push_back(r5);
    std::vector<int> noSeats;
    std::vector<RECT> single(1, r4);
    //某块画面的矩形
    RECT out;
    REQUIRE(PickScreenRect(seats, rects, 4, out));
    CHECK(::EqualRect(&out, &r4));
    CHECK_FALSE(PickScreenRect(seats, rects, 7, out));
    CHECK(::IsRectEmpty(&out));
    //还没画过（空矩形）：找不到
    std::vector<RECT> blank(3);
    memset(&blank[0], 0, sizeof(RECT) * blank.size());
    CHECK_FALSE(PickScreenRect(seats, blank, 4, out));
    //没有清单：单画面就是那一块
    REQUIRE(PickScreenRect(noSeats, single, 0, out));
    CHECK(::EqualRect(&out, &r4));
}

TEST_CASE("SSMC10 白板的协议：快照里的白板与申请中、1738 的 kind 与座位、1737 带 kind", "[sharemulti]")
{
    ShareStateNotify s;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":13,\"actor\":\"2001\",\"host\":\"2001\",\"hostonline\":1,\"maxsharers\":4,"
        "\"members\":[{\"uid\":\"2001\",\"seat\":0,\"sharing\":1,\"screenseat\":0},"
        "{\"uid\":\"2002\",\"seat\":1,\"sharing\":0,\"boardrequesting\":1}],"
        "\"whiteboards\":[{\"seat\":5,\"owner\":\"2001\"},{\"seat\":-1,\"owner\":\"2002\"}]}", s));
    REQUIRE(s.m_whiteboards.size() == 1);
    CHECK(s.m_whiteboards[0].m_seat == 5);
    CHECK(s.m_whiteboards[0].m_ownerId == 2001);
    REQUIRE(s.m_members.size() == 2);
    CHECK_FALSE(s.m_members[0].m_boardRequesting);
    CHECK(s.m_members[1].m_boardRequesting);
    CHECK_FALSE(s.m_members[1].m_requesting);
    //老快照不带白板
    ShareStateNotify old;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":1,\"actor\":\"2001\",\"host\":\"2001\",\"members\":[]}", old));
    CHECK(old.m_whiteboards.empty());

    //1738：白板的同意带座位、不带令牌
    ShareSharingNotify n;
    REQUIRE(ShareProtocolClient::parseSharingNotify(
        "{\"roomid\":\"ss-1\",\"kind\":2,\"action\":3,\"actor\":\"2001\",\"actorname\":\"z\",\"screenseat\":5}", n));
    CHECK(n.m_kind == share_kind_whiteboard);
    CHECK(n.m_screenSeat == 5);
    CHECK_FALSE(n.HasPublishInfo());
    REQUIRE(ShareProtocolClient::parseSharingNotify("{\"roomid\":\"ss-1\",\"action\":4,\"actor\":\"2001\"}", n));
    CHECK(n.m_kind == share_kind_screen);

    //1737：白板带 kind，屏幕不带
    CHECK(ShareProtocolClient::buildSharing("ss-1", share_sharing_request, 0, share_kind_whiteboard).find("\"kind\":2") !=
          std::string::npos);
    CHECK(ShareProtocolClient::buildSharing("ss-1", share_sharing_request, 0).find("kind") == std::string::npos);
}

TEST_CASE("SSMC11 白板进画面清单、占共享名额、不能接任主持人；按白板的申请队列", "[sharemulti]")
{
    ShareStateNotify s;
    s.m_hostId = 2001;
    s.m_members.push_back(ssmcMember(2001, 0, true, 0));
    s.m_members.push_back(ssmcMember(2002, 1, true, 4));
    s.m_members.push_back(ssmcMember(2003, 2, false, -1));
    ShareWhiteboardInfo b1;
    b1.m_seat = 6;
    b1.m_ownerId = 2003;
    ShareWhiteboardInfo b2;
    b2.m_seat = 5;
    b2.m_ownerId = 2001;
    s.m_whiteboards.push_back(b1);
    s.m_whiteboards.push_back(b2);
    //清单：主持人的屏幕、2002 的屏幕、两块白板（按座位）
    const std::vector<ShareScreenInfo> all = CollectShareScreens(s);
    REQUIRE(all.size() == 4);
    CHECK_FALSE(all[0].m_isWhiteboard);
    CHECK_FALSE(all[1].m_isWhiteboard);
    CHECK(all[2].m_isWhiteboard);
    CHECK(all[2].m_screenSeat == 5);
    CHECK(all[2].m_ownerId == 2001);
    CHECK(all[2].m_isHost);
    CHECK(all[3].m_screenSeat == 6);
    CHECK(all[3].m_ownerId == 2003);
    CHECK(all[3].m_ownerSeat == 2);
    //本人建的白板不在本人要看的清单里
    const std::vector<ShareScreenInfo> mine = CollectOtherScreens(s, 2003);
    REQUIRE(mine.size() == 3);
    CHECK(FindScreenBySeat(mine, 6) < 0);
    //名额：两个屏幕加两块白板
    CHECK(ShareSlotsUsed(s) == 4);
    //只建了白板的 2003 不能接任主持人
    const std::vector<int64_t> c = HostTransferCandidates(s, 2001);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == 2002);
    //白板的申请队列看 boardrequesting
    ShareRequestQueue boards(share_kind_whiteboard);
    boards.Add(2002, L"B");
    boards.Add(2003, L"C");
    s.m_members[1].m_requesting = true;          //2002 申请的是屏幕，不是白板
    s.m_members[2].m_boardRequesting = true;     //2003 在申请白板
    const std::vector<int64_t> removed = boards.Prune(s);
    REQUIRE(removed.size() == 1);
    CHECK(removed[0] == 2002);
    CHECK(boards.FrontUser() == 2003);
}
