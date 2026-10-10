/**
 *  机器人的报文编解码实现（客户端侧）。约定见 BotProtocolClient.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotProtocolClient.h"
#include <stdlib.h>
#include <memory>
#include "Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

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

//读一个整数：数字或数字字符串
int64_t ReadInt64(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    return 0;
}

//读一个开关：true / 非零数字 / "1"
bool ReadFlag(const Json::Value& v)
{
    if (v.isBool())
        return v.asBool();
    return ReadInt64(v) != 0;
}

//读一个字符串
std::string ReadString(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

//解析用法说明的一项
BotUsageItem ParseUsage(const Json::Value& v)
{
    BotUsageItem u;
    u.m_type = static_cast<int>(ReadInt64(v["t"]));
    u.m_qa = ReadFlag(v["qa"]);
    const Json::Value& kws = v["kw"];
    if (kws.isArray())
    {
        for (Json::ArrayIndex i = 0; i < kws.size(); ++i)
        {
            if (kws[i].isString())
                u.m_keywords.push_back(kws[i].asString());
        }
    }
    u.m_match = v.isMember("m") ? static_cast<int>(ReadInt64(v["m"])) : 1;
    u.m_needAt = v.isMember("at") ? ReadFlag(v["at"]) : true;
    u.m_scheduleKind = v.isMember("k") ? static_cast<int>(ReadInt64(v["k"])) : 1;
    u.m_time = ReadString(v["time"]);
    const Json::Value& wd = v["wd"];
    if (wd.isArray())
    {
        for (Json::ArrayIndex i = 0; i < wd.size(); ++i)
            u.m_weekdays.push_back(static_cast<int>(ReadInt64(wd[i])));
    }
    u.m_monthDay = v.isMember("md") ? static_cast<int>(ReadInt64(v["md"])) : 1;
    return u;
}

//机器人命令里交给 FlamingoBot.dll 的那一段：2802（详情）至 2809（试一试）
const uint16_t kDllFrameFirst = 2802;
const uint16_t kDllFrameLast = 2809;

} // anonymous namespace

std::string BotProtocolClient::buildGetGroupBotsRequest(int64_t groupid)
{
    Json::Value v;
    v["groupid"] = std::to_string(static_cast<long long>(groupid));
    return WriteCompact(v);
}

bool BotProtocolClient::parseGetGroupBotsResponse(const std::string& body, GroupBotsResult& out)
{
    out = GroupBotsResult();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    out.m_code = static_cast<int>(ReadInt64(root["code"]));
    out.m_msg = ReadString(root["msg"]);
    out.m_groupid = ReadInt64(root["groupid"]);
    const Json::Value& caps = root["caps"];
    if (caps.isObject())
    {
        out.m_caps.m_isOwner = ReadFlag(caps["isowner"]);
        out.m_caps.m_groupBotAllowed = ReadFlag(caps["groupbot"]);
        out.m_caps.m_externalAllowed = ReadFlag(caps["external"]);
        out.m_caps.m_groupBotMax = static_cast<int>(ReadInt64(caps["groupbotmax"]));
    }
    const Json::Value& bots = root["bots"];
    if (!bots.isArray())
        return true;
    for (Json::ArrayIndex i = 0; i < bots.size(); ++i)
    {
        const Json::Value& b = bots[i];
        if (!b.isObject())
            continue;
        GroupBotInfo info;
        info.m_botId = ReadInt64(b["botid"]);
        info.m_userId = ReadInt64(b["userid"]);
        info.m_ownerType = static_cast<int>(ReadInt64(b["ownertype"]));
        info.m_isGroupBot = ReadString(b["source"]) == "group";
        info.m_name = ReadString(b["name"]);
        info.m_desc = ReadString(b["desc"]);
        info.m_glyph = ReadString(b["glyph"]);
        info.m_color = ReadString(b["color"]);
        info.m_status = static_cast<int>(ReadInt64(b["status"]));
        info.m_off = ReadFlag(b["off"]);
        info.m_active = ReadFlag(b["active"]);
        info.m_canOff = ReadFlag(b["canoff"]);
        info.m_atable = ReadFlag(b["atable"]);
        const Json::Value& usage = b["usage"];
        if (usage.isArray())
        {
            for (Json::ArrayIndex k = 0; k < usage.size(); ++k)
            {
                if (usage[k].isObject())
                    info.m_usage.push_back(ParseUsage(usage[k]));
            }
        }
        if (info.m_botId > 0)
            out.m_bots.push_back(info);
    }
    return true;
}

bool BotProtocolClient::parseGroupBotsChangedNotify(const std::string& body, int64_t& groupid)
{
    groupid = 0;
    Json::Value root;
    if (!ParseObject(body, root) || !root.isMember("groupid"))
        return false;
    groupid = ReadInt64(root["groupid"]);
    return groupid >= 0;
}

std::string BotProtocolClient::buildGetAutoReplyRequest()
{
    return "{}";
}

bool BotProtocolClient::parseGetAutoReplyResponse(const std::string& body, AutoReplyResult& out)
{
    out = AutoReplyResult();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    out.m_code = static_cast<int>(ReadInt64(root["code"]));
    out.m_msg = ReadString(root["msg"]);
    out.m_exists = ReadFlag(root["exists"]);
    out.m_allowed = ReadFlag(root["allowed"]);
    out.m_settings.m_awayOn = ReadFlag(root["awayon"]);
    out.m_settings.m_awayText = ReadString(root["awaytext"]);
    out.m_settings.m_awayDefault = root.isMember("awaydefault") ? ReadFlag(root["awaydefault"]) : true;
    out.m_settings.m_busyOn = ReadFlag(root["busyon"]);
    out.m_settings.m_busyText = ReadString(root["busytext"]);
    out.m_settings.m_busyDefault = root.isMember("busydefault") ? ReadFlag(root["busydefault"]) : true;
    return true;
}

std::string BotProtocolClient::buildSetAutoReplyRequest(const AutoReplyWire& s)
{
    Json::Value v;
    v["awayon"] = s.m_awayOn ? 1 : 0;
    v["awaytext"] = s.m_awayText;
    v["awaydefault"] = s.m_awayDefault ? 1 : 0;
    v["busyon"] = s.m_busyOn ? 1 : 0;
    v["busytext"] = s.m_busyText;
    v["busydefault"] = s.m_busyDefault ? 1 : 0;
    return WriteCompact(v);
}

bool BotProtocolClient::parseSimpleResponse(const std::string& body, int& code, std::string& msg)
{
    code = -1;
    msg.clear();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    code = static_cast<int>(ReadInt64(root["code"]));
    msg = ReadString(root["msg"]);
    return true;
}

bool BotProtocolClient::isDllFrame(uint16_t cmd)
{
    return cmd >= kDllFrameFirst && cmd <= kDllFrameLast;
}

} // namespace net
