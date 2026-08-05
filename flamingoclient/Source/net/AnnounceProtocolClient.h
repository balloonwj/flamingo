/**
 *  AnnounceProtocolClient.h
 *
 *  平台公告报文（cmd_get_announcements=2101 / cmd_mark_announcement_read=2102 /
 *  cmd_announcement_notify=2103）的客户端侧编解码，与服务端
 *  chatserversrc/AnnouncementProtocol.{h,cpp} 一一对应。
 *
 *  平台公告由企业管理控制台(flamingoAdmin)发布，投递到 PC 主面板的「消息盒子」。
 *  典型用法：登录后发 cmd_get_announcements 全量拉一遍落本地库，之后靠
 *  cmd_announcement_notify 推送增量；用户在消息盒子里点开某条时发
 *  cmd_mark_announcement_read 上报已读，管理控制台的「已读进度」统计的就是它。
 *
 *  注意与<u>群公告</u>（cmd_get_group_announcements=1323 等，见 GroupProtocolClient）
 *  不是一回事：群公告归属某个具体的群，平台公告归属整个平台，两套报文互不相干。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不碰窗口、不碰 socket，可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

/**
 *  一条平台公告，即「消息盒子」列表里的一项。
 *
 *  字段与服务端 AnnouncementPushItem 一一对应。文本字段一律是 <u>UTF-8</u>，
 *  交给界面前须转成 UTF-16。
 */
struct AnnouncementItem
{
    int64_t     m_announcementId;  //公告ID，全局唯一且递增；本端据此去重、并作增量拉取的游标
    std::string m_title;           //标题（UTF-8）
    std::string m_body;            //正文（UTF-8）
    int         m_severity;        //级别，取值见 Protocol.h 的 announcement_severity（0 普通 1 提醒 2 警告）
    std::string m_senderName;      //发布者名称（UTF-8），如「平台运营」
    int         m_pinned;          //是否置顶：0 否 1 是；置顶的在消息盒子列表里排最前
    int64_t     m_sentTimeMs;      //发布时间，Unix 毫秒
    int         m_read;            //本人是否已读：0 未读 1 已读

    AnnouncementItem()
        : m_announcementId(0)
        , m_severity(0)
        , m_pinned(0)
        , m_sentTimeMs(0)
        , m_read(0)
    {
    }
};

//cmd_get_announcements 应答解析结果。
struct AnnouncementsResult
{
    int         m_code;      //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
    std::string m_msg;       //结果描述（UTF-8），可为空串
    bool        m_hasMore;   //是否还有更多：为 true 时本端应以最后一条的ID作新的 sinceid 继续拉
    std::vector<AnnouncementItem> m_items;  //本次返回的公告，已按「置顶优先、发布时间倒序」排好

    AnnouncementsResult()
        : m_code(-1)       //-1：尚未解析出有效结果码
        , m_hasMore(false)
    {
    }
};

//cmd_mark_announcement_read 应答解析结果。
struct AnnounceMarkReadResult
{
    int         m_code;            //结果码，0 成功
    std::string m_msg;             //结果描述（UTF-8），可为空串
    int64_t     m_announcementId;  //本次上报的公告ID，服务端原样回带 —— 用户连点几条时
                                   //会有多条上报在途，靠它把结果对上是哪一条

    AnnounceMarkReadResult()
        : m_code(-1)       //-1：尚未解析出有效结果码
        , m_announcementId(0)
    {
    }
};

//cmd_announcement_notify 推送解析结果。
struct AnnouncementNotify
{
    int m_changeType;           //变更类型，取值见 Protocol.h 的 announcement_change_type（1 新发布 2 已撤回）
    AnnouncementItem m_item;    //公告内容；m_changeType 为「已撤回」时<u>只有</u> m_announcementId 有效

    AnnouncementNotify()
        : m_changeType(0)
    {
    }
};

//平台公告报文编解码工具类；全部为静态方法，不可实例化。
class AnnounceProtocolClient
{
public:
    /**
     *  构造「拉取发给本人的平台公告」请求（cmd_get_announcements=2101）。
     *
     *    sinceId： 增量游标——只要ID大于它的公告；传 0 表示全量拉取（首次登录用）。
     *    limit：   本次最多要多少条；服务端会把它规整到 [1, 100]，传 0 即用服务端默认值 50。
     *
     *  @return 紧凑格式的请求 JSON，形如 {"sinceid":"10023","limit":50}。
     *          sinceid 以字符串编码，避免大整数在 JSON 里丢精度。
     */
    static std::string buildGetAnnouncementsRequest(int64_t sinceId, int limit);

    /**
     *  构造「上报某条平台公告已读」请求（cmd_mark_announcement_read=2102）。
     *
     *    announcementId： 被读的公告ID（须 > 0，否则服务端以 bad request 拒绝）。
     *
     *  @return 紧凑格式的请求 JSON，形如 {"announcementid":"10023"}。
     *
     *  重复上报是安全的：服务端靠 (公告ID, 用户ID) 唯一键吸收，第二次起静默跳过。
     */
    static std::string buildMarkReadRequest(int64_t announcementId);

    /**
     *  解析 cmd_get_announcements 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法且含整型 code 时返回 true；否则 false（调用方应丢弃该应答）。
     *
     *  单个条目缺字段时不会让整条应答失败——那一条按默认值填充后照常收下，
     *  免得服务端将来加字段或个别数据不全就导致整批公告都收不到。
     */
    static bool parseGetAnnouncementsResponse(const std::string& jsonbody, AnnouncementsResult& out);

    /**
     *  解析 cmd_announcement_notify 推送包体。
     *
     *    jsonbody： 推送包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法、changetype 有效且公告ID为正时返回 true；否则 false。
     *
     *  撤回推送只带公告ID（服务端不重复传一遍正文），故此时 out.m_item 除
     *  m_announcementId 外都是默认值，调用方不要去读它们。
     */
    static bool parseAnnouncementNotify(const std::string& jsonbody, AnnouncementNotify& out);

    /**
     *  解析 cmd_mark_announcement_read 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法且含整型 code 时返回 true；否则 false（调用方应丢弃该应答）。
     *
     *  announcementid 由服务端原样回带；即便它缺失也不判整条应答失败 —— code 已经拿到，
     *  调用方至少知道成没成。
     */
    static bool parseMarkReadResponse(const std::string& jsonbody, AnnounceMarkReadResult& out);

private:
    AnnounceProtocolClient() = delete;
    ~AnnounceProtocolClient() = delete;
    AnnounceProtocolClient(const AnnounceProtocolClient&) = delete;
    AnnounceProtocolClient& operator=(const AnnounceProtocolClient&) = delete;
};

} // namespace net
