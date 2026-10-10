/**
 *  帖子详情模型 CThreadDetailModel 的实现，见 ThreadDetailModel.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadDetailModel.h"

#include <algorithm>

#include "ThreadReactions.h"

namespace threadgroup {

namespace {

//留言、回复按 ID 升序
bool CommentOrder(const ThreadComment& a, const ThreadComment& b)
{
    return a.m_commentId < b.m_commentId;
}

//补充按 ID 升序
bool AddendumOrder(const ThreadAddendum& a, const ThreadAddendum& b)
{
    return a.m_addId < b.m_addId;
}

//把 incoming 按 ID 去重并入 list（已有的整条替换），再按 ID 升序排列
void MergeComments(std::vector<ThreadComment>& list, const std::vector<ThreadComment>& incoming)
{
    for (size_t i = 0; i < incoming.size(); ++i)
    {
        bool replaced = false;
        for (size_t j = 0; j < list.size(); ++j)
        {
            if (list[j].m_commentId == incoming[i].m_commentId)
            {
                list[j] = incoming[i];
                replaced = true;
                break;
            }
        }
        if (!replaced)
            list.push_back(incoming[i]);
    }
    std::stable_sort(list.begin(), list.end(), CommentOrder);
}

//回复游标：已加载的最后一条回复 ID
void UpdateReplyCursor(ThreadComment& comment)
{
    comment.m_replyCursor = comment.m_replies.empty() ? 0 : comment.m_replies[comment.m_replies.size() - 1].m_commentId;
}

//把留言改成「已删除」的占位
void MakePlaceholder(ThreadComment& comment)
{
    comment.m_deleted = true;
    comment.m_text.clear();
    comment.m_mentions.clear();
    comment.m_reactions.clear();
}

} // namespace

CThreadDetailModel::CThreadDetailModel()
    : m_postId(0)
    , m_postLoaded(false)
    , m_deleted(false)
    , m_commentCursor(0)
    , m_hasMoreComments(false)
    , m_commentsLoading(false)
    , m_commentsLoaded(false)
{
}

void CThreadDetailModel::Select(int64_t postId, const ThreadPost* summary)
{
    m_postId = postId;
    m_post = (summary != NULL && summary->m_postId == postId) ? *summary : ThreadPost();
    m_post.m_postId = postId;
    m_post.m_full = false;
    m_post.m_addenda.clear();
    m_postLoaded = false;
    m_deleted = false;
    m_comments.clear();
    m_commentCursor = 0;
    m_hasMoreComments = false;
    m_commentsLoading = false;
    m_commentsLoaded = false;
}

int64_t CThreadDetailModel::PostId() const
{
    return m_postId;
}

const ThreadPost& CThreadDetailModel::Post() const
{
    return m_post;
}

bool CThreadDetailModel::IsPostLoaded() const
{
    return m_postLoaded;
}

bool CThreadDetailModel::IsDeleted() const
{
    return m_deleted;
}

void CThreadDetailModel::MarkDeleted()
{
    if (m_postId > 0)
        m_deleted = true;
}

bool CThreadDetailModel::ApplyPost(const ThreadPost& post)
{
    if (m_postId <= 0 || post.m_postId != m_postId)
        return false;
    m_post = post;
    std::stable_sort(m_post.m_addenda.begin(), m_post.m_addenda.end(), AddendumOrder);
    if (m_post.m_full)
        m_post.m_addendumCount = static_cast<int>(m_post.m_addenda.size());
    SortReactions(m_post.m_reactions);
    m_postLoaded = true;
    m_deleted = false;
    return true;
}

bool CThreadDetailModel::BeginLoadComments()
{
    if (m_postId <= 0 || m_commentsLoading)
        return false;
    m_commentsLoading = true;
    return true;
}

void CThreadDetailModel::LoadCommentsFailed()
{
    m_commentsLoading = false;
}

bool CThreadDetailModel::IsLoadingComments() const
{
    return m_commentsLoading;
}

bool CThreadDetailModel::IsCommentsLoaded() const
{
    return m_commentsLoaded;
}

bool CThreadDetailModel::HasMoreComments() const
{
    return m_hasMoreComments;
}

int64_t CThreadDetailModel::NextCommentCursor() const
{
    return m_commentCursor;
}

void CThreadDetailModel::ApplyComments(int64_t postId, const std::vector<ThreadComment>& comments, int64_t nextCursor,
                                       bool hasMore, bool firstPage)
{
    if (postId != m_postId)
        return;
    m_commentsLoading = false;
    m_commentsLoaded = true;
    if (firstPage)
        m_comments.clear();
    MergeComments(m_comments, comments);
    m_hasMoreComments = hasMore;
    if (nextCursor > 0)
        m_commentCursor = nextCursor;
    else if (!m_comments.empty())
        m_commentCursor = m_comments[m_comments.size() - 1].m_commentId;
}

bool CThreadDetailModel::BeginLoadReplies(int64_t commentId, int64_t& cursor)
{
    cursor = 0;
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        if (m_comments[i].m_commentId != commentId)
            continue;
        if (m_comments[i].m_repliesLoading)
            return false;
        m_comments[i].m_repliesLoading = true;
        cursor = m_comments[i].m_replyCursor;
        return true;
    }
    return false;
}

void CThreadDetailModel::LoadRepliesFailed(int64_t commentId)
{
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        if (m_comments[i].m_commentId == commentId)
            m_comments[i].m_repliesLoading = false;
    }
}

void CThreadDetailModel::ApplyReplies(int64_t commentId, const std::vector<ThreadComment>& replies)
{
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        if (m_comments[i].m_commentId != commentId)
            continue;
        m_comments[i].m_repliesLoading = false;
        MergeComments(m_comments[i].m_replies, replies);
        UpdateReplyCursor(m_comments[i]);
        //回复总数至少是已加载的条数（推送晚到时以本地为准）
        if (m_comments[i].m_replyCount < static_cast<int>(m_comments[i].m_replies.size()))
            m_comments[i].m_replyCount = static_cast<int>(m_comments[i].m_replies.size());
        return;
    }
}

bool CThreadDetailModel::AddComment(const ThreadComment& comment)
{
    if (m_postId <= 0 || comment.m_postId != m_postId || comment.m_commentId <= 0)
        return false;
    if (comment.m_parentId == 0)
    {
        for (size_t i = 0; i < m_comments.size(); ++i)
        {
            if (m_comments[i].m_commentId == comment.m_commentId)
                return false;
        }
        m_comments.push_back(comment);
        std::stable_sort(m_comments.begin(), m_comments.end(), CommentOrder);
        return true;
    }
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        ThreadComment& parent = m_comments[i];
        if (parent.m_commentId != comment.m_parentId)
            continue;
        for (size_t j = 0; j < parent.m_replies.size(); ++j)
        {
            if (parent.m_replies[j].m_commentId == comment.m_commentId)
                return false;
        }
        //已加载的回复少于总数时，新回复按 ID 排在最后，但中间还有没加载的：只记总数，等「查看更多回复」拉到
        const bool allLoaded = static_cast<int>(parent.m_replies.size()) >= parent.m_replyCount;
        ++parent.m_replyCount;
        if (allLoaded)
        {
            parent.m_replies.push_back(comment);
            std::stable_sort(parent.m_replies.begin(), parent.m_replies.end(), CommentOrder);
            UpdateReplyCursor(parent);
        }
        return true;
    }
    //所属留言还没加载（在后面的页里）：等翻到那一页时一起拉到
    return false;
}

bool CThreadDetailModel::RemoveComment(int64_t commentId, const ThreadComment* placeholder)
{
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        ThreadComment& c = m_comments[i];
        if (c.m_commentId == commentId)
        {
            const bool keep = (placeholder != NULL && placeholder->m_deleted) || c.m_replyCount > 0;
            if (keep)
            {
                if (c.m_deleted)
                    return false;
                MakePlaceholder(c);
            }
            else
            {
                m_comments.erase(m_comments.begin() + i);
            }
            return true;
        }
        for (size_t j = 0; j < c.m_replies.size(); ++j)
        {
            if (c.m_replies[j].m_commentId != commentId)
                continue;
            c.m_replies.erase(c.m_replies.begin() + j);
            if (c.m_replyCount > 0)
                --c.m_replyCount;
            UpdateReplyCursor(c);
            //占位留言下的最后一条回复被删：服务端同时删除了那条占位
            if (c.m_deleted && c.m_replyCount == 0)
                m_comments.erase(m_comments.begin() + i);
            return true;
        }
    }
    return false;
}

bool CThreadDetailModel::AddAddendum(const ThreadAddendum& addendum)
{
    if (m_postId <= 0 || addendum.m_postId != m_postId || addendum.m_addId <= 0)
        return false;
    for (size_t i = 0; i < m_post.m_addenda.size(); ++i)
    {
        if (m_post.m_addenda[i].m_addId == addendum.m_addId)
            return false;
    }
    m_post.m_addenda.push_back(addendum);
    std::stable_sort(m_post.m_addenda.begin(), m_post.m_addenda.end(), AddendumOrder);
    m_post.m_addendumCount = static_cast<int>(m_post.m_addenda.size());
    if (addendum.m_createTime > m_post.m_activeTime)
        m_post.m_activeTime = addendum.m_createTime;
    return true;
}

bool CThreadDetailModel::RemoveAddendum(int64_t addId)
{
    for (size_t i = 0; i < m_post.m_addenda.size(); ++i)
    {
        if (m_post.m_addenda[i].m_addId != addId)
            continue;
        m_post.m_addenda.erase(m_post.m_addenda.begin() + i);
        m_post.m_addendumCount = static_cast<int>(m_post.m_addenda.size());
        return true;
    }
    return false;
}

bool CThreadDetailModel::ToggleReactionLocal(int targetType, int64_t targetId, int reaction, bool on,
                                             std::vector<ThreadReaction>& before)
{
    if (targetType == thread_target_post)
    {
        if (m_postId <= 0 || targetId != m_postId)
            return false;
        before = m_post.m_reactions;
        ToggleReaction(m_post.m_reactions, reaction, on);
        return true;
    }
    int parentIndex = -1;
    bool isReply = false;
    ThreadComment* c = FindMutable(targetId, parentIndex, isReply);
    if (c == NULL || c->m_deleted)
        return false;
    before = c->m_reactions;
    ToggleReaction(c->m_reactions, reaction, on);
    return true;
}

void CThreadDetailModel::SetReactions(int targetType, int64_t targetId, const std::vector<ThreadReaction>& reactions)
{
    if (targetType == thread_target_post)
    {
        if (m_postId > 0 && targetId == m_postId)
        {
            m_post.m_reactions = reactions;
            SortReactions(m_post.m_reactions);
        }
        return;
    }
    int parentIndex = -1;
    bool isReply = false;
    ThreadComment* c = FindMutable(targetId, parentIndex, isReply);
    if (c != NULL)
    {
        c->m_reactions = reactions;
        SortReactions(c->m_reactions);
    }
}

bool CThreadDetailModel::ApplyChange(const ThreadChange& change, int64_t myId)
{
    if (m_postId <= 0)
        return false;
    //互动变化的帖子 ID 可能是目标所属的帖子，先判断目标
    if (change.m_changeType == thread_change_reaction)
    {
        if (change.m_postId != m_postId && !(change.m_targetType == thread_target_post && change.m_targetId == m_postId))
            return false;
        const bool mine = myId > 0 && change.m_operator == myId;
        if (change.m_targetType == thread_target_post)
        {
            if (change.m_targetId != m_postId)
                return false;
            ApplyReactionNotify(m_post.m_reactions, change.m_reactions, change.m_reaction, change.m_on, mine);
            return true;
        }
        int parentIndex = -1;
        bool isReply = false;
        ThreadComment* c = FindMutable(change.m_targetId, parentIndex, isReply);
        if (c == NULL)
            return false;
        ApplyReactionNotify(c->m_reactions, change.m_reactions, change.m_reaction, change.m_on, mine);
        return true;
    }
    if (change.m_postId != m_postId)
        return false;
    if (change.m_commentCount >= 0)
        m_post.m_commentCount = change.m_commentCount;
    if (change.m_activeTime > m_post.m_activeTime)
        m_post.m_activeTime = change.m_activeTime;
    switch (change.m_changeType)
    {
    //帖子被修改：正文、标题、图片、修改时间换成推送里的（推送是摘要形态，正文可能被截取：再拉一次完整的由调用方决定）
    case thread_change_post_edit:
        if (!change.m_hasPost)
            return false;
        m_post.m_title = change.m_post.m_title;
        m_post.m_editTime = change.m_post.m_editTime;
        m_post.m_mentions = change.m_post.m_mentions;
        if (!change.m_post.m_textTruncated && change.m_post.m_imageCount == static_cast<int>(change.m_post.m_images.size()))
        {
            m_post.m_text = change.m_post.m_text;
            m_post.m_images = change.m_post.m_images;
            m_post.m_imageCount = change.m_post.m_imageCount;
        }
        return true;

    //帖子被删除
    case thread_change_post_delete:
        m_deleted = true;
        return true;

    //置顶或取消置顶
    case thread_change_post_pin:
        if (!change.m_hasPost)
            return false;
        m_post.m_pinTime = change.m_post.m_pinTime;
        return true;

    //楼主追加补充
    case thread_change_addendum_new:
        if (!change.m_hasAddendum)
            return false;
        return AddAddendum(change.m_addendum);

    //补充被删除
    case thread_change_addendum_delete:
        return RemoveAddendum(change.m_addId);

    //新的留言或回复
    case thread_change_comment_new:
        if (!change.m_hasComment)
            return true;
        AddComment(change.m_comment);
        return true;

    //留言或回复被删除
    case thread_change_comment_delete:
        RemoveComment(change.m_commentId, change.m_hasComment ? &change.m_comment : NULL);
        return true;

    //新帖与不认识的种类：与当前详情无关
    default:
        return false;
    }
}

const std::vector<ThreadComment>& CThreadDetailModel::Comments() const
{
    return m_comments;
}

const ThreadComment* CThreadDetailModel::FindComment(int64_t commentId) const
{
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        if (m_comments[i].m_commentId == commentId)
            return &m_comments[i];
        for (size_t j = 0; j < m_comments[i].m_replies.size(); ++j)
        {
            if (m_comments[i].m_replies[j].m_commentId == commentId)
                return &m_comments[i].m_replies[j];
        }
    }
    return NULL;
}

int CThreadDetailModel::UnloadedReplies(const ThreadComment& comment)
{
    const int rest = comment.m_replyCount - static_cast<int>(comment.m_replies.size());
    return rest > 0 ? rest : 0;
}

ThreadComment* CThreadDetailModel::FindMutable(int64_t commentId, int& parentIndex, bool& isReply)
{
    parentIndex = -1;
    isReply = false;
    for (size_t i = 0; i < m_comments.size(); ++i)
    {
        if (m_comments[i].m_commentId == commentId)
        {
            parentIndex = static_cast<int>(i);
            return &m_comments[i];
        }
        for (size_t j = 0; j < m_comments[i].m_replies.size(); ++j)
        {
            if (m_comments[i].m_replies[j].m_commentId == commentId)
            {
                parentIndex = static_cast<int>(i);
                isReply = true;
                return &m_comments[i].m_replies[j];
            }
        }
    }
    return NULL;
}

} // namespace threadgroup
