/**
 *  帖子卡片与帖子动态卡片被服务端改写后，同步到本机（docs/帖子群方案.md 14.6）：
 *    - 删帖、删留言、删补充、编辑帖子之后服务端改写对应卡片，当时在线的成员经 2721 推送收到改写后的卡片；
 *    - 当时不在线的成员，登录后经按改写序号同步（cmd_sync_rewritten_messages，Feature/RewrittenSync）补上。
 *      2026-10-08 之前这一步是把本地卡片的 msgid 分批经 2716 发给服务端核对，现已停用（docs/消息改写离线同步方案.md 5.5）。
 *  收到的卡片一律经 ApplyThreadCardToStore 写回本地消息库（content 列、原始 JSON 与搜索文本一起改写，被删的原文
 *  不再出现在消息记录与本地搜索里），会话列表最后一条正是它时由主面板刷新摘要（ThreadCardDigestOf）。
 *
 *  本文件是纯逻辑（JSON 解析与摘要）加一个对本地消息库的操作，不碰窗口，可被 FlamingoUnit 对着真的 SQLite 库测试。
 *
 *  典型用法：
 *      int64_t groupId = 0;
 *      std::vector<ThreadCardUpdate> cards;
 *      if (ParseThreadCards(body, groupId, cards))           //2721 推送
 *          for (...) ApplyThreadCardToStore(store, groupId, cards[i], hit);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonhistory { class CV2MsgStore; }

namespace balloonthread {

//一张被改写的卡片（2721 推送里 cards 数组的一项）
struct ThreadCardUpdate
{
    int64_t     m_msgid;     //卡片的 msgid
    int         m_msgtype;   //message_type_thread_post 或 message_type_thread_reply
    std::string m_content;   //改写后的 content（紧凑 JSON 文本）

    ThreadCardUpdate()
        : m_msgid(0)
        , m_msgtype(0)
    {
    }
};

/**
 *  解析 2721 推送：{groupid, cards: [{msgid, msgtype, content}]}。带 code 字段（2716 应答的格式，客户端已不再发 2716）
 *  且不为 0 时整体不要。
 *  msgid 不大于 0、msgtype 不是 25 或 26、content 不是对象的项跳过。
 *  @param body    包体（JSON）
 *  @param groupId [出参] 帖子群
 *  @param out     [出参] 卡片
 *  @return 包体是 JSON 对象、群号合法、且 code（有时）为 0 时返回 true
 */
bool ParseThreadCards(const std::string& body, int64_t& groupId, std::vector<ThreadCardUpdate>& out);

/**
 *  一张卡片当前内容对应的会话摘要（不含发送者名字，主面板显示时再加），如「帖子已删除」「留言已删除」。
 *  @return content 解析不了时返回空串
 */
std::string ThreadCardDigestOf(int msgtype, const std::string& content);

/**
 *  把一张改写后的卡片写回本地消息库：content 列、原始 JSON 与搜索文本按新卡片重新生成，置顶、已听等其余列原样保留。
 *  @param store   本地消息库（已打开）
 *  @param groupId 帖子群
 *  @param card    改写后的卡片
 *  @param outHit  [出参] 本地有这一行并已改写时为 true；本地没有（本机没同步过这段历史）时为 false
 *  @return 读写本地库成功、或本地没有这一行时返回 true；本地那一行不是帖子卡片、读写失败时返回 false
 */
bool ApplyThreadCardToStore(balloonhistory::CV2MsgStore& store, int64_t groupId, const ThreadCardUpdate& card,
                            bool& outHit);

} // namespace balloonthread
