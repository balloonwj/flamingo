/**
 *  2802～2809 的请求构造与应答解析（docs/协议设计-v2.md 25.1、25.2；字段名与服务端 chatserversrc/BotProtocol.cpp、
 *  BotRules.cpp 的 BotToJson / RuleToJson / TriggerToJson / ActionToJson 一一对应，改一边要同步改另一边）。
 *
 *  ID 一律写成十进制字符串（与服务端一致），读的时候字符串与整数两种写法都接受。
 *  应答统一解析成 BotReply：哪些字段有效由命令号决定（见 BotReply 各字段的注释）。
 *
 *  纯逻辑，只依赖 jsoncpp，单元测试直接调用（Tests/BotCoreTests.cpp）。
 *
 *  典型用法：
 *      const std::string body = flamingobot::BotCodec::BuildSaveGroupBotRequest(groupId, bot);
 *      const uint32_t seq = host->SendBotCommand(flamingobot::bot_cmd_save_group_bot, body.data(), (uint32_t)body.size());
 *      ...
 *      flamingobot::BotReply reply;
 *      flamingobot::BotCodec::ParseReply(cmd, seq, json, reply);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "BotTypes.h"

namespace flamingobot {

//一条 2802～2809 应答的解析结果
struct BotReply
{
    uint16_t               m_cmd;      //命令号，取值见 BotCmd
    uint32_t               m_seq;      //帧 seq（与请求相同）
    bool                   m_parsed;   //包体是 JSON 对象且带 code；为 false 时其余字段无意义，按失败处理
    int                    m_code;     //结果码，0 成功，其余见 BotErrorCode
    std::string            m_msg;      //结果说明（服务端写的英文，只记日志，不显示）
    int64_t                m_groupId;  //2803 / 2804 / 2805：群号
    int64_t                m_botId;    //2803 / 2804 / 2805 / 2807 / 2808：机器人 id（新建成功时为新分配的 id）
    bool                   m_off;      //2805：请求的关闭状态
    bool                   m_hasBot;   //2802：应答里带了机器人
    BotRecord              m_bot;      //2802：机器人（含规则；有编辑权限时含 Webhook 令牌与签名密钥）
    bool                   m_allowed;  //2806：企业允许个人机器人
    int                    m_max;      //2806：每人最多个人机器人数
    std::vector<BotRecord> m_bots;     //2806：本人的个人机器人（含规则，不含自动回复的预置机器人）
    BotTryResult           m_try;      //2809：「试一试」的结果

    BotReply()
        : m_cmd(0)
        , m_seq(0)
        , m_parsed(false)
        , m_code(-1)
        , m_groupId(0)
        , m_botId(0)
        , m_off(false)
        , m_hasBot(false)
        , m_allowed(false)
        , m_max(kPersonalBotMax)
    {
    }
};

/**
 *  BotCodec —— 报文编解码（全部为静态函数）。
 */
class BotCodec
{
public:
    /**
     *  2802 拉取机器人详情：{"botid":"12"}。
     */
    static std::string BuildGetDetailRequest(int64_t botId);

    /**
     *  2803 保存群机器人：{"groupid":"...","bot":{botid("0" 表示新建),name,desc,glyph,color,status,rules}}。
     *  @param groupId 群号
     *  @param bot     机器人（m_botId 为 0 表示新建）
     */
    static std::string BuildSaveGroupBotRequest(int64_t groupId, const BotRecord& bot);

    /**
     *  2804 删除群机器人：{"groupid":"...","botid":"..."}。
     */
    static std::string BuildDeleteGroupBotRequest(int64_t groupId, int64_t botId);

    /**
     *  2805 企业机器人在本群关闭 / 打开，群机器人停用 / 启用：{"groupid":"...","botid":"...","off":0|1}。
     */
    static std::string BuildSetGroupBotOffRequest(int64_t groupId, int64_t botId, bool off);

    /**
     *  2806 拉取本人的个人机器人：{}。
     */
    static std::string BuildGetMyBotsRequest();

    /**
     *  2807 保存个人机器人：{"bot":{...}}。
     */
    static std::string BuildSaveMyBotRequest(const BotRecord& bot);

    /**
     *  2808 删除个人机器人：{"botid":"..."}。
     */
    static std::string BuildDeleteMyBotRequest(int64_t botId);

    /**
     *  2809「试一试」，用尚未保存的机器人（编辑窗里的当前内容）判定：
     *  群机器人 {"groupid":"...","bot":{...},"input":{text,at}}；个人机器人 {"bot":{...},"input":{text,presence,senderid,time}}。
     *  @param groupId 群号；个人机器人传 0（服务端据此把 bot 当成本人的个人机器人草稿）
     *  @param draft   机器人草稿
     *  @param input   模拟输入
     */
    static std::string BuildTryRequest(int64_t groupId, const BotRecord& draft, const BotTryInput& input);

    /**
     *  解析一条 2802～2809 的应答。
     *  @param cmd  命令号
     *  @param seq  帧 seq
     *  @param body 包体（UTF-8 JSON）
     *  @param out  [出参] 解析结果；包体不是 JSON 对象或缺少 code 时 m_parsed 为 false
     *  @return 与 out.m_parsed 相同
     */
    static bool ParseReply(uint16_t cmd, uint32_t seq, const std::string& body, BotReply& out);

    /**
     *  机器人对象写成保存请求里的 bot（只写可编辑字段：botid、name、desc、glyph、color、status、rules）。
     *  @return 紧凑 JSON 文本
     */
    static std::string BotToSaveJson(const BotRecord& bot);

    /**
     *  解析一个机器人对象（2802 的 bot、2806 的 bots[]）。
     *  @param json 机器人对象的 JSON 文本
     *  @param out  [出参] 机器人；规则解析不出的条目跳过
     *  @return json 是对象返回 true
     */
    static bool ParseBotJson(const std::string& json, BotRecord& out);

    /**
     *  一条规则写成规则对象的 JSON 文本（单元测试核对字段名与服务端 RuleToJson 一致）。
     */
    static std::string RuleToJson(const BotRule& rule);

    /**
     *  解析一条规则对象。
     *  @return 触发方式在 1～6 之内、且（外部系统推送以外的）动作在 1～3 之内时返回 true，与服务端 RuleFromJson 相同
     */
    static bool ParseRuleJson(const std::string& json, BotRule& out);
};

} // namespace flamingobot
