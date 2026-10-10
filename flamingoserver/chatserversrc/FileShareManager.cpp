/**
 *  文件共享的共享记录、成员与审计的存储（服务端侧）实现，FileShareManager.cpp
 *
 *  字符串字段（clientshareid、设备标识、名称、路径、访问会话 id）都来自客户端，一律经 escapeString
 *  转义后再拼进 SQL；数字字段由调用方以整数传入，直接拼接。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "FileShareManager.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include "FileSharePolicy.h"   //pushTargets / cardAudience：通知名单与卡片推送用同一套规则
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"

namespace {

//成员表每条 INSERT 最多写入的行数。一次请求最多 2000 名成员（kShareMaxMembersPerRequest），
//分 4 批写入，避免拼出过长的 SQL
const size_t kMemberInsertBatch = 500;

//启动清扫读群成员时，每条查询的 IN 列表最多放的群 id 个数，避免拼出过长的 SQL
const size_t kGroupQueryBatch = 500;

//t_file_share 按 shareColumns 顺序取出的列数，fillShareRecord 按同一顺序按下标取值
const int kShareColumnCount = 16;

//生成 t_file_share 的列清单。prefix 为表别名加点（如 "s."），不联表时传空串
std::string shareColumns(const char* prefix)
{
    const char* const columns[kShareColumnCount] = {
        "f_shareid", "f_client_share_id", "f_owner_id", "f_enterprise_id", "f_device_id",
        "f_ctype", "f_cid", "f_kind", "f_name", "f_display_path", "f_include_hidden",
        "f_state", "f_cancel_reason", "f_canceled_by", "f_create_time", "f_cancel_time"
    };
    std::ostringstream os;
    for (int i = 0; i < kShareColumnCount; ++i)
    {
        if (i > 0)
            os << ", ";
        os << prefix << columns[i];
    }
    return os.str();
}

//把按 shareColumns 顺序取出的一行映射为 ShareRecord
void fillShareRecord(Field* pRow, ShareRecord& rec)
{
    rec.m_shareId       = static_cast<int64_t>(pRow[0].getUInt64());
    rec.m_clientShareId = pRow[1].getCppString();
    rec.m_ownerId       = static_cast<int64_t>(pRow[2].getUInt64());
    rec.m_enterpriseId  = static_cast<int64_t>(pRow[3].getUInt64());
    rec.m_deviceId      = pRow[4].getCppString();
    rec.m_ctype         = pRow[5].getInt32();
    rec.m_cid           = static_cast<int64_t>(pRow[6].getUInt64());
    rec.m_kind          = pRow[7].getInt32();
    rec.m_name          = pRow[8].getCppString();
    rec.m_displayPath   = pRow[9].getCppString();
    rec.m_includeHidden = (pRow[10].getInt32() != 0);
    rec.m_state         = pRow[11].getInt32();
    rec.m_cancelReason  = pRow[12].getInt32();
    rec.m_canceledBy    = static_cast<int64_t>(pRow[13].getUInt64());
    rec.m_createTime    = static_cast<int64_t>(pRow[14].getUInt64());
    rec.m_cancelTime    = static_cast<int64_t>(pRow[15].getUInt64());
}

//审计表的列，按 fillAuditRecord 的顺序
const int kAuditColumnCount = 13;

std::string auditColumns(const char* prefix)
{
    const char* const columns[kAuditColumnCount] = {
        "f_id", "f_shareid", "f_owner_id", "f_actor_id", "f_enterprise_id", "f_action", "f_source", "f_path",
        "f_to_path", "f_size", "f_result", "f_access_id", "f_create_time"
    };
    std::ostringstream os;
    for (int i = 0; i < kAuditColumnCount; ++i)
    {
        if (i > 0)
            os << ", ";
        os << prefix << columns[i];
    }
    return os.str();
}

//把按 auditColumns 顺序取出的一行映射为 ShareAuditRecord
void fillAuditRecord(Field* pRow, ShareAuditRecord& rec)
{
    rec.m_id           = static_cast<int64_t>(pRow[0].getUInt64());
    rec.m_shareId      = static_cast<int64_t>(pRow[1].getUInt64());
    rec.m_ownerId      = static_cast<int64_t>(pRow[2].getUInt64());
    rec.m_actorId      = static_cast<int64_t>(pRow[3].getUInt64());
    rec.m_enterpriseId = static_cast<int64_t>(pRow[4].getUInt64());
    rec.m_action       = pRow[5].getInt32();
    rec.m_source       = pRow[6].getInt32();
    rec.m_path         = pRow[7].getCppString();
    rec.m_toPath       = pRow[8].getCppString();
    rec.m_size         = static_cast<int64_t>(pRow[9].getUInt64());
    rec.m_result       = pRow[10].getInt32();
    rec.m_accessId     = pRow[11].getCppString();
    rec.m_createTime   = static_cast<int64_t>(pRow[12].getUInt64());
}

//执行一条 SELECT COUNT(*)；出错返回 false
bool queryCount(IDatabase* pConn, const std::string& sql, int64_t& total)
{
    total = 0;
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        total = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return true;
}

//把字符串转义成可以放进单引号里的 SQL 文本
std::string escapeText(IDatabase* pConn, const std::string& text)
{
    std::vector<char> buf(text.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], text.c_str(), static_cast<uint32_t>(text.size()));
    return std::string(&buf[0]);
}

//生成写入一条审计记录的 INSERT 语句（addAudit 与 addAudits 共用）
std::string auditInsertSql(IDatabase* pConn, const ShareAuditRecord& rec)
{
    std::ostringstream osSql;
    osSql << "INSERT INTO t_file_share_audit(f_shareid, f_owner_id, f_actor_id, f_enterprise_id, f_action, f_source, "
             "f_path, f_to_path, f_size, f_result, f_access_id, f_create_time) VALUES("
          << rec.m_shareId << ", " << rec.m_ownerId << ", " << rec.m_actorId << ", " << rec.m_enterpriseId << ", "
          << rec.m_action << ", " << rec.m_source << ", '" << escapeText(pConn, rec.m_path) << "', '"
          << escapeText(pConn, rec.m_toPath) << "', " << rec.m_size << ", " << rec.m_result << ", '"
          << escapeText(pConn, rec.m_accessId) << "', " << rec.m_createTime << ")";
    return osSql.str();
}

//执行一条返回若干条共享记录的查询，结果追加到 out；查询出错返回 false
bool queryShares(IDatabase* pConn, const std::string& sql, std::vector<ShareRecord>& out)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        ShareRecord rec;
        fillShareRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

//在给定连接上读出某个共享的全部成员，结果按 userid 升序放进 out（调用前清空）；查询出错返回 false
bool readMembers(IDatabase* pConn, int64_t shareId, std::map<int64_t, int>& out)
{
    out.clear();
    std::ostringstream osSql;
    osSql << "SELECT f_user_id, f_perms FROM t_file_share_member WHERE f_shareid = " << shareId
          << " ORDER BY f_user_id";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out[static_cast<int64_t>(pRow[0].getUInt64())] = pRow[1].getInt32();
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

//在给定连接上分批插入成员行；任一批失败返回 false
bool insertMembers(IDatabase* pConn, int64_t shareId, const std::vector<ShareMemberSpec>& members, int64_t addTime)
{
    for (size_t begin = 0; begin < members.size(); begin += kMemberInsertBatch)
    {
        const size_t end = (begin + kMemberInsertBatch < members.size())
                           ? (begin + kMemberInsertBatch) : members.size();
        std::ostringstream osSql;
        osSql << "INSERT INTO t_file_share_member(f_shareid, f_user_id, f_perms, f_add_time) VALUES";
        for (size_t i = begin; i < end; ++i)
        {
            if (i > begin)
                osSql << ",";
            osSql << "(" << shareId << ", " << members[i].m_userId << ", " << members[i].m_perms
                  << ", " << addTime << ")";
        }
        if (!pConn->execute(osSql.str().c_str()))
            return false;
    }
    return true;
}

//在给定连接上按 (分享者, clientshareid) 查已有共享；找到返回 shareid，没有或出错返回 0
int64_t findShareByClientId(IDatabase* pConn, int64_t ownerId, const std::string& escClientShareId)
{
    std::ostringstream osSql;
    osSql << "SELECT f_shareid FROM t_file_share WHERE f_owner_id = " << ownerId
          << " AND f_client_share_id = '" << escClientShareId << "'";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return 0;

    int64_t shareId = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        shareId = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return shareId;
}

//取本连接上最近一次 INSERT 生成的自增主键；取不到返回 0
int64_t queryLastInsertId(IDatabase* pConn)
{
    int64_t id = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            id = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }
    return id;
}

//建一条数据库连接；失败时记日志并返回空指针
IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("FileShareManager::%s db init failed, db: %s", caller, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

} // namespace

//static const 整型成员被按引用使用（如测试里的 CHECK）时需要类外定义，否则 GCC 下链接失败
const int FileShareManager::kShareRecordActive;
const int FileShareManager::kShareRecordCanceled;
const int FileShareManager::kDefaultMaxSharesPerOwner;

FileShareManager::FileShareManager()
    : m_maxSharesPerOwner(kDefaultMaxSharesPerOwner)
{
}

FileShareManager::~FileShareManager()
{
}

void FileShareManager::setMaxSharesPerOwner(int maxSharesPerOwner)
{
    m_maxSharesPerOwner = (maxSharesPerOwner < 1) ? kDefaultMaxSharesPerOwner : maxSharesPerOwner;
}

int FileShareManager::getMaxSharesPerOwner() const
{
    return m_maxSharesPerOwner;
}

int FileShareManager::createShare(const ShareRecord& record, const std::vector<ShareMemberSpec>& members,
                                  int64_t& shareId, bool& created)
{
    shareId = 0;
    created = false;

    std::unique_ptr<IDatabase> pConn(openConnection("createShare"));
    if (!pConn)
        return error_code_server_internal;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("FileShareManager::createShare begin transaction error, owner: %lld", (long long)record.m_ownerId);
        return error_code_server_internal;
    }

    const std::string escClientId = escapeText(pConn.get(), record.m_clientShareId);

    //重发去重：同一分享者、同一 clientshareid 已建过就直接返回那一条，成员以第一次为准
    const int64_t existed = findShareByClientId(pConn.get(), record.m_ownerId, escClientId);
    if (existed > 0)
    {
        shareId = existed;
        LOGI("create share deduplicated, owner: %lld, clientshareid: %s, shareid: %lld",
             (long long)record.m_ownerId, record.m_clientShareId.c_str(), (long long)existed);
        return error_code_ok;
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_file_share(f_client_share_id, f_owner_id, f_enterprise_id, f_device_id, f_ctype, f_cid, "
             "f_kind, f_name, f_display_path, f_include_hidden, f_state, f_create_time) VALUES('"
          << escClientId << "', " << record.m_ownerId << ", " << record.m_enterpriseId << ", '"
          << escapeText(pConn.get(), record.m_deviceId) << "', " << record.m_ctype << ", " << record.m_cid << ", "
          << record.m_kind << ", '" << escapeText(pConn.get(), record.m_name) << "', '"
          << escapeText(pConn.get(), record.m_displayPath) << "', " << (record.m_includeHidden ? 1 : 0) << ", "
          << kShareRecordActive << ", " << record.m_createTime << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FileShareManager::createShare insert share error, owner: %lld, clientshareid: %s",
             (long long)record.m_ownerId, record.m_clientShareId.c_str());
        return error_code_server_internal;
    }

    const int64_t newId = queryLastInsertId(pConn.get());
    if (newId <= 0)
    {
        LOGE("FileShareManager::createShare last insert id error, owner: %lld", (long long)record.m_ownerId);
        return error_code_server_internal;
    }

    if (!insertMembers(pConn.get(), newId, members, record.m_createTime))
    {
        LOGE("FileShareManager::createShare insert members error, shareid: %lld, members: %u",
             (long long)newId, static_cast<unsigned>(members.size()));
        return error_code_server_internal;
    }

    //提交失败时守卫仍持有该事务，由它的析构补一次回滚
    if (!transaction.Commit())
    {
        LOGE("FileShareManager::createShare commit error, shareid: %lld", (long long)newId);
        return error_code_server_internal;
    }

    shareId = newId;
    created = true;
    LOGI("create share ok, shareid: %lld, owner: %lld, ctype: %d, cid: %lld, kind: %d, members: %u",
         (long long)newId, (long long)record.m_ownerId, record.m_ctype, (long long)record.m_cid, record.m_kind,
         static_cast<unsigned>(members.size()));
    return error_code_ok;
}

int64_t FileShareManager::findShareIdByClientShareId(int64_t ownerId, const std::string& clientShareId)
{
    std::unique_ptr<IDatabase> pConn(openConnection("findShareIdByClientShareId"));
    if (!pConn)
        return 0;
    return findShareByClientId(pConn.get(), ownerId, escapeText(pConn.get(), clientShareId));
}

bool FileShareManager::getShare(int64_t shareId, ShareRecord& out)
{
    std::unique_ptr<IDatabase> pConn(openConnection("getShare"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_shareid = " << shareId;
    std::vector<ShareRecord> rows;
    if (!queryShares(pConn.get(), osSql.str(), rows) || rows.empty())
        return false;

    out = rows[0];
    return true;
}

bool FileShareManager::getMembers(int64_t shareId, std::vector<ShareMemberSpec>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("getMembers"));
    if (!pConn)
        return false;

    std::map<int64_t, int> members;
    if (!readMembers(pConn.get(), shareId, members))
        return false;

    for (std::map<int64_t, int>::const_iterator it = members.begin(); it != members.end(); ++it)
    {
        ShareMemberSpec m;
        m.m_userId = it->first;
        m.m_perms = it->second;
        out.push_back(m);
    }
    return true;
}

bool FileShareManager::getMemberPerms(int64_t shareId, int64_t userId, int& perms)
{
    perms = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("getMemberPerms"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT f_perms FROM t_file_share_member WHERE f_shareid = " << shareId
          << " AND f_user_id = " << userId;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        perms = pRow[0].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

int FileShareManager::countActiveShares(int64_t ownerId)
{
    std::unique_ptr<IDatabase> pConn(openConnection("countActiveShares"));
    if (!pConn)
        return -1;

    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_file_share WHERE f_owner_id = " << ownerId
          << " AND f_state = " << kShareRecordActive;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return -1;

    int count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

bool FileShareManager::listOwnerShares(int64_t ownerId, int ctype, int64_t cid, std::vector<ShareRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listOwnerShares"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_owner_id = " << ownerId
          << " AND f_state = " << kShareRecordActive;
    //分享者视角的会话 id 就是记录里的 f_cid，单聊群聊写法相同
    if (ctype != 0)
        osSql << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    osSql << " ORDER BY f_create_time DESC, f_shareid DESC";
    return queryShares(pConn.get(), osSql.str(), out);
}

bool FileShareManager::listMemberShares(int64_t userId, int ctype, int64_t cid, std::vector<ShareRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listMemberShares"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("s.") << " FROM t_file_share s JOIN t_file_share_member m"
             " ON s.f_shareid = m.f_shareid WHERE m.f_user_id = " << userId
          << " AND s.f_state = " << kShareRecordActive;
    if (ctype == conversation_single)
    {
        //单聊：成员视角的会话 id 是分享者，记录里的 f_cid（分享者视角的对端）是成员本人
        osSql << " AND s.f_ctype = " << conversation_single << " AND s.f_owner_id = " << cid
              << " AND s.f_cid = " << userId;
    }
    else if (ctype != 0)
    {
        //群聊：双方视角的会话 id 都是群 id
        osSql << " AND s.f_ctype = " << ctype << " AND s.f_cid = " << cid;
    }
    osSql << " ORDER BY s.f_create_time DESC, s.f_shareid DESC";
    return queryShares(pConn.get(), osSql.str(), out);
}

bool FileShareManager::listActiveConversationShares(int ctype, int64_t cid, std::vector<ShareRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listActiveConversationShares"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_ctype = " << ctype
          << " AND f_cid = " << cid << " AND f_state = " << kShareRecordActive << " ORDER BY f_shareid";
    return queryShares(pConn.get(), osSql.str(), out);
}

bool FileShareManager::listActiveEnterpriseShares(int64_t enterpriseId, std::vector<ShareRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listActiveEnterpriseShares"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_enterprise_id = " << enterpriseId
          << " AND f_state = " << kShareRecordActive << " ORDER BY f_shareid";
    return queryShares(pConn.get(), osSql.str(), out);
}

bool FileShareManager::listActiveShareGroupIds(std::set<int64_t>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listActiveShareGroupIds"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT DISTINCT f_cid FROM t_file_share WHERE f_ctype = " << conversation_group
          << " AND f_state = " << kShareRecordActive;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out.insert(static_cast<int64_t>(pRow[0].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool FileShareManager::loadGroupMembership(const std::set<int64_t>& groupIds,
                                           std::map<int64_t, std::set<int64_t> >& out)
{
    out.clear();
    if (groupIds.empty())
        return true;

    std::unique_ptr<IDatabase> pConn(openConnection("loadGroupMembership"));
    if (!pConn)
        return false;

    const std::vector<int64_t> ids(groupIds.begin(), groupIds.end());
    for (size_t begin = 0; begin < ids.size(); begin += kGroupQueryBatch)
    {
        const size_t end = (begin + kGroupQueryBatch < ids.size()) ? (begin + kGroupQueryBatch) : ids.size();
        std::ostringstream inList;
        for (size_t i = begin; i < end; ++i)
        {
            if (i > begin)
                inList << ",";
            inList << ids[i];
        }

        //先确定哪些群还存在：存在的群即便一个成员都没有，也要在结果里占一项
        std::ostringstream osGroups;
        osGroups << "SELECT f_groupid FROM t_group WHERE f_groupid IN (" << inList.str() << ")";
        QueryResult* pGroups = pConn->query(osGroups.str().c_str());
        if (pGroups == NULL)
        {
            LOGE("FileShareManager::loadGroupMembership query groups error");
            return false;
        }
        while (true)
        {
            Field* pRow = pGroups->fetch();
            if (pRow == NULL)
                break;
            out[static_cast<int64_t>(pRow[0].getUInt64())];
            if (!pGroups->nextRow())
                break;
        }
        pGroups->endQuery();
        delete pGroups;

        std::ostringstream osMembers;
        osMembers << "SELECT f_groupid, f_userid FROM t_group_member WHERE f_groupid IN (" << inList.str() << ")";
        QueryResult* pMembers = pConn->query(osMembers.str().c_str());
        if (pMembers == NULL)
        {
            LOGE("FileShareManager::loadGroupMembership query members error");
            return false;
        }
        while (true)
        {
            Field* pRow = pMembers->fetch();
            if (pRow == NULL)
                break;
            const int64_t groupId = static_cast<int64_t>(pRow[0].getUInt64());
            //成员表里有、群表里却没有的群（解散过程中途失败的残留）按群已不存在处理
            std::map<int64_t, std::set<int64_t> >::iterator it = out.find(groupId);
            if (it != out.end())
                it->second.insert(static_cast<int64_t>(pRow[1].getUInt64()));
            if (!pMembers->nextRow())
                break;
        }
        pMembers->endQuery();
        delete pMembers;
    }
    return true;
}

bool FileShareManager::sweepGroupShares(const std::map<int64_t, std::set<int64_t> >& groupMembersOf, int64_t nowMs,
                                        ShareSweepPlan& applied)
{
    applied = ShareSweepPlan();

    std::vector<ShareRecord> shares;
    {
        std::unique_ptr<IDatabase> pConn(openConnection("sweepGroupShares"));
        if (!pConn)
            return false;
        std::ostringstream osSql;
        osSql << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_ctype = " << conversation_group
              << " AND f_state = " << kShareRecordActive << " ORDER BY f_shareid";
        if (!queryShares(pConn.get(), osSql.str(), shares))
        {
            LOGE("FileShareManager::sweepGroupShares list shares error");
            return false;
        }
    }

    std::map<int64_t, std::vector<ShareMemberSpec> > membersOf;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        if (!getMembers(shares[i].m_shareId, membersOf[shares[i].m_shareId]))
        {
            LOGE("FileShareManager::sweepGroupShares read members error, shareid: %lld", (long long)shares[i].m_shareId);
            return false;
        }
    }

    const ShareSweepPlan plan = FileSharePolicy::planGroupSweep(shares, membersOf, groupMembersOf);
    for (size_t i = 0; i < plan.m_cancels.size(); ++i)
    {
        //操作人记 0：由系统清扫取消，不是某个用户或管理员
        const int code = cancelShare(plan.m_cancels[i].m_shareId, plan.m_cancels[i].m_reason, 0, nowMs);
        if (code != error_code_ok && code != error_code_share_canceled)
            return false;
        applied.m_cancels.push_back(plan.m_cancels[i]);
        LOGI("share sweep canceled share, shareid: %lld, reason: %d",
             (long long)plan.m_cancels[i].m_shareId, plan.m_cancels[i].m_reason);
    }
    for (std::map<int64_t, std::vector<int64_t> >::const_iterator it = plan.m_removeMembers.begin();
         it != plan.m_removeMembers.end(); ++it)
    {
        ShareUpdateMembersRequest req;
        req.m_shareId = it->first;
        req.m_remove = it->second;
        ShareMemberChanges changes;
        if (updateMembers(req, nowMs, changes) != error_code_ok)
            return false;
        applied.m_removeMembers[it->first] = changes.m_removed;
        LOGI("share sweep removed members, shareid: %lld, removed: %u",
             (long long)it->first, static_cast<unsigned>(changes.m_removed.size()));
    }
    return true;
}

int FileShareManager::cancelShare(int64_t shareId, int reason, int64_t canceledBy, int64_t nowMs)
{
    std::unique_ptr<IDatabase> pConn(openConnection("cancelShare"));
    if (!pConn)
        return error_code_server_internal;

    //只更新仍有效的那一行：两个取消请求同时到达时只有一个能改到，第一次的原因与操作人不会被覆盖
    std::ostringstream osSql;
    osSql << "UPDATE t_file_share SET f_state = " << kShareRecordCanceled << ", f_cancel_reason = " << reason
          << ", f_canceled_by = " << canceledBy << ", f_cancel_time = " << nowMs
          << " WHERE f_shareid = " << shareId << " AND f_state = " << kShareRecordActive;
    uint32_t affected = 0;
    int errNo = 0;
    if (!pConn->execute(osSql.str().c_str(), affected, errNo))
    {
        LOGE("FileShareManager::cancelShare update error, shareid: %lld, errno: %d", (long long)shareId, errNo);
        return error_code_server_internal;
    }

    if (affected == 0)
    {
        //没改到：要么共享不存在，要么已经取消过
        std::ostringstream osCheck;
        osCheck << "SELECT " << shareColumns("") << " FROM t_file_share WHERE f_shareid = " << shareId;
        std::vector<ShareRecord> rows;
        if (!queryShares(pConn.get(), osCheck.str(), rows))
            return error_code_server_internal;
        if (rows.empty())
            return error_code_share_not_exist;
        return error_code_share_canceled;
    }

    LOGI("cancel share ok, shareid: %lld, reason: %d, by: %lld", (long long)shareId, reason, (long long)canceledBy);
    return error_code_ok;
}

int FileShareManager::updateMembers(const ShareUpdateMembersRequest& req, int64_t nowMs, ShareMemberChanges& changes)
{
    changes = ShareMemberChanges();

    std::unique_ptr<IDatabase> pConn(openConnection("updateMembers"));
    if (!pConn)
        return error_code_server_internal;

    //先读后写必须在同一个事务里，否则两次调整交错时会按过期的成员表算出错误的变化
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("FileShareManager::updateMembers begin transaction error, shareid: %lld", (long long)req.m_shareId);
        return error_code_server_internal;
    }

    std::map<int64_t, int> current;
    if (!readMembers(pConn.get(), req.m_shareId, current))
    {
        LOGE("FileShareManager::updateMembers read members error, shareid: %lld", (long long)req.m_shareId);
        return error_code_server_internal;
    }

    //在一份副本上依次套用追加、改权限、移除，得到目标成员表；同时出现在追加与移除里的人以移除为准
    const std::set<int64_t> removing(req.m_remove.begin(), req.m_remove.end());
    std::map<int64_t, int> target = current;
    for (size_t i = 0; i < req.m_add.size(); ++i)
    {
        if (removing.count(req.m_add[i].m_userId) == 0)
            target[req.m_add[i].m_userId] = req.m_add[i].m_perms;
    }
    for (size_t i = 0; i < req.m_update.size(); ++i)
    {
        std::map<int64_t, int>::iterator it = target.find(req.m_update[i].m_userId);
        if (it != target.end())
            it->second = req.m_update[i].m_perms;
    }
    for (std::set<int64_t>::const_iterator it = removing.begin(); it != removing.end(); ++it)
        target.erase(*it);

    //比较现状与目标，得出实际的变化（两张表都按 userid 有序，结果也按 userid 升序）
    ShareMemberChanges diff;
    for (std::map<int64_t, int>::const_iterator it = target.begin(); it != target.end(); ++it)
    {
        std::map<int64_t, int>::const_iterator old = current.find(it->first);
        ShareMemberSpec m;
        m.m_userId = it->first;
        m.m_perms = it->second;
        if (old == current.end())
            diff.m_added.push_back(m);
        else if (old->second != it->second)
            diff.m_updated.push_back(m);
    }
    for (std::map<int64_t, int>::const_iterator it = current.begin(); it != current.end(); ++it)
    {
        if (target.find(it->first) == target.end())
            diff.m_removed.push_back(it->first);
    }

    if (!diff.m_removed.empty())
    {
        std::ostringstream osDelete;
        osDelete << "DELETE FROM t_file_share_member WHERE f_shareid = " << req.m_shareId << " AND f_user_id IN (";
        for (size_t i = 0; i < diff.m_removed.size(); ++i)
        {
            if (i > 0)
                osDelete << ",";
            osDelete << diff.m_removed[i];
        }
        osDelete << ")";
        if (!pConn->execute(osDelete.str().c_str()))
        {
            LOGE("FileShareManager::updateMembers delete error, shareid: %lld", (long long)req.m_shareId);
            return error_code_server_internal;
        }
    }

    if (!insertMembers(pConn.get(), req.m_shareId, diff.m_added, nowMs))
    {
        LOGE("FileShareManager::updateMembers insert error, shareid: %lld", (long long)req.m_shareId);
        return error_code_server_internal;
    }

    for (size_t i = 0; i < diff.m_updated.size(); ++i)
    {
        std::ostringstream osUpdate;
        osUpdate << "UPDATE t_file_share_member SET f_perms = " << diff.m_updated[i].m_perms
                 << " WHERE f_shareid = " << req.m_shareId << " AND f_user_id = " << diff.m_updated[i].m_userId;
        if (!pConn->execute(osUpdate.str().c_str()))
        {
            LOGE("FileShareManager::updateMembers update perms error, shareid: %lld, userid: %lld",
                 (long long)req.m_shareId, (long long)diff.m_updated[i].m_userId);
            return error_code_server_internal;
        }
    }

    if (!transaction.Commit())
    {
        LOGE("FileShareManager::updateMembers commit error, shareid: %lld", (long long)req.m_shareId);
        return error_code_server_internal;
    }

    changes = diff;
    LOGI("update share members ok, shareid: %lld, added: %u, removed: %u, updated: %u",
         (long long)req.m_shareId, static_cast<unsigned>(diff.m_added.size()),
         static_cast<unsigned>(diff.m_removed.size()), static_cast<unsigned>(diff.m_updated.size()));
    return error_code_ok;
}

bool FileShareManager::addAudit(const ShareAuditRecord& rec)
{
    std::unique_ptr<IDatabase> pConn(openConnection("addAudit"));
    if (!pConn)
        return false;

    if (!pConn->execute(auditInsertSql(pConn.get(), rec).c_str()))
    {
        LOGE("FileShareManager::addAudit insert error, shareid: %lld, actor: %lld, action: %d",
             (long long)rec.m_shareId, (long long)rec.m_actorId, rec.m_action);
        return false;
    }
    return true;
}

bool FileShareManager::addAudits(const std::vector<ShareAuditRecord>& recs)
{
    if (recs.empty())
        return true;

    std::unique_ptr<IDatabase> pConn(openConnection("addAudits"));
    if (!pConn)
        return false;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("FileShareManager::addAudits begin transaction error, count: %u", static_cast<unsigned>(recs.size()));
        return false;
    }
    for (size_t i = 0; i < recs.size(); ++i)
    {
        const ShareAuditRecord& rec = recs[i];
        if (!pConn->execute(auditInsertSql(pConn.get(), rec).c_str()))
        {
            LOGE("FileShareManager::addAudits insert error, shareid: %lld, actor: %lld, action: %d, index: %u",
                 (long long)rec.m_shareId, (long long)rec.m_actorId, rec.m_action, static_cast<unsigned>(i));
            return false;
        }
    }
    if (!transaction.Commit())
    {
        LOGE("FileShareManager::addAudits commit error, count: %u", static_cast<unsigned>(recs.size()));
        return false;
    }
    return true;
}

void FileShareManager::setProvideState(int64_t shareId, ShareConnKey conn, bool available, int reason)
{
    ShareProvideState state;
    state.m_available = available;
    state.m_reason = available ? 0 : reason;
    state.m_conn = conn;

    std::lock_guard<std::mutex> guard(m_provideMutex);
    m_provideStates[shareId] = state;
}

bool FileShareManager::getProvideState(int64_t shareId, ShareProvideState& out) const
{
    std::lock_guard<std::mutex> guard(m_provideMutex);
    std::map<int64_t, ShareProvideState>::const_iterator it = m_provideStates.find(shareId);
    if (it == m_provideStates.end())
        return false;
    out = it->second;
    return true;
}

std::vector<int64_t> FileShareManager::clearProvideStatesForConnection(ShareConnKey conn)
{
    std::vector<int64_t> cleared;
    std::lock_guard<std::mutex> guard(m_provideMutex);
    std::map<int64_t, ShareProvideState>::iterator it = m_provideStates.begin();
    while (it != m_provideStates.end())
    {
        if (it->second.m_conn != conn)
        {
            ++it;
            continue;
        }
        cleared.push_back(it->first);
        m_provideStates.erase(it++);
    }
    return cleared;
}

std::vector<ShareOfflineNotice> FileShareManager::takeOfflineNotices(ShareConnKey conn)
{
    std::vector<int64_t> cleared = clearProvideStatesForConnection(conn);
    std::sort(cleared.begin(), cleared.end());

    std::vector<ShareOfflineNotice> notices;
    for (size_t i = 0; i < cleared.size(); ++i)
    {
        ShareOfflineNotice notice;
        if (!getShare(cleared[i], notice.m_share))
        {
            LOGW("FileShareManager::takeOfflineNotices share not readable, shareid: %lld", (long long)cleared[i]);
            continue;
        }
        if (notice.m_share.m_state != kShareRecordActive)
            continue;

        std::vector<ShareMemberSpec> members;
        getMembers(cleared[i], members);
        notice.m_notifyUsers = FileSharePolicy::pushTargets(
            FileSharePolicy::cardAudience(members, notice.m_share.m_ownerId), notice.m_share.m_ownerId);
        notices.push_back(notice);
    }
    return notices;
}

void FileShareManager::clearProvideState(int64_t shareId)
{
    std::lock_guard<std::mutex> guard(m_provideMutex);
    m_provideStates.erase(shareId);
}

bool FileShareManager::getAudits(int64_t shareId, std::vector<ShareAuditRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("getAudits"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << auditColumns("") << " FROM t_file_share_audit WHERE f_shareid = " << shareId << " ORDER BY f_id";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        ShareAuditRecord rec;
        fillAuditRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool FileShareManager::queryAdminShares(const ShareAdminQuery& q, std::vector<ShareAdminView>& out, int64_t& total)
{
    out.clear();
    total = 0;
    if (q.m_limit <= 0 || q.m_offset < 0)
        return false;
    std::unique_ptr<IDatabase> pConn(openConnection("queryAdminShares"));
    if (!pConn)
        return false;

    //分享者、群聊的群名、单聊的对方都用 LEFT JOIN：查不到名字的共享照样列出
    std::ostringstream from;
    from << " FROM t_file_share s"
         << " LEFT JOIN t_user o ON o.f_user_id = s.f_owner_id"
         << " LEFT JOIN t_group g ON s.f_ctype = " << conversation_group << " AND g.f_groupid = s.f_cid"
         << " LEFT JOIN t_user p ON s.f_ctype = " << conversation_single << " AND p.f_user_id = s.f_cid"
         << " WHERE 1 = 1";
    if (q.m_enterpriseId > 0)
        from << " AND s.f_enterprise_id = " << q.m_enterpriseId;
    if (q.m_state != 0)
        from << " AND s.f_state = " << q.m_state;
    if (!q.m_keyword.empty())
    {
        const std::string kw = escapeText(pConn.get(), q.m_keyword);
        from << " AND (o.f_username LIKE '%" << kw << "%' OR o.f_nickname LIKE '%" << kw << "%')";
    }
    if (!queryCount(pConn.get(), "SELECT COUNT(*)" + from.str(), total))
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << shareColumns("s.") << ", o.f_nickname, o.f_username, g.f_name, p.f_nickname,"
          << " (SELECT COUNT(*) FROM t_file_share_member m WHERE m.f_shareid = s.f_shareid)" << from.str()
          << " ORDER BY s.f_shareid DESC LIMIT " << q.m_limit << " OFFSET " << q.m_offset;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        ShareAdminView view;
        fillShareRecord(pRow, view.m_share);
        view.m_ownerName = pRow[kShareColumnCount].getCppString();
        view.m_ownerAccount = pRow[kShareColumnCount + 1].getCppString();
        //所在会话：群聊取群名，单聊取对方昵称
        view.m_conversationName = (view.m_share.m_ctype == conversation_group) ? pRow[kShareColumnCount + 2].getCppString()
                                                                               : pRow[kShareColumnCount + 3].getCppString();
        view.m_memberCount = pRow[kShareColumnCount + 4].getInt32();
        out.push_back(view);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool FileShareManager::getMemberViews(int64_t shareId, std::vector<ShareMemberView>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("getMemberViews"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT m.f_user_id, m.f_perms, u.f_nickname, u.f_username FROM t_file_share_member m"
          << " LEFT JOIN t_user u ON u.f_user_id = m.f_user_id WHERE m.f_shareid = " << shareId << " ORDER BY m.f_user_id";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        ShareMemberView view;
        view.m_userId = static_cast<int64_t>(pRow[0].getUInt64());
        view.m_perms = pRow[1].getInt32();
        view.m_name = pRow[2].getCppString();
        view.m_account = pRow[3].getCppString();
        out.push_back(view);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool FileShareManager::queryAdminAudits(const ShareAuditQuery& q, std::vector<ShareAuditView>& out, int64_t& total)
{
    out.clear();
    total = 0;
    if (q.m_limit <= 0 || q.m_offset < 0)
        return false;
    std::unique_ptr<IDatabase> pConn(openConnection("queryAdminAudits"));
    if (!pConn)
        return false;

    //操作人一般是用户；后台强制取消的那一条操作人是管理员（f_actor_id 为管理员 id），按共享的取消原因区分
    std::ostringstream adminCancel;
    adminCancel << "(a.f_action = " << share_audit_cancel_share << " AND s.f_cancel_reason = " << share_invalid_admin_canceled << ")";
    std::ostringstream from;
    from << " FROM t_file_share_audit a"
         << " LEFT JOIN t_user u ON u.f_user_id = a.f_actor_id"
         << " LEFT JOIN t_user o ON o.f_user_id = a.f_owner_id"
         << " LEFT JOIN t_file_share s ON s.f_shareid = a.f_shareid"
         << " LEFT JOIN t_admin ad ON ad.f_admin_id = a.f_actor_id"
         << " WHERE 1 = 1";
    if (q.m_enterpriseId > 0)
        from << " AND a.f_enterprise_id = " << q.m_enterpriseId;
    if (q.m_action != 0)
        from << " AND a.f_action = " << q.m_action;
    if (q.m_startMs > 0)
        from << " AND a.f_create_time >= " << q.m_startMs;
    if (q.m_endMs > 0)
        from << " AND a.f_create_time <= " << q.m_endMs;
    if (q.m_shareId > 0)
        from << " AND a.f_shareid = " << q.m_shareId;
    if (!q.m_actorKeyword.empty())
    {
        const std::string kw = escapeText(pConn.get(), q.m_actorKeyword);
        from << " AND ((NOT " << adminCancel.str() << " AND (u.f_username LIKE '%" << kw << "%' OR u.f_nickname LIKE '%"
             << kw << "%')) OR (" << adminCancel.str() << " AND ad.f_name LIKE '%" << kw << "%'))";
    }
    if (!queryCount(pConn.get(), "SELECT COUNT(*)" + from.str(), total))
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << auditColumns("a.") << ", u.f_nickname, o.f_nickname, s.f_name, s.f_cancel_reason, ad.f_name"
          << from.str() << " ORDER BY a.f_create_time DESC, a.f_id DESC LIMIT " << q.m_limit << " OFFSET " << q.m_offset;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        ShareAuditView view;
        fillAuditRecord(pRow, view.m_record);
        const std::string userName = pRow[kAuditColumnCount].getCppString();
        view.m_ownerName = pRow[kAuditColumnCount + 1].getCppString();
        view.m_shareName = pRow[kAuditColumnCount + 2].getCppString();
        const int cancelReason = pRow[kAuditColumnCount + 3].getInt32();
        const std::string adminName = pRow[kAuditColumnCount + 4].getCppString();
        view.m_actorIsAdmin = (view.m_record.m_action == share_audit_cancel_share && cancelReason == share_invalid_admin_canceled);
        view.m_actorName = view.m_actorIsAdmin ? adminName : userName;
        out.push_back(view);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}
