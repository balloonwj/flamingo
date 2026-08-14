/**
 *  企业租户管理器实现，EnterpriseManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "EnterpriseManager.h"
#include <memory>
#include <stdio.h>
#include "AdminApiCodes.h"
#include "AdminManager.h"
#include "DepartmentManager.h"
#include "../base/Singleton.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"

//拼装 SQL 的字符缓冲区大小。
#define ENT_SQL_BUF_LEN   2048


//把一个字段值转义为 CSV 字段：用双引号包裹，内部双引号翻倍。
static std::string CsvField(const std::string& src)
{
    std::string out = "\"";
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '"')
            out.push_back('"');
        out.push_back(src[i]);
    }
    out.push_back('"');
    return out;
}

EnterpriseManager::EnterpriseManager()
{

}

EnterpriseManager::~EnterpriseManager()
{

}

bool EnterpriseManager::init()
{
    LOGI("EnterpriseManager::init ok, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
    return true;
}

IDatabase* EnterpriseManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("EnterpriseManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

int32_t EnterpriseManager::queryCount(IDatabase* pConn, const char* sql)
{
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return 0;

    int32_t count = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

std::string EnterpriseManager::buildWhere(const std::string& keyword, int32_t status)
{
    std::string where = "WHERE 1 = 1";
    if (!keyword.empty())
    {
        std::string kw = dbapi::EscapeString(keyword);
        where += " AND (e.f_name LIKE '%" + kw + "%' OR e.f_short_name LIKE '%"
              + kw + "%' OR e.f_industry LIKE '%" + kw + "%')";
    }
    if (status >= 0)
    {
        where += " AND e.f_status = " + std::to_string(status);
    }
    return where;
}

void EnterpriseManager::listEnterprises(const std::string& keyword, int32_t status, int32_t page, int32_t pageSize,
                                        int32_t& outTotal, std::vector<EnterpriseInfo>& outList)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildWhere(keyword, status);

    //总条数。
    std::string countSql = "SELECT COUNT(*) FROM t_enterprise e " + where;
    outTotal = queryCount(pConn.get(), countSql.c_str());

    //分页：page 从 1 起。
    if (page < 1)
        page = 1;
    if (pageSize < 1)
        pageSize = 20;
    int32_t offset = (page - 1) * pageSize;

    //员工数、部门数用相关子查询一并取出。
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT e.f_enterprise_id, e.f_name, e.f_short_name, e.f_industry, e.f_region, e.f_plan, e.f_status, "
        "e.f_admin_name, e.f_admin_email, e.f_logo_color, e.f_open_date, e.f_root_dept_id, "
        "(SELECT COUNT(*) FROM t_user u WHERE u.f_enterprise_id = e.f_enterprise_id), "
        "(SELECT COUNT(*) FROM t_department d WHERE d.f_enterprise_id = e.f_enterprise_id) "
        "FROM t_enterprise e %s ORDER BY e.f_enterprise_id LIMIT %d, %d",
        where.c_str(), offset, pageSize);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        EnterpriseInfo e;
        e.enterpriseId = pRow[0].getInt32();
        e.name         = pRow[1].getString();
        e.shortName    = pRow[2].getString();
        e.industry     = pRow[3].getString();
        e.region       = pRow[4].getString();
        e.plan         = pRow[5].getInt32();
        e.status       = pRow[6].getInt32();
        e.adminName    = pRow[7].getString();
        e.adminEmail   = pRow[8].getString();
        e.logoColor    = pRow[9].getString();
        e.openDate     = pRow[10].getString();
        e.rootDeptId   = pRow[11].getInt32();
        e.memberCount  = pRow[12].getInt32();
        e.deptCount    = pRow[13].getInt32();
        outList.push_back(e);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

bool EnterpriseManager::getEnterprise(int64_t enterpriseId, EnterpriseInfo& outInfo)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_name, f_short_name, f_industry, f_region, f_plan, f_status, "
        "f_admin_name, f_admin_email, f_logo_color, f_open_date, f_license_expire_time, f_create_time, "
        "f_root_dept_id "
        "FROM t_enterprise WHERE f_enterprise_id = %lld",
        (long long)enterpriseId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outInfo.enterpriseId      = pRow[0].getInt32();
        outInfo.name              = pRow[1].getString();
        outInfo.shortName         = pRow[2].getString();
        outInfo.industry          = pRow[3].getString();
        outInfo.region            = pRow[4].getString();
        outInfo.plan              = pRow[5].getInt32();
        outInfo.status            = pRow[6].getInt32();
        outInfo.adminName         = pRow[7].getString();
        outInfo.adminEmail        = pRow[8].getString();
        outInfo.logoColor         = pRow[9].getString();
        outInfo.openDate          = pRow[10].getString();
        outInfo.licenseExpireTime = pRow[11].getString();
        outInfo.createTime        = pRow[12].getString();
        outInfo.rootDeptId        = pRow[13].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //补充员工数与部门数。
    char csql[256] = { 0 };
    snprintf(csql, sizeof(csql), "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    outInfo.memberCount = queryCount(pConn.get(), csql);
    snprintf(csql, sizeof(csql), "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    outInfo.deptCount = queryCount(pConn.get(), csql);
    return true;
}

bool EnterpriseManager::getEnterpriseByName(const std::string& name, EnterpriseInfo& outInfo)
{
    //与本类其它读操作一致，不加锁（理由见 getRootDeptId 的注释）。
    if (name.empty())
    {
        return false;
    }

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
    {
        return false;
    }

    //按 f_enterprise_id 升序取首行：正常情况下 uniq_enterprise_name 保证只有一行，
    //排序只是为了在存量库尚未建立该唯一约束、确实存在重名时结果稳定可复现。
    std::string nameEsc = dbapi::EscapeString(name);
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_name, f_short_name, f_industry, f_region, f_plan, f_status, "
        "f_admin_name, f_admin_email, f_logo_color, f_open_date, f_license_expire_time, f_create_time, "
        "f_root_dept_id "
        "FROM t_enterprise WHERE f_name = '%s' ORDER BY f_enterprise_id",
        nameEsc.c_str());

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
    {
        return false;
    }

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outInfo.enterpriseId      = pRow[0].getInt32();
        outInfo.name              = pRow[1].getString();
        outInfo.shortName         = pRow[2].getString();
        outInfo.industry          = pRow[3].getString();
        outInfo.region            = pRow[4].getString();
        outInfo.plan              = pRow[5].getInt32();
        outInfo.status            = pRow[6].getInt32();
        outInfo.adminName         = pRow[7].getString();
        outInfo.adminEmail        = pRow[8].getString();
        outInfo.logoColor         = pRow[9].getString();
        outInfo.openDate          = pRow[10].getString();
        outInfo.licenseExpireTime = pRow[11].getString();
        outInfo.createTime        = pRow[12].getString();
        outInfo.rootDeptId        = pRow[13].getInt32();
        found = true;

        //还能取到第二行，说明存量库缺少 uniq_enterprise_name 约束且已经产生重名，
        //此时"该把人放进哪一家"其实无从判定。这里不失败，但必须留下告警日志，
        //让运维执行 migrations/v4_add_t_enterprise_name_unique.sql 清理。
        if (pResult->nextRow() && pResult->fetch() != NULL)
        {
            LOGW("getEnterpriseByName: duplicate enterprise name in db, name: %s, picked enterpriseid: %lld; "
                 "run migrations/v4_add_t_enterprise_name_unique.sql to clean up",
                 name.c_str(), (long long)outInfo.enterpriseId);
        }
    }
    pResult->endQuery();
    delete pResult;

    return found;
}

bool EnterpriseManager::isNameTaken(IDatabase* pConn, const std::string& name, int64_t excludeEntId)
{
    std::string nameEsc = dbapi::EscapeString(name);
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_enterprise WHERE f_name = '%s' AND f_enterprise_id <> %lld",
        nameEsc.c_str(), (long long)excludeEntId);
    return queryCount(pConn, sql) > 0;
}

int64_t EnterpriseManager::getRootDeptId(int64_t enterpriseId)
{
    //刻意不加锁，原因见头文件中本方法的注释：DepartmentManager 会在它自己的锁内
    //调用本方法，而 createEnterprise 又会在本类锁内调用 DepartmentManager，
    //两处若都加锁会构成加锁顺序反转。
    if (enterpriseId == 0)
    {
        return 0;
    }

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
    {
        return 0;
    }

    //queryCount 取的是结果集首行首列，用于取单值字段同样适用（企业不存在时返回 0）。
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_root_dept_id FROM t_enterprise WHERE f_enterprise_id = %lld",
        (long long)enterpriseId);
    return queryCount(pConn.get(), sql);
}

int32_t EnterpriseManager::createEnterprise(const EnterpriseInfo& req, int64_t& outEnterpriseId,
                                            int64_t& outAdminId, std::string& outInitPassword)
{
    if (req.name.empty())
    {
        LOGE("createEnterprise failed: empty name");
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //企业全称查重。库上虽有 uniq_enterprise_name 兜底，但直接撞唯一键只会得到一句
    //笼统的 db execute error；这里先查一次，才能给管理台回一个能看懂的错误码。
    //新建时不排除任何一家，故 excludeEntId 传 0（企业业务ID从 1 起分配）。
    if (isNameTaken(pConn.get(), req.name, 0))
    {
        LOGE("createEnterprise failed: name already exists, name: %s", req.name.c_str());
        return error_code_enterprise_nameexist;
    }

    //分配企业业务ID = 当前最大值 + 1。
    int64_t newId = queryCount(pConn.get(), "SELECT IFNULL(MAX(f_enterprise_id), 0) FROM t_enterprise") + 1;

    //新企业默认状态为待审核(0)，开通日期取当前时间。
    std::string nameEsc  = dbapi::EscapeString(req.name);
    std::string shortEsc = dbapi::EscapeString(req.shortName);
    std::string indEsc   = dbapi::EscapeString(req.industry);
    std::string regEsc   = dbapi::EscapeString(req.region);
    std::string anEsc    = dbapi::EscapeString(req.adminName);
    std::string aeEsc    = dbapi::EscapeString(req.adminEmail);
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_short_name, f_industry, f_region, f_plan, "
        "f_status, f_admin_name, f_admin_email, f_open_date, f_create_time) "
        "VALUES(%lld, '%s', '%s', '%s', '%s', %d, 0, '%s', '%s', NOW(), NOW())",
        (long long)newId, nameEsc.c_str(), shortEsc.c_str(), indEsc.c_str(), regEsc.c_str(),
        req.plan, anEsc.c_str(), aeEsc.c_str());
    if (!pConn->execute(sql))
    {
        LOGE("createEnterprise failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outEnterpriseId = newId;
    outAdminId = 0;

    //连带创建"公司根部门"：一行 f_parent_dept_id 为 0、以企业全称命名的真实部门，
    //代表组织架构树上"公司本身"这一层，员工可以直接挂在它下面。建成后把它的ID
    //回写到企业记录的 f_root_dept_id，后续判定某节点是否为公司节点即以此为准。
    int64_t rootDeptId = 0;
    int32_t dc = Singleton<DepartmentManager>::Instance().createEnterpriseRootDepartment(
        newId, req.name, rootDeptId);
    if (dc != error_code_ok)
    {
        //企业已建好，仅根部门创建失败；此时 f_root_dept_id 留为 0，组织架构树上
        //将没有可挂人的公司节点。记录错误日志，由 tools 下的校验脚本发现后补建。
        LOGE("createEnterprise: enterprise %lld created but root department failed, code: %d",
             (long long)newId, dc);
    }
    else
    {
        snprintf(sql, sizeof(sql),
            "UPDATE t_enterprise SET f_root_dept_id = %lld WHERE f_enterprise_id = %lld",
            (long long)rootDeptId, (long long)newId);
        if (!pConn->execute(sql))
        {
            LOGE("createEnterprise: write back root dept id failed, enterpriseid: %lld, rootdeptid: %lld",
                 (long long)newId, (long long)rootDeptId);
        }
    }

    //连带创建该企业的初始管理员账号。
    if (!req.adminName.empty() && !req.adminEmail.empty())
    {
        int32_t ac = Singleton<AdminManager>::Instance().createAdmin(
            newId, req.adminName, req.adminEmail, outAdminId, outInitPassword);
        if (ac != error_code_ok)
        {
            //企业已建好，仅初始管理员创建失败（如邮箱重复）；记录告警，不回滚企业。
            LOGE("createEnterprise: enterprise %lld created but initial admin failed, code: %d",
                 (long long)newId, ac);
        }
    }

    LOGI("EnterpriseManager::createEnterprise ok, enterpriseid: %lld, name: %s",
         (long long)newId, req.name.c_str());
    return error_code_ok;
}

int32_t EnterpriseManager::updateEnterprise(const EnterpriseInfo& req)
{
    if (req.name.empty())
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //企业必须存在。
    char check[256] = { 0 };
    snprintf(check, sizeof(check),
        "SELECT COUNT(*) FROM t_enterprise WHERE f_enterprise_id = %lld", (long long)req.enterpriseId);
    if (queryCount(pConn.get(), check) <= 0)
    {
        LOGE("updateEnterprise failed: enterprise not exist, id: %lld", (long long)req.enterpriseId);
        return error_code_unknown;
    }

    //改名查重：排除自身，这样"只改了行业、名字没动"不会被误判成重名。
    if (isNameTaken(pConn.get(), req.name, req.enterpriseId))
    {
        LOGE("updateEnterprise failed: name already used by another enterprise, name: %s, id: %lld",
             req.name.c_str(), (long long)req.enterpriseId);
        return error_code_enterprise_nameexist;
    }

    std::string nameEsc  = dbapi::EscapeString(req.name);
    std::string shortEsc = dbapi::EscapeString(req.shortName);
    std::string indEsc   = dbapi::EscapeString(req.industry);
    std::string regEsc   = dbapi::EscapeString(req.region);
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_enterprise SET f_name = '%s', f_short_name = '%s', f_industry = '%s', "
        "f_region = '%s', f_plan = %d WHERE f_enterprise_id = %lld",
        nameEsc.c_str(), shortEsc.c_str(), indEsc.c_str(), regEsc.c_str(),
        req.plan, (long long)req.enterpriseId);
    if (!pConn->execute(sql))
    {
        LOGE("updateEnterprise failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    //公司根部门的名字始终跟随企业全称，否则管理端组织架构树和 IM 客户端通讯录上
    //显示的公司名会与企业名不一致。这里用同一条连接取根部门ID，避免再开一条连接。
    snprintf(sql, sizeof(sql),
        "SELECT f_root_dept_id FROM t_enterprise WHERE f_enterprise_id = %lld",
        (long long)req.enterpriseId);
    int64_t rootDeptId = queryCount(pConn.get(), sql);
    if (rootDeptId != 0)
    {
        //改名失败不影响企业本身已经改好的结果，仅记录日志，避免把整次修改判为失败。
        int32_t dc = Singleton<DepartmentManager>::Instance().renameDepartment(rootDeptId, req.name);
        if (dc != error_code_ok)
        {
            LOGE("updateEnterprise: rename root department failed, enterpriseid: %lld, rootdeptid: %lld, code: %d",
                 (long long)req.enterpriseId, (long long)rootDeptId, dc);
        }
    }
    else
    {
        //存量数据迁移前的企业没有根部门，属于已知情形，记一条告警便于排查。
        LOGW("updateEnterprise: enterprise %lld has no root department, skip renaming",
             (long long)req.enterpriseId);
    }

    LOGI("EnterpriseManager::updateEnterprise ok, enterpriseid: %lld", (long long)req.enterpriseId);
    return error_code_ok;
}

int32_t EnterpriseManager::setEnterpriseStatus(int64_t enterpriseId, int32_t status)
{
    //状态仅允许 0待审核 / 1活跃 / 2停用。
    if (status < 0 || status > 2)
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_enterprise SET f_status = %d WHERE f_enterprise_id = %lld",
        status, (long long)enterpriseId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    LOGI("EnterpriseManager::setEnterpriseStatus ok, enterpriseid: %lld, status: %d",
         (long long)enterpriseId, status);
    return error_code_ok;
}

int32_t EnterpriseManager::deleteEnterprise(int64_t enterpriseId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[ENT_SQL_BUF_LEN] = { 0 };

    //1) 删除该企业管理员的会话。
    snprintf(sql, sizeof(sql),
        "DELETE s FROM t_admin_session s JOIN t_admin a ON s.f_admin_id = a.f_admin_id "
        "WHERE a.f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //2) 删除该企业的管理员账号。
    snprintf(sql, sizeof(sql), "DELETE FROM t_admin WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //3) 删除该企业服务号的绑定与服务号本身。
    snprintf(sql, sizeof(sql),
        "DELETE b FROM t_service_account_binding b JOIN t_service_account s ON b.f_service_id = s.f_service_id "
        "WHERE s.f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);
    snprintf(sql, sizeof(sql), "DELETE FROM t_service_account WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //4) 删除该企业的部门成员关系与部门。
    snprintf(sql, sizeof(sql), "DELETE FROM t_department_member WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);
    snprintf(sql, sizeof(sql), "DELETE FROM t_department WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //5) 员工账号保留（含其聊天记录），仅解除企业归属。
    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_enterprise_id = 0, f_role = 0 WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //6) 删除企业本身。
    snprintf(sql, sizeof(sql), "DELETE FROM t_enterprise WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    //7) 同步组织架构缓存：上面第 4 步的部门与成员记录是直接批量 DELETE 的，
    //   不经 DepartmentManager 的写方法，其内存缓存不会自行重载。不同步的话，
    //   被删企业的整棵部门树会继续留在 tree.do 与在线客户端的通讯录里。
    //   第 5 步把员工的 f_enterprise_id 清了零，用户→企业映射同样要一并清掉。
    Singleton<DepartmentManager>::Instance().removeCachedEnterprise(enterpriseId);

    LOGI("EnterpriseManager::deleteEnterprise ok, enterpriseid: %lld", (long long)enterpriseId);
    return error_code_ok;
}

void EnterpriseManager::exportEnterprisesCsv(const std::string& keyword, int32_t status, std::string& outCsv)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildWhere(keyword, status);
    std::string sql = "SELECT e.f_enterprise_id, e.f_name, e.f_short_name, e.f_industry, e.f_region, "
                      "e.f_plan, e.f_status, e.f_admin_name, e.f_admin_email, e.f_open_date "
                      "FROM t_enterprise e " + where + " ORDER BY e.f_enterprise_id";

    //CSV 表头：用 u8 字面量确保 UTF-8（MSVC 普通窄字面量按系统 GBK 编码）。
    outCsv = u8"企业ID,名称,简称,行业,地区,套餐,状态,主管理员,管理员邮箱,开通日期\r\n";

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        std::string line;
        line += CsvField(pRow[0].getString()) + ",";
        line += CsvField(pRow[1].getString()) + ",";
        line += CsvField(pRow[2].getString()) + ",";
        line += CsvField(pRow[3].getString()) + ",";
        line += CsvField(pRow[4].getString()) + ",";
        line += CsvField(pRow[5].getString()) + ",";
        line += CsvField(pRow[6].getString()) + ",";
        line += CsvField(pRow[7].getString()) + ",";
        line += CsvField(pRow[8].getString()) + ",";
        line += CsvField(pRow[9].getString());
        outCsv += line;
        outCsv += "\r\n";

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}
