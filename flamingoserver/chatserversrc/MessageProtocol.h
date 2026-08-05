/**
 *  v2 会话与消息相关报文（cmd_send_message / cmd_message_notify /
 *  cmd_message_received_ack）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_messageprotocol.cpp）。content / quote 一律按不透明
 *  JSON 子树处理，不解析其内部结构。消息落库、在线推送等均在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//cmd_send_message 请求
struct SendMessageRequest
{
    std::string clientmsgid;        //客户端消息ID（UUID），用于去重与回执回填
    int         ctype;              //会话类型：1 单聊 2 群聊
    int64_t     cid;                //会话ID：单聊为对端userid
    int         msgtype;            //内容类型：1 文本 2 图片 …17 系统
    std::string content;            //content 对象的原始紧凑JSON文本（不透明）
    std::vector<int64_t> mentions;  //被@的userid列表
    bool        mentionall;         //是否@所有人
    std::string quote;              //quote 对象的原始紧凑JSON文本；空串表示无引用

    SendMessageRequest()
        : ctype(1), cid(0), msgtype(1), mentionall(false)
    {
    }
};

//一条完整消息体：cmd_message_notify 推送与 cmd_sync_messages 应答共用
struct MessageRecord
{
    int64_t     msgid;              //服务器消息ID
    std::string clientmsgid;        //客户端消息ID
    int         ctype;              //会话类型：1 单聊 2 群聊
    int64_t     from;               //发送方userid
    int64_t     to;                 //接收方：单聊为对端userid，群聊为群id
    int         msgtype;            //内容类型
    std::string content;            //content 对象的原始紧凑JSON文本
    std::vector<int64_t> mentions;  //被@的userid列表
    bool        mentionall;         //是否@所有人
    std::string quote;              //quote 对象的原始紧凑JSON文本；空串表示无引用
    int64_t     timestamp;          //消息时间，Unix毫秒
    bool        recalled;           //是否已撤回
    int         fileackstate;       //文件收取状态，取值见 enum file_ack_state；0=无（非文件消息、
                                    //或接收方还没有任何动作）。随 cmd_sync_messages 回给发送方，
                                    //使其离线期间对方的接收/拒绝在下次上线时也能看到

    MessageRecord()
        : msgid(0), ctype(1), from(0), to(0), msgtype(1)
        , mentionall(false), timestamp(0), recalled(false), fileackstate(0)
    {
    }
};

//cmd_sync_messages 请求：按锚点拉取某会话的一段消息
struct SyncMessagesRequest
{
    int         ctype;        //会话类型：1 单聊 2 群聊
    int64_t     cid;          //会话ID：单聊为对端userid
    bool        isAfterMode;  //true=补齐模式(取 aftermsgid 之后)，false=翻历史(取 beforemsgid 之前)
    int64_t     beforemsgid;  //翻历史锚点：拉取此msgid之前的消息；0 表示从最新开始
    int64_t     aftermsgid;   //补齐锚点：拉取此msgid之后的消息
    int         limit;        //本次最多返回条数

    SyncMessagesRequest()
        : ctype(1), cid(0), isAfterMode(false)
        , beforemsgid(0), aftermsgid(0), limit(0)
    {
    }
};

//cmd_sync_conversations 应答里的单条会话摘要
struct ConversationBrief
{
    int         ctype;         //会话类型：1 单聊 2 群聊
    int64_t     cid;           //会话ID：单聊为对端userid
    int         unread;        //未读消息数
    bool        pinned;        //是否置顶
    bool        mute;          //是否免打扰
    int64_t     lastmsgid;     //最后一条消息的msgid
    int64_t     lastfrom;      //最后一条消息的发送方userid
    int         lastmsgtype;   //最后一条消息的内容类型
    std::string lastcontent;   //最后一条消息 content 的原始JSON文本（用于生成摘要）
    int64_t     lasttimestamp; //最后一条消息的时间，Unix毫秒

    ConversationBrief()
        : ctype(1), cid(0), unread(0), pinned(false), mute(false)
        , lastmsgid(0), lastfrom(0), lastmsgtype(1), lasttimestamp(0)
    {
    }
};

//cmd_mark_read 请求
struct MarkReadRequest
{
    int     ctype;      //会话类型：1 单聊 2 群聊
    int64_t cid;        //会话ID：单聊为对端userid
    int64_t readmsgid;  //已读到的最大消息msgid

    MarkReadRequest()
        : ctype(1), cid(0), readmsgid(0)
    {
    }
};

//cmd_recall_message 请求
struct RecallMessageRequest
{
    int     ctype;  //会话类型：1 单聊 2 群聊
    int64_t cid;    //会话ID：单聊为对端userid
    int64_t msgid;  //被撤回的消息msgid

    RecallMessageRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//t_message_pin 的一行：某条消息被谁、在什么时候钉住的。
//与 MessageRecord 一样放在本文件而不是 MessagePinManager.h —— 它既是数据库行、
//也是 cmd_get_pinned_messages 应答里 pin 子对象的来源，放这里两边都能用，
//也避免 MessagePinManager.h 与本文件互相包含。
struct PinRecord
{
    int64_t msgid;       //被Pin的消息msgid
    int64_t operatorId;  //执行Pin的用户userid；取消Pin的权限判断依据
    int64_t pinTime;     //Pin时间，Unix毫秒

    PinRecord()
        : msgid(0), operatorId(0), pinTime(0)
    {
    }
};

//cmd_pin_message 请求：Pin / 取消 Pin 一条消息。一条命令兼管两个方向，由 pin 字段区分。
struct PinMessageRequest
{
    int     ctype;  //会话类型：1 单聊 2 群聊
    int64_t cid;    //会话ID：单聊为对端userid，群聊为群id
    int64_t msgid;  //被Pin / 取消Pin的消息msgid
    int     pin;    //目标状态：1 钉住、0 取消

    PinMessageRequest()
        : ctype(1), cid(0), msgid(0), pin(0)
    {
    }
};

//cmd_get_pinned_messages 请求：拉取某会话当前被Pin的消息（带完整消息体）。
//客户端的"消息记录"窗按本地库查 Pin 分类，而本机未必同步过那些老消息，
//故要有这条命令把消息体补齐，详见 docs/消息Pin接入方案.md §4.3。
struct GetPinnedMessagesRequest
{
    int     ctype;  //会话类型：1 单聊 2 群聊
    int64_t cid;    //会话ID：单聊为对端userid，群聊为群id

    GetPinnedMessagesRequest()
        : ctype(1), cid(0)
    {
    }
};

//cmd_typing_notify 请求（同时也是服务器转发给对端的推送体）
struct TypingNotifyRequest
{
    int     ctype;  //会话类型：1 单聊 2 群聊
    int64_t cid;    //会话ID：单聊为对端userid
    int     state;  //输入状态：1 输入中 0 停止

    TypingNotifyRequest()
        : ctype(1), cid(0), state(0)
    {
    }
};

//cmd_file_transfer_ack 请求（接收方上报）/ 服务器转推发送方的推送体，两向同结构。
//发送方据此在自己的文件气泡上显示对方收到什么程度了。
struct FileTransferAckRequest
{
    int     ctype;  //会话类型：1 单聊（当前只支持单聊文件消息）
    int64_t cid;    //会话ID：上报时为发送方userid（对端）；转推时为接收方userid（收件方视角）
    int64_t msgid;  //被收取的那条文件消息的服务器msgid
    int     state;  //收取状态，取值见 enum file_ack_state

    FileTransferAckRequest()
        : ctype(1), cid(0), msgid(0), state(0)
    {
    }
};

//cmd_set_conversation 请求；has_* 标记该属性本次是否需要更新
struct SetConversationRequest
{
    int     ctype;        //会话类型
    int64_t cid;          //会话ID：单聊为对端userid
    bool    has_pinned;   //本次是否更新置顶
    bool    pinned;       //置顶目标值
    bool    has_mute;     //本次是否更新免打扰
    bool    mute;         //免打扰目标值
    bool    has_deleted;  //本次是否更新删除标记
    bool    deleted;      //删除标记目标值

    SetConversationRequest()
        : ctype(1), cid(0), has_pinned(false), pinned(false)
        , has_mute(false), mute(false), has_deleted(false), deleted(false)
    {
    }
};

//cmd_search_messages 请求
struct SearchMessagesRequest
{
    int         ctype;        //会话类型
    int64_t     cid;          //会话ID：单聊为对端userid
    std::string keyword;      //搜索关键字
    int64_t     beforemsgid;  //翻页锚点：拉取此msgid之前的命中；0 表示从最新开始
    int         limit;        //本次最多返回条数

    SearchMessagesRequest()
        : ctype(1), cid(0), beforemsgid(0), limit(0)
    {
    }
};

//cmd_forward_message 请求
struct ForwardMessageRequest
{
    std::string clientmsgid;      //本次转发操作的客户端ID（逐条转发时按 "clientmsgid-序号" 派生每条）
    int         toctype;          //目标会话类型
    int64_t     tocid;            //目标会话ID
    bool        merge;            //true=合并转发为一条 msgtype=15；false=逐条转发
    std::string title;            //合并转发的标题；空串则用默认
    std::vector<int64_t> msgids;  //被转发的消息msgid列表

    ForwardMessageRequest()
        : toctype(1), tocid(0), merge(false)
    {
    }
};

//cmd_mass_send 单次群发的目标数上限。
//
//ChatSession::onMassSend 是在 EventLoop 线程上<u>串行</u>逐个目标落库的，而每个目标
//都要经 MessageManager::saveMessage 新建一条数据库连接、同步 INSERT、再同步查一次
//自增主键。目标一多，这段时间该 loop 上的所有客户端连接都被卡住，故在解析阶段就把
//超限的请求拒掉（回 error_code_bad_request），不让它进入投递循环。
//
//本值必须与客户端 flamingoclient/Source/BalloonMain/MassSendModel.h 的
//kMassSendMaxTargets 保持一致，改动时两处要一起改，否则会出现"客户端允许发、
//服务端却拒绝"的不一致。
constexpr size_t MASS_SEND_MAX_TARGETS = 50;

//cmd_mass_send 的单个发送目标
struct MassSendTarget
{
    int     ctype;  //会话类型
    int64_t cid;    //会话ID：单聊为对端userid

    MassSendTarget()
        : ctype(1), cid(0)
    {
    }
};

//cmd_mass_send 请求
struct MassSendRequest
{
    std::string clientmsgid;             //本次群发的客户端ID（按 "clientmsgid-序号" 派生每条）
    int         msgtype;                 //内容类型
    std::string content;                 //content 对象的原始JSON文本
    std::vector<MassSendTarget> targets; //群发目标列表

    MassSendRequest()
        : msgtype(1)
    {
    }
};

//cmd_mass_send 单个目标的发送结果
struct MassSendResult
{
    int64_t cid;    //目标会话ID
    int64_t msgid;  //发送成功的消息msgid；0 表示该目标被跳过（不存在/被拉黑/失败）

    MassSendResult()
        : cid(0), msgid(0)
    {
    }
};

//会话与消息报文编解码工具类；全部为静态方法，不可实例化
class MessageProtocol
{
public:
    /**
     *  解析 cmd_send_message 请求。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果
     *  @return clientmsgid 非空、cid 为正、msgtype 为正、content 为对象时返回 true
     */
    static bool parseSendMessageRequest(const std::string& json, SendMessageRequest& req);

    /**
     *  解析 cmd_message_received_ack 请求；msgid 缺失或非正返回 false。
     *  @param msgid [出参] 客户端确认已送达的服务器消息ID
     */
    static bool parseReceivedAck(const std::string& json, int64_t& msgid);

    /**
     *  构造 cmd_send_message 应答（发送回执）JSON：
     *  { code, msg, clientmsgid, msgid, timestamp }。
     *  @param msgid     服务器分配的消息ID（以字符串形式落入JSON）
     *  @param timestamp 服务器盖的消息时间，Unix毫秒
     */
    static std::string buildSendMessageResponse(int code, const std::string& msg,
                                                const std::string& clientmsgid,
                                                int64_t msgid, int64_t timestamp);

    /**
     *  构造 cmd_message_notify 推送 JSON（完整消息体）。
     *  @param rec 消息记录
     *  @param cid 收件方视角的会话ID（单聊推给收件人时取发送方userid）
     */
    static std::string buildMessageNotify(const MessageRecord& rec, int64_t cid);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  把 content/mentions/mentionall/quote 打包成一段 JSON 文本，
     *  作为 t_chatmsg.f_content 的落库内容。
     */
    static std::string buildStoredContent(const std::string& content,
                                          const std::vector<int64_t>& mentions,
                                          bool mentionall, const std::string& quote);

    /**
     *  解包 t_chatmsg.f_content，还原 content/mentions/mentionall/quote。
     *  @return blob 为合法 JSON 对象时返回 true
     */
    static bool parseStoredContent(const std::string& blob, std::string& content,
                                   std::vector<int64_t>& mentions,
                                   bool& mentionall, std::string& quote);

    /**
     *  解析 cmd_sync_messages 请求。
     *  请求形如 { ctype, cid, beforemsgid, limit } 或 { ctype, cid, aftermsgid }：
     *  出现 aftermsgid 即为补齐模式，否则为翻历史模式（beforemsgid 缺省按 0）。
     *  @return cid 为正时返回 true
     */
    static bool parseSyncMessagesRequest(const std::string& json, SyncMessagesRequest& req);

    /**
     *  构造 cmd_sync_conversations 应答 JSON：
     *  { code, msg, synctime, conversations:[ {ctype,cid,unread,pinned,mute,
     *    lastmsgid,lastmsg:{from,msgtype,digest,timestamp}} ] }。
     *  @param synctime 服务器同步时刻，Unix毫秒（客户端用作下次同步水位）
     */
    static std::string buildSyncConversationsResponse(int code, const std::string& msg,
                                                      int64_t synctime,
                                                      const std::vector<ConversationBrief>& items);

    /**
     *  构造 cmd_sync_messages 应答 JSON：
     *  { code, msg, ctype, cid, hasmore, messages:[ 完整消息体... ] }。
     *  messages 各元素结构与 cmd_message_notify 包体一致。
     *  @param hasmore 锚点方向上是否还有更多消息
     */
    static std::string buildSyncMessagesResponse(int code, const std::string& msg,
                                                 int ctype, int64_t cid, bool hasmore,
                                                 const std::vector<MessageRecord>& messages);

    /**
     *  按消息类型生成会话列表用的内容摘要（digest）。
     *  文本/系统消息取其正文；图片、文件等非文本类型返回占位文案（如"[图片]"）。
     *  @param msgtype 内容类型：1 文本 …17 系统
     *  @param content content 对象的原始JSON文本
     */
    static std::string buildMessageDigest(int msgtype, const std::string& content);

    /**
     *  取出消息 content 里的纯文本正文（content 的 "text" 字段）。
     *
     *  content 对服务端而言本是不透明 JSON，只有文本类消息（message_type_text）
     *  与系统消息（message_type_system）保证带 "text"。群敏感词过滤与会话摘要
     *  都要看这一段文本，故抽出来共用。
     *
     *  @param content 消息体 JSON 文本（UTF-8）
     *  @return "text" 字段的值；content 不是对象、或没有 "text"、或它不是字符串
     *          时返回空串（调用方据此跳过 —— 拿不到正文就谈不上过滤）
     */
    static std::string extractText(const std::string& content);

    /**
     *  构造一条纯文本消息的 content —— 即 { "text": "..." } 的紧凑 JSON。
     *
     *  服务器自己生成文本类消息（目前是入群欢迎语这类 message_type_system）时用。
     *  会话层不该为了拼这么一个对象而直接依赖 jsoncpp，故收在本模块里。
     *
     *  @param text 正文（UTF-8），会被正确转义
     *  @return 紧凑格式的 content JSON 文本
     */
    static std::string buildTextContent(const std::string& text);

    /**
     *  解析 cmd_mark_read 请求；cid 或 readmsgid 缺失/非正返回 false。
     */
    static bool parseMarkReadRequest(const std::string& json, MarkReadRequest& req);

    /**
     *  解析 cmd_recall_message 请求；cid 或 msgid 缺失/非正返回 false。
     */
    static bool parseRecallMessageRequest(const std::string& json, RecallMessageRequest& req);

    /**
     *  构造 cmd_message_read_notify 推送 JSON（推给被读消息的发送方）。
     *  @param cid       收件方视角的会话ID（推给发送方时取"已读方"userid）
     *  @param readmsgid 对方已读到的最大消息msgid
     *  @param readcount 已读人数（单聊恒为 1）
     */
    static std::string buildMessageReadNotify(int ctype, int64_t cid,
                                              int64_t readmsgid, int readcount);

    /**
     *  构造 cmd_message_recalled_notify 推送 JSON。
     *  @param cid        收件方视角的会话ID
     *  @param msgid      被撤回的消息msgid
     *  @param operatorId 执行撤回的用户userid
     */
    static std::string buildMessageRecalledNotify(int ctype, int64_t cid,
                                                  int64_t msgid, int64_t operatorId);

    /**
     *  解析 cmd_pin_message 请求。
     *
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果
     *  @return 字段齐备且取值合法返回 true；cid 或 msgid 缺失 / 非正、
     *          pin 不是 0 或 1 时一律返回 false —— pin 直接决定落库方向，
     *          不能让越界值混进去当成"取消"处理。
     */
    static bool parsePinMessageRequest(const std::string& json, PinMessageRequest& req);

    /**
     *  构造 cmd_pin_message 应答 JSON：{ code, msg, ctype, cid, msgid, pin }。
     *
     *  ctype / cid / msgid / pin 原样回带，便于客户端在多条请求在途时对上号，
     *  并给出准确的失败提示（是哪一条钉失败了）。
     *
     *  @param code  结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
     *  @param msg   结果描述
     *  @param ctype 会话类型
     *  @param cid   会话ID（请求里的原值）
     *  @param msgid 消息msgid
     *  @param pin   本次操作的目标状态：1 钉住、0 取消
     */
    static std::string buildPinMessageResponse(int code, const std::string& msg,
                                               int ctype, int64_t cid,
                                               int64_t msgid, int pin);

    /**
     *  构造 cmd_message_pinned_notify 推送 JSON：
     *  { ctype, cid, msgid, pin, operator, pintime }。
     *
     *  @param ctype      会话类型
     *  @param cid        <u>收件方视角</u>的会话ID。单聊要给双方各推一条、cid 各不相同
     *                    （给对端推时取操作者的userid，给操作者推时取对端的userid），
     *                    与 buildMessageRecalledNotify 同一口径
     *  @param msgid      被Pin / 取消Pin的消息msgid
     *  @param pin        新状态：1 已钉住、0 已取消
     *  @param operatorId 执行本次操作的用户userid
     *  @param pinTime    Pin时间，Unix毫秒；取消Pin时传 0
     */
    static std::string buildMessagePinnedNotify(int ctype, int64_t cid, int64_t msgid,
                                                int pin, int64_t operatorId,
                                                int64_t pinTime);

    /**
     *  解析 cmd_get_pinned_messages 请求；cid 缺失 / 非正返回 false。
     */
    static bool parseGetPinnedMessagesRequest(const std::string& json,
                                              GetPinnedMessagesRequest& req);

    /**
     *  构造 cmd_get_pinned_messages 应答 JSON：
     *  { code, msg, ctype, cid, total, msgs:[ <完整消息体> + pin 子对象 ... ] }。
     *
     *  消息体与 cmd_sync_messages 应答里的元素同结构，额外挂一个 pin 子对象
     *  { operator, pintime } 说明这条是谁在什么时候钉的。
     *
     *  @param code     结果码，0 成功
     *  @param msg      结果描述
     *  @param ctype    会话类型
     *  @param cid      <u>请求方视角</u>的会话ID，原样回带
     *  @param total    <u>不受单次返回条数约束</u>的真实总数。它可能大于 msgs 的长度，
     *                  客户端据此判断发生了截断，进而决定本地对账的时间下界 ——
     *                  少了这个字段，超出 PIN_FETCH_LIMIT 的老 Pin 会在客户端被误清
     *  @param messages 本次返回的消息（最新在前）
     *  @param pins     与 messages <u>一一对应</u>的 Pin 记录（操作者与 Pin 时间）；
     *                  长度须与 messages 相同，短了的部分按 operator=0 / pintime=0 输出
     */
    static std::string buildGetPinnedMessagesResponse(int code, const std::string& msg,
                                                      int ctype, int64_t cid, int64_t total,
                                                      const std::vector<MessageRecord>& messages,
                                                      const std::vector<PinRecord>& pins);

    /**
     *  解析 cmd_typing_notify 请求；cid 缺失/非正返回 false。
     */
    static bool parseTypingNotifyRequest(const std::string& json, TypingNotifyRequest& req);

    /**
     *  构造 cmd_typing_notify 转发给对端的推送 JSON：{ ctype, cid, state }。
     *  @param cid 收件方视角的会话ID（取正在输入一方的userid）
     */
    static std::string buildTypingNotify(int ctype, int64_t cid, int state);

    /**
     *  解析 cmd_file_transfer_ack 请求（接收方上报"我把这个文件收到什么程度了"）。
     *  msgid 非正、或 state 不在 enum file_ack_state 范围内一律返回 false ——
     *  状态值直接落库并回显给发送方，不能让越界值混进去。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果
     *  @return 字段齐备且取值合法返回 true
     */
    static bool parseFileTransferAckRequest(const std::string& json, FileTransferAckRequest& req);

    /**
     *  构造 cmd_file_transfer_ack 转推给发送方的推送 JSON：{ ctype, cid, msgid, state }。
     *  @param ctype 会话类型（当前恒为单聊）
     *  @param cid   收件方（即原文件的发送方）视角的会话ID，取上报方的userid
     *  @param msgid 被收取的文件消息msgid
     *  @param state 收取状态，取值见 enum file_ack_state
     *  @return 紧凑 JSON 文本
     */
    static std::string buildFileTransferAckNotify(int ctype, int64_t cid,
                                                  int64_t msgid, int state);

    /**
     *  解析 cmd_set_conversation 请求；pinned/mute/deleted 为可选字段，
     *  仅出现的字段对应的 has_* 置 true。cid 缺失/非正返回 false。
     */
    static bool parseSetConversationRequest(const std::string& json, SetConversationRequest& req);

    /**
     *  构造 cmd_conversation_changed_notify 推送 JSON：携带会话的完整当前状态。
     */
    static std::string buildConversationChangedNotify(int ctype, int64_t cid,
                                                      bool pinned, bool mute, bool deleted);

    /**
     *  解析 cmd_search_messages 请求；cid 或 keyword 缺失返回 false。
     */
    static bool parseSearchMessagesRequest(const std::string& json, SearchMessagesRequest& req);

    /**
     *  解析 cmd_forward_message 请求；clientmsgid/tocid/msgids 缺失或为空返回 false。
     */
    static bool parseForwardMessageRequest(const std::string& json, ForwardMessageRequest& req);

    /**
     *  构造 cmd_forward_message 应答 JSON：{ code, msg, msgids:[新建消息ID...] }。
     */
    static std::string buildForwardMessageResponse(int code, const std::string& msg,
                                                   const std::vector<int64_t>& msgids);

    /**
     *  构造"合并转发"消息（msgtype=15）的 content JSON：{ title, messages:[子消息体...] }。
     *  @param title 合并转发标题
     *  @param subs  被合并的源消息列表
     */
    static std::string buildMergedContent(const std::string& title,
                                          const std::vector<MessageRecord>& subs);

    /**
     *  解析 cmd_delete_message 请求（请求体为 { "msgid": "..." }）。
     *  @param msgid [出参] 要删除的消息msgid
     *  @return msgid 缺失或非正返回 false
     */
    static bool parseDeleteMessageRequest(const std::string& json, int64_t& msgid);

    /**
     *  解析 cmd_mass_send 请求；clientmsgid/content/targets 缺失或为空返回 false，
     *  targets 条数超过 MASS_SEND_MAX_TARGETS 同样返回 false（调用方据此回
     *  error_code_bad_request）。
     */
    static bool parseMassSendRequest(const std::string& json, MassSendRequest& req);

    /**
     *  构造 cmd_mass_send 应答 JSON：{ code, msg, results:[{cid,msgid}...] }。
     */
    static std::string buildMassSendResponse(int code, const std::string& msg,
                                             const std::vector<MassSendResult>& results);

private:
    MessageProtocol() = delete;
    ~MessageProtocol() = delete;
    MessageProtocol(const MessageProtocol&) = delete;
    MessageProtocol& operator=(const MessageProtocol&) = delete;
};
