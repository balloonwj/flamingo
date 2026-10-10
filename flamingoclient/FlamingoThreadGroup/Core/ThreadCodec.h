/**
 *  帖子群报文的编解码（纯函数，2700 段，docs/帖子群方案.md 第 6 节、docs/协议设计-v2.md 帖子群一节）：
 *  组装 2701~2715 的请求 JSON，解析它们的应答与 2720 推送。字段名与服务端 chatserversrc/ThreadProtocol.cpp 一致：
 *  ID 一律为十进制字符串（解析时整数写法也接受），文字为 UTF-8。
 *
 *  典型用法：
 *      std::string body = threadgroup::ThreadCodec::BuildListPosts(groupId, thread_sort_activity, "", 20);
 *      ...收到应答...
 *      int code = 0;
 *      std::vector<threadgroup::ThreadPost> posts;
 *      std::string next;
 *      bool more = false;
 *      threadgroup::ThreadCodec::ParseListPosts(json, code, posts, next, more);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ThreadTypes.h"

namespace threadgroup {

/**
 *  帖子群报文的编解码，全部为静态方法。
 */
class ThreadCodec
{
public:
    //---- 请求 ----

    //2701：拉一页帖子。cursor 为空表示第一页
    static std::string BuildListPosts(int64_t groupId, int sort, const std::string& cursor, int limit);
    //2702 / 2705：只带 postid
    static std::string BuildPostId(int64_t postId);
    /**
     *  2703：发帖。
     *  @param clientPostId 本机生成的去重标识（应答超时重发时用同一个）
     *  @param title / text 标题与线上正文（UTF-16，组包时转 UTF-8）
     */
    static std::string BuildPublishPost(int64_t groupId, const std::string& clientPostId, const std::wstring& title,
                                        const std::wstring& text, const std::vector<ThreadImage>& images,
                                        const std::vector<int64_t>& mentions,
                                        const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());
    //2704：修改帖子；at 为正文里 @ 片段的位置（为空时不写该字段，docs/帖子群方案.md 14.4），以下 2709、2714 同
    static std::string BuildEditPost(int64_t postId, const std::wstring& title, const std::wstring& text,
                                     const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                                     const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());
    //2706：置顶（pin 为 true）或取消置顶
    static std::string BuildPin(int64_t postId, bool pin);
    //2707：拉一页留言，cursor 为上一页最后一条留言的 ID，0 为第一页
    static std::string BuildListComments(int64_t postId, int64_t cursor, int limit);
    //2708：拉某条留言下的一页回复
    static std::string BuildListReplies(int64_t commentId, int64_t cursor, int limit);
    //2709：留言（parentId 为 0）或回复（parentId 为被回复的那条留言或回复）
    static std::string BuildAddComment(int64_t postId, int64_t parentId, const std::string& clientCommentId,
                                       const std::wstring& text, const std::vector<int64_t>& mentions,
                                       const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());
    //2710：只带 commentid
    static std::string BuildCommentId(int64_t commentId);
    //2711：加上（on 为 true）或取消一种互动
    static std::string BuildReact(int targetType, int64_t targetId, int reaction, bool on);
    //2712：拉加了某种互动的用户
    static std::string BuildGetReactors(int targetType, int64_t targetId, int reaction, int limit);
    //2713：登记（watch 为 true）或撤销「正在查看」
    static std::string BuildWatch(int64_t groupId, bool watch);
    //2714：追加补充
    static std::string BuildAddAddendum(int64_t postId, const std::string& clientAddId, const std::wstring& text,
                                        const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                                        const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());
    //2715：只带 addid
    static std::string BuildAddendumId(int64_t addId);

    //---- 应答 ----
    //以下函数在报文不是 JSON 对象时返回 false；是对象时返回 true，code 取应答的 code（缺失按服务器内部错误）。
    //code 不为 0 时其余出参保持空。

    //只取 code
    static bool ParseCode(const std::string& json, int& code);
    //2701 应答
    static bool ParseListPosts(const std::string& json, int& code, std::vector<ThreadPost>& posts,
                               std::string& nextCursor, bool& hasMore);
    //2702 / 2703 / 2704 / 2706 应答：hasPost 表示应答里带了帖子
    static bool ParsePostResponse(const std::string& json, int& code, ThreadPost& post, bool& hasPost);
    //2707 / 2708 应答：ownerId 为楼主
    static bool ParseCommentsResponse(const std::string& json, int& code, int64_t& ownerId,
                                      std::vector<ThreadComment>& comments, int64_t& nextCursor, bool& hasMore);
    //2709 应答
    static bool ParseCommentResponse(const std::string& json, int& code, ThreadComment& comment, bool& hasComment);
    //2711 应答：目标最新的互动汇总（mine 为本人视角）
    static bool ParseReactResponse(const std::string& json, int& code, int& targetType, int64_t& targetId,
                                   std::vector<ThreadReaction>& reactions);
    //2712 应答
    static bool ParseReactorsResponse(const std::string& json, int& code, int& targetType, int64_t& targetId,
                                      int& reaction, std::vector<int64_t>& users, int& total);
    //2714 应答
    static bool ParseAddendumResponse(const std::string& json, int& code, ThreadAddendum& addendum, bool& hasAddendum);
    //带一个 ID 字段的应答（2705 的 postid、2710 的 commentid、2715 的 addid）
    static bool ParseIdResponse(const std::string& json, const char* key, int& code, int64_t& id);

    //---- 推送 ----

    //2720：groupid 缺失或不合法时返回 false
    static bool ParseChangedNotify(const std::string& json, ThreadChange& change);
};

} // namespace threadgroup
