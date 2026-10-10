/**
 *  flamingoAdmin —— 企业机器人（docs/机器人方案.md 第 7 节，接口见 docs/Admin控制台接口文档.md 模块 14）单元测试。
 *
 *    - EBOT-1：十个接口的请求构造（路径、ID 一律写成字符串、新建时 botid 为 "0"、应用范围为全部群时 groupids 为空、
 *              外部系统推送的规则不带 action、试一试的 input、enterpriseid 为 0 时不带）；
 *    - EBOT-2：应答解析（列表、详情的规则与密钥、保存、试一试、令牌、白名单、群）、170～176 的中文提示、save.do 失败时
 *              「第 N 条规则：……」的说明、传输失败与格式不对；详情解析后再提交，界面上不编辑的字段原样带回；
 *    - EBOT-3：BotRuleText 的转换：关键词、HH:MM、星期、中文名称、触发方式与动作的搭配、应用范围文字、定时文字、
 *              规则摘要、试一试结果、外部服务地址与白名单、域名、头像底色、Webhook 地址；
 *    - EBOT-4：提交前的本地校验（与服务端 BotRules::ValidateBot 一致）；
 *    - EBOT-5：机器人页：进入即拉列表、群与白名单；收到列表后的行数；仓库通知的机器人只读；停用 / 启用二选一；
 *              编辑先拉详情、过时的详情丢弃；删除与重新生成先确认；保存、试一试、白名单的结果；平台超管未选企业时只提示；
 *              ActionTable 让出不显示链接的位置；
 *    - EBOT-6：机器人编辑窗：新建的默认值、应用范围与群列表晚到、规则的添加 / 编辑 / 上移 / 下移 / 删除与按钮是否可用、
 *              规则满 50 条、编辑时预填（色块外的底色原样保留）、Webhook 显示与重新生成、试一试的输入；
 *    - EBOT-7：规则编辑窗：按触发方式、动作、定时种类显隐控件（隐藏后再显示时位置正确），各触发方式取回的规则，
 *              外部服务地址按白名单校验，打开后原样取回时界面上不编辑的字段保留；
 *    - EBOT-8：外部服务域名白名单窗；机器人的四个功能开关（键、默认值、确认文字、紧凑名称）及其在企业详情「功能开关」页里
 *              的控件编号、结果标签与位置（2026-10-08 起「机器人」卡片排在含云端录像空间的「音视频」卡片之后，§11 Q18）；
 *    - EBOT-9：服务端下发的 Webhook 对外完整地址（webhookurl，docs/机器人方案.md §11 Q19）：列表与详情解析，
 *              有就优先显示、没有就按连接地址拼出；编辑窗显示的地址。
 *
 *  只调用纯函数与控件的数据接口，不发起网络请求、不调 OnPaint。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "UiIds.h"
#include "Util/BotAdminApi.h"
#include "Util/BotRuleText.h"
#include "Screens/EnterpriseBotScreen.h"
#include "Screens/EnterpriseBotModal.h"
#include "Screens/BotRuleModal.h"
#include "Screens/BotDomainsModal.h"
#include "Screens/FeatureSwitchPanel.h"
#include "Screens/FeatureSwitchesPage.h"
#include "Screens/CloudRecordQuotaPanel.h"
#include "Widgets/ActionTable.h"
#include "json.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Input/DuiSwitch.h"

#include <memory>
#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::botapi;
using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiEdit;

// 测试用的企业ID、群ID与机器人ID
const long long kEnt = 9001;
const long long kGroupA = 268435457LL;
const long long kGroupB = 268435458LL;
const long long kBotIt = 12;
const long long kBotRepo = 13;

// 测试用的界面矩形与窗口
const RECT kScreenRect = { 0, 0, 1200, 800 };
const RECT kWindowRect = { 0, 0, 1440, 900 };

// 测试用的服务器端口
const int kTestPort = 12345;

// 两条规则之间的下标差（上移、下移一位）
const int kMoveUp = -1;
const int kMoveDown = 1;

ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = kTestPort;
    s.m_useTls = false;
    return s;
}

HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

bool ParseBody(const HttpRequest& req, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const bool ok = reader->parse(req.m_bodyUtf8.c_str(), req.m_bodyUtf8.c_str() + req.m_bodyUtf8.size(), &out, &errs);
    return ok && errs.empty() && out.isObject();
}

std::vector<BotGroup> TwoGroups()
{
    std::vector<BotGroup> groups(2);
    groups[0].m_id = kGroupA;
    groups[0].m_name = _T("研发群");
    groups[1].m_id = kGroupB;
    groups[1].m_name = _T("行政群");
    return groups;
}

// 取控件树里某个编号的输入框；找不到时返回空
DuiEdit* EditById(balloonwjui::DuiControl& root, UINT id)
{
    return dynamic_cast<DuiEdit*>(root.FindCtrlById(id));
}

// 取控件树里某个编号的按钮（勾选框也是按钮）；找不到时返回空
DuiButton* ButtonById(balloonwjui::DuiControl& root, UINT id)
{
    return dynamic_cast<DuiButton*>(root.FindCtrlById(id));
}

// 某个编号的控件是否可见；找不到时返回 false
bool IsShown(balloonwjui::DuiControl& root, UINT id)
{
    balloonwjui::DuiControl* ctrl = root.FindCtrlById(id);
    return ctrl != nullptr && ctrl->IsVisible();
}

// 矩形 inner 是否完整落在 outer 之内且不为空
bool InsideNonEmpty(const RECT& inner, const RECT& outer)
{
    return inner.right > inner.left && inner.bottom > inner.top && inner.left >= outer.left && inner.right <= outer.right
           && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

// 一条「被 @ 时回复文字」的规则
BotRule MakeAtRule(const CString& name, const CString& text)
{
    BotRule rule;
    rule.m_name = name;
    rule.m_trigger.m_type = kTriggerAt;
    rule.m_action.m_type = kActionReply;
    rule.m_action.m_text = text;
    return rule;
}

// 记录页面发出的请求与界面请求
struct Captured
{
    std::vector<HttpRequest>                    m_requests;  // 请求出口收到的请求
    std::vector<EnterpriseBotScreen::UiRequest> m_ui;        // 界面请求
};

void CaptureRequest(void* user, HttpRequest& request)
{
    static_cast<Captured*>(user)->m_requests.push_back(request);
}

void CaptureUi(void* user, const EnterpriseBotScreen::UiRequest& request)
{
    static_cast<Captured*>(user)->m_ui.push_back(request);
}

// 最近一条某种的界面请求；没有时返回 m_kind 为 0 的请求
EnterpriseBotScreen::UiRequest LastUi(const Captured& cap, int kind)
{
    for (size_t i = cap.m_ui.size(); i > 0; --i)
    {
        if (cap.m_ui[i - 1].m_kind == kind)
            return cap.m_ui[i - 1];
    }
    return EnterpriseBotScreen::UiRequest();
}

// 两个机器人的列表应答：一个普通的企业机器人（启用），一个 GitHub 仓库通知的机器人（停用）
const char* const kTwoBotsJson =
    u8"{\"code\":0,\"msg\":\"ok\",\"list\":["
    u8"{\"botid\":\"12\",\"userid\":\"5401\",\"enterpriseid\":\"9001\",\"ownertype\":1,\"ownerid\":\"9001\","
    u8"\"name\":\"IT 小助手\",\"desc\":\"IT 支持问答\",\"glyph\":\"IT\",\"color\":\"#1E88E5\",\"scope\":1,"
    u8"\"groupids\":[\"268435457\"],\"allowgroupoff\":1,\"status\":0,\"source\":0,\"creatorname\":\"admin\","
    u8"\"createtime\":1791200400000,\"updatetime\":1791200400000,\"lastfire\":1791200500000,\"rulecount\":2,"
    u8"\"webhookpath\":\"/bot/webhook.do\"},"
    u8"{\"botid\":\"13\",\"userid\":\"5402\",\"enterpriseid\":\"9001\",\"ownertype\":1,\"ownerid\":\"9001\","
    u8"\"name\":\"仓库通知\",\"desc\":\"\",\"glyph\":\"\",\"color\":\"\",\"scope\":0,\"groupids\":[],\"allowgroupoff\":0,"
    u8"\"status\":1,\"source\":1,\"creatorname\":\"admin\",\"createtime\":1,\"updatetime\":1,\"lastfire\":0,"
    u8"\"rulecount\":0,\"webhookpath\":\"/bot/webhook.do\"}]}";

// 机器人 12 的详情：四条规则（关键词带时间段与个人机器人用的字段、每周定时、被 @ 呼叫外部服务、外部系统推送）
const char* const kDetailJson =
    u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"12\",\"userid\":\"5401\",\"ownertype\":1,\"name\":\"IT 小助手\","
    u8"\"desc\":\"IT 支持问答\",\"glyph\":\"IT\",\"color\":\"#ABCDEF\",\"scope\":1,\"groupids\":[\"268435457\"],"
    u8"\"allowgroupoff\":1,\"status\":0,\"source\":0,\"rulecount\":4,\"lastfire\":0,\"webhookpath\":\"/bot/webhook.do\","
    u8"\"webhooktoken\":\"tok32\",\"secret\":\"sec32\",\"rules\":["
    u8"{\"id\":\"31\",\"name\":\"VPN 问题\",\"enabled\":1,\"trigger\":{\"type\":2,\"keywords\":[\"VPN\",\"密码\"],"
    u8"\"match\":1,\"needat\":1,\"schedule\":{\"kind\":1,\"time\":\"09:00\",\"weekdays\":[],\"monthday\":1},"
    u8"\"groupids\":[],\"presence\":[0,2],\"window\":{\"on\":1,\"days\":1,\"from\":\"18:00\",\"to\":\"09:00\"},"
    u8"\"senders\":{\"kind\":1,\"users\":[\"5001\"],\"depts\":[]},\"cooldown\":30,\"ownerquiet\":5},"
    u8"\"action\":{\"type\":1,\"text\":\"重启客户端\",\"markdown\":1,\"url\":\"\"}},"
    u8"{\"id\":\"32\",\"name\":\"早会\",\"enabled\":0,\"trigger\":{\"type\":4,\"keywords\":[],\"match\":1,\"needat\":1,"
    u8"\"schedule\":{\"kind\":3,\"time\":\"09:30\",\"weekdays\":[1,3],\"monthday\":1},\"groupids\":[\"268435458\"]},"
    u8"\"action\":{\"type\":1,\"text\":\"早会开始\",\"markdown\":0,\"url\":\"\"}},"
    u8"{\"id\":\"33\",\"name\":\"\",\"enabled\":1,\"trigger\":{\"type\":1},"
    u8"\"action\":{\"type\":3,\"text\":\"稍后再试\",\"markdown\":0,\"url\":\"https://api.example.com/hook\"}},"
    u8"{\"id\":\"34\",\"name\":\"部署\",\"enabled\":1,\"trigger\":{\"type\":6}}]}}";

// ---------------------------------------------------------------------------
// EBOT-1：请求构造
// ---------------------------------------------------------------------------
void TestBotRequests()
{
    BeginCase("EBOT-1 机器人十个接口的请求构造");
    const ServerConfig server = MakeServer();
    Json::Value body;

    HttpRequest list = MakeBotListRequest(server, _T("tok"), 0);
    FA_EXPECT_TRUE(list.m_path == L"/admin/bot/list.do");
    FA_EXPECT_TRUE(ParseBody(list, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    FA_EXPECT_TRUE(body["token"].asString() == "tok");
    HttpRequest listPlat = MakeBotListRequest(server, _T("tok"), kEnt);
    FA_EXPECT_TRUE(ParseBody(listPlat, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEnt);

    HttpRequest detail = MakeBotDetailRequest(server, _T("tok"), 0, kBotIt);
    FA_EXPECT_TRUE(detail.m_path == L"/admin/bot/detail.do");
    FA_EXPECT_TRUE(ParseBody(detail, body));
    FA_EXPECT_TRUE(body["botid"].isString());
    FA_EXPECT_TRUE(body["botid"].asString() == "12");

    //新建：botid 为 "0"；应用范围为全部群时即使带了群也提交空数组；规则带全部字段，外部系统推送的规则不带 action
    BotItem bot;
    bot.m_name = _T("IT 小助手");
    bot.m_glyph = _T("IT");
    bot.m_color = _T("#1E88E5");
    bot.m_scope = kBotScopeAll;
    bot.m_groupIds.push_back(kGroupA);
    bot.m_allowGroupOff = true;
    BotRule keyword;
    keyword.m_name = _T("VPN");
    keyword.m_trigger.m_type = kTriggerKeyword;
    keyword.m_trigger.m_keywords.push_back(_T("VPN"));
    keyword.m_trigger.m_match = kMatchPrefix;
    keyword.m_trigger.m_needAt = false;
    keyword.m_trigger.m_window.m_on = true;
    keyword.m_trigger.m_window.m_days = kWindowWeekend;
    keyword.m_action.m_type = kActionQa;
    keyword.m_action.m_text = _T("没找到答案");
    bot.m_rules.push_back(keyword);
    BotRule hook;
    hook.m_trigger.m_type = kTriggerWebhook;
    bot.m_rules.push_back(hook);
    HttpRequest save = MakeBotSaveRequest(server, _T("tok"), 0, bot);
    FA_EXPECT_TRUE(save.m_path == L"/admin/bot/save.do");
    FA_EXPECT_TRUE(ParseBody(save, body));
    const Json::Value& jb = body["bot"];
    FA_EXPECT_TRUE(jb["botid"].asString() == "0");
    FA_EXPECT_TRUE(jb["name"].asString() == std::string(u8"IT 小助手"));
    FA_EXPECT_EQ(jb["scope"].asInt(), kBotScopeAll);
    FA_EXPECT_TRUE(jb["groupids"].isArray());
    FA_EXPECT_EQ(static_cast<int>(jb["groupids"].size()), 0);
    FA_EXPECT_EQ(jb["allowgroupoff"].asInt(), 1);
    FA_EXPECT_EQ(static_cast<int>(jb["rules"].size()), 2);
    const Json::Value& r0 = jb["rules"][0];
    FA_EXPECT_TRUE(r0["id"].asString() == "0");
    FA_EXPECT_EQ(r0["trigger"]["type"].asInt(), kTriggerKeyword);
    FA_EXPECT_TRUE(r0["trigger"]["keywords"][0].asString() == "VPN");
    FA_EXPECT_EQ(r0["trigger"]["match"].asInt(), kMatchPrefix);
    FA_EXPECT_EQ(r0["trigger"]["needat"].asInt(), 0);
    FA_EXPECT_EQ(r0["trigger"]["window"]["on"].asInt(), 1);
    FA_EXPECT_EQ(r0["trigger"]["window"]["days"].asInt(), kWindowWeekend);
    FA_EXPECT_TRUE(r0["trigger"]["schedule"]["time"].asString() == "09:00");
    FA_EXPECT_EQ(r0["action"]["type"].asInt(), kActionQa);
    FA_EXPECT_FALSE(jb["rules"][1].isMember("action"));

    //修改：botid 写成字符串；所选的群提交群号字符串
    bot.m_botId = kBotIt;
    bot.m_scope = kBotScopeSelected;
    HttpRequest update = MakeBotSaveRequest(server, _T("tok"), kEnt, bot);
    FA_EXPECT_TRUE(ParseBody(update, body));
    FA_EXPECT_TRUE(body["bot"]["botid"].asString() == "12");
    FA_EXPECT_TRUE(body["bot"]["groupids"][0].asString() == std::to_string(kGroupA));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEnt);

    HttpRequest del = MakeBotDeleteRequest(server, _T("tok"), 0, kBotIt);
    FA_EXPECT_TRUE(del.m_path == L"/admin/bot/delete.do");
    FA_EXPECT_TRUE(ParseBody(del, body));
    FA_EXPECT_TRUE(body["botid"].asString() == "12");

    HttpRequest status = MakeBotSetStatusRequest(server, _T("tok"), 0, kBotIt, kBotStatusDisabled);
    FA_EXPECT_TRUE(status.m_path == L"/admin/bot/setstatus.do");
    FA_EXPECT_TRUE(ParseBody(status, body));
    FA_EXPECT_EQ(body["status"].asInt(), kBotStatusDisabled);

    FA_EXPECT_TRUE(MakeBotGroupsRequest(server, _T("tok"), 0).m_path == L"/admin/bot/groups.do");
    FA_EXPECT_TRUE(MakeBotDomainsRequest(server, _T("tok"), 0).m_path == L"/admin/bot/domains.do");
    FA_EXPECT_TRUE(MakeBotResetTokenRequest(server, _T("tok"), 0, kBotIt).m_path == L"/admin/bot/resettoken.do");

    //试一试：带上编辑窗里的机器人；没有选群、没有时间时不带这两项
    BotTryInput input;
    input.m_text = _T("VPN 连不上");
    input.m_at = false;
    HttpRequest tryReq = MakeBotTryRequest(server, _T("tok"), 0, bot, input);
    FA_EXPECT_TRUE(tryReq.m_path == L"/admin/bot/try.do");
    FA_EXPECT_TRUE(ParseBody(tryReq, body));
    FA_EXPECT_TRUE(body["bot"].isObject());
    FA_EXPECT_TRUE(body["input"]["text"].asString() == std::string(u8"VPN 连不上"));
    FA_EXPECT_EQ(body["input"]["at"].asInt(), 0);
    FA_EXPECT_FALSE(body["input"].isMember("groupid"));
    FA_EXPECT_FALSE(body["input"].isMember("time"));
    input.m_groupId = kGroupB;
    FA_EXPECT_TRUE(ParseBody(MakeBotTryRequest(server, _T("tok"), 0, bot, input), body));
    FA_EXPECT_TRUE(body["input"]["groupid"].asString() == std::to_string(kGroupB));

    std::vector<CString> domains;
    domains.push_back(_T("example.com"));
    domains.push_back(_T("api.example.org"));
    HttpRequest setDomains = MakeBotSetDomainsRequest(server, _T("tok"), 0, domains);
    FA_EXPECT_TRUE(setDomains.m_path == L"/admin/bot/setdomains.do");
    FA_EXPECT_TRUE(ParseBody(setDomains, body));
    FA_EXPECT_EQ(static_cast<int>(body["domains"].size()), 2);
    FA_EXPECT_TRUE(body["domains"][1].asString() == "api.example.org");
}

// ---------------------------------------------------------------------------
// EBOT-2：应答解析与错误提示
// ---------------------------------------------------------------------------
void TestBotResponses()
{
    BeginCase("EBOT-2 应答解析、170～176 的提示、保存失败时写明第几条规则、详情原样带回");

    const BotListResult list = ParseBotListResponse(MakeHttpOk(kTwoBotsJson));
    FA_EXPECT_TRUE(list.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(list.m_bots.size()), 2);
    FA_EXPECT_TRUE(list.m_bots[0].m_botId == kBotIt);
    FA_EXPECT_STREQ(list.m_bots[0].m_name, L"IT 小助手");
    FA_EXPECT_EQ(list.m_bots[0].m_scope, kBotScopeSelected);
    FA_EXPECT_TRUE(list.m_bots[0].m_groupIds.size() == 1 && list.m_bots[0].m_groupIds[0] == kGroupA);
    FA_EXPECT_TRUE(list.m_bots[0].m_allowGroupOff);
    FA_EXPECT_EQ(list.m_bots[0].m_ruleCount, 2);
    FA_EXPECT_FALSE(list.m_bots[0].m_hasRules);
    FA_EXPECT_TRUE(list.m_bots[0].m_lastFire == 1791200500000LL);
    FA_EXPECT_STREQ(list.m_bots[0].m_webhookPath, L"/bot/webhook.do");
    FA_EXPECT_EQ(list.m_bots[1].m_source, kBotSourceGithub);
    FA_EXPECT_EQ(list.m_bots[1].m_status, kBotStatusDisabled);

    const BotDetailResult detail = ParseBotDetailResponse(MakeHttpOk(kDetailJson));
    FA_EXPECT_TRUE(detail.Succeeded());
    const BotItem& bot = detail.m_bot;
    FA_EXPECT_TRUE(bot.m_hasRules);
    FA_EXPECT_EQ(static_cast<int>(bot.m_rules.size()), 4);
    FA_EXPECT_STREQ(bot.m_webhookToken, L"tok32");
    FA_EXPECT_STREQ(bot.m_secret, L"sec32");
    if (bot.m_rules.size() == 4)
    {
        const BotRule& kw = bot.m_rules[0];
        FA_EXPECT_TRUE(kw.m_id == 31);
        FA_EXPECT_EQ(kw.m_trigger.m_type, kTriggerKeyword);
        FA_EXPECT_EQ(static_cast<int>(kw.m_trigger.m_keywords.size()), 2);
        FA_EXPECT_STREQ(kw.m_trigger.m_keywords[1], L"密码");
        FA_EXPECT_TRUE(kw.m_trigger.m_window.m_on);
        FA_EXPECT_STREQ(kw.m_trigger.m_window.m_from, L"18:00");
        FA_EXPECT_TRUE(kw.m_action.m_markdown);
        const BotRule& sch = bot.m_rules[1];
        FA_EXPECT_FALSE(sch.m_enabled);
        FA_EXPECT_EQ(sch.m_trigger.m_schedule.m_kind, kScheduleWeekly);
        FA_EXPECT_EQ(static_cast<int>(sch.m_trigger.m_schedule.m_weekdays.size()), 2);
        FA_EXPECT_TRUE(sch.m_trigger.m_groupIds.size() == 1 && sch.m_trigger.m_groupIds[0] == kGroupB);
        FA_EXPECT_EQ(bot.m_rules[2].m_action.m_type, kActionExternal);
        FA_EXPECT_STREQ(bot.m_rules[2].m_action.m_url, L"https://api.example.com/hook");
        FA_EXPECT_EQ(bot.m_rules[3].m_trigger.m_type, kTriggerWebhook);
    }

    //详情原样提交：个人机器人用的字段（presence、senders、cooldown、ownerquiet）与规则 ID 都带回
    Json::Value body;
    FA_EXPECT_TRUE(ParseBody(MakeBotSaveRequest(MakeServer(), _T("tok"), 0, bot), body));
    const Json::Value& r0 = body["bot"]["rules"][0];
    FA_EXPECT_TRUE(r0["id"].asString() == "31");
    FA_EXPECT_EQ(static_cast<int>(r0["trigger"]["presence"].size()), 2);
    FA_EXPECT_TRUE(r0["trigger"]["senders"]["users"][0].asString() == "5001");
    FA_EXPECT_EQ(r0["trigger"]["cooldown"].asInt(), 30);
    FA_EXPECT_EQ(r0["trigger"]["ownerquiet"].asInt(), 5);
    FA_EXPECT_TRUE(body["bot"]["color"].asString() == "#ABCDEF");

    const BotSaveResult saved = ParseBotSaveResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"botid\":\"15\"}"));
    FA_EXPECT_TRUE(saved.Succeeded());
    FA_EXPECT_TRUE(saved.m_botId == 15);

    const BotTryResult tried = ParseBotTryResponse(MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"matched\":1,\"kind\":1,\"ruleid\":\"31\",\"rulename\":\"VPN 问题\","
        u8"\"text\":\"重启客户端\",\"markdown\":1,\"url\":\"\",\"reason\":0}"));
    FA_EXPECT_TRUE(tried.Succeeded());
    FA_EXPECT_TRUE(tried.m_matched);
    FA_EXPECT_EQ(tried.m_kind, kTryKindReply);
    FA_EXPECT_TRUE(tried.m_ruleId == 31);
    FA_EXPECT_TRUE(tried.m_markdown);

    const BotResetTokenResult reset = ParseBotResetTokenResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"webhooktoken\":\"newtok\",\"secret\":\"newsec\"}"));
    FA_EXPECT_STREQ(reset.m_webhookToken, L"newtok");
    FA_EXPECT_STREQ(reset.m_secret, L"newsec");

    const BotDomainsResult domains = ParseBotDomainsResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"domains\":[\"example.com\",\"\"]}"));
    FA_EXPECT_EQ(static_cast<int>(domains.m_domains.size()), 1);

    const BotGroupsResult groups = ParseBotGroupsResponse(MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"list\":[{\"groupid\":\"268435457\",\"name\":\"研发群\"},{\"groupid\":\"0\"}]}"));
    FA_EXPECT_EQ(static_cast<int>(groups.m_groups.size()), 1);

    //170～176 各有说法，不带「错误码」
    const int codes[] = { 170, 171, 172, 173, 174, 175, 176 };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
        const BotResultBase r = ParseBotWriteResponse(MakeHttpOk("{\"code\":" + std::to_string(codes[i]) + ",\"msg\":\"x\"}"));
        FA_EXPECT_FALSE(r.Succeeded());
        FA_EXPECT_EQ(r.m_code, codes[i]);
        FA_EXPECT_TRUE(r.m_message.Find(_T("错误码")) < 0);
        FA_EXPECT_TRUE(BotErrorText(codes[i]).Find(_T("错误码")) < 0);
    }
    FA_EXPECT_TRUE(BotErrorText(999).Find(_T("999")) >= 0);

    //保存失败：服务端的规则下标从 0 起，提示从 1 起；认不得的原因退回错误码的说法
    const BotSaveResult bad = ParseBotSaveResponse(MakeHttpOk("{\"code\":170,\"msg\":\"rule 2: empty reply\"}"));
    FA_EXPECT_FALSE(bad.Succeeded());
    FA_EXPECT_STREQ(bad.m_serverMsg, L"rule 2: empty reply");
    FA_EXPECT_STREQ(bad.m_message, L"第 3 条规则：回复文字不能为空");
    FA_EXPECT_STREQ(BotSaveErrorText(170, _T("bad name")), L"名称不能为空，最多 20 个字");
    FA_EXPECT_STREQ(BotSaveErrorText(174, _T("rule 0: something new")), BotErrorText(174));
    FA_EXPECT_STREQ(BotSaveErrorText(176, _T("repo bot is read-only here")), BotErrorText(176));

    HttpResult broken;
    broken.m_transportOk = false;
    broken.m_error = _T("连不上");
    FA_EXPECT_FALSE(ParseBotListResponse(broken).m_transportOk);
    FA_EXPECT_STREQ(ParseBotListResponse(broken).m_message, L"连不上");
    FA_EXPECT_FALSE(ParseBotListResponse(MakeHttpOk("not json")).Succeeded());
    FA_EXPECT_FALSE(ParseBotDetailResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}")).Succeeded());
}

// ---------------------------------------------------------------------------
// EBOT-3：BotRuleText 的转换
// ---------------------------------------------------------------------------
void TestBotRuleText()
{
    BeginCase("EBOT-3 规则与界面字段的转换：关键词、时刻、星期、名称、摘要、白名单、Webhook 地址");

    //关键词：多种分隔符、去空白、去空项与重复项，拼回去再拆开与原数组相同
    const std::vector<CString> words = botrule::SplitKeywords(_T(" VPN ，密码,VPN、 打印机 \r\n;报销；"));
    FA_EXPECT_EQ(static_cast<int>(words.size()), 4);
    if (words.size() == 4)
    {
        FA_EXPECT_STREQ(words[0], L"VPN");
        FA_EXPECT_STREQ(words[2], L"打印机");
        FA_EXPECT_STREQ(words[3], L"报销");
    }
    FA_EXPECT_STREQ(botrule::JoinKeywords(words), L"VPN，密码，打印机，报销");
    FA_EXPECT_TRUE(botrule::SplitKeywords(botrule::JoinKeywords(words)) == words);
    FA_EXPECT_TRUE(botrule::SplitKeywords(_T(" ， ")).empty());

    //时刻
    int minute = -1;
    FA_EXPECT_TRUE(botrule::ParseHhMm(_T("9:05"), &minute));
    FA_EXPECT_EQ(minute, 545);
    FA_EXPECT_TRUE(botrule::ParseHhMm(_T(" 23：59 "), &minute));
    FA_EXPECT_EQ(minute, 1439);
    FA_EXPECT_FALSE(botrule::ParseHhMm(_T("24:00"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseHhMm(_T("9:5"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseHhMm(_T("09:60"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseHhMm(_T("ab:cd"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseHhMm(_T(""), nullptr));
    FA_EXPECT_STREQ(botrule::NormalizeHhMm(_T("9:05")), L"09:05");
    FA_EXPECT_STREQ(botrule::NormalizeHhMm(_T("9")), L"");
    FA_EXPECT_STREQ(botrule::FormatHhMm(0), L"00:00");
    FA_EXPECT_STREQ(botrule::FormatHhMm(botrule::kMinutesPerDay), L"");

    //星期：掩码往返，越界的取值忽略，结果升序
    std::vector<int> days;
    days.push_back(7);
    days.push_back(1);
    days.push_back(3);
    days.push_back(8);
    const std::vector<int> sorted = botrule::WeekdaysFromMask(botrule::WeekdayMask(days));
    FA_EXPECT_EQ(static_cast<int>(sorted.size()), 3);
    FA_EXPECT_TRUE(sorted.size() == 3 && sorted[0] == 1 && sorted[1] == 3 && sorted[2] == 7);
    FA_EXPECT_STREQ(botrule::WeekdayName(7), L"周日");
    FA_EXPECT_STREQ(botrule::WeekdayName(0), L"");

    //中文名称
    FA_EXPECT_STREQ(botrule::TriggerName(kTriggerAt), L"被 @");
    FA_EXPECT_STREQ(botrule::TriggerName(kTriggerWebhook), L"外部系统推送");
    FA_EXPECT_STREQ(botrule::ActionName(kActionQa), L"用问答作答");
    FA_EXPECT_STREQ(botrule::MatchName(kMatchPrefix), L"以……开头");
    FA_EXPECT_STREQ(botrule::ScheduleKindName(kScheduleWorkdays), L"工作日");
    FA_EXPECT_STREQ(botrule::WindowDaysName(kWindowWeekend), L"周末");
    FA_EXPECT_STREQ(botrule::StatusName(kBotStatusDisabled), L"停用");

    //触发方式与动作的搭配
    FA_EXPECT_EQ(static_cast<int>(botrule::AllowedTriggers().size()), 5);
    FA_EXPECT_FALSE(botrule::IsTriggerAllowed(kTriggerSingle));
    FA_EXPECT_EQ(static_cast<int>(botrule::AllowedActions(kTriggerKeyword).size()), 3);
    FA_EXPECT_EQ(static_cast<int>(botrule::AllowedActions(kTriggerJoin).size()), 1);
    FA_EXPECT_EQ(static_cast<int>(botrule::AllowedActions(kTriggerWebhook).size()), 0);
    FA_EXPECT_FALSE(botrule::IsActionAllowed(kTriggerSchedule, kActionQa));
    FA_EXPECT_TRUE(botrule::IsActionAllowed(kTriggerWebhook, kActionExternal));
    FA_EXPECT_TRUE(botrule::HasWindow(kTriggerJoin));
    FA_EXPECT_FALSE(botrule::HasWindow(kTriggerSchedule));
    FA_EXPECT_TRUE(botrule::IsReadOnlySource(kBotSourceGitlab));
    FA_EXPECT_FALSE(botrule::IsReadOnlySource(kBotSourceRules));

    //应用范围文字
    BotItem all;
    FA_EXPECT_STREQ(botrule::ScopeText(all, TwoGroups()), L"全部群");
    BotItem some;
    some.m_scope = kBotScopeSelected;
    some.m_groupIds.push_back(kGroupB);
    some.m_groupIds.push_back(999);
    const CString scope = botrule::ScopeText(some, TwoGroups());
    FA_EXPECT_TRUE(scope.Find(_T("2 个群")) == 0);
    FA_EXPECT_TRUE(scope.Find(_T("行政群")) > 0);
    FA_EXPECT_TRUE(scope.Find(_T("999")) > 0);

    //定时文字
    BotSchedule weekly;
    weekly.m_kind = kScheduleWeekly;
    weekly.m_time = _T("09:30");
    weekly.m_weekdays.push_back(3);
    weekly.m_weekdays.push_back(1);
    FA_EXPECT_STREQ(botrule::ScheduleText(weekly), L"每周一、三 09:30");
    BotSchedule monthly;
    monthly.m_kind = kScheduleMonthly;
    monthly.m_monthDay = 15;
    FA_EXPECT_STREQ(botrule::ScheduleText(monthly), L"每月 15 号 09:00");

    //规则摘要
    const BotDetailResult detail = ParseBotDetailResponse(MakeHttpOk(kDetailJson));
    if (detail.m_bot.m_rules.size() == 4)
    {
        const CString kw = botrule::RuleSummary(detail.m_bot.m_rules[0]);
        FA_EXPECT_TRUE(kw.Find(_T("VPN 问题：")) == 0);
        FA_EXPECT_TRUE(kw.Find(_T("关键词「VPN、密码」（包含，须同时 @）")) > 0);
        FA_EXPECT_TRUE(kw.Find(_T("工作日 18:00～09:00")) > 0);
        FA_EXPECT_TRUE(kw.Find(_T("回复「重启客户端」")) > 0);
        const CString sch = botrule::RuleSummary(detail.m_bot.m_rules[1]);
        FA_EXPECT_TRUE(sch.Find(_T("[停用] ")) == 0);
        FA_EXPECT_TRUE(sch.Find(_T("每周一、三 09:30，发往 1 个群")) > 0);
        FA_EXPECT_TRUE(botrule::RuleSummary(detail.m_bot.m_rules[2]).Find(_T("呼叫外部服务 api.example.com")) > 0);
        FA_EXPECT_TRUE(botrule::RuleSummary(detail.m_bot.m_rules[3]).Find(_T("外部系统推送")) >= 0);
    }
    BotRule longReply = MakeAtRule(CString(), _T("一二三四五六七八九十一二三四五六七八九十"));
    FA_EXPECT_TRUE(botrule::RuleSummary(longReply).Find(_T("…」")) > 0);

    //试一试结果
    BotTryResult tr;
    tr.m_kind = kTryKindReply;
    tr.m_ruleName = _T("VPN 问题");
    tr.m_text = _T("重启");
    FA_EXPECT_STREQ(botrule::TryResultText(tr), L"命中规则「VPN 问题」，会回复：重启");
    tr.m_kind = kTryKindExternal;
    tr.m_ruleName.Empty();
    tr.m_url = _T("https://a.example.com");
    FA_EXPECT_TRUE(botrule::TryResultText(tr).Find(_T("一条未命名的规则")) > 0);
    tr.m_kind = kTryKindNone;
    tr.m_reason = kTryReasonNoRule;
    FA_EXPECT_STREQ(botrule::TryResultText(tr), L"没有规则命中，机器人不会回复");
    tr.m_reason = kTryReasonCooldown;
    FA_EXPECT_TRUE(botrule::TryResultText(tr).Find(_T("冷却")) == 0);
    tr.m_reason = kTryReasonNotTrigger;
    FA_EXPECT_STREQ(botrule::TryResultText(tr), L"这种消息不会触发机器人");

    //外部服务地址与白名单（与服务端 IsUrlAllowed 一致）
    CString host;
    FA_EXPECT_TRUE(botrule::ParseUrlHost(_T("HTTPS://Api.Example.com:8443/hook?a=1"), &host));
    FA_EXPECT_STREQ(host, L"api.example.com");
    FA_EXPECT_FALSE(botrule::ParseUrlHost(_T("ftp://example.com"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseUrlHost(_T("https://user@evil.com"), nullptr));
    FA_EXPECT_FALSE(botrule::ParseUrlHost(_T("https:///path"), nullptr));
    std::vector<CString> whitelist;
    whitelist.push_back(_T("Example.com"));
    FA_EXPECT_TRUE(botrule::IsUrlInWhitelist(_T("https://example.com/a"), whitelist));
    FA_EXPECT_TRUE(botrule::IsUrlInWhitelist(_T("http://api.example.com"), whitelist));
    FA_EXPECT_FALSE(botrule::IsUrlInWhitelist(_T("https://badexample.com"), whitelist));

    //域名
    FA_EXPECT_TRUE(botrule::IsValidDomain(_T("api.example-1.org")));
    FA_EXPECT_FALSE(botrule::IsValidDomain(_T("localhost")));
    FA_EXPECT_FALSE(botrule::IsValidDomain(_T(".example.com")));
    FA_EXPECT_FALSE(botrule::IsValidDomain(_T("example..com")));
    FA_EXPECT_FALSE(botrule::IsValidDomain(_T("例子.com")));
    const std::vector<CString> split = botrule::SplitDomains(_T(" Example.com \r\n\r\napi.example.org\nEXAMPLE.COM"));
    FA_EXPECT_EQ(static_cast<int>(split.size()), 2);
    FA_EXPECT_TRUE(split.size() == 2 && split[0] == _T("example.com"));
    FA_EXPECT_TRUE(botrule::SplitDomains(botrule::JoinDomains(split)) == split);
    CString error;
    FA_EXPECT_TRUE(botrule::ValidateDomains(split, &error));
    std::vector<CString> badDomains = split;
    badDomains.push_back(_T("not a domain"));
    FA_EXPECT_FALSE(botrule::ValidateDomains(badDomains, &error));
    FA_EXPECT_TRUE(error.Find(_T("not a domain")) > 0);
    std::vector<CString> tooMany;
    for (int i = 0; i <= kBotDomainMax; ++i)
    {
        CString d;
        d.Format(_T("d%d.example.com"), i);
        tooMany.push_back(d);
    }
    FA_EXPECT_FALSE(botrule::ValidateDomains(tooMany, &error));

    //头像底色
    FA_EXPECT_TRUE(botrule::IsValidColor(_T("")));
    FA_EXPECT_TRUE(botrule::IsValidColor(_T("#1e88E5")));
    FA_EXPECT_FALSE(botrule::IsValidColor(_T("1E88E5")));
    FA_EXPECT_FALSE(botrule::IsValidColor(_T("#1E88G5")));
    FA_EXPECT_EQ(botrule::PaletteIndexOf(_T("#1e88e5")), 0);
    FA_EXPECT_EQ(botrule::PaletteIndexOf(_T("#ABCDEF")), -1);
    FA_EXPECT_TRUE(botrule::ColorFromHex(_T("#1E88E5"), 0) == RGB(0x1E, 0x88, 0xE5));
    FA_EXPECT_TRUE(botrule::ColorFromHex(_T("bad"), RGB(1, 2, 3)) == RGB(1, 2, 3));
    BotItem avatar;
    avatar.m_name = _T("小助手");
    FA_EXPECT_STREQ(botrule::AvatarGlyph(avatar), L"小");
    avatar.m_glyph = _T("IT");
    FA_EXPECT_STREQ(botrule::AvatarGlyph(avatar), L"IT");

    //Webhook 地址（服务端没有下发对外地址时按连接地址拼）
    FA_EXPECT_STREQ(WebhookUrl(MakeServer(), _T("/bot/webhook.do"), CString()), L"http://127.0.0.1:12345/bot/webhook.do");
    FA_EXPECT_STREQ(WebhookUrl(MakeServer(), CString(), CString()), L"");
}

// ---------------------------------------------------------------------------
// EBOT-4：本地校验
// ---------------------------------------------------------------------------
void TestBotValidate()
{
    BeginCase("EBOT-4 提交前的本地校验与服务端 ValidateBot 一致");
    CString error;

    BotItem bot;
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    FA_EXPECT_STREQ(error, L"请填写名称");
    bot.m_name = _T("一二三四五六七八九十一二三四五六七八九十一");
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    bot.m_name = _T("IT 小助手");
    FA_EXPECT_TRUE(botrule::ValidateBot(bot, nullptr, &error));
    FA_EXPECT_TRUE(error.IsEmpty());
    CString longDesc;
    for (int i = 0; i <= kBotDescMaxChars; ++i)
        longDesc += _T("字");
    bot.m_desc = longDesc;
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    bot.m_desc.Empty();
    bot.m_glyph = _T("ABC");
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    bot.m_glyph = _T("AB");
    bot.m_color = _T("red");
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    bot.m_color.Empty();
    bot.m_scope = kBotScopeSelected;
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    FA_EXPECT_STREQ(error, L"请至少选择一个群");
    bot.m_groupIds.push_back(kGroupA);
    FA_EXPECT_TRUE(botrule::ValidateBot(bot, nullptr, &error));

    //规则：50 条以内，逐条校验，原因前写明第几条
    for (int i = 0; i <= kBotRuleMax; ++i)
        bot.m_rules.push_back(MakeAtRule(CString(), _T("你好")));
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    bot.m_rules.resize(2);
    bot.m_rules[1].m_action.m_text = _T("  ");
    FA_EXPECT_FALSE(botrule::ValidateBot(bot, nullptr, &error));
    FA_EXPECT_STREQ(error, L"第 2 条规则：请填写回复文字");

    //关键词规则
    BotRule kw = MakeAtRule(CString(), _T("回复"));
    kw.m_trigger.m_type = kTriggerKeyword;
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    FA_EXPECT_STREQ(error, L"请至少填写一个关键词");
    kw.m_trigger.m_keywords.push_back(_T("VPN"));
    FA_EXPECT_TRUE(botrule::ValidateRule(kw, nullptr, &error));
    kw.m_trigger.m_keywords.push_back(_T("一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三"));
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    kw.m_trigger.m_keywords.resize(1);
    for (int i = 0; i < kBotKeywordMax; ++i)
    {
        CString w;
        w.Format(_T("w%d"), i);
        kw.m_trigger.m_keywords.push_back(w);
    }
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    kw.m_trigger.m_keywords.resize(1);
    kw.m_trigger.m_match = 0;
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    kw.m_trigger.m_match = kMatchExact;
    CString longName;
    for (int i = 0; i <= kBotRuleNameMaxChars; ++i)
        longName += _T("名");
    kw.m_name = longName;
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    kw.m_name.Empty();
    CString longReply;
    for (int i = 0; i <= kBotReplyMaxChars; ++i)
        longReply += _T("字");
    kw.m_action.m_text = longReply;
    FA_EXPECT_FALSE(botrule::ValidateRule(kw, nullptr, &error));
    //用问答作答时文字是兜底文字，可以为空
    kw.m_action.m_type = kActionQa;
    kw.m_action.m_text.Empty();
    FA_EXPECT_TRUE(botrule::ValidateRule(kw, nullptr, &error));

    //定时规则
    BotRule sch = MakeAtRule(CString(), _T("早会"));
    sch.m_trigger.m_type = kTriggerSchedule;
    sch.m_trigger.m_schedule.m_time = _T("25:00");
    FA_EXPECT_FALSE(botrule::ValidateRule(sch, nullptr, &error));
    sch.m_trigger.m_schedule.m_time = _T("09:00");
    FA_EXPECT_TRUE(botrule::ValidateRule(sch, nullptr, &error));
    sch.m_trigger.m_schedule.m_kind = kScheduleWeekly;
    FA_EXPECT_FALSE(botrule::ValidateRule(sch, nullptr, &error));
    FA_EXPECT_STREQ(error, L"每周定时请至少选择一天");
    sch.m_trigger.m_schedule.m_weekdays.push_back(5);
    FA_EXPECT_TRUE(botrule::ValidateRule(sch, nullptr, &error));
    sch.m_trigger.m_schedule.m_kind = kScheduleMonthly;
    sch.m_trigger.m_schedule.m_monthDay = kMaxMonthDay + 1;
    FA_EXPECT_FALSE(botrule::ValidateRule(sch, nullptr, &error));
    sch.m_trigger.m_schedule.m_monthDay = kMaxMonthDay;
    FA_EXPECT_TRUE(botrule::ValidateRule(sch, nullptr, &error));
    //入群、定时只能回复文字
    sch.m_action.m_type = kActionQa;
    FA_EXPECT_FALSE(botrule::ValidateRule(sch, nullptr, &error));

    //时间段
    BotRule win = MakeAtRule(CString(), _T("下班了"));
    win.m_trigger.m_window.m_on = true;
    win.m_trigger.m_window.m_from = _T("18:00");
    win.m_trigger.m_window.m_to = _T("9点");
    FA_EXPECT_FALSE(botrule::ValidateRule(win, nullptr, &error));
    win.m_trigger.m_window.m_to = _T("09:00");
    FA_EXPECT_TRUE(botrule::ValidateRule(win, nullptr, &error));

    //触发方式：企业机器人不能用「收到单聊」；外部系统推送没有动作，也不检查回复文字
    BotRule single = MakeAtRule(CString(), _T("x"));
    single.m_trigger.m_type = kTriggerSingle;
    FA_EXPECT_FALSE(botrule::ValidateRule(single, nullptr, &error));
    BotRule hook;
    hook.m_trigger.m_type = kTriggerWebhook;
    FA_EXPECT_TRUE(botrule::ValidateRule(hook, nullptr, &error));

    //外部服务地址：http / https；白名单已拉到时检查主机
    BotRule ext = MakeAtRule(CString(), CString());
    ext.m_action.m_type = kActionExternal;
    FA_EXPECT_FALSE(botrule::ValidateRule(ext, nullptr, &error));
    FA_EXPECT_STREQ(error, L"请填写外部服务地址");
    ext.m_action.m_url = _T("ftp://example.com");
    FA_EXPECT_FALSE(botrule::ValidateRule(ext, nullptr, &error));
    ext.m_action.m_url = _T("https://api.example.com/hook");
    FA_EXPECT_TRUE(botrule::ValidateRule(ext, nullptr, &error));
    std::vector<CString> empty;
    FA_EXPECT_FALSE(botrule::ValidateRule(ext, &empty, &error));
    FA_EXPECT_TRUE(error.Find(_T("域名白名单")) >= 0);
    std::vector<CString> other;
    other.push_back(_T("other.com"));
    FA_EXPECT_FALSE(botrule::ValidateRule(ext, &other, &error));
    std::vector<CString> ok;
    ok.push_back(_T("example.com"));
    FA_EXPECT_TRUE(botrule::ValidateRule(ext, &ok, &error));
}

// ---------------------------------------------------------------------------
// EBOT-5：机器人页
// ---------------------------------------------------------------------------
void TestBotScreen()
{
    BeginCase("EBOT-5 机器人页：进入即拉取、行数与只读行、编辑删除停用、保存试一试白名单的结果");
    AdminSession::Instance().Set(_T("tok-bot"), 1, _T("企业管理员"), _T("ent@test.com"), kEnt);
    EnterpriseBotScreen screen;
    Captured captured;
    screen.SetRequestSink(&CaptureRequest, &captured);
    screen.SetUiRequestHandler(&CaptureUi, &captured);
    screen.SetRect(kScreenRect);

    //企业管理员进入：拉机器人、群与白名单，不带 enterpriseid
    screen.OnEnter();
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 3);
    if (captured.m_requests.size() == 3)
    {
        FA_EXPECT_TRUE(captured.m_requests[0].m_tag == static_cast<UINT_PTR>(kApiTagBotList));
        FA_EXPECT_TRUE(captured.m_requests[1].m_tag == static_cast<UINT_PTR>(kApiTagBotGroups));
        FA_EXPECT_TRUE(captured.m_requests[2].m_tag == static_cast<UINT_PTR>(kApiTagBotDomains));
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(captured.m_requests[0], body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    }
    FA_EXPECT_EQ(screen.LoadStateValue(), static_cast<int>(EnterpriseBotScreen::kLoadLoading));
    FA_EXPECT_EQ(screen.TableRowCount(), 0);

    //收到列表：两行；仓库通知的机器人不显示「编辑」「删除」；停用 / 启用按状态二选一
    screen.OnApiResult(kApiTagBotList, MakeHttpOk(kTwoBotsJson));
    FA_EXPECT_EQ(screen.LoadStateValue(), static_cast<int>(EnterpriseBotScreen::kLoadLoaded));
    FA_EXPECT_EQ(screen.TableRowCount(), 2);
    FA_EXPECT_TRUE(screen.IsRowActionVisible(0, EnterpriseBotScreen::kActionEdit));
    FA_EXPECT_TRUE(screen.IsRowActionVisible(0, EnterpriseBotScreen::kActionDisable));
    FA_EXPECT_FALSE(screen.IsRowActionVisible(0, EnterpriseBotScreen::kActionEnable));
    FA_EXPECT_TRUE(screen.IsRowActionVisible(0, EnterpriseBotScreen::kActionDelete));
    FA_EXPECT_FALSE(screen.IsRowActionVisible(1, EnterpriseBotScreen::kActionEdit));
    FA_EXPECT_FALSE(screen.IsRowActionVisible(1, EnterpriseBotScreen::kActionDelete));
    FA_EXPECT_TRUE(screen.IsRowActionVisible(1, EnterpriseBotScreen::kActionEnable));
    FA_EXPECT_FALSE(screen.IsRowActionVisible(1, EnterpriseBotScreen::kActionDisable));

    screen.OnApiResult(kApiTagBotGroups, MakeHttpOk(u8"{\"code\":0,\"msg\":\"ok\",\"list\":[{\"groupid\":\"268435457\",\"name\":\"研发群\"}]}"));
    FA_EXPECT_EQ(static_cast<int>(screen.Groups().size()), 1);
    FA_EXPECT_EQ(LastUi(captured, EnterpriseBotScreen::kUiGroupsLoaded).m_kind,
                 static_cast<int>(EnterpriseBotScreen::kUiGroupsLoaded));
    FA_EXPECT_FALSE(screen.DomainsLoaded());
    screen.OnApiResult(kApiTagBotDomains, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"domains\":[\"example.com\"]}"));
    FA_EXPECT_TRUE(screen.DomainsLoaded());
    FA_EXPECT_EQ(static_cast<int>(screen.Domains().size()), 1);

    //仓库通知的机器人点「编辑」「删除」不起作用（链接本就不显示）
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.OnRowAction(1, EnterpriseBotScreen::kActionEdit);
    screen.OnRowAction(1, EnterpriseBotScreen::kActionDelete);
    FA_EXPECT_TRUE(captured.m_requests.empty());
    FA_EXPECT_TRUE(captured.m_ui.empty());

    //「编辑」先拉详情；拉到的是别的机器人（过时）时丢弃，是这一个时打开编辑窗
    screen.OnRowAction(0, EnterpriseBotScreen::kActionEdit);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    FA_EXPECT_TRUE(captured.m_requests[0].m_path == L"/admin/bot/detail.do");
    screen.OnApiResult(kApiTagBotDetail, MakeHttpOk(u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"99\",\"name\":\"别的\"}}"));
    FA_EXPECT_EQ(LastUi(captured, EnterpriseBotScreen::kUiOpenEditModal).m_kind, 0);
    screen.OnRowAction(0, EnterpriseBotScreen::kActionEdit);
    screen.OnApiResult(kApiTagBotDetail, MakeHttpOk(kDetailJson));
    const EnterpriseBotScreen::UiRequest edit = LastUi(captured, EnterpriseBotScreen::kUiOpenEditModal);
    FA_EXPECT_EQ(edit.m_kind, static_cast<int>(EnterpriseBotScreen::kUiOpenEditModal));
    FA_EXPECT_EQ(static_cast<int>(edit.m_bot.m_rules.size()), 4);

    //「停用」直接提交，成功后提示并重拉
    captured.m_requests.clear();
    screen.OnRowAction(0, EnterpriseBotScreen::kActionDisable);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    Json::Value body;
    FA_EXPECT_TRUE(ParseBody(captured.m_requests[0], body));
    FA_EXPECT_EQ(body["status"].asInt(), kBotStatusDisabled);
    screen.OnApiResult(kApiTagBotSetStatus, MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}"));
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 4);

    //「删除」先确认：取消不发请求，确认才发
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.OnRowAction(0, EnterpriseBotScreen::kActionDelete);
    const EnterpriseBotScreen::UiRequest confirm = LastUi(captured, EnterpriseBotScreen::kUiConfirm);
    FA_EXPECT_TRUE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("IT 小助手")) > 0);
    screen.OnConfirmResult(false);
    FA_EXPECT_TRUE(captured.m_requests.empty());
    screen.OnRowAction(0, EnterpriseBotScreen::kActionDelete);
    screen.OnConfirmResult(true);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    FA_EXPECT_TRUE(captured.m_requests[0].m_path == L"/admin/bot/delete.do");

    //保存：失败时编辑窗里显示第几条规则的问题；成功时关编辑窗并重拉；没有应答前不重复提交
    BotItem bot;
    bot.m_name = _T("新机器人");
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.SubmitBot(bot);
    screen.SubmitBot(bot);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    FA_EXPECT_TRUE(screen.IsSaving());
    screen.OnApiResult(kApiTagBotSave, MakeHttpOk("{\"code\":170,\"msg\":\"rule 0: bad keyword\"}"));
    FA_EXPECT_FALSE(screen.IsSaving());
    const EnterpriseBotScreen::UiRequest modalError = LastUi(captured, EnterpriseBotScreen::kUiModalError);
    FA_EXPECT_STREQ(modalError.m_text, L"第 1 条规则：关键词不能为空，每个最多 32 个字");
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.SubmitBot(bot);
    screen.OnApiResult(kApiTagBotSave, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"botid\":\"20\"}"));
    FA_EXPECT_EQ(captured.m_ui[0].m_kind, static_cast<int>(EnterpriseBotScreen::kUiCloseModal));
    //保存一次、重拉机器人、群与白名单各一次
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 4);

    //试一试：结果写成文字交给编辑窗；失败时按错误显示
    captured.m_ui.clear();
    BotTryInput input;
    input.m_text = _T("VPN");
    screen.RequestTry(bot, input);
    screen.OnApiResult(kApiTagBotTry, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"matched\":0,\"kind\":0,\"reason\":1}"));
    const EnterpriseBotScreen::UiRequest tryResult = LastUi(captured, EnterpriseBotScreen::kUiTryResult);
    FA_EXPECT_STREQ(tryResult.m_text, L"没有规则命中，机器人不会回复");
    FA_EXPECT_FALSE(tryResult.m_isError);
    screen.RequestTry(bot, input);
    screen.OnApiResult(kApiTagBotTry, MakeHttpOk("{\"code\":171,\"msg\":\"x\"}"));
    FA_EXPECT_TRUE(LastUi(captured, EnterpriseBotScreen::kUiTryResult).m_isError);

    //重新生成：没保存的机器人只提示；已保存的先确认，确认后发请求，结果交给编辑窗
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.RequestResetToken(0);
    FA_EXPECT_TRUE(LastUi(captured, EnterpriseBotScreen::kUiToast).m_isError);
    FA_EXPECT_TRUE(captured.m_requests.empty());
    screen.RequestResetToken(kBotIt);
    FA_EXPECT_EQ(LastUi(captured, EnterpriseBotScreen::kUiConfirm).m_kind, static_cast<int>(EnterpriseBotScreen::kUiConfirm));
    screen.OnConfirmResult(true);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    FA_EXPECT_TRUE(captured.m_requests[0].m_path == L"/admin/bot/resettoken.do");
    screen.OnApiResult(kApiTagBotResetToken, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"webhooktoken\":\"t2\",\"secret\":\"s2\"}"));
    const EnterpriseBotScreen::UiRequest secrets = LastUi(captured, EnterpriseBotScreen::kUiSecretsReset);
    FA_EXPECT_TRUE(secrets.m_bot.m_botId == kBotIt);
    FA_EXPECT_STREQ(secrets.m_bot.m_webhookToken, L"t2");

    //白名单：打开时带上当前白名单；保存成功后关窗口并更新本地白名单，失败时在窗口里提示
    captured.m_ui.clear();
    screen.OnDomainsClicked();
    FA_EXPECT_EQ(static_cast<int>(LastUi(captured, EnterpriseBotScreen::kUiOpenDomainsModal).m_domains.size()), 1);
    std::vector<CString> domains;
    domains.push_back(_T("a.example.com"));
    domains.push_back(_T("b.example.com"));
    screen.SubmitDomains(domains);
    screen.OnApiResult(kApiTagBotSetDomains, MakeHttpOk("{\"code\":175,\"msg\":\"bad\"}"));
    FA_EXPECT_EQ(LastUi(captured, EnterpriseBotScreen::kUiDomainsModalError).m_kind,
                 static_cast<int>(EnterpriseBotScreen::kUiDomainsModalError));
    FA_EXPECT_EQ(static_cast<int>(screen.Domains().size()), 1);
    screen.SubmitDomains(domains);
    screen.OnApiResult(kApiTagBotSetDomains, MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}"));
    FA_EXPECT_EQ(LastUi(captured, EnterpriseBotScreen::kUiCloseDomainsModal).m_kind,
                 static_cast<int>(EnterpriseBotScreen::kUiCloseDomainsModal));
    FA_EXPECT_EQ(static_cast<int>(screen.Domains().size()), 2);

    //平台超管：进入时只拉企业列表；没选企业时新建与白名单只提示
    AdminSession::Instance().Set(_T("tok-bot-plat"), 2, _T("平台超管"), _T("root@test.com"), 0);
    EnterpriseBotScreen platform;
    Captured platCap;
    platform.SetRequestSink(&CaptureRequest, &platCap);
    platform.SetUiRequestHandler(&CaptureUi, &platCap);
    platform.SetRect(kScreenRect);
    platform.OnEnter();
    FA_EXPECT_EQ(static_cast<int>(platCap.m_requests.size()), 1);
    FA_EXPECT_TRUE(platCap.m_requests.size() == 1 && platCap.m_requests[0].m_tag == static_cast<UINT_PTR>(kApiTagBotEnterprises));
    platform.OnAddClicked();
    platform.OnDomainsClicked();
    FA_EXPECT_EQ(LastUi(platCap, EnterpriseBotScreen::kUiOpenAddModal).m_kind, 0);
    FA_EXPECT_EQ(LastUi(platCap, EnterpriseBotScreen::kUiOpenDomainsModal).m_kind, 0);
    FA_EXPECT_TRUE(LastUi(platCap, EnterpriseBotScreen::kUiToast).m_text.Find(_T("请先选择企业")) == 0);

    //ActionTable 让出不显示链接的位置：第 0 行隐去中间的链接后，第三个链接移到第二个链接的位置
    ui::ActionTable table;
    table.SetRect(kScreenRect);
    const int kRowH = 40;
    const int kActionAreaW = 240;
    table.SetRowHeight(kRowH);
    table.SetActionAreaWidth(kActionAreaW);
    table.AddAction(_T("编辑"), 0);
    table.AddAction(_T("停用"), 0);
    table.AddAction(_T("删除"), 0);
    table.SetRowCount(2);
    struct HideMiddleOnRow0
    {
        static bool Visible(void* /*user*/, int rowIndex, int actionIndex)
        {
            return !(rowIndex == 0 && actionIndex == 1);
        }
    };
    table.SetActionVisibility(&HideMiddleOnRow0::Visible, nullptr);
    FA_EXPECT_TRUE(table.GetLinkRect(0, 2).left > table.GetLinkRect(1, 1).left);
    table.SetCollapseHiddenActions(true);
    FA_EXPECT_EQ(table.GetLinkRect(0, 2).left, table.GetLinkRect(1, 1).left);
    //全部显示的行不受影响：第三个链接仍在第二个之后
    FA_EXPECT_TRUE(table.GetLinkRect(1, 2).left > table.GetLinkRect(1, 1).left);
}

// ---------------------------------------------------------------------------
// EBOT-6：机器人编辑窗
// ---------------------------------------------------------------------------
void TestBotModal()
{
    BeginCase("EBOT-6 机器人编辑窗：新建默认值、应用范围、规则增删与上下移、编辑预填、Webhook 与试一试");
    EnterpriseBotModal modal;
    modal.SetRect(kWindowRect);
    modal.SetServer(MakeServer());

    //新建：没有规则，底色为第一个色块，只有「添加」可用；没填名称时校验不过
    modal.ShowForAdd(std::vector<BotGroup>());
    FA_EXPECT_FALSE(modal.IsEditing());
    FA_EXPECT_EQ(modal.RuleCount(), 0);
    FA_EXPECT_EQ(modal.ColorIndex(), 0);
    FA_EXPECT_TRUE(modal.IsRuleButtonEnabled(kIdBotModalRuleAdd));
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleEdit));
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleUp));
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleDelete));
    FA_EXPECT_FALSE(modal.HasSecrets());
    FA_EXPECT_STREQ(modal.WebhookUrlText(), L"");
    CString error;
    FA_EXPECT_FALSE(modal.Validate(&error));
    DuiEdit* name = EditById(modal, kIdBotModalName);
    FA_EXPECT_NOTNULL(name);
    if (name == nullptr)
        return;
    name->SetText(_T("  值班提醒 "));
    FA_EXPECT_TRUE(modal.Validate(&error));
    BotItem got = modal.GetBot();
    FA_EXPECT_STREQ(got.m_name, L"值班提醒");
    FA_EXPECT_STREQ(got.m_color, L"#1E88E5");
    FA_EXPECT_EQ(got.m_status, kBotStatusActive);
    modal.SelectColor(3);
    FA_EXPECT_STREQ(modal.GetBot().m_color, botrule::PaletteColor(3));

    //应用范围：所选的群至少一个；群列表晚到时保留勾选；改回全部群时不带群
    modal.SetScope(kBotScopeSelected);
    FA_EXPECT_FALSE(modal.Validate(&error));
    modal.SetGroups(TwoGroups());
    FA_EXPECT_EQ(modal.GroupRowCount(), 2);
    modal.SetGroupRowChecked(1, true);
    modal.SetGroups(TwoGroups());
    FA_EXPECT_TRUE(modal.IsGroupRowChecked(1));
    FA_EXPECT_FALSE(modal.IsGroupRowChecked(0));
    got = modal.GetBot();
    FA_EXPECT_TRUE(got.m_groupIds.size() == 1 && got.m_groupIds[0] == kGroupB);
    FA_EXPECT_TRUE(modal.Validate(&error));
    modal.SetScope(kBotScopeAll);
    FA_EXPECT_TRUE(modal.GetBot().m_groupIds.empty());

    //规则：添加三条（每次选中新加的那一条），上移、下移、编辑、删除
    BotRule rule;
    FA_EXPECT_TRUE(modal.BeginAddRule(&rule));
    FA_EXPECT_EQ(rule.m_trigger.m_type, kTriggerAt);
    modal.ApplyRuleEdit(MakeAtRule(_T("甲"), _T("a")));
    FA_EXPECT_TRUE(modal.BeginAddRule(&rule));
    modal.ApplyRuleEdit(MakeAtRule(_T("乙"), _T("b")));
    FA_EXPECT_TRUE(modal.BeginAddRule(&rule));
    modal.ApplyRuleEdit(MakeAtRule(_T("丙"), _T("c")));
    FA_EXPECT_EQ(modal.RuleCount(), 3);
    FA_EXPECT_EQ(modal.SelectedRuleIndex(), 2);
    FA_EXPECT_TRUE(modal.IsRuleButtonEnabled(kIdBotModalRuleUp));
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleDown));
    FA_EXPECT_FALSE(modal.MoveSelectedRule(kMoveDown));
    FA_EXPECT_TRUE(modal.MoveSelectedRule(kMoveUp));
    FA_EXPECT_EQ(modal.SelectedRuleIndex(), 1);
    FA_EXPECT_STREQ(modal.RuleAt(1).m_name, L"丙");
    FA_EXPECT_STREQ(modal.RuleAt(2).m_name, L"乙");
    FA_EXPECT_TRUE(modal.IsRuleButtonEnabled(kIdBotModalRuleDown));
    modal.SelectRule(0);
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleUp));
    FA_EXPECT_FALSE(modal.MoveSelectedRule(kMoveUp));
    FA_EXPECT_TRUE(modal.BeginEditRule(&rule));
    FA_EXPECT_STREQ(rule.m_name, L"甲");
    rule.m_name = _T("甲改");
    modal.ApplyRuleEdit(rule);
    FA_EXPECT_EQ(modal.RuleCount(), 3);
    FA_EXPECT_STREQ(modal.RuleAt(0).m_name, L"甲改");
    FA_EXPECT_EQ(modal.SelectedRuleIndex(), 0);
    FA_EXPECT_TRUE(modal.DeleteSelectedRule());
    FA_EXPECT_EQ(modal.RuleCount(), 2);
    FA_EXPECT_STREQ(modal.RuleAt(0).m_name, L"丙");
    FA_EXPECT_EQ(modal.SelectedRuleIndex(), 0);
    got = modal.GetBot();
    FA_EXPECT_EQ(static_cast<int>(got.m_rules.size()), 2);
    FA_EXPECT_STREQ(got.m_rules[1].m_name, L"乙");
    //没有选中时「编辑」提示
    modal.SelectRule(-1);
    FA_EXPECT_FALSE(modal.BeginEditRule(&rule));
    FA_EXPECT_FALSE(modal.ErrorText().IsEmpty());
    FA_EXPECT_FALSE(modal.DeleteSelectedRule());
    //规则满 50 条：不能再添加
    for (int i = modal.RuleCount(); i < kBotRuleMax; ++i)
    {
        modal.BeginAddRule(&rule);
        modal.ApplyRuleEdit(MakeAtRule(CString(), _T("x")));
    }
    FA_EXPECT_EQ(modal.RuleCount(), kBotRuleMax);
    FA_EXPECT_FALSE(modal.IsRuleButtonEnabled(kIdBotModalRuleAdd));
    FA_EXPECT_FALSE(modal.BeginAddRule(&rule));

    //编辑：预填；色块外的底色原样保留；令牌与签名密钥显示，Webhook 地址按服务器配置拼出
    const BotDetailResult detail = ParseBotDetailResponse(MakeHttpOk(kDetailJson));
    modal.ShowForEdit(detail.m_bot, TwoGroups());
    FA_EXPECT_TRUE(modal.IsEditing());
    FA_EXPECT_TRUE(modal.EditingId() == kBotIt);
    FA_EXPECT_EQ(modal.RuleCount(), 4);
    FA_EXPECT_EQ(modal.SelectedRuleIndex(), 0);
    FA_EXPECT_EQ(modal.ColorIndex(), -1);
    FA_EXPECT_TRUE(modal.IsGroupRowChecked(0));
    got = modal.GetBot();
    FA_EXPECT_STREQ(got.m_color, L"#ABCDEF");
    FA_EXPECT_EQ(got.m_scope, kBotScopeSelected);
    FA_EXPECT_TRUE(got.m_allowGroupOff);
    FA_EXPECT_TRUE(modal.HasSecrets());
    FA_EXPECT_STREQ(modal.WebhookUrlText(), L"http://127.0.0.1:12345/bot/webhook.do");
    FA_EXPECT_TRUE(modal.WebhookCopyText().Find(_T("tok32")) > 0);
    FA_EXPECT_TRUE(modal.WebhookCopyText().Find(_T("sec32")) > 0);
    //白名单里没有 api.example.com 时第 3 条规则（呼叫外部服务）校验不过
    std::vector<CString> domains;
    domains.push_back(_T("other.com"));
    modal.SetDomains(domains, true);
    FA_EXPECT_FALSE(modal.Validate(&error));
    FA_EXPECT_TRUE(error.Find(_T("第 3 条规则")) == 0);
    domains.push_back(_T("example.com"));
    modal.SetDomains(domains, true);
    FA_EXPECT_TRUE(modal.Validate(&error));
    //重新生成：别的机器人的结果不理会，这一个的结果更新显示
    modal.SetSecrets(kBotRepo, _T("x"), _T("y"));
    FA_EXPECT_TRUE(modal.WebhookCopyText().Find(_T("tok32")) > 0);
    modal.SetSecrets(kBotIt, _T("newtok"), _T("newsec"));
    FA_EXPECT_TRUE(modal.WebhookCopyText().Find(_T("newtok")) > 0);

    //试一试的输入：文字去空白、是否 @、所选的群
    DuiEdit* tryText = EditById(modal, kIdBotModalTryText);
    DuiButton* tryAt = ButtonById(modal, kIdBotModalTryAt);
    DuiComboBox* tryGroup = dynamic_cast<DuiComboBox*>(modal.FindCtrlById(kIdBotModalTryGroup));
    FA_EXPECT_NOTNULL(tryText);
    FA_EXPECT_NOTNULL(tryAt);
    FA_EXPECT_NOTNULL(tryGroup);
    if (tryText == nullptr || tryAt == nullptr || tryGroup == nullptr)
        return;
    tryText->SetText(_T(" VPN 连不上 "));
    BotTryInput input = modal.GetTryInput();
    FA_EXPECT_STREQ(input.m_text, L"VPN 连不上");
    FA_EXPECT_TRUE(input.m_at);
    FA_EXPECT_TRUE(input.m_groupId == 0);
    tryAt->SetCheck(false, false);
    FA_EXPECT_EQ(tryGroup->GetCount(), 3);
    tryGroup->SetCurSel(2, false);
    input = modal.GetTryInput();
    FA_EXPECT_FALSE(input.m_at);
    FA_EXPECT_TRUE(input.m_groupId == kGroupB);
    //群列表刷新后「试一试」选中的群保留
    modal.SetGroups(TwoGroups());
    FA_EXPECT_TRUE(modal.GetTryInput().m_groupId == kGroupB);
    modal.SetTryResult(_T("命中规则「x」"), false);
    FA_EXPECT_STREQ(modal.TryResultText(), L"命中规则「x」");
    modal.SetBusy(true);
    FA_EXPECT_TRUE(modal.IsBusy());
    modal.SetError(_T("出错了"));
    FA_EXPECT_STREQ(modal.ErrorText(), L"出错了");

    //控件都排在卡片之内（两栏）
    FA_EXPECT_TRUE(InsideNonEmpty(modal.FindCtrlById(kIdBotModalGroups)->GetRect(), kWindowRect));
    FA_EXPECT_TRUE(InsideNonEmpty(modal.FindCtrlById(kIdBotModalTryRun)->GetRect(), kWindowRect));
    FA_EXPECT_TRUE(modal.FindCtrlById(kIdBotModalRules)->GetRect().left > modal.FindCtrlById(kIdBotModalName)->GetRect().right);
}

// ---------------------------------------------------------------------------
// EBOT-7：规则编辑窗
// ---------------------------------------------------------------------------
void TestBotRuleModal()
{
    BeginCase("EBOT-7 规则编辑窗：按触发方式显隐控件、各触发方式取回的规则、外部服务地址校验、原样取回");
    BotRuleModal modal;
    modal.SetRect(kWindowRect);
    const RECT panelArea = kWindowRect;

    //默认（被 @ 时回复文字）：时间段与动作可见，关键词与定时隐藏，「按 Markdown 显示」可见、外部服务地址隐藏
    modal.ShowForRule(BotRule(), true, TwoGroups());
    FA_EXPECT_TRUE(modal.IsNew());
    FA_EXPECT_EQ(modal.TriggerType(), kTriggerAt);
    FA_EXPECT_EQ(modal.ActionType(), kActionReply);
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleKeywords));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleSchedKind));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleWindowOn));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleAction));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleMarkdown));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleUrl));
    //时间段没有勾选时其余三个控件不可用
    FA_EXPECT_FALSE(modal.FindCtrlById(kIdBotRuleWindowFrom)->IsEnabled());
    CString error;
    FA_EXPECT_FALSE(modal.Validate(&error));
    FA_EXPECT_STREQ(error, L"请填写回复文字");

    //关键词：隐藏后再显示，关键词输入框排在卡片里、触发方式下拉框之下
    modal.SetTriggerType(kTriggerKeyword);
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleKeywords));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleNeedAt));
    const RECT kwRect = modal.FindCtrlById(kIdBotRuleKeywords)->GetRect();
    FA_EXPECT_TRUE(InsideNonEmpty(kwRect, panelArea));
    FA_EXPECT_TRUE(kwRect.top > modal.FindCtrlById(kIdBotRuleTrigger)->GetRect().bottom);
    DuiEdit* keywords = EditById(modal, kIdBotRuleKeywords);
    DuiEdit* text = EditById(modal, kIdBotRuleText);
    FA_EXPECT_NOTNULL(keywords);
    FA_EXPECT_NOTNULL(text);
    if (keywords == nullptr || text == nullptr)
        return;
    keywords->SetText(_T("VPN，密码"));
    text->SetText(_T("重启客户端"));
    modal.SetActionType(kActionQa);
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleMarkdown));
    BotRule got = modal.GetRule();
    FA_EXPECT_EQ(got.m_trigger.m_type, kTriggerKeyword);
    FA_EXPECT_EQ(static_cast<int>(got.m_trigger.m_keywords.size()), 2);
    FA_EXPECT_EQ(got.m_action.m_type, kActionQa);
    FA_EXPECT_FALSE(got.m_action.m_markdown);
    FA_EXPECT_TRUE(modal.Validate(&error));

    //外部服务：地址可见；白名单为空时校验不过，主机在白名单里时通过
    modal.SetActionType(kActionExternal);
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleUrl));
    FA_EXPECT_TRUE(InsideNonEmpty(modal.FindCtrlById(kIdBotRuleUrl)->GetRect(), panelArea));
    DuiEdit* url = EditById(modal, kIdBotRuleUrl);
    FA_EXPECT_NOTNULL(url);
    if (url == nullptr)
        return;
    url->SetText(_T(" https://api.example.com/hook "));
    modal.SetDomains(std::vector<CString>(), true);
    FA_EXPECT_FALSE(modal.Validate(&error));
    std::vector<CString> domains;
    domains.push_back(_T("example.com"));
    modal.SetDomains(domains, true);
    FA_EXPECT_TRUE(modal.Validate(&error));
    FA_EXPECT_STREQ(modal.GetRule().m_action.m_url, L"https://api.example.com/hook");

    //入群：只能回复文字，动作自动改回回复文字；关键词清空
    modal.SetTriggerType(kTriggerJoin);
    FA_EXPECT_EQ(modal.ActionType(), kActionReply);
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleKeywords));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleUrl));
    got = modal.GetRule();
    FA_EXPECT_TRUE(got.m_trigger.m_keywords.empty());
    FA_EXPECT_TRUE(got.m_action.m_url.IsEmpty());

    //定时：时间段隐藏；每周时显示星期、每月时显示几号；时刻规范成 HH:MM，发往的群取勾选
    modal.SetTriggerType(kTriggerSchedule);
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleSchedKind));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleSchedGroups));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleWindowOn));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleWeekdayFirst));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleSchedDay));
    modal.SetScheduleKind(kScheduleWeekly);
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleWeekdayFirst));
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleWeekdayLast));
    FA_EXPECT_TRUE(InsideNonEmpty(modal.FindCtrlById(kIdBotRuleWeekdayLast)->GetRect(), panelArea));
    DuiEdit* timeEdit = EditById(modal, kIdBotRuleSchedTime);
    FA_EXPECT_NOTNULL(timeEdit);
    if (timeEdit == nullptr)
        return;
    timeEdit->SetText(_T("9:30"));
    FA_EXPECT_FALSE(modal.Validate(&error));
    FA_EXPECT_STREQ(error, L"每周定时请至少选择一天");
    ButtonById(modal, kIdBotRuleWeekdayFirst)->SetCheck(true, false);
    ButtonById(modal, kIdBotRuleWeekdayFirst + 2)->SetCheck(true, false);
    FA_EXPECT_TRUE(modal.Validate(&error));
    got = modal.GetRule();
    FA_EXPECT_STREQ(got.m_trigger.m_schedule.m_time, L"09:30");
    FA_EXPECT_TRUE(got.m_trigger.m_schedule.m_weekdays.size() == 2 && got.m_trigger.m_schedule.m_weekdays[1] == 3);
    FA_EXPECT_FALSE(got.m_trigger.m_window.m_on);
    modal.SetScheduleKind(kScheduleMonthly);
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleSchedDay));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleWeekdayFirst));
    EditById(modal, kIdBotRuleSchedDay)->SetText(_T("32"));
    FA_EXPECT_FALSE(modal.Validate(&error));
    EditById(modal, kIdBotRuleSchedDay)->SetText(_T("15"));
    FA_EXPECT_TRUE(modal.Validate(&error));
    FA_EXPECT_EQ(modal.GetRule().m_trigger.m_schedule.m_monthDay, 15);
    FA_EXPECT_TRUE(modal.GetRule().m_trigger.m_schedule.m_weekdays.empty());

    //外部系统推送：没有动作，动作区隐藏；校验只看触发条件
    modal.SetTriggerType(kTriggerWebhook);
    FA_EXPECT_EQ(modal.ActionType(), 0);
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleAction));
    FA_EXPECT_FALSE(IsShown(modal, kIdBotRuleText));
    FA_EXPECT_TRUE(modal.Validate(&error));

    //编辑：打开详情里的关键词规则，原样取回时 ID、时间段、个人机器人用的字段都保留
    const BotDetailResult detail = ParseBotDetailResponse(MakeHttpOk(kDetailJson));
    FA_EXPECT_EQ(static_cast<int>(detail.m_bot.m_rules.size()), 4);
    if (detail.m_bot.m_rules.size() != 4)
        return;
    modal.ShowForRule(detail.m_bot.m_rules[0], false, TwoGroups());
    FA_EXPECT_FALSE(modal.IsNew());
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleKeywords));
    FA_EXPECT_TRUE(modal.FindCtrlById(kIdBotRuleWindowFrom)->IsEnabled());
    got = modal.GetRule();
    FA_EXPECT_TRUE(got.m_id == 31);
    FA_EXPECT_STREQ(got.m_name, L"VPN 问题");
    FA_EXPECT_TRUE(got.m_trigger.m_keywords == detail.m_bot.m_rules[0].m_trigger.m_keywords);
    FA_EXPECT_TRUE(got.m_trigger.m_window.m_on);
    FA_EXPECT_EQ(got.m_trigger.m_window.m_days, kWindowWorkdays);
    FA_EXPECT_STREQ(got.m_trigger.m_window.m_to, L"09:00");
    FA_EXPECT_TRUE(got.m_action.m_markdown);
    FA_EXPECT_EQ(static_cast<int>(got.m_trigger.m_presence.size()), 2);
    FA_EXPECT_EQ(got.m_trigger.m_cooldown, 30);
    //每周定时的规则：星期、目标群回填
    modal.ShowForRule(detail.m_bot.m_rules[1], false, TwoGroups());
    FA_EXPECT_TRUE(IsShown(modal, kIdBotRuleWeekdayFirst));
    FA_EXPECT_TRUE(ButtonById(modal, kIdBotRuleWeekdayFirst)->IsChecked());
    FA_EXPECT_FALSE(ButtonById(modal, kIdBotRuleWeekdayFirst + 1)->IsChecked());
    got = modal.GetRule();
    FA_EXPECT_FALSE(got.m_enabled);
    FA_EXPECT_TRUE(got.m_trigger.m_groupIds.size() == 1 && got.m_trigger.m_groupIds[0] == kGroupB);
}

// ---------------------------------------------------------------------------
// EBOT-8：白名单窗与四个功能开关
// ---------------------------------------------------------------------------
void TestBotDomainsAndSwitches()
{
    BeginCase("EBOT-8 外部服务域名白名单窗；机器人的四个功能开关与「功能开关」页里的位置");

    BotDomainsModal domains;
    domains.SetRect(kWindowRect);
    std::vector<CString> current;
    current.push_back(_T("example.com"));
    current.push_back(_T("api.example.org"));
    domains.ShowWithDomains(current);
    FA_EXPECT_TRUE(domains.GetDomains() == current);
    domains.SetText(_T("A.example.com\r\n\r\nb.example.com\r\na.EXAMPLE.com"));
    const std::vector<CString> got = domains.GetDomains();
    FA_EXPECT_EQ(static_cast<int>(got.size()), 2);
    CString error;
    FA_EXPECT_TRUE(domains.Validate(&error));
    domains.SetText(_T("example.com\r\nbad domain"));
    FA_EXPECT_FALSE(domains.Validate(&error));
    domains.SetText(CString());
    FA_EXPECT_TRUE(domains.Validate(&error));
    FA_EXPECT_TRUE(domains.GetDomains().empty());
    domains.SetBusy(true);
    FA_EXPECT_TRUE(domains.IsBusy());

    //四个开关：键、关闭的确认框样式、紧凑名称不超过五个字
    const FeatureSwitchSpec* const specs[] = { &BotSwitchSpec(), &BotGroupSwitchSpec(), &BotPersonalSwitchSpec(),
                                               &BotGroupExternalSwitchSpec() };
    FA_EXPECT_STREQ(specs[0]->m_key, _T("bot.enabled"));
    FA_EXPECT_STREQ(specs[1]->m_key, _T("bot.groupbot"));
    FA_EXPECT_STREQ(specs[2]->m_key, _T("bot.personal"));
    FA_EXPECT_STREQ(specs[3]->m_key, _T("bot.groupexternal"));
    FA_EXPECT_TRUE(specs[0]->m_offIsDanger);
    FA_EXPECT_TRUE(CString(specs[0]->m_confirmOffText).Find(_T("本企业全部机器人停止响应")) >= 0);
    const int kCompactNameMaxChars = 5;
    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i)
    {
        const CString compact(specs[i]->m_compactName);
        FA_EXPECT_TRUE(!compact.IsEmpty() && compact.GetLength() <= kCompactNameMaxChars);
        if (i > 0)
            FA_EXPECT_FALSE(specs[i]->m_offIsDanger);
    }

    //企业详情的「功能开关」页（2026-10-08 起两种角色都在这里设置，原企业设置弹窗与平台设置页里的四个开关已删除）：
    //「机器人」卡片排在含云端录像空间的「音视频」卡片之后（docs/机器人方案.md §11 Q18），开关编号与结果标签与 AdminFrame 分发用的一致
    FeatureSwitchesPage page;
    const int botItems[] = { FeatureSwitchesPage::kItemBot, FeatureSwitchesPage::kItemBotGroup,
                             FeatureSwitchesPage::kItemBotPersonal, FeatureSwitchesPage::kItemBotGroupExternal };
    const int botIds[] = { kIdFeatSwitchBot, kIdFeatSwitchBotGroup, kIdFeatSwitchBotPersonal, kIdFeatSwitchBotGroupExternal };
    for (size_t i = 0; i < sizeof(botItems) / sizeof(botItems[0]); ++i)
    {
        FeatureSwitchPanel* panel = page.Panel(botItems[i]);
        FA_EXPECT_NOTNULL(panel);
        if (panel == nullptr)
            return;
        FA_EXPECT_TRUE(page.FindCtrlById(botIds[i]) == panel->Switch());
        FA_EXPECT_STREQ(panel->Spec().m_key, specs[i]->m_key);
        FA_EXPECT_EQ(FeatureSwitchesPage::GroupOf(botItems[i]), static_cast<int>(FeatureSwitchesPage::kGroupBot));
        FA_EXPECT_EQ(panel->PanelHeight(), FeatureSwitchPanel::kPanelHeight);
    }
    FA_EXPECT_EQ(static_cast<int>(page.Panel(FeatureSwitchesPage::kItemBot)->GetTag()), static_cast<int>(kApiTagBotSwitchGet));
    FA_EXPECT_EQ(static_cast<int>(page.Panel(FeatureSwitchesPage::kItemBotGroupExternal)->SetTag()),
                 static_cast<int>(kApiTagBotExternalSwitchSet));
    FA_EXPECT_EQ(static_cast<int>(page.Panel(FeatureSwitchesPage::kItemBotPersonal)->GetTag()),
                 static_cast<int>(kApiTagBotPersonalSwitchGet));
    FA_EXPECT_EQ(static_cast<int>(page.Panel(FeatureSwitchesPage::kItemBotGroup)->SetTag()),
                 static_cast<int>(kApiTagBotGroupSwitchSet));
    //两栏与一栏两种排法下，四个面板都在「机器人」卡片里，自上而下排列，都在云端录像空间之后
    const RECT layouts[] = { RECT{ 0, 0, 1216, 1400 }, RECT{ 0, 0, 700, 2000 } };
    for (size_t k = 0; k < sizeof(layouts) / sizeof(layouts[0]); ++k)
    {
        page.SetRect(layouts[k]);
        const RECT card = page.GroupRect(FeatureSwitchesPage::kGroupBot);
        const RECT quota = page.GetCloudQuotaPanel()->GetRect();
        for (size_t i = 0; i < sizeof(botItems) / sizeof(botItems[0]); ++i)
        {
            const RECT rc = page.Panel(botItems[i])->GetRect();
            FA_EXPECT_TRUE(rc.top > quota.bottom);
            FA_EXPECT_TRUE(rc.left >= card.left && rc.right <= card.right && rc.top >= card.top && rc.bottom <= card.bottom);
            if (i > 0)
            {
                FA_EXPECT_TRUE(rc.top >= page.Panel(botItems[i - 1])->GetRect().bottom);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// EBOT-9：服务端下发的 Webhook 对外完整地址
// ---------------------------------------------------------------------------
void TestBotWebhookUrl()
{
    BeginCase("EBOT-9 服务端下发的 Webhook 对外完整地址：解析、优先显示、没有时按连接地址拼");
    const CString external = _T("https://im.example.com:8443/bot/webhook.do");

    //列表与详情都解析 webhookurl；没有这个字段时为空
    const char* const kListWithUrl =
        u8"{\"code\":0,\"msg\":\"ok\",\"list\":[{\"botid\":\"12\",\"name\":\"IT 小助手\",\"status\":0,\"source\":0,"
        u8"\"rulecount\":0,\"webhookpath\":\"/bot/webhook.do\","
        u8"\"webhookurl\":\"https://im.example.com:8443/bot/webhook.do\"}]}";
    const BotListResult list = ParseBotListResponse(MakeHttpOk(kListWithUrl));
    FA_EXPECT_TRUE(list.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(list.m_bots.size()), 1);
    if (!list.m_bots.empty())
    {
        FA_EXPECT_STREQ(list.m_bots[0].m_webhookUrl, external);
    }
    const char* const kDetailWithUrl =
        u8"{\"code\":0,\"msg\":\"ok\",\"bot\":{\"botid\":\"12\",\"name\":\"IT 小助手\",\"status\":0,\"source\":0,"
        u8"\"scope\":0,\"rulecount\":0,\"rules\":[],\"webhookpath\":\"/bot/webhook.do\","
        u8"\"webhookurl\":\"https://im.example.com:8443/bot/webhook.do\",\"webhooktoken\":\"tok32\",\"secret\":\"sec32\"}}";
    const BotDetailResult withUrl = ParseBotDetailResponse(MakeHttpOk(kDetailWithUrl));
    FA_EXPECT_TRUE(withUrl.Succeeded());
    FA_EXPECT_STREQ(withUrl.m_bot.m_webhookUrl, external);
    const BotDetailResult noUrl = ParseBotDetailResponse(MakeHttpOk(kDetailJson));
    FA_EXPECT_STREQ(noUrl.m_bot.m_webhookUrl, L"");

    //有对外地址就直接用，没有就按控制台的连接地址拼；不是可编辑的机器人（没有路径）时都为空
    FA_EXPECT_STREQ(WebhookUrl(MakeServer(), _T("/bot/webhook.do"), external), external);
    FA_EXPECT_STREQ(WebhookUrl(MakeServer(), _T("/bot/webhook.do"), CString()), L"http://127.0.0.1:12345/bot/webhook.do");
    FA_EXPECT_STREQ(WebhookUrl(MakeServer(), CString(), external), L"");

    //编辑窗显示服务端给的地址
    EnterpriseBotModal modal;
    modal.SetRect(kWindowRect);
    modal.SetServer(MakeServer());
    modal.ShowForEdit(withUrl.m_bot, TwoGroups());
    FA_EXPECT_STREQ(modal.WebhookUrlText(), external);
    FA_EXPECT_TRUE(modal.WebhookCopyText().Find(external) >= 0);
    //换成没有对外地址的机器人：退回连接地址
    modal.ShowForEdit(noUrl.m_bot, TwoGroups());
    FA_EXPECT_STREQ(modal.WebhookUrlText(), L"http://127.0.0.1:12345/bot/webhook.do");
}

} // namespace

void RunAllEnterpriseBotTests()
{
    TestBotRequests();
    TestBotResponses();
    TestBotRuleText();
    TestBotValidate();
    TestBotScreen();
    TestBotModal();
    TestBotRuleModal();
    TestBotDomainsAndSwitches();
    TestBotWebhookUrl();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
