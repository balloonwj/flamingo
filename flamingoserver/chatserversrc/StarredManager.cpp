/**
 *  「特别关注」存储管理器实现，StarredManager.cpp
 *  设计与约定见 StarredManager.h。
 *  balloonwj@qq.com   2026-08-12
 */
#include "StarredManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"      //error_code

StarredManager::StarredManager()
{
}

StarredManager::~StarredManager()
{
}

bool StarredManager::getStarred(int64_t userid, std::vector<int64_t>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("StarredManager::getStarred db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //按标记时间倒序，最近标记的排最前。f_target 作次级排序键，是为了让"同一毫秒内
    //连着标记的几个人"每次查询的顺序都一样，否则名单顺序会莫名其妙地互换。
    std::ostringstream osSql;
    osSql << "SELECT f_target FROM t_starred WHERE f_userid = " << userid
          << " ORDER BY f_create_time DESC, f_target ASC";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //查询失败与"一个都没关注"是两回事：前者要让调用方知道，后者是正常情况。
        //dbapi 在结果集为空时同样返回 NULL，故这里只能一并按"没有关注"处理，
        //但仍返回 true —— 名单拉不到不该连累整个通讯录应答。
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        out.push_back(static_cast<int64_t>(pRow[0].getUInt64()));

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

int StarredManager::countStarred(int64_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("StarredManager::countStarred db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return -1;
    }

    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_starred WHERE f_userid = " << userid;

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //COUNT(*) 恒有一行结果，取不到说明查询本身出了问题，据实返回 -1。
        return -1;
    }

    int count = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = pRow[0].getInt32();
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

bool StarredManager::setStarred(int64_t userid, int64_t target, int starred,
                                int64_t now, int& outErrorCode)
{
    outErrorCode = error_code_ok;

    //取值域兜底。正常流程里 StarredProtocol::parseSetStarredRequest 已经拦过一道，
    //这里再判一次是防着日后从别的路径（如 admin 控制台）调进来。
    if (userid <= 0 || target <= 0)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }
    if (starred != 0 && starred != 1)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }
    //不能关注自己：自己的上下线自己最清楚，给自己弹提示没有意义。
    if (userid == target)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("StarredManager::setStarred db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    if (starred == 0)
    {
        //取消关注：删掉即可。删到 0 行也算成功 —— 用户连点两下"取消特别关注"
        //不该在第二下收到报错。取消不受人数上限约束，否则存量超限的用户
        //连"取消几个"都做不到。
        std::ostringstream osSql;
        osSql << "DELETE FROM t_starred WHERE f_userid = " << userid
              << " AND f_target = " << target;
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("StarredManager::setStarred delete error, userid: %lld, target: %lld",
                 (long long)userid, (long long)target);
            outErrorCode = error_code_server_internal;
            return false;
        }

        LOGI("remove starred ok, userid: %lld, target: %lld",
             (long long)userid, (long long)target);
        return true;
    }

    //标记关注。先看这一条是不是已经存在：已存在就直接返回成功（幂等），
    //同时避免它白白占掉一个上限名额、把本来能标记的用户挡在外面。
    std::ostringstream osExist;
    osExist << "SELECT COUNT(*) FROM t_starred WHERE f_userid = " << userid
            << " AND f_target = " << target;
    QueryResult* pExist = pConn->query(osExist.str().c_str());
    if (pExist != NULL)
    {
        int existCount = 0;
        Field* pRow = pExist->fetch();
        if (pRow != NULL)
        {
            existCount = pRow[0].getInt32();
        }
        pExist->endQuery();
        delete pExist;

        if (existCount > 0)
        {
            LOGI("add starred ok (already exists), userid: %lld, target: %lld",
                 (long long)userid, (long long)target);
            return true;
        }
    }

    //人数上限校验。countStarred 返回 -1 表示查询本身失败，此时宁可拒绝写入，
    //也不要在"不知道现在有多少人"的情况下放行 —— 否则上限形同虚设。
    const int current = countStarred(userid);
    if (current < 0)
    {
        outErrorCode = error_code_server_internal;
        return false;
    }
    if (current >= kMaxStarredCount)
    {
        LOGI("add starred rejected by limit, userid: %lld, current: %d, limit: %d",
             (long long)userid, current, kMaxStarredCount);
        outErrorCode = error_code_starred_limit;
        return false;
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_starred(f_userid, f_target, f_create_time) VALUES("
          << userid << ", " << target << ", " << now << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("StarredManager::setStarred insert error, userid: %lld, target: %lld",
             (long long)userid, (long long)target);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("add starred ok, userid: %lld, target: %lld", (long long)userid, (long long)target);
    return true;
}

bool StarredManager::removeStarredForAll(int64_t target)
{
    if (target <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("StarredManager::removeStarredForAll db init failed, target: %lld, db: %s",
             (long long)target, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_starred WHERE f_target = " << target;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("StarredManager::removeStarredForAll delete error, target: %lld",
             (long long)target);
        return false;
    }

    LOGI("remove starred for all ok, target: %lld", (long long)target);
    return true;
}
