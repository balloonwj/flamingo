/**
 *  群通话在 exe 一侧的单元测试（Suite GCX，标签 [groupcall]），GroupCallCardTests.cpp
 *  覆盖（对应 docs/群音视频通话方案.md 第 6.3 节）：
 *    - GCX1 状态缓存：进行中、结束、换了一场后旧房间视为已结束、还不知道；说进行中却没有房间号按没有处理；清空；
 *    - GCX2 卡片 content 解析（与服务端 GroupCallProtocol::buildCardContent / buildEndedCardContent 的输出一致）：
 *           房间号、媒体类型、发起人、发起人名；结束卡片的时长；kind 不对、缺 roomid 失败；
 *    - GCX3 套用到气泡：kind 为屏幕共享卡片且标为群通话、标题与 text 按媒体类型、副标题（本人 / 别人 / 没有名字），
 *           本人发起的卡片也有「加入」；内容不可用时没有按钮；
 *    - GCX4 状态文字与按钮：进行中（带 / 不带人数）、已结束、还不知道；content 带时长的卡片一直是「已结束 · 时长」；
 *           状态变化时重填，非群通话卡片不受影响；
 *    - GCX5 时长文字：MM:SS 与 H:MM:SS；
 *    - GCX6 群聊横幅文字：语音 / 视频、带 / 不带人数、本人在通话中；
 *    - GCX7 消息解析与摘要：msgtype 16 的 content 原文保存在 m_groupCallContent，会话摘要「[群语音通话]」「[群视频通话]」，
 *           不是群通话卡片时为「[通话]」；
 *    - GCX8 本地库：content 列存 groupcall 子对象、预览按媒体类型；结束后改写 content 列，还原出来带时长，原始JSON不动；
 *           消息记录窗的行菜单没有转发；聊天窗菜单与屏幕共享卡片相同（没有转发、Pin、撤回）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Feature/GroupCall/GroupCallStatusCache.h"
#include "../UI/BalloonChatWnd/ChatGroupCallCard.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"   // GCX6：群聊横幅文字
#include "../UI/BalloonHistory/HistoryView.h"             // GCX8：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"          // GCX8：消息记录窗的行菜单
#include "../UI/BalloonHistory/V2MsgQuery.h"              // GCX8：RecordFromNotify / BuildContentJson
#include "../UI/BalloonHistory/V2MsgStore.h"              // GCX8：真的 SQLite 库
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"

using namespace balloonchatwnd;
using namespace balloongroupcall;

namespace {

//测试用的群、房间与人
const int64_t kGroupId = 600001;
const int64_t kOtherGroupId = 600002;
const int64_t kInitiatorId = 5302;
const int64_t kCardMsgId = 7701;
const char* const kRoomId = "gc-600001-1";
const char* const kOtherRoom = "gc-600001-2";
//发起时间：2026-10-04 某时刻，Unix 毫秒
const int64_t kStartTime = 1759550000000LL;
//群通话的媒体类型（与 GroupCallHub.h 的 GroupCallMedia 一致）：1 群语音、2 群视频
const int kMediaVoice = 1;
const int kMediaVideo = 2;

//服务端 GroupCallProtocol::buildCardContent 的输出（jsoncpp 紧凑写法，键按字母序）：群语音，发起人「张三」
const char* const kVoiceCard =
    "{\"initiator\":\"5302\",\"initiatorname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"kind\":\"groupcall\",\"media\":1,"
    "\"roomid\":\"gc-600001-1\",\"starttime\":\"1759550000000\"}";
//同一场群视频
const char* const kVideoCard =
    "{\"initiator\":\"5302\",\"initiatorname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"kind\":\"groupcall\",\"media\":2,"
    "\"roomid\":\"gc-600001-1\",\"starttime\":\"1759550000000\"}";
//buildEndedCardContent 改写后的群语音卡片：通话 1 小时 2 分 5 秒
const char* const kEndedVoiceCard =
    "{\"duration\":3725,\"endtime\":\"1759553725000\",\"initiator\":\"5302\",\"initiatorname\":\"\xE5\xBC\xA0\xE4\xB8\x89\","
    "\"kind\":\"groupcall\",\"media\":1,\"roomid\":\"gc-600001-1\",\"starttime\":\"1759550000000\"}";
//结束卡片的时长（秒），与 kEndedVoiceCard 一致
const int64_t kEndedDuration = 3725;

ChatMessage Bubble(bool fromMe)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = kCardMsgId;
    return msg;
}

bool HasMenuId(const std::vector<ChatMessageMenuEntry>& model, int id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
            return true;
    }
    return false;
}

//本次进程专用的临时库路径（先删掉同名残留，含 WAL 日志）
std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

Json::Value ParseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//服务端推给群成员的一条群通话卡片消息（msgtype 16，content 为对象）
std::string CardMessageJson(const char* content)
{
    Json::Value msg;
    msg["msgid"] = std::to_string(kCardMsgId);
    msg["ctype"] = conversation_group;
    msg["cid"] = std::to_string(kGroupId);
    msg["from"] = std::to_string(kInitiatorId);
    msg["to"] = std::to_string(kGroupId);
    msg["msgtype"] = message_type_rtc;
    msg["content"] = ParseJson(content);
    msg["timestamp"] = static_cast<Json::Int64>(kStartTime);
    return Compact(msg);
}

} // namespace

TEST_CASE("GCX1 各群的群通话状态缓存", "[groupcall]")
{
    GroupCallStatusCache cache;
    //还没收到任何状态
    CHECK(cache.RoomState(kGroupId, kRoomId) == kGroupCallRoomUnknown);
    GroupCallActiveInfo info;
    CHECK_FALSE(cache.GetActive(kGroupId, info));

    cache.Update(kGroupId, kRoomId, true, kMediaVideo, 3, kInitiatorId, kStartTime);
    CHECK(cache.RoomState(kGroupId, kRoomId) == kGroupCallRoomActive);
    REQUIRE(cache.GetActive(kGroupId, info));
    CHECK(info.m_roomId == kRoomId);
    CHECK(info.m_media == kMediaVideo);
    CHECK(info.m_count == 3);
    CHECK(info.m_initiatorId == kInitiatorId);
    CHECK(info.m_startTime == kStartTime);
    //同一群里另一场房间号：已结束
    CHECK(cache.RoomState(kGroupId, kOtherRoom) == kGroupCallRoomEnded);
    //别的群不受影响
    CHECK(cache.RoomState(kOtherGroupId, kRoomId) == kGroupCallRoomUnknown);

    //换了一场：旧房间视为已结束
    cache.Update(kGroupId, kOtherRoom, true, kMediaVoice, 1, kInitiatorId, kStartTime + 1);
    CHECK(cache.RoomState(kGroupId, kRoomId) == kGroupCallRoomEnded);
    CHECK(cache.RoomState(kGroupId, kOtherRoom) == kGroupCallRoomActive);

    //结束
    cache.Update(kGroupId, kOtherRoom, false, kMediaVoice, 0, kInitiatorId, 0);
    CHECK(cache.RoomState(kGroupId, kOtherRoom) == kGroupCallRoomEnded);
    CHECK_FALSE(cache.GetActive(kGroupId, info));

    //说进行中却没有房间号：按没有处理
    cache.Update(kOtherGroupId, "", true, kMediaVoice, 2, kInitiatorId, kStartTime);
    CHECK_FALSE(cache.GetActive(kOtherGroupId, info));
    CHECK(cache.RoomState(kOtherGroupId, kRoomId) == kGroupCallRoomEnded);

    //群 id 不合法时忽略
    cache.Update(0, kRoomId, true, kMediaVoice, 1, kInitiatorId, kStartTime);
    CHECK(cache.RoomState(0, kRoomId) == kGroupCallRoomUnknown);

    cache.Clear();
    CHECK(cache.RoomState(kGroupId, kRoomId) == kGroupCallRoomUnknown);
}

TEST_CASE("GCX2 群通话卡片 content 的解析", "[groupcall]")
{
    GroupCallCardInfo info;
    REQUIRE(ParseGroupCallCard(kVoiceCard, info));
    CHECK(info.m_roomId == kRoomId);
    CHECK(info.m_media == kMediaVoice);
    CHECK(info.m_initiatorId == kInitiatorId);
    CHECK(info.m_initiatorName == CString(L"张三"));
    CHECK_FALSE(info.m_ended);
    CHECK(GroupCallRoomIdOf(kVoiceCard) == kRoomId);

    REQUIRE(ParseGroupCallCard(kEndedVoiceCard, info));
    CHECK(info.m_ended);
    CHECK(info.m_durationSeconds == kEndedDuration);

    CHECK_FALSE(ParseGroupCallCard("{\"kind\":\"screenshare\",\"roomid\":\"ss-1\"}", info));
    CHECK_FALSE(ParseGroupCallCard("{\"kind\":\"groupcall\"}", info));
    CHECK_FALSE(ParseGroupCallCard("not json", info));
    CHECK(GroupCallRoomIdOf("{}").empty());
}

TEST_CASE("GCX3 群通话卡片套用到气泡", "[groupcall]")
{
    //别人发起的群语音
    ChatMessage other = Bubble(false);
    ApplyGroupCallCard(other, kVoiceCard, CString(L"张三（研发）"), kGroupCallRoomActive, 2);
    CHECK(other.kind == kMsgScreenShare);
    CHECK(other.screenShareIsGroupCall);
    CHECK(other.groupCallMedia == kMediaVoice);
    CHECK(other.text == CString(L"[群语音通话]"));
    CHECK(other.screenShareTitle == CString(L"群语音通话"));
    CHECK(other.screenShareRoomId == kRoomId);
    //调用方给了群名片就用群名片
    CHECK(other.screenShareHostName == CString(L"张三（研发）"));
    CHECK(other.screenShareSubtitle == CString(L"张三（研发）发起的通话"));
    CHECK(other.screenShareHasButton);
    CHECK(other.screenShareContent == kVoiceCard);
    CHECK(other.msgid == kCardMsgId);

    //没给名字：用卡片里的 initiatorname
    ChatMessage noName = Bubble(false);
    ApplyGroupCallCard(noName, kVideoCard, CString(), kGroupCallRoomActive, 0);
    CHECK(noName.groupCallMedia == kMediaVideo);
    CHECK(noName.text == CString(L"[群视频通话]"));
    CHECK(noName.screenShareTitle == CString(L"群视频通话"));
    CHECK(noName.screenShareSubtitle == CString(L"张三发起的通话"));

    //本人发起的：写「我发起的通话」，仍有「加入」（中途离开后可以回来）
    ChatMessage mine = Bubble(true);
    ApplyGroupCallCard(mine, kVoiceCard, CString(), kGroupCallRoomActive, 1);
    CHECK(mine.screenShareSubtitle == CString(L"我发起的通话"));
    CHECK(mine.screenShareHasButton);
    CHECK(mine.screenShareButtonEnabled);

    //内容不可用：仍是一张卡片，没有房间号、没有按钮
    ChatMessage broken = Bubble(false);
    ApplyGroupCallCard(broken, "{}", CString(L"张三"), kGroupCallRoomUnknown, 0);
    CHECK(broken.kind == kMsgScreenShare);
    CHECK(broken.screenShareIsGroupCall);
    CHECK(broken.screenShareRoomId.empty());
    CHECK_FALSE(broken.screenShareHasButton);
    CHECK_FALSE(broken.screenShareButtonEnabled);
}

TEST_CASE("GCX4 群通话卡片的状态文字与按钮", "[groupcall]")
{
    ChatMessage msg = Bubble(false);
    ApplyGroupCallCard(msg, kVoiceCard, CString(L"张三"), kGroupCallRoomActive, 3);
    CHECK(msg.screenShareState == kGroupCallRoomActive);
    CHECK(msg.screenShareStatus == CString(L"正在通话 · 3 人"));
    CHECK(msg.screenShareButtonEnabled);

    ApplyGroupCallState(msg, kGroupCallRoomActive, 0);
    CHECK(msg.screenShareStatus == CString(L"正在通话"));

    //还不知道：按钮可点，由服务端判定
    ApplyGroupCallState(msg, kGroupCallRoomUnknown, 0);
    CHECK(msg.screenShareStatus == CString(L"正在获取状态"));
    CHECK(msg.screenShareButtonEnabled);

    ApplyGroupCallState(msg, kGroupCallRoomEnded, 0);
    CHECK(msg.screenShareState == kGroupCallRoomEnded);
    CHECK(msg.screenShareStatus == CString(L"通话已结束"));
    CHECK_FALSE(msg.screenShareButtonEnabled);

    //服务端改写过的卡片：不管缓存怎么说，一直是已结束、写时长、没有按钮
    ChatMessage ended = Bubble(false);
    ApplyGroupCallCard(ended, kEndedVoiceCard, CString(L"张三"), kGroupCallRoomActive, 2);
    CHECK(ended.screenShareStatus == CString(L"通话已结束 · 时长 1:02:05"));
    CHECK_FALSE(ended.screenShareHasButton);
    CHECK_FALSE(ended.screenShareButtonEnabled);
    ApplyGroupCallState(ended, kGroupCallRoomActive, 4);
    CHECK(ended.screenShareStatus == CString(L"通话已结束 · 时长 1:02:05"));

    //不是群通话卡片：不受影响
    ChatMessage text = Bubble(false);
    text.screenShareStatus = L"x";
    ApplyGroupCallState(text, kGroupCallRoomEnded, 0);
    CHECK(text.kind == kMsgText);
    CHECK(text.screenShareStatus == CString(L"x"));
}

TEST_CASE("GCX5 卡片上的时长文字", "[groupcall]")
{
    CHECK(FormatGroupCallCardDuration(0) == CString(L"00:00"));
    CHECK(FormatGroupCallCardDuration(-3) == CString(L"00:00"));
    CHECK(FormatGroupCallCardDuration(59) == CString(L"00:59"));
    CHECK(FormatGroupCallCardDuration(65) == CString(L"01:05"));
    CHECK(FormatGroupCallCardDuration(kEndedDuration) == CString(L"1:02:05"));
}

TEST_CASE("GCX6 群聊横幅的文字", "[groupcall]")
{
    CHECK(MakeGroupCallBannerText(false, false, 3) == L"群语音通话进行中 · 3 人，点击加入 ›");
    CHECK(MakeGroupCallBannerText(false, true, 2) == L"群视频通话进行中 · 2 人，点击加入 ›");
    CHECK(MakeGroupCallBannerText(false, false, 0) == L"群语音通话进行中，点击加入 ›");
    CHECK(MakeGroupCallBannerText(true, true, 5) == L"你正在群视频通话中");
}

TEST_CASE("GCX7 群通话卡片消息的解析与摘要", "[groupcall]")
{
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CardMessageJson(kVideoCard), notify));
    CHECK(notify.m_msgtype == message_type_rtc);
    CHECK(notify.m_msgid == kCardMsgId);
    CHECK(GroupCallRoomIdOf(notify.m_groupCallContent) == kRoomId);
    //content 经客户端重新序列化（中文写成 \u 转义），按解析结果核对，不比较原文
    GroupCallCardInfo parsed;
    REQUIRE(ParseGroupCallCard(notify.m_groupCallContent, parsed));
    CHECK(parsed.m_media == kMediaVideo);
    CHECK(parsed.m_initiatorId == kInitiatorId);
    CHECK(parsed.m_initiatorName == CString(L"张三"));
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[群视频通话]");

    notify.m_groupCallContent = kVoiceCard;
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[群语音通话]");
    //不是群通话卡片：只写类型
    CHECK(net::MessageProtocolClient::groupCallDigest("{\"kind\":\"other\"}") == u8"[通话]");
    CHECK(net::MessageProtocolClient::groupCallDigest("") == u8"[通话]");
}

TEST_CASE("GCX8 本地库、消息记录窗与菜单", "[groupcall]")
{
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CardMessageJson(kVoiceCard), notify));
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_group, kGroupId);
    CHECK(rec.m_content.find("\"groupcall\"") != std::string::npos);
    CHECK(balloonhistory::PreviewFromContent(message_type_rtc, rec.m_content) == u8"[群语音通话]");
    net::MessageNotify videoNotify = notify;
    videoNotify.m_groupCallContent = kVideoCard;
    const balloonhistory::HistoryRecord videoRec =
        balloonhistory::RecordFromNotify(videoNotify, conversation_group, kGroupId);
    CHECK(balloonhistory::PreviewFromContent(message_type_rtc, videoRec.m_content) == u8"[群视频通话]");

    //通话结束：改写 content 列，还原出来带时长；原始JSON仍是服务端当初发来的样子
    const std::wstring path = FreshTempDb(L"flamingo_gcx8.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    REQUIRE(store.Upsert(rec));
    net::MessageNotify forColumn;
    forColumn.m_msgtype = message_type_rtc;
    forColumn.m_groupCallContent = kEndedVoiceCard;
    bool hit = false;
    REQUIRE(store.UpdateContent(conversation_group, kGroupId, kCardMsgId, balloonhistory::BuildContentJson(forColumn),
                                hit));
    CHECK(hit);
    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kCardMsgId, row, found));
    REQUIRE(found);
    std::string fromColumn;
    REQUIRE(balloonhistory::GroupCallContentFromRecord(row, fromColumn));
    GroupCallCardInfo info;
    REQUIRE(ParseGroupCallCard(fromColumn, info));
    CHECK(info.m_ended);
    CHECK(info.m_durationSeconds == kEndedDuration);
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(row, restored));
    REQUIRE(ParseGroupCallCard(restored.m_groupCallContent, info));
    CHECK(info.m_ended);
    net::MessageNotify fromRaw;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, fromRaw));
    REQUIRE(ParseGroupCallCard(fromRaw.m_groupCallContent, info));
    CHECK_FALSE(info.m_ended);

    //本地没有这张卡片：命中 0 行，不报错
    hit = true;
    REQUIRE(store.UpdateContent(conversation_group, kGroupId, kCardMsgId + 1,
                                balloonhistory::BuildContentJson(forColumn), hit));
    CHECK_FALSE(hit);
    store.Close();
    ::DeleteFileW(path.c_str());

    //消息记录窗不能转发群通话卡片
    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_rtc, kCardMsgId));
    //聊天窗：与屏幕共享卡片同一种气泡，没有转发、Pin、撤回
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindScreenShare, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(HasMenuId(model, kChatMsgMenuDelete));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuRecall));
}
