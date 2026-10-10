/**
 *  企业列表「更多筛选」的服务端测试（Suite ELF），test_enterpriselistfilter.cpp
 *
 *  admin 控制台的 enterprise/list.do 与 enterprise/export.do 原先只按关键字与状态筛选。
 *  2026-10-04 起增加三项可选条件（EnterpriseManager.h 的 EnterpriseListFilter）：行业（精确
 *  匹配）、套餐（-1 表示不限）、开通起止日期（YYYY-MM-DD，起止当天都包含；格式不对时不参与
 *  筛选）。本套用例对着临时库真的跑 EnterpriseManager 的 SQL：
 *    - ELF1：按行业精确匹配，不会把名字里含该行业的、或行业只是前缀相同的企业算进来；
 *    - ELF2：按套餐筛选，-1 时不限；
 *    - ELF3：开通日期区间包含起止两天，只给一端时只限制那一端，格式不对的日期被忽略；
 *    - ELF4：各条件与关键字、状态同时生效；不带新条件时与原来的 (keyword, status) 版本结果一致，
 *            导出 CSV 也按同样的条件筛选。
 *
 *  每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上，企业直接写入 t_enterprise。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/EnterpriseManager.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const ELF_DB_PATH = "flamingo_sqlite_enterpriselistfilter_test.db";
static const char* const ELF_MYSQL_DB = "flamingo_test_enterpriselistfilter";

//一页取够：本套用例的企业数远小于它
static const int32_t ELF_PAGE_SIZE = 50;

//企业状态与套餐编码（t_enterprise.f_status / f_plan）
static const int32_t ELF_STATUS_PENDING = 0;
static const int32_t ELF_STATUS_ACTIVE  = 1;
static const int32_t ELF_PLAN_PRO       = 1;
static const int32_t ELF_PLAN_ENT       = 2;

//直接插入一家企业
//  id：企业业务 ID；name：全称（唯一）；industry：行业；plan：套餐；status：状态；
//  openDate：开通时间，形如 "2026-03-01 00:00:00"
static void elfInsert(int64_t id, const char* name, const char* industry, int32_t plan, int32_t status,
                      const char* openDate)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    const std::string sql =
        "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_industry, f_plan, f_status, f_open_date, f_create_time) "
        "VALUES(" + std::to_string(id) + ", '" + name + "', '" + industry + "', " + std::to_string(plan) + ", "
        + std::to_string(status) + ", '" + openDate + "', NOW())";
    REQUIRE(pConn->execute(sql.c_str()));
}

//造五家企业：行业、套餐、状态、开通时间各不相同，开通时间卡在区间边界上
static void elfSeed()
{
    elfInsert(1, "甲科技", "软件与信息技术", ELF_PLAN_PRO, ELF_STATUS_ACTIVE,  "2026-02-28 23:59:59");
    elfInsert(2, "乙银行", "金融服务",       ELF_PLAN_ENT, ELF_STATUS_ACTIVE,  "2026-03-01 00:00:00");
    elfInsert(3, "丙软件", "软件与信息技术", ELF_PLAN_ENT, ELF_STATUS_PENDING, "2026-03-31 23:59:59");
    elfInsert(4, "丁金融服务中心", "金融服务外包", ELF_PLAN_PRO, ELF_STATUS_ACTIVE, "2026-04-01 00:00:00");
    elfInsert(5, "戊物流", "运输与仓储",     ELF_PLAN_PRO, ELF_STATUS_PENDING, "2026-03-15 12:00:00");
}

//按筛选条件取第一页，返回企业 ID（升序）并核对总条数与列表条数一致
static std::vector<int64_t> elfIds(const EnterpriseListFilter& filter)
{
    int32_t total = -1;
    std::vector<EnterpriseInfo> list;
    Singleton<EnterpriseManager>::Instance().listEnterprises(filter, 1, ELF_PAGE_SIZE, total, list);
    CHECK(total == static_cast<int32_t>(list.size()));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < list.size(); ++i)
        ids.push_back(list[i].enterpriseId);
    std::sort(ids.begin(), ids.end());
    return ids;
}

//把若干 ID 拼成期望值，便于 CHECK 比较
static std::vector<int64_t> elfExpect(std::initializer_list<int64_t> ids)
{
    return std::vector<int64_t>(ids);
}

TEST_CASE("ELF1 按行业精确匹配")
{
    testdb::resetDb(ELF_DB_PATH, ELF_MYSQL_DB);
    elfSeed();

    EnterpriseListFilter f;
    f.industry = "软件与信息技术";
    CHECK(elfIds(f) == elfExpect({ 1, 3 }));

    //「金融服务」不包括行业为「金融服务外包」、名字里带「金融服务」的 4 号
    f.industry = "金融服务";
    CHECK(elfIds(f) == elfExpect({ 2 }));

    //没有这个行业
    f.industry = "教育";
    CHECK(elfIds(f).empty());

    testdb::cleanupDb(ELF_DB_PATH);
}

TEST_CASE("ELF2 按套餐筛选，-1 时不限")
{
    testdb::resetDb(ELF_DB_PATH, ELF_MYSQL_DB);
    elfSeed();

    EnterpriseListFilter f;
    f.plan = ELF_PLAN_ENT;
    CHECK(elfIds(f) == elfExpect({ 2, 3 }));
    f.plan = ELF_PLAN_PRO;
    CHECK(elfIds(f) == elfExpect({ 1, 4, 5 }));
    f.plan = -1;
    CHECK(elfIds(f) == elfExpect({ 1, 2, 3, 4, 5 }));

    testdb::cleanupDb(ELF_DB_PATH);
}

TEST_CASE("ELF3 开通日期区间包含起止两天，格式不对的日期被忽略")
{
    testdb::resetDb(ELF_DB_PATH, ELF_MYSQL_DB);
    elfSeed();

    //3 月整月：3-01 00:00:00 与 3-31 23:59:59 都在内，2-28 23:59:59 与 4-01 00:00:00 不在
    EnterpriseListFilter f;
    f.openFrom = "2026-03-01";
    f.openTo   = "2026-03-31";
    CHECK(elfIds(f) == elfExpect({ 2, 3, 5 }));

    //只给起始日期
    EnterpriseListFilter fromOnly;
    fromOnly.openFrom = "2026-03-31";
    CHECK(elfIds(fromOnly) == elfExpect({ 3, 4 }));

    //只给截止日期
    EnterpriseListFilter toOnly;
    toOnly.openTo = "2026-02-28";
    CHECK(elfIds(toOnly) == elfExpect({ 1 }));

    //同一天：起止相同时取这一整天
    EnterpriseListFilter sameDay;
    sameDay.openFrom = "2026-03-15";
    sameDay.openTo   = "2026-03-15";
    CHECK(elfIds(sameDay) == elfExpect({ 5 }));

    //格式不对（含注入企图）的日期不参与筛选，结果与不带该条件相同
    EnterpriseListFilter bad;
    bad.openFrom = "2026/03/01";
    bad.openTo   = "2026-03-31' OR '1'='1";
    CHECK(elfIds(bad) == elfExpect({ 1, 2, 3, 4, 5 }));
    bad.openFrom = "2026-13-01";
    bad.openTo   = "2026-02-30x";
    CHECK(elfIds(bad) == elfExpect({ 1, 2, 3, 4, 5 }));

    testdb::cleanupDb(ELF_DB_PATH);
}

TEST_CASE("ELF4 组合筛选，与原有的关键字、状态同时生效")
{
    testdb::resetDb(ELF_DB_PATH, ELF_MYSQL_DB);
    elfSeed();

    //不带新条件：与原来的 (keyword, status) 版本结果一致
    int32_t oldTotal = -1;
    std::vector<EnterpriseInfo> oldList;
    Singleton<EnterpriseManager>::Instance().listEnterprises("软件", ELF_STATUS_ACTIVE, 1, ELF_PAGE_SIZE,
                                                             oldTotal, oldList);
    EnterpriseListFilter legacy;
    legacy.keyword = "软件";
    legacy.status  = ELF_STATUS_ACTIVE;
    const std::vector<int64_t> legacyIds = elfIds(legacy);
    CHECK(oldTotal == static_cast<int32_t>(legacyIds.size()));
    CHECK(legacyIds == elfExpect({ 1 }));   //关键字也匹配行业，1 号行业含「软件」且活跃

    //行业 + 套餐 + 日期 + 状态同时生效
    EnterpriseListFilter all;
    all.industry = "软件与信息技术";
    all.plan     = ELF_PLAN_ENT;
    all.openFrom = "2026-03-01";
    all.openTo   = "2026-03-31";
    CHECK(elfIds(all) == elfExpect({ 3 }));
    all.status = ELF_STATUS_ACTIVE;
    CHECK(elfIds(all).empty());

    //导出 CSV 按同样的条件筛选：表头一行 + 命中的企业各一行
    EnterpriseListFilter exp;
    exp.plan = ELF_PLAN_ENT;
    std::string csv;
    Singleton<EnterpriseManager>::Instance().exportEnterprisesCsv(exp, csv);
    const size_t lines = static_cast<size_t>(std::count(csv.begin(), csv.end(), '\n'));
    CHECK(lines == 3);
    CHECK(csv.find("乙银行") != std::string::npos);
    CHECK(csv.find("丙软件") != std::string::npos);
    CHECK(csv.find("甲科技") == std::string::npos);

    testdb::cleanupDb(ELF_DB_PATH);
}
