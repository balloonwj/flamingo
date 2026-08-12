/**
 *  「特别关注」存储管理器单元测试（Suite STM），test_starredmanager.cpp
 *
 *  覆盖 StarredManager 的关键行为：
 *    - 标记后能读回，且按标记时间倒序（最近标记的排最前）；
 *    - 两个方向都幂等：重复标记不产生第二行、取消一个没标记的人不算失败；
 *    - 人数上限 kMaxStarredCount：超出时以 error_code_starred_limit 拒绝，
 *      而取消不受上限约束（否则存量超限的用户连"取消几个"都做不到）；
 *    - 不能关注自己；
 *    - removeStarredForAll 把所有人对某个人的关注一次清干净；
 *    - 不同用户的名单互不干扰。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— StarredManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/StarredManager.h"
#include "../chatserversrc/StarredProtocol.h"   //kMaxStarredCount
#include "../chatserversrc/Protocol.h"          //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与别的套用的文件分开，避免互相干扰
static const char* const STARRED_DB_PATH = "flamingo_sqlite_starred_test.db";

//本套用例统一使用的基准时间戳（Unix 毫秒）。需要区分先后顺序时在其上加偏移，
//避免取当前时间导致结果不可复现。
static const int64_t STARRED_TEST_TIME = 1700000000000LL;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeStarredDbFiles()
{
    std::string basePath(STARRED_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次：StarredManager 是单例、用例之间共享，唯一能隔离的
//就是底下这个库文件。
static void resetStarredDb()
{
    removeStarredDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 补列 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(STARRED_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, STARRED_DB_PATH);
}

TEST_CASE("STM1 标记后能读回，且按标记时间倒序")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //三个人依次标记，时间递增；读回时应当是"最近标记的排最前"
    REQUIRE(mgr.setStarred(1001, 2001, 1, STARRED_TEST_TIME + 1, code));
    REQUIRE(mgr.setStarred(1001, 2002, 1, STARRED_TEST_TIME + 2, code));
    REQUIRE(mgr.setStarred(1001, 2003, 1, STARRED_TEST_TIME + 3, code));

    std::vector<int64_t> list;
    REQUIRE(mgr.getStarred(1001, list));
    REQUIRE(list.size() == 3);
    CHECK(list[0] == 2003);
    CHECK(list[1] == 2002);
    CHECK(list[2] == 2001);

    CHECK(mgr.countStarred(1001) == 3);
}

TEST_CASE("STM2 两个方向都幂等")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //重复标记同一个人：不报错，也不产生第二行（否则会白占一个上限名额）
    REQUIRE(mgr.setStarred(1001, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1001, 2001, 1, STARRED_TEST_TIME, code));
    CHECK(mgr.countStarred(1001) == 1);

    //取消一个从没标记过的人：同样算成功，客户端连点两下不该收到莫名的报错
    REQUIRE(mgr.setStarred(1001, 9999, 0, STARRED_TEST_TIME, code));
    CHECK(code == error_code_ok);

    //取消已标记的：真的删掉
    REQUIRE(mgr.setStarred(1001, 2001, 0, STARRED_TEST_TIME, code));
    CHECK(mgr.countStarred(1001) == 0);
}

TEST_CASE("STM3 不能关注自己")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //自己的上下线自己最清楚，给自己弹提示没有意义
    CHECK_FALSE(mgr.setStarred(1001, 1001, 1, STARRED_TEST_TIME, code));
    CHECK(code == error_code_bad_request);
    CHECK(mgr.countStarred(1001) == 0);
}

TEST_CASE("STM4 人数上限：标记被拒但取消不受限")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //先标满
    for (int i = 0; i < kMaxStarredCount; ++i)
    {
        REQUIRE(mgr.setStarred(1001, 3000 + i, 1, STARRED_TEST_TIME + i, code));
    }
    CHECK(mgr.countStarred(1001) == kMaxStarredCount);

    //再标一个：被上限挡下
    CHECK_FALSE(mgr.setStarred(1001, 8888, 1, STARRED_TEST_TIME, code));
    CHECK(code == error_code_starred_limit);
    CHECK(mgr.countStarred(1001) == kMaxStarredCount);

    //已经标过的那个人重复标记不受上限影响（幂等分支在上限校验之前）
    REQUIRE(mgr.setStarred(1001, 3000, 1, STARRED_TEST_TIME, code));

    //取消不受上限约束 —— 否则存量超限的用户连"取消几个"都做不到
    REQUIRE(mgr.setStarred(1001, 3000, 0, STARRED_TEST_TIME, code));
    CHECK(mgr.countStarred(1001) == kMaxStarredCount - 1);

    //腾出名额后又能标新的了
    REQUIRE(mgr.setStarred(1001, 8888, 1, STARRED_TEST_TIME, code));
    CHECK(mgr.countStarred(1001) == kMaxStarredCount);
}

TEST_CASE("STM5 不同用户的名单互不干扰")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    REQUIRE(mgr.setStarred(1001, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1002, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1002, 2002, 1, STARRED_TEST_TIME, code));

    CHECK(mgr.countStarred(1001) == 1);
    CHECK(mgr.countStarred(1002) == 2);

    //1001 取消对 2001 的关注，不该影响 1002 对同一个人的关注
    REQUIRE(mgr.setStarred(1001, 2001, 0, STARRED_TEST_TIME, code));
    CHECK(mgr.countStarred(1001) == 0);
    CHECK(mgr.countStarred(1002) == 2);
}

TEST_CASE("STM6 removeStarredForAll 清掉所有人对某人的关注")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //三个人都关注了 2001，另外 1001 还关注着 2002
    REQUIRE(mgr.setStarred(1001, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1002, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1003, 2001, 1, STARRED_TEST_TIME, code));
    REQUIRE(mgr.setStarred(1001, 2002, 1, STARRED_TEST_TIME, code));

    //2001 离职 / 被移出企业：把别人对他的关注一并清掉，免得名单里长期挂着
    //一个永远不会上线的人
    REQUIRE(mgr.removeStarredForAll(2001));

    CHECK(mgr.countStarred(1002) == 0);
    CHECK(mgr.countStarred(1003) == 0);
    //1001 对 2002 的关注不受牵连
    std::vector<int64_t> list;
    REQUIRE(mgr.getStarred(1001, list));
    REQUIRE(list.size() == 1);
    CHECK(list[0] == 2002);

    //清一个根本没人关注的人：不算失败
    CHECK(mgr.removeStarredForAll(7777));
}

TEST_CASE("STM7 取值域兜底：userid / target / starred 非法一律拒绝")
{
    resetStarredDb();
    StarredManager& mgr = Singleton<StarredManager>::Instance();

    int code = error_code_ok;
    //正常流程里 StarredProtocol 已经拦过一道，这里防的是日后从别的路径调进来
    CHECK_FALSE(mgr.setStarred(0, 2001, 1, STARRED_TEST_TIME, code));
    CHECK(code == error_code_bad_request);

    CHECK_FALSE(mgr.setStarred(1001, 0, 1, STARRED_TEST_TIME, code));
    CHECK(code == error_code_bad_request);

    CHECK_FALSE(mgr.setStarred(1001, 2001, 2, STARRED_TEST_TIME, code));
    CHECK(code == error_code_bad_request);
}
