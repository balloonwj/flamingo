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
#include "../UI/BalloonChatWnd/GroupMentionModel.h"

namespace Json { class Value; }   //jsoncpp 的值类型：批注读写函数的参数，这里只前置声明，不引入 jsoncpp 头文件

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

//文字消息的格式，随 content.format 在收发双方之间传递（docs/Markdown消息方案.md 第 4 节）。
//服务端不认识也不改动这个字段，原样存储与转发；旧客户端忽略它，把 text 当纯文字显示。
enum text_format
{
    text_format_plain    = 0,   //纯文字：content 不带 format 字段（现有行为），或带了不认识的取值
    text_format_markdown = 1,   //Markdown：content.format 为 "markdown"，text 是 Markdown 源文本（表情标记照常）
    text_format_code     = 2,   //代码片段：content.format 为 "code"，text 是代码原文（不做表情标记转义），
                                //content.lang 为语言标识（docs/代码片段方案.md §4）
};

//content.format 取 Markdown 时的字面值
extern const char* const kTextFormatMarkdownValue;

//content.format 取代码片段时的字面值
extern const char* const kTextFormatCodeValue;

//代码片段的语言标识在 content 里的键名
extern const char* const kCodeLangKey;

//代码片段的批注在 content 里的键名（docs/代码片段方案.md 第 10.5 节）
extern const char* const kCodeNotesKey;

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

/**
 *  代码片段的一条批注：content.notes 数组的一项 {"from":3,"to":5,"text":"...","userid":"100008","new":1}
 *  （docs/代码片段方案.md 第 10.5 节、第 11.4 节）。行号从 1 起、包含两端，指规范化之后的代码。
 *  服务端不认识这个字段，只把说明文字纳入敏感词检查。
 */
struct CodeNoteData
{
    int         m_from;     //起始行，从 1 起
    int         m_to;       //结束行，从 1 起，包含这一行
    std::string m_text;     //说明文字（UTF-8）
    int64_t     m_userId;   //作者 userid（notes[].userid）；0 表示缺省，即作者是这条消息的发送者
    bool        m_isNew;    //notes[].new 为 1：消息发送者在这一轮追加批注时新增或修改的批注

    CodeNoteData()
        : m_from(0)
        , m_to(0)
        , m_userId(0)
        , m_isNew(false)
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
    int         m_textFormat;   //文本格式，取值见 enum text_format；只有文本消息可能为 Markdown 或代码片段，其余恒为纯文字
    std::string m_codeLang;     //代码片段的语言标识（content.lang，如 "cpp"）；只在 m_textFormat 为 text_format_code 时有效，
                                //缺省或不是字符串时为空，显示时按纯文本
    std::vector<CodeNoteData> m_codeNotes;   //代码片段的批注（content.notes）；只在 m_textFormat 为 text_format_code 时读，
                                             //格式不对的条目在这里就跳过，越界、相交等由显示端再整理
    int64_t     m_timestamp;    //消息时间（Unix 毫秒）
    bool        m_recalled;     //是否已撤回（拉历史时服务端照样下发已撤回的消息，聊天窗据此换成撤回提示行）
    int64_t     m_recalledBy;   //撤回者 userid（recalledby）；0 表示未撤回，或服务端没有记录撤回者
                                //（2026-10-03 服务端开始记录撤回者之前撤回的消息），此时按发送方处理

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
    //单聊单文件 P2P 直传留下的文件消息（content 带 direct=1，2026-10-04 起）：服务器上没有这个文件、没有 fileid
    bool        m_fileDirect;            //是不是在线直传的文件消息
    std::string m_fileDirectSessionId;   //直传的会话 id；聊天窗据它接管那条实时气泡
    int         m_fileDirectResult;      //直传的结局：folder_state_completed(8) 或 folder_state_rejected(3)；非直传为 0
    int         m_fileLinkPath;          //直传数据实际走的 P2P 链路路径（1 局域网直连、2 打洞直连、3 中转）；0=没有

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
    int         m_folderLinkPath;    //数据实际走的 P2P 链路路径（2026-10-04 起，1/2/3）；0=没有走 P2P 或老记录

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

    //==== 合并转发的聊天记录（仅 m_msgtype==message_type_merged 时有效）====
    //content 原文（紧凑 JSON），形如 {"title":"...","messages":[子消息体...]}。
    //不在解析推送时展开：聊天窗只画卡片、用不到全部子消息，打开查看窗时再经
    //MessageProtocolClient::parseMergedContent 解析；嵌套的记录也是用到哪层解析哪层。
    std::string m_mergedContent;

    //==== 群接龙卡片（仅 m_msgtype==message_type_relay 时有效）====
    //content 原文（紧凑 JSON），形如 {"title","note","deadline","items",...}。与合并记录一样
    //不在解析推送时展开，用到时经 CardProtocolClient::parseRelayCardContent 解析。同步、Pin 列表、
    //搜索下发的卡片带服务端合并的名单与状态；实时推送的新卡片只有 title / note / deadline。
    //本地库里的最新内容以 content 列为准（见 V2MsgQuery.h 的 NotifyFromRecord）
    std::string m_relayContent;

    //==== 文件共享卡片（仅 m_msgtype==message_type_fileshare 时有效）====
    //content 原文（紧凑 JSON），形如 {"shareid","kind","name","displaypath","ownerid"}；同步与推送下发时服务端
    //另按看的人合并了 sharestate / sharereason / myperms。用到时经 MessageProtocolClient::parseShareCardContent 解析
    std::string m_shareContent;

    //==== 屏幕共享卡片（仅 m_msgtype==message_type_screenshare 时有效）====
    //content 原文（紧凑 JSON），形如 {"roomid","host","hostname","starttime"}，服务端发起共享时生成、此后不再变化。
    //用到时经 MessageProtocolClient::parseScreenShareCardContent 解析
    std::string m_screenShareContent;

    //==== 通话记录卡片（仅 m_msgtype==message_type_rtc 时有效）====
    //content 原文（紧凑 JSON），两种：群通话卡片（kind 为 groupcall，docs/协议设计-v2.md 21.5）形如
    //{"kind":"groupcall","roomid","media","initiator","initiatorname","starttime"}，通话结束时服务端改写，另带 endtime 与 duration，
    //用到时经 groupcall::GroupCallProtocolClient::parseCard 解析；一对一通话卡片（kind 为 call，7.8 节「一对一通话卡片」）形如
    //{"kind":"call","sessionid","media","caller","callername","callee","starttime"}，结束时另带 result、endtime 与 duration（接通过才有），
    //用到时经 balloonchatwnd::ParseCallCard 解析
    std::string m_groupCallContent;

    //==== 入群邀请卡片（仅 m_msgtype==message_type_group_invite 时有效，docs/邀请入群方案.md §4.3）====
    //content 原文（紧凑 JSON），形如 {"inviteid","groupid","groupname","inviterid","inviter","inviteeid","invitee",
    //"expiretime"}；同步与推送下发时服务端另合并了 state / membercount 与群的当前名字、头像。
    //用到时经 balloonchatwnd::ApplyGroupInviteCard 解析
    std::string m_groupInviteContent;

    //==== 群公告卡片（仅 m_msgtype==message_type_announcement 时有效，docs/群公告接入方案.md §13.2）====
    //content 原文（紧凑 JSON），形如 {"announcementid","groupid","title","body","severity","publisherid","publisher",
    //"publishtime","republish","text"}；公告被删除后服务端改写为已删除形态（没有 body、带 deleted）。
    //用到时经 balloonchatwnd::ApplyAnnouncementCard 解析
    std::string m_announcementContent;

    //==== 群备忘卡片（仅 m_msgtype==message_type_memo 时有效，docs/备忘与提醒方案.md §7.2）====
    //content 原文（紧凑 JSON），形如 {"memoid","groupid","change","title","note","start","advance","repeat","weekdays",
    //"count","targetall","targetcount","editorid","editor","text"}，是保存那一刻的快照、之后不变。
    //用到时经 balloonchatwnd::ApplyMemoCard 解析
    std::string m_memoCardContent;

    //==== 帖子卡片与帖子动态卡片（仅 m_msgtype 为 message_type_thread_post / message_type_thread_reply 时有效，
    //docs/帖子群方案.md 6.2）====
    //content 原文（紧凑 JSON）：帖子卡片形如 {"postid","title","digest","imagecount"}，删帖后带 deleted；
    //帖子动态卡片形如 {"postid","commentid","addid","posttitle","digest","kind"}。用到时经 parseThreadCardContent 解析
    std::string m_threadCardContent;
    //卡片 content.mentions 里被 @ 的人（帖子里的 @、留言回复补充里的 @）；没有时为空。服务端生成的卡片没有正文，
    //普通消息那种带文字位置的 content.at 用不上，故单列（2026-10-07 实机联调发现卡片的 @ 提醒不生效）
    std::vector<int64_t> m_threadMentions;
    //卡片 content.mentionall：帖子、留言、回复或补充 @ 了所有人（docs/帖子群方案.md §15.1 第 2 条），此时除发布者外
    //全体成员都按被 @ 处理（threadCardMentions 对任何人都返回 true，发布者本人由调用方排除）
    bool m_threadMentionAll;

    //==== 云端录像卡片（仅 m_msgtype==message_type_cloudrecord 时有效，docs/屏幕共享与录制方案.md 12.6.5）====
    //content 原文（紧凑 JSON），形如 {"recordid","owner","title","durationms","bytes","audioonly"}，发出之后不变。
    //用到时经 balloonchatwnd::ApplyCloudRecordCard 解析
    std::string m_cloudRecordContent;

    //==== 语音消息字段（仅 m_msgtype==message_type_voice 时有效，docs/语音消息方案.md 第 5 节）====
    std::string m_voiceFileId;    //语音文件ID（字符串编码），下载时传给 cmd_request_download
    std::string m_voiceMd5;       //语音文件MD5（小写十六进制），下载完成后校验，也是本机缓存文件名
    int         m_voiceDuration;  //时长（整秒，1~120）
    int64_t     m_voiceSize;      //文件字节数
    std::string m_voiceWave;      //波形：40 个音量值的标准 Base64；老消息或发送方没写时为空串，用 net::VoiceWaveForDisplay 换算

    //接龙相关的群系统提示（m_msgtype==message_type_system）所指的卡片消息 msgid，取自
    //content.cardmsgid；服务端只在参与、修改、撤回、提前结束接龙的提示里带这个字段。
    //非 0 时提示行可以点击并打开对应卡片的详情窗；其余系统提示与其它类型的消息恒为 0
    int64_t m_cardMsgId;

    //系统提示（m_msgtype==message_type_system）的结构化类型与参数（多语言第 5 期）：取自 content.tipkind 与
    //content.tipargs（紧凑 JSON 文本），取值见 Protocol.h 的 kSystemTip*。解析时若认得类型，m_text 已换成当前
    //界面语言的文字（net::FormatSystemTip）；这两项另存一份，供落本地库后消息记录窗按同样的规则重新生成。
    //老服务端发的、或此前落库的系统提示没有这两个字段，两者都为空串，按服务端的 m_text 显示
    std::string m_tipKind;
    std::string m_tipArgs;

    //发送者昵称。只有合并记录里的子消息带（服务端转发时写入的 fromname，即转发当时
    //发送者的名字），普通推送与历史消息恒为空，调用方为空时按本地通讯录解析名字
    std::string m_fromName;

    //这条消息的原始 JSON（服务端发来的整个消息对象，紧凑格式），解析时一并保存。
    //本地消息库存下它，打开聊天窗时可以先用本地数据经 MessageProtocolClient::parseMessageNotify
    //还原出与网络应答完全相同的消息，不必等服务端应答（见 V2MsgQuery.h 的 NotifyFromRecord）。
    //手工构造、不经解析得到的消息为空串
    std::string m_rawJson;

    //这条消息是不是发送方开启「离开 / 忙碌时自动回复」后由客户端自动发出的（content.autoreply 为 true）。
    //只有文本消息可能为 true；收到这类消息时本端不再自动回复，避免两台都开着自动回复的客户端互相无限回复
    bool m_autoReply;

    //机器人标记（content.bot，只由服务端产生，docs/协议设计-v2.md 25.3）：群里机器人账号发的消息、单聊里个人机器人
    //替主人代答的消息都带。没有这个字段时 m_botId 为 0
    int64_t     m_botId;    //机器人 ID
    std::string m_botName;  //机器人名称（UTF-8；单聊代答与群机器人消息都带，群里用于显示已删除机器人的名字）
    std::string m_botKind;  //种类：群里为空串；单聊代答为 "personal"（个人机器人）或 "autoreply"（离开 / 忙碌自动回复）

    MessageNotify()
        : m_msgid(0)
        , m_ctype(0)
        , m_cid(0)
        , m_from(0)
        , m_to(0)
        , m_msgtype(0)
        , m_textFormat(text_format_plain)
        , m_timestamp(0)
        , m_recalled(false)
        , m_recalledBy(0)
        , m_imageWidth(0)
        , m_imageHeight(0)
        , m_imageSize(0)
        , m_fileSize(0)
        , m_fileDirect(false)
        , m_fileDirectResult(0)
        , m_fileLinkPath(0)
        , m_folderTotalDirs(0)
        , m_folderTotalFiles(0)
        , m_folderTotalSize(0)
        , m_folderResult(0)
        , m_folderErrCode(0)
        , m_folderLinkPath(0)
        , m_atAll(false)
        , m_fileAckState(0)
        , m_cardMsgId(0)
        , m_autoReply(false)
        , m_botId(0)
        , m_voiceDuration(0)
        , m_voiceSize(0)
        , m_threadMentionAll(false)
    {
    }
};

//自动回复消息正文的前缀（UTF-8）：「[自动回复] 」，只做了标记（TR_NOOP），写入正文时用 TR8 取发送方界面语言的译文。前缀写进正文本身，PC、安卓、消息记录与会话列表摘要
//里都能看出这是一条自动回复（2026-10-03 与用户确认）；是否为自动回复以 content.autoreply 为准，不按前缀判断
extern const char* const kAutoReplyTextPrefix;

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

/**
 *  删除单条消息的应答（cmd_delete_message=1512 的应答）。
 *  删除成功后本地的删行、移除气泡统一在收到 cmd_messages_deleted_notify 时做，
 *  本应答只用于失败时提示用户。
 */
struct DeleteMessageResponse
{
    int         m_code;   //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;    //结果描述（成功为 "ok"，失败为诊断信息）

    DeleteMessageResponse()
        : m_code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

/**
 *  按时间范围清理聊天记录的应答（cmd_delete_messages_by_time=1521 的应答）。
 *  成功时带回服务端截断后的实际区间，清理对话框据此提示"已清理 X 至 Y"。
 */
struct DeleteMessagesByTimeResponse
{
    int         m_code;       //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;        //结果描述
    int64_t     m_begintime;  //实际生效的区间开始，Unix毫秒；失败应答不带时为 0
    int64_t     m_endtime;    //实际生效的区间结束，Unix毫秒；失败应答不带时为 0

    DeleteMessagesByTimeResponse()
        : m_code(-1)
        , m_begintime(0)
        , m_endtime(0)
    {
    }
};

/**
 *  按会话清空聊天记录的应答（cmd_clear_conversation=1523）。失败应答只带 code / msg。
 */
struct ClearConversationResponse
{
    int         m_code;          //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;           //结果描述
    int         m_ctype;         //会话类型：1 单聊 2 群聊；失败应答不带时为 0
    int64_t     m_cid;           //本人视角的会话ID；失败应答不带时为 0
    int64_t     m_clearedMsgid;  //生效的清空锚点：该会话 msgid 不大于它的消息对本人隐藏；失败应答不带时为 0

    ClearConversationResponse()
        : m_code(-1)
        , m_ctype(0)
        , m_cid(0)
        , m_clearedMsgid(0)
    {
    }
};

/**
 *  "本人删除了消息"的推送（cmd_messages_deleted_notify=1522），推给本人全部在线设备，
 *  发起删除的这一台也会收到。三种形式以 m_isClear / m_isRange 区分：
 *    - 单条形式（来自 1512）：m_ctype / m_cid / m_msgids 有效；
 *    - 区间形式（来自 1521）：m_begintime / m_endtime 有效，作用于本人全部会话；
 *    - 清空形式（来自 1523，m_isClear 为 true）：m_ctype / m_cid / m_clearedMsgid 有效，该会话里
 *      msgid 不大于锚点的消息都已对本人隐藏。
 */
struct MessagesDeletedNotify
{
    bool                 m_isRange;    //true=区间形式，false=单条形式
    int                  m_ctype;      //会话类型（单条形式）：单聊=1、群聊=2
    int64_t              m_cid;        //本人视角的会话ID（单条形式）：单聊=对方 userid，群聊=群 id
    std::vector<int64_t> m_msgids;     //被删除的消息 msgid（单条形式），当前恒为一条
    int64_t              m_begintime;  //区间开始，Unix毫秒（区间形式，含）
    int64_t              m_endtime;    //区间结束，Unix毫秒（区间形式，含）
    bool                 m_isClear;    //true=清空形式（来自 1523）；此时 m_isRange 为 false
    int64_t              m_clearedMsgid; //清空锚点（清空形式）：该会话 msgid 不大于它的消息都已清空；可为 0

    MessagesDeletedNotify()
        : m_isRange(false)
        , m_ctype(0)
        , m_cid(0)
        , m_begintime(0)
        , m_endtime(0)
        , m_isClear(false)
        , m_clearedMsgid(0)
    {
    }
};

//屏幕共享卡片（msgtype 19）的内容，由 MessageProtocolClient::parseScreenShareCardContent 从 content 原文解析
struct ScreenShareCardContent
{
    std::string m_roomId;      //房间号
    int64_t     m_hostId;      //发起人 userid
    std::string m_hostName;    //发起人显示名（UTF-8），发起时的快照
    int64_t     m_startTime;   //发起时间，Unix 毫秒；正文里没有时为 0
    bool        m_locked;      //加入是否需要密码（第 12 期）；正文里没有时为 false
    bool        m_forced;      //是否强制模式（第 13 期）；正文里没有时为 false
    std::string m_title;       //共享标题（第 15 期，UTF-8）；第 15 期之前的卡片没有，为空
    std::string m_desc;        //共享描述（第 15 期，UTF-8）；没填或第 15 期之前的卡片为空

    ScreenShareCardContent()
        : m_hostId(0)
        , m_startTime(0)
        , m_locked(false)
        , m_forced(false)
    {
    }
};

//文件共享卡片（msgtype 20）的内容，由 MessageProtocolClient::parseShareCardContent 从 content 原文解析
struct ShareCardContent
{
    int64_t     m_shareId;       //共享 id
    int         m_kind;          //共享类型：share_kind（1 磁盘 2 文件夹 3 文件）
    std::string m_name;          //显示名（UTF-8）
    std::string m_displayPath;   //显示路径（UTF-8）
    int64_t     m_ownerId;       //分享者 userid
    bool        m_hasState;      //是否带了服务端合并的状态（sharestate / sharereason）
    int         m_state;         //合并的状态：share_state
    int         m_reason;        //合并的原因
    bool        m_hasPerms;      //是否带了 myperms（分享者本人与已失效的成员不带）
    int         m_myPerms;       //看的人的写权限位

    ShareCardContent()
        : m_shareId(0)
        , m_kind(0)
        , m_ownerId(0)
        , m_hasState(false)
        , m_state(0)
        , m_reason(0)
        , m_hasPerms(false)
        , m_myPerms(0)
    {
    }
};

/**
 *  对方已读推送（cmd_message_read_notify=1507）。接收方查看会话后上报 cmd_mark_read，
 *  服务端据此向发送方推送本条，告诉它「对方已经读到了第 readmsgid 条」。已读是一个位点，
 *  不是逐条确认：本会话里 msgid 不大于 readmsgid 的消息都算对方已读。
 *  只有单聊会收到本推送；群聊按协议不做已读回执。
 */
struct MessageReadNotify
{
    int     m_ctype;      //会话类型：见 enum conversation_type，当前只会是单聊=1
    int64_t m_cid;        //收件方视角的会话ID：单聊=读消息的那个人（对端）的 userid
    int64_t m_readmsgid;  //对方已读到的最大消息 msgid（服务器分配，恒 >0）
    int     m_readcount;  //已读人数：单聊恒为 1；为群聊预留

    MessageReadNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_readmsgid(0)
        , m_readcount(0)
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
//一对一通话卡片已改写的推送（cmd_call_card_notify=1717，docs/协议设计-v2.md 7.8「一对一通话卡片」）解析结果
struct CallCardNotify
{
    std::string m_sessionId;   //通话会话 id
    int64_t     m_msgid;       //卡片消息的 msgid（服务器分配，恒 >0）
    int64_t     m_cid;         //收件方视角的会话 id：对方的 userid
    std::string m_content;     //改写后的卡片 content 原文（紧凑 JSON），用到时经 balloonchatwnd::ParseCallCard 解析

    CallCardNotify()
        : m_msgid(0)
        , m_cid(0)
    {
    }
};

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
    bool    m_hasmore;    //锚点方向上是否还有更早的消息（聊天窗上翻加载更多时据此决定是否继续）
    //单聊时对方已读到的最大消息 msgid（应答的 peerreadmsgid 字段，字符串编码）：自己发出的
    //消息 msgid 不大于它即为对方已读。对方从没读过、或群聊（服务端不带该字段）时为 0。
    int64_t m_peerReadMsgid;

    //应答是否回带了翻历史模式的请求锚点（beforemsgid 字段，2026-09-30 起服务端回带）。
    //旧服务端不带、或应答来自补齐模式（回带的是 aftermsgid）时为 false，此时 m_beforeMsgid
    //无意义，调用方不得据本页计算本地覆盖区间（见 docs/清理聊天记录方案.md §7.2）。
    bool    m_hasAnchor;
    //本页请求的锚点：拉取此 msgid 之前的消息；0 表示从最新开始。仅 m_hasAnchor 为 true 时有效
    int64_t m_beforeMsgid;

    //本次拉到的消息，按 msgid 升序（时间正序）排列：服务端翻历史时按降序查、返回前统一反转
    //为升序（flamingoserver/chatserversrc/MessageManager.cpp 的 getConversationMessages）
    std::vector<MessageNotify> m_messages;

    SyncMessagesResult()
        : m_code(-1)
        , m_ctype(0)
        , m_cid(0)
        , m_hasmore(false)
        , m_peerReadMsgid(0)
        , m_hasAnchor(false)
        , m_beforeMsgid(0)
    {
    }
};

//cmd_sync_rewritten_messages（1526）应答里的一项：一条被改写过（或被撤回）的消息及其改写序号
struct RewrittenMessageItem
{
    MessageNotify m_msg;   //消息本体，与 cmd_sync_messages 的元素同结构；m_ctype / m_cid 为本条所属会话（本人视角）
    int64_t       m_rev;   //改写序号（字符串编码的 rev 字段）；缺失或非法时为 0

    RewrittenMessageItem()
        : m_rev(0)
    {
    }
};

//按改写序号同步（cmd_sync_rewritten_messages=1526）应答解析结果（docs/消息改写离线同步方案.md）
struct SyncRewrittenResult
{
    int     m_code;     //结果码，0 成功；其余字段只在成功时有效
    //本页条目，按改写序号升序；解析失败（缺 msgid / from 的脏数据）的条目已跳过
    std::vector<RewrittenMessageItem> m_items;
    int64_t m_rev;      //本页最后一项的序号，下一页以它为 since；本页为空时等于请求的 since
    bool    m_more;     //后面是否还有
    int64_t m_currev;   //服务端当前的最大改写序号；本地库为空时直接把水位设成它

    SyncRewrittenResult()
        : m_code(-1)
        , m_rev(0)
        , m_more(false)
        , m_currev(0)
    {
    }
};

//合并转发的聊天记录（msgtype=15）的 content 解析结果。
//典型用法：聊天窗画卡片时取标题、条数与前几条的摘要；查看窗逐条显示子消息。
struct MergedRecord
{
    std::string m_title;                    //记录标题（UTF-8），如「张三和李四的聊天记录」
    //子消息，顺序与服务端打包时一致（源消息按 msgid 升序）。每条都是完整消息体，
    //m_fromName 为转发当时发送者的名字（可能为空）；子消息本身是合并记录时，
    //其 m_mergedContent 可再次交给 parseMergedContent 解析
    std::vector<MessageNotify> m_messages;
};

//转发消息（cmd_forward_message=1511）应答解析结果，见 docs/协议设计-v2.md 的 1511 一节。
//应答里没有请求的会话信息，失败应答连 clientmsgid 都不带，调用方按请求时 CIUSocket::ForwardMessage
//返回的帧 seq 把应答对回到是哪一次转发。
struct ForwardMessageResponse
{
    int         m_code;         //结果码，0 成功；非 0 见 enum error_code（如禁言、含敏感词、被拉黑）
    std::string m_msg;          //结果描述（英文，仅供记日志）
    std::string m_clientmsgid;  //请求里的 clientmsgid 原样回带；失败应答与旧服务端的应答为空

    //本次新建的消息 msgid，按新建顺序排列。逐条转发时每条源消息各一个（被跳过的源消息
    //没有对应项），合并转发时只有一个
    std::vector<int64_t> m_msgids;

    //新建消息的完整消息体，与 m_msgids 逐条对应，但可能只是它的前缀：服务端的消息体
    //累计超过体积预算后只给 msgid，此时缺消息体的那部分须经 cmd_sync_messages 拉取。
    //旧服务端不带本字段，恒为空
    std::vector<MessageNotify> m_messages;

    ForwardMessageResponse()
        : m_code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//单次转发可带的源消息条数上限。本值必须与服务端 chatserversrc/MessageProtocol.h 的
//FORWARD_MAX_MESSAGES 保持一致，改动时两处要一起改，否则会出现"客户端允许转发、
//服务端却拒绝"的不一致。
const size_t kForwardMaxMessages = 100;

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
     *  @param textFormat  文本格式，取值见 enum text_format；为 Markdown 或代码片段时写 content.format，纯文字时不写
     *  @param codeLang    代码片段的语言标识（如 "cpp"，见 codesnippet::CodeLanguageId）；textFormat 为代码片段
     *                    且不为空时写 content.lang，其余情况不写（docs/代码片段方案.md §4）
     *  @param codeNotes   代码片段的批注；textFormat 为代码片段且不为空时写 content.notes，其余情况不写
     *                    （docs/代码片段方案.md 第 10.5 节）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendTextRequest(int64_t peerUserId,
                                            const std::string& clientMsgId,
                                            const std::string& text,
                                            const TextFont& font = TextFont(),
                                            const ReplyQuote& quote = ReplyQuote(),
                                            int textFormat = text_format_plain,
                                            const std::string& codeLang = std::string(),
                                            const std::vector<CodeNoteData>& codeNotes = std::vector<CodeNoteData>());

    /**
     *  构造单聊自动回复的发送请求 JSON 包体（cmd_send_message=1503，msgtype=文本，紧凑 UTF-8）。
     *  与普通单聊文本相同，区别有两点：content.text 为 kAutoReplyTextPrefix + replyText；content 另带
     *  autoreply=true，收到的客户端据此不再自动回复。服务端原样存储并转发 content，不需要认识这个字段。
     *  @param peerUserId  对端用户 userid（>0）；作为 JSON 的 cid 字段以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID，非空）；回执据此对回到主面板记下的那条自动回复
     *  @param replyText   回复文字（UTF-8，不含前缀）；由调用方先经 NormalizeAutoReplyText 整理
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendAutoReplyRequest(int64_t peerUserId,
                                                 const std::string& clientMsgId,
                                                 const std::string& replyText);

    /**
     *  按服务端推送的格式拼出本人刚发出的那条自动回复，供回执成功后本端入库与显示。单聊消息服务端
     *  只推给对方、不回推给发送方，所以发送方要自己补这一份；拼出的 JSON 再经 parseMessageNotify
     *  同一套解析得到结果，m_rawJson 与服务端推来的消息格式一致，可以直接存进本地消息库。
     *  @param selfId      本人 userid（>0），作为 from
     *  @param peerUserId  对端 userid（>0），作为 cid 与 to（发送方视角下的会话 ID 就是对端）
     *  @param clientMsgId 发送时用的客户端消息ID
     *  @param msgid       回执里服务器分配的 msgid（>0）
     *  @param timestamp   回执里的服务器时间（Unix 毫秒）
     *  @param replyText   回复文字（UTF-8，不含前缀），与发送时传给 buildSendAutoReplyRequest 的相同
     *  @param out         [出参] 拼出的消息；m_autoReply 为 true，m_text 带前缀
     *  @return 参数合法且解析成功返回 true；selfId / peerUserId / msgid 不为正时返回 false，out 不可用
     */
    static bool buildAutoReplyEcho(int64_t selfId, int64_t peerUserId, const std::string& clientMsgId,
                                   int64_t msgid, int64_t timestamp, const std::string& replyText,
                                   MessageNotify& out);

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
     *  @param quote       引用回复快照；m_valid 为 true 时写入 content.quote，否则不写
     *  @param textFormat  文本格式，取值见 enum text_format；为 Markdown 或代码片段时写 content.format，纯文字时不写
     *  @param codeLang    代码片段的语言标识（如 "cpp"，见 codesnippet::CodeLanguageId）；textFormat 为代码片段
     *                    且不为空时写 content.lang，其余情况不写（docs/代码片段方案.md §4）
     *  @param codeNotes   代码片段的批注；textFormat 为代码片段且不为空时写 content.notes，其余情况不写
     *                    （docs/代码片段方案.md 第 10.5 节）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendGroupTextRequest(int64_t groupId,
                                                 const std::string& clientMsgId,
                                                 const std::string& text,
                                                 const TextFont& font = TextFont(),
                                                 const ReplyQuote& quote = ReplyQuote(),
                                                 int textFormat = text_format_plain,
                                                 const std::string& codeLang = std::string(),
                                                 const std::vector<CodeNoteData>& codeNotes = std::vector<CodeNoteData>());

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
     *  @param quote       引用回复快照；m_valid 为 true 时写入 content.quote
     *  @param textFormat  文本格式，取值见 enum text_format；为 Markdown 或代码片段时写 content.format，纯文字时不写
     *  @param codeLang    代码片段的语言标识（如 "cpp"，见 codesnippet::CodeLanguageId）；textFormat 为代码片段
     *                    且不为空时写 content.lang，其余情况不写（docs/代码片段方案.md §4）
     *  @param codeNotes   代码片段的批注；textFormat 为代码片段且不为空时写 content.notes，其余情况不写
     *                    （docs/代码片段方案.md 第 10.5 节）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSendGroupTextRequest(int64_t groupId,
                                                 const std::string& clientMsgId,
                                                 const std::string& text,
                                                 const std::vector<balloonchatwnd::MentionSpan>& ats,
                                                 bool atAll,
                                                 const TextFont& font = TextFont(),
                                                 const ReplyQuote& quote = ReplyQuote(),
                                                 int textFormat = text_format_plain,
                                                 const std::string& codeLang = std::string(),
                                                 const std::vector<CodeNoteData>& codeNotes = std::vector<CodeNoteData>());

    /**
     *  计算一条文字消息 content 序列化后的字节数。口径与服务端检查单条消息体积时完全一致：
     *  content 的组装与上面几个文本发送请求共用同一段代码（正文、字体、@、引用），序列化用
     *  同一个紧凑写出器（非 ASCII 字符写成 \uXXXX，一个汉字计 6 字节）。发送前拿它与
     *  MESSAGE_CONTENT_MAX_BYTES 比较，见 BalloonChatWnd/ChatSendLimit.h。
     *  @param text  正文（UTF-8 字节）
     *  @param font  文本字体设置；m_valid 为 true 时计入 content.font
     *  @param quote 引用回复快照；m_valid 为 true 时计入 content.quote
     *  @param ats   被 @ 的人及其片段；单聊或不带 @ 时传空
     *  @param atAll 是否 @所有人
     *  @param textFormat 文本格式，取值见 enum text_format；为 Markdown 或代码片段时计入 content.format
     *  @param codeLang   代码片段的语言标识；textFormat 为代码片段且不为空时计入 content.lang
     *  @param codeNotes  代码片段的批注；textFormat 为代码片段且不为空时计入 content.notes
     *  @return content 紧凑 JSON 的字节数
     */
    static size_t textContentBytes(const std::string& text, const TextFont& font,
                                   const ReplyQuote& quote,
                                   const std::vector<balloonchatwnd::MentionSpan>& ats,
                                   bool atAll,
                                   int textFormat = text_format_plain,
                                   const std::string& codeLang = std::string(),
                                   const std::vector<CodeNoteData>& codeNotes = std::vector<CodeNoteData>());

    /**
     *  把代码片段的批注写进 content.notes（数组，每项 from / to / text；m_userId 不为 0 时写 userid（十进制字符串），
     *  m_isNew 为真时写 "new": 1）；批注为空时不写。文字消息的发送请求与本地库的 content 列共用，保证两边写法一致。
     *  @param content [入参/出参] 要写入的 content 对象
     *  @param notes   批注
     */
    static void writeCodeNotesToContent(Json::Value& content, const std::vector<CodeNoteData>& notes);

    /**
     *  从 content 读出代码片段的批注：notes 不是数组时为空；条目不是对象、from / to 不是正整数或 to 小于 from、
     *  text 不是非空字符串、带了 userid 却不是正整数（字符串或整数）的跳过；new 为 1 或 true 时 m_isNew 为真。
     *  越界、相交、超过条数与字数上限由显示端整理（codesnippet::SanitizeCodeNotes）。
     *  @param content content 对象
     *  @return 批注
     */
    static std::vector<CodeNoteData> readCodeNotesFromContent(const Json::Value& content);

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
     *  构造一条语音消息的发送请求 JSON 包体（cmd_send_message=1503，紧凑 UTF-8），方案见 docs/语音消息方案.md。
     *  语音文件须已经走聊天文件场景上传到 fileserver（拿到 fileid），本方法只把元信息打进 content（msgtype=4）。
     *  @param ctype           会话类型（单聊=1、群聊=2）
     *  @param cid             会话ID（单聊=对端 userid，群聊=群id，>0）；以字符串编码
     *  @param clientMsgId     客户端消息ID（非空）；服务器据此去重并在回执里回填
     *  @param fileid          语音文件ID（字符串编码，来自 cmd_request_upload 应答）
     *  @param md5             文件内容MD5（小写十六进制）
     *  @param durationSeconds 时长（整秒，1~120，见 net::VoiceDurationSeconds）
     *  @param size            文件字节数（>0）
     *  @param wave            波形的 Base64（见 net::EncodeVoiceWave）；为空串时不写 wave 字段
     *  @return 紧凑格式的请求 JSON 文本（msgtype=message_type_voice）
     */
    static std::string buildSendVoiceRequest(int ctype, int64_t cid,
                                             const std::string& clientMsgId,
                                             const std::string& fileid,
                                             const std::string& md5,
                                             int durationSeconds, int64_t size,
                                             const std::string& wave);

    /**
     *  构造一条卡片消息（投票 / 接龙 / 待办）的发送请求 JSON 包体（cmd_send_message=1503，
     *  紧凑 UTF-8）。卡片 content 由各卡片的协议模块校验并拼好（接龙见
     *  CardProtocolClient::buildRelayCardContent），本方法只套上与其它消息相同的外层信封。
     *  @param ctype       会话类型，取值见 enum conversation_type（单聊=1、群聊=2）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id）；须 >0，以字符串编码
     *  @param clientMsgId 客户端消息ID（UUID）；不能为空，服务器据此去重并在回执里回填
     *  @param msgtype     卡片的消息类型（message_type_poll / message_type_relay / message_type_todo）
     *  @param contentJson 卡片 content 的 JSON 文本；必须是 JSON 对象
     *  @return 紧凑 JSON 文本；cid 非正、clientMsgId 为空或 contentJson 不是 JSON 对象时返回<u>空串</u>，
     *          调用方据此放弃发送
     */
    static std::string buildSendCardRequest(int ctype, int64_t cid,
                                            const std::string& clientMsgId,
                                            int msgtype,
                                            const std::string& contentJson);

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
     *  构造按改写序号同步的请求 JSON 包体（cmd_sync_rewritten_messages=1526，紧凑 UTF-8）。
     *  @param since 本地已同步到的改写序号；首次为 0。以字符串编码，与服务端 getStringId 的契约一致
     *  @param limit 本页最多几条（服务端上限 200，不大于 0 时取 200）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSyncRewrittenRequest(int64_t since, int limit);

    /**
     *  解析按改写序号同步的应答 JSON 包体（cmd_sync_rewritten_messages=1526 的应答）。
     *  JSON 合法且含整型 code 即返回 true；code==0 时其余字段有效。items 每条按 cmd_sync_messages 的消息条目
     *  同口径解析，另取字符串编码的 rev；应答的 rev / currev 同样是字符串，缺失时取 0，more 缺失时取 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSyncRewrittenResponse(const std::string& jsonbody, SyncRewrittenResult& out);

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
     *  按一条已解析的消息算会话预览：接龙卡片（msgtype = 11）写成「[接龙] 标题」，标题取自
     *  m_relayContent，与服务端 buildMessageDigest 一致（标题为空或 content 不可用时只写「[接龙]」）；
     *  其余类型与 digestForMsgType(notify.m_msgtype, notify.m_text) 相同。
     *  @param notify 已解析的消息
     *  @return 预览文字（UTF-8）
     */
    static std::string digestForNotify(const MessageNotify& notify);

    /**
     *  帖子卡片（25）与帖子动态卡片（26）的会话摘要，按当前界面语言生成（docs/帖子群方案.md 4.5、6.2）。
     *  不含发送者名字：主面板显示时在前面加上，成为「张三 发帖：周五团建报名」「李四 回复：我也去」。
     *  @param msgtype 消息类型：message_type_thread_post 或 message_type_thread_reply
     *  @param kind    帖子动态卡片的 kind（thread_reply_kind）；帖子卡片忽略
     *  @param arg     接在后面的文字：帖子标题（没有标题时为正文摘要）或留言、回复、补充的内容摘要；可为空
     *  @param deleted 卡片已改写为已删除形态（帖子卡片与动态卡片都可能，docs/帖子群方案.md 14.6）
     *  @return 摘要（UTF-8），如「发帖：周五团建报名」「留言：我也去」「补充了帖子：报名截止改到周四」「帖子已删除」
     *          「留言已删除」
     */
    static std::string threadCardDigest(int msgtype, int kind, const std::string& arg, bool deleted);

    /**
     *  从帖子卡片或帖子动态卡片的 content 取出摘要所需的字段（与服务端 MessageProtocol 的 threadCardDigestArg 同口径：
     *  帖子卡片取标题，没有标题时取正文摘要；动态卡片取内容摘要，为空时取帖子标题；已删除时都为空）。
     *  @param msgtype  消息类型
     *  @param content  content 原文（JSON）
     *  @param arg      [出参] 摘要里接在后面的文字
     *  @param kind     [出参] 动态卡片的 kind；帖子卡片为 0
     *  @param postId   [出参] 帖子 ID；取不到时为 0
     *  @param deleted  [出参] 卡片是否已改写为已删除形态（帖子卡片与动态卡片）
     *  @return content 是 JSON 对象时返回 true
     */
    static bool parseThreadCardContent(int msgtype, const std::string& content, std::string& arg, int& kind,
                                       int64_t& postId, bool& deleted);

    /**
     *  一张帖子卡片或帖子动态卡片有没有 @ 到某人（看 MessageNotify::m_threadMentions；卡片 @ 了所有人
     *  （m_threadMentionAll）时对任何人都返回 true，本人是不是发布者由调用方判断）。
     *  @param notify 已解析的消息；不是这两种卡片时返回 false
     *  @param userId 要判断的人（通常是本人）
     */
    static bool threadCardMentions(const MessageNotify& notify, int64_t userId);

    /**
     *  通话记录卡片（message_type_rtc）的摘要：群通话卡片写「[群视频通话]」「[群语音通话]」，一对一通话卡片写「[视频通话]」
     *  「[语音通话]」，与服务端 buildMessageDigest 一致；content 两者都不是或解析不了时写「[通话]」。会话列表与消息记录窗共用。
     *  @param contentJson 卡片 content 原文（UTF-8 JSON）
     *  @return UTF-8 摘要（已按当前界面语言翻译）
     */
    static std::string groupCallDigest(const std::string& contentJson);

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
     *  构造删除单条消息的请求 JSON（cmd_delete_message=1512），形如 { "msgid": "..." }。
     *  删除是本人视角的软删除，对方不受影响。
     *  @param msgid 要删除的消息的服务器 msgid；须 >0
     *  @return 紧凑格式的请求 JSON 文本；msgid 非正时返回空串（调用方不应发送）
     */
    static std::string buildDeleteMessageRequest(int64_t msgid);

    /**
     *  解析删除单条消息的应答 JSON（cmd_delete_message=1512 的应答）。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 m_code / m_msg 有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseDeleteMessageResponse(const std::string& jsonbody, DeleteMessageResponse& resp);

    /**
     *  构造按时间范围清理聊天记录的请求 JSON（cmd_delete_messages_by_time=1521），
     *  形如 { "begintime": 毫秒, "endtime": 毫秒 }，两端都包含。
     *  @param begintime 区间开始，Unix毫秒；0 表示从最早开始；须 >=0
     *  @param endtime   区间结束，Unix毫秒；须不早于 begintime
     *  @return 紧凑格式的请求 JSON 文本；开始为负或结束早于开始时返回空串（调用方不应发送）
     */
    static std::string buildDeleteMessagesByTimeRequest(int64_t begintime, int64_t endtime);

    /**
     *  解析按时间范围清理的应答 JSON（cmd_delete_messages_by_time=1521 的应答）。
     *  失败应答只带 code / msg，此时起止时间保持为 0。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时各字段有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseDeleteMessagesByTimeResponse(const std::string& jsonbody,
                                                  DeleteMessagesByTimeResponse& resp);

    /**
     *  解析"本人删除了消息"的推送 JSON（cmd_messages_deleted_notify=1522），
     *  与服务端 MessageProtocol::buildMessagesDeletedNotify / buildMessagesDeletedRangeNotify
     *  / buildMessagesClearedNotify 互为镜像。带 clearedmsgid 的是清空形式，带 msgids 的是单条形式，
     *  否则按区间形式解析。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时对应形式的字段有效
     *  @return 解析成功返回 true。以下情形返回 false（此时 notify 内容未定义，调用方不得使用）：
     *          JSON 非法或不是对象；单条形式 cid 非正、msgids 为空或含非正的 msgid；
     *          区间形式缺少起止时间、时间为负或结束早于开始；清空形式会话类型不是 1 / 2、
     *          会话ID非正或锚点为负
     */
    static bool parseMessagesDeletedNotify(const std::string& jsonbody, MessagesDeletedNotify& notify);

    /**
     *  构造按会话清空聊天记录的请求 JSON（cmd_clear_conversation=1523）：{ "ctype": 1|2, "cid": "..." }。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   本人视角的会话ID（单聊为对方 userid，群聊为群 id），须为正
     *  @return 紧凑格式的请求 JSON 文本；会话类型不是 1 / 2 或会话ID非正时返回空串（调用方不应发送）
     */
    static std::string buildClearConversationRequest(int ctype, int64_t cid);

    /**
     *  解析按会话清空聊天记录的应答 JSON（cmd_clear_conversation=1523 的应答）。
     *  失败应答只带 code / msg，此时会话与锚点保持为 0。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时各字段有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseClearConversationResponse(const std::string& jsonbody,
                                               ClearConversationResponse& resp);

    /**
     *  解析"对方已读你的消息"的推送 JSON 包体（cmd_message_read_notify=1507）。
     *  与服务端 MessageProtocol::buildMessageReadNotify 互为镜像，报文格式见
     *  docs/协议设计-v2.md 10.5 节。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时各字段有效
     *  @return 解析成功返回 true；JSON 非法、或 cid / readmsgid 缺失或非正时返回 false
     *          （此时 notify 内容未定义，调用方不得使用）
     */
    static bool parseMessageReadNotify(const std::string& jsonbody,
                                       MessageReadNotify& notify);

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
     *  解析「一对一通话卡片已改写」的推送 JSON 包体（cmd_call_card_notify=1717）：{ sessionid, msgid, cid, content }。
     *  与服务端 CallProtocol::buildCardNotify 互为镜像。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 true 时各字段有效，content 为紧凑 JSON 原文
     *  @return 解析成功返回 true；JSON 非法、msgid 或 cid 缺失或非正、content 不是 JSON 对象时返回 false
     */
    static bool parseCallCardNotify(const std::string& jsonbody, CallCardNotify& notify);

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
     *  构造转发消息的请求 JSON 包体（cmd_forward_message=1511，紧凑 UTF-8）。
     *  与服务端 MessageProtocol::parseForwardMessageRequest 互为镜像。
     *  @param clientmsgid 本次转发的客户端ID（非空）；逐条转发时服务端按 "clientmsgid-序号" 派生每条
     *  @param toctype     目标会话类型：1 单聊、2 群聊
     *  @param tocid       目标会话ID：单聊为对方 userid，群聊为群 id（>0）
     *  @param merge       true 表示合并转发为一条聊天记录，false 表示逐条转发
     *  @param title       合并转发的标题（UTF-8）；仅 merge 为 true 且非空时写入请求，
     *                     为空时由服务端取缺省值，超长由服务端截断
     *  @param msgids      被转发的消息 msgid，条数 1 ~ kForwardMaxMessages
     *  @return 请求包体；clientmsgid 为空、tocid 非正、msgids 为空或超过上限时返回空串，
     *          调用方据此放弃发送
     */
    static std::string buildForwardMessageRequest(const std::string& clientmsgid, int toctype,
                                                  int64_t tocid, bool merge,
                                                  const std::string& title,
                                                  const std::vector<int64_t>& msgids);

    /**
     *  解析转发消息的应答 JSON 包体（cmd_forward_message=1511 的应答）。
     *  messages 里单条解析失败的元素跳过、不影响其余，口径与 parseSyncMessagesResponse 一致；
     *  缺 msgids / messages 按空列表处理，不算失败。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseForwardMessageResponse(const std::string& jsonbody,
                                            ForwardMessageResponse& resp);

    /**
     *  解析合并转发聊天记录的 content（MessageNotify::m_mergedContent）。
     *  子消息里解析失败的元素（如缺 msgid / from 的脏数据）跳过，不影响其余。
     *  @param contentJson content 原文（紧凑 JSON）
     *  @param out         [出参] 标题与子消息
     *  @return content 是 JSON 对象且含 messages 数组时返回 true；否则返回 false，
     *          调用方按"内容不可用"降级显示
     */
    static bool parseMergedContent(const std::string& contentJson, MergedRecord& out);

    /**
     *  解析文件共享卡片的 content（MessageNotify::m_shareContent）。字段与服务端 FileShareProtocol::buildCardContent /
     *  augmentCardContent 逐字对应（docs/文件共享方案.md 5.6）。
     *  @param contentJson content 原文（紧凑 JSON）
     *  @param out         [出参] 卡片内容
     *  @return shareid、ownerid 大于 0、kind 合法、name 为字符串时返回 true；否则返回 false，调用方按「内容不可用」降级显示
     */
    static bool parseShareCardContent(const std::string& contentJson, ShareCardContent& out);

    /**
     *  解析屏幕共享卡片的 content（MessageNotify::m_screenShareContent）。字段与服务端
     *  ScreenShareProtocol::buildCardContent 逐字对应（docs/协议设计-v2.md 第 17 节）：64 位 id 与时间以字符串编码，也接受整数写法。
     *  @param contentJson content 原文（紧凑 JSON）
     *  @param out         [出参] 卡片内容
     *  @return roomid 为非空字符串、host 大于 0 时返回 true；否则返回 false，调用方按「内容不可用」降级显示
     */
    static bool parseScreenShareCardContent(const std::string& contentJson, ScreenShareCardContent& out);

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
