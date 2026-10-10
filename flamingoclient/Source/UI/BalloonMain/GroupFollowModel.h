/**
 *  GroupFollowModel.h
 *
 *  主面板持有的「群 → 本人关注的成员」缓存（提醒模式 5「只提醒 @我和关注的人」用，
 *  docs/消息提醒模式与消息盒子方案.md §12）。名单存服务端：登录时随 cmd_sync_conversations
 *  （1501）的 groupfollows 整体建立，cmd_group_follow_changed_notify（1331）到达时整份覆盖
 *  一个群；本端改名单时先在一份拷贝上算出新名单发 cmd_set_group_follow（1330），以推送为准，
 *  不做乐观更新。
 *
 *  另附群消息摘要前缀的纯函数 DecorateGroupDigest：被 @ 时加「[有人@我] 」，关注的成员发言时
 *  加「[关注] 」，两者同时成立时只加前者。
 *
 *  本模块不碰窗口、不碰网络，Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法（CBalloonMainPanel）：
 *      m_groupFollows.Reset(result.m_groupFollows);                 //1501 到达
 *      m_groupFollows.ApplyGroup(notify);                            //1331 到达
 *      const bool followed = m_groupFollows.IsFollowed(gid, from);   //收到群消息时
 *      std::vector<uint32_t> next;
 *      if (m_groupFollows.BuildToggled(gid, uid, true, next) == kFollowToggleOk)
 *          CIUSocket::GetInstance().SendSetGroupFollow(gid, next);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::GroupFollowEntry / kGroupFollowMax
#include "../../net/MessageProtocolClient.h"         // net::MessageNotify：IsGroupMessageAtMe

namespace balloonmain {

/**
 *  BuildToggled 的结果。
 */
enum FollowToggleResult
{
    kFollowToggleOk        = 0,  // 算出了新名单，可以发 1330
    kFollowToggleUnchanged = 1,  // 目标状态与现状相同（已关注再关注、未关注再取消），不必发请求
    kFollowToggleFull      = 2,  // 本群名单已满（kGroupFollowMax 人），不能再关注新的人
};

/**
 *  GroupFollowModel —— 本人在各群关注的成员。值语义，可拷贝。
 */
class GroupFollowModel
{
public:
    GroupFollowModel();

    /**
     *  按 1501 应答的 groupfollows 整体重建。先清空，再逐个群写入；名单为空的群不保存。
     *    entries：各群的名单（ConversationsResult::m_groupFollows）。
     */
    void Reset(const std::vector<net::GroupFollowEntry>& entries);

    /**
     *  按 1331 推送整份覆盖一个群的名单；名单为空时删除这个群。
     *    entry：推送解析结果；m_groupid 非正时忽略。
     */
    void ApplyGroup(const net::GroupFollowEntry& entry);

    /**
     *  删除一个群的名单（本人退群、被移出、群解散时）。不存在时什么都不做。
     */
    void RemoveGroup(int64_t groupid);

    /**
     *  清空全部名单（登出时）。
     */
    void Clear();

    /**
     *  本人是否在某个群里关注了某位成员。
     *  @return 已关注返回 true；群或成员不在缓存里返回 false
     */
    bool IsFollowed(int64_t groupid, int64_t userid) const;

    /**
     *  取某个群的名单，按 userid 升序。
     *  @return 名单；没有时为空
     */
    std::vector<uint32_t> GetTargets(int64_t groupid) const;

    /**
     *  在现有名单上加入或去掉一个人，算出要发给服务端的完整名单（整份覆盖）。不修改本对象。
     *    groupid：群ID。
     *    userid： 目标成员。
     *    follow： true 加入，false 去掉。
     *    out：    [出参] 新名单（升序）；返回 kFollowToggleOk 以外的值时内容无意义。
     *  @return 见 FollowToggleResult
     */
    FollowToggleResult BuildToggled(int64_t groupid, int64_t userid, bool follow,
                                    std::vector<uint32_t>& out) const;

    /**
     *  有名单的群的个数（日志用）。
     */
    size_t GroupCount() const;

private:
    std::map<int64_t, std::set<uint32_t> > m_follows;  // 群ID → 关注的成员 userid；只存名单不为空的群
};

/**
 *  关注的成员在群里发言时给会话摘要加的前缀：「[关注] 」在当前界面语言下的译文（UTF-8，末尾带一个空格）。
 */
std::string FollowDigestPrefix();

/**
 *  给一条群消息的会话摘要加前缀。纯函数。
 *    digest：      原摘要（UTF-8）。
 *    atMe：        这条消息是否 @ 到本人（含 @所有人）；本人自己发的应传 false。
 *    fromFollowed：发送人是否在本人对这个群的关注名单里；本人自己发的应传 false。
 *  @return 被 @ 时为「[有人@我] 」+ 原摘要；否则关注的成员发言时为「[关注] 」+ 原摘要；都不是时原样返回
 */
std::string DecorateGroupDigest(const std::string& digest, bool atMe, bool fromFollowed);

/**
 *  一条群消息有没有 @ 到本人：决定会话摘要加不加「[有人@我] 」、要不要按被 @ 提醒。纯函数。
 *  本人自己发的不算（在别的设备上发出、同步回来的也一样）。普通消息看 content.at（含 @所有人），只看 uid、不看文字；
 *  帖子卡片与帖子动态卡片看卡片带的 mentions 与 mentionall（docs/帖子群方案.md 4.5、§15.1 第 2 条）。
 *    notify：已解析的群消息。
 *    selfId：本人 userid。
 *  @return @ 到本人返回 true
 */
bool IsGroupMessageAtMe(const net::MessageNotify& notify, int64_t selfId);

} // namespace balloonmain
