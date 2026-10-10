/**
 *  帖子群的存储管理 ThreadManager 的实现，见 ThreadManager.h 与 docs/帖子群方案.md 第 5 节。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadManager.h"

#include <memory>
#include <set>
#include <sstream>
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"

//t_thread_post 的列清单，SELECT 时按这个顺序取，readPostRow 按同一顺序解读
#define THREAD_POST_COLUMNS \
    "f_postid, f_groupid, f_author, f_client_postid, f_content, f_create_time, f_edit_time, f_active_time, " \
    "f_comment_count, f_pin_time, f_cardmsgid"

//t_thread_addendum 的列清单
#define THREAD_ADDENDUM_COLUMNS "f_addid, f_postid, f_groupid, f_author, f_content, f_create_time, f_cardmsgid"

//t_thread_comment 的列清单
#define THREAD_COMMENT_COLUMNS \
    "f_commentid, f_postid, f_groupid, f_author, f_parentid, f_reply_to, f_client_commentid, f_content, " \
    "f_create_time, f_deleted, f_reply_count, f_cardmsgid"

namespace {

//建一条数据库连接；失败时记日志并返回 NULL。caller 只用于日志
IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("ThreadManager::%s db init failed, db: %s", caller, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

//把一段文本按当前数据库的规则转义，供拼进单引号里的字符串字面量
std::string escapeText(IDatabase* pConn, const std::string& text)
{
    std::vector<char> buf(text.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], text.c_str(), static_cast<uint32_t>(text.size()));
    return std::string(&buf[0]);
}

//把一组 ID 拼成 "1, 2, 3"
std::string joinIds(const std::vector<int64_t>& ids)
{
    std::ostringstream os;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0)
            os << ", ";
        os << ids[i];
    }
    return os.str();
}

//读一行 THREAD_POST_COLUMNS
void readPostRow(Field* pRow, ThreadPost& p)
{
    int col = 0;
    p.postid = static_cast<int64_t>(pRow[col++].getUInt64());
    p.groupid = static_cast<int64_t>(pRow[col++].getUInt64());
    p.author = static_cast<int64_t>(pRow[col++].getUInt64());
    p.clientPostId = pRow[col++].getCppString();
    ThreadProtocol::bodyFromJson(pRow[col++].getCppString(), p.body);
    p.createTime = static_cast<int64_t>(pRow[col++].getUInt64());
    p.editTime = static_cast<int64_t>(pRow[col++].getUInt64());
    p.activeTime = static_cast<int64_t>(pRow[col++].getUInt64());
    p.commentCount = pRow[col++].getInt32();
    p.pinTime = static_cast<int64_t>(pRow[col++].getUInt64());
    p.cardMsgId = static_cast<int64_t>(pRow[col++].getUInt64());
}

//读一行 THREAD_ADDENDUM_COLUMNS
void readAddendumRow(Field* pRow, ThreadAddendum& a)
{
    int col = 0;
    a.addid = static_cast<int64_t>(pRow[col++].getUInt64());
    a.postid = static_cast<int64_t>(pRow[col++].getUInt64());
    a.groupid = static_cast<int64_t>(pRow[col++].getUInt64());
    a.author = static_cast<int64_t>(pRow[col++].getUInt64());
    ThreadProtocol::bodyFromJson(pRow[col++].getCppString(), a.body);
    a.createTime = static_cast<int64_t>(pRow[col++].getUInt64());
    a.cardMsgId = static_cast<int64_t>(pRow[col++].getUInt64());
}

//读一行 THREAD_COMMENT_COLUMNS
void readCommentRow(Field* pRow, ThreadComment& c)
{
    int col = 0;
    c.commentid = static_cast<int64_t>(pRow[col++].getUInt64());
    c.postid = static_cast<int64_t>(pRow[col++].getUInt64());
    c.groupid = static_cast<int64_t>(pRow[col++].getUInt64());
    c.author = static_cast<int64_t>(pRow[col++].getUInt64());
    c.parentid = static_cast<int64_t>(pRow[col++].getUInt64());
    c.replyTo = static_cast<int64_t>(pRow[col++].getUInt64());
    c.clientCommentId = pRow[col++].getCppString();
    ThreadProtocol::bodyFromJson(pRow[col++].getCppString(), c.body);
    c.createTime = static_cast<int64_t>(pRow[col++].getUInt64());
    c.deleted = pRow[col++].getInt32() != 0;
    c.replyCount = pRow[col++].getInt32();
    c.cardMsgId = static_cast<int64_t>(pRow[col++].getUInt64());
}

//执行一条查询并逐行交给 reader；caller 只用于日志
template <typename T>
bool queryRows(IDatabase* pConn, const std::string& sql, std::vector<T>& out, void (*reader)(Field*, T&),
               const char* caller)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::%s query error, sql: %s", caller, sql.c_str());
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        T item;
        reader(pRow, item);
        out.push_back(item);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

//执行一条只取第一行第一列整数的查询；没有行时 value 为 0
bool queryInt64(IDatabase* pConn, const std::string& sql, int64_t& value, const char* caller)
{
    value = 0;
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::%s query error, sql: %s", caller, sql.c_str());
        return false;
    }
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return true;
}

//执行一条写语句；失败时记日志
bool exec(IDatabase* pConn, const std::string& sql, const char* caller)
{
    if (!pConn->execute(sql.c_str()))
    {
        LOGE("ThreadManager::%s execute error, sql: %s", caller, sql.c_str());
        return false;
    }
    return true;
}

} // namespace

//======================== 帖子 ========================

bool ThreadManager::findPostByClientId(int64_t author, const std::string& clientPostId, int64_t& postid)
{
    postid = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("findPostByClientId"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT f_postid FROM t_thread_post WHERE f_author = " << author << " AND f_client_postid = '"
       << escapeText(pConn.get(), clientPostId) << "' LIMIT 1";
    return queryInt64(pConn.get(), os.str(), postid, "findPostByClientId");
}

bool ThreadManager::insertPost(ThreadPost& post)
{
    std::unique_ptr<IDatabase> pConn(openConnection("insertPost"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "INSERT INTO t_thread_post(f_groupid, f_author, f_client_postid, f_content, f_create_time, f_edit_time, "
          "f_active_time, f_comment_count, f_pin_time, f_cardmsgid) VALUES("
       << post.groupid << ", " << post.author << ", '" << escapeText(pConn.get(), post.clientPostId) << "', '"
       << escapeText(pConn.get(), ThreadProtocol::bodyToJson(post.body)) << "', " << post.createTime << ", 0, "
       << post.activeTime << ", 0, 0, 0)";
    if (!exec(pConn.get(), os.str(), "insertPost"))
        return false;
    //自增 ID 是连接私有的状态，必须在执行 INSERT 的同一条连接上读取
    post.postid = static_cast<int64_t>(pConn->getInsertID());
    if (post.postid <= 0)
    {
        LOGE("ThreadManager::insertPost got no insert id, groupid: %lld", (long long)post.groupid);
        return false;
    }
    return true;
}

bool ThreadManager::getPost(int64_t postid, ThreadPost& out, bool& found)
{
    found = false;
    std::unique_ptr<IDatabase> pConn(openConnection("getPost"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_POST_COLUMNS " FROM t_thread_post WHERE f_postid = " << postid;
    std::vector<ThreadPost> rows;
    if (!queryRows(pConn.get(), os.str(), rows, readPostRow, "getPost"))
        return false;
    if (!rows.empty())
    {
        out = rows[0];
        found = true;
    }
    return true;
}

bool ThreadManager::updatePostBody(int64_t postid, const ThreadBody& body, int64_t editTime)
{
    std::unique_ptr<IDatabase> pConn(openConnection("updatePostBody"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "UPDATE t_thread_post SET f_content = '" << escapeText(pConn.get(), ThreadProtocol::bodyToJson(body))
       << "', f_edit_time = " << editTime << " WHERE f_postid = " << postid;
    return exec(pConn.get(), os.str(), "updatePostBody");
}

bool ThreadManager::setPostCardMsgId(int64_t postid, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setPostCardMsgId"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "UPDATE t_thread_post SET f_cardmsgid = " << msgid << " WHERE f_postid = " << postid;
    return exec(pConn.get(), os.str(), "setPostCardMsgId");
}

bool ThreadManager::setPostPin(int64_t postid, int64_t pinTime)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setPostPin"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "UPDATE t_thread_post SET f_pin_time = " << pinTime << " WHERE f_postid = " << postid;
    return exec(pConn.get(), os.str(), "setPostPin");
}

bool ThreadManager::countPins(int64_t groupid, int& count)
{
    count = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("countPins"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_thread_post WHERE f_groupid = " << groupid << " AND f_pin_time > 0";
    int64_t value = 0;
    if (!queryInt64(pConn.get(), os.str(), value, "countPins"))
        return false;
    count = static_cast<int>(value);
    return true;
}

bool ThreadManager::deletePost(int64_t postid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("deletePost"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("ThreadManager::deletePost begin transaction error, postid: %lld", (long long)postid);
        return false;
    }
    std::vector<std::string> statements;
    {
        //留言与回复上的互动：先删，删留言之后就找不到它们的 ID 了
        std::ostringstream os;
        os << "DELETE FROM t_thread_reaction WHERE f_target_type = " << thread_target_comment
           << " AND f_target_id IN (SELECT f_commentid FROM t_thread_comment WHERE f_postid = " << postid << ")";
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_reaction WHERE f_target_type = " << thread_target_post << " AND f_target_id = "
           << postid;
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_comment WHERE f_postid = " << postid;
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_addendum WHERE f_postid = " << postid;
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_post WHERE f_postid = " << postid;
        statements.push_back(os.str());
    }
    for (size_t i = 0; i < statements.size(); ++i)
    {
        if (!exec(pConn.get(), statements[i], "deletePost"))
            return false;
    }
    if (!transaction.Commit())
    {
        LOGE("ThreadManager::deletePost commit error, postid: %lld", (long long)postid);
        return false;
    }
    return true;
}

bool ThreadManager::listPosts(int64_t groupid, int sort, const std::string& cursorIn, int limit,
                              std::vector<ThreadPost>& out, std::string& nextCursor, bool& hasMore)
{
    //先复制游标：调用方可能把同一个字符串既当入参又当出参 nextCursor 传进来，下面清空出参时会连带清掉它
    const std::string cursor = cursorIn;
    out.clear();
    nextCursor.clear();
    hasMore = false;
    std::unique_ptr<IDatabase> pConn(openConnection("listPosts"));
    if (!pConn)
        return false;

    //解析游标；格式不对按第一页处理
    int64_t cursorTime = 0;
    int64_t cursorPost = 0;
    bool firstPage = true;
    if (sort == thread_sort_publish)
        firstPage = !ThreadProtocol::parsePublishCursor(cursor, cursorPost);
    else
        firstPage = !ThreadProtocol::parseActivityCursor(cursor, cursorTime, cursorPost);

    //第一页：全部置顶帖在最前
    if (firstPage)
    {
        std::ostringstream os;
        os << "SELECT " THREAD_POST_COLUMNS " FROM t_thread_post WHERE f_groupid = " << groupid
           << " AND f_pin_time > 0 ORDER BY f_pin_time DESC";
        if (!queryRows(pConn.get(), os.str(), out, readPostRow, "listPosts"))
            return false;
    }

    //非置顶帖：多取一条判断有没有下一页
    std::ostringstream os;
    os << "SELECT " THREAD_POST_COLUMNS " FROM t_thread_post WHERE f_groupid = " << groupid << " AND f_pin_time = 0";
    if (sort == thread_sort_publish)
    {
        if (!firstPage)
            os << " AND f_postid < " << cursorPost;
        os << " ORDER BY f_postid DESC";
    }
    else
    {
        if (!firstPage)
        {
            os << " AND (f_active_time < " << cursorTime << " OR (f_active_time = " << cursorTime
               << " AND f_postid < " << cursorPost << "))";
        }
        os << " ORDER BY f_active_time DESC, f_postid DESC";
    }
    os << " LIMIT " << (limit + 1);
    std::vector<ThreadPost> page;
    if (!queryRows(pConn.get(), os.str(), page, readPostRow, "listPosts"))
        return false;
    if (static_cast<int>(page.size()) > limit)
    {
        hasMore = true;
        page.resize(static_cast<size_t>(limit));
    }
    if (hasMore && !page.empty())
    {
        const ThreadPost& last = page[page.size() - 1];
        nextCursor = (sort == thread_sort_publish) ? ThreadProtocol::makePublishCursor(last.postid)
                                                   : ThreadProtocol::makeActivityCursor(last.activeTime, last.postid);
    }
    out.insert(out.end(), page.begin(), page.end());
    return true;
}

bool ThreadManager::getPostCounters(int64_t postid, int& commentCount, int64_t& activeTime)
{
    commentCount = 0;
    activeTime = 0;
    ThreadPost post;
    bool found = false;
    if (!getPost(postid, post, found))
        return false;
    if (found)
    {
        commentCount = post.commentCount;
        activeTime = post.activeTime;
    }
    return true;
}

//======================== 楼主补充 ========================

bool ThreadManager::insertAddendum(ThreadAddendum& a)
{
    std::unique_ptr<IDatabase> pConn(openConnection("insertAddendum"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("ThreadManager::insertAddendum begin transaction error, postid: %lld", (long long)a.postid);
        return false;
    }
    std::ostringstream os;
    os << "INSERT INTO t_thread_addendum(f_postid, f_groupid, f_author, f_content, f_create_time) VALUES(" << a.postid
       << ", " << a.groupid << ", " << a.author << ", '" << escapeText(pConn.get(), ThreadProtocol::bodyToJson(a.body))
       << "', " << a.createTime << ")";
    if (!exec(pConn.get(), os.str(), "insertAddendum"))
        return false;
    a.addid = static_cast<int64_t>(pConn->getInsertID());
    if (a.addid <= 0)
    {
        LOGE("ThreadManager::insertAddendum got no insert id, postid: %lld", (long long)a.postid);
        return false;
    }
    std::ostringstream osTouch;
    osTouch << "UPDATE t_thread_post SET f_active_time = " << a.createTime << " WHERE f_postid = " << a.postid;
    if (!exec(pConn.get(), osTouch.str(), "insertAddendum"))
        return false;
    if (!transaction.Commit())
    {
        LOGE("ThreadManager::insertAddendum commit error, postid: %lld", (long long)a.postid);
        a.addid = 0;
        return false;
    }
    return true;
}

bool ThreadManager::getAddendum(int64_t addid, ThreadAddendum& out, bool& found)
{
    found = false;
    std::unique_ptr<IDatabase> pConn(openConnection("getAddendum"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_ADDENDUM_COLUMNS " FROM t_thread_addendum WHERE f_addid = " << addid;
    std::vector<ThreadAddendum> rows;
    if (!queryRows(pConn.get(), os.str(), rows, readAddendumRow, "getAddendum"))
        return false;
    if (!rows.empty())
    {
        out = rows[0];
        found = true;
    }
    return true;
}

bool ThreadManager::deleteAddendum(int64_t addid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("deleteAddendum"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "DELETE FROM t_thread_addendum WHERE f_addid = " << addid;
    return exec(pConn.get(), os.str(), "deleteAddendum");
}

bool ThreadManager::listAddenda(int64_t postid, std::vector<ThreadAddendum>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("listAddenda"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_ADDENDUM_COLUMNS " FROM t_thread_addendum WHERE f_postid = " << postid
       << " ORDER BY f_addid ASC";
    return queryRows(pConn.get(), os.str(), out, readAddendumRow, "listAddenda");
}

bool ThreadManager::countAddenda(const std::vector<int64_t>& postids, std::map<int64_t, int>& out)
{
    out.clear();
    if (postids.empty())
        return true;
    std::unique_ptr<IDatabase> pConn(openConnection("countAddenda"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT f_postid, COUNT(*) FROM t_thread_addendum WHERE f_postid IN (" << joinIds(postids)
       << ") GROUP BY f_postid";
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::countAddenda query error");
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out[static_cast<int64_t>(pRow[0].getUInt64())] = pRow[1].getInt32();
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ThreadManager::setAddendumCardMsgId(int64_t addid, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setAddendumCardMsgId"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "UPDATE t_thread_addendum SET f_cardmsgid = " << msgid << " WHERE f_addid = " << addid;
    return exec(pConn.get(), os.str(), "setAddendumCardMsgId");
}

//======================== 留言与回复 ========================

bool ThreadManager::findCommentByClientId(int64_t author, const std::string& clientId, int64_t& commentid)
{
    commentid = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("findCommentByClientId"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT f_commentid FROM t_thread_comment WHERE f_author = " << author << " AND f_client_commentid = '"
       << escapeText(pConn.get(), clientId) << "' LIMIT 1";
    return queryInt64(pConn.get(), os.str(), commentid, "findCommentByClientId");
}

bool ThreadManager::insertComment(ThreadComment& c)
{
    std::unique_ptr<IDatabase> pConn(openConnection("insertComment"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("ThreadManager::insertComment begin transaction error, postid: %lld", (long long)c.postid);
        return false;
    }
    std::ostringstream os;
    os << "INSERT INTO t_thread_comment(f_postid, f_groupid, f_author, f_parentid, f_reply_to, f_client_commentid, "
          "f_content, f_create_time, f_deleted, f_reply_count) VALUES("
       << c.postid << ", " << c.groupid << ", " << c.author << ", " << c.parentid << ", " << c.replyTo << ", '"
       << escapeText(pConn.get(), c.clientCommentId) << "', '"
       << escapeText(pConn.get(), ThreadProtocol::bodyToJson(c.body)) << "', " << c.createTime << ", 0, 0)";
    if (!exec(pConn.get(), os.str(), "insertComment"))
        return false;
    c.commentid = static_cast<int64_t>(pConn->getInsertID());
    if (c.commentid <= 0)
    {
        LOGE("ThreadManager::insertComment got no insert id, postid: %lld", (long long)c.postid);
        return false;
    }
    std::ostringstream osPost;
    osPost << "UPDATE t_thread_post SET f_comment_count = f_comment_count + 1, f_active_time = " << c.createTime
           << " WHERE f_postid = " << c.postid;
    if (!exec(pConn.get(), osPost.str(), "insertComment"))
        return false;
    if (c.parentid > 0)
    {
        std::ostringstream osParent;
        osParent << "UPDATE t_thread_comment SET f_reply_count = f_reply_count + 1 WHERE f_commentid = " << c.parentid;
        if (!exec(pConn.get(), osParent.str(), "insertComment"))
            return false;
    }
    if (!transaction.Commit())
    {
        LOGE("ThreadManager::insertComment commit error, postid: %lld", (long long)c.postid);
        c.commentid = 0;
        return false;
    }
    return true;
}

bool ThreadManager::getComment(int64_t commentid, ThreadComment& out, bool& found)
{
    found = false;
    std::unique_ptr<IDatabase> pConn(openConnection("getComment"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_COMMENT_COLUMNS " FROM t_thread_comment WHERE f_commentid = " << commentid;
    std::vector<ThreadComment> rows;
    if (!queryRows(pConn.get(), os.str(), rows, readCommentRow, "getComment"))
        return false;
    if (!rows.empty())
    {
        out = rows[0];
        found = true;
    }
    return true;
}

bool ThreadManager::deleteComment(const ThreadComment& c, bool& keptPlaceholder)
{
    keptPlaceholder = false;
    std::unique_ptr<IDatabase> pConn(openConnection("deleteComment"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("ThreadManager::deleteComment begin transaction error, commentid: %lld", (long long)c.commentid);
        return false;
    }
    std::vector<std::string> statements;
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_reaction WHERE f_target_type = " << thread_target_comment << " AND f_target_id = "
           << c.commentid;
        statements.push_back(os.str());
    }
    if (c.parentid == 0 && c.replyCount > 0)
    {
        //留言下面还有回复：保留占位，清空内容
        keptPlaceholder = true;
        std::ostringstream os;
        os << "UPDATE t_thread_comment SET f_deleted = 1, f_content = '{}' WHERE f_commentid = " << c.commentid;
        statements.push_back(os.str());
    }
    else
    {
        std::ostringstream os;
        os << "DELETE FROM t_thread_comment WHERE f_commentid = " << c.commentid;
        statements.push_back(os.str());
    }
    //已经是占位的留言不再计入总数，删的时候也就不必再减
    if (!c.deleted)
    {
        std::ostringstream os;
        os << "UPDATE t_thread_post SET f_comment_count = f_comment_count - 1 WHERE f_postid = " << c.postid
           << " AND f_comment_count > 0";
        statements.push_back(os.str());
    }
    if (c.parentid > 0)
    {
        std::ostringstream os;
        os << "UPDATE t_thread_comment SET f_reply_count = f_reply_count - 1 WHERE f_commentid = " << c.parentid
           << " AND f_reply_count > 0";
        statements.push_back(os.str());
        //所属留言是占位、且这是它最后一条回复：占位一并删除
        std::ostringstream osClean;
        osClean << "DELETE FROM t_thread_comment WHERE f_commentid = " << c.parentid
                << " AND f_deleted = 1 AND f_reply_count = 0";
        statements.push_back(osClean.str());
    }
    for (size_t i = 0; i < statements.size(); ++i)
    {
        if (!exec(pConn.get(), statements[i], "deleteComment"))
            return false;
    }
    if (!transaction.Commit())
    {
        LOGE("ThreadManager::deleteComment commit error, commentid: %lld", (long long)c.commentid);
        return false;
    }
    return true;
}

bool ThreadManager::listComments(int64_t postid, int64_t cursor, int limit, std::vector<ThreadComment>& out,
                                 int64_t& nextCursor, bool& hasMore)
{
    out.clear();
    nextCursor = 0;
    hasMore = false;
    std::unique_ptr<IDatabase> pConn(openConnection("listComments"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_COMMENT_COLUMNS " FROM t_thread_comment WHERE f_postid = " << postid
       << " AND f_parentid = 0 AND f_commentid > " << cursor << " ORDER BY f_commentid ASC LIMIT " << (limit + 1);
    if (!queryRows(pConn.get(), os.str(), out, readCommentRow, "listComments"))
        return false;
    if (static_cast<int>(out.size()) > limit)
    {
        hasMore = true;
        out.resize(static_cast<size_t>(limit));
    }
    if (hasMore && !out.empty())
        nextCursor = out[out.size() - 1].commentid;

    //每条留言顺带前几条回复：条数很少，逐条查
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].replyCount <= 0)
            continue;
        std::ostringstream osReplies;
        osReplies << "SELECT " THREAD_COMMENT_COLUMNS " FROM t_thread_comment WHERE f_parentid = " << out[i].commentid
                  << " ORDER BY f_commentid ASC LIMIT " << kThreadPreviewReplies;
        if (!queryRows(pConn.get(), osReplies.str(), out[i].previewReplies, readCommentRow, "listComments"))
            return false;
    }
    return true;
}

bool ThreadManager::listReplies(int64_t parentid, int64_t cursor, int limit, std::vector<ThreadComment>& out,
                                int64_t& nextCursor, bool& hasMore)
{
    out.clear();
    nextCursor = 0;
    hasMore = false;
    std::unique_ptr<IDatabase> pConn(openConnection("listReplies"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT " THREAD_COMMENT_COLUMNS " FROM t_thread_comment WHERE f_parentid = " << parentid
       << " AND f_commentid > " << cursor << " ORDER BY f_commentid ASC LIMIT " << (limit + 1);
    if (!queryRows(pConn.get(), os.str(), out, readCommentRow, "listReplies"))
        return false;
    if (static_cast<int>(out.size()) > limit)
    {
        hasMore = true;
        out.resize(static_cast<size_t>(limit));
    }
    if (hasMore && !out.empty())
        nextCursor = out[out.size() - 1].commentid;
    return true;
}

bool ThreadManager::commentersOfPost(int64_t postid, std::vector<int64_t>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("commentersOfPost"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT DISTINCT f_author FROM t_thread_comment WHERE f_postid = " << postid;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::commentersOfPost query error, postid: %lld", (long long)postid);
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out.push_back(static_cast<int64_t>(pRow[0].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ThreadManager::setCommentCardMsgId(int64_t commentid, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setCommentCardMsgId"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "UPDATE t_thread_comment SET f_cardmsgid = " << msgid << " WHERE f_commentid = " << commentid;
    return exec(pConn.get(), os.str(), "setCommentCardMsgId");
}

bool ThreadManager::replyCardMsgIdsOfPost(int64_t postid, std::vector<int64_t>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn(openConnection("replyCardMsgIdsOfPost"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT f_cardmsgid FROM t_thread_comment WHERE f_postid = " << postid << " AND f_cardmsgid > 0"
       << " UNION ALL SELECT f_cardmsgid FROM t_thread_addendum WHERE f_postid = " << postid << " AND f_cardmsgid > 0";
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::replyCardMsgIdsOfPost query error, postid: %lld", (long long)postid);
        return false;
    }
    std::set<int64_t> ids;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        ids.insert(static_cast<int64_t>(pRow[0].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    out.assign(ids.begin(), ids.end());
    return true;
}

//======================== 互动 ========================

bool ThreadManager::setReaction(int targetType, int64_t targetId, int64_t groupid, int64_t userId, int reaction,
                                bool on, int64_t now)
{
    std::unique_ptr<IDatabase> pConn(openConnection("setReaction"));
    if (!pConn)
        return false;
    std::ostringstream os;
    if (on)
    {
        //已存在时不变（主键即四元组），SQLite 下由翻译层把 INSERT IGNORE 改写为 INSERT OR IGNORE
        os << "INSERT IGNORE INTO t_thread_reaction(f_target_type, f_target_id, f_userid, f_reaction, f_groupid, "
              "f_create_time) VALUES("
           << targetType << ", " << targetId << ", " << userId << ", " << reaction << ", " << groupid << ", " << now
           << ")";
    }
    else
    {
        os << "DELETE FROM t_thread_reaction WHERE f_target_type = " << targetType << " AND f_target_id = " << targetId
           << " AND f_userid = " << userId << " AND f_reaction = " << reaction;
    }
    return exec(pConn.get(), os.str(), "setReaction");
}

bool ThreadManager::loadReactions(int targetType, const std::vector<int64_t>& targetIds, int64_t viewerId,
                                  std::map<int64_t, std::vector<ThreadReaction> >& out)
{
    out.clear();
    if (targetIds.empty())
        return true;
    std::unique_ptr<IDatabase> pConn(openConnection("loadReactions"));
    if (!pConn)
        return false;
    std::ostringstream os;
    os << "SELECT f_target_id, f_reaction, COUNT(*), SUM(CASE WHEN f_userid = " << viewerId
       << " THEN 1 ELSE 0 END) FROM t_thread_reaction WHERE f_target_type = " << targetType << " AND f_target_id IN ("
       << joinIds(targetIds) << ") GROUP BY f_target_id, f_reaction ORDER BY f_target_id, f_reaction";
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::loadReactions query error, targettype: %d", targetType);
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        ThreadReaction r;
        r.reaction = pRow[1].getInt32();
        r.count = pRow[2].getInt32();
        r.mine = pRow[3].getInt32() > 0;
        out[static_cast<int64_t>(pRow[0].getUInt64())].push_back(r);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ThreadManager::listReactors(int targetType, int64_t targetId, int reaction, int limit,
                                 std::vector<int64_t>& users, int& total)
{
    users.clear();
    total = 0;
    std::unique_ptr<IDatabase> pConn(openConnection("listReactors"));
    if (!pConn)
        return false;
    std::ostringstream where;
    where << " FROM t_thread_reaction WHERE f_target_type = " << targetType << " AND f_target_id = " << targetId
          << " AND f_reaction = " << reaction;
    int64_t count = 0;
    if (!queryInt64(pConn.get(), "SELECT COUNT(*)" + where.str(), count, "listReactors"))
        return false;
    total = static_cast<int>(count);
    std::ostringstream os;
    os << "SELECT f_userid" << where.str() << " ORDER BY f_create_time ASC LIMIT " << limit;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("ThreadManager::listReactors query error, targetid: %lld", (long long)targetId);
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        users.push_back(static_cast<int64_t>(pRow[0].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}
