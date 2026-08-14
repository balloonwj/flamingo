/**
 *  文件夹传输多通道中转配对表的单元测试（Suite FRH），test_folderrelayhub.cpp
 *  覆盖 fileserver::FolderRelayHubT：同一通道两端正确配对、跨通道不串话、同一
 *  (sessionid, lane, role) 重复登记被拒、一条通道至多两端、通道序号越界被拒且不留条目、
 *  开满 1 条控制通道 + 8 条数据通道时全部配对、只有一端时取不到对端、按连接摘除后配对关系
 *  消失、按 sessionid 取全部通道供整会话连带关闭、摘空的通道与会话条目不残留、两次传输互不
 *  干扰，以及会话对象销毁后弱引用提升失败被当作对端已走。
 *  本套用例只依赖标准库：会话与连接均用本文件内的假类型代入类模板，不牵扯
 *  TcpConnection / FileSession 等网络类型。假类型刻意不叫 FakeSession / FakeConnection，
 *  以免与 tests/test_filesessionremoval.cpp 里的同名类型在同一个测试程序里撞名。
 *  balloonwj@qq.com   2026-08-13
 */
#include "doctest.h"
#include "../fileserversrc/FolderRelayHub.h"

#include <memory>
#include <string>
#include <vector>

/**
 *  假连接类型。配对表对连接的要求只有「能按对象身份相互区分」，不需要任何网络能力，
 *  故不引入 TcpConnection。编号仅供断言时辨认是哪一条连接，不参与配对逻辑。
 */
class FakeLaneConnection
{
public:
    /**
     *  构造一条假连接。
     *  @param id 连接编号，由各用例自行编排，取值从 1 开始（0 被断言辅助函数用于表示「没有会话」）
     */
    explicit FakeLaneConnection(int id)
        : m_id(id)
    {
    }

    /**
     *  取本连接的编号。
     *  @return 构造时传入的编号，仅用于断言辨认，不参与配对逻辑
     */
    int getId() const
    {
        return m_id;
    }

private:
    int m_id;   //连接编号，测试内唯一，生命周期与本对象相同
};

//假连接的共享指针，与生产代码中的 std::shared_ptr<TcpConnection> 对应
typedef std::shared_ptr<FakeLaneConnection> FakeLaneConnectionPtr;

/**
 *  假会话类型。只实现配对表所要求的 getConnectionPtr()，以此同时验证该类模板对会话类型的
 *  要求确实仅限这一个成员函数。
 */
class FakeLaneSession
{
public:
    /**
     *  构造一个归属于指定连接的假会话。
     *  @param conn 本会话所属的连接，允许传空指针（模拟连接对象已先行销毁的会话）；
     *              会话与调用方共享该连接的所有权
     */
    explicit FakeLaneSession(const FakeLaneConnectionPtr& conn)
        : m_conn(conn)
    {
    }

    /**
     *  取本会话所属的连接。签名刻意与 TcpSession::getConnectionPtr() 一致：非 const、按值返回。
     *  @return 所属连接的共享指针；构造时传入空指针则返回空指针
     */
    FakeLaneConnectionPtr getConnectionPtr()
    {
        return m_conn;
    }

private:
    FakeLaneConnectionPtr m_conn;   //本会话所属的连接，生命周期由共享指针管理
};

//假会话的共享指针，与生产代码中 FileServer 会话列表持有会话的方式一致
typedef std::shared_ptr<FakeLaneSession> FakeLaneSessionPtr;

//代入假类型后的配对表类型
typedef fileserver::FolderRelayHubT<FakeLaneSession, FakeLaneConnectionPtr> FakeFolderRelayHub;

//发送端角色。取值与 RelayToken::role_folder_sender 一致，但本套用例刻意不包含 RelayToken.h：
//配对表对角色只要求「同一通道上两端不同」，并不认识具体取值
const int kTestRoleSender = 5;
//接收端角色，取值与 RelayToken::role_folder_receiver 一致
const int kTestRoleReceiver = 6;

//一次传输每一端的通道总条数：1 条控制通道 + kFolderMaxDataLanes 条数据通道
const size_t kTestMaxLaneCount = static_cast<size_t>(fileserver::kFolderMaxLane + 1);

//「取不到会话」时约定的连接编号。假连接的编号一律从 1 开始，故不会与任何真实连接混淆
const int kNoSessionConnId = 0;

//新建一条连接并包装成会话，模拟一条中转通道连上来之后建立的会话对象
static FakeLaneSessionPtr makeLaneSession(int connId)
{
    FakeLaneConnectionPtr conn(new FakeLaneConnection(connId));
    return FakeLaneSessionPtr(new FakeLaneSession(conn));
}

//取会话所属连接的编号，用于断言取到的究竟是哪一端。
//本套用例一律比较连接编号而不是直接比较会话指针：doctest 只前向声明了 std::basic_ostream，
//把 std::shared_ptr 交给它会因输出运算符实例化失败而编不过；比较编号还有个好处是断言失败时
//能直接看出取到的是谁
static int connIdOf(const FakeLaneSessionPtr& session)
{
    if (!session)
        return kNoSessionConnId;

    FakeLaneConnectionPtr conn = session->getConnectionPtr();
    if (!conn)
        return kNoSessionConnId;

    return conn->getId();
}

//按顺序取出各端所属连接的编号，用于断言整会话连带关闭时取到的是哪几条通道、顺序对不对
static std::vector<int> collectConnIds(const std::vector<FakeLaneSessionPtr>& sessions)
{
    std::vector<int> ids;
    for (size_t i = 0; i < sessions.size(); ++i)
        ids.push_back(connIdOf(sessions[i]));

    return ids;
}

TEST_CASE("FRH1 同一通道上的发送端与接收端互为对端")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(1);
    FakeLaneSessionPtr receiver = makeLaneSession(2);

    REQUIRE(hub.join("sid-a", fileserver::kFolderControlLane, kTestRoleSender, sender));
    REQUIRE(hub.join("sid-a", fileserver::kFolderControlLane, kTestRoleReceiver, receiver));

    CHECK(hub.getEndpointCount("sid-a", fileserver::kFolderControlLane) == 2);
    CHECK(connIdOf(hub.getPeer("sid-a", fileserver::kFolderControlLane, sender->getConnectionPtr()))
          == connIdOf(receiver));
    CHECK(connIdOf(hub.getPeer("sid-a", fileserver::kFolderControlLane, receiver->getConnectionPtr()))
          == connIdOf(sender));
}

TEST_CASE("FRH2 跨通道不串话：发送端第 k 条通道的对端只能是接收端第 k 条通道")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender1 = makeLaneSession(11);
    FakeLaneSessionPtr receiver1 = makeLaneSession(12);
    FakeLaneSessionPtr sender2 = makeLaneSession(21);
    FakeLaneSessionPtr receiver2 = makeLaneSession(22);

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender1));
    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiver1));
    REQUIRE(hub.join("sid-a", 2, kTestRoleSender, sender2));
    REQUIRE(hub.join("sid-a", 2, kTestRoleReceiver, receiver2));

    //各条通道只在自己内部配对，取到的绝不能是另一条通道上的端
    CHECK(connIdOf(hub.getPeer("sid-a", 1, sender1->getConnectionPtr())) == connIdOf(receiver1));
    CHECK(connIdOf(hub.getPeer("sid-a", 1, receiver1->getConnectionPtr())) == connIdOf(sender1));
    CHECK(connIdOf(hub.getPeer("sid-a", 2, sender2->getConnectionPtr())) == connIdOf(receiver2));
    CHECK(connIdOf(hub.getPeer("sid-a", 2, receiver2->getConnectionPtr())) == connIdOf(sender2));

    //两端报的通道序号不一致（发送端在第 1 条、接收端在第 2 条）时，谁也配不上谁：
    //若配对键退化成 sessionid，这两端就会被错误地当成一对
    FakeFolderRelayHub crossHub;
    FakeLaneSessionPtr crossSender = makeLaneSession(31);
    FakeLaneSessionPtr crossReceiver = makeLaneSession(32);
    REQUIRE(crossHub.join("sid-a", 1, kTestRoleSender, crossSender));
    REQUIRE(crossHub.join("sid-a", 2, kTestRoleReceiver, crossReceiver));

    CHECK(connIdOf(crossHub.getPeer("sid-a", 1, crossSender->getConnectionPtr())) == kNoSessionConnId);
    CHECK(connIdOf(crossHub.getPeer("sid-a", 2, crossReceiver->getConnectionPtr())) == kNoSessionConnId);
}

TEST_CASE("FRH3 同一 (sessionid, lane, role) 重复登记被拒，已有配对不受影响")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(41);
    FakeLaneSessionPtr receiver = makeLaneSession(42);
    //冒名者：与已登记的一端同会话、同通道、同角色，另开一条连接报到
    FakeLaneSessionPtr impostor = makeLaneSession(43);

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender));
    CHECK_FALSE(hub.join("sid-a", 1, kTestRoleSender, impostor));
    CHECK(hub.getEndpointCount("sid-a", 1) == 1);

    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiver));
    CHECK_FALSE(hub.join("sid-a", 1, kTestRoleReceiver, impostor));
    CHECK(hub.getEndpointCount("sid-a", 1) == 2);

    //被拒的登记没有挤掉原有的两端，配对关系仍然成立
    CHECK(connIdOf(hub.getPeer("sid-a", 1, sender->getConnectionPtr())) == connIdOf(receiver));
    CHECK(connIdOf(hub.getPeer("sid-a", 1, receiver->getConnectionPtr())) == connIdOf(sender));
}

TEST_CASE("FRH4 一条通道至多两端：第三端即便角色相反也被拒")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(51);
    FakeLaneSessionPtr receiver = makeLaneSession(52);
    FakeLaneSessionPtr third = makeLaneSession(53);

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender));
    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiver));

    //两端已满，第三端无论报什么角色都不该被收下
    CHECK_FALSE(hub.join("sid-a", 1, kTestRoleSender, third));
    CHECK_FALSE(hub.join("sid-a", 1, kTestRoleReceiver, third));
    CHECK(hub.getEndpointCount("sid-a", 1) == 2);
}

TEST_CASE("FRH5 通道序号越界被拒且不在表里留下条目，边界值 0 与硬上限可登记")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr negativeLane = makeLaneSession(61);
    FakeLaneSessionPtr overLimitLane = makeLaneSession(62);
    FakeLaneSessionPtr farOverLimitLane = makeLaneSession(63);
    FakeLaneSessionPtr controlLane = makeLaneSession(64);
    FakeLaneSessionPtr lastDataLane = makeLaneSession(65);

    //越界判定本身：控制通道 0 与最后一条数据通道 kFolderMaxLane 都是合法值
    CHECK_FALSE(FakeFolderRelayHub::isValidLane(-1));
    CHECK_FALSE(FakeFolderRelayHub::isValidLane(fileserver::kFolderMaxLane + 1));
    CHECK(FakeFolderRelayHub::isValidLane(fileserver::kFolderControlLane));
    CHECK(FakeFolderRelayHub::isValidLane(fileserver::kFolderMaxLane));

    //负数与超过硬上限的通道序号一律拒绝登记
    CHECK_FALSE(hub.join("sid-a", -1, kTestRoleSender, negativeLane));
    CHECK_FALSE(hub.join("sid-a", fileserver::kFolderMaxLane + 1, kTestRoleSender, overLimitLane));
    CHECK_FALSE(hub.join("sid-a", 100, kTestRoleSender, farOverLimitLane));

    //被拒的登记不能在表里留下空的会话条目，否则反复试探就能把表撑大
    CHECK(hub.getSessionCount() == 0);

    CHECK(hub.join("sid-a", fileserver::kFolderControlLane, kTestRoleSender, controlLane));
    CHECK(hub.join("sid-a", fileserver::kFolderMaxLane, kTestRoleSender, lastDataLane));
    CHECK(hub.getLaneCount("sid-a") == 2);
}

TEST_CASE("FRH6 一次传输开满 1 条控制通道 + 8 条数据通道时全部正确配对")
{
    FakeFolderRelayHub hub;
    const std::string sid = "sid-full";
    std::vector<FakeLaneSessionPtr> senders;
    std::vector<FakeLaneSessionPtr> receivers;

    //控制通道 lane 0 与全部数据通道 lane 1..kFolderMaxLane 各建一对
    for (int lane = fileserver::kFolderControlLane; lane <= fileserver::kFolderMaxLane; ++lane)
    {
        FakeLaneSessionPtr sender = makeLaneSession(100 + lane);
        FakeLaneSessionPtr receiver = makeLaneSession(200 + lane);
        REQUIRE(hub.join(sid, lane, kTestRoleSender, sender));
        REQUIRE(hub.join(sid, lane, kTestRoleReceiver, receiver));
        senders.push_back(sender);
        receivers.push_back(receiver);
    }

    CHECK(hub.getLaneCount(sid) == kTestMaxLaneCount);

    //9 条通道两两配对，且每一端取到的都是自己那条通道上的对端
    for (int lane = fileserver::kFolderControlLane; lane <= fileserver::kFolderMaxLane; ++lane)
    {
        size_t index = static_cast<size_t>(lane - fileserver::kFolderControlLane);
        CHECK(connIdOf(hub.getPeer(sid, lane, senders[index]->getConnectionPtr()))
              == connIdOf(receivers[index]));
        CHECK(connIdOf(hub.getPeer(sid, lane, receivers[index]->getConnectionPtr()))
              == connIdOf(senders[index]));
    }

    //再多开一条就越界，必须拒绝，否则一枚令牌就能开出任意多条连接
    FakeLaneSessionPtr extra = makeLaneSession(999);
    CHECK_FALSE(hub.join(sid, fileserver::kFolderMaxLane + 1, kTestRoleSender, extra));
    CHECK(hub.getLaneCount(sid) == kTestMaxLaneCount);

    //整会话连带关闭时，18 端一个不少地取出来
    std::vector<FakeLaneSessionPtr> all;
    hub.getSessionEndpoints(sid, all);
    CHECK(all.size() == kTestMaxLaneCount * 2);
}

TEST_CASE("FRH7 通道只有一端时取不到对端，会话或通道不存在时同样取不到")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(71);

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender));

    //接收端尚未连上来，本端取不到对端
    CHECK(connIdOf(hub.getPeer("sid-a", 1, sender->getConnectionPtr())) == kNoSessionConnId);
    //同一次传输的其它通道、以及别的会话都还没有任何登记
    CHECK(connIdOf(hub.getPeer("sid-a", 2, sender->getConnectionPtr())) == kNoSessionConnId);
    CHECK(connIdOf(hub.getPeer("sid-b", 1, sender->getConnectionPtr())) == kNoSessionConnId);
}

TEST_CASE("FRH8 按连接摘除一端后配对关系消失，重复摘除同一连接返回空")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(81);
    FakeLaneSessionPtr receiver = makeLaneSession(82);

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender));
    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiver));

    //摘除发送端时返回同一条通道上的接收端供调用方连带关闭，并回填会话 id
    std::string sessionid;
    CHECK(connIdOf(hub.leaveByConn(sender->getConnectionPtr(), &sessionid)) == connIdOf(receiver));
    CHECK(sessionid == "sid-a");

    //摘除之后双方都取不到对端：留下来的接收端在这条通道上已经没有对端；被摘除的发送端
    //则因为不再登记在该通道上，也不会再取到接收端往里转发
    CHECK(connIdOf(hub.getPeer("sid-a", 1, receiver->getConnectionPtr())) == kNoSessionConnId);
    CHECK(connIdOf(hub.getPeer("sid-a", 1, sender->getConnectionPtr())) == kNoSessionConnId);
    CHECK(hub.getEndpointCount("sid-a", 1) == 1);

    //同一条连接再断一次（断开回调重入）不该再摘掉别人，也不该回填会话 id
    std::string secondSessionid;
    CHECK(connIdOf(hub.leaveByConn(sender->getConnectionPtr(), &secondSessionid)) == kNoSessionConnId);
    CHECK(secondSessionid.empty());
    CHECK(hub.getEndpointCount("sid-a", 1) == 1);
}

TEST_CASE("FRH9 按 sessionid 取出全部通道供整会话连带关闭，顺序为通道序号升序")
{
    FakeFolderRelayHub hub;
    const std::string sid = "sid-a";
    FakeLaneSessionPtr ctrlSender = makeLaneSession(90);
    FakeLaneSessionPtr ctrlReceiver = makeLaneSession(91);
    FakeLaneSessionPtr dataSender1 = makeLaneSession(92);
    FakeLaneSessionPtr dataReceiver1 = makeLaneSession(93);
    FakeLaneSessionPtr dataSender2 = makeLaneSession(94);
    FakeLaneSessionPtr dataReceiver2 = makeLaneSession(95);

    //刻意先登记第 2 条数据通道，验证结果顺序取决于通道序号而非登记先后
    REQUIRE(hub.join(sid, 2, kTestRoleSender, dataSender2));
    REQUIRE(hub.join(sid, 2, kTestRoleReceiver, dataReceiver2));
    REQUIRE(hub.join(sid, fileserver::kFolderControlLane, kTestRoleSender, ctrlSender));
    REQUIRE(hub.join(sid, fileserver::kFolderControlLane, kTestRoleReceiver, ctrlReceiver));
    REQUIRE(hub.join(sid, 1, kTestRoleSender, dataSender1));
    REQUIRE(hub.join(sid, 1, kTestRoleReceiver, dataReceiver1));

    std::vector<FakeLaneSessionPtr> all;
    hub.getSessionEndpoints(sid, all);
    std::vector<int> allIds = collectConnIds(all);
    REQUIRE(allIds.size() == 6);
    CHECK(allIds[0] == connIdOf(ctrlSender));
    CHECK(allIds[1] == connIdOf(ctrlReceiver));
    CHECK(allIds[2] == connIdOf(dataSender1));
    CHECK(allIds[3] == connIdOf(dataReceiver1));
    CHECK(allIds[4] == connIdOf(dataSender2));
    CHECK(allIds[5] == connIdOf(dataReceiver2));

    //任一条通道断开即整个文件夹传输失败：先摘掉断开的那一端，再取其余各端一并关闭
    std::string sessionid;
    CHECK(connIdOf(hub.leaveByConn(dataSender1->getConnectionPtr(), &sessionid))
          == connIdOf(dataReceiver1));
    CHECK(sessionid == sid);

    std::vector<FakeLaneSessionPtr> rest;
    hub.getSessionEndpoints(sessionid, rest);
    std::vector<int> restIds = collectConnIds(rest);
    REQUIRE(restIds.size() == 5);
    CHECK(restIds[0] == connIdOf(ctrlSender));
    CHECK(restIds[1] == connIdOf(ctrlReceiver));
    CHECK(restIds[2] == connIdOf(dataReceiver1));
    CHECK(restIds[3] == connIdOf(dataSender2));
    CHECK(restIds[4] == connIdOf(dataReceiver2));

    //连带关闭做完之后清掉整条会话，后续各条连接断开时的摘除都变成空操作
    CHECK(hub.removeSession(sid));
    CHECK(hub.getSessionCount() == 0);
    CHECK_FALSE(hub.removeSession(sid));

    std::vector<FakeLaneSessionPtr> afterRemove;
    hub.getSessionEndpoints(sid, afterRemove);
    CHECK(afterRemove.empty());
    CHECK(connIdOf(hub.leaveByConn(ctrlSender->getConnectionPtr())) == kNoSessionConnId);
}

TEST_CASE("FRH10 逐端摘除后通道条目与会话条目都不残留")
{
    FakeFolderRelayHub hub;
    const std::string sid = "sid-a";
    FakeLaneSessionPtr ctrlSender = makeLaneSession(101);
    FakeLaneSessionPtr ctrlReceiver = makeLaneSession(102);
    FakeLaneSessionPtr dataSender = makeLaneSession(103);
    FakeLaneSessionPtr dataReceiver = makeLaneSession(104);

    REQUIRE(hub.join(sid, fileserver::kFolderControlLane, kTestRoleSender, ctrlSender));
    REQUIRE(hub.join(sid, fileserver::kFolderControlLane, kTestRoleReceiver, ctrlReceiver));
    REQUIRE(hub.join(sid, 1, kTestRoleSender, dataSender));
    REQUIRE(hub.join(sid, 1, kTestRoleReceiver, dataReceiver));
    CHECK(hub.getSessionCount() == 1);
    CHECK(hub.getLaneCount(sid) == 2);

    //摘掉数据通道的第一端：通道上还剩一端，通道条目自然还在
    hub.leaveByConn(dataSender->getConnectionPtr());
    CHECK(hub.getLaneCount(sid) == 2);

    //摘掉数据通道的最后一端：该通道条目随之消失，控制通道与会话条目仍在
    hub.leaveByConn(dataReceiver->getConnectionPtr());
    CHECK(hub.getLaneCount(sid) == 1);
    CHECK(hub.getEndpointCount(sid, 1) == 0);
    CHECK(hub.getSessionCount() == 1);

    //再摘掉控制通道的两端：最后一端离开时整条会话条目也一并清除，表里不留空壳
    hub.leaveByConn(ctrlSender->getConnectionPtr());
    CHECK(hub.getSessionCount() == 1);
    hub.leaveByConn(ctrlReceiver->getConnectionPtr());
    CHECK(hub.getSessionCount() == 0);
    CHECK(hub.getLaneCount(sid) == 0);
}

TEST_CASE("FRH11 两次不同的传输互不干扰")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr senderA = makeLaneSession(111);
    FakeLaneSessionPtr receiverA = makeLaneSession(112);
    FakeLaneSessionPtr senderB = makeLaneSession(121);
    FakeLaneSessionPtr receiverB = makeLaneSession(122);

    //两次传输用同样的通道序号与角色，仅 sessionid 不同
    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, senderA));
    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiverA));
    REQUIRE(hub.join("sid-b", 1, kTestRoleSender, senderB));
    REQUIRE(hub.join("sid-b", 1, kTestRoleReceiver, receiverB));
    CHECK(hub.getSessionCount() == 2);

    CHECK(connIdOf(hub.getPeer("sid-a", 1, senderA->getConnectionPtr())) == connIdOf(receiverA));
    CHECK(connIdOf(hub.getPeer("sid-b", 1, senderB->getConnectionPtr())) == connIdOf(receiverB));

    //一次传输整体失败并清除，另一次不受影响
    std::vector<FakeLaneSessionPtr> all;
    hub.getSessionEndpoints("sid-a", all);
    CHECK(all.size() == 2);
    CHECK(hub.removeSession("sid-a"));

    CHECK(hub.getSessionCount() == 1);
    CHECK(connIdOf(hub.getPeer("sid-a", 1, senderA->getConnectionPtr())) == kNoSessionConnId);
    CHECK(connIdOf(hub.getPeer("sid-b", 1, senderB->getConnectionPtr())) == connIdOf(receiverB));
    CHECK(connIdOf(hub.getPeer("sid-b", 1, receiverB->getConnectionPtr())) == connIdOf(senderB));
}

TEST_CASE("FRH12 会话对象销毁后弱引用提升失败，被当作对端已走")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr sender = makeLaneSession(131);
    FakeLaneSessionPtr receiver = makeLaneSession(132);
    //单独留一份接收端的连接：会话对象销毁后连接对象仍在，用来验证「连接还在但会话没了」
    FakeLaneConnectionPtr receiverConn = receiver->getConnectionPtr();

    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, sender));
    REQUIRE(hub.join("sid-a", 1, kTestRoleReceiver, receiver));
    REQUIRE(connIdOf(hub.getPeer("sid-a", 1, sender->getConnectionPtr())) == connIdOf(receiver));

    //模拟接收端会话对象随连接出错被会话列表移除后销毁
    const int receiverConnId = receiverConn->getId();
    receiver.reset();

    //表里那一端的弱引用已提升不了，发送端取对端时视作对端已走
    CHECK(connIdOf(hub.getPeer("sid-a", 1, sender->getConnectionPtr())) == kNoSessionConnId);

    //整会话连带关闭时同样跳过它，本就无须再关
    std::vector<FakeLaneSessionPtr> all;
    hub.getSessionEndpoints("sid-a", all);
    std::vector<int> allIds = collectConnIds(all);
    REQUIRE(allIds.size() == 1);
    CHECK(allIds[0] == connIdOf(sender));

    //会话已销毁的端无从比对连接，按连接摘不掉（其连接编号仍是 132，说明连接对象还活着）；
    //这类残留条目由整会话清除兜底
    CHECK(receiverConnId == 132);
    CHECK(connIdOf(hub.leaveByConn(receiverConn)) == kNoSessionConnId);
    CHECK(hub.getEndpointCount("sid-a", 1) == 2);
    CHECK(hub.removeSession("sid-a"));
    CHECK(hub.getSessionCount() == 0);
}

TEST_CASE("FRH13 参数非法的登记与摘除一律被拒，且不在表里留下条目")
{
    FakeFolderRelayHub hub;
    FakeLaneSessionPtr session = makeLaneSession(141);
    FakeLaneSessionPtr nullSession;
    FakeLaneConnectionPtr nullConn;

    //会话 id 为空、会话指针为空都属于非法登记
    CHECK_FALSE(hub.join("", 1, kTestRoleSender, session));
    CHECK_FALSE(hub.join("sid-a", 1, kTestRoleSender, nullSession));
    CHECK(hub.getSessionCount() == 0);

    //空连接不参与匹配：既取不到对端，也摘不掉任何一端
    REQUIRE(hub.join("sid-a", 1, kTestRoleSender, session));
    CHECK(connIdOf(hub.getPeer("sid-a", 1, nullConn)) == kNoSessionConnId);
    CHECK(connIdOf(hub.leaveByConn(nullConn)) == kNoSessionConnId);
    CHECK(hub.getEndpointCount("sid-a", 1) == 1);
}
