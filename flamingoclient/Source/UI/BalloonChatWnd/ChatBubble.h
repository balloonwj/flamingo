/**
 *  ChatBubble.h
 *
 *  单聊窗里的一条消息行 —— 对照 design 07 / chat.jsx 的 MessageBubble。
 *  一条消息一个 ChatBubble：可能是居中时间分隔，也可能是带头像的气泡
 *  （对方靠左 / 自己靠右），气泡内容支持文字 / emoji / 图片 / 文件 / 文件夹传输，
 *  气泡上方可带引用块、自己消息下方带"✓ 已读"回执。
 *
 *  文字气泡支持自动折行：超过气泡最大宽度的长文字在气泡内换行、气泡
 *  随之变高。折行布局由 Measure() 按消息区可用宽度测量，须在排版
 *  （GetRowHeight / OnPaint）之前调用。
 *
 *  本行还支持右键上下文菜单（复制 / 引用回复 / 转发 / 撤回 / 删除 /
 *  多选）；进入多选模式后行首显示勾选圈、左键点击即切换选中。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"
#include "ChatFolderStateModel.h"  // kMaxChatFolderActions（文件夹按钮命中矩形数组的长度）
#include "ChatTextSelection.h"   // SelLayout（成员需完整类型）
#include "ChatInlineText.h"      // InlinePlaced（正文排版结果缓存，成员需完整类型）
#include "ChatLinkDetect.h"      // LinkSpan（成员需完整类型）
#include "ChatClipboardContent.h" // ChatClipSegment（复制时写进剪贴板的聊天内容片段）
#include "../../Feature/Markdown/MarkdownDocument.h"   // CMarkdownDocument（Markdown 消息的 DLL 文档，成员需完整类型）
#include "ChatCodeBlock.h"         // CodeBlockContent / CodeBlockLayout（代码片段卡片，成员需完整类型）
#include <string>

//图片气泡解码用的 GDI+ 位图前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonchatwnd {

/**
 *  气泡下方回执行显示的内容。
 */
enum ChatReceiptKind
{
    kReceiptNone   = 0,   // 不显示：对方的消息、未开启回执的列表（群聊）、时间分隔行、
                          // 文件夹行，或尚未拿到服务器 msgid（发送中 / 发送失败）
    kReceiptUnread = 1,   // 显示「未读」：单聊里自己发出、服务端已确认、对方还没读到本条
    kReceiptRead   = 2,   // 显示「✓ 已读」：单聊里自己发出、对方的已读位点已越过本条
};

/**
 *  判定一条消息的气泡下方回执行显示什么。纯函数，不碰控件与 GDI。
 *
 *  规则（2026-09-30 起）：只有开启回执的列表（单聊）里、自己发出的、已拿到服务器
 *  msgid 的真实消息才显示回执，其中 read 为真显示「✓ 已读」、否则显示「未读」。
 *  在此之前气泡对所有自己发的消息无条件画「已读」，给离线的人发消息也显示已读。
 *    msg：           要判定的消息。
 *    receiptEnabled：所在列表是否开启已读回执（单聊开启；群聊按协议不做已读回执，不开启）。
 *  返回：回执行显示的内容，取值见 ChatReceiptKind。
 */
ChatReceiptKind DecideReadReceipt(const ChatMessage& msg, bool receiptEnabled);

/**
 *  ChatBubble —— 一条消息行（时间分隔 / 气泡消息）。自绘控件。
 */
class ChatBubble : public balloonwjui::DuiControl
{
public:
    ChatBubble();
    ~ChatBubble();

    // 设置本行承载的消息（拷贝一份）。
    void SetMessage(const ChatMessage& msg);

    // 取本行消息的客户端消息ID（图片发送态更新时按它定位本行）。空=无。
    const std::string& ClientMsgId() const { return m_msg.clientMsgId; }

    // 更新图片上传进度（0-100）并置为"上传中"态；触发重绘。
    void SetImageUploadProgress(int percent);

    // 把图片标为"上传/发送失败"态；触发重绘。
    void SetImageUploadFailed();

    // 把图片置为"正常"态（上传完成 / 接收完成）；触发重绘。
    void SetImageUploadDone();

    // 取本行消息的服务器 msgid（收到的图片下载完成后按它定位本行）。0=无。
    long long Msgid() const { return m_msg.msgid; }

    // 收图下载完成：填本地缓存路径、置正常态、丢弃旧位图缓存以重新解码；触发重绘。
    void SetImageDownloaded(const CString& localPath);

    // 收图下载失败：置"失败"态；触发重绘。
    void SetImageDownloadFailed();

    // ---- 语音气泡（kMsgVoice）的下载态更新（语音消息第 3d-3 步）----
    // 语音气泡高度不随状态变化，宿主更新后只需重绘、不必重排。

    // 语音文件下载完成：记下本地路径（ChatVoice\<md5>.ogg）、置就绪态；触发重绘。
    void SetVoiceDownloaded(const CString& localPath);

    // 语音文件下载失败：置下载失败态（点击时重新下载）；触发重绘。
    void SetVoiceDownloadFailed();

    // 下载失败的语音重新下载：置下载中；触发重绘（语音消息第 3e 步）。
    void SetVoiceDownloading();

    // 进入 / 退出播放中（语音消息第 3e 步）：进入时一并记为已听（未听红点消失）；退出时图标回到第一帧。触发重绘。
    void SetVoicePlaying(bool playing);

    // 播放中喇叭图标的帧号（0 ~ kVoicePlayFrameCount-1，见 ChatVoiceBubble.h）；触发重绘。
    void SetVoicePlayFrame(int frame);

    // 别的窗口里听过这条语音（语音消息第 6a 步）：记为已听，未听红点消失；触发重绘。
    void SetVoicePlayed();

    // ---- 文件气泡（kMsgFile）的收发态更新 ----
    // 注：文件气泡高度随状态变化（有无进度条 / 按钮行），故 ChatMessageList 在调用这些
    // 之后须 ForceLayout 重排（不能像图片那样只 Invalidate）。

    // 取本行文件消息的客户端消息ID（发送态更新时按它定位本行）。空=无。
    const std::string& FileClientMsgId() const { return m_msg.fileClientMsgId; }

    // 取本行文件消息的服务器 msgid（下载态更新时按它定位本行）。0=无。
    long long FileMsgid() const { return m_msg.fileMsgid; }

    // 取本行文件当前传输进度（0-100）。供列表按整数百分比节流强制重绘用。
    int FileProgress() const { return m_msg.fileProgress; }

    // 更新文件上传进度（0-100）并置"上传中"态；触发重绘。
    void SetFileUploadProgress(int percent);

    // 文件上传完成：置"完成"态、进度满格；触发重绘。
    void SetFileUploadDone();

    // 文件上传失败：置"失败"态（保留当前进度作红色停位）；触发重绘。
    void SetFileUploadFailed();

    // 更新文件下载进度（0-100）并置"下载中"态；触发重绘。
    void SetFileDownloadProgress(int percent);

    // 文件下载完成：填本地落地路径、置"完成"态；触发重绘。
    void SetFileDownloaded(const CString& localPath);

    // 文件下载失败：置"失败"态；触发重绘。
    void SetFileDownloadFailed();

    // 文件传输已取消：置"已取消"态（保留当前进度作灰色停位）；触发重绘。
    void SetFileCancelled();

    // 本条文件是否可"重新发送"（而非"重新下载"）：需同时具备与发送中气泡对回的
    // clientMsgId 和仍在原处的本地源文件，即只有本端刚发出的那笔才行；历史里自己
    // 发过的文件两者皆无，只能从服务器重新下载。决定重试按钮的文案与宿主的分派。
    bool CanResendFile() const;

    // 用户拒绝接收该文件：置"已拒绝"态（不下载、不通知发送方）；触发重绘。
    // 该态仍提供「重新下载」，用户反悔时可再收。
    void SetFileRejected();

    // 用户点「接收」/「重新下载」后真正发起了下载：置"下载中"态并把进度清零；触发重绘。
    void SetFileDownloadStarted();

    // 用户点「重新发送」后重新入队上传：置"上传中"态并把进度清零；触发重绘。
    void SetFileUploadStarted();

    // 更新"对端把这个文件收到什么程度了"（仅本端发出的文件气泡有意义）；触发重绘。
    // 不改 fileState —— 本端这一侧传得怎么样与对端收得怎么样是两回事。
    //   peerState：取值见 ChatFilePeerState。
    void SetFilePeerState(int peerState);

    // 文件已删除 / 失效（服务端已不存在）：置"已删除"态、文件名加删除线；触发重绘。
    void SetFileDeleted();

    // ---- 文件夹气泡（kMsgFolder）----
    // 注：文件夹气泡的高度同样随状态变化（有无进度条 / 失败原因行 / 按钮行），故宿主
    // 更新完状态后须 ForceLayout 重排，不能只 Invalidate。

    // 取本行文件夹传输的会话 id（状态推送到达后宿主按它定位本行）。空=无。
    const std::string& FolderSessionId() const { return m_msg.folderSessionId; }

    // 只读取回本行承载的消息（右键菜单复制 / 引用等需要）。
    const ChatMessage& GetMessage() const { return m_msg; }

    /**
     *  让折行测量与渲染缓存作废，使下一次排版重新测量本行。
     *
     *  用于"消息内容没变、但渲染口径变了"的场合 —— 目前只有"自动识别链接"开关被
     *  改动时用得上：链接片段是在 Measure 里算的，不重新测量就还是旧的那一份。
     *  与 SetMessage 不同，本方法<u>不</u>动消息内容与选区。
     */
    void InvalidateMeasureCache();

    /**
     *  回填服务器分配的消息 ID 与消息时间（本端发出、服务器回执到达后调用）。
     *
     *  <u>只</u>改这两个字段。它们不参与折行，除已读回执外也不参与绘制，主要供撤回
     *  时限判断（见 .cpp 里 canRecallMessage 的调用），故不置脏、不重排。开启已读回执
     *  的行拿到 msgid 后回执行要从「不显示」变成「未读」，因此会重画一次本行（行高
     *  早已预留，不需要重排）。
     *
     *  不要退回用 SetMessage 做这件事：那是"整条换掉"的重路径，会连带清空选区
     *  模型、把折行测量缓存置为失效，而回填方本就没有理由触发重排 —— 于是这一行
     *  在下次重排之前都选不中文字（表现为回执一到就选不了，改一下窗口大小又好了）。
     *    msgid：       服务器分配的消息 ID。
     *    timestampMs： 消息时间（Unix 毫秒）。
     */
    void BackfillServerIds(long long msgid, long long timestampMs);

    /**
     *  设置本行是否显示已读回执。由所在的消息列表在建行时设置（单聊开启、群聊不开启），
     *  见 ChatMessageList::SetReadReceiptEnabled。
     *    b：true 开启。开启后自己发出的消息下方预留一行回执，按 DecideReadReceipt 的
     *       结果显示「未读」或「✓ 已读」；关闭则既不预留也不绘制。
     *  副作用：改变本行行高（自己发出的消息），调用方须随后重排列表。
     */
    void SetReadReceiptEnabled(bool b);

    /**
     *  把本行标为「发送失败」并记下原因：气泡左侧画红色感叹号，下方画原因。图片行同时置为上传失败态，
     *  文件行同时置为失败态。群聊（不预留回执行）里行高会多出原因行，调用方须重排；单聊里原因画在
     *  回执行的位置，行高不变。
     *  @param reason 失败原因（界面显示用，不带句号）
     */
    void SetSendFailed(const CString& reason);

    // 本行是否处于「发送失败」。
    bool IsSendFailed() const { return m_msg.sendFailed; }

    // 本行是否显示已读回执。
    bool IsReadReceiptEnabled() const { return m_readReceiptEnabled; }

    /**
     *  把本行标为「对方已读」（对方的已读位点越过了本条时，由消息列表调用）。
     *  只改 read 字段并重画本行：回执行的高度早已预留，不需要重排。已读只进不退，
     *  没有「改回未读」的接口。
     *  返回：本行原本是未读、这次被改成已读时返回 true；原本就已读时返回 false。
     */
    bool MarkPeerRead();

    /**
     *  回填本端发出的图片的 fileid / md5（上传成功后）。
     *  这两项不参与折行也不参与绘制，只在本条被引用时写进 content.quote，
     *  故与 BackfillServerIds 一样：不置脏、不重排、不重绘。
     */
    void SetImageIds(const CString& fileid, const CString& md5)
    {
        m_msg.imageFileId = fileid;
        m_msg.imageMd5    = md5;
    }

    // 本行发送者 userid（真图头像下载完成后宿主按它定位本行刷新；0=未知）。
    int64_t SenderUserId() const { return m_msg.senderUserId; }

    /**
     *  设置本行头像是否画成灰度（离线用户头像置灰，2026-10-03）：真图头像换成
     *  ChatAvatarBitmapCache 里的灰度副本，字形头像换成等亮度的灰色底。由 ChatMessageList
     *  按发送者的在线状态（本人的气泡按本端连接态）设入。
     *    gray：true 画灰度，false 画彩色。
     *  取值变化时把渲染缓存置脏并请求重绘 —— 文字气泡的头像画在渲染缓存里，不置脏就不会重画。
     */
    void SetAvatarGray(bool gray);

    // 本行头像当前是否画成灰度，见 SetAvatarGray。
    bool IsAvatarGray() const { return m_avatarGray; }

    // 设置 / 更新发送者真实图片头像本地路径（真图下载完成后宿主调用），并触发重绘；
    // 传空串则回退画字形头像。路径没变也会作废共享头像缓存（ChatAvatarBitmapCache）
    // 里这一路径的位图 —— 同一路径的文件可能刚被新下载的头像覆盖。
    void SetSenderAvatarImage(const CString& path);

    /**
     *  一次性更新发送者的显示信息（群聊气泡上方的名字与头像）。用于群成员列表晚于历史
     *  消息到达时，把先按备用信息（"用户<id>"）画出来的气泡换成真实信息。
     *  只改显示，不改行高（名字行的高度与名字内容无关）；有变化时触发重绘。
     *    name：     发送者显示名。
     *    glyph：    头像字形（没有真图头像时画它）。
     *    color：    头像底色。
     *    imagePath：真图头像本地路径；空表示画字形头像。
     *  @return 四项里至少有一项与原值不同、本行确实被更新时返回 true。
     */
    bool SetSenderInfo(const CString& name, const CString& glyph, COLORREF color,
                       const CString& imagePath);

    /**
     *  标记发送者是不是机器人账号（群聊气泡在名字后面画「机器人」徽章）。机器人列表晚于消息到达时由宿主调用。
     *    isBot：是否为机器人。
     *  @return 与原值不同、本行确实被更新时返回 true（并触发重绘）。
     */
    bool SetSenderBot(bool isBot);

    // 按消息区可用宽度测量文字气泡的折行布局（折行后气泡的宽 / 高）。
    // 须由 ChatMessageList 在排版时对每行调用一次；availWidth 变化
    // （窗口缩放）后须重新调用。非文字消息调用本函数无副作用。
    //   availWidth：消息区可用宽度（逻辑像素）。
    void Measure(int availWidth);

    // 本行所需高度（逻辑像素）—— 供 ChatMessageList 排版用。文字气泡的
    // 高度取自最近一次 Measure() 的折行结果（未测量则回退到单行高）。
    int GetRowHeight() const;

    // ---- 性能探针（测试 / 排查用）-------------------------------------
    // 统计"重量级折行测量"（GetDC + 建字体 + DrawText(DT_CALCRECT|
    // DT_WORDBREAK)）实际执行的次数。Measure 命中缓存时会在该路径之前
    // 返回，故此计数恰等于真正发生的昂贵测量次数 —— 用来量化 resize 时
    // 的重复测量浪费、并作为缓存修复的回归断言依据。进程级全局计数。
    static unsigned long HeavyMeasureCount();
    // 把上述计数清零（一次度量 / 一个用例开始前调用）。
    static void ResetHeavyMeasureCount();

    // ---- 超长气泡分段缓存（2026-10-04）--------------------------------
    // 行高超过分段阈值的文字气泡，渲染缓存只覆盖可见区及其上下各一段余量，不再整条缓存：第一次
    // 绘制的耗时因此只与可视区大小有关，与消息长度无关。滚动到缓存段以外时，按新位置重画一段。
    // 分段阈值（像素）：行高超过它的气泡才分段缓存。默认值见 ChatBubble.cpp 的 kDefaultTallSliceThresholdPx。
    static int TallSliceThreshold();
    // 修改分段阈值，进程级全局设置。px：新的阈值（像素），传 INT_MAX 等于关闭分段。
    // 供测试使用：临时关闭分段，画出整条缓存的结果作为比较基准。
    static void SetTallSliceThreshold(int px);
    // 当前渲染缓存位图的高度（像素）；尚未建立缓存时返回 0。分段缓存时小于行高。
    int GetRenderCacheHeight() const;
    // 自进程启动或上次 ResetRenderPassCount 以来，渲染缓存整张（分段缓存时为整段）重画的次数。
    // 进程级全局计数，性能探针，供测试断言滚动时不做多余的重画。
    static unsigned long RenderPassCount();
    // 把上述计数清零（一个用例开始前调用）。
    static void ResetRenderPassCount();

    // 把本行就地变成一条居中的系统提示行，显示"某某撤回了一条消息"。收到服务端
    // cmd_message_recalled_notify 推送后调用。调用后本行 kind 变为时间分隔样式、
    // 行高随之改变，调用方须触发一次重排。
    //   tipText：提示文案，由 balloonchat::BuildRecallTipText 按撤回者是否本人生成
    //            （"你撤回了一条消息" / "张三撤回了一条消息"）。传空串时本行会变成
    //            一条空提示，调用方应保证非空。
    void RecallToTip(const CString& tipText);

    /**
     *  给撤回提示行挂上「编辑」（docs/撤回后重新编辑方案.md §3.2）：提示行末尾画一个可点击的「编辑」，点击后
     *  把快照投给宿主聊天窗放回输入框（WM_CHAT_REEDIT_REQUEST，与发送失败后的「重新编辑」相同）。
     *  只对 kMsgTime 行有效；本方法不改行高，只重画。
     *    draftJson： 快照（UTF-8 JSON，RecallEdit.h 的 SerializeRecallDraft 写出）；为空表示去掉「编辑」。
     *    deadlineMs：到期时刻（Unix 毫秒），过了这个时刻不再显示。
     */
    void SetRecallEdit(const std::string& draftJson, long long deadlineMs);

    /**
     *  本行在给定时刻是否显示「编辑」：是挂了快照的提示行，且尚未到期。
     *    nowMs：当前时间（Unix 毫秒）。
     */
    bool HasRecallEdit(long long nowMs) const;

    /**
     *  到期处理：「编辑」已到期时去掉快照并重画，本行变回普通提示行。
     *    nowMs：当前时间（Unix 毫秒）。
     *  @return 本行发生了变化返回 true。
     */
    bool ExpireRecallEdit(long long nowMs);

    /**
     *  落点是否在「编辑」两个字上（前面的提示文字与行两侧的空白都不算）；多选模式、已到期时一律为 false。
     *    pt：落点（宿主客户区坐标）。
     */
    bool RecallEditAtPoint(POINT pt) const;

    // 本行当前是否被 Pin 住（右键菜单据它决定那一项的文案是「Pin」还是「取消 Pin」）。
    bool IsPinned() const { return m_msg.pinned; }

    // 设置本行的 Pin 标记（收到服务端 cmd_message_pinned_notify 推送后由消息列表调用）。
    // 标记画在气泡<u>外侧</u>、不占内容宽度也不改行高，故只触发重绘、不必重排。
    //   pinned：true 钉住、false 取消。
    void SetPinned(bool pinned);

    // 多选模式下本行是否被选中 / 设置选中态。SetSelected 会触发重绘。
    bool IsSelected() const { return m_selected; }
    void SetSelected(bool selected);

    /**
     *  本行是否属于密聊内容（isSecret 为 true 的密聊消息，或密聊邀请卡片）：这类行没有右键菜单，
     *  不能多选（docs/密聊方案.md 4.5「复制、转发等」）。
     */
    bool IsSecretRow() const;

    /**
     *  本行是否不允许弹菜单、拖出：本身是密聊内容，或所在列表处于密聊限制（单聊窗处于密聊状态）。
     */
    bool IsSecretLocked() const;

    /**
     *  把本行的文字与选区副本在内存里清零后清空（退出密聊、移除密聊消息行之前调用，docs/密聊方案.md 4.5）。
     */
    void WipeSecretText();

    /**
     *  落点命中密聊邀请卡片底部的第几个按钮（docs/密聊方案.md 4.8）：发起方一侧只有「取消」，被邀请方一侧
     *  左半「同意」、右半「忽略」。按钮行的矩形在绘制时记下，绘制之前一律返回 -1。
     *    pt：落点（宿主客户区坐标）。
     *  @return 按钮下标（从 0 起）；不是密聊邀请卡片、卡片没有按钮、处于多选模式或没有命中时返回 -1
     */
    int SecretInviteButtonAt(POINT pt) const;

    // ---- 气泡内文字选中（拖拽选择 + 复制）------------------------------
    // 注意与上面的"多选模式"是两回事：那个是整行勾选用于转发 / 删除，这个是
    // 在一条消息内部选中部分文字。

    /**
     *  本气泡当前是否有非空的文字选区。
     */
    bool HasTextSelection() const;

    /**
     *  取当前选中的文字。内联表情会被替换成 "[表情]"（与会话列表预览同口径）。
     *  @return 选中文本；无选区时返回空串。
     */
    CString GetSelectedText() const;

    /**
     *  取当前选中的内容，拆成"文字 / 表情"片段（与 GetSelectedText 是同一个选区）。
     *  复制时写进剪贴板的 Flamingo 格式由它生成，粘贴到聊天输入框时表情能还原成表情。
     *  @return 选中内容的片段序列；无选区时返回空序列。
     */
    std::vector<ChatClipSegment> GetSelectedClipSegments() const;

    /**
     *  清除文字选区并重绘。点到别处、切换消息、进入多选模式时调用。
     */
    void ClearTextSelection();

    /**
     *  把本气泡的全部文字设为选中（三击 / 菜单"全选"用）。非文字消息无副作用。
     */
    void SelectAllText();

    // DuiControl 覆写：绘制本行。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 同名隐藏 DuiControl::Invalidate（基类非虚）：本行任何内容变更都经
    // Invalidate() 请求重绘，这里顺带把渲染缓存置脏，使下次 OnPaint 重画缓存。
    // 内部各 setter 调的无限定 Invalidate() 都解析到本版本，故无需逐处置脏；
    // 框架经 DuiControl* 发起的非内容重绘走基类、不置脏（缓存仍有效，语义正确）。
    void Invalidate();

    // DuiControl 覆写：左键点击 —— 点在已 Pin 消息旁的图钉上时请求取消 Pin（经
    // ChatMessageList::RequestPinBubble 投给宿主窗口，本地标记不就地修改）；否则文字气泡
    // 开始拖拽选择；多选模式下切换本行选中态（此时图钉不响应）。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：左键抬起 —— 结束拖拽选择、释放鼠标捕获。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：右键弹出上下文菜单（复制 / 引用回复 / 转发 /
    // 撤回 / 删除 / 多选）。时间分隔 / 撤回提示行不弹菜单。
    bool OnRButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：双击 —— 仅当本行是已就绪的图片消息、且双击点落在图片
    // 矩形内时，向宿主聊天窗发 WM_VIEW_CHAT_IMAGE 请求打开看图窗放大查看。
    // 其余情况不消费。需宿主对 DuiHost 开启 EnableDoubleClick(true) 才会触发。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标移动 —— 文件行据落点更新底部操作链接的悬停高亮（hover
    // 变化时重绘，使悬停链接变深色 + 加下划线）；移到图钉上时登记「取消 Pin」悬停
    // 提示，移开时撤销登记。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标离开本行 —— 清除文件链接悬停态并重绘，撤销图钉悬停提示的登记。
    bool OnMouseLeave() override;

    // DuiControl 覆写：光标落在可点击的图钉、文件操作链接或正文超链接上时显示手型，
    // 否则普通箭头。
    bool OnSetCursor(POINT pt) override;

private:
    /**
     *  按卡片内宽把接龙说明折成至多两行，结果写入 m_relayNoteLines（说明为空时清空）。
     *  要按说明的实际字体量宽度，故临时取一次屏幕 DC。仅接龙卡片行调用，由 Measure 在消息内容
     *  变化后调用一次。
     */
    void MeasureRelayNote();

    /**
     *  按卡片内宽把屏幕共享卡片的描述折成至多两行（第 15 期），结果写入 m_shareDescLines（没有描述时清空）。
     *  与 MeasureRelayNote 同一做法，由 Measure 在消息内容变化后调用一次。
     */
    void MeasureScreenShareDesc();

    /**
     *  请宿主聊天窗打开某张接龙卡片的详情窗（投 WM_CHAT_RELAY_ACTION，msgid 放在堆上，
     *  宿主处理后 delete）。
     *    cardMsgid：卡片消息的服务器 msgid——卡片行取本行 msgid，接龙提示行取 relayCardMsgid；
     *               非正（卡片还没拿到服务器 msgid）时什么都不做。
     *    action：   取值见 ChatMessageList.h 的 ChatRelayAction。
     */
    void PostRelayAction(long long cardMsgid, int action);

    /**
     *  请宿主聊天窗打开本行共享卡片对应的共享（投 WM_CHAT_OPEN_SHARE，共享 id 放在堆上，宿主处理后 delete）。
     *  共享 id 非正、或按钮不可点时什么都不做。
     */
    void PostShareOpen();

    /**
     *  请宿主聊天窗加入本行屏幕共享卡片对应的共享（投 WM_CHAT_JOIN_SCREENSHARE，房间号与发起人名放在堆上的
     *  ScreenShareJoinRequest 里，宿主处理后 delete）。卡片没有房间号或按钮不可点时不做任何事。
     */
    void PostScreenShareJoin();

    /**
     *  请宿主群聊窗打开本行群公告卡片（投 WM_CHAT_OPEN_ANNOUNCEMENT_CARD）或群备忘卡片（投 WM_CHAT_OPEN_MEMO_CARD）对应的
     *  公告或备忘。载荷是堆上的 InfoCardOpenRequest，宿主处理后 delete。卡片内容不可用或不可点时不做任何事。
     */
    void PostInfoCardOpen();

    /**
     *  请宿主聊天窗播放本行云端录像卡片对应的录像（投 WM_CHAT_OPEN_CLOUD_RECORD_CARD）。载荷是堆上的
     *  CloudRecordOpenRequest，宿主处理后 delete。卡片内容不可用或不可点时不做任何事。
     */
    void PostCloudRecordOpen();

    /**
     *  请宿主聊天窗处理语音气泡上的一次点击（投 WM_CHAT_VOICE_ACTION，消息 id 放在堆上的 VoiceActionRequest 里，
     *  宿主处理后 delete）。宿主窗口不存在时不做任何事。
     *  @param action 取值见 ChatVoiceAction：播放 / 停止，或重发
     */
    void PostVoiceAction(int action);

    /**
     *  本行入群邀请卡片的「加入群聊」此刻是否可点：卡片可用、显示按钮（被邀请人一侧且待加入）、且不在「正在加入」。
     *  @return 可点返回 true；不是入群邀请卡片时返回 false
     */
    bool GroupInviteButtonClickable() const;

    /**
     *  请宿主单聊窗加入本行入群邀请卡片对应的群（投 WM_CHAT_JOIN_GROUP_INVITE，卡片 msgid 与邀请 id 放在堆上的
     *  GroupInviteJoinRequest 里，宿主处理后 delete）。按钮不可点时不做任何事。
     */
    void PostGroupInviteJoin();

    /**
     *  请宿主单聊窗执行密聊邀请卡片上的第 index 个按钮（投 WM_CHAT_SECRET_INVITE_ACTION，wParam 为动作，
     *  会话号放在堆上的 unsigned long long 里，宿主处理后 delete）。
     *    index：按钮下标；越界、卡片会话号为 0 或宿主窗口不存在时不做任何事。
     */
    void PostSecretInviteAction(int index);

    /**
     *  落点是否在可点击的接龙提示行的文字上：本行是带 relayCardMsgid 的提示行、不在多选模式、
     *  且落在居中那段文字的横向范围内（行两侧的空白不算）。
     *    pt：落点（宿主客户区坐标）。
     */
    bool RelayTipAtPoint(POINT pt) const;

    /**
     *  落点命中了第几条超链接。
     *
     *  命中测试在"相对文本区原点"的坐标系里做（与 @提醒 的 TryPopupMentionMenu
     *  同一套），故非文字气泡、尚未测量、或本条没有链接时一律返回 -1。
     *    pt：落点（宿主客户区坐标）。
     *  @return m_linksFlat 的下标；没命中返回 -1。
     */
    int LinkAtPoint(POINT pt) const;

    /**
     *  请宿主聊天窗打开第 index 条链接（投 WM_CHAT_OPEN_LINK，URL 用堆上的
     *  std::wstring 传递，宿主处理后 delete）。气泡自己不碰 ShellExecute ——
     *  协议白名单校验只该有一处，放在宿主窗里。
     *    index：m_linksFlat 的下标；越界时什么都不做。
     */
    void OpenLinkByIndex(int index);


    // 本行是否是"可被选择 / 可弹菜单的真实消息"（时间分隔与撤回提示
    // 行返回 false）。
    bool IsRealMessage() const;

    // 取祖先消息列表是否处于多选模式（找不到列表则按 false）。
    bool ParentInMultiSelect() const;

    // 本行是否可走"渲染缓存"：文字 / emoji / 时间分隔行可缓存（绘制时不记录任何
    // 宿主坐标系命中矩形）。图片 / 文件行不可缓存 —— 它们绘制时要记下双击图片、
    // 点按钮的命中矩形（m_imageRect / m_fileBtnRect），缓存会让这些矩形失效。
    bool IsRenderCacheable() const;

    // 把本行按 m_rcItem 的相对坐标实际绘制到 hdc（原 OnPaint 主体）。OnPaint 据
    // IsRenderCacheable 决定直接调它（图片 / 文件行）还是先画进缓存位图再 BitBlt。
    void RenderContent(HDC hdc);

    // 图片消息的实际状态：状态值为"正常"、但本地文件丢失或无法解码（ChatImageCache 记录过
    // 解码失败）时按"下载失败"（kChatImageStateDownloadFailed）返回，其余情况原样返回
    // m_msg.imageState。双击放大与右键菜单以它为准，避免对打不开的文件提供"放大 / 另存为"。
    // 非图片消息原样返回 m_msg.imageState。会读一次图片缓存（加锁），不修改本行状态。
    int EffectiveImageState() const;

    // 整条消息的剪贴板片段，没有选区时右键「复制」用：文字消息与大表情按正文拆成文字 /
    // 表情；图片消息在本地原图可用时是一个图片片段。其余类型（文件、文件夹等）以及
    // 本地没有原图的图片返回空序列，由调用方退回只写纯文字。
    std::vector<ChatClipSegment> WholeMessageClipSegments() const;

    // 图片消息的本地原图是否可用：状态为就绪（含"未记录解码失败"）、有本地路径、且文件
    // 存在。复制图片与拖出图片都要求它。非图片消息返回 false。
    bool IsLocalImageAvailable() const;

    // 点 pt（宿主坐标）是否真正落在已选中的文字上（按字形矩形判断，行尾空白处不算）。
    // 没有选区时返回 false。
    bool PointInTextSelection(POINT pt) const;

    // 开始把内容拖出去：先交还鼠标捕获，再按文字选区或整张图片取片段、发起拖拽（模态，
    // 拖拽结束才返回）。返回后不得再访问本对象的成员 —— 拖放期间界面可能已经重建。
    void BeginChatDrag();

    // 本行是不是"可以选中文字"的文字类消息（纯文字 / emoji 大表情之外的文字气泡）。
    // 时间分隔、撤回提示、图片、文件行都不参与文字选中。
    bool IsTextSelectable() const;

    // 把宿主客户区坐标换算成文本区内的坐标，再映射成字符下标。
    // 落点在文本区之外时，靠 CharIndexFromPoint 自身的夹取规则归到首 / 尾。
    int CharIndexFromHostPoint(POINT pt) const;

    // 取本行文本区左上角（宿主客户区坐标）。选区矩形与命中测试都以它为原点。
    // 未测量 / 非文字消息时返回 (0,0) 并令 okOut 为 false。
    POINT TextOriginInHost(bool& okOut) const;

    // 选区从 (oldAnchor, oldCaret) 变成当前的 (m_selAnchor, m_selCaret) 之后调用：只把选区变化
    // 涉及的那几行标成待重画（渲染缓存里局部重画），并请求宿主重画；不把整张渲染缓存置脏。
    // 选区变化之外还有别的内容变化时，调用方仍应调 Invalidate()。
    //   oldAnchor / oldCaret：变化前的选区两端（字符下标，-1 表示无选区）。
    void InvalidateSelectionChange(int oldAnchor, int oldCaret);

    // 把当前选区画成高亮矩形（在文字之下）。hdc 为本行绘制目标，origin 为文本区原点
    // （已按缓存平移过），故这里只做相对到绝对的偏移。
    void DrawTextSelection(HDC hdc, const POINT& origin) const;

    // 若落点命中某个 @某人 片段，则弹出该片段的右键菜单并把选中项投给宿主聊天窗。
    //   pt：   落点（宿主客户区坐标）。
    //   hwnd： 宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 命中并已处理（无论用户是否选了菜单项）返回 true；没命中返回 false，
    //         调用方据此继续弹气泡自己的菜单。
    bool TryPopupMentionMenu(const POINT& pt, HWND hwnd);

    // 本行发送者头像的矩形（宿主客户区坐标）。几何与 RenderContent 里画头像那段
    // 逐字一致：自己发的靠右、别人发的靠左，边长 kAvatarSize。
    // 时间分隔 / 撤回提示行没有头像，此时返回空矩形。
    RECT AvatarRectInHost() const;

    // 若落点命中本行的发送者头像，则弹出该发送者的右键菜单（与 @某人 片段、群成员
    // 侧栏共用同一套模型与分发消息）并把选中项投给宿主聊天窗。
    //
    // <u>只在群聊里生效</u>：单聊窗上下就两个人，对着头像弹"发消息 / @TA"没有意义。
    // 判据是本行带不带发送者名（showSender 那套口径，只有群聊才填 senderName）。
    // 自己发的那条也不弹 —— 与 @ 到自己时只留「查看资料」是同一个道理，但这里连
    // 菜单都不必出：右键点自己的头像本就不是个有意义的动作。
    //   pt：   落点（宿主客户区坐标）。
    //   hwnd： 宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 命中并已处理返回 true；没命中返回 false，调用方据此继续往下走。
    bool TryPopupAvatarMenu(const POINT& pt, HWND hwnd);

    // 建出成员操作菜单、弹出、把选中项投给宿主聊天窗。@某人 片段与发送者头像两个
    // 入口共用它 —— 两处菜单本来就该逐字一样，各建一份迟早会走样。
    //   uid：        目标成员 userid。
    //   displayName：目标成员显示名（不含 '@'），复制名字 / 再 @ 一次要用。
    //   isSelf：     目标是不是本人（决定菜单只留「查看资料」）。
    //   pt：         落点（宿主客户区坐标）。
    //   hwnd：       宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 菜单弹出过（无论用户是否选了项）返回 true；模型为空、建菜单失败返回 false。
    bool PopupMemberMenu(long long uid, const std::wstring& displayName, bool isSelf,
                         const POINT& pt, HWND hwnd);

    // 取落点 pt（宿主客户区坐标）命中的文件操作链接下标（0~2）；未命中返回 -1。
    // 供悬停高亮（OnMouseMove）、手型光标（OnSetCursor）、点击（OnLButtonDown）共用。
    int FileLinkAtPoint(POINT pt) const;

    // 取落点 pt（宿主客户区坐标）命中的文件夹操作链接下标；未命中返回 -1。用途与
    // FileLinkAtPoint 相同。<u>不可用的按钮（如保留期已过的「重试」）一律返回 -1</u>：
    // 它画成灰色只是让用户知道有这条路、暂时走不通，不该给悬停高亮、手型光标和点击。
    int FolderLinkAtPoint(POINT pt) const;

    // 落点 pt（宿主客户区坐标）是否落在<u>可点击</u>的图钉上。以下情况一律返回 false：
    // 本行未 Pin、不是真实消息、尚未分配服务器 msgid（取消请求必被服务端判为消息不存在）、
    // 处于多选模式（整行是勾选目标）、图钉尚未绘制过。供点击（OnLButtonDown）、手型光标
    // （OnSetCursor）、悬停提示（OnMouseMove）共用，三处的判断因此始终一致。
    bool PinIconAtPoint(POINT pt) const;

    // 切换鼠标是否停在图钉上的状态，并同步悬停提示的登记：停上去时向 DuiToolTipMgr 登记
    // 「取消 Pin」并按当前位置开始计时；离开时撤销登记。必须撤销而不是只隐藏 —— 宿主在
    // 鼠标进入某个控件时会为已登记的控件自动弹提示，不撤销的话鼠标进入本行任意位置都会
    // 弹出「取消 Pin」。状态未变化时不做任何事。
    //   hover： 鼠标是否停在图钉上。
    //   ptHost：鼠标位置（宿主客户区坐标），只在 hover 为 true 时用来定位提示浮窗。
    void SetPinHover(bool hover, POINT ptHost);

    // 确保渲染缓存位图存在且为 w×h；尺寸变化则重建。ref 提供兼容 DC。
    void EnsureRenderCache(HDC ref, int w, int h);
    // 释放渲染缓存位图（尺寸变 / SetMessage 等内容变 / 析构时）。
    void ReleaseRenderCache();

    // 文件气泡底部按钮被点击：组 ChatFileActionData 经 WM_CHAT_FILE_ACTION 发给宿主
    // 聊天窗集中处理（打开 / 另存为 / 取消 / 重试）。action 取值见 ChatFileActionKind。
    void HandleFileButton(int action);

    // 文件夹气泡底部按钮被点击：组 ChatFolderActionData 经 WM_CHAT_FOLDER_ACTION 发给
    // 宿主聊天窗集中处理（接收 / 另存为 / 拒绝 / 取消 / 重试续传 / 重新发送 / 打开文件夹）。
    // action 取值见 ChatFolderActionKind。气泡不认识传输会话，只负责把点击如实上报。
    void HandleFolderButton(int action);

    // 图片气泡右键选了「图片另存为」/「再次发送」：组 ChatImageActionData 经
    // WM_CHAT_IMAGE_ACTION 发给宿主聊天窗执行。默认另存名在这里就按本行的
    // imagePath / msgid 算好，宿主拿到即用。
    //   hwnd：   宿主聊天窗窗口句柄，调用方须保证非 NULL；
    //   menuId： 选中的菜单项，取值见 ChatMessageMenuId（仅上述两项）。
    void PostImageMenuAction(HWND hwnd, int menuId);

    // 右键「重新编辑」：把原文拆成片段、带上 @ 候选，打包投给宿主聊天窗放回输入框（WM_CHAT_REEDIT_REQUEST）。
    void PostReeditRequest(HWND hwnd);

    // 撤回提示行点了「编辑」：解析快照，投给宿主聊天窗放回输入框（WM_CHAT_REEDIT_REQUEST）。快照解析失败时什么都不做。
    void PostRecallEditRequest();

    // 右键「添加到备忘」/「添加到群备忘」：把本条消息的副本投给宿主聊天窗（WM_CHAT_ADD_TO_MEMO），由它补上来源信息后
    // 打开新建备忘的窗口。target 取值见 MemoPrefillTarget。
    void PostAddToMemo(HWND hwnd, int target);

    // 撤回提示行带「编辑」时两段文字的横向位置（宿主客户区坐标）：tipLeft 起画提示文字，linkLeft 起画「编辑」，
    // linkWidth 是「编辑」的宽度。整段居中在本行里。hdc 只用来量宽。
    void RecallEditLayout(HDC hdc, int& tipLeft, int& linkLeft, int& linkWidth) const;

    // ---- Markdown 消息（docs/Markdown消息方案.md 第 11.3 节：显示为卡片）----

    // 本行此刻是否画成 Markdown 卡片：是文字消息、带 Markdown 标志、FlamingoMarkdown.dll 可用且解析成功。
    // 为 false 时本行与普通文字气泡完全相同（显示源文本）。
    bool MarkdownActive() const;

    // Measure 的 Markdown 分支：按需解析，取卡片的标题与摘要、算出卡片尺寸，成功时设好 m_textBubbleW / m_textBubbleH
    // 并返回 true；不是 Markdown 消息、DLL 不可用或解析失败时返回 false，调用方继续走普通文字的折行测量。
    //   hdc：测量用的设备上下文；maxTextW：正文最大宽度（卡片不用）；maxBubbleW：气泡最大宽度（像素）。
    bool MeasureMarkdown(HDC hdc, int maxTextW, int maxBubbleW);

    // 带引用的文字气泡：把引用块的高度计入气泡高、并给气泡宽度定下限。普通文字与代码卡片的测量路径共用。
    //   maxBubbleW：气泡最大宽度（像素），下限不超过它。
    void ApplyQuoteToTextBubble(int maxBubbleW);

    // 可选中文字的总长度（扁平文字下标的上限）。
    int SelectableTextLength() const;

    // 第 index 个链接的地址（取自 m_linksFlat）；越界时返回空串。
    std::wstring LinkUrlByIndex(int index) const;

    // 点 pt（宿主客户区坐标）是否落在 Markdown 卡片上；不是卡片或卡片还没画过时返回 false。
    bool PointInMarkdownCard(POINT pt) const;

    // 请宿主聊天窗打开 Markdown 预览窗口（WM_CHAT_OPEN_MARKDOWN_PREVIEW，载荷为堆上的 MarkdownPreviewOpenRequest）。
    void PostMarkdownPreviewOpen();

    // ---- 代码片段（docs/代码片段方案.md 第 5.3 节）----

    // 本行此刻是否画成代码卡片：是带代码片段标志的文字消息，且最近一次 Measure 已按卡片排版。
    bool CodeActive() const;

    // Measure 的代码片段分支：按需准备卡片内容，按两侧头像之间的整列宽度排版，设好 m_textBubbleW / m_textBubbleH
    // 并返回 true；不是代码片段时返回 false，调用方继续走 Markdown 与普通文字的测量。
    //   hdc：测量用的设备上下文；contentW：内容区宽度（已扣掉多选的勾选列，像素）。
    bool MeasureCode(HDC hdc, int contentW);

    // 光标所在的代码卡片部位（取值见 CodeBlockPart）：pt 为宿主客户区坐标；不是代码卡片或卡片还没画过时为 kCodePartNone。
    int CodePartAtPoint(POINT pt) const;

    // 按当前鼠标位置更新代码卡片的悬停状态（「复制」、底栏）；鼠标离开本行时把「已复制」恢复为「复制」。有变化时重画并返回 true。
    //   pt：鼠标位置（宿主客户区坐标）；inside：鼠标是否仍在本行内（离开本行时传 false）。
    bool UpdateCodeHover(POINT pt, bool inside);

    // 把代码原文（CRLF 换行、Tab 原样）写进剪贴板，顶部改为「已复制」。不是代码卡片时什么都不做。
    //   hwnd：剪贴板的所有者窗口（宿主窗口）。
    void CopyCodeToClipboard(HWND hwnd);

    // 请宿主聊天窗打开代码查看窗（WM_CHAT_OPEN_CODE_VIEWER）。不是代码卡片或宿主窗口无效时什么都不做。
    void PostCodeViewerOpen();

    // 请宿主聊天窗打开代码查看窗，或为这条代码片段追加批注（WM_CHAT_OPEN_CODE_VIEWER）。不是代码卡片或宿主窗口无效时
    // 什么都不做；追加批注时这条消息还没有服务器 msgid 也什么都不做。
    //   action：要做的事，取值见 ChatMessageList.h 的 CodeViewerAction
    void PostCodeViewerOpen(int action);

private:
    ChatMessage m_msg;            // 本行承载的消息

    // 本行是否显示已读回执：单聊的消息列表建行时置为 true，群聊保持 false。
    // 为 false 时自己的消息下方既不预留回执行也不绘制回执。见 SetReadReceiptEnabled。
    bool        m_readReceiptEnabled;

    // 本行头像是否画成灰度（发送者离线，或本端掉线时本人的气泡），见 SetAvatarGray。默认 false。
    bool        m_avatarGray;

    // 注：图片（kMsgImage）的"解码原图 + 缩放到展示尺寸"已移到后台线程的
    // ChatImageCache（按路径 + 展示尺寸缓存）。气泡绘制时只查缓存、命中即画，未命中
    // 画占位并请求后台解码，故本类不再持有任何图片位图。

    // 注：发送者真图头像也不由本类持有。它放在进程级的 ChatAvatarBitmapCache 里按路径
    // 共用（2026-09-30 起），同一发送者的所有气泡共用一张已缩放好的位图。

    // kMsgImage 行最近一次绘制时图片在宿主客户区坐标系里的矩形，供双击命中测试
    //（双击图片打开看图窗）。仅图片行有效；非图片行不读取此值。
    RECT             m_imageRect;

    // kMsgMerged 行最近一次绘制时整张卡片在宿主客户区坐标系里的矩形，供点击命中测试
    //（点卡片任意位置打开聊天记录查看窗）。仅合并卡片行有效；其余行不读取此值。
    RECT             m_mergedRect;
    // 左键是否按在了合并卡片上、尚未抬起。抬起时仍在卡片内才算一次单击、打开查看窗；
    // 按下后拖出卡片再抬起不算，与按钮的点击习惯一致
    bool             m_mergedPressed;

    // kMsgRelay 行：接龙说明按卡片内宽折好的各行（至多两行，最后一行放不下时以省略号收尾）。
    // Measure 在消息内容变化之后重算一次，GetRowHeight 按它的行数算卡片高。仅接龙卡片行有效
    std::vector<CString> m_relayNoteLines;
    // kMsgScreenShare 行：共享描述按卡片内宽折好的各行（第 15 期，至多两行，最后一行放不下时以省略号收尾）。
    // Measure 在消息内容变化之后重算一次，GetRowHeight 按它的行数算卡片高。仅屏幕共享卡片行有效
    std::vector<CString> m_shareDescLines;
    // kMsgRelay 行最近一次绘制时整张卡片、底部按钮在宿主客户区坐标系里的矩形，供点击命中测试
    //（点卡片打开详情窗，点按钮打开详情窗并把焦点放到输入框）。没有按钮时 m_relayBtnRect 为空
    RECT             m_relayRect;
    RECT             m_relayBtnRect;
    // 左键是否按在了接龙卡片上、尚未抬起。抬起时仍在卡片内才算一次单击，含义同 m_mergedPressed
    bool             m_relayPressed;
    // 鼠标是否停在接龙卡片的按钮上。OnMouseMove / OnMouseLeave 维护，绘制时据它把按钮文字
    // 加深并加下划线，与文件气泡的操作链接一致
    bool             m_relayBtnHover;

    // 左键是否按在撤回提示行的「编辑」上、尚未抬起。抬起时仍在「编辑」上才算一次单击
    bool             m_recallEditPressed;
    // 鼠标是否停在撤回提示行的「编辑」上。OnMouseMove / OnMouseLeave 维护，绘制时据它给「编辑」加下划线
    bool             m_recallEditHover;

    // kMsgFileShare 行最近一次绘制时整张卡片、底部按钮在宿主客户区坐标系里的矩形，供点击命中测试
    //（按钮可点时点卡片任意位置都打开共享）。没有按钮时 m_shareBtnRect 为空。
    // kMsgScreenShare 行（屏幕共享卡片）几何相同，也用这两个矩形与下面两个标志：点卡片任意位置加入共享
    RECT             m_shareRect;
    RECT             m_shareBtnRect;
    // 左键是否按在了共享卡片上、尚未抬起。抬起时仍在卡片内才算一次单击，含义同 m_mergedPressed
    bool             m_sharePressed;
    // 鼠标是否停在共享卡片的按钮上，绘制时据它把按钮文字加深并加下划线
    bool             m_shareBtnHover;
    // kMsgSecretInvite 行（密聊邀请卡片）同样用 m_shareRect / m_shareBtnRect 记卡片与按钮行的矩形；按钮行在被邀请方
    // 一侧对半分成两个按钮，因此另用下标记悬停与按下。m_secretBtnHover：鼠标停在第几个按钮上，-1 为没有，绘制时据它
    // 把那个按钮的文字加深并加下划线；m_secretBtnPressed：左键按下时落在第几个按钮上、尚未抬起，-1 为没有，抬起时
    // 仍在同一个按钮上才算一次单击
    int              m_secretBtnHover;
    int              m_secretBtnPressed;

    // kMsgVoice 行最近一次绘制时语音气泡、发送失败的红色感叹号在宿主客户区坐标系里的矩形，供点击命中测试；
    // 没有发送失败时 m_voiceFailRect 为空（语音消息第 3d 步）
    RECT             m_voiceRect;
    RECT             m_voiceFailRect;
    // 左键是否按在了语音气泡 / 红色感叹号上、尚未抬起。抬起时仍在原处才算一次单击，含义同 m_mergedPressed
    bool             m_voicePressed;
    bool             m_voiceFailPressed;

    // kMsgFile 行最近一次绘制时底部各按钮在宿主客户区坐标系里的矩形与动作，供左键
    // 命中测试（点击打开 / 打开所在文件夹 / 另存为 / 取消 / 重试）。m_fileBtnCount 为
    // 本次绘制的按钮数（0~3），随文件态变化；仅文件行有效。动作取值见 ChatFileAction.h。
    RECT             m_fileBtnRect[3];
    int              m_fileBtnAction[3];
    int              m_fileBtnCount;

    // kMsgFile 行底部操作链接当前悬停的下标（0~2）；-1=无。OnMouseMove / OnMouseLeave
    // 维护，OnPaint 据它把悬停链接画成深色 + 下划线。
    int              m_fileHoverIdx;

    // kMsgFolder 行最近一次绘制时底部各按钮在宿主客户区坐标系里的矩形、动作与可用性，
    // 供左键命中测试（接收 / 另存为 / 拒绝 / 取消 / 重试 / 重新发送 / 打开文件夹）。按钮数与动作
    // 一律取自 FolderButtonActions，随传输状态与 fromMe 变化；m_folderBtnEnabled 为 false
    // 的那个画成灰色且不可点（目前只有保留期已过的「重试」会这样，见 FolderRetryEnabled）。
    // 仅文件夹行有效。动作取值见 ChatFolderActionKind。
    RECT             m_folderBtnRect[kMaxChatFolderActions];
    int              m_folderBtnAction[kMaxChatFolderActions];
    bool             m_folderBtnEnabled[kMaxChatFolderActions];
    int              m_folderBtnCount;

    // kMsgFolder 行底部操作链接当前悬停的下标；-1=无。与 m_fileHoverIdx 分开两个成员
    // 而不共用一个：两者的下标空间不同（按钮数上限不同），共用会在同一行既是文件又是
    // 文件夹这种本不该发生的情形下越界，分开则各自封闭。
    int              m_folderHoverIdx;

    // ---- Measure() 产出的文字气泡折行布局缓存 ----
    int  m_measuredWidth;         // 上次 Measure 的 availWidth；-1=未测量
    bool m_measuredMultiSel;      // 上次 Measure 时所属列表是否处于多选模式
                                  // （多选会让出勾选列、收窄气泡可用宽度，
                                  //  故它与 availWidth 一起构成折行缓存键）
    int  m_textBubbleW;           // kMsgText 折行后气泡宽（含内边距）
    int  m_textBubbleH;           // kMsgText 折行后气泡高（含内边距，可多行）

    bool m_selected;              // 多选模式下本行是否被选中

    // ---- 气泡内文字选中的状态 ----
    // m_selLayout / m_selFlatText 由 Measure() 在折行测量的同时产出（两者必须同源，
    // 否则选区矩形会与实际画出来的文字错位）；m_selAnchor / m_selCaret 是拖拽的两个
    // 端点（下标，非负时有效），m_selDragging 表示正在拖。
    // 引用块在<u>本行内</u>的矩形（相对 m_rcItem 左上角），绘制时记下、左键命中时用。
    // 刻意存相对坐标而不是宿主坐标：本行的绘制结果有渲染缓存，缓存期间 m_rcItem 会被
    // 临时移到 (0,0)，存宿主坐标会失效（图片 / 文件行正因为要存宿主坐标才不能缓存）。
    RECT m_quoteRectRel;

    // 引用缩略图入队解码的日志只打一次（绘制每帧都会走到那一支）。
    bool m_quoteThumbLogged;

    // 图钉的可点击区域（相对 m_rcItem 左上角，已含外扩），绘制图钉时记下，点击、手型
    // 光标与悬停提示都以它为准。存相对坐标的理由与 m_quoteRectRel 相同。每次绘制先清空，
    // 本行未 Pin、或从未绘制过时为空矩形。
    RECT m_pinRectRel;

    // 鼠标当前是否停在可点击的图钉上。只在它变化时才切换悬停提示的登记，避免每次鼠标
    // 移动都重新登记、重新计时。由 OnMouseMove 维护，离开本行 / 换消息 / 取消 Pin 时复位。
    bool m_pinHover;

    SelLayout    m_selLayout;      // 字符级排版结果（供命中测试与选区矩形）
    std::wstring m_selFlatText;    // 扁平文本（表情用 U+FFFC 占位）

    // m_msg.ats 换算到<u>扁平文本</u>下标后的副本，与 m_selLayout 同一套口径，故可
    // 直接喂给 BuildSelectionRects 拿到 @ 片段的矩形（画底色块、命中测试都用它）。
    // m_msg.ats 本身记的是<u>发送正文</u>下标，正文里有表情或转义斜杠时两者不等，
    // 换算见 balloonchat::BuildFlatIndexMap。随 Measure 一起重建。
    std::vector<RenderSpan> m_atsFlat;

    // 本条正文里识别出的超链接，口径同样是<u>扁平文本</u>下标，与 m_selLayout 配套。
    // Measure() 里与选区模型一起产出；识别开关关掉时恒为空，于是渲染 / 命中 / 菜单
    // 三处都自然退化成"没有链接"，不需要各自再判一次开关。
    std::vector<LinkSpan> m_linksFlat;
    // 左键按下时命中的链接下标（-1 = 没按在链接上）。抬起时要求落在同一条链接上
    // 才算点击 —— 否则"从链接上按下、拖出去选中一段文字"会被误当成点击。
    int m_linkPressIdx;
    int          m_selAnchor;      // 选区锚点字符下标；-1 = 无选区
    int          m_selCaret;       // 选区当前端字符下标；-1 = 无选区
    bool         m_selDragging;    // 是否正在拖拽选择（鼠标按下未抬起）

    // ---- 把内容拖出去（见 ChatDragSource.h）----
    // 按在已有选区内（文字）或已就绪的图片上时置 true：此时先不动选区，等鼠标移动超过
    // 系统拖动阈值再真正开始拖；没拖就松手则按普通单击处理并复位。
    bool         m_dragArmed;
    // 按下鼠标时的位置（宿主坐标），用来算移动距离；仅 m_dragArmed 为 true 时有意义。
    POINT        m_dragPressPt;

    // ---- 渲染缓存（仅文字 / emoji / 时间分隔行）----
    // 把本行画好的位图缓存下来：内容 / 尺寸 / 多选态都不变时，OnPaint 直接 BitBlt
    // 这张缓存，省掉每帧用 GDI+ 重画气泡（拖动分隔条 / 滚动时的主要每帧开销）。
    // 失效条件：内容变（SetMessage / SetSelected / RecallToTip / 折行重测）置
    // m_renderDirty；行宽高变或多选态变由 OnPaint 比对 m_renderW/H/MultiSel 自动发现。
    HDC      m_renderDC;          // 缓存位图内存 DC；NULL=未建（或缓存创建失败）
    HBITMAP  m_renderBmp;         // 缓存位图
    HBITMAP  m_renderOldBmp;      // SelectObject 旧位图，释放时换回
    int      m_renderW;           // 缓存位图对应的行宽
    int      m_renderH;           // 缓存位图的高度（像素）：整条缓存时等于行高，分段缓存时为缓存段的高度
    bool     m_renderMultiSel;    // 缓存渲染时所属列表的多选态
    bool     m_renderDirty;       // 内容已变、需重画缓存
    int      m_renderRowH;        // 缓存渲染时的行高（像素）；行高变了（如重新折行）就要整张重画
    int      m_renderSliceTop;    // 缓存位图第一行像素对应行内的纵坐标（像素）；整条缓存时为 0

    // ---- 选区变化时的局部重画（2026-10-01）----
    // 拖选文字时选区每变一次，原先整张缓存位图都要重画：两三万字的消息每步约 250 毫秒，
    // 拖起来明显不跟手。现在选区变化只标出变化的那几行（m_selBandRect），OnPaint 在缓存位图里
    // 只重画这一横条；正文的排版结果缓存在 m_textPlaced，局部重画时不再重新排版。
    bool     m_selBandDirty;      // 缓存位图里只有 m_selBandRect 这一横条待重画（仅选区变了）
    RECT     m_selBandRect;       // 待重画的横条（行内相对坐标，左上角为本行左上角）；m_selBandDirty 为 false 时无意义
    std::vector<InlinePlaced> m_textPlaced;   // 正文按绘制样式、文本区宽度排出的放置项（RenderContent 用）
    bool     m_textPlacedValid;   // m_textPlaced 是否可用；整张缓存重画时置 false，由 RenderContent 重新排版
    int      m_textPlacedW;       // m_textPlaced 对应的文本区宽度（像素）

    // ---- 测量阶段的排版结果（2026-10-04）----
    // Measure 折行时排出的放置项留下来，RenderContent 需要排版时直接复用，不再按绘制样式把正文重新排一遍：
    // 一万字的消息这次排版要几十到上百毫秒。文字区宽度落在 [m_measuredLineW, m_measuredMaxTextW] 之间时，
    // 两次排版的结果完全相同（Tests/ChatLayoutReuseTests.cpp 的 B1a）。
    std::vector<InlinePlaced> m_measuredPlaced;   // Measure 排出的放置项；下一次真正的折行测量时整体替换
    int      m_measuredLineW;     // 那次排版的最大行宽（像素）；-1 表示没有可复用的结果（未测量或内容已变）
    int      m_measuredMaxTextW;  // 那次排版所用的可用宽度（像素）

    // ---- Markdown 消息（2026-10-06，docs/Markdown消息方案.md 第 11.3 节：显示为卡片）----
    // 生效时 m_selLayout / m_selFlatText / m_linksFlat / m_atsFlat 保持为空，本行没有可选文字（在预览窗口里选中复制）。
    markdown::CMarkdownDocument m_markdown;   // DLL 文档；SetMessage 时释放，Measure 时按需解析
    bool         m_markdownActive;   // 最近一次 Measure 是否按卡片排版成功
    bool         m_markdownFailed;   // 本条消息解析失败过：之后不再重试，一直按普通文字显示（SetMessage 时复位）
    std::wstring m_mdCardTitle;      // 卡片标题（第一个标题块的文字，没有时为第一行）；Measure 时取得
    std::wstring m_mdCardSummary;    // 卡片摘要（其余文字压成的一段）；Measure 时取得，可为空
    int          m_mdCardSummaryH;   // 摘要折行后占用的高度（像素，最多三行）；没有摘要时为 0
    RECT         m_mdCardRel;        // 卡片矩形，相对本行左上角；绘制时更新，供点击命中，还没画过时为空

    // ---- 代码片段（2026-10-06，docs/代码片段方案.md 第 5.3 节）----
    // 生效时 m_selLayout / m_selFlatText / m_linksFlat / m_atsFlat 保持为空，本行没有可选文字（第一版不支持拖选）。
    CodeBlockContent m_codeContent;   // 卡片内容（规范化后的代码与着色结果）；SetMessage 时清空，Measure 时按需准备
    CodeBlockLayout  m_codeLayout;    // 卡片排版结果（坐标相对卡片左上角）；Measure 时按可用宽度重排
    bool     m_codeActive;            // 最近一次 Measure 是否按代码卡片排版
    bool     m_codePrepared;          // m_codeContent 是否已按当前消息准备好
    bool     m_codeCopied;            // 刚点过「复制」：顶部显示「已复制」，鼠标离开本行时恢复
    int      m_codeHover;             // 鼠标悬停的卡片部位（「复制」或底栏），取值见 CodeBlockPart
    RECT     m_codeCardRel;           // 代码卡片矩形（相对本行左上角），绘制时记下供命中测试；未画过时为空矩形

    // ---- 性能探针计数器 ----
    static unsigned long s_heavyMeasureCount;  // 重量级折行测量实际执行次数
    static unsigned long s_renderPassCount;    // 渲染缓存整张（或整段）重画的次数，见 RenderPassCount

    // 超长气泡分段缓存的阈值（像素），见 TallSliceThreshold。进程级全局设置
    static int s_tallSliceThresholdPx;
};

} // namespace balloonchatwnd
