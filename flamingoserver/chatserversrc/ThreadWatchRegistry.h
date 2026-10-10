/**
 *  帖子群「正在查看」登记表，ThreadWatchRegistry.h
 *
 *  留言、回复与互动变化频繁，2720 推送只发给登记了「正在查看」该群的连接（docs/帖子群方案.md 6.1 的 2713），
 *  避免每次都按「成员数 × 在线会话数」推给全群。客户端打开帖子群窗口时登记、关闭时撤销；连接断开、退出登录、
 *  本人退群或被移出、群被解散时由服务端撤销。
 *
 *  纯内存，不触及网络与数据库，全部方法线程安全，便于单元测试（tests/test_threadgroup.cpp）。
 *
 *  典型用法：
 *      ThreadWatchRegistry& reg = Singleton<ThreadWatchRegistry>::Instance();
 *      reg.watch(groupid, connKey, userid);
 *      std::vector<uint64_t> conns = reg.watchers(groupid);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <vector>

/**
 *  帖子群「正在查看」登记表。chatserver 内经 Singleton<ThreadWatchRegistry>::Instance() 使用。
 *  连接标识与密聊、P2P 相同，取 ChatSession 对象的地址。
 */
class ThreadWatchRegistry
{
public:
    /**
     *  登记一条连接正在查看某个帖子群；重复登记无副作用。
     *  @param groupid 帖子群
     *  @param conn    连接标识
     *  @param userId  连接所属用户（退群时按用户撤销）
     */
    void watch(int64_t groupid, uint64_t conn, int64_t userId);

    /**
     *  撤销一条连接对某个帖子群的登记；没有登记时无副作用。
     */
    void unwatch(int64_t groupid, uint64_t conn);

    /**
     *  撤销一条连接的全部登记（连接断开、退出登录）。
     *  @return 撤销的登记数
     */
    size_t removeConn(uint64_t conn);

    /**
     *  撤销某些用户对某个帖子群的全部登记（退群、被移出）。
     *  @return 撤销的登记数
     */
    size_t removeUsers(int64_t groupid, const std::vector<int64_t>& userIds);

    /**
     *  撤销某个帖子群的全部登记（群被解散）。
     */
    void removeGroup(int64_t groupid);

    /**
     *  取正在查看某个帖子群的全部连接。
     */
    std::vector<uint64_t> watchers(int64_t groupid) const;

private:
    typedef std::map<uint64_t, int64_t> ConnUserMap;   //连接标识 → 所属用户

    mutable std::mutex                m_mutex;    //保护 m_groups
    std::map<int64_t, ConnUserMap>    m_groups;   //帖子群 → 正在查看它的连接
};
