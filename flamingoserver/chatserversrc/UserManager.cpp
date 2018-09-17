/**
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/
#include <memory>
#include <sstream>
#include <stdio.h>
#include "../database/DatabaseMysql.h"
#include "../base/Logging.h"
#include "../jsoncpp-0.5.0/json.h"
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
            LOG_ERROR << "Load relationship from db error, userid=" << iter.userid;
            continue;
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
    QueryResult* pResult = pConn->Query("SELECT f_user_id, f_username, f_nickname, f_password,  f_facetype, f_customface, f_gender, f_birthday, f_signature, f_address, f_phonenumber, f_mail, f_teaminfo FROM t_user ORDER BY  f_user_id DESC");
    if (NULL == pResult)
    {
        LOG_INFO << "UserManager::_Query error, dbname=" << m_strDbName;
        return false;
    }
    
    string teaminfo;
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
        u.teaminfo = pRow[12].GetString();
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

//数据库里面互为好友的两个人id，小者在先，大者在后
bool UserManager::MakeFriendRelationshipInDB(int32_t smallUserid, int32_t greaterUserid)
{
    if (smallUserid == greaterUserid)
        return false;

    if (smallUserid > greaterUserid)
    {
        int32_t tmp = greaterUserid;
        greaterUserid = smallUserid;
        smallUserid = tmp;
    }

    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadUsersFromDb failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    char sql[512] = { 0 };
    snprintf(sql, 512, "INSERT INTO t_user_relationship(f_user_id1, f_user_id2, f_user1_teamname, f_user2_teamname) VALUES(%d, %d, '%s', '%s')", smallUserid, greaterUserid, DEFAULT_TEAMNAME, DEFAULT_TEAMNAME);
    if (!pConn->Execute(sql))
    {
        LOG_ERROR << "make relationship error, sql=" << sql << ", smallUserid = " << smallUserid << ", greaterUserid = " << greaterUserid;;
        return false;
    }
    
    //if (!AddFriendToUser(smallUserid, greaterUserid))
    //{
    //    LOG_ERROR << "make relationship error, smallUserid=" << smallUserid << ", greaterUserid=" << greaterUserid;
    //    return false;
    //}

    return true;
}

bool UserManager::ReleaseFriendRelationshipInDBAndMemory(int32_t smallUserid, int32_t greaterUserid)
{
    if(smallUserid == greaterUserid)
        return false;

    if (smallUserid > greaterUserid)
    {
        int32_t tmp = greaterUserid;
        greaterUserid = smallUserid;
        smallUserid = tmp;
    }

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

bool UserManager::UpdateUserRelationshipInMemory(int32_t userid, int32_t target, FRIEND_OPERATION operation)
{
    if (operation == FRIEND_OPERATION_ADD)
    {
        bool found1 = false;
        bool found2 = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            for (auto& iter : m_allCachedUsers)
            {
                if (iter.userid == userid)
                {
                    FriendInfo fi = { target, "", DEFAULT_TEAMNAME };
                    iter.friends.emplace_back(fi);
                    found1 = true;
                    continue;;
                }

                if (iter.userid == target)
                {
                    FriendInfo fi = { userid, "", DEFAULT_TEAMNAME };
                    iter.friends.emplace_back(fi);
                    found2 = true;
                    continue;
                }
            }
        }

        if (found1 && found2)
            return true;

        return false;
    }
    else if (operation == FRIEND_OPERATION_DELETE)
    {
        bool found1 = false;
        bool found2 = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            for (auto& iter : m_allCachedUsers)
            {
                if (iter.userid == userid)
                {
                    for (auto iter2 = iter.friends.begin(); iter2 != iter.friends.end(); ++iter2)
                    {
                        if (iter2->friendid == target)
                        {
                            iter.friends.erase(iter2);
                            found1 = true;
                            break;
                        }
                    }

                    if (found1)
                        continue;
                }

                if (iter.userid == target)
                {
                    for (auto iter3 = iter.friends.begin(); iter3 != iter.friends.end(); ++iter3)
                    {
                        if (iter3->friendid == userid)
                        {
                            iter.friends.erase(iter3);
                            found2 = true;
                            break;
                        }
                    }

                    if (found2)
                        continue;
                }
            }
        }

        if (found1 && found2)
            return true;

        return false;
    }

    return false;
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
            FriendInfo fi = { friendid, "", DEFAULT_TEAMNAME };
            iter.friends.emplace_back(fi);
            bFound1 = true;
        }

        if (iter.userid == friendid)
        {
            FriendInfo fi = { userid, "", DEFAULT_TEAMNAME };
            iter.friends.emplace_back(fi);
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
    for (auto iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            for (auto iter2 = iter.friends.begin(); iter2 != iter.friends.end(); ++iter2)
            {
                if (iter2->friendid == friendid)
                {
                    iter.friends.erase(iter2);
                    bFound1 = true;
                    break;
                }
            }
            
            if (bFound1)
                continue;
        }

        if (iter.userid == friendid)
        {
            for (auto iter2 = iter.friends.begin(); iter2 != iter.friends.end(); ++iter2)
            {
                if (iter2->friendid == userid)
                {
                    iter.friends.erase(iter2);
                    bFound2 = true;
                    break;
                }
            }

            if (bFound2)
                continue;
        }   
    }

    if (bFound1 && bFound2)
        return true;

    return false;
}

bool UserManager::IsFriend(int32_t userid, int32_t friendid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            for (auto iter2 = iter.friends.begin(); iter2 != iter.friends.end(); ++iter2)
            {
                if (iter2->friendid == friendid)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool UserManager::UpdateUserInfoInDb(int32_t userid, const User& newuserinfo)
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

    LOG_ERROR << "Failed to update user password to db, find no exsit user in memory error, m_allCachedUsers.size(): " << m_allCachedUsers.size() << ", userid: " << userid << ", sql : " << osSql.str();

    return false;
}

bool UserManager::UpdateUserTeamInfoInDbAndMemory(int32_t userid, const std::string& newteaminfo)
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
    osSql << "UPDATE t_user SET f_teaminfo='"
        << newteaminfo << "' WHERE f_user_id="
        << userid;
    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "Update Team Info error, sql=" << osSql.str();
        return false;
    }

    LOG_INFO << "update user teaminfo successfully, userid: " << userid << ", sql : " << osSql.str();

    //TODO: 重复的代码，需要去掉
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            iter.teaminfo = newteaminfo;
            return true;
        }
    }

    LOG_ERROR << "Failed to update user teaminfo to db, find no exsit user in memory error, m_allCachedUsers.size(): " << m_allCachedUsers.size() << ", userid: " << userid << ", sql : " << osSql.str();

    return false;
}

bool UserManager::DeleteTeam(int32_t userid, const std::string& deletedteamname)
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
    osSql << "UPDATE t_user_relationship SET f_user1_teamname='"
          << DEFAULT_TEAMNAME << "' WHERE f_user_id1=" << userid 
          << " AND f_user1_teamname='" << deletedteamname << "'";
    

    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "DeleteTeam error, sql=" << osSql.str() 
                  << ", userid=" << userid 
                  << ", deletedteamname=" << deletedteamname;
        return false;
    }

    osSql.str("");

    osSql << "UPDATE t_user_relationship SET f_user2_teamname='"
        << DEFAULT_TEAMNAME << "' WHERE f_user_id2=" << userid
        << " AND f_user2_teamname='" << deletedteamname << "'";

    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "DeleteTeam error, sql=" << osSql.str()
            << ", userid=" << userid
            << ", deletedteamname=" << deletedteamname;
        return false;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto& iter : m_allCachedUsers)
        {
            if (iter.userid == userid)
            {
                for (auto& iter2 : iter.friends)
                {
                    if (iter2.teamname == deletedteamname)
                        iter2.teamname = DEFAULT_TEAMNAME;
                }
                return true;
            }
        }
    }
    
    return false; 
}

bool UserManager::ModifyTeamName(int32_t userid, const std::string& newteamname, const std::string& oldteamname)
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
    osSql << "UPDATE t_user_relationship SET f_user1_teamname='"
        << newteamname << "' WHERE f_user_id1=" << userid
        << " AND f_user1_teamname='" << oldteamname << "'";


    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "ModifyTeamName error, sql=" << osSql.str() 
                  << ", userid=" << userid
                  << ", newteamname=" << newteamname
                  << ", oldteamname=" << oldteamname;
        return false;
    }

    osSql.str("");

    osSql << "UPDATE t_user_relationship SET f_user2_teamname='"
        << newteamname << "' WHERE f_user_id2=" << userid
        << " AND f_user2_teamname='" << oldteamname << "'";

    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "ModifyTeamName error, sql=" << osSql.str()
                  << ", userid=" << userid
                  << ", newteamname=" << newteamname
                  << ", oldteamname=" << oldteamname;
        return false;
    }

     {
         std::lock_guard<std::mutex> guard(m_mutex);
         for (auto& iter : m_allCachedUsers)
         {
             if (iter.userid == userid)
             {
                 for (auto& iter2 : iter.friends)
                 {
                     if (iter2.teamname == oldteamname)
                         iter2.teamname = newteamname;
                 }
                 return true;
             }
         }
     }

    return true;
}

bool UserManager::UpdateMarknameInDb(int32_t userid, int32_t friendid, const std::string& newmarkname)
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
    if (userid < friendid)
    {
        osSql << "UPDATE t_user_relationship SET f_user1_markname='"
            << newmarkname << "' WHERE f_user_id1="
            << userid << " AND f_user_id2=" << friendid;
    }
    else
    {
        osSql << "UPDATE t_user_relationship SET f_user2_markname='"
            << newmarkname << "' WHERE f_user_id2="
            << userid << " AND f_user_id1=" << friendid;
    }
    
    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "Update Markname error, sql=" << osSql.str();
        return false;
    }

    LOG_INFO << "update markname successfully, userid: " << userid << ", friendid: " << friendid << ", sql : " << osSql.str();

    //TODO: 重复的代码，需要去掉
    std::lock_guard<std::mutex> guard(m_mutex);
    std::set<FriendInfo> friends;
    for (auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            for (auto& iter2 : iter.friends)
            {
                if (iter2.friendid == friendid)
                {
                    iter2.markname = newmarkname;
                    return true;
                }
            }          
        }
    }

    LOG_ERROR << "Failed to update markname, find no exsit user in memory error, m_allCachedUsers.size(): " << m_allCachedUsers.size() 
              << ", userid: " << userid << ", friendid: " << friendid << ", sql : " << osSql.str();

    return false;
}

bool UserManager::MoveFriendToOtherTeam(int32_t userid, int32_t friendid, const std::string& newteamname)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_ERROR << "UserManager::Initialize db failed, please check params: dbserver=" << m_strDbServer
                  << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
                  << ", dbname=" << m_strDbName << ", userid=" << userid << ", friendid=" << friendid
                  << ", newteamname=" << newteamname;
        return false;
    }

    std::ostringstream osSql;
    if (userid < friendid)
    {
        osSql << "UPDATE t_user_relationship SET f_user1_teamname='"
              << newteamname << "' WHERE f_user_id1="
              << userid << " AND f_user_id2=" << friendid;
    }
    else
    {
        osSql << "UPDATE t_user_relationship SET f_user2_teamname='"
              << newteamname << "' WHERE f_user_id1="
              << friendid << " AND f_user_id2=" << userid;
    }

    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_ERROR << "MoveFriendToOtherTeam, sql=" << osSql.str() 
                  << ", userid=" << userid << ", friendid=" << friendid
                  << ", newteamname=" << newteamname;
        return false;
    }

    LOG_INFO << "MoveFriendToOtherTeam db operation successfully, userid: " << userid << ", friendid: " << friendid << ", sql : " << osSql.str();

    //改变内存中用户的分组信息
    User* u = NULL;
    if (!GetUserInfoByUserId(userid, u) || u == NULL)
    {
        LOG_ERROR << "MoveFriendToOtherTeam memory operation error, userid: " << userid << ", friendid: " << friendid;
        return false;
    }

    for (auto& f : u->friends)
    {
        if (f.friendid == friendid)
        {
            f.teamname = newteamname;
            return true;
        }
    }

    return false;
}

bool UserManager::AddGroup(const char* groupname, int32_t ownerid, int32_t& groupid)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_ERROR << "UserManager::AddGroup failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName << ", groupname=" << groupname << ", ownerid=" << ownerid;
        return false;
    }

    ++m_baseGroupId;
    char sql[256] = { 0 };
    snprintf(sql, 256, "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_owner_id, f_register_time) VALUES(%d, '%d', '%s', '', %d,  NOW())", m_baseGroupId, m_baseGroupId, groupname, ownerid);
    if (!pConn->Execute(sql))
    {
        LOG_ERROR << "insert group error, sql=" << sql;
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

#ifdef FXN_VERSION
bool UserManager::InsertDeviceInfo(int32_t userid, int32_t deviceid, int32_t classtype, int64_t uploadtime, const std::string& deviceinfo)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::InsertDeviceInfo failed, please check params: dbserver=" << m_strDbServer
            << ", dbusername=" << m_strDbUserName << ", dbpassword" << m_strDbPassword
            << ", dbname=" << m_strDbName;
        return false;
    }

    if (uploadtime <= 0)
        uploadtime = time(NULL);

    std::ostringstream osSql;
    osSql << "INSERT INTO t_device(f_user_id, f_deviceid, f_classtype, f_deviceinfo, f_upload_time, f_create_time) VALUES("
          << userid << ", "
          << deviceid << ", "
          << classtype << ", '"
          << deviceinfo << "',  FROM_UNIXTIME("
          << uploadtime << ", '%Y-%m-%d %H:%i:%S'), "
          << "NOW())";
    if (!pConn->Execute(osSql.str().c_str()))
    {
        LOG_WARN << "insert group error, sql=" << osSql.str();
        return false;
    }

    return true;
}
#endif

bool UserManager::SaveChatMsgToDb(int32_t senderid, int32_t targetid, const std::string& chatmsg)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_ERROR << "UserManager::SaveChatMsgToDb failed, please check params: dbserver=" << m_strDbServer
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

bool UserManager::GetUserInfoByUserId(int32_t userid, User*& u)
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

bool UserManager::GetFriendInfoByUserId(int32_t userid, std::list<User>& friends)
{
    std::list<FriendInfo> friendInfo;
    std::lock_guard<std::mutex> guard(m_mutex);
    //先找到friends的id列表
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            friendInfo = iter.friends;
            break;
        }
    }

    //再通过每个friendid找到对应的User集合
    //TODO: 这种算法效率太低
    for (const auto& iter : friendInfo)
    {
        User u;
        for (const auto& iter2 : m_allCachedUsers)
        {
            if (iter2.userid == iter.friendid)
            {
                u = iter2;
                friends.push_back(u);
                break;
            }
        }
    }

    return true;
}

bool UserManager::GetFriendMarknameByUserId(int32_t userid1, int32_t friendid, std::string& markname)
{
    std::list<FriendInfo> friendInfo;
    std::lock_guard<std::mutex> guard(m_mutex);
    //先找到friends的id列表
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid1)
        {
            friendInfo = iter.friends;
            for (const auto& iter2 : friendInfo)
            {
                if (iter2.friendid == friendid)
                {
                    markname = iter2.markname;
                    return true;
                }
            }         
        }
    }

    return false;
}

bool UserManager::GetTeamInfoByUserId(int32_t userid, std::string& teaminfo)
{
    std::set<int32_t> friendsId;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid == userid)
        {
            teaminfo = iter.teaminfo;
            return true;
        }
    }
    
    return false;
}

bool UserManager::LoadRelationshipFromDb(int32_t userid, std::list<FriendInfo>& r)
{
    std::unique_ptr<CDatabaseMysql> pConn;
    pConn.reset(new CDatabaseMysql());
    if (!pConn->Initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    {
        LOG_FATAL << "UserManager::LoadRelationhipFromDb failed, please check params";
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "SELECT f_user_id1, f_user_id2, f_user1_markname, f_user2_markname, f_user1_teamname, f_user2_teamname FROM t_user_relationship WHERE f_user_id1 = %d OR f_user_id2 = %d ", userid, userid);
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

        int32_t friendid1 = pRow[0].GetInt32();
        int32_t friendid2 = pRow[1].GetInt32();
        string markname1 = pRow[2].GetCppString();
        string markname2 = pRow[3].GetCppString();  
        string teamname1 = pRow[4].GetCppString();
        string teamname2 = pRow[5].GetCppString();
        if (teamname1.empty())
            teamname1 = DEFAULT_TEAMNAME;
        if (teamname2.empty())
            teamname2 = DEFAULT_TEAMNAME;
        FriendInfo fi;
        if (friendid1 == userid)
        {
            fi.friendid = friendid2;
            fi.markname = markname1;
            fi.teamname = teamname1;
            r.emplace_back(fi);
            //LOG_INFO << "userid=" << userid << ", friendid=" << friendid2;
        }
        else
        {
            fi.friendid = friendid1;
            fi.markname = markname2;
            fi.teamname = teamname2;
            r.emplace_back(fi);
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
