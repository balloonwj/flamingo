/**
 *  云端录像的存储层，见 CloudRecordStore.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordStore.h"

#include <memory>
#include <sstream>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbTransaction.h"
#include "../dbapi/IDatabase.h"
#include "Protocol.h"

namespace {

//t_cloud_record 全部列，顺序与 readRow 一致
const char* const kRecordColumns =
    "f_id, f_enterprise_id, f_owner, f_source, f_roomid, f_ctype, f_cid, f_title, f_start_time, f_end_time, "
    "f_duration_ms, f_frec_bytes, f_bytes, f_audio_only, f_status, f_frec_file, f_file, f_expire_time, f_warned, "
    "f_create_time";

//列在 kRecordColumns 里的下标
enum record_column
{
    col_id = 0,           //f_id
    col_enterprise,       //f_enterprise_id
    col_owner,            //f_owner
    col_source,           //f_source
    col_roomid,           //f_roomid
    col_ctype,            //f_ctype
    col_cid,              //f_cid
    col_title,            //f_title
    col_start_time,       //f_start_time
    col_end_time,         //f_end_time
    col_duration,         //f_duration_ms
    col_frec_bytes,       //f_frec_bytes
    col_bytes,            //f_bytes
    col_audio_only,       //f_audio_only
    col_status,           //f_status
    col_frec_file,        //f_frec_file
    col_file,             //f_file
    col_expire_time,      //f_expire_time
    col_warned,           //f_warned
    col_create_time,      //f_create_time
};

IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("CloudRecordStore::%s db init failed, db: %s", caller, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

std::string escapeText(IDatabase* pConn, const std::string& text)
{
    std::vector<char> buf(text.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], text.c_str(), static_cast<uint32_t>(text.size()));
    return std::string(&buf[0]);
}

int64_t toInt64(const Field& f)
{
    return atoll(f.getString().c_str());
}

void readRow(Field* pRow, CloudRecordRow& row)
{
    row.m_id = toInt64(pRow[col_id]);
    row.m_enterpriseId = toInt64(pRow[col_enterprise]);
    row.m_owner = toInt64(pRow[col_owner]);
    row.m_source = pRow[col_source].getInt32();
    row.m_roomId = pRow[col_roomid].getString();
    row.m_ctype = pRow[col_ctype].getInt32();
    row.m_cid = toInt64(pRow[col_cid]);
    row.m_title = pRow[col_title].getString();
    row.m_startTime = toInt64(pRow[col_start_time]);
    row.m_endTime = toInt64(pRow[col_end_time]);
    row.m_durationMs = toInt64(pRow[col_duration]);
    row.m_frecBytes = toInt64(pRow[col_frec_bytes]);
    row.m_bytes = toInt64(pRow[col_bytes]);
    row.m_audioOnly = pRow[col_audio_only].getInt32() != 0;
    row.m_status = pRow[col_status].getInt32();
    row.m_frecFile = pRow[col_frec_file].getString();
    row.m_file = pRow[col_file].getString();
    row.m_expireTime = toInt64(pRow[col_expire_time]);
    row.m_warned = pRow[col_warned].getInt32() != 0;
    row.m_createTime = toInt64(pRow[col_create_time]);
}

//执行一条查询并把结果读成录像列表
bool queryRows(IDatabase* pConn, const std::string& sql, std::vector<CloudRecordRow>& out)
{
    out.clear();
    QueryResult* pResult = pConn->query(sql.c_str());
    //两种后端在零行时都返回空结果集，返回 NULL 只表示出错（后端已记日志）
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        CloudRecordRow row;
        readRow(pRow, row);
        out.push_back(row);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

//执行一条写语句；失败时记日志
bool exec(IDatabase* pConn, const std::string& sql, const char* caller)
{
    if (pConn->execute(sql.c_str()))
        return true;
    LOGE("CloudRecordStore::%s execute failed: %s", caller, sql.c_str());
    return false;
}

} // namespace

bool CloudRecordStore::insert(CloudRecordRow& row)
{
    std::unique_ptr<IDatabase> pConn(openConnection("insert"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "INSERT INTO t_cloud_record (f_enterprise_id, f_owner, f_source, f_roomid, f_ctype, f_cid, f_title, "
           "f_start_time, f_end_time, f_duration_ms, f_frec_bytes, f_bytes, f_audio_only, f_status, f_frec_file, f_file, "
           "f_expire_time, f_warned, f_create_time) VALUES ("
        << row.m_enterpriseId << ", " << row.m_owner << ", " << row.m_source << ", '"
        << escapeText(pConn.get(), row.m_roomId) << "', " << row.m_ctype << ", " << row.m_cid << ", '"
        << escapeText(pConn.get(), row.m_title) << "', " << row.m_startTime << ", " << row.m_endTime << ", "
        << row.m_durationMs << ", " << row.m_frecBytes << ", " << row.m_bytes << ", " << (row.m_audioOnly ? 1 : 0)
        << ", " << row.m_status << ", '" << escapeText(pConn.get(), row.m_frecFile) << "', '"
        << escapeText(pConn.get(), row.m_file) << "', " << row.m_expireTime << ", " << (row.m_warned ? 1 : 0) << ", "
        << row.m_createTime << ")";
    if (!exec(pConn.get(), sql.str(), "insert"))
        return false;
    row.m_id = static_cast<int64_t>(pConn->getInsertID());
    LOGI("cloud record inserted, id: %lld, owner: %lld, source: %d, room: %s", (long long)row.m_id,
         (long long)row.m_owner, row.m_source, row.m_roomId.c_str());
    return row.m_id > 0;
}

bool CloudRecordStore::get(int64_t id, CloudRecordRow& out, bool& found)
{
    found = false;
    std::unique_ptr<IDatabase> pConn(openConnection("get"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT " << kRecordColumns << " FROM t_cloud_record WHERE f_id = " << id;
    std::vector<CloudRecordRow> rows;
    if (!queryRows(pConn.get(), sql.str(), rows))
        return false;
    if (!rows.empty())
    {
        out = rows[0];
        found = true;
    }
    return true;
}

bool CloudRecordStore::listByOwner(int64_t owner, int64_t beforeId, int limit, std::vector<CloudRecordRow>& out,
                                   bool& hasMore)
{
    hasMore = false;
    std::unique_ptr<IDatabase> pConn(openConnection("listByOwner"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    //多取一条，据此判断还有没有更早的
    sql << "SELECT " << kRecordColumns << " FROM t_cloud_record WHERE f_owner = " << owner;
    if (beforeId > 0)
        sql << " AND f_id < " << beforeId;
    sql << " ORDER BY f_id DESC LIMIT " << (limit + 1);
    if (!queryRows(pConn.get(), sql.str(), out))
        return false;
    if (static_cast<int>(out.size()) > limit)
    {
        hasMore = true;
        out.resize(static_cast<size_t>(limit));
    }
    return true;
}

bool CloudRecordStore::finishRecording(int64_t id, int status, int64_t endTime, int64_t durationMs, int64_t frecBytes,
                                       int64_t expireTime, bool& changed)
{
    changed = false;
    std::unique_ptr<IDatabase> pConn(openConnection("finishRecording"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_status = " << status << ", f_end_time = " << endTime << ", f_duration_ms = "
        << durationMs << ", f_frec_bytes = " << frecBytes << ", f_expire_time = " << expireTime
        << " WHERE f_id = " << id << " AND f_status = " << cloudrecord_status_recording;
    uint32_t affected = 0;
    int err = 0;
    if (!pConn->execute(sql.str().c_str(), affected, err))
    {
        LOGE("CloudRecordStore::finishRecording failed, id: %lld, errno: %d", (long long)id, err);
        return false;
    }
    changed = affected > 0;
    return true;
}

bool CloudRecordStore::markReady(int64_t id, const std::string& file, int64_t bytes, int64_t durationMs,
                                 int64_t expireTime)
{
    std::unique_ptr<IDatabase> pConn(openConnection("markReady"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_status = " << cloudrecord_status_ready << ", f_file = '"
        << escapeText(pConn.get(), file) << "', f_bytes = " << bytes;
    if (durationMs > 0)
        sql << ", f_duration_ms = " << durationMs;
    if (expireTime > 0)
        sql << ", f_expire_time = " << expireTime;
    sql << " WHERE f_id = " << id;
    return exec(pConn.get(), sql.str(), "markReady");
}

bool CloudRecordStore::clearFrec(int64_t id)
{
    std::unique_ptr<IDatabase> pConn(openConnection("clearFrec"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_frec_file = '', f_frec_bytes = 0 WHERE f_id = " << id;
    return exec(pConn.get(), sql.str(), "clearFrec");
}

bool CloudRecordStore::rename(int64_t id, const std::string& title)
{
    std::unique_ptr<IDatabase> pConn(openConnection("rename"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_title = '" << escapeText(pConn.get(), title) << "' WHERE f_id = " << id;
    return exec(pConn.get(), sql.str(), "rename");
}

bool CloudRecordStore::remove(int64_t id)
{
    std::unique_ptr<IDatabase> pConn(openConnection("remove"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("CloudRecordStore::remove begin transaction error, id: %lld", (long long)id);
        return false;
    }
    std::ostringstream delShares;
    delShares << "DELETE FROM t_cloud_record_share WHERE f_recordid = " << id;
    std::ostringstream delRecord;
    delRecord << "DELETE FROM t_cloud_record WHERE f_id = " << id;
    if (!exec(pConn.get(), delShares.str(), "remove") || !exec(pConn.get(), delRecord.str(), "remove") ||
        !transaction.Commit())
        return false;
    LOGI("cloud record removed, id: %lld", (long long)id);
    return true;
}

bool CloudRecordStore::usedBytes(int64_t enterpriseId, int64_t& out)
{
    out = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("usedBytes"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT COALESCE(SUM(f_frec_bytes + f_bytes), 0) FROM t_cloud_record WHERE f_enterprise_id = " << enterpriseId;
    QueryResult* pResult = pConn->query(sql.str().c_str());
    if (pResult == NULL)
        return false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        out = toInt64(pRow[0]);
    pResult->endQuery();
    delete pResult;
    return true;
}

bool CloudRecordStore::setFiles(int64_t id, const std::string& frecFile, const std::string& file, int64_t bytes)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setFiles"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_frec_file = '" << escapeText(pConn.get(), frecFile) << "', f_file = '"
        << escapeText(pConn.get(), file) << "', f_bytes = " << bytes << " WHERE f_id = " << id;
    return exec(pConn.get(), sql.str(), "setFiles");
}

bool CloudRecordStore::listExpired(int64_t nowMs, int limit, std::vector<CloudRecordRow>& out)
{
    std::unique_ptr<IDatabase> pConn(openConnection("listExpired"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT " << kRecordColumns << " FROM t_cloud_record WHERE f_expire_time > 0 AND f_expire_time <= " << nowMs
        << " ORDER BY f_expire_time LIMIT " << limit;
    return queryRows(pConn.get(), sql.str(), out);
}

bool CloudRecordStore::listToWarn(int64_t nowMs, int64_t warnBeforeMs, int limit, std::vector<CloudRecordRow>& out)
{
    std::unique_ptr<IDatabase> pConn(openConnection("listToWarn"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT " << kRecordColumns << " FROM t_cloud_record WHERE f_warned = 0 AND f_expire_time > " << nowMs
        << " AND f_expire_time <= " << (nowMs + warnBeforeMs) << " ORDER BY f_expire_time LIMIT " << limit;
    return queryRows(pConn.get(), sql.str(), out);
}

bool CloudRecordStore::markWarned(int64_t id)
{
    std::unique_ptr<IDatabase> pConn(openConnection("markWarned"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "UPDATE t_cloud_record SET f_warned = 1 WHERE f_id = " << id;
    return exec(pConn.get(), sql.str(), "markWarned");
}

bool CloudRecordStore::listByStatus(int status, std::vector<CloudRecordRow>& out)
{
    std::unique_ptr<IDatabase> pConn(openConnection("listByStatus"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT " << kRecordColumns << " FROM t_cloud_record WHERE f_status = " << status << " ORDER BY f_id";
    return queryRows(pConn.get(), sql.str(), out);
}

bool CloudRecordStore::addShare(const CloudRecordShareRow& share)
{
    std::unique_ptr<IDatabase> pConn(openConnection("addShare"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;
    //两种数据库都支持的写法：先删后插（主键为 录像 id + 会话类型 + 会话 id）
    std::ostringstream del;
    del << "DELETE FROM t_cloud_record_share WHERE f_recordid = " << share.m_recordId << " AND f_ctype = "
        << share.m_ctype << " AND f_cid = " << share.m_cid;
    std::ostringstream ins;
    ins << "INSERT INTO t_cloud_record_share (f_recordid, f_ctype, f_cid, f_sender, f_send_time) VALUES ("
        << share.m_recordId << ", " << share.m_ctype << ", " << share.m_cid << ", " << share.m_sender << ", "
        << share.m_sendTime << ")";
    return exec(pConn.get(), del.str(), "addShare") && exec(pConn.get(), ins.str(), "addShare") && transaction.Commit();
}

bool CloudRecordStore::listShares(int64_t recordId, std::vector<CloudRecordShareRow>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listShares"));
    if (!pConn)
        return false;
    std::ostringstream sql;
    sql << "SELECT f_recordid, f_ctype, f_cid, f_sender, f_send_time FROM t_cloud_record_share WHERE f_recordid = "
        << recordId;
    QueryResult* pResult = pConn->query(sql.str().c_str());
    if (pResult == NULL)
        return false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        CloudRecordShareRow s;
        s.m_recordId = toInt64(pRow[0]);
        s.m_ctype = pRow[1].getInt32();
        s.m_cid = toInt64(pRow[2]);
        s.m_sender = toInt64(pRow[3]);
        s.m_sendTime = toInt64(pRow[4]);
        out.push_back(s);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}
