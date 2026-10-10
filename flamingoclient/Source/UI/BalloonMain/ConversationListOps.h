/**
 *  ConversationListOps.h
 *
 *  最近会话列表（std::vector<net::ConversationItem>）的<u>纯操作函数</u>集合：
 *  排序、按 (ctype,cid) 查找、删除单条 / 全部、改置顶 / 免打扰标志。
 *
 *  存在理由 —— 这些逻辑原先散在 CBalloonMainPanel 里（排序比较器是
 *  BalloonMainPanel.cpp 的文件静态函数，删除 / 改标志则是各处手写的遍历循环），
 *  既无法被 FlamingoUnit 测试工程调用，也容易在新增入口时各写一份口径不一致的
 *  遍历。抽到这里后：不依赖 HWND、不依赖 balloonui、不依赖 CFlamingoClient，
 *  只碰传进来的 vector，可被单元测试直接断言（见
 *  flamingoclient/Source/Tests/ConversationListOpsTests.cpp）。
 *
 *  <u>纯内存操作，不发协议</u>：这些函数只改本地缓存，向服务端上报（cmd_set_conversation
 *  等）由调用方 CBalloonMainPanel 负责，两件事刻意分开以便各自测试。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <stdint.h>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::ConversationItem

namespace balloonmain {

/**
 *  ConversationKey —— 一条会话的唯一标识。会话在 v2 协议里由 (ctype, cid) 二元组
 *  唯一确定：单聊的 cid 是对端 userid、群聊的 cid 是群 id，两个 id 空间不重叠但
 *  仍须带上 ctype 才能无歧义地区分。
 *
 *  用途：删除全部会话时先把各会话的标识快照下来（清空 vector 之后就取不到了），
 *  再逐个发协议 / 清托盘闪烁。
 */
struct ConversationKey
{
    int     m_ctype;   // 会话类型：见 net 层 enum conversation_type，1单聊 2群聊
    int64_t m_cid;     // 会话ID：单聊=对端userid，群聊=群id

    ConversationKey()
        : m_ctype(0)
        , m_cid(0)
    {
    }

    ConversationKey(int ctype, int64_t cid)
        : m_ctype(ctype)
        , m_cid(cid)
    {
    }
};

/**
 *  会话列表的排序比较：置顶的排前面，其次按最后消息时间倒序，时间相同时按 cid
 *  升序兜底（保证顺序稳定、不因排序算法实现而抖动）。
 *
 *  这就是"消息"Tab 里会话行的呈现顺序，任何改动会话的操作之后都应重新排序。
 *    a  左侧会话。
 *    b  右侧会话。
 *  @return a 应排在 b 前面返回 true
 */
bool ConversationOrderLess(const net::ConversationItem& a, const net::ConversationItem& b);

/**
 *  按 ConversationOrderLess 就地重排会话列表。
 *    items  [出入参] 会话列表。
 */
void SortConversations(std::vector<net::ConversationItem>& items);

/**
 *  按 (ctype,cid) 在会话列表里查一条会话（可写版本）。
 *    items  会话列表。
 *    ctype  会话类型（1单聊 2群聊）。
 *    cid    会话ID。
 *  @return 命中的会话指针（指向 items 内部元素，调用方不得跨"下次增删 items"持有）；
 *          没有该会话返回 NULL
 */
net::ConversationItem* FindConversation(std::vector<net::ConversationItem>& items,
                                        int ctype, int64_t cid);

/**
 *  按 (ctype,cid) 在会话列表里查一条会话（只读版本）。所有权 / 生命周期说明同上。
 *    items  会话列表。
 *    ctype  会话类型（1单聊 2群聊）。
 *    cid    会话ID。
 *  @return 命中的会话指针；没有该会话返回 NULL
 */
const net::ConversationItem* FindConversation(const std::vector<net::ConversationItem>& items,
                                              int ctype, int64_t cid);

/**
 *  从会话列表里删除指定会话。理论上 (ctype,cid) 唯一，但防御性地把所有匹配项都删掉，
 *  避免脏数据留下一条看不见摸不着的重复会话。
 *    items  [出入参] 会话列表。
 *    ctype  会话类型（1单聊 2群聊）。
 *    cid    会话ID。
 *  @return 实际删掉的条数；没有该会话返回 0
 */
size_t RemoveConversation(std::vector<net::ConversationItem>& items, int ctype, int64_t cid);

/**
 *  取会话列表里全部会话的 (ctype,cid) 标识快照，顺序与 items 一致。供"删除全部会话"
 *  在清空列表<u>之前</u>留下标识，用于逐个上报服务端与清除托盘闪烁。
 *    items  会话列表。
 *  @return 标识列表（值语义，调用方拥有）
 */
std::vector<ConversationKey> CollectConversationKeys(const std::vector<net::ConversationItem>& items);

/**
 *  设置某会话的置顶标志。置顶会改变列表顺序，故本函数在标志真的变了之后会顺带重排。
 *    items   [出入参] 会话列表。
 *    ctype   会话类型（1单聊 2群聊）。
 *    cid     会话ID。
 *    pinned  目标置顶状态。
 *  @return 标志确实发生了变化返回 true；会话不存在、或本就是该状态返回 false
 *          （返回 false 时不需要重绘列表）
 */
bool SetConversationPinned(std::vector<net::ConversationItem>& items,
                           int ctype, int64_t cid, bool pinned);

/**
 *  设置某会话的「手动标为未读」标志（docs/会话标为未读方案.md）。不影响排序。
 *    items     [出入参] 会话列表。
 *    ctype     会话类型（1单聊 2群聊）。
 *    cid       会话ID。
 *    mark      目标状态。
 *  @return 标志确实发生了变化返回 true；会话不存在、或本就是该状态返回 false
 */
bool SetConversationUnreadMark(std::vector<net::ConversationItem>& items,
                               int ctype, int64_t cid, bool mark);

/**
 *  查某会话是否被手动标为未读。会话不在列表里时返回 false。
 */
bool IsConversationUnreadMarked(const std::vector<net::ConversationItem>& items, int ctype, int64_t cid);

//原来这里还有 SetConversationMuted / IsConversationMuted 两个函数（按会话缓存读写免打扰标志）。
//2026-10-03 起免打扰扩展成五种消息提醒模式，改由独立的模式表 NotifyModeModel 记录，
//两个函数随之删除，见 docs/消息提醒模式与消息盒子方案.md §7.2。

/**
 *  收到一条消息的当下，该会话能否直接算作"用户已经看到了"。
 *
 *  存在理由：消息到达时会被立刻渲染进"已经打开着"的聊天窗，但会话列表的未读数
 *  此前是无条件累加的，于是用户明明在窗口里看着这条消息，最近会话列表仍然亮着
 *  红点。本函数给出"算不算已经看到"的统一判据，供收消息路径决定累加还是清零。
 *
 *  五个入参刻意都用 bool 而不直接收 HWND —— 判据本身与 Win32 无关，拆成布尔量
 *  才能在单元测试里穷举组合；把 HWND 翻译成这五个值是调用方的事。
 *    windowExists   该会话的聊天窗对象是否存在（指针非空）。
 *    isWindow       窗口句柄是否仍然有效（::IsWindow）。对象还在但窗口已销毁时为 false。
 *    isVisible      窗口是否可见（::IsWindowVisible）。
 *    isIconic       窗口是否已最小化（::IsIconic）。<u>最小化的窗口 IsWindowVisible
 *                   仍返回 TRUE</u>，故必须单独判这一项，否则最小化时会被误判成已读。
 *    isForeground   窗口是否是当前前台窗口（::GetForegroundWindow 与之相等）。
 *    subtitleActive 该会话是否正处于字幕模式（docs/聊天字幕模式方案.md，字幕由主面板持有）。
 *                   用户确认过「字幕上显示过就算已读」：字幕模式下消息会显示在桌面字幕上，
 *                   不论聊天窗是否存在、可见、最小化、在不在前台都视为已经看到。默认 false。
 *  @return 正处于字幕模式时返回 true；否则窗口存在且有效、可见、未最小化、在前台五项全中
 *          时返回 true，此时该消息视为用户已经看到；其余情况返回 false，按未读处理
 */
bool ShouldTreatAsReadOnArrival(bool windowExists, bool isWindow, bool isVisible,
                                bool isIconic, bool isForeground, bool subtitleActive = false);

/**
 *  收到一条消息之后，该会话的未读数应当变成多少。
 *
 *  之所以独立成一个函数：光"不累加"是不够的 —— 该会话可能还挂着之前累积的旧未读
 *  （用户此前一直没点开过），窗口在前台看到新消息时那些旧未读也应当一并清掉，
 *  否则红点照样亮着。
 *    oldUnread      该会话当前的未读数，负值按 0 处理（防御脏数据）。
 *    countAsUnread  这条消息本身是否该计未读。自己在别的设备发出、同步回来的消息
 *                   为 false，见 net::ConversationsProtocolClient::shouldCountAsUnread。
 *    treatAsRead    当下是否视为用户已经看到，取自 ShouldTreatAsReadOnArrival。
 *  @return 新的未读数。视为已看到时恒为 0；否则该计未读就加一、不计则维持原值
 */
int NextUnreadCount(int oldUnread, bool countAsUnread, bool treatAsRead);

/**
 *  会话行副行的摘要该怎样显示，由 ClassifyRecalledDigest 给出。
 */
enum RecalledDigestKind
{
    //最后一条消息没有撤回：照原样显示 m_digest
    kRecalledDigestNone = 0,
    //最后一条消息已撤回、撤回者就是本人：显示「你撤回了一条消息」
    kRecalledDigestBySelf = 1,
    //最后一条消息已撤回、撤回者是别人：显示「某某撤回了一条消息」，名字按 RecalledDigestOperator 取
    kRecalledDigestByOther = 2,
};

/**
 *  判断一条会话的摘要要不要显示成撤回提示、撤回者是不是本人（2026-10-03 起）。
 *
 *  服务端对已撤回的最后一条只下发 recalled 标志与中性的「撤回了一条消息」，不带撤回者，
 *  因为它不知道查看者怎样称呼对方。客户端在渲染会话行时据本函数拼出与在线撤回
 *  （CBalloonMainPanel::OnMessageRecalled）同口径的文字；放在渲染时而不是收到会话列表时拼，
 *  是因为通讯录可能晚于会话列表到达，到达后会话树会整体重绘，名字随之更新。
 *
 *  撤回者取 RecalledDigestOperator：群主或管理员撤回别人的消息时它与发送方不同（2026-10-03 起）。
 *    item    一条会话。
 *    selfId  本端 userid；取不到时传 0，此时一律按别人撤回处理。
 *  @return 摘要的显示方式，见 RecalledDigestKind
 */
RecalledDigestKind ClassifyRecalledDigest(const net::ConversationItem& item, uint32_t selfId);

/**
 *  取会话最后一条已撤回消息的撤回者。服务端记录了撤回者时随会话列表下发（lastmsg.recalledby）；
 *  没有记录的（2026-10-03 服务端开始记录撤回者之前撤回的消息）按发送方处理，绝大多数撤回是发送方自己操作的。
 *    item  一条会话；只在 m_lastRecalled 为真时有意义。
 *  @return 撤回者 userid：m_lastRecalledBy 大于 0 时取它，否则取 m_lastFrom
 */
int64_t RecalledDigestOperator(const net::ConversationItem& item);

} // namespace balloonmain
