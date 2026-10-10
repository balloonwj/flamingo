/**
 *  屏幕共享房间中转慢观看者丢帧策略的单元测试（Suite RRP），test_roomforwardpolicy.cpp
 *
 *  覆盖 RoomForwardPolicy（docs/屏幕共享与录制方案.md 第 11.4 节 2b-1，2026-10-02 与用户确认的用例）：
 *    - RRP1 积压没超过上限时所有帧照常转发；
 *    - RRP2 超过上限后发给该观看者的视频非关键帧被丢弃，直到下一个关键帧到达；关键帧照发，发完恢复；
 *           是否丢弃只在首分片上决定，其余分片跟随首分片；
 *    - RRP3 音频、标注、控制帧即使超过上限也不丢；
 *    - RRP4 开始丢帧时向这路视频的发送者请求关键帧，同一路视频 2 秒内至多一次；顺带检查关键帧请求的字节格式；
 *    - RRP5 新成员加入时向所有已知的视频发送者请求关键帧，不受限频约束，但刷新限频计时；
 *    - RRP6 状态按接收者 × 发送者 × 视频轨分别记，互不影响；成员离开后其状态清空；
 *    - RRP7 发送者独自在房间时发的视频也算已知视频流（2026-10-02 联调用例 SSRC1 查出漏洞后补的用例）。
 *
 *  典型用法：Tests.exe --test-case="RRP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../imgserversrc/RoomForwardPolicy.h"
#include "../imgserversrc/RoomRelayFrame.h"
#include "../chatserversrc/Protocol.h"

namespace {

//测试用的积压上限，以及低于、高于它的积压字节数
const size_t kRrpCap = 1000;
const size_t kRrpUnder = 0;
const size_t kRrpOver = 2000;

//座位号：发起人 0，观看者 1、2、3
const int kRrpHost = 0;
const int kRrpViewer1 = 1;
const int kRrpViewer2 = 2;
const int kRrpViewer3 = 3;

//常用的标志位组合
const int kRrpWhole = screenshare_flag_first_frag | screenshare_flag_last_frag;   //不拆分的非关键帧
const int kRrpWholeKey = kRrpWhole | screenshare_flag_keyframe;                     //不拆分的关键帧

//几个时间点，单位毫秒
const int64_t kRrpT0 = 10000;
const int64_t kRrpOneSecond = 1000;
const int64_t kRrpHalfSecond = 500;

RoomRelayFrameHeader rrpHeader(int track, int senderSeat, int flags)
{
    RoomRelayFrameHeader h;
    h.m_track = static_cast<uint8_t>(track);
    h.m_senderSeat = static_cast<uint8_t>(senderSeat);
    h.m_flags = static_cast<uint8_t>(flags);
    return h;
}

//对一帧做决定，丢弃返回 false、转发返回 true；requested 回填是否要求发关键帧请求
bool rrpSend(RoomForwardPolicy& p, int receiver, int track, int sender, int flags, size_t pending, int64_t now,
             bool& requested)
{
    return p.decide(receiver, rrpHeader(track, sender, flags), pending, now, requested) == room_forward_send;
}

bool rrpSend(RoomForwardPolicy& p, int receiver, int track, int sender, int flags, size_t pending, int64_t now)
{
    bool requested = false;
    return rrpSend(p, receiver, track, sender, flags, pending, now, requested);
}

} // namespace

TEST_CASE("RRP1 积压没超过上限时所有帧照常转发")
{
    RoomForwardPolicy p(kRrpCap);
    bool requested = true;
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0, requested));
    CHECK_FALSE(requested);
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_camera, kRrpViewer2, kRrpWhole, kRrpCap, kRrpT0, requested));
    CHECK_FALSE(requested);
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_mic, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_annotation, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_control, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
}

TEST_CASE("RRP2 超过上限后丢视频非关键帧直到下一个关键帧，关键帧照发，分片跟随首分片")
{
    RoomForwardPolicy p(kRrpCap);
    const int screen = screenshare_track_screen;

    SUBCASE("不拆分的帧")
    {
        CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));
        //积压已经消退，仍然等关键帧
        CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
        //关键帧即使积压仍超过上限也照发
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWholeKey, kRrpOver, kRrpT0));
        //关键帧之后恢复正常
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
    }
    SUBCASE("拆成多个分片的帧")
    {
        //非关键帧的首分片被丢，中间与最后一个分片跟着丢（即使积压已经消退）
        CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, screenshare_flag_first_frag, kRrpOver, kRrpT0));
        CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, 0, kRrpUnder, kRrpT0));
        CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, screenshare_flag_last_frag, kRrpUnder, kRrpT0));
        //关键帧的三个分片都照发
        const int key = screenshare_flag_keyframe;
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, key | screenshare_flag_first_frag, kRrpOver, kRrpT0));
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, key, kRrpOver, kRrpT0));
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, key | screenshare_flag_last_frag, kRrpOver, kRrpT0));
        //首分片照发的帧，后续分片即使积压超过上限也照发，不会把一帧截成半截
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, screenshare_flag_first_frag, kRrpUnder, kRrpT0));
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, 0, kRrpOver, kRrpT0));
        CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, screenshare_flag_last_frag, kRrpOver, kRrpT0));
    }
}

TEST_CASE("RRP3 音频、标注、控制帧即使超过上限也不丢")
{
    RoomForwardPolicy p(kRrpCap);
    //先让观看者 1 对发起人的屏幕画面进入等关键帧状态
    REQUIRE_FALSE(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));

    bool requested = true;
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_mic, kRrpHost, kRrpWhole, kRrpOver, kRrpT0, requested));
    CHECK_FALSE(requested);
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_system_audio, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_annotation, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_control, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_mic, kRrpViewer2, 0, kRrpOver, kRrpT0));
}

TEST_CASE("RRP4 开始丢帧时向发送者请求关键帧，同一路视频 2 秒内至多一次")
{
    RoomForwardPolicy p(kRrpCap);
    const int screen = screenshare_track_screen;
    bool requested = false;

    //观看者 1 开始丢帧：要求发请求
    CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0, requested));
    CHECK(requested);
    //已在等关键帧，后续丢帧不再重复请求
    CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0 + kRrpHalfSecond, requested));
    CHECK_FALSE(requested);
    //观看者 2 在 1 秒后也开始丢同一路视频：2 秒内已请求过，不再请求
    CHECK_FALSE(rrpSend(p, kRrpViewer2, screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0 + kRrpOneSecond, requested));
    CHECK_FALSE(requested);
    //关键帧到达，两人都恢复
    CHECK(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWholeKey, kRrpUnder, kRrpT0 + kRrpOneSecond));
    CHECK(rrpSend(p, kRrpViewer2, screen, kRrpHost, kRrpWholeKey, kRrpUnder, kRrpT0 + kRrpOneSecond));
    //1.5 秒时再次开始丢帧：仍在 2 秒内，不请求
    CHECK_FALSE(rrpSend(p, kRrpViewer1, screen, kRrpHost, kRrpWhole, kRrpOver,
                        kRrpT0 + kRrpOneSecond + kRrpHalfSecond, requested));
    CHECK_FALSE(requested);
    //距上次请求满 2 秒：观看者 2 开始丢帧时再次请求
    CHECK_FALSE(rrpSend(p, kRrpViewer2, screen, kRrpHost, kRrpWhole, kRrpOver,
                        kRrpT0 + RoomForwardPolicy::kKeyFrameRequestIntervalMs, requested));
    CHECK(requested);

    //关键帧请求的字节格式：控制帧、发送者座位 255、首尾分片位、时间戳 0，载荷为 [请求关键帧][视频轨]
    const std::string req = RoomRelayFrame::buildKeyFrameRequest(screenshare_track_camera);
    REQUIRE(req.size() == kRoomRelayHeaderSize + 2);
    RoomRelayFrameHeader h;
    REQUIRE(RoomRelayFrame::parseHeader(req.data(), req.size(), h));
    CHECK(h.m_track == screenshare_track_control);
    CHECK(h.m_senderSeat == screenshare_seat_server);
    CHECK(h.m_flags == kRrpWhole);
    CHECK(h.m_timestamp == 0);
    CHECK(static_cast<unsigned char>(req[kRoomRelayHeaderSize]) == screenshare_control_keyframe_request);
    CHECK(static_cast<unsigned char>(req[kRoomRelayHeaderSize + 1]) == screenshare_track_camera);
}

TEST_CASE("RRP5 新成员加入时向所有已知视频发送者请求关键帧，不受限频约束但刷新计时")
{
    RoomForwardPolicy p(kRrpCap);
    std::vector<RoomVideoStream> requests;

    //还没有转发过任何视频：没有要请求的
    p.onMemberJoined(kRrpT0, requests);
    CHECK(requests.empty());

    //发起人的屏幕画面因观看者 1 丢帧刚请求过一次；观看者 2 的摄像头画面正常转发过一帧；音频不算视频流
    bool requested = false;
    REQUIRE_FALSE(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0, requested));
    REQUIRE(requested);
    REQUIRE(rrpSend(p, kRrpViewer1, screenshare_track_camera, kRrpViewer2, kRrpWhole, kRrpUnder, kRrpT0));
    REQUIRE(rrpSend(p, kRrpViewer1, screenshare_track_mic, kRrpViewer3, kRrpWhole, kRrpUnder, kRrpT0));

    //半秒后有人加入：两路视频都要请求，不受刚才那次请求的限频约束；按发送者座位号、视频轨升序
    p.onMemberJoined(kRrpT0 + kRrpHalfSecond, requests);
    REQUIRE(requests.size() == 2);
    CHECK(requests[0] == RoomVideoStream(kRrpHost, screenshare_track_screen));
    CHECK(requests[1] == RoomVideoStream(kRrpViewer2, screenshare_track_camera));

    //加入时的请求刷新了限频计时：再过 1.6 秒（距 T0 已 2.1 秒、距加入 1.6 秒）有人开始丢帧，不再请求
    const int64_t later = kRrpT0 + kRrpHalfSecond + kRrpOneSecond + kRrpHalfSecond + kRrpHalfSecond / 5;
    CHECK_FALSE(rrpSend(p, kRrpViewer3, screenshare_track_camera, kRrpViewer2, kRrpWhole, kRrpOver, later, requested));
    CHECK_FALSE(requested);

    //观看者 2 离开后，他的摄像头画面不再列入
    p.onMemberLeft(kRrpViewer2);
    p.onMemberJoined(later, requests);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == RoomVideoStream(kRrpHost, screenshare_track_screen));
}

TEST_CASE("RRP6 状态按接收者、发送者、视频轨分别记，成员离开后其状态清空")
{
    RoomForwardPolicy p(kRrpCap);
    //观看者 1 对发起人的屏幕画面进入等关键帧状态
    REQUIRE_FALSE(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpOver, kRrpT0));

    //同一接收者、同一发送者的摄像头画面不受影响
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_camera, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
    //同一接收者、别的发送者的摄像头画面不受影响
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_camera, kRrpViewer2, kRrpWhole, kRrpUnder, kRrpT0));
    //别的接收者收同一路屏幕画面不受影响
    CHECK(rrpSend(p, kRrpViewer3, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
    //观看者 1 自己的屏幕画面仍在等关键帧
    CHECK_FALSE(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));

    //观看者 1 离开后其状态清空：同一座位号的新成员收非关键帧照常转发
    p.onMemberLeft(kRrpViewer1);
    CHECK(rrpSend(p, kRrpViewer1, screenshare_track_screen, kRrpHost, kRrpWhole, kRrpUnder, kRrpT0));
}

TEST_CASE("RRP7 发送者独自在房间时发的视频也算已知视频流，之后有人加入时请求关键帧")
{
    RoomForwardPolicy p(kRrpCap);
    //房间里只有发起人：他发的屏幕画面没有接收者，不经过 decide，只经过 noteFrame
    p.noteFrame(rrpHeader(screenshare_track_screen, kRrpHost, kRrpWholeKey));
    //声音不算视频流
    p.noteFrame(rrpHeader(screenshare_track_mic, kRrpHost, kRrpWhole));

    std::vector<RoomVideoStream> requests;
    p.onMemberJoined(kRrpT0, requests);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == RoomVideoStream(kRrpHost, screenshare_track_screen));

    //发送者离开后不再列入
    p.onMemberLeft(kRrpHost);
    p.onMemberJoined(kRrpT0 + kRrpOneSecond, requests);
    CHECK(requests.empty());
}
