/**
 *  BotClientTests.cpp
 *
 *  机器人功能 exe 侧的单元测试（docs/机器人方案.md，标签 [bot]）。覆盖：
 *    BOT-C1  2801 应答解析：能力说明、机器人各字段、用法说明（字段名与服务端 BotProtocol.cpp 一致，ID 为字符串）；
 *    BOT-C2  2801 / 2810 / 2811 请求构造与 2820 推送解析；
 *    BOT-C3  2810 应答解析：缺少 default 标记时按默认文字处理；
 *    BOT-C4  哪些帧原样转交 FlamingoBot.dll（只有 2802～2809）；
 *    BOT-C5  （已删除：成员栏里机器人一行的文字。2026-10-08 机器人行改成成员行样式后名字与标签分开显示，由 BOT-C15 覆盖）
 *    BOT-C6  用法说明：被 @、关键词三种匹配、入群、四种定时、外部推送、不认识的类型；
 *    BOT-C7  资料卡小窗的内容（MakeBotCardView）：简介、用法、来源、标签、头像上的字与底色，没有可用触发方式时用法为空；
 *    BOT-C8  单聊代答的显示文字：去掉服务端前缀、换成按界面语言翻译的标记；
 *    BOT-C9  自动回复设置与服务端之间的换算，以及企业开关打开后本机不再自动回复；
 *    BOT-C10 消息推送里的机器人标记：单聊代答、群机器人消息、非文本消息不认；
 *    BOT-C11 群系统提示：机器人的添加、删除、关闭、打开，操作人缺失时写「群主」；
 *    BOT-C12 系统菜单「我的机器人」紧跟在「备忘与提醒」之后，不可用时不出现；
 *    BOT-C13 群聊气泡发送者的机器人标记经 ResolveGroupSender 保留下来；
 *    BOT-C14 机器人删除后按消息自带的 content.bot.name 显示发送者；
 *    BOT-C15 成员页签机器人行的标签、头像上的字与底色（2026-10-08 改成与成员行同一样式）。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不创建窗口、不连网络。比较界面文字之前先把多语言模块恢复到
 *  未初始化状态（显示源文字），免得前面的用例留下的语言设置影响结果。
 *
 *  balloonwj@qq.com   2026-10-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../net/BotProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/SystemTipText.h"
#include "../net/Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../Common/I18n/I18n.h"
#include "../UI/BalloonChatWnd/GroupBotModel.h"
#include "../UI/BalloonChatWnd/GroupSenderRefresh.h"
#include "../UI/BalloonMain/AutoReplySync.h"
#include "../UI/BalloonMain/AutoReplyPolicy.h"
#include "../UI/BalloonMain/MainPanelSystemMenu.h"
#include "../UI/BalloonMain/MainPanelIcons.h"

using net::BotProtocolClient;
using net::MessageNotify;
using net::MessageProtocolClient;
using balloonmain::SystemMenuEntry;
using balloonmain::BuildSystemMenuModel;
using balloonmain::kRecordMenuHidden;
using balloonmain::kSysMenuMemo;
using balloonmain::kSysMenuMyBots;
using balloonmain::kSysMenuOcr;
using balloonmain::kIconBot;
using balloonchatwnd::GroupSenderRender;
using balloonchatwnd::ResolveGroupSender;

namespace {

//测试用的群号、机器人 ID 与账号
const int64_t kGroupId = 30001;
const int64_t kBotId = 12;
const int64_t kBotUserId = 880012;
const int64_t kPeerUid = 10002;
const int64_t kSelfUid = 10001;

//帧号段的边界（docs/协议设计-v2.md 25 节）
const uint16_t kCmdGetGroupBots = 2801;
const uint16_t kCmdFirstDllFrame = 2802;
const uint16_t kCmdLastDllFrame = 2809;
const uint16_t kCmdGetAutoReply = 2810;
const uint16_t kCmdSetAutoReply = 2811;
const uint16_t kCmdGroupBotsChanged = 2820;

//用法说明的触发方式与定时种类，取值与服务端 BotDefs.h 一致
const int kTriggerAt = 1;
const int kTriggerKeyword = 2;
const int kTriggerJoin = 3;
const int kTriggerSchedule = 4;
const int kTriggerWebhook = 6;
const int kTriggerUnknown = 99;
const int kMatchContains = 1;
const int kMatchExact = 2;
const int kMatchPrefix = 3;
const int kScheduleDaily = 1;
const int kScheduleWorkdays = 2;
const int kScheduleWeekly = 3;
const int kScheduleMonthly = 4;

//每群最多群机器人数（服务端的默认上限）
const int kGroupBotMax = 5;
//测试用的每月几号
const int kMonthDay = 15;

//把 Json::Value 序列化为紧凑 JSON 文本
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    wb["emitUTF8"] = true;
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成 Json::Value
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//按服务端 BuildGroupBotsResponse 的格式拼一条 2801 应答：一个企业机器人（被 @ 用问答作答、在本群关闭）
//和一个群机器人（关键词 + 每周定时）
std::string MakeGroupBotsJson()
{
    Json::Value root;
    root["code"] = 0;
    root["msg"] = "ok";
    root["groupid"] = std::to_string(kGroupId);
    Json::Value caps;
    caps["isowner"] = 1;
    caps["groupbot"] = 1;
    caps["external"] = 0;
    caps["groupbotmax"] = kGroupBotMax;
    root["caps"] = caps;

    Json::Value bots(Json::arrayValue);
    Json::Value e;
    e["botid"] = std::to_string(kBotId);
    e["userid"] = std::to_string(kBotUserId);
    e["ownertype"] = 1;
    e["source"] = "enterprise";
    e["name"] = u8"行政助手";
    e["desc"] = u8"回答行政问题";
    e["glyph"] = u8"行";
    e["color"] = "#3366CC";
    e["status"] = 0;
    e["off"] = 1;
    e["active"] = 0;
    e["canoff"] = 1;
    e["atable"] = 1;
    Json::Value eu(Json::arrayValue);
    Json::Value at;
    at["t"] = kTriggerAt;
    at["qa"] = 1;
    eu.append(at);
    e["usage"] = eu;
    bots.append(e);

    Json::Value g;
    g["botid"] = "13";
    g["userid"] = "880013";
    g["ownertype"] = 2;
    g["source"] = "group";
    g["name"] = u8"值班";
    g["status"] = 1;
    g["off"] = 0;
    g["active"] = 0;
    g["canoff"] = 1;
    g["atable"] = 0;
    Json::Value gu(Json::arrayValue);
    Json::Value kw;
    kw["t"] = kTriggerKeyword;
    Json::Value kws(Json::arrayValue);
    kws.append(u8"值班");
    kws.append(u8"排班");
    kw["kw"] = kws;
    kw["m"] = kMatchPrefix;
    kw["at"] = 0;
    gu.append(kw);
    Json::Value sc;
    sc["t"] = kTriggerSchedule;
    sc["k"] = kScheduleWeekly;
    sc["time"] = "09:30";
    Json::Value wd(Json::arrayValue);
    wd.append(1);
    wd.append(5);
    sc["wd"] = wd;
    sc["md"] = 1;
    gu.append(sc);
    g["usage"] = gu;
    bots.append(g);

    root["bots"] = bots;
    return WriteCompact(root);
}

//拼一条服务端格式的消息推送，content 由调用方给出
std::string MakeNotifyJson(int ctype, int64_t cid, int64_t from, int msgtype, const Json::Value& content)
{
    Json::Value root;
    root["msgid"] = "900001";
    root["clientmsgid"] = "bot-test-0001";
    root["ctype"] = ctype;
    root["cid"] = std::to_string(cid);
    root["from"] = std::to_string(from);
    root["to"] = std::to_string(ctype == conversation_single ? kSelfUid : cid);
    root["msgtype"] = msgtype;
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(1790000000000LL);
    root["recalled"] = false;
    return WriteCompact(root);
}

//机器人的一项用法：只填触发方式，其余取默认
net::BotUsageItem MakeUsage(int type)
{
    net::BotUsageItem u;
    u.m_type = type;
    return u;
}

//在系统菜单里找某个命令 id 的行号；找不到返回 -1
int IndexOfMenu(const std::vector<SystemMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && !items[i].isHeader && items[i].id == id)
            return (int)i;
    }
    return -1;
}

} // anonymous namespace

TEST_CASE("BOT-C1 2801 应答解析出能力说明、机器人各字段与用法说明", "[bot]")
{
    net::GroupBotsResult r;
    REQUIRE(BotProtocolClient::parseGetGroupBotsResponse(MakeGroupBotsJson(), r));
    CHECK(r.m_code == 0);
    CHECK(r.m_groupid == kGroupId);
    CHECK(r.m_caps.m_isOwner);
    CHECK(r.m_caps.m_groupBotAllowed);
    CHECK_FALSE(r.m_caps.m_externalAllowed);
    CHECK(r.m_caps.m_groupBotMax == kGroupBotMax);
    REQUIRE(r.m_bots.size() == 2);

    const net::GroupBotInfo& e = r.m_bots[0];
    CHECK(e.m_botId == kBotId);
    CHECK(e.m_userId == kBotUserId);
    CHECK(e.m_ownerType == 1);
    CHECK_FALSE(e.m_isGroupBot);
    CHECK(e.m_name == u8"行政助手");
    CHECK(e.m_desc == u8"回答行政问题");
    CHECK(e.m_glyph == u8"行");
    CHECK(e.m_color == "#3366CC");
    CHECK(e.m_status == 0);
    CHECK(e.m_off);
    CHECK_FALSE(e.m_active);
    CHECK(e.m_canOff);
    CHECK(e.m_atable);
    REQUIRE(e.m_usage.size() == 1);
    CHECK(e.m_usage[0].m_type == kTriggerAt);
    CHECK(e.m_usage[0].m_qa);

    const net::GroupBotInfo& g = r.m_bots[1];
    CHECK(g.m_isGroupBot);
    CHECK(g.m_status == 1);
    CHECK_FALSE(g.m_atable);
    REQUIRE(g.m_usage.size() == 2);
    CHECK(g.m_usage[0].m_type == kTriggerKeyword);
    REQUIRE(g.m_usage[0].m_keywords.size() == 2);
    CHECK(g.m_usage[0].m_keywords[1] == u8"排班");
    CHECK(g.m_usage[0].m_match == kMatchPrefix);
    CHECK_FALSE(g.m_usage[0].m_needAt);
    CHECK(g.m_usage[1].m_type == kTriggerSchedule);
    CHECK(g.m_usage[1].m_scheduleKind == kScheduleWeekly);
    CHECK(g.m_usage[1].m_time == "09:30");
    REQUIRE(g.m_usage[1].m_weekdays.size() == 2);
    CHECK(g.m_usage[1].m_weekdays[1] == 5);

    //包体不是 JSON 对象时返回 false
    net::GroupBotsResult bad;
    CHECK_FALSE(BotProtocolClient::parseGetGroupBotsResponse("[1,2]", bad));
    CHECK_FALSE(BotProtocolClient::parseGetGroupBotsResponse("not json", bad));
}

TEST_CASE("BOT-C2 请求构造与 2820 推送解析", "[bot]")
{
    Json::Value root;
    REQUIRE(ParseJsonText(BotProtocolClient::buildGetGroupBotsRequest(kGroupId), root));
    CHECK(root["groupid"].asString() == std::to_string(kGroupId));

    Json::Value empty;
    REQUIRE(ParseJsonText(BotProtocolClient::buildGetAutoReplyRequest(), empty));
    CHECK(empty.isObject());

    net::AutoReplyWire w;
    w.m_awayOn = true;
    w.m_awayText = u8"我在开会";
    w.m_awayDefault = false;
    w.m_busyOn = false;
    w.m_busyText = u8"忙";
    w.m_busyDefault = true;
    Json::Value set;
    REQUIRE(ParseJsonText(BotProtocolClient::buildSetAutoReplyRequest(w), set));
    CHECK(set["awayon"].asInt() == 1);
    CHECK(set["awaytext"].asString() == u8"我在开会");
    CHECK(set["awaydefault"].asInt() == 0);
    CHECK(set["busyon"].asInt() == 0);
    CHECK(set["busydefault"].asInt() == 1);

    int64_t groupid = -1;
    REQUIRE(BotProtocolClient::parseGroupBotsChangedNotify("{\"groupid\":\"30001\"}", groupid));
    CHECK(groupid == kGroupId);
    //groupid 为 0 表示本企业的全部群
    REQUIRE(BotProtocolClient::parseGroupBotsChangedNotify("{\"groupid\":\"0\"}", groupid));
    CHECK(groupid == 0);
    //缺少 groupid 视为不合法
    CHECK_FALSE(BotProtocolClient::parseGroupBotsChangedNotify("{}", groupid));

    int code = -1;
    std::string msg;
    REQUIRE(BotProtocolClient::parseSimpleResponse("{\"code\":1406,\"msg\":\"sensitive\"}", code, msg));
    CHECK(code == error_code_bot_sensitive_word);
    CHECK(msg == "sensitive");
}

TEST_CASE("BOT-C3 2810 应答解析，缺少 default 标记时按默认文字处理", "[bot]")
{
    net::AutoReplyResult r;
    REQUIRE(BotProtocolClient::parseGetAutoReplyResponse(
        u8"{\"code\":0,\"exists\":1,\"allowed\":1,\"awayon\":1,\"awaytext\":\"开会中\",\"awaydefault\":0,"
        u8"\"busyon\":0,\"busytext\":\"忙碌\"}", r));
    CHECK(r.m_code == 0);
    CHECK(r.m_exists);
    CHECK(r.m_allowed);
    CHECK(r.m_settings.m_awayOn);
    CHECK(r.m_settings.m_awayText == u8"开会中");
    CHECK_FALSE(r.m_settings.m_awayDefault);
    CHECK_FALSE(r.m_settings.m_busyOn);
    CHECK(r.m_settings.m_busyDefault);

    net::AutoReplyResult none;
    REQUIRE(BotProtocolClient::parseGetAutoReplyResponse("{\"code\":0,\"exists\":0,\"allowed\":0}", none));
    CHECK_FALSE(none.m_exists);
    CHECK_FALSE(none.m_allowed);
}

TEST_CASE("BOT-C4 只有 2802～2809 原样转交 FlamingoBot.dll", "[bot]")
{
    CHECK_FALSE(BotProtocolClient::isDllFrame(kCmdGetGroupBots));
    for (uint16_t cmd = kCmdFirstDllFrame; cmd <= kCmdLastDllFrame; ++cmd)
    {
        CHECK(BotProtocolClient::isDllFrame(cmd));
    }
    CHECK_FALSE(BotProtocolClient::isDllFrame(kCmdGetAutoReply));
    CHECK_FALSE(BotProtocolClient::isDllFrame(kCmdSetAutoReply));
    CHECK_FALSE(BotProtocolClient::isDllFrame(kCmdGroupBotsChanged));
    CHECK_FALSE(BotProtocolClient::isDllFrame(cmd_heartbeat));
}

TEST_CASE("BOT-C6 用法说明按触发方式写成文字", "[bot]")
{
    i18n::ResetForTest();
    SECTION("被 @：区分是否用问答作答")
    {
        net::BotUsageItem u = MakeUsage(kTriggerAt);
        CHECK(balloonchatwnd::BotUsageLine(u) == L"在群里 @我");
        u.m_qa = true;
        CHECK(balloonchatwnd::BotUsageLine(u) == L"在群里 @我 并输入问题，我会从群问答里找答案");
    }

    SECTION("关键词：三种匹配方式，要求 @ 时补一句")
    {
        net::BotUsageItem u = MakeUsage(kTriggerKeyword);
        u.m_keywords.push_back(u8"值班");
        u.m_keywords.push_back(u8"排班");
        u.m_needAt = false;
        u.m_match = kMatchExact;
        const std::wstring exact = balloonchatwnd::BotUsageLine(u);
        CHECK(exact.find(L"发送「值班") == 0);
        CHECK(exact.find(L"排班") != std::wstring::npos);
        u.m_match = kMatchPrefix;
        CHECK(balloonchatwnd::BotUsageLine(u).find(L"开头的消息") != std::wstring::npos);
        u.m_match = kMatchContains;
        const std::wstring contains = balloonchatwnd::BotUsageLine(u);
        CHECK(contains.find(L"发送包含「") == 0);
        CHECK(contains.find(L"（需要同时 @我）") == std::wstring::npos);
        u.m_needAt = true;
        CHECK(balloonchatwnd::BotUsageLine(u).find(L"（需要同时 @我）") != std::wstring::npos);
    }

    SECTION("入群与外部推送")
    {
        CHECK(balloonchatwnd::BotUsageLine(MakeUsage(kTriggerJoin)) == L"新成员入群时发送欢迎消息");
        CHECK(balloonchatwnd::BotUsageLine(MakeUsage(kTriggerWebhook)) == L"接收外部系统推送的消息");
    }

    SECTION("定时：每天、工作日、每周、每月")
    {
        net::BotUsageItem u = MakeUsage(kTriggerSchedule);
        u.m_time = "09:30";
        u.m_scheduleKind = kScheduleDaily;
        CHECK(balloonchatwnd::BotUsageLine(u) == L"每天 09:30 发送消息");
        u.m_scheduleKind = kScheduleWorkdays;
        CHECK(balloonchatwnd::BotUsageLine(u) == L"每个工作日 09:30 发送消息");
        u.m_scheduleKind = kScheduleMonthly;
        u.m_monthDay = kMonthDay;
        CHECK(balloonchatwnd::BotUsageLine(u) == L"每月 15 号 09:30 发送消息");
        u.m_scheduleKind = kScheduleWeekly;
        u.m_weekdays.push_back(1);
        u.m_weekdays.push_back(5);
        //超出 1～7 的值忽略
        u.m_weekdays.push_back(9);
        const std::wstring weekly = balloonchatwnd::BotUsageLine(u);
        CHECK(weekly.find(L"每周的") == 0);
        CHECK(weekly.find(L"09:30 发送消息") != std::wstring::npos);
    }

    SECTION("不认识的触发方式不显示")
    {
        CHECK(balloonchatwnd::BotUsageLine(MakeUsage(kTriggerUnknown)).empty());
    }
}

TEST_CASE("BOT-C7 资料卡小窗的内容：简介、用法、来源、标签与头像", "[bot]")
{
    i18n::ResetForTest();
    const COLORREF kFallback = RGB(1, 2, 3);
    net::GroupBotInfo b;
    b.m_name = u8"行政助手";
    b.m_desc = u8"回答行政问题";
    b.m_color = "#1E88E5";
    b.m_usage.push_back(MakeUsage(kTriggerJoin));
    b.m_usage.push_back(MakeUsage(kTriggerUnknown));
    const balloonchatwnd::BotCardView card = balloonchatwnd::MakeBotCardView(b, kFallback);
    CHECK(card.m_name == L"行政助手");
    CHECK(card.m_avatarText == L"行");
    CHECK(card.m_avatarColor == RGB(0x1E, 0x88, 0xE5));
    CHECK(card.m_tag == L"企业");
    CHECK(card.m_desc == L"回答行政问题");
    //不认识的用法类型写不成文字，不收
    REQUIRE(card.m_usageLines.size() == 1);
    CHECK(card.m_usageLines[0] == L"新成员入群时发送欢迎消息");
    CHECK(card.m_source == L"企业管理员配置");

    //群机器人：没有标签，来源是群主；没有一项能写成文字时用法为空（资料卡显示「暂无可用的触发方式」）；
    //没有设置底色时用调用方给的颜色
    net::GroupBotInfo g;
    g.m_isGroupBot = true;
    g.m_name = u8"值班提醒";
    g.m_usage.push_back(MakeUsage(kTriggerUnknown));
    const balloonchatwnd::BotCardView gc = balloonchatwnd::MakeBotCardView(g, kFallback);
    CHECK(gc.m_tag.empty());
    CHECK(gc.m_desc.empty());
    CHECK(gc.m_usageLines.empty());
    CHECK(gc.m_source == L"群主配置");
    CHECK(gc.m_avatarColor == kFallback);
}

TEST_CASE("BOT-C8 单聊代答的显示文字换成按界面语言翻译的标记", "[bot]")
{
    i18n::ResetForTest();
    CHECK(balloonchatwnd::BotReplyDisplayText(L"[机器人] 我在开会", "personal") == L"[机器人代答] 我在开会");
    CHECK(balloonchatwnd::BotReplyDisplayText(L"[自动回复] 我在开会", "autoreply") == L"[自动回复] 我在开会");
    //没有服务端前缀的也加上标记
    CHECK(balloonchatwnd::BotReplyDisplayText(L"我在开会", "personal") == L"[机器人代答] 我在开会");
    //群里的机器人消息（kind 为空）原样显示
    CHECK(balloonchatwnd::BotReplyDisplayText(L"[机器人] 你好", "") == L"[机器人] 你好");
}

TEST_CASE("BOT-C9 自动回复设置与服务端之间的换算", "[bot][autoreply]")
{
    i18n::ResetForTest();
    SECTION("文字为空或只有空白时上传默认文字并置 default 标记")
    {
        const net::AutoReplyWire w = balloonmain::BuildAutoReplyWire(true, L"  ", false, L"我在忙");
        CHECK(w.m_awayOn);
        CHECK(w.m_awayDefault);
        CHECK_FALSE(w.m_awayText.empty());
        CHECK_FALSE(w.m_busyOn);
        CHECK_FALSE(w.m_busyDefault);
        CHECK(w.m_busyText == u8"我在忙");
    }

    SECTION("从没改过的本机设置不必上传")
    {
        CHECK(balloonmain::IsDefaultAutoReply(false, L"", false, L" "));
        CHECK_FALSE(balloonmain::IsDefaultAutoReply(true, L"", false, L""));
        CHECK_FALSE(balloonmain::IsDefaultAutoReply(false, L"开会", false, L""));
    }

    SECTION("服务端标为默认文字的，本机写空串")
    {
        net::AutoReplyWire w;
        w.m_awayOn = true;
        w.m_awayText = u8"默认文字";
        w.m_awayDefault = true;
        w.m_busyOn = true;
        w.m_busyText = u8"开会中";
        w.m_busyDefault = false;
        bool awayOn = false;
        bool busyOn = false;
        std::wstring awayText = L"旧";
        std::wstring busyText;
        balloonmain::ApplyServerAutoReply(w, awayOn, awayText, busyOn, busyText);
        CHECK(awayOn);
        CHECK(awayText.empty());
        CHECK(busyOn);
        CHECK(busyText == L"开会中");
    }

    SECTION("企业打开机器人后由服务端代答，本机不再自动回复")
    {
        CHECK_FALSE(balloonmain::ShouldClientAutoReply(true));
        CHECK(balloonmain::ShouldClientAutoReply(false));
    }
}

TEST_CASE("BOT-C10 消息推送里的机器人标记", "[bot]")
{
    SECTION("单聊代答：带 autoreply 与 bot.kind")
    {
        Json::Value content;
        content["text"] = u8"[机器人] 我在开会";
        content["autoreply"] = true;
        Json::Value mark;
        mark["id"] = std::to_string(kBotId);
        mark["name"] = u8"助理";
        mark["kind"] = "personal";
        content["bot"] = mark;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(
            MakeNotifyJson(conversation_single, kPeerUid, kPeerUid, message_type_text, content), n));
        CHECK(n.m_autoReply);
        CHECK(n.m_botId == kBotId);
        CHECK(n.m_botName == u8"助理");
        CHECK(n.m_botKind == "personal");
        //正文原样保留，由显示层去掉前缀
        CHECK(n.m_text == u8"[机器人] 我在开会");
    }

    SECTION("群机器人消息：只有 bot.id，kind 为空，不是自动回复")
    {
        Json::Value content;
        content["text"] = u8"早上好";
        Json::Value mark;
        mark["id"] = std::to_string(kBotId);
        content["bot"] = mark;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(
            MakeNotifyJson(conversation_group, kGroupId, kBotUserId, message_type_text, content), n));
        CHECK(n.m_botId == kBotId);
        CHECK(n.m_botKind.empty());
        CHECK_FALSE(n.m_autoReply);
    }

    SECTION("普通消息与非文本消息不认机器人标记")
    {
        Json::Value plain;
        plain["text"] = u8"你好";
        MessageNotify n1;
        REQUIRE(MessageProtocolClient::parseMessageNotify(
            MakeNotifyJson(conversation_single, kPeerUid, kPeerUid, message_type_text, plain), n1));
        CHECK(n1.m_botId == 0);

        Json::Value notText;
        Json::Value mark;
        mark["id"] = std::to_string(kBotId);
        notText["bot"] = mark;
        notText["tipkind"] = "member_leave";
        notText["tipargs"] = Json::Value(Json::objectValue);
        MessageNotify n2;
        REQUIRE(MessageProtocolClient::parseMessageNotify(
            MakeNotifyJson(conversation_group, kGroupId, 0, message_type_system, notText), n2));
        CHECK(n2.m_botId == 0);
    }
}

TEST_CASE("BOT-C11 群系统提示：机器人的添加、删除、关闭与打开", "[bot][systemtip]")
{
    i18n::ResetForTest();
    std::string out;
    REQUIRE(net::FormatSystemTipJson(kSystemTipBotAdded, u8"{\"operator\":\"张三\",\"bot\":\"值班\"}", out));
    CHECK(out == u8"张三 添加了机器人「值班」");
    REQUIRE(net::FormatSystemTipJson(kSystemTipBotAdded, u8"{\"operator\":\"张三\",\"bot\":\"值班\",\"external\":\"1\"}",
                                     out));
    CHECK(out == u8"张三 添加了机器人「值班」，@ 它的消息会发送到外部服务");
    REQUIRE(net::FormatSystemTipJson(kSystemTipBotRemoved, u8"{\"operator\":\"张三\",\"bot\":\"值班\"}", out));
    CHECK(out == u8"张三 删除了机器人「值班」");
    REQUIRE(net::FormatSystemTipJson(kSystemTipBotOff, u8"{\"operator\":\"张三\",\"bot\":\"值班\"}", out));
    CHECK(out == u8"张三 在本群关闭了机器人「值班」");
    //操作人缺失时写「群主」
    REQUIRE(net::FormatSystemTipJson(kSystemTipBotOn, u8"{\"bot\":\"值班\"}", out));
    CHECK(out == u8"群主 在本群打开了机器人「值班」");
}

TEST_CASE("BOT-C12 系统菜单「我的机器人」紧跟在「备忘与提醒」之后，不可用时不出现", "[bot][sysmenu]")
{
    i18n::ResetForTest();
    const std::vector<SystemMenuEntry> off = BuildSystemMenuModel(false, kRecordMenuHidden, NULL, false, false, false,
                                                                  false, false, false, false);
    CHECK(IndexOfMenu(off, kSysMenuMyBots) == -1);

    const std::vector<SystemMenuEntry> on = BuildSystemMenuModel(false, kRecordMenuHidden, NULL, false, false, false,
                                                                 true, false, false, true);
    const int memo = IndexOfMenu(on, kSysMenuMemo);
    const int bots = IndexOfMenu(on, kSysMenuMyBots);
    REQUIRE(memo >= 0);
    REQUIRE(bots == memo + 1);
    CHECK(std::wstring(on[bots].label) == L"我的机器人");
    CHECK(on[bots].iconKind == kIconBot);
    CHECK(on[bots].enabled);
    //图片文字识别排在「我的机器人」后面
    CHECK(IndexOfMenu(on, kSysMenuOcr) == bots + 1);
    //多出的正好两行：「我的机器人」与「图片文字识别」
    CHECK(on.size() == off.size() + 2);
}

TEST_CASE("BOT-C13 群聊气泡发送者的机器人标记经 ResolveGroupSender 保留", "[bot]")
{
    i18n::ResetForTest();
    GroupSenderRender found;
    found.m_name = L"行政助手";
    found.m_isBot = true;
    const GroupSenderRender r = ResolveGroupSender(kBotUserId, &found, RGB(1, 2, 3));
    CHECK(r.m_isBot);
    CHECK(r.m_name == L"行政助手");
    CHECK(r.m_glyph == L"行");

    //成员表里查不到的普通发送者不是机器人
    const GroupSenderRender none = ResolveGroupSender(kPeerUid, NULL, RGB(1, 2, 3));
    CHECK_FALSE(none.m_isBot);
}

TEST_CASE("BOT-C14 机器人删除后按消息自带的名字显示发送者", "[bot]")
{
    i18n::ResetForTest();
    GroupSenderRender out;
    out.m_name = L"旧值";
    //不是机器人消息、或没有名字：不补，out 不变
    CHECK_FALSE(balloonchatwnd::SenderFromBotMark(0, L"值班", out));
    CHECK_FALSE(balloonchatwnd::SenderFromBotMark(kBotId, L"", out));
    CHECK(out.m_name == L"旧值");

    REQUIRE(balloonchatwnd::SenderFromBotMark(kBotId, L"值班", out));
    CHECK(out.m_name == L"值班");
    CHECK(out.m_isBot);
    //字形与底色交给 ResolveGroupSender 回退
    const GroupSenderRender r = ResolveGroupSender(kBotUserId, &out, RGB(1, 2, 3));
    CHECK(r.m_name == L"值班");
    CHECK(r.m_glyph == L"值");
    CHECK(r.m_isBot);
}

TEST_CASE("BOT-C15 机器人行的标签、头像上的字与底色", "[bot]")
{
    i18n::ResetForTest();
    net::GroupBotInfo b;
    b.m_name = u8"IT助手";
    //企业机器人写「企业」；在本群关闭的写「本群已关闭」；停用优先
    CHECK(balloonchatwnd::BotRowTag(b) == L"企业");
    b.m_off = true;
    CHECK(balloonchatwnd::BotRowTag(b) == L"本群已关闭");
    b.m_status = 1;
    CHECK(balloonchatwnd::BotRowTag(b) == L"已停用");
    //本群的群机器人没有标签
    net::GroupBotInfo g;
    g.m_isGroupBot = true;
    g.m_name = u8"值班提醒";
    CHECK(balloonchatwnd::BotRowTag(g).empty());

    //头像上的字：有字形用字形，否则取名称第一个字
    CHECK(balloonchatwnd::BotAvatarText(g) == L"值");
    g.m_glyph = u8"值班";
    CHECK(balloonchatwnd::BotAvatarText(g) == L"值班");
    net::GroupBotInfo empty;
    CHECK(balloonchatwnd::BotAvatarText(empty).empty());

    //底色："#RRGGBB" 解析，格式不对时用默认色
    const COLORREF kFallback = RGB(1, 2, 3);
    CHECK(balloonchatwnd::BotAvatarColor("#1E88E5", kFallback) == RGB(0x1E, 0x88, 0xE5));
    CHECK(balloonchatwnd::BotAvatarColor("", kFallback) == kFallback);
    CHECK(balloonchatwnd::BotAvatarColor("1E88E5", kFallback) == kFallback);
    CHECK(balloonchatwnd::BotAvatarColor("#1E88EZ", kFallback) == kFallback);
}
