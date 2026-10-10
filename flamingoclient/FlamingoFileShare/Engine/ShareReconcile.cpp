/**
 *  分享者上线比对 ShareReconcile 的实现，见 ShareReconcile.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareReconcile.h"

#include <map>
#include <set>

#include "../../Source/net/Protocol.h"   //share_unavailable_reason

namespace fileshare {

ShareReconcilePlan ShareReconcile::Plan(const std::vector<ShareListEntry>& serverMine,
                                        const std::vector<ShareLocalRecord>& local)
{
    ShareReconcilePlan plan;

    //本机记录按 shareid 建索引；还没有 shareid 的记录不参与比对
    std::map<int64_t, size_t> localIndex;
    for (size_t i = 0; i < local.size(); ++i)
    {
        if (local[i].m_shareId != 0 && localIndex.find(local[i].m_shareId) == localIndex.end())
            localIndex[local[i].m_shareId] = i;
    }

    std::set<int64_t> seenOnServer;
    for (size_t i = 0; i < serverMine.size(); ++i)
    {
        const ShareListEntry& entry = serverMine[i];
        if (!seenOnServer.insert(entry.m_shareId).second)
            continue;

        //绑定别的设备：不报告；本机即便有记录也保留
        if (!entry.m_onThisDevice)
        {
            plan.m_otherDevice.push_back(entry.m_shareId);
            continue;
        }

        std::map<int64_t, size_t>::const_iterator it = localIndex.find(entry.m_shareId);
        if (it == localIndex.end())
            plan.m_missingLocal.push_back(entry.m_shareId);
        else
            plan.m_report.push_back(local[it->second]);
    }

    //本机有、服务端已没有：按本机记录的顺序列出
    for (size_t i = 0; i < local.size(); ++i)
    {
        const int64_t shareId = local[i].m_shareId;
        if (shareId == 0 || seenOnServer.find(shareId) != seenOnServer.end())
            continue;
        //同一个 shareid 在本机重复出现时只删一次
        bool listed = false;
        for (size_t j = 0; j < plan.m_removeLocal.size(); ++j)
        {
            if (plan.m_removeLocal[j] == shareId)
            {
                listed = true;
                break;
            }
        }
        if (!listed)
            plan.m_removeLocal.push_back(shareId);
    }
    return plan;
}

ShareServeItem ShareReconcile::MakeServeItem(int64_t shareId, bool rootAvailable)
{
    ShareServeItem item;
    item.m_shareId = shareId;
    item.m_available = rootAvailable;
    item.m_reason = rootAvailable ? 0 : share_unavailable_root_missing;
    return item;
}

} // namespace fileshare
