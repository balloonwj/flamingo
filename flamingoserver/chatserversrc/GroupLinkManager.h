/**
 *  加群链接存储管理器，GroupLinkManager.h（docs/邀请入群方案.md §12）。
 *
 *  负责 t_group_link：生成（随机码撞上已有的就换一个重试）、按随机码查、列出本群可用的链接、作废。权限与有效期的
 *  判断在会话层（ChatSession）与 GroupLinkProtocol，本类只管存取。群解散时由 GroupManager::dismissGroup 一并删除。
 *
 *  每次操作单独建立一条数据库连接，不在内存缓存。
 *
 *  典型用法：
 *      GroupLinkRecord rec;
 *      rec.groupid = gid; rec.creator = uid; rec.enterpriseid = eid; rec.expiretime = ...;
 *      Singleton<GroupLinkManager>::Instance().create(rec, nowMs);   //成功后 rec.id 与 rec.code 已填好
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "GroupLinkProtocol.h"   // GroupLinkRecord

//加群链接存储管理器；全局单例，经 Singleton<GroupLinkManager> 访问。无需 init()。
class GroupLinkManager final
{
public:
    GroupLinkManager();
    ~GroupLinkManager();

    GroupLinkManager(const GroupLinkManager& rhs) = delete;
    GroupLinkManager& operator=(const GroupLinkManager& rhs) = delete;

    /**
     *  生成一条链接：随机码由本函数生成（撞上已有的码就重试，最多若干次），写入后把 id、code、createtime 回填进 rec。
     *  @param rec   [入出参] groupid、creator、enterpriseid、expiretime 须已填好
     *  @param nowMs 生成时间，Unix 毫秒
     *  @return 写入成功返回 true
     */
    bool create(GroupLinkRecord& rec, int64_t nowMs);

    /**
     *  按随机码查一条链接（不论是否可用）。
     *  @return 找到返回 true
     */
    bool getByCode(const std::string& code, GroupLinkRecord& out);

    /**
     *  按 ID 查一条链接（不论是否可用）。
     *  @return 找到返回 true
     */
    bool getById(int64_t id, GroupLinkRecord& out);

    /**
     *  列出本群此刻可用的链接（没作废且没过期），按生成时间倒序。
     *  @param creator 只要这个人生成的；0 表示不限
     */
    bool listUsable(int64_t groupid, int64_t creator, int64_t nowMs, std::vector<GroupLinkRecord>& out);

    /**
     *  作废一条链接（只作废这个群的那一条）。
     *  @return 写入成功返回 true
     */
    bool revoke(int64_t groupid, int64_t id);
};
