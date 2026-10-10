/**
 *  本会话共享列表浮层的纯逻辑 ShareConversationModel 的实现，见 ShareConversationModel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareConversationModel.h"

#include <algorithm>
#include <set>

#include "ShareBrowseModel.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//排序：未失效的在前；同一类里发起时间新的在前，时间相同按 shareid 大的在前
bool NewerFirst(const ShareListEntry& a, const ShareListEntry& b)
{
    const bool aInvalid = (a.m_state == share_state_invalid);
    const bool bInvalid = (b.m_state == share_state_invalid);
    if (aInvalid != bInvalid)
        return !aInvalid;
    if (a.m_createTime != b.m_createTime)
        return a.m_createTime > b.m_createTime;
    return a.m_shareId > b.m_shareId;
}

} // namespace

std::vector<ShareListEntry> ShareConversationModel::Merge(const std::vector<ShareListEntry>& mine,
                                                          const std::vector<ShareListEntry>& sharedMe)
{
    std::vector<ShareListEntry> out;
    std::set<int64_t> seen;
    for (size_t i = 0; i < mine.size(); ++i)
    {
        if (seen.insert(mine[i].m_shareId).second)
            out.push_back(mine[i]);
    }
    for (size_t i = 0; i < sharedMe.size(); ++i)
    {
        if (seen.insert(sharedMe[i].m_shareId).second)
            out.push_back(sharedMe[i]);
    }
    std::stable_sort(out.begin(), out.end(), NewerFirst);
    return out;
}

int ShareConversationModel::RowActions(const ShareListEntry& entry, int64_t selfId, int myRole)
{
    if (entry.m_state == share_state_invalid)
        return 0;
    if (entry.m_ownerId == selfId)
        return share_row_action_edit | share_row_action_cancel;
    int actions = share_row_action_open;
    //群主与群管理员可以取消群里别人的共享
    if (entry.m_ctype == conversation_group && (myRole == kShareRoleAdmin || myRole == kShareRoleOwner))
        actions |= share_row_action_cancel;
    return actions;
}

std::wstring ShareConversationModel::StatusText(int state, int reason)
{
    if (state == share_state_available)
        return TR(L"可用");
    return ShareBrowseModel::StateText(state, reason);
}

} // namespace fileshare
