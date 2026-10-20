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

#include "../../net/MessageProtocolClient.h"   // net::TextFont（每条文本消息的字体设置）
#include "../../Common/Code/CodeNotes.h"       // codesnippet::CodeNote（代码片段的批注）

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
    kMsgMerged = 6,   // 合并转发的聊天记录（msgtype=15）：画成「标题 + 前几条摘要 + 共 N 条」的卡片，
                      // 点击打开聊天记录查看窗。设计见 docs/聊天消息转发方案.md §3.5
    kMsgRelay  = 7,   // 群接龙卡片（msgtype=11）：画成「标题 + 说明 + 前几条接龙 + 状态行 + 参与按钮」的卡片，
                      // 点击打开接龙详情窗。设计见 docs/群接龙方案.md §3.2
    kMsgFileShare = 8,  // 文件共享卡片（msgtype=20）：画成「类型图标 + 名称、显示路径、某某的共享 · 状态、打开 / 下载按钮」
                        // 的卡片，点击经 FlamingoFileShare.dll 打开浏览窗口。设计见 docs/文件共享方案.md 7.3
    kMsgScreenShare = 9,  // 屏幕共享卡片（msgtype=19）：画成「图标 + 屏幕共享、某某发起的共享、状态、加入按钮」的卡片，
                          // 点击经 FlamingoMedia.dll 加入共享。设计见 docs/屏幕共享与录制方案.md 5.2 第 4 步
    kMsgVoice = 10,       // 语音消息（msgtype=4）：画成「播放图标 + 波形 + 时长」的气泡，宽度随时长变化，对方发来且未听过的
                          // 在气泡外侧画红点；点击播放 / 停止。设计见 docs/语音消息方案.md 第 3.2 节
    kMsgGroupInvite = 11, // 入群邀请卡片（msgtype=21，单聊）：画成「图标 + 「张三」邀请你加入群聊、群名 · N 名成员、状态、加入群聊按钮」
                          // 的卡片，与屏幕共享卡片同一套几何；按钮只在被邀请人一侧、待加入时出现。设计见 docs/邀请入群方案.md §3.3
    kMsgSecretInvite = 12, // 密聊邀请卡片：本地气泡，不落库，按会话号定位（照 kMsgFolder 的做法）。被邀请方画「张三 邀请你进入密聊」与
                           // 「同意」「忽略」两个按钮，发起方画「等待对方同意」与「取消」按钮，都带剩余秒数；与入群邀请卡片同一套几何。
                           // 设计见 docs/密聊方案.md 4.8
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

    // 是否为消息列表自动插入的时间分隔行（仅 kMsgTime 用，2026-10-14）。为 true 时 timestampMs 是这一行所标注的那条消息的
    // 时间，本地日期变化后消息列表据此重新格式化 text（如「21:30」变为「昨天 21:30」）；系统提示、撤回提示等其他 kMsgTime 行为 false
    bool     isTimeSeparator = false;

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

    // 是否为 Markdown 消息（仅 kMsgText 用，协议 content.format 为 "markdown"）。为 true 且
    // FlamingoMarkdown.dll 可用时，气泡把 text 当 Markdown 源文本渲染（只取 font 的字号与颜色作正文基准）；
    // DLL 不可用时与普通文字消息完全相同，显示源文本。kind 仍是 kMsgText，转发、引用、菜单等照文字消息处理。
    // 方案见 docs/Markdown消息方案.md。
    bool isMarkdown = false;

    // 是否为代码片段（仅 kMsgText 用，协议 content.format 为 "code"）。为 true 时 text 是代码原文（换行为 \n，Tab 原样），
    // 气泡画成代码卡片（ChatCodeBlock），不解析表情标记，也不走 Markdown 分支；kind 仍是 kMsgText，转发、引用、
    // 撤回等照文字消息处理。方案见 docs/代码片段方案.md。
    bool isCode = false;

    // 代码片段的语言（仅 isCode 为 true 时有效），取值见 codesnippet::CodeLanguage；协议里不认识的语言标识为纯文本（0）
    int codeLang = 0;

    // 代码片段的批注（仅 isCode 为 true 时有效，docs/代码片段方案.md 第 10.5 节），已经过 SanitizeCodeNotes 整理：
    // 按起始行升序、互不相交；卡片据此标出被批注的行，代码查看窗在批注栏里列出说明
    std::vector<codesnippet::CodeNote> codeNotes;

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
    int         imageState = 0;  // 图片态：0 正常/已就绪，1 上传中，2 上传失败，3 下载中，4 下载失败
                                 // （取值有名字，见 ChatMessageMenu.h 的 kChatImageState*）
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
    // 该会话是否没有「接收 / 拒绝」流程。群聊里的文件消息（转发进群的文件）不需要谁来决定
    // 收不收，待接收态只给一个「下载」按钮、状态文字写"未下载"；单聊保持 false，走接收流程。
    bool        fileDirectDownload = false;
    // ---- 单聊单文件 P2P 直传（2026-10-04 起，docs/单聊文件P2P直传与P2P徽章方案.md）----
    bool        fileDirect = false;         // 是不是在线直传的文件（服务器上没有这个文件）；改为离线文件之后为 false
    std::string fileDirectSessionId;        // 直传的会话 id（UTF-8）；发送方在 1810 应答到达后才有，接收方从邀请推送里来
    int         fileLinkPath = 0;           // 直传数据实际走的 P2P 链路路径（1 局域网直连、2 打洞直连、3 中转）；0=没有。供徽章
    int         fileDirectEnd = 0;          // 直传作废的原因（folder_transfer_state 的取值），只在 fileState 为
                                            // kFileDirectToOffline 时有意义，决定状态文字

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
    int         folderLinkPath = 0;      // 数据实际走的 P2P 链路路径（2026-10-04 起，1/2/3）；0=没有走 P2P。供「P2P」徽章
    bool        folderWithinRetry = false;  // 本次会话是否仍在服务端的可重试保留期内（folderretrywindow，默认 10 分钟）。
                                            // 由宿主按失败时刻与保留期时长算出；气泡把它交给 FolderRetryEnabled
                                            // 决定「重试」按钮画成可用还是不可用

    // ---- 合并转发的聊天记录（kind == kMsgMerged）----
    // 由 ChatMergedCard 的 ApplyMergedCard 从 content 原文解析填写，气泡据此画卡片。
    CString              mergedTitle;          // 记录标题，如「张三和李四的聊天记录」
    std::vector<CString> mergedLines;          // 卡片上的预览行「发送者：摘要」，最多 kMergedCardPreviewLines 行
    int                  mergedCount = 0;      // 记录里的消息条数，画在卡片底部；0=内容不可用
    std::string          mergedContent;        // content 原文（紧凑 JSON，UTF-8），点卡片打开查看窗时原样交给它

    // ---- 群接龙卡片（kind == kMsgRelay）----
    // 由 ChatRelayCard 的 ApplyRelayCard 从 content 原文解析填写，气泡只照这些字段画卡片，
    // 不再自己解析 JSON、也不判断状态。收到接龙变更推送、或到了截止时间时由宿主整组重填。
    CString              relayTitle;           // 标题，单行；旧数据标题为空时为「接龙」
    CString              relayNote;            // 说明全文，可多行；卡片上最多显示两行，由气泡折行截断
    std::vector<CString> relayLines;           // 前 kRelayCardPreviewLines 条接龙，每条形如「1. 张三　本人+1」
    CString              relayStatus;          // 状态行，如「共 12 人参与 · 截止 10-03 18:00」
    int                  relayCount = 0;       // 参与人数（名单条数），为 0 时卡片写「还没有人参与」
    int                  relayState = 0;       // 按填写时刻的本机时间换算后的状态，取值见 enum relay_state
    int                  relayButton = 0;      // 底部按钮，取值见 ChatRelayCard.h 的 RelayCardButton；0=不显示
    long long            relayDeadline = 0;    // 截止时间，Unix 毫秒；0=不限。宿主据它判断何时要把卡片重填为已截止
    CString              relayDeadlineText;    // 卡片副标题「截止 10-16 14:05」（docs/聊天卡片美化方案.md 5.3）；不限截止时间时为空
    std::string          relayContent;         // 卡片当前内容（紧凑 JSON，UTF-8），已合并收到过的变更推送；
                                               // 再收到推送时在它的基础上合并，打开详情窗时原样交给详情窗
    CString              relayCopyBody;        // 「复制」要放进剪贴板的正文（标题、说明、完整名单，行与行之间用回车换行分隔），
                                               // 不含末行；末行「（共 N 人，截至 …）」写的是复制时刻，复制时现拼

    // ---- 文件共享卡片（kind == kMsgFileShare）----
    // 由 ChatFileShareCard 的 ApplyFileShareCard 从 content 原文解析填写，状态变化时由 ApplyFileShareState 重填；
    // 气泡只照这些字段画卡片，不自己解析 JSON、也不判断状态。
    long long            shareId = 0;          // 共享 id；0=内容不可用
    int                  shareKind = 0;        // 共享类型：1 磁盘 2 文件夹 3 文件（share_kind），决定标题前的图标与按钮文字
    CString              shareName;            // 共享的显示名，标题行
    CString              sharePath;            // 共享的显示路径，标题下一行（灰）；为空时不画这一行
    CString              shareOwnerLine;       // 「张三的共享」或「我的共享」
    CString              shareStatus;          // 状态文字，如「可用」「暂不可用：分享者不在线」
    int                  shareState = 0;       // 对本人的状态（share_state）；0=还不知道
    bool                 shareIsOwner = false; // 本人是不是分享者：分享者的卡片不显示按钮
    int                  shareButton = 0;      // 底部按钮，取值见 ChatFileShareCard.h 的 ShareCardButton；0=不显示
    bool                 shareButtonEnabled = false;   // 按钮是否可点：共享已失效、本客户端没有加载文件共享模块时不可点
    std::string          shareContent;         // content 原文（紧凑 JSON，UTF-8）

    // ---- 屏幕共享卡片（kind == kMsgScreenShare）----
    // 由 ChatScreenShareCard 的 ApplyScreenShareCard 从 content 原文解析填写，共享状态变化时由 ApplyScreenShareState 重填；
    // 气泡只照这些字段画卡片，不自己解析 JSON、也不判断状态。
    std::string          screenShareRoomId;    // 房间号；为空表示内容不可用
    CString              screenShareHostName;  // 发起人显示名，加入时用作观看窗标题
    CString              screenShareSubtitle;  // 标题下一行（灰）：「张三发起的共享」或「我发起的共享」
    CString              screenShareStatus;    // 状态文字：「正在共享」「共享已结束」「正在获取状态」
    int                  screenShareState = 0; // 房间状态，取值见 ScreenShareRoomState.h；0=还不知道
    bool                 screenShareHasButton = false;       // 是否显示「加入」按钮：本人发起的卡片与内容不可用的卡片不显示
    bool                 screenShareButtonEnabled = false;   // 按钮是否可点：共享已结束时不可点
    bool                 screenShareForced = false;          // 是否强制模式（第 13 期）：加入前先确认
    CString              screenShareNotice;    // 副标题下的说明（第 14 期）：强制模式时写明效果，最多两行；为空时不画这一块
    CString              screenShareTitle;     // 粗体标题行（第 15 期）：共享标题；第 15 期之前的卡片写「屏幕共享」
    CString              screenShareDesc;      // 共享描述（第 15 期）：副标题下的灰字，最多两行；为空时不画这一块
    std::string          screenShareContent;   // content 原文（紧凑 JSON，UTF-8）
    // 群通话卡片（msgtype=16，docs/群音视频通话方案.md 6.3）复用本组字段与屏幕共享卡片的版式：kind 同为 kMsgScreenShare，
    // 以下两项区分。由 ChatGroupCallCard 的 ApplyGroupCallCard 填写；状态取自群通话状态缓存（GroupCallRoomState）。
    bool                 screenShareIsGroupCall = false;   // 是否群通话卡片：标题前画电话或摄像机图标，「加入」走群通话
    bool                 screenShareIsCallCard = false;    // 是否一对一通话卡片（ChatCallCard）：标题前画电话或摄像机图标，没有按钮
    int                  groupCallMedia = 0;               // 群通话卡片或一对一通话卡片的媒体类型：1 语音、2 视频；都不是时为 0
    // 群公告卡片（msgtype=22，docs/群公告接入方案.md §13.4）与群备忘卡片（msgtype=23，docs/备忘与提醒方案.md §7.3）同样复用
    // 本组字段与屏幕共享卡片的版式（2026-10-05）：kind 同为 kMsgScreenShare，以下三项区分。由 ChatAnnouncementCard 的
    // ApplyAnnouncementCard、ChatMemoCard 的 ApplyMemoCard 填写；content 原文放在 screenShareContent，房间号 screenShareRoomId 为空
    bool                 screenShareIsAnnouncement = false; // 是否群公告卡片：标题前画喇叭，可点时点卡片请宿主打开公告详情
    bool                 screenShareIsMemo = false;         // 是否群备忘卡片：标题前画日历，可点时点卡片请宿主打开这条备忘
    // 云端录像卡片（msgtype=24，docs/屏幕共享与录制方案.md 12.6.5）也复用本组字段与版式（2026-10-06）：kind 同为 kMsgScreenShare，
    // 以下两项区分。由 ChatCloudRecordCard 的 ApplyCloudRecordCard 填写；录像 id 放在 infoCardRefId，房间号 screenShareRoomId 为空
    bool                 screenShareIsCloudRecord = false;  // 是否云端录像卡片：标题前画场记板（只有声音时画话筒），可点时点卡片请宿主播放
    bool                 cloudRecordAudioOnly = false;      // 云端录像是否只有声音（独立录制的 m4a 录音）
    long long            infoCardRefId = 0;                 // 群公告卡片的公告 id、群备忘卡片的备忘 id、云端录像卡片的录像 id；0=内容不可用。其余卡片恒为 0

    // ---- 入群邀请卡片（kind == kMsgGroupInvite，docs/邀请入群方案.md §3.3）。卡片要显示的文字由 ChatGroupInviteCard
    //      算好放在这里，ChatBubble 只管画。本组字段仅 kind == kMsgGroupInvite 时有效 ----
    long long            inviteId = 0;          // 邀请 id；0=内容不可用
    long long            inviteGroupId = 0;     // 目标群 id
    CString              inviteTitle;           // 粗体标题行：被邀请人一侧「「张三」邀请你加入群聊」，邀请人一侧「你邀请「李四」加入群聊」
    CString              inviteSubtitle;        // 标题下一行（灰）：待加入时「产品研发部周会群 · 32 名成员」，其余状态只写群名
    CString              inviteStatus;          // 状态行文字，如「等待对方确认」「已加入」「邀请已过期」
    int                  inviteState = 0;       // 显示状态（group_invite_state）
    bool                 inviteHasButton = false;   // 是否显示「加入群聊」按钮：只有被邀请人一侧、待加入时显示
    bool                 inviteJoining = false;     // 点了「加入群聊」、正等服务端应答：按钮写「正在加入…」且不可点
    std::string          inviteContent;         // content 原文（紧凑 JSON，UTF-8）

    // ---- 密聊（docs/密聊方案.md 4.7、4.8）----
    // 密聊消息（kMsgText / kMsgImage，isSecret 为 true）只存在于消息列表的内存里：没有右键菜单，不能复制、拖出、多选；
    // 图片的 imagePath 是 ChatImageCache 的内存图片伪路径（secretmem://…），不对应任何文件。退出密聊时整批清零后删除。
    bool                 isSecret = false;
    // 密聊邀请卡片（kind == kMsgSecretInvite）：会话号、卡片上的文字与按钮。文字由 ChatSecretMode 的 ApplySecretInviteCard 算好，
    // ChatBubble 只管画；按钮被点后经 WM_CHAT_SECRET_INVITE_ACTION 投给宿主聊天窗
    unsigned long long   secretSessionId = 0;      // 会话号；0=内容不可用
    CString              secretInviteTitle;        // 粗体标题行
    CString              secretInviteSubtitle;     // 标题下一行（灰）：说明文字；对方无法防截图时写明
    CString              secretInviteStatus;       // 状态行：「剩余 58 秒」
    int                  secretInviteButtons = 0;  // 按钮：0 不显示，1 只有「取消」（发起方），2 为「同意」「忽略」（被邀请方）

    // ---- 语音消息（kind == kMsgVoice，docs/语音消息方案.md 第 3.2 节）。由 ChatVoiceBubble 的 ApplyVoiceMessage 填写，
    //      播放、下载、发送时由聊天窗改状态字段；ChatBubble 只照这些字段画 ----
    std::string          voiceFileId;           // 语音文件 ID（字符串），下载时传给 cmd_request_download
    std::string          voiceMd5;              // 文件内容 MD5（小写十六进制），也是本机缓存文件名 ChatVoice\<md5>.ogg
    int                  voiceDuration = 0;     // 时长（整秒，1~120）
    long long            voiceSize = 0;         // 文件字节数
    std::vector<unsigned char> voiceWave;       // 波形值（每个 0~255）；消息里没有时为均匀波形（net::VoiceWaveForDisplay）
    bool                 voicePlayed = true;    // 本机是否已听过：对方发来的取本地库 played 列，本人发的一律为 true
    int                  voiceState = 0;        // 状态，取值见 ChatVoiceBubble.h 的 ChatVoiceState（就绪、发送中、下载中、下载失败）
    bool                 voicePlaying = false;  // 是否正在播放：图标随播放进度轮换声波道数
    int                  voicePlayFrame = 0;    // 播放中图标的帧号（0~2，对应 1~3 道声波）
    CString              voiceLocalPath;        // 本机文件路径（发送方是录音文件，接收方是下载到的缓存文件）；空串表示还没有

    // 接龙系统提示行（kind == kMsgTime，参与、修改、撤回、结束接龙时服务端发的提示）所属卡片的
    // 服务器 msgid，取自系统消息 content 的 cardmsgid。非 0 时提示行悬停显示手形、点击打开那张卡片
    // 的详情窗；0 表示普通提示行或时间分隔行，不可点击。
    long long            relayCardMsgid = 0;

    // 撤回提示行（kind == kMsgTime，本人撤回的文字类消息）的「编辑」（docs/撤回后重新编辑方案.md）：
    // recallDraftJson 是撤回那一刻存下的快照（UTF-8 JSON，格式见 RecallEdit.h 的 SerializeRecallDraft），
    // recallEditDeadlineMs 是「编辑」的到期时刻（Unix 毫秒）。两者都有效、且当前时间早于到期时刻时，提示行末尾
    // 画可点击的「编辑」；为空或 0 表示普通提示行。
    std::string          recallDraftJson;
    long long            recallEditDeadlineMs = 0;

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

    // 自己发出的消息对方是否已读（气泡下方显示"✓ 已读"还是"未读"，见 DecideReadReceipt）。
    // 构造时一律为 false，只能由对方的已读位点置为 true（ChatMessageList::ApplyPeerReadPosition），
    // 构造函数不得按 fromMe 预置 —— 那样给离线的人发消息也会显示已读。
    bool     read = false;

    // 本端发出的这条消息是否被服务端拒绝（发送回执失败，如包含敏感词、已被禁言）。为真时气泡左侧画红色
    // 感叹号、下方画 failReason。只由 ChatMessageList::MarkSendFailed 置位；失败的行没有服务器 msgid，
    // 因而没有已读回执，也不能撤回、转发、引用、Pin。见 docs/敏感词方案.md 第 8.3 节。
    bool     sendFailed = false;
    CString  failReason;    // 发送失败的原因（界面显示用，不带句号），sendFailed 为真时有效

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
    bool     senderIsBot = false;    // 发送者是机器人账号（群聊）：名字后面画「机器人」徽章（docs/机器人方案.md §4.2）

    // ---- 卡片的状态标签与信息行（docs/聊天卡片美化方案.md 3.4 节，2026-10-14）。由各卡片的 ApplyXxx 与状态刷新函数
    //      在写原有状态文字时一并填写，ChatBubble 只照它们画；原有的 xxxStatus 字段照旧保留（复制、摘要等处仍在用）----
    int      cardChipTone = 0;   // 状态标签的语义，取值见 ChatCardModel.h 的 ChatCardChipTone；0 表示不画标签
    CString  cardChipText;       // 状态标签的文字；为空时不画标签
    CString  cardMetaText;       // 标签右侧的信息行文字（时长、人数、大小）；为空时没有

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
