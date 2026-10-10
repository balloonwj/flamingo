/**
 *  ChatSendBumpQueue.h
 *
 *  聊天窗「发送后把会话冒泡到会话列表顶部」的待发队列（docs/敏感词方案.md 第 8.5 节）。
 *
 *  以前每个发送点在按下发送的当时就通知主面板冒泡，被服务端拒绝的消息（包含敏感词、已被禁言等）也会成为
 *  会话预览。现在各发送点先把冒泡内容按客户端消息 ID 存进本队列，发送回执成功到达时再取出发给主面板；
 *  回执失败、或图片 / 文件在发出消息之前就上传失败时丢弃。窗口抖动没有气泡行，同样走本队列。
 *
 *  只在 UI 线程使用，不加锁。为防止异常路径（如上传线程没有回投结果）让条目一直留着，队列最多保留
 *  kMaxPending 条，超出时丢弃最早的。
 *
 *  典型用法：
 *      m_sendBumps.Add(clientMsgId, bump);               //发送时
 *      ConvSendBumpData bump;
 *      if (m_sendBumps.TakeOnSuccess(clientMsgId, bump))  //回执成功时
 *          PostBumpToHost(bump);
 *      m_sendBumps.Drop(clientMsgId);                     //回执失败或上传失败时
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <deque>
#include <map>
#include <string>

#include "../../Session/UserSessionData.h"   // ConvSendBumpData

namespace balloonchatwnd {

// 发送后待冒泡的队列。
class ChatSendBumpQueue
{
public:
    // 队列最多保留的条数，超出时丢弃最早加入的
    static const size_t kMaxPending = 256;

    // 登记一条待冒泡的发送。同一个 clientMsgId 再次登记时覆盖原来的内容。
    //   clientMsgId：本端生成的客户端消息 ID；为空时不登记。
    //   data：冒泡内容（会话、消息类型、预览文字）。
    void Add(const std::string& clientMsgId, const ConvSendBumpData& data);

    // 发送成功：取出并移除该条。
    //   clientMsgId：回执里的客户端消息 ID。
    //   out：[出参] 取到时为冒泡内容。
    //   返回：队列里有这一条返回 true；没有（别的窗口发出的、已取过、已丢弃）返回 false。
    bool TakeOnSuccess(const std::string& clientMsgId, ConvSendBumpData& out);

    // 发送失败：丢弃该条，不再冒泡。队列里没有这一条时什么都不做。
    void Drop(const std::string& clientMsgId);

    // 当前待冒泡的条数。
    size_t Size() const { return m_pending.size(); }

private:
    std::map<std::string, ConvSendBumpData> m_pending;  // clientMsgId → 冒泡内容
    std::deque<std::string>                 m_order;    // 登记顺序，超出上限时据此丢弃最早的；可能含已取走的 id
};

/**
 *  发一个文件时要登记的冒泡内容：会话类型、会话 id、文件消息类型、摘要「[文件]」。单聊发文件的首次发送与
 *  「重新发送」共用（重发原先不登记，重发成功后会话不冒泡、预览不更新）。
 *  @param ctype 会话类型（1 单聊、2 群聊）
 *  @param cid   会话 id（单聊为对端 userid，群聊为群 id）
 *  @return 冒泡内容
 */
ConvSendBumpData MakeFileSendBump(int ctype, long long cid);

} // namespace balloonchatwnd
