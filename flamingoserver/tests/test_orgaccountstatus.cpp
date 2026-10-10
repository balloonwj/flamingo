/**
 *  组织架构成员账号状态单元测试（Suite OAS），test_orgaccountstatus.cpp
 *
 *  组织架构应答（cmd 1200 与管理控制台 tree.do）里成员的 status 是在线状态，原先没有账号状态，
 *  管理控制台把在线状态 1 当成「已停用」。2026-10-10 起每个成员另带 accountstatus（t_user.f_status，0 启用 / 1 停用）：
 *    - OAS1 DepartmentManager 从库加载组织架构时，成员的 accountstatus 取自 t_user.f_status；
 *    - OAS2 EmployeeManager::setEmployeeStatus 启用 / 停用员工后，不重新加载，缓存里该成员的 accountstatus 立即变化，
 *           拼出的应答也随之变化；兼属的部门一并更新。
 *
 *  这些用例真的打开一个临时 .db 文件跑 SQL：DepartmentManager 与 EmployeeManager 都经 DbFactory 取连接，
 *  DbFactory 读的是全局 CDbConfig，故先把 CDbConfig 指到本套专用的库文件上（与 test_defaultenterprise.cpp 相同做法）。
 *
 *  balloonwj@qq.com   2026-10-10
 */
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/DepartmentManager.h"
#include "../chatserversrc/DepartmentMsg.h"
#include "../chatserversrc/EmployeeManager.h"
#include "../chatserversrc/AdminApiCodes.h"           //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

//本套用例专用的临时数据库文件，与其它套件用的文件分开，避免互相干扰
static const char* const OAS_DB_PATH = "flamingo_sqlite_orgaccount_test.db";

//预置数据：一家企业、两个部门、两名员工（取值只需互不冲突）
static const int64_t OAS_ENTERPRISE_ID = 31;      //企业ID
static const int64_t OAS_DEPT_RD       = 7001;    //研发部：两名员工的主部门
static const int64_t OAS_DEPT_QA       = 7002;    //测试部：员工甲兼属于此
static const int64_t OAS_USER_ACTIVE   = 9001;    //员工甲：账号启用
static const int64_t OAS_USER_DISABLED = 9002;    //员工乙：账号已停用

//账号状态取值（t_user.f_status）
static const int32_t OAS_ACCOUNT_ACTIVE   = 0;    //启用
static const int32_t OAS_ACCOUNT_DISABLED = 1;    //停用

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeOasDbFiles()
{
    std::string basePath(OAS_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//执行一条不取结果的 SQL；失败时断言失败
static void oasExec(const char* sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    INFO("sql: " << sql);
    REQUIRE(pConn->execute(sql));
}

//执行一条返回单个整数的 SQL；查询失败或无结果时返回 -1
static int64_t oasQueryInt(const char* sql)
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
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return value;
}

//重建测试库、预置部门与员工，并让 DepartmentManager 从这个库加载组织架构
static void resetOasDb()
{
    removeOasDbFiles();
    REQUIRE(Singleton<CSqliteManager>::Instance().init(OAS_DB_PATH));
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL, NULL, OAS_DB_PATH);

    char sql[512] = { 0 };
    const int64_t depts[] = { OAS_DEPT_RD, OAS_DEPT_QA };
    for (size_t i = 0; i < sizeof(depts) / sizeof(depts[0]); ++i)
    {
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_department(f_dept_id, f_enterprise_id, f_dept_name, f_parent_dept_id, f_dept_path, "
            "f_status, f_create_time) VALUES(%lld, %lld, 'dept%lld', 0, '/%lld/', 0, NOW())",
            (long long)depts[i], (long long)OAS_ENTERPRISE_ID, (long long)depts[i], (long long)depts[i]);
        oasExec(sql);
    }
    const int64_t users[] = { OAS_USER_ACTIVE, OAS_USER_DISABLED };
    const int32_t states[] = { OAS_ACCOUNT_ACTIVE, OAS_ACCOUNT_DISABLED };
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); ++i)
    {
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time, f_enterprise_id, "
            "f_status) VALUES(%lld, 'u%lld', 'user%lld', '', NOW(), %lld, %d)",
            (long long)users[i], (long long)users[i], (long long)users[i], (long long)OAS_ENTERPRISE_ID, states[i]);
        oasExec(sql);
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_department_member(f_dept_id, f_user_id, f_is_primary, f_status, f_join_time) "
            "VALUES(%lld, %lld, 1, 0, NOW())",
            (long long)OAS_DEPT_RD, (long long)users[i]);
        oasExec(sql);
    }
    //员工甲兼属测试部
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_department_member(f_dept_id, f_user_id, f_is_primary, f_status, f_join_time) "
        "VALUES(%lld, %lld, 0, 0, NOW())",
        (long long)OAS_DEPT_QA, (long long)OAS_USER_ACTIVE);
    oasExec(sql);

    REQUIRE(Singleton<DepartmentManager>::Instance().init());
}

//在本企业的组织架构缓存里找出某员工在某部门的成员条目；找不到时返回 false
static bool findMember(int64_t deptId, int64_t userId, dept::DepartmentMember& out)
{
    std::vector<dept::DepartmentInfo> depts;
    Singleton<DepartmentManager>::Instance().getOrgStructure(OAS_ENTERPRISE_ID, depts);
    for (size_t i = 0; i < depts.size(); ++i)
    {
        if (depts[i].deptid != deptId)
            continue;
        for (size_t k = 0; k < depts[i].members.size(); ++k)
        {
            if (depts[i].members[k].userid == userId)
            {
                out = depts[i].members[k];
                return true;
            }
        }
    }
    return false;
}

//本企业组织架构应答里某员工在某部门的 accountstatus；找不到时返回 -1
static int jsonAccountStatus(int64_t deptId, int64_t userId)
{
    std::vector<dept::DepartmentInfo> depts;
    Singleton<DepartmentManager>::Instance().getOrgStructure(OAS_ENTERPRISE_ID, depts);
    const std::string text = dept::BuildGetOrgStructureResp(0, "ok", depts);
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    JSONCPP_STRING errs;
    if (!reader->parse(text.c_str(), text.c_str() + text.length(), &root, &errs))
        return -1;
    const Json::Value& arr = root["departments"];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i]["deptid"].asInt64() != deptId)
            continue;
        const Json::Value& members = arr[i]["members"];
        for (Json::ArrayIndex k = 0; k < members.size(); ++k)
        {
            if (members[k]["userid"].asInt64() == userId && members[k].isMember("accountstatus"))
                return members[k]["accountstatus"].asInt();
        }
    }
    return -1;
}

TEST_CASE("OAS1 加载组织架构时成员的 accountstatus 取自 t_user.f_status")
{
    resetOasDb();

    dept::DepartmentMember m;
    REQUIRE(findMember(OAS_DEPT_RD, OAS_USER_ACTIVE, m));
    CHECK(m.accountstatus == OAS_ACCOUNT_ACTIVE);
    REQUIRE(findMember(OAS_DEPT_RD, OAS_USER_DISABLED, m));
    CHECK(m.accountstatus == OAS_ACCOUNT_DISABLED);

    CHECK(jsonAccountStatus(OAS_DEPT_RD, OAS_USER_ACTIVE) == OAS_ACCOUNT_ACTIVE);
    CHECK(jsonAccountStatus(OAS_DEPT_RD, OAS_USER_DISABLED) == OAS_ACCOUNT_DISABLED);

    removeOasDbFiles();
}

TEST_CASE("OAS2 启用 / 停用员工后组织架构缓存里的 accountstatus 立即变化，兼属部门一并更新")
{
    resetOasDb();
    EmployeeManager& employees = Singleton<EmployeeManager>::Instance();
    char sql[256] = { 0 };

    //停用员工甲：库与缓存（主部门、兼属部门）都变成停用，不需要重新加载
    REQUIRE(employees.setEmployeeStatus(OAS_USER_ACTIVE, OAS_ACCOUNT_DISABLED) == error_code_ok);
    snprintf(sql, sizeof(sql), "SELECT f_status FROM t_user WHERE f_user_id = %lld", (long long)OAS_USER_ACTIVE);
    CHECK(oasQueryInt(sql) == OAS_ACCOUNT_DISABLED);
    dept::DepartmentMember m;
    REQUIRE(findMember(OAS_DEPT_RD, OAS_USER_ACTIVE, m));
    CHECK(m.accountstatus == OAS_ACCOUNT_DISABLED);
    REQUIRE(findMember(OAS_DEPT_QA, OAS_USER_ACTIVE, m));
    CHECK(m.accountstatus == OAS_ACCOUNT_DISABLED);
    CHECK(jsonAccountStatus(OAS_DEPT_RD, OAS_USER_ACTIVE) == OAS_ACCOUNT_DISABLED);

    //启用员工乙
    REQUIRE(employees.setEmployeeStatus(OAS_USER_DISABLED, OAS_ACCOUNT_ACTIVE) == error_code_ok);
    REQUIRE(findMember(OAS_DEPT_RD, OAS_USER_DISABLED, m));
    CHECK(m.accountstatus == OAS_ACCOUNT_ACTIVE);
    CHECK(jsonAccountStatus(OAS_DEPT_RD, OAS_USER_DISABLED) == OAS_ACCOUNT_ACTIVE);

    removeOasDbFiles();
}
