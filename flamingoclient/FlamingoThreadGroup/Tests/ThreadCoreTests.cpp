/**
 *  帖子群 DLL 纯逻辑的单元测试（标签 [threaddll]），覆盖 docs/帖子群方案.md 第 8 节 DLL 部分：
 *    - TGD1~TGD4  帖子列表模型：首屏、翻页合并与去重、按推送插入 / 更新 / 删除、切换排序、置顶在前；
 *    - TGD5~TGD7  留言区模型：留言与回复分组；删除后的占位与占位的清除；回复挂到所属留言下；
 *    - TGD8~TGD10 互动汇总：点了立即更新、失败恢复；多种表情并存；推送合并 mine；名单「等 N 人」的拆分；
 *    - TGD11      发帖校验：长度、图片数、空内容、种类不允许的内容；
 *    - TGD14      帖子详情模型：补充按时间排列、去重；只有楼主可以追加补充；
 *    - TGD16      表情编码：/f["n"] 与「[表情名]」互转，字面编码的转义，换行统一为 LF；
 *    - TGD17      报文编解码：请求字段、应答与 2720 推送的解析；
 *    - TGD18      时间分档；
 *    - TGD28      @ 片段（docs/帖子群方案.md 14.4）：发送前重扫（同一位置取最长的名字、删掉一半的不算、前面不是空白的不算）、
 *                 从原文的片段取回当时的名字（编辑帖子用）、编解码里 at 的写出与读回；
 *    - TGD29      @ 片段的显示换算：认识的表情算 1 个字符、不认识的按替代文字长度算、「//」转义、落在表情标签中间的拒绝；
 *                 整段片段换算后标出 @ 到本人的那几段。
 *  TGD15（DLL 里重复的协议常量与 Protocol.h 一致）在 ThreadParityTests.cpp：那里要包含主程序的协议头，
 *  其中的全局枚举与本 DLL 的同名枚举会冲突，不能与本文件的 using namespace 放在一起。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Core/ThreadTypes.h"
#include "../Core/ThreadText.h"
#include "../Core/ThreadRules.h"
#include "../Core/ThreadCodec.h"
#include "../Core/ThreadReactions.h"
#include "../Core/ThreadFeedModel.h"
#include "../Core/ThreadDetailModel.h"
#include "../Core/ThreadTime.h"
#include "../Core/ThreadMention.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

using namespace threadgroup;

namespace {

//测试用的群、用户与时间
const int64_t kGroup = 7001;
const int64_t kMe = 9001;
const int64_t kOther = 9002;
const int64_t kThird = 9003;
const int64_t kBaseTime = 1791000000000LL;
const int64_t kMinuteMs = 60000;

//做一条摘要形态的帖子
ThreadPost MakePost(int64_t postId, int64_t activeTime, int64_t pinTime)
{
    ThreadPost p;
    p.m_postId = postId;
    p.m_groupId = kGroup;
    p.m_author = kOther;
    p.m_title = L"t";
    p.m_text = L"body";
    p.m_createTime = activeTime;
    p.m_activeTime = activeTime;
    p.m_pinTime = pinTime;
    return p;
}

//做一条留言或回复
ThreadComment MakeComment(int64_t commentId, int64_t postId, int64_t parentId, int64_t author)
{
    ThreadComment c;
    c.m_commentId = commentId;
    c.m_postId = postId;
    c.m_parentId = parentId;
    c.m_author = author;
    c.m_text = L"hi";
    c.m_createTime = kBaseTime + commentId;
    return c;
}

//列表里各帖子的 ID，按显示顺序
std::vector<int64_t> FeedIds(const CThreadFeedModel& feed)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < feed.Posts().size(); ++i)
        ids.push_back(feed.Posts()[i].m_postId);
    return ids;
}

//解析 JSON（测试里核对请求字段）
Json::Value Parse(const std::string& json)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return root;
}

//一段补充
ThreadAddendum MakeAddendum(int64_t addId, int64_t postId)
{
    ThreadAddendum a;
    a.m_addId = addId;
    a.m_postId = postId;
    a.m_author = kOther;
    a.m_text = L"more";
    a.m_createTime = kBaseTime + addId * kMinuteMs;
    return a;
}

} // namespace

TEST_CASE("TGD1 帖子列表首屏：置顶在前，其余按最新动态倒序", "[threaddll]")
{
    CThreadFeedModel feed;
    feed.Reset(thread_sort_activity);
    REQUIRE(feed.BeginLoad());
    CHECK_FALSE(feed.BeginLoad());
    std::vector<ThreadPost> page;
    page.push_back(MakePost(1, kBaseTime + 10, 0));
    page.push_back(MakePost(2, kBaseTime + 30, 0));
    page.push_back(MakePost(3, kBaseTime + 20, kBaseTime + 5));
    page.push_back(MakePost(4, kBaseTime + 30, 0));
    feed.ApplyPage(page, "a1_1", true, true);
    CHECK(feed.IsLoaded());
    CHECK_FALSE(feed.IsLoading());
    CHECK(feed.HasMore());
    const std::vector<int64_t> ids = FeedIds(feed);
    REQUIRE(ids.size() == 4);
    CHECK(ids[0] == 3);   // 置顶
    CHECK(ids[1] == 4);   // 动态时间相同按 ID 倒序
    CHECK(ids[2] == 2);
    CHECK(ids[3] == 1);
    CHECK(feed.PinnedCount() == 1);
}

TEST_CASE("TGD2 翻页合并：追加并按 ID 去重，最后一页没有游标", "[threaddll]")
{
    CThreadFeedModel feed;
    feed.Reset(thread_sort_publish);
    feed.BeginLoad();
    std::vector<ThreadPost> first;
    first.push_back(MakePost(10, kBaseTime, 0));
    first.push_back(MakePost(9, kBaseTime, 0));
    feed.ApplyPage(first, "p9", true, true);
    REQUIRE(feed.BeginLoad());
    std::vector<ThreadPost> second;
    ThreadPost dup = MakePost(9, kBaseTime, 0);
    dup.m_commentCount = 5;
    second.push_back(dup);
    second.push_back(MakePost(8, kBaseTime, 0));
    feed.ApplyPage(second, "", false, false);
    const std::vector<int64_t> ids = FeedIds(feed);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 10);
    CHECK(ids[1] == 9);
    CHECK(ids[2] == 8);
    CHECK(feed.Find(9)->m_commentCount == 5);
    CHECK_FALSE(feed.HasMore());
}

TEST_CASE("TGD3 推送：新帖插入、修改保留本地互动、删除、留言更新动态时间", "[threaddll]")
{
    CThreadFeedModel feed;
    feed.Reset(thread_sort_activity);
    feed.BeginLoad();
    std::vector<ThreadPost> page;
    ThreadPost liked = MakePost(1, kBaseTime, 0);
    ThreadReaction like;
    like.m_reaction = kReactionLike;
    like.m_count = 1;
    like.m_mine = true;
    liked.m_reactions.push_back(like);
    page.push_back(liked);
    page.push_back(MakePost(2, kBaseTime + 1, 0));
    feed.ApplyPage(page, "", false, true);

    ThreadChange added;
    added.m_groupId = kGroup;
    added.m_changeType = thread_change_post_new;
    added.m_postId = 3;
    added.m_hasPost = true;
    added.m_post = MakePost(3, kBaseTime + 2, 0);
    CHECK(feed.ApplyChange(added, kMe));
    CHECK(FeedIds(feed)[0] == 3);
    //同一条新帖再推一次不重复
    CHECK_FALSE(feed.ApplyChange(added, kMe));
    CHECK(feed.Posts().size() == 3);

    ThreadChange edit;
    edit.m_groupId = kGroup;
    edit.m_changeType = thread_change_post_edit;
    edit.m_postId = 1;
    edit.m_hasPost = true;
    edit.m_post = MakePost(1, kBaseTime, 0);
    edit.m_post.m_title = L"new title";
    edit.m_post.m_editTime = kBaseTime + 100;
    CHECK(feed.ApplyChange(edit, kMe));
    CHECK(feed.Find(1)->m_title == L"new title");
    CHECK(feed.Find(1)->m_editTime == kBaseTime + 100);
    REQUIRE(feed.Find(1)->m_reactions.size() == 1);
    CHECK(feed.Find(1)->m_reactions[0].m_mine);

    ThreadChange comment;
    comment.m_groupId = kGroup;
    comment.m_changeType = thread_change_comment_new;
    comment.m_postId = 1;
    comment.m_commentCount = 4;
    comment.m_activeTime = kBaseTime + 500;
    CHECK(feed.ApplyChange(comment, kMe));
    CHECK(FeedIds(feed)[0] == 1);
    CHECK(feed.Find(1)->m_commentCount == 4);

    ThreadChange removed;
    removed.m_groupId = kGroup;
    removed.m_changeType = thread_change_post_delete;
    removed.m_postId = 2;
    CHECK(feed.ApplyChange(removed, kMe));
    CHECK(feed.Find(2) == NULL);

    //第一页还没到时新帖推送不插入
    CThreadFeedModel empty;
    empty.Reset(thread_sort_activity);
    CHECK_FALSE(empty.ApplyChange(added, kMe));
}

TEST_CASE("TGD4 置顶与取消置顶调整顺序；补充推送更新段数与动态时间；切换排序清空", "[threaddll]")
{
    CThreadFeedModel feed;
    feed.Reset(thread_sort_activity);
    feed.BeginLoad();
    std::vector<ThreadPost> page;
    page.push_back(MakePost(1, kBaseTime + 1, 0));
    page.push_back(MakePost(2, kBaseTime + 2, 0));
    feed.ApplyPage(page, "", false, true);
    ThreadChange pin;
    pin.m_groupId = kGroup;
    pin.m_changeType = thread_change_post_pin;
    pin.m_postId = 1;
    pin.m_hasPost = true;
    pin.m_post = MakePost(1, kBaseTime + 1, kBaseTime + 9);
    CHECK(feed.ApplyChange(pin, kMe));
    CHECK(FeedIds(feed)[0] == 1);
    pin.m_post.m_pinTime = 0;
    CHECK(feed.ApplyChange(pin, kMe));
    CHECK(FeedIds(feed)[0] == 2);

    ThreadChange add;
    add.m_groupId = kGroup;
    add.m_changeType = thread_change_addendum_new;
    add.m_postId = 1;
    add.m_activeTime = kBaseTime + 50;
    CHECK(feed.ApplyChange(add, kMe));
    CHECK(feed.Find(1)->m_addendumCount == 1);
    CHECK(FeedIds(feed)[0] == 1);
    ThreadChange del;
    del.m_groupId = kGroup;
    del.m_changeType = thread_change_addendum_delete;
    del.m_postId = 1;
    CHECK(feed.ApplyChange(del, kMe));
    CHECK(feed.Find(1)->m_addendumCount == 0);

    feed.Reset(thread_sort_publish);
    CHECK(feed.Posts().empty());
    CHECK(feed.Sort() == thread_sort_publish);
    CHECK_FALSE(feed.IsLoaded());
}

TEST_CASE("TGD5 留言区：留言与回复分组，新回复挂到所属留言下，重复的只算一次", "[threaddll]")
{
    CThreadDetailModel detail;
    detail.Select(100, NULL);
    REQUIRE(detail.BeginLoadComments());
    std::vector<ThreadComment> page;
    ThreadComment c1 = MakeComment(1, 100, 0, kOther);
    c1.m_replyCount = 1;
    c1.m_replies.push_back(MakeComment(3, 100, 1, kThird));
    c1.m_replyCursor = 3;
    page.push_back(c1);
    page.push_back(MakeComment(2, 100, 0, kThird));
    detail.ApplyComments(100, page, 2, false, true);
    REQUIRE(detail.Comments().size() == 2);
    CHECK(detail.Comments()[0].m_replies.size() == 1);

    ThreadComment reply = MakeComment(5, 100, 1, kMe);
    reply.m_replyTo = kThird;
    CHECK(detail.AddComment(reply));
    CHECK_FALSE(detail.AddComment(reply));
    REQUIRE(detail.Comments()[0].m_replies.size() == 2);
    CHECK(detail.Comments()[0].m_replyCount == 2);
    CHECK(detail.FindComment(5)->m_replyTo == kThird);

    //不属于当前帖子的留言忽略
    CHECK_FALSE(detail.AddComment(MakeComment(6, 101, 0, kMe)));
    //新的留言排在最后
    CHECK(detail.AddComment(MakeComment(7, 100, 0, kMe)));
    CHECK(detail.Comments()[2].m_commentId == 7);
}

TEST_CASE("TGD6 删除留言：有回复时保留占位；删掉占位下最后一条回复时占位一并去掉", "[threaddll]")
{
    CThreadDetailModel detail;
    detail.Select(100, NULL);
    detail.BeginLoadComments();
    std::vector<ThreadComment> page;
    ThreadComment c1 = MakeComment(1, 100, 0, kOther);
    c1.m_replyCount = 1;
    c1.m_replies.push_back(MakeComment(3, 100, 1, kThird));
    page.push_back(c1);
    page.push_back(MakeComment(2, 100, 0, kThird));
    detail.ApplyComments(100, page, 0, false, true);

    CHECK(detail.RemoveComment(1, NULL));
    REQUIRE(detail.Comments().size() == 2);
    CHECK(detail.Comments()[0].m_deleted);
    CHECK(detail.Comments()[0].m_text.empty());
    CHECK(detail.RemoveComment(3, NULL));
    REQUIRE(detail.Comments().size() == 1);
    CHECK(detail.Comments()[0].m_commentId == 2);
    CHECK(detail.RemoveComment(2, NULL));
    CHECK(detail.Comments().empty());
    CHECK_FALSE(detail.RemoveComment(2, NULL));
}

TEST_CASE("TGD7 回复的更多分页：游标取已加载的最后一条，拉到的按 ID 去重并入", "[threaddll]")
{
    CThreadDetailModel detail;
    detail.Select(100, NULL);
    detail.BeginLoadComments();
    std::vector<ThreadComment> page;
    ThreadComment c1 = MakeComment(1, 100, 0, kOther);
    c1.m_replyCount = 5;
    c1.m_replies.push_back(MakeComment(2, 100, 1, kThird));
    c1.m_replies.push_back(MakeComment(3, 100, 1, kThird));
    c1.m_replyCursor = 3;
    page.push_back(c1);
    detail.ApplyComments(100, page, 0, false, true);
    CHECK(CThreadDetailModel::UnloadedReplies(detail.Comments()[0]) == 3);

    //所属留言下的回复没加载全时，新回复只计数
    CHECK(detail.AddComment(MakeComment(9, 100, 1, kMe)));
    CHECK(detail.Comments()[0].m_replies.size() == 2);
    CHECK(detail.Comments()[0].m_replyCount == 6);

    int64_t cursor = 0;
    REQUIRE(detail.BeginLoadReplies(1, cursor));
    CHECK(cursor == 3);
    CHECK_FALSE(detail.BeginLoadReplies(1, cursor));
    std::vector<ThreadComment> more;
    more.push_back(MakeComment(3, 100, 1, kThird));
    more.push_back(MakeComment(4, 100, 1, kThird));
    more.push_back(MakeComment(9, 100, 1, kMe));
    detail.ApplyReplies(1, more);
    REQUIRE(detail.Comments()[0].m_replies.size() == 4);
    CHECK(detail.Comments()[0].m_replies[3].m_commentId == 9);
    CHECK(detail.Comments()[0].m_replyCursor == 9);
    CHECK_FALSE(detail.Comments()[0].m_repliesLoading);
}

TEST_CASE("TGD8 互动：点了立即更新，失败时恢复原状", "[threaddll]")
{
    CThreadFeedModel feed;
    feed.Reset(thread_sort_activity);
    feed.BeginLoad();
    std::vector<ThreadPost> page;
    page.push_back(MakePost(1, kBaseTime, 0));
    feed.ApplyPage(page, "", false, true);
    std::vector<ThreadReaction> before;
    REQUIRE(feed.ToggleReactionLocal(1, kReactionLike, true, before));
    CHECK(before.empty());
    CHECK(ReactionCount(feed.Find(1)->m_reactions, kReactionLike) == 1);
    CHECK(HasMyReaction(feed.Find(1)->m_reactions, kReactionLike));
    feed.SetReactions(1, before);
    CHECK(feed.Find(1)->m_reactions.empty());

    std::vector<ThreadReaction> list;
    ToggleReaction(list, kReactionLike, true);
    ToggleReaction(list, kReactionLike, true);
    CHECK(ReactionCount(list, kReactionLike) == 1);
    ToggleReaction(list, kReactionLike, false);
    CHECK(list.empty());
    ToggleReaction(list, kReactionLike, false);
    CHECK(list.empty());
}

TEST_CASE("TGD9 多种表情并存：赞在最前，其余按编号；推送合并时保留本人的 mine", "[threaddll]")
{
    std::vector<ThreadReaction> list;
    ToggleReaction(list, ReactionOfFace(5), true);
    ToggleReaction(list, ReactionOfFace(2), true);
    ToggleReaction(list, kReactionLike, true);
    REQUIRE(list.size() == 3);
    CHECK(list[0].m_reaction == kReactionLike);
    CHECK(list[1].m_reaction == ReactionOfFace(2));
    CHECK(list[2].m_reaction == ReactionOfFace(5));
    CHECK(FaceOfReaction(list[2].m_reaction) == 5);
    CHECK(FaceOfReaction(kReactionLike) == -1);

    //别人加了一个赞：推送里 mine 全是 false，本人原来加过的照旧
    std::vector<ThreadReaction> summary;
    ThreadReaction like;
    like.m_reaction = kReactionLike;
    like.m_count = 2;
    summary.push_back(like);
    ThreadReaction face2;
    face2.m_reaction = ReactionOfFace(2);
    face2.m_count = 1;
    summary.push_back(face2);
    ApplyReactionNotify(list, summary, kReactionLike, true, false);
    REQUIRE(list.size() == 2);
    CHECK(list[0].m_count == 2);
    CHECK(list[0].m_mine);
    CHECK(list[1].m_mine);

    //本人在另一台电脑上取消了表情 2：那一项按 on 改
    std::vector<ThreadReaction> after;
    after.push_back(like);
    ApplyReactionNotify(list, after, ReactionOfFace(2), false, true);
    REQUIRE(list.size() == 1);
    CHECK(list[0].m_mine);

    CHECK(IsValidReaction(kReactionLike));
    CHECK(IsValidReaction(ReactionOfFace(142)));
    CHECK_FALSE(IsValidReaction(1));
    CHECK_FALSE(IsValidReaction(kReactionFaceBase + kReactionMaxFaceId + 1));
}

TEST_CASE("TGD10 互动名单：最多列 20 个名字，其余写「等 N 人」", "[threaddll]")
{
    int shown = 0;
    int rest = 0;
    ReactorListSplit(3, 3, kReactorListLimit, shown, rest);
    CHECK(shown == 3);
    CHECK(rest == 0);
    ReactorListSplit(20, 35, kReactorListLimit, shown, rest);
    CHECK(shown == 20);
    CHECK(rest == 35);
    ReactorListSplit(25, 25, kReactorListLimit, shown, rest);
    CHECK(shown == 20);
    CHECK(rest == 25);
}

TEST_CASE("TGD11 发帖校验：长度、图片数、空内容与种类不允许的内容", "[threaddll]")
{
    CHECK(CheckCompose(compose_post, L"title", L"text", 0, 0) == compose_ok);
    CHECK(CheckCompose(compose_post, L"", L"", 1, 0) == compose_ok);
    CHECK(CheckCompose(compose_post, L"title only", L"  \r\n\x3000", 0, 0) == compose_empty);
    CHECK(CheckCompose(compose_post, std::wstring(kTitleMaxChars, L'a'), L"x", 0, 0) == compose_ok);
    CHECK(CheckCompose(compose_post, std::wstring(kTitleMaxChars + 1, L'a'), L"x", 0, 0) == compose_title_too_long);
    CHECK(CheckCompose(compose_post, L"", std::wstring(kPostTextMaxChars, L'a'), 0, 0) == compose_ok);
    CHECK(CheckCompose(compose_post, L"", std::wstring(kPostTextMaxChars + 1, L'a'), 0, 0) == compose_text_too_long);
    CHECK(CheckCompose(compose_post, L"", L"x", kMaxImages + 1, 0) == compose_too_many_images);
    CHECK(CheckCompose(compose_post, L"", L"x", 0, kMaxMentions + 1) == compose_too_many_mentions);
    CHECK(CheckCompose(compose_addendum, L"t", L"x", 0, 0) == compose_not_allowed);
    CHECK(CheckCompose(compose_addendum, L"", std::wstring(kAddendumTextMaxChars + 1, L'a'), 0, 0)
          == compose_text_too_long);
    CHECK(CheckCompose(compose_comment, L"", L"x", 1, 0) == compose_not_allowed);
    CHECK(CheckCompose(compose_comment, L"", std::wstring(kCommentTextMaxChars + 1, L'a'), 0, 0)
          == compose_text_too_long);
    //代理对算一个字
    std::wstring emoji;
    for (int i = 0; i < kCommentTextMaxChars; ++i)
        emoji += L"\xD83D\xDE00";
    CHECK(CountChars(emoji) == kCommentTextMaxChars);
    CHECK(CheckCompose(compose_comment, L"", emoji, 0, 0) == compose_ok);

    CHECK(CanEditPost(kMe, kMe));
    CHECK_FALSE(CanEditPost(kOther, kMe));
    CHECK(CanDeleteContent(kMe, kMe, thread_role_member));
    CHECK_FALSE(CanDeleteContent(kOther, kMe, thread_role_member));
    CHECK(CanDeleteContent(kOther, kMe, thread_role_admin));
    CHECK(CanPin(thread_role_owner));
    CHECK_FALSE(CanPin(thread_role_member));
    CHECK(CanSpeak(thread_role_member, false));
    CHECK_FALSE(CanSpeak(thread_role_member, true));
    CHECK_FALSE(CanSpeak(thread_role_none, false));
}

TEST_CASE("TGD14 帖子详情：补充按时间排列并去重，只有楼主能追加补充", "[threaddll]")
{
    ThreadPost summary = MakePost(100, kBaseTime, 0);
    summary.m_commentCount = 3;
    CThreadDetailModel detail;
    detail.Select(100, &summary);
    CHECK_FALSE(detail.IsPostLoaded());
    CHECK(detail.Post().m_commentCount == 3);

    ThreadPost full = summary;
    full.m_full = true;
    full.m_addenda.push_back(MakeAddendum(12, 100));
    full.m_addenda.push_back(MakeAddendum(11, 100));
    CHECK(detail.ApplyPost(full));
    REQUIRE(detail.Post().m_addenda.size() == 2);
    CHECK(detail.Post().m_addenda[0].m_addId == 11);
    CHECK(detail.Post().m_addendumCount == 2);

    CHECK(detail.AddAddendum(MakeAddendum(13, 100)));
    CHECK_FALSE(detail.AddAddendum(MakeAddendum(13, 100)));
    CHECK_FALSE(detail.AddAddendum(MakeAddendum(14, 101)));
    CHECK(detail.Post().m_addendumCount == 3);
    CHECK(detail.RemoveAddendum(11));
    CHECK_FALSE(detail.RemoveAddendum(11));
    CHECK(detail.Post().m_addenda[0].m_addId == 12);

    //别的帖子的完整内容不采用
    ThreadPost other = MakePost(101, kBaseTime, 0);
    CHECK_FALSE(detail.ApplyPost(other));

    CHECK(CanAddAddendum(kOther, kOther, 0));
    CHECK_FALSE(CanAddAddendum(kOther, kMe, 0));
    CHECK_FALSE(CanAddAddendum(kOther, kOther, kMaxAddenda));

    //推送说帖子被删
    ThreadChange del;
    del.m_groupId = kGroup;
    del.m_changeType = thread_change_post_delete;
    del.m_postId = 100;
    CHECK(detail.ApplyChange(del, kMe));
    CHECK(detail.IsDeleted());
}

TEST_CASE("TGD16 表情编码：/f[\"n\"] 与「[表情名]」互转，字面编码转义", "[threaddll]")
{
    FaceTable faces;
    faces.Add(0, L"微笑");
    faces.Add(12, L"大笑");
    CHECK(faces.Count() == 2);
    CHECK(DisplayToWire(L"你好[微笑]再见[大笑]", faces) == L"你好/f[\"0\"]再见/f[\"12\"]");
    CHECK(WireToDisplay(L"你好/f[\"0\"]再见/f[\"12\"]", faces, L"[表情]") == L"你好[微笑]再见[大笑]");
    //不认识的表情编号
    CHECK(WireToDisplay(L"a/f[\"99\"]", faces, L"[表情]") == L"a[表情]");
    //不在表情库里的方括号原样保留
    CHECK(DisplayToWire(L"[注意]看这里", faces) == L"[注意]看这里");
    //字面的 /f["3"] 发出前补一个 '/'，显示时还原
    CHECK(DisplayToWire(L"see /f[\"3\"] here", faces) == L"see //f[\"3\"] here");
    CHECK(WireToDisplay(L"see //f[\"3\"] here", faces, L"[表情]") == L"see /f[\"3\"] here");
    //网址里的双斜杠不受影响
    CHECK(DisplayToWire(L"http://x.cn", faces) == L"http://x.cn");
    //换行统一为 LF：RichEdit 取出的 CR、粘贴来的 CRLF
    CHECK(DisplayToWire(L"a\rb\r\nc\nd", faces) == L"a\nb\nc\nd");
    CHECK(WireToDisplay(L"http://x.cn", faces, L"[表情]") == L"http://x.cn");
    const std::vector<TextSegment> segs = ParseWire(L"a/f[\"12\"]b");
    REQUIRE(segs.size() == 3);
    CHECK(segs[1].m_face);
    CHECK(segs[1].m_faceId == 12);

    bool cut = false;
    CHECK(MakeDigest(L"  第一行\r\n\r\n第二行  ", 100, cut) == L"第一行 第二行");
    CHECK_FALSE(cut);
    CHECK(MakeDigest(L"abcdef", 3, cut) == L"abc");
    CHECK(cut);
    CHECK(TruncateChars(L"a\xD83D\xDE00" L"b", 2, cut) == L"a\xD83D\xDE00");
    CHECK(Utf8ToWide(WideToUtf8(L"中文\xD83D\xDE00")) == L"中文\xD83D\xDE00");
}

TEST_CASE("TGD17 报文编解码：请求字段、应答与推送", "[threaddll]")
{
    Json::Value req = Parse(ThreadCodec::BuildListPosts(kGroup, thread_sort_publish, "p9", 20));
    CHECK(req["groupid"].asString() == "7001");
    CHECK(req["sort"].asInt() == thread_sort_publish);
    CHECK(req["cursor"].asString() == "p9");
    CHECK(req["limit"].asInt() == 20);

    std::vector<ThreadImage> images(1);
    images[0].m_fileId = "123";
    images[0].m_md5 = "abc";
    images[0].m_width = 640;
    images[0].m_height = 480;
    images[0].m_size = 1024;
    std::vector<int64_t> mentions(1, kOther);
    req = Parse(ThreadCodec::BuildPublishPost(kGroup, "tp-1", L"标题", L"正文/f[\"1\"]", images, mentions));
    CHECK(req["clientpostid"].asString() == "tp-1");
    CHECK(req["title"].asString() == WideToUtf8(L"标题"));
    CHECK(req["text"].asString() == WideToUtf8(L"正文/f[\"1\"]"));
    CHECK(req["images"][0]["fileid"].asString() == "123");
    CHECK(req["images"][0]["width"].asInt() == 640);
    CHECK(req["mentions"][0].asString() == "9002");

    req = Parse(ThreadCodec::BuildAddComment(100, 0, "tc-1", L"hi", std::vector<int64_t>()));
    CHECK(req["parentid"].asString() == "0");
    CHECK_FALSE(req.isMember("title"));
    CHECK_FALSE(req.isMember("images"));
    req = Parse(ThreadCodec::BuildReact(thread_target_comment, 55, ReactionOfFace(3), false));
    CHECK(req["targettype"].asInt() == thread_target_comment);
    CHECK(req["reaction"].asInt() == 1003);
    CHECK(req["on"].asInt() == 0);
    req = Parse(ThreadCodec::BuildWatch(kGroup, true));
    CHECK(req["watch"].asInt() == 1);

    //2701 应答（与服务端 buildListPostsResponse 同形）
    const std::string listJson =
        "{\"code\":0,\"msg\":\"ok\",\"posts\":[{\"postid\":\"5\",\"groupid\":\"7001\",\"author\":\"9002\","
        "\"title\":\"T\",\"text\":\"abc\",\"texttruncated\":1,\"images\":[{\"fileid\":\"1\",\"md5\":\"m\","
        "\"width\":10,\"height\":20,\"size\":30,\"thumbfileid\":\"\"}],\"imagecount\":4,\"mentions\":[\"9001\"],"
        "\"createtime\":100,\"edittime\":0,\"activetime\":200,\"commentcount\":2,\"pintime\":0,\"addendumcount\":1,"
        "\"reactions\":[{\"r\":0,\"n\":3,\"me\":1}]}],\"nextcursor\":\"a200_5\",\"hasmore\":1}";
    int code = -1;
    std::vector<ThreadPost> posts;
    std::string next;
    bool more = false;
    REQUIRE(ThreadCodec::ParseListPosts(listJson, code, posts, next, more));
    CHECK(code == 0);
    REQUIRE(posts.size() == 1);
    CHECK(posts[0].m_postId == 5);
    CHECK(posts[0].m_textTruncated);
    CHECK(posts[0].m_imageCount == 4);
    CHECK(posts[0].m_images[0].m_height == 20);
    CHECK(posts[0].m_mentions[0] == kMe);
    CHECK(posts[0].m_activeTime == 200);
    CHECK(posts[0].m_reactions[0].m_mine);
    CHECK_FALSE(posts[0].m_full);
    CHECK(next == "a200_5");
    CHECK(more);

    //错误应答
    REQUIRE(ThreadCodec::ParseListPosts("{\"code\":1300,\"msg\":\"no access\"}", code, posts, next, more));
    CHECK(code == thread_error_disabled);
    CHECK(posts.empty());
    CHECK_FALSE(ThreadCodec::ParseListPosts("not json", code, posts, next, more));

    //2707 应答：带顺带的回复与占位
    const std::string commentsJson =
        "{\"code\":0,\"ownerid\":\"9002\",\"comments\":[{\"commentid\":\"1\",\"postid\":\"5\",\"author\":\"9003\","
        "\"parentid\":\"0\",\"replyto\":\"0\",\"text\":\"\",\"mentions\":[],\"createtime\":1,\"deleted\":1,"
        "\"replycount\":2,\"reactions\":[],\"replies\":[{\"commentid\":\"2\",\"postid\":\"5\",\"author\":\"9001\","
        "\"parentid\":\"1\",\"replyto\":\"9003\",\"text\":\"r\",\"mentions\":[],\"createtime\":2,\"deleted\":0,"
        "\"replycount\":0,\"reactions\":[]}]}],\"nextcursor\":\"1\",\"hasmore\":0}";
    int64_t owner = 0;
    std::vector<ThreadComment> comments;
    int64_t cursor = 0;
    REQUIRE(ThreadCodec::ParseCommentsResponse(commentsJson, code, owner, comments, cursor, more));
    CHECK(owner == kOther);
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].m_deleted);
    CHECK(comments[0].m_replyCount == 2);
    REQUIRE(comments[0].m_replies.size() == 1);
    CHECK(comments[0].m_replies[0].m_replyTo == kThird);
    CHECK(comments[0].m_replyCursor == 2);
    CHECK(cursor == 1);
    CHECK_FALSE(more);

    //2720 推送：互动变化
    const std::string notify =
        "{\"groupid\":\"7001\",\"changetype\":9,\"operator\":\"9002\",\"postid\":\"5\",\"targettype\":2,"
        "\"targetid\":\"77\",\"reaction\":1005,\"on\":1,\"reactions\":[{\"r\":1005,\"n\":1,\"me\":0}]}";
    ThreadChange change;
    REQUIRE(ThreadCodec::ParseChangedNotify(notify, change));
    CHECK(change.m_groupId == kGroup);
    CHECK(change.m_changeType == thread_change_reaction);
    CHECK(change.m_operator == kOther);
    CHECK(change.m_targetType == thread_target_comment);
    CHECK(change.m_targetId == 77);
    CHECK(change.m_on);
    CHECK(change.m_commentCount == -1);
    REQUIRE(change.m_reactions.size() == 1);
    CHECK(change.m_reactions[0].m_reaction == 1005);
    CHECK_FALSE(ThreadCodec::ParseChangedNotify("{\"changetype\":1}", change));

    //2714 应答
    ThreadAddendum a;
    bool has = false;
    REQUIRE(ThreadCodec::ParseAddendumResponse(
        "{\"code\":0,\"addendum\":{\"addid\":\"8\",\"postid\":\"5\",\"author\":\"9002\",\"text\":\"x\",\"images\":[],"
        "\"mentions\":[],\"createtime\":9}}",
        code, a, has));
    CHECK(has);
    CHECK(a.m_addId == 8);
    int64_t id = 0;
    REQUIRE(ThreadCodec::ParseIdResponse("{\"code\":0,\"msg\":\"ok\",\"commentid\":\"42\"}", "commentid", code, id));
    CHECK(id == 42);
}

TEST_CASE("TGD18 时间分档：刚刚、分钟前、今天、昨天、今年、往年", "[threaddll]")
{
    //2026-10-07 12:00:00 北京时间 = 04:00:00 UTC
    const int64_t kBeijing = 8LL * 60 * kMinuteMs;
    const int64_t now = 1791345600000LL;
    TimeParts t = ClassifyTime(now - 30000, now, kBeijing);
    CHECK(t.m_kind == time_just_now);
    t = ClassifyTime(now - 5 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_minutes_ago);
    CHECK(t.m_minutes == 5);
    t = ClassifyTime(now - 3 * 60 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_today);
    CHECK(t.m_hour == 9);
    t = ClassifyTime(now - 14 * 60 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_yesterday);
    CHECK(t.m_day == 6);
    CHECK(t.m_hour == 22);
    t = ClassifyTime(now - 30LL * 24 * 60 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_this_year);
    CHECK(t.m_month == 9);
    t = ClassifyTime(now - 400LL * 24 * 60 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_older);
    CHECK(t.m_year == 2025);
    //本机时钟比服务端慢：将来的时间按「刚刚」
    t = ClassifyTime(now + 5 * kMinuteMs, now, kBeijing);
    CHECK(t.m_kind == time_just_now);
}

namespace {

//一个 @ 候选
ThreadAtCandidate MakeCandidate(int64_t userId, const wchar_t* name)
{
    ThreadAtCandidate c;
    c.m_userId = userId;
    c.m_name = name;
    return c;
}

//一段 @ 片段
ThreadAtSpan MakeSpan(int64_t userId, int off, int len)
{
    ThreadAtSpan s;
    s.m_userId = userId;
    s.m_off = off;
    s.m_len = len;
    return s;
}

} // namespace

TEST_CASE("TGD28 @ 片段：发送前重扫、从原文取名字、编解码往返", "[threaddll]")
{
    const int64_t kZhang = 4103;
    const int64_t kLi = 4104;
    const int64_t kZhangShort = 4105;
    //「请看 」7 字节；「@张三」7 字节从 7 开始；「 和 」5 字节；「@李四」从 19 开始
    const std::wstring wire = L"请看 @张三 和 @李四 好的";
    std::vector<ThreadAtCandidate> cands;
    cands.push_back(MakeCandidate(kZhangShort, L"张"));
    cands.push_back(MakeCandidate(kZhang, L"张三"));
    cands.push_back(MakeCandidate(kLi, L"李四"));
    const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, cands);
    REQUIRE(at.size() == 2);
    //同一位置取名字最长的候选
    CHECK(at[0].m_userId == kZhang);
    CHECK(at[0].m_off == 7);
    CHECK(at[0].m_len == 7);
    CHECK(at[1].m_userId == kLi);
    CHECK(at[1].m_off == 19);

    //插入后又删掉一半、或前面不是空白：都不算
    CHECK(RescanAtSpans(L"请看 @张 好", std::vector<ThreadAtCandidate>(1, MakeCandidate(kZhang, L"张三"))).empty());
    CHECK(RescanAtSpans(L"abc@张三", std::vector<ThreadAtCandidate>(1, MakeCandidate(kZhang, L"张三"))).empty());
    CHECK(RescanAtSpans(wire, std::vector<ThreadAtCandidate>()).empty());

    //从原文的片段取回当时的名字；越界、不以 '@' 开头的跳过
    std::vector<ThreadAtSpan> withBad = at;
    withBad.push_back(MakeSpan(kLi, 100, 7));
    withBad.push_back(MakeSpan(kLi, 0, 3));
    const std::vector<ThreadAtCandidate> back = CandidatesFromSpans(wire, withBad);
    REQUIRE(back.size() == 2);
    CHECK(back[0].m_userId == kZhang);
    CHECK(back[0].m_name == L"张三");
    CHECK(back[1].m_name == L"李四");

    //编解码：有片段时写出 at（uid 为字符串），没有时不写；读回时跳过格式不对的项
    const std::string req = ThreadCodec::BuildAddComment(100, 0, "tc-1", wire, std::vector<int64_t>(1, kZhang), at);
    CHECK(req.find("\"at\":[{") != std::string::npos);
    CHECK(req.find("\"uid\":\"4103\"") != std::string::npos);
    CHECK(ThreadCodec::BuildAddComment(100, 0, "tc-1", wire, std::vector<int64_t>()).find("\"at\"") == std::string::npos);
    const std::string postJson =
        "{\"code\":0,\"msg\":\"ok\",\"post\":{\"postid\":\"5\",\"groupid\":\"6\",\"author\":\"7\","
        "\"text\":\"x\",\"at\":[{\"uid\":\"4103\",\"off\":0,\"len\":7},{\"uid\":4104,\"off\":8,\"len\":7},"
        "{\"uid\":\"x1\",\"off\":0,\"len\":1},{\"uid\":\"9\",\"off\":\"x\",\"len\":1},\"bad\"]}}";
    int code = -1;
    ThreadPost post;
    bool hasPost = false;
    REQUIRE(ThreadCodec::ParsePostResponse(postJson, code, post, hasPost));
    REQUIRE(hasPost);
    REQUIRE(post.m_at.size() == 2);
    CHECK(post.m_at[0].m_userId == kZhang);
    CHECK(post.m_at[1].m_userId == kLi);
    CHECK(post.m_at[1].m_off == 8);
}

TEST_CASE("TGD29 @ 片段的显示换算：表情、转义与 @ 到本人", "[threaddll]")
{
    std::set<int> known;
    known.insert(1);
    const int kUnknownChars = 4;   //不认识的表情显示成 4 个字符的替代文字（如「[表情]」）
    int ds = 0;
    int de = 0;

    //「a」+ 表情 1（线上 7 个字符）+「@张三」：认识的表情在显示里占 1 个字符
    const std::wstring withFace = L"a/f[\"1\"]@张三";
    REQUIRE(WireRangeToDisplay(withFace, 8, 11, known, kUnknownChars, ds, de));
    CHECK(ds == 2);
    CHECK(de == 5);
    //不认识的表情按替代文字的长度算
    REQUIRE(WireRangeToDisplay(withFace, 8, 11, std::set<int>(), kUnknownChars, ds, de));
    CHECK(ds == 5);
    CHECK(de == 8);
    //落在表情标签中间：拒绝
    CHECK_FALSE(WireRangeToDisplay(withFace, 2, 9, known, kUnknownChars, ds, de));
    //越界、空区间：拒绝
    CHECK_FALSE(WireRangeToDisplay(withFace, 8, 100, known, kUnknownChars, ds, de));
    CHECK_FALSE(WireRangeToDisplay(withFace, 8, 8, known, kUnknownChars, ds, de));

    //「//」转义：两个 '/' 显示成一个，后面的标签按普通文字显示
    const std::wstring escaped = L"//f[\"1\"]@x";
    REQUIRE(WireRangeToDisplay(escaped, 8, 10, known, kUnknownChars, ds, de));
    CHECK(ds == 7);
    CHECK(de == 9);

    //整段换算：越界与落在多字节字符中间的片段丢弃；@ 到本人的标出来
    const std::wstring wire = L"@张三 @李四";
    std::vector<ThreadAtSpan> at;
    at.push_back(MakeSpan(4103, 0, 7));
    at.push_back(MakeSpan(4104, 8, 7));
    at.push_back(MakeSpan(4105, 100, 3));
    at.push_back(MakeSpan(4106, 2, 3));
    const std::vector<ThreadAtDisplay> ranges = AtSpansToDisplay(wire, at, 4104, known, kUnknownChars);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0].m_start == 0);
    CHECK(ranges[0].m_end == 3);
    CHECK_FALSE(ranges[0].m_isMe);
    CHECK(ranges[1].m_start == 4);
    CHECK(ranges[1].m_end == 7);
    CHECK(ranges[1].m_isMe);
    CHECK(AtSpansToDisplay(wire, std::vector<ThreadAtSpan>(), 4104, known, kUnknownChars).empty());
}
