/**
 *  ChatSession 的帖子群部分（cmd_thread_* 2701~2720，docs/帖子群方案.md 第 4~6 节）。
 *
 *  每条命令的处理顺序大体相同：解析 → 企业开关、群类型与成员身份（写操作另查禁言与入群必读）→ 字段校验 →
 *  权限 → 敏感词 → 写库 → 应答 → 写卡片消息（发帖、留言、回复、补充）→ 2720 推送。
 *  帖子级变化（新帖、修改、删除、置顶、补充）推给全体在线成员；留言、回复与互动只推给登记了「正在查看」的连接
 *  （ThreadWatchRegistry），避免高频变化每次都推给全群。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "ChatSession.h"

#include <stdlib.h>
#include <chrono>
#include <set>
#include <vector>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "GroupProtocol.h"
#include "GroupManager.h"
#include "MessageManager.h"
#include "MessageProtocol.h"
#include "FeatureSwitchManager.h"
#include "DepartmentManager.h"
#include "SensitiveWordDefs.h"
#include "ThreadProtocol.h"
#include "ThreadManager.h"
#include "ThreadWatchRegistry.h"

namespace {

//当前时间（毫秒，Unix 纪元起）
int64_t threadNowMs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

//取一组帖子的 ID
std::vector<int64_t> postIdsOf(const std::vector<ThreadPost>& posts)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < posts.size(); ++i)
        ids.push_back(posts[i].postid);
    return ids;
}

//给一组帖子填互动汇总与补充段数（列表、完整帖子都要）；查询失败时这两项留空，不影响帖子本身下发
void fillPostExtras(std::vector<ThreadPost>& posts, int64_t viewerId)
{
    if (posts.empty())
        return;
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    const std::vector<int64_t> ids = postIdsOf(posts);
    std::map<int64_t, std::vector<ThreadReaction> > reactions;
    mgr.loadReactions(thread_target_post, ids, viewerId, reactions);
    std::map<int64_t, int> addenda;
    mgr.countAddenda(ids, addenda);
    for (size_t i = 0; i < posts.size(); ++i)
    {
        std::map<int64_t, std::vector<ThreadReaction> >::const_iterator r = reactions.find(posts[i].postid);
        if (r != reactions.end())
            posts[i].reactions = r->second;
        std::map<int64_t, int>::const_iterator a = addenda.find(posts[i].postid);
        posts[i].addendumCount = (a != addenda.end()) ? a->second : 0;
    }
}

//给一组留言（含它们顺带的回复）填互动汇总
void fillCommentReactions(std::vector<ThreadComment>& comments, int64_t viewerId)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < comments.size(); ++i)
    {
        ids.push_back(comments[i].commentid);
        for (size_t j = 0; j < comments[i].previewReplies.size(); ++j)
            ids.push_back(comments[i].previewReplies[j].commentid);
    }
    if (ids.empty())
        return;
    std::map<int64_t, std::vector<ThreadReaction> > reactions;
    Singleton<ThreadManager>::Instance().loadReactions(thread_target_comment, ids, viewerId, reactions);
    for (size_t i = 0; i < comments.size(); ++i)
    {
        std::map<int64_t, std::vector<ThreadReaction> >::const_iterator r = reactions.find(comments[i].commentid);
        if (r != reactions.end())
            comments[i].reactions = r->second;
        for (size_t j = 0; j < comments[i].previewReplies.size(); ++j)
        {
            ThreadComment& reply = comments[i].previewReplies[j];
            std::map<int64_t, std::vector<ThreadReaction> >::const_iterator rr = reactions.find(reply.commentid);
            if (rr != reactions.end())
                reply.reactions = rr->second;
        }
    }
}

//读出一个群的全部成员 userid
std::vector<int64_t> groupMemberIds(int64_t groupid)
{
    std::vector<int64_t> ids;
    std::vector<GroupMemberRecord> members;
    if (Singleton<GroupManager>::Instance().getGroupMembers(groupid, members))
    {
        for (size_t i = 0; i < members.size(); ++i)
            ids.push_back(members[i].userid);
    }
    return ids;
}

//要查敏感词的文字：标题与正文
std::vector<std::string> sensitiveTextsOf(const ThreadBody& body)
{
    std::vector<std::string> texts;
    if (!body.title.empty())
        texts.push_back(body.title);
    if (!body.text.empty())
        texts.push_back(body.text);
    return texts;
}

} // namespace

void ChatSession::onThreadCommand(uint16_t cmd, const std::string& body)
{
    switch (cmd)
    {
    //拉一页帖子
    case cmd_thread_list_posts:
        onThreadListPosts(body);
        break;

    //拉一条完整帖子（含补充）
    case cmd_thread_get_post:
        onThreadGetPost(body);
        break;

    //发帖
    case cmd_thread_publish_post:
        onThreadPublishPost(body);
        break;

    //作者修改帖子
    case cmd_thread_edit_post:
        onThreadEditPost(body);
        break;

    //删除帖子
    case cmd_thread_delete_post:
        onThreadDeletePost(body);
        break;

    //置顶 / 取消置顶
    case cmd_thread_pin_post:
        onThreadPinPost(body);
        break;

    //拉一页留言
    case cmd_thread_list_comments:
        onThreadListComments(body);
        break;

    //拉一页回复
    case cmd_thread_list_replies:
        onThreadListReplies(body);
        break;

    //留言或回复
    case cmd_thread_add_comment:
        onThreadAddComment(body);
        break;

    //删除留言或回复
    case cmd_thread_delete_comment:
        onThreadDeleteComment(body);
        break;

    //点赞 / 表情回应，或取消
    case cmd_thread_react:
        onThreadReact(body);
        break;

    //拉互动名单
    case cmd_thread_get_reactors:
        onThreadGetReactors(body);
        break;

    //登记 / 撤销「正在查看」
    case cmd_thread_watch:
        onThreadWatch(body);
        break;

    //楼主追加补充
    case cmd_thread_add_addendum:
        onThreadAddAddendum(body);
        break;

    //删除一段补充
    case cmd_thread_delete_addendum:
        onThreadDeleteAddendum(body);
        break;

    //核对客户端本地存的卡片
    case cmd_thread_check_cards:
        onThreadCheckCards(body);
        break;

    //2720、2721 只由服务端推送，号段内的其余编号未定义：按报文非法应答
    default:
        send(cmd, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "unsupported thread cmd"));
        break;
    }
}

int ChatSession::checkThreadAccess(int64_t groupid, GroupRecord& group, int& myrole)
{
    myrole = -1;
    const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (!Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureThreadGroup))
        return error_code_thread_disabled;
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    if (!groupMgr.getGroup(groupid, group))
        return error_code_group_not_exist;
    if (group.grouptype != group_type_thread)
        return error_code_thread_not_thread_group;
    myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
        return error_code_not_group_member;
    return error_code_ok;
}

int ChatSession::checkThreadSpeak(uint16_t cmd, int64_t groupid, GroupRecord& group, int& myrole)
{
    const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (!Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureThreadGroup))
        return error_code_thread_disabled;
    //成员身份、禁言、入群必读与群聊发言共用同一套判定
    std::string reason;
    const int code = checkGroupSpeak(cmd, groupid, myrole, group, reason);
    if (code != error_code_ok)
        return code;
    if (group.grouptype != group_type_thread)
        return error_code_thread_not_thread_group;
    return error_code_ok;
}

void ChatSession::notifyThreadWatchers(int64_t groupid, const std::string& body)
{
    const std::vector<uint64_t> conns = Singleton<ThreadWatchRegistry>::Instance().watchers(groupid);
    for (size_t i = 0; i < conns.size(); ++i)
    {
        std::shared_ptr<ChatSession> target = findSessionByP2PConnKey(conns[i]);
        if (target)
            target->send(cmd_thread_changed_notify, 0, body);
    }
}

void ChatSession::cleanupThreadWatchOnOffline()
{
    const size_t removed = Singleton<ThreadWatchRegistry>::Instance().removeConn(p2pConnKey());
    if (removed > 0)
        LOGI("thread watch cleanup on offline, userid: %d, removed: %u", m_userinfo.userid,
             static_cast<unsigned>(removed));
}

int64_t ChatSession::sendThreadPostCard(const ThreadPost& post)
{
    const std::string content = ThreadProtocol::buildPostCardContent(post);
    const std::string stored = MessageProtocol::buildStoredContent(content, post.body.mentions, post.body.mentionall, "");
    const std::string clientMsgId = ThreadProtocol::postCardClientMsgId(post.postid);
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_group, static_cast<int32_t>(post.author), static_cast<int32_t>(post.groupid),
        message_type_thread_post, stored, post.createTime);
    if (msgId == 0)
    {
        LOGE("save thread post card failed, groupid: %lld, postid: %lld", (long long)post.groupid,
             (long long)post.postid);
        return 0;
    }
    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = conversation_group;
    rec.from = post.author;
    rec.to = post.groupid;
    rec.msgtype = message_type_thread_post;
    rec.content = content;
    rec.mentions = post.body.mentions;
    rec.mentionall = post.body.mentionall;
    rec.timestamp = post.createTime;
    rec.recalled = false;
    //推给全体成员（含作者本人的各台设备：他的会话列表同样要更新摘要）
    notifyGroupMembers(post.groupid, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, post.groupid));
    return msgId;
}

int64_t ChatSession::sendThreadReplyCard(int64_t groupid, int64_t postid, int64_t commentid, int64_t addid,
                                         const std::string& postTitle, const std::string& digest, int kind,
                                         const std::vector<int64_t>& audience, const std::vector<int64_t>& mentions,
                                         int64_t createTime, const std::string& clientMsgId,
                                         const std::vector<ThreadImage>& images, bool mentionAll)
{
    //可见者为空时不写卡片：没有人需要被提醒（例如楼主在自己的帖子下留言、又没有 @ 任何人）
    if (audience.empty())
        return 0;
    const std::string content = ThreadProtocol::buildReplyCardContent(postid, commentid, addid, postTitle, digest, kind,
                                                                      mentions, images, mentionAll);
    const std::string stored = MessageProtocol::buildStoredContent(content, mentions, mentionAll, "");
    std::vector<int32_t> visible;
    for (size_t i = 0; i < audience.size(); ++i)
        visible.push_back(static_cast<int32_t>(audience[i]));
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_group, m_userinfo.userid, static_cast<int32_t>(groupid), message_type_thread_reply,
        stored, createTime, &visible);
    if (msgId == 0)
    {
        LOGE("save thread reply card failed, groupid: %lld, postid: %lld, kind: %d", (long long)groupid,
             (long long)postid, kind);
        return 0;
    }
    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = conversation_group;
    rec.from = m_userinfo.userid;
    rec.to = groupid;
    rec.msgtype = message_type_thread_reply;
    rec.content = content;
    rec.mentions = mentions;
    rec.mentionall = mentionAll;
    rec.timestamp = createTime;
    rec.recalled = false;
    const std::string notify = MessageProtocol::buildMessageNotify(rec, groupid);
    //只推给可见者与动作发起人本人：名单外的成员此刻收不到，之后同步、搜索也看不到（visibleToUserClause）
    for (size_t i = 0; i < audience.size(); ++i)
        sendToUser(static_cast<int32_t>(audience[i]), cmd_message_notify, notify);
    sendToUser(m_userinfo.userid, cmd_message_notify, notify);
    return msgId;
}

void ChatSession::onThreadListPosts(const std::string& body)
{
    ListPostsRequest req;
    if (!ThreadProtocol::parseListPostsRequest(body, req))
    {
        send(cmd_thread_list_posts, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(req.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_list_posts, m_seq, ThreadProtocol::buildSimpleResponse(access, "no access"));
        return;
    }
    std::vector<ThreadPost> posts;
    std::string nextCursor;
    bool hasMore = false;
    if (!Singleton<ThreadManager>::Instance().listPosts(req.groupid, req.sort, req.cursor, req.limit, posts, nextCursor,
                                                        hasMore))
    {
        send(cmd_thread_list_posts, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_server_internal, "list posts failed"));
        return;
    }
    fillPostExtras(posts, m_userinfo.userid);
    send(cmd_thread_list_posts, m_seq,
         ThreadProtocol::buildListPostsResponse(error_code_ok, "ok", posts, nextCursor, hasMore));
    LOGI("thread list posts ok, userid: %d, groupid: %lld, sort: %d, count: %u, hasmore: %d", m_userinfo.userid,
         (long long)req.groupid, req.sort, static_cast<unsigned>(posts.size()), hasMore ? 1 : 0);
}

void ChatSession::onThreadGetPost(const std::string& body)
{
    int64_t postid = 0;
    if (!ThreadProtocol::parsePostIdRequest(body, postid))
    {
        send(cmd_thread_get_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(postid, post, found))
    {
        send(cmd_thread_get_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_server_internal, "load failed"));
        return;
    }
    if (!found)
    {
        send(cmd_thread_get_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_thread_not_exist, "not exist"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(post.groupid, group, myrole);
    if (access != error_code_ok)
    {
        //不是本群成员时按「不存在」回，不向他透露别的群里有这条帖子
        const int code = (access == error_code_not_group_member) ? error_code_thread_not_exist : access;
        send(cmd_thread_get_post, m_seq, ThreadProtocol::buildSimpleResponse(code, "no access"));
        return;
    }
    mgr.listAddenda(postid, post.addenda);
    std::vector<ThreadPost> one(1, post);
    fillPostExtras(one, m_userinfo.userid);
    send(cmd_thread_get_post, m_seq, ThreadProtocol::buildPostResponse(error_code_ok, "ok", &one[0], true));
}

void ChatSession::onThreadPublishPost(const std::string& body)
{
    PostWriteRequest req;
    if (!ThreadProtocol::parsePublishPostRequest(body, req))
    {
        send(cmd_thread_publish_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    const int valid = ThreadProtocol::validateBody(req.body, ThreadProtocol::kBodyPost);
    if (valid != error_code_ok)
    {
        send(cmd_thread_publish_post, m_seq, ThreadProtocol::buildSimpleResponse(valid, "invalid post"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int speak = checkThreadSpeak(cmd_thread_publish_post, req.groupid, group, myrole);
    if (speak != error_code_ok)
    {
        send(cmd_thread_publish_post, m_seq, ThreadProtocol::buildSimpleResponse(speak, "cannot post"));
        return;
    }
    //@所有人：与普通群发消息同一条规则（群设置「谁可以 @ 全体」，docs/帖子群方案.md §15.1 第 1 条）
    if (req.body.mentionall && !GroupProtocol::canMentionAll(group.atallscope, myrole))
    {
        send(cmd_thread_publish_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_at_all_denied, "only owner or admin can mention all"));
        LOGI("thread post mention all denied, userid: %d, groupid: %lld, role: %d", m_userinfo.userid,
             (long long)req.groupid, myrole);
        return;
    }

    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    //去重：应答超时后客户端用同一个 clientpostid 重发，回已有的那条
    int64_t existing = 0;
    if (mgr.findPostByClientId(m_userinfo.userid, req.clientId, existing) && existing > 0)
    {
        ThreadPost post;
        bool found = false;
        if (mgr.getPost(existing, post, found) && found)
        {
            std::vector<ThreadPost> one(1, post);
            fillPostExtras(one, m_userinfo.userid);
            send(cmd_thread_publish_post, m_seq, ThreadProtocol::buildPostResponse(error_code_ok, "duplicate", &one[0], true));
            return;
        }
    }

    if (hitSensitive(chatserver::sensitive_source_thread_post, conversation_group, req.groupid, group.name,
                     sensitiveTextsOf(req.body)))
    {
        send(cmd_thread_publish_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_group_sensitive_word, "post contains sensitive word"));
        return;
    }

    ThreadProtocol::filterMentions(req.body.mentions, groupMemberIds(req.groupid));
    ThreadProtocol::sanitizeMentionSpans(req.body);
    const int64_t now = threadNowMs();
    ThreadPost post;
    post.groupid = req.groupid;
    post.author = m_userinfo.userid;
    post.clientPostId = req.clientId;
    post.body = req.body;
    post.createTime = now;
    post.activeTime = now;
    if (!mgr.insertPost(post))
    {
        send(cmd_thread_publish_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_server_internal, "save post failed"));
        return;
    }
    //帖子引用的图片：首次被引用时把文件记录标记为已完成（只有上传者本人的标记生效）
    for (size_t i = 0; i < post.body.images.size(); ++i)
        markReferencedFileCompleted(static_cast<int64_t>(atoll(post.body.images[i].fileid.c_str())), "thread post");

    send(cmd_thread_publish_post, m_seq, ThreadProtocol::buildPostResponse(error_code_ok, "ok", &post, true));

    //帖子卡片：全员可见、对作者以外的人计未读；写成之后记下 msgid，删帖时据此改写
    post.cardMsgId = sendThreadPostCard(post);
    if (post.cardMsgId > 0)
        mgr.setPostCardMsgId(post.postid, post.cardMsgId);

    notifyGroupMembers(post.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(post.groupid, thread_change_post_new, m_userinfo.userid, &post,
                                                          post.postid, NULL, 0, NULL, 0, 0, post.activeTime, NULL));
    LOGI("thread post published, userid: %d, groupid: %lld, postid: %lld, images: %u, mentions: %u, card msgid: %lld",
         m_userinfo.userid, (long long)post.groupid, (long long)post.postid,
         static_cast<unsigned>(post.body.images.size()), static_cast<unsigned>(post.body.mentions.size()),
         (long long)post.cardMsgId);
}

void ChatSession::onThreadEditPost(const std::string& body)
{
    PostWriteRequest req;
    if (!ThreadProtocol::parseEditPostRequest(body, req))
    {
        send(cmd_thread_edit_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    const int valid = ThreadProtocol::validateBody(req.body, ThreadProtocol::kBodyPost);
    if (valid != error_code_ok)
    {
        send(cmd_thread_edit_post, m_seq, ThreadProtocol::buildSimpleResponse(valid, "invalid post"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(req.postid, post, found) || !found)
    {
        send(cmd_thread_edit_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_thread_not_exist, "not exist"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int speak = checkThreadSpeak(cmd_thread_edit_post, post.groupid, group, myrole);
    if (speak != error_code_ok)
    {
        send(cmd_thread_edit_post, m_seq, ThreadProtocol::buildSimpleResponse(speak, "cannot edit"));
        return;
    }
    if (!ThreadProtocol::canEditPost(post.author, m_userinfo.userid))
    {
        send(cmd_thread_edit_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_thread_no_permission, "only author can edit"));
        return;
    }
    //@所有人：编辑后的内容同样按群设置判定（作者被撤销管理员后，编辑时须去掉「@所有人」）
    if (req.body.mentionall && !GroupProtocol::canMentionAll(group.atallscope, myrole))
    {
        send(cmd_thread_edit_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_at_all_denied, "only owner or admin can mention all"));
        LOGI("thread post edit mention all denied, userid: %d, postid: %lld, role: %d", m_userinfo.userid,
             (long long)post.postid, myrole);
        return;
    }
    if (hitSensitive(chatserver::sensitive_source_thread_post, conversation_group, post.groupid, group.name,
                     sensitiveTextsOf(req.body)))
    {
        send(cmd_thread_edit_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_group_sensitive_word, "post contains sensitive word"));
        return;
    }
    ThreadProtocol::filterMentions(req.body.mentions, groupMemberIds(post.groupid));
    ThreadProtocol::sanitizeMentionSpans(req.body);
    const int64_t now = threadNowMs();
    if (!mgr.updatePostBody(post.postid, req.body, now))
    {
        send(cmd_thread_edit_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_server_internal, "save post failed"));
        return;
    }
    post.body = req.body;
    post.editTime = now;
    for (size_t i = 0; i < post.body.images.size(); ++i)
        markReferencedFileCompleted(static_cast<int64_t>(atoll(post.body.images[i].fileid.c_str())), "thread post");

    //帖子卡片的标题与摘要跟着改写，并向群成员推 2721：在线成员本地聊天记录里的卡片立即换成新标题、新摘要，
    //离线成员登录后经 cmd_sync_rewritten_messages 补上（docs/消息改写离线同步方案.md 5.3）。
    //会话列表摘要不另推送，等下一条消息时自然刷新
    if (post.cardMsgId > 0)
    {
        const std::string cardContent = ThreadProtocol::buildPostCardContent(post);
        std::vector<int64_t> noMentions;
        if (Singleton<MessageManager>::Instance().updateMessageContent(
                post.cardMsgId, MessageProtocol::buildStoredContent(cardContent, noMentions, false, "")))
        {
            ThreadCardState card;
            card.msgid = post.cardMsgId;
            card.msgtype = message_type_thread_post;
            card.content = cardContent;
            notifyThreadCards(post.groupid, std::vector<ThreadCardState>(1, card));
        }
        else
        {
            LOGE("thread post edited but card rewrite failed, postid: %lld, card msgid: %lld", (long long)post.postid,
                 (long long)post.cardMsgId);
        }
    }

    std::vector<ThreadPost> one(1, post);
    fillPostExtras(one, m_userinfo.userid);
    send(cmd_thread_edit_post, m_seq, ThreadProtocol::buildPostResponse(error_code_ok, "ok", &one[0], true));
    notifyGroupMembers(post.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(post.groupid, thread_change_post_edit, m_userinfo.userid,
                                                          &one[0], post.postid, NULL, 0, NULL, 0, -1, 0, NULL));
    LOGI("thread post edited, userid: %d, groupid: %lld, postid: %lld", m_userinfo.userid, (long long)post.groupid,
         (long long)post.postid);
}

void ChatSession::onThreadDeletePost(const std::string& body)
{
    int64_t postid = 0;
    if (!ThreadProtocol::parsePostIdRequest(body, postid))
    {
        send(cmd_thread_delete_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(postid, post, found) || !found)
    {
        send(cmd_thread_delete_post, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_not_exist, "not exist", "postid", postid));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(post.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_delete_post, m_seq, ThreadProtocol::buildIdResponse(access, "no access", "postid", postid));
        return;
    }
    if (!ThreadProtocol::canDeleteContent(post.author, m_userinfo.userid, myrole))
    {
        send(cmd_thread_delete_post, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_no_permission, "no permission", "postid", postid));
        return;
    }
    //删帖会在同一个事务里删掉留言、回复与补充的行，之后就查不到它们的动态卡片了，所以先取出来
    std::vector<int64_t> cardIds;
    if (!mgr.replyCardMsgIdsOfPost(postid, cardIds))
        LOGE("thread post delete: load reply card ids failed, postid: %lld", (long long)postid);
    if (!mgr.deletePost(postid))
    {
        send(cmd_thread_delete_post, m_seq,
             ThreadProtocol::buildIdResponse(error_code_server_internal, "delete failed", "postid", postid));
        return;
    }
    send(cmd_thread_delete_post, m_seq, ThreadProtocol::buildIdResponse(error_code_ok, "ok", "postid", postid));

    //帖子卡片与该帖的全部动态卡片改写为已删除形态（与群公告卡片的做法相同，docs/帖子群方案.md 14.6）。
    //改写在删除提交之后进行，改写失败只记日志，删除本身不回滚
    if (post.cardMsgId > 0)
        cardIds.insert(cardIds.begin(), post.cardMsgId);
    std::vector<ThreadCardState> rewritten;
    for (size_t i = 0; i < cardIds.size(); ++i)
    {
        ThreadCardState state;
        if (rewriteThreadCardDeleted(cardIds[i], state))
            rewritten.push_back(state);
    }
    notifyGroupMembers(post.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(post.groupid, thread_change_post_delete, m_userinfo.userid,
                                                          NULL, postid, NULL, 0, NULL, 0, -1, 0, NULL));
    notifyThreadCards(post.groupid, rewritten);
    LOGI("thread post deleted, userid: %d, role: %d, groupid: %lld, postid: %lld, author: %lld, cards rewritten: %u/%u",
         m_userinfo.userid, myrole, (long long)post.groupid, (long long)postid, (long long)post.author,
         static_cast<unsigned>(rewritten.size()), static_cast<unsigned>(cardIds.size()));
}

void ChatSession::onThreadPinPost(const std::string& body)
{
    int64_t postid = 0;
    bool pin = false;
    if (!ThreadProtocol::parsePinRequest(body, postid, pin))
    {
        send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(postid, post, found) || !found)
    {
        send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_thread_not_exist, "not exist"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(post.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildSimpleResponse(access, "no access"));
        return;
    }
    if (!ThreadProtocol::canPin(myrole))
    {
        send(cmd_thread_pin_post, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_thread_no_permission, "only owner or admin can pin"));
        return;
    }
    //置顶已满：已经置顶的帖子再置顶一次不算新增
    if (pin && post.pinTime == 0)
    {
        int pins = 0;
        if (!mgr.countPins(post.groupid, pins))
        {
            send(cmd_thread_pin_post, m_seq,
                 ThreadProtocol::buildSimpleResponse(error_code_server_internal, "count pins failed"));
            return;
        }
        if (pins >= kThreadMaxPins)
        {
            send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_thread_pin_full, "pin full"));
            return;
        }
    }
    const int64_t pinTime = pin ? (post.pinTime > 0 ? post.pinTime : threadNowMs()) : 0;
    if (!mgr.setPostPin(postid, pinTime))
    {
        send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildSimpleResponse(error_code_server_internal, "pin failed"));
        return;
    }
    post.pinTime = pinTime;
    std::vector<ThreadPost> one(1, post);
    fillPostExtras(one, m_userinfo.userid);
    send(cmd_thread_pin_post, m_seq, ThreadProtocol::buildPostResponse(error_code_ok, "ok", &one[0], false));
    notifyGroupMembers(post.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(post.groupid, thread_change_post_pin, m_userinfo.userid,
                                                          &one[0], postid, NULL, 0, NULL, 0, -1, 0, NULL));
    LOGI("thread post pin changed, userid: %d, groupid: %lld, postid: %lld, pin: %d", m_userinfo.userid,
         (long long)post.groupid, (long long)postid, pin ? 1 : 0);
}

void ChatSession::onThreadListComments(const std::string& body)
{
    ListCommentsRequest req;
    const std::vector<ThreadComment> kNone;
    if (!ThreadProtocol::parseListCommentsRequest(body, req))
    {
        send(cmd_thread_list_comments, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_bad_request, "bad request", 0, kNone, 0, false));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(req.postid, post, found) || !found)
    {
        send(cmd_thread_list_comments, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_thread_not_exist, "not exist", 0, kNone, 0, false));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(post.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_list_comments, m_seq,
             ThreadProtocol::buildCommentsResponse(access, "no access", 0, kNone, 0, false));
        return;
    }
    std::vector<ThreadComment> comments;
    int64_t nextCursor = 0;
    bool hasMore = false;
    if (!mgr.listComments(req.postid, req.cursor, req.limit, comments, nextCursor, hasMore))
    {
        send(cmd_thread_list_comments, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_server_internal, "list failed", 0, kNone, 0, false));
        return;
    }
    fillCommentReactions(comments, m_userinfo.userid);
    send(cmd_thread_list_comments, m_seq,
         ThreadProtocol::buildCommentsResponse(error_code_ok, "ok", post.author, comments, nextCursor, hasMore));
}

void ChatSession::onThreadListReplies(const std::string& body)
{
    ListCommentsRequest req;
    const std::vector<ThreadComment> kNone;
    if (!ThreadProtocol::parseListRepliesRequest(body, req))
    {
        send(cmd_thread_list_replies, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_bad_request, "bad request", 0, kNone, 0, false));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadComment parent;
    bool found = false;
    if (!mgr.getComment(req.commentid, parent, found) || !found)
    {
        send(cmd_thread_list_replies, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_thread_not_exist, "not exist", 0, kNone, 0, false));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(parent.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_list_replies, m_seq,
             ThreadProtocol::buildCommentsResponse(access, "no access", 0, kNone, 0, false));
        return;
    }
    std::vector<ThreadComment> replies;
    int64_t nextCursor = 0;
    bool hasMore = false;
    if (!mgr.listReplies(req.commentid, req.cursor, req.limit, replies, nextCursor, hasMore))
    {
        send(cmd_thread_list_replies, m_seq,
             ThreadProtocol::buildCommentsResponse(error_code_server_internal, "list failed", 0, kNone, 0, false));
        return;
    }
    fillCommentReactions(replies, m_userinfo.userid);
    ThreadPost post;
    bool postFound = false;
    mgr.getPost(parent.postid, post, postFound);
    send(cmd_thread_list_replies, m_seq,
         ThreadProtocol::buildCommentsResponse(error_code_ok, "ok", post.author, replies, nextCursor, hasMore));
}

void ChatSession::onThreadAddComment(const std::string& body)
{
    AddCommentRequest req;
    if (!ThreadProtocol::parseAddCommentRequest(body, req))
    {
        send(cmd_thread_add_comment, m_seq,
             ThreadProtocol::buildCommentResponse(error_code_bad_request, "bad request", NULL));
        return;
    }
    const int valid = ThreadProtocol::validateBody(req.body, ThreadProtocol::kBodyComment);
    if (valid != error_code_ok)
    {
        send(cmd_thread_add_comment, m_seq, ThreadProtocol::buildCommentResponse(valid, "invalid comment", NULL));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(req.postid, post, found) || !found)
    {
        send(cmd_thread_add_comment, m_seq,
             ThreadProtocol::buildCommentResponse(error_code_thread_not_exist, "post not exist", NULL));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int speak = checkThreadSpeak(cmd_thread_add_comment, post.groupid, group, myrole);
    if (speak != error_code_ok)
    {
        send(cmd_thread_add_comment, m_seq, ThreadProtocol::buildCommentResponse(speak, "cannot comment", NULL));
        return;
    }
    //@所有人：与发帖同一条规则
    if (req.body.mentionall && !GroupProtocol::canMentionAll(group.atallscope, myrole))
    {
        send(cmd_thread_add_comment, m_seq,
             ThreadProtocol::buildCommentResponse(error_code_at_all_denied, "only owner or admin can mention all", NULL));
        LOGI("thread comment mention all denied, userid: %d, postid: %lld, role: %d", m_userinfo.userid,
             (long long)post.postid, myrole);
        return;
    }

    //回复：被回复的若本身是回复，挂到它所属的留言下（只有两级），被回复的人取它的作者
    ThreadComment c;
    c.postid = post.postid;
    c.groupid = post.groupid;
    c.author = m_userinfo.userid;
    c.clientCommentId = req.clientId;
    c.body = req.body;
    int kind = thread_reply_comment;
    if (req.parentid > 0)
    {
        ThreadComment target;
        bool targetFound = false;
        if (!mgr.getComment(req.parentid, target, targetFound) || !targetFound || target.postid != post.postid
            || target.deleted)
        {
            send(cmd_thread_add_comment, m_seq,
                 ThreadProtocol::buildCommentResponse(error_code_thread_not_exist, "comment not exist", NULL));
            return;
        }
        c.parentid = (target.parentid > 0) ? target.parentid : target.commentid;
        c.replyTo = target.author;
        kind = thread_reply_reply;
    }

    //去重：应答超时后客户端用同一个 clientcommentid 重发，回已有的那条
    int64_t existing = 0;
    if (mgr.findCommentByClientId(m_userinfo.userid, req.clientId, existing) && existing > 0)
    {
        ThreadComment old;
        bool oldFound = false;
        if (mgr.getComment(existing, old, oldFound) && oldFound)
        {
            send(cmd_thread_add_comment, m_seq, ThreadProtocol::buildCommentResponse(error_code_ok, "duplicate", &old));
            return;
        }
    }

    if (hitSensitive(chatserver::sensitive_source_thread_comment, conversation_group, post.groupid, group.name,
                     sensitiveTextsOf(req.body)))
    {
        send(cmd_thread_add_comment, m_seq,
             ThreadProtocol::buildCommentResponse(error_code_group_sensitive_word, "comment contains sensitive word",
                                                  NULL));
        return;
    }
    const std::vector<int64_t> members = groupMemberIds(post.groupid);
    ThreadProtocol::filterMentions(c.body.mentions, members);
    ThreadProtocol::sanitizeMentionSpans(c.body);
    c.createTime = threadNowMs();
    if (!mgr.insertComment(c))
    {
        send(cmd_thread_add_comment, m_seq,
             ThreadProtocol::buildCommentResponse(error_code_server_internal, "save comment failed", NULL));
        return;
    }
    send(cmd_thread_add_comment, m_seq, ThreadProtocol::buildCommentResponse(error_code_ok, "ok", &c));

    //帖子动态卡片：留言提醒楼主，回复提醒楼主与被回复的人，另加 @ 到的人
    std::vector<int64_t> audience;
    const std::vector<int64_t> noCommenters;
    ThreadProtocol::replyCardAudience(kind, post.author, c.replyTo, noCommenters, c.body.mentions, m_userinfo.userid,
                                      audience);
    //@所有人：全体成员都要收到这张卡片与 @ 提醒
    if (c.body.mentionall)
        ThreadProtocol::mergeMentionAllAudience(members, m_userinfo.userid, audience);
    const std::vector<ThreadImage> noImages;
    const int64_t cardMsgId = sendThreadReplyCard(
        post.groupid, post.postid, c.commentid, 0, ThreadProtocol::displayTitle(post),
        ThreadProtocol::makeDigest(c.body.text, kThreadDigestChars), kind, audience, c.body.mentions, c.createTime,
        ThreadProtocol::commentCardClientMsgId(c.commentid), noImages, c.body.mentionall);
    //记下卡片 msgid：删除这条留言或回复、或删帖时据此把卡片改写为已删除形态（docs/帖子群方案.md 14.6）
    if (cardMsgId > 0 && !mgr.setCommentCardMsgId(c.commentid, cardMsgId))
        LOGE("thread comment card msgid not saved, commentid: %lld, msgid: %lld", (long long)c.commentid,
             (long long)cardMsgId);

    int commentCount = post.commentCount + 1;
    int64_t activeTime = c.createTime;
    mgr.getPostCounters(post.postid, commentCount, activeTime);
    notifyThreadWatchers(post.groupid,
                         ThreadProtocol::buildChangedNotify(post.groupid, thread_change_comment_new, m_userinfo.userid,
                                                            NULL, post.postid, &c, c.commentid, NULL, 0, commentCount,
                                                            activeTime, NULL));
    LOGI("thread comment added, userid: %d, groupid: %lld, postid: %lld, commentid: %lld, parentid: %lld, "
         "audience: %u, card msgid: %lld",
         m_userinfo.userid, (long long)post.groupid, (long long)post.postid, (long long)c.commentid,
         (long long)c.parentid, static_cast<unsigned>(audience.size()), (long long)cardMsgId);
}

void ChatSession::onThreadDeleteComment(const std::string& body)
{
    int64_t commentid = 0;
    if (!ThreadProtocol::parseCommentIdRequest(body, commentid))
    {
        send(cmd_thread_delete_comment, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadComment c;
    bool found = false;
    if (!mgr.getComment(commentid, c, found) || !found || c.deleted)
    {
        send(cmd_thread_delete_comment, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_not_exist, "not exist", "commentid", commentid));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(c.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_delete_comment, m_seq,
             ThreadProtocol::buildIdResponse(access, "no access", "commentid", commentid));
        return;
    }
    if (!ThreadProtocol::canDeleteContent(c.author, m_userinfo.userid, myrole))
    {
        send(cmd_thread_delete_comment, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_no_permission, "no permission", "commentid", commentid));
        return;
    }
    bool keptPlaceholder = false;
    if (!mgr.deleteComment(c, keptPlaceholder))
    {
        send(cmd_thread_delete_comment, m_seq,
             ThreadProtocol::buildIdResponse(error_code_server_internal, "delete failed", "commentid", commentid));
        return;
    }
    send(cmd_thread_delete_comment, m_seq, ThreadProtocol::buildIdResponse(error_code_ok, "ok", "commentid", commentid));

    int commentCount = -1;
    int64_t activeTime = 0;
    mgr.getPostCounters(c.postid, commentCount, activeTime);
    //推送里带上删除后的这一条：保留占位时 deleted 为 1，客户端据此把它显示为「该留言已删除」
    c.deleted = keptPlaceholder;
    if (keptPlaceholder)
        c.body = ThreadBody();
    notifyThreadWatchers(c.groupid,
                         ThreadProtocol::buildChangedNotify(c.groupid, thread_change_comment_delete, m_userinfo.userid,
                                                            NULL, c.postid, keptPlaceholder ? &c : NULL, commentid,
                                                            NULL, 0, commentCount, 0, NULL));

    //这条留言或回复的动态卡片改写为已删除形态，被删的原文不再出现在会话摘要与消息记录里（docs/帖子群方案.md 14.6）
    std::vector<ThreadCardState> rewritten;
    ThreadCardState state;
    if (c.cardMsgId > 0 && rewriteThreadCardDeleted(c.cardMsgId, state))
        rewritten.push_back(state);
    notifyThreadCards(c.groupid, rewritten);
    LOGI("thread comment deleted, userid: %d, role: %d, groupid: %lld, commentid: %lld, placeholder: %d, "
         "card msgid: %lld, card rewritten: %d",
         m_userinfo.userid, myrole, (long long)c.groupid, (long long)commentid, keptPlaceholder ? 1 : 0,
         (long long)c.cardMsgId, rewritten.empty() ? 0 : 1);
}

void ChatSession::onThreadReact(const std::string& body)
{
    ReactRequest req;
    const std::vector<ThreadReaction> kNone;
    if (!ThreadProtocol::parseReactRequest(body, req))
    {
        send(cmd_thread_react, m_seq, ThreadProtocol::buildReactResponse(error_code_bad_request, "bad request", 0, 0, kNone));
        return;
    }
    if (!ThreadProtocol::isValidReaction(req.reaction))
    {
        send(cmd_thread_react, m_seq,
             ThreadProtocol::buildReactResponse(error_code_thread_invalid, "bad reaction", req.targettype, req.targetid,
                                                kNone));
        return;
    }
    //找到目标所属的帖子群；已删除的留言（占位）不能互动
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    int64_t groupid = 0;
    int64_t postid = 0;
    bool found = false;
    if (req.targettype == thread_target_post)
    {
        ThreadPost post;
        if (mgr.getPost(req.targetid, post, found) && found)
        {
            groupid = post.groupid;
            postid = post.postid;
        }
    }
    else
    {
        ThreadComment c;
        if (mgr.getComment(req.targetid, c, found) && found && !c.deleted)
        {
            groupid = c.groupid;
            postid = c.postid;
        }
    }
    if (groupid == 0)
    {
        send(cmd_thread_react, m_seq,
             ThreadProtocol::buildReactResponse(error_code_thread_not_exist, "not exist", req.targettype, req.targetid,
                                                kNone));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_react, m_seq,
             ThreadProtocol::buildReactResponse(access, "no access", req.targettype, req.targetid, kNone));
        return;
    }
    if (!mgr.setReaction(req.targettype, req.targetid, groupid, m_userinfo.userid, req.reaction, req.on, threadNowMs()))
    {
        send(cmd_thread_react, m_seq,
             ThreadProtocol::buildReactResponse(error_code_server_internal, "save failed", req.targettype, req.targetid,
                                                kNone));
        return;
    }
    std::map<int64_t, std::vector<ThreadReaction> > summary;
    mgr.loadReactions(req.targettype, std::vector<int64_t>(1, req.targetid), m_userinfo.userid, summary);
    const std::vector<ThreadReaction>& mine = summary[req.targetid];
    send(cmd_thread_react, m_seq, ThreadProtocol::buildReactResponse(error_code_ok, "ok", req.targettype, req.targetid, mine));

    //推给正在查看的人：mine 一律置 false，看的人按 operator 是不是自己判断
    ThreadProtocol::ReactChange change;
    change.targettype = req.targettype;
    change.targetid = req.targetid;
    change.reaction = req.reaction;
    change.on = req.on;
    change.summary = mine;
    for (size_t i = 0; i < change.summary.size(); ++i)
        change.summary[i].mine = false;
    notifyThreadWatchers(groupid, ThreadProtocol::buildChangedNotify(groupid, thread_change_reaction, m_userinfo.userid,
                                                                     NULL, postid, NULL, 0, NULL, 0, -1, 0, &change));
}

void ChatSession::onThreadGetReactors(const std::string& body)
{
    ReactRequest req;
    const std::vector<int64_t> kNone;
    if (!ThreadProtocol::parseGetReactorsRequest(body, req) || !ThreadProtocol::isValidReaction(req.reaction))
    {
        send(cmd_thread_get_reactors, m_seq,
             ThreadProtocol::buildReactorsResponse(error_code_bad_request, "bad request", 0, 0, 0, kNone, 0));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    int64_t groupid = 0;
    bool found = false;
    if (req.targettype == thread_target_post)
    {
        ThreadPost post;
        if (mgr.getPost(req.targetid, post, found) && found)
            groupid = post.groupid;
    }
    else
    {
        ThreadComment c;
        if (mgr.getComment(req.targetid, c, found) && found)
            groupid = c.groupid;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = (groupid == 0) ? error_code_thread_not_exist : checkThreadAccess(groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_get_reactors, m_seq,
             ThreadProtocol::buildReactorsResponse(access, "no access", req.targettype, req.targetid, req.reaction, kNone, 0));
        return;
    }
    std::vector<int64_t> users;
    int total = 0;
    if (!mgr.listReactors(req.targettype, req.targetid, req.reaction, req.limit, users, total))
    {
        send(cmd_thread_get_reactors, m_seq,
             ThreadProtocol::buildReactorsResponse(error_code_server_internal, "list failed", req.targettype, req.targetid,
                                                   req.reaction, kNone, 0));
        return;
    }
    send(cmd_thread_get_reactors, m_seq,
         ThreadProtocol::buildReactorsResponse(error_code_ok, "ok", req.targettype, req.targetid, req.reaction, users,
                                               total));
}

void ChatSession::onThreadWatch(const std::string& body)
{
    int64_t groupid = 0;
    bool watch = false;
    if (!ThreadProtocol::parseWatchRequest(body, groupid, watch))
    {
        send(cmd_thread_watch, m_seq, ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadWatchRegistry& reg = Singleton<ThreadWatchRegistry>::Instance();
    //撤销不做任何检查：窗口关闭时群可能已经被解散、本人可能已经不在群里
    if (!watch)
    {
        reg.unwatch(groupid, p2pConnKey());
        send(cmd_thread_watch, m_seq, ThreadProtocol::buildSimpleResponse(error_code_ok, "ok"));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_watch, m_seq, ThreadProtocol::buildSimpleResponse(access, "no access"));
        return;
    }
    reg.watch(groupid, p2pConnKey(), m_userinfo.userid);
    send(cmd_thread_watch, m_seq, ThreadProtocol::buildSimpleResponse(error_code_ok, "ok"));
    LOGI("thread watch, userid: %d, groupid: %lld", m_userinfo.userid, (long long)groupid);
}

void ChatSession::onThreadAddAddendum(const std::string& body)
{
    PostWriteRequest req;
    if (!ThreadProtocol::parseAddAddendumRequest(body, req))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_bad_request, "bad request", NULL));
        return;
    }
    const int valid = ThreadProtocol::validateBody(req.body, ThreadProtocol::kBodyAddendum);
    if (valid != error_code_ok)
    {
        send(cmd_thread_add_addendum, m_seq, ThreadProtocol::buildAddendumResponse(valid, "invalid addendum", NULL));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadPost post;
    bool found = false;
    if (!mgr.getPost(req.postid, post, found) || !found)
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_thread_not_exist, "post not exist", NULL));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int speak = checkThreadSpeak(cmd_thread_add_addendum, post.groupid, group, myrole);
    if (speak != error_code_ok)
    {
        send(cmd_thread_add_addendum, m_seq, ThreadProtocol::buildAddendumResponse(speak, "cannot add", NULL));
        return;
    }
    if (!ThreadProtocol::canAddAddendum(post.author, m_userinfo.userid))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_thread_no_permission, "only author can add", NULL));
        return;
    }
    //@所有人：与发帖同一条规则
    if (req.body.mentionall && !GroupProtocol::canMentionAll(group.atallscope, myrole))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_at_all_denied, "only owner or admin can mention all",
                                                   NULL));
        LOGI("thread addendum mention all denied, userid: %d, postid: %lld, role: %d", m_userinfo.userid,
             (long long)post.postid, myrole);
        return;
    }
    std::vector<ThreadAddendum> existing;
    if (!mgr.listAddenda(post.postid, existing))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_server_internal, "load failed", NULL));
        return;
    }
    if (static_cast<int>(existing.size()) >= kThreadMaxAddenda)
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_thread_addendum_full, "addendum full", NULL));
        return;
    }
    if (hitSensitive(chatserver::sensitive_source_thread_post, conversation_group, post.groupid, group.name,
                     sensitiveTextsOf(req.body)))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_group_sensitive_word, "addendum contains sensitive word",
                                                   NULL));
        return;
    }
    const std::vector<int64_t> members = groupMemberIds(post.groupid);
    ThreadProtocol::filterMentions(req.body.mentions, members);
    ThreadProtocol::sanitizeMentionSpans(req.body);
    ThreadAddendum a;
    a.postid = post.postid;
    a.groupid = post.groupid;
    a.author = m_userinfo.userid;
    a.body = req.body;
    a.createTime = threadNowMs();
    if (!mgr.insertAddendum(a))
    {
        send(cmd_thread_add_addendum, m_seq,
             ThreadProtocol::buildAddendumResponse(error_code_server_internal, "save failed", NULL));
        return;
    }
    for (size_t i = 0; i < a.body.images.size(); ++i)
        markReferencedFileCompleted(static_cast<int64_t>(atoll(a.body.images[i].fileid.c_str())), "thread addendum");
    send(cmd_thread_add_addendum, m_seq, ThreadProtocol::buildAddendumResponse(error_code_ok, "ok", &a));

    //帖子动态卡片：提醒在该帖下留过言或回复过的人，另加 @ 到的人
    std::vector<int64_t> commenters;
    mgr.commentersOfPost(post.postid, commenters);
    std::vector<int64_t> audience;
    ThreadProtocol::replyCardAudience(thread_reply_addendum, post.author, 0, commenters, a.body.mentions,
                                      m_userinfo.userid, audience);
    //@所有人：全体成员都要收到这张卡片与 @ 提醒
    if (a.body.mentionall)
        ThreadProtocol::mergeMentionAllAudience(members, m_userinfo.userid, audience);
    const int64_t cardMsgId = sendThreadReplyCard(
        post.groupid, post.postid, 0, a.addid, ThreadProtocol::displayTitle(post),
        ThreadProtocol::makeDigest(a.body.text, kThreadDigestChars), thread_reply_addendum, audience, a.body.mentions,
        a.createTime, ThreadProtocol::addendumCardClientMsgId(a.addid), a.body.images, a.body.mentionall);
    //记下卡片 msgid：删除这段补充或删帖时据此把卡片改写为已删除形态（docs/帖子群方案.md 14.6）
    if (cardMsgId > 0 && !mgr.setAddendumCardMsgId(a.addid, cardMsgId))
        LOGE("thread addendum card msgid not saved, addid: %lld, msgid: %lld", (long long)a.addid,
             (long long)cardMsgId);

    notifyGroupMembers(post.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(post.groupid, thread_change_addendum_new, m_userinfo.userid,
                                                          NULL, post.postid, NULL, 0, &a, a.addid, -1, a.createTime,
                                                          NULL));
    LOGI("thread addendum added, userid: %d, groupid: %lld, postid: %lld, addid: %lld, audience: %u, card msgid: %lld",
         m_userinfo.userid, (long long)post.groupid, (long long)post.postid, (long long)a.addid,
         static_cast<unsigned>(audience.size()), (long long)cardMsgId);
}

void ChatSession::onThreadDeleteAddendum(const std::string& body)
{
    int64_t addid = 0;
    if (!ThreadProtocol::parseAddendumIdRequest(body, addid))
    {
        send(cmd_thread_delete_addendum, m_seq,
             ThreadProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    ThreadManager& mgr = Singleton<ThreadManager>::Instance();
    ThreadAddendum a;
    bool found = false;
    if (!mgr.getAddendum(addid, a, found) || !found)
    {
        send(cmd_thread_delete_addendum, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_not_exist, "not exist", "addid", addid));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(a.groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_delete_addendum, m_seq, ThreadProtocol::buildIdResponse(access, "no access", "addid", addid));
        return;
    }
    if (!ThreadProtocol::canDeleteContent(a.author, m_userinfo.userid, myrole))
    {
        send(cmd_thread_delete_addendum, m_seq,
             ThreadProtocol::buildIdResponse(error_code_thread_no_permission, "no permission", "addid", addid));
        return;
    }
    if (!mgr.deleteAddendum(addid))
    {
        send(cmd_thread_delete_addendum, m_seq,
             ThreadProtocol::buildIdResponse(error_code_server_internal, "delete failed", "addid", addid));
        return;
    }
    send(cmd_thread_delete_addendum, m_seq, ThreadProtocol::buildIdResponse(error_code_ok, "ok", "addid", addid));
    notifyGroupMembers(a.groupid, cmd_thread_changed_notify,
                       ThreadProtocol::buildChangedNotify(a.groupid, thread_change_addendum_delete, m_userinfo.userid,
                                                          NULL, a.postid, NULL, 0, NULL, addid, -1, 0, NULL));

    //这段补充的动态卡片改写为已删除形态（docs/帖子群方案.md 14.6）
    std::vector<ThreadCardState> rewritten;
    ThreadCardState state;
    if (a.cardMsgId > 0 && rewriteThreadCardDeleted(a.cardMsgId, state))
        rewritten.push_back(state);
    notifyThreadCards(a.groupid, rewritten);
    LOGI("thread addendum deleted, userid: %d, role: %d, groupid: %lld, addid: %lld, card msgid: %lld, "
         "card rewritten: %d",
         m_userinfo.userid, myrole, (long long)a.groupid, (long long)addid, (long long)a.cardMsgId,
         rewritten.empty() ? 0 : 1);
}

void ChatSession::onThreadCheckCards(const std::string& body)
{
    int64_t groupid = 0;
    std::vector<int64_t> msgids;
    const std::vector<ThreadCardState> none;
    if (!ThreadProtocol::parseCheckCardsRequest(body, groupid, msgids))
    {
        send(cmd_thread_check_cards, m_seq,
             ThreadProtocol::buildCheckCardsResponse(error_code_bad_request, "bad request", groupid, none));
        return;
    }
    GroupRecord group;
    int myrole = -1;
    const int access = checkThreadAccess(groupid, group, myrole);
    if (access != error_code_ok)
    {
        send(cmd_thread_check_cards, m_seq, ThreadProtocol::buildCheckCardsResponse(access, "no access", groupid, none));
        return;
    }
    //按本人的可见范围取：只对别人可见的动态卡片、本人已删除的消息都取不到（visibleToUserClause）
    std::vector<MessageRecord> records;
    if (!Singleton<MessageManager>::Instance().getMessagesByIds(m_userinfo.userid, msgids, records))
    {
        send(cmd_thread_check_cards, m_seq,
             ThreadProtocol::buildCheckCardsResponse(error_code_server_internal, "load failed", groupid, none));
        return;
    }
    //只回本群的、已是已删除形态的帖子卡片与动态卡片；其余的客户端本地内容不用改
    std::vector<ThreadCardState> cards;
    for (size_t i = 0; i < records.size(); ++i)
    {
        const MessageRecord& rec = records[i];
        if (!ThreadProtocol::isDeletedCardOfGroup(rec.ctype, rec.to, rec.msgtype, rec.content, groupid))
            continue;
        ThreadCardState state;
        state.msgid = rec.msgid;
        state.msgtype = rec.msgtype;
        state.content = rec.content;
        cards.push_back(state);
    }
    send(cmd_thread_check_cards, m_seq, ThreadProtocol::buildCheckCardsResponse(error_code_ok, "ok", groupid, cards));
    LOGI("thread check cards, userid: %d, groupid: %lld, asked: %u, found: %u, deleted: %u", m_userinfo.userid,
         (long long)groupid, static_cast<unsigned>(msgids.size()), static_cast<unsigned>(records.size()),
         static_cast<unsigned>(cards.size()));
}

bool ChatSession::rewriteThreadCardDeleted(int64_t msgid, ThreadCardState& out)
{
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord card;
    if (!msgMgr.getMessageById(msgid, card))
    {
        LOGE("thread card rewrite: card not found, msgid: %lld", (long long)msgid);
        return false;
    }
    std::string deleted;
    bool built = false;
    if (card.msgtype == message_type_thread_post)
        built = ThreadProtocol::buildDeletedPostCardContent(card.content, deleted);
    else if (card.msgtype == message_type_thread_reply)
        built = ThreadProtocol::buildDeletedReplyCardContent(card.content, deleted);
    if (!built)
    {
        LOGE("thread card rewrite: not a thread card or bad content, msgid: %lld, msgtype: %d", (long long)msgid,
             card.msgtype);
        return false;
    }
    //落库时套上外壳；卡片的 @ 提醒已经发过，改写后不再带 mentions
    std::vector<int64_t> noMentions;
    if (!msgMgr.updateMessageContent(msgid, MessageProtocol::buildStoredContent(deleted, noMentions, false, "")))
    {
        LOGE("thread card rewrite: update failed, msgid: %lld", (long long)msgid);
        return false;
    }
    out.msgid = msgid;
    out.msgtype = card.msgtype;
    out.content = deleted;
    return true;
}

void ChatSession::notifyThreadCards(int64_t groupid, const std::vector<ThreadCardState>& cards)
{
    if (cards.empty())
        return;
    //推给全体成员：动态卡片只对一部分人可见，但改写后的内容不带正文，本地没有这张卡片的客户端按 msgid 找不到就跳过
    notifyGroupMembers(groupid, cmd_thread_cards_notify, ThreadProtocol::buildCardsNotify(groupid, cards));
}
