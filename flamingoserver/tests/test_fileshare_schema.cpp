/**
 *  文件共享相关数据表的建库测试（Suite FSDB），test_fileshare_schema.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 4 步，2026-10-02 与用户确认的用例）：
 *    - FSDB1 空库冷启动建出五张新表，t_chatmsg 带 f_audience 列且默认 0；
 *    - FSDB3 唯一约束：同一分享者的同一 clientshareid、成员表 / 可见范围表 / 配置表的主键不可重复；
 *    - FSDB4 关键索引确实建出来了；
 *    - FSDB5 共享 id 自增且删除后不复用（卡片里引用的就是它，复用会让旧卡片指向新共享）。
 *  在 SQLite 测试库上运行；MySQL 侧的表定义（mysqlmgr/MysqlManager.cpp）由 test_dbbackend 在
 *  有可用 MySQL 时覆盖。
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdio.h>
#include <string>

#include "doctest.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"
#include "../sqliteapi/SqliteSchema.h"

namespace {

//本套用例专用的临时数据库文件，与其它 SQLite 用例的文件分开，避免互相干扰
const char* const kFileShareSchemaDbPath = "flamingo_fileshare_schema_test.db";

//文件共享新增的五张表
const char* const kFileShareTables[] =
{
    "t_file_share",
    "t_file_share_member",
    "t_file_share_audit",
    "t_message_audience",
    "t_system_setting",
};

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
void removeDbFiles()
{
    std::string basePath(kFileShareSchemaDbPath);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//取一条只返回单个数值的查询结果，失败返回 -1
int64_t queryScalar(CDatabaseSqlite& db, const char* sql)
{
    QueryResult* pResult = db.query(sql);
    if (pResult == NULL)
        return -1;

    Field* pRow = pResult->fetch();
    int64_t value = -1;
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());

    delete pResult;
    return value;
}

//判断某张表里是否存在某个列
bool hasColumn(CDatabaseSqlite& db, const char* table, const char* column)
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

//判断某张表是否存在
bool hasTable(CDatabaseSqlite& db, const char* table)
{
    std::string sql = std::string("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '")
        + table + "'";
    return queryScalar(db, sql.c_str()) == 1;
}

//判断某个索引是否存在
bool hasIndex(CDatabaseSqlite& db, const char* index)
{
    std::string sql = std::string("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = '")
        + index + "'";
    return queryScalar(db, sql.c_str()) == 1;
}

//在空库上跑一遍引导并打开连接
void bootstrapEmptyDb(CDatabaseSqlite& db)
{
    removeDbFiles();
    CSqliteManager manager;
    REQUIRE(manager.init(kFileShareSchemaDbPath));
    REQUIRE(db.initialize(kFileShareSchemaDbPath));
}

} // namespace

TEST_CASE("FSDB1 空库冷启动建出文件共享的五张表，t_chatmsg 带 f_audience 列且默认 0")
{
    CDatabaseSqlite db;
    bootstrapEmptyDb(db);

    for (size_t i = 0; i < sizeof(kFileShareTables) / sizeof(kFileShareTables[0]); ++i)
    {
        INFO("table: " << kFileShareTables[i]);
        CHECK(hasTable(db, kFileShareTables[i]));
    }

    REQUIRE(hasColumn(db, "t_chatmsg", "f_audience"));

    //不指定 f_audience 插入一条消息：取默认值 0，即全员可见
    REQUIRE(db.execute("INSERT INTO t_chatmsg(f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
                       "f_create_time) VALUES('c-fsdb1', 2, 10001, 268435457, 1, 'x', 1759370000000)"));
    CHECK(queryScalar(db, "SELECT f_audience FROM t_chatmsg WHERE f_clientmsgid = 'c-fsdb1'") == 0);

    removeDbFiles();
}

TEST_CASE("FSDB3 文件共享各表的唯一约束")
{
    CDatabaseSqlite db;
    bootstrapEmptyDb(db);

    //同一分享者的同一 clientshareid 只能有一条：发起共享应答超时重发时靠它去重
    REQUIRE(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                       "VALUES('uuid-1', 10001, 1, 10002, 2)"));
    CHECK_FALSE(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                           "VALUES('uuid-1', 10001, 1, 10002, 2)"));
    //不同分享者用同一个 clientshareid 互不影响
    CHECK(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                     "VALUES('uuid-1', 10003, 1, 10002, 2)"));

    //成员表：同一共享里同一个人只能有一行
    REQUIRE(db.execute("INSERT INTO t_file_share_member(f_shareid, f_user_id, f_perms) VALUES(1, 10002, 0)"));
    CHECK_FALSE(db.execute("INSERT INTO t_file_share_member(f_shareid, f_user_id, f_perms) VALUES(1, 10002, 1)"));

    //可见范围表：同一条消息对同一个人只登记一次
    REQUIRE(db.execute("INSERT INTO t_message_audience(f_msgid, f_user_id) VALUES(500, 10002)"));
    CHECK_FALSE(db.execute("INSERT INTO t_message_audience(f_msgid, f_user_id) VALUES(500, 10002)"));

    //配置表：同一企业的同一项配置只有一行；不同企业各自一行
    REQUIRE(db.execute("INSERT INTO t_system_setting(f_enterprise_id, f_key, f_value) "
                       "VALUES(7, 'fileshare.enabled', '0')"));
    CHECK_FALSE(db.execute("INSERT INTO t_system_setting(f_enterprise_id, f_key, f_value) "
                           "VALUES(7, 'fileshare.enabled', '1')"));
    CHECK(db.execute("INSERT INTO t_system_setting(f_enterprise_id, f_key, f_value) "
                     "VALUES(8, 'fileshare.enabled', '1')"));

    removeDbFiles();
}

TEST_CASE("FSDB4 文件共享各表的关键索引确实建出来了")
{
    CDatabaseSqlite db;
    bootstrapEmptyDb(db);

    //索引名统一加了表名前缀（SQLite 的索引名库内全局唯一，见 test_sqlite_schema.cpp 的 SQM4）
    const char* indexNames[] =
    {
        "t_file_share_uk_owner_client",          //发起共享重发去重
        "t_file_share_idx_owner",                //拉取「我的共享」
        "t_file_share_idx_conv",                 //按会话列出共享（聊天窗按钮）
        "t_file_share_idx_enterprise",           //后台按企业列出共享
        "t_file_share_member_idx_user",          //拉取「共享给我的」
        "t_message_audience_idx_user",           //按人清理可见范围（成员离群等）
        "t_file_share_audit_idx_enterprise_time",//后台按企业、时间查审计
        "t_file_share_audit_idx_actor_time",     //后台按操作人、时间查审计
        "t_file_share_audit_idx_share",          //按共享查审计
    };

    for (size_t i = 0; i < sizeof(indexNames) / sizeof(indexNames[0]); ++i)
    {
        INFO("index: " << indexNames[i]);
        CHECK(hasIndex(db, indexNames[i]));
    }

    removeDbFiles();
}

TEST_CASE("FSDB5 共享 id 自增且删除后不复用")
{
    CDatabaseSqlite db;
    bootstrapEmptyDb(db);

    REQUIRE(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                       "VALUES('uuid-a', 10001, 1, 10002, 2)"));
    REQUIRE(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                       "VALUES('uuid-b', 10001, 1, 10002, 2)"));
    int64_t maxBefore = queryScalar(db, "SELECT MAX(f_shareid) FROM t_file_share");
    REQUIRE(maxBefore > 0);

    //删掉最新的那条，再插一条：新 id 必须大于被删的那个
    REQUIRE(db.execute("DELETE FROM t_file_share WHERE f_client_share_id = 'uuid-b'"));
    REQUIRE(db.execute("INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_ctype, f_cid, f_kind) "
                       "VALUES('uuid-c', 10001, 1, 10002, 2)"));
    int64_t newId = queryScalar(db, "SELECT f_shareid FROM t_file_share WHERE f_client_share_id = 'uuid-c'");
    CHECK(newId > maxBefore);

    removeDbFiles();
}
