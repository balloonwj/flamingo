/**
 *  帖子群窗口右栏的帖子详情 CThreadDetailPane（docs/帖子群方案.md 4.6 右栏，样稿第 0 期）。
 *
 *  子类化 DuiScrollView（不改 balloonui），内容从上到下：
 *    - 作者行：头像、名字（楼主标记）、时间（修改过的标「已编辑」），右侧「编辑」「置顶 / 取消置顶」「删除」（按权限显示）；
 *    - 标题、正文（只读富文本，表情画成图片）、图片网格（每行三张）；
 *    - 互动标签（「赞 N」与各表情 + 人数，本人加过的高亮）；
 *    - 楼主的各段补充：左侧主色竖条，「补充 · 时间」标题行（能删时右侧有「删除」）、正文与图片；楼主本人在下面看到「＋ 追加补充」。
 *  留言区与留言框在第 2c-2、2c-3 步加入。
 *
 *  窗口把帖子整理成 DetailViewData（名字、头像、时间文字、权限、图片位图都已备好）交给 Rebuild，本类只按数据搭控件，
 *  不接触宿主。按钮的点击以 DUIN_CLICK 通知窗口（控件 id 见下面的常量，补充的「删除」按段序号区分）。
 *  正文、补充、留言与回复里点 @XXX（左键或右键）经 SetAtClickHandler 登记的回调告诉窗口（docs/帖子群方案.md §15.1 第 4 条）。
 *  多行文字的高度随宽度变化：Rebuild 与 Relayout 都对自己排两次版（富文本框第一趟拿不到宽度，见 DuiRichEdit 的自动增高说明）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../Host/ThreadWindowApi.h"   // ThreadFaceEntry
#include "ThreadPostCard.h"            // PostCardChip
#include "ThreadRichText.h"            // ThreadAtClickFn：点 @XXX 的回调

namespace threadgroup {

//---- 详情里按钮的控件 id（DUIN_CLICK 的 ctrlId） ----
const UINT kIdDetailEdit = 7301;          //「编辑」
const UINT kIdDetailPin = 7302;           //「置顶」或「取消置顶」
const UINT kIdDetailDelete = 7303;        //「删除」帖子
const UINT kIdDetailAddAddendum = 7304;   //「＋ 追加补充」
const UINT kIdDetailImageBase = 7400;     //帖子的第 i 张图片：7400 + i（i 小于 9）
const UINT kIdAddendumDeleteBase = 7500;  //第 i 段补充的「删除」：7500 + i（i 小于 20）
const UINT kIdLoadMoreComments = 7600;    //「加载更多留言」
const UINT kIdCommentActionBase = 10000;  //留言区里的按钮：10000 + 槽位号，槽位号经 ActionAt 查出动作与留言 ID
const UINT kIdCommentActionLast = 19999;  //留言区按钮的最后一个控件 id（槽位号最多 10000 个）

//留言区里一个按钮的动作
enum DetailCommentAction
{
    detail_action_none          = 0,   //不是留言区的按钮
    detail_action_reply         = 1,   //「回复」：回复这条留言或回复
    detail_action_delete        = 2,   //「删除」：删除这条留言或回复
    detail_action_more_replies  = 3,   //「查看更多回复」：拉这条留言下更多的回复
    detail_action_react         = 4,   //互动标签：切换本人对这个目标的这种互动（加上或取消）
    detail_action_add_face      = 5,   //「＋」：打开表情浮层，选一个表情回应这个目标
};

//留言区与互动按钮登记的一项（控件 id = kIdCommentActionBase + 下标）
struct DetailActionSlot
{
    int     m_action;      //DetailCommentAction
    int     m_targetType;  //目标类型（ThreadTargetType）：帖子或留言 / 回复
    int64_t m_targetId;    //目标 ID：帖子 ID，或留言 / 回复 ID
    int     m_reaction;    //互动编号（detail_action_react 时有效）

    DetailActionSlot()
        : m_action(0)
        , m_targetType(0)
        , m_targetId(0)
        , m_reaction(0)
    {
    }
};

//详情里的一条回复（挂在留言下面）
struct DetailReplyView
{
    int64_t                   m_commentId;   //回复 ID
    std::wstring              m_authorName;  //回复人
    bool                      m_authorIsOwner; //回复人是楼主（名字后面带「楼主」）
    std::wstring              m_replyToName; //被回复的人
    std::wstring              m_timeText;    //时间文字
    std::wstring              m_wireText;    //正文（线上格式）
    std::vector<ThreadAtSpan> m_at;          //正文里 @ 片段的位置（加背景色用）
    bool                      m_canDelete;   //本人能否删除
    std::vector<PostCardChip> m_chips;       //互动标签

    DetailReplyView()
        : m_commentId(0)
        , m_authorIsOwner(false)
        , m_canDelete(false)
    {
    }
};

//详情里的一条留言
struct DetailCommentView
{
    int64_t                      m_commentId;    //留言 ID
    bool                         m_deleted;      //已删除，只剩占位（下面还有回复）
    std::wstring                 m_authorName;   //留言人
    bool                         m_authorIsOwner;//留言人是楼主
    std::wstring                 m_avatarGlyph;  //留言人字形头像上的字
    COLORREF                     m_avatarColor;  //留言人字形头像的底色
    HBITMAP                      m_avatarBitmap; //留言人照片头像（借用）；为 NULL 时画字形
    std::wstring                 m_timeText;     //时间文字
    std::wstring                 m_wireText;     //正文（线上格式）
    std::vector<ThreadAtSpan>    m_at;           //正文里 @ 片段的位置（加背景色用）
    bool                         m_canDelete;    //本人能否删除
    std::vector<PostCardChip>    m_chips;        //互动标签
    std::vector<DetailReplyView> m_replies;      //已加载的回复，按时间排列
    int                          m_moreReplies;  //还有多少条回复没加载（「查看更多回复（N 条）」）

    DetailCommentView()
        : m_commentId(0)
        , m_deleted(false)
        , m_authorIsOwner(false)
        , m_avatarColor(0)
        , m_avatarBitmap(NULL)
        , m_canDelete(false)
        , m_moreReplies(0)
    {
    }
};

//详情里的一段补充
struct DetailAddendumView
{
    int64_t              m_addId;      //补充 ID
    std::wstring         m_timeText;   //时间文字
    std::wstring         m_wireText;   //正文（线上格式，表情为 /f["编号"]）
    std::vector<ThreadAtSpan> m_at;    //正文里 @ 片段的位置（加背景色用）
    std::vector<HBITMAP> m_images;     //图片（借用；还没下载的为 NULL，画占位底色）
    bool                 m_canDelete;  //本人能否删除这段

    DetailAddendumView()
        : m_addId(0)
        , m_canDelete(false)
    {
    }
};

//右栏要显示的全部内容（窗口按会话里的帖子详情与宿主准备好）
struct DetailViewData
{
    int64_t                   m_postId;         //帖子 ID
    bool                      m_deleted;        //帖子已被删除（只显示一行说明）
    std::wstring              m_authorName;     //作者显示名
    std::wstring              m_avatarGlyph;    //作者字形头像上的字
    COLORREF                  m_avatarColor;    //作者字形头像的底色
    HBITMAP                   m_avatarBitmap;   //作者照片头像（借用）；为 NULL 时画字形
    std::wstring              m_timeText;       //发帖时间文字
    bool                      m_edited;         //修改过（时间后面标「已编辑」）
    bool                      m_canEdit;        //显示「编辑」
    bool                      m_canPin;         //显示「置顶 / 取消置顶」
    bool                      m_pinned;         //已置顶（按钮写「取消置顶」）
    bool                      m_canDelete;      //显示「删除」
    std::wstring              m_title;          //标题；为空时没有标题行
    std::wstring              m_wireText;       //正文（线上格式）
    std::vector<ThreadAtSpan> m_at;             //正文里 @ 片段的位置（加背景色用）
    int64_t                   m_myUserId;       //本人：@ 到本人的片段用更醒目的配色
    std::vector<HBITMAP>      m_images;         //全部图片（借用；还没下载的为 NULL）
    std::vector<PostCardChip> m_chips;          //互动标签
    std::vector<DetailAddendumView> m_addenda;  //楼主的各段补充，按时间排列
    bool                      m_canAddAddendum; //显示「＋ 追加补充」
    int                       m_commentCount;   //留言与回复总数（留言区标题里的数字）
    std::vector<DetailCommentView> m_comments;  //已加载的留言，按时间排列
    bool                      m_commentsLoaded; //第一页留言已经到了（之前显示「正在加载…」）
    bool                      m_commentsLoading;//正在拉下一页留言
    bool                      m_hasMoreComments;//留言还有下一页（显示「加载更多留言」）

    DetailViewData()
        : m_postId(0)
        , m_deleted(false)
        , m_avatarColor(0)
        , m_avatarBitmap(NULL)
        , m_edited(false)
        , m_canEdit(false)
        , m_canPin(false)
        , m_pinned(false)
        , m_canDelete(false)
        , m_myUserId(0)
        , m_canAddAddendum(false)
        , m_commentCount(0)
        , m_commentsLoaded(false)
        , m_commentsLoading(false)
        , m_hasMoreComments(false)
    {
    }
};

/**
 *  CThreadDetailPane —— 右栏的帖子详情。只在界面线程上使用。
 */
class CThreadDetailPane : public balloonwjui::DuiScrollView
{
public:
    CThreadDetailPane();

    /**
     *  登记搭控件要用的字体与表情库。窗口创建后、第一次 Rebuild 之前调用。
     *  @param bodyFont  正文的字体（借用）
     *  @param boldFont  名字的加粗字体（借用）
     *  @param titleFont 标题的加大加粗字体（借用）
     *  @param faces     表情库（借用，须比本对象存活久）
     */
    void Setup(HFONT bodyFont, HFONT boldFont, HFONT titleFont, const std::vector<ThreadFaceEntry>* faces);

    /**
     *  登记点 @XXX 的回调：之后 Rebuild 建出的正文、补充、留言与回复的富文本都用它（标题没有 @，不登记）。
     *  @param fn   回调；为 NULL 时点 @ 不做任何事
     *  @param user 回调的 userdata（借用，须比本对象存活久）
     */
    void SetAtClickHandler(ThreadAtClickFn fn, void* user);

    /**
     *  按数据整体重建详情。换了帖子（postId 变了）时滚回顶部，同一条帖子刷新时保持滚动位置。
     *  @param data 要显示的内容
     */
    void Rebuild(const DetailViewData& data);

    /**
     *  宽度变了之后重新排版两次（富文本框的高度随宽度变化，第一趟量不准）。窗口改变大小后调用。
     */
    void Relayout();

    //当前显示的帖子；没有为 0
    int64_t PostId() const;

    /**
     *  留言区里一个按钮的动作。
     *  @param ctrlId    按钮的控件 id（kIdCommentActionBase ~ kIdCommentActionLast）
     *  @param commentId [出参] 动作针对的留言或回复 ID
     *  @return 动作，取值见 DetailCommentAction；不是留言区的按钮时为 detail_action_none
     */
    int ActionAt(UINT ctrlId, int64_t& commentId) const;

    /**
     *  留言区与互动按钮登记的完整内容。
     *  @param ctrlId 按钮的控件 id
     *  @param out    [出参] 登记的内容
     *  @return 是本面板登记过的按钮返回 true
     */
    bool SlotAt(UINT ctrlId, DetailActionSlot& out) const;

    /**
     *  滚动到某条留言或回复并使它完整可见（本人发的留言、回复成功后，docs/帖子群方案.md §15.1 第 5 条）：已完整可见时
     *  不动，规则见 ScrollToReveal。这条没有显示出来（回复所在的留言还有没展开的回复，新回复只计入条数）时改为
     *  滚动到 fallbackId 那一条（通常是被回复的那条）。
     *  @param commentId  要显示的留言或回复
     *  @param fallbackId commentId 没有显示时改用的那一条；为 0 时不改用
     *  @return 找到了要显示的条目返回 true（滚动位置可能不变）
     */
    bool RevealComment(int64_t commentId, int64_t fallbackId);

private:
    //内容变了之后对自己排两次版（UI 约定第 12 条；第二趟富文本框才量得到宽度）
    void LayoutTwice();

    //登记一个留言区按钮（目标为留言或回复），返回它的控件 id；槽位用完时返回 0（按钮不加）
    UINT RegisterAction(int action, int64_t commentId);

    //登记一个按钮（任意目标与互动编号），返回它的控件 id；槽位用完时返回 0
    UINT RegisterSlot(int action, int targetType, int64_t targetId, int reaction);

    /**
     *  建一行互动：各互动标签（可点击）与后面的「＋」。
     *  @param row        要加进去的那一行（借用）
     *  @param chips      互动标签
     *  @param targetType 目标类型（ThreadTargetType）
     *  @param targetId   目标 ID
     *  @param plusText   「＋」按钮的文字（帖子为「＋ 表情」，留言为「＋」）
     */
    void AddReactionRow(balloonwjui::DuiHBox* row, const std::vector<PostCardChip>& chips, int targetType, int64_t targetId,
                        const std::wstring& plusText);

    //建留言区：标题行、各条留言（含回复）、「加载更多留言」
    void BuildComments(balloonwjui::DuiVBox* content, const DetailViewData& data);

    //建一条留言（头像 | 名字行、正文、操作行、回复块）
    std::unique_ptr<balloonwjui::DuiHBox> BuildComment(const DetailCommentView& comment);

    //建一条回复（「某某 回复 某某：」加正文、操作行）
    std::unique_ptr<balloonwjui::DuiVBox> BuildReply(const DetailReplyView& reply);

    //建一行操作：时间（可为空）、「回复」、互动标签、空白、「删除」（能删时）
    std::unique_ptr<balloonwjui::DuiHBox> BuildActionRow(const std::wstring& timeText, int64_t commentId,
                                                         const std::vector<PostCardChip>& chips, bool canDelete);

private:
    HFONT                               m_bodyFont;   //正文字体（借用）
    HFONT                               m_boldFont;   //加粗字体（借用）
    HFONT                               m_titleFont;  //标题的加大加粗字体（借用）
    const std::vector<ThreadFaceEntry>* m_faces;      //表情库（借用）
    int64_t                             m_postId;     //当前显示的帖子
    int64_t                             m_viewerId;   //本人（每次 Rebuild 取自 DetailViewData）：@ 到本人的片段用更醒目的配色
    ThreadAtClickFn                     m_atClickFn;  //点 @XXX 的回调（SetAtClickHandler 登记）；为 NULL 时不处理
    void*                               m_atClickUser;//回调的 userdata（借用）
    std::vector<DetailActionSlot>       m_actions;    //留言区与互动按钮的登记表，下标为槽位号；每次 Rebuild 重填
    std::map<int64_t, balloonwjui::DuiControl*> m_commentRows; //留言或回复 ID → 它那一块控件（归控件树所有）；每次 Rebuild 重填
};

} // namespace threadgroup
