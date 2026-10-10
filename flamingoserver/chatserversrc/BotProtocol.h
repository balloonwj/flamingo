/**
 *  机器人客户端命令（2801～2811、2820）的编解码，BotProtocol.h（docs/机器人方案.md 第 6 节）。
 *
 *  纯逻辑：只依赖 jsoncpp 与 BotRules，不碰数据库与网络，单元测试直接调用（tests/test_bot.cpp 的 BOT-S8）。
 *  会话层的权限、开关与读写在 ChatSessionBot.cpp。
 *
 *  典型用法：
 *      int64_t groupid = 0;
 *      bot::BotRecord rec;
 *      if (botproto::ParseSaveGroupBotRequest(body, groupid, rec))
 *          ...
 *      send(cmd_save_group_bot, seq, botproto::BuildSaveResponse(0, "ok", groupid, rec.m_botId));
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "BotDefs.h"
#include "BotRules.h"
#include "BotEngine.h"   //BotTryInput

namespace botproto {

//群机器人列表里的一项（2801）
struct GroupBotView
{
    std::shared_ptr<const bot::BotRecord> m_bot;  //机器人
    bool m_off;     //企业机器人在本群被关闭
    bool m_active;  //在本群生效（启用且没有关闭）
    bool m_canOff;  //群主能否在本群关闭它：企业机器人看 allowgroupoff，群机器人恒为 true（停用）

    GroupBotView()
        : m_off(false), m_active(false), m_canOff(false)
    {
    }
};

//2801 应答附带的能力说明，客户端据此决定显示哪些入口
struct GroupBotCaps
{
    bool m_isOwner;          //请求人是群主
    bool m_groupBotAllowed;  //企业允许群主添加群机器人（且机器人总开关打开）
    bool m_externalAllowed;  //企业允许群主给群机器人配置外部服务
    int  m_groupBotMax;      //每群最多群机器人数

    GroupBotCaps()
        : m_isOwner(false), m_groupBotAllowed(false), m_externalAllowed(false), m_groupBotMax(0)
    {
    }
};

//「试一试」请求（2809）
struct TryRequest
{
    int64_t         m_botId;   //已保存的机器人 id；为 0 时用 m_bot
    bool            m_hasBot;  //请求里带了机器人（尚未保存的规则）
    bot::BotRecord  m_bot;     //请求里带的机器人
    BotTryInput     m_input;   //模拟输入

    TryRequest()
        : m_botId(0), m_hasBot(false)
    {
    }
};

// ---------------- 请求解析 ----------------

/**
 *  解析 {botid}（2802、2808）。
 *  @return botid 大于 0 时返回 true
 */
bool ParseBotIdRequest(const std::string& body, int64_t& botId);

/**
 *  解析 {groupid, botid}（2804）。
 */
bool ParseGroupBotIdRequest(const std::string& body, int64_t& groupid, int64_t& botId);

/**
 *  解析 {groupid, botid, off}（2805）。
 */
bool ParseSetGroupBotOffRequest(const std::string& body, int64_t& groupid, int64_t& botId, bool& off);

/**
 *  解析 {groupid, bot:{...}}（2803）。只取可编辑字段，归属等由会话层填写。
 */
bool ParseSaveGroupBotRequest(const std::string& body, int64_t& groupid, bot::BotRecord& rec);

/**
 *  解析 {bot:{...}}（2807）。
 */
bool ParseSaveMyBotRequest(const std::string& body, bot::BotRecord& rec);

/**
 *  解析「试一试」：{groupid, botid 或 bot, input:{text, at, presence, senderid, time}}（2809）。
 */
bool ParseTryRequest(const std::string& body, TryRequest& out);

/**
 *  解析自动回复设置：{awayon, awaytext, awaydefault, busyon, busytext, busydefault}（2811）。
 *  @return 结构正确且两段文字都不超过 kAutoReplyTextMaxChars 返回 true
 */
bool ParseAutoReplyRequest(const std::string& body, bot::AutoReplySettings& out);

// ---------------- 应答与推送 ----------------

//{code, msg}
std::string BuildSimpleResponse(int code, const std::string& msg);

/**
 *  2801 应答：{code, msg, groupid, caps:{isowner, groupbot, external, groupbotmax}, bots:[...]}。
 *  每个机器人在 BotToJson（不带规则与密钥）之外另带 source（enterprise / group）、off、active、canoff、atable、usage。
 */
std::string BuildGroupBotsResponse(int code, const std::string& msg, int64_t groupid, const GroupBotCaps& caps,
                                   const std::vector<GroupBotView>& bots);

/**
 *  2802 应答：{code, msg, bot:{...含规则}}；withSecrets 为 true 时带 Webhook 令牌与签名密钥，webhookUrl 不为空时
 *  还带 webhookurl（Webhook 的对外完整地址，§11 Q19）。
 *    webhookUrl：BotManager::webhookUrl()；空串表示服务端没有配置对外地址，不带这个字段。
 */
std::string BuildBotDetailResponse(int code, const std::string& msg, const bot::BotRecord* b, bool withSecrets,
                                   const std::string& webhookUrl);

/**
 *  2803 / 2804 应答：{code, msg, groupid, botid}。
 */
std::string BuildGroupBotResponse(int code, const std::string& msg, int64_t groupid, int64_t botId);

/**
 *  2805 应答：{code, msg, groupid, botid, off}。
 */
std::string BuildSetOffResponse(int code, const std::string& msg, int64_t groupid, int64_t botId, bool off);

/**
 *  2806 应答：{code, msg, allowed, max, bots:[...含规则]}；不含「自动回复」的预置机器人。
 */
std::string BuildMyBotsResponse(int code, const std::string& msg, bool allowed,
                                const std::vector<std::shared_ptr<const bot::BotRecord> >& bots);

/**
 *  2807 / 2808 应答：{code, msg, botid}。
 */
std::string BuildMyBotResponse(int code, const std::string& msg, int64_t botId);

/**
 *  2809 应答：{code, msg, matched, kind, ruleid, rulename, text, markdown, url, reason}。
 */
std::string BuildTryResponse(int code, const std::string& msg, const bot::BotDecision& d);

/**
 *  2810 应答：{code, msg, exists, allowed, awayon, awaytext, awaydefault, busyon, busytext, busydefault}。
 */
std::string BuildAutoReplyResponse(int code, const std::string& msg, bool exists, bool allowed,
                                   const bot::AutoReplySettings& s);

/**
 *  2820 推送：{groupid}。
 */
std::string BuildChangedNotify(int64_t groupid);

/**
 *  机器人相关的群系统提示 content：{text, tipkind, tipargs:{operator, bot, external}}。
 *  @param tipkind  Protocol.h 的 kSystemTipBot*
 *  @param operatorName 操作人（群主）显示名
 *  @param botName  机器人名称
 *  @param external 是否会把消息发送到外部服务（只用于 bot_added）
 */
std::string BuildBotTipContent(const char* tipkind, const std::string& operatorName, const std::string& botName,
                               bool external);

/**
 *  收集一个机器人里需要过敏感词的文字：名称、简介、规则名称、关键词、回复与兜底文字。
 */
void CollectTexts(const bot::BotRecord& b, std::vector<std::string>& out);

} // namespace botproto
