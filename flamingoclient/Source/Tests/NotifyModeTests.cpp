/**
 *  会话消息提醒模式（客户端侧）单元测试，NotifyModeTests.cpp，标签 [notifymode]
 *
 *  会话的「消息免打扰」开关在 2026-10-03 扩展成五种互斥的提醒模式（允许提醒 / 接收不提醒 /
 *  收进消息盒子 / 屏蔽 / 特别关注），见 docs/消息提醒模式与消息盒子方案.md。本文件按方案的
 *  分期组织用例：
 *    - NMC：协议层的解析与构造（第 2a 步，net/ConversationsProtocolClient）；
 *    - NMD：模式表 NotifyModeModel（第 2b 步）；
 *    - NMPL：收到消息时的判定函数 NotifyModePolicy（第 2b 步）；
 *    - NMU：右键菜单「消息提醒设置」二级菜单与资料窗「消息提醒」下拉框的选项 NotifyModeMenu（第 3a 步）；
 *    - NMB：会话列表里「消息盒子」一行的汇总、行序与挪行定位 MsgBoxModel（第 4a 步）；
 *    - NMG：批量管理窗口的候选、搜索过滤、勾选与提交条目 NotifyManageModel（第 5 步）。
 *
 *  典型用法：FlamingoUnit.exe "[notifymode]"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"   // enum conversation_notify_mode / conversation_type
#include "../UI/BalloonMain/NotifyModeModel.h"
#include "../UI/BalloonMain/NotifyModePolicy.h"
#include "../UI/BalloonMain/FolderInviteNotifyPolicy.h"   // ShouldNotifyFolderInvite（NMPL10）
#include "../UI/BalloonMain/NotifyModeMenu.h"
#include "../UI/BalloonMain/MsgBoxModel.h"
#include "../UI/BalloonNotifyMode/NotifyManageModel.h"
#include "../UI/BalloonMain/ConversationListOps.h"   // SortConversations：NMB 组按真实排序规则排好会话
#include "../UI/BalloonMain/MainPanelConversationMenu.h"   // NMU8：三个右键菜单的主菜单 id
#include "../UI/BalloonMain/MainPanelGroupMenu.h"
#include "../UI/BalloonMain/MainPanelContactMenu.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

#include <string>
#include <vector>

using namespace net;

namespace {

//NMC 组用到的会话：一个单聊对端、一个群。群 id 取超过 double 精确范围的值，顺带核对字符串编码。
const int64_t kNmcPeerId = 100007;
const int64_t kNmcGroupId = 9007199254740993LL;

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

//造 count 条批量条目：都是群、模式都是收进消息盒子，cid 从 kNmcPeerId 起逐个加一
std::vector<NotifyModeEntry> MakeEntries(size_t count)
{
    std::vector<NotifyModeEntry> items;
    for (size_t i = 0; i < count; ++i)
    {
        NotifyModeEntry e;
        e.m_ctype = conversation_group;
        e.m_cid   = kNmcPeerId + (int64_t)i;
        e.m_mode  = notify_mode_msgbox;
        items.push_back(e);
    }
    return items;
}

} // anonymous namespace

// =====================================================================
// NMC —— 协议层
// =====================================================================

TEST_CASE("NMC1 解析 1501 会话的 notifymode：0~4 原样读出，缺失、类型不对、越界按允许提醒", "[notifymode]")
{
    const std::string body =
        "{\"code\":0,\"synctime\":0,\"conversations\":["
        "{\"ctype\":2,\"cid\":\"9001\",\"unread\":0,\"notifymode\":3,\"lastmsgid\":\"1\"},"
        "{\"ctype\":1,\"cid\":\"9002\",\"unread\":0,\"notifymode\":4,\"lastmsgid\":\"2\"},"
        "{\"ctype\":1,\"cid\":\"9003\",\"unread\":0,\"lastmsgid\":\"3\"},"
        "{\"ctype\":1,\"cid\":\"9004\",\"unread\":0,\"notifymode\":true,\"lastmsgid\":\"4\"},"
        "{\"ctype\":1,\"cid\":\"9005\",\"unread\":0,\"notifymode\":9,\"lastmsgid\":\"5\"},"
        "{\"ctype\":1,\"cid\":\"9006\",\"unread\":0,\"notifymode\":-1,\"lastmsgid\":\"6\"}"
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 6);
    CHECK(out.m_items[0].m_notifyMode == notify_mode_block);
    CHECK(out.m_items[1].m_notifyMode == notify_mode_starred);
    CHECK(out.m_items[2].m_notifyMode == notify_mode_allow);
    CHECK(out.m_items[3].m_notifyMode == notify_mode_allow);
    CHECK(out.m_items[4].m_notifyMode == notify_mode_allow);
    CHECK(out.m_items[5].m_notifyMode == notify_mode_allow);
}

TEST_CASE("NMC2 解析 1501 顶层 notifymodes：读成列表，坏条目逐条跳过", "[notifymode]")
{
    const std::string body =
        "{\"code\":0,\"synctime\":0,\"conversations\":[],"
        "\"notifymodes\":["
        "{\"ctype\":2,\"cid\":\"9007199254740993\",\"mode\":3},"   // 合法：大 cid 不丢精度
        "{\"ctype\":1,\"cid\":\"100007\",\"mode\":4},"             // 合法：还没聊过的联系人
        "{\"ctype\":1,\"cid\":\"0\",\"mode\":1},"                  // cid 非正
        "{\"ctype\":3,\"cid\":\"5\",\"mode\":1},"                  // ctype 不是单聊或群聊
        "{\"ctype\":1,\"cid\":\"6\",\"mode\":7},"                  // mode 越界
        "{\"ctype\":1,\"cid\":\"7\"},"                             // 缺 mode
        "\"oops\""                                                 // 不是对象
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_notifyModes.size() == 2);
    CHECK(out.m_notifyModes[0].m_ctype == conversation_group);
    CHECK(out.m_notifyModes[0].m_cid == kNmcGroupId);
    CHECK(out.m_notifyModes[0].m_mode == notify_mode_block);
    CHECK(out.m_notifyModes[1].m_ctype == conversation_single);
    CHECK(out.m_notifyModes[1].m_cid == kNmcPeerId);
    CHECK(out.m_notifyModes[1].m_mode == notify_mode_starred);

    //老服务端不带 notifymodes：为空，不算解析失败
    ConversationsResult noModes;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(
        "{\"code\":0,\"synctime\":0,\"conversations\":[]}", noModes));
    CHECK(noModes.m_notifyModes.empty());
}

TEST_CASE("NMC3 构造 1513 请求：只在要改时写整数 notifymode，永远不写 mute", "[notifymode]")
{
    Json::Value root;
    REQUIRE(ParseJson(ConversationsProtocolClient::buildSetConversationRequest(
        conversation_group, kNmcGroupId, false, false, true, notify_mode_block), root));
    REQUIRE(root["notifymode"].isInt());
    CHECK(root["notifymode"].asInt() == notify_mode_block);
    CHECK(root["cid"].asString() == "9007199254740993");
    CHECK_FALSE(root.isMember("mute"));

    Json::Value pinOnly;
    REQUIRE(ParseJson(ConversationsProtocolClient::buildSetConversationRequest(
        conversation_single, kNmcPeerId, true, true, false, notify_mode_starred), pinOnly));
    CHECK_FALSE(pinOnly.isMember("notifymode"));
    CHECK_FALSE(pinOnly.isMember("mute"));
}

TEST_CASE("NMC4 解析 1514 推送的 notifymode", "[notifymode]")
{
    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":1,\"cid\":\"100007\",\"pinned\":false,\"notifymode\":2,\"deleted\":false}", n));
    CHECK(n.m_notifyMode == notify_mode_msgbox);

    //越界按允许提醒处理
    ConversationChangedNotify bad;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":1,\"cid\":\"100007\",\"notifymode\":8}", bad));
    CHECK(bad.m_notifyMode == notify_mode_allow);
}

TEST_CASE("NMC5 构造 1524 请求与按 200 条拆批", "[notifymode]")
{
    std::vector<NotifyModeEntry> two;
    NotifyModeEntry group;
    group.m_ctype = conversation_group;
    group.m_cid   = kNmcGroupId;
    group.m_mode  = notify_mode_msgbox;
    two.push_back(group);
    NotifyModeEntry peer;
    peer.m_ctype = conversation_single;
    peer.m_cid   = kNmcPeerId;
    peer.m_mode  = notify_mode_allow;
    two.push_back(peer);

    Json::Value root;
    REQUIRE(ParseJson(ConversationsProtocolClient::buildSetNotifyModesRequest(two), root));
    REQUIRE(root["items"].size() == 2);
    CHECK(root["items"][0]["ctype"].asInt() == conversation_group);
    REQUIRE(root["items"][0]["cid"].isString());
    CHECK(root["items"][0]["cid"].asString() == "9007199254740993");
    CHECK(root["items"][0]["mode"].asInt() == notify_mode_msgbox);
    CHECK(root["items"][1]["cid"].asString() == "100007");
    CHECK(root["items"][1]["mode"].asInt() == notify_mode_allow);

    //450 条拆成 200、200、50，顺序不变
    const size_t kTotal = 450;
    const size_t kLastBatch = 50;
    std::vector<std::vector<NotifyModeEntry> > batches;
    ConversationsProtocolClient::splitNotifyModeBatches(MakeEntries(kTotal), kNotifyModeBatchMax, batches);
    REQUIRE(batches.size() == 3);
    CHECK(batches[0].size() == kNotifyModeBatchMax);
    CHECK(batches[1].size() == kNotifyModeBatchMax);
    CHECK(batches[2].size() == kLastBatch);
    CHECK(batches[0][0].m_cid == kNmcPeerId);
    CHECK(batches[1][0].m_cid == kNmcPeerId + (int64_t)kNotifyModeBatchMax);
    CHECK(batches[2].back().m_cid == kNmcPeerId + (int64_t)(kTotal - 1));

    //正好一批、以及一条都没有
    ConversationsProtocolClient::splitNotifyModeBatches(MakeEntries(kNotifyModeBatchMax), kNotifyModeBatchMax,
                                                        batches);
    CHECK(batches.size() == 1);
    ConversationsProtocolClient::splitNotifyModeBatches(std::vector<NotifyModeEntry>(), kNotifyModeBatchMax,
                                                        batches);
    CHECK(batches.empty());
}

TEST_CASE("NMC6 解析 1524 应答与 1525 推送", "[notifymode]")
{
    SetNotifyModesResult result;
    REQUIRE(ConversationsProtocolClient::parseSetNotifyModesResponse(
        "{\"code\":0,\"msg\":\"ok\",\"results\":["
        "{\"ctype\":2,\"cid\":\"9007199254740993\",\"code\":0},"
        "{\"ctype\":1,\"cid\":\"100007\",\"code\":210}]}", result));
    CHECK(result.m_code == 0);
    REQUIRE(result.m_results.size() == 2);
    CHECK(result.m_results[0].m_cid == kNmcGroupId);
    CHECK(result.m_results[0].m_code == 0);
    CHECK(result.m_results[1].m_ctype == conversation_single);
    CHECK(result.m_results[1].m_cid == kNmcPeerId);
    CHECK(result.m_results[1].m_code == error_code_starred_limit);

    //整单失败
    SetNotifyModesResult fail;
    REQUIRE(ConversationsProtocolClient::parseSetNotifyModesResponse(
        "{\"code\":3,\"msg\":\"bad request\",\"results\":[]}", fail));
    CHECK(fail.m_code == error_code_bad_request);
    CHECK(fail.m_results.empty());
    CHECK_FALSE(ConversationsProtocolClient::parseSetNotifyModesResponse("{\"msg\":\"x\"}", fail));

    NotifyModesChangedNotify notify;
    REQUIRE(ConversationsProtocolClient::parseNotifyModesChangedNotify(
        "{\"items\":[{\"ctype\":2,\"cid\":\"9007199254740993\",\"mode\":2},"
        "{\"ctype\":1,\"cid\":\"0\",\"mode\":1}]}", notify));
    REQUIRE(notify.m_items.size() == 1);
    CHECK(notify.m_items[0].m_cid == kNmcGroupId);
    CHECK(notify.m_items[0].m_mode == notify_mode_msgbox);

    CHECK_FALSE(ConversationsProtocolClient::parseNotifyModesChangedNotify("{}", notify));
    CHECK_FALSE(ConversationsProtocolClient::parseNotifyModesChangedNotify("not json", notify));
}

// =====================================================================
// NMD —— 模式表 NotifyModeModel
// =====================================================================

namespace {

//NMD / NMPL 组用到的会话
const int64_t kNmdPeerA = 200001;
const int64_t kNmdPeerB = 200002;
const int64_t kNmdGroupA = 268435501LL;

//NMD3 灌满特别关注时用的联系人 id 起点：kNmdStarredBase + 0 ~ 49
const int64_t kNmdStarredBase = 300000;

NotifyModeEntry MakeEntry(int ctype, int64_t cid, int mode)
{
    NotifyModeEntry e;
    e.m_ctype = ctype;
    e.m_cid   = cid;
    e.m_mode  = mode;
    return e;
}

} // anonymous namespace

TEST_CASE("NMD1 用 1501 的列表初始化：能查出模式，查不到为 0，0 与非法项不存", "[notifymode]")
{
    std::vector<NotifyModeEntry> entries;
    entries.push_back(MakeEntry(conversation_group, kNmdGroupA, notify_mode_block));
    entries.push_back(MakeEntry(conversation_single, kNmdPeerA, notify_mode_starred));
    entries.push_back(MakeEntry(conversation_single, kNmdPeerB, notify_mode_allow));   // 0 不存
    entries.push_back(MakeEntry(conversation_service, kNmdPeerB, notify_mode_silent)); // ctype 非法
    entries.push_back(MakeEntry(conversation_single, 0, notify_mode_silent));          // cid 非法

    balloonmain::NotifyModeModel model;
    model.Reset(entries);
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_block);
    CHECK(model.GetMode(conversation_single, kNmdPeerA) == notify_mode_starred);
    CHECK(model.GetMode(conversation_single, kNmdPeerB) == notify_mode_allow);
    //同一个 id 换个会话类型就是另一个会话
    CHECK(model.GetMode(conversation_group, kNmdPeerA) == notify_mode_allow);

    std::vector<NotifyModeEntry> exported;
    model.ExportEntries(exported);
    CHECK(exported.size() == 2);

    //再次 Reset 会先清空
    model.Reset(std::vector<NotifyModeEntry>());
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_allow);
}

TEST_CASE("NMD2 修改单个会话：返回是否真的变了，改成 0 即删除，非法参数不改", "[notifymode]")
{
    balloonmain::NotifyModeModel model;
    CHECK(model.SetMode(conversation_group, kNmdGroupA, notify_mode_msgbox));
    CHECK_FALSE(model.SetMode(conversation_group, kNmdGroupA, notify_mode_msgbox));
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_msgbox);

    CHECK(model.SetMode(conversation_group, kNmdGroupA, notify_mode_allow));
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_allow);
    std::vector<NotifyModeEntry> exported;
    model.ExportEntries(exported);
    CHECK(exported.empty());

    //非法参数：模式越界、ctype 非法、cid 非正
    CHECK_FALSE(model.SetMode(conversation_group, kNmdGroupA, notify_mode_mention_follow + 1));
    CHECK_FALSE(model.SetMode(conversation_service, kNmdGroupA, notify_mode_silent));
    CHECK_FALSE(model.SetMode(conversation_single, 0, notify_mode_silent));
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_allow);
}

TEST_CASE("NMD3 特别关注计数单聊群聊合计，满 50 个不能再加新的，原本就是的不受影响", "[notifymode]")
{
    balloonmain::NotifyModeModel model;
    model.SetMode(conversation_group, kNmdGroupA, notify_mode_starred);
    for (int i = 0; i < balloonmain::kNotifyModeStarredMax - 1; ++i)
    {
        model.SetMode(conversation_single, kNmdStarredBase + i, notify_mode_starred);
    }
    model.SetMode(conversation_single, kNmdPeerB, notify_mode_silent);

    CHECK(model.CountMode(notify_mode_starred) == balloonmain::kNotifyModeStarredMax);
    CHECK(model.CountMode(notify_mode_silent) == 1);
    CHECK(model.CountMode(notify_mode_allow) == 0);

    CHECK_FALSE(model.CanSetStarred(conversation_single, kNmdPeerA));
    CHECK(model.CanSetStarred(conversation_group, kNmdGroupA));
    CHECK(model.CanSetStarred(conversation_single, kNmdStarredBase));

    //空出一个名额就能加
    model.SetMode(conversation_single, kNmdStarredBase, notify_mode_allow);
    CHECK(model.CanSetStarred(conversation_single, kNmdPeerA));
}

TEST_CASE("NMD4 应用 1525 推送：逐条覆盖，返回是否有变化", "[notifymode]")
{
    balloonmain::NotifyModeModel model;
    model.SetMode(conversation_group, kNmdGroupA, notify_mode_silent);

    std::vector<NotifyModeEntry> pushed;
    pushed.push_back(MakeEntry(conversation_group, kNmdGroupA, notify_mode_msgbox));
    pushed.push_back(MakeEntry(conversation_single, kNmdPeerA, notify_mode_starred));
    CHECK(model.ApplyEntries(pushed));
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_msgbox);
    CHECK(model.GetMode(conversation_single, kNmdPeerA) == notify_mode_starred);

    //同样的推送再来一次：没有变化
    CHECK_FALSE(model.ApplyEntries(pushed));
}

TEST_CASE("NMD5 登出时清空", "[notifymode]")
{
    balloonmain::NotifyModeModel model;
    model.SetMode(conversation_group, kNmdGroupA, notify_mode_block);
    model.SetMode(conversation_single, kNmdPeerA, notify_mode_starred);
    model.Clear();
    CHECK(model.GetMode(conversation_group, kNmdGroupA) == notify_mode_allow);
    CHECK(model.GetMode(conversation_single, kNmdPeerA) == notify_mode_allow);
    CHECK(model.CountMode(notify_mode_starred) == 0);
}

// =====================================================================
// NMPL —— 收到消息时的判定 NotifyModePolicy
// =====================================================================

namespace {

//造一条「别人发来、窗口没开、没在看」的普通消息的判定输入
balloonmain::ArrivalInput MakeArrival(int mode, bool isGroup)
{
    balloonmain::ArrivalInput in;
    in.m_mode       = mode;
    in.m_isGroup    = isGroup;
    in.m_atMe       = false;
    in.m_countable  = true;
    in.m_windowOpen = false;
    in.m_watching   = false;
    in.m_isShake    = false;
    return in;
}

} // anonymous namespace

TEST_CASE("NMPL1 允许提醒、窗口没开：计未读、更新会话行、闪托盘、响提示音", "[notifymode]")
{
    const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(MakeArrival(notify_mode_allow, false));
    CHECK(d.m_updateRow);
    CHECK(d.m_countUnread);
    CHECK(d.m_flashTray);
    CHECK(d.m_playSound);
    CHECK_FALSE(d.m_popupWindow);
    CHECK_FALSE(d.m_flashTaskbar);

    //窗口开着但没在看：照常计未读，不再另外提醒
    balloonmain::ArrivalInput open = MakeArrival(notify_mode_allow, false);
    open.m_windowOpen = true;
    const balloonmain::ArrivalDecision d2 = balloonmain::DecideArrival(open);
    CHECK(d2.m_countUnread);
    CHECK_FALSE(d2.m_flashTray);
    CHECK_FALSE(d2.m_playSound);
}

TEST_CASE("NMPL2 接收不提醒：计未读不闪不响，群里被 @ 时照常提醒", "[notifymode]")
{
    const balloonmain::ArrivalDecision single = balloonmain::DecideArrival(MakeArrival(notify_mode_silent, false));
    CHECK(single.m_updateRow);
    CHECK(single.m_countUnread);
    CHECK_FALSE(single.m_flashTray);
    CHECK_FALSE(single.m_playSound);

    balloonmain::ArrivalInput at = MakeArrival(notify_mode_silent, true);
    at.m_atMe = true;
    const balloonmain::ArrivalDecision atMe = balloonmain::DecideArrival(at);
    CHECK(atMe.m_countUnread);
    CHECK(atMe.m_flashTray);
    CHECK(atMe.m_playSound);

    CHECK(balloonmain::IsQuietBadgeMode(notify_mode_silent));
}

TEST_CASE("NMPL3 收进消息盒子：计未读、灰色角标，被 @ 也不闪不响", "[notifymode]")
{
    balloonmain::ArrivalInput at = MakeArrival(notify_mode_msgbox, true);
    at.m_atMe = true;
    const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(at);
    CHECK(d.m_updateRow);
    CHECK(d.m_countUnread);
    CHECK_FALSE(d.m_flashTray);
    CHECK_FALSE(d.m_playSound);
    CHECK_FALSE(d.m_popupWindow);
    CHECK(balloonmain::IsQuietBadgeMode(notify_mode_msgbox));
}

TEST_CASE("NMPL4 屏蔽：不计未读、不动会话行、不提醒，被 @ 也一样", "[notifymode]")
{
    balloonmain::ArrivalInput at = MakeArrival(notify_mode_block, true);
    at.m_atMe = true;
    const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(at);
    CHECK_FALSE(d.m_updateRow);
    CHECK_FALSE(d.m_countUnread);
    CHECK_FALSE(d.m_flashTray);
    CHECK_FALSE(d.m_playSound);
    CHECK_FALSE(d.m_popupWindow);
    CHECK_FALSE(balloonmain::ShowsUnreadBadge(notify_mode_block));
    CHECK(balloonmain::ShowsUnreadBadge(notify_mode_silent));
}

TEST_CASE("NMPL5 特别关注、窗口没开：弹出聊天窗并响提示音，不闪托盘", "[notifymode]")
{
    const balloonmain::ArrivalDecision single = balloonmain::DecideArrival(MakeArrival(notify_mode_starred, false));
    CHECK(single.m_popupWindow);
    CHECK(single.m_playSound);
    CHECK(single.m_countUnread);
    CHECK(single.m_updateRow);
    CHECK_FALSE(single.m_flashTray);
    CHECK_FALSE(single.m_flashTaskbar);

    //群也一样
    const balloonmain::ArrivalDecision group = balloonmain::DecideArrival(MakeArrival(notify_mode_starred, true));
    CHECK(group.m_popupWindow);
    CHECK(group.m_playSound);
}

TEST_CASE("NMPL6 特别关注、窗口开着但没在看：闪任务栏、响提示音，不提到前台", "[notifymode]")
{
    balloonmain::ArrivalInput in = MakeArrival(notify_mode_starred, true);
    in.m_windowOpen = true;
    const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(in);
    CHECK(d.m_flashTaskbar);
    CHECK(d.m_playSound);
    CHECK(d.m_countUnread);
    CHECK_FALSE(d.m_popupWindow);
    CHECK_FALSE(d.m_flashTray);
}

TEST_CASE("NMPL7 单聊窗口抖动：0、1、2、4 四种模式下都照常弹窗，不闪托盘", "[notifymode]")
{
    const int modes[] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox, notify_mode_starred };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        balloonmain::ArrivalInput in = MakeArrival(modes[i], false);
        in.m_isShake = true;
        const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(in);
        INFO("mode = " << modes[i]);
        CHECK(d.m_popupWindow);
        CHECK(d.m_updateRow);
        CHECK_FALSE(d.m_flashTray);
        CHECK_FALSE(d.m_playSound);
    }
}

TEST_CASE("NMPL8 自己同步回来的消息与群系统消息：不计未读、不提醒、不弹窗，只有屏蔽连会话行也不动", "[notifymode]")
{
    const int modes[] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox, notify_mode_starred };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        balloonmain::ArrivalInput in = MakeArrival(modes[i], true);
        in.m_countable = false;
        const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(in);
        INFO("mode = " << modes[i]);
        CHECK(d.m_updateRow);
        CHECK_FALSE(d.m_countUnread);
        CHECK_FALSE(d.m_flashTray);
        CHECK_FALSE(d.m_playSound);
        CHECK_FALSE(d.m_popupWindow);
    }

    balloonmain::ArrivalInput block = MakeArrival(notify_mode_block, true);
    block.m_countable = false;
    CHECK_FALSE(balloonmain::DecideArrival(block).m_updateRow);
}

TEST_CASE("NMPL9 正看着聊天窗：任何模式都不计未读、不提醒", "[notifymode]")
{
    const int modes[] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox, notify_mode_starred };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        balloonmain::ArrivalInput in = MakeArrival(modes[i], true);
        in.m_atMe       = true;
        in.m_windowOpen = true;
        in.m_watching   = true;
        const balloonmain::ArrivalDecision d = balloonmain::DecideArrival(in);
        INFO("mode = " << modes[i]);
        CHECK(d.m_updateRow);
        CHECK_FALSE(d.m_countUnread);
        CHECK_FALSE(d.m_flashTray);
        CHECK_FALSE(d.m_flashTaskbar);
        CHECK_FALSE(d.m_playSound);
        CHECK_FALSE(d.m_popupWindow);
    }
}

TEST_CASE("NMPL10 文件夹传输邀请：接收不提醒、收进消息盒子不提醒，特别关注改为弹窗", "[notifymode]")
{
    CHECK(balloonmain::ShouldNotifyFolderInvite(false, false, notify_mode_allow));
    CHECK_FALSE(balloonmain::ShouldNotifyFolderInvite(false, false, notify_mode_silent));
    CHECK_FALSE(balloonmain::ShouldNotifyFolderInvite(false, false, notify_mode_msgbox));
    //特别关注不闪托盘，由 DecideArrival 给出弹窗
    CHECK_FALSE(balloonmain::ShouldNotifyFolderInvite(false, false, notify_mode_starred));
    CHECK(balloonmain::DecideArrival(MakeArrival(notify_mode_starred, false)).m_popupWindow);
}

TEST_CASE("NMPL11 「消息」页签红色总数只算允许提醒与特别关注", "[notifymode]")
{
    const int kUnreadAllow = 3;
    const int kUnreadSilent = 5;
    const int kUnreadMsgbox = 7;
    const int kUnreadStarred = 11;
    const int kUnreadBlock = 13;

    std::vector<ConversationItem> items;
    const int64_t cids[] = { 1001, 1002, 1003, 1004, 1005 };
    const int unreads[] = { kUnreadAllow, kUnreadSilent, kUnreadMsgbox, kUnreadStarred, kUnreadBlock };
    for (size_t i = 0; i < sizeof(cids) / sizeof(cids[0]); ++i)
    {
        ConversationItem c;
        c.m_ctype  = conversation_group;
        c.m_cid    = cids[i];
        c.m_unread = unreads[i];
        items.push_back(c);
    }
    //脏数据：负的未读数按 0 处理
    ConversationItem dirty;
    dirty.m_ctype  = conversation_single;
    dirty.m_cid    = kNmdPeerA;
    dirty.m_unread = -1;
    items.push_back(dirty);

    balloonmain::NotifyModeModel model;
    model.SetMode(conversation_group, cids[1], notify_mode_silent);
    model.SetMode(conversation_group, cids[2], notify_mode_msgbox);
    model.SetMode(conversation_group, cids[3], notify_mode_starred);
    model.SetMode(conversation_group, cids[4], notify_mode_block);

    CHECK(balloonmain::ComputeAlertingUnread(items, model) == kUnreadAllow + kUnreadStarred);
}

TEST_CASE("NMPL12 改成三种不提醒的模式时清掉托盘闪烁，改成允许提醒或特别关注时不清", "[notifymode]")
{
    CHECK(balloonmain::ShouldClearTrayOnModeChange(notify_mode_silent));
    CHECK(balloonmain::ShouldClearTrayOnModeChange(notify_mode_msgbox));
    CHECK(balloonmain::ShouldClearTrayOnModeChange(notify_mode_block));
    CHECK_FALSE(balloonmain::ShouldClearTrayOnModeChange(notify_mode_allow));
    CHECK_FALSE(balloonmain::ShouldClearTrayOnModeChange(notify_mode_starred));
}

TEST_CASE("NMPL13 只有从屏蔽改成别的模式才算解除屏蔽", "[notifymode]")
{
    CHECK(balloonmain::IsLeavingBlock(notify_mode_block, notify_mode_allow));
    CHECK(balloonmain::IsLeavingBlock(notify_mode_block, notify_mode_silent));
    CHECK(balloonmain::IsLeavingBlock(notify_mode_block, notify_mode_msgbox));
    CHECK(balloonmain::IsLeavingBlock(notify_mode_block, notify_mode_starred));
    CHECK_FALSE(balloonmain::IsLeavingBlock(notify_mode_block, notify_mode_block));
    CHECK_FALSE(balloonmain::IsLeavingBlock(notify_mode_allow, notify_mode_block));
    CHECK_FALSE(balloonmain::IsLeavingBlock(notify_mode_silent, notify_mode_allow));
    CHECK_FALSE(balloonmain::IsLeavingBlock(notify_mode_allow, notify_mode_allow));
}

// =====================================================================
// NMU：「消息提醒设置」二级菜单与资料窗下拉框的选项（NotifyModeMenu）
// =====================================================================

namespace {

//二级菜单里模式项的个数：群聊六项（2026-10-06 加了「只提醒 @我和关注的人」），单聊没有这一项与屏蔽，四项
const size_t kNmuGroupModeCount = 6;
const size_t kNmuSingleModeCount = 4;

//二级菜单模式项之后的行数：单聊为分隔条 +「批量管理消息提醒」；群聊再多一行「管理关注的成员…」
const size_t kNmuTrailingRows = 2;
const size_t kNmuGroupTrailingRows = 3;

//在行模型里数打勾的项
int CountChecked(const std::vector<balloonmain::NotifyModeMenuEntry>& rows)
{
    int n = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_checked)
            ++n;
    }
    return n;
}

//取打勾那一项的命令 id；没有打勾的项时返回 0
UINT CheckedId(const std::vector<balloonmain::NotifyModeMenuEntry>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_checked)
            return rows[i].m_id;
    }
    return 0;
}

} // anonymous namespace

TEST_CASE("NMU1 群聊的二级菜单：六种模式、分隔条、批量管理、管理关注的成员，顺序与文字固定", "[notifymode][notifymenu]")
{
    const std::vector<balloonmain::NotifyModeMenuEntry> rows =
        balloonmain::BuildNotifyModeSubMenu(true, notify_mode_allow);
    REQUIRE(rows.size() == kNmuGroupModeCount + kNmuGroupTrailingRows);

    CHECK(rows[0].m_id == balloonmain::kNotifyMenuStarred);
    CHECK(std::wstring(rows[0].m_label) == L"特别关注");
    CHECK(rows[1].m_id == balloonmain::kNotifyMenuAllow);
    CHECK(std::wstring(rows[1].m_label) == L"允许消息提醒");
    CHECK(rows[2].m_id == balloonmain::kNotifyMenuMentionFollow);
    CHECK(std::wstring(rows[2].m_label) == L"只提醒 @我和关注的人");
    CHECK(rows[3].m_id == balloonmain::kNotifyMenuSilent);
    CHECK(std::wstring(rows[3].m_label) == L"接收消息但不提醒");
    CHECK(rows[4].m_id == balloonmain::kNotifyMenuMsgbox);
    CHECK(std::wstring(rows[4].m_label) == L"收进消息盒子且不提醒");
    CHECK(rows[5].m_id == balloonmain::kNotifyMenuBlock);
    CHECK(std::wstring(rows[5].m_label) == L"屏蔽消息");
    for (size_t i = 0; i < kNmuGroupModeCount; ++i)
    {
        CHECK(rows[i].m_checkable);
        CHECK_FALSE(rows[i].m_isSeparator);
    }

    CHECK(rows[6].m_isSeparator);
    CHECK(rows[7].m_id == balloonmain::kNotifyMenuBatch);
    CHECK(std::wstring(rows[7].m_label) == L"批量管理消息提醒");
    CHECK_FALSE(rows[7].m_checkable);
    CHECK_FALSE(rows[7].m_checked);
    CHECK(rows[8].m_id == balloonmain::kNotifyMenuManageFollow);
    CHECK(std::wstring(rows[8].m_label) == L"管理关注的成员…");
    CHECK_FALSE(rows[8].m_checkable);
}

TEST_CASE("NMU2 单聊的二级菜单没有「屏蔽消息」「只提醒 @我和关注的人」与「管理关注的成员…」", "[notifymode][notifymenu]")
{
    const std::vector<balloonmain::NotifyModeMenuEntry> rows =
        balloonmain::BuildNotifyModeSubMenu(false, notify_mode_allow);
    REQUIRE(rows.size() == kNmuSingleModeCount + kNmuTrailingRows);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CHECK(rows[i].m_id != balloonmain::kNotifyMenuBlock);
        CHECK(rows[i].m_id != balloonmain::kNotifyMenuMentionFollow);
        CHECK(rows[i].m_id != balloonmain::kNotifyMenuManageFollow);
    }
    CHECK(rows[kNmuSingleModeCount].m_isSeparator);
    CHECK(rows[kNmuSingleModeCount + 1].m_id == balloonmain::kNotifyMenuBatch);
}

TEST_CASE("NMU3 当前模式那一项打勾，且只有这一项打勾", "[notifymode][notifymenu]")
{
    const int groupModes[] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox,
                               notify_mode_block, notify_mode_starred, notify_mode_mention_follow };
    const UINT groupIds[] = { balloonmain::kNotifyMenuAllow, balloonmain::kNotifyMenuSilent,
                              balloonmain::kNotifyMenuMsgbox, balloonmain::kNotifyMenuBlock,
                              balloonmain::kNotifyMenuStarred, balloonmain::kNotifyMenuMentionFollow };
    for (size_t i = 0; i < kNmuGroupModeCount; ++i)
    {
        const std::vector<balloonmain::NotifyModeMenuEntry> rows =
            balloonmain::BuildNotifyModeSubMenu(true, groupModes[i]);
        CHECK(CountChecked(rows) == 1);
        CHECK(CheckedId(rows) == groupIds[i]);
    }

    //单聊的四种模式同理
    const std::vector<balloonmain::NotifyModeMenuEntry> single =
        balloonmain::BuildNotifyModeSubMenu(false, notify_mode_msgbox);
    CHECK(CountChecked(single) == 1);
    CHECK(CheckedId(single) == balloonmain::kNotifyMenuMsgbox);
}

TEST_CASE("NMU4 不认识的模式、单聊上的屏蔽：按允许提醒打勾", "[notifymode][notifymenu]")
{
    const int kUnknownMode = 99;
    CHECK(CheckedId(balloonmain::BuildNotifyModeSubMenu(true, kUnknownMode)) == balloonmain::kNotifyMenuAllow);
    CHECK(CheckedId(balloonmain::BuildNotifyModeSubMenu(true, -1)) == balloonmain::kNotifyMenuAllow);
    CHECK(CheckedId(balloonmain::BuildNotifyModeSubMenu(false, notify_mode_block)) == balloonmain::kNotifyMenuAllow);
    //单聊上的「只提醒 @我和关注的人」同理
    CHECK(CheckedId(balloonmain::BuildNotifyModeSubMenu(false, notify_mode_mention_follow))
          == balloonmain::kNotifyMenuAllow);
}

TEST_CASE("NMU5 命令 id 与提醒模式互换；批量管理与其它 id 不是模式项", "[notifymode][notifymenu]")
{
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuStarred) == notify_mode_starred);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuAllow) == notify_mode_allow);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuSilent) == notify_mode_silent);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuMsgbox) == notify_mode_msgbox);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuBlock) == notify_mode_block);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuMentionFollow) == notify_mode_mention_follow);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuBatch) == -1);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kNotifyMenuManageFollow) == -1);
    CHECK(balloonmain::NotifyModeFromMenuId(0) == -1);
    CHECK(balloonmain::NotifyModeFromMenuId(balloonmain::kConvMenuPin) == -1);

    //菜单里每个模式项点下去得到的模式，再建一次菜单时正是打勾的那一项
    const std::vector<balloonmain::NotifyModeMenuEntry> rows =
        balloonmain::BuildNotifyModeSubMenu(true, notify_mode_allow);
    for (size_t i = 0; i < kNmuGroupModeCount; ++i)
    {
        const int mode = balloonmain::NotifyModeFromMenuId(rows[i].m_id);
        CHECK(CheckedId(balloonmain::BuildNotifyModeSubMenu(true, mode)) == rows[i].m_id);
    }
}

TEST_CASE("NMU6 父项文字与群聊窗成员菜单的特别关注快捷项", "[notifymode][notifymenu]")
{
    CHECK(std::wstring(balloonmain::GetNotifyModeMenuTitle()) == L"消息提醒设置");
    CHECK(std::wstring(balloonmain::GetStarredMenuLabel(false)) == L"特别关注");
    CHECK(std::wstring(balloonmain::GetStarredMenuLabel(true)) == L"取消特别关注");
}

TEST_CASE("NMU7 资料窗下拉框的选项下标与提醒模式互换", "[notifymode][notifymenu]")
{
    //选项顺序与二级菜单相同
    const std::vector<int> groupChoices = balloonmain::GetNotifyModeChoices(true);
    const std::vector<int> singleChoices = balloonmain::GetNotifyModeChoices(false);
    REQUIRE(groupChoices.size() == kNmuGroupModeCount);
    REQUIRE(singleChoices.size() == kNmuSingleModeCount);
    const std::vector<balloonmain::NotifyModeMenuEntry> rows =
        balloonmain::BuildNotifyModeSubMenu(true, notify_mode_allow);
    for (size_t i = 0; i < groupChoices.size(); ++i)
    {
        CHECK(std::wstring(balloonmain::GetNotifyModeLabel(groupChoices[i])) == std::wstring(rows[i].m_label));
        CHECK(balloonmain::NotifyModeChoiceIndex(true, groupChoices[i]) == (int)i);
        CHECK(balloonmain::NotifyModeOfChoice(true, (int)i) == groupChoices[i]);
    }
    for (size_t i = 0; i < singleChoices.size(); ++i)
    {
        CHECK(balloonmain::NotifyModeChoiceIndex(false, singleChoices[i]) == (int)i);
        CHECK(balloonmain::NotifyModeOfChoice(false, (int)i) == singleChoices[i]);
    }

    //不认识的模式、单聊上的屏蔽：落到允许提醒那一项
    const int allowGroup = balloonmain::NotifyModeChoiceIndex(true, notify_mode_allow);
    const int allowSingle = balloonmain::NotifyModeChoiceIndex(false, notify_mode_allow);
    CHECK(balloonmain::NotifyModeChoiceIndex(true, 99) == allowGroup);
    CHECK(balloonmain::NotifyModeChoiceIndex(false, notify_mode_block) == allowSingle);

    //越界下标（含下拉框未选中时的 -1）：按允许提醒
    CHECK(balloonmain::NotifyModeOfChoice(true, -1) == notify_mode_allow);
    CHECK(balloonmain::NotifyModeOfChoice(true, (int)kNmuGroupModeCount) == notify_mode_allow);
    CHECK(balloonmain::NotifyModeOfChoice(false, (int)kNmuSingleModeCount) == notify_mode_allow);
}

TEST_CASE("NMU8 三个右键菜单的主菜单 id 都不会与二级菜单的 id 相撞", "[notifymode][notifymenu]")
{
    //每个菜单都按"所有可选项都出现"的状态构造，取全部行
    balloonmain::ConversationMenuState conv;
    conv.m_isGroup = true;
    conv.m_unread = 1;
    std::vector<UINT> ids;
    std::vector<balloonmain::ConversationMenuEntry> convRows = balloonmain::BuildConversationMenuModel(conv);
    for (size_t i = 0; i < convRows.size(); ++i)
        ids.push_back(convRows[i].m_id);
    conv.m_isGroup = false;
    convRows = balloonmain::BuildConversationMenuModel(conv);
    for (size_t i = 0; i < convRows.size(); ++i)
        ids.push_back(convRows[i].m_id);

    balloonmain::GroupMenuState group;
    group.m_canManage = true;
    group.m_isOwner = true;
    const std::vector<balloonmain::GroupMenuEntry> groupRows = balloonmain::BuildGroupMenuModel(group);
    for (size_t i = 0; i < groupRows.size(); ++i)
        ids.push_back(groupRows[i].m_id);

    balloonmain::ContactMenuState contact;
    const std::vector<balloonmain::ContactMenuEntry> contactRows = balloonmain::BuildContactMenuModel(contact);
    for (size_t i = 0; i < contactRows.size(); ++i)
        ids.push_back(contactRows[i].m_id);

    for (size_t i = 0; i < ids.size(); ++i)
    {
        CHECK(ids[i] < (UINT)balloonmain::kNotifyMenuStarred);
    }
}

TEST_CASE("NMU9 三个右键菜单都有「消息提醒设置」父项，且不再有免打扰与特别关注两项", "[notifymode][notifymenu]")
{
    balloonmain::ConversationMenuState conv;
    conv.m_isGroup = false;
    const std::vector<balloonmain::ConversationMenuEntry> convRows = balloonmain::BuildConversationMenuModel(conv);
    int convParents = 0;
    for (size_t i = 0; i < convRows.size(); ++i)
    {
        if (convRows[i].m_hasNotifySubMenu)
        {
            ++convParents;
            CHECK(convRows[i].m_id == balloonmain::kConvMenuNotifyMode);
            CHECK(std::wstring(convRows[i].m_label) == L"消息提醒设置");
        }
        if (!convRows[i].m_isSeparator)
        {
            CHECK(std::wstring(convRows[i].m_label).find(L"免打扰") == std::wstring::npos);
            CHECK(std::wstring(convRows[i].m_label).find(L"特别关注") == std::wstring::npos);
        }
    }
    CHECK(convParents == 1);

    balloonmain::GroupMenuState group;
    const std::vector<balloonmain::GroupMenuEntry> groupRows = balloonmain::BuildGroupMenuModel(group);
    int groupParents = 0;
    for (size_t i = 0; i < groupRows.size(); ++i)
    {
        if (groupRows[i].m_hasNotifySubMenu)
        {
            ++groupParents;
            CHECK(groupRows[i].m_id == balloonmain::kGroupMenuNotifyMode);
            CHECK(std::wstring(groupRows[i].m_label) == L"消息提醒设置");
        }
        if (!groupRows[i].m_isSeparator)
        {
            CHECK(std::wstring(groupRows[i].m_label).find(L"免打扰") == std::wstring::npos);
        }
    }
    CHECK(groupParents == 1);

    balloonmain::ContactMenuState contact;
    const std::vector<balloonmain::ContactMenuEntry> contactRows = balloonmain::BuildContactMenuModel(contact);
    int contactParents = 0;
    for (size_t i = 0; i < contactRows.size(); ++i)
    {
        if (contactRows[i].m_hasNotifySubMenu)
        {
            ++contactParents;
            CHECK(contactRows[i].m_id == balloonmain::kContactMenuNotifyMode);
            CHECK(std::wstring(contactRows[i].m_label) == L"消息提醒设置");
        }
        if (!contactRows[i].m_isSeparator)
        {
            CHECK(std::wstring(contactRows[i].m_label).find(L"免打扰") == std::wstring::npos);
            CHECK(std::wstring(contactRows[i].m_label).find(L"特别关注") == std::wstring::npos);
        }
    }
    CHECK(contactParents == 1);

    //右键自己：没有这一项
    contact.m_isSelf = true;
    const std::vector<balloonmain::ContactMenuEntry> selfRows = balloonmain::BuildContactMenuModel(contact);
    for (size_t i = 0; i < selfRows.size(); ++i)
    {
        CHECK_FALSE(selfRows[i].m_hasNotifySubMenu);
    }
}

// =====================================================================
// NMB：会话列表里「消息盒子」一行（MsgBoxModel）
// =====================================================================

namespace {

//NMB 组的时间基准（Unix 毫秒）与相邻两条消息的间隔
const int64_t kNmbBaseTime = 1759460000000LL;
const int64_t kNmbStep = 60000;

//造一个会话：ctype、cid、时间偏移（第几个间隔）、未读、置顶、摘要
net::ConversationItem MakeConv(int ctype, int64_t cid, int ageSteps, int unread, bool pinned,
                               const std::string& digest)
{
    net::ConversationItem c;
    c.m_ctype = ctype;
    c.m_cid = cid;
    c.m_timestamp = kNmbBaseTime - ageSteps * kNmbStep;
    c.m_unread = unread;
    c.m_pinned = pinned ? 1 : 0;
    c.m_digest = digest;
    return c;
}

//消息盒子行的键
balloonmain::ConvNodeKey BoxKey()
{
    return balloonmain::ConvNodeKey(balloonmain::kMsgBoxRowCtype, balloonmain::kMsgBoxRowCid);
}

//普通会话的键
balloonmain::ConvNodeKey Key(int ctype, int64_t cid)
{
    return balloonmain::ConvNodeKey(ctype, cid);
}

} // anonymous namespace

TEST_CASE("NMB1 没有收进消息盒子的会话：没有消息盒子行，行序与会话顺序相同", "[notifymode][msgbox]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_single, 11, 0, 1, false, "a"));
    convs.push_back(MakeConv(conversation_group, 21, 1, 0, false, "b"));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_silent);
    balloonmain::SortConversations(convs);

    const balloonmain::MsgBoxSummary sum = balloonmain::ComputeMsgBoxSummary(convs, modes);
    CHECK(sum.m_count == 0);
    CHECK(sum.m_unread == 0);

    const std::vector<balloonmain::ConvNodeKey> order = balloonmain::BuildConversationRowOrder(convs, modes);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == Key(conversation_single, 11));
    CHECK(order[1] == Key(conversation_group, 21));
    CHECK(balloonmain::CollectMsgBoxConversations(convs, modes).empty());
}

TEST_CASE("NMB2 被收进的会话汇总：未读之和、最新时间、摘要取最新那一个", "[notifymode][msgbox]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_group, 21, 3, 4, false, "old"));
    convs.push_back(MakeConv(conversation_group, 22, 1, 2, false, "newest"));
    convs.push_back(MakeConv(conversation_single, 11, 2, 5, false, "normal"));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_msgbox);
    modes.SetMode(conversation_group, 22, notify_mode_msgbox);

    const balloonmain::MsgBoxSummary sum = balloonmain::ComputeMsgBoxSummary(convs, modes);
    CHECK(sum.m_count == 2);
    CHECK(sum.m_unread == 4 + 2);
    CHECK(sum.m_timestamp == kNmbBaseTime - kNmbStep);
    CHECK(sum.m_digestCtype == conversation_group);
    CHECK(sum.m_digestCid == 22);
    CHECK(sum.m_digest == "newest");
    CHECK_FALSE(sum.m_atMe);
}

TEST_CASE("NMB3 有带未读的 @我时摘要取那一条并去掉前缀，已读的 @我不再突出", "[notifymode][msgbox]")
{
    const std::string at = balloonmain::AtMeDigestPrefix();
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_group, 21, 3, 1, false, at + "call me"));
    convs.push_back(MakeConv(conversation_group, 22, 1, 2, false, "newest"));
    convs.push_back(MakeConv(conversation_group, 23, 0, 0, false, at + "already read"));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_msgbox);
    modes.SetMode(conversation_group, 22, notify_mode_msgbox);
    modes.SetMode(conversation_group, 23, notify_mode_msgbox);

    const balloonmain::MsgBoxSummary sum = balloonmain::ComputeMsgBoxSummary(convs, modes);
    CHECK(sum.m_count == 3);
    CHECK(sum.m_atMe);
    CHECK(sum.m_digestCid == 21);
    CHECK(sum.m_digest == "call me");
    //时间仍取被收进的会话里最新的那一条（23 最新，虽然它没有未读）
    CHECK(sum.m_timestamp == kNmbBaseTime);

    std::string stripped;
    CHECK(balloonmain::StripAtMePrefix(at + "x", stripped));
    CHECK(stripped == "x");
    CHECK_FALSE(balloonmain::StripAtMePrefix("[x] y", stripped));
    CHECK(stripped == "[x] y");
    CHECK_FALSE(balloonmain::StripAtMePrefix("", stripped));
}

TEST_CASE("NMB4 行序：被收进的会话（含置顶）不单独成行，消息盒子行排在置顶之后按时间混排", "[notifymode][msgbox]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_single, 11, 5, 0, true, "pinned normal"));
    convs.push_back(MakeConv(conversation_group, 21, 4, 0, true, "pinned boxed"));
    convs.push_back(MakeConv(conversation_single, 12, 0, 0, false, "newest normal"));
    convs.push_back(MakeConv(conversation_group, 22, 2, 1, false, "boxed"));
    convs.push_back(MakeConv(conversation_single, 13, 3, 0, false, "older normal"));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_msgbox);
    modes.SetMode(conversation_group, 22, notify_mode_msgbox);
    balloonmain::SortConversations(convs);

    //消息盒子行的时间取 21、22 里最新的 22（第 2 个间隔），排在 12（第 0 个）之后、13（第 3 个）之前
    const std::vector<balloonmain::ConvNodeKey> order = balloonmain::BuildConversationRowOrder(convs, modes);
    REQUIRE(order.size() == 4);
    CHECK(order[0] == Key(conversation_single, 11));
    CHECK(order[1] == Key(conversation_single, 12));
    CHECK(order[2] == BoxKey());
    CHECK(order[3] == Key(conversation_single, 13));
    CHECK(balloonmain::IsMsgBoxRowKey(order[2]));
    CHECK_FALSE(balloonmain::IsMsgBoxRowKey(order[0]));

    //消息盒子窗口里：置顶的在前
    const std::vector<net::ConversationItem> boxed = balloonmain::CollectMsgBoxConversations(convs, modes);
    REQUIRE(boxed.size() == 2);
    CHECK(boxed[0].m_cid == 21);
    CHECK(boxed[1].m_cid == 22);
}

TEST_CASE("NMB5 行序：消息盒子比全部普通会话都新时紧跟置顶会话，都旧时排在最后", "[notifymode][msgbox]")
{
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_msgbox);

    std::vector<net::ConversationItem> newest;
    newest.push_back(MakeConv(conversation_single, 11, 5, 0, true, "pinned"));
    newest.push_back(MakeConv(conversation_group, 21, 0, 1, false, "boxed"));
    newest.push_back(MakeConv(conversation_single, 12, 1, 0, false, "normal"));
    balloonmain::SortConversations(newest);
    std::vector<balloonmain::ConvNodeKey> order = balloonmain::BuildConversationRowOrder(newest, modes);
    REQUIRE(order.size() == 3);
    CHECK(order[0] == Key(conversation_single, 11));
    CHECK(order[1] == BoxKey());
    CHECK(order[2] == Key(conversation_single, 12));

    std::vector<net::ConversationItem> oldest;
    oldest.push_back(MakeConv(conversation_single, 12, 1, 0, false, "normal"));
    oldest.push_back(MakeConv(conversation_group, 21, 9, 1, false, "boxed"));
    balloonmain::SortConversations(oldest);
    order = balloonmain::BuildConversationRowOrder(oldest, modes);
    REQUIRE(order.size() == 2);
    CHECK(order[1] == BoxKey());

    //时间相同：普通会话在前
    std::vector<net::ConversationItem> tie;
    tie.push_back(MakeConv(conversation_single, 12, 1, 0, false, "normal"));
    tie.push_back(MakeConv(conversation_group, 21, 1, 1, false, "boxed"));
    balloonmain::SortConversations(tie);
    order = balloonmain::BuildConversationRowOrder(tie, modes);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == Key(conversation_single, 12));
    CHECK(order[1] == BoxKey());

    //只有被收进的会话：列表里只有消息盒子一行
    std::vector<net::ConversationItem> only;
    only.push_back(MakeConv(conversation_group, 21, 1, 1, true, "boxed"));
    order = balloonmain::BuildConversationRowOrder(only, modes);
    REQUIRE(order.size() == 1);
    CHECK(order[0] == BoxKey());
}

TEST_CASE("NMB6 挪行定位：行序里有消息盒子行时照样能算出目标，缺行时退回整表重建", "[notifymode][msgbox]")
{
    std::vector<balloonmain::ConvNodeKey> order;
    order.push_back(Key(conversation_single, 11));
    order.push_back(BoxKey());
    order.push_back(Key(conversation_single, 12));

    std::map<balloonmain::ConvNodeKey, int> nodeOf;
    nodeOf[Key(conversation_single, 11)] = 100;
    nodeOf[BoxKey()] = 101;
    nodeOf[Key(conversation_single, 12)] = 102;

    int node = 0;
    int before = 0;
    REQUIRE(balloonmain::FindRowMoveTarget(order, nodeOf, Key(conversation_single, 11), node, before));
    CHECK(node == 100);
    CHECK(before == 101);
    REQUIRE(balloonmain::FindRowMoveTarget(order, nodeOf, BoxKey(), node, before));
    CHECK(node == 101);
    CHECK(before == 102);
    REQUIRE(balloonmain::FindRowMoveTarget(order, nodeOf, Key(conversation_single, 12), node, before));
    CHECK(node == 102);
    CHECK(before == -1);

    //不在行序里（比如被收进了消息盒子）
    CHECK_FALSE(balloonmain::FindRowMoveTarget(order, nodeOf, Key(conversation_group, 21), node, before));
    //后面那一行还没建出来
    nodeOf.erase(BoxKey());
    CHECK_FALSE(balloonmain::FindRowMoveTarget(order, nodeOf, Key(conversation_single, 11), node, before));
    //它自己的行不存在
    CHECK_FALSE(balloonmain::FindRowMoveTarget(order, nodeOf, BoxKey(), node, before));
}

TEST_CASE("NMB7 屏蔽、不提醒、特别关注的会话照常单独成行，不进消息盒子", "[notifymode][msgbox]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_group, 21, 0, 3, false, "silent"));
    convs.push_back(MakeConv(conversation_group, 22, 1, 3, false, "block"));
    convs.push_back(MakeConv(conversation_group, 23, 2, 3, false, "starred"));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 21, notify_mode_silent);
    modes.SetMode(conversation_group, 22, notify_mode_block);
    modes.SetMode(conversation_group, 23, notify_mode_starred);
    balloonmain::SortConversations(convs);

    CHECK(balloonmain::ComputeMsgBoxSummary(convs, modes).m_count == 0);
    CHECK(balloonmain::BuildConversationRowOrder(convs, modes).size() == 3);
}

// =====================================================================
// NMG：批量管理窗口（NotifyManageModel）
// =====================================================================

namespace {

//造一行
balloonnotifymode::ManageItem MakeItem(int ctype, int64_t cid, const wchar_t* name, const wchar_t* extra)
{
    balloonnotifymode::ManageItem item;
    item.m_ctype = ctype;
    item.m_cid = cid;
    item.m_name = name;
    item.m_extra = extra;
    return item;
}

//NMG 组的候选来源：两个群；单聊基础池两人；通讯录三人，其中一人也在基础池里
balloonnotifymode::ManagePools MakePools()
{
    balloonnotifymode::ManagePools pools;
    pools.m_groups.push_back(MakeItem(conversation_group, 21, L"项目组", L"21"));
    pools.m_groups.push_back(MakeItem(conversation_group, 22, L"Lunch Club", L"22"));
    pools.m_singleBase.push_back(MakeItem(conversation_single, 11, L"江屿白", L"jiangyb"));
    pools.m_singleBase.push_back(MakeItem(conversation_single, 12, L"周思源", L"zhousy"));
    pools.m_contacts.push_back(MakeItem(conversation_single, 11, L"江屿白", L"jiangyb"));
    pools.m_contacts.push_back(MakeItem(conversation_single, 13, L"江晓燕", L"jiangxy"));
    pools.m_contacts.push_back(MakeItem(conversation_single, 14, L"林冲", L"linchong"));
    return pools;
}

} // anonymous namespace

TEST_CASE("NMG1 群聊页列出全部群，单聊页不搜索时只列基础池", "[notifymode][notifymanage]")
{
    const balloonnotifymode::ManagePools pools = MakePools();
    bool truncated = true;
    const std::vector<balloonnotifymode::ManageItem> groups =
        balloonnotifymode::BuildVisibleItems(pools, conversation_group, L"", truncated);
    REQUIRE(groups.size() == 2);
    CHECK_FALSE(truncated);
    CHECK(groups[0].m_cid == 21);

    const std::vector<balloonnotifymode::ManageItem> singles =
        balloonnotifymode::BuildVisibleItems(pools, conversation_single, L"", truncated);
    REQUIRE(singles.size() == 2);
    CHECK(singles[0].m_cid == 11);
    CHECK(singles[1].m_cid == 12);

    CHECK(balloonnotifymode::BuildVisibleItems(pools, 0, L"", truncated).empty());
}

TEST_CASE("NMG2 单聊页搜索时在通讯录里找，去重且基础池在前；名字与账号都能搜，不分大小写", "[notifymode][notifymanage]")
{
    const balloonnotifymode::ManagePools pools = MakePools();
    bool truncated = false;
    const std::vector<balloonnotifymode::ManageItem> byName =
        balloonnotifymode::BuildVisibleItems(pools, conversation_single, L"江", truncated);
    REQUIRE(byName.size() == 2);
    CHECK(byName[0].m_cid == 11);   //基础池里的在前
    CHECK(byName[1].m_cid == 13);   //通讯录补充，11 不再重复

    const std::vector<balloonnotifymode::ManageItem> byAccount =
        balloonnotifymode::BuildVisibleItems(pools, conversation_single, L"LINCH", truncated);
    REQUIRE(byAccount.size() == 1);
    CHECK(byAccount[0].m_cid == 14);

    const std::vector<balloonnotifymode::ManageItem> group =
        balloonnotifymode::BuildVisibleItems(pools, conversation_group, L"lunch", truncated);
    REQUIRE(group.size() == 1);
    CHECK(group[0].m_cid == 22);

    CHECK(balloonnotifymode::BuildVisibleItems(pools, conversation_single, L"不存在", truncated).empty());
}

TEST_CASE("NMG3 超过上限时截断并置标记", "[notifymode][notifymanage]")
{
    balloonnotifymode::ManagePools pools;
    const size_t total = balloonnotifymode::kNotifyManageMaxRows + 1;
    for (size_t i = 0; i < total; ++i)
    {
        pools.m_groups.push_back(MakeItem(conversation_group, (int64_t)(i + 1), L"g", L""));
    }
    bool truncated = false;
    const std::vector<balloonnotifymode::ManageItem> items =
        balloonnotifymode::BuildVisibleItems(pools, conversation_group, L"", truncated);
    CHECK(items.size() == balloonnotifymode::kNotifyManageMaxRows);
    CHECK(truncated);
}

TEST_CASE("NMG4 勾选：切换、全选、整体状态、按页签计数，换搜索词不丢", "[notifymode][notifymanage]")
{
    const balloonnotifymode::ManagePools pools = MakePools();
    bool truncated = false;
    const std::vector<balloonnotifymode::ManageItem> groups =
        balloonnotifymode::BuildVisibleItems(pools, conversation_group, L"", truncated);

    balloonnotifymode::ManageSelection sel;
    CHECK(sel.StateOf(groups) == balloonnotifymode::kManageCheckNone);
    sel.Toggle(conversation_group, 21);
    CHECK(sel.IsChecked(conversation_group, 21));
    CHECK(sel.StateOf(groups) == balloonnotifymode::kManageCheckSome);
    sel.SetAll(groups, true);
    CHECK(sel.StateOf(groups) == balloonnotifymode::kManageCheckAll);
    CHECK(sel.CountOf(conversation_group) == 2);
    CHECK(sel.CountOf(conversation_single) == 0);

    //单聊与群聊的同一个 id 互不影响
    sel.SetChecked(conversation_single, 21, true);
    CHECK(sel.CountOf(conversation_single) == 1);
    CHECK(sel.IsChecked(conversation_group, 21));

    //换了搜索词，只列出一个群，之前勾的另一个仍在
    const std::vector<balloonnotifymode::ManageItem> filtered =
        balloonnotifymode::BuildVisibleItems(pools, conversation_group, L"lunch", truncated);
    sel.SetAll(filtered, false);
    CHECK(sel.IsChecked(conversation_group, 21));
    CHECK_FALSE(sel.IsChecked(conversation_group, 22));

    const std::vector<int64_t> ids = sel.CheckedIds(conversation_group);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 21);

    sel.Clear();
    CHECK(sel.CountOf(conversation_group) == 0);
    CHECK(sel.StateOf(std::vector<balloonnotifymode::ManageItem>()) == balloonnotifymode::kManageCheckNone);
}

TEST_CASE("NMG5 提交条目：跳过模式没变的、单聊不能屏蔽，按 cid 升序", "[notifymode][notifymanage]")
{
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, 22, notify_mode_silent);

    balloonnotifymode::ManageSelection sel;
    sel.SetChecked(conversation_group, 23, true);
    sel.SetChecked(conversation_group, 22, true);
    sel.SetChecked(conversation_group, 21, true);
    sel.SetChecked(conversation_single, 11, true);

    int skipped = 0;
    const std::vector<net::NotifyModeEntry> entries =
        balloonnotifymode::BuildApplyEntries(sel, conversation_group, notify_mode_silent, modes, skipped);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].m_cid == 21);
    CHECK(entries[1].m_cid == 23);
    CHECK(entries[0].m_ctype == conversation_group);
    CHECK(entries[0].m_mode == notify_mode_silent);
    CHECK(skipped == 1);

    //单聊页设屏蔽：一条也不生成
    const std::vector<net::NotifyModeEntry> blocked =
        balloonnotifymode::BuildApplyEntries(sel, conversation_single, notify_mode_block, modes, skipped);
    CHECK(blocked.empty());
    CHECK(skipped == 1);
}

TEST_CASE("NMG6 特别关注上限预检：设完超过 50 个才拦，改掉原有的特别关注也算进去", "[notifymode][notifymanage]")
{
    balloonmain::NotifyModeModel modes;
    const int kExisting = balloonmain::kNotifyModeStarredMax - 1;
    for (int i = 0; i < kExisting; ++i)
    {
        modes.SetMode(conversation_single, 1000 + i, notify_mode_starred);
    }

    std::vector<net::NotifyModeEntry> one;
    net::NotifyModeEntry e;
    e.m_ctype = conversation_group;
    e.m_cid = 21;
    e.m_mode = notify_mode_starred;
    one.push_back(e);
    CHECK_FALSE(balloonnotifymode::WouldExceedStarredLimit(modes, one));   //正好 50 个

    std::vector<net::NotifyModeEntry> two = one;
    e.m_cid = 22;
    two.push_back(e);
    CHECK(balloonnotifymode::WouldExceedStarredLimit(modes, two));         //51 个

    //同时把一个原有的特别关注改回允许提醒：仍是 50 个
    net::NotifyModeEntry off;
    off.m_ctype = conversation_single;
    off.m_cid = 1000;
    off.m_mode = notify_mode_allow;
    two.push_back(off);
    CHECK_FALSE(balloonnotifymode::WouldExceedStarredLimit(modes, two));
}
