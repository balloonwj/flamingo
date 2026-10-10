/**
 *  机器人引擎、协议与 HTTP 客户端的单元测试（Suite BOT-S 第二部分），test_botengine.cpp（docs/机器人方案.md 第 12 节）
 *
 *    BOT-S7  频率限制：同一会话每分钟第 21 条起丢弃，下一分钟恢复
 *    BOT-S8  报文：2801～2811 的解析与构造、群系统提示的 content
 *    BOT-S11 HTTP：GET 目标解析、URL 与应答报文解析、本机回环的 POST 请求与超时
 *    BOT-S12 呼叫外部服务：请求签名、应答解析、失败与敏感词时的兜底文字、群机器人须企业允许
 *    BOT-S14 引擎：群里应答（开关、帖子群、机器人账号、非文字消息）、问答、个人代答（冷却、主人正在交谈、
 *            自动回复排在后面）、入群、定时（同一分钟只发一次、关闭的群跳过）、Webhook 的各种结果、试一试
 *    BOT-S15 BUG-124：客户端不能发送系统提示；转发时去掉机器人与自动回复的标记
 *    BOT-S17 Webhook 对外完整地址（botwebhookbaseurl，§11 Q19）：拼接规则；2802 详情带密钥且配置了地址时才有 webhookurl
 *
 *  引擎经 IBotEnv 注入假实现（FakeBotEnv），不起线程，直接调用 process* 系列。
 *
 *  典型用法：Tests.exe --test-case="BOT-S*"
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../base/Platform.h"
#include "../chatserversrc/BotDefs.h"
#include "../chatserversrc/BotRules.h"
#include "../chatserversrc/BotManager.h"
#include "../chatserversrc/BotEngine.h"
#include "../chatserversrc/BotProtocol.h"
#include "../chatserversrc/HttpRequestParse.h"
#include "../chatserversrc/FeatureSwitchManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/HttpsClient.h"
#include "../base/Singleton.h"

#ifndef WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace bot;

namespace {

//本套用例的临时库（引擎发消息后会记最近发消息时间，需要一个可写的库）
const char* const kEngDbPath = "flamingo_sqlite_botengine_test.db";
const char* const kEngMysqlDb = "flamingo_test_botengine";

const int64_t kEnt = 7201;
const int64_t kGroupA = 268437001LL;
const int64_t kGroupB = 268437002LL;
const int64_t kThreadGroup = 268437003LL;
const int64_t kOwner = 5501;
const int64_t kSender = 5502;
const int64_t kEntBotUser = 5601;
const int64_t kGroupBotUser = 5602;
//一分钟的毫秒数
const int64_t kMinute = 60000;

//一次发消息的记录
struct Posted
{
    int         m_ctype;
    int64_t     m_from;
    int64_t     m_target;
    std::string m_content;
    std::string m_clientMsgId;
};

//假运行环境：群、人、开关、问答都在内存里，发出的消息记在 m_posted
class FakeBotEnv : public IBotEnv
{
public:
    FakeBotEnv()
        : m_now(1760000000000LL), m_httpOk(true), m_httpStatus(200), m_nextMsgId(1000)
    {
        m_groups[kGroupA] = GroupInfo(kEnt, "A", false);
        m_groups[kGroupB] = GroupInfo(kEnt, "B", false);
        m_groups[kThreadGroup] = GroupInfo(kEnt, "T", true);
        m_names[kOwner] = u8"张三";
        m_names[kSender] = u8"李四";
        m_userEnt[kOwner] = kEnt;
        m_userEnt[kSender] = kEnt;
    }

    int64_t nowMs() override
    {
        return m_now;
    }

    bool groupInfo(int64_t groupid, int64_t& enterpriseId, std::string& name, bool& isThread) override
    {
        std::map<int64_t, GroupInfo>::const_iterator it = m_groups.find(groupid);
        if (it == m_groups.end())
            return false;
        enterpriseId = it->second.m_ent;
        name = it->second.m_name;
        isThread = it->second.m_thread;
        return true;
    }

    int64_t userEnterprise(int64_t userId) override
    {
        std::map<int64_t, int64_t>::const_iterator it = m_userEnt.find(userId);
        return it == m_userEnt.end() ? 0 : it->second;
    }

    std::string displayName(int64_t userId) override
    {
        std::map<int64_t, std::string>::const_iterator it = m_names.find(userId);
        return it == m_names.end() ? std::string() : it->second;
    }

    std::string groupDisplayName(int64_t groupid, int64_t userId) override
    {
        (void)groupid;
        return displayName(userId);
    }

    int presence(int64_t userId) override
    {
        std::map<int64_t, int>::const_iterator it = m_presence.find(userId);
        return it == m_presence.end() ? presence_offline : it->second;
    }

    void userDeptChain(int64_t userId, std::vector<int64_t>& out) override
    {
        out = m_depts[userId];
    }

    void groupQa(int64_t groupid, int64_t enterpriseId, std::vector<QaItem>& out) override
    {
        (void)groupid;
        (void)enterpriseId;
        out = m_qa;
    }

    void enterpriseGroups(int64_t enterpriseId, std::vector<int64_t>& out) override
    {
        out.clear();
        for (std::map<int64_t, GroupInfo>::const_iterator it = m_groups.begin(); it != m_groups.end(); ++it)
        {
            if (it->second.m_ent == enterpriseId)
                out.push_back(it->first);
        }
    }

    bool featureOn(int64_t enterpriseId, const std::string& feature) override
    {
        (void)enterpriseId;
        //默认值与 FeatureSwitchManager 一致：群机器人外部服务默认关闭，其余默认打开
        if (m_off.count(feature) > 0)
            return false;
        if (feature == kFeatureBotGroupExternal)
            return m_on.count(feature) > 0;
        return true;
    }

    int64_t postMessage(int ctype, int64_t from, int64_t target, const std::string& content,
                        const std::string& clientMsgId) override
    {
        for (size_t i = 0; i < m_posted.size(); ++i)
        {
            if (m_posted[i].m_from == from && m_posted[i].m_clientMsgId == clientMsgId)
                return 1;
        }
        Posted p;
        p.m_ctype = ctype;
        p.m_from = from;
        p.m_target = target;
        p.m_content = content;
        p.m_clientMsgId = clientMsgId;
        m_posted.push_back(p);
        return ++m_nextMsgId;
    }

    bool hitSensitive(int64_t enterpriseId, int64_t botUserId, const std::string& botName, int64_t groupid,
                      const std::string& text) override
    {
        (void)enterpriseId;
        (void)botUserId;
        (void)botName;
        (void)groupid;
        return !m_sensitive.empty() && text.find(m_sensitive) != std::string::npos;
    }

    bool httpPost(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
                  const std::string& body, int timeoutMs, int& status, std::string& respBody) override
    {
        (void)timeoutMs;
        m_lastUrl = url;
        m_lastHeaders = headers;
        m_lastBody = body;
        status = m_httpStatus;
        respBody = m_httpResp;
        return m_httpOk;
    }

    //一个群
    struct GroupInfo
    {
        int64_t     m_ent;
        std::string m_name;
        bool        m_thread;

        GroupInfo()
            : m_ent(0), m_thread(false)
        {
        }

        GroupInfo(int64_t ent, const std::string& name, bool thread)
            : m_ent(ent), m_name(name), m_thread(thread)
        {
        }
    };

    int64_t                                       m_now;
    std::map<int64_t, GroupInfo>                  m_groups;
    std::map<int64_t, std::string>                m_names;
    std::map<int64_t, int64_t>                    m_userEnt;
    std::map<int64_t, int>                        m_presence;
    std::map<int64_t, std::vector<int64_t> >      m_depts;
    std::vector<QaItem>                           m_qa;
    std::set<std::string>                         m_off;
    std::set<std::string>                         m_on;
    std::string                                   m_sensitive;
    std::vector<Posted>                           m_posted;
    bool                                          m_httpOk;
    int                                           m_httpStatus;
    std::string                                   m_httpResp;
    std::string                                   m_lastUrl;
    std::vector<std::pair<std::string, std::string> > m_lastHeaders;
    std::string                                   m_lastBody;
    int64_t                                       m_nextMsgId;
};

//解析 JSON
Json::Value Obj(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    return root;
}

//紧凑 JSON
std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, v);
}

//造一条规则
BotRule Rule(int trigger, int action, const std::string& text)
{
    BotRule r;
    r.m_id = 900 + trigger;
    r.m_trigger.m_type = trigger;
    r.m_action.m_type = action;
    r.m_action.m_text = text;
    return r;
}

//装好快照：几个机器人
void InstallBots(const std::vector<BotRecord>& bots, const std::map<int64_t, std::set<int64_t> >& off)
{
    std::vector<BotSnapshot::BotPtr> ptrs;
    for (size_t i = 0; i < bots.size(); ++i)
        ptrs.push_back(std::make_shared<BotRecord>(bots[i]));
    std::shared_ptr<BotSnapshot> snap = std::make_shared<BotSnapshot>();
    snap->build(ptrs, off, std::map<int64_t, std::vector<std::string> >());
    Singleton<BotManager>::Instance().setSnapshotForTest(snap);
}

//企业机器人：应用到全部群，被 @ 时回复
BotRecord EntBot()
{
    BotRecord b;
    b.m_botId = 1;
    b.m_userId = static_cast<int32_t>(kEntBotUser);
    b.m_enterpriseId = kEnt;
    b.m_ownerType = bot_owner_enterprise;
    b.m_ownerId = kEnt;
    b.m_name = u8"IT 小助手";
    b.m_webhookToken = "enttoken";
    b.m_secret = "entsecret";
    b.m_rules.push_back(Rule(bot_trigger_at, bot_action_reply, u8"收到"));
    return b;
}

//群机器人：群 A，关键词「值班」不要求 @
BotRecord GroupBot()
{
    BotRecord b;
    b.m_botId = 2;
    b.m_userId = static_cast<int32_t>(kGroupBotUser);
    b.m_enterpriseId = kEnt;
    b.m_ownerType = bot_owner_group;
    b.m_ownerId = kGroupA;
    b.m_name = u8"值班";
    b.m_webhookToken = "grptoken";
    b.m_secret = "grpsecret";
    BotRule r = Rule(bot_trigger_keyword, bot_action_reply, u8"今天值班：王五");
    r.m_trigger.m_keywords.push_back(u8"值班");
    r.m_trigger.m_needAt = false;
    b.m_rules.push_back(r);
    return b;
}

//一条带 @ 的群消息
BotGroupEvent AtEvent(int64_t groupid, int64_t at, const std::string& text, int64_t msgid)
{
    const std::string atText = u8"@某人";
    Json::Value c;
    c["text"] = atText + " " + text;
    Json::Value one;
    one["uid"] = std::to_string(static_cast<long long>(at));
    one["off"] = 0;
    one["len"] = static_cast<int>(atText.size());
    c["at"]["all"] = 0;
    c["at"]["users"].append(one);
    BotGroupEvent ev;
    ev.m_groupid = groupid;
    ev.m_from = kSender;
    ev.m_msgid = msgid;
    ev.m_msgtype = message_type_text;
    ev.m_content = Compact(c);
    return ev;
}

//一条普通群消息
BotGroupEvent TextEvent(int64_t groupid, const std::string& text, int64_t msgid)
{
    Json::Value c;
    c["text"] = text;
    BotGroupEvent ev;
    ev.m_groupid = groupid;
    ev.m_from = kSender;
    ev.m_msgid = msgid;
    ev.m_msgtype = message_type_text;
    ev.m_content = Compact(c);
    return ev;
}

//本地时间某日某时某分对应的 Unix 毫秒
int64_t LocalMs(int year, int month, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 10;
    t.tm_isdst = -1;
    return static_cast<int64_t>(mktime(&t)) * 1000;
}

//请求头里取一项
std::string Header(const std::vector<std::pair<std::string, std::string> >& headers, const std::string& name)
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (headers[i].first == name)
            return headers[i].second;
    }
    return std::string();
}

//一个测试用例的公共准备：临时库、清空引擎状态、装上假环境
struct EngineFixture
{
    FakeBotEnv m_env;

    EngineFixture()
    {
        testdb::resetDb(kEngDbPath, kEngMysqlDb);
        Singleton<BotEngine>::Instance().setEnvForTest(&m_env);
        Singleton<BotEngine>::Instance().resetStateForTest();
    }

    ~EngineFixture()
    {
        Singleton<BotEngine>::Instance().setEnvForTest(NULL);
        Singleton<BotManager>::Instance().setSnapshotForTest(std::make_shared<BotSnapshot>());
        testdb::cleanupDb(kEngDbPath);
    }
};

} // anonymous namespace

TEST_CASE("BOT-S7 频率限制：每分钟上限、下一分钟恢复")
{
    BotRateLimiter limiter;
    const int64_t t0 = 1760000000000LL - (1760000000000LL % kMinute);
    for (int i = 0; i < 20; ++i)
        CHECK(limiter.allow("k", 20, t0 + i));
    CHECK_FALSE(limiter.allow("k", 20, t0 + 30));
    CHECK(limiter.allow("other", 20, t0 + 30));
    CHECK(limiter.allow("k", 20, t0 + kMinute));
}

TEST_CASE("BOT-S14a 群里应答：@、关键词、开关、帖子群、机器人账号、非文字")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    std::vector<BotRecord> bots;
    bots.push_back(EntBot());
    bots.push_back(GroupBot());
    InstallBots(bots, std::map<int64_t, std::set<int64_t> >());

    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"你好", 11));
    REQUIRE(fx.m_env.m_posted.size() == 1);
    const Posted& p = fx.m_env.m_posted[0];
    CHECK(p.m_ctype == conversation_group);
    CHECK(p.m_from == kEntBotUser);
    CHECK(p.m_target == kGroupA);
    CHECK(p.m_clientMsgId == "bot-1-m11");
    Json::Value c = Obj(p.m_content);
    CHECK(c["text"].asString() == u8"@李四 收到");
    CHECK(c["bot"]["id"].asString() == "1");
    CHECK(c["at"]["users"][0]["uid"].asString() == std::to_string(static_cast<long long>(kSender)));

    //重复处理同一条消息：clientmsgid 相同，不会再发
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"你好", 11));
    CHECK(fx.m_env.m_posted.size() == 1);

    //关键词：只有群 A 的群机器人回复；群 B 没有这个群机器人
    engine.processGroupMessage(TextEvent(kGroupA, u8"今天谁值班", 12));
    REQUIRE(fx.m_env.m_posted.size() == 2);
    CHECK(fx.m_env.m_posted[1].m_from == kGroupBotUser);
    engine.processGroupMessage(TextEvent(kGroupB, u8"今天谁值班", 13));
    CHECK(fx.m_env.m_posted.size() == 2);

    //帖子群、机器人账号发的消息、非文字消息都不触发
    engine.processGroupMessage(AtEvent(kThreadGroup, kEntBotUser, u8"你好", 14));
    BotGroupEvent fromBot = TextEvent(kGroupA, u8"值班", 15);
    fromBot.m_from = kEntBotUser;
    engine.processGroupMessage(fromBot);
    BotGroupEvent image = TextEvent(kGroupA, u8"值班", 16);
    image.m_msgtype = message_type_image;
    engine.processGroupMessage(image);
    CHECK(fx.m_env.m_posted.size() == 2);

    //群机器人开关关闭：群机器人不响应，企业机器人照常
    fx.m_env.m_off.insert(kFeatureBotGroup);
    engine.processGroupMessage(TextEvent(kGroupA, u8"值班", 17));
    CHECK(fx.m_env.m_posted.size() == 2);
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"在吗", 18));
    CHECK(fx.m_env.m_posted.size() == 3);
    //机器人总开关关闭：都不响应
    fx.m_env.m_off.insert(kFeatureBot);
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"在吗", 19));
    CHECK(fx.m_env.m_posted.size() == 3);
}

TEST_CASE("BOT-S14g 消息 @ 了某个机器人时，只让被 @ 的机器人回答")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    std::vector<BotRecord> bots;
    bots.push_back(EntBot());
    bots.push_back(GroupBot());
    InstallBots(bots, std::map<int64_t, std::set<int64_t> >());

    //@ 了企业机器人、正文又含群机器人的关键词「值班」：只有企业机器人回复（2026-10-08 用户确认，§11 Q9）
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"今天谁值班", 31));
    REQUIRE(fx.m_env.m_posted.size() == 1);
    CHECK(fx.m_env.m_posted[0].m_from == kEntBotUser);

    //没有 @ 任何机器人：关键词照常触发
    engine.processGroupMessage(TextEvent(kGroupA, u8"今天谁值班", 32));
    REQUIRE(fx.m_env.m_posted.size() == 2);
    CHECK(fx.m_env.m_posted[1].m_from == kGroupBotUser);

    //@ 的是普通成员（不是机器人）：关键词照常触发
    engine.processGroupMessage(AtEvent(kGroupA, kSender, u8"值班表发一下", 33));
    REQUIRE(fx.m_env.m_posted.size() == 3);
    CHECK(fx.m_env.m_posted[2].m_from == kGroupBotUser);
}

TEST_CASE("BOT-S14b 群里应答：在本群关闭、停用、问答作答、同一会话限流")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord ent = EntBot();
    ent.m_rules[0] = Rule(bot_trigger_at, bot_action_qa, "");
    std::vector<BotRecord> bots;
    bots.push_back(ent);
    std::map<int64_t, std::set<int64_t> > off;
    off[kGroupB].insert(ent.m_botId);
    InstallBots(bots, off);
    fx.m_env.m_qa.push_back(QaItem(u8"年假有几天", u8"按工龄"));

    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"年假有几天", 21));
    REQUIRE(fx.m_env.m_posted.size() == 1);
    CHECK(Obj(fx.m_env.m_posted[0].m_content)["text"].asString() == std::string(u8"@李四 问：年假有几天\n答：按工龄"));
    //群 B 里关闭了：不响应
    engine.processGroupMessage(AtEvent(kGroupB, kEntBotUser, u8"年假有几天", 22));
    CHECK(fx.m_env.m_posted.size() == 1);

    //同一会话每分钟最多 20 条
    for (int i = 0; i < 25; ++i)
        engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"年假有几天", 100 + i));
    CHECK(fx.m_env.m_posted.size() == 20);
    fx.m_env.m_now += kMinute;
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"年假有几天", 200));
    CHECK(fx.m_env.m_posted.size() == 21);
}

TEST_CASE("BOT-S12 呼叫外部服务：签名、应答、兜底、敏感词、群机器人须企业允许")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord ent = EntBot();
    ent.m_rules[0] = Rule(bot_trigger_at, bot_action_external, u8"服务暂时不可用");
    ent.m_rules[0].m_action.m_url = "https://api.example.com/bot";
    BotRecord grp = GroupBot();
    grp.m_rules[0] = Rule(bot_trigger_keyword, bot_action_external, "");
    grp.m_rules[0].m_trigger.m_keywords.push_back("ping");
    grp.m_rules[0].m_trigger.m_needAt = false;
    grp.m_rules[0].m_action.m_url = "https://api.example.com/grp";
    std::vector<BotRecord> bots;
    bots.push_back(ent);
    bots.push_back(grp);
    InstallBots(bots, std::map<int64_t, std::set<int64_t> >());

    fx.m_env.m_httpResp = "{\"text\":\"**pong**\",\"format\":\"markdown\"}";
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 31));
    REQUIRE(fx.m_env.m_posted.size() == 1);
    Json::Value c = Obj(fx.m_env.m_posted[0].m_content);
    CHECK(c["text"].asString() == u8"@李四 **pong**");
    CHECK(c["format"].asString() == "markdown");
    CHECK(fx.m_env.m_lastUrl == "https://api.example.com/bot");
    Json::Value req = Obj(fx.m_env.m_lastBody);
    CHECK(req["text"].asString() == u8"问一下");
    CHECK(req["groupid"].asString() == std::to_string(static_cast<long long>(kGroupA)));
    const std::string ts = Header(fx.m_env.m_lastHeaders, "X-Flamingo-Timestamp");
    CHECK(Header(fx.m_env.m_lastHeaders, "X-Flamingo-Signature") == ComputeSignature("entsecret", ts, fx.m_env.m_lastBody));

    //纯文字应答
    fx.m_env.m_httpResp = "plain answer";
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 32));
    REQUIRE(fx.m_env.m_posted.size() == 2);
    CHECK(Obj(fx.m_env.m_posted[1].m_content)["text"].asString() == u8"@李四 plain answer");

    //失败：回复兜底文字
    fx.m_env.m_httpOk = false;
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 33));
    REQUIRE(fx.m_env.m_posted.size() == 3);
    CHECK(Obj(fx.m_env.m_posted[2].m_content)["text"].asString() == u8"@李四 服务暂时不可用");
    //状态码不是 2xx 也走兜底
    fx.m_env.m_httpOk = true;
    fx.m_env.m_httpStatus = 500;
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 34));
    CHECK(fx.m_env.m_posted.size() == 4);
    //外部应答含敏感词：回复兜底文字
    fx.m_env.m_httpStatus = 200;
    fx.m_env.m_httpResp = "bad word inside";
    fx.m_env.m_sensitive = "bad word";
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 35));
    REQUIRE(fx.m_env.m_posted.size() == 5);
    CHECK(Obj(fx.m_env.m_posted[4].m_content)["text"].asString() == u8"@李四 服务暂时不可用");
    //应答为空：不回复
    fx.m_env.m_sensitive.clear();
    fx.m_env.m_httpResp = "{\"text\":\"\"}";
    engine.processGroupMessage(AtEvent(kGroupA, kEntBotUser, u8"问一下", 36));
    CHECK(fx.m_env.m_posted.size() == 5);

    //群机器人的外部服务：企业没有允许时不调用
    fx.m_env.m_lastUrl.clear();
    fx.m_env.m_httpResp = "pong";
    engine.processGroupMessage(TextEvent(kGroupA, "ping", 37));
    CHECK(fx.m_env.m_lastUrl.empty());
    CHECK(fx.m_env.m_posted.size() == 5);
    fx.m_env.m_on.insert(kFeatureBotGroupExternal);
    engine.processGroupMessage(TextEvent(kGroupA, "ping", 38));
    CHECK(fx.m_env.m_lastUrl == "https://api.example.com/grp");
    CHECK(fx.m_env.m_posted.size() == 6);
}

TEST_CASE("BOT-S14c 个人代答：在线状态、冷却、主人正在交谈、自动回复排在后面、开关")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord personal;
    personal.m_botId = 3;
    personal.m_enterpriseId = kEnt;
    personal.m_ownerType = bot_owner_personal;
    personal.m_ownerId = kOwner;
    personal.m_name = u8"下班代答";
    BotRule r = Rule(bot_trigger_single, bot_action_reply, u8"{发送者}，{主人}已下班");
    r.m_trigger.m_presence.push_back(presence_offline);
    r.m_trigger.m_cooldownMinutes = 30;
    r.m_trigger.m_ownerQuietMinutes = 5;
    personal.m_rules.push_back(r);
    BotRecord autoReply;
    autoReply.m_botId = 4;
    autoReply.m_enterpriseId = kEnt;
    autoReply.m_ownerType = bot_owner_personal;
    autoReply.m_ownerId = kOwner;
    autoReply.m_source = bot_source_autoreply;
    AutoReplySettings s;
    s.m_awayOn = true;
    s.m_awayText = u8"我离开了";
    s.m_busyOn = true;
    s.m_busyText = u8"我在忙";
    autoReply.m_rules = AutoReplyToRules(s);
    std::vector<BotRecord> bots;
    //故意把自动回复放在前面：引擎应先判定员工自己配置的个人机器人
    bots.push_back(autoReply);
    bots.push_back(personal);
    InstallBots(bots, std::map<int64_t, std::set<int64_t> >());

    BotSingleEvent ev;
    ev.m_from = kSender;
    ev.m_to = kOwner;
    ev.m_msgid = 41;
    ev.m_msgtype = message_type_text;
    ev.m_content = u8"{\"text\":\"在吗\"}";
    fx.m_env.m_presence[kOwner] = presence_offline;
    engine.processSingleMessage(ev);
    REQUIRE(fx.m_env.m_posted.size() == 1);
    const Posted& p = fx.m_env.m_posted[0];
    CHECK(p.m_ctype == conversation_single);
    CHECK(p.m_from == kOwner);
    CHECK(p.m_target == kSender);
    Json::Value c = Obj(p.m_content);
    CHECK(c["text"].asString() == u8"[机器人] 李四，张三已下班");
    CHECK(c["autoreply"].asBool());
    CHECK(c["bot"]["kind"].asString() == "personal");

    //冷却中：不再代答，也不轮到自动回复
    ev.m_msgid = 42;
    fx.m_env.m_now += kMinute;
    engine.processSingleMessage(ev);
    CHECK(fx.m_env.m_posted.size() == 1);

    //离开：个人机器人的条件不满足，轮到自动回复
    fx.m_env.m_presence[kOwner] = presence_away;
    ev.m_msgid = 43;
    engine.processSingleMessage(ev);
    REQUIRE(fx.m_env.m_posted.size() == 2);
    CHECK(Obj(fx.m_env.m_posted[1].m_content)["text"].asString() == u8"[自动回复] 我离开了");

    //主人刚在这个会话里发过言：个人机器人不代答
    engine.resetStateForTest();
    fx.m_env.m_presence[kOwner] = presence_offline;
    BotSingleEvent ownerSpeaks;
    ownerSpeaks.m_from = kOwner;
    ownerSpeaks.m_to = kSender;
    ownerSpeaks.m_msgid = 44;
    ownerSpeaks.m_msgtype = message_type_text;
    ownerSpeaks.m_content = u8"{\"text\":\"稍等\"}";
    engine.processSingleMessage(ownerSpeaks);
    ev.m_msgid = 45;
    engine.processSingleMessage(ev);
    CHECK(fx.m_env.m_posted.size() == 2);

    //个人机器人开关关闭：都不代答
    engine.resetStateForTest();
    fx.m_env.m_off.insert(kFeatureBotPersonal);
    ev.m_msgid = 46;
    engine.processSingleMessage(ev);
    CHECK(fx.m_env.m_posted.size() == 2);
}

TEST_CASE("BOT-S14d 入群欢迎与定时消息")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord grp = GroupBot();
    grp.m_rules.clear();
    grp.m_rules.push_back(Rule(bot_trigger_join, bot_action_reply, u8"欢迎 {新成员}"));
    BotRule sch = Rule(bot_trigger_schedule, bot_action_reply, u8"站会 {日期}");
    sch.m_trigger.m_schedule.m_kind = bot_schedule_daily;
    sch.m_trigger.m_schedule.m_minuteOfDay = 9 * 60;
    grp.m_rules.push_back(sch);
    BotRecord ent = EntBot();
    ent.m_rules.clear();
    BotRule entSch = Rule(bot_trigger_schedule, bot_action_reply, u8"周报提醒");
    entSch.m_trigger.m_schedule.m_kind = bot_schedule_daily;
    entSch.m_trigger.m_schedule.m_minuteOfDay = 9 * 60;
    ent.m_rules.push_back(entSch);
    std::vector<BotRecord> bots;
    bots.push_back(ent);
    bots.push_back(grp);
    std::map<int64_t, std::set<int64_t> > off;
    off[kGroupB].insert(ent.m_botId);
    InstallBots(bots, off);

    std::vector<int64_t> members;
    members.push_back(kOwner);
    members.push_back(kSender);
    engine.processJoin(kGroupA, members);
    REQUIRE(fx.m_env.m_posted.size() == 1);
    CHECK(Obj(fx.m_env.m_posted[0].m_content)["text"].asString() == u8"欢迎 张三、李四");
    CHECK_FALSE(Obj(fx.m_env.m_posted[0].m_content).isMember("at"));

    //09:00：群机器人发群 A；企业机器人应用到全部群，但群 B 关闭了、帖子群跳过，只发群 A
    fx.m_env.m_posted.clear();
    const int64_t nine = LocalMs(2026, 10, 8, 9, 0);
    engine.processScheduleTick(nine);
    REQUIRE(fx.m_env.m_posted.size() == 2);
    int toA = 0;
    for (size_t i = 0; i < fx.m_env.m_posted.size(); ++i)
    {
        if (fx.m_env.m_posted[i].m_target == kGroupA)
            ++toA;
    }
    CHECK(toA == 2);
    bool hasDate = false;
    for (size_t i = 0; i < fx.m_env.m_posted.size(); ++i)
    {
        if (Obj(fx.m_env.m_posted[i].m_content)["text"].asString() == u8"站会 2026-10-08")
            hasDate = true;
    }
    CHECK(hasDate);
    //同一分钟再检查一次：不重复发
    engine.processScheduleTick(nine + 20000);
    CHECK(fx.m_env.m_posted.size() == 2);
    //换一分钟：没到点，不发
    engine.processScheduleTick(nine + kMinute);
    CHECK(fx.m_env.m_posted.size() == 2);
}

TEST_CASE("BOT-S14e Webhook：成功与各种失败")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord ent = EntBot();
    ent.m_scope = bot_scope_selected;
    ent.m_groupIds.push_back(kGroupA);
    ent.m_rules.push_back(Rule(bot_trigger_webhook, bot_action_reply, ""));
    BotRecord grp = GroupBot();
    grp.m_rules.push_back(Rule(bot_trigger_webhook, bot_action_reply, ""));
    BotRecord noHook = GroupBot();
    noHook.m_botId = 9;
    noHook.m_userId = 5609;
    noHook.m_webhookToken = "nohook";
    std::vector<BotRecord> bots;
    bots.push_back(ent);
    bots.push_back(grp);
    bots.push_back(noHook);
    InstallBots(bots, std::map<int64_t, std::set<int64_t> >());

    std::string resp;
    CHECK(engine.handleWebhook("not json", resp) == bot_webhook_bad_request);
    CHECK(engine.handleWebhook("{\"token\":\"xx\",\"text\":\"hi\"}", resp) == bot_webhook_unauthorized);
    CHECK(engine.handleWebhook("{\"token\":\"nohook\",\"text\":\"hi\"}", resp) == bot_webhook_forbidden);

    //企业机器人只应用到群 A：不指定群时发群 A
    CHECK(engine.handleWebhook(u8"{\"token\":\"enttoken\",\"text\":\"部署完成\",\"msgtype\":\"markdown\"}", resp)
          == bot_webhook_ok);
    REQUIRE(fx.m_env.m_posted.size() == 1);
    CHECK(fx.m_env.m_posted[0].m_target == kGroupA);
    CHECK(Obj(fx.m_env.m_posted[0].m_content)["format"].asString() == "markdown");
    CHECK(Obj(resp)["msgid"].asString().size() > 0);
    //指定了不在范围内的群
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"x\",\"groupid\":\"" +
                                   std::to_string(static_cast<long long>(kGroupB)) + "\"}",
                               resp) == bot_webhook_bad_group);
    //正文为空
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"  \"}", resp) == bot_webhook_bad_request);
    //签名：正确的通过，错的与过期的拒绝
    const std::string ts = std::to_string(static_cast<long long>(fx.m_env.m_now));
    const std::string sign = ComputeSignature("entsecret", ts, "signed");
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"signed\",\"timestamp\":\"" + ts + "\",\"sign\":\"" + sign + "\"}",
                               resp) == bot_webhook_ok);
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"signed\",\"timestamp\":\"" + ts + "\",\"sign\":\"bad\"}",
                               resp) == bot_webhook_unauthorized);
    const std::string old = std::to_string(static_cast<long long>(fx.m_env.m_now - 10 * kMinute));
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"signed\",\"timestamp\":\"" + old + "\",\"sign\":\"" +
                                   ComputeSignature("entsecret", old, "signed") + "\"}",
                               resp) == bot_webhook_unauthorized);
    //敏感词
    fx.m_env.m_sensitive = "secret";
    CHECK(engine.handleWebhook("{\"token\":\"enttoken\",\"text\":\"top secret\"}", resp) == bot_webhook_sensitive);
    fx.m_env.m_sensitive.clear();
    //群机器人的 Webhook 须企业允许外部服务
    CHECK(engine.handleWebhook("{\"token\":\"grptoken\",\"text\":\"x\"}", resp) == bot_webhook_forbidden);
    fx.m_env.m_on.insert(kFeatureBotGroupExternal);
    CHECK(engine.handleWebhook("{\"token\":\"grptoken\",\"text\":\"x\"}", resp) == bot_webhook_ok);
    //每分钟 20 条
    int ok = 0;
    for (int i = 0; i < 25; ++i)
    {
        if (engine.handleWebhook("{\"token\":\"grptoken\",\"text\":\"n" + std::to_string(i) + "\"}", resp) == bot_webhook_ok)
            ++ok;
    }
    CHECK(ok == 19);
    CHECK(Obj(resp)["code"].asInt() == bot_webhook_rate_limited);
}

TEST_CASE("BOT-S14f 试一试：未保存的规则、个人机器人")
{
    EngineFixture fx;
    BotEngine& engine = Singleton<BotEngine>::Instance();
    BotRecord draft;
    draft.m_ownerType = bot_owner_group;
    draft.m_ownerId = kGroupA;
    draft.m_enterpriseId = kEnt;
    BotRule r = Rule(bot_trigger_at, bot_action_qa, "");
    draft.m_rules.push_back(r);
    fx.m_env.m_qa.push_back(QaItem(u8"报销流程", u8"见 OA"));
    BotTryInput in;
    in.m_text = u8"报销流程";
    in.m_at = true;
    in.m_groupid = kGroupA;
    BotDecision d = engine.tryBot(draft, in);
    CHECK(d.m_kind == bot_decision_reply);
    CHECK(d.m_text == std::string(u8"问：报销流程\n答：见 OA"));
    in.m_at = false;
    CHECK(engine.tryBot(draft, in).m_kind == bot_decision_none);
    CHECK(fx.m_env.m_posted.empty());

    BotRecord personal;
    personal.m_ownerType = bot_owner_personal;
    personal.m_ownerId = kOwner;
    BotRule pr = Rule(bot_trigger_single, bot_action_reply, u8"{主人}不在");
    pr.m_trigger.m_presence.push_back(presence_busy);
    personal.m_rules.push_back(pr);
    BotTryInput pin;
    pin.m_text = u8"在吗";
    pin.m_presence = presence_busy;
    CHECK(engine.tryBot(personal, pin).m_text == u8"张三不在");
    pin.m_presence = presence_online;
    CHECK(engine.tryBot(personal, pin).m_reason == bot_reason_no_rule);
}

TEST_CASE("BOT-S8 报文：请求解析与应答构造")
{
    int64_t groupid = 0;
    BotRecord rec;
    REQUIRE(botproto::ParseSaveGroupBotRequest(
        "{\"groupid\":\"268437001\",\"bot\":{\"botid\":\"0\",\"name\":\"n\",\"rules\":[{\"id\":\"0\",\"name\":\"r\","
        "\"enabled\":1,\"trigger\":{\"type\":1},\"action\":{\"type\":1,\"text\":\"hi\"}}]}}",
        groupid, rec));
    CHECK(groupid == kGroupA);
    CHECK(rec.m_name == "n");
    REQUIRE(rec.m_rules.size() == 1);
    CHECK(rec.m_rules[0].m_action.m_text == "hi");
    CHECK_FALSE(botproto::ParseSaveGroupBotRequest("{\"bot\":{}}", groupid, rec));
    CHECK_FALSE(botproto::ParseSaveGroupBotRequest(
        "{\"groupid\":\"1\",\"bot\":{\"rules\":[{\"trigger\":{\"type\":99}}]}}", groupid, rec));

    int64_t botId = 0;
    bool off = false;
    CHECK(botproto::ParseSetGroupBotOffRequest("{\"groupid\":\"5\",\"botid\":\"7\",\"off\":1}", groupid, botId, off));
    CHECK(off);
    CHECK(botId == 7);
    CHECK_FALSE(botproto::ParseBotIdRequest("{\"botid\":\"0\"}", botId));

    botproto::TryRequest tr;
    REQUIRE(botproto::ParseTryRequest("{\"botid\":\"3\",\"input\":{\"text\":\"hi\",\"at\":0,\"presence\":2}}", tr));
    CHECK(tr.m_botId == 3);
    CHECK_FALSE(tr.m_input.m_at);
    CHECK(tr.m_input.m_presence == 2);
    CHECK_FALSE(botproto::ParseTryRequest("{\"input\":{\"text\":\"hi\"}}", tr));

    AutoReplySettings s;
    REQUIRE(botproto::ParseAutoReplyRequest("{\"awayon\":1,\"awaytext\":\"a\",\"awaydefault\":0,\"busyon\":0}", s));
    CHECK(s.m_awayOn);
    CHECK_FALSE(s.m_awayDefault);
    CHECK(s.m_busyDefault);
    std::string longText(bot::kAutoReplyTextMaxChars + 1, 'x');
    CHECK_FALSE(botproto::ParseAutoReplyRequest("{\"awaytext\":\"" + longText + "\"}", s));

    //2801 应答
    BotRecord ent = EntBot();
    ent.m_allowGroupOff = true;
    botproto::GroupBotView v;
    v.m_bot = std::make_shared<BotRecord>(ent);
    v.m_active = true;
    v.m_canOff = true;
    std::vector<botproto::GroupBotView> views(1, v);
    botproto::GroupBotCaps caps;
    caps.m_isOwner = true;
    caps.m_groupBotAllowed = true;
    caps.m_groupBotMax = bot::kGroupBotMax;
    Json::Value list = Obj(botproto::BuildGroupBotsResponse(0, "ok", kGroupA, caps, views));
    CHECK(list["caps"]["isowner"].asInt() == 1);
    REQUIRE(list["bots"].size() == 1);
    CHECK(list["bots"][0]["source"].asString() == "enterprise");
    CHECK(list["bots"][0]["atable"].asInt() == 1);
    CHECK(list["bots"][0]["userid"].asString() == std::to_string(static_cast<long long>(kEntBotUser)));
    CHECK_FALSE(list["bots"][0].isMember("secret"));
    CHECK(list["bots"][0]["usage"].size() == 1);

    //群系统提示
    Json::Value tip = Obj(botproto::BuildBotTipContent(kSystemTipBotAdded, u8"张三", u8"值班", true));
    CHECK(tip["tipkind"].asString() == "bot_added");
    CHECK(tip["tipargs"]["external"].asString() == "1");
    CHECK(tip["text"].asString().find(u8"外部服务") != std::string::npos);
    Json::Value tip2 = Obj(botproto::BuildBotTipContent(kSystemTipBotOff, "", u8"值班", false));
    CHECK_FALSE(tip2["tipargs"].isMember("operator"));
    CHECK(tip2["text"].asString() == u8"群主 在本群关闭了机器人「值班」");

    std::vector<std::string> texts;
    botproto::CollectTexts(GroupBot(), texts);
    CHECK(texts.size() == 3);
}

TEST_CASE("BOT-S17 Webhook 对外完整地址")
{
    //没有配置、只有空白或斜杠：不下发
    CHECK(BuildWebhookUrl("").empty());
    CHECK(BuildWebhookUrl("   ").empty());
    CHECK(BuildWebhookUrl("/").empty());
    //末尾带与不带斜杠、带端口、首尾空白
    CHECK(BuildWebhookUrl("https://im.example.com") == "https://im.example.com/bot/webhook.do");
    CHECK(BuildWebhookUrl("https://im.example.com/") == "https://im.example.com/bot/webhook.do");
    CHECK(BuildWebhookUrl("http://10.0.0.5:12345//") == "http://10.0.0.5:12345/bot/webhook.do");
    CHECK(BuildWebhookUrl("  https://im.example.com:8443  ") == "https://im.example.com:8443/bot/webhook.do");
    //反向代理的路径前缀保留
    CHECK(BuildWebhookUrl("https://example.com/im/") == "https://example.com/im/bot/webhook.do");

    //2802 详情：带密钥且配置了地址时才有 webhookurl
    const BotRecord b = GroupBot();
    const std::string url = BuildWebhookUrl("https://im.example.com");
    Json::Value withUrl = Obj(botproto::BuildBotDetailResponse(0, "ok", &b, true, url));
    CHECK(withUrl["bot"]["webhooktoken"].asString() == "grptoken");
    CHECK(withUrl["bot"]["webhookurl"].asString() == url);
    Json::Value noSecrets = Obj(botproto::BuildBotDetailResponse(0, "ok", &b, false, url));
    CHECK_FALSE(noSecrets["bot"].isMember("webhooktoken"));
    CHECK_FALSE(noSecrets["bot"].isMember("webhookurl"));
    Json::Value noUrl = Obj(botproto::BuildBotDetailResponse(0, "ok", &b, true, std::string()));
    CHECK(noUrl["bot"]["webhooktoken"].asString() == "grptoken");
    CHECK_FALSE(noUrl["bot"].isMember("webhookurl"));
}

TEST_CASE("BOT-S11 GET 目标解析、URL 与应答报文解析")
{
    std::string url;
    std::string param;
    CHECK(httpparse::ParseGetTarget("/register.do?p=abc", url, param));
    CHECK(url == "/register.do");
    CHECK(param == "abc");
    CHECK(httpparse::ParseGetTarget("/x?p=", url, param));
    CHECK(param.empty());
    //查询串只有一个字符的请求：原先对它 substr(2) 会抛出异常，异常逸出 I/O 线程后整个 chatserver 退出
    CHECK_FALSE(httpparse::ParseGetTarget("/x?a", url, param));
    CHECK_FALSE(httpparse::ParseGetTarget("/x?", url, param));
    CHECK_FALSE(httpparse::ParseGetTarget("/x", url, param));
    CHECK_FALSE(httpparse::ParseGetTarget("/x?q=1", url, param));
    CHECK_FALSE(httpparse::ParseGetTarget("?p=1", url, param));

    httpclient::ParsedUrl u;
    REQUIRE(httpclient::ParseUrl("https://API.example.com:8443/bot?x=1#frag", u));
    CHECK(u.m_https);
    CHECK(u.m_host == "API.example.com");
    CHECK(u.m_port == 8443);
    CHECK(u.m_path == "/bot?x=1");
    REQUIRE(httpclient::ParseUrl("http://127.0.0.1", u));
    CHECK(u.m_port == 80);
    CHECK(u.m_path == "/");
    CHECK_FALSE(httpclient::ParseUrl("ftp://x/y", u));
    CHECK_FALSE(httpclient::ParseUrl("http://:80/", u));
    CHECK_FALSE(httpclient::ParseUrl("http://a@b/", u));
    CHECK_FALSE(httpclient::ParseUrl("http://host:99999/", u));

    int status = 0;
    std::string body;
    bool complete = false;
    CHECK_FALSE(httpclient::ParseResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n", status, body, complete));
    REQUIRE(httpclient::ParseResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel", status, body, complete));
    CHECK_FALSE(complete);
    REQUIRE(httpclient::ParseResponse("HTTP/1.1 201 Created\r\ncontent-length: 5\r\n\r\nhelloXX", status, body, complete));
    CHECK(complete);
    CHECK(status == 201);
    CHECK(body == "hello");
    REQUIRE(httpclient::ParseResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n", status, body,
        complete));
    CHECK(complete);
    CHECK(body == "Wikipedia");
    REQUIRE(httpclient::ParseResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWi", status, body, complete));
    CHECK_FALSE(complete);
    REQUIRE(httpclient::ParseResponse("HTTP/1.0 204 No Content\r\n\r\n", status, body, complete));
    CHECK(complete);
    CHECK_FALSE(httpclient::ParseResponse("garbage\r\n\r\n", status, body, complete));
}

namespace {

#ifdef WIN32
typedef SOCKET TestSock;
const TestSock kBadSock = INVALID_SOCKET;
void CloseTestSock(TestSock s)
{
    closesocket(s);
}
#else
typedef int TestSock;
const TestSock kBadSock = -1;
void CloseTestSock(TestSock s)
{
    close(s);
}
#endif

//本机回环上的一次性 HTTP 服务：接一个连接，读完请求，按 m_reply 应答（m_reply 为空时不应答，用于测超时）
struct OneShotServer
{
    TestSock    m_listen;
    int         m_port;
    std::string m_reply;
    std::string m_request;

    OneShotServer()
        : m_listen(kBadSock), m_port(0)
    {
#ifdef WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        m_listen = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bind(m_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(m_listen, 1);
        socklen_t len = sizeof(addr);
        getsockname(m_listen, reinterpret_cast<sockaddr*>(&addr), &len);
        m_port = ntohs(addr.sin_port);
    }

    ~OneShotServer()
    {
        CloseTestSock(m_listen);
    }

    //在线程里运行：接一个连接、读到请求完整（头部加 Content-Length）、应答后关闭
    void serveOnce()
    {
        TestSock c = accept(m_listen, NULL, NULL);
        if (c == kBadSock)
            return;
        char buf[4096];
        while (true)
        {
            const int n = static_cast<int>(recv(c, buf, sizeof(buf), 0));
            if (n <= 0)
                break;
            m_request.append(buf, static_cast<size_t>(n));
            const size_t headEnd = m_request.find("\r\n\r\n");
            if (headEnd == std::string::npos)
                continue;
            const size_t cl = m_request.find("Content-Length: ");
            const size_t want = cl == std::string::npos ? 0 : static_cast<size_t>(atoi(m_request.c_str() + cl + 16));
            if (m_request.size() >= headEnd + 4 + want)
                break;
        }
        if (!m_reply.empty())
        {
            send(c, m_reply.data(), static_cast<int>(m_reply.size()), 0);
        }
        else
        {
            //不应答：等客户端超时断开
            while (recv(c, buf, sizeof(buf), 0) > 0)
            {
            }
        }
        CloseTestSock(c);
    }
};

} // anonymous namespace

TEST_CASE("BOT-S11b HTTP 客户端：本机回环的 POST 与超时")
{
    OneShotServer server;
    server.m_reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"text\":\"pong\"}";
    std::thread t(std::bind(&OneShotServer::serveOnce, &server));
    std::vector<std::pair<std::string, std::string> > headers;
    headers.push_back(std::make_pair(std::string("X-Flamingo-Signature"), std::string("abc")));
    httpclient::HttpResponse resp;
    const bool ok = httpclient::Post("http://127.0.0.1:" + std::to_string(server.m_port) + "/hook?a=1", headers,
                                     "{\"text\":\"ping\"}", 3000, 65536, resp);
    t.join();
    CHECK(ok);
    CHECK(resp.m_status == 200);
    CHECK(resp.m_body == "{\"text\":\"pong\"}");
    CHECK(server.m_request.find("POST /hook?a=1 HTTP/1.1") == 0);
    CHECK(server.m_request.find("X-Flamingo-Signature: abc") != std::string::npos);
    CHECK(server.m_request.find("{\"text\":\"ping\"}") != std::string::npos);

    //应答体超过上限：截断
    OneShotServer big;
    big.m_reply = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
    std::thread t2(std::bind(&OneShotServer::serveOnce, &big));
    httpclient::HttpResponse resp2;
    CHECK(httpclient::Post("http://127.0.0.1:" + std::to_string(big.m_port) + "/", headers, "x", 3000, 4, resp2));
    t2.join();
    CHECK(resp2.m_body == "0123");

    //对方不应答：超时返回 false
    OneShotServer silent;
    std::thread t3(std::bind(&OneShotServer::serveOnce, &silent));
    httpclient::HttpResponse resp3;
    CHECK_FALSE(httpclient::Post("http://127.0.0.1:" + std::to_string(silent.m_port) + "/", headers, "x", 300, 65536, resp3));
    t3.join();

    //地址不合法、连不上
    httpclient::HttpResponse resp4;
    CHECK_FALSE(httpclient::Post("not a url", headers, "x", 300, 65536, resp4));
}

TEST_CASE("BOT-S15 BUG-124：客户端不能发送系统提示；转发时去掉机器人与自动回复标记")
{
    CHECK_FALSE(MessageProtocol::isClientSendableMsgType(message_type_system));
    CHECK_FALSE(MessageProtocol::isClientSendableMsgType(message_type_fileshare));
    CHECK(MessageProtocol::isClientSendableMsgType(message_type_text));
    CHECK(MessageProtocol::isClientSendableMsgType(message_type_image));
    //原有的判断不变：系统提示仍算「可操作」的类型（撤回、置顶的既有行为不受影响）
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_system));

    const std::string fwd = MessageProtocol::buildForwardedContent(
        message_type_text, u8"{\"text\":\"[机器人] x\",\"autoreply\":true,\"bot\":{\"id\":\"3\"},\"format\":\"markdown\"}");
    Json::Value v = Obj(fwd);
    CHECK_FALSE(v.isMember("bot"));
    CHECK_FALSE(v.isMember("autoreply"));
    CHECK(v["format"].asString() == "markdown");
}
