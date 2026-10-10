/**
 *  GroupCallStatusCache.h
 *
 *  exe 侧记录各群的群通话状态（docs/群音视频通话方案.md 6.3）：FlamingoMedia.dll 经 IMediaHost::OnGroupCallStatus
 *  回报查询结果与群内状态推送（开始、人数变化、结束），本缓存按群记下最近一次的状态，群聊窗据此显示横幅、决定通话卡片
 *  的状态文字与「加入」按钮。只记本次登录期间收到的状态，登出时清空。
 *
 *  纯逻辑，不依赖窗口与网络，单元测试见 Source/Tests/GroupCallStatusCacheTests.cpp（标签 [groupcall]）。
 *  所有方法只在 UI 线程调用，内部不加锁。
 *
 *  典型用法：
 *      cache.Update(groupId, roomId, true, media, count, initiatorId, startTime);
 *      int state = cache.RoomState(groupId, cardRoomId);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>

namespace balloongroupcall {

//群通话卡片所指的那一场在本客户端看来的状态
enum GroupCallRoomState
{
    kGroupCallRoomUnknown = 0,   //还不知道：本次登录还没收到这个群的查询应答或状态推送
    kGroupCallRoomActive  = 1,   //进行中：这个群当前进行中的群通话就是这一场
    kGroupCallRoomEnded   = 2,   //已结束：这个群当前没有群通话，或者进行中的是另一场
};

//某群进行中的群通话
struct GroupCallActiveInfo
{
    std::string m_roomId;        //房间号
    int         m_media;         //媒体类型：1 群语音、2 群视频
    int         m_count;         //已加入人数
    int64_t     m_initiatorId;   //发起人 userid
    int64_t     m_startTime;     //发起时间，Unix 毫秒

    GroupCallActiveInfo()
        : m_media(0)
        , m_count(0)
        , m_initiatorId(0)
        , m_startTime(0)
    {
    }
};

//各群的群通话状态缓存
class GroupCallStatusCache
{
public:
    /**
     *  记下某群的最新状态。
     *  @param groupId     群 id；不大于 0 时忽略
     *  @param roomId      房间号；active 为 false 时忽略
     *  @param active      是否有进行中的群通话
     *  @param media       媒体类型
     *  @param count       已加入人数
     *  @param initiatorId 发起人
     *  @param startTime   发起时间，Unix 毫秒
     */
    void Update(int64_t groupId, const std::string& roomId, bool active, int media, int count, int64_t initiatorId,
                int64_t startTime);

    /**
     *  某张卡片所指那一场的状态。
     *  @param groupId 卡片所在的群
     *  @param roomId  卡片 content 里的房间号
     *  @return 取值见 enum GroupCallRoomState
     */
    int RoomState(int64_t groupId, const std::string& roomId) const;

    /**
     *  取某群进行中的群通话（横幅用）。
     *  @return 已知有进行中的群通话返回 true
     */
    bool GetActive(int64_t groupId, GroupCallActiveInfo& out) const;

    //清空（登出时）
    void Clear();

private:
    //一个群的状态
    struct Entry
    {
        bool                m_active;   //是否有进行中的群通话
        GroupCallActiveInfo m_info;     //进行中的那一场；m_active 为 false 时无意义

        Entry()
            : m_active(false)
        {
        }
    };

    std::map<int64_t, Entry> m_entries;   //群 id → 状态；只含收到过状态的群
};

} // namespace balloongroupcall
