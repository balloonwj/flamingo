/**
 *  主面板里与群接龙详情窗相关的实现：接收群聊窗投来的打开请求、按卡片 msgid 管理详情窗、
 *  给详情窗提供名字解析。推送与操作应答转给详情窗的部分在 BalloonMainPanelNetHandlers.cpp 的
 *  OnV2RelayChanged / OnV2RelayOpResult 里。
 *
 *  这里的函数都是 CBalloonMainPanel 的成员，与 BalloonMainPanel.cpp 同属一个类，只是单独成文，
 *  做法与 BalloonMainPanelForward.cpp 相同。整体设计见 docs/群接龙方案.md §3.3、§6.6。
 *
 *  balloonwj@qq.com   2026-10-02
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <memory>

#include "../../App/FlamingoClient.h"            // CFlamingoClient::m_UserMgr（取当前登录用户）
#include "../../Common/Log/IULog.h"              // LOG_INFO / LOG_ERROR
#include "../../net/Protocol.h"                  // message_type_relay
#include "../BalloonHistory/V2MsgQuery.h"        // HistoryRecord / RelayContentFromRecord
#include "../BalloonCard/RelayDetailModel.h"     // ballooncard::RelayDetailRequest

namespace {

/**
 *  接龙详情窗的名字解析：按卡片所在的群经主面板解析（该群的聊天窗开着时用它的成员表，
 *  否则用通讯录）。每个详情窗持有一个，生命周期不长于主面板。
 */
class RelayDetailNameResolver : public balloonchatwnd::IRelayNameResolver
{
public:
    /**
     *  @param panel   主面板；本对象只保存指针，不持有
     *  @param groupId 卡片所在群的 id
     */
    RelayDetailNameResolver(const CBalloonMainPanel* panel, int64_t groupId)
        : m_panel(panel)
        , m_groupId(groupId)
    {
    }

    /**
     *  取某个用户在详情窗名单里显示的名字。
     *  @param userid 用户 userid
     *  @return 显示名；主面板不在时返回空串，由调用方写成「用户<id>」
     */
    CString ResolveName(int64_t userid) const override
    {
        if (m_panel == NULL)
        {
            return CString();
        }
        return m_panel->ResolveRelayMemberName(m_groupId, userid);
    }

private:
    const CBalloonMainPanel* m_panel;     // 主面板，借用不持有
    int64_t                  m_groupId;   // 卡片所在群的 id
};

} // anonymous namespace

CString CBalloonMainPanel::ResolveRelayMemberName(int64_t groupId, int64_t userid) const
{
    //该群的聊天窗开着：按它的成员表解析，群名片优先，与卡片气泡上的名字一致
    std::map<int64_t, CBalloonGroupChatWindow*>::const_iterator it = m_groupChatWindows.find(groupId);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        return it->second->ResolveRelayMemberName(userid);
    }
    return ResolveSenderName(userid);
}

CRelayDetailWindow* CBalloonMainPanel::FindRelayDetailWindow(int64_t msgid)
{
    //已关闭的详情窗对象留着没有用处，趁这次查找清掉
    std::map<int64_t, std::unique_ptr<CRelayDetailWindow> >::iterator it = m_relayDetailWindows.begin();
    while (it != m_relayDetailWindows.end())
    {
        if (!it->second || !it->second->IsWindow())
        {
            it = m_relayDetailWindows.erase(it);
            continue;
        }
        ++it;
    }
    std::map<int64_t, std::unique_ptr<CRelayDetailWindow> >::iterator found = m_relayDetailWindows.find(msgid);
    return (found != m_relayDetailWindows.end()) ? found->second.get() : NULL;
}

LRESULT CBalloonMainPanel::OnOpenRelayDetailMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为群聊窗堆分配的请求，无论成败都在这里释放
    std::unique_ptr<ballooncard::RelayDetailRequest> request((ballooncard::RelayDetailRequest*)lParam);
    if (!request || request->m_msgid <= 0 || request->m_cid <= 0)
    {
        return 0;
    }

    //点的是系统提示行、卡片又不在群聊窗当前视图里：从本地库取卡片内容与发起人
    if (request->m_contentJson.empty() || request->m_creatorId <= 0)
    {
        if (!EnsureMsgStoreOpen())
        {
            LOG_ERROR("open relay detail: local message store not ready, msgid=%lld.", (long long)request->m_msgid);
            return 0;
        }
        balloonhistory::HistoryRecord rec;
        bool found = false;
        if (!m_msgStore.QueryRecord(request->m_ctype, request->m_cid, request->m_msgid, rec, found) || !found)
        {
            LOG_INFO("open relay detail: card not in local store, ctype=%d, cid=%lld, msgid=%lld.",
                     request->m_ctype, (long long)request->m_cid, (long long)request->m_msgid);
            return 0;
        }
        //卡片已撤回：方案 §3.4 约定点击提示行无反应
        if (rec.m_recalled || rec.m_msgtype != message_type_relay)
        {
            LOG_INFO("open relay detail ignored: card recalled or not a relay card, msgid=%lld, recalled=%d, msgtype=%d.",
                     (long long)request->m_msgid, rec.m_recalled ? 1 : 0, rec.m_msgtype);
            return 0;
        }
        if (request->m_contentJson.empty()
            && !balloonhistory::RelayContentFromRecord(rec, request->m_contentJson))
        {
            LOG_ERROR("open relay detail: card content unavailable in local store, msgid=%lld.",
                      (long long)request->m_msgid);
            return 0;
        }
        if (request->m_creatorId <= 0)
        {
            request->m_creatorId = rec.m_fromid;
        }
    }

    const int64_t selfId = (m_lpFMGClient != NULL) ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::unique_ptr<balloonchatwnd::IRelayNameResolver> names(new RelayDetailNameResolver(this, request->m_cid));

    //这张卡片已经有详情窗：换成最新内容并调到前台
    CRelayDetailWindow* existing = FindRelayDetailWindow(request->m_msgid);
    if (existing != NULL)
    {
        existing->Open(*request, selfId, std::move(names));
        return 0;
    }

    std::unique_ptr<CRelayDetailWindow> created(new CRelayDetailWindow());
    if (!created->Open(*request, selfId, std::move(names)))
    {
        return 0;
    }
    m_relayDetailWindows[request->m_msgid] = std::move(created);
    LOG_INFO("relay detail window created by main panel, msgid=%lld, open windows=%u.",
             (long long)request->m_msgid, (unsigned)m_relayDetailWindows.size());
    return 0;
}
