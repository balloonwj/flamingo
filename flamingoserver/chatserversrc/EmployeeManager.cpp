/**
 *  控制台员工管理器实现，EmployeeManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "EmployeeManager.h"
#include <memory>
#include <stdio.h>
#include <time.h>
#include "AdminApiCodes.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../utils/MD5.h"
#include "../utils/PasswordHasher.h"   //口令存储哈希与盐值生成
#include "../utils/Sha256.h"           //服务端自行计算口令哈希的第一层（客户端不参与该场景）
#include "UserManager.h"
#include "DepartmentManager.h"   //改职务后同步组织架构缓存里的 position，见 updateEmployee
#include "AccountName.h"   //登录账号生成规则，与自助注册共用

//拼装 SQL 的字符缓冲区大小。
//最长语句为建员工 INSERT：昵称/工号/签名等转义后总长远小于 4096，取 4096 留足余量。
#define EMPLOYEE_SQL_BUF_LEN     4096

//生成的初始密码长度（取 MD5 十六进制摘要的前若干位）。
//10 位在「足够随机、不易撞」与「便于管理员转告员工」之间取平衡。
#define EMPLOYEE_PASSWORD_LENGTH 10

//普通用户ID的上界（不含）。userid 落在 (0, 0x0FFFFFFF)，群组ID自 0x0FFFFFFF 起，
//见 UserManager.h 的 GROUPID_BOUBDARY；员工列表须借此排除群账号。
#define EMPLOYEE_USER_ID_MAX     0x0FFFFFFF

//新建员工时 t_user.f_birthday 的默认值（库中该列默认 19900101）。
#define EMPLOYEE_DEFAULT_BIRTHDAY 19900101

//列表分页每页条数的上限，防止单次请求拉取过多数据。
#define EMPLOYEE_MAX_PAGE_SIZE   200

//分页缺省每页条数。
#define EMPLOYEE_DEFAULT_PAGE_SIZE 20

//generatePassword 用的进程内自增序列，保证同一秒内多次调用也生成不同密码。
static std::atomic<uint32_t> g_passwordSeq(0);


//计算字符串的 MD5 十六进制摘要。
static std::string Md5Hex(const std::string& src)
{
    MD5 md5(src);
    return md5.toString();
}

//把分页参数规整到合法范围：页码至少 1，每页条数落在 [1, EMPLOYEE_MAX_PAGE_SIZE]。
static void NormalizePaging(int32_t& page, int32_t& pageSize)
{
    if (page < 1)
        page = 1;
    if (pageSize <= 0)
        pageSize = EMPLOYEE_DEFAULT_PAGE_SIZE;
    if (pageSize > EMPLOYEE_MAX_PAGE_SIZE)
        pageSize = EMPLOYEE_MAX_PAGE_SIZE;
}

EmployeeManager::EmployeeManager()
{
    //本类不再自持用户ID分配基数——建员工时向 UserManager 要ID，
    //保证全进程只有一处在分配（理由见 createEmployee 里的注释）。
}

EmployeeManager::~EmployeeManager()
{

}

bool EmployeeManager::init()
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
    {
        LOGE("EmployeeManager::init failed to connect db, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //这里不再扫描 MAX(f_user_id) 建立自己的ID分配基数：用户ID统一由
    //UserManager 分配（它在 init 时已按同样口径取过库中最大普通用户ID）。
    LOGI("EmployeeManager::init ok, user id allocation delegated to UserManager");
    return true;
}

IDatabase* EmployeeManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("EmployeeManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

int32_t EmployeeManager::queryCount(IDatabase* pConn, const char* sql)
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

std::string EmployeeManager::generatePassword(int64_t userId)
{
    //以 用户ID | 当前时间 | 进程内自增序列 为种子做 MD5，取前若干位十六进制为密码。
    //三者组合确保即便同一秒为同一用户多次生成也互不相同。
    char seed[128] = { 0 };
    snprintf(seed, sizeof(seed), "emp|%lld|%lld|%u",
             (long long)userId, (long long)time(NULL), g_passwordSeq.fetch_add(1) + 1);
    return Md5Hex(seed).substr(0, EMPLOYEE_PASSWORD_LENGTH);
}

std::string EmployeeManager::buildListWhere(int64_t enterpriseId, int64_t deptId, const std::string& keyword)
{
    //始终排除群账号：员工列表只展示真实自然人。
    std::string where = " WHERE u.f_user_id < ";
    {
        char buf[32] = { 0 };
        snprintf(buf, sizeof(buf), "%d", EMPLOYEE_USER_ID_MAX);
        where += buf;
    }

    //企业过滤：enterpriseId 为 0 表示不限企业。
    if (enterpriseId != 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), " AND u.f_enterprise_id = %lld", (long long)enterpriseId);
        where += buf;
    }

    //部门过滤：只保留属于该部门在职成员的用户。
    if (deptId != 0)
    {
        char buf[160] = { 0 };
        snprintf(buf, sizeof(buf),
            " AND u.f_user_id IN (SELECT f_user_id FROM t_department_member "
            "WHERE f_dept_id = %lld AND f_status = 0)",
            (long long)deptId);
        where += buf;
    }

    //关键字过滤：在 姓名/用户名/工号 三列做模糊匹配。
    if (!keyword.empty())
    {
        std::string kw = dbapi::EscapeString(keyword);
        where += " AND (u.f_nickname LIKE '%" + kw + "%' OR u.f_username LIKE '%" + kw +
                 "%' OR u.f_employee_no LIKE '%" + kw + "%')";
    }

    return where;
}

void EmployeeManager::listEmployees(int64_t enterpriseId, int64_t deptId, const std::string& keyword,
                                    int32_t page, int32_t pageSize,
                                    int32_t& outTotal, std::vector<EmployeeListItem>& outList)
{
    outTotal = 0;
    outList.clear();
    NormalizePaging(page, pageSize);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildListWhere(enterpriseId, deptId, keyword);

    //1. 先取满足条件的总条数，供前端分页。
    std::string countSql = "SELECT COUNT(*) FROM t_user u" + where;
    outTotal = queryCount(pConn.get(), countSql.c_str());
    if (outTotal <= 0)
        return;

    //2. 取当前页：LEFT JOIN 主部门成员记录与部门表，带出职务与主部门名称。
    //   一名员工至多一条主部门记录（f_is_primary=1 且在职），故 JOIN 后仍为一行一员工。
    int32_t offset = (page - 1) * pageSize;
    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT u.f_user_id, u.f_nickname, u.f_role, u.f_status, d.f_dept_name, m.f_position "
        "FROM t_user u "
        "LEFT JOIN t_department_member m ON u.f_user_id = m.f_user_id "
        "AND m.f_is_primary = 1 AND m.f_status = 0 "
        "LEFT JOIN t_department d ON m.f_dept_id = d.f_dept_id "
        "%s ORDER BY u.f_user_id LIMIT %d, %d",
        where.c_str(), offset, pageSize);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        EmployeeListItem item;
        item.userid   = pRow[0].getInt32();
        item.name     = pRow[1].getString();
        item.role     = pRow[2].getInt32();
        item.status   = pRow[3].getInt32();
        item.deptname = pRow[4].getString();
        item.title    = pRow[5].getString();
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

bool EmployeeManager::getEmployeeDetail(int64_t userId, EmployeeDetail& outDetail)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //1. 取 t_user 基础字段。
    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_username, f_nickname, f_role, f_status, f_employee_no, "
        "f_join_date, f_landline, f_workstation, f_phonenumber, f_mail, f_signature, "
        "f_address, f_gender, f_birthday, f_register_time "
        "FROM t_user WHERE f_user_id = %lld",
        (long long)userId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outDetail.userid       = userId;
        outDetail.enterpriseid = pRow[0].getInt32();
        outDetail.username     = pRow[1].getString();
        outDetail.name         = pRow[2].getString();
        outDetail.role         = pRow[3].getInt32();
        outDetail.status       = pRow[4].getInt32();
        outDetail.employeeNo   = pRow[5].getString();
        outDetail.joinDate     = pRow[6].getString();
        outDetail.landline     = pRow[7].getString();
        outDetail.workstation  = pRow[8].getString();
        outDetail.phone        = pRow[9].getString();
        outDetail.email        = pRow[10].getString();
        outDetail.signature    = pRow[11].getString();
        outDetail.address      = pRow[12].getString();
        outDetail.gender       = pRow[13].getInt32();
        outDetail.birthday     = pRow[14].getInt32();
        outDetail.registerTime = pRow[15].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //2. 取主部门（f_is_primary 优先），带出主部门ID、名称与职务。
    snprintf(sql, sizeof(sql),
        "SELECT d.f_dept_id, d.f_dept_name, m.f_position "
        "FROM t_department_member m JOIN t_department d ON m.f_dept_id = d.f_dept_id "
        "WHERE m.f_user_id = %lld AND m.f_status = 0 "
        "ORDER BY m.f_is_primary DESC, m.f_dept_id LIMIT 1",
        (long long)userId);
    pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        pRow = pResult->fetch();
        if (pRow != NULL)
        {
            outDetail.deptid   = pRow[0].getInt32();
            outDetail.deptname = pRow[1].getString();
            outDetail.title    = pRow[2].getString();
        }
        pResult->endQuery();
        delete pResult;
    }

    return true;
}

int32_t EmployeeManager::createEmployee(const CreateEmployeeReq& req, int64_t& outUserId,
                                        std::string& outInitPassword, std::string& outUsername)
{
    if (req.name.empty())
    {
        LOGE("createEmployee failed: empty name");
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //登录账号(f_username)与工号(f_employee_no)是两个独立字段：登录账号由
    //服务器自动生成、全局唯一(取 "EMP"+用户ID)；工号取表单 employeeNo，可空。
    //账号的生成规则与 PC 端自助注册共用 MakeEmployeeAccount，见 AccountName.h。
    std::string employeeNo = req.employeeNo;

    //工号非空时校验企业内不重复(对齐 uk_ent_employee_no 约束，先查给友好提示)。
    if (!employeeNo.empty())
    {
        std::string noEsc = dbapi::EscapeString(employeeNo);
        char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id = %lld AND f_employee_no = '%s'",
            (long long)req.enterpriseId, noEsc.c_str());
        if (queryCount(pConn.get(), sql) > 0)
        {
            LOGE("createEmployee failed: employee_no already exists in enterprise %lld: %s",
                 (long long)req.enterpriseId, employeeNo.c_str());
            return error_code_registeralready;
        }
    }

    //分配新用户ID，并据此生成全局唯一的登录账号。
    //ID 一律向 UserManager 要 —— 它是本进程唯一的用户ID分配入口。本类历史上
    //自己维护过一份 m_baseUserId，两份基数启动时都取库中最大ID，管理员建员工
    //与用户自助注册同时发生时会分配出同一个ID、撞上 t_user 主键，已改掉。
    int64_t newUserId = Singleton<UserManager>::Instance().allocUserId();
    std::string username = MakeEmployeeAccount(newUserId);
    if (username.empty())
    {
        LOGE("createEmployee failed: cannot make account name for userid %lld", (long long)newUserId);
        return error_code_unknown;
    }
    //生成明文初始口令，并按 v2 的口令存储格式算出入库用的哈希与盐值。
    //明文只用于回给管理员转告员工，绝不入库 —— 库里存的必须是
    //MD5(SHA256(明文) + salt)，与 ChatSession 注册流程、以及登录时
    //PasswordHasher::verifyPassword 的比对方式完全一致。
    //这里之所以要服务端自己算第一层 SHA-256（正常登录时该层由客户端算），
    //是因为这个场景的明文是服务端生成的，客户端全程不参与。
    std::string password = generatePassword(newUserId);
    std::string salt = PasswordHasher::generateSalt();
    std::string storedPassword = PasswordHasher::hashPassword(Sha256::hexDigest(password), salt);

    //入库：仅写 t_user；入职日期为空则写 NULL。
    std::string nicknameEsc = dbapi::EscapeString(req.name);
    std::string usernameEsc = dbapi::EscapeString(username);
    std::string phoneEsc = dbapi::EscapeString(req.phone);
    std::string mailEsc = dbapi::EscapeString(req.email);
    std::string joinDateValue;
    if (req.joinDate.empty())
    {
        joinDateValue = "NULL";
    }
    else
    {
        joinDateValue = "'" + dbapi::EscapeString(req.joinDate) + "'";
    }
    //工号为空写 NULL：uk_ent_employee_no 把多个 NULL 视为互不相同、空串视为冲突。
    std::string employeeNoValue;
    if (employeeNo.empty())
    {
        employeeNoValue = "NULL";
    }
    else
    {
        employeeNoValue = "'" + dbapi::EscapeString(employeeNo) + "'";
    }

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_salt, f_facetype, "
        "f_gender, f_birthday, f_signature, f_phonenumber, f_mail, f_register_time, "
        "f_enterprise_id, f_role, f_employee_no, f_join_date, f_status) "
        "VALUES(%lld, '%s', '%s', '%s', '%s', 0, 0, %d, '', '%s', '%s', NOW(), %lld, 0, %s, %s, 0)",
        (long long)newUserId, usernameEsc.c_str(), nicknameEsc.c_str(),
        storedPassword.c_str(), salt.c_str(),
        EMPLOYEE_DEFAULT_BIRTHDAY, phoneEsc.c_str(), mailEsc.c_str(),
        (long long)req.enterpriseId, employeeNoValue.c_str(), joinDateValue.c_str());
    if (!pConn->execute(sql))
    {
        LOGE("createEmployee failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outUserId = newUserId;
    outInitPassword = password;
    outUsername = username;
    //把新员工同步进 IM 侧 UserManager 缓存，使其无需重启 chatserver 即可登录。
    Singleton<UserManager>::Instance().reloadUserCache(static_cast<int32_t>(newUserId));
    LOGI("createEmployee ok, userid: %lld, username: %s, enterpriseid: %lld",
         (long long)newUserId, username.c_str(), (long long)req.enterpriseId);
    return error_code_ok;
}

int32_t EmployeeManager::updateEmployee(const UpdateEmployeeReq& req)
{
    if (req.name.empty())
    {
        LOGE("updateEmployee failed: empty name, userid: %lld", (long long)req.userId);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //目标用户必须存在。
    {
        char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_user WHERE f_user_id = %lld", (long long)req.userId);
        if (queryCount(pConn.get(), sql) <= 0)
        {
            LOGE("updateEmployee failed: user not exist, userid: %lld", (long long)req.userId);
            return error_code_notregister;
        }
    }

    std::string joinDateValue;
    if (req.joinDate.empty())
    {
        joinDateValue = "NULL";
    }
    else
    {
        joinDateValue = "'" + dbapi::EscapeString(req.joinDate) + "'";
    }

    //1. 更新 t_user 基础字段。
    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_nickname = '%s', f_employee_no = '%s', f_join_date = %s, "
        "f_landline = '%s', f_workstation = '%s', f_phonenumber = '%s', f_mail = '%s', "
        "f_signature = '%s' WHERE f_user_id = %lld",
        dbapi::EscapeString(req.name).c_str(), dbapi::EscapeString(req.employeeNo).c_str(), joinDateValue.c_str(),
        dbapi::EscapeString(req.landline).c_str(), dbapi::EscapeString(req.workstation).c_str(),
        dbapi::EscapeString(req.phone).c_str(), dbapi::EscapeString(req.email).c_str(),
        dbapi::EscapeString(req.signature).c_str(), (long long)req.userId);
    if (!pConn->execute(sql))
    {
        LOGE("updateEmployee failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    //2. 同步把职务写入主部门成员记录的 f_position（无主部门记录则该 UPDATE 影响 0 行）。
    //   此处直改 t_department_member、不经 DepartmentManager 的写方法，故其内存缓存
    //   不会自行重载，须在下面显式同步一份，否则 tree.do 与客户端通讯录
    //   （cmd_get_contacts 的数据源即该缓存）读到的都还是旧职务。
    snprintf(sql, sizeof(sql),
        "UPDATE t_department_member SET f_position = '%s' "
        "WHERE f_user_id = %lld AND f_is_primary = 1 AND f_status = 0",
        dbapi::EscapeString(req.title).c_str(), (long long)req.userId);
    if (!pConn->execute(sql))
    {
        //职务同步失败不致命：基础信息已更新成功，仅记日志。
        LOGE("updateEmployee: sync position to dept member failed, userid: %lld", (long long)req.userId);
    }
    else
    {
        //库改成功才同步缓存：写库失败时缓存保持旧值，与库一致，不制造"内存比库新"的假象。
        Singleton<DepartmentManager>::Instance().updateCachedMemberPosition(req.userId, req.title);
    }

    //把改动同步进 IM 侧 UserManager 缓存（昵称、签名、手机号等会即时对 IM 端生效）。
    Singleton<UserManager>::Instance().reloadUserCache(static_cast<int32_t>(req.userId));
    LOGI("updateEmployee ok, userid: %lld", (long long)req.userId);
    return error_code_ok;
}

int32_t EmployeeManager::setEmployeeStatus(int64_t userId, int32_t status)
{
    //账号状态仅 0=启用 / 1=停用 两种取值。
    if (status != 0 && status != 1)
    {
        LOGE("setEmployeeStatus failed: invalid status %d, userid: %lld", status, (long long)userId);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_user WHERE f_user_id = %lld", (long long)userId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("setEmployeeStatus failed: user not exist, userid: %lld", (long long)userId);
        return error_code_notregister;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_status = %d WHERE f_user_id = %lld", status, (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("setEmployeeStatus failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    //同步进 IM 侧缓存：缓存的 status 更新后，IM 登录链路即可据此拒绝已停用账号。
    Singleton<UserManager>::Instance().reloadUserCache(static_cast<int32_t>(userId));
    LOGI("setEmployeeStatus ok, userid: %lld, status: %d", (long long)userId, status);
    return error_code_ok;
}

int32_t EmployeeManager::deleteEmployee(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_user WHERE f_user_id = %lld", (long long)userId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("deleteEmployee failed: user not exist, userid: %lld", (long long)userId);
        return error_code_notregister;
    }

    //先清理从属数据，再删主记录：部门成员记录、权限覆盖记录。
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_department_member WHERE f_user_id = %lld", (long long)userId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql),
        "DELETE FROM t_user_permission WHERE f_user_id = %lld", (long long)userId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql),
        "DELETE FROM t_user WHERE f_user_id = %lld", (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("deleteEmployee failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    //从 IM 侧缓存中移除该用户，使其无法再登录（否则重启前仍能用旧缓存登录）。
    Singleton<UserManager>::Instance().reloadUserCache(static_cast<int32_t>(userId));

    //同步把他从组织架构缓存的各部门成员列表里摘掉。这一步不能省：上面那句已经把
    //他的资料从 UserManager 移除了，若组织架构缓存里还留着他，onGetContacts 就会
    //在部门里列出一个查不到资料的 userid，客户端只能显示成"用户<userid>"；管理端
    //的组织架构树同样会继续显示他，看上去像是"删不掉"。
    Singleton<DepartmentManager>::Instance().removeCachedMember(userId);

    LOGI("deleteEmployee ok, userid: %lld", (long long)userId);
    return error_code_ok;
}

int32_t EmployeeManager::resetPassword(int64_t userId, std::string& outNewPassword)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_user WHERE f_user_id = %lld", (long long)userId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("resetPassword failed: user not exist, userid: %lld", (long long)userId);
        return error_code_notregister;
    }

    //与 createEmployee 同理：明文只回给管理员，入库的是 MD5(SHA256(明文) + salt)。
    //盐值必须一并重新生成并写回 —— 沿用旧盐会让「重置口令」这个动作无法作废
    //可能已经泄露的旧盐，而且历史遗留账号的 f_salt 本来就是空的。
    std::string newPwd = generatePassword(userId);
    std::string salt = PasswordHasher::generateSalt();
    std::string storedPassword = PasswordHasher::hashPassword(Sha256::hexDigest(newPwd), salt);
    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_password = '%s', f_salt = '%s' WHERE f_user_id = %lld",
        storedPassword.c_str(), salt.c_str(), (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("resetPassword failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outNewPassword = newPwd;
    //登录时比对的是 UserManager 缓存里的 password / salt（见 ChatSession::onLogin），
    //不刷新缓存的话新口令要等 chatserver 重启才生效，表现为「刚重置的密码登录不了」。
    //createEmployee 一直有这一步，这里此前是漏的。
    Singleton<UserManager>::Instance().reloadUserCache(static_cast<int32_t>(userId));
    LOGI("resetPassword ok, userid: %lld", (long long)userId);
    return error_code_ok;
}

void EmployeeManager::listLoginRecords(int64_t userId, int32_t page, int32_t pageSize,
                                       int32_t& outTotal, std::vector<LoginRecordItem>& outList)
{
    outTotal = 0;
    outList.clear();
    NormalizePaging(page, pageSize);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_login_record WHERE f_user_id = %lld", (long long)userId);
    outTotal = queryCount(pConn.get(), sql);
    if (outTotal <= 0)
        return;

    int32_t offset = (page - 1) * pageSize;
    snprintf(sql, sizeof(sql),
        "SELECT f_login_time, f_device, f_ip, f_location, f_result, f_fail_reason "
        "FROM t_login_record WHERE f_user_id = %lld "
        "ORDER BY f_login_time DESC LIMIT %d, %d",
        (long long)userId, offset, pageSize);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        LoginRecordItem item;
        item.ts         = pRow[0].getString();
        item.device     = pRow[1].getString();
        item.ip         = pRow[2].getString();
        item.location   = pRow[3].getString();
        item.result     = pRow[4].getInt32();
        item.failReason = pRow[5].getString();
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

void EmployeeManager::listActivities(int64_t userId, int32_t rangeDays, int32_t page, int32_t pageSize,
                                     int32_t& outTotal, std::vector<ActivityItem>& outList)
{
    outTotal = 0;
    outList.clear();
    NormalizePaging(page, pageSize);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //时间窗口子句：rangeDays > 0 时只取最近 rangeDays 天的记录。
    std::string rangeClause;
    if (rangeDays > 0)
    {
        //时间下界的表达式按当前数据库后端生成，两种方言的 INTERVAL 写法不同
        std::string sinceExpr = dbapi::NowMinusDays(rangeDays);
        char buf[128] = { 0 };
        snprintf(buf, sizeof(buf), " AND f_create_time >= %s", sinceExpr.c_str());
        rangeClause = buf;
    }

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_user_activity_log WHERE f_user_id = %lld%s",
        (long long)userId, rangeClause.c_str());
    outTotal = queryCount(pConn.get(), sql);
    if (outTotal <= 0)
        return;

    int32_t offset = (page - 1) * pageSize;
    snprintf(sql, sizeof(sql),
        "SELECT f_create_time, f_action, f_detail, f_severity "
        "FROM t_user_activity_log WHERE f_user_id = %lld%s "
        "ORDER BY f_create_time DESC LIMIT %d, %d",
        (long long)userId, rangeClause.c_str(), offset, pageSize);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        ActivityItem item;
        item.ts       = pRow[0].getString();
        item.action   = pRow[1].getString();
        item.detail   = pRow[2].getString();
        item.severity = pRow[3].getInt32();
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

int64_t EmployeeManager::getEmployeeEnterpriseId(int64_t userId)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return 0;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id FROM t_user WHERE f_user_id = %lld", (long long)userId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return 0;

    int64_t enterpriseId = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        enterpriseId = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return enterpriseId;
}

//==== 角色与权限 ====

//企业内角色取值，与 t_user.f_role 及接口文档 6.1 一致。
#define EMPLOYEE_ROLE_MEMBER  0  //普通成员：仅查看与协作
#define EMPLOYEE_ROLE_LEAD    1  //部门负责人：管理所属部门成员与权限
#define EMPLOYEE_ROLE_ADMIN   2  //企业管理员：全权管理本企业组织、成员、设置

//单个权限项的常量定义。
struct PermissionDef
{
    const char* group;             //权限分组名
    const char* key;               //权限项键名
    const char* label;             //权限项中文名
    bool        defaultForCommon;  //普通成员/部门负责人的默认值（企业管理员一律为 true）
};

//权限矩阵常量表：13 个权限项及各角色默认值固定为代码常量
//（见 table.sql 中 t_user_permission 的注释），取自设计稿
//design/FlamingoIM Enterprise Admin.html 的 PERMISSION_GROUPS。
//Chinese 字面量须用 u8""，否则经 HTTP/JSON 输出会因 MSVC 按 GBK 编码而乱码。
static const PermissionDef g_permissionDefs[] =
{
    { u8"通讯",       "msg.send",         u8"发送消息",             true  },
    { u8"通讯",       "msg.recall",       u8"撤回消息（5 分钟内）", true  },
    { u8"通讯",       "msg.audio_video",  u8"发起音视频通话",       true  },
    { u8"通讯",       "msg.external",     u8"添加外部联系人",       false },
    { u8"文件与会议", "file.upload",      u8"上传文件 (≤500MB)",    true  },
    { u8"文件与会议", "file.large",       u8"上传大文件 (>500MB)",  false },
    { u8"文件与会议", "meet.create",      u8"创建会议室",           true  },
    { u8"文件与会议", "meet.record",      u8"会议录制",             false },
    { u8"群组",       "group.create",     u8"创建群组",             true  },
    { u8"群组",       "group.invite_ext", u8"邀请外部成员入群",     false },
    { u8"群组",       "group.broadcast",  u8"发送群公告",           false },
    { u8"数据",       "data.export_chat", u8"导出个人聊天记录",     true  },
    { u8"数据",       "data.export_org",  u8"导出企业通讯录",       false },
};

//权限项总数（由常量表长度推导，与 t_user_permission 注释的「13 个」一致）。
static const int32_t PERMISSION_DEF_COUNT =
    (int32_t)(sizeof(g_permissionDefs) / sizeof(g_permissionDefs[0]));

//求某角色下某权限项的默认值：企业管理员对所有项默认放开，其余角色取该项的通用默认。
static bool RoleDefaultOf(int32_t role, const PermissionDef& def)
{
    if (role == EMPLOYEE_ROLE_ADMIN)
        return true;
    return def.defaultForCommon;
}

int32_t EmployeeManager::setRole(int64_t userId, int32_t role)
{
    //角色只允许 0/1/2 三种取值。
    if (role != EMPLOYEE_ROLE_MEMBER && role != EMPLOYEE_ROLE_LEAD && role != EMPLOYEE_ROLE_ADMIN)
    {
        LOGE("setRole failed: invalid role %d, userid: %lld", role, (long long)userId);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_user WHERE f_user_id = %lld", (long long)userId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("setRole failed: user not exist, userid: %lld", (long long)userId);
        return error_code_notregister;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_user SET f_role = %d WHERE f_user_id = %lld", role, (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("setRole failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    //改角色后清空该员工的全部逐人权限覆盖，使权限回到新角色的默认值。
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_user_permission WHERE f_user_id = %lld", (long long)userId);
    pConn->execute(sql);

    LOGI("setRole ok, userid: %lld, role: %d", (long long)userId, role);
    return error_code_ok;
}

bool EmployeeManager::getPermissions(int64_t userId, int32_t& outRole, std::vector<PermissionItem>& outList)
{
    outList.clear();

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //1. 取员工角色，同时校验员工存在。
    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_role FROM t_user WHERE f_user_id = %lld", (long long)userId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outRole = pRow[0].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //2. 取该员工的全部逐人覆盖，存入并列数组（至多 13 条，线性查找足够）。
    std::vector<std::string> ovKeys;
    std::vector<bool>        ovGranted;
    snprintf(sql, sizeof(sql),
        "SELECT f_perm_key, f_granted FROM t_user_permission WHERE f_user_id = %lld",
        (long long)userId);
    pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        while (true)
        {
            pRow = pResult->fetch();
            if (pRow == NULL)
                break;
            ovKeys.push_back(pRow[0].getString());
            ovGranted.push_back(pRow[1].getInt32() != 0);
            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    //3. 逐个权限项算出 默认值 / 是否被覆盖 / 生效值。
    for (int32_t i = 0; i < PERMISSION_DEF_COUNT; ++i)
    {
        const PermissionDef& def = g_permissionDefs[i];
        PermissionItem item;
        item.group       = def.group;
        item.key         = def.key;
        item.label       = def.label;
        item.roleDefault = RoleDefaultOf(outRole, def);
        item.overridden  = false;
        item.effective   = item.roleDefault;

        for (size_t j = 0; j < ovKeys.size(); ++j)
        {
            if (ovKeys[j] == def.key)
            {
                item.overridden = true;
                item.effective  = ovGranted[j];
                break;
            }
        }
        outList.push_back(item);
    }

    return true;
}

int32_t EmployeeManager::setPermissions(int64_t userId,
                                        const std::vector<std::pair<std::string, bool> >& perms)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //取员工角色（用于判定哪些提交项真正构成覆盖），同时校验员工存在。
    int32_t role = EMPLOYEE_ROLE_MEMBER;
    {
        char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
        snprintf(sql, sizeof(sql),
            "SELECT f_role FROM t_user WHERE f_user_id = %lld", (long long)userId);
        QueryResult* pResult = pConn->query(sql);
        if (pResult == NULL)
            return error_code_unknown;
        bool found = false;
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            role = pRow[0].getInt32();
            found = true;
        }
        pResult->endQuery();
        delete pResult;
        if (!found)
        {
            LOGE("setPermissions failed: user not exist, userid: %lld", (long long)userId);
            return error_code_notregister;
        }
    }

    //整体替换语义：先清空该员工的旧覆盖。
    char sql[EMPLOYEE_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_user_permission WHERE f_user_id = %lld", (long long)userId);
    pConn->execute(sql);

    //逐项写入：仅键名合法、且取值与角色默认不同的项才作为覆盖入库。
    for (size_t i = 0; i < perms.size(); ++i)
    {
        const std::string& key = perms[i].first;
        bool granted = perms[i].second;

        //在常量表中定位该权限项。
        const PermissionDef* pDef = NULL;
        for (int32_t k = 0; k < PERMISSION_DEF_COUNT; ++k)
        {
            if (key == g_permissionDefs[k].key)
            {
                pDef = &g_permissionDefs[k];
                break;
            }
        }
        if (pDef == NULL)
            continue;  //未知权限键，忽略
        if (granted == RoleDefaultOf(role, *pDef))
            continue;  //与角色默认相同，不构成覆盖

        std::string keyEsc = dbapi::EscapeString(key);
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_user_permission(f_user_id, f_perm_key, f_granted) "
            "VALUES(%lld, '%s', %d)",
            (long long)userId, keyEsc.c_str(), granted ? 1 : 0);
        if (!pConn->execute(sql))
        {
            LOGE("setPermissions failed: insert error, sql: %s", sql);
            return error_code_unknown;
        }
    }

    LOGI("setPermissions ok, userid: %lld, submitted: %u",
         (long long)userId, (uint32_t)perms.size());
    return error_code_ok;
}
