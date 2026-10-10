/**
 *  群公告卡片与群备忘卡片的单元测试（Suite AC-C 标签 [announcecard]，Suite MC-C 标签 [memocard]），GroupInfoCardTests.cpp
 *
 *  群公告卡片（msgtype=22，docs/群公告接入方案.md §13.7）：
 *    - AC-C1 卡片 content 解析：正常、缺字段、已删除形态、不是 JSON；整条消息解析后 content 原文保存在 m_announcementContent，
 *            兜底用的 text 不会被当成正文；
 *    - AC-C2 显示文字：三档优先级、正文进描述块、副标题的几种写法（本人 / 别人 / 重新发布 / 取不到名字）、已删除形态不可点；
 *    - AC-C3 摘要一致：实时消息、会话列表 digestarg、消息记录窗预览三处都是「[群公告] 标题」；
 *    - AC-C4 1326 删除推送解析出 cardmsgid 与改写后的卡片（老服务端不带时为 0）；对着真的 SQLite 库改写本地那一行：
 *            content 列、原始JSON与搜索文本都换成已删除形态，置顶等其余列不变，本地没有这张卡片时不报错；
 *    - AC-C5 右键菜单与消息记录窗的行菜单：没有撤回、转发、Pin。
 *  群备忘卡片（msgtype=23，docs/备忘与提醒方案.md §7.6）：
 *    - MC-C1 卡片 content 解析：正常、缺字段、不是 JSON；整条消息解析后 content 原文保存在 m_memoCardContent；
 *    - MC-C2 显示文字：「新建」「修改」的副标题，时间与重复规则，「提醒 N 人」与「提醒全体成员」，有无备注两种情况；
 *            措辞与群「备忘」页签卡片一致（MemoViewModel 的同名函数）；
 *    - MC-C3 失效判定：备忘簿未加载时为可用，已加载且有这一条为可用，已加载且没有这一条为失效；可用与失效来回切换；
 *    - MC-C4 能否点开：可用时可以，失效时与内容不可用时不可以；
 *    - MC-C5 三处摘要一致，消息记录窗的行菜单没有转发与 Pin。
 *  ChatBubble 把这两种卡片画成屏幕共享卡片的版式，几何与菜单规则由 ScreenShareCardTests 覆盖，这里只核对 kind 与标志位。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatMessageList.h"     // AC-C6、INFOSNAP：把卡片排进一个列表里测量与绘制
#include "../UI/BalloonChatWnd/ChatBubble.h"          // AC-C6：取卡片那一行的行高

#include "../UI/BalloonChatWnd/ChatAnnouncementCard.h"
#include "../UI/BalloonChatWnd/ChatMemoCard.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonHistory/HistoryView.h"         // PreviewFromContent：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"      // 消息记录窗的行菜单与转发判定
#include "../UI/BalloonHistory/V2MsgQuery.h"          // RecordFromNotify / NotifyFromRecord / AnnouncementContentFromRecord
#include "../UI/BalloonHistory/V2MsgStore.h"          // AC-C4：真的 SQLite 库
#include "../UI/BalloonMain/AnnouncementCardApply.h"  // AC-C4：写库
#include "../Feature/Memo/MemoRule.h"                 // MC-C2：本地分钟序号
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../net/GroupProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"

using namespace balloonchatwnd;

namespace {

//群、发布人（别人）、本人
const long long kGroupId     = 268435463LL;
const long long kPublisherId = 100023;
//公告 id、卡片消息的 msgid 与发布时间（Unix 毫秒）
const long long kAnnounceId  = 15;
const long long kCardMsgId   = 9101;
const long long kPublishTime = 1785000000000LL;
//备忘卡片：备忘 id、卡片 msgid、操作人
const long long kMemoId      = 12;
const long long kMemoMsgId   = 9102;
const long long kEditorId    = 5127;
//备忘卡片里被提醒的人数
const int kTargetCount = 3;

//服务端 GroupProtocol::buildAnnouncementCardContent 的输出（jsoncpp 紧凑写法，键按字母序），优先级「重要」
const char* const kAnnounceCard =
    u8"{\"announcementid\":\"15\",\"body\":\"请各组提前完成回归测试\",\"groupid\":\"268435463\","
    u8"\"publisher\":\"林知夏\",\"publisherid\":\"100023\",\"publishtime\":1785000000000,\"republish\":false,"
    u8"\"severity\":1,\"text\":\"[群公告] v3.2 发布周五上线\",\"title\":\"v3.2 发布周五上线\"}";

//同一张卡片被服务端改写后的已删除形态（GroupProtocol::buildDeletedAnnouncementCardContent）
const char* const kAnnounceCardDeleted =
    u8"{\"announcementid\":\"15\",\"deleted\":true,\"groupid\":\"268435463\","
    u8"\"publisher\":\"林知夏\",\"publisherid\":\"100023\",\"publishtime\":1785000000000,\"republish\":false,"
    u8"\"severity\":1,\"text\":\"[群公告已删除] v3.2 发布周五上线\",\"title\":\"v3.2 发布周五上线\"}";

//服务端 MemoProtocol::buildMemoCardContent 的输出：新建、只提醒 3 人、每周一 09:30、提前 10 分钟、带备注
const char* const kMemoCard =
    u8"{\"advance\":10,\"change\":1,\"count\":0,\"editor\":\"张三\",\"editorid\":\"5127\",\"groupid\":\"268435463\","
    u8"\"memoid\":\"12\",\"note\":\"带上周报\",\"repeat\":3,\"start\":\"2026-10-05 09:30\",\"targetall\":false,"
    u8"\"targetcount\":3,\"text\":\"[群备忘] 周会\",\"title\":\"周会\",\"weekdays\":1}";

//一条普通气泡数据：对方发来或本人发出
ChatMessage Bubble(bool fromMe, long long msgid)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = msgid;
    return msg;
}

//JSON 文本 → JSON 值
Json::Value IcParse(const std::string& text)
{
    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(text.c_str(), text.c_str() + text.size(), &v, &errs);
    delete reader;
    return v;
}

//JSON 值 → 紧凑文本
std::string IcCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//照服务端推送的格式拼一条群聊卡片消息
std::string GroupCardMessageJson(long long msgid, long long from, int msgtype, const char* content)
{
    Json::Value msg;
    msg["msgid"] = std::to_string(msgid);
    msg["clientmsgid"] = "card-" + std::to_string(msgid);
    msg["ctype"] = conversation_group;
    msg["cid"] = std::to_string(kGroupId);
    msg["from"] = std::to_string(from);
    msg["to"] = std::to_string(kGroupId);
    msg["msgtype"] = msgtype;
    msg["content"] = IcParse(content);
    msg["timestamp"] = static_cast<Json::Int64>(kPublishTime);
    return IcCompact(msg);
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

//临时库的全路径：带上进程号，两份测试进程同时跑时不会共用一个文件；先删掉同名旧文件
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
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//AC-C6：两种消息区宽度（逻辑像素）。窄的比卡片加头像还窄，宽的是聊天窗常见宽度；消息区高度放得下几张卡片
const int kNarrowListW = 300;
const int kWideListW   = 900;
const int kListH       = 1400;

//INFOSNAP：效果图尺寸与相对 exe（Debug_Win32\Tests）的输出路径，落在仓库的 tmp\anncard\ 下
const int kSnapW = 720;
const int kSnapH = 1400;
const wchar_t* const kSnapPath = L"\\..\\..\\..\\tmp\\anncard\\info_cards.png";

//AC-C6：很长的标题与正文（伪语言把文字拉长一倍以上的情形）
const wchar_t* const kLongText =
    L"一段非常非常长的文字用来检查卡片的标题行与描述块会不会以省略号收尾而不是换行把按钮挤出卡片之外再多写几个字";

//把一张卡片排进消息列表，按给定宽度排版，返回卡片那一行的行高
int CardRowHeight(const ChatMessage& card, int listWidth)
{
    ChatMessageList list;
    list.AppendMessage(card);
    RECT all = { 0, 0, listWidth, kListH };
    list.ForceLayout(all);
    ChatBubble* bubble = dynamic_cast<ChatBubble*>(list.Children().back().get());
    return (bubble != NULL) ? bubble->GetRowHeight() : -1;
}

//取 PNG 编码器
bool FindPngEncoder(CLSID& clsid)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(info[i].MimeType, L"image/png") == 0)
        {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

//MC-C2 用的「现在」：2026-10-01 08:00，与卡片的开始时间不在同一天、同一年
memo::LocalMinute NowForMemo()
{
    const int kYear = 2026;
    const int kMonth = 10;
    const int kDay = 1;
    const int kHour = 8;
    return memo::LocalMinuteFromParts(kYear, kMonth, kDay, kHour, 0);
}

} // namespace

// ============================== 群公告卡片 ==============================

TEST_CASE("AC-C1 群公告卡片 content 解析：正常、缺字段、已删除形态、不是 JSON", "[announcecard]")
{
    AnnouncementCardInfo info;
    REQUIRE(ParseAnnouncementCard(kAnnounceCard, info));
    CHECK(info.m_announcementId == kAnnounceId);
    CHECK(info.m_groupId == kGroupId);
    CHECK(info.m_title == L"v3.2 发布周五上线");
    CHECK(info.m_body == L"请各组提前完成回归测试");
    CHECK(info.m_severity == group_announcement_important);
    CHECK(info.m_publisherId == kPublisherId);
    CHECK(info.m_publisherName == L"林知夏");
    CHECK(info.m_publishTimeMs == kPublishTime);
    CHECK_FALSE(info.m_republish);
    CHECK_FALSE(info.m_deleted);

    AnnouncementCardInfo deleted;
    REQUIRE(ParseAnnouncementCard(kAnnounceCardDeleted, deleted));
    CHECK(deleted.m_deleted);
    CHECK(deleted.m_body.IsEmpty());
    CHECK(deleted.m_title == L"v3.2 发布周五上线");

    //缺字段：只有 id 也能解析，其余取默认值；缺 id、id 非正、不是 JSON 都失败
    AnnouncementCardInfo minimal;
    REQUIRE(ParseAnnouncementCard(u8"{\"announcementid\":\"7\"}", minimal));
    CHECK(minimal.m_severity == group_announcement_normal);
    CHECK(minimal.m_title.IsEmpty());
    AnnouncementCardInfo bad;
    CHECK_FALSE(ParseAnnouncementCard(u8"{\"title\":\"x\"}", bad));
    CHECK_FALSE(ParseAnnouncementCard(u8"{\"announcementid\":\"0\"}", bad));
    CHECK_FALSE(ParseAnnouncementCard("not json", bad));
    CHECK_FALSE(ParseAnnouncementCard("", bad));

    //整条消息解析：content 原文保存下来，兜底的 text 不会被当成正文
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        GroupCardMessageJson(kCardMsgId, kPublisherId, message_type_announcement, kAnnounceCard), notify));
    CHECK(notify.m_msgtype == message_type_announcement);
    CHECK_FALSE(notify.m_announcementContent.empty());
    CHECK(notify.m_text.empty());
    AnnouncementCardInfo fromNotify;
    REQUIRE(ParseAnnouncementCard(notify.m_announcementContent, fromNotify));
    CHECK(fromNotify.m_announcementId == kAnnounceId);
}

TEST_CASE("AC-C2 群公告卡片的显示文字：优先级、正文、副标题的几种写法、已删除形态", "[announcecard]")
{
    CHECK(AnnouncementSeverityText(group_announcement_normal) == L"通知");
    CHECK(AnnouncementSeverityText(group_announcement_important) == L"重要");
    CHECK(AnnouncementSeverityText(group_announcement_urgent) == L"紧急");
    //越界值按「通知」处理
    const int kUnknownSeverity = 9;
    CHECK(AnnouncementSeverityText(kUnknownSeverity) == L"通知");

    CHECK(AnnouncementCardSubtitle(L"林知夏", false, false) == L"林知夏发布的群公告");
    CHECK(AnnouncementCardSubtitle(L"林知夏", false, true) == L"林知夏重新发布的群公告");
    CHECK(AnnouncementCardSubtitle(L"林知夏", true, false) == L"我发布的群公告");
    CHECK(AnnouncementCardSubtitle(L"", true, true) == L"我重新发布的群公告");
    CHECK(AnnouncementCardSubtitle(L"", false, false) == L"群公告");

    //别人发的：宿主给的名字优先；按钮可点
    ChatMessage msg = Bubble(false, kCardMsgId);
    ApplyAnnouncementCard(msg, kAnnounceCard, L"知夏（群名片）");
    CHECK(msg.kind == kMsgScreenShare);
    CHECK(msg.screenShareIsAnnouncement);
    CHECK_FALSE(msg.screenShareIsMemo);
    CHECK_FALSE(msg.screenShareIsGroupCall);
    CHECK_FALSE(msg.screenShareIsCallCard);
    CHECK(msg.screenShareRoomId.empty());
    CHECK(msg.infoCardRefId == kAnnounceId);
    CHECK(msg.screenShareTitle == L"v3.2 发布周五上线");
    CHECK(msg.screenShareSubtitle == L"知夏（群名片）发布的群公告");
    CHECK(msg.screenShareDesc == L"请各组提前完成回归测试");
    CHECK(msg.screenShareStatus == L"优先级：重要");
    CHECK(msg.text == L"[群公告] v3.2 发布周五上线");
    CHECK(msg.screenShareHasButton);
    CHECK(msg.screenShareButtonEnabled);
    CHECK(IsAnnouncementCardOpenable(msg));

    //宿主取不到名字：退回服务端写进卡片的昵称
    ChatMessage fallback = Bubble(false, kCardMsgId);
    ApplyAnnouncementCard(fallback, kAnnounceCard, L"");
    CHECK(fallback.screenShareSubtitle == L"林知夏发布的群公告");

    //已删除形态：没有正文，状态行写明，不可点
    ChatMessage gone = Bubble(false, kCardMsgId);
    ApplyAnnouncementCard(gone, kAnnounceCardDeleted, L"");
    CHECK(gone.screenShareTitle == L"v3.2 发布周五上线");
    CHECK(gone.screenShareDesc.IsEmpty());
    CHECK(gone.screenShareStatus == L"该公告已被删除");
    CHECK_FALSE(gone.screenShareButtonEnabled);
    CHECK_FALSE(IsAnnouncementCardOpenable(gone));

    //内容不可用：标题写类型，没有按钮
    ChatMessage broken = Bubble(true, kCardMsgId);
    ApplyAnnouncementCard(broken, "not json", L"");
    CHECK(broken.kind == kMsgScreenShare);
    CHECK(broken.screenShareIsAnnouncement);
    CHECK(broken.infoCardRefId == 0);
    CHECK(broken.screenShareTitle == L"群公告");
    CHECK_FALSE(broken.screenShareHasButton);
    CHECK_FALSE(IsAnnouncementCardOpenable(broken));
}

TEST_CASE("AC-C3 群公告卡片的摘要：实时消息、会话列表、消息记录窗三处一致", "[announcecard]")
{
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_announcement, std::string()) == u8"[群公告]");
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        GroupCardMessageJson(kCardMsgId, kPublisherId, message_type_announcement, kAnnounceCard), notify));
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[群公告] v3.2 发布周五上线");
    net::MessageNotify noContent = notify;
    noContent.m_announcementContent.clear();
    CHECK(net::MessageProtocolClient::digestForNotify(noContent) == u8"[群公告]");

    //会话列表：服务端 digest 故意写成别的文字，本地按 digestarg 生成
    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":2,\"cid\":\"268435463\",\"lastmsgid\":\"9101\",\"lastmsg\":{\"from\":\"100023\",\"msgtype\":22,"
        u8"\"digest\":\"S1\",\"digestarg\":\"v3.2 发布周五上线\",\"timestamp\":1}},"
        u8"{\"ctype\":2,\"cid\":\"268435464\",\"lastmsgid\":\"9102\",\"lastmsg\":{\"from\":\"100023\",\"msgtype\":22,"
        u8"\"digest\":\"S2\",\"timestamp\":1}}"
        u8"]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 2);
    CHECK(out.m_items[0].m_digest == u8"[群公告] v3.2 发布周五上线");
    CHECK(out.m_items[1].m_digest == u8"[群公告]");

    //消息记录窗：预览带标题，content 列取不到时只写类型
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_group, kGroupId);
    CHECK(rec.m_content.find("\"announcement\"") != std::string::npos);
    CHECK(balloonhistory::PreviewFromContent(message_type_announcement, rec.m_content) == u8"[群公告] v3.2 发布周五上线");
    CHECK(balloonhistory::PreviewFromContent(message_type_announcement, "{}") == u8"[群公告]");
    //本地搜索文本：标题与正文
    CHECK(rec.m_searchText.find(u8"回归测试") != std::string::npos);
}

TEST_CASE("AC-C4 1326 删除推送带卡片，本地库里那张卡片换成已删除形态", "[announcecard]")
{
    //推送解析：删除推送带 cardmsgid 与 card
    const std::string delBody =
        std::string(u8"{\"changetype\":2,\"groupid\":\"268435463\",\"announcement\":{\"id\":\"15\",\"groupid\":\"268435463\","
                    u8"\"title\":\"v3.2 发布周五上线\",\"content\":\"\",\"severity\":1,\"publisher\":\"100023\","
                    u8"\"publishername\":\"林知夏\",\"publishtime\":1785000000000},\"cardmsgid\":\"9101\",\"card\":")
        + kAnnounceCardDeleted + "}";
    net::GroupAnnouncementNotify notify;
    REQUIRE(net::GroupProtocolClient::parseAnnouncementNotify(delBody, notify));
    CHECK(notify.m_changetype == group_announcement_change_delete);
    CHECK(notify.m_cardMsgId == kCardMsgId);
    AnnouncementCardInfo pushed;
    REQUIRE(ParseAnnouncementCard(notify.m_cardContent, pushed));
    CHECK(pushed.m_deleted);

    //老服务端不带这两个字段、或只带一个：一律视为没有卡片
    const std::string oldBody =
        u8"{\"changetype\":2,\"groupid\":\"268435463\",\"announcement\":{\"id\":\"15\",\"title\":\"t\"}}";
    net::GroupAnnouncementNotify oldNotify;
    REQUIRE(net::GroupProtocolClient::parseAnnouncementNotify(oldBody, oldNotify));
    CHECK(oldNotify.m_cardMsgId == 0);
    CHECK(oldNotify.m_cardContent.empty());
    const std::string halfBody =
        u8"{\"changetype\":2,\"groupid\":\"268435463\",\"announcement\":{\"id\":\"15\",\"title\":\"t\"},\"cardmsgid\":\"9101\"}";
    net::GroupAnnouncementNotify halfNotify;
    REQUIRE(net::GroupProtocolClient::parseAnnouncementNotify(halfBody, halfNotify));
    CHECK(halfNotify.m_cardMsgId == 0);

    //真的 SQLite 库
    const std::wstring path = FreshTempDb(L"flamingo_acc4.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //本地还没有这张卡片：不报错也不建行
    bool hit = true;
    REQUIRE(balloonmain::ApplyAnnouncementCardToStore(store, kGroupId, kCardMsgId, notify.m_cardContent, hit));
    CHECK_FALSE(hit);

    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        GroupCardMessageJson(kCardMsgId, kPublisherId, message_type_announcement, kAnnounceCard), received));
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(received, conversation_group, kGroupId);
    REQUIRE(store.Upsert(rec));
    //置顶状态只经 SetPinned 写入（Upsert 不写 pinned / pin_time 两列），先钉住，检查改写之后仍在
    const long long kPinTime = 1785000001000LL;
    bool pinHit = false;
    REQUIRE(store.SetPinned(conversation_group, kGroupId, kCardMsgId, true, kPinTime, pinHit));
    REQUIRE(pinHit);

    REQUIRE(balloonmain::ApplyAnnouncementCardToStore(store, kGroupId, kCardMsgId, notify.m_cardContent, hit));
    CHECK(hit);

    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kCardMsgId, row, found));
    REQUIRE(found);
    //三处都换成了已删除形态：content 列、原始JSON、搜索文本
    std::string fromColumn;
    REQUIRE(balloonhistory::AnnouncementContentFromRecord(row, fromColumn));
    AnnouncementCardInfo columnInfo;
    REQUIRE(ParseAnnouncementCard(fromColumn, columnInfo));
    CHECK(columnInfo.m_deleted);
    net::MessageNotify fromRaw;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, fromRaw));
    CHECK(fromRaw.m_announcementContent.find(u8"回归测试") == std::string::npos);
    CHECK(row.m_searchText.find(u8"回归测试") == std::string::npos);
    CHECK(row.m_searchText.find(u8"周五上线") != std::string::npos);
    //从本地库还原：已删除形态
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(row, restored));
    AnnouncementCardInfo restoredInfo;
    REQUIRE(ParseAnnouncementCard(restored.m_announcementContent, restoredInfo));
    CHECK(restoredInfo.m_deleted);
    //其余列不变
    CHECK(row.m_pinned);
    CHECK(row.m_pinTime == kPinTime);
    CHECK(row.m_fromid == kPublisherId);

    //参数非法、卡片不是 JSON 对象：失败
    CHECK_FALSE(balloonmain::ApplyAnnouncementCardToStore(store, kGroupId, kCardMsgId, "not json", hit));
    CHECK_FALSE(balloonmain::ApplyAnnouncementCardToStore(store, 0, kCardMsgId, notify.m_cardContent, hit));

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AC-C5 群公告卡片的菜单：没有撤回、转发、Pin", "[announcecard]")
{
    //聊天窗：卡片复用屏幕共享卡片的 kind，菜单规则相同
    ChatMessage msg = Bubble(false, kCardMsgId);
    ApplyAnnouncementCard(msg, kAnnounceCard, L"");
    CHECK(msg.kind == kChatMenuKindScreenShare);
    CHECK_FALSE(CanForwardMessage(kChatMenuKindScreenShare, kCardMsgId, 0));
    CHECK_FALSE(CanMergeForwardMessage(kChatMenuKindScreenShare, kCardMsgId, 0));
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindScreenShare, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(HasMenuId(model, kChatMsgMenuDelete));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuRecall));

    //消息记录窗
    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_announcement, kCardMsgId));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_announcement;
    ctx.m_msgId = kCardMsgId;
    const std::vector<balloonhistory::HistoryRowMenuItem> rowModel = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < rowModel.size(); ++i)
    {
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}

TEST_CASE("AC-C6 卡片布局：窄宽度下行高不变，文字很长时不换行，描述最多两行", "[announcecard][memocard]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessage announce = Bubble(false, kCardMsgId);
        ApplyAnnouncementCard(announce, kAnnounceCard, L"");
        const int wide = CardRowHeight(announce, kWideListW);
        REQUIRE(wide > 0);
        //卡片固定宽度：窄宽度下不会被压扁或撑高
        CHECK(CardRowHeight(announce, kNarrowListW) == wide);
        //标题、副标题很长时以省略号收尾；正文很长时描述块至多两行，行高与一行正文时相比至多多一行
        ChatMessage longAnnounce = announce;
        longAnnounce.screenShareTitle = kLongText;
        longAnnounce.screenShareSubtitle = kLongText;
        longAnnounce.screenShareDesc = CString(kLongText) + kLongText + kLongText;
        const int longH = CardRowHeight(longAnnounce, kWideListW);
        CHECK(longH >= wide);
        ChatMessage twoLines = announce;
        twoLines.screenShareDesc = CString(kLongText) + L"\n" + kLongText + L"\n" + kLongText;
        CHECK(CardRowHeight(twoLines, kWideListW) == longH);
        //已删除形态没有正文：比有正文的矮
        ChatMessage gone = Bubble(false, kCardMsgId);
        ApplyAnnouncementCard(gone, kAnnounceCardDeleted, L"");
        CHECK(CardRowHeight(gone, kWideListW) < wide);

        //群备忘卡片：描述两行（时间与重复规则、备注），窄宽度下行高同样不变
        ChatMessage memoCard = Bubble(false, kMemoMsgId);
        ApplyMemoCard(memoCard, kMemoCard, L"", NowForMemo());
        const int memoH = CardRowHeight(memoCard, kWideListW);
        REQUIRE(memoH > 0);
        CHECK(CardRowHeight(memoCard, kNarrowListW) == memoH);
        //失效后只换状态文字，行高不变
        SetMemoCardAvailable(memoCard, false);
        CHECK(CardRowHeight(memoCard, kWideListW) == memoH);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("INFOSNAP group announcement and memo card snapshot", "[.snapshot][announcecard][memocard]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        //群公告卡片：别人发的、本人重新发布的、已删除的
        ChatMessage announce = Bubble(false, kCardMsgId);
        announce.senderName = L"林知夏";
        ApplyAnnouncementCard(announce, kAnnounceCard, L"林知夏");
        list.AppendMessage(announce);
        Json::Value republish = IcParse(kAnnounceCard);
        republish["republish"] = true;
        republish["severity"] = group_announcement_urgent;
        ChatMessage mine = Bubble(true, kCardMsgId + 1);
        ApplyAnnouncementCard(mine, IcCompact(republish), L"");
        list.AppendMessage(mine);
        ChatMessage gone = Bubble(false, kCardMsgId + 2);
        gone.senderName = L"林知夏";
        ApplyAnnouncementCard(gone, kAnnounceCardDeleted, L"林知夏");
        list.AppendMessage(gone);
        //群备忘卡片：别人新建的、本人修改的、失效的
        ChatMessage memoCard = Bubble(false, kMemoMsgId);
        memoCard.senderName = L"张三";
        ApplyMemoCard(memoCard, kMemoCard, L"张三", NowForMemo());
        list.AppendMessage(memoCard);
        Json::Value modified = IcParse(kMemoCard);
        modified["change"] = memo_card_modified;
        modified["targetall"] = true;
        ChatMessage myMemo = Bubble(true, kMemoMsgId + 1);
        ApplyMemoCard(myMemo, IcCompact(modified), L"", NowForMemo());
        list.AppendMessage(myMemo);
        ChatMessage invalid = Bubble(false, kMemoMsgId + 2);
        invalid.senderName = L"张三";
        ApplyMemoCard(invalid, kMemoCard, L"张三", NowForMemo());
        SetMemoCardAvailable(invalid, false);
        list.AppendMessage(invalid);

        RECT all = { 0, 0, kSnapW, kSnapH };
        list.ForceLayout(all);

        HDC screenDC = ::GetDC(NULL);
        REQUIRE(screenDC != NULL);
        HDC memDC = ::CreateCompatibleDC(screenDC);
        HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kSnapW, kSnapH);
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
        REQUIRE(FindPngEncoder(png));
        {
            Gdiplus::Bitmap image(bmp, NULL);
            CHECK(image.Save((dir + kSnapPath).c_str(), &png, NULL) == Gdiplus::Ok);
        }
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

// ============================== 群备忘卡片 ==============================

TEST_CASE("MC-C1 群备忘卡片 content 解析：正常、缺字段、不是 JSON", "[memocard]")
{
    MemoCardInfo info;
    REQUIRE(ParseMemoCard(kMemoCard, info));
    CHECK(info.m_memoId == kMemoId);
    CHECK(info.m_groupId == kGroupId);
    CHECK(info.m_change == memo_card_created);
    CHECK(info.m_title == L"周会");
    CHECK(info.m_note == L"带上周报");
    CHECK(info.m_start == 202610050930LL);
    CHECK(info.m_repeat == memo_repeat_weekly);
    CHECK(info.m_weekdays == 1);
    CHECK(info.m_count == 0);
    CHECK_FALSE(info.m_targetAll);
    CHECK(info.m_targetCount == kTargetCount);
    CHECK(info.m_editorId == kEditorId);
    CHECK(info.m_editorName == L"张三");

    //缺字段：只有 id 也能解析，change 缺省按「修改」；开始时间格式不对时为 0
    MemoCardInfo minimal;
    REQUIRE(ParseMemoCard(u8"{\"memoid\":\"3\",\"start\":\"2026/10/05\"}", minimal));
    CHECK(minimal.m_change == memo_card_modified);
    CHECK(minimal.m_start == 0);
    MemoCardInfo bad;
    CHECK_FALSE(ParseMemoCard(u8"{\"title\":\"x\"}", bad));
    CHECK_FALSE(ParseMemoCard("not json", bad));

    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        GroupCardMessageJson(kMemoMsgId, kEditorId, message_type_memo, kMemoCard), notify));
    CHECK_FALSE(notify.m_memoCardContent.empty());
    CHECK(notify.m_text.empty());
}

TEST_CASE("MC-C2 群备忘卡片的显示文字：副标题、时间与重复规则、提醒对象、备注", "[memocard]")
{
    CHECK(MemoCardSubtitle(L"张三", false, memo_card_created) == L"张三新建了群备忘");
    CHECK(MemoCardSubtitle(L"张三", false, memo_card_modified) == L"张三修改了群备忘");
    CHECK(MemoCardSubtitle(L"张三", true, memo_card_created) == L"我新建了群备忘");
    CHECK(MemoCardSubtitle(L"", true, memo_card_modified) == L"我修改了群备忘");
    CHECK(MemoCardSubtitle(L"", false, memo_card_created) == L"新建了一条群备忘");
    CHECK(MemoCardSubtitle(L"", false, memo_card_modified) == L"修改了一条群备忘");

    MemoCardInfo info;
    REQUIRE(ParseMemoCard(kMemoCard, info));
    const memo::LocalMinute now = NowForMemo();
    //与群「备忘」页签卡片同一套写法：EventTimeText + RepeatText
    CHECK(MemoCardTimeLine(info, now) == L"10月5日 09:30 · 每周一");
    CHECK(MemoCardTargetsText(info) == L"提醒 3 人");
    MemoCardInfo all = info;
    all.m_targetAll = true;
    CHECK(MemoCardTargetsText(all) == L"提醒全体成员");
    //开始时间不可用：只写重复规则
    MemoCardInfo noStart = info;
    noStart.m_start = 0;
    CHECK(MemoCardTimeLine(noStart, now) == L"每周一");

    //套用：描述块第一行时间与重复规则、第二行备注；状态行提醒对象；可点
    ChatMessage msg = Bubble(false, kMemoMsgId);
    ApplyMemoCard(msg, kMemoCard, L"", now);
    CHECK(msg.kind == kMsgScreenShare);
    CHECK(msg.screenShareIsMemo);
    CHECK_FALSE(msg.screenShareIsAnnouncement);
    CHECK(msg.infoCardRefId == kMemoId);
    CHECK(msg.screenShareTitle == L"周会");
    CHECK(msg.screenShareSubtitle == L"张三新建了群备忘");
    CHECK(msg.screenShareDesc == L"10月5日 09:30 · 每周一\n带上周报");
    CHECK(msg.screenShareStatus == L"提醒 3 人");
    CHECK(msg.text == L"[群备忘] 周会");
    CHECK(msg.screenShareButtonEnabled);

    //没有备注：描述块只有一行
    Json::Value noNote = IcParse(kMemoCard);
    noNote["note"] = "";
    ChatMessage plain = Bubble(true, kMemoMsgId);
    ApplyMemoCard(plain, IcCompact(noNote), L"", now);
    CHECK(plain.screenShareDesc == L"10月5日 09:30 · 每周一");
    CHECK(plain.screenShareSubtitle == L"我新建了群备忘");

    //内容不可用：标题写类型，没有按钮
    ChatMessage broken = Bubble(false, kMemoMsgId);
    ApplyMemoCard(broken, "{}", L"", now);
    CHECK(broken.screenShareTitle == L"群备忘");
    CHECK(broken.infoCardRefId == 0);
    CHECK_FALSE(broken.screenShareHasButton);
}

TEST_CASE("MC-C3 群备忘卡片的失效判定与切换", "[memocard]")
{
    //备忘簿未加载：一律可用；已加载：簿里有才可用
    CHECK(IsMemoCardAvailable(false, false));
    CHECK(IsMemoCardAvailable(false, true));
    CHECK(IsMemoCardAvailable(true, true));
    CHECK_FALSE(IsMemoCardAvailable(true, false));

    ChatMessage msg = Bubble(false, kMemoMsgId);
    ApplyMemoCard(msg, kMemoCard, L"", NowForMemo());
    //本来就可用：没有变化
    CHECK_FALSE(SetMemoCardAvailable(msg, true));
    //转为失效
    CHECK(SetMemoCardAvailable(msg, false));
    CHECK_FALSE(msg.screenShareButtonEnabled);
    CHECK(msg.screenShareStatus == L"备忘已删除，或你已不在提醒对象中");
    CHECK_FALSE(SetMemoCardAvailable(msg, false));
    //重新出现（又被加回提醒对象）：恢复可用，状态行换回提醒对象
    CHECK(SetMemoCardAvailable(msg, true));
    CHECK(msg.screenShareButtonEnabled);
    CHECK(msg.screenShareStatus == L"提醒 3 人");

    //不是群备忘卡片、内容不可用的卡片：不受影响
    ChatMessage announce = Bubble(false, kCardMsgId);
    ApplyAnnouncementCard(announce, kAnnounceCard, L"");
    CHECK_FALSE(SetMemoCardAvailable(announce, false));
    CHECK(announce.screenShareButtonEnabled);
    ChatMessage broken = Bubble(false, kMemoMsgId);
    ApplyMemoCard(broken, "{}", L"", NowForMemo());
    CHECK_FALSE(SetMemoCardAvailable(broken, false));
}

TEST_CASE("MC-C4 群备忘卡片能否点开：可用时可以，失效与内容不可用时不可以", "[memocard]")
{
    ChatMessage msg = Bubble(false, kMemoMsgId);
    ApplyMemoCard(msg, kMemoCard, L"", NowForMemo());
    CHECK(IsMemoCardOpenable(msg));
    CHECK_FALSE(IsAnnouncementCardOpenable(msg));
    SetMemoCardAvailable(msg, false);
    CHECK_FALSE(IsMemoCardOpenable(msg));

    ChatMessage broken = Bubble(false, kMemoMsgId);
    ApplyMemoCard(broken, "not json", L"", NowForMemo());
    CHECK_FALSE(IsMemoCardOpenable(broken));
}

TEST_CASE("MC-C5 群备忘卡片的摘要与消息记录窗", "[memocard]")
{
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_memo, std::string()) == u8"[群备忘]");
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        GroupCardMessageJson(kMemoMsgId, kEditorId, message_type_memo, kMemoCard), notify));
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[群备忘] 周会");

    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":2,\"cid\":\"268435463\",\"lastmsgid\":\"9102\",\"lastmsg\":{\"from\":\"5127\",\"msgtype\":23,"
        u8"\"digest\":\"S1\",\"digestarg\":\"周会\",\"timestamp\":1}}"
        u8"]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_digest == u8"[群备忘] 周会");

    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_group, kGroupId);
    CHECK(balloonhistory::PreviewFromContent(message_type_memo, rec.m_content) == u8"[群备忘] 周会");
    CHECK(balloonhistory::PreviewFromContent(message_type_memo, "{}") == u8"[群备忘]");
    CHECK(rec.m_searchText.find(u8"周报") != std::string::npos);

    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_memo, kMemoMsgId));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_memo;
    ctx.m_msgId = kMemoMsgId;
    const std::vector<balloonhistory::HistoryRowMenuItem> rowModel = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < rowModel.size(); ++i)
    {
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(rowModel[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}
