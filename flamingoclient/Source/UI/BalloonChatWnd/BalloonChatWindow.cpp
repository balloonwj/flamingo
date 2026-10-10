/**
 *  BalloonChatWindow.cpp
 *
 *  新版单聊窗口实现。P0：骨架 —— 以 DuiFrameWindow 形式创建 / 显示一个
 *  气泡聊天窗，客户区暂为占位文字。真正的头部 / 消息区 / 输入区由后续
 *  P1 阶段的 ChatWindowLayout 构建。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonChatWindow.h"

#include <memory>

#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Session/UserSessionData.h"       // FMG_MSG_OPEN_CONTACT_PROFILE（头部点击路由）
#include "../../Common/Log/IULog.h"                 // LOG_INFO
#include "ChatWindowLayout.h"         // BuildChatWindowBody / CtrlId / 分割记忆
#include "WindowAttention.h"          // RaiseWindowForAttention：收到抖动时把窗口调到用户眼前
#include "ChatInputRichEdit.h"        // 输入框控件（无窗口富文本的聊天定制子类）
#include "ChatImageCache.h"           // 图片后台解码缓存：注册监听 + 就绪重绘
#include "ChatHeaderInfo.h"           // ChatHeaderInfo::SetPresence/SetSignature（头部对端真实状态）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"  // 拖动完成后算回分割比例
#include "ChatMessageList.h"          // ChatMessageList::AppendMessage
#include "ChatQuoteModel.h"     // FillQuoteForRender（引用回复的统一入口）
#include "ChatMergedCard.h"     // ApplyMergedCard（合并转发的聊天记录画成卡片）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiLayout::SetHint（引用条人名段按内容定宽）
#include "ChatMockData.h"             // ChatMessage / MakeTextMessage / MockAutoReply
#include "GroupBotModel.h"            // BotReplyDisplayText：机器人代答消息的显示文字（docs/机器人方案.md §4.3）
#include "ChatFontUtil.h"             // 当前聊天字体的载入/保存/应用到输入框
#include "../../App/FlamingoClient.h"        // CFlamingoClient::m_UserMgr（取对方/自己昵称）
#include "../../Common/Util/EncodeUtil.h"            // UTF-8 ↔ Unicode 转换
#include "../BalloonMain/MainPanelAvatar.h"  // MakeMonogramAvatar（窗口图标用对方头像）
#include "../BalloonMain/AvatarGray.h"       // MakeGrayColor / MakeGrayAvatarBitmap：对方离线时窗口图标画成灰度
#include "../BalloonMain/AvatarSpec.h"        // DecodeAvatar：标题/任务栏图标按对端 (facetype,customface) 取字形/底色
#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile（真图头像任务栏图标）
#include "ChatAvatarResolve.h"        // ResolveAvatarRender（气泡 / 标题真图头像呈现解析）
#include "ChatTitleIcon.h"            // MakeRoundTitleIcon / MakeRoundTitleIconFromImage / IconFromBitmap
#include "../../net/IUSocket.h"          // CIUSocket::SendChatTextMessage
#include "../BalloonMain/OnlineCapability.h"   // 离线门禁：连接态、提示条文案、被拦提示
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 顶部离线提示条

// 顶部提示带配色。离线用琥珀色系（与状态胶囊的"离开"同色系，是"需要你处理"的语气），
// 正在连接用中性灰（只是告知，不需要用户动作）。每种各三个色：整条底色、文字色、
// 下边缘分隔线色。底色取得很淡，压着深色文字仍然清晰；分隔线略深于底色，把提示带
// 与下方内容切开。群聊窗用同一套值（见 BalloonGroupChatWindow.cpp）。
static const COLORREF kOfflineBannerBgColor     = RGB(0xFF, 0xF4, 0xE5);
static const COLORREF kOfflineBannerColor       = RGB(0xB4, 0x53, 0x09);
static const COLORREF kOfflineBannerBorderColor = RGB(0xF3, 0xE0, 0xC0);

static const COLORREF kConnectingBannerBgColor     = RGB(0xF5, 0xF5, 0xF6);
static const COLORREF kConnectingBannerColor       = RGB(0x6B, 0x72, 0x80);
static const COLORREF kConnectingBannerBorderColor = RGB(0xE5, 0xE7, 0xEB);
#include "../../net/MessageProtocolClient.h"  // net::MessageNotify
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）     // net::ImageTransfer（发图工作线程）/ ImageSend* 结构
#include "../../net/UserCachePath.h"     // net::UserCacheDir（文件夹接收的默认落地目录与 ChatFile / ChatImage 同一口径）
#include "../../net/FileTransfer.h"      // net::FileTransfer（收发文件工作线程）/ FileSend* 结构
#include "ChatFileAction.h"           // ChatFileActionKind / ChatFileActionData（文件气泡按钮动作）
#include "ChatFolderAction.h"         // ChatFolderActionKind / ChatFolderActionData / ChatFolderState（文件夹气泡按钮动作与状态）
#include "FileBubbleFormat.h"         // FormatFileSize（文件夹总大小文字，与文件气泡同一口径）
#include "../../net/FolderProtocolClient.h"           // net::FolderInviteNotify / FolderStateNotify（文件夹传输推送载体）
#include "../../Feature/FolderTransfer/FolderTransferManager.h" // 文件夹传输会话管理器（发起 / 应答 / 取消 / 重试都转交它）
#include <shlobj.h>                   // SHBrowseForFolder / SHGetPathFromIDList（选目录框）
#include "ChatMessageMenu.h"          // ChatImageActionData / ChatMessageMenuId（图片气泡右键动作）
#include "ChatFileShareCard.h"        // ApplyFileShareCard（文件共享卡片）
#include "ChatShareCardBridge.h"      // MakeShareCardLiveState / RefreshShareCardsInList
#include "../../Feature/FileShare/FileShareHost.h"   // 文件共享模块是否已加载、打开共享与本会话共享浮层
#include "../../Feature/SecretChat/SecretChatHost.h" // 关窗时撤回本人发出的密聊邀请
#include "ChatSecretMode.h"                          // 密聊状态下各按钮是否可用、标题后缀（docs/密聊方案.md 4.7）
#include "SecretImageOverlay.h"                      // 密聊图片的大图浮层（docs/密聊方案.md 4.10）
#include "ChatIconButton.h"                          // 头部文件共享按钮的角标
#include "../../Feature/ScreenShare/ScreenShareHub.h" // 屏幕共享：发起、加入、查询与各会话的共享状态
#include "../../Feature/Voice/VoiceHub.h"                 // 语音消息：开始、停止、取消录音
#include "ChatReceivedVoice.h"                             // 语音消息：收到或翻到时做成气泡、查已听状态
#include "ChatVoiceBubble.h"                               // 语音消息：发送中气泡、能否重发、气泡动作
#include "../../net/VoiceContent.h"                        // 语音消息：发送时把波形编成 Base64
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"  // media_voice_record_ok 等开始录音的结果
#include "ChatScreenShareCard.h"      // ApplyScreenShareCard（屏幕共享卡片）
#include "ChatCloudRecordCard.h"      // ApplyCloudRecordCard（云端录像卡片）
#include "ChatGroupInviteCard.h"      // ApplyGroupInviteCard（入群邀请卡片）
#include "ChatCallCard.h"             // ApplyCallCard（一对一通话卡片）
#include "ChatSystemTip.h"            // MakeSystemTipRow（屏幕共享结束提示等系统消息）、MakeShakeTipRow / ShakeTipText（窗口抖动提示）
#include "ChatImageSaveAs.h"          // SaveChatImageAs（图片另存为，与群聊窗共用）
#include "../BalloonMain/ForwardRequest.h"    // balloonmain::ForwardRequest（多选转发投给主面板）
#include "ChatReceivedFile.h"         // 文件气泡的构造与本地文件动作（与群聊窗共用）
#include "ChatDropModel.h"            // 拖进来的路径归类成 DropPlan
#include "ChatLinkDetect.h"           // IsSafeLinkUrl（打开链接前的协议白名单）
#include <windowsx.h>                 // GET_X_LPARAM / GET_Y_LPARAM（旁听点击定焦点区域）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   // 拖放跳过项的轻量提示
#include "../BalloonHistory/FolderLocalStore.h"  // 文件夹本地落点登记库（按会话 id 查根目录）
#include "../BalloonHistory/V2MsgQuery.h"      // HistoryTypeFilter：打开消息记录窗时预选哪个类型胶囊
#include <gdiplus.h>                  // 读图片真实宽高以算展示尺寸
#include <shellapi.h>                 // ShellExecute（文件气泡"打开文件"）
#pragma comment(lib, "gdiplus.lib")
#include "ChatScreenshotMenu.h"                         // 剪刀右边 ▾ 的截图选项菜单、截图请求编号
#include "ChatRecordMenu.h"                             // 录制按钮的下拉菜单（开始录制、录制管理）
#include "../../Feature/Screenshot/ScreenshotHotkey.h"  // 剪刀悬停提示里的快捷键文字
#include "../../Feature/Screenshot/ScreenshotEntry.h"   // 全局配置里的截图快捷键与「截图时隐藏当前窗口」
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "ChatRichContentSplit.h"     // 输入框图文混排的有序拆分（发送用）
#include "ChatSendLimit.h"            // 发送前检查单条消息体积；发送失败的原因文案
#include "ChatReedit.h"               // 「重新编辑」把原文放回输入框
#include "RecallEdit.h"               // 撤回提示行的「编辑」：快照查询、到期定时器（docs/撤回后重新编辑方案.md）
#include "ChatMemoPrefill.h"          // 「添加到备忘」的预填内容（docs/备忘与提醒方案.md §8）
#include "../BalloonMemo/MemoCenter.h"   // 新建备忘的编辑窗口（CMemoCenter::OpenEditor）
#include "../../Feature/Memo/MemoClock.h"   // NowLocalMinute：新建备忘的默认时间
#include "../../Feature/Memo/MemoRule.h"    // kMinutesPerDay
#include "ChatCodeMessage.h"                    // ApplyCodeFields：收到与拉取的代码片段消息
#include "ChatCodeBlock.h"                      // NormalizeCodeNewlines：粘贴识别把输入框文字移进发送代码窗口
#include "../../Common/Code/CodeLexer.h"        // codesnippet::CodeLanguageId：发送代码片段时写 content.lang
#include "../BalloonCode/CodeViewerWindow.h"     // 代码片段卡片打开的代码查看窗
#include "ChatFaceTag.h"              // 内联表情的 dwUser 标记编码 + 显示尺寸
#include "../BalloonChat/BalloonChatMsgFormat.h"  // BodyToDigest（会话摘要里表情→[表情]）
#include "ChatMarkdown.h"                         // ApplyMarkdownEditorButton（工具栏的 Markdown 编辑器按钮）
#include "../BalloonMarkdown/MarkdownEditorManager.h"   // 打开 Markdown 编辑器（docs/Markdown消息方案.md 第 11.3 节）
#include "../BalloonMarkdown/MarkdownEditorModel.h"     // MarkdownBodyFromSourceUtf8：编辑器发来的文档转成聊天正文
#include "ChatSendMode.h"             // 发送键模式（Enter / Ctrl+Enter）
#include "ChatSendMenu.h"             // 发送按钮 ▾ 菜单：快捷回复与发送键（docs/快捷回复方案.md）
#include "../BalloonSettings/SettingsPageModel.h"   // kPageChat：「设置快捷回复…」打开的设置页
#include "ChatBannerLabel.h"          // 顶部离线 / 正在连接提示带（带底色的 DuiLabel 子类）
#include "ChatSendButton.h"           // ChatSendButton::DUIN_SENDBTN_DROPDOWN
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"      // 发送模式切换菜单
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"    // 提示文字 SetText
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiTrace.h"   // BUI_TRACE：打开窗口与首屏历史渲染的时序跟踪
#include "HistoryRenderBreakdown.h"                              // 首屏历史渲染按消息类型分段计时
#include <objbase.h>                  // CoCreateGuid（生成 clientmsgid）
#include <vector>
#include <string>
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译
#include "../../Feature/Ocr/OcrLauncher.h"   // ocr::LaunchOcrForFile：图片右键菜单「图片文字识别」
#include "../../Feature/Lock/LockHost.h"            // IsScreenLocked：锁屏期间收到抖动不调到前台
#include "../../Feature/Lock/LockSuppressPolicy.h"  // DecideDuringLock

#pragma comment(lib, "ole32.lib")     // CoCreateGuid

// 聊天字体偏好里单聊会话的会话类型（与协议 conversation_type 的单聊取值一致），
// 会话ID 为对端 userid。见 Session/ChatFontPrefs.h。
static const int kChatFontCtypeSingle = 1;

// ---- 文本编码与 clientmsgid 辅助（文件作用域） -------------------------

// CString(UTF-16) → UTF-8 std::string。空串安全。
static std::string CStringToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // UTF-8 单个 BMP/补充字符最多 4 字节，按 4 倍 + 1 预留缓冲。
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// UTF-8 std::string → CString(UTF-16)。空串安全。
static CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    // wchar 个数不超过 UTF-8 字节数，按字节数 + 1 预留缓冲。
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// 按 userid 从固定调色板挑头像底色（与联系人树/搜索结果同口径，保证同一人
// 颜色稳定）。
static COLORREF AvatarColorForUser(UINT userid)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x7A, 0x45), RGB(0x3A, 0x86, 0xD9), RGB(0x2B, 0xB6, 0x73),
        RGB(0x8B, 0x5C, 0xF6), RGB(0xEF, 0x6C, 0x9A), RGB(0xF5, 0x9E, 0x0B),
        RGB(0x14, 0xB8, 0xA6), RGB(0xE1, 0x5B, 0x4C),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[(int)(userid % (UINT)n)];
}

// 生成一个 clientmsgid（UUID 十六进制串，纯 ASCII）。用于消息去重与回执回填。
static std::string GenClientMsgId()
{
    GUID guid = { 0 };
    char sz[40] = { 0 };
    if (::CoCreateGuid(&guid) == S_OK)
    {
        sprintf_s(sz, sizeof(sz),
                  "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        // 极少数失败时用时间戳兜底，保证非空。
        sprintf_s(sz, sizeof(sz), "CM-%lld", (long long)::GetTickCount64());
    }
    return std::string(sz);
}

// ---- 文件作用域常量 -----------------------------------------------------

// 单聊窗固定客户区尺寸（逻辑像素）—— 对照设计稿 07 的气泡聊天窗。
// 输入框里内联图片（截图、粘贴或拖入的图片）的展示上限（像素）：等比缩放到这个方框以内，
// 避免大图把输入框撑得过高。只影响展示，发送的仍是原图。
static const int kInlineImageMaxPx = 320;

// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修正 ResizeClient 的窗口尺寸时由 640×620 加上
// 修复前 ResizeClient 多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小；2026-10-06 按用户要求
// 由 658×667 调大为 760×720（工具栏加了「代码」按钮、输入区也调高了，见 docs/代码片段方案.md 第 3.1 节）。
static const int kChatWindowWidth  = 760;
static const int kChatWindowHeight = 720;

// 单聊窗允许缩放到的最小尺寸见 ChatWindowLayout.h 的 kChatWindowMinWidth / kChatWindowMinHeight
// （布局单测要按最小宽度检查 Markdown 格式条，故放在布局头文件里）。

// 标题栏高度（逻辑像素）—— 常规标题栏，承载 min/max/close。
static const int kChatWindowTitleBarHeight = 32;

// mock 自动回复定时器 id 与延时（毫秒）—— 发出消息后过一会儿模拟对方回话。
static const UINT_PTR kReplyTimerId = 0xC001;
static const UINT     kReplyDelayMs = 1400;

// 窗口抖动动画定时器 id 与单步间隔（毫秒）。
static const UINT_PTR kShakeTimerId    = 0xC002;
static const UINT     kShakeStepMs     = 24;

// 「正在输入」定时器 id 与 tick 间隔（毫秒）。同一个定时器兼管两件事：本端停手满
// kTypingIdleMs 后发 state=0，以及对端提示满 kTypingExpireMs 后自动收起。1 秒的
// 精度对这种秒级状态足够，也不至于频繁唤醒 UI 线程。
static const UINT_PTR kTypingTimerId = 0xC003;
static const UINT     kTypingTickMs  = 1000;

// 「打开链接」延后执行的定时器 id。点链接不立刻打开，而是等一个系统双击间隔 ——
// 双击的第一下本身就是一次完整单击，若立刻打开，用户双击链接想选中整条时会平白
// 弹出浏览器。这段时间内若收到双击（WM_CHAT_CANCEL_LINK_OPEN），这次打开就作废。
// 延时取 ::GetDoubleClickTime()（跟随系统设置），故这里只定 id、不定时长。
static const UINT_PTR kLinkOpenTimerId = 0xC004;

// 撤回提示行「编辑」的到期定时器 id（docs/撤回后重新编辑方案.md §3.3）。时长按最早的到期时刻现算，
// 见 balloonchatwnd::RescheduleRecallEditTimer。
static const UINT_PTR kRecallEditTimerId = 0xC005;

// 抖动偏移序列（相对起点的像素位移）：先大后小来回晃几下，最后归位到 (0,0)。
static const int kShakeOffsets[][2] =
{
    {  6, -6 }, { -6, -6 }, { -6,  6 }, {  6,  6 },
    {  4, -4 }, { -4, -4 }, { -4,  4 }, {  4,  4 },
    {  2, -2 }, { -2,  2 }, {  0,  0 },
};
static const int kShakeStepCount = (int)(sizeof(kShakeOffsets) / sizeof(kShakeOffsets[0]));

// 窗口抖动收发两侧的最小间隔见 ShakeRateLimiter.h 的 kShakeSendMinIntervalMs（5 秒）
// 与 kShakeRecvMinIntervalMs（4 秒）。接收侧较短，用来容纳网络延迟的波动。

// 消息内容类型常量，与 net/Protocol.h 的 message_type 对齐 —— 本窗口只用到
// 文本、图片、文件几类。此处另立常量而不直接 include Protocol.h：原先的理由是
// 协议头与 v1 的 net/Msg.h 枚举名冲突，v1 协议头已于 2026-08-14 删除、该限制不复
// 存在；现在的理由只剩「本文件用不上协议头的其余几百个定义」。取值一致，日后
// 想直接用 message_type 也可以。
static const int kMsgTypeText  = 1;    // message_type_text
static const int kMsgTypeImage = 2;    // message_type_image
static const int kMsgTypeFile  = 3;    // message_type_file
static const int kMsgTypeVoice = 4;    // message_type_voice：语音消息，画成语音气泡（docs/语音消息方案.md）
static const int kMsgTypeShake = 13;   // message_type_shake
static const int kMsgTypeFolder = 18;  // message_type_folder：文件夹传输记录条（服务端在传输走到终态后生成）
static const int kMsgTypeMerged = 15;  // message_type_merged：合并转发的聊天记录，画成卡片
static const int kMsgTypeFileShare = 20;  // message_type_fileshare：文件共享卡片（服务端生成），画成卡片
static const int kMsgTypeScreenShare = 19;  // message_type_screenshare：屏幕共享卡片（服务端在发起共享时生成），画成卡片
static const int kMsgTypeSystem = 17;  // message_type_system：系统提示；单聊里只有屏幕共享的结束提示，画成居中提示行
static const int kMsgTypeRtc = 16;  // message_type_rtc：通话记录；单聊里是一对一通话卡片（服务端在呼叫时生成、结束时改写），画成卡片
static const int kMsgTypeCloudRecord = 24;  // message_type_cloudrecord：云端录像卡片（服务端以开启者名义生成），画成卡片
static const int kMsgTypeGroupInvite = 21;  // message_type_group_invite：入群邀请卡片（服务端在邀请人与被邀请人的单聊里生成），画成卡片

// 会话类型常量，与 net::conversation_type（net/Protocol.h）对齐。本窗口是单聊窗，
// 发撤回请求时固定取单聊；同上，不 include Protocol.h 而是就地取值。
static const int kConvTypeSingle = 1;  // conversation_single

// 对端在线状态常量，与 net/Protocol.h 的 enum presence_type 对齐；同上，就地复述取值。
// 只用得上"离线"这一个：离开与勿扰的用户连接仍在，服务端判定文件夹传输能否发起时
// 看的是"名下有没有在线会话"，那两种状态照样发得出去。
static const int kPresenceOffline = 0;   // presence_offline：对方没有任何在线连接

// 文件回执状态常量，与 file_ack_state（net/Protocol.h:398）对齐。理由同上 ——
// 本文件刻意不 include Protocol.h，故就地复述一份取值。
static const int kFileAckReceiving = 1;   // file_ack_receiving：对方点了接收、正在下载
static const int kFileAckReceived  = 2;   // file_ack_received：对方已完整收到并校验通过
static const int kFileAckRejected  = 3;   // file_ack_rejected：对方点了拒绝

// 文件夹传输的会话状态，与 net/Protocol.h 的 enum folder_transfer_state 对齐。理由同上 ——
// 本文件刻意不 include Protocol.h，故就地复述一份取值。这几个值同时也与
// ChatFolderStateModel.h 里 ChatFolderState 的 1~8 号逐值相同，故转换是恒等映射；
// 仍然分两套名字，是因为一个来自协议、一个属于界面，两者今后未必继续同步。
static const int kFolderStateRinging      = 1;   // folder_state_ringing：邀请已送达，等对方应答
static const int kFolderStateTransferring = 2;   // folder_state_transferring：目录已建好，正在收发字节
static const int kFolderStateRejected     = 3;   // folder_state_rejected：对方点了拒绝
static const int kFolderStateCanceled     = 4;   // folder_state_canceled：发送方在应答前撤回了邀请
static const int kFolderStatePeerOffline  = 5;   // folder_state_peer_offline：对方不在线
static const int kFolderStateTimeout      = 6;   // folder_state_timeout：邀请超时无人应答
static const int kFolderStateFailed       = 7;   // folder_state_failed：传输失败，保留期内可重试续传
static const int kFolderStateCompleted    = 8;   // folder_state_completed：全部传完并通过校验

// 文件夹传输用到的结果码，与 net/Protocol.h 的 error_code / error_code_folder_* 对齐。
// 同样是就地复述，理由见上。
static const int kFolderErrOk             = 0;     // kFolderErrOk：成功
static const int kFolderErrTooManyFiles   = 609;   // kFolderErrTooManyFiles：文件数超出服务端上限
static const int kFolderErrTooLarge       = 610;   // kFolderErrTooLarge：总字节数超出服务端上限
static const int kFolderErrReadFail       = 614;   // kFolderErrReadFail：本端读源目录 / 源文件失败
static const int kFolderErrCanceled       = 617;   // kFolderErrCanceled：用户取消
static const int kFolderErrSessionExpired = 618;   // kFolderErrSessionExpired：会话已超出保留期，不能再续传

// 进度百分比的满值。气泡的 folderProgress 取值区间是 0~100，完成时置满。
static const int kFolderFullPercent = 100;

// 图片气泡内图片的最大展示尺寸（逻辑像素）：超出按比例缩小，小图保持原尺寸。
static const int kImageDisplayMaxW = 220;
static const int kImageDisplayMaxH = 260;

// 把图片真实宽高按比例缩到不超过 (kImageDisplayMaxW, kImageDisplayMaxH) 的展示
// 尺寸；输出至少 1×1。realW/realH 非正时回退到一个占位方块尺寸。
static void ComputeImageDisplaySize(int realW, int realH, int& dispW, int& dispH)
{
    if (realW <= 0 || realH <= 0)
    {
        dispW = 160;
        dispH = 120;
        return;
    }
    double scale = 1.0;
    if (realW > kImageDisplayMaxW)
        scale = (double)kImageDisplayMaxW / (double)realW;
    if (realH * scale > kImageDisplayMaxH)
        scale = (double)kImageDisplayMaxH / (double)realH;
    if (scale > 1.0)
        scale = 1.0;   // 小图不放大
    dispW = (int)(realW * scale + 0.5);
    dispH = (int)(realH * scale + 0.5);
    if (dispW < 1)
        dispW = 1;
    if (dispH < 1)
        dispH = 1;
}

void CBalloonChatWindow::SecretImageDisplaySize(int realW, int realH, int& dispW, int& dispH)
{
    ComputeImageDisplaySize(realW, realH, dispW, dispH);
}

// 给客户区里指定 id 的控件注册一段 tooltip 文字（控件不存在则静默跳过）。
static void RegisterTip(balloonwjui::DuiControl* content, UINT ctrlId,
                        LPCTSTR text)
{
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* c = content->FindCtrlById(ctrlId);
    if (c != NULL)
    {
        balloonwjui::DuiToolTipMgr::Inst().Register(c, text);
    }
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonChatWindow::CBalloonChatWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_replyIndex(0)
    // kConnUiOnline：能开出聊天窗说明此刻本就在线，随后由主面板把真实状态推过来。
    // 初始化顺序须与声明顺序一致（本成员声明在 m_peerUserId 之前）。
    , m_connUiState(0)
    , m_peerUserId(0)
    , m_hostPanel(NULL)
    , m_inCallMedia(0)
    , m_pendingLocateMsgid(0)
    , m_pinnedCount(0)          // 开窗先按"一条都没钉"起步，提示带隐藏；随后由服务端全集校准
    , m_peerPresence(-1)        // -1=未注入：头部回退 mock，直到打开方注入真实 presence
    , m_peerSignatureSet(false)
    , m_peerFacetype(0)         // 0=自动：标题/任务栏头像按昵称首字 + userid 哈希底色
    , m_titleIconBmp(NULL)
    , m_hTaskIcon(NULL)
    , m_peerIconGray(false)
    , m_shakeStep(-1)
    , m_hInputFont(NULL)
    , m_inputDefaultTextColor(RGB(0, 0, 0))
    , m_focusArea(CHAT_FOCUS_INPUT)   // 开窗时焦点在输入框
    , m_dropToast(NULL)
    , m_secretInviting(false)
    , m_secretState(0)                // balloonchatwnd::kSecretStateNormal：开窗时没有密聊
    , m_secretSessionId(0)
    , m_secretEndReason(0)
    , m_secretMyProtect(0)
    , m_secretPeerProtect(0)
    , m_secretExitTipPending(false)
    , m_secretImageOverlay(NULL)
{
    // m_sendShakeLimiter / m_recvShakeLimiter 由 ShakeRateLimiter 默认构造，
    // 初始为"从未放行"状态，无需在此显式初始化。
    m_shakeOrigin.x = 0;
    m_shakeOrigin.y = 0;
}

void CBalloonChatWindow::SetPeerUserId(UINT peerUserId)
{
    m_peerUserId = peerUserId;
}

void CBalloonChatWindow::SetPeerName(LPCTSTR name)
{
    m_peerName = (name != NULL) ? name : _T("");
}

void CBalloonChatWindow::SetHostPanelHwnd(HWND hostPanel)
{
    m_hostPanel = hostPanel;
}

void CBalloonChatWindow::SetPeerPresence(int presence)
{
    m_peerPresence = presence;
    ApplyPeerStatusToHeader();
    ApplyAvatarGrayToMessageList();

    // 标题栏与任务栏的对方头像：对方离线时画成灰度。灰彩没变时不重建，免得每次状态推送都重新解码头像
    const bool gray = balloonmain::ShouldGrayUserAvatar(false, m_peerPresence, m_connUiState);
    if (IsWindow() && gray != m_peerIconGray)
    {
        RebuildPeerIcons();
    }
}

void CBalloonChatWindow::SetPeerSignature(const CString& signature)
{
    m_peerSignature    = signature;
    m_peerSignatureSet = true;
    ApplyPeerStatusToHeader();
}

void CBalloonChatWindow::ApplyPeerStatusToHeader()
{
    //窗口尚未建好（InitUi 之前）取不到客户区 / 控件，安全返回；InitUi 末尾会再调一次兜底。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatHeaderInfo* hdr = dynamic_cast<balloonchatwnd::ChatHeaderInfo*>(
        content->FindCtrlById(balloonchatwnd::kIdHeaderInfo));
    if (hdr == NULL)
    {
        return;
    }
    if (m_peerPresence >= 0)
    {
        hdr->SetPresence(m_peerPresence);
    }
    if (m_peerSignatureSet)
    {
        hdr->SetSignature(m_peerSignature);
    }
    //头部信息块是静态宿主里的纯绘制控件，改完数据须显式重绘才生效。
    hdr->Invalidate();
}

void CBalloonChatWindow::ApplyAvatarGrayToMessageList()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list = dynamic_cast<balloonchatwnd::ChatMessageList*>(
        content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    // 对端不会是本人（不允许和自己聊天），按别人的规则只看对方的在线状态；本人的气泡只看本端连接态
    list->SetSenderAvatarGray((int64_t)m_peerUserId,
                              balloonmain::ShouldGrayUserAvatar(false, m_peerPresence, m_connUiState));
    list->SetSelfAvatarGray(balloonmain::ShouldGrayAvatar(m_connUiState));
}

void CBalloonChatWindow::RefreshTypingHeader()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatHeaderInfo* hdr = dynamic_cast<balloonchatwnd::ChatHeaderInfo*>(
        content->FindCtrlById(balloonchatwnd::kIdHeaderInfo));
    if (hdr == NULL)
    {
        return;
    }
    hdr->SetTyping(m_peerTyping.IsTyping());
    //头部信息块是纯绘制控件，改完数据须显式重绘才生效（同 ApplyPeerStatusToHeader）。
    hdr->Invalidate();
}

void CBalloonChatWindow::OnPeerTyping(int state)
{
    //状态没变就不重绘 —— 对方持续打字时每 4 秒来一包，没必要每包都刷一次界面。
    if (!m_peerTyping.OnNotify(::GetTickCount64(), state))
    {
        return;
    }
    LOG_INFO("单聊窗刷新对端正在输入提示: peer=%u, typing=%d.",
             m_peerUserId, m_peerTyping.IsTyping() ? 1 : 0);
    RefreshTypingHeader();
}

void CBalloonChatWindow::SendTypingAction(int action)
{
    if (action == balloonchatwnd::typing_action_none)
    {
        return;
    }

    //调试入口没有对端，无处可发。
    if (m_peerUserId == 0)
    {
        return;
    }

    //密聊状态下不发「正在输入」（docs/密聊方案.md 4.4：以后要加时走密聊信封，不走普通通道）
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        return;
    }

    //离线 / 正在连接时不发：这条提示可有可无，发不出去也不该像发消息那样弹提示打扰
    //用户，所以这里不走 CheckOnlineFor（它会弹提示框），只安静地跳过。
    if (m_connUiState != (int)balloonmain::kConnUiOnline)
    {
        return;
    }

    const int state = (action == balloonchatwnd::typing_action_send_typing)
                      ? net::typing_state_typing
                      : net::typing_state_stopped;
    CIUSocket::GetInstance().ReportTyping((int64_t)m_peerUserId, state);
}

void CBalloonChatWindow::StopTypingNotify()
{
    SendTypingAction(m_typingThrottle.OnStop());
}

LRESULT CBalloonChatWindow::OnInputChanged(UINT /*uMsg*/, WPARAM wParam,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //失焦（wParam 非 0）：用户切走了，无论输入框里还剩什么都立即收尾。
    if (wParam != 0)
    {
        StopTypingNotify();
        return 0;
    }

    //普通内容变化：取当前文本判断空不空，交节流器决定发不发包。这里只要知道
    //"有没有内容"，不必像 DoSend 那样枚举富文本各段，取纯文本即可。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return 0;
    }

    CString text = input->GetText();
    text.Trim();
    SendTypingAction(m_typingThrottle.OnInputChanged(::GetTickCount64(), text.IsEmpty()));
    return 0;
}

void CBalloonChatWindow::SetPeerAvatar(int facetype, const std::string& customfaceUtf8)
{
    m_peerFacetype   = facetype;
    m_peerCustomface = customfaceUtf8;
    if (IsWindow())
    {
        RebuildPeerIcons();
    }
}

void CBalloonChatWindow::UpdatePeerInfo(LPCTSTR name, LPCTSTR signature,
                                        int facetype, const std::string& customfaceUtf8)
{
    // 窗口未打开就不动 UI（重新打开时主面板会按最新缓存注入）。
    if (!IsWindow())
    {
        return;
    }

    // 名字或头像变了都要重建标题栏 / 任务栏图标（头像可能变了而名字没变）。
    bool needRebuild = false;

    CString newName = (name != NULL) ? name : _T("");
    if (!newName.IsEmpty() && newName != m_peerName)
    {
        m_peerName = newName;
        m_windowTitle = m_peerName;
        //密聊状态下保留「（密聊）」后缀
        SetTitle(balloonchatwnd::IsSecretState(m_secretState)
                     ? CString(balloonchatwnd::SecretWindowTitle((LPCWSTR)m_windowTitle).c_str())
                     : m_windowTitle);
        needRebuild = true;
    }
    if (facetype != m_peerFacetype || customfaceUtf8 != m_peerCustomface)
    {
        m_peerFacetype   = facetype;
        m_peerCustomface = customfaceUtf8;
        needRebuild = true;
    }
    if (needRebuild)
    {
        RebuildPeerIcons();
    }

    // 签名:复用 SetPeerSignature(内部 ApplyPeerStatusToHeader 刷头部签名行 + 重绘)。
    SetPeerSignature((signature != NULL) ? CString(signature) : CString());
}

void CBalloonChatWindow::RebuildPeerIcons()
{
    if (m_peerUserId == 0)
    {
        return;   // 调试入口不设图标
    }
    // 按对端 (facetype, customface) 解出头像呈现：字形头像 / 自动头像给字形 + 底色；
    // 真图头像(facetype=2)缓存命中给真图路径（未命中会入队下载，完成后 OnAvatarDownloaded
    // 回刷），由本函数据 m_imagePath 是否为空决定标题栏 / 任务栏画真图还是字形。
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        (int64_t)m_peerUserId, m_peerFacetype, m_peerCustomface,
        m_peerName.IsEmpty() ? NULL : (LPCTSTR)m_peerName, selfId);
    CString  glyph    = ar.m_glyph;
    COLORREF color    = ar.m_color;
    bool     useImage = !ar.m_imagePath.IsEmpty();

    // 对方离线时两个图标都画成灰度：字形头像换灰色底，真图头像换灰度图像。对端不会是本人
    // （不允许和自己聊天），故按别人的规则判断，只看对方的在线状态
    const bool gray = balloonmain::ShouldGrayUserAvatar(false, m_peerPresence, m_connUiState);
    m_peerIconGray = gray;
    const COLORREF iconColor = gray ? balloonmain::MakeGrayColor(color) : color;

    // 标题栏图标:先释放旧位图,再重建(48px 生成,标题栏 StretchBlt 到 16px)。真图命中画圆形
    // 真图、否则画字形 monogram；真图解码失败也回退字形。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (useImage)
    {
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIconFromImage(
            (LPCWSTR)ar.m_imagePath, balloonchatwnd::kChatTitleBarBg, 48, gray);
    }
    if (m_titleIconBmp == NULL)
    {
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIcon(
            glyph, iconColor, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    // 任务栏 / Alt-Tab 图标:先毁旧 HICON,再用 32px 位图重建（真图圆形 / 否则字形 monogram）。
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    HBITMAP bmp32 = NULL;
    if (useImage)
    {
        // 圆形真图（圆角半径=半边长即圆，与联系人树一致）。
        bmp32 = balloonmain::MakeRoundedAvatarFromFile((LPCWSTR)ar.m_imagePath, 32, 16);
    }
    if (bmp32 == NULL)
    {
        bmp32 = balloonmain::MakeMonogramAvatar(glyph, iconColor, 32);
    }
    // 真图头像的灰度副本另行生成（字形头像上面已换成灰色底，再转一次结果不变）。
    // 生成失败时保留彩色位图，不画成空白
    if (bmp32 != NULL && gray && useImage)
    {
        HBITMAP grayBmp = balloonmain::MakeGrayAvatarBitmap(bmp32);
        if (grayBmp != NULL)
        {
            ::DeleteObject(bmp32);
            bmp32 = grayBmp;
        }
    }
    if (bmp32 != NULL)
    {
        m_hTaskIcon = balloonchatwnd::IconFromBitmap(bmp32);
        ::DeleteObject(bmp32);
        if (m_hTaskIcon != NULL)
        {
            ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG,   (LPARAM)m_hTaskIcon);
            ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hTaskIcon);
        }
    }
}

CBalloonChatWindow::~CBalloonChatWindow()
{
    // 没有经过 OnCloseMsg 就销毁时（如登出时直接销毁窗口），同样取消或丢弃还没发送的录音、停止试听
    m_voiceSession.OnWindowClosing();
    // 本窗口正在播放语音时一并停止，之后语音中心不再给本窗口发播放通知（第 3e 步）
    balloonvoice::CVoiceHub::GetInstance().DetachPlayListener(this);
    // 「发送代码」窗口是独立的顶层窗口，对象析构时仍持有窗口句柄会触发断言
    if (m_codeComposeWnd)
    {
        m_codeComposeWnd->Close();
    }
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
    // 标题栏图标位图与任务栏图标由本类持有，这里释放。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    // 输入框自定义字体由本窗口持有（RichEdit 不接管所有权），在此释放。
    if (m_hInputFont != NULL)
    {
        ::DeleteObject(m_hInputFont);
        m_hInputFont = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonChatWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    //分段计时：建窗口、构建客户区、首次显示各花多久，首屏历史渲染完再看总耗时。
    m_openStopWatch.Restart();
    BUI_TRACE("CHATWND-OPEN-BEGIN peer=%u", m_peerUserId);

    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("single chat window: Create failed, peer=%u.", m_peerUserId);
        return;
    }
    const double createdMs = m_openStopWatch.ElapsedMs();

    // 标题栏：常规（非透明）标题栏，承载 min/max/close 三按钮。窗口标题
    // 取对方真实昵称：优先查 CUserMgr，查不到时回退 "用户<id>"；调试入口
    // （m_peerUserId==0）下保留占位名。
    CString title = TR(L"单聊");
    if (m_peerUserId != 0)
    {
        if (!m_peerName.IsEmpty())
        {
            // 优先用打开方传入的真实昵称（v2 下 CUserMgr 好友列表为空）。
            title = m_peerName;
        }
        else if (m_lpFMGClient != NULL)
        {
            tstring nick = m_lpFMGClient->m_UserMgr.GetNickName(m_peerUserId);
            if (!nick.empty())
            {
                title = nick.c_str();
            }
            else
            {
                title.Format(TR(L"用户%u"), m_peerUserId);
            }
        }
        else
        {
            title.Format(TR(L"用户%u"), m_peerUserId);
        }
    }
    m_windowTitle = title;
    SetTitle(title);

    // 窗口图标 = 对端头像（字形 / 真图，按 (facetype, customface) 解析），同时设到标题栏
    // 左侧与任务栏 / Alt-Tab。统一走 RebuildPeerIcons，确保首次打开即按对端头像渲染（含真图
    // 缓存命中）；调试入口（m_peerUserId==0）下 RebuildPeerIcons 早退、保留默认图标。
    RebuildPeerIcons();
    SetTitleBarHeight(kChatWindowTitleBarHeight);
    SetButtons(true, true, true);
    SetMinSize(balloonchatwnd::kChatWindowMinWidth, balloonchatwnd::kChatWindowMinHeight);

    // 让气泡能收到双击，用于双击图片放大查看。
    // （2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);

    // 真实会话：拉最近一页历史消息（kHistoryPageSize 条，beforeMsgId=0 从最新开始），应答经
    // 主面板路由回 OnHistoryMessages。调试入口（m_peerUserId==0）不拉。
    // 这两个请求放在 InitUi 之前发出（2026-10-04 起）：建客户区控件树与首次显示共需几十毫秒，
    // 先发请求，服务端处理与本端建界面就能同时进行；而且 InitUi 里还会发文件共享、屏幕共享的
    // 状态查询，服务端按顺序处理同一条连接上的请求，拉历史排在前面才不会等它们。
    // 应答由网络线程投递给界面线程，要等本函数返回、主面板把本窗口登记进窗口表之后才会被取出处理，
    // 所以提前发出不会出现应答先到、找不到窗口的情况。
    if (m_peerUserId != 0)
    {
        CIUSocket::GetInstance().RequestSyncMessages(
            1, (int64_t)m_peerUserId, 0, balloonchatwnd::kHistoryPageSize);

        // 再拉一次本会话被 Pin 的消息。上面那次只覆盖"最近一页"，而被钉的往往恰恰是
        // 更早的老消息 —— 不单独拉一次，历史里那些气泡就不会带图钉标记。应答经主面板
        // 的 OnPinnedMessagesArrived 落库并回投到本窗口。
        CIUSocket::GetInstance().GetPinnedMessages(kConvTypeSingle, (int64_t)m_peerUserId);
    }

    InitUi();
    const double initUiMs = m_openStopWatch.ElapsedMs();
    ResizeClient(kChatWindowWidth, kChatWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    const double shownMs = m_openStopWatch.ElapsedMs();
    BUI_TRACE("CHATWND-OPEN-SHOWN create=%.3fms initui=%.3fms shown=%.3fms",
              createdMs, initUiMs, shownMs);

    if (m_peerUserId != 0)
    {
        // 补出对方此前发来、仍在等本端应答的文件夹邀请气泡。收到邀请不再弹窗（见
        // CBalloonMainPanel::OnFolderInviteNotify），用户多半是看到会话列表未读之后才
        // 点进来的，这一步是他唯一能看到「接收 / 另存为 / 拒绝」的机会。历史那一页回来时会先
        // 清空列表，故 OnHistoryMessages 渲染完首屏后还要再补一次。
        RestorePendingFolderInvite();
        // 密聊邀请同理：窗口没开时到达的邀请不建窗，用户在倒计时内打开窗口时补出卡片（docs/密聊方案.md 4.8）
        RestoreSecretInviteCards();
    }

    //三个时刻都是从打开起算的累计值：create 为窗口建好，initui 为客户区控件树建好，
    //shown 为首次显示并同步重绘完成。首屏历史要等服务端应答，另见 OnHistoryMessages。
    LOG_INFO("single chat window shown: peer=%u, create=%.1fms, initui=%.1fms, shown=%.1fms.",
             m_peerUserId, createdMs, initUiMs, shownMs);
}

// =======================================================================
// 客户区
// =======================================================================

//「回车是发送还是换行」的查询回调，注册给输入框控件。
//
// 判断放在这里而不是控件里：它要读用户选的发送模式，而那个设置存在配置文件中，
// 读它会牵进配置文件读写与主程序目录这一整条依赖链；输入框控件还要被编进单元
// 测试工程，那边刻意不引入这些。
//   bCtrlDown：按下回车时 Ctrl 是否也按着。
//   返回：true 表示发送，false 表示插入换行。
static bool ChatSendOnEnterQuery(bool bCtrlDown, void* /*pCtx*/)
{
    // 回车发送模式 —— 不按 Ctrl 即发送、按 Ctrl 换行；
    // Ctrl+回车发送模式 —— 按住 Ctrl 才发送、单按回车换行。
    return (balloonchatwnd::GetChatSendMode() == balloonchatwnd::kSendOnEnter)
           ? !bCtrlDown : bCtrlDown;
}


void CBalloonChatWindow::InitUi()
{
    // 构建单聊窗客户区控件树（来自 ChatWindowLayout 的内嵌 XML）。真实会话
    // （m_peerUserId != 0）起始为空，不预填 mock 历史；调试入口才用 mock。
    std::unique_ptr<balloonwjui::DuiControl> root =
        balloonchatwnd::BuildChatWindowBody(m_peerUserId == 0);
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildChatWindowBody 返回空 —— 单聊窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 输入框是无窗口富文本控件的聊天定制子类（见 ChatInputRichEdit.h）。
    // 它没有子窗口，**不需要任何创建步骤** —— 从 XML 建出来就能用。
    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
            content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input != NULL)
    {
        input->SetShowBorder(false);   // 输入框无边框，融入输入区
        // 多行 + 自动换行。这两项在无窗口控件上是随时可改的属性，不像旧控件
        // 那样是创建期定死的窗口风格位，所以顺序上没有讲究。
        input->SetMultiLine(true);
        input->SetWordWrap(true);
        // 输入区高度由用户拖分割条决定，内容多了在框内滚动，不随内容长高。
        input->SetAutoGrow(false);
        // 粘贴一律按纯文本（聊天输入不要带来源的颜色 / 字体 / 链接 / 对象）。
        input->SetPasteAsPlainTextDefault(true);
        // 关掉输入框自带的拖放。
        //
        // 拖放在聊天窗里是**整窗**接收的：拖文件进来就发送，由 m_dropCtrl 统一
        // 处理（见 ChatDropController.h）。而一个窗口只能注册一个拖放目标 ——
        // 输入框若也要一份，就会与聊天窗那份抢，先注册的那个赢，后一个静默失败。
        // 这与旧控件时代的取舍一致：那时是把子窗口自带的拖放目标摘掉。
        input->SetDragDropEnabled(false);

        // 回车发送、「正在输入」通知。原先是往子窗口上挂两段子类过程，现在
        // 由控件自己处理，只需把宿主窗口与消息号交给它 —— 消息协议没变，
        // 本窗口接消息那一侧的代码一行都不用改。
        input->SetNotifyTarget(m_hWnd);
        input->SetSendOnEnterMsg(kSendOnEnterMsg);
        input->SetSendOnEnterQuery(&ChatSendOnEnterQuery, NULL);
        input->SetTypingChangedMsg(kInputChangedMsg);
        // 粘贴识别：往空输入框里粘贴像代码的文字时显示「以代码片段发送」提示条（docs/代码片段方案.md 第 3.1 节）
        input->SetPasteTextMsg(balloonchatwnd::kMsgChatCodePaste);
        // 粘贴 / 拖入的聊天内容里有图片时，由本窗口登记源文件并带标记插入。
        input->SetImageStager(&CBalloonChatWindow::StageImageThunk, this);

        // 按"本会话自定义 > 全局默认"取聊天字体并应用到输入框（所见即所得）；两者都
        // 没设过则保持默认外观。先记下输入框原本的文字色，恢复默认外观时要用。
        m_inputDefaultTextColor = input->GetTextColor();
        m_currentFont = ResolveChatFont();
        ApplyCurrentFontToInput();
    }

    // 字体设置浮层：装"变化"与"使用默认字体"两个回调（this 作上下文）。
    m_fontPopup.SetChangeCallback(&CBalloonChatWindow::OnFontChanged, this);
    m_fontPopup.SetUseDefaultCallback(&CBalloonChatWindow::OnUseDefaultFont, this);

    // 给头部 / 输入工具栏 / 发送按钮注册 tooltip（图标按钮无文字，
    // 尤其需要提示）。
    RegisterTip(content, balloonchatwnd::kIdHdrVoice,     TR(L"语音通话"));
    RegisterTip(content, balloonchatwnd::kIdHdrVideo,     TR(L"视频通话"));
    RegisterTip(content, balloonchatwnd::kIdHdrRemoteDesktop, TR(L"远程桌面"));
    RegisterTip(content, balloonchatwnd::kIdHdrScreenShare, TR(L"共享屏幕"));
    RegisterTip(content, balloonchatwnd::kIdTbRecord, TR(L"屏幕录制"));
    RegisterTip(content, balloonchatwnd::kIdHdrMore,      TR(L"更多"));
    RegisterTip(content, balloonchatwnd::kIdTbEmoji,      TR(L"表情"));
    RegisterTip(content, balloonchatwnd::kIdTbImage,      TR(L"图片"));
    // 文件、文件夹两个按钮的提示第二行说明也可以拖进输入框发送（2026-10-07 起；拖放只认输入框，见 ChatDropController）
    RegisterTip(content, balloonchatwnd::kIdTbFile,       TR(L"发送文件\n也可以把文件直接拖到下面的输入框里发送"));
    RegisterTip(content, balloonchatwnd::kIdTbFolder,     TR(L"发送文件夹\n也可以把文件夹直接拖到下面的输入框里发送"));
    RegisterTip(content, balloonchatwnd::kIdHdrShare,      TR(L"文件共享"));
    RefreshScreenshotTip();   // 剪刀：「截图（Alt+Z）」，随设置里的截图快捷键变化
    RegisterTip(content, balloonchatwnd::kIdTbScreenshotMenu, TR(L"截图选项"));
    RegisterTip(content, balloonchatwnd::kIdTbVoice,      TR(L"语音消息"));
    RegisterTip(content, balloonchatwnd::kIdTbFont,       TR(L"字体设置"));
    RegisterTip(content, balloonchatwnd::kIdTbShake,      TR(L"窗口抖动"));
    RegisterTip(content, balloonchatwnd::kIdTbCode,       TR(L"发送代码"));
    // 「字幕模式」按钮的提示随本会话字幕的开关切换（用户 2026-10-05 确认），状态由主面板经 SetSubtitleHighlight 设置
    balloonchatwnd::ChatIconButton* subtitleBtn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kIdHdrSubtitle))
        : NULL;
    if (subtitleBtn != NULL)
    {
        subtitleBtn->SetToggleTips(TR(L"字幕已开启"), TR(L"字幕已关闭"));
    }
    RegisterTip(content, balloonchatwnd::kIdTbPlus,       TR(L"更多功能"));
    RegisterTip(content, balloonchatwnd::kIdSendButton,   TR(L"发送"));
    // Markdown 编辑器按钮：打开独立的 Markdown 编辑器并预先选好本会话；FlamingoMarkdown.dll 不可用时隐藏
    //（docs/Markdown消息方案.md 第 11.3 节）
    RegisterTip(content, balloonchatwnd::kIdTbMarkdown, TR(L"Markdown 编辑器"));
    balloonchatwnd::ApplyMarkdownEditorButton(content, balloonchatwnd::kIdTbMarkdown);
    // 截图的剪刀与 ▾：截图模块 FlamingoScreenshot.dll 不可用时隐藏（docs/截图独立模块方案.md 3.3 节）
    balloonchatwnd::ApplyScreenshotButtons(content, balloonchatwnd::kIdTbScreenshot, balloonchatwnd::kIdTbScreenshotMenu,
                                           balloonscreenshot::IsScreenshotAvailable());
    ApplyShareButtonVisibility();
    ApplyScreenShareButtonVisibility();
    ApplyVoiceButtonVisibility();
    // 头部「密聊」按钮：密聊 DLL 就绪、企业开关打开、服务端确认开启时才显示（docs/密聊方案.md 6.2）
    RegisterTip(content, balloonchatwnd::kIdHdrSecret, TR(L"密聊"));
    // 鼠标停在密聊头部条的状态文字上时，说明防截图防不住的情形（docs/密聊方案.md 4.6 第 6 条）
    RegisterTip(content, balloonchatwnd::kIdSecretBannerText,
                TR(L"密聊能防止本程序与常见截图、录屏软件截取本窗口，防不住手机拍屏、采集卡或虚拟机宿主截取画面，以及被篡改的客户端"));
    ApplySecretEntryVisibility();
    // 录音条（语音消息第 3b、6g 步）：由录音会话在布局的占位里建出控件，平时隐藏
    {
        balloonchatwnd::VoiceRecordBarIds voiceIds;
        voiceIds.m_pause = balloonchatwnd::kIdVoicePause;
        voiceIds.m_stop = balloonchatwnd::kIdVoiceStop;
        voiceIds.m_preview = balloonchatwnd::kIdVoicePreview;
        voiceIds.m_cancel = balloonchatwnd::kIdVoiceCancel;
        voiceIds.m_send = balloonchatwnd::kIdVoiceSend;
        voiceIds.m_confirm = balloonchatwnd::kIdVoiceConfirm;
        voiceIds.m_back = balloonchatwnd::kIdVoiceBack;
        m_voiceSession.Init(this, dynamic_cast<balloonwjui::DuiHBox*>(content->FindCtrlById(balloonchatwnd::kIdVoiceBar)),
                            voiceIds);
    }
    // 登记本会话：文件共享模块数出可用共享的个数后经主面板设置头部按钮的角标
    if (m_peerUserId != 0)
    {
        balloonfileshare::CFileShareHost::GetInstance().WatchConversationShares(kConvTypeSingle, (int64_t)m_peerUserId);
    }
    // 本次登录还不知道这个会话有没有进行中的屏幕共享：查一次，结果回来后历史里的共享卡片据此显示状态
    if (m_peerUserId != 0)
    {
        balloonscreenshare::CScreenShareHub::GetInstance().Query(kConvTypeSingle, (int64_t)m_peerUserId);
    }

    // 按持久化的发送模式刷新左下角提示与输入框 placeholder（重启后保留上次选择）。
    RefreshSendModeUi();

    // 若打开方已在 CreateAndShow 之前注入过对端 presence / 签名，此处把它应用到刚建好的
    // 头部信息块（控件此刻才存在）；未注入则头部继续回退 mock。
    ApplyPeerStatusToHeader();
    // 气泡头像的灰彩同理：消息列表此刻才存在，先告知它，之后追加的消息按此设置
    ApplyAvatarGrayToMessageList();

    // 注册到图片解码缓存：本窗口里的图片气泡发起后台解码后，缩放完成会经
    // WM_CHAT_IMAGE_READY 通知本窗口重绘。注销在 OnCloseMsg。
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);

    // 「正在输入」秒级定时器：管本端停手收尾与对端提示过期两件事。只有真实会话才需要
    // ——调试入口没有对端可通告，也不会收到推送。定时器随窗口销毁自动停。
    if (m_peerUserId != 0)
    {
        ::SetTimer(m_hWnd, kTypingTimerId, kTypingTickMs, NULL);
    }

    // 单聊开启已读回执：自己发出、服务端已确认的消息下方显示「未读」或「✓ 已读」，
    // 已读状态来自对方的已读位点（cmd_message_read_notify 推送与同步消息应答）。
    // 群聊窗不开启 —— 按协议群聊不做已读回执。
    {
        balloonchatwnd::ChatMessageList* receiptList =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kIdMessageArea));
        if (receiptList != NULL)
        {
            receiptList->SetReadReceiptEnabled(true);
            // 文字消息右键菜单里给「添加到备忘」一项（docs/备忘与提醒方案.md §8）
            receiptList->SetMemoMenuMode(balloonchatwnd::kChatMemoMenuSingle);
        }
    }

    // 调试入口（--chatwindow，没有主面板也就没有服务端）下，顶部「已 Pin N 条」提示带
    // 的条数没有别处可问，就照 mock 消息里带 Pin 标记的行数凑一个。真实会话不能这么算
    // ——列表里只有已加载进当前视图的行，被钉住的老消息往往还没翻到，照这里数会少报；
    // 那条路走的是服务端 total（见 SetPinnedCount）。
    if (m_peerUserId == 0)
    {
        balloonchatwnd::ChatMessageList* mockList =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kIdMessageArea));
        if (mockList != NULL)
        {
            SetPinnedCount(mockList->CountPinned());
        }
    }

    // 轻量提示条：挂在客户区根容器<u>末尾</u>（基类按子顺序绘制，末尾即最上层），
    // 布局 Fixed(0) 使它不占 vbox 主轴空间 —— DuiToast 自己算"顶部居中"的落点。
    // 客户区根容器是 vbox（见 BuildChatWindowBody 的返回值说明），带布局提示的
    // AddChild 在 DuiVBox 上，故这里要向下转一次。
    balloonwjui::DuiVBox* rootBox = dynamic_cast<balloonwjui::DuiVBox*>(content);
    if (rootBox != NULL)
    {
        // 密聊图片的大图浮层：同样挂在根容器末尾、Fixed(0) 不占主轴空间，显示时自己铺满客户区。放在提示条之前加入，
        // 提示条画在它上面（docs/密聊方案.md 4.10）
        std::unique_ptr<balloonchatwnd::SecretImageOverlay> overlay(new balloonchatwnd::SecretImageOverlay());
        overlay->SetRestoreFocus(content->FindCtrlById(balloonchatwnd::kIdInputEdit));
        m_secretImageOverlay = overlay.get();
        rootBox->AddChild(std::move(overlay), balloonwjui::DuiLayout::Hint().Fixed(0));

        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        m_dropToast = toast.get();
        rootBox->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));
    }

    // 拖文件进窗口发送：把拖放目标注册到本窗口。
    //
    // 第一个参数是宿主对象，传本窗口自身（DuiFrameWindow 派生自 DuiHost）。
    // 收宿主而不是光收一个 HWND，是为了让接收器在注册之前先让出窗口上可能
    // 已被 balloonui 控件占用的拖放目标 —— 一个窗口只能注册一个，被占住时
    // 本窗口这份会静默失败，症状是拖任何文件进来都没有反应（见
    // ChatDropController.h 的文件头）。
    //
    // 第二个参数原先传的是输入框内部子窗口的句柄，用来摘掉那个子窗口自带的
    // 拖放目标（否则拖到输入框上的文件会被它当内嵌对象吃掉）。输入框换成
    // 无窗口控件之后没有子窗口了，它自带的拖放也已经在上面关掉，所以这里
    // 传空 —— 没有第二个目标需要摘。
    //
    // 第三个参数是拖放回调接收方，也是本窗口（实现了 IChatDropSink）。
    m_dropCtrl.Attach(this, NULL, this);

    LOG_INFO(_T("单聊窗客户区 UI 初始化完成。"));
}

void CBalloonChatWindow::RefreshLinkRendering()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->RefreshAllRows();
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

balloonwjui::DuiVBox* CBalloonChatWindow::InputBlockCtrl()
{
    // 发送框 = 输入框（richedit）所在的那个 vbox：工具栏 + 输入框 + 底部提示 / 发送行
    // 都在里面（见 ChatWindowLayout.cpp 的内嵌 XML）。拿它的父控件而不是自己按坐标
    // 硬算，布局改了也不会失准。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    balloonwjui::DuiControl* input = content->FindCtrlById(balloonchatwnd::kIdInputEdit);
    if (input == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonwjui::DuiVBox*>(input->GetParent());
}

bool CBalloonChatWindow::IsChatDropZone(const POINT& ptClient)
{
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block == NULL)
    {
        return false;
    }
    RECT rc = block->GetRect();
    return ::PtInRect(&rc, ptClient) != FALSE;
}

void CBalloonChatWindow::OnChatDropHintChanged(bool visible)
{
    // 提示落在发送框上：整块铺一层浅蓝底，并把左下角那行提示文字换成"松开发送给
    // XXX"。不再在消息区盖浮层 —— 拖文件的落点就是发送框。
    //
    // 用<u>底色</u>而不是描边：发送框的矩形左右两边正好贴着客户区边缘，描边是压在
    // 矩形边线上画的，贴边那一侧只剩半条、实测左边那条根本看不见（用户报过）。
    // 填充没有这个问题，四周一致，也更显眼。
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block != NULL)
    {
        //拖到发送框上时的底色：浅蓝，压得住白底又不刺眼。
        const COLORREF kDropZoneBg = RGB(0xE6, 0xF1, 0xFD);
        //CLR_INVALID = 不画底，恢复成原本的样子（发送框本来没有自己的底色）。
        block->SetBgColor(visible ? kDropZoneBg : CLR_INVALID);
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdHintLabel));
        if (hint != NULL)
        {
            if (visible)
            {
                // 文案里带上对方是谁 —— 同时开着几个聊天窗时，这一句是"别拖错窗口"
                // 的最后一道确认。
                CString text;
                if (m_peerName.IsEmpty())
                {
                    text = TR(L"松开发送文件");
                }
                else
                {
                    text.Format(TR(L"松开发送给 %s"), (LPCTSTR)m_peerName);
                }
                hint->SetText(text);
            }
            else
            {
                // 收起时把那行字还原成随发送模式联动的原文案。
                RefreshSendModeUi();
            }
        }
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonChatWindow::OnChatContentDropped(
    const std::vector<balloonchatwnd::ChatClipSegment>& segs)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 与粘贴走同一个插入口：文字原样、表情还原成表情、图片登记源文件后带标记插入。
    input->InsertClipSegments(segs);
    // 拖放的源头多半是另一个聊天窗：把本窗口提到前台、焦点交给输入框，松手后可以直接接着打字。
    ::SetForegroundWindow(m_hWnd);
    input->SetFocus();
}

void CBalloonChatWindow::OnChatFilesDropped(const std::vector<std::wstring>& paths,
                                            bool forceAsFile)
{
    // 调试入口（无对端绑定）没有服务器可发，直接给一句提示，免得用户以为发出去了。
    if (m_peerUserId == 0)
    {
        if (m_dropToast != NULL)
        {
            m_dropToast->Show(TR(L"调试窗口没有会话，无法发送文件"));
        }
        return;
    }

    // 归类：图片走图片消息、普通文件走文件消息、文件夹走文件夹传输；失效路径与空文件
    // 进跳过项。三条链路互不影响，混着拖进来时各发各的。
    const balloonchatwnd::DropPlan plan =
        balloonchatwnd::BuildDropPlanFromPaths(paths, forceAsFile);

    // 密聊状态：只接收图片（按住 Shift 也按图片处理），逐张以密聊图片发出；文件、文件夹不接收，提示原因
    //（docs/密聊方案.md 4.7）。只能查看时什么都不发
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        bool rejected = !plan.m_folders.empty();
        for (size_t i = 0; i < plan.m_items.size(); ++i)
        {
            const balloonchatwnd::DropSendItem& item = plan.m_items[i];
            if (item.m_kind == balloonchatwnd::kDropSendImage && m_secretState == balloonchatwnd::kSecretStateActive)
            {
                if (!SendSecretImageFile(item.m_path))
                {
                    break;
                }
            }
            else
            {
                rejected = true;
            }
        }
        if (rejected && m_dropToast != NULL)
        {
            m_dropToast->Show(balloonchatwnd::SecretOnlyTextAndImageTip().c_str());
        }
        return;
    }

    // 一次拖太多先问一句，避免误把整个目录拖进来把对方刷屏。这道确认只管文件，
    // 文件夹另有一道本机并发笔数闸门。
    if (plan.m_needConfirm)
    {
        CString ask;
        ask.Format(TR(L"确定要发送 %d 个文件吗？"), (int)plan.m_items.size());
        if (::MessageBoxW(m_hWnd, (LPCWSTR)ask, TR(L"发送文件"),
                          MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        {
            LOG_INFO("单聊窗拖放：用户取消了 %d 个文件的发送。", (int)plan.m_items.size());
            return;
        }
    }

    // 按拖入顺序逐个发出。两条链路内部都是"先乐观回显气泡、再交工作线程上传"，
    // 故这里连续调用不会卡住 UI 线程。
    for (std::vector<balloonchatwnd::DropSendItem>::size_type i = 0;
         i < plan.m_items.size(); ++i)
    {
        const balloonchatwnd::DropSendItem& item = plan.m_items[i];
        if (item.m_kind == balloonchatwnd::kDropSendImage)
        {
            SendImageFile(item.m_path);
        }
        else
        {
            SendFileData(item.m_path);
        }
    }

    // 文件夹逐个走文件夹传输链路，与工具栏「发送文件夹」按钮同一条路（本机并发笔数
    // 达到上限后剩下的不再发起，由该方法给出提示）。放在文件之后处理，是因为它可能
    // 弹出模态提示框，先把不需要用户过问的那些发出去。
    SendDroppedFolders(plan.m_folders);

    // 有发不出去的，汇总成一句话提示。逐条弹窗会很烦，故只报"几个 + 主要原因"。
    if (!plan.m_skipped.empty() && m_dropToast != NULL)
    {
        const std::wstring note = balloonchatwnd::DescribeDropSkipped(plan.m_skipped);
        m_dropToast->Show(note.c_str());
    }

    LOG_INFO("Single chat drop handled: files %d, folders %d, skipped %d, shift=%d.",
             (int)plan.m_items.size(), (int)plan.m_folders.size(),
             (int)plan.m_skipped.size(), forceAsFile ? 1 : 0);
}

// =======================================================================
// 消息处理
// =======================================================================

// =======================================================================
// =======================================================================
// 【历史问题存档】最小化本窗口后主面板曾被别的程序盖住（2026-08-12 已修）
//
// 曾经的现象：在主面板前面打开本窗口，再点标题栏最小化，露出来的不是主面板，而是
// 桌面上另一个不相干的程序（复现时是资源管理器），主面板被它盖住。
//
// 原因不在本文件，而在主面板建窗时带的 WS_EX_TOOLWINDOW —— 工具窗口会被系统排除在
// "最小化某窗口后该激活谁"的候选之外，于是激活权跳过主面板，落到 Z 序里下一个普通
// 窗口，那个窗口浮上来就盖住了主面板。修法是把主面板改成普通窗口、改用
// ITaskbarList::DeleteTab 摘任务栏按钮，见 BalloonMainPanel::CreateAndShow 的注释
// 与 `docs/主面板被其它窗口盖住问题排查.md`。
//
// 留这段注释是因为：<u>现象出在本窗口的最小化动作上，症结却在别处</u>。日志实测
// 主面板当时既没有被移动、也没有被隐藏，它一条 Z 序变动消息都没收到 —— 所以将来
// 若再遇到类似现象，不要在本文件里找"哪里把主面板压下去了"，那里什么都没有，
// 该查的是相关窗口的扩展样式与激活权归属。
// =======================================================================

LRESULT CBalloonChatWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    // 本处只是搭个便车，不消费 WM_ACTIVATE：置 FALSE 让消息继续沿消息映射
    // 交给 DuiFrameWindow 链（基类目前没有该消息的 handler，最终走 DefWindowProc）。
    bHandled = FALSE;

    // 只关心"被激活"，失活不做任何事。
    if (LOWORD(wParam) == WA_INACTIVE)
    {
        return 0;
    }
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return 0;
    }
    if (m_peerUserId == 0)
    {
        return 0;
    }

    // 用户切回这个聊天窗 = 他要看这个会话了：请主面板清未读并上报已读位点。
    // cid 用堆上的 long long 传递（Win32 下 LPARAM 装不下 64 位），主面板处理后 delete。
    long long* pCid = new long long((long long)m_peerUserId);
    LOG_INFO("单聊窗被激活，请求清除该会话未读: peer=%u.", m_peerUserId);
    ::PostMessage(m_hostPanel, FMG_MSG_CONV_MARK_READ, (WPARAM)1, (LPARAM)pCid);
    return 0;
}

LRESULT CBalloonChatWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 密聊状态下关窗即退出密聊、清除内容，先二次确认（docs/密聊方案.md 4.7）；用户取消时窗口保持打开
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        const std::wstring ask = balloonchatwnd::SecretCloseConfirmText();
        if (::MessageBox(m_hWnd, ask.c_str(), g_strAppTitle.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return 0;
        }
        ExitSecretMode(true, false);
    }
    // 调试入口（--chatwindow）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 请求进程退出。后续真实集成（从主面板打开聊天窗）时关闭
    // 行为另议。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    // 关窗前给对端补一包 state=0：人都把窗口关了，对面那句"正在输入…"不该还挂着。
    // 须在 DestroyWindow 之前发 —— 窗口一销毁就没机会了。
    StopTypingNotify();
    // 从图片解码缓存注销本窗口，避免就绪后向已销毁窗口 PostMessage。
    balloonchatwnd::ChatImageCache::Inst().UnregisterListener(m_hWnd);
    // 录音中关窗按取消处理、试听中的录音丢弃（语音消息第 3b、6g 步），之后语音消息中心不再给录音会话发通知
    m_voiceSession.OnWindowClosing();
    // 本窗口正在播放语音时一并停止，之后语音中心不再给本窗口发播放通知（第 3e 步）
    balloonvoice::CVoiceHub::GetInstance().DetachPlayListener(this);
    // 注销拖放目标。须在 DestroyWindow 之前 —— RevokeDragDrop 要求窗口还在。
    m_dropCtrl.Detach();
    // 「发送代码」窗口随本窗口关闭（还没发出的代码不保留）
    if (m_codeComposeWnd)
    {
        m_codeComposeWnd->Close();
    }
    // 本人从本窗口发出、还在等对方同意的密聊邀请：关窗视同撤回（docs/密聊方案.md 4.2）。对方发来的邀请不动，
    // 倒计时内重新打开窗口还能补出卡片
    for (std::map<unsigned long long, SecretInviteCardInfo>::const_iterator it = m_secretInviteCards.begin();
         it != m_secretInviteCards.end(); ++it)
    {
        if (it->second.m_iAmInviter)
        {
            balloonsecret::CSecretChatHost::GetInstance().Cancel(it->first);
            LOG_INFO("secret invite canceled by closing the window, session=%llu, peer=%u.", it->first, m_peerUserId);
        }
    }
    m_secretInviteCards.clear();
    // 桌面字幕由主面板持有，不随聊天窗关闭（docs/聊天字幕模式方案.md 第 1 节第 10 条），这里不动它
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatFocusArea(UINT /*uMsg*/, WPARAM wParam,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam == (WPARAM)CHAT_FOCUS_INPUT || wParam == (WPARAM)CHAT_FOCUS_MESSAGES)
    {
        m_focusArea = (int)wParam;
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnLButtonDownMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    // 只旁听：DuiFrameWindow / DuiHost 还要靠这条消息派发点击。
    bHandled = FALSE;

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    balloonwjui::DuiControl* msgArea = content->FindCtrlById(balloonchatwnd::kIdMessageArea);
    if (msgArea != NULL)
    {
        RECT rc = msgArea->GetRect();
        if (::PtInRect(&rc, pt))
        {
            m_focusArea = CHAT_FOCUS_MESSAGES;
            return 0;
        }
    }

    // 点发送框那一整块（工具栏、提示行、发送按钮等）同样算回到发送框。输入框本身
    // 也在这块矩形之内，点它会落到同一分支；此外输入框自己在被点击、得到焦点或
    // 键入可见字符时也会投 WM_CHAT_FOCUS_AREA 上报（见 ChatInputRichEdit），两条
    // 路径给出的结果一致。
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block != NULL)
    {
        RECT rcBlock = block->GetRect();
        if (::PtInRect(&rcBlock, pt))
        {
            m_focusArea = CHAT_FOCUS_INPUT;
        }
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatCopySelection(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 焦点在发送框时这一下不归消息区 —— 返回 0，输入框照常复制自己的选中文字。
    // 这正是"接收框里残留一段选中文字，就再也复制不出发送框内容"那个问题的修法。
    if (m_focusArea != CHAT_FOCUS_MESSAGES)
    {
        return 0;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return 0;
    }
    // 返回值要真实反映"有没有复制"：输入框的键盘钩子据它决定 Ctrl+C 是被本窗
    // 接走了、还是该让输入框自己复制。
    return list->CopySelectedTextToClipboard() ? 1 : 0;
}

LRESULT CBalloonChatWindow::OnKeyDownMsg(UINT uMsg, WPARAM wParam,
                                         LPARAM lParam, BOOL& bHandled)
{
    // 录音条显示时：Enter 等于「发送」、Esc 等于「取消」，确认放弃状态下 Esc 等于「返回」（语音消息第 3b、6g 步）。
    // 输入框此时隐藏但可能仍持有焦点，必须在这里截住，否则 Enter 会被输入框当成发送文字消息
    if (m_voiceSession.HandleKey((UINT)wParam))
    {
        return 0;
    }
    // 焦点不在输入框时（点过消息区空白处等）的 Ctrl+C 走这条路。焦点在输入框里时
    // 这条消息根本到不了本窗口，那条路由输入框的键盘钩子转成 WM_CHAT_COPY_SELECTION。
    if (wParam == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        BOOL dummy = TRUE;
        if (OnChatCopySelection(uMsg, 0, 0, dummy) != 0)
        {
            return 0;   // 已复制，消费掉
        }
    }
    bHandled = FALSE;   // 其余按键一律不拦，继续走 DuiFrameWindow 链
    (void)lParam;
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenLink(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 不立刻打开：先记下来、等一个系统双击间隔（理由见 kLinkOpenTimerId 的注释）。
    // 这期间若气泡报来双击，OnChatCancelLinkOpen 会把它作废。
    m_pendingLinkUrl = *pUrl;
    delete pUrl;
    if (IsWindow())
    {
        ::SetTimer(m_hWnd, kLinkOpenTimerId, ::GetDoubleClickTime(), NULL);
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatCancelLinkOpen(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 双击到了：刚才那一下单击排队的"打开链接"作废。
    if (IsWindow())
    {
        ::KillTimer(m_hWnd, kLinkOpenTimerId);
    }
    m_pendingLinkUrl.clear();
    return 0;
}

void CBalloonChatWindow::OpenPendingLink()
{
    if (m_pendingLinkUrl.empty())
    {
        return;
    }
    const std::wstring url = m_pendingLinkUrl;
    m_pendingLinkUrl.clear();

    // 屏幕共享的分享链接（第 25 期）：在客户端里凭链接加入，不交给浏览器
    if (balloonchatwnd::IsScreenShareLinkUrl(url))
    {
        LOG_INFO("single chat window: share link clicked.");
        balloonscreenshare::CScreenShareHub::GetInstance().JoinByLink(url);
        return;
    }
    // 加群链接（docs/邀请入群方案.md §12）：交给主面板查群并弹确认框，不交给浏览器
    std::string joinCode;
    if (balloonchatwnd::IsJoinGroupLinkUrl(url, &joinCode))
    {
        LOG_INFO("single chat window: join-group link clicked.");
        std::string* pCode = new std::string(joinCode);
        if (m_hostPanel == NULL || !::PostMessage(m_hostPanel, FMG_MSG_OPEN_JOIN_GROUP_LINK, 0, (LPARAM)pCode))
        {
            delete pCode;
        }
        return;
    }
    // 协议白名单：只放行 http / https。file: / javascript: 之类到这一步本来就
    // 不该出现（识别阶段已拒），这里再拦一道 —— 打开外部程序这件事只有这一处入口，
    // 判断也就只留在这一处。
    if (balloonchatwnd::IsSafeLinkUrl(url))
    {
        ::ShellExecuteW(m_hWnd, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("单聊窗拒绝打开非 http/https 链接。");
    }
}

LRESULT CBalloonChatWindow::OnViewImageMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& /*bHandled*/)
{
    // 气泡双击图片冒泡上来：lParam 为图片本地路径。打开进程内共享看图窗放大查看。
    LPCTSTR path = (LPCTSTR)lParam;
    if (path == NULL || path[0] == _T('\0'))
    {
        return 0;
    }
    // 密聊图片（内存伪路径）不交给看图窗：看图窗是独立窗口、按文件路径加载，还带另存与转发。改在本窗口内的
    // 大图浮层里显示（docs/密聊方案.md 4.10），字节从内存图源复制一份交给浮层解码后立即清零
    if (balloonchatwnd::ChatImageCache::IsMemoryPath(std::wstring(path)))
    {
        std::vector<unsigned char> bytes;
        if (m_secretImageOverlay != NULL
            && balloonchatwnd::ChatImageCache::Inst().CopyMemoryImage(std::wstring(path), bytes))
        {
            m_secretImageOverlay->Open(bytes);
            ::SecureZeroMemory(&bytes[0], bytes.size());
        }
        return 0;
    }
    CBalloonImageViewerWindow::OpenShared(m_hWnd, path);
    return 0;
}

LRESULT CBalloonChatWindow::OnImageReady(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 后台某张图片缩放完成：重绘消息区即可 —— 图片气泡会再画一次、这次查 ChatImageCache
    // 命中、换成真图。图片展示尺寸不变，无需重排，直接 Invalidate。
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}

// 引用条的配色。人名主题橙（与气泡里引用块的人名同色）、摘要灰 —— 一眼分清
// "回复谁"与"回复的是什么"。与群聊窗取值一致。
static const COLORREF kReplyNameColor   = RGB(0x3A, 0x86, 0xD9);   // 人名：蓝，与截图一致
static const COLORREF kReplyDigestColor = RGB(0x6B, 0x6B, 0x6B);   // 被引用内容：中灰

void CBalloonChatWindow::RefreshReplyBar()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* bar = content->FindCtrlById(balloonchatwnd::kIdReplyBar);
    if (bar == NULL)
    {
        return;
    }

    const bool show = m_pendingQuote.m_valid;
    if (show)
    {
        // 两行：人名一行、被引用内容一行。拆成两个 DuiLabel 是因为一个标签只能一种
        // 颜色，而人名与内容要区分深浅；分两行之后也不必再按文字宽度实测排布了。
        balloonwjui::DuiLabel* nameLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdReplyName));
        balloonwjui::DuiLabel* digestLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdReplyDigest));

        if (nameLabel != NULL)
        {
            CString name = Utf8ToCString(m_pendingQuote.m_name);
            name += _T(":");
            nameLabel->SetText(name);
            nameLabel->SetTextColor(kReplyNameColor);
            nameLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (digestLabel != NULL)
        {
            digestLabel->SetText(Utf8ToCString(m_pendingQuote.m_digest));
            digestLabel->SetTextColor(kReplyDigestColor);
            // 内容超长时省略号收尾，卡片高度固定，绝不把输入区撑变形。
            digestLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    if (bar->IsVisible() != show)
    {
        bar->SetVisible(show);
    }
    // 卡片占一块固定高度，显隐会改变输入区块的内部排布，必须重排；只 Invalidate
    // 的话输入框不会让出那块位置，卡片会盖在它上面。
    //
    // 注意不能只对根 ForceLayout：根的 Layout 会对各级容器调 SetRect，而 SetRect
    // 在矩形没变时提前返回 —— 输入区块本身的矩形并没有变（变的只是它内部的分配），
    // 于是刚变可见的卡片永远拿不到矩形，表现为引用卡片完全看不见。这是 balloonui
    // 已知的布局坑（群聊窗切边栏 Tab 处也有同样的注释），解法是直接对卡片所在的
    // 那个容器 ForceLayout，绕开 SetRect 的提前返回。
    balloonwjui::DuiControl* holder = bar->GetParent();
    if (holder != NULL)
    {
        holder->ForceLayout(holder->GetRect());
    }
    else
    {
        content->ForceLayout(content->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonChatWindow::CancelPendingReply()
{
    m_pendingQuote = net::ReplyQuote();
    RefreshReplyBar();
}

LRESULT CBalloonChatWindow::OnChatLocateQuoted(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    const long long msgid = *pMsgid;
    delete pMsgid;

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    // 原消息不在已加载范围内时 ScrollToMsgid 返回 false —— 本期到此为止，
    // 「自动回拉历史直到那一条出现」属于后续阶段。
    const bool located = list->ScrollToMsgid(msgid);
    LOG_INFO("单聊窗定位被引用消息: msgid=%lld, 命中=%d.", msgid, located ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatReplyRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    const long long msgid = *pMsgid;
    delete pMsgid;

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    balloonchatwnd::ChatMessage src;
    if (!list->GetMessageByMsgid(msgid, src))
    {
        return 0;   //该行已不在列表里（被删 / 尚未加载），安静忽略
    }

    //单聊里发送人非此即彼：自己发的取本端昵称，对方发的取对端昵称。
    //对端昵称由打开方传入（m_peerName），空则回退成"用户<id>"；本端取登录账号昵称。
    CString senderName;
    if (src.fromMe)
    {
        if (m_lpFMGClient != NULL)
        {
            senderName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
        }
    }
    else
    {
        senderName = m_peerName;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(TR(L"用户%u"), src.fromMe ? 0u : m_peerUserId);
    }
    // 被引用的若是图片，把原图的 fileid / md5 一并带上 —— 收方据此在引用块里画
    // 缩略图。ChatMessage 上这两项由收图时记下（见 imageFileId / imageMd5 的说明）。
    m_pendingQuote = balloonchatwnd::BuildQuoteFromMessage(
        src, senderName, src.imageFileId, src.imageMd5);
    RefreshReplyBar();
    LOG_INFO("单聊窗挂起引用回复: quote.msgid=%lld, valid=%d.",
             msgid, m_pendingQuote.m_valid ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatLoadOlder(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_peerUserId == 0)
    {
        return 0;   // 调试入口没有真实会话，无历史可翻
    }
    // 密聊状态下列表里只有密聊内容，不往上翻普通历史
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        return 0;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    if (!balloonchatwnd::ShouldLoadOlderHistory(m_paging, list->ScrollY(),
                                                list->ViewHeight(), list->ContentHeight()))
    {
        return 0;
    }

    m_paging.m_loading = true;
    CIUSocket::GetInstance().RequestSyncMessages(
        1, (int64_t)m_peerUserId, m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("单聊窗请求更早一页历史: peer=%u, before=%lld, limit=%d.",
             m_peerUserId, (long long)m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatRecallRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡右键「撤回」冒泡上来：lParam 为堆上 long long（服务器 msgid），处理后 delete。
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    long long msgid = *pMsgid;
    delete pMsgid;

    //调试入口（m_peerUserId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_peerUserId == 0)
    {
        return 0;
    }

    //单聊：ctype=1、cid=对端 userid。界面改动等服务端推回撤回通知后再做。
    CIUSocket::GetInstance().RecallMessage(kConvTypeSingle,
                                           (int64_t)m_peerUserId, msgid);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatPinRequest(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡右键「Pin / 取消 Pin」冒泡上来：wParam 为目标状态（1 钉住、0 取消），
    //lParam 为堆上 long long（服务器 msgid），处理后 delete。
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    long long msgid = *pMsgid;
    delete pMsgid;

    //调试入口（m_peerUserId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_peerUserId == 0)
    {
        return 0;
    }

    //单聊：ctype=1、cid=对端 userid。界面改动等服务端推回 Pin 通知后再做。
    CIUSocket::GetInstance().PinMessage(kConvTypeSingle, (int64_t)m_peerUserId,
                                        msgid, wParam != 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatDeleteRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡右键「删除」或多选「删除 N」冒泡上来：lParam 为堆上 std::vector<long long>
    //（服务器 msgid），处理后 delete。先取出内容再 delete，后面弹模态框期间不持有堆对象。
    std::vector<long long>* pMsgids = (std::vector<long long>*)lParam;
    if (pMsgids == NULL)
    {
        return 0;
    }
    const std::vector<long long> msgids(*pMsgids);
    delete pMsgids;
    if (msgids.empty())
    {
        return 0;
    }

    const int answer = ::MessageBox(m_hWnd,
        balloonchat::BuildDeleteMessagesConfirmText(msgids.size()),
        TR(L"删除消息"), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDOK)
    {
        return 0;
    }

    //调试入口（m_peerUserId==0，走 mock 数据）下没有服务端可问，直接在本窗口移除
    if (m_peerUserId == 0)
    {
        OnMessagesDeleted(msgids);
        return 0;
    }

    //cmd_delete_message 一次只收一个 msgid，多选时逐条发；气泡等删除推送到达后再移除
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        CIUSocket::GetInstance().DeleteMessage((int64_t)msgids[i]);
    }
    LOG_INFO("Chat window requests delete messages, peer=%u, count=%u.",
             m_peerUserId, (unsigned)msgids.size());
    return 0;
}

void CBalloonChatWindow::ShowToast(LPCTSTR text)
{
    if (m_dropToast != NULL && text != NULL)
    {
        m_dropToast->Show(text);
    }
}

LRESULT CBalloonChatWindow::OnChatOpenMergedRecord(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的 content 原文：投递成功则所有权交给主面板，否则在这里释放
    std::string* pContent = (std::string*)lParam;
    if (pContent == NULL)
    {
        return 0;
    }
    //主面板持有全部查看窗并负责把下载结果转给它们，查看窗只能由它打开
    if (!::IsWindow(m_hostPanel)
        || !::PostMessage(m_hostPanel, FMG_MSG_OPEN_MERGED_VIEWER, 0, (LPARAM)pContent))
    {
        LOG_INFO("chat window cannot open merged record viewer: no main panel.");
        delete pContent;
        return 0;
    }
    LOG_INFO("chat window requests merged record viewer, content length=%u.",
             (unsigned)pContent->size());
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenShare(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的共享 id，用完在这里释放
    long long* pShareId = (long long*)lParam;
    if (pShareId == NULL)
    {
        return 0;
    }
    const long long shareId = *pShareId;
    delete pShareId;
    //本人的共享在本机打开（文件就在本机上），别人的共享打开浏览窗口
    const bool own = (wParam == kChatOpenShareOwn);
    balloonfileshare::CFileShareHost& host = balloonfileshare::CFileShareHost::GetInstance();
    const bool ok = own ? host.OpenOwnShareLocally(m_hWnd, (int64_t)shareId)
                        : host.OpenShareFromCard(m_hWnd, (int64_t)shareId);
    LOG_INFO("chat window opens share from card, peer=%u, shareid=%lld, own=%d, ok=%d.", m_peerUserId, shareId,
             own ? 1 : 0, ok ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    if (m_peerUserId != 0)
    {
        balloonfileshare::CFileShareHost::GetInstance().UnwatchConversationShares(kConvTypeSingle, (int64_t)m_peerUserId);
    }
    //退出登录、进程退出时窗口不经 WM_CLOSE 直接销毁：结束密聊、撤销防截图、清除密聊内容（不再拉历史）
    ExitSecretMode(true, false);
    return 0;
}

void CBalloonChatWindow::SetShareBadgeCount(int count)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatIconButton* btn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kIdHdrShare))
        : NULL;
    if (btn == NULL)
    {
        return;
    }
    btn->SetBadgeCount(count);
}

int CBalloonChatWindow::GetShareBadgeCount()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatIconButton* btn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kIdHdrShare))
        : NULL;
    return (btn != NULL) ? btn->GetBadgeCount() : 0;
}

void CBalloonChatWindow::OpenConversationShares()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* btn = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kIdHdrShare) : NULL;
    if (btn == NULL || m_peerUserId == 0)
    {
        return;
    }
    // 取按钮屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕），做法同表情浮层
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };
    const bool ok = balloonfileshare::CFileShareHost::GetInstance().OpenConversationShares(
        m_hWnd, kConvTypeSingle, (int64_t)m_peerUserId, &anchor);
    LOG_INFO("chat window opens conversation shares, peer=%u, ok=%d.", m_peerUserId, ok ? 1 : 0);
}

void CBalloonChatWindow::ApplyShareButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* btn = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kIdHdrShare) : NULL;
    if (btn == NULL)
    {
        return;
    }
    //密聊状态下头部按钮一律隐藏（1013 推送会再调本函数，不能借此显示出来）
    const bool show = balloonfileshare::CFileShareHost::GetInstance().IsModuleReady()
                      && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, balloonchatwnd::kIdHdrShare);
    if (btn->IsVisible() == show)
    {
        return;
    }
    btn->SetVisible(show);
    balloonwjui::DuiControl* row = btn->GetParent();
    if (row != NULL)
    {
        row->ForceLayout(row->GetRect());
    }
}

void CBalloonChatWindow::AppendShareCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify,
                                         bool fromMe, const std::string& clientMsgId)
{
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage card = MakeRealTextMessage(
        fromMe, TR(L"[共享]"), net::TextFont(), notify.m_msgid, notify.m_timestamp, clientMsgId, net::ReplyQuote());
    //单聊里卡片不是本人发的就是对方发的；对方昵称还没取到时写「对方」
    const CString ownerName = m_peerName.IsEmpty() ? CString(TR(L"对方")) : m_peerName;
    balloonchatwnd::ApplyFileShareCard(card, notify.m_shareContent, ownerName,
                                       balloonchatwnd::MakeShareCardLiveState(notify.m_shareContent));
    list->AppendMessage(card);
    LOG_INFO("chat window: share card shown, from=%lld, msgid=%lld, shareid=%lld.",
             (long long)notify.m_from, (long long)notify.m_msgid, card.shareId);
}

int CBalloonChatWindow::OnShareCardStateChanged(long long shareId, int state, int reason)
{
    return balloonchatwnd::RefreshShareCardsInList(GetMessageList(), shareId, state, reason);
}

LRESULT CBalloonChatWindow::OnChatJoinScreenShare(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的加入请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::ScreenShareJoinRequest> req((balloonchatwnd::ScreenShareJoinRequest*)lParam);
    if (!req || m_peerUserId == 0)
    {
        return 0;
    }
    // 加入要走信令通道，离线时必然失败，在入口就拦住。
    if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return 0;
    }
    balloonscreenshare::CScreenShareHub::GetInstance().JoinShare(kConvTypeSingle, (int64_t)m_peerUserId,
                                                                 req->m_roomId, req->m_hostName, req->m_forced);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenCloudRecordCard(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                      BOOL& /*bHandled*/)
{
    //lParam 为堆上的播放请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::CloudRecordOpenRequest> req((balloonchatwnd::CloudRecordOpenRequest*)lParam);
    if (!req)
    {
        return 0;
    }
    const bool ok = balloonscreenshare::CScreenShareHub::GetInstance().OpenCloudRecord(
        m_hWnd, (int64_t)req->m_recordId, std::wstring((LPCWSTR)req->m_title));
    LOG_INFO("chat window: cloud record card play, peer=%u, record=%lld, ok=%d.", (unsigned)m_peerUserId,
             req->m_recordId, ok ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenCodeViewer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::CodeViewerOpenRequest* pReq = (balloonchatwnd::CodeViewerOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    // 单聊里代码片段不是本人发的就是对方发的
    const long long senderId = pReq->m_fromMe ? SelfUserIdForCode() : (long long)m_peerUserId;
    // 打开代码查看窗，已发出的代码片段可以在查看窗里追加批注（docs/代码片段方案.md 第 12 节）；
    // 聊天卡片右键「添加批注…」打开时展开批注栏
    ShowCodeViewer(pReq->m_code, pReq->m_lang, pReq->m_notes, pReq->m_key, pReq->m_msgid, senderId, pReq->m_fromMe,
                   pReq->m_action == balloonchatwnd::kCodeViewerActionAnnotate);
    delete pReq;
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenMarkdownPreview(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::MarkdownPreviewOpenRequest> req((balloonchatwnd::MarkdownPreviewOpenRequest*)lParam);
    if (!req)
    {
        return 0;
    }
    //单聊里卡片不是本人发的就是对方发的；气泡里没有发送者名字时用对方昵称
    std::wstring sender = req->m_fromMe ? std::wstring(TR(L"我")) : req->m_sender;
    if (sender.empty())
    {
        sender = std::wstring((LPCWSTR)m_peerName);
    }
    balloonmd::MarkdownPreviewDoc doc;
    doc.m_sourceUtf8 = req->m_sourceUtf8;
    doc.m_title = req->m_title;
    doc.m_meta = balloonmd::MarkdownPreviewMeta(sender, req->m_timestampMs);
    doc.m_key = req->m_key;
    balloonmd::CMarkdownEditorManager::GetInstance().OpenPreview(doc);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatJoinGroupInvite(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的加入请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::GroupInviteJoinRequest> req((balloonchatwnd::GroupInviteJoinRequest*)lParam);
    if (!req || req->m_inviteId <= 0 || m_peerUserId == 0)
    {
        return 0;
    }
    //加入要向服务器发请求，离线时在入口处拦截并提示
    if (!CheckOnlineFor(balloonmain::kActionAccountOp))
    {
        return 0;
    }
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetMessageByMsgid(req->m_msgid, msg) || msg.kind != balloonchatwnd::kMsgGroupInvite)
    {
        return 0;
    }
    //已经点过、正等应答：不重复发请求
    if (msg.inviteJoining)
    {
        return 0;
    }
    balloonchatwnd::SetGroupInviteJoining(msg, true);
    list->SetMessageByMsgid(req->m_msgid, msg);
    CIUSocket::GetInstance().SendAcceptGroupInvite((int64_t)req->m_inviteId);
    LOG_INFO("chat window: accept group invite requested, peer=%u, msgid=%lld, inviteid=%lld.",
             m_peerUserId, req->m_msgid, req->m_inviteId);
    return 0;
}

//本账号的语音缓存目录 Users\<账号>\ChatVoice\（语音消息第 3b 步，见 docs/语音消息方案.md 第 7.3 节）
static std::wstring CurrentUserVoiceDir()
{
    return net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(), L"ChatVoice");
}

void CBalloonChatWindow::StartVoiceRecording()
{
    //已经有录音条在显示时录音会话直接忽略；失败时由它提示原因（经 ShowVoiceRecordToast）
    m_voiceSession.Start(CurrentUserVoiceDir(), GenClientMsgId());
}

void CBalloonChatWindow::SendRecordedVoice(const std::string& clientMsgId, const balloonvoice::VoiceRecordOutput& out)
{
    LOG_INFO("chat window: voice recorded, clientmsgid %s, %lld ms, %lld bytes.", clientMsgId.c_str(),
             static_cast<long long>(out.m_durationMs), static_cast<long long>(out.m_fileBytes));

    //密聊中不发语音（录音是在进入密聊之前开始的）：语音要上传文件服务器、留下记录，与密聊的约定不符
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        ShowToast(balloonchatwnd::SecretBlockedSendTip().c_str());
        LOG_INFO("recorded voice blocked in secret mode, peer=%u.", m_peerUserId);
        return;
    }

    //录好的文件已在 ChatVoice\<md5>.ogg：先在列表末尾插一条发送中的语音气泡，再交给文件传输线程上传并发出
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeSendingVoiceMessage(
        clientMsgId, out.m_durationMs, out.m_wave, out.m_path, out.m_md5, out.m_fileBytes);
    // 头像沿用本端真实头像（字形 / 真图）
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);
    StartVoiceUpload(msg);
}

LRESULT CBalloonChatWindow::OnChatVoiceAction(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::VoiceActionRequest* req = reinterpret_cast<balloonchatwnd::VoiceActionRequest*>(lParam);
    if (req == NULL)
    {
        return 0;
    }
    if ((int)wParam == balloonchatwnd::kVoiceActionResend)
    {
        ResendVoice(req->m_clientMsgId);
    }
    else
    {
        //点了气泡：播放、停止、提示正在下载或重新下载
        HandleVoiceClick(req->m_msgid, req->m_clientMsgId);
    }
    delete req;
    return 0;
}

void CBalloonChatWindow::ResendVoice(const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !balloonchatwnd::FindVoiceByClientMsgId(*list, clientMsgId, msg))
    {
        return;
    }
    if (!balloonchatwnd::CanResendVoice(msg))
    {
        //发送失败的本人语音仍不能重发，只剩一种情况：录音文件已被「清理缓存」删掉
        if (msg.fromMe && msg.sendFailed)
        {
            ShowToast(balloonchatwnd::VoiceFileMissingHint().c_str());
            LOG_INFO("chat window: voice resend refused, file missing, clientmsgid=%s.", clientMsgId.c_str());
        }
        return;
    }
    //气泡位置不变、回到发送中并清掉失败原因，再用原 clientmsgid 重新上传（文件已在服务端时只重发 1503）
    if (!list->RestartVoiceSendByClientMsgId(clientMsgId))
    {
        return;
    }
    LOG_INFO("chat window: voice resend, clientmsgid=%s.", clientMsgId.c_str());
    StartVoiceUpload(msg);
}

void CBalloonChatWindow::StartVoiceUpload(const balloonchatwnd::ChatMessage& msg)
{
    //调试入口没有会话绑定，不真正发送
    if (m_peerUserId == 0)
    {
        LOG_INFO("chat window: voice upload skipped, no conversation bound, clientmsgid=%s.", msg.clientMsgId.c_str());
        return;
    }
    net::FileTransfer::GetInstance().EnqueueSendVoice(kConvTypeSingle, (int64_t)m_peerUserId, msg.clientMsgId,
                                                      std::wstring((LPCWSTR)msg.voiceLocalPath), msg.voiceDuration,
                                                      net::EncodeVoiceWave(msg.voiceWave));
    //登记待冒泡：发送回执成功后才把本会话冒泡到会话列表顶部、预览写「[语音]」，被服务端拒绝的消息不冒泡
    ConvSendBumpData bump;
    bump.ctype   = kConvTypeSingle;
    bump.cid     = (long long)m_peerUserId;
    bump.msgType = kMsgTypeVoice;
    bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeVoice, std::string());
    m_sendBumps.Add(msg.clientMsgId, bump);
}

void CBalloonChatWindow::HandleVoiceClick(long long msgid, const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage msg;
    bool found = (msgid > 0 && list->GetMessageByMsgid(msgid, msg) && msg.kind == balloonchatwnd::kMsgVoice);
    if (!found)
    {
        found = balloonchatwnd::FindVoiceByClientMsgId(*list, clientMsgId, msg);
    }
    if (!found)
    {
        return;
    }
    switch (balloonchatwnd::DecideVoiceClick(msg))
    {
    //就绪的语音（或本人发送中、发送失败的）：开始播放
    case balloonchatwnd::kVoiceClickPlay:
        StartVoicePlayback(msg);
        break;
    //正在播放：停止；语音中心通知本窗口播放结束，气泡在 OnVoicePlayEnded 里退出播放中
    case balloonchatwnd::kVoiceClickStop:
        balloonvoice::CVoiceHub::GetInstance().StopPlay();
        break;
    //文件还在下载：提示稍候
    case balloonchatwnd::kVoiceClickWaitDownload:
        ShowToast(balloonchatwnd::VoiceDownloadingHint().c_str());
        break;
    //文件不在本机（上次下载失败等）：重新下载，结果按 md5 经主面板广播回来（OnVoiceDownloadResult）
    case balloonchatwnd::kVoiceClickRetryDownload:
    {
        const std::wstring voiceDir = CurrentUserVoiceDir();
        if (msg.voiceFileId.empty() || msg.voiceMd5.empty() || voiceDir.empty())
        {
            ShowToast(balloonchatwnd::VoicePlayFailedHint().c_str());
            break;
        }
        list->MarkVoiceDownloading(msg.voiceMd5);
        net::FileTransfer::GetInstance().EnqueueVoiceDownload(msg.voiceFileId, msg.voiceMd5, msg.voiceSize,
                                                              balloonvoice::VoiceCachePath(voiceDir, msg.voiceMd5));
        LOG_INFO("chat window: voice download retried, md5=%s.", msg.voiceMd5.c_str());
        break;
    }
    //其它取值不会出现
    default:
        break;
    }
}

void CBalloonChatWindow::StartVoicePlayback(const balloonchatwnd::ChatMessage& msg)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    balloonvoice::VoicePlayKey key;
    key.m_msgid = msg.msgid;
    key.m_clientMsgId = msg.clientMsgId;
    //正在播别的语音（本窗口或别的窗口）时，语音中心先通知原来的窗口播放结束，再开播这一条
    if (!balloonvoice::CVoiceHub::GetInstance().StartPlay(this, key, std::wstring((LPCWSTR)msg.voiceLocalPath)))
    {
        ShowToast(balloonchatwnd::VoicePlayFailedHint().c_str());
        LOG_WARNING("chat window: voice play failed, msgid=%lld.", msg.msgid);
        return;
    }
    list->SetVoicePlaying(msg.msgid, msg.clientMsgId, true);
    //对方发来、还没听过：请主面板在本地库标为已听（红点只记在本机，方案 V7）
    if (!msg.fromMe && !msg.voicePlayed && msg.msgid > 0 && ::IsWindow(m_hostPanel))
    {
        balloonchatwnd::VoicePlayedMark* mark = new balloonchatwnd::VoicePlayedMark();
        mark->m_ctype = kConvTypeSingle;
        mark->m_cid = (int64_t)m_peerUserId;
        mark->m_msgid = msg.msgid;
        if (!::PostMessage(m_hostPanel, FMG_MSG_VOICE_PLAYED, 0, (LPARAM)mark))
        {
            delete mark;
        }
    }
}

void CBalloonChatWindow::OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs, int64_t totalMs)
{
    (void)totalMs;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list != NULL)
    {
        list->SetVoicePlayFrame(key.m_msgid, key.m_clientMsgId, balloonchatwnd::VoicePlayFrame(playedMs));
    }
}

void CBalloonChatWindow::OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    list->SetVoicePlaying(key.m_msgid, key.m_clientMsgId, false);
    //播到结尾：紧挨着的下一条是对方发来、未听、已下载好的语音时接着播（方案第 3.2 节）；被停止的不接着播
    if (reason != balloonvoice::voice_play_end_finished)
    {
        return;
    }
    balloonchatwnd::ChatMessage next;
    if (list->NextAutoPlayVoice(key.m_msgid, key.m_clientMsgId, next))
    {
        LOG_INFO("chat window: voice auto-play next, msgid=%lld.", next.msgid);
        StartVoicePlayback(next);
    }
}

void CBalloonChatWindow::ShowVoiceRecordToast(const std::wstring& text)
{
    ShowToast(text.c_str());
}

void CBalloonChatWindow::ShowVoiceRecordBarUi(bool show)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* input = content->FindCtrlById(balloonchatwnd::kIdInputEdit);
    balloonchatwnd::ShowVoiceRecordBar(input, content->FindCtrlById(balloonchatwnd::kIdInputBottomRow),
                                       content->FindCtrlById(balloonchatwnd::kIdVoiceBar), show);
    //录音时把焦点从（已隐藏的）输入框移开；收起后还给输入框
    SetDuiFocus(show ? NULL : input);
    InvalidateRect(NULL, FALSE);
}

void CBalloonChatWindow::ApplyVoiceButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //「语音消息」的录音与播放都由 FlamingoMedia.dll 提供：模块没有加载时隐藏
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbVoice);
    const bool show = balloonvoice::CVoiceHub::GetInstance().IsModuleReady()
                      && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, balloonchatwnd::kIdTbVoice);
    if (btn == NULL || btn->IsVisible() == show)
    {
        return;
    }
    btn->SetVisible(show);
    //显隐变化后对按钮所在的那一行重排（开发约定文档「UI 约定」第 5 条）
    balloonwjui::DuiControl* row = btn->GetParent();
    if (row != NULL)
    {
        row->ForceLayout(row->GetRect());
    }
}

void CBalloonChatWindow::ApplyScreenShareButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //头部「共享屏幕」与工具栏「屏幕录制」都由 FlamingoMedia.dll 提供：模块没有加载（录屏另要求系统支持）时隐藏
    const balloonscreenshare::CScreenShareHub& hub = balloonscreenshare::CScreenShareHub::GetInstance();
    const UINT ids[] = { balloonchatwnd::kIdHdrScreenShare, balloonchatwnd::kIdTbRecord };
    //密聊状态下两个按钮都隐藏
    const bool shows[] = {
        hub.IsModuleReady() && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, ids[0]),
        hub.IsRecordSupported() && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, ids[1]) };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        balloonwjui::DuiControl* btn = content->FindCtrlById(ids[i]);
        if (btn == NULL || btn->IsVisible() == shows[i])
        {
            continue;
        }
        btn->SetVisible(shows[i]);
        //显隐变化后对按钮所在的那一行重排（开发约定文档「UI 约定」第 5 条）
        balloonwjui::DuiControl* row = btn->GetParent();
        if (row != NULL)
        {
            row->ForceLayout(row->GetRect());
        }
    }
}

void CBalloonChatWindow::AppendScreenShareCard(balloonchatwnd::ChatMessageList* list,
                                               const net::MessageNotify& notify, bool fromMe,
                                               const std::string& clientMsgId)
{
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage card = MakeRealTextMessage(
        fromMe, TR(L"[屏幕共享]"), net::TextFont(), notify.m_msgid, notify.m_timestamp, clientMsgId,
        net::ReplyQuote());
    const std::string roomId = balloonchatwnd::ScreenShareRoomIdOf(notify.m_screenShareContent);
    const int state = balloonscreenshare::CScreenShareHub::GetInstance().RoomState(
        kConvTypeSingle, (int64_t)m_peerUserId, roomId);
    //单聊里卡片不是本人发起的就是对方发起的；对方昵称还没取到时用卡片里的发起人名
    balloonchatwnd::ApplyScreenShareCard(card, notify.m_screenShareContent, fromMe ? CString() : m_peerName, state);
    list->AppendMessage(card);
    LOG_INFO("chat window: screen share card shown, from=%lld, msgid=%lld, room=%s, state=%d.",
             (long long)notify.m_from, (long long)notify.m_msgid, roomId.c_str(), state);
}

void CBalloonChatWindow::AppendCloudRecordCard(balloonchatwnd::ChatMessageList* list,
                                               const net::MessageNotify& notify, bool fromMe,
                                               const std::string& clientMsgId)
{
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage card = MakeRealTextMessage(
        fromMe, TR(L"[云端录像]"), net::TextFont(), notify.m_msgid, notify.m_timestamp, clientMsgId,
        net::ReplyQuote());
    //单聊里卡片不是本人发的就是对方发的；副标题写对方的名字
    balloonchatwnd::ApplyCloudRecordCard(card, notify.m_cloudRecordContent, fromMe ? CString() : m_peerName);
    list->AppendMessage(card);
    LOG_INFO("chat window: cloud record card shown, from=%lld, msgid=%lld, record=%lld.",
             (long long)notify.m_from, (long long)notify.m_msgid, card.infoCardRefId);
}

int CBalloonChatWindow::OnScreenShareStatusChanged()
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return 0;
    }
    const balloonscreenshare::CScreenShareHub& hub = balloonscreenshare::CScreenShareHub::GetInstance();
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgScreenShare);
    int refreshed = 0;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        if (!list->GetMessageByMsgid(msgids[i], msg))
        {
            continue;
        }
        const int state = hub.RoomState(kConvTypeSingle, (int64_t)m_peerUserId, msg.screenShareRoomId);
        if (state == msg.screenShareState)
        {
            continue;
        }
        balloonchatwnd::ApplyScreenShareState(msg, state);
        list->SetMessageByMsgid(msgids[i], msg);
        ++refreshed;
    }
    return refreshed;
}

bool CBalloonChatWindow::OnScreenShareInfoChanged(int64_t msgid, const std::string& contentJson)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || msgid <= 0)
    {
        return false;
    }
    balloonchatwnd::ChatMessage msg;
    if (!list->GetMessageByMsgid(msgid, msg))
    {
        return false;
    }
    if (!balloonchatwnd::ApplyScreenShareInfo(msg, contentJson))
    {
        return false;
    }
    list->SetMessageByMsgid(msgid, msg);
    LOG_INFO("chat window: screen share card info refreshed, peer=%u, msgid=%lld.", m_peerUserId, (long long)msgid);
    return true;
}

void CBalloonChatWindow::AppendGroupInviteCard(balloonchatwnd::ChatMessageList* list,
                                               const net::MessageNotify& notify, bool fromMe,
                                               const std::string& clientMsgId)
{
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage card = MakeRealTextMessage(
        fromMe, TR(L"[入群邀请]"), net::TextFont(), notify.m_msgid, notify.m_timestamp, clientMsgId,
        net::ReplyQuote());
    balloonchatwnd::ApplyGroupInviteCard(card, notify.m_groupInviteContent);
    list->AppendMessage(card);
    LOG_INFO("chat window: group invite card shown, from=%lld, msgid=%lld, inviteid=%lld, state=%d.",
             (long long)notify.m_from, (long long)notify.m_msgid, card.inviteId, card.inviteState);
}

void CBalloonChatWindow::AppendCallCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify,
                                        bool fromMe, const std::string& clientMsgId)
{
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage card = MakeRealTextMessage(
        fromMe, TR(L"[通话]"), net::TextFont(), notify.m_msgid, notify.m_timestamp, clientMsgId, net::ReplyQuote());
    balloonchatwnd::ApplyCallCard(card, notify.m_groupCallContent, m_peerName, m_inCallMedia);
    list->AppendMessage(card);
    LOG_INFO("chat window: call card shown, peer=%u, msgid=%lld, fromMe=%d.", m_peerUserId, (long long)notify.m_msgid,
             fromMe ? 1 : 0);
}

bool CBalloonChatWindow::IsShowingAnyMessage(const std::vector<int64_t>& msgids)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        if (msgids[i] > 0 && list->GetMessageByMsgid((long long)msgids[i], msg))
        {
            return true;
        }
    }
    return false;
}

bool CBalloonChatWindow::OnCallCardChanged(int64_t msgid, const std::string& contentJson)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || msgid <= 0)
    {
        return false;
    }
    balloonchatwnd::ChatMessage msg;
    if (!list->GetMessageByMsgid(msgid, msg) || !msg.screenShareIsCallCard)
    {
        return false;
    }
    balloonchatwnd::ApplyCallCard(msg, contentJson, m_peerName, m_inCallMedia);
    list->SetMessageByMsgid(msgid, msg);
    LOG_INFO("chat window: call card refreshed, peer=%u, msgid=%lld.", m_peerUserId, (long long)msgid);
    return true;
}

void CBalloonChatWindow::SetInCallMedia(int media)
{
    m_inCallMedia = media;
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return;
    }
    //只重填还没结束的通话卡片（「正在呼叫」「通话中」）；已结束的卡片状态取自 content，不受影响
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgScreenShare);
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        if (!list->GetMessageByMsgid(msgids[i], msg) || !msg.screenShareIsCallCard
            || balloonchatwnd::IsCallCardEnded(msg.screenShareContent))
        {
            continue;
        }
        balloonchatwnd::ApplyCallCard(msg, msg.screenShareContent, m_peerName, media);
        list->SetMessageByMsgid(msgids[i], msg);
    }
}

bool CBalloonChatWindow::OnGroupInviteChanged(int64_t msgid, int state)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || msgid <= 0)
    {
        return false;
    }
    balloonchatwnd::ChatMessage msg;
    if (!list->GetMessageByMsgid(msgid, msg) || msg.kind != balloonchatwnd::kMsgGroupInvite)
    {
        return false;
    }
    balloonchatwnd::ApplyGroupInviteState(msg, state);
    list->SetMessageByMsgid(msgid, msg);
    LOG_INFO("chat window: group invite card state refreshed, peer=%u, msgid=%lld, state=%d.",
             m_peerUserId, (long long)msgid, state);
    return true;
}

int64_t CBalloonChatWindow::OnGroupInviteAcceptFailed(int64_t inviteId, int state)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || inviteId <= 0)
    {
        return 0;
    }
    //同一个邀请只有一张卡片；按邀请 id 在本窗口已加载的入群邀请卡片里查找
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgGroupInvite);
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        if (!list->GetMessageByMsgid(msgids[i], msg) || msg.inviteId != inviteId)
        {
            continue;
        }
        if (state != balloonchatwnd::kGroupInviteStateUnchanged)
        {
            balloonchatwnd::ApplyGroupInviteState(msg, state);
        }
        else
        {
            balloonchatwnd::SetGroupInviteJoining(msg, false);
        }
        list->SetMessageByMsgid(msgids[i], msg);
        LOG_INFO("chat window: group invite accept failed, card restored, peer=%u, msgid=%lld, state=%d.",
                 m_peerUserId, (long long)msgids[i], state);
        return (int64_t)msgids[i];
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatForwardRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    //多选操作条「逐条转发」「合并转发」冒泡上来：lParam 为堆上 ChatForwardRequest，处理后 delete
    std::unique_ptr<balloonchatwnd::ChatForwardRequest> pSel(
        (balloonchatwnd::ChatForwardRequest*)lParam);
    if (!pSel)
    {
        return 0;
    }
    if (pSel->m_msgids.empty() && pSel->m_reuploadFiles.empty())
    {
        //右键「转发」的是一条本机已没有文件的在线直传文件（2026-10-05 起）：服务器上没有副本，转发不了
        if (pSel->m_skippedDirect > 0)
        {
            LOG_INFO("Chat window forward refused: direct file without local copy, peer=%u.", m_peerUserId);
            ShowToast(TR(L"在线直传的文件无法转发"));
        }
        return 0;
    }

    //调试入口（m_peerUserId==0，走 mock 数据）没有主面板，也没有服务端可问
    if (m_peerUserId == 0 || !::IsWindow(m_hostPanel))
    {
        LOG_INFO("Chat window forward ignored in debug entry, merge=%d, count=%u.",
                 pSel->m_merge ? 1 : 0, (unsigned)pSel->m_msgids.size());
        return 0;
    }

    balloonmain::ForwardRequest* pReq = new balloonmain::ForwardRequest();
    pReq->m_srcCtype = kConvTypeSingle;
    pReq->m_srcCid   = (int64_t)m_peerUserId;
    pReq->m_srcWnd   = m_hWnd;
    pReq->m_merge    = pSel->m_merge;
    pReq->m_msgids.assign(pSel->m_msgids.begin(), pSel->m_msgids.end());
    pReq->m_skipped  = pSel->m_skipped;
    //在线直传的文件（2026-10-05 起）：逐条转发时要重新发送的本机文件、无法转发的条数，合并转发时只留说明文字的条数
    pReq->m_reuploadFiles      = pSel->m_reuploadFiles;
    pReq->m_skippedDirect      = pSel->m_skippedDirect;
    pReq->m_directPlaceholders = pSel->m_directPlaceholders;
    //合并转发的标题由主面板生成：本窗口的 m_peerName 是「真名（备注）」，备注是本人私有的
    //称呼，不能写进要发给别人的标题，而真名只有主面板的通讯录缓存里有

    //主面板持有会话、联系人与群的全部数据，目标选择窗只能由它打开
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_FORWARD_PICKER, 0, (LPARAM)pReq))
    {
        delete pReq;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
        return 0;
    }
    LOG_INFO("Chat window requests forward, peer=%u, merge=%d, count=%u, skipped=%u, reupload=%u, "
             "skippeddirect=%u, directplaceholders=%u.",
             m_peerUserId, pSel->m_merge ? 1 : 0, (unsigned)pSel->m_msgids.size(),
             (unsigned)pSel->m_skipped, (unsigned)pSel->m_reuploadFiles.size(),
             (unsigned)pSel->m_skippedDirect, (unsigned)pSel->m_directPlaceholders);
    return 0;
}

void CBalloonChatWindow::OnMessagePinned(long long msgid, bool pinned)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有这条（该消息尚未加载进当前视图）时安静忽略：本地库那份标记已经落好，
    //下次翻历史渲染出来时自然带着。
    list->SetPinnedByMsgid(msgid, pinned);
}

void CBalloonChatWindow::SetPinnedCount(int count)
{
    m_pinnedCount = (count > 0) ? count : 0;
    RefreshPinBanner();
}

void CBalloonChatWindow::AdjustPinnedCount(int delta)
{
    m_pinnedCount += delta;
    if (m_pinnedCount < 0)
    {
        m_pinnedCount = 0;
    }
    RefreshPinBanner();
}

void CBalloonChatWindow::RefreshPinBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等 InitUi 之后主面板会再校准一次
    }

    //密聊状态下列表里只有密聊内容，「已 Pin N 条」说的是普通历史，按 0 条收起
    const int shownCount = balloonchatwnd::IsSecretState(m_secretState) ? 0 : m_pinnedCount;
    const bool visibilityChanged =
        balloonchatwnd::UpdatePinBanner(content, balloonchatwnd::kIdPinBanner, shownCount);

    //提示带显隐改变了消息区上方的占位高度，处理与离线提示带完全一样：先整树重排让它
    //拿到高度，再走一次本窗自己的 relayout 把输入区块高度钉回用户选定值。
    if (visibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
}

void CBalloonChatWindow::RequestOpenHistory(int initialTypeFilter)
{
    if (m_peerUserId == 0 || m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;   //调试入口下没有主面板，安静忽略
    }

    //对端 userid 经堆 int64_t 随消息回投，由主面板处理后 delete。会话类型与"预选哪个
    //类型胶囊"一起塞在 wParam 里（约定见 UserSessionData.h 的 FMG_MSG_OPEN_HISTORY）。
    int64_t* pCid = new int64_t((int64_t)m_peerUserId);
    const WPARAM wp = (WPARAM)(kConvTypeSingle | (initialTypeFilter << 8));
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_HISTORY, wp, (LPARAM)pCid))
    {
        delete pCid;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
    }
}

void CBalloonChatWindow::OnSendChatAckBackfill(const std::string& clientMsgId, long long msgid,
                                               long long timestampMs)
{
    //发送成功：此时才把会话冒泡到顶部（窗口抖动没有气泡行，同样在这里冒泡）
    ConvSendBumpData bump;
    if (m_sendBumps.TakeOnSuccess(clientMsgId, bump))
    {
        PostSendBump(bump);
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //本窗口没有该 clientMsgId（是别的会话发出的）时安静忽略。
    list->BackfillSentMessage(clientMsgId, msgid, timestampMs);

    //首屏渲染前发出的 Markdown 文档已入库：条件满足时重新拉最新一页，把被首屏冲掉的卡片显示出来
    if (m_mdEarlySends.OnAck(clientMsgId, true))
    {
        ReloadIfMarkdownEarlySendsDone();
    }
}

void CBalloonChatWindow::ReloadIfMarkdownEarlySendsDone()
{
    if (!m_mdEarlySends.TakeReloadNeeded())
    {
        return;
    }
    LOG_INFO("chat window: reload latest history to show markdown documents sent before the first page, peer=%u.",
             m_peerUserId);
    ReloadLatestMessages();
}

bool CBalloonChatWindow::OnSendChatAckFailed(const std::string& clientMsgId, int code)
{
    //被拒绝的消息不冒泡
    m_sendBumps.Drop(clientMsgId);

    //首屏渲染前发出、被拒绝的 Markdown 文档没有入库，不再等它；其余已成功的可能因此满足重新拉取的条件
    if (m_mdEarlySends.OnAck(clientMsgId, false))
    {
        ReloadIfMarkdownEarlySendsDone();
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return false;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return false;
    }
    //本窗口没有该 clientMsgId（是别的会话发出的）时返回 false，主面板接着问别的窗口
    const bool marked = list->MarkSendFailed(clientMsgId,
                                             CString(balloonchatwnd::SendFailureText(code).c_str()));
    if (marked)
    {
        LOG_INFO("chat window: send rejected, marked bubble failed, to=%u, code=%d, clientmsgid=%s.",
                 m_peerUserId, code, clientMsgId.c_str());
    }
    return marked;
}

LRESULT CBalloonChatWindow::OnChatReeditRequest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                BOOL& /*bHandled*/)
{
    balloonchatwnd::ChatReeditData* data = reinterpret_cast<balloonchatwnd::ChatReeditData*>(lParam);
    if (data == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatInputRichEdit* input = (content == NULL) ? NULL
        : dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    // 代码片段：打开发送代码窗口并预填原文与语言，不放回输入框（docs/代码片段方案.md 第 3.1 节）
    if (data->m_isCode)
    {
        // 追加批注的消息（批注里有标「新」的、且引用了原消息）：打开原消息的代码查看窗并放回批注（docs/代码片段方案.md
        // 第 12 节）。原消息还在列表里时以它的批注为底，否则以这条消息里没有标「新」的批注代替
        if (data->m_codeQuoteMsgid > 0 && codesnippet::CountNewCodeNotes(data->m_codeNotes) > 0)
        {
            balloonchatwnd::ChatMessageList* codeList = (content == NULL) ? NULL
                : dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea));
            balloonchatwnd::ChatMessage base;
            if (codeList != NULL && codeList->GetMessageByMsgid(data->m_codeQuoteMsgid, base) && base.isCode)
            {
                const long long baseSenderId = base.fromMe ? SelfUserIdForCode() : (long long)m_peerUserId;
                ShowCodeViewer(data->m_code, data->m_codeLang, base.codeNotes, std::to_string(data->m_codeQuoteMsgid),
                               data->m_codeQuoteMsgid, baseSenderId, base.fromMe, true, data->m_codeNotes);
            }
            else
            {
                ShowCodeViewer(data->m_code, data->m_codeLang, balloonchatwnd::BaselineNotesForReedit(data->m_codeNotes),
                               std::to_string(data->m_codeQuoteMsgid), data->m_codeQuoteMsgid, data->m_codeQuoteFrom,
                               data->m_codeQuoteFrom == SelfUserIdForCode(), true, data->m_codeNotes);
            }
            LOG_INFO("reedit: annotated code snippet goes back to the code viewer, base msgid=%lld, notes=%u.",
                     data->m_codeQuoteMsgid, (unsigned)data->m_codeNotes.size());
            delete data;
            return 0;
        }
        OpenCodeCompose(data->m_code, codesnippet::CodeLanguageId(data->m_codeLang), data->m_codeNotes);
        LOG_INFO("chat window: reedit code snippet, to=%u, chars=%u, notes=%u.", m_peerUserId,
                 (unsigned)data->m_code.size(), (unsigned)data->m_codeNotes.size());
        delete data;
        return 0;
    }
    // Markdown：打开 Markdown 编辑器并预填源文本，发送对象预选本会话，不放回输入框（docs/Markdown消息方案.md 第 11.3 节）
    if (data->m_isMarkdown)
    {
        OpenMarkdownEditor(data->m_markdownSource);
        LOG_INFO("chat window: reedit markdown in editor, to=%u, bytes=%u.", m_peerUserId,
                 (unsigned)data->m_markdownSource.size());
        delete data;
        return 0;
    }
    if (input != NULL)
    {
        //单聊没有 @，不登记候选
        balloonchatwnd::ApplyReeditToInput(*data, input, NULL);
        input->SetFocus();
        LOG_INFO("chat window: reedit applied, to=%u, segments=%u.", m_peerUserId,
                 (unsigned)data->m_segments.size());
    }
    delete data;
    return 0;
}

LRESULT CBalloonChatWindow::OnChatAddToMemo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::ChatAddToMemoRequest* req = reinterpret_cast<balloonchatwnd::ChatAddToMemoRequest*>(lParam);
    if (req == NULL)
    {
        return 0;
    }
    balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    //备忘列表还没取到时与「备忘与提醒」窗口的「新建」一样不打开，避免保存时与服务端的数据对不上
    if (center.ListTimedOut())
    {
        LOG_WARNING("chat window: add to memo ignored, memo list not available, to=%u.", m_peerUserId);
        delete req;
        return 0;
    }
    //来源：单聊里不是本人发的就是对方发的
    balloonchatwnd::MemoPrefillSource src;
    src.m_fromMe = req->m_msg.fromMe;
    src.m_senderName = m_peerName;
    src.m_timestampMs = req->m_msg.timestampMs;
    const balloonchatwnd::MemoPrefill prefill = balloonchatwnd::BuildMemoPrefill(req->m_msg, src);

    const memo::LocalMinute now = memo::NowLocalMinute();
    net::MemoItem item = balloonmemo::DefaultMemoForDay(now / memo::kMinutesPerDay, now, net::kMemoCtypePersonal, 0);
    item.m_title = prefill.m_title;
    item.m_note = prefill.m_note;
    LOG_INFO("chat window: add to memo, to=%u, msgid=%lld, title bytes=%u, note bytes=%u.", m_peerUserId,
             req->m_msg.msgid, (unsigned)item.m_title.size(), (unsigned)item.m_note.size());
    delete req;
    center.OpenEditor(m_hWnd, item, balloonmemo::kMemoEditNew, std::vector<net::ContactItem>());
    return 0;
}

void CBalloonChatWindow::PostSendBump(const ConvSendBumpData& data)
{
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;
    }
    ConvSendBumpData* pBump = new ConvSendBumpData(data);
    if (!::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP, (WPARAM)m_peerUserId, (LPARAM)pBump))
    {
        delete pBump;   //投递失败（主面板已销毁）自行回收
    }
}

int CBalloonChatWindow::ApplyPeerReadPosition(long long readMsgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return 0;
    }

    //被标记的气泡各自失效了自己的矩形，宿主会随后重画，这里不必整窗重画。
    return list->ApplyPeerReadPosition(readMsgid);
}

std::string CBalloonChatWindow::OnMessageRecalled(long long msgid, const CString& tipText, bool recalledByMe,
                                                  long long nowMs)
{
    // 桌面字幕上的撤回提示由主面板直接交给字幕（字幕由主面板持有），这里只处理本窗口的消息列表
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return std::string();
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return std::string();
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略——用户下次翻历史时，
    //本地库里那条已被标记撤回、根本不会再出现。
    std::string draftJson;
    list->RecallMessageByMsgid(msgid, tipText, recalledByMe, nowMs, &draftJson);
    if (!draftJson.empty())
    {
        //提示行带上了「编辑」：定好到期定时器，5 分钟到点时让它自动消失
        RefreshRecallEditTimer();
    }
    return draftJson;
}

void CBalloonChatWindow::RefreshRecallEditTimer()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    const long long nowMs = balloonchatwnd::RecallEditNowMs();
    const long long next = list->ExpireRecallEdits(nowMs);
    balloonchatwnd::RescheduleRecallEditTimer(m_hWnd, kRecallEditTimerId, next, nowMs);
}

void CBalloonChatWindow::OnSendModeChanged()
{
    RefreshSendModeUi();
}

void CBalloonChatWindow::OnMessagesDeleted(const std::vector<long long>& msgids)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //没加载进当前视图的那些行安静忽略：服务端已把它们对本人过滤掉，往上翻页也不会再出现
    const int removed = list->RemoveMessagesByMsgid(msgids);
    LOG_INFO("Chat window removed deleted messages, peer=%u, requested=%u, removed=%d.",
             m_peerUserId, (unsigned)msgids.size(), removed);
}

//调用方：主面板（服务端删除了本会话的消息之后），以及本窗口的 ReloadIfMarkdownEarlySendsDone
//（首屏渲染前发出的 Markdown 文档被首屏清掉之后）
void CBalloonChatWindow::ReloadLatestMessages()
{
    //调试入口没有对端，也没有服务端可问
    if (m_peerUserId == 0)
    {
        return;
    }

    //翻页状态回到"首屏还没到位"：应答到达时 OnHistoryMessages 据 m_loading 为假按首屏
    //处理，先清空列表再按服务端快照重新渲染；翻页锚点清零，首屏到达前不允许往上翻
    m_paging = balloonchatwnd::HistoryPagingState();
    //在新快照渲染之前发出的 Markdown 文档同样会被清掉，重新开始记录
    m_mdEarlySends.OnReloadRequested();
    //新快照到达之前插入的居中提示同理，回到「首屏未渲染」重新记录
    m_earlyTips.OnReloadRequested();
    CIUSocket::GetInstance().RequestSyncMessages(
        1, (int64_t)m_peerUserId, 0, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("Chat window reloads latest history (after a server-side delete, or to show markdown documents sent before the first page), peer=%u.", m_peerUserId);
}

LRESULT CBalloonChatWindow::OnChatFileAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    // 文件气泡按钮点击冒泡上来：lParam 为堆上 ChatFileActionData，处理后 delete。
    balloonchatwnd::ChatFileActionData* d = (balloonchatwnd::ChatFileActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    //打开文件、打开所在文件夹、另存为与会话无关，与群聊窗共用同一份处理
    if (balloonchatwnd::HandleLocalFileAction(m_hWnd, *d))
    {
        delete d;
        return 0;
    }

    //在线直传的文件气泡：动作交给文件夹传输管理器（2026-10-04 起）
    if (HandleDirectFileAction(*d))
    {
        delete d;
        return 0;
    }

    switch (d->action)
    {
    case balloonchatwnd::kChatFileCancel:
        //取消传输：发送方按 clientMsgId 定位那笔上传，接收方按 msgid 定位那笔下载。
        //两者都只是置位取消标志——真正的收尾（气泡置「已取消」态）等 FileTransfer 的
        //结果回投到达时统一做，避免这里先改状态、worker 稍后又回投一个失败态覆盖掉。
        if (!d->clientMsgId.empty())
        {
            LOG_INFO("单聊窗取消发文件: clientmsgid=%s.", d->clientMsgId.c_str());
            net::FileTransfer::GetInstance().CancelSend(d->clientMsgId);
        }
        else if (d->msgid > 0)
        {
            LOG_INFO("单聊窗取消收文件: msgid=%lld.", (long long)d->msgid);
            net::FileTransfer::GetInstance().CancelDownload(d->msgid);
        }
        else
        {
            LOG_ERROR("单聊窗取消文件传输但两个定位键都为空: action=%d.", d->action);
        }
        break;

    case balloonchatwnd::kChatFileAccept:
        //用户同意接收：这时才真正发起下载（收到消息时只渲染气泡、不自动下载）。
        StartFileDownloadFromBubble(*d);
        break;

    case balloonchatwnd::kChatFileReject:
        //拒绝接收：把本端气泡置「已拒绝」，同时告知发送方（服务端落库后转推，
        //发送方不在线则等其下次拉历史时看到）。用户反悔可再点「重新下载」。
        LOG_INFO("单聊窗拒绝接收文件: msgid=%lld.", (long long)d->msgid);
        MarkFileRejected(d->msgid);
        ReportFileAck(d->msgid, kFileAckRejected);
        break;

    case balloonchatwnd::kChatFileRetry:
        //重试分两种：本端刚发出的那笔（有 clientMsgId + 原始本地文件）是重新发送，
        //其余（对方发来的、以及历史里自己发过的）都是重新从服务器下载。
        if (!d->clientMsgId.empty() && !d->localPath.IsEmpty())
        {
            RestartFileSendFromBubble(*d);
        }
        else
        {
            StartFileDownloadFromBubble(*d);
        }
        break;

    default:
        break;
    }

    delete d;
    return 0;
}

// =======================================================================
// 单聊文件夹传输（cmd_folder_* 1810~1817）
//
// 本窗口只做两件事：把用户在气泡上的点击转交 foldertransfer::FolderTransferManager，
// 以及把管理器 / 网络层回投上来的事件画到那条文件夹气泡上。会话生命周期（扫描、建通道、
// 收发字节、重试续传、保留期）一概不在这里，全在管理器手上。
//
// 定位一律靠<u>会话 id</u>：文件夹气泡是本地气泡，不进服务端消息表、没有 msgid。发起
// 之初用的是管理器给的本地占位 id，服务端分配了真会话 id 之后由 OnFolderBound 换掉。
// =======================================================================

bool CBalloonChatWindow::GetFolderMessage(const std::string& sessionId,
                                          balloonchatwnd::ChatMessage& out)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return false;
    }
    return list->GetFolderMessage(sessionId, out);
}

bool CBalloonChatWindow::SetFolderMessage(const std::string& sessionId,
                                          const balloonchatwnd::ChatMessage& msg)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return false;
    }
    return list->SetFolderMessage(sessionId, msg);
}

void CBalloonChatWindow::ApplyFolderFailure(const std::string& sessionId, int errCode)
{
    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(sessionId, msg))
    {
        //这笔传输不属于本会话（主面板是向所有已打开的聊天窗广播的），安静忽略。
        return;
    }

    //用户取消要与真正的传输失败分开：两者的气泡措辞不同（「已取消」而不是「传输失败」），
    //且取消之后不该再给「重试」。会话已过保留期同理，单独一档，按钮换成「重新发送」。
    if (errCode == kFolderErrCanceled)
    {
        msg.folderState = balloonchatwnd::kFolderUserCanceled;
    }
    else if (errCode == kFolderErrSessionExpired)
    {
        msg.folderState = balloonchatwnd::kFolderExpired;
    }
    else
    {
        msg.folderState = balloonchatwnd::kFolderFailed;
    }

    msg.folderErrCode = errCode;
    //「重试」可用与否由管理器手里的保留期计时说了算，界面不自己算 —— 那份计时是与
    //服务端 folderretrywindow 对齐的唯一一处。
    msg.folderWithinRetry =
        foldertransfer::FolderTransferManager::GetInstance().IsWithinRetryWindow(sessionId);

    SetFolderMessage(sessionId, msg);
}

// 「同时进行的文件夹传输已达上限」的提示文案。上限来自配置项（flamingo.ini 的
// [app] foldermaxconcurrent），故文案里的笔数一律向管理器现取，界面不写死 —— 改了配置
// 提示自然跟着变。发起、重新发送、重试三处共用本函数，三处措辞因而必然一致。
static CString FolderBusyTipText()
{
    CString tip;
    tip.Format(TR(L"同时进行的文件夹传输已达上限（最多 %d 笔），请等其中一笔结束后再试。"),
               foldertransfer::FolderTransferManager::GetInstance().GetMaxConcurrent());
    return tip;
}

// 「对方不在线，发不了文件夹」的提示文案。两处用到：发起之前按本端掌握的对端在线
// 状态先拦一道，以及服务端回 error_code_folder_peer_offline 时的兜底，两处措辞一致。
//
// 文件夹传输必须由双方各自连上 fileserver 建中转通道才能传字节，对方不在线时这条
// 通道根本建不起来，服务端也因此在收到邀请的那一刻就直接拒绝、连会话都不建。所以
// 它不是"发了但失败了"，而是"这会儿发不了"。
static const wchar_t* const kFolderPeerOfflineTip =
    TR_NOOP(L"对方当前不在线，暂时无法发送文件夹。等对方上线后再发送吧。");

// 文件夹接收的默认落地目录名。与聊天文件缓存的 ChatFile、图片缓存的 ChatImage 同级，
// 都挂在 Users\<登录账号>\ 之下（这条口径的唯一出处是 net/UserCachePath.h），因而一个
// 账号收到的文件夹不会落进另一个账号的目录里。
static const wchar_t* const kFolderReceiveSubDir = L"ChatFolder";

/**
 *  取本机「文件夹接收」的默认落地位置 <数据根目录>Users\<登录账号>\ChatFolder\，
 *  并按需逐级创建 Users、Users\<账号>、ChatFolder 三级目录。
 *
 *  气泡上点「接收」走的就是这条路：不打断用户去选位置，直接落到这里。根目录重名时
 *  由接收端状态机自动改名（xxx(2) / xxx(3)……，见 FolderRecvSession::PrepareRootDirectory），
 *  本函数只负责把<u>父目录</u>准备好。
 *
 *  @param out [出参] 目录完整路径，以反斜杠结尾；返回 false 时被清空
 *  @return 目录已就位返回 true；尚未登录（拼不出账号目录）或建目录失败返回 false
 */
static bool EnsureDefaultFolderReceiveDir(std::wstring& out)
{
    out.clear();

    //账号为空（尚未登录）时 UserCacheDir 返回空串，据此判定拼不出有效路径，
    //绝不退回到当前工作目录去写。
    const std::wstring account = net::CurrentUserAccount();
    const std::wstring targetDir =
        net::UserCacheDir(g_szDataRoot, account, kFolderReceiveSubDir);
    if (targetDir.empty())
    {
        LOG_ERROR("Folder recv: cannot build the default receive directory, account is empty.");
        return false;
    }

    //CreateDirectoryW 只建最后一级，故从 Users 往下逐级调用；中间某级已存在会返回失败，
    //忽略即可，成败以最后对目标目录的属性检查为准（与 net/FileTransfer 的下载落地同一套写法）。
    std::wstring usersRoot = g_szDataRoot;
    usersRoot += L"Users\\";
    ::CreateDirectoryW(usersRoot.c_str(), NULL);

    const std::wstring userDir = net::UserCacheDir(g_szDataRoot, account, NULL);
    ::CreateDirectoryW(userDir.c_str(), NULL);
    ::CreateDirectoryW(targetDir.c_str(), NULL);

    const DWORD attr = ::GetFileAttributesW(targetDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        LOG_ERROR("Folder recv: create default receive directory failed, win32 error %lu.",
                  ::GetLastError());
        return false;
    }

    out = targetDir;
    return true;
}

void CBalloonChatWindow::AcceptFolderInvite(const balloonchatwnd::ChatFolderActionData& data,
                                            const std::wstring& saveParentDir)
{
    const int verdict = foldertransfer::FolderTransferManager::GetInstance()
                            .AcceptInvite(data.sessionId, saveParentDir);
    if (verdict == foldertransfer::kFolderStartOk)
    {
        LOG_INFO("Folder recv: invite accepted, session=%s.", data.sessionId.c_str());
        return;
    }

    LOG_ERROR("Folder recv: accept refused by manager, session=%s, verdict=%d.",
              data.sessionId.c_str(), verdict);
    ::MessageBox(m_hWnd, TR(L"这次文件夹传输已经结束，无法再接收。"),
                 g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    //会话已不存在：把气泡收到「已取消」一档，免得「接收 / 另存为 / 拒绝」三个按钮一直挂着。
    ApplyFolderFailure(data.sessionId, kFolderErrCanceled);
}

LRESULT CBalloonChatWindow::OnChatFolderAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& /*bHandled*/)
{
    // 文件夹气泡按钮点击冒泡上来：lParam 为堆上 ChatFolderActionData，处理后 delete。
    balloonchatwnd::ChatFolderActionData* d = (balloonchatwnd::ChatFolderActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    foldertransfer::FolderTransferManager& mgr =
        foldertransfer::FolderTransferManager::GetInstance();

    switch (d->action)
    {
    //接收：不弹选目录框，直接落到本机默认的文件夹接收目录
    //（<数据根目录>Users\<登录账号>\ChatFolder\<文件夹名>，同名时由接收端状态机自动
    //改名成 xxx(2) / xxx(3)……），随后发 cmd_folder_response(accept=1)。想自己挑位置的
    //用户走旁边的「另存为」。
    case balloonchatwnd::kChatFolderAccept:
    {
        std::wstring saveDir;
        if (!EnsureDefaultFolderReceiveDir(saveDir))
        {
            //默认目录建不出来（尚未登录、数据根目录只读或已被删除等）：如实告知，
            //并指出「另存为」这条退路，不能什么都不发生让用户以为按钮点坏了。
            ::MessageBox(m_hWnd,
                         TR(L"无法创建默认的接收目录，请改用「另存为」选择其他保存位置。"),
                         g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
            break;
        }

        AcceptFolderInvite(*d, saveDir);
        break;
    }

    //另存为：同样是同意接收，只是先弹「选择保存位置」由用户挑落地位置 —— 一个文件夹
    //可能有几十 GB，想放到别的盘时走这条路。选定之后的流程与「接收」完全一致。
    case balloonchatwnd::kChatFolderSaveAs:
    {
        std::wstring saveDir;
        if (!PickFolderPath(TR(L"选择保存位置"), saveDir))
        {
            LOG_INFO("Folder recv: user canceled the save-location dialog, session=%s.",
                     d->sessionId.c_str());
            break;
        }

        AcceptFolderInvite(*d, saveDir);
        break;
    }

    //拒绝：发 cmd_folder_response(accept=0)，会话立即结束，不产生任何中转连接。
    case balloonchatwnd::kChatFolderReject:
    {
        LOG_INFO("Folder recv: user rejected invite, session=%s.", d->sessionId.c_str());
        mgr.RejectInvite(d->sessionId);

        balloonchatwnd::ChatMessage msg;
        if (GetFolderMessage(d->sessionId, msg))
        {
            msg.folderState = balloonchatwnd::kFolderRejected;
            SetFolderMessage(d->sessionId, msg);
        }
        break;
    }

    //取消：应答前是撤回邀请（发 cmd_folder_cancel），传输中则停本端状态机、按失败流程收场。
    //两种情形都由管理器判定，界面不预判；气泡状态等结果回投到达时再改，避免这里先改一次、
    //稍后又被结果覆盖成另一种。
    case balloonchatwnd::kChatFolderCancel:
        LOG_INFO("Folder transfer: user canceled from bubble, session=%s, fromme=%d.",
                 d->sessionId.c_str(), d->fromMe ? 1 : 0);
        mgr.CancelTransfer(d->sessionId);
        //撤回邀请这条路服务端只回一个通用应答、不再推状态，故这里就把气泡收掉。
        ApplyFolderFailure(d->sessionId, kFolderErrCanceled);
        break;

    //重试续传：发 cmd_folder_retry，服务端校验通过后重新下发中转参数，双方复用上次的
    //清单与已落盘进度接着传，全程不再打扰接收方。
    case balloonchatwnd::kChatFolderRetry:
    {
        const int retryVerdict = mgr.RetryTransfer(d->sessionId);
        if (retryVerdict == foldertransfer::kFolderStartOk)
        {
            //这一笔上一次失败时，服务端已经落过一条 result=7 的记录、把气泡接管走了
            //（气泡身上带着那条记录的 msgid）。重试意味着它重新变成一条活的本地气泡：
            //先把 msgid 清掉，后续的进度更新才落得进去，本次尝试结束时到达的那条新记录
            //也才接管得了它。没被接管过的气泡走这一步什么都不会变。
            balloonchatwnd::ChatMessageList* retryList = GetMessageList();
            if (retryList != NULL)
            {
                retryList->ReviveFolderMessage(d->sessionId);
            }

            balloonchatwnd::ChatMessage msg;
            if (GetFolderMessage(d->sessionId, msg))
            {
                //先回到「传输中」，让用户立刻看到点下去有反应；真正的进度随后就来。
                msg.folderState   = balloonchatwnd::kFolderTransferring;
                msg.folderErrCode = 0;
                SetFolderMessage(d->sessionId, msg);
            }
            break;
        }

        LOG_WARNING("Folder transfer: retry refused, session=%s, verdict=%d.",
                    d->sessionId.c_str(), retryVerdict);
        if (retryVerdict == foldertransfer::kFolderStartBusy)
        {
            //名额被别的几笔占满：这一笔并没有过期，气泡照旧停在失败态，等用户稍后再点。
            ::MessageBox(m_hWnd, (LPCTSTR)FolderBusyTipText(),
                         g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
            break;
        }

        ::MessageBox(m_hWnd,
                     TR(L"这次传输已超过可续传的时限，请重新发送该文件夹。"),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        ApplyFolderFailure(d->sessionId, kFolderErrSessionExpired);
        break;
    }

    //打开文件夹：发送端开的是源目录，接收端开的是落地目录，两端同一个动作 id，
    //开哪个由气泡里记着的 localRoot 决定（收发两侧写进去的本就不是同一个路径）。
    case balloonchatwnd::kChatFolderOpenFolder:
        if (d->localRoot.IsEmpty())
        {
            LOG_WARNING("Folder transfer: open folder skipped, local root is empty, session=%s.",
                        d->sessionId.c_str());
            break;
        }
        ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", (LPCWSTR)d->localRoot,
                        NULL, SW_SHOWNORMAL);
        break;

    //重新发送：会话已不可续传（超过保留期），从零发起一次完整传输，重新征求对方同意。
    //沿用气泡里记着的源目录，省得用户再选一次。
    case balloonchatwnd::kChatFolderResend:
    {
        if (d->localRoot.IsEmpty())
        {
            //源目录没记住（例如是接收端那条气泡），退回让用户重新选。
            PickAndSendFolder();
            break;
        }

        std::string localId;
        const int verdict = mgr.StartSend((int64_t)m_peerUserId,
                                          std::wstring((LPCWSTR)d->localRoot), localId);
        if (verdict != foldertransfer::kFolderStartOk)
        {
            LOG_WARNING("Folder send: resend refused, verdict=%d.", verdict);
            ::MessageBox(m_hWnd, (LPCTSTR)FolderBusyTipText(),
                         g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
            break;
        }

        //另起一条新气泡：这是一次全新的传输（重新征求同意、从零开始），与旧那条不是同一笔。
        balloonchatwnd::ChatMessageList* list = GetMessageList();
        if (list != NULL)
        {
            balloonchatwnd::ChatMessage msg;
            msg.kind            = balloonchatwnd::kMsgFolder;
            msg.fromMe          = true;
            msg.folderName      = d->folderName;
            msg.folderState     = balloonchatwnd::kFolderInviting;
            msg.folderSessionId = localId;
            msg.folderLocalRoot = d->localRoot;
            FillSenderAvatar(msg, true);
            list->AppendMessage(msg);
        }
        break;
    }

    //未知动作：气泡与本窗口的动作枚举对不上，只可能是一方漏改，记日志好定位。
    default:
        LOG_ERROR("Folder transfer: unknown bubble action %d, session=%s.",
                  d->action, d->sessionId.c_str());
        break;
    }

    delete d;
    return 0;
}

bool CBalloonChatWindow::PickFolderPath(LPCTSTR title, std::wstring& out)
{
    out.clear();

    // 弹系统选目录框。BIF_NEWDIALOGSTYLE 需调用线程已初始化 OLE，故就地配平
    // CoInitialize / CoUninitialize（已初始化时返回 S_FALSE，仍需配平以不改引用计数语义）。
    // 与「系统设置 → 存储」里选数据目录用的是同一套写法，不另造一份。
    HRESULT hrCo = ::CoInitialize(NULL);

    BROWSEINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);
    TCHAR szPath[MAX_PATH] = { 0 };
    BOOL  gotPath = FALSE;
    if (pidl != NULL)
    {
        gotPath = ::SHGetPathFromIDList(pidl, szPath);
        ::CoTaskMemFree(pidl);
    }

    if (SUCCEEDED(hrCo))
    {
        ::CoUninitialize();
    }

    if (!gotPath || szPath[0] == _T('\0'))
    {
        return false;
    }

    out = szPath;
    return true;
}

int CBalloonChatWindow::StartFolderTransfer(const std::wstring& folderPath)
{
    std::string localId;
    const int verdict = foldertransfer::FolderTransferManager::GetInstance().StartSend(
        (int64_t)m_peerUserId, folderPath, localId);
    if (verdict != foldertransfer::kFolderStartOk)
    {
        LOG_WARNING("Folder send: start refused, verdict=%d, peer=%u.", verdict, m_peerUserId);
        return verdict;
    }

    // 扫描在工作线程上跑，规模统计要等 FMG_MSG_V2_FOLDER_SCANNED 才有；这里先把气泡建
    // 出来（挂本地占位会话 id），用户立刻能看到"已经在发了"，随后由扫描结果补齐数字。
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        balloonchatwnd::ChatMessage msg;
        msg.kind            = balloonchatwnd::kMsgFolder;
        msg.fromMe          = true;
        msg.folderName      = CString(foldertransfer::ExtractFolderName(folderPath).c_str());
        msg.folderState     = balloonchatwnd::kFolderInviting;
        msg.folderSessionId = localId;
        msg.folderLocalRoot = CString(folderPath.c_str());
        FillSenderAvatar(msg, true);
        list->AppendMessage(msg);
    }

    LOG_INFO("Folder send: bubble created, peer=%u, localid=%s.", m_peerUserId, localId.c_str());
    return foldertransfer::kFolderStartOk;
}

void CBalloonChatWindow::PickAndSendFolder()
{
    // 文件夹传输要与对方建中转通道，离线时这一步根本走不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无对端绑定）不支持真实发文件夹。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 对方不在线时中转通道建不起来，服务端会直接拒绝。在这里就拦下并说明原因，
    // 免得用户白选一次目录、再看到一条"发送失败"的气泡。
    if (!CheckPeerOnlineForFolder())
    {
        return;
    }

    // 本机同时可进行的文件夹传输有笔数上限（发送与接收各算一笔，上限见 flamingo.ini 的
    // [app] foldermaxconcurrent）。先在这里拦一道给出明确提示，免得用户白选一次目录；
    // 管理器与服务端另有两道同样的校验，服务端是权威方。
    if (foldertransfer::FolderTransferManager::GetInstance().IsBusy())
    {
        ::MessageBox(m_hWnd, (LPCTSTR)FolderBusyTipText(),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring folderPath;
    if (!PickFolderPath(TR(L"选择要发送的文件夹"), folderPath))
    {
        return;
    }

    const int verdict = StartFolderTransfer(folderPath);
    if (verdict != foldertransfer::kFolderStartOk)
    {
        const CString tip = (verdict == foldertransfer::kFolderStartBusy)
                            ? FolderBusyTipText()
                            : CString(TR(L"无法发送这个文件夹，请换一个目录再试。"));
        ::MessageBox(m_hWnd, (LPCTSTR)tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

// 提示文案里最多点名列出几个文件夹。多于这个数就写成「素材、备份、日志 等 8 个」——
// 一次拖十几个文件夹进来时全列出来会把提示框拉得很长，前几个名字加上总数已经够用户
// 认出是哪一批了。
static const int kMaxNamedFoldersInTip = 3;

/**
 *  把一组文件夹名拼成提示里用的一句话，最多点名 kMaxNamedFoldersInTip 个，多出来的
 *  只报总数。
 *
 *  @param names 文件夹名（不含路径），顺序即用户拖入顺序
 *  @return 形如「素材、备份」或「素材、备份、日志 等 8 个」的文本；names 为空时返回空串
 */
static CString DescribeFolderNames(const std::vector<std::wstring>& names)
{
    CString text;
    const int total  = (int)names.size();
    const int listed = (total < kMaxNamedFoldersInTip) ? total : kMaxNamedFoldersInTip;
    for (int i = 0; i < listed; ++i)
    {
        if (i > 0)
        {
            text += _T("、");
        }
        text += CString(names[i].c_str());
    }
    if (total > listed)
    {
        //{0} 为上面拼好的名字列表，{1} 为文件夹总数
        text = TRF(L"{0} 等 {1} 个", text, total).c_str();
    }
    return text;
}

void CBalloonChatWindow::SendDroppedFolders(const std::vector<std::wstring>& folders)
{
    if (folders.empty())
    {
        return;
    }

    // 调试入口（无对端绑定）与离线态都发不了文件夹，门禁与工具栏「发送文件夹」按钮一致。
    if (m_peerUserId == 0)
    {
        return;
    }
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 对方不在线时一个都发不了，整批直接拦下，理由同工具栏那条入口。
    if (!CheckPeerOnlineForFolder())
    {
        return;
    }

    std::vector<std::wstring> busyNames;      // 因本机并发笔数已达上限而没发起的
    std::vector<std::wstring> refusedNames;   // 被管理器以别的理由拒绝的（如拖进来的是盘符根目录）
    int startedCount = 0;

    for (std::vector<std::wstring>::size_type i = 0; i < folders.size(); ++i)
    {
        const int verdict = StartFolderTransfer(folders[i]);
        if (verdict == foldertransfer::kFolderStartOk)
        {
            ++startedCount;
            continue;
        }

        if (verdict == foldertransfer::kFolderStartBusy)
        {
            // 名额已满：本笔连同<u>后面所有</u>都不再发起。名额只有等已在跑的某一笔结束
            // 才会放开，继续往下试只是把同一个失败重复若干次。
            for (std::vector<std::wstring>::size_type j = i; j < folders.size(); ++j)
            {
                busyNames.push_back(foldertransfer::ExtractFolderName(folders[j]));
            }
            break;
        }

        // 其余理由（路径非法等）只影响这一个，后面的照发。
        refusedNames.push_back(foldertransfer::ExtractFolderName(folders[i]));
    }

    LOG_INFO("Folder send by drop: total %d, started %d, busy %d, refused %d, peer=%u.",
             (int)folders.size(), startedCount, (int)busyNames.size(),
             (int)refusedNames.size(), m_peerUserId);

    if (busyNames.empty() && refusedNames.empty())
    {
        return;
    }

    // 没发起的要说清是哪几个、为什么 —— 只说"有几个没发出去"，用户还得自己一个个核对。
    CString tip;
    if (!busyNames.empty())
    {
        tip.Format(TR(L"同时进行的文件夹传输已达上限（最多 %d 笔），以下 %d 个文件夹未发起：%s。"
                   L"请等其中一笔结束后再把它们拖进来。"),
                   foldertransfer::FolderTransferManager::GetInstance().GetMaxConcurrent(),
                   (int)busyNames.size(),
                   (LPCTSTR)DescribeFolderNames(busyNames));
    }
    if (!refusedNames.empty())
    {
        CString refusedText;
        refusedText.Format(TR(L"以下 %d 个文件夹无法发送，请确认它不是磁盘根目录后再试：%s。"),
                           (int)refusedNames.size(),
                           (LPCTSTR)DescribeFolderNames(refusedNames));
        if (!tip.IsEmpty())
        {
            tip += _T("\n");
        }
        tip += refusedText;
    }
    ::MessageBox(m_hWnd, (LPCTSTR)tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
}

void CBalloonChatWindow::AppendFolderInviteBubble(const std::string& sessionIdUtf8,
                                                  const std::string& folderNameUtf8,
                                                  long long totalDirs, long long totalFiles,
                                                  long long totalBytes)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return;
    }

    std::wstring folderName;
    foldertransfer::Utf8ToUtf16(folderNameUtf8, folderName);

    balloonchatwnd::ChatMessage msg;
    msg.kind             = balloonchatwnd::kMsgFolder;
    msg.fromMe           = false;
    msg.folderName       = CString(folderName.c_str());
    msg.folderFileCount  = (int)totalFiles;
    msg.folderDirCount   = (int)totalDirs;
    msg.folderSizeBytes  = totalBytes;
    msg.folderSize       = CString(balloonchatwnd::FormatFileSize(totalBytes).c_str());
    msg.folderTotalBytes = totalBytes;
    //邀请态：接收端此时看到的是「接收 / 另存为 / 拒绝」三个按钮，没有进度条。
    msg.folderState      = balloonchatwnd::kFolderInviting;
    msg.folderSessionId  = sessionIdUtf8;
    FillSenderAvatar(msg, false);
    list->AppendMessage(msg);
}

void CBalloonChatWindow::OnFolderInvite(const net::FolderInviteNotify& notify)
{
    //密聊状态下列表里只显示密聊内容：邀请留在文件夹传输管理器里，退出密聊、首屏历史渲染完时由
    //RestorePendingFolderInvite 补出
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        LOG_INFO("Folder invite deferred in secret mode, session=%s.", notify.m_sessionId.c_str());
        return;
    }

    //单聊单文件直传的邀请画成待接收的文件气泡（2026-10-04 起）
    if (notify.m_kind == net::folder_kind_file)
    {
        AppendDirectFileInviteBubble(notify.m_sessionId, notify.m_folderName, (long long)notify.m_totalSize);
        return;
    }

    //走到这里说明本窗口<u>已经打开着</u>：主面板只把邀请交给已打开的聊天窗，窗口没开
    //时改由会话列表未读 + 托盘闪烁提醒（见 CBalloonMainPanel::OnFolderInviteNotify）。
    //故这里既不建窗、也不再把窗口弹到前台 —— 用户没有主动打开会话时不打扰他。
    AppendFolderInviteBubble(notify.m_sessionId, notify.m_folderName,
                             (long long)notify.m_totalDirs, (long long)notify.m_totalFiles,
                             (long long)notify.m_totalSize);

    LOG_INFO("Folder recv: invite bubble created, session=%s, files=%lld, bytes=%lld.",
             notify.m_sessionId.c_str(), (long long)notify.m_totalFiles,
             (long long)notify.m_totalSize);
}

void CBalloonChatWindow::RestorePendingFolderInvite()
{
    //调试入口（--chatwindow，无对端绑定）没有真实会话可查。
    if (m_peerUserId == 0)
    {
        return;
    }

    //可能有多条：同一个人先后发来两个文件夹时，两条邀请会同时挂着（本机同时可进行多笔
    //文件夹传输），都要补画出来，只画一条会让另一条永远没机会被应答。
    std::vector<foldertransfer::FolderPendingInvite> invites;
    if (foldertransfer::FolderTransferManager::GetInstance().GetPendingInvites(
            (int64_t)m_peerUserId, invites) == 0)
    {
        //与本会话对端之间没有待应答的邀请，属最常见情形，不必记日志。
        return;
    }

    for (size_t i = 0; i < invites.size(); ++i)
    {
        const foldertransfer::FolderPendingInvite& invite = invites[i];

        //单聊单文件直传的邀请补成文件气泡（已经画过的不会重复画）
        if (invite.m_kind == net::folder_kind_file)
        {
            AppendDirectFileInviteBubble(invite.m_sessionId, invite.m_folderNameUtf8, (long long)invite.m_totalBytes);
            continue;
        }

        //已经画过了（本方法在建窗与首屏历史渲染完之后各调一次，实时推送也可能已经补上）：
        //按会话 id 查得到本行即说明气泡在，跳过，避免同一条邀请画出两遍。
        balloonchatwnd::ChatMessage existing;
        if (GetFolderMessage(invite.m_sessionId, existing))
        {
            continue;
        }

        AppendFolderInviteBubble(invite.m_sessionId, invite.m_folderNameUtf8,
                                 (long long)invite.m_dirCount, (long long)invite.m_fileCount,
                                 (long long)invite.m_totalBytes);

        LOG_INFO("Folder recv: pending invite restored, session=%s, peer=%u, files=%lld, bytes=%lld.",
                 invite.m_sessionId.c_str(), m_peerUserId,
                 (long long)invite.m_fileCount, (long long)invite.m_totalBytes);
    }
}

void CBalloonChatWindow::OnFolderScanned(const foldertransfer::FolderScanNotice& notice)
{
    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(notice.m_localId, msg))
    {
        //本地占位 id 只存在于发起这次传输的那个窗口里，别的窗口查不到属正常情形。
        return;
    }

    if (notice.m_scanError != foldertransfer::kFolderScanOk)
    {
        //扫描失败即整个文件夹失败（需求：任一错误即整体失败）。用「读源文件失败」这个
        //错误码 —— 无权限、目录不存在、枚举出错都属于"本端读不出来"这一类。
        LOG_ERROR("Folder send: scan failed, localid=%s, error=%d, path=%s.",
                  notice.m_localId.c_str(), notice.m_scanError, notice.m_errorPath.c_str());
        msg.folderState   = balloonchatwnd::kFolderFailed;
        msg.folderErrCode = kFolderErrReadFail;
        //本地扫描失败没有服务端会话，谈不上续传，「重试」不给。
        msg.folderWithinRetry = false;
        SetFolderMessage(notice.m_localId, msg);
        return;
    }

    msg.folderName       = CString(notice.m_folderName.c_str());
    msg.folderFileCount  = (int)notice.m_fileCount;
    msg.folderDirCount   = (int)notice.m_dirCount;
    msg.folderSizeBytes  = notice.m_totalBytes;
    msg.folderSize       = CString(balloonchatwnd::FormatFileSize(notice.m_totalBytes).c_str());
    msg.folderTotalBytes = notice.m_totalBytes;
    msg.folderLocalRoot  = CString(notice.m_rootPath.c_str());
    SetFolderMessage(notice.m_localId, msg);
}

void CBalloonChatWindow::OnFolderBound(const foldertransfer::FolderBindNotice& bind)
{
    //在线直传的文件气泡（2026-10-04 起）
    if (OnDirectFileBound(bind))
    {
        return;
    }

    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(bind.m_localId, msg))
    {
        return;
    }

    if (bind.m_code == kFolderErrOk)
    {
        //把气泡上的本地占位 id 换成服务端分配的真会话 id，此后所有信令都按它定位本行。
        msg.folderSessionId = bind.m_sessionId;
        SetFolderMessage(bind.m_localId, msg);

        //真会话 id 到手的这一刻，把发送端的<u>源目录</u>按它登记进本地落点库。日后翻
        //聊天记录 / 消息记录窗时，那条 msgtype=18 的传输记录条据此才给得出「打开文件夹」
        //——记录条本身不含任何路径，服务端也不记。
        if (!msg.folderLocalRoot.IsEmpty())
        {
            balloonhistory::CFolderLocalStore::GetInstance().Upsert(
                bind.m_sessionId, std::wstring((LPCWSTR)msg.folderLocalRoot));
        }

        LOG_INFO("Folder send: bubble bound to server session %s (localid=%s).",
                 bind.m_sessionId.c_str(), bind.m_localId.c_str());
        return;
    }

    //对方不在线：这一笔连会话都没建立起来（服务端在收到邀请时就直接拒绝并返回，
    //不建会话、不落任何消息记录），一个字节都没发出去。此时把气泡改成"发送失败"
    //会在聊天记录里平白留下一条并不存在的失败消息，故改为整行撤掉、另给一句提示。
    //气泡是本地占位的，撤掉之后翻历史也不会再出现。
    //
    //正常情况下发起之前的 CheckPeerOnlineForFolder 已经拦住了，走到这里说明本端
    //掌握的对端状态是旧的（对方刚掉线、推送还没到），这是那道门禁的兜底。
    if (balloonchatwnd::FolderInviteRejectShouldRemoveBubble(bind.m_code))
    {
        balloonchatwnd::ChatMessageList* list = GetMessageList();
        if (list != NULL)
        {
            list->RemoveFolderMessage(bind.m_localId);
        }
        LOG_INFO("Folder send: invite rejected because peer is offline, bubble removed "
                 "(localid=%s, peer=%u).", bind.m_localId.c_str(), m_peerUserId);
        ::MessageBox(m_hWnd, TR(kFolderPeerOfflineTip), g_strAppTitle.c_str(),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    //其余理由（规模超限 / 服务器繁忙 / 对方满额等）：气泡转失败态，失败原因用服务端
    //给的码，双方看到的是同一条文案。
    msg.folderState       = balloonchatwnd::kFolderFailed;
    msg.folderErrCode     = bind.m_code;
    msg.folderWithinRetry = false;
    SetFolderMessage(bind.m_localId, msg);

    //规模超限时另给一次提示，把服务端配置的上限值原样告诉用户 —— 客户端不硬编码上限，
    //改了服务端配置提示文案自然跟着变。
    if (bind.m_code == kFolderErrTooManyFiles
        || bind.m_code == kFolderErrTooLarge)
    {
        CString tip;
        tip.Format(TR(L"这个文件夹超出了服务器的限制：单次最多 %lld 个文件、总大小不超过 %s。"),
                   (long long)bind.m_maxFiles,
                   balloonchatwnd::FormatFileSize(bind.m_maxSize).c_str());
        ::MessageBox(m_hWnd, (LPCTSTR)tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

void CBalloonChatWindow::OnFolderState(const net::FolderStateNotify& notify)
{
    //在线直传的文件气泡（2026-10-04 起）
    if (OnDirectFileState(notify))
    {
        return;
    }

    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(notify.m_sessionId, msg))
    {
        return;
    }

    switch (notify.m_state)
    {
    //响铃中：邀请已送达对方，发送端显示「等待对方接收」。
    case kFolderStateRinging:
        msg.folderState = balloonchatwnd::kFolderInviting;
        break;

    //传输中：目录已建好、正在收发字节，气泡开始显示进度条。
    case kFolderStateTransferring:
        msg.folderState = balloonchatwnd::kFolderTransferring;
        break;

    //对方拒绝接收。
    case kFolderStateRejected:
        msg.folderState = balloonchatwnd::kFolderRejected;
        break;

    //发送方在对方应答前撤回了邀请。
    case kFolderStateCanceled:
        msg.folderState = balloonchatwnd::kFolderInviteCanceled;
        break;

    //对方不在线：发起时就不在线，或响铃期间退出登录。
    case kFolderStatePeerOffline:
        msg.folderState = balloonchatwnd::kFolderPeerOffline;
        break;

    //邀请超时无人应答，服务端已自动结束会话。
    case kFolderStateTimeout:
        msg.folderState = balloonchatwnd::kFolderTimeout;
        break;

    //传输失败：失败原因与「重试」可用与否由 ApplyFolderFailure 统一处理，
    //本分支直接转交，不在这里另写一份。
    case kFolderStateFailed:
        ApplyFolderFailure(notify.m_sessionId, notify.m_errCode);
        return;

    //全部完成并通过校验。
    case kFolderStateCompleted:
        msg.folderState    = balloonchatwnd::kFolderCompleted;
        msg.folderProgress = kFolderFullPercent;
        break;

    //越界状态：解析层已按 1~8 校验过，走到这里说明协议实现不一致，记日志后不动界面。
    default:
        LOG_ERROR("Folder transfer: unexpected state %d for session %s.",
                  notify.m_state, notify.m_sessionId.c_str());
        return;
    }

    SetFolderMessage(notify.m_sessionId, msg);
}

void CBalloonChatWindow::OnFolderTransferStarted(const std::string& sessionId)
{
    //在线直传的文件气泡（2026-10-04 起）
    if (OnDirectFileTransferStarted(sessionId))
    {
        return;
    }

    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(sessionId, msg))
    {
        return;
    }

    msg.folderState   = balloonchatwnd::kFolderTransferring;
    msg.folderErrCode = 0;
    SetFolderMessage(sessionId, msg);
}

void CBalloonChatWindow::OnFolderProgress(const foldertransfer::FolderProgressNotice& progress)
{
    //在线直传的文件气泡（2026-10-04 起）
    if (OnDirectFileProgress(progress))
    {
        return;
    }

    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(progress.m_sessionId, msg))
    {
        return;
    }

    msg.folderState      = balloonchatwnd::kFolderTransferring;
    //链路实际路径：直连时气泡上画「P2P」徽章（2026-10-04 起）
    msg.folderLinkPath   = progress.m_linkPath;
    msg.folderSentBytes  = progress.m_doneBytes;
    msg.folderTotalBytes = progress.m_totalBytes;
    msg.folderProgress   = (progress.m_totalBytes > 0)
                           ? (int)(progress.m_doneBytes * kFolderFullPercent / progress.m_totalBytes)
                           : 0;
    //完成文件数从 0 起，而气泡上写的是"第 i 个"，故显示序号取"已完成数 + 1"，
    //并夹到文件总数之内，免得最后一个文件传完时冒出"第 n+1 个"。
    msg.folderCurIndex   = (int)(progress.m_doneFiles + 1);
    if (progress.m_totalFiles > 0 && msg.folderCurIndex > (int)progress.m_totalFiles)
    {
        msg.folderCurIndex = (int)progress.m_totalFiles;
    }

    //状态行只显示文件名，清单里的相对路径太长会把那一行挤爆。
    std::wstring current;
    foldertransfer::Utf8ToUtf16(progress.m_currentFile, current);
    const std::wstring::size_type cut = current.find_last_of(L"\\/");
    if (cut != std::wstring::npos)
    {
        current = current.substr(cut + 1);
    }
    msg.folderCurFile = CString(current.c_str());

    SetFolderMessage(progress.m_sessionId, msg);
}

void CBalloonChatWindow::OnFolderResult(const foldertransfer::FolderResultNotice& result)
{
    //在线直传的文件气泡（2026-10-04 起）
    if (OnDirectFileResult(result))
    {
        return;
    }

    //本地根目录一律先登记：接收端在这里才第一次拿到落地目录（含重名自动改名之后的名字），
    //发送端登记的则是绑定会话时那个源目录（这里再写一遍，覆盖成同一个值，无副作用）。
    //放在成功 / 失败分叉<u>之前</u>：失败的那一笔在保留期内还能重试，重试成功时的记录条
    //仍要能打开这个目录；接收端即便这次失败，已落盘的那部分也在这个目录里。
    if (!result.m_sessionId.empty() && !result.m_localRoot.empty())
    {
        balloonhistory::CFolderLocalStore::GetInstance().Upsert(result.m_sessionId,
                                                                result.m_localRoot);
    }

    if (!result.m_success)
    {
        ApplyFolderFailure(result.m_sessionId, result.m_errCode);
        return;
    }

    balloonchatwnd::ChatMessage msg;
    if (!GetFolderMessage(result.m_sessionId, msg))
    {
        return;
    }

    msg.folderState    = balloonchatwnd::kFolderCompleted;
    msg.folderProgress = kFolderFullPercent;
    msg.folderErrCode  = 0;
    msg.folderLinkPath = result.m_linkPath;
    msg.folderCurFile.Empty();
    if (!result.m_localRoot.empty())
    {
        //接收端在这里才第一次拿到落地目录（含自动改名之后的名字），「打开文件夹」要用它。
        msg.folderLocalRoot = CString(result.m_localRoot.c_str());
    }
    SetFolderMessage(result.m_sessionId, msg);

    LOG_INFO("Folder transfer: bubble marked completed, session=%s, files=%lld, bytes=%lld.",
             result.m_sessionId.c_str(), (long long)result.m_totalFiles,
             (long long)result.m_totalBytes);
}

void CBalloonChatWindow::OnFolderRecord(const net::MessageNotify& notify, bool fromMe)
{
    //content 解析不出来（字段缺失、结局非终态等）时 m_folderResult 保持 0。这种记录画
    //出来只会是一条什么都说不清的气泡，不如不画，留一条日志供回查。
    const int state = balloonchatwnd::FolderStateFromRecord(notify.m_folderResult,
                                                            notify.m_folderErrCode);
    if (state == 0)
    {
        LOG_WARNING("Folder record: unusable content, msgid=%lld, result=%d, errcode=%d.",
                    (long long)notify.m_msgid, notify.m_folderResult, notify.m_folderErrCode);
        return;
    }

    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return;
    }

    //先看会话里有没有那条还没被接管过的本地气泡。有就在它原地改，界面上不多出一条、
    //位置也不跳；没有就下面新建一条。
    balloonchatwnd::ChatMessage msg;
    const bool adopting = !notify.m_folderSessionId.empty()
                          && list->GetFolderMessage(notify.m_folderSessionId, msg);
    if (!adopting)
    {
        //新建的这条记录气泡：规模统计只能取记录里带来的那份（本机没有这笔传输的现场）。
        msg = balloonchatwnd::ChatMessage();
        msg.kind            = balloonchatwnd::kMsgFolder;
        msg.folderSessionId = notify.m_folderSessionId;
        FillSenderAvatar(msg, fromMe);
    }

    msg.fromMe = fromMe;

    //文件夹名与三项规模统计一律以服务端记录为准：本地气泡上那份是扫描时算的，
    //两者正常相同；真不同时服务端那份才是双方都看得到的那一份。
    std::wstring folderName;
    foldertransfer::Utf8ToUtf16(notify.m_folderName, folderName);
    msg.folderName      = CString(folderName.c_str());
    msg.folderDirCount  = (int)notify.m_folderTotalDirs;
    msg.folderFileCount = (int)notify.m_folderTotalFiles;
    msg.folderSizeBytes = notify.m_folderTotalSize;
    msg.folderSize      = CString(balloonchatwnd::FormatFileSize(notify.m_folderTotalSize).c_str());

    msg.folderState   = state;
    msg.folderErrCode = notify.m_folderErrCode;
    //链路实际路径（2026-10-04 起的记录才有）：直连时画「P2P」徽章
    if (notify.m_folderLinkPath != 0)
    {
        msg.folderLinkPath = notify.m_folderLinkPath;
    }
    //完成态把进度条置满：接管的那条气泡可能停在 99%（最后一片的回调与记录到达谁先谁后
    //说不准），新建的那条则本来就没有进度。
    if (state == balloonchatwnd::kFolderCompleted)
    {
        msg.folderProgress = kFolderFullPercent;
        msg.folderCurFile.Empty();
    }

    //「重试」可用与否仍由管理器手里的保留期计时说了算 —— 那是与服务端 folderretrywindow
    //对齐的唯一一处。翻历史看到的老记录在管理器里查不到会话，自然得到 false，按钮置灰。
    msg.folderWithinRetry =
        foldertransfer::FolderTransferManager::GetInstance().IsWithinRetryWindow(
            notify.m_folderSessionId);

    //本地根目录：接管时气泡上多半已经有了；新建时（换机器、或窗口是传完才打开的）
    //去本机的落点登记库里查一次。查不到就没有「打开文件夹」——那台机器上从来没有过
    //这个目录，这是预期行为。
    if (msg.folderLocalRoot.IsEmpty() && !notify.m_folderSessionId.empty())
    {
        std::wstring registered;
        if (balloonhistory::CFolderLocalStore::GetInstance().Query(notify.m_folderSessionId,
                                                                   registered))
        {
            msg.folderLocalRoot = CString(registered.c_str());
        }
    }

    //填上服务器 msgid 与消息时间 —— 这一步之后本条气泡就不再是"活的本地气泡"，
    //同一会话后续到达的记录（下一次尝试留下的那条）不会再改动它。
    msg.msgid       = (long long)notify.m_msgid;
    msg.timestampMs = (long long)notify.m_timestamp;

    if (adopting)
    {
        list->SetFolderMessage(notify.m_folderSessionId, msg);
        LOG_INFO("Folder record: adopted the live bubble, session=%s, msgid=%lld, result=%d.",
                 notify.m_folderSessionId.c_str(), (long long)notify.m_msgid,
                 notify.m_folderResult);
        return;
    }

    list->AppendMessage(msg);
    LOG_INFO("Folder record: appended as a new row, session=%s, msgid=%lld, result=%d.",
             notify.m_folderSessionId.c_str(), (long long)notify.m_msgid,
             notify.m_folderResult);
}

LRESULT CBalloonChatWindow::OnChatImageAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    // 图片气泡右键菜单冒泡上来：lParam 为堆上 ChatImageActionData，处理后 delete。
    balloonchatwnd::ChatImageActionData* d = (balloonchatwnd::ChatImageActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    // 菜单只在图片就绪时才给这两项，正常不会拿到空路径；真拿到就是状态判定出了岔子，
    // 记一条日志便于回查，不再往下走。
    if (d->imagePath.empty())
    {
        LOG_ERROR("单聊窗图片菜单动作缺本地路径: action=%d.", (int)d->id);
        delete d;
        return 0;
    }

    switch (d->id)
    {
    case balloonchatwnd::kChatMsgMenuSaveImageAs:
        //弹保存对话框 + 按原字节拷贝，与群聊窗共用同一份实现。
        balloonchatwnd::SaveChatImageAs(m_hWnd, d->imagePath, d->saveName);
        break;

    case balloonchatwnd::kChatMsgMenuResendImage:
        // 再次发送 = 把这张图作为一条<u>新</u>图片消息发到本会话，与选图 / 截图发送
        // 共用同一条链路（新的 clientMsgId、重新上传），不是重发原来那条。
        LOG_INFO("单聊窗再次发送图片: to=%u, path=%s.", m_peerUserId,
                 CStringToUtf8(CString(d->imagePath.c_str())).c_str());
        SendImageFile(d->imagePath);
        break;

    // 图片文字识别：前置判断（系统不支持、没有识别语言时提示）后打开识别结果窗口
    case balloonchatwnd::kChatMsgMenuOcr:
        LOG_INFO("single chat window: ocr requested for an image, to=%u.", m_peerUserId);
        ocr::LaunchOcrForFile(m_hWnd, d->imagePath, 0);
        break;

    default:
        LOG_ERROR("单聊窗收到未知的图片菜单动作: action=%d.", (int)d->id);
        break;
    }

    delete d;
    return 0;
}

balloonchatwnd::ChatMessageList* CBalloonChatWindow::GetMessageList()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonchatwnd::ChatMessageList*>(
        content->FindCtrlById(balloonchatwnd::kIdMessageArea));
}

void CBalloonChatWindow::StartFileDownloadFromBubble(const balloonchatwnd::ChatFileActionData& d)
{
    //发起下载并把气泡切到「下载中」与群聊窗共用；缺少定位信息时该函数内部记日志并返回 false
    if (!balloonchatwnd::StartChatFileDownload(d, GetMessageList()))
    {
        return;
    }

    // 告知发送方"我开始收了"。大文件要传一阵子，先让对方看到进展，别干等着。
    ReportFileAck(d.msgid, kFileAckReceiving);
}

void CBalloonChatWindow::ReportFileAck(long long msgid, int state)
{
    // 只有真实会话下的收到消息才谈得上回执：msgid 是服务端分配的，本端发出的消息
    // 与调试入口都没有它；对端 userid 则是这条回执要送达的目标。
    if (m_peerUserId == 0 || msgid <= 0)
    {
        return;
    }
    CIUSocket::GetInstance().ReportFileTransferAck((int64_t)m_peerUserId, msgid, state);
}

void CBalloonChatWindow::RestartFileSendFromBubble(const balloonchatwnd::ChatFileActionData& d)
{
    // 真实会话才重发（调试入口无对端绑定）。
    if (m_peerUserId == 0)
    {
        return;
    }
    if (d.clientMsgId.empty() || d.localPath.IsEmpty())
    {
        LOG_ERROR("单聊窗重发文件失败(缺少定位信息或本地路径): clientmsgid=%s.",
                  d.clientMsgId.c_str());
        return;
    }

    // 源文件可能已被移走 / 删除 —— 那样重发注定失败，直接把气泡标失败并告知用户，
    // 不必白跑一趟传输流程。
    DWORD attr = ::GetFileAttributesW((LPCWSTR)d.localPath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        LOG_ERROR("单聊窗重发文件失败(源文件已不存在): clientmsgid=%s.", d.clientMsgId.c_str());
        balloonchatwnd::ChatMessageList* failList = GetMessageList();
        if (failList != NULL)
        {
            failList->MarkFileUploadFailed(d.clientMsgId);
        }
        ::MessageBoxW(m_hWnd, TR(L"源文件已不存在，无法重新发送。"), TR(L"提示"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 沿用原 clientMsgId：气泡还是那一条，进度 / 结果照旧按它对回，无需另插一行。
    std::wstring localPath((LPCWSTR)d.localPath);
    LOG_INFO("单聊窗重新发送文件: to=%u, clientmsgid=%s.", m_peerUserId, d.clientMsgId.c_str());
    net::FileTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, d.clientMsgId, localPath);

    //气泡回到上传中并清掉上一次的失败标记与原因
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkFileUploadStartedByClientMsgId(d.clientMsgId);
    }
    //重新登记待冒泡：上一次失败时登记已被丢弃，不登记的话这次发送成功后会话不冒泡、预览不更新
    m_sendBumps.Add(d.clientMsgId, balloonchatwnd::MakeFileSendBump(1, (long long)m_peerUserId));
}

void CBalloonChatWindow::MarkFileRejected(long long msgid)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkFileRejectedByMsgid(msgid);
    }
}

LRESULT CBalloonChatWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    // WM_DUI_NOTIFY 的 LPARAM 指向一个 DuiNotify（仅在本次同步派发内有效）。
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdSendButton)
    {
        DoSend();
        return 0;
    }

    // 工具栏的 Markdown 编辑器按钮：打开空白编辑器，发送对象预选本会话（docs/Markdown消息方案.md 第 11.3 节）
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbMarkdown)
    {
        OpenMarkdownEditor(std::string());
        return 0;
    }

    // 顶部离线提示带只作告知、不可点击，故这里没有它的分支 —— 恢复在线统一走托盘 /
    // 主面板的状态菜单那个唯一入口。

    // 点发送按钮右侧「▼」→ 弹发送模式切换菜单。这里只投递自定义消息、不直接弹，
    // 避免在 DUI 鼠标事件的同步派发栈里启动模态菜单导致捕获/重入问题。
    if (pNotify != NULL
        && pNotify->code == balloonchatwnd::ChatSendButton::DUIN_SENDBTN_DROPDOWN
        && pNotify->ctrlId == balloonchatwnd::kIdSendButton)
    {
        ::PostMessage(m_hWnd, kOpenSendMenuMsg, 0, 0);
        return 0;
    }

    // 点击头部"消息记录"按钮 → 请主面板打开与对端的历史消息记录窗，落在"全部"分类上。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrHistory)
    {
        RequestOpenHistory(balloonhistory::kHistoryTypeAll);
        return 0;
    }

    // 点击头部「密聊」按钮 → 发起密聊。可能要弹确认框（本机无法防截图），先投递消息，等鼠标事件处理完再执行
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrSecret)
    {
        ::PostMessage(m_hWnd, kSecretActionMsg, (WPARAM)kSecretActInvite, 0);
        return 0;
    }

    // 密聊头部条上的三个链接：退出密聊、重新邀请、安全码。同样先投递，等鼠标事件处理完再执行（会弹框、会重建列表）
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && (pNotify->ctrlId == balloonchatwnd::kIdSecretExit
            || pNotify->ctrlId == balloonchatwnd::kIdSecretReinvite
            || pNotify->ctrlId == balloonchatwnd::kIdSecretSafety))
    {
        WPARAM action = (WPARAM)kSecretActExit;
        if (pNotify->ctrlId == balloonchatwnd::kIdSecretReinvite)
        {
            action = (WPARAM)kSecretActReinvite;
        }
        else if (pNotify->ctrlId == balloonchatwnd::kIdSecretSafety)
        {
            action = (WPARAM)kSecretActSafetyCode;
        }
        ::PostMessage(m_hWnd, kSecretActionMsg, action, 0);
        return 0;
    }

    // 点击头部「字幕模式」按钮 → 进入或退出字幕模式（桌面上显示本会话最新一条消息的字幕）
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrSubtitle)
    {
        ToggleSubtitleMode();
        return 0;
    }

    // 点击头部下方的「已 Pin N 条」提示带 → 同样是打开历史消息记录窗，但直接切到 Pin
    // 分类。本轮不另做一个"Pin 列表浮层"：消息记录窗里那份列表（可搜索、可翻页、可定位、
    // 可取消 Pin）本来就是要的东西，复用它比再画一份省得多，见 docs/消息Pin接入方案.md §8。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdPinBanner)
    {
        RequestOpenHistory(balloonhistory::kHistoryTypePinned);
        return 0;
    }

    // 点击头部"远程桌面"按钮（显示器图标）→ 通知主面板远程桌面协调器发起邀请
    // （由协调器统一发 cmd_rd_invite 并弹出"呼叫中"对话框）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrRemoteDesktop)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            //单聊窗的 m_hMainDlg 为 NULL（关闭只销毁本窗、不退出程序），故经主面板
            //（m_hostPanel）转发给通话协调器。peerName 走堆 std::wstring，
            //最终由通话协调器处理后 delete。
            ::PostMessage(m_hostPanel, FMG_MSG_RD_START_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击头部「共享屏幕」按钮 → 经屏幕共享中心交给 FlamingoMedia.dll：先弹出共享内容选择窗，选好后才发起。
    // 工具栏「语音消息」→ 开始录音并显示录音条；录音条上的各个按钮交给录音会话（语音消息第 3b、6g 步）。
    // 录音会话只认自己那几个按钮编号，别的控件的点击返回 false、照常往下分派
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbVoice)
    {
        StartVoiceRecording();
        return 0;
    }
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && m_voiceSession.HandleButton(pNotify->ctrlId))
    {
        return 0;
    }
    // 正在共享或观看、系统不支持时由 DLL 提示原因。
    // 工具栏「屏幕录制」→ 弹出录制菜单（开始录制 / 停止录制、录制管理，docs/语音字幕方案.md 第 2 节第 10 条），
    // 菜单不能在鼠标事件里弹，先投递消息。录制本身不需要联网，不做在线检查
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbRecord)
    {
        PostMessage(kOpenRecordMenuMsg);
        return 0;
    }
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrScreenShare)
    {
        // 屏幕共享要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0)
        {
            balloonscreenshare::CScreenShareHub::GetInstance().StartShare(kConvTypeSingle, (int64_t)m_peerUserId,
                                                                          m_peerName);
        }
        return 0;
    }

    // 点击头部"语音通话"按钮 → 通知主面板通话协调器发起语音呼叫（协调器统一发
    // cmd_call_invite 并弹"呼叫中"对话框）。机制同上方远程桌面：单聊窗 m_hMainDlg 为
    // NULL，故经主面板 m_hostPanel 转发给通话协调器；peerName 走堆 std::wstring，
    // 最终由通话协调器处理后 delete。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrVoice)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_CALL_START_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击头部"视频通话"按钮 → 发起视频呼叫：转投主面板通话协调器（同语音按钮两步路由，
    // 仅消息不同），由其发 cmd_call_invite(media=video) 并弹"呼叫中"对话框。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrVideo)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_CALL_START_VIDEO_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击「代码」工具栏按钮 → 打开「发送代码」窗口（docs/代码片段方案.md 第 3.1 节）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbCode)
    {
        OpenCodeCompose(std::wstring(), RememberedCodeLang());
        return 0;
    }

    // 点了粘贴识别提示条上的「以代码片段发送」→ 把输入框里的文字移进发送代码窗口（语言按自动识别），输入框清空。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdCodePasteSend)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatInputRichEdit* input = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kIdInputEdit))
            : NULL;
        // 先收起提示条（同时清掉记下的文字），清空输入框引起的变化通知就不会再处理提示条
        ShowCodePasteBar(false);
        if (input != NULL)
        {
            const std::wstring code = balloonchatwnd::NormalizeCodeNewlines(std::wstring((LPCWSTR)input->GetText()));
            input->SetText(_T(""));
            OpenCodeCompose(code, std::string());
        }
        return 0;
    }

    // 点了粘贴识别提示条上的「不用了」→ 只收起提示条，输入框不动，回车照常按普通文字发送。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdCodePasteDismiss)
    {
        ShowCodePasteBar(false);
        return 0;
    }

    // 点击"窗口抖动"工具栏按钮 → 给对端发一条抖动消息。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbShake)
    {
        DoSendShake();
        return 0;
    }

    // 点击"表情"工具栏按钮 → 打开 emoji 选择面板。
    // 引用条上的 ✕：取消本次引用（清空待发快照并收起引用条）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdReplyClose)
    {
        CancelPendingReply();
        return 0;
    }

    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbEmoji)
    {
        OpenEmojiPicker();
        return 0;
    }

    // 点击"字体"工具栏按钮（AF）→ 打开字体设置面板。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbFont)
    {
        OpenFontPanel();
        return 0;
    }

    // 点击"图片"工具栏按钮 → 选图并作为图片消息发送。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbImage)
    {
        PickAndSendImage();
        return 0;
    }

    // 点击"文件"工具栏按钮 → 选文件并作为文件消息发送。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbFile)
    {
        PickAndSendFile();
        return 0;
    }

    // 点击"发送文件夹"工具栏按钮 → 选一个本地目录整体发给对方（走 cmd_folder_* 那条
    // 独立链路，与单个文件传输不是一回事）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbFolder)
    {
        PickAndSendFolder();
        return 0;
    }

    // 点击「文件共享」工具栏按钮 → 在按钮旁弹出本会话的共享列表浮层
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrShare)
    {
        OpenConversationShares();
        return 0;
    }

    // 点击"截图"工具栏按钮 → 调起全屏截图浮层。这里只投递自定义消息、不直接发起：
    // 截图浮层是模态消息循环，若在 DUI 鼠标事件派发栈内启动会引发捕获 / 重入问题，
    // 导致截图结束后发送按钮等点击失灵（与发送▼菜单同理）。延后到鼠标事件外再发起。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbScreenshot)
    {
        ::PostMessage(m_hWnd, kDoScreenshotMsg, (WPARAM)balloonchatwnd::kShotReqCapture, 0);
        return 0;
    }

    // 点击剪刀右边的 ▾ → 弹出截图选项菜单。同样延后到鼠标事件之外再弹（模态菜单）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbScreenshotMenu)
    {
        ::PostMessage(m_hWnd, kDoScreenshotMsg, (WPARAM)balloonchatwnd::kShotReqMenu, 0);
        return 0;
    }

    // 点击头部信息块（个性签名所在）→ 请主面板打开对方的联系人资料窗。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHeaderInfo)
    {
        if (::IsWindow(m_hostPanel) && m_peerUserId != 0)
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CONTACT_PROFILE,
                          (WPARAM)m_peerUserId, 0);
        }
        return 0;
    }

    // 用户拖完中间分隔条（kIdInputSplitter）→ 记下当前"输入区块高度"，供本次
    // 运行期间新开的聊天窗、以及本窗口后续缩放时套用。分隔条松手时发一次
    // DUIN_VALUECHANGED；输入区块高度 = 分隔轴可用高度 - 消息区(pane0)高度。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kIdInputSplitter)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiSplitter* sp = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiSplitter*>(
                  content->FindCtrlById(balloonchatwnd::kIdInputSplitter))
            : NULL;
        if (sp != NULL)
        {
            RECT rc = sp->GetRect();
            int track = (rc.bottom - rc.top) - sp->GetBarThickness();
            if (track > 0)
            {
                balloonchatwnd::RememberChatInputHeightPx(track - sp->GetSplitPx());
            }
        }
        return 0;
    }

    // 其它控件的通知（工具栏 / 头部按钮的点击等）本阶段不处理。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonChatWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam,
                                    LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == (WPARAM)kLinkOpenTimerId)
    {
        // 一次性定时器：双击间隔内没等到双击，这次点击算数，真的打开链接。
        ::KillTimer(m_hWnd, kLinkOpenTimerId);
        OpenPendingLink();
        return 0;
    }

    if (wParam == (WPARAM)kRecallEditTimerId)
    {
        // 撤回提示行的「编辑」到期：去掉已到期的，再按下一个到期时刻重新定时（没有了就停掉）
        RefreshRecallEditTimer();
        return 0;
    }

    if (wParam == (WPARAM)kSecretInviteTimerId)
    {
        // 密聊邀请卡片的倒计时：刷新剩余秒数，过期太久仍没有结果的卡片由本机收尾
        OnSecretInviteTick();
        return 0;
    }

    if (wParam == (WPARAM)kReplyTimerId)
    {
        // 一次性定时器：先关掉，再追加一条 mock 对方回复。
        ::KillTimer(m_hWnd, kReplyTimerId);

        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* list =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kIdMessageArea));
            if (list != NULL)
            {
                list->AppendMessage(balloonchatwnd::MakeTextMessage(
                    false, balloonchatwnd::MockAutoReply(m_replyIndex)));
                ++m_replyIndex;
            }
        }
        return 0;
    }

    if (wParam == (WPARAM)kTypingTimerId)
    {
        const unsigned long long now = ::GetTickCount64();

        // 本端：停手满 kTypingIdleMs 就给对端补一包 state=0。
        SendTypingAction(m_typingThrottle.OnTick(now));

        // 对端：满 kTypingExpireMs 还没等到新推送，说明对方多半掉线了（state=0 永远
        // 不会来），自动把提示收起。这是整条链路唯一的兜底，去掉的话那句"正在输入…"
        // 会一直挂在头部。
        if (m_peerTyping.OnTick(now))
        {
            LOG_INFO("对端正在输入提示已过期，自动收起: peer=%u.", m_peerUserId);
            RefreshTypingHeader();
        }
        return 0;
    }

    if (wParam == (WPARAM)kShakeTimerId)
    {
        // 抖动动画：把窗口移到 起点 + 当前步偏移；走完序列后停表并归位。
        if (m_shakeStep < 0 || m_shakeStep >= kShakeStepCount)
        {
            ::KillTimer(m_hWnd, kShakeTimerId);
            m_shakeStep = -1;
            return 0;
        }
        int dx = kShakeOffsets[m_shakeStep][0];
        int dy = kShakeOffsets[m_shakeStep][1];
        ::SetWindowPos(m_hWnd, NULL,
                       m_shakeOrigin.x + dx, m_shakeOrigin.y + dy, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ++m_shakeStep;
        if (m_shakeStep >= kShakeStepCount)
        {
            // 最后一步偏移已是 (0,0)（归位），停表。
            ::KillTimer(m_hWnd, kShakeTimerId);
            m_shakeStep = -1;
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

// 被引用的原图是否已经在本地图片缓存里？是就返回它的完整路径，否则空串。
// 引用块据此决定画缩略图还是画占位块。
//
// <u>只查缓存、不发起下载</u>：引用块指向的是另一条消息的图，那条消息未必在已加载
// 范围内，图片气泡那套"按 msgid 回填路径并刷新那一行"在这里用不上。所以效果是
// "引用刚聊过的图有缩略图，引用很早以前的图只有占位块"——完整的下载链路留待后续。
static CString ResolveQuoteThumbPath(long long selfUserId, const net::ReplyQuote& q,
                                     balloonchatwnd::ChatMessageList* list)
{
    // 本函数对每条要渲染的消息都会调用，而绝大多数消息根本没有引用、或引用的不是图片。
    // 这两种情况没有缩略图可找，直接返回，也不打日志 —— 原先不分情况地打一条"缺少定位
    // 信息"，渲染一页历史就是几十条无用日志。
    if (!q.m_valid || q.m_msgid == 0 || q.m_msgtype != kMsgTypeImage)
    {
        return CString();
    }
    if (selfUserId == 0 || q.m_md5.empty())
    {
        // 这里以前是静默返回，导致缩略图空白时日志里一条线索都没有，只能靠推理逐个
        // 排除。md5 为空说明发送方没把原图信息写进 quote（老消息、或发送侧漏填）。
        LOG_INFO("quote image has no locating info, drawing placeholder: selfUserId=%lld, "
                 "md5 empty=%d.", (long long)selfUserId, q.m_md5.empty() ? 1 : 0);
        return CString();
    }
    const std::wstring cached =
        net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), q.m_md5);
    // CacheFilePathForMd5 只拼路径、不判断存在，得自己确认文件真在。
    if (cached.empty() || ::GetFileAttributesW(cached.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        // 本端还没下过这张图，引用块画占位块。这是只查缓存不下载的预期结果；
        // 记一条日志便于分辨两种都表现为空白的情形：路径没找到 vs 解码还没回来。
        // 缓存没命中不代表本端没有这张图：<u>自己发出去的图从来没被下载过</u>，
        // 它还在当初选的那个原始路径上，而缓存目录只存下载来的图。所以再退一步 ——
        // 去列表里找那条原消息，直接用它气泡上已经解析好的本地路径。
        balloonchatwnd::ChatMessage src;
        if (list != NULL && list->GetMessageByMsgid(q.m_msgid, src)
            && !src.imagePath.IsEmpty()
            && ::GetFileAttributesW((LPCWSTR)src.imagePath) != INVALID_FILE_ATTRIBUTES)
        {
            LOG_INFO("quote image: using the local path of the original bubble, msgid=%lld.", (long long)q.m_msgid);
            return src.imagePath;
        }
        LOG_INFO("quote image: not in local cache and original message not in list, drawing placeholder, md5=%s.", q.m_md5.c_str());
        return CString();
    }
    LOG_INFO("quote image: thumbnail found in local cache, md5=%s.", q.m_md5.c_str());
    return CString(cached.c_str());
}

balloonchatwnd::ChatMessage CBalloonChatWindow::MakeRealTextMessage(bool fromMe, const CString& text,
                                                                    const net::TextFont& font,
                                                                    long long msgid,
                                                                    long long timestampMs,
                                                                    const std::string& clientMsgId,
                                                                    const net::ReplyQuote& quote)
{
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(fromMe, text);
    msg.font = font;   // 自己发：当前字体；收到 / 历史：该条消息携带的字体

    // 撤回判定所需：服务器 msgid 与消息时间。本端刚发出时两者为 0，回执到达后由
    // ChatMessageList::BackfillSentMessage 按 clientMsgId 找到本行补上。
    msg.msgid       = msgid;
    msg.timestampMs = timestampMs;
    msg.clientMsgId = clientMsgId;

    // 气泡头像按各自 (facetype, customface) 解析：字形 / 自动头像画字形 + 底色，真图头像
    // 缓存命中画真图、未命中入队下载（完成后 OnAvatarDownloaded 回刷本气泡）。
    FillSenderAvatar(msg, fromMe);

    // 引用回复：本端乐观气泡、实时推送、历史渲染三条路都经本工厂，故只在这里填一次
    // 就够 —— 不像群聊窗那样每条路各填一遍，也就没有跑偏的机会。
    const long long selfForQuote = (m_lpFMGClient != NULL)
        ? (long long)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 本函数没有现成的 list 变量（它是纯构造消息结构的工厂），就地取一次 —— 引用图片
    // 缓存没命中时要回退去找原消息气泡上的本地路径。
    balloonwjui::DuiControl* quoteContent = GetClientContent();
    balloonchatwnd::ChatMessageList* quoteList = (quoteContent != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              quoteContent->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    balloonchatwnd::FillQuoteForRender(quote, msg,
                                       ResolveQuoteThumbPath(selfForQuote, quote, quoteList));
    return msg;
}

void CBalloonChatWindow::FillSenderAvatar(balloonchatwnd::ChatMessage& msg, bool fromMe)
{
    int64_t     selfId = (m_lpFMGClient != NULL)
                         ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    int64_t     userId;
    int         facetype;
    std::string customface;
    CString     fallbackName;

    if (fromMe)
    {
        userId   = selfId;
        facetype = 0;
        if (m_lpFMGClient != NULL)
        {
            const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
            facetype = (int)me.m_nFace;   // v2 facetype 存在 m_nFace
            customface.assign(me.m_strCustomFaceName.GetString(),
                              (size_t)me.m_strCustomFaceName.GetLength());   // customface UTF-8 字节
            fallbackName = me.m_strNickName.c_str();
        }
        if (fallbackName.IsEmpty())
        {
            fallbackName = TR(L"我");
        }
    }
    else
    {
        userId       = (int64_t)m_peerUserId;
        facetype     = m_peerFacetype;
        customface   = m_peerCustomface;
        // 对端昵称优先用打开方传入的 m_peerName，回退到窗口标题。
        fallbackName = !m_peerName.IsEmpty() ? m_peerName : GetTitle();
        if (fallbackName.IsEmpty())
        {
            fallbackName = _T("?");
        }
    }

    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        userId, facetype, customface, (LPCTSTR)fallbackName, selfId);

    // 只覆盖头像（单聊气泡不显示发送者名，故不设 senderName）。
    msg.senderUserId         = userId;
    msg.senderAvatarText     = ar.m_glyph;
    msg.senderAvatarColor    = ar.m_color;
    msg.senderAvatarImagePath = ar.m_imagePath;
}

void CBalloonChatWindow::OnAvatarDownloaded(const std::string& fileid)
{
    if (fileid.empty())
    {
        return;
    }
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    if (selfId <= 0)
    {
        return;
    }

    // 真图缓存路径（刚下好的）。取不到（异常）则不刷。
    std::wstring path =
        net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), fileid);
    if (path.empty() || ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    CString imagePath = path.c_str();

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;

    // 对端真图头像命中：重建标题栏 / 任务栏图标（现可画真图）+ 刷新对端气泡头像。
    if (m_peerFacetype == balloonmain::avatar_kind_image && m_peerCustomface == fileid)
    {
        RebuildPeerIcons();
        if (list != NULL)
        {
            list->RefreshSenderAvatar((int64_t)m_peerUserId, imagePath);
        }
    }

    // 本人真图头像命中：刷新本人气泡头像（自己的头像通常本地已缓存，少经此下载路径）。
    if (m_lpFMGClient != NULL && list != NULL)
    {
        const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
        std::string myFace(me.m_strCustomFaceName.GetString(),
                           (size_t)me.m_strCustomFaceName.GetLength());
        if ((int)me.m_nFace == balloonmain::avatar_kind_image && myFace == fileid)
        {
            list->RefreshSenderAvatar(selfId, imagePath);
        }
    }
}

LRESULT CBalloonChatWindow::OnSendOnEnter(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 输入框按 Enter：等同点「发送」按钮。Ctrl+Enter 的换行已由过滤器在框内
    // 就地处理，不会走到这里。
    DoSend();
    return 0;
}

LRESULT CBalloonChatWindow::OnDoScreenshot(UINT /*uMsg*/, WPARAM wParam,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 已脱离 DUI 鼠标事件派发栈，安全地发起模态截图浮层或弹出菜单。
    switch ((int)wParam)
    {
    //点了剪刀右边的 ▾：弹出截图选项菜单
    case balloonchatwnd::kShotReqMenu:
        OpenScreenshotMenu();
        break;
    //菜单选了「截取全屏」：选区直接为鼠标所在的整块显示器
    case balloonchatwnd::kShotReqFullScreen:
        DoScreenshot(true);
        break;
    //点了剪刀、菜单选了「截图」，或主面板转来的截图快捷键
    case balloonchatwnd::kShotReqCapture:
    default:
        DoScreenshot(false);
        break;
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnOpenRecordMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbRecord);
    if (btn == NULL)
    {
        return 0;
    }
    // 菜单贴在录制按钮下方弹出
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    const RECT anchor = { tl.x, tl.y, br.x, br.y };
    balloonscreenshare::CScreenShareHub& hub = balloonscreenshare::CScreenShareHub::GetInstance();
    const balloonchatwnd::RecordMenuChoice choice =
        balloonchatwnd::TrackRecordMenu(m_hWnd, anchor, hub.IsRecording(), hub.IsModuleReady());
    switch (choice)
    {
    //开始录制：弹出「选择要录制的内容」，录完可一键发到本会话
    case balloonchatwnd::kRecordMenuStart:
        hub.StartRecording(kConvTypeSingle, (int64_t)m_peerUserId, m_peerName);
        break;
    //停止并保存正在进行的录制
    case balloonchatwnd::kRecordMenuStop:
        hub.StopRecording();
        break;
    //打开录制管理窗口（录像列表，可在那里编辑字幕）
    case balloonchatwnd::kRecordMenuManager:
        hub.ShowRecordManager();
        break;
    //没有选
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnOpenSendMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdSendButton);
    if (btn == NULL)
    {
        return 0;
    }

    // 取发送按钮左上角的屏幕坐标作锚点。
    RECT rc = btn->GetRect();
    POINT anchor = { rc.left, rc.top };
    ::ClientToScreen(m_hWnd, &anchor);

    // 快捷回复（docs/快捷回复方案.md）：三条按本人账号配置取，没设过的是默认文字，特意清空的为空串、不出菜单项
    std::vector<CString> replies;
    if (m_lpFMGClient != NULL)
    {
        for (int i = 0; i < CUserConfig::kQuickReplySlotCount; ++i)
        {
            replies.push_back(m_lpFMGClient->m_UserConfig.GetQuickReply(i));
        }
    }
    const balloonchatwnd::ChatSendMenuCommand cmd = balloonchatwnd::PopupChatSendMenu(
        m_hWnd, anchor, replies, balloonchatwnd::GetChatSendMode() == balloonchatwnd::kSendOnEnter);
    switch (cmd.m_kind)
    {
    //点了一条快捷回复：直接发出去，输入框里的文字、待发的引用都不动
    case balloonchatwnd::kSendMenuQuickReply:
        if (cmd.m_replyIndex >= 0 && cmd.m_replyIndex < (int)replies.size())
        {
            SendQuickReply(replies[cmd.m_replyIndex]);
        }
        break;

    //改为按 Enter 发送
    case balloonchatwnd::kSendMenuEnterKey:
        balloonchatwnd::SetChatSendMode(balloonchatwnd::kSendOnEnter);
        RefreshSendModeUi();
        break;

    //改为按 Ctrl+Enter 发送
    case balloonchatwnd::kSendMenuCtrlEnterKey:
        balloonchatwnd::SetChatSendMode(balloonchatwnd::kSendOnCtrlEnter);
        RefreshSendModeUi();
        break;

    //「设置快捷回复…」：系统设置窗由主面板持有，请它打开并切到「聊天」页
    case balloonchatwnd::kSendMenuSettings:
        if (::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_SETTINGS_PAGE, (WPARAM)balloonsettings::kPageChat, 0);
        }
        break;

    //菜单被取消
    default:
        break;
    }
    return 0;
}

void CBalloonChatWindow::SendQuickReply(const CString& reply)
{
    //与按发送键发消息同一道在线检查：离线时给出相同的提示，不发送
    if (!CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return;
    }
    const CString body = balloonchatwnd::QuickReplyBody(reply);
    if (body.IsEmpty())
    {
        return;
    }
    // 密聊中：快捷回复也是文字，按密聊文字发出；只能查看时不发
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        if (m_secretState == balloonchatwnd::kSecretStateActive)
        {
            SendSecretTextPart(body);
        }
        return;
    }
    LOG_INFO("chat window: quick reply sent, to=%u, chars=%d.", m_peerUserId, reply.GetLength());
    //SendPlainText 不带引用、不读也不清输入框
    SendPlainText(std::wstring((LPCWSTR)body));
}

void CBalloonChatWindow::RefreshSendModeUi()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::SendKeyMode mode = balloonchatwnd::GetChatSendMode();

    // 左下角提示文字随模式联动。
    balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kIdHintLabel));
    if (hint != NULL)
    {
        hint->SetText(balloonchatwnd::GetChatSendHintText(mode));
    }

    // 输入框 placeholder 随模式联动（避免空框时提示与实际行为矛盾）。
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input != NULL)
    {
        input->SetPlaceholder(balloonchatwnd::GetChatSendPlaceholder(mode));
    }
}

// ---- 输入框图文混排发送用的本地辅助 -----------------------------------

// EnumContent 的逐段收集回调：把（文本 / 内嵌图+标记）按文档顺序
// 收进 vector，供 BuildOutgoingParts 聚合成待发送部分。ctx 为收集目标 vector。
static void CollectInputSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
{
    std::vector<balloonchatwnd::RichInputSegment>* segs =
        (std::vector<balloonchatwnd::RichInputSegment>*)ctx;
    if (segs == NULL)
    {
        return;
    }
    balloonchatwnd::RichInputSegment seg;
    seg.isImage = isImage;
    seg.text = (text != NULL) ? text : _T("");
    seg.tag = tag;
    segs->push_back(seg);
}

void CBalloonChatWindow::SendPlainText(const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    // 密聊中不从其它入口（屏幕共享的邀请链接等）发普通消息：它会混进密聊列表，也会被当成普通记录留下
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        ShowToast(balloonchatwnd::SecretBlockedSendTip().c_str());
        LOG_INFO("plain text blocked in secret mode, peer=%u.", m_peerUserId);
        return;
    }
    SendTextPartReal(CString(text.c_str()), false);
}

void CBalloonChatWindow::SendTextPartReal(const CString& text, bool withQuote)
{
    // 真实会话的一段文字：乐观回显气泡 + 经网络发文本消息 + 冒泡会话。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_peerUserId == 0)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //先生成 clientMsgId 再建气泡：气泡要带上它，回执到达后才能按它找回本行、
    //把服务器 msgid 与时间戳回填进去（撤回菜单的时限判断依赖这两个值）。
    std::string clientMsgId = GenClientMsgId();
    //不带引用时用一个空引用（SendPlainText，屏幕共享第 25 期）
    const net::ReplyQuote noQuote;
    const net::ReplyQuote& quote = withQuote ? m_pendingQuote : noQuote;
    //本端乐观气泡也要显示引用块 —— 否则"发出去自己看不到引用、对方却看得到"。
    balloonchatwnd::ChatMessage echo = MakeRealTextMessage(true, text, m_currentFont, 0, 0,
                                                           clientMsgId, quote);
    list->AppendMessage(echo);

    std::string utf8 = CStringToUtf8(text);
    CIUSocket::GetInstance().SendChatTextMessage((int64_t)m_peerUserId, clientMsgId,
                                                 utf8, m_currentFont, quote);

    //引用是"一次性"的：带出去之后就清掉并收起引用条，
    //否则后面每条消息都会带同一个引用。没带引用时引用条保持原样
    if (withQuote)
    {
        CancelPendingReply();
    }
    LOG_INFO("单聊窗发送一段文字: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。
    {
        ConvSendBumpData bump;
        bump.ctype   = 1;
        bump.cid     = (long long)m_peerUserId;
        bump.msgType = kMsgTypeText;
        // 会话列表摘要：内联表情标记替换成 [表情]、转义斜杠还原，不暴露 /f["id"]。
        bump.digest  = CStringToUtf8(balloonchat::BodyToDigest(text));
        m_sendBumps.Add(clientMsgId, bump);
    }
}

bool CBalloonChatWindow::SendMarkdownDocument(const std::string& sourceUtf8)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_peerUserId == 0 || sourceUtf8.empty())
    {
        return false;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return false;
    }
    //密聊中不从 Markdown 编辑器发普通消息到本会话，理由同 SendPlainText
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        ShowToast(balloonchatwnd::SecretBlockedSendTip().c_str());
        LOG_INFO("markdown document blocked in secret mode, peer=%u.", m_peerUserId);
        return false;
    }
    //源文本按聊天正文的口径转义（字面的 /f[" 写成 //f["），接收方用 MarkdownSourceFromBody 还原
    const std::string bodyUtf8 = balloonmd::MarkdownBodyFromSourceUtf8(sourceUtf8);
    const std::string clientMsgId = GenClientMsgId();
    //乐观气泡：Markdown 消息显示为卡片；不带引用，字体用默认值（卡片不受聊天字体影响）
    balloonchatwnd::ChatMessage echo = MakeRealTextMessage(true, Utf8ToCString(bodyUtf8), net::TextFont(), 0, 0,
                                                           clientMsgId, net::ReplyQuote());
    echo.isMarkdown = true;
    list->AppendMessage(echo);
    CIUSocket::GetInstance().SendChatTextMessage((int64_t)m_peerUserId, clientMsgId, bodyUtf8, net::TextFont(),
                                                 net::ReplyQuote(), net::text_format_markdown);
    //窗口刚打开、首屏历史还没渲染时，上面插入的卡片会在首屏渲染时被清掉，记下来以便之后重新拉取
    m_mdEarlySends.OnSent(clientMsgId);
    LOG_INFO("chat window: markdown document sent, to=%u, clientmsgid=%s, bytes=%u, early=%u.", m_peerUserId,
             clientMsgId.c_str(), (unsigned)bodyUtf8.size(), (unsigned)m_mdEarlySends.PendingCount());

    // 登记待冒泡：回执成功后主面板把会话冒泡到顶部，预览文字为 Markdown 摘要
    ConvSendBumpData bump;
    bump.ctype   = 1;
    bump.cid     = (long long)m_peerUserId;
    bump.msgType = kMsgTypeText;
    bump.digest  = balloonchatwnd::MarkdownDigestFromUtf8Body(bodyUtf8);
    m_sendBumps.Add(clientMsgId, bump);
    return true;
}

void CBalloonChatWindow::OpenMarkdownEditor(const std::string& sourceUtf8)
{
    balloonmd::MarkdownSendTarget target;
    target.m_isGroup = false;
    target.m_id = (int64_t)m_peerUserId;
    target.m_name = std::wstring((LPCWSTR)m_peerName);
    balloonmd::CMarkdownEditorManager::GetInstance().OpenEditor(sourceUtf8, (m_peerUserId != 0) ? &target : NULL);
    LOG_INFO("chat window: open markdown editor, peer=%u, bytes=%u.", m_peerUserId, (unsigned)sourceUtf8.size());
}

void CBalloonChatWindow::OpenCodeCompose(const std::wstring& initialCode, const std::string& langId,
                                         const std::vector<codesnippet::CodeNote>& notes)
{
    std::wstring code = initialCode;
    // 没有指定预填内容时，取输入框里选中的文字（RichEdit 的字符位置与 GetText 的下标一致）
    if (code.empty())
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatInputRichEdit* input = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kIdInputEdit))
            : NULL;
        if (input != NULL)
        {
            long selBegin = 0;
            long selEnd = 0;
            input->GetSel(selBegin, selEnd);
            const CString all = input->GetText();
            if (selEnd > selBegin && selEnd <= all.GetLength())
            {
                code = std::wstring((LPCWSTR)all.Mid(selBegin, selEnd - selBegin));
            }
        }
    }
    if (!m_codeComposeWnd)
    {
        m_codeComposeWnd.reset(new ballooncode::CCodeComposeWindow());
    }
    m_codeComposeWnd->Open(m_hWnd, code, langId, notes);
}

std::string CBalloonChatWindow::RememberedCodeLang() const
{
    return (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserConfig.GetCodeSnippetLang() : std::string();
}

long long CBalloonChatWindow::SelfUserIdForCode() const
{
    return (m_lpFMGClient != NULL) ? (long long)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
}

std::map<long long, std::wstring> CBalloonChatWindow::CodeNoteAuthorNames(
    const std::vector<codesnippet::CodeNote>& /*notes*/, long long /*baseSenderId*/) const
{
    // 单聊里批注的作者不是本人就是对方；其余（转发来的批注）卡片上写 userid
    std::map<long long, std::wstring> names;
    const long long selfId = SelfUserIdForCode();
    if (selfId != 0 && m_lpFMGClient != NULL && !m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.empty())
    {
        names[selfId] = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName;
    }
    if (m_peerUserId != 0 && !m_peerName.IsEmpty())
    {
        names[(long long)m_peerUserId] = std::wstring((LPCWSTR)m_peerName);
    }
    return names;
}

net::ReplyQuote CBalloonChatWindow::CodeBaseQuote(long long baseMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage src;
    if (list == NULL || !list->GetMessageByMsgid(baseMsgId, src))
    {
        LOG_INFO("chat window: base code snippet %lld is not in the list, the annotation is sent without a quote.",
                 baseMsgId);
        return net::ReplyQuote();
    }
    // 与「引用回复」同一口径：自己发的取本端昵称，对方发的取对端昵称，都空时回退成「用户<id>」
    CString senderName;
    if (src.fromMe)
    {
        if (m_lpFMGClient != NULL)
        {
            senderName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
        }
    }
    else
    {
        senderName = m_peerName;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(TR(L"用户%u"), src.fromMe ? 0u : m_peerUserId);
    }
    return balloonchatwnd::BuildQuoteFromMessage(src, senderName);
}

void CBalloonChatWindow::ShowCodeViewer(const std::wstring& code, int lang, const std::vector<codesnippet::CodeNote>& notes,
                        const std::string& key, long long msgid, long long senderId, bool fromMe, bool openNotesPane,
                        const std::vector<codesnippet::CodeNote>& editedNotes)
{
    ballooncode::CodeViewerExtras extras;
    extras.m_canAnnotate = (msgid > 0);
    extras.m_msgid = msgid;
    extras.m_senderId = senderId;
    extras.m_fromMe = fromMe;
    extras.m_selfId = SelfUserIdForCode();
    extras.m_authorNames = CodeNoteAuthorNames(notes, senderId);
    std::map<long long, std::wstring>::const_iterator it = extras.m_authorNames.find(senderId);
    extras.m_senderName = (it != extras.m_authorNames.end()) ? it->second : std::wstring();
    extras.m_editedNotes = editedNotes;
    extras.m_openNotesPane = openNotesPane;
    ballooncode::CCodeViewerWindow::Show(m_hWnd, code, lang, key, codesnippet::FillCodeNoteAuthors(notes, senderId),
                                         extras);
    LOG_INFO("chat window: code viewer requested, key=%s, msgid=%lld, sender=%lld, notes=%u, edited=%u, open notes=%d.",
             key.c_str(), msgid, senderId, (unsigned)notes.size(), (unsigned)editedNotes.size(), openNotesPane ? 1 : 0);
}

LRESULT CBalloonChatWindow::OnCodeComposeSend(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    ballooncode::CodeComposeResult* pResult = (ballooncode::CodeComposeResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }
    // 手选的语言记下来，下次打开发送代码窗口时默认选中它
    if (pResult->m_manualLang && m_lpFMGClient != NULL)
    {
        m_lpFMGClient->m_UserConfig.SetCodeSnippetLang(codesnippet::CodeLanguageId(pResult->m_lang));
    }
    // 追加批注（docs/代码片段方案.md 第 11.3 节）：引用原消息发出；没能发出时回到批注模式并放回本次的批注
    if (pResult->m_annotate.m_baseMsgId > 0)
    {
        const net::ReplyQuote quote = CodeBaseQuote(pResult->m_annotate.m_baseMsgId);
        if (!SendCodeSnippet(pResult->m_code, pResult->m_lang, pResult->m_notes, &quote))
        {
            ShowCodeViewer(pResult->m_code, pResult->m_lang, pResult->m_originalNotes,
                           std::to_string(pResult->m_annotate.m_baseMsgId), pResult->m_annotate.m_baseMsgId,
                           pResult->m_annotate.m_baseSenderId, pResult->m_annotate.m_baseSenderId == SelfUserIdForCode(),
                           true, pResult->m_notes);
        }
        delete pResult;
        return 0;
    }
    // 没有发出（离线、超限）时重新打开发送代码窗口并放回这段代码与批注，用户写的内容不丢
    if (!SendCodeSnippet(pResult->m_code, pResult->m_lang, pResult->m_notes))
    {
        OpenCodeCompose(pResult->m_code, codesnippet::CodeLanguageId(pResult->m_lang), pResult->m_notes);
    }
    delete pResult;
    return 0;
}

bool CBalloonChatWindow::SendCodeSnippet(const std::wstring& code, int lang,
                                         const std::vector<codesnippet::CodeNote>& notes,
                                         const net::ReplyQuote* quoteOverride)
{
    // 带上的引用：追加批注时是原消息，平常是输入框里待发的引用
    const net::ReplyQuote& quote = (quoteOverride != NULL) ? *quoteOverride : m_pendingQuote;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL || code.empty())
    {
        return false;
    }
    // 密聊中不发代码片段（「发送代码」窗口可能是进入密聊之前打开的），理由同 SendPlainText
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        ShowToast(balloonchatwnd::SecretBlockedSendTip().c_str());
        LOG_INFO("code snippet blocked in secret mode, peer=%u.", m_peerUserId);
        return false;
    }
    // 离线 / 连接未就绪时不发：消息到不了服务器，气泡却会留在窗口里（调试入口没有真实会话，不检查）
    if (!(m_peerUserId == 0) && !CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return false;
    }
    const CString text(code.c_str());
    const std::string utf8 = CStringToUtf8(text);
    const std::string langId = codesnippet::CodeLanguageId(lang);
    // 作者是本人的批注不写 userid（缺省即为发送者，docs/代码片段方案.md 第 11.4 节）
    const std::vector<net::CodeNoteData> noteData = balloonchatwnd::CodeNotesToData(notes, SelfUserIdForCode());
    // 发送前检查体积（待发的引用与批注一并计入）：超过单条消息上限的服务端会拒收，整条不发
    if (balloonchatwnd::IsCodeTooLarge(utf8, langId, quote, noteData))
    {
        LOG_WARNING("chat window: code snippet is too large to send, peer=%u, bytes=%u, notes=%u.", m_peerUserId,
                    (unsigned)utf8.size(), (unsigned)notes.size());
        ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return false;
    }
    // 调试入口（没有真实会话）：只在本地显示
    if (m_peerUserId == 0)
    {
        balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, text);
        echo.isCode = true;
        echo.codeLang = lang;
        echo.codeNotes = notes;
        list->AppendMessage(echo);
        return true;
    }

    // 与 SendTextPartReal 同一条路径：先生成 clientMsgId 再建乐观气泡，回执到达后按它回填 msgid 与时间
    const std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage echo = MakeRealTextMessage(true, text, net::TextFont(), 0, 0, clientMsgId,
                                                           quote);
    echo.isCode = true;
    echo.codeLang = lang;
    echo.codeNotes = notes;
    list->AppendMessage(echo);
    CIUSocket::GetInstance().SendChatTextMessage((int64_t)m_peerUserId, clientMsgId, utf8, net::TextFont(),
                                                 quote, net::text_format_code, langId, noteData);
    // 引用是一次性的：带出去之后清掉并收起引用条。追加批注时引用的是原消息，输入框里待发的引用保持不动
    if (quoteOverride == NULL)
    {
        CancelPendingReply();
    }
    LOG_INFO("chat window: code snippet sent, to=%u, clientmsgid=%s, lang=%s, bytes=%u, notes=%u.", m_peerUserId,
             clientMsgId.c_str(), langId.c_str(), (unsigned)utf8.size(), (unsigned)notes.size());

    // 登记待冒泡：回执成功后主面板把会话冒泡到顶部，摘要写「[代码] 第一行」（追加批注时为「[批注] 说明」）
    ConvSendBumpData bump;
    bump.ctype   = 1;
    bump.cid     = (long long)m_peerUserId;
    bump.msgType = kMsgTypeText;
    bump.digest  = CStringToUtf8(CString(balloonchatwnd::CodeDigestFromCode(code, notes).c_str()));
    m_sendBumps.Add(clientMsgId, bump);
    return true;
}

void CBalloonChatWindow::ShowCodePasteBar(bool show)
{
    if (!show)
    {
        m_codePasteText.clear();
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* bar = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kIdCodePasteBar) : NULL;
    if (bar == NULL || bar->IsVisible() == show)
    {
        return;
    }
    bar->SetVisible(show);
    // SetVisible 不会重排（开发约定文档 UI 约定第 5 条）：对提示条所在的输入区块 ForceLayout，输入框才会让出或收回
    // 这一行；只对根 ForceLayout 不够，输入区块本身的矩形没有变化，SetRect 会直接返回
    balloonwjui::DuiControl* holder = bar->GetParent();
    if (holder != NULL)
    {
        holder->ForceLayout(holder->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

LRESULT CBalloonChatWindow::OnCodePasteNotify(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatInputRichEdit* input = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kIdInputEdit))
        : NULL;
    if (input == NULL)
    {
        return 0;
    }
    const std::wstring text((LPCWSTR)input->GetText());
    // 刚往空输入框里粘贴了文字：像代码时显示提示条，并记下此刻的文字。聊天输入框已没有 Markdown 模式
    //（Markdown 改由独立编辑器发送，docs/Markdown消息方案.md 第 11.2 节），故第二个参数恒为 false
    if (wParam != 0)
    {
        if (balloonchatwnd::ShouldOfferCodePaste(true, false, text))
        {
            ShowCodePasteBar(true);
            m_codePasteText = text;
            LOG_INFO("chat window: pasted text looks like code, offer code snippet, peer=%u, chars=%u.",
                     m_peerUserId, (unsigned)text.size());
        }
        return 0;
    }
    // 内容变了：提示条显示之后用户继续输入、删改或发送了，提示条随即收起。粘贴本身引起的变化通知排在
    // 粘贴通知之前处理，那时提示条还没有显示，不受影响
    if (!m_codePasteText.empty() && text != m_codePasteText)
    {
        ShowCodePasteBar(false);
    }
    return 0;
}

void CBalloonChatWindow::SendImagePartReal(int imageIndex)
{
    // imageIndex 是源文件在 m_pendingShotPaths 里的下标：带标记插入的图从标记读回（见
    // StageImageInInput），未带标记的旧式内嵌图按出现顺序对应。直接发登记下来的原文件——
    // 不从 OLE 对象抽位图（那条路在静态图对象上取到的是黑块），也不重新编码。
    if (imageIndex < 0 || imageIndex >= (int)m_pendingShotPaths.size())
    {
        LOG_WARNING("单聊窗发送：内嵌图序号 %d 无对应源文件（图序与追踪不一致），跳过。",
                    imageIndex);
        return;
    }
    SendImageFile(m_pendingShotPaths[imageIndex]);
}

void CBalloonChatWindow::ApplyConnectionState(int state)
{
    m_connUiState = state;

    // 掉线 / 主动离线：本端的"正在输入"通告静默复位（此刻发 state=0 也送不出去，
    // 且重连后节流器应从干净状态起步）；对端那句提示同样收起 —— 连都断了，界面上
    // 再说人家正在输入只会误导。
    if (state != (int)balloonmain::kConnUiOnline)
    {
        m_typingThrottle.Reset();
        if (m_peerTyping.Clear())
        {
            RefreshTypingHeader();
        }
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等窗口创建完主面板会再推一次
    }

    // 本人的气泡头像随本端连接态变灰 / 恢复彩色
    ApplyAvatarGrayToMessageList();

    const bool bOnline = (state == (int)balloonmain::kConnUiOnline);

    // 顶部提示带：在线时不显示（GetConnectionBannerText 返回 NULL），离线 / 正在连接
    // 时显示对应文字。不可见的子控件不占布局高度，故无需另调整尺寸。
    balloonchatwnd::ChatBannerLabel* banner =
        dynamic_cast<balloonchatwnd::ChatBannerLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdOfflineBanner));
    // 提示带显隐是否真的发生了变化。变化了才需要补一次布局（见本函数末尾）。
    bool bannerVisibilityChanged = false;
    if (banner != NULL)
    {
        const bool wasVisible = banner->IsVisible();
        const wchar_t* text = balloonmain::GetConnectionBannerText(state);
        if (text == NULL)
        {
            banner->SetVisible(false);
        }
        else
        {
            banner->SetText(text);
            // 离线用琥珀色系（"需要你处理"的语气），正在连接用中性灰（只是告知）。
            // 两种都是普通文字、整条不可点 —— 恢复在线走托盘 / 主面板的状态菜单。
            if (state == (int)balloonmain::kConnUiOffline)
            {
                banner->SetBannerColors(kOfflineBannerBgColor,
                                        kOfflineBannerBorderColor);
                banner->SetTextColor(kOfflineBannerColor);
            }
            else
            {
                banner->SetBannerColors(kConnectingBannerBgColor,
                                        kConnectingBannerBorderColor);
                banner->SetTextColor(kConnectingBannerColor);
            }
            banner->SetVisible(true);
        }
        bannerVisibilityChanged = (banner->IsVisible() != wasVisible);
    }

    // 离线时只把发送按钮置灰并不再响应点击；输入框保持可编辑，用户可以继续写草稿，
    // 恢复在线后再发送（2026-10-01 起，此前离线时输入框置为只读）。离线时按回车发送
    // 同样发不出去：DoSend 开头的 CheckOnlineFor 会给出提示并返回，草稿留在输入框里。
    balloonwjui::DuiControl* sendBtn = content->FindCtrlById(balloonchatwnd::kIdSendButton);
    if (sendBtn != NULL)
    {
        //密聊「只能查看」时发送按钮同样不可用（docs/密聊方案.md 4.7）
        sendBtn->SetEnabled(bOnline
                            && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, balloonchatwnd::kIdSendButton));
    }

    // 提示条显隐改变了客户区顶部的占位高度，必须补一次整树布局才生效 ——
    // DuiControl::SetVisible 只 Invalidate 重绘、不重新布局，光设可见的话提示条
    // 拿不到高度，画出来是空的（反过来隐藏它，那条空白也不会自动收掉）。
    // 整树重排会顺带打乱消息区 / 输入区的分割，故随后走一次窗口自己的 relayout，
    // 把输入区块高度重新钉回用户选定值 —— 与窗口缩放后的处理是同一条路。
    if (bannerVisibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
    LOG_INFO("ChatWindow::ApplyConnectionState: peer=%u, state=%d (0=online 1=connecting 2=offline), "
             "banner visibility changed %d.",
        m_peerUserId, state, (int)bannerVisibilityChanged);

    // 密聊中掉线或切到离线：连接一断，会话即在服务端结束，断线后也无法续聊（docs/密聊方案.md 4.3）。
    // 本机清零密钥，转为「只能查看」，内容保留到本人退出
    if (!bOnline && m_secretState == balloonchatwnd::kSecretStateActive)
    {
        balloonsecret::CSecretChatHost::GetInstance().Close(m_secretSessionId);
        EnterSecretReadOnly(balloonchatwnd::kSecretEndLocalOffline);
    }
}

bool CBalloonChatWindow::CheckOnlineFor(int action)
{
    // 门禁只看本窗记下的连接态：它由会话控制器在每次状态变化时推下来，与托盘图标、
    // 状态胶囊同源。判定本身交给 OnlineCapability，不在本窗口就地写一份。
    if (balloonmain::IsActionAllowedInState(m_connUiState, action))
    {
        return true;
    }

    LOG_INFO("ChatWindow::CheckOnlineFor: blocked. peer=%u, action=%d, "
             "connUiState=%d (0=online 1=connecting 2=offline).",
        m_peerUserId, action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    return false;
}

bool CBalloonChatWindow::CheckPeerOnlineForFolder()
{
    // 只在确知对方离线时才拦。m_peerPresence 为 -1 表示状态还没注入进来（窗口刚建、
    // 通讯录应答未到），此时无从判断，放行交给服务端应答兜底 —— 宁可让服务端拒绝
    // 一次，也不要在状态未知时把能发的也挡掉。
    if (m_peerPresence != kPresenceOffline)
    {
        return true;
    }

    LOG_INFO("ChatWindow: folder send blocked, peer=%u is offline.", m_peerUserId);
    ::MessageBox(m_hWnd, TR(kFolderPeerOfflineTip), g_strAppTitle.c_str(),
                 MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonChatWindow::DoSend()
{
    // 离线 / 连接未就绪时不发：消息发出去也到不了服务器，气泡却会留在窗口里，
    // 让用户误以为发出去了。
    if (!CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (input == NULL || list == NULL)
    {
        return;
    }

    // 按文档顺序枚举输入框内容（文本段 / 内嵌图段+标记），聚合成待发送部分序列：
    // 表情（带 faceId 标记的内嵌图）随文字编码进文本部分，截图等非表情图独立成段。
    std::vector<balloonchatwnd::RichInputSegment> segs;
    input->EnumContent(&CollectInputSegment, &segs);
    std::vector<balloonchatwnd::RichOutgoingPart> parts =
        balloonchatwnd::BuildOutgoingParts(segs);
    if (parts.empty())
    {
        // 空 / 全空白，不发。输入框里有内容却聚合不出可发送部分时记一条，以后同类问题有迹可查（BUG-106）
        if (!segs.empty())
        {
            LOG_INFO("ChatWindow::DoSend: nothing to send, %u input segment(s) are blank, peer=%u.",
                     (unsigned)segs.size(), m_peerUserId);
        }
        return;
    }

    // 密聊状态：文字交给密聊模块加密发送，图片读入内存后分块发送（docs/密聊方案.md 4.7、4.10）。不识别代码片段、
    // 不带引用、不经 SendChatTextMessage、不登记会话冒泡。只能查看时输入框只读、发送按钮不可用，走到这里也不发。
    // 某一段没发出去时停在那里，输入框保留原样，方便用户处理后再发
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        if (m_secretState != balloonchatwnd::kSecretStateActive)
        {
            return;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const bool sent = (parts[i].kind == balloonchatwnd::kRichPartText)
                              ? SendSecretTextPart(parts[i].text)
                              : SendSecretImagePart(parts[i].imageIndex);
            if (!sent)
            {
                return;
            }
        }
        input->SetText(_T(""));
        m_pendingShotPaths.clear();
        return;
    }

    bool hasImage = false;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartImage)
        {
            hasImage = true;
            break;
        }
    }

    // 整条消息以 ``` 包起来时按代码片段发送（docs/代码片段方案.md 第 3.1 节）：只有一段文字、没有图片时才判断。
    // 聊天输入框已没有 Markdown 模式（docs/Markdown消息方案.md 第 11.2 节），故第二个参数恒为 false。
    // 没有发出（离线、超限）时输入框原样保留
    if (!hasImage && parts.size() == 1)
    {
        std::wstring fencedCode;
        int fencedLang = codesnippet::code_lang_plain;
        if (balloonchatwnd::TryCodeFenceSend(std::wstring((LPCWSTR)parts[0].text), false, fencedCode, fencedLang))
        {
            if (SendCodeSnippet(fencedCode, fencedLang))
            {
                input->SetText(_T(""));
                StopTypingNotify();
            }
            return;
        }
    }

    // 调试入口（--chatwindow，无对端绑定）：不支持发图。把各文字段拼起来做 mock 回显
    // 与自动回复，保留离线核对体验。
    if (m_peerUserId == 0)
    {
        CString joined;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i].kind == balloonchatwnd::kRichPartText)
            {
                joined += parts[i].text;
            }
        }
        joined.Trim();
        input->SetText(_T(""));
        if (joined.IsEmpty())
        {
            return;
        }
        balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, joined);
        echo.font = m_currentFont;
        list->AppendMessage(echo);
        ::SetTimer(m_hWnd, kReplyTimerId, kReplyDelayMs, NULL);
        //调试入口没有对端可通告，这里只是把节流器复位，不会真的发包。
        StopTypingNotify();
        return;
    }

    // 发送前检查每段文字的体积：超过单条消息上限的，服务端会拒收。
    // 任何一段超限就整条不发、不生成气泡，输入框原样保留，方便用户删减后再发。单聊没有 @。
    const int oversized = balloonchatwnd::FindOversizedTextPart(
        parts, m_currentFont, m_pendingQuote, std::vector<balloonchatwnd::MentionCandidate>());
    if (oversized >= 0)
    {
        LOG_WARNING("chat window: text part %d is too large to send, peer=%u.", oversized, m_peerUserId);
        ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    // 真实会话：按文档顺序逐段发出（文字段→文字消息，内嵌图→图片消息）。各段乐观
    // 气泡按序追加。注意：发图要从 RichEdit 抽内嵌对象，故须在全部抽取 / 发送完成<u>之后</u>
    // 才清空输入框（提前清空会把内嵌图一起抹掉、抽取失败）。
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartText)
        {
            SendTextPartReal(parts[i].text);
        }
        else
        {
            SendImagePartReal(parts[i].imageIndex);
        }
    }
    input->SetText(_T(""));
    m_pendingShotPaths.clear();   // 内嵌图已发完，清空源文件追踪

    //消息都发出去了，"正在输入"自然该收尾：立即补一包 state=0，别让对端那句提示
    //干等到过期才消失。
    StopTypingNotify();
}

void CBalloonChatWindow::PickAndSendImage()
{
    // 图片要先上传到图片服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无对端绑定）不支持真实发图。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 弹文件选择对话框选一张图片。
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"图片文件"), filedlg::kImagePatterns, TR(L"所有文件"), L"*.*");
    ofn.lpstrFilter = fileFilter.c_str();
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn))
    {
        return;   // 用户取消选择
    }
    SendImageFile(szFile);
}

void CBalloonChatWindow::SendImageFile(const std::wstring& path)
{
    // 真实会话才发图（调试入口无对端绑定）。选图发送与截图「发送」共用本函数。
    if (m_peerUserId == 0)
    {
        return;
    }
    // 密聊状态：工具栏「图片」选的图改为密聊图片发送（读入内存、分块加密，不上传图片服务器）；只能查看时不发
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        if (m_secretState == balloonchatwnd::kSecretStateActive)
        {
            SendSecretImageFile(path);
        }
        return;
    }

    // 读图片真实宽高以算气泡内展示尺寸；解码失败则用占位尺寸（上传仍按字节流走）。
    int realW = 0;
    int realH = 0;
    {
        Gdiplus::Image img(path.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok)
        {
            realW = (int)img.GetWidth();
            realH = (int)img.GetHeight();
        }
    }
    int dispW = 0;
    int dispH = 0;
    ComputeImageDisplaySize(realW, realH, dispW, dispH);

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"的图片气泡（显示本地图），再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        true, CString(path.c_str()), dispW, dispH, clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);

    // 交图片传输工作线程：申请上传→传字节→发图片消息。进度 / 结果经
    // FMG_MSG_V2_IMAGE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::ImageTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, clientMsgId, path);
    LOG_INFO("单聊窗发图: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。
    {
        ConvSendBumpData bump;
        bump.ctype   = 1;
        bump.cid     = (long long)m_peerUserId;
        bump.msgType = kMsgTypeImage;
        bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeImage, std::string());
        m_sendBumps.Add(clientMsgId, bump);
    }
}

void CBalloonChatWindow::SendLocalImage(const std::wstring& path)
{
    LOG_INFO("chat: send local image requested by host, peer=%u.", m_peerUserId);
    SendImageFile(path);
}

void CBalloonChatWindow::SendLocalFile(const std::wstring& path, bool forceOffline)
{
    //与「发送文件」按钮同一条路：离线时两步都做不了，调试入口没有对端
    if (!CheckOnlineFor(balloonmain::kActionTransferFile) || m_peerUserId == 0 || path.empty())
    {
        return;
    }
    // 密聊中不发普通文件（录屏完成后的「发送给 某某」等），理由同 SendPlainText
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        ShowToast(balloonchatwnd::SecretBlockedSendTip().c_str());
        LOG_INFO("local file blocked in secret mode, peer=%u.", m_peerUserId);
        return;
    }
    LOG_INFO("chat window: send local file requested by host, peer=%u, forceoffline=%d.", m_peerUserId,
             forceOffline ? 1 : 0);
    SendFileData(path, forceOffline);
}

void CBalloonChatWindow::PickAndSendFile()
{
    // 文件要先上传到文件服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无对端绑定）不支持真实发文件。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 弹文件选择对话框选一个任意类型文件。
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"所有文件"), L"*.*");
    ofn.lpstrFilter = fileFilter.c_str();
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn))
    {
        return;   // 用户取消选择
    }
    SendFileData(szFile);
}

void CBalloonChatWindow::SendFileData(const std::wstring& path, bool forceOffline)
{
    // 真实会话才发文件（调试入口无对端绑定）。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 取文件大小（仅取属性、不读盘）。空文件 / 取不到大小则不发。
    long long sizeBytes = 0;
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)
            && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            sizeBytes = ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
        }
    }
    if (sizeBytes <= 0)
    {
        LOG_ERROR("单聊窗发文件: 文件为空或读不到大小, to=%u.", m_peerUserId);
        return;
    }

    // 文件名取路径基名（随消息发出、气泡显示）。
    CString fileName;
    {
        size_t sep = path.find_last_of(L"\\/");
        fileName = (sep == std::wstring::npos)
                       ? CString(path.c_str())
                       : CString(path.substr(sep + 1).c_str());
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"文件气泡，再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();

    // 对方在线时先试在线直传（2026-10-04 起，docs/单聊文件P2P直传与P2P徽章方案.md）：数据经 P2P 链路直接给对方，
    // 不经服务器；对方不支持、5 分钟没接收或建不起链路时自动改为上传服务器。调用方要求按离线文件上传时不试
    if (TrySendFileDirect(path, fileName, sizeBytes, clientMsgId, m_peerPresence == kPresenceOffline, forceOffline))
    {
        return;
    }

    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeFileMessage(
        true, fileName, sizeBytes, CString(path.c_str()), clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);

    // 交文件传输工作线程：申请上传→传字节→发文件消息。进度 / 结果经
    // FMG_MSG_V2_FILE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::FileTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, clientMsgId, path);
    LOG_INFO("单聊窗发文件: to=%u, clientmsgid=%s, size=%lld.",
             m_peerUserId, clientMsgId.c_str(), sizeBytes);

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。重新发送时登记同样的内容
    m_sendBumps.Add(clientMsgId, balloonchatwnd::MakeFileSendBump(1, (long long)m_peerUserId));
}

void CBalloonChatWindow::DoScreenshot(bool fullScreen)
{
    // 调试入口（无对端绑定）不支持真实发图，截图也就无处可发，直接不弹。
    if (m_peerUserId == 0)
    {
        return;
    }
    // 截图模块没有加载成功时截图按钮已隐藏，这里拦住截图快捷键转来的请求
    if (!balloonscreenshot::IsScreenshotAvailable())
    {
        LOG_INFO("%s: screenshot ignored, the screenshot module is unavailable.", "ChatWindow");
        return;
    }
    // 密聊状态下截图入口禁用（工具栏按钮已隐藏，这里拦住截图快捷键，docs/密聊方案.md 4.6 第 3 条）
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        LOG_INFO("screenshot ignored in secret mode, peer=%u.", m_peerUserId);
        return;
    }
    RefreshScreenshotTip();
    // 模态发起截图：复制 / 保存由浮层内部完成；「发送」立即发图、「发送到对话框」
    // 把截图内联插入输入框（经下面两个回调）。「截图时隐藏当前窗口」开启时本窗口不出现在截图里。
    bool hideSelf = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hideWindow;
    balloonscreenshot::ScreenshotOptions opts;
    opts.m_excludeHwnd = hideSelf ? m_hWnd : NULL;
    opts.m_startFullScreen = fullScreen;
    opts.m_sendFn = &CBalloonChatWindow::OnScreenshotSend;
    opts.m_stageFn = &CBalloonChatWindow::OnScreenshotStage;
    opts.m_ctx = this;
    LOG_INFO("ChatWindow::DoScreenshot: peer=%u, full screen=%d, hide self=%d.",
             m_peerUserId, fullScreen ? 1 : 0, hideSelf ? 1 : 0);
    // 截图界面在 FlamingoScreenshot.dll 里（2026-10-07，docs/截图独立模块方案.md），经截图宿主模态运行
    balloonscreenshot::RunScreenshot(m_hWnd, opts);
}

void CBalloonChatWindow::RequestScreenshot(bool fullScreen)
{
    if (!IsWindow())
    {
        return;
    }
    ::PostMessage(m_hWnd, kDoScreenshotMsg,
                  (WPARAM)(fullScreen ? balloonchatwnd::kShotReqFullScreen : balloonchatwnd::kShotReqCapture), 0);
}

void CBalloonChatWindow::OpenScreenshotMenu()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_lpFMGClient == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* cut = content->FindCtrlById(balloonchatwnd::kIdTbScreenshot);
    balloonwjui::DuiControl* caret = content->FindCtrlById(balloonchatwnd::kIdTbScreenshotMenu);
    if (cut == NULL || caret == NULL)
    {
        return;
    }
    RefreshScreenshotTip();

    // 锚点：剪刀与 ▾ 合起来的矩形（客户区坐标转屏幕坐标），菜单左边与剪刀左边对齐
    RECT rcCut = cut->GetRect();
    RECT rcCaret = caret->GetRect();
    POINT tl = { rcCut.left, rcCut.top };
    POINT br = { rcCaret.right, rcCaret.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    //截图快捷键与「截图时隐藏当前窗口」2026-10-07 起存在全局配置里（docs/截图独立模块方案.md 3.4 节）
    balloonscreenshot::ScreenshotPrefs prefs = balloonscreenshot::LoadGlobalScreenshotPrefs();
    balloonchatwnd::ScreenshotMenuChoice choice = balloonchatwnd::TrackScreenshotMenu(
        m_hWnd, anchor, prefs.m_hotkey, prefs.m_hideWindow);
    switch (choice)
    {
    //「截图」：与点剪刀相同
    case balloonchatwnd::kShotMenuCapture:
        DoScreenshot(false);
        break;
    //「截取全屏」
    case balloonchatwnd::kShotMenuFullScreen:
        DoScreenshot(true);
        break;
    //「截图时隐藏当前窗口」：取反并立即存盘（与系统设置「聊天」页的开关是同一个配置）
    case balloonchatwnd::kShotMenuToggleHide:
        prefs.m_hideWindow = !prefs.m_hideWindow;
        balloonscreenshot::SaveGlobalScreenshotPrefs(prefs);
        LOG_INFO("ChatWindow: screenshot hide-window option set to %d.", prefs.m_hideWindow ? 1 : 0);
        break;
    //没有选
    case balloonchatwnd::kShotMenuNone:
    default:
        break;
    }
}

void CBalloonChatWindow::RefreshScreenshotTip()
{
    DWORD hotkey = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey;
    CString tip = balloonscreenshot::ScreenshotCommandText(hotkey);
    RegisterTip(GetClientContent(), balloonchatwnd::kIdTbScreenshot, tip);
}

void CBalloonChatWindow::OnScreenshotSend(void* ctx, const wchar_t* pngPath)
{
    CBalloonChatWindow* self = static_cast<CBalloonChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送」走与选图发送同一条链路（乐观气泡 + imgserver 上传 + 会话冒泡）。
    self->SendImageFile(pngPath);
}

void CBalloonChatWindow::OnScreenshotStage(void* ctx, const wchar_t* pngPath)
{
    CBalloonChatWindow* self = static_cast<CBalloonChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送到对话框」：把截图作为内联图片插入输入框（按上限缩放展示），不立即发送；
    // 用户可再配文字，按「发送」时由 DoSend 按文档顺序连同文字一起逐段发出。
    self->StageImageInInput(pngPath);
}

bool CBalloonChatWindow::StageImageInInput(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return false;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return false;
    }
    // 下标取登记前的表长：这张图的源文件将落在表里的这个位置。标记随图片对象存在文档里，
    // 发送时 BuildOutgoingParts 从标记读回下标，不再依赖"文档第几张 = 第几个源文件"。
    const int index = (int)m_pendingShotPaths.size();
    if (!input->InsertTaggedImage(path.c_str(), balloonchatwnd::ImageTagFromIndex(index),
                                  kInlineImageMaxPx))
    {
        LOG_WARNING("chat window: failed to stage image into input, index=%d.", index);
        return false;
    }
    // 发送时直接发这个源文件，不从内嵌对象抽位图（那条路在静态图对象上取到的是黑块）。
    m_pendingShotPaths.push_back(path);
    return true;
}

bool CBalloonChatWindow::StageImageThunk(void* ctx, const std::wstring& path)
{
    CBalloonChatWindow* self = static_cast<CBalloonChatWindow*>(ctx);
    if (self == NULL || !self->IsWindow())
    {
        return false;
    }
    return self->StageImageInInput(path);
}

void CBalloonChatWindow::OnImageSendProgress(const std::string& clientMsgId,
                                             long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(sent * 100 / total);
    }
    list->UpdateImageUploadProgress(clientMsgId, percent);
}

void CBalloonChatWindow::OnImageSendResult(bool ok, const std::string& clientMsgId,
                                           const std::string& fileid,
                                           const std::string& md5)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    if (ok)
    {
        list->MarkImageUploadDone(clientMsgId);
        // 回填 fileid / md5：本条将来被引用时要写进 content.quote，
        // 收方据此在引用块里画缩略图。不回填的话引用自己发的图带不出定位信息。
        list->SetImageIdsByClientMsgId(clientMsgId,
                                       Utf8ToCString(fileid), Utf8ToCString(md5));
    }
    else
    {
        list->MarkImageUploadFailed(clientMsgId);
        //图片没能发出：不冒泡
        m_sendBumps.Drop(clientMsgId);
    }
}

void CBalloonChatWindow::OnFileSendProgress(const std::string& clientMsgId,
                                            long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(sent * 100 / total);
    }
    list->UpdateFileUploadProgress(clientMsgId, percent);
}

void CBalloonChatWindow::OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId, int errcode)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    if (ok)
    {
        list->MarkFileUploadDone(clientMsgId);
    }
    else if (cancelled)
    {
        //用户主动取消：置「已取消」态（保留进度作灰色停位），不按失败处理
        list->MarkFileCancelledByClientMsgId(clientMsgId);
        m_sendBumps.Drop(clientMsgId);
    }
    else
    {
        list->MarkFileUploadFailed(clientMsgId);
        //气泡下方写原因（如文件名包含敏感词）；文件没能发出，不冒泡
        list->MarkSendFailed(clientMsgId, CString(balloonchatwnd::FileSendFailureText(errcode).c_str()));
        m_sendBumps.Drop(clientMsgId);
    }
}

void CBalloonChatWindow::DoSendShake()
{
    if (!CheckOnlineFor(balloonmain::kActionShakeWindow))
    {
        return;
    }

    // 抖动只在真实会话下发送（调试入口无对端绑定，仅本地晃一下窗口看效果）。
    if (m_peerUserId == 0)
    {
        ShakeWindow();
        return;
    }

    // 发送侧频率限制：对同一目标，两次窗口抖动的间隔不得小于 kShakeSendMinIntervalMs
    // （5 秒）。距上次成功发送不足该间隔的点击被拦截 —— 不发送、不晃自己，只在
    // 本窗口居中提示，避免对对端刷屏式抖动。被拦截不刷新计时（间隔始终从上次
    // 成功发送算起，连点不会把可发送时刻一直往后推）。
    if (!m_sendShakeLimiter.TryPass(::GetTickCount64(), balloonchatwnd::kShakeSendMinIntervalMs))
    {
        CString tip;
        tip.Format(TR(L"发送窗口抖动过于频繁，请 %d 秒后再试"),
                   (int)(balloonchatwnd::kShakeSendMinIntervalMs / 1000));
        AppendSystemTip(tip);
        LOG_INFO("chat window: shake send throttled (less than %llu ms since last send), to=%u.",
                 (unsigned long long)balloonchatwnd::kShakeSendMinIntervalMs, m_peerUserId);
        return;
    }

    // 经网络层发送抖动消息（msgtype=13，content={}）。回执经 cmd_send_message
    // 应答异步回投，失败时由主面板提示（与文本同路径）。
    std::string clientMsgId = GenClientMsgId();
    CIUSocket::GetInstance().SendChatShakeMessage((int64_t)m_peerUserId, clientMsgId);
    LOG_INFO("单聊窗发送窗口抖动: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    // 本地反馈：聊天区居中提示 + 晃自己窗口。提示文字与历史里的抖动提示行共用（ShakeTipText）
    AppendSystemTip(balloonchatwnd::ShakeTipText(true));
    ShakeWindow();

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。
    {
        ConvSendBumpData bump;
        bump.ctype   = 1;                   // 单聊
        bump.cid     = (long long)m_peerUserId;
        bump.msgType = kMsgTypeShake;
        bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeShake, std::string());
        m_sendBumps.Add(clientMsgId, bump);
    }
}

void CBalloonChatWindow::BringChatWindowToFront()
{
    if (!IsWindow())
    {
        return;
    }

    // 用户正在操作别的程序时，Windows 会拒绝本进程设前台；此时改为只把窗口提到其他
    // 普通窗口之上并闪烁任务栏按钮，不抢键盘焦点（详见 WindowAttention.h）。
    balloonchatwnd::Win32WindowRaiseOps ops;
    const balloonchatwnd::WindowRaiseResult r = balloonchatwnd::RaiseWindowForAttention(m_hWnd, ops);

    // 诊断日志：调用前的前台窗口属于哪个进程、是不是本进程，用来判断设前台被拒绝是否
    // 因为用户正在操作别的程序（2026-09-30 用户反馈"某些情况下对方的聊天窗口不会前置"）。
    DWORD dwForegroundPid = 0;
    if (r.m_foregroundBefore != NULL)
    {
        ::GetWindowThreadProcessId(r.m_foregroundBefore, &dwForegroundPid);
    }
    LOG_INFO("chat window: shake raise, hwnd=%p, minimized=%d, SetForegroundWindow=%d, "
             "foreground before=%p (pid=%lu, own process=%d), foreground now=%d, "
             "raised without focus=%d, taskbar flashed=%d.",
             m_hWnd, r.m_wasMinimized ? 1 : 0, r.m_setForegroundReturned ? 1 : 0,
             r.m_foregroundBefore, (unsigned long)dwForegroundPid,
             (dwForegroundPid == ::GetCurrentProcessId()) ? 1 : 0,
             r.m_isForeground ? 1 : 0, r.m_raisedWithoutFocus ? 1 : 0, r.m_flashed ? 1 : 0);
}

void CBalloonChatWindow::SetSubtitleHighlight(bool active)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatIconButton* btn =
        dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kIdHdrSubtitle));
    if (btn != NULL)
    {
        btn->SetActive(active);
    }
}

void CBalloonChatWindow::ToggleSubtitleMode()
{
    //字幕由主面板持有（不随本窗口关闭）：同步请主面板切换，按结果高亮按钮（主面板的状态回调也会再设一次）
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        LOG_INFO("chat window: subtitle toggle ignored, no host panel, peer=%u.", m_peerUserId);
        return;
    }
    int64_t cid = (int64_t)m_peerUserId;
    const bool active = ::SendMessage(m_hostPanel, FMG_MSG_TOGGLE_SUBTITLE, (WPARAM)kConvTypeSingle,
                                      reinterpret_cast<LPARAM>(&cid)) != 0;
    SetSubtitleHighlight(active);
    LOG_INFO("chat window: subtitle mode %s, peer=%u.", active ? "on" : "off", m_peerUserId);
}

void CBalloonChatWindow::ShakeWindow()
{
    if (!IsWindow())
    {
        return;
    }
    // 最小化状态下先还原，否则晃动不可见。
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    // 仅当当前不在抖动时记录起点（避免在已偏移位置上取到错误基准）；
    // 已在抖动则只把步数复位到 0，从同一起点重放。
    if (m_shakeStep < 0)
    {
        RECT rc = { 0 };
        ::GetWindowRect(m_hWnd, &rc);
        m_shakeOrigin.x = rc.left;
        m_shakeOrigin.y = rc.top;
    }
    m_shakeStep = 0;
    ::SetTimer(m_hWnd, kShakeTimerId, kShakeStepMs, NULL);
}

void CBalloonChatWindow::AppendSystemTip(const CString& text)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    // 密聊状态下列表里只显示密聊内容（docs/密聊方案.md 4.7）：通话结果、窗口抖动这类普通提示不插入。
    // 密聊自己的提示走 AppendSecretTip
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        return;
    }
    list->AppendMessage(balloonchatwnd::MakeSystemTipMessage(text));
    // 首屏历史还没渲染时（窗口刚打开就插入提示，典型如从主面板右键菜单发起通话、对方不在线），首屏渲染会清空列表：
    // 记下来，OnHistoryMessages 渲染完首屏后补回。调试入口不拉历史，不记录
    if (m_peerUserId != 0)
    {
        m_earlyTips.OnTipShown(std::wstring(text.GetString()));
    }
}

void CBalloonChatWindow::OpenEmojiPicker()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbEmoji);
    if (btn == NULL)
    {
        return;
    }

    // 取表情按钮屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    // 弹 程序目录/Face 图片表情浮层（已取代旧的 Unicode emoji 浮层 m_emojiPopup）。
    m_facePopup.SetPickCallback(&CBalloonChatWindow::OnFacePicked, this);
    m_facePopup.OpenAt(anchor, m_hWnd);
}

void CBalloonChatWindow::OnEmojiPicked(void* userdata, const wchar_t* emoji)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertEmojiToInput(emoji);
    }
}

void CBalloonChatWindow::OnFacePicked(void* userdata, int faceId, const wchar_t* facePath)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertFaceToInput(faceId, facePath);
    }
}

void CBalloonChatWindow::OpenFontPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbFont);
    if (btn == NULL)
    {
        return;
    }

    // 取「AF」按钮屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    m_fontPopup.SetFont(m_currentFont);   // 回显当前设置
    // 本会话设过自定义字体时，「使用默认字体」才可点
    const bool hasOverride = (m_lpFMGClient != NULL && m_peerUserId != 0)
        && m_lpFMGClient->m_UserConfig.ChatFonts().HasOverride(kChatFontCtypeSingle, (int64_t)m_peerUserId);
    m_fontPopup.SetUseDefaultEnabled(hasOverride);
    m_fontPopup.OpenAt(anchor, m_hWnd);
}

void CBalloonChatWindow::OnFontChanged(void* userdata, const net::TextFont& font)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self == NULL)
    {
        return;
    }
    self->m_currentFont = font;
    self->ApplyCurrentFontToInput();

    // 存为本会话的自定义字体（只影响这个会话，重启后仍然有效）
    if (self->m_lpFMGClient != NULL && self->m_peerUserId != 0)
    {
        self->m_lpFMGClient->m_UserConfig.ChatFonts().SetOverride(
            kChatFontCtypeSingle, (int64_t)self->m_peerUserId, font);
        self->SaveChatFontPrefs();
        self->m_fontPopup.SetUseDefaultEnabled(true);
    }
}

void CBalloonChatWindow::OnUseDefaultFont(void* userdata)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self == NULL || self->m_lpFMGClient == NULL || self->m_peerUserId == 0)
    {
        return;
    }

    // 删掉本会话的自定义字体，改用全局默认（全局默认也没设过时恢复默认外观）
    self->m_lpFMGClient->m_UserConfig.ChatFonts().RemoveOverride(
        kChatFontCtypeSingle, (int64_t)self->m_peerUserId);
    self->SaveChatFontPrefs();
    self->m_currentFont = self->ResolveChatFont();
    self->ApplyCurrentFontToInput();

    // 浮层保持打开，回显改用后的字体；按钮变为不可点
    self->m_fontPopup.SetFont(self->m_currentFont);
    self->m_fontPopup.SetUseDefaultEnabled(false);
    LOG_INFO("chat font reset to default for single chat, peer=%u.", self->m_peerUserId);
}

net::TextFont CBalloonChatWindow::ResolveChatFont() const
{
    if (m_lpFMGClient == NULL || m_peerUserId == 0)
    {
        return net::TextFont();
    }
    return m_lpFMGClient->m_UserConfig.ChatFonts().Resolve(kChatFontCtypeSingle, (int64_t)m_peerUserId);
}

void CBalloonChatWindow::ApplyCurrentFontToInput()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }

    // RichEdit 不接管 HFONT 所有权：先让控件换用新字体（或退回默认字体），再释放上一只。
    // 新字体建不出来时控件仍在用上一只，此时不能释放它
    HFONT hNewFont = NULL;
    if (m_currentFont.m_valid)
    {
        hNewFont = balloonchatwnd::ApplyChatFontToEdit(input, m_currentFont);
        if (hNewFont == NULL)
        {
            return;
        }
    }
    else
    {
        balloonchatwnd::ResetChatInputFont(input, m_inputDefaultTextColor);
    }
    if (m_hInputFont != NULL)
    {
        ::DeleteObject(m_hInputFont);
    }
    m_hInputFont = hNewFont;
}

void CBalloonChatWindow::SaveChatFontPrefs()
{
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->SaveUserConfig();
    }
}

void CBalloonChatWindow::ApplyDefaultChatFontIfNotOverridden()
{
    if (m_lpFMGClient == NULL || m_peerUserId == 0)
    {
        return;
    }
    // 本会话单独设过字体：不跟随全局默认
    if (m_lpFMGClient->m_UserConfig.ChatFonts().HasOverride(kChatFontCtypeSingle, (int64_t)m_peerUserId))
    {
        return;
    }
    m_currentFont = ResolveChatFont();
    ApplyCurrentFontToInput();
    // 字体浮层若正开着，回显新的默认字体
    m_fontPopup.SetFont(m_currentFont);
}

void CBalloonChatWindow::InsertEmojiToInput(const wchar_t* emoji)
{
    if (emoji == NULL || emoji[0] == L'\0')
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 追加到输入框末尾（面板保持打开便于连续选；不抢焦点，避免面板因失焦关闭）。
    CString cur = input->GetText();
    cur += emoji;
    input->SetText(cur);
}

void CBalloonChatWindow::InsertFaceToInput(int faceId, const wchar_t* facePath)
{
    if (facePath == NULL || facePath[0] == L'\0')
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 以"带 faceId 标记的内联图"插入当前光标处（所见即所得）。标记按 ChatFaceTag 约定
    // 编码进 REOBJECT.dwUser，发送时据此把表情与截图区分、内联进文本消息。面板保持
    // 打开便于连续选，故不在此抢焦点。
    input->InsertTaggedImage(facePath,
                             balloonchatwnd::FaceTagFromId(faceId),
                             balloonchatwnd::kFaceDisplayPx);
}

void CBalloonChatWindow::OnRecvMessage(const net::MessageNotify& notify)
{
    // 对方的消息都收到了，"正在输入…"就该收起 —— 不必干等它过期，也不必指望对端
    // 一定会补发 state=0（那包不可靠）。放在函数最前面，抖动等提前 return 的分支也覆盖到。
    if (m_peerTyping.Clear())
    {
        RefreshTypingHeader();
    }

    // 密聊状态下对方经别的设备发来的普通消息：主面板照常落库、进会话列表，这里不显示在密聊列表里，
    // 退出密聊、重新加载历史后出现（docs/密聊方案.md 4.7）
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        LOG_INFO("normal message not shown in secret mode, peer=%u, msgid=%lld.", m_peerUserId,
                 (long long)notify.m_msgid);
        return;
    }

    // 桌面字幕由主面板持有，收到的消息由主面板直接交给字幕（聊天窗关着也要显示），这里不再处理

    // 窗口抖动：先于"非文本即跳过"处理（抖动无正文）。居中提示 + 晃动本窗口，
    // 并对短时间内的连发抖动做节流，避免被刷屏式晃动。
    if (notify.m_msgtype == kMsgTypeShake)
    {
        // 接收侧节流：同一来源两次抖动间隔不足 kShakeRecvMinIntervalMs（4 秒）则
        // 静默忽略，避免被对端连发刷屏式晃动。被拦截不刷新计时。
        if (!m_recvShakeLimiter.TryPass(::GetTickCount64(), balloonchatwnd::kShakeRecvMinIntervalMs))
        {
            LOG_INFO("chat window: shake received within the throttle interval (%llu ms), ignored, from=%lld, msgid=%lld.",
                     (unsigned long long)balloonchatwnd::kShakeRecvMinIntervalMs,
                     (long long)notify.m_from, (long long)notify.m_msgid);
            return;
        }
        // 锁屏期间只留下系统提示、不调到前台也不晃动：调到前台会抢走锁屏界面的前台与键盘焦点
        //（docs/锁屏方案.md 第 4.9 节）。窗口抖动有时效，解锁后不再补做
        if (balloonlock::DecideDuringLock(balloonlock::kLockEvtChatWindowRaise, balloonlock::IsScreenLocked())
            != balloonlock::kLockActNormal)
        {
            AppendSystemTip(balloonchatwnd::ShakeTipText(false));
            LOG_INFO("chat window: shake received while the screen is locked, raise skipped, from=%lld, msgid=%lld.",
                     (long long)notify.m_from, (long long)notify.m_msgid);
            return;
        }
        // 接收方窗口若未显示 / 最小化 / 被别的窗口挡住，先调到用户眼前再抖动，确保对方能看到。
        BringChatWindowToFront();
        AppendSystemTip(balloonchatwnd::ShakeTipText(false));
        ShakeWindow();
        LOG_INFO("chat window: shake received, from=%lld, msgid=%lld.",
                 (long long)notify.m_from, (long long)notify.m_msgid);
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 本人发出的消息也会走到这里：本人把消息转发给对方后，主面板用转发应答里的消息体
    // 调本函数显示（服务端不会把单聊消息推回给发送方）。这类消息要画成本人的气泡。
    const UINT selfUserId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const bool fromMe = (selfUserId != 0 && (UINT)notify.m_from == selfUserId);

    // 图片消息：插一条"下载中"气泡，并触发原图下载；
    // 下载完成后经 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 回投、按 msgid 回填渲染。
    if (notify.m_msgtype == kMsgTypeImage)
    {
        // 去重：若该图片（同 msgid）已在列表里（如历史已先渲染），不再重复追加，避免错位 / 重复。
        if (list->HasImageMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("单聊窗收到图片已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingImage(list, fromMe, notify);
        LOG_INFO("单聊窗收到图片消息: from=%lld, msgid=%lld, fileid=%s.",
                 (long long)notify.m_from, (long long)notify.m_msgid,
                 notify.m_imageFileId.c_str());
        return;
    }

    // 文件消息：插一条气泡（缓存命中=完成态、否则"下载中"并触发下载），按 msgid 去重。
    if (notify.m_msgtype == kMsgTypeFile)
    {
        if (list->HasFileMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("单聊窗收到文件已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingFile(list, fromMe, notify);
        LOG_INFO("单聊窗收到文件消息: from=%lld, msgid=%lld, fileid=%s.",
                 (long long)notify.m_from, (long long)notify.m_msgid,
                 notify.m_fileFileId.c_str());
        return;
    }

    // 语音消息：画成语音气泡，本机没有这段语音就在后台下载；同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeVoice)
    {
        balloonchatwnd::ChatMessage existingVoice;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingVoice))
        {
            return;
        }
        balloonchatwnd::VoicePlayedQuery played;
        played.m_ctype = kConvTypeSingle;
        played.m_cid = (int64_t)m_peerUserId;
        played.m_msgids.push_back(notify.m_msgid);
        balloonchatwnd::QueryVoicePlayed(m_hostPanel, played);
        RenderIncomingVoice(list, fromMe, notify, played);
        return;
    }

    // 文件夹传输记录：优先就地接管会话里那条还没被接管过的本地文件夹气泡，接管不了才
    // 追加一条新的（详见 OnFolderRecord 的说明）。这条记录由服务端生成、<u>向双方各推
    // 一帧</u>，其 from 恒为文件夹发送方，故本端到底是发送方还是接收方要与登录用户比对
    // 才知道 —— 不能像别的推送那样一律当成"对方发来的"。
    if (notify.m_msgtype == kMsgTypeFolder)
    {
        OnFolderRecord(notify, fromMe);
        return;
    }

    // 合并转发的聊天记录：画成卡片。先按普通文字气泡构造（沿用头像与 msgid 的填法），
    // 再套用卡片内容；同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeMerged)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeRealTextMessage(
            fromMe, TR(L"[聊天记录]"), net::TextFont(), notify.m_msgid, notify.m_timestamp,
            notify.m_clientmsgid, net::ReplyQuote());
        balloonchatwnd::ApplyMergedCard(card, notify.m_mergedContent);
        list->AppendMessage(card);
        LOG_INFO("chat window: merged record shown, from=%lld, msgid=%lld, items=%d.",
                 (long long)notify.m_from, (long long)notify.m_msgid, card.mergedCount);
        return;
    }

    // 文件共享卡片：画成卡片，同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeFileShare)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        AppendShareCard(list, notify, fromMe, notify.m_clientmsgid);
        //新的共享卡片：重新拉取本会话的共享，新发起的共享由此计入头部按钮的角标
        balloonfileshare::CFileShareHost::GetInstance().WatchConversationShares(kConvTypeSingle, (int64_t)m_peerUserId);
        return;
    }

    // 屏幕共享卡片：画成卡片，同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeScreenShare)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        AppendScreenShareCard(list, notify, fromMe, notify.m_clientmsgid);
        return;
    }

    // 一对一通话卡片：画成卡片，同一条（同 msgid）已在列表里时不再追加（结束时的改写走 OnCallCardChanged）
    if (notify.m_msgtype == kMsgTypeRtc)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        AppendCallCard(list, notify, fromMe, notify.m_clientmsgid);
        return;
    }

    // 云端录像卡片：画成卡片，同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeCloudRecord)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        AppendCloudRecordCard(list, notify, fromMe, notify.m_clientmsgid);
        return;
    }

    // 入群邀请卡片：画成卡片，同一条（同 msgid）已在列表里时不再追加
    if (notify.m_msgtype == kMsgTypeGroupInvite)
    {
        balloonchatwnd::ChatMessage existingCard;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingCard))
        {
            return;
        }
        AppendGroupInviteCard(list, notify, fromMe, notify.m_clientmsgid);
        return;
    }

    // 系统提示（单聊里只有屏幕共享的结束提示，服务端记在发起人名下）：画成居中提示行，不画发送人气泡。按 msgid 去重
    if (notify.m_msgtype == kMsgTypeSystem)
    {
        balloonchatwnd::ChatMessage existingTip;
        if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existingTip))
        {
            return;
        }
        list->AppendMessage(balloonchatwnd::MakeSystemTipRow(notify));
        LOG_INFO("chat window: system tip shown, peer=%u, msgid=%lld.", m_peerUserId, (long long)notify.m_msgid);
        return;
    }

    // 其余非文本类型（解析层未填 m_text）暂不渲染（后续轮次补）。
    if (notify.m_text.empty())
    {
        LOG_INFO("单聊窗收到非文本消息，暂不渲染: from=%lld, msgtype=%d.",
                 (long long)notify.m_from, notify.m_msgtype);
        return;
    }

    // 去重：同一条消息（同 msgid）已在列表里时不再追加。转发应答与历史同步可能先后
    // 送来同一条
    balloonchatwnd::ChatMessage existing;
    if (notify.m_msgid > 0 && list->GetMessageByMsgid((long long)notify.m_msgid, existing))
    {
        LOG_INFO("chat window: message already shown, skip, msgid=%lld.", (long long)notify.m_msgid);
        return;
    }

    // 对方发来的画成对方气泡、用对端真实头像；本人发出的（转发）画成本人气泡。
    // 按该条消息携带的字体渲染；Markdown 消息带上标志，气泡据此交给 FlamingoMarkdown.dll 渲染。
    CString text = Utf8ToCString(notify.m_text);
    //机器人代答（content.bot）：去掉服务端给老客户端的前缀，换成按界面语言翻译的「[机器人代答]」「[自动回复]」
    if (notify.m_botId > 0 && !notify.m_botKind.empty())
        text = balloonchatwnd::BotReplyDisplayText((LPCWSTR)text, notify.m_botKind).c_str();
    balloonchatwnd::ChatMessage incoming = MakeRealTextMessage(fromMe, text, notify.m_font, notify.m_msgid,
                                                               notify.m_timestamp, std::string(), notify.m_quote);
    incoming.isMarkdown = (notify.m_textFormat == net::text_format_markdown);
    balloonchatwnd::ApplyCodeFields(incoming, notify.m_textFormat, notify.m_codeLang, notify.m_codeNotes, notify.m_from);
    list->AppendMessage(incoming);
    LOG_INFO("单聊窗渲染收到的消息: from=%lld, msgid=%lld.",
             (long long)notify.m_from, (long long)notify.m_msgid);
}

void CBalloonChatWindow::RenderIncomingImage(balloonchatwnd::ChatMessageList* list,
                                             bool fromMe, const net::MessageNotify& notify)
{
    if (list == NULL)
    {
        return;
    }

    // 按消息里的真实宽高算气泡内展示尺寸（缺失则用占位尺寸）。
    int dispW = 0;
    int dispH = 0;
    ComputeImageDisplaySize(notify.m_imageWidth, notify.m_imageHeight, dispW, dispH);

    // 当前登录用户 userid：缓存按账号隔离（Users\<userId>\ChatImage\），故先取好。
    UINT selfId = 0;
    if (m_lpFMGClient != NULL)
    {
        selfId = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    }

    // 缓存命中检查：图片按内容 MD5 缓存在本地账号目录，若已存在则直接渲染原图、
    // 不再触发下载——否则每次渲染历史/收到推送都会把同一张图重下一遍，把 imgserver
    // 刷爆、还把发送任务堵在串行队列后面。
    // 下载侧用"临时文件写完再改名"原子落地，故 <md5> 一旦存在即为完整文件；这里再
    // 用文件实际大小与消息里的期望大小比对兜底（防住历史遗留 / 非原子写入的半截、
    // 空文件被误判命中），不读盘、仅取属性，开销极低。期望大小缺失(<=0)时退回只判存在。
    std::wstring cachePath = net::ImageTransfer::CacheFilePathForMd5(
        net::CurrentUserAccount(), notify.m_imageMd5);
    bool cached = false;
    if (!cachePath.empty())
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (::GetFileAttributesExW(cachePath.c_str(), GetFileExInfoStandard, &fad)
            && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (notify.m_imageSize <= 0)
            {
                cached = true;   //无期望大小可比，退回只判存在
            }
            else
            {
                ULONGLONG actualBytes =
                    (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                cached = (actualBytes == static_cast<ULONGLONG>(notify.m_imageSize));
                if (!cached)
                {
                    LOG_INFO("单聊窗图片缓存大小不符(重下): msgid=%lld, 实际=%llu, 期望=%lld.",
                             (long long)notify.m_msgid, (unsigned long long)actualBytes,
                             (long long)notify.m_imageSize);
                }
            }
        }
    }

    balloonchatwnd::ChatMessage msg;
    if (cached)
    {
        //缓存命中：正常态图片气泡，直接用本地缓存路径渲染。
        msg = balloonchatwnd::MakeImageMessage(
            fromMe, CString(cachePath.c_str()), dispW, dispH, std::string(), 0 /*正常*/);
        msg.msgid = (long long)notify.m_msgid;
    }
    else
    {
        //未命中：插"下载中"气泡，记 msgid 供下载结果回填定位本行。
        msg = balloonchatwnd::MakeDownloadingImageMessage(
            fromMe, dispW, dispH, (long long)notify.m_msgid);
    }
    // 记下原图的 fileid / md5：本条消息将来被别人引用时，要把它们写进 content.quote，
    // 收方才能在引用块里画出缩略图（按 md5 找本地缓存 / 按 fileid 下载）。
    msg.imageFileId = Utf8ToCString(notify.m_imageFileId);
    msg.imageMd5    = Utf8ToCString(notify.m_imageMd5);

    // 头像：自己发的用本端头像、否则对端头像（字形 / 真图，与文本气泡同口径）。
    FillSenderAvatar(msg, fromMe);
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("单聊窗收到图片(缓存命中,免下载): msgid=%lld, md5=%s.",
                 (long long)notify.m_msgid, notify.m_imageMd5.c_str());
        return;
    }

    // 未命中才触发原图下载（fileid 来自消息 content）。下载结果按 msgid 回投，
    // 落地到当前登录账号的缓存目录（selfId）。
    if (!notify.m_imageFileId.empty())
    {
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)notify.m_msgid, notify.m_imageFileId,
            notify.m_imageMd5, notify.m_imageSize, net::CurrentUserAccount());
    }
}

void CBalloonChatWindow::RenderIncomingFile(balloonchatwnd::ChatMessageList* list,
                                            bool fromMe, const net::MessageNotify& notify)
{
    if (list == NULL)
    {
        return;
    }

    //在线直传留下的文件消息：会话里还有那条实时气泡时就地接管，不另插一行（2026-10-04 起）
    if (notify.m_fileDirect && AdoptDirectFileRecord(notify))
    {
        return;
    }

    //本地落点查找与缓存命中判定与群聊窗共用；单聊走「接收 / 拒绝」流程（directDownload=false）
    bool cached = false;
    balloonchatwnd::ChatMessage msg =
        balloonchatwnd::BuildReceivedFileMessage(fromMe, notify, false, &cached);

    // 头像：自己发的用本端头像、否则对端头像（字形 / 真图，与文本气泡同口径）。
    FillSenderAvatar(msg, fromMe);
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("单聊窗收到文件(缓存命中,免下载): msgid=%lld.", (long long)notify.m_msgid);
    }
    else
    {
        LOG_INFO("单聊窗收到文件(待用户接收): msgid=%lld, fromMe=%d.",
                 (long long)notify.m_msgid, fromMe ? 1 : 0);
    }
}

void CBalloonChatWindow::RenderIncomingVoice(balloonchatwnd::ChatMessageList* list, bool fromMe,
                                             const net::MessageNotify& notify,
                                             const balloonchatwnd::VoicePlayedQuery& played)
{
    if (list == NULL)
    {
        return;
    }
    const std::wstring voiceDir = CurrentUserVoiceDir();
    const bool isPlayed = balloonchatwnd::ResolveVoicePlayed(fromMe, played.m_storeReady,
                                                             played.m_played.count(notify.m_msgid) > 0);
    bool needDownload = false;
    balloonchatwnd::ChatMessage msg =
        balloonchatwnd::BuildReceivedVoiceMessage(fromMe, notify, isPlayed, voiceDir, &needDownload);
    // 头像：自己发的用本端头像、否则对端头像（字形 / 真图，与文本气泡同口径）
    FillSenderAvatar(msg, fromMe);
    list->AppendMessage(msg);

    // 本机没有这段语音：在后台下载到 ChatVoice\<md5>.ogg，结果按 md5 经主面板广播回来（OnVoiceDownloadResult）
    if (needDownload)
    {
        net::FileTransfer::GetInstance().EnqueueVoiceDownload(
            notify.m_voiceFileId, notify.m_voiceMd5, notify.m_voiceSize,
            balloonvoice::VoiceCachePath(voiceDir, notify.m_voiceMd5));
    }
    LOG_INFO("chat window: voice message shown, peer=%u, msgid=%lld, fromMe=%d, played=%d, download=%d.",
             m_peerUserId, (long long)notify.m_msgid, fromMe ? 1 : 0, isPlayed ? 1 : 0, needDownload ? 1 : 0);
}

void CBalloonChatWindow::OnImageDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->SetImageDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonChatWindow::OnImageDownloadFailed(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->MarkImageDownloadFailedByMsgid(msgid);
    }
}

void CBalloonChatWindow::OnFileDownloadProgress(long long msgid, long long received, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(received * 100 / total);
    }
    list->UpdateFileDownloadProgress(msgid, percent);
}

void CBalloonChatWindow::OnFileDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    bool incoming = false;
    const bool hit = (list != NULL)
                   && list->SetFileDownloadedByMsgid(msgid, CString(localPath.c_str()), &incoming);

    // 文件已完整落地并校验通过，这时才告诉发送方"收到了" —— 中途失败 / 取消都不会
    // 走到这里，所以对方看到的「已接收」是准的。下载结果是按 msgid 广播给所有聊天窗的，
    // 只有本窗口里确有这条、且是对方发来的文件才上报：别的会话的文件、或自己发出后从
    // 历史里重新下载的文件，都不该由本窗口向对端发回执
    if (hit && incoming)
    {
        ReportFileAck(msgid, kFileAckReceived);
    }
}

void CBalloonChatWindow::OnFilePeerAck(long long msgid, int peerState)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        LOG_ERROR("单聊窗更新对方收取状态失败(消息区未就绪): msgid=%lld.", (long long)msgid);
        return;
    }
    //命中与否都记一笔：没命中说明本窗口没有这条消息（别的会话、或尚未加载进视图），
    //与"命中了但没刷新"是两类完全不同的问题，日志里必须能分开。
    const bool hit = list->SetFilePeerStateByMsgid(msgid, peerState);
    LOG_INFO("单聊窗更新对方收取状态: msgid=%lld, state=%d, 命中气泡=%d.",
             (long long)msgid, peerState, hit ? 1 : 0);
}

void CBalloonChatWindow::OnVoiceSendResult(bool ok, const std::string& clientMsgId, int errcode)
{
    //上传成功：等 1503 的发送回执（FMG_MSG_V2_SEND_CHATMSG_ACK）回填 msgid、去掉发送中的圆环，这里不改气泡
    if (ok)
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list != NULL)
    {
        //气泡下方写「语音发送失败（错误码 N）」；气泡左侧改画红色感叹号，点它可重发
        list->MarkSendFailed(clientMsgId, CString(balloonchatwnd::VoiceSendFailureText(errcode).c_str()));
    }
    //语音没能发出，不冒泡
    m_sendBumps.Drop(clientMsgId);
}

void CBalloonChatWindow::OnVoicePlayedElsewhere(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL && list->MarkVoicePlayedByMsgid(msgid))
    {
        LOG_INFO("chat window: voice played elsewhere, red dot cleared, peer=%u, msgid=%lld.", m_peerUserId, msgid);
    }
}

void CBalloonChatWindow::OnVoiceDownloadResult(const std::string& md5, bool ok, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->ApplyVoiceDownloadResult(md5, ok, CString(localPath.c_str()));
    }
}

void CBalloonChatWindow::OnFileDownloadFailed(long long msgid, bool cancelled)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        if (cancelled)
        {
            //用户主动取消下载：置「已取消」态，不按失败处理
            list->MarkFileCancelledByMsgid(msgid);
        }
        else
        {
            list->MarkFileDownloadFailedByMsgid(msgid);
        }
    }
}

void CBalloonChatWindow::LocateMessage(long long msgid)
{
    m_pendingLocateMsgid = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL || !list->ScrollToMsgid(msgid))
    {
        //此刻没找到（历史还在途 / 该消息较早未加载）：记为待定，等历史渲染后补定位。
        m_pendingLocateMsgid = msgid;
    }
}

void CBalloonChatWindow::OnHistoryMessages(const net::SyncMessagesResult& result)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 密聊状态下不渲染普通历史（例如刚打开窗口就同意了密聊，首屏历史这时才到）：丢弃这次应答并复位翻页在途标志，
    // 退出密聊时会重新拉取
    if (balloonchatwnd::IsSecretState(m_secretState))
    {
        m_paging.m_loading = false;
        LOG_INFO("history page dropped in secret mode, peer=%u, count=%u.", m_peerUserId,
                 (unsigned)result.m_messages.size());
        return;
    }

    // 本次应答是首屏还是"往上翻的一页"？服务端应答里不回显 before，只能靠本端
    // 记的在途标志区分 —— 只有 OnChatLoadOlder 发出的翻页请求会把它置真。
    const bool isOlderPage = m_paging.m_loading;

    //应答到达时刻（从打开窗口起算）与本次渲染耗时，写进下面的日志供性能排查。
    const double arrivedMs = m_openStopWatch.ElapsedMs();
    PerfStopWatch renderStopWatch;
    BUI_TRACE("CHATWND-HISTORY-ARRIVED peer=%u count=%u older=%d sinceOpen=%.3fms",
              m_peerUserId, (unsigned)result.m_messages.size(), isOlderPage ? 1 : 0, arrivedMs);

    //按消息类型分段统计渲染耗时，写进下面的 CHATWND-HISTORY-BREAKDOWN 跟踪行，
    //用来判断首屏渲染的时间花在哪一类消息上
    balloonchatwnd::HistoryRenderBreakdown breakdown;
    PerfStopWatch stageWatch;

    if (isOlderPage)
    {
        // 翻页：绝不能清空。开"头部插入批次"，下面照常按时间正序渲染。
        list->BeginPrependBatch();
    }
    else
    {
        // 首屏是"最近 N 条"完整快照：渲染前先清空列表，避免与窗口刚创建时（拉历史
        // 仍在途中）收到推送、被 OnRecvMessage 乐观追加进来的那几行叠加，导致这些
        // live 行卡在最前面 / 重复。清空后按历史时间正序重渲，位置即正确。
        list->ClearAllMessages();
    }
    breakdown.m_prepareMs = stageWatch.ElapsedMs();

    // 当前登录用户 userid，用于区分自己发的（右侧气泡）与对方发的（左侧气泡）。
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 本页里的语音消息：一次查出本地库记为已听的，供下面决定画不画红点。历史先显示后入库，
    // 库里还没有的对方语音按未听处理（与随后入库时写入的未听一致）
    balloonchatwnd::VoicePlayedQuery voicePlayed;
    voicePlayed.m_ctype = kConvTypeSingle;
    voicePlayed.m_cid = (int64_t)m_peerUserId;
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& vm = result.m_messages[k];
        if (vm.m_msgtype == kMsgTypeVoice && !vm.m_recalled && vm.m_msgid > 0)
        {
            voicePlayed.m_msgids.push_back(vm.m_msgid);
        }
    }
    if (!voicePlayed.m_msgids.empty())
    {
        balloonchatwnd::QueryVoicePlayed(m_hostPanel, voicePlayed);
    }

    // 本页里本人发出、本人撤回的消息：一次查出本机库里的撤回快照，撤回后 5 分钟内的提示行带「编辑」
    //（docs/撤回后重新编辑方案.md §3.4）
    balloonchatwnd::RecallDraftQuery recallDrafts;
    recallDrafts.m_ctype = kConvTypeSingle;
    recallDrafts.m_cid = (long long)m_peerUserId;
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& rm = result.m_messages[k];
        if (rm.m_recalled && rm.m_msgid > 0 && selfId != 0 && (UINT)rm.m_from == selfId
            && balloonchatwnd::RecallOperatorOf(rm) == (int64_t)selfId)
        {
            recallDrafts.m_msgids.push_back(rm.m_msgid);
        }
    }
    balloonchatwnd::QueryRecallDrafts(m_hostPanel, recallDrafts);
    const long long recallNowMs = balloonchatwnd::RecallEditNowMs();
    bool anyRecallEdit = false;
    // 首屏里画出的窗口抖动提示：收到抖动时插入的即时提示会被记为早到提示、首屏渲染后补回，据此去掉重复的那条
    std::vector<std::wstring> historyShakeTips;

    // 服务端以 msgid 升序（时间正序、最旧在前）返回（见 MessageManager.cpp 翻历史
    // 模式把 DESC 查询反转为升序）。正序遍历即得"最旧在上、最新在下"，逐条追加。
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& m = result.m_messages[k];
        bool fromMe = (selfId != 0 && (UINT)m.m_from == selfId);
        PerfStopWatch itemWatch;

        // 已撤回的消息：服务端拉历史时照样下发（带 recalled: true），这里换成撤回提示行，与在线收到
        // 撤回推送时气泡就地改成的提示行相同；不拦下的话撤回的内容在重开窗口后又会显示出来。
        // 单聊里不是本人发的就是对端发的，名字取对端昵称
        if (m.m_recalled)
        {
            //单聊只能撤回本人发的消息（服务端 onRecallMessage），撤回者与发送人相同；仍按撤回者判断，与群聊同口径
            const bool recalledByMe = (selfId != 0 && balloonchatwnd::RecallOperatorOf(m) == (int64_t)selfId);
            balloonchatwnd::ChatMessage tipRow = balloonchatwnd::MakeRecalledTipRow(m, recalledByMe, m_peerName);
            //本机库里有这条的撤回快照且尚未到期：提示行带「编辑」
            if (recalledByMe && fromMe && balloonchatwnd::ApplyRecallDraft(tipRow, recallDrafts, recallNowMs))
            {
                anyRecallEdit = true;
            }
            list->AppendMessage(tipRow);
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 图片消息：插"下载中"气泡并触发下载（历史里自己发的图也需下载，本端无原图）。
        if (m.m_msgtype == kMsgTypeImage)
        {
            RenderIncomingImage(list, fromMe, m);
            breakdown.Add(balloonchatwnd::kHistoryRenderImage, itemWatch.ElapsedMs());
            continue;
        }

        // 文件消息：缓存命中=完成态、否则"下载中"并触发下载（历史里自己发的也按缓存判定）。
        if (m.m_msgtype == kMsgTypeFile)
        {
            RenderIncomingFile(list, fromMe, m);
            breakdown.Add(balloonchatwnd::kHistoryRenderFile, itemWatch.ElapsedMs());
            continue;
        }

        // 语音消息：与实时推送同一构造；本机没有这段语音就在后台下载。耗时计入「文字及其它」一类
        if (m.m_msgtype == kMsgTypeVoice)
        {
            RenderIncomingVoice(list, fromMe, m, voicePlayed);
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 文件夹传输记录：与实时推送共用 OnFolderRecord，两条路看到的样子因而一致。
        // 同一次传输的多次尝试各有一条记录，翻历史时会依次渲染出多条 —— 这是预期行为
        // （见 OnFolderRecord 的说明），不是重复消息。
        if (m.m_msgtype == kMsgTypeFolder)
        {
            OnFolderRecord(m, fromMe);
            breakdown.Add(balloonchatwnd::kHistoryRenderFolder, itemWatch.ElapsedMs());
            continue;
        }

        // 合并转发的聊天记录：画成卡片，构造方式与实时推送相同。耗时计入「文字及其它
        // 占位渲染」一类
        if (m.m_msgtype == kMsgTypeMerged)
        {
            balloonchatwnd::ChatMessage card = MakeRealTextMessage(
                fromMe, TR(L"[聊天记录]"), net::TextFont(), m.m_msgid, m.m_timestamp,
                std::string(), net::ReplyQuote());
            balloonchatwnd::ApplyMergedCard(card, m.m_mergedContent);
            list->AppendMessage(card);
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 文件共享卡片：构造方式与实时推送相同
        if (m.m_msgtype == kMsgTypeFileShare)
        {
            AppendShareCard(list, m, fromMe, std::string());
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 屏幕共享卡片：构造方式与实时推送相同
        if (m.m_msgtype == kMsgTypeScreenShare)
        {
            AppendScreenShareCard(list, m, fromMe, std::string());
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 一对一通话卡片：构造方式与实时推送相同（content 取自本地库，通话结束后已被改写过）
        if (m.m_msgtype == kMsgTypeRtc)
        {
            AppendCallCard(list, m, fromMe, std::string());
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 云端录像卡片：构造方式与实时推送相同
        if (m.m_msgtype == kMsgTypeCloudRecord)
        {
            AppendCloudRecordCard(list, m, fromMe, std::string());
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 入群邀请卡片：构造方式与实时推送相同
        if (m.m_msgtype == kMsgTypeGroupInvite)
        {
            AppendGroupInviteCard(list, m, fromMe, std::string());
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 系统提示（屏幕共享的结束提示）：居中提示行，与实时推送同一构造
        if (m.m_msgtype == kMsgTypeSystem)
        {
            list->AppendMessage(balloonchatwnd::MakeSystemTipRow(m));
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 窗口抖动：没有正文，画成与实时收发时相同的居中提示行，不能落到下面的占位文字分支画成「[抖一抖]」气泡
        if (m.m_msgtype == kMsgTypeShake)
        {
            const balloonchatwnd::ChatMessage tip = balloonchatwnd::MakeShakeTipRow(m, fromMe);
            if (!isOlderPage)
            {
                historyShakeTips.push_back(std::wstring(tip.text.GetString()));
            }
            list->AppendMessage(tip);
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 文本取正文；其余非文本（文件/语音等，m_text 空）用占位文字渲染成文本气泡，
        // 避免历史出现空洞。
        CString text;
        if (!m.m_text.empty())
        {
            text = Utf8ToCString(m.m_text);
            //机器人代答：显示文字换成按界面语言翻译的标记，理由同实时消息
            if (m.m_botId > 0 && !m.m_botKind.empty())
                text = balloonchatwnd::BotReplyDisplayText((LPCWSTR)text, m.m_botKind).c_str();
        }
        else
        {
            text = Utf8ToCString(net::MessageProtocolClient::digestForMsgType(m.m_msgtype, m.m_text));
        }
        balloonchatwnd::ChatMessage historic = MakeRealTextMessage(fromMe, text, m.m_font, m.m_msgid,
                                                                   m.m_timestamp, std::string(), m.m_quote);
        historic.isMarkdown = (m.m_textFormat == net::text_format_markdown);
        balloonchatwnd::ApplyCodeFields(historic, m.m_textFormat, m.m_codeLang, m.m_codeNotes, m.m_from);
        list->AppendMessage(historic);
        breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
    }
    //本页有带「编辑」的撤回提示行：定好到期定时器
    if (anyRecallEdit)
    {
        RefreshRecallEditTimer();
    }
    stageWatch.Restart();
    // 本批最早那条的 msgid，作为下一页的 before 锚点（服务端按时间正序返回）。
    const long long oldestInBatch = result.m_messages.empty()
                                  ? 0
                                  : (long long)result.m_messages[0].m_msgid;

    if (isOlderPage)
    {
        list->EndPrependBatch();
        balloonchatwnd::OnOlderPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
        LOG_INFO("single chat older page rendered: peer=%u, count=%u, oldest msgid=%lld, "
                 "hasmore=%d, render=%.1fms.",
                 m_peerUserId, (uint32_t)result.m_messages.size(),
                 (long long)oldestInBatch, result.m_hasmore ? 1 : 0,
                 renderStopWatch.ElapsedMs());
        return;   // 翻页不滚到底
    }

    balloonchatwnd::OnFirstPageLoaded(m_paging, oldestInBatch, result.m_hasmore);

    // 首屏渲染前把列表整个清空过，建窗时补出的那条文件夹邀请气泡也一并没了；在这里
    // 再补一次。文件夹气泡不进服务端消息表，历史里不会有它，故只能这样重建。
    RestorePendingFolderInvite();
    // 密聊邀请卡片同样是纯本地气泡，一并重建
    RestoreSecretInviteCards();
    // 首屏渲染之前插入的居中提示（不入库）也被清掉了：按插入顺序补回末尾。此时记录类已转为「首屏已渲染」，
    // 补回时 AppendSystemTip 不会再次记录
    // 首屏历史里已经画出的窗口抖动提示不再补回，否则同一句提示出现两次
    const std::vector<std::wstring> earlyTips =
        balloonchatwnd::DropTipsShownInHistory(m_earlyTips.TakeTipsOnFirstPageRendered(), historyShakeTips);
    for (size_t i = 0; i < earlyTips.size(); ++i)
    {
        AppendSystemTip(CString(earlyTips[i].c_str()));
    }
    if (!earlyTips.empty())
    {
        LOG_INFO("single chat restored %u system tips cleared by the first history page, peer=%u.",
                 (unsigned)earlyTips.size(), m_peerUserId);
    }
    // 本人刚退出密聊：恢复的普通历史末尾插一条「你已退出密聊，聊天内容已清除」（不落库，关窗即消失）
    if (m_secretExitTipPending)
    {
        m_secretExitTipPending = false;
        AppendSystemTip(CString(balloonchatwnd::SecretExitTip().c_str()));
    }

    list->ScrollToBottom();
    // 首屏渲染前发出的 Markdown 文档的卡片也被清掉了：它们都已确认入库时重新拉一次最新一页
    m_mdEarlySends.OnFirstPageRendered();
    ReloadIfMarkdownEarlySendsDone();
    // 历史已渲染：若有从历史窗"定位"过来、当时消息还没加载的待办，现在补定位，成功则清。
    if (m_pendingLocateMsgid != 0 && list->ScrollToMsgid(m_pendingLocateMsgid))
    {
        m_pendingLocateMsgid = 0;
    }
    // 一并记下首尾 msgid：服务端约定按 msgid 升序（最旧在前）返回，本函数据此正序
    // 渲染。正常应有 first < last；两边的顺序约定一旦对不上，看这一行即可发现
    // （群聊窗曾因按"最新在前"倒序遍历，把整个历史列表颠倒过来）。
    long long firstMsgid = 0;
    long long lastMsgid  = 0;
    if (!result.m_messages.empty())
    {
        firstMsgid = (long long)result.m_messages.front().m_msgid;
        lastMsgid  = (long long)result.m_messages.back().m_msgid;
    }
    //arrived 为应答到达时刻、render 为本次渲染耗时，都是毫秒；arrived 从打开窗口起算，
    //它减去 CreateAndShow 日志里的 shown 大致就是等服务端应答的时间。
    const double renderMs = renderStopWatch.ElapsedMs();
    breakdown.m_finishMs = stageWatch.ElapsedMs();
    BUI_TRACE("CHATWND-HISTORY-BREAKDOWN peer=%u prepare=%.3fms text=%u/%.3fms image=%u/%.3fms "
              "file=%u/%.3fms folder=%u/%.3fms finish=%.3fms",
              m_peerUserId, breakdown.m_prepareMs,
              breakdown.m_count[balloonchatwnd::kHistoryRenderText], breakdown.m_ms[balloonchatwnd::kHistoryRenderText],
              breakdown.m_count[balloonchatwnd::kHistoryRenderImage], breakdown.m_ms[balloonchatwnd::kHistoryRenderImage],
              breakdown.m_count[balloonchatwnd::kHistoryRenderFile], breakdown.m_ms[balloonchatwnd::kHistoryRenderFile],
              breakdown.m_count[balloonchatwnd::kHistoryRenderFolder], breakdown.m_ms[balloonchatwnd::kHistoryRenderFolder],
              breakdown.m_finishMs);
    BUI_TRACE("CHATWND-HISTORY-RENDERED peer=%u render=%.3fms sinceOpen=%.3fms",
              m_peerUserId, renderMs, m_openStopWatch.ElapsedMs());
    LOG_INFO("single chat first page rendered: peer=%u, count=%u, first.msgid=%lld, "
             "last.msgid=%lld, arrived=%.1fms, render=%.1fms.",
             m_peerUserId, (uint32_t)result.m_messages.size(),
             firstMsgid, lastMsgid, arrivedMs, renderMs);
}

LRESULT CBalloonChatWindow::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/,
                                   LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_SIZE —— 仍交 DuiFrameWindow 链缩放客户区与后备缓冲。
    // 投递一个自定义消息：等 WM_SIZE 全程走完、客户区尺寸更新后，再强制
    // 重排客户区控件树。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonChatWindow::OnRelayout(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }

    // 把输入区块稳定在用户选定（或默认）的高度：窗口缩放时让消息区吃掉高度
    // 变化、输入区高度基本不变 —— 与改造前"输入区固定、消息区撑满"的手感一致。
    // 此时 DuiHost::OnSize 已按新尺寸把含 splitter 的客户区树重排过，故直接读
    // splitter 现矩形算分割点；SetSplitPx 会按 splitter 自身 min-size 夹取并重排
    // 其两个 pane。须放在下面消息区 ForceLayout 之前 —— 否则消息区按旧矩形重排。
    balloonwjui::DuiSplitter* sp = dynamic_cast<balloonwjui::DuiSplitter*>(
        content->FindCtrlById(balloonchatwnd::kIdInputSplitter));
    if (sp != NULL)
    {
        RECT rcSp = sp->GetRect();
        int track = (rcSp.bottom - rcSp.top) - sp->GetBarThickness();
        if (track > 0)
        {
            sp->SetSplitPx(track - balloonchatwnd::GetRememberedChatInputHeightPx());
        }
    }

    // 窗口缩放时 DuiHost::OnSize 已按新尺寸把整棵客户区树（含消息区）重排
    // 过了（m_root == frame 的 skeleton，客户区是其 Weight(1) 子节点），故
    // 此处不再重复整树 ForceLayout —— 那是每步缩放的冗余重排。仅对消息区
    // 单独强制一次重排，作为"气泡按新高度重新裁剪、不溢出到输入区"的定向
    // 保险（代价极低：折行结果已被 ChatBubble 的宽度缓存命中）。
    balloonwjui::DuiControl* msgArea =
        content->FindCtrlById(balloonchatwnd::kIdMessageArea);
    if (msgArea != NULL)
    {
        msgArea->ForceLayout(msgArea->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}
