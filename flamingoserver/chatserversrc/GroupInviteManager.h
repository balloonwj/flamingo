/**
 *  入群邀请表 t_group_invite 的读写（「邀请入群需被邀请人同意」开启时用，docs/邀请入群方案.md §5.1、§5.4）。
 *  全局单例，经 Singleton<GroupInviteManager> 访问；每次操作各取一条数据库连接，可在任意线程调用。
 *
 *  典型用法：
 *      GroupInviteManager& mgr = Singleton<GroupInviteManager>::Instance();
 *      int64_t id = mgr.addInvite(rec);            //发卡片之前先落一行，拿到邀请 id 写进卡片
 *      mgr.setMsgId(id, msgid);                    //卡片消息落库后记下它的 msgid
 *      mgr.updateState(id, group_invite_joined, nowMs);
 *
 *  单元测试见 tests/test_groupinvite.cpp（Suite GV）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include "GroupInviteProtocol.h"   //GroupInviteRecord

/**
 *  GroupInviteManager —— t_group_invite 的读写。只做存取，不做任何业务判断（判定在 GroupInviteProtocol）。
 */
class GroupInviteManager final
{
public:
    GroupInviteManager();
    ~GroupInviteManager();

    GroupInviteManager(const GroupInviteManager&) = delete;
    GroupInviteManager& operator=(const GroupInviteManager&) = delete;

    /**
     *  新增一条入群邀请。
     *  @param rec 邀请；m_id 与 m_msgid 不用填（m_id 由数据库分配，m_msgid 在卡片落库后由 setMsgId 补上）
     *  @return 新邀请的 id；写库失败返回 0
     */
    int64_t addInvite(const GroupInviteRecord& rec);

    /**
     *  按 id 读一条入群邀请。
     *  @param id  邀请 id
     *  @param out [出参] 邀请；返回 false 时不修改
     *  @return 邀请存在且读取成功返回 true
     */
    bool getInvite(int64_t id, GroupInviteRecord& out);

    /**
     *  记下卡片消息的 msgid（卡片落库之后调用；状态变化推送里要带它，客户端据此找到卡片）。
     *  @param id    邀请 id
     *  @param msgid 卡片消息的 msgid
     *  @return 写库成功返回 true
     */
    bool setMsgId(int64_t id, int64_t msgid);

    /**
     *  改一条邀请的落库状态。
     *  @param id         邀请 id
     *  @param state      新状态，取值见 group_invite_state 的 0 ~ 3（4、5 不落库）
     *  @param handleTime 状态变化的时间，Unix 毫秒
     *  @return 写库成功返回 true
     */
    bool updateState(int64_t id, int state, int64_t handleTime);
};
