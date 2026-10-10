/**
 *  ChatGroupCallCard.h
 *
 *  群通话卡片（msgtype=16 通话记录，content 的 kind 为 groupcall，docs/协议设计-v2.md 21.5）在群聊窗里显示为一张卡片。
 *  卡片复用屏幕共享卡片的版式（kind 同为 kMsgScreenShare，screenShareIsGroupCall 为 true）：标题前按媒体类型画摄像机或电话，
 *  粗体标题「群语音通话」「群视频通话」，副标题「某某发起的通话」，状态行，通话没结束时底部有「加入」按钮。
 *  本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上，群通话状态变化时重填状态与按钮。纯逻辑，不碰绘制，
 *  可被 FlamingoUnit 直接测试（Source/Tests/ChatGroupCallCardTests.cpp，标签 [groupcall]）。
 *
 *  状态的来源有两个：content 里带了 duration（服务端在通话结束时改写过卡片）就一定是已结束、显示时长；否则取 exe 侧的
 *  群通话状态缓存（GroupCallStatusCache）：进行中显示人数、已结束、还不知道时写「正在获取状态」。
 *
 *  典型用法（群聊窗收到或翻到一张群通话卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, _T("[群语音通话]"));
 *      int state = hub.RoomState(groupId, GroupCallRoomIdOf(notify.m_groupCallContent));
 *      balloonchatwnd::ApplyGroupCallCard(msg, notify.m_groupCallContent, senderName, state, count);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare

namespace balloonchatwnd {

//从群通话卡片 content 解析出的字段
struct GroupCallCardInfo
{
    std::string m_roomId;            //房间号
    int         m_media;             //媒体类型：1 群语音、2 群视频
    int64_t     m_initiatorId;       //发起人 userid
    CString     m_initiatorName;     //发起时写进卡片的发起人显示名
    bool        m_ended;             //content 里是否带了时长（通话结束时服务端改写过）
    int64_t     m_durationSeconds;   //通话时长，单位秒；m_ended 为 true 时有效

    GroupCallCardInfo()
        : m_media(0)
        , m_initiatorId(0)
        , m_ended(false)
        , m_durationSeconds(0)
    {
    }
};

/**
 *  解析群通话卡片的 content。
 *  @return content 是 JSON 对象、kind 为 groupcall 且带 roomid 时返回 true
 */
bool ParseGroupCallCard(const std::string& contentJson, GroupCallCardInfo& out);

/**
 *  从卡片 content 原文里取房间号（群聊窗查群通话状态时用）。
 *  @return 房间号；不是群通话卡片或内容不可用时为空串
 */
std::string GroupCallRoomIdOf(const std::string& contentJson);

/**
 *  通话时长的文字：不足 1 小时为「MM:SS」，满 1 小时为「H:MM:SS」。
 *  @param seconds 秒数；负数按 0
 */
CString FormatGroupCallCardDuration(int64_t seconds);

/**
 *  把一条气泡数据改成群通话卡片：kind 置为 kMsgScreenShare 并置 screenShareIsGroupCall，填写标题、副标题、状态与按钮，
 *  text 置为「[群语音通话]」或「[群视频通话]」（引用回复、复制等仍按 text 取摘要的地方因此拿到它）。
 *  content 不可用时仍是一张卡片：没有房间号、没有按钮。
 *  @param msg           [入参/出参] 已按普通文字气泡构造好的气泡数据（fromMe、头像、msgid 等保持不变）
 *  @param contentJson   content 原文，原样存入 msg.screenShareContent
 *  @param initiatorName 发起人显示名（群名片或通讯录里的名字）；为空时用 content 里的 initiatorname
 *  @param roomState     这一场的状态，取值见 GroupCallStatusCache.h 的 GroupCallRoomState
 *  @param joinedCount   进行中时已加入的人数（来自状态缓存）；不知道时传 0
 */
void ApplyGroupCallCard(ChatMessage& msg, const std::string& contentJson, const CString& initiatorName, int roomState,
                        int joinedCount);

/**
 *  群通话状态变了：重填状态文字与按钮是否可点。content 里已带时长的卡片保持「已结束」不变。
 *  @param msg         [入参/出参] 群通话卡片（不是群通话卡片时不做任何事）
 *  @param roomState   新状态，取值见 GroupCallRoomState
 *  @param joinedCount 进行中时已加入的人数；不知道时传 0
 */
void ApplyGroupCallState(ChatMessage& msg, int roomState, int joinedCount);

} // namespace balloonchatwnd
