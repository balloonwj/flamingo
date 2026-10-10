/**
 *  GroupFollowModel.cpp
 *
 *  群关注名单缓存与群消息摘要前缀的实现。说明见 GroupFollowModel.h。不碰窗口，
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "GroupFollowModel.h"
#include "MsgBoxModel.h"              // AtMeDigestPrefix
#include "../BalloonChatWnd/GroupMentionModel.h"   // IsMentioningMe：普通消息的 content.at
#include "../../Common/I18n/I18n.h"   // TR8：「[关注] 」前缀的多语言查找

namespace balloonmain {

GroupFollowModel::GroupFollowModel()
{
}

void GroupFollowModel::Reset(const std::vector<net::GroupFollowEntry>& entries)
{
    m_follows.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        ApplyGroup(entries[i]);
    }
}

void GroupFollowModel::ApplyGroup(const net::GroupFollowEntry& entry)
{
    if (entry.m_groupid <= 0)
        return;

    //整份覆盖：空名单表示这个群一个都不关注了
    if (entry.m_targets.empty())
    {
        m_follows.erase(entry.m_groupid);
        return;
    }
    m_follows[entry.m_groupid] = std::set<uint32_t>(entry.m_targets.begin(), entry.m_targets.end());
}

void GroupFollowModel::RemoveGroup(int64_t groupid)
{
    m_follows.erase(groupid);
}

void GroupFollowModel::Clear()
{
    m_follows.clear();
}

bool GroupFollowModel::IsFollowed(int64_t groupid, int64_t userid) const
{
    if (userid <= 0)
        return false;

    std::map<int64_t, std::set<uint32_t> >::const_iterator it = m_follows.find(groupid);
    if (it == m_follows.end())
        return false;
    return it->second.count((uint32_t)userid) > 0;
}

std::vector<uint32_t> GroupFollowModel::GetTargets(int64_t groupid) const
{
    std::map<int64_t, std::set<uint32_t> >::const_iterator it = m_follows.find(groupid);
    if (it == m_follows.end())
        return std::vector<uint32_t>();
    return std::vector<uint32_t>(it->second.begin(), it->second.end());
}

FollowToggleResult GroupFollowModel::BuildToggled(int64_t groupid, int64_t userid, bool follow,
                                                  std::vector<uint32_t>& out) const
{
    out.clear();
    std::set<uint32_t> next;
    std::map<int64_t, std::set<uint32_t> >::const_iterator it = m_follows.find(groupid);
    if (it != m_follows.end())
        next = it->second;

    const uint32_t uid = (uint32_t)userid;
    const bool already = next.count(uid) > 0;
    if (already == follow)
        return kFollowToggleUnchanged;

    if (follow)
    {
        //上限与服务端 kGroupFollowMax 一致，超出的请求会被拒绝，这里先拦下给出提示
        if (next.size() >= net::kGroupFollowMax)
            return kFollowToggleFull;
        next.insert(uid);
    }
    else
    {
        next.erase(uid);
    }
    out.assign(next.begin(), next.end());
    return kFollowToggleOk;
}

size_t GroupFollowModel::GroupCount() const
{
    return m_follows.size();
}

std::string FollowDigestPrefix()
{
    return TR8(u8"[关注] ");
}

std::string DecorateGroupDigest(const std::string& digest, bool atMe, bool fromFollowed)
{
    //被 @ 比「关注的人发言」更值得注意，两者同时成立时只标前者，不叠加两个前缀
    if (atMe)
        return AtMeDigestPrefix() + digest;
    if (fromFollowed)
        return FollowDigestPrefix() + digest;
    return digest;
}

bool IsGroupMessageAtMe(const net::MessageNotify& notify, int64_t selfId)
{
    //本人自己发的不提醒自己（帖子卡片的发布者、留言回复补充的发起人同样在这里排除）
    if (static_cast<int64_t>(notify.m_from) == selfId)
        return false;
    //帖子卡片、帖子动态卡片的 @ 不在 content.at 里，另看卡片带的 mentions 与 mentionall
    return balloonchatwnd::IsMentioningMe(notify.m_ats, notify.m_atAll, selfId)
           || net::MessageProtocolClient::threadCardMentions(notify, selfId);
}

} // namespace balloonmain
