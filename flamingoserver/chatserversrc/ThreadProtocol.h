/**
 *  帖子群协议与规则（cmd_thread_* 2701~2720，docs/帖子群方案.md 第 4~6 节），ThreadProtocol.h
 *
 *  本文件只放纯逻辑，不触及网络与数据库，便于单元测试（tests/test_threadgroup.cpp，Suite TG）：
 *    - 数据结构：帖子、楼主补充、留言与回复、互动汇总；
 *    - 请求解析：各命令的 JSON 包体 → 请求结构；
 *    - 字段校验：标题、正文长度（按字符计）、图片数、表情编号、空内容；
 *    - 权限判定：谁能修改、删除、置顶、追加补充；
 *    - 提醒对象：留言、回复、补充之后，帖子动态卡片（msgtype 26）对谁可见；
 *    - JSON 组包：各命令的应答、2720 推送、两种卡片的 content。
 *
 *  帖子与补充的内容统一用 ThreadBody 表示，落库为 t_thread_* 表 f_content 列里的 JSON：
 *      {"title":"...","text":"...","images":[{fileid,md5,width,height,size,thumbfileid}],"mentions":["uid",...]}
 *  ID 一律写成十进制字符串（v2 惯例）。
 *
 *  典型用法（ChatSessionThread.cpp）：
 *      PublishPostRequest req;
 *      if (!ThreadProtocol::parsePublishPostRequest(body, req)) { ...回 3... }
 *      int code = ThreadProtocol::validateBody(req.body, ThreadProtocol::kBodyPost);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

//---- 长度与数量上限（docs/帖子群方案.md 4.2~4.4） ----
const int kThreadTitleMaxChars = 60;          //帖子标题最多 60 个字符（可为空）
const int kThreadPostTextMaxChars = 5000;     //帖子正文最多 5000 个字符
const int kThreadAddendumTextMaxChars = 2000; //一段补充最多 2000 个字符
const int kThreadCommentTextMaxChars = 1000;  //留言与回复最多 1000 个字符
const int kThreadMaxImages = 9;               //帖子与每段补充最多 9 张图
const int kThreadMaxAddenda = 20;             //每个帖子最多 20 段补充
const int kThreadMaxPins = 3;                 //每个帖子群最多置顶 3 条
const int kThreadMaxMentions = 50;            //一次最多 @ 50 人
const int kThreadMaxMentionSpans = 100;       //正文里最多保留 100 段 @ 片段（同一个人可以被 @ 多次）
const int kThreadMaxIdTextBytes = 64;         //fileid、md5、去重标识等短字符串的字节上限
const int kThreadMaxCheckCards = 500;         //2716 一次最多核对 500 张卡片（docs/帖子群方案.md 14.6）

//---- 互动编号（t_thread_reaction.f_reaction） ----
const int kThreadReactionLike = 0;            //「赞」
const int kThreadReactionFaceBase = 1000;     //表情回应：1000 + 表情编号
const int kThreadMaxFaceId = 999;             //表情编号上限（现有表情库为 0~142，留出扩充余地）

//---- 分页 ----
const int kThreadDefaultPageSize = 20;        //一页缺省条数
const int kThreadMaxPageSize = 50;            //一页最多条数
const int kThreadPreviewReplies = 3;          //留言列表里每条留言顺带的回复条数
const int kThreadListTextChars = 300;         //帖子列表里正文截取的字符数
const int kThreadListImages = 3;              //帖子列表里每条帖子带的图片数
const int kThreadDigestChars = 100;           //卡片摘要截取的字符数
const int kThreadMaxReactors = 50;            //一次最多列出的互动人数

//帖子里的一张图片（与图片消息的 content 字段一致，docs/协议设计-v2.md 10.2）
struct ThreadImage
{
    std::string fileid;       //原图文件 ID
    std::string md5;          //原图 MD5
    int         width;        //宽（像素）
    int         height;       //高（像素）
    int64_t     size;         //字节数
    std::string thumbfileid;  //缩略图文件 ID，可为空

    ThreadImage()
        : width(0)
        , height(0)
        , size(0)
    {
    }
};

//正文里的一段「@名字」（docs/帖子群方案.md 14.4），与普通群消息 content.at.users 的一项相同：
//off / len 是这一段在正文 text（UTF-8）里的字节偏移与长度，'@' 计入，其后的分隔空格不计入
struct ThreadMentionSpan
{
    int64_t uid;   //被 @ 的人，大于 0
    int     off;   //字节偏移
    int     len;   //字节长度，大于 0

    ThreadMentionSpan()
        : uid(0)
        , off(0)
        , len(0)
    {
    }
};

//帖子、补充、留言与回复共用的内容
struct ThreadBody
{
    std::string                    title;     //标题：只有帖子有，可为空
    std::string                    text;      //正文（UTF-8），表情以 /f["id"] 编码
    std::vector<ThreadImage>       images;    //图片：帖子与补充可有，留言与回复没有
    std::vector<int64_t>           mentions;  //@ 到的成员 userid（决定谁收到 @ 提醒）
    std::vector<ThreadMentionSpan> at;        //@ 片段的位置（客户端据此给「@名字」加背景色）；可为空。「@所有人」的片段 uid 为 0
    bool                           mentionall; //是否 @ 所有人（docs/帖子群方案.md §15）：为真时全体成员（发布者除外）按被 @ 处理

    ThreadBody()
        : mentionall(false)
    {
    }
};

//一个目标上某一种互动的汇总
struct ThreadReaction
{
    int  reaction;   //互动编号：0 为赞，1000 + 表情编号为表情回应
    int  count;      //加了这种互动的人数
    bool mine;       //看的人自己是否加过

    ThreadReaction()
        : reaction(0)
        , count(0)
        , mine(false)
    {
    }
};

//楼主补充
struct ThreadAddendum
{
    int64_t    addid;        //补充 ID
    int64_t    postid;       //所属帖子
    int64_t    groupid;      //所属帖子群
    int64_t    author;       //补充人（楼主）
    ThreadBody body;         //内容（没有标题）
    int64_t    createTime;   //时间，Unix 毫秒
    int64_t    cardMsgId;    //这段补充的动态卡片（msgtype 26）的 msgid；0 表示没有写卡片（没有要提醒的人）

    ThreadAddendum()
        : addid(0)
        , postid(0)
        , groupid(0)
        , author(0)
        , createTime(0)
        , cardMsgId(0)
    {
    }
};

//留言或回复
struct ThreadComment
{
    int64_t                     commentid;        //ID
    int64_t                     postid;           //所属帖子
    int64_t                     groupid;          //所属帖子群
    int64_t                     author;           //作者
    int64_t                     parentid;         //0 为留言，否则为所属留言的 ID
    int64_t                     replyTo;          //被回复的人；留言为 0
    std::string                 clientCommentId;  //客户端生成的去重标识
    ThreadBody                  body;             //内容（只有 text 与 mentions）
    int64_t                     createTime;       //时间，Unix 毫秒
    bool                        deleted;          //已删除但保留占位（留言下面还有回复时）
    int                         replyCount;       //留言下面未删除的回复数；回复本身为 0
    int64_t                     cardMsgId;        //这条留言或回复的动态卡片（msgtype 26）的 msgid；0 表示没有写卡片
    std::vector<ThreadReaction> reactions;        //互动汇总（下发前由调用方填）
    std::vector<ThreadComment>  previewReplies;   //留言列表里顺带的前几条回复（下发前由调用方填）

    ThreadComment()
        : commentid(0)
        , postid(0)
        , groupid(0)
        , author(0)
        , parentid(0)
        , replyTo(0)
        , createTime(0)
        , deleted(false)
        , replyCount(0)
        , cardMsgId(0)
    {
    }
};

//一张卡片（帖子卡片或动态卡片）的当前内容：2716 应答与 2721 推送里的一项
struct ThreadCardState
{
    int64_t     msgid;     //卡片的 msgid
    int         msgtype;   //message_type_thread_post 或 message_type_thread_reply
    std::string content;   //卡片当前的 content（JSON 文本，不带落库外壳）

    ThreadCardState()
        : msgid(0)
        , msgtype(0)
    {
    }
};

//帖子
struct ThreadPost
{
    int64_t                     postid;        //帖子 ID
    int64_t                     groupid;       //所属帖子群
    int64_t                     author;        //作者
    std::string                 clientPostId;  //客户端生成的发帖去重标识
    ThreadBody                  body;          //内容
    int64_t                     createTime;    //发帖时间，Unix 毫秒
    int64_t                     editTime;      //最后修改时间；0 表示没有修改过
    int64_t                     activeTime;    //最新动态时间
    int                         commentCount;  //未删除的留言与回复总数
    int64_t                     pinTime;       //置顶时间；0 表示没有置顶
    int64_t                     cardMsgId;     //帖子卡片的 msgid；0 表示没有写成
    int                         addendumCount; //补充段数（下发前由调用方填）
    std::vector<ThreadReaction> reactions;     //互动汇总（下发前由调用方填）
    std::vector<ThreadAddendum> addenda;       //全部补充（只在 2702 的完整帖子里填）

    ThreadPost()
        : postid(0)
        , groupid(0)
        , author(0)
        , createTime(0)
        , editTime(0)
        , activeTime(0)
        , commentCount(0)
        , pinTime(0)
        , cardMsgId(0)
        , addendumCount(0)
    {
    }
};

//---- 请求结构 ----

//2701 拉帖子列表
struct ListPostsRequest
{
    int64_t     groupid;   //帖子群
    int         sort;      //排序，取值见 thread_sort；缺省为最新动态
    std::string cursor;    //游标：空为第一页（第一页另带全部置顶帖），否则为上一页应答的 nextcursor
    int         limit;     //条数，1~kThreadMaxPageSize

    ListPostsRequest()
        : groupid(0)
        , sort(1)
        , limit(kThreadDefaultPageSize)
    {
    }
};

//2703 发帖、2704 修改帖子、2714 追加补充
struct PostWriteRequest
{
    int64_t     groupid;    //帖子群（2703）
    int64_t     postid;     //帖子（2704、2714）
    std::string clientId;   //去重标识（2703 的 clientpostid、2714 的 clientaddid）
    ThreadBody  body;       //内容

    PostWriteRequest()
        : groupid(0)
        , postid(0)
    {
    }
};

//2707 拉留言、2708 拉回复
struct ListCommentsRequest
{
    int64_t postid;      //帖子（2707）
    int64_t commentid;   //留言（2708）
    int64_t cursor;      //游标：上一页最后一条的 ID，0 为第一页
    int     limit;       //条数

    ListCommentsRequest()
        : postid(0)
        , commentid(0)
        , cursor(0)
        , limit(kThreadDefaultPageSize)
    {
    }
};

//2709 留言或回复
struct AddCommentRequest
{
    int64_t     postid;     //帖子
    int64_t     parentid;   //0 为留言，否则为被回复的那条留言或回复的 ID（回复的回复仍挂到所属留言下）
    std::string clientId;   //去重标识 clientcommentid
    ThreadBody  body;       //内容（text 与 mentions）

    AddCommentRequest()
        : postid(0)
        , parentid(0)
    {
    }
};

//2711 互动、2712 拉互动名单
struct ReactRequest
{
    int     targettype;   //目标类型，取值见 thread_target_type
    int64_t targetid;     //目标 ID
    int     reaction;     //互动编号
    bool    on;           //true 为加上，false 为取消（2711）
    int     limit;        //名单条数（2712）

    ReactRequest()
        : targettype(0)
        , targetid(0)
        , reaction(0)
        , on(true)
        , limit(kThreadMaxReactors)
    {
    }
};

/**
 *  帖子群的协议与规则，全部为静态方法。
 */
class ThreadProtocol
{
public:
    //validateBody 的内容种类
    enum BodyKind
    {
        kBodyPost = 1,       //帖子：可有标题，正文 5000 字，最多 9 张图
        kBodyAddendum = 2,   //楼主补充：不能有标题，正文 2000 字，最多 9 张图
        kBodyComment = 3,    //留言与回复：不能有标题与图片，正文 1000 字
    };

    //---- 解析 ----

    //2701：groupid 必填；sort 缺省为最新动态；limit 缺省 20、超过 50 按 50
    static bool parseListPostsRequest(const std::string& json, ListPostsRequest& req);
    //2702 / 2705：只带 postid
    static bool parsePostIdRequest(const std::string& json, int64_t& postid);
    //2703：groupid、clientpostid 必填，title / text / images / mentions 可选
    static bool parsePublishPostRequest(const std::string& json, PostWriteRequest& req);
    //2704：postid 必填，其余同 2703
    static bool parseEditPostRequest(const std::string& json, PostWriteRequest& req);
    //2706：postid 与 pin（1 置顶、0 取消）
    static bool parsePinRequest(const std::string& json, int64_t& postid, bool& pin);
    //2707：postid 必填，cursor 为留言 ID
    static bool parseListCommentsRequest(const std::string& json, ListCommentsRequest& req);
    //2708：commentid 必填，cursor 为回复 ID
    static bool parseListRepliesRequest(const std::string& json, ListCommentsRequest& req);
    //2709：postid、clientcommentid 必填，parentid 缺省为 0
    static bool parseAddCommentRequest(const std::string& json, AddCommentRequest& req);
    //2710：只带 commentid
    static bool parseCommentIdRequest(const std::string& json, int64_t& commentid);
    //2711：targettype、targetid、reaction、on
    static bool parseReactRequest(const std::string& json, ReactRequest& req);
    //2712：targettype、targetid、reaction，limit 可选
    static bool parseGetReactorsRequest(const std::string& json, ReactRequest& req);
    //2713：groupid 与 watch（1 登记、0 撤销）
    static bool parseWatchRequest(const std::string& json, int64_t& groupid, bool& watch);
    //2714：postid、clientaddid 必填，text / images / mentions 可选
    static bool parseAddAddendumRequest(const std::string& json, PostWriteRequest& req);
    //2715：只带 addid
    static bool parseAddendumIdRequest(const std::string& json, int64_t& addid);
    /**
     *  2716：groupid 必填；msgids 为非空数组，元素为十进制字符串或整数，每个都大于 0，最多 kThreadMaxCheckCards 个。
     *  @param msgids [出参] 去重后的 msgid，保持请求里的先后顺序
     */
    static bool parseCheckCardsRequest(const std::string& json, int64_t& groupid, std::vector<int64_t>& msgids);

    //---- 校验与规则 ----

    /**
     *  校验一份内容。
     *  @param body 内容
     *  @param kind 内容种类，取值见 BodyKind
     *  @return error_code_ok；不合法时返回 error_code_thread_invalid
     */
    static int validateBody(const ThreadBody& body, int kind);

    /**
     *  判断互动编号是否合法：0（赞）或 1000~1999（表情回应）。
     */
    static bool isValidReaction(int reaction);

    /**
     *  统计 UTF-8 文本的字符数（按码点计，非法字节各算一个）。
     */
    static int utf8Chars(const std::string& text);

    /**
     *  截取 UTF-8 文本的前 maxChars 个字符，不切断多字节字符。
     *  @param text     原文
     *  @param maxChars 最多保留的字符数
     *  @param cut      [出参] 是否截断了
     */
    static std::string utf8Prefix(const std::string& text, int maxChars, bool& cut);

    /**
     *  生成卡片与列表用的一行摘要：取正文前 maxChars 个字符，换行与连续空白折成一个空格，去掉首尾空白。
     */
    static std::string makeDigest(const std::string& text, int maxChars);

    //只有作者能修改帖子
    static bool canEditPost(int64_t authorId, int64_t userId);
    //作者、群主、管理员能删除帖子、补充、留言与回复；role 取值见 group_role
    static bool canDeleteContent(int64_t authorId, int64_t userId, int role);
    //只有群主、管理员能置顶
    static bool canPin(int role);
    //只有楼主能追加补充
    static bool canAddAddendum(int64_t postAuthorId, int64_t userId);

    /**
     *  计算帖子动态卡片（msgtype 26）的可见者（docs/帖子群方案.md 4.5）：
     *    - 留言：楼主；
     *    - 回复：楼主与被回复的人；
     *    - 补充：在该帖下留过言或回复过的人；
     *    另加 mentions 里的人；一律去掉动作发起人本人与 0，去重并升序。
     *  @param kind        卡片种类，取值见 thread_reply_kind
     *  @param postAuthor  楼主
     *  @param replyTo     被回复的人（只在回复时有意义）
     *  @param commenters  该帖下留过言或回复过的人（只在补充时有意义）
     *  @param mentions    @ 到的人
     *  @param operatorId  动作发起人
     *  @param out         [出参] 可见者
     */
    static void replyCardAudience(int kind, int64_t postAuthor, int64_t replyTo, const std::vector<int64_t>& commenters,
                                  const std::vector<int64_t>& mentions, int64_t operatorId, std::vector<int64_t>& out);

    /**
     *  留言、回复或补充带「@所有人」时，把全体成员并入动态卡片的可见者（docs/帖子群方案.md §15.1 第 2 条）：
     *  并入后去掉动作发起人本人与 0，去重并升序，与 replyCardAudience 的输出格式相同。
     *  @param members    群的全部成员 userid
     *  @param operatorId 动作发起人
     *  @param audience   [入参 / 出参] replyCardAudience 算出的可见者，并入全体成员后原地改写
     */
    static void mergeMentionAllAudience(const std::vector<int64_t>& members, int64_t operatorId,
                                        std::vector<int64_t>& audience);

    /**
     *  只保留属于本群成员的 @：发帖与留言只能 @ 本群成员，名单外的静默丢弃。
     */
    static void filterMentions(std::vector<int64_t>& mentions, const std::vector<int64_t>& members);

    /**
     *  整理正文里的 @ 片段（docs/帖子群方案.md 14.4）：丢弃 uid 小于 0、长度不是正数、越出正文、首字节不是 '@'、
     *  uid 不在 body.mentions 里的段，以及与前一段重叠的段；其余按 off 升序排列，最多保留 kThreadMaxMentionSpans 段。
     *  uid 为 0 的段是「@所有人」，只在 body.mentionall 为真时保留（§15）。
     *  须在 filterMentions 之后调用，使片段与最终的 mentions 一致。
     */
    static void sanitizeMentionSpans(ThreadBody& body);

    //---- 游标 ----

    //最新动态排序的游标：「a<最新动态时间>_<帖子 ID>」
    static std::string makeActivityCursor(int64_t activeTime, int64_t postid);
    static bool parseActivityCursor(const std::string& cursor, int64_t& activeTime, int64_t& postid);
    //最新发布排序的游标：「p<帖子 ID>」
    static std::string makePublishCursor(int64_t postid);
    static bool parsePublishCursor(const std::string& cursor, int64_t& postid);

    //---- 内容 JSON（落库） ----

    //ThreadBody → 落库 JSON
    static std::string bodyToJson(const ThreadBody& body);
    //落库 JSON → ThreadBody；解析失败返回 false（body 置空）
    static bool bodyFromJson(const std::string& json, ThreadBody& body);

    //---- 应答与推送 ----

    //只带 code 与 msg 的应答
    static std::string buildSimpleResponse(int code, const std::string& msg);
    //2701 应答：posts 为摘要形态（正文截取、最多 3 张图）
    static std::string buildListPostsResponse(int code, const std::string& msg, const std::vector<ThreadPost>& posts,
                                              const std::string& nextCursor, bool hasMore);
    //2702 / 2703 / 2704 / 2706 的应答：post 为 NULL 时只带 code 与 msg；full 为 true 时带完整正文、全部图片与补充
    static std::string buildPostResponse(int code, const std::string& msg, const ThreadPost* post, bool full);
    //2707 / 2708 应答
    static std::string buildCommentsResponse(int code, const std::string& msg, int64_t ownerId,
                                             const std::vector<ThreadComment>& comments, int64_t nextCursor, bool hasMore);
    //2709 应答
    static std::string buildCommentResponse(int code, const std::string& msg, const ThreadComment* comment);
    //2711 应答：目标最新的互动汇总
    static std::string buildReactResponse(int code, const std::string& msg, int targettype, int64_t targetid,
                                          const std::vector<ThreadReaction>& reactions);
    //2712 应答
    static std::string buildReactorsResponse(int code, const std::string& msg, int targettype, int64_t targetid,
                                             int reaction, const std::vector<int64_t>& users, int total);
    //2714 应答
    static std::string buildAddendumResponse(int code, const std::string& msg, const ThreadAddendum* addendum);
    //带一个 ID 字段的应答（2705 的 postid、2710 的 commentid、2715 的 addid）
    static std::string buildIdResponse(int code, const std::string& msg, const char* key, int64_t id);

    /**
     *  2720 推送。
     *  @param groupid    帖子群
     *  @param changetype 变化类型，取值见 thread_change_type
     *  @param operatorId 动作发起人
     *  @param post       相关帖子（新帖、修改、置顶时带摘要形态的帖子）；可为 NULL
     *  @param postid     帖子 ID（post 为 NULL 时用它）
     *  @param comment    相关留言或回复（新留言时带全文）；可为 NULL
     *  @param commentid  留言 ID（comment 为 NULL 时用它）
     *  @param addendum   相关补充（新补充时带全文）；可为 NULL
     *  @param addid      补充 ID（addendum 为 NULL 时用它）
     *  @param commentCount 帖子当前的留言与回复总数；小于 0 时不带
     *  @param activeTime 帖子当前的最新动态时间；0 时不带
     *  @param react      互动变化：目标类型、目标 ID、互动编号、加上还是取消与最新汇总；targettype 为 0 时不带
     */
    struct ReactChange
    {
        int                         targettype;   //目标类型；0 表示本次推送不是互动变化
        int64_t                     targetid;     //目标 ID
        int                         reaction;     //互动编号
        bool                        on;           //加上还是取消
        std::vector<ThreadReaction> summary;      //最新汇总（mine 一律为 false，看的人按 operator 自行判断）

        ReactChange()
            : targettype(0)
            , targetid(0)
            , reaction(0)
            , on(false)
        {
        }
    };
    static std::string buildChangedNotify(int64_t groupid, int changetype, int64_t operatorId, const ThreadPost* post,
                                          int64_t postid, const ThreadComment* comment, int64_t commentid,
                                          const ThreadAddendum* addendum, int64_t addid, int commentCount,
                                          int64_t activeTime, const ReactChange* react);

    //---- 卡片 ----

    //帖子卡片（msgtype 25）的 content：postid、title、digest、imagecount、mentions（被 @ 的人，字符串数组；
    //客户端据此判断这张卡片有没有 @ 到自己，普通消息的 content.at 带文字位置，卡片没有正文可对应，故另用此字段）、
    //images（帖子的全部图片，最多 9 张，消息记录据此显示缩略图，docs/帖子群方案.md 14.5；没有图片时不写该字段）、
    //mentionall（帖子 @ 了所有人时为 true，§15；没有时不写该字段）
    static std::string buildPostCardContent(const ThreadPost& post);
    //把帖子卡片 content 改写为已删除形态：去掉 title、digest、mentions、mentionall 与 images，带 deleted；
    //不是 JSON 对象时返回 false
    static bool buildDeletedPostCardContent(const std::string& content, std::string& out);
    //帖子动态卡片（msgtype 26）的 content；mentions 为留言、回复或补充里 @ 的人，images 为补充的图片（两者为空时都不写），
    //mentionAll 为真时写 "mentionall": true（留言、回复或补充 @ 了所有人，§15），为假时不写
    static std::string buildReplyCardContent(int64_t postid, int64_t commentid, int64_t addid,
                                             const std::string& postTitle, const std::string& digest, int kind,
                                             const std::vector<int64_t>& mentions = std::vector<int64_t>(),
                                             const std::vector<ThreadImage>& images = std::vector<ThreadImage>(),
                                             bool mentionAll = false);
    /**
     *  把动态卡片 content 改写为已删除形态（docs/帖子群方案.md 14.6）：保留 postid、commentid、addid、kind，
     *  去掉 posttitle、digest、mentions、mentionall 与 images，带 deleted。不是 JSON 对象时返回 false。
     */
    static bool buildDeletedReplyCardContent(const std::string& content, std::string& out);
    //卡片 content 是否已是已删除形态（带 deleted 且为真）；不是 JSON 对象时返回 false
    static bool isDeletedCardContent(const std::string& content);

    /**
     *  2716 的筛选：一条消息是不是属于 groupid 这个群、且已改写为已删除形态的帖子卡片或动态卡片。
     *  @param ctype   消息的会话类型（只认 conversation_group）
     *  @param to      消息的接收方（群消息为群号）
     *  @param msgtype 消息类型（只认 25、26）
     *  @param content 消息的 content（不带落库外壳）
     *  @param groupid 请求核对的帖子群
     */
    static bool isDeletedCardOfGroup(int ctype, int64_t to, int msgtype, const std::string& content, int64_t groupid);

    //2716 应答：{code, msg, groupid, cards: [{msgid, msgtype, content}]}，content 为 JSON 对象
    static std::string buildCheckCardsResponse(int code, const std::string& msg, int64_t groupid,
                                               const std::vector<ThreadCardState>& cards);
    //2721 推送：{groupid, cards: [{msgid, msgtype, content}]}
    static std::string buildCardsNotify(int64_t groupid, const std::vector<ThreadCardState>& cards);
    //卡片的 clientmsgid：帖子卡片「threadpost-<postid>」，留言与回复「threadcomment-<commentid>」，补充「threadadd-<addid>」
    static std::string postCardClientMsgId(int64_t postid);
    static std::string commentCardClientMsgId(int64_t commentid);
    static std::string addendumCardClientMsgId(int64_t addid);

    //帖子的标题；没有标题时取正文摘要（卡片与提醒的「帖子标题」都用它）
    static std::string displayTitle(const ThreadPost& post);
};
