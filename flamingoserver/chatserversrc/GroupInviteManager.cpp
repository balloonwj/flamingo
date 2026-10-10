/**
 *  入群邀请表 t_group_invite 的读写实现，说明见 GroupInviteManager.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupInviteManager.h"
#include <memory>
#include <sstream>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

namespace {

//查询 t_group_invite 的列清单，fillInviteRecord 按这个顺序取值
const char* const kInviteSelectColumns =
    "f_id, f_groupid, f_inviter, f_invitee, f_history_limit, f_state, f_msgid, f_create_time, f_expire_time, "
    "f_handle_time";

//按 kInviteSelectColumns 的列序填一条邀请
void fillInviteRecord(Field* pRow, GroupInviteRecord& rec)
{
    rec.m_id           = static_cast<int64_t>(pRow[0].getUInt64());
    rec.m_groupid      = static_cast<int64_t>(pRow[1].getUInt64());
    rec.m_inviter      = static_cast<int64_t>(pRow[2].getUInt64());
    rec.m_invitee      = static_cast<int64_t>(pRow[3].getUInt64());
    rec.m_historyLimit = pRow[4].getInt32();
    rec.m_state        = pRow[5].getInt32();
    rec.m_msgid        = static_cast<int64_t>(pRow[6].getUInt64());
    rec.m_createTime   = static_cast<int64_t>(pRow[7].getUInt64());
    rec.m_expireTime   = static_cast<int64_t>(pRow[8].getUInt64());
    rec.m_handleTime   = static_cast<int64_t>(pRow[9].getUInt64());
}

//打开一条数据库连接；失败时记日志并返回 NULL
IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("GroupInviteManager::%s db init failed, db: %s", caller, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

} // namespace

GroupInviteManager::GroupInviteManager()
{
}

GroupInviteManager::~GroupInviteManager()
{
}

int64_t GroupInviteManager::addInvite(const GroupInviteRecord& rec)
{
    std::unique_ptr<IDatabase> pConn(openConnection("addInvite"));
    if (!pConn)
        return 0;

    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_invite(f_groupid, f_inviter, f_invitee, f_history_limit, f_state, f_msgid, "
             "f_create_time, f_expire_time, f_handle_time) VALUES("
          << rec.m_groupid << ", " << rec.m_inviter << ", " << rec.m_invitee << ", " << rec.m_historyLimit << ", "
          << rec.m_state << ", " << rec.m_msgid << ", " << rec.m_createTime << ", " << rec.m_expireTime << ", "
          << rec.m_handleTime << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupInviteManager::addInvite insert error, groupid: %lld, inviter: %lld, invitee: %lld",
             (long long)rec.m_groupid, (long long)rec.m_inviter, (long long)rec.m_invitee);
        return 0;
    }

    //取回自增主键作为邀请 id（须与 INSERT 同一连接）
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
    LOGI("group invite added, id: %lld, groupid: %lld, inviter: %lld, invitee: %lld, historylimit: %d",
         (long long)id, (long long)rec.m_groupid, (long long)rec.m_inviter, (long long)rec.m_invitee,
         rec.m_historyLimit);
    return id;
}

bool GroupInviteManager::getInvite(int64_t id, GroupInviteRecord& out)
{
    std::unique_ptr<IDatabase> pConn(openConnection("getInvite"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT " << kInviteSelectColumns << " FROM t_group_invite WHERE f_id = " << id;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillInviteRecord(pRow, out);
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool GroupInviteManager::setMsgId(int64_t id, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setMsgId"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "UPDATE t_group_invite SET f_msgid = " << msgid << " WHERE f_id = " << id;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupInviteManager::setMsgId error, id: %lld, msgid: %lld", (long long)id, (long long)msgid);
        return false;
    }
    return true;
}

bool GroupInviteManager::updateState(int64_t id, int state, int64_t handleTime)
{
    std::unique_ptr<IDatabase> pConn(openConnection("updateState"));
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "UPDATE t_group_invite SET f_state = " << state << ", f_handle_time = " << handleTime
          << " WHERE f_id = " << id;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupInviteManager::updateState error, id: %lld, state: %d", (long long)id, state);
        return false;
    }
    LOGI("group invite state updated, id: %lld, state: %d", (long long)id, state);
    return true;
}
