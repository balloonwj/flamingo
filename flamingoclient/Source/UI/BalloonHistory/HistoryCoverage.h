/**
 *  消息记录窗的"本地连续覆盖区间"纯逻辑，HistoryCoverage.h
 *
 *  背景：本地库 msgv2.db 里的消息来源很杂 —— 打开消息记录窗时回填的最新 100 条、聊天窗往上
 *  翻页拉到的、实时推送的、Pin 列表带回来的任意久远的消息。所以本地库在时间上可能有断层，
 *  不能用"本地最早一条"当作向服务端继续往前拉的锚点，否则断层那一段永远补不上。
 *
 *  做法：每个会话记一段 [lo, hi]，含义是"服务端上 msgid 落在这段里的消息，本地一条不缺"，
 *  外加一个"已经拉到服务端最早一条"的标记。每页同步应答到达时按本文件的规则合并区间，并在
 *  本页覆盖的范围内对账 —— 本地有而服务端没返回的，就是在别处被删掉的，本地同步删除。
 *  消息记录窗只显示区间内的消息，翻到区间最早处时再以区间下界为锚点向服务端往前拉。
 *  方案见 docs/清理聊天记录方案.md §7.2、§7.3、§9.2。
 *
 *  本文件只做计算，不碰数据库与网络，全部由单元测试覆盖（Tests/HistoryCoverageTests.cpp，
 *  标签 [coverage]）。区间的读写在 CV2MsgStore，接入在 CBalloonMainPanel::OnSyncMessagesArrived。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonhistory {

/**
 *  某会话本地历史的连续覆盖区间。
 */
struct CoverRange
{
    bool    m_valid;          //是否已建立。刚升级、刚登录（登录后会清空）或从没回填过时为 false
    int64_t m_lo;             //下界 msgid（含）；已拉到最早时为 0
    int64_t m_hi;             //上界 msgid（含）
    bool    m_reachedOldest;  //是否已经拉到服务端上本人可见的最早一条

    CoverRange()
        : m_valid(false)
        , m_lo(0)
        , m_hi(0)
        , m_reachedOldest(false)
    {
    }
};

/**
 *  一页同步应答（cmd_sync_messages 翻历史模式）里与覆盖区间有关的信息。
 */
struct SyncPageInfo
{
    int64_t              m_beforeMsgid;  //请求锚点：拉取此 msgid 之前的消息；0 表示从最新开始
    bool                 m_hasMore;      //锚点方向上是否还有更早的消息
    std::vector<int64_t> m_msgids;       //本页各条消息的 msgid，顺序不限

    SyncPageInfo()
        : m_beforeMsgid(0)
        , m_hasMore(false)
    {
    }
};

/**
 *  一页同步应答覆盖的 msgid 范围。
 */
struct PageSpan
{
    bool    m_valid;          //本页是否能确定一个覆盖范围
    int64_t m_lo;             //范围下界（含）；本页已到最早时为 0
    int64_t m_hi;             //范围上界（含）
    bool    m_reachesOldest;  //本页是否已到服务端最早一条

    PageSpan()
        : m_valid(false)
        , m_lo(0)
        , m_hi(0)
        , m_reachesOldest(false)
    {
    }
};

/**
 *  算出一页同步应答覆盖的 msgid 范围：服务端上落在这个范围里的消息，本页已经全部给出。
 *
 *    - 从最新开始拉（锚点 0）、有消息：[本页最早, 本页最新]；
 *    - 往前翻（锚点 B）、有消息：[本页最早, B-1]；
 *    - 往前翻、一条都没返回：[0, B-1]，已到最早；
 *    - 以上各种情形，服务端表示没有更多时下界改为 0，并标记已到最早；
 *    - 从最新开始拉却一条都没返回：无法确定范围（m_valid 为 false）。此时本页既不建立区间，
 *      也不对账 —— 对账期间可能刚好有新消息到达，没有上界就会误删它。
 *
 *  @param page 一页同步应答的信息
 *  @return 覆盖范围；纯函数，无副作用
 */
PageSpan ComputePageSpan(const SyncPageInfo& page);

/**
 *  把一页同步应答并入原有的覆盖区间。
 *
 *    - 从最新开始拉的一页：与原区间接得上（本页范围下界不高于原区间上界 + 1）时合并，
 *      下界取小、上界取大；接不上（中间可能有断层）或原来没有区间时，以本页范围重新起算。
 *    - 往前翻的一页：原来没有区间时不建立（只有从最新开始拉的一页才能确定"从最新往下"）；
 *      接得上（本页范围与原区间重叠或首尾相接）时扩展，接不上时原区间不变。
 *    - "已到最早"一旦成立就随合并保留下来。
 *
 *  @param cur  原有的覆盖区间（可以未建立）
 *  @param page 一页同步应答的信息
 *  @return 合并后的覆盖区间；纯函数，无副作用
 */
CoverRange MergeCoverage(const CoverRange& cur, const SyncPageInfo& page);

/**
 *  对账：算出本页覆盖范围内、本地有而本页没有的 msgid —— 它们已在别处被删除（或被清理），
 *  本地应同步删除。服务端的同步结果包含已撤回的消息，所以撤回的消息不会被列进来。
 *
 *  @param page        一页同步应答的信息
 *  @param localMsgids 本地库里该会话的 msgid（可以只传本页范围内的，也可以多传，范围外的会被忽略）
 *  @return 应从本地删除的 msgid；本页无法确定范围时为空。纯函数，无副作用
 */
std::vector<int64_t> ComputeReconcileDeletions(const SyncPageInfo& page,
                                               const std::vector<int64_t>& localMsgids);

/**
 *  消息记录窗按覆盖区间计算的显示下界。
 *
 *  没有区间时（刚升级、刚登录、或断网时回填到不了）不限制，照旧显示全部本地记录；回填到达、
 *  区间建立之后再收窄到区间内。回填到达前可能短暂显示出断层以下的旧记录、或别处已删而本机
 *  还没对账的记录，回填一到即自动刷新纠正；断网时则与改动前一样能翻本地记录。
 *  （2026-09-30 与用户确认此口径，原先的"不显示、等回填"会让断网时窗口一直是空的。）
 *
 *  @param cover            该会话的覆盖区间
 *  @param isPinnedCategory 当前是否在 Pin 分类（Pin 列表是服务端单独给的全集，不受区间限制）
 *  @return 显示下界 msgid：只显示 msgid 不小于它的消息；0 表示不限制
 */
int64_t CoverageMinVisibleMsgid(const CoverRange& cover, bool isPinnedCategory);

/**
 *  消息记录窗翻到当前最后一页时，是否需要以区间下界为锚点向服务端拉更早的消息。
 *
 *  @param cover                  该会话的覆盖区间
 *  @param isLastPage             当前是否停在最后一页
 *  @param isPinnedCategory       当前是否在 Pin 分类
 *  @param hasTimeLowerBound      当前时间筛选是否有下界（如"近 7 天"、自定义起点）
 *  @param timeLowerBoundMs       时间筛选的下界，Unix毫秒；hasTimeLowerBound 为 false 时无意义
 *  @param oldestCoveredTimeMs    覆盖区间内最早一条消息的时间，Unix毫秒
 *  @return 需要拉取返回 true。以下情形不拉：区间未建立、已到最早、不在最后一页、Pin 分类、
 *          时间筛选的下界不早于区间内最早一条的时间（区间已经盖住了筛选的整个时间段）
 */
bool ShouldFetchOlder(const CoverRange& cover, bool isLastPage, bool isPinnedCategory,
                      bool hasTimeLowerBound, int64_t timeLowerBoundMs,
                      int64_t oldestCoveredTimeMs);

/**
 *  当前筛选条件下，服务端是否还有更早的记录没有加载到本地。
 *
 *  条件与 ShouldFetchOlder 相同，只是不看"是否停在最后一页"：ShouldFetchOlder 回答"现在要不要
 *  去拉"，本函数回答"还有没有没拉的"。消息记录窗据它决定总数写成"共 N 条"还是"已加载 N 条"
 *  （见 BuildHistorySubtitleText / BuildHistoryFooterText）。
 *
 *  @param cover                  该会话的覆盖区间
 *  @param isPinnedCategory       当前是否在 Pin 分类
 *  @param hasTimeLowerBound      当前时间筛选是否有下界
 *  @param timeLowerBoundMs       时间筛选的下界，Unix毫秒；hasTimeLowerBound 为 false 时无意义
 *  @param oldestCoveredTimeMs    覆盖区间内最早一条消息的时间，Unix毫秒
 *  @return 还有未加载的更早记录时返回 true。以下情形返回 false：区间未建立（离线、调试入口，
 *          此时无从得知）、已拉到最早、Pin 分类、时间筛选的下界不早于区间内最早一条的时间
 */
bool HasUnloadedOlder(const CoverRange& cover, bool isPinnedCategory,
                      bool hasTimeLowerBound, int64_t timeLowerBoundMs,
                      int64_t oldestCoveredTimeMs);

/**
 *  消息记录窗顶部副标题的文字。
 *    total：           当前筛选条件下本地的条数。
 *    hasUnloadedOlder：是否还有更早的记录没有加载（HasUnloadedOlder 的结果）。
 *  @return 有未加载的记录时为"已加载 N 条消息，翻到最后一页会继续加载更早的"，否则为"共 N 条消息"。
 */
std::wstring BuildHistorySubtitleText(int64_t total, bool hasUnloadedOlder);

/**
 *  消息记录窗底部信息栏的文字。
 *    total：           当前筛选条件下本地的条数。
 *    currentPage：     当前页号（从 1 起）。
 *    pageCount：       总页数。
 *    hasUnloadedOlder：是否还有更早的记录没有加载。
 *    fetchingOlder：   是否正在向服务端拉取更早的记录。
 *  @return 有未加载的记录时以"已加载 N 条"开头，否则以"共 N 条"开头，后接" · 第 x/y 页"；
 *          正在拉取时末尾再加" · 正在加载更早的消息…"。
 */
std::wstring BuildHistoryFooterText(int64_t total, int currentPage, int pageCount,
                                    bool hasUnloadedOlder, bool fetchingOlder);

//用户翻到末页一次，最多连着向服务端往前拉几轮（每轮 100 条）。有类型或关键字筛选时，一轮
//拉回来的消息里可能一条都不符合，要接着拉；但不能无限拉下去占着网络，故设上限，
//用户再翻一次末页会再给 5 轮。
const int kMaxFetchOlderRounds = 5;

/**
 *  往前拉回一轮之后，是否还要接着拉下一轮。
 *
 *  筛选结果已经多出至少一整页、或已拉到服务端最早一条、或已到轮数上限时停；否则接着拉。
 *  没有筛选时一轮就会多出远超一页的消息，自然只拉一轮。
 *
 *  @param roundsDone     本次翻到末页以来已经拉了几轮（含刚完成的这一轮）
 *  @param maxRounds      轮数上限，一般传 kMaxFetchOlderRounds
 *  @param countBefore    开始拉取之前，当前筛选条件下的总条数
 *  @param countNow       刚完成的这一轮落库之后，当前筛选条件下的总条数
 *  @param pageSize       消息记录窗每页条数
 *  @param reachedOldest  覆盖区间是否已拉到服务端最早一条
 *  @return 需要接着拉返回 true；纯函数，无副作用
 */
bool ShouldContinueFetchOlder(int roundsDone, int maxRounds, int64_t countBefore,
                              int64_t countNow, int pageSize, bool reachedOldest);

} // namespace balloonhistory
