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
#include "../dbapi/SqlDialect.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"   //会话类型常量 conversation_single / conversation_group

//t_conversation 的 UPSERT 冲突列。必须与该表主键 (f_userid, f_ctype, f_cid) 完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define CONVERSATION_CONFLICT_COLUMNS   "f_userid, f_ctype, f_cid"

//t_file_ack 的 UPSERT 冲突列。同上，必须与该表主键 (f_msgid) 完全一致。
#define FILE_ACK_CONFLICT_COLUMNS       "f_msgid"

MessageManager::MessageManager()
{
}

MessageManager::~MessageManager()
{
}

bool MessageManager::init()
{
    return true;
}

int64_t MessageManager::saveMessage(const std::string& clientmsgid, int32_t ctype,
                                    int32_t fromUserid, int32_t toUserid, int32_t msgtype,
                                    const std::string& storedContent, int64_t createTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::saveMessage db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //f_content 是 JSON 文本，含双引号、可能含单引号，必须经 escapeString
    //转义后才能拼进 SQL；转义后最坏长度为原串两倍，故缓冲区取 2n+1
    std::vector<char> escContent(storedContent.size() * 2 + 1, '\0');
    pConn->escapeString(&escContent[0], storedContent.c_str(),
                        static_cast<uint32_t>(storedContent.size()));
    std::vector<char> escClientId(clientmsgid.size() * 2 + 1, '\0');
    pConn->escapeString(&escClientId[0], clientmsgid.c_str(),
                        static_cast<uint32_t>(clientmsgid.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_chatmsg(f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_create_time) VALUES('"
          << &escClientId[0] << "', " << ctype << ", " << fromUserid << ", " << toUserid << ", "
          << msgtype << ", '" << &escContent[0] << "', " << static_cast<long long>(createTime) << ")";
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
    int     mute;          //是否免打扰
    int64_t readmsgid;     //已读位点
    int     deleted;       //是否已从列表删除
    int64_t deletedmsgid;  //删除会话那一刻该会话的最后消息msgid（删除锚点）

    ConvState()
        : pinned(0), mute(0), readmsgid(0), deleted(0), deletedmsgid(0)
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
//调用方的 SELECT 必须按此列序取 9 列：
//f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, f_recalled, f_create_time
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
    //从 f_content 解出 content/mentions/mentionall/quote
    std::string blob = pRow[6].getCppString();
    MessageProtocol::parseStoredContent(blob, rec.content, rec.mentions,
                                        rec.mentionall, rec.quote);
}

//生成"排除该用户已删除消息"的 SQL 片段（拼到 WHERE 条件之后，自带前置空格）
static std::string notDeletedClause(int32_t userid)
{
    std::ostringstream os;
    os << " AND f_msgid NOT IN (SELECT f_msgid FROM t_message_deleted WHERE f_userid = "
       << userid << ")";
    return os.str();
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
        osSql << "SELECT f_msgid, f_from, f_msgtype, f_content, f_create_time "
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
        //f_deleted_msgid 是后加的列，老库经 checkTable 自动 ALTER 补出来时该列可为 NULL
        //（ALTER 用的是裸类型、不带 DEFAULT）。故用 IFNULL 兜底成 -1，把"老记录没有锚点"
        //与"锚点确实是 0"区分开，两者行为不同，见 isConversationHidden。
        osSql << "SELECT f_cid, f_pinned, f_mute, f_readmsgid, f_deleted, "
                 "IFNULL(f_deleted_msgid, -1) FROM t_conversation "
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
                st.mute         = pRow[2].getInt32();
                st.readmsgid    = static_cast<int64_t>(pRow[3].getUInt64());
                st.deleted      = pRow[4].getInt32();
                st.deletedmsgid = static_cast<int64_t>(pRow[5].getUInt64());
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
              << " OR f_to = " << userid << ")" << notDeletedClause(userid)
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
        brief.mute      = (st.mute != 0);
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
                  << notDeletedClause(userid)
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
                                             int64_t minCreateTime)
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
             "f_recalled, f_create_time FROM t_chatmsg WHERE ";
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
    osSql << notDeletedClause(userid);

    //时间下界：群设为"新成员不可查看入群前历史消息"时，调用方把该成员的入群
    //时间传进来，早于它的消息一律不返回。0 表示不限（单聊、以及允许看历史的群）。
    if (minCreateTime > 0)
        osSql << " AND f_create_time >= " << minCreateTime;

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
          << "), f_update_time = " << updateTime;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::markRead error, userid: %d, cid: %lld, readmsgid: %lld",
             userid, (long long)cid, (long long)readmsgid);
        return false;
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
             "f_recalled, f_create_time FROM t_chatmsg WHERE f_msgid = " << msgid;
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

bool MessageManager::recallMessage(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::recallMessage db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_chatmsg SET f_recalled = 1 WHERE f_msgid = " << msgid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::recallMessage error, msgid: %lld", (long long)msgid);
        return false;
    }
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
    osSql << notDeletedClause(userid);

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

bool MessageManager::setConversation(int32_t userid, const SetConversationRequest& req,
                                     int64_t updateTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::setConversation db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //INSERT 部分给所有可变列一个值（未指定的列取 0，与 DEFAULT 0 一致）；
    //冲突时只改 has_* 为 true 的列
    int pinnedVal  = (req.has_pinned  && req.pinned)  ? 1 : 0;
    int muteVal    = (req.has_mute    && req.mute)    ? 1 : 0;
    int deletedVal = (req.has_deleted && req.deleted) ? 1 : 0;

    //删除会话时记下"删除那一刻的最后消息msgid"作锚点：之后只要该会话来了比锚点更新的
    //消息，getConversationBriefs 就让它重新出现（见 isConversationHidden）。取消删除时
    //把锚点清 0，下次再删会重新记。
    int64_t deletedMsgId = 0;
    if (deletedVal != 0)
        deletedMsgId = queryConversationLastMsgId(pConn.get(), userid, req.ctype, req.cid);

    std::ostringstream osSql;
    osSql << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_pinned, f_mute, f_deleted, f_deleted_msgid, f_update_time) VALUES("
          << userid << ", " << req.ctype << ", " << req.cid << ", "
          << pinnedVal << ", " << muteVal << ", " << deletedVal << ", "
          << deletedMsgId << ", " << updateTime
          << ")" << dbapi::UpsertClause(CONVERSATION_CONFLICT_COLUMNS)
          << "f_update_time = " << updateTime;
    if (req.has_pinned)
        osSql << ", f_pinned = " << pinnedVal;
    if (req.has_mute)
        osSql << ", f_mute = " << muteVal;
    if (req.has_deleted)
    {
        osSql << ", f_deleted = " << deletedVal;
        osSql << ", f_deleted_msgid = " << deletedMsgId;
    }

    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("MessageManager::setConversation error, userid: %d, cid: %lld",
             userid, (long long)req.cid);
        return false;
    }

    if (req.has_deleted && req.deleted)
    {
        LOGI("conversation deleted, userid: %d, ctype: %d, cid: %lld, anchor msgid: %lld",
             userid, req.ctype, (long long)req.cid, (long long)deletedMsgId);
    }
    return true;
}

bool MessageManager::getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                                          bool& pinned, bool& mute, bool& deleted)
{
    pinned  = false;
    mute    = false;
    deleted = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::getConversationState db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_pinned, f_mute, f_deleted FROM t_conversation WHERE f_userid = " << userid
          << " AND f_ctype = " << ctype << " AND f_cid = " << cid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        pinned  = (pRow[0].getInt32() != 0);
        mute    = (pRow[1].getInt32() != 0);
        deleted = (pRow[2].getInt32() != 0);
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool MessageManager::searchMessages(int32_t userid, const SearchMessagesRequest& req,
                                    std::vector<MessageRecord>& out, bool& hasMore)
{
    hasMore = false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("MessageManager::searchMessages db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //关键字可能含引号等特殊字符，须转义后再拼进 LIKE
    std::vector<char> escKw(req.keyword.size() * 2 + 1, '\0');
    pConn->escapeString(&escKw[0], req.keyword.c_str(),
                        static_cast<uint32_t>(req.keyword.size()));

    std::ostringstream osSql;
    osSql << "SELECT f_msgid, f_clientmsgid, f_ctype, f_from, f_to, f_msgtype, f_content, "
             "f_recalled, f_create_time FROM t_chatmsg WHERE ";
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
    osSql << notDeletedClause(userid)
          << " AND f_content LIKE '%" << &escKw[0] << "%'";
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

bool MessageManager::getMessagesByIds(const std::vector<int64_t>& msgids,
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
             "f_recalled, f_create_time FROM t_chatmsg WHERE f_msgid IN (";
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        if (i > 0)
            osSql << ",";
        osSql << msgids[i];
    }
    osSql << ") ORDER BY f_msgid ASC";

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
        osSql << "SELECT f_cid, f_pinned, f_mute, f_readmsgid, f_deleted, "
                 "IFNULL(f_deleted_msgid, -1) FROM t_conversation "
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
                st.mute         = pRow[2].getInt32();
                st.readmsgid    = static_cast<int64_t>(pRow[3].getUInt64());
                st.deleted      = pRow[4].getInt32();
                st.deletedmsgid = static_cast<int64_t>(pRow[5].getUInt64());
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
            osSql << ")" << notDeletedClause(userid) << " GROUP BY f_to";

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
        brief.mute      = (st.mute != 0);

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
        {
            std::ostringstream osSql;
            osSql << "SELECT COUNT(*) FROM (SELECT 1 FROM t_chatmsg"
                     " WHERE f_ctype = " << conversation_group
                  << " AND f_to = " << groupid << " AND f_from != " << userid
                  << " AND f_msgid > " << st.readmsgid << notDeletedClause(userid)
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
