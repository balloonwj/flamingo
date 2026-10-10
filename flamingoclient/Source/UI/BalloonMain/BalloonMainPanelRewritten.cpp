/**
 *  BalloonMainPanelRewritten.cpp
 *
 *  主面板的「按改写序号同步」部分（方案见 docs/消息改写离线同步方案.md）：登录成功（含断线重连成功）后
 *  开始一轮同步，收到 cmd_sync_rewritten_messages（1526）应答时交给控制器覆盖本地库、推进水位、续拉下一页，
 *  以及控制器经 IRewrittenSyncHost 向主面板要的三样服务：发请求、查询会话有没有开着的窗口、刷新这个会话的窗口。
 *  逐页应用与水位的规则在 Feature/RewrittenSync/RewrittenSync.h，可单独测试（标签 [rewrittensync]）。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <memory>

#include "../../Common/Log/IULog.h"                  // LOG_INFO / LOG_WARNING
#include "../../net/IUSocket.h"                      // CIUSocket::RequestSyncRewritten
#include "../../net/MessageProtocolClient.h"         // net::SyncRewrittenResult
#include "../../net/Protocol.h"                      // conversation_single / conversation_group
#include "../BalloonChatWnd/BalloonChatWindow.h"     // CBalloonChatWindow::IsShowingAnyMessage / ReloadLatestMessages
#include "../BalloonChatWnd/BalloonGroupChatWindow.h" // CBalloonGroupChatWindow::IsShowingAnyMessage / ReloadLatestMessages

void CBalloonMainPanel::OnRewrittenSyncLoginSucceeded()
{
    if (!EnsureMsgStoreOpen())
    {
        LOG_WARNING("rewritten sync: local message store not ready, skipped for this login.");
        return;
    }
    m_rewrittenSync.Start(m_msgStore);
}

LRESULT CBalloonMainPanel::OnV2SyncRewritten(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::SyncRewrittenResult，处理后须 delete
    std::unique_ptr<net::SyncRewrittenResult> pResult(reinterpret_cast<net::SyncRewrittenResult*>(lParam));
    if (!pResult)
        return 0;
    if (!EnsureMsgStoreOpen())
    {
        LOG_WARNING("rewritten sync: local message store not ready, page dropped.");
        m_rewrittenSync.Reset();
        return 0;
    }
    m_rewrittenSync.OnPage(m_msgStore, *pResult, static_cast<int64_t>(SelfUserId()));
    return 0;
}

void CBalloonMainPanel::SendRewrittenRequest(int64_t since, int limit)
{
    CIUSocket::GetInstance().RequestSyncRewritten(since, limit);
}

bool CBalloonMainPanel::IsConversationShown(int ctype, int64_t cid)
{
    if (m_historyWindow.IsShowingConversation(ctype, cid))
        return true;
    if (ctype == conversation_single)
    {
        std::map<UINT, CBalloonChatWindow*>::const_iterator it = m_chatWindows.find(static_cast<UINT>(cid));
        return it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow();
    }
    if (ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::const_iterator it = m_groupChatWindows.find(cid);
        return it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow();
    }
    return false;
}

void CBalloonMainPanel::RefreshConversation(int ctype, int64_t cid, const std::vector<int64_t>& msgids)
{
    //消息记录窗正显示这个会话时重查本地库（窗口自己判断，不是这个会话时什么也不做）
    m_historyWindow.RefreshIfConversation(ctype, cid);

    //聊天窗：被改写的消息正显示在列表里才整页重新加载；不在视图里时不打乱滚动位置，以后滚动到那里时从本地库或服务端
    //取到的已是新内容
    bool reloaded = false;
    if (ctype == conversation_single)
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(static_cast<UINT>(cid));
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow()
            && it->second->IsShowingAnyMessage(msgids))
        {
            it->second->ReloadLatestMessages();
            reloaded = true;
        }
    }
    else if (ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow()
            && it->second->IsShowingAnyMessage(msgids))
        {
            it->second->ReloadLatestMessages();
            reloaded = true;
        }
    }
    LOG_INFO("rewritten sync: conversation refreshed, ctype=%d, cid=%lld, msgids=%u, chat window reloaded=%d.", ctype,
             static_cast<long long>(cid), static_cast<unsigned>(msgids.size()), reloaded ? 1 : 0);
}
