/**
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/
#include <memory>
#include <sstream>
#include <stdio.h>
#include "../database/databasemysql.h"
#include "../base/logging.h"
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

        if (!MakeUpTeamInfo(iter, iter.friends))
        {
            LOG_ERROR << "MakeUpTeamInfo error, userid=" << iter.userid;
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

bool UserManager::MakeUpTeamInfo(User& u, const set<int32_t>& friends)
{
    //如果已经存在分组信息，则不设置默认的
    if (!u.teaminfo.empty())
        return true;
    
    //拼装默认的分组信息
    /*
    [
    {
    "teamindex": 0,
    "teamname": "我的好友",
    "members": [
    {
    "userid": 1,
    "markname": "张某某"
    },
    {
    "userid": 2,
    "markname": "张xx"
    }
    ]
    }
    ]
    */
    std::string strUserInfo;
    ostringstream osTeamInfo;
    osTeamInfo << "[{\"teamindex\": 0, \"teamname\" : \"My Friends\", \"members\" : ";
    for (const auto& iter : friends)
    {
        /*
        {"code": 0, "msg": "ok", "userinfo":[{"userid": 1,"username":"qqq,
        "nickname":"qqq, "facetype": 0, "customface":"", "gender":0, "birthday":19900101,
        "signature":", "address": "", "phonenumber": "", "mail":", "clienttype": 1, "status":1"]}
        */
        ostringstream osSingleUserInfo;
        osSingleUserInfo << "{\"userid\": " << iter << ",\"markname\":\"" << "\"}";

        strUserInfo += osSingleUserInfo.str();
        strUserInfo += ",";
    }
    //去掉最后多余的逗号
    strUserInfo = strUserInfo.substr(0, strUserInfo.length() - 1);
    std::ostringstream os;
    os << osTeamInfo.str() << "[" << strUserInfo << "]}]";
    u.teaminfo = os.str();

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

    //新注册的用户分组信息由于没有初始化，这里初始化一下
    if (!MakeUpTeamInfo(u, u.friends))
    {
        LOG_ERROR << "MakeUpTeamInfo error, userid=" << u.userid;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(u);
    }

    return true;
}

//数据库里面互为好友的两个人id，小者在先，大者在后
bool UserManager::MakeFriendRelationship(int32_t smallUserid, int32_t greaterUserid)
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

bool UserManager::UpdateUserTeamInfo(int32_t userid, int32_t target, FRIEND_OPERATION operation)
{
    std::string* pTeaminfo = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto& iter : m_allCachedUsers)
        {
            if (iter.userid == userid)
            {
                pTeaminfo = &iter.teaminfo;
                break;
            }
        }
    }
    
    if (pTeaminfo == NULL)
    {
        LOG_ERROR << "teaminfo is NULL while they must not be, userid=" << userid 
                  << ", teaminfo=" << pTeaminfo;
        return false;
    }
    else if (pTeaminfo->empty())
    {
        //非群组账号的teaminfo不能为空
        if (userid < GROUPID_BOUBDARY)
        {
            LOG_ERROR << "teaminfo is empty while they must not be, userid=" << userid
                << ", teaminfo=" << *pTeaminfo;
            return false;
        }
        else
            return true;
    }
        
    /*
    [
        {
            "teamindex": 0,
            "teamname" : "我的好友",
            "members" : [
            {
                "userid": 1,
                "markname" : "张某某"
            },
            {
                "userid": 2,
                "markname" : "张xx"
            }
                ]
        },
        {
            "teamindex": 1,
            "teamname" : "我的朋友",
            "members" : [
            {
                "userid": 3,
                "markname" : "张某某"
            },
            {
                "userid": 4,
                "markname" : "张xx"
            }
            ]
        }
    ]
    */

    Json::Reader JsonReader(Json::Features::strictMode());
    Json::Value JsonRoot;
    if (!JsonReader.parse(*pTeaminfo, JsonRoot) || !JsonRoot.isArray())
    {
        LOG_ERROR << "parse teaminfo json failed, userid: " << userid << ", teaminfo: " << *pTeaminfo;
        return false;
    }

    for (size_t i = 0; i < JsonRoot.size(); ++i)
    {
        //将添加的好友，默认放到第一个分组里面去
        if (operation == FRIEND_OPERATION_ADD)
        {
            if (JsonRoot[i]["teamindex"].isInt() && JsonRoot[i]["teamindex"].asInt() == 0)
            {
                if (JsonRoot[i]["members"].isArray())
                {
                    size_t count = JsonRoot[i]["members"].size();
                    JsonRoot[i]["members"][count]["userid"] = target;
                    JsonRoot[i]["members"][count]["markname"] = "";
                    Json::FastWriter writer;
                    *pTeaminfo = writer.write(JsonRoot);
                    if (UpdateUserTeamInfoInDb(userid, *pTeaminfo))
                        return true;
                    return false;
                }
            }
        }
        else if (operation == FRIEND_OPERATION_DELETE)
        {
            for (size_t j = 0; j < JsonRoot[i]["members"].size(); ++j)
            {
                Json::Value& node = JsonRoot[i]["members"][j];
                if (node["userid"].isInt() && node["userid"].asInt() == target)
                {
                    JsonRoot[i]["members"].removeArrayElement(j);
                    Json::FastWriter writer;
                    *pTeaminfo = writer.write(JsonRoot);
                    if(UpdateUserTeamInfoInDb(userid, *pTeaminfo))
                        return true;
                    return false;
                }
            }
             
        }
    }// end for-loop
   
    return false;
}

bool UserManager::UpdateUserTeamInfoInDb(int32_t userid, const std::string& newteaminfo)
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
