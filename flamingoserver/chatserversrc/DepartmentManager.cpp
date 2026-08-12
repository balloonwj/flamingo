/**
 *  企业部门管理器实现，DepartmentManager.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "DepartmentManager.h"
#include <memory>
#include <stdio.h>
#include "DepartmentPath.h"
#include "Msg.h"
#include "EnterpriseManager.h"
#include "../base/Singleton.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"

//公司根部门的父部门ID与层级：它是整棵组织架构树的根，没有父部门，层级固定为最外层。
#define DEPT_ROOT_PARENT_ID   0
#define DEPT_ROOT_LEVEL       1

//拼装 SQL 的字符缓冲区大小。
//最长的语句为建部门 INSERT：部门名(转义后≤128) + 描述(转义后≤512) + 物化路径(≤255)
//加上 SQL 关键字，总长远小于 2048，取 2048 留足余量。
#define DEPT_SQL_BUF_LEN   2048


DepartmentManager::DepartmentManager()
{
    //部门ID分配基数默认从 DEPARTMENT_ID_BASE 起，init() 时再用库中最大值修正。
    m_baseDeptId.store(DEPARTMENT_ID_BASE);
}

DepartmentManager::~DepartmentManager()
{

}

bool DepartmentManager::init()
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if (!loadFromDb())
    {
        LOGE("DepartmentManager::init failed to load org structure from db, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //取库中当前最大 f_dept_id 作为ID分配基数；库为空时 MAX 返回 NULL，getInt32 得 0，保持 DEPARTMENT_ID_BASE。
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    QueryResult* pResult = pConn->query("SELECT MAX(f_dept_id) FROM t_department");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            int64_t maxId = pRow[0].getInt32();
            if (maxId > m_baseDeptId.load())
                m_baseDeptId.store(maxId);
        }
        pResult->endQuery();
        delete pResult;
    }

    LOGI("DepartmentManager::init ok, departments: %u, base dept id: %lld",
         (uint32_t)m_cachedDepts.size(), (long long)m_baseDeptId.load());
    return true;
}

IDatabase* DepartmentManager::createDbConnection()
{
    IDatabase* pConn = DbFactory::createConnection();
    if (!pConn)
    {
        LOGE("DepartmentManager db connect failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return NULL;
    }

    return pConn;
}

bool DepartmentManager::loadFromDb()
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    std::vector<dept::DepartmentInfo> depts;

    //1. 加载所有正常部门，按层级、同级排序输出，方便客户端按 parentdeptid 还原成树。
    QueryResult* pDeptResult = pConn->query(
        "SELECT f_dept_id, f_dept_name, f_parent_dept_id, f_dept_level, f_dept_order, "
        "f_leader_user_id, f_dept_desc, f_enterprise_id FROM t_department WHERE f_status = 0 "
        "ORDER BY f_dept_level, f_dept_order, f_dept_id");
    if (pDeptResult != NULL)
    {
        while (true)
        {
            Field* pRow = pDeptResult->fetch();
            if (pRow == NULL)
                break;

            dept::DepartmentInfo d;
            d.deptid       = pRow[0].getInt32();
            d.deptname     = pRow[1].getString();
            d.parentdeptid = pRow[2].getInt32();
            d.deptlevel    = pRow[3].getInt32();
            d.deptorder    = pRow[4].getInt32();
            d.leaderuserid = pRow[5].getInt32();
            d.deptdesc     = pRow[6].getString();
            d.enterpriseid = pRow[7].getInt32();
            depts.push_back(d);

            if (!pDeptResult->nextRow())
                break;
        }
        pDeptResult->endQuery();
        delete pDeptResult;
    }

    //2. 加载所有在职成员，连 t_user 取展示字段；在线状态不在此填充（由处理器应答时补齐）。
    QueryResult* pMemberResult = pConn->query(
        "SELECT m.f_dept_id, m.f_user_id, m.f_position, m.f_is_primary, m.f_member_order, "
        "u.f_username, u.f_nickname, u.f_facetype, u.f_customface, u.f_employee_no "
        "FROM t_department_member m JOIN t_user u ON m.f_user_id = u.f_user_id "
        "WHERE m.f_status = 0 ORDER BY m.f_member_order, m.f_user_id");
    if (pMemberResult != NULL)
    {
        while (true)
        {
            Field* pRow = pMemberResult->fetch();
            if (pRow == NULL)
                break;

            int64_t deptId = pRow[0].getInt32();
            dept::DepartmentMember m;
            m.userid      = pRow[1].getInt32();
            m.position    = pRow[2].getString();
            m.isprimary   = pRow[3].getInt32();
            m.memberorder = pRow[4].getInt32();
            m.username    = pRow[5].getString();
            m.nickname    = pRow[6].getString();
            m.facetype    = pRow[7].getInt32();
            m.customface  = pRow[8].getString();
            m.employeeno  = pRow[9].getString();

            //把成员挂到所属部门下；部门集合不大，线性查找即可。
            for (size_t i = 0; i < depts.size(); ++i)
            {
                if (depts[i].deptid == deptId)
                {
                    depts[i].members.push_back(m);
                    break;
                }
            }

            if (!pMemberResult->nextRow())
                break;
        }
        pMemberResult->endQuery();
        delete pMemberResult;
    }

    m_cachedDepts.swap(depts);

    //组织架构刚重载（部门/成员发生过写操作）：顺带清空 userid→enterpriseid 缓存，使任何
    //可能的企业归属变更自愈。本函数调用方已持有 m_mutex，这里再取缓存锁，锁序与
    //getUserEnterpriseId（只取缓存锁）一致，不会死锁。
    {
        std::lock_guard<std::mutex> guard(m_userEnterpriseCacheMutex);
        m_userEnterpriseCache.clear();
    }

    LOGI("DepartmentManager::loadFromDb ok, departments: %u", (uint32_t)m_cachedDepts.size());
    return true;
}

bool DepartmentManager::queryDept(IDatabase* pConn, int64_t deptId,
                                  std::string& outPath, int32_t& outLevel, int64_t& outParentId)
{
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_dept_path, f_dept_level, f_parent_dept_id FROM t_department "
        "WHERE f_dept_id = %lld AND f_status = 0",
        (long long)deptId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        outPath     = pRow[0].getString();
        outLevel    = pRow[1].getInt32();
        outParentId = pRow[2].getInt32();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

int32_t DepartmentManager::queryCount(IDatabase* pConn, const char* sql)
{
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return -1;

    int32_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

bool DepartmentManager::isSiblingNameExist(IDatabase* pConn, int64_t enterpriseId, int64_t parentDeptId,
                                           const std::string& name, int64_t excludeDeptId)
{
    std::string nameEsc = dbapi::EscapeString(name);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id = %lld AND f_parent_dept_id = %lld "
        "AND f_dept_name = '%s' AND f_status = 0 AND f_dept_id <> %lld",
        (long long)enterpriseId, (long long)parentDeptId, nameEsc.c_str(), (long long)excludeDeptId);
    return queryCount(pConn, sql) > 0;
}

bool DepartmentManager::isActiveMember(IDatabase* pConn, int64_t deptId, int64_t userId)
{
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department_member WHERE f_dept_id = %lld AND f_user_id = %lld "
        "AND f_status = %d",
        (long long)deptId, (long long)userId, deptmember_status_active);
    return queryCount(pConn, sql) > 0;
}

int64_t DepartmentManager::queryDeptEnterpriseId(IDatabase* pConn, int64_t deptId)
{
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id FROM t_department WHERE f_dept_id = %lld",
        (long long)deptId);

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

int64_t DepartmentManager::getUserEnterpriseId(int64_t userId)
{
    //1. 先查内存缓存（命中直接返回，省去查库）。只在缓存锁内做 map 查找，不持锁查库。
    {
        std::lock_guard<std::mutex> guard(m_userEnterpriseCacheMutex);
        std::map<int64_t, int64_t>::iterator it = m_userEnterpriseCache.find(userId);
        if (it != m_userEnterpriseCache.end())
            return it->second;
    }

    //2. 未命中：查库。
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return 0;

    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id FROM t_user WHERE f_user_id = %lld",
        (long long)userId);

    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return 0;

    int64_t enterpriseId = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        enterpriseId = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;

    //3. 回填缓存：只缓存正值。0（用户不存在 / 尚未建好）不缓存，避免被永久钉死，
    //   下次仍走查库（用户建好后即可查到真实企业）。
    if (enterpriseId > 0)
    {
        std::lock_guard<std::mutex> guard(m_userEnterpriseCacheMutex);
        m_userEnterpriseCache[userId] = enterpriseId;
    }
    return enterpriseId;
}

void DepartmentManager::getOrgStructure(int64_t enterpriseId, std::vector<dept::DepartmentInfo>& outDepts)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //只返回指定企业的部门，实现多租户隔离。
    outDepts.clear();
    for (size_t i = 0; i < m_cachedDepts.size(); ++i)
    {
        if (m_cachedDepts[i].enterpriseid == enterpriseId)
            outDepts.push_back(m_cachedDepts[i]);
    }
}

void DepartmentManager::updateCachedMemberAvatar(int64_t userId, int32_t facetype, const std::string& customface)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //同一用户可能兼属多个部门，全部更新；命中即改其头像字段。
    for (size_t i = 0; i < m_cachedDepts.size(); ++i)
    {
        std::vector<dept::DepartmentMember>& members = m_cachedDepts[i].members;
        for (size_t k = 0; k < members.size(); ++k)
        {
            if (members[k].userid == userId)
            {
                members[k].facetype   = facetype;
                members[k].customface = customface;
            }
        }
    }
}

void DepartmentManager::updateCachedMemberPosition(int64_t userId, const std::string& position)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //同一用户可能兼属多个部门，但职务只跟着主部门走，故加 isprimary 条件，
    //与 EmployeeManager::updateEmployee 里那条 UPDATE 的 WHERE 保持一致。
    for (size_t i = 0; i < m_cachedDepts.size(); ++i)
    {
        std::vector<dept::DepartmentMember>& members = m_cachedDepts[i].members;
        for (size_t k = 0; k < members.size(); ++k)
        {
            if (members[k].userid == userId && members[k].isprimary == 1)
            {
                members[k].position = position;
            }
        }
    }
}

void DepartmentManager::removeCachedMember(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    int removed = 0;
    for (size_t i = 0; i < m_cachedDepts.size(); ++i)
    {
        std::vector<dept::DepartmentMember>& members = m_cachedDepts[i].members;
        //倒序遍历后再 erase：正序删会让后续元素前移、跳过紧邻的下一个。
        //同一用户在同一部门理论上只有一条记录，但这里不依赖该前提。
        for (size_t k = members.size(); k > 0; --k)
        {
            size_t idx = k - 1;
            if (members[idx].userid == userId)
            {
                members.erase(members.begin() + idx);
                ++removed;
            }
        }
    }

    //关键路径日志：删员工时若这里报 0，说明缓存与库已经不同步，界面上会表现为
    //"删不掉"，据此可直接判定问题出在缓存而不是数据库。
    LOGI("DepartmentManager::removeCachedMember, userid: %lld, removed member rows: %d",
         (long long)userId, removed);
}

void DepartmentManager::removeCachedEnterprise(int64_t enterpriseId)
{
    //企业ID 为 0 是"无企业归属"的哨兵值，不是真实企业；真按它清理会把所有
    //未归属用户的映射误删，直接挡掉。
    if (enterpriseId == 0)
    {
        LOGE("DepartmentManager::removeCachedEnterprise ignored: enterpriseid is 0");
        return;
    }

    int removedDepts = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        //倒序遍历后再 erase，避免正序删导致后续元素前移、跳过紧邻的下一个。
        for (size_t i = m_cachedDepts.size(); i > 0; --i)
        {
            size_t idx = i - 1;
            if (m_cachedDepts[idx].enterpriseid == enterpriseId)
            {
                m_cachedDepts.erase(m_cachedDepts.begin() + idx);
                ++removedDepts;
            }
        }
    }

    //两把锁分开取、不嵌套持有：头文件约定的锁序是 m_mutex→m_userEnterpriseCacheMutex，
    //这里上面那段已经出了作用域、锁已释放，不构成嵌套，也就不存在反向加锁的风险。
    int removedUsers = 0;
    {
        std::lock_guard<std::mutex> guard(m_userEnterpriseCacheMutex);

        std::map<int64_t, int64_t>::iterator it = m_userEnterpriseCache.begin();
        while (it != m_userEnterpriseCache.end())
        {
            if (it->second == enterpriseId)
            {
                //erase(it++) 而不是 erase(it)：后者会让 it 失效，再自增就是未定义行为。
                m_userEnterpriseCache.erase(it++);
                ++removedUsers;
            }
            else
            {
                ++it;
            }
        }
    }

    LOGI("DepartmentManager::removeCachedEnterprise, enterpriseid: %lld, removed depts: %d, removed user-enterprise entries: %d",
         (long long)enterpriseId, removedDepts, removedUsers);
}

int32_t DepartmentManager::createDepartment(int64_t enterpriseId, const dept::OperateDepartmentReq& req, int64_t& outDeptId)
{
    if (req.deptname.empty())
    {
        LOGE("createDepartment failed: empty deptname");
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //1. 解析实际父部门。整棵组织架构树的唯一根是公司根部门，业务上不再产生第二个
    //   顶级部门，因此 parentdeptid 传 0 时把新部门挂到本企业的公司根部门之下。
    int64_t parentDeptId = req.parentdeptid;
    if (parentDeptId == 0)
    {
        parentDeptId = Singleton<EnterpriseManager>::Instance().getRootDeptId(enterpriseId);
        if (parentDeptId == 0)
        {
            //存量数据迁移之前的企业尚无公司根部门，只能退回旧行为建成顶级部门。
            LOGW("createDepartment: enterprise %lld has no root department, fall back to top-level",
                 (long long)enterpriseId);
        }
    }

    //2. 确定父部门的物化路径与层级：parentDeptId 为 0 表示建的就是顶级部门。
    std::string parentPath;
    int32_t parentLevel = 0;
    if (parentDeptId != 0)
    {
        int32_t ignoreParentLevelHolder = 0;
        int64_t ignoreGrandParent = 0;
        if (!queryDept(pConn.get(), parentDeptId, parentPath, ignoreParentLevelHolder, ignoreGrandParent))
        {
            LOGE("createDepartment failed: parent dept not exist, parentdeptid: %lld", (long long)parentDeptId);
            return error_code_dept_invalidparent;
        }
        parentLevel = ignoreParentLevelHolder;
    }

    //3. 同级不允许重名（限定在本企业内）。
    if (isSiblingNameExist(pConn.get(), enterpriseId, parentDeptId, req.deptname, 0))
    {
        LOGE("createDepartment failed: sibling name exists, parentdeptid: %lld, name: %s",
             (long long)parentDeptId, req.deptname.c_str());
        return error_code_dept_nameexist;
    }

    //4. 分配新部门ID，并据父部门路径算出本部门的物化路径与层级。
    int64_t newDeptId = ++m_baseDeptId;
    std::string deptPath = dept::MakeChildPath(parentPath, newDeptId);
    int32_t deptLevel = dept::MakeChildLevel(parentLevel);

    //5. 入库。
    std::string nameEsc = dbapi::EscapeString(req.deptname);
    std::string descEsc = dbapi::EscapeString(req.deptdesc);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_department(f_dept_id, f_enterprise_id, f_dept_name, f_parent_dept_id, f_dept_path, "
        "f_dept_level, f_dept_order, f_leader_user_id, f_dept_desc, f_status, f_create_time) "
        "VALUES(%lld, %lld, '%s', %lld, '%s', %d, %d, %lld, '%s', %d, NOW())",
        (long long)newDeptId, (long long)enterpriseId, nameEsc.c_str(), (long long)parentDeptId, deptPath.c_str(),
        deptLevel, req.deptorder, (long long)req.leaderuserid, descEsc.c_str(),
        department_status_normal);
    if (!pConn->execute(sql))
    {
        LOGE("createDepartment failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outDeptId = newDeptId;
    loadFromDb();
    LOGI("createDepartment ok, deptid: %lld, name: %s, parentdeptid: %lld, path: %s",
         (long long)newDeptId, req.deptname.c_str(), (long long)parentDeptId, deptPath.c_str());
    return error_code_ok;
}

int32_t DepartmentManager::createEnterpriseRootDepartment(int64_t enterpriseId, const std::string& deptName,
                                                          int64_t& outDeptId)
{
    if (enterpriseId == 0 || deptName.empty())
    {
        LOGE("createEnterpriseRootDepartment failed: invalid arg, enterpriseid: %lld, name: %s",
             (long long)enterpriseId, deptName.c_str());
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //公司根部门显式建根：父部门恒为 0、层级恒为最外层，物化路径就是它自己。
    //这里刻意不走 createDepartment，因为那里会把 parentdeptid 为 0 重定向到公司
    //根部门，而新建企业时根部门恰恰还不存在，重定向将无法自举。
    int64_t newDeptId = ++m_baseDeptId;
    std::string deptPath = dept::MakeChildPath(std::string(), newDeptId);

    std::string nameEsc = dbapi::EscapeString(deptName);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_department(f_dept_id, f_enterprise_id, f_dept_name, f_parent_dept_id, f_dept_path, "
        "f_dept_level, f_dept_order, f_leader_user_id, f_dept_desc, f_status, f_create_time) "
        "VALUES(%lld, %lld, '%s', %d, '%s', %d, 0, 0, '', %d, NOW())",
        (long long)newDeptId, (long long)enterpriseId, nameEsc.c_str(), DEPT_ROOT_PARENT_ID, deptPath.c_str(),
        DEPT_ROOT_LEVEL, department_status_normal);
    if (!pConn->execute(sql))
    {
        LOGE("createEnterpriseRootDepartment failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    outDeptId = newDeptId;
    loadFromDb();
    LOGI("createEnterpriseRootDepartment ok, enterpriseid: %lld, rootdeptid: %lld, name: %s",
         (long long)enterpriseId, (long long)newDeptId, deptName.c_str());
    return error_code_ok;
}

int32_t DepartmentManager::renameDepartment(int64_t deptId, const std::string& newName)
{
    if (newName.empty())
    {
        LOGE("renameDepartment failed: empty name, deptid: %lld", (long long)deptId);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //部门必须存在且在用。
    std::string ignorePath;
    int32_t ignoreLevel = 0;
    int64_t ignoreParent = 0;
    if (!queryDept(pConn.get(), deptId, ignorePath, ignoreLevel, ignoreParent))
    {
        LOGE("renameDepartment failed: dept not exist, deptid: %lld", (long long)deptId);
        return error_code_dept_notexist;
    }

    //刻意不做同级重名校验：本方法的用途是让公司根部门的名字跟随企业全称，
    //而公司根部门在本企业内没有同级部门，校验没有意义反而会挡住正常改名。
    std::string nameEsc = dbapi::EscapeString(newName);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_department SET f_dept_name = '%s' WHERE f_dept_id = %lld AND f_status = %d",
        nameEsc.c_str(), (long long)deptId, department_status_normal);
    if (!pConn->execute(sql))
    {
        LOGE("renameDepartment failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("renameDepartment ok, deptid: %lld, name: %s", (long long)deptId, newName.c_str());
    return error_code_ok;
}

int32_t DepartmentManager::updateDepartment(const dept::OperateDepartmentReq& req)
{
    if (req.deptname.empty())
    {
        LOGE("updateDepartment failed: empty deptname, deptid: %lld", (long long)req.deptid);
        return error_code_unknown;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //部门必须存在；同时取出其父部门ID用于同级重名校验。
    std::string ignorePath;
    int32_t ignoreLevel = 0;
    int64_t parentDeptId = 0;
    if (!queryDept(pConn.get(), req.deptid, ignorePath, ignoreLevel, parentDeptId))
    {
        LOGE("updateDepartment failed: dept not exist, deptid: %lld", (long long)req.deptid);
        return error_code_dept_notexist;
    }

    //同级不允许重名（排除自身），限定在本部门所属企业内。
    int64_t enterpriseId = queryDeptEnterpriseId(pConn.get(), req.deptid);
    if (isSiblingNameExist(pConn.get(), enterpriseId, parentDeptId, req.deptname, req.deptid))
    {
        LOGE("updateDepartment failed: sibling name exists, deptid: %lld, name: %s",
             (long long)req.deptid, req.deptname.c_str());
        return error_code_dept_nameexist;
    }

    std::string nameEsc = dbapi::EscapeString(req.deptname);
    std::string descEsc = dbapi::EscapeString(req.deptdesc);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_department SET f_dept_name = '%s', f_dept_desc = '%s', "
        "f_leader_user_id = %lld, f_dept_order = %d WHERE f_dept_id = %lld AND f_status = 0",
        nameEsc.c_str(), descEsc.c_str(), (long long)req.leaderuserid, req.deptorder,
        (long long)req.deptid);
    if (!pConn->execute(sql))
    {
        LOGE("updateDepartment failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("updateDepartment ok, deptid: %lld", (long long)req.deptid);
    return error_code_ok;
}

int32_t DepartmentManager::deleteDepartment(int64_t deptId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //部门必须存在。
    std::string ignorePath;
    int32_t ignoreLevel = 0;
    int64_t ignoreParent = 0;
    if (!queryDept(pConn.get(), deptId, ignorePath, ignoreLevel, ignoreParent))
    {
        LOGE("deleteDepartment failed: dept not exist, deptid: %lld", (long long)deptId);
        return error_code_dept_notexist;
    }

    //公司根部门代表企业本身，删掉它整棵组织架构树就没有根了，一律拒绝。
    //要连企业一起删时走 EnterpriseManager::deleteEnterprise 的级联删除。
    int64_t ownerEnterpriseId = queryDeptEnterpriseId(pConn.get(), deptId);
    if (ownerEnterpriseId != 0 &&
        Singleton<EnterpriseManager>::Instance().getRootDeptId(ownerEnterpriseId) == deptId)
    {
        LOGE("deleteDepartment failed: is enterprise root department, deptid: %lld, enterpriseid: %lld",
             (long long)deptId, (long long)ownerEnterpriseId);
        return error_code_dept_isroot;
    }

    //其下若仍有在用子部门或在职成员，不允许删除。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department WHERE f_parent_dept_id = %lld AND f_status = 0",
        (long long)deptId);
    if (queryCount(pConn.get(), sql) > 0)
    {
        LOGE("deleteDepartment failed: still has sub-departments, deptid: %lld", (long long)deptId);
        return error_code_dept_hassub;
    }

    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department_member WHERE f_dept_id = %lld AND f_status = 0",
        (long long)deptId);
    if (queryCount(pConn.get(), sql) > 0)
    {
        LOGE("deleteDepartment failed: still has members, deptid: %lld", (long long)deptId);
        return error_code_dept_hassub;
    }

    //软删除：置为停用，保留历史数据。
    snprintf(sql, sizeof(sql),
        "UPDATE t_department SET f_status = %d WHERE f_dept_id = %lld",
        department_status_disabled, (long long)deptId);
    if (!pConn->execute(sql))
    {
        LOGE("deleteDepartment failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("deleteDepartment ok, deptid: %lld", (long long)deptId);
    return error_code_ok;
}

int32_t DepartmentManager::moveDepartment(int64_t deptId, int64_t newParentDeptId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //被移动部门必须存在，取出其旧物化路径与旧层级。
    std::string oldPath;
    int32_t oldLevel = 0;
    int64_t oldParentId = 0;
    if (!queryDept(pConn.get(), deptId, oldPath, oldLevel, oldParentId))
    {
        LOGE("moveDepartment failed: dept not exist, deptid: %lld", (long long)deptId);
        return error_code_dept_notexist;
    }

    //取本企业的公司根部门，用于下面两处判断。
    int64_t ownerEnterpriseId = queryDeptEnterpriseId(pConn.get(), deptId);
    int64_t rootDeptId = 0;
    if (ownerEnterpriseId != 0)
    {
        rootDeptId = Singleton<EnterpriseManager>::Instance().getRootDeptId(ownerEnterpriseId);
    }

    //公司根部门是整棵组织架构树的根，把它挂到别的部门下会让树失去根，一律拒绝。
    if (rootDeptId != 0 && rootDeptId == deptId)
    {
        LOGE("moveDepartment failed: cannot move enterprise root department, deptid: %lld, enterpriseid: %lld",
             (long long)deptId, (long long)ownerEnterpriseId);
        return error_code_dept_isroot;
    }

    //newParentDeptId 为 0 的含义已由"移动到顶层"改为"移动到本企业的公司根部门之下"，
    //与 createDepartment 保持一致；企业尚无公司根部门时才退回旧含义。
    if (newParentDeptId == 0 && rootDeptId != 0)
    {
        newParentDeptId = rootDeptId;
    }

    //确定新父部门的物化路径与层级：newParentDeptId 为 0 表示移动到顶层。
    std::string newParentPath;
    int32_t newParentLevel = 0;
    if (newParentDeptId != 0)
    {
        std::string parentPath;
        int32_t parentLevel = 0;
        int64_t grandParentId = 0;
        if (!queryDept(pConn.get(), newParentDeptId, parentPath, parentLevel, grandParentId))
        {
            LOGE("moveDepartment failed: new parent not exist, newparentdeptid: %lld",
                 (long long)newParentDeptId);
            return error_code_dept_invalidparent;
        }
        newParentPath = parentPath;
        newParentLevel = parentLevel;
    }

    //目标父部门不能是被移动部门自身或其子孙，否则部门树会成环。
    if (dept::WouldFormCycle(oldPath, newParentPath))
    {
        LOGE("moveDepartment failed: would form cycle, deptid: %lld, newparentdeptid: %lld",
             (long long)deptId, (long long)newParentDeptId);
        return error_code_dept_invalidparent;
    }

    //算出被移动部门的新路径、新层级，以及整棵子树的层级增量。
    std::string newPath = dept::MakeChildPath(newParentPath, deptId);
    int32_t newLevel = dept::MakeChildLevel(newParentLevel);
    int32_t levelDelta = newLevel - oldLevel;

    //1) 一条 UPDATE 改写整棵子树（含被移动部门自身）的物化路径与层级：
    //   旧路径前缀 oldPath 整体替换为 newPath，层级统一加 levelDelta。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_department SET f_dept_path = CONCAT('%s', SUBSTRING(f_dept_path, %d)), "
        "f_dept_level = f_dept_level + %d WHERE f_dept_path LIKE '%s%%'",
        newPath.c_str(), (int)(oldPath.size() + 1), levelDelta, oldPath.c_str());
    if (!pConn->execute(sql))
    {
        LOGE("moveDepartment failed: rewrite subtree path error, sql: %s", sql);
        return error_code_unknown;
    }

    //2) 修正被移动部门自身的父部门ID。
    snprintf(sql, sizeof(sql),
        "UPDATE t_department SET f_parent_dept_id = %lld WHERE f_dept_id = %lld",
        (long long)newParentDeptId, (long long)deptId);
    if (!pConn->execute(sql))
    {
        LOGE("moveDepartment failed: update parent error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("moveDepartment ok, deptid: %lld, newparentdeptid: %lld", (long long)deptId, (long long)newParentDeptId);
    return error_code_ok;
}

bool DepartmentManager::getDepartmentDetail(int64_t deptId, DepartmentDetail& out)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return false;

    //1. 取部门基础字段；部门必须存在且在用（f_status = 0）。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_enterprise_id, f_dept_name, f_parent_dept_id, f_dept_level, f_dept_order, "
        "f_leader_user_id, f_dept_desc, f_dept_path FROM t_department "
        "WHERE f_dept_id = %lld AND f_status = 0",
        (long long)deptId);
    QueryResult* pResult = pConn->query(sql);
    if (pResult == NULL)
        return false;

    std::string deptPath;
    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.deptid       = deptId;
        out.enterpriseid = pRow[0].getInt32();
        out.deptname     = pRow[1].getString();
        out.parentdeptid = pRow[2].getInt32();
        out.deptlevel    = pRow[3].getInt32();
        out.deptorder    = pRow[4].getInt32();
        out.leaderuserid = pRow[5].getInt32();
        out.deptdesc     = pRow[6].getString();
        deptPath         = pRow[7].getString();
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    if (!found)
        return false;

    //2. 负责人姓名：仅在指定了负责人时查 t_user，取昵称展示。
    if (out.leaderuserid != 0)
    {
        snprintf(sql, sizeof(sql),
            "SELECT f_nickname FROM t_user WHERE f_user_id = %lld",
            (long long)out.leaderuserid);
        pResult = pConn->query(sql);
        if (pResult != NULL)
        {
            pRow = pResult->fetch();
            if (pRow != NULL)
                out.leadername = pRow[0].getString();
            pResult->endQuery();
            delete pResult;
        }
    }

    //3. 直属在职成员数。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department_member WHERE f_dept_id = %lld AND f_status = %d",
        (long long)deptId, deptmember_status_active);
    int32_t cnt = queryCount(pConn.get(), sql);
    out.directMembers = (cnt > 0) ? cnt : 0;

    //4. 直接在用子部门数。
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM t_department WHERE f_parent_dept_id = %lld AND f_status = 0",
        (long long)deptId);
    cnt = queryCount(pConn.get(), sql);
    out.subDepts = (cnt > 0) ? cnt : 0;

    //5. 子树在职成员去重合计：本部门物化路径为前缀的所有在用部门内的在职成员。
    //   一名员工可兼属多个子部门，故按 f_user_id 去重。
    if (!deptPath.empty())
    {
        std::string pathEsc = dbapi::EscapeString(deptPath);
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(DISTINCT m.f_user_id) FROM t_department_member m "
            "JOIN t_department d ON m.f_dept_id = d.f_dept_id "
            "WHERE d.f_dept_path LIKE '%s%%' AND d.f_status = 0 AND m.f_status = %d",
            pathEsc.c_str(), deptmember_status_active);
        cnt = queryCount(pConn.get(), sql);
        out.totalMembers = (cnt > 0) ? cnt : 0;
    }
    else
    {
        //物化路径异常缺失时退化为直属成员数，避免 LIKE '%' 误统计全表。
        out.totalMembers = out.directMembers;
    }

    return true;
}

int32_t DepartmentManager::reorderDepartments(int64_t enterpriseId, int64_t parentDeptId,
                                              const std::vector<int64_t>& orderedIds)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //逐个把部门的 f_dept_order 刷成其在数组中的下标。
    //UPDATE 同时以 f_parent_dept_id 限定，确保只影响这批真正的同级部门；
    //enterpriseId 非0时再加企业过滤，防止跨租户误改。
    for (size_t i = 0; i < orderedIds.size(); ++i)
    {
        char sql[DEPT_SQL_BUF_LEN] = { 0 };
        if (enterpriseId != 0)
        {
            snprintf(sql, sizeof(sql),
                "UPDATE t_department SET f_dept_order = %d "
                "WHERE f_dept_id = %lld AND f_parent_dept_id = %lld "
                "AND f_enterprise_id = %lld AND f_status = 0",
                (int)i, (long long)orderedIds[i], (long long)parentDeptId,
                (long long)enterpriseId);
        }
        else
        {
            snprintf(sql, sizeof(sql),
                "UPDATE t_department SET f_dept_order = %d "
                "WHERE f_dept_id = %lld AND f_parent_dept_id = %lld AND f_status = 0",
                (int)i, (long long)orderedIds[i], (long long)parentDeptId);
        }
        if (!pConn->execute(sql))
        {
            LOGE("reorderDepartments failed: db execute error, sql: %s", sql);
            return error_code_unknown;
        }
    }

    loadFromDb();
    LOGI("reorderDepartments ok, parentdeptid: %lld, count: %u",
         (long long)parentDeptId, (uint32_t)orderedIds.size());
    return error_code_ok;
}

int64_t DepartmentManager::getDepartmentEnterpriseId(int64_t deptId)
{
    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return 0;

    return queryDeptEnterpriseId(pConn.get(), deptId);
}

int32_t DepartmentManager::addMember(const dept::OperateDeptMemberReq& req)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //部门必须存在。
    std::string ignorePath;
    int32_t ignoreLevel = 0;
    int64_t ignoreParent = 0;
    if (!queryDept(pConn.get(), req.deptid, ignorePath, ignoreLevel, ignoreParent))
    {
        LOGE("addMember failed: dept not exist, deptid: %lld", (long long)req.deptid);
        return error_code_dept_notexist;
    }
    //成员记录冗余存所属企业ID，与所在部门保持一致，便于按租户隔离查询。
    int64_t enterpriseId = queryDeptEnterpriseId(pConn.get(), req.deptid);

    //查该用户在该部门是否已有成员记录（含已移出的历史记录）。
    //t_department_member 对 (f_dept_id, f_user_id) 有唯一键，故曾被移出的成员只能复用原行。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_status FROM t_department_member WHERE f_dept_id = %lld AND f_user_id = %lld",
        (long long)req.deptid, (long long)req.userid);
    QueryResult* pResult = pConn->query(sql);
    int32_t existStatus = -1;   //-1 表示该部门下无此用户的任何记录
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            existStatus = pRow[0].getInt32();
        pResult->endQuery();
        delete pResult;
    }

    if (existStatus == deptmember_status_active)
    {
        LOGE("addMember failed: user already in dept, deptid: %lld, userid: %lld",
             (long long)req.deptid, (long long)req.userid);
        return error_code_deptmember_exist;
    }

    std::string posEsc = dbapi::EscapeString(req.position);
    if (existStatus == deptmember_status_removed)
    {
        //曾被移出：复用原行，重新置为在职。
        snprintf(sql, sizeof(sql),
            "UPDATE t_department_member SET f_position = '%s', f_is_primary = %d, "
            "f_member_order = %d, f_status = %d, f_join_time = NOW() "
            "WHERE f_dept_id = %lld AND f_user_id = %lld",
            posEsc.c_str(), req.isprimary, req.memberorder, deptmember_status_active,
            (long long)req.deptid, (long long)req.userid);
    }
    else
    {
        //无记录：新增一行。
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_department_member(f_dept_id, f_enterprise_id, f_user_id, f_position, f_is_primary, "
            "f_member_order, f_status, f_join_time) VALUES(%lld, %lld, %lld, '%s', %d, %d, %d, NOW())",
            (long long)req.deptid, (long long)enterpriseId, (long long)req.userid, posEsc.c_str(),
            req.isprimary, req.memberorder, deptmember_status_active);
    }
    if (!pConn->execute(sql))
    {
        LOGE("addMember failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("addMember ok, deptid: %lld, userid: %lld", (long long)req.deptid, (long long)req.userid);
    return error_code_ok;
}

int32_t DepartmentManager::updateMember(const dept::OperateDeptMemberReq& req)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    if (!isActiveMember(pConn.get(), req.deptid, req.userid))
    {
        LOGE("updateMember failed: user not in dept, deptid: %lld, userid: %lld",
             (long long)req.deptid, (long long)req.userid);
        return error_code_deptmember_notexist;
    }

    std::string posEsc = dbapi::EscapeString(req.position);
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_department_member SET f_position = '%s', f_is_primary = %d, f_member_order = %d "
        "WHERE f_dept_id = %lld AND f_user_id = %lld AND f_status = %d",
        posEsc.c_str(), req.isprimary, req.memberorder,
        (long long)req.deptid, (long long)req.userid, deptmember_status_active);
    if (!pConn->execute(sql))
    {
        LOGE("updateMember failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("updateMember ok, deptid: %lld, userid: %lld", (long long)req.deptid, (long long)req.userid);
    return error_code_ok;
}

int32_t DepartmentManager::removeMember(int64_t deptId, int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    if (!isActiveMember(pConn.get(), deptId, userId))
    {
        LOGE("removeMember failed: user not in dept, deptid: %lld, userid: %lld",
             (long long)deptId, (long long)userId);
        return error_code_deptmember_notexist;
    }

    //软删除：置为已移出，保留历史数据。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "UPDATE t_department_member SET f_status = %d WHERE f_dept_id = %lld AND f_user_id = %lld",
        deptmember_status_removed, (long long)deptId, (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("removeMember failed: db execute error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("removeMember ok, deptid: %lld, userid: %lld", (long long)deptId, (long long)userId);
    return error_code_ok;
}

int32_t DepartmentManager::moveMember(int64_t deptId, int64_t userId, int64_t newDeptId)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::unique_ptr<IDatabase> pConn(createDbConnection());
    if (!pConn)
        return error_code_unknown;

    //源部门里必须有该在职成员。
    if (!isActiveMember(pConn.get(), deptId, userId))
    {
        LOGE("moveMember failed: user not in source dept, deptid: %lld, userid: %lld",
             (long long)deptId, (long long)userId);
        return error_code_deptmember_notexist;
    }

    //目标部门必须存在。
    std::string ignorePath;
    int32_t ignoreLevel = 0;
    int64_t ignoreParent = 0;
    if (!queryDept(pConn.get(), newDeptId, ignorePath, ignoreLevel, ignoreParent))
    {
        LOGE("moveMember failed: target dept not exist, newdeptid: %lld", (long long)newDeptId);
        return error_code_dept_notexist;
    }
    //目标部门所属企业ID，用于迁入的成员记录的 f_enterprise_id。
    int64_t enterpriseId = queryDeptEnterpriseId(pConn.get(), newDeptId);

    //目标部门里不能已有该在职成员。
    if (isActiveMember(pConn.get(), newDeptId, userId))
    {
        LOGE("moveMember failed: user already in target dept, newdeptid: %lld, userid: %lld",
             (long long)newDeptId, (long long)userId);
        return error_code_deptmember_exist;
    }

    //读出该成员在源部门的归属属性，迁移时一并带到目标部门。
    char sql[DEPT_SQL_BUF_LEN] = { 0 };
    snprintf(sql, sizeof(sql),
        "SELECT f_position, f_is_primary, f_member_order FROM t_department_member "
        "WHERE f_dept_id = %lld AND f_user_id = %lld AND f_status = %d",
        (long long)deptId, (long long)userId, deptmember_status_active);
    std::string position;
    int32_t isPrimary = 0;
    int32_t memberOrder = 0;
    QueryResult* pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            position    = pRow[0].getString();
            isPrimary   = pRow[1].getInt32();
            memberOrder = pRow[2].getInt32();
        }
        pResult->endQuery();
        delete pResult;
    }

    //1) 从源部门移出。
    snprintf(sql, sizeof(sql),
        "UPDATE t_department_member SET f_status = %d WHERE f_dept_id = %lld AND f_user_id = %lld",
        deptmember_status_removed, (long long)deptId, (long long)userId);
    if (!pConn->execute(sql))
    {
        LOGE("moveMember failed: remove from source error, sql: %s", sql);
        return error_code_unknown;
    }

    //2) 加入目标部门：目标部门若有该用户的历史移出记录则复用原行，否则新增。
    std::string posEsc = dbapi::EscapeString(position);
    snprintf(sql, sizeof(sql),
        "SELECT f_status FROM t_department_member WHERE f_dept_id = %lld AND f_user_id = %lld",
        (long long)newDeptId, (long long)userId);
    int32_t targetExistStatus = -1;
    pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            targetExistStatus = pRow[0].getInt32();
        pResult->endQuery();
        delete pResult;
    }

    if (targetExistStatus == -1)
    {
        snprintf(sql, sizeof(sql),
            "INSERT INTO t_department_member(f_dept_id, f_enterprise_id, f_user_id, f_position, f_is_primary, "
            "f_member_order, f_status, f_join_time) VALUES(%lld, %lld, %lld, '%s', %d, %d, %d, NOW())",
            (long long)newDeptId, (long long)enterpriseId, (long long)userId, posEsc.c_str(),
            isPrimary, memberOrder, deptmember_status_active);
    }
    else
    {
        snprintf(sql, sizeof(sql),
            "UPDATE t_department_member SET f_position = '%s', f_is_primary = %d, "
            "f_member_order = %d, f_status = %d, f_join_time = NOW() "
            "WHERE f_dept_id = %lld AND f_user_id = %lld",
            posEsc.c_str(), isPrimary, memberOrder, deptmember_status_active,
            (long long)newDeptId, (long long)userId);
    }
    if (!pConn->execute(sql))
    {
        LOGE("moveMember failed: add to target error, sql: %s", sql);
        return error_code_unknown;
    }

    loadFromDb();
    LOGI("moveMember ok, userid: %lld, from dept %lld to dept %lld",
         (long long)userId, (long long)deptId, (long long)newDeptId);
    return error_code_ok;
}
