/**
 *  帖子详情模型（docs/帖子群方案.md 4.3、4.6 右栏）：保存当前选中的那条帖子的完整内容、楼主的各段补充，
 *  以及留言区（留言与它们下面的回复，两级），合并 2702 / 2707 / 2708 的应答、本人操作的应答与 2720 推送。
 *  纯逻辑，不触及界面与网络。
 *
 *  规则（与服务端 ThreadManager 一致）：
 *    - 补充按 ID（即时间）升序排在正文之后；
 *    - 留言按 ID 升序；每条留言下的回复也按 ID 升序，留言列表顺带前 3 条，其余经「查看更多回复」拉取；
 *    - 删除留言：下面还有回复时保留占位（显示「该留言已删除」），没有回复时直接去掉；
 *      删掉占位留言下的最后一条回复时，占位一并去掉（服务端同时删除了那条占位）；
 *    - 同一条留言、回复、补充经应答与推送各到一次时只算一次（按 ID 去重）。
 *
 *  典型用法：
 *      CThreadDetailModel detail;
 *      detail.Select(postId, feed.Find(postId));
 *      detail.ApplyPost(fullPost);
 *      detail.ApplyComments(comments, next, more, true);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "ThreadTypes.h"

namespace threadgroup {

/**
 *  CThreadDetailModel —— 帖子详情模型。只在界面线程上使用。
 */
class CThreadDetailModel
{
public:
    CThreadDetailModel();

    /**
     *  选中一条帖子：清空上一条的内容。
     *  @param postId  帖子 ID；0 表示不选中任何帖子
     *  @param summary 帖子列表里的摘要（可为 NULL），完整内容到达之前先用它显示
     */
    void Select(int64_t postId, const ThreadPost* summary);

    //当前选中的帖子 ID；没有选中为 0
    int64_t PostId() const;

    //帖子（完整内容到达前为摘要形态）
    const ThreadPost& Post() const;

    //是否已经收到完整内容
    bool IsPostLoaded() const;

    //选中的帖子已被删除（看着的时候被删，或拉取时服务端回「不存在」）
    bool IsDeleted() const;

    //标记选中的帖子已被删除
    void MarkDeleted();

    /**
     *  收到完整帖子（2702 / 2704 的应答）。ID 与当前选中的不同时忽略。
     *  @return 已采用返回 true
     */
    bool ApplyPost(const ThreadPost& post);

    /**
     *  标记开始拉一页留言；正在拉时返回 false。
     */
    bool BeginLoadComments();

    //拉留言失败：清掉「正在拉」
    void LoadCommentsFailed();

    //是否正在拉留言
    bool IsLoadingComments() const;

    //是否已经收到过第一页留言
    bool IsCommentsLoaded() const;

    //留言是否还有下一页
    bool HasMoreComments() const;

    //下一页留言的游标（已加载的最后一条留言 ID）
    int64_t NextCommentCursor() const;

    /**
     *  合并一页留言。
     *  @param postId    应答所属的帖子（与当前选中的不同时忽略）
     *  @param firstPage 是否第一页：是则替换全部
     */
    void ApplyComments(int64_t postId, const std::vector<ThreadComment>& comments, int64_t nextCursor, bool hasMore,
                       bool firstPage);

    /**
     *  标记开始拉某条留言的更多回复；留言不存在或正在拉时返回 false。
     *  @param cursor [出参] 请求用的游标
     */
    bool BeginLoadReplies(int64_t commentId, int64_t& cursor);

    //拉回复失败：清掉那条留言的「正在拉」
    void LoadRepliesFailed(int64_t commentId);

    /**
     *  合并某条留言下的一页回复（按 ID 去重、升序）。
     */
    void ApplyReplies(int64_t commentId, const std::vector<ThreadComment>& replies);

    /**
     *  加入一条留言或回复（本人留言的应答，或 2720 推送）。不属于当前帖子时忽略。
     *  @return 有变化返回 true
     */
    bool AddComment(const ThreadComment& comment);

    /**
     *  去掉一条留言或回复。
     *  @param commentId   ID
     *  @param placeholder 服务端保留了占位时为占位留言（deleted 为 1），否则为 NULL；
     *                     为 NULL 而这条留言下面还有回复时，本地同样保留占位
     *  @return 有变化返回 true
     */
    bool RemoveComment(int64_t commentId, const ThreadComment* placeholder);

    /**
     *  加入一段补充（按 ID 去重、升序）。不属于当前帖子时忽略。
     *  @return 有变化返回 true
     */
    bool AddAddendum(const ThreadAddendum& addendum);

    /**
     *  去掉一段补充。
     *  @return 有变化返回 true
     */
    bool RemoveAddendum(int64_t addId);

    /**
     *  本人点了互动后立即更新汇总，返回点之前的汇总供失败时恢复。
     *  @param targetType 目标类型，取值见 ThreadTargetType
     *  @return 目标在详情里返回 true
     */
    bool ToggleReactionLocal(int targetType, int64_t targetId, int reaction, bool on,
                             std::vector<ThreadReaction>& before);

    //用 2711 应答（或失败时的恢复值）替换目标的互动汇总
    void SetReactions(int targetType, int64_t targetId, const std::vector<ThreadReaction>& reactions);

    /**
     *  合并一条 2720 推送。
     *  @return 详情有变化（需要重画）返回 true
     */
    bool ApplyChange(const ThreadChange& change, int64_t myId);

    //留言列表（按 ID 升序，回复挂在各自的留言下）
    const std::vector<ThreadComment>& Comments() const;

    //按 ID 找留言或回复；没有返回 NULL
    const ThreadComment* FindComment(int64_t commentId) const;

    /**
     *  一条留言还有多少条回复没加载（回复总数减去已加载的条数，不小于 0）。
     */
    static int UnloadedReplies(const ThreadComment& comment);

private:
    //按 ID 找留言或回复（可修改）；parentIndex 返回所属留言的下标，本身是留言时为它自己的下标
    ThreadComment* FindMutable(int64_t commentId, int& parentIndex, bool& isReply);

private:
    int64_t                    m_postId;          //当前选中的帖子；0 表示没有
    ThreadPost                 m_post;            //帖子（完整内容到达前为摘要形态）
    bool                       m_postLoaded;      //是否已经收到完整内容
    bool                       m_deleted;         //选中的帖子已被删除
    std::vector<ThreadComment> m_comments;        //留言（按 ID 升序），回复挂在 m_replies 里
    int64_t                    m_commentCursor;   //下一页留言的游标
    bool                       m_hasMoreComments; //留言是否还有下一页
    bool                       m_commentsLoading; //是否正在拉留言
    bool                       m_commentsLoaded;  //是否已经收到过第一页留言
};

} // namespace threadgroup
