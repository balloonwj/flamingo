/**
 *  帖子列表模型 CThreadFeedModel 的实现，见 ThreadFeedModel.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadFeedModel.h"

#include <algorithm>

#include "ThreadReactions.h"

namespace threadgroup {

namespace {

//最新动态排序：置顶在前（置顶时间倒序），其余按最新动态时间倒序、帖子 ID 倒序
bool ActivityOrder(const ThreadPost& a, const ThreadPost& b)
{
    const bool pa = a.m_pinTime > 0;
    const bool pb = b.m_pinTime > 0;
    if (pa != pb)
        return pa;
    if (pa)
        return a.m_pinTime > b.m_pinTime;
    if (a.m_activeTime != b.m_activeTime)
        return a.m_activeTime > b.m_activeTime;
    return a.m_postId > b.m_postId;
}

//最新发布排序：置顶在前，其余按帖子 ID 倒序
bool PublishOrder(const ThreadPost& a, const ThreadPost& b)
{
    const bool pa = a.m_pinTime > 0;
    const bool pb = b.m_pinTime > 0;
    if (pa != pb)
        return pa;
    if (pa)
        return a.m_pinTime > b.m_pinTime;
    return a.m_postId > b.m_postId;
}

} // namespace

CThreadFeedModel::CThreadFeedModel()
    : m_sort(thread_sort_activity)
    , m_hasMore(false)
    , m_loading(false)
    , m_loaded(false)
{
}

void CThreadFeedModel::Reset(int sort)
{
    m_sort = (sort == thread_sort_publish) ? thread_sort_publish : thread_sort_activity;
    m_posts.clear();
    m_nextCursor.clear();
    m_hasMore = false;
    m_loading = false;
    m_loaded = false;
}

int CThreadFeedModel::Sort() const
{
    return m_sort;
}

bool CThreadFeedModel::BeginLoad()
{
    if (m_loading)
        return false;
    m_loading = true;
    return true;
}

void CThreadFeedModel::LoadFailed()
{
    m_loading = false;
}

bool CThreadFeedModel::IsLoading() const
{
    return m_loading;
}

bool CThreadFeedModel::IsLoaded() const
{
    return m_loaded;
}

bool CThreadFeedModel::HasMore() const
{
    return m_hasMore;
}

const std::string& CThreadFeedModel::NextCursor() const
{
    return m_nextCursor;
}

void CThreadFeedModel::ApplyPage(const std::vector<ThreadPost>& posts, const std::string& nextCursor, bool hasMore,
                                 bool firstPage)
{
    m_loading = false;
    m_loaded = true;
    if (firstPage)
        m_posts.clear();
    for (size_t i = 0; i < posts.size(); ++i)
    {
        const int idx = IndexOf(posts[i].m_postId);
        if (idx >= 0)
            m_posts[idx] = posts[i];
        else
            m_posts.push_back(posts[i]);
    }
    m_nextCursor = nextCursor;
    m_hasMore = hasMore && !nextCursor.empty();
    Reorder();
}

void CThreadFeedModel::Upsert(const ThreadPost& post)
{
    if (post.m_postId <= 0)
        return;
    const int idx = IndexOf(post.m_postId);
    if (idx >= 0)
        m_posts[idx] = post;
    else
        m_posts.push_back(post);
    Reorder();
}

bool CThreadFeedModel::Remove(int64_t postId)
{
    const int idx = IndexOf(postId);
    if (idx < 0)
        return false;
    m_posts.erase(m_posts.begin() + idx);
    return true;
}

bool CThreadFeedModel::ToggleReactionLocal(int64_t postId, int reaction, bool on, std::vector<ThreadReaction>& before)
{
    const int idx = IndexOf(postId);
    if (idx < 0)
        return false;
    before = m_posts[idx].m_reactions;
    ToggleReaction(m_posts[idx].m_reactions, reaction, on);
    return true;
}

void CThreadFeedModel::SetReactions(int64_t postId, const std::vector<ThreadReaction>& reactions)
{
    const int idx = IndexOf(postId);
    if (idx < 0)
        return;
    m_posts[idx].m_reactions = reactions;
    SortReactions(m_posts[idx].m_reactions);
}

bool CThreadFeedModel::ApplyChange(const ThreadChange& change, int64_t myId)
{
    const int idx = IndexOf(change.m_postId);
    switch (change.m_changeType)
    {
    //新帖：第一页还没到时不插（第一页会带上它）；已在列表里（本人发帖的应答先到）时不重复
    case thread_change_post_new:
        if (!m_loaded || !change.m_hasPost || idx >= 0)
            return false;
        m_posts.push_back(change.m_post);
        Reorder();
        return true;

    //修改：只换正文相关字段，互动汇总保留本地的（推送里的 mine 是修改人视角）
    case thread_change_post_edit:
        if (idx < 0 || !change.m_hasPost)
            return false;
        {
            ThreadPost& p = m_posts[idx];
            p.m_title = change.m_post.m_title;
            p.m_text = change.m_post.m_text;
            p.m_textTruncated = change.m_post.m_textTruncated;
            p.m_images = change.m_post.m_images;
            p.m_imageCount = change.m_post.m_imageCount;
            p.m_mentions = change.m_post.m_mentions;
            p.m_editTime = change.m_post.m_editTime;
        }
        return true;

    //删除
    case thread_change_post_delete:
        return Remove(change.m_postId);

    //置顶或取消置顶：只换置顶时间
    case thread_change_post_pin:
        if (idx < 0 || !change.m_hasPost)
            return false;
        m_posts[idx].m_pinTime = change.m_post.m_pinTime;
        Reorder();
        return true;

    //楼主追加补充：段数加一，最新动态时间前移
    case thread_change_addendum_new:
        if (idx < 0)
            return false;
        ++m_posts[idx].m_addendumCount;
        if (change.m_activeTime > m_posts[idx].m_activeTime)
            m_posts[idx].m_activeTime = change.m_activeTime;
        Reorder();
        return true;

    //补充被删除：段数减一
    case thread_change_addendum_delete:
        if (idx < 0)
            return false;
        if (m_posts[idx].m_addendumCount > 0)
            --m_posts[idx].m_addendumCount;
        return true;

    //新留言或回复：留言总数与最新动态时间以推送为准
    case thread_change_comment_new:
        if (idx < 0)
            return false;
        if (change.m_commentCount >= 0)
            m_posts[idx].m_commentCount = change.m_commentCount;
        else
            ++m_posts[idx].m_commentCount;
        if (change.m_activeTime > m_posts[idx].m_activeTime)
            m_posts[idx].m_activeTime = change.m_activeTime;
        Reorder();
        return true;

    //留言或回复被删除：留言总数以推送为准
    case thread_change_comment_delete:
        if (idx < 0 || change.m_commentCount < 0)
            return false;
        m_posts[idx].m_commentCount = change.m_commentCount;
        return true;

    //互动变化：只处理目标是帖子的
    case thread_change_reaction:
        if (change.m_targetType != thread_target_post)
            return false;
        {
            const int target = IndexOf(change.m_targetId);
            if (target < 0)
                return false;
            ApplyReactionNotify(m_posts[target].m_reactions, change.m_reactions, change.m_reaction, change.m_on,
                                myId > 0 && change.m_operator == myId);
        }
        return true;

    //不认识的变化类型：新版本服务端可能加了新的种类，忽略
    default:
        return false;
    }
}

const std::vector<ThreadPost>& CThreadFeedModel::Posts() const
{
    return m_posts;
}

const ThreadPost* CThreadFeedModel::Find(int64_t postId) const
{
    const int idx = IndexOf(postId);
    return idx >= 0 ? &m_posts[idx] : NULL;
}

int CThreadFeedModel::PinnedCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_posts.size(); ++i)
    {
        if (m_posts[i].m_pinTime > 0)
            ++count;
    }
    return count;
}

void CThreadFeedModel::Reorder()
{
    if (m_sort == thread_sort_publish)
        std::stable_sort(m_posts.begin(), m_posts.end(), PublishOrder);
    else
        std::stable_sort(m_posts.begin(), m_posts.end(), ActivityOrder);
}

int CThreadFeedModel::IndexOf(int64_t postId) const
{
    for (size_t i = 0; i < m_posts.size(); ++i)
    {
        if (m_posts[i].m_postId == postId)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace threadgroup
