/**
 *  SQLite 建库建表引导的集成测试（Suite SQM），test_sqlite_schema.cpp
 *  balloonwj@qq.com   2026-07-31
 *
 *  验证 CSqliteManager 能把一个空文件建成完整可用的库，重复执行安全，
 *  并且能给结构落后的存量库补上缺失的列。
 *
 *  除此之外还盯住三条与 MySQL 对齐的关键行为，它们错了不会报错、只会静默出问题：
 *    - COLLATE NOCASE：MySQL 默认排序规则不区分大小写，不加的话 SQLite 上
 *      Alice 与 alice 会被当成两个账号，同一个人能注册两次；
 *    - AUTOINCREMENT：t_chatmsg.f_msgid 要求全局单调递增，SQLite 不加这个关键字
 *      会在删行后复用 ID，导致新消息的 ID 比老消息小；
 *    - 更新时间触发器：MySQL 的 ON UPDATE CURRENT_TIMESTAMP 在 SQLite 里没有对应语法。
 */
#include <stdio.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"
#include "../sqliteapi/SqliteSchema.h"

//本套用例专用的临时数据库文件，与 SQD 套用的文件分开，避免互相干扰
static const char* const SCHEMA_DB_PATH = "flamingo_sqlite_schema_test.db";

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeSchemaDbFiles()
{
    std::string basePath(SCHEMA_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//取一条只返回单个数值的查询结果，失败返回 -1
static int64_t queryScalar(CDatabaseSqlite& db, const char* sql)
{
    QueryResult* pResult = db.query(sql);
    if (pResult == NULL)
        return -1;

    Field* pRow = pResult->fetch();
    int64_t value = -1;
    if (pRow != NULL)
    {
        value = static_cast<int64_t>(pRow[0].getUInt64());
    }

    delete pResult;
    return value;
}

//判断某张表里是否存在某个列
static bool hasColumn(CDatabaseSqlite& db, const char* table, const char* column)
{
    std::string sql = std::string("PRAGMA table_info(") + table + ")";
    QueryResult* pResult = db.query(sql.c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        //PRAGMA table_info 每行依次是 cid、name、type、notnull、dflt_value、pk
        if (pRow[1].getString() == column)
        {
            found = true;
            break;
        }

        if (!pResult->nextRow())
            break;
    }

    delete pResult;
    return found;
}

TEST_CASE("SQM1 空库冷启动建出全部表、索引与触发器")
{
    removeSchemaDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));

    //表数量要与生成出来的结构定义一致。sqlite_ 开头的是 SQLite 自建的内部表，须排除
    int64_t tableCount = queryScalar(
        db,
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'");
    CHECK(tableCount == sqliteapi::SCHEMA_TABLE_COUNT);

    //逐一确认每张表都真的建出来了，光看数量对不上具体是哪张漏了
    for (int i = 0; i < sqliteapi::SCHEMA_TABLE_COUNT; i++)
    {
        std::string sql = std::string(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '")
            + sqliteapi::SCHEMA_TABLES[i] + "'";
        INFO("table: " << sqliteapi::SCHEMA_TABLES[i]);
        CHECK(queryScalar(db, sql.c_str()) == 1);
    }

    //索引与触发器也要建出来。触发器只由 ON UPDATE CURRENT_TIMESTAMP 的列产生，
    //数目固定，删表 / 加表时要跟着改这个期望值（2026-08-05 删 t_user_relationship 后
    //由 9 降为 8）。索引数量把 SQLite 为唯一约束自动创建的算在内，
    //所以只断言「不少于显式创建的条数」（当前生成 49 条）
    CHECK(queryScalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger'") == 8);
    CHECK(queryScalar(db,
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name NOT LIKE 'sqlite_%'") >= 49);
}

TEST_CASE("SQM2 重复执行引导是幂等的")
{
    removeSchemaDbFiles();

    CSqliteManager first;
    REQUIRE(first.init(SCHEMA_DB_PATH));

    //先写一行数据，用来验证第二次引导不会把库重建掉
    {
        CDatabaseSqlite db;
        REQUIRE(db.initialize(SCHEMA_DB_PATH));
        REQUIRE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                           "VALUES(1001, 'zhangsan', '张三', '2026-07-31 10:00:00')"));
    }

    //第二次引导：不能报错，不能重建表，更不能丢数据
    CSqliteManager second;
    REQUIRE(second.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));
    CHECK(queryScalar(db,
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'")
        == sqliteapi::SCHEMA_TABLE_COUNT);
    CHECK(queryScalar(db, "SELECT COUNT(*) FROM t_user") == 1);
    CHECK(queryScalar(db, "SELECT f_user_id FROM t_user") == 1001);
}

TEST_CASE("SQM3 存量库缺列时自动补齐且不丢数据")
{
    removeSchemaDbFiles();

    //先手工造一个「结构落后」的库：t_user 只有自增主键和业务 ID 两列。
    //自增主键列必须自己建好 —— 它不参与补列（SQLite 不允许 ADD COLUMN 时带 PRIMARY KEY）
    {
        CDatabaseSqlite db;
        REQUIRE(db.initialize(SCHEMA_DB_PATH));
        REQUIRE(db.execute("CREATE TABLE t_user(f_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "f_user_id INTEGER NOT NULL)"));
        REQUIRE(db.execute("INSERT INTO t_user(f_user_id) VALUES(2002)"));
    }

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));

    //缺的列都要补上
    CHECK(hasColumn(db, "t_user", "f_username"));
    CHECK(hasColumn(db, "t_user", "f_nickname"));
    CHECK(hasColumn(db, "t_user", "f_usertype"));
    CHECK(hasColumn(db, "t_user", "f_enterprise_id"));

    //原有数据必须原样保留 —— 补列是 ALTER TABLE，不能变成重建表
    CHECK(queryScalar(db, "SELECT COUNT(*) FROM t_user") == 1);
    CHECK(queryScalar(db, "SELECT f_user_id FROM t_user") == 2002);

    //补出来的列对存量行取默认值；有常量默认值的列要能读到该默认值
    CHECK(queryScalar(db, "SELECT f_enterprise_id FROM t_user WHERE f_user_id = 2002") == 0);

    //补完之后应当能按完整结构正常写入
    REQUIRE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                       "VALUES(2003, 'lisi', '李四', '2026-07-31 10:00:00')"));
    CHECK(queryScalar(db, "SELECT COUNT(*) FROM t_user") == 2);
}

TEST_CASE("SQM4 关键索引确实建出来了")
{
    removeSchemaDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));

    //抽查几个业务查询强依赖的索引。索引名统一加了表名前缀，
    //因为 SQLite 的索引名是库内全局唯一的，而 idx_enterprise 这类名字在多张表里重复出现
    const char* indexNames[] = {
        "t_user_uk_f_username",         //登录账号唯一
        "t_user_idx_enterprise",        //按企业查人
        "t_chatmsg_idx_from_to",        //拉取单聊历史消息
        "t_chatmsg_idx_to_from",
        "t_department_uniq_dept_id",    //部门业务ID唯一
        "t_department_idx_path",        //物化路径查子树
        "t_group_member_idx_user",      //查我加入的群
    };

    for (size_t i = 0; i < sizeof(indexNames) / sizeof(indexNames[0]); i++)
    {
        std::string sql = std::string(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = '")
            + indexNames[i] + "'";
        INFO("index: " << indexNames[i]);
        CHECK(queryScalar(db, sql.c_str()) == 1);
    }
}

TEST_CASE("SQM5 用户名的大小写不敏感唯一约束")
{
    removeSchemaDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));

    REQUIRE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                       "VALUES(1, 'Alice', 'A', '2026-07-31 10:00:00')"));

    //MySQL 的 utf8_general_ci 不区分大小写，SQLite 默认区分。
    //给 f_username 加了 COLLATE NOCASE 才能对齐：大小写不同的同名账号必须被拒
    uint32_t affected = 0;
    int errcode = 0;
    CHECK_FALSE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                           "VALUES(2, 'alice', 'B', '2026-07-31 10:00:00')", affected, errcode));
    CHECK(errcode != 0);
    CHECK(queryScalar(db, "SELECT COUNT(*) FROM t_user") == 1);

    //大小写不敏感同样体现在等值查询上：用小写也要能查到大写写入的那行
    CHECK(queryScalar(db, "SELECT f_user_id FROM t_user WHERE f_username = 'ALICE'") == 1);

    //降级成唯一索引的原主键（f_user_id）也必须仍然唯一
    CHECK_FALSE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                           "VALUES(1, 'bob', 'B', '2026-07-31 10:00:00')", affected, errcode));
}

TEST_CASE("SQM6 消息ID单调递增且删除后不复用")
{
    removeSchemaDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));

    for (int i = 0; i < 3; i++)
    {
        REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to) VALUES(1, 2)"));
    }

    int64_t lastId = queryScalar(db, "SELECT MAX(f_msgid) FROM t_chatmsg");
    CHECK(lastId == 3);

    //删掉最后一行再插入：没有 AUTOINCREMENT 关键字的话 SQLite 会复用被删掉的 ID，
    //新消息的 msgid 就会比老消息小，破坏 t_chatmsg「全局单调递增」的约定
    REQUIRE(db.execute("DELETE FROM t_chatmsg WHERE f_msgid = 3"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to) VALUES(1, 2)"));
    CHECK(queryScalar(db, "SELECT MAX(f_msgid) FROM t_chatmsg") > lastId);
}

TEST_CASE("SQM7 更新时间触发器等价于 ON UPDATE CURRENT_TIMESTAMP")
{
    removeSchemaDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(SCHEMA_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(SCHEMA_DB_PATH));
    REQUIRE(db.execute("INSERT INTO t_user(f_user_id, f_username, f_nickname, f_register_time) "
                       "VALUES(1, 'alice', 'A', '2026-07-31 10:00:00')"));

    //插入时默认值要填上当前时间，不能是空
    QueryResult* pResult = db.query("SELECT f_update_time FROM t_user WHERE f_user_id = 1");
    REQUIRE(pResult != NULL);
    Field* pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    std::string inserted = pRow[0].getString();
    delete pResult;
    CHECK(inserted.size() == 19);

    //业务显式写入的时间要被保留，不能被触发器覆盖（MySQL 的语义也是显式赋值优先）
    REQUIRE(db.execute("UPDATE t_user SET f_update_time = '2000-01-01 00:00:00' WHERE f_user_id = 1"));
    pResult = db.query("SELECT f_update_time FROM t_user WHERE f_user_id = 1");
    REQUIRE(pResult != NULL);
    pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getString() == "2000-01-01 00:00:00");
    delete pResult;

    //改别的列时，更新时间要自动刷新
    REQUIRE(db.execute("UPDATE t_user SET f_nickname = 'AA' WHERE f_user_id = 1"));
    pResult = db.query("SELECT f_update_time FROM t_user WHERE f_user_id = 1");
    REQUIRE(pResult != NULL);
    pRow = pResult->fetch();
    REQUIRE(pRow != NULL);
    CHECK(pRow[0].getString() != "2000-01-01 00:00:00");
    CHECK(pRow[0].getString().size() == 19);
    delete pResult;
}
