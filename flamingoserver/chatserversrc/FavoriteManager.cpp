/**
 *  「我的收藏」存储管理器实现，FavoriteManager.cpp
 *  balloonwj@qq.com   2026-08-03
 */
#include "FavoriteManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"      //error_code / conversation_type

FavoriteManager::FavoriteManager()
{
}

FavoriteManager::~FavoriteManager()
{
}

bool FavoriteManager::getFavorites(int64_t userid, std::vector<FavoriteItem>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FavoriteManager::getFavorites db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //按收藏时间倒序，最近收藏的排最前。f_ctype / f_cid 作次级排序键，
    //是为了让"同一毫秒内连着收藏的几条"每次查询的顺序都一样，否则界面上
    //那几行会莫名其妙地互换位置。
    std::ostringstream osSql;
    osSql << "SELECT f_ctype, f_cid FROM t_favorite WHERE f_userid = " << userid
          << " ORDER BY f_create_time DESC, f_ctype ASC, f_cid ASC";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //查询失败与"一条收藏都没有"是两回事：前者要让调用方知道，后者是正常情况。
        //dbapi 在结果集为空时同样返回 NULL，故这里只能一并按"没有收藏"处理，
        //但仍返回 true —— 收藏拉不到不该连累整个通讯录应答。
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        FavoriteItem item;
        item.ctype = pRow[0].getInt32();
        item.cid   = static_cast<int64_t>(pRow[1].getUInt64());
        out.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

int FavoriteManager::countFavorites(int64_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FavoriteManager::countFavorites db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return -1;
    }

    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_favorite WHERE f_userid = " << userid;

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

bool FavoriteManager::setFavorite(int64_t userid, int ctype, int64_t cid, int favorite,
                                  int64_t now, int& outErrorCode)
{
    outErrorCode = error_code_ok;

    //取值域兜底。正常流程里 FavoriteProtocol::parseSetFavoriteRequest 已经拦过一道，
    //这里再判一次是防着日后从别的路径（如 admin 控制台）调进来。
    if (ctype != conversation_single && ctype != conversation_group)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }
    if (cid <= 0 || userid <= 0)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FavoriteManager::setFavorite db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    if (favorite == 0)
    {
        //取消收藏：删掉即可。删到 0 行也算成功 —— 用户连点两下"取消收藏"
        //不该在第二下收到报错。取消不受数量上限约束，否则存量超限的用户
        //连"删掉几个"都做不到。
        std::ostringstream osSql;
        osSql << "DELETE FROM t_favorite WHERE f_userid = " << userid
              << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("FavoriteManager::setFavorite delete error, userid: %lld, ctype: %d, cid: %lld",
                 (long long)userid, ctype, (long long)cid);
            outErrorCode = error_code_server_internal;
            return false;
        }

        LOGI("remove favorite ok, userid: %lld, ctype: %d, cid: %lld",
             (long long)userid, ctype, (long long)cid);
        return true;
    }

    //加入收藏。先看这一条是不是已经存在：已存在就直接返回成功（幂等），
    //同时避免它白白占掉一个上限名额、把本来能收藏的用户挡在外面。
    std::ostringstream osExist;
    osExist << "SELECT COUNT(*) FROM t_favorite WHERE f_userid = " << userid
            << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
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
            LOGI("add favorite ok (already exists), userid: %lld, ctype: %d, cid: %lld",
                 (long long)userid, ctype, (long long)cid);
            return true;
        }
    }

    //数量上限校验。countFavorites 返回 -1 表示查询本身失败，此时宁可拒绝写入，
    //也不要在"不知道现在有多少条"的情况下放行 —— 否则上限形同虚设。
    const int current = countFavorites(userid);
    if (current < 0)
    {
        outErrorCode = error_code_server_internal;
        return false;
    }
    if (current >= kMaxFavoriteCount)
    {
        LOGI("add favorite rejected by limit, userid: %lld, current: %d, limit: %d",
             (long long)userid, current, kMaxFavoriteCount);
        outErrorCode = error_code_favorite_limit;
        return false;
    }

    //f_order 本期恒为 0（未手动排序），排序一律按 f_create_time 倒序。
    std::ostringstream osSql;
    osSql << "INSERT INTO t_favorite(f_userid, f_ctype, f_cid, f_order, f_create_time) VALUES("
          << userid << ", " << ctype << ", " << cid << ", 0, " << now << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FavoriteManager::setFavorite insert error, userid: %lld, ctype: %d, cid: %lld",
             (long long)userid, ctype, (long long)cid);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("add favorite ok, userid: %lld, ctype: %d, cid: %lld, count: %d",
         (long long)userid, ctype, (long long)cid, current + 1);
    return true;
}

bool FavoriteManager::removeFavoriteForAll(int ctype, int64_t cid)
{
    if (ctype != conversation_single && ctype != conversation_group)
        return false;
    if (cid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FavoriteManager::removeFavoriteForAll db init failed, ctype: %d, cid: %lld, db: %s",
             ctype, (long long)cid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_favorite WHERE f_ctype = " << ctype << " AND f_cid = " << cid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FavoriteManager::removeFavoriteForAll delete error, ctype: %d, cid: %lld",
             ctype, (long long)cid);
        return false;
    }

    LOGI("remove favorite for all ok, ctype: %d, cid: %lld", ctype, (long long)cid);
    return true;
}
