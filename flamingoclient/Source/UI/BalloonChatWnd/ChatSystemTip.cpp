/**
 *  ChatSystemTip.cpp —— 系统消息的居中提示行，见 ChatSystemTip.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ChatSystemTip.h"
#include "../BalloonChat/BalloonChatMsgFormat.h"   // BuildRecallTipText（撤回提示的文字，与在线撤回时一致）
#include "../../Common/I18n/I18n.h"                // TR：窗口抖动提示的多语言查找

namespace balloonchatwnd {

namespace {

//UTF-8 → CString（宽字符）
CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return CString();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return CString();
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.GetBuffer(n), n);
    out.ReleaseBuffer(n);
    return out;
}

} // namespace

ChatMessage MakeSystemTipRow(const net::MessageNotify& m)
{
    const std::string text = m.m_text.empty()
                           ? net::MessageProtocolClient::digestForMsgType(m.m_msgtype, m.m_text)
                           : m.m_text;
    ChatMessage tip = MakeTextMessage(false, Utf8ToWide(text));
    tip.kind        = kMsgTime;
    tip.msgid       = m.m_msgid;
    tip.timestampMs = m.m_timestamp;
    //接龙的系统提示（参与、修改、撤回、结束）带着卡片 msgid，提示行据此可以点击打开详情窗
    tip.relayCardMsgid = (long long)m.m_cardMsgId;
    return tip;
}

int64_t RecallOperatorOf(const net::MessageNotify& m)
{
    if (m.m_recalledBy > 0)
        return m.m_recalledBy;
    return m.m_from;
}

ChatMessage MakeRecalledTipRow(const net::MessageNotify& m, bool recalledByMe, const CString& recallerName)
{
    ChatMessage tip = MakeTextMessage(false, balloonchat::BuildRecallTipText(recalledByMe, recallerName));
    tip.kind        = kMsgTime;
    tip.msgid       = m.m_msgid;
    tip.timestampMs = m.m_timestamp;
    return tip;
}

CString ShakeTipText(bool fromMe)
{
    return fromMe ? CString(TR(L"你向对方发送了窗口抖动")) : CString(TR(L"对方向你发送了窗口抖动"));
}

ChatMessage MakeShakeTipRow(const net::MessageNotify& m, bool fromMe)
{
    ChatMessage tip = MakeTextMessage(false, ShakeTipText(fromMe));
    tip.kind        = kMsgTime;
    tip.msgid       = m.m_msgid;
    tip.timestampMs = m.m_timestamp;
    return tip;
}

} // namespace balloonchatwnd
