/**
 *  客户端版本发布记录管理器实现，ClientVersionManager.cpp
 *  balloonwj@qq.com   2026-08-11
 */
#include "ClientVersionManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"   //EscapeString
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"              //enum error_code

//f_enabled 的"已发布"取值。草稿（0）不下发给任何客户端，
//便于先把记录插好、核对无误后再置 1 放出去。
#define CLIENT_VERSION_ENABLED   1
//f_enabled 的"草稿"取值
#define CLIENT_VERSION_DRAFT     0

//MD5 十六进制串的长度，单位字符。入库前按它卡一道，避免把明显不对的值写进去
//——客户端拿这个值去 fileserver 反查文件，错了就是下载失败。
static const size_t CLIENT_VERSION_MD5_LEN = 32;

//列出记录时统一的查询列顺序，各处解析按同一顺序取值，改动时两边一起改
#define CLIENT_VERSION_COLUMNS \
    "f_id, f_platform, f_version, f_min_version, f_release_note, f_package_md5, " \
    "f_package_size, f_package_name, f_forced, f_enabled, f_publish_time, f_creator_adminid, f_create_time"

//判断一个串是不是长度合规的十六进制 MD5
static bool isValidMd5(const std::string& md5)
{
    if (md5.size() != CLIENT_VERSION_MD5_LEN)
        return false;

    for (size_t i = 0; i < md5.size(); ++i)
    {
        const char c = md5[i];
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isLowerHex = (c >= 'a' && c <= 'f');
        const bool isUpperHex = (c >= 'A' && c <= 'F');
        if (!isDigit && !isLowerHex && !isUpperHex)
            return false;
    }
    return true;
}

//按 CLIENT_VERSION_COLUMNS 的列顺序把一行结果读进记录结构
static void readRecord(Field* pRow, ClientVersionRecord& out)
{
    out.id             = static_cast<int64_t>(pRow[0].getUInt64());
    out.platform       = pRow[1].getInt32();
    out.version        = pRow[2].getCppString();
    out.minVersion     = pRow[3].getCppString();
    out.releaseNote    = pRow[4].getCppString();
    out.packageMd5     = pRow[5].getCppString();
    out.packageSize    = static_cast<int64_t>(pRow[6].getUInt64());
    out.packageName    = pRow[7].getCppString();
    //注意：f_forced 插在 f_enabled 之前，故从这里往后各列下标整体后移一位。
    //列清单 CLIENT_VERSION_COLUMNS 与本函数必须同步改，改一处漏一处会读串列。
    out.forced         = pRow[8].getInt32();
    out.enabled        = pRow[9].getInt32();
    out.publishTime    = static_cast<int64_t>(pRow[10].getUInt64());
    out.creatorAdminId = static_cast<int64_t>(pRow[11].getUInt64());
    out.createTime     = static_cast<int64_t>(pRow[12].getUInt64());
}

//确认某条记录存在。用于增改前的存在性判定，好把"记录不存在"与"库不可用"分开报。
//outExists 仅在返回 true 时有效。
static bool recordExists(IDatabase* pConn, int64_t id, bool& outExists)
{
    outExists = false;

    std::ostringstream osSql;
    osSql << "SELECT f_id FROM t_client_version WHERE f_id = " << id;

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //dbapi 对"空结果集"与"查询出错"同样返回 NULL，此处按"不存在"处理：
        //调用方据此回 error_code_not_found，比笼统地报服务器内部错误更贴近真相
        return true;
    }

    outExists = (pResult->fetch() != NULL);
    pResult->endQuery();
    delete pResult;
    return true;
}

ClientVersionManager::ClientVersionManager()
{
}

ClientVersionManager::~ClientVersionManager()
{
}

bool ClientVersionManager::getLatestVersion(int32_t platform, ClientVersionInfo& out)
{
    out = ClientVersionInfo();

    if (platform <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ClientVersionManager::getLatestVersion db init failed, platform: %d, db: %s",
             (int)platform, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //命中 idx_platform_enabled；发布时间相同时按 f_id 兜底，保证结果确定。
    //platform 是本进程内构造的整数，不经用户文本，故直接拼接无注入面。
    std::ostringstream osSql;
    osSql << "SELECT f_version, f_min_version, f_release_note, f_package_md5, "
          << "f_package_size, f_package_name, f_forced FROM t_client_version"
          << " WHERE f_platform = " << platform
          << " AND f_enabled = " << CLIENT_VERSION_ENABLED
          << " ORDER BY f_publish_time DESC, f_id DESC LIMIT 1";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //dbapi 在"结果集为空"与"查询出错"两种情况下同样返回 NULL，此处无从区分。
        //连接失败已在上面那个分支里报过，走到这里绝大多数是该平台还没发过版本，
        //按 INFO 记录即可，不必按错误告警。
        LOGI("ClientVersionManager::getLatestVersion no published version, platform: %d",
             (int)platform);
        return false;
    }

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.version     = pRow[0].getCppString();
        out.minVersion  = pRow[1].getCppString();
        out.releaseNote = pRow[2].getCppString();
        out.packageMd5  = pRow[3].getCppString();
        out.packageSize = static_cast<int64_t>(pRow[4].getUInt64());
        out.packageName = pRow[5].getCppString();
        out.forced      = pRow[6].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;

    if (!found)
    {
        LOGI("ClientVersionManager::getLatestVersion empty row, platform: %d", (int)platform);
        return false;
    }

    LOGI("ClientVersionManager::getLatestVersion ok, platform: %d, version: %s, md5: %s, size: %lld",
         (int)platform, out.version.c_str(), out.packageMd5.c_str(), (long long)out.packageSize);
    return true;
}

bool ClientVersionManager::listVersions(int32_t platform, std::vector<ClientVersionRecord>& out)
{
    out.clear();

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ClientVersionManager::listVersions db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //草稿也要列出来：控制台得能看到没放出去的记录并继续编辑，
    //这与"下发给客户端"的取数规则（只取 f_enabled=1）不是一回事。
    std::ostringstream osSql;
    osSql << "SELECT " << CLIENT_VERSION_COLUMNS << " FROM t_client_version";
    if (platform > 0)
        osSql << " WHERE f_platform = " << platform;
    osSql << " ORDER BY f_publish_time DESC, f_id DESC";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //空结果集与查询出错在 dbapi 里同样返回 NULL，一并按"没有记录"处理
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        ClientVersionRecord rec;
        readRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ClientVersionManager::createVersion(const ClientVersionRecord& rec, int64_t nowMs,
                                         int64_t& outId, int& outErrorCode)
{
    outId = 0;
    outErrorCode = error_code_ok;

    //这四个值直接决定客户端能不能下到包，与其写进库等着客户端下载失败，不如在入口拦掉
    if (rec.platform <= 0 || rec.version.empty()
        || !isValidMd5(rec.packageMd5) || rec.packageSize <= 0)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ClientVersionManager::createVersion db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    //版本号、说明、文件名都可能含单引号 / 反斜杠 / 中文，必须转义后再拼进 SQL
    const std::string escVersion  = dbapi::EscapeString(rec.version);
    const std::string escMinVer   = dbapi::EscapeString(rec.minVersion);
    const std::string escNote     = dbapi::EscapeString(rec.releaseNote);
    const std::string escMd5      = dbapi::EscapeString(rec.packageMd5);
    const std::string escPkgName  = dbapi::EscapeString(rec.packageName);

    //草稿的发布时间留 0：它还没被放出去，写一个时间进去只会让列表页显示得莫名其妙
    const int enabled = (rec.enabled == CLIENT_VERSION_ENABLED)
                            ? CLIENT_VERSION_ENABLED : CLIENT_VERSION_DRAFT;
    const int64_t publishTime = (enabled == CLIENT_VERSION_ENABLED) ? nowMs : 0;

    std::ostringstream osSql;
    osSql << "INSERT INTO t_client_version(f_platform, f_version, f_min_version, f_release_note,"
          << " f_package_md5, f_package_size, f_package_name, f_forced, f_enabled, f_publish_time,"
          << " f_creator_adminid, f_create_time) VALUES("
          << rec.platform
          << ", '" << escVersion << "'"
          << ", '" << escMinVer << "'"
          << ", '" << escNote << "'"
          << ", '" << escMd5 << "'"
          << ", " << rec.packageSize
          << ", '" << escPkgName << "'"
          << ", " << (rec.forced != 0 ? 1 : 0)
          << ", " << enabled
          << ", " << publishTime
          << ", " << rec.creatorAdminId
          << ", " << nowMs << ")";

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ClientVersionManager::createVersion insert error, version: %s", rec.version.c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    outId = static_cast<int64_t>(pConn->getInsertID());
    LOGI("create client version ok, id: %lld, platform: %d, version: %s, enabled: %d, md5: %s",
         (long long)outId, (int)rec.platform, rec.version.c_str(), enabled, rec.packageMd5.c_str());
    return true;
}

bool ClientVersionManager::updateVersion(int64_t id, const std::string& minVersion,
                                         const std::string& releaseNote, int32_t forced,
                                         int& outErrorCode)
{
    outErrorCode = error_code_ok;

    if (id <= 0)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ClientVersionManager::updateVersion db init failed, id: %lld", (long long)id);
        outErrorCode = error_code_server_internal;
        return false;
    }

    bool exists = false;
    if (!recordExists(pConn.get(), id, exists) || !exists)
    {
        outErrorCode = error_code_not_found;
        return false;
    }

    //更新说明与最低版本号都按传入值写入，空串同样是合法值：最低版本为空表示不另设
    //门槛，与新建时留空的含义一致。控制台编辑弹窗会预填当前值，传来空串说明是有意清空。
    //forced 取 CLIENT_VERSION_FORCED_KEEP 时不动 f_forced，其余值按「非 0 即强制」写入。
    std::ostringstream osSql;
    osSql << "UPDATE t_client_version SET f_release_note = '"
          << dbapi::EscapeString(releaseNote) << "'"
          << ", f_min_version = '" << dbapi::EscapeString(minVersion) << "'";
    if (forced != CLIENT_VERSION_FORCED_KEEP)
        osSql << ", f_forced = " << (forced != 0 ? 1 : 0);
    osSql << " WHERE f_id = " << id;

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ClientVersionManager::updateVersion error, id: %lld", (long long)id);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("update client version ok, id: %lld, minversion: %s, forced: %d",
         (long long)id, minVersion.c_str(), (int)forced);
    return true;
}

bool ClientVersionManager::setEnabled(int64_t id, bool enabled, int64_t nowMs, int& outErrorCode)
{
    outErrorCode = error_code_ok;

    if (id <= 0)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ClientVersionManager::setEnabled db init failed, id: %lld", (long long)id);
        outErrorCode = error_code_server_internal;
        return false;
    }

    bool exists = false;
    if (!recordExists(pConn.get(), id, exists) || !exists)
    {
        outErrorCode = error_code_not_found;
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_client_version SET f_enabled = "
          << (enabled ? CLIENT_VERSION_ENABLED : CLIENT_VERSION_DRAFT);
    //发布（含回滚到旧版本）把发布时间刷到当前，使该条立刻成为取数规则下的"最新发布"；
    //撤回不动这一列——它在 f_enabled 为 0 时本就不参与取数，留着还能看出上次何时放出去过。
    if (enabled)
        osSql << ", f_publish_time = " << nowMs;
    osSql << " WHERE f_id = " << id;

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ClientVersionManager::setEnabled error, id: %lld, enabled: %d",
             (long long)id, enabled ? 1 : 0);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("set client version enabled ok, id: %lld, enabled: %d, publishtime: %lld",
         (long long)id, enabled ? 1 : 0, (long long)(enabled ? nowMs : 0));
    return true;
}
