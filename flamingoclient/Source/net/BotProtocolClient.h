/**
 *  机器人的报文编解码（客户端侧，docs/协议设计-v2.md 第 25 节、docs/机器人方案.md 第 6 节）：
 *  cmd_get_group_bots(2801) 的请求与应答、cmd_group_bots_changed_notify(2820) 推送、
 *  cmd_get_auto_reply(2810) / cmd_set_auto_reply(2811) 的请求与应答。2802～2809 由 FlamingoBot.dll 自己编解码，
 *  exe 只负责把这几条帧原样转交（IsDllFrame）。
 *
 *  纯逻辑，只依赖 jsoncpp，不触及网络与 UI，单测见 Source/Tests/BotClientTests.cpp（标签 [bot]）。
 *
 *  典型用法：
 *      std::string body = net::BotProtocolClient::buildGetGroupBotsRequest(groupid);
 *      net::GroupBotsResult r;
 *      net::BotProtocolClient::parseGetGroupBotsResponse(json, r);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//机器人用法说明的一项（2801 bots[].usage 数组的一项），客户端按界面语言写成文字
struct BotUsageItem
{
    int                      m_type;          //触发方式：1 被 @、2 关键词、3 入群、4 定时、6 外部系统推送
    bool                     m_qa;            //被 @：是否用问答作答
    std::vector<std::string> m_keywords;      //关键词（UTF-8）
    int                      m_match;         //关键词匹配方式：1 包含、2 完全相同、3 以……开头
    bool                     m_needAt;        //关键词：是否要求同时 @
    int                      m_scheduleKind;  //定时：1 每天、2 工作日、3 每周、4 每月
    std::string              m_time;          //定时：HH:MM
    std::vector<int>         m_weekdays;      //定时：每周的哪几天（1 周一 … 7 周日）
    int                      m_monthDay;      //定时：每月几号

    BotUsageItem()
        : m_type(0), m_qa(false), m_match(1), m_needAt(true), m_scheduleKind(1), m_monthDay(1)
    {
    }
};

//一个群可用的机器人（2801 应答 bots 数组的一项）
struct GroupBotInfo
{
    int64_t                   m_botId;      //机器人 ID
    int64_t                   m_userId;     //机器人在群里发消息用的账号 userid
    int                       m_ownerType;  //1 企业机器人、2 群机器人
    bool                      m_isGroupBot; //source 为 "group"：本群的群机器人
    std::string               m_name;       //名称（UTF-8）
    std::string               m_desc;       //简介（UTF-8）
    std::string               m_glyph;      //头像字形（UTF-8）；空表示取名称的第一个字
    std::string               m_color;      //头像底色 "#RRGGBB"；空表示默认色
    int                       m_status;     //0 启用、1 停用
    bool                      m_off;        //企业机器人在本群被关闭
    bool                      m_active;     //在本群生效（启用且没有关闭）
    bool                      m_canOff;     //群主能否在本群关闭它（群机器人恒为 true，表示停用）
    bool                      m_atable;     //能否被 @（列进 @ 候选）
    std::vector<BotUsageItem> m_usage;      //用法说明

    GroupBotInfo()
        : m_botId(0), m_userId(0), m_ownerType(1), m_isGroupBot(false), m_status(0), m_off(false), m_active(false)
        , m_canOff(false), m_atable(false)
    {
    }
};

//2801 应答附带的能力说明
struct GroupBotCaps
{
    bool m_isOwner;          //请求人是群主（能看到「管理」入口）
    bool m_groupBotAllowed;  //企业允许群主添加群机器人
    bool m_externalAllowed;  //企业允许群主给群机器人配置外部服务
    int  m_groupBotMax;      //每群最多群机器人数

    GroupBotCaps()
        : m_isOwner(false), m_groupBotAllowed(false), m_externalAllowed(false), m_groupBotMax(0)
    {
    }
};

//拉取群机器人（2801）的应答解析结果
struct GroupBotsResult
{
    int                       m_code;     //0 成功
    std::string               m_msg;      //结果说明
    int64_t                   m_groupid;  //群号
    GroupBotCaps              m_caps;     //能力说明
    std::vector<GroupBotInfo> m_bots;     //本群的机器人（含停用的、在本群关闭了的）

    GroupBotsResult()
        : m_code(-1), m_groupid(0)
    {
    }
};

//「离开 / 忙碌自动回复」设置（2810 应答、2811 请求）
struct AutoReplyWire
{
    bool        m_awayOn;       //离开时自动回复
    std::string m_awayText;     //离开时实际发出的文字（UTF-8）
    bool        m_awayDefault;  //离开时的文字是界面语言的默认文字（用户没填）
    bool        m_busyOn;       //忙碌时自动回复
    std::string m_busyText;     //忙碌时实际发出的文字（UTF-8）
    bool        m_busyDefault;  //忙碌时的文字是默认文字

    AutoReplyWire()
        : m_awayOn(false), m_awayDefault(true), m_busyOn(false), m_busyDefault(true)
    {
    }
};

//拉取自动回复设置（2810）的应答解析结果
struct AutoReplyResult
{
    int           m_code;      //0 成功
    std::string   m_msg;       //结果说明
    bool          m_exists;    //服务端已经有这个人的设置（没有时客户端把本机 ini 的旧设置上传一次）
    bool          m_allowed;   //企业允许个人机器人（关闭时自动回复不生效）
    AutoReplyWire m_settings;  //设置

    AutoReplyResult()
        : m_code(-1), m_exists(false), m_allowed(false)
    {
    }
};

//机器人报文的编解码
class BotProtocolClient
{
public:
    /**
     *  构造拉取群机器人请求（2801）：{"groupid":"..."}。
     */
    static std::string buildGetGroupBotsRequest(int64_t groupid);

    /**
     *  解析拉取群机器人应答（2801）。
     *  @return 包体是 JSON 对象返回 true
     */
    static bool parseGetGroupBotsResponse(const std::string& body, GroupBotsResult& out);

    /**
     *  解析群机器人变化推送（2820）：{"groupid":"..."}；groupid 为 0 表示本企业的全部群。
     *  @return 包体合法返回 true
     */
    static bool parseGroupBotsChangedNotify(const std::string& body, int64_t& groupid);

    /**
     *  构造拉取自动回复设置请求（2810）：{}。
     */
    static std::string buildGetAutoReplyRequest();

    /**
     *  解析拉取自动回复设置应答（2810）。
     */
    static bool parseGetAutoReplyResponse(const std::string& body, AutoReplyResult& out);

    /**
     *  构造保存自动回复设置请求（2811）。
     */
    static std::string buildSetAutoReplyRequest(const AutoReplyWire& s);

    /**
     *  解析只有 {code, msg} 的应答（2811）。
     */
    static bool parseSimpleResponse(const std::string& body, int& code, std::string& msg);

    /**
     *  这一帧是否要原样转交 FlamingoBot.dll（2802～2809：机器人详情、群机器人与个人机器人的增删改、试一试）。
     *  2801、2810、2811、2820 由 exe 自己处理。
     */
    static bool isDllFrame(uint16_t cmd);
};

} // namespace net
