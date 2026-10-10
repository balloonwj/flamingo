/**
 *  文件元数据存储管理器实现，FileStoreManager.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileStoreManager.h"
#include <memory>
#include <sstream>
#include <vector>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"   //file_scene 枚举值
#include "../utils/FileToken.h"   //场景归属的判定（getServerForScene）

FileStoreManager::FileStoreManager()
    : m_imgPort(0), m_imgTls(false), m_filePort(0), m_fileTls(false)
{
}

FileStoreManager::~FileStoreManager()
{
}

bool FileStoreManager::init()
{
    return true;
}

void FileStoreManager::configure(const std::string& tokenKey,
                                 const std::string& imgHost, int imgPort, bool imgTls,
                                 const std::string& fileHost, int filePort, bool fileTls)
{
    m_tokenKey = tokenKey;
    m_imgHost  = imgHost;
    m_imgPort  = imgPort;
    m_imgTls   = imgTls;
    m_fileHost = fileHost;
    m_filePort = filePort;
    m_fileTls  = fileTls;
}

const std::string& FileStoreManager::getTokenKey() const
{
    return m_tokenKey;
}

void FileStoreManager::getServerForScene(int scene, std::string& host, int& port, bool& tls) const
{
    //头像、图片消息、云端录像走 imgserver；聊天文件、群文件、升级包走 fileserver。
    //与 fileserver / imgserver 的场景门禁（FileSession.cpp 的 isSceneAllowedOnServer）共用 FileToken::isImageScene，
    //新增场景时两边不会不一致（2026-10-07 之前这里只列了头像与图片消息，云端录像的令牌被指向 fileserver 而遭拒绝）
    if (FileToken::isImageScene(scene))
    {
        host = m_imgHost;
        port = m_imgPort;
        tls  = m_imgTls;
    }
    else
    {
        host = m_fileHost;
        port = m_filePort;
        tls  = m_fileTls;
    }
}

bool FileStoreManager::findCompletedByMd5(const std::string& md5, int scene, FileRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FileStoreManager::findCompletedByMd5 db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //md5 为十六进制串，仍统一经转义以防御异常输入
    std::vector<char> escMd5(md5.size() * 2 + 1, '\0');
    pConn->escapeString(&escMd5[0], md5.c_str(), static_cast<uint32_t>(md5.size()));

    //只取同场景的记录：跨场景复用会让接收方连错服务器或被群文件鉴权拒绝，
    //详见头文件中本函数的注释。scene 是整数，直接拼接即可。
    std::ostringstream osSql;
    osSql << "SELECT f_id, f_md5, f_name, f_ext, f_size, f_scene, f_uploaderid, "
             "f_status, f_createtime FROM t_file WHERE f_md5 = '" << &escMd5[0]
          << "' AND f_scene = " << scene
          << " AND f_status = " << file_status_completed << " LIMIT 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.fileid     = static_cast<int64_t>(pRow[0].getUInt64());
        out.md5        = pRow[1].getCppString();
        out.name       = pRow[2].getCppString();
        out.ext        = pRow[3].getCppString();
        out.size       = static_cast<int64_t>(pRow[4].getUInt64());
        out.scene      = pRow[5].getInt32();
        out.uploaderid = static_cast<int64_t>(pRow[6].getUInt64());
        out.status     = pRow[7].getInt32();
        out.createtime = static_cast<int64_t>(pRow[8].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

int64_t FileStoreManager::createPendingFile(const std::string& md5, const std::string& name,
                                            const std::string& ext, int64_t size, int scene,
                                            int64_t uploaderid, int64_t now)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FileStoreManager::createPendingFile db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //md5 / name / ext 均经 escapeString 转义后才拼进 SQL
    std::vector<char> escMd5(md5.size() * 2 + 1, '\0');
    pConn->escapeString(&escMd5[0], md5.c_str(), static_cast<uint32_t>(md5.size()));
    std::vector<char> escName(name.size() * 2 + 1, '\0');
    pConn->escapeString(&escName[0], name.c_str(), static_cast<uint32_t>(name.size()));
    std::vector<char> escExt(ext.size() * 2 + 1, '\0');
    pConn->escapeString(&escExt[0], ext.c_str(), static_cast<uint32_t>(ext.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_file(f_md5, f_name, f_ext, f_size, f_scene, f_uploaderid, "
             "f_status, f_createtime) VALUES('"
          << &escMd5[0] << "', '" << &escName[0] << "', '" << &escExt[0] << "', "
          << static_cast<long long>(size) << ", " << scene << ", "
          << static_cast<long long>(uploaderid) << ", " << file_status_pending << ", "
          << static_cast<long long>(now) << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FileStoreManager::createPendingFile insert error, md5: %s, uploaderid: %lld",
             md5.c_str(), static_cast<long long>(uploaderid));
        return 0;
    }

    //取回自增主键 f_id 作为 fileid（须与 INSERT 同一连接）
    int64_t fileid = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            fileid = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }
    return fileid;
}

bool FileStoreManager::getFileById(int64_t fileid, FileRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FileStoreManager::getFileById db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_id, f_md5, f_name, f_ext, f_size, f_scene, f_uploaderid, "
             "f_status, f_createtime FROM t_file WHERE f_id = "
          << static_cast<long long>(fileid) << " LIMIT 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.fileid     = static_cast<int64_t>(pRow[0].getUInt64());
        out.md5        = pRow[1].getCppString();
        out.name       = pRow[2].getCppString();
        out.ext        = pRow[3].getCppString();
        out.size       = static_cast<int64_t>(pRow[4].getUInt64());
        out.scene      = pRow[5].getInt32();
        out.uploaderid = static_cast<int64_t>(pRow[6].getUInt64());
        out.status     = pRow[7].getInt32();
        out.createtime = static_cast<int64_t>(pRow[8].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool FileStoreManager::markCompleted(int64_t fileid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FileStoreManager::markCompleted db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_file SET f_status = " << file_status_completed
          << " WHERE f_id = " << static_cast<long long>(fileid);
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FileStoreManager::markCompleted error, fileid: %lld",
             static_cast<long long>(fileid));
        return false;
    }
    return true;
}

bool FileStoreManager::markCompletedByUploader(int64_t fileid, int64_t uploaderid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("FileStoreManager::markCompletedByUploader db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //上传者条件写进 WHERE：不是上传者本人时一行都不改，但这不算出错。两个参数都是
    //整数，直接拼接即可。
    std::ostringstream osSql;
    osSql << "UPDATE t_file SET f_status = " << file_status_completed
          << " WHERE f_id = " << static_cast<long long>(fileid)
          << " AND f_uploaderid = " << static_cast<long long>(uploaderid);
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("FileStoreManager::markCompletedByUploader error, fileid: %lld, uploaderid: %lld",
             static_cast<long long>(fileid), static_cast<long long>(uploaderid));
        return false;
    }
    return true;
}
