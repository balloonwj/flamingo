/**
 *  机器人管理界面的数据结构与取值（docs/协议设计-v2.md 25.1，与服务端 chatserversrc/BotDefs.h 一一对应）。
 *
 *  纯数据：不依赖界面、网络与 jsoncpp，Core 的编解码、校验、文字与界面字段互转都基于它。文字字段一律按 UTF-8 存放
 *  （与报文相同），界面显示时再转成 UTF-16。服务端改了取值或上限，这里要同步改（单元测试 BOTC 系列核对了几项关键取值）。
 *
 *  典型用法：
 *      flamingobot::BotRecord bot;
 *      bot.m_ownerType = flamingobot::bot_owner_group;
 *      bot.m_rules.push_back(flamingobot::MakeDefaultRule(flamingobot::bot_owner_group));
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingobot {

//机器人的归属（机器人对象的 ownertype）
enum BotOwnerType
{
    //企业机器人：企业管理员在 flamingoAdmin 里配置，本 DLL 只读显示、在本群关闭 / 打开
    bot_owner_enterprise = 1,
    //群机器人：群主给本群配置，在群机器人管理窗里新建、编辑、停用、删除
    bot_owner_group      = 2,
    //个人机器人：员工给自己配置，在「我的机器人」窗口里管理，只在别人发给本人的单聊里代答
    bot_owner_personal   = 3,
};

//机器人的状态（机器人对象的 status）
enum BotStatus
{
    //启用：按规则响应
    bot_status_active   = 0,
    //停用：不响应，配置保留
    bot_status_disabled = 1,
};

//机器人的来源（机器人对象的 source）
enum BotSource
{
    //普通机器人：按规则响应（本 DLL 只管理这一种）
    bot_source_rules       = 0,
    //GitHub 仓库通知的发送者：没有规则，只读
    bot_source_repo_github = 1,
    //GitLab 仓库通知的发送者：没有规则，只读
    bot_source_repo_gitlab = 2,
    //「离开 / 忙碌自动回复」的预置个人机器人：由系统设置的「聊天」页维护，2806 不下发
    bot_source_autoreply   = 3,
};

//规则的触发方式（规则对象的 trigger.type）
enum BotTriggerType
{
    //群里被 @（企业、群机器人）
    bot_trigger_at       = 1,
    //关键词（企业、群机器人；群里可要求同时 @）
    bot_trigger_keyword  = 2,
    //新成员入群（企业、群机器人）
    bot_trigger_join     = 3,
    //定时：每天、工作日、每周几、每月几号的某个时刻（企业、群机器人）
    bot_trigger_schedule = 4,
    //收到单聊：别人发给主人的单聊消息（只用于个人机器人）
    bot_trigger_single   = 5,
    //外部系统推送：外部系统经 Webhook 推送内容（企业机器人；群机器人须企业允许外部服务）
    bot_trigger_webhook  = 6,
};

//规则的动作（规则对象的 action.type）
enum BotActionType
{
    //回复文字（可按 Markdown 显示）
    bot_action_reply    = 1,
    //用本群问答与企业问答作答（只用于群里的被 @ 与关键词）
    bot_action_qa       = 2,
    //呼叫外部服务（只用于企业机器人与经企业允许的群机器人）
    bot_action_external = 3,
};

//关键词的匹配方式（trigger.match）
enum BotMatchMode
{
    //正文包含关键词
    bot_match_contains = 1,
    //正文与关键词完全相同
    bot_match_exact    = 2,
    //正文以关键词开头
    bot_match_prefix   = 3,
};

//定时的种类（trigger.schedule.kind）
enum BotScheduleKind
{
    //每天
    bot_schedule_daily    = 1,
    //工作日（周一至周五）
    bot_schedule_workdays = 2,
    //每周所选的几天（weekdays：1 周一 … 7 周日）
    bot_schedule_weekly   = 3,
    //每月某一号（monthday）；该月没有这一天时不发
    bot_schedule_monthly  = 4,
};

//时间段适用的日子（trigger.window.days）
enum BotDaysKind
{
    //每天
    bot_days_every    = 0,
    //工作日（周一至周五）
    bot_days_workdays = 1,
    //周末（周六、周日）
    bot_days_weekend  = 2,
};

//个人机器人的发送者范围（trigger.senders.kind）
enum BotSenderKind
{
    //所有人
    bot_sender_all   = 0,
    //指定的人（users）
    bot_sender_users = 1,
    //指定的部门（depts，含下级部门）：规则编辑窗经宿主的选择部门对话框选择（2026-10-08 起）
    bot_sender_depts = 2,
};

//主人的在线状态（trigger.presence 的元素与「试一试」的 input.presence，与 Protocol.h 的 presence_type 一致）
enum BotPresence
{
    //离线
    bot_presence_offline = 0,
    //在线
    bot_presence_online  = 1,
    //离开
    bot_presence_away    = 2,
    //忙碌（与请勿打扰同值）
    bot_presence_busy    = 3,
};

//「试一试」的判定结果（2809 应答的 kind）
enum BotDecisionKind
{
    //没有命中任何规则
    bot_decision_none     = 0,
    //命中规则，回复文字（或问答的答案、兜底文字）
    bot_decision_reply    = 1,
    //命中规则，需要呼叫外部服务（只返回地址，不实际调用）
    bot_decision_external = 2,
};

//「试一试」没有产生回复的原因（2809 应答的 reason）
enum BotDecisionReason
{
    //命中了规则
    bot_reason_ok              = 0,
    //没有任何规则命中
    bot_reason_no_rule         = 1,
    //命中的规则在冷却中：最近已经代答过这位发送者
    bot_reason_cooldown        = 2,
    //主人最近在这个会话里发过言，说明正在交谈
    bot_reason_owner_speaking  = 3,
    //这种消息不触发机器人（系统提示、卡片、机器人或自动回复发出的消息）
    bot_reason_not_triggerable = 4,
};

//本地校验的结果（ValidateBot / ValidateRule 的返回值，与服务端 BotRules.h 的 bot_check_* 同义）
enum BotCheckResult
{
    //合法
    bot_check_ok          = 0,
    //字段不合法（名称为空或超长、关键词为空、回复为空、时间格式错误等）
    bot_check_invalid     = 1,
    //数量超出上限（规则、关键词、指定的人）
    bot_check_full        = 2,
    //用了不允许的触发方式或动作（多为企业没有允许外部服务）
    bot_check_not_allowed = 3,
};

//应答里的错误码（Protocol.h 的 error_code，只列本 DLL 需要区分的几个）
enum BotErrorCode
{
    //成功
    bot_error_ok             = 0,
    //服务器内部错误
    bot_error_internal       = 1,
    //尚未登录或登录态已失效
    bot_error_not_login      = 2,
    //报文格式错误
    bot_error_bad_request    = 3,
    //无权限执行该操作
    bot_error_permission     = 6,
    //群不存在
    bot_error_group_not_exist= 301,
    //不是群成员
    bot_error_not_member     = 302,
    //机器人不存在、已删除，或不属于这个群 / 这个人
    bot_error_not_exist      = 1401,
    //没有权限：不是群主、不是机器人的主人，或企业机器人不允许在本群关闭
    bot_error_no_permission  = 1402,
    //数量超出上限
    bot_error_full           = 1403,
    //机器人或规则不合法
    bot_error_invalid        = 1404,
    //本企业关闭了机器人，或关闭了群机器人 / 个人机器人 / 群机器人的外部服务
    bot_error_disabled       = 1405,
    //名称、简介或回复文字含企业敏感词
    bot_error_sensitive      = 1406,
};

//2802～2809 的命令号（Protocol.h 的 cmd_type）
enum BotCmd
{
    //拉取一个机器人及其规则（群机器人只给群主，个人机器人只给主人）
    bot_cmd_get_detail       = 2802,
    //新建或修改群机器人（只有群主）
    bot_cmd_save_group_bot   = 2803,
    //删除群机器人（只有群主）
    bot_cmd_delete_group_bot = 2804,
    //企业机器人在本群关闭 / 打开，群机器人停用 / 启用（只有群主）
    bot_cmd_set_group_bot_off= 2805,
    //拉取本人的个人机器人（含规则）
    bot_cmd_get_my_bots      = 2806,
    //新建或修改个人机器人
    bot_cmd_save_my_bot      = 2807,
    //删除个人机器人
    bot_cmd_delete_my_bot    = 2808,
    //「试一试」：按给定规则判定一段模拟输入，不发消息
    bot_cmd_try              = 2809,
};

//======== 上限（字数一律按 Unicode 字符计，与服务端 BotDefs.h 一致） ========

//机器人名称最多字数
const int kBotNameMaxChars = 20;
//机器人简介最多字数
const int kBotDescMaxChars = 100;
//头像字形最多字数
const int kBotGlyphMaxChars = 2;
//头像底色字符串的长度（"#RRGGBB"）
const int kBotColorLength = 7;
//规则名称最多字数
const int kBotRuleNameMaxChars = 32;
//每个机器人最多规则数
const int kBotRuleMax = 50;
//每条规则最多关键词数
const int kBotKeywordMax = 20;
//每个关键词最多字数
const int kBotKeywordMaxChars = 32;
//回复文字最多字数
const int kBotReplyMaxChars = 2048;
//外部服务地址最长字节数
const int kBotUrlMaxBytes = 512;
//每个群最多群机器人数（2801 的 caps.groupbotmax 没有给出时用它）
const int kGroupBotMax = 5;
//每人最多个人机器人数（2806 的 max 没有给出时用它）
const int kPersonalBotMax = 3;
//冷却与「主人正在交谈」的最长分钟数（一天）
const int kBotMinutesMax = 1440;
//个人机器人默认冷却分钟数
const int kBotDefaultCooldownMinutes = 30;
//个人机器人默认「主人最近发过言则不代答」的分钟数
const int kBotDefaultOwnerQuietMinutes = 5;
//指定发送者、指定部门、定时发往的群各自最多条数
const int kBotIdListMax = 200;
//一天的分钟数
const int kMinutesPerDay = 1440;
//一周的天数（weekdays 取 1～7）
const int kDaysPerWeek = 7;
//每月几号的最大值
const int kMaxMonthDay = 31;

//======== 数据结构 ========

//定时设置（bot_trigger_schedule）
struct BotSchedule
{
    int              m_kind;         //定时种类，取值见 BotScheduleKind
    int              m_minuteOfDay;  //一天中的第几分钟（0～1439）；报文里的时刻解析不出时为 -1
    std::vector<int> m_weekdays;     //每周的哪几天（1 周一 … 7 周日），只用于 bot_schedule_weekly
    int              m_monthDay;     //每月几号（1～31），只用于 bot_schedule_monthly

    BotSchedule()
        : m_kind(bot_schedule_daily)
        , m_minuteOfDay(0)
        , m_monthDay(1)
    {
    }
};

//时间段（附加条件）
struct BotTimeWindow
{
    bool m_enabled;     //是否限定时间段；为 false 时不检查
    int  m_days;        //适用的日子，取值见 BotDaysKind
    int  m_fromMinute;  //开始时刻（一天中的第几分钟，含）；解析不出时为 -1
    int  m_toMinute;    //结束时刻（不含）；小于开始时刻表示跨零点，等于开始时刻表示全天；解析不出时为 -1

    BotTimeWindow()
        : m_enabled(false)
        , m_days(bot_days_every)
        , m_fromMinute(0)
        , m_toMinute(0)
    {
    }
};

//发送者范围（个人机器人的附加条件）
struct BotSenderScope
{
    int                  m_kind;   //取值见 BotSenderKind
    std::vector<int64_t> m_users;  //指定的人（userid），只用于 bot_sender_users
    std::vector<int64_t> m_depts;  //指定的部门 id（含下级部门），只用于 bot_sender_depts

    BotSenderScope()
        : m_kind(bot_sender_all)
    {
    }
};

//触发方式与附加条件（规则对象的 trigger）
struct BotTrigger
{
    int                      m_type;              //触发方式，取值见 BotTriggerType
    std::vector<std::string> m_keywords;          //关键词（UTF-8）；关键词规则必填，收到单聊时可选（空表示不限）
    int                      m_match;             //关键词匹配方式，取值见 BotMatchMode
    bool                     m_needAt;            //群里的关键词规则是否要求同时 @ 机器人
    BotSchedule              m_schedule;          //定时设置
    std::vector<int64_t>     m_scheduleGroups;    //企业机器人的定时消息发往哪些群；本 DLL 不编辑，只原样保留
    std::vector<int>         m_presence;          //主人在线状态属于其中之一时才代答（取值见 BotPresence）；空表示不限
    BotTimeWindow            m_window;            //时间段
    BotSenderScope           m_senders;           //发送者范围
    int                      m_cooldownMinutes;   //同一发送者在多少分钟内只代答一次；0 表示不限
    int                      m_ownerQuietMinutes; //主人最近多少分钟内在这个会话里发过言则不代答；0 表示不检查

    BotTrigger()
        : m_type(bot_trigger_at)
        , m_match(bot_match_contains)
        , m_needAt(true)
        , m_cooldownMinutes(0)
        , m_ownerQuietMinutes(0)
    {
    }
};

//动作（规则对象的 action）
struct BotAction
{
    int         m_type;        //动作，取值见 BotActionType
    std::string m_text;        //回复文字（UTF-8）；问答与外部服务的动作里是兜底文字
    bool        m_markdown;    //回复文字是否按 Markdown 显示
    std::string m_url;         //外部服务地址（bot_action_external）
    bool        m_defaultText; //回复文字是客户端给的默认文字（只用于自动回复预置规则，本 DLL 原样保留）

    BotAction()
        : m_type(bot_action_reply)
        , m_markdown(false)
        , m_defaultText(false)
    {
    }
};

//一条规则（规则对象）
struct BotRule
{
    int64_t     m_id;       //规则 id；新建时为 0
    std::string m_name;     //规则名称（UTF-8），可为空
    bool        m_enabled;  //是否启用
    BotTrigger  m_trigger;  //触发方式与附加条件
    BotAction   m_action;   //动作

    BotRule()
        : m_id(0)
        , m_enabled(true)
    {
    }
};

//一个机器人（机器人对象）
struct BotRecord
{
    int64_t              m_botId;          //机器人 id；新建时为 0
    int64_t              m_userId;         //在群里发消息用的账号 userid；个人机器人为 0
    int64_t              m_enterpriseId;   //所属企业
    int                  m_ownerType;      //归属，取值见 BotOwnerType
    int64_t              m_ownerId;        //企业 id、群号或主人 userid
    std::string          m_name;           //名称（UTF-8）
    std::string          m_desc;           //简介（UTF-8）
    std::string          m_glyph;          //头像字形（UTF-8），空表示取名称的第一个字
    std::string          m_color;          //头像底色 "#RRGGBB"，空表示默认色
    int                  m_scope;          //企业机器人的应用范围（0 全部群、1 所选的群）；本 DLL 不编辑
    std::vector<int64_t> m_groupIds;       //企业机器人所选的群；本 DLL 不编辑
    bool                 m_allowGroupOff;  //企业机器人是否允许群主在本群关闭
    int                  m_status;         //状态，取值见 BotStatus
    int                  m_source;         //来源，取值见 BotSource
    std::string          m_creatorName;    //创建人名字
    int64_t              m_createTime;     //创建时间（Unix 毫秒）
    int64_t              m_updateTime;     //最后修改时间（Unix 毫秒）
    int64_t              m_lastFire;       //最近一次发出消息的时间（Unix 毫秒）；0 表示还没有发过
    int                  m_ruleCount;      //规则条数（不带规则的应答里也有）
    bool                 m_hasRules;       //应答里带了规则（m_rules 有效）
    std::vector<BotRule> m_rules;          //规则，按判定顺序
    std::string          m_webhookToken;   //Webhook 令牌（只给有编辑权限的人）；空表示应答里没有
    std::string          m_secret;         //签名密钥（只给有编辑权限的人）；空表示应答里没有
    std::string          m_webhookUrl;     //Webhook 对外完整地址（服务端配置了 botwebhookbaseurl 时随令牌下发）；空表示没有

    BotRecord()
        : m_botId(0)
        , m_userId(0)
        , m_enterpriseId(0)
        , m_ownerType(bot_owner_group)
        , m_ownerId(0)
        , m_scope(0)
        , m_allowGroupOff(false)
        , m_status(bot_status_active)
        , m_source(bot_source_rules)
        , m_createTime(0)
        , m_updateTime(0)
        , m_lastFire(0)
        , m_ruleCount(0)
        , m_hasRules(false)
    {
    }
};

//「试一试」的模拟输入（2809 请求的 input）
struct BotTryInput
{
    std::string m_text;      //模拟的正文（UTF-8）
    bool        m_at;        //群里：是否 @ 了机器人
    int         m_presence;  //个人机器人：模拟的主人在线状态，取值见 BotPresence
    int64_t     m_senderId;  //个人机器人：模拟的发送者；0 表示本人以外的任意同事
    int64_t     m_timeMs;    //模拟的时间（Unix 毫秒）；0 表示现在

    BotTryInput()
        : m_at(true)
        , m_presence(bot_presence_offline)
        , m_senderId(0)
        , m_timeMs(0)
    {
    }
};

//「试一试」的结果（2809 应答）
struct BotTryResult
{
    bool        m_matched;   //是否命中
    int         m_kind;      //取值见 BotDecisionKind
    int64_t     m_ruleId;    //命中的规则 id（未保存的规则为 0）
    std::string m_ruleName;  //命中的规则名称（UTF-8）
    std::string m_text;      //会回复的文字（UTF-8，变量已替换）
    bool        m_markdown;  //回复按 Markdown 显示
    std::string m_url;       //需要呼叫的外部服务地址
    int         m_reason;    //没有回复的原因，取值见 BotDecisionReason

    BotTryResult()
        : m_matched(false)
        , m_kind(bot_decision_none)
        , m_ruleId(0)
        , m_markdown(false)
        , m_reason(bot_reason_ok)
    {
    }
};

//企业开关（登录 features 与 1013 推送），决定界面上哪些操作可用
struct BotFeatures
{
    bool m_bot;            //机器人总开关
    bool m_groupBot;       //允许群主添加群机器人
    bool m_personal;       //允许员工配置个人机器人
    bool m_groupExternal;  //允许群主给群机器人配置外部服务

    BotFeatures()
        : m_bot(true)
        , m_groupBot(true)
        , m_personal(true)
        , m_groupExternal(false)
    {
    }
};

//某个群的机器人能力说明（2801 的 caps，经宿主取来）
struct GroupBotCaps
{
    bool m_isOwner;          //本人是群主
    bool m_groupBotAllowed;  //企业允许群主添加群机器人
    bool m_externalAllowed;  //企业允许群主给群机器人配置外部服务
    int  m_groupBotMax;      //每群最多群机器人数

    GroupBotCaps()
        : m_isOwner(false)
        , m_groupBotAllowed(false)
        , m_externalAllowed(false)
        , m_groupBotMax(kGroupBotMax)
    {
    }
};

//某个群可用的一个机器人（2801 bots 数组的一项，经宿主取来）
struct GroupBotItem
{
    int64_t      m_botId;       //机器人 ID
    int64_t      m_userId;      //在群里发消息用的账号
    int          m_ownerType;   //归属：1 企业机器人、2 群机器人
    bool         m_isGroupBot;  //来源是本群
    std::wstring m_name;        //名称
    std::wstring m_desc;        //简介
    std::wstring m_glyph;       //头像字形
    std::wstring m_color;       //头像底色 "#RRGGBB"
    int          m_status;      //0 启用、1 停用
    bool         m_off;         //企业机器人在本群被关闭
    bool         m_active;      //在本群生效
    bool         m_canOff;      //群主能否在本群关闭它（群机器人恒为 true）
    bool         m_atable;      //能否被 @

    GroupBotItem()
        : m_botId(0)
        , m_userId(0)
        , m_ownerType(bot_owner_enterprise)
        , m_isGroupBot(false)
        , m_status(bot_status_active)
        , m_off(false)
        , m_active(false)
        , m_canOff(false)
        , m_atable(false)
    {
    }
};

//某个群的机器人列表（宿主缓存的 2801 结果）
struct GroupBotList
{
    GroupBotCaps              m_caps;  //能力说明
    std::vector<GroupBotItem> m_bots;  //本群可用的机器人（含停用的、在本群关闭了的），按服务端给的顺序
};

} // namespace flamingobot
