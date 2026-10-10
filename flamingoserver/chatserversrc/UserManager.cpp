/**
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/
#include "UserManager.h"
#include <memory>
#include <sstream>
#include <stdio.h>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "AccountName.h"
#include "Protocol.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

//addUser 拼装 INSERT 语句的缓冲区大小。
//t_user 里被拼进来的几个文本列合计上限约 288 字节（username/nickname/password/
//phonenumber 各 64、salt 32），转义后最坏翻倍约 576 字节，加上语句本身的固定文本
//与几个整型字段，2048 有充裕余量，不会因 snprintf 截断拼出半截 SQL。
#define ADD_USER_SQL_BUF_LEN   2048

//抽随机账号时最多重抽的次数。8 位数字有 9000 万个号，即使已有百万账号，单次撞号概率也只有约 1%，
//20 次都撞上的概率可以忽略
#define ACCOUNT_ALLOC_MAX_TRIES   20

//两个账号是否相同（只按 ASCII 不区分大小写）：库上 f_username 在 SQLite 上是 COLLATE NOCASE、
//在 MySQL 上是不区分大小写的 utf8_general_ci，内存里的比较与之保持一致
static bool sameAccount(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i];
        char y = b[i];
        if (x >= 'a' && x <= 'z')
            x = static_cast<char>(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = static_cast<char>(y - 'a' + 'A');
        if (x != y)
            return false;
    }
    return true;
}

UserManager::UserManager()
    : m_accountRng(std::random_device()())
{

}

UserManager::~UserManager()
{

}

bool UserManager::init()
{
    //从数据库中加载所有用户信息。企业版没有好友关系，启动时不再加载任何关系链，
    //联系人一律由 DepartmentManager 从组织架构 t_department_member 给出。
    if (!loadUsersFromDb())
        return false;

    return true;
}

//加载用户资料时 SELECT 的列清单。loadUsersFromDb 与 reloadUserCache 共用，列的顺序须与
//fillUserFromRow 的下标一致；新增列时两处一起改，避免两条加载路径取的列不一样
static const char* const kUserSelectColumns =
    "f_user_id, f_username, f_nickname, f_password, f_facetype, f_customface, "
    "f_gender, f_birthday, f_signature, f_address, f_phonenumber, f_mail, f_teaminfo, f_status, f_salt, f_usertype, "
    "f_landline, f_post, f_phone_visible, f_landline_visible";

//按 kUserSelectColumns 的列顺序把一行查询结果填进 User
static void fillUserFromRow(Field* pRow, User& u)
{
    u.userid      = pRow[0].getInt32();
    u.username    = pRow[1].getString();
    u.nickname    = pRow[2].getString();
    u.password    = pRow[3].getString();
    u.facetype    = pRow[4].getInt32();
    u.customface  = pRow[5].getString();
    u.gender      = pRow[6].getInt32();
    u.birthday    = pRow[7].getInt32();
    u.signature   = pRow[8].getString();
    u.address     = pRow[9].getString();
    u.phonenumber = pRow[10].getString();
    u.mail        = pRow[11].getString();
    u.teaminfo    = pRow[12].getString();
    u.status      = pRow[13].getInt32();
    u.salt        = pRow[14].getString();
    u.usertype    = pRow[15].getInt32();
    u.landline    = pRow[16].getString();
    u.post        = pRow[17].getString();
    u.phonevisible    = pRow[18].getInt32();
    u.landlinevisible = pRow[19].getInt32();
}

bool UserManager::loadUsersFromDb()
{  
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGF("UserManager::LoadUsersFromDb failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //TODO: 到底是空数据集还是出错，需要修改下返回类型
    const std::string loadSql = std::string("SELECT ") + kUserSelectColumns + " FROM t_user ORDER BY f_user_id DESC";
    QueryResult* pResult = pConn->query(loadSql.c_str());
    if (NULL == pResult)
    {
        LOGI("UserManager::_Query error, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }
    
    string teaminfo;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        
        User u;
        fillUserFromRow(pRow, u);
        m_allCachedUsers.push_back(u);

        LOGI("userid: %d, username: %s, password: %s, nickname: %s, signature: %s", u.userid, u.username.c_str(), u.password.c_str(), u.nickname.c_str(), u.signature.c_str());
        
        //计算当前最大userid
        if (u.userid < GROUPID_BOUBDARY && u.userid > m_baseUserId)
            m_baseUserId = u.userid;

        //计算当前最大群组id
        if (u.userid > GROUPID_BOUBDARY && u.userid > m_baseGroupId)
            m_baseGroupId = u.userid;

        if (!pResult->nextRow())
        {
            break;
        }
    }

    LOGI("current base userid: %d, current base group id: %d", m_baseUserId.load(), m_baseGroupId.load());

    pResult->endQuery();

    delete pResult;

    return true;
}

int32_t UserManager::allocUserId()
{
    return ++m_baseUserId;
}

std::string UserManager::allocRandomAccount(const std::string& prefix)
{
    std::uniform_int_distribution<int32_t> dist(RANDOM_ACCOUNT_NUMBER_MIN, RANDOM_ACCOUNT_NUMBER_MAX);
    std::lock_guard<std::mutex> guard(m_mutex);
    for (int tries = 0; tries < ACCOUNT_ALLOC_MAX_TRIES; ++tries)
    {
        const std::string candidate = MakeRandomAccount(prefix, dist(m_accountRng));
        bool taken = false;
        for (const auto& iter : m_allCachedUsers)
        {
            if (sameAccount(iter.username, candidate))
            {
                taken = true;
                break;
            }
        }
        if (!taken)
            return candidate;
        LOGW("allocRandomAccount: candidate %s already taken, retry", candidate.c_str());
    }
    LOGE("allocRandomAccount: no free account after %d tries, prefix: '%s'", ACCOUNT_ALLOC_MAX_TRIES, prefix.c_str());
    return std::string();
}

void UserManager::seedAccountRngForTest(uint32_t seed)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_accountRng.seed(seed);
}

bool UserManager::addUser(User& u, int64_t enterpriseId)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::AddUser failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //先分配用户ID：账号名要按它生成，故必须在拼 SQL 之前拿到。
    const int32_t newUserId = allocUserId();

    //账号留空表示"由服务端自动生成"：抽一个默认 EMP 前缀的随机账号。正常情况下自助注册会先按企业
    //前缀抽好账号再调本函数（见 ChatSession.cpp 的 createUser），这里只是兜底；规则与 admin 控制台
    //建员工共用 allocRandomAccount，两边不会各写一份而漂移
    if (u.username.empty())
    {
        u.username = allocRandomAccount(std::string());
        if (u.username.empty())
        {
            //重抽次数用完仍然撞号（理论上不会发生）：宁可建号失败，也不要造出一个与别人相撞的账号
            LOGE("UserManager::addUser failed: cannot allocate account name for userid %d", newUserId);
            return false;
        }
    }

    //昵称与账号都可能来自用户输入，必须转义后再拼进 SQL：名字里带一个单引号就足以
    //破坏整条语句。password/salt 虽然是程序生成的十六进制串，一并转义以免日后
    //换了算法出现意外字符。
    std::string usernameEsc = dbapi::EscapeString(u.username);
    std::string nicknameEsc = dbapi::EscapeString(u.nickname);
    std::string passwordEsc = dbapi::EscapeString(u.password);
    std::string saltEsc     = dbapi::EscapeString(u.salt);
    std::string phoneEsc    = dbapi::EscapeString(u.phonenumber);

    char sql[ADD_USER_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_salt, f_usertype, "
        "f_phonenumber, f_enterprise_id, f_register_time) "
        "VALUES(%d, '%s', '%s', '%s', '%s', %d, '%s', %lld, NOW())",
        newUserId, usernameEsc.c_str(), nicknameEsc.c_str(), passwordEsc.c_str(),
        saltEsc.c_str(), u.usertype, phoneEsc.c_str(), (long long)enterpriseId);
    if (!pConn->execute(sql))
    {
        LOGW("insert user error, sql: %s", sql);
        return false;
    }
    //设置一些字段的默认值
    u.userid = newUserId;
    u.facetype = 0;
    u.birthday = 19900101;
    u.gender = 0;
    u.ownerid = 0;
    u.status = 0;   //新注册用户默认启用
    //公开范围与两列的库默认值一致：手机号仅本人可见，座机对同事可见
    u.phonevisible    = profile_visibility_self_only;
    u.landlinevisible = profile_visibility_colleagues;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(u);
    }

    return true;
}

bool UserManager::updateUserInfoInDb(int32_t userid, const User& newuserinfo)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed,  please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //文字字段都可能来自用户输入，必须转义后再拼进 SQL：签名里带一个单引号就足以破坏整条语句
    //（2026-10-04 修复，此前这里直接拼接原文）
    std::ostringstream osSql;
    osSql << "UPDATE t_user SET f_nickname='"
          << dbapi::EscapeString(newuserinfo.nickname) << "', f_facetype="
          << newuserinfo.facetype << ", f_customface='"
          << dbapi::EscapeString(newuserinfo.customface) << "', f_gender="
          << newuserinfo.gender << ", f_birthday="
          << newuserinfo.birthday << ", f_signature='"
          << dbapi::EscapeString(newuserinfo.signature) << "', f_address='"
          << dbapi::EscapeString(newuserinfo.address) << "', f_phonenumber='"
          << dbapi::EscapeString(newuserinfo.phonenumber) << "', f_mail='"
          << dbapi::EscapeString(newuserinfo.mail) << "', f_landline='"
          << dbapi::EscapeString(newuserinfo.landline) << "', f_post='"
          << dbapi::EscapeString(newuserinfo.post) << "', f_phone_visible="
          << newuserinfo.phonevisible << ", f_landline_visible="
          << newuserinfo.landlinevisible << " WHERE f_user_id="
          << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("UpdateUserInfo error, sql: %s", osSql.str().c_str());
        return false;
    }

    LOGI("update userinfo successfully, userid: %d, sql: %s", userid, osSql.str().c_str());

    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.nickname = newuserinfo.nickname;
            iter.facetype = newuserinfo.facetype;
            iter.customface = newuserinfo.customface;
            iter.gender = newuserinfo.gender;
            iter.birthday = newuserinfo.birthday;
            iter.signature = newuserinfo.signature;
            iter.address = newuserinfo.address;
            iter.phonenumber = newuserinfo.phonenumber;
            iter.mail = newuserinfo.mail;
            iter.landline = newuserinfo.landline;
            iter.post = newuserinfo.post;
            iter.phonevisible = newuserinfo.phonevisible;
            iter.landlinevisible = newuserinfo.landlinevisible;
            return true;
        }
    }

    LOGE("Failed to update userinfo to db, find exsit user in memory error, m_allCachedUsers.size(): %d, userid: %d, sql: %s", m_allCachedUsers.size(), userid, osSql.str().c_str());

    return false;
}

bool UserManager::modifyUserPassword(int32_t userid, const std::string& newpassword)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed,  please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_user SET f_password='"
        << newpassword << "' WHERE f_user_id="
        << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("UpdateUserInfo error, sql: %s", osSql.str().c_str());
        return false;
    }

    LOGI("update user password successfully, userid: %d, sql: %s" , userid, osSql.str().c_str());

    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.password = newpassword;         
            return true;
        }
    }

    LOGE("Failed to update user password to db, find no exsit user in memory error, m_allCachedUsers.size(): %d, userid: %d, sql: %s", m_allCachedUsers.size(), userid, osSql.str().c_str());

    return false;
}

bool UserManager::getUserInfoByUsername(const std::string& username, User& u)
{
    //账号不区分大小写，与库上 uk_f_username 的比较规则一致（2026-10-06 起；此前在内存里区分大小写，
    //库里存 HW.48203917、用户输入 hw.48203917 时登录不上）
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (sameAccount(iter.username, username))
        {
            u = iter;
            return true;
        }
    }

    return false;
}

bool UserManager::findLoginUser(const std::string& input, User& u, bool& matchedByPhone)
{
    matchedByPhone = false;
    if (input.empty())
        return false;
    //先按账号：账号恰好是 11 位数字的老账号也按账号优先，不会被当成手机号
    if (getUserInfoByUsername(input, u))
        return true;
    //账号查不到、且输入像手机号时，再按手机号找
    if (IsValidPhoneNumber(input) && getUserInfoByPhone(input, u))
    {
        matchedByPhone = true;
        return true;
    }
    return false;
}

bool UserManager::getUserInfoByUserId(int32_t userid, User& u)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            u = iter;
            return true;
        }
    }

    return false;
}

bool UserManager::getUserInfoByUserId(int32_t userid, User*& u)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            u = &iter;
            return true;
        }
    }

    return false;
}

bool UserManager::getUserInfoByPhone(const std::string& phone, User& u)
{
    if (phone.empty())
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        //只看普通用户：群账号没有手机号，也不能登录
        if (iter.userid >= GROUPID_BOUBDARY)
            continue;
        if (iter.phonenumber == phone)
        {
            u = iter;
            return true;
        }
    }

    return false;
}

bool UserManager::isPhoneTaken(const std::string& phone, int32_t excludeUserId)
{
    if (phone.empty())
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        //群账号没有手机号，也不参与登录，跳过
        if (iter.userid >= GROUPID_BOUBDARY || iter.userid == excludeUserId)
            continue;
        if (iter.phonenumber == phone)
            return true;
    }
    return false;
}

bool UserManager::searchUsersByPrefix(const std::string& prefix, bool wantGroup,
                                      std::vector<User>& out, int maxResults)
{
    if (prefix.empty())
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        //GROUPID_BOUBDARY 以上为群号，以下为普通用户
        bool isGroup = (iter.userid >= GROUPID_BOUBDARY);
        if (isGroup != wantGroup)
            continue;
        //机器人与服务号账号不出现在找人结果里（docs/机器人方案.md §3.2）
        if (!wantGroup && iter.usertype != 0)
            continue;

        //用户按账号前缀匹配；群按群号(userid 十进制串)前缀匹配
        std::string key = wantGroup ? std::to_string(iter.userid) : iter.username;
        if (key.size() >= prefix.size() && key.compare(0, prefix.size(), prefix) == 0)
        {
            out.push_back(iter);
            if (static_cast<int>(out.size()) >= maxResults)
                break;
        }
    }

    return true;
}

bool UserManager::addToBlacklist(int32_t userid, int32_t blockedUserid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::addToBlacklist db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //INSERT IGNORE：联合主键已存在时静默忽略，重复拉黑视为成功
    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT IGNORE INTO t_blacklist(f_userid, f_blocked_userid) VALUES(%d, %d)",
        userid, blockedUserid);
    if (!pConn->execute(sql))
    {
        LOGE("addToBlacklist insert error, sql: %s", sql);
        return false;
    }
    return true;
}

bool UserManager::removeFromBlacklist(int32_t userid, int32_t blockedUserid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::removeFromBlacklist db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //记录不存在时 DELETE 影响 0 行，仍视为成功
    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_blacklist WHERE f_userid = %d AND f_blocked_userid = %d",
        userid, blockedUserid);
    if (!pConn->execute(sql))
    {
        LOGE("removeFromBlacklist delete error, sql: %s", sql);
        return false;
    }
    return true;
}

bool UserManager::isInBlacklist(int32_t userid, int32_t blockedUserid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_userid FROM t_blacklist WHERE f_userid = %d AND f_blocked_userid = %d",
        userid, blockedUserid);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = (pResult->fetch() != NULL);
    pResult->endQuery();
    delete pResult;
    return found;
}

bool UserManager::reloadUserCache(int32_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::reloadUserCache failed to connect db, userid: %d", userid);
        return false;
    }

    //1. 先从数据库取该用户的最新数据（与 loadUsersFromDb 取相同列，额外取 f_status）。
    //   此处不持 m_mutex，避免在数据库 I/O 期间阻塞其它缓存读写。
    const std::string sql = std::string("SELECT ") + kUserSelectColumns + " FROM t_user WHERE f_user_id = "
                            + std::to_string(userid);
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
    {
        LOGE("UserManager::reloadUserCache query error, userid: %d", userid);
        return false;
    }

    bool existsInDb = false;
    User dbUser;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillUserFromRow(pRow, dbUser);
        dbUser.ownerid     = 0;
        existsInDb = true;
    }
    pResult->endQuery();
    delete pResult;

    //2. 用户已被删除：从缓存中移除。
    if (!existsInDb)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto it = m_allCachedUsers.begin(); it != m_allCachedUsers.end(); ++it)
        {
            if (it->userid == userid)
            {
                m_allCachedUsers.erase(it);
                break;
            }
        }
        LOGI("UserManager::reloadUserCache: user %d removed from cache (deleted in db)", userid);
        return true;
    }

    //3. 用户在缓存中已存在：原地更新标量字段。
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto& iter : m_allCachedUsers)
        {
            if (iter.userid == userid)
            {
                iter.username    = dbUser.username;
                iter.nickname    = dbUser.nickname;
                iter.password    = dbUser.password;
                iter.facetype    = dbUser.facetype;
                iter.customface  = dbUser.customface;
                iter.gender      = dbUser.gender;
                iter.birthday    = dbUser.birthday;
                iter.signature   = dbUser.signature;
                iter.address     = dbUser.address;
                iter.phonenumber = dbUser.phonenumber;
                iter.mail        = dbUser.mail;
                iter.teaminfo    = dbUser.teaminfo;
                iter.status      = dbUser.status;
                iter.salt        = dbUser.salt;
                iter.usertype    = dbUser.usertype;
                iter.landline    = dbUser.landline;
                iter.post        = dbUser.post;
                iter.phonevisible    = dbUser.phonevisible;
                iter.landlinevisible = dbUser.landlinevisible;
                LOGI("UserManager::reloadUserCache: user %d updated in cache", userid);
                return true;
            }
        }
    }

    //4. 用户在库中存在、缓存中没有（admin 新建的员工）：直接加入缓存。
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(dbUser);
    }
    LOGI("UserManager::reloadUserCache: user %d inserted into cache (new)", userid);
    return true;
}
