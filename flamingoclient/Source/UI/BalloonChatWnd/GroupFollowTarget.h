/**
 *  GroupFollowTarget.h
 *
 *  群聊窗与主面板之间关于「群关注名单」的消息负载（docs/消息提醒模式与消息盒子方案.md §12）：
 *  FMG_MSG_QUERY_GROUP_FOLLOW（同步 SendMessage，问某位成员是否已被本人在本群关注，负载在调用方栈上）
 *  与 FMG_MSG_TOGGLE_GROUP_FOLLOW（PostMessage，请主面板切换关注状态，负载在堆上、主面板处理后 delete）
 *  共用这一个结构。名单缓存只有主面板那一份（balloonmain::GroupFollowModel），群聊窗只负责转达。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

namespace balloonchatwnd {

/**
 *  GroupFollowTarget —— 「哪个群里的哪位成员」。
 */
struct GroupFollowTarget
{
    int64_t m_groupId;  // 群ID
    int64_t m_userId;   // 成员 userid

    GroupFollowTarget()
        : m_groupId(0)
        , m_userId(0)
    {
    }
};

} // namespace balloonchatwnd
