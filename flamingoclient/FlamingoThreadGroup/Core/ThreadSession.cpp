/**
 *  帖子群会话控制器 CThreadSession 的实现，见 ThreadSession.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadSession.h"

#include <windows.h>
#include <stdio.h>

#include "ThreadCodec.h"
#include "ThreadText.h"

namespace threadgroup {

namespace {

//去重标识的缓冲长度：前缀 + 三个 64 位整数的十进制文本与分隔符
const int kClientIdChars = 96;

//判断一个命令是不是本人发起、要把结果告诉界面的操作（列表、留言等加载类请求不算）
bool IsUserAction(uint16_t cmd)
{
    switch (cmd)
    {
    //发帖、修改、删除、置顶、留言、删留言、互动、补充、删补充
    case thread_cmd_publish_post:
    case thread_cmd_edit_post:
    case thread_cmd_delete_post:
    case thread_cmd_pin_post:
    case thread_cmd_add_comment:
    case thread_cmd_delete_comment:
    case thread_cmd_react:
    case thread_cmd_add_addendum:
    case thread_cmd_delete_addendum:
        return true;

    //拉列表、拉帖子、拉留言与回复、拉互动名单、登记查看
    default:
        return false;
    }
}

} // namespace

CThreadSession::CThreadSession(int64_t groupId, IThreadSessionSender* sender)
    : m_groupId(groupId)
    , m_sender(sender)
    , m_view(NULL)
    , m_clientIdSeq(0)
    , m_started(false)
{
}

void CThreadSession::SetView(IThreadSessionView* view)
{
    m_view = view;
}

int64_t CThreadSession::GroupId() const
{
    return m_groupId;
}

void CThreadSession::Start()
{
    m_started = true;
    Pending watch;
    watch.m_cmd = thread_cmd_watch;
    Send(thread_cmd_watch, ThreadCodec::BuildWatch(m_groupId, true), watch);
    LoadFirstPage();
    if (m_detail.PostId() > 0)
        LoadSelectedPost();
}

void CThreadSession::Stop()
{
    m_pending.clear();
    if (!m_started)
        return;
    m_started = false;
    //撤销登记不关心应答：会话随后销毁，应答到达时找不到会话，由模块丢弃
    if (m_sender != NULL)
        m_sender->SendThreadCommand(m_groupId, thread_cmd_watch, ThreadCodec::BuildWatch(m_groupId, false));
}

void CThreadSession::OnDisconnected()
{
    std::map<uint32_t, Pending> pending;
    pending.swap(m_pending);
    for (std::map<uint32_t, Pending>::const_iterator it = pending.begin(); it != pending.end(); ++it)
    {
        const Pending& p = it->second;
        if (IsUserAction(p.m_cmd))
            FailPending(p, thread_error_timeout);
        else if (p.m_cmd == thread_cmd_list_replies)
            m_detail.LoadRepliesFailed(p.m_commentId);
    }
    m_feed.LoadFailed();
    m_detail.LoadCommentsFailed();
    NotifyFeed();
    NotifyDetail();
}

//======================== 帖子列表 ========================

void CThreadSession::SetSort(int sort)
{
    m_feed.Reset(sort);
    NotifyFeed();
    LoadFirstPage();
}

bool CThreadSession::LoadFirstPage()
{
    m_feed.Reset(m_feed.Sort());
    if (!m_feed.BeginLoad())
        return false;
    Pending p;
    p.m_cmd = thread_cmd_list_posts;
    p.m_firstPage = true;
    p.m_sort = m_feed.Sort();
    if (!Send(thread_cmd_list_posts, ThreadCodec::BuildListPosts(m_groupId, m_feed.Sort(), std::string(), kPostPageSize), p))
    {
        m_feed.LoadFailed();
        return false;
    }
    return true;
}

bool CThreadSession::LoadMore()
{
    if (!m_feed.IsLoaded() || !m_feed.HasMore() || !m_feed.BeginLoad())
        return false;
    Pending p;
    p.m_cmd = thread_cmd_list_posts;
    p.m_firstPage = false;
    p.m_sort = m_feed.Sort();
    if (!Send(thread_cmd_list_posts,
              ThreadCodec::BuildListPosts(m_groupId, m_feed.Sort(), m_feed.NextCursor(), kPostPageSize), p))
    {
        m_feed.LoadFailed();
        return false;
    }
    return true;
}

//======================== 帖子详情 ========================

void CThreadSession::SelectPost(int64_t postId)
{
    m_detail.Select(postId, m_feed.Find(postId));
    NotifyDetail();
    if (postId > 0)
        LoadSelectedPost();
}

void CThreadSession::LoadSelectedPost()
{
    const int64_t postId = m_detail.PostId();
    if (postId <= 0)
        return;
    ReloadPost();
    if (m_detail.BeginLoadComments())
    {
        Pending c;
        c.m_cmd = thread_cmd_list_comments;
        c.m_postId = postId;
        c.m_firstPage = true;
        if (!Send(thread_cmd_list_comments, ThreadCodec::BuildListComments(postId, 0, kCommentPageSize), c))
            m_detail.LoadCommentsFailed();
    }
}

bool CThreadSession::ReloadPost()
{
    const int64_t postId = m_detail.PostId();
    if (postId <= 0)
        return false;
    Pending p;
    p.m_cmd = thread_cmd_get_post;
    p.m_postId = postId;
    return Send(thread_cmd_get_post, ThreadCodec::BuildPostId(postId), p);
}

bool CThreadSession::LoadMoreComments()
{
    const int64_t postId = m_detail.PostId();
    if (postId <= 0 || !m_detail.IsCommentsLoaded() || !m_detail.HasMoreComments() || !m_detail.BeginLoadComments())
        return false;
    Pending p;
    p.m_cmd = thread_cmd_list_comments;
    p.m_postId = postId;
    p.m_firstPage = false;
    if (!Send(thread_cmd_list_comments,
              ThreadCodec::BuildListComments(postId, m_detail.NextCommentCursor(), kCommentPageSize), p))
    {
        m_detail.LoadCommentsFailed();
        return false;
    }
    return true;
}

bool CThreadSession::LoadMoreReplies(int64_t commentId)
{
    int64_t cursor = 0;
    if (!m_detail.BeginLoadReplies(commentId, cursor))
        return false;
    Pending p;
    p.m_cmd = thread_cmd_list_replies;
    p.m_postId = m_detail.PostId();
    p.m_commentId = commentId;
    if (!Send(thread_cmd_list_replies, ThreadCodec::BuildListReplies(commentId, cursor, kReplyPageSize), p))
    {
        m_detail.LoadRepliesFailed(commentId);
        return false;
    }
    return true;
}

//======================== 本人发起的操作 ========================

bool CThreadSession::PublishPost(const std::wstring& title, const std::wstring& wireText,
                                 const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                                 const std::vector<ThreadAtSpan>& at)
{
    Pending p;
    p.m_cmd = thread_cmd_publish_post;
    return Send(thread_cmd_publish_post,
                ThreadCodec::BuildPublishPost(m_groupId, NewClientId("tp"), title, wireText, images, mentions, at), p);
}

bool CThreadSession::EditPost(int64_t postId, const std::wstring& title, const std::wstring& wireText,
                              const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                              const std::vector<ThreadAtSpan>& at)
{
    Pending p;
    p.m_cmd = thread_cmd_edit_post;
    p.m_postId = postId;
    return Send(thread_cmd_edit_post, ThreadCodec::BuildEditPost(postId, title, wireText, images, mentions, at), p);
}

bool CThreadSession::DeletePost(int64_t postId)
{
    Pending p;
    p.m_cmd = thread_cmd_delete_post;
    p.m_postId = postId;
    return Send(thread_cmd_delete_post, ThreadCodec::BuildPostId(postId), p);
}

bool CThreadSession::PinPost(int64_t postId, bool pin)
{
    Pending p;
    p.m_cmd = thread_cmd_pin_post;
    p.m_postId = postId;
    return Send(thread_cmd_pin_post, ThreadCodec::BuildPin(postId, pin), p);
}

bool CThreadSession::AddComment(int64_t postId, int64_t parentId, const std::wstring& wireText,
                                const std::vector<int64_t>& mentions, const std::vector<ThreadAtSpan>& at)
{
    Pending p;
    p.m_cmd = thread_cmd_add_comment;
    p.m_postId = postId;
    p.m_commentId = parentId;
    return Send(thread_cmd_add_comment,
                ThreadCodec::BuildAddComment(postId, parentId, NewClientId("tc"), wireText, mentions, at), p);
}

bool CThreadSession::DeleteComment(int64_t commentId)
{
    Pending p;
    p.m_cmd = thread_cmd_delete_comment;
    p.m_postId = m_detail.PostId();
    p.m_commentId = commentId;
    return Send(thread_cmd_delete_comment, ThreadCodec::BuildCommentId(commentId), p);
}

bool CThreadSession::React(int targetType, int64_t targetId, int reaction, bool on)
{
    Pending p;
    p.m_cmd = thread_cmd_react;
    p.m_targetType = targetType;
    p.m_targetId = targetId;
    p.m_reaction = reaction;
    //先发后改：没有发出去就不动本地汇总
    const std::string body = ThreadCodec::BuildReact(targetType, targetId, reaction, on);
    if (targetType == thread_target_post)
        p.m_beforeInFeed = m_feed.ToggleReactionLocal(targetId, reaction, on, p.m_beforeFeed);
    p.m_beforeInDetail = m_detail.ToggleReactionLocal(targetType, targetId, reaction, on, p.m_beforeDetail);
    if (!Send(thread_cmd_react, body, p))
    {
        if (p.m_beforeInFeed)
            m_feed.SetReactions(targetId, p.m_beforeFeed);
        if (p.m_beforeInDetail)
            m_detail.SetReactions(targetType, targetId, p.m_beforeDetail);
        return false;
    }
    if (p.m_beforeInFeed)
        NotifyFeed();
    if (p.m_beforeInDetail)
        NotifyDetail();
    return true;
}

bool CThreadSession::GetReactors(int targetType, int64_t targetId, int reaction)
{
    Pending p;
    p.m_cmd = thread_cmd_get_reactors;
    p.m_targetType = targetType;
    p.m_targetId = targetId;
    p.m_reaction = reaction;
    return Send(thread_cmd_get_reactors, ThreadCodec::BuildGetReactors(targetType, targetId, reaction, kReactorListLimit),
                p);
}

bool CThreadSession::AddAddendum(int64_t postId, const std::wstring& wireText, const std::vector<ThreadImage>& images,
                                 const std::vector<int64_t>& mentions, const std::vector<ThreadAtSpan>& at)
{
    Pending p;
    p.m_cmd = thread_cmd_add_addendum;
    p.m_postId = postId;
    return Send(thread_cmd_add_addendum,
                ThreadCodec::BuildAddAddendum(postId, NewClientId("ta"), wireText, images, mentions, at), p);
}

bool CThreadSession::DeleteAddendum(int64_t addId)
{
    Pending p;
    p.m_cmd = thread_cmd_delete_addendum;
    p.m_postId = m_detail.PostId();
    p.m_addId = addId;
    return Send(thread_cmd_delete_addendum, ThreadCodec::BuildAddendumId(addId), p);
}

//======================== 收帧 ========================

bool CThreadSession::OnResponse(uint16_t cmd, uint32_t seq, const std::string& body)
{
    std::map<uint32_t, Pending>::iterator it = m_pending.find(seq);
    if (it == m_pending.end())
        return false;
    const Pending p = it->second;
    m_pending.erase(it);
    if (p.m_cmd != cmd)
    {
        //同一个 seq 的应答命令号对不上：按失败处理，避免把别的报文当成这条请求的结果
        if (IsUserAction(p.m_cmd))
            FailPending(p, thread_error_bad_request);
        return true;
    }
    switch (cmd)
    {
    //一页帖子
    case thread_cmd_list_posts:
        HandleListPosts(p, body);
        break;

    //完整帖子
    case thread_cmd_get_post:
        HandleGetPost(p, body);
        break;

    //发帖、修改、置顶：应答都带帖子
    case thread_cmd_publish_post:
    case thread_cmd_edit_post:
    case thread_cmd_pin_post:
        HandlePostWrite(p, body);
        break;

    //删除帖子
    case thread_cmd_delete_post:
        HandleDeletePost(p, body);
        break;

    //一页留言或一页回复
    case thread_cmd_list_comments:
    case thread_cmd_list_replies:
        HandleComments(p, body);
        break;

    //留言或回复
    case thread_cmd_add_comment:
        HandleAddComment(p, body);
        break;

    //删除留言或回复
    case thread_cmd_delete_comment:
        HandleDeleteComment(p, body);
        break;

    //互动
    case thread_cmd_react:
        HandleReact(p, body);
        break;

    //互动名单
    case thread_cmd_get_reactors:
        HandleReactors(p, body);
        break;

    //追加补充
    case thread_cmd_add_addendum:
        HandleAddAddendum(p, body);
        break;

    //删除补充
    case thread_cmd_delete_addendum:
        HandleDeleteAddendum(p, body);
        break;

    //登记「正在查看」：失败时告诉界面（例如企业刚关闭了帖子群）
    case thread_cmd_watch:
        {
            int code = thread_error_server_internal;
            ThreadCodec::ParseCode(body, code);
            if (code != thread_error_ok)
                NotifyAction(thread_cmd_watch, code, 0);
        }
        break;

    //其余命令不是由会话发出的
    default:
        break;
    }
    return true;
}

void CThreadSession::OnChange(const ThreadChange& change)
{
    if (change.m_groupId != m_groupId)
        return;
    const int64_t myId = (m_sender != NULL) ? m_sender->MyUserId() : 0;
    const bool feedChanged = m_feed.ApplyChange(change, myId);
    const bool detailChanged = m_detail.ApplyChange(change, myId);
    if (feedChanged)
        NotifyFeed();
    if (detailChanged)
        NotifyDetail();
    //正文被修改而推送里的摘要被截取了（或图片不全）：再拉一次完整内容
    if (change.m_changeType == thread_change_post_edit && change.m_postId == m_detail.PostId() && change.m_hasPost
        && (change.m_post.m_textTruncated
            || change.m_post.m_imageCount != static_cast<int>(change.m_post.m_images.size())))
        ReloadPost();
}

const CThreadFeedModel& CThreadSession::Feed() const
{
    return m_feed;
}

const CThreadDetailModel& CThreadSession::Detail() const
{
    return m_detail;
}

size_t CThreadSession::PendingCount() const
{
    return m_pending.size();
}

//======================== 内部 ========================

bool CThreadSession::Send(uint16_t cmd, const std::string& body, const Pending& pending)
{
    if (m_sender == NULL)
        return false;
    const uint32_t seq = m_sender->SendThreadCommand(m_groupId, cmd, body);
    if (seq == 0)
        return false;
    m_pending[seq] = pending;
    return true;
}

std::string CThreadSession::NewClientId(const char* prefix)
{
    ++m_clientIdSeq;
    char buf[kClientIdChars];
    snprintf(buf, sizeof(buf), "%s-%lld-%llu-%u", prefix, static_cast<long long>(m_groupId),
             static_cast<unsigned long long>(::GetTickCount64()), static_cast<unsigned>(m_clientIdSeq));
    return std::string(buf);
}

void CThreadSession::FailPending(const Pending& p, int code)
{
    if (p.m_cmd == thread_cmd_react)
    {
        if (p.m_beforeInFeed)
        {
            m_feed.SetReactions(p.m_targetId, p.m_beforeFeed);
            NotifyFeed();
        }
        if (p.m_beforeInDetail)
        {
            m_detail.SetReactions(p.m_targetType, p.m_targetId, p.m_beforeDetail);
            NotifyDetail();
        }
    }
    int64_t id = p.m_postId;
    if (p.m_cmd == thread_cmd_delete_comment)
        id = p.m_commentId;
    else if (p.m_cmd == thread_cmd_delete_addendum)
        id = p.m_addId;
    else if (p.m_cmd == thread_cmd_react)
        id = p.m_targetId;
    NotifyAction(p.m_cmd, code, id);
}

void CThreadSession::HandleListPosts(const Pending& p, const std::string& body)
{
    //应答到达时排序已经切换：这一页属于另一种排序，丢弃
    if (p.m_sort != m_feed.Sort())
        return;
    int code = thread_error_server_internal;
    std::vector<ThreadPost> posts;
    std::string next;
    bool more = false;
    if (!ThreadCodec::ParseListPosts(body, code, posts, next, more) || code != thread_error_ok)
    {
        m_feed.LoadFailed();
        NotifyFeed();
        NotifyAction(thread_cmd_list_posts, code, 0);
        return;
    }
    m_feed.ApplyPage(posts, next, more, p.m_firstPage);
    NotifyFeed();
}

void CThreadSession::HandleGetPost(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    ThreadPost post;
    bool hasPost = false;
    ThreadCodec::ParsePostResponse(body, code, post, hasPost);
    if (p.m_postId != m_detail.PostId())
        return;
    if (code == thread_error_not_exist)
    {
        m_detail.MarkDeleted();
        NotifyDetail();
        return;
    }
    if (code != thread_error_ok || !hasPost)
    {
        NotifyAction(thread_cmd_get_post, code, p.m_postId);
        return;
    }
    m_detail.ApplyPost(post);
    NotifyDetail();
}

void CThreadSession::HandlePostWrite(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    ThreadPost post;
    bool hasPost = false;
    ThreadCodec::ParsePostResponse(body, code, post, hasPost);
    if (code != thread_error_ok || !hasPost)
    {
        FailPending(p, code == thread_error_ok ? thread_error_server_internal : code);
        return;
    }
    m_feed.Upsert(post);
    NotifyFeed();
    if (post.m_postId == m_detail.PostId())
    {
        if (post.m_full)
        {
            m_detail.ApplyPost(post);
        }
        else
        {
            //置顶的应答是摘要形态：只换置顶时间
            ThreadChange pin;
            pin.m_groupId = m_groupId;
            pin.m_changeType = thread_change_post_pin;
            pin.m_postId = post.m_postId;
            pin.m_hasPost = true;
            pin.m_post = post;
            m_detail.ApplyChange(pin, 0);
        }
        NotifyDetail();
    }
    NotifyAction(p.m_cmd, thread_error_ok, post.m_postId);
}

void CThreadSession::HandleDeletePost(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int64_t postId = 0;
    ThreadCodec::ParseIdResponse(body, "postid", code, postId);
    //帖子已经不存在也按删除成功处理：结果相同
    if (code != thread_error_ok && code != thread_error_not_exist)
    {
        FailPending(p, code);
        return;
    }
    if (m_feed.Remove(p.m_postId))
        NotifyFeed();
    if (m_detail.PostId() == p.m_postId)
    {
        m_detail.Select(0, NULL);
        NotifyDetail();
    }
    NotifyAction(thread_cmd_delete_post, thread_error_ok, p.m_postId);
}

void CThreadSession::HandleComments(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int64_t owner = 0;
    std::vector<ThreadComment> comments;
    int64_t next = 0;
    bool more = false;
    const bool parsed = ThreadCodec::ParseCommentsResponse(body, code, owner, comments, next, more);
    if (!parsed || code != thread_error_ok)
    {
        if (p.m_cmd == thread_cmd_list_replies)
            m_detail.LoadRepliesFailed(p.m_commentId);
        else if (p.m_postId == m_detail.PostId())
            m_detail.LoadCommentsFailed();
        NotifyDetail();
        NotifyAction(p.m_cmd, code, p.m_postId);
        return;
    }
    if (p.m_postId != m_detail.PostId())
        return;
    if (p.m_cmd == thread_cmd_list_replies)
        m_detail.ApplyReplies(p.m_commentId, comments);
    else
        m_detail.ApplyComments(p.m_postId, comments, next, more, p.m_firstPage);
    NotifyDetail();
}

void CThreadSession::HandleAddComment(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    ThreadComment c;
    bool hasComment = false;
    ThreadCodec::ParseCommentResponse(body, code, c, hasComment);
    if (code != thread_error_ok || !hasComment)
    {
        FailPending(p, code == thread_error_ok ? thread_error_server_internal : code);
        return;
    }
    //本地先把留言总数加一、最新动态时间前移；随后到达的推送带绝对值，以推送为准
    const ThreadPost* inFeed = m_feed.Find(c.m_postId);
    int count = (inFeed != NULL) ? inFeed->m_commentCount : m_detail.Post().m_commentCount;
    ThreadChange change;
    change.m_groupId = m_groupId;
    change.m_changeType = thread_change_comment_new;
    change.m_postId = c.m_postId;
    change.m_commentId = c.m_commentId;
    change.m_hasComment = true;
    change.m_comment = c;
    change.m_commentCount = count + 1;
    change.m_activeTime = c.m_createTime;
    if (m_feed.ApplyChange(change, 0))
        NotifyFeed();
    if (m_detail.ApplyChange(change, 0))
        NotifyDetail();
    NotifyAction(thread_cmd_add_comment, thread_error_ok, c.m_commentId);
}

void CThreadSession::HandleDeleteComment(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int64_t commentId = 0;
    ThreadCodec::ParseIdResponse(body, "commentid", code, commentId);
    if (code != thread_error_ok && code != thread_error_not_exist)
    {
        FailPending(p, code);
        return;
    }
    const ThreadPost* inFeed = m_feed.Find(p.m_postId);
    int count = (inFeed != NULL) ? inFeed->m_commentCount : m_detail.Post().m_commentCount;
    ThreadChange change;
    change.m_groupId = m_groupId;
    change.m_changeType = thread_change_comment_delete;
    change.m_postId = p.m_postId;
    change.m_commentId = p.m_commentId;
    change.m_commentCount = (count > 0) ? count - 1 : 0;
    if (m_feed.ApplyChange(change, 0))
        NotifyFeed();
    if (m_detail.ApplyChange(change, 0))
        NotifyDetail();
    NotifyAction(thread_cmd_delete_comment, thread_error_ok, p.m_commentId);
}

void CThreadSession::HandleReact(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int targetType = 0;
    int64_t targetId = 0;
    std::vector<ThreadReaction> reactions;
    ThreadCodec::ParseReactResponse(body, code, targetType, targetId, reactions);
    if (code != thread_error_ok)
    {
        FailPending(p, code);
        return;
    }
    //应答带回的汇总是本人视角，直接替换
    if (p.m_targetType == thread_target_post && m_feed.Find(p.m_targetId) != NULL)
    {
        m_feed.SetReactions(p.m_targetId, reactions);
        NotifyFeed();
    }
    m_detail.SetReactions(p.m_targetType, p.m_targetId, reactions);
    NotifyDetail();
    NotifyAction(thread_cmd_react, thread_error_ok, p.m_targetId);
}

void CThreadSession::HandleReactors(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int targetType = 0;
    int64_t targetId = 0;
    int reaction = 0;
    std::vector<int64_t> users;
    int total = 0;
    ThreadCodec::ParseReactorsResponse(body, code, targetType, targetId, reaction, users, total);
    if (code != thread_error_ok)
        return;
    if (m_view != NULL)
        m_view->OnReactorsLoaded(p.m_targetType, p.m_targetId, p.m_reaction, users, total);
}

void CThreadSession::HandleAddAddendum(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    ThreadAddendum a;
    bool hasAddendum = false;
    ThreadCodec::ParseAddendumResponse(body, code, a, hasAddendum);
    if (code != thread_error_ok || !hasAddendum)
    {
        FailPending(p, code == thread_error_ok ? thread_error_server_internal : code);
        return;
    }
    //列表里的补充段数只按推送更新（推送一定会到：补充推给全体在线成员）
    if (m_detail.AddAddendum(a))
        NotifyDetail();
    NotifyAction(thread_cmd_add_addendum, thread_error_ok, a.m_addId);
}

void CThreadSession::HandleDeleteAddendum(const Pending& p, const std::string& body)
{
    int code = thread_error_server_internal;
    int64_t addId = 0;
    ThreadCodec::ParseIdResponse(body, "addid", code, addId);
    if (code != thread_error_ok && code != thread_error_not_exist)
    {
        FailPending(p, code);
        return;
    }
    if (m_detail.RemoveAddendum(p.m_addId))
        NotifyDetail();
    NotifyAction(thread_cmd_delete_addendum, thread_error_ok, p.m_addId);
}

void CThreadSession::NotifyFeed()
{
    if (m_view != NULL)
        m_view->OnFeedChanged();
}

void CThreadSession::NotifyDetail()
{
    if (m_view != NULL)
        m_view->OnDetailChanged();
}

void CThreadSession::NotifyAction(int cmd, int code, int64_t id)
{
    if (m_view != NULL)
        m_view->OnActionDone(cmd, code, id);
}

} // namespace threadgroup
