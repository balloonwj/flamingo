/**
 *  群文件存储管理器实现，GroupFileManager.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "GroupFileManager.h"
#include <memory>
#include <sstream>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

GroupFileManager::GroupFileManager()
{
}

GroupFileManager::~GroupFileManager()
{
}

bool GroupFileManager::init()
{
    return true;
}

int64_t GroupFileManager::addGroupFile(int64_t groupid, int64_t fileid, int filetype,
                                       int64_t uploaderid, int64_t now)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFileManager::addGroupFile db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //各字段均为整型，无字符串拼接，无需转义
    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_file(f_groupid, f_fileid, f_filetype, f_uploaderid, "
             "f_uploadtime) VALUES("
          << static_cast<long long>(groupid) << ", "
          << static_cast<long long>(fileid) << ", "
          << filetype << ", "
          << static_cast<long long>(uploaderid) << ", "
          << static_cast<long long>(now) << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupFileManager::addGroupFile insert error, groupid: %lld, fileid: %lld",
             static_cast<long long>(groupid), static_cast<long long>(fileid));
        return 0;
    }

    //取回自增主键 f_id 作为 entryid（须与 INSERT 同一连接）
    int64_t entryid = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        //query() 返回前已 nextRow() 把游标停在第 1 行，故直接 fetch() 即取该行；
        //0 行时 query() 内 endQuery 已把 m_CurrentRow 置 NULL，fetch() 返回 NULL。
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            entryid = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }
    return entryid;
}

bool GroupFileManager::getGroupFiles(int64_t groupid, int filetype, int offset, int count,
                                     std::vector<GroupFileEntry>& out, int& total)
{
    out.clear();
    total = 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFileManager::getGroupFiles db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //group_file_type_all(0) 表示不筛选类别；其余值按 f_filetype 精确匹配
    std::ostringstream osFilter;
    osFilter << " WHERE f_groupid = " << static_cast<long long>(groupid);
    if (filetype != group_file_type_all)
        osFilter << " AND f_filetype = " << filetype;
    std::string filter = osFilter.str();

    //先查该群该筛选条件下的文件总数，供客户端分页
    std::ostringstream osCount;
    osCount << "SELECT COUNT(*) FROM t_group_file" << filter;
    QueryResult* pCountResult = pConn->query(osCount.str().c_str());
    if (pCountResult == NULL)
        return false;
    //query() 已停在第 1 行，直接 fetch() 取 COUNT 值；0 行时 fetch() 返回 NULL。
    Field* pCountRow = pCountResult->fetch();
    if (pCountRow != NULL)
        total = pCountRow[0].getInt32();
    pCountResult->endQuery();
    delete pCountResult;

    //再分页取本页明细：t_group_file JOIN t_file 取文件元数据，按登记时间倒序。
    //WHERE 子句里 t_group_file 的列无别名歧义，复用 filter；JOIN 不影响其语义。
    std::ostringstream osSql;
    osSql << "SELECT gf.f_id, gf.f_fileid, gf.f_filetype, gf.f_uploaderid, gf.f_uploadtime, "
             "f.f_name, f.f_ext, f.f_size, f.f_md5 "
             "FROM t_group_file gf JOIN t_file f ON gf.f_fileid = f.f_id"
          << filter
          << " ORDER BY gf.f_uploadtime DESC, gf.f_id DESC LIMIT "
          << offset << ", " << count;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    //query() 返回前已 nextRow() 把游标停在第 1 行：先 fetch() 取当前行、处理，循环体
    //末尾再 nextRow() 前进（取完返回 false 退出）——与 MessageManager 等一致。原 bug 是
    //漏了末尾 nextRow()，对第 1 行无限 push_back，撑爆内存触发 bad_alloc 崩溃。
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupFileEntry e;
        e.entryid    = static_cast<int64_t>(pRow[0].getUInt64());
        e.fileid     = static_cast<int64_t>(pRow[1].getUInt64());
        e.filetype   = pRow[2].getInt32();
        e.uploaderid = static_cast<int64_t>(pRow[3].getUInt64());
        e.uploadtime = static_cast<int64_t>(pRow[4].getUInt64());
        e.name       = pRow[5].getCppString();
        e.ext        = pRow[6].getCppString();
        e.size       = static_cast<int64_t>(pRow[7].getUInt64());
        e.md5        = pRow[8].getCppString();
        //uploadername 由会话层从 UserManager 补全，此处留空
        out.push_back(e);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupFileManager::getEntryById(int64_t entryid, int64_t& groupid,
                                    int64_t& fileid, int64_t& uploaderid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFileManager::getEntryById db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_groupid, f_fileid, f_uploaderid FROM t_group_file WHERE f_id = "
          << static_cast<long long>(entryid) << " LIMIT 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    //query() 已停在第 1 行，直接 fetch() 取该行；0 行时 fetch() 返回 NULL。
    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        groupid    = static_cast<int64_t>(pRow[0].getUInt64());
        fileid     = static_cast<int64_t>(pRow[1].getUInt64());
        uploaderid = static_cast<int64_t>(pRow[2].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool GroupFileManager::deleteGroupFile(int64_t entryid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFileManager::deleteGroupFile db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_group_file WHERE f_id = " << static_cast<long long>(entryid);
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupFileManager::deleteGroupFile error, entryid: %lld",
             static_cast<long long>(entryid));
        return false;
    }
    return true;
}

bool GroupFileManager::checkDownloadAccess(int64_t fileid, int64_t userid,
                                           bool& isGroupFile, bool& isMember)
{
    isGroupFile = false;
    isMember    = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupFileManager::checkDownloadAccess db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //一次查询取两个布尔：该文件是否为群文件、调用者是否为持有它的某群成员。
    //EXISTS 子查询命中 idx_fileid，不扫全表。
    std::ostringstream osSql;
    osSql << "SELECT "
             "EXISTS(SELECT 1 FROM t_group_file WHERE f_fileid = "
          << static_cast<long long>(fileid) << "), "
             "EXISTS(SELECT 1 FROM t_group_file gf JOIN t_group_member gm "
             "ON gf.f_groupid = gm.f_groupid WHERE gf.f_fileid = "
          << static_cast<long long>(fileid) << " AND gm.f_userid = "
          << static_cast<long long>(userid) << ")";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    //query() 已停在第 1 行，直接 fetch() 取该行；0 行时 fetch() 返回 NULL。
    bool ok = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        isGroupFile = (pRow[0].getInt32() != 0);
        isMember    = (pRow[1].getInt32() != 0);
        ok = true;
    }
    pResult->endQuery();
    delete pResult;
    return ok;
}
