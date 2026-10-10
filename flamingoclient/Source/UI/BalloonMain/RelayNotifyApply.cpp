/**
 *  RelayNotifyApply.cpp —— 接龙变更推送合并进本地消息库，见 RelayNotifyApply.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-02
 */

// ---- ATL 头文件要求的前置依赖，与 UI/BalloonChatWnd/ChatRelayCard.cpp 相同 ----
// （ChatRelayCard.h 用到 CString，须先引入 atlstr.h）
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "RelayNotifyApply.h"
#include "../BalloonChatWnd/ChatRelayCard.h"     // balloonchatwnd::ApplyRelayNotify
#include "../BalloonHistory/V2MsgQuery.h"        // RelayContentFromRecord / BuildContentJson
#include "../BalloonHistory/V2MsgStore.h"        // CV2MsgStore::QueryRecord / UpdateContent
#include "../../net/CardProtocolClient.h"        // parseRelayCardContent / serializeRelayCardContent
#include "../../net/MessageProtocolClient.h"     // net::MessageNotify（拼 content 列用）
#include "../../net/Protocol.h"                  // message_type_relay

namespace balloonmain {

bool ApplyRelayNotifyToStore(balloonhistory::CV2MsgStore& store, const net::RelayChangedNotify& notify,
                             std::string& outRelayJson, bool& outHit)
{
    outHit = false;

    //一、按主键取出本地那一行。本地没有：本机没同步过这段历史，属正常，下次同步会带回最新内容
    balloonhistory::HistoryRecord row;
    bool found = false;
    if (!store.QueryRecord(notify.m_ctype, notify.m_cid, notify.m_msgid, row, found))
    {
        return false;
    }
    if (!found)
    {
        return true;
    }

    //二、取出卡片当前内容：content 列优先，退回原始JSON
    std::string relayJson;
    net::RelayCardContent card;
    if (!balloonhistory::RelayContentFromRecord(row, relayJson)
        || !net::CardProtocolClient::parseRelayCardContent(relayJson, card))
    {
        return false;
    }

    //三、用推送替换名单与状态；标题、说明、截止时间推送不带，保持原值
    balloonchatwnd::ApplyRelayNotify(card, notify);
    const std::string merged = net::CardProtocolClient::serializeRelayCardContent(card);

    //四、写回 content 列。格式与入库时相同（BuildContentJson 的 relay 子对象），原始JSON不动
    net::MessageNotify forColumn;
    forColumn.m_msgtype      = message_type_relay;
    forColumn.m_relayContent = merged;
    bool hit = false;
    if (!store.UpdateContent(notify.m_ctype, notify.m_cid, notify.m_msgid,
                             balloonhistory::BuildContentJson(forColumn), hit))
    {
        return false;
    }

    outHit = hit;
    outRelayJson = merged;
    return true;
}

} // namespace balloonmain
