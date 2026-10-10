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
#include <map>
#include <string>
#include <vector>
#include "GroupProtocol.h"   //GroupFollowList（cmd_sync_conversations 应答的 groupfollows）

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
    int64_t     recalledby;         //撤回者userid（t_chatmsg.f_recalled_by）。0 表示未撤回，或撤回发生在
                                    //2026-10-03 之前、没有记录；群主 / 管理员可撤回别人的群消息，故不一定等于 from
    int         fileackstate;       //文件收取状态，取值见 enum file_ack_state；0=无（非文件消息、
                                    //或接收方还没有任何动作）。随 cmd_sync_messages 回给发送方，
                                    //使其离线期间对方的接收/拒绝在下次上线时也能看到

    MessageRecord()
        : msgid(0), ctype(1), from(0), to(0), msgtype(1)
        , mentionall(false), timestamp(0), recalled(false), recalledby(0), fileackstate(0)
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

//cmd_sync_rewritten_messages（1526）一页最多返回的条数，也是请求缺省 limit 时的取值
//（docs/消息改写离线同步方案.md 3.2）。条数之外还受 RESPONSE_MESSAGES_BUDGET_BYTES 的体积约束
constexpr int REWRITTEN_SYNC_LIMIT_MAX = 200;

//cmd_sync_rewritten_messages 请求：取本人可见范围内改写序号大于 since 的消息
struct SyncRewrittenRequest
{
    int64_t since;   //客户端已同步到的改写序号；首次为 0，缺省或为负时按 0 处理
    int     limit;   //本页最多几条；解析时已夹在 1 ~ REWRITTEN_SYNC_LIMIT_MAX，缺省或不大于 0 时取上限

    SyncRewrittenRequest()
        : since(0), limit(REWRITTEN_SYNC_LIMIT_MAX)
    {
    }
};

//cmd_sync_rewritten_messages 应答里的一项：一条被改写过（或被撤回）的消息及其改写序号
struct RewrittenMessage
{
    MessageRecord record;   //消息本体，已按 cmd_sync_messages 的规则加工（卡片状态、文件收取状态）
    int64_t       rev;      //改写序号（t_chatmsg.f_rev），大于 0

    RewrittenMessage()
        : rev(0)
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
    int         notifymode;    //消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode；没有状态行时为 0（允许提醒）
    int64_t     lastmsgid;     //最后一条消息的msgid
    int64_t     lastfrom;      //最后一条消息的发送方userid
    int         lastmsgtype;   //最后一条消息的内容类型
    std::string lastcontent;   //最后一条消息 content 的原始JSON文本（用于生成摘要）
    int64_t     lasttimestamp; //最后一条消息的时间，Unix毫秒
    bool        lastrecalled;  //最后一条消息是否已被撤回；为真时应答不下发原文，摘要改为撤回提示
    int64_t     lastrecalledby; //最后一条消息的撤回者userid；0 表示未撤回或撤回者未知（见 MessageRecord::recalledby）
    bool        unreadmark;    //本人是否把该会话手动标为未读（t_conversation.f_unread_mark，docs/会话标为未读方案.md）

    ConversationBrief()
        : ctype(1), cid(0), unread(0), pinned(false), notifymode(0)
        , lastmsgid(0), lastfrom(0), lastmsgtype(1), lasttimestamp(0), lastrecalled(false)
        , lastrecalledby(0), unreadmark(false)
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
    bool    has_notifymode; //本次是否更新消息提醒模式（请求里出现了整数型 notifymode 字段）
    int     notifymode;   //消息提醒模式目标值，取值见 Protocol.h 的 enum conversation_notify_mode；
                          //解析时只取原值，是否合法由调用方用 MessageProtocol::isValidNotifyMode 判断
    bool    has_deleted;  //本次是否更新删除标记
    bool    deleted;      //删除标记目标值
    bool    has_unreadmark; //本次是否更新「手动标为未读」（请求里出现了布尔型 unreadmark 字段）
    bool    unreadmark;     //「手动标为未读」目标值：true 标为未读，false 清除（docs/会话标为未读方案.md）

    SetConversationRequest()
        : ctype(1), cid(0), has_pinned(false), pinned(false)
        , has_notifymode(false), notifymode(0), has_deleted(false), deleted(false)
        , has_unreadmark(false), unreadmark(false)
    {
    }
};

//cmd_set_notify_modes 单次请求可带的条目数上限。
//
//这些条目在一个事务里写入 t_conversation，条数一多事务就长，占住数据库的时间也长；
//批量管理窗口超过本值时由客户端拆成几批发送（docs/消息提醒模式与消息盒子方案.md §5.4）。
//超限的请求整单以 error_code_bad_request 拒绝。
constexpr size_t NOTIFY_MODE_BATCH_MAX = 200;

//提醒模式为「特别关注」（notify_mode_starred）的会话数上限，单聊与群聊合计。
//
//特别关注的会话一来消息就弹出聊天窗，数量多了等于没有关注，还会频繁打断用户；上限同时
//防止刷表。超出时以 error_code_starred_limit 拒绝。沿用并入前独立的特别关注功能的上限 50
//（原 StarredProtocol.h 的 kMaxStarredCount），并入后改为联系人与群合计（2026-10-03 确认）。
constexpr int NOTIFY_MODE_STARRED_MAX = 50;

//一个会话的消息提醒模式。用在三处：cmd_sync_conversations 应答顶层的 notifymodes 数组
//（本人全部非默认模式的会话）、cmd_set_notify_modes 的请求条目、
//cmd_notify_modes_changed_notify 推送的条目。
struct NotifyModeItem
{
    int     ctype;  //会话类型：1 单聊 2 群聊
    int64_t cid;    //会话ID：单聊为对端userid，群聊为群id
    int     mode;   //消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode

    NotifyModeItem()
        : ctype(0), cid(0), mode(0)
    {
    }
};

//cmd_set_notify_modes 请求里的一条。比 NotifyModeItem 多一个 wellformed：
//某一条缺字段或类型不对时只把这一条判为非法、回它自己的错误码，不影响同一批里的其它条目。
struct SetNotifyModeEntry
{
    NotifyModeItem item;        //解析出的会话与目标模式；wellformed 为 false 时 ctype / cid 为能解析出的部分，其余为 0
    bool           wellformed;  //ctype、cid、mode 三个字段是否都齐全且类型正确（cid 还须为正）

    SetNotifyModeEntry()
        : wellformed(false)
    {
    }
};

//cmd_set_notify_modes 应答里的一条结果，与请求条目一一对应、顺序相同
struct NotifyModeResult
{
    int     ctype;  //会话类型，原样回带请求条目里的值
    int64_t cid;    //会话ID，原样回带请求条目里的值
    int     code;   //该条的结果码：0 成功；其余取值见 Protocol.h 的 enum error_code

    NotifyModeResult()
        : ctype(0), cid(0), code(0)
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

//cmd_forward_message 单次请求可带的源消息条数上限。
//
//逐条转发时 ChatSession::onForwardMessage 在 EventLoop 线程上串行地逐条落库，理由与
//下文 MASS_SEND_MAX_TARGETS 相同：条数一多，该 loop 上的所有客户端连接都被卡住。
//合并转发虽然只落一条，但条数决定了这条消息 content 的体积。故在解析阶段就把超限
//的请求拒掉（回 error_code_bad_request）。
//
//本值必须与客户端 flamingoclient/Source/net/MessageProtocolClient.h 的
//kForwardMaxMessages（转发方案第 1a 步新增）保持一致，改动时两处要一起改。
constexpr size_t FORWARD_MAX_MESSAGES = 100;

//合并转发标题的长度上限，单位为<u>字符</u>（UTF-8 码点），不是字节。超长的标题在解析
//阶段截断到本值，不拒绝请求：标题由客户端按会话名拼出（如「<群名>的聊天记录」），
//群名很长时超限是正常情况，不应因此转发失败。
constexpr size_t FORWARD_TITLE_MAX_CHARS = 64;

//以下几项体积上限的单位换算：1 KiB = 1024 字节
constexpr size_t MSG_BYTES_PER_KIB = 1024;

//合并记录（msgtype=15）content 的体积上限，单位字节。
//
//客户端解码 chatserver 的帧时，要求原始长度不超过 MAX_BODY_CHATSERVER（1 MiB），超了就
//断开连接。一条合并记录会被推送、被同步、还可能被再次合并转发，
//其 content 必须明显小于 1 MiB，给消息体的其余字段与同一帧里的其他消息留出余量。
//超限的合并转发回 error_code_msg_too_large。普通长度的文字 100 条打包后约 30 KB，
//本值留有充分余量。
constexpr size_t FORWARD_MERGED_CONTENT_MAX_BYTES = 256 * MSG_BYTES_PER_KIB;

//普通消息 content 与 quote 合计的体积上限，单位字节。适用于 cmd_send_message、cmd_mass_send，
//以及逐条转发时复制出的每一条。按服务端重新序列化后的紧凑 JSON 计量：jsoncpp 把非 ASCII
//字符写成 \uXXXX，一个汉字计 6 字节，与它在推送帧里实际占的字节数一致。
//
//理由与合并记录上限相同：一条消息会被推送、被同步，推送帧或同步页
//超过客户端单帧上限 MAX_BODY_CHATSERVER（1 MiB）时对方会断线；而同步页按体积截断时单条
//消息至少保留一条，因此单条消息本身必须明显小于 1 MiB。取与合并记录相同的 256 KiB，两种
//消息口径一致。超限回 error_code_msg_too_large，不落库、不推送。客户端发送前按同一口径检查。
constexpr size_t MESSAGE_CONTENT_MAX_BYTES = 256 * MSG_BYTES_PER_KIB;

//语音消息（message_type_voice）content 的取值范围，见 docs/语音消息方案.md 第 5 节。
//时长按整秒向上取整：最短 1 秒（客户端不到 1 秒不发送），最长 120 秒（录音上限 2 分钟）。
constexpr int VOICE_DURATION_MIN_SECONDS = 1;
constexpr int VOICE_DURATION_MAX_SECONDS = 120;
//语音文件大小上限，单位字节：Ogg Opus 24 kbps 录满 2 分钟约 360 KB，取 1 MiB 留出余量
constexpr int64_t VOICE_FILE_MAX_BYTES = 1024 * static_cast<int64_t>(MSG_BYTES_PER_KIB);
//波形 wave 字段解码后的字节数上限：客户端放 40 个音量值，取 64 留出余量
constexpr size_t VOICE_WAVE_MAX_BYTES = 64;

//cmd_forward_message 应答里 messages 数组的体积预算，单位字节。
//逐条转发 100 条长消息时，全部消息体放进一个应答会超过单帧上限。按顺序放入，放不下的
//那一条及其后的只在 msgids 里出现、不带消息体，客户端对这部分改用同步拉取。
constexpr size_t FORWARD_RESPONSE_MESSAGES_BUDGET_BYTES = 512 * MSG_BYTES_PER_KIB;

//cmd_sync_messages、cmd_search_messages、cmd_get_pinned_messages 应答里消息体的体积预算，
//单位字节。一页最多 100 条，几条长消息就能让整页超过 1 MiB 的单帧上限；超过预算时由
//trimMessagesToBudget 截断并把 hasmore 置为真（Pin 列表靠 total 表达截断）。
//预算低于 1 MiB，是给应答的其余字段留出余量。
constexpr size_t RESPONSE_MESSAGES_BUDGET_BYTES = 768 * MSG_BYTES_PER_KIB;

//cmd_forward_message 请求
struct ForwardMessageRequest
{
    std::string clientmsgid;      //本次转发操作的客户端ID（逐条转发时按 "clientmsgid-序号" 派生每条）
    int         toctype;          //目标会话类型
    int64_t     tocid;            //目标会话ID
    bool        merge;            //true=合并转发为一条 msgtype=15；false=逐条转发
    std::string title;            //合并转发的标题；解析时已经 normalizeForwardTitle 处理（截断、空串取缺省值）
    std::vector<int64_t> msgids;  //被转发的消息msgid列表，条数不超过 FORWARD_MAX_MESSAGES

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

//cmd_delete_messages_by_time 请求：按时间范围清理本人的聊天记录。
//两端都包含；取值为解析出的原始值，截断与合法性校验由 NormalizeDeleteRange 负责。
struct DeleteMessagesByTimeRequest
{
    int64_t begintime;  //区间开始，Unix毫秒；0 表示从最早开始
    int64_t endtime;    //区间结束，Unix毫秒

    DeleteMessagesByTimeRequest()
        : begintime(0), endtime(0)
    {
    }
};

//cmd_clear_conversation 请求：按会话清空本人的聊天记录。
struct ClearConversationRequest
{
    int     ctype;   //会话类型：1 单聊 2 群聊
    int64_t cid;     //会话ID：单聊为对端userid，群聊为群id

    ClearConversationRequest()
        : ctype(0), cid(0)
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
     *  从消息 content 里取出所引用文件的 fileid。
     *
     *  用途：发送方引用某个 fileid 发出消息，说明该文件已上传完成（客户端总是传完才发
     *  消息），服务端据此把文件记录标记为已完成，使后续同内容的上传能命中秒传。
     *
     *  只处理会携带文件的消息类型（图片、文件、语音、短视频、大表情、截图），其余类型
     *  （如文本）不解析 JSON、直接返回 false。
     *  @param msgtype 消息类型，取值见 enum message_type
     *  @param content content 对象的紧凑 JSON 文本（即 SendMessageRequest::content）
     *  @param fileid  [出参] 取到时回填文件ID
     *  @return content 顶层有字符串字段 fileid 且其值为正整数时返回 true；消息类型不带
     *          文件、JSON 不合法、字段缺失或取值不是正整数时返回 false
     */
    static bool extractContentFileId(int msgtype, const std::string& content, int64_t& fileid);

    /**
     *  把一段 fileid 文本解析成文件ID。协议约定 64 位 ID 以十进制字符串传输；头像的
     *  customface 字段存的也是这种文本。
     *  @param text   fileid 文本
     *  @param fileid [出参] 解析成功时回填
     *  @return 整串都是十进制数字、不超过 19 位且值为正时返回 true；空串、含非数字
     *          字符（如 "img_8f3c"、"117abc"、"-5"）或值为 0 时返回 false
     */
    static bool parseFileIdText(const std::string& text, int64_t& fileid);

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

    //会话最后一条消息已被撤回时下发的摘要。不含撤回者的名字（服务端不知道查看者怎么称呼他），
    //PC 客户端据 lastmsg.recalled 与撤回者（recalledby，缺省时取 from）改写成「你撤回了一条消息」/「某某撤回了一条消息」，
    //不认识 recalled 的老客户端照样能显示这一句
    static const char* const kRecalledDigest;

    /**
     *  构造 cmd_sync_conversations 应答 JSON：
     *  { code, msg, synctime, conversations:[ {ctype,cid,unread,pinned,notifymode,
     *    lastmsgid,lastmsg:{from,msgtype,digest,timestamp[,recalled][,recalledby][,tipkind,tipargs][,digestarg]}} ],
     *    notifymodes:[ {ctype,cid,mode} ] }。
     *  notifymodes 列出本人全部非默认模式的会话，包括还没有消息、因而不在 conversations 里的
     *  群与联系人，客户端以它为准建立模式表（docs/消息提醒模式与消息盒子方案.md §5.2）；
     *  没有时输出空数组。
     *  最后一条消息已被撤回时 lastmsg 带 recalled: true，digest 为 kRecalledDigest，不下发原文
     *  （2026-10-03 之前按原文生成摘要，重新登录后会话列表又露出撤回的内容）；
     *  撤回者有记录时再带 recalledby（群主 / 管理员撤回别人的消息时与 from 不同）。
     *  多语言第 5 期起 lastmsg 另带客户端在本地生成摘要所需的参数：最后一条是带结构化字段的系统提示时带
     *  tipkind / tipargs（与消息 content 里的相同），是接龙卡片或文件共享卡片时带 digestarg（标题 / 共享名）；
     *  已撤回、或没有这些信息时不带。老客户端只用 digest。
     *  @param synctime    服务器同步时刻，Unix毫秒（客户端用作下次同步水位）
     *  @param items       会话摘要列表
     *  @param notifyModes 本人全部非默认模式的会话（MessageManager::getNotifyModes 的结果）；
     *                     默认空，供只关心会话摘要的单元测试省略不写
     *  @param groupFollows 本人在各群的关注名单（GroupFollowManager::getFollowsAll 的结果），输出为
     *                     顶层 groupfollows 数组，名单为空的群不输出（docs/消息提醒模式与消息盒子方案.md §12.4）；
     *                     默认空
     */
    static std::string buildSyncConversationsResponse(int code, const std::string& msg,
                                                      int64_t synctime,
                                                      const std::vector<ConversationBrief>& items,
                                                      const std::vector<NotifyModeItem>& notifyModes
                                                          = std::vector<NotifyModeItem>(),
                                                      const std::vector<GroupFollowList>& groupFollows
                                                          = std::vector<GroupFollowList>());

    //buildSyncMessagesResponse 的 peerReadMsgid 取这个值时表示「不输出对方已读位点」。
    //cmd_search_messages 复用同一个构造函数，搜索结果与已读回执无关，走的就是这个默认值。
    static const int64_t kPeerReadMsgidOmitted = -1;

    /**
     *  构造 cmd_sync_messages 应答 JSON：
     *  { code, msg, ctype, cid, hasmore, [peerreadmsgid,] messages:[ 完整消息体... ] }。
     *  messages 各元素结构与 cmd_message_notify 包体一致。
     *  @param hasmore       锚点方向上是否还有更多消息
     *  @param peerReadMsgid 单聊时对方已读到的最大消息 msgid（取对方那一行 t_conversation
     *                       的 f_readmsgid，从没读过为 0），以字符串编码输出为 peerreadmsgid，
     *                       供发送方在重开聊天窗、或离线期间对方读过之后显示正确的已读状态。
     *                       群聊按协议不做已读回执，不输出；取 kPeerReadMsgidOmitted 时也不输出。
     *  @param anchorReq     本次同步请求（借用，不持有），用于原样回带锚点：翻历史模式输出
     *                       beforemsgid（0 输出为 "0"），补齐模式输出 aftermsgid，二者只出现其一。
     *                       客户端据此算出本页覆盖了哪段 msgid（见 docs/清理聊天记录方案.md §7.2）。
     *                       传 NULL 时两者都不输出 —— 复用本结构的搜索应答就是这样。
     */
    static std::string buildSyncMessagesResponse(int code, const std::string& msg,
                                                 int ctype, int64_t cid, bool hasmore,
                                                 const std::vector<MessageRecord>& messages,
                                                 int64_t peerReadMsgid = kPeerReadMsgidOmitted,
                                                 const SyncMessagesRequest* anchorReq = NULL);

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
     *  与系统消息（message_type_system）保证带 "text"。会话摘要与敏感词检查
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
     *  解析 cmd_set_conversation 请求；pinned / notifymode / deleted 为可选字段，
     *  仅出现且类型正确（pinned、deleted 为布尔，notifymode 为整数）的字段对应的 has_* 置 true，
     *  类型不对的字段视同没有出现。2026-10-03 之前的布尔字段 mute 已删除，不再识别。
     *  cid 缺失/非正返回 false。notifymode 的取值是否合法不在这里判断，见 isValidNotifyMode。
     */
    static bool parseSetConversationRequest(const std::string& json, SetConversationRequest& req);

    /**
     *  判断某种会话能否设为某种消息提醒模式。
     *  @param ctype 会话类型：1 单聊 2 群聊，其它值一律不合法
     *  @param mode  目标模式，取值见 Protocol.h 的 enum conversation_notify_mode；
     *               超出 0~5 不合法；单聊不能设为屏蔽（notify_mode_block）与
     *               只提醒 @我和关注的人（notify_mode_mention_follow）
     *  @return 合法返回 true
     */
    static bool isValidNotifyMode(int ctype, int mode);

    /**
     *  构造 cmd_conversation_changed_notify 推送 JSON：携带会话的完整当前状态
     *  { ctype, cid, pinned, notifymode, deleted, unreadmark }。
     *  @param notifymode 消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
     *  @param unreadmark 本人是否把该会话手动标为未读（docs/会话标为未读方案.md）
     */
    static std::string buildConversationChangedNotify(int ctype, int64_t cid,
                                                      bool pinned, int notifymode, bool deleted,
                                                      bool unreadmark);

    /**
     *  解析 cmd_set_notify_modes 请求 { items:[ {ctype, cid, mode} ] }。
     *  整单失败（返回 false）只有三种情况：JSON 非法或不是对象、items 缺失或不是数组、
     *  items 为空或超过 NOTIFY_MODE_BATCH_MAX 条。某一条缺字段、类型不对或 cid 非正时，
     *  只把这一条的 wellformed 置 false，照常解析其余条目。模式取值是否合法不在这里判断。
     *  @param json    请求包体
     *  @param entries [出参] 与 items 一一对应、顺序相同的条目；返回 false 时内容无意义
     *  @return 整单可以继续处理返回 true
     */
    static bool parseSetNotifyModesRequest(const std::string& json,
                                           std::vector<SetNotifyModeEntry>& entries);

    /**
     *  构造 cmd_set_notify_modes 应答 JSON：
     *  { code, msg, results:[ {ctype, cid, code} ] }，cid 以字符串编码。
     *  @param code    整单结果码，0 表示请求已被受理（各条是否成功看 results）
     *  @param results 逐条结果，与请求条目一一对应；整单失败时传空
     */
    static std::string buildSetNotifyModesResponse(int code, const std::string& msg,
                                                   const std::vector<NotifyModeResult>& results);

    /**
     *  构造 cmd_notify_modes_changed_notify 推送 JSON：{ items:[ {ctype, cid, mode} ] }，
     *  cid 以字符串编码。只应传入改成功的条目。
     */
    static std::string buildNotifyModesChangedNotify(const std::vector<NotifyModeItem>& items);

    /**
     *  解析 cmd_search_messages 请求；cid 或 keyword 缺失返回 false。
     */
    static bool parseSearchMessagesRequest(const std::string& json, SearchMessagesRequest& req);

    /**
     *  解析 cmd_forward_message 请求。
     *  clientmsgid / tocid / msgids 缺失或为空、或 msgids 超过 FORWARD_MAX_MESSAGES 条时
     *  返回 false；title 字段经 normalizeForwardTitle 处理后写入 req.title。
     */
    static bool parseForwardMessageRequest(const std::string& json, ForwardMessageRequest& req);

    /**
     *  规范化合并转发的标题：空串取缺省值「聊天记录」；超过 FORWARD_TITLE_MAX_CHARS 个
     *  字符时截断，截断点落在多字节字符中间时整个字符一起舍去，不会留下半个字符。
     *  @param raw 客户端传来的标题（UTF-8），可以为空
     *  @return 规范化后的标题，非空
     */
    static std::string normalizeForwardTitle(const std::string& raw);

    /**
     *  构造逐条转发时新消息的 content。
     *  文字消息（msgtype=1）去掉 content 里的 at 与 quote，其余字段（text、font 等）原样保留：
     *  at 里的 uid 是源会话的成员，带进目标会话会让同一个人误收到「有人@我」提醒；
     *  quote 指向源会话里的消息，在目标会话里无法定位。
     *  非文字消息、以及解析失败的 content 原样返回。合并转发不经过本函数，子消息保持原样。
     *  @param msgtype 源消息的内容类型
     *  @param content 源消息的 content JSON 文本（即 MessageRecord::content）
     *  @return 新消息的 content JSON 文本
     */
    static std::string buildForwardedContent(int msgtype, const std::string& content);

    /**
     *  消息 content 是否 @ 了所有人（content.at.all 为非 0 整数或 true）。PC 客户端把 @全体 写在这里而不是
     *  请求顶层的 mentionall，群消息的 @全体 权限校验须两处都认。
     *  @param content 消息的 content JSON 文本（客户端原样带来的那一份，不是 buildStoredContent 的外壳）
     *  @return @ 了所有人返回 true；缺 at、at 不是对象、all 为 0 / false、content 不是 JSON 对象时返回 false
     */
    static bool contentMentionsAll(const std::string& content);

    /**
     *  去掉消息 content 里的 at（群发助手投递之前调用）：@ 的文字位置只对一个会话有意义，群发到多个会话时没有用处，
     *  也免得借群发绕过群的 @全体 权限。没有 at、或 content 不是 JSON 对象时不改动（不重新序列化）。
     *  @param content [入参 / 出参] 消息的 content JSON 文本，有 at 时原地改写
     *  @return 去掉了 at 返回 true
     */
    static bool stripContentMentions(std::string& content);

    /**
     *  从一条待发出的消息里取出需要做敏感词检查的文字，追加到 out（规则见 docs/敏感词方案.md
     *  第 6.3.1 节）。发送消息、群发助手、参与接龙之外的消息类检查点都用它；转发时传入的是
     *  实际要新建的内容（逐条转发为 buildForwardedContent 的结果，合并转发为 buildMergedContent
     *  的结果），而不是源消息原文，因此查的正是对方会收到的内容。
     *  各类型取的字段：
     *    - 文字（1）：content.text；
     *    - 文件（3）：content.name；
     *    - 投票（10）：title 与 options 中的每个字符串；接龙（11）：title 与 note；待办（12）：title；
     *    - 合并记录（15）：title，再对 messages 的每条子消息按本规则递归，子消息上的
     *      quote.digest 一并取；子消息的 fromname 是发送人昵称，不取；
     *    - 其余类型：content 是结构化字段或从通讯录选来的名称，不取。
     *  任何类型的 content 里若带 quote 对象，取其 digest；参数 quote 里的 digest 也取。引用里的
     *  name 是被引用人的名字，不取。
     *  取的顺序：content 的正文字段 → content.quote.digest → 参数 quote 的 digest；合并记录先取
     *  标题，再按子消息顺序逐条取。空串、非字符串字段不取；content 或 quote 不是合法的 JSON
     *  对象时忽略该项。递归深度不另设上限，理由见实现处的注释。
     *  @param msgtype 消息内容类型（message_type 的取值）
     *  @param content content 对象的 JSON 文本
     *  @param quote   请求顶层 quote 对象的 JSON 文本；没有时传空串
     *  @param out     [出参] 取出的文字（UTF-8）追加到末尾；原有内容保留
     */
    static void collectSensitiveTexts(int msgtype, const std::string& content, const std::string& quote,
                                      std::vector<std::string>& out);

    /**
     *  判断合并记录的 content 是否超过 FORWARD_MERGED_CONTENT_MAX_BYTES。
     *  @param mergedContent buildMergedContent 构造出的 content JSON 文本
     *  @return 超过上限返回 true，调用方应回 error_code_msg_too_large
     */
    static bool isMergedContentTooLarge(const std::string& mergedContent);

    /**
     *  判断一条普通消息的 content 与 quote 合计是否超过 MESSAGE_CONTENT_MAX_BYTES。
     *  @param content content 对象的紧凑 JSON 文本：解析请求时重新序列化的结果，
     *                 或逐条转发时 buildForwardedContent 的结果
     *  @param quote   quote 对象的紧凑 JSON 文本；没有引用时传空串
     *  @return 超过上限返回 true，调用方应回 error_code_msg_too_large，不落库、不推送
     */
    static bool isMessageContentTooLarge(const std::string& content, const std::string& quote);

    /**
     *  判断某种消息类型是否允许客户端直接操作：经 cmd_send_message / cmd_mass_send 发送、经
     *  cmd_pin_message 置顶或取消置顶、经 cmd_recall_message 撤回。
     *  文件共享卡片（message_type_fileshare）只能由服务端在发起共享或追加成员时生成，并且只给部分成员看：
     *  客户端自行发送等于伪造一张指向别人共享的卡片；置顶会让可见范围之外的人在 Pin 列表里看到它；
     *  撤回与「取消共享」语义重叠，统一走取消共享（见 docs/文件共享方案.md 6.1）。
     *  屏幕共享卡片（message_type_screenshare）同样只由服务端在发起共享时生成：客户端自行发送等于伪造一张「加入」卡片，
     *  转发到别的会话没有意义（房间只对原会话的成员开放），置顶与撤回也都没有对应的语义。
     *  入群邀请卡片（message_type_group_invite）只由服务端在「邀请入群需被邀请人同意」开启时生成：客户端自行发送等于
     *  伪造一张入群邀请，撤回、置顶也都没有对应的语义（docs/邀请入群方案.md §4.3）。
     *  通话记录（message_type_rtc，目前只有群通话卡片）只由服务端在发起群通话时生成、结束时改写：客户端自行发送等于
     *  伪造一张「加入」卡片（docs/协议设计-v2.md 21.5）。其余类型一律允许。
     *  @param msgtype 消息内容类型
     *  @return 允许返回 true；两种共享卡片、入群邀请卡片与通话记录返回 false，调用方应回 error_code_permission_denied
     */
    static bool isClientOperableMsgType(int msgtype);

    /**
     *  客户端能否发送（cmd_send_message、群发助手）这种消息类型：在 isClientOperableMsgType 之外再排除系统提示
     *  （message_type_system）。系统提示只能由服务端生成，客户端自行发送等于伪造一行「某某被移出群聊」之类的提示
     *  （bugs.md BUG-124）。
     *  @param msgtype 消息内容类型
     *  @return 允许返回 true
     */
    static bool isClientSendableMsgType(int msgtype);

    /**
     *  校验语音消息（message_type_voice）的 content，见 docs/语音消息方案.md 第 5 节。
     *  语音文件走聊天文件的上传通道，上传时不限制大小，因此由发送消息这一步检查字段与取值范围。
     *  @param content content 对象的紧凑 JSON 文本
     *  @return 以下条件全部满足时返回 true：fileid 是正整数的十进制字符串；md5 是非空字符串；
     *          duration 是整数且在 VOICE_DURATION_MIN_SECONDS~VOICE_DURATION_MAX_SECONDS 之间；
     *          size 是整数且在 1~VOICE_FILE_MAX_BYTES 之间；wave 可以没有，有则必须是标准 Base64
     *          且解码后不超过 VOICE_WAVE_MAX_BYTES 字节。JSON 不合法或不是对象时返回 false
     */
    static bool isValidVoiceContent(const std::string& content);

    /**
     *  发送消息（cmd_send_message、cmd_mass_send）之前按消息类型检查 content。目前只检查语音，
     *  其余类型的内容由各自的处理流程负责（如接龙卡片的规范化），这里一律放行。
     *  @param msgtype 消息内容类型，取值见 enum message_type
     *  @param content content 对象的紧凑 JSON 文本
     *  @return 合格返回 error_code_ok；不合格返回 error_code_bad_request，调用方应回执该错误码，
     *          不落库、不推送
     */
    static int checkSendContent(int msgtype, const std::string& content);

    /**
     *  判断一条消息能否作为 cmd_forward_message 的转发源（逐条转发与合并转发相同）：已撤回的不能转发；
     *  类型不允许客户端操作的（见 isClientOperableMsgType）也不能转发，否则只给部分成员看的共享卡片
     *  会被扩散到可见范围之外。本人能否看到该消息所在的会话不在这里判断，由调用方另行判断。
     *  @param src 转发源消息
     *  @return 能转发返回 true
     */
    static bool isForwardableSource(const MessageRecord& src);

    /**
     *  按体积截断一页消息，使其消息体序列化后的累计字节数不超过 budgetBytes。
     *  从锚点一侧开始逐条累计，放不下的那一条及其后的全部删去，保证留下的是连续的一段，
     *  客户端按留下的消息计算翻页锚点与本地覆盖区间时不会跳过或误删消息。
     *  至少保留一条：单条就超过预算时仍保留它，否则客户端永远翻不过这一条。
     *  @param messages    [入参/出参] 一页消息；截断后只剩留下的那一段，顺序不变
     *  @param cid         消息体里 cid 字段的取值，与构造应答时一致（用于准确计算体积）
     *  @param keepTail    true 表示锚点在末尾、保留末尾一段（msgid 升序的翻历史与搜索应答）；
     *                     false 表示锚点在开头、保留开头一段（补齐模式、按时间倒序的 Pin 列表）
     *  @param budgetBytes 体积预算，单位字节；正式调用传 RESPONSE_MESSAGES_BUDGET_BYTES
     *  @return 删去的条数；0 表示没有截断
     */
    static size_t trimMessagesToBudget(std::vector<MessageRecord>& messages, int64_t cid,
                                       bool keepTail, size_t budgetBytes);

    /**
     *  构造 cmd_sync_messages 的应答，并保证消息体总量不超过体积预算（2026-10-04）。
     *  结果与「先 trimMessagesToBudget、截断了就把 hasmore 置真、再 buildSyncMessagesResponse」完全相同，
     *  只是先按整页组装：整个应答都没超过预算时，各条消息体的体积之和必然更小（应答里还有分隔逗号与
     *  外层字段），trimMessagesToBudget 一条也不会删，直接返回这一份；超过时才逐条精确截断并重新组装。
     *  原先的做法每条消息都要构造、序列化两遍（截断时量一遍体积、组装时再来一遍），一万字的长消息上
     *  这一遍并不便宜；绝大多数页面远小于预算，现在每条只构造、序列化一遍。
     *  @param hasMore     [入参/出参] 是否还有更多；发生截断时置为 true
     *  @param messages    [入参/出参] 一页消息；发生截断时只剩留下的那一段
     *  @param keepTail    截断时保留哪一端，含义同 trimMessagesToBudget
     *  @param budgetBytes 体积预算，单位字节；正式调用传 RESPONSE_MESSAGES_BUDGET_BYTES
     *  @param trimmed     [出参] 截掉的条数；0 表示没有截断
     *  其余参数同 buildSyncMessagesResponse。
     *  @return 应答 JSON 文本
     */
    static std::string buildSyncMessagesResponseWithinBudget(int code, const std::string& msg,
                                                             int ctype, int64_t cid, bool& hasMore,
                                                             std::vector<MessageRecord>& messages,
                                                             int64_t peerReadMsgid,
                                                             const SyncMessagesRequest* anchorReq,
                                                             bool keepTail, size_t budgetBytes,
                                                             size_t& trimmed);

    /**
     *  构造 cmd_get_pinned_messages 的成功应答，并保证消息体总量不超过体积预算（2026-10-04）。
     *  结果与「先 trimMessagesToBudget（保留开头）、把 pins 截到与 messages 一样长、再
     *  buildGetPinnedMessagesResponse」完全相同，做法与 buildSyncMessagesResponseWithinBudget 一样：
     *  先按整页组装，超过预算才逐条截断后重新组装。每一项是消息体再加一个 pin 子对象，同样比单独的
     *  消息体大，整页没超过预算即说明截断不会删任何一条。
     *  @param messages    [入参/出参] Pin 列表里的消息（按时间倒序）；发生截断时只剩开头一段
     *  @param pins        [入参/出参] 与 messages 一一对应的 Pin 元信息；截到不比 messages 长
     *  @param budgetBytes 体积预算，单位字节；正式调用传 RESPONSE_MESSAGES_BUDGET_BYTES
     *  其余参数同 buildGetPinnedMessagesResponse。
     *  @return 应答 JSON 文本
     */
    static std::string buildGetPinnedMessagesResponseWithinBudget(int code, const std::string& msg,
                                                                  int ctype, int64_t cid, int64_t total,
                                                                  std::vector<MessageRecord>& messages,
                                                                  std::vector<PinRecord>& pins,
                                                                  size_t budgetBytes);

    /**
     *  解析 cmd_sync_rewritten_messages（1526）请求。
     *  since 按协议约定以字符串编码，缺省、不是字符串或为负时取 0；limit 缺省、不是整数或不大于 0 时取
     *  REWRITTEN_SYNC_LIMIT_MAX，大于上限时截到上限（与 cmd_sync_messages 缺省取默认值的做法一致）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return 包体是合法 JSON 对象返回 true；否则返回 false
     */
    static bool parseSyncRewrittenRequest(const std::string& json, SyncRewrittenRequest& req);

    /**
     *  一条消息在某个用户视角下的会话 id：群聊为群 id；单聊为对方 userid（本人发出的取接收方，
     *  收到的取发送方）。cmd_sync_rewritten_messages 一页里混有多个会话，每一项的 cid 由它算出。
     *  @param rec          消息
     *  @param viewerUserid 查看者 userid
     *  @return 会话 id
     */
    static int64_t conversationIdForViewer(const MessageRecord& rec, int64_t viewerUserid);

    /**
     *  构造 cmd_sync_rewritten_messages 的应答，并保证各项消息体的总量不超过体积预算。
     *  每一项就是 cmd_sync_messages 的消息条目（同一个 messageBodyValue，cid 取查看者视角），另加字符串编码的
     *  rev。超过预算时从序号小的一端起逐条累计，放不下的那一项及其后的全部删去、more 置为 true；至少保留一项，
     *  单项就超过预算时仍保留它，避免客户端卡在同一个序号上。应答的 rev 取最后保留的那一项的序号，一项都没有
     *  时取 since，客户端下一页以它为 since。
     *  @param code         结果码
     *  @param msg          结果描述
     *  @param viewerUserid 查看者 userid，用于算每一项的 cid
     *  @param since        请求里的 since，空页时作为应答的 rev
     *  @param items        [入参/出参] 本页条目，须按 rev 升序；发生截断时只剩开头一段
     *  @param more         [入参/出参] 后面是否还有；发生截断时置为 true
     *  @param currev       服务端当前的最大改写序号
     *  @param budgetBytes  体积预算，单位字节；正式调用传 RESPONSE_MESSAGES_BUDGET_BYTES
     *  @param trimmed      [出参] 截掉的条数；0 表示没有截断
     *  @return 应答 JSON 文本
     */
    static std::string buildSyncRewrittenResponseWithinBudget(int code, const std::string& msg,
                                                              int64_t viewerUserid, int64_t since,
                                                              std::vector<RewrittenMessage>& items,
                                                              bool& more, int64_t currev,
                                                              size_t budgetBytes, size_t& trimmed);

    /**
     *  构造 cmd_forward_message 的成功应答 JSON：
     *  { code, msg, clientmsgid, msgids:[新建消息ID...], messages:[新建消息的完整消息体...] }。
     *  messages 的元素结构与 cmd_message_notify 包体相同，供转发人在本端直接显示与落库 ——
     *  目标为单聊时服务端只把新消息推给对方，转发人除应答之外拿不到这些消息。
     *  msgids 与 messages 条数、顺序一致；保留 msgids 是为了兼容只认它的老客户端。
     *  @param code        错误码，0 表示成功
     *  @param msg         说明文字（英文）
     *  @param clientmsgid 请求里的 clientmsgid，原样回带
     *  @param created     本次新建的消息，按新建顺序排列；可以为空
     *  @param cid         消息体里 cid 字段的取值：转发人视角的会话ID，即请求里的 tocid
     */
    static std::string buildForwardMessageResponse(int code, const std::string& msg,
                                                   const std::string& clientmsgid,
                                                   const std::vector<MessageRecord>& created,
                                                   int64_t cid);

    /**
     *  构造"合并转发"消息（msgtype=15）的 content JSON：
     *  { title, messages:[子消息体...] }，子消息体结构与 cmd_message_notify 包体相同，
     *  另外在 fromNames 查得到名字时多一个 fromname 字段（转发当时发送者的昵称）。
     *  @param title     合并转发标题
     *  @param subs      被合并的源消息列表
     *  @param fromNames 发送者 userid 到昵称的映射；查不到的发送者不写 fromname，由接收方
     *                   按本地通讯录解析。缺省为空映射，即一个都不写
     */
    static std::string buildMergedContent(const std::string& title,
                                          const std::vector<MessageRecord>& subs,
                                          const std::map<int64_t, std::string>& fromNames =
                                              std::map<int64_t, std::string>());

    /**
     *  解析 cmd_delete_message 请求（请求体为 { "msgid": "..." }）。
     *  @param msgid [出参] 要删除的消息msgid
     *  @return msgid 缺失或非正返回 false
     */
    static bool parseDeleteMessageRequest(const std::string& json, int64_t& msgid);

    /**
     *  解析 cmd_delete_messages_by_time 请求（请求体为 { "begintime": 毫秒, "endtime": 毫秒 }）。
     *  两个字段都必须出现，且都是非负整数（JSON 数字；字符串不接受）。本函数只做格式校验，
     *  "结束不早于开始""按服务端当前时刻截断"由 NormalizeDeleteRange 负责。
     *  @param json 请求体
     *  @param req  [出参] 解析结果
     *  @return 格式合法返回 true；缺字段、类型不对、值为负、JSON 非法或不是对象返回 false
     */
    static bool parseDeleteMessagesByTimeRequest(const std::string& json,
                                                 DeleteMessagesByTimeRequest& req);

    /**
     *  构造 cmd_delete_messages_by_time 应答 JSON：{ code, msg, begintime, endtime }。
     *  @param begintime 实际生效的区间开始（Unix毫秒），即截断校验之后的值
     *  @param endtime   实际生效的区间结束（Unix毫秒），即截断校验之后的值
     */
    static std::string buildDeleteMessagesByTimeResponse(int code, const std::string& msg,
                                                         int64_t begintime, int64_t endtime);

    /**
     *  构造单条形式的 cmd_messages_deleted_notify 推送 JSON：{ ctype, cid, msgids:[...] }。
     *  由 cmd_delete_message 成功后推给删除者本人的全部在线设备。
     *  @param ctype  会话类型：1 单聊 2 群聊
     *  @param cid    删除者视角的会话ID（单聊为对方userid，群聊为群id），以字符串输出
     *  @param msgids 被删除的消息msgid，各以字符串输出
     */
    static std::string buildMessagesDeletedNotify(int ctype, int64_t cid,
                                                  const std::vector<int64_t>& msgids);

    /**
     *  构造区间形式的 cmd_messages_deleted_notify 推送 JSON：{ begintime, endtime }，不带 msgids。
     *  由 cmd_delete_messages_by_time 成功后推给清理者本人的全部在线设备。
     *  @param begintime 实际生效的区间开始（Unix毫秒）
     *  @param endtime   实际生效的区间结束（Unix毫秒）
     */
    static std::string buildMessagesDeletedRangeNotify(int64_t begintime, int64_t endtime);

    /**
     *  解析 cmd_clear_conversation 请求：{ ctype, cid }。
     *  ctype 必须出现且只能是 1 或 2（不默认成单聊，免得清错会话）；cid 按字符串或数字解析，须为正。
     *  @param json 请求体
     *  @param req  [出参] 解析结果
     *  @return 格式合法返回 true；JSON 非法、不是对象、缺字段或取值不合法返回 false
     */
    static bool parseClearConversationRequest(const std::string& json, ClearConversationRequest& req);

    /**
     *  构造 cmd_clear_conversation 应答 JSON：{ code, msg, ctype, cid, clearedmsgid }。
     *  @param ctype        会话类型
     *  @param cid          会话ID，以字符串输出
     *  @param clearedmsgid 生效的清空锚点（msgid 不大于它的消息对本人隐藏），以字符串输出
     */
    static std::string buildClearConversationResponse(int code, const std::string& msg,
                                                      int ctype, int64_t cid, int64_t clearedmsgid);

    /**
     *  构造清空形式的 cmd_messages_deleted_notify 推送 JSON：{ ctype, cid, clearedmsgid }，
     *  不带 msgids 与时间。由 cmd_clear_conversation 成功后推给清空者本人的全部在线设备，各设备据此
     *  删掉本地库里该会话 msgid 不大于锚点的消息。
     *  @param ctype        会话类型
     *  @param cid          清空者视角的会话ID，以字符串输出
     *  @param clearedmsgid 生效的清空锚点，以字符串输出
     */
    static std::string buildMessagesClearedNotify(int ctype, int64_t cid, int64_t clearedmsgid);

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
