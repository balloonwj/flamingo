/**
 *  入群必读客户端的单测（docs/群公告接入方案.md §14），GroupMustReadTests.cpp
 *
 *  [mustread]
 *    MR-C1 协议：群资料带 mustreadid、成员带 mustread；1335 应答（有无公告、失败）；1332 请求
 *    MR-C2 输入区状态：入群必读优先于仅通知；「未确认必读」标记只给群主与管理员；自动弹窗只弹一次
 *    MR-C3 必读卡片：content 的 mustread 解析；发送人与副标题；发送被拒（315）与转发被拒的提示
 *    MR-C4 群管理窗：成员映射与菜单「代为确认入群必读」、群资料映射 mustreadid、设为必读失败的提示
 *    MR-C5 必读卡片的整条推送（发送者为系统 0）能解析；发送者为 0 的普通公告卡片与文字消息仍被拒绝
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../net/GroupProtocolClient.h"
#include "../net/MessageProtocolClient.h"   // MR-C5：整条消息推送的解析
#include "../net/Protocol.h"
#include "../UI/BalloonChatWnd/GroupMustRead.h"
#include "../UI/BalloonChatWnd/GroupNotifyOnly.h"
#include "../UI/BalloonChatWnd/ChatAnnouncementCard.h"
#include "../UI/BalloonChatWnd/ChatSendLimit.h"
#include "../UI/BalloonGroupManage/GroupManageMapping.h"

using namespace balloonchatwnd;

namespace {

// 群主、管理员、普通成员（网络侧角色取值，与 group_member_role 一致）
const int kNetOwner = 2;
const int kNetAdmin = 1;
const int kNetMember = 0;

// 一张入群必读卡片的 content（服务端 buildMustReadCardContent 的形态）
const char* const kMustReadCard =
    "{\"announcementid\":\"42\",\"groupid\":\"268435457\",\"title\":\"入群须知\",\"body\":\"先读群规\","
    "\"severity\":0,\"publisherid\":\"100001\",\"publisher\":\"张三\",\"publishtime\":1700000000000,"
    "\"republish\":false,\"text\":\"[群公告] 入群须知\",\"mustread\":true}";

} // namespace

TEST_CASE("MR-C1 协议：群资料、成员、1335 应答与 1332 请求", "[mustread]")
{
    //群资料带 mustreadid（字符串）；老服务端不带时为 0
    net::GroupInfoResult info;
    REQUIRE(net::GroupProtocolClient::parseGroupInfoResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"name\":\"g\",\"mustreadid\":\"42\"}", info));
    CHECK(info.m_mustreadid == 42);
    net::GroupInfoResult old;
    REQUIRE(net::GroupProtocolClient::parseGroupInfoResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"name\":\"g\"}", old));
    CHECK(old.m_mustreadid == 0);

    //成员带 mustread
    net::GroupMembersResult members;
    REQUIRE(net::GroupProtocolClient::parseGroupMembersResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"members\":["
        "{\"userid\":\"100001\",\"role\":2},{\"userid\":\"100002\",\"role\":0,\"mustread\":1}]}", members));
    REQUIRE(members.m_members.size() == 2);
    CHECK(members.m_members[0].m_mustread == 0);
    CHECK(members.m_members[1].m_mustread == 1);

    //1335 应答：待确认、带公告
    net::MustReadResult r;
    REQUIRE(net::GroupProtocolClient::parseGetMustReadResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"mustreadid\":\"42\",\"pending\":true,"
        "\"announcement\":{\"id\":\"42\",\"groupid\":\"268435457\",\"title\":\"入群须知\",\"content\":\"先读群规\","
        "\"severity\":1,\"publisher\":\"100001\",\"publishername\":\"张三\",\"publishtime\":1700000000000}}", r));
    CHECK(r.m_pending);
    CHECK(r.m_mustreadid == 42);
    CHECK(r.m_hasAnnouncement);
    CHECK(r.m_announcement.m_title == std::string("入群须知"));
    //没有设置必读：不带公告
    net::MustReadResult none;
    REQUIRE(net::GroupProtocolClient::parseGetMustReadResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"mustreadid\":\"0\",\"pending\":false}", none));
    CHECK_FALSE(none.m_hasAnnouncement);
    CHECK_FALSE(none.m_pending);
    //失败应答：只带 code / msg 也算解析成功
    net::MustReadResult failed;
    REQUIRE(net::GroupProtocolClient::parseGetMustReadResponse("{\"code\":302,\"msg\":\"not a group member\"}", failed));
    CHECK(failed.m_code == 302);
    CHECK_FALSE(net::GroupProtocolClient::parseGetMustReadResponse("not json", failed));

    //1332 请求：ID 写成字符串，0 表示取消
    CHECK(net::GroupProtocolClient::buildSetMustReadRequest(268435457LL, 42)
          == std::string("{\"announcementid\":\"42\",\"groupid\":\"268435457\"}"));
    CHECK(net::GroupProtocolClient::buildSetMustReadRequest(268435457LL, 0).find("\"announcementid\":\"0\"")
          != std::string::npos);
}

TEST_CASE("MR-C2 输入区状态、成员标记与自动弹窗", "[mustread]")
{
    //入群必读优先于仅通知
    CHECK(DecideGroupInputBlock(0, kNetMember, false) == kGroupInputOpen);
    CHECK(DecideGroupInputBlock(1, kNetMember, false) == kGroupInputNotifyOnly);
    CHECK(DecideGroupInputBlock(1, kNetMember, true) == kGroupInputMustRead);
    CHECK(DecideGroupInputBlock(0, kNetMember, true) == kGroupInputMustRead);
    CHECK(DecideGroupInputBlock(1, kNetAdmin, false) == kGroupInputOpen);
    //角色未知时仅通知不拦，入群必读照样拦（服务端也会拦）
    CHECK(DecideGroupInputBlock(1, kGroupRoleUnknown, false) == kGroupInputOpen);
    CHECK(DecideGroupInputBlock(0, kGroupRoleUnknown, true) == kGroupInputMustRead);

    //「未确认必读」标记只给群主与管理员
    CHECK(ShouldShowMustReadMark(kNetOwner, 1));
    CHECK(ShouldShowMustReadMark(kNetAdmin, 1));
    CHECK_FALSE(ShouldShowMustReadMark(kNetMember, 1));
    CHECK_FALSE(ShouldShowMustReadMark(kNetOwner, 0));
    CHECK_FALSE(ShouldShowMustReadMark(kGroupRoleUnknown, 1));

    //自动弹窗：待确认、公告已到、还没弹过
    CHECK(ShouldAutoPopupMustRead(true, true, false));
    CHECK_FALSE(ShouldAutoPopupMustRead(true, true, true));
    CHECK_FALSE(ShouldAutoPopupMustRead(true, false, false));
    CHECK_FALSE(ShouldAutoPopupMustRead(false, true, false));

    //提示文字非空，且与仅通知的提示不同
    CHECK(std::wstring(MustReadInputHintText()) != std::wstring(NotifyOnlyInputHintText()));
    CHECK_FALSE(std::wstring(MustReadInputHintText()).empty());
}

TEST_CASE("MR-C3 必读卡片的解析与显示、发送与转发被拒的提示", "[mustread]")
{
    AnnouncementCardInfo info;
    REQUIRE(ParseAnnouncementCard(kMustReadCard, info));
    CHECK(info.m_mustRead);
    CHECK(info.m_announcementId == 42);
    //普通公告卡片没有 mustread
    AnnouncementCardInfo normal;
    REQUIRE(ParseAnnouncementCard("{\"announcementid\":\"7\",\"title\":\"t\"}", normal));
    CHECK_FALSE(normal.m_mustRead);

    //套用：发送人显示「入群必读」，副标题说明阅读后才能发言，可以点开
    ChatMessage msg;
    msg.fromMe = false;
    ApplyAnnouncementCard(msg, kMustReadCard, CString(L"系统"));
    CHECK(msg.screenShareHostName == CString(MustReadSenderName()));
    CHECK(msg.screenShareSubtitle == CString(MustReadCardSubtitle()));
    CHECK(IsAnnouncementCardOpenable(msg));

    //发送文字、文件被拒（315）
    CHECK(SendFailureText(error_code_must_read_pending, false) == MustReadSendFailureText());
    CHECK(FileSendFailureText(error_code_must_read_pending, false) == MustReadSendFailureText());
    //与禁言的提示不同
    CHECK(SendFailureText(error_code_member_muted, false) != MustReadSendFailureText());
}

TEST_CASE("MR-C4 群管理窗：成员映射与「代为确认」菜单、群资料映射、失败提示", "[mustread]")
{
    using namespace balloongroupmanage;
    const int64_t kNow = 1700000000000LL;

    net::GroupMembersResult members;
    members.m_code = 0;
    members.m_groupid = 268435457LL;
    net::GroupMemberItem a;
    a.m_userid = 100001;
    a.m_role = kNetOwner;
    net::GroupMemberItem b;
    b.m_userid = 100002;
    b.m_role = kNetMember;
    b.m_mustread = 1;
    members.m_members.push_back(a);
    members.m_members.push_back(b);
    GroupManageModel model;
    ApplyGroupMembers(members, kNow, model);
    REQUIRE(model.m_members.size() == 2);
    bool foundPending = false;
    for (size_t i = 0; i < model.m_members.size(); ++i)
    {
        if (model.m_members[i].userid == 100002)
        {
            CHECK(model.m_members[i].mustReadPending);
            foundPending = true;
        }
        else
        {
            CHECK_FALSE(model.m_members[i].mustReadPending);
        }
    }
    CHECK(foundPending);

    //菜单：群主、管理员对待确认的成员有「代为确认入群必读」，普通成员没有
    MemberMenuState state;
    state.m_myRole = kRoleAdmin;
    state.m_targetRole = kRoleMember;
    state.m_targetMustRead = true;
    std::vector<MemberMenuEntry> entries = BuildMemberMenuModel(state);
    bool hasRelease = false;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (!entries[i].m_isSeparator && entries[i].m_id == kMemberMenuReleaseMustRead)
            hasRelease = true;
    }
    CHECK(hasRelease);
    state.m_myRole = kRoleMember;
    entries = BuildMemberMenuModel(state);
    for (size_t i = 0; i < entries.size(); ++i)
    {
        CHECK(entries[i].m_id != (UINT)kMemberMenuReleaseMustRead);
    }
    state.m_myRole = kRoleOwner;
    state.m_targetMustRead = false;
    entries = BuildMemberMenuModel(state);
    for (size_t i = 0; i < entries.size(); ++i)
    {
        CHECK(entries[i].m_id != (UINT)kMemberMenuReleaseMustRead);
    }

    //群资料映射 mustreadid
    net::GroupInfoResult info;
    info.m_code = 0;
    info.m_groupid = 268435457LL;
    info.m_mustreadid = 42;
    ApplyGroupInfo(info, model);
    CHECK(model.m_mustReadId == 42);

    //设为必读的公告已被删除
    const CString tip = DescribeGroupOpFailure(cmd_set_must_read, error_code_announcement_404, "announcement not exist");
    CHECK(tip.Find(L"入群必读") >= 0);
}

TEST_CASE("MR-C5 必读卡片的整条推送能解析：发送者为系统（0）的公告卡片不被当成脏数据", "[mustread]")
{
    //2026-10-07 实测（tmp/threadgroup/live 的客户端日志）：审批通过后服务端推给新成员的必读卡片，
    //客户端报「Parse v2 message-notify failed」后丢弃，卡片没有显示。下面是日志里的原始报文
    const std::string pushed =
        "{\"cid\":\"268435456\",\"clientmsgid\":\"mustread-268435456-5127-1791385706000\",\"content\":"
        "{\"announcementid\":\"1\",\"body\":\"\\u5927\\u5bb6\\u6765501\\u5f00\\u4f1a\\u3002\",\"groupid\":\"268435456\","
        "\"mustread\":true,\"publisher\":\"\\u97e9\\u6653\\u71d5888\",\"publisherid\":\"5308\",\"publishtime\":1785763678000,"
        "\"republish\":false,\"severity\":0,\"text\":\"[\\u7fa4\\u516c\\u544a] \\u5f00\\u4f1a\",\"title\":\"\\u5f00\\u4f1a\"},"
        "\"ctype\":2,\"from\":\"0\",\"mentionall\":false,\"mentions\":[],\"msgid\":\"1417\",\"msgtype\":22,"
        "\"recalled\":false,\"timestamp\":1791385706000,\"to\":\"268435456\"}";
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(pushed, notify));
    CHECK(notify.m_msgid == 1417);
    CHECK(notify.m_from == 0);
    CHECK(notify.m_msgtype == message_type_announcement);
    //卡片内容照常取出，并认得出是入群必读
    AnnouncementCardInfo info;
    REQUIRE(ParseAnnouncementCard(notify.m_announcementContent, info));
    CHECK(info.m_mustRead);
    CHECK(info.m_announcementId == 1);

    //发送者为 0 只对必读卡片放行：普通公告卡片（以发布人名义发出）与普通文字消息的 from 为 0 时仍是脏数据
    std::string normalCard = pushed;
    const std::string mustReadFlag = "\"mustread\":true,";
    normalCard.erase(normalCard.find(mustReadFlag), mustReadFlag.size());
    net::MessageNotify rejectedCard;
    CHECK_FALSE(net::MessageProtocolClient::parseMessageNotify(normalCard, rejectedCard));
    const std::string text =
        "{\"cid\":\"268435456\",\"clientmsgid\":\"c1\",\"content\":{\"text\":\"hi\"},\"ctype\":2,\"from\":\"0\","
        "\"mentionall\":false,\"mentions\":[],\"msgid\":\"1418\",\"msgtype\":1,\"recalled\":false,"
        "\"timestamp\":1791385706000,\"to\":\"268435456\"}";
    net::MessageNotify rejectedText;
    CHECK_FALSE(net::MessageProtocolClient::parseMessageNotify(text, rejectedText));
}
