/**
 *  默认超级管理员引导注入单元测试（Suite ADB），test_adminbootstrap.cpp
 *
 *  覆盖 AdminManager::ensureDefaultSuperAdmin 的七条行为：
 *    - 空库首次调用会建出一个平台超管，落库字段与口令哈希格式都正确；
 *    - 建出来的账号能用配置里的明文口令真的登录进去；
 *    - 重复调用不会重复注入（幂等），服务反复重启也只有一个默认账号；
 *    - 库里已经有别的平台超管时跳过，不再多塞一个；
 *    - 默认邮箱被某个企业管理员占用、且库里没有超管时，放弃注入而不是让
 *      唯一索引 uniq_email 把 INSERT 撞失败；
 *    - 新账号的业务ID 接在既有账号之后分配，不与已有 f_admin_id 冲突；
 *    - 配置开关关掉时完全不动数据库。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— AdminManager 经 DbFactory 取连接，
 *  而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套专用的库文件
 *  上。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *
 *  balloonwj@qq.com   2026-08-10
 */
#include <stdio.h>
#include <memory>
#include <string>

#include "doctest.h"
#include "../chatserversrc/AdminManager.h"
#include "../chatserversrc/Msg.h"           //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"
#include "../utils/MD5.h"

//本套用例专用的临时数据库文件，与 CRM / SQD / SQM / MPM 几套用的文件分开，避免互相干扰
static const char* const ADMIN_BOOT_DB_PATH = "flamingo_sqlite_adminboot_test.db";

//本套用例统一使用的默认超管配置，对应 etc/chatserver.conf 里的三个配置项
static const char* const BOOT_ADMIN_EMAIL    = "admin@flamingo.com";
static const char* const BOOT_ADMIN_PASSWORD = "admin123";
static const char* const BOOT_ADMIN_NAME     = "超级管理员";

//盐值长度（十六进制字符数）。实现取 MD5 摘要的前 16 位，与既有 createAdmin 的做法一致，
//这里断言它没被改短，因为盐一旦变短，暴力破解的代价会显著下降
static const size_t BOOT_SALT_LEN = 16;

//平台超级管理员的企业ID 口径：0 表示不隶属任何企业、管辖整个平台
static const int64_t ENTERPRISE_ID_PLATFORM = 0;

//用例里手工插入的"企业管理员"所属的企业ID，随便取一个非 0 值即可
static const int64_t ENTERPRISE_ID_TEST = 7;

//登录用例里传给 login 的来源IP，只写进会话与审计表，不参与任何断言
static const char* const BOOT_LOGIN_IP = "127.0.0.1";

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeAdminBootDbFiles()
{
    std::string basePath(ADMIN_BOOT_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始 —— AdminManager 是单例，
//用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetAdminBootDb()
{
    removeAdminBootDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 补列 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(ADMIN_BOOT_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, ADMIN_BOOT_DB_PATH);
}

//造一份启用状态的默认超管配置，用例可在返回值上按需改字段
static DefaultAdminConfig makeBootConfig()
{
    DefaultAdminConfig cfg;
    cfg.enable   = true;
    cfg.email    = BOOT_ADMIN_EMAIL;
    cfg.password = BOOT_ADMIN_PASSWORD;
    cfg.name     = BOOT_ADMIN_NAME;
    return cfg;
}

//计算字符串的 MD5 十六进制摘要，用于在用例里独立复算一遍口令哈希
static std::string md5Hex(const std::string& src)
{
    MD5 md5(src);
    return md5.toString();
}

//执行一条不取结果的 SQL；用于在用例里预置既有账号。失败返回 false。
static bool execSql(const char* sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return false;

    return pConn->execute(sql);
}

//往 t_admin 里手工插入一个账号，模拟"库里本来就有的管理员"。
//口令哈希按与实现相同的口径算：MD5(明文 + 盐)。
static bool insertAdminRow(int64_t adminId, const char* email, const char* plainPassword,
                           const char* salt, const char* name, int64_t enterpriseId)
{
    std::string pwdHash = md5Hex(std::string(plainPassword) + salt);

    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_admin(f_admin_id, f_email, f_password, f_salt, f_name, f_enterprise_id, f_status, f_create_time) "
        "VALUES(%lld, '%s', '%s', '%s', '%s', %lld, 0, NOW())",
        (long long)adminId, email, pwdHash.c_str(), salt, name, (long long)enterpriseId);
    return execSql(sql);
}

//数一数 t_admin 里现在有几行。数据库不可用时返回 -1，调用方须与 0 区分开。
static int64_t countAdminRows(const char* whereClause)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;

    char sql[512] = { 0 };
    if (whereClause != NULL && whereClause[0] != '\0')
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_admin WHERE %s", whereClause);
    else
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_admin");

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return -1;

    int64_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

//一行 t_admin 记录里本套用例关心的字段
struct AdminRow
{
    AdminRow() : adminId(0), enterpriseId(0), status(-1), found(false) {}

    int64_t     adminId;       //管理员业务ID
    std::string pwdHash;       //落库的口令哈希
    std::string salt;          //口令盐值
    std::string name;          //管理员姓名
    int64_t     enterpriseId;  //管辖企业ID，0 表示平台超管
    int32_t     status;        //状态：0=正常 1=停用
    bool        found;         //该邮箱的账号是否存在
};

//按邮箱读回一行 t_admin 记录，供用例逐字段断言
static AdminRow queryAdminRow(const char* email)
{
    AdminRow row;

    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return row;

    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_admin_id, f_password, f_salt, f_name, f_enterprise_id, f_status "
        "FROM t_admin WHERE f_email = '%s'", email);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return row;

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        row.adminId      = static_cast<int64_t>(pRow[0].getUInt64());
        row.pwdHash      = pRow[1].getString();
        row.salt         = pRow[2].getString();
        row.name         = pRow[3].getString();
        row.enterpriseId = static_cast<int64_t>(pRow[4].getUInt64());
        row.status       = pRow[5].getInt32();
        row.found        = true;
    }
    pResult->endQuery();
    delete pResult;
    return row;
}

// ---------------------------------------------------------------------------
// ADB1  空库首次注入
// ---------------------------------------------------------------------------

TEST_CASE("ADB1 空库首次调用会建出平台超管，且口令按 MD5(明文 + 盐) 落库")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    REQUIRE(countAdminRows(NULL) == 0);

    bool created = false;
    CHECK(mgr.ensureDefaultSuperAdmin(makeBootConfig(), created) == error_code_ok);
    CHECK(created);

    //账号建出来了，而且只建了一个
    REQUIRE(countAdminRows(NULL) == 1);

    AdminRow row = queryAdminRow(BOOT_ADMIN_EMAIL);
    REQUIRE(row.found);
    CHECK(row.name == BOOT_ADMIN_NAME);
    CHECK(row.enterpriseId == ENTERPRISE_ID_PLATFORM);  //必须是平台超管而不是某企业的管理员
    CHECK(row.status == 0);                             //建出来就是启用状态，否则登不进去

    //盐必须真的生成了，且没被改短
    CHECK(row.salt.size() == BOOT_SALT_LEN);

    //口令哈希的口径要与 AdminManager::login 的校验口径一致，否则账号建了也登不进去
    CHECK(row.pwdHash == md5Hex(std::string(BOOT_ADMIN_PASSWORD) + row.salt));

    //明文口令绝不能原样落库
    CHECK(row.pwdHash != BOOT_ADMIN_PASSWORD);
}

// ---------------------------------------------------------------------------
// ADB2  注入的账号真的能登录
// ---------------------------------------------------------------------------

TEST_CASE("ADB2 注入出来的账号能用配置里的明文口令登录，且身份是平台超管")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    bool created = false;
    REQUIRE(mgr.ensureDefaultSuperAdmin(makeBootConfig(), created) == error_code_ok);
    REQUIRE(created);

    //这条是整个功能的目的所在：建完之后必须能直接登进 admin 控制台
    std::string token;
    AdminInfo admin;
    CHECK(mgr.login(BOOT_ADMIN_EMAIL, BOOT_ADMIN_PASSWORD, BOOT_LOGIN_IP, token, admin) == error_code_ok);
    CHECK_FALSE(token.empty());
    CHECK(admin.email == BOOT_ADMIN_EMAIL);
    CHECK(admin.enterpriseid == ENTERPRISE_ID_PLATFORM);

    //错误口令仍然要被挡住，不能因为是引导账号就放宽校验
    std::string token2;
    AdminInfo admin2;
    CHECK(mgr.login(BOOT_ADMIN_EMAIL, "wrongpassword", BOOT_LOGIN_IP, token2, admin2) != error_code_ok);
}

// ---------------------------------------------------------------------------
// ADB3  幂等
// ---------------------------------------------------------------------------

TEST_CASE("ADB3 重复调用不重复注入：服务反复重启也只有一个默认超管")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    bool firstCreated = false;
    REQUIRE(mgr.ensureDefaultSuperAdmin(makeBootConfig(), firstCreated) == error_code_ok);
    REQUIRE(firstCreated);
    AdminRow first = queryAdminRow(BOOT_ADMIN_EMAIL);
    REQUIRE(first.found);

    //第二次调用相当于服务重启：流程正常，但这次什么都不该建
    bool secondCreated = true;
    CHECK(mgr.ensureDefaultSuperAdmin(makeBootConfig(), secondCreated) == error_code_ok);
    CHECK_FALSE(secondCreated);
    CHECK(countAdminRows(NULL) == 1);

    //业务ID 与口令哈希都不能被改写 —— 管理员登录后改过密码，重启不该被打回默认口令
    AdminRow second = queryAdminRow(BOOT_ADMIN_EMAIL);
    REQUIRE(second.found);
    CHECK(second.adminId == first.adminId);
    CHECK(second.pwdHash == first.pwdHash);
    CHECK(second.salt == first.salt);
}

// ---------------------------------------------------------------------------
// ADB4  已有别的平台超管
// ---------------------------------------------------------------------------

TEST_CASE("ADB4 库里已有别的平台超管时跳过，不再多塞一个默认账号")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    //模拟已导入 testdata.sql 的库：里面已经有一个平台超管
    REQUIRE(insertAdminRow(1, "li@flamingo.com", "admin123", "slt10001abc",
                           "李正阳", ENTERPRISE_ID_PLATFORM));

    bool created = true;
    CHECK(mgr.ensureDefaultSuperAdmin(makeBootConfig(), created) == error_code_ok);
    CHECK_FALSE(created);

    //既没多出账号，默认邮箱那个也压根没建
    CHECK(countAdminRows(NULL) == 1);
    CHECK_FALSE(queryAdminRow(BOOT_ADMIN_EMAIL).found);
}

// ---------------------------------------------------------------------------
// ADB5  默认邮箱被企业管理员占用
// ---------------------------------------------------------------------------

TEST_CASE("ADB5 默认邮箱被企业管理员占用且库里无超管时，放弃注入而不是撞唯一索引")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    //库里没有任何平台超管，但默认邮箱已经被一个企业管理员占了。
    //按"有没有超管"的判定口径这里该注入，可 uniq_email 不允许重复邮箱。
    REQUIRE(insertAdminRow(1, BOOT_ADMIN_EMAIL, "someotherpwd", "slt00001xyz",
                           "某企业管理员", ENTERPRISE_ID_TEST));

    bool created = true;
    //返回"邮箱已被占用"，调用方据此打日志，但不能让服务起不来
    CHECK(mgr.ensureDefaultSuperAdmin(makeBootConfig(), created) == error_code_registeralready);
    CHECK_FALSE(created);

    //原账号必须原封不动：企业归属、姓名、口令都不能被引导逻辑改写
    CHECK(countAdminRows(NULL) == 1);
    AdminRow row = queryAdminRow(BOOT_ADMIN_EMAIL);
    REQUIRE(row.found);
    CHECK(row.enterpriseId == ENTERPRISE_ID_TEST);
    CHECK(row.name == "某企业管理员");
    CHECK(row.pwdHash == md5Hex(std::string("someotherpwd") + "slt00001xyz"));
}

// ---------------------------------------------------------------------------
// ADB6  业务ID 分配
// ---------------------------------------------------------------------------

TEST_CASE("ADB6 新超管的业务ID 接在既有账号之后分配，不与已有 f_admin_id 冲突")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    //库里已有三个企业管理员（都不是超管，所以引导逻辑仍会注入），占掉 ID 1~3
    REQUIRE(insertAdminRow(1, "a@corp.com", "pwd1", "slt00001aaa", "甲", ENTERPRISE_ID_TEST));
    REQUIRE(insertAdminRow(2, "b@corp.com", "pwd2", "slt00002bbb", "乙", ENTERPRISE_ID_TEST));
    REQUIRE(insertAdminRow(3, "c@corp.com", "pwd3", "slt00003ccc", "丙", ENTERPRISE_ID_TEST));

    bool created = false;
    CHECK(mgr.ensureDefaultSuperAdmin(makeBootConfig(), created) == error_code_ok);
    CHECK(created);

    CHECK(countAdminRows(NULL) == 4);

    AdminRow row = queryAdminRow(BOOT_ADMIN_EMAIL);
    REQUIRE(row.found);
    CHECK(row.adminId == 4);
    CHECK(row.enterpriseId == ENTERPRISE_ID_PLATFORM);
}

// ---------------------------------------------------------------------------
// ADB7  配置开关
// ---------------------------------------------------------------------------

TEST_CASE("ADB7 配置开关关掉时完全不注入")
{
    resetAdminBootDb();
    AdminManager& mgr = Singleton<AdminManager>::Instance();

    DefaultAdminConfig cfg = makeBootConfig();
    cfg.enable = false;

    bool created = true;
    CHECK(mgr.ensureDefaultSuperAdmin(cfg, created) == error_code_ok);
    CHECK_FALSE(created);

    //关掉开关就该一行都不建，生产库不会平白多出一个带默认口令的超管
    CHECK(countAdminRows(NULL) == 0);
}
