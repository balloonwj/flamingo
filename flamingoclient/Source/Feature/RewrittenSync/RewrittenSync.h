/**
 *  消息改写离线同步的客户端逻辑（方案见 docs/消息改写离线同步方案.md）。
 *
 *  服务端改写一条已经发出的消息（群公告卡片被删、通话结束、屏幕共享改标题、帖子被编辑或删除）或撤回一条消息时，
 *  给它分配一个全局递增的改写序号。本端在本地库里记一个水位（已同步到的最大序号），登录后用
 *  cmd_sync_rewritten_messages（1526）逐页取回离线期间被改写过的消息，只覆盖本地库里已有的行。
 *
 *  两部分：
 *    · ApplyRewrittenPage：把一页应答应用到本地库，整页成功才推进水位（第 2b 步）；
 *    · CRewrittenSyncController：一轮同步的控制（登录、重连后发第一页，有更多时续拉，刷新受影响且开着的会话），
 *      经 IRewrittenSyncHost 发请求、查询与刷新界面，不依赖主面板，便于单测（第 2c 步）。
 *  都只在界面线程上使用。
 *
 *  典型用法（主面板）：
 *      m_rewrittenSync.Start(m_msgStore);                        //会话列表到达之后，每次登录一次
 *      m_rewrittenSync.OnPage(m_msgStore, *result, SelfUserId()); //收到 FMG_MSG_V2_SYNC_REWRITTEN
 *      m_rewrittenSync.Reset();                                   //登出
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace net { struct SyncRewrittenResult; }
namespace balloonhistory { class CV2MsgStore; }

namespace rewrittensync {

//本地库同步进度表 t_sync_meta 里存水位的键
extern const char* const kWatermarkKey;

//一页请求的条数，取服务端的上限（REWRITTEN_SYNC_LIMIT_MAX）
const int kPageLimit = 200;

//一个会话（本人视角）及本页里它被改写的消息
struct RewrittenConversation
{
    int     m_ctype;   //会话类型：1 单聊 2 群聊
    int64_t m_cid;     //会话 id：单聊为对方 userid，群聊为群 id
    std::vector<int64_t> m_msgids;   //本页覆盖过的、属于这个会话的消息 msgid，按出现先后

    RewrittenConversation()
        : m_ctype(0)
        , m_cid(0)
    {
    }
};

//把一页应答应用到本地库的结果
struct RewrittenPageOutcome
{
    int64_t m_watermark;   //应用之后的水位（失败时为应用之前的值）
    size_t  m_applied;     //覆盖了本地库里几行
    size_t  m_skipped;     //本地库里没有、因而跳过的条数
    bool    m_jumped;      //首次同步且本地库为空：没有逐条处理，水位直接设成服务端当前的最大序号
    //本页覆盖过的行所属的会话，去重，按首次出现的先后排列；跳过的条目不计入
    std::vector<RewrittenConversation> m_affected;

    RewrittenPageOutcome()
        : m_watermark(0)
        , m_applied(0)
        , m_skipped(0)
        , m_jumped(false)
    {
    }
};

/**
 *  读本地库里的水位。没有这一项、或读失败时按 0 处理（从头同步，结果只是多拉几页，不会出错）。
 *  @param store 本账号的本地库（已打开）
 *  @return 水位
 */
int64_t ReadWatermark(balloonhistory::CV2MsgStore& store);

/**
 *  把一页 cmd_sync_rewritten_messages 应答应用到本地库。
 *  逐条查本地库：已有这一行的整行覆盖（content、raw、search_text、recalled 等都换成新的，置顶与已听状态保留，
 *  与 cmd_sync_messages 落库同一条 UpsertBatch），没有的跳过、不插入。整页写入成功之后才把水位推进到应答的 rev
 *  （只增不减：比库里现有的水位小时不改）。任何一步失败都返回 false，水位保持不动，下次登录从原处重来。
 *  firstSyncOnEmptyStore 为 true（本轮从水位 0 开始、且开始时本地库为空）时不逐条处理，直接把水位设成应答的 currev。
 *  @param store                 本账号的本地库（已打开）
 *  @param page                  一页应答（m_code 须为 0）
 *  @param selfUserId            本人 userid，按它决定语音消息的已听初值（RecordFromNotify 的四参数版本）
 *  @param firstSyncOnEmptyStore 是否走首次同步的捷径，见上
 *  @param out                   [出参] 结果
 *  @return 成功返回 true
 */
bool ApplyRewrittenPage(balloonhistory::CV2MsgStore& store, const net::SyncRewrittenResult& page, int64_t selfUserId,
                        bool firstSyncOnEmptyStore, RewrittenPageOutcome& out);

/**
 *  按改写序号同步时，控制器向宿主要的几样服务。主面板实现它；单测用记录调用的假宿主。
 */
class IRewrittenSyncHost
{
public:
    virtual ~IRewrittenSyncHost()
    {
    }

    /**
     *  发一页请求（CIUSocket::RequestSyncRewritten）。
     *  @param since 已同步到的序号
     *  @param limit 本页最多几条
     */
    virtual void SendRewrittenRequest(int64_t since, int limit) = 0;

    /**
     *  这个会话当前有没有开着的窗口（聊天窗、消息记录窗、帖子群窗口等显示本地库内容的窗口）。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     *  @return 开着返回 true
     */
    virtual bool IsConversationShown(int ctype, int64_t cid) = 0;

    /**
     *  让这个会话开着的窗口按本地库重新显示被改写的消息。消息记录窗按会话重查本地库；聊天窗只在 msgids
     *  里有消息正显示在列表里时才重新加载（否则这些消息不在视图里，不必打乱用户的滚动位置）。
     *  @param ctype  会话类型
     *  @param cid    会话 id
     *  @param msgids 本页里这个会话被改写的消息 msgid（非空）
     */
    virtual void RefreshConversation(int ctype, int64_t cid, const std::vector<int64_t>& msgids) = 0;
};

/**
 *  一轮按改写序号同步的控制器。Start 读水位发第一页；OnPage 应用一页、刷新受影响且开着的会话，应答带 more 时
 *  以新水位续拉下一页，否则本轮结束。应用失败或服务端回错误码时本轮结束，水位不动，下次登录从原处重来。
 *  断线重连后再调 Start 会重新开始一轮（上一轮在途的应答不会再到达；万一到达，按序号覆盖同样的内容，水位只增不减）。
 */
class CRewrittenSyncController
{
public:
    /**
     *  @param host 宿主（借用，不持有），生命周期须长于本对象
     */
    explicit CRewrittenSyncController(IRewrittenSyncHost* host);

    /**
     *  开始一轮：读本地库的水位，记下此刻本地库是否为空，发第一页请求。已有一轮在进行时同样重新开始。
     *  @param store 本账号的本地库（已打开）；没打开时不发请求
     */
    void Start(balloonhistory::CV2MsgStore& store);

    /**
     *  处理一页应答。没有进行中的一轮时忽略。
     *  @param store      本账号的本地库（已打开）
     *  @param page       应答
     *  @param selfUserId 本人 userid
     */
    void OnPage(balloonhistory::CV2MsgStore& store, const net::SyncRewrittenResult& page, int64_t selfUserId);

    //结束当前一轮且不再处理它的应答（登出时调用）
    void Reset();

    //是否有一轮在进行（已发出请求、还没收到 more 为 false 的应答）
    bool IsRunning() const;

private:
    IRewrittenSyncHost* m_host;          //宿主（借用）
    bool                m_running;       //是否有一轮在进行
    int64_t             m_since;         //本轮最近一次请求带的 since
    bool                m_firstOnEmpty;  //本轮从水位 0 开始、且开始时本地库为空：第一页走首次同步的捷径
};

} // namespace rewrittensync
