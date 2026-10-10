/**
 *  ChatSystemTip.h
 *
 *  把服务端生成的系统消息（msgtype=17）构造成聊天窗里居中的灰色提示行，单聊窗与群聊窗共用。
 *  群聊的系统提示（发布公告、入群欢迎、退出群聊、接龙参与等）发送者为 0；单聊里唯一的系统提示是屏幕共享的结束提示
 *  「屏幕共享已结束，时长 01:05」，服务端把它记在发起人名下（单聊会话由收发双方的 userid 确定，见
 *  docs/屏幕共享与录制方案.md 5.3）。两者在界面上一样：不画发送人气泡，只画一行居中的灰字。
 *
 *  典型用法：
 *      if (notify.m_msgtype == message_type_system)
 *          list->AppendMessage(balloonchatwnd::MakeSystemTipRow(notify));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "ChatMockData.h"
#include "../../net/MessageProtocolClient.h"

namespace balloonchatwnd {

/**
 *  构造系统提示行。复用列表里「居中时间分隔」那一种行（kMsgTime），带上 msgid 与时间，列表据 msgid 去重
 *  （推送与拉历史可能各来一次）。
 *  @param m 系统消息；正文在 m.m_text（UTF-8），为空时用类型占位文字
 *  @return 提示行；接龙的系统提示带着卡片 msgid（relayCardMsgid），提示行据此可以点击打开详情窗
 */
ChatMessage MakeSystemTipRow(const net::MessageNotify& m);

/**
 *  取历史里一条已撤回消息的撤回者。群主与管理员可以撤回别人的群消息（服务端 ChatSession.cpp 的
 *  群消息撤回分支），所以撤回者不一定是发送人；服务端记录了撤回者时随历史下发 recalledby
 *  （2026-10-03 起）。没有记录的（这之前撤回的消息）按发送人处理，绝大多数撤回是发送人
 *  自己操作的。
 *  @param m 已撤回的消息
 *  @return 撤回者 userid：m.m_recalledBy 大于 0 时取它，否则取 m.m_from
 */
int64_t RecallOperatorOf(const net::MessageNotify& m);

/**
 *  把历史里一条已撤回的消息（m.m_recalled 为 true）构造成撤回提示行，文字与在线收到撤回推送时
 *  气泡就地改成的提示行相同（balloonchat::BuildRecallTipText）：本人撤回的写「你撤回了一条消息」，
 *  别人撤回的写「某某撤回了一条消息」，名字为空时写「对方撤回了一条消息」。撤回者由调用方用
 *  RecallOperatorOf 取得。
 *  复用「居中时间分隔」那一种行（kMsgTime），带上原消息的 msgid 与时间，列表据 msgid 去重。
 *  服务端拉历史时照样下发已撤回的消息（带 recalled: true），聊天窗渲染历史前先用本函数替换掉，
 *  否则撤回的内容在重开窗口后又会显示出来。
 *  @param m             已撤回的消息
 *  @param recalledByMe  撤回者是否本人
 *  @param recallerName  撤回者的显示名；recalledByMe 为 true 时不用
 *  @return 提示行
 */
ChatMessage MakeRecalledTipRow(const net::MessageNotify& m, bool recalledByMe, const CString& recallerName);

/**
 *  窗口抖动的居中提示文字，实时收发与历史共用：本人发出的为「你向对方发送了窗口抖动」，对方发来的为
 *  「对方向你发送了窗口抖动」。
 *  @param fromMe 是否本人发出
 *  @return 当前界面语言的提示文字
 */
CString ShakeTipText(bool fromMe);

/**
 *  把历史里的一条窗口抖动消息（msgtype=13，没有正文）构造成居中提示行，文字取 ShakeTipText，与实时收发时插入的
 *  提示相同。复用「居中时间分隔」那一种行（kMsgTime），带上 msgid 与时间，列表据 msgid 去重。
 *  此前历史里的抖动落进「非文本用占位文字」的分支，画成内容为「[抖一抖]」的文字气泡。
 *  @param m      抖动消息
 *  @param fromMe 是否本人发出
 *  @return 提示行
 */
ChatMessage MakeShakeTipRow(const net::MessageNotify& m, bool fromMe);

} // namespace balloonchatwnd
