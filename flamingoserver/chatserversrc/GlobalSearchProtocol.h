/**
 *  全局搜索（cmd_global_search 1904）的报文编解码，GlobalSearchProtocol.h
 *
 *  在本人能看到的全部聊天记录里按关键字搜索、按会话汇总命中，并附带本人所在群的群公告命中。
 *  PC 客户端搜索下拉的「消息」标签末尾「搜索服务器上的聊天记录」发出本请求（本机只保存了部分
 *  聊天记录，更早的要到服务端上找）。报文格式见 docs/协议设计-v2.md 的「全局搜索」一节。
 *  本文件只放纯函数，不碰数据库；查询在 MessageManager::globalSearchConversations 与
 *  GroupManager::searchAnnouncements，会话层在 ChatSession::onGlobalSearch。
 *
 *  典型用法：
 *      GlobalSearchRequest req;
 *      if (!GlobalSearchProtocol::parseRequest(body, req)) { ...回 bad request... }
 *      ...查库得到 convs、anns...
 *      send(cmd_global_search, seq, GlobalSearchProtocol::buildResponse(0, "ok", req.keyword, convs, anns));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// 关键字最长多少字节（UTF-8，去掉首尾空白之后）。搜索框里一般不会输入这么长，超过的按非法请求处理。
const int GLOBAL_SEARCH_KEYWORD_MAX_BYTES = 128;
// 应答里最多列多少个会话：请求不带 limit 时取默认值，超过上限按上限处理。
const int GLOBAL_SEARCH_DEFAULT_LIMIT = 20;
const int GLOBAL_SEARCH_LIMIT_MAX = 50;
// 应答里最多列多少条群公告：请求不带 announcementlimit 时取默认值，超过上限按上限处理；传 0 表示不搜群公告。
const int GLOBAL_SEARCH_ANNOUNCEMENT_DEFAULT_LIMIT = 10;
const int GLOBAL_SEARCH_ANNOUNCEMENT_LIMIT_MAX = 30;
// 每条命中带回的摘录最多多少字节（UTF-8）。摘录取关键字附近的一段（MakeSearchExcerpt），客户端
// 结果行只显示一行，这个长度足够。
const size_t GLOBAL_SEARCH_EXCERPT_MAX_BYTES = 240;

/**
 *  cmd_global_search 请求。
 */
struct GlobalSearchRequest
{
    std::string keyword;             // 关键字（UTF-8，已去掉首尾空白，非空）
    int         limit;               // 最多返回几个会话，已夹到 [1, GLOBAL_SEARCH_LIMIT_MAX]
    int         announcementlimit;   // 最多返回几条群公告，已夹到 [0, GLOBAL_SEARCH_ANNOUNCEMENT_LIMIT_MAX]；0 表示不搜

    GlobalSearchRequest()
        : limit(GLOBAL_SEARCH_DEFAULT_LIMIT)
        , announcementlimit(GLOBAL_SEARCH_ANNOUNCEMENT_DEFAULT_LIMIT)
    {
    }
};

/**
 *  一个会话里的聊天记录命中汇总。
 */
struct GlobalSearchConversationHit
{
    int         ctype;         // 会话类型：1 单聊、2 群聊
    int64_t     cid;           // 会话 id（本人视角）：单聊为对方 userid，群聊为群 id
    int         hitcount;      // 命中的消息条数
    int64_t     latestmsgid;   // 最近一条命中消息的 msgid
    int64_t     latesttime;    // 它的时间（Unix 毫秒）
    std::string latesttext;    // 它的搜索文本中关键字附近的摘录（UTF-8）

    GlobalSearchConversationHit()
        : ctype(1), cid(0), hitcount(0), latestmsgid(0), latesttime(0)
    {
    }
};

/**
 *  一条群公告命中。
 */
struct GlobalSearchAnnouncementHit
{
    int64_t     groupid;       // 所属群 id
    int64_t     id;            // 公告 id
    std::string title;         // 公告标题
    std::string excerpt;       // 正文中关键字附近的摘录；关键字只出现在标题里时为正文开头
    int64_t     publishtime;   // 发布时间（Unix 毫秒）

    GlobalSearchAnnouncementHit()
        : groupid(0), id(0), publishtime(0)
    {
    }
};

/**
 *  GlobalSearchProtocol —— cmd_global_search 的请求解析与应答构造（纯静态函数）。
 */
class GlobalSearchProtocol
{
public:
    /**
     *  解析请求：{"keyword":"...","limit":20,"announcementlimit":10}。keyword 必填，去掉首尾空白后
     *  不能为空、不能超过 GLOBAL_SEARCH_KEYWORD_MAX_BYTES 字节；limit 与 announcementlimit 可省略，
     *  非法值夹到合理区间。
     *    json：请求体（UTF-8 JSON）。
     *    req：[出参] 解析结果。
     *  @return 合法返回 true；不是 JSON 对象、缺 keyword 或关键字为空、过长时返回 false。
     */
    static bool parseRequest(const std::string& json, GlobalSearchRequest& req);

    /**
     *  构造应答。code 非 0 时只带 code 与 msg。
     *    code / msg：错误码与说明。
     *    keyword：回显请求里的关键字，客户端据此丢弃过期的应答。
     *    convs：会话命中，按最近一条命中的 msgid 倒序。
     *    anns：群公告命中，按发布时间倒序。
     *  @return 应答 JSON 文本；64 位 id 编码为字符串。
     */
    static std::string buildResponse(int code, const std::string& msg, const std::string& keyword,
                                     const std::vector<GlobalSearchConversationHit>& convs,
                                     const std::vector<GlobalSearchAnnouncementHit>& anns);
};
