/**
 *  文件服务器会话按连接移除的单元测试（Suite FSR），test_filesessionremoval.cpp
 *  覆盖 fileserver::RemoveSessionByConnection：匹配首项 / 中间项 / 末项时只移除该项、
 *  其余元素与相对顺序不变（缺陷版本无论断开哪条连接都移除首项，正是本套用例锁定的行为）、
 *  给定连接不在列表中时列表原样保留、空列表不崩溃、连接指针为空的会话不被误删，
 *  以及同一连接重复出现时只移除第一个匹配项。
 *  本套用例只依赖标准库：会话与连接均用本文件内的假类型代入函数模板，
 *  不牵扯 TcpConnection / FileSession 等网络类型。
 *  balloonwj@qq.com   2026-08-13
 */
#include "doctest.h"
#include "../fileserversrc/FileSessionRemoval.h"

#include <list>
#include <memory>
#include <vector>

/**
 *  假连接类型。本测试只需要「能按对象身份相互区分的实体」，不需要任何网络能力，
 *  故不引入 TcpConnection。编号仅供断言时辨认列表里剩下的是哪几条连接。
 */
class FakeConnection
{
public:
    /**
     *  构造一条假连接。
     *  @param id 连接编号，由各用例自行编排，取值从 1 开始（0 被断言辅助函数用于表示空连接）
     */
    explicit FakeConnection(int id)
        : m_id(id)
    {
    }

    /**
     *  取本连接的编号。
     *  @return 构造时传入的编号，仅用于断言辨认，不参与匹配逻辑
     */
    int getId() const
    {
        return m_id;
    }

private:
    int m_id;   //连接编号，测试内唯一，生命周期与本对象相同
};

//假连接的共享指针，与生产代码中的 std::shared_ptr<TcpConnection> 对应
typedef std::shared_ptr<FakeConnection> FakeConnectionPtr;

/**
 *  假会话类型。只实现 RemoveSessionByConnection 所要求的 getConnectionPtr()，
 *  以此同时验证该函数模板对会话类型的要求确实仅限这一个成员函数。
 */
class FakeSession
{
public:
    /**
     *  构造一个归属于指定连接的假会话。
     *  @param conn 本会话所属的连接，允许传空指针（模拟连接对象已先行销毁的会话）；
     *              会话与调用方共享该连接的所有权
     */
    explicit FakeSession(const FakeConnectionPtr& conn)
        : m_conn(conn)
    {
    }

    /**
     *  取本会话所属的连接。签名刻意与 TcpSession::getConnectionPtr() 一致：非 const、按值返回。
     *  @return 所属连接的共享指针；构造时传入空指针则返回空指针
     */
    FakeConnectionPtr getConnectionPtr()
    {
        return m_conn;
    }

private:
    FakeConnectionPtr m_conn;   //本会话所属的连接，生命周期由共享指针管理
};

//假会话列表，与 FileServer::m_sessions 的容器形态一致
typedef std::list<std::shared_ptr<FakeSession>> FakeSessionList;

//把一条连接包装成会话追加到列表尾部，模拟 FileServer::onConnected 登记新会话的动作
static void appendSession(FakeSessionList& sessions, const FakeConnectionPtr& conn)
{
    sessions.push_back(std::shared_ptr<FakeSession>(new FakeSession(conn)));
}

//按列表顺序取出各会话所属连接的编号，用于断言剩下的是哪几条会话、相对顺序有没有被打乱。
//连接指针为空的会话记为 0，假连接的编号从 1 开始，两者不会混淆
static std::vector<int> collectConnectionIds(FakeSessionList& sessions)
{
    std::vector<int> ids;
    for (FakeSessionList::iterator iter = sessions.begin(); iter != sessions.end(); ++iter)
    {
        FakeConnectionPtr conn = (*iter)->getConnectionPtr();
        if (conn)
            ids.push_back(conn->getId());
        else
            ids.push_back(0);
    }

    return ids;
}

TEST_CASE("FSR1 匹配首项时移除首项，其余会话保持原有顺序")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeConnectionPtr conn2(new FakeConnection(2));
    FakeConnectionPtr conn3(new FakeConnection(3));

    FakeSessionList sessions;
    appendSession(sessions, conn1);
    appendSession(sessions, conn2);
    appendSession(sessions, conn3);

    REQUIRE(fileserver::RemoveSessionByConnection(sessions, conn1));

    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 2);
    CHECK(ids[1] == 3);
}

TEST_CASE("FSR2 匹配中间一项时只移除该项，首项与末项都保留")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeConnectionPtr conn2(new FakeConnection(2));
    FakeConnectionPtr conn3(new FakeConnection(3));

    FakeSessionList sessions;
    appendSession(sessions, conn1);
    appendSession(sessions, conn2);
    appendSession(sessions, conn3);

    //断开的是中间那条连接。缺陷版本在此处移除的是首项（编号 1），
    //于是最早建立的会话被误删，而真正断开的那条会话残留在表里
    REQUIRE(fileserver::RemoveSessionByConnection(sessions, conn2));

    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 3);
}

TEST_CASE("FSR3 匹配末项时只移除末项，先建立的会话不受影响")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeConnectionPtr conn2(new FakeConnection(2));
    FakeConnectionPtr conn3(new FakeConnection(3));

    FakeSessionList sessions;
    appendSession(sessions, conn1);
    appendSession(sessions, conn2);
    appendSession(sessions, conn3);

    REQUIRE(fileserver::RemoveSessionByConnection(sessions, conn3));

    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
}

TEST_CASE("FSR4 给定连接不在列表中时返回 false，且不移除任何元素")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeConnectionPtr conn2(new FakeConnection(2));
    //从未登记进会话列表的连接，模拟会话创建失败或已被提前移除的情形
    FakeConnectionPtr strangerConn(new FakeConnection(9));

    FakeSessionList sessions;
    appendSession(sessions, conn1);
    appendSession(sessions, conn2);

    CHECK_FALSE(fileserver::RemoveSessionByConnection(sessions, strangerConn));

    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
}

TEST_CASE("FSR5 空列表返回 false 且不崩溃")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeSessionList sessions;

    CHECK_FALSE(fileserver::RemoveSessionByConnection(sessions, conn1));
    CHECK(sessions.empty());
}

TEST_CASE("FSR6 连接指针为空的会话被跳过而不中断查找，传入空连接不移除任何元素")
{
    FakeConnectionPtr nullConn;
    FakeConnectionPtr conn2(new FakeConnection(2));

    FakeSessionList sessions;
    //首项的连接对象已先行销毁，其 getConnectionPtr() 返回空指针
    appendSession(sessions, nullConn);
    appendSession(sessions, conn2);

    //查找不因遇到空连接的会话而中断，排在其后的匹配项仍能被找到
    REQUIRE(fileserver::RemoveSessionByConnection(sessions, conn2));
    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 0);

    //传入空连接视为无匹配：不能把连接指针恰好为空的那个会话当成匹配项删掉
    CHECK_FALSE(fileserver::RemoveSessionByConnection(sessions, nullConn));
    CHECK(sessions.size() == 1);
}

TEST_CASE("FSR7 同一连接重复登记时只移除第一个匹配项")
{
    FakeConnectionPtr conn1(new FakeConnection(1));
    FakeConnectionPtr conn2(new FakeConnection(2));

    FakeSessionList sessions;
    appendSession(sessions, conn1);
    appendSession(sessions, conn2);
    appendSession(sessions, conn2);

    REQUIRE(fileserver::RemoveSessionByConnection(sessions, conn2));

    std::vector<int> ids = collectConnectionIds(sessions);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
}
