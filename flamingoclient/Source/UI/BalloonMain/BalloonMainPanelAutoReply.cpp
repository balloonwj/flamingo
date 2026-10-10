/**
 *  主面板里与「离开 / 忙碌时自动回复」相关的实现：收到单聊消息时判断要不要回复并发出，
 *  收到回执后把自动回复按本人发出的消息入库、显示。
 *
 *  这里的函数都是 CBalloonMainPanel 的成员，与 BalloonMainPanel.cpp 同属一个类，只是单独成文：
 *  自动回复是一块相对独立的功能，放在一起便于阅读，也避免主面板的大文件继续膨胀。判定规则在
 *  AutoReplyPolicy.h，协议约定见 docs/协议设计-v2.md 10.3 节的「自动回复」。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <objbase.h>                         // CoCreateGuid（生成 clientmsgid）
#include <stdio.h>                           // sprintf_s

#include "../../App/FlamingoClient.h"        // CFlamingoClient::m_UserMgr / m_UserConfig
#include "../../Common/Log/IULog.h"          // LOG_INFO / LOG_WARNING / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"    // 回复文字由 Unicode 转 UTF-8
#include "../../net/IUSocket.h"              // CIUSocket::SendAutoReplyMessage
#include "../../net/MessageProtocolClient.h" // net::MessageProtocolClient::buildAutoReplyEcho
#include "AutoReplyPolicy.h"
#include "AutoReplySync.h"                   // ShouldClientAutoReply：企业打开机器人时由服务端代答

namespace {

// clientmsgid 缓冲区长度（字符数）：32 位十六进制 GUID 加结尾 0，留有余量
const size_t kAutoReplyClientMsgIdBufLen = 40;

//生成一条自动回复的 clientmsgid（GUID 的十六进制串，纯 ASCII）。写法与聊天窗、转发生成 clientmsgid
//的方式相同；极少数取不到 GUID 时用开机以来的毫秒数兜底，前缀 AR- 便于在日志里认出来
std::string GenAutoReplyClientMsgId()
{
    GUID guid = { 0 };
    char sz[kAutoReplyClientMsgIdBufLen] = { 0 };
    if (::CoCreateGuid(&guid) == S_OK)
    {
        sprintf_s(sz, sizeof(sz),
                  "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        sprintf_s(sz, sizeof(sz), "AR-%llu", (unsigned long long)::GetTickCount64());
    }
    return std::string(sz);
}

//这些「不回复」的原因每条消息都可能出现（在线时收到的每一条、群消息、本人发出的消息），
//记日志只会刷屏；其余原因只在本人处于离开或忙碌时才会出现，值得记一条
bool IsRoutineSkip(balloonmain::AutoReplyDecision d)
{
    return d == balloonmain::kAutoReplySkipPresence || d == balloonmain::kAutoReplySkipNotSingle
        || d == balloonmain::kAutoReplySkipFromSelf;
}

} // anonymous namespace

void CBalloonMainPanel::TryAutoReply(const net::MessageNotify& notify, bool watching)
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    //企业打开了机器人时，「离开 / 忙碌自动回复」由服务端的预置个人机器人代答，本机不再回复（docs/机器人方案.md §11 Q2）
    if (!balloonmain::ShouldClientAutoReply(m_lpFMGClient->m_UserMgr.m_bBotFeature))
    {
        return;
    }

    //设置每次都现读：在系统设置里改了开关或文字，下一条消息立即按新设置处理
    const CUserConfig& cfg = m_lpFMGClient->m_UserConfig;
    balloonmain::AutoReplySettings settings;
    settings.m_awayEnabled = (cfg.IsEnableAutoReplyAway() != FALSE);
    settings.m_awayText = cfg.GetAutoReplyAwayText();
    settings.m_busyEnabled = (cfg.IsEnableAutoReplyBusy() != FALSE);
    settings.m_busyText = cfg.GetAutoReplyBusyText();

    balloonmain::AutoReplyIncoming incoming;
    incoming.m_ctype = notify.m_ctype;
    incoming.m_from = notify.m_from;
    incoming.m_msgid = notify.m_msgid;
    incoming.m_msgtype = notify.m_msgtype;
    //机器人代答的消息（content.bot）与自动回复一样不再回复，避免互相回复
    incoming.m_isAutoReply = notify.m_autoReply || notify.m_botId > 0;
    incoming.m_watching = watching;

    const int64_t selfId = (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    const int presence = GetPresence();
    std::wstring text;
    const balloonmain::AutoReplyDecision decision =
        m_autoReplyPolicy.Decide(presence, m_connUiState, selfId, settings, incoming, text);
    if (decision != balloonmain::kAutoReplySend)
    {
        if (!IsRoutineSkip(decision))
        {
            LOG_INFO("Auto-reply skipped: from=%lld, msgid=%lld, msgtype=%d, presence=%d, connUiState=%d, "
                     "decision=%d.", (long long)notify.m_from, (long long)notify.m_msgid, notify.m_msgtype,
                     presence, m_connUiState, (int)decision);
        }
        return;
    }

    const std::string clientMsgId = GenAutoReplyClientMsgId();
    PendingAutoReply pending;
    pending.m_peerUserId = notify.m_from;
    pending.m_replyUtf8 = EncodeUtil::UnicodeToUtf8(text);
    m_pendingAutoReplies[clientMsgId] = pending;

    CIUSocket::GetInstance().SendAutoReplyMessage(pending.m_peerUserId, clientMsgId, pending.m_replyUtf8);
    LOG_INFO("Auto-reply sent: to=%lld, in reply to msgid=%lld, presence=%d, clientmsgid=%s, pending=%u.",
             (long long)pending.m_peerUserId, (long long)notify.m_msgid, presence, clientMsgId.c_str(),
             (unsigned)m_pendingAutoReplies.size());
}

bool CBalloonMainPanel::OnAutoReplyAck(const net::SendMessageResponse& resp)
{
    std::map<std::string, PendingAutoReply>::iterator it = m_pendingAutoReplies.find(resp.m_clientmsgid);
    if (it == m_pendingAutoReplies.end())
    {
        return false;
    }
    const PendingAutoReply pending = it->second;
    m_pendingAutoReplies.erase(it);

    //失败（命中敏感词、对方不存在等）只记日志：这条回复不是用户亲手发的，弹框只会让人莫名其妙
    if (resp.m_code != 0)
    {
        LOG_WARNING("Auto-reply rejected by server: to=%lld, code=%d, msg=%s, clientmsgid=%s.",
                    (long long)pending.m_peerUserId, resp.m_code, resp.m_msg.c_str(),
                    resp.m_clientmsgid.c_str());
        return true;
    }

    const int64_t selfId = (m_lpFMGClient != NULL) ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    net::MessageNotify echo;
    if (!net::MessageProtocolClient::buildAutoReplyEcho(selfId, pending.m_peerUserId, resp.m_clientmsgid,
                                                      resp.m_msgid, resp.m_timestamp, pending.m_replyUtf8, echo))
    {
        LOG_ERROR("Auto-reply delivered but the local echo could not be built: self=%lld, to=%lld, msgid=%lld.",
                  (long long)selfId, (long long)pending.m_peerUserId, (long long)resp.m_msgid);
        return true;
    }

    LOG_INFO("Auto-reply delivered: to=%lld, msgid=%lld, clientmsgid=%s.",
             (long long)pending.m_peerUserId, (long long)resp.m_msgid, resp.m_clientmsgid.c_str());
    //与转发到单聊成功后的处理相同：交给收消息的入口落本地库、显示到已打开的聊天窗、刷新会话列表。
    //发送方是本人，所以不计未读、不闪托盘，也不会再触发自动回复
    OnRecvChatMessage(echo);
    return true;
}
