/**
 *  群提醒模式「只提醒 @我和关注的人」与群关注名单（客户端侧）单元测试，GroupFollowTests.cpp，标签 [groupfollow]
 *
 *  群的提醒模式新增取值 5：群里其他消息照常计未读、不提醒，只有被 @ 或「关注的成员」发言时才闪托盘、
 *  响提示音；每个群单独维护一份关注名单（存服务端，1501 下发、1330 设置、1331 同步）。
 *  方案见 docs/消息提醒模式与消息盒子方案.md §12。本文件覆盖：
 *    MF-C1  协议与名单缓存：解析 1501 的 groupfollows、1331，构造 1330；GroupFollowModel 的读写与上限
 *    MF-C2  DecideArrival 模式 5 的判定
 *    MF-C3  会话摘要前缀「[关注] 」与「[有人@我] 」
 *    MF-C4  模式 5 只出现在群的各处选项里；胶囊灰色、不计入红色总数；批量窗对单聊过滤掉这一项
 *    MF-C5  群成员右键菜单：「关注 TA 在本群的发言」与改名后的「设为特别关注联系人」
 *
 *  典型用法：FlamingoUnit.exe "[groupfollow]"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"   // conversation_single / conversation_group / notify_mode_*
#include "../UI/BalloonMain/GroupFollowModel.h"
#include "../UI/BalloonMain/MsgBoxModel.h"            // AtMeDigestPrefix
#include "../UI/BalloonMain/NotifyModeModel.h"
#include "../UI/BalloonMain/NotifyModePolicy.h"
#include "../UI/BalloonMain/NotifyModeMenu.h"
#include "../UI/BalloonNotifyMode/NotifyManageModel.h"
#include "../UI/BalloonChatWnd/GroupMentionMenu.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

#include <string>
#include <vector>

namespace {

//本组用到的群与成员。群 id 取超过 double 精确范围的值，顺带核对字符串编码
const int64_t kMfGroup  = 9007199254740995LL;
const int64_t kMfGroup2 = 268435621LL;
const int64_t kMfAlice  = 300001;
const int64_t kMfBob    = 300002;
const int64_t kMfCarol  = 300003;
//本人 userid（菜单用例判「是不是自己」）
const int64_t kMfSelf   = 300099;

//群成员角色：普通成员（grouprole::NetGroupRole 的网络侧取值）
const int kMfNetMember = 0;

//消息摘要原文
const char* const kMfDigest = "hello";

//把 JSON 文本解析成 Json::Value，失败返回 false
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//造一份模式 5 的群消息到达输入：别人发的、窗口没开、没在看、没锁屏
balloonmain::ArrivalInput MakeFollowArrival()
{
    balloonmain::ArrivalInput in;
    in.m_mode       = notify_mode_mention_follow;
    in.m_isGroup    = true;
    in.m_atMe       = false;
    in.m_countable  = true;
    in.m_windowOpen = false;
    in.m_watching   = false;
    in.m_isShake    = false;
    return in;
}

//在菜单行模型里找某一项；找不到返回 -1
int IndexOfMention(const std::vector<balloonchatwnd::MentionMenuEntry>& items, balloonchatwnd::MentionMenuId id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == id)
            return (int)i;
    }
    return -1;
}

//造一条会话
net::ConversationItem MakeConv(int ctype, int64_t cid, int unread)
{
    net::ConversationItem c;
    c.m_ctype  = ctype;
    c.m_cid    = cid;
    c.m_unread = unread;
    return c;
}

} // anonymous namespace

TEST_CASE("MF-C1a 1501 的 groupfollows：只收名单不为空、字段合法的群；老服务端不带时为空", "[groupfollow]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":[],\"notifymodes\":[],"
        "\"groupfollows\":["
        "{\"groupid\":\"9007199254740995\",\"targets\":[\"300001\",\"300002\"]},"
        "{\"groupid\":\"268435621\",\"targets\":[]},"
        "{\"groupid\":\"0\",\"targets\":[\"300003\"]},"
        "{\"groupid\":\"268435622\",\"targets\":\"300003\"}"
        "]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_groupFollows.size() == 1);
    CHECK(out.m_groupFollows[0].m_groupid == kMfGroup);
    REQUIRE(out.m_groupFollows[0].m_targets.size() == 2);
    CHECK(out.m_groupFollows[0].m_targets[0] == (uint32_t)kMfAlice);
    CHECK(out.m_groupFollows[0].m_targets[1] == (uint32_t)kMfBob);

    net::ConversationsResult old;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(
        "{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":[]}", old));
    CHECK(old.m_groupFollows.empty());
}

TEST_CASE("MF-C1b 1331 解析与 1330 构造：包体同形，空名单写成空数组", "[groupfollow]")
{
    std::vector<uint32_t> targets;
    targets.push_back((uint32_t)kMfAlice);
    targets.push_back((uint32_t)kMfCarol);
    const std::string req = net::ConversationsProtocolClient::buildSetGroupFollowRequest(kMfGroup, targets);
    Json::Value root;
    REQUIRE(ParseJson(req, root));
    CHECK(root["groupid"].asString() == std::to_string(kMfGroup));
    REQUIRE(root["targets"].isArray());
    REQUIRE(root["targets"].size() == 2);
    CHECK(root["targets"][0].asString() == std::to_string(kMfAlice));

    //推送与请求同形：用请求包体当推送解析，结果一致
    net::GroupFollowEntry parsed;
    REQUIRE(net::ConversationsProtocolClient::parseGroupFollowChangedNotify(req, parsed));
    CHECK(parsed.m_groupid == kMfGroup);
    CHECK(parsed.m_targets == targets);

    //清空：targets 是空数组，不能省略
    Json::Value empty;
    REQUIRE(ParseJson(net::ConversationsProtocolClient::buildSetGroupFollowRequest(kMfGroup, std::vector<uint32_t>()),
                      empty));
    REQUIRE(empty["targets"].isArray());
    CHECK(empty["targets"].size() == 0);
    net::GroupFollowEntry cleared;
    REQUIRE(net::ConversationsProtocolClient::parseGroupFollowChangedNotify(
        "{\"groupid\":\"268435621\",\"targets\":[]}", cleared));
    CHECK(cleared.m_targets.empty());

    //坏推送：群 id 非法、targets 不是数组
    net::GroupFollowEntry bad;
    CHECK_FALSE(net::ConversationsProtocolClient::parseGroupFollowChangedNotify("{\"groupid\":\"0\",\"targets\":[]}", bad));
    CHECK_FALSE(net::ConversationsProtocolClient::parseGroupFollowChangedNotify("{\"groupid\":\"1\"}", bad));
    CHECK_FALSE(net::ConversationsProtocolClient::parseGroupFollowChangedNotify("not json", bad));
}

TEST_CASE("MF-C1c 名单缓存：整体建立、整份覆盖一个群、退群删除、切换时的上限", "[groupfollow]")
{
    std::vector<net::GroupFollowEntry> entries(1);
    entries[0].m_groupid = kMfGroup;
    entries[0].m_targets.push_back((uint32_t)kMfAlice);
    balloonmain::GroupFollowModel model;
    model.Reset(entries);
    CHECK(model.IsFollowed(kMfGroup, kMfAlice));
    CHECK_FALSE(model.IsFollowed(kMfGroup, kMfBob));
    CHECK_FALSE(model.IsFollowed(kMfGroup2, kMfAlice));   //名单按群分开

    //1331：整份覆盖
    net::GroupFollowEntry n;
    n.m_groupid = kMfGroup;
    n.m_targets.push_back((uint32_t)kMfBob);
    model.ApplyGroup(n);
    CHECK_FALSE(model.IsFollowed(kMfGroup, kMfAlice));
    CHECK(model.IsFollowed(kMfGroup, kMfBob));

    //切换：加入、去掉、状态没变
    std::vector<uint32_t> next;
    CHECK(model.BuildToggled(kMfGroup, kMfCarol, true, next) == balloonmain::kFollowToggleOk);
    CHECK(next.size() == 2);
    CHECK(model.IsFollowed(kMfGroup, kMfBob));   //只算新名单，不改缓存
    CHECK_FALSE(model.IsFollowed(kMfGroup, kMfCarol));
    CHECK(model.BuildToggled(kMfGroup, kMfBob, false, next) == balloonmain::kFollowToggleOk);
    CHECK(next.empty());
    CHECK(model.BuildToggled(kMfGroup, kMfBob, true, next) == balloonmain::kFollowToggleUnchanged);

    //名单已满：不能再关注新的人，但可以取消
    net::GroupFollowEntry full;
    full.m_groupid = kMfGroup2;
    for (size_t i = 0; i < net::kGroupFollowMax; ++i)
        full.m_targets.push_back((uint32_t)(kMfAlice + 100 + (int64_t)i));
    model.ApplyGroup(full);
    CHECK(model.BuildToggled(kMfGroup2, kMfCarol, true, next) == balloonmain::kFollowToggleFull);
    CHECK(model.BuildToggled(kMfGroup2, full.m_targets[0], false, next) == balloonmain::kFollowToggleOk);
    CHECK(next.size() == net::kGroupFollowMax - 1);

    //空名单的推送：删掉这个群
    net::GroupFollowEntry none;
    none.m_groupid = kMfGroup;
    model.ApplyGroup(none);
    CHECK(model.GetTargets(kMfGroup).empty());
    //退群
    model.RemoveGroup(kMfGroup2);
    CHECK(model.GroupCount() == 0);
}

TEST_CASE("MF-C2 模式 5：普通消息计未读不提醒；被 @、@所有人、关注的人发言时闪托盘并响", "[groupfollow]")
{
    //普通消息：计未读、更新会话行，不闪不响、不弹窗
    const balloonmain::ArrivalDecision plain = balloonmain::DecideArrival(MakeFollowArrival());
    CHECK(plain.m_updateRow);
    CHECK(plain.m_countUnread);
    CHECK_FALSE(plain.m_flashTray);
    CHECK_FALSE(plain.m_playSound);
    CHECK_FALSE(plain.m_popupWindow);

    //被 @（@所有人在调用方同样算作 m_atMe）
    balloonmain::ArrivalInput at = MakeFollowArrival();
    at.m_atMe = true;
    const balloonmain::ArrivalDecision atMe = balloonmain::DecideArrival(at);
    CHECK(atMe.m_countUnread);
    CHECK(atMe.m_flashTray);
    CHECK(atMe.m_playSound);
    CHECK_FALSE(atMe.m_popupWindow);

    //关注的人发言
    balloonmain::ArrivalInput followed = MakeFollowArrival();
    followed.m_fromFollowed = true;
    const balloonmain::ArrivalDecision byFollowed = balloonmain::DecideArrival(followed);
    CHECK(byFollowed.m_countUnread);
    CHECK(byFollowed.m_flashTray);
    CHECK(byFollowed.m_playSound);
    CHECK_FALSE(byFollowed.m_popupWindow);

    //聊天窗开着：消息已渲染进窗口，不另外提醒
    balloonmain::ArrivalInput open = followed;
    open.m_windowOpen = true;
    const balloonmain::ArrivalDecision opened = balloonmain::DecideArrival(open);
    CHECK(opened.m_countUnread);
    CHECK_FALSE(opened.m_flashTray);
    CHECK_FALSE(opened.m_playSound);

    //正看着聊天窗：不计未读、不提醒
    balloonmain::ArrivalInput watching = followed;
    watching.m_windowOpen = true;
    watching.m_watching = true;
    CHECK_FALSE(balloonmain::DecideArrival(watching).m_countUnread);

    //锁屏：托盘照常闪，不响
    balloonmain::ArrivalInput locked = followed;
    locked.m_screenLocked = true;
    const balloonmain::ArrivalDecision lockedD = balloonmain::DecideArrival(locked);
    CHECK(lockedD.m_flashTray);
    CHECK_FALSE(lockedD.m_playSound);

    //其它模式下关注名单不影响提醒：接收不提醒的群，关注的人发言照样不闪
    balloonmain::ArrivalInput silent = followed;
    silent.m_mode = notify_mode_silent;
    CHECK_FALSE(balloonmain::DecideArrival(silent).m_flashTray);
}

TEST_CASE("MF-C3 摘要前缀：关注的人发言加「[关注] 」，同时被 @ 只加「[有人@我] 」，都不是时原样", "[groupfollow]")
{
    const std::string digest(kMfDigest);
    CHECK(balloonmain::DecorateGroupDigest(digest, false, true) == balloonmain::FollowDigestPrefix() + digest);
    CHECK(balloonmain::FollowDigestPrefix() == std::string(u8"[关注] "));
    CHECK(balloonmain::DecorateGroupDigest(digest, true, true) == balloonmain::AtMeDigestPrefix() + digest);
    CHECK(balloonmain::DecorateGroupDigest(digest, true, false) == balloonmain::AtMeDigestPrefix() + digest);
    //自己发的消息由调用方把两个标志都传成 false
    CHECK(balloonmain::DecorateGroupDigest(digest, false, false) == digest);
}

TEST_CASE("MF-C4 模式 5 只给群：选项、文字、灰色胶囊、不计入红色总数，批量窗的单聊不提交", "[groupfollow]")
{
    //群的选项里有，单聊没有
    const std::vector<int> group = balloonmain::GetNotifyModeChoices(true);
    const std::vector<int> single = balloonmain::GetNotifyModeChoices(false);
    bool groupHas = false;
    for (size_t i = 0; i < group.size(); ++i)
        groupHas = groupHas || (group[i] == notify_mode_mention_follow);
    CHECK(groupHas);
    for (size_t i = 0; i < single.size(); ++i)
        CHECK(single[i] != notify_mode_mention_follow);
    CHECK(std::wstring(balloonmain::GetNotifyModeLabel(notify_mode_mention_follow)) == L"只提醒 @我和关注的人");
    //资料窗下拉框：单聊传入模式 5 时按允许提醒
    CHECK(balloonmain::NotifyModeChoiceIndex(false, notify_mode_mention_follow)
          == balloonmain::NotifyModeChoiceIndex(false, notify_mode_allow));

    //灰色胶囊、改成这个模式时清掉托盘闪烁、模式表认这个取值
    CHECK(balloonmain::IsQuietBadgeMode(notify_mode_mention_follow));
    CHECK(balloonmain::ShouldClearTrayOnModeChange(notify_mode_mention_follow));
    balloonmain::NotifyModeModel accept;
    CHECK(accept.SetMode(conversation_group, kMfGroup, notify_mode_mention_follow));
    CHECK(accept.GetMode(conversation_group, kMfGroup) == notify_mode_mention_follow);

    //不计入「消息」页签的红色总数
    std::vector<net::ConversationItem> convs;
    const int kAllowUnread = 3;
    const int kFollowUnread = 5;
    convs.push_back(MakeConv(conversation_group, kMfGroup, kFollowUnread));
    convs.push_back(MakeConv(conversation_group, kMfGroup2, kAllowUnread));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, kMfGroup, notify_mode_mention_follow);
    CHECK(balloonmain::ComputeAlertingUnread(convs, modes) == kAllowUnread);

    //批量窗：单聊页设模式 5 一条也不提交，群页照常
    balloonnotifymode::ManageSelection sel;
    sel.SetChecked(conversation_single, kMfAlice, true);
    sel.SetChecked(conversation_group, kMfGroup2, true);
    int skipped = 0;
    CHECK(balloonnotifymode::BuildApplyEntries(sel, conversation_single, notify_mode_mention_follow, modes, skipped)
              .empty());
    CHECK(skipped == 1);
    CHECK(balloonnotifymode::BuildApplyEntries(sel, conversation_group, notify_mode_mention_follow, modes, skipped)
              .size() == 1);
}

TEST_CASE("MF-C5 成员菜单：对他人有「关注 TA 在本群的发言」并随状态取反，对本人与已退群的人没有", "[groupfollow]")
{
    using namespace balloonchatwnd;
    std::vector<MentionMenuEntry> notFollowed =
        BuildMentionMenuModel(kMfAlice, kMfSelf, true, kMfNetMember, kMfNetMember, false, false);
    const int idx = IndexOfMention(notFollowed, kMentionMenuGroupFollow);
    REQUIRE(idx >= 0);
    CHECK(std::wstring(notFollowed[(size_t)idx].label) == L"关注 TA 在本群的发言");
    CHECK(notFollowed[(size_t)idx].enabled);
    //紧跟在特别关注那一项之后
    CHECK(IndexOfMention(notFollowed, kMentionMenuStarred) == idx - 1);
    CHECK(std::wstring(notFollowed[(size_t)idx - 1].label) == L"设为特别关注联系人");

    std::vector<MentionMenuEntry> followed =
        BuildMentionMenuModel(kMfAlice, kMfSelf, true, kMfNetMember, kMfNetMember, true, true);
    const int idx2 = IndexOfMention(followed, kMentionMenuGroupFollow);
    REQUIRE(idx2 >= 0);
    CHECK(std::wstring(followed[(size_t)idx2].label) == L"取消关注 TA 在本群的发言");
    CHECK(std::wstring(followed[(size_t)idx2 - 1].label) == L"取消特别关注联系人");

    //本人：只有「查看资料」
    CHECK(IndexOfMention(BuildMentionMenuModel(kMfSelf, kMfSelf, true, kMfNetMember, kMfNetMember, false, false),
                         kMentionMenuGroupFollow) < 0);
    //已退群：没有这一项（本群的关注名单只能填本群成员）
    CHECK(IndexOfMention(BuildMentionMenuModel(kMfAlice, kMfSelf, false, kMfNetMember, kMfNetMember, false, false),
                         kMentionMenuGroupFollow) < 0);

    //主面板联系人菜单的叫法不变
    CHECK(std::wstring(balloonmain::GetStarredMenuLabel(false)) == L"特别关注");
}
