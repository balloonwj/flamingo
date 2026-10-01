/**
 *  群置顶状态的读取测试（Suite UGP），test_pinnedgroups.cpp
 *
 *  背景：客户端「群」Tab 要把置顶的群排在最前面并加上置顶样式。群会话只有在群里有过
 *  消息之后才会出现在会话列表里（见 MessageManager::getGroupConversationBriefs），
 *  没有消息的空群置顶之后，客户端只能从群列表应答（cmd_get_user_groups）里得知它的
 *  置顶状态，服务端据 MessageManager::getPinnedConversationIds 取数。
 *
 *  这些用例真的打开一个临时库跑 SQL（默认 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL，
 *  见 test_dbbackend.h）；置顶走产品自己的写入路径 MessageManager::setConversation。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include <set>
#include <stdint.h>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../base/Singleton.h"

namespace {

// 本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）。
const char* const kUgpSqliteFile = "flamingo_sqlite_pinnedgroups_test.db";
const char* const kUgpMysqlDb    = "flamingo_test_pinnedgroups";

// 会话类型，与 Protocol.h 的 CHAT_TYPE_SINGLE / CHAT_TYPE_GROUP 取值一致。
const int kCtypeSingle = 1;
const int kCtypeGroup  = 2;

// 本人与另一名用户的 userid。
const int32_t kMe    = 100001;
const int32_t kOther = 100002;

// 几个测试群的群ID；本套用例不往 t_chatmsg 写任何消息，它们都是空群。
const int64_t kGroupA = 900001;
const int64_t kGroupB = 900002;
const int64_t kGroupC = 900003;
const int64_t kGroupD = 900004;

// 单聊对端 userid。故意取与某个群ID相同的数值，验证按会话类型区分而不是只看 cid。
const int64_t kPeerSameAsGroupA = kGroupA;

// 本套用例统一的更新时间（Unix 毫秒）。
const int64_t kUgpTime = 1700000000000LL;

// 以 userid 的身份设置某个会话的置顶状态（走 cmd_set_conversation 的写库路径）。
void setPinned(int32_t userid, int ctype, int64_t cid, bool pinned)
{
    SetConversationRequest req;
    req.ctype      = ctype;
    req.cid        = cid;
    req.has_pinned = true;
    req.pinned     = pinned;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(userid, req, kUgpTime));
}

// 以 userid 的身份删除某个会话（只改删除标记，不带置顶项）。
void deleteConversation(int32_t userid, int ctype, int64_t cid)
{
    SetConversationRequest req;
    req.ctype       = ctype;
    req.cid         = cid;
    req.has_deleted = true;
    req.deleted     = true;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(userid, req, kUgpTime));
}

} // anonymous namespace

TEST_CASE("UGP2 取本人置顶的群：含空群，不混入别人的置顶与单聊置顶")
{
    testdb::resetDb(kUgpSqliteFile, kUgpMysqlDb);
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();

    //一个群都没置顶时为空集合
    std::set<int64_t> none;
    REQUIRE(msgMgr.getPinnedConversationIds(kMe, kCtypeGroup, none));
    CHECK(none.empty());

    setPinned(kMe, kCtypeGroup, kGroupA, true);                 //本人置顶空群 A
    setPinned(kOther, kCtypeGroup, kGroupB, true);              //别人置顶群 B
    setPinned(kMe, kCtypeGroup, kGroupC, true);                 //本人置顶群 C 后又取消
    setPinned(kMe, kCtypeGroup, kGroupC, false);
    setPinned(kMe, kCtypeSingle, kPeerSameAsGroupA, true);      //本人置顶一个单聊
    setPinned(kMe, kCtypeGroup, kGroupD, true);                 //本人置顶群 D 后删除该会话
    deleteConversation(kMe, kCtypeGroup, kGroupD);

    std::set<int64_t> pinned;
    REQUIRE(msgMgr.getPinnedConversationIds(kMe, kCtypeGroup, pinned));
    CHECK(pinned.size() == 2);
    CHECK(pinned.count(kGroupA) == 1);
    CHECK(pinned.count(kGroupB) == 0);
    CHECK(pinned.count(kGroupC) == 0);
    //删除会话只是把它从会话列表里隐藏，不清除置顶标记（setConversation 只改带上的列）
    CHECK(pinned.count(kGroupD) == 1);

    //按会话类型区分：同一个数值作为单聊对端时单独查得到
    std::set<int64_t> singles;
    REQUIRE(msgMgr.getPinnedConversationIds(kMe, kCtypeSingle, singles));
    CHECK(singles.size() == 1);
    CHECK(singles.count(kPeerSameAsGroupA) == 1);

    //出参调用前不会被清空
    std::set<int64_t> accumulated;
    accumulated.insert(kGroupB);
    REQUIRE(msgMgr.getPinnedConversationIds(kMe, kCtypeGroup, accumulated));
    CHECK(accumulated.size() == 3);

    testdb::cleanupDb(kUgpSqliteFile);
}
