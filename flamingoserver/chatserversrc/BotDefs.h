/**
 *  机器人的公共定义，BotDefs.h（docs/机器人方案.md）。
 *
 *  机器人分三种：企业机器人（管理员在 flamingoAdmin 配置，应用到本企业全部群或所选的群）、群机器人（群主给本群配置）、
 *  个人机器人（员工给自己配置，只在单聊里替本人回复）。每个机器人对应一行 t_user（f_usertype = user_type_bot），
 *  有一组有序规则：「触发方式 + 附加条件 → 动作」，命中第一条就执行。
 *
 *  本文件只放枚举、上限常量与数据结构，不依赖数据库与网络；读写在 BotManager，判定在 BotRules，执行在 BotEngine。
 *
 *  典型用法：
 *      bot::BotRecord rec;
 *      rec.m_ownerType = bot::bot_owner_enterprise;
 *      rec.m_rules.push_back(rule);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace bot {

//机器人的归属：决定由谁配置、在哪里生效。存于 t_bot.f_owner_type
enum bot_owner_type
{
    //企业机器人：企业管理员在 flamingoAdmin 里配置，f_owner_id 为企业 id，按 f_scope 应用到本企业的群
    bot_owner_enterprise = 1,
    //群机器人：群主给本群配置，f_owner_id 为群号，只在本群生效
    bot_owner_group      = 2,
    //个人机器人：员工给自己配置，f_owner_id 为主人 userid，只在别人发给主人的单聊里代答
    bot_owner_personal   = 3,
};

//企业机器人的应用范围。存于 t_bot.f_scope；群机器人与个人机器人恒为 bot_scope_all，不使用
enum bot_scope
{
    //应用到本企业的全部群（含以后新建的群）
    bot_scope_all      = 0,
    //只应用到 t_bot_group 里登记的群
    bot_scope_selected = 1,
};

//机器人的状态。存于 t_bot.f_status
enum bot_status
{
    //启用：按规则响应
    bot_status_active   = 0,
    //停用：不响应，但仍出现在群的机器人列表里（标为已停用），配置保留
    bot_status_disabled = 1,
    //已删除（软删除）：不响应，也不再出现在任何列表里；对应的 t_user 行保留，历史消息仍能解析出名字
    bot_status_deleted  = 2,
};

//机器人的来源。存于 t_bot.f_source
enum bot_source
{
    //普通机器人：按规则响应
    bot_source_rules         = 0,
    //GitHub 仓库通知的发送者：没有规则，只由仓库通知模块发消息（docs/仓库变动通知方案.md §5.2）
    bot_source_repo_github   = 1,
    //GitLab 仓库通知的发送者：同上
    bot_source_repo_gitlab   = 2,
    //「离开 / 忙碌自动回复」的预置个人机器人：每人至多一个，由 2810 / 2811 维护，不占个人机器人的数量
    bot_source_autoreply     = 3,
};

//规则的触发方式。存于 t_bot_rule.f_trigger_type
enum bot_trigger_type
{
    //群里被 @：content.at.users[].uid 里有机器人的 userid（@所有人不算）
    bot_trigger_at       = 1,
    //关键词：正文命中关键词；群里可要求同时 @ 机器人
    bot_trigger_keyword  = 2,
    //新成员入群：入群完成之后
    bot_trigger_join     = 3,
    //定时：每天、工作日、每周几、每月几号的某个时刻
    bot_trigger_schedule = 4,
    //收到单聊：别人发给个人机器人主人的单聊消息
    bot_trigger_single   = 5,
    //外部系统调用：外部系统经 Webhook 推送内容，由机器人发到群里
    bot_trigger_webhook  = 6,
};

//规则的动作。存于 t_bot_rule.f_action_type
enum bot_action_type
{
    //回复文字或 Markdown
    bot_action_reply    = 1,
    //用本群问答与企业问答作答（只用于群里的 @ 与关键词触发）
    bot_action_qa       = 2,
    //把触发消息发给外部服务，把应答作为回复发出（只用于企业机器人与经企业允许的群机器人）
    bot_action_external = 3,
};

//关键词的匹配方式
enum bot_match_mode
{
    //正文包含关键词
    bot_match_contains = 1,
    //正文与关键词完全相同（去掉 @ 片段与首尾空白之后）
    bot_match_exact    = 2,
    //正文以关键词开头
    bot_match_prefix   = 3,
};

//定时的种类
enum bot_schedule_kind
{
    //每天
    bot_schedule_daily    = 1,
    //工作日（周一至周五）
    bot_schedule_workdays = 2,
    //每周所选的几天
    bot_schedule_weekly   = 3,
    //每月某一号；该月没有这一天（如 31 号）时不发
    bot_schedule_monthly  = 4,
};

//时间段适用的日子
enum bot_days_kind
{
    //每天
    bot_days_every    = 0,
    //工作日（周一至周五）
    bot_days_workdays = 1,
    //周末（周六、周日）
    bot_days_weekend  = 2,
};

//个人机器人的发送者范围
enum bot_sender_kind
{
    //所有人
    bot_sender_all   = 0,
    //指定的人
    bot_sender_users = 1,
    //指定的部门（含下级部门）
    bot_sender_depts = 2,
};

//创建人的类别。存于 t_bot.f_creator_type
enum bot_creator_type
{
    //flamingoAdmin 的管理员（企业机器人）
    bot_creator_admin = 1,
    //IM 用户（群机器人的群主、个人机器人的主人）
    bot_creator_user  = 2,
};

//判定没有产生回复时的原因（「试一试」据此向用户说明；客户端按编号显示本地化文字）
enum bot_decision_reason
{
    //命中了规则（或产生了回复）
    bot_reason_ok             = 0,
    //没有任何规则命中
    bot_reason_no_rule        = 1,
    //命中的规则在冷却中：最近已经代答过这位发送者
    bot_reason_cooldown       = 2,
    //主人最近在这个会话里发过言，说明正在交谈
    bot_reason_owner_speaking = 3,
    //这种消息不触发机器人（系统提示、卡片、机器人或自动回复发出的消息）
    bot_reason_not_triggerable = 4,
};

//「试一试」与真实判定共用的判定结果
enum bot_decision_kind
{
    //没有命中任何规则
    bot_decision_none    = 0,
    //命中规则，回复文字（或问答的答案、候选列表、兜底文字）
    bot_decision_reply   = 1,
    //命中规则，需要呼叫外部服务；回复由外部服务决定
    bot_decision_external = 2,
};

//======== 上限（字数一律按 UTF-8 字符计） ========

//机器人名称最多字数
const int kBotNameMaxChars = 20;
//机器人简介最多字数
const int kBotDescMaxChars = 100;
//头像字形最多字数
const int kBotGlyphMaxChars = 2;
//头像底色字符串最长字节数（"#RRGGBB"）
const int kBotColorMaxBytes = 16;
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
//每个企业最多企业机器人数
const int kEnterpriseBotMax = 50;
//每个群最多群机器人数
const int kGroupBotMax = 5;
//每人最多个人机器人数（不含「自动回复」的预置机器人）
const int kPersonalBotMax = 3;
//每个企业的外部服务域名白名单最多条数
const int kBotDomainMax = 50;
//每个域名最长字节数
const int kBotDomainMaxBytes = 128;
//冷却与「主人正在交谈」的最长分钟数（一天）
const int kBotMinutesMax = 1440;
//个人机器人默认冷却分钟数
const int kBotDefaultCooldownMinutes = 30;
//个人机器人默认「主人最近发过言则不代答」的分钟数
const int kBotDefaultOwnerQuietMinutes = 5;
//自动回复文字最多字数（与客户端设置页的限长一致）
const int kAutoReplyTextMaxChars = 100;
//指定发送者、指定部门、定时发往的群各自最多条数
const int kBotIdListMax = 200;

//======== 频率限制 ========

//每个机器人在每个会话里每分钟最多发几条
const int kBotRatePerConversationPerMinute = 20;
//每个企业的全部机器人合计每分钟最多发几条
const int kBotRatePerEnterprisePerMinute = 600;
//每个机器人的 Webhook 每分钟最多接收几条
const int kBotWebhookPerMinute = 20;

//一天的分钟数
const int kMinutesPerDay = 1440;

//======== 数据结构 ========

//定时设置（bot_trigger_schedule）
struct BotSchedule
{
    int              m_kind;         //定时种类，取值见 bot_schedule_kind
    int              m_minuteOfDay;  //一天中的第几分钟（0～1439），如 9:00 为 540
    std::vector<int> m_weekdays;     //每周的哪几天（1 周一 … 7 周日），只用于 bot_schedule_weekly
    int              m_monthDay;     //每月几号（1～31），只用于 bot_schedule_monthly

    BotSchedule()
        : m_kind(bot_schedule_daily), m_minuteOfDay(0), m_monthDay(1)
    {
    }
};

//时间段（附加条件）
struct BotTimeWindow
{
    bool m_enabled;     //是否限定时间段；为 false 时不检查
    int  m_days;        //适用的日子，取值见 bot_days_kind
    int  m_fromMinute;  //开始时刻（一天中的第几分钟，含）
    int  m_toMinute;    //结束时刻（不含）；小于开始时刻表示跨零点，如 18:00～09:00

    BotTimeWindow()
        : m_enabled(false), m_days(bot_days_every), m_fromMinute(0), m_toMinute(0)
    {
    }
};

//发送者范围（个人机器人的附加条件）
struct BotSenderScope
{
    int                  m_kind;   //取值见 bot_sender_kind
    std::vector<int64_t> m_users;  //指定的人（userid），只用于 bot_sender_users
    std::vector<int64_t> m_depts;  //指定的部门 id（含下级部门），只用于 bot_sender_depts

    BotSenderScope()
        : m_kind(bot_sender_all)
    {
    }
};

//触发方式与附加条件
struct BotTrigger
{
    int                      m_type;              //触发方式，取值见 bot_trigger_type
    std::vector<std::string> m_keywords;          //关键词（bot_trigger_keyword；bot_trigger_single 可选，空表示不限）
    int                      m_match;             //关键词匹配方式，取值见 bot_match_mode
    bool                     m_needAt;            //群里的关键词规则是否要求同时 @ 机器人
    BotSchedule              m_schedule;          //定时设置（bot_trigger_schedule）
    std::vector<int64_t>     m_scheduleGroups;    //企业机器人的定时消息发往哪些群；空表示应用范围内的全部群
    std::vector<int>         m_presence;          //主人在线状态属于其中之一时才代答（取值见 Protocol.h 的 presence_type）；空表示不限
    BotTimeWindow            m_window;            //时间段
    BotSenderScope           m_senders;           //发送者范围
    int                      m_cooldownMinutes;   //同一发送者在多少分钟内只代答一次；0 表示不限
    int                      m_ownerQuietMinutes; //主人最近多少分钟内在这个会话里发过言则不代答；0 表示不检查

    BotTrigger()
        : m_type(bot_trigger_at), m_match(bot_match_contains), m_needAt(true)
        , m_cooldownMinutes(0), m_ownerQuietMinutes(0)
    {
    }
};

//动作
struct BotAction
{
    int         m_type;      //动作，取值见 bot_action_type
    std::string m_text;      //回复文字；问答与外部服务的动作里是兜底文字（没有找到答案、外部服务失败时回复）
    bool        m_markdown;  //回复文字是否按 Markdown 显示（content.format = "markdown"）
    std::string m_url;       //外部服务地址（bot_action_external）
    bool        m_defaultText; //回复文字是客户端按界面语言给出的默认文字（用户没有自己填写）；只用于「自动回复」的预置规则，
                               //客户端据此在设置页显示空框

    BotAction()
        : m_type(bot_action_reply), m_markdown(false), m_defaultText(false)
    {
    }
};

//一条规则
struct BotRule
{
    int64_t     m_id;          //规则 id（t_bot_rule.f_rule_id）；新建时为 0
    int64_t     m_botId;       //所属机器人 id
    int         m_seq;         //在机器人内的顺序，从 0 起
    std::string m_name;        //规则名称，可为空
    bool        m_enabled;     //是否启用
    BotTrigger  m_trigger;     //触发方式与附加条件
    BotAction   m_action;      //动作
    int64_t     m_createTime;  //创建时间（Unix 毫秒）
    int64_t     m_updateTime;  //最后修改时间（Unix 毫秒）

    BotRule()
        : m_id(0), m_botId(0), m_seq(0), m_enabled(true), m_createTime(0), m_updateTime(0)
    {
    }
};

//一个机器人
struct BotRecord
{
    int64_t              m_botId;          //机器人 id（t_bot.f_bot_id）；新建时为 0
    int32_t              m_userId;         //对应的 t_user 行（f_usertype = user_type_bot）
    int64_t              m_enterpriseId;   //所属企业
    int                  m_ownerType;      //归属，取值见 bot_owner_type
    int64_t              m_ownerId;        //企业 id、群号或主人 userid，随 m_ownerType 而定
    std::string          m_name;           //名称
    std::string          m_desc;           //简介
    std::string          m_glyph;          //头像字形，空表示取名称的第一个字
    std::string          m_color;          //头像底色 "#RRGGBB"，空表示客户端默认色
    int                  m_scope;          //企业机器人的应用范围，取值见 bot_scope
    std::vector<int64_t> m_groupIds;       //企业机器人所选的群（m_scope 为 bot_scope_selected 时有效）
    bool                 m_allowGroupOff;  //企业机器人是否允许群主在本群关闭
    int                  m_status;         //状态，取值见 bot_status
    int                  m_source;         //来源，取值见 bot_source
    int                  m_creatorType;    //创建人类别，取值见 bot_creator_type
    int64_t              m_creatorId;      //创建人 id（管理员 id 或 userid）
    std::string          m_creatorName;    //创建人名字（冗余，便于列表直读）
    std::string          m_webhookToken;   //Webhook 令牌（32 位十六进制）；没有 Webhook 规则的机器人也会分配，供以后添加
    std::string          m_secret;         //签名密钥（32 位十六进制）：外部服务请求的签名与 Webhook 的可选签名都用它
    int64_t              m_createTime;     //创建时间（Unix 毫秒）
    int64_t              m_updateTime;     //最后修改时间（Unix 毫秒）
    int64_t              m_lastFireTime;   //最近一次发出消息的时间（Unix 毫秒）；0 表示还没有发过
    std::vector<BotRule> m_rules;          //规则，按 m_seq 排序

    BotRecord()
        : m_botId(0), m_userId(0), m_enterpriseId(0), m_ownerType(bot_owner_enterprise), m_ownerId(0)
        , m_scope(bot_scope_all), m_allowGroupOff(false), m_status(bot_status_active), m_source(bot_source_rules)
        , m_creatorType(bot_creator_admin), m_creatorId(0), m_createTime(0), m_updateTime(0), m_lastFireTime(0)
    {
    }
};

} // namespace bot
