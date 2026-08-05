/**
 *  SqlDialect 方言片段生成的测试（Suite SQL），test_sqldialect.cpp
 *  balloonwj@qq.com   2026-08-01
 *
 *  分两部分：
 *    - 纯逻辑部分：直接指定后端类型，验证两种方言生成的片段文本正确；
 *    - 集成部分：把生成的片段拼成完整 SQL，在真实的临时 SQLite 库上跑一遍，
 *      确认它们不只是"长得像"，而是真能执行且语义正确。
 *
 *  MySQL 侧只验证片段文本 —— 跑真 SQL 需要一台 MySQL 服务，不适合放进默认测试集。
 */
#include <stdio.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "../dbapi/SqlDialect.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"

//本套用例专用的临时数据库文件
static const char* const DIALECT_DB_PATH = "flamingo_sqlite_dialect_test.db";

static void removeDialectDbFiles()
{
    std::string basePath(DIALECT_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//取一条只返回单个值的查询结果的文本形式；查不到返回空串
static std::string queryText(CDatabaseSqlite& db, const char* sql)
{
    QueryResult* pResult = db.query(sql);
    if (pResult == NULL)
        return std::string();

    Field* pRow = pResult->fetch();
    std::string value;
    if (pRow != NULL)
    {
        value = pRow[0].getString();
    }

    delete pResult;
    return value;
}

TEST_CASE("SQL1 日期区间表达式在两种方言下的文本")
{
    //MySQL 侧保持改造前的原样写法，确保这次改动对 MySQL 部署是零行为变化
    CHECK(dbapi::NowPlusHours(DB_TYPE_MYSQL, 24) == "DATE_ADD(NOW(), INTERVAL 24 HOUR)");
    CHECK(dbapi::NowMinusDays(DB_TYPE_MYSQL, 30) == "DATE_SUB(NOW(), INTERVAL 30 DAY)");
    CHECK(dbapi::TodayMinusDays(DB_TYPE_MYSQL, 13) == "CURDATE() - INTERVAL 13 DAY");

    //SQLite 侧：必须带 'localtime' 修饰符，否则用的是 UTC，会与 MySQL 的 NOW() 差出时区
    CHECK(dbapi::NowPlusHours(DB_TYPE_SQLITE, 24) == "datetime('now','localtime','+24 hours')");
    CHECK(dbapi::NowMinusDays(DB_TYPE_SQLITE, 30) == "datetime('now','localtime','-30 days')");
    CHECK(dbapi::TodayMinusDays(DB_TYPE_SQLITE, 13) == "date('now','localtime','-13 days')");
}

TEST_CASE("SQL2 日期区间表达式对负数与零的处理")
{
    //负数不能拼出 "+-3 hours" 这种 SQLite 解析不了的东西
    CHECK(dbapi::NowPlusHours(DB_TYPE_SQLITE, -3) == "datetime('now','localtime','-3 hours')");
    CHECK(dbapi::NowMinusDays(DB_TYPE_SQLITE, -7) == "datetime('now','localtime','+7 days')");
    CHECK(dbapi::TodayMinusDays(DB_TYPE_SQLITE, -1) == "date('now','localtime','+1 days')");

    //零：正负号仍要有，SQLite 认 "+0 days"
    CHECK(dbapi::TodayMinusDays(DB_TYPE_SQLITE, 0) == "date('now','localtime','+0 days')");

    //MySQL 侧负数直接落进 INTERVAL，本来就合法
    CHECK(dbapi::NowPlusHours(DB_TYPE_MYSQL, -3) == "DATE_ADD(NOW(), INTERVAL -3 HOUR)");
}

TEST_CASE("SQL3 UPSERT 子句在两种方言下的文本")
{
    //MySQL 不需要冲突列，参数被忽略
    CHECK(dbapi::UpsertClause(DB_TYPE_MYSQL, "f_userid, f_ctype, f_cid")
          == " ON DUPLICATE KEY UPDATE ");

    //SQLite 必须写明冲突目标
    CHECK(dbapi::UpsertClause(DB_TYPE_SQLITE, "f_userid, f_ctype, f_cid")
          == " ON CONFLICT(f_userid, f_ctype, f_cid) DO UPDATE SET ");
    CHECK(dbapi::UpsertClause(DB_TYPE_SQLITE, "f_msgid, f_userid")
          == " ON CONFLICT(f_msgid, f_userid) DO UPDATE SET ");

    //两端各带一个空格，调用方可以直接接在 ")" 之后、赋值表达式之前
    std::string clause = dbapi::UpsertClause(DB_TYPE_SQLITE, "f_a");
    CHECK(clause[0] == ' ');
    CHECK(clause[clause.size() - 1] == ' ');
}

TEST_CASE("SQL4 转义规则在两种方言下的差异")
{
    //MySQL：反斜杠与单引号都用反斜杠转义
    CHECK(dbapi::EscapeString(DB_TYPE_MYSQL, "it's") == "it\\'s");
    CHECK(dbapi::EscapeString(DB_TYPE_MYSQL, "C:\\path") == "C:\\\\path");

    //SQLite：单引号翻倍，反斜杠原样保留 —— 这是两者最容易写错的地方
    CHECK(dbapi::EscapeString(DB_TYPE_SQLITE, "it's") == "it''s");
    CHECK(dbapi::EscapeString(DB_TYPE_SQLITE, "C:\\path") == "C:\\path");

    //无需转义的内容与空串两边一致
    CHECK(dbapi::EscapeString(DB_TYPE_MYSQL, "plain") == "plain");
    CHECK(dbapi::EscapeString(DB_TYPE_SQLITE, "plain") == "plain");
    CHECK(dbapi::EscapeString(DB_TYPE_MYSQL, "") == "");
    CHECK(dbapi::EscapeString(DB_TYPE_SQLITE, "") == "");

    //中文不受影响
    CHECK(dbapi::EscapeString(DB_TYPE_SQLITE, "部门'名称") == "部门''名称");
}

TEST_CASE("SQL5 UPSERT 片段在真实 SQLite 库上的插入与冲突更新")
{
    removeDialectDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(DIALECT_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(DIALECT_DB_PATH));

    //复现 MessageManager::markRead 的写法：会话行可能不存在，已读位点只增不减
    std::string clause = dbapi::UpsertClause(DB_TYPE_SQLITE, "f_userid, f_ctype, f_cid");

    //第一次：走 INSERT 分支
    std::string sql = "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_readmsgid, f_update_time) "
                      "VALUES(1, 1, 100, 50, 1000)" + clause
                    + "f_readmsgid = GREATEST(f_readmsgid, 50), f_update_time = 1000";
    REQUIRE(db.execute(sql.c_str()));
    CHECK(queryText(db, "SELECT f_readmsgid FROM t_conversation WHERE f_userid = 1") == "50");

    //第二次：撞主键，走 UPDATE 分支，位点前进
    sql = "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_readmsgid, f_update_time) "
          "VALUES(1, 1, 100, 80, 2000)" + clause
        + "f_readmsgid = GREATEST(f_readmsgid, 80), f_update_time = 2000";
    REQUIRE(db.execute(sql.c_str()));
    CHECK(queryText(db, "SELECT f_readmsgid FROM t_conversation WHERE f_userid = 1") == "80");
    CHECK(queryText(db, "SELECT f_update_time FROM t_conversation WHERE f_userid = 1") == "2000");

    //第三次：位点回退的请求不能让已读位点变小 —— 这正是 GREATEST 的作用。
    //赋值右侧的裸列名 f_readmsgid 必须指代"已存在的那一行"，与 MySQL 语义一致
    sql = "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_readmsgid, f_update_time) "
          "VALUES(1, 1, 100, 20, 3000)" + clause
        + "f_readmsgid = GREATEST(f_readmsgid, 20), f_update_time = 3000";
    REQUIRE(db.execute(sql.c_str()));
    CHECK(queryText(db, "SELECT f_readmsgid FROM t_conversation WHERE f_userid = 1") == "80");

    //整个过程只应有一行
    CHECK(queryText(db, "SELECT COUNT(*) FROM t_conversation") == "1");
}

TEST_CASE("SQL6 日期区间片段在真实 SQLite 库上能筛出正确的行")
{
    removeDialectDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(DIALECT_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(DIALECT_DB_PATH));

    //造三条消息：今天、3 天前、30 天前。f_create_time 是 Unix 毫秒
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now') AS INTEGER) * 1000)"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now','-3 days') AS INTEGER) * 1000)"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now','-30 days') AS INTEGER) * 1000)"));

    //复现 AdminManager 的趋势统计：取近 14 天（今天往前 13 天的零点起）
    std::string sinceExpr = dbapi::TodayMinusDays(DB_TYPE_SQLITE, 13);
    std::string sql = "SELECT COUNT(*) FROM t_chatmsg "
                      "WHERE FROM_UNIXTIME(f_create_time/1000) >= " + sinceExpr;
    //今天与 3 天前的两条落在窗口内，30 天前那条被排除
    CHECK(queryText(db, sql.c_str()) == "2");

    //窗口收窄到 1 天：只剩今天那条
    sinceExpr = dbapi::TodayMinusDays(DB_TYPE_SQLITE, 0);
    sql = "SELECT COUNT(*) FROM t_chatmsg "
          "WHERE FROM_UNIXTIME(f_create_time/1000) >= " + sinceExpr;
    CHECK(queryText(db, sql.c_str()) == "1");

    //复现 EmployeeManager 的时间窗口：最近 7 天（精确到秒，不是自然日）
    sinceExpr = dbapi::NowMinusDays(DB_TYPE_SQLITE, 7);
    sql = "SELECT COUNT(*) FROM t_chatmsg "
          "WHERE FROM_UNIXTIME(f_create_time/1000) >= " + sinceExpr;
    CHECK(queryText(db, sql.c_str()) == "2");
}

TEST_CASE("SQL7 趋势统计的完整查询形态能在 SQLite 上跑通")
{
    removeDialectDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(DIALECT_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(DIALECT_DB_PATH));

    //今天两条、3 天前一条、30 天前一条
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now') AS INTEGER) * 1000)"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now') AS INTEGER) * 1000)"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now','-3 days') AS INTEGER) * 1000)"));
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_from, f_to, f_create_time) "
                       "VALUES(1, 2, CAST(strftime('%s','now','-30 days') AS INTEGER) * 1000)"));

    //完整照搬 AdminManager::getMessageTrend 的语句形态：
    //DATE(FROM_UNIXTIME(...)) 做分组键、COUNT(*) 计数、方言片段做时间下界。
    //这一串里 DATE() 是 SQLite 内置函数、FROM_UNIXTIME() 是我们注册的兼容函数，
    //两者能否套在一起用、GROUP BY 别名能否生效，都得真跑一遍才知道
    std::string sinceExpr = dbapi::TodayMinusDays(DB_TYPE_SQLITE, 13);
    std::string sql = "SELECT DATE(FROM_UNIXTIME(f_create_time/1000)) d, COUNT(*) c "
                      "FROM t_chatmsg "
                      "WHERE FROM_UNIXTIME(f_create_time/1000) >= " + sinceExpr +
                      " GROUP BY d";

    QueryResult* pResult = db.query(sql.c_str());
    REQUIRE(pResult != NULL);

    //窗口内应当分出两组：今天（2 条）与 3 天前（1 条）；30 天前那条被时间下界排除
    int groupCount = 0;
    int totalCount = 0;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        //分组键必须是 "YYYY-MM-DD" 形式，AdminManager 拿它与日期轴逐一比对
        CHECK(pRow[0].getString().size() == 10);
        groupCount++;
        totalCount += pRow[1].getInt32();

        if (!pResult->nextRow())
            break;
    }
    delete pResult;

    CHECK(groupCount == 2);
    CHECK(totalCount == 3);
}

TEST_CASE("SQL8 转义后的内容拼进 SQL 能原样往返")
{
    removeDialectDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(DIALECT_DB_PATH));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(DIALECT_DB_PATH));

    //这条路径对应各 Manager 里原先手写 EscapeSql 的那 60 多处调用：
    //部门名、企业名、公告标题等都取自客户端，含引号与反斜杠是完全可能的。
    //按 MySQL 规则转义的话，'\'' 会让字面量提前闭合，语句直接报错
    std::string raw = "研发部'一组\\北京";
    std::string escaped = dbapi::EscapeString(DB_TYPE_SQLITE, raw);

    std::string sql = "INSERT INTO t_department(f_dept_id, f_dept_name, f_create_time) "
                      "VALUES(1, '" + escaped + "', '2026-08-01 10:00:00')";
    REQUIRE(db.execute(sql.c_str()));

    CHECK(queryText(db, "SELECT f_dept_name FROM t_department WHERE f_dept_id = 1") == raw);
}
