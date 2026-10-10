/**
 *  ChatCallCard.h
 *  一对一通话卡片（msgtype=16 通话记录，content 的 kind 为 call，docs/协议设计-v2.md 7.8「一对一通话卡片」）在单聊窗里显示为一张卡片。
 *  卡片复用屏幕共享卡片的版式（kind 同为 kMsgScreenShare，screenShareIsCallCard 为 true）：标题前按媒体类型画电话或摄像机，
 *  粗体标题「语音通话」「视频通话」，副标题「我发起的通话」或「某某发起的通话」，状态行写结束方式与时长，没有按钮。
 *  状态的来源：content 里带了 result（服务端在通话结束时改写过卡片）就按结束方式写，接通过的写时长；还没结束时看本人此刻是否
 *  正与对方通话，在通话中写「通话中」，否则主叫写「正在呼叫」、被叫写「邀请你通话」。
 *  纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试（Source/Tests/CallUxTests.cpp 的 CCD1~CCD4）。
 *
 *  典型用法（单聊窗收到或翻到一张通话卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, TR(L"[通话]"));
 *      balloonchatwnd::ApplyCallCard(msg, notify.m_groupCallContent, m_peerName, m_inCallMedia);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare

namespace balloonchatwnd {

//一对一通话卡片的媒体类型：与协议 call_media 取值一致
const int kCallCardVoice = 1;   //语音通话
const int kCallCardVideo = 2;   //视频通话

//从一对一通话卡片 content 解析出的字段
struct CallCardInfo
{
    std::string m_sessionId;       //通话会话 id
    int         m_media;           //媒体类型：kCallCardVoice / kCallCardVideo（结束时改写为结束时的媒体类型）
    int64_t     m_callerId;        //主叫 userid
    int64_t     m_calleeId;        //被叫 userid
    CString     m_callerName;      //呼叫时写进卡片的主叫昵称
    bool        m_ended;           //content 里是否带了 result（通话结束时服务端改写过）
    int         m_result;          //结束方式，取值同协议 call_state（7 已结束、2 已拒绝、3 已取消、5 超时）；m_ended 为 true 时有效
    bool        m_connected;       //content 里是否带了 duration（接通过）
    int64_t     m_durationSeconds; //接通到结束的时长，单位秒；m_connected 为 true 时有效

    CallCardInfo()
        : m_media(0)
        , m_callerId(0)
        , m_calleeId(0)
        , m_ended(false)
        , m_result(0)
        , m_connected(false)
        , m_durationSeconds(0)
    {
    }
};

/**
 *  解析一对一通话卡片的 content。
 *  @param contentJson content 原文（紧凑 JSON）
 *  @param out         [出参] 解析结果；返回 false 时内容未定义
 *  @return content 是 JSON 对象且 kind 为 call 时返回 true
 */
bool ParseCallCard(const std::string& contentJson, CallCardInfo& out);

/**
 *  卡片状态行的文字。
 *  @param info         解析出的卡片（ParseCallCard 的结果）
 *  @param selfIsCaller 本人是不是主叫（卡片记在主叫名下，所以「卡片是本人发的」即本人是主叫）
 *  @param inCallMedia  本人此刻与对方通话的媒体类型（kCallCardVoice / kCallCardVideo）；不在通话中为 0。只对还没结束的卡片起作用
 *  @return 已按当前界面语言翻译的状态文字，如「通话时长 02:51」「对方已拒绝」「通话中」
 */
CString CallCardStatusText(const CallCardInfo& info, bool selfIsCaller, int inCallMedia);

/**
 *  把一条气泡数据改成一对一通话卡片：kind 置为 kMsgScreenShare 并置 screenShareIsCallCard，填写标题、副标题与状态，没有按钮；
 *  text 置为「[语音通话]」或「[视频通话]」（引用回复、复制等仍按 text 取摘要的地方因此拿到它）。content 不可用时仍是一张
 *  语音通话卡片，状态写「通话已结束」。
 *  @param msg         [入参/出参] 已按普通文字气泡构造好的气泡数据（fromMe、头像、msgid 等保持不变）
 *  @param contentJson content 原文，原样存入 msg.screenShareContent
 *  @param peerName    对方的显示名（备注优先）；卡片是对方发的时副标题用它，为空时用 content 里的主叫昵称
 *  @param inCallMedia 本人此刻与对方通话的媒体类型；不在通话中为 0
 */
void ApplyCallCard(ChatMessage& msg, const std::string& contentJson, const CString& peerName, int inCallMedia);

/**
 *  卡片是否已结束（content 带 result）。单聊窗在通话状态变化时只重填还没结束的卡片。
 *  @param contentJson content 原文
 *  @return 已结束返回 true；不是一对一通话卡片或没结束返回 false
 */
bool IsCallCardEnded(const std::string& contentJson);

} // namespace balloonchatwnd
