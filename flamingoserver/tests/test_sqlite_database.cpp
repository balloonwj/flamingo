/**
 *  SQLite 驱动的集成测试（Suite SQD），test_sqlite_database.cpp
 *  balloonwj@qq.com   2026-07-31
 *
 *  这些用例会真的打开一个临时的 .db 文件跑 SQL。SQLite 是嵌入式的，不需要任何外部服务，
 *  所以放进默认测试集没有负担。每个用例开头都会清掉上次残留的库文件，从干净状态开始。
 *
 *  重点验证两类事情：
 *    1. SqliteQueryResult 的遍历语义与 MySQL 实现完全一致 —— 业务层那 208 处遍历循环
 *       是按 MySQL 的约定写的，两边语义只要差一点，改后端就会大面积出错；
 *    2. SqliteFunctions 注册的 MySQL 兼容函数行为正确，尤其是 NOW / FROM_UNIXTIME
 *       必须用**本地时区**（SQLite 原生的 datetime('now') 是 UTC，直接照搬会差几个时区）。
 */
#include <stdio.h>
#include <time.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "../sqliteapi/DatabaseSqlite.h"

//测试用的临时数据库文件名，落在测试程序的工作目录下
static const char* const TEST_DB_PATH = "flamingo_sqlite_test.db";

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉，
//保证每个用例都从一个全新的空库开始
static void removeTestDbFiles()
{
    std::string basePath(TEST_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//取结果集第一行第 col 列的文本值。调用前须确保结果集非空
static std::string firstCellText(QueryResult* pResult, int col)
{
    Field* pRow = pResult->fetch();
    if (pRow == NULL)
        return std::string();

    return pRow[col].getString();
}

TEST_CASE("SQD1 打开数据库并确认 WAL 日志模式生效")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    //journal_mode 必须是 wal。不是的话说明 PRAGMA 没生效，
    //并发读写会互相阻塞，与方案里设定的并发模型不符
    QueryResult* pResult = db.query("PRAGMA journal_mode");
    REQUIRE(pResult != NULL);
    std::string mode = firstCellText(pResult, 0);
    toLowerString(mode);
    CHECK(mode == "wal");
    delete pResult;

    //外键约束必须是打开的：SQLite 默认不校验外键，与 MySQL 的行为对不齐
    pResult = db.query("PRAGMA foreign_keys");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "1");
    delete pResult;
}

TEST_CASE("SQD2 建表、插入、查询的基本往返与遍历语义")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(64), f_score INTEGER)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('alice', 90)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('bob', 85)"));

    QueryResult* pResult = db.query("SELECT f_id, f_name, f_score FROM t_demo ORDER BY f_id");
    REQUIRE(pResult != NULL);

    //拿到手的结果集已经停在第一行上 —— 这是 MySQL 实现定下的约定，业务层的循环依赖它
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getInt32() == 1);
    CHECK(pRow[1].getString() == "alice");
    CHECK(pRow[2].getInt32() == 90);

    REQUIRE(pResult->nextRow());
    pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[1].getString() == "bob");
    CHECK(pRow[2].getInt32() == 85);

    //取完最后一行后 nextRow 返回 false，且 fetch 变成 NULL，业务层的循环据此结束
    CHECK_FALSE(pResult->nextRow());
    CHECK(pResult->fetch() == NULL);
    //重复调用不能出问题
    CHECK_FALSE(pResult->nextRow());

    delete pResult;
}

TEST_CASE("SQD3 结果集元信息、按列名取值与空结果集")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(64), f_score INTEGER)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('alice', 90)"));

    QueryResult* pResult = db.query("SELECT f_id, f_name, f_score FROM t_demo");
    REQUIRE(pResult != NULL);
    CHECK(pResult->getFieldCount() == 3);
    CHECK(pResult->getRowCount() == 1);
    CHECK(pResult->getField_idx("f_name") == 1);
    CHECK(pResult->getField_idx("f_not_exist") == uint32_t(-1));
    CHECK((*pResult)["f_name"].getString() == "alice");
    CHECK((*pResult)[2].getInt32() == 90);
    delete pResult;

    //空结果集：拿到手时 fetch 直接是 NULL，业务层的循环一次都不进
    QueryResult* pEmpty = db.query("SELECT f_id FROM t_demo WHERE f_id = 99999");
    REQUIRE(pEmpty != NULL);
    CHECK(pEmpty->fetch() == NULL);
    CHECK(pEmpty->getRowCount() == 0);
    delete pEmpty;
}

TEST_CASE("SQD4 NULL 字段的判定与取值")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(64), f_score INTEGER)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('carol', NULL)"));

    QueryResult* pResult = db.query("SELECT f_name, f_score FROM t_demo WHERE f_name = 'carol'");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK_FALSE(pRow[0].isNULL());
    //与 MySQL 实现一致：NULL 字段的 isNULL 为真，取值为空串（而不是未初始化的内容）
    CHECK(pRow[1].isNULL());
    CHECK(pRow[1].getString() == "");
    delete pResult;
}

TEST_CASE("SQD5 自增 ID 与 LAST_INSERT_ID 在同一条连接上一致")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(64))"));

    REQUIRE(db.execute("INSERT INTO t_demo(f_name) VALUES('dave')"));
    uint32_t firstId = db.getInsertID();
    CHECK(firstId == 1);

    REQUIRE(db.execute("INSERT INTO t_demo(f_name) VALUES('erin')"));
    uint32_t secondId = db.getInsertID();
    CHECK(secondId == 2);

    //业务层有 6 处是「先 execute 插入、紧接着 SELECT LAST_INSERT_ID() 取 ID」，
    //这条路径必须能拿到刚插入那行的 ID
    QueryResult* pResult = db.query("SELECT LAST_INSERT_ID()");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getUInt64() == static_cast<uint64_t>(secondId));
    delete pResult;
}

TEST_CASE("SQD6 NOW 与 CURDATE 的格式，以及二者的一致性")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    QueryResult* pResult = db.query("SELECT NOW(), CURDATE()");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);

    std::string now = pRow[0].getString();
    std::string today = pRow[1].getString();

    //MySQL 的 DATETIME 文本固定 19 字节 "YYYY-MM-DD HH:MM:SS"，DATE 文本固定 10 字节
    REQUIRE(now.size() == 19);
    REQUIRE(today.size() == 10);
    CHECK(now[4] == '-');
    CHECK(now[7] == '-');
    CHECK(now[10] == ' ');
    CHECK(now[13] == ':');
    CHECK(now[16] == ':');
    CHECK(today[4] == '-');
    CHECK(today[7] == '-');

    //NOW() 的日期部分就是 CURDATE()。理论上会在午夜零点整跨天时不成立，概率可忽略
    CHECK(now.substr(0, 10) == today);

    delete pResult;
}

TEST_CASE("SQD7 FROM_UNIXTIME 的格式与本地时区语义")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    //格式检查
    QueryResult* pResult = db.query("SELECT FROM_UNIXTIME(1700000000)");
    REQUIRE(pResult != NULL);
    std::string text = firstCellText(pResult, 0);
    CHECK(text.size() == 19);
    CHECK(text[4] == '-');
    CHECK(text[10] == ' ');
    delete pResult;

    //本地时区检查：把「当前秒」喂给 FROM_UNIXTIME，结果应当与 NOW() 相同。
    //这条断言能挡住「误用 UTC」这个最容易犯的错 —— 用了 UTC 就会差好几个小时。
    //只比到分钟，避免恰好跨秒时误报（跨分钟的概率约万分之一，可接受）。
    char szSql[128] = { 0 };
    snprintf(szSql, sizeof(szSql), "SELECT NOW(), FROM_UNIXTIME(%lld)",
             static_cast<long long>(time(NULL)));

    pResult = db.query(szSql);
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getString().substr(0, 16) == pRow[1].getString().substr(0, 16));
    delete pResult;
}

TEST_CASE("SQD8 GREATEST 与 CONCAT 的 MySQL 语义")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    QueryResult* pResult = db.query(
        "SELECT GREATEST(3, 7, 5), GREATEST(10, 2), GREATEST('a', 'c', 'b'), "
        "CONCAT('a', 'b', 'c'), CONCAT('id-', 42)");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getInt32() == 7);
    CHECK(pRow[1].getInt32() == 10);
    CHECK(pRow[2].getString() == "c");
    CHECK(pRow[3].getString() == "abc");
    CHECK(pRow[4].getString() == "id-42");
    delete pResult;

    //MySQL 语义：任一参数为 NULL 时，GREATEST 与 CONCAT 的结果都是 NULL
    pResult = db.query("SELECT GREATEST(1, NULL), CONCAT('x', NULL)");
    REQUIRE(pResult != NULL);
    pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].isNULL());
    CHECK(pRow[1].isNULL());
    delete pResult;

    //业务层用 GREATEST 比的是消息 ID 这类大整数，不能因为经 double 中转而丢精度
    pResult = db.query("SELECT GREATEST(9007199254740993, 9007199254740992)");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "9007199254740993");
    delete pResult;
}

TEST_CASE("SQD9 SUBSTRING 的下标语义与多字节内容")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    //MySQL 的 SUBSTRING 下标从 1 开始；负数表示从末尾倒数；下标为 0 返回空串
    QueryResult* pResult = db.query(
        "SELECT SUBSTRING('abcdef', 2), SUBSTRING('abcdef', 2, 3), "
        "SUBSTRING('abcdef', -2), SUBSTRING('abcdef', 0)");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getString() == "bcdef");
    CHECK(pRow[1].getString() == "bcd");
    CHECK(pRow[2].getString() == "ef");
    CHECK(pRow[3].getString() == "");
    delete pResult;

    //按**字符**而不是字节计数：中文每字三字节，按字节切会切出乱码
    pResult = db.query("SELECT SUBSTRING('中文测试', 2, 2)");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "文测");
    delete pResult;

    //业务层的实际用法是在物化路径上取子串（DepartmentManager 的子树改写）
    pResult = db.query("SELECT SUBSTRING('/1001/1005/', 6)");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "/1005/");
    delete pResult;
}

TEST_CASE("SQD10 INSERT IGNORE 经改写后的幂等行为")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_uniq(f_id INTEGER PRIMARY KEY, f_v INTEGER)"));

    //第一次插入正常写入
    uint32_t affected = 0;
    int errcode = 0;
    REQUIRE(db.execute("INSERT IGNORE INTO t_uniq(f_id, f_v) VALUES(1, 100)", affected, errcode));
    CHECK(affected == 1);

    //重复插入同一主键：MySQL 的 INSERT IGNORE 会静默跳过，
    //改写成 INSERT OR IGNORE 之后行为必须一致 —— 不报错、不新增行、不覆盖原值
    REQUIRE(db.execute("INSERT IGNORE INTO t_uniq(f_id, f_v) VALUES(1, 200)", affected, errcode));
    CHECK(affected == 0);
    CHECK(errcode == 0);

    QueryResult* pResult = db.query("SELECT f_v FROM t_uniq WHERE f_id = 1");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "100");
    delete pResult;

    pResult = db.query("SELECT COUNT(*) FROM t_uniq");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "1");
    delete pResult;
}

TEST_CASE("SQD11 影响行数与错误码的回填")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(64), f_score INTEGER)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('alice', 90)"));
    REQUIRE(db.execute("INSERT INTO t_demo(f_name, f_score) VALUES('bob', 85)"));

    uint32_t affected = 0;
    int errcode = 0;
    REQUIRE(db.execute("UPDATE t_demo SET f_score = 1 WHERE f_name IN('alice', 'bob')",
                       affected, errcode));
    CHECK(affected == 2);
    CHECK(errcode == 0);

    //条件匹配不到任何行时影响行数为 0，但不算失败
    REQUIRE(db.execute("UPDATE t_demo SET f_score = 2 WHERE f_name = 'nobody'", affected, errcode));
    CHECK(affected == 0);
    CHECK(errcode == 0);
}

TEST_CASE("SQD12 错误路径：语法错误的 SQL")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));

    uint32_t affected = 0;
    int errcode = 0;
    CHECK_FALSE(db.execute("THIS IS NOT VALID SQL", affected, errcode));
    //失败时必须回填非零错误码，且影响行数保持 0
    CHECK(errcode != 0);
    CHECK(affected == 0);

    //query 遇到语法错误返回 NULL
    CHECK(db.query("SELECT FROM WHERE") == NULL);

    //与 MySQL 实现一致：拿 query 去跑一条不产生结果集的语句时返回 NULL，但语句本身要执行掉
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, f_name VARCHAR(64))"));
    CHECK(db.query("INSERT INTO t_demo(f_name) VALUES('frank')") == NULL);
    QueryResult* pResult = db.query("SELECT COUNT(*) FROM t_demo");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == "1");
    delete pResult;
}

TEST_CASE("SQD13 escapeString 拼进 SQL 后能原样往返")
{
    removeTestDbFiles();

    CDatabaseSqlite db;
    REQUIRE(db.initialize(TEST_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE t_demo(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "f_name VARCHAR(256))"));

    //同时含单引号、反斜杠、双引号与中文。反斜杠是关键：按 MySQL 的规则去转义会多出一个反斜杠，
    //查回来的内容就和写进去的对不上，这条断言正是为了挡住那种写法
    std::string raw = "it's a \\path\\ \"quoted\" 中文内容";

    std::vector<char> escaped(raw.size() * 2 + 1, '\0');
    db.escapeString(&escaped[0], raw.c_str(), static_cast<uint32_t>(raw.size()));

    std::string sql = "INSERT INTO t_demo(f_name) VALUES('";
    sql += &escaped[0];
    sql += "')";
    REQUIRE(db.execute(sql.c_str()));

    QueryResult* pResult = db.query("SELECT f_name FROM t_demo");
    REQUIRE(pResult != NULL);
    CHECK(firstCellText(pResult, 0) == raw);
    delete pResult;
}
