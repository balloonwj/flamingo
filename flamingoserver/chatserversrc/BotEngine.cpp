/**
 *  机器人引擎实现，BotEngine.cpp。设计与约定见 BotEngine.h 与 docs/机器人方案.md §5.2～§5.4。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotEngine.h"

#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <functional>
#include <memory>

#include "BotManager.h"
#include "FeatureSwitchManager.h"
#include "Protocol.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThread.h"
#include "../jsoncpp1.9.0/json.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

using bot::BotRecord;
using bot::BotRule;
using bot::BotDecision;
using bot::BotSnapshot;

namespace {

//定时检查的间隔：30 秒，单位微秒（EventLoop::runEvery 的单位）
const int64_t kScheduleCheckIntervalUs = 30LL * 1000 * 1000;
//外部服务请求的超时（毫秒）
const int kExternalTimeoutMs = 5000;
//外部服务回复与 Webhook 正文的最多字数
const int kExternalTextMaxChars = 4096;
//Webhook 签名时间戳允许的偏差（毫秒）：5 分钟，防止重放
const int64_t kWebhookSkewMs = 5LL * 60 * 1000;
//外部服务工作线程数的范围
const int kExternalThreadsMin = 1;
const int kExternalThreadsMax = 8;
//入群欢迎里最多列出的新成员数；超出的不再列名字
const size_t kJoinNamesMax = 20;
//「试一试」里个人机器人模拟发送者的名字（服务端生成的文字只有简体中文）
const char* const kTrySenderName = u8"同事";

//紧凑 JSON
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, v);
}

//解析 JSON 对象
bool ParseObject(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs))
        return false;
    return root.isObject();
}

//读一个 id（数字或数字字符串）
int64_t ReadId(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
    {
        const std::string s = v.asString();
        if (s.empty())
            return 0;
        char* end = NULL;
        const long long n = strtoll(s.c_str(), &end, 10);
        return (end != NULL && *end == '\0') ? static_cast<int64_t>(n) : 0;
    }
    return 0;
}

//数字转字符串
std::string Str(int64_t v)
{
    return std::to_string(static_cast<long long>(v));
}

//日期文字 YYYY-MM-DD（替换定时消息里的 {日期}）
std::string DateOf(const bot::LocalClock& clock)
{
    char buf[16] = { 0 };
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", clock.m_year, clock.m_month, clock.m_day);
    return buf;
}

//机器人有没有某种动作的启用规则
bool HasActionRule(const BotRecord& b, int action)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        if (b.m_rules[i].m_enabled && b.m_rules[i].m_action.m_type == action)
            return true;
    }
    return false;
}

//机器人有没有某种触发方式的启用规则
bool HasTriggerRule(const BotRecord& b, int trigger)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        if (b.m_rules[i].m_enabled && b.m_rules[i].m_trigger.m_type == trigger)
            return true;
    }
    return false;
}

//个人机器人的规则里有没有按部门限定发送者的
bool NeedsDeptChain(const BotRecord& b)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        if (b.m_rules[i].m_enabled && b.m_rules[i].m_trigger.m_senders.m_kind == bot::bot_sender_depts)
            return true;
    }
    return false;
}

//应答 JSON
std::string WebhookResp(int code, const char* msg, int64_t msgid)
{
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    if (msgid > 0)
        v["msgid"] = Str(msgid);
    return WriteCompact(v);
}

} // anonymous namespace

//======================== BotRateLimiter ========================

namespace {
//每分钟的毫秒数
const int64_t kMsPerMinute = 60000;
}

BotRateLimiter::BotRateLimiter()
    : m_lastPrune(0)
{
}

bool BotRateLimiter::allow(const std::string& key, int limit, int64_t nowMs)
{
    const int64_t minute = nowMs / kMsPerMinute;
    std::lock_guard<std::mutex> guard(m_mutex);
    //每过一分钟清理一次上一分钟以前的计数，表不会无限增长
    if (minute != m_lastPrune)
    {
        std::map<std::string, std::pair<int64_t, int> >::iterator it = m_counters.begin();
        while (it != m_counters.end())
        {
            if (it->second.first < minute)
                m_counters.erase(it++);
            else
                ++it;
        }
        m_lastPrune = minute;
    }
    std::pair<int64_t, int>& c = m_counters[key];
    if (c.first != minute)
    {
        c.first = minute;
        c.second = 0;
    }
    if (c.second >= limit)
        return false;
    ++c.second;
    return true;
}

void BotRateLimiter::reset()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_counters.clear();
    m_lastPrune = 0;
}

//======================== BotEngine ========================

BotEngine::BotEngine()
    : m_env(NULL), m_loop(NULL), m_stopping(false)
{
}

BotEngine::~BotEngine()
{
    stop();
}

bool BotEngine::start(IBotEnv* env, int externalThreads)
{
    m_env = env;
    m_thread.reset(new net::EventLoopThread(net::EventLoopThread::ThreadInitCallback(), "bot-engine"));
    net::EventLoop* loop = m_thread->startLoop();
    loop->runEvery(kScheduleCheckIntervalUs, std::bind(&BotEngine::onScheduleTimer, this));
    m_loop.store(loop);

    if (externalThreads < kExternalThreadsMin)
        externalThreads = kExternalThreadsMin;
    if (externalThreads > kExternalThreadsMax)
        externalThreads = kExternalThreadsMax;
    {
        std::lock_guard<std::mutex> guard(m_jobMutex);
        m_stopping = false;
    }
    for (int i = 0; i < externalThreads; ++i)
        m_workers.push_back(std::thread(std::bind(&BotEngine::externalWorker, this)));
    LOGI("bot engine started, external threads: %d", externalThreads);
    return true;
}

void BotEngine::stop()
{
    {
        std::lock_guard<std::mutex> guard(m_jobMutex);
        m_stopping = true;
    }
    m_jobCond.notify_all();
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        if (m_workers[i].joinable())
            m_workers[i].join();
    }
    m_workers.clear();
    m_loop.store(NULL);
    //析构 EventLoopThread 会让机器人线程的事件循环退出并等待线程结束
    if (m_thread)
    {
        m_thread.reset();
        LOGI("bot engine stopped");
    }
}

void BotEngine::setEnvForTest(IBotEnv* env)
{
    m_env = env;
}

void BotEngine::resetStateForTest()
{
    {
        std::lock_guard<std::mutex> guard(m_stateMutex);
        m_lastReply.clear();
        m_lastSpoke.clear();
        m_lastScheduleKey.clear();
    }
    m_limiter.reset();
}

bool BotEngine::botAllowed(const BotRecord& b, int64_t enterpriseId)
{
    if (m_env == NULL || !m_env->featureOn(enterpriseId, kFeatureBot))
        return false;
    switch (b.m_ownerType)
    {
    //群机器人：企业须允许群主添加群机器人
    case bot::bot_owner_group:
        return m_env->featureOn(enterpriseId, kFeatureBotGroup);
    //个人机器人：企业须允许员工配置个人机器人
    case bot::bot_owner_personal:
        return m_env->featureOn(enterpriseId, kFeatureBotPersonal);
    //企业机器人：只看机器人总开关
    default:
        return true;
    }
}

int64_t BotEngine::sendGroup(const BotRecord& b, int64_t enterpriseId, int64_t groupid, const std::string& content,
                             const std::string& clientMsgId)
{
    const int64_t now = m_env->nowMs();
    if (!m_limiter.allow("c:" + Str(b.m_botId) + ":" + Str(groupid), bot::kBotRatePerConversationPerMinute, now))
    {
        LOGW("bot rate limited in conversation, botid: %lld, group: %lld", (long long)b.m_botId, (long long)groupid);
        return -1;
    }
    if (!m_limiter.allow("e:" + Str(enterpriseId), bot::kBotRatePerEnterprisePerMinute, now))
    {
        LOGW("bot rate limited in enterprise, botid: %lld, enterprise: %lld", (long long)b.m_botId,
             (long long)enterpriseId);
        return -1;
    }
    const int64_t msgid = m_env->postMessage(conversation_group, b.m_userId, groupid, content, clientMsgId);
    if (msgid > 0)
        Singleton<BotManager>::Instance().touchFire(b.m_botId, now);
    else
        LOGE("bot post group message failed, botid: %lld, group: %lld", (long long)b.m_botId, (long long)groupid);
    return msgid;
}

// ---------------- 判定入口 ----------------

void BotEngine::onGroupMessage(const BotGroupEvent& ev)
{
    net::EventLoop* loop = m_loop.load();
    if (loop == NULL)
        return;
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    if (!snap->groupMayHaveBots(ev.m_groupid, ev.m_enterpriseId))
        return;
    loop->runInLoop(std::bind(&BotEngine::processGroupMessage, this, ev));
}

void BotEngine::onSingleMessage(const BotSingleEvent& ev)
{
    net::EventLoop* loop = m_loop.load();
    if (loop == NULL)
        return;
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    if (!snap->userHasPersonalBot(ev.m_to) && !snap->userHasPersonalBot(ev.m_from))
        return;
    loop->runInLoop(std::bind(&BotEngine::processSingleMessage, this, ev));
}

void BotEngine::onMembersJoined(int64_t groupid, int64_t enterpriseId, const std::vector<int64_t>& members)
{
    net::EventLoop* loop = m_loop.load();
    if (loop == NULL || members.empty())
        return;
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    if (!snap->groupMayHaveBots(groupid, enterpriseId))
        return;
    loop->runInLoop(std::bind(&BotEngine::processJoin, this, groupid, members));
}

void BotEngine::onScheduleTimer()
{
    if (m_env != NULL)
        processScheduleTick(m_env->nowMs());
}

// ---------------- 同步处理 ----------------

void BotEngine::processGroupMessage(const BotGroupEvent& ev)
{
    if (m_env == NULL || ev.m_msgtype != message_type_text)
        return;
    int64_t enterpriseId = 0;
    std::string groupName;
    bool isThread = false;
    if (!m_env->groupInfo(ev.m_groupid, enterpriseId, groupName, isThread) || isThread)
        return;
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    //机器人账号发的消息不进判定
    if (snap->findByUserId(ev.m_from))
        return;
    std::vector<BotSnapshot::BotPtr> bots;
    snap->botsForGroup(ev.m_groupid, enterpriseId, true, bots);
    if (bots.empty())
        return;

    bot::MessageView view;
    if (!bot::ParseMessageContent(ev.m_content, view) || view.m_botMarked || view.m_autoReply)
        return;
    const bot::LocalClock clock = bot::MakeLocalClock(m_env->nowMs());
    std::string senderName;
    bool senderLoaded = false;
    std::vector<bot::QaItem> qa;
    bool qaLoaded = false;
    //本群可用的机器人账号：消息 @ 了其中某个时，只让被 @ 的那个回答（§11 Q9）
    std::vector<int64_t> botUserIds;
    for (size_t i = 0; i < bots.size(); ++i)
        botUserIds.push_back(bots[i]->m_userId);

    for (size_t i = 0; i < bots.size(); ++i)
    {
        const BotRecord& b = *bots[i];
        if (!botAllowed(b, enterpriseId))
            continue;
        if (bot::MentionsOtherBot(view, b.m_userId, botUserIds))
        {
            LOGI("bot skipped, message mentions another bot, botid: %lld, group: %lld, msgid: %lld", (long long)b.m_botId,
                 (long long)ev.m_groupid, (long long)ev.m_msgid);
            continue;
        }
        if (!qaLoaded && HasActionRule(b, bot::bot_action_qa))
        {
            m_env->groupQa(ev.m_groupid, enterpriseId, qa);
            qaLoaded = true;
        }
        if (!senderLoaded)
        {
            senderName = m_env->groupDisplayName(ev.m_groupid, ev.m_from);
            senderLoaded = true;
        }
        const BotDecision d = bot::EvaluateGroupMessage(b, view, clock, qaLoaded ? &qa : NULL, senderName);
        if (d.m_kind == bot::bot_decision_reply)
        {
            const std::string content =
                bot::BuildGroupReplyContent(b.m_botId, d.m_text, d.m_markdown, ev.m_from, senderName, b.m_name);
            const int64_t msgid = sendGroup(b, enterpriseId, ev.m_groupid, content,
                                            "bot-" + Str(b.m_botId) + "-m" + Str(ev.m_msgid));
            LOGI("bot replied in group, botid: %lld, rule: %lld, group: %lld, trigger msgid: %lld, reply msgid: %lld",
                 (long long)b.m_botId, (long long)d.m_ruleId, (long long)ev.m_groupid, (long long)ev.m_msgid,
                 (long long)msgid);
        }
        else if (d.m_kind == bot::bot_decision_external)
        {
            //群机器人呼叫外部服务须企业允许；企业机器人恒允许
            if (b.m_ownerType == bot::bot_owner_group && !m_env->featureOn(enterpriseId, kFeatureBotGroupExternal))
            {
                LOGI("bot external call skipped, group external disabled, botid: %lld", (long long)b.m_botId);
                continue;
            }
            BotExternalJob job;
            job.m_bot = bots[i];
            job.m_groupid = ev.m_groupid;
            job.m_groupName = groupName;
            job.m_senderId = ev.m_from;
            job.m_senderName = senderName;
            job.m_msgid = ev.m_msgid;
            job.m_question = d.m_question;
            job.m_url = d.m_url;
            job.m_fallback = d.m_text;
            job.m_enterpriseId = enterpriseId;
            LOGI("bot external call queued, botid: %lld, rule: %lld, group: %lld, trigger msgid: %lld",
                 (long long)b.m_botId, (long long)d.m_ruleId, (long long)ev.m_groupid, (long long)ev.m_msgid);
            enqueueExternal(job);
        }
    }
}

void BotEngine::processSingleMessage(const BotSingleEvent& ev)
{
    if (m_env == NULL)
        return;
    const int64_t now = m_env->nowMs();
    int64_t ownerLastSpoke = 0;
    {
        std::lock_guard<std::mutex> guard(m_stateMutex);
        m_lastSpoke[std::make_pair(ev.m_from, ev.m_to)] = now;
        std::map<std::pair<int64_t, int64_t>, int64_t>::const_iterator it =
            m_lastSpoke.find(std::make_pair(ev.m_to, ev.m_from));
        if (it != m_lastSpoke.end())
            ownerLastSpoke = it->second;
    }
    if (!bot::IsTriggerableMsgType(ev.m_msgtype))
        return;

    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    std::vector<BotSnapshot::BotPtr> all;
    snap->personalBots(ev.m_to, true, all);
    if (all.empty() || snap->findByUserId(ev.m_from))
        return;
    const int64_t ownerEnterprise = m_env->userEnterprise(ev.m_to);
    //先判定员工自己配置的个人机器人，再判定「自动回复」的预置机器人；一条消息至多代答一次
    std::vector<BotSnapshot::BotPtr> ordered;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i]->m_source != bot::bot_source_autoreply)
            ordered.push_back(all[i]);
    }
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i]->m_source == bot::bot_source_autoreply)
            ordered.push_back(all[i]);
    }

    bot::PersonalInput in;
    if (!bot::ParseMessageContent(ev.m_content, in.m_msg))
        in.m_msg = bot::MessageView();
    in.m_msgtype = ev.m_msgtype;
    in.m_senderId = ev.m_from;
    in.m_senderName = m_env->displayName(ev.m_from);
    in.m_ownerName = m_env->displayName(ev.m_to);
    in.m_ownerPresence = m_env->presence(ev.m_to);
    in.m_clock = bot::MakeLocalClock(now);
    in.m_nowMs = now;
    in.m_ownerLastSpokeMs = ownerLastSpoke;
    bool deptLoaded = false;

    for (size_t i = 0; i < ordered.size(); ++i)
    {
        const BotRecord& b = *ordered[i];
        if (!botAllowed(b, ownerEnterprise))
            continue;
        if (!deptLoaded && NeedsDeptChain(b))
        {
            m_env->userDeptChain(ev.m_from, in.m_senderDepts);
            deptLoaded = true;
        }
        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            std::map<std::pair<int64_t, int64_t>, int64_t>::const_iterator it =
                m_lastReply.find(std::make_pair(b.m_botId, ev.m_from));
            in.m_lastReplyMs = it == m_lastReply.end() ? 0 : it->second;
        }
        const BotDecision d = bot::EvaluateSingleMessage(b, in);
        if (d.m_kind != bot::bot_decision_reply)
        {
            if (d.m_reason == bot::bot_reason_cooldown || d.m_reason == bot::bot_reason_owner_speaking)
            {
                LOGI("personal bot skipped, botid: %lld, reason: %d, from: %lld", (long long)b.m_botId, d.m_reason,
                     (long long)ev.m_from);
                return;
            }
            continue;
        }
        if (!m_limiter.allow("p:" + Str(b.m_botId) + ":" + Str(ev.m_from), bot::kBotRatePerConversationPerMinute, now))
        {
            LOGW("personal bot rate limited, botid: %lld, from: %lld", (long long)b.m_botId, (long long)ev.m_from);
            return;
        }
        const std::string content = bot::BuildPersonalReplyContent(b, d.m_text);
        const int64_t msgid = m_env->postMessage(conversation_single, ev.m_to, ev.m_from, content,
                                                 "bot-" + Str(b.m_botId) + "-m" + Str(ev.m_msgid));
        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            m_lastReply[std::make_pair(b.m_botId, ev.m_from)] = now;
        }
        if (msgid > 0)
            Singleton<BotManager>::Instance().touchFire(b.m_botId, now);
        LOGI("personal bot replied, botid: %lld, owner: %lld, to: %lld, trigger msgid: %lld, reply msgid: %lld",
             (long long)b.m_botId, (long long)ev.m_to, (long long)ev.m_from, (long long)ev.m_msgid, (long long)msgid);
        return;
    }
}

void BotEngine::processJoin(int64_t groupid, const std::vector<int64_t>& members)
{
    if (m_env == NULL || members.empty())
        return;
    int64_t enterpriseId = 0;
    std::string groupName;
    bool isThread = false;
    if (!m_env->groupInfo(groupid, enterpriseId, groupName, isThread) || isThread)
        return;
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    std::vector<BotSnapshot::BotPtr> bots;
    snap->botsForGroup(groupid, enterpriseId, true, bots);
    std::vector<std::string> names;
    bool namesLoaded = false;
    const int64_t now = m_env->nowMs();
    const bot::LocalClock clock = bot::MakeLocalClock(now);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        const BotRecord& b = *bots[i];
        if (!HasTriggerRule(b, bot::bot_trigger_join) || !botAllowed(b, enterpriseId))
            continue;
        if (!namesLoaded)
        {
            for (size_t k = 0; k < members.size() && k < kJoinNamesMax; ++k)
            {
                //机器人账号不会入群，这里只是防御
                if (!snap->findByUserId(members[k]))
                    names.push_back(m_env->groupDisplayName(groupid, members[k]));
            }
            namesLoaded = true;
        }
        if (names.empty())
            return;
        const BotDecision d = bot::EvaluateJoin(b, clock, names);
        if (d.m_kind != bot::bot_decision_reply)
            continue;
        const std::string content =
            bot::BuildGroupReplyContent(b.m_botId, d.m_text, d.m_markdown, 0, std::string(), b.m_name);
        const int64_t msgid = sendGroup(b, enterpriseId, groupid, content,
                                        "bot-" + Str(b.m_botId) + "-j" + Str(members[0]) + "-" + Str(now));
        LOGI("bot welcomed new members, botid: %lld, group: %lld, members: %u, msgid: %lld", (long long)b.m_botId,
             (long long)groupid, static_cast<unsigned>(members.size()), (long long)msgid);
    }
}

void BotEngine::processScheduleTick(int64_t nowMs)
{
    if (m_env == NULL)
        return;
    const bot::LocalClock clock = bot::MakeLocalClock(nowMs);
    const std::string key = bot::ScheduleMinuteKey(clock);
    {
        std::lock_guard<std::mutex> guard(m_stateMutex);
        if (key == m_lastScheduleKey)
            return;
        m_lastScheduleKey = key;
    }
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    std::vector<BotSnapshot::BotPtr> bots;
    snap->scheduledBots(bots);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        const BotRecord& b = *bots[i];
        const std::vector<size_t> due = bot::DueScheduleRules(b, clock);
        if (due.empty() || !botAllowed(b, b.m_enterpriseId))
            continue;
        for (size_t k = 0; k < due.size(); ++k)
        {
            const BotRule& r = b.m_rules[due[k]];
            //发往哪些群：群机器人只发本群；企业机器人按规则指定的群（须在应用范围内），没有指定时发往应用范围内的全部群
            std::vector<int64_t> targets;
            if (b.m_ownerType == bot::bot_owner_group)
            {
                targets.push_back(b.m_ownerId);
            }
            else if (!r.m_trigger.m_scheduleGroups.empty())
            {
                for (size_t g = 0; g < r.m_trigger.m_scheduleGroups.size(); ++g)
                {
                    const int64_t gid = r.m_trigger.m_scheduleGroups[g];
                    if (b.m_scope == bot::bot_scope_all
                        || std::find(b.m_groupIds.begin(), b.m_groupIds.end(), gid) != b.m_groupIds.end())
                        targets.push_back(gid);
                }
            }
            else if (b.m_scope == bot::bot_scope_selected)
            {
                targets = b.m_groupIds;
            }
            else
            {
                m_env->enterpriseGroups(b.m_enterpriseId, targets);
            }
            const std::string text =
                bot::ExpandVariables(r.m_action.m_text, std::string(), std::string(), DateOf(clock), std::string());
            const std::string content = bot::BuildGroupReplyContent(b.m_botId, text, r.m_action.m_markdown, 0,
                                                                    std::string(), b.m_name);
            int sent = 0;
            for (size_t g = 0; g < targets.size(); ++g)
            {
                const int64_t gid = targets[g];
                if (b.m_ownerType == bot::bot_owner_enterprise && snap->isOffInGroup(gid, b.m_botId))
                    continue;
                int64_t ent = 0;
                std::string name;
                bool isThread = false;
                if (!m_env->groupInfo(gid, ent, name, isThread) || isThread || ent != b.m_enterpriseId)
                    continue;
                const int64_t msgid = sendGroup(b, ent, gid, content,
                                                "bot-" + Str(b.m_botId) + "-s" + Str(r.m_id) + "-" + key + "-" + Str(gid));
                if (msgid > 0)
                    ++sent;
            }
            LOGI("bot schedule fired, botid: %lld, rule: %lld, minute: %s, groups: %u, sent: %d", (long long)b.m_botId,
                 (long long)r.m_id, key.c_str(), static_cast<unsigned>(targets.size()), sent);
        }
    }
}

// ---------------- 外部服务 ----------------

void BotEngine::enqueueExternal(const BotExternalJob& job)
{
    {
        std::lock_guard<std::mutex> guard(m_jobMutex);
        if (!m_workers.empty() && !m_stopping)
        {
            m_jobs.push_back(job);
            m_jobCond.notify_one();
            return;
        }
    }
    //没有起工作线程（单元测试），直接执行
    processExternalJob(job);
}

void BotEngine::externalWorker()
{
    while (true)
    {
        BotExternalJob job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            while (m_jobs.empty() && !m_stopping)
                m_jobCond.wait(lock);
            if (m_jobs.empty() && m_stopping)
                return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        processExternalJob(job);
    }
}

void BotEngine::processExternalJob(const BotExternalJob& job)
{
    if (m_env == NULL || !job.m_bot)
        return;
    const BotRecord& b = *job.m_bot;
    const int64_t now = m_env->nowMs();
    Json::Value req;
    req["botid"] = Str(b.m_botId);
    req["botname"] = b.m_name;
    req["groupid"] = Str(job.m_groupid);
    req["groupname"] = job.m_groupName;
    req["senderid"] = Str(job.m_senderId);
    req["sendername"] = job.m_senderName;
    req["msgid"] = Str(job.m_msgid);
    req["text"] = job.m_question;
    req["timestamp"] = Str(now);
    const std::string body = WriteCompact(req);
    const std::string ts = Str(now);
    std::vector<std::pair<std::string, std::string> > headers;
    headers.push_back(std::make_pair(std::string("Content-Type"), std::string("application/json; charset=utf-8")));
    headers.push_back(std::make_pair(std::string("X-Flamingo-Bot"), Str(b.m_botId)));
    headers.push_back(std::make_pair(std::string("X-Flamingo-Timestamp"), ts));
    headers.push_back(std::make_pair(std::string("X-Flamingo-Signature"), bot::ComputeSignature(b.m_secret, ts, body)));

    int status = 0;
    std::string resp;
    const bool ok = m_env->httpPost(job.m_url, headers, body, kExternalTimeoutMs, status, resp);
    std::string text;
    bool markdown = false;
    if (ok && status >= 200 && status < 300)
    {
        Json::Value root;
        if (ParseObject(resp, root))
        {
            text = root["text"].isString() ? root["text"].asString() : std::string();
            markdown = root["format"].isString() && root["format"].asString() == "markdown";
        }
        else
        {
            text = resp;
        }
        if (bot::NormalizeText(text).empty())
        {
            LOGI("bot external call returned empty reply, botid: %lld, status: %d", (long long)b.m_botId, status);
            return;
        }
        if (bot::Utf8Length(text) > kExternalTextMaxChars)
        {
            LOGW("bot external reply too long, botid: %lld", (long long)b.m_botId);
            text = job.m_fallback;
            markdown = false;
        }
        else if (m_env->hitSensitive(job.m_enterpriseId, b.m_userId, b.m_name, job.m_groupid, text))
        {
            LOGW("bot external reply hit sensitive word, botid: %lld, group: %lld", (long long)b.m_botId,
                 (long long)job.m_groupid);
            text = job.m_fallback;
            markdown = false;
        }
    }
    else
    {
        LOGW("bot external call failed, botid: %lld, ok: %d, status: %d", (long long)b.m_botId, ok ? 1 : 0, status);
        text = job.m_fallback;
    }
    if (bot::NormalizeText(text).empty())
        return;
    const std::string content =
        bot::BuildGroupReplyContent(b.m_botId, text, markdown, job.m_senderId, job.m_senderName, b.m_name);
    const int64_t msgid = sendGroup(b, job.m_enterpriseId, job.m_groupid, content,
                                    "bot-" + Str(b.m_botId) + "-m" + Str(job.m_msgid));
    LOGI("bot external reply sent, botid: %lld, group: %lld, status: %d, msgid: %lld", (long long)b.m_botId,
         (long long)job.m_groupid, status, (long long)msgid);
}

// ---------------- Webhook 与试一试 ----------------

int BotEngine::handleWebhook(const std::string& body, std::string& respJson)
{
    Json::Value root;
    if (m_env == NULL || !ParseObject(body, root) || !root["token"].isString() || !root["text"].isString())
    {
        respJson = WebhookResp(bot_webhook_bad_request, "bad request", 0);
        return bot_webhook_bad_request;
    }
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    BotSnapshot::BotPtr b = snap->findByWebhookToken(root["token"].asString());
    if (!b)
    {
        LOGW("bot webhook rejected: unknown token");
        respJson = WebhookResp(bot_webhook_unauthorized, "invalid token", 0);
        return bot_webhook_unauthorized;
    }
    const int64_t ent = b->m_enterpriseId;
    if (b->m_status != bot::bot_status_active || !bot::HasWebhookRule(*b) || !botAllowed(*b, ent)
        || (b->m_ownerType == bot::bot_owner_group && !m_env->featureOn(ent, kFeatureBotGroupExternal)))
    {
        LOGW("bot webhook rejected: forbidden, botid: %lld", (long long)b->m_botId);
        respJson = WebhookResp(bot_webhook_forbidden, "bot disabled or webhook not enabled", 0);
        return bot_webhook_forbidden;
    }
    const int64_t now = m_env->nowMs();
    const std::string sign = root["sign"].isString() ? root["sign"].asString() : std::string();
    const std::string text = root["text"].asString();
    if (!sign.empty())
    {
        const std::string ts = root["timestamp"].isString() ? root["timestamp"].asString()
                                                            : (root["timestamp"].isIntegral() ? Str(root["timestamp"].asInt64())
                                                                                              : std::string());
        const int64_t tsv = ReadId(Json::Value(ts));
        const int64_t skew = now > tsv ? now - tsv : tsv - now;
        if (tsv <= 0 || skew > kWebhookSkewMs || bot::ComputeSignature(b->m_secret, ts, text) != sign)
        {
            LOGW("bot webhook rejected: bad signature, botid: %lld", (long long)b->m_botId);
            respJson = WebhookResp(bot_webhook_unauthorized, "bad signature or timestamp", 0);
            return bot_webhook_unauthorized;
        }
    }
    if (bot::NormalizeText(text).empty() || bot::Utf8Length(text) > kExternalTextMaxChars)
    {
        respJson = WebhookResp(bot_webhook_bad_request, "text empty or too long", 0);
        return bot_webhook_bad_request;
    }
    if (!m_limiter.allow("w:" + Str(b->m_botId), bot::kBotWebhookPerMinute, now))
    {
        LOGW("bot webhook rate limited, botid: %lld", (long long)b->m_botId);
        respJson = WebhookResp(bot_webhook_rate_limited, "rate limited", 0);
        return bot_webhook_rate_limited;
    }

    //目标群：群机器人只能发本群；企业机器人须在应用范围内，应用到多个群时必须指定
    const int64_t asked = ReadId(root["groupid"]);
    int64_t groupid = 0;
    if (b->m_ownerType == bot::bot_owner_group)
    {
        groupid = (asked == 0 || asked == b->m_ownerId) ? b->m_ownerId : 0;
    }
    else if (asked != 0)
    {
        if (b->m_scope == bot::bot_scope_all
            || std::find(b->m_groupIds.begin(), b->m_groupIds.end(), asked) != b->m_groupIds.end())
            groupid = asked;
    }
    else if (b->m_scope == bot::bot_scope_selected && b->m_groupIds.size() == 1)
    {
        groupid = b->m_groupIds[0];
    }
    int64_t groupEnt = 0;
    std::string groupName;
    bool isThread = false;
    if (groupid == 0 || !m_env->groupInfo(groupid, groupEnt, groupName, isThread) || isThread || groupEnt != ent
        || (b->m_ownerType == bot::bot_owner_enterprise && snap->isOffInGroup(groupid, b->m_botId)))
    {
        LOGW("bot webhook rejected: bad group, botid: %lld, asked: %lld", (long long)b->m_botId, (long long)asked);
        respJson = WebhookResp(bot_webhook_bad_group, "groupid required or not in bot scope", 0);
        return bot_webhook_bad_group;
    }
    if (m_env->hitSensitive(ent, b->m_userId, b->m_name, groupid, text))
    {
        respJson = WebhookResp(bot_webhook_sensitive, "text contains sensitive word", 0);
        return bot_webhook_sensitive;
    }
    const bool markdown = root["msgtype"].isString() && root["msgtype"].asString() == "markdown";
    const std::string content = bot::BuildGroupReplyContent(b->m_botId, text, markdown, 0, std::string(), b->m_name);
    const int64_t msgid = sendGroup(*b, ent, groupid, content, "bot-" + Str(b->m_botId) + "-w" + bot::RandomHex32());
    if (msgid == -1)
    {
        respJson = WebhookResp(bot_webhook_rate_limited, "rate limited", 0);
        return bot_webhook_rate_limited;
    }
    if (msgid <= 0)
    {
        respJson = WebhookResp(bot_webhook_server_error, "post message failed", 0);
        return bot_webhook_server_error;
    }
    LOGI("bot webhook delivered, botid: %lld, group: %lld, msgid: %lld", (long long)b->m_botId, (long long)groupid,
         (long long)msgid);
    respJson = WebhookResp(bot_webhook_ok, "ok", msgid);
    return bot_webhook_ok;
}

bot::BotDecision BotEngine::tryBot(const BotRecord& b, const BotTryInput& in)
{
    BotDecision d;
    if (m_env == NULL)
        return d;
    const int64_t now = in.m_timeMs > 0 ? in.m_timeMs : m_env->nowMs();
    const bot::LocalClock clock = bot::MakeLocalClock(now);
    if (b.m_ownerType == bot::bot_owner_personal)
    {
        bot::PersonalInput pin;
        Json::Value c;
        c["text"] = in.m_text;
        bot::ParseMessageContent(WriteCompact(c), pin.m_msg);
        pin.m_msgtype = message_type_text;
        pin.m_senderId = in.m_senderId;
        pin.m_senderName = in.m_senderId > 0 ? m_env->displayName(in.m_senderId) : std::string(kTrySenderName);
        pin.m_ownerName = m_env->displayName(b.m_ownerId);
        pin.m_ownerPresence = in.m_presence;
        pin.m_clock = clock;
        pin.m_nowMs = now;
        if (in.m_senderId > 0)
            m_env->userDeptChain(in.m_senderId, pin.m_senderDepts);
        return bot::EvaluateSingleMessage(b, pin);
    }

    //尚未保存的机器人还没有账号，给它一个临时的 userid，模拟「@ 了它」
    BotRecord copy = b;
    if (copy.m_userId <= 0)
        copy.m_userId = 1;
    bot::MessageView view;
    view.m_text = in.m_text;
    view.m_textWithoutAt = in.m_text;
    if (in.m_at)
        view.m_atUsers.push_back(copy.m_userId);
    std::vector<bot::QaItem> qa;
    int64_t ent = b.m_enterpriseId;
    if (in.m_groupid > 0)
    {
        std::string name;
        bool isThread = false;
        m_env->groupInfo(in.m_groupid, ent, name, isThread);
    }
    m_env->groupQa(in.m_groupid, ent, qa);
    return bot::EvaluateGroupMessage(copy, view, clock, &qa, std::string(kTrySenderName));
}
