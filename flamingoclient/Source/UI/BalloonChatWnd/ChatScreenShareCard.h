/**
 *  ChatScreenShareCard.h
 *
 *  屏幕共享卡片（msgtype=19）在聊天窗里显示为一张卡片：屏幕共享图标与标题「屏幕共享」、「某某发起的共享」、状态文字，
 *  以及别人发起的卡片底部的「加入」按钮。本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上，共享状态
 *  变化时重填状态与按钮。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。卡片的绘制在 ChatBubble 里。
 *
 *  状态取自 exe 侧的共享状态缓存（ScreenShareStatusCache）：正在共享时按钮可点；已结束时按钮置灰；还不知道时按钮
 *  也可点 —— 共享若已结束，服务端会拒绝加入，DLL 随即回报「没有共享」，卡片被重填为已结束。
 *
 *  典型用法（聊天窗收到或翻到一张屏幕共享卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, _T("[屏幕共享]"));
 *      int state = hub.RoomState(ctype, cid, roomIdOf(notify));
 *      balloonchatwnd::ApplyScreenShareCard(msg, notify.m_screenShareContent, senderName, state);
 *      list->AppendMessage(msg);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare
#include "../../Feature/ScreenShare/ScreenShareRoomState.h"

namespace balloonchatwnd {

//卡片「加入」请宿主聊天窗加入共享时随 WM_CHAT_JOIN_SCREENSHARE 投递的堆对象，宿主处理后 delete
struct ScreenShareJoinRequest
{
    std::string m_roomId;     //房间号
    CString     m_hostName;   //发起人显示名，用作观看窗标题
    bool        m_forced;     //是否强制模式（第 13 期）：DLL 加入前据此先请用户确认
    bool        m_groupCall;  //是否群通话卡片的「加入」（群通话卡片复用屏幕共享卡片的版式，见 ChatGroupCallCard.h）：
                              //为 true 时宿主群聊窗经群通话中心加入群通话，m_hostName 为发起人显示名

    ScreenShareJoinRequest()
        : m_forced(false)
        , m_groupCall(false)
    {
    }
};

/**
 *  从卡片 content 原文里取房间号（聊天窗查共享状态时用）。
 *  @return 房间号；内容不可用时为空串
 */
std::string ScreenShareRoomIdOf(const std::string& contentJson);

/**
 *  把一条气泡数据改成屏幕共享卡片：kind 置为 kMsgScreenShare，填写 screenShareXxx 字段，text 置为「[屏幕共享]」
 *  （引用回复、复制等仍按 text 取摘要的地方因此拿到它）。content 不可用时仍是一张卡片：没有房间号、没有按钮。
 *  @param msg         [入参/出参] 已按普通文字气泡构造好的气泡数据（fromMe、头像、msgid 等保持不变）；
 *                     fromMe 为真表示本人发起，本人的卡片不显示按钮
 *  @param contentJson content 原文，原样存入 msg.screenShareContent
 *  @param hostName    发起人显示名（通讯录或群名片里的名字）；为空时用 content 里的 hostname
 *  @param roomState   这一场共享的状态，取值见 enum ScreenShareRoomState
 */
void ApplyScreenShareCard(ChatMessage& msg, const std::string& contentJson, const CString& hostName, int roomState);

/**
 *  共享状态变了：重填状态文字与按钮是否可点。
 *  @param msg       [入参/出参] 屏幕共享卡片（kind 不是 kMsgScreenShare 时不做任何事）
 *  @param roomState 新状态，取值见 enum ScreenShareRoomState
 */
void ApplyScreenShareState(ChatMessage& msg, int roomState);

/**
 *  发起人在共享中改了标题与描述（第 15 期，cmd_screenshare_info_notify=1734）：按改写后的卡片 content 重填标题行与描述，
 *  并把 content 原文换成新的；副标题、状态、按钮保持不变。
 *  @param msg         [入参/出参] 屏幕共享卡片
 *  @param contentJson 改写后的卡片 content 原文
 *  @return 标题、描述或 content 原文确有变化返回 true；msg 不是屏幕共享卡片、新内容不可用或与原来相同时返回 false，
 *          此时 msg 不变
 */
bool ApplyScreenShareInfo(ChatMessage& msg, const std::string& contentJson);

} // namespace balloonchatwnd
