/**
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/
#include <memory>
#include <sstream>
#include <stdio.h>
#include "../database/databasemysql.h"
#include "../base/logging.h"
#include "UserManager.h"

UserManager::UserManager()
{
    
}

UserManager::~UserManager()
{

}

bool UserManager::Init(const char* dbServer, const char* dbUserName, const char* dbPassword, const char* dbName)
{
    m_strDbServer = dbServer;
    m_strDbUserName = dbUserName;
    if (dbPassword != NULL)
        m_strDbPassword = dbPassword;
    m_strDbName = dbName;

    //从数据库中加载所有用户信息
    if (!LoadUsersFromDb())
        return false;

    for (auto& iter : m_allCachedUsers)
    {
        if (!LoadRelationshipFromDb(iter.userid, iter.friends))
        {
            LOG_WARN << "Load relationship from db error, userid=" << iter.userid;
        }
    }

    return true;
}

bool UserManager::LoadUsersFromDb()
{  
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadUsersFromDb failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    //TODO: 到底是空数据集还是出错，需要修改下返回类型
    QueryResult* pResult = pConn->Query("SELECT f_user_id, f_username, f_nickname, f_password,  f_facetype, f_customface, f_gender, f_birthday, f_signature, f_address, f_phonenumber, f_mail FROM t_user ORDER BY  f_user_id DESC");
    if (NULL == pResult)
    {
        LOG_INFO << "UserManager::_Query error, dbname=" << m_strDbName;
        return false;
    }
 
    while (true)
    {
        Field* pRow = pResult->Fetch();
        if (pRow == NULL)
            break;
        
        User u;
        u.userid = pRow[0].GetInt32();
        u.username = pRow[1].GetString();
        u.nickname = pRow[2].GetString();
        u.password = pRow[3].GetString();
        u.facetype = pRow[4].GetInt32();
        u.customface = pRow[5].GetString();
        u.gender = pRow[6].GetInt32();
        u.birthday = pRow[7].GetInt32();
        u.signature = pRow[8].GetString();
        u.address = pRow[9].GetString();
        u.phonenumber = pRow[10].GetString();
        u.mail = pRow[11].GetString();
        m_allCachedUsers.push_back(u);

        LOG_INFO << "userid: " << u.userid << ", username: " << u.username << ", password: " << u.password << ", nickname: " << u.nickname << ", signature: " << u.signature;
        
        //计算当前最大userid
        if (u.userid < GROUPID_BOUBDARY && u.userid > m_baseUserId)
            m_baseUserId = u.userid;

        //计算当前最大群组id
        if (u.userid > GROUPID_BOUBDARY && u.userid > m_baseGroupId)
            m_baseGroupId = u.userid;

        if (!pResult->NextRow())
        {
            break;
        }
    }

    LOG_INFO << "current base userid: " << m_baseUserId << ", current base group id: " << m_baseGroupId;

    pResult->EndQuery();

    return true;
}

bool UserManager::AddUser(User& u)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::AddUser failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    ++ m_baseUserId;
    char sql[256] = { 0 };
    snprintf(sql, 256, "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_register_time) VALUES(%d, '%s', '%s', '%s', NOW())", m_baseUserId, u.username.c_str(), u.nickname.c_str(), u.password.c_str());
    if (!pConn->Execute(sql))
    {
        LOG_WARN << "insert user error, sql=" << sql;
        return false;
    }
    //设置一些字段的默认值
    u.userid = m_baseUserId;
    u.facetype = 0;
    u.birthday = 19900101;
    u.gender = 0;
    u.ownerid = 0;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(u);
    }

    return true;
}

bool UserManager::MakeFriendRelationship(int32_t smallUserid, int32_t greaterUserid)
{
    if (smallUserid >= greaterUserid)
        return false;

    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadUsersFromDb failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "INSERT INTO t_user_relationship(f_user_id1, f_user_id2) VALUES(%d, %d)", smallUserid, greaterUserid);
    if (!pConn->Execute(sql))
    {
        LOG_ERROR << "make relationship error, sql=" << sql << ", smallUserid = " << smallUserid << ", greaterUserid = " << greaterUserid;;
        return false;
    }
    
    if (!AddFriendToUser(smallUserid, greaterUserid))
    {
        LOG_ERROR << "make relationship error, smallUserid=" << smallUserid << ", greaterUserid=" << greaterUserid;
        return false;
    }

    return true;
}

bool UserManager::ReleaseFriendRelationship(int32_t smallUserid, int32_t greaterUserid)
{
    if(smallUserid >= greaterUserid)
        return false;

    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadUsersFromDb failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "DELETE FROM t_user_relationship WHERE f_user_id1 = %d AND f_user_id2 = %d", smallUserid, greaterUserid);
    if (!pConn->Execute(sql))
    {
        LOG_ERROR << "release relationship error, sql=" << sql << ", smallUserid = " << smallUserid << ", greaterUserid = " << greaterUserid;;
        return false;
    }

    if (!DeleteFriendToUser(smallUserid, greaterUserid))
    {
        LOG_ERROR << "delete relationship error, smallUserid=" << smallUserid << ", greaterUserid=" << greaterUserid;
        return false;
    }

    return true;
}

bool UserManager::AddFriendToUser(int32_t userid, int32_t friendid)
{
    bool bFound1 = false;
    bool bFound2 = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.friends.insert(friendid);
            bFound1 = true;
        }

        if (iter.userid == friendid)
        {
            iter.friends.insert(userid);
            bFound2 = true;
        }

        if (bFound1 && bFound2)
            return true;
    }

    return false;
}

bool UserManager::DeleteFriendToUser(int32_t userid, int32_t friendid)
{
    bool bFound1 = false;
    bool bFound2 = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.friends.erase(friendid);
            bFound1 = true;
        }

        if (iter.userid == friendid)
        {
            iter.friends.erase(userid);
            bFound2 = true;
        }

        if (bFound1 && bFound2)
            return true;
    }

    return false;
}

bool UserManager::UpdateUserInfo(int32_t userid, const User& newuserinfo)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_ERROR << "UserManager::Initialize db failed, please check params";
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
    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "UpdateUserInfo error, sql=" << osSql.str();
        return false;
    }

    LOG_INFO << "update userinfo successfully, userid: " << userid << ", sql: " << osSql.str();

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

    LOG_ERROR << "Failed to update userinfo to db, find exsit user in memory error, m_allCachedUsers.size(): " << m_allCachedUsers.size() << ", userid: " << userid << ", sql : " << osSql.str();

    return false;
}


bool UserManager::ModifyUserPassword(int32_t userid, const std::string& newpassword)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_ERROR << "UserManager::Initialize db failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_user SET f_password='"
        << newpassword << "' WHERE f_user_id="
        << userid;
    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "UpdateUserInfo error, sql=" << osSql.str();
        return false;
    }

    LOG_INFO << "update user password successfully, userid: " << userid << ", sql : " << osSql.str();

    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.password = newpassword;         
            return true;
        }
    }

    LOG_ERROR << "Failed to update user password to db, find exsit user in memory error, m_allCachedUsers.size(): " << m_allCachedUsers.size() << ", userid: " << userid << ", sql : " << osSql.str();

    return false;
}

bool UserManager::AddGroup(const char* groupname, int32_t ownerid, int32_t& groupid)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::AddUser failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    ++m_baseGroupId;
    char sql[256] = { 0 };
    snprintf(sql, 256, "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_owner_id, f_register_time) VALUES(%d, '%d', '%s', '', %d,  NOW())", m_baseGroupId, m_baseGroupId, groupname, ownerid);
    if (!pConn->Execute(sql))
    {
        LOG_WARN << "insert group error, sql=" << sql;
        return false;
    }
    
    groupid = m_baseGroupId;

    User u;
    u.userid = groupid;
    char szUserName[12] = { 0 };
    snprintf(szUserName, 12, "%d", groupid);
    u.username = szUserName;
    u.nickname = groupname;
    u.ownerid = ownerid;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(u);
    }

    return true;
}

bool UserManager::SaveChatMsgToDb(int32_t senderid, int32_t targetid, const std::string& chatmsg)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::SaveChatMsgToDb failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    ostringstream sql;
    sql << "INSERT INTO t_chatmsg(f_senderid, f_targetid, f_msgcontent) VALUES(" << senderid << ", " << targetid << ", '" << chatmsg << "')";
    if (!pConn->Execute(sql.str().c_str()))
    {
        LOG_WARN << "UserManager::SaveChatMsgToDb, sql=" << sql.str() << ", senderid = " << senderid << ", targetid = " << targetid << ", chatmsg:" << chatmsg;
        return false;
    }

    return true;
}

bool UserManager::GetUserInfoByUsername(const std::string& username, User& u)
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

bool UserManager::GetUserInfoByUserId(int32_t userid, User& u)
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

bool UserManager::GetFriendInfoByUserId(int32_t userid, std::list<User>& friends)
{
    std::set<int32_t> friendsId;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            friendsId = iter.friends;
            break;
        }
    }

    //TODO: 这种算法效率太低
    for (const auto& iter : friendsId)
    {
        User u;
        for (const auto& iter2 : m_allCachedUsers)
        {
            if (iter2.userid == iter)
            {
                u = iter2;
                friends.push_back(u);
                break;
            }
        }
    }

    return true;
}

bool UserManager::LoadRelationshipFromDb(int32_t userid, std::set<int32_t>& r)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadRelationhipFromDb failed, please check params";
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "SELECT f_user_id1, f_user_id2 FROM t_user_relationship WHERE f_user_id1 = %d OR f_user_id2 = %d ", userid, userid);
    QueryResult* pResult = pConn->Query(sql);
    if (NULL == pResult)
    {
        LOG_INFO << "UserManager::Query error, db=" << m_strDbName;
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->Fetch();
        if (pRow == NULL)
            break;

        int friendid1 = pRow[0].GetInt32();
        int friendid2 = pRow[1].GetInt32();
        if (friendid1 == userid)
        {
            r.insert(friendid2);
            //LOG_INFO << "userid=" << userid << ", friendid=" << friendid2;
        }
        else
        {
            r.insert(friendid1);
            //LOG_INFO << "userid=" << userid << ", friendid=" << friendid1;
        }

        
        if (!pResult->NextRow())
        {
            break;
        }
    }

    pResult->EndQuery();
    
    return true;
}
