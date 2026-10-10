/**
 *  帖子群 exe 一侧的单元测试（标签 [threadgroup]），ThreadGroupHostTests.cpp
 *  覆盖（对应 docs/帖子群方案.md 第 8 节 exe 部分，第 2a 期）：
 *    - TGE1 群对象解析 grouptype：1303 应答、1318 推送、1320 应答的 groups[] 都解析出群类型；缺省、取值不认识时为普通群；
 *    - TGE2 隐藏判定：模块就绪 × 企业开关 × 群类型；主程序里重复的常量与协议头、DLL 接口头一致；代发命令的放行规则；
 *    - TGE3 不可用时的隐藏（第 3a 期）：隐藏群号的计算，群列表与会话列表的过滤（顺序不变，单聊不受影响），
 *           隐藏会话来消息时只更新数据与未读、不弹窗不闪不响；
 *    - TGE4 打开群会话的分流：普通群建群聊窗，帖子群在功能可用时交给模块，不可用时什么也不打开；
 *    - TGE5 卡片 25、26 的摘要（第 3c 期）：摘要文字与已删除形态，实时推送、会话同步（记下帖子号）、本地记录的预览与搜索文字；
 *    - TGE7 提醒判定（第 3d 期）：帖子群按帖子群窗口判断「正在看」，正在看时不计未读、不提醒；
 *    - TGE12 第 5a 步（docs/帖子群方案.md 14.1 的 P1、P2 与右键菜单）：@ 的显示名取群名片或昵称、不含备注；
 *            会话摘要开头的「[有人@我] 」「[关注] 」标记的长度；帖子群的右键菜单第一项为「打开帖子群」、没有字幕模式；
 *    - TGE15 第 5c 步（docs/帖子群方案.md 14.6）：2721 推送的解析、被改写卡片的摘要、写回本地消息库
 *            （content 列、原始 JSON、搜索文本一起改，置顶保留，本地没有那一行时不算失败）；
 *    - TGE16 已删除（2026-10-08）：客户端不再发 2716，离线期间的改写由按改写序号同步补上（[rewrittensync]）；
 *    - TGE8 第 5e 步（docs/帖子群方案.md 14.3）：帖子群的群成员面板没有输入框，成员右键菜单去掉「@TA」，其余各项、
 *           顺序与分隔线不变；
 *    - TGE13 第 5g 步（14.5）：本地记录里帖子卡片与补充卡片的图片列表（字段、顺序、不合法的项跳过、已删除形态不取），
 *            缩略图最多 3 张、多出的写「+N」；
 *    - TGE17 登录成功时模块的处理（2026-10-08 实机发现）：开关打开时激活；断线期间开关被关掉、模块还处于已登录状态时
 *            停用（关闭帖子群窗口）；开关关闭且模块本来就没有激活时什么也不做。
 *    - TGE18 第 6b 步（docs/帖子群方案.md §15.1 第 3 条）：帖子群的群管理窗口没有「群公告」页与基本信息页的「群公告」一行，
 *            其余各页与顺序同普通群；
 *    - TGE19 第 6b 步（§15.1 第 4 条）：点正文里的 @某人弹出的成员菜单与群成员面板的右键菜单逐项相同（没有「@TA」），
 *            @ 本人只有「查看资料」，对方已退群时按原有规则，名单未到时没有管理类，@所有人没有菜单；
 *    - TGE20 第 6g 步（§15.1 第 2 条）：卡片带 mentionall 时除发布者外每个成员都按被 @ 处理（会话摘要加「[有人@我] 」、
 *            按被 @ 提醒），发布者本人不算；不带时只看 mentions，与原来相同。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../Feature/ThreadGroup/ThreadCardSync.h"     // 第 5c 步：卡片改写后同步到本机
#include "../Feature/ThreadGroup/ThreadGroupFilter.h"
#include "../UI/BalloonHistory/V2MsgStore.h"         // CV2MsgStore：本地消息库
#include "../Feature/ThreadGroup/ThreadGroupGate.h"
#include "../UI/BalloonMain/NotifyModePolicy.h"   // DecideArrival：隐藏会话的提醒判定
#include "../UI/BalloonHistory/HistoryView.h"      // PreviewFromContent：本地记录的预览文字
#include "../UI/BalloonHistory/V2MsgQuery.h"       // RecordFromNotify / NotifyFromRecord：本地消息库的读写
#include "../net/ConversationsProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include <memory>
#include <string.h>
#include "../net/GroupProtocolClient.h"
#include "../net/Protocol.h"
#include "../../FlamingoThreadGroup/ThreadGroupApi.h"
#include "../UI/BalloonMain/MainPanelGroupMenu.h"   // BuildGroupMenuModel：帖子群的右键菜单
#include <cwchar>                                   // wcscmp：比对菜单项文字
#include "../UI/BalloonChatWnd/GroupMentionMenu.h"  // RemoveMentionMenuEntry：群成员面板的右键菜单
#include "../UI/BalloonGroupManage/GroupManageMapping.h"   // IsAnnouncementPageVisible：帖子群没有「群公告」页
#include "../UI/BalloonGroupManage/GroupManageModel.h"
#include "../UI/BalloonGroupManage/GroupManageLayout.h"    // BuildNav / BuildPage：群管理窗口的导航与页面
#include "../UI/BalloonMain/GroupFollowModel.h"           // IsGroupMessageAtMe / DecorateGroupDigest：TGE20
#include "../UI/BalloonMain/MsgBoxModel.h"                // AtMeDigestPrefix：TGE20

using namespace balloonthread;

namespace {

//测试用的群号
const int64_t kTestGroupId = 600006;

//一份 1303 应答（帖子群）
const char* const kThreadGroupInfoJson =
    "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"600006\",\"name\":\"tg\",\"ownerid\":\"10001\","
    "\"membercount\":3,\"myrole\":2,\"grouptype\":1}";

//一份 1303 应答（老服务端，不带 grouptype）
const char* const kLegacyGroupInfoJson =
    "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"600006\",\"name\":\"g\",\"ownerid\":\"10001\",\"membercount\":3}";

//一份 1303 应答（grouptype 为不认识的取值）
const char* const kUnknownTypeGroupInfoJson =
    "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"600006\",\"name\":\"g\",\"ownerid\":\"10001\",\"grouptype\":7}";

//一份 1318 推送（帖子群改了群名）
const char* const kThreadGroupChangedJson =
    "{\"changetype\":1,\"groupid\":\"600006\",\"name\":\"tg2\",\"ownerid\":\"10001\",\"membercount\":3,\"grouptype\":1}";

//一份 1320 应答：一个普通群（带 grouptype 0）、一个帖子群、一个老服务端口径（不带 grouptype）
const char* const kUserGroupsJson =
    "{\"code\":0,\"msg\":\"ok\",\"groups\":["
    "{\"groupid\":\"600001\",\"name\":\"a\",\"ownerid\":\"10001\",\"membercount\":2,\"grouptype\":0},"
    "{\"groupid\":\"600006\",\"name\":\"tg\",\"ownerid\":\"10001\",\"membercount\":3,\"grouptype\":1},"
    "{\"groupid\":\"600007\",\"name\":\"c\",\"ownerid\":\"10002\",\"membercount\":4}]}";

} // namespace

TEST_CASE("TGE1 群对象解析 grouptype：1303、1318、1320 都带回群类型，缺省与不认识的取值为普通群", "[threadgroup]")
{
    SECTION("1303 应答：帖子群")
    {
        net::GroupInfoResult info;
        REQUIRE(net::GroupProtocolClient::parseGroupInfoResponse(kThreadGroupInfoJson, info));
        CHECK(info.m_code == 0);
        CHECK(info.m_groupid == kTestGroupId);
        CHECK(info.m_grouptype == group_type_thread);
    }

    SECTION("1303 应答：老服务端不带 grouptype 时为普通群")
    {
        net::GroupInfoResult info;
        REQUIRE(net::GroupProtocolClient::parseGroupInfoResponse(kLegacyGroupInfoJson, info));
        CHECK(info.m_grouptype == group_type_normal);
    }

    SECTION("1303 应答：不认识的取值按普通群")
    {
        net::GroupInfoResult info;
        REQUIRE(net::GroupProtocolClient::parseGroupInfoResponse(kUnknownTypeGroupInfoJson, info));
        CHECK(info.m_grouptype == group_type_normal);
    }

    SECTION("1318 推送：群资料部分同样带回群类型")
    {
        net::GroupInfoChangedNotify notify;
        REQUIRE(net::GroupProtocolClient::parseGroupInfoChangedNotify(kThreadGroupChangedJson, notify));
        CHECK(notify.m_info.m_groupid == kTestGroupId);
        CHECK(notify.m_info.m_grouptype == group_type_thread);
    }

    SECTION("1320 应答：逐个群解析，缺省为普通群")
    {
        net::UserGroupsResult result;
        REQUIRE(net::GroupProtocolClient::parseUserGroupsResponse(kUserGroupsJson, result));
        REQUIRE(result.m_groups.size() == 3);
        CHECK(result.m_groups[0].m_grouptype == group_type_normal);
        CHECK(result.m_groups[1].m_groupid == kTestGroupId);
        CHECK(result.m_groups[1].m_grouptype == group_type_thread);
        CHECK(result.m_groups[2].m_grouptype == group_type_normal);
    }

    SECTION("构造时的缺省值为普通群")
    {
        CHECK(net::GroupInfoResult().m_grouptype == group_type_normal);
        CHECK(net::UserGroupItem().m_grouptype == group_type_normal);
    }
}

TEST_CASE("TGE2 隐藏判定：模块就绪 × 开关 × 群类型；常量与协议头一致；代发命令的放行规则", "[threadgroup]")
{
    SECTION("主程序重复的常量与协议头、DLL 接口头一致")
    {
        CHECK(kGroupTypeNormal == static_cast<int>(group_type_normal));
        CHECK(kGroupTypeThread == static_cast<int>(group_type_thread));
        CHECK(kThreadCmdFirst == static_cast<uint32_t>(threadgroup::kThreadCmdFirst));
        CHECK(kThreadCmdLast == static_cast<uint32_t>(threadgroup::kThreadCmdLast));
        CHECK(IsThreadFrame(cmd_thread_list_posts));
        CHECK(IsThreadFrame(cmd_thread_changed_notify));
    }

    SECTION("功能可用：DLL 就绪且开关打开")
    {
        CHECK(IsThreadGroupAvailable(true, true));
        CHECK_FALSE(IsThreadGroupAvailable(true, false));
        CHECK_FALSE(IsThreadGroupAvailable(false, true));
        CHECK_FALSE(IsThreadGroupAvailable(false, false));
    }

    SECTION("只有功能不可用时的帖子群被隐藏，普通群总是显示")
    {
        CHECK_FALSE(IsHiddenGroup(kGroupTypeNormal, true));
        CHECK_FALSE(IsHiddenGroup(kGroupTypeNormal, false));
        CHECK_FALSE(IsHiddenGroup(kGroupTypeThread, true));
        CHECK(IsHiddenGroup(kGroupTypeThread, false));
        //不认识的类型按普通群
        const int kUnknownType = 7;
        CHECK_FALSE(IsHiddenGroup(kUnknownType, false));
    }

    SECTION("代发命令：只放行 2700~2799、包体非空、已连上")
    {
        const uint32_t kBody = 10;
        CHECK(CanSendThreadCommand(cmd_thread_list_posts, kBody, true));
        CHECK(CanSendThreadCommand(cmd_thread_delete_addendum, kBody, true));
        CHECK_FALSE(CanSendThreadCommand(cmd_thread_list_posts, kBody, false));
        CHECK_FALSE(CanSendThreadCommand(cmd_thread_list_posts, 0, true));
        CHECK_FALSE(CanSendThreadCommand(cmd_send_message, kBody, true));
        CHECK_FALSE(CanSendThreadCommand(kThreadCmdFirst - 1, kBody, true));
        CHECK_FALSE(CanSendThreadCommand(kThreadCmdLast + 1, kBody, true));
        CHECK_FALSE(IsThreadFrame(kThreadCmdLast + 1));
    }
}

TEST_CASE("TGE3 帖子群不可用时的隐藏：群列表、会话列表与提醒", "[threadgroup]")
{
    const int64_t kNormalGroup = 1;
    const int64_t kThreadGroupA = 2;
    const int64_t kThreadGroupB = 3;
    const int64_t kUncachedGroup = 4;   //群类型表里没有的群：按普通群
    std::map<int64_t, int> types;
    types[kNormalGroup] = kGroupTypeNormal;
    types[kThreadGroupA] = kGroupTypeThread;
    types[kThreadGroupB] = kGroupTypeThread;

    SECTION("隐藏的群号：可用时为空，不可用时为全部帖子群")
    {
        CHECK(HiddenThreadGroupIds(types, true).empty());
        const std::set<int64_t> hidden = HiddenThreadGroupIds(types, false);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden.count(kThreadGroupA) == 1);
        CHECK(hidden.count(kThreadGroupB) == 1);
    }

    SECTION("群列表与会话列表：去掉隐藏的群，顺序不变；单聊对端号与帖子群号相同也不受影响")
    {
        const std::set<int64_t> hidden = HiddenThreadGroupIds(types, false);
        std::vector<net::UserGroupItem> groups;
        const int64_t ids[] = { kThreadGroupA, kNormalGroup, kThreadGroupB, kUncachedGroup };
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        {
            net::UserGroupItem g;
            g.m_groupid = ids[i];
            groups.push_back(g);
        }
        const std::vector<net::UserGroupItem> visible = FilterGroups(groups, hidden);
        REQUIRE(visible.size() == 2);
        CHECK(visible[0].m_groupid == kNormalGroup);
        CHECK(visible[1].m_groupid == kUncachedGroup);
        CHECK(FilterGroups(groups, std::set<int64_t>()).size() == groups.size());

        std::vector<net::ConversationItem> convs;
        net::ConversationItem single;
        single.m_ctype = conversation_single;
        single.m_cid = kThreadGroupA;
        net::ConversationItem threadConv;
        threadConv.m_ctype = conversation_group;
        threadConv.m_cid = kThreadGroupA;
        net::ConversationItem normalConv;
        normalConv.m_ctype = conversation_group;
        normalConv.m_cid = kNormalGroup;
        convs.push_back(threadConv);
        convs.push_back(single);
        convs.push_back(normalConv);
        const std::vector<net::ConversationItem> shown = FilterConversations(convs, hidden);
        REQUIRE(shown.size() == 2);
        CHECK(shown[0].m_ctype == conversation_single);
        CHECK(shown[1].m_cid == kNormalGroup);
        CHECK(IsHiddenConversation(conversation_group, kThreadGroupA, hidden));
        CHECK_FALSE(IsHiddenConversation(conversation_single, kThreadGroupA, hidden));
        CHECK_FALSE(IsHiddenConversation(conversation_group, kNormalGroup, hidden));
    }

    SECTION("隐藏的会话来了消息：会话数据与未读照常更新，但不弹窗、不闪、不响")
    {
        balloonmain::ArrivalInput in;
        in.m_mode = notify_mode_allow;
        in.m_isGroup = true;
        in.m_countable = true;
        balloonmain::ArrivalDecision normal = balloonmain::DecideArrival(in);
        CHECK(normal.m_flashTray);
        CHECK(normal.m_playSound);
        in.m_hidden = true;
        balloonmain::ArrivalDecision hiddenDecision = balloonmain::DecideArrival(in);
        CHECK(hiddenDecision.m_updateRow);
        CHECK(hiddenDecision.m_countUnread);
        CHECK_FALSE(hiddenDecision.m_flashTray);
        CHECK_FALSE(hiddenDecision.m_playSound);
        //特别关注的群也不例外：不弹窗、不闪任务栏
        in.m_mode = notify_mode_starred;
        hiddenDecision = balloonmain::DecideArrival(in);
        CHECK_FALSE(hiddenDecision.m_popupWindow);
        CHECK_FALSE(hiddenDecision.m_flashTaskbar);
        CHECK_FALSE(hiddenDecision.m_playSound);
    }
}

namespace {

//TGE5 用的帖子卡片推送：群聊、发送者 10001、帖子 55
std::string ThreadCardMessageJson(int msgtype, const char* contentJson)
{
    const int64_t kSender = 10001;
    const int64_t kMsgId = 880001;
    const int64_t kSendTime = 1791500000000LL;
    Json::Value content;
    Json::CharReaderBuilder rb;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    std::string errs;
    reader->parse(contentJson, contentJson + strlen(contentJson), &content, &errs);
    Json::Value msg;
    msg["msgid"] = std::to_string(kMsgId);
    msg["clientmsgid"] = "threadpost-55";
    msg["ctype"] = conversation_group;
    msg["cid"] = std::to_string(kTestGroupId);
    msg["from"] = std::to_string(kSender);
    msg["to"] = std::to_string(kTestGroupId);
    msg["msgtype"] = msgtype;
    msg["content"] = content;
    msg["timestamp"] = static_cast<Json::Int64>(kSendTime);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, msg);
}

} // namespace

TEST_CASE("TGE5 帖子卡片与帖子动态卡片的摘要：推送、会话同步、本地记录与已删除形态", "[threadgroup]")
{
    typedef net::MessageProtocolClient MPC;
    SECTION("摘要文字")
    {
        CHECK(MPC::threadCardDigest(message_type_thread_post, 0, u8"周五团建", false) == u8"发帖：周五团建");
        CHECK(MPC::threadCardDigest(message_type_thread_post, 0, "", false) == u8"发帖");
        CHECK(MPC::threadCardDigest(message_type_thread_post, 0, u8"周五团建", true) == u8"帖子已删除");
        CHECK(MPC::threadCardDigest(message_type_thread_reply, thread_reply_comment, u8"我也去", false) == u8"留言：我也去");
        CHECK(MPC::threadCardDigest(message_type_thread_reply, thread_reply_reply, u8"可以", false) == u8"回复：可以");
        CHECK(MPC::threadCardDigest(message_type_thread_reply, thread_reply_addendum, u8"截止周四", false)
              == u8"补充了帖子：截止周四");
        CHECK(MPC::threadCardDigest(message_type_thread_reply, thread_reply_comment, "", false) == u8"留言");
        //不认识的 kind：只写类型
        const int kUnknownKind = 9;
        CHECK(MPC::threadCardDigest(message_type_thread_reply, kUnknownKind, "x", false) == u8"[帖子动态]");
        CHECK(MPC::digestForMsgType(message_type_thread_post, std::string()) == u8"[帖子]");
    }

    SECTION("实时推送：content 原文保留，摘要与会话同步的写法一致")
    {
        net::MessageNotify post;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_post,
            u8"{\"postid\":\"55\",\"title\":\"周五团建\",\"digest\":\"报名\",\"imagecount\":0}"), post));
        CHECK(post.m_msgtype == message_type_thread_post);
        CHECK_FALSE(post.m_threadCardContent.empty());
        CHECK(post.m_text.empty());
        CHECK(MPC::digestForNotify(post) == u8"发帖：周五团建");
        std::string arg;
        int kind = 0;
        int64_t postId = 0;
        bool deleted = false;
        REQUIRE(MPC::parseThreadCardContent(post.m_msgtype, post.m_threadCardContent, arg, kind, postId, deleted));
        CHECK(postId == 55);

        net::MessageNotify reply;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_reply,
            u8"{\"postid\":\"55\",\"commentid\":\"66\",\"addid\":\"0\",\"posttitle\":\"周五团建\",\"digest\":\"可以\",\"kind\":2}"),
            reply));
        CHECK(MPC::digestForNotify(reply) == u8"回复：可以");

        //卡片带的 @：帖子 @ 了 5308（2026-10-07 实机联调发现 @ 提醒不生效后补充）
        net::MessageNotify mentioned;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_post,
            u8"{\"postid\":\"55\",\"title\":\"t\",\"digest\":\"d\",\"imagecount\":0,\"mentions\":[\"5308\"]}"), mentioned));
        REQUIRE(mentioned.m_threadMentions.size() == 1);
        CHECK(MPC::threadCardMentions(mentioned, 5308));
        CHECK_FALSE(MPC::threadCardMentions(mentioned, 5127));
        CHECK_FALSE(MPC::threadCardMentions(post, 5308));

        //本地记录：预览与会话列表同一写法，搜索文字是标题与摘要
        const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(post, conversation_group, kTestGroupId);
        CHECK(balloonhistory::PreviewFromContent(message_type_thread_post, rec.m_content) == u8"发帖：周五团建");
        CHECK(balloonhistory::PreviewFromContent(message_type_thread_post, "{}") == u8"[帖子]");
        CHECK(rec.m_searchText == u8"周五团建 报名");
        net::MessageNotify restored;
        REQUIRE(balloonhistory::NotifyFromRecord(rec, restored));
        CHECK(restored.m_threadCardContent == post.m_threadCardContent);
    }

    SECTION("会话同步：按 digestarg 与 threadkind 在本地生成摘要，记下帖子号；已删除形态")
    {
        const std::string body =
            u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
            u8"{\"ctype\":2,\"cid\":\"600006\",\"lastmsgid\":\"9201\",\"lastmsg\":{\"from\":\"10001\",\"msgtype\":26,"
            u8"\"digest\":\"S\",\"digestarg\":\"截止周四\",\"threadkind\":3,\"threadpostid\":\"55\",\"timestamp\":1}},"
            u8"{\"ctype\":2,\"cid\":\"600007\",\"lastmsgid\":\"9202\",\"lastmsg\":{\"from\":\"10001\",\"msgtype\":25,"
            u8"\"digest\":\"S\",\"threaddeleted\":1,\"threadpostid\":\"56\",\"timestamp\":1}}"
            u8"]}";
        net::ConversationsResult out;
        REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
        REQUIRE(out.m_items.size() == 2);
        CHECK(out.m_items[0].m_digest == u8"补充了帖子：截止周四");
        CHECK(out.m_items[0].m_threadPostId == 55);
        CHECK(out.m_items[1].m_digest == u8"帖子已删除");
        CHECK(out.m_items[1].m_threadPostId == 56);
    }
}

TEST_CASE("TGE7 提醒判定：帖子群按帖子群窗口判断「正在看」", "[threadgroup]")
{
    //只有帖子群、模块报告正在看、且没有锁屏时才算正在看
    CHECK(IsWatchingThreadGroup(kGroupTypeThread, true, false));
    CHECK_FALSE(IsWatchingThreadGroup(kGroupTypeThread, false, false));
    CHECK_FALSE(IsWatchingThreadGroup(kGroupTypeThread, true, true));
    CHECK_FALSE(IsWatchingThreadGroup(kGroupTypeNormal, true, false));

    //正看着帖子群窗口时来了一张卡片：不计未读、不闪、不响；窗口没在看时照常提醒
    balloonmain::ArrivalInput in;
    in.m_mode = notify_mode_allow;
    in.m_isGroup = true;
    in.m_countable = true;
    in.m_windowOpen = IsWatchingThreadGroup(kGroupTypeThread, true, false);
    in.m_watching = in.m_windowOpen;
    balloonmain::ArrivalDecision watching = balloonmain::DecideArrival(in);
    CHECK_FALSE(watching.m_countUnread);
    CHECK_FALSE(watching.m_flashTray);
    CHECK_FALSE(watching.m_playSound);
    in.m_windowOpen = IsWatchingThreadGroup(kGroupTypeThread, false, false);
    in.m_watching = in.m_windowOpen;
    balloonmain::ArrivalDecision away = balloonmain::DecideArrival(in);
    CHECK(away.m_countUnread);
    CHECK(away.m_flashTray);
    CHECK(away.m_playSound);
}

TEST_CASE("TGE4 打开群会话的分流：普通群建群聊窗，帖子群交给模块，功能不可用时不建窗", "[threadgroup]")
{
    CHECK(DecideGroupOpen(kGroupTypeNormal, true) == group_open_chat_window);
    CHECK(DecideGroupOpen(kGroupTypeNormal, false) == group_open_chat_window);
    CHECK(DecideGroupOpen(kGroupTypeThread, true) == group_open_thread_window);
    CHECK(DecideGroupOpen(kGroupTypeThread, false) == group_open_nothing);
}

TEST_CASE("TGE12 @ 的显示名、摘要标记与帖子群的右键菜单", "[threadgroup]")
{
    SECTION("@ 的显示名：群名片优先，其次昵称，都没有时为空")
    {
        CHECK(ThreadMentionName(u8"群名片", u8"昵称") == L"群名片");
        CHECK(ThreadMentionName("", u8"昵称") == L"昵称");
        CHECK(ThreadMentionName("", "").empty());
    }

    SECTION("摘要开头的标记：取匹配到的最长那个，没有标记时为 0")
    {
        std::vector<std::wstring> tags;
        tags.push_back(L"[有人@我] ");
        tags.push_back(L"[关注] ");
        tags.push_back(L"[有人@我] [x] ");   //互为前缀时取最长的
        CHECK(DigestTagLength(L"[有人@我] 发帖：测试提醒", tags) == wcslen(L"[有人@我] "));
        CHECK(DigestTagLength(L"[关注] 留言：好的", tags) == wcslen(L"[关注] "));
        CHECK(DigestTagLength(L"[有人@我] [x] 回复：可以", tags) == wcslen(L"[有人@我] [x] "));
        CHECK(DigestTagLength(L"发帖：测试提醒", tags) == static_cast<size_t>(0));
        CHECK(DigestTagLength(L"", tags) == static_cast<size_t>(0));
        CHECK(DigestTagLength(L"[有人@我] ", std::vector<std::wstring>()) == static_cast<size_t>(0));
    }

    SECTION("帖子群的右键菜单：第一项为「打开帖子群」，没有字幕模式；普通群不变")
    {
        balloonmain::GroupMenuState state;
        state.m_canSubtitle = true;
        state.m_isThreadGroup = true;
        const std::vector<balloonmain::GroupMenuEntry> threadRows = balloonmain::BuildGroupMenuModel(state);
        REQUIRE_FALSE(threadRows.empty());
        CHECK(threadRows[0].m_id == balloonmain::kGroupMenuEnter);
        CHECK(std::wcscmp(threadRows[0].m_label, L"打开帖子群") == 0);
        bool hasSubtitle = false;
        for (size_t i = 0; i < threadRows.size(); ++i)
        {
            if (threadRows[i].m_id == balloonmain::kGroupMenuSubtitle)
                hasSubtitle = true;
        }
        CHECK_FALSE(hasSubtitle);

        state.m_isThreadGroup = false;
        const std::vector<balloonmain::GroupMenuEntry> normalRows = balloonmain::BuildGroupMenuModel(state);
        CHECK(std::wcscmp(normalRows[0].m_label, L"进入群聊") == 0);
        bool normalHasSubtitle = false;
        for (size_t i = 0; i < normalRows.size(); ++i)
        {
            if (normalRows[i].m_id == balloonmain::kGroupMenuSubtitle)
                normalHasSubtitle = true;
        }
        CHECK(normalHasSubtitle);
    }
}

namespace {

//一条帖子卡片或动态卡片消息（指定 msgid），用于写进本地库
std::string ThreadCardMessageJsonWithId(int msgtype, int64_t msgid, const char* contentJson)
{
    const int64_t kSender = 10001;
    const int64_t kSendTime = 1791500000000LL;
    Json::Value content;
    Json::CharReaderBuilder rb;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    std::string errs;
    reader->parse(contentJson, contentJson + strlen(contentJson), &content, &errs);
    Json::Value msg;
    msg["msgid"] = std::to_string(msgid);
    msg["clientmsgid"] = "threadcard-" + std::to_string(msgid);
    msg["ctype"] = conversation_group;
    msg["cid"] = std::to_string(kTestGroupId);
    msg["from"] = std::to_string(kSender);
    msg["to"] = std::to_string(kTestGroupId);
    msg["msgtype"] = msgtype;
    msg["content"] = content;
    msg["timestamp"] = static_cast<Json::Int64>(kSendTime + msgid);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, msg);
}

//本进程专用的临时本地库路径（先删掉同名旧库与 WAL 残留）
std::wstring FreshThreadTempDb(const wchar_t* name)
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

//把一张卡片写进本地库
void StoreThreadCard(balloonhistory::CV2MsgStore& store, int msgtype, int64_t msgid, const char* contentJson)
{
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(ThreadCardMessageJsonWithId(msgtype, msgid, contentJson), notify));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(notify, conversation_group, kTestGroupId)));
}

} // namespace

TEST_CASE("TGE15 卡片改写：推送与应答的解析、摘要与写回本地库", "[threadgroup]")
{
    SECTION("2721 推送：取群号与卡片，跳过不合法的项；带 code 且不为 0 时整体不要")
    {
        int64_t groupId = 0;
        std::vector<ThreadCardUpdate> cards;
        REQUIRE(ParseThreadCards(u8"{\"groupid\":\"600006\",\"cards\":["
                                 u8"{\"msgid\":\"1424\",\"msgtype\":25,\"content\":{\"postid\":\"5\",\"deleted\":1}},"
                                 u8"{\"msgid\":\"0\",\"msgtype\":25,\"content\":{}},"
                                 u8"{\"msgid\":\"9\",\"msgtype\":1,\"content\":{}},"
                                 u8"{\"msgid\":\"10\",\"msgtype\":26,\"content\":\"x\"},"
                                 u8"{\"msgid\":1425,\"msgtype\":26,\"content\":{\"kind\":1,\"deleted\":1}}]}",
                                 groupId, cards));
        CHECK(groupId == kTestGroupId);
        REQUIRE(cards.size() == 2);
        CHECK(cards[0].m_msgid == 1424);
        CHECK(cards[0].m_msgtype == message_type_thread_post);
        CHECK(cards[1].m_msgid == 1425);
        CHECK(cards[1].m_msgtype == message_type_thread_reply);
        CHECK(ParseThreadCards(u8"{\"code\":0,\"groupid\":\"600006\",\"cards\":[]}", groupId, cards));
        CHECK(cards.empty());
        CHECK_FALSE(ParseThreadCards(u8"{\"code\":1300,\"groupid\":\"600006\",\"cards\":[]}", groupId, cards));
        CHECK_FALSE(ParseThreadCards(u8"{\"cards\":[]}", groupId, cards));
        CHECK_FALSE(ParseThreadCards("not json", groupId, cards));
    }

    SECTION("被改写卡片的摘要：帖子已删除、留言 / 回复 / 补充已删除")
    {
        CHECK(ThreadCardDigestOf(message_type_thread_post, u8"{\"postid\":\"5\",\"deleted\":1}") == u8"帖子已删除");
        CHECK(ThreadCardDigestOf(message_type_thread_reply, u8"{\"postid\":\"5\",\"kind\":1,\"deleted\":1}")
              == u8"留言已删除");
        CHECK(ThreadCardDigestOf(message_type_thread_reply, u8"{\"postid\":\"5\",\"kind\":2,\"deleted\":1}")
              == u8"回复已删除");
        CHECK(ThreadCardDigestOf(message_type_thread_reply, u8"{\"postid\":\"5\",\"kind\":3,\"deleted\":1}")
              == u8"补充已删除");
        CHECK(ThreadCardDigestOf(message_type_thread_reply, "not json").empty());
    }

    SECTION("写回本地库：content、原始 JSON、搜索文本都换成已删除形态，置顶保留；本地没有那一行时不算失败")
    {
        const std::wstring path = FreshThreadTempDb(L"flamingo_tge15.db");
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        const int64_t kCardId = 900101;
        StoreThreadCard(store, message_type_thread_reply, kCardId,
                        u8"{\"postid\":\"5\",\"commentid\":\"7\",\"addid\":\"0\",\"posttitle\":\"周五团建\","
                        u8"\"digest\":\"违规内容\",\"kind\":1}");
        bool pinHit = false;
        REQUIRE(store.SetPinned(conversation_group, kTestGroupId, kCardId, true, 1791500000000LL, pinHit));
        REQUIRE(pinHit);

        ThreadCardUpdate update;
        update.m_msgid = kCardId;
        update.m_msgtype = message_type_thread_reply;
        update.m_content = u8"{\"postid\":\"5\",\"commentid\":\"7\",\"addid\":\"0\",\"kind\":1,\"deleted\":1}";
        bool hit = false;
        REQUIRE(ApplyThreadCardToStore(store, kTestGroupId, update, hit));
        CHECK(hit);

        balloonhistory::HistoryRecord row;
        bool found = false;
        REQUIRE(store.QueryRecord(conversation_group, kTestGroupId, kCardId, row, found));
        REQUIRE(found);
        CHECK(row.m_content.find("\"deleted\":1") != std::string::npos);
        CHECK(row.m_content.find(u8"违规内容") == std::string::npos);
        CHECK(row.m_raw.find(u8"违规内容") == std::string::npos);
        CHECK(row.m_searchText.find(u8"违规内容") == std::string::npos);
        CHECK(row.m_pinned);
        //从本地库还原出来的消息同样是已删除形态
        net::MessageNotify back;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, back));
        CHECK(net::MessageProtocolClient::digestForNotify(back) == u8"留言已删除");

        //本地没有那一行：不算失败，也没有命中
        ThreadCardUpdate missing = update;
        missing.m_msgid = kCardId + 1;
        REQUIRE(ApplyThreadCardToStore(store, kTestGroupId, missing, hit));
        CHECK_FALSE(hit);
        //不是帖子卡片的类型：拒绝
        ThreadCardUpdate wrongType = update;
        wrongType.m_msgtype = message_type_text;
        CHECK_FALSE(ApplyThreadCardToStore(store, kTestGroupId, wrongType, hit));
    }
}

//菜单模型里有没有某一项
static bool HasMenuId(const std::vector<balloonchatwnd::MentionMenuEntry>& items, int id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == id)
            return true;
    }
    return false;
}

//菜单模型的分隔线是否都夹在两项之间：不在首尾，也不相邻
static bool SeparatorsWellFormed(const std::vector<balloonchatwnd::MentionMenuEntry>& items)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator)
            continue;
        if (i == 0 || i + 1 == items.size() || items[i - 1].isSeparator)
            return false;
    }
    return true;
}

TEST_CASE("TGE8 群成员面板的右键菜单：去掉「@TA」，其余不变", "[threadgroup]")
{
    const long long kSelf = 100;            //本人
    const long long kTarget = 200;          //被右键的成员
    const int kRoleMember = 0;              //普通成员
    const int kRoleOwner = 2;               //群主

    SECTION("群主对普通成员：只少「@TA」一项，「发消息」仍在第一组，分隔线仍然合法")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> full =
            balloonchatwnd::BuildMentionMenuModel(kTarget, kSelf, true, kRoleOwner, kRoleMember);
        REQUIRE(HasMenuId(full, balloonchatwnd::kMentionMenuMentionMore));
        const std::vector<balloonchatwnd::MentionMenuEntry> panel = balloonchatwnd::RemoveMentionMenuEntry(full);
        CHECK(panel.size() + 1 == full.size());
        CHECK_FALSE(HasMenuId(panel, balloonchatwnd::kMentionMenuMentionMore));
        REQUIRE_FALSE(panel.empty());
        CHECK(panel[0].id == balloonchatwnd::kMentionMenuChat);
        CHECK(SeparatorsWellFormed(panel));
        //管理项与移出都还在
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuSetAdmin));
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuTransferOwner));
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuKick));
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuProfile));
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuStarred));
        CHECK(HasMenuId(panel, balloonchatwnd::kMentionMenuGroupFollow));
    }

    SECTION("普通成员对普通成员：没有管理项，去掉「@TA」后分隔线仍然合法")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> panel = balloonchatwnd::RemoveMentionMenuEntry(
            balloonchatwnd::BuildMentionMenuModel(kTarget, kSelf, true, kRoleMember, kRoleMember));
        CHECK_FALSE(HasMenuId(panel, balloonchatwnd::kMentionMenuMentionMore));
        CHECK_FALSE(HasMenuId(panel, balloonchatwnd::kMentionMenuKick));
        CHECK(SeparatorsWellFormed(panel));
    }

    SECTION("本人那一行：原本只有「查看资料」，去掉之后不变")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> self =
            balloonchatwnd::BuildMentionMenuModel(kSelf, kSelf, true, kRoleOwner, kRoleOwner);
        const std::vector<balloonchatwnd::MentionMenuEntry> panel = balloonchatwnd::RemoveMentionMenuEntry(self);
        REQUIRE(panel.size() == self.size());
        REQUIRE(panel.size() == 1);
        CHECK(panel[0].id == balloonchatwnd::kMentionMenuProfile);
    }

    SECTION("空模型（@所有人）去掉之后仍为空")
    {
        CHECK(balloonchatwnd::RemoveMentionMenuEntry(std::vector<balloonchatwnd::MentionMenuEntry>()).empty());
    }
}

TEST_CASE("TGE13 消息记录里帖子卡片的图片：取图片列表与缩略图张数", "[threadgroup]")
{
    SECTION("帖子卡片带 4 张图：按顺序取出，字段齐全；缺 fileid 的项跳过")
    {
        const std::string content = u8"{\"threadcard\":{\"postid\":\"7\",\"title\":\"t\",\"images\":["
                                    u8"{\"fileid\":\"f1\",\"md5\":\"m1\",\"width\":800,\"height\":600,\"size\":1024,\"thumbfileid\":\"t1\"},"
                                    u8"{\"fileid\":\"f2\",\"md5\":\"m2\",\"width\":10,\"height\":20,\"thumbfileid\":\"\"},"
                                    u8"{\"md5\":\"nofileid\"},"
                                    u8"{\"fileid\":\"f3\",\"md5\":\"m3\"},"
                                    u8"{\"fileid\":\"f4\",\"md5\":\"m4\"}]}}";
        const balloonhistory::HistoryContentBrief brief =
            balloonhistory::ParseContentBrief(message_type_thread_post, content);
        REQUIRE(brief.m_threadImages.size() == 4);
        CHECK(brief.m_threadImages[0].m_fileId == "f1");
        CHECK(brief.m_threadImages[0].m_md5 == "m1");
        CHECK(brief.m_threadImages[0].m_thumbFileId == "t1");
        CHECK(brief.m_threadImages[0].m_width == 800);
        CHECK(brief.m_threadImages[0].m_height == 600);
        CHECK(brief.m_threadImages[0].m_size == 1024);
        CHECK(brief.m_threadImages[1].m_thumbFileId.empty());
        CHECK(brief.m_threadImages[2].m_fileId == "f3");
        CHECK(brief.m_threadImages[2].m_width == 0);
        CHECK(brief.m_threadImages[3].m_fileId == "f4");

        const balloonhistory::HistoryThumbStrip strip = balloonhistory::ComputeThumbStrip(brief.m_threadImages.size());
        CHECK(strip.m_shown == 3);
        CHECK(strip.m_extra == 1);
    }

    SECTION("补充卡片同样取图片；已删除形态不取")
    {
        const std::string addendum = u8"{\"threadcard\":{\"postid\":\"7\",\"kind\":3,\"digest\":\"d\","
                                     u8"\"images\":[{\"fileid\":\"a1\",\"md5\":\"x1\"}]}}";
        CHECK(balloonhistory::ParseContentBrief(message_type_thread_reply, addendum).m_threadImages.size() == 1);

        const std::string deleted = u8"{\"threadcard\":{\"postid\":\"7\",\"deleted\":1,"
                                    u8"\"images\":[{\"fileid\":\"a1\",\"md5\":\"x1\"}]}}";
        CHECK(balloonhistory::ParseContentBrief(message_type_thread_post, deleted).m_threadImages.empty());
    }

    SECTION("没有图片、content 不合法或不是帖子卡片：图片列表为空")
    {
        CHECK(balloonhistory::ParseContentBrief(message_type_thread_post,
                                                u8"{\"threadcard\":{\"postid\":\"7\"}}").m_threadImages.empty());
        CHECK(balloonhistory::ParseContentBrief(message_type_thread_post, "not json").m_threadImages.empty());
        CHECK(balloonhistory::ParseContentBrief(message_type_text,
                                                u8"{\"threadcard\":{\"images\":[{\"fileid\":\"f\"}]}}")
                  .m_threadImages.empty());
    }

    SECTION("缩略图张数：不足 3 张全显示，没有「+N」；0 张时什么也不显示")
    {
        CHECK(balloonhistory::ComputeThumbStrip(0).m_shown == 0);
        CHECK(balloonhistory::ComputeThumbStrip(0).m_extra == 0);
        CHECK(balloonhistory::ComputeThumbStrip(2).m_shown == 2);
        CHECK(balloonhistory::ComputeThumbStrip(2).m_extra == 0);
        CHECK(balloonhistory::ComputeThumbStrip(3).m_shown == 3);
        CHECK(balloonhistory::ComputeThumbStrip(3).m_extra == 0);
        CHECK(balloonhistory::ComputeThumbStrip(9).m_extra == 6);
    }
}

TEST_CASE("TGE17 登录成功时帖子群模块的处理：激活、停用或不动", "[threadgroup]")
{
    //开关打开：无论模块此前是否激活，都要激活（已打开的窗口重新登记「正在查看」并刷新）
    CHECK(DecideThreadLogin(true, false) == thread_login_activate);
    CHECK(DecideThreadLogin(true, true) == thread_login_activate);
    //断线期间开关被关掉，模块还处于已登录状态：停用，关闭帖子群窗口
    CHECK(DecideThreadLogin(false, true) == thread_login_deactivate);
    //开关关闭、模块本来就没有激活（首次登录即为关闭）：什么也不做
    CHECK(DecideThreadLogin(false, false) == thread_login_none);
}

namespace {

//导航栏里各页导航项的 ctrl id，按从上到下的顺序（分组标题与间隔没有页面 id，不计入）
std::vector<int> NavPageIds(const balloonwjui::DuiControl* nav)
{
    std::vector<int> ids;
    for (size_t i = 0; i < nav->Children().size(); ++i)
    {
        const int id = static_cast<int>(nav->Children()[i]->GetCtrlId());
        if (id >= balloongroupmanage::kPageMembers && id <= balloongroupmanage::kPageBots)
            ids.push_back(id);
    }
    return ids;
}

//两份菜单行模型逐项相同：标识、文字、可点与否、是否分隔线
bool SameMenu(const std::vector<balloonchatwnd::MentionMenuEntry>& a,
              const std::vector<balloonchatwnd::MentionMenuEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].isSeparator != b[i].isSeparator)
            return false;
        if (a[i].isSeparator)
            continue;
        if (a[i].id != b[i].id || a[i].enabled != b[i].enabled || wcscmp(a[i].label, b[i].label) != 0)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("TGE18 帖子群的群管理窗口：没有「群公告」页与「群公告」一行，其余页与普通群相同", "[threadgroup]")
{
    using namespace balloongroupmanage;
    GroupManageModel model;
    model.m_myRole = kRoleOwner;
    model.m_botsAvailable = false;

    SECTION("群主：普通群 8 页，帖子群少了「群公告」，其余 7 页顺序不变")
    {
        std::vector<int> normal;
        normal.push_back(kPageMembers);
        normal.push_back(kPageApproval);
        normal.push_back(kPageMute);
        normal.push_back(kPageLinks);
        normal.push_back(kPageAnnouncement);
        normal.push_back(kPageBasicInfo);
        normal.push_back(kPagePerms);
        normal.push_back(kPageDanger);
        model.m_isThreadGroup = false;
        CHECK(NavPageIds(BuildNav(model).get()) == normal);

        std::vector<int> thread = normal;
        thread.erase(thread.begin() + 4);
        model.m_isThreadGroup = true;
        CHECK(NavPageIds(BuildNav(model).get()) == thread);
        CHECK(thread.size() == 7);
    }

    SECTION("管理员：帖子群同样没有「群公告」，也没有危险区域")
    {
        model.m_myRole = kRoleAdmin;
        model.m_isThreadGroup = true;
        std::vector<int> expect;
        expect.push_back(kPageMembers);
        expect.push_back(kPageApproval);
        expect.push_back(kPageMute);
        expect.push_back(kPageLinks);
        expect.push_back(kPageBasicInfo);
        expect.push_back(kPagePerms);
        CHECK(NavPageIds(BuildNav(model).get()) == expect);
    }

    SECTION("判定函数：帖子群一律不可见，普通群按角色")
    {
        CHECK(IsAnnouncementPageVisible(kRoleOwner, false));
        CHECK(IsAnnouncementPageVisible(kRoleAdmin, false));
        CHECK_FALSE(IsAnnouncementPageVisible(kRoleMember, false));
        CHECK_FALSE(IsAnnouncementPageVisible(kRoleOwner, true));
        CHECK_FALSE(IsAnnouncementPageVisible(kRoleAdmin, true));
    }

    SECTION("基本信息页：普通群有「群公告」一行与「去管理」，帖子群没有")
    {
        //群资料到达之前基本信息页只显示加载状态（BuildBasicInfoPage），先置为已加载
        model.m_groupInfoState = kLoadDone;
        model.m_isThreadGroup = false;
        CHECK(BuildPage(kPageBasicInfo, model)->FindCtrlById(kIdBtnAnnounceGoto) != NULL);
        model.m_isThreadGroup = true;
        CHECK(BuildPage(kPageBasicInfo, model)->FindCtrlById(kIdBtnAnnounceGoto) == NULL);
    }
}

TEST_CASE("TGE19 点正文里的 @某人：成员菜单与群成员面板的右键菜单逐项相同", "[threadgroup]")
{
    const long long kSelf = 100;     //本人
    const long long kTarget = 200;   //被 @ 的人
    const int kRoleMember = 0;       //普通成员
    const int kRoleAdmin = 1;        //管理员
    const int kRoleOwner = 2;        //群主

    SECTION("对方在群里：本人与对方的各种角色、关注状态下都与群成员面板相同，没有「@TA」")
    {
        for (int my = kRoleMember; my <= kRoleOwner; ++my)
        {
            for (int target = kRoleMember; target <= kRoleOwner; ++target)
            {
                for (int flags = 0; flags < 4; ++flags)
                {
                    const bool starred = (flags & 1) != 0;
                    const bool followed = (flags & 2) != 0;
                    const std::vector<balloonchatwnd::MentionMenuEntry> menu =
                        balloonchatwnd::BuildThreadMentionMenuModel(kTarget, kSelf, my, target, starred, followed);
                    const std::vector<balloonchatwnd::MentionMenuEntry> panel = balloonchatwnd::RemoveMentionMenuEntry(
                        balloonchatwnd::BuildMentionMenuModel(kTarget, kSelf, true, my, target, starred, followed));
                    INFO("my role " << my << ", target role " << target << ", flags " << flags);
                    CHECK(SameMenu(menu, panel));
                    CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuMentionMore));
                    CHECK(SeparatorsWellFormed(menu));
                }
            }
        }
    }

    SECTION("@ 的是本人：只有「查看资料」")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> menu =
            balloonchatwnd::BuildThreadMentionMenuModel(kSelf, kSelf, kRoleOwner, kRoleOwner, false, false);
        REQUIRE(menu.size() == 1);
        CHECK(menu[0].id == balloonchatwnd::kMentionMenuProfile);
    }

    SECTION("对方已退群：按成员菜单原有规则，没有管理类与「关注 TA 在本群的发言」")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> menu = balloonchatwnd::BuildThreadMentionMenuModel(
            kTarget, kSelf, kRoleOwner, balloonchatwnd::kThreadTargetRoleLeft, false, false);
        CHECK(SameMenu(menu, balloonchatwnd::RemoveMentionMenuEntry(
                                 balloonchatwnd::BuildMentionMenuModel(kTarget, kSelf, false, kRoleOwner, -1))));
        CHECK(HasMenuId(menu, balloonchatwnd::kMentionMenuProfile));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuGroupFollow));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuSetAdmin));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuTransferOwner));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuKick));
    }

    SECTION("成员名单还没取到：按仍在群里处理，但对方角色未知，管理类的几项不出现")
    {
        const std::vector<balloonchatwnd::MentionMenuEntry> menu = balloonchatwnd::BuildThreadMentionMenuModel(
            kTarget, kSelf, kRoleOwner, balloonchatwnd::kThreadTargetRoleUnknown, false, false);
        CHECK(HasMenuId(menu, balloonchatwnd::kMentionMenuChat));
        CHECK(HasMenuId(menu, balloonchatwnd::kMentionMenuProfile));
        CHECK(HasMenuId(menu, balloonchatwnd::kMentionMenuGroupFollow));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuSetAdmin));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuCancelAdmin));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuTransferOwner));
        CHECK_FALSE(HasMenuId(menu, balloonchatwnd::kMentionMenuKick));
        CHECK(SeparatorsWellFormed(menu));
    }

    SECTION("@所有人：没有菜单")
    {
        CHECK(balloonchatwnd::BuildThreadMentionMenuModel(0, kSelf, kRoleOwner, kRoleMember, false, false).empty());
    }

    SECTION("插件接口与菜单模型里 targetRole 的两个特殊取值相同")
    {
        CHECK(threadgroup::kMemberRoleLeft == balloonchatwnd::kThreadTargetRoleLeft);
        CHECK(threadgroup::kMemberRoleUnknown == balloonchatwnd::kThreadTargetRoleUnknown);
    }
}

TEST_CASE("TGE20 卡片 @ 了所有人：除发布者外每个成员都按被 @ 处理（摘要「[有人@我] 」与提醒）", "[threadgroup]")
{
    typedef net::MessageProtocolClient MPC;
    const int64_t kPublisher = 10001;   //ThreadCardMessageJson 里的发送人
    const int64_t kMemberA = 5308;      //没有被单独 @ 的成员
    const int64_t kMemberB = 5127;      //另一个成员

    SECTION("帖子卡片带 mentionall：任何成员都算被 @，发布者本人不算")
    {
        net::MessageNotify post;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_post,
            u8"{\"postid\":\"55\",\"title\":\"t\",\"digest\":\"d\",\"imagecount\":0,\"mentionall\":true}"), post));
        CHECK(post.m_threadMentionAll);
        CHECK(MPC::threadCardMentions(post, kMemberA));
        CHECK(MPC::threadCardMentions(post, kMemberB));
        CHECK(balloonmain::IsGroupMessageAtMe(post, kMemberA));
        CHECK(balloonmain::IsGroupMessageAtMe(post, kMemberB));
        CHECK_FALSE(balloonmain::IsGroupMessageAtMe(post, kPublisher));
        //会话摘要：被 @ 的成员前面加「[有人@我] 」，发布者本人不加
        const std::string digest = MPC::digestForNotify(post);
        const std::string prefix = balloonmain::AtMeDigestPrefix();
        CHECK(balloonmain::DecorateGroupDigest(digest, balloonmain::IsGroupMessageAtMe(post, kMemberA), false)
              == prefix + digest);
        CHECK(balloonmain::DecorateGroupDigest(digest, balloonmain::IsGroupMessageAtMe(post, kPublisher), false)
              == digest);
    }

    SECTION("动态卡片（留言）带 mentionall：同样按被 @ 处理")
    {
        net::MessageNotify reply;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_reply,
            u8"{\"postid\":\"55\",\"commentid\":\"66\",\"addid\":\"0\",\"posttitle\":\"t\",\"digest\":\"d\",\"kind\":1,"
            u8"\"mentionall\":true}"), reply));
        CHECK(reply.m_threadMentionAll);
        CHECK(balloonmain::IsGroupMessageAtMe(reply, kMemberA));
        CHECK_FALSE(balloonmain::IsGroupMessageAtMe(reply, kPublisher));
    }

    SECTION("不带 mentionall：只有 mentions 里的人算被 @（与原来相同）")
    {
        net::MessageNotify post;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_post,
            u8"{\"postid\":\"55\",\"title\":\"t\",\"digest\":\"d\",\"imagecount\":0,\"mentions\":[\"5308\"]}"), post));
        CHECK_FALSE(post.m_threadMentionAll);
        CHECK(balloonmain::IsGroupMessageAtMe(post, kMemberA));
        CHECK_FALSE(balloonmain::IsGroupMessageAtMe(post, kMemberB));
        //mentionall 写成 false 或不是布尔值：不算 @ 了所有人
        net::MessageNotify notAll;
        REQUIRE(MPC::parseMessageNotify(ThreadCardMessageJson(message_type_thread_post,
            u8"{\"postid\":\"55\",\"title\":\"t\",\"digest\":\"d\",\"imagecount\":0,\"mentionall\":1}"), notAll));
        CHECK_FALSE(notAll.m_threadMentionAll);
        CHECK_FALSE(balloonmain::IsGroupMessageAtMe(notAll, kMemberB));
    }
}
