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

    //按 (f_msgid,f_userid) 联合唯一键 UPSERT：重复互动即覆盖 f_data 与时间。
    //冲突子句两种后端写法不同（SQLite 必须写明冲突列），由 SqlDialect 生成；
    //赋值部分的文本两边通用，无需区分
    std::ostringstream osSql;
    osSql << "INSERT INTO t_card_interaction(f_msgid, f_userid, f_data, f_create_time) VALUES("
          << msgid << ", " << userid << ", '" << &escData[0] << "', " << time
          << ")" << dbapi::UpsertClause(CARD_INTERACTION_CONFLICT_COLUMNS)
          << "f_data='" << &escData[0]
          << "', f_create_time=" << time;
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
             "WHERE f_msgid = " << msgid << " ORDER BY f_create_time ASC";
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
