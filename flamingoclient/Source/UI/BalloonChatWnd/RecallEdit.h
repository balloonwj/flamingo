/**
 *  RecallEdit.h
 *
 *  撤回后重新编辑（docs/撤回后重新编辑方案.md）的纯逻辑：本人撤回一条文字类消息后，撤回提示行末尾在 5 分钟内
 *  显示可点击的「编辑」，点击后把撤回的内容放回输入框。本文件只放与窗口无关的部分，便于单测：
 *
 *    · 是否给「编辑」的判定（CanOfferRecallEdit）；
 *    · 撤回那一刻生成的快照（BuildRecallDraft），以及快照与本机库里 UTF-8 JSON 之间的转换
 *      （SerializeRecallDraft / ParseRecallDraft）；
 *    · 5 分钟时限的到期计算（RecallEditDeadline / IsRecallEditAlive）。
 *
 *  快照就是现有「重新编辑」的数据 ChatReeditData，点「编辑」时照原样投 WM_CHAT_REEDIT_REQUEST，宿主的处理与
 *  发送失败后的「重新编辑」完全相同。
 *
 *  典型用法（聊天窗收到本人撤回的推送、在 RecallToTip 覆盖原文之前）：
 *      if (CanOfferRecallEdit(bubbleMsg, true))
 *      {
 *          ChatReeditData draft = BuildRecallDraft(bubbleMsg);
 *          std::string payload = SerializeRecallDraft(draft);   //交给主面板写入本机库 t_recall_draft
 *          long long deadline = RecallEditDeadline(RecallEditNowMs(), RecallEditNowMs());
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "ChatMockData.h"   // ChatMessage
#include "ChatReedit.h"     // ChatReeditData

namespace balloonchatwnd {

/**
 *  本机库里的一条撤回快照（FMG_MSG_QUERY_RECALL_DRAFTS 查询结果的一项）。
 */
struct RecallDraftHit
{
    long long   m_msgid;          // 被撤回消息的服务器 msgid
    long long   m_recallTimeMs;   // 撤回推送到达本机的时间（Unix 毫秒）
    std::string m_payload;        // 快照 JSON（UTF-8）

    RecallDraftHit()
        : m_msgid(0)
        , m_recallTimeMs(0)
    {
    }
};

/**
 *  聊天窗拉历史时向主面板查撤回快照（同步 SendMessage FMG_MSG_QUERY_RECALL_DRAFTS，主面板就地填写）。
 */
struct RecallDraftQuery
{
    int                         m_ctype;        // 会话类型（1 单聊 2 群聊），与本机库 t_msg 口径相同
    long long                   m_cid;          // 会话ID（单聊=对端userid，群聊=群id）
    std::vector<long long>      m_msgids;       // 入参：本页里本人撤回的消息 msgid
    std::vector<RecallDraftHit> m_hits;         // 出参：查到的快照
    bool                        m_storeReady;   // 出参：本机库是否可用

    RecallDraftQuery()
        : m_ctype(0)
        , m_cid(0)
        , m_storeReady(false)
    {
    }
};

// 撤回后可以重新编辑的时长（毫秒）：5 分钟。2026-10-06 用户确认。
const long long kRecallEditWindowMs = 5LL * 60LL * 1000LL;

// 快照 JSON 的格式版本号。解析时遇到不认识的版本一律视为失败，不按猜测的格式去读。
const int kRecallDraftVersion = 1;

/**
 *  判断一条消息在撤回后要不要给「编辑」。
 *  条件：本人发送、已有服务器 msgid、不是发送失败的行、是文字类（kMsgText，含 Markdown 与代码片段；或大表情
 *  kMsgEmoji），并且撤回者是本人（群主、管理员撤回别人的消息不给）。
 *    msg：         撤回前的那一行消息（原文尚未被提示文字覆盖）。
 *    recalledByMe：撤回者是否本人。
 *  @return 要给返回 true。本函数无副作用。
 */
bool CanOfferRecallEdit(const ChatMessage& msg, bool recalledByMe);

/**
 *  从撤回前的那一行消息生成快照（与发送失败后「重新编辑」放回输入框的内容相同）。
 *    msg：撤回前的消息；不满足「文字类、本人发送」时返回空内容。
 *  @return 快照。本函数无副作用。
 */
ChatReeditData BuildRecallDraft(const ChatMessage& msg);

/**
 *  把快照写成紧凑的 UTF-8 JSON，存进本机库 t_recall_draft 的 payload 列。
 *  格式：{"v":1,"segs":[{"k":0,"t":"文字"},{"k":1,"f":表情编号}],"ats":[{"uid":n,"name":"显示名"}],
 *         "md":bool,"mdsrc":"Markdown 源文本","code":bool,"lang":n,"src":"代码原文",
 *         "notes":[{"from":起始行,"to":结束行,"text":"批注"}]}
 *  notes 是代码片段的批注（docs/代码片段方案.md 第 10.5 节），读回时按代码行数整理（SanitizeCodeNotes）。
 *  图片片段（kChatClipImage）不写入：文字消息的正文里不会出现图片。
 *    data：快照。
 *  @return JSON 文本。本函数无副作用。
 */
std::string SerializeRecallDraft(const ChatReeditData& data);

/**
 *  解析 SerializeRecallDraft 写出的 JSON。
 *    json：JSON 文本（UTF-8）。
 *    out： [出参] 解析成功时写入快照；失败时不改动。
 *  @return 成功返回 true；JSON 损坏、不是对象、版本号不认识、缺少片段数组时返回 false。本函数无副作用。
 */
bool ParseRecallDraft(const std::string& json, ChatReeditData& out);

/**
 *  计算「编辑」的到期时刻。
 *    recallTimeMs：撤回推送到达本机的时间（Unix 毫秒，本机时钟）；<= 0 表示未知。
 *    nowMs：       当前时间（Unix 毫秒）。
 *  @return 到期时刻（Unix 毫秒）：一般为 recallTimeMs + kRecallEditWindowMs；撤回时间晚于当前时间（本机时钟被
 *          往回调过）时按当前时间计，保证到期时刻不超过「当前时间加 5 分钟」；撤回时间未知时返回 0（已到期）。
 */
long long RecallEditDeadline(long long recallTimeMs, long long nowMs);

/**
 *  判断「编辑」在给定时刻是否仍然有效。
 *    deadlineMs：到期时刻（RecallEditDeadline 的结果），<= 0 表示没有「编辑」。
 *    nowMs：     当前时间（Unix 毫秒）。
 *  @return nowMs 早于到期时刻返回 true。
 */
bool IsRecallEditAlive(long long deadlineMs, long long nowMs);

/**
 *  取本机当前时间（Unix 毫秒，按系统时钟）。撤回时刻与到期判断都用它，保证同一套时钟。
 */
long long RecallEditNowMs();

/**
 *  向主面板查一批 msgid 的撤回快照（同步 SendMessage FMG_MSG_QUERY_RECALL_DRAFTS）。
 *    hostPanel：主面板窗口；不是有效窗口时（调试入口）什么都查不到。
 *    query：    [入出参] 入参填 ctype、cid、msgids；返回时 m_hits、m_storeReady 已填好。
 */
void QueryRecallDrafts(HWND hostPanel, RecallDraftQuery& query);

/**
 *  拉历史生成撤回提示行时，按查询结果给这一行挂上「编辑」：查到这条 msgid 的快照、且尚未到期时，写入
 *  recallDraftJson 与 recallEditDeadlineMs；否则不改动。
 *    tipRow：撤回提示行（MakeRecalledTipRow 的结果）。
 *    query： 查询结果。
 *    nowMs： 当前时间（Unix 毫秒）。
 *  @return 挂上了「编辑」返回 true。
 */
bool ApplyRecallDraft(ChatMessage& tipRow, const RecallDraftQuery& query, long long nowMs);

/**
 *  按下一个到期时刻重排聊天窗的到期定时器：nextDeadlineMs 为 0 时停掉定时器，否则定在到期之后一小段时间触发
 *  （最短也隔 kRecallEditTimerMinMs，避免连续触发）。
 *    hwnd：          聊天窗。
 *    timerId：       定时器 id（各窗口自定）。
 *    nextDeadlineMs：最早的到期时刻（Unix 毫秒），ChatMessageList::ExpireRecallEdits 的返回值；0 表示没有。
 *    nowMs：         当前时间（Unix 毫秒）。
 */
void RescheduleRecallEditTimer(HWND hwnd, UINT_PTR timerId, long long nextDeadlineMs, long long nowMs);

} // namespace balloonchatwnd
