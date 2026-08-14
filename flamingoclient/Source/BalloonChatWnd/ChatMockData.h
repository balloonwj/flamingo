/**
 *  ChatMockData.h
 *
 *  单聊窗的 mock 数据 —— 聊天双方信息 + 会话消息列表。数据取自 design 的
 *  data.js（联系人 c1 苏沐辰 + MESSAGES.c1）。本阶段(P1b)用 mock 数据
 *  驱动单聊窗视觉，真实消息收发的接入留作后续阶段。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString / COLORREF）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <vector>
#include <string>

#include "../net/MessageProtocolClient.h"   // net::TextFont（每条文本消息的字体设置）

namespace balloonchatwnd {

/**
 *  在线状态。
 */
enum ChatStatus
{
    kChatOnline  = 0,   // 在线
    kChatAway    = 1,   // 离开
    kChatBusy    = 2,   // 忙碌
    kChatOffline = 4,   // 离线
};

/**
 *  聊天一方（对方 / 自己）的简要信息。
 */
struct ChatPeer
{
    CString  name;         // 昵称
    CString  avatarText;   // 头像内显示的字
    COLORREF avatarColor;  // 头像底色
    CString  signature;    // 个性签名
    int      status;       // 在线状态，取值见 ChatStatus
};

/**
 *  消息类型。
 */
enum ChatMsgKind
{
    kMsgTime   = 0,   // 居中时间分隔
    kMsgText   = 1,   // 文字
    kMsgEmoji  = 2,   // 大 emoji
    kMsgImage  = 3,   // 图片
    kMsgFile   = 4,   // 文件
    kMsgFolder = 5,   // 文件夹整体传输。与 kMsgFile 是两回事：它不走 cmd_send_message、
                      // 不进服务端消息表，只是本端会话里的一条本地气泡（退出登录后不再
                      // 出现），承载「邀请 → 应答 → 建目录 → 传字节 → 收尾」整条链路的
                      // 界面表达。设计见 docs/单聊文件夹传输方案.md 第 7.2 节
};

/**
 *  一条会话消息。kind 决定哪些字段有效。
 *
 *  本结构体的每个内置类型成员都必须给出默认值。构造方往往只填自己那一类消息用得上的
 *  字段（例如文件夹气泡只填 folderXxx 那一批），缺默认值的成员就会保留栈上的垃圾值，
 *  而气泡是按这些标志决定画什么的 —— 2026-08-14 实测到的现象是文件夹气泡左侧凭空多出
 *  一条引用竖线：hasQuote 当时没有默认值，取到真值就把引用块的竖线画了出来，而引用
 *  内容是空的，于是只剩一根孤零零的竖条。新增成员时请一并给默认值。
 */
struct ChatMessage
{
    int      kind = 0;      // 取值见 ChatMsgKind
    bool     fromMe = false;  // true=自己发,false=对方发（kMsgTime 忽略）

    CString  text;          // kMsgText/kMsgEmoji 的内容；kMsgTime 的文字

    // 本条文本消息里的 @ 片段（仅 kMsgText 用，按出现顺序）。偏移是 text 的
    // UTF-16 字符口径，由收发两侧在组装本结构时用 ConvertSpansForRender 从协议的
    // UTF-8 字节口径换算好，并顺带标好 m_isMe —— 气泡只管按它画高亮，不再查身份。
    //
    // 为空 = 本条不含 @：气泡走原有的整段 DrawText 快路径与渲染缓存，一行不变。
    // 非空才切到 GDI+ 排版路径（只有它能按字符区间要到高亮矩形）。
    // 方案见 docs/群聊@提醒方案.md。
    std::vector<RenderSpan> ats;

    // 文本消息字体（仅 kMsgText 用）。font.m_valid 为 true 时气泡按此字体（字号/
    // 粗体/斜体/下划线/删除线/颜色）渲染；否则按气泡默认外观。随消息收发与历史，
    // 使对端看到相同效果。
    net::TextFont font;

    // kMsgImage
    CString  imageCaption;  // 图片占位标签（真实图片渲染时一般留空，由 imageState 决定覆盖文字）
    int      imageW = 0;    // 图片显示宽（已按比例缩放后的展示像素，供气泡布局）
    int      imageH = 0;    // 图片显示高（同上）

    // kMsgImage 真实渲染 / 发送态（v2 聊天图片）。imagePath 非空时 ChatBubble
    // 解码该文件按 imageW×imageH 缩放绘制；否则回退到占位块。
    CString     imagePath;       // 本地图片完整路径（发送=所选文件，接收=下载缓存）
    // 原图的 fileid 与 md5（来自协议 content）。气泡自己画图只用 imagePath，这两项是
    // 给引用用的：别人引用本条消息时要把它们写进 content.quote，收方才能按 md5 找本地
    // 缓存、或按 fileid 去下载，从而在引用块里画出缩略图。缺了它们，引用一张图发出去
    // 就只剩一句占位文字，对方的引用块永远是空的占位块。
    CString     imageFileId;     // 原图 fileid（字符串编码）
    CString     imageMd5;        // 原图内容 MD5（小写十六进制）
    std::string clientMsgId;     // 关联本端"发送中"气泡，与发图进度/结果/回执对回（UTF-8）
    int         imageState = 0;  // 图片态：0 正常/已就绪，1 上传中，2 失败，3 下载中
    int         imageProgress = 0;  // 上传进度 0-100（仅 imageState==1 时有意义）
    long long   msgid = 0;       // 服务器消息ID；收到的图片按它与下载结果对回（0=本端发出/无）

    // ---- kMsgFile —— 真实收发态（v2 聊天文件，msgtype=3）。与 kMsgImage 的状态字段
    //      对齐：发送方按 fileClientMsgId、接收方按 fileMsgid 与进度 / 结果回投对回。----
    CString     fileName;            // 文件名（含扩展名，显示用）
    CString     fileSize;            // 文件大小文字（如 "4.2 MB"，由 fileSizeBytes 格式化）
    CString     fileType;            // 文件类型小写扩展名（如 "pdf"/"docx"；空=无扩展名）
    int         fileProgress = 0;    // 传输进度 0-100（仅 fileState 为上传中 / 下载中时有意义）
    int         fileState = 0;       // 文件态：0 完成/已就绪，1 上传中，2 失败，3 下载中，4 已取消，
                                     // 5 已删除/失效，6 待接收（收到但未下载），7 已拒绝接收
    std::string fileClientMsgId;     // 关联本端"发送中"气泡（UTF-8），与发文件进度 / 结果 / 回执对回
    long long   fileMsgid = 0;       // 服务器消息ID；收到的文件按它与下载进度 / 结果对回（0=本端发出/无）
    CString     fileLocalPath;       // 本地文件完整路径（发送=所选文件；接收完成=下载落地路径；空=未就绪）
    std::string fileFileId;          // 文件ID（接收方触发下载用，来自消息 content）
    std::string fileMd5;             // 文件内容MD5（下载缓存命中检查 / 校验用）
    long long   fileSizeBytes = 0;   // 文件字节数（格式化大小文字、下载缓存命中检查用）
    // 对端把这个文件收到什么程度了（仅本端发出的文件消息有意义）。取值见 net 的
    // enum file_ack_state：1 对方正在接收、2 对方已接收、3 对方已拒绝；0=还没有任何动作。
    // 与 fileState 是两回事：fileState 说的是"本端这一侧传得怎么样"，本字段说的是
    // "对端收得怎么样"，两者同时存在（自己传完了 ≠ 对方收了）。
    int         filePeerState = 0;

    // ---- kMsgFolder —— 文件夹整体传输态。状态取值与「状态 → 可用操作」的映射都在
    //      ChatFolderStateModel.h（ChatFolderState / FolderStateHasProgress /
    //      FolderStateButtonCount / FolderButtonActions / FolderRetryEnabled），气泡
    //      只照结果画。本组字段仅 kind == kMsgFolder 时有效，其余消息一律留默认值。----
    CString     folderName;              // 文件夹名（只取所选目录的最后一级名字，显示在标题行）
    int         folderFileCount = 0;     // 文件夹内的文件总数（含各级子目录里的文件），单位：个
    int         folderDirCount = 0;      // 文件夹内的子目录总数（含空目录，不含根目录自身），单位：个
    long long   folderSizeBytes = 0;     // 文件夹内全部文件的字节数之和
    CString     folderSize;              // 总大小文字（如 "2.4 GB"），由 folderSizeBytes 经 FormatFileSize 格式化，
                                         // 气泡直接画它，不在绘制时重复格式化
    int         folderState = 0;         // 传输状态，取值见 ChatFolderState。0 不是合法状态（ChatFolderState 从 1 起），
                                         // 出现 0 说明宿主漏填，此时气泡按"无进度条、无按钮"退化处理
    int         folderProgress = 0;      // 整体进度 0-100（按字节数算）。仅 FolderStateHasProgress(folderState)
                                         // 为真时有意义；失败 / 取消后停在中断处不再变
    long long   folderSentBytes = 0;     // 本次传输已发出 / 已收到的字节数（断点续传时从续传位点继续累加）
    long long   folderTotalBytes = 0;    // 本次传输应传的总字节数。断点续传时它小于 folderSizeBytes ——
                                         // 已传过的部分不再重传，故进度条按本字段而不是按 folderSizeBytes 算
    CString     folderCurFile;           // 当前正在传的那个文件的文件名（不含路径）。仅传输中有意义，
                                         // 其余状态留空，状态行相应地不显示文件名
    int         folderCurIndex = 0;      // 当前传的是第几个文件（从 1 起），与 folderFileCount 一起拼成
                                         // "第 i / 共 n 个"。0=尚未开始传第一个文件，状态行不显示序号
    int         folderErrCode = 0;       // 失败原因错误码，取值见 net/Protocol.h 的 error_code_folder_*（606~620）。
                                         // 0=没有失败原因（正常流程，或宿主未记录），此时气泡不画失败原因那一行；
                                         // 非 0 时经 FolderErrorText 映射成中文展示文案
    std::string folderSessionId;         // 本次文件夹传输的会话 id（UTF-8）。服务端在应答里分配，此后
                                         // 所有信令都带着它；上层按它在消息列表里定位到本条气泡再更新状态
    CString     folderLocalRoot;         // 本地根目录完整路径。发送端是所选源目录，接收端是落地目录；
                                         // 「打开文件夹」按钮据它在资源管理器里打开。空=尚未确定（如邀请还没被接受）
    bool        folderWithinRetry = false;  // 本次会话是否仍在服务端的可重试保留期内（folderretrywindow，默认 10 分钟）。
                                            // 由宿主按失败时刻与保留期时长算出；气泡把它交给 FolderRetryEnabled
                                            // 决定「重试」按钮画成可用还是不可用

    // 引用回复（气泡上方的引用块）
    // ---- 引用回复（气泡上方的引用块）----
    // hasQuote 为假时其余 quote* 字段无意义。协议侧对应 content.quote，读写见
    // net::ReplyQuote 与 MessageProtocolClient 的 readQuoteFromContent。
    // 存的是<u>发送时的快照</u>而不是只存 msgid：原消息可能不在已加载范围内、也可能
    // 已被撤回，只有 msgid 的话引用块什么都显示不出来。
    bool     hasQuote = false;  // 是否带引用
    CString  quoteName;     // 被引用者显示名快照（发送时定格，群名片改了也不影响）
    CString  quoteText;     // 被引用消息的摘要快照

    long long quoteMsgid = 0;    // 被引用消息的服务器 msgid；点引用块据它定位原消息。0=无
    long long quoteFrom = 0;     // 被引用消息的发送者 userid
    int       quoteMsgType = 0;  // 被引用消息的类型（message_type_*），决定引用块画文字还是缩略图

    // ---- 仅被引用消息为图片时有效：引用块里画缩略图要用 ----
    CString  quoteImagePath;  // 缩略图本地路径（缓存命中才非空；空=未就绪，先画占位）
    CString  quoteFileId;     // 原图 fileid，未命中缓存时据它发起下载
    CString  quoteMd5;        // 原图 MD5，缓存命中检查用
    int      quoteImageW = 0;     // 原图宽（像素），按引用块高度等比缩放用
    int      quoteImageH = 0;     // 原图高（像素）

    // 本条是否被 Pin 住。Pin 是<u>会话内共享</u>的状态：单聊里对方也看得到，群里全体
    // 成员都看得到，由服务端 cmd_message_pinned_notify 推送维护，本端不做乐观更新。
    // 为真时气泡右上角画一枚小图钉，右键菜单里那一项文案变为「取消 Pin」。
    bool     pinned = false;

    bool     read = false;  // 自己消息是否已读（显示"✓ 已读"）

    // ---- 群聊发送者（单聊消息留空 —— 此时气泡不显示发送者名、头像
    //      回退到单聊对象 ChatPartner）----
    CString  senderName;        // 发送者昵称
    CString  senderAvatarText;  // 发送者头像字
    COLORREF senderAvatarColor = 0; // 发送者头像底色（仅 senderName 非空时有效）

    // ---- 发送者真实图片头像（facetype=2）。两字段配合：senderUserId 用于真图下载
    //      完成后按发送者定位本气泡刷新；senderAvatarImagePath 非空时气泡画真图、
    //      否则回退画 senderAvatarText 字形。----
    int64_t  senderUserId = 0;       // 发送者 userid（0=未知；真图下载完成按它刷新本行）
    CString  senderAvatarImagePath;  // 发送者真图本地路径（空=画字形，非空=画真图）

    // 消息发出时间（Unix 毫秒，服务器时间）。用于判断本条是否还在可撤回时限内 ——
    // 右键菜单据此决定要不要出现"撤回"项（见 net::MessageProtocolClient::canRecallMessage）。
    // 0=未知（历史 mock 数据、或尚未从服务端拿到时间），此时一律视为不可撤回。
    long long timestampMs = 0;
};

/**
 *  聊天对方（mock，design data.js 的 c1 苏沐辰）。
 */
const ChatPeer& ChatPartner();

/**
 *  当前用户自己（mock，design data.js 的 ME 林知夏）。
 */
const ChatPeer& ChatMe();

/**
 *  会话消息列表（mock，design MESSAGES.c1）。
 */
const std::vector<ChatMessage>& ChatMessages();

/**
 *  构造一条文字消息（运行时新增消息用，其余字段置默认值）。
 *    fromMe：true=自己发，false=对方发。
 *    text：  消息文字。
 */
ChatMessage MakeTextMessage(bool fromMe, const CString& text);

/**
 *  构造一条图片消息（运行时新增消息用，其余字段置默认值）。
 *    fromMe：     true=自己发，false=对方发。
 *    imagePath：  本地图片完整路径（用于 ChatBubble 解码渲染）。
 *    dispW/dispH：气泡内图片的展示像素尺寸（调用方按真实宽高比缩放后传入）。
 *    clientMsgId：客户端消息ID（UTF-8），与发图进度/结果/回执对回；可空。
 *    state：      初始发送态（0 正常/已接收，1 上传中，2 失败）。
 */
ChatMessage MakeImageMessage(bool fromMe, const CString& imagePath,
                             int dispW, int dispH,
                             const std::string& clientMsgId, int state);

/**
 *  构造一条"下载中"的收到图片消息（运行时收到 msgtype=2 时用）。imagePath 暂空、
 *  imageState=3（下载中），待下载完成后由 ChatBubble::SetImageDownloaded 填真实
 *  本地路径再渲染。
 *    fromMe：     true=自己发（历史里自己发的图也需下载），false=对方发。
 *    dispW/dispH：气泡内图片展示像素尺寸（按消息里的宽高比缩放后传入）。
 *    msgid：      服务器消息ID，与下载结果对回。
 */
ChatMessage MakeDownloadingImageMessage(bool fromMe, int dispW, int dispH, long long msgid);

/**
 *  构造一条文件消息（运行时新增消息用，其余字段置默认值）。大小文字与类型标签由
 *  sizeBytes / fileName 经 FileBubbleFormat 自动算出。
 *    fromMe：     true=自己发（右侧气泡），false=对方发（左侧气泡）。
 *    fileName：   文件名（含扩展名，显示用）。
 *    sizeBytes：  文件字节数。
 *    localPath：  本地文件完整路径（发送=所选文件；可空）。
 *    clientMsgId：客户端消息ID（UTF-8），与发文件进度 / 结果 / 回执对回；可空。
 *    state：      初始态（0 完成/已就绪，1 上传中，2 失败）。
 */
ChatMessage MakeFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                            const CString& localPath, const std::string& clientMsgId, int state);

/**
 *  构造一条"下载中"的收到文件消息（运行时收到 msgtype=3 时用）。fileLocalPath 暂空、
 *  fileState=3（下载中），待下载完成后由 ChatBubble::SetFileDownloaded 填真实本地路径。
 *    fromMe：    true=自己发（历史里自己发的文件也可能需下载），false=对方发。
 *    fileName：  文件名（含扩展名）。
 *    sizeBytes： 文件字节数。
 *    msgid：     服务器消息ID，与下载进度 / 结果对回。
 *    fileId：    文件ID（触发下载用，来自消息 content）。
 *    md5：       文件内容MD5（缓存命中检查 / 校验用）。
 */
ChatMessage MakeDownloadingFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                                       long long msgid, const std::string& fileId,
                                       const std::string& md5);

/**
 *  构造一条尚未落地的文件消息，初始态由调用方指定。MakeDownloadingFileMessage 是它
 *  state=3 的特例。收到文件消息时用 state=6（待接收）：只渲染气泡、不自动下载，等用户
 *  点「接收」再发起——避免替用户决定要不要收，也避免自己发出的文件在翻历史时被重新拉一遍。
 *    fromMe：    true=自己发（历史里自己发的文件本地缓存也可能没有），false=对方发。
 *    fileName：  文件名（含扩展名）。
 *    sizeBytes： 文件字节数。
 *    msgid：     服务器消息ID，与下载进度 / 结果对回。
 *    fileId：    文件ID（触发下载用，来自消息 content）。
 *    md5：       文件内容MD5（缓存命中检查 / 校验用）。
 *    state：     初始文件态，取值同 ChatMessage::fileState（6=待接收、3=下载中）。
 */
ChatMessage MakeIncomingFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                                    long long msgid, const std::string& fileId,
                                    const std::string& md5, int state);

/**
 *  取一条 mock 自动回复文字。
 *    index：轮转下标（对回复条数取模）。
 */
CString MockAutoReply(int index);

// =====================================================================
// 群聊 mock 数据
// =====================================================================

/**
 *  群信息（mock，design 08 的「FlamingoIM 项目组」）。
 */
struct GroupInfo
{
    CString name;         // 群名
    int     memberCount;  // 群成员总数
    CString notice;       // 群公告
};

/**
 *  群成员（mock）。
 */
struct GroupMember
{
    CString  name;         // 成员昵称
    CString  avatarText;   // 头像内显示的字
    COLORREF avatarColor;  // 头像底色
    bool     isOwner;      // 是否群主
    int      status;       // 在线状态，取值见 ChatStatus
};

/**
 *  群信息（mock）。
 */
const GroupInfo& MockGroupInfo();

/**
 *  群成员列表（mock，design 08 边栏可见的 9 名成员）。
 */
const std::vector<GroupMember>& MockGroupMembers();

/**
 *  群会话消息列表（mock，design 08 的群对话片段）。
 */
const std::vector<ChatMessage>& MockGroupMessages();

/**
 *  取一条 mock 群自动回复消息（由某位群成员发出，index 轮转选成员与
 *  回复文字）。
 */
ChatMessage MakeGroupAutoReply(int index);

} // namespace balloonchatwnd
