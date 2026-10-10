/**
 *  屏幕共享房间中转帧头的单元测试（Suite RRF），test_roomrelayframe.cpp
 *
 *  覆盖 RoomRelayFrame（docs/屏幕共享与录制方案.md 第 11.4 节 2b-1，2026-10-02 与用户确认的用例）：
 *    - RRF1 解析帧头 [轨道 1B][发送者座位 1B][标志 1B][时间戳 8B 大端]；不足 11 字节或轨道类型越界一律拒绝；
 *           顺带检查哪些轨道算视频轨；
 *    - RRF2 把帧头里的发送者座位号改写为本连接登记的座位号，其余字节不变；包体太短时不改。
 *  服务端生成的关键帧请求的字节格式在 RRP4（test_roomforwardpolicy.cpp）里检查。
 *  第 18 期补 SSCT5：键鼠输入控制帧取出目标座位（服务端只转发给目标座位）。
 *  第 22 期补 SSMS7（只推流的连接）、SSMS9（不回送给发送者本人的其他连接）。
 *  语音字幕（docs/语音字幕方案.md 第 9 节，2026-10-06）补 CAPRF1：字幕控制帧既不是订阅也不是键鼠输入，按普通控制帧
 *  改写座位号后广播；主持人、观看者都能发，群通话成员发来的控制帧仍不转发（由 RelaySession 按角色判断）。
 *
 *  典型用法：Tests.exe --test-case="RRF*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>

#include "doctest.h"
#include "../imgserversrc/RoomRelayFrame.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

namespace {

//测试用的时间戳：八个字节各不相同，便于发现字节序错误
const uint64_t kRrfTimestamp = 0x0102030405060708ULL;

//测试用的发送者座位号与改写后的座位号
const uint8_t kRrfSpoofedSeat = 9;
const uint8_t kRrfRealSeat = 2;

//构造一帧 1860 包体：帧头 + 载荷
std::string rrfMakeBody(int track, int seat, int flags, uint64_t timestamp, const std::string& payload)
{
    std::string body;
    body.push_back(static_cast<char>(track));
    body.push_back(static_cast<char>(seat));
    body.push_back(static_cast<char>(flags));
    //时间戳按大端写入：最高字节在前
    const int kBitsPerByte = 8;
    const int kTimestampBytes = 8;
    for (int i = kTimestampBytes - 1; i >= 0; --i)
        body.push_back(static_cast<char>((timestamp >> (i * kBitsPerByte)) & 0xFF));
    body += payload;
    return body;
}

} // namespace

TEST_CASE("RRF1 解析帧头：各字段与大端时间戳，长度不足或轨道越界拒绝")
{
    const int keyFirst = screenshare_flag_keyframe | screenshare_flag_first_frag;
    std::string body = rrfMakeBody(screenshare_track_screen, 3, keyFirst, kRrfTimestamp, "abc");
    REQUIRE(body.size() == kRoomRelayHeaderSize + 3);

    RoomRelayFrameHeader h;
    REQUIRE(RoomRelayFrame::parseHeader(body.data(), body.size(), h));
    CHECK(h.m_track == screenshare_track_screen);
    CHECK(h.m_senderSeat == 3);
    CHECK(h.m_flags == keyFirst);
    CHECK(h.m_timestamp == kRrfTimestamp);

    //恰好只有帧头、没有载荷也合法
    std::string headerOnly = rrfMakeBody(screenshare_track_control, screenshare_seat_server, 0, 0, "");
    REQUIRE(RoomRelayFrame::parseHeader(headerOnly.data(), headerOnly.size(), h));
    CHECK(h.m_track == screenshare_track_control);
    CHECK(h.m_senderSeat == screenshare_seat_server);

    //长度不足帧头
    CHECK_FALSE(RoomRelayFrame::parseHeader(body.data(), kRoomRelayHeaderSize - 1, h));
    CHECK_FALSE(RoomRelayFrame::parseHeader(NULL, 0, h));

    //轨道类型越界：0 与比最大值大 1
    std::string zeroTrack = rrfMakeBody(0, 1, 0, kRrfTimestamp, "x");
    CHECK_FALSE(RoomRelayFrame::parseHeader(zeroTrack.data(), zeroTrack.size(), h));
    std::string bigTrack = rrfMakeBody(screenshare_track_control + 1, 1, 0, kRrfTimestamp, "x");
    CHECK_FALSE(RoomRelayFrame::parseHeader(bigTrack.data(), bigTrack.size(), h));

    //只有屏幕画面与摄像头画面算视频轨
    CHECK(RoomRelayFrame::isVideoTrack(screenshare_track_screen));
    CHECK(RoomRelayFrame::isVideoTrack(screenshare_track_camera));
    CHECK_FALSE(RoomRelayFrame::isVideoTrack(screenshare_track_mic));
    CHECK_FALSE(RoomRelayFrame::isVideoTrack(screenshare_track_system_audio));
    CHECK_FALSE(RoomRelayFrame::isVideoTrack(screenshare_track_annotation));
    CHECK_FALSE(RoomRelayFrame::isVideoTrack(screenshare_track_control));
}

TEST_CASE("RRF2 发送者座位号改写为本连接登记的座位号，其余字节不变")
{
    const int full = screenshare_flag_first_frag | screenshare_flag_last_frag;
    std::string body = rrfMakeBody(screenshare_track_camera, kRrfSpoofedSeat, full, kRrfTimestamp, "payload");
    const std::string original = body;

    REQUIRE(RoomRelayFrame::rewriteSenderSeat(body, kRrfRealSeat));
    RoomRelayFrameHeader h;
    REQUIRE(RoomRelayFrame::parseHeader(body.data(), body.size(), h));
    CHECK(h.m_senderSeat == kRrfRealSeat);
    //只有第 2 个字节（下标 1）变了
    REQUIRE(body.size() == original.size());
    CHECK(body[0] == original[0]);
    CHECK(body.substr(2) == original.substr(2));

    //包体太短：不改并返回 false
    std::string tooShort = original.substr(0, kRoomRelayHeaderSize - 1);
    const std::string tooShortCopy = tooShort;
    CHECK_FALSE(RoomRelayFrame::rewriteSenderSeat(tooShort, kRrfRealSeat));
    CHECK(tooShort == tooShortCopy);
}

TEST_CASE("SSCT5 键鼠输入控制帧：取出目标座位，只转发给它；格式不对时按普通控制帧处理")
{
    const int firstLast = screenshare_flag_first_frag | screenshare_flag_last_frag;
    //[4][目标座位 0][一条键盘命令的首字节]
    std::string payload;
    payload.push_back(static_cast<char>(screenshare_control_input));
    payload.push_back(static_cast<char>(0));
    payload.push_back(static_cast<char>(2));
    int seat = -1;
    REQUIRE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_control, 3, firstLast, 0, payload), seat));
    CHECK(seat == 0);
    //目标座位可以是观看者（多人共享时画面主人不一定是发起人）
    payload[1] = static_cast<char>(7);
    REQUIRE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_control, 3, firstLast, 0, payload), seat));
    CHECK(seat == 7);

    //目标座位为 255（服务端专用）、没有输入命令、控制类型不是输入、轨道不是控制帧、帧头不完整：不认
    std::string badSeat = payload;
    badSeat[1] = static_cast<char>(screenshare_seat_server);
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_control, 3, firstLast, 0, badSeat), seat));
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_control, 3, firstLast, 0,
                                                             payload.substr(0, 2)), seat));
    std::string notInput = payload;
    notInput[0] = static_cast<char>(screenshare_control_source_state);
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_control, 3, firstLast, 0, notInput), seat));
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(rrfMakeBody(screenshare_track_annotation, 3, firstLast, 0, payload),
                                                 seat));
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(std::string(kRoomRelayHeaderSize - 1, '\0'), seat));
}

TEST_CASE("SSMS7 只推流的连接（第 22 期）：只能发屏幕画面与控制帧，不接收房间里的转发")
{
    const int kPublisher = RelayToken::role_screenshare_publisher;
    CHECK(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_screen));
    CHECK(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_control));
    CHECK_FALSE(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_camera));
    CHECK_FALSE(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_mic));
    CHECK_FALSE(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_system_audio));
    CHECK_FALSE(RoomRelayFrame::isTrackAllowedForRole(kPublisher, screenshare_track_annotation));
    CHECK_FALSE(RoomRelayFrame::receivesRoomFrames(kPublisher));
    CHECK(RoomRelayFrame::receivesRoomFrames(RelayToken::role_screenshare_host));
    CHECK(RoomRelayFrame::receivesRoomFrames(RelayToken::role_screenshare_member));
    CHECK(RoomRelayFrame::receivesRoomFrames(RelayToken::role_groupcall_member));
}

TEST_CASE("SSMS9 房间里的一帧不回送给发送者本人的其他连接（共享者的画面不回到他自己的观看连接）")
{
    const int kMember = RelayToken::role_screenshare_member;
    const int kPublisher = RelayToken::role_screenshare_publisher;
    //别人：照常转
    CHECK(RoomRelayFrame::shouldForwardTo(kMember, 2003, 2002));
    //本人的另一条连接：不转
    CHECK_FALSE(RoomRelayFrame::shouldForwardTo(kMember, 2002, 2002));
    //只推流的连接：一律不转
    CHECK_FALSE(RoomRelayFrame::shouldForwardTo(kPublisher, 2003, 2002));
    //不知道发送者是谁：只看角色
    CHECK(RoomRelayFrame::shouldForwardTo(kMember, 0, 0));
    CHECK(RoomRelayFrame::shouldForwardTo(RelayToken::role_screenshare_host, 2001, 0));
}

TEST_CASE("CAPRF1 字幕控制帧：不当作订阅或键鼠输入，改写座位号后按普通控制帧广播；共享主持人与观看者都能发")
{
    const int firstLast = screenshare_flag_first_frag | screenshare_flag_last_frag;
    //[5][标志 1 = 最终结果][句子编号 4B][时长 2B][UTF-8 文字]
    std::string payload;
    payload.push_back(static_cast<char>(screenshare_control_caption));
    payload.push_back(static_cast<char>(1));
    //句子编号 7、时长 1500 毫秒，均为大端
    const unsigned char kNumbers[] = { 0x00, 0x00, 0x00, 0x07, 0x05, 0xDC };
    payload.append(reinterpret_cast<const char*>(kNumbers), sizeof(kNumbers));
    payload.append(u8"你好");
    std::string body = rrfMakeBody(screenshare_track_control, kRrfSpoofedSeat, firstLast, kRrfTimestamp, payload);

    //既不是订阅，也不是键鼠输入：RelaySession 走到最后的广播分支
    std::vector<int> seats;
    int target = -1;
    CHECK_FALSE(RoomRelayFrame::parseSubscribe(body, seats));
    CHECK_FALSE(RoomRelayFrame::parseInputTarget(body, target));

    //座位号改写为本连接登记的座位，载荷原样保留
    REQUIRE(RoomRelayFrame::rewriteSenderSeat(body, kRrfRealSeat));
    RoomRelayFrameHeader header;
    REQUIRE(RoomRelayFrame::parseHeader(body.data(), body.size(), header));
    CHECK(header.m_track == screenshare_track_control);
    CHECK(header.m_senderSeat == kRrfRealSeat);
    CHECK(body.substr(kRoomRelayHeaderSize) == payload);

    //每个发言人识别自己的话：主持人、观看者、只推流的共享者都要能发控制帧
    CHECK(RoomRelayFrame::isTrackAllowedForRole(RelayToken::role_screenshare_host, screenshare_track_control));
    CHECK(RoomRelayFrame::isTrackAllowedForRole(RelayToken::role_screenshare_member, screenshare_track_control));
    CHECK(RoomRelayFrame::isTrackAllowedForRole(RelayToken::role_screenshare_publisher, screenshare_track_control));
}
