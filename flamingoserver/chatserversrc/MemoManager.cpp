/**
 *  备忘存储管理器实现，MemoManager.cpp
 *  balloonwj@qq.com   2026-10-03
 */
#include "MemoManager.h"
#include <memory>
#include <sstream>
#include <map>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h" //CDbTransaction：备忘本体与提醒对象的多条写语句一起成败
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"   //conversation_type

//t_memo 的列清单，SELECT 时按这个顺序取，readMemoRow 按同一顺序解读
#define MEMO_SELECT_COLUMNS \
    "m.f_id, m.f_ctype, m.f_owner, m.f_creator, m.f_editor, m.f_title, m.f_note, m.f_start, " \
    "m.f_advance, m.f_repeat, m.f_weekdays, m.f_count, m.f_style, m.f_target_all, " \
    "m.f_create_time, m.f_update_time"

//MEMO_SELECT_COLUMNS 里的列数；listGroupMemosOfMember 在它后面再多取一列角色
constexpr int MEMO_SELECT_COLUMN_COUNT = 16;

//建一条数据库连接；失败时记日志并返回 NULL。caller 只用于日志
static IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
    {
        LOGE("MemoManager::%s db init failed, db: %s", caller,
             Singleton<CDbConfig>::Instance().describe().c_str());
    }
    return pConn;
}

//把一段文本按当前数据库的规则转义，供拼进单引号里的字符串字面量
static std::string escapeText(IDatabase* pConn, const std::string& text)
{
    std::vector<char> buf(text.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], text.c_str(), static_cast<uint32_t>(text.size()));
    return std::string(&buf[0]);
}

//把一行 MEMO_SELECT_COLUMNS 解读成 MemoRecord（不含提醒对象）
static void readMemoRow(Field* pRow, MemoRecord& out)
{
    int col = 0;
    out.memoid     = static_cast<int64_t>(pRow[col++].getUInt64());
    out.ctype      = pRow[col++].getInt32();
    out.owner      = static_cast<int64_t>(pRow[col++].getUInt64());
    out.creator    = static_cast<int64_t>(pRow[col++].getUInt64());
    out.editor     = static_cast<int64_t>(pRow[col++].getUInt64());
    out.title      = pRow[col++].getCppString();
    out.note       = pRow[col++].getCppString();
    out.start      = static_cast<int64_t>(pRow[col++].getUInt64());
    out.advance    = pRow[col++].getInt32();
    out.repeat     = pRow[col++].getInt32();
    out.weekdays   = pRow[col++].getInt32();
    out.count      = pRow[col++].getInt32();
    out.style      = pRow[col++].getInt32();
    out.targetAll  = (pRow[col++].getInt32() != 0);
    out.createTime = static_cast<int64_t>(pRow[col++].getUInt64());
    out.updateTime = static_cast<int64_t>(pRow[col++].getUInt64());
    out.targets.clear();
}

/**
 *  执行一条 SELECT MEMO_SELECT_COLUMNS（可再多取一列整数）的查询，把结果追加到 out。
 *    pConn：     已建立的连接，不转移所有权；
 *    sql：       查询语句；
 *    out：       [出参] 结果追加到末尾；
 *    outExtra：  非 NULL 时把第 MEMO_SELECT_COLUMN_COUNT 列（下标从 0 起）的整数追加进去。
 *  返回：查询执行成功返回 true。
 */
static bool queryMemos(IDatabase* pConn, const std::string& sql, std::vector<MemoRecord>& out,
                       std::vector<int>* outExtra)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
    {
        LOGE("MemoManager query error, sql: %s", sql.c_str());
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        MemoRecord rec;
        readMemoRow(pRow, rec);
        out.push_back(rec);
        if (outExtra != NULL)
            outExtra->push_back(pRow[MEMO_SELECT_COLUMN_COUNT].getInt32());

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

/**
 *  给一批备忘补上提醒对象：一次查出这些备忘在 t_memo_target 里的全部行，再按备忘分发。
 *  只有群备忘、且不是提醒全体的那些才会有行，其余的 targets 保持为空。
 *    pConn：已建立的连接，不转移所有权；
 *    memos：[入参/出参] 待补的备忘。
 *  返回：查询执行成功返回 true；memos 为空时直接返回 true。
 */
static bool fillTargets(IDatabase* pConn, std::vector<MemoRecord>& memos)
{
    std::ostringstream osIds;
    std::map<int64_t, size_t> indexById;
    for (size_t i = 0; i < memos.size(); ++i)
    {
        if (memos[i].ctype != conversation_group || memos[i].targetAll)
            continue;
        if (!indexById.empty())
            osIds << ",";
        osIds << memos[i].memoid;
        indexById[memos[i].memoid] = i;
    }
    if (indexById.empty())
        return true;

    std::ostringstream osSql;
    osSql << "SELECT f_memoid, f_userid FROM t_memo_target WHERE f_memoid IN (" << osIds.str()
          << ") ORDER BY f_memoid ASC, f_userid ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MemoManager::fillTargets query error, sql: %s", osSql.str().c_str());
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        const int64_t memoid = static_cast<int64_t>(pRow[0].getUInt64());
        const int64_t userid = static_cast<int64_t>(pRow[1].getUInt64());
        std::map<int64_t, size_t>::const_iterator it = indexById.find(memoid);
        if (it != indexById.end())
            memos[it->second].targets.push_back(userid);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

//在事务里写一条备忘的提醒对象（先删后插）。调用方已开启事务；任何一条失败返回 false
static bool writeTargets(IDatabase* pConn, const MemoRecord& memo)
{
    std::ostringstream osDel;
    osDel << "DELETE FROM t_memo_target WHERE f_memoid = " << memo.memoid;
    if (!pConn->execute(osDel.str().c_str()))
    {
        LOGE("MemoManager::writeTargets delete error, memoid: %lld", (long long)memo.memoid);
        return false;
    }

    if (memo.ctype != conversation_group || memo.targetAll || memo.targets.empty())
        return true;

    std::ostringstream osIns;
    osIns << "INSERT INTO t_memo_target(f_memoid, f_userid) VALUES";
    for (size_t i = 0; i < memo.targets.size(); ++i)
    {
        if (i > 0)
            osIns << ",";
        osIns << "(" << memo.memoid << ", " << memo.targets[i] << ")";
    }
    if (!pConn->execute(osIns.str().c_str()))
    {
        LOGE("MemoManager::writeTargets insert error, memoid: %lld, count: %u",
             (long long)memo.memoid, static_cast<unsigned>(memo.targets.size()));
        return false;
    }
    return true;
}

MemoManager::MemoManager()
{
}

MemoManager::~MemoManager()
{
}

bool MemoManager::init()
{
    return true;
}

bool MemoManager::insertMemo(MemoRecord& memo)
{
    std::unique_ptr<IDatabase> pConn(openConnection("insertMemo"));
    if (!pConn)
        return false;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MemoManager::insertMemo begin transaction error, owner: %lld", (long long)memo.owner);
        return false;
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_memo(f_ctype, f_owner, f_creator, f_editor, f_title, f_note, f_start, "
             "f_advance, f_repeat, f_weekdays, f_count, f_style, f_target_all, f_create_time, f_update_time) VALUES("
          << memo.ctype << ", " << memo.owner << ", " << memo.creator << ", " << memo.editor << ", '"
          << escapeText(pConn.get(), memo.title) << "', '" << escapeText(pConn.get(), memo.note) << "', "
          << memo.start << ", " << memo.advance << ", " << memo.repeat << ", " << memo.weekdays << ", "
          << memo.count << ", " << memo.style << ", " << (memo.targetAll ? 1 : 0) << ", "
          << memo.createTime << ", " << memo.updateTime << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MemoManager::insertMemo insert error, ctype: %d, owner: %lld", memo.ctype, (long long)memo.owner);
        return false;
    }

    //自增 ID 是连接私有的状态，必须在执行 INSERT 的同一条连接上读取
    memo.memoid = static_cast<int64_t>(pConn->getInsertID());
    if (memo.memoid <= 0)
    {
        LOGE("MemoManager::insertMemo got no insert id, owner: %lld", (long long)memo.owner);
        return false;
    }

    if (!writeTargets(pConn.get(), memo))
        return false;

    if (!transaction.Commit())
    {
        LOGE("MemoManager::insertMemo commit error, memoid: %lld", (long long)memo.memoid);
        memo.memoid = 0;
        return false;
    }
    return true;
}

bool MemoManager::updateMemo(const MemoRecord& memo)
{
    std::unique_ptr<IDatabase> pConn(openConnection("updateMemo"));
    if (!pConn)
        return false;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MemoManager::updateMemo begin transaction error, memoid: %lld", (long long)memo.memoid);
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_memo SET f_editor = " << memo.editor
          << ", f_title = '" << escapeText(pConn.get(), memo.title)
          << "', f_note = '" << escapeText(pConn.get(), memo.note)
          << "', f_start = " << memo.start
          << ", f_advance = " << memo.advance
          << ", f_repeat = " << memo.repeat
          << ", f_weekdays = " << memo.weekdays
          << ", f_count = " << memo.count
          << ", f_style = " << memo.style
          << ", f_target_all = " << (memo.targetAll ? 1 : 0)
          << ", f_update_time = " << memo.updateTime
          << " WHERE f_id = " << memo.memoid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MemoManager::updateMemo update error, memoid: %lld", (long long)memo.memoid);
        return false;
    }

    if (!writeTargets(pConn.get(), memo))
        return false;

    if (!transaction.Commit())
    {
        LOGE("MemoManager::updateMemo commit error, memoid: %lld", (long long)memo.memoid);
        return false;
    }
    return true;
}

bool MemoManager::deleteMemo(int64_t memoid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("deleteMemo"));
    if (!pConn)
        return false;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MemoManager::deleteMemo begin transaction error, memoid: %lld", (long long)memoid);
        return false;
    }

    std::ostringstream osTarget;
    osTarget << "DELETE FROM t_memo_target WHERE f_memoid = " << memoid;
    std::ostringstream osMemo;
    osMemo << "DELETE FROM t_memo WHERE f_id = " << memoid;
    if (!pConn->execute(osTarget.str().c_str()) || !pConn->execute(osMemo.str().c_str()))
    {
        LOGE("MemoManager::deleteMemo error, memoid: %lld", (long long)memoid);
        return false;
    }

    if (!transaction.Commit())
    {
        LOGE("MemoManager::deleteMemo commit error, memoid: %lld", (long long)memoid);
        return false;
    }
    return true;
}

bool MemoManager::getMemo(int64_t memoid, MemoRecord& out, bool& found)
{
    found = false;
    std::unique_ptr<IDatabase> pConn(openConnection("getMemo"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " MEMO_SELECT_COLUMNS " FROM t_memo m WHERE m.f_id = " << memoid;
    std::vector<MemoRecord> rows;
    if (!queryMemos(pConn.get(), osSql.str(), rows, NULL))
        return false;
    if (rows.empty())
        return true;
    if (!fillTargets(pConn.get(), rows))
        return false;

    out = rows[0];
    found = true;
    return true;
}

bool MemoManager::countMemos(int ctype, int64_t owner, int& outCount)
{
    outCount = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("countMemos"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_memo WHERE f_ctype = " << ctype << " AND f_owner = " << owner;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MemoManager::countMemos error, ctype: %d, owner: %lld", ctype, (long long)owner);
        return false;
    }
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        outCount = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MemoManager::listPersonalMemos(int64_t userid, std::vector<MemoRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listPersonalMemos"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " MEMO_SELECT_COLUMNS " FROM t_memo m WHERE m.f_ctype = " << conversation_single
          << " AND m.f_owner = " << userid << " ORDER BY m.f_start ASC, m.f_id ASC";
    return queryMemos(pConn.get(), osSql.str(), out, NULL);
}

bool MemoManager::listGroupMemosOfMember(int64_t userid, int64_t groupid,
                                         std::vector<MemoRecord>& out, std::vector<int>& outRoles)
{
    out.clear();
    outRoles.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listGroupMemosOfMember"));
    if (!pConn)
        return false;

    //以成员行为驱动：t_group_member 的 idx_user 定位此人所在的各群，再按 idx_owner 取各群的群备忘。
    //角色随行带出，可见范围由调用方按 MemoProtocol::isVisibleTo 过滤，规则只有一份
    std::ostringstream osSql;
    osSql << "SELECT " MEMO_SELECT_COLUMNS ", gm.f_role FROM t_group_member gm "
             "JOIN t_memo m ON m.f_ctype = " << conversation_group << " AND m.f_owner = gm.f_groupid "
             "WHERE gm.f_userid = " << userid;
    if (groupid > 0)
        osSql << " AND gm.f_groupid = " << groupid;
    osSql << " ORDER BY m.f_owner ASC, m.f_start ASC, m.f_id ASC";
    if (!queryMemos(pConn.get(), osSql.str(), out, &outRoles))
        return false;
    return fillTargets(pConn.get(), out);
}

bool MemoManager::listGroupMemos(int64_t groupid, std::vector<MemoRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listGroupMemos"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " MEMO_SELECT_COLUMNS " FROM t_memo m WHERE m.f_ctype = " << conversation_group
          << " AND m.f_owner = " << groupid << " ORDER BY m.f_start ASC, m.f_id ASC";
    if (!queryMemos(pConn.get(), osSql.str(), out, NULL))
        return false;
    return fillTargets(pConn.get(), out);
}

bool MemoManager::removeTargetsInGroup(int64_t groupid, const std::vector<int64_t>& userids)
{
    if (userids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn(openConnection("removeTargetsInGroup"));
    if (!pConn)
        return false;

    std::ostringstream osIds;
    for (size_t i = 0; i < userids.size(); ++i)
    {
        if (i > 0)
            osIds << ",";
        osIds << userids[i];
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_memo_target WHERE f_userid IN (" << osIds.str() << ") AND f_memoid IN "
             "(SELECT f_id FROM t_memo WHERE f_ctype = " << conversation_group << " AND f_owner = " << groupid << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MemoManager::removeTargetsInGroup error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}

bool MemoManager::deleteGroupMemos(int64_t groupid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("deleteGroupMemos"));
    if (!pConn)
        return false;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MemoManager::deleteGroupMemos begin transaction error, groupid: %lld", (long long)groupid);
        return false;
    }

    std::ostringstream osTarget;
    osTarget << "DELETE FROM t_memo_target WHERE f_memoid IN (SELECT f_id FROM t_memo WHERE f_ctype = "
             << conversation_group << " AND f_owner = " << groupid << ")";
    std::ostringstream osMemo;
    osMemo << "DELETE FROM t_memo WHERE f_ctype = " << conversation_group << " AND f_owner = " << groupid;
    if (!pConn->execute(osTarget.str().c_str()) || !pConn->execute(osMemo.str().c_str()))
    {
        LOGE("MemoManager::deleteGroupMemos error, groupid: %lld", (long long)groupid);
        return false;
    }

    if (!transaction.Commit())
    {
        LOGE("MemoManager::deleteGroupMemos commit error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}
