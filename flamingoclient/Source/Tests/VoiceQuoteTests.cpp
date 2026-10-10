/**
 *  语音消息的摘要与引用（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节（2026-10-04 用户确认）：
 *    - VM14 语音的文字：会话摘要「[语音]」，引用块「[语音] 8″」，消息记录窗与合并记录查看窗的预览「[语音] 1′05″」
 *           （没有时长时只写「[语音]」）；UTF-8 的时长文字与气泡上的一致（第 4a 步）；
 *    - VM34 引用一条语音：引用块的协议类型为 4、文字「[语音] 8″」、不带 fileid；收到的引用还原为语音种类、
 *           不带缩略图（第 4a 步）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../net/Protocol.h"
#include "../net/MessageProtocolClient.h"
#include "../net/VoiceContent.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatQuoteModel.h"
#include "../UI/BalloonHistory/HistoryView.h"
#include "../Common/Util/EncodeUtil.h"

using namespace balloonchatwnd;

namespace {

//测试用的被引用语音的服务器消息ID与发送人
const long long kVqMsgid = 4401;
const long long kVqSender = 100002;
//测试用的两种时长（秒）：不到 1 分钟与超过 1 分钟
const int kVqShortSec = 8;
const int kVqLongSec = 65;

//一条对方发来的语音
ChatMessage peerVoice(int durationSec)
{
    ChatMessage m;
    m.fromMe = false;
    m.msgid = kVqMsgid;
    m.senderUserId = kVqSender;
    ApplyVoiceMessage(m, "998877", "d41d8cd98f00b204e9800998ecf8427e", durationSec, 25600, "AAECAw==", false);
    return m;
}

} // namespace

TEST_CASE("VM14 语音的文字：会话摘要、引用块、记录窗预览", "[voicemsg]")
{
    //会话列表与消息盒子的摘要只写类型，不带时长
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_voice, std::string()) == u8"[语音]");

    //引用块：类型加时长
    CHECK(BuildQuoteDigest(peerVoice(kVqShortSec)) == CString(L"[语音] 8″"));
    CHECK(BuildQuoteDigest(peerVoice(kVqLongSec)) == CString(L"[语音] 1′05″"));

    //消息记录窗与合并记录查看窗的预览：从本地库的 content 读时长
    CHECK(balloonhistory::PreviewFromContent(message_type_voice,
        "{\"voice\":{\"fileid\":\"998877\",\"md5\":\"abc\",\"duration\":65,\"size\":25600}}") == u8"[语音] 1′05″");
    CHECK(balloonhistory::PreviewFromContent(message_type_voice,
        "{\"voice\":{\"fileid\":\"998877\",\"md5\":\"abc\",\"duration\":8,\"size\":25600}}") == u8"[语音] 8″");
    //没有时长、content 不是 JSON：只写「[语音]」
    CHECK(balloonhistory::PreviewFromContent(message_type_voice, "{\"voice\":{}}") == u8"[语音]");
    CHECK(balloonhistory::PreviewFromContent(message_type_voice, "") == u8"[语音]");
    CHECK(balloonhistory::FullTextFromContent(message_type_voice,
        "{\"voice\":{\"duration\":8}}") == u8"[语音] 8″");

    //UTF-8 的时长文字与气泡上的宽字符版本一致
    const int samples[] = { 1, 8, 59, 60, 65, 120 };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        CHECK(net::VoiceDurationText(samples[i]) == EncodeUtil::UnicodeToUtf8(FormatVoiceDuration(samples[i])));
    }
}

TEST_CASE("VM34 引用一条语音：协议类型 4、文字带时长、不带 fileid，收到后还原为语音种类", "[voicemsg]")
{
    CHECK(MsgKindToProtocolType(kMsgVoice) == message_type_voice);
    CHECK(ProtocolTypeToMsgKind(message_type_voice) == kMsgVoice);

    const net::ReplyQuote q = BuildQuoteFromMessage(peerVoice(kVqShortSec), CString(L"Zhang"));
    REQUIRE(q.m_valid);
    CHECK(q.m_msgid == kVqMsgid);
    CHECK(q.m_from == kVqSender);
    CHECK(q.m_msgtype == message_type_voice);
    CHECK(q.m_digest == u8"[语音] 8″");
    //不带文件定位信息：引用块只画文字，不画缩略图
    CHECK(q.m_fileid.empty());
    CHECK(q.m_md5.empty());

    //收到的引用：还原为语音种类，没有缩略图
    ChatMessage reply;
    FillQuoteForRender(q, reply);
    CHECK(reply.hasQuote);
    CHECK(reply.quoteMsgType == kMsgVoice);
    CHECK(reply.quoteText == CString(L"[语音] 8″"));
    CHECK(reply.quoteImagePath.IsEmpty());
    CHECK(reply.quoteFileId.IsEmpty());
}
