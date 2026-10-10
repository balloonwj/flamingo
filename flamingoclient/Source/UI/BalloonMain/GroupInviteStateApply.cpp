/**
 *  GroupInviteStateApply.cpp —— 入群邀请卡片的状态变化推送写进本地消息库，见 GroupInviteStateApply.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- ATL 头文件要求的前置依赖，与 UI/BalloonChatWnd/ChatRelayCard.cpp 相同 ----
// （ChatGroupInviteCard.h 用到 CString，须先引入 atlstr.h）
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

#include "GroupInviteStateApply.h"
#include "../BalloonChatWnd/ChatGroupInviteCard.h"   // balloonchatwnd::SetGroupInviteContentState
#include "../BalloonHistory/V2MsgQuery.h"            // GroupInviteContentFromRecord / BuildContentJson
#include "../BalloonHistory/V2MsgStore.h"            // CV2MsgStore::QueryRecord / UpdateContent
#include "../../net/MessageProtocolClient.h"         // net::MessageNotify（拼 content 列用）
#include "../../net/Protocol.h"                      // message_type_group_invite、conversation_single

namespace balloonmain {

int64_t GroupInvitePeerId(int64_t inviterId, int64_t inviteeId, int64_t selfId)
{
    if (inviterId <= 0 || inviteeId <= 0 || selfId <= 0)
        return 0;
    if (selfId == inviterId)
        return inviteeId;
    if (selfId == inviteeId)
        return inviterId;
    return 0;
}

bool ApplyGroupInviteStateToStore(balloonhistory::CV2MsgStore& store, int64_t peerId, int64_t msgid, int state,
                                  bool& outHit)
{
    outHit = false;
    if (peerId <= 0 || msgid <= 0)
        return false;

    //一、按主键取出本地那一行。本地没有：本机没同步过这段历史，属正常，下次同步会带回最新状态
    balloonhistory::HistoryRecord row;
    bool found = false;
    if (!store.QueryRecord(conversation_single, peerId, msgid, row, found))
        return false;
    if (!found)
        return true;

    //二、取出卡片当前内容（content 列优先，退回原始JSON），改掉其中的 state
    std::string contentJson;
    if (!balloonhistory::GroupInviteContentFromRecord(row, contentJson))
        return false;
    const std::string updated = balloonchatwnd::SetGroupInviteContentState(contentJson, state);

    //三、写回 content 列。格式与入库时相同（BuildContentJson 的 groupinvite 子对象），原始JSON不动
    net::MessageNotify forColumn;
    forColumn.m_msgtype            = message_type_group_invite;
    forColumn.m_groupInviteContent = updated;
    bool hit = false;
    if (!store.UpdateContent(conversation_single, peerId, msgid, balloonhistory::BuildContentJson(forColumn), hit))
        return false;
    outHit = hit;
    return true;
}

} // namespace balloonmain
