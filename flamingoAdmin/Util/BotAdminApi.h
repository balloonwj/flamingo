/**
 *  flamingoAdmin —— 企业机器人接口（/admin/bot/*.do，docs/Admin控制台接口文档.md 模块 14，docs/机器人方案.md 第 7 节）。
 *
 *  机器人对象与规则对象的字段对齐 docs/协议设计-v2.md 25.1（服务端 bot::BotToJson / bot::RuleToJson）；本文件负责
 *  十个接口的请求构造、应答解析，以及错误码 170～176 的中文提示。规则与界面字段的互相转换、提交前的本地校验在
 *  Util/BotRuleText 里。与 AdminApi 分开放：AdminApi.cpp 很大、改动频繁，机器人自成一个文件更容易维护。
 *  不依赖窗口与网络线程，单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法：
 *      HttpRequest req = botapi::MakeBotListRequest(server, session.Token(), enterpriseId);
 *      req.m_tag = kApiTagBotList;
 *      //应答到达后
 *      botapi::BotListResult r = botapi::ParseBotListResponse(http);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <vector>

#include "Util/AppConfig.h"     // ServerConfig
#include "Util/HttpWorker.h"    // HttpRequest
#include "Util/HttpClient.h"    // HttpResult

namespace flamingoadmin { namespace botapi {

// ---- 上限（与服务端 chatserversrc/BotDefs.h 一致）----
const int kBotNameMaxChars     = 20;     // 机器人名称最多多少个字（kBotNameMaxChars）
const int kBotDescMaxChars     = 100;    // 简介最多多少个字（kBotDescMaxChars）
const int kBotGlyphMaxChars    = 2;      // 头像文字最多多少个字（kBotGlyphMaxChars）
const int kBotRuleNameMaxChars = 32;     // 规则名称最多多少个字（kBotRuleNameMaxChars）
const int kBotRuleMax          = 50;     // 一个机器人最多多少条规则（kBotRuleMax）
const int kBotKeywordMax       = 20;     // 一条规则最多多少个关键词（kBotKeywordMax）
const int kBotKeywordMaxChars  = 32;     // 每个关键词最多多少个字（kBotKeywordMaxChars）
const int kBotReplyMaxChars    = 2048;   // 回复文字最多多少个字（kBotReplyMaxChars）
const int kBotUrlMaxBytes      = 512;    // 外部服务地址最多多少字节（kBotUrlMaxBytes）
const int kBotDomainMax        = 50;     // 域名白名单最多多少条（kBotDomainMax）
const int kBotDomainMaxBytes   = 128;    // 每个域名最多多少字节（kBotDomainMaxBytes）
const int kBotIdListMax        = 200;    // 所选的群、定时消息的目标群最多多少个（kBotIdListMax）
const int kEnterpriseBotMax    = 50;     // 一个企业最多多少个企业机器人（kEnterpriseBotMax）
const int kDaysPerWeek         = 7;      // 一周的天数：星期取值 1（周一）～ 7（周日）
const int kMaxMonthDay         = 31;     // 每月定时的「几号」最大取值

// 应用范围（机器人对象的 scope）
enum BotScope
{
    // 应用到本企业的全部群（groupids 忽略）
    kBotScopeAll = 0,
    // 只应用到 groupids 列出的群
    kBotScopeSelected = 1
};

// 机器人状态（机器人对象的 status）
enum BotStatus
{
    // 启用：按规则响应
    kBotStatusActive = 0,
    // 停用：不响应任何消息，群里也不再列出
    kBotStatusDisabled = 1
};

// 机器人来源（机器人对象的 source）
enum BotSource
{
    // 按规则响应：管理员在本页新建的普通企业机器人
    kBotSourceRules = 0,
    // GitHub 仓库通知：由「仓库通知」页管理，本页只读
    kBotSourceGithub = 1,
    // GitLab 仓库通知：由「仓库通知」页管理，本页只读
    kBotSourceGitlab = 2,
    // 自动回复预置：个人机器人里「离开 / 忙碌自动回复」的预置机器人，企业机器人列表里不会出现
    kBotSourceAutoReply = 3
};

// 触发方式（规则对象的 trigger.type）
enum BotTriggerType
{
    // 群里有人 @ 机器人
    kTriggerAt = 1,
    // 群消息正文命中关键词
    kTriggerKeyword = 2,
    // 新成员入群
    kTriggerJoin = 3,
    // 定时：每天 / 工作日 / 每周所选的几天 / 每月几号的某个时刻
    kTriggerSchedule = 4,
    // 收到单聊：只用于个人机器人，企业机器人不能用
    kTriggerSingle = 5,
    // 外部系统推送（Webhook）：外部系统按机器人的 Webhook 地址推来消息
    kTriggerWebhook = 6
};

// 关键词匹配方式（规则对象的 trigger.match）
enum BotMatchMode
{
    // 正文包含关键词
    kMatchContains = 1,
    // 正文与关键词完全相同
    kMatchExact = 2,
    // 正文以关键词开头
    kMatchPrefix = 3
};

// 定时的种类（规则对象的 trigger.schedule.kind）
enum BotScheduleKind
{
    // 每天
    kScheduleDaily = 1,
    // 工作日（周一至周五）
    kScheduleWorkdays = 2,
    // 每周所选的几天（weekdays）
    kScheduleWeekly = 3,
    // 每月几号（monthday）
    kScheduleMonthly = 4
};

// 时间段的日子（规则对象的 trigger.window.days）
enum BotWindowDays
{
    // 每天
    kWindowEveryDay = 0,
    // 工作日（周一至周五）
    kWindowWorkdays = 1,
    // 周末（周六、周日）
    kWindowWeekend = 2
};

// 动作（规则对象的 action.type）
enum BotActionType
{
    // 回复文字（action.text；markdown 为 1 时按 Markdown 显示）
    kActionReply = 1,
    // 用问答作答：取本群问答与企业问答，action.text 为没有答案时的兜底文字
    kActionQa = 2,
    // 呼叫外部服务：把触发消息 POST 给 action.url，action.text 为失败时的兜底文字
    kActionExternal = 3
};

// 「试一试」的判定结果（try.do 应答的 kind）
enum BotTryKind
{
    // 没有命中，不回复
    kTryKindNone = 0,
    // 命中，回复 text
    kTryKindReply = 1,
    // 命中，呼叫外部服务 url（试一试只返回地址，不实际调用）
    kTryKindExternal = 2
};

// 「试一试」没有回复的原因（try.do 应答的 reason）
enum BotTryReason
{
    // 命中了规则
    kTryReasonMatched = 0,
    // 没有规则命中
    kTryReasonNoRule = 1,
    // 冷却中（同一发送者在冷却时间内已经回复过）
    kTryReasonCooldown = 2,
    // 主人正在交谈（个人机器人用，企业机器人不会出现）
    kTryReasonOwnerTalking = 3,
    // 这种消息不触发机器人
    kTryReasonNotTrigger = 4
};

// 定时（规则对象的 trigger.schedule）
struct BotSchedule
{
    int              m_kind;       // 定时的种类，取值见 BotScheduleKind
    CString          m_time;       // 时刻，"HH:MM"（服务器本地时间）
    std::vector<int> m_weekdays;   // 每周所选的几天，1 周一 … 7 周日（m_kind 为 kScheduleWeekly 时有效）
    int              m_monthDay;   // 每月几号，1～31（m_kind 为 kScheduleMonthly 时有效）

    BotSchedule()
        : m_kind(kScheduleDaily), m_time(_T("09:00")), m_monthDay(1)
    {
    }
};

// 时间段（规则对象的 trigger.window）：只在这个时间段内响应
struct BotWindow
{
    bool    m_on;     // 是否启用时间段；false 表示任何时间都响应
    int     m_days;   // 哪些日子，取值见 BotWindowDays
    CString m_from;   // 起始时刻 "HH:MM"
    CString m_to;     // 结束时刻 "HH:MM"；小于 m_from 表示跨零点，等于 m_from 表示全天

    BotWindow()
        : m_on(false), m_days(kWindowEveryDay), m_from(_T("09:00")), m_to(_T("18:00"))
    {
    }
};

// 发送者范围（规则对象的 trigger.senders，只用于个人机器人；企业机器人原样保留、不在界面上编辑）
struct BotSenders
{
    int                    m_kind;    // 0 所有人、1 指定的人、2 指定的部门
    std::vector<long long> m_users;   // 指定的人（userid）
    std::vector<long long> m_depts;   // 指定的部门（部门ID）

    BotSenders()
        : m_kind(0)
    {
    }
};

// 触发条件（规则对象的 trigger）
struct BotTrigger
{
    int                    m_type;         // 触发方式，取值见 BotTriggerType
    std::vector<CString>   m_keywords;     // 关键词（m_type 为 kTriggerKeyword 时有效）
    int                    m_match;        // 关键词匹配方式，取值见 BotMatchMode
    bool                   m_needAt;       // 关键词规则是否要求同时 @ 机器人
    BotSchedule            m_schedule;     // 定时（m_type 为 kTriggerSchedule 时有效）
    std::vector<long long> m_groupIds;     // 定时消息发往哪些群；空表示应用范围内的全部群
    std::vector<int>       m_presence;     // 个人机器人用：主人在线状态；企业机器人原样保留
    BotWindow              m_window;       // 时间段
    BotSenders             m_senders;      // 个人机器人用：发送者范围；企业机器人原样保留
    int                    m_cooldown;     // 个人机器人用：同一发送者多少分钟内只代答一次；企业机器人原样保留
    int                    m_ownerQuiet;   // 个人机器人用：主人最近多少分钟内发过言则不代答；企业机器人原样保留

    BotTrigger()
        : m_type(kTriggerAt), m_match(kMatchContains), m_needAt(true), m_cooldown(0), m_ownerQuiet(0)
    {
    }
};

// 动作（规则对象的 action）
struct BotAction
{
    int     m_type;          // 动作，取值见 BotActionType
    CString m_text;          // 回复文字；用问答作答、呼叫外部服务时为兜底文字
    bool    m_markdown;      // 回复文字是否按 Markdown 显示
    CString m_url;           // 外部服务地址（m_type 为 kActionExternal 时有效）
    bool    m_defaultText;   // 只用于自动回复预置规则；企业机器人原样保留

    BotAction()
        : m_type(kActionReply), m_markdown(false), m_defaultText(false)
    {
    }
};

// 一条规则（规则对象）
struct BotRule
{
    long long  m_id;        // 规则ID；0 表示新加的规则
    CString    m_name;      // 规则名称，可为空
    bool       m_enabled;   // 是否启用
    BotTrigger m_trigger;   // 触发条件
    BotAction  m_action;    // 动作（外部系统推送的规则没有动作）

    BotRule()
        : m_id(0), m_enabled(true)
    {
    }
};

// 一个企业机器人（机器人对象，加上 list.do / detail.do 附带的 lastfire 与 webhookpath）
struct BotItem
{
    long long              m_botId;          // 机器人ID；0 表示新建
    long long              m_userId;         // 机器人在群里发消息的账号
    int                    m_ownerType;      // 1 企业机器人（本页只管这一种）
    CString                m_name;           // 名称
    CString                m_desc;           // 简介
    CString                m_glyph;          // 头像文字（最多 2 个字）
    CString                m_color;          // 头像底色 "#RRGGBB"，空表示默认色
    int                    m_scope;          // 应用范围，取值见 BotScope
    std::vector<long long> m_groupIds;       // 所选的群（m_scope 为 kBotScopeSelected 时有效）
    bool                   m_allowGroupOff;  // 是否允许群主在本群关闭
    int                    m_status;         // 状态，取值见 BotStatus
    int                    m_source;         // 来源，取值见 BotSource
    CString                m_creatorName;    // 创建它的管理员
    long long              m_createTime;     // 创建时间，Unix 毫秒
    long long              m_updateTime;     // 最后修改时间，Unix 毫秒
    long long              m_lastFire;       // 最近一次发出消息的时间，Unix 毫秒；0 表示还没发过
    int                    m_ruleCount;      // 规则条数（列表应答不带规则，只带条数）
    bool                   m_hasRules;       // 应答是否带了规则（detail.do 带，list.do 不带）
    std::vector<BotRule>   m_rules;          // 规则，按判定顺序排列（m_hasRules 为 true 时有效）
    CString                m_webhookToken;   // Webhook 令牌（detail.do 才有）
    CString                m_secret;         // 签名密钥（detail.do 才有）
    CString                m_webhookPath;    // Webhook 路径，如 "/bot/webhook.do"
    CString                m_webhookUrl;     // Webhook 对外完整地址（服务端配置了 botwebhookbaseurl 时随列表与详情下发）；
                                             // 空串表示服务端没有配置，界面按连接地址拼出

    BotItem()
        : m_botId(0), m_userId(0), m_ownerType(1), m_scope(kBotScopeAll), m_allowGroupOff(false)
        , m_status(kBotStatusActive), m_source(kBotSourceRules), m_createTime(0), m_updateTime(0)
        , m_lastFire(0), m_ruleCount(0), m_hasRules(false)
    {
    }
};

// 本企业的一个群（选择应用范围、定时目标群、「试一试」的群用）
struct BotGroup
{
    long long m_id;     // 群ID
    CString   m_name;   // 群名

    BotGroup()
        : m_id(0)
    {
    }
};

// 「试一试」的输入（try.do 的 input）
struct BotTryInput
{
    CString   m_text;      // 模拟的消息正文
    bool      m_at;        // 这条消息是否 @ 了机器人
    long long m_groupId;   // 在哪个群里试（用来取该群的问答）；0 表示不指定
    long long m_timeMs;    // 模拟的发送时间，Unix 毫秒；0 表示现在（请求里不带）

    BotTryInput()
        : m_at(true), m_groupId(0), m_timeMs(0)
    {
    }
};

// 应答的公共部分
struct BotResultBase
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码；0 成功
    CString m_message;       // 失败时给用户看的中文提示
    CString m_serverMsg;     // 服务端应答的 msg 原文（英文，save.do 失败时写明是哪条规则的什么问题）

    BotResultBase()
        : m_transportOk(false), m_code(0)
    {
    }

    // 往返成功且业务成功
    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// list.do 的应答
struct BotListResult : public BotResultBase
{
    std::vector<BotItem> m_bots;   // 企业机器人，按机器人ID升序；不带规则与密钥
};

// detail.do 的应答
struct BotDetailResult : public BotResultBase
{
    BotItem m_bot;   // 机器人，带规则、令牌与密钥
};

// save.do 的应答
struct BotSaveResult : public BotResultBase
{
    long long m_botId;   // 保存成功时的机器人ID（新建时为新ID）

    BotSaveResult()
        : m_botId(0)
    {
    }
};

// groups.do 的应答
struct BotGroupsResult : public BotResultBase
{
    std::vector<BotGroup> m_groups;   // 本企业的群，按群ID升序
};

// try.do 的应答
struct BotTryResult : public BotResultBase
{
    bool      m_matched;    // 是否命中
    int       m_kind;       // 判定结果，取值见 BotTryKind
    long long m_ruleId;     // 命中的规则ID
    CString   m_ruleName;   // 命中的规则名称
    CString   m_text;       // 会回复的文字
    bool      m_markdown;   // 回复是否按 Markdown 显示
    CString   m_url;        // 会呼叫的外部服务地址（m_kind 为 kTryKindExternal 时）
    int       m_reason;     // 没有回复的原因，取值见 BotTryReason

    BotTryResult()
        : m_matched(false), m_kind(kTryKindNone), m_ruleId(0), m_markdown(false), m_reason(kTryReasonMatched)
    {
    }
};

// resettoken.do 的应答
struct BotResetTokenResult : public BotResultBase
{
    CString m_webhookToken;   // 新的 Webhook 令牌
    CString m_secret;         // 新的签名密钥
};

// domains.do 的应答
struct BotDomainsResult : public BotResultBase
{
    std::vector<CString> m_domains;   // 外部服务的域名白名单
};

/**
 *  构造拉取企业机器人列表的请求（bot/list.do）。
 *    server：服务器配置（m_host 可为空，由请求出口补上）；token：会话令牌；
 *    enterpriseId：企业ID；企业管理员传 0（服务端按本企业处理），平台超管必须传选中的企业。
 */
HttpRequest MakeBotListRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

/**
 *  构造拉取企业机器人详情的请求（bot/detail.do）。参数同 MakeBotListRequest；botId：机器人ID。
 */
HttpRequest MakeBotDetailRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                 long long botId);

/**
 *  构造新建或修改企业机器人的请求（bot/save.do）。bot.m_botId 为 0 时新建（botid 写 "0"），否则修改；
 *  规则整份提交。应用范围为全部群时 groupids 为空数组。
 */
HttpRequest MakeBotSaveRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                               const BotItem& bot);

/**
 *  构造删除企业机器人的请求（bot/delete.do）。
 */
HttpRequest MakeBotDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                 long long botId);

/**
 *  构造停用 / 启用的请求（bot/setstatus.do）。status：kBotStatusActive 启用、kBotStatusDisabled 停用。
 */
HttpRequest MakeBotSetStatusRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                    long long botId, int status);

/**
 *  构造拉取本企业全部群的请求（bot/groups.do）。参数同 MakeBotListRequest。
 */
HttpRequest MakeBotGroupsRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

/**
 *  构造「试一试」的请求（bot/try.do）：带上编辑窗里尚未保存的机器人（含全部规则），服务端按它判定，不发任何消息。
 *    draft：编辑窗里当前的机器人；input：模拟的消息，m_groupId 为 0 时不带 groupid，m_timeMs 为 0 时不带 time。
 */
HttpRequest MakeBotTryRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                              const BotItem& draft, const BotTryInput& input);

/**
 *  构造重新生成 Webhook 令牌与签名密钥的请求（bot/resettoken.do）。旧令牌立即失效。
 */
HttpRequest MakeBotResetTokenRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     long long botId);

/**
 *  构造拉取外部服务域名白名单的请求（bot/domains.do）。参数同 MakeBotListRequest。
 */
HttpRequest MakeBotDomainsRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

/**
 *  构造修改外部服务域名白名单的请求（bot/setdomains.do），整份覆盖。
 *    domains：域名列表（调用方已去掉首尾空白；大小写与重复由服务端处理）。
 */
HttpRequest MakeBotSetDomainsRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const std::vector<CString>& domains);

// 解析 list.do 的应答
BotListResult ParseBotListResponse(const HttpResult& http);

// 解析 detail.do 的应答
BotDetailResult ParseBotDetailResponse(const HttpResult& http);

// 解析 save.do 的应答；失败时 m_message 按 BotSaveErrorText 写明是哪条规则的什么问题
BotSaveResult ParseBotSaveResponse(const HttpResult& http);

// 解析 delete.do、setstatus.do、setdomains.do 等只关心成败的应答
BotResultBase ParseBotWriteResponse(const HttpResult& http);

// 解析 groups.do 的应答
BotGroupsResult ParseBotGroupsResponse(const HttpResult& http);

// 解析 try.do 的应答
BotTryResult ParseBotTryResponse(const HttpResult& http);

// 解析 resettoken.do 的应答
BotResetTokenResult ParseBotResetTokenResponse(const HttpResult& http);

// 解析 domains.do 的应答
BotDomainsResult ParseBotDomainsResponse(const HttpResult& http);

/**
 *  业务错误码对应的中文提示：170～176 为本模块专属，其余给通用说法。
 */
CString BotErrorText(int code);

/**
 *  save.do 失败时的中文提示：服务端的 msg 形如 "rule 2: empty reply"（规则下标从 0 起）或 "bad name"，
 *  认得的原因翻成中文并写明是第几条规则（从 1 起数），认不得时退回 BotErrorText(code)。
 *    code：业务错误码；serverMsg：服务端应答的 msg 原文。
 */
CString BotSaveErrorText(int code, const CString& serverMsg);

/**
 *  机器人的 Webhook 完整地址：服务端下发了对外完整地址（webhookurl）时直接用它；否则按控制台连接服务器所用的协议、
 *  主机与端口拼上 webhookpath（Webhook 与管理接口同在 chatserver 的 HTTP 端口上，docs/协议设计-v2.md 25.4）。
 *    server：   控制台当前的服务器配置；
 *    path：     webhookpath，空串时返回空串（不是可编辑的机器人）；
 *    serverUrl：服务端下发的 webhookurl；空串表示服务端没有配置对外地址。
 */
CString WebhookUrl(const ServerConfig& server, const CString& path, const CString& serverUrl);

}} // namespace flamingoadmin::botapi
