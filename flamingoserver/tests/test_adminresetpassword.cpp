/**
 *  管理后台「重置员工登录密码」的服务端测试，test_adminresetpassword.cpp
 *
 *  背景：flamingoAdmin 员工详情的「重置登录密码」调 /admin/user/resetpassword.do（HttpSession::onAdminUserResetPassword），
 *  服务端经 EmployeeManager::resetPassword 生成新密码、换新盐、写库并刷新 UserManager 缓存，再踢掉该员工的在线设备。
 *  2026-10-07 之前这条路径没有任何单测（核实结论见用户 2026-10-07 的确认），本套用例对着临时库真的跑 SQL：
 *    - APR1：重置后返回 10 位十六进制的新密码，与初始密码不同；按登录时的比对方式（PasswordHasher::verifyPassword，
 *            客户端发来的是明文的 SHA-256）新密码能通过、旧密码不能；盐换成了新的；库里的口令与盐与缓存一致；
 *    - APR2：连续重置两次，两次的新密码不同，第一次的新密码随之失效；
 *    - APR3：员工不存在时返回 error_code_notregister，不给出新密码；
 *    - APR4：跨企业拦截所依赖的数据：getEmployeeEnterpriseId 取到员工所属的企业，员工不存在时为 0。接口层据此拒绝
 *            企业管理员重置别的企业的员工（HttpSession.cpp 里 admin.enterpriseid != 0 && empEnt != admin.enterpriseid
 *            那一行），那一行比较本身在 HTTP 处理函数里，服务端测试没有调用 HTTP 处理函数的手段，不在本套覆盖范围内。
 *
 *  每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <ctype.h>
#include <stdio.h>
#include <memory>
#include <string>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/EmployeeManager.h"
#include "../chatserversrc/UserManager.h"
#include "../chatserversrc/AdminApiCodes.h"   //error_code_ok / error_code_notregister
#include "../utils/PasswordHasher.h"
#include "../utils/Sha256.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const APR_DB_PATH = "flamingo_sqlite_adminresetpassword_test.db";
static const char* const APR_MYSQL_DB = "flamingo_test_adminresetpassword";

//测试员工所属的两个企业 ID（APR4 用来区分；只用于落库）
static const int64_t APR_ENTERPRISE_A = 1;
static const int64_t APR_ENTERPRISE_B = 2;

//重置后的明文密码长度，与 EmployeeManager.cpp 里的 EMPLOYEE_PASSWORD_LENGTH 相同（那个宏只在 .cpp 里，测试看不到）
static const size_t APR_PASSWORD_LENGTH = 10;

//一个肯定不存在的 userid（测试库里新建的员工 ID 从很小的数开始）
static const int64_t APR_MISSING_USER = 987654;

//生成一个本进程里不重复的合法手机号（138 + 8 位序号）。用户缓存是进程级单例、换库时不清空，序号在各用例之间持续递增
static std::string aprNextPhone()
{
    static int s_seq = 0;
    char buf[16] = { 0 };
    snprintf(buf, sizeof(buf), "138%08d", ++s_seq);
    return std::string(buf);
}

//在 enterpriseId 下新建一名不归属任何部门的员工，返回其 userid 与初始密码；失败时让用例停下
static int64_t aprCreate(int64_t enterpriseId, std::string& initPassword)
{
    CreateEmployeeReq req;
    req.enterpriseId = enterpriseId;
    req.deptId = 0;
    req.name = "apr";
    req.email = "apr@test.com";
    req.phone = aprNextPhone();
    int64_t userId = 0;
    std::string username;
    REQUIRE(Singleton<EmployeeManager>::Instance().createEmployee(req, userId, initPassword, username)
            == error_code_ok);
    REQUIRE(userId > 0);
    return userId;
}

//按登录时的比对方式检查明文密码 plain 能否通过：取 UserManager 缓存里的口令与盐，客户端发来的是明文的 SHA-256
static bool aprLoginAccepts(int64_t userId, const std::string& plain)
{
    User u;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(userId), u))
        return false;
    return PasswordHasher::verifyPassword(Sha256::hexDigest(plain), u.password, u.salt);
}

//缓存里的盐；取不到时返回空串
static std::string aprCachedSalt(int64_t userId)
{
    User u;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(userId), u))
        return std::string();
    return u.salt;
}

//绕过缓存从库里读 f_password 与 f_salt；读不到时返回 false
static bool aprReadStored(int64_t userId, std::string& password, std::string& salt)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return false;
    const std::string sql = "SELECT f_password, f_salt FROM t_user WHERE f_user_id = " + std::to_string(userId);
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return false;
    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        password = pRow[0].getString();
        salt = pRow[1].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

//是否全部是十六进制字符
static bool aprIsHex(const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (!isxdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    return !s.empty();
}

TEST_CASE("APR1 重置后新密码能登录、旧密码失效，换了新盐，库与缓存一致")
{
    testdb::resetDb(APR_DB_PATH, APR_MYSQL_DB);
    std::string initPassword;
    const int64_t userId = aprCreate(APR_ENTERPRISE_A, initPassword);
    REQUIRE(aprLoginAccepts(userId, initPassword));
    const std::string oldSalt = aprCachedSalt(userId);

    std::string newPassword;
    REQUIRE(Singleton<EmployeeManager>::Instance().resetPassword(userId, newPassword) == error_code_ok);
    CHECK(newPassword.size() == APR_PASSWORD_LENGTH);
    CHECK(aprIsHex(newPassword));
    CHECK(newPassword != initPassword);

    CHECK(aprLoginAccepts(userId, newPassword));
    CHECK_FALSE(aprLoginAccepts(userId, initPassword));
    const std::string newSalt = aprCachedSalt(userId);
    CHECK_FALSE(newSalt.empty());
    CHECK(newSalt != oldSalt);

    std::string storedPassword;
    std::string storedSalt;
    REQUIRE(aprReadStored(userId, storedPassword, storedSalt));
    User u;
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(userId), u));
    CHECK(storedPassword == u.password);
    CHECK(storedSalt == u.salt);
    testdb::cleanupDb(APR_DB_PATH);
}

TEST_CASE("APR2 连续重置两次，两次的新密码不同，第一次的随之失效")
{
    testdb::resetDb(APR_DB_PATH, APR_MYSQL_DB);
    std::string initPassword;
    const int64_t userId = aprCreate(APR_ENTERPRISE_A, initPassword);

    std::string first;
    std::string second;
    REQUIRE(Singleton<EmployeeManager>::Instance().resetPassword(userId, first) == error_code_ok);
    REQUIRE(Singleton<EmployeeManager>::Instance().resetPassword(userId, second) == error_code_ok);
    CHECK(first != second);
    CHECK(aprLoginAccepts(userId, second));
    CHECK_FALSE(aprLoginAccepts(userId, first));
    CHECK_FALSE(aprLoginAccepts(userId, initPassword));
    testdb::cleanupDb(APR_DB_PATH);
}

TEST_CASE("APR3 员工不存在时返回 error_code_notregister，不给出新密码")
{
    testdb::resetDb(APR_DB_PATH, APR_MYSQL_DB);
    std::string newPassword;
    CHECK(Singleton<EmployeeManager>::Instance().resetPassword(APR_MISSING_USER, newPassword)
          == error_code_notregister);
    CHECK(newPassword.empty());
    testdb::cleanupDb(APR_DB_PATH);
}

TEST_CASE("APR4 跨企业拦截依赖的数据：取到员工所属企业，员工不存在时为 0")
{
    testdb::resetDb(APR_DB_PATH, APR_MYSQL_DB);
    std::string passwordA;
    std::string passwordB;
    const int64_t userA = aprCreate(APR_ENTERPRISE_A, passwordA);
    const int64_t userB = aprCreate(APR_ENTERPRISE_B, passwordB);
    EmployeeManager& mgr = Singleton<EmployeeManager>::Instance();
    CHECK(mgr.getEmployeeEnterpriseId(userA) == APR_ENTERPRISE_A);
    CHECK(mgr.getEmployeeEnterpriseId(userB) == APR_ENTERPRISE_B);
    //企业 A 的管理员（enterpriseid 为 A）对 userB：接口层比较 B != A，拒绝
    CHECK(mgr.getEmployeeEnterpriseId(userB) != APR_ENTERPRISE_A);
    //员工不存在：取到 0，企业管理员同样被拒绝；平台管理员（enterpriseid 为 0）放行后由 resetPassword 返回 notregister
    CHECK(mgr.getEmployeeEnterpriseId(APR_MISSING_USER) == 0);
    testdb::cleanupDb(APR_DB_PATH);
}
