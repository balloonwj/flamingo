/**
 *  群通话在 imgserver 房间中转上的改动单元测试（Suite IGC），test_groupcallrelay.cpp
 *
 *  覆盖 docs/协议设计-v2.md 21.8 与 docs/群音视频通话方案.md 5.2：
 *    IGC1 成员表：群通话成员（角色 13）坐 1~254 号，不能坐 0 号；屏幕共享与群通话的成员不能进同一个房间
 *    IGC2 一对一报到（远程桌面、音视频、P2P）拒绝角色 13
 *    IGC3 按角色过滤轨道：角色 13 只能发摄像头、麦克风、控制帧；屏幕共享的角色六种都可以
 *    IGC4 订阅控制帧的构造与解析：合法帧、座位数为 0、长度不符、不是控制帧、控制类型不对
 *    IGC5 丢帧策略的订阅：没订阅过收全部；订阅后只收订阅的座位的摄像头（声音不受影响）；新订阅的座位先等关键帧
 *         并请求一次关键帧；订阅为空时一路画面都不收；成员离开时清掉他的订阅与别人订阅里的他
 *
 *  典型用法：Tests.exe --test-case="IGC*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../imgserversrc/RelayRoomHub.h"
#include "../imgserversrc/RelayHelloPolicy.h"
#include "../imgserversrc/RoomForwardPolicy.h"
#include "../imgserversrc/RoomRelayFrame.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

namespace {

//假连接：只需能按对象身份区分
class IgcFakeConnection
{
public:
    explicit IgcFakeConnection(int id)
        : m_id(id)
    {
    }

    int getId() const
    {
        return m_id;
    }

private:
    int m_id;   //连接编号，测试内唯一
};

typedef std::shared_ptr<IgcFakeConnection> IgcConnPtr;

//假会话：只实现成员表要求的 getConnectionPtr()
class IgcFakeSession
{
public:
    explicit IgcFakeSession(const IgcConnPtr& conn)
        : m_conn(conn)
    {
    }

    IgcConnPtr getConnectionPtr()
    {
        return m_conn;
    }

private:
    IgcConnPtr m_conn;   //本会话所属的连接
};

typedef std::shared_ptr<IgcFakeSession> IgcSessionPtr;
typedef imgserver::RelayRoomHubT<IgcFakeSession, IgcConnPtr> IgcHub;

//两个房间号：一个群通话、一个屏幕共享
const char* kIgcCallRoom = "gc-600001-1790943882000-3";
const char* kIgcShareRoom = "ss-5127-1790943882000-8";

//丢帧策略测试用的积压上限与积压字节数
const size_t kIgcCap = 1000;
const size_t kIgcUnder = 0;

//常用的标志位组合：不拆分的非关键帧、不拆分的关键帧
const int kIgcWhole = screenshare_flag_first_frag | screenshare_flag_last_frag;
const int kIgcWholeKey = kIgcWhole | screenshare_flag_keyframe;

//时间点，单位毫秒
const int64_t kIgcT0 = 10000;

IgcSessionPtr makeSession(int id)
{
    return IgcSessionPtr(new IgcFakeSession(IgcConnPtr(new IgcFakeConnection(id))));
}

RoomRelayFrameHeader igcHeader(int track, int senderSeat, int flags)
{
    RoomRelayFrameHeader h;
    h.m_track = static_cast<uint8_t>(track);
    h.m_senderSeat = static_cast<uint8_t>(senderSeat);
    h.m_flags = static_cast<uint8_t>(flags);
    return h;
}

//对一帧做决定：转发返回 true
bool igcSend(RoomForwardPolicy& p, int receiver, int track, int sender, int flags)
{
    bool requested = false;
    return p.decide(receiver, igcHeader(track, sender, flags), kIgcUnder, kIgcT0, requested) == room_forward_send;
}

} // namespace

TEST_CASE("IGC1 成员表：群通话成员坐 1~254 号，屏幕共享与群通话不能进同一个房间")
{
    IgcHub hub;
    const int member = RelayToken::role_groupcall_member;
    IgcSessionPtr a = makeSession(1);
    IgcSessionPtr b = makeSession(2);
    IgcSessionPtr c = makeSession(3);
    //0 号座位不给群通话
    CHECK(!hub.join(kIgcCallRoom, 0, member, a));
    CHECK(hub.join(kIgcCallRoom, 1, member, a));
    CHECK(hub.join(kIgcCallRoom, 2, member, b));
    //座位已被占用
    CHECK(!hub.join(kIgcCallRoom, 2, member, c));
    //屏幕共享的角色不能进群通话房间
    CHECK(!hub.join(kIgcCallRoom, 3, RelayToken::role_screenshare_member, c));
    CHECK(hub.memberCount(kIgcCallRoom) == 2);

    std::vector<IgcHub::MemberRef> others;
    REQUIRE(hub.getOthers(kIgcCallRoom, a->getConnectionPtr(), others));
    REQUIRE(others.size() == 1);
    CHECK(others[0].m_seat == 2);

    //群通话的角色不能进屏幕共享房间
    IgcSessionPtr host = makeSession(4);
    CHECK(hub.join(kIgcShareRoom, 0, RelayToken::role_screenshare_host, host));
    CHECK(!hub.join(kIgcShareRoom, 1, member, c));
    CHECK(hub.join(kIgcShareRoom, 1, RelayToken::role_screenshare_member, c));

    CHECK(IgcHub::roomKindOfRole(member) == imgserver::room_kind_groupcall);
    CHECK(IgcHub::roomKindOfRole(RelayToken::role_screenshare_host) == imgserver::room_kind_screenshare);
    CHECK(IgcHub::roomKindOfRole(RelayToken::role_caller) == imgserver::room_kind_none);

    //房间摘空后可以换一种成员重新使用同一个房间号
    CHECK(hub.leaveByConn(a->getConnectionPtr(), NULL, NULL));
    CHECK(hub.leaveByConn(b->getConnectionPtr(), NULL, NULL));
    CHECK(hub.memberCount(kIgcCallRoom) == 0);
}

TEST_CASE("IGC2 一对一报到拒绝群通话角色")
{
    const int member = RelayToken::role_groupcall_member;
    CHECK(!RelayHelloPolicy::isAcceptedPairRole(relay_pair_rd, member));
    CHECK(!RelayHelloPolicy::isAcceptedPairRole(relay_pair_av, member));
    CHECK(!RelayHelloPolicy::isAcceptedPairRole(relay_pair_p2p, member));
    //原有角色不受影响
    CHECK(RelayHelloPolicy::isAcceptedPairRole(relay_pair_av, RelayToken::role_caller));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(relay_pair_rd, RelayToken::role_controller));
}

TEST_CASE("IGC3 按角色过滤轨道")
{
    const int member = RelayToken::role_groupcall_member;
    CHECK(RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_camera));
    CHECK(RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_mic));
    CHECK(RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_control));
    CHECK(!RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_screen));
    CHECK(!RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_system_audio));
    CHECK(!RoomRelayFrame::isTrackAllowedForRole(member, screenshare_track_annotation));
    for (int track = screenshare_track_screen; track <= screenshare_track_control; ++track)
    {
        CHECK(RoomRelayFrame::isTrackAllowedForRole(RelayToken::role_screenshare_host, track));
        CHECK(RoomRelayFrame::isTrackAllowedForRole(RelayToken::role_screenshare_member, track));
    }
}

TEST_CASE("IGC4 订阅控制帧的构造与解析")
{
    std::vector<int> seats;
    seats.push_back(2);
    seats.push_back(5);
    const std::string body = RoomRelayFrame::buildSubscribe(seats);
    RoomRelayFrameHeader header;
    REQUIRE(RoomRelayFrame::parseHeader(body.data(), body.size(), header));
    CHECK(header.m_track == screenshare_track_control);
    std::vector<int> parsed;
    REQUIRE(RoomRelayFrame::parseSubscribe(body, parsed));
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0] == 2);
    CHECK(parsed[1] == 5);

    //座位数为 0：一路画面都不要
    REQUIRE(RoomRelayFrame::parseSubscribe(RoomRelayFrame::buildSubscribe(std::vector<int>()), parsed));
    CHECK(parsed.empty());

    //长度与座位数不符
    std::string truncated = body;
    truncated.erase(truncated.size() - 1);
    CHECK(!RoomRelayFrame::parseSubscribe(truncated, parsed));
    std::string longer = body;
    longer.push_back(static_cast<char>(7));
    CHECK(!RoomRelayFrame::parseSubscribe(longer, parsed));
    //关键帧请求不是订阅帧
    CHECK(!RoomRelayFrame::parseSubscribe(RoomRelayFrame::buildKeyFrameRequest(screenshare_track_camera), parsed));
    //不是控制帧
    std::string notControl = body;
    notControl[0] = static_cast<char>(screenshare_track_mic);
    CHECK(!RoomRelayFrame::parseSubscribe(notControl, parsed));
    //只有帧头
    CHECK(!RoomRelayFrame::parseSubscribe(body.substr(0, kRoomRelayHeaderSize), parsed));
}

TEST_CASE("IGC5 丢帧策略的订阅：只收订阅的座位、新订阅先等关键帧、空订阅、成员离开")
{
    RoomForwardPolicy p(kIgcCap);
    const int receiver = 1;
    const int seatA = 2;
    const int seatB = 3;
    //没订阅过：两路摄像头都收
    CHECK(igcSend(p, receiver, screenshare_track_camera, seatA, kIgcWholeKey));
    CHECK(igcSend(p, receiver, screenshare_track_camera, seatB, kIgcWholeKey));
    CHECK(p.isSubscribed(receiver, seatA));

    //只订阅 seatA：原来就在收，不算新订阅，不请求关键帧；seatB 的画面不再转发，声音照常
    std::vector<RoomVideoStream> requests;
    p.setSubscription(receiver, std::vector<int>(1, seatA), kIgcT0, requests);
    CHECK(requests.empty());
    CHECK(igcSend(p, receiver, screenshare_track_camera, seatA, kIgcWhole));
    CHECK(!igcSend(p, receiver, screenshare_track_camera, seatB, kIgcWholeKey));
    CHECK(igcSend(p, receiver, screenshare_track_mic, seatB, kIgcWhole));
    //别的接收者不受影响
    CHECK(igcSend(p, 4, screenshare_track_camera, seatB, kIgcWhole));

    //再订阅 seatB：新订阅，请求一次关键帧；在关键帧到达之前非关键帧不转发
    std::vector<int> both;
    both.push_back(seatA);
    both.push_back(seatB);
    p.setSubscription(receiver, both, kIgcT0, requests);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].m_senderSeat == seatB);
    CHECK(requests[0].m_track == screenshare_track_camera);
    CHECK(!igcSend(p, receiver, screenshare_track_camera, seatB, kIgcWhole));
    CHECK(igcSend(p, receiver, screenshare_track_camera, seatB, kIgcWholeKey));
    CHECK(igcSend(p, receiver, screenshare_track_camera, seatB, kIgcWhole));

    //订阅一个还没发过画面的座位：不请求关键帧（没有这路画面），但仍先等关键帧
    p.setSubscription(receiver, std::vector<int>(1, 9), kIgcT0, requests);
    CHECK(requests.empty());
    CHECK(!igcSend(p, receiver, screenshare_track_camera, 9, kIgcWhole));
    CHECK(igcSend(p, receiver, screenshare_track_camera, 9, kIgcWholeKey));

    //空订阅：一路画面都不收，声音照常
    p.setSubscription(receiver, std::vector<int>(), kIgcT0, requests);
    CHECK(!igcSend(p, receiver, screenshare_track_camera, seatA, kIgcWholeKey));
    CHECK(igcSend(p, receiver, screenshare_track_mic, seatA, kIgcWhole));

    //接收者订阅 seatA 后 seatA 离开：别人订阅里的 seatA 被清掉；接收者自己离开后重新按「没订阅过」处理
    p.setSubscription(receiver, std::vector<int>(1, seatA), kIgcT0, requests);
    p.onMemberLeft(seatA);
    CHECK(!p.isSubscribed(receiver, seatA));
    p.onMemberLeft(receiver);
    CHECK(p.isSubscribed(receiver, seatB));
}
