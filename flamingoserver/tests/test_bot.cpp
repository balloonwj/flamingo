/**
 *  机器人的单元测试（Suite BOT-S），test_bot.cpp（docs/机器人方案.md 第 12 节）
 *
 *    BOT-S1  机器人身份：企业 / 群机器人建 usertype=2 的账号，个人机器人不建；软删除后账号仍在、已停用
 *    BOT-S2  可见性：全部群、所选的群、其他企业的群、在本群关闭；快照的几张索引
 *    BOT-S3  @ 解析：content.at 的 uid 与剪掉 @ 片段后的正文；@所有人不算 @ 机器人
 *    BOT-S4  关键词：包含、完全相同、以……开头；大小写、全半角、首尾空白归一；「必须 @」
 *    BOT-S5  问答作答：完全相同、相似度过阈值、给出候选、兜底文字
 *    BOT-S6  防循环与防伪：机器人消息、自动回复不触发；删掉客户端带来的 bot 字段
 *    BOT-S9  入群与定时：入群文字；每天、工作日、每周、每月；时间段（含跨零点）
 *    BOT-S10 个人机器人：在线状态（含多设备合并）、发送者范围、冷却、主人正在交谈；自动回复的换算
 *    BOT-S13 规则与机器人的 JSON 往返、校验（字段、上限、归属与动作的搭配、域名白名单）、签名
 *
 *  典型用法：Tests.exe --test-case="BOT-S*"
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/BotDefs.h"
#include "../chatserversrc/BotRules.h"
#include "../chatserversrc/BotManager.h"
#include "../chatserversrc/UserManager.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"

using namespace bot;

namespace {

//本套用例的临时库
const char* const kBotDbPath  = "flamingo_sqlite_bot_test.db";
const char* const kBotMysqlDb = "flamingo_test_bot";

//两个企业、三个群、几个人
const int64_t kEnt = 7101;
const int64_t kOtherEnt = 7102;
const int64_t kGroupA = 268436001LL;
const int64_t kGroupB = 268436002LL;
const int64_t kGroupC = 268436003LL;
const int64_t kOwner = 5301;
const int64_t kSender = 5302;
const int64_t kBotUser = 5401;
const int64_t kTime = 1700000000000LL;
//一分钟的毫秒数
const int64_t kMinute = 60000;

//把 JSON 文本解析成对象
Json::Value ParseObj(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    return root;
}

//造一条规则
BotRule MakeRule(int trigger, int action, const std::string& text)
{
    BotRule r;
    r.m_trigger.m_type = trigger;
    r.m_action.m_type = action;
    r.m_action.m_text = text;
    return r;
}

//造一个机器人
BotRecord MakeBot(int64_t botId, int ownerType, int64_t ownerId, int64_t enterpriseId)
{
    BotRecord b;
    b.m_botId = botId;
    b.m_userId = static_cast<int32_t>(kBotUser + botId);
    b.m_ownerType = ownerType;
    b.m_ownerId = ownerId;
    b.m_enterpriseId = enterpriseId;
    b.m_name = u8"助手";
    b.m_webhookToken = "tok" + std::to_string(static_cast<long long>(botId));
    return b;
}

//造一段带 @ 的 content：text 里在 off 处有 len 字节的 @ 片段指向 uid
std::string AtContent(const std::string& text, int64_t uid, int off, int len)
{
    Json::Value c;
    c["text"] = text;
    Json::Value at;
    at["all"] = 0;
    Json::Value one;
    one["uid"] = std::to_string(static_cast<long long>(uid));
    one["off"] = off;
    one["len"] = len;
    at["users"].append(one);
    c["at"] = at;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, c);
}

//造一个本地时刻
LocalClock Clock(int weekday, int hour, int minute, int day)
{
    LocalClock c;
    c.m_year = 2026;
    c.m_month = 10;
    c.m_day = day;
    c.m_weekday = weekday;
    c.m_minuteOfDay = hour * 60 + minute;
    return c;
}

} // anonymous namespace

TEST_CASE("BOT-S3 @ 解析：uid 与剪掉 @ 片段后的正文")
{
    const std::string at = u8"@助手";
    const std::string text = at + u8" VPN 连不上";
    MessageView v;
    REQUIRE(ParseMessageContent(AtContent(text, kBotUser, 0, static_cast<int>(at.size())), v));
    REQUIRE(v.m_atUsers.size() == 1);
    CHECK(v.m_atUsers[0] == kBotUser);
    CHECK_FALSE(v.m_atAll);
    CHECK(v.m_text == text);
    CHECK(NormalizeText(v.m_textWithoutAt) == u8"vpn 连不上");

    //片段越界时不剪，uid 仍然记下
    MessageView v2;
    REQUIRE(ParseMessageContent(AtContent(u8"短", kBotUser, 0, 100), v2));
    CHECK(v2.m_textWithoutAt == u8"短");
    CHECK(v2.m_atUsers.size() == 1);

    //@所有人：不算 @ 机器人
    MessageView v3;
    REQUIRE(ParseMessageContent("{\"text\":\"hi\",\"at\":{\"all\":1,\"users\":[]}}", v3));
    CHECK(v3.m_atAll);
    CHECK(v3.m_atUsers.empty());
    BotRecord b = MakeBot(1, bot_owner_enterprise, kEnt, kEnt);
    b.m_userId = static_cast<int32_t>(kBotUser);
    b.m_rules.push_back(MakeRule(bot_trigger_at, bot_action_reply, "pong"));
    CHECK(EvaluateGroupMessage(b, v3, Clock(1, 10, 0, 5), NULL, "x").m_kind == bot_decision_none);
    CHECK(EvaluateGroupMessage(b, v, Clock(1, 10, 0, 5), NULL, "x").m_kind == bot_decision_reply);

    //不是 JSON 对象
    MessageView v4;
    CHECK_FALSE(ParseMessageContent("[1,2]", v4));
}

TEST_CASE("BOT-S4 关键词：三种匹配方式、归一化、必须 @")
{
    CHECK(NormalizeText(u8"　ＶＰＮ  Abc ") == "vpn  abc");
    CHECK(MatchAnyKeyword(u8"我的ＶＰＮ又断了", std::vector<std::string>(1, "vpn"), bot_match_contains));
    CHECK_FALSE(MatchAnyKeyword(u8"我的ＶＰＮ又断了", std::vector<std::string>(1, "vpn"), bot_match_exact));
    CHECK(MatchAnyKeyword(u8"  请假  ", std::vector<std::string>(1, u8"请假"), bot_match_exact));
    CHECK(MatchAnyKeyword(u8"报销流程是什么", std::vector<std::string>(1, u8"报销"), bot_match_prefix));
    CHECK_FALSE(MatchAnyKeyword(u8"我想报销", std::vector<std::string>(1, u8"报销"), bot_match_prefix));
    CHECK_FALSE(MatchAnyKeyword("anything", std::vector<std::string>(1, "   "), bot_match_contains));

    BotRecord b = MakeBot(2, bot_owner_group, kGroupA, kEnt);
    b.m_userId = static_cast<int32_t>(kBotUser);
    BotRule kw = MakeRule(bot_trigger_keyword, bot_action_reply, u8"{发送者}，值班电话 1234");
    kw.m_trigger.m_keywords.push_back(u8"值班");
    kw.m_trigger.m_needAt = true;
    b.m_rules.push_back(kw);

    MessageView noAt;
    ParseMessageContent(u8"{\"text\":\"今天谁值班\"}", noAt);
    CHECK(EvaluateGroupMessage(b, noAt, Clock(1, 10, 0, 5), NULL, u8"张三").m_kind == bot_decision_none);

    const std::string at = u8"@值班助手";
    MessageView withAt;
    ParseMessageContent(AtContent(at + u8" 今天谁值班", kBotUser, 0, static_cast<int>(at.size())), withAt);
    BotDecision d = EvaluateGroupMessage(b, withAt, Clock(1, 10, 0, 5), NULL, u8"张三");
    CHECK(d.m_kind == bot_decision_reply);
    CHECK(d.m_text == u8"张三，值班电话 1234");

    //不要求 @ 时，不 @ 也命中；@ 片段本身的文字（「值班助手」）不参与关键词匹配
    b.m_rules[0].m_trigger.m_needAt = false;
    CHECK(EvaluateGroupMessage(b, noAt, Clock(1, 10, 0, 5), NULL, u8"张三").m_kind == bot_decision_reply);
    MessageView onlyAt;
    ParseMessageContent(AtContent(at + u8" 你好", kBotUser, 0, static_cast<int>(at.size())), onlyAt);
    CHECK(EvaluateGroupMessage(b, onlyAt, Clock(1, 10, 0, 5), NULL, u8"张三").m_kind == bot_decision_none);

    //时间段：只在工作日 9:00～18:00 生效
    b.m_rules[0].m_trigger.m_window.m_enabled = true;
    b.m_rules[0].m_trigger.m_window.m_days = bot_days_workdays;
    b.m_rules[0].m_trigger.m_window.m_fromMinute = 9 * 60;
    b.m_rules[0].m_trigger.m_window.m_toMinute = 18 * 60;
    CHECK(EvaluateGroupMessage(b, noAt, Clock(6, 10, 0, 10), NULL, "x").m_kind == bot_decision_none);
    CHECK(EvaluateGroupMessage(b, noAt, Clock(5, 17, 59, 9), NULL, "x").m_kind == bot_decision_reply);
    CHECK(EvaluateGroupMessage(b, noAt, Clock(5, 18, 0, 9), NULL, "x").m_kind == bot_decision_none);
}

TEST_CASE("BOT-S5 问答作答：完全相同、过阈值、候选、兜底")
{
    std::vector<QaItem> qa;
    qa.push_back(QaItem(u8"VPN 连不上怎么办", u8"重启客户端"));
    qa.push_back(QaItem(u8"报销流程", u8"见 OA"));
    qa.push_back(QaItem(u8"年假有几天", u8"按工龄"));

    QaAnswer exact = AnswerFromQa(u8"vpn连不上怎么办？", qa);
    CHECK(exact.m_kind == qa_answer_found);
    CHECK(exact.m_index == 0);
    CHECK(exact.m_score == doctest::Approx(1.0));

    QaAnswer near = AnswerFromQa(u8"VPN 连不上", qa);
    CHECK(near.m_kind == qa_answer_found);
    CHECK(near.m_index == 0);
    CHECK(near.m_score >= kQaAnswerThreshold);

    QaAnswer sugg = AnswerFromQa(u8"VPN", qa);
    CHECK(sugg.m_kind == qa_answer_suggestions);
    REQUIRE_FALSE(sugg.m_suggestions.empty());
    CHECK(sugg.m_suggestions[0] == 0);
    const std::string suggText = BuildQaReplyText(sugg, qa, "");
    CHECK(suggText.find(u8"你可以试试") != std::string::npos);
    CHECK(suggText.find(u8"1. VPN 连不上怎么办") != std::string::npos);

    QaAnswer none = AnswerFromQa(u8"食堂菜单", qa);
    CHECK(none.m_kind == qa_answer_none);
    CHECK(BuildQaReplyText(none, qa, u8"请找行政") == u8"请找行政");
    CHECK(BuildQaReplyText(none, qa, "  ") == u8"没有找到相关问答。");
    CHECK(BuildQaReplyText(exact, qa, "") == std::string(u8"问：VPN 连不上怎么办\n答：重启客户端"));

    //机器人规则：@ 机器人提问、用问答作答
    BotRecord b = MakeBot(3, bot_owner_enterprise, kEnt, kEnt);
    b.m_userId = static_cast<int32_t>(kBotUser);
    b.m_rules.push_back(MakeRule(bot_trigger_at, bot_action_qa, ""));
    const std::string at = u8"@IT 小助手";
    MessageView v;
    ParseMessageContent(AtContent(at + u8" 年假有几天", kBotUser, 0, static_cast<int>(at.size())), v);
    BotDecision d = EvaluateGroupMessage(b, v, Clock(1, 10, 0, 5), &qa, "x");
    CHECK(d.m_kind == bot_decision_reply);
    CHECK(d.m_text == std::string(u8"问：年假有几天\n答：按工龄"));
    //没有问答时回复兜底文字
    CHECK(EvaluateGroupMessage(b, v, Clock(1, 10, 0, 5), NULL, "x").m_text == u8"没有找到相关问答。");
}

TEST_CASE("BOT-S6 防循环与防伪：机器人与自动回复的消息不触发；删掉客户端的 bot 字段")
{
    BotRecord b = MakeBot(4, bot_owner_group, kGroupA, kEnt);
    b.m_userId = static_cast<int32_t>(kBotUser);
    BotRule kw = MakeRule(bot_trigger_keyword, bot_action_reply, "hi");
    kw.m_trigger.m_keywords.push_back("hello");
    kw.m_trigger.m_needAt = false;
    b.m_rules.push_back(kw);

    MessageView fromBot;
    ParseMessageContent("{\"text\":\"hello\",\"bot\":{\"id\":\"9\"}}", fromBot);
    CHECK(fromBot.m_botMarked);
    BotDecision d = EvaluateGroupMessage(b, fromBot, Clock(1, 10, 0, 5), NULL, "x");
    CHECK(d.m_kind == bot_decision_none);
    CHECK(d.m_reason == bot_reason_not_triggerable);

    MessageView fromAuto;
    ParseMessageContent("{\"text\":\"hello\",\"autoreply\":true}", fromAuto);
    CHECK(EvaluateGroupMessage(b, fromAuto, Clock(1, 10, 0, 5), NULL, "x").m_kind == bot_decision_none);

    //机器人自己的回复 content 带 bot 标记，再进判定也不会触发
    MessageView own;
    ParseMessageContent(BuildGroupReplyContent(4, "hello", false, 0, "", "bot4"), own);
    CHECK(own.m_botMarked);
    CHECK(EvaluateGroupMessage(b, own, Clock(1, 10, 0, 5), NULL, "x").m_kind == bot_decision_none);

    std::string forged = "{\"text\":\"hello\",\"bot\":{\"id\":\"9\"}}";
    CHECK(StripBotMarker(forged));
    CHECK_FALSE(ParseObj(forged).isMember("bot"));
    CHECK(ParseObj(forged)["text"].asString() == "hello");
    std::string plain = "{\"text\":\"hello\"}";
    CHECK_FALSE(StripBotMarker(plain));
    CHECK(plain == "{\"text\":\"hello\"}");

    CHECK(IsTriggerableMsgType(message_type_text));
    CHECK(IsTriggerableMsgType(message_type_image));
    CHECK_FALSE(IsTriggerableMsgType(message_type_system));
    CHECK_FALSE(IsTriggerableMsgType(message_type_rtc));
    CHECK_FALSE(IsTriggerableMsgType(message_type_announcement));
}

TEST_CASE("BOT-S16 @ 了别的机器人时本机器人不判定")
{
    std::vector<int64_t> botUsers;
    botUsers.push_back(9001);
    botUsers.push_back(9002);
    MessageView v;
    //没有 @ 任何人
    CHECK_FALSE(MentionsOtherBot(v, 9001, botUsers));
    //@ 了普通成员
    v.m_atUsers.push_back(5302);
    CHECK_FALSE(MentionsOtherBot(v, 9001, botUsers));
    //@ 了别的机器人
    v.m_atUsers.push_back(9002);
    CHECK(MentionsOtherBot(v, 9001, botUsers));
    //同时也 @ 了自己：照常判定
    v.m_atUsers.push_back(9001);
    CHECK_FALSE(MentionsOtherBot(v, 9001, botUsers));
    CHECK_FALSE(MentionsOtherBot(v, 9002, botUsers));
}

TEST_CASE("BOT-S6b 机器人消息的 content：@ 发送者与个人代答的标记")
{
    const std::string c = BuildGroupReplyContent(12, u8"答案", true, kSender, u8"张三", u8"行政助手");
    Json::Value v = ParseObj(c);
    CHECK(v["text"].asString() == u8"@张三 答案");
    CHECK(v["format"].asString() == "markdown");
    CHECK(v["bot"]["id"].asString() == "12");
    //机器人名字随消息保存，机器人删除后历史消息仍能显示
    CHECK(v["bot"]["name"].asString() == u8"行政助手");
    REQUIRE(v["at"]["users"].size() == 1);
    CHECK(v["at"]["users"][0]["uid"].asString() == std::to_string(static_cast<long long>(kSender)));
    CHECK(v["at"]["users"][0]["off"].asInt() == 0);
    CHECK(v["at"]["users"][0]["len"].asInt() == static_cast<int>(std::string(u8"@张三").size()));

    const std::string plain = BuildGroupReplyContent(12, u8"早上好", false, 0, "", u8"行政助手");
    Json::Value p = ParseObj(plain);
    CHECK(p["text"].asString() == u8"早上好");
    CHECK_FALSE(p.isMember("at"));
    CHECK_FALSE(p.isMember("format"));

    BotRecord personal = MakeBot(37, bot_owner_personal, kOwner, kEnt);
    personal.m_name = u8"下班代答";
    Json::Value pv = ParseObj(BuildPersonalReplyContent(personal, u8"我已下班"));
    CHECK(pv["text"].asString() == u8"[机器人] 我已下班");
    CHECK(pv["autoreply"].asBool());
    CHECK(pv["bot"]["kind"].asString() == "personal");
    CHECK(pv["bot"]["name"].asString() == u8"下班代答");
    personal.m_source = bot_source_autoreply;
    Json::Value av = ParseObj(BuildPersonalReplyContent(personal, u8"稍后回复"));
    CHECK(av["text"].asString() == u8"[自动回复] 稍后回复");
    CHECK(av["bot"]["kind"].asString() == "autoreply");
}

TEST_CASE("BOT-S9 入群与定时：入群文字；四种定时；时间段跨零点")
{
    BotRecord b = MakeBot(5, bot_owner_group, kGroupA, kEnt);
    BotRule join = MakeRule(bot_trigger_join, bot_action_reply, u8"欢迎 {新成员}，今天是 {日期}");
    b.m_rules.push_back(join);
    std::vector<std::string> names;
    names.push_back(u8"张三");
    names.push_back(u8"李四");
    BotDecision d = EvaluateJoin(b, Clock(4, 10, 0, 8), names);
    CHECK(d.m_kind == bot_decision_reply);
    CHECK(d.m_text == u8"欢迎 张三、李四，今天是 2026-10-08");

    BotSchedule s;
    s.m_minuteOfDay = 9 * 60;
    s.m_kind = bot_schedule_daily;
    CHECK(ScheduleDue(s, Clock(7, 9, 0, 11)));
    CHECK_FALSE(ScheduleDue(s, Clock(7, 9, 1, 11)));
    s.m_kind = bot_schedule_workdays;
    CHECK(ScheduleDue(s, Clock(5, 9, 0, 9)));
    CHECK_FALSE(ScheduleDue(s, Clock(6, 9, 0, 10)));
    s.m_kind = bot_schedule_weekly;
    s.m_weekdays.push_back(1);
    s.m_weekdays.push_back(3);
    CHECK(ScheduleDue(s, Clock(3, 9, 0, 7)));
    CHECK_FALSE(ScheduleDue(s, Clock(2, 9, 0, 6)));
    s.m_kind = bot_schedule_monthly;
    s.m_monthDay = 8;
    CHECK(ScheduleDue(s, Clock(4, 9, 0, 8)));
    CHECK_FALSE(ScheduleDue(s, Clock(5, 9, 0, 9)));
    CHECK(ScheduleMinuteKey(Clock(4, 9, 5, 8)) == "202610080905");

    BotRule sch = MakeRule(bot_trigger_schedule, bot_action_reply, u8"站会");
    sch.m_trigger.m_schedule.m_kind = bot_schedule_workdays;
    sch.m_trigger.m_schedule.m_minuteOfDay = 9 * 60 + 30;
    b.m_rules.push_back(sch);
    std::vector<size_t> due = DueScheduleRules(b, Clock(1, 9, 30, 5));
    REQUIRE(due.size() == 1);
    CHECK(due[0] == 1);
    CHECK(DueScheduleRules(b, Clock(6, 9, 30, 10)).empty());

    //工作日 18:00～09:00：周五 23:00 在内、周六 03:00 仍属于周五的时间段、周六 23:00 不在内、周一 03:00 不在内
    BotTimeWindow w;
    w.m_enabled = true;
    w.m_days = bot_days_workdays;
    w.m_fromMinute = 18 * 60;
    w.m_toMinute = 9 * 60;
    CHECK(InTimeWindow(w, Clock(5, 23, 0, 9)));
    CHECK(InTimeWindow(w, Clock(6, 3, 0, 10)));
    CHECK_FALSE(InTimeWindow(w, Clock(6, 23, 0, 10)));
    CHECK_FALSE(InTimeWindow(w, Clock(1, 3, 0, 12)));
    CHECK(InTimeWindow(w, Clock(2, 3, 0, 13)));
    CHECK_FALSE(InTimeWindow(w, Clock(2, 12, 0, 13)));
    int minute = 0;
    CHECK(ParseHhMm("09:30", minute));
    CHECK(minute == 570);
    CHECK_FALSE(ParseHhMm("24:00", minute));
    CHECK_FALSE(ParseHhMm("9:3", minute));
    CHECK(FormatHhMm(570) == "09:30");
}

TEST_CASE("BOT-S10 个人机器人：在线状态、发送者范围、冷却、主人正在交谈；自动回复换算")
{
    std::vector<int> devices;
    CHECK(MergePresence(devices) == presence_offline);
    devices.push_back(presence_away);
    devices.push_back(presence_busy);
    CHECK(MergePresence(devices) == presence_busy);
    devices.push_back(presence_online);
    CHECK(MergePresence(devices) == presence_online);

    BotRecord b = MakeBot(6, bot_owner_personal, kOwner, kEnt);
    BotRule r = MakeRule(bot_trigger_single, bot_action_reply, u8"{发送者}您好，{主人}不在");
    r.m_trigger.m_presence.push_back(presence_offline);
    r.m_trigger.m_presence.push_back(presence_away);
    r.m_trigger.m_cooldownMinutes = 30;
    r.m_trigger.m_ownerQuietMinutes = 5;
    b.m_rules.push_back(r);

    PersonalInput in;
    ParseMessageContent(u8"{\"text\":\"在吗\"}", in.m_msg);
    in.m_msgtype = message_type_text;
    in.m_senderId = kSender;
    in.m_senderName = u8"李四";
    in.m_ownerName = u8"张三";
    in.m_ownerPresence = presence_away;
    in.m_clock = Clock(1, 20, 0, 5);
    in.m_nowMs = kTime;
    BotDecision d = EvaluateSingleMessage(b, in);
    CHECK(d.m_kind == bot_decision_reply);
    CHECK(d.m_text == u8"李四您好，张三不在");

    in.m_ownerPresence = presence_online;
    CHECK(EvaluateSingleMessage(b, in).m_reason == bot_reason_no_rule);
    in.m_ownerPresence = presence_away;

    in.m_lastReplyMs = kTime - 10 * kMinute;
    CHECK(EvaluateSingleMessage(b, in).m_reason == bot_reason_cooldown);
    in.m_lastReplyMs = kTime - 31 * kMinute;
    CHECK(EvaluateSingleMessage(b, in).m_kind == bot_decision_reply);

    in.m_ownerLastSpokeMs = kTime - 2 * kMinute;
    CHECK(EvaluateSingleMessage(b, in).m_reason == bot_reason_owner_speaking);
    in.m_ownerLastSpokeMs = 0;

    //发送者范围：指定部门（含下级部门，由调用方给出发送者的部门链）
    b.m_rules[0].m_trigger.m_senders.m_kind = bot_sender_depts;
    b.m_rules[0].m_trigger.m_senders.m_depts.push_back(30);
    in.m_senderDepts.push_back(31);
    CHECK(EvaluateSingleMessage(b, in).m_kind == bot_decision_none);
    in.m_senderDepts.push_back(30);
    CHECK(EvaluateSingleMessage(b, in).m_kind == bot_decision_reply);

    //系统提示、自动回复发来的消息不触发
    in.m_msgtype = message_type_system;
    CHECK(EvaluateSingleMessage(b, in).m_reason == bot_reason_not_triggerable);
    in.m_msgtype = message_type_text;
    ParseMessageContent(u8"{\"text\":\"[自动回复] x\",\"autoreply\":true}", in.m_msg);
    CHECK(EvaluateSingleMessage(b, in).m_reason == bot_reason_not_triggerable);

    //自动回复的换算：两条规则往返一致；没有文字时用默认文字
    AutoReplySettings s;
    s.m_awayOn = true;
    s.m_awayText = u8"出去了";
    s.m_awayDefault = false;
    s.m_busyOn = false;
    s.m_busyText = "";
    s.m_busyDefault = true;
    std::vector<BotRule> rules = AutoReplyToRules(s);
    REQUIRE(rules.size() == 2);
    CHECK(rules[0].m_enabled);
    CHECK_FALSE(rules[1].m_enabled);
    CHECK(rules[1].m_action.m_text == u8"您好，我正在忙，稍后回复您。");
    AutoReplySettings back = RulesToAutoReply(rules);
    CHECK(back.m_awayOn);
    CHECK(back.m_awayText == u8"出去了");
    CHECK_FALSE(back.m_awayDefault);
    CHECK_FALSE(back.m_busyOn);
    CHECK(back.m_busyDefault);

    BotRecord ar = MakeBot(7, bot_owner_personal, kOwner, kEnt);
    ar.m_source = bot_source_autoreply;
    ar.m_rules = rules;
    PersonalInput in2;
    ParseMessageContent(u8"{\"text\":\"在吗\"}", in2.m_msg);
    in2.m_ownerPresence = presence_away;
    in2.m_nowMs = kTime;
    CHECK(EvaluateSingleMessage(ar, in2).m_text == u8"出去了");
    in2.m_ownerPresence = presence_busy;
    CHECK(EvaluateSingleMessage(ar, in2).m_kind == bot_decision_none);
    in2.m_ownerPresence = presence_offline;
    CHECK(EvaluateSingleMessage(ar, in2).m_kind == bot_decision_none);
}

TEST_CASE("BOT-S13 JSON 往返、校验与签名")
{
    BotRecord b = MakeBot(8, bot_owner_enterprise, kEnt, kEnt);
    b.m_scope = bot_scope_selected;
    b.m_groupIds.push_back(kGroupA);
    b.m_color = "#1E88E5";
    BotRule kw = MakeRule(bot_trigger_keyword, bot_action_reply, u8"回复");
    kw.m_name = u8"规则一";
    kw.m_trigger.m_keywords.push_back("vpn");
    kw.m_trigger.m_match = bot_match_prefix;
    kw.m_trigger.m_window.m_enabled = true;
    kw.m_trigger.m_window.m_fromMinute = 600;
    kw.m_trigger.m_window.m_toMinute = 60;
    b.m_rules.push_back(kw);
    BotRule sch = MakeRule(bot_trigger_schedule, bot_action_reply, u8"站会");
    sch.m_trigger.m_schedule.m_kind = bot_schedule_weekly;
    sch.m_trigger.m_schedule.m_minuteOfDay = 570;
    sch.m_trigger.m_schedule.m_weekdays.push_back(2);
    sch.m_trigger.m_scheduleGroups.push_back(kGroupA);
    b.m_rules.push_back(sch);

    Json::Value v = BotToJson(b, true, true);
    CHECK(v["botid"].asString() == "8");
    CHECK(v["webhooktoken"].asString() == "tok8");
    BotRecord back;
    REQUIRE(BotFromJson(v, back));
    CHECK(back.m_botId == 8);
    CHECK(back.m_scope == bot_scope_selected);
    REQUIRE(back.m_groupIds.size() == 1);
    CHECK(back.m_groupIds[0] == kGroupA);
    REQUIRE(back.m_rules.size() == 2);
    CHECK(back.m_rules[0].m_name == u8"规则一");
    CHECK(back.m_rules[0].m_trigger.m_match == bot_match_prefix);
    CHECK(back.m_rules[0].m_trigger.m_window.m_fromMinute == 600);
    CHECK(back.m_rules[0].m_trigger.m_window.m_toMinute == 60);
    CHECK(back.m_rules[1].m_trigger.m_schedule.m_weekdays.size() == 1);
    CHECK(back.m_rules[1].m_trigger.m_scheduleGroups.size() == 1);
    CHECK(back.m_rules[1].m_seq == 1);
    CHECK_FALSE(BotToJson(b, false, false).isMember("rules"));
    CHECK_FALSE(BotToJson(b, false, false).isMember("secret"));

    std::string why;
    std::vector<std::string> domains;
    back.m_ownerType = bot_owner_enterprise;
    CHECK(ValidateBot(back, true, domains, why) == bot_check_ok);

    //字段不合法
    BotRecord bad = back;
    bad.m_name = "   ";
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_name = u8"一二三四五六七八九十一二三四五六七八九十一";
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_color = "red";
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_groupIds.clear();
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_rules[0].m_trigger.m_keywords.clear();
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_rules[0].m_action.m_text = " ";
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    bad.m_rules[1].m_trigger.m_schedule.m_weekdays.clear();
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_invalid);
    bad = back;
    for (int i = 0; i < kBotRuleMax; ++i)
        bad.m_rules.push_back(back.m_rules[0]);
    CHECK(ValidateBot(bad, true, domains, why) == bot_check_full);

    //归属与触发、动作的搭配
    BotRecord personal = MakeBot(9, bot_owner_personal, kOwner, kEnt);
    personal.m_rules.push_back(MakeRule(bot_trigger_at, bot_action_reply, "x"));
    CHECK(ValidateBot(personal, false, domains, why) == bot_check_not_allowed);
    personal.m_rules[0] = MakeRule(bot_trigger_single, bot_action_qa, "x");
    CHECK(ValidateBot(personal, false, domains, why) == bot_check_not_allowed);
    personal.m_rules[0] = MakeRule(bot_trigger_single, bot_action_reply, "x");
    CHECK(ValidateBot(personal, false, domains, why) == bot_check_ok);
    BotRecord group = MakeBot(10, bot_owner_group, kGroupA, kEnt);
    group.m_rules.push_back(MakeRule(bot_trigger_webhook, bot_action_reply, ""));
    CHECK(ValidateBot(group, false, domains, why) == bot_check_not_allowed);
    CHECK(ValidateBot(group, true, domains, why) == bot_check_ok);
    group.m_rules[0] = MakeRule(bot_trigger_join, bot_action_qa, "x");
    CHECK(ValidateBot(group, true, domains, why) == bot_check_not_allowed);

    //外部服务地址要在域名白名单里
    BotRecord ext = MakeBot(11, bot_owner_enterprise, kEnt, kEnt);
    BotRule er = MakeRule(bot_trigger_at, bot_action_external, "");
    er.m_action.m_url = "https://api.example.com/bot";
    ext.m_rules.push_back(er);
    CHECK(ValidateBot(ext, true, domains, why) == bot_check_not_allowed);
    domains.push_back("example.com");
    CHECK(ValidateBot(ext, true, domains, why) == bot_check_ok);
    CHECK(IsUrlAllowed("http://example.com:8080/x", domains));
    CHECK_FALSE(IsUrlAllowed("https://badexample.com/x", domains));
    CHECK_FALSE(IsUrlAllowed("https://user@example.com.evil.org/x", domains));
    CHECK_FALSE(IsUrlAllowed("ftp://example.com/x", domains));
    CHECK(IsValidDomain("oa.example.com"));
    CHECK_FALSE(IsValidDomain(".example.com"));
    CHECK_FALSE(IsValidDomain("localhost"));
    CHECK_FALSE(IsValidDomain("exa mple.com"));

    //签名：HMAC-SHA256(key, timestamp + "\n" + body)。期望值由 Python 的 hmac 模块算出：
    //hmac.new(b'Jefe', b'what do ya\nwant for nothing?', hashlib.sha256).hexdigest()
    CHECK(ComputeSignature("Jefe", "what do ya", "want for nothing?") ==
          "3172aa11c3b638a05d32b6f86b0173837fc43fbcbd7a76f27193f7161305a41e");
    CHECK(RandomHex32().size() == 32);
    CHECK(RandomHex32() != RandomHex32());

    //用法说明
    Json::Value usage = BuildUsageJson(back);
    REQUIRE(usage.size() == 2);
    CHECK(usage[0]["t"].asInt() == bot_trigger_keyword);
    CHECK(usage[1]["time"].asString() == "09:30");
    //关键词规则的「必须 @」默认打开；关掉之后这个机器人没有可被 @ 的规则
    back.m_rules[0].m_trigger.m_needAt = false;
    CHECK_FALSE(IsAtable(back));
    back.m_rules[0].m_trigger.m_needAt = true;
    CHECK(IsAtable(back));
}

TEST_CASE("BOT-S2 可见性：快照索引")
{
    std::vector<BotSnapshot::BotPtr> bots;
    BotRecord all = MakeBot(20, bot_owner_enterprise, kEnt, kEnt);
    all.m_rules.push_back(MakeRule(bot_trigger_at, bot_action_reply, "x"));
    BotRecord sel = MakeBot(21, bot_owner_enterprise, kEnt, kEnt);
    sel.m_scope = bot_scope_selected;
    sel.m_groupIds.push_back(kGroupB);
    BotRecord other = MakeBot(22, bot_owner_enterprise, kOtherEnt, kOtherEnt);
    BotRecord grp = MakeBot(23, bot_owner_group, kGroupA, kEnt);
    BotRecord grpOff = MakeBot(24, bot_owner_group, kGroupA, kEnt);
    grpOff.m_status = bot_status_disabled;
    BotRecord personal = MakeBot(25, bot_owner_personal, kOwner, kEnt);
    personal.m_userId = 0;
    BotRecord deleted = MakeBot(26, bot_owner_group, kGroupA, kEnt);
    deleted.m_status = bot_status_deleted;
    bots.push_back(std::make_shared<BotRecord>(all));
    bots.push_back(std::make_shared<BotRecord>(sel));
    bots.push_back(std::make_shared<BotRecord>(other));
    bots.push_back(std::make_shared<BotRecord>(grp));
    bots.push_back(std::make_shared<BotRecord>(grpOff));
    bots.push_back(std::make_shared<BotRecord>(personal));
    bots.push_back(std::make_shared<BotRecord>(deleted));
    std::map<int64_t, std::set<int64_t> > off;
    off[kGroupC].insert(20);
    std::map<int64_t, std::vector<std::string> > domains;
    domains[kEnt].push_back("example.com");
    BotSnapshot snap;
    snap.build(bots, off, domains);

    std::vector<BotSnapshot::BotPtr> out;
    snap.botsForGroup(kGroupA, kEnt, true, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0]->m_botId == 20);
    CHECK(out[1]->m_botId == 23);
    snap.botsForGroup(kGroupA, kEnt, false, out);
    CHECK(out.size() == 3);
    snap.botsForGroup(kGroupB, kEnt, true, out);
    REQUIRE(out.size() == 2);
    CHECK(out[1]->m_botId == 21);
    snap.botsForGroup(kGroupC, kEnt, true, out);
    CHECK(out.empty());
    snap.botsForGroup(kGroupC, kEnt, false, out);
    CHECK(out.size() == 1);
    CHECK(snap.isOffInGroup(kGroupC, 20));
    snap.botsForGroup(kGroupA, kOtherEnt, true, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0]->m_botId == 22);

    CHECK(snap.groupMayHaveBots(kGroupA, 0));
    CHECK(snap.groupMayHaveBots(kGroupB, kEnt));
    CHECK_FALSE(snap.groupMayHaveBots(kGroupB, 0));
    CHECK(snap.userHasPersonalBot(kOwner));
    CHECK_FALSE(snap.userHasPersonalBot(kSender));
    CHECK(snap.findByUserId(kBotUser + 23)->m_botId == 23);
    CHECK_FALSE(snap.findByUserId(0));
    CHECK(snap.findByWebhookToken("tok21")->m_botId == 21);
    CHECK_FALSE(snap.findByWebhookToken(""));
    CHECK_FALSE(snap.find(26));
    CHECK(snap.domains(kEnt).size() == 1);
    CHECK(snap.domains(kOtherEnt).empty());
    snap.enterpriseBots(kEnt, out);
    CHECK(out.size() == 2);
}

TEST_CASE("BOT-S1 机器人身份与读写：建账号、改名、关闭、软删除、清理")
{
    testdb::resetDb(kBotDbPath, kBotMysqlDb);
    REQUIRE(Singleton<UserManager>::Instance().init());
    BotManager& mgr = Singleton<BotManager>::Instance();
    REQUIRE(mgr.loadFromDb());

    BotRecord ent = MakeBot(0, bot_owner_enterprise, kEnt, kEnt);
    ent.m_name = u8"IT 小助手";
    ent.m_scope = bot_scope_selected;
    ent.m_groupIds.push_back(kGroupA);
    ent.m_groupIds.push_back(kGroupA);
    ent.m_allowGroupOff = true;
    ent.m_webhookToken = RandomHex32();
    ent.m_secret = RandomHex32();
    BotRule r = MakeRule(bot_trigger_at, bot_action_qa, "");
    r.m_name = u8"问答";
    ent.m_rules.push_back(r);
    REQUIRE(mgr.createBot(ent, kTime) == bot_store_ok);
    CHECK(ent.m_botId > 0);
    CHECK(ent.m_userId > 0);
    REQUIRE(ent.m_rules.size() == 1);
    CHECK(ent.m_rules[0].m_id > 0);

    User u;
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUserId(ent.m_userId, u));
    CHECK(u.usertype == user_type_bot);
    CHECK(u.nickname == u8"IT 小助手");

    std::shared_ptr<const BotSnapshot> snap = mgr.snapshot();
    BotSnapshot::BotPtr got = snap->find(ent.m_botId);
    REQUIRE(got);
    CHECK(got->m_groupIds.size() == 1);
    CHECK(got->m_rules.size() == 1);
    CHECK(got->m_rules[0].m_action.m_type == bot_action_qa);
    CHECK(snap->findByWebhookToken(ent.m_webhookToken)->m_botId == ent.m_botId);

    //个人机器人不建账号
    BotRecord personal = MakeBot(0, bot_owner_personal, kOwner, kEnt);
    personal.m_rules.push_back(MakeRule(bot_trigger_single, bot_action_reply, "x"));
    REQUIRE(mgr.createBot(personal, kTime) == bot_store_ok);
    CHECK(personal.m_userId == 0);
    CHECK(mgr.snapshot()->userHasPersonalBot(kOwner));

    //改名同步账号昵称；规则整份替换
    BotRecord upd = *got;
    upd.m_name = u8"IT 助手";
    upd.m_rules.push_back(MakeRule(bot_trigger_keyword, bot_action_reply, "y"));
    upd.m_rules[1].m_trigger.m_keywords.push_back("vpn");
    REQUIRE(mgr.updateBot(upd, kTime + 1) == bot_store_ok);
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUserId(ent.m_userId, u));
    CHECK(u.nickname == u8"IT 助手");
    got = mgr.snapshot()->find(ent.m_botId);
    REQUIRE(got);
    CHECK(got->m_rules.size() == 2);
    CHECK(got->m_rules[0].m_createTime == kTime);

    //在群里关闭、打开
    REQUIRE(mgr.setGroupOff(kGroupA, ent.m_botId, true, kOwner, kTime) == bot_store_ok);
    CHECK(mgr.snapshot()->isOffInGroup(kGroupA, ent.m_botId));
    REQUIRE(mgr.setGroupOff(kGroupA, ent.m_botId, false, kOwner, kTime) == bot_store_ok);
    CHECK_FALSE(mgr.snapshot()->isOffInGroup(kGroupA, ent.m_botId));

    //停用、重置密钥、白名单
    REQUIRE(mgr.setStatus(ent.m_botId, bot_status_disabled, kTime) == bot_store_ok);
    CHECK(mgr.snapshot()->find(ent.m_botId)->m_status == bot_status_disabled);
    std::string token;
    std::string secret;
    REQUIRE(mgr.resetSecrets(ent.m_botId, token, secret) == bot_store_ok);
    CHECK(mgr.snapshot()->findByWebhookToken(token));
    CHECK_FALSE(mgr.snapshot()->findByWebhookToken(ent.m_webhookToken));
    std::vector<std::string> domains;
    domains.push_back("example.com");
    domains.push_back("example.com");
    REQUIRE(mgr.setDomains(kEnt, domains));
    CHECK(mgr.snapshot()->domains(kEnt).size() == 1);

    //最近发消息时间：内存立即可见
    mgr.touchFire(ent.m_botId, kTime + 5);
    CHECK(mgr.lastFireTime(ent.m_botId) == kTime + 5);

    //软删除：快照里没有了，账号仍在且已停用
    REQUIRE(mgr.deleteBot(ent.m_botId, kTime + 2) == bot_store_ok);
    CHECK_FALSE(mgr.snapshot()->find(ent.m_botId));
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUserId(ent.m_userId, u));
    CHECK(u.status == 1);
    CHECK(mgr.deleteBot(ent.m_botId, kTime + 3) == bot_store_not_exist);
    BotRecord loaded;
    REQUIRE(mgr.loadBot(ent.m_botId, loaded));
    CHECK(loaded.m_status == bot_status_deleted);

    //群解散清理群机器人；员工删除清理个人机器人
    BotRecord grp = MakeBot(0, bot_owner_group, kGroupB, kEnt);
    grp.m_rules.push_back(MakeRule(bot_trigger_join, bot_action_reply, "hi"));
    REQUIRE(mgr.createBot(grp, kTime) == bot_store_ok);
    std::vector<BotSnapshot::BotPtr> inB;
    mgr.snapshot()->botsForGroup(kGroupB, 0, true, inB);
    CHECK(inB.size() == 1);
    mgr.onGroupDismissed(kGroupB, kTime + 10);
    mgr.snapshot()->botsForGroup(kGroupB, 0, false, inB);
    CHECK(inB.empty());
    mgr.onUserDeleted(kOwner, kTime + 10);
    CHECK_FALSE(mgr.snapshot()->userHasPersonalBot(kOwner));

    mgr.onEnterpriseDeleted(kEnt);
    CHECK(mgr.snapshot()->domains(kEnt).empty());

    testdb::cleanupDb(kBotDbPath);
}
