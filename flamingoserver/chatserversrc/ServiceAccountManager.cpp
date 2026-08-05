/**
 *  企业服务号管理器实现，ServiceAccountManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "ServiceAccountManager.h"
#include <memory>
#include <stdio.h>
#include "Msg.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

//拼装 SQL 的字符缓冲区大小。
//最长语句为建服务号 INSERT：名称(转义后≤128) + 描述(转义后≤512) 加关键字，
//总长远小于 2048，取 2048 留足余量。
#define SVC_SQL_BUF_LEN        2048

//服务号绑定类型，与 t_service_account_binding.f_bind_type 一致。
#define SVC_BIND_ALL          0   //本企业全员
#define SVC_BIND_DEPARTMENT   1   //指定部门
#define SVC_BIND_USER         2   //指定员工


ServiceAccountManager::ServiceAccountManager()
{
    //服务号ID分配基数默认 0，init() 时用库中最大值修正，首个新服务号ID为 1。
    m_baseServiceId.store(0);
}

ServiceAccountManager::~ServiceAccountManager()
{

}

bool ServiceAccountManager::init()
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
    {
        LOGE("ServiceAccountManager::init failed to connect db, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //取库中当前最大 f_service_id 作为ID分配基数；库为空时 MAX 返回 NULL，得 0。
    QueryResult* pResult = pConn->query("SELECT MAX(f_service_id) FROM t_service_account");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            int64_t maxId = pRow[0].getInt32();
            if (maxId > m_baseServiceId.load())
                m_baseServiceId.store(maxId);
        }
        pResult->endQuery();
        delete pResult;
    }

    LOGI("ServiceAccountManager::init ok, base service id: %lld", (long long)m_baseServiceId.load());
    return true;
}

IDatabase* ServiceAccountManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("ServiceAccountManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

int32_t ServiceAccountManager::queryCount(IDatabase* pConn, const char* sql)
{
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return 0;

    int32_t count = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

bool ServiceAccountManager::replaceBindings(IDatabase* pConn, int64_t serviceId,
                                            const std::vector<ServiceBinding>& bindings)
{
    //整体替换：先清空该服务号的旧绑定。
    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_service_account_binding WHERE f_service_id = %lld",
        (long long)serviceId);
    pConn->execute(sql);

    //逐个写入新绑定；非法绑定类型跳过。
    for (size_t i = 0; i < bindings.size(); ++i)
    {
        int32_t bt = bindings[i].bindType;
        if (bt != SVC_BIND_ALL && bt != SVC_BIND_DEPARTMENT && bt != SVC_BIND_USER)
            continue;
        //「本企业全员」绑定无具体目标，目标ID统一记为 0。
        int64_t targetId = (bt == SVC_BIND_ALL) ? 0 : bindings[i].targetId;
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_service_account_binding(f_service_id, f_bind_type, f_bind_target_id, f_create_time) "
            "VALUES(%lld, %d, %lld, NOW())",
            (long long)serviceId, bt, (long long)targetId);
        if (!pConn->execute(sql))
        {
            LOGE("replaceBindings failed: insert error, sql: %s", sql);
            return false;
        }
    }

    return true;
}

void ServiceAccountManager::listServices(int64_t enterpriseId, std::vector<ServiceAccountInfo>& outList)
{
    outList.clear();

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_service_id, f_enterprise_id, f_name, f_kind, f_description, f_avatar_color, "
        "f_avatar_glyph, f_owner_admin_id, f_allow_unsubscribe, f_status, f_create_time "
        "FROM t_service_account WHERE f_enterprise_id = %lld ORDER BY f_service_id",
        (long long)enterpriseId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        ServiceAccountInfo info;
        info.serviceId        = pRow[0].getInt32();
        info.enterpriseId     = pRow[1].getInt32();
        info.name             = pRow[2].getString();
        info.kind             = pRow[3].getInt32();
        info.description      = pRow[4].getString();
        info.avatarColor      = pRow[5].getString();
        info.avatarGlyph      = pRow[6].getString();
        info.ownerAdminId     = pRow[7].getInt32();
        info.allowUnsubscribe = pRow[8].getInt32();
        info.status           = pRow[9].getInt32();
        info.createTime       = pRow[10].getString();
        outList.push_back(info);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

bool ServiceAccountManager::getService(int64_t serviceId, ServiceAccountInfo& outInfo)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //1. 取服务号主记录。
    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_name, f_kind, f_description, f_avatar_color, f_avatar_glyph, "
        "f_owner_admin_id, f_allow_unsubscribe, f_status, f_create_time "
        "FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outInfo.serviceId        = serviceId;
        outInfo.enterpriseId     = pRow[0].getInt32();
        outInfo.name             = pRow[1].getString();
        outInfo.kind             = pRow[2].getInt32();
        outInfo.description      = pRow[3].getString();
        outInfo.avatarColor      = pRow[4].getString();
        outInfo.avatarGlyph      = pRow[5].getString();
        outInfo.ownerAdminId     = pRow[6].getInt32();
        outInfo.allowUnsubscribe = pRow[7].getInt32();
        outInfo.status           = pRow[8].getInt32();
        outInfo.createTime       = pRow[9].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //2. 取可见范围绑定。
    snprintf(sql, sizeof(sql),
        "SELECT f_bind_type, f_bind_target_id FROM t_service_account_binding "
        "WHERE f_service_id = %lld", (long long)serviceId);
    pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        while (true)
        {
            pRow = pResult->fetch();
            if (pRow == NULL)
                break;
            ServiceBinding b;
            b.bindType = pRow[0].getInt32();
            b.targetId = pRow[1].getInt32();
            outInfo.bindings.push_back(b);
            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    return true;
}

int32_t ServiceAccountManager::saveService(const SaveServiceReq& req, int64_t ownerAdminId, int64_t& outId)
{
    if (req.name.empty())
    {
        LOGE("saveService failed: empty name");
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    std::string nameEsc = dbapi::EscapeString(req.name);
    std::string descEsc = dbapi::EscapeString(req.description);
    std::string colorEsc = dbapi::EscapeString(req.avatarColor);

    int64_t serviceId = req.serviceId;
    char sql[SVC_SQL_BUF_LEN] = { 0 };
    if (serviceId == 0)
    {
        //新建：分配ID，状态为正常，记录所有者管理员。
        serviceId = ++m_baseServiceId;
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_service_account(f_service_id, f_enterprise_id, f_name, f_kind, "
            "f_description, f_avatar_color, f_owner_admin_id, f_allow_unsubscribe, f_status, f_create_time) "
            "VALUES(%lld, %lld, '%s', %d, '%s', '%s', %lld, %d, 0, NOW())",
            (long long)serviceId, (long long)req.enterpriseId, nameEsc.c_str(), req.kind,
            descEsc.c_str(), colorEsc.c_str(), (long long)ownerAdminId,
            req.allowUnsubscribe ? 1 : 0);
        if (!pConn->execute(sql))
        {
            LOGE("saveService failed: insert error, sql: %s", sql);
            return error_code_unknown;
        }
    }
    else
    {
        //编辑：服务号必须存在；不动所属企业、所有者、状态。
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_service_account WHERE f_service_id = %lld",
            (long long)serviceId);
        if (queryCount(pConn.get(), sql) <= 0)
        {
            LOGE("saveService failed: service not exist, serviceid: %lld", (long long)serviceId);
            return error_code_unknown;
        }

        snprintf(sql, sizeof(sql),
            "UPDATE t_service_account SET f_name = '%s', f_kind = %d, f_description = '%s', "
            "f_avatar_color = '%s', f_allow_unsubscribe = %d WHERE f_service_id = %lld",
            nameEsc.c_str(), req.kind, descEsc.c_str(), colorEsc.c_str(),
            req.allowUnsubscribe ? 1 : 0, (long long)serviceId);
        if (!pConn->execute(sql))
        {
            LOGE("saveService failed: update error, sql: %s", sql);
            return error_code_unknown;
        }
    }

    outId = serviceId;
    LOGI("saveService ok, serviceid: %lld, isNew: %d",
         (long long)serviceId, req.serviceId == 0 ? 1 : 0);
    return error_code_ok;
}

int32_t ServiceAccountManager::deleteService(int64_t serviceId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("deleteService failed: service not exist, serviceid: %lld", (long long)serviceId);
        return error_code_unknown;
    }

    //先删可见范围绑定，再删服务号主记录。
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_service_account_binding WHERE f_service_id = %lld",
        (long long)serviceId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql),
        "DELETE FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    if (!pConn->execute(sql))
    {
        LOGE("deleteService failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("deleteService ok, serviceid: %lld", (long long)serviceId);
    return error_code_ok;
}

int32_t ServiceAccountManager::setBindings(int64_t serviceId, const std::vector<ServiceBinding>& bindings)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("setBindings failed: service not exist, serviceid: %lld", (long long)serviceId);
        return error_code_unknown;
    }

    if (!replaceBindings(pConn.get(), serviceId, bindings))
        return error_code_unknown;

    LOGI("setBindings ok, serviceid: %lld, count: %u",
         (long long)serviceId, (uint32_t)bindings.size());
    return error_code_ok;
}

int32_t ServiceAccountManager::setStatus(int64_t serviceId, int32_t status)
{
    //服务号状态仅 0=正常 / 1=停用 两种取值。
    if (status != 0 && status != 1)
    {
        LOGE("setStatus failed: invalid status %d, serviceid: %lld", status, (long long)serviceId);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("setStatus failed: service not exist, serviceid: %lld", (long long)serviceId);
        return error_code_unknown;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_service_account SET f_status = %d WHERE f_service_id = %lld",
        status, (long long)serviceId);
    if (!pConn->execute(sql))
    {
        LOGE("setStatus failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("setStatus ok, serviceid: %lld, status: %d", (long long)serviceId, status);
    return error_code_ok;
}

int64_t ServiceAccountManager::getServiceEnterpriseId(int64_t serviceId)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return 0;

    char sql[SVC_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id FROM t_service_account WHERE f_service_id = %lld",
        (long long)serviceId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return 0;

    int64_t enterpriseId = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        enterpriseId = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return enterpriseId;
}
