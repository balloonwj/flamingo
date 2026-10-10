/**
 *  群互动卡片互动存储管理器实现，CardManager.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "CardManager.h"
#include <memory>
#include <sstream>
#include <vector>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

//t_card_interaction 的 UPSERT 冲突列。必须与该表上唯一索引 uniq_msg_user 的列完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define CARD_INTERACTION_CONFLICT_COLUMNS   "f_msgid, f_userid"

CardManager::CardManager()
{
}

CardManager::~CardManager()
{
}

bool CardManager::init()
{
    return true;
}

bool CardManager::upsertInteraction(int64_t msgid, int32_t userid,
                                    const std::string& data, int64_t time)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::upsertInteraction db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //f_data 可能为接龙文本（含引号 / 中文），须经 escapeString 转义
    std::vector<char> escData(data.size() * 2 + 1, '\0');
    pConn->escapeString(&escData[0], data.c_str(), static_cast<uint32_t>(data.size()));

    //按 (f_msgid,f_userid) 联合唯一键 UPSERT：重复互动只覆盖 f_data，f_create_time 保留
    //首次互动的时间。读取时按该时间排序，接龙的名次因此不随修改而变化（2026-10-02 之前
    //冲突时连时间一起刷新，修改本人那条后这一条会排到列表末尾）。
    //冲突子句两种后端写法不同（SQLite 必须写明冲突列），由 SqlDialect 生成；
    //赋值部分的文本两边通用，无需区分
    std::ostringstream osSql;
    osSql << "INSERT INTO t_card_interaction(f_msgid, f_userid, f_data, f_create_time) VALUES("
          << msgid << ", " << userid << ", '" << &escData[0] << "', " << time
          << ")" << dbapi::UpsertClause(CARD_INTERACTION_CONFLICT_COLUMNS)
          << "f_data='" << &escData[0] << "'";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("CardManager::upsertInteraction error, msgid: %lld, userid: %d",
             (long long)msgid, userid);
        return false;
    }
    return true;
}

bool CardManager::removeInteraction(int64_t msgid, int32_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::removeInteraction db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_card_interaction WHERE f_msgid = " << msgid
          << " AND f_userid = " << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("CardManager::removeInteraction error, msgid: %lld, userid: %d",
             (long long)msgid, userid);
        return false;
    }
    return true;
}

bool CardManager::getInteraction(int64_t msgid, int32_t userid, CardInteractionRecord& out, bool& found)
{
    found = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::getInteraction db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_userid, f_data, f_create_time FROM t_card_interaction "
             "WHERE f_msgid = " << msgid << " AND f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    //query 只在执行失败时返回 NULL；结果为空时仍返回结果集对象，只是取不到行
    //（见 IDatabase::query 的约定）。调用方据返回值区分"服务端内部错误"与"没有这一行"
    if (pResult == NULL)
    {
        LOGE("CardManager::getInteraction error, msgid: %lld, userid: %d", (long long)msgid, userid);
        return false;
    }

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.userid     = static_cast<int64_t>(pRow[0].getUInt64());
        out.data       = pRow[1].getCppString();
        out.createtime = static_cast<int64_t>(pRow[2].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool CardManager::closeCard(int64_t msgid, int32_t closer, int64_t time, bool& inserted)
{
    inserted = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::closeCard db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //f_msgid 是主键：卡片已被结束时 INSERT IGNORE 不写入、受影响行数为 0，原记录保持不变。
    //SQLite 侧由 SqliteSqlTranslator 改写成 INSERT OR IGNORE
    std::ostringstream osSql;
    osSql << "INSERT IGNORE INTO t_card_state(f_msgid, f_closed, f_closer, f_close_time) VALUES("
          << msgid << ", 1, " << closer << ", " << time << ")";
    uint32_t affected = 0;
    int dbErrno = 0;
    if (!pConn->execute(osSql.str().c_str(), affected, dbErrno))
    {
        LOGE("CardManager::closeCard error, msgid: %lld, closer: %d, errno: %d",
             (long long)msgid, closer, dbErrno);
        return false;
    }
    inserted = (affected > 0);
    return true;
}

bool CardManager::getCardState(int64_t msgid, CardStateRecord& out, bool& found)
{
    found = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::getCardState db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_closed, f_closer, f_close_time FROM t_card_state WHERE f_msgid = " << msgid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    //query 只在执行失败时返回 NULL；没有这一行时仍返回结果集对象，只是取不到行
    if (pResult == NULL)
    {
        LOGE("CardManager::getCardState error, msgid: %lld", (long long)msgid);
        return false;
    }

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.closed    = (pRow[0].getInt32() != 0);
        out.closer    = static_cast<int64_t>(pRow[1].getUInt64());
        out.closetime = static_cast<int64_t>(pRow[2].getUInt64());
        found = out.closed;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool CardManager::getCardInteractions(int64_t msgid, std::vector<CardInteractionRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("CardManager::getCardInteractions db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_userid, f_data, f_create_time FROM t_card_interaction "
             "WHERE f_msgid = " << msgid << " ORDER BY f_create_time ASC, f_id ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        CardInteractionRecord rec;
        rec.userid     = static_cast<int64_t>(pRow[0].getUInt64());
        rec.data       = pRow[1].getCppString();
        rec.createtime = static_cast<int64_t>(pRow[2].getUInt64());
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}
