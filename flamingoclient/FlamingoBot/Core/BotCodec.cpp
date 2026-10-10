/**
 *  2802～2809 的请求构造与应答解析，见 BotCodec.h。字段名与服务端 chatserversrc/BotProtocol.cpp、BotRules.cpp 一一对应。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotCodec.h"

#include <stdlib.h>
#include <memory>

#include "BotStrings.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace flamingobot {

namespace {

//64 位整数十进制文本的最大长度（不含结尾 0），超过即视为不合法
const size_t kMaxIdTextLen = 20;

//把 Json::Value 写成紧凑 JSON（无缩进、无换行）
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本；失败或顶层不是对象返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    return ok && root.isObject();
}

//64 位整数写成十进制字符串（报文里的 ID 一律用字符串）
std::string IdText(int64_t id)
{
    return std::to_string(static_cast<long long>(id));
}

//读一个 ID：十进制字符串与整数两种写法都接受；其余返回 0
int64_t ReadId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty() || text.size() > kMaxIdTextLen)
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(_atoi64(text.c_str()));
    }
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//读一个整数：整数或数字字符串；其余返回 def
int ReadInt(const Json::Value& v, int def)
{
    if (v.isIntegral())
        return v.asInt();
    if (v.isString() && !v.asString().empty())
        return atoi(v.asString().c_str());
    return def;
}

//读一个 64 位整数（时间戳）；其余返回 0
int64_t ReadInt64(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString() && !v.asString().empty())
        return static_cast<int64_t>(_atoi64(v.asString().c_str()));
    return 0;
}

//读一个布尔：true / 非 0 整数 / "1" / "true" 为真；缺失时为 def
bool ReadBool(const Json::Value& v, bool def)
{
    if (v.isBool())
        return v.asBool();
    if (v.isIntegral())
        return v.asInt64() != 0;
    if (v.isString())
        return v.asString() == "1" || v.asString() == "true";
    return def;
}

//读一个字符串；不是字符串时为空
std::string ReadString(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

//读一组 ID
void ReadIdArray(const Json::Value& v, std::vector<int64_t>& out)
{
    out.clear();
    if (!v.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
    {
        const int64_t id = ReadId(v[i]);
        if (id > 0)
            out.push_back(id);
    }
}

//读一组整数
void ReadIntArray(const Json::Value& v, std::vector<int>& out)
{
    out.clear();
    if (!v.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
    {
        if (v[i].isIntegral())
            out.push_back(v[i].asInt());
    }
}

//一组 ID 写成字符串数组
Json::Value IdArrayToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(IdText(ids[i]));
    return arr;
}

//一组整数写成整数数组
Json::Value IntArrayToJson(const std::vector<int>& values)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < values.size(); ++i)
        arr.append(values[i]);
    return arr;
}

//「HH:MM」解析成分钟，解析不出时为 -1（与服务端 TriggerFromJson 相同）
int ReadMinute(const Json::Value& v)
{
    int minute = 0;
    if (!ParseHhMm(ReadString(v), minute))
        return -1;
    return minute;
}

//触发方式与附加条件 → JSON（与服务端 TriggerToJson 相同的字段）
Json::Value TriggerToValue(const BotTrigger& t)
{
    Json::Value v;
    v["type"] = t.m_type;
    Json::Value kws(Json::arrayValue);
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
        kws.append(t.m_keywords[i]);
    v["keywords"] = kws;
    v["match"] = t.m_match;
    v["needat"] = t.m_needAt ? 1 : 0;
    Json::Value sch;
    sch["kind"] = t.m_schedule.m_kind;
    sch["time"] = FormatHhMm(t.m_schedule.m_minuteOfDay);
    sch["weekdays"] = IntArrayToJson(t.m_schedule.m_weekdays);
    sch["monthday"] = t.m_schedule.m_monthDay;
    v["schedule"] = sch;
    v["groupids"] = IdArrayToJson(t.m_scheduleGroups);
    v["presence"] = IntArrayToJson(t.m_presence);
    Json::Value win;
    win["on"] = t.m_window.m_enabled ? 1 : 0;
    win["days"] = t.m_window.m_days;
    win["from"] = FormatHhMm(t.m_window.m_fromMinute);
    win["to"] = FormatHhMm(t.m_window.m_toMinute);
    v["window"] = win;
    Json::Value snd;
    snd["kind"] = t.m_senders.m_kind;
    snd["users"] = IdArrayToJson(t.m_senders.m_users);
    snd["depts"] = IdArrayToJson(t.m_senders.m_depts);
    v["senders"] = snd;
    v["cooldown"] = t.m_cooldownMinutes;
    v["ownerquiet"] = t.m_ownerQuietMinutes;
    return v;
}

//JSON → 触发方式与附加条件（与服务端 TriggerFromJson 相同的缺省值）；触发方式不在 1～6 时返回 false
bool ValueToTrigger(const Json::Value& v, BotTrigger& t)
{
    t = BotTrigger();
    if (!v.isObject())
        return false;
    t.m_type = ReadInt(v["type"], 0);
    if (t.m_type < bot_trigger_at || t.m_type > bot_trigger_webhook)
        return false;
    const Json::Value& kws = v["keywords"];
    if (kws.isArray())
    {
        for (Json::ArrayIndex i = 0; i < kws.size(); ++i)
        {
            if (kws[i].isString())
                t.m_keywords.push_back(kws[i].asString());
        }
    }
    t.m_match = ReadInt(v["match"], bot_match_contains);
    t.m_needAt = ReadBool(v["needat"], true);
    const Json::Value& sch = v["schedule"];
    if (sch.isObject())
    {
        t.m_schedule.m_kind = ReadInt(sch["kind"], bot_schedule_daily);
        t.m_schedule.m_minuteOfDay = ReadMinute(sch["time"]);
        ReadIntArray(sch["weekdays"], t.m_schedule.m_weekdays);
        t.m_schedule.m_monthDay = ReadInt(sch["monthday"], 1);
    }
    ReadIdArray(v["groupids"], t.m_scheduleGroups);
    ReadIntArray(v["presence"], t.m_presence);
    const Json::Value& win = v["window"];
    if (win.isObject())
    {
        t.m_window.m_enabled = ReadBool(win["on"], false);
        t.m_window.m_days = ReadInt(win["days"], bot_days_every);
        t.m_window.m_fromMinute = ReadMinute(win["from"]);
        t.m_window.m_toMinute = ReadMinute(win["to"]);
    }
    const Json::Value& snd = v["senders"];
    if (snd.isObject())
    {
        t.m_senders.m_kind = ReadInt(snd["kind"], bot_sender_all);
        ReadIdArray(snd["users"], t.m_senders.m_users);
        ReadIdArray(snd["depts"], t.m_senders.m_depts);
    }
    t.m_cooldownMinutes = ReadInt(v["cooldown"], 0);
    t.m_ownerQuietMinutes = ReadInt(v["ownerquiet"], 0);
    return true;
}

//动作 → JSON（与服务端 ActionToJson 相同的字段）
Json::Value ActionToValue(const BotAction& a)
{
    Json::Value v;
    v["type"] = a.m_type;
    v["text"] = a.m_text;
    v["markdown"] = a.m_markdown ? 1 : 0;
    v["url"] = a.m_url;
    if (a.m_defaultText)
        v["defaulttext"] = 1;
    return v;
}

//JSON → 动作；动作不在 1～3 时返回 false
bool ValueToAction(const Json::Value& v, BotAction& a)
{
    a = BotAction();
    if (!v.isObject())
        return false;
    a.m_type = ReadInt(v["type"], 0);
    if (a.m_type < bot_action_reply || a.m_type > bot_action_external)
        return false;
    a.m_text = ReadString(v["text"]);
    a.m_markdown = ReadBool(v["markdown"], false);
    a.m_url = ReadString(v["url"]);
    a.m_defaultText = ReadBool(v["defaulttext"], false);
    return true;
}

//规则 → JSON（与服务端 RuleToJson 相同的字段）
Json::Value RuleToValue(const BotRule& r)
{
    Json::Value v;
    v["id"] = IdText(r.m_id);
    v["name"] = r.m_name;
    v["enabled"] = r.m_enabled ? 1 : 0;
    v["trigger"] = TriggerToValue(r.m_trigger);
    v["action"] = ActionToValue(r.m_action);
    return v;
}

//JSON → 规则（与服务端 RuleFromJson 相同：外部系统推送的规则可以没有动作）
bool ValueToRule(const Json::Value& v, BotRule& r)
{
    r = BotRule();
    if (!v.isObject())
        return false;
    r.m_id = ReadId(v["id"]);
    r.m_name = ReadString(v["name"]);
    r.m_enabled = ReadBool(v["enabled"], true);
    if (!ValueToTrigger(v["trigger"], r.m_trigger))
        return false;
    if (!ValueToAction(v["action"], r.m_action))
    {
        if (r.m_trigger.m_type != bot_trigger_webhook)
            return false;
        r.m_action = BotAction();
    }
    return true;
}

//机器人 → 保存请求里的 bot（只写可编辑字段）
Json::Value BotToSaveValue(const BotRecord& b)
{
    Json::Value v;
    v["botid"] = IdText(b.m_botId);
    v["name"] = b.m_name;
    v["desc"] = b.m_desc;
    v["glyph"] = b.m_glyph;
    v["color"] = b.m_color;
    v["status"] = b.m_status;
    Json::Value rules(Json::arrayValue);
    for (size_t i = 0; i < b.m_rules.size(); ++i)
        rules.append(RuleToValue(b.m_rules[i]));
    v["rules"] = rules;
    return v;
}

//JSON → 机器人（与服务端 BotToJson 的字段对应；规则解析不出的条目跳过）
bool ValueToBot(const Json::Value& v, BotRecord& b)
{
    b = BotRecord();
    if (!v.isObject())
        return false;
    b.m_botId = ReadId(v["botid"]);
    b.m_userId = ReadId(v["userid"]);
    b.m_enterpriseId = ReadId(v["enterpriseid"]);
    b.m_ownerType = ReadInt(v["ownertype"], bot_owner_group);
    b.m_ownerId = ReadId(v["ownerid"]);
    b.m_name = ReadString(v["name"]);
    b.m_desc = ReadString(v["desc"]);
    b.m_glyph = ReadString(v["glyph"]);
    b.m_color = ReadString(v["color"]);
    b.m_scope = ReadInt(v["scope"], 0);
    ReadIdArray(v["groupids"], b.m_groupIds);
    b.m_allowGroupOff = ReadBool(v["allowgroupoff"], false);
    b.m_status = ReadInt(v["status"], bot_status_active);
    b.m_source = ReadInt(v["source"], bot_source_rules);
    b.m_creatorName = ReadString(v["creatorname"]);
    b.m_createTime = ReadInt64(v["createtime"]);
    b.m_updateTime = ReadInt64(v["updatetime"]);
    b.m_lastFire = ReadInt64(v["lastfire"]);
    b.m_ruleCount = ReadInt(v["rulecount"], 0);
    const Json::Value& rules = v["rules"];
    b.m_hasRules = rules.isArray();
    if (b.m_hasRules)
    {
        for (Json::ArrayIndex i = 0; i < rules.size(); ++i)
        {
            BotRule r;
            if (ValueToRule(rules[i], r))
                b.m_rules.push_back(r);
        }
        b.m_ruleCount = static_cast<int>(b.m_rules.size());
    }
    b.m_webhookToken = ReadString(v["webhooktoken"]);
    b.m_secret = ReadString(v["secret"]);
    b.m_webhookUrl = ReadString(v["webhookurl"]);
    return true;
}

} // namespace

std::string BotCodec::BuildGetDetailRequest(int64_t botId)
{
    Json::Value v;
    v["botid"] = IdText(botId);
    return WriteCompact(v);
}

std::string BotCodec::BuildSaveGroupBotRequest(int64_t groupId, const BotRecord& bot)
{
    Json::Value v;
    v["groupid"] = IdText(groupId);
    v["bot"] = BotToSaveValue(bot);
    return WriteCompact(v);
}

std::string BotCodec::BuildDeleteGroupBotRequest(int64_t groupId, int64_t botId)
{
    Json::Value v;
    v["groupid"] = IdText(groupId);
    v["botid"] = IdText(botId);
    return WriteCompact(v);
}

std::string BotCodec::BuildSetGroupBotOffRequest(int64_t groupId, int64_t botId, bool off)
{
    Json::Value v;
    v["groupid"] = IdText(groupId);
    v["botid"] = IdText(botId);
    v["off"] = off ? 1 : 0;
    return WriteCompact(v);
}

std::string BotCodec::BuildGetMyBotsRequest()
{
    return WriteCompact(Json::Value(Json::objectValue));
}

std::string BotCodec::BuildSaveMyBotRequest(const BotRecord& bot)
{
    Json::Value v;
    v["bot"] = BotToSaveValue(bot);
    return WriteCompact(v);
}

std::string BotCodec::BuildDeleteMyBotRequest(int64_t botId)
{
    Json::Value v;
    v["botid"] = IdText(botId);
    return WriteCompact(v);
}

std::string BotCodec::BuildTryRequest(int64_t groupId, const BotRecord& draft, const BotTryInput& input)
{
    Json::Value v;
    if (groupId > 0)
        v["groupid"] = IdText(groupId);
    v["bot"] = BotToSaveValue(draft);
    Json::Value in;
    in["text"] = input.m_text;
    if (groupId > 0)
    {
        in["at"] = input.m_at ? 1 : 0;
    }
    else
    {
        in["presence"] = input.m_presence;
        in["senderid"] = IdText(input.m_senderId);
        in["time"] = static_cast<Json::Int64>(input.m_timeMs);
    }
    v["input"] = in;
    return WriteCompact(v);
}

bool BotCodec::ParseReply(uint16_t cmd, uint32_t seq, const std::string& body, BotReply& out)
{
    out = BotReply();
    out.m_cmd = cmd;
    out.m_seq = seq;
    Json::Value root;
    if (!ParseObject(body, root) || !root.isMember("code"))
        return false;
    out.m_parsed = true;
    out.m_code = ReadInt(root["code"], -1);
    out.m_msg = ReadString(root["msg"]);
    switch (cmd)
    {
    //机器人详情
    case bot_cmd_get_detail:
        out.m_hasBot = root["bot"].isObject() && ValueToBot(root["bot"], out.m_bot);
        break;
    //保存、删除群机器人：群号与机器人 id
    case bot_cmd_save_group_bot:
    case bot_cmd_delete_group_bot:
        out.m_groupId = ReadId(root["groupid"]);
        out.m_botId = ReadId(root["botid"]);
        break;
    //在本群关闭 / 打开：另带关闭状态
    case bot_cmd_set_group_bot_off:
        out.m_groupId = ReadId(root["groupid"]);
        out.m_botId = ReadId(root["botid"]);
        out.m_off = ReadBool(root["off"], false);
        break;
    //本人的个人机器人
    case bot_cmd_get_my_bots:
    {
        out.m_allowed = ReadBool(root["allowed"], false);
        out.m_max = ReadInt(root["max"], kPersonalBotMax);
        const Json::Value& bots = root["bots"];
        if (bots.isArray())
        {
            for (Json::ArrayIndex i = 0; i < bots.size(); ++i)
            {
                BotRecord b;
                if (ValueToBot(bots[i], b) && b.m_source != bot_source_autoreply)
                    out.m_bots.push_back(b);
            }
        }
        break;
    }
    //保存、删除个人机器人：机器人 id
    case bot_cmd_save_my_bot:
    case bot_cmd_delete_my_bot:
        out.m_botId = ReadId(root["botid"]);
        break;
    //「试一试」
    case bot_cmd_try:
        out.m_try.m_matched = ReadBool(root["matched"], false);
        out.m_try.m_kind = ReadInt(root["kind"], bot_decision_none);
        out.m_try.m_ruleId = ReadId(root["ruleid"]);
        out.m_try.m_ruleName = ReadString(root["rulename"]);
        out.m_try.m_text = ReadString(root["text"]);
        out.m_try.m_markdown = ReadBool(root["markdown"], false);
        out.m_try.m_url = ReadString(root["url"]);
        out.m_try.m_reason = ReadInt(root["reason"], bot_reason_no_rule);
        break;
    //其余命令只有 code 与 msg
    default:
        break;
    }
    return true;
}

std::string BotCodec::BotToSaveJson(const BotRecord& bot)
{
    return WriteCompact(BotToSaveValue(bot));
}

bool BotCodec::ParseBotJson(const std::string& json, BotRecord& out)
{
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    return ValueToBot(root, out);
}

std::string BotCodec::RuleToJson(const BotRule& rule)
{
    return WriteCompact(RuleToValue(rule));
}

bool BotCodec::ParseRuleJson(const std::string& json, BotRule& out)
{
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    return ValueToRule(root, out);
}

} // namespace flamingobot
