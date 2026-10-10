/**
 *  帖子群会话控制器 CThreadSession 的单元测试（标签 [threaddll]，无界面模式）：
 *    - TGD19 打开：登记「正在查看」并拉第一页，应答合并进列表并通知界面；翻页带上游标；
 *    - TGD20 选中帖子：拉完整内容与第一页留言；帖子不存在时标记已删除；过期的应答丢弃；
 *    - TGD21 发帖：应答成功后列表里有这条帖子并通知界面；随后的新帖推送不重复；失败时只通知界面；
 *    - TGD22 互动：点了立即更新，失败恢复、成功以应答为准；本人的推送不重复计数；
 *    - TGD23 留言：成功后详情里有这条留言、列表留言数加一；推送带绝对值，不重复计数；删除留言；
 *    - TGD24 断线：未完成的本人操作按超时通知界面，正在加载的标记清掉，之后可以重新加载；
 *    - TGD25 切换排序：旧排序的应答晚到时丢弃；补充的应答与推送只算一次。
 *  用假的发送出口记录发出的报文与 seq，测试按 seq 交回服务端形状的应答。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Core/ThreadSession.h"
#include "../Core/ThreadCodec.h"
#include "../Core/ThreadReactions.h"
#include "../Core/ThreadText.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

using namespace threadgroup;

namespace {

const int64_t kGroup = 7101;
const int64_t kMe = 9101;
const int64_t kOther = 9102;
const int64_t kBaseTime = 1791000000000LL;

//发出的一条报文
struct SentFrame
{
    int64_t     m_groupId;
    uint16_t    m_cmd;
    uint32_t    m_seq;
    std::string m_body;
};

//假的发送出口：记录报文，seq 从 1 递增；m_fail 为 true 时一律不发
class FakeSender : public IThreadSessionSender
{
public:
    FakeSender()
        : m_nextSeq(1)
        , m_fail(false)
    {
    }

    virtual uint32_t SendThreadCommand(int64_t groupId, uint16_t cmd, const std::string& body) override
    {
        if (m_fail)
            return 0;
        SentFrame f;
        f.m_groupId = groupId;
        f.m_cmd = cmd;
        f.m_seq = m_nextSeq++;
        f.m_body = body;
        m_frames.push_back(f);
        return f.m_seq;
    }

    virtual int64_t MyUserId() override
    {
        return kMe;
    }

    //最后一条某命令的报文；没有时返回 NULL
    const SentFrame* Last(uint16_t cmd) const
    {
        for (size_t i = m_frames.size(); i > 0; --i)
        {
            if (m_frames[i - 1].m_cmd == cmd)
                return &m_frames[i - 1];
        }
        return NULL;
    }

    //某命令发了多少条
    int Count(uint16_t cmd) const
    {
        int n = 0;
        for (size_t i = 0; i < m_frames.size(); ++i)
        {
            if (m_frames[i].m_cmd == cmd)
                ++n;
        }
        return n;
    }

    std::vector<SentFrame> m_frames;
    uint32_t               m_nextSeq;
    bool                   m_fail;
};

//一次操作结果
struct ActionRecord
{
    int     m_cmd;
    int     m_code;
    int64_t m_id;
};

//假的界面：数通知
class FakeView : public IThreadSessionView
{
public:
    FakeView()
        : m_feedChanged(0)
        , m_detailChanged(0)
        , m_reactorTotal(-1)
    {
    }

    virtual void OnFeedChanged() override
    {
        ++m_feedChanged;
    }

    virtual void OnDetailChanged() override
    {
        ++m_detailChanged;
    }

    virtual void OnActionDone(int cmd, int code, int64_t id) override
    {
        ActionRecord r;
        r.m_cmd = cmd;
        r.m_code = code;
        r.m_id = id;
        m_actions.push_back(r);
    }

    virtual void OnReactorsLoaded(int targetType, int64_t targetId, int reaction, const std::vector<int64_t>& users,
                                  int total) override
    {
        (void)targetType;
        (void)targetId;
        (void)reaction;
        m_reactors = users;
        m_reactorTotal = total;
    }

    int                       m_feedChanged;
    int                       m_detailChanged;
    std::vector<ActionRecord> m_actions;
    std::vector<int64_t>      m_reactors;
    int                       m_reactorTotal;
};

//写成紧凑 JSON
std::string Write(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//服务端形状的帖子对象
Json::Value PostJson(int64_t postId, int64_t activeTime, bool full)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["groupid"] = IdToString(kGroup);
    v["author"] = IdToString(kOther);
    v["title"] = "t";
    v["text"] = "body";
    v["texttruncated"] = 0;
    v["images"] = Json::Value(Json::arrayValue);
    v["imagecount"] = 0;
    v["mentions"] = Json::Value(Json::arrayValue);
    v["createtime"] = static_cast<Json::Int64>(activeTime);
    v["edittime"] = 0;
    v["activetime"] = static_cast<Json::Int64>(activeTime);
    v["commentcount"] = 0;
    v["pintime"] = 0;
    v["addendumcount"] = 0;
    v["reactions"] = Json::Value(Json::arrayValue);
    if (full)
        v["addenda"] = Json::Value(Json::arrayValue);
    return v;
}

//2701 应答
std::string ListResponse(const std::vector<int64_t>& ids, const std::string& next, bool more)
{
    Json::Value v(Json::objectValue);
    v["code"] = 0;
    v["msg"] = "ok";
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(PostJson(ids[i], kBaseTime + ids[i], false));
    v["posts"] = arr;
    v["nextcursor"] = next;
    v["hasmore"] = more ? 1 : 0;
    return Write(v);
}

//带一个帖子的应答（2702 / 2703 / 2704）
std::string PostResponse(int64_t postId, bool full)
{
    Json::Value v(Json::objectValue);
    v["code"] = 0;
    v["msg"] = "ok";
    v["post"] = PostJson(postId, kBaseTime + postId, full);
    return Write(v);
}

//只带 code 的应答
std::string CodeResponse(int code)
{
    Json::Value v(Json::objectValue);
    v["code"] = code;
    v["msg"] = "x";
    return Write(v);
}

//留言对象
Json::Value CommentJson(int64_t commentId, int64_t postId, int64_t parentId, int64_t author)
{
    Json::Value v(Json::objectValue);
    v["commentid"] = IdToString(commentId);
    v["postid"] = IdToString(postId);
    v["author"] = IdToString(author);
    v["parentid"] = IdToString(parentId);
    v["replyto"] = "0";
    v["text"] = "c";
    v["mentions"] = Json::Value(Json::arrayValue);
    v["createtime"] = static_cast<Json::Int64>(kBaseTime + commentId);
    v["deleted"] = 0;
    v["replycount"] = 0;
    v["reactions"] = Json::Value(Json::arrayValue);
    return v;
}

//打开会话并让第一页到达（帖子 ids），返回第一页请求的 seq
void OpenWithPage(CThreadSession& session, FakeSender& sender, const std::vector<int64_t>& ids)
{
    session.Start();
    const SentFrame* list = sender.Last(thread_cmd_list_posts);
    REQUIRE(list != NULL);
    REQUIRE(session.OnResponse(thread_cmd_list_posts, list->m_seq, ListResponse(ids, "", false)));
}

} // namespace

TEST_CASE("TGD19 打开：登记查看并拉第一页，翻页带游标", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    session.Start();
    REQUIRE(sender.Count(thread_cmd_watch) == 1);
    REQUIRE(sender.Count(thread_cmd_list_posts) == 1);
    CHECK(sender.m_frames[0].m_body.find("\"watch\":1") != std::string::npos);
    CHECK(sender.m_frames[0].m_groupId == kGroup);
    CHECK(session.Feed().IsLoading());

    std::vector<int64_t> ids;
    ids.push_back(3);
    ids.push_back(2);
    const uint32_t seq = sender.Last(thread_cmd_list_posts)->m_seq;
    REQUIRE(session.OnResponse(thread_cmd_list_posts, seq, ListResponse(ids, "a100_2", true)));
    CHECK(session.Feed().Posts().size() == 2);
    CHECK(view.m_feedChanged > 0);
    //watch 的应答被吞掉，不通知界面
    REQUIRE(session.OnResponse(thread_cmd_watch, sender.m_frames[0].m_seq, CodeResponse(0)));
    CHECK(view.m_actions.empty());
    //不认识的 seq 不处理
    CHECK_FALSE(session.OnResponse(thread_cmd_list_posts, 999, ListResponse(ids, "", false)));

    REQUIRE(session.LoadMore());
    CHECK_FALSE(session.LoadMore());
    CHECK(sender.Last(thread_cmd_list_posts)->m_body.find("\"cursor\":\"a100_2\"") != std::string::npos);
    std::vector<int64_t> more;
    more.push_back(1);
    session.OnResponse(thread_cmd_list_posts, sender.Last(thread_cmd_list_posts)->m_seq, ListResponse(more, "", false));
    CHECK(session.Feed().Posts().size() == 3);
    CHECK_FALSE(session.Feed().HasMore());
    CHECK_FALSE(session.LoadMore());

    //登记查看失败（企业关闭了帖子群）时告诉界面
    CThreadSession other(kGroup, &sender);
    FakeView otherView;
    other.SetView(&otherView);
    other.Start();
    other.OnResponse(thread_cmd_watch, sender.Last(thread_cmd_watch)->m_seq, CodeResponse(thread_error_disabled));
    REQUIRE(otherView.m_actions.size() == 1);
    CHECK(otherView.m_actions[0].m_code == thread_error_disabled);

    session.Stop();
    CHECK(sender.Last(thread_cmd_watch)->m_body.find("\"watch\":0") != std::string::npos);
    CHECK(session.PendingCount() == 0);
}

TEST_CASE("TGD20 选中帖子：拉完整内容与留言，不存在时标记已删除，过期应答丢弃", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    std::vector<int64_t> ids(1, 5);
    OpenWithPage(session, sender, ids);

    session.SelectPost(5);
    CHECK(session.Detail().PostId() == 5);
    CHECK(session.Detail().Post().m_title == L"t");   //先用摘要显示
    const SentFrame* get = sender.Last(thread_cmd_get_post);
    const SentFrame* comments = sender.Last(thread_cmd_list_comments);
    REQUIRE(get != NULL);
    REQUIRE(comments != NULL);
    const uint32_t getSeq = get->m_seq;
    const uint32_t commentsSeq = comments->m_seq;
    session.OnResponse(thread_cmd_get_post, getSeq, PostResponse(5, true));
    CHECK(session.Detail().IsPostLoaded());
    Json::Value list(Json::objectValue);
    list["code"] = 0;
    list["ownerid"] = IdToString(kOther);
    list["comments"] = Json::Value(Json::arrayValue);
    list["comments"].append(CommentJson(1, 5, 0, kOther));
    list["nextcursor"] = "1";
    list["hasmore"] = 0;
    session.OnResponse(thread_cmd_list_comments, commentsSeq, Write(list));
    CHECK(session.Detail().Comments().size() == 1);
    CHECK(session.Detail().IsCommentsLoaded());

    //切到另一条帖子后，前一条的应答晚到：丢弃
    session.SelectPost(6);
    const uint32_t getSeq6 = sender.Last(thread_cmd_get_post)->m_seq;
    session.SelectPost(5);
    session.OnResponse(thread_cmd_get_post, getSeq6, PostResponse(6, true));
    CHECK(session.Detail().PostId() == 5);
    CHECK_FALSE(session.Detail().IsPostLoaded());

    session.OnResponse(thread_cmd_get_post, sender.Last(thread_cmd_get_post)->m_seq,
                       CodeResponse(thread_error_not_exist));
    CHECK(session.Detail().IsDeleted());
}

TEST_CASE("TGD21 发帖：成功后列表有这条帖子，推送不重复；失败只通知", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    std::vector<int64_t> ids(1, 5);
    OpenWithPage(session, sender, ids);

    REQUIRE(session.PublishPost(L"标题", L"正文", std::vector<ThreadImage>(), std::vector<int64_t>()));
    const SentFrame* pub = sender.Last(thread_cmd_publish_post);
    REQUIRE(pub != NULL);
    CHECK(pub->m_body.find("\"clientpostid\":\"tp-7101-") != std::string::npos);
    session.OnResponse(thread_cmd_publish_post, pub->m_seq, PostResponse(9, true));
    REQUIRE(view.m_actions.size() == 1);
    CHECK(view.m_actions[0].m_cmd == thread_cmd_publish_post);
    CHECK(view.m_actions[0].m_code == 0);
    CHECK(view.m_actions[0].m_id == 9);
    CHECK(session.Feed().Find(9) != NULL);

    ThreadChange change;
    change.m_groupId = kGroup;
    change.m_changeType = thread_change_post_new;
    change.m_postId = 9;
    change.m_hasPost = true;
    change.m_post.m_postId = 9;
    change.m_post.m_activeTime = kBaseTime + 9;
    session.OnChange(change);
    CHECK(session.Feed().Posts().size() == 2);
    //别的群的推送不处理
    change.m_groupId = kGroup + 1;
    change.m_postId = 10;
    change.m_post.m_postId = 10;
    session.OnChange(change);
    CHECK(session.Feed().Posts().size() == 2);

    REQUIRE(session.PublishPost(L"", L"x", std::vector<ThreadImage>(), std::vector<int64_t>()));
    session.OnResponse(thread_cmd_publish_post, sender.Last(thread_cmd_publish_post)->m_seq,
                       CodeResponse(thread_error_member_muted));
    REQUIRE(view.m_actions.size() == 2);
    CHECK(view.m_actions[1].m_code == thread_error_member_muted);
    CHECK(session.Feed().Posts().size() == 2);

    //没有发出去时直接返回 false
    sender.m_fail = true;
    CHECK_FALSE(session.PublishPost(L"", L"x", std::vector<ThreadImage>(), std::vector<int64_t>()));
}

TEST_CASE("TGD22 互动：立即更新，失败恢复，成功以应答为准，本人推送不重复计数", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    std::vector<int64_t> ids(1, 5);
    OpenWithPage(session, sender, ids);
    session.SelectPost(5);

    REQUIRE(session.React(thread_target_post, 5, kReactionLike, true));
    CHECK(ReactionCount(session.Feed().Find(5)->m_reactions, kReactionLike) == 1);
    CHECK(HasMyReaction(session.Detail().Post().m_reactions, kReactionLike));
    session.OnResponse(thread_cmd_react, sender.Last(thread_cmd_react)->m_seq, CodeResponse(thread_error_server_internal));
    CHECK(session.Feed().Find(5)->m_reactions.empty());
    CHECK(session.Detail().Post().m_reactions.empty());
    REQUIRE_FALSE(view.m_actions.empty());
    CHECK(view.m_actions.back().m_code == thread_error_server_internal);

    REQUIRE(session.React(thread_target_post, 5, kReactionLike, true));
    Json::Value ok(Json::objectValue);
    ok["code"] = 0;
    ok["targettype"] = thread_target_post;
    ok["targetid"] = "5";
    Json::Value r(Json::objectValue);
    r["r"] = 0;
    r["n"] = 2;
    r["me"] = 1;
    ok["reactions"] = Json::Value(Json::arrayValue);
    ok["reactions"].append(r);
    session.OnResponse(thread_cmd_react, sender.Last(thread_cmd_react)->m_seq, Write(ok));
    CHECK(ReactionCount(session.Feed().Find(5)->m_reactions, kReactionLike) == 2);
    CHECK(ReactionCount(session.Detail().Post().m_reactions, kReactionLike) == 2);

    //本人这次互动的推送随后到达：汇总以推送为准，mine 保持
    ThreadChange change;
    change.m_groupId = kGroup;
    change.m_changeType = thread_change_reaction;
    change.m_operator = kMe;
    change.m_postId = 5;
    change.m_targetType = thread_target_post;
    change.m_targetId = 5;
    change.m_reaction = kReactionLike;
    change.m_on = true;
    ThreadReaction like;
    like.m_reaction = kReactionLike;
    like.m_count = 2;
    change.m_reactions.push_back(like);
    session.OnChange(change);
    CHECK(ReactionCount(session.Feed().Find(5)->m_reactions, kReactionLike) == 2);
    CHECK(HasMyReaction(session.Feed().Find(5)->m_reactions, kReactionLike));

    //拉互动名单
    REQUIRE(session.GetReactors(thread_target_post, 5, kReactionLike));
    Json::Value reactors(Json::objectValue);
    reactors["code"] = 0;
    reactors["targettype"] = 1;
    reactors["targetid"] = "5";
    reactors["reaction"] = 0;
    reactors["users"] = Json::Value(Json::arrayValue);
    reactors["users"].append(IdToString(kMe));
    reactors["users"].append(IdToString(kOther));
    reactors["total"] = 2;
    session.OnResponse(thread_cmd_get_reactors, sender.Last(thread_cmd_get_reactors)->m_seq, Write(reactors));
    CHECK(view.m_reactorTotal == 2);
    CHECK(view.m_reactors.size() == 2);
}

TEST_CASE("TGD23 留言：成功后加进详情、列表留言数加一，推送不重复；删除留言", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    std::vector<int64_t> ids(1, 5);
    OpenWithPage(session, sender, ids);
    session.SelectPost(5);
    Json::Value list(Json::objectValue);
    list["code"] = 0;
    list["ownerid"] = IdToString(kOther);
    list["comments"] = Json::Value(Json::arrayValue);
    list["nextcursor"] = "0";
    list["hasmore"] = 0;
    session.OnResponse(thread_cmd_list_comments, sender.Last(thread_cmd_list_comments)->m_seq, Write(list));

    REQUIRE(session.AddComment(5, 0, L"hello", std::vector<int64_t>()));
    CHECK(sender.Last(thread_cmd_add_comment)->m_body.find("\"clientcommentid\":\"tc-") != std::string::npos);
    Json::Value ok(Json::objectValue);
    ok["code"] = 0;
    ok["comment"] = CommentJson(31, 5, 0, kMe);
    session.OnResponse(thread_cmd_add_comment, sender.Last(thread_cmd_add_comment)->m_seq, Write(ok));
    CHECK(session.Detail().Comments().size() == 1);
    CHECK(session.Feed().Find(5)->m_commentCount == 1);
    CHECK(view.m_actions.back().m_cmd == thread_cmd_add_comment);
    CHECK(view.m_actions.back().m_id == 31);

    ThreadChange change;
    change.m_groupId = kGroup;
    change.m_changeType = thread_change_comment_new;
    change.m_operator = kMe;
    change.m_postId = 5;
    change.m_commentId = 31;
    change.m_hasComment = true;
    change.m_comment.m_commentId = 31;
    change.m_comment.m_postId = 5;
    change.m_commentCount = 1;
    change.m_activeTime = kBaseTime + 31;
    session.OnChange(change);
    CHECK(session.Detail().Comments().size() == 1);
    CHECK(session.Feed().Find(5)->m_commentCount == 1);
    CHECK(session.Detail().Post().m_commentCount == 1);

    REQUIRE(session.DeleteComment(31));
    Json::Value del(Json::objectValue);
    del["code"] = 0;
    del["commentid"] = "31";
    session.OnResponse(thread_cmd_delete_comment, sender.Last(thread_cmd_delete_comment)->m_seq, Write(del));
    CHECK(session.Detail().Comments().empty());
    CHECK(session.Feed().Find(5)->m_commentCount == 0);
    CHECK(view.m_actions.back().m_cmd == thread_cmd_delete_comment);
}

TEST_CASE("TGD24 断线：未完成的本人操作按超时通知，加载标记清掉后可以重新加载", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    session.Start();
    REQUIRE(session.Feed().IsLoading());
    REQUIRE(session.PublishPost(L"", L"x", std::vector<ThreadImage>(), std::vector<int64_t>()));
    REQUIRE(session.DeletePost(3));
    session.OnDisconnected();
    CHECK(session.PendingCount() == 0);
    CHECK_FALSE(session.Feed().IsLoading());
    int timeouts = 0;
    for (size_t i = 0; i < view.m_actions.size(); ++i)
    {
        if (view.m_actions[i].m_code == thread_error_timeout)
            ++timeouts;
    }
    CHECK(timeouts == 2);
    //重连后重新开始
    session.Start();
    CHECK(session.Feed().IsLoading());
    CHECK(sender.Count(thread_cmd_watch) == 2);
}

TEST_CASE("TGD25 切换排序时旧应答丢弃；补充的应答与推送只算一次", "[threaddll]")
{
    FakeSender sender;
    FakeView view;
    CThreadSession session(kGroup, &sender);
    session.SetView(&view);
    session.Start();
    const uint32_t oldSeq = sender.Last(thread_cmd_list_posts)->m_seq;
    session.SetSort(thread_sort_publish);
    CHECK(session.Feed().Sort() == thread_sort_publish);
    std::vector<int64_t> ids(1, 5);
    session.OnResponse(thread_cmd_list_posts, oldSeq, ListResponse(ids, "", false));
    CHECK(session.Feed().Posts().empty());
    session.OnResponse(thread_cmd_list_posts, sender.Last(thread_cmd_list_posts)->m_seq, ListResponse(ids, "", false));
    CHECK(session.Feed().Posts().size() == 1);

    session.SelectPost(5);
    session.OnResponse(thread_cmd_get_post, sender.Last(thread_cmd_get_post)->m_seq, PostResponse(5, true));
    REQUIRE(session.AddAddendum(5, L"more", std::vector<ThreadImage>(), std::vector<int64_t>()));
    Json::Value ok(Json::objectValue);
    ok["code"] = 0;
    Json::Value a(Json::objectValue);
    a["addid"] = "44";
    a["postid"] = "5";
    a["author"] = IdToString(kMe);
    a["text"] = "more";
    a["images"] = Json::Value(Json::arrayValue);
    a["mentions"] = Json::Value(Json::arrayValue);
    a["createtime"] = static_cast<Json::Int64>(kBaseTime + 100);
    ok["addendum"] = a;
    session.OnResponse(thread_cmd_add_addendum, sender.Last(thread_cmd_add_addendum)->m_seq, Write(ok));
    CHECK(session.Detail().Post().m_addenda.size() == 1);
    CHECK(session.Feed().Find(5)->m_addendumCount == 0);   //列表只按推送计数

    ThreadChange change;
    change.m_groupId = kGroup;
    change.m_changeType = thread_change_addendum_new;
    change.m_operator = kMe;
    change.m_postId = 5;
    change.m_addId = 44;
    change.m_hasAddendum = true;
    change.m_addendum.m_addId = 44;
    change.m_addendum.m_postId = 5;
    change.m_activeTime = kBaseTime + 100;
    session.OnChange(change);
    CHECK(session.Detail().Post().m_addenda.size() == 1);
    CHECK(session.Feed().Find(5)->m_addendumCount == 1);

    REQUIRE(session.DeleteAddendum(44));
    Json::Value del(Json::objectValue);
    del["code"] = 0;
    del["addid"] = "44";
    session.OnResponse(thread_cmd_delete_addendum, sender.Last(thread_cmd_delete_addendum)->m_seq, Write(del));
    CHECK(session.Detail().Post().m_addenda.empty());
}
