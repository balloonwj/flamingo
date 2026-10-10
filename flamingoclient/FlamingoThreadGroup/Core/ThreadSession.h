/**
 *  一个帖子群的会话控制器 CThreadSession（docs/帖子群方案.md 第 4、6 节）：帖子群窗口背后的全部逻辑，不含界面。
 *
 *  职责：
 *    - 发请求：经 IThreadSessionSender 发出 2701~2715，按应答的 seq 记下每个请求的用途；
 *    - 收应答：按 seq 找回用途，更新帖子列表模型（CThreadFeedModel）与帖子详情模型（CThreadDetailModel），
 *      再经 IThreadSessionView 通知界面重画或提示错误；
 *    - 收推送：2720 按变化类型合并进两个模型；
 *    - 互动：点了立即更新（乐观更新），失败时恢复点之前的汇总；
 *    - 断线重连：未完成的请求一律按失败通知界面，重新登记「正在查看」并重新加载。
 *
 *  去重的约定（避免本人操作的应答与随后的推送重复计数）：帖子级变化（新帖、删除、置顶、补充）一律推给全体在线成员，
 *  列表的计数只按推送更新；留言只推给正在查看的连接（本人打开窗口时已登记），推送带的留言总数是绝对值，
 *  本人留言成功时先本地加一、推送到达后以推送为准。详情里的留言、补充按 ID 去重。
 *
 *  单元测试直接构造本类，用假的 Sender / View 驱动（Tests/ThreadSessionTests.cpp）。
 *
 *  典型用法（帖子群窗口）：
 *      CThreadSession session(groupId, &sender);
 *      session.SetView(this);
 *      session.Start();                  // 登记「正在查看」并拉第一页
 *      session.SelectPost(postId);
 *      ...模块收到应答：session.OnResponse(cmd, seq, body);
 *      ...模块收到推送：session.OnChange(change);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "ThreadTypes.h"
#include "ThreadFeedModel.h"
#include "ThreadDetailModel.h"

namespace threadgroup {

/**
 *  会话发请求的出口（模块实现：经宿主发出，并把 seq 登记到本群名下）。
 */
class IThreadSessionSender
{
public:
    /**
     *  发出一条 2700 段命令。
     *  @param groupId 发起请求的帖子群（模块据此把应答交回本会话）
     *  @return 帧 seq（大于 0）；没有发出时返回 0
     */
    virtual uint32_t SendThreadCommand(int64_t groupId, uint16_t cmd, const std::string& body) = 0;

    //本人 userid；未登录时为 0
    virtual int64_t MyUserId() = 0;

protected:
    ~IThreadSessionSender()
    {
    }
};

/**
 *  会话通知界面的接口（帖子群窗口实现）。全部在界面线程上调用。
 */
class IThreadSessionView
{
public:
    //帖子列表变了（翻页、推送、本人操作），需要重画左栏
    virtual void OnFeedChanged() = 0;

    //帖子详情变了（内容、补充、留言、互动），需要重画右栏
    virtual void OnDetailChanged() = 0;

    /**
     *  一个由本人发起的操作完成了。
     *  @param cmd  命令号，取值见 ThreadCmd
     *  @param code 结果：0 成功；thread_error_timeout 表示没有发出去或断线；其余为服务端错误码
     *  @param id   相关的 ID：发帖、修改、删除、置顶为帖子 ID，留言为新留言 ID，补充为补充 ID；失败时可能为 0
     */
    virtual void OnActionDone(int cmd, int code, int64_t id) = 0;

    /**
     *  互动名单到了（2712 的应答）。
     *  @param users 用户 ID（最多 kReactorListLimit 个）
     *  @param total 加了这种互动的总人数
     */
    virtual void OnReactorsLoaded(int targetType, int64_t targetId, int reaction, const std::vector<int64_t>& users,
                                  int total) = 0;

protected:
    ~IThreadSessionView()
    {
    }
};

/**
 *  CThreadSession —— 一个帖子群的会话控制器。只在界面线程上使用。
 */
class CThreadSession
{
public:
    /**
     *  @param groupId 帖子群 ID
     *  @param sender  发请求的出口（借用，须比本对象存活久）
     */
    CThreadSession(int64_t groupId, IThreadSessionSender* sender);

    //登记接收通知的界面（借用）；传 NULL 解除
    void SetView(IThreadSessionView* view);

    //帖子群 ID
    int64_t GroupId() const;

    /**
     *  开始：登记「正在查看」并拉第一页帖子；已选中帖子时重新拉它的内容与留言（断线重连后调用同一函数）。
     */
    void Start();

    /**
     *  结束：撤销「正在查看」，丢弃未完成的请求（不再通知界面）。窗口关闭时调用。
     */
    void Stop();

    /**
     *  断线：未完成的本人操作一律按 thread_error_timeout 通知界面，正在加载的标记清掉。
     */
    void OnDisconnected();

    //---- 帖子列表 ----

    //切换排序：清空列表并重新拉第一页
    void SetSort(int sort);

    //拉第一页（刷新）
    bool LoadFirstPage();

    //拉下一页；没有下一页或正在加载时返回 false
    bool LoadMore();

    //---- 帖子详情 ----

    /**
     *  选中一条帖子：先用列表里的摘要显示，同时拉完整内容与第一页留言。
     *  @param postId 帖子 ID；0 表示取消选中
     */
    void SelectPost(int64_t postId);

    //重新拉当前帖子的完整内容（推送说正文改了而摘要被截取时）
    bool ReloadPost();

    //拉下一页留言
    bool LoadMoreComments();

    //拉某条留言下的更多回复
    bool LoadMoreReplies(int64_t commentId);

    //---- 本人发起的操作（结果经 IThreadSessionView::OnActionDone 回来）----

    /**
     *  发帖。
     *  @param title    标题
     *  @param wireText 线上正文（表情已编码）
     *  @param images   已上传的图片
     *  @param mentions @ 的人
     *  @param at       正文里 @ 片段的位置（发送前按最终正文重扫出来的，docs/帖子群方案.md 14.4）；可为空
     *  @return 已发出返回 true
     */
    bool PublishPost(const std::wstring& title, const std::wstring& wireText, const std::vector<ThreadImage>& images,
                     const std::vector<int64_t>& mentions,
                     const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());

    //修改帖子（作者本人）；at 同 PublishPost
    bool EditPost(int64_t postId, const std::wstring& title, const std::wstring& wireText,
                  const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                  const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());

    //删除帖子
    bool DeletePost(int64_t postId);

    //置顶或取消置顶
    bool PinPost(int64_t postId, bool pin);

    /**
     *  留言或回复。
     *  @param postId   帖子
     *  @param parentId 0 为留言，否则为被回复的那条留言或回复
     *  @param at       正文里 @ 片段的位置；可为空
     */
    bool AddComment(int64_t postId, int64_t parentId, const std::wstring& wireText, const std::vector<int64_t>& mentions,
                    const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());

    //删除留言或回复
    bool DeleteComment(int64_t commentId);

    /**
     *  加上或取消一种互动：立即更新本地汇总，失败时恢复。
     *  @param targetType 目标类型，取值见 ThreadTargetType
     */
    bool React(int targetType, int64_t targetId, int reaction, bool on);

    //拉互动名单（结果经 OnReactorsLoaded）
    bool GetReactors(int targetType, int64_t targetId, int reaction);

    //追加补充（楼主本人）；at 同 PublishPost
    bool AddAddendum(int64_t postId, const std::wstring& wireText, const std::vector<ThreadImage>& images,
                     const std::vector<int64_t>& mentions,
                     const std::vector<ThreadAtSpan>& at = std::vector<ThreadAtSpan>());

    //删除一段补充
    bool DeleteAddendum(int64_t addId);

    //---- 收帧（模块调用）----

    /**
     *  交来一条应答。
     *  @return seq 是本会话发出的请求返回 true
     */
    bool OnResponse(uint16_t cmd, uint32_t seq, const std::string& body);

    //交来一条 2720 推送（已确认 groupid 是本群）
    void OnChange(const ThreadChange& change);

    //---- 模型（界面读取）----

    const CThreadFeedModel& Feed() const;
    const CThreadDetailModel& Detail() const;

    //未完成的请求数（测试用）
    size_t PendingCount() const;

private:
    //一个未完成的请求
    struct Pending
    {
        uint16_t                    m_cmd;        //命令号
        int64_t                     m_postId;     //相关帖子
        int64_t                     m_commentId;  //相关留言（拉回复、删留言）
        int64_t                     m_addId;      //相关补充（删补充）
        int                         m_targetType; //互动目标类型
        int64_t                     m_targetId;   //互动目标
        int                         m_reaction;   //互动编号
        bool                        m_firstPage;  //拉列表、拉留言时是否第一页
        int                         m_sort;       //拉列表时的排序（应答到达时排序已切换则丢弃）
        std::vector<ThreadReaction> m_beforeFeed;     //互动请求发出前列表里的汇总（失败时恢复）
        std::vector<ThreadReaction> m_beforeDetail;   //互动请求发出前详情里的汇总（失败时恢复）
        bool                        m_beforeInFeed;   //目标在列表里、m_beforeFeed 有效
        bool                        m_beforeInDetail; //目标在详情里、m_beforeDetail 有效

        Pending()
            : m_cmd(0)
            , m_postId(0)
            , m_commentId(0)
            , m_addId(0)
            , m_targetType(0)
            , m_targetId(0)
            , m_reaction(0)
            , m_firstPage(false)
            , m_sort(0)
            , m_beforeInFeed(false)
            , m_beforeInDetail(false)
        {
        }
    };

    //发出请求并登记；没有发出时返回 false（不登记）
    bool Send(uint16_t cmd, const std::string& body, const Pending& pending);

    //生成一个去重标识：前缀 + 群号 + 本机时刻 + 序号
    std::string NewClientId(const char* prefix);

    //拉当前帖子的完整内容与第一页留言
    void LoadSelectedPost();

    //本人操作失败或超时：恢复乐观更新并通知界面
    void FailPending(const Pending& p, int code);

    //各类应答的处理
    void HandleListPosts(const Pending& p, const std::string& body);
    void HandleGetPost(const Pending& p, const std::string& body);
    void HandlePostWrite(const Pending& p, const std::string& body);
    void HandleDeletePost(const Pending& p, const std::string& body);
    void HandleComments(const Pending& p, const std::string& body);
    void HandleAddComment(const Pending& p, const std::string& body);
    void HandleDeleteComment(const Pending& p, const std::string& body);
    void HandleReact(const Pending& p, const std::string& body);
    void HandleReactors(const Pending& p, const std::string& body);
    void HandleAddAddendum(const Pending& p, const std::string& body);
    void HandleDeleteAddendum(const Pending& p, const std::string& body);

    //通知界面（界面未登记时什么也不做）
    void NotifyFeed();
    void NotifyDetail();
    void NotifyAction(int cmd, int code, int64_t id);

private:
    int64_t                     m_groupId;       //帖子群 ID
    IThreadSessionSender*       m_sender;        //发请求的出口（借用）
    IThreadSessionView*         m_view;          //界面（借用）；可为 NULL
    CThreadFeedModel            m_feed;          //帖子列表
    CThreadDetailModel          m_detail;        //帖子详情
    std::map<uint32_t, Pending> m_pending;       //未完成的请求：seq → 用途
    uint32_t                    m_clientIdSeq;   //去重标识的序号
    bool                        m_started;       //Start 之后、Stop 之前
};

} // namespace threadgroup
