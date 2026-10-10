/**
 *  帖子群的存储管理，ThreadManager.h
 *
 *  读写 t_thread_post / t_thread_addendum / t_thread_comment / t_thread_reaction 四张表（docs/帖子群方案.md 第 5 节）。
 *  同步访问数据库：每个操作经 DbFactory::createConnection() 取一条连接，与 MemoManager 相同；涉及多张表或计数列的写操作
 *  在一个事务里完成。内容 JSON 的编解码与各项规则在 ThreadProtocol 里，本类只负责存取。
 *
 *  典型用法（ChatSessionThread.cpp）：
 *      ThreadManager& mgr = Singleton<ThreadManager>::Instance();
 *      ThreadPost post;  ...填 groupid、author、clientPostId、body、各时间...
 *      if (mgr.insertPost(post)) { ...post.postid 已回填... }
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>
#include "ThreadProtocol.h"

/**
 *  帖子群的存储管理。chatserver 内经 Singleton<ThreadManager>::Instance() 使用；无状态，测试里可直接构造。
 *  所有返回 bool 的方法：数据库访问成功返回 true，失败返回 false 并已写日志。
 */
class ThreadManager
{
public:
    //======== 帖子 ========

    /**
     *  按「作者 + 客户端去重标识」查已有的帖子（发帖应答超时后客户端重发时去重）。
     *  @param author       作者 userid
     *  @param clientPostId 去重标识
     *  @param postid       [出参] 命中时为帖子 ID，否则为 0
     */
    bool findPostByClientId(int64_t author, const std::string& clientPostId, int64_t& postid);

    /**
     *  新增一条帖子。
     *  @param post [入出参] 入参用 groupid、author、clientPostId、body、createTime、activeTime；成功后回填 postid
     */
    bool insertPost(ThreadPost& post);

    /**
     *  读一条帖子（不含互动汇总与补充）。
     *  @param postid 帖子 ID
     *  @param out    [出参] 帖子
     *  @param found  [出参] 是否存在
     */
    bool getPost(int64_t postid, ThreadPost& out, bool& found);

    /**
     *  修改帖子内容，并记下修改时间。
     */
    bool updatePostBody(int64_t postid, const ThreadBody& body, int64_t editTime);

    /**
     *  记下帖子卡片的 msgid。
     */
    bool setPostCardMsgId(int64_t postid, int64_t msgid);

    /**
     *  置顶或取消置顶。
     *  @param pinTime 置顶时间；0 表示取消置顶
     */
    bool setPostPin(int64_t postid, int64_t pinTime);

    /**
     *  统计一个帖子群当前置顶的帖子数。
     */
    bool countPins(int64_t groupid, int& count);

    /**
     *  删除一条帖子，连同它的补充、留言与回复、全部互动，在一个事务里完成。
     */
    bool deletePost(int64_t postid);

    /**
     *  拉一页帖子（不含互动汇总与补充数）。第一页（cursor 为空）另带全部置顶帖（按置顶时间倒序，排在最前，不计入 limit）。
     *  @param groupid    帖子群
     *  @param sort       排序，取值见 thread_sort
     *  @param cursor     游标；空为第一页；格式不对按第一页处理
     *  @param limit      非置顶帖的条数
     *  @param out        [出参] 帖子
     *  @param nextCursor [出参] 下一页游标；没有更多时为空
     *  @param hasMore    [出参] 是否还有更多
     */
    bool listPosts(int64_t groupid, int sort, const std::string& cursor, int limit, std::vector<ThreadPost>& out,
                   std::string& nextCursor, bool& hasMore);

    /**
     *  读帖子当前的留言与回复总数与最新动态时间。
     */
    bool getPostCounters(int64_t postid, int& commentCount, int64_t& activeTime);

    //======== 楼主补充 ========

    /**
     *  新增一段补充，并把帖子的最新动态时间刷新为补充时间，在一个事务里完成。
     *  @param a [入出参] 入参用 postid、groupid、author、body、createTime；成功后回填 addid
     */
    bool insertAddendum(ThreadAddendum& a);

    /**
     *  读一段补充。
     */
    bool getAddendum(int64_t addid, ThreadAddendum& out, bool& found);

    /**
     *  删除一段补充（真删）。
     */
    bool deleteAddendum(int64_t addid);

    /**
     *  按时间先后读一条帖子的全部补充。
     */
    bool listAddenda(int64_t postid, std::vector<ThreadAddendum>& out);

    /**
     *  统计一批帖子各自的补充段数。没有补充的帖子不出现在 out 里。
     */
    bool countAddenda(const std::vector<int64_t>& postids, std::map<int64_t, int>& out);

    /**
     *  记下一段补充的动态卡片 msgid（写卡片之后调用；删除补充时据此把卡片改写为已删除形态，docs/帖子群方案.md 14.6）。
     */
    bool setAddendumCardMsgId(int64_t addid, int64_t msgid);

    //======== 留言与回复 ========

    /**
     *  按「作者 + 客户端去重标识」查已有的留言或回复。
     */
    bool findCommentByClientId(int64_t author, const std::string& clientId, int64_t& commentid);

    /**
     *  新增一条留言或回复：同一个事务里把帖子的留言总数加一、最新动态时间刷新为 createTime，回复时再把所属留言的回复数加一。
     *  @param c [入出参] 入参用 postid、groupid、author、parentid、replyTo、clientCommentId、body、createTime；
     *           成功后回填 commentid
     */
    bool insertComment(ThreadComment& c);

    /**
     *  读一条留言或回复（不含互动汇总）。
     */
    bool getComment(int64_t commentid, ThreadComment& out, bool& found);

    /**
     *  删除一条留言或回复，同一个事务里维护计数并删除它的互动：
     *    - 留言下面还有回复：只置已删除标志、清空内容，保留占位；
     *    - 其余情况：直接删除；删掉的是回复且所属留言是没有其余回复的占位时，占位一并删除。
     *  @param c               要删除的那一条（由 getComment 读出）
     *  @param keptPlaceholder [出参] 是否只置了已删除标志
     */
    bool deleteComment(const ThreadComment& c, bool& keptPlaceholder);

    /**
     *  拉一页留言（按 ID 升序），每条带前 kThreadPreviewReplies 条回复。
     *  @param cursor 上一页最后一条留言的 ID；0 为第一页
     */
    bool listComments(int64_t postid, int64_t cursor, int limit, std::vector<ThreadComment>& out, int64_t& nextCursor,
                      bool& hasMore);

    /**
     *  拉一页回复（按 ID 升序）。
     *  @param parentid 所属留言
     *  @param cursor   上一页最后一条回复的 ID；0 为第一页
     */
    bool listReplies(int64_t parentid, int64_t cursor, int limit, std::vector<ThreadComment>& out, int64_t& nextCursor,
                     bool& hasMore);

    /**
     *  在一条帖子下留过言或回复过的人（去重）。楼主补充时据此决定提醒谁。
     */
    bool commentersOfPost(int64_t postid, std::vector<int64_t>& out);

    /**
     *  记下一条留言或回复的动态卡片 msgid（写卡片之后调用；删除时据此改写卡片，docs/帖子群方案.md 14.6）。
     */
    bool setCommentCardMsgId(int64_t commentid, int64_t msgid);

    /**
     *  一条帖子下全部留言、回复与补充的动态卡片 msgid（大于 0 的，升序）。删帖之前调用：删帖会删掉这些行，
     *  之后就查不到了。
     */
    bool replyCardMsgIdsOfPost(int64_t postid, std::vector<int64_t>& out);

    //======== 互动 ========

    /**
     *  加上或取消一种互动。加上时已存在则不变，取消时不存在也算成功。
     */
    bool setReaction(int targetType, int64_t targetId, int64_t groupid, int64_t userId, int reaction, bool on,
                     int64_t now);

    /**
     *  读一批目标的互动汇总（按互动编号升序，「赞」在最前），mine 按 viewerId 计算。
     *  没有任何互动的目标不出现在 out 里。
     */
    bool loadReactions(int targetType, const std::vector<int64_t>& targetIds, int64_t viewerId,
                       std::map<int64_t, std::vector<ThreadReaction> >& out);

    /**
     *  列出对某个目标加了某种互动的人（按时间先后），并给出总人数。
     */
    bool listReactors(int targetType, int64_t targetId, int reaction, int limit, std::vector<int64_t>& users,
                      int& total);
};
