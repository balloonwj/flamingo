/**
 *  平台公告投递报文（cmd_get_announcements / cmd_mark_announcement_read /
 *  cmd_announcement_notify）的编解码，AnnouncementProtocol.h
 *
 *  这三条报文负责把企业管理控制台(flamingoAdmin)发布的平台公告投递给 IM 客户端，
 *  在 PC 主面板的「公告中心」里展示。典型用法：会话层收到 cmd_get_announcements
 *  后先用 parseGetAnnouncementsRequest 取出请求参数，交 AnnouncementManager 做受众
 *  匹配拿到公告列表，再用 buildGetAnnouncementsResponse 编回应答。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试（见
 *  tests/test_announcementprotocol.cpp）。受众匹配、已读落库、推送时机等都不在
 *  这里，分别位于 AnnouncementManager 与 ChatSession。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//cmd_get_announcements 单次返回的条数硬上限。客户端传的 limit 超过它一律按它截断，
//防止受众为「全部企业」的历史公告积累很多时，一次拉取产生过大的包。
#define ANNOUNCEMENT_FETCH_LIMIT_MAX       100

//客户端没指定 limit（字段缺失或传了非正数）时采用的默认条数。
#define ANNOUNCEMENT_FETCH_LIMIT_DEFAULT   50

/**
 *  一条投递给 IM 客户端的平台公告。
 *
 *  与管理侧的 AnnouncementInfo（AnnouncementManager.h）<u>不是</u>同一个结构，
 *  两者不要混用：那个是给管理控制台看的，带草稿状态、受众类型、投放目标、已读
 *  人数等运营字段；这个只装普通用户在公告中心里该看到的内容。
 */
struct AnnouncementPushItem
{
    int64_t     announcementId;  //公告业务ID，全局唯一且递增；客户端据此去重、并作增量拉取的游标
    std::string title;           //公告标题
    std::string body;            //公告正文
    int         severity;        //级别，取值见 Protocol.h 的 announcement_severity（0 普通 1 提醒 2 警告）
    std::string senderName;      //发布者名称（如「平台运营」），冗余自 t_announcement.f_sender_name
    int         pinned;          //是否置顶：0 否 1 是；置顶的公告在公告中心列表里排最前
    int64_t     sentTime;        //实际发布时间，Unix 毫秒（表里存的是 datetime，由 Manager 转换）
    int         read;            //本人是否已读：0 未读 1 已读；据 t_announcement_read 有无对应行算出

    AnnouncementPushItem()
        : announcementId(0), severity(0), pinned(0), sentTime(0), read(0)
    {
    }
};

//cmd_get_announcements 请求的参数。
struct GetAnnouncementsReqData
{
    int64_t sinceId;  //本端已有的最大公告ID；0 表示全量拉取，非0时服务端只返回ID大于它的公告
    int     limit;    //本次最多要多少条；解析时会规整到 [1, ANNOUNCEMENT_FETCH_LIMIT_MAX]

    GetAnnouncementsReqData()
        : sinceId(0), limit(ANNOUNCEMENT_FETCH_LIMIT_DEFAULT)
    {
    }
};

//平台公告投递报文的编解码工具类；全部为静态方法，不可实例化。
class AnnouncementProtocol
{
public:
    /**
     *  解析 cmd_get_announcements 请求。
     *
     *  两个字段都是可选的：请求体为空对象 {} 时表示「全量拉取、用默认条数」，这是
     *  客户端首次登录时的正常用法，<u>不</u>算错误。
     *
     *    json  请求包体（UTF-8 JSON 文本）。
     *    req   [出参] 解析结果；sinceId 缺失时为 0，limit 缺失或非正时取
     *          ANNOUNCEMENT_FETCH_LIMIT_DEFAULT，超过上限时截断为 ANNOUNCEMENT_FETCH_LIMIT_MAX。
     *  @return JSON 文本合法且是一个对象时返回 true；否则返回 false 且 req 不可用。
     */
    static bool parseGetAnnouncementsRequest(const std::string& json, GetAnnouncementsReqData& req);

    /**
     *  解析 cmd_mark_announcement_read 请求。
     *
     *    json            请求包体（UTF-8 JSON 文本）。
     *    announcementId  [出参] 被标记为已读的公告ID。
     *  @return announcementid 字段缺失或非正数时返回 false（此时不应写库）。
     */
    static bool parseMarkReadRequest(const std::string& json, int64_t& announcementId);

    /**
     *  构造 cmd_get_announcements 应答 JSON：{ code, msg, hasmore, announcements:[...] }。
     *
     *    code     结果码，0 表示成功，取值见 Protocol.h 的 enum error_code。
     *    msg      结果描述。
     *    hasMore  是否还有更多：本次返回条数被 limit 截断时为 true，客户端据此用
     *             最后一条的ID作新的 sinceId 继续拉。
     *    items    本次返回的公告列表；code 非 0 时传空 vector 即可。
     *  @return 紧凑（无缩进）的 JSON 文本。
     */
    static std::string buildGetAnnouncementsResponse(int code, const std::string& msg, bool hasMore,
                                                     const std::vector<AnnouncementPushItem>& items);

    /**
     *  构造 cmd_announcement_notify 推送 JSON：{ changetype, announcement:{...} }。
     *
     *    changeType  变更类型，取值见 Protocol.h 的 announcement_change_type。
     *    item        公告内容。changeType 为 announcement_change_recalled（已撤回）时，
     *                只有 announcementId 会被写进 JSON —— 撤回通知不需要携带正文，
     *                客户端凭ID把本地那条移除即可。
     *  @return 紧凑（无缩进）的 JSON 文本。
     */
    static std::string buildAnnouncementNotify(int changeType, const AnnouncementPushItem& item);

    /**
     *  构造通用 { code, msg } 应答，供各类错误应答使用。
     *    code  结果码，0 表示成功。
     *    msg   结果描述。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  构造 cmd_mark_announcement_read 应答：{ code, msg, announcementid }。
     *
     *  比通用应答多回带一个 announcementid，是为了让客户端<u>对上号</u>：用户在公告中心里
     *  连点几条公告时会有多条已读上报在途，应答里不带ID的话，失败了也不知道是哪条失败、
     *  没法重试。与 cmd_set_favorite 原样回带 ctype/cid/favorite 是同一个道理。
     *
     *    code            结果码，0 表示成功。
     *    msg             结果描述。
     *    announcementId  本次上报的公告ID，原样回带（解析失败时传 0）。
     */
    static std::string buildMarkReadResponse(int code, const std::string& msg, int64_t announcementId);

    /**
     *  把数据库里的 datetime 文本转成 Unix 毫秒。
     *
     *  放在本模块而不是 AnnouncementManager，是因为它属于「把库里的表示形式编成协议要的
     *  表示形式」这一步，且必须能被单元测试直接覆盖。<u>不</u>在 SQL 里用 UNIX_TIMESTAMP()
     *  做这件事：本项目同时支持 MySQL 与 SQLite，而 SQLite 兼容层（sqliteapi/SqliteFunctions.cpp）
     *  只注册了 NOW / CURDATE / LAST_INSERT_ID / FROM_UNIXTIME / GREATEST / CONCAT / SUBSTRING，
     *  没有 UNIX_TIMESTAMP，在 SQL 里写它会让 SQLite 侧整条查询失败。
     *
     *    text  形如 "2026-08-05 14:30:00" 的日期时间文本，按<u>本地时区</u>解释
     *          （与 NOW() 写进库时的时区一致）。允许秒部分缺省（"2026-08-05 14:30"）。
     *  @return 对应的 Unix 毫秒；文本为空或格式无法识别时返回 0（调用方据此当作「没有发布时间」）。
     */
    static int64_t dateTimeToMs(const std::string& text);
};
