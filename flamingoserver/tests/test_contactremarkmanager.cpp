/**
 *  联系人备注名存储管理器单元测试（Suite CRM），test_contactremarkmanager.cpp
 *
 *  覆盖 ContactRemarkManager 的四条关键行为：
 *    - 设置后能原样读回；
 *    - 空串表示清除（真的删行，不是留一个空串行）；
 *    - 备注里含单引号 / 反斜杠时能正确转义，既不破坏 SQL 也不改动内容；
 *    - 同一个人对不同目标的备注互不干扰，不同人对同一目标的备注也互不干扰
 *      （备注名是单向的）。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— ContactRemarkManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include <stdio.h>
#include <map>
#include <memory>
#include <string>

#include "doctest.h"
#include "../chatserversrc/ContactRemarkManager.h"
#include "../chatserversrc/Protocol.h"       //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与 SQD / SQM / MPM 几套用的文件分开，避免互相干扰
static const char* const REMARK_DB_PATH = "flamingo_sqlite_remark_test.db";

//本套用例统一使用的时间戳（Unix 毫秒）。备注名的落库时间不参与任何断言，
//取一个固定值即可，避免用例之间因取当前时间而结果不可复现。
static const int64_t REMARK_TEST_TIME = 1700000000000LL;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeRemarkDbFiles()
{
    std::string basePath(REMARK_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始 —— ContactRemarkManager 是单例，
//用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetRemarkDb()
{
    removeRemarkDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(REMARK_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, REMARK_DB_PATH);
}

//数一数 t_contact_remark 里现在有几行，用于"清除备注是删行而不是留空串"这类断言。
//数据库不可用时返回 -1，调用方须与 0 区分开。
static int64_t countRemarkRows()
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;

    QueryResult* pResult = pConn->query("SELECT COUNT(*) FROM t_contact_remark");
    if (pResult == NULL)
        return -1;

    int64_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

// ---------------------------------------------------------------------------
// CRM1  设置后能读回
// ---------------------------------------------------------------------------

TEST_CASE("CRM1 设置备注后能原样读回；重复设置是覆盖而不是新增一行")
{
    resetRemarkDb();
    ContactRemarkManager& mgr = Singleton<ContactRemarkManager>::Instance();

    int errorCode = -1;
    REQUIRE(mgr.setRemark(100001, 100002, "产品组同事", REMARK_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_ok);

    std::map<int32_t, std::string> remarks;
    REQUIRE(mgr.getRemarks(100001, remarks));
    REQUIRE(remarks.size() == 1);
    CHECK(remarks[100002] == "产品组同事");
    CHECK(countRemarkRows() == 1);

    //改成另一个备注：覆盖原行，不产生第二行
    REQUIRE(mgr.setRemark(100001, 100002, "小李", REMARK_TEST_TIME + 1, errorCode));
    remarks.clear();
    REQUIRE(mgr.getRemarks(100001, remarks));
    REQUIRE(remarks.size() == 1);
    CHECK(remarks[100002] == "小李");
    CHECK(countRemarkRows() == 1);

    //一条备注都没设过的人：查询成功，出参为空
    std::map<int32_t, std::string> none;
    REQUIRE(mgr.getRemarks(100009, none));
    CHECK(none.empty());
}

// ---------------------------------------------------------------------------
// CRM2  空串表示清除
// ---------------------------------------------------------------------------

TEST_CASE("CRM2 空串表示清除：删掉整行，且清除一条不存在的备注也算成功")
{
    resetRemarkDb();
    ContactRemarkManager& mgr = Singleton<ContactRemarkManager>::Instance();

    int errorCode = -1;
    REQUIRE(mgr.setRemark(100001, 100002, "小李", REMARK_TEST_TIME, errorCode));
    CHECK(countRemarkRows() == 1);

    //空串 = 清除。这里要的是"行没了"，而不是"行还在、值是空串"——
    //后者会让 getRemarks 白白搬回一堆空备注，通讯录侧还得再过滤一遍。
    REQUIRE(mgr.setRemark(100001, 100002, "", REMARK_TEST_TIME + 1, errorCode));
    CHECK(errorCode == error_code_ok);
    CHECK(countRemarkRows() == 0);

    std::map<int32_t, std::string> remarks;
    REQUIRE(mgr.getRemarks(100001, remarks));
    CHECK(remarks.empty());

    //再清一次、以及清一条压根没设过的：都算成功，客户端连点两下不该报错
    CHECK(mgr.setRemark(100001, 100002, "", REMARK_TEST_TIME + 2, errorCode));
    CHECK(mgr.setRemark(100001, 100003, "", REMARK_TEST_TIME + 2, errorCode));

    //参数非法要与"没设过"区分开：userid / targetUserId 非正，或者给自己设备注
    CHECK_FALSE(mgr.setRemark(0, 100002, "x", REMARK_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
    CHECK_FALSE(mgr.setRemark(100001, 0, "x", REMARK_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
    CHECK_FALSE(mgr.setRemark(100001, 100001, "x", REMARK_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
}

// ---------------------------------------------------------------------------
// CRM3  转义
// ---------------------------------------------------------------------------

TEST_CASE("CRM3 备注含单引号 / 反斜杠时正确转义：SQL 不被破坏，内容也不被改动")
{
    resetRemarkDb();
    ContactRemarkManager& mgr = Singleton<ContactRemarkManager>::Instance();

    //单引号是最典型的一个：不转义的话字面量会在这里提前闭合，轻则语句报错、
    //重则被拼出一条别的语句（这正是老代码 UserManager::updateMarknameInDb 的毛病）。
    int errorCode = -1;
    const std::string quoted = "O'Brien";
    REQUIRE(mgr.setRemark(100001, 100002, quoted, REMARK_TEST_TIME, errorCode));

    std::map<int32_t, std::string> remarks;
    REQUIRE(mgr.getRemarks(100001, remarks));
    REQUIRE(remarks.size() == 1);
    //读回来必须与写进去的一模一样：不能多出转义符，也不能少掉半截
    CHECK(remarks[100002] == quoted);

    //连续两个单引号、以及反斜杠（SQLite 下反斜杠是普通字符，MySQL 下是转义引导符，
    //两种后端的规则不同，都要能原样存取）
    const std::string nasty = "a''b\\c'; DROP TABLE t_contact_remark; --";
    REQUIRE(mgr.setRemark(100001, 100003, nasty, REMARK_TEST_TIME, errorCode));

    remarks.clear();
    REQUIRE(mgr.getRemarks(100001, remarks));
    REQUIRE(remarks.size() == 2);
    CHECK(remarks[100003] == nasty);

    //注入串真被当成数据存下来了，表还在、行还在
    CHECK(countRemarkRows() == 2);
}

// ---------------------------------------------------------------------------
// CRM4  互不干扰
// ---------------------------------------------------------------------------

TEST_CASE("CRM4 同一个人对不同目标、不同人对同一目标的备注互不干扰")
{
    resetRemarkDb();
    ContactRemarkManager& mgr = Singleton<ContactRemarkManager>::Instance();

    int errorCode = -1;
    //100001 给两个人各设一个备注
    REQUIRE(mgr.setRemark(100001, 100002, "小李", REMARK_TEST_TIME, errorCode));
    REQUIRE(mgr.setRemark(100001, 100003, "老王", REMARK_TEST_TIME, errorCode));
    //100004 给同一个 100002 设一个完全不同的备注 —— 备注名是单向的，
    //同一个人在不同人的通讯录里显示的名字可以完全不同
    REQUIRE(mgr.setRemark(100004, 100002, "李工", REMARK_TEST_TIME, errorCode));

    std::map<int32_t, std::string> mine;
    REQUIRE(mgr.getRemarks(100001, mine));
    REQUIRE(mine.size() == 2);
    CHECK(mine[100002] == "小李");
    CHECK(mine[100003] == "老王");

    std::map<int32_t, std::string> others;
    REQUIRE(mgr.getRemarks(100004, others));
    REQUIRE(others.size() == 1);
    CHECK(others[100002] == "李工");

    //100001 清掉对 100002 的备注，不该动到 100004 那一条，也不该动到 100003 那一条
    REQUIRE(mgr.setRemark(100001, 100002, "", REMARK_TEST_TIME + 1, errorCode));

    mine.clear();
    REQUIRE(mgr.getRemarks(100001, mine));
    REQUIRE(mine.size() == 1);
    CHECK(mine[100003] == "老王");

    others.clear();
    REQUIRE(mgr.getRemarks(100004, others));
    REQUIRE(others.size() == 1);
    CHECK(others[100002] == "李工");
}
