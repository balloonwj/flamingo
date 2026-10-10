/**
 *  默认企业启动引导单元测试（Suite DE），test_defaultenterprise.cpp
 *
 *  覆盖 EnterpriseManager::ensureDefaultEnterprise 的行为：
 *    - 空库调用会建出默认企业：状态为「活跃」、带公司根部门、不建企业管理员；
 *    - 重复调用不会重复创建（幂等），企业ID 与根部门ID 保持不变；
 *    - 企业已存在且处于「已停用」或「待审核」时不修改其状态；
 *    - 企业已存在但缺少公司根部门时补建根部门并回写，状态仍不修改；
 *    - 新企业的业务ID 接在既有企业之后分配，不与已有 f_enterprise_id 冲突；
 *    - 建出来的默认企业满足注册流程的放行条件（活跃 + 有根部门）；
 *    - 名称为空时直接返回错误，不写库；
 *    - 默认企业的全称来自 chatserver.conf 的 defaultenterprise（2026-10-04 起可配置，PC 客户端
 *      flamingo.ini 的 [ui] defaultcompany 须配成同一个值）：DEN1 配置值去掉首尾空白后使用，
 *      DEN2 没有这一项或值为空白时用内置的 FlamingoIM.org，DEN3 配置成别的名称（含中文）时
 *      启动补建的就是那家企业。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— EnterpriseManager 与 DepartmentManager
 *  都经 DbFactory 取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig
 *  指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdio.h>
#include <memory>
#include <string>

#include "doctest.h"
#include "../chatserversrc/EnterpriseManager.h"
#include "../chatserversrc/DepartmentManager.h"
#include "../chatserversrc/AdminApiCodes.h"           //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与其它套件用的文件分开，避免互相干扰
static const char* const DEFAULT_ENT_DB_PATH = "flamingo_sqlite_defaultent_test.db";

//用例里手工预置的「别的企业」所用的业务ID。取两个不连续的值，验证新企业取的是最大值加一
static const int64_t OTHER_ENT_ID_SMALL = 3;
static const int64_t OTHER_ENT_ID_LARGE = 9;

//DE5 用例里手工预置的默认企业所用的业务ID，随便取一个非 0 值即可
static const int64_t PRESET_DEFAULT_ENT_ID = 5;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeDefaultEntDbFiles()
{
    std::string basePath(DEFAULT_ENT_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始。DepartmentManager 是单例，
//它的部门ID分配基数在 init 时从库里读取，故重建库之后要重新 init 一次。
static void resetDefaultEntDb()
{
    removeDefaultEntDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(DEFAULT_ENT_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, DEFAULT_ENT_DB_PATH);

    REQUIRE(Singleton<DepartmentManager>::Instance().init());
}

//执行一条不取结果的 SQL；用于在用例里预置数据。失败返回 false。
static bool execSql(const char* sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return false;

    return pConn->execute(sql);
}

//执行一条返回单个整数的 SQL（形如 SELECT COUNT(*) ...）。查询失败或无结果时返回 -1，
//调用方须与 0 区分开。
static int64_t queryInt(const char* sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return -1;

    int64_t value = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        value = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return value;
}

//往 t_enterprise 里手工插入一家企业，模拟「库里本来就有的企业」
static bool insertEnterpriseRow(int64_t enterpriseId, const char* name, int32_t status, int64_t rootDeptId)
{
    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_status, f_root_dept_id, f_create_time) "
        "VALUES(%lld, '%s', %d, %lld, NOW())",
        (long long)enterpriseId, name, status, (long long)rootDeptId);
    return execSql(sql);
}

//把指定名称的企业状态改成 status，模拟管理员在管理台停用 / 退回待审核
static bool setEnterpriseStatusByName(const char* name, int32_t status)
{
    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql), "UPDATE t_enterprise SET f_status = %d WHERE f_name = '%s'", status, name);
    return execSql(sql);
}

//数一数 t_enterprise 里叫这个名字的企业有几家
static int64_t countEnterprisesByName(const char* name)
{
    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_enterprise WHERE f_name = '%s'", name);
    return queryInt(sql);
}

//一行 t_department 记录里本套用例关心的字段
struct DeptRow
{
    DeptRow() : enterpriseId(0), parentDeptId(-1), found(false) {}

    int64_t     enterpriseId;  //所属企业ID
    std::string name;          //部门名称
    int64_t     parentDeptId;  //父部门ID，公司根部门为 0
    bool        found;         //该部门是否存在
};

//按部门业务ID 读回一行 t_department 记录
static DeptRow queryDeptRow(int64_t deptId)
{
    DeptRow row;

    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return row;

    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_dept_name, f_parent_dept_id FROM t_department WHERE f_dept_id = %lld",
        (long long)deptId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return row;

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        row.enterpriseId = static_cast<int64_t>(pRow[0].getUInt64());
        row.name         = pRow[1].getString();
        row.parentDeptId = static_cast<int64_t>(pRow[2].getUInt64());
        row.found        = true;
    }
    pResult->endQuery();
    delete pResult;
    return row;
}

//按名称读回企业信息；企业不存在时 REQUIRE 失败
static EnterpriseInfo requireEnterpriseByName(const char* name)
{
    EnterpriseInfo info;
    REQUIRE(Singleton<EnterpriseManager>::Instance().getEnterpriseByName(name, info));
    return info;
}

// ---------------------------------------------------------------------------
// DE1  空库首次调用
// ---------------------------------------------------------------------------

TEST_CASE("DE1 空库调用会建出默认企业：状态活跃、带公司根部门、不建企业管理员")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    REQUIRE(countEnterprisesByName(DEFAULT_ENTERPRISE_NAME) == 0);

    bool created = false;
    CHECK(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    CHECK(created);

    CHECK(countEnterprisesByName(DEFAULT_ENTERPRISE_NAME) == 1);
    EnterpriseInfo info = requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME);
    CHECK(info.status == kEnterpriseStatusActive);
    CHECK(info.rootDeptId != 0);

    //根部门是一行真实的部门记录：属于这家企业、以企业全称命名、没有父部门
    DeptRow root = queryDeptRow(info.rootDeptId);
    REQUIRE(root.found);
    CHECK(root.enterpriseId == info.enterpriseId);
    CHECK(root.name == DEFAULT_ENTERPRISE_NAME);
    CHECK(root.parentDeptId == 0);

    //默认企业由平台超级管理员管理，不连带创建企业管理员
    CHECK(queryInt("SELECT COUNT(*) FROM t_admin") == 0);
}

// ---------------------------------------------------------------------------
// DE2  幂等
// ---------------------------------------------------------------------------

TEST_CASE("DE2 连续调用两次只有一家默认企业，企业ID 与根部门ID 不变")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    bool firstCreated = false;
    REQUIRE(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, firstCreated) == error_code_ok);
    REQUIRE(firstCreated);
    EnterpriseInfo first = requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME);

    bool secondCreated = true;
    CHECK(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, secondCreated) == error_code_ok);
    CHECK_FALSE(secondCreated);

    CHECK(countEnterprisesByName(DEFAULT_ENTERPRISE_NAME) == 1);
    EnterpriseInfo second = requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME);
    CHECK(second.enterpriseId == first.enterpriseId);
    CHECK(second.rootDeptId == first.rootDeptId);

    //第二次调用也没有多建一个根部门
    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id = %lld",
             (long long)first.enterpriseId);
    CHECK(queryInt(sql) == 1);
}

// ---------------------------------------------------------------------------
// DE3 / DE4  已存在时不改状态
// ---------------------------------------------------------------------------

TEST_CASE("DE3 默认企业已被停用时，再次调用不改其状态，也不新建")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    bool created = false;
    REQUIRE(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    REQUIRE(setEnterpriseStatusByName(DEFAULT_ENTERPRISE_NAME, kEnterpriseStatusDisabled));

    created = true;
    CHECK(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    CHECK_FALSE(created);

    CHECK(countEnterprisesByName(DEFAULT_ENTERPRISE_NAME) == 1);
    CHECK(requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME).status == kEnterpriseStatusDisabled);
}

TEST_CASE("DE4 默认企业处于待审核时，再次调用不改其状态")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    bool created = false;
    REQUIRE(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    REQUIRE(setEnterpriseStatusByName(DEFAULT_ENTERPRISE_NAME, kEnterpriseStatusPending));

    created = true;
    CHECK(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    CHECK_FALSE(created);

    CHECK(requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME).status == kEnterpriseStatusPending);
}

// ---------------------------------------------------------------------------
// DE5  补建根部门
// ---------------------------------------------------------------------------

TEST_CASE("DE5 默认企业已存在但缺少公司根部门时补建并回写，状态不变")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    //预置一家没有根部门、处于待审核的默认企业，模拟存量数据
    REQUIRE(insertEnterpriseRow(PRESET_DEFAULT_ENT_ID, DEFAULT_ENTERPRISE_NAME, kEnterpriseStatusPending, 0));

    bool created = true;
    CHECK(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    CHECK_FALSE(created);

    EnterpriseInfo info = requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME);
    CHECK(info.enterpriseId == PRESET_DEFAULT_ENT_ID);
    CHECK(info.status == kEnterpriseStatusPending);
    REQUIRE(info.rootDeptId != 0);

    DeptRow root = queryDeptRow(info.rootDeptId);
    REQUIRE(root.found);
    CHECK(root.enterpriseId == PRESET_DEFAULT_ENT_ID);
    CHECK(root.name == DEFAULT_ENTERPRISE_NAME);
    CHECK(root.parentDeptId == 0);
}

// ---------------------------------------------------------------------------
// DE6  业务ID 分配
// ---------------------------------------------------------------------------

TEST_CASE("DE6 库里已有别的企业时，默认企业的业务ID 取当前最大值加一")
{
    resetDefaultEntDb();
    EnterpriseManager& mgr = Singleton<EnterpriseManager>::Instance();

    REQUIRE(insertEnterpriseRow(OTHER_ENT_ID_SMALL, "Other Small Co.", kEnterpriseStatusActive, 0));
    REQUIRE(insertEnterpriseRow(OTHER_ENT_ID_LARGE, "Other Large Co.", kEnterpriseStatusActive, 0));

    bool created = false;
    REQUIRE(mgr.ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created) == error_code_ok);
    REQUIRE(created);

    CHECK(requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME).enterpriseId == OTHER_ENT_ID_LARGE + 1);
    CHECK(queryInt("SELECT COUNT(*) FROM t_enterprise") == 3);
}

// ---------------------------------------------------------------------------
// DE7  满足注册放行条件
// ---------------------------------------------------------------------------

TEST_CASE("DE7 建出的默认企业满足注册流程的放行条件，名称与客户端约定的一致")
{
    //名称是与 PC 客户端注册窗的约定，改了这里客户端的默认选项就注册不进来了
    CHECK(std::string(DEFAULT_ENTERPRISE_NAME) == "FlamingoIM.org");

    resetDefaultEntDb();
    bool created = false;
    REQUIRE(Singleton<EnterpriseManager>::Instance().ensureDefaultEnterprise(DEFAULT_ENTERPRISE_NAME, created)
            == error_code_ok);

    //RegisterService::doRegister 按名称找到企业后，要求它处于活跃状态且已有公司根部门
    EnterpriseInfo info = requireEnterpriseByName(DEFAULT_ENTERPRISE_NAME);
    CHECK(info.status == kEnterpriseStatusActive);
    CHECK(info.rootDeptId != 0);
    CHECK(info.name == DEFAULT_ENTERPRISE_NAME);
}

// ---------------------------------------------------------------------------
// DE8  参数校验
// ---------------------------------------------------------------------------

TEST_CASE("DE8 名称为空时返回错误，不写库")
{
    resetDefaultEntDb();

    bool created = true;
    CHECK(Singleton<EnterpriseManager>::Instance().ensureDefaultEnterprise("", created) != error_code_ok);
    CHECK_FALSE(created);
    CHECK(queryInt("SELECT COUNT(*) FROM t_enterprise") == 0);
    CHECK(queryInt("SELECT COUNT(*) FROM t_department") == 0);
}

// ---------------------------------------------------------------------------
// DEN1 ~ DEN3  默认企业的全称可配置（chatserver.conf 的 defaultenterprise）
// ---------------------------------------------------------------------------

TEST_CASE("DEN1 配置了默认企业的全称时去掉首尾空白后使用")
{
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName("Acme Corp") == "Acme Corp");
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName("  \tAcme Corp \t") == "Acme Corp");
    //名称中间的空格保留
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName("Acme  Corp") == "Acme  Corp");
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName(u8"火烈鸟科技有限公司")
          == std::string(u8"火烈鸟科技有限公司"));
}

TEST_CASE("DEN2 没有这一项或值为空白时用内置的 FlamingoIM.org")
{
    CHECK(std::string(DEFAULT_ENTERPRISE_NAME) == "FlamingoIM.org");
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName(NULL) == DEFAULT_ENTERPRISE_NAME);
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName("") == DEFAULT_ENTERPRISE_NAME);
    CHECK(EnterpriseManager::resolveDefaultEnterpriseName(" \t ") == DEFAULT_ENTERPRISE_NAME);
}

TEST_CASE("DEN3 配置成别的名称时，启动补建的就是那家企业")
{
    resetDefaultEntDb();
    const std::string name = EnterpriseManager::resolveDefaultEnterpriseName(u8" 火烈鸟科技有限公司 ");

    bool created = false;
    REQUIRE(Singleton<EnterpriseManager>::Instance().ensureDefaultEnterprise(name, created) == error_code_ok);
    CHECK(created);

    //按配置的名称能找到它，且满足注册流程的放行条件；内置名称的企业不会被建出来
    EnterpriseInfo info = requireEnterpriseByName(name.c_str());
    CHECK(info.status == kEnterpriseStatusActive);
    CHECK(info.rootDeptId != 0);
    CHECK(queryInt("SELECT COUNT(*) FROM t_enterprise") == 1);
    CHECK(queryInt("SELECT COUNT(*) FROM t_enterprise WHERE f_name = 'FlamingoIM.org'") == 0);
}
