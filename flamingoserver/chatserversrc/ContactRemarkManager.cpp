/**
 *  联系人备注名存储管理器实现，ContactRemarkManager.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "ContactRemarkManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"   //EscapeString / UpsertClause
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"              //error_code

//t_contact_remark 的 UPSERT 冲突列。必须与该表主键的列完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define CONTACT_REMARK_CONFLICT_COLUMNS   "f_userid, f_target_userid"

ContactRemarkManager::ContactRemarkManager()
{
}

ContactRemarkManager::~ContactRemarkManager()
{
}

bool ContactRemarkManager::getRemarks(int64_t userid, std::map<int32_t, std::string>& out)
{
    out.clear();

    if (userid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ContactRemarkManager::getRemarks db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //命中主键前缀（也命中 idx_user 索引），一个用户的备注全在连续的几行里。
    std::ostringstream osSql;
    osSql << "SELECT f_target_userid, f_remark FROM t_contact_remark WHERE f_userid = " << userid;

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //查询失败与"一条备注都没设"是两回事，但 dbapi 在结果集为空时同样返回 NULL，
        //此处无从区分，只能一并按"没有备注"处理并返回 true —— 备注拉不到不该
        //连累整个通讯录应答。真正的连接失败在上面那个分支里已经报了 false。
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        const int32_t targetUserId = static_cast<int32_t>(pRow[0].getUInt64());
        const std::string remark = pRow[1].getCppString();

        //空串行理论上不存在（清除备注走的是 DELETE），万一存量数据里混进来了也跳过，
        //免得客户端把某个联系人的名字显示成空白
        if (!remark.empty())
            out[targetUserId] = remark;

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ContactRemarkManager::setRemark(int64_t userid, int64_t targetUserId,
                                     const std::string& remark, int64_t now, int& outErrorCode)
{
    outErrorCode = error_code_ok;

    //取值域兜底。正常流程里会话层已经拦过一道，这里再判一次是防着日后
    //从别的路径（如 admin 控制台）调进来。
    if (userid <= 0 || targetUserId <= 0 || userid == targetUserId)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    if (static_cast<int>(remark.size()) > kMaxContactRemarkBytes)
    {
        LOGI("ContactRemarkManager::setRemark rejected by length, userid: %lld, target: %lld, bytes: %d",
             (long long)userid, (long long)targetUserId, static_cast<int>(remark.size()));
        outErrorCode = error_code_bad_request;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ContactRemarkManager::setRemark db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    if (remark.empty())
    {
        //清除备注：直接删掉那一行，不留空串行。这样表里的每一行都是"确实设过备注"，
        //getRemarks 不必再过滤，通讯录侧也不会为一堆空备注白跑一趟。
        //删到 0 行也算成功 —— 用户连点两下"清除备注"不该在第二下收到报错。
        std::ostringstream osSql;
        osSql << "DELETE FROM t_contact_remark WHERE f_userid = " << userid
              << " AND f_target_userid = " << targetUserId;
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("ContactRemarkManager::setRemark delete error, userid: %lld, target: %lld",
                 (long long)userid, (long long)targetUserId);
            outErrorCode = error_code_server_internal;
            return false;
        }

        LOGI("clear contact remark ok, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        return true;
    }

    //备注名是用户自由输入的文本，可能含单引号 / 反斜杠 / 中文，<u>必须</u>转义后再拼进 SQL。
    //这里用 dbapi::EscapeString 而不是 IDatabase::escapeString，是为了与 SqlDialect
    //提供的"不持连接也能转义"的统一入口保持一致；两者对本场景等价。
    const std::string escRemark = dbapi::EscapeString(remark);

    //按主键 (f_userid, f_target_userid) UPSERT：重复设置同一个人的备注就是覆盖。
    //冲突子句两种后端写法不同（SQLite 必须写明冲突列），由 SqlDialect 生成；
    //赋值部分的文本两边通用，无需区分。
    std::ostringstream osSql;
    osSql << "INSERT INTO t_contact_remark(f_userid, f_target_userid, f_remark, f_update_time) VALUES("
          << userid << ", " << targetUserId << ", '" << escRemark << "', " << now
          << ")" << dbapi::UpsertClause(CONTACT_REMARK_CONFLICT_COLUMNS)
          << "f_remark='" << escRemark << "', f_update_time=" << now;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ContactRemarkManager::setRemark upsert error, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("set contact remark ok, userid: %lld, target: %lld, remark: %s",
         (long long)userid, (long long)targetUserId, remark.c_str());
    return true;
}
