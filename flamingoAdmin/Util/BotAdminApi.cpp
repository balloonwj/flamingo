/**
 *  flamingoAdmin —— 企业机器人接口实现，说明见 BotAdminApi.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Util/BotAdminApi.h"

#include "json.h"      // 第三方 jsoncpp 1.9.0（third-party/jsoncpp）

#include <memory>
#include <string>

namespace flamingoadmin { namespace botapi {

namespace {

// 各接口路径
const wchar_t* const kPathBotList       = L"/admin/bot/list.do";
const wchar_t* const kPathBotDetail     = L"/admin/bot/detail.do";
const wchar_t* const kPathBotSave       = L"/admin/bot/save.do";
const wchar_t* const kPathBotDelete     = L"/admin/bot/delete.do";
const wchar_t* const kPathBotSetStatus  = L"/admin/bot/setstatus.do";
const wchar_t* const kPathBotGroups     = L"/admin/bot/groups.do";
const wchar_t* const kPathBotTry        = L"/admin/bot/try.do";
const wchar_t* const kPathBotResetToken = L"/admin/bot/resettoken.do";
const wchar_t* const kPathBotDomains    = L"/admin/bot/domains.do";
const wchar_t* const kPathBotSetDomains = L"/admin/bot/setdomains.do";

// 各阶段超时（毫秒），与企业问答等接口相同
const int kBotTimeoutMs = 8000;
// HTTP 成功状态码
const int kHttpStatusOk = 200;
// 本地判定的「应答格式不对」
const int kErrLocalBadResponse = -1;
// 机器人对象的 ownertype：1 为企业机器人（本页只管这一种）
const int kOwnerTypeEnterprise = 1;

// 服务端错误码（chatserversrc/AdminApiCodes.h）
const int kErrUnknown          = 1;     // 服务器内部错误（也用于缺少 enterpriseid、企业不存在）
const int kErrPermission       = 6;     // 无权限
const int kErrBotInvalid       = 170;   // 机器人或规则的字段不合法
const int kErrBotNotExist      = 171;   // 机器人不存在、已删除，或不是本企业的企业机器人
const int kErrBotFull          = 172;   // 机器人、规则或关键词超出上限
const int kErrBotGroupInvalid  = 173;   // 所选的群或定时目标群不合规
const int kErrBotNotAllowed    = 174;   // 不允许的触发方式或动作，或外部服务地址不在白名单内
const int kErrBotDomainInvalid = 175;   // 域名白名单不合法
const int kErrBotReadonly      = 176;   // 仓库通知的机器人只读

// save.do 失败时 msg 里「第几条规则」的前缀："rule 2: empty reply"
const char kRulePrefix[] = "rule ";
// 规则下标与原因之间的分隔
const char kRuleReasonSep[] = ": ";

// 服务端校验失败的原因（英文，BotRules::ValidateBot 与 HttpSession 的 msg）与中文说明
struct ReasonText
{
    const char*    m_reason;   // 服务端 msg 里的原因
    const wchar_t* m_text;     // 给管理员看的中文说明
};

const ReasonText kReasonTexts[] =
{
    { "bad name",                         L"名称不能为空，最多 20 个字" },
    { "bad desc or glyph",                L"简介最多 100 个字，头像文字最多 2 个字" },
    { "bad color",                        L"头像底色须为 #RRGGBB 格式" },
    { "bad status",                       L"状态取值不对" },
    { "bad scope",                        L"应用范围取值不对" },
    { "selected scope without groups",    L"应用到所选的群时，请至少选择一个群" },
    { "too many groups",                  L"所选的群太多" },
    { "too many rules",                   L"规则最多 50 条" },
    { "rule name too long",               L"规则名称最多 32 个字" },
    { "trigger type not allowed",         L"企业机器人不能使用这种触发方式" },
    { "action type not allowed",          L"这种触发方式不能搭配所选的动作" },
    { "bad match mode",                   L"关键词的匹配方式取值不对" },
    { "too many keywords",                L"关键词最多 20 个" },
    { "bad keyword",                      L"关键词不能为空，每个最多 32 个字" },
    { "keyword rule without keywords",    L"关键词规则至少要填写一个关键词" },
    { "bad schedule",                     L"定时的种类或时刻不对" },
    { "weekly schedule without weekdays", L"每周定时至少要选择一天" },
    { "bad weekday",                      L"星期取值不对" },
    { "bad month day",                    L"每月定时的日期须为 1～31 号" },
    { "too many schedule groups",         L"定时消息的目标群太多" },
    { "bad time window",                  L"时间段的起止时刻不对" },
    { "bad cooldown",                     L"冷却时间取值不对" },
    { "reply too long",                   L"回复文字最多 2048 个字" },
    { "empty reply",                      L"回复文字不能为空" },
    { "url not allowed",                  L"外部服务地址须为 http 或 https 地址，且主机在域名白名单内" },
    { "groups not in enterprise",         L"只能选择本企业的群（定时消息的目标群也是）" },
    { "bot limit reached",                L"本企业的企业机器人已达 50 个" },
    { "repo bot is read-only here",       L"仓库通知的机器人只能在「仓库通知」页修改" },
    { "invalid bot fields",               L"提交的内容格式不对" },
};

std::string Utf8FromWide(const CString& wide)
{
    if (wide.IsEmpty())
        return std::string();
    const int len = wide.GetLength();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, len, &out[0], bytes, nullptr, nullptr);
    return out;
}

CString WideFromUtf8(const std::string& utf8)
{
    if (utf8.empty())
        return CString();
    const int bytes = static_cast<int>(utf8.size());
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), bytes, nullptr, 0);
    if (len <= 0)
        return CString();
    CString out;
    wchar_t* buf = out.GetBuffer(len);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), bytes, buf, len);
    out.ReleaseBuffer(len);
    return out;
}

// 64 位 ID：字符串按十进制解析，整数直接取
long long ToId(const Json::Value& v)
{
    if (v.isString())
        return _strtoi64(v.asCString(), nullptr, 10);
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

// 读一个整数：整数直接取，数字字符串按十进制解析，其余取默认值
int ReadInt(const Json::Value& v, int def)
{
    //超出 int 范围的整数 asInt 会抛异常，先按 64 位取再截断
    if (v.isInt())
        return v.asInt();
    if (v.isIntegral())
        return static_cast<int>(v.asInt64());
    if (v.isString() && !v.asString().empty())
        return static_cast<int>(_strtoi64(v.asCString(), nullptr, 10));
    return def;
}

// 读一个布尔：true、非 0 的整数、非 "0" 的数字字符串为真；缺省时取默认值
bool ReadBool(const Json::Value& v, bool def)
{
    if (v.isNull())
        return def;
    if (v.isBool())
        return v.asBool();
    return ReadInt(v, def ? 1 : 0) != 0;
}

// 读一个 64 位整数（时间等）：整数直接取，其余取 0
long long ReadInt64(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return _strtoi64(v.asCString(), nullptr, 10);
    return 0;
}

// 读一段文字：不是字符串时取空
CString ReadText(const Json::Value& v)
{
    return v.isString() ? WideFromUtf8(v.asString()) : CString();
}

// 读一组 ID（字符串或整数），跳过 0
std::vector<long long> ReadIdArray(const Json::Value& v)
{
    std::vector<long long> out;
    if (!v.isArray())
        return out;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
    {
        const long long id = ToId(v[i]);
        if (id > 0)
            out.push_back(id);
    }
    return out;
}

// 读一组整数
std::vector<int> ReadIntArray(const Json::Value& v)
{
    std::vector<int> out;
    if (!v.isArray())
        return out;
    for (Json::ArrayIndex i = 0; i < v.size(); ++i)
        out.push_back(ReadInt(v[i], 0));
    return out;
}

// 写一组 ID：一律写成字符串
Json::Value IdArrayToJson(const std::vector<long long>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(std::to_string(ids[i]));
    return arr;
}

// 写一组整数
Json::Value IntArrayToJson(const std::vector<int>& vals)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < vals.size(); ++i)
        arr.append(vals[i]);
    return arr;
}

Json::Value MakeBody(const CString& token, long long enterpriseId)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    if (enterpriseId > 0)
        root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return root;
}

HttpRequest MakeRequest(const ServerConfig& server, const wchar_t* path, const Json::Value& body)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    HttpRequest request;
    request.m_host      = server.m_host.GetString();
    request.m_port      = server.m_port;
    request.m_useTls    = server.m_useTls;
    request.m_path      = path;
    request.m_bodyUtf8  = Json::writeString(wb, body);
    request.m_timeoutMs = kBotTimeoutMs;
    return request;
}

// 解析应答的公共部分；业务成功时返回 true 并给出 root
bool ParsePrelude(const HttpResult& http, BotResultBase& result, Json::Value& root)
{
    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return false;
    }
    result.m_transportOk = true;
    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return false;
    }
    Json::CharReaderBuilder b;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    if (!reader->parse(http.m_body.data(), http.m_body.data() + http.m_body.size(), &root, &errs) || !root.isObject())
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message = BotErrorText(kErrLocalBadResponse);
        return false;
    }
    result.m_code = root["code"].isIntegral() ? root["code"].asInt() : kErrLocalBadResponse;
    if (result.m_code != 0)
    {
        result.m_serverMsg = ReadText(root["msg"]);
        result.m_message = BotErrorText(result.m_code);
        return false;
    }
    return true;
}

// ---- 规则对象与机器人对象的编码（字段见 docs/协议设计-v2.md 25.1）----

Json::Value TriggerToJson(const BotTrigger& t)
{
    Json::Value v;
    v["type"] = t.m_type;
    Json::Value kws(Json::arrayValue);
    for (size_t i = 0; i < t.m_keywords.size(); ++i)
        kws.append(Utf8FromWide(t.m_keywords[i]));
    v["keywords"] = kws;
    v["match"] = t.m_match;
    v["needat"] = t.m_needAt ? 1 : 0;
    Json::Value sch;
    sch["kind"] = t.m_schedule.m_kind;
    sch["time"] = Utf8FromWide(t.m_schedule.m_time);
    sch["weekdays"] = IntArrayToJson(t.m_schedule.m_weekdays);
    sch["monthday"] = t.m_schedule.m_monthDay;
    v["schedule"] = sch;
    v["groupids"] = IdArrayToJson(t.m_groupIds);
    v["presence"] = IntArrayToJson(t.m_presence);
    Json::Value win;
    win["on"] = t.m_window.m_on ? 1 : 0;
    win["days"] = t.m_window.m_days;
    win["from"] = Utf8FromWide(t.m_window.m_from);
    win["to"] = Utf8FromWide(t.m_window.m_to);
    v["window"] = win;
    Json::Value snd;
    snd["kind"] = t.m_senders.m_kind;
    snd["users"] = IdArrayToJson(t.m_senders.m_users);
    snd["depts"] = IdArrayToJson(t.m_senders.m_depts);
    v["senders"] = snd;
    v["cooldown"] = t.m_cooldown;
    v["ownerquiet"] = t.m_ownerQuiet;
    return v;
}

BotTrigger TriggerFromJson(const Json::Value& v)
{
    BotTrigger t;
    if (!v.isObject())
        return t;
    t.m_type = ReadInt(v["type"], kTriggerAt);
    const Json::Value& kws = v["keywords"];
    if (kws.isArray())
    {
        for (Json::ArrayIndex i = 0; i < kws.size(); ++i)
        {
            if (kws[i].isString())
                t.m_keywords.push_back(WideFromUtf8(kws[i].asString()));
        }
    }
    t.m_match = ReadInt(v["match"], kMatchContains);
    t.m_needAt = ReadBool(v["needat"], true);
    const Json::Value& sch = v["schedule"];
    if (sch.isObject())
    {
        t.m_schedule.m_kind = ReadInt(sch["kind"], kScheduleDaily);
        t.m_schedule.m_time = ReadText(sch["time"]);
        t.m_schedule.m_weekdays = ReadIntArray(sch["weekdays"]);
        t.m_schedule.m_monthDay = ReadInt(sch["monthday"], 1);
    }
    t.m_groupIds = ReadIdArray(v["groupids"]);
    t.m_presence = ReadIntArray(v["presence"]);
    const Json::Value& win = v["window"];
    if (win.isObject())
    {
        t.m_window.m_on = ReadBool(win["on"], false);
        t.m_window.m_days = ReadInt(win["days"], kWindowEveryDay);
        t.m_window.m_from = ReadText(win["from"]);
        t.m_window.m_to = ReadText(win["to"]);
    }
    const Json::Value& snd = v["senders"];
    if (snd.isObject())
    {
        t.m_senders.m_kind = ReadInt(snd["kind"], 0);
        t.m_senders.m_users = ReadIdArray(snd["users"]);
        t.m_senders.m_depts = ReadIdArray(snd["depts"]);
    }
    t.m_cooldown = ReadInt(v["cooldown"], 0);
    t.m_ownerQuiet = ReadInt(v["ownerquiet"], 0);
    return t;
}

Json::Value ActionToJson(const BotAction& a)
{
    Json::Value v;
    v["type"] = a.m_type;
    v["text"] = Utf8FromWide(a.m_text);
    v["markdown"] = a.m_markdown ? 1 : 0;
    v["url"] = Utf8FromWide(a.m_url);
    if (a.m_defaultText)
        v["defaulttext"] = 1;
    return v;
}

BotAction ActionFromJson(const Json::Value& v)
{
    BotAction a;
    if (!v.isObject())
        return a;
    a.m_type = ReadInt(v["type"], kActionReply);
    a.m_text = ReadText(v["text"]);
    a.m_markdown = ReadBool(v["markdown"], false);
    a.m_url = ReadText(v["url"]);
    a.m_defaultText = ReadBool(v["defaulttext"], false);
    return a;
}

Json::Value RuleToJson(const BotRule& r)
{
    Json::Value v;
    v["id"] = std::to_string(r.m_id > 0 ? r.m_id : 0);
    v["name"] = Utf8FromWide(r.m_name);
    v["enabled"] = r.m_enabled ? 1 : 0;
    v["trigger"] = TriggerToJson(r.m_trigger);
    //外部系统推送的规则没有动作；其余规则必须带动作
    if (r.m_trigger.m_type != kTriggerWebhook)
        v["action"] = ActionToJson(r.m_action);
    return v;
}

BotRule RuleFromJson(const Json::Value& v)
{
    BotRule r;
    if (!v.isObject())
        return r;
    r.m_id = ToId(v["id"]);
    r.m_name = ReadText(v["name"]);
    r.m_enabled = ReadBool(v["enabled"], true);
    r.m_trigger = TriggerFromJson(v["trigger"]);
    r.m_action = ActionFromJson(v["action"]);
    return r;
}

// 提交给 save.do、try.do 的机器人对象：只带可编辑的字段
Json::Value BotToJson(const BotItem& b)
{
    Json::Value v;
    v["botid"] = std::to_string(b.m_botId > 0 ? b.m_botId : 0);
    v["name"] = Utf8FromWide(b.m_name);
    v["desc"] = Utf8FromWide(b.m_desc);
    v["glyph"] = Utf8FromWide(b.m_glyph);
    v["color"] = Utf8FromWide(b.m_color);
    v["status"] = b.m_status;
    v["scope"] = b.m_scope;
    //应用到全部群时不带群，免得保存一份不再用到的旧选择
    v["groupids"] = (b.m_scope == kBotScopeSelected) ? IdArrayToJson(b.m_groupIds) : Json::Value(Json::arrayValue);
    v["allowgroupoff"] = b.m_allowGroupOff ? 1 : 0;
    Json::Value rules(Json::arrayValue);
    for (size_t i = 0; i < b.m_rules.size(); ++i)
        rules.append(RuleToJson(b.m_rules[i]));
    v["rules"] = rules;
    return v;
}

BotItem BotFromJson(const Json::Value& v)
{
    BotItem b;
    if (!v.isObject())
        return b;
    b.m_botId = ToId(v["botid"]);
    b.m_userId = ToId(v["userid"]);
    b.m_ownerType = ReadInt(v["ownertype"], kOwnerTypeEnterprise);
    b.m_name = ReadText(v["name"]);
    b.m_desc = ReadText(v["desc"]);
    b.m_glyph = ReadText(v["glyph"]);
    b.m_color = ReadText(v["color"]);
    b.m_scope = (ReadInt(v["scope"], kBotScopeAll) == kBotScopeSelected) ? kBotScopeSelected : kBotScopeAll;
    b.m_groupIds = ReadIdArray(v["groupids"]);
    b.m_allowGroupOff = ReadBool(v["allowgroupoff"], false);
    b.m_status = (ReadInt(v["status"], kBotStatusActive) == kBotStatusDisabled) ? kBotStatusDisabled : kBotStatusActive;
    b.m_source = ReadInt(v["source"], kBotSourceRules);
    b.m_creatorName = ReadText(v["creatorname"]);
    b.m_createTime = ReadInt64(v["createtime"]);
    b.m_updateTime = ReadInt64(v["updatetime"]);
    b.m_lastFire = ReadInt64(v["lastfire"]);
    b.m_ruleCount = ReadInt(v["rulecount"], 0);
    const Json::Value& rules = v["rules"];
    b.m_hasRules = rules.isArray();
    if (b.m_hasRules)
    {
        for (Json::ArrayIndex i = 0; i < rules.size(); ++i)
            b.m_rules.push_back(RuleFromJson(rules[i]));
        b.m_ruleCount = static_cast<int>(b.m_rules.size());
    }
    b.m_webhookToken = ReadText(v["webhooktoken"]);
    b.m_secret = ReadText(v["secret"]);
    b.m_webhookPath = ReadText(v["webhookpath"]);
    b.m_webhookUrl = ReadText(v["webhookurl"]);
    return b;
}

// 查原因对应的中文说明；认不得返回空串
CString ReasonToText(const std::string& reason)
{
    for (size_t i = 0; i < sizeof(kReasonTexts) / sizeof(kReasonTexts[0]); ++i)
    {
        if (reason == kReasonTexts[i].m_reason)
            return CString(kReasonTexts[i].m_text);
    }
    return CString();
}

} // anonymous namespace

HttpRequest MakeBotListRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathBotList, MakeBody(token, enterpriseId));
}

HttpRequest MakeBotDetailRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                 long long botId)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["botid"] = std::to_string(botId);
    return MakeRequest(server, kPathBotDetail, root);
}

HttpRequest MakeBotSaveRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                               const BotItem& bot)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["bot"] = BotToJson(bot);
    return MakeRequest(server, kPathBotSave, root);
}

HttpRequest MakeBotDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                 long long botId)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["botid"] = std::to_string(botId);
    return MakeRequest(server, kPathBotDelete, root);
}

HttpRequest MakeBotSetStatusRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                    long long botId, int status)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["botid"] = std::to_string(botId);
    root["status"] = (status == kBotStatusDisabled) ? kBotStatusDisabled : kBotStatusActive;
    return MakeRequest(server, kPathBotSetStatus, root);
}

HttpRequest MakeBotGroupsRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathBotGroups, MakeBody(token, enterpriseId));
}

HttpRequest MakeBotTryRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                              const BotItem& draft, const BotTryInput& input)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["bot"] = BotToJson(draft);
    Json::Value in;
    in["text"] = Utf8FromWide(input.m_text);
    in["at"] = input.m_at ? 1 : 0;
    if (input.m_groupId > 0)
        in["groupid"] = std::to_string(input.m_groupId);
    if (input.m_timeMs > 0)
        in["time"] = static_cast<Json::Int64>(input.m_timeMs);
    root["input"] = in;
    return MakeRequest(server, kPathBotTry, root);
}

HttpRequest MakeBotResetTokenRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     long long botId)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["botid"] = std::to_string(botId);
    return MakeRequest(server, kPathBotResetToken, root);
}

HttpRequest MakeBotDomainsRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathBotDomains, MakeBody(token, enterpriseId));
}

HttpRequest MakeBotSetDomainsRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const std::vector<CString>& domains)
{
    Json::Value root = MakeBody(token, enterpriseId);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < domains.size(); ++i)
        arr.append(Utf8FromWide(domains[i]));
    root["domains"] = arr;
    return MakeRequest(server, kPathBotSetDomains, root);
}

BotListResult ParseBotListResponse(const HttpResult& http)
{
    BotListResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& jo = list[i];
        if (!jo.isObject())
            continue;
        BotItem bot = BotFromJson(jo);
        if (bot.m_botId <= 0)
            continue;
        result.m_bots.push_back(bot);
    }
    return result;
}

BotDetailResult ParseBotDetailResponse(const HttpResult& http)
{
    BotDetailResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& bot = root["bot"];
    if (!bot.isObject())
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message = BotErrorText(kErrLocalBadResponse);
        return result;
    }
    result.m_bot = BotFromJson(bot);
    return result;
}

BotSaveResult ParseBotSaveResponse(const HttpResult& http)
{
    BotSaveResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
    {
        if (result.m_transportOk && result.m_code > 0)
            result.m_message = BotSaveErrorText(result.m_code, result.m_serverMsg);
        return result;
    }
    result.m_botId = ToId(root["botid"]);
    return result;
}

BotResultBase ParseBotWriteResponse(const HttpResult& http)
{
    BotResultBase result;
    Json::Value root;
    ParsePrelude(http, result, root);
    return result;
}

BotGroupsResult ParseBotGroupsResponse(const HttpResult& http)
{
    BotGroupsResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& jo = list[i];
        if (!jo.isObject())
            continue;
        BotGroup g;
        g.m_id = ToId(jo["groupid"]);
        if (g.m_id <= 0)
            continue;
        g.m_name = ReadText(jo["name"]);
        result.m_groups.push_back(g);
    }
    return result;
}

BotTryResult ParseBotTryResponse(const HttpResult& http)
{
    BotTryResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    result.m_matched = ReadBool(root["matched"], false);
    result.m_kind = ReadInt(root["kind"], kTryKindNone);
    result.m_ruleId = ToId(root["ruleid"]);
    result.m_ruleName = ReadText(root["rulename"]);
    result.m_text = ReadText(root["text"]);
    result.m_markdown = ReadBool(root["markdown"], false);
    result.m_url = ReadText(root["url"]);
    result.m_reason = ReadInt(root["reason"], kTryReasonMatched);
    return result;
}

BotResetTokenResult ParseBotResetTokenResponse(const HttpResult& http)
{
    BotResetTokenResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    result.m_webhookToken = ReadText(root["webhooktoken"]);
    result.m_secret = ReadText(root["secret"]);
    return result;
}

BotDomainsResult ParseBotDomainsResponse(const HttpResult& http)
{
    BotDomainsResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& list = root["domains"];
    if (!list.isArray())
        return result;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        if (list[i].isString() && !list[i].asString().empty())
            result.m_domains.push_back(WideFromUtf8(list[i].asString()));
    }
    return result;
}

CString BotErrorText(int code)
{
    switch (code)
    {
    //机器人或规则的字段不合法
    case kErrBotInvalid:
        return CString(_T("机器人或规则的内容不合规，请检查后重试"));
    //机器人已不存在
    case kErrBotNotExist:
        return CString(_T("这个机器人已不存在，可能已被其他管理员删除"));
    //机器人、规则或关键词超出上限
    case kErrBotFull:
        return CString(_T("已达上限：每个企业最多 50 个企业机器人，每个机器人最多 50 条规则，每条规则最多 20 个关键词"));
    //所选的群或定时目标群不合规
    case kErrBotGroupInvalid:
        return CString(_T("请至少选择一个群，且只能选择本企业的群（定时消息的目标群也是）"));
    //不允许的触发方式、动作，或外部服务地址不在白名单内
    case kErrBotNotAllowed:
        return CString(_T("规则用了不允许的触发方式或动作，或外部服务地址不在域名白名单内"));
    //域名白名单不合法
    case kErrBotDomainInvalid:
        return CString(_T("域名白名单里有不合法的域名，或超过 50 条"));
    //仓库通知的机器人只读
    case kErrBotReadonly:
        return CString(_T("仓库通知的机器人只能在「仓库通知」页修改"));
    //无权限
    case kErrPermission:
        return CString(_T("没有权限执行该操作"));
    //服务器内部错误
    case kErrUnknown:
        return CString(_T("服务器内部错误，请稍后重试"));
    //应答格式不对
    case kErrLocalBadResponse:
        return CString(_T("服务器应答异常，请稍后重试"));
    //其余错误：带上错误码
    default:
    {
        CString text;
        text.Format(_T("操作失败（错误码 %d）"), code);
        return text;
    }
    }
}

CString BotSaveErrorText(int code, const CString& serverMsg)
{
    const std::string msg = Utf8FromWide(serverMsg);
    const size_t prefixLen = sizeof(kRulePrefix) - 1;
    //"rule N: reason"：取出规则下标与原因
    if (msg.compare(0, prefixLen, kRulePrefix) == 0)
    {
        const size_t sep = msg.find(kRuleReasonSep, prefixLen);
        if (sep != std::string::npos && sep > prefixLen)
        {
            const std::string indexText = msg.substr(prefixLen, sep - prefixLen);
            const std::string reason = msg.substr(sep + sizeof(kRuleReasonSep) - 1);
            const CString detail = ReasonToText(reason);
            if (!detail.IsEmpty())
            {
                const long long index = _strtoi64(indexText.c_str(), nullptr, 10);
                CString text;
                text.Format(_T("第 %lld 条规则：%s"), index + 1, detail.GetString());
                return text;
            }
        }
        return BotErrorText(code);
    }
    const CString detail = ReasonToText(msg);
    return detail.IsEmpty() ? BotErrorText(code) : detail;
}

CString WebhookUrl(const ServerConfig& server, const CString& path, const CString& serverUrl)
{
    if (path.IsEmpty())
        return CString();
    //服务端配置了对外地址：外部系统要用的就是它，控制台自己的连接地址在 NAT 或反向代理之后未必能从外部访问
    if (!serverUrl.IsEmpty())
        return serverUrl;
    if (server.m_host.IsEmpty())
        return path;
    CString url;
    url.Format(_T("%s://%s:%d%s"), server.m_useTls ? _T("https") : _T("http"), server.m_host.GetString(),
               server.m_port, path.GetString());
    return url;
}

}} // namespace flamingoadmin::botapi
