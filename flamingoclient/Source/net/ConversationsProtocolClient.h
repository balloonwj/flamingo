/**
 *  v2 会话列表报文编解码（客户端侧）：解析 cmd_sync_conversations(1501) 应答，
 *  并构造 cmd_set_conversation(1513) 会话设置（置顶 / 免打扰）请求。
 *
 *  本模块与服务端 chatserversrc/MessageProtocol 互为镜像——服务端构造同步应答 /
 *  解析会话设置请求，客户端解析同步应答 / 构造会话设置请求。纯逻辑，只依赖
 *  jsoncpp，不触及网络与 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/ConversationsProtocolTests.cpp。
 *
 *  会话名称 / 头像不在本应答里——客户端按 m_cid 去通讯录缓存查；最后一条消息
 *  的预览文字 m_digest 由服务端拼好，客户端直接显示。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//一条会话（cmd_sync_conversations 应答里 conversations 数组的一项）。
struct ConversationItem
{
    int         m_ctype;        //会话类型：见 enum conversation_type，1单聊 2群聊
    int64_t     m_cid;          //会话ID：单聊=对端userid，群聊=群id
    int         m_unread;       //未读消息数
    int         m_pinned;       //是否置顶：0否 1是
    int         m_mute;         //是否免打扰：0否 1是
    int64_t     m_lastmsgid;    //最后一条消息的服务器msgid
    int64_t     m_lastFrom;     //最后一条消息的发送方userid
    int         m_lastMsgType;  //最后一条消息内容类型：见 enum message_type
    std::string m_digest;       //最后一条消息的预览文字（UTF-8，服务端拼好，如"[图片]"/正文）
    int64_t     m_timestamp;    //最后一条消息时间（Unix 毫秒）

    ConversationItem()
        : m_ctype(0)
        , m_cid(0)
        , m_unread(0)
        , m_pinned(0)
        , m_mute(0)
        , m_lastmsgid(0)
        , m_lastFrom(0)
        , m_lastMsgType(0)
        , m_timestamp(0)
    {
    }
};

//cmd_conversation_changed_notify(1514) 推送解析结果 —— 会话属性的多端同步。
//
//某一端改了会话的置顶 / 免打扰 / 删除之后，服务端把该会话<u>读回来的完整状态</u>推给这个
//用户的每一个在线端（含发起改动的那一端自己，见服务端 ChatSession::sendToUser）。所以
//本推送带的是"改完之后是什么样"，而不是"这次改了什么"，接收端直接照它覆盖本地状态即可，
//重复收到同一状态也不会出问题。
struct ConversationChangedNotify
{
    int     m_ctype;     //会话类型：见 enum conversation_type，1单聊 2群聊
    int64_t m_cid;       //会话ID：单聊=对端userid，群聊=群id
    int     m_pinned;    //改完之后是否置顶：0否 1是
    int     m_mute;      //改完之后是否免打扰：0否 1是
    int     m_deleted;   //改完之后是否已从会话列表删除：0否 1是

    ConversationChangedNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_pinned(0)
        , m_mute(0)
        , m_deleted(0)
    {
    }
};

//cmd_sync_conversations 应答解析结果。
struct ConversationsResult
{
    int                           m_code;      //结果码，0 成功
    std::string                   m_msg;       //结果描述
    int64_t                       m_synctime;  //服务器同步时间（Unix 毫秒）
    std::vector<ConversationItem> m_items;     //会话列表

    ConversationsResult()
        : m_code(-1)        //-1：尚未解析出有效结果码
        , m_synctime(0)
    {
    }
};

//会话列表报文编解码工具类；全部为静态方法，不可实例化。
class ConversationsProtocolClient
{
public:
    /**
     *  解析 cmd_sync_conversations 应答 JSON 包体。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_synctime / m_items 有效。
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSyncConversationsResponse(const std::string& jsonbody,
                                               ConversationsResult& out);

    /**
     *  构造"会话设置"请求 JSON 包体（cmd_set_conversation=1513）。本轮只用于置顶 /
     *  免打扰；与服务端 MessageProtocol::parseSetConversationRequest 对齐——
     *  pinned / mute 为可选 bool 字段，只有"本次要改"的字段才写入 JSON，未写入的
     *  字段服务端保持原值不变。cid 以字符串编码（避免大整数精度丢失）。
     *  @param ctype     会话类型（见 enum conversation_type，1单聊 2群聊）
     *  @param cid       会话ID（单聊=对端userid，群聊=群id；>0）
     *  @param hasPinned true 表示本次要改置顶，按 pinned 写入；false 则不写 pinned 字段
     *  @param pinned    置顶目标值（hasPinned 为 true 时有效）
     *  @param hasMute   true 表示本次要改免打扰，按 mute 写入；false 则不写 mute 字段
     *  @param mute      免打扰目标值（hasMute 为 true 时有效）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSetConversationRequest(int ctype, int64_t cid,
                                                   bool hasPinned, bool pinned,
                                                   bool hasMute, bool mute);

    /**
     *  构造"删除会话"请求 JSON 包体（复用 cmd_set_conversation=1513 的 deleted 字段）。
     *  与服务端 MessageProtocol::parseSetConversationRequest 对齐——只写 deleted 一个
     *  可选字段，故服务端只更新 t_conversation.f_deleted，置顶 / 免打扰保持原值。
     *
     *  语义是"把会话从最近会话列表移除"，<u>不删除聊天消息本体</u>：t_chatmsg 不动，
     *  历史消息记录窗仍能查到；服务端记下删除时刻的最后消息 msgid 作锚点，之后该
     *  会话再来新消息时会重新出现在列表里。
     *
     *  @param ctype 会话类型（见 enum conversation_type，1单聊 2群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id；>0）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildDeleteConversationRequest(int ctype, int64_t cid);

    /**
     *  解析 cmd_conversation_changed_notify(1514) 推送包体，用于会话属性的多端同步。
     *
     *  服务端把 pinned / mute / deleted 写成 <u>JSON bool</u>（见服务端
     *  MessageProtocol::buildConversationChangedNotify），本函数也接受写成整数 0/1 的
     *  形式，免得日后哪一端改了写法就静默失效。cid 以字符串编码（防大整数精度丢失），
     *  同样兼容整数写法。
     *
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果，仅返回 true 时有效
     *  @return JSON 合法、且 cid 为正数时返回 true；否则 false（调用方应丢弃该推送）
     */
    static bool parseConversationChangedNotify(const std::string& jsonbody,
                                               ConversationChangedNotify& out);

    /**
     *  统计一组会话的未读消息总数 —— 把各会话的 m_unread 逐项相加。供主面板把
     *  "消息"Tab 的角标刷成真实未读总数（而非占位数）。纯函数，便于单元测试。
     *  @param items 会话列表（一般为已过滤后的单聊会话；本函数不关心会话类型，
     *               只对传入项求和，由调用方决定统计范围）
     *  @return 未读消息总数；空列表返回 0
     */
    static int computeTotalUnread(const std::vector<ConversationItem>& items);

    /**
     *  判断一条刚收到的消息推送是否应该让会话未读数 +1。
     *
     *  存在的理由：服务端把群消息推给<u>包括发送者在内</u>的全体群成员，单聊在多端
     *  登录时也会把自己发的消息同步回本端。这类"自己发出、又回推回来"的消息不应
     *  计入未读，否则自己发一条群消息就会给自己点亮一个红点。
     *
     *  服务端生成的系统消息（发布公告、入群欢迎语、退出群聊、转让群主，msgtype 为
     *  message_type_system）同样不计：它们是群里的变动通知，不是有人对你说话。服务端算会话
     *  未读数时也不计它们（MessageManager 的群未读查询），两边口径一致。
     *
     *  @param fromUserId 消息发送方 userid（取自推送的 from 字段；系统消息为 0）
     *  @param selfId     本端登录用户的 userid；为 0 表示尚未登录 / 取不到，
     *                    此时一律不计未读，避免在未登录兜底态下误加
     *  @param msgtype    消息内容类型，取值见 Protocol.h 的 msg_type 一组 message_type_xxx
     *  @return 应计未读返回 true；自己发出的、系统消息、或 selfId 无效时返回 false
     */
    static bool shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId, int msgtype);

private:
    ConversationsProtocolClient() = delete;
    ~ConversationsProtocolClient() = delete;
    ConversationsProtocolClient(const ConversationsProtocolClient&) = delete;
    ConversationsProtocolClient& operator=(const ConversationsProtocolClient&) = delete;
};

} // namespace net
