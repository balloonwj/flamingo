/**
 *  屏幕共享房间中转成员表的单元测试（Suite RRH），test_relayroomhub.cpp
 *
 *  覆盖 imgserver::RelayRoomHubT（docs/屏幕共享与录制方案.md 第 11.4 节 2b-1，2026-10-02 与用户确认的用例）：
 *    - RRH1 发起人与两名观看者登记后，任一成员取「房间里的其他人」得到另外两人，按座位号升序；
 *    - RRH2 同一房间同一座位重复登记被拒，已有成员不受影响；
 *    - RRH3 座位号越界被拒；发起人只能坐 0 号，观看者不能坐 0 号，其它角色一律被拒；
 *    - RRH4 按连接摘除一人后其他人取不到他；最后一人离开后房间条目不残留；
 *    - RRH5 两个房间互不串话；
 *    - RRH6 会话对象销毁后弱引用提升失败，按「此人已走」跳过；
 *    - RRH7 按房间与座位号取某个成员。
 *  会话与连接用本文件内的假类型代入类模板，不牵扯网络类型。假类型的名字与 test_folderrelayhub.cpp 的区分开，
 *  避免在同一个测试程序里撞名。
 *
 *  典型用法：Tests.exe --test-case="RRH*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "doctest.h"
#include "../imgserversrc/RelayRoomHub.h"

#include <memory>
#include <string>
#include <vector>

//假连接：只需能按对象身份区分，编号用于断言时辨认
class FakeRoomConnection
{
public:
    explicit FakeRoomConnection(int id)
        : m_id(id)
    {
    }

    int getId() const
    {
        return m_id;
    }

private:
    int m_id;   //连接编号，测试内唯一，从 1 开始
};

typedef std::shared_ptr<FakeRoomConnection> FakeRoomConnectionPtr;

//假会话：只实现成员表要求的 getConnectionPtr()
class FakeRoomSession
{
public:
    explicit FakeRoomSession(const FakeRoomConnectionPtr& conn)
        : m_conn(conn)
    {
    }

    FakeRoomConnectionPtr getConnectionPtr()
    {
        return m_conn;
    }

private:
    FakeRoomConnectionPtr m_conn;   //本会话所属的连接
};

typedef std::shared_ptr<FakeRoomSession> FakeRoomSessionPtr;
typedef imgserver::RelayRoomHubT<FakeRoomSession, FakeRoomConnectionPtr> FakeRelayRoomHub;

namespace {

//发起人与观看者的角色
const int kRrhHost = RelayToken::role_screenshare_host;
const int kRrhMember = RelayToken::role_screenshare_member;
//一个与屏幕共享无关的角色（远程桌面控制端），用来检查其它角色被拒
const int kRrhOtherRole = RelayToken::role_controller;

//两个房间号
const char* kRrhRoomA = "ss-5127-1790943882000-8";
const char* kRrhRoomB = "ss-5308-1790943882000-9";

//「取不到会话」时约定的连接编号
const int kRrhNoSession = 0;

FakeRoomSessionPtr rrhMakeSession(int connId)
{
    FakeRoomConnectionPtr conn(new FakeRoomConnection(connId));
    return FakeRoomSessionPtr(new FakeRoomSession(conn));
}

int rrhConnIdOf(const FakeRoomSessionPtr& session)
{
    if (!session)
        return kRrhNoSession;
    FakeRoomConnectionPtr conn = session->getConnectionPtr();
    return conn ? conn->getId() : kRrhNoSession;
}

//取出各成员的座位号与连接编号，交替排列成一个序列，便于整体断言
std::vector<int> rrhSeatsAndIds(const std::vector<FakeRelayRoomHub::MemberRef>& members)
{
    std::vector<int> out;
    for (size_t i = 0; i < members.size(); ++i)
    {
        out.push_back(members[i].m_seat);
        out.push_back(rrhConnIdOf(members[i].m_session));
    }
    return out;
}

std::vector<int> rrhList(int a, int b, int c, int d)
{
    std::vector<int> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    v.push_back(d);
    return v;
}

std::vector<int> rrhList(int a, int b)
{
    std::vector<int> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

} // namespace

TEST_CASE("RRH1 发起人与两名观看者登记后，任一成员取其他人得到另外两人，按座位号升序")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr v1 = rrhMakeSession(2);
    FakeRoomSessionPtr v2 = rrhMakeSession(3);
    //故意先登记 2 号座位，检查结果按座位号而不是登记顺序排列
    REQUIRE(hub.join(kRrhRoomA, 2, kRrhMember, v2));
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, v1));
    CHECK(hub.memberCount(kRrhRoomA) == 3);

    std::vector<FakeRelayRoomHub::MemberRef> others;
    REQUIRE(hub.getOthers(kRrhRoomA, host->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(1, 2, 2, 3));
    REQUIRE(hub.getOthers(kRrhRoomA, v1->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(0, 1, 2, 3));
    REQUIRE(hub.getOthers(kRrhRoomA, v2->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(0, 1, 1, 2));
}

TEST_CASE("RRH2 同一房间同一座位重复登记被拒，已有成员不受影响")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr v1 = rrhMakeSession(2);
    FakeRoomSessionPtr intruder = rrhMakeSession(3);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, v1));

    CHECK_FALSE(hub.join(kRrhRoomA, 1, kRrhMember, intruder));
    CHECK_FALSE(hub.join(kRrhRoomA, 0, kRrhHost, intruder));
    CHECK(hub.memberCount(kRrhRoomA) == 2);
    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomA, 1)) == 2);

    //被拒的连接不在房间里，取不到其他人
    std::vector<FakeRelayRoomHub::MemberRef> others;
    CHECK_FALSE(hub.getOthers(kRrhRoomA, intruder->getConnectionPtr(), others));
    CHECK(others.empty());
}

TEST_CASE("RRH3 座位号越界被拒；发起人只能坐 0 号，观看者不能坐 0 号，其它角色被拒")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr s = rrhMakeSession(1);

    CHECK(FakeRelayRoomHub::isValidSeat(imgserver::kRoomHostSeat));
    CHECK(FakeRelayRoomHub::isValidSeat(imgserver::kRoomMaxSeat));
    CHECK_FALSE(FakeRelayRoomHub::isValidSeat(-1));
    CHECK_FALSE(FakeRelayRoomHub::isValidSeat(imgserver::kRoomMaxSeat + 1));

    CHECK_FALSE(hub.join(kRrhRoomA, -1, kRrhMember, s));
    CHECK_FALSE(hub.join(kRrhRoomA, imgserver::kRoomMaxSeat + 1, kRrhMember, s));
    CHECK_FALSE(hub.join(kRrhRoomA, 1, kRrhHost, s));
    CHECK_FALSE(hub.join(kRrhRoomA, 0, kRrhMember, s));
    CHECK_FALSE(hub.join(kRrhRoomA, 1, kRrhOtherRole, s));
    CHECK_FALSE(hub.join("", 1, kRrhMember, s));
    CHECK_FALSE(hub.join(kRrhRoomA, 1, kRrhMember, FakeRoomSessionPtr()));
    //被拒的登记不在表里留下任何条目
    CHECK(hub.roomCount() == 0);

    //边界值可以登记
    CHECK(hub.join(kRrhRoomA, imgserver::kRoomMaxSeat, kRrhMember, s));
    CHECK(hub.join(kRrhRoomA, 0, kRrhHost, rrhMakeSession(2)));
}

TEST_CASE("RRH4 按连接摘除后其他人取不到他，最后一人离开后房间条目不残留")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr v1 = rrhMakeSession(2);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, v1));

    std::string roomId;
    int seat = -1;
    REQUIRE(hub.leaveByConn(v1->getConnectionPtr(), &roomId, &seat));
    CHECK(roomId == kRrhRoomA);
    CHECK(seat == 1);
    //重复摘除同一连接返回 false
    CHECK_FALSE(hub.leaveByConn(v1->getConnectionPtr(), NULL, NULL));

    std::vector<FakeRelayRoomHub::MemberRef> others;
    REQUIRE(hub.getOthers(kRrhRoomA, host->getConnectionPtr(), others));
    CHECK(others.empty());
    //已摘除的连接再也取不到房间里的人
    CHECK_FALSE(hub.getOthers(kRrhRoomA, v1->getConnectionPtr(), others));
    //座位释放后可以重新登记（会话要有人持有，否则登记后弱引用立即失效）
    FakeRoomSessionPtr v3 = rrhMakeSession(3);
    CHECK(hub.join(kRrhRoomA, 1, kRrhMember, v3));

    REQUIRE(hub.leaveByConn(host->getConnectionPtr(), NULL, NULL));
    FakeRoomSessionPtr rejoined = hub.getMember(kRrhRoomA, 1);
    REQUIRE(rrhConnIdOf(rejoined) == 3);
    REQUIRE(hub.leaveByConn(rejoined->getConnectionPtr(), NULL, NULL));
    CHECK(hub.roomCount() == 0);
    CHECK(hub.memberCount(kRrhRoomA) == 0);
}

TEST_CASE("RRH5 两个房间互不串话")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr hostA = rrhMakeSession(1);
    FakeRoomSessionPtr viewerA = rrhMakeSession(2);
    FakeRoomSessionPtr hostB = rrhMakeSession(3);
    FakeRoomSessionPtr viewerB = rrhMakeSession(4);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, hostA));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, viewerA));
    REQUIRE(hub.join(kRrhRoomB, 0, kRrhHost, hostB));
    REQUIRE(hub.join(kRrhRoomB, 1, kRrhMember, viewerB));
    CHECK(hub.roomCount() == 2);

    std::vector<FakeRelayRoomHub::MemberRef> others;
    REQUIRE(hub.getOthers(kRrhRoomA, hostA->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(1, 2));
    REQUIRE(hub.getOthers(kRrhRoomB, hostB->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(1, 4));
    //拿着 A 房间的连接报 B 房间的房间号，取不到 B 房间的人
    CHECK_FALSE(hub.getOthers(kRrhRoomB, viewerA->getConnectionPtr(), others));
    CHECK(others.empty());
}

TEST_CASE("RRH6 会话对象销毁后弱引用提升失败，按此人已走跳过")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr v1 = rrhMakeSession(2);
    FakeRoomSessionPtr v2 = rrhMakeSession(3);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, v1));
    REQUIRE(hub.join(kRrhRoomA, 2, kRrhMember, v2));

    v1.reset();
    std::vector<FakeRelayRoomHub::MemberRef> others;
    REQUIRE(hub.getOthers(kRrhRoomA, host->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(2, 3));
    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomA, 1)) == kRrhNoSession);
    //连接已先行销毁的会话同样跳过
    FakeRoomSessionPtr orphan(new FakeRoomSession(FakeRoomConnectionPtr()));
    REQUIRE(hub.join(kRrhRoomA, 3, kRrhMember, orphan));
    REQUIRE(hub.getOthers(kRrhRoomA, host->getConnectionPtr(), others));
    CHECK(rrhSeatsAndIds(others) == rrhList(2, 3));
}

TEST_CASE("RRH7 按房间与座位号取某个成员")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr v5 = rrhMakeSession(2);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 5, kRrhMember, v5));

    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomA, 0)) == 1);
    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomA, 5)) == 2);
    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomA, 1)) == kRrhNoSession);
    CHECK(rrhConnIdOf(hub.getMember(kRrhRoomB, 0)) == kRrhNoSession);
}

TEST_CASE("RRH8 只推流的连接（第 22 期）：可登记在屏幕共享房间的非 0 座位，不能坐 0 号、不能进群通话房间；取其他成员时带角色")
{
    FakeRelayRoomHub hub;
    const int kPublisher = RelayToken::role_screenshare_publisher;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr viewer = rrhMakeSession(2);
    FakeRoomSessionPtr publisher = rrhMakeSession(3);
    FakeRoomSessionPtr publisherAtZero = rrhMakeSession(4);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, viewer));
    CHECK_FALSE(hub.join(kRrhRoomA, 0, kPublisher, publisherAtZero));
    REQUIRE(hub.join(kRrhRoomA, 5, kPublisher, publisher));
    std::vector<FakeRelayRoomHub::MemberRef> others;
    REQUIRE(hub.getOthers(kRrhRoomA, host->getConnectionPtr(), others));
    REQUIRE(others.size() == 2);
    CHECK(others[0].m_seat == 1);
    CHECK(others[0].m_role == kRrhMember);
    CHECK(others[1].m_seat == 5);
    CHECK(others[1].m_role == kPublisher);
    //群通话房间里不能混入只推流的连接
    FakeRoomSessionPtr groupMember = rrhMakeSession(5);
    FakeRoomSessionPtr groupPublisher = rrhMakeSession(6);
    REQUIRE(hub.join(kRrhRoomB, 1, RelayToken::role_groupcall_member, groupMember));
    CHECK_FALSE(hub.join(kRrhRoomB, 2, kPublisher, groupPublisher));
}

TEST_CASE("RRH9 登记时记下 userid（第 22 期）：取其他成员时带上各自的 userid，并回填本端的 userid")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr viewer = rrhMakeSession(2);
    FakeRoomSessionPtr publisher = rrhMakeSession(3);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host, 2001));
    REQUIRE(hub.join(kRrhRoomA, 1, kRrhMember, viewer, 2002));
    REQUIRE(hub.join(kRrhRoomA, 3, RelayToken::role_screenshare_publisher, publisher, 2002));
    std::vector<FakeRelayRoomHub::MemberRef> others;
    int64_t selfUserId = 0;
    REQUIRE(hub.getOthers(kRrhRoomA, publisher->getConnectionPtr(), others, &selfUserId));
    CHECK(selfUserId == 2002);
    REQUIRE(others.size() == 2);
    CHECK(others[0].m_seat == 0);
    CHECK(others[0].m_userId == 2001);
    CHECK(others[1].m_seat == 1);
    CHECK(others[1].m_userId == 2002);
    //不带 userid 登记的（老调用方式）为 0
    FakeRoomSessionPtr legacy = rrhMakeSession(4);
    REQUIRE(hub.join(kRrhRoomA, 4, kRrhMember, legacy));
    REQUIRE(hub.getOthers(kRrhRoomA, legacy->getConnectionPtr(), others, &selfUserId));
    CHECK(selfUserId == 0);
}

TEST_CASE("RRH10 列出房间的全部座位与 userid（第 26 期云端录制开始时写座位记录）：按座位号升序，房间不存在时为空")
{
    FakeRelayRoomHub hub;
    FakeRoomSessionPtr host = rrhMakeSession(1);
    FakeRoomSessionPtr publisher = rrhMakeSession(2);
    FakeRoomSessionPtr viewer = rrhMakeSession(3);
    REQUIRE(hub.join(kRrhRoomA, 0, kRrhHost, host, 2001));
    REQUIRE(hub.join(kRrhRoomA, 5, RelayToken::role_screenshare_publisher, publisher, 2002));
    REQUIRE(hub.join(kRrhRoomA, 2, kRrhMember, viewer, 2003));
    std::vector<std::pair<int, int64_t> > seats;
    hub.listSeats(kRrhRoomA, seats);
    REQUIRE(seats.size() == 3);
    CHECK(seats[0] == std::make_pair(0, (int64_t)2001));
    CHECK(seats[1] == std::make_pair(2, (int64_t)2003));
    CHECK(seats[2] == std::make_pair(5, (int64_t)2002));
    hub.listSeats(kRrhRoomB, seats);
    CHECK(seats.empty());
}
