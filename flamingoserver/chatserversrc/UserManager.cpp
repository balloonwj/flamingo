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
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

//addUser 拼装 INSERT 语句的缓冲区大小。
//t_user 里被拼进来的几个文本列合计上限约 288 字节（username/nickname/password/
//phonenumber 各 64、salt 32），转义后最坏翻倍约 576 字节，加上语句本身的固定文本
//与几个整型字段，2048 有充裕余量，不会因 snprintf 截断拼出半截 SQL。
#define ADD_USER_SQL_BUF_LEN   2048

UserManager::UserManager()
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
    QueryResult* pResult = pConn->query("SELECT f_user_id, f_username, f_nickname, f_password,  f_facetype, f_customface, f_gender, f_birthday, f_signature, f_address, f_phonenumber, f_mail, f_teaminfo, f_status, f_salt, f_usertype FROM t_user ORDER BY  f_user_id DESC");
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
        u.userid = pRow[0].getInt32();
        u.username = pRow[1].getString();
        u.nickname = pRow[2].getString();
        u.password = pRow[3].getString();
        u.facetype = pRow[4].getInt32();
        u.customface = pRow[5].getString();
        u.gender = pRow[6].getInt32();
        u.birthday = pRow[7].getInt32();
        u.signature = pRow[8].getString();
        u.address = pRow[9].getString();
        u.phonenumber = pRow[10].getString();
        u.mail = pRow[11].getString();
        u.teaminfo = pRow[12].getString();
        u.status = pRow[13].getInt32();
        u.salt = pRow[14].getString();
        u.usertype = pRow[15].getInt32();
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

    //账号留空表示"由服务端自动生成"（PC 客户端自助注册走这条路）。生成规则与
    //admin 控制台建员工共用 MakeEmployeeAccount，两边不会各写一份而漂移。
    if (u.username.empty())
    {
        u.username = MakeEmployeeAccount(newUserId);
        if (u.username.empty())
        {
            //只有分配到非正数ID才会走到这里，说明ID分配基数出了问题。
            //宁可建号失败，也不要造出一个空账号或与别人相撞的账号。
            LOGE("UserManager::addUser failed: cannot make account name for userid %d", newUserId);
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

    std::ostringstream osSql;
    osSql << "UPDATE t_user SET f_nickname='"        
          << newuserinfo.nickname << "', f_facetype=" 
          << newuserinfo.facetype << ", f_customface='" 
          << newuserinfo.customface << "', f_gender=" 
          << newuserinfo.gender << ", f_birthday=" 
          << newuserinfo.birthday << ", f_signature='" 
          << newuserinfo.signature << "', f_address='" 
          << newuserinfo.address << "', f_phonenumber='" 
          << newuserinfo.phonenumber << "', f_mail='" 
          << newuserinfo.mail << "' WHERE f_user_id=" 
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
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.username == username)
        {
            u = iter;
            return true;
        }
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
        if (iter.phonenumber == phone)
        {
            u = iter;
            return true;
        }
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
    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_user_id, f_username, f_nickname, f_password, f_facetype, f_customface, "
        "f_gender, f_birthday, f_signature, f_address, f_phonenumber, f_mail, f_teaminfo, f_status, f_salt, f_usertype "
        "FROM t_user WHERE f_user_id = %d", userid);
    QueryResult* pResult = pConn->query(sql);
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
        dbUser.userid      = pRow[0].getInt32();
        dbUser.username    = pRow[1].getString();
        dbUser.nickname    = pRow[2].getString();
        dbUser.password    = pRow[3].getString();
        dbUser.facetype    = pRow[4].getInt32();
        dbUser.customface  = pRow[5].getString();
        dbUser.gender      = pRow[6].getInt32();
        dbUser.birthday    = pRow[7].getInt32();
        dbUser.signature   = pRow[8].getString();
        dbUser.address     = pRow[9].getString();
        dbUser.phonenumber = pRow[10].getString();
        dbUser.mail        = pRow[11].getString();
        dbUser.teaminfo    = pRow[12].getString();
        dbUser.status      = pRow[13].getInt32();
        dbUser.salt        = pRow[14].getString();
        dbUser.usertype    = pRow[15].getInt32();
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
