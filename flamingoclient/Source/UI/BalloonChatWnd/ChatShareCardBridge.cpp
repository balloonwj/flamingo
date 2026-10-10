/**
 *  ChatShareCardBridge.cpp —— 聊天窗与文件共享宿主之间关于共享卡片的衔接，见 ChatShareCardBridge.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ChatShareCardBridge.h"

#include <vector>

#include "ChatMessageList.h"
#include "../../net/MessageProtocolClient.h"           // parseShareCardContent
#include "../../net/Protocol.h"                        // share_state_unavailable / share_unavailable_feature_disabled
#include "../../Feature/FileShare/FileShareHost.h"     // 模块是否已加载、DLL 手里的最新状态

namespace balloonchatwnd {

ShareCardLiveState MakeShareCardLiveState(const std::string& contentJson)
{
    ShareCardLiveState live;
    balloonfileshare::CFileShareHost& host = balloonfileshare::CFileShareHost::GetInstance();
    //本企业的文件共享已被管理员关闭：卡片一律显示「暂不可用：功能已被关闭」、按钮不可点，
    //不写成「当前客户端未启用共享功能」（客户端本身装了模块，是开关关着）
    if (host.IsFeatureDisabled())
    {
        live.m_moduleReady = true;
        live.m_hasState = true;
        live.m_state = share_state_unavailable;
        live.m_reason = share_unavailable_feature_disabled;
        return live;
    }
    live.m_moduleReady = host.IsModuleReady();
    net::ShareCardContent card;
    if (!live.m_moduleReady || !net::MessageProtocolClient::parseShareCardContent(contentJson, card))
        return live;
    int state = 0;
    int reason = 0;
    if (host.GetShareState(card.m_shareId, state, reason))
    {
        live.m_hasState = true;
        live.m_state = state;
        live.m_reason = reason;
    }
    return live;
}

int RefreshShareCardsInList(ChatMessageList* list, long long shareId, int state, int reason)
{
    if (list == NULL || shareId <= 0)
        return 0;
    //本企业的文件共享已关闭：不论推来的是什么状态，一律按「暂不可用：功能已被关闭」显示（见 MakeShareCardLiveState）
    const bool featureDisabled = balloonfileshare::CFileShareHost::GetInstance().IsFeatureDisabled();
    const bool moduleReady = featureDisabled || balloonfileshare::CFileShareHost::GetInstance().IsModuleReady();
    if (featureDisabled)
    {
        state = share_state_unavailable;
        reason = share_unavailable_feature_disabled;
    }
    const std::vector<long long> msgids = list->GetMsgidsOfKind(kMsgFileShare);
    int refreshed = 0;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        ChatMessage msg;
        if (!list->GetMessageByMsgid(msgids[i], msg) || msg.shareId != shareId)
            continue;
        ApplyFileShareState(msg, moduleReady, state, reason);
        list->SetMessageByMsgid(msgids[i], msg);
        ++refreshed;
    }
    return refreshed;
}

} // namespace balloonchatwnd
