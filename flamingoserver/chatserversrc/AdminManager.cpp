/**
 *  控制台管理员管理器实现，AdminManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "AdminManager.h"
#include <memory>
#include <stdio.h>
#include <time.h>
#include <atomic>
#include "Msg.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../utils/MD5.h"

//拼装 SQL 的字符缓冲区大小。审计日志含若干文本字段，取 2048 留足余量。
#define ADMIN_SQL_BUF_LEN   2048

//「近 N 天消息量」趋势图的天数窗口，含今天在内共 14 个自然日。
//日期轴（BuildLast14Days）与 SQL 里的时间下界必须用同一个值，否则柱子数量与数据对不上；
//SQL 侧取的是「今天往前 ADMIN_TREND_DAYS - 1 天」的零点，正好覆盖这 14 个自然日
#define ADMIN_TREND_DAYS    14

//密码盐的长度（十六进制字符数）。取 MD5 摘要的前 16 位，与 createAdmin 生成盐的口径一致。
//盐一旦取短，同口令的哈希碰撞面与彩虹表的可用性都会上升，故单列成常量避免被随手改小。
#define DEFAULT_ADMIN_SALT_LEN   16

//token 序列号，与时间戳一起参与生成，保证同一秒内多次登录的 token 也不重复。
static std::atomic<uint32_t> g_tokenSeq(0);


//计算字符串的 MD5 十六进制摘要。
static std::string Md5Hex(const std::string& src)
{
    MD5 md5(src);
    return md5.toString();
}

//生成会话 token：MD5(邮箱 | 当前时间 | 自增序列)，得到 32 位十六进制串。
static std::string GenToken(const std::string& email)
{
    uint32_t seq = ++g_tokenSeq;
    std::string raw = email + "|" + std::to_string((long long)time(NULL)) + "|" + std::to_string(seq);
    return Md5Hex(raw);
}

AdminManager::AdminManager()
{

}

AdminManager::~AdminManager()
{

}

bool AdminManager::init()
{
    LOGI("AdminManager::init ok, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
    return true;
}

int32_t AdminManager::ensureDefaultSuperAdmin(const DefaultAdminConfig& cfg, bool& outCreated)
{
    outCreated = false;

    //开关关闭时连数据库都不碰：生产库不该因为服务起了一次就平白多出一个带默认口令的超管
    if (!cfg.enable)
    {
        LOGI("AdminManager::ensureDefaultSuperAdmin skipped, disabled by config.");
        return error_code_ok;
    }

    //邮箱与口令缺任何一个都建不出能登录的账号，与其建一个残缺账号不如直接报错让人去改配置
    if (cfg.email.empty() || cfg.password.empty())
    {
        LOGE("AdminManager::ensureDefaultSuperAdmin failed, email or password is empty in config.");
        return error_code_unknown;
    }

    //与登录、改密等写操作串行化，避免启动期与其它路径同时改 t_admin
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //判定口径：库里只要已经有任意一个平台超管（f_enterprise_id = 0），就不需要引导账号了。
    //之所以不按默认邮箱判，是为了让导入过 testdata.sql 的库不被多塞一个账号，
    //也让管理员改过口令之后重启服务不会被打回默认口令。
    if (queryCount(pConn.get(), "SELECT COUNT(*) FROM t_admin WHERE f_enterprise_id = 0") > 0)
    {
        LOGI("AdminManager::ensureDefaultSuperAdmin skipped, a platform super admin already exists.");
        return error_code_ok;
    }

    //库里没有超管，但默认邮箱仍可能已被某个企业管理员占用。t_admin 的 uniq_email 不允许
    //邮箱重复，这时硬插会失败，故先查出来记日志跳过，免得启动期抛一条看不懂的 SQL 错误。
    std::string emailEsc = dbapi::EscapeString(cfg.email);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_admin WHERE f_email = '%s'", emailEsc.c_str());
    if (queryCount(pConn.get(), sql) > 0)
    {
        LOGE("AdminManager::ensureDefaultSuperAdmin failed, email already taken by another admin: %s",
             cfg.email.c_str());
        return error_code_registeralready;
    }

    //分配管理员业务ID = 当前最大值 + 1，口径与 createAdmin 保持一致
    int64_t newAdminId = queryCount(pConn.get(), "SELECT IFNULL(MAX(f_admin_id), 0) FROM t_admin") + 1;

    //每个管理员一份独立的盐，做法同 createAdmin：拿邮箱 + 当前时间 + 自增序列做种子算 MD5
    uint32_t seq = ++g_tokenSeq;
    std::string base = cfg.email + std::to_string((long long)time(NULL)) + std::to_string(seq);
    std::string salt = Md5Hex("salt" + base).substr(0, DEFAULT_ADMIN_SALT_LEN);

    //口令哈希的口径必须与 login 的校验口径一致（MD5(明文 + 盐)），否则账号建出来也登不进去
    std::string pwdHash = Md5Hex(cfg.password + salt);

    //企业ID 固定写 0（平台超管），状态固定写 0（启用）—— 建出来就是为了立刻能登录
    std::string nameEsc = dbapi::EscapeString(cfg.name);
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_admin(f_admin_id, f_email, f_password, f_salt, f_name, f_enterprise_id, f_status, f_create_time) "
        "VALUES(%lld, '%s', '%s', '%s', '%s', 0, 0, NOW())",
        (long long)newAdminId, emailEsc.c_str(), pwdHash.c_str(), salt.c_str(), nameEsc.c_str());
    if (!pConn->execute(sql))
    {
        LOGE("AdminManager::ensureDefaultSuperAdmin failed, insert error, email: %s", cfg.email.c_str());
        return error_code_unknown;
    }

    outCreated = true;
    //这条日志是部署后找默认账号的唯一线索，必须带上邮箱；口令就写在配置文件里，日志不重复打印
    LOGI("AdminManager::ensureDefaultSuperAdmin ok, created default super admin, adminid: %lld, email: %s",
         (long long)newAdminId, cfg.email.c_str());
    return error_code_ok;
}

IDatabase* AdminManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("AdminManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

int32_t AdminManager::queryCount(IDatabase* pConn, const char* sql)
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

bool AdminManager::queryAdminByEmail(IDatabase* pConn, const std::string& email,
                                     AdminInfo& outAdmin, std::string& outPwdHash, std::string& outSalt)
{
    std::string emailEsc = dbapi::EscapeString(email);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_admin_id, f_email, f_password, f_salt, f_name, f_enterprise_id, f_status "
        "FROM t_admin WHERE f_email = '%s'",
        emailEsc.c_str());

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outAdmin.adminid      = pRow[0].getInt32();
        outAdmin.email        = pRow[1].getString();
        outPwdHash            = pRow[2].getString();
        outSalt               = pRow[3].getString();
        outAdmin.name         = pRow[4].getString();
        outAdmin.enterpriseid = pRow[5].getInt32();
        outAdmin.status       = pRow[6].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

int32_t AdminManager::login(const std::string& email, const std::string& password, const std::string& loginIp,
                            std::string& outToken, AdminInfo& outAdmin)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    std::string pwdHash;
    std::string salt;
    AdminInfo admin;
    if (!queryAdminByEmail(pConn.get(), email, admin, pwdHash, salt))
    {
        //邮箱不存在与密码错误统一返回 103，避免暴露账号是否存在。
        LOGE("AdminManager::login failed, email not found: %s", email.c_str());
        return error_code_invalidpassword;
    }

    //校验密码：库中存的是 MD5(明文密码 + 盐)。
    if (Md5Hex(password + salt) != pwdHash)
    {
        LOGE("AdminManager::login failed, wrong password, email: %s", email.c_str());
        writeAudit(admin.adminid, admin.name, loginIp, admin.enterpriseid,
                   "login.failed", "-", 2, u8"登录密码错误");
        return error_code_invalidpassword;
    }

    //停用的账号不允许登录。
    if (admin.status != 0)
    {
        LOGE("AdminManager::login failed, account disabled, email: %s", email.c_str());
        return error_code_notlogin;
    }

    //生成 token 并写入会话表，有效期 ADMIN_TOKEN_VALID_HOURS 小时。
    std::string token = GenToken(email);
    std::string ipEsc = dbapi::EscapeString(loginIp);
    //过期时间的表达式按当前数据库后端生成：MySQL 的 INTERVAL 是语法结构，SQLite 认不了
    std::string expireExpr = dbapi::NowPlusHours(ADMIN_TOKEN_VALID_HOURS);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_admin_session(f_token, f_admin_id, f_login_ip, f_login_time, f_expire_time, f_create_time) "
        "VALUES('%s', %lld, '%s', NOW(), %s, NOW())",
        token.c_str(), (long long)admin.adminid, ipEsc.c_str(), expireExpr.c_str());
    if (!pConn->execute(sql))
    {
        LOGE("AdminManager::login failed, insert session error: %s", sql);
        return error_code_unknown;
    }

    //更新末次登录时间与IP。
    snprintf(sql, sizeof(sql),
        "UPDATE t_admin SET f_last_login_time = NOW(), f_last_login_ip = '%s' WHERE f_admin_id = %lld",
        ipEsc.c_str(), (long long)admin.adminid);
    pConn->execute(sql);

    writeAudit(admin.adminid, admin.name, loginIp, admin.enterpriseid,
               "login.success", "-", 0, u8"管理员登录成功");

    outToken = token;
    outAdmin = admin;
    LOGI("AdminManager::login ok, email: %s, adminid: %lld", email.c_str(), (long long)admin.adminid);
    return error_code_ok;
}

void AdminManager::logout(const std::string& token)
{
    if (token.empty())
        return;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string tokenEsc = dbapi::EscapeString(token);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql), "DELETE FROM t_admin_session WHERE f_token = '%s'", tokenEsc.c_str());
    pConn->execute(sql);
    LOGI("AdminManager::logout, session cleared");
}

bool AdminManager::checkToken(const std::string& token, AdminInfo& outAdmin)
{
    if (token.empty())
        return false;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //会话需存在且未过期；连 t_admin 取管理员信息。
    std::string tokenEsc = dbapi::EscapeString(token);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT a.f_admin_id, a.f_email, a.f_name, a.f_enterprise_id, a.f_status "
        "FROM t_admin_session s JOIN t_admin a ON s.f_admin_id = a.f_admin_id "
        "WHERE s.f_token = '%s' AND s.f_expire_time > NOW()",
        tokenEsc.c_str());

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool ok = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outAdmin.adminid      = pRow[0].getInt32();
        outAdmin.email        = pRow[1].getString();
        outAdmin.name         = pRow[2].getString();
        outAdmin.enterpriseid = pRow[3].getInt32();
        outAdmin.status       = pRow[4].getInt32();
        //停用账号即便持有未过期 token 也视为无效。
        ok = (outAdmin.status == 0);
    }
    pResult->endQuery();
    delete pResult;
    return ok;
}

int32_t AdminManager::changePassword(int64_t adminId, const std::string& oldPassword, const std::string& newPassword)
{
    if (newPassword.empty())
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //取当前密码哈希与盐。
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_password, f_salt FROM t_admin WHERE f_admin_id = %lld", (long long)adminId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return error_code_unknown;

    std::string pwdHash;
    std::string salt;
    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        pwdHash = pRow[0].getString();
        salt    = pRow[1].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return error_code_unknown;

    //校验旧密码。
    if (Md5Hex(oldPassword + salt) != pwdHash)
    {
        LOGE("AdminManager::changePassword failed, wrong old password, adminid: %lld", (long long)adminId);
        return error_code_invalidpassword;
    }

    //写新密码（沿用原盐值）。
    std::string newHash = Md5Hex(newPassword + salt);
    snprintf(sql, sizeof(sql),
        "UPDATE t_admin SET f_password = '%s' WHERE f_admin_id = %lld",
        newHash.c_str(), (long long)adminId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    LOGI("AdminManager::changePassword ok, adminid: %lld", (long long)adminId);
    return error_code_ok;
}

int32_t AdminManager::updateProfile(int64_t adminId, const std::string& name)
{
    if (name.empty())
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    std::string nameEsc = dbapi::EscapeString(name);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_admin SET f_name = '%s' WHERE f_admin_id = %lld",
        nameEsc.c_str(), (long long)adminId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    LOGI("AdminManager::updateProfile ok, adminid: %lld", (long long)adminId);
    return error_code_ok;
}

void AdminManager::getDashboardStats(int64_t enterpriseId, DashboardStats& outStats)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    outStats.enterprisesTotal   = queryCount(pConn.get(), "SELECT COUNT(*) FROM t_enterprise");
    outStats.enterprisesActive  = queryCount(pConn.get(), "SELECT COUNT(*) FROM t_enterprise WHERE f_status = 1");
    outStats.enterprisesPending = queryCount(pConn.get(), "SELECT COUNT(*) FROM t_enterprise WHERE f_status = 0");

    //平台超管统计全部企业员工；企业级管理员只统计本企业。
    if (enterpriseId == 0)
    {
        outStats.usersTotal = queryCount(pConn.get(),
            "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id <> 0");
    }
    else
    {
        char sql[ADMIN_SQL_BUF_LEN] = { 0 };
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id = %lld", (long long)enterpriseId);
        outStats.usersTotal = queryCount(pConn.get(), sql);
    }

    outStats.auditToday = queryCount(pConn.get(),
        "SELECT COUNT(*) FROM t_admin_audit_log WHERE f_create_time >= CURDATE()");
    outStats.auditTodayWarn = queryCount(pConn.get(),
        "SELECT COUNT(*) FROM t_admin_audit_log WHERE f_create_time >= CURDATE() AND f_severity >= 1");
}

//算出近 14 天的日期序列（最旧→今天）：outYmd 为 "YYYY-MM-DD"（用于匹配 SQL
//里 DATE() 的结果），outLabel 为 "MM-DD"（用于前端展示）。两者一一对应、各 14 个。
static void BuildLast14Days(std::vector<std::string>& outYmd,
                            std::vector<std::string>& outLabel)
{
    outYmd.clear();
    outLabel.clear();

    //近 14 天 = 含今天在内的 14 个自然日，见 ADMIN_TREND_DAYS。
    time_t now = time(NULL);
    for (int i = ADMIN_TREND_DAYS - 1; i >= 0; --i)
    {
        time_t t = now - (time_t)i * 24 * 3600;
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char ymd[16] = { 0 };
        char label[8] = { 0 };
        snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
        snprintf(label, sizeof(label), "%02d-%02d",
                 tmv.tm_mon + 1, tmv.tm_mday);
        outYmd.push_back(ymd);
        outLabel.push_back(label);
    }
}

//在给定 SQL（须 SELECT 出「日期, 条数」两列、按日期 GROUP BY）的结果上，
//按近 14 天日期轴补齐成连续 14 个点（缺消息的天 count 为 0）。
static void FillTrendFromSql(IDatabase* pConn, const char* sql,
                             std::vector<MessageTrendPoint>& outTrend)
{
    outTrend.clear();

    std::vector<std::string> ymd;
    std::vector<std::string> label;
    BuildLast14Days(ymd, label);

    //SQL 结果收进「日期 / 条数」并行数组（最多 14 行，后续线性匹配即可）。
    std::vector<std::string> qDate;
    std::vector<int32_t>      qCount;
    QueryResult* pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        while (true)
        {
            Field* pRow = pResult->fetch();
            if (pRow == NULL)
                break;

            qDate.push_back(pRow[0].getString());
            qCount.push_back(pRow[1].getInt32());

            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    //按 14 天日期轴补齐。
    for (size_t i = 0; i < ymd.size(); ++i)
    {
        MessageTrendPoint pt;
        pt.date  = label[i];
        pt.count = 0;
        for (size_t j = 0; j < qDate.size(); ++j)
        {
            if (qDate[j] == ymd[i])
            {
                pt.count = qCount[j];
                break;
            }
        }
        outTrend.push_back(pt);
    }
}

void AdminManager::getMessageTrend(int64_t enterpriseId,
                                   std::vector<MessageTrendPoint>& outTrend)
{
    outTrend.clear();

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //f_create_time 为 Unix 毫秒，先 /1000 还原成秒再 FROM_UNIXTIME。
    //时间下界的表达式按当前数据库后端生成，两种方言的 INTERVAL 写法不同
    std::string sinceExpr = dbapi::TodayMinusDays(ADMIN_TREND_DAYS - 1);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    if (enterpriseId == 0)
    {
        //平台口径：统计全平台消息。
        snprintf(sql, sizeof(sql),
            "SELECT DATE(FROM_UNIXTIME(f_create_time/1000)) d, COUNT(*) c "
            "FROM t_chatmsg "
            "WHERE FROM_UNIXTIME(f_create_time/1000) >= %s "
            "GROUP BY d", sinceExpr.c_str());
    }
    else
    {
        //企业口径：只统计该企业员工发出的消息（按发送方 join t_user）。
        snprintf(sql, sizeof(sql),
            "SELECT DATE(FROM_UNIXTIME(m.f_create_time/1000)) d, COUNT(*) c "
            "FROM t_chatmsg m JOIN t_user u ON u.f_user_id = m.f_from "
            "WHERE u.f_enterprise_id = %lld "
            "AND FROM_UNIXTIME(m.f_create_time/1000) >= %s "
            "GROUP BY d", (long long)enterpriseId, sinceExpr.c_str());
    }
    FillTrendFromSql(pConn.get(), sql, outTrend);
}

void AdminManager::getUserMessageStats(int64_t userId, UserMessageStats& outStats)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    char sql[ADMIN_SQL_BUF_LEN] = { 0 };

    //发出的消息总数。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_chatmsg WHERE f_from = %lld", (long long)userId);
    outStats.sent = queryCount(pConn.get(), sql);

    //收到的单聊消息总数（群聊的 f_to 是群id，故限定 f_ctype=1）。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_chatmsg WHERE f_to = %lld AND f_ctype = 1",
        (long long)userId);
    outStats.received = queryCount(pConn.get(), sql);

    //单聊会话数：与该员工有过单聊往来的去重对端数（UNION 自带去重）。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM ("
        "SELECT f_to peer FROM t_chatmsg WHERE f_from = %lld AND f_ctype = 1 "
        "UNION "
        "SELECT f_from peer FROM t_chatmsg WHERE f_to = %lld AND f_ctype = 1) t",
        (long long)userId, (long long)userId);
    outStats.conversations = queryCount(pConn.get(), sql);

    //近 14 天该员工每天发出的消息数。
    std::string sinceExpr = dbapi::TodayMinusDays(ADMIN_TREND_DAYS - 1);
    snprintf(sql, sizeof(sql),
        "SELECT DATE(FROM_UNIXTIME(f_create_time/1000)) d, COUNT(*) c "
        "FROM t_chatmsg "
        "WHERE f_from = %lld "
        "AND FROM_UNIXTIME(f_create_time/1000) >= %s "
        "GROUP BY d", (long long)userId, sinceExpr.c_str());
    FillTrendFromSql(pConn.get(), sql, outStats.trend);
}

void AdminManager::writeAudit(int64_t actorId, const std::string& actorName, const std::string& ip,
                              int64_t enterpriseId, const std::string& action, const std::string& target,
                              int32_t severity, const std::string& detail)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string nameEsc   = dbapi::EscapeString(actorName);
    std::string ipEsc     = dbapi::EscapeString(ip);
    std::string actionEsc = dbapi::EscapeString(action);
    std::string targetEsc = dbapi::EscapeString(target);
    std::string detailEsc = dbapi::EscapeString(detail);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_admin_audit_log(f_actor_type, f_actor_id, f_actor_name, f_ip, f_enterprise_id, "
        "f_action, f_target, f_severity, f_detail, f_create_time) "
        "VALUES(0, %lld, '%s', '%s', %lld, '%s', '%s', %d, '%s', NOW())",
        (long long)actorId, nameEsc.c_str(), ipEsc.c_str(), (long long)enterpriseId,
        actionEsc.c_str(), targetEsc.c_str(), severity, detailEsc.c_str());
    pConn->execute(sql);
}

int32_t AdminManager::createAdmin(int64_t enterpriseId, const std::string& name, const std::string& email,
                                  int64_t& outAdminId, std::string& outInitialPassword)
{
    if (name.empty() || email.empty())
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //登录邮箱不可重复。
    std::string emailEsc = dbapi::EscapeString(email);
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM t_admin WHERE f_email = '%s'", emailEsc.c_str());
    if (queryCount(pConn.get(), sql) > 0)
    {
        LOGE("AdminManager::createAdmin failed, email already exists: %s", email.c_str());
        return error_code_registeralready;
    }

    //分配管理员业务ID = 当前最大值 + 1。
    int64_t newAdminId = queryCount(pConn.get(), "SELECT IFNULL(MAX(f_admin_id), 0) FROM t_admin") + 1;

    //生成每管理员独立的盐与随机初始密码。
    uint32_t seq = ++g_tokenSeq;
    std::string base = email + std::to_string((long long)time(NULL)) + std::to_string(seq);
    std::string salt = Md5Hex("salt" + base).substr(0, 16);
    std::string initialPwd = Md5Hex("pwd" + base).substr(0, 10);
    std::string pwdHash = Md5Hex(initialPwd + salt);

    std::string nameEsc = dbapi::EscapeString(name);
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_admin(f_admin_id, f_email, f_password, f_salt, f_name, f_enterprise_id, f_status, f_create_time) "
        "VALUES(%lld, '%s', '%s', '%s', '%s', %lld, 0, NOW())",
        (long long)newAdminId, emailEsc.c_str(), pwdHash.c_str(), salt.c_str(),
        nameEsc.c_str(), (long long)enterpriseId);
    if (!pConn->execute(sql))
    {
        LOGE("AdminManager::createAdmin failed, insert error: %s", sql);
        return error_code_unknown;
    }

    outAdminId = newAdminId;
    outInitialPassword = initialPwd;
    LOGI("AdminManager::createAdmin ok, adminid: %lld, email: %s, enterpriseid: %lld",
         (long long)newAdminId, email.c_str(), (long long)enterpriseId);
    return error_code_ok;
}

void AdminManager::listAdmins(int64_t enterpriseId, std::vector<AdminListItem>& outList)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_admin_id, f_name, f_email, f_status, f_last_login_time FROM t_admin "
        "WHERE f_enterprise_id = %lld ORDER BY f_admin_id",
        (long long)enterpriseId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        AdminListItem item;
        item.adminid       = pRow[0].getInt32();
        item.name          = pRow[1].getString();
        item.email         = pRow[2].getString();
        item.status        = pRow[3].getInt32();
        item.lastLoginTime = pRow[4].getString();
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

int32_t AdminManager::removeAdmin(int64_t adminId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //先删该管理员的全部会话，再删账号本身。
    char sql[ADMIN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql), "DELETE FROM t_admin_session WHERE f_admin_id = %lld", (long long)adminId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql), "DELETE FROM t_admin WHERE f_admin_id = %lld", (long long)adminId);
    if (!pConn->execute(sql))
        return error_code_unknown;

    LOGI("AdminManager::removeAdmin ok, adminid: %lld", (long long)adminId);
    return error_code_ok;
}

//==== 审计日志查询与导出 ====

//把一个字段值转义为 CSV 单元格：含逗号/双引号/换行时用双引号包裹，并把内部双引号翻倍。
static std::string CsvField(const std::string& src)
{
    bool needQuote = false;
    for (size_t i = 0; i < src.size(); ++i)
    {
        char c = src[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r')
        {
            needQuote = true;
            break;
        }
    }
    if (!needQuote)
        return src;

    std::string out;
    out.reserve(src.size() + 8);
    out.push_back('"');
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '"')
            out.push_back('"');   //双引号翻倍
        out.push_back(src[i]);
    }
    out.push_back('"');
    return out;
}

std::string AdminManager::buildAuditWhere(const AuditQuery& query)
{
    //逐个过滤项拼成 WHERE 子句；无任何过滤项时返回空串。
    std::string where;

    if (query.actorType >= 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), "f_actor_type = %d", query.actorType);
        where += (where.empty() ? "" : " AND ");
        where += buf;
    }
    if (!query.action.empty())
    {
        //前缀匹配：传 "user" 可匹配 user.create / user.delete 等。
        std::string actionEsc = dbapi::EscapeString(query.action);
        where += (where.empty() ? "" : " AND ");
        where += "f_action LIKE '" + actionEsc + "%'";
    }
    if (query.enterpriseId != 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), "f_enterprise_id = %lld", (long long)query.enterpriseId);
        where += (where.empty() ? "" : " AND ");
        where += buf;
    }
    if (query.severity >= 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), "f_severity = %d", query.severity);
        where += (where.empty() ? "" : " AND ");
        where += buf;
    }
    if (!query.startTime.empty())
    {
        where += (where.empty() ? "" : " AND ");
        where += "f_create_time >= '" + dbapi::EscapeString(query.startTime) + "'";
    }
    if (!query.endTime.empty())
    {
        where += (where.empty() ? "" : " AND ");
        where += "f_create_time <= '" + dbapi::EscapeString(query.endTime) + "'";
    }

    if (where.empty())
        return std::string();
    return " WHERE " + where;
}

void AdminManager::queryAuditLogs(const AuditQuery& query, int32_t page, int32_t pageSize,
                                  int32_t& outTotal, std::vector<AuditLogItem>& outList)
{
    outTotal = 0;
    outList.clear();

    //分页参数规整。
    if (page < 1)
        page = 1;
    if (pageSize <= 0)
        pageSize = 50;
    if (pageSize > 500)
        pageSize = 500;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildAuditWhere(query);

    //1. 取满足条件的总条数。
    std::string countSql = "SELECT COUNT(*) FROM t_admin_audit_log" + where;
    outTotal = queryCount(pConn.get(), countSql.c_str());
    if (outTotal <= 0)
        return;

    //2. 取当前页，按操作时间倒序。
    int32_t offset = (page - 1) * pageSize;
    char tail[128] = { 0 };
    snprintf(tail, sizeof(tail), " ORDER BY f_create_time DESC, f_id DESC LIMIT %d, %d",
             offset, pageSize);
    std::string sql =
        "SELECT f_id, f_create_time, f_actor_type, f_actor_name, f_ip, f_enterprise_id, "
        "f_action, f_target, f_severity, f_detail FROM t_admin_audit_log" + where + tail;

    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        AuditLogItem item;
        item.id           = pRow[0].getInt32();
        item.ts           = pRow[1].getString();
        item.actorType    = pRow[2].getInt32();
        item.actorName    = pRow[3].getString();
        item.ip           = pRow[4].getString();
        item.enterpriseId = pRow[5].getInt32();
        item.action       = pRow[6].getString();
        item.target       = pRow[7].getString();
        item.severity     = pRow[8].getInt32();
        item.detail       = pRow[9].getString();
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

void AdminManager::exportAuditLogsCsv(const AuditQuery& query, std::string& outCsv)
{
    outCsv.clear();

    //CSV 表头（中文须用 u8"" 否则经 HTTP 输出会因 MSVC 按 GBK 编码而乱码）。
    outCsv = u8"日志ID,时间,操作者类型,操作者,IP,企业ID,操作类型,操作目标,级别,详情\r\n";

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    std::string where = buildAuditWhere(query);

    //导出最多 ADMIN_AUDIT_EXPORT_MAX 行，避免应答过大。
    char tail[96] = { 0 };
    snprintf(tail, sizeof(tail), " ORDER BY f_create_time DESC, f_id DESC LIMIT %d",
             ADMIN_AUDIT_EXPORT_MAX);
    std::string sql =
        "SELECT f_id, f_create_time, f_actor_type, f_actor_name, f_ip, f_enterprise_id, "
        "f_action, f_target, f_severity, f_detail FROM t_admin_audit_log" + where + tail;

    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        char numbuf[64] = { 0 };
        std::string line;

        snprintf(numbuf, sizeof(numbuf), "%d", pRow[0].getInt32());
        line += numbuf;
        line += ',';
        line += CsvField(pRow[1].getString());
        line += ',';
        snprintf(numbuf, sizeof(numbuf), "%d", pRow[2].getInt32());
        line += numbuf;
        line += ',';
        line += CsvField(pRow[3].getString());
        line += ',';
        line += CsvField(pRow[4].getString());
        line += ',';
        snprintf(numbuf, sizeof(numbuf), "%d", pRow[5].getInt32());
        line += numbuf;
        line += ',';
        line += CsvField(pRow[6].getString());
        line += ',';
        line += CsvField(pRow[7].getString());
        line += ',';
        snprintf(numbuf, sizeof(numbuf), "%d", pRow[8].getInt32());
        line += numbuf;
        line += ',';
        line += CsvField(pRow[9].getString());
        line += "\r\n";
        outCsv += line;

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

void AdminManager::getAuditStats(int32_t enterpriseId, int32_t& outToday, int32_t& outTotal,
                                 int32_t& outWarn, int32_t& outDanger)
{
    outToday  = 0;
    outTotal  = 0;
    outWarn   = 0;
    outDanger = 0;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //作用域 WHERE：平台超管（enterpriseId==0）统计全表，企业级管理员只统计本企业。
    //此处仅按企业过滤，不接受任何其它筛选条件。
    std::string where;
    if (enterpriseId != 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), " WHERE f_enterprise_id = %d", enterpriseId);
        where = buf;
    }

    //一条 SQL 同时算出 4 项计数，减少与数据库的往返：
    //  today  —— 今日事件数，「今日」判定与 dashboard/stats.do 的 audit_today 保持一致，
    //            均以 f_create_time >= CURDATE()（服务器本地日期 00:00 起）为准；
    //  total  —— 作用域内全部事件数；
    //  warn   —— f_severity=1（warn）的事件数；
    //  danger —— f_severity=2（danger）的事件数。
    //f_severity 取值含义：0=info 1=warn 2=danger。
    std::string sql =
        "SELECT "
        "SUM(f_create_time >= CURDATE()), "
        "COUNT(*), "
        "SUM(f_severity = 1), "
        "SUM(f_severity = 2) "
        "FROM t_admin_audit_log" + where;

    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
    {
        LOGE("AdminManager::getAuditStats query failed, sql: %s", sql.c_str());
        return;
    }

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        //表为空时 SUM(...) 返回 NULL，getInt32() 会得到 0，符合预期。
        outToday  = pRow[0].getInt32();
        outTotal  = pRow[1].getInt32();
        outWarn   = pRow[2].getInt32();
        outDanger = pRow[3].getInt32();
    }
    pResult->endQuery();
    delete pResult;

    LOGI("AdminManager::getAuditStats enterpriseId: %d, today: %d, total: %d, warn: %d, danger: %d",
         enterpriseId, outToday, outTotal, outWarn, outDanger);
}
