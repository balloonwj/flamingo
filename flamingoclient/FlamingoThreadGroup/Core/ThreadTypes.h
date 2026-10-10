/**
 *  帖子群客户端的数据结构（与服务端 chatserversrc/ThreadProtocol.h 的应答字段一一对应，docs/帖子群方案.md 第 5、6 节）。
 *
 *  只放纯数据，不触及界面与网络；由 ThreadCodec 从应答与推送的 JSON 解析出来，由帖子列表模型、帖子详情模型保存。
 *  界面上显示的文字（标题、正文）保存为 UTF-16（std::wstring），正文里的表情仍是线上的 /f["id"] 编码，显示前由
 *  ThreadText 换成「[表情名]」；ID 一律为 64 位整数；fileid、md5 等标识保留原样的 UTF-8 / ASCII 字符串。
 *
 *  典型用法：
 *      threadgroup::ThreadPost post;
 *      if (threadgroup::ThreadCodec::ParsePostResponse(json, code, post)) { ... }
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ThreadMention.h"   // ThreadAtSpan：正文里 @ 片段的位置

namespace threadgroup {

//---- 与服务端一致的长度与数量上限（chatserversrc/ThreadProtocol.h，docs/帖子群方案.md 4.2~4.4） ----
const int kTitleMaxChars = 60;          //帖子标题最多 60 个字符（可为空）
const int kPostTextMaxChars = 5000;     //帖子正文最多 5000 个字符
const int kAddendumTextMaxChars = 2000; //一段补充最多 2000 个字符
const int kCommentTextMaxChars = 1000;  //留言与回复最多 1000 个字符
const int kMaxImages = 9;               //帖子与每段补充最多 9 张图
const int kMaxAddenda = 20;             //每个帖子最多 20 段补充
const int kMaxPins = 3;                 //每个帖子群最多置顶 3 条
const int kMaxMentions = 50;            //一次最多 @ 50 人

//---- 互动编号（与服务端 t_thread_reaction.f_reaction 一致） ----
const int kReactionLike = 0;            //「赞」
const int kReactionFaceBase = 1000;     //表情回应：1000 + 表情编号
const int kReactionMaxFaceId = 999;     //表情编号上限

//---- 分页 ----
const int kPostPageSize = 20;           //帖子列表一页的条数
const int kCommentPageSize = 20;        //留言一页的条数
const int kReplyPageSize = 20;          //「查看更多回复」一页的条数
const int kReactorListLimit = 20;       //互动名单最多列出的人数（docs/帖子群方案.md 4.4：最多列 20 个名字）

//帖子群命令号（与 Source/net/Protocol.h 的 cmd_thread_* 一致；DLL 不包含主程序的协议头，在这里重复一份，
//由单元测试 TGD15 核对两边数值相同）
enum ThreadCmd
{
    thread_cmd_list_posts      = 2701,  //分页拉帖子摘要
    thread_cmd_get_post        = 2702,  //拉一条完整帖子（含补充）
    thread_cmd_publish_post    = 2703,  //发帖
    thread_cmd_edit_post       = 2704,  //作者修改帖子
    thread_cmd_delete_post     = 2705,  //删除帖子
    thread_cmd_pin_post        = 2706,  //置顶 / 取消置顶
    thread_cmd_list_comments   = 2707,  //分页拉留言
    thread_cmd_list_replies    = 2708,  //分页拉某条留言下的回复
    thread_cmd_add_comment     = 2709,  //留言或回复
    thread_cmd_delete_comment  = 2710,  //删除留言或回复
    thread_cmd_react           = 2711,  //点赞 / 表情回应或取消
    thread_cmd_get_reactors    = 2712,  //拉互动名单
    thread_cmd_watch           = 2713,  //登记 / 撤销「正在查看」
    thread_cmd_add_addendum    = 2714,  //楼主追加补充
    thread_cmd_delete_addendum = 2715,  //删除一段补充
    thread_cmd_changed_notify  = 2720,  //推送：帖子群内容变化
};

//2720 推送的 changetype（与 Protocol.h 的 thread_change_type 一致）
enum ThreadChangeType
{
    thread_change_post_new        = 1,  //有人发了新帖
    thread_change_post_edit       = 2,  //帖子被作者修改
    thread_change_post_delete     = 3,  //帖子被删除
    thread_change_post_pin        = 4,  //帖子被置顶或取消置顶
    thread_change_addendum_new    = 5,  //楼主追加了补充
    thread_change_addendum_delete = 6,  //补充被删除
    thread_change_comment_new     = 7,  //新的留言或回复（只推给正在查看的连接）
    thread_change_comment_delete  = 8,  //留言或回复被删除（只推给正在查看的连接）
    thread_change_reaction        = 9,  //某个目标的互动汇总变了（只推给正在查看的连接）
};

//帖子列表的排序（与 Protocol.h 的 thread_sort 一致）
enum ThreadSort
{
    thread_sort_activity = 1,   //最新动态：按最近一次发帖、留言、回复或补充的时间倒序（缺省）
    thread_sort_publish  = 2,   //最新发布：按发帖时间倒序
};

//互动目标类型（与 Protocol.h 的 thread_target_type 一致）
enum ThreadTargetType
{
    thread_target_post    = 1,  //帖子本身（补充不单独互动，算在帖子上）
    thread_target_comment = 2,  //留言或回复
};

//本人在群里的角色（与 Protocol.h 的 group_member_role 一致）；不是成员时为 -1
enum ThreadGroupRole
{
    thread_role_none   = -1,  //不是群成员（已退群、被移出或群被解散）
    thread_role_member = 0,   //普通成员
    thread_role_admin  = 1,   //管理员
    thread_role_owner  = 2,   //群主
};

//群设置「谁可以 @ 全体」（与 Protocol.h 的 group_at_all_scope 一致，docs/帖子群方案.md §15.1 第 1 条）
enum ThreadAtAllScope
{
    thread_at_all_owner_admin = 0,   //仅群主和管理员（默认）
    thread_at_all_everyone    = 1,   //群内所有人
};

//会用到的服务端错误码（与 Protocol.h 的 error_code 一致）
enum ThreadErrorCode
{
    thread_error_ok                = 0,     //成功
    thread_error_server_internal   = 1,     //服务器内部错误
    thread_error_bad_request       = 3,     //报文格式错误
    thread_error_group_not_exist   = 301,   //群不存在
    thread_error_not_group_member  = 302,   //不是群成员
    thread_error_member_muted      = 306,   //被禁言，或仅通知模式下不是管理员
    thread_error_sensitive_word    = 312,   //包含敏感词
    thread_error_must_read_pending = 315,   //还没有确认入群必读
    thread_error_disabled          = 1300,  //本企业关闭了帖子群
    thread_error_not_exist         = 1301,  //帖子、补充、留言或回复不存在（已被删除）
    thread_error_no_permission     = 1302,  //没有权限
    thread_error_invalid           = 1303,  //字段不合法：长度、图片数、表情编号、空内容
    thread_error_pin_full          = 1304,  //置顶已满 3 条
    thread_error_not_thread_group  = 1305,  //目标群不是帖子群
    thread_error_addendum_full     = 1306,  //补充已满 20 段
    thread_error_timeout           = -1,    //客户端判定：请求没有发出去或应答超时（不是服务端的错误码）
};

//帖子里的一张图片（与图片消息 content 的字段一致）
struct ThreadImage
{
    std::string m_fileId;       //原图文件 ID
    std::string m_md5;          //原图 MD5
    int         m_width;        //宽（像素）
    int         m_height;       //高（像素）
    int64_t     m_size;         //字节数
    std::string m_thumbFileId;  //缩略图文件 ID，可为空

    ThreadImage()
        : m_width(0)
        , m_height(0)
        , m_size(0)
    {
    }
};

//一个目标上某一种互动的汇总
struct ThreadReaction
{
    int  m_reaction;   //互动编号：0 为赞，1000 + 表情编号为表情回应
    int  m_count;      //加了这种互动的人数
    bool m_mine;       //本人是否加过

    ThreadReaction()
        : m_reaction(0)
        , m_count(0)
        , m_mine(false)
    {
    }
};

//楼主的一段补充
struct ThreadAddendum
{
    int64_t                  m_addId;       //补充 ID
    int64_t                  m_postId;      //所属帖子
    int64_t                  m_author;      //补充人（楼主）
    std::wstring             m_text;        //正文（表情为 /f["id"] 编码）
    std::vector<ThreadImage> m_images;      //图片
    std::vector<int64_t>     m_mentions;    //@ 到的人
    std::vector<ThreadAtSpan> m_at;         //正文里 @ 片段的位置（docs/帖子群方案.md 14.4）
    int64_t                  m_createTime;  //时间，Unix 毫秒

    ThreadAddendum()
        : m_addId(0)
        , m_postId(0)
        , m_author(0)
        , m_createTime(0)
    {
    }
};

//一条留言或回复
struct ThreadComment
{
    int64_t                     m_commentId;   //ID
    int64_t                     m_postId;      //所属帖子
    int64_t                     m_author;      //作者
    int64_t                     m_parentId;    //0 为留言，否则为所属留言的 ID
    int64_t                     m_replyTo;     //被回复的人；留言为 0
    std::wstring                m_text;        //正文（表情为 /f["id"] 编码）；已删除的占位为空
    std::vector<int64_t>        m_mentions;    //@ 到的人
    std::vector<ThreadAtSpan>   m_at;          //正文里 @ 片段的位置；已删除的占位为空
    int64_t                     m_createTime;  //时间，Unix 毫秒
    bool                        m_deleted;     //已删除但保留占位（留言下面还有回复时）
    int                         m_replyCount;  //留言下面未删除的回复数；回复本身为 0
    std::vector<ThreadReaction> m_reactions;   //互动汇总
    std::vector<ThreadComment>  m_replies;     //已加载的回复（留言列表顺带的前几条，以及「查看更多回复」拉到的）
    int64_t                     m_replyCursor; //再拉回复用的游标：已加载的最后一条回复的 ID
    bool                        m_repliesLoading; //正在拉这条留言的回复

    ThreadComment()
        : m_commentId(0)
        , m_postId(0)
        , m_author(0)
        , m_parentId(0)
        , m_replyTo(0)
        , m_createTime(0)
        , m_deleted(false)
        , m_replyCount(0)
        , m_replyCursor(0)
        , m_repliesLoading(false)
    {
    }
};

//一条帖子（列表里是摘要形态：正文截取、最多 3 张图；详情里是完整形态，带全部补充）
struct ThreadPost
{
    int64_t                     m_postId;         //帖子 ID
    int64_t                     m_groupId;        //所属帖子群
    int64_t                     m_author;         //作者
    std::wstring                m_title;          //标题，可为空
    std::wstring                m_text;           //正文（表情为 /f["id"] 编码）
    bool                        m_textTruncated;  //摘要形态下正文被截取了
    std::vector<ThreadImage>    m_images;         //图片（摘要形态最多 3 张）
    int                         m_imageCount;     //图片总数
    std::vector<int64_t>        m_mentions;       //@ 到的人
    std::vector<ThreadAtSpan>   m_at;             //正文里 @ 片段的位置（摘要形态只带落在截取范围内的）
    int64_t                     m_createTime;     //发帖时间，Unix 毫秒
    int64_t                     m_editTime;       //最后修改时间；0 表示没有修改过
    int64_t                     m_activeTime;     //最新动态时间
    int                         m_commentCount;   //未删除的留言与回复总数
    int64_t                     m_pinTime;        //置顶时间；0 表示没有置顶
    int                         m_addendumCount;  //补充段数
    std::vector<ThreadReaction> m_reactions;      //互动汇总
    std::vector<ThreadAddendum> m_addenda;        //全部补充（只在完整形态里有）
    bool                        m_full;           //是否完整形态（2702 / 2703 / 2704 的应答）

    ThreadPost()
        : m_postId(0)
        , m_groupId(0)
        , m_author(0)
        , m_textTruncated(false)
        , m_imageCount(0)
        , m_createTime(0)
        , m_editTime(0)
        , m_activeTime(0)
        , m_commentCount(0)
        , m_pinTime(0)
        , m_addendumCount(0)
        , m_full(false)
    {
    }
};

//2720 推送解析后的内容
struct ThreadChange
{
    int64_t                     m_groupId;       //帖子群
    int                         m_changeType;    //变化类型，取值见 ThreadChangeType
    int64_t                     m_operator;      //动作发起人
    int64_t                     m_postId;        //相关帖子
    bool                        m_hasPost;       //带了摘要形态的帖子（新帖、修改、置顶）
    ThreadPost                  m_post;          //m_hasPost 为 true 时有效
    int64_t                     m_commentId;     //相关留言或回复的 ID；没有为 0
    bool                        m_hasComment;    //带了留言全文（新留言；删除留言而保留占位时带占位）
    ThreadComment               m_comment;       //m_hasComment 为 true 时有效
    int64_t                     m_addId;         //相关补充的 ID；没有为 0
    bool                        m_hasAddendum;   //带了补充全文（新补充）
    ThreadAddendum              m_addendum;      //m_hasAddendum 为 true 时有效
    int                         m_commentCount;  //帖子当前的留言与回复总数；推送里没带为 -1
    int64_t                     m_activeTime;    //帖子当前的最新动态时间；推送里没带为 0
    int                         m_targetType;    //互动变化的目标类型；不是互动变化为 0
    int64_t                     m_targetId;      //互动变化的目标 ID
    int                         m_reaction;      //互动编号
    bool                        m_on;            //加上还是取消
    std::vector<ThreadReaction> m_reactions;     //目标最新的互动汇总（m_mine 一律为 false）

    ThreadChange()
        : m_groupId(0)
        , m_changeType(0)
        , m_operator(0)
        , m_postId(0)
        , m_hasPost(false)
        , m_commentId(0)
        , m_hasComment(false)
        , m_addId(0)
        , m_hasAddendum(false)
        , m_commentCount(-1)
        , m_activeTime(0)
        , m_targetType(0)
        , m_targetId(0)
        , m_reaction(0)
        , m_on(false)
    {
    }
};

} // namespace threadgroup
