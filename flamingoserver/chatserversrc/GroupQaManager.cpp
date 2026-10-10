/**
 *  群问答存储管理器实现，GroupQaManager.cpp。设计与约定见 GroupQaManager.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupQaManager.h"

#include <map>
#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"   //CDbTransaction：企业问答与它应用到的群一起写
#include "../dbapi/SqlDialect.h"      //EscapeString
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

namespace {

//建一条数据库连接；失败时记日志并返回空
std::unique_ptr<IDatabase> OpenConn(const char* where)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        LOGE("%s db init failed, db: %s", where, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

//执行一条只返回单个整数的查询；失败或没有结果返回 -1
int64_t QueryScalar(IDatabase* pConn, const std::string& sql)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return -1;
    int64_t value = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return value;
}

//t_group_qa 的列清单与按列序填一条记录
const char* const kGroupQaColumns = "f_id, f_groupid, f_question, f_answer, f_editor, f_create_time, f_update_time";

void FillGroupQa(Field* pRow, GroupQaRecord& r)
{
    r.id         = static_cast<int64_t>(pRow[0].getUInt64());
    r.groupid    = static_cast<int64_t>(pRow[1].getUInt64());
    r.question   = pRow[2].getCppString();
    r.answer     = pRow[3].getCppString();
    r.editor     = static_cast<int64_t>(pRow[4].getUInt64());
    r.createtime = static_cast<int64_t>(pRow[5].getUInt64());
    r.updatetime = static_cast<int64_t>(pRow[6].getUInt64());
    r.source     = group_qa_source_group;
}

//t_enterprise_qa 的列清单与按列序填一条记录（不含应用到的群）
const char* const kEnterpriseQaColumns =
    "f_id, f_enterprise_id, f_question, f_answer, f_scope, f_creator_id, f_creator_name, f_create_time, f_update_time";

void FillEnterpriseQa(Field* pRow, EnterpriseQaRecord& r)
{
    r.id           = static_cast<int64_t>(pRow[0].getUInt64());
    r.enterpriseid = static_cast<int64_t>(pRow[1].getUInt64());
    r.question     = pRow[2].getCppString();
    r.answer       = pRow[3].getCppString();
    r.scope        = pRow[4].getInt32();
    r.creatorid    = static_cast<int64_t>(pRow[5].getUInt64());
    r.creatorname  = pRow[6].getCppString();
    r.createtime   = static_cast<int64_t>(pRow[7].getUInt64());
    r.updatetime   = static_cast<int64_t>(pRow[8].getUInt64());
}

//逐行读出一个查询的全部记录；结果集为空时 dbapi 返回 NULL，按没有记录处理
template <typename Record, typename Filler>
void ReadRows(IDatabase* pConn, const std::string& sql, Filler fill, std::vector<Record>& out)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        Record r;
        fill(pRow, r);
        out.push_back(r);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

//读出一批企业问答各自应用到的群，填进 groupids
void FillQaGroups(IDatabase* pConn, std::vector<EnterpriseQaRecord>& items)
{
    if (items.empty())
        return;
    std::map<int64_t, size_t> indexById;
    std::ostringstream ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        indexById[items[i].id] = i;
        if (i > 0)
            ids << ",";
        ids << items[i].id;
    }
    std::ostringstream osSql;
    osSql << "SELECT f_qa_id, f_groupid FROM t_enterprise_qa_group WHERE f_qa_id IN (" << ids.str()
          << ") ORDER BY f_qa_id, f_groupid";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        std::map<int64_t, size_t>::const_iterator it = indexById.find(static_cast<int64_t>(pRow[0].getUInt64()));
        if (it != indexById.end())
            items[it->second].groupids.push_back(static_cast<int64_t>(pRow[1].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

//把一条企业问答应用到的群整份写入（先删后插），在调用方的事务里执行
bool WriteQaGroups(IDatabase* pConn, int64_t qaId, int scope, const std::vector<int64_t>& groupids)
{
    std::ostringstream osDel;
    osDel << "DELETE FROM t_enterprise_qa_group WHERE f_qa_id = " << qaId;
    if (!pConn->execute(osDel.str().c_str()))
        return false;
    //应用到全部群时不登记具体的群
    if (scope != enterprise_qa_scope_selected)
        return true;
    for (size_t i = 0; i < groupids.size(); ++i)
    {
        std::ostringstream osIns;
        osIns << "INSERT INTO t_enterprise_qa_group(f_qa_id, f_groupid) VALUES(" << qaId << ", " << groupids[i] << ")";
        if (!pConn->execute(osIns.str().c_str()))
            return false;
    }
    return true;
}

} // anonymous namespace

GroupQaManager::GroupQaManager()
{
}

GroupQaManager::~GroupQaManager()
{
}

bool GroupQaManager::listGroupQa(int64_t groupid, std::vector<GroupQaRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::listGroupQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kGroupQaColumns << " FROM t_group_qa WHERE f_groupid = " << groupid
          << " ORDER BY f_update_time DESC, f_id DESC";
    ReadRows(pConn.get(), osSql.str(), FillGroupQa, out);
    return true;
}

int GroupQaManager::countGroupQa(int64_t groupid)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::countGroupQa");
    if (!pConn)
        return -1;
    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_group_qa WHERE f_groupid = " << groupid;
    return static_cast<int>(QueryScalar(pConn.get(), osSql.str()));
}

bool GroupQaManager::getGroupQa(int64_t id, GroupQaRecord& out)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::getGroupQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kGroupQaColumns << " FROM t_group_qa WHERE f_id = " << id;
    std::vector<GroupQaRecord> rows;
    ReadRows(pConn.get(), osSql.str(), FillGroupQa, rows);
    if (rows.empty())
        return false;
    out = rows[0];
    return true;
}

bool GroupQaManager::addGroupQa(int64_t groupid, const std::string& question, const std::string& answer,
                                int64_t editor, int64_t nowMs, int64_t& outId)
{
    outId = 0;
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::addGroupQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_qa(f_groupid, f_question, f_answer, f_editor, f_create_time, f_update_time) VALUES("
          << groupid << ", '" << dbapi::EscapeString(question) << "', '" << dbapi::EscapeString(answer) << "', "
          << editor << ", " << nowMs << ", " << nowMs << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupQaManager::addGroupQa error, groupid: %lld", (long long)groupid);
        return false;
    }
    outId = static_cast<int64_t>(pConn->getInsertID());
    return true;
}

bool GroupQaManager::updateGroupQa(int64_t groupid, int64_t id, const std::string& question,
                                   const std::string& answer, int64_t editor, int64_t nowMs)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::updateGroupQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "UPDATE t_group_qa SET f_question = '" << dbapi::EscapeString(question) << "', f_answer = '"
          << dbapi::EscapeString(answer) << "', f_editor = " << editor << ", f_update_time = " << nowMs
          << " WHERE f_id = " << id << " AND f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupQaManager::updateGroupQa error, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
        return false;
    }
    return true;
}

bool GroupQaManager::deleteGroupQa(int64_t groupid, int64_t id)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::deleteGroupQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "DELETE FROM t_group_qa WHERE f_id = " << id << " AND f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupQaManager::deleteGroupQa error, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
        return false;
    }
    return true;
}

bool GroupQaManager::listEnterpriseQaForGroup(int64_t enterpriseId, int64_t groupid, std::vector<GroupQaRecord>& out)
{
    out.clear();
    if (enterpriseId <= 0)
        return true;
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::listEnterpriseQaForGroup");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kEnterpriseQaColumns << " FROM t_enterprise_qa WHERE f_enterprise_id = " << enterpriseId
          << " AND (f_scope = " << enterprise_qa_scope_all
          << " OR f_id IN (SELECT f_qa_id FROM t_enterprise_qa_group WHERE f_groupid = " << groupid << "))"
          << " ORDER BY f_update_time DESC, f_id DESC";
    std::vector<EnterpriseQaRecord> rows;
    ReadRows(pConn.get(), osSql.str(), FillEnterpriseQa, rows);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        GroupQaRecord r;
        r.id         = rows[i].id;
        r.groupid    = groupid;
        r.question   = rows[i].question;
        r.answer     = rows[i].answer;
        r.source     = group_qa_source_enterprise;
        r.editor     = rows[i].creatorid;
        r.editorname = rows[i].creatorname;
        r.createtime = rows[i].createtime;
        r.updatetime = rows[i].updatetime;
        out.push_back(r);
    }
    return true;
}

bool GroupQaManager::listEnterpriseQa(int64_t enterpriseId, std::vector<EnterpriseQaRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::listEnterpriseQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kEnterpriseQaColumns << " FROM t_enterprise_qa WHERE f_enterprise_id = " << enterpriseId
          << " ORDER BY f_update_time DESC, f_id DESC";
    ReadRows(pConn.get(), osSql.str(), FillEnterpriseQa, out);
    FillQaGroups(pConn.get(), out);
    return true;
}

int GroupQaManager::countEnterpriseQa(int64_t enterpriseId)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::countEnterpriseQa");
    if (!pConn)
        return -1;
    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_enterprise_qa WHERE f_enterprise_id = " << enterpriseId;
    return static_cast<int>(QueryScalar(pConn.get(), osSql.str()));
}

bool GroupQaManager::getEnterpriseQa(int64_t id, EnterpriseQaRecord& out)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::getEnterpriseQa");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kEnterpriseQaColumns << " FROM t_enterprise_qa WHERE f_id = " << id;
    std::vector<EnterpriseQaRecord> rows;
    ReadRows(pConn.get(), osSql.str(), FillEnterpriseQa, rows);
    if (rows.empty())
        return false;
    FillQaGroups(pConn.get(), rows);
    out = rows[0];
    return true;
}

bool GroupQaManager::addEnterpriseQa(const EnterpriseQaRecord& rec, int64_t nowMs, int64_t& outId)
{
    outId = 0;
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::addEnterpriseQa");
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;
    std::ostringstream osSql;
    osSql << "INSERT INTO t_enterprise_qa(f_enterprise_id, f_question, f_answer, f_scope, f_creator_id, f_creator_name, "
             "f_create_time, f_update_time) VALUES("
          << rec.enterpriseid << ", '" << dbapi::EscapeString(rec.question) << "', '"
          << dbapi::EscapeString(rec.answer) << "', " << rec.scope << ", " << rec.creatorid << ", '"
          << dbapi::EscapeString(rec.creatorname) << "', " << nowMs << ", " << nowMs << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupQaManager::addEnterpriseQa insert error, enterprise: %lld", (long long)rec.enterpriseid);
        return false;
    }
    const int64_t newId = static_cast<int64_t>(pConn->getInsertID());
    if (newId <= 0 || !WriteQaGroups(pConn.get(), newId, rec.scope, rec.groupids))
    {
        LOGE("GroupQaManager::addEnterpriseQa groups error, enterprise: %lld", (long long)rec.enterpriseid);
        return false;
    }
    if (!transaction.Commit())
        return false;
    outId = newId;
    return true;
}

bool GroupQaManager::updateEnterpriseQa(const EnterpriseQaRecord& rec, int64_t nowMs)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::updateEnterpriseQa");
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;
    std::ostringstream osSql;
    osSql << "UPDATE t_enterprise_qa SET f_question = '" << dbapi::EscapeString(rec.question) << "', f_answer = '"
          << dbapi::EscapeString(rec.answer) << "', f_scope = " << rec.scope << ", f_creator_id = " << rec.creatorid
          << ", f_creator_name = '" << dbapi::EscapeString(rec.creatorname) << "', f_update_time = " << nowMs
          << " WHERE f_id = " << rec.id << " AND f_enterprise_id = " << rec.enterpriseid;
    if (!pConn->execute(osSql.str().c_str()) || !WriteQaGroups(pConn.get(), rec.id, rec.scope, rec.groupids))
    {
        LOGE("GroupQaManager::updateEnterpriseQa error, id: %lld", (long long)rec.id);
        return false;
    }
    return transaction.Commit();
}

bool GroupQaManager::deleteEnterpriseQa(int64_t enterpriseId, int64_t id)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::deleteEnterpriseQa");
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;
    std::ostringstream osGroups;
    osGroups << "DELETE FROM t_enterprise_qa_group WHERE f_qa_id = " << id;
    std::ostringstream osQa;
    osQa << "DELETE FROM t_enterprise_qa WHERE f_id = " << id << " AND f_enterprise_id = " << enterpriseId;
    if (!pConn->execute(osGroups.str().c_str()) || !pConn->execute(osQa.str().c_str()))
    {
        LOGE("GroupQaManager::deleteEnterpriseQa error, id: %lld", (long long)id);
        return false;
    }
    return transaction.Commit();
}

bool GroupQaManager::listAllGroups(std::vector<std::pair<int64_t, std::pair<std::string, int64_t> > >& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupQaManager::listAllGroups");
    if (!pConn)
        return false;
    QueryResult* pResult = pConn->query("SELECT f_groupid, f_name, f_ownerid FROM t_group ORDER BY f_groupid");
    if (pResult == NULL)
        return true;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out.push_back(std::make_pair(static_cast<int64_t>(pRow[0].getUInt64()),
                                     std::make_pair(pRow[1].getCppString(), static_cast<int64_t>(pRow[2].getUInt64()))));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}
