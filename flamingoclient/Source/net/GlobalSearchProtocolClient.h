/**
 *  全局搜索（cmd_global_search 1904）报文编解码（客户端侧），GlobalSearchProtocolClient.h
 *
 *  主面板搜索下拉「消息」标签末尾的「搜索服务器上的聊天记录」用：在服务器上本人能看到的全部聊天
 *  记录里按关键字搜索、按会话汇总，并附带本人所在群的群公告命中（本机只保存了部分聊天记录，更早的
 *  要到服务器上找）。报文格式见 docs/协议设计-v2.md 的 10.25 节，与服务端
 *  chatserversrc/GlobalSearchProtocol 互为镜像。
 *
 *  本模块只产出 / 解析 JSON 文本，不触及网络与 UI，只依赖 jsoncpp，便于单元测试，见
 *  flamingoclient/Source/Tests/GlobalSearchProtocolClientTests.cpp。
 *
 *  典型用法：
 *      std::string body = net::GlobalSearchProtocolClient::buildRequest(keywordUtf8, 20, 10);
 *      ...收到应答...
 *      net::GlobalSearchResult result;
 *      net::GlobalSearchProtocolClient::parseResponse(body, result);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//全局搜索应答里一个会话的聊天记录命中汇总。
struct GlobalSearchConversation
{
    int         m_ctype;         //会话类型：1 单聊、2 群聊
    int64_t     m_cid;           //会话 id（本人视角）：单聊为对方 userid，群聊为群 id
    int         m_hitCount;      //该会话里命中的消息条数
    int64_t     m_latestMsgid;   //最近一条命中消息的 msgid
    int64_t     m_latestTime;    //它的时间（Unix 毫秒）
    std::string m_latestText;    //它的文字里关键字附近的一段（UTF-8，前后截掉时带省略号）

    GlobalSearchConversation()
        : m_ctype(1)
        , m_cid(0)
        , m_hitCount(0)
        , m_latestMsgid(0)
        , m_latestTime(0)
    {
    }
};

//全局搜索应答里的一条群公告命中。
struct GlobalSearchAnnouncement
{
    int64_t     m_groupid;       //所属群 id
    int64_t     m_id;            //公告 id
    std::string m_title;         //公告标题（UTF-8）
    std::string m_excerpt;       //正文里关键字附近的一段（UTF-8）
    int64_t     m_publishTime;   //发布时间（Unix 毫秒）

    GlobalSearchAnnouncement()
        : m_groupid(0)
        , m_id(0)
        , m_publishTime(0)
    {
    }
};

//cmd_global_search 应答的解析结果。成功（m_code==0）时各字段有效；失败时只有 m_code / m_msg 有意义。
struct GlobalSearchResult
{
    int         m_code;      //结果码，0 成功，非 0 见 enum error_code
    std::string m_msg;       //结果描述
    std::string m_keyword;   //服务端回显的关键字（已去掉首尾空白）；搜索下拉据此丢弃过期的应答
    std::vector<GlobalSearchConversation> m_conversations;   //会话命中，按最近一条命中倒序
    std::vector<GlobalSearchAnnouncement> m_announcements;   //群公告命中，按发布时间倒序

    GlobalSearchResult()
        : m_code(-1)         //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//全局搜索报文编解码；全部为静态方法。
class GlobalSearchProtocolClient
{
public:
    /**
     *  构造 cmd_global_search 请求 JSON 包体（紧凑 UTF-8）。
     *    keywordUtf8：关键字（调用方已去掉首尾空白、非空）。
     *    limit：最多返回几个会话（服务端缺省 20、上限 50）。
     *    announcementLimit：最多返回几条群公告（服务端缺省 10、上限 30）；0 表示不搜群公告。
     *  @return 请求包体。
     */
    static std::string buildRequest(const std::string& keywordUtf8, int limit, int announcementLimit);

    /**
     *  解析 cmd_global_search 应答 JSON 包体。
     *    jsonbody：应答包体。
     *    out：[出参] 解析结果。
     *  @return 包体是 JSON 对象且带整型 code 时返回 true（失败应答同样返回 true，看 m_code）；否则 false。
     */
    static bool parseResponse(const std::string& jsonbody, GlobalSearchResult& out);
};

} // namespace net
