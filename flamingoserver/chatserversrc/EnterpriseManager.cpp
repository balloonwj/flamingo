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
#include "SensitiveWordManager.h"   //删除企业时清理该企业的敏感词库与拦截记录
#include "AccountName.h"            //NormalizeAccountPrefix：账号前缀的规整与校验
#include "../base/Singleton.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"

//拼装 SQL 的字符缓冲区大小。
#define ENT_SQL_BUF_LEN   2048

//套餐取值「基础版」，即 t_enterprise.f_plan 的 0（取值说明见 table_mysql.sql 的字段注释）。
//启动时自动创建的默认企业使用这个套餐。
#define ENT_PLAN_BASIC    0


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

bool EnterpriseManager::isValidDateText(const std::string& text)
{
    //「YYYY-MM-DD」：长度 10，下标 4 与 7 为分隔符。
    const size_t DATE_TEXT_LEN = 10;
    const size_t DATE_DASH1 = 4;
    const size_t DATE_DASH2 = 7;
    const int MONTH_MAX = 12;
    const int DAY_MAX = 31;
    const int DECIMAL_BASE = 10;
    if (text.size() != DATE_TEXT_LEN)
        return false;
    for (size_t i = 0; i < DATE_TEXT_LEN; ++i)
    {
        const char c = text[i];
        if (i == DATE_DASH1 || i == DATE_DASH2)
        {
            if (c != '-')
                return false;
        }
        else if (c < '0' || c > '9')
        {
            return false;
        }
    }
    const int month = (text[DATE_DASH1 + 1] - '0') * DECIMAL_BASE + (text[DATE_DASH1 + 2] - '0');
    const int day = (text[DATE_DASH2 + 1] - '0') * DECIMAL_BASE + (text[DATE_DASH2 + 2] - '0');
    return month >= 1 && month <= MONTH_MAX && day >= 1 && day <= DAY_MAX;
}

std::string EnterpriseManager::buildWhere(const EnterpriseListFilter& filter)
{
    std::string where = "WHERE 1 = 1";
    if (!filter.keyword.empty())
    {
        std::string kw = dbapi::EscapeString(filter.keyword);
        where += " AND (e.f_name LIKE '%" + kw + "%' OR e.f_short_name LIKE '%"
              + kw + "%' OR e.f_industry LIKE '%" + kw + "%')";
    }
    if (filter.status >= 0)
    {
        where += " AND e.f_status = " + std::to_string(filter.status);
    }
    //行业精确匹配（控制台从固定的行业列表里选）。
    if (!filter.industry.empty())
    {
        where += " AND e.f_industry = '" + dbapi::EscapeString(filter.industry) + "'";
    }
    if (filter.plan >= 0)
    {
        where += " AND e.f_plan = " + std::to_string(filter.plan);
    }
    //开通日期区间：起止当天都包含。日期先经格式校验，格式不对的不参与筛选，也就不会拼进 SQL。
    if (isValidDateText(filter.openFrom))
    {
        where += " AND e.f_open_date >= '" + filter.openFrom + " 00:00:00'";
    }
    if (isValidDateText(filter.openTo))
    {
        where += " AND e.f_open_date <= '" + filter.openTo + " 23:59:59'";
    }
    return where;
}

void EnterpriseManager::listEnterprises(const std::string& keyword, int32_t status, int32_t page, int32_t pageSize,
                                        int32_t& outTotal, std::vector<EnterpriseInfo>& outList)
{
    EnterpriseListFilter filter;
    filter.keyword = keyword;
    filter.status = status;
    listEnterprises(filter, page, pageSize, outTotal, outList);
}

void EnterpriseManager::listEnterprises(const EnterpriseListFilter& filter, int32_t page, int32_t pageSize,
                                        int32_t& outTotal, std::vector<EnterpriseInfo>& outList)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildWhere(filter);

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
        "(SELECT COUNT(*) FROM t_user u WHERE u.f_enterprise_id = e.f_enterprise_id AND u.f_usertype = 0), "
        "(SELECT COUNT(*) FROM t_department d WHERE d.f_enterprise_id = e.f_enterprise_id), "
        "e.f_account_prefix "
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
        e.accountPrefix = pRow[14].getString();
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
        "f_root_dept_id, f_account_prefix "
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
        outInfo.accountPrefix     = pRow[14].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //补充员工数与部门数。
    char csql[256] = { 0 };
    snprintf(csql, sizeof(csql), "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id = %lld AND f_usertype = 0",
             (long long)enterpriseId);
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
        "f_root_dept_id, f_account_prefix "
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
        outInfo.accountPrefix     = pRow[14].getString();
        found = true;

        //还能取到第二行，说明存量库缺少 uniq_enterprise_name 约束且已经产生重名，
        //此时"该把人放进哪一家"其实无从判定。这里不失败，但必须留下告警日志，
        //让运维手工清理重名企业，并给 t_enterprise.f_name 补上唯一约束 uniq_enterprise_name。
        if (pResult->nextRow() && pResult->fetch() != NULL)
        {
            LOGW("getEnterpriseByName: duplicate enterprise name in db, name: %s, picked enterpriseid: %lld; "
                 "remove the duplicates and add unique key uniq_enterprise_name by hand",
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

    //连带创建"公司根部门"并回写到企业记录。失败时企业保留、f_root_dept_id 留为 0，
    //组织架构树上将没有可挂人的公司节点；错误日志已在辅助函数内记录，由 tools 下的
    //校验脚本发现后补建。
    attachRootDepartment(pConn.get(), newId, req.name);

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

std::string EnterpriseManager::resolveDefaultEnterpriseName(const char* configValue)
{
    if (configValue == NULL)
        return DEFAULT_ENTERPRISE_NAME;

    std::string name(configValue);
    const char* const kBlank = " \t";
    const size_t first = name.find_first_not_of(kBlank);
    if (first == std::string::npos)
        return DEFAULT_ENTERPRISE_NAME;
    const size_t last = name.find_last_not_of(kBlank);
    return name.substr(first, last - first + 1);
}

int32_t EnterpriseManager::ensureDefaultEnterprise(const std::string& name, bool& outCreated)
{
    outCreated = false;

    if (name.empty())
    {
        LOGE("ensureDefaultEnterprise failed: empty name");
        return error_code_unknown;
    }

    //与 createEnterprise 一样持锁，避免与管理台同时建同名企业。getEnterpriseByName 与
    //DepartmentManager 内部都不会再取本类的锁，不构成加锁顺序反转。
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    EnterpriseInfo existing;
    if (getEnterpriseByName(name, existing))
    {
        //已存在：状态保持原样。不是活跃状态时记一条日志，便于排查「选默认企业注册
        //却提示企业不可加入」这类问题。
        if (existing.status != kEnterpriseStatusActive)
        {
            LOGI("ensureDefaultEnterprise: default enterprise exists but is not active, left unchanged, "
                 "enterpriseid: %lld, status: %d, name: %s",
                 (long long)existing.enterpriseId, existing.status, name.c_str());
        }

        //缺少公司根部门时补建，否则用户选默认企业注册会被 error_code_enterprise_no_root_dept 拒绝。
        if (existing.rootDeptId == 0)
        {
            LOGI("ensureDefaultEnterprise: default enterprise has no root department, creating one, "
                 "enterpriseid: %lld, name: %s",
                 (long long)existing.enterpriseId, name.c_str());
            attachRootDepartment(pConn.get(), existing.enterpriseId, name);
        }

        LOGI("ensureDefaultEnterprise: default enterprise already exists, enterpriseid: %lld, name: %s",
             (long long)existing.enterpriseId, name.c_str());
        return error_code_ok;
    }

    //不存在：业务ID 按 createEnterprise 的同一规则分配，即当前最大值加一。
    int64_t newId = queryCount(pConn.get(), "SELECT IFNULL(MAX(f_enterprise_id), 0) FROM t_enterprise") + 1;

    //与 createEnterprise 的区别：状态直接为活跃，用户不改默认名就能注册进来；套餐取基础版；
    //不填管理员姓名与邮箱，也不建企业管理员。
    std::string nameEsc = dbapi::EscapeString(name);
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_plan, f_status, f_open_date, f_create_time) "
        "VALUES(%lld, '%s', %d, %d, NOW(), NOW())",
        (long long)newId, nameEsc.c_str(), ENT_PLAN_BASIC, (int)kEnterpriseStatusActive);
    if (!pConn->execute(sql))
    {
        LOGE("ensureDefaultEnterprise failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outCreated = true;
    attachRootDepartment(pConn.get(), newId, name);

    LOGI("ensureDefaultEnterprise ok, created default enterprise, enterpriseid: %lld, name: %s",
         (long long)newId, name.c_str());
    return error_code_ok;
}

bool EnterpriseManager::attachRootDepartment(IDatabase* pConn, int64_t enterpriseId, const std::string& deptName)
{
    //公司根部门是一行 f_parent_dept_id 为 0、以企业全称命名的真实部门，代表组织架构树上
    //"公司本身"这一层，员工可以直接挂在它下面。建成后把它的ID 回写到企业记录的
    //f_root_dept_id，后续判定某节点是否为公司节点即以此为准。
    int64_t rootDeptId = 0;
    int32_t dc = Singleton<DepartmentManager>::Instance().createEnterpriseRootDepartment(
        enterpriseId, deptName, rootDeptId);
    if (dc != error_code_ok)
    {
        LOGE("attachRootDepartment: create root department failed, enterpriseid: %lld, code: %d",
             (long long)enterpriseId, dc);
        return false;
    }

    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_enterprise SET f_root_dept_id = %lld WHERE f_enterprise_id = %lld",
        (long long)rootDeptId, (long long)enterpriseId);
    if (!pConn->execute(sql))
    {
        LOGE("attachRootDepartment: write back root dept id failed, enterpriseid: %lld, rootdeptid: %lld",
             (long long)enterpriseId, (long long)rootDeptId);
        return false;
    }

    return true;
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

int32_t EnterpriseManager::setAccountPrefix(int64_t enterpriseId, const std::string& rawPrefix)
{
    std::string prefix;
    if (!NormalizeAccountPrefix(rawPrefix, prefix))
    {
        LOGE("setAccountPrefix failed: invalid prefix '%s', enterpriseid: %lld", rawPrefix.c_str(),
             (long long)enterpriseId);
        return error_code_account_prefix_invalid;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char check[256] = { 0 };
    snprintf(check, sizeof(check),
        "SELECT COUNT(*) FROM t_enterprise WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    if (queryCount(pConn.get(), check) <= 0)
    {
        LOGE("setAccountPrefix failed: enterprise not exist, id: %lld", (long long)enterpriseId);
        return error_code_unknown;
    }

    //空串写 NULL：表示没设前缀、按默认的 EMP 生成。前缀已规整成只含大写字母与点号，转义只是保险
    const std::string value = prefix.empty() ? std::string("NULL") : ("'" + dbapi::EscapeString(prefix) + "'");
    char sql[ENT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_enterprise SET f_account_prefix = %s WHERE f_enterprise_id = %lld",
        value.c_str(), (long long)enterpriseId);
    if (!pConn->execute(sql))
    {
        LOGE("setAccountPrefix failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }
    LOGI("setAccountPrefix ok, enterpriseid: %lld, prefix: '%s'", (long long)enterpriseId, prefix.c_str());
    return error_code_ok;
}

std::string EnterpriseManager::getAccountPrefix(int64_t enterpriseId)
{
    if (enterpriseId == 0)
        return std::string();

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return std::string();

    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_account_prefix FROM t_enterprise WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return std::string();
    std::string prefix;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        prefix = pRow[0].getString();
    pResult->endQuery();
    delete pResult;
    return prefix;
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

    //5) 删除该企业的敏感词库与拦截记录，并丢弃其匹配器缓存（docs/敏感词方案.md 第 6.6 节）。
    //   与上面几步的处理方式一致：失败不中断删除，原因由 deleteEnterpriseData 记在日志里。
    Singleton<SensitiveWordManager>::Instance().deleteEnterpriseData(enterpriseId);

    //6) 员工账号保留（含其聊天记录），仅解除企业归属。
    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_enterprise_id = 0, f_role = 0 WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    pConn->execute(sql);

    //7) 删除企业本身。
    snprintf(sql, sizeof(sql), "DELETE FROM t_enterprise WHERE f_enterprise_id = %lld", (long long)enterpriseId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    //8) 同步组织架构缓存：上面第 4 步的部门与成员记录是直接批量 DELETE 的，
    //   不经 DepartmentManager 的写方法，其内存缓存不会自行重载。不同步的话，
    //   被删企业的整棵部门树会继续留在 tree.do 与在线客户端的通讯录里。
    //   第 6 步把员工的 f_enterprise_id 清了零，用户→企业映射同样要一并清掉。
    Singleton<DepartmentManager>::Instance().removeCachedEnterprise(enterpriseId);

    LOGI("EnterpriseManager::deleteEnterprise ok, enterpriseid: %lld", (long long)enterpriseId);
    return error_code_ok;
}

void EnterpriseManager::exportEnterprisesCsv(const std::string& keyword, int32_t status, std::string& outCsv)
{
    EnterpriseListFilter filter;
    filter.keyword = keyword;
    filter.status = status;
    exportEnterprisesCsv(filter, outCsv);
}

void EnterpriseManager::exportEnterprisesCsv(const EnterpriseListFilter& filter, std::string& outCsv)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildWhere(filter);
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
