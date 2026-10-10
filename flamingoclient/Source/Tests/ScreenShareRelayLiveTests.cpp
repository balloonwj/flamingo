/**
 *  屏幕共享房间中转的端到端联调用例（Suite SSRELAY，标签 [.live][screenshare]），ScreenShareRelayLiveTests.cpp
 *
 *  直接连 imgserver 的中转端口，验证 2b-2 接入之后的房间转发（docs/屏幕共享与录制方案.md 第 11.4 节，
 *  2026-10-02 与用户确认的联调用例 1~7）：多人转发与不回送、发送者座位改写、握手拒绝、新成员加入时的关键帧请求、
 *  成员断开后其余人不受影响、慢观看者丢帧、压缩帧不转发。中转令牌在测试里自己签（与 chatserver 用同一把密钥），
 *  不经过 chatserver —— imgserver 只认令牌。
 *
 *  默认不参与常规测试：用例带 [.live] 标签。跑之前先起 tmp/calltest 的测试服务端（setup_calltest.py screenshare
 *  会把 imgserver 的观看者积压上限调到 256 KiB，用例 6 依赖这一点），然后：
 *
 *      FlamingoUnit.exe "[screenshare]"
 *
 *  连接、令牌、收发帧的辅助函数在 RoomRelayTestKit（2026-10-03 抽出，与压测用例共用）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <thread>
#include <time.h>

#include "RoomRelayTestKit.h"
#include "../net/Protocol.h"
#include "../../../flamingoserver/utils/RelayToken.h"

using namespace roomrelaytest;

namespace {

//用例 6：每帧载荷大小、非关键帧个数（共约 12.8 MB，远超 256 KiB 的积压上限与本机回环的 socket 缓冲）
const size_t kFloodFrameBytes = 64 * 1024;
const int kFloodFrames = 200;
//用例 6 的发送节奏：每发两帧停 5 毫秒，让正常读取的观看者跟得上，积压不超过上限（慢观看者不读，照样越积越多）
const int kFloodBatchFrames = 2;
const DWORD kFloodPauseMs = 5;

void requireJoined(RoomPeer& p, const std::string& roomId, int role, int seat)
{
    int code = -1;
    std::string reason;
    const bool ok = joinRoom(p, roomId, role, seat, kRoomTokenKey, code, reason);
    INFO("join seat " << seat << ": " << reason);
    REQUIRE(ok);
    REQUIRE(code == 0);
}

//用例 6 里正常读取的观看者：在独立线程里收满 expected 帧，统计收到的帧数
void drainFrames(RoomPeer* p, int expected, int* received)
{
    RoomFrame f;
    while (*received < expected && recvRoomFrame(*p, f))
        ++(*received);
}

} // namespace

TEST_CASE("SSRELAY1 多人转发：发起人的画面两名观看者都收到，观看者的声音转给其他人且不回送", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    RoomPeer v2;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);
    requireJoined(v2, room, RelayToken::role_screenshare_member, 2);

    const std::string screen = makePayload(1000, 1);
    REQUIRE(sendRoomFrame(host, screenshare_track_screen, 0, kWholeKey, 1001, screen));
    for (int i = 0; i < 2; ++i)
    {
        RoomPeer& v = (i == 0) ? v1 : v2;
        RoomFrame f;
        REQUIRE(recvRoomFrame(v, f));
        CHECK(f.m_track == screenshare_track_screen);
        CHECK(f.m_seat == 0);
        CHECK(f.m_flags == kWholeKey);
        CHECK(f.m_timestamp == 1001);
        CHECK(f.m_payload == screen);
    }

    const std::string voice = makePayload(200, 7);
    REQUIRE(sendRoomFrame(v1, screenshare_track_mic, 1, kWhole, 2002, voice));
    RoomFrame f;
    REQUIRE(recvRoomFrame(host, f));
    CHECK(f.m_track == screenshare_track_mic);
    CHECK(f.m_seat == 1);
    CHECK(f.m_payload == voice);
    REQUIRE(recvRoomFrame(v2, f));
    CHECK(f.m_seat == 1);
    CHECK(f.m_timestamp == 2002);

    //不回送：发起人收到声音之后再发一帧标记，观看者 1 收到的下一帧应当就是这帧标记
    REQUIRE(sendRoomFrame(host, screenshare_track_annotation, 0, kWhole, 3003, "marker"));
    REQUIRE(recvRoomFrame(v1, f));
    CHECK(f.m_timestamp == 3003);
    CHECK(f.m_payload == "marker");
}

TEST_CASE("SSRELAY2 发送者座位号以连接登记的为准，冒充无效", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    RoomPeer v2;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);
    requireJoined(v2, room, RelayToken::role_screenshare_member, 2);

    //观看者 1 冒充发起人（座位 0）发一帧
    REQUIRE(sendRoomFrame(v1, screenshare_track_annotation, 0, kWhole, 4004, "spoof"));
    RoomFrame f;
    REQUIRE(recvRoomFrame(host, f));
    CHECK(f.m_seat == 1);
    REQUIRE(recvRoomFrame(v2, f));
    CHECK(f.m_seat == 1);
    CHECK(f.m_payload == "spoof");
}

TEST_CASE("SSRELAY3 不合规的握手被拒，已有成员不受影响", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);

    struct Attempt
    {
        const char* m_what;   //拒绝的原因，断言失败时显示
        int         m_role;   //令牌里的角色
        int         m_seat;   //令牌里的座位号
        const char* m_key;    //签令牌的密钥
    };
    const Attempt attempts[] =
    {
        { "call token (role 3)", RelayToken::role_caller, 2, kRoomTokenKey },
        { "host token with a nonzero seat", RelayToken::role_screenshare_host, 3, kRoomTokenKey },
        { "duplicate seat 1", RelayToken::role_screenshare_member, 1, kRoomTokenKey },
        { "wrong signature", RelayToken::role_screenshare_member, 4, kWrongTokenKey },
    };
    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); ++i)
    {
        RoomPeer bad;
        int code = -1;
        std::string reason;
        const bool acked = joinRoom(bad, room, attempts[i].m_role, attempts[i].m_seat, attempts[i].m_key, code, reason);
        INFO(attempts[i].m_what << ": " << reason);
        REQUIRE(acked);
        CHECK(code != 0);
    }

    //已有成员照常收发
    REQUIRE(sendRoomFrame(host, screenshare_track_screen, 0, kWholeKey, 5005, "still-here"));
    RoomFrame f;
    REQUIRE(recvRoomFrame(v1, f));
    CHECK(f.m_payload == "still-here");
}

TEST_CASE("SSRELAY4 有新成员加入时，发过画面的发起人收到关键帧请求", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);
    REQUIRE(sendRoomFrame(host, screenshare_track_screen, 0, kWholeKey, 6006, "frame"));
    RoomFrame f;
    REQUIRE(recvRoomFrame(v1, f));

    RoomPeer v2;
    requireJoined(v2, room, RelayToken::role_screenshare_member, 2);
    REQUIRE(recvRoomFrame(host, f));
    CHECK(isKeyFrameRequest(f, screenshare_track_screen));
}

TEST_CASE("SSRELAY5 一名观看者断开后其余人照常收发，空出的座位可以重新登记", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v2;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    {
        RoomPeer v1;
        requireJoined(v1, room, RelayToken::role_screenshare_member, 1);
        requireJoined(v2, room, RelayToken::role_screenshare_member, 2);
        v1.m_conn.Close();
    }
    //给服务端一点时间处理断开
    ::Sleep(300);

    REQUIRE(sendRoomFrame(host, screenshare_track_annotation, 0, kWhole, 7007, "after-leave"));
    RoomFrame f;
    REQUIRE(recvRoomFrame(v2, f));
    CHECK(f.m_payload == "after-leave");
    REQUIRE(sendRoomFrame(v2, screenshare_track_mic, 2, kWhole, 7008, "v2-voice"));
    REQUIRE(recvRoomFrame(host, f));
    CHECK(f.m_payload == "v2-voice");

    RoomPeer again;
    requireJoined(again, room, RelayToken::role_screenshare_member, 1);
    REQUIRE(sendRoomFrame(host, screenshare_track_annotation, 0, kWhole, 7009, "welcome-back"));
    REQUIRE(recvRoomFrame(again, f));
    CHECK(f.m_payload == "welcome-back");
}

TEST_CASE("SSRELAY6 慢观看者：只丢发给他的非关键帧，发起人收到关键帧请求，其他人不受影响", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    RoomPeer slow;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);
    requireJoined(slow, room, RelayToken::role_screenshare_member, 2);

    //观看者 1 在独立线程里正常读取；慢观看者这段时间完全不读
    const int total = kFloodFrames + 2;
    int v1Received = 0;
    std::thread reader(drainFrames, &v1, total, &v1Received);

    const std::string chunk = makePayload(kFloodFrameBytes, 3);
    bool sentAll = sendRoomFrame(host, screenshare_track_screen, 0, kWholeKey, 0, chunk);
    for (int i = 1; sentAll && i <= kFloodFrames; ++i)
    {
        sentAll = sendRoomFrame(host, screenshare_track_screen, 0, kWhole, (uint64_t)i, chunk);
        if (i % kFloodBatchFrames == 0)
            ::Sleep(kFloodPauseMs);
    }
    //最后再发一个关键帧，时间戳取一个特殊值作为结束标记
    const uint64_t lastKeyTs = 999999;
    sentAll = sentAll && sendRoomFrame(host, screenshare_track_screen, 0, kWholeKey, lastKeyTs, chunk);
    reader.join();
    REQUIRE(sentAll);
    CHECK(v1Received == total);

    //发起人收到了关键帧请求
    RoomFrame f;
    REQUIRE(recvRoomFrame(host, f));
    CHECK(isKeyFrameRequest(f, screenshare_track_screen));

    //慢观看者恢复读取：收到的帧数少于发出的，且最后那个关键帧一定收到
    int slowReceived = 0;
    bool gotLastKey = false;
    while (!gotLastKey && recvRoomFrame(slow, f))
    {
        ++slowReceived;
        gotLastKey = (f.m_timestamp == lastKeyTs && (f.m_flags & screenshare_flag_keyframe) != 0);
    }
    CHECK(gotLastKey);
    CHECK(slowReceived < total);
}

TEST_CASE("SSRELAY7 标了压缩的 1860 帧不转发，连接保持，之后不压缩的帧照常转发", "[.live][screenshare]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    RoomPeer v1;
    requireJoined(host, room, RelayToken::role_screenshare_host, 0);
    requireJoined(v1, room, RelayToken::role_screenshare_member, 1);

    REQUIRE(sendRoomFrame(host, screenshare_track_annotation, 0, kWhole, 8008, "compressed", true));
    REQUIRE(sendRoomFrame(host, screenshare_track_annotation, 0, kWhole, 8009, "plain"));
    RoomFrame f;
    REQUIRE(recvRoomFrame(v1, f));
    CHECK(f.m_timestamp == 8009);
    CHECK(f.m_payload == "plain");
}
