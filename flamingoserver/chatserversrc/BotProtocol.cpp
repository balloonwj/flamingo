/**
 *  机器人客户端命令的编解码实现，BotProtocol.cpp。约定见 BotProtocol.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotProtocol.h"

#include <stdlib.h>
#include <memory>

#include "Protocol.h"

namespace botproto {

namespace {

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

//紧凑 JSON
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, v);
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

//读一个布尔：true / 1 / "1" 为真
bool ReadFlag(const Json::Value& v, bool def)
{
    if (v.isBool())
        return v.asBool();
    if (v.isIntegral())
        return v.asInt64() != 0;
    if (v.isString())
        return v.asString() == "1" || v.asString() == "true";
    return def;
}

//读一个字符串；不是字符串时取空
std::string ReadString(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

//数字转字符串
std::string Str(int64_t v)
{
    return std::to_string(static_cast<long long>(v));
}

//应答的公共部分
Json::Value Head(int code, const std::string& msg)
{
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    return v;
}

//机器人相关提示的中文正文（客户端认得 tipkind 时按界面语言重写）
const char* const kTipAddedFmtA = u8" 添加了机器人「";
const char* const kTipRemovedFmtA = u8" 删除了机器人「";
const char* const kTipOffFmtA = u8" 在本群关闭了机器人「";
const char* const kTipOnFmtA = u8" 在本群打开了机器人「";
const char* const kTipCloseQuote = u8"」";
const char* const kTipExternalSuffix = u8"，@ 它的消息会发送到外部服务";
const char* const kTipDefaultOperator = u8"群主";

} // anonymous namespace

bool ParseBotIdRequest(const std::string& body, int64_t& botId)
{
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    botId = ReadId(root["botid"]);
    return botId > 0;
}

bool ParseGroupBotIdRequest(const std::string& body, int64_t& groupid, int64_t& botId)
{
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    groupid = ReadId(root["groupid"]);
    botId = ReadId(root["botid"]);
    return groupid > 0 && botId > 0;
}

bool ParseSetGroupBotOffRequest(const std::string& body, int64_t& groupid, int64_t& botId, bool& off)
{
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    groupid = ReadId(root["groupid"]);
    botId = ReadId(root["botid"]);
    off = ReadFlag(root["off"], false);
    return groupid > 0 && botId > 0;
}

bool ParseSaveGroupBotRequest(const std::string& body, int64_t& groupid, bot::BotRecord& rec)
{
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    groupid = ReadId(root["groupid"]);
    if (groupid <= 0)
        return false;
    return bot::BotFromJson(root["bot"], rec);
}

bool ParseSaveMyBotRequest(const std::string& body, bot::BotRecord& rec)
{
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    return bot::BotFromJson(root["bot"], rec);
}

bool ParseTryRequest(const std::string& body, TryRequest& out)
{
    out = TryRequest();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    out.m_input.m_groupid = ReadId(root["groupid"]);
    out.m_botId = ReadId(root["botid"]);
    if (root["bot"].isObject())
    {
        if (!bot::BotFromJson(root["bot"], out.m_bot))
            return false;
        out.m_hasBot = true;
    }
    if (out.m_botId <= 0 && !out.m_hasBot)
        return false;
    const Json::Value& in = root["input"];
    if (!in.isObject())
        return false;
    out.m_input.m_text = ReadString(in["text"]);
    out.m_input.m_at = ReadFlag(in["at"], true);
    out.m_input.m_presence = static_cast<int>(ReadId(in["presence"]));
    out.m_input.m_senderId = ReadId(in["senderid"]);
    out.m_input.m_timeMs = ReadId(in["time"]);
    return true;
}

bool ParseAutoReplyRequest(const std::string& body, bot::AutoReplySettings& out)
{
    out = bot::AutoReplySettings();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    out.m_awayOn = ReadFlag(root["awayon"], false);
    out.m_awayText = ReadString(root["awaytext"]);
    out.m_awayDefault = ReadFlag(root["awaydefault"], true);
    out.m_busyOn = ReadFlag(root["busyon"], false);
    out.m_busyText = ReadString(root["busytext"]);
    out.m_busyDefault = ReadFlag(root["busydefault"], true);
    return bot::Utf8Length(out.m_awayText) <= bot::kAutoReplyTextMaxChars
           && bot::Utf8Length(out.m_busyText) <= bot::kAutoReplyTextMaxChars;
}

std::string BuildSimpleResponse(int code, const std::string& msg)
{
    return WriteCompact(Head(code, msg));
}

std::string BuildGroupBotsResponse(int code, const std::string& msg, int64_t groupid, const GroupBotCaps& caps,
                                   const std::vector<GroupBotView>& bots)
{
    Json::Value v = Head(code, msg);
    v["groupid"] = Str(groupid);
    Json::Value c;
    c["isowner"] = caps.m_isOwner ? 1 : 0;
    c["groupbot"] = caps.m_groupBotAllowed ? 1 : 0;
    c["external"] = caps.m_externalAllowed ? 1 : 0;
    c["groupbotmax"] = caps.m_groupBotMax;
    v["caps"] = c;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        const GroupBotView& gv = bots[i];
        if (!gv.m_bot)
            continue;
        Json::Value b = bot::BotToJson(*gv.m_bot, false, false);
        b["source"] = gv.m_bot->m_ownerType == bot::bot_owner_group ? "group" : "enterprise";
        b["off"] = gv.m_off ? 1 : 0;
        b["active"] = gv.m_active ? 1 : 0;
        b["canoff"] = gv.m_canOff ? 1 : 0;
        b["atable"] = bot::IsAtable(*gv.m_bot) ? 1 : 0;
        b["usage"] = bot::BuildUsageJson(*gv.m_bot);
        arr.append(b);
    }
    v["bots"] = arr;
    return WriteCompact(v);
}

std::string BuildBotDetailResponse(int code, const std::string& msg, const bot::BotRecord* b, bool withSecrets,
                                   const std::string& webhookUrl)
{
    Json::Value v = Head(code, msg);
    if (b != NULL)
    {
        v["bot"] = bot::BotToJson(*b, true, withSecrets);
        //对外地址只随令牌一起给有编辑权限的人
        if (withSecrets && !webhookUrl.empty())
            v["bot"]["webhookurl"] = webhookUrl;
    }
    return WriteCompact(v);
}

std::string BuildGroupBotResponse(int code, const std::string& msg, int64_t groupid, int64_t botId)
{
    Json::Value v = Head(code, msg);
    v["groupid"] = Str(groupid);
    v["botid"] = Str(botId);
    return WriteCompact(v);
}

std::string BuildSetOffResponse(int code, const std::string& msg, int64_t groupid, int64_t botId, bool off)
{
    Json::Value v = Head(code, msg);
    v["groupid"] = Str(groupid);
    v["botid"] = Str(botId);
    v["off"] = off ? 1 : 0;
    return WriteCompact(v);
}

std::string BuildMyBotsResponse(int code, const std::string& msg, bool allowed,
                                const std::vector<std::shared_ptr<const bot::BotRecord> >& bots)
{
    Json::Value v = Head(code, msg);
    v["allowed"] = allowed ? 1 : 0;
    v["max"] = bot::kPersonalBotMax;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i] && bots[i]->m_source != bot::bot_source_autoreply)
            arr.append(bot::BotToJson(*bots[i], true, false));
    }
    v["bots"] = arr;
    return WriteCompact(v);
}

std::string BuildMyBotResponse(int code, const std::string& msg, int64_t botId)
{
    Json::Value v = Head(code, msg);
    v["botid"] = Str(botId);
    return WriteCompact(v);
}

std::string BuildTryResponse(int code, const std::string& msg, const bot::BotDecision& d)
{
    Json::Value v = Head(code, msg);
    v["matched"] = d.m_kind != bot::bot_decision_none ? 1 : 0;
    v["kind"] = d.m_kind;
    v["ruleid"] = Str(d.m_ruleId);
    v["rulename"] = d.m_ruleName;
    v["text"] = d.m_text;
    v["markdown"] = d.m_markdown ? 1 : 0;
    v["url"] = d.m_url;
    v["reason"] = d.m_reason;
    return WriteCompact(v);
}

std::string BuildAutoReplyResponse(int code, const std::string& msg, bool exists, bool allowed,
                                   const bot::AutoReplySettings& s)
{
    Json::Value v = Head(code, msg);
    v["exists"] = exists ? 1 : 0;
    v["allowed"] = allowed ? 1 : 0;
    v["awayon"] = s.m_awayOn ? 1 : 0;
    v["awaytext"] = s.m_awayText;
    v["awaydefault"] = s.m_awayDefault ? 1 : 0;
    v["busyon"] = s.m_busyOn ? 1 : 0;
    v["busytext"] = s.m_busyText;
    v["busydefault"] = s.m_busyDefault ? 1 : 0;
    return WriteCompact(v);
}

std::string BuildChangedNotify(int64_t groupid)
{
    Json::Value v;
    v["groupid"] = Str(groupid);
    return WriteCompact(v);
}

std::string BuildBotTipContent(const char* tipkind, const std::string& operatorName, const std::string& botName,
                               bool external)
{
    const std::string who = operatorName.empty() ? std::string(kTipDefaultOperator) : operatorName;
    const std::string kind = tipkind;
    std::string text;
    if (kind == kSystemTipBotAdded)
        text = who + kTipAddedFmtA + botName + kTipCloseQuote + (external ? kTipExternalSuffix : "");
    else if (kind == kSystemTipBotRemoved)
        text = who + kTipRemovedFmtA + botName + kTipCloseQuote;
    else if (kind == kSystemTipBotOff)
        text = who + kTipOffFmtA + botName + kTipCloseQuote;
    else
        text = who + kTipOnFmtA + botName + kTipCloseQuote;
    Json::Value root;
    root["text"] = text;
    root["tipkind"] = tipkind;
    Json::Value args(Json::objectValue);
    if (!operatorName.empty())
        args["operator"] = operatorName;
    args["bot"] = botName;
    if (external)
        args["external"] = "1";
    root["tipargs"] = args;
    return WriteCompact(root);
}

void CollectTexts(const bot::BotRecord& b, std::vector<std::string>& out)
{
    out.clear();
    out.push_back(b.m_name);
    if (!b.m_desc.empty())
        out.push_back(b.m_desc);
    for (size_t i = 0; i < b.m_rules.size(); ++i)
    {
        const bot::BotRule& r = b.m_rules[i];
        if (!r.m_name.empty())
            out.push_back(r.m_name);
        for (size_t k = 0; k < r.m_trigger.m_keywords.size(); ++k)
            out.push_back(r.m_trigger.m_keywords[k]);
        if (!r.m_action.m_text.empty())
            out.push_back(r.m_action.m_text);
    }
}

} // namespace botproto
