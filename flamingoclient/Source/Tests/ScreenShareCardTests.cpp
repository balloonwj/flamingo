/**
 *  屏幕共享卡片与 exe 侧状态的单元测试（Suite SSC，标签 [sscard]），ScreenShareCardTests.cpp
 *  覆盖（对应 docs/屏幕共享与录制方案.md 第 2 期 2g）：
 *    - SSC1 解析卡片正文（与服务端 ScreenShareProtocol::buildCardContent 的输出逐字一致）：roomid、发起人、发起人名、
 *           发起时间；脏数据解析失败；整条消息解析后 content 原文保存在 m_screenShareContent；会话摘要「[屏幕共享]」；
 *    - SSC2 套用到气泡：别人的卡片有「加入」按钮、副标题是「某某发起的共享」，本人的卡片没有按钮、写「我发起的共享」；
 *           text 为「[屏幕共享]」；内容不可用时仍是一张没有按钮的卡片；
 *    - SSC3 状态：正在共享时按钮可点、写「正在共享」，已结束时按钮不可点、写「共享已结束」，还不知道时按钮可点、
 *           写「正在获取状态」；状态变化时重填，非屏幕共享卡片不受影响；
 *    - SSC4 各会话的共享状态缓存：开始、结束、查询结果三种更新（同一场换了主持人时不沿用原主持人的名字）；同一会话里旧房间在新房间开始后视为已结束；单聊与群聊
 *           即使 id 相同也互不影响；清空后回到「还不知道」；
 *    - SSC5 右键菜单：没有转发、合并转发、Pin、撤回，保留复制、引用回复、删除、多选；kind 镜像一致；
 *    - SSC6 本地库与消息记录窗：content 列存 screenshare 子对象，预览「[屏幕共享]」，行菜单没有转发与 Pin；
 *    - SSC7 单聊里服务端写的结束提示（msgtype 17，发送者为发起人）画成居中提示行，与群聊系统提示同一种行；
 *    - SSPWC5 设了加入密码的共享（第 12 期）：卡片正文的 locked 解析出来，副标题后标出「需要密码」；状态缓存记下
 *           是否要密码；群聊横幅写「正在共享屏幕（需要密码）」，本人发起时仍只写「你正在共享屏幕」；
 *    - SSFMC2b 强制模式的共享（第 13 期，第 14 期改措辞）：卡片正文的 forced、副标题下另起说明行写明强制模式的效果、
 *             加入请求带强制模式、状态缓存与群聊横幅的强制模式说明；
 *    - SSTIC3 共享标题与描述（第 15 期）：卡片正文的 title / desc，粗体标题行写共享标题（旧卡片照旧写「屏幕共享」）、
 *             副标题下的描述，群聊横幅带标题；
 *    - SSTIC7 状态缓存记下共享标题（第 15 期），同一场共享在不带标题的更新里沿用，换了一场则清空；
 *    - SSTIC11 共享中改了标题与描述的推送（1734）的解析：房间号、会话、卡片 msgid 与改写后的 content，不带卡片时 msgid 为 0；
 *    - SSTIC12 聊天窗里的卡片换成新的标题与描述，副标题、状态、按钮不变；相同内容、非卡片、内容不可用时不改；
 *    - SSTIC13 本地库里的卡片：改写 content 列，还原出来是新标题，原始JSON不动；
 *    - SSTIC14 本地没有这张卡片、推送不带卡片时不报错也不建行；content 列没有卡片内容的旧行退回原始JSON；
 *    - SSTIC15 移交主持人后的 1734 带新主持人：解析出 hostid（字符串或整数写法）与 hostname，不带时为 0 与空；
 *    - SSTIC16 状态缓存按 1734 更新这一场共享：带新主持人时换主持人与名字，不带时沿用；标题为空时沿用；
 *            不是缓存记着的那一场、或没有可更新的内容时不改动也不新建记录；
 *    - SSCSNAP 效果图（标签 [.snapshot]，默认不运行）：几种状态的卡片画进一个消息列表，存到 snapshots/screenshare_cards.png。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatScreenShareCard.h"
#include "../UI/BalloonChatWnd/ChatSystemTip.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"   // SSCSNAP：把几张卡片排进一个列表里画
#include "../UI/BalloonHistory/HistoryView.h"       // SSC6：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"    // SSC6：消息记录窗的行菜单
#include "../UI/BalloonHistory/V2MsgQuery.h"        // SSC6：RecordFromNotify
#include "../UI/BalloonHistory/V2MsgStore.h"        // SSTIC13、SSTIC14：真的 SQLite 库
#include "../UI/BalloonMain/ScreenShareInfoApply.h" // SSTIC11、SSTIC13、SSTIC14：1734 推送解析与写库
#include "../third-party/jsoncpp-1.9.0/json.h"      // SSTIC11、SSTIC13：拼推送与消息正文
#include "../Feature/ScreenShare/ScreenShareStatusCache.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"   // SSPWC5：群聊横幅文字
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"

using namespace balloonchatwnd;

namespace {

//发起人与会话
const long long kHostId = 10001;
const long long kPeerId = 10002;
const long long kGroupId = 600001;
//发起时间：2026-10-02 某时刻，Unix 毫秒
const long long kStartTime = 1759370000000LL;

//服务端 ScreenShareProtocol::buildCardContent 的输出（jsoncpp 紧凑写法，键按字母序）
const char* const kCardContent =
    "{\"host\":\"10001\",\"hostname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"roomid\":\"ss-7\",\"starttime\":\"1759370000000\"}";

//另一场共享的房间号
const char* const kOtherRoom = "ss-8";

ChatMessage Bubble(bool fromMe)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = 900;
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

} // namespace

TEST_CASE("SSC1 parse screen share card content", "[sscard]")
{
    net::ScreenShareCardContent card;
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(kCardContent, card));
    CHECK(card.m_roomId == "ss-7");
    CHECK(card.m_hostId == kHostId);
    CHECK(card.m_hostName == u8"张三");
    CHECK(card.m_startTime == kStartTime);

    //缺 roomid、缺发起人、不是 JSON 都解析失败
    CHECK_FALSE(net::MessageProtocolClient::parseScreenShareCardContent("{\"host\":\"10001\"}", card));
    CHECK_FALSE(net::MessageProtocolClient::parseScreenShareCardContent("{\"roomid\":\"ss-7\"}", card));
    CHECK_FALSE(net::MessageProtocolClient::parseScreenShareCardContent("not json", card));

    //会话摘要：「[屏幕共享]」，与服务端 buildMessageDigest 一致
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_screenshare, std::string()) == u8"[屏幕共享]");
    net::MessageNotify notify;
    notify.m_msgtype = message_type_screenshare;
    notify.m_screenShareContent = kCardContent;
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[屏幕共享]");

    //完整消息的解析：content 原文保存在 m_screenShareContent
    const std::string raw = std::string("{\"msgid\":\"77\",\"from\":\"10001\",\"to\":\"10002\",\"msgtype\":19,\"content\":")
        + kCardContent + ",\"timestamp\":1759370000000}";
    net::MessageNotify parsed;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(raw, parsed));
    CHECK(parsed.m_msgtype == message_type_screenshare);
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(parsed.m_screenShareContent, card));
    CHECK(card.m_roomId == "ss-7");
}

TEST_CASE("SSC2 apply card to bubble", "[sscard]")
{
    //别人发起的：有「加入」按钮，副标题带发起人名
    ChatMessage other = Bubble(false);
    ApplyScreenShareCard(other, kCardContent, L"张三", kScreenShareRoomActive);
    CHECK(other.kind == kMsgScreenShare);
    CHECK(other.text == L"[屏幕共享]");
    CHECK(other.screenShareRoomId == "ss-7");
    CHECK(other.screenShareHostName == L"张三");
    CHECK(other.screenShareSubtitle == L"张三发起的共享");
    CHECK(other.screenShareHasButton);
    CHECK(other.screenShareContent == kCardContent);
    CHECK(other.msgid == 900);

    //调用方给不出显示名时用正文里的 hostname
    ChatMessage noName = Bubble(false);
    ApplyScreenShareCard(noName, kCardContent, L"", kScreenShareRoomActive);
    CHECK(noName.screenShareSubtitle == L"张三发起的共享");
    CHECK(noName.screenShareHostName == L"张三");

    //本人发起的：没有按钮，写「我发起的共享」
    ChatMessage mine = Bubble(true);
    ApplyScreenShareCard(mine, kCardContent, L"", kScreenShareRoomActive);
    CHECK(mine.kind == kMsgScreenShare);
    CHECK(mine.screenShareSubtitle == L"我发起的共享");
    CHECK_FALSE(mine.screenShareHasButton);
    CHECK_FALSE(mine.screenShareButtonEnabled);

    //内容不可用：仍是一张卡片，没有房间号、没有按钮
    ChatMessage broken = Bubble(false);
    ApplyScreenShareCard(broken, "not json", L"张三", kScreenShareRoomUnknown);
    CHECK(broken.kind == kMsgScreenShare);
    CHECK(broken.screenShareRoomId.empty());
    CHECK_FALSE(broken.screenShareHasButton);
    CHECK_FALSE(broken.screenShareButtonEnabled);
}

TEST_CASE("SSC3 card state", "[sscard]")
{
    ChatMessage msg = Bubble(false);
    ApplyScreenShareCard(msg, kCardContent, L"张三", kScreenShareRoomActive);
    CHECK(msg.screenShareState == kScreenShareRoomActive);
    CHECK(msg.screenShareStatus == L"正在共享");
    CHECK(msg.screenShareButtonEnabled);

    ApplyScreenShareState(msg, kScreenShareRoomEnded);
    CHECK(msg.screenShareState == kScreenShareRoomEnded);
    CHECK(msg.screenShareStatus == L"共享已结束");
    CHECK_FALSE(msg.screenShareButtonEnabled);
    CHECK(msg.screenShareHasButton);

    //还不知道状态时允许点「加入」：共享已经结束的话服务端会拒绝，卡片随后被重填为已结束
    ApplyScreenShareState(msg, kScreenShareRoomUnknown);
    CHECK(msg.screenShareStatus == L"正在获取状态");
    CHECK(msg.screenShareButtonEnabled);

    //本人的卡片：状态文字照常变化，始终没有按钮
    ChatMessage mine = Bubble(true);
    ApplyScreenShareCard(mine, kCardContent, L"", kScreenShareRoomEnded);
    CHECK(mine.screenShareStatus == L"共享已结束");
    ApplyScreenShareState(mine, kScreenShareRoomActive);
    CHECK(mine.screenShareStatus == L"正在共享");
    CHECK_FALSE(mine.screenShareButtonEnabled);

    //非屏幕共享卡片不受影响
    ChatMessage text = Bubble(false);
    text.text = L"hello";
    ApplyScreenShareState(text, kScreenShareRoomEnded);
    CHECK(text.kind == kMsgText);
    CHECK(text.screenShareStatus.IsEmpty());
}

TEST_CASE("SSC4 status cache per conversation", "[sscard]")
{
    balloonscreenshare::ScreenShareStatusCache cache;
    CHECK(cache.RoomState(conversation_single, kPeerId, "ss-7") == kScreenShareRoomUnknown);

    //有人开始共享
    cache.Update(conversation_single, kPeerId, "ss-7", kHostId, L"张三", true);
    CHECK(cache.RoomState(conversation_single, kPeerId, "ss-7") == kScreenShareRoomActive);
    //同一会话里别的房间（更早的卡片）视为已结束
    CHECK(cache.RoomState(conversation_single, kPeerId, kOtherRoom) == kScreenShareRoomEnded);
    balloonscreenshare::ScreenShareActiveInfo info;
    REQUIRE(cache.GetActive(conversation_single, kPeerId, info));
    CHECK(info.m_roomId == "ss-7");
    CHECK(info.m_hostId == kHostId);
    CHECK(info.m_hostName == L"张三");

    //查询结果不带发起人名：沿用已知的名字
    cache.Update(conversation_single, kPeerId, "ss-7", kHostId, L"", true);
    REQUIRE(cache.GetActive(conversation_single, kPeerId, info));
    CHECK(info.m_hostName == L"张三");

    //同一场共享换了主持人（2026-10-08）：查询结果不带名字时不再沿用原主持人的名字，由调用方按新主持人取名字
    cache.Update(conversation_single, kPeerId, "ss-7", kPeerId, L"", true);
    REQUIRE(cache.GetActive(conversation_single, kPeerId, info));
    CHECK(info.m_hostId == kPeerId);
    CHECK(info.m_hostName.empty());
    cache.Update(conversation_single, kPeerId, "ss-7", kHostId, L"张三", true);

    //单聊与群聊即使 id 相同也互不影响
    CHECK(cache.RoomState(conversation_group, kPeerId, "ss-7") == kScreenShareRoomUnknown);
    CHECK_FALSE(cache.GetActive(conversation_group, kPeerId, info));

    //共享结束：该会话里所有房间都已结束
    cache.Update(conversation_single, kPeerId, std::string(), 0, L"", false);
    CHECK(cache.RoomState(conversation_single, kPeerId, "ss-7") == kScreenShareRoomEnded);
    CHECK_FALSE(cache.GetActive(conversation_single, kPeerId, info));

    //新的一场开始：新房间正在共享，旧房间仍是已结束
    cache.Update(conversation_single, kPeerId, kOtherRoom, kPeerId, L"李四", true);
    CHECK(cache.RoomState(conversation_single, kPeerId, kOtherRoom) == kScreenShareRoomActive);
    CHECK(cache.RoomState(conversation_single, kPeerId, "ss-7") == kScreenShareRoomEnded);

    //群聊的查询结果：没有共享
    cache.Update(conversation_group, kGroupId, std::string(), 0, L"", false);
    CHECK(cache.RoomState(conversation_group, kGroupId, "ss-9") == kScreenShareRoomEnded);

    //登出时清空：全部回到还不知道
    cache.Clear();
    CHECK(cache.RoomState(conversation_single, kPeerId, kOtherRoom) == kScreenShareRoomUnknown);
    CHECK(cache.RoomState(conversation_group, kGroupId, "ss-9") == kScreenShareRoomUnknown);
}

TEST_CASE("SSC5 message menu for screen share cards", "[sscard]")
{
    CHECK(kChatMenuKindScreenShare == (int)kMsgScreenShare);
    CHECK_FALSE(CanForwardMessage(kChatMenuKindScreenShare, 900, 0));
    CHECK_FALSE(CanMergeForwardMessage(kChatMenuKindScreenShare, 900, 0));
    //调用方即使按普通消息传了可撤回、可 Pin、可转发，屏幕共享卡片也不给这几项
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindScreenShare, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(HasMenuId(model, kChatMsgMenuCopy));
    CHECK(HasMenuId(model, kChatMsgMenuReply));
    CHECK(HasMenuId(model, kChatMsgMenuDelete));
    CHECK(HasMenuId(model, kChatMsgMenuMultiSelect));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuRecall));
}

TEST_CASE("SSC6 local store and history window", "[sscard]")
{
    net::MessageNotify notify;
    notify.m_msgid = 901;
    notify.m_from = kHostId;
    notify.m_msgtype = message_type_screenshare;
    notify.m_screenShareContent = kCardContent;
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_single, kPeerId);
    CHECK(rec.m_content.find("\"screenshare\"") != std::string::npos);
    CHECK(rec.m_content.find("ss-7") != std::string::npos);
    CHECK(balloonhistory::PreviewFromContent(message_type_screenshare, rec.m_content) == "[屏幕共享]");
    CHECK(balloonhistory::PreviewFromContent(message_type_screenshare, "{}") == "[屏幕共享]");

    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_screenshare, 901));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_screenshare;
    ctx.m_msgId = 901;
    const std::vector<balloonhistory::HistoryRowMenuItem> model = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < model.size(); ++i)
    {
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}

TEST_CASE("SSC7 end tip in single chat is a centered tip row", "[sscard]")
{
    //单聊的结束提示由服务端记在发起人名下（docs/屏幕共享与录制方案.md 5.3），正文在 content.text
    const std::string raw = "{\"msgid\":\"78\",\"from\":\"10001\",\"to\":\"10002\",\"msgtype\":17,"
                            "\"content\":{\"text\":\"\xE5\xB1\x8F\xE5\xB9\x95\xE5\x85\xB1\xE4\xBA\xAB\xE5\xB7\xB2"
                            "\xE7\xBB\x93\xE6\x9D\x9F\xEF\xBC\x8C\xE6\x97\xB6\xE9\x95\xBF 01:05\"},"
                            "\"timestamp\":1759370065000}";
    net::MessageNotify parsed;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(raw, parsed));
    REQUIRE(parsed.m_msgtype == message_type_system);
    const ChatMessage tip = MakeSystemTipRow(parsed);
    CHECK(tip.kind == kMsgTime);
    CHECK(tip.text == L"屏幕共享已结束，时长 01:05");
    CHECK(tip.msgid == 78);
    CHECK(tip.timestampMs == 1759370065000LL);

    //正文为空时用类型占位文字，不画空行
    net::MessageNotify empty;
    empty.m_msgtype = message_type_system;
    empty.m_msgid = 79;
    const ChatMessage placeholder = MakeSystemTipRow(empty);
    CHECK(placeholder.kind == kMsgTime);
    CHECK_FALSE(placeholder.text.IsEmpty());
}

namespace {

//效果图的尺寸（像素）：宽度与聊天窗消息区相近，高度放得下全部卡片
const int kSsSnapWidth = 620;
//第 15 期多画两张带标题与描述的卡片，图加高
const int kSsSnapHeight = 1040;
//带标题与描述的卡片正文（第 15 期）：普通共享与强制模式各一张
const char* const kSsTitledCard =
    u8"{\"host\":\"10001\",\"hostname\":\"张三\",\"roomid\":\"ss-7\",\"starttime\":\"1759370000000\","
    u8"\"title\":\"周会演示：第三季度产品路线图\",\"desc\":\"讲一下第三季度的产品路线图与各组排期，有问题会后再讨论。\"}";
const char* const kSsTitledForcedCard =
    u8"{\"host\":\"10001\",\"hostname\":\"张三\",\"roomid\":\"ss-7\",\"starttime\":\"1759370000000\","
    u8"\"forced\":1,\"title\":\"全员培训\",\"desc\":\"新版报销流程，请全程观看。\"}";

//效果图位置（测试程序目录 Debug_Win32/Tests 往上三级是仓库根）
const wchar_t* const kSsSnapPath = L"\\..\\..\\..\\snapshots\\screenshare_cards.png";

//在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID
bool FindSsPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//别人发起的卡片：带发送人名与头像字
ChatMessage SnapOthersCard(long long msgid, int state)
{
    ChatMessage msg = Bubble(false);
    msg.msgid = msgid;
    msg.senderName = L"张三";
    msg.senderAvatarText = L"张";
    msg.senderAvatarColor = RGB(0x4F, 0x8E, 0xF7);
    msg.senderUserId = kHostId;
    ApplyScreenShareCard(msg, kCardContent, L"张三", state);
    return msg;
}

} // namespace

TEST_CASE("SSPWC5 locked share shows password required on card, cache and banner", "[sscard][sharepassword]")
{
    //服务端 buildCardContent 在设了密码时多一个 locked（键按字母序排在 hostname 与 roomid 之间）
    const char* const lockedCard =
        "{\"host\":\"10001\",\"hostname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"locked\":1,\"roomid\":\"ss-7\","
        "\"starttime\":\"1759370000000\"}";
    net::ScreenShareCardContent card;
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(lockedCard, card));
    CHECK(card.m_locked);
    CHECK(card.m_roomId == "ss-7");
    //第 12 期之前的卡片没有 locked：按不要密码
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(kCardContent, card));
    CHECK_FALSE(card.m_locked);

    //副标题后标出「需要密码」，按钮照常可点（点了再输密码）
    ChatMessage other = Bubble(false);
    ApplyScreenShareCard(other, lockedCard, L"张三", kScreenShareRoomActive);
    CHECK(other.screenShareSubtitle == L"张三发起的共享 · 需要密码");
    CHECK(other.screenShareHasButton);
    CHECK(other.screenShareButtonEnabled);
    ChatMessage mine = Bubble(true);
    ApplyScreenShareCard(mine, lockedCard, L"", kScreenShareRoomActive);
    CHECK(mine.screenShareSubtitle == L"我发起的共享 · 需要密码");
    ChatMessage plain = Bubble(false);
    ApplyScreenShareCard(plain, kCardContent, L"张三", kScreenShareRoomActive);
    CHECK(plain.screenShareSubtitle == L"张三发起的共享");

    //状态缓存：记下是否要密码，每次更新直接覆盖；共享结束后清掉
    balloonscreenshare::ScreenShareStatusCache cache;
    balloonscreenshare::ScreenShareActiveInfo info;
    cache.Update(conversation_group, kGroupId, "ss-7", kHostId, L"张三", true, true);
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_locked);
    cache.Update(conversation_group, kGroupId, kOtherRoom, kHostId, L"张三", true, false);
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK_FALSE(info.m_locked);
    cache.Update(conversation_group, kGroupId, "ss-9", kHostId, L"", true, true);
    cache.Update(conversation_group, kGroupId, std::string(), 0, L"", false, true);
    CHECK_FALSE(cache.GetActive(conversation_group, kGroupId, info));

    //群聊横幅
    CHECK(MakeScreenShareBannerText(false, L"张三", true) == L"张三 正在共享屏幕（需要密码），点击加入 ›");
    CHECK(MakeScreenShareBannerText(false, L"", true) == L"有人正在共享屏幕（需要密码），点击加入 ›");
    CHECK(MakeScreenShareBannerText(false, L"张三", false) == L"张三 正在共享屏幕，点击加入 ›");
    //本人正在共享：只告知，不提示加入，也不提密码（密码在控制条上）
    CHECK(MakeScreenShareBannerText(true, L"张三", true) == L"你正在共享屏幕");
}

TEST_CASE("SSFMC2b forced share shows forced label on card, cache and banner", "[sscard][shareroom]")
{
    //服务端 buildCardContent 在强制模式时 forced 为 1（键按字母序排在 hostname 之前）
    const char* const forcedCard =
        "{\"forced\":1,\"host\":\"10001\",\"hostname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"locked\":0,\"roomid\":\"ss-7\","
        "\"starttime\":\"1759370000000\"}";
    const char* const bothCard =
        "{\"forced\":1,\"host\":\"10001\",\"hostname\":\"\xE5\xBC\xA0\xE4\xB8\x89\",\"locked\":1,\"roomid\":\"ss-7\","
        "\"starttime\":\"1759370000000\"}";
    net::ScreenShareCardContent card;
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(forcedCard, card));
    CHECK(card.m_forced);
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(kCardContent, card));
    CHECK_FALSE(card.m_forced);

    //第 14 期：强制模式不再只写「强制观看」，副标题下另起说明行写明效果；加入请求要带上强制模式（加入前据此先确认）
    ChatMessage other = Bubble(false);
    ApplyScreenShareCard(other, forcedCard, L"张三", kScreenShareRoomActive);
    CHECK(other.screenShareSubtitle == L"张三发起的共享");
    CHECK(other.screenShareNotice == L"强制模式：加入后全屏观看、不能退出，直到共享结束");
    CHECK(other.screenShareForced);
    ChatMessage both = Bubble(false);
    ApplyScreenShareCard(both, bothCard, L"张三", kScreenShareRoomActive);
    CHECK(both.screenShareSubtitle == L"张三发起的共享 · 需要密码");
    CHECK(both.screenShareNotice == L"强制模式：加入后全屏观看、不能退出，直到共享结束");
    ChatMessage plain = Bubble(false);
    ApplyScreenShareCard(plain, kCardContent, L"张三", kScreenShareRoomActive);
    CHECK_FALSE(plain.screenShareForced);
    CHECK(plain.screenShareNotice.IsEmpty());

    //状态缓存记下是否强制模式
    balloonscreenshare::ScreenShareStatusCache cache;
    balloonscreenshare::ScreenShareActiveInfo info;
    cache.Update(conversation_group, kGroupId, "ss-7", kHostId, L"张三", true, false, true);
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_forced);
    cache.Update(conversation_group, kGroupId, "ss-8", kHostId, L"张三", true, false, false);
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK_FALSE(info.m_forced);

    //群聊横幅：「点击加入 ›」在前，强制模式的说明接在后面（横幅一行放不下时省略的是说明的末尾）
    CHECK(MakeScreenShareBannerText(false, L"张三", false, true) ==
          L"张三 正在共享屏幕，点击加入 ›　强制模式：加入后全屏观看、不能退出，直到共享结束");
    CHECK(MakeScreenShareBannerText(false, L"张三", true, true) ==
          L"张三 正在共享屏幕（需要密码），点击加入 ›　强制模式：加入后全屏观看、不能退出，直到共享结束");
    CHECK(MakeScreenShareBannerText(false, L"", false, true) ==
          L"有人正在共享屏幕，点击加入 ›　强制模式：加入后全屏观看、不能退出，直到共享结束");
    CHECK(MakeScreenShareBannerText(true, L"张三", true, true) == L"你正在共享屏幕");
}

TEST_CASE("SSCSNAP screen share card snapshot", "[.snapshot][sscard]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        //1. 别人发起，正在共享
        list.AppendMessage(SnapOthersCard(1001, kScreenShareRoomActive));
        //2. 别人发起，还不知道状态
        list.AppendMessage(SnapOthersCard(1002, kScreenShareRoomUnknown));
        //3. 别人发起，已结束；其后是服务端写的结束提示
        list.AppendMessage(SnapOthersCard(1003, kScreenShareRoomEnded));
        net::MessageNotify endTip;
        endTip.m_msgid = 1004;
        endTip.m_msgtype = message_type_system;
        endTip.m_text = u8"屏幕共享已结束，时长 01:05";
        list.AppendMessage(MakeSystemTipRow(endTip));
        //5.（第 15 期）带标题与描述的卡片，正在共享
        ChatMessage titled = SnapOthersCard(1006, kScreenShareRoomActive);
        ApplyScreenShareCard(titled, kSsTitledCard, L"张三", kScreenShareRoomActive);
        list.AppendMessage(titled);
        //6.（第 15 期）带标题与描述的强制模式卡片：描述块在说明块上方
        ChatMessage titledForced = SnapOthersCard(1007, kScreenShareRoomActive);
        ApplyScreenShareCard(titledForced, kSsTitledForcedCard, L"张三", kScreenShareRoomActive);
        list.AppendMessage(titledForced);
        //4. 本人发起，正在共享
        ChatMessage mine = Bubble(true);
        mine.msgid = 1005;
        ApplyScreenShareCard(mine, kCardContent, L"", kScreenShareRoomActive);
        list.AppendMessage(mine);

        RECT all = { 0, 0, kSsSnapWidth, kSsSnapHeight };
        list.ForceLayout(all);

        HDC screenDC = ::GetDC(NULL);
        REQUIRE(screenDC != NULL);
        HDC memDC = ::CreateCompatibleDC(screenDC);
        HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kSsSnapWidth, kSsSnapHeight);
        ::ReleaseDC(NULL, screenDC);
        REQUIRE(memDC != NULL);
        REQUIRE(bmp != NULL);
        HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
        HBRUSH bg = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        ::FillRect(memDC, &all, bg);
        ::DeleteObject(bg);
        list.OnPaint(memDC, all);
        ::GdiFlush();
        ::SelectObject(memDC, oldBmp);

        wchar_t exePath[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir(exePath);
        dir = dir.substr(0, dir.find_last_of(L'\\'));
        CLSID png;
        REQUIRE(FindSsPngEncoder(png));
        {
            Gdiplus::Bitmap image(bmp, NULL);
            CHECK(image.Save((dir + kSsSnapPath).c_str(), &png, NULL) == Gdiplus::Ok);
        }
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("SSTIC3 共享标题与描述：卡片的标题行与描述，旧卡片照旧写「屏幕共享」，群聊横幅带标题", "[sscard][shareroom]")
{
    const std::string titled =
        u8"{\"host\":\"10001\",\"hostname\":\"张三\",\"roomid\":\"ss-7\",\"starttime\":\"1759370000000\","
        u8"\"title\":\"周会演示\",\"desc\":\"第三季度汇报\"}";
    net::ScreenShareCardContent card;
    REQUIRE(net::MessageProtocolClient::parseScreenShareCardContent(titled, card));
    CHECK(card.m_title == u8"周会演示");
    CHECK(card.m_desc == u8"第三季度汇报");

    ChatMessage other = Bubble(false);
    ApplyScreenShareCard(other, titled, L"张三", kScreenShareRoomActive);
    CHECK(other.screenShareTitle == L"周会演示");
    CHECK(other.screenShareDesc == L"第三季度汇报");
    CHECK(other.screenShareSubtitle == L"张三发起的共享");
    //旧卡片：标题行照旧写「屏幕共享」，没有描述
    ChatMessage old = Bubble(false);
    ApplyScreenShareCard(old, kCardContent, L"张三", kScreenShareRoomActive);
    CHECK(old.screenShareTitle == L"屏幕共享");
    CHECK(old.screenShareDesc.IsEmpty());

    //群聊横幅
    CHECK(MakeScreenShareBannerText(false, L"张三", false, false, L"周会演示") == L"张三 正在共享：周会演示，点击加入 ›");
    CHECK(MakeScreenShareBannerText(false, L"张三", true, false, L"周会演示") ==
          L"张三 正在共享：周会演示（需要密码），点击加入 ›");
    CHECK(MakeScreenShareBannerText(false, L"张三", false, true, L"周会演示") ==
          L"张三 正在共享：周会演示，点击加入 ›　强制模式：加入后全屏观看、不能退出，直到共享结束");
    CHECK(MakeScreenShareBannerText(false, L"", false, false, L"周会演示") == L"有人正在共享：周会演示，点击加入 ›");
    CHECK(MakeScreenShareBannerText(true, L"张三", false, false, L"周会演示") == L"你正在共享：周会演示");
    //没有标题（老服务端）：与第 14 期相同
    CHECK(MakeScreenShareBannerText(false, L"张三", false, false, L"") == L"张三 正在共享屏幕，点击加入 ›");
}

TEST_CASE("SSTIC7 状态缓存记下共享标题，同一场共享在不带标题的更新里沿用，换了一场则清空", "[sscard][shareroom]")
{
    balloonscreenshare::ScreenShareStatusCache cache;
    balloonscreenshare::ScreenShareActiveInfo info;
    cache.Update(conversation_group, kGroupId, "ss-7", kHostId, L"张三", true, false, false, L"周会演示");
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_title == L"周会演示");
    //老服务端的查询应答不带标题：同一场沿用
    cache.Update(conversation_group, kGroupId, "ss-7", kHostId, L"", true, false, false, L"");
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_title == L"周会演示");
    //发起人改了标题
    cache.Update(conversation_group, kGroupId, "ss-7", kHostId, L"", true, false, false, L"新标题");
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_title == L"新标题");
    //换了一场、又没带标题：不沿用上一场的
    cache.Update(conversation_group, kGroupId, "ss-8", kHostId, L"", true, false, false, L"");
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_title.empty());
}

namespace {

//SSTIC11~14：群里那张共享卡片的消息 id
const int64_t kSsInfoCardMsgId = 9007199254741031LL;

//改写前后的卡片 content（服务端 ScreenShareProtocol::buildCardContent 的输出）
const char* const kSsInfoBefore =
    u8"{\"desc\":\"第三季度汇报\",\"host\":\"10001\",\"hostname\":\"张三\",\"roomid\":\"ss-7\","
    u8"\"starttime\":\"1759370000000\",\"title\":\"周会演示\"}";
const char* const kSsInfoAfter =
    u8"{\"desc\":\"\",\"host\":\"10001\",\"hostname\":\"张三\",\"roomid\":\"ss-7\","
    u8"\"starttime\":\"1759370000000\",\"title\":\"周会演示（改）\"}";

//紧凑写法输出一个 JSON 值
std::string SsCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//JSON 文本 → JSON 值
Json::Value SsParse(const std::string& text)
{
    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(text.c_str(), text.c_str() + text.size(), &v, &errs);
    delete reader;
    return v;
}

//临时库的全路径：带上进程号，两份测试进程同时跑时不会共用一个文件；先删掉同名旧文件
std::wstring SsFreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//照服务端同步下发的格式拼一条群聊共享卡片消息（content 为改写前的内容）
std::string SsCardMessageJson()
{
    Json::Value msg;
    msg["msgid"] = std::to_string(kSsInfoCardMsgId);
    msg["ctype"] = conversation_group;
    msg["cid"] = std::to_string(kGroupId);
    msg["from"] = std::to_string(kHostId);
    msg["to"] = std::to_string(kGroupId);
    msg["msgtype"] = message_type_screenshare;
    msg["content"] = SsParse(kSsInfoBefore);
    msg["timestamp"] = static_cast<Json::Int64>(kStartTime);
    return SsCompact(msg);
}

//一次 1734 推送：改了标题、清空了描述
balloonmain::ScreenShareInfoUpdate SsInfoUpdate()
{
    balloonmain::ScreenShareInfoUpdate update;
    update.m_roomId = "ss-7";
    update.m_ctype = conversation_group;
    update.m_cid = kGroupId;
    update.m_msgid = kSsInfoCardMsgId;
    update.m_content = kSsInfoAfter;
    return update;
}

//从卡片 content 原文里取标题（UTF-8）
std::string SsTitleOf(const std::string& content)
{
    net::ScreenShareCardContent card;
    if (!net::MessageProtocolClient::parseScreenShareCardContent(content, card))
        return std::string("<bad>");
    return card.m_title;
}

} // namespace

TEST_CASE("SSTIC11 共享中改了标题与描述的推送（1734）的解析：房间号、会话、卡片 msgid 与改写后的 content", "[sscard][shareroom]")
{
    Json::Value root;
    root["roomid"] = "ss-7";
    root["ctype"] = conversation_group;
    root["cid"] = std::to_string(kGroupId);
    root["title"] = u8"周会演示（改）";
    root["desc"] = "";
    root["msgid"] = std::to_string(kSsInfoCardMsgId);
    root["content"] = kSsInfoAfter;

    balloonmain::ScreenShareInfoUpdate update;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(root), update));
    CHECK(update.m_roomId == "ss-7");
    CHECK(update.m_ctype == conversation_group);
    CHECK(update.m_cid == kGroupId);
    CHECK(update.m_title == u8"周会演示（改）");
    CHECK(update.m_msgid == kSsInfoCardMsgId);
    CHECK(update.m_content == kSsInfoAfter);

    //64 位 id 也接受整数写法
    Json::Value asInts = root;
    asInts["cid"] = static_cast<Json::Int64>(kGroupId);
    asInts["msgid"] = static_cast<Json::Int64>(kSsInfoCardMsgId);
    balloonmain::ScreenShareInfoUpdate fromInts;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(asInts), fromInts));
    CHECK(fromInts.m_cid == kGroupId);
    CHECK(fromInts.m_msgid == kSsInfoCardMsgId);

    //服务端没能改写卡片：不带 msgid 与 content，仍算解析成功
    Json::Value noCard = root;
    noCard.removeMember("msgid");
    noCard.removeMember("content");
    balloonmain::ScreenShareInfoUpdate withoutCard;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(noCard), withoutCard));
    CHECK(withoutCard.m_msgid == 0);
    CHECK(withoutCard.m_content.empty());

    //不是 JSON、缺房间号、会话 id 不对：解析失败
    balloonmain::ScreenShareInfoUpdate bad;
    CHECK_FALSE(balloonmain::ParseScreenShareInfoUpdate("not json", bad));
    Json::Value noRoom = root;
    noRoom.removeMember("roomid");
    CHECK_FALSE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(noRoom), bad));
    Json::Value badCid = root;
    badCid["cid"] = "abc";
    CHECK_FALSE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(badCid), bad));
}

TEST_CASE("SSTIC12 聊天窗里的卡片换成新的标题与描述，副标题、状态、按钮不变", "[sscard][shareroom]")
{
    ChatMessage card = Bubble(false);
    ApplyScreenShareCard(card, kSsInfoBefore, L"张三", kScreenShareRoomActive);
    const ChatMessage before = card;
    REQUIRE(card.screenShareTitle == L"周会演示");
    REQUIRE(card.screenShareDesc == L"第三季度汇报");

    CHECK(ApplyScreenShareInfo(card, kSsInfoAfter));
    CHECK(card.screenShareTitle == L"周会演示（改）");
    CHECK(card.screenShareDesc.IsEmpty());
    CHECK(card.screenShareContent == kSsInfoAfter);
    CHECK(card.screenShareSubtitle == before.screenShareSubtitle);
    CHECK(card.screenShareHostName == L"张三");
    CHECK(card.screenShareState == kScreenShareRoomActive);
    CHECK(card.screenShareStatus == before.screenShareStatus);
    CHECK(card.screenShareButtonEnabled);
    CHECK(card.kind == kMsgScreenShare);

    //同样的内容再来一次：没有变化
    CHECK_FALSE(ApplyScreenShareInfo(card, kSsInfoAfter));
    //内容不可用：不改
    CHECK_FALSE(ApplyScreenShareInfo(card, "not json"));
    CHECK(card.screenShareTitle == L"周会演示（改）");
    //不是屏幕共享卡片：不改
    ChatMessage text = Bubble(false);
    text.text = L"你好";
    CHECK_FALSE(ApplyScreenShareInfo(text, kSsInfoAfter));
    CHECK(text.kind == kMsgText);
    CHECK(text.text == L"你好");
}

TEST_CASE("SSTIC13 本地库里的卡片：改写 content 列，还原出来是新标题，原始JSON不动", "[sscard][shareroom]")
{
    const std::wstring path = SsFreshTempDb(L"flamingo_sstic13.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(SsCardMessageJson(), received));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(received, conversation_group, kGroupId)));

    bool hit = false;
    REQUIRE(balloonmain::ApplyScreenShareInfoToStore(store, SsInfoUpdate(), hit));
    CHECK(hit);

    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kSsInfoCardMsgId, row, found));
    REQUIRE(found);
    //content 列与还原结果都是新标题
    std::string fromColumn;
    REQUIRE(balloonhistory::ScreenShareContentFromRecord(row, fromColumn));
    CHECK(SsTitleOf(fromColumn) == u8"周会演示（改）");
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(row, restored));
    CHECK(SsTitleOf(restored.m_screenShareContent) == u8"周会演示（改）");
    //原始JSON仍是服务端当初发来的样子
    net::MessageNotify fromRaw;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, fromRaw));
    CHECK(SsTitleOf(fromRaw.m_screenShareContent) == u8"周会演示");

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SSTIC14 本地没有这张卡片、推送不带卡片时不报错也不建行；content 列没有卡片内容的旧行退回原始JSON", "[sscard][shareroom]")
{
    const std::wstring path = SsFreshTempDb(L"flamingo_sstic14.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //本地没有这张卡片
    bool hit = true;
    REQUIRE(balloonmain::ApplyScreenShareInfoToStore(store, SsInfoUpdate(), hit));
    CHECK_FALSE(hit);
    balloonhistory::HistoryRecord row;
    bool found = true;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kSsInfoCardMsgId, row, found));
    CHECK_FALSE(found);

    //推送不带卡片（服务端没能改写）
    balloonmain::ScreenShareInfoUpdate noCard = SsInfoUpdate();
    noCard.m_msgid = 0;
    noCard.m_content.clear();
    hit = true;
    REQUIRE(balloonmain::ApplyScreenShareInfoToStore(store, noCard, hit));
    CHECK_FALSE(hit);

    //content 列没有卡片内容的旧行：取原始JSON里的内容
    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(SsCardMessageJson(), received));
    balloonhistory::HistoryRecord old = balloonhistory::RecordFromNotify(received, conversation_group, kGroupId);
    old.m_content = "{}";
    std::string content;
    REQUIRE(balloonhistory::ScreenShareContentFromRecord(old, content));
    CHECK(SsTitleOf(content) == u8"周会演示");
    //两处都没有：取不到
    old.m_raw.clear();
    CHECK_FALSE(balloonhistory::ScreenShareContentFromRecord(old, content));

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SSTIC15 移交主持人后的 1734 带新主持人：解析出 hostid 与 hostname，不带时为 0 与空", "[sscard][shareroom]")
{
    //2026-10-08：服务端移交主持人后经 1734 告诉全体会话成员新主持人是谁，不带卡片
    Json::Value root;
    root["roomid"] = "ss-7";
    root["ctype"] = conversation_group;
    root["cid"] = std::to_string(kGroupId);
    root["title"] = u8"张勇的屏幕共享";
    root["desc"] = "";
    root["hostid"] = "5308";
    root["hostname"] = u8"韩晓燕888";

    balloonmain::ScreenShareInfoUpdate update;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(root), update));
    CHECK(update.m_roomId == "ss-7");
    CHECK(update.m_hostId == 5308);
    CHECK(update.m_hostName == u8"韩晓燕888");
    CHECK(update.m_msgid == 0);
    CHECK(update.m_content.empty());

    //hostid 也接受整数写法
    Json::Value asInt = root;
    asInt["hostid"] = static_cast<Json::Int64>(5308);
    balloonmain::ScreenShareInfoUpdate fromInt;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(asInt), fromInt));
    CHECK(fromInt.m_hostId == 5308);

    //服务端查不到新主持人的资料：hostname 为空串，主持人照样取出
    Json::Value noName = root;
    noName["hostname"] = "";
    balloonmain::ScreenShareInfoUpdate withoutName;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(noName), withoutName));
    CHECK(withoutName.m_hostId == 5308);
    CHECK(withoutName.m_hostName.empty());

    //修改标题与描述的那种 1734、老服务端：不带 hostid 与 hostname
    Json::Value plain = root;
    plain.removeMember("hostid");
    plain.removeMember("hostname");
    balloonmain::ScreenShareInfoUpdate withoutHost;
    REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(plain), withoutHost));
    CHECK(withoutHost.m_hostId == 0);
    CHECK(withoutHost.m_hostName.empty());

    //hostid 不是正数或不是数字：按不带处理，推送本身仍有效（标题照样更新）
    const char* badIds[] = { "0", "-3", "abc" };
    for (size_t i = 0; i < sizeof(badIds) / sizeof(badIds[0]); ++i)
    {
        Json::Value bad = root;
        bad["hostid"] = badIds[i];
        balloonmain::ScreenShareInfoUpdate u;
        REQUIRE(balloonmain::ParseScreenShareInfoUpdate(SsCompact(bad), u));
        CHECK(u.m_hostId == 0);
        CHECK(u.m_hostName.empty());
    }
}

TEST_CASE("SSTIC16 状态缓存按 1734 更新这一场共享：带新主持人时换主持人，不带时沿用；不是这一场时不改动", "[sscard][shareroom]")
{
    //2026-10-08：不在房间里的会话成员靠这一步把横幅上的主持人换成新主持人
    balloonscreenshare::ScreenShareStatusCache cache;
    cache.Update(conversation_group, kGroupId, "ss-7", 5127, L"张勇", true, true, false, L"张勇的屏幕共享");
    balloonscreenshare::ScreenShareActiveInfo info;

    //带新主持人与名字：主持人与名字换掉，标题、是否需要密码、是否强制模式沿用
    CHECK(cache.ApplyInfo(conversation_group, kGroupId, "ss-7", L"张勇的屏幕共享", 5308, L"韩晓燕888"));
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_roomId == "ss-7");
    CHECK(info.m_hostId == 5308);
    CHECK(info.m_hostName == L"韩晓燕888");
    CHECK(info.m_title == L"张勇的屏幕共享");
    CHECK(info.m_locked);
    CHECK_FALSE(info.m_forced);

    //带新主持人但名字为空：名字留空，由聊天窗按 userid 取名字，不沿用上一位主持人的名字
    CHECK(cache.ApplyInfo(conversation_group, kGroupId, "ss-7", L"", 5310, L""));
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_hostId == 5310);
    CHECK(info.m_hostName.empty());
    //标题为空：沿用
    CHECK(info.m_title == L"张勇的屏幕共享");

    //不带主持人（修改标题与描述）：只换标题，主持人与名字沿用（第 15 期的原有行为）
    cache.Update(conversation_group, kGroupId, "ss-7", 5308, L"韩晓燕888", true, false, false, L"旧标题");
    CHECK(cache.ApplyInfo(conversation_group, kGroupId, "ss-7", L"新标题", 0, L""));
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_title == L"新标题");
    CHECK(info.m_hostId == 5308);
    CHECK(info.m_hostName == L"韩晓燕888");

    //标题为空且不带主持人：没有可更新的内容
    CHECK_FALSE(cache.ApplyInfo(conversation_group, kGroupId, "ss-7", L"", 0, L""));

    //房间号不是缓存记着的那一场：不改动
    CHECK_FALSE(cache.ApplyInfo(conversation_group, kGroupId, "ss-other", L"别的标题", 9999, L"别人"));
    REQUIRE(cache.GetActive(conversation_group, kGroupId, info));
    CHECK(info.m_roomId == "ss-7");
    CHECK(info.m_hostId == 5308);
    CHECK(info.m_title == L"新标题");

    //缓存里没有这个会话、或这个会话的共享已结束：不改动，也不新建记录
    CHECK_FALSE(cache.ApplyInfo(conversation_group, kGroupId + 1, "ss-7", L"标题", 5308, L"韩晓燕888"));
    CHECK_FALSE(cache.GetActive(conversation_group, kGroupId + 1, info));
    cache.Update(conversation_single, kPeerId, "ss-8", kPeerId, L"", false);
    CHECK_FALSE(cache.ApplyInfo(conversation_single, kPeerId, "ss-8", L"标题", 5308, L"韩晓燕888"));
    CHECK_FALSE(cache.GetActive(conversation_single, kPeerId, info));
}
