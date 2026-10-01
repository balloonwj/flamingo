/**
 *  消息 Pin 存储管理器实现，MessagePinManager.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "MessagePinManager.h"

#include <memory>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"      //conversation_type
#include "MessageDeleteRange.h"   //按时间范围清理的区间过滤条件，与 MessageManager 共用同一份

//联表查询里各列的固定顺序：前 9 列是消息本体（与 MessageManager 里那份 SELECT 列表
//刻意保持同序，便于两处对照），末尾两列是 Pin 元信息。fillPinnedRow 按这个顺序读，
//两者必须一起改。
//Pin 元信息跟着同一条查询一起取回，而不是逐条再查一次 —— 200 条就是 200 次建连接。
static const char* const PINNED_MESSAGE_COLUMNS =
    "m.f_msgid, m.f_clientmsgid, m.f_ctype, m.f_from, m.f_to, m.f_msgtype, "
    "m.f_content, m.f_recalled, m.f_create_time, p.f_operator, p.f_pin_time";

//把一行联表查询结果拆进 MessageRecord 与 PinRecord。列序须与 PINNED_MESSAGE_COLUMNS 一致。
static void fillPinnedRow(Field* pRow, MessageRecord& rec, PinRecord& pin)
{
    rec.msgid       = static_cast<int64_t>(pRow[0].getUInt64());
    rec.clientmsgid = pRow[1].getCppString();
    rec.ctype       = pRow[2].getInt32();
    rec.from        = pRow[3].getInt32();
    rec.to          = pRow[4].getInt32();
    rec.msgtype     = pRow[5].getInt32();
    rec.recalled    = (pRow[7].getInt32() != 0);
    rec.timestamp   = static_cast<int64_t>(pRow[8].getUInt64());
    //从 f_content 解出 content/mentions/mentionall/quote
    std::string blob = pRow[6].getCppString();
    MessageProtocol::parseStoredContent(blob, rec.content, rec.mentions,
                                        rec.mentionall, rec.quote);

    pin.msgid      = rec.msgid;
    pin.operatorId = static_cast<int64_t>(pRow[9].getUInt64());
    pin.pinTime    = static_cast<int64_t>(pRow[10].getUInt64());
}

//生成"这条消息属于 (ctype, cid) 这个会话、且未撤回、且 selfUserId 没在本端删掉它"
//的 WHERE 条件（不含前导 WHERE，供 COUNT 与分页查询共用，避免两处口径漂移）。
//单聊要取双向消息：我发给对方的、对方发给我的，都算这个会话里的。
//    ctype       会话类型（conversation_single / conversation_group）
//    cid         会话ID（单聊为对端userid，群聊为群id）
//    selfUserId  发起拉取的用户userid
static std::string buildPinnedWhere(int ctype, int64_t cid, int64_t selfUserId)
{
    std::ostringstream os;
    if (ctype == conversation_group)
    {
        os << "m.f_ctype = " << conversation_group << " AND m.f_to = " << cid;
    }
    else
    {
        os << "m.f_ctype = " << conversation_single
           << " AND ((m.f_from = " << selfUserId << " AND m.f_to = " << cid << ")"
           << " OR (m.f_from = " << cid << " AND m.f_to = " << selfUserId << "))";
    }

    //已撤回的不返回。撤回时会顺手删掉它的Pin行，这里是双保险、防住历史脏数据。
    os << " AND m.f_recalled = 0";

    //排除该用户本端已删除的消息。口径与 MessageManager 的 notDeletedClause 一致，
    //区别只是这里必须写成 m.f_msgid —— 联表之后裸 f_msgid 有歧义。
    os << " AND m.f_msgid NOT IN (SELECT f_msgid FROM t_message_deleted WHERE f_userid = "
       << selfUserId << ")";

    //再排除该用户按时间范围清理掉的消息（cmd_delete_messages_by_time）。时间列同样要带
    //别名 m，理由同上
    os << msgdelete::BuildNotInDeletedRangeClause(selfUserId, "m.f_create_time");
    return os.str();
}

MessagePinManager::MessagePinManager()
{
}

MessagePinManager::~MessagePinManager()
{
}

bool MessagePinManager::init()
{
    return true;
}

bool MessagePinManager::pinMessage(int64_t msgid, int64_t operatorId, int64_t now)
{
    if (msgid <= 0)
    {
        LOGE("MessagePinManager::pinMessage invalid msgid: %lld", (long long)msgid);
        return false;
    }

    //幂等短路：已经钉着就直接成功返回，不重复写库、更不刷新 f_operator 与
    //f_pin_time —— 否则甲钉的消息被乙点一下"Pin"，取消权限就悄悄转到乙手里了。
    PinRecord exist;
    if (getPinRecord(msgid, exist))
    {
        return true;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessagePinManager::pinMessage db init failed, msgid: %lld, db: %s",
             (long long)msgid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_message_pin(f_msgid, f_operator, f_pin_time) VALUES("
          << msgid << ", " << operatorId << ", " << now << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessagePinManager::pinMessage error, msgid: %lld, operator: %lld",
             (long long)msgid, (long long)operatorId);
        return false;
    }
    return true;
}

bool MessagePinManager::unpinMessage(int64_t msgid)
{
    if (msgid <= 0)
    {
        LOGE("MessagePinManager::unpinMessage invalid msgid: %lld", (long long)msgid);
        return false;
    }
    return removePinForMessage(msgid);
}

bool MessagePinManager::getPinRecord(int64_t msgid, PinRecord& out)
{
    if (msgid <= 0)
    {
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessagePinManager::getPinRecord db init failed, msgid: %lld, db: %s",
             (long long)msgid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_operator, f_pin_time FROM t_message_pin WHERE f_msgid = "
          << msgid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        return false;
    }

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        out.msgid      = static_cast<int64_t>(pRow[0].getUInt64());
        out.operatorId = static_cast<int64_t>(pRow[1].getUInt64());
        out.pinTime    = static_cast<int64_t>(pRow[2].getUInt64());
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool MessagePinManager::getPinnedMessages(int ctype, int64_t cid, int64_t selfUserId,
                                          int limit, std::vector<MessageRecord>& out,
                                          std::vector<PinRecord>& outPins, int64_t& outTotal)
{
    out.clear();
    outPins.clear();
    outTotal = 0;

    if (cid <= 0 || limit <= 0)
    {
        LOGE("MessagePinManager::getPinnedMessages invalid arg, ctype: %d, cid: %lld, limit: %d",
             ctype, (long long)cid, limit);
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessagePinManager::getPinnedMessages db init failed, ctype: %d, cid: %lld, db: %s",
             ctype, (long long)cid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    const std::string where = buildPinnedWhere(ctype, cid, selfUserId);

    //先取不受 limit 约束的真实总数。客户端拿它与返回条数比较判断是否发生截断，
    //进而决定本地对账的时间下界——少了这一步，超过 limit 的老 Pin 会被误清。
    std::ostringstream osCountSql;
    osCountSql << "SELECT COUNT(*) FROM t_chatmsg m"
                  " JOIN t_message_pin p ON m.f_msgid = p.f_msgid WHERE " << where;
    QueryResult* pCount = pConn->query(osCountSql.str().c_str());
    if (pCount == NULL)
    {
        //COUNT(*) 恒有一行结果，取不到说明查询本身出了问题，据实返回失败。
        LOGE("MessagePinManager::getPinnedMessages count failed, ctype: %d, cid: %lld",
             ctype, (long long)cid);
        return false;
    }
    Field* pCountRow = pCount->fetch();
    if (pCountRow != NULL)
    {
        outTotal = static_cast<int64_t>(pCountRow[0].getUInt64());
    }
    pCount->endQuery();
    delete pCount;

    if (outTotal == 0)
    {
        //一条都没钉，不必再跑一次分页查询。
        return true;
    }

    //按消息时间倒序取最近 limit 条。f_msgid 作次级排序键，是为了让"同一毫秒内的
    //几条消息"每次查询的顺序都一样，否则界面上那几行会莫名其妙地互换位置。
    std::ostringstream osSql;
    osSql << "SELECT " << PINNED_MESSAGE_COLUMNS << " FROM t_chatmsg m"
             " JOIN t_message_pin p ON m.f_msgid = p.f_msgid WHERE " << where
          << " ORDER BY m.f_create_time DESC, m.f_msgid DESC LIMIT " << limit;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MessagePinManager::getPinnedMessages query failed, ctype: %d, cid: %lld",
             ctype, (long long)cid);
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
        {
            break;
        }

        MessageRecord rec;
        PinRecord pin;
        fillPinnedRow(pRow, rec, pin);
        out.push_back(rec);
        outPins.push_back(pin);

        if (!pResult->nextRow())
        {
            break;
        }
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessagePinManager::removePinForMessage(int64_t msgid)
{
    if (msgid <= 0)
    {
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessagePinManager::removePinForMessage db init failed, msgid: %lld, db: %s",
             (long long)msgid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "DELETE FROM t_message_pin WHERE f_msgid = " << msgid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessagePinManager::removePinForMessage error, msgid: %lld", (long long)msgid);
        return false;
    }
    return true;
}

bool MessagePinManager::removePinsForGroup(int64_t groupid)
{
    if (groupid <= 0)
    {
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessagePinManager::removePinsForGroup db init failed, groupid: %lld, db: %s",
             (long long)groupid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //本表只有 msgid 一列键，要按群清理就得回到 t_chatmsg 里找出这个群的全部消息。
    std::ostringstream osSql;
    osSql << "DELETE FROM t_message_pin WHERE f_msgid IN ("
             "SELECT f_msgid FROM t_chatmsg WHERE f_ctype = " << conversation_group
          << " AND f_to = " << groupid << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessagePinManager::removePinsForGroup error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}
