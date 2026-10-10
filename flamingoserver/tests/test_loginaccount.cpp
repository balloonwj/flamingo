/**
 *  登录账号与手机号登录（docs/登录账号与手机号登录方案.md）服务端单元测试（Suite LGA），test_loginaccount.cpp
 *
 *  纯函数部分（LGA1 随机账号拼接、LGA2 前缀规整、LGA8 手机号格式）在 test_accountname.cpp；员工工号为空与
 *  重复（LGA7）、员工手机号（LGA9）在 test_employeegender.cpp；注册的手机号格式（LGA10）在 test_registerservice.cpp。
 *  本文件是要连库跑的那几条：
 *    - LGA3 allocRandomAccount：账号格式为前缀 + 8 位数字；抽中已有账号（含只差大小写的情形）时重抽；
 *           固定种子下连续抽 1000 个互不相同，且不是连续号码；
 *    - LGA4 addUser 账号留空时抽一个 EMP + 8 位随机数字的账号，不再是 EMP + 用户ID；
 *    - LGA5 管理员新建员工：企业设了前缀时账号是前缀 + 8 位数字，没设时是 EMP + 8 位数字；
 *    - LGA6 企业账号前缀的存取：setAccountPrefix 规整后写库、不合格返回错误码且不写库、空串清除；
 *           getAccountPrefix、getEnterprise、listEnterprises 都带前缀；
 *    - LGA11 findLoginUser：账号不区分大小写；账号查不到且输入像手机号时按手机号找到；
 *            账号恰好是 11 位数字时优先按账号；都找不到返回失败。
 *  自助注册（ChatSession 里的建号）与 update.do 的「带了才改」在 tmp/loginacct 的端到端联调里覆盖。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/UserManager.h"
#include "../chatserversrc/EnterpriseManager.h"
#include "../chatserversrc/EmployeeManager.h"
#include "../chatserversrc/AccountName.h"
#include "../chatserversrc/AdminApiCodes.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const LGA_DB_PATH = "flamingo_sqlite_loginaccount_test.db";
static const char* const LGA_MYSQL_DB = "flamingo_test_loginaccount";

//测试用的两家企业：一家设前缀、一家不设
static const int64_t LGA_ENT_WITH_PREFIX = 9301;
static const int64_t LGA_ENT_NO_PREFIX = 9302;

//LGA3 固定的随机数种子（取值无特殊含义，只为让抽号序列可复现）
static const uint32_t LGA_RNG_SEED = 20261006;

//LGA3 连续抽号的次数
static const int LGA_DRAW_COUNT = 1000;

//在当前测试库里插入一家企业（只填建号用得到的列）
static void lgaInsertEnterprise(int64_t enterpriseId, const char* name)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    const std::string sql = "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_status, f_create_time) VALUES("
                            + std::to_string(enterpriseId) + ", '" + name + "', 1, '2026-10-06 10:00:00')";
    REQUIRE(pConn->execute(sql.c_str()));
}

//账号是否形如「prefix + 8 位数字」
static bool lgaLooksLike(const std::string& account, const std::string& prefix)
{
    if (account.size() != prefix.size() + RANDOM_ACCOUNT_DIGITS || account.compare(0, prefix.size(), prefix) != 0)
        return false;
    for (size_t i = prefix.size(); i < account.size(); ++i)
    {
        if (account[i] < '0' || account[i] > '9')
            return false;
    }
    return account[prefix.size()] != '0';
}

//用指定账号与手机号建一个普通用户（不经过员工管理），返回其 userid
static int32_t lgaAddUser(const std::string& username, const std::string& phone)
{
    User u;
    u.username = username;
    u.nickname = "lga";
    u.password = "x";
    u.salt = "s";
    u.phonenumber = phone;
    u.usertype = 0;
    REQUIRE(Singleton<UserManager>::Instance().addUser(u, LGA_ENT_NO_PREFIX));
    return u.userid;
}

//按 LGA_RNG_SEED 预测第 index 次（从 0 起）抽到的数字：与 UserManager::allocRandomAccount 用同一种发生器与分布
static int32_t lgaPredictNumber(int index)
{
    std::mt19937 rng(LGA_RNG_SEED);
    std::uniform_int_distribution<int32_t> dist(RANDOM_ACCOUNT_NUMBER_MIN, RANDOM_ACCOUNT_NUMBER_MAX);
    int32_t value = 0;
    for (int i = 0; i <= index; ++i)
        value = dist(rng);
    return value;
}

TEST_CASE("LGA3 allocRandomAccount：前缀 + 8 位数字，撞上已有账号（含只差大小写）时重抽，号码不连续")
{
    testdb::resetDb(LGA_DB_PATH, LGA_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //格式：带前缀与不带前缀
    CHECK(lgaLooksLike(userMgr.allocRandomAccount("HW."), "HW."));
    CHECK(lgaLooksLike(userMgr.allocRandomAccount(""), "EMP"));

    //撞号重抽：固定种子下预知第一次会抽到的号，先用小写占住它，抽到的应当是第二个号
    const std::string first = MakeRandomAccount("HW.", lgaPredictNumber(0));
    const std::string second = MakeRandomAccount("HW.", lgaPredictNumber(1));
    REQUIRE(first != second);
    std::string firstLower = first;
    for (size_t i = 0; i < firstLower.size(); ++i)
    {
        if (firstLower[i] >= 'A' && firstLower[i] <= 'Z')
            firstLower[i] = static_cast<char>(firstLower[i] - 'A' + 'a');
    }
    lgaAddUser(firstLower, "13800003001");
    userMgr.seedAccountRngForTest(LGA_RNG_SEED);
    CHECK(userMgr.allocRandomAccount("HW.") == second);

    //固定种子下连续抽 1000 个：互不相同，且不是一串连续号码
    userMgr.seedAccountRngForTest(LGA_RNG_SEED + 1);
    std::set<std::string> drawn;
    std::vector<int32_t> numbers;
    for (int i = 0; i < LGA_DRAW_COUNT; ++i)
    {
        const std::string account = userMgr.allocRandomAccount("");
        REQUIRE(lgaLooksLike(account, "EMP"));
        drawn.insert(account);
        numbers.push_back(atoi(account.c_str() + strlen(EMPLOYEE_ACCOUNT_PREFIX)));
    }
    CHECK(drawn.size() == (size_t)LGA_DRAW_COUNT);
    int consecutive = 0;
    for (size_t i = 1; i < numbers.size(); ++i)
    {
        if (numbers[i] == numbers[i - 1] + 1)
            ++consecutive;
    }
    CHECK(consecutive < LGA_DRAW_COUNT / 2);

    //恢复为随机播种的状态（用一个不固定的种子），不影响后面的用例
    userMgr.seedAccountRngForTest(std::random_device()());
    testdb::cleanupDb(LGA_DB_PATH);
}

TEST_CASE("LGA4 addUser 账号留空时抽 EMP + 8 位随机数字的账号，不再是 EMP + 用户ID")
{
    testdb::resetDb(LGA_DB_PATH, LGA_MYSQL_DB);
    User u;
    u.nickname = "lga4";
    u.password = "x";
    u.salt = "s";
    u.phonenumber = "13800004001";
    u.usertype = 0;
    REQUIRE(Singleton<UserManager>::Instance().addUser(u, LGA_ENT_NO_PREFIX));
    CHECK(lgaLooksLike(u.username, "EMP"));
    CHECK(u.username != MakeEmployeeAccount(u.userid));

    //缓存里能按这个账号找到
    User cached;
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUsername(u.username, cached));
    CHECK(cached.userid == u.userid);
    testdb::cleanupDb(LGA_DB_PATH);
}

TEST_CASE("LGA5 管理员新建员工：企业设了前缀时账号是前缀 + 8 位数字，没设时是 EMP + 8 位数字")
{
    testdb::resetDb(LGA_DB_PATH, LGA_MYSQL_DB);
    lgaInsertEnterprise(LGA_ENT_WITH_PREFIX, "lga5-hw");
    lgaInsertEnterprise(LGA_ENT_NO_PREFIX, "lga5-plain");
    REQUIRE(Singleton<EnterpriseManager>::Instance().setAccountPrefix(LGA_ENT_WITH_PREFIX, "hw.") == error_code_ok);

    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    CreateEmployeeReq req;
    req.enterpriseId = LGA_ENT_WITH_PREFIX;
    req.name = "lga5";
    req.email = "lga5@test.com";
    req.phone = "13800005001";
    int64_t userId = 0;
    std::string initPassword;
    std::string username;
    REQUIRE(empMgr.createEmployee(req, userId, initPassword, username) == error_code_ok);
    CHECK(lgaLooksLike(username, "HW."));

    CreateEmployeeReq plain = req;
    plain.enterpriseId = LGA_ENT_NO_PREFIX;
    plain.phone = "13800005002";
    std::string plainUsername;
    REQUIRE(empMgr.createEmployee(plain, userId, initPassword, plainUsername) == error_code_ok);
    CHECK(lgaLooksLike(plainUsername, "EMP"));
    CHECK(plainUsername != MakeEmployeeAccount(userId));
    testdb::cleanupDb(LGA_DB_PATH);
}

TEST_CASE("LGA6 企业账号前缀的存取：规整后写库，不合格不写，空串清除；读企业时都带前缀")
{
    testdb::resetDb(LGA_DB_PATH, LGA_MYSQL_DB);
    lgaInsertEnterprise(LGA_ENT_WITH_PREFIX, "lga6");
    EnterpriseManager& entMgr = Singleton<EnterpriseManager>::Instance();

    //新企业没设前缀
    CHECK(entMgr.getAccountPrefix(LGA_ENT_WITH_PREFIX).empty());

    //设前缀：去空白、转大写后写库，详情与列表都带上
    CHECK(entMgr.setAccountPrefix(LGA_ENT_WITH_PREFIX, " fl.im ") == error_code_ok);
    CHECK(entMgr.getAccountPrefix(LGA_ENT_WITH_PREFIX) == "FL.IM");
    EnterpriseInfo info;
    REQUIRE(entMgr.getEnterprise(LGA_ENT_WITH_PREFIX, info));
    CHECK(info.accountPrefix == "FL.IM");
    EnterpriseListFilter filter;
    filter.keyword = "lga6";
    int32_t total = 0;
    std::vector<EnterpriseInfo> list;
    entMgr.listEnterprises(filter, 1, 20, total, list);
    REQUIRE(list.size() == 1);
    CHECK(list[0].accountPrefix == "FL.IM");

    //不合格：返回错误码，库里保持原值
    CHECK(entMgr.setAccountPrefix(LGA_ENT_WITH_PREFIX, "H-W") == error_code_account_prefix_invalid);
    CHECK(entMgr.setAccountPrefix(LGA_ENT_WITH_PREFIX, "ABCDEFGHIJK") == error_code_account_prefix_invalid);
    CHECK(entMgr.getAccountPrefix(LGA_ENT_WITH_PREFIX) == "FL.IM");

    //空串：清除，改回默认
    CHECK(entMgr.setAccountPrefix(LGA_ENT_WITH_PREFIX, "") == error_code_ok);
    CHECK(entMgr.getAccountPrefix(LGA_ENT_WITH_PREFIX).empty());

    //企业不存在
    CHECK(entMgr.setAccountPrefix(LGA_ENT_NO_PREFIX + 100, "HW") != error_code_ok);
    CHECK(entMgr.getAccountPrefix(0).empty());
    testdb::cleanupDb(LGA_DB_PATH);
}

TEST_CASE("LGA11 findLoginUser：账号不区分大小写，查不到且像手机号时按手机号，11 位数字账号优先按账号")
{
    testdb::resetDb(LGA_DB_PATH, LGA_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();
    const int32_t prefixed = lgaAddUser("HW.12345678", "13800011001");
    //老账号恰好是 11 位数字，另有一人的手机号与它相同
    const int32_t digitAccount = lgaAddUser("13900022002", "13800011002");
    const int32_t phoneOwner = lgaAddUser("EMP87654321", "13900022002");

    User u;
    bool byPhone = true;
    //按账号：大小写不同也能找到
    REQUIRE(userMgr.findLoginUser("hw.12345678", u, byPhone));
    CHECK(u.userid == prefixed);
    CHECK_FALSE(byPhone);
    REQUIRE(userMgr.findLoginUser("emp87654321", u, byPhone));
    CHECK(u.userid == phoneOwner);

    //按手机号：账号查不到、输入像手机号
    REQUIRE(userMgr.findLoginUser("13800011001", u, byPhone));
    CHECK(u.userid == prefixed);
    CHECK(byPhone);

    //输入既是某人的账号、又是另一人的手机号：按账号优先
    REQUIRE(userMgr.findLoginUser("13900022002", u, byPhone));
    CHECK(u.userid == digitAccount);
    CHECK_FALSE(byPhone);

    //都找不到：不像手机号的输入不按手机号找；像手机号但没人用
    CHECK_FALSE(userMgr.findLoginUser("NOSUCH", u, byPhone));
    CHECK_FALSE(userMgr.findLoginUser("13800019999", u, byPhone));
    CHECK_FALSE(userMgr.findLoginUser("", u, byPhone));
    testdb::cleanupDb(LGA_DB_PATH);
}
