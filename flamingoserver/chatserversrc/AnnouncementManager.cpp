/**
 *  平台公告管理器实现，AnnouncementManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "AnnouncementManager.h"
#include <memory>
#include <stdio.h>
#include "Msg.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

//拼装定长 SQL（计数、状态变更等）的字符缓冲区大小。
//公告正文 f_body 为 text 类型、可能较大，涉及正文的 SQL 改用 std::string 拼装。
#define ANN_SQL_BUF_LEN          1024

//公告状态取值，与 t_announcement.f_status 及接口文档一致。
#define ANN_STATUS_DRAFT         0   //草稿：尚未发布
#define ANN_STATUS_PUBLISHED     1   //已发布
#define ANN_STATUS_RECALLED      2   //已撤回

//投放目标类型，与 t_announcement_target.f_target_type 一致。
#define ANN_TARGET_ENTERPRISE    1   //目标为企业
#define ANN_TARGET_DEPARTMENT    2   //目标为部门
#define ANN_TARGET_USER          3   //目标为员工

//受众类型，与 t_announcement.f_audience_type 一致。
#define ANN_AUDIENCE_ALL_ENT     0   //全部企业
#define ANN_AUDIENCE_ENTERPRISE  1   //指定企业
#define ANN_AUDIENCE_DEPARTMENT  2   //指定部门
#define ANN_AUDIENCE_USER        3   //指定员工
#define ANN_AUDIENCE_PLATFORM    4   //平台内部

//普通用户ID的上界（不含）；统计受众人数时借此排除群账号，见 UserManager.h。
#define ANN_USER_ID_MAX          0x0FFFFFFF

//列表分页每页条数的上限。
#define ANN_MAX_PAGE_SIZE        200

//分页缺省每页条数。
#define ANN_DEFAULT_PAGE_SIZE    20


//把分页参数规整到合法范围：页码至少 1，每页条数落在 [1, ANN_MAX_PAGE_SIZE]。
static void NormalizePaging(int32_t& page, int32_t& pageSize)
{
    if (page < 1)
        page = 1;
    if (pageSize <= 0)
        pageSize = ANN_DEFAULT_PAGE_SIZE;
    if (pageSize > ANN_MAX_PAGE_SIZE)
        pageSize = ANN_MAX_PAGE_SIZE;
}

/**
 *  生成「某用户是否命中某条公告的受众」的 SQL 条件片段。
 *
 *  投递侧有两处要做这个判断：用户主动拉取(getAnnouncementsForUser)与公告发布时筛选
 *  推送对象(filterAudienceAmong)。两处的规则必须<u>完全一致</u>，否则会出现「发布时
 *  没推给他、他自己拉却拉得到」这类前后矛盾的现象，所以抽到这里统一生成，避免两边
 *  各写一份 SQL 之后慢慢走样。
 *
 *  两处的差别只在于「当前用户」怎么表达：拉取时是个常量 userid，筛选时是外层 t_user
 *  的列，故把它做成参数。
 *
 *  注意本片段<u>不</u>包含「平台内部公告不投」这一条——那一条由调用方写在外层 WHERE 里，
 *  与 f_status 的过滤放在一起，两处都不能漏。
 *
 *    annAlias          公告表在外层查询里的别名，如 "a"。
 *    useridExpr        当前用户ID的 SQL 表达式，如 "100009" 或 "u.f_user_id"。
 *    enterpriseIdExpr  当前用户所属企业ID的 SQL 表达式，如 "u.f_enterprise_id"
 *                      或一个反查 t_user 的子查询。
 *  @return 形如 "(a.f_audience_type = 0 OR ...)" 的条件片段，可直接 AND 进 WHERE。
 */
static std::string BuildAudienceMatchSql(const char* annAlias,
                                         const std::string& useridExpr,
                                         const std::string& enterpriseIdExpr)
{
    const std::string a = annAlias;

    std::string clause = "(";

    //受众为「全部企业」：投给所有 IM 用户，无附加条件。
    clause += a + ".f_audience_type = " + std::to_string(ANN_AUDIENCE_ALL_ENT);

    //受众为「指定企业」：该用户所属企业在投放目标里。
    clause += " OR (" + a + ".f_audience_type = " + std::to_string(ANN_AUDIENCE_ENTERPRISE) +
              " AND EXISTS (SELECT 1 FROM t_announcement_target t"
              " WHERE t.f_announcement_id = " + a + ".f_announcement_id"
              " AND t.f_target_type = " + std::to_string(ANN_TARGET_ENTERPRISE) +
              " AND t.f_target_id = " + enterpriseIdExpr + "))";

    //受众为「指定部门」：该用户<u>在职</u>归属的部门里有一个落在投放目标中。
    //当前只匹配精确部门、不向下级部门递归，理由见 docs/消息盒子接入方案.md 第 9 节。
    clause += " OR (" + a + ".f_audience_type = " + std::to_string(ANN_AUDIENCE_DEPARTMENT) +
              " AND EXISTS (SELECT 1 FROM t_announcement_target t, t_department_member m"
              " WHERE t.f_announcement_id = " + a + ".f_announcement_id"
              " AND t.f_target_type = " + std::to_string(ANN_TARGET_DEPARTMENT) +
              " AND m.f_dept_id = t.f_target_id"
              " AND m.f_user_id = " + useridExpr +
              " AND m.f_status = " + std::to_string(deptmember_status_active) + "))";

    //受众为「指定员工」：该用户本人被逐个列在投放目标里。
    clause += " OR (" + a + ".f_audience_type = " + std::to_string(ANN_AUDIENCE_USER) +
              " AND EXISTS (SELECT 1 FROM t_announcement_target t"
              " WHERE t.f_announcement_id = " + a + ".f_announcement_id"
              " AND t.f_target_type = " + std::to_string(ANN_TARGET_USER) +
              " AND t.f_target_id = " + useridExpr + "))";

    clause += ")";
    return clause;
}

AnnouncementManager::AnnouncementManager()
{
    //公告ID分配基数默认 0，init() 时用库中最大值修正，首个新公告ID为 1。
    m_baseAnnouncementId.store(0);
}

AnnouncementManager::~AnnouncementManager()
{

}

bool AnnouncementManager::init()
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
    {
        LOGE("AnnouncementManager::init failed to connect db, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //取库中当前最大 f_announcement_id 作为ID分配基数；库为空时 MAX 返回 NULL，得 0。
    QueryResult* pResult = pConn->query("SELECT MAX(f_announcement_id) FROM t_announcement");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            int64_t maxId = pRow[0].getInt32();
            if (maxId > m_baseAnnouncementId.load())
                m_baseAnnouncementId.store(maxId);
        }
        pResult->endQuery();
        delete pResult;
    }

    LOGI("AnnouncementManager::init ok, base announcement id: %lld",
         (long long)m_baseAnnouncementId.load());
    return true;
}

IDatabase* AnnouncementManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("AnnouncementManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

int32_t AnnouncementManager::queryCount(IDatabase* pConn, const char* sql)
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

bool AnnouncementManager::replaceTargets(IDatabase* pConn, int64_t announcementId,
                                         const std::vector<AnnouncementTarget>& targets)
{
    //整体替换：先清空该公告的旧目标。
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_announcement_target WHERE f_announcement_id = %lld",
        (long long)announcementId);
    pConn->execute(sql);

    //逐个写入新目标；非法目标类型跳过。
    for (size_t i = 0; i < targets.size(); ++i)
    {
        int32_t t = targets[i].type;
        if (t != ANN_TARGET_ENTERPRISE && t != ANN_TARGET_DEPARTMENT && t != ANN_TARGET_USER)
            continue;
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_announcement_target(f_announcement_id, f_target_type, f_target_id, f_create_time) "
            "VALUES(%lld, %d, %lld, NOW())",
            (long long)announcementId, t, (long long)targets[i].id);
        if (!pConn->execute(sql))
        {
            LOGE("replaceTargets failed: insert error, sql: %s", sql);
            return false;
        }
    }

    return true;
}

int32_t AnnouncementManager::computeAudienceTotal(IDatabase* pConn, int32_t audienceType,
                                                  const std::vector<AnnouncementTarget>& targets)
{
    //把 targets 中指定类型的目标ID拼成 "id1,id2,..." 形式的 IN 列表。
    //typeFilter 为 0 时不限目标类型（取全部目标）。
    std::string idList;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        char buf[32] = { 0 };
        snprintf(buf, sizeof(buf), "%s%lld", idList.empty() ? "" : ",", (long long)targets[i].id);
        idList += buf;
    }

    char sql[ANN_SQL_BUF_LEN] = { 0 };
    switch (audienceType)
    {
    case ANN_AUDIENCE_ALL_ENT:
        //全部企业：全体IM自然人用户。
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_user WHERE f_user_id < %d", ANN_USER_ID_MAX);
        return queryCount(pConn, sql);

    case ANN_AUDIENCE_ENTERPRISE:
        //指定企业：这些企业下的全部自然人用户。
        if (idList.empty())
            return 0;
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_user WHERE f_user_id < %d AND f_enterprise_id IN (%s)",
            ANN_USER_ID_MAX, idList.c_str());
        return queryCount(pConn, sql);

    case ANN_AUDIENCE_DEPARTMENT:
        //指定部门：这些部门的在职成员去重计数。
        if (idList.empty())
            return 0;
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(DISTINCT f_user_id) FROM t_department_member "
            "WHERE f_status = 0 AND f_dept_id IN (%s)", idList.c_str());
        return queryCount(pConn, sql);

    case ANN_AUDIENCE_USER:
        //指定员工：目标条数即受众人数。
        return static_cast<int32_t>(targets.size());

    case ANN_AUDIENCE_PLATFORM:
    default:
        //平台内部公告无IM用户受众。
        return 0;
    }
}

void AnnouncementManager::listAnnouncements(int32_t status, int32_t page, int32_t pageSize,
                                            int32_t& outTotal, std::vector<AnnouncementInfo>& outList)
{
    outTotal = 0;
    outList.clear();
    NormalizePaging(page, pageSize);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //状态过滤：status 为 -1 表示不过滤。
    std::string where;
    if (status >= 0)
    {
        char buf[64] = { 0 };
        snprintf(buf, sizeof(buf), " WHERE f_status = %d", status);
        where = buf;
    }

    //1. 取满足条件的总条数。
    std::string countSql = "SELECT COUNT(*) FROM t_announcement" + where;
    outTotal = queryCount(pConn.get(), countSql.c_str());
    if (outTotal <= 0)
        return;

    //2. 取当前页：置顶优先、再按创建时间倒序；已读人数用关联子查询带出。
    int32_t offset = (page - 1) * pageSize;
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        //带上 f_body：控制台的公告列表项要画一行正文摘要，详情面板首次打开时
        //也直接用它显示正文。此前列表不返回正文，表现为“第一次进公告界面，
        //选中那条的正文是空白的，点过一遍才有”。
        "SELECT a.f_announcement_id, a.f_title, a.f_body, a.f_severity, a.f_sender_name, a.f_audience_type, "
        "a.f_pinned, a.f_scheduled, a.f_scheduled_time, a.f_sent_time, a.f_status, a.f_create_time, "
        "(SELECT COUNT(*) FROM t_announcement_read r WHERE r.f_announcement_id = a.f_announcement_id) "
        "FROM t_announcement a%s ORDER BY a.f_pinned DESC, a.f_create_time DESC LIMIT %d, %d",
        where.c_str(), offset, pageSize);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        AnnouncementInfo info;
        info.announcementId = pRow[0].getInt32();
        info.title          = pRow[1].getString();
        info.body           = pRow[2].getString();
        info.severity       = pRow[3].getInt32();
        info.senderName     = pRow[4].getString();
        info.audienceType   = pRow[5].getInt32();
        info.pinned         = pRow[6].getInt32();
        info.scheduled      = pRow[7].getInt32();
        info.scheduledTime  = pRow[8].getString();
        info.sentTime       = pRow[9].getString();
        info.status         = pRow[10].getInt32();
        info.createTime     = pRow[11].getString();
        info.readCount      = pRow[12].getInt32();
        outList.push_back(info);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

bool AnnouncementManager::getAnnouncement(int64_t announcementId, AnnouncementInfo& outInfo)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //1. 取公告主记录。
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_title, f_body, f_severity, f_sender_admin_id, f_sender_name, f_audience_type, "
        "f_pinned, f_scheduled, f_scheduled_time, f_sent_time, f_status, f_create_time, f_update_time "
        "FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outInfo.announcementId = announcementId;
        outInfo.title          = pRow[0].getString();
        outInfo.body           = pRow[1].getString();
        outInfo.severity       = pRow[2].getInt32();
        outInfo.senderAdminId  = pRow[3].getInt32();
        outInfo.senderName     = pRow[4].getString();
        outInfo.audienceType   = pRow[5].getInt32();
        outInfo.pinned         = pRow[6].getInt32();
        outInfo.scheduled      = pRow[7].getInt32();
        outInfo.scheduledTime  = pRow[8].getString();
        outInfo.sentTime       = pRow[9].getString();
        outInfo.status         = pRow[10].getInt32();
        outInfo.createTime     = pRow[11].getString();
        outInfo.updateTime     = pRow[12].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //2. 取投放目标。
    snprintf(sql, sizeof(sql),
        "SELECT f_target_type, f_target_id FROM t_announcement_target "
        "WHERE f_announcement_id = %lld", (long long)announcementId);
    pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        while (true)
        {
            pRow = pResult->fetch();
            if (pRow == NULL)
                break;
            AnnouncementTarget t;
            t.type = pRow[0].getInt32();
            t.id   = pRow[1].getInt32();
            outInfo.targets.push_back(t);
            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    //3. 已读人数与受众总人数。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_announcement_read WHERE f_announcement_id = %lld",
        (long long)announcementId);
    outInfo.readCount = queryCount(pConn.get(), sql);
    outInfo.totalCount = computeAudienceTotal(pConn.get(), outInfo.audienceType, outInfo.targets);

    return true;
}

int32_t AnnouncementManager::saveAnnouncement(const SaveAnnouncementReq& req, int64_t senderAdminId,
                                              const std::string& senderName, int64_t& outId)
{
    if (req.title.empty())
    {
        LOGE("saveAnnouncement failed: empty title");
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    std::string titleEsc = dbapi::EscapeString(req.title);
    std::string bodyEsc = dbapi::EscapeString(req.body);
    std::string senderEsc = dbapi::EscapeString(senderName);

    int64_t announcementId = req.announcementId;
    if (announcementId == 0)
    {
        //新建：分配ID，状态为草稿，记录发布者；正文用 std::string 拼装以容纳大文本。
        announcementId = ++m_baseAnnouncementId;

        char head[256] = { 0 };
        snprintf(head, sizeof(head),
            "INSERT INTO t_announcement(f_announcement_id, f_severity, f_sender_admin_id, "
            "f_audience_type, f_pinned, f_scheduled, f_status, f_create_time, f_title, f_body, f_sender_name) "
            "VALUES(%lld, %d, %lld, %d, %d, 0, %d, NOW(), '",
            (long long)announcementId, req.severity, (long long)senderAdminId,
            req.audienceType, req.pinned, ANN_STATUS_DRAFT);
        std::string insertSql = head;
        insertSql += titleEsc;
        insertSql += "', '";
        insertSql += bodyEsc;
        insertSql += "', '";
        insertSql += senderEsc;
        insertSql += "')";
        if (!pConn->execute(insertSql.c_str()))
        {
            LOGE("saveAnnouncement failed: insert error, announcementid: %lld",
                 (long long)announcementId);
            return error_code_unknown;
        }
    }
    else
    {
        //编辑：公告必须存在；仅改内容相关字段，不动发布者/状态/发布时间。
        char sql[ANN_SQL_BUF_LEN] = { 0 };
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id = %lld",
            (long long)announcementId);
        if (queryCount(pConn.get(), sql) <= 0)
        {
            LOGE("saveAnnouncement failed: announcement not exist, announcementid: %lld",
                 (long long)announcementId);
            return error_code_unknown;
        }

        char head[256] = { 0 };
        snprintf(head, sizeof(head),
            "UPDATE t_announcement SET f_severity = %d, f_audience_type = %d, f_pinned = %d, f_title = '",
            req.severity, req.audienceType, req.pinned);
        std::string updateSql = head;
        updateSql += titleEsc;
        updateSql += "', f_body = '";
        updateSql += bodyEsc;
        char tail[96] = { 0 };
        snprintf(tail, sizeof(tail), "' WHERE f_announcement_id = %lld", (long long)announcementId);
        updateSql += tail;
        if (!pConn->execute(updateSql.c_str()))
        {
            LOGE("saveAnnouncement failed: update error, announcementid: %lld",
                 (long long)announcementId);
            return error_code_unknown;
        }
    }

    //整体重写投放目标集。
    replaceTargets(pConn.get(), announcementId, req.targets);

    outId = announcementId;
    LOGI("saveAnnouncement ok, announcementid: %lld, isNew: %d",
         (long long)announcementId, req.announcementId == 0 ? 1 : 0);
    return error_code_ok;
}

int32_t AnnouncementManager::sendAnnouncement(int64_t announcementId, bool scheduled,
                                              const std::string& scheduledTime)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("sendAnnouncement failed: announcement not exist, announcementid: %lld",
             (long long)announcementId);
        return error_code_unknown;
    }

    if (scheduled)
    {
        //定时发布：登记定时信息，状态暂保持草稿。
        //到点后由后台定时器调用 publishDueScheduled() 自动把它转为「已发布」。
        std::string timeEsc = dbapi::EscapeString(scheduledTime);
        snprintf(sql, sizeof(sql),
            "UPDATE t_announcement SET f_scheduled = 1, f_scheduled_time = '%s' "
            "WHERE f_announcement_id = %lld",
            timeEsc.c_str(), (long long)announcementId);
    }
    else
    {
        //立即发布：状态置为已发布并记录发布时间。
        snprintf(sql, sizeof(sql),
            "UPDATE t_announcement SET f_status = %d, f_scheduled = 0, f_sent_time = NOW() "
            "WHERE f_announcement_id = %lld",
            ANN_STATUS_PUBLISHED, (long long)announcementId);
    }
    if (!pConn->execute(sql))
    {
        LOGE("sendAnnouncement failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("sendAnnouncement ok, announcementid: %lld, scheduled: %d",
         (long long)announcementId, scheduled ? 1 : 0);
    return error_code_ok;
}

int32_t AnnouncementManager::deleteAnnouncement(int64_t announcementId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("deleteAnnouncement failed: announcement not exist, announcementid: %lld",
             (long long)announcementId);
        return error_code_unknown;
    }

    //物理删除：先删从属表（投放目标、已读回执），再删主记录。
    snprintf(sql, sizeof(sql),
        "DELETE FROM t_announcement_target WHERE f_announcement_id = %lld",
        (long long)announcementId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql),
        "DELETE FROM t_announcement_read WHERE f_announcement_id = %lld",
        (long long)announcementId);
    pConn->execute(sql);

    snprintf(sql, sizeof(sql),
        "DELETE FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    if (!pConn->execute(sql))
    {
        LOGE("deleteAnnouncement failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("deleteAnnouncement ok, announcementid: %lld", (long long)announcementId);
    return error_code_ok;
}

int32_t AnnouncementManager::recallAnnouncement(int64_t announcementId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //仅「已发布」状态的公告可撤回。
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_status FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return error_code_unknown;
    int32_t status = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        status = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;

    if (status < 0)
    {
        LOGE("recallAnnouncement failed: announcement not exist, announcementid: %lld",
             (long long)announcementId);
        return error_code_unknown;
    }
    if (status != ANN_STATUS_PUBLISHED)
    {
        LOGE("recallAnnouncement failed: not published, announcementid: %lld, status: %d",
             (long long)announcementId, status);
        return error_code_unknown;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_announcement SET f_status = %d WHERE f_announcement_id = %lld",
        ANN_STATUS_RECALLED, (long long)announcementId);
    if (!pConn->execute(sql))
    {
        LOGE("recallAnnouncement failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("recallAnnouncement ok, announcementid: %lld", (long long)announcementId);
    return error_code_ok;
}

int32_t AnnouncementManager::setPinned(int64_t announcementId, int32_t pinned)
{
    //置顶标志仅 0 / 1 两种取值。
    if (pinned != 0 && pinned != 1)
        return error_code_unknown;

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id = %lld",
        (long long)announcementId);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("setPinned failed: announcement not exist, announcementid: %lld",
             (long long)announcementId);
        return error_code_unknown;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_announcement SET f_pinned = %d WHERE f_announcement_id = %lld",
        pinned, (long long)announcementId);
    if (!pConn->execute(sql))
    {
        LOGE("setPinned failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("setPinned ok, announcementid: %lld, pinned: %d", (long long)announcementId, pinned);
    return error_code_ok;
}

int32_t AnnouncementManager::publishDueScheduled(std::vector<int64_t>* outPublishedIds)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if (outPublishedIds != NULL)
        outPublishedIds->clear();

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return 0;

    //先查出这一轮该发布哪些公告，再按查到的ID去 UPDATE。
    //之所以不像原先那样直接 "UPDATE ... WHERE 定时时间已到" 一条了事：调用方要拿到
    //<u>具体的公告ID</u>才能把它们推给在线用户，而 UPDATE 只能告诉你改了几行、说不出
    //改的是哪几条。先查ID再按ID更新，还顺带避免了「某条公告的定时时间恰好落在两条
    //语句之间」导致的漏推——两个语句处理的是严格相同的集合。
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_announcement_id FROM t_announcement "
        "WHERE f_scheduled = 1 AND f_status = %d "
        "AND f_scheduled_time IS NOT NULL AND f_scheduled_time <= NOW()",
        ANN_STATUS_DRAFT);

    std::vector<int64_t> dueIds;
    QueryResult* pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        while (true)
        {
            Field* pRow = pResult->fetch();
            if (pRow == NULL)
                break;

            dueIds.push_back(static_cast<int64_t>(pRow[0].getUInt64()));

            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    //没有到点的定时公告——这是每分钟扫描的常态，直接返回、不记日志。
    if (dueIds.empty())
        return 0;

    //把这些公告置为已发布。ID 列表由上一步查得，逐个拼进 IN 子句。
    std::string idList;
    for (size_t i = 0; i < dueIds.size(); ++i)
    {
        char buf[32] = { 0 };
        snprintf(buf, sizeof(buf), "%s%lld", idList.empty() ? "" : ",", (long long)dueIds[i]);
        idList += buf;
    }

    std::string updateSql =
        "UPDATE t_announcement SET f_status = " + std::to_string(ANN_STATUS_PUBLISHED) +
        ", f_sent_time = NOW() WHERE f_announcement_id IN (" + idList + ")";

    uint32_t affectedCount = 0;
    int dbErrno = 0;
    if (!pConn->execute(updateSql.c_str(), affectedCount, dbErrno))
    {
        LOGE("publishDueScheduled failed: db execute error, errno: %d", dbErrno);
        return 0;
    }

    if (outPublishedIds != NULL)
        *outPublishedIds = dueIds;

    LOGI("publishDueScheduled: %u scheduled announcement(s) auto-published",
         static_cast<unsigned>(dueIds.size()));
    return static_cast<int32_t>(dueIds.size());
}

void AnnouncementManager::getAnnouncementsForUser(int32_t userid, int64_t sinceId, int32_t limit,
                                                  std::vector<AnnouncementPushItem>& outList,
                                                  bool& outHasMore)
{
    outList.clear();
    outHasMore = false;

    if (userid <= 0 || limit <= 0)
        return;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //「当前用户」在受众判定里的两种表达：userid 是个常量，所属企业则要现查一次。
    const std::string useridExpr = std::to_string(userid);
    const std::string enterpriseIdExpr =
        "(SELECT u2.f_enterprise_id FROM t_user u2 WHERE u2.f_user_id = " + useridExpr + ")";

    //多取一条用来判断后面还有没有：取回 limit+1 条就说明还有更多，返回时把多的那条丢掉。
    const int32_t fetchLimit = limit + 1;

    std::string sql =
        "SELECT a.f_announcement_id, a.f_title, a.f_body, a.f_severity, a.f_sender_name, "
        "a.f_pinned, a.f_sent_time, "
        //本人是否读过：t_announcement_read 里有对应行即为已读。
        "(SELECT COUNT(*) FROM t_announcement_read r "
        "WHERE r.f_announcement_id = a.f_announcement_id AND r.f_user_id = " + useridExpr + ") "
        "FROM t_announcement a "
        "WHERE a.f_status = " + std::to_string(ANN_STATUS_PUBLISHED) + " "
        //平台内部公告一律不投给 IM 用户。这个条件写在 SQL 最内层而非应用层判断，
        //是为了让将来任何走这条查询的新调用路径都绕不过它。
        "AND a.f_audience_type <> " + std::to_string(ANN_AUDIENCE_PLATFORM) + " "
        "AND a.f_announcement_id > " + std::to_string(sinceId) + " "
        "AND " + BuildAudienceMatchSql("a", useridExpr, enterpriseIdExpr) + " "
        //与消息盒子列表的展示顺序一致：置顶的排最前，其余按发布时间由新到旧。
        "ORDER BY a.f_pinned DESC, a.f_sent_time DESC "
        "LIMIT " + std::to_string(fetchLimit);

    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        //已经凑够 limit 条，那么这多出来的一条只用于判定「还有更多」，不放进结果。
        if (static_cast<int32_t>(outList.size()) >= limit)
        {
            outHasMore = true;
            break;
        }

        AnnouncementPushItem item;
        item.announcementId = static_cast<int64_t>(pRow[0].getUInt64());
        item.title          = pRow[1].getString();
        item.body           = pRow[2].getString();
        item.severity       = pRow[3].getInt32();
        item.senderName     = pRow[4].getString();
        item.pinned         = pRow[5].getInt32();
        //库里 f_sent_time 是 datetime 文本，协议要 Unix 毫秒，在这里换算。
        item.sentTime       = AnnouncementProtocol::dateTimeToMs(pRow[6].getString());
        item.read           = (pRow[7].getInt32() > 0) ? 1 : 0;
        outList.push_back(item);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

int32_t AnnouncementManager::markAnnouncementRead(int64_t announcementId, int32_t userid)
{
    if (announcementId <= 0 || userid <= 0)
        return error_code_unknown;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //先确认这条公告确实存在且已发布。不校验也能靠唯一键防重复，但异常客户端可以据此
    //往表里灌任意ID的回执，把管理控制台的已读统计搅乱，所以挡在写入之前。
    char sql[ANN_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id = %lld AND f_status = %d",
        (long long)announcementId, ANN_STATUS_PUBLISHED);
    if (queryCount(pConn.get(), sql) <= 0)
    {
        LOGE("markAnnouncementRead failed: announcement not exist or not published, "
             "announcementid: %lld, userid: %d", (long long)announcementId, userid);
        return error_code_unknown;
    }

    //重复上报是正常现象（用户反复点开同一条公告），靠 (公告ID, 用户ID) 唯一键吸收。
    //INSERT IGNORE 在 SQLite 侧由 SqliteSqlTranslator 改写成 INSERT OR IGNORE。
    snprintf(sql, sizeof(sql),
        "INSERT IGNORE INTO t_announcement_read(f_announcement_id, f_user_id, f_read_time) "
        "VALUES(%lld, %d, NOW())",
        (long long)announcementId, userid);
    if (!pConn->execute(sql))
    {
        LOGE("markAnnouncementRead failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    LOGI("markAnnouncementRead ok, announcementid: %lld, userid: %d",
         (long long)announcementId, userid);
    return error_code_ok;
}

void AnnouncementManager::filterAudienceAmong(int64_t announcementId,
                                              const std::vector<int32_t>& candidates,
                                              std::vector<int32_t>& outHitUserIds)
{
    outHitUserIds.clear();

    if (announcementId <= 0 || candidates.empty())
        return;

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return;

    //候选用户ID拼成 IN 列表。候选来自在线会话，数量受在线人数约束。
    std::string idList;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        char buf[32] = { 0 };
        snprintf(buf, sizeof(buf), "%s%d", idList.empty() ? "" : ",", candidates[i]);
        idList += buf;
    }

    //这里的「当前用户」是外层 t_user 的列，而不是常量——一条 SQL 判完全部候选。
    std::string sql =
        "SELECT u.f_user_id FROM t_user u, t_announcement a "
        "WHERE a.f_announcement_id = " + std::to_string(announcementId) + " "
        "AND a.f_status = " + std::to_string(ANN_STATUS_PUBLISHED) + " "
        //与 getAnnouncementsForUser 一样，平台内部公告在最内层就被挡掉。
        "AND a.f_audience_type <> " + std::to_string(ANN_AUDIENCE_PLATFORM) + " "
        "AND u.f_user_id IN (" + idList + ") "
        "AND " + BuildAudienceMatchSql("a", "u.f_user_id", "u.f_enterprise_id");

    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        outHitUserIds.push_back(pRow[0].getInt32());

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}
