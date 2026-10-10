/**
 *  机器人的纯逻辑，BotRules.h（docs/机器人方案.md §3.5、§5.4、§6.2）。
 *
 *  包括：文字归一化与 UTF-8 字数、消息 content 的解析（正文、@、机器人标记）、关键词匹配、问答作答的打分、
 *  定时与时间段、在线状态合并、规则判定、机器人消息 content 的构造、规则与机器人的 JSON 编解码与校验。
 *  只依赖 jsoncpp，不碰数据库与网络，便于单元测试（tests/test_bot.cpp）。
 *
 *  典型用法：
 *      bot::MessageView view;
 *      bot::ParseMessageContent(contentJson, view);
 *      bot::BotDecision d = bot::EvaluateGroupMessage(botRecord, view, clock, &qaItems, senderName);
 *      if (d.m_kind == bot::bot_decision_reply)
 *          content = bot::BuildGroupReplyContent(botRecord.m_botId, d.m_text, d.m_markdown, senderId, senderName,
 *                                                botRecord.m_name);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "BotDefs.h"
#include "../jsoncpp1.9.0/json.h"

namespace bot {

//======== 文字 ========

/**
 *  UTF-8 字符数（非法字节按一个字符计）。
 *  @param s UTF-8 文字
 *  @return 字符数
 */
int Utf8Length(const std::string& s);

/**
 *  归一化：去掉首尾空白（含全角空格），全角 ASCII 字符转半角，ASCII 大写转小写。比较关键词、问题前都先归一化。
 *  @param s UTF-8 文字
 *  @return 归一化后的文字
 */
std::string NormalizeText(const std::string& s);

/**
 *  把一个 HH:MM 形式的时刻换算成一天中的第几分钟。
 *  @param hhmm   如 "09:00"、"18:30"；小时 0～23，分钟 0～59
 *  @param minute [出参] 一天中的第几分钟
 *  @return 格式正确返回 true
 */
bool ParseHhMm(const std::string& hhmm, int& minute);

/**
 *  把一天中的第几分钟格式化成 HH:MM。
 *  @param minute 0～1439
 *  @return 如 "09:00"
 */
std::string FormatHhMm(int minute);

//======== 消息内容 ========

//一条消息 content 里机器人关心的部分
struct MessageView
{
    std::string          m_text;           //content.text；没有这个字段时为空
    std::string          m_textWithoutAt;  //按 content.at 的片段剪掉 @ 之后的正文（未归一化）
    std::vector<int64_t> m_atUsers;        //content.at.users[].uid
    bool                 m_atAll;          //content.at.all 为 1
    bool                 m_botMarked;      //content 里有 bot 字段（机器人发出的消息）
    bool                 m_autoReply;      //content.autoreply 为 true（自动回复发出的消息）

    MessageView()
        : m_atAll(false), m_botMarked(false), m_autoReply(false)
    {
    }
};

/**
 *  这条群消息是否 @ 了本群别的机器人、却没有 @ 自己。是的话本机器人这次不判定：消息 @ 了某个机器人时只让被 @ 的
 *  机器人回答，其他机器人（包括不要求 @ 的关键词规则）都不响应（2026-10-08 用户确认，docs/机器人方案.md §11 Q9）。
 *    view：           消息内容（ParseMessageContent 的结果）。
 *    selfUserId：     本机器人的账号 userid。
 *    groupBotUserIds：本群可用的全部机器人的账号 userid（含本机器人）。
 *  @return 消息 @ 了列表里别的机器人、且没有 @ 本机器人时返回 true；没有 @ 任何机器人时返回 false
 */
bool MentionsOtherBot(const MessageView& view, int64_t selfUserId, const std::vector<int64_t>& groupBotUserIds);

/**
 *  解析一条消息的 content（客户端发来的原始 content 对象，不是 buildStoredContent 的外壳）。
 *  @param contentJson content 对象的紧凑 JSON 文本
 *  @param out         [出参] 解析结果
 *  @return content 是 JSON 对象返回 true
 */
bool ParseMessageContent(const std::string& contentJson, MessageView& out);

/**
 *  删除客户端发来的 content 里的 bot 字段，保证只有服务端能产生带机器人标记的消息（§5.6）。
 *  @param contentJson [入出参] content 对象的紧凑 JSON 文本；有 bot 字段时改写为删掉该字段后的文本
 *  @return 删掉了 bot 字段返回 true；没有该字段或不是 JSON 对象时返回 false，文本不变
 */
bool StripBotMarker(std::string& contentJson);

/**
 *  这种消息类型能否触发机器人：服务端生成的卡片、系统提示、通话记录等不触发。
 *  @param msgtype 消息内容类型（Protocol.h 的 message_type）
 *  @return 能触发返回 true
 */
bool IsTriggerableMsgType(int msgtype);

//======== 关键词与问答 ========

/**
 *  一段归一化之后的正文是否命中某个关键词（关键词在函数内归一化）。
 *  @param normalizedText 已归一化的正文
 *  @param keyword        关键词（未归一化）；归一化后为空时不命中
 *  @param mode           匹配方式，取值见 bot_match_mode
 *  @return 命中返回 true
 */
bool MatchKeyword(const std::string& normalizedText, const std::string& keyword, int mode);

/**
 *  正文是否命中任意一个关键词。
 *  @param text     正文（未归一化）
 *  @param keywords 关键词列表；为空时不命中
 *  @param mode     匹配方式
 *  @return 命中任意一个返回 true
 */
bool MatchAnyKeyword(const std::string& text, const std::vector<std::string>& keywords, int mode);

/**
 *  两段文字的相似度：去掉空白与标点并归一化后，按字符二元组计算 Dice 系数（只有一个字时按单字比较）。
 *  @return 0～1；两段都为空时为 0
 */
double TextSimilarity(const std::string& a, const std::string& b);

//问答作答用的一条问答
struct QaItem
{
    std::string m_question;  //问题
    std::string m_answer;    //答案

    QaItem()
    {
    }

    QaItem(const std::string& q, const std::string& a)
        : m_question(q), m_answer(a)
    {
    }
};

//「用问答作答」的结果
enum qa_answer_kind
{
    //一条都对不上，也没有可推荐的问题
    qa_answer_none        = 0,
    //找到了答案（完全相同，或相似度不低于阈值）
    qa_answer_found       = 1,
    //没有找到答案，但有几个相近的问题可以推荐
    qa_answer_suggestions = 2,
};

//「用问答作答」的结果详情
struct QaAnswer
{
    int                 m_kind;         //取值见 qa_answer_kind
    size_t              m_index;        //找到答案时为问答下标
    double              m_score;        //找到答案时的相似度（完全相同为 1）
    std::vector<size_t> m_suggestions;  //推荐的问题下标，按相似度从高到低

    QaAnswer()
        : m_kind(qa_answer_none), m_index(0), m_score(0)
    {
    }
};

//问答作答：相似度不低于它即直接回答
const double kQaAnswerThreshold = 0.6;
//问答作答：相似度不低于它才列为推荐
const double kQaSuggestThreshold = 0.2;
//问答作答：最多推荐几条
const size_t kQaSuggestMax = 3;

/**
 *  在问答里找一个问题的答案（§5.4）。
 *  @param question 提问（已剪掉 @ 片段，未归一化）
 *  @param items    可用的问答（本群问答在前、企业问答在后，已去重）
 *  @return 结果；提问为空时为 qa_answer_none
 */
QaAnswer AnswerFromQa(const std::string& question, const std::vector<QaItem>& items);

/**
 *  把作答结果写成回复文字。
 *  @param answer   AnswerFromQa 的结果
 *  @param items    同一组问答
 *  @param fallback 规则里配置的兜底文字；为空时用默认文字
 *  @return 回复文字
 */
std::string BuildQaReplyText(const QaAnswer& answer, const std::vector<QaItem>& items, const std::string& fallback);

//======== 时间 ========

//判定用的本地时刻
struct LocalClock
{
    int m_year;          //年，如 2026
    int m_month;         //月，1～12
    int m_day;           //日，1～31
    int m_weekday;       //星期，1 周一 … 7 周日
    int m_minuteOfDay;   //一天中的第几分钟，0～1439

    LocalClock()
        : m_year(1970), m_month(1), m_day(1), m_weekday(4), m_minuteOfDay(0)
    {
    }
};

/**
 *  把 Unix 毫秒换成服务器本地时间的 LocalClock。
 *  @param unixMs Unix 毫秒
 *  @return 本地时刻
 */
LocalClock MakeLocalClock(int64_t unixMs);

/**
 *  定时规则在这一分钟是否到点。
 *  @param schedule 定时设置
 *  @param clock    当前本地时刻
 *  @return 到点返回 true
 */
bool ScheduleDue(const BotSchedule& schedule, const LocalClock& clock);

/**
 *  定时消息的判重键：同一规则在同一分钟只发一次。
 *  @return 形如 "202610080900"
 */
std::string ScheduleMinuteKey(const LocalClock& clock);

/**
 *  当前时刻是否在时间段内；时间段没有启用时恒为 true。
 *  @param window 时间段
 *  @param clock  当前本地时刻
 *  @return 在时间段内返回 true
 */
bool InTimeWindow(const BotTimeWindow& window, const LocalClock& clock);

/**
 *  合并一个人多个设备的在线状态，取最在线的那个：在线 > 忙碌 > 离开 > 离线（§3.5）。
 *  @param presences 各设备的状态（Protocol.h 的 presence_type）；为空表示不在线
 *  @return 合并后的状态
 */
int MergePresence(const std::vector<int>& presences);

/**
 *  发送者是否在规则的发送者范围内。
 *  @param scope       发送者范围
 *  @param senderId    发送者 userid
 *  @param senderDepts 发送者所在的部门及其全部上级部门（只在 bot_sender_depts 时使用）
 *  @return 在范围内返回 true
 */
bool SenderInScope(const BotSenderScope& scope, int64_t senderId, const std::vector<int64_t>& senderDepts);

/**
 *  替换回复文字里的变量：{发送者}、{主人}、{日期}、{新成员}。
 *  @return 替换后的文字
 */
std::string ExpandVariables(const std::string& text, const std::string& sender, const std::string& owner,
                            const std::string& date, const std::string& newMembers);

//======== 判定 ========

//一次判定的结果
struct BotDecision
{
    int         m_kind;       //取值见 bot_decision_kind
    int64_t     m_ruleId;     //命中的规则 id
    size_t      m_ruleIndex;  //命中的规则下标
    std::string m_ruleName;   //命中的规则名称
    std::string m_text;       //回复文字（bot_decision_reply）；外部服务的兜底文字（bot_decision_external）
    bool        m_markdown;   //回复是否按 Markdown 显示
    std::string m_url;        //外部服务地址（bot_decision_external）
    std::string m_question;   //外部服务收到的正文（剪掉 @ 之后）
    int         m_reason;     //没有产生回复时的原因，取值见 bot_decision_reason

    BotDecision()
        : m_kind(bot_decision_none), m_ruleId(0), m_ruleIndex(0), m_markdown(false), m_reason(bot_reason_no_rule)
    {
    }
};

/**
 *  群消息的判定（T1 被 @、T2 关键词）。规则按顺序判定，命中第一条即止。
 *  @param bot        机器人（调用方已确认它在这个群可用、处于启用状态）
 *  @param msg        消息解析结果
 *  @param clock      当前本地时刻（检查规则的时间段）
 *  @param qa         本群可用的问答；为 NULL 时「用问答作答」的规则一律回复兜底文字
 *  @param senderName 发送者显示名（替换 {发送者}）
 *  @return 判定结果
 */
BotDecision EvaluateGroupMessage(const BotRecord& bot, const MessageView& msg, const LocalClock& clock,
                                 const std::vector<QaItem>* qa, const std::string& senderName);

//个人机器人判定的输入
struct PersonalInput
{
    MessageView          m_msg;              //消息解析结果
    int                  m_msgtype;          //消息内容类型
    int64_t              m_senderId;         //发送者 userid
    std::string          m_senderName;       //发送者显示名
    std::string          m_ownerName;        //主人显示名
    std::vector<int64_t> m_senderDepts;      //发送者所在部门及全部上级部门
    int                  m_ownerPresence;    //主人合并后的在线状态
    LocalClock           m_clock;            //当前本地时刻
    int64_t              m_nowMs;            //当前时间（Unix 毫秒）
    int64_t              m_lastReplyMs;      //这个机器人上一次代答这位发送者的时间；0 表示没有代答过
    int64_t              m_ownerLastSpokeMs; //主人上一次在这个会话里发言的时间；0 表示不知道

    PersonalInput()
        : m_msgtype(1), m_senderId(0), m_ownerPresence(0), m_nowMs(0), m_lastReplyMs(0), m_ownerLastSpokeMs(0)
    {
    }
};

/**
 *  单聊消息的判定（T5 收到单聊）：在线状态、时间段、发送者范围、关键词、冷却、主人正在交谈。
 *  @param bot 个人机器人（含「自动回复」的预置机器人）
 *  @param in  输入
 *  @return 判定结果
 */
BotDecision EvaluateSingleMessage(const BotRecord& bot, const PersonalInput& in);

/**
 *  入群的判定（T3）：命中第一条启用的入群规则。
 *  @param bot        机器人
 *  @param clock      当前本地时刻
 *  @param memberNames 新成员的显示名，用顿号连接后替换 {新成员}
 *  @return 判定结果
 */
BotDecision EvaluateJoin(const BotRecord& bot, const LocalClock& clock, const std::vector<std::string>& memberNames);

/**
 *  这一分钟到点的定时规则。
 *  @param bot   机器人
 *  @param clock 当前本地时刻
 *  @return 到点的规则下标
 */
std::vector<size_t> DueScheduleRules(const BotRecord& bot, const LocalClock& clock);

/**
 *  机器人是否有 Webhook 规则（有才接受外部系统推送）。
 */
bool HasWebhookRule(const BotRecord& bot);

/**
 *  机器人能否被 @：有启用的 T1 规则，或要求 @ 的 T2 规则。客户端据此决定是否列进 @ 候选。
 */
bool IsAtable(const BotRecord& bot);

/**
 *  由规则生成的用法说明（结构化），随 2801 下发；客户端按界面语言写成文字显示在机器人资料卡上，
 *  例如「在群里 @我 并输入问题」。每条启用的 T1～T4、T6 规则一项：
 *      {"t":1,"qa":1}                         被 @（qa 为 1 表示用问答作答）
 *      {"t":2,"kw":["VPN"],"m":1,"at":1}       关键词（m 为匹配方式，at 为是否要求 @）
 *      {"t":3}                                 新成员入群
 *      {"t":4,"k":2,"time":"09:00","wd":[1],"md":1}  定时
 *      {"t":6}                                 接收外部系统推送
 *  @return JSON 数组；没有启用的规则时为空数组
 */
Json::Value BuildUsageJson(const BotRecord& bot);

//======== 消息 content ========

/**
 *  机器人在群里回复的 content：正文以「@发送者 」开头并带 content.at，使发送者收到 @ 提醒（§6.2）。
 *  @param botId      机器人 id（写进 content.bot.id）
 *  @param text       回复正文
 *  @param markdown   是否按 Markdown 显示
 *  @param atUserId   要 @ 的人；0 表示不 @（定时、入群、Webhook）
 *  @param atName     要 @ 的人的显示名
 *  @param botName    机器人名称（写进 content.bot.name）：机器人删除后，客户端从本群机器人列表里查不到它，
 *                    凭这个名字显示历史消息的发送者
 *  @return content 对象的紧凑 JSON 文本
 */
std::string BuildGroupReplyContent(int64_t botId, const std::string& text, bool markdown, int64_t atUserId,
                                   const std::string& atName, const std::string& botName);

/**
 *  个人机器人在单聊里代答的 content：from 为主人，带 autoreply 与 bot 标记，正文前缀「[机器人] 」或「[自动回复] 」（§6.2）。
 *  @param bot  个人机器人（m_source 为 bot_source_autoreply 时前缀为「[自动回复] 」）
 *  @param text 回复正文
 *  @return content 对象的紧凑 JSON 文本
 */
std::string BuildPersonalReplyContent(const BotRecord& bot, const std::string& text);

//======== 序列化与校验 ========

/**
 *  触发方式与附加条件 → JSON（存库的 f_trigger 与线上共用）。
 */
Json::Value TriggerToJson(const BotTrigger& t);

/**
 *  JSON → 触发方式与附加条件。缺省字段取默认值。
 *  @return v 是对象且 type 合法返回 true
 */
bool TriggerFromJson(const Json::Value& v, BotTrigger& t);

/**
 *  动作 → JSON（存库的 f_action 与线上共用）。
 */
Json::Value ActionToJson(const BotAction& a);

/**
 *  JSON → 动作。
 *  @return v 是对象且 type 合法返回 true
 */
bool ActionFromJson(const Json::Value& v, BotAction& a);

/**
 *  一条规则 → 线上 JSON：{id, name, enabled, trigger, action}。
 */
Json::Value RuleToJson(const BotRule& r);

/**
 *  线上 JSON → 一条规则（id 为字符串编码）。
 *  @return 结构正确返回 true
 */
bool RuleFromJson(const Json::Value& v, BotRule& r);

//外部系统推送消息的请求路径（HTTP 短连接端口上，由 HttpSession 处理）
const char* const kWebhookPath = "/bot/webhook.do";

/**
 *  由对外根地址拼出 Webhook 的完整地址：去掉首尾空白与末尾的「/」，再接上 kWebhookPath
 *  （2026-10-08 用户确认，docs/机器人方案.md §11 Q19）。
 *    baseUrl：chatserver.conf 的 botwebhookbaseurl，如 "https://im.example.com:8443"，也可以带路径前缀，
 *             如 "https://example.com/im"（反向代理把 /im 转到 chatserver 的 HTTP 端口时）。
 *  @return 完整地址；根地址为空（或只有空白与斜杠）时返回空串，表示没有配置
 */
std::string BuildWebhookUrl(const std::string& baseUrl);

/**
 *  机器人 → 线上 JSON。
 *  @param b           机器人
 *  @param withRules   是否带规则
 *  @param withSecrets 是否带 Webhook 令牌与签名密钥（只给有编辑权限的人）
 */
Json::Value BotToJson(const BotRecord& b, bool withRules, bool withSecrets);

/**
 *  线上 JSON → 机器人的可编辑字段：botid、name、desc、glyph、color、status、scope、groupids、allowgroupoff、rules。
 *  归属、企业、令牌等由调用方填写。
 *  @return 结构正确返回 true（内容是否合法另由 ValidateBot 判断）
 */
bool BotFromJson(const Json::Value& v, BotRecord& b);

//ValidateBot 的结果
enum bot_check_result
{
    //合法
    bot_check_ok          = 0,
    //字段不合法：名称为空或超长、关键词为空、回复为空、时间格式错误等
    bot_check_invalid     = 1,
    //规则数、关键词数超出上限
    bot_check_full        = 2,
    //用了这种机器人不允许的触发方式或动作（如个人机器人用外部服务、群机器人在不允许时用外部服务）
    bot_check_not_allowed = 3,
};

/**
 *  校验一个机器人（含全部规则）。
 *  @param b             机器人；m_ownerType 必须已填
 *  @param allowExternal 是否允许 Webhook 与外部服务（企业机器人恒允许；群机器人看企业开关；个人机器人恒不允许）
 *  @param domains       外部服务的域名白名单；外部服务地址的主机必须是其中之一或其子域名
 *  @param why           [出参] 不合法时的原因（英文，写日志、回给客户端）
 *  @return 取值见 bot_check_result
 */
int ValidateBot(const BotRecord& b, bool allowExternal, const std::vector<std::string>& domains, std::string& why);

/**
 *  外部服务地址是否合法：http 或 https，主机在域名白名单内（等于其中之一或是其子域名）。
 *  @param url     地址
 *  @param domains 域名白名单（小写）
 *  @return 合法返回 true
 */
bool IsUrlAllowed(const std::string& url, const std::vector<std::string>& domains);

/**
 *  域名白名单的一项是否合法：只含字母、数字、点与连字符，不以点开头或结尾，至少有一个点。
 */
bool IsValidDomain(const std::string& domain);

/**
 *  生成 32 位十六进制的随机串（Webhook 令牌、签名密钥）。
 */
std::string RandomHex32();

/**
 *  计算签名：HMAC-SHA256(secret, timestamp + "\n" + body)，十六进制小写。
 *  外部服务请求的 X-Flamingo-Signature 与 Webhook 的 sign 字段都用它。
 *  @param secret    签名密钥
 *  @param timestamp 时间戳（Unix 毫秒的十进制文本）
 *  @param body      请求体
 *  @return 64 位十六进制
 */
std::string ComputeSignature(const std::string& secret, const std::string& timestamp, const std::string& body);

/**
 *  个人「离开 / 忙碌自动回复」设置与预置机器人规则之间的换算（§11 Q2）。
 */
struct AutoReplySettings
{
    bool        m_awayOn;       //离开时自动回复
    std::string m_awayText;     //离开时的回复文字（实际发出的文字）
    bool        m_awayDefault;  //离开时的文字是客户端给的默认文字（用户没有自己填写）
    bool        m_busyOn;       //忙碌时自动回复
    std::string m_busyText;     //忙碌时的回复文字（实际发出的文字）
    bool        m_busyDefault;  //忙碌时的文字是客户端给的默认文字

    AutoReplySettings()
        : m_awayOn(false), m_awayDefault(true), m_busyOn(false), m_busyDefault(true)
    {
    }
};

/**
 *  自动回复设置 → 预置机器人的两条规则（离开一条、忙碌一条；关闭的那条 m_enabled 为 false）。
 */
std::vector<BotRule> AutoReplyToRules(const AutoReplySettings& s);

/**
 *  预置机器人的规则 → 自动回复设置。
 */
AutoReplySettings RulesToAutoReply(const std::vector<BotRule>& rules);

} // namespace bot
