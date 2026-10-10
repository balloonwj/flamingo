/**
 *  ScreenShareStatusCache.h
 *
 *  exe 侧按会话记录的屏幕共享状态：每个会话（单聊按对方 userid、群聊按群 id）当前有没有进行中的共享、是哪一场、
 *  发起人是谁。数据来自 FlamingoMedia.dll 经 IMediaHost::OnScreenShareStatus 回报的三种情况：有人开始共享（1721）、
 *  共享结束（1729）、打开聊天窗时的查询应答（1727）。聊天窗据它判断一张共享卡片的那一场还在不在，群聊窗据它显示横幅。
 *
 *  只记录本次登录期间收到过的会话；没收到过的一律是「还不知道」，由聊天窗打开时发起一次查询补上。登出时清空。
 *  纯逻辑，不依赖窗口，所有方法只在 UI 线程调用，内部不加锁。用例见 Source/Tests/ScreenShareCardTests.cpp 的 SSC4。
 *
 *  典型用法：
 *      cache.Update(conversation_single, peerId, roomId, hostId, hostName, true);
 *      int state = cache.RoomState(conversation_single, peerId, card.screenShareRoomId);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <utility>

#include "ScreenShareRoomState.h"

namespace balloonscreenshare {

//某个会话里进行中的那一场共享
struct ScreenShareActiveInfo
{
    std::string  m_roomId;     //房间号
    int64_t      m_hostId;     //发起人 userid
    std::wstring m_hostName;   //发起人显示名；不知道时为空
    bool         m_locked;     //加入是否需要密码（第 12 期），群聊横幅据此标出「需要密码」
    bool         m_forced;     //是否强制模式（第 13 期），群聊横幅据此标出「强制观看」、加入前先确认
    std::wstring m_title;      //共享标题（第 15 期），群聊横幅据此写「某某 正在共享：标题」；老服务端不带时为空

    ScreenShareActiveInfo()
        : m_hostId(0)
        , m_locked(false)
        , m_forced(false)
    {
    }
};

//按会话记录的屏幕共享状态
class ScreenShareStatusCache
{
public:
    /**
     *  记下某个会话的最新状态。
     *  @param ctype    会话类型：conversation_single / conversation_group
     *  @param cid      从本人角度看的会话 id：单聊为对方 userid，群聊为群 id
     *  @param roomId   进行中的房间号；active 为 false 时忽略
     *  @param hostId   发起人 userid；active 为 false 时忽略
     *  @param hostName 发起人显示名；为空且房间号与已记录的相同时沿用已记录的名字（查询应答不带名字）
     *  @param active   该会话现在是否有进行中的共享
     *  @param locked   加入是否需要密码（第 12 期）；active 为 false 时忽略。开始推送与查询应答都带这一项，每次直接覆盖
     *  @param forced   是否强制模式（第 13 期）；同上
     *  @param title    共享标题（第 15 期）；为空且房间号与已记录的相同时沿用已记录的标题（老服务端的查询应答不带标题）
     */
    void Update(int ctype, int64_t cid, const std::string& roomId, int64_t hostId, const std::wstring& hostName,
                bool active, bool locked = false, bool forced = false, const std::wstring& title = std::wstring());

    /**
     *  某张卡片所指的那一场共享的状态。
     *  @param roomId 卡片上的房间号；为空时返回 kScreenShareRoomUnknown
     *  @return 取值见 enum ScreenShareRoomState：没有记录过该会话为「还不知道」，该会话进行中的正是这一场为「正在共享」，
     *          其余为「已结束」
     */
    int RoomState(int ctype, int64_t cid, const std::string& roomId) const;

    /**
     *  取某个会话进行中的共享。
     *  @param out [出参] 进行中的共享；返回 false 时不改动
     *  @return 已知该会话有进行中的共享返回 true；没有或还不知道返回 false
     */
    bool GetActive(int ctype, int64_t cid, ScreenShareActiveInfo& out) const;

    /**
     *  收到「共享标题与描述变了」推送（1734，第 15 期；2026-10-08 起移交主持人后也推，带新主持人）后更新某一场共享。
     *  只在缓存记着该会话进行中的正是这一场（房间号相同）时更新，其余情况不改动、也不新建记录：
     *    - title 非空时换成新标题，为空时沿用；
     *    - hostId 大于 0 时换成新主持人，名字用 hostName（为空时留空，由聊天窗按 userid 取名字）；为 0 时主持人与名字沿用；
     *    - 是否需要密码、是否强制模式沿用。
     *  @param ctype    会话类型：conversation_single / conversation_group
     *  @param cid      从本人角度看的会话 id
     *  @param roomId   推送里的房间号
     *  @param title    推送里的新标题；可为空
     *  @param hostId   推送里的新主持人 userid；推送不带时为 0
     *  @param hostName 推送里的新主持人显示名；可为空
     *  @return 更新了记录返回 true（调用方据此刷新群聊横幅）；没有这一场、或 title 为空且 hostId 为 0 时返回 false
     */
    bool ApplyInfo(int ctype, int64_t cid, const std::string& roomId, const std::wstring& title, int64_t hostId,
                   const std::wstring& hostName);

    //清空全部记录（登出时调用），此后所有会话都是「还不知道」
    void Clear();

private:
    //一个会话的记录
    struct Entry
    {
        bool                  m_active;   //是否有进行中的共享
        ScreenShareActiveInfo m_info;     //进行中的共享；m_active 为 false 时无意义

        Entry()
            : m_active(false)
        {
        }
    };

    typedef std::pair<int, int64_t> ConversationKey;   //（会话类型，会话 id）

    std::map<ConversationKey, Entry> m_entries;   //已收到过状态的会话，登出时清空
};

} // namespace balloonscreenshare
