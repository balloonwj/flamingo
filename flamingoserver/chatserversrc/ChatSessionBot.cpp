/**
 *  ChatSession 的机器人部分（cmd_*bot* 2801~2811，docs/机器人方案.md 第 6 节）。
 *
 *  每条命令的处理顺序大体相同：解析 → 群与成员身份 → 企业开关 → 权限（群机器人只有群主，个人机器人只有主人）→
 *  规则校验（BotRules::ValidateBot）→ 企业敏感词（来源 sensitive_source_bot_config）→ 写库（BotManager）→ 应答 →
 *  2820 推送与群系统提示。判定与执行不在这里，在 BotEngine。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "ChatSession.h"

#include <time.h>
#include <vector>

#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "GroupProtocol.h"
#include "GroupManager.h"
#include "FeatureSwitchManager.h"
#include "DepartmentManager.h"
#include "SensitiveWordDefs.h"
#include "BotDefs.h"
#include "BotRules.h"
#include "BotManager.h"
#include "BotEngine.h"
#include "BotProtocol.h"

using bot::BotRecord;
using bot::BotSnapshot;

namespace {

//每秒的毫秒数
const int64_t kBotMsPerSecond = 1000;
//「自动回复」预置机器人的名称（内部使用；客户端按 bot.kind 显示，不显示这个名字）
const char* const kAutoReplyBotName = u8"自动回复";

//当前时间（Unix 毫秒）
int64_t NowMs()
{
    return static_cast<int64_t>(time(NULL)) * kBotMsPerSecond;
}

//ValidateBot 的结果换成错误码
int MapCheck(int rc)
{
    switch (rc)
    {
    //合法
    case bot::bot_check_ok:
        return error_code_ok;
    //数量超出上限
    case bot::bot_check_full:
        return error_code_bot_full;
    //用了不允许的触发方式或动作（多为企业没有允许外部服务）
    case bot::bot_check_not_allowed:
        return error_code_bot_disabled;
    //其余字段不合法
    default:
        return error_code_bot_invalid;
    }
}

//机器人有没有会把消息发出服务器的规则（外部服务或 Webhook）
bool HasExternalRule(const BotRecord& b)
{
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const bot::BotRule& r = b.m_rules[i];
        if (!r.m_enabled)
            continue;
        if (r.m_action.m_type == bot::bot_action_external || r.m_trigger.m_type == bot::bot_trigger_webhook)
            return true;
    }
    return false;
}

//某群的群机器人个数（含停用的）
int CountGroupBots(const BotSnapshot& snap, int64_t groupid)
{
    std::vector<BotSnapshot::BotPtr> bots;
    snap.botsForGroup(groupid, 0, false, bots);
    int n = 0;
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i]->m_ownerType == bot::bot_owner_group)
            ++n;
    }
    return n;
}

//某人的个人机器人个数（不含「自动回复」的预置机器人）
int CountPersonalBots(const BotSnapshot& snap, int64_t ownerId)
{
    std::vector<BotSnapshot::BotPtr> bots;
    snap.personalBots(ownerId, false, bots);
    int n = 0;
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i]->m_source != bot::bot_source_autoreply)
            ++n;
    }
    return n;
}

//某人的「自动回复」预置机器人；没有时返回空
BotSnapshot::BotPtr FindAutoReplyBot(const BotSnapshot& snap, int64_t ownerId)
{
    std::vector<BotSnapshot::BotPtr> bots;
    snap.personalBots(ownerId, false, bots);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i]->m_source == bot::bot_source_autoreply)
            return bots[i];
    }
    return BotSnapshot::BotPtr();
}

} // anonymous namespace

void ChatSession::notifyGroupBotsChanged(int64_t groupid)
{
    notifyGroupMembers(groupid, cmd_group_bots_changed_notify, botproto::BuildChangedNotify(groupid));
}

void ChatSession::onGetGroupBots(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_bots, m_seq, botproto::BuildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_get_group_bots, m_seq, botproto::BuildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_get_group_bots, m_seq, botproto::BuildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    FeatureSwitchManager& fs = Singleton<FeatureSwitchManager>::Instance();
    const bool botOn = fs.isEnabled(ent, kFeatureBot);
    botproto::GroupBotCaps caps;
    caps.m_isOwner = group.ownerid == m_userinfo.userid;
    caps.m_groupBotAllowed = botOn && fs.isEnabled(ent, kFeatureBotGroup);
    caps.m_externalAllowed = caps.m_groupBotAllowed && fs.isEnabled(ent, kFeatureBotGroupExternal);
    caps.m_groupBotMax = bot::kGroupBotMax;

    std::vector<botproto::GroupBotView> views;
    if (botOn && group.grouptype != group_type_thread)
    {
        std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
        std::vector<BotSnapshot::BotPtr> all;
        snap->botsForGroup(groupid, ent, false, all);
        for (size_t i = 0; i < all.size(); ++i)
        {
            botproto::GroupBotView v;
            v.m_bot = all[i];
            const bool isEnterpriseBot = all[i]->m_ownerType == bot::bot_owner_enterprise;
            v.m_off = isEnterpriseBot && snap->isOffInGroup(groupid, all[i]->m_botId);
            v.m_active = all[i]->m_status == bot::bot_status_active && !v.m_off
                         && (isEnterpriseBot || caps.m_groupBotAllowed);
            v.m_canOff = isEnterpriseBot ? all[i]->m_allowGroupOff : true;
            views.push_back(v);
        }
    }
    send(cmd_get_group_bots, m_seq, botproto::BuildGroupBotsResponse(0, "ok", groupid, caps, views));
    LOGI("get group bots ok, groupid: %lld, userid: %d, bots: %u", (long long)groupid, m_userinfo.userid,
         static_cast<unsigned>(views.size()));
}

void ChatSession::onGetBotDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t botId = 0;
    if (!botproto::ParseBotIdRequest(body, botId))
    {
        send(cmd_get_bot_detail, m_seq, botproto::BuildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    std::shared_ptr<const BotSnapshot> snap = Singleton<BotManager>::Instance().snapshot();
    BotSnapshot::BotPtr b = snap->find(botId);
    if (!b)
    {
        send(cmd_get_bot_detail, m_seq, botproto::BuildSimpleResponse(error_code_bot_not_exist, "bot not exist"));
        return;
    }
    bool allowed = false;
    if (b->m_ownerType == bot::bot_owner_group)
    {
        GroupRecord group;
        allowed = Singleton<GroupManager>::Instance().getGroup(b->m_ownerId, group) && group.ownerid == m_userinfo.userid;
    }
    else if (b->m_ownerType == bot::bot_owner_personal)
    {
        allowed = b->m_ownerId == m_userinfo.userid;
    }
    if (!allowed)
    {
        send(cmd_get_bot_detail, m_seq, botproto::BuildSimpleResponse(error_code_bot_no_permission, "no permission"));
        return;
    }
    BotRecord detail = *b;
    detail.m_lastFireTime = Singleton<BotManager>::Instance().lastFireTime(botId);
    send(cmd_get_bot_detail, m_seq,
         botproto::BuildBotDetailResponse(0, "ok", &detail, true, Singleton<BotManager>::Instance().webhookUrl()));
    LOGI("get bot detail ok, botid: %lld, userid: %d", (long long)botId, m_userinfo.userid);
}

void ChatSession::onSaveGroupBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    BotRecord rec;
    if (!botproto::ParseSaveGroupBotRequest(body, groupid, rec))
    {
        send(cmd_save_group_bot, m_seq, botproto::BuildGroupBotResponse(error_code_bad_request, "bad request", 0, 0));
        return;
    }
    GroupRecord group;
    if (!Singleton<GroupManager>::Instance().getGroup(groupid, group))
    {
        send(cmd_save_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_group_not_exist, "group not exist", groupid, 0));
        return;
    }
    //群机器人只有群主能配置（docs/机器人方案.md §11 Q4）
    if (group.ownerid != m_userinfo.userid)
    {
        send(cmd_save_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_no_permission, "only group owner", groupid, 0));
        return;
    }
    if (group.grouptype == group_type_thread)
    {
        send(cmd_save_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_invalid, "thread group has no bots", groupid, 0));
        return;
    }
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    FeatureSwitchManager& fs = Singleton<FeatureSwitchManager>::Instance();
    if (!fs.isEnabled(ent, kFeatureBot) || !fs.isEnabled(ent, kFeatureBotGroup))
    {
        send(cmd_save_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_disabled, "group bot disabled", groupid, 0));
        return;
    }
    const bool allowExternal = fs.isEnabled(ent, kFeatureBotGroupExternal);
    BotManager& mgr = Singleton<BotManager>::Instance();
    std::shared_ptr<const BotSnapshot> snap = mgr.snapshot();

    rec.m_ownerType = bot::bot_owner_group;
    rec.m_ownerId = groupid;
    rec.m_enterpriseId = ent;
    rec.m_scope = bot::bot_scope_all;
    rec.m_groupIds.clear();
    rec.m_allowGroupOff = false;
    rec.m_source = bot::bot_source_rules;
    std::string why;
    const int check = bot::ValidateBot(rec, allowExternal, snap->domains(ent), why);
    if (check != bot::bot_check_ok)
    {
        LOGW("save group bot rejected, groupid: %lld, userid: %d, reason: %s", (long long)groupid, m_userinfo.userid,
             why.c_str());
        send(cmd_save_group_bot, m_seq, botproto::BuildGroupBotResponse(MapCheck(check), why, groupid, 0));
        return;
    }
    std::vector<std::string> texts;
    botproto::CollectTexts(rec, texts);
    if (hitSensitive(chatserver::sensitive_source_bot_config, conversation_group, groupid, std::string(), texts))
    {
        send(cmd_save_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_sensitive_word, "sensitive word", groupid, 0));
        return;
    }

    const int64_t now = NowMs();
    bool created = false;
    bool externalBefore = false;
    int stored = bot::bot_store_ok;
    if (rec.m_botId > 0)
    {
        BotSnapshot::BotPtr exist = snap->find(rec.m_botId);
        if (!exist || exist->m_ownerType != bot::bot_owner_group || exist->m_ownerId != groupid)
        {
            send(cmd_save_group_bot, m_seq,
                 botproto::BuildGroupBotResponse(error_code_bot_not_exist, "bot not exist", groupid, 0));
            return;
        }
        externalBefore = HasExternalRule(*exist);
        stored = mgr.updateBot(rec, now);
    }
    else
    {
        if (CountGroupBots(*snap, groupid) >= bot::kGroupBotMax)
        {
            send(cmd_save_group_bot, m_seq,
                 botproto::BuildGroupBotResponse(error_code_bot_full, "group bot limit reached", groupid, 0));
            return;
        }
        rec.m_creatorType = bot::bot_creator_user;
        rec.m_creatorId = m_userinfo.userid;
        rec.m_creatorName = m_userinfo.nickname;
        rec.m_webhookToken = bot::RandomHex32();
        rec.m_secret = bot::RandomHex32();
        stored = mgr.createBot(rec, now);
        created = true;
    }
    if (stored != bot::bot_store_ok)
    {
        const int code = stored == bot::bot_store_not_exist ? error_code_bot_not_exist : error_code_server_internal;
        send(cmd_save_group_bot, m_seq, botproto::BuildGroupBotResponse(code, "save bot failed", groupid, 0));
        return;
    }
    send(cmd_save_group_bot, m_seq, botproto::BuildGroupBotResponse(0, "ok", groupid, rec.m_botId));
    notifyGroupBotsChanged(groupid);
    //新建时、以及新加了会把消息发出服务器的规则时，在群里写一行提示，让成员知道
    const bool externalNow = HasExternalRule(rec);
    if (created || (externalNow && !externalBefore))
    {
        sendGroupSystemMessage(groupid,
                               botproto::BuildBotTipContent(kSystemTipBotAdded, m_userinfo.nickname, rec.m_name,
                                                            externalNow),
                               "sys-bot-add-" + std::to_string(static_cast<long long>(rec.m_botId)) + "-" +
                                   std::to_string(static_cast<long long>(now)));
    }
    LOGI("save group bot ok, groupid: %lld, botid: %lld, by: %d, created: %d, rules: %u", (long long)groupid,
         (long long)rec.m_botId, m_userinfo.userid, created ? 1 : 0, static_cast<unsigned>(rec.m_rules.size()));
}

void ChatSession::onDeleteGroupBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t botId = 0;
    if (!botproto::ParseGroupBotIdRequest(body, groupid, botId))
    {
        send(cmd_delete_group_bot, m_seq, botproto::BuildGroupBotResponse(error_code_bad_request, "bad request", 0, 0));
        return;
    }
    GroupRecord group;
    if (!Singleton<GroupManager>::Instance().getGroup(groupid, group))
    {
        send(cmd_delete_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_group_not_exist, "group not exist", groupid, botId));
        return;
    }
    if (group.ownerid != m_userinfo.userid)
    {
        send(cmd_delete_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_no_permission, "only group owner", groupid, botId));
        return;
    }
    BotManager& mgr = Singleton<BotManager>::Instance();
    BotSnapshot::BotPtr exist = mgr.snapshot()->find(botId);
    if (!exist || exist->m_ownerType != bot::bot_owner_group || exist->m_ownerId != groupid)
    {
        send(cmd_delete_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_bot_not_exist, "bot not exist", groupid, botId));
        return;
    }
    const int64_t now = NowMs();
    if (mgr.deleteBot(botId, now) != bot::bot_store_ok)
    {
        send(cmd_delete_group_bot, m_seq,
             botproto::BuildGroupBotResponse(error_code_server_internal, "delete bot failed", groupid, botId));
        return;
    }
    send(cmd_delete_group_bot, m_seq, botproto::BuildGroupBotResponse(0, "ok", groupid, botId));
    notifyGroupBotsChanged(groupid);
    sendGroupSystemMessage(groupid,
                           botproto::BuildBotTipContent(kSystemTipBotRemoved, m_userinfo.nickname, exist->m_name, false),
                           "sys-bot-del-" + std::to_string(static_cast<long long>(botId)) + "-" +
                               std::to_string(static_cast<long long>(now)));
    LOGI("delete group bot ok, groupid: %lld, botid: %lld, by: %d", (long long)groupid, (long long)botId,
         m_userinfo.userid);
}

void ChatSession::onSetGroupBotOff(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t botId = 0;
    bool off = false;
    if (!botproto::ParseSetGroupBotOffRequest(body, groupid, botId, off))
    {
        send(cmd_set_group_bot_off, m_seq, botproto::BuildSetOffResponse(error_code_bad_request, "bad request", 0, 0, false));
        return;
    }
    GroupRecord group;
    if (!Singleton<GroupManager>::Instance().getGroup(groupid, group))
    {
        send(cmd_set_group_bot_off, m_seq,
             botproto::BuildSetOffResponse(error_code_group_not_exist, "group not exist", groupid, botId, off));
        return;
    }
    if (group.ownerid != m_userinfo.userid)
    {
        send(cmd_set_group_bot_off, m_seq,
             botproto::BuildSetOffResponse(error_code_bot_no_permission, "only group owner", groupid, botId, off));
        return;
    }
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    BotManager& mgr = Singleton<BotManager>::Instance();
    std::shared_ptr<const BotSnapshot> snap = mgr.snapshot();
    std::vector<BotSnapshot::BotPtr> inGroup;
    snap->botsForGroup(groupid, ent, false, inGroup);
    BotSnapshot::BotPtr target;
    for (size_t i = 0; i < inGroup.size(); ++i)
    {
        if (inGroup[i]->m_botId == botId)
            target = inGroup[i];
    }
    if (!target)
    {
        send(cmd_set_group_bot_off, m_seq,
             botproto::BuildSetOffResponse(error_code_bot_not_exist, "bot not in group", groupid, botId, off));
        return;
    }
    const int64_t now = NowMs();
    int stored = bot::bot_store_ok;
    if (target->m_ownerType == bot::bot_owner_enterprise)
    {
        //企业机器人：管理员允许时群主才能在本群关闭（docs/机器人方案.md §11 Q5）
        if (!target->m_allowGroupOff)
        {
            send(cmd_set_group_bot_off, m_seq,
                 botproto::BuildSetOffResponse(error_code_bot_no_permission, "cannot turn off", groupid, botId, off));
            return;
        }
        stored = mgr.setGroupOff(groupid, botId, off, m_userinfo.userid, now);
    }
    else
    {
        stored = mgr.setStatus(botId, off ? bot::bot_status_disabled : bot::bot_status_active, now);
    }
    if (stored != bot::bot_store_ok)
    {
        send(cmd_set_group_bot_off, m_seq,
             botproto::BuildSetOffResponse(error_code_server_internal, "set off failed", groupid, botId, off));
        return;
    }
    send(cmd_set_group_bot_off, m_seq, botproto::BuildSetOffResponse(0, "ok", groupid, botId, off));
    notifyGroupBotsChanged(groupid);
    sendGroupSystemMessage(groupid,
                           botproto::BuildBotTipContent(off ? kSystemTipBotOff : kSystemTipBotOn, m_userinfo.nickname,
                                                        target->m_name, false),
                           "sys-bot-off-" + std::to_string(static_cast<long long>(botId)) + "-" +
                               std::to_string(static_cast<long long>(now)));
    LOGI("set group bot off ok, groupid: %lld, botid: %lld, off: %d, by: %d", (long long)groupid, (long long)botId,
         off ? 1 : 0, m_userinfo.userid);
}

void ChatSession::onGetMyBots(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)body;
    (void)conn;
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    FeatureSwitchManager& fs = Singleton<FeatureSwitchManager>::Instance();
    const bool allowed = fs.isEnabled(ent, kFeatureBot) && fs.isEnabled(ent, kFeatureBotPersonal);
    std::vector<BotSnapshot::BotPtr> bots;
    Singleton<BotManager>::Instance().snapshot()->personalBots(m_userinfo.userid, false, bots);
    send(cmd_get_my_bots, m_seq, botproto::BuildMyBotsResponse(0, "ok", allowed, bots));
    LOGI("get my bots ok, userid: %d, bots: %u, allowed: %d", m_userinfo.userid, static_cast<unsigned>(bots.size()),
         allowed ? 1 : 0);
}

void ChatSession::onSaveMyBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    BotRecord rec;
    if (!botproto::ParseSaveMyBotRequest(body, rec))
    {
        send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bad_request, "bad request", 0));
        return;
    }
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    FeatureSwitchManager& fs = Singleton<FeatureSwitchManager>::Instance();
    if (!fs.isEnabled(ent, kFeatureBot) || !fs.isEnabled(ent, kFeatureBotPersonal))
    {
        send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bot_disabled, "personal bot disabled", 0));
        return;
    }
    rec.m_ownerType = bot::bot_owner_personal;
    rec.m_ownerId = m_userinfo.userid;
    rec.m_enterpriseId = ent;
    rec.m_scope = bot::bot_scope_all;
    rec.m_groupIds.clear();
    rec.m_allowGroupOff = false;
    rec.m_source = bot::bot_source_rules;
    std::string why;
    const int check = bot::ValidateBot(rec, false, std::vector<std::string>(), why);
    if (check != bot::bot_check_ok)
    {
        LOGW("save my bot rejected, userid: %d, reason: %s", m_userinfo.userid, why.c_str());
        send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(MapCheck(check), why, 0));
        return;
    }
    std::vector<std::string> texts;
    botproto::CollectTexts(rec, texts);
    if (hitSensitive(chatserver::sensitive_source_bot_config, 0, 0, std::string(), texts))
    {
        send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bot_sensitive_word, "sensitive word", 0));
        return;
    }
    BotManager& mgr = Singleton<BotManager>::Instance();
    std::shared_ptr<const BotSnapshot> snap = mgr.snapshot();
    const int64_t now = NowMs();
    int stored = bot::bot_store_ok;
    if (rec.m_botId > 0)
    {
        BotSnapshot::BotPtr exist = snap->find(rec.m_botId);
        if (!exist || exist->m_ownerType != bot::bot_owner_personal || exist->m_ownerId != m_userinfo.userid
            || exist->m_source != bot::bot_source_rules)
        {
            send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bot_not_exist, "bot not exist", 0));
            return;
        }
        stored = mgr.updateBot(rec, now);
    }
    else
    {
        if (CountPersonalBots(*snap, m_userinfo.userid) >= bot::kPersonalBotMax)
        {
            send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bot_full, "personal bot limit reached", 0));
            return;
        }
        rec.m_creatorType = bot::bot_creator_user;
        rec.m_creatorId = m_userinfo.userid;
        rec.m_creatorName = m_userinfo.nickname;
        rec.m_webhookToken = bot::RandomHex32();
        rec.m_secret = bot::RandomHex32();
        stored = mgr.createBot(rec, now);
    }
    if (stored != bot::bot_store_ok)
    {
        send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_server_internal, "save bot failed", 0));
        return;
    }
    send(cmd_save_my_bot, m_seq, botproto::BuildMyBotResponse(0, "ok", rec.m_botId));
    LOGI("save my bot ok, userid: %d, botid: %lld, rules: %u", m_userinfo.userid, (long long)rec.m_botId,
         static_cast<unsigned>(rec.m_rules.size()));
}

void ChatSession::onDeleteMyBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t botId = 0;
    if (!botproto::ParseBotIdRequest(body, botId))
    {
        send(cmd_delete_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bad_request, "bad request", 0));
        return;
    }
    BotManager& mgr = Singleton<BotManager>::Instance();
    BotSnapshot::BotPtr exist = mgr.snapshot()->find(botId);
    if (!exist || exist->m_ownerType != bot::bot_owner_personal || exist->m_ownerId != m_userinfo.userid
        || exist->m_source != bot::bot_source_rules)
    {
        send(cmd_delete_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_bot_not_exist, "bot not exist", botId));
        return;
    }
    if (mgr.deleteBot(botId, NowMs()) != bot::bot_store_ok)
    {
        send(cmd_delete_my_bot, m_seq, botproto::BuildMyBotResponse(error_code_server_internal, "delete failed", botId));
        return;
    }
    send(cmd_delete_my_bot, m_seq, botproto::BuildMyBotResponse(0, "ok", botId));
    LOGI("delete my bot ok, userid: %d, botid: %lld", m_userinfo.userid, (long long)botId);
}

void ChatSession::onTryBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    botproto::TryRequest req;
    if (!botproto::ParseTryRequest(body, req))
    {
        send(cmd_try_bot, m_seq, botproto::BuildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    BotRecord target;
    const int64_t groupid = req.m_input.m_groupid;
    if (req.m_hasBot)
    {
        //尚未保存的机器人：带群号的是本群的群机器人草稿（须为群主），不带的是本人的个人机器人草稿
        target = req.m_bot;
        if (groupid > 0)
        {
            GroupRecord group;
            if (!Singleton<GroupManager>::Instance().getGroup(groupid, group) || group.ownerid != m_userinfo.userid)
            {
                send(cmd_try_bot, m_seq, botproto::BuildSimpleResponse(error_code_bot_no_permission, "no permission"));
                return;
            }
            target.m_ownerType = bot::bot_owner_group;
            target.m_ownerId = groupid;
            target.m_enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
        }
        else
        {
            target.m_ownerType = bot::bot_owner_personal;
            target.m_ownerId = m_userinfo.userid;
            target.m_enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
        }
    }
    else
    {
        BotSnapshot::BotPtr b = Singleton<BotManager>::Instance().snapshot()->find(req.m_botId);
        bool allowed = false;
        if (b && b->m_ownerType == bot::bot_owner_personal)
        {
            allowed = b->m_ownerId == m_userinfo.userid;
        }
        else if (b && b->m_ownerType == bot::bot_owner_group)
        {
            GroupRecord group;
            allowed = Singleton<GroupManager>::Instance().getGroup(b->m_ownerId, group)
                      && group.ownerid == m_userinfo.userid;
        }
        if (!allowed)
        {
            send(cmd_try_bot, m_seq, botproto::BuildSimpleResponse(error_code_bot_no_permission, "no permission"));
            return;
        }
        target = *b;
        if (target.m_ownerType == bot::bot_owner_group && req.m_input.m_groupid <= 0)
            req.m_input.m_groupid = target.m_ownerId;
    }
    const bot::BotDecision d = Singleton<BotEngine>::Instance().tryBot(target, req.m_input);
    send(cmd_try_bot, m_seq, botproto::BuildTryResponse(0, "ok", d));
    LOGI("try bot ok, userid: %d, botid: %lld, matched: %d, reason: %d", m_userinfo.userid, (long long)target.m_botId,
         d.m_kind != bot::bot_decision_none ? 1 : 0, d.m_reason);
}

void ChatSession::onGetAutoReply(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)body;
    (void)conn;
    const int64_t ent = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    FeatureSwitchManager& fs = Singleton<FeatureSwitchManager>::Instance();
    const bool allowed = fs.isEnabled(ent, kFeatureBot) && fs.isEnabled(ent, kFeatureBotPersonal);
    BotSnapshot::BotPtr preset = FindAutoReplyBot(*Singleton<BotManager>::Instance().snapshot(), m_userinfo.userid);
    bot::AutoReplySettings s;
    if (preset)
        s = bot::RulesToAutoReply(preset->m_rules);
    send(cmd_get_auto_reply, m_seq, botproto::BuildAutoReplyResponse(0, "ok", preset ? true : false, allowed, s));
    LOGI("get auto reply ok, userid: %d, exists: %d", m_userinfo.userid, preset ? 1 : 0);
}

void ChatSession::onSetAutoReply(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    bot::AutoReplySettings s;
    if (!botproto::ParseAutoReplyRequest(body, s))
    {
        send(cmd_set_auto_reply, m_seq, botproto::BuildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    std::vector<std::string> texts;
    if (!s.m_awayDefault)
        texts.push_back(s.m_awayText);
    if (!s.m_busyDefault)
        texts.push_back(s.m_busyText);
    if (!texts.empty() && hitSensitive(chatserver::sensitive_source_bot_config, 0, 0, std::string(), texts))
    {
        send(cmd_set_auto_reply, m_seq, botproto::BuildSimpleResponse(error_code_bot_sensitive_word, "sensitive word"));
        return;
    }
    BotManager& mgr = Singleton<BotManager>::Instance();
    BotSnapshot::BotPtr preset = FindAutoReplyBot(*mgr.snapshot(), m_userinfo.userid);
    const int64_t now = NowMs();
    int stored = bot::bot_store_ok;
    if (preset)
    {
        BotRecord upd = *preset;
        upd.m_rules = bot::AutoReplyToRules(s);
        //沿用原规则的 id，保留创建时间
        for (size_t i = 0; i < upd.m_rules.size() && i < preset->m_rules.size(); ++i)
            upd.m_rules[i].m_id = preset->m_rules[i].m_id;
        stored = mgr.updateBot(upd, now);
    }
    else
    {
        BotRecord rec;
        rec.m_ownerType = bot::bot_owner_personal;
        rec.m_ownerId = m_userinfo.userid;
        rec.m_enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
        rec.m_name = kAutoReplyBotName;
        rec.m_source = bot::bot_source_autoreply;
        rec.m_creatorType = bot::bot_creator_user;
        rec.m_creatorId = m_userinfo.userid;
        rec.m_creatorName = m_userinfo.nickname;
        rec.m_rules = bot::AutoReplyToRules(s);
        stored = mgr.createBot(rec, now);
    }
    if (stored != bot::bot_store_ok)
    {
        send(cmd_set_auto_reply, m_seq, botproto::BuildSimpleResponse(error_code_server_internal, "save failed"));
        return;
    }
    send(cmd_set_auto_reply, m_seq, botproto::BuildSimpleResponse(0, "ok"));
    LOGI("set auto reply ok, userid: %d, away: %d, busy: %d", m_userinfo.userid, s.m_awayOn ? 1 : 0, s.m_busyOn ? 1 : 0);
}
