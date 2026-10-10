/**
 *  加群链接存储管理器实现，GroupLinkManager.cpp。设计与约定见 GroupLinkManager.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupLinkManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"   //EscapeString
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

namespace {

//随机码撞上已有的码时最多重试几次。12 位、31 个字符的空间有 31^12 种，撞上几乎不可能，重试只为兜底
const int kCreateMaxAttempts = 5;

//t_group_link 的列清单与按列序填一条记录
const char* const kLinkColumns = "f_id, f_code, f_groupid, f_creator, f_enterprise_id, f_create_time, f_expire_time, f_revoked";

void FillLink(Field* pRow, GroupLinkRecord& r)
{
    r.id           = static_cast<int64_t>(pRow[0].getUInt64());
    r.code         = pRow[1].getCppString();
    r.groupid      = static_cast<int64_t>(pRow[2].getUInt64());
    r.creator      = static_cast<int64_t>(pRow[3].getUInt64());
    r.enterpriseid = static_cast<int64_t>(pRow[4].getUInt64());
    r.createtime   = static_cast<int64_t>(pRow[5].getUInt64());
    r.expiretime   = static_cast<int64_t>(pRow[6].getUInt64());
    r.revoked      = pRow[7].getInt32();
}

//读出一个查询的全部链接；结果集为空时 dbapi 返回 NULL，按没有记录处理
void ReadLinks(IDatabase* pConn, const std::string& sql, std::vector<GroupLinkRecord>& out)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        GroupLinkRecord r;
        FillLink(pRow, r);
        out.push_back(r);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

//建一条数据库连接；失败时记日志并返回空
std::unique_ptr<IDatabase> OpenConn(const char* where)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        LOGE("%s db init failed, db: %s", where, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

} // anonymous namespace

GroupLinkManager::GroupLinkManager()
{
}

GroupLinkManager::~GroupLinkManager()
{
}

bool GroupLinkManager::create(GroupLinkRecord& rec, int64_t nowMs)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupLinkManager::create");
    if (!pConn)
        return false;
    for (int attempt = 0; attempt < kCreateMaxAttempts; ++attempt)
    {
        const std::string code = GroupLinkProtocol::makeRandomCode();
        //先查一次：撞上已有的码就换一个，不靠唯一索引报错（两种后端的报错没法统一区分）
        std::vector<GroupLinkRecord> exists;
        std::ostringstream osCheck;
        osCheck << "SELECT " << kLinkColumns << " FROM t_group_link WHERE f_code = '" << dbapi::EscapeString(code) << "'";
        ReadLinks(pConn.get(), osCheck.str(), exists);
        if (!exists.empty())
            continue;

        std::ostringstream osSql;
        osSql << "INSERT INTO t_group_link(f_code, f_groupid, f_creator, f_enterprise_id, f_create_time, f_expire_time, "
                 "f_revoked) VALUES('" << dbapi::EscapeString(code) << "', " << rec.groupid << ", " << rec.creator << ", "
              << rec.enterpriseid << ", " << nowMs << ", " << rec.expiretime << ", 0)";
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("GroupLinkManager::create insert error, groupid: %lld", (long long)rec.groupid);
            return false;
        }
        rec.id = static_cast<int64_t>(pConn->getInsertID());
        rec.code = code;
        rec.createtime = nowMs;
        rec.revoked = 0;
        LOGI("group link created, groupid: %lld, id: %lld, creator: %lld, expire: %lld",
             (long long)rec.groupid, (long long)rec.id, (long long)rec.creator, (long long)rec.expiretime);
        return true;
    }
    LOGE("GroupLinkManager::create gave up after %d attempts, groupid: %lld", kCreateMaxAttempts, (long long)rec.groupid);
    return false;
}

bool GroupLinkManager::getByCode(const std::string& code, GroupLinkRecord& out)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupLinkManager::getByCode");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kLinkColumns << " FROM t_group_link WHERE f_code = '" << dbapi::EscapeString(code) << "'";
    std::vector<GroupLinkRecord> rows;
    ReadLinks(pConn.get(), osSql.str(), rows);
    if (rows.empty())
        return false;
    out = rows[0];
    return true;
}

bool GroupLinkManager::getById(int64_t id, GroupLinkRecord& out)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupLinkManager::getById");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kLinkColumns << " FROM t_group_link WHERE f_id = " << id;
    std::vector<GroupLinkRecord> rows;
    ReadLinks(pConn.get(), osSql.str(), rows);
    if (rows.empty())
        return false;
    out = rows[0];
    return true;
}

bool GroupLinkManager::listUsable(int64_t groupid, int64_t creator, int64_t nowMs, std::vector<GroupLinkRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupLinkManager::listUsable");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "SELECT " << kLinkColumns << " FROM t_group_link WHERE f_groupid = " << groupid << " AND f_revoked = 0"
          << " AND (f_expire_time = 0 OR f_expire_time > " << nowMs << ")";
    if (creator > 0)
        osSql << " AND f_creator = " << creator;
    osSql << " ORDER BY f_create_time DESC, f_id DESC";
    ReadLinks(pConn.get(), osSql.str(), out);
    return true;
}

bool GroupLinkManager::revoke(int64_t groupid, int64_t id)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("GroupLinkManager::revoke");
    if (!pConn)
        return false;
    std::ostringstream osSql;
    osSql << "UPDATE t_group_link SET f_revoked = 1 WHERE f_id = " << id << " AND f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupLinkManager::revoke error, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
        return false;
    }
    LOGI("group link revoked, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
    return true;
}
