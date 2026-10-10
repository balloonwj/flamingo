/**
 *  一对一通话卡片的落库、改写与推送（docs/音视频通话优化方案.md 第 6 项、docs/协议设计-v2.md 7.8「一对一通话卡片」），CallCardWriter.h
 *
 *  呼叫建好会话时，以主叫的名义往两人的单聊写一张「语音 / 视频通话」卡片（message_type_rtc，content 的 kind 为 call）
 *  并以 cmd_message_notify 推给双方；通话结束（拒绝、取消、挂断、振铃超时、掉线结束）时把卡片改写成结束状态与时长，
 *  以 cmd_call_card_notify 推给双方，客户端据此改本地库并刷新单聊窗。卡片 content 的构造见 CallProtocol，本文件只负责
 *  落库与推送（用到 MessageManager、ChatServer 等单例，只编进 chatserver，不进单元测试）。
 *
 *  典型用法：
 *      std::string sessionId = mgr.createSession(callerId, calleeId, media, now);
 *      CallCardWriter::writeStartCard(sessionId, callerId, callerName, calleeId, media, now);
 *      ...
 *      CallSessionInfo s;
 *      mgr.getSession(sessionId, s);                          //结束前取会话快照
 *      CallCardWriter::finishCard(s, call_state_rejected, now);
 *      mgr.removeSession(sessionId);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>

struct CallSessionInfo;

/**
 *  CallCardWriter —— 一对一通话卡片的写入与改写。全是静态方法，可在任意 EventLoop 线程与主循环的定时任务里调用
 *  （MessageManager 与 ChatServer 的推送各自线程安全）。失败只记日志，不影响通话本身。
 */
class CallCardWriter
{
public:
    /**
     *  呼叫建好会话时写卡片：以主叫的名义写入两人的单聊，推给双方的全部在线设备，并把 msgid 与 content 记进
     *  CallManager 的会话表（结束时据此改写）。
     *  @param sessionId  会话 id（须已由 CallManager::createSession 登记）
     *  @param callerId   主叫 userid
     *  @param callerName 主叫昵称（卡片上显示）
     *  @param calleeId   被叫 userid
     *  @param media      呼叫时的媒体类型，取值见 enum call_media
     *  @param nowMs      当前时刻，Unix 毫秒（卡片的发送时间与 starttime）
     */
    static void writeStartCard(const std::string& sessionId, int64_t callerId, const std::string& callerName,
                               int64_t calleeId, int media, int64_t nowMs);

    /**
     *  通话结束时改写卡片并推 cmd_call_card_notify 给双方的全部在线设备。没有写成卡片（cardMsgId 为 0）时什么也不做。
     *  @param s      结束前的会话快照（removeSession 或定时清理之前取得，含 cardMsgId、cardContent、connectTime）
     *  @param result 结束方式，取值见 enum call_state（ended / rejected / canceled / timeout）
     *  @param nowMs  结束时刻，Unix 毫秒
     */
    static void finishCard(const CallSessionInfo& s, int result, int64_t nowMs);

private:
    CallCardWriter() = delete;
    ~CallCardWriter() = delete;
    CallCardWriter(const CallCardWriter&) = delete;
    CallCardWriter& operator=(const CallCardWriter&) = delete;
};
