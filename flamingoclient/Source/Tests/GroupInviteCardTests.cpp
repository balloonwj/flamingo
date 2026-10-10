/**
 *  入群邀请卡片的单元测试（Suite IC，标签 [groupinvite]），GroupInviteCardTests.cpp
 *  覆盖（对应 docs/邀请入群方案.md 第 3 期 3d）：
 *    - IC1 解析卡片 content（含服务端下发时合并进来的状态、成员数、群名）：两侧的标题、副标题、摘要；整条消息解析后
 *          content 原文保存在 m_groupInviteContent；内容不可用时是一张没有按钮的卡片；
 *    - IC2 状态文字：被邀请人一侧与邀请人一侧各六种状态，认不出的状态不写；
 *    - IC3 「加入群聊」按钮只在被邀请人一侧、待加入时出现；状态变化后重填按钮并清除「正在加入」，content 里的 state 随之改；
 *    - IC4 1328 推送解析；按 inviterid / inviteeid 里不是本人的那一方定位单聊会话；对着真的 SQLite 库改写卡片状态，
 *          还原出来是新状态、原始JSON不动；本地没有这张卡片时不报错；content 列没有卡片内容的旧行退回原始JSON；
 *    - IC5 1327 请求构造与应答解析（入群 / 转审批 / 失败带邀请 id），各错误码对应的卡片状态与提示；
 *    - IC6 会话摘要「[入群邀请] 群名」（实时消息与会话列表两条路径）与消息记录窗的预览、行菜单；
 *    - IC7 聊天窗右键菜单里没有撤回、转发、引用回复、Pin；
 *    - IC8 登录应答与 1013 推送里 inviteconfirm 的解析，登录结果带出开关；邀请对话框的说明行据此提示对方同意后才会入群；
 *    - IC9 卡片布局：窄宽度与宽宽度下行高相同，名字很长时也不换行（标题与副标题以省略号收尾），按钮行计入卡片高度；
 *    - ICSNAP 效果图（标签 [.snapshot]，默认不运行）：两侧几种状态的卡片画进一个消息列表，存到 snapshots/group_invite_cards.png。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatGroupInviteCard.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"   // IC9、ICSNAP：把卡片排进一个列表里测量与绘制
#include "../UI/BalloonChatWnd/ChatBubble.h"        // IC9：取卡片那一行的行高
#include "../UI/BalloonHistory/HistoryView.h"       // IC6：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"    // IC6：消息记录窗的行菜单
#include "../UI/BalloonHistory/V2MsgQuery.h"        // IC4、IC6：RecordFromNotify / GroupInviteContentFromRecord
#include "../UI/BalloonHistory/V2MsgStore.h"        // IC4：真的 SQLite 库
#include "../UI/BalloonMain/GroupInviteStateApply.h" // IC4：定位会话与写库
#include "../UI/BalloonMain/MemberPickerLayout.h"   // IC8：邀请对话框的说明行
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../net/GroupProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/ConversationsProtocolClient.h"
#include "../net/LoginProtocolClient.h"
#include "../net/LoginResultFill.h"
#include "../net/IUProtocolData.h"                  // CLoginResult
#include "../net/Protocol.h"

using namespace balloonchatwnd;

namespace {

//邀请人、被邀请人、群
const long long kInviterId = 10001;
const long long kInviteeId = 10002;
const long long kGroupId   = 600001;
//邀请 id 与卡片消息的 msgid
const long long kInviteId  = 77;
const long long kCardMsgId = 9001;
//群当前的成员数（服务端下发时合并进 content）
const int kMemberCount = 32;
//卡片生成时间：2026-10-04 某时刻，Unix 毫秒
const long long kCardTime = 1759550000000LL;

//服务端 GroupInviteProtocol::augmentCardContent 之后的 content（jsoncpp 紧凑写法，键按字母序），状态为待加入
const char* const kCardPending =
    u8"{\"customface\":\"\",\"expiretime\":1760154800000,\"facetype\":0,\"groupid\":\"600001\","
    u8"\"groupname\":\"产品研发部周会群\",\"invitee\":\"李四\",\"inviteeid\":\"10002\",\"inviteid\":\"77\","
    u8"\"inviter\":\"张三\",\"inviterid\":\"10001\",\"membercount\":32,\"state\":0}";

//同一张卡片刚生成、还没合并状态与成员数时的 content（实时推送的原文）
const char* const kCardRaw =
    u8"{\"customface\":\"\",\"expiretime\":1760154800000,\"facetype\":0,\"groupid\":\"600001\","
    u8"\"groupname\":\"产品研发部周会群\",\"invitee\":\"李四\",\"inviteeid\":\"10002\",\"inviteid\":\"77\","
    u8"\"inviter\":\"张三\",\"inviterid\":\"10001\"}";

//IC9：一个很长的名字（伪语言把文字拉长一倍以上的情形）
const wchar_t* const kLongName =
    L"一个非常非常长的邀请人名字用来检查标题行是否会以省略号收尾而不是换行把按钮挤出卡片";

//IC9：两种消息区宽度（逻辑像素）。窄的比卡片加头像还窄，宽的是聊天窗常见宽度
const int kNarrowListW = 300;
const int kWideListW   = 900;
//IC9、ICSNAP：消息区高度，放得下几张卡片
const int kListH = 1200;

//ICSNAP：效果图尺寸与相对 exe 的输出路径
const int kSnapW = 720;
const int kSnapH = 1100;
const wchar_t* const kSnapPath = L"\\..\\..\\..\\snapshots\\group_invite_cards.png";

//一条普通气泡数据：对方发来（被邀请人一侧）或本人发出（邀请人一侧）
ChatMessage Bubble(bool fromMe, long long msgid)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = msgid;
    return msg;
}

//套好卡片的气泡数据
ChatMessage Card(bool fromMe, long long msgid, const std::string& content)
{
    ChatMessage msg = Bubble(fromMe, msgid);
    ApplyGroupInviteCard(msg, content);
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

//从卡片 content 里取 state；没有该字段时返回 -1
int StateOf(const std::string& content)
{
    const Json::Value v = IcParse(content);
    return (v.isObject() && v["state"].isInt()) ? v["state"].asInt() : -1;
}

//照服务端同步下发的格式拼一条单聊入群邀请卡片消息（发送者为邀请人）
std::string CardMessageJson(const char* content)
{
    Json::Value msg;
    msg["msgid"] = std::to_string(kCardMsgId);
    msg["ctype"] = conversation_single;
    msg["cid"] = std::to_string(kInviteeId);
    msg["from"] = std::to_string(kInviterId);
    msg["to"] = std::to_string(kInviteeId);
    msg["msgtype"] = message_type_group_invite;
    msg["content"] = IcParse(content);
    msg["timestamp"] = static_cast<Json::Int64>(kCardTime);
    return IcCompact(msg);
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

//把一组卡片排进消息列表，按给定宽度排版，返回最后一行（卡片）的行高
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

} // namespace

TEST_CASE("IC1 解析卡片 content：两侧的标题、副标题与摘要，整条消息保留 content 原文", "[groupinvite]")
{
    //被邀请人一侧（对方发来的卡片）
    const ChatMessage invitee = Card(false, kCardMsgId, kCardPending);
    CHECK(invitee.kind == kMsgGroupInvite);
    CHECK(invitee.inviteId == kInviteId);
    CHECK(invitee.inviteGroupId == kGroupId);
    CHECK(invitee.inviteTitle == L"「张三」邀请你加入群聊");
    CHECK(invitee.inviteSubtitle == L"产品研发部周会群 · 32 名成员");
    CHECK(invitee.inviteState == group_invite_pending);
    CHECK(invitee.text == L"[入群邀请] 产品研发部周会群");
    CHECK(invitee.inviteContent == kCardPending);
    CHECK(GroupInviteIdOf(kCardPending) == kInviteId);

    //邀请人一侧（本人发出的卡片）
    const ChatMessage inviter = Card(true, kCardMsgId, kCardPending);
    CHECK(inviter.inviteTitle == L"你邀请「李四」加入群聊");

    //没有合并成员数与状态的原文：副标题只写群名，按待加入处理
    const ChatMessage raw = Card(false, kCardMsgId, kCardRaw);
    CHECK(raw.inviteSubtitle == L"产品研发部周会群");
    CHECK(raw.inviteState == group_invite_pending);

    //不是待加入的卡片：副标题只写群名，不写人数（人数停在卡片下发那一刻，入群后会过时）
    Json::Value joinedRoot = IcParse(kCardPending);
    joinedRoot["state"] = group_invite_joined;
    CHECK(Card(false, kCardMsgId, IcCompact(joinedRoot)).inviteSubtitle == L"产品研发部周会群");

    //取不到名字时用不带名字的写法
    Json::Value noName = IcParse(kCardPending);
    noName["inviter"] = "";
    CHECK(Card(false, kCardMsgId, IcCompact(noName)).inviteTitle == L"有人邀请你加入群聊");

    //内容不可用：仍是一张没有按钮的卡片
    const ChatMessage bad = Card(false, kCardMsgId, "not json");
    CHECK(bad.kind == kMsgGroupInvite);
    CHECK(bad.inviteId == 0);
    CHECK(bad.inviteTitle == L"入群邀请");
    CHECK_FALSE(bad.inviteHasButton);
    CHECK(GroupInviteIdOf("not json") == 0);

    //整条消息解析：content 原文保存在 m_groupInviteContent
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CardMessageJson(kCardPending), notify));
    CHECK(notify.m_msgtype == message_type_group_invite);
    CHECK(notify.m_from == kInviterId);
    CHECK(GroupInviteIdOf(notify.m_groupInviteContent) == kInviteId);
    CHECK(StateOf(notify.m_groupInviteContent) == group_invite_pending);
}

TEST_CASE("IC2 状态文字：两侧各六种状态，认不出的状态不写", "[groupinvite]")
{
    //被邀请人一侧
    CHECK(GroupInviteStatusText(group_invite_pending, true) == L"邀请 7 天内有效");
    CHECK(GroupInviteStatusText(group_invite_approving, true) == L"已申请加入，等待群主或管理员审批");
    CHECK(GroupInviteStatusText(group_invite_joined, true) == L"已加入");
    CHECK(GroupInviteStatusText(group_invite_rejected, true) == L"未通过群主或管理员审批");
    CHECK(GroupInviteStatusText(group_invite_expired, true) == L"邀请已过期");
    CHECK(GroupInviteStatusText(group_invite_invalid, true) == L"邀请已失效");
    //邀请人一侧
    CHECK(GroupInviteStatusText(group_invite_pending, false) == L"等待对方确认");
    CHECK(GroupInviteStatusText(group_invite_approving, false) == L"对方已同意，等待群主或管理员审批");
    CHECK(GroupInviteStatusText(group_invite_joined, false) == L"对方已加入");
    CHECK(GroupInviteStatusText(group_invite_rejected, false) == L"未通过群主或管理员审批");
    CHECK(GroupInviteStatusText(group_invite_expired, false) == L"邀请已过期");
    CHECK(GroupInviteStatusText(group_invite_invalid, false) == L"邀请已失效");
    //更新的服务端新加的状态
    const int kUnknownState = 99;
    CHECK(GroupInviteStatusText(kUnknownState, true).IsEmpty());
    CHECK(GroupInviteStatusText(kUnknownState, false).IsEmpty());
}

TEST_CASE("IC3 「加入群聊」按钮只在被邀请人一侧、待加入时出现，状态变化后重填", "[groupinvite]")
{
    const int states[] = { group_invite_pending, group_invite_joined, group_invite_approving,
                           group_invite_rejected, group_invite_expired, group_invite_invalid };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        INFO("state " << states[i]);
        CHECK(GroupInviteHasButton(states[i], true) == (states[i] == group_invite_pending));
        CHECK_FALSE(GroupInviteHasButton(states[i], false));
    }

    //气泡数据：被邀请人一侧有按钮，邀请人一侧没有
    ChatMessage invitee = Card(false, kCardMsgId, kCardPending);
    CHECK(invitee.inviteHasButton);
    CHECK(invitee.inviteStatus == L"邀请 7 天内有效");
    CHECK_FALSE(Card(true, kCardMsgId, kCardPending).inviteHasButton);

    //点了「加入群聊」：按钮写「正在加入…」
    CHECK(GroupInviteButtonText(false) == L"加入群聊");
    CHECK(GroupInviteButtonText(true) == L"正在加入…");
    SetGroupInviteJoining(invitee, true);
    CHECK(invitee.inviteJoining);

    //1328 推送已加入：按钮消失、「正在加入」清除、content 里的 state 一并改掉
    ApplyGroupInviteState(invitee, group_invite_joined);
    CHECK_FALSE(invitee.inviteHasButton);
    CHECK_FALSE(invitee.inviteJoining);
    CHECK(invitee.inviteState == group_invite_joined);
    CHECK(invitee.inviteStatus == L"已加入");
    CHECK(invitee.inviteSubtitle == L"产品研发部周会群");
    CHECK(StateOf(invitee.inviteContent) == group_invite_joined);
    //改了 state 的 content 重新套用，结果相同
    CHECK(Card(false, kCardMsgId, invitee.inviteContent).inviteStatus == L"已加入");

    //content 解析失败时 SetGroupInviteContentState 原样返回
    CHECK(SetGroupInviteContentState("not json", group_invite_joined) == "not json");
}

TEST_CASE("IC4 1328 推送：定位单聊会话，改写本地库里的卡片状态", "[groupinvite]")
{
    //推送解析
    const std::string body = u8"{\"inviteid\":\"77\",\"msgid\":\"9001\",\"inviterid\":\"10001\","
                             u8"\"inviteeid\":\"10002\",\"state\":1}";
    net::GroupInviteChangedNotify notify;
    REQUIRE(net::GroupProtocolClient::parseGroupInviteChangedNotify(body, notify));
    CHECK(notify.m_inviteid == kInviteId);
    CHECK(notify.m_msgid == kCardMsgId);
    CHECK(notify.m_inviterid == kInviterId);
    CHECK(notify.m_inviteeid == kInviteeId);
    CHECK(notify.m_state == group_invite_joined);
    //缺 msgid、state 不是整数：解析失败
    net::GroupInviteChangedNotify bad;
    CHECK_FALSE(net::GroupProtocolClient::parseGroupInviteChangedNotify(
        u8"{\"inviteid\":\"77\",\"inviterid\":\"10001\",\"inviteeid\":\"10002\",\"state\":1}", bad));
    CHECK_FALSE(net::GroupProtocolClient::parseGroupInviteChangedNotify(
        u8"{\"inviteid\":\"77\",\"msgid\":\"9001\",\"state\":\"1\"}", bad));

    //定位会话：两个 userid 里不是本人的那一方
    CHECK(balloonmain::GroupInvitePeerId(kInviterId, kInviteeId, kInviteeId) == kInviterId);
    CHECK(balloonmain::GroupInvitePeerId(kInviterId, kInviteeId, kInviterId) == kInviteeId);
    const long long kStranger = 10009;
    CHECK(balloonmain::GroupInvitePeerId(kInviterId, kInviteeId, kStranger) == 0);
    CHECK(balloonmain::GroupInvitePeerId(0, kInviteeId, kInviteeId) == 0);

    //真的 SQLite 库：被邀请人本机收到的卡片，会话为与邀请人的单聊
    const std::wstring path = FreshTempDb(L"flamingo_ic4.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //本地还没有这张卡片：不报错也不建行
    bool hit = true;
    REQUIRE(balloonmain::ApplyGroupInviteStateToStore(store, kInviterId, kCardMsgId, group_invite_joined, hit));
    CHECK_FALSE(hit);

    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CardMessageJson(kCardPending), received));
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(received, conversation_single, kInviterId);
    CHECK(rec.m_content.find("\"groupinvite\"") != std::string::npos);
    REQUIRE(store.Upsert(rec));

    REQUIRE(balloonmain::ApplyGroupInviteStateToStore(store, kInviterId, kCardMsgId, group_invite_joined, hit));
    CHECK(hit);

    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_single, kInviterId, kCardMsgId, row, found));
    REQUIRE(found);
    //content 列与还原结果都是新状态
    std::string fromColumn;
    REQUIRE(balloonhistory::GroupInviteContentFromRecord(row, fromColumn));
    CHECK(StateOf(fromColumn) == group_invite_joined);
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(row, restored));
    CHECK(StateOf(restored.m_groupInviteContent) == group_invite_joined);
    //原始JSON仍是服务端当初发来的样子
    net::MessageNotify fromRaw;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, fromRaw));
    CHECK(StateOf(fromRaw.m_groupInviteContent) == group_invite_pending);

    //content 列没有卡片内容的旧行：取原始JSON里的内容；两处都没有：取不到
    balloonhistory::HistoryRecord old = rec;
    old.m_content = "{}";
    std::string content;
    REQUIRE(balloonhistory::GroupInviteContentFromRecord(old, content));
    CHECK(StateOf(content) == group_invite_pending);
    old.m_raw.clear();
    CHECK_FALSE(balloonhistory::GroupInviteContentFromRecord(old, content));

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("IC5 1327 请求与应答，各错误码对应的卡片状态与提示", "[groupinvite]")
{
    //请求：邀请 id 写成字符串
    const Json::Value req = IcParse(net::GroupProtocolClient::buildAcceptGroupInviteRequest(kInviteId));
    CHECK(req["inviteid"].asString() == "77");

    //成功、已入群
    net::AcceptGroupInviteResult joined;
    REQUIRE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(
        u8"{\"code\":0,\"msg\":\"ok\",\"inviteid\":\"77\",\"groupid\":\"600001\",\"joined\":1}", joined));
    CHECK(joined.m_code == 0);
    CHECK(joined.m_inviteid == kInviteId);
    CHECK(joined.m_groupid == kGroupId);
    CHECK(joined.m_joined);
    //成功、转为加群申请
    net::AcceptGroupInviteResult approving;
    REQUIRE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(
        u8"{\"code\":0,\"msg\":\"ok\",\"inviteid\":\"77\",\"groupid\":\"600001\",\"joined\":0}", approving));
    CHECK_FALSE(approving.m_joined);
    //失败：照样带回邀请 id，客户端据此找到卡片
    net::AcceptGroupInviteResult expired;
    REQUIRE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(
        u8"{\"code\":313,\"msg\":\"cannot accept group invite\",\"inviteid\":\"77\",\"groupid\":\"600001\",\"joined\":0}",
        expired));
    CHECK(expired.m_code == error_code_group_invite_expired);
    CHECK(expired.m_inviteid == kInviteId);
    //请求非法：不带邀请 id
    net::AcceptGroupInviteResult badReq;
    REQUIRE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(u8"{\"code\":3,\"msg\":\"bad request\"}", badReq));
    CHECK(badReq.m_inviteid == 0);
    //没有 code：解析失败
    net::AcceptGroupInviteResult noCode;
    CHECK_FALSE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(u8"{\"msg\":\"ok\"}", noCode));

    //错误码 → 卡片状态：已过期、已失效由客户端自己改，其余不改
    CHECK(GroupInviteStateForAcceptError(error_code_group_invite_expired) == group_invite_expired);
    CHECK(GroupInviteStateForAcceptError(error_code_group_invite_invalid) == group_invite_invalid);
    CHECK(GroupInviteStateForAcceptError(error_code_already_in_group) == kGroupInviteStateUnchanged);
    CHECK(GroupInviteStateForAcceptError(error_code_server_internal) == kGroupInviteStateUnchanged);

    //错误码 → 提示：已在群中不提示（直接打开群聊窗），其余各有提示
    CHECK(GroupInviteAcceptErrorText(error_code_already_in_group).IsEmpty());
    CHECK(GroupInviteAcceptErrorText(error_code_group_invite_expired) == L"邀请已过期，请联系邀请人重新邀请。");
    CHECK(GroupInviteAcceptErrorText(error_code_group_invite_invalid) == L"邀请已失效：群可能已解散，或邀请人已退出群聊。");
    CHECK(GroupInviteAcceptErrorText(error_code_group_not_exist) == L"该群已解散。");
    CHECK(GroupInviteAcceptErrorText(error_code_server_internal) == L"加入群聊失败，请稍后再试。");

    //应答失败后的卡片：已过期时按钮消失、写「邀请已过期」；状态不变时只清除「正在加入」
    ChatMessage card = Card(false, kCardMsgId, kCardPending);
    SetGroupInviteJoining(card, true);
    ApplyGroupInviteState(card, GroupInviteStateForAcceptError(error_code_group_invite_expired));
    CHECK_FALSE(card.inviteHasButton);
    CHECK(card.inviteStatus == L"邀请已过期");
}

TEST_CASE("IC6 会话摘要「[入群邀请] 群名」与消息记录窗", "[groupinvite]")
{
    //实时消息的摘要（通知、托盘）
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_group_invite, std::string()) == u8"[入群邀请]");
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(CardMessageJson(kCardPending), notify));
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[入群邀请] 产品研发部周会群");
    //content 不可用：只写类型
    net::MessageNotify noContent = notify;
    noContent.m_groupInviteContent.clear();
    CHECK(net::MessageProtocolClient::digestForNotify(noContent) == u8"[入群邀请]");

    //会话列表：服务端 digest 故意写成别的文字，本地按 digestarg 生成
    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":1,\"cid\":\"10001\",\"lastmsgid\":\"9001\",\"lastmsg\":{\"from\":\"10001\",\"msgtype\":21,"
        u8"\"digest\":\"S1\",\"digestarg\":\"产品研发部周会群\",\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"10003\",\"lastmsgid\":\"9002\",\"lastmsg\":{\"from\":\"10003\",\"msgtype\":21,"
        u8"\"digest\":\"S2\",\"timestamp\":1}}"
        u8"]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 2);
    CHECK(out.m_items[0].m_digest == u8"[入群邀请] 产品研发部周会群");
    CHECK(out.m_items[1].m_digest == u8"[入群邀请]");

    //消息记录窗：预览带群名，content 列取不到时只写类型；行菜单没有转发与 Pin
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_single, kInviterId);
    CHECK(balloonhistory::PreviewFromContent(message_type_group_invite, rec.m_content) == u8"[入群邀请] 产品研发部周会群");
    CHECK(balloonhistory::PreviewFromContent(message_type_group_invite, "{}") == u8"[入群邀请]");
    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_group_invite, kCardMsgId));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_group_invite;
    ctx.m_msgId = kCardMsgId;
    const std::vector<balloonhistory::HistoryRowMenuItem> model = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < model.size(); ++i)
    {
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}

TEST_CASE("IC7 聊天窗右键菜单：没有撤回、转发、引用回复、Pin", "[groupinvite]")
{
    CHECK(kChatMenuKindGroupInvite == (int)kMsgGroupInvite);
    CHECK_FALSE(CanForwardMessage(kChatMenuKindGroupInvite, kCardMsgId, 0));
    CHECK_FALSE(CanMergeForwardMessage(kChatMenuKindGroupInvite, kCardMsgId, 0));
    //调用方即使按普通消息传了可撤回、可 Pin、可转发，入群邀请卡片也不给这几项
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindGroupInvite, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(HasMenuId(model, kChatMsgMenuDelete));
    CHECK(HasMenuId(model, kChatMsgMenuMultiSelect));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuReply));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuRecall));
}

TEST_CASE("IC8 inviteconfirm 开关：登录应答、1013 推送与邀请对话框的说明行", "[groupinvite]")
{
    //登录应答
    const std::string base =
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"8\",\"servertime\":1,\"profile\":{\"enterpriseid\":\"7\"}";
    net::LoginResponse on;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + ",\"features\":{\"fileshare\":1,\"inviteconfirm\":1}}", on));
    CHECK(net::LoginProtocolClient::isFeatureOn(on.features, net::kFeatureInviteConfirm));
    CLoginResult onResult;
    net::FillLoginResultFromResponse(on, onResult);
    CHECK(onResult.m_nInviteConfirmFeature == 1);
    CHECK(onResult.m_nFileShareFeature == 1);

    //关闭、老服务端不下发：一律关闭
    net::LoginResponse off;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + ",\"features\":{\"inviteconfirm\":0}}", off));
    CHECK_FALSE(net::LoginProtocolClient::isFeatureOn(off.features, net::kFeatureInviteConfirm));
    net::LoginResponse old;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + "}", old));
    CLoginResult oldResult;
    net::FillLoginResultFromResponse(old, oldResult);
    CHECK(oldResult.m_nInviteConfirmFeature == 0);

    //1013 推送与登录应答的 features 同构
    net::FeaturesNotify pushed;
    REQUIRE(net::LoginProtocolClient::parseFeaturesNotify("{\"fileshare\":1,\"inviteconfirm\":1,\"p2p\":1}", pushed));
    CHECK(net::LoginProtocolClient::isFeatureOn(pushed.m_features, net::kFeatureInviteConfirm));

    //邀请对话框的说明行
    CHECK(balloonmain::InviteHistoryHintText(true, false).IsEmpty());
    CHECK(balloonmain::InviteHistoryHintText(true, true) == L"对方同意后才会加入群聊");
    CHECK(balloonmain::InviteHistoryHintText(false, true)
          == L"本群已设置为新成员不可查看入群前的聊天记录\n对方同意后才会加入群聊");
}

TEST_CASE("IC9 卡片布局：窄宽度下行高不变，名字很长时不换行，按钮行计入卡片高度", "[groupinvite]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        const ChatMessage withButton = Card(false, kCardMsgId, kCardPending);
        const ChatMessage noButton = Card(true, kCardMsgId, kCardPending);
        REQUIRE(withButton.inviteHasButton);
        REQUIRE_FALSE(noButton.inviteHasButton);

        const int wide = CardRowHeight(withButton, kWideListW);
        const int narrow = CardRowHeight(withButton, kNarrowListW);
        REQUIRE(wide > 0);
        //卡片固定宽度，文字单行省略：窄宽度下不会被压扁或撑高
        CHECK(narrow == wide);
        //按钮行计入卡片高度：有按钮比没按钮高
        CHECK(wide > CardRowHeight(noButton, kWideListW));

        //名字与群名都很长：标题、副标题以省略号收尾，行高与普通卡片相同，按钮仍在卡片内
        ChatMessage longCard = withButton;
        longCard.inviteTitle = kLongName;
        longCard.inviteSubtitle = kLongName;
        CHECK(CardRowHeight(longCard, kNarrowListW) == wide);
        CHECK(CardRowHeight(longCard, kWideListW) == wide);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("ICSNAP group invite card snapshot", "[.snapshot][groupinvite]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        long long msgid = kCardMsgId;
        //被邀请人一侧：待加入、正在加入、等待审批、已加入、已过期
        list.AppendMessage(Card(false, msgid++, kCardPending));
        ChatMessage joining = Card(false, msgid++, kCardPending);
        SetGroupInviteJoining(joining, true);
        list.AppendMessage(joining);
        const int inviteeStates[] = { group_invite_approving, group_invite_joined, group_invite_expired };
        for (size_t i = 0; i < sizeof(inviteeStates) / sizeof(inviteeStates[0]); ++i)
        {
            ChatMessage card = Card(false, msgid++, kCardPending);
            ApplyGroupInviteState(card, inviteeStates[i]);
            list.AppendMessage(card);
        }
        //邀请人一侧：等待对方确认、对方已加入、已失效
        list.AppendMessage(Card(true, msgid++, kCardPending));
        const int inviterStates[] = { group_invite_joined, group_invite_invalid };
        for (size_t i = 0; i < sizeof(inviterStates) / sizeof(inviterStates[0]); ++i)
        {
            ChatMessage card = Card(true, msgid++, kCardPending);
            ApplyGroupInviteState(card, inviterStates[i]);
            list.AppendMessage(card);
        }

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
