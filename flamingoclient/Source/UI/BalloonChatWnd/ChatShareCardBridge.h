/**
 *  ChatShareCardBridge.h
 *
 *  聊天窗与文件共享宿主之间关于共享卡片的两处衔接，单聊窗与群聊窗共用：
 *    - MakeShareCardLiveState：画卡片时向文件共享宿主要实时信息（模块是否已加载、DLL 手里的最新状态）；
 *    - RefreshShareCardsInList：DLL 通知某个共享的状态变了，把消息列表里这个共享的卡片就地重填并重排。
 *  卡片内容本身的解析与文字在 ChatFileShareCard（纯逻辑），这里只负责取数与刷新。
 *
 *  典型用法（单聊窗收到共享卡片时）：
 *      ChatMessage card = MakeRealTextMessage(...);
 *      ApplyFileShareCard(card, notify.m_shareContent, ownerName, MakeShareCardLiveState(notify.m_shareContent));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

#include "ChatFileShareCard.h"

namespace balloonchatwnd {

class ChatMessageList;

/**
 *  取共享卡片的实时信息：模块是否已加载、DLL 手里有没有这个共享的最新状态。
 *  @param contentJson 卡片 content 原文（从中取 shareid）；解析不出来时只填模块是否已加载
 *  @return 实时信息
 */
ShareCardLiveState MakeShareCardLiveState(const std::string& contentJson);

/**
 *  把消息列表里某个共享的全部卡片（同一个共享可能在会话里出现多张卡片，例如调整成员后另发的那张）按新状态重填。
 *  @param list    消息列表；为 NULL 时不做任何事
 *  @param shareId 共享 id
 *  @param state   新状态
 *  @param reason  新原因
 *  @return 重填了的卡片张数
 */
int RefreshShareCardsInList(ChatMessageList* list, long long shareId, int state, int reason);

} // namespace balloonchatwnd
