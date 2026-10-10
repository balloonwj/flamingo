/**
 *  机器人 DLL 纯逻辑的单元测试（标签 [botdll]，docs/机器人方案.md 第 12 节的客户端部分）：
 *    - BOTD1  2802 / 2804 / 2805 / 2806 / 2808 的请求字段（ID 一律写成字符串）；
 *    - BOTD2  规则写成 JSON 再读回，各字段不变；字段名与服务端 RuleToJson 一致；
 *    - BOTD3  2803 / 2807 的请求只带可编辑字段，新建时 botid 为 "0"；
 *    - BOTD4  2802 应答：机器人对象（协议文档 25.1 的示例）与规则、令牌、密钥；
 *    - BOTD5  2806 应答：allowed、max、bots，并跳过自动回复的预置机器人；
 *    - BOTD6  2809 应答与 2803 / 2805 应答；包体不是 JSON 时按失败处理；
 *    - BOTD7  2809 请求：群机器人带 groupid 与 at，个人机器人带 presence、senderid、time；
 *    - BOTD8  各种归属可用的触发方式与动作（与服务端 TriggerAllowed / ActionAllowed 一致）；
 *    - BOTD9  ValidateBot 与服务端逐项一致：各种不合法的写法给出相同的结果类别，CheckToErrorCode 与 MapCheck 一致；
 *    - BOTD10 规则与界面字段互转：往返不变；只保留当前触发方式用得到的字段；解析失败给出说明；
 *    - BOTD11 NormalizeForm：换了归属或触发方式后调整到合法的组合；
 *    - BOTD12 字符串工具：时刻、关键词拆分、字数、颜色、整数；
 *    - BOTD13 界面文字：错误码、原因、规则摘要、用法说明、试一试结果、变量提示；
 *    - BOTD14 请求登记簿：按 seq 与命令号认领，撤销发起方，清空；
 *    - BOTD20 2802 应答里的 Webhook 对外完整地址（webhookurl，docs/机器人方案.md §11 Q19）：有就取出，没有为空。
 *  框架 Catch2 v3。没有语言包时 TR 返回原文，断言直接比较简体中文。
 *  balloonwj@qq.com   2026-10-08
 */
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../Core/BotCodec.h"
#include "../Core/BotRequestBook.h"
#include "../Core/BotRuleForm.h"
#include "../Core/BotRules.h"
#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../Core/BotTypes.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

using namespace flamingobot;

namespace {

//解析 JSON 文本（测试辅助）
Json::Value ParseJson(const std::string& text)
{
    Json::CharReaderBuilder b;
    std::string errs;
    Json::Value root;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    return root;
}

//一条群机器人的关键词规则
BotRule KeywordRule()
{
    BotRule r;
    r.m_id = 31;
    r.m_name = "VPN";
    r.m_enabled = true;
    r.m_trigger.m_type = bot_trigger_keyword;
    r.m_trigger.m_keywords.push_back("VPN");
    r.m_trigger.m_keywords.push_back(u8"密码");
    r.m_trigger.m_match = bot_match_prefix;
    r.m_trigger.m_needAt = true;
    r.m_action.m_type = bot_action_reply;
    r.m_action.m_text = u8"重启客户端";
    r.m_action.m_markdown = true;
    return r;
}

//一条个人机器人的收到单聊规则
BotRule SingleRule()
{
    BotRule r = MakeDefaultRule(bot_owner_personal);
    r.m_trigger.m_presence.push_back(bot_presence_away);
    r.m_trigger.m_presence.push_back(bot_presence_busy);
    r.m_trigger.m_window.m_enabled = true;
    r.m_trigger.m_window.m_days = bot_days_workdays;
    r.m_trigger.m_window.m_fromMinute = 18 * 60;
    r.m_trigger.m_window.m_toMinute = 9 * 60;
    r.m_trigger.m_senders.m_kind = bot_sender_users;
    r.m_trigger.m_senders.m_users.push_back(5302);
    r.m_action.m_text = u8"我已下班";
    return r;
}

//一个合法的群机器人
BotRecord ValidGroupBot()
{
    BotRecord b;
    b.m_ownerType = bot_owner_group;
    b.m_name = u8"值班提醒";
    b.m_rules.push_back(KeywordRule());
    return b;
}

//一个合法的个人机器人
BotRecord ValidPersonalBot()
{
    BotRecord b;
    b.m_ownerType = bot_owner_personal;
    b.m_name = u8"下班代答";
    b.m_rules.push_back(SingleRule());
    return b;
}

//一个由若干个字组成的 UTF-8 字符串
std::string RepeatChar(const char* ch, int count)
{
    std::string s;
    for (int i = 0; i < count; ++i)
        s += ch;
    return s;
}

} // namespace

TEST_CASE("BOTD1 2802 / 2804 / 2805 / 2806 / 2808 的请求字段", "[botdll]")
{
    Json::Value v = ParseJson(BotCodec::BuildGetDetailRequest(12));
    CHECK(v["botid"].asString() == "12");
    v = ParseJson(BotCodec::BuildDeleteGroupBotRequest(268435456LL, 12));
    CHECK(v["groupid"].asString() == "268435456");
    CHECK(v["botid"].asString() == "12");
    v = ParseJson(BotCodec::BuildSetGroupBotOffRequest(268435456LL, 12, true));
    CHECK(v["off"].asInt() == 1);
    v = ParseJson(BotCodec::BuildSetGroupBotOffRequest(268435456LL, 12, false));
    CHECK(v["off"].asInt() == 0);
    v = ParseJson(BotCodec::BuildGetMyBotsRequest());
    CHECK(v.isObject());
    CHECK(v.size() == 0);
    v = ParseJson(BotCodec::BuildDeleteMyBotRequest(37));
    CHECK(v["botid"].asString() == "37");
}

TEST_CASE("BOTD2 规则写成 JSON 再读回各字段不变", "[botdll]")
{
    BotRule rule = SingleRule();
    rule.m_trigger.m_keywords.push_back(u8"请假");
    const std::string json = BotCodec::RuleToJson(rule);
    const Json::Value v = ParseJson(json);
    //字段名与服务端 RuleToJson / TriggerToJson / ActionToJson 一致
    CHECK(v["id"].isString());
    CHECK(v["trigger"]["type"].asInt() == bot_trigger_single);
    CHECK(v["trigger"]["window"]["from"].asString() == "18:00");
    CHECK(v["trigger"]["window"]["to"].asString() == "09:00");
    CHECK(v["trigger"]["window"]["on"].asInt() == 1);
    CHECK(v["trigger"]["senders"]["users"][0].asString() == "5302");
    CHECK(v["trigger"]["cooldown"].asInt() == kBotDefaultCooldownMinutes);
    CHECK(v["trigger"]["ownerquiet"].asInt() == kBotDefaultOwnerQuietMinutes);
    CHECK(v["trigger"]["schedule"]["time"].isString());
    CHECK(v["action"]["type"].asInt() == bot_action_reply);
    CHECK(v["action"].isMember("defaulttext") == false);

    BotRule back;
    REQUIRE(BotCodec::ParseRuleJson(json, back));
    CHECK(back.m_trigger.m_type == bot_trigger_single);
    CHECK(back.m_trigger.m_keywords.size() == 1);
    CHECK(back.m_trigger.m_presence.size() == 2);
    CHECK(back.m_trigger.m_window.m_enabled);
    CHECK(back.m_trigger.m_window.m_days == bot_days_workdays);
    CHECK(back.m_trigger.m_window.m_fromMinute == 18 * 60);
    CHECK(back.m_trigger.m_window.m_toMinute == 9 * 60);
    CHECK(back.m_trigger.m_senders.m_kind == bot_sender_users);
    REQUIRE(back.m_trigger.m_senders.m_users.size() == 1);
    CHECK(back.m_trigger.m_senders.m_users[0] == 5302);
    CHECK(back.m_action.m_text == u8"我已下班");

    //定时规则：时刻、星期、几号
    BotRule sch;
    sch.m_trigger.m_type = bot_trigger_schedule;
    sch.m_trigger.m_schedule.m_kind = bot_schedule_weekly;
    sch.m_trigger.m_schedule.m_minuteOfDay = 9 * 60 + 30;
    sch.m_trigger.m_schedule.m_weekdays.push_back(1);
    sch.m_trigger.m_schedule.m_weekdays.push_back(3);
    sch.m_action.m_text = "hi";
    REQUIRE(BotCodec::ParseRuleJson(BotCodec::RuleToJson(sch), back));
    CHECK(back.m_trigger.m_schedule.m_kind == bot_schedule_weekly);
    CHECK(back.m_trigger.m_schedule.m_minuteOfDay == 9 * 60 + 30);
    CHECK(back.m_trigger.m_schedule.m_weekdays == sch.m_trigger.m_schedule.m_weekdays);

    //外部系统推送的规则可以没有动作；其余规则没有动作时不认
    CHECK(BotCodec::ParseRuleJson("{\"id\":\"1\",\"trigger\":{\"type\":6}}", back));
    CHECK(back.m_trigger.m_type == bot_trigger_webhook);
    CHECK_FALSE(BotCodec::ParseRuleJson("{\"id\":\"1\",\"trigger\":{\"type\":1}}", back));
    CHECK_FALSE(BotCodec::ParseRuleJson("{\"id\":\"1\",\"trigger\":{\"type\":9},\"action\":{\"type\":1}}", back));
}

TEST_CASE("BOTD3 2803 / 2807 的请求只带可编辑字段", "[botdll]")
{
    BotRecord bot = ValidGroupBot();
    bot.m_desc = u8"简介";
    bot.m_glyph = u8"值";
    bot.m_color = "#43A047";
    bot.m_webhookToken = "secret-token";
    Json::Value v = ParseJson(BotCodec::BuildSaveGroupBotRequest(268435456LL, bot));
    CHECK(v["groupid"].asString() == "268435456");
    const Json::Value& b = v["bot"];
    CHECK(b["botid"].asString() == "0");
    CHECK(b["name"].asString() == u8"值班提醒");
    CHECK(b["desc"].asString() == u8"简介");
    CHECK(b["glyph"].asString() == u8"值");
    CHECK(b["color"].asString() == "#43A047");
    CHECK(b["status"].asInt() == bot_status_active);
    CHECK(b["rules"].size() == 1);
    CHECK_FALSE(b.isMember("webhooktoken"));
    CHECK_FALSE(b.isMember("ownertype"));

    BotRecord mine = ValidPersonalBot();
    mine.m_botId = 37;
    mine.m_status = bot_status_disabled;
    v = ParseJson(BotCodec::BuildSaveMyBotRequest(mine));
    CHECK_FALSE(v.isMember("groupid"));
    CHECK(v["bot"]["botid"].asString() == "37");
    CHECK(v["bot"]["status"].asInt() == bot_status_disabled);
}

TEST_CASE("BOTD4 2802 应答：机器人对象与规则、令牌、密钥", "[botdll]")
{
    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"12\",\"userid\":\"5401\",\"enterpriseid\":\"7001\",\"ownertype\":2,"
        u8"\"ownerid\":\"268435456\",\"name\":\"IT 小助手\",\"desc\":\"IT 支持问答\",\"glyph\":\"IT\",\"color\":\"#1E88E5\","
        u8"\"scope\":1,\"groupids\":[\"268435456\"],\"allowgroupoff\":1,\"status\":0,\"source\":0,\"creatorname\":\"admin\","
        u8"\"createtime\":1791200400000,\"updatetime\":1791200400000,\"lastfire\":0,\"rulecount\":2,"
        u8"\"rules\":[{\"id\":\"31\",\"name\":\"VPN 问题\",\"enabled\":1,\"trigger\":{\"type\":2,\"keywords\":[\"VPN\"],"
        u8"\"match\":1,\"needat\":1,\"schedule\":{\"kind\":2,\"time\":\"09:00\",\"weekdays\":[1],\"monthday\":1},"
        u8"\"groupids\":[],\"presence\":[0,2],\"window\":{\"on\":1,\"days\":1,\"from\":\"18:00\",\"to\":\"09:00\"},"
        u8"\"senders\":{\"kind\":0,\"users\":[],\"depts\":[]},\"cooldown\":30,\"ownerquiet\":5},"
        u8"\"action\":{\"type\":1,\"text\":\"重启\",\"markdown\":0,\"url\":\"\",\"defaulttext\":0}},"
        u8"{\"id\":\"32\",\"trigger\":{\"type\":6}}],"
        u8"\"webhooktoken\":\"0123456789abcdef0123456789abcdef\",\"secret\":\"fedcba9876543210fedcba9876543210\"}}";
    BotReply reply;
    REQUIRE(BotCodec::ParseReply(bot_cmd_get_detail, 7, body, reply));
    CHECK(reply.m_seq == 7);
    CHECK(reply.m_code == 0);
    REQUIRE(reply.m_hasBot);
    const BotRecord& b = reply.m_bot;
    CHECK(b.m_botId == 12);
    CHECK(b.m_userId == 5401);
    CHECK(b.m_ownerType == bot_owner_group);
    CHECK(b.m_ownerId == 268435456LL);
    CHECK(b.m_name == u8"IT 小助手");
    CHECK(b.m_glyph == "IT");
    CHECK(b.m_allowGroupOff);
    CHECK(b.m_createTime == 1791200400000LL);
    REQUIRE(b.m_hasRules);
    REQUIRE(b.m_rules.size() == 2);
    CHECK(b.m_ruleCount == 2);
    CHECK(b.m_rules[0].m_id == 31);
    CHECK(b.m_rules[0].m_trigger.m_keywords.size() == 1);
    CHECK(b.m_rules[0].m_trigger.m_window.m_fromMinute == 18 * 60);
    CHECK(b.m_rules[0].m_trigger.m_cooldownMinutes == 30);
    CHECK(b.m_rules[1].m_trigger.m_type == bot_trigger_webhook);
    CHECK(b.m_webhookToken == "0123456789abcdef0123456789abcdef");
    CHECK(b.m_secret == "fedcba9876543210fedcba9876543210");

    //没有权限：只有 code 与 msg
    REQUIRE(BotCodec::ParseReply(bot_cmd_get_detail, 8, "{\"code\":1402,\"msg\":\"no permission\"}", reply));
    CHECK(reply.m_code == bot_error_no_permission);
    CHECK_FALSE(reply.m_hasBot);
}

TEST_CASE("BOTD20 2802 应答里的 Webhook 对外完整地址", "[botdll]")
{
    const std::string withUrl =
        u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"12\",\"ownertype\":2,\"ownerid\":\"268435456\","
        u8"\"name\":\"值班\",\"rulecount\":0,\"rules\":[],\"webhooktoken\":\"tok\",\"secret\":\"sec\","
        u8"\"webhookurl\":\"https://im.example.com:8443/bot/webhook.do\"}}";
    BotReply reply;
    REQUIRE(BotCodec::ParseReply(bot_cmd_get_detail, 9, withUrl, reply));
    REQUIRE(reply.m_hasBot);
    CHECK(reply.m_bot.m_webhookToken == "tok");
    CHECK(reply.m_bot.m_webhookUrl == "https://im.example.com:8443/bot/webhook.do");

    //服务端没有配置对外地址：没有这个字段
    const std::string noUrl =
        u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"12\",\"ownertype\":2,\"ownerid\":\"268435456\","
        u8"\"name\":\"值班\",\"rulecount\":0,\"rules\":[],\"webhooktoken\":\"tok\",\"secret\":\"sec\"}}";
    REQUIRE(BotCodec::ParseReply(bot_cmd_get_detail, 10, noUrl, reply));
    REQUIRE(reply.m_hasBot);
    CHECK(reply.m_bot.m_webhookUrl.empty());
}

TEST_CASE("BOTD5 2806 应答：allowed、max、bots，跳过自动回复的预置机器人", "[botdll]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"allowed\":1,\"max\":3,\"bots\":["
        "{\"botid\":\"37\",\"ownertype\":3,\"name\":\"a\",\"status\":1,\"source\":0,\"rules\":[]},"
        "{\"botid\":\"38\",\"ownertype\":3,\"name\":\"b\",\"status\":0,\"source\":3,\"rules\":[]}]}";
    BotReply reply;
    REQUIRE(BotCodec::ParseReply(bot_cmd_get_my_bots, 3, body, reply));
    CHECK(reply.m_allowed);
    CHECK(reply.m_max == 3);
    REQUIRE(reply.m_bots.size() == 1);
    CHECK(reply.m_bots[0].m_botId == 37);
    CHECK(reply.m_bots[0].m_status == bot_status_disabled);
    CHECK(reply.m_bots[0].m_hasRules);

    REQUIRE(BotCodec::ParseReply(bot_cmd_get_my_bots, 4, "{\"code\":0,\"allowed\":0,\"bots\":[]}", reply));
    CHECK_FALSE(reply.m_allowed);
    CHECK(reply.m_max == kPersonalBotMax);
}

TEST_CASE("BOTD6 2809、2803、2805 的应答；包体不是 JSON 时按失败处理", "[botdll]")
{
    BotReply reply;
    REQUIRE(BotCodec::ParseReply(bot_cmd_try, 9,
                                 u8"{\"code\":0,\"msg\":\"ok\",\"matched\":1,\"kind\":1,\"ruleid\":\"0\",\"rulename\":\"VPN\","
                                 u8"\"text\":\"@测试 重启\",\"markdown\":1,\"url\":\"\",\"reason\":0}",
                                 reply));
    CHECK(reply.m_try.m_matched);
    CHECK(reply.m_try.m_kind == bot_decision_reply);
    CHECK(reply.m_try.m_ruleName == "VPN");
    CHECK(reply.m_try.m_markdown);
    CHECK(reply.m_try.m_reason == bot_reason_ok);

    REQUIRE(BotCodec::ParseReply(bot_cmd_try, 10, "{\"code\":0,\"matched\":0,\"kind\":0,\"reason\":3}", reply));
    CHECK_FALSE(reply.m_try.m_matched);
    CHECK(reply.m_try.m_reason == bot_reason_owner_speaking);

    REQUIRE(BotCodec::ParseReply(bot_cmd_save_group_bot, 11, "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"88\",\"botid\":\"99\"}",
                                 reply));
    CHECK(reply.m_groupId == 88);
    CHECK(reply.m_botId == 99);

    REQUIRE(BotCodec::ParseReply(bot_cmd_set_group_bot_off, 12,
                                 "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"88\",\"botid\":\"12\",\"off\":1}", reply));
    CHECK(reply.m_off);
    CHECK(reply.m_botId == 12);

    CHECK_FALSE(BotCodec::ParseReply(bot_cmd_save_my_bot, 13, "not json", reply));
    CHECK_FALSE(reply.m_parsed);
    CHECK(reply.m_cmd == bot_cmd_save_my_bot);
    CHECK(reply.m_seq == 13);
    CHECK_FALSE(BotCodec::ParseReply(bot_cmd_save_my_bot, 14, "{\"msg\":\"ok\"}", reply));
}

TEST_CASE("BOTD7 2809 请求：群机器人带 groupid 与 at，个人机器人带 presence", "[botdll]")
{
    BotTryInput input;
    input.m_text = "VPN";
    input.m_at = false;
    Json::Value v = ParseJson(BotCodec::BuildTryRequest(88, ValidGroupBot(), input));
    CHECK(v["groupid"].asString() == "88");
    CHECK(v["bot"]["name"].asString() == u8"值班提醒");
    CHECK(v["input"]["text"].asString() == "VPN");
    CHECK(v["input"]["at"].asInt() == 0);
    CHECK_FALSE(v["input"].isMember("presence"));
    CHECK_FALSE(v.isMember("botid"));

    input.m_presence = bot_presence_busy;
    v = ParseJson(BotCodec::BuildTryRequest(0, ValidPersonalBot(), input));
    CHECK_FALSE(v.isMember("groupid"));
    CHECK(v["input"]["presence"].asInt() == bot_presence_busy);
    CHECK(v["input"]["senderid"].asString() == "0");
    CHECK(v["input"]["time"].asInt64() == 0);
    CHECK_FALSE(v["input"].isMember("at"));
}

TEST_CASE("BOTD8 各种归属可用的触发方式与动作", "[botdll]")
{
    std::vector<int> expected;
    expected.push_back(bot_trigger_single);
    CHECK(AllowedTriggers(bot_owner_personal, true) == expected);
    expected.clear();
    for (int t = bot_trigger_at; t <= bot_trigger_schedule; ++t)
        expected.push_back(t);
    CHECK(AllowedTriggers(bot_owner_group, false) == expected);
    expected.push_back(bot_trigger_webhook);
    CHECK(AllowedTriggers(bot_owner_group, true) == expected);
    CHECK(AllowedTriggers(bot_owner_enterprise, false) == expected);

    std::vector<int> actions;
    actions.push_back(bot_action_reply);
    CHECK(AllowedActions(bot_owner_personal, bot_trigger_single, true) == actions);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_join, true) == actions);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_schedule, true) == actions);
    actions.push_back(bot_action_qa);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_at, false) == actions);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_keyword, false) == actions);
    actions.push_back(bot_action_external);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_at, true) == actions);
    CHECK(AllowedActions(bot_owner_group, bot_trigger_webhook, true).empty());
    CHECK_FALSE(TriggerHasAction(bot_trigger_webhook));
    CHECK(TriggerUsesWindow(bot_trigger_single));
    CHECK_FALSE(TriggerUsesWindow(bot_trigger_schedule));

    //默认规则
    const BotRule personal = MakeDefaultRule(bot_owner_personal);
    CHECK(personal.m_trigger.m_type == bot_trigger_single);
    CHECK(personal.m_trigger.m_cooldownMinutes == kBotDefaultCooldownMinutes);
    CHECK(personal.m_trigger.m_ownerQuietMinutes == kBotDefaultOwnerQuietMinutes);
    CHECK(MakeDefaultRule(bot_owner_group).m_trigger.m_type == bot_trigger_at);
}

TEST_CASE("BOTD9 ValidateBot 与服务端逐项一致", "[botdll]")
{
    std::wstring why;
    CHECK(ValidateBot(ValidGroupBot(), false, &why) == bot_check_ok);
    CHECK(why.empty());
    CHECK(ValidateBot(ValidPersonalBot(), false, &why) == bot_check_ok);

    //基本信息
    BotRecord b = ValidGroupBot();
    b.m_name = "  ";
    CHECK(ValidateBot(b, false, &why) == bot_check_invalid);
    CHECK_FALSE(why.empty());
    b.m_name = RepeatChar(u8"字", kBotNameMaxChars);
    CHECK(ValidateBot(b, false, NULL) == bot_check_ok);
    b.m_name = RepeatChar(u8"字", kBotNameMaxChars + 1);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_desc = RepeatChar(u8"字", kBotDescMaxChars + 1);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_glyph = RepeatChar(u8"字", kBotGlyphMaxChars + 1);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_color = "#12345";
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b.m_color = "#12345G";
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b.m_color = "#abcdef";
    CHECK(ValidateBot(b, false, NULL) == bot_check_ok);
    b.m_status = 2;
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_rules.assign(kBotRuleMax + 1, KeywordRule());
    CHECK(ValidateBot(b, false, NULL) == bot_check_full);

    //规则
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_keywords.clear();
    CHECK(ValidateBot(b, false, &why) == bot_check_invalid);
    CHECK(why.find(L"第 1 条规则") == 0);
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_keywords.assign(kBotKeywordMax + 1, "a");
    CHECK(ValidateBot(b, false, NULL) == bot_check_full);
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_keywords.push_back(" ");
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_match = 4;
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_rules[0].m_name = RepeatChar("a", kBotRuleNameMaxChars + 1);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b = ValidGroupBot();
    b.m_rules[0].m_action.m_text = "  ";
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    b.m_rules[0].m_action.m_text = RepeatChar("a", kBotReplyMaxChars + 1);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);

    //定时
    b = ValidGroupBot();
    BotRule& sch = b.m_rules[0];
    sch.m_trigger.m_type = bot_trigger_schedule;
    sch.m_trigger.m_keywords.clear();
    sch.m_trigger.m_schedule.m_minuteOfDay = -1;
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    sch.m_trigger.m_schedule.m_minuteOfDay = 540;
    CHECK(ValidateBot(b, false, NULL) == bot_check_ok);
    sch.m_trigger.m_schedule.m_kind = bot_schedule_weekly;
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    sch.m_trigger.m_schedule.m_weekdays.push_back(8);
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    sch.m_trigger.m_schedule.m_weekdays[0] = 7;
    CHECK(ValidateBot(b, false, NULL) == bot_check_ok);
    sch.m_trigger.m_schedule.m_kind = bot_schedule_monthly;
    sch.m_trigger.m_schedule.m_monthDay = 32;
    CHECK(ValidateBot(b, false, NULL) == bot_check_invalid);
    //定时只能回复文字
    sch.m_trigger.m_schedule.m_monthDay = 31;
    sch.m_action.m_type = bot_action_qa;
    CHECK(ValidateBot(b, false, NULL) == bot_check_not_allowed);

    //个人机器人的附加条件
    BotRecord p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_presence.push_back(4);
    CHECK(ValidateBot(p, false, NULL) == bot_check_invalid);
    p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_window.m_fromMinute = -1;
    CHECK(ValidateBot(p, false, NULL) == bot_check_invalid);
    p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_senders.m_users.clear();
    CHECK(ValidateBot(p, false, NULL) == bot_check_invalid);
    p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_cooldownMinutes = kBotMinutesMax + 1;
    CHECK(ValidateBot(p, false, NULL) == bot_check_invalid);
    p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_senders.m_users.assign(kBotIdListMax + 1, 1);
    CHECK(ValidateBot(p, false, NULL) == bot_check_full);

    //触发方式与动作不可用
    p = ValidPersonalBot();
    p.m_rules[0].m_trigger.m_type = bot_trigger_at;
    CHECK(ValidateBot(p, true, NULL) == bot_check_not_allowed);
    p = ValidPersonalBot();
    p.m_rules[0].m_action.m_type = bot_action_qa;
    CHECK(ValidateBot(p, true, NULL) == bot_check_not_allowed);
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_type = bot_trigger_webhook;
    CHECK(ValidateBot(b, false, NULL) == bot_check_not_allowed);
    CHECK(ValidateBot(b, true, NULL) == bot_check_ok);
    b = ValidGroupBot();
    b.m_rules[0].m_trigger.m_type = bot_trigger_single;
    CHECK(ValidateBot(b, true, NULL) == bot_check_not_allowed);
    b = ValidGroupBot();
    b.m_rules[0].m_action.m_type = bot_action_external;
    b.m_rules[0].m_action.m_url = "https://hooks.example.com/bot";
    CHECK(ValidateBot(b, false, NULL) == bot_check_not_allowed);
    CHECK(ValidateBot(b, true, NULL) == bot_check_ok);
    b.m_rules[0].m_action.m_url = "ftp://hooks.example.com/bot";
    CHECK(ValidateBot(b, true, NULL) == bot_check_not_allowed);
    b.m_rules[0].m_action.m_url = "https://";
    CHECK(ValidateBot(b, true, NULL) == bot_check_not_allowed);
    //外部服务的兜底文字可以为空
    b.m_rules[0].m_action.m_url = "HTTP://hooks.example.com";
    b.m_rules[0].m_action.m_text.clear();
    CHECK(ValidateBot(b, true, NULL) == bot_check_ok);

    //结果类别换成错误码，与服务端 MapCheck 相同
    CHECK(CheckToErrorCode(bot_check_ok) == 0);
    CHECK(CheckToErrorCode(bot_check_full) == bot_error_full);
    CHECK(CheckToErrorCode(bot_check_not_allowed) == bot_error_disabled);
    CHECK(CheckToErrorCode(bot_check_invalid) == bot_error_invalid);
    CHECK(IsAtable(ValidGroupBot()));
    CHECK_FALSE(IsAtable(ValidPersonalBot()));
}

TEST_CASE("BOTD10 规则与界面字段互转", "[botdll]")
{
    //关键词规则：往返不变
    const BotRule keyword = KeywordRule();
    BotRuleForm form = RuleToForm(keyword);
    CHECK(form.m_trigger == bot_trigger_keyword);
    CHECK(form.m_keywords == L"VPN\r\n密码");
    CHECK(form.m_match == bot_match_prefix);
    CHECK(form.m_markdown);
    BotRule back;
    std::wstring why;
    REQUIRE(FormToRule(form, bot_owner_group, false, back, &why));
    CHECK(BotCodec::RuleToJson(back) == BotCodec::RuleToJson(keyword));

    //个人机器人的收到单聊规则：往返不变
    const BotRule single = SingleRule();
    form = RuleToForm(single);
    CHECK(form.m_presence[bot_presence_away]);
    CHECK(form.m_presence[bot_presence_busy]);
    CHECK_FALSE(form.m_presence[bot_presence_offline]);
    CHECK(form.m_windowFrom == L"18:00");
    CHECK(form.m_cooldown == L"30");
    REQUIRE(FormToRule(form, bot_owner_personal, false, back, &why));
    CHECK(BotCodec::RuleToJson(back) == BotCodec::RuleToJson(single));

    //关键词的多种分隔符、去重；个人机器人不要求 @
    form = RuleToForm(single);
    form.m_keywords = L" 请假，加班;请假 、\n\n出差 ";
    form.m_needAt = true;
    REQUIRE(FormToRule(form, bot_owner_personal, false, back, &why));
    REQUIRE(back.m_trigger.m_keywords.size() == 3);
    CHECK(back.m_trigger.m_keywords[0] == u8"请假");
    CHECK(back.m_trigger.m_keywords[2] == u8"出差");
    CHECK_FALSE(back.m_trigger.m_needAt);

    //只保留当前触发方式用得到的字段：换成定时之后关键词、在线状态、发送者、冷却都不带
    form = RuleToForm(single);
    form.m_trigger = bot_trigger_schedule;
    form.m_scheduleKind = bot_schedule_weekly;
    form.m_scheduleTime = L"8:05";
    form.m_weekdays[0] = true;
    form.m_weekdays[6] = true;
    REQUIRE(FormToRule(form, bot_owner_group, false, back, &why));
    CHECK(back.m_trigger.m_keywords.empty());
    CHECK(back.m_trigger.m_presence.empty());
    CHECK(back.m_trigger.m_senders.m_kind == bot_sender_all);
    CHECK(back.m_trigger.m_cooldownMinutes == 0);
    CHECK_FALSE(back.m_trigger.m_window.m_enabled);
    CHECK(back.m_trigger.m_schedule.m_minuteOfDay == 8 * 60 + 5);
    REQUIRE(back.m_trigger.m_schedule.m_weekdays.size() == 2);
    CHECK(back.m_trigger.m_schedule.m_weekdays[1] == 7);

    //解析失败给出说明
    form.m_scheduleTime = L"25:00";
    CHECK_FALSE(FormToRule(form, bot_owner_group, false, back, &why));
    CHECK(why.find(L"09:00") != std::wstring::npos);
    form = RuleToForm(single);
    form.m_windowTo = L"9点";
    CHECK_FALSE(FormToRule(form, bot_owner_personal, false, back, &why));
    form = RuleToForm(single);
    form.m_cooldown = L"-1";
    CHECK_FALSE(FormToRule(form, bot_owner_personal, false, back, &why));
    form.m_cooldown = L"";
    REQUIRE(FormToRule(form, bot_owner_personal, false, back, &why));
    CHECK(back.m_trigger.m_cooldownMinutes == 0);
    //时间段没有勾选时，格式不对也不拦
    form.m_windowOn = false;
    form.m_windowTo = L"x";
    CHECK(FormToRule(form, bot_owner_personal, false, back, &why));
    //本地校验不通过（回复为空）也返回 false
    form = RuleToForm(single);
    form.m_text = L"   ";
    CHECK_FALSE(FormToRule(form, bot_owner_personal, false, back, &why));
    CHECK_FALSE(why.empty());

    //外部系统推送：不带动作，动作写成回复文字、正文为空
    form = RuleToForm(keyword);
    form.m_trigger = bot_trigger_webhook;
    REQUIRE(FormToRule(form, bot_owner_group, true, back, &why));
    CHECK(back.m_action.m_type == bot_action_reply);
    CHECK(back.m_action.m_text.empty());

    //指定的部门原样带回
    BotRule depts = SingleRule();
    depts.m_trigger.m_senders.m_kind = bot_sender_depts;
    depts.m_trigger.m_senders.m_users.clear();
    depts.m_trigger.m_senders.m_depts.push_back(11);
    REQUIRE(FormToRule(RuleToForm(depts), bot_owner_personal, false, back, &why));
    CHECK(back.m_trigger.m_senders.m_kind == bot_sender_depts);
    CHECK(back.m_trigger.m_senders.m_depts.size() == 1);
}

TEST_CASE("BOTD11 NormalizeForm 调整到合法的组合", "[botdll]")
{
    BotRuleForm form;
    form.m_trigger = bot_trigger_at;
    form.m_action = bot_action_qa;
    form.m_needAt = true;
    NormalizeForm(form, bot_owner_personal, true);
    CHECK(form.m_trigger == bot_trigger_single);
    CHECK(form.m_action == bot_action_reply);
    CHECK_FALSE(form.m_needAt);

    form = BotRuleForm();
    form.m_trigger = bot_trigger_single;
    NormalizeForm(form, bot_owner_group, false);
    CHECK(form.m_trigger == bot_trigger_at);

    form.m_trigger = bot_trigger_join;
    form.m_action = bot_action_qa;
    NormalizeForm(form, bot_owner_group, false);
    CHECK(form.m_action == bot_action_reply);

    form.m_trigger = bot_trigger_webhook;
    NormalizeForm(form, bot_owner_group, false);
    CHECK(form.m_trigger == bot_trigger_at);
    form.m_trigger = bot_trigger_webhook;
    NormalizeForm(form, bot_owner_group, true);
    CHECK(form.m_trigger == bot_trigger_webhook);

    form.m_match = 0;
    form.m_scheduleKind = 9;
    form.m_windowDays = -1;
    form.m_senderKind = 5;
    NormalizeForm(form, bot_owner_group, true);
    CHECK(form.m_match == bot_match_contains);
    CHECK(form.m_scheduleKind == bot_schedule_daily);
    CHECK(form.m_windowDays == bot_days_every);
    CHECK(form.m_senderKind == bot_sender_all);
}

TEST_CASE("BOTD12 字符串工具", "[botdll]")
{
    int minute = -1;
    CHECK(ParseHhMm("09:00", minute));
    CHECK(minute == 540);
    CHECK(ParseHhMm("9:05", minute));
    CHECK(minute == 545);
    CHECK(ParseHhMm("23:59", minute));
    CHECK_FALSE(ParseHhMm("24:00", minute));
    CHECK_FALSE(ParseHhMm("12:60", minute));
    CHECK_FALSE(ParseHhMm("12:5", minute));
    CHECK_FALSE(ParseHhMm(":30", minute));
    CHECK_FALSE(ParseHhMm("ab:cd", minute));
    CHECK_FALSE(ParseHhMm("", minute));
    CHECK(FormatHhMm(0) == "00:00");
    CHECK(FormatHhMm(18 * 60 + 7) == "18:07");
    CHECK(FormatHhMm(-5) == "00:00");

    CHECK(Utf8CharCount(u8"值班提醒") == 4);
    CHECK(Utf8CharCount("abc") == 3);
    CHECK(WideCharCount(L"ab") == 2);
    const wchar_t surrogate[] = { 0xD83D, 0xDE00, 0 };
    CHECK(WideCharCount(surrogate) == 1);
    CHECK(TrimWide(L"\x3000 a b \r\n") == L"a b");
    CHECK(TrimUtf8(u8"  值班  ") == u8"值班");
    CHECK(Utf8ToWide(WideToUtf8(L"群机器人")) == L"群机器人");

    const std::vector<std::string> kws = SplitKeywords(L"VPN，密码;VPN\r\n\r\n 打印 ");
    REQUIRE(kws.size() == 3);
    CHECK(kws[0] == "VPN");
    CHECK(kws[1] == u8"密码");
    CHECK(kws[2] == u8"打印");
    CHECK(JoinKeywords(kws) == L"VPN\r\n密码\r\n打印");
    CHECK(SplitKeywords(L"  ").empty());

    int value = -1;
    CHECK(ParseNonNegativeInt(L" 30 ", value));
    CHECK(value == 30);
    CHECK_FALSE(ParseNonNegativeInt(L"-1", value));
    CHECK_FALSE(ParseNonNegativeInt(L"3a", value));
    CHECK_FALSE(ParseNonNegativeInt(L"", value));
    CHECK_FALSE(ParseNonNegativeInt(L"99999999999", value));

    uint32_t color = 0;
    CHECK(ParseHexColor("#1E88E5", color));
    CHECK(color == 0x00E5881E);
    CHECK(FormatHexColor(color) == "#1E88E5");
    CHECK_FALSE(ParseHexColor("1E88E5", color));
    CHECK_FALSE(ParseHexColor("#1E88E", color));
    CHECK(IsColorValid(""));
    CHECK_FALSE(IsColorValid("#XYZXYZ"));
}

TEST_CASE("BOTD13 界面文字", "[botdll]")
{
    //错误码 1401～1406 各有说明且互不相同
    std::vector<std::wstring> texts;
    for (int code = bot_error_not_exist; code <= bot_error_sensitive; ++code)
    {
        const std::wstring text = ErrorText(code);
        CHECK_FALSE(text.empty());
        for (size_t i = 0; i < texts.size(); ++i)
            CHECK(texts[i] != text);
        texts.push_back(text);
    }
    CHECK(ErrorText(0).empty());
    CHECK(ErrorText(9999).find(L"9999") != std::wstring::npos);
    CHECK_FALSE(NotSentText().empty());

    //原因 0～4 各有说明且互不相同
    texts.clear();
    for (int reason = bot_reason_ok; reason <= bot_reason_not_triggerable; ++reason)
    {
        const std::wstring text = ReasonText(reason);
        CHECK_FALSE(text.empty());
        for (size_t i = 0; i < texts.size(); ++i)
            CHECK(texts[i] != text);
        texts.push_back(text);
    }

    //规则摘要
    std::wstring summary = RuleSummary(KeywordRule(), bot_owner_group);
    CHECK(summary.find(L"VPN") != std::wstring::npos);
    CHECK(summary.find(L"密码") != std::wstring::npos);
    CHECK(summary.find(L"须 @") != std::wstring::npos);
    CHECK(summary.find(L"回复文字") != std::wstring::npos);
    summary = RuleSummary(SingleRule(), bot_owner_personal);
    CHECK(summary.find(L"收到单聊") != std::wstring::npos);
    CHECK(summary.find(L"离开") != std::wstring::npos);
    CHECK(summary.find(L"18:00") != std::wstring::npos);
    CHECK(summary.find(L"指定的 1 人") != std::wstring::npos);
    BotRule webhook;
    webhook.m_trigger.m_type = bot_trigger_webhook;
    CHECK(RuleSummary(webhook, bot_owner_group) == L"外部系统推送");

    //用法说明：每条启用的规则一行，停用的不写
    BotRecord bot = ValidGroupBot();
    BotRule at;
    at.m_trigger.m_type = bot_trigger_at;
    at.m_action.m_type = bot_action_qa;
    bot.m_rules.push_back(at);
    BotRule off = KeywordRule();
    off.m_enabled = false;
    bot.m_rules.push_back(off);
    const std::wstring usage = UsageText(bot);
    CHECK(usage.find(L"发送以「VPN、密码」开头的消息（需要同时 @我）") != std::wstring::npos);
    CHECK(usage.find(L"群问答") != std::wstring::npos);
    CHECK(std::count(usage.begin(), usage.end(), L'\n') == 1);

    //「试一试」的结果
    BotTryResult r;
    r.m_kind = bot_decision_reply;
    r.m_ruleName = "VPN";
    r.m_text = "hello";
    r.m_markdown = true;
    std::wstring text = TryResultText(r);
    CHECK(text.find(L"VPN") != std::wstring::npos);
    CHECK(text.find(L"hello") != std::wstring::npos);
    CHECK(text.find(L"Markdown") != std::wstring::npos);
    r.m_kind = bot_decision_external;
    r.m_url = "https://a.example.com";
    CHECK(TryResultText(r).find(L"https://a.example.com") != std::wstring::npos);
    r.m_kind = bot_decision_none;
    r.m_reason = bot_reason_cooldown;
    CHECK(TryResultText(r).find(ReasonText(bot_reason_cooldown)) != std::wstring::npos);

    //回复文字里可用的变量：只有群里的入群规则有 {新成员}
    CHECK(VariableHint(bot_owner_group, bot_trigger_join).find(L"{新成员}") != std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_at).find(L"{新成员}") == std::wstring::npos);
    CHECK(VariableHint(bot_owner_personal, bot_trigger_single).find(L"{发送者}") != std::wstring::npos);
    //{主人} 只给个人机器人；入群欢迎与定时消息没有发送者
    CHECK(VariableHint(bot_owner_personal, bot_trigger_single).find(L"{主人}") != std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_at).find(L"{主人}") == std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_at).find(L"{发送者}") != std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_schedule).find(L"{发送者}") == std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_join).find(L"{发送者}") == std::wstring::npos);
    CHECK(VariableHint(bot_owner_group, bot_trigger_schedule).find(L"{日期}") != std::wstring::npos);

    //名字列表
    std::vector<std::wstring> names;
    names.push_back(L"张三");
    names.push_back(L"李四");
    names.push_back(L"王五");
    CHECK(JoinNames(names, 5) == L"张三、李四、王五");
    CHECK(JoinNames(names, 2) == L"张三、李四等 3 人");

    //管理窗里的状态文字
    GroupBotItem item;
    item.m_status = bot_status_active;
    item.m_active = true;
    CHECK(GroupBotStateText(item) == L"生效中");
    item.m_off = true;
    item.m_active = false;
    CHECK(GroupBotStateText(item) == L"已在本群关闭");
    item.m_status = bot_status_disabled;
    CHECK(GroupBotStateText(item) == L"已停用");
}

TEST_CASE("BOTD14 请求登记簿", "[botdll]")
{
    CBotRequestBook book;
    book.Add(5, bot_cmd_get_detail, 1);
    book.Add(6, bot_cmd_try, 1);
    book.Add(7, bot_cmd_save_my_bot, 2);
    book.Add(0, bot_cmd_try, 1);
    book.Add(8, bot_cmd_try, 0);
    CHECK(book.Size() == 3);
    CHECK(book.CountOf(1) == 2);

    uint32_t client = 0;
    //命令号对不上的不认领，登记保持不变
    CHECK_FALSE(book.Take(5, bot_cmd_try, client));
    CHECK(book.Size() == 3);
    CHECK(book.Take(5, bot_cmd_get_detail, client));
    CHECK(client == 1);
    CHECK_FALSE(book.Take(5, bot_cmd_get_detail, client));
    CHECK_FALSE(book.Take(99, bot_cmd_try, client));

    CHECK(book.RemoveClient(1) == 1);
    CHECK(book.Size() == 1);
    CHECK(book.Take(7, bot_cmd_save_my_bot, client));
    CHECK(client == 2);
    book.Add(9, bot_cmd_try, 3);
    book.Clear();
    CHECK(book.Size() == 0);
}
