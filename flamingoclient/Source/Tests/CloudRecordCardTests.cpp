/**
 *  云端录像卡片的单元测试（Suite CRC，标签 [cloudreccard]），CloudRecordCardTests.cpp
 *
 *  云端录像卡片（msgtype=24，docs/屏幕共享与录制方案.md 12.6.5）：
 *    - CRC1 卡片 content 解析：服务端的写法（id 为字符串）、id 为整数、缺 recordid、不是 JSON、负数按 0；整条消息解析后
 *           content 原文保存在 m_cloudRecordContent，不会被当成文字消息；
 *    - CRC2 显示文字：副标题（本人 / 别人 / 取不到名字）、状态行（视频、只有声音、缺时长、缺大小）；
 *    - CRC3 套用到气泡：kind 与标志位、录像 id、标题与摘要、「播放」按钮；内容不可用时不可点；没有标题时写「云端录像」；
 *           屏幕共享状态的刷新（ApplyScreenShareState / ApplyScreenShareInfo）不改动这张卡片；
 *    - CRC4 三处摘要一致（实时消息、会话列表 digestarg、消息记录窗预览），本地库的搜索文字是标题；
 *           聊天窗与消息记录窗的菜单没有转发、Pin、撤回。
 *  ChatBubble 把这种卡片画成屏幕共享卡片的版式，几何与菜单规则由 ScreenShareCardTests 覆盖，这里只核对 kind 与标志位。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatCloudRecordCard.h"
#include "../UI/BalloonChatWnd/ChatScreenShareCard.h"   // ApplyScreenShareState / ApplyScreenShareInfo：不应改动云端录像卡片
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonHistory/HistoryView.h"         // PreviewFromContent：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"      // 消息记录窗的行菜单与转发判定
#include "../UI/BalloonHistory/V2MsgQuery.h"          // RecordFromNotify：入库的 content 列与搜索文字
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../net/MessageProtocolClient.h"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"

using namespace balloonchatwnd;

namespace {

//单聊的对方（开启者）与卡片消息
const long long kCrcOwnerId  = 5127;
const long long kCrcPeerId   = 5308;
const long long kCrcMsgId    = 9201;
const long long kCrcRecordId = 42;
const long long kCrcSendTime = 1790000000000LL;

//服务端 CloudRecordService 发卡片时写的 content（jsoncpp 紧凑写法，键按字母序）：时长 83 秒、15.6 MB 的视频录像
const char* const kCrcCard =
    u8"{\"audioonly\":0,\"bytes\":16357785,\"durationms\":83000,\"owner\":\"5127\",\"recordid\":\"42\","
    u8"\"title\":\"周会\"}";

//只有声音的录音：时长 1 小时 2 分 5 秒、1.2 MB
const char* const kCrcAudioCard =
    u8"{\"audioonly\":1,\"bytes\":1258291,\"durationms\":3725000,\"owner\":\"5127\",\"recordid\":43,"
    u8"\"title\":\"访谈\"}";

ChatMessage CrcBubble(bool fromMe)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = kCrcMsgId;
    return msg;
}

//JSON 文本 → JSON 值
Json::Value CrcParse(const std::string& text)
{
    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    reader->parse(text.c_str(), text.c_str() + text.size(), &v, &errs);
    return v;
}

//照服务端推送的格式拼一条单聊的云端录像卡片消息（发送者是开启者）
std::string CrcMessageJson(const char* content)
{
    Json::Value msg;
    msg["msgid"] = std::to_string(kCrcMsgId);
    msg["clientmsgid"] = "cloudrec-card-" + std::to_string(kCrcMsgId);
    msg["ctype"] = conversation_single;
    msg["cid"] = std::to_string(kCrcOwnerId);
    msg["from"] = std::to_string(kCrcOwnerId);
    msg["to"] = std::to_string(kCrcPeerId);
    msg["msgtype"] = message_type_cloudrecord;
    msg["content"] = CrcParse(content);
    msg["timestamp"] = static_cast<Json::Int64>(kCrcSendTime);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, msg);
}

bool CrcHasMenuId(const std::vector<ChatMessageMenuEntry>& model, int id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("CRC1 云端录像卡片 content 解析：字符串与整数 id、缺字段、不是 JSON", "[cloudreccard]")
{
    CloudRecordCardInfo info;
    REQUIRE(ParseCloudRecordCard(kCrcCard, info));
    CHECK(info.m_recordId == kCrcRecordId);
    CHECK(info.m_owner == kCrcOwnerId);
    CHECK(info.m_title == L"周会");
    CHECK(info.m_durationMs == 83000);
    CHECK(info.m_bytes == 16357785);
    CHECK_FALSE(info.m_audioOnly);

    //recordid 写成整数也认
    CloudRecordCardInfo audio;
    REQUIRE(ParseCloudRecordCard(kCrcAudioCard, audio));
    CHECK(audio.m_recordId == 43);
    CHECK(audio.m_audioOnly);

    //只有 id 也能解析；负数的时长与大小按 0
    CloudRecordCardInfo minimal;
    REQUIRE(ParseCloudRecordCard(u8"{\"recordid\":\"7\",\"durationms\":-5,\"bytes\":-1}", minimal));
    CHECK(minimal.m_title.IsEmpty());
    CHECK(minimal.m_durationMs == 0);
    CHECK(minimal.m_bytes == 0);

    CloudRecordCardInfo bad;
    CHECK_FALSE(ParseCloudRecordCard(u8"{\"title\":\"x\"}", bad));
    CHECK_FALSE(ParseCloudRecordCard(u8"{\"recordid\":\"0\"}", bad));
    CHECK_FALSE(ParseCloudRecordCard("not json", bad));
    CHECK_FALSE(ParseCloudRecordCard("", bad));

    //整条消息：content 原文保存在 m_cloudRecordContent，正文为空
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CrcMessageJson(kCrcCard), notify));
    CHECK(notify.m_msgtype == message_type_cloudrecord);
    CHECK_FALSE(notify.m_cloudRecordContent.empty());
    CHECK(notify.m_text.empty());
    CloudRecordCardInfo fromNotify;
    REQUIRE(ParseCloudRecordCard(notify.m_cloudRecordContent, fromNotify));
    CHECK(fromNotify.m_recordId == kCrcRecordId);
}

TEST_CASE("CRC2 云端录像卡片的显示文字：副标题与状态行", "[cloudreccard]")
{
    CHECK(CloudRecordCardSubtitle(L"张三", false) == L"张三分享的云端录像");
    CHECK(CloudRecordCardSubtitle(L"张三", true) == L"我分享的云端录像");
    CHECK(CloudRecordCardSubtitle(L"", true) == L"我分享的云端录像");
    CHECK(CloudRecordCardSubtitle(L"", false) == L"云端录像");

    CloudRecordCardInfo info;
    REQUIRE(ParseCloudRecordCard(kCrcCard, info));
    CHECK(CloudRecordCardStatusText(info) == L"时长 01:23 · 15.6 MB");

    CloudRecordCardInfo audio;
    REQUIRE(ParseCloudRecordCard(kCrcAudioCard, audio));
    CHECK(CloudRecordCardStatusText(audio) == L"录音 · 时长 1:02:05 · 1.2 MB");

    //缺时长只写大小；缺大小只写时长；都缺时视频为空串、录音只写「录音」
    CloudRecordCardInfo part = info;
    part.m_durationMs = 0;
    CHECK(CloudRecordCardStatusText(part) == L"15.6 MB");
    part = info;
    part.m_bytes = 0;
    CHECK(CloudRecordCardStatusText(part) == L"时长 01:23");
    part.m_durationMs = 0;
    CHECK(CloudRecordCardStatusText(part).IsEmpty());
    part.m_audioOnly = true;
    CHECK(CloudRecordCardStatusText(part) == L"录音");
}

TEST_CASE("CRC3 云端录像卡片套用到气泡：标志位、按钮、内容不可用、不受共享状态刷新影响", "[cloudreccard]")
{
    ChatMessage msg = CrcBubble(false);
    ApplyCloudRecordCard(msg, kCrcCard, L"张三");
    CHECK(msg.kind == kMsgScreenShare);
    CHECK(msg.screenShareIsCloudRecord);
    CHECK_FALSE(msg.screenShareIsMemo);
    CHECK_FALSE(msg.screenShareIsAnnouncement);
    CHECK_FALSE(msg.screenShareIsGroupCall);
    CHECK_FALSE(msg.screenShareIsCallCard);
    CHECK_FALSE(msg.cloudRecordAudioOnly);
    CHECK(msg.infoCardRefId == kCrcRecordId);
    CHECK(msg.screenShareRoomId.empty());
    CHECK(msg.screenShareTitle == L"周会");
    CHECK(msg.screenShareSubtitle == L"张三分享的云端录像");
    CHECK(msg.screenShareStatus == L"时长 01:23 · 15.6 MB");
    CHECK(msg.screenShareDesc.IsEmpty());
    CHECK(msg.text == L"[云端录像] 周会");
    CHECK(msg.screenShareContent == kCrcCard);
    CHECK(msg.screenShareHasButton);
    CHECK(msg.screenShareButtonEnabled);
    CHECK(IsCloudRecordCardOpenable(msg));

    //屏幕共享状态的刷新遍历同一 kind 的全部卡片：不得改动云端录像卡片
    ApplyScreenShareState(msg, kScreenShareRoomEnded);
    CHECK(msg.screenShareButtonEnabled);
    CHECK(msg.screenShareStatus == L"时长 01:23 · 15.6 MB");
    CHECK_FALSE(ApplyScreenShareInfo(msg, u8"{\"roomid\":\"r1\",\"title\":\"x\"}"));
    CHECK(msg.screenShareTitle == L"周会");

    //本人发的同样可以播放
    ChatMessage mine = CrcBubble(true);
    ApplyCloudRecordCard(mine, kCrcAudioCard, L"");
    CHECK(mine.cloudRecordAudioOnly);
    CHECK(mine.screenShareSubtitle == L"我分享的云端录像");
    CHECK(IsCloudRecordCardOpenable(mine));

    //没有标题：标题行写「云端录像」，摘要只写类型
    ChatMessage untitled = CrcBubble(false);
    ApplyCloudRecordCard(untitled, u8"{\"recordid\":\"9\"}", L"");
    CHECK(untitled.screenShareTitle == L"云端录像");
    CHECK(untitled.text == L"[云端录像]");
    CHECK(IsCloudRecordCardOpenable(untitled));

    //内容不可用：不可点，状态写明原因
    ChatMessage broken = CrcBubble(false);
    ApplyCloudRecordCard(broken, "not json", L"张三");
    CHECK(broken.screenShareIsCloudRecord);
    CHECK(broken.infoCardRefId == 0);
    CHECK_FALSE(broken.screenShareHasButton);
    CHECK_FALSE(IsCloudRecordCardOpenable(broken));
    CHECK(broken.screenShareStatus == L"录像内容不可用");

    //别的卡片不算
    ChatMessage text = CrcBubble(false);
    CHECK_FALSE(IsCloudRecordCardOpenable(text));
}

TEST_CASE("CRC4 云端录像卡片的摘要、本地库与菜单", "[cloudreccard]")
{
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_cloudrecord, std::string()) == u8"[云端录像]");
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CrcMessageJson(kCrcCard), notify));
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[云端录像] 周会");

    //会话列表：按 digestarg 在本地生成摘要
    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":1,\"cid\":\"5127\",\"lastmsgid\":\"9201\",\"lastmsg\":{\"from\":\"5127\",\"msgtype\":24,"
        u8"\"digest\":\"S1\",\"digestarg\":\"周会\",\"timestamp\":1}}"
        u8"]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_digest == u8"[云端录像] 周会");

    //消息记录窗：预览取自 content 列的 cloudrecord 子对象，搜索文字是标题
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_single, kCrcOwnerId);
    CHECK(balloonhistory::PreviewFromContent(message_type_cloudrecord, rec.m_content) == u8"[云端录像] 周会");
    CHECK(balloonhistory::PreviewFromContent(message_type_cloudrecord, "{}") == u8"[云端录像]");
    CHECK(rec.m_searchText == u8"周会");
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(rec, restored));
    CHECK(restored.m_cloudRecordContent == notify.m_cloudRecordContent);

    //聊天窗：卡片复用屏幕共享卡片的 kind，菜单规则相同
    ChatMessage msg = CrcBubble(false);
    ApplyCloudRecordCard(msg, kCrcCard, L"");
    CHECK(msg.kind == kChatMenuKindScreenShare);
    CHECK_FALSE(CanForwardMessage(kChatMenuKindScreenShare, kCrcMsgId, 0));
    CHECK_FALSE(CanMergeForwardMessage(kChatMenuKindScreenShare, kCrcMsgId, 0));
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindScreenShare, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(CrcHasMenuId(model, kChatMsgMenuDelete));
    CHECK_FALSE(CrcHasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(CrcHasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(CrcHasMenuId(model, kChatMsgMenuRecall));

    //消息记录窗的行菜单
    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_cloudrecord, kCrcMsgId));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_cloudrecord;
    ctx.m_msgId = kCrcMsgId;
    const std::vector<balloonhistory::HistoryRowMenuItem> rowModel = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < rowModel.size(); ++i)
    {
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}
