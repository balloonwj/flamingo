/**
 *  群内关注名单存储管理器实现，GroupFollowManager.cpp
 *  设计与约定见 GroupFollowManager.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "GroupFollowManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h" //CDbTransaction，整份覆盖名单时保证删与插一起成败
#include "../dbapi/SqlDialect.h"   //UpsertClause
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "GroupManager.h"          //isGroupMember，校验本人与名单里的人是否在群里
#include "Protocol.h"              //error_code

//t_group_follow 的 UPSERT 冲突列。必须与该表主键的列完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define GROUP_FOLLOW_CONFLICT_COLUMNS   "f_userid, f_groupid, f_target_userid"

GroupFollowManager::GroupFollowManager()
{
}

GroupFollowManager::~GroupFollowManager()
{
}

int GroupFollowManager::checkTargets(int32_t userid, int64_t groupid, const std::vector<int32_t>& targets)
{
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //名单只对本人所在的群有意义
    if (!groupMgr.isGroupMember(groupid, userid))
        return error_code_not_group_member;

    //上限：客户端选人时按同一个值限制，超出只可能是异常请求
    if (targets.size() > kGroupFollowMax)
    {
        LOGI("group follow rejected by limit, userid: %d, groupid: %lld, count: %u, limit: %u",
             userid, (long long)groupid, static_cast<unsigned>(targets.size()),
             static_cast<unsigned>(kGroupFollowMax));
        return error_code_bad_request;
    }

    //只能关注本群的其他成员：自己的发言本来就不提醒，非成员的发言不会出现在群里
    for (size_t i = 0; i < targets.size(); ++i)
    {
        if (targets[i] == userid || !groupMgr.isGroupMember(groupid, targets[i]))
        {
            LOGI("group follow rejected, invalid target, userid: %d, groupid: %lld, target: %d",
                 userid, (long long)groupid, targets[i]);
            return error_code_bad_request;
        }
    }
    return error_code_ok;
}

bool GroupFollowManager::getFollowsAll(int32_t userid, std::vector<GroupFollowList>& out)
{
    out.clear();
    if (userid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFollowManager::getFollowsAll db init failed, userid: %d, db: %s",
             userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //命中主键前缀 f_userid；按 (群, 成员) 排序后同一个群的行连续返回，逐行归组即可
    std::ostringstream osSql;
    osSql << "SELECT f_groupid, f_target_userid FROM t_group_follow WHERE f_userid = " << userid
          << " ORDER BY f_groupid ASC, f_target_userid ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //dbapi 在结果集为空时也返回 NULL，与查询失败无从区分；名单取不到不该连累整个
        //会话列表应答，一并按「没有名单」处理
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        const int64_t groupid = static_cast<int64_t>(pRow[0].getUInt64());
        const int32_t target  = static_cast<int32_t>(pRow[1].getUInt64());
        if (out.empty() || out.back().groupid != groupid)
        {
            GroupFollowList item;
            item.groupid = groupid;
            out.push_back(item);
        }
        out.back().targets.push_back(target);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupFollowManager::getFollows(int32_t userid, int64_t groupid, std::vector<int32_t>& out)
{
    out.clear();
    if (userid <= 0 || groupid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFollowManager::getFollows db init failed, userid: %d, groupid: %lld, db: %s",
             userid, (long long)groupid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_target_userid FROM t_group_follow WHERE f_userid = " << userid
          << " AND f_groupid = " << groupid << " ORDER BY f_target_userid ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return true;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        out.push_back(static_cast<int32_t>(pRow[0].getUInt64()));

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupFollowManager::setFollows(int32_t userid, int64_t groupid,
                                    const std::vector<int32_t>& targets, int64_t now)
{
    if (userid <= 0 || groupid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFollowManager::setFollows db init failed, userid: %d, groupid: %lld, db: %s",
             userid, (long long)groupid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //删与插必须一起成败：中途失败若不回滚，用户看到的就是「改名单把原来的也弄丢了」。
    //各条错误路径直接 return，守卫析构时自动回滚
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("GroupFollowManager::setFollows begin transaction error, userid: %d, groupid: %lld",
             userid, (long long)groupid);
        return false;
    }

    std::ostringstream osDelete;
    osDelete << "DELETE FROM t_group_follow WHERE f_userid = " << userid << " AND f_groupid = " << groupid;
    if (!pConn->execute(osDelete.str().c_str()))
    {
        LOGE("GroupFollowManager::setFollows delete error, userid: %d, groupid: %lld",
             userid, (long long)groupid);
        return false;
    }

    for (size_t i = 0; i < targets.size(); ++i)
    {
        //调用方已去重；仍带 UPSERT 子句，名单里万一混进重复的 userid 时改为更新而不是报主键冲突
        std::ostringstream osInsert;
        osInsert << "INSERT INTO t_group_follow(f_userid, f_groupid, f_target_userid, f_create_time) VALUES("
                 << userid << ", " << groupid << ", " << targets[i] << ", " << now << ")"
                 << dbapi::UpsertClause(GROUP_FOLLOW_CONFLICT_COLUMNS)
                 << "f_create_time = " << now;
        if (!pConn->execute(osInsert.str().c_str()))
        {
            LOGE("GroupFollowManager::setFollows insert error, userid: %d, groupid: %lld, target: %d",
                 userid, (long long)groupid, targets[i]);
            return false;
        }
    }

    //提交失败时守卫仍持有该事务，由它的析构补一次回滚
    if (!transaction.Commit())
    {
        LOGE("GroupFollowManager::setFollows commit error, userid: %d, groupid: %lld",
             userid, (long long)groupid);
        return false;
    }

    LOGI("set group follows ok, userid: %d, groupid: %lld, count: %u",
         userid, (long long)groupid, static_cast<unsigned>(targets.size()));
    return true;
}
