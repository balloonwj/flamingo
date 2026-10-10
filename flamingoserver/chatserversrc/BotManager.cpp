/**
 *  机器人存储管理器实现，BotManager.cpp。设计与约定见 BotManager.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotManager.h"

#include <algorithm>
#include <memory>
#include <sstream>

#include "BotRules.h"
#include "UserManager.h"
#include "Protocol.h"                 //user_type_bot
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"   //CDbTransaction：机器人、规则、所选的群一起写
#include "../dbapi/SqlDialect.h"      //EscapeString
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

using bot::BotRecord;
using bot::BotRule;
using bot::BotSnapshot;

namespace {

//「最近发消息时间」写库的最短间隔（毫秒）：每分钟最多写一次
const int64_t kFireWriteIntervalMs = 60000;
//机器人账号用户名的前缀；后面接 16 位随机十六进制，与员工账号（字母与数字，见 AccountName）不会相撞
const char* const kBotAccountPrefix = "__bot_";
//机器人账号用户名里随机部分的长度
const size_t kBotAccountRandomLen = 16;
//账号状态：停用
const int kUserStatusDisabled = 1;

//建一条数据库连接；失败时记日志并返回空
std::unique_ptr<IDatabase> OpenConn(const char* where)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        LOGE("%s db init failed, db: %s", where, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

//紧凑 JSON
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, v);
}

//解析 JSON
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
}

//t_bot 的列清单
const char* const kBotColumns =
    "f_bot_id, f_user_id, f_enterprise_id, f_owner_type, f_owner_id, f_name, f_desc, f_glyph, f_color, f_scope, "
    "f_allow_group_off, f_status, f_source, f_creator_type, f_creator_id, f_creator_name, f_webhook_token, f_secret, "
    "f_create_time, f_update_time, f_last_fire_time";

//按列序填一条机器人记录（不含规则与所选的群）
void FillBot(Field* pRow, BotRecord& b)
{
    b.m_botId = static_cast<int64_t>(pRow[0].getUInt64());
    b.m_userId = static_cast<int32_t>(pRow[1].getUInt64());
    b.m_enterpriseId = static_cast<int64_t>(pRow[2].getUInt64());
    b.m_ownerType = pRow[3].getInt32();
    b.m_ownerId = static_cast<int64_t>(pRow[4].getUInt64());
    b.m_name = pRow[5].getCppString();
    b.m_desc = pRow[6].getCppString();
    b.m_glyph = pRow[7].getCppString();
    b.m_color = pRow[8].getCppString();
    b.m_scope = pRow[9].getInt32();
    b.m_allowGroupOff = pRow[10].getInt32() != 0;
    b.m_status = pRow[11].getInt32();
    b.m_source = pRow[12].getInt32();
    b.m_creatorType = pRow[13].getInt32();
    b.m_creatorId = static_cast<int64_t>(pRow[14].getUInt64());
    b.m_creatorName = pRow[15].getCppString();
    b.m_webhookToken = pRow[16].getCppString();
    b.m_secret = pRow[17].getCppString();
    b.m_createTime = static_cast<int64_t>(pRow[18].getUInt64());
    b.m_updateTime = static_cast<int64_t>(pRow[19].getUInt64());
    b.m_lastFireTime = static_cast<int64_t>(pRow[20].getUInt64());
}

//t_bot_rule 的列清单
const char* const kRuleColumns =
    "f_rule_id, f_bot_id, f_seq, f_name, f_enabled, f_trigger, f_action, f_create_time, f_update_time";

//按列序填一条规则；触发或动作的 JSON 解析失败时返回 false（这条规则不载入）
bool FillRule(Field* pRow, BotRule& r)
{
    r.m_id = static_cast<int64_t>(pRow[0].getUInt64());
    r.m_botId = static_cast<int64_t>(pRow[1].getUInt64());
    r.m_seq = pRow[2].getInt32();
    r.m_name = pRow[3].getCppString();
    r.m_enabled = pRow[4].getInt32() != 0;
    Json::Value trig;
    Json::Value act;
    if (!ParseJson(pRow[5].getCppString(), trig) || !bot::TriggerFromJson(trig, r.m_trigger))
        return false;
    if (!ParseJson(pRow[6].getCppString(), act) || !bot::ActionFromJson(act, r.m_action))
    {
        if (r.m_trigger.m_type != bot::bot_trigger_webhook)
            return false;
        r.m_action = bot::BotAction();
    }
    r.m_createTime = static_cast<int64_t>(pRow[7].getUInt64());
    r.m_updateTime = static_cast<int64_t>(pRow[8].getUInt64());
    return true;
}

//逐行读出一个查询的结果，交给回调；结果集为空时 dbapi 返回 NULL，按没有记录处理
template <typename Sink>
void ForEachRow(IDatabase* pConn, const std::string& sql, Sink& sink)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        sink.onRow(pRow);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
}

//收集机器人行
struct BotRowSink
{
    std::vector<BotRecord> m_rows;  //读到的机器人

    void onRow(Field* pRow)
    {
        BotRecord b;
        FillBot(pRow, b);
        m_rows.push_back(b);
    }
};

//收集规则行，按机器人分组
struct RuleRowSink
{
    std::map<int64_t, std::vector<BotRule> > m_byBot;  //机器人 id → 规则（按读出的顺序，SQL 已按 f_seq 排序）
    int m_bad;                                         //解析失败、没有载入的规则数

    RuleRowSink()
        : m_bad(0)
    {
    }

    void onRow(Field* pRow)
    {
        BotRule r;
        if (!FillRule(pRow, r))
        {
            ++m_bad;
            return;
        }
        m_byBot[r.m_botId].push_back(r);
    }
};

//收集两列整数（如 f_bot_id, f_groupid）
struct PairRowSink
{
    std::vector<std::pair<int64_t, int64_t> > m_rows;  //读到的两列

    void onRow(Field* pRow)
    {
        m_rows.push_back(std::make_pair(static_cast<int64_t>(pRow[0].getUInt64()),
                                        static_cast<int64_t>(pRow[1].getUInt64())));
    }
};

//收集白名单行
struct DomainRowSink
{
    std::map<int64_t, std::vector<std::string> > m_byEnterprise;  //企业 id → 域名

    void onRow(Field* pRow)
    {
        m_byEnterprise[static_cast<int64_t>(pRow[0].getUInt64())].push_back(pRow[1].getCppString());
    }
};

//写一个机器人的全部规则（先删后插），在调用方的事务里执行
bool WriteRules(IDatabase* pConn, int64_t botId, std::vector<BotRule>& rules, int64_t nowMs)
{
    std::ostringstream osDel;
    osDel << "DELETE FROM t_bot_rule WHERE f_bot_id = " << botId;
    if (!pConn->execute(osDel.str().c_str()))
        return false;
    for (size_t i = 0; i < rules.size(); ++i)
    {
        BotRule& r = rules[i];
        r.m_botId = botId;
        r.m_seq = static_cast<int>(i);
        if (r.m_createTime == 0)
            r.m_createTime = nowMs;
        r.m_updateTime = nowMs;
        std::ostringstream osIns;
        osIns << "INSERT INTO t_bot_rule(f_bot_id, f_seq, f_name, f_enabled, f_trigger_type, f_trigger, f_action_type, "
                 "f_action, f_create_time, f_update_time) VALUES("
              << botId << ", " << r.m_seq << ", '" << dbapi::EscapeString(r.m_name) << "', " << (r.m_enabled ? 1 : 0)
              << ", " << r.m_trigger.m_type << ", '" << dbapi::EscapeString(WriteCompact(bot::TriggerToJson(r.m_trigger)))
              << "', " << r.m_action.m_type << ", '" << dbapi::EscapeString(WriteCompact(bot::ActionToJson(r.m_action)))
              << "', " << r.m_createTime << ", " << r.m_updateTime << ")";
        if (!pConn->execute(osIns.str().c_str()))
            return false;
        r.m_id = static_cast<int64_t>(pConn->getInsertID());
    }
    return true;
}

//写一个企业机器人所选的群（先删后插），在调用方的事务里执行
bool WriteGroups(IDatabase* pConn, int64_t botId, int scope, const std::vector<int64_t>& groupIds)
{
    std::ostringstream osDel;
    osDel << "DELETE FROM t_bot_group WHERE f_bot_id = " << botId;
    if (!pConn->execute(osDel.str().c_str()))
        return false;
    if (scope != bot::bot_scope_selected)
        return true;
    std::set<int64_t> seen;
    for (size_t i = 0; i < groupIds.size(); ++i)
    {
        if (!seen.insert(groupIds[i]).second)
            continue;
        std::ostringstream osIns;
        osIns << "INSERT INTO t_bot_group(f_bot_id, f_groupid) VALUES(" << botId << ", " << groupIds[i] << ")";
        if (!pConn->execute(osIns.str().c_str()))
            return false;
    }
    return true;
}

//改机器人账号的昵称或状态，并刷新用户缓存
void UpdateBotAccount(IDatabase* pConn, int32_t userId, const std::string* nickname, int status)
{
    if (userId <= 0)
        return;
    std::ostringstream os;
    os << "UPDATE t_user SET ";
    bool first = true;
    if (nickname != NULL)
    {
        os << "f_nickname = '" << dbapi::EscapeString(*nickname) << "'";
        first = false;
    }
    if (status >= 0)
    {
        if (!first)
            os << ", ";
        os << "f_status = " << status;
    }
    os << " WHERE f_user_id = " << userId << " AND f_usertype = " << static_cast<int>(user_type_bot);
    if (!pConn->execute(os.str().c_str()))
        LOGW("BotManager: update bot account failed, userid: %d", userId);
    Singleton<UserManager>::Instance().reloadUserCache(userId);
}

} // anonymous namespace

//======================== BotSnapshot ========================

namespace bot {

BotSnapshot::BotSnapshot()
{
}

void BotSnapshot::build(const std::vector<BotPtr>& bots, const std::map<int64_t, std::set<int64_t> >& groupOff,
                        const std::map<int64_t, std::vector<std::string> >& domains)
{
    m_bots.clear();
    m_userToBot.clear();
    m_tokenToBot.clear();
    m_enterpriseBots.clear();
    m_groupBots.clear();
    m_personalBots.clear();
    m_groupOff = groupOff;
    m_domains = domains;
    for (size_t i = 0; i < bots.size(); ++i)
    {
        const BotPtr& b = bots[i];
        if (!b || b->m_status == bot_status_deleted)
            continue;
        m_bots[b->m_botId] = b;
        if (b->m_userId > 0)
            m_userToBot[b->m_userId] = b->m_botId;
        if (!b->m_webhookToken.empty())
            m_tokenToBot[b->m_webhookToken] = b->m_botId;
        switch (b->m_ownerType)
        {
        //企业机器人按企业索引
        case bot_owner_enterprise:
            m_enterpriseBots[b->m_enterpriseId].push_back(b->m_botId);
            break;
        //群机器人按群号索引
        case bot_owner_group:
            m_groupBots[b->m_ownerId].push_back(b->m_botId);
            break;
        //个人机器人按主人索引
        case bot_owner_personal:
            m_personalBots[b->m_ownerId].push_back(b->m_botId);
            break;
        //不认识的归属不建索引
        default:
            break;
        }
    }
    //索引里的 id 排好序，列表顺序稳定
    for (std::map<int64_t, std::vector<int64_t> >::iterator it = m_enterpriseBots.begin(); it != m_enterpriseBots.end();
         ++it)
        std::sort(it->second.begin(), it->second.end());
    for (std::map<int64_t, std::vector<int64_t> >::iterator it = m_groupBots.begin(); it != m_groupBots.end(); ++it)
        std::sort(it->second.begin(), it->second.end());
    for (std::map<int64_t, std::vector<int64_t> >::iterator it = m_personalBots.begin(); it != m_personalBots.end(); ++it)
        std::sort(it->second.begin(), it->second.end());
}

BotSnapshot::BotPtr BotSnapshot::find(int64_t botId) const
{
    std::map<int64_t, BotPtr>::const_iterator it = m_bots.find(botId);
    return it == m_bots.end() ? BotPtr() : it->second;
}

BotSnapshot::BotPtr BotSnapshot::findByUserId(int64_t userId) const
{
    std::map<int64_t, int64_t>::const_iterator it = m_userToBot.find(userId);
    return it == m_userToBot.end() ? BotPtr() : find(it->second);
}

BotSnapshot::BotPtr BotSnapshot::findByWebhookToken(const std::string& token) const
{
    if (token.empty())
        return BotPtr();
    std::map<std::string, int64_t>::const_iterator it = m_tokenToBot.find(token);
    return it == m_tokenToBot.end() ? BotPtr() : find(it->second);
}

bool BotSnapshot::isOffInGroup(int64_t groupid, int64_t botId) const
{
    std::map<int64_t, std::set<int64_t> >::const_iterator it = m_groupOff.find(groupid);
    return it != m_groupOff.end() && it->second.count(botId) > 0;
}

void BotSnapshot::botsForGroup(int64_t groupid, int64_t enterpriseId, bool activeOnly, std::vector<BotPtr>& out) const
{
    out.clear();
    std::map<int64_t, std::vector<int64_t> >::const_iterator eit = m_enterpriseBots.find(enterpriseId);
    if (enterpriseId > 0 && eit != m_enterpriseBots.end())
    {
        for (size_t i = 0; i < eit->second.size(); ++i)
        {
            BotPtr b = find(eit->second[i]);
            if (!b)
                continue;
            if (b->m_scope == bot_scope_selected
                && std::find(b->m_groupIds.begin(), b->m_groupIds.end(), groupid) == b->m_groupIds.end())
                continue;
            if (activeOnly && (b->m_status != bot_status_active || isOffInGroup(groupid, b->m_botId)))
                continue;
            out.push_back(b);
        }
    }
    std::map<int64_t, std::vector<int64_t> >::const_iterator git = m_groupBots.find(groupid);
    if (git != m_groupBots.end())
    {
        for (size_t i = 0; i < git->second.size(); ++i)
        {
            BotPtr b = find(git->second[i]);
            if (!b)
                continue;
            if (activeOnly && b->m_status != bot_status_active)
                continue;
            out.push_back(b);
        }
    }
}

bool BotSnapshot::groupMayHaveBots(int64_t groupid, int64_t enterpriseId) const
{
    if (m_groupBots.find(groupid) != m_groupBots.end())
        return true;
    return enterpriseId > 0 && m_enterpriseBots.find(enterpriseId) != m_enterpriseBots.end();
}

void BotSnapshot::personalBots(int64_t ownerId, bool activeOnly, std::vector<BotPtr>& out) const
{
    out.clear();
    std::map<int64_t, std::vector<int64_t> >::const_iterator it = m_personalBots.find(ownerId);
    if (it == m_personalBots.end())
        return;
    for (size_t i = 0; i < it->second.size(); ++i)
    {
        BotPtr b = find(it->second[i]);
        if (!b)
            continue;
        if (activeOnly && b->m_status != bot_status_active)
            continue;
        out.push_back(b);
    }
}

bool BotSnapshot::userHasPersonalBot(int64_t ownerId) const
{
    std::vector<BotPtr> bots;
    personalBots(ownerId, true, bots);
    return !bots.empty();
}

void BotSnapshot::enterpriseBots(int64_t enterpriseId, std::vector<BotPtr>& out) const
{
    out.clear();
    std::map<int64_t, std::vector<int64_t> >::const_iterator it = m_enterpriseBots.find(enterpriseId);
    if (it == m_enterpriseBots.end())
        return;
    for (size_t i = 0; i < it->second.size(); ++i)
    {
        BotPtr b = find(it->second[i]);
        if (b)
            out.push_back(b);
    }
}

void BotSnapshot::scheduledBots(std::vector<BotPtr>& out) const
{
    out.clear();
    for (std::map<int64_t, BotPtr>::const_iterator it = m_bots.begin(); it != m_bots.end(); ++it)
    {
        const BotPtr& b = it->second;
        if (b->m_status != bot_status_active || b->m_ownerType == bot_owner_personal)
            continue;
        for (size_t i = 0; i < b->m_rules.size(); ++i)
        {
            if (b->m_rules[i].m_enabled && b->m_rules[i].m_trigger.m_type == bot_trigger_schedule)
            {
                out.push_back(b);
                break;
            }
        }
    }
}

std::vector<std::string> BotSnapshot::domains(int64_t enterpriseId) const
{
    std::map<int64_t, std::vector<std::string> >::const_iterator it = m_domains.find(enterpriseId);
    return it == m_domains.end() ? std::vector<std::string>() : it->second;
}

size_t BotSnapshot::botCount() const
{
    return m_bots.size();
}

} // namespace bot

//======================== BotManager ========================

BotManager::BotManager()
    : m_snapshot(std::make_shared<bot::BotSnapshot>())
{
}

BotManager::~BotManager()
{
}

void BotManager::setWebhookBaseUrl(const std::string& baseUrl)
{
    m_webhookUrl = bot::BuildWebhookUrl(baseUrl);
    LOGI("bot webhook url: %s", m_webhookUrl.empty() ? "(not configured)" : m_webhookUrl.c_str());
}

const std::string& BotManager::webhookUrl() const
{
    return m_webhookUrl;
}

std::shared_ptr<const bot::BotSnapshot> BotManager::snapshot() const
{
    std::lock_guard<std::mutex> guard(m_snapMutex);
    return m_snapshot;
}

void BotManager::setSnapshotForTest(const std::shared_ptr<const bot::BotSnapshot>& snap)
{
    std::lock_guard<std::mutex> guard(m_snapMutex);
    m_snapshot = snap;
}

bool BotManager::loadFromDb()
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    return reloadLocked();
}

bool BotManager::reloadLocked()
{
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::reload");
    if (!pConn)
        return false;

    BotRowSink bots;
    std::ostringstream osBots;
    osBots << "SELECT " << kBotColumns << " FROM t_bot WHERE f_status <> " << bot::bot_status_deleted
           << " ORDER BY f_bot_id";
    ForEachRow(pConn.get(), osBots.str(), bots);

    RuleRowSink rules;
    std::ostringstream osRules;
    osRules << "SELECT " << kRuleColumns << " FROM t_bot_rule ORDER BY f_bot_id, f_seq, f_rule_id";
    ForEachRow(pConn.get(), osRules.str(), rules);

    PairRowSink groups;
    ForEachRow(pConn.get(), "SELECT f_bot_id, f_groupid FROM t_bot_group ORDER BY f_bot_id, f_groupid", groups);

    PairRowSink offs;
    ForEachRow(pConn.get(), "SELECT f_groupid, f_bot_id FROM t_bot_group_off", offs);

    DomainRowSink domains;
    ForEachRow(pConn.get(), "SELECT f_enterprise_id, f_domain FROM t_bot_domain ORDER BY f_enterprise_id, f_domain",
               domains);

    std::map<int64_t, std::vector<int64_t> > groupsByBot;
    for (size_t i = 0; i < groups.m_rows.size(); ++i)
        groupsByBot[groups.m_rows[i].first].push_back(groups.m_rows[i].second);
    std::map<int64_t, std::set<int64_t> > offByGroup;
    for (size_t i = 0; i < offs.m_rows.size(); ++i)
        offByGroup[offs.m_rows[i].first].insert(offs.m_rows[i].second);

    std::vector<bot::BotSnapshot::BotPtr> ptrs;
    ptrs.reserve(bots.m_rows.size());
    for (size_t i = 0; i < bots.m_rows.size(); ++i)
    {
        std::shared_ptr<BotRecord> b = std::make_shared<BotRecord>(bots.m_rows[i]);
        std::map<int64_t, std::vector<BotRule> >::iterator rit = rules.m_byBot.find(b->m_botId);
        if (rit != rules.m_byBot.end())
            b->m_rules = rit->second;
        std::map<int64_t, std::vector<int64_t> >::iterator git = groupsByBot.find(b->m_botId);
        if (git != groupsByBot.end())
            b->m_groupIds = git->second;
        ptrs.push_back(b);
    }

    std::shared_ptr<bot::BotSnapshot> snap = std::make_shared<bot::BotSnapshot>();
    snap->build(ptrs, offByGroup, domains.m_byEnterprise);
    {
        std::lock_guard<std::mutex> guard(m_snapMutex);
        m_snapshot = snap;
    }
    if (rules.m_bad > 0)
        LOGW("BotManager: %d rules skipped because of bad json", rules.m_bad);
    LOGI("BotManager: snapshot reloaded, bots: %u", static_cast<unsigned>(snap->botCount()));
    return true;
}

bool BotManager::loadBot(int64_t botId, BotRecord& out)
{
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::loadBot");
    if (!pConn)
        return false;
    BotRowSink bots;
    std::ostringstream osBot;
    osBot << "SELECT " << kBotColumns << " FROM t_bot WHERE f_bot_id = " << botId;
    ForEachRow(pConn.get(), osBot.str(), bots);
    if (bots.m_rows.empty())
        return false;
    out = bots.m_rows[0];
    RuleRowSink rules;
    std::ostringstream osRules;
    osRules << "SELECT " << kRuleColumns << " FROM t_bot_rule WHERE f_bot_id = " << botId << " ORDER BY f_seq, f_rule_id";
    ForEachRow(pConn.get(), osRules.str(), rules);
    out.m_rules = rules.m_byBot[botId];
    PairRowSink groups;
    std::ostringstream osGroups;
    osGroups << "SELECT f_bot_id, f_groupid FROM t_bot_group WHERE f_bot_id = " << botId << " ORDER BY f_groupid";
    ForEachRow(pConn.get(), osGroups.str(), groups);
    out.m_groupIds.clear();
    for (size_t i = 0; i < groups.m_rows.size(); ++i)
        out.m_groupIds.push_back(groups.m_rows[i].second);
    return true;
}

int BotManager::createBot(BotRecord& rec, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);

    //企业机器人与群机器人先建账号；个人机器人以主人的身份代答，不建账号
    rec.m_userId = 0;
    if (rec.m_ownerType != bot::bot_owner_personal)
    {
        User u;
        u.userid = 0;
        u.username = std::string(kBotAccountPrefix) + bot::RandomHex32().substr(0, kBotAccountRandomLen);
        u.nickname = rec.m_name;
        u.password = bot::RandomHex32();
        u.salt = bot::RandomHex32();
        u.usertype = user_type_bot;
        u.facetype = 0;
        u.gender = 0;
        u.birthday = 0;
        u.phonevisible = 0;
        u.landlinevisible = 0;
        u.ownerid = 0;
        u.status = 0;
        if (!Singleton<UserManager>::Instance().addUser(u, rec.m_enterpriseId))
        {
            LOGE("BotManager::createBot add account failed, enterprise: %lld", (long long)rec.m_enterpriseId);
            return bot::bot_store_db_fail;
        }
        rec.m_userId = u.userid;
    }

    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::createBot");
    if (!pConn)
        return bot::bot_store_db_fail;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return bot::bot_store_db_fail;
    rec.m_createTime = nowMs;
    rec.m_updateTime = nowMs;
    std::ostringstream osIns;
    osIns << "INSERT INTO t_bot(f_user_id, f_enterprise_id, f_owner_type, f_owner_id, f_name, f_desc, f_glyph, f_color, "
             "f_scope, f_allow_group_off, f_status, f_source, f_creator_type, f_creator_id, f_creator_name, "
             "f_webhook_token, f_secret, f_create_time, f_update_time, f_last_fire_time) VALUES("
          << rec.m_userId << ", " << rec.m_enterpriseId << ", " << rec.m_ownerType << ", " << rec.m_ownerId << ", '"
          << dbapi::EscapeString(rec.m_name) << "', '" << dbapi::EscapeString(rec.m_desc) << "', '"
          << dbapi::EscapeString(rec.m_glyph) << "', '" << dbapi::EscapeString(rec.m_color) << "', " << rec.m_scope
          << ", " << (rec.m_allowGroupOff ? 1 : 0) << ", " << rec.m_status << ", " << rec.m_source << ", "
          << rec.m_creatorType << ", " << rec.m_creatorId << ", '" << dbapi::EscapeString(rec.m_creatorName) << "', '"
          << dbapi::EscapeString(rec.m_webhookToken) << "', '" << dbapi::EscapeString(rec.m_secret) << "', " << nowMs
          << ", " << nowMs << ", 0)";
    if (!pConn->execute(osIns.str().c_str()))
    {
        LOGE("BotManager::createBot insert failed, enterprise: %lld, owner: %d/%lld", (long long)rec.m_enterpriseId,
             rec.m_ownerType, (long long)rec.m_ownerId);
        return bot::bot_store_db_fail;
    }
    const int64_t botId = static_cast<int64_t>(pConn->getInsertID());
    if (botId <= 0 || !WriteRules(pConn.get(), botId, rec.m_rules, nowMs)
        || !WriteGroups(pConn.get(), botId, rec.m_scope, rec.m_groupIds))
    {
        LOGE("BotManager::createBot rules or groups failed, enterprise: %lld", (long long)rec.m_enterpriseId);
        return bot::bot_store_db_fail;
    }
    if (!transaction.Commit())
        return bot::bot_store_db_fail;
    rec.m_botId = botId;
    reloadLocked();
    LOGI("BotManager: bot created, botid: %lld, userid: %d, owner: %d/%lld, rules: %u", (long long)botId, rec.m_userId,
         rec.m_ownerType, (long long)rec.m_ownerId, static_cast<unsigned>(rec.m_rules.size()));
    return bot::bot_store_ok;
}

int BotManager::updateBot(const BotRecord& rec, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    BotRecord exist;
    if (!loadBot(rec.m_botId, exist) || exist.m_status == bot::bot_status_deleted)
        return bot::bot_store_not_exist;

    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::updateBot");
    if (!pConn)
        return bot::bot_store_db_fail;
    {
        CDbTransaction transaction(pConn.get());
        if (!transaction.IsActive())
            return bot::bot_store_db_fail;
        std::ostringstream osUpd;
        osUpd << "UPDATE t_bot SET f_name = '" << dbapi::EscapeString(rec.m_name) << "', f_desc = '"
              << dbapi::EscapeString(rec.m_desc) << "', f_glyph = '" << dbapi::EscapeString(rec.m_glyph)
              << "', f_color = '" << dbapi::EscapeString(rec.m_color) << "', f_scope = " << rec.m_scope
              << ", f_allow_group_off = " << (rec.m_allowGroupOff ? 1 : 0) << ", f_status = " << rec.m_status
              << ", f_update_time = " << nowMs << " WHERE f_bot_id = " << rec.m_botId;
        if (!pConn->execute(osUpd.str().c_str()))
            return bot::bot_store_db_fail;
        std::vector<BotRule> rules = rec.m_rules;
        //保留原规则的创建时间：按 id 对上的沿用，其余视为新规则
        for (size_t i = 0; i < rules.size(); ++i)
        {
            for (size_t k = 0; k < exist.m_rules.size(); ++k)
            {
                if (rules[i].m_id > 0 && rules[i].m_id == exist.m_rules[k].m_id)
                    rules[i].m_createTime = exist.m_rules[k].m_createTime;
            }
        }
        if (!WriteRules(pConn.get(), rec.m_botId, rules, nowMs)
            || !WriteGroups(pConn.get(), rec.m_botId, rec.m_scope, rec.m_groupIds))
            return bot::bot_store_db_fail;
        if (!transaction.Commit())
            return bot::bot_store_db_fail;
    }
    if (exist.m_name != rec.m_name)
        UpdateBotAccount(pConn.get(), exist.m_userId, &rec.m_name, -1);
    reloadLocked();
    LOGI("BotManager: bot updated, botid: %lld, rules: %u, status: %d", (long long)rec.m_botId,
         static_cast<unsigned>(rec.m_rules.size()), rec.m_status);
    return bot::bot_store_ok;
}

int BotManager::setStatus(int64_t botId, int status, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    BotRecord exist;
    if (!loadBot(botId, exist) || exist.m_status == bot::bot_status_deleted)
        return bot::bot_store_not_exist;
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::setStatus");
    if (!pConn)
        return bot::bot_store_db_fail;
    std::ostringstream os;
    os << "UPDATE t_bot SET f_status = " << status << ", f_update_time = " << nowMs << " WHERE f_bot_id = " << botId;
    if (!pConn->execute(os.str().c_str()))
        return bot::bot_store_db_fail;
    reloadLocked();
    LOGI("BotManager: bot status changed, botid: %lld, status: %d", (long long)botId, status);
    return bot::bot_store_ok;
}

int BotManager::deleteBot(int64_t botId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    BotRecord exist;
    if (!loadBot(botId, exist) || exist.m_status == bot::bot_status_deleted)
        return bot::bot_store_not_exist;
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::deleteBot");
    if (!pConn)
        return bot::bot_store_db_fail;
    {
        CDbTransaction transaction(pConn.get());
        if (!transaction.IsActive())
            return bot::bot_store_db_fail;
        std::ostringstream osUpd;
        osUpd << "UPDATE t_bot SET f_status = " << bot::bot_status_deleted << ", f_update_time = " << nowMs
              << " WHERE f_bot_id = " << botId;
        std::ostringstream osRules;
        osRules << "DELETE FROM t_bot_rule WHERE f_bot_id = " << botId;
        std::ostringstream osGroups;
        osGroups << "DELETE FROM t_bot_group WHERE f_bot_id = " << botId;
        std::ostringstream osOff;
        osOff << "DELETE FROM t_bot_group_off WHERE f_bot_id = " << botId;
        if (!pConn->execute(osUpd.str().c_str()) || !pConn->execute(osRules.str().c_str())
            || !pConn->execute(osGroups.str().c_str()) || !pConn->execute(osOff.str().c_str()))
            return bot::bot_store_db_fail;
        if (!transaction.Commit())
            return bot::bot_store_db_fail;
    }
    UpdateBotAccount(pConn.get(), exist.m_userId, NULL, kUserStatusDisabled);
    reloadLocked();
    LOGI("BotManager: bot deleted, botid: %lld, userid: %d", (long long)botId, exist.m_userId);
    return bot::bot_store_ok;
}

int BotManager::setGroupOff(int64_t groupid, int64_t botId, bool off, int64_t operatorId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    BotRecord exist;
    if (!loadBot(botId, exist) || exist.m_status == bot::bot_status_deleted)
        return bot::bot_store_not_exist;
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::setGroupOff");
    if (!pConn)
        return bot::bot_store_db_fail;
    std::ostringstream osDel;
    osDel << "DELETE FROM t_bot_group_off WHERE f_groupid = " << groupid << " AND f_bot_id = " << botId;
    if (!pConn->execute(osDel.str().c_str()))
        return bot::bot_store_db_fail;
    if (off)
    {
        std::ostringstream osIns;
        osIns << "INSERT INTO t_bot_group_off(f_groupid, f_bot_id, f_operator_id, f_time) VALUES(" << groupid << ", "
              << botId << ", " << operatorId << ", " << nowMs << ")";
        if (!pConn->execute(osIns.str().c_str()))
            return bot::bot_store_db_fail;
    }
    reloadLocked();
    LOGI("BotManager: bot off in group changed, botid: %lld, group: %lld, off: %d, by: %lld", (long long)botId,
         (long long)groupid, off ? 1 : 0, (long long)operatorId);
    return bot::bot_store_ok;
}

int BotManager::resetSecrets(int64_t botId, std::string& outToken, std::string& outSecret)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    BotRecord exist;
    if (!loadBot(botId, exist) || exist.m_status == bot::bot_status_deleted)
        return bot::bot_store_not_exist;
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::resetSecrets");
    if (!pConn)
        return bot::bot_store_db_fail;
    outToken = bot::RandomHex32();
    outSecret = bot::RandomHex32();
    std::ostringstream os;
    os << "UPDATE t_bot SET f_webhook_token = '" << outToken << "', f_secret = '" << outSecret
       << "' WHERE f_bot_id = " << botId;
    if (!pConn->execute(os.str().c_str()))
        return bot::bot_store_db_fail;
    reloadLocked();
    LOGI("BotManager: bot secrets reset, botid: %lld", (long long)botId);
    return bot::bot_store_ok;
}

bool BotManager::setDomains(int64_t enterpriseId, const std::vector<std::string>& domains)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::setDomains");
    if (!pConn)
        return false;
    {
        CDbTransaction transaction(pConn.get());
        if (!transaction.IsActive())
            return false;
        std::ostringstream osDel;
        osDel << "DELETE FROM t_bot_domain WHERE f_enterprise_id = " << enterpriseId;
        if (!pConn->execute(osDel.str().c_str()))
            return false;
        std::set<std::string> seen;
        for (size_t i = 0; i < domains.size(); ++i)
        {
            if (!seen.insert(domains[i]).second)
                continue;
            std::ostringstream osIns;
            osIns << "INSERT INTO t_bot_domain(f_enterprise_id, f_domain) VALUES(" << enterpriseId << ", '"
                  << dbapi::EscapeString(domains[i]) << "')";
            if (!pConn->execute(osIns.str().c_str()))
                return false;
        }
        if (!transaction.Commit())
            return false;
    }
    reloadLocked();
    LOGI("BotManager: domains replaced, enterprise: %lld, count: %u", (long long)enterpriseId,
         static_cast<unsigned>(domains.size()));
    return true;
}

void BotManager::touchFire(int64_t botId, int64_t nowMs)
{
    bool needWrite = false;
    {
        std::lock_guard<std::mutex> guard(m_fireMutex);
        m_lastFire[botId] = nowMs;
        std::map<int64_t, int64_t>::iterator it = m_fireWritten.find(botId);
        if (it == m_fireWritten.end() || nowMs - it->second >= kFireWriteIntervalMs)
        {
            m_fireWritten[botId] = nowMs;
            needWrite = true;
        }
    }
    if (!needWrite)
        return;
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::touchFire");
    if (!pConn)
        return;
    std::ostringstream os;
    os << "UPDATE t_bot SET f_last_fire_time = " << nowMs << " WHERE f_bot_id = " << botId;
    if (!pConn->execute(os.str().c_str()))
        LOGW("BotManager::touchFire write failed, botid: %lld", (long long)botId);
}

int64_t BotManager::lastFireTime(int64_t botId) const
{
    {
        std::lock_guard<std::mutex> guard(m_fireMutex);
        std::map<int64_t, int64_t>::const_iterator it = m_lastFire.find(botId);
        if (it != m_lastFire.end())
            return it->second;
    }
    std::shared_ptr<const bot::BotSnapshot> snap = snapshot();
    bot::BotSnapshot::BotPtr b = snap->find(botId);
    return b ? b->m_lastFireTime : 0;
}

void BotManager::onGroupDismissed(int64_t groupid, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::onGroupDismissed");
    if (!pConn)
        return;
    //先把该群群机器人的账号停用，再软删除
    std::shared_ptr<const bot::BotSnapshot> snap = snapshot();
    std::vector<bot::BotSnapshot::BotPtr> bots;
    snap->botsForGroup(groupid, 0, false, bots);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i]->m_ownerType == bot::bot_owner_group)
            UpdateBotAccount(pConn.get(), bots[i]->m_userId, NULL, kUserStatusDisabled);
    }
    std::ostringstream osBots;
    osBots << "UPDATE t_bot SET f_status = " << bot::bot_status_deleted << ", f_update_time = " << nowMs
           << " WHERE f_owner_type = " << bot::bot_owner_group << " AND f_owner_id = " << groupid;
    std::ostringstream osGroup;
    osGroup << "DELETE FROM t_bot_group WHERE f_groupid = " << groupid;
    std::ostringstream osOff;
    osOff << "DELETE FROM t_bot_group_off WHERE f_groupid = " << groupid;
    if (!pConn->execute(osBots.str().c_str()) || !pConn->execute(osGroup.str().c_str())
        || !pConn->execute(osOff.str().c_str()))
        LOGW("BotManager::onGroupDismissed cleanup failed, group: %lld", (long long)groupid);
    reloadLocked();
    LOGI("BotManager: group dismissed, bots cleaned, group: %lld", (long long)groupid);
}

void BotManager::onUserDeleted(int64_t userId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::onUserDeleted");
    if (!pConn)
        return;
    std::ostringstream os;
    os << "UPDATE t_bot SET f_status = " << bot::bot_status_deleted << ", f_update_time = " << nowMs
       << " WHERE f_owner_type = " << bot::bot_owner_personal << " AND f_owner_id = " << userId;
    if (!pConn->execute(os.str().c_str()))
        LOGW("BotManager::onUserDeleted cleanup failed, userid: %lld", (long long)userId);
    reloadLocked();
}

void BotManager::onEnterpriseDeleted(int64_t enterpriseId)
{
    std::lock_guard<std::mutex> guard(m_writeMutex);
    std::unique_ptr<IDatabase> pConn = OpenConn("BotManager::onEnterpriseDeleted");
    if (!pConn)
        return;
    std::ostringstream osRules;
    osRules << "DELETE FROM t_bot_rule WHERE f_bot_id IN (SELECT f_bot_id FROM t_bot WHERE f_enterprise_id = "
            << enterpriseId << ")";
    std::ostringstream osGroups;
    osGroups << "DELETE FROM t_bot_group WHERE f_bot_id IN (SELECT f_bot_id FROM t_bot WHERE f_enterprise_id = "
             << enterpriseId << ")";
    std::ostringstream osOff;
    osOff << "DELETE FROM t_bot_group_off WHERE f_bot_id IN (SELECT f_bot_id FROM t_bot WHERE f_enterprise_id = "
          << enterpriseId << ")";
    std::ostringstream osBots;
    osBots << "DELETE FROM t_bot WHERE f_enterprise_id = " << enterpriseId;
    std::ostringstream osDomains;
    osDomains << "DELETE FROM t_bot_domain WHERE f_enterprise_id = " << enterpriseId;
    if (!pConn->execute(osRules.str().c_str()) || !pConn->execute(osGroups.str().c_str())
        || !pConn->execute(osOff.str().c_str()) || !pConn->execute(osBots.str().c_str())
        || !pConn->execute(osDomains.str().c_str()))
        LOGW("BotManager::onEnterpriseDeleted cleanup failed, enterprise: %lld", (long long)enterpriseId);
    reloadLocked();
    LOGI("BotManager: enterprise deleted, bots removed, enterprise: %lld", (long long)enterpriseId);
}
