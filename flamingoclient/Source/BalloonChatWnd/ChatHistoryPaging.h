/**
 *  ChatHistoryPaging.h
 *
 *  聊天窗「向上翻看更早消息」的<u>纯逻辑</u>部分 —— 分页状态与「此刻该不该去拉
 *  下一页」的判定。不碰 HWND / balloonui / 网络，只吃几个整数，便于被 FlamingoUnit
 *  测试工程直接调用断言。
 *
 *  背景：聊天窗原本只拉「最近 N 条」的一次性快照，渲染前清空列表，没有任何翻页
 *  能力 —— 想看更早的消息只能去历史消息记录窗。服务端其实早就支持向前翻页
 *  （MessageManager.cpp 的 `AND f_msgid < beforemsgid ORDER BY f_msgid DESC
 *  LIMIT limit+1`，多取一条用来判断 hasmore），只是客户端一直把 before 传 0。
 *
 *  单聊窗与群聊窗各持有一份本状态，逻辑完全共用 —— 两边的历史渲染本来就是各写
 *  一份，判定再各写一份迟早跑偏。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

namespace balloonchatwnd {

// 首屏与向上翻页的每页条数。两者取同一个值：首屏小一点开窗更快，翻页同值则
// 用户往上滚的节奏是均匀的，不会「第一次翻很多、之后每次翻很少」。
const int kHistoryPageSize = 50;

// 触发加载的顶部阈值（像素）。滚动偏移小于它就算「滚到顶部附近」，开始拉下一页。
// 取值略大于一行消息的高度：等真的贴到 0 才加载，用户会先看到一片空白再看到内容
// 突然插进来；提前一行触发，多数情况下内容在用户滚到顶之前就已经就位。
const int kHistoryLoadTriggerY = 48;

/**
 *  一个会话的历史分页状态。窗口每打开一个会话持有一份，切换会话时整体重置。
 */
struct HistoryPagingState
{
    // 当前列表里<u>最早</u>那条消息的服务器 msgid，也就是下一页请求的 before 锚点。
    // 0 表示首屏还没到位 —— 此时不允许翻页，否则会拿 before=0 再拉一次最新的，
    // 结果是同一批消息被当成"更早的"插到头部，列表直接重影。
    long long m_oldestMsgid;

    // 服务端是否表示还有更早的消息（应答里的 hasmore）。为假即已到会话开头。
    bool m_hasMore;

    // 是否有翻页请求在途。应答与请求之间隔着一次网络往返，其间用户很可能还在
    // 顶部继续滚 —— 少了这个闸门会连发好几页，重复插入且顺序错乱。
    bool m_loading;

    HistoryPagingState()
        : m_oldestMsgid(0)
        , m_hasMore(false)
        , m_loading(false)
    {
    }
};

/**
 *  判定此刻该不该去拉更早的一页。
 *
 *  五个否决条件，任一成立就不拉：
 *    · 服务端已说没有更早的了；
 *    · 已经有请求在途；
 *    · 首屏还没到位（锚点为 0）；
 *    · 内容还不满一屏 —— 连滚动条都没有，谈不上"往上翻"；
 *    · 还没滚到顶部阈值内。
 *
 *    st：          当前分页状态。
 *    scrollY：     当前纵向滚动偏移（0 = 已在顶部）。
 *    viewHeight：  消息行可视区高度。
 *    contentHeight：全部消息行的总高。
 *  @return 该发请求返回 true。
 */
bool ShouldLoadOlderHistory(const HistoryPagingState& st,
                            int scrollY, int viewHeight, int contentHeight);

/**
 *  首屏历史到达后更新分页状态。
 *    st：           [出参] 待更新的状态。
 *    oldestMsgid：  本批里最早那条的 msgid；本批为空时传 0。
 *    hasMore：      应答里的 hasmore。
 */
void OnFirstPageLoaded(HistoryPagingState& st, long long oldestMsgid, bool hasMore);

/**
 *  翻页应答到达后更新分页状态。与首屏的区别在于：本批为空时<u>不能</u>把锚点清成 0
 *  （那会让下一次翻页退化成"再拉一次最新的"），锚点原样保留即可。
 *    st：          [出参] 待更新的状态。
 *    oldestMsgid： 本批里最早那条的 msgid；本批为空时传 0，表示锚点不变。
 *    hasMore：     应答里的 hasmore。
 */
void OnOlderPageLoaded(HistoryPagingState& st, long long oldestMsgid, bool hasMore);

} // namespace balloonchatwnd
