/**
 *  控制台仪表板统计的作用域测试（Suite DSB），test_dashboardstats.cpp
 *
 *  dashboard/stats.do 由 AdminManager::getDashboardStats 计算。2026-10-04 之前只有员工数按
 *  企业过滤，企业数（总数 / 活跃 / 待审核）与今日审计、今日告警都是全平台口径，企业管理员在
 *  仪表板上能看到整个平台有几家企业、全平台当天有多少条审计；而审计列表 audit/list.do 对企业
 *  管理员是按企业过滤的，两处口径不一致。现在企业管理员的各项统计都只算本企业：
 *    - DSB1：平台超管（enterpriseId = 0）统计全平台；
 *    - DSB2：企业管理员只统计本企业 —— 企业数按本企业的状态算（总数 1，活跃 / 待审核
 *            按状态取 0 或 1），员工数、今日审计、今日告警都只算本企业的记录。
 *
 *  每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上，再直接写入企业与员工，
 *  审计记录经 AdminManager::writeAudit 写入（与产品代码同一条路径）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <memory>
#include <string>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/AdminManager.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const DSB_DB_PATH = "flamingo_sqlite_dashboardstats_test.db";
static const char* const DSB_MYSQL_DB = "flamingo_test_dashboardstats";

//三家测试企业的业务 ID，分别处于三种状态（t_enterprise.f_status：0 待审核 / 1 活跃 / 2 已停用）
static const int64_t DSB_ENT_ACTIVE   = 7;
static const int64_t DSB_ENT_PENDING  = 8;
static const int64_t DSB_ENT_DISABLED = 9;
static const int DSB_STATUS_PENDING  = 0;
static const int DSB_STATUS_ACTIVE   = 1;
static const int DSB_STATUS_DISABLED = 2;

//平台级操作的企业 ID 口径：审计表里 f_enterprise_id 为 0 表示不关联任何企业
static const int64_t DSB_PLATFORM = 0;

//审计级别：0 info / 1 warn / 2 danger（t_admin_audit_log.f_severity）
static const int32_t DSB_SEV_INFO   = 0;
static const int32_t DSB_SEV_WARN   = 1;
static const int32_t DSB_SEV_DANGER = 2;

//直接插入一家企业
//  enterpriseId：企业业务 ID；name：企业全称（唯一）；status：企业状态
static void dsbInsertEnterprise(int64_t enterpriseId, const char* name, int status)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    const std::string sql =
        "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_status, f_create_time) VALUES("
        + std::to_string(enterpriseId) + ", '" + name + "', " + std::to_string(status) + ", NOW())";
    REQUIRE(pConn->execute(sql.c_str()));
}

//直接插入一名员工
//  userId：用户 ID（唯一）；enterpriseId：所属企业
static void dsbInsertUser(int64_t userId, int64_t enterpriseId)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    const std::string id = std::to_string(userId);
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    const std::string sql =
        "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_enterprise_id, f_register_time) VALUES("
        + id + ", 'dsb" + id + "', 'dsb" + id + "', '', " + std::to_string(enterpriseId) + ", NOW())";
    REQUIRE(pConn->execute(sql.c_str()));
}

//写一条今天的审计记录
//  enterpriseId：关联企业，平台级操作传 0；severity：级别
static void dsbAudit(int64_t enterpriseId, int32_t severity)
{
    Singleton<AdminManager>::Instance().writeAudit(1, "dsb", "127.0.0.1", enterpriseId,
                                                    "dsb.test", "target", severity, "detail");
}

//造一份三家企业、四名员工、五条今日审计的数据：
//  活跃企业：员工 2 名，审计 info + warn；待审核企业：员工 1 名，审计 danger；
//  已停用企业：员工 1 名，没有审计；平台级：审计 warn 一条。
static void dsbSeed()
{
    dsbInsertEnterprise(DSB_ENT_ACTIVE, "DSB 活跃企业", DSB_STATUS_ACTIVE);
    dsbInsertEnterprise(DSB_ENT_PENDING, "DSB 待审核企业", DSB_STATUS_PENDING);
    dsbInsertEnterprise(DSB_ENT_DISABLED, "DSB 已停用企业", DSB_STATUS_DISABLED);

    dsbInsertUser(101, DSB_ENT_ACTIVE);
    dsbInsertUser(102, DSB_ENT_ACTIVE);
    dsbInsertUser(103, DSB_ENT_PENDING);
    dsbInsertUser(104, DSB_ENT_DISABLED);

    dsbAudit(DSB_ENT_ACTIVE, DSB_SEV_INFO);
    dsbAudit(DSB_ENT_ACTIVE, DSB_SEV_WARN);
    dsbAudit(DSB_ENT_PENDING, DSB_SEV_DANGER);
    dsbAudit(DSB_PLATFORM, DSB_SEV_WARN);
}

TEST_CASE("DSB1 平台超管的仪表板统计为全平台口径")
{
    testdb::resetDb(DSB_DB_PATH, DSB_MYSQL_DB);
    dsbSeed();

    DashboardStats st;
    Singleton<AdminManager>::Instance().getDashboardStats(DSB_PLATFORM, st);
    CHECK(st.enterprisesTotal == 3);
    CHECK(st.enterprisesActive == 1);
    CHECK(st.enterprisesPending == 1);
    CHECK(st.usersTotal == 4);
    CHECK(st.auditToday == 4);
    CHECK(st.auditTodayWarn == 3);

    testdb::cleanupDb(DSB_DB_PATH);
}

TEST_CASE("DSB2 企业管理员的仪表板统计只算本企业")
{
    testdb::resetDb(DSB_DB_PATH, DSB_MYSQL_DB);
    dsbSeed();

    //活跃企业：企业数按自身状态算，员工 2 名，审计 2 条，其中告警 1 条
    DashboardStats active;
    Singleton<AdminManager>::Instance().getDashboardStats(DSB_ENT_ACTIVE, active);
    CHECK(active.enterprisesTotal == 1);
    CHECK(active.enterprisesActive == 1);
    CHECK(active.enterprisesPending == 0);
    CHECK(active.usersTotal == 2);
    CHECK(active.auditToday == 2);
    CHECK(active.auditTodayWarn == 1);

    //待审核企业：审计 1 条（danger 也算告警）
    DashboardStats pending;
    Singleton<AdminManager>::Instance().getDashboardStats(DSB_ENT_PENDING, pending);
    CHECK(pending.enterprisesTotal == 1);
    CHECK(pending.enterprisesActive == 0);
    CHECK(pending.enterprisesPending == 1);
    CHECK(pending.usersTotal == 1);
    CHECK(pending.auditToday == 1);
    CHECK(pending.auditTodayWarn == 1);

    //已停用企业：没有审计，平台级的那条告警也不算进来
    DashboardStats disabled;
    Singleton<AdminManager>::Instance().getDashboardStats(DSB_ENT_DISABLED, disabled);
    CHECK(disabled.enterprisesTotal == 1);
    CHECK(disabled.enterprisesActive == 0);
    CHECK(disabled.enterprisesPending == 0);
    CHECK(disabled.usersTotal == 1);
    CHECK(disabled.auditToday == 0);
    CHECK(disabled.auditTodayWarn == 0);

    testdb::cleanupDb(DSB_DB_PATH);
}
