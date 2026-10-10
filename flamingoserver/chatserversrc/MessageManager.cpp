/**
 *  消息存储管理器实现，MessageManager.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "MessageManager.h"
#include <memory>
#include <sstream>
#include <vector>
#include <map>
#include <utility>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/SqlDialect.h"   //dbapi::EscapeString（搜索关键字拼进 SQL 前转义）
#include "../dbapi/DbTransaction.h"   //CDbTransaction：addDeletedRange 的删与插须一起成败
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"   //会话类型常量 conversation_single / conversation_group
#include "GroupProtocol.h"   //kInviteHistoryLimitAll：computeGroupHistoryFrom 的「同步全部」取值
#include "MessageSearchText.h"   //BuildStoredSearchText：入库时提取搜索用纯文本

//t_conversation 的 UPSERT 冲突列。必须与该表主键 (f_userid, f_ctype, f_cid) 完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define CONVERSATION_CONFLICT_COLUMNS   "f_userid, f_ctype, f_cid"

//t_file_ack 的 UPSERT 冲突列。同上，必须与该表主键 (f_msgid) 完全一致。
#define FILE_ACK_CONFLICT_COLUMNS       "f_msgid"

MessageManager::MessageManager()
    : m_lastRev(0)
    , m_revLoaded(false)
{
}

MessageManager::~MessageManager()
{
}

bool MessageManager::init()
{
    return true;
}

//t_message_audience 每条 INSERT 最多写入的行数。共享成员一次最多 2000 人
//（FileShareProtocol 的 kShareMaxMembersPerRequest），分 4 批写入，避免拼出过长的 SQL
static const size_t kAudienceInsertBatch = 500;

//取本连接上最近一次 INSERT 生成的自增主键；取不到返回 0。必须与 INSERT 使用同一条连接
static int64_t queryLastInsertId(IDatabase* pConn)
{
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
    return id;
}

//在一个事务里写入一条带可见范围的消息：t_chatmsg 一行（f_audience=1）加 t_message_audience 若干行。
//  pConn：数据库连接（借用，不持有）；audience：可见范围，去重后写入，空列表表示只有发送者本人可见；
//  其余参数含义同 MessageManager::saveMessage。
//  返回：成功返回新消息ID；任一步失败返回 0，事务由 CDbTransaction 的析构回滚
static int64_t saveRestrictedMessage(IDatabase* pConn, const std::string& clientmsgid, int32_t ctype,
                                     int32_t fromUserid, int32_t toUserid, int32_t msgtype,
                                     const std::string& storedContent, int64_t createTime,
                                     const std::vector<int32_t>& audience)
{
    CDbTransaction transaction(pConn);
    if (!transaction.IsActive())
    {
        LOGE("MessageManager::saveMessage begin transaction error, from: %d, to: %d, clientmsgid: %s",
             fromUserid, toUserid, clientmsgid.c_str());
        return 0;
    }

    std::vector<char> escContent(storedContent.size() * 2 + 1, '\0');
    pConn->escapeString(&escContent[0], storedContent.c_str(),
                        static_cast<uint32_t>(storedContent.size()));
    std::vector<char> escClientId(clientmsgid.size() * 2 + 1, '\0');
    pConn->escapeString(&escClientId[0], clientmsgid.c_str(),
                        static_cast<uint32_t>(clientmsgid.size()));
    //搜索用纯文本：同样可能含引号，转义后再拼
    const std::string searchText = chatserver::BuildStoredSearchText(msgtype, storedContent);
    std::vector<char> escSearch(searchText.size() * 2 + 1, '\0');
    pConn->escapeString(&escSearch[0], searchText.c_str(), static_cast<uint32_t>(searchText.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_chatmsg(f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_create_time, "
             "f_audience, f_search_text) VALUES('"
          << &escClientId[0] << "', " << ctype << ", " << fromUserid << ", " << toUserid << ", "
          << msgtype << ", '" << &escContent[0] << "', " << static_cast<long long>(createTime) << ", 1, '"
          << &escSearch[0] << "')";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::saveMessage insert restricted message error, from: %d, to: %d, clientmsgid: %s",
             fromUserid, toUserid, clientmsgid.c_str());
        return 0;
    }

    const int64_t msgId = queryLastInsertId(pConn);
    if (msgId <= 0)
    {
        LOGE("MessageManager::saveMessage last insert id error, from: %d, clientmsgid: %s",
             fromUserid, clientmsgid.c_str());
        return 0;
    }

    //去重：同一人重复出现会撞上 t_message_audience 的主键 (f_msgid, f_user_id)
    std::set<int32_t> uniq(audience.begin(), audience.end());
    std::vector<int32_t> users(uniq.begin(), uniq.end());
    for (size_t begin = 0; begin < users.size(); begin += kAudienceInsertBatch)
    {
        const size_t end = (begin + kAudienceInsertBatch < users.size())
                           ? (begin + kAudienceInsertBatch) : users.size();
        std::ostringstream osAudience;
        osAudience << "INSERT INTO t_message_audience(f_msgid, f_user_id) VALUES";
        for (size_t i = begin; i < end; ++i)
        {
            if (i > begin)
                osAudience << ",";
            osAudience << "(" << msgId << ", " << users[i] << ")";
        }
        if (!pConn->execute(osAudience.str().c_str()))
        {
            LOGE("MessageManager::saveMessage insert audience error, msgid: %lld, batch begin: %u",
                 (long long)msgId, static_cast<unsigned>(begin));
            return 0;
        }
    }

    //提交失败时守卫仍持有该事务，由它的析构补一次回滚
    if (!transaction.Commit())
    {
        LOGE("MessageManager::saveMessage commit error, msgid: %lld", (long long)msgId);
        return 0;
    }

    LOGI("save restricted message ok, msgid: %lld, from: %d, ctype: %d, to: %d, msgtype: %d, audience: %u",
         (long long)msgId, fromUserid, ctype, toUserid, msgtype, static_cast<unsigned>(users.size()));
    return msgId;
}

int64_t MessageManager::saveMessage(const std::string& clientmsgid, int32_t ctype,
                                    int32_t fromUserid, int32_t toUserid, int32_t msgtype,
                                    const std::string& storedContent, int64_t createTime,
                                    const std::vector<int32_t>* audience)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::saveMessage db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //带可见范围的消息要与范围表一起成败：只写进消息、没写进范围时，范围内的人会看不到它
    if (audience != NULL)
        return saveRestrictedMessage(pConn.get(), clientmsgid, ctype, fromUserid, toUserid, msgtype,
                                     storedContent, createTime, *audience);

    //f_content 是 JSON 文本，含双引号、可能含单引号，必须经 escapeString
    //转义后才能拼进 SQL；转义后最坏长度为原串两倍，故缓冲区取 2n+1
    std::vector<char> escContent(storedContent.size() * 2 + 1, '\0');
    pConn->escapeString(&escContent[0], storedContent.c_str(),
                        static_cast<uint32_t>(storedContent.size()));
    std::vector<char> escClientId(clientmsgid.size() * 2 + 1, '\0');
    pConn->escapeString(&escClientId[0], clientmsgid.c_str(),
                        static_cast<uint32_t>(clientmsgid.size()));
    //搜索用纯文本：同样可能含引号，转义后再拼
    const std::string searchText = chatserver::BuildStoredSearchText(msgtype, storedContent);
    std::vector<char> escSearch(searchText.size() * 2 + 1, '\0');
    pConn->escapeString(&escSearch[0], searchText.c_str(), static_cast<uint32_t>(searchText.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_chatmsg(f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_create_time, "
             "f_search_text) VALUES('"
          << &escClientId[0] << "', " << ctype << ", " << fromUserid << ", " << toUserid << ", "
          << msgtype << ", '" << &escContent[0] << "', " << static_cast<long long>(createTime) << ", '"
          << &escSearch[0] << "')";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::saveMessage insert error, from: %d, to: %d, clientmsgid: %s",
             fromUserid, toUserid, clientmsgid.c_str());
        return 0;
    }

    //取回自增主键 f_msgid 作为消息ID（须与 INSERT 同一连接）
    int64_t msgId = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            msgId = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }

    return msgId;
}

int64_t MessageManager::getMsgIdByClientId(int32_t fromUserid, const std::string& clientmsgid)
{
    if (clientmsgid.empty())
        return 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getMsgIdByClientId db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    std::vector<char> escClientId(clientmsgid.size() * 2 + 1, '\0');
    pConn->escapeString(&escClientId[0], clientmsgid.c_str(),
                        static_cast<uint32_t>(clientmsgid.size()));

    std::ostringstream osSql;
    osSql << "SELECT f_msgid FROM t_chatmsg WHERE f_from = " << fromUserid
          << " AND f_clientmsgid = '" << &escClientId[0] << "' LIMIT 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return 0;

    int64_t msgId = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        msgId = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;

    return msgId;
}

//某用户某会话的可变状态（t_conversation 一行；缺行时全部取默认 0）
struct ConvState
{
    int     pinned;        //是否置顶
    int     notifymode;    //消息提醒模式（t_conversation.f_notify_mode），取值见 enum conversation_notify_mode
    int64_t readmsgid;     //已读位点
    int     deleted;       //是否已从列表删除
    int64_t deletedmsgid;  //删除会话那一刻该会话的最后消息msgid（删除锚点）
    int     unreadmark;    //本人是否手动标为未读（t_conversation.f_unread_mark，docs/会话标为未读方案.md）

    ConvState()
        : pinned(0), notifymode(0), readmsgid(0), deleted(0), deletedmsgid(0), unreadmark(0)
    {
    }
};

//会话被删除后是否仍应对该用户隐藏。
//
//删除会话的语义是"从最近会话列表里拿掉"，而不是"永远别再出现"：拿掉之后对方再发
//消息过来，会话理应重新冒出来。判断办法是拿删除时记下的锚点（当时的最后消息msgid）
//与此刻的最后消息msgid 比：还是那条旧消息（<=锚点）就继续隐藏；来了更新的消息
//（>锚点）就让它重现。
//
//锚点取负数（读取处对 NULL 用 IFNULL(...,-1) 兜底）表示这行是<u>加锚点列之前</u>留下的
//老删除记录：那时没有锚点可比，退化为老行为"一直隐藏"，免得升级后所有历史删除记录集体
//复活。锚点为 0 则是正常值，表示"删除时这个会话一条可见消息都没有"，此后来任何消息
//（msgid 恒 > 0）都应让它重现。
//    st         该会话的状态行
//    lastmsgid  该会话此刻的最后一条消息msgid
static bool isConversationHidden(const ConvState& st, int64_t lastmsgid)
{
    if (st.deleted == 0)
        return false;

    if (st.deletedmsgid < 0)
        return true;

    return lastmsgid <= st.deletedmsgid;
}

//把 t_chatmsg 的一行映射为 MessageRecord。
//调用方的 SELECT 必须按此列序取 10 列：
//f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_recalled, f_create_time,
//f_recalled_by
//前 9 列与 MessagePinManager.cpp 的 PINNED_MESSAGE_COLUMNS 同序；f_recalled_by 放在最后，
//那边的 Pin 查询只取未撤回的消息，用不到撤回者，所以不必跟着加列。
static void fillMessageRecord(Field* pRow, MessageRecord& rec)
{
    rec.msgid       = static_cast<int64_t>(pRow[0].getUInt64());
    rec.clientmsgid = pRow[1].getCppString();
    rec.ctype       = pRow[2].getInt32();
    rec.from        = pRow[3].getInt32();
    rec.to          = pRow[4].getInt32();
    rec.msgtype     = pRow[5].getInt32();
    rec.recalled    = (pRow[7].getInt32() != 0);
    rec.timestamp   = static_cast<int64_t>(pRow[8].getUInt64());
    rec.recalledby  = static_cast<int64_t>(pRow[9].getUInt64());
    //从 f_content 解出 content/mentions/mentionall/quote
    std::string blob = pRow[6].getCppString();
    MessageProtocol::parseStoredContent(blob, rec.content, rec.mentions,
                                        rec.mentionall, rec.quote);
}

//生成"该用户能看到的消息"的 SQL 片段（拼到 WHERE 条件之后，自带前置空格）。
//由两部分组成，同时生效。
//其一，排除本人已删除的消息。已删除有三种来源：
//  1. t_message_deleted：cmd_delete_message 逐条删除的；
//  2. t_message_deleted_range：cmd_delete_messages_by_time 按时间范围清理的，只记区间，
//     按消息时间落在任一区间内过滤（见 MessageDeleteRange.h）；
//  3. t_conversation.f_cleared_msgid：cmd_clear_conversation 按会话清空的，只记锚点，
//     msgid 不大于本人对该会话的锚点即过滤（见 MessageDeleteRange.h 的 BuildNotClearedClause）。
//其二，排除不在本人可见范围内的消息：f_audience=0 的消息会话内所有人可见；f_audience=1 的消息
//只有发送者本人与 t_message_audience 里登记的人可见（文件共享卡片只给被指定的群成员看，
//见 docs/文件共享方案.md 6.1）。f_audience 列为 NOT NULL DEFAULT 0，不需要对 NULL 兜底。
//本文件各查询的 FROM 子句都直接写 t_chatmsg、不起别名，所以列一律带 t_chatmsg. 前缀；时间列
//必须带表名，否则在区间过滤的相关子查询里会被解析成子查询自己的列。
static std::string visibleToUserClause(int32_t userid)
{
    std::ostringstream os;
    os << " AND f_msgid NOT IN (SELECT f_msgid FROM t_message_deleted WHERE f_userid = "
       << userid << ")"
       << msgdelete::BuildNotInDeletedRangeClause(userid, "t_chatmsg.f_create_time")
       << msgdelete::BuildNotClearedClause(userid, "t_chatmsg.")
       << " AND (t_chatmsg.f_audience = 0 OR t_chatmsg.f_from = " << userid
       << " OR t_chatmsg.f_msgid IN (SELECT f_msgid FROM t_message_audience WHERE f_user_id = "
       << userid << "))";
    return os.str();
}

//算群会话未读时，已读位点之后最多检查多少条系统消息。新成员的入群提示就是入群后的第一条系统消息
//（入群时已读位点初始化到入群前的最后一条，见 initNewMembersReadPos），按 msgid 升序取前几条足以找到它；
//设上限是为了让积压了大量系统提示的群也只多读这几行
static const int kMaxJoinTipScan = 20;

/**
 *  数某人在某群已读位点之后、点了他名字的「有人入群」提示有几条（会话未读里系统消息的唯一例外，
 *  判断口径见 GroupProtocol::isMemberJoinTipFor）。
 *    pConn：     数据库连接，调用方持有。
 *    userid：    本人。
 *    groupid：   群 id。
 *    readmsgid： 本人对该群的已读位点。
 *    maxCount：  最多数到几条（调用方传「未读上限减去已有的未读数」）；不大于 0 时直接返回 0。
 *  @return 条数；查询失败时为 0
 */
static int countJoinTipsForUser(IDatabase* pConn, int32_t userid, int64_t groupid, int64_t readmsgid,
                                int maxCount)
{
    if (pConn == NULL || maxCount <= 0)
        return 0;
    std::ostringstream osSql;
    osSql << "SELECT f_content FROM t_chatmsg WHERE f_ctype = " << conversation_group
          << " AND f_to = " << groupid << " AND f_msgtype = " << message_type_system
          << " AND f_msgid > " << readmsgid << visibleToUserClause(userid)
          << " ORDER BY f_msgid ASC LIMIT " << kMaxJoinTipScan;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return 0;
    int count = 0;
    while (count < maxCount)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        if (GroupProtocol::isMemberJoinTipFor(pRow[0].getCppString(), userid))
            ++count;
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

//静态常量的定义。类内已经给了初值，这里再给一份定义是为了让它可以被取地址 / 绑引用 ——
//doctest 的 CHECK 宏会把两边表达式按引用捕获，只有类内初值时 g++ 会在链接期报
//undefined reference（MSVC 恰好能过，故这类问题只在 Linux 全量构建时暴露）。
const int MessageManager::kMaxUnreadCount;
const int MessageManager::kDetailBatchSize;

void MessageManager::fetchLastMsgDetails(IDatabase* pConn,
                                         const std::vector<int64_t>& msgids,
                                         std::map<int64_t, LastMsgDetail>& out)
{
    out.clear();
    if (pConn == NULL || msgids.empty())
    {
        return;
    }

    //先去重：同一个 msgid 被要两次没有意义，还会把 IN 列表撑长。
    std::vector<int64_t> uniq;
    {
        std::map<int64_t, bool> seen;
        for (size_t i = 0; i < msgids.size(); ++i)
        {
            if (msgids[i] <= 0)
                continue;
            if (seen.find(msgids[i]) != seen.end())
                continue;
            seen[msgids[i]] = true;
            uniq.push_back(msgids[i]);
        }
    }

    //分批拼 IN 列表。一条 SQL 的长度有上限，会话极多时不分批会拼出超长语句被拒。
    for (size_t begin = 0; begin < uniq.size(); begin += kDetailBatchSize)
    {
        const size_t end = (begin + kDetailBatchSize < uniq.size())
                           ? (begin + kDetailBatchSize) : uniq.size();

        std::ostringstream osSql;
        osSql << "SELECT f_msgid, f_from, f_msgtype, f_content, f_create_time, f_recalled, "
                 "f_recalled_by "
                 "FROM t_chatmsg WHERE f_msgid IN (";
        for (size_t i = begin; i < end; ++i)
        {
            if (i > begin)
                osSql << ",";
            osSql << uniq[i];
        }
        osSql << ")";

        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult == NULL)
        {
            continue;
        }
        while (true)
        {
            Field* pRow = pResult->fetch();
            if (pRow == NULL)
                break;

            const int64_t msgid = static_cast<int64_t>(pRow[0].getUInt64());
            LastMsgDetail detail;
            detail.from    = pRow[1].getInt32();
            detail.msgtype = pRow[2].getInt32();

            //f_content 是打包过的（content/mentions/mentionall/quote），
            //会话摘要只要 content 那一层，其余解出来即丢。
            const std::string blob = pRow[3].getCppString();
            std::vector<int64_t> ignoreMentions;
            bool ignoreMentionAll = false;
            std::string ignoreQuote;
            MessageProtocol::parseStoredContent(blob, detail.content,
                                                ignoreMentions, ignoreMentionAll, ignoreQuote);

            detail.timestamp = static_cast<int64_t>(pRow[4].getUInt64());
            //已撤回的最后一条：会话摘要改为撤回提示，不再下发原文（2026-10-03 起）
            detail.recalled  = (pRow[5].getInt32() != 0);
            //撤回者随摘要下发，客户端据此写「某某撤回了一条消息」（2026-10-03 起）
            detail.recalledby = static_cast<int64_t>(pRow[6].getUInt64());
            out[msgid] = detail;

            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }
}

bool MessageManager::getConversationBriefs(int32_t userid, std::vector<ConversationBrief>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getConversationBriefs db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //1) 先取该用户的全部会话状态行（置顶/免打扰/已读位点/删除标记）
    std::map<int64_t, ConvState> stateMap;
    {
        std::ostringstream osSql;
        //f_deleted_msgid 定义为 NOT NULL DEFAULT 0，不会为 NULL，下面 IFNULL 兜底成 -1 的分支实际走不到；
        //-1 原本用来把"没有锚点"与"锚点确实是 0"区分开（两者行为不同，见 isConversationHidden），
        //日后清理时可去掉 IFNULL（见 MysqlManager.cpp 里该列的说明）。
        osSql << "SELECT f_cid, f_pinned, f_notify_mode, f_readmsgid, f_deleted, "
                 "IFNULL(f_deleted_msgid, -1), f_unread_mark FROM t_conversation "
                 "WHERE f_userid = " << userid << " AND f_ctype = 1";
        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult != NULL)
        {
            while (true)
            {
                Field* pRow = pResult->fetch();
                if (pRow == NULL)
                    break;

                int64_t cid = static_cast<int64_t>(pRow[0].getUInt64());
                ConvState st;
                st.pinned       = pRow[1].getInt32();
                st.notifymode   = pRow[2].getInt32();
                st.readmsgid    = static_cast<int64_t>(pRow[3].getUInt64());
                st.deleted      = pRow[4].getInt32();
                st.deletedmsgid = static_cast<int64_t>(pRow[5].getUInt64());
                st.unreadmark   = pRow[6].getInt32();
                stateMap[cid] = st;

                if (!pResult->nextRow())
                    break;
            }
            pResult->endQuery();
            delete pResult;
        }
    }

    //2) 从 t_chatmsg 派生该用户的全部单聊会话：对端 + 该会话最后一条消息msgid
    std::vector<std::pair<int64_t, int64_t> > peers;  //(对端userid, 最后一条msgid)
    {
        std::ostringstream osSql;
        osSql << "SELECT peer, MAX(f_msgid) FROM ("
                 "SELECT (CASE WHEN f_from = " << userid << " THEN f_to ELSE f_from END) AS peer, "
                 "f_msgid FROM t_chatmsg WHERE f_ctype = 1 AND (f_from = " << userid
              << " OR f_to = " << userid << ")" << visibleToUserClause(userid)
              << ") AS t GROUP BY peer";
        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult == NULL)
            return false;

        while (true)
        {
            Field* pRow = pResult->fetch();
            if (pRow == NULL)
                break;

            int64_t peer = static_cast<int64_t>(pRow[0].getUInt64());
            int64_t last = static_cast<int64_t>(pRow[1].getUInt64());
            peers.push_back(std::make_pair(peer, last));

            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }

    //3) 一次批量取回所有会话"最后一条消息"的详情。原先是每个会话一条 SQL，
    //   会话一多就线性变慢。这里先把 msgid 收齐，下面的循环直接查表拼装。
    //   未读数仍是逐会话一条 —— 理由同群聊侧（见 getGroupConversationBriefs 的说明）。
    std::map<int64_t, LastMsgDetail> detailMap;
    {
        std::vector<int64_t> wanted;
        for (size_t i = 0; i < peers.size(); ++i)
        {
            wanted.push_back(peers[i].second);
        }
        fetchLastMsgDetails(pConn.get(), wanted, detailMap);
    }

    //4) 逐个会话拼出摘要
    for (size_t i = 0; i < peers.size(); ++i)
    {
        int64_t peer      = peers[i].first;
        int64_t lastmsgid = peers[i].second;

        //会话状态：无 t_conversation 行则全部取默认
        ConvState st;
        std::map<int64_t, ConvState>::iterator it = stateMap.find(peer);
        if (it != stateMap.end())
            st = it->second;

        //已删除的会话：只有在"删除之后没有更新的消息"时才继续隐藏；来了新消息就重现
        if (isConversationHidden(st, lastmsgid))
            continue;

        ConversationBrief brief;
        brief.ctype     = 1;
        brief.cid       = peer;
        brief.pinned    = (st.pinned != 0);
        brief.notifymode = st.notifymode;
        brief.unreadmark = (st.unreadmark != 0);
        brief.lastmsgid = lastmsgid;

        //lastmsg 详情：上面已经整批取回，这里直接查表。查不到就保持默认值 ——
        //消息刚好在这两步之间被删掉才会出现，摘要少一行预览，不该整条会话丢掉。
        {
            std::map<int64_t, LastMsgDetail>::iterator itDetail = detailMap.find(lastmsgid);
            if (itDetail != detailMap.end())
            {
                brief.lastfrom      = itDetail->second.from;
                brief.lastmsgtype   = itDetail->second.msgtype;
                brief.lastcontent   = itDetail->second.content;
                brief.lasttimestamp = itDetail->second.timestamp;
                brief.lastrecalled  = itDetail->second.recalled;
                brief.lastrecalledby = itDetail->second.recalledby;
            }
        }

        //未读数：来自对端、msgid 大于已读位点的消息条数。
        //数到 kMaxUnreadCount 就停 —— 内层子查询带 LIMIT，外层只数子查询吐出来的行，
        //于是无论积压多少条，这条查询的工作量都是固定的。理由见 MessageManager.h 上
        //kMaxUnreadCount 的说明。
        {
            std::ostringstream osSql;
            osSql << "SELECT COUNT(*) FROM (SELECT 1 FROM t_chatmsg"
                     " WHERE f_ctype = 1 AND f_from = " << peer
                  << " AND f_to = " << userid << " AND f_msgid > " << st.readmsgid
                  << visibleToUserClause(userid)
                  << " LIMIT " << kMaxUnreadCount << ") t";
            QueryResult* pResult = pConn->query(osSql.str().c_str());
            if (pResult != NULL)
            {
                Field* pRow = pResult->fetch();
                if (pRow != NULL)
                    brief.unread = pRow[0].getInt32();
                pResult->endQuery();
                delete pResult;
            }
        }

        out.push_back(brief);
    }

    return true;
}

bool MessageManager::getConversationMessages(int32_t userid, const SyncMessagesRequest& req,
                                             std::vector<MessageRecord>& out, bool& hasMore,
                                             int64_t minCreateTime, int64_t minMsgId)
{
    hasMore = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getConversationMessages db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
             "f_recalled, f_create_time, f_recalled_by FROM t_chatmsg WHERE ";
    if (req.ctype == conversation_group)
    {
        //群聊：本群（f_to=群id）的全部消息
        osSql << "f_ctype = " << conversation_group << " AND f_to = " << req.cid;
    }
    else
    {
        //单聊：本人与对端（req.cid）的双向消息
        int32_t peerid = static_cast<int32_t>(req.cid);
        osSql << "f_ctype = " << conversation_single
              << " AND ((f_from = " << userid << " AND f_to = " << peerid << ") OR "
                 "(f_from = " << peerid << " AND f_to = " << userid << "))";
    }
    osSql << visibleToUserClause(userid);

    //时间下界：群设为"新成员不可查看入群前历史消息"时，调用方把该成员的入群
    //时间传进来，早于它的消息一律不返回。0 表示不限（单聊、以及允许看历史的群）。
    if (minCreateTime > 0)
        osSql << " AND f_create_time >= " << minCreateTime;
    //msgid 下界：邀请入群时选择了「同步最近 N 条」，本人只能看到这一条及之后的消息
    if (minMsgId > 0)
        osSql << " AND f_msgid >= " << minMsgId;

    if (req.isAfterMode)
    {
        //补齐模式：取锚点之后的消息，按 msgid 升序
        osSql << " AND f_msgid > " << req.aftermsgid << " ORDER BY f_msgid ASC";
    }
    else
    {
        //翻历史模式：取锚点之前的消息（beforemsgid=0 表示从最新开始），按 msgid 降序
        if (req.beforemsgid > 0)
            osSql << " AND f_msgid < " << req.beforemsgid;
        osSql << " ORDER BY f_msgid DESC";
    }
    //多取 1 行以判断锚点方向上是否还有更多
    osSql << " LIMIT " << (req.limit + 1);

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    std::vector<MessageRecord> rows;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        MessageRecord rec;
        fillMessageRecord(pRow, rec);
        rows.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;

    //行数超过 limit 说明锚点方向上还有更多；丢弃多取的那 1 行
    if (req.limit > 0 && static_cast<int>(rows.size()) > req.limit)
    {
        hasMore = true;
        rows.resize(req.limit);
    }

    //统一以 msgid 升序（时间正序）返回；翻历史模式查的是降序，需反转
    if (req.isAfterMode)
        out = rows;
    else
        out.assign(rows.rbegin(), rows.rend());

    return true;
}

bool MessageManager::markRead(int32_t userid, int32_t ctype, int64_t cid,
                              int64_t readmsgid, int64_t updateTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::markRead db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //UPSERT：t_conversation 行可能尚不存在；已读位点只增不减，故取 GREATEST。
    //赋值右侧的裸列名 f_readmsgid 在两种后端里都指代「已存在的那一行」的值，
    //所以 GREATEST(f_readmsgid, N) 这段文本无需按方言区分，只有冲突子句要
    std::ostringstream osSql;
    osSql << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_readmsgid, f_update_time) VALUES("
          << userid << ", " << ctype << ", " << cid << ", " << readmsgid << ", " << updateTime
          << ")" << dbapi::UpsertClause(CONVERSATION_CONFLICT_COLUMNS)
          << "f_readmsgid = GREATEST(f_readmsgid, " << readmsgid
          << "), f_unread_mark = 0, f_update_time = " << updateTime;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::markRead error, userid: %d, cid: %lld, readmsgid: %lld",
             userid, (long long)cid, (long long)readmsgid);
        return false;
    }
    return true;
}

bool MessageManager::initNewMembersReadPos(int64_t groupid, const std::vector<int64_t>& userids,
                                           int64_t updateTime)
{
    if (userids.empty())
        return true;

    int64_t lastMsgId = 0;
    {
        std::unique_ptr<IDatabase> pConn;
        pConn.reset(DbFactory::createConnection());
        if (!pConn)
        {
            LOGE("MessageManager::initNewMembersReadPos db init failed, db: %s",
                 Singleton<CDbConfig>::Instance().describe().c_str());
            return false;
        }

        //本群全部消息里最大的 msgid。不按某个人的删除记录过滤：新成员此前不在群里，不会有删除记录
        std::ostringstream osSql;
        osSql << "SELECT MAX(f_msgid) FROM t_chatmsg WHERE f_ctype = " << conversation_group
              << " AND f_to = " << groupid;
        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult == NULL)
        {
            LOGE("MessageManager::initNewMembersReadPos query max msgid error, groupid: %lld", (long long)groupid);
            return false;
        }
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            lastMsgId = static_cast<int64_t>(pRow[0].getUInt64());   //群里没有消息时 MAX 为 NULL，取到 0
        pResult->endQuery();
        delete pResult;
    }

    //群里还没有消息：没有可以算成未读的旧消息，不必写会话行
    if (lastMsgId <= 0)
        return true;

    bool allOk = true;
    for (size_t i = 0; i < userids.size(); ++i)
    {
        if (!markRead(static_cast<int32_t>(userids[i]), conversation_group, groupid, lastMsgId, updateTime))
            allOk = false;
    }
    LOGI("init new members read position, groupid: %lld, members: %u, readmsgid: %lld, ok: %d",
         (long long)groupid, static_cast<unsigned>(userids.size()), (long long)lastMsgId, allOk ? 1 : 0);
    return allOk;
}

bool MessageManager::computeGroupHistoryFrom(int64_t groupid, int historyLimit, int64_t& out)
{
    out = 0;
    //同步全部（以及任何小于 0 的取值）：不设下界，不必查库
    if (historyLimit <= kInviteHistoryLimitAll)
        return true;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::computeGroupHistoryFrom db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //不同步：取本群当前最大的 msgid，下界为它加 1；同步最近 N 条：按 msgid 降序跳过前 N - 1 条，取第 N 条。
    //两种写法都只扫本群按 msgid 排好序的前几行
    std::ostringstream osSql;
    osSql << "SELECT f_msgid FROM t_chatmsg WHERE f_ctype = " << conversation_group << " AND f_to = " << groupid
          << " ORDER BY f_msgid DESC LIMIT 1";
    if (historyLimit > 0)
        osSql << " OFFSET " << (historyLimit - 1);
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MessageManager::computeGroupHistoryFrom query failed, groupid: %lld, limit: %d",
             (long long)groupid, historyLimit);
        return false;
    }

    int64_t msgid = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        msgid = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;

    //查不到行：本群没有消息，或消息不足 N 条，两种情况都不设下界
    if (msgid > 0)
        out = (historyLimit == 0) ? (msgid + 1) : msgid;

    LOGI("compute group history from, groupid: %lld, limit: %d, historyfrom: %lld",
         (long long)groupid, historyLimit, (long long)out);
    return true;
}

bool MessageManager::clearConversation(int32_t userid, int32_t ctype, int64_t cid,
                                       int64_t updateTime, int64_t& outAnchor)
{
    outAnchor = 0;
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::clearConversation db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //1) 该会话此刻的最大 msgid 作为锚点。不套 visibleToUserClause：锚点只关心"清空这一刻会话里
    //   最新的是哪条"，已被逐条删除或按时间清理的消息照样参与，免得锚点偏小
    std::ostringstream osMax;
    osMax << "SELECT MAX(f_msgid) FROM t_chatmsg WHERE f_ctype = " << ctype;
    if (ctype == conversation_group)
    {
        osMax << " AND f_to = " << cid;
    }
    else
    {
        osMax << " AND ((f_from = " << userid << " AND f_to = " << cid << ")"
              << " OR (f_from = " << cid << " AND f_to = " << userid << "))";
    }
    int64_t maxMsgid = 0;
    QueryResult* pMax = pConn->query(osMax.str().c_str());
    if (pMax != NULL)
    {
        Field* pRow = pMax->fetch();
        if (pRow != NULL)
            maxMsgid = static_cast<int64_t>(pRow[0].getUInt64());
        pMax->endQuery();
        delete pMax;
    }

    //2) UPSERT 本人这一行：锚点只增不减（GREATEST），置顶 / 免打扰 / 已读位点等其余列不动
    std::ostringstream osSql;
    osSql << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_cleared_msgid, f_update_time) VALUES("
          << userid << ", " << ctype << ", " << cid << ", " << maxMsgid << ", " << updateTime
          << ")" << dbapi::UpsertClause(CONVERSATION_CONFLICT_COLUMNS)
          << "f_cleared_msgid = GREATEST(f_cleared_msgid, " << maxMsgid
          << "), f_update_time = " << updateTime;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::clearConversation upsert error, userid: %d, ctype: %d, cid: %lld",
             userid, ctype, (long long)cid);
        return false;
    }

    //3) 读回生效的锚点（之前清空得更靠后时保持原值）
    std::ostringstream osRead;
    osRead << "SELECT f_cleared_msgid FROM t_conversation WHERE f_userid = " << userid
           << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    QueryResult* pRead = pConn->query(osRead.str().c_str());
    if (pRead == NULL)
    {
        LOGE("MessageManager::clearConversation read back error, userid: %d, cid: %lld",
             userid, (long long)cid);
        return false;
    }
    Field* pRow = pRead->fetch();
    if (pRow != NULL)
        outAnchor = static_cast<int64_t>(pRow[0].getUInt64());
    pRead->endQuery();
    delete pRead;

    LOGI("MessageManager::clearConversation ok, userid: %d, ctype: %d, cid: %lld, anchor: %lld",
         userid, ctype, (long long)cid, (long long)outAnchor);
    return true;
}

bool MessageManager::getReadMsgid(int32_t userid, int32_t ctype, int64_t cid, int64_t& out)
{
    out = 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getReadMsgid db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //(f_userid, f_ctype, f_cid) 是 t_conversation 的联合主键，最多一行。
    //没有这一行（从没上报过已读）时 query 返回空结果，位点按 0 处理。
    std::ostringstream osSql;
    osSql << "SELECT f_readmsgid FROM t_conversation WHERE f_userid = " << userid
          << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            out = static_cast<int64_t>(pRow[0].getUInt64());
        }
        pResult->endQuery();
        delete pResult;
    }
    return true;
}

bool MessageManager::getMessageById(int64_t msgid, MessageRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getMessageById db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
             "f_recalled, f_create_time, f_recalled_by FROM t_chatmsg WHERE f_msgid = " << msgid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillMessageRecord(pRow, out);
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool MessageManager::saveFileAckState(int64_t msgid, int state, int64_t updateTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::saveFileAckState db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //UPSERT：同一条文件消息会被上报多次（开始接收 → 已接收；拒绝后反悔再接收），
    //一律以最后一次上报为准，故冲突时直接覆盖，不做状态机比较。
    std::ostringstream osSql;
    osSql << "INSERT INTO t_file_ack(f_msgid, f_state, f_update_time) VALUES("
          << msgid << ", " << state << ", " << updateTime
          << ")" << dbapi::UpsertClause(FILE_ACK_CONFLICT_COLUMNS)
          << "f_state = " << state << ", f_update_time = " << updateTime;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::saveFileAckState error, msgid: %lld, state: %d",
             (long long)msgid, state);
        return false;
    }
    return true;
}

int MessageManager::getFileAckState(int64_t msgid)
{
    if (msgid <= 0)
        return 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getFileAckState db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_state FROM t_file_ack WHERE f_msgid = " << msgid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return 0;   //无记录：接收方还没有任何动作

    int state = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        state = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return state;
}

int64_t MessageManager::allocateRevisionLocked(IDatabase* pConn)
{
    if (!m_revLoaded)
    {
        //空表时 MAX 为 NULL，用 COALESCE 换成 0；两种后端都支持这个写法
        QueryResult* pResult = pConn->query("SELECT COALESCE(MAX(f_rev), 0) FROM t_chatmsg");
        if (pResult == NULL)
        {
            LOGE("MessageManager::allocateRevisionLocked failed to read MAX(f_rev)");
            return 0;
        }
        Field* pRow = pResult->fetch();
        m_lastRev = (pRow != NULL) ? static_cast<int64_t>(pRow[0].getUInt64()) : 0;
        pResult->endQuery();
        delete pResult;
        m_revLoaded = true;
        LOGI("MessageManager: rewrite revision starts after %lld", (long long)m_lastRev);
    }
    ++m_lastRev;
    return m_lastRev;
}

bool MessageManager::recallMessage(int64_t msgid, int64_t operatorId)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::recallMessage db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //取号与写库在同一把锁里，保证序号小的先落库（见 allocateRevisionLocked 的说明）
    std::lock_guard<std::mutex> revLock(m_revMutex);
    const int64_t rev = allocateRevisionLocked(pConn.get());
    if (rev <= 0)
        return false;
    std::ostringstream osSql;
    osSql << "UPDATE t_chatmsg SET f_recalled = 1, f_recalled_by = " << operatorId
          << ", f_rev = " << rev << " WHERE f_msgid = " << msgid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::recallMessage error, msgid: %lld, operator: %lld",
             (long long)msgid, (long long)operatorId);
        return false;
    }
    LOGI("MessageManager::recallMessage msgid: %lld, operator: %lld, rev: %lld",
         (long long)msgid, (long long)operatorId, (long long)rev);
    return true;
}

bool MessageManager::updateMessageContent(int64_t msgid, const std::string& storedContent)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::updateMessageContent db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //搜索文本按消息类型提取，先取出这条消息的类型；msgid 不存在时什么也不改
    int32_t msgtype = 0;
    {
        std::ostringstream osType;
        osType << "SELECT f_msgtype FROM t_chatmsg WHERE f_msgid = " << msgid;
        QueryResult* pResult = pConn->query(osType.str().c_str());
        if (pResult != NULL)
        {
            Field* pRow = pResult->fetch();
            if (pRow != NULL)
                msgtype = pRow[0].getInt32();
            pResult->endQuery();
            delete pResult;
        }
    }

    //f_content 是 JSON 文本，含双引号、可能含单引号，必须经 escapeString 转义后才能拼进 SQL；
    //转义后最坏长度为原串两倍，故缓冲区取 2n+1
    std::vector<char> escContent(storedContent.size() * 2 + 1, '\0');
    pConn->escapeString(&escContent[0], storedContent.c_str(), static_cast<uint32_t>(storedContent.size()));
    const std::string searchText = chatserver::BuildStoredSearchText(msgtype, storedContent);
    std::vector<char> escSearch(searchText.size() * 2 + 1, '\0');
    pConn->escapeString(&escSearch[0], searchText.c_str(), static_cast<uint32_t>(searchText.size()));

    //取号与写库在同一把锁里，保证序号小的先落库（见 allocateRevisionLocked 的说明）
    std::lock_guard<std::mutex> revLock(m_revMutex);
    const int64_t rev = allocateRevisionLocked(pConn.get());
    if (rev <= 0)
        return false;
    std::ostringstream osSql;
    osSql << "UPDATE t_chatmsg SET f_content = '" << &escContent[0] << "', f_search_text = '"
          << &escSearch[0] << "', f_rev = " << rev << " WHERE f_msgid = " << msgid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::updateMessageContent error, msgid: %lld", (long long)msgid);
        return false;
    }
    LOGI("MessageManager::updateMessageContent msgid: %lld, msgtype: %d, rev: %lld",
         (long long)msgid, msgtype, (long long)rev);
    return true;
}

//查某用户某会话此刻的最后一条消息msgid（排除该用户本端已删除的消息，与
//getConversationBriefs 里算 lastmsgid 的口径完全一致——两者要拿来相互比较）。
//会话尚无任何消息返回 0。
//    pConn   已初始化的数据库连接
//    userid  会话归属用户
//    ctype   会话类型（conversation_single / conversation_group）
//    cid     会话ID（单聊为对端userid，群聊为群id）
static int64_t queryConversationLastMsgId(IDatabase* pConn, int32_t userid,
                                          int32_t ctype, int64_t cid)
{
    std::ostringstream osSql;
    osSql << "SELECT MAX(f_msgid) FROM t_chatmsg WHERE ";
    if (ctype == conversation_group)
    {
        //群聊：本群（f_to=群id）的全部消息
        osSql << "f_ctype = " << conversation_group << " AND f_to = " << cid;
    }
    else
    {
        //单聊：本人与对端的双向消息
        int32_t peerid = static_cast<int32_t>(cid);
        osSql << "f_ctype = " << conversation_single
              << " AND ((f_from = " << userid << " AND f_to = " << peerid << ") OR "
                 "(f_from = " << peerid << " AND f_to = " << userid << "))";
    }
    osSql << visibleToUserClause(userid);

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return 0;

    int64_t lastmsgid = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        lastmsgid = static_cast<int64_t>(pRow[0].getUInt64());   //无消息时 MAX 为 NULL，取到 0
    pResult->endQuery();
    delete pResult;
    return lastmsgid;
}

//数某用户处于某种提醒模式的会话个数（单聊与群聊合计）。查询失败返回 -1。
//    pConn   已初始化的数据库连接
//    userid  会话归属用户
//    mode    提醒模式，取值见 enum conversation_notify_mode
static int queryNotifyModeCount(IDatabase* pConn, int32_t userid, int mode)
{
    std::ostringstream osSql;
    osSql << "SELECT COUNT(*) FROM t_conversation WHERE f_userid = " << userid
          << " AND f_notify_mode = " << mode;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return -1;

    int count = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

//取某会话当前的提醒模式；没有状态行时为 notify_mode_allow。查询失败返回 false。
//    pConn   已初始化的数据库连接
//    userid  会话归属用户
//    ctype   会话类型
//    cid     会话ID（单聊为对端userid，群聊为群id）
//    mode    [出参] 当前模式
static bool queryNotifyMode(IDatabase* pConn, int32_t userid, int32_t ctype, int64_t cid, int& mode)
{
    mode = notify_mode_allow;

    std::ostringstream osSql;
    osSql << "SELECT f_notify_mode FROM t_conversation WHERE f_userid = " << userid
          << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        mode = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return true;
}

//在调用方已开启的事务里，把一个会话的提醒模式写成 mode。setConversation 与 setNotifyModes 共用，
//两条路径的规则因此完全一致：
//  1. 改成特别关注时校验上限，本来就是特别关注的重复设置不占新名额；
//  2. 从屏蔽改成别的模式时，把已读位点推进到该会话当前最后一条可见消息 —— 屏蔽期间不计未读，
//     解除后这些消息也不该一下子变成一大堆未读（docs/消息提醒模式与消息盒子方案.md §3.2）。
//    pConn        已开启事务的数据库连接
//    userid       会话归属用户
//    ctype        会话类型
//    cid          会话ID
//    mode         目标模式，取值已由调用方校验
//    updateTime   本次更新时间，Unix毫秒
//    starredCount [入参 / 出参] 本人当前特别关注的会话数，按本次写入的结果增减
//返回结果码：error_code_ok、error_code_starred_limit（已满，未写入）、error_code_server_internal
static int applyNotifyMode(IDatabase* pConn, int32_t userid, int32_t ctype, int64_t cid,
                           int mode, int64_t updateTime, int& starredCount)
{
    int oldMode = notify_mode_allow;
    if (!queryNotifyMode(pConn, userid, ctype, cid, oldMode))
    {
        LOGE("MessageManager query notify mode failed, userid: %d, ctype: %d, cid: %lld",
             userid, ctype, (long long)cid);
        return error_code_server_internal;
    }

    const bool becomeStarred = (mode == notify_mode_starred && oldMode != notify_mode_starred);
    const bool leaveStarred  = (oldMode == notify_mode_starred && mode != notify_mode_starred);
    if (becomeStarred && starredCount >= NOTIFY_MODE_STARRED_MAX)
    {
        LOGI("set notify mode rejected, starred limit reached, userid: %d, ctype: %d, cid: %lld, starred: %d",
             userid, ctype, (long long)cid, starredCount);
        return error_code_starred_limit;
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_notify_mode, f_update_time) VALUES("
          << userid << ", " << ctype << ", " << cid << ", " << mode << ", " << updateTime
          << ")" << dbapi::UpsertClause(CONVERSATION_CONFLICT_COLUMNS)
          << "f_notify_mode = " << mode << ", f_update_time = " << updateTime;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager write notify mode failed, userid: %d, ctype: %d, cid: %lld, mode: %d",
             userid, ctype, (long long)cid, mode);
        return error_code_server_internal;
    }

    if (becomeStarred)
        ++starredCount;
    else if (leaveStarred)
        --starredCount;

    //从屏蔽改出：已读位点推进到当前最后一条可见消息（只前进不后退）
    int64_t readAdvancedTo = 0;
    if (oldMode == notify_mode_block && mode != notify_mode_block)
    {
        readAdvancedTo = queryConversationLastMsgId(pConn, userid, ctype, cid);
        if (readAdvancedTo > 0)
        {
            std::ostringstream osRead;
            osRead << "UPDATE t_conversation SET f_readmsgid = GREATEST(f_readmsgid, " << readAdvancedTo
                   << ") WHERE f_userid = " << userid << " AND f_ctype = " << ctype
                   << " AND f_cid = " << cid;
            if (!pConn->execute(osRead.str().c_str()))
            {
                LOGE("MessageManager advance read position failed, userid: %d, ctype: %d, cid: %lld",
                     userid, ctype, (long long)cid);
                return error_code_server_internal;
            }
        }
    }

    LOGI("notify mode set, userid: %d, ctype: %d, cid: %lld, mode: %d -> %d, read advanced to: %lld",
         userid, ctype, (long long)cid, oldMode, mode, (long long)readAdvancedTo);
    return error_code_ok;
}

bool MessageManager::setConversation(int32_t userid, const SetConversationRequest& req,
                                     int64_t updateTime, int& errorCode)
{
    errorCode = error_code_server_internal;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::setConversation db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //提醒模式的上限与解除屏蔽处理，要与置顶、删除标记的写入一起成败
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MessageManager::setConversation begin transaction failed, userid: %d, cid: %lld",
             userid, (long long)req.cid);
        return false;
    }

    //提醒模式单独一条语句写：要先读旧值，才能做特别关注上限与解除屏蔽两项处理
    if (req.has_notifymode)
    {
        int starredCount = queryNotifyModeCount(pConn.get(), userid, notify_mode_starred);
        if (starredCount < 0)
        {
            LOGE("MessageManager::setConversation count starred failed, userid: %d", userid);
            transaction.Rollback();
            return false;
        }

        int code = applyNotifyMode(pConn.get(), userid, req.ctype, req.cid, req.notifymode,
                                   updateTime, starredCount);
        if (code != error_code_ok)
        {
            errorCode = code;
            transaction.Rollback();
            return false;
        }
    }

    //置顶、删除与手动标为未读。只改了提醒模式时这一条可以省掉：会话行在上面已经写过了
    if (req.has_pinned || req.has_deleted || req.has_unreadmark || !req.has_notifymode)
    {
        //INSERT 部分给这几列一个值（未指定的取 0，与 DEFAULT 0 一致）；冲突时只改 has_* 为 true 的列
        int pinnedVal  = (req.has_pinned  && req.pinned)  ? 1 : 0;
        int deletedVal = (req.has_deleted && req.deleted) ? 1 : 0;
        int unreadVal  = (req.has_unreadmark && req.unreadmark) ? 1 : 0;

        //删除会话时记下"删除那一刻的最后消息msgid"作锚点：之后只要该会话来了比锚点更新的
        //消息，getConversationBriefs 就让它重新出现（见 isConversationHidden）。取消删除时
        //把锚点清 0，下次再删会重新记。
        int64_t deletedMsgId = 0;
        if (deletedVal != 0)
            deletedMsgId = queryConversationLastMsgId(pConn.get(), userid, req.ctype, req.cid);

        std::ostringstream osSql;
        osSql << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_pinned, f_deleted, f_deleted_msgid, f_unread_mark, f_update_time) VALUES("
              << userid << ", " << req.ctype << ", " << req.cid << ", "
              << pinnedVal << ", " << deletedVal << ", "
              << deletedMsgId << ", " << unreadVal << ", " << updateTime
              << ")" << dbapi::UpsertClause(CONVERSATION_CONFLICT_COLUMNS)
              << "f_update_time = " << updateTime;
        if (req.has_pinned)
            osSql << ", f_pinned = " << pinnedVal;
        if (req.has_deleted)
        {
            osSql << ", f_deleted = " << deletedVal;
            osSql << ", f_deleted_msgid = " << deletedMsgId;
        }
        //手动标为未读（docs/会话标为未读方案.md）：请求里带了这个字段才改
        if (req.has_unreadmark)
            osSql << ", f_unread_mark = " << unreadVal;

        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("MessageManager::setConversation error, userid: %d, cid: %lld",
                 userid, (long long)req.cid);
            transaction.Rollback();
            return false;
        }

        if (req.has_deleted && req.deleted)
        {
            LOGI("conversation deleted, userid: %d, ctype: %d, cid: %lld, anchor msgid: %lld",
                 userid, req.ctype, (long long)req.cid, (long long)deletedMsgId);
        }
    }

    if (!transaction.Commit())
    {
        LOGE("MessageManager::setConversation commit failed, userid: %d, cid: %lld",
             userid, (long long)req.cid);
        return false;
    }

    errorCode = error_code_ok;
    return true;
}

bool MessageManager::setConversation(int32_t userid, const SetConversationRequest& req,
                                     int64_t updateTime)
{
    int errorCode = error_code_ok;
    return setConversation(userid, req, updateTime, errorCode);
}

//批量设置失败时，把本批里原本要写（结果码仍为成功）的条目一律标成服务器内部错误。
//事务已整体回滚，已经写过的那几条也一并作废，故同样要改。
static void markPendingAsInternalError(std::vector<NotifyModeResult>& results)
{
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (results[i].code == error_code_ok)
            results[i].code = error_code_server_internal;
    }
}

bool MessageManager::setNotifyModes(int32_t userid, const std::vector<NotifyModeItem>& items,
                                    int64_t updateTime, std::vector<NotifyModeResult>& results,
                                    std::vector<NotifyModeItem>& changed)
{
    changed.clear();
    if (results.size() != items.size())
    {
        LOGE("MessageManager::setNotifyModes results size mismatch, userid: %d, items: %u, results: %u",
             userid, static_cast<unsigned>(items.size()), static_cast<unsigned>(results.size()));
        markPendingAsInternalError(results);
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::setNotifyModes db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        markPendingAsInternalError(results);
        return false;
    }

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MessageManager::setNotifyModes begin transaction failed, userid: %d", userid);
        markPendingAsInternalError(results);
        return false;
    }

    int starredCount = queryNotifyModeCount(pConn.get(), userid, notify_mode_starred);
    if (starredCount < 0)
    {
        LOGE("MessageManager::setNotifyModes count starred failed, userid: %d", userid);
        transaction.Rollback();
        markPendingAsInternalError(results);
        return false;
    }

    std::vector<NotifyModeItem> written;
    for (size_t i = 0; i < items.size(); ++i)
    {
        //调用方已判定非法的条目
        if (results[i].code != error_code_ok)
            continue;

        int code = applyNotifyMode(pConn.get(), userid, items[i].ctype, items[i].cid, items[i].mode,
                                   updateTime, starredCount);
        if (code == error_code_server_internal)
        {
            transaction.Rollback();
            markPendingAsInternalError(results);
            return false;
        }

        results[i].code = code;
        if (code == error_code_ok)
            written.push_back(items[i]);
    }

    if (!transaction.Commit())
    {
        LOGE("MessageManager::setNotifyModes commit failed, userid: %d", userid);
        markPendingAsInternalError(results);
        return false;
    }

    changed.swap(written);
    LOGI("set notify modes ok, userid: %d, items: %u, written: %u",
         userid, static_cast<unsigned>(items.size()), static_cast<unsigned>(changed.size()));
    return true;
}

bool MessageManager::getNotifyModes(int32_t userid, std::vector<NotifyModeItem>& out)
{
    out.clear();

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getNotifyModes db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_ctype, f_cid, f_notify_mode FROM t_conversation WHERE f_userid = " << userid
          << " AND f_notify_mode <> " << notify_mode_allow << " ORDER BY f_ctype, f_cid";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MessageManager::getNotifyModes query failed, userid: %d", userid);
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        NotifyModeItem item;
        item.ctype = pRow[0].getInt32();
        item.cid   = static_cast<int64_t>(pRow[1].getUInt64());
        item.mode  = pRow[2].getInt32();
        out.push_back(item);

        //fetch 只取当前行、不前进，必须显式移到下一行，否则会反复读到同一行
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::countNotifyMode(int32_t userid, int mode, int& count)
{
    count = 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::countNotifyMode db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    int value = queryNotifyModeCount(pConn.get(), userid, mode);
    if (value < 0)
    {
        LOGE("MessageManager::countNotifyMode query failed, userid: %d, mode: %d", userid, mode);
        return false;
    }
    count = value;
    return true;
}

bool MessageManager::getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                                          bool& pinned, int& notifymode, bool& deleted)
{
    bool unreadmark = false;
    return getConversationState(userid, ctype, cid, pinned, notifymode, deleted, unreadmark);
}

bool MessageManager::getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                                          bool& pinned, int& notifymode, bool& deleted,
                                          bool& unreadmark)
{
    pinned     = false;
    notifymode = notify_mode_allow;
    deleted    = false;
    unreadmark = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getConversationState db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_pinned, f_notify_mode, f_deleted, f_unread_mark FROM t_conversation WHERE f_userid = " << userid
          << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        pinned     = (pRow[0].getInt32() != 0);
        notifymode = pRow[1].getInt32();
        deleted    = (pRow[2].getInt32() != 0);
        unreadmark = (pRow[3].getInt32() != 0);
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::getPinnedConversationIds(int32_t userid, int32_t ctype, std::set<int64_t>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getPinnedConversationIds db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //不看 f_deleted：删除会话只是把它从会话列表里隐藏，置顶标记仍然有效
    std::ostringstream osSql;
    osSql << "SELECT f_cid FROM t_conversation WHERE f_userid = " << userid
          << " AND f_ctype = " << ctype << " AND f_pinned = 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    //结果集为空时 query 也返回 NULL：该用户没有置顶任何此类会话
    if (pResult == NULL)
        return true;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        out.insert(static_cast<int64_t>(pRow[0].getUInt64()));

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::searchMessages(int32_t userid, const SearchMessagesRequest& req,
                                    std::vector<MessageRecord>& out, bool& hasMore, int64_t minCreateTime,
                                    int64_t minMsgId)
{
    hasMore = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::searchMessages db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //先按 LIKE 规则转义 ! % _，再按 SQL 字符串字面量规则转义引号等，顺序不能颠倒
    const std::string likeEsc = dbapi::EscapeString(chatserver::MakeLikeContainsPattern(req.keyword));

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
             "f_recalled, f_create_time, f_recalled_by FROM t_chatmsg WHERE ";
    if (req.ctype == conversation_group)
    {
        //群聊：在本群（f_to=群id）范围内搜
        osSql << "f_ctype = " << conversation_group << " AND f_to = " << req.cid;
    }
    else
    {
        //单聊：在本人与对端的双向消息范围内搜
        int32_t peerid = static_cast<int32_t>(req.cid);
        osSql << "f_ctype = " << conversation_single
              << " AND ((f_from = " << userid << " AND f_to = " << peerid << ") OR "
                 "(f_from = " << peerid << " AND f_to = " << userid << "))";
    }
    //查入库时提取的纯文本；还没回填的存量行（NULL）退回查 f_content，回填期间不少结果。
    //已撤回的消息只剩一条撤回提示，不算命中
    osSql << visibleToUserClause(userid)
          << " AND t_chatmsg.f_recalled = 0"
          << " AND (t_chatmsg.f_search_text LIKE '" << likeEsc << "' ESCAPE '!'"
          << " OR (t_chatmsg.f_search_text IS NULL AND t_chatmsg.f_content LIKE '" << likeEsc << "' ESCAPE '!'))";
    if (minCreateTime > 0)
        osSql << " AND t_chatmsg.f_create_time >= " << minCreateTime;
    //msgid 下界：邀请时选择了「同步最近 N 条」，与拉历史同一口径
    if (minMsgId > 0)
        osSql << " AND t_chatmsg.f_msgid >= " << minMsgId;
    if (req.beforemsgid > 0)
        osSql << " AND f_msgid < " << req.beforemsgid;
    //降序查 limit+1 行：多取的 1 行用于判断是否还有更早命中
    osSql << " ORDER BY f_msgid DESC LIMIT " << (req.limit + 1);

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    std::vector<MessageRecord> rows;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        MessageRecord rec;
        fillMessageRecord(pRow, rec);
        rows.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;

    if (req.limit > 0 && static_cast<int>(rows.size()) > req.limit)
    {
        hasMore = true;
        rows.resize(req.limit);
    }
    //查询是降序，统一反转为 msgid 升序（时间正序）返回
    out.assign(rows.rbegin(), rows.rend());
    return true;
}

//生成「本人参与的单聊或 groups 里的群聊」这一段 WHERE 条件（不带前后空格，外面已加括号即可直接拼接）。
//群聊：没有下界的群合成一个 IN 列表，有入群时间下界或 msgid 下界的群各自一个条件；一个群都没有时群聊
//部分恒不匹配。全局搜索与按改写序号同步共用，两处的可见范围因此始终一致。列一律带 t_chatmsg. 前缀，
//理由见 visibleToUserClause 的说明。
static std::string userConversationScopeClause(int32_t userid, const std::vector<GroupSearchScope>& groups)
{
    std::ostringstream osGroups;
    {
        std::ostringstream osIn;
        int inCount = 0;
        std::ostringstream osTimed;
        int timedCount = 0;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (groups[i].minCreateTime > 0 || groups[i].minMsgId > 0)
            {
                osTimed << (timedCount > 0 ? " OR " : "") << "(t_chatmsg.f_to = " << groups[i].groupid;
                if (groups[i].minCreateTime > 0)
                    osTimed << " AND t_chatmsg.f_create_time >= " << groups[i].minCreateTime;
                if (groups[i].minMsgId > 0)
                    osTimed << " AND t_chatmsg.f_msgid >= " << groups[i].minMsgId;
                osTimed << ")";
                ++timedCount;
            }
            else
            {
                osIn << (inCount > 0 ? ", " : "") << groups[i].groupid;
                ++inCount;
            }
        }
        if (inCount == 0 && timedCount == 0)
        {
            //不在任何群里：群聊一条也不匹配
            osGroups << "0 = 1";
        }
        else
        {
            if (inCount > 0)
                osGroups << "t_chatmsg.f_to IN (" << osIn.str() << ")";
            if (inCount > 0 && timedCount > 0)
                osGroups << " OR ";
            if (timedCount > 0)
                osGroups << osTimed.str();
        }
    }

    std::ostringstream os;
    os << "((t_chatmsg.f_ctype = " << conversation_single << " AND (t_chatmsg.f_from = " << userid
       << " OR t_chatmsg.f_to = " << userid << ")) OR (t_chatmsg.f_ctype = " << conversation_group
       << " AND (" << osGroups.str() << ")))";
    return os.str();
}

bool MessageManager::getRewrittenMessages(int32_t userid, const std::vector<GroupSearchScope>& groups, int64_t since,
                                          int limit, std::vector<RewrittenMessage>& out, bool& hasMore,
                                          int64_t& currev)
{
    out.clear();
    hasMore = false;
    currev = 0;
    if (limit <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getRewrittenMessages db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //库里当前的最大序号：客户端本地库为空时直接把水位设成它（docs/消息改写离线同步方案.md 3.4）
    {
        QueryResult* pResult = pConn->query("SELECT COALESCE(MAX(f_rev), 0) FROM t_chatmsg");
        if (pResult == NULL)
        {
            LOGE("MessageManager::getRewrittenMessages failed to read MAX(f_rev), userid: %d", userid);
            return false;
        }
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            currev = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }

    //前 10 列与 fillMessageRecord 的列序一致，第 11 列是 f_rev；多取 1 行以判断后面是否还有
    std::ostringstream osSql;
    osSql << "SELECT t_chatmsg.f_msgid, t_chatmsg.f_clientmsgid, t_chatmsg.f_ctype, t_chatmsg.f_from, "
             "t_chatmsg.f_to, t_chatmsg.f_msgtype, t_chatmsg.f_content, t_chatmsg.f_recalled, "
             "t_chatmsg.f_create_time, t_chatmsg.f_recalled_by, t_chatmsg.f_rev FROM t_chatmsg WHERE "
          << "t_chatmsg.f_rev > " << since << " AND " << userConversationScopeClause(userid, groups)
          << visibleToUserClause(userid)
          << " ORDER BY t_chatmsg.f_rev ASC LIMIT " << (limit + 1);
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MessageManager::getRewrittenMessages query error, userid: %d, since: %lld", userid, (long long)since);
        return false;
    }
    //f_rev 在结果行里的列下标（前 10 列由 fillMessageRecord 读取）
    const int kRevColumn = 10;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        RewrittenMessage item;
        fillMessageRecord(pRow, item.record);
        item.rev = static_cast<int64_t>(pRow[kRevColumn].getUInt64());
        out.push_back(item);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;

    //行数超过 limit 说明后面还有；丢弃多取的那 1 行
    if (static_cast<int>(out.size()) > limit)
    {
        hasMore = true;
        out.resize(limit);
    }
    return true;
}

bool MessageManager::globalSearchConversations(int32_t userid, const std::string& keyword,
                                               const std::vector<GroupSearchScope>& groups, int limit,
                                               std::vector<GlobalSearchConversationHit>& out)
{
    out.clear();
    if (keyword.empty() || limit <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::globalSearchConversations db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //先按 LIKE 规则转义 ! % _，再按 SQL 字符串字面量规则转义引号等，顺序不能颠倒
    const std::string likeEsc = dbapi::EscapeString(chatserver::MakeLikeContainsPattern(keyword));

    //本人参与的单聊与 groups 里的群聊（见 userConversationScopeClause）
    const std::string scopeClause = userConversationScopeClause(userid, groups);

    //会话 id 取本人视角：群聊为群 id，单聊为对方 userid
    std::ostringstream osCid;
    osCid << "CASE WHEN t_chatmsg.f_ctype = " << conversation_group << " THEN t_chatmsg.f_to"
          << " WHEN t_chatmsg.f_from = " << userid << " THEN t_chatmsg.f_to ELSE t_chatmsg.f_from END";

    //第一步：按会话汇总命中条数与最近一条命中的 msgid
    std::ostringstream osSql;
    osSql << "SELECT t_chatmsg.f_ctype, " << osCid.str() << ", COUNT(*), MAX(t_chatmsg.f_msgid) FROM t_chatmsg WHERE "
          << scopeClause
          << visibleToUserClause(userid)
          << " AND t_chatmsg.f_recalled = 0"
          << " AND (t_chatmsg.f_search_text LIKE '" << likeEsc << "' ESCAPE '!'"
          << " OR (t_chatmsg.f_search_text IS NULL AND t_chatmsg.f_content LIKE '" << likeEsc << "' ESCAPE '!'))"
          << " GROUP BY t_chatmsg.f_ctype, " << osCid.str()
          << " ORDER BY MAX(t_chatmsg.f_msgid) DESC LIMIT " << limit;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("MessageManager::globalSearchConversations query error, userid: %d", userid);
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        GlobalSearchConversationHit hit;
        hit.ctype       = pRow[0].getInt32();
        hit.cid         = static_cast<int64_t>(pRow[1].getUInt64());
        hit.hitcount    = pRow[2].getInt32();
        hit.latestmsgid = static_cast<int64_t>(pRow[3].getUInt64());
        out.push_back(hit);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    if (out.empty())
        return true;

    //第二步：取各会话最近一条命中的时间与文字。不写成一条语句：MySQL 不保证聚合查询里非聚合列
    //取自 MAX 所在的那一行
    std::ostringstream osDetail;
    osDetail << "SELECT f_msgid, f_create_time, f_msgtype, f_search_text, f_content FROM t_chatmsg WHERE f_msgid IN (";
    for (size_t i = 0; i < out.size(); ++i)
        osDetail << (i > 0 ? ", " : "") << out[i].latestmsgid;
    osDetail << ")";
    QueryResult* pDetail = pConn->query(osDetail.str().c_str());
    if (pDetail == NULL)
    {
        LOGE("MessageManager::globalSearchConversations detail query error, userid: %d", userid);
        return false;
    }
    std::map<int64_t, std::pair<int64_t, std::string> > details;
    while (true)
    {
        Field* pRow = pDetail->fetch();
        if (pRow == NULL)
            break;
        const int64_t msgid = static_cast<int64_t>(pRow[0].getUInt64());
        const int64_t createTime = static_cast<int64_t>(pRow[1].getUInt64());
        //还没回填搜索文本的存量行现场提取
        std::string text = pRow[3].getCppString();
        if (text.empty())
            text = chatserver::BuildStoredSearchText(pRow[2].getInt32(), pRow[4].getCppString());
        details[msgid] = std::make_pair(createTime, text);
        if (!pDetail->nextRow())
            break;
    }
    pDetail->endQuery();
    delete pDetail;

    for (size_t i = 0; i < out.size(); ++i)
    {
        std::map<int64_t, std::pair<int64_t, std::string> >::const_iterator it = details.find(out[i].latestmsgid);
        if (it == details.end())
            continue;
        out[i].latesttime = it->second.first;
        out[i].latesttext = chatserver::MakeSearchExcerpt(it->second.second, keyword, GLOBAL_SEARCH_EXCERPT_MAX_BYTES);
    }
    return true;
}

int MessageManager::backfillSearchText(int batchSize)
{
    if (batchSize <= 0)
        return -1;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::backfillSearchText db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return -1;
    }

    //先把这一批读出来（按 msgid 升序），再在一个事务里逐行写回
    struct PendingRow
    {
        int64_t     msgid;     // 消息ID
        int32_t     msgtype;   // 消息类型
        std::string content;   // f_content 原文
    };
    std::vector<PendingRow> rows;
    {
        std::ostringstream osSql;
        osSql << "SELECT f_msgid, f_msgtype, f_content FROM t_chatmsg WHERE f_search_text IS NULL "
                 "ORDER BY f_msgid LIMIT " << batchSize;
        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult == NULL)
        {
            LOGE("MessageManager::backfillSearchText select error");
            return -1;
        }
        while (true)
        {
            Field* pRow = pResult->fetch();
            if (pRow == NULL)
                break;
            PendingRow row;
            row.msgid   = static_cast<int64_t>(pRow[0].getUInt64());
            row.msgtype = pRow[1].getInt32();
            row.content = pRow[2].getCppString();
            rows.push_back(row);
            if (!pResult->nextRow())
                break;
        }
        pResult->endQuery();
        delete pResult;
    }
    if (rows.empty())
        return 0;

    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MessageManager::backfillSearchText begin transaction error");
        return -1;
    }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const std::string searchText = chatserver::BuildStoredSearchText(rows[i].msgtype, rows[i].content);
        std::vector<char> escSearch(searchText.size() * 2 + 1, '\0');
        pConn->escapeString(&escSearch[0], searchText.c_str(), static_cast<uint32_t>(searchText.size()));
        std::ostringstream osSql;
        osSql << "UPDATE t_chatmsg SET f_search_text = '" << &escSearch[0] << "' WHERE f_msgid = " << rows[i].msgid;
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("MessageManager::backfillSearchText update error, msgid: %lld", (long long)rows[i].msgid);
            return -1;
        }
    }
    if (!transaction.Commit())
    {
        LOGE("MessageManager::backfillSearchText commit error");
        return -1;
    }
    return static_cast<int>(rows.size());
}

bool MessageManager::deleteMessageForUser(int32_t userid, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::deleteMessageForUser db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //INSERT IGNORE：联合主键已存在（重复删除）时静默忽略，幂等
    std::ostringstream osSql;
    osSql << "INSERT IGNORE INTO t_message_deleted(f_userid, f_msgid) VALUES("
          << userid << ", " << msgid << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::deleteMessageForUser error, userid: %d, msgid: %lld",
             userid, (long long)msgid);
        return false;
    }
    return true;
}

//在给定连接上读出某用户的全部清理区间（按开始时间升序）。addDeletedRange 要在同一个事务、
//同一条连接上先读后写，故单独成函数、由调用方传入连接。
//  pConn：数据库连接（借用，不持有）；userid：用户ID；out：[出参] 区间列表，调用前清空。
//  返回：查询成功返回 true（没有任何区间也算成功）；查询出错返回 false。
static bool readDeletedRanges(IDatabase* pConn, int32_t userid,
                              std::vector<msgdelete::DeleteRange>& out)
{
    out.clear();

    std::ostringstream osSql;
    osSql << "SELECT f_begin_time, f_end_time FROM t_message_deleted_range WHERE f_userid = "
          << userid << " ORDER BY f_begin_time";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //两种后端在"查询成功但没有任何行"时都返回空结果集而不是 NULL（DatabaseSqlite.cpp /
        //DatabaseMysql.cpp 的 query），NULL 只表示出错。这里必须按失败处理：addDeletedRange
        //若把出错当成"没有旧区间"，随后会删掉本人的全部旧行、只写回本次的区间，
        //之前清理过的消息就会重新出现
        LOGE("MessageManager readDeletedRanges query error, userid: %d", userid);
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        msgdelete::DeleteRange range;
        range.begintime = static_cast<int64_t>(pRow[0].getUInt64());
        range.endtime = static_cast<int64_t>(pRow[1].getUInt64());
        out.push_back(range);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::addDeletedRange(int32_t userid, const msgdelete::DeleteRange& range,
                                     int64_t nowMs)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::addDeletedRange db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //先读后写必须在同一个事务里：两台设备同时清理时，若读写之间被对方插进来一次写入，
    //各自按旧数据合并再整体覆盖，就会把对方刚写的区间冲掉
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("MessageManager::addDeletedRange begin transaction error, userid: %d", userid);
        return false;
    }

    std::vector<msgdelete::DeleteRange> existing;
    if (!readDeletedRanges(pConn.get(), userid, existing))
        return false;

    const std::vector<msgdelete::DeleteRange> merged =
        msgdelete::MergeDeleteRanges(existing, range);

    //整体替换：删掉本人的全部旧行，再逐段写回合并结果。段数通常只有几段
    std::ostringstream osDelete;
    osDelete << "DELETE FROM t_message_deleted_range WHERE f_userid = " << userid;
    if (!pConn->execute(osDelete.str().c_str()))
    {
        LOGE("MessageManager::addDeletedRange delete error, userid: %d", userid);
        return false;
    }

    for (size_t i = 0; i < merged.size(); ++i)
    {
        std::ostringstream osInsert;
        osInsert << "INSERT INTO t_message_deleted_range(f_userid, f_begin_time, f_end_time, "
                    "f_update_time) VALUES("
                 << userid << ", " << merged[i].begintime << ", " << merged[i].endtime << ", "
                 << nowMs << ")";
        if (!pConn->execute(osInsert.str().c_str()))
        {
            LOGE("MessageManager::addDeletedRange insert error, userid: %d, begin: %lld, end: %lld",
                 userid, (long long)merged[i].begintime, (long long)merged[i].endtime);
            return false;
        }
    }

    //提交失败时守卫仍持有该事务，由它的析构补一次回滚
    if (!transaction.Commit())
    {
        LOGE("MessageManager::addDeletedRange commit error, userid: %d", userid);
        return false;
    }

    LOGI("add deleted range ok, userid: %d, begin: %lld, end: %lld, ranges: %u -> %u",
         userid, (long long)range.begintime, (long long)range.endtime,
         static_cast<unsigned>(existing.size()), static_cast<unsigned>(merged.size()));
    return true;
}

bool MessageManager::getDeletedRanges(int32_t userid, std::vector<msgdelete::DeleteRange>& out)
{
    out.clear();

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getDeletedRanges db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }
    return readDeletedRanges(pConn.get(), userid, out);
}

bool MessageManager::getMessagesByIds(int32_t viewerUserid, const std::vector<int64_t>& msgids,
                                      std::vector<MessageRecord>& out)
{
    if (msgids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getMessagesByIds db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
             "f_recalled, f_create_time, f_recalled_by FROM t_chatmsg WHERE f_msgid IN (";
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        if (i > 0)
            osSql << ",";
        osSql << msgids[i];
    }
    //排除本人已删除的：否则构造请求直接带上已删除消息的 msgid，仍能把它转发出去
    osSql << ")" << visibleToUserClause(viewerUserid) << " ORDER BY f_msgid ASC";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        MessageRecord rec;
        fillMessageRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::getGroupConversationBriefs(int32_t userid, const std::vector<int64_t>& groupids,
                                                std::vector<ConversationBrief>& out)
{
    if (groupids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getGroupConversationBriefs db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //1) 取该用户全部群会话（ctype=2）的状态行（置顶 / 免打扰 / 已读位点 / 删除标记）
    std::map<int64_t, ConvState> stateMap;
    {
        std::ostringstream osSql;
        //IFNULL(-1) 的理由同 getConversationBriefs：区分"老记录没有锚点"与"锚点为 0"。
        osSql << "SELECT f_cid, f_pinned, f_notify_mode, f_readmsgid, f_deleted, "
                 "IFNULL(f_deleted_msgid, -1), f_unread_mark FROM t_conversation "
                 "WHERE f_userid = " << userid << " AND f_ctype = " << conversation_group;
        QueryResult* pResult = pConn->query(osSql.str().c_str());
        if (pResult != NULL)
        {
            while (true)
            {
                Field* pRow = pResult->fetch();
                if (pRow == NULL)
                    break;

                int64_t cid = static_cast<int64_t>(pRow[0].getUInt64());
                ConvState st;
                st.pinned       = pRow[1].getInt32();
                st.notifymode   = pRow[2].getInt32();
                st.readmsgid    = static_cast<int64_t>(pRow[3].getUInt64());
                st.deleted      = pRow[4].getInt32();
                st.deletedmsgid = static_cast<int64_t>(pRow[5].getUInt64());
                st.unreadmark   = pRow[6].getInt32();
                stateMap[cid] = st;

                if (!pResult->nextRow())
                    break;
            }
            pResult->endQuery();
            delete pResult;
        }
    }

    //2) 一次查出每个群的最新 msgid（排除本人已删除的），再一次批量取回这些消息的详情。
    //   原先是逐群两条 SQL，群一多就线性变慢；现在这两步与群数量无关，只剩下面的
    //   未读数还是逐群一条 —— 那条经 kMaxUnreadCount 封顶后是固定小开销，
    //   硬要合并得为每个群拼各自的已读位点阈值，SQL 变得难读难维护，收益却有限。
    std::map<int64_t, int64_t> lastMsgIdMap;   //群id → 该群最新一条的 msgid
    if (!groupids.empty())
    {
        for (size_t begin = 0; begin < groupids.size(); begin += kDetailBatchSize)
        {
            const size_t end = (begin + kDetailBatchSize < groupids.size())
                               ? (begin + kDetailBatchSize) : groupids.size();

            std::ostringstream osSql;
            osSql << "SELECT f_to, MAX(f_msgid) FROM t_chatmsg WHERE f_ctype = "
                  << conversation_group << " AND f_to IN (";
            for (size_t i = begin; i < end; ++i)
            {
                if (i > begin)
                    osSql << ",";
                osSql << groupids[i];
            }
            osSql << ")" << visibleToUserClause(userid) << " GROUP BY f_to";

            QueryResult* pResult = pConn->query(osSql.str().c_str());
            if (pResult == NULL)
            {
                continue;
            }
            while (true)
            {
                Field* pRow = pResult->fetch();
                if (pRow == NULL)
                    break;
                lastMsgIdMap[static_cast<int64_t>(pRow[0].getUInt64())] =
                    static_cast<int64_t>(pRow[1].getUInt64());
                if (!pResult->nextRow())
                    break;
            }
            pResult->endQuery();
            delete pResult;
        }
    }

    std::map<int64_t, LastMsgDetail> detailMap;
    {
        std::vector<int64_t> wanted;
        for (std::map<int64_t, int64_t>::iterator it = lastMsgIdMap.begin();
             it != lastMsgIdMap.end(); ++it)
        {
            wanted.push_back(it->second);
        }
        fetchLastMsgDetails(pConn.get(), wanted, detailMap);
    }

    //3) 逐群拼出摘要；无任何消息的群不纳入会话列表（与单聊口径一致）
    for (size_t i = 0; i < groupids.size(); ++i)
    {
        int64_t groupid = groupids[i];

        ConvState st;
        std::map<int64_t, ConvState>::iterator it = stateMap.find(groupid);
        if (it != stateMap.end())
            st = it->second;
        //注意：已删除会话的判定放在取到 lastmsgid 之后——要拿最后消息与删除锚点比，
        //才能判断"删除之后有没有来新消息"。

        ConversationBrief brief;
        brief.ctype     = conversation_group;
        brief.cid       = groupid;
        brief.pinned    = (st.pinned != 0);
        brief.notifymode = st.notifymode;
        brief.unreadmark = (st.unreadmark != 0);

        bool hasMsg = false;
        std::map<int64_t, int64_t>::iterator itLast = lastMsgIdMap.find(groupid);
        if (itLast != lastMsgIdMap.end() && itLast->second > 0)
        {
            brief.lastmsgid = itLast->second;
            hasMsg = true;

            std::map<int64_t, LastMsgDetail>::iterator itDetail =
                detailMap.find(brief.lastmsgid);
            if (itDetail != detailMap.end())
            {
                brief.lastfrom      = itDetail->second.from;
                brief.lastmsgtype   = itDetail->second.msgtype;
                brief.lastcontent   = itDetail->second.content;
                brief.lasttimestamp = itDetail->second.timestamp;
                brief.lastrecalled  = itDetail->second.recalled;
                brief.lastrecalledby = itDetail->second.recalledby;
            }
        }
        //群尚无任何消息：不纳入会话列表（用户可从通讯录的群列表进入该群）
        if (!hasMsg)
            continue;

        //已删除的群会话：只有在"删除之后没有更新的群消息"时才继续隐藏
        if (isConversationHidden(st, brief.lastmsgid))
            continue;

        //未读数：来自他人、msgid 大于已读位点的群消息条数。
        //与单聊那条同一套写法：数到 kMaxUnreadCount 就停，工作量与积压量无关。
        //群聊是这次优化的主要受益方 —— 实测里"越用越慢"那条曲线就是这条查询画出来的。
        //服务端生成的系统消息（发布公告、入群欢迎语、退出群聊、转让群主）不计：它们是群里的
        //变动通知，不是有人对你说话；客户端实时收到时同样不计（ConversationsProtocolClient::
        //shouldCountAsUnread），两边口径一致，重新登录后未读数才不会凭空多出来
        {
            std::ostringstream osSql;
            osSql << "SELECT COUNT(*) FROM (SELECT 1 FROM t_chatmsg"
                     " WHERE f_ctype = " << conversation_group
                  << " AND f_to = " << groupid << " AND f_from != " << userid
                  << " AND f_msgtype != " << message_type_system
                  << " AND f_msgid > " << st.readmsgid << visibleToUserClause(userid)
                  << " LIMIT " << kMaxUnreadCount << ") t";
            QueryResult* pResult = pConn->query(osSql.str().c_str());
            if (pResult != NULL)
            {
                Field* pRow = pResult->fetch();
                if (pRow != NULL)
                    brief.unread = pRow[0].getInt32();
                pResult->endQuery();
                delete pResult;
            }
        }

        //系统消息不计未读的唯一例外：「有人入群」提示对名单里的新成员本人计未读（2026-10-07 用户确认），
        //加群申请审批通过、被人拉进群的人因此能在会话行上看到红点。客户端实时收到时同一口径
        //（ConversationsProtocolClient::shouldCountAsUnread）。只在已读位点之后还有消息时才查：
        //全部已读的群（会话列表里的绝大多数）不多出任何查询
        if (brief.lastmsgid > st.readmsgid && brief.unread < kMaxUnreadCount)
            brief.unread += countJoinTipsForUser(pConn.get(), userid, groupid, st.readmsgid,
                                                 kMaxUnreadCount - brief.unread);

        out.push_back(brief);
    }

    return true;
}
