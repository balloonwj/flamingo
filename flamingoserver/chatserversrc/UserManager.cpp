/**
 *  管理所有的用户信息，初始信息从数据库中加载, UserManager.h
 *  zhangyl 2017.03.15
 **/
#include "UserManager.h"
#include <memory>
#include <sstream>
#include <unordered_map>
#include <stdio.h>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "AccountName.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

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
    //从数据库中加载所有用户信息
    if (!loadUsersFromDb())
        return false;

    //加载所有好友关系：一次查全表 + 内存分发，替代旧的「逐用户各开一条远程
    //连接查询」做法（用户多时该循环会把启动拖死几分钟）。失败仅告警、不阻断
    //启动，与旧实现一致（旧循环 continue 容忍单用户失败、从不因此 return false）。
    if (!loadAllRelationshipsFromDb())
        LOGE("UserManager::init: load all relationships failed, friend lists may be incomplete");

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

//数据库里面互为好友的两个人id，小者在先，大者在后
bool UserManager::makeFriendRelationshipInDB(int32_t smallUserid, int32_t greaterUserid)
{
    if (smallUserid == greaterUserid)
        return false;

    if (smallUserid > greaterUserid)
    {
        int32_t tmp = greaterUserid;
        greaterUserid = smallUserid;
        smallUserid = tmp;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::LoadUsersFromDb failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    char sql[512] = { 0 };
    snprintf(sql, 512, "INSERT INTO t_user_relationship(f_user_id1, f_user_id2, f_user1_teamname, f_user2_teamname) VALUES(%d, %d, '%s', '%s')", smallUserid, greaterUserid, DEFAULT_TEAMNAME, DEFAULT_TEAMNAME);
    if (!pConn->execute(sql))
    {
        LOGE("make relationship error, sql: %s, smallUserid: %s, greaterUserid: %s", sql, smallUserid , greaterUserid);
        return false;
    }
    
    //if (!AddFriendToUser(smallUserid, greaterUserid))
    //{
    //    LOGE << "make relationship error, smallUserid=" << smallUserid << ", greaterUserid=" << greaterUserid;
    //    return false;
    //}

    return true;
}

bool UserManager::releaseFriendRelationshipInDBAndMemory(int32_t smallUserid, int32_t greaterUserid)
{
    if(smallUserid == greaterUserid)
        return false;

    if (smallUserid > greaterUserid)
    {
        int32_t tmp = greaterUserid;
        greaterUserid = smallUserid;
        smallUserid = tmp;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::LoadUsersFromDb failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "DELETE FROM t_user_relationship WHERE f_user_id1 = %d AND f_user_id2 = %d", smallUserid, greaterUserid);
    if (!pConn->execute(sql))
    {
        LOGE("release relationship error, sql: %s, smallUserid: %d, , greaterUserid: %d", sql, smallUserid, greaterUserid);
        return false;
    }

    if (!deleteFriendToUser(smallUserid, greaterUserid))
    {
        LOGE("delete relationship error, smallUserid: %d, , greaterUserid: %d", smallUserid, greaterUserid);
        return false;
    }

    return true;
}

bool UserManager::updateUserRelationshipInMemory(int32_t userid, int32_t target, FRIEND_OPERATION operation)
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

bool UserManager::addFriendToUser(int32_t userid, int32_t friendid)
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

bool UserManager::deleteFriendToUser(int32_t userid, int32_t friendid)
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

bool UserManager::isFriend(int32_t userid, int32_t friendid)
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

bool UserManager::updateUserTeamInfoInDbAndMemory(int32_t userid, const std::string& newteaminfo)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_user SET f_teaminfo='"
        << newteaminfo << "' WHERE f_user_id="
        << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("Update Team Info error, sql: %s", osSql.str().c_str());
        return false;
    }

    LOGI("update user teaminfo successfully, userid: %d, sql: %s", userid, osSql.str().c_str());

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

    LOGE("Failed to update user teaminfo to db, find no exsit user in memory error, m_allCachedUsers.size(): %d, userid: %d, sql: %s", m_allCachedUsers.size(), userid, osSql.str().c_str());

    return false;
}

bool UserManager::deleteTeam(int32_t userid, const std::string& deletedteamname)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_user_relationship SET f_user1_teamname='"
          << DEFAULT_TEAMNAME << "' WHERE f_user_id1=" << userid 
          << " AND f_user1_teamname='" << deletedteamname << "'";
    

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("DeleteTeam error, sql: %s, userid: %d, deletedteamname: %s", osSql.str().c_str(), userid, deletedteamname.c_str());
        return false;
    }

    osSql.str("");

    osSql << "UPDATE t_user_relationship SET f_user2_teamname='"
        << DEFAULT_TEAMNAME << "' WHERE f_user_id2=" << userid
        << " AND f_user2_teamname='" << deletedteamname << "'";

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("DeleteTeam error, sql: %s, userid: %d, deletedteamname: %s", osSql.str().c_str(), userid, deletedteamname.c_str());
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

bool UserManager::modifyTeamName(int32_t userid, const std::string& newteamname, const std::string& oldteamname)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_user_relationship SET f_user1_teamname='"
        << newteamname << "' WHERE f_user_id1=" << userid
        << " AND f_user1_teamname='" << oldteamname << "'";


    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ModifyTeamName error, sql: %s, userid: %d, newteamname: %s, oldteamname: %s", osSql.str().c_str(), userid, newteamname.c_str(), oldteamname.c_str());
        return false;
    }

    osSql.str("");

    osSql << "UPDATE t_user_relationship SET f_user2_teamname='"
        << newteamname << "' WHERE f_user_id2=" << userid
        << " AND f_user2_teamname='" << oldteamname << "'";

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ModifyTeamName error, sql: %s, userid: %d, newteamname: %s, oldteamname: %s", osSql.str().c_str(), userid, newteamname.c_str(), oldteamname.c_str());
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

bool UserManager::updateMarknameInDb(int32_t userid, int32_t friendid, const std::string& newmarkname)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed, please check params: db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
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
    
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("Update Markname error, sql: %s", osSql.str().c_str());
        return false;
    }

    LOGI("update markname successfully, userid: %d, friendid: %d, sql: %s", userid, friendid, osSql.str().c_str());

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

    LOGE("Failed to update markname, find no exsit user in memory error, m_allCachedUsers.size(): %d, userid: %d, friendid: %d, sql: %s",
         m_allCachedUsers.size(), userid, friendid, osSql.str().c_str());

    return false;
}

bool UserManager::updateFriendTagsInDb(int32_t userid, int32_t friendid, const std::string& tagsJson)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::updateFriendTagsInDb db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //tagsJson 为 JSON 文本，含引号、可能含中文，须经 escapeString 转义
    std::vector<char> escTags(tagsJson.size() * 2 + 1, '\0');
    pConn->escapeString(&escTags[0], tagsJson.c_str(), static_cast<uint32_t>(tagsJson.size()));

    //标签是单向的：与备注名同规则，按 userid 与 friendid 的大小决定写哪一侧的列
    std::ostringstream osSql;
    if (userid < friendid)
    {
        osSql << "UPDATE t_user_relationship SET f_user1_tags='" << &escTags[0]
              << "' WHERE f_user_id1=" << userid << " AND f_user_id2=" << friendid;
    }
    else
    {
        osSql << "UPDATE t_user_relationship SET f_user2_tags='" << &escTags[0]
              << "' WHERE f_user_id2=" << userid << " AND f_user_id1=" << friendid;
    }

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("UserManager::updateFriendTagsInDb error, userid: %d, friendid: %d", userid, friendid);
        return false;
    }

    LOGI("update friend tags ok, userid: %d, friendid: %d", userid, friendid);
    return true;
}

bool UserManager::moveFriendToOtherTeam(int32_t userid, int32_t friendid, const std::string& newteamname)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::Initialize db failed, please check params: db: %s, userid: %d, friendid: %d, newteamname: %s", Singleton<CDbConfig>::Instance().describe().c_str(), userid, friendid, newteamname.c_str());
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

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MoveFriendToOtherTeam, sql: %s, userid: %d, friendid: %d, newteamname: %s", osSql.str().c_str(), userid, friendid, newteamname.c_str());
        return false;
    }

    LOGI("MoveFriendToOtherTeam db operation successfully, userid: %d, friendid: %d, sql: %s" , userid, friendid, osSql.str().c_str());

    //改变内存中用户的分组信息
    User* u = NULL;
    if (!getUserInfoByUserId(userid, u) || u == NULL)
    {
        LOGE("MoveFriendToOtherTeam memory operation error, userid: %d, friendid: %d" , userid, friendid);
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

bool UserManager::getFriendInfoByUserId(int32_t userid, std::list<User>& friends)
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

bool UserManager::getFriendMarknameByUserId(int32_t userid1, int32_t friendid, std::string& markname)
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

bool UserManager::getFriendMarknames(int32_t userid, std::map<int32_t, std::string>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    for (const auto& iter : m_allCachedUsers)
    {
        if (iter.userid != userid)
            continue;

        //找到本人这条就把好友表整个搬进映射，随即返回——userid 在缓存里唯一，
        //不必把剩下的用户也扫完。
        for (const auto& f : iter.friends)
        {
            out[f.friendid] = f.markname;
        }
        return true;
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

int64_t UserManager::addFriendRequest(int32_t fromUserid, int32_t toUserid,
                                      const std::string& verifymsg, const std::string& source,
                                      int64_t createTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("UserManager::addFriendRequest db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_friend_request(f_from_userid, f_to_userid, f_verify_msg, f_source, f_status, f_create_time, f_handle_time) "
        "VALUES(%d, %d, '%s', '%s', 0, %lld, 0)",
        fromUserid, toUserid, verifymsg.c_str(), source.c_str(), (long long)createTime);
    if (!pConn->execute(sql))
    {
        LOGE("addFriendRequest insert error, sql: %s", sql);
        return 0;
    }

    //取回自增主键 f_id 作为申请ID（须与 INSERT 同一连接）
    int64_t requestId = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            requestId = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }

    return requestId;
}

bool UserManager::getFriendRequestById(int64_t requestId, FriendRequestRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_id, f_from_userid, f_to_userid, f_verify_msg, f_source, f_status, f_create_time, f_handle_time "
        "FROM t_friend_request WHERE f_id = %lld", (long long)requestId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.id         = static_cast<int64_t>(pRow[0].getUInt64());
        out.fromuserid = pRow[1].getInt32();
        out.touserid   = pRow[2].getInt32();
        out.verifymsg  = pRow[3].getCppString();
        out.source     = pRow[4].getCppString();
        out.status     = pRow[5].getInt32();
        out.createtime = static_cast<int64_t>(pRow[6].getUInt64());
        out.handletime = static_cast<int64_t>(pRow[7].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool UserManager::updateFriendRequestStatus(int64_t requestId, int32_t status, int64_t handleTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_friend_request SET f_status = %d, f_handle_time = %lld WHERE f_id = %lld",
        status, (long long)handleTime, (long long)requestId);
    return pConn->execute(sql);
}

bool UserManager::getPendingFriendRequests(int32_t toUserid, std::vector<FriendRequestRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    char sql[512] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_id, f_from_userid, f_to_userid, f_verify_msg, f_source, f_status, f_create_time, f_handle_time "
        "FROM t_friend_request WHERE f_to_userid = %d AND f_status = 0 ORDER BY f_create_time DESC",
        toUserid);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        FriendRequestRecord rec;
        rec.id         = static_cast<int64_t>(pRow[0].getUInt64());
        rec.fromuserid = pRow[1].getInt32();
        rec.touserid   = pRow[2].getInt32();
        rec.verifymsg  = pRow[3].getCppString();
        rec.source     = pRow[4].getCppString();
        rec.status     = pRow[5].getInt32();
        rec.createtime = static_cast<int64_t>(pRow[6].getUInt64());
        rec.handletime = static_cast<int64_t>(pRow[7].getUInt64());
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
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

bool UserManager::getTeamInfoByUserId(int32_t userid, std::string& teaminfo)
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

bool UserManager::loadRelationshipFromDb(int32_t userid, std::list<FriendInfo>& r)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGF("UserManager::LoadRelationhipFromDb failed, please check params");
        return false;
    }

    char sql[256] = { 0 };
    snprintf(sql, 256, "SELECT f_user_id1, f_user_id2, f_user1_markname, f_user2_markname, f_user1_teamname, f_user2_teamname FROM t_user_relationship WHERE f_user_id1 = %d OR f_user_id2 = %d ", userid, userid);
    QueryResult* pResult = pConn->query(sql);
    if (NULL == pResult)
    {
        LOGI("UserManager::Query error, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        int32_t friendid1 = pRow[0].getInt32();
        int32_t friendid2 = pRow[1].getInt32();
        string markname1 = pRow[2].getCppString();
        string markname2 = pRow[3].getCppString();  
        string teamname1 = pRow[4].getCppString();
        string teamname2 = pRow[5].getCppString();
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
            LOGI("userid=%d, friendid=%d", userid, friendid2);
        }
        else
        {
            fi.friendid = friendid1;
            fi.markname = markname2;
            fi.teamname = teamname2;
            r.emplace_back(fi);
            LOGI("userid=%d, friendid=%d", userid, friendid2);            
        }
       
        if (!pResult->nextRow())
        {
            break;
        }
    }

    pResult->endQuery();

    delete pResult;

    return true;
}

//一次性把整张 t_user_relationship 读入内存，按 userid 分发到各普通用户的好友
//列表。相比「逐用户各开一条远程连接查询」的旧做法，这里只用 1 条连接、1 次
//查询，把 O(N) 次远程往返压成 O(1)，启动从几分钟降到几十毫秒。
//  群账号（userid >= GROUPID_BOUBDARY）与服务号（usertype == 1）不参与好友
//  关系，不为其建索引，相关行直接跳过。
//  返回：成功 true；连接失败 / 查询失败 false。
bool UserManager::loadAllRelationshipsFromDb()
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGF("UserManager::loadAllRelationshipsFromDb failed to connect db, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //只为普通用户建立 userid -> User* 索引（普通用户 = userid 在群号边界以下
    //且 usertype 为 0）。群账号、服务号不收好友关系，不入索引。
    //m_allCachedUsers 是 std::list，元素地址稳定，存指针安全；本函数仅在启动
    //单线程阶段调用，且只往各 User 的 friends 追加、不改动链表本身。
    std::unordered_map<int32_t, User*> normalUserIndex;
    for (auto& u : m_allCachedUsers)
    {
        if (u.userid < GROUPID_BOUBDARY && u.usertype == 0)
            normalUserIndex[u.userid] = &u;
    }

    QueryResult* pResult = pConn->query("SELECT f_user_id1, f_user_id2, f_user1_markname, f_user2_markname, f_user1_teamname, f_user2_teamname FROM t_user_relationship");
    if (NULL == pResult)
    {
        //空表与出错此处无法区分（受 QueryResult 接口所限，与 loadRelationshipFromDb 同）。
        LOGI("UserManager::loadAllRelationshipsFromDb query returns empty or error, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    int32_t rowCount = 0;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        int32_t userid1 = pRow[0].getInt32();
        int32_t userid2 = pRow[1].getInt32();
        std::string markname1 = pRow[2].getCppString();
        std::string markname2 = pRow[3].getCppString();
        std::string teamname1 = pRow[4].getCppString();
        std::string teamname2 = pRow[5].getCppString();
        if (teamname1.empty())
            teamname1 = DEFAULT_TEAMNAME;
        if (teamname2.empty())
            teamname2 = DEFAULT_TEAMNAME;

        //userid1 的好友是 userid2，备注名 / 分组取 user1 一侧的列。
        auto it1 = normalUserIndex.find(userid1);
        if (it1 != normalUserIndex.end())
        {
            FriendInfo fi;
            fi.friendid = userid2;
            fi.markname = markname1;
            fi.teamname = teamname1;
            it1->second->friends.emplace_back(fi);
        }

        //userid2 的好友是 userid1，备注名 / 分组取 user2 一侧的列。
        auto it2 = normalUserIndex.find(userid2);
        if (it2 != normalUserIndex.end())
        {
            FriendInfo fi;
            fi.friendid = userid1;
            fi.markname = markname2;
            fi.teamname = teamname2;
            it2->second->friends.emplace_back(fi);
        }

        ++rowCount;
        if (!pResult->nextRow())
            break;
    }

    pResult->endQuery();

    delete pResult;

    LOGI("UserManager::loadAllRelationshipsFromDb done, relationship rows: %d, normal users indexed: %d",
         rowCount, static_cast<int>(normalUserIndex.size()));

    return true;
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

    //3. 用户在缓存中已存在：原地更新标量字段，保留其好友列表。
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

    //4. 用户在库中存在、缓存中没有（admin 新建的员工）：连带加载好友关系后加入缓存。
    loadRelationshipFromDb(userid, dbUser.friends);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_allCachedUsers.push_back(dbUser);
    }
    LOGI("UserManager::reloadUserCache: user %d inserted into cache (new)", userid);
    return true;
}
