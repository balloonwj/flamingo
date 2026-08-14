/**
 *  v2 单聊消息报文编解码（客户端侧）：构造 cmd_send_message(1503) 发送请求
 *  JSON、解析其发送回执 JSON、解析 cmd_message_notify(1504) 推送 JSON。
 *
 *  本模块与服务端 chatserversrc/MessageProtocol 互为镜像——服务端解析请求
 *  /构造应答与推送，客户端构造请求/解析应答与推送。本模块是纯逻辑，只依赖
 *  jsoncpp，不触及网络与 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/MessageProtocolTests.cpp。
 *
 *  本轮覆盖"实时收发文本 + 图片 + 文件 + 群聊@提醒"：发送文本 / 图片 / 文件请求、
 *  发送回执、收到文本 / 图片 / 文件推送、以及文本消息里的 @ 片段（content.at）；
 *  语音、引用、撤回等暂不处理（留作后续轮次）。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// @ 片段的结构体（MentionSpan）定义在这里 —— 它是<u>不依赖 ATL / balloonui / 任何
// UI 对象</u>的纯 std 头，描述的又正是 content.at 的线上格式，故协议层直接复用，
// 避免两边各定义一份结构相同的类型再来回拷贝。
#include "../BalloonChatWnd/GroupMentionModel.h"

namespace net {

//文本消息的字体设置。随 content.font 子对象在收发双方之间传递，使对端与历史
//记录看到与发送方相同的字体效果。字体族固定微软雅黑，本结构只承载可变属性。
//m_valid==false 时收发两侧都不写 / 不读 content.font，按默认外观渲染（向后兼容
//旧消息）。颜色用 0xRRGGBB（R 在高位，<u>非</u> Win32 COLORREF 的 0x00BBGGRR），
//收发与 JSON 都用这个口径，UI 侧自行与 COLORREF 互转。
struct TextFont
{
    bool     m_valid;      //是否携带字体设置（false=按默认外观，不写/不读 content.font）
    int      m_size;       //字号（像素，对应 CreateFontW 的字高绝对值）
    bool     m_bold;       //粗体
    bool     m_italic;     //斜体
    bool     m_underline;  //下划线
    bool     m_strike;     //删除线
    uint32_t m_colorRgb;   //文字颜色 0xRRGGBB

    TextFont()
        : m_valid(false)
        , m_size(14)
        , m_bold(false)
        , m_italic(false)
        , m_underline(false)
        , m_strike(false)
        , m_colorRgb(0x1A1A1A)
    {
    }
};

//发送回执（cmd_send_message=1503 的应答）解析结果。
//发送成功（code==0）时 msgid/timestamp 有效；失败时仅 code/msg 有效。
struct SendMessageResponse
{
    int         m_code;         //结果码，0 成功，非 0 见 enum error_code（如对端不存在/被拉黑）
    std::string m_msg;          //结果描述（成功为 "ok"，失败为诊断信息）
    std::string m_clientmsgid;  //客户端消息ID，用于把回执对回到本端那条"发送中"气泡
    int64_t     m_msgid;        //服务器分配的消息ID（全局自增），仅 code==0 时有效
    int64_t     m_timestamp;    //服务器时间（Unix 毫秒），仅 code==0 时有效

    SendMessageResponse()
        : m_code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , m_msgid(0)
        , m_timestamp(0)
    {
    }
};

//收到的新消息（cmd_message_notify=1504 推送）解析结果。本轮支持文本 / 图片 / 文件：
//msgtype==message_type_text 时 m_text 为正文；msgtype==message_type_image 时
//m_imageXxx 系列字段为图片元信息（客户端据 m_imageFileId 走下载链路取原图）；
//msgtype==message_type_file 时 m_fileXxx 系列字段为文件元信息（客户端据 m_fileFileId
/**
 *  引用回复的被引用消息快照（content.quote）。
 *
 *  为什么存快照而不是只存 msgid：原消息可能根本不在本地已加载范围内（翻不到那么
 *  早），也可能已被撤回 / 删除 —— 只有 msgid 的话引用块什么都显示不出来。带上发送时
 *  的显示名与摘要，任何情况下都能优雅降级。代价是原消息若被编辑快照会过时，而本项目
 *  没有消息编辑功能，这个代价目前为零。
 *
 *  服务端对 quote 是<u>完全不透明</u>的：MessageProtocol 只把它当一段 JSON 原样落库、
 *  原样回推（见 flamingoserver/chatserversrc/MessageProtocol.cpp），字段怎么定由客户端
 *  说了算，加字段也不需要服务端配合。
 */
struct ReplyQuote
{
    //是否带引用。为 false 时其余字段无意义，且发送时整个 quote 字段都不写。
    bool        m_valid;

    //被引用消息的服务器 msgid。字符串编码传输（与 cid / fileid 同口径，避免大整数
    //在 JSON 里丢精度），本结构里已换算回整数。0 表示原消息还没有服务器 id（不该发生）。
    int64_t     m_msgid;

    //被引用消息的发送者 userid。点引用块跳转、以及将来做"跳转后高亮"时用得上。
    int64_t     m_from;

    //发送时快照的显示名（UTF-8，不含 '@'）。群名片改了也不影响已发出的引用。
    std::string m_name;

    //被引用消息的类型（message_type_*）。决定引用块画文字还是画缩略图。
    int         m_msgtype;

    //摘要快照（UTF-8）。文本取正文截断；图片 / 文件等非文本类型取占位文案或文件名。
    std::string m_digest;

    //==== 仅被引用消息为图片时有效：引用块里画缩略图要用 ====
    std::string m_fileid;   //原图 fileid（字符串编码）
    std::string m_md5;      //原图 MD5，配合 fileid 做本地缓存命中
    int         m_width;    //原图宽（像素），按引用块高度等比缩放用
    int         m_height;   //原图高（像素）

    ReplyQuote()
        : m_valid(false)
        , m_msgid(0)
        , m_from(0)
        , m_msgtype(0)
        , m_width(0)
        , m_height(0)
    {
    }
};

//走下载链路取原文件、按 m_fileName 命名落地）；其它类型上述字段均为空 / 0。
struct MessageNotify
{
    int64_t     m_msgid;        //服务器消息ID
    std::string m_clientmsgid;  //发送方的客户端消息ID（多端去重/回填用）
    int         m_ctype;        //会话类型：见 enum conversation_type，单聊=1
    int64_t     m_cid;          //会话ID：收件人视角下=发送方 userid
    int64_t     m_from;         //发送方 userid
    int64_t     m_to;           //接收方 userid（即本端自己）
    int         m_msgtype;      //内容类型：见 enum message_type，文本=1、图片=2
    std::string m_text;         //文本正文（仅 m_msgtype==message_type_text 时有效，UTF-8）
    TextFont    m_font;         //文本字体设置（仅文本消息、且发送方设过字体时 m_valid 为 true）
    int64_t     m_timestamp;    //消息时间（Unix 毫秒）
    bool        m_recalled;     //是否已撤回（本轮恒为 false，预留）

    //==== 图片消息字段（仅 m_msgtype==message_type_image 时有效）====
    std::string m_imageFileId;  //图片文件ID（字符串编码），下载原图时传给 cmd_request_download
    std::string m_imageMd5;     //图片内容MD5（小写十六进制），下载完成后校验
    int         m_imageWidth;   //图片像素宽（发送方写入，供收方先按比例占位再填图）
    int         m_imageHeight;  //图片像素高
    int64_t     m_imageSize;    //图片文件字节数

    //==== 文件消息字段（仅 m_msgtype==message_type_file 时有效）====
    std::string m_fileFileId;   //文件ID（字符串编码），下载原文件时传给 cmd_request_download
    std::string m_fileName;     //原始文件名（含扩展名，UTF-8），收方据此命名落地文件
    int64_t     m_fileSize;     //文件字节数（发送方写入，供收方显示大小与作下载进度分母）
    std::string m_fileMd5;      //文件内容MD5（小写十六进制），下载完成后校验完整性

    //==== 文件夹传输记录字段（仅 m_msgtype==message_type_folder 时有效）====
    //一次单聊「发送文件夹」走到终态后，服务端在双方会话里各留一条这样的记录条，
    //本端不发这条消息、只收。字段含义与取值范围见 net::FolderRecordContent
    //（net/FolderProtocolClient.h），解析也是转调那边的 parseRecordContent。
    //<u>同一次传输可能留下多条记录</u>：每次尝试各一条（失败一条、重试成功再一条），
    //故 m_folderSessionId 在多条消息之间可以重复，不是唯一键。
    std::string m_folderSessionId;   //本次传输的会话 id；聊天窗据它把记录与本地气泡对上
    std::string m_folderName;        //根目录名（只有名字、不含路径，UTF-8）
    int64_t     m_folderTotalDirs;   //目录总数，含空目录、不含根目录本身；单位：个
    int64_t     m_folderTotalFiles;  //文件总数；单位：个
    int64_t     m_folderTotalSize;   //全部文件字节数合计；单位：字节
    int         m_folderResult;      //结局，取值为 folder_transfer_state 的终态（3~8）；0=非文件夹记录
    int         m_folderErrCode;     //失败原因（error_code_folder_*）；仅结局为 7 时有意义

    //==== 群聊 @提醒 字段（仅文本消息、且发送方 @ 过人时非空；见 docs/群聊@提醒方案.md）====
    //老版本客户端发来的消息没有 content.at，这两项保持默认（false / 空），按纯文本渲染。
    bool m_atAll;                                       //本条是否 @所有人
    std::vector<balloonchatwnd::MentionSpan> m_ats;     //被 @ 的人及其在 m_text 里的片段（UTF-8 字节口径）

    //==== 引用回复（content.quote）====
    //老版本客户端发来的消息没有 content.quote，m_quote.m_valid 保持 false，按无引用渲染。
    ReplyQuote m_quote;

    //文件收取状态（仅本端发出的文件消息、且经 cmd_sync_messages 拉回时才可能非 0）。
    //取值见 enum file_ack_state：1 对方正在接收、2 对方已接收、3 对方已拒绝；0=无。
    //服务端只对"拉取者自己发出的文件消息"回填本字段，使发送方离线期间对方的接收 /
    //拒绝在下次上线拉历史时也能看到。实时的状态变化另经 cmd_file_transfer_ack 推送。
    int m_fileAckState;

    MessageNotify()
        : m_msgid(0)
        , m_ctype(0)
        , m_cid(0)
        , m_from(0)
        , m_to(0)
        , m_msgtype(0)
        , m_timestamp(0)
        , m_recalled(false)
        , m_imageWidth(0)
        , m_imageHeight(0)
        , m_imageSize(0)
        , m_fileSize(0)
        , m_folderTotalDirs(0)
        , m_folderTotalFiles(0)
        , m_folderTotalSize(0)
        , m_folderResult(0)
        , m_folderErrCode(0)
        , m_atAll(false)
        , m_fileAckState(0)
    {
    }
};

//cmd_file_transfer_ack(1517) 推送解析结果：对方把我发的某个文件收到什么程度了。
//本端作为文件发送方收到，据 m_msgid 定位那条文件气泡、按 m_state 更新其状态行。
struct FileTransferAckNotify
{
    int     m_ctype;   //会话类型：1 单聊（当前只有单聊文件消息）
    int64_t m_cid;     //收件方（即本端）视角的会话ID，取上报方（文件接收方）的userid
    int64_t m_msgid;   //被收取的那条文件消息的服务器msgid
    int     m_state;   //收取状态，取值见 enum file_ack_state

    FileTransferAckNotify()
        : m_ctype(1)
        , m_cid(0)
        , m_msgid(0)
        , m_state(0)
    {
    }
};

//cmd_typing_notify(1510) 包体 state 字段的取值（见 docs/协议设计-v2.md 10.7）。
//该字段只有这两个取值，协议里没有别的语义，故就近定义在本文件而不进 Protocol.h ——
//Protocol.h 要与服务端那份逐字节同步，不宜单方面添枚举。
enum typing_state
{
    typing_state_stopped = 0,   //停止输入：对端界面应收起"正在输入…"提示
    typing_state_typing  = 1,   //正在输入：对端界面应显示"正在输入…"提示
};

//cmd_typing_notify(1510) 推送解析结果：对端正在（或停止）输入。
//注意请求与推送的 cid 语义<u>不同</u>：本端发请求时 cid 填"我发给谁"（对端userid），
//服务端转发时把它改写成"正在输入的是谁"（见服务端 ChatSession::onTypingNotify），
//所以本端收到的 m_cid 就是对端userid，可直接据它路由到对应聊天窗。
struct TypingNotify
{
    int     m_ctype;   //会话类型：1 单聊（服务端只转发单聊，群聊一律丢弃）
    int64_t m_cid;     //正在输入的那一方的userid（即本会话对端）
    int     m_state;   //输入状态：1 输入中、0 停止

    TypingNotify()
        : m_ctype(1)
        , m_cid(0)
        , m_state(0)
    {
    }
};

//撤回消息（cmd_recall_message=1508）的应答解析结果。
//m_code==0 表示服务端已把该条标记为撤回并向相关方推送了 cmd_message_recalled_notify；
//非 0 时按 enum error_code 区分原因，调用方据此给出不同提示（见 Protocol.h）：
//  error_code_recall_timeout(501)     超出可撤回时限（服务端为发出后 2 分钟）
//  error_code_recall_not_allowed(502) 无权撤回该消息（非本人发出、且非群主/管理员）
//  error_code_message_not_exist(503)  消息不存在（如本端 msgid 尚未回填就发了请求）
struct RecallMessageResponse
{
    int         m_code;   //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;    //结果描述（成功为 "ok"，失败为诊断信息）

    RecallMessageResponse()
        : m_code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//某条消息被撤回的推送（cmd_message_recalled_notify=1509）解析结果。
//单聊时服务端向双方各推一条（各自视角的 cid 不同）；群聊时向全体群成员推送、cid 取群 id。
//撤回者本人也会收到，故调用方须据 m_operator 是否等于自己来决定提示文案。
struct MessageRecalledNotify
{
    int     m_ctype;      //会话类型：见 enum conversation_type，单聊=1、群聊=2
    int64_t m_cid;        //收件方视角的会话ID：单聊=对端 userid，群聊=群 id
    int64_t m_msgid;      //被撤回的消息 msgid（服务器分配，恒 >0）
    int64_t m_operator;   //执行撤回的用户 userid（可能是消息发送者本人，也可能是群主/管理员）

    MessageRecalledNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
        , m_operator(0)
    {
    }
};

//Pin / 取消 Pin 一条消息（cmd_pin_message=1518）的应答解析结果。
//m_code==0 表示服务端已落库并向相关方推送了 cmd_message_pinned_notify；
//非 0 时按 enum error_code 区分原因，调用方据此给出不同提示（见 Protocol.h）：
//  error_code_pin_not_allowed(506)    无权取消该消息的 Pin（群里只有钉它的人本人与
//                                     群主 / 管理员能取消）
//  error_code_message_not_exist(503)  消息不存在或已被撤回
//  error_code_not_group_member(302)   已不在该群里
//应答把 ctype / cid / msgid / pin 原样回带，便于多条请求在途时对上号 —— 用户连点
//几条消息时会有好几个请求同时在飞，不带这四项就没法指出到底是哪一条失败了。
struct PinMessageResponse
{
    int         m_code;    //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;     //结果描述（成功为 "ok"，失败为诊断信息）
    int         m_ctype;   //本次操作的会话类型，服务端原样回带
    int64_t     m_cid;     //本次操作的会话ID，服务端原样回带
    int64_t     m_msgid;   //本次操作的消息msgid，服务端原样回带
    int         m_pin;     //本次操作的目标状态：1 钉住、0 取消，服务端原样回带

    PinMessageResponse()
        : m_code(-1)       //-1：尚未解析出有效结果码，避免被误判为成功
        , m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
        , m_pin(0)
    {
    }
};

//某条消息被 Pin / 取消 Pin 的推送（cmd_message_pinned_notify=1519）解析结果。
//Pin 是<u>会话内共享</u>的状态：单聊时服务端向双方各推一条（各自视角的 cid 不同），
//群聊时向全体群成员推送、cid 取群 id。操作者本人也会收到 —— 界面改动一律等推送，
//与撤回同口径，这样本端与对端看到的结果必然一致。
struct MessagePinnedNotify
{
    int     m_ctype;      //会话类型：见 enum conversation_type，单聊=1、群聊=2
    int64_t m_cid;        //收件方视角的会话ID：单聊=对端 userid，群聊=群 id
    int64_t m_msgid;      //被 Pin / 取消 Pin 的消息 msgid（服务器分配，恒 >0）
    int     m_pin;        //新状态：1 已钉住、0 已取消
    int64_t m_operator;   //执行本次操作的用户 userid
    int64_t m_pinTime;    //Pin 时间（Unix毫秒）；取消 Pin 时为 0

    MessagePinnedNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
        , m_pin(0)
        , m_operator(0)
        , m_pinTime(0)
    {
    }
};

//cmd_get_pinned_messages=1520 应答里的一条：完整消息体 + 它的 Pin 元信息。
//消息体复用 MessageNotify（与 cmd_sync_messages 的元素同结构）。
struct PinnedMessageItem
{
    MessageNotify m_msg;        //消息本体
    int64_t       m_operator;   //钉住它的用户 userid
    int64_t       m_pinTime;    //Pin 时间（Unix毫秒）

    PinnedMessageItem()
        : m_operator(0)
        , m_pinTime(0)
    {
    }
};

//拉取某会话被 Pin 的消息（cmd_get_pinned_messages=1520）应答解析结果。
//
//这条命令存在的理由：客户端"消息记录"窗的 Pin 分类查的是本地 SQLite，而本地库里
//只有本机同步过的那些消息。同事钉了一条三个月前的老消息时本机很可能压根没有那一行，
//Pin 分类就会漏掉它。故开窗时拉一次全集，把缺的消息体补进本地库并顺带对账。
struct PinnedMessagesResult
{
    int     m_code;    //结果码，0 成功
    int     m_ctype;   //会话类型：1 单聊 2 群聊
    int64_t m_cid;     //会话ID：单聊=对端userid，群聊=群id
    //<u>不受单次返回条数约束</u>的真实总数。它可能大于 m_items 的长度，说明本次被
    //截断了；调用方据此决定本地对账的时间下界 —— 全量清零会把那些"更老的、这次没
    //返回的" Pin 误清掉。详见 docs/消息Pin接入方案.md §4.3。
    int64_t m_total;
    std::vector<PinnedMessageItem> m_items;   //本次返回的被 Pin 消息（最新在前）

    PinnedMessagesResult()
        : m_code(-1)
        , m_ctype(0)
        , m_cid(0)
        , m_total(0)
    {
    }
};

//拉取历史消息（cmd_sync_messages=1502）应答解析结果。每条消息复用 MessageNotify
//结构（字段一致：msgid/from/to/ctype/cid/msgtype/text/timestamp 等）。服务端按
//msgid 倒序（最新在前）返回，渲染时调用方须倒序遍历成时间正序。
struct SyncMessagesResult
{
    int     m_code;       //结果码，0 成功
    int     m_ctype;      //会话类型：1 单聊 2 群聊
    int64_t m_cid;        //会话ID：单聊=对端userid，群聊=群id
    bool    m_hasmore;    //是否还有更早的消息（本轮暂不接"上拉加载更多"）
    std::vector<MessageNotify> m_messages;   //本次拉到的消息（最新在前）

    SyncMessagesResult()
        : m_code(-1)
        , m_ctype(0)
        , m_cid(0)
        , m_hasmore(false)
    {
    }
};

//消息可撤回的时限（毫秒）：消息发出后 2 分钟内允许撤回，超时服务端返回
//error_code_recall_timeout。本值必须与服务端 chatserversrc/ChatSession.cpp 里的
//RECALL_TIME_LIMIT_MS 保持一致，改动时两处要一起改，否则会出现"客户端显示可撤回、
//服务端却拒绝"的不一致。
const int64_t kRecallTimeLimitMs = 2 * 60 * 1000;

//单聊消息报文编解码工具类；全部为静态方法，不可实例化。
class MessageProtocolClient
{
public:
    /**
     *  构造单聊文本消息的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8）。
     *  @param peerUserId  对端用户 userid（>0）；作为 JSON 的 cid 字段以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；服务器据此去重并在回执里回填
     *  @param text        文本正文（UTF-8 字节）；写入 content.text
     *  @param font        文本字体设置；m_valid 为 true 时写入 content.font，否则不写
     *  @param quote       引用回复快照；m_valid 为 true 时写入 content.quote，否则不写
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendTextRequest(int64_t peerUserId,
                                            const std::string& clientMsgId,
                                            const std::string& text,
                                            const TextFont& font = TextFont(),
                                            const ReplyQuote& quote = ReplyQuote());

    /**
     *  构造单聊"窗口抖动"的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8）。
     *  抖动无正文，content 为空对象 {}；服务端对所有 msgtype 通用处理、透明存 content。
     *  @param peerUserId  对端用户 userid（>0）；作为 JSON 的 cid 字段以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；服务器据此去重并在回执里回填
     *  @return 紧凑格式的请求 JSON 文本（msgtype=message_type_shake，content={}）
     */
    static std::string buildSendShakeRequest(int64_t peerUserId,
                                             const std::string& clientMsgId);

    /**
     *  构造一条群聊文本消息的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8）。
     *  与单聊文本同结构，区别仅在 ctype=conversation_group、cid=群id。
     *  @param groupId     目标群 id（>0）；作为 JSON 的 cid 字段以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；服务器据此去重并在回执里回填
     *  @param text        文本正文（UTF-8 字节）；写入 content.text
     *  @param font        文本字体设置；m_valid 为 true 时写入 content.font，否则不写
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendGroupTextRequest(int64_t groupId,
                                                 const std::string& clientMsgId,
                                                 const std::string& text,
                                                 const TextFont& font = TextFont(),
                                                 const ReplyQuote& quote = ReplyQuote());

    /**
     *  构造一条<u>带 @提醒</u>的群聊文本消息发送请求（cmd_send_message=1503）。
     *  与 buildSendGroupTextRequest 同结构，额外在 content 里写入 at 对象：
     *    "at": { "all": 0, "users": [ { "uid":"10001", "off":0, "len":7 }, ... ] }
     *  ats 为空且 atAll 为 false 时<u>不写</u> at 字段，退化成普通群聊文本消息，
     *  与老版本客户端完全兼容。协议细节见 docs/群聊@提醒方案.md §3。
     *  @param groupId     目标群 id（>0）；作为 JSON 的 cid 字段以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）
     *  @param text        文本正文（UTF-8 字节）；写入 content.text
     *  @param ats         被 @ 的人及其片段；m_off/m_len 是 text 里的 UTF-8 字节口径，
     *                     须由调用方在发送前用 RescanMentions 按最终文本重算过
     *  @param atAll       是否 @所有人；为真时写 at.all=1
     *  @param font        文本字体设置；m_valid 为 true 时写入 content.font
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendGroupTextRequest(int64_t groupId,
                                                 const std::string& clientMsgId,
                                                 const std::string& text,
                                                 const std::vector<balloonchatwnd::MentionSpan>& ats,
                                                 bool atAll,
                                                 const TextFont& font = TextFont(),
                                                 const ReplyQuote& quote = ReplyQuote());

    /**
     *  构造一条图片消息的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8）。
     *  单聊群聊共用：仅 ctype 与 cid 含义不同（单聊 cid=对端 userid，群聊 cid=群id）。
     *  图片须已上传到 imgserver（先走 cmd_request_upload + cmd_file_upload 拿到
     *  fileid），本方法只负责把 fileid 等元信息打进 content（msgtype=2）。本轮不带
     *  缩略图（thumbfileid），收方下载原图后按比例缩放显示。
     *  @param ctype       会话类型，取值见 enum conversation_type（单聊=1、群聊=2）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id，>0）；以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；服务器据此去重并在回执里回填
     *  @param fileid      图片文件ID（字符串编码，来自 cmd_request_upload 应答）
     *  @param md5         图片内容MD5（小写十六进制，32 位）
     *  @param width       图片像素宽（>0）
     *  @param height      图片像素高（>0）
     *  @param size        图片文件字节数（>0）
     *  @return 紧凑格式的请求 JSON 文本（msgtype=message_type_image）
     */
    static std::string buildSendImageRequest(int ctype, int64_t cid,
                                             const std::string& clientMsgId,
                                             const std::string& fileid,
                                             const std::string& md5,
                                             int width, int height, int64_t size);

    /**
     *  构造一条文件消息的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8）。
     *  单聊群聊共用：仅 ctype 与 cid 含义不同（单聊 cid=对端 userid，群聊 cid=群id）。
     *  文件须已上传到 fileserver（先走 cmd_request_upload + cmd_file_upload 拿到
     *  fileid），本方法只负责把 fileid 等元信息打进 content（msgtype=3）。收方据
     *  fileid 走 cmd_request_download 下载，按 filename 命名落地、按 md5 校验完整性。
     *  @param ctype       会话类型，取值见 enum conversation_type（单聊=1、群聊=2）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id，>0）；以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；服务器据此去重并在回执里回填
     *  @param fileid      文件ID（字符串编码，来自 cmd_request_upload 应答）
     *  @param filename    原始文件名（含扩展名，UTF-8 字节，非空）
     *  @param filesize    文件字节数（>0）
     *  @param md5         文件内容MD5（小写十六进制，32 位）
     *  @return 紧凑格式的请求 JSON 文本（msgtype=message_type_file）
     */
    static std::string buildSendFileRequest(int ctype, int64_t cid,
                                            const std::string& clientMsgId,
                                            const std::string& fileid,
                                            const std::string& filename,
                                            int64_t filesize,
                                            const std::string& md5);

    /**
     *  构造拉取历史消息的请求 JSON 包体（cmd_sync_messages=1502，紧凑 UTF-8）。
     *  "拉最近 N 条"：beforeMsgId 传 0（从最新开始往旧取），limit 传 N（服务端上限 100）。
     *  @param ctype       会话类型（1 单聊 2 群聊）
     *  @param cid         会话ID（单聊=对端userid，群聊=群id）；以字符串编码
     *  @param beforeMsgId 翻历史锚点：取此 msgid 之前（更旧）的消息；0=从最新开始；字符串编码
     *  @param limit       本次最多拉取条数（服务端会夹到 [1,100]）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSyncMessagesRequest(int ctype, int64_t cid,
                                                int64_t beforeMsgId, int limit);

    /**
     *  构造上报已读位点的请求（cmd_mark_read=1506 的请求体）。用户查看会话后
     *  调用，告诉服务端"本会话已读至 readmsgid"，使服务端把 t_conversation 的
     *  f_readmsgid 取 GREATEST 更新；下次 cmd_sync_conversations 同步会话列表时，
     *  该会话未读数据此重新计算（已读部分清零），从而修复"看过消息后重新登录未读
     *  仍不清零"的问题。与服务端 MessageProtocol::parseMarkReadRequest 互为镜像。
     *  @param ctype     会话类型（1 单聊 2 群聊）
     *  @param cid       会话ID（单聊=对端userid，群聊=群id）；以字符串编码
     *  @param readmsgid 已读到的最大消息msgid；以字符串编码；调用方须保证为正，
     *                   服务端对 cid/readmsgid<=0 视为非法请求
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildMarkReadRequest(int ctype, int64_t cid, int64_t readmsgid);

    /**
     *  解析拉取历史消息的应答 JSON 包体（cmd_sync_messages=1502 的应答）。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_ctype/m_cid/m_hasmore/m_messages
     *  有效。messages 每条按 parseMessageNotify 同口径解析（仅文本填 m_text）。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSyncMessagesResponse(const std::string& jsonbody, SyncMessagesResult& out);

    /**
     *  按消息内容类型生成会话列表的"最后一条消息"预览文字（digest）。与服务端
     *  MessageProtocol::buildMessageDigest 语义对齐：文本/系统消息取正文（空则
     *  "[消息]"），其余类型返回占位文案（如表情"[表情]"、抖动"[抖一抖]"）。
     *  本端收到推送、或本端发出消息后冒泡会话时，用它统一算预览，使会话列表与
     *  服务端下次同步的 digest 一致。
     *  @param msgtype 消息内容类型，取值见 net::message_type（Protocol.h）
     *  @param text    文本正文（UTF-8）；仅 msgtype 为文本/系统时用到，其它忽略
     *  @return 预览文字（UTF-8）
     */
    static std::string digestForMsgType(int msgtype, const std::string& text);

    /**
     *  检测并净化"内联表情"文本（UTF-8 字节级）：PC 端把系统表情以 /f["<数字>"] 标签
     *  内联在文本消息正文里，文字部分的 '/' 则被转义成 '//'。本函数把标签替换成占位
     *  "[表情]"、把 '//' 还原成 '/'，使会话列表 / 通知 / 托盘预览里不暴露原始标记。
     *
     *  <u>仅当解析出至少一个合法 /f["N"] 标签时才认定"含内联表情"</u>并做上述替换；
     *  否则不写 out、返回 false。这个保守判定是有意为之：没有表情标签就无从判断文本
     *  里的 '//' 究竟是转义还是字面（如他端发来的 "http://..."），一律还原会把链接
     *  改坏，故宁可原样保留。
     *
     *  @param text 待净化的文本（UTF-8），可为空
     *  @param out  [出参] 净化后的文本；仅返回 true 时被写入，返回 false 时不动
     *  @return 含至少一个合法表情标签、已净化并写入 out 返回 true；否则返回 false
     */
    static bool tryNormalizeInlineFaceText(const std::string& text, std::string& out);

    /**
     *  构造 cmd_file_transfer_ack(1517) 上报请求 JSON：{ ctype, cid, msgid, state }。
     *  由文件<u>接收方</u>在点「接收」、下载成功、点「拒绝」这三个时机各发一次，
     *  服务端落库后转推发送方，使其知道文件到底被收没收。
     *  @param peerUserId 该文件消息的发送方userid（>0），即本次会话的对端
     *  @param msgid      被收取的文件消息的服务器msgid（>0）
     *  @param state      收取状态，取值见 enum file_ack_state
     *  @return 紧凑 JSON 文本；peerUserId / msgid 非正或 state 越界时返回空串
     */
    static std::string buildFileTransferAckRequest(int64_t peerUserId, int64_t msgid, int state);

    /**
     *  解析 cmd_file_transfer_ack(1517) 推送 JSON（本端作为文件发送方收到）。
     *  @param jsonbody 推送包体（UTF-8 JSON）
     *  @param out      [出参] 解析结果；返回 true 时各字段有效
     *  @return JSON 合法、msgid 为正且 state 在合法取值内返回 true，否则 false
     */
    static bool parseFileTransferAckNotify(const std::string& jsonbody, FileTransferAckNotify& out);

    /**
     *  构造 cmd_typing_notify(1510)「正在输入」请求 JSON：{ ctype, cid, state }。
     *  这条提示不落库、服务端不应答，发失败也无所谓，故调用方无需关心结果。
     *  @param peerUserId 对端userid（>0），即"这条提示要发给谁"
     *  @param state      输入状态：1 输入中、0 停止；其余值视为非法
     *  @return 紧凑 JSON 文本；peerUserId 非正或 state 越界时返回空串
     */
    static std::string buildTypingNotifyRequest(int64_t peerUserId, int state);

    /**
     *  解析 cmd_typing_notify(1510) 推送 JSON（对端正在输入 / 停止输入）。
     *  服务端只转发单聊，故 ctype 非单聊的推送一律视为非法。
     *  @param jsonbody 推送包体（UTF-8 JSON）
     *  @param out      [出参] 解析结果；返回 true 时各字段有效，m_cid 为对端userid
     *  @return JSON 合法、cid 为正且 state 取 0/1 返回 true，否则 false
     */
    static bool parseTypingNotify(const std::string& jsonbody, TypingNotify& out);

    /**
     *  解析单聊发送回执 JSON 包体（cmd_send_message=1503 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true，调用方据 resp.m_code 判定结果；
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 m_code/m_msg/m_clientmsgid 有效，
     *                  m_code==0 时 m_msgid/m_timestamp 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSendResponse(const std::string& jsonbody, SendMessageResponse& resp);

    /**
     *  解析收到的新消息推送 JSON 包体（cmd_message_notify=1504）。
     *  缺少关键字段（msgid/from 等）或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时各字段有效，文本正文在 m_text
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseMessageNotify(const std::string& jsonbody, MessageNotify& notify);

    /**
     *  构造撤回一条消息的请求 JSON 包体（cmd_recall_message=1508，紧凑 UTF-8）。
     *  与服务端 MessageProtocol::parseRecallMessageRequest 互为镜像：cid 与 msgid
     *  按 v2 约定以字符串编码，避免 64 位整数在 JSON 里丢精度。
     *  @param ctype 会话类型，取值见 enum conversation_type（单聊=1、群聊=2）
     *  @param cid   会话ID（单聊=对端 userid，群聊=群 id）；须 >0，服务端对 <=0 视为非法请求
     *  @param msgid 被撤回消息的服务器 msgid；须 >0，服务端对 <=0 视为非法请求
     *  @return 紧凑格式的请求 JSON 文本；cid 或 msgid 非正时返回空串（调用方不应发送）
     */
    static std::string buildRecallMessageRequest(int ctype, int64_t cid, int64_t msgid);

    /**
     *  解析撤回消息的应答 JSON 包体（cmd_recall_message=1508 的应答）。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 resp.m_code 判定结果与提示文案。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 m_code / m_msg 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRecallResponse(const std::string& jsonbody, RecallMessageResponse& resp);

    /**
     *  解析"某条消息被撤回"的推送 JSON 包体（cmd_message_recalled_notify=1509）。
     *  与服务端 MessageProtocol::buildMessageRecalledNotify 互为镜像。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时各字段有效
     *  @return 解析成功返回 true；JSON 非法、或 cid / msgid 缺失或非正时返回 false
     *          （此时 notify 内容未定义，调用方不得使用）
     */
    static bool parseMessageRecalledNotify(const std::string& jsonbody,
                                           MessageRecalledNotify& notify);

    /**
     *  构造 Pin / 取消 Pin 一条消息的请求 JSON 包体（cmd_pin_message=1518，紧凑 UTF-8）。
     *  与服务端 MessageProtocol::parsePinMessageRequest 互为镜像。
     *  @param ctype 会话类型（见 enum conversation_type，1 单聊 2 群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id）；须 >0
     *  @param msgid 目标消息的服务器 msgid；须 >0，服务端对 <=0 视为非法请求
     *  @param pin   true 钉住、false 取消
     *  @return 紧凑 JSON 文本；cid 或 msgid 非正时返回<u>空串</u>，调用方据此放弃发送
     */
    static std::string buildPinMessageRequest(int ctype, int64_t cid, int64_t msgid, bool pin);

    /**
     *  解析 Pin 消息的应答 JSON 包体（cmd_pin_message=1518 的应答）。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 resp.m_code 判定结果与提示文案。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时各字段有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parsePinResponse(const std::string& jsonbody, PinMessageResponse& resp);

    /**
     *  解析"某条消息被 Pin / 取消 Pin"的推送 JSON 包体（cmd_message_pinned_notify=1519）。
     *  与服务端 MessageProtocol::buildMessagePinnedNotify 互为镜像。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时各字段有效
     *  @return 解析成功返回 true；JSON 非法、或 cid / msgid 缺失或非正时返回 false
     *          （此时 notify 内容未定义，调用方不得使用）
     */
    static bool parseMessagePinnedNotify(const std::string& jsonbody,
                                         MessagePinnedNotify& notify);

    /**
     *  构造"拉取某会话被 Pin 的消息"的请求 JSON 包体（cmd_get_pinned_messages=1520）。
     *  @param ctype 会话类型
     *  @param cid   会话ID；须 >0
     *  @return 紧凑 JSON 文本；cid 非正时返回<u>空串</u>，调用方据此放弃发送
     */
    static std::string buildGetPinnedMessagesRequest(int ctype, int64_t cid);

    /**
     *  解析"拉取某会话被 Pin 的消息"的应答 JSON 包体（cmd_get_pinned_messages=1520）。
     *
     *  与 parseSyncMessagesResponse 同样的容错口径：单条消息解析失败（脏数据）时
     *  跳过该条、不影响其余；缺 msgs 字段按空列表处理，不算失败。
     *
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 且 m_code==0 时 m_items / m_total 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseGetPinnedMessagesResponse(const std::string& jsonbody,
                                               PinnedMessagesResult& out);

    /**
     *  判断一条消息当前是否还能撤回 —— 决定右键菜单里要不要出现"撤回"项。
     *
     *  本判断与服务端保持一致，避免用户点了才被拒：服务端在
     *  chatserversrc/ChatSession.cpp 的 onRecallMessage 里用
     *  "now - rec.timestamp > RECALL_TIME_LIMIT_MS" 判超时，故此处同样取<u>严格大于</u>，
     *  即正好卡在时限上仍算可撤回。
     *
     *  注意：服务端对群主 / 管理员撤回<u>不受</u>时限约束，本函数不掌握群内角色，
     *  只按"普通成员撤回本人消息"这一最严口径判断；群管理员撤他人消息的入口不走
     *  本函数（当前客户端也只对自己发出的消息显示撤回项）。
     *
     *  @param msgid          消息的服务器 msgid。<=0 表示服务器尚未回执、还不认识这条
     *                        消息（本端刚发出的"发送中"气泡），此时不允许撤回，
     *                        否则请求必然被服务端以 error_code_message_not_exist 拒绝
     *  @param msgTimestampMs 消息时间（Unix 毫秒）。<=0 视为时间未知，不允许撤回
     *  @param nowMs          当前时间（Unix 毫秒）。早于消息时间（时钟回拨）时差值为负，
     *                        不判为超时
     *  @return 可撤回返回 true，否则 false
     */
    static bool canRecallMessage(int64_t msgid, int64_t msgTimestampMs, int64_t nowMs);

private:
    MessageProtocolClient() = delete;
    ~MessageProtocolClient() = delete;
    MessageProtocolClient(const MessageProtocolClient&) = delete;
    MessageProtocolClient& operator=(const MessageProtocolClient&) = delete;
};

} // namespace net
