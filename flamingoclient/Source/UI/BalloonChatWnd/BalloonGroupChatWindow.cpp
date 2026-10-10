/**
 *  BalloonGroupChatWindow.cpp
 *
 *  新版群聊窗口实现。G-P0：骨架 —— 以 DuiFrameWindow 形式创建 / 显示一个
 *  群聊窗，客户区暂为占位文字。真正的头部 / 消息区 / 输入区 / 边栏由后续
 *  G-P1 阶段的 GroupChatWindowLayout 构建。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonGroupChatWindow.h"

#include <memory>

#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Session/UserSessionData.h"       // FMG_MSG_CONV_SEND_BUMP / ConvSendBumpData
#include "../../Common/Log/IULog.h"                 // LOG_INFO
#include "../../Common/Util/EncodeUtil.h"            // UTF-8 ↔ Unicode
#include "../BalloonMemo/MemoCenter.h"               // 边栏「备忘」页签：备忘簿、编辑对话框、删除确认
#include "../BalloonMemo/MemoCardList.h"             // 「备忘」面板的备忘卡片：重建、悬停、ctrlId 反推
#include "../../Feature/Memo/MemoClock.h"            // 本地当前时间（列表里的下一次提醒时间）
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   // 备忘列表的右键菜单
#include "../../App/FlamingoClient.h"        // CFlamingoClient::m_UserMgr（取自己 userid）
#include "ChatInputRichEdit.h"   // 输入框控件（无窗口富文本的聊天定制子类）
#include "GroupChatWindowLayout.h"    // BuildGroupChatWindowBody / GroupCtrlId
#include "ChatWindowLayout.h"         // RememberChatSplitFraction（单/群共用分割记忆）
#include "ChatImageCache.h"           // 图片后台解码缓存：注册监听 + 就绪重绘
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"  // 拖动完成后算回分割比例
#include "ChatMessageList.h"          // ChatMessageList::AppendMessage
#include "ChatDropModel.h"            // 拖进来的路径归类成 DropPlan
#include "ChatLinkDetect.h"           // IsSafeLinkUrl（打开链接前的协议白名单）
// RaiseWindowForAttention：字幕上点「回到聊天窗」时把本窗口调到前台。必须排在 <windowsx.h> 之前：
// windowsx.h 定义了函数式宏 IsMinimized(hwnd) → IsIconic(hwnd)，排在它之后时接口里的 IsMinimized
// 会被改写成 IsIconic，本文件与 WindowAttention.cpp 对同一个类的理解不一致，链接时报无法解析的外部符号
#include "WindowAttention.h"
#include <windowsx.h>                 // GET_X_LPARAM / GET_Y_LPARAM（旁听点击定焦点区域）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   // 拖放跳过项的轻量提示
#include "ChatMockData.h"             // ChatMessage / MakeTextMessage / MakeGroupAutoReply
#include "ChatFontUtil.h"             // 当前聊天字体的载入/保存/应用到输入框
#include "GroupMentionMenu.h"         // MentionMenuId / MentionActionData（气泡里 @某人 右键菜单的分发负载）
#include "ChatBannerLabel.h"          // 顶部离线 / 正在连接提示带（带底色的 DuiLabel 子类）
#include "GroupMemberOrder.h"         // OrderMemberIndicesByRole（成员侧栏：群主 → 管理员 → 普通成员）
#include "ChatQuoteModel.h"           // FillQuoteForRender（引用回复的统一入口）
#include "ChatMergedCard.h"           // ApplyMergedCard（合并转发的聊天记录画成卡片）
#include "ChatFileShareCard.h"        // ApplyFileShareCard（文件共享卡片）
#include "ChatScreenShareCard.h"      // ApplyScreenShareCard（屏幕共享卡片）
#include "ChatSystemTip.h"            // MakeSystemTipRow（系统提示行，与单聊窗共用）
#include "../../Feature/ScreenShare/ScreenShareHub.h" // 屏幕共享：发起、加入、查询与各会话的共享状态
#include "../../Feature/Voice/VoiceHub.h"                 // 语音消息：开始、停止、取消录音
#include "ChatReceivedVoice.h"                             // 语音消息：收到或翻到时做成气泡、查已听状态
#include "ChatVoiceBubble.h"                               // 语音消息：发送中气泡、能否重发、气泡动作
#include "../../net/VoiceContent.h"                        // 语音消息：发送时把波形编成 Base64
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"  // media_voice_record_ok 等开始录音的结果
#include "../../net/UserCachePath.h"                      // net::UserCacheDir（语音缓存目录）
#include "ChatShareCardBridge.h"      // MakeShareCardLiveState / RefreshShareCardsInList
#include "../../Feature/FileShare/FileShareHost.h"   // 文件共享模块是否已加载、打开共享与本会话共享浮层
#include "../../Feature/GroupCall/GroupCallHub.h"    // 群通话：发起、加入、查询与本群的群通话状态
#include "ChatGroupCallCard.h"                        // 群通话卡片的解析与套用（复用屏幕共享卡片的版式）
#include "ChatAnnouncementCard.h"                     // 群公告卡片的解析与套用（复用屏幕共享卡片的版式）
#include "GroupMustRead.h"                           // 入群必读：输入区状态判定、标记与界面文字
#include "GroupQaModel.h"                            // 问答：筛选、网址提取、发到群里的文字、权限
#include "GroupBotModel.h"                           // 机器人：成员页签的行标签、头像与资料卡内容（docs/机器人方案.md §4.2）
#include "GroupBotSection.h"          // ApplyGroupBotSection：成员页签「机器人」区域的重建与逐层重排
#include "GroupQaCardList.h"                         // 问答卡片列表
#include "../BalloonMemo/MemoCardList.h"             // kMemoScrollGutter：问答卡片视口与备忘页签共用
#include "ChatMemoCard.h"                             // 群备忘卡片的解析与套用（复用屏幕共享卡片的版式）
#include "ChatCloudRecordCard.h"                      // 云端录像卡片的解析与套用（复用屏幕共享卡片的版式）
#include "../BalloonMain/CreateGroupDlg.h"            // CCreateGroupDlg / IMemberPickSink（群通话选人）
#include "ChatIconButton.h"                          // 头部文件共享按钮的角标
#include "ChatMessageMenu.h"          // ChatImageActionData / ChatMessageMenuId（图片气泡右键动作）
#include "ChatImageSaveAs.h"          // SaveChatImageAs（图片另存为，与单聊窗共用）
#include "ChatReceivedFile.h"         // 文件气泡的构造与本地文件动作（与单聊窗共用）
#include "../BalloonMain/ForwardRequest.h"    // balloonmain::ForwardRequest（多选转发投给主面板）
#include "../BalloonMain/GroupExitFlow.h"     // balloonmain::TransferOwnerRequest / BuildTransferConfirmText（成员右键「转让群主」）
#include "GroupSideTab.h"             // GroupSideTab::SetActive
#include "SideTabText.h"              // MakeSideTabText（边栏 Tab 的"标题 + 真实数量"文案）
#include "GroupMemberList.h"          // 成员侧栏容器
#include "../BalloonMain/OverlayScrollBarGeometry.h"   // ComputeResizeBorderInsetPx：成员侧栏给窗口缩放边框让位
#include "GroupMemberItem.h"          // 成员行
#include "GroupFollowTarget.h"        // 群关注名单的查询与切换负载（FMG_MSG_QUERY_GROUP_FOLLOW / FMG_MSG_TOGGLE_GROUP_FOLLOW）
#include "GroupMemberDeptTree.h"      // 成员侧栏的部门树视图：部门树的行序列
#include "GroupDeptHeaderItem.h"      // 成员侧栏的部门树视图：部门标题行
#include "GroupFileList.h"            // 群文件列表堆叠容器（DuiScrollView 内容）
#include "GroupFileItem.h"            // 群文件行
#include "FileBubbleFormat.h"         // FormatFileSize / FormatRelativeDayTime（文件行说明文字）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView（文件列表滚动视口）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"      // 分类筛选 chips 选中态切换
#include "ChatTitleIcon.h"            // MakeRoundTitleIcon / IconFromBitmap（共用标题图标）
#include "../BalloonMain/MainPanelAvatar.h"  // MakeMonogramAvatar（任务栏图标）
#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile（群头像真图的任务栏图标）
#include "../BalloonMain/AvatarSpec.h"        // DecodeAvatar：群成员/自己气泡按 (facetype,customface) 取字形/底色
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "../BalloonHistory/V2MsgQuery.h"      // HistoryTypeFilter：打开消息记录窗时预选哪个类型胶囊
#include "../../net/IUSocket.h"          // CIUSocket::SendGroupTextMessage / RequestGroupMembers
#include "../BalloonMain/OnlineCapability.h"   // 离线门禁：连接态、提示条文案、被拦提示
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 顶部离线提示条
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiLayout::SetHint（引用条人名段按内容定宽）

// 顶部提示带配色，与单聊窗同一套值（见 BalloonChatWindow.cpp 的同名常量与说明）：
// 离线用琥珀色系（需要用户处理），正在连接用中性灰（仅告知）。每种三个色 ——
// 整条底色、文字色、下边缘分隔线色。
// 聊天字体偏好里群聊会话的会话类型（与协议 conversation_type 的群聊取值一致），
// 会话ID 为群ID。见 Session/ChatFontPrefs.h。
static const int kChatFontCtypeGroup = 2;

static const COLORREF kGroupOfflineBannerBgColor     = RGB(0xFF, 0xF4, 0xE5);
static const COLORREF kGroupOfflineBannerColor       = RGB(0xB4, 0x53, 0x09);
static const COLORREF kGroupOfflineBannerBorderColor = RGB(0xF3, 0xE0, 0xC0);

static const COLORREF kGroupConnectingBannerBgColor     = RGB(0xF5, 0xF5, 0xF6);
static const COLORREF kGroupConnectingBannerColor       = RGB(0x6B, 0x72, 0x80);
static const COLORREF kGroupConnectingBannerBorderColor = RGB(0xE5, 0xE7, 0xEB);
#include "../../net/MessageProtocolClient.h"  // net::MessageNotify / digestForMsgType
#include "../../net/GroupProtocolClient.h"    // net::GroupMembersResult
#include "../../net/Protocol.h"               // group_member_role（群主/管理员/成员角色枚举，全局命名空间）
#include "../BalloonCard/RelayDetailModel.h"  // ballooncard::RelayDetailRequest（打开接龙详情窗的请求）
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）          // net::ImageTransfer（发图工作线程）
#include "../../net/FileTransfer.h"           // net::FileTransfer（群文件上传/下载）+ GroupFileUpload/DownloadResult
#include "ChatAvatarResolve.h"             // ResolveAvatarRender（气泡 / 成员真图头像呈现解析）
#include "GroupChatHeaderInfo.h"           // 头部公告摘要行
//公告卡底部那行时间与群管理窗的历史公告列表同一个口径，复用同一个纯函数，
//不在这里另写一份（两处显示不一致比多一个依赖更糟）。
#include "../BalloonGroupManage/GroupManageMapping.h"   // FormatAnnouncementTime
#include <time.h>
#include <gdiplus.h>                       // 读图片真实宽高以算展示尺寸
#pragma comment(lib, "gdiplus.lib")
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"      // 成员搜索框
#include "ChatScreenshotMenu.h"                         // 剪刀右边 ▾ 的截图选项菜单、截图请求编号
#include "ChatRecordMenu.h"                             // 录制按钮的下拉菜单（开始录制、录制管理）
#include "../../Feature/Screenshot/ScreenshotHotkey.h"  // 剪刀悬停提示里的快捷键文字
#include "../../Feature/Screenshot/ScreenshotEntry.h"   // 全局配置里的截图快捷键与「截图时隐藏当前窗口」
#include "ChatRichContentSplit.h"     // 输入框图文混排的有序拆分（发送用）
#include "ChatSendLimit.h"            // 发送前检查单条消息体积；发送失败的原因文案
#include "ChatReedit.h"               // 「重新编辑」把原文放回输入框
#include "RecallEdit.h"               // 撤回提示行的「编辑」：快照查询、到期定时器（docs/撤回后重新编辑方案.md）
#include "ChatMemoPrefill.h"          // 「添加到我的备忘 / 群备忘」的预填内容（docs/备忘与提醒方案.md §8）
#include "GroupNotifyOnly.h"          // 仅通知模式：能否发言的判定（docs/群管理接入方案.md §14）
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
#include "ChatSendButton.h"           // ChatSendButton::DUIN_SENDBTN_DROPDOWN
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"      // 发送模式切换菜单
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"    // 提示文字 SetText
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiTrace.h"   // BUI_TRACE：打开窗口与首屏历史渲染的时序跟踪
#include "HistoryRenderBreakdown.h"                              // 首屏历史渲染按消息类型分段计时
#include <objbase.h>                  // CoCreateGuid（生成 clientmsgid）
#include <shellapi.h>                 // ShellExecuteW（群文件下载完成后打开文件）
#pragma comment(lib, "shell32.lib")
#include <set>
#include <vector>
#include <string>
#include <ctime>                      // time()（群文件相对时间的当下基准）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译
#include "../../Feature/Ocr/OcrLauncher.h"   // ocr::LaunchOcrForFile：图片右键菜单「图片文字识别」

#pragma comment(lib, "ole32.lib")     // CoCreateGuid

// ---- 文件作用域辅助函数的前置声明 ---------------------------------------

// 判定输入框里光标前一个字符是否为空白。定义在本文件靠后的位置，但插入 @提醒
// 的代码在它前面就要用，故在此前置声明。
//   hEdit：输入框（RichEdit）句柄。
//   pos：  光标位置（字符下标）。
// @return pos 为 0，或前一个字符是空白时返回 true。
static bool PrevCharIsWhitespace(balloonchatwnd::ChatInputRichEdit* edit, int pos);

// ---- 文件作用域常量 -----------------------------------------------------

// 群聊窗固定客户区尺寸（逻辑像素）—— 比单聊窗宽，右侧多一条 240px 边栏。
// 输入框里内联图片（截图、粘贴或拖入的图片）的展示上限（像素）：等比缩放到这个方框以内，
// 避免大图把输入框撑得过高。只影响展示，发送的仍是原图。
static const int kGroupInlineImageMaxPx = 320;

// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 880×620 加上
// 修复前多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小；2026-10-06 按用户要求
// 由 898×667 调大为 1000×720（工具栏加了「代码」按钮、输入区也调高了，见 docs/代码片段方案.md 第 3.1 节）。
static const int kGroupChatWindowWidth  = 1000;
static const int kGroupChatWindowHeight = 720;

// 群聊窗允许缩放到的最小尺寸见 GroupChatWindowLayout.h 的 kGroupChatWindowMinWidth /
// kGroupChatWindowMinHeight（布局单测要按最小宽度检查头部，故放在布局头文件里）。

// 标题栏高度（逻辑像素）—— 常规标题栏，承载 min/max/close。
static const int kGroupChatWindowTitleBarHeight = 32;

// mock 自动回复定时器 id 与延时（毫秒）—— 发出消息后过一会儿模拟群成员回话。
static const UINT_PTR kReplyTimerId = 0xC101;
static const UINT     kReplyDelayMs = 1400;

// 「打开链接」延后执行的定时器 id。理由与单聊窗同一处注释：点链接不立刻打开，
// 先等一个系统双击间隔，期间收到双击就把这次打开作废（双击的语义是选中整条）。
static const UINT_PTR kLinkOpenTimerId = 0xC104;

// 接龙卡片截止定时器 id。一次性定时器，安排在最早的那个截止时间，到时把卡片重画为已截止
// （服务端到截止时间不发推送，客户端自己判断，见 docs/群接龙方案.md §3.5）。
static const UINT_PTR kRelayExpiryTimerId = 0xC105;
// 截止定时器最短等多久（毫秒）：截止时间已过或就在眼前时也至少等这么久，避免定时器连续触发
static const long long kRelayExpiryMinDelayMs = 1000;
// 截止定时器最长等多久（毫秒）：截止时间很远时每小时检查一次，本机时间被调整之后也不会错过太久
static const long long kRelayExpiryMaxDelayMs = 3600LL * 1000;

// 「备忘」页签的重画定时器 id。一次性定时器，安排在卡片上最早的那个提醒时刻：到了这一分钟，那张卡片的
// 时间要换成下一次事件或「已结束」。本人不是被提醒的人时备忘中枢在这个时刻不会通知本窗口，所以由本窗口自己安排
static const UINT_PTR kMemoPanelRefreshTimerId = 0xC106;
// 撤回提示行「编辑」的到期定时器 id（docs/撤回后重新编辑方案.md §3.3）。时长按最早的到期时刻现算，
// 见 balloonchatwnd::RescheduleRecallEditTimer。
static const UINT_PTR kRecallEditTimerId = 0xC107;
// 重画定时器在那一分钟开始之后再多等多久（毫秒），保证重画时已经进入那一分钟
static const long long kMemoPanelRefreshSlackMs = 500;
// 重画定时器最长等多久（毫秒）：最早的提醒很远时每小时重画一次，本机时间被调整之后也不会错过太久
static const long long kMemoPanelRefreshMaxDelayMs = 3600LL * 1000;

// 群聊窗不掌握本人的禁言状态：不记全员禁言，个人禁言的变化也不推送到本窗口。接龙卡片因此按
// 未禁言显示按钮，被禁言的人点了参与之后由服务端以 306 拒绝，详情窗据应答提示原因
static const bool kRelaySelfMutedUnknown = false;

// 消息类型常量，与 net::message_type（net/Protocol.h）对齐。
static const int kMsgTypeText  = 1;   // message_type_text
static const int kMsgTypeImage = 2;   // message_type_image
static const int kMsgTypeFile  = 3;   // message_type_file：来自转发，或宿主「发送到会话」发来的文件（上传群文件后再发，2026-10-05）
static const int kMsgTypeVoice = 4;   // message_type_voice：语音消息，画成语音气泡（docs/语音消息方案.md）
static const int kMsgTypeMerged = 15; // message_type_merged：合并转发的聊天记录，画成卡片
static const int kMsgTypeRelay  = 11; // message_type_relay：群接龙卡片，画成卡片，见 docs/群接龙方案.md §3.2
static const int kMsgTypeScreenShare = 19; // message_type_screenshare：屏幕共享卡片（服务端在发起共享时生成），画成卡片
static const int kMsgTypeRtc = 16;         // message_type_rtc：通话记录，目前只有群通话卡片（服务端在发起群通话时生成），画成卡片
static const int kMsgTypeFileShare = 20; // message_type_fileshare：文件共享卡片（服务端生成），画成卡片，见 docs/文件共享方案.md 7.3
// 入群必读卡片发送人头像的底色（系统发出的卡片没有真实发送人，用固定的蓝色）
static const COLORREF kMustReadAvatarColor = RGB(0x45, 0x6C, 0xDF);
static const int kMsgTypeAnnouncement = 22; // message_type_announcement：群公告卡片（服务端以发布人名义生成），画成卡片，见 docs/群公告接入方案.md §13.4
static const int kMsgTypeCloudRecord = 24;  // message_type_cloudrecord：云端录像卡片（服务端以开启者名义生成），画成卡片，见 docs/屏幕共享与录制方案.md 12.6.5
static const int kMsgTypeMemo = 23;         // message_type_memo：群备忘卡片（服务端以操作人名义生成，只给接收者看），画成卡片，见 docs/备忘与提醒方案.md §7.3
static const int kMsgTypeSystem = 17; // message_type_system：服务端生成的系统提示（发布公告、入群欢迎语、退出群聊、转让群主），画成居中提示行

// 群通话一次最多邀请几个人：服务端人数上限默认 16（含发起人，docs/协议设计-v2.md 21.7），选多了先在本地提示，
// 免得服务端整单拒绝（724）。服务端改了上限时以服务端为准，超出的同样会被拒绝
static const size_t kGroupCallMaxInvitees = 15;

/**
 *  群通话选人框的结果接收方：把选人框确认的结果转给群聊窗。由群聊窗持有，选人框关闭之前一直有效。
 */
class GroupCallPickSink : public IMemberPickSink
{
public:
    /**
     *  @param owner 所属群聊窗（不拥有），须比本对象活得久
     */
    explicit GroupCallPickSink(CBalloonGroupChatWindow* owner)
        : m_owner(owner)
    {
    }

    //选人框确认：转给群聊窗
    virtual void OnMembersPicked(const std::vector<int64_t>& userIds) override
    {
        if (m_owner != NULL)
        {
            m_owner->OnGroupCallMembersPicked(userIds);
        }
    }

private:
    CBalloonGroupChatWindow* m_owner;   // 所属群聊窗（不拥有）
};

// CString(UTF-16) → UTF-8 std::string。空串安全。
static std::string CStringToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
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
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// 群文件面板每页条数（逻辑常量）。设计稿呈现分页（共 N · 第 x/y 页），此处取 6：
// 一页约填满边栏可视高度、且 14 条样本正好分 3 页与设计稿一致。后续如需更大页可改此值。
static const int kGroupFilePageSize = 6;

// 「拉全量」每次向服务端请求的页大小。服务端 cmd_get_group_files 单页上限 50
// （GROUP_FILE_PAGE_MAX），取满以最少往返拉全。
static const int kGroupFileFetchPageSize = 50;

// 公告边栏一次拉几条。边栏一次只显示一条，故取 1：翻到哪条要哪条，公告有
// 几千条也不受服务端单页上限影响，本地也只留一条。
static const int kNoticeFetchPageSize = 1;

// 当前时间，Unix 毫秒。接龙卡片按它判断是否已截止、格式化截止时间。
static int64_t NowMillisecondsForRelay()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

// 当前时间，Unix毫秒。公告卡底部那行"发布人 · 时间"按这个口径算相对日期。
static int64_t NowMillisecondsForNotice()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

// 构造一批 mock 群文件条目，仅用于调试入口（--groupchatwindow，无真实群）目检面板。
// 覆盖各扩展名（决定彩色角标 + 分类筛选）与各上传时间（决定相对时间显示：今天/昨天/
// 周X/日期）。真实群的文件由 cmd_get_group_files 应答填充，不走本函数。
// i18n-ignore-begin: 调试入口的模拟群文件列表
static std::vector<net::GroupFileEntry> MockGroupFileEntries()
{
    // 以当前时刻为基准造各种"多久以前"的上传时间。
    long long nowMs = (long long)time(NULL) * 1000LL;
    const long long kHour = 3600LL * 1000;
    const long long kDay  = 24 * kHour;

    struct Spec
    {
        const char* entryid;
        const char* name;
        const char* ext;
        const char* uploader;
        long long   size;
        long long   uploadMs;
    };
    Spec specs[] =
    {
        { "101", "v3.2-发布计划.xlsx",   "xlsx", "谢知行", 131072LL,   nowMs - 2 * kHour  },
        { "102", "登录页 final.fig",      "fig",  "我",     13002342LL, nowMs - 5 * kHour  },
        { "103", "群文件功能 PRD.docx",   "docx", "方亦初", 90112LL,    nowMs - 1 * kDay   },
        { "104", "品牌色规范.pdf",        "pdf",  "江屿白", 1887436LL,  nowMs - 3 * kDay   },
        { "105", "API 接口文档 v3.docx",  "docx", "宋微之", 350208LL,   nowMs - 3 * kDay   },
        { "106", "用户研究报告.pdf",      "pdf",  "许清欢", 5872025LL,  nowMs - 20 * kDay  },
        { "107", "原型交互稿.sketch",     "sketch","林知夏", 9646899LL,  nowMs - 24 * kDay  },
        { "108", "测试用例清单.xlsx",     "xlsx", "周慎",   65536LL,    nowMs - 26 * kDay  },
        { "109", "数据导出.csv",          "csv",  "我",     20480LL,    nowMs - 30 * kDay  },
        { "110", "发布会演示.pptx",       "pptx", "高叙",   4194304LL,  nowMs - 33 * kDay  },
        { "111", "首页大图.png",          "png",  "苏晚",   2359296LL,  nowMs - 40 * kDay  },
        { "112", "源码归档.zip",          "zip",  "陈聿",   41943040LL, nowMs - 60 * kDay  },
        { "113", "需求评审纪要.txt",      "txt",  "我",     8192LL,     nowMs - 90 * kDay  },
        { "114", "旧版设计稿.psd",        "psd",  "黎照",   31457280LL, nowMs - 400 * kDay },
    };

    std::vector<net::GroupFileEntry> out;
    const int kCount = (int)(sizeof(specs) / sizeof(specs[0]));
    for (int i = 0; i < kCount; ++i)
    {
        net::GroupFileEntry e;
        e.m_entryid      = specs[i].entryid;
        e.m_fileid       = specs[i].entryid;   // mock：文件ID 借用条目ID
        e.m_name         = specs[i].name;
        e.m_ext          = specs[i].ext;
        e.m_size         = specs[i].size;
        e.m_uploadername = specs[i].uploader;
        e.m_uploadtime   = specs[i].uploadMs;
        out.push_back(e);
    }
    return out;
}
// i18n-ignore-end

// 在群文件列表（GroupFileList）里按 entryid 找到对应的文件行控件；找不到返回 NULL
// （该条目不在当前页 / 列表未建时）。下载进度回投后据此定位那一行就地更新进度。
static balloonchatwnd::GroupFileItem* FindGroupFileItemByEntryId(
    balloonwjui::DuiControl* content, const CString& entryid)
{
    if (content == NULL || entryid.IsEmpty())
    {
        return NULL;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return NULL;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonchatwnd::GroupFileItem* item =
            dynamic_cast<balloonchatwnd::GroupFileItem*>(kids[i].get());
        if (item != NULL && item->EntryId() == entryid)
        {
            return item;
        }
    }
    return NULL;
}

// 在群文件列表里按 uploadId 找到对应的「上传中」行控件；找不到返回 NULL。上传进度
// 回投后据此定位顶部那一行就地更新进度。
static balloonchatwnd::GroupFileItem* FindGroupFileUploadItem(
    balloonwjui::DuiControl* content, int uploadId)
{
    if (content == NULL || uploadId <= 0)
    {
        return NULL;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return NULL;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonchatwnd::GroupFileItem* item =
            dynamic_cast<balloonchatwnd::GroupFileItem*>(kids[i].get());
        if (item != NULL && item->UploadId() == uploadId)
        {
            return item;
        }
    }
    return NULL;
}

// 按 userid 从固定调色板挑头像底色（与联系人/单聊同口径，保证同一人颜色稳定）。
static COLORREF AvatarColorForUser(int64_t userid)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x7A, 0x45), RGB(0x3A, 0x86, 0xD9), RGB(0x2B, 0xB6, 0x73),
        RGB(0x8B, 0x5C, 0xF6), RGB(0xEF, 0x6C, 0x9A), RGB(0xF5, 0x9E, 0x0B),
        RGB(0x14, 0xB8, 0xA6), RGB(0xE1, 0x5B, 0x4C),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    int idx = (int)(((userid % n) + n) % n);
    return kPalette[idx];
}

// 把一条"自己发"的群消息气泡的发送者头像填成本端真实头像（字形 / 真图，按本端
// (facetype, customface) 解析；真图未缓存会入队下载，完成后 OnAvatarDownloaded 回刷）。
// 群聊里自己气泡不显示发送者名，故不设 senderName。
static void FillSelfSenderAvatar(CFlamingoClient* client, balloonchatwnd::ChatMessage& msg)
{
    int64_t     selfId = 0;
    int         facetype = 0;
    std::string customface;
    CString     name = TR(L"我");
    if (client != NULL)
    {
        const CBuddyInfo& me = client->m_UserMgr.m_UserInfo;
        selfId   = (int64_t)me.m_uUserID;
        facetype = (int)me.m_nFace;   // v2 facetype 存在 m_nFace
        customface.assign(me.m_strCustomFaceName.GetString(),
                          (size_t)me.m_strCustomFaceName.GetLength());   // customface UTF-8 字节
        if (!me.m_strNickName.empty())
        {
            name = me.m_strNickName.c_str();
        }
    }
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        selfId, facetype, customface, name, selfId);
    msg.senderUserId          = selfId;
    msg.senderAvatarText      = ar.m_glyph;
    msg.senderAvatarColor     = ar.m_color;
    msg.senderAvatarImagePath = ar.m_imagePath;
}

// 生成一个客户端消息ID（去横杠的 GUID 十六进制串，纯 ASCII）。
static std::string GenClientMsgId()
{
    GUID guid = { 0 };
    if (::CoCreateGuid(&guid) != S_OK)
    {
        return std::string("cmid");
    }
    char buf[40] = { 0 };
    sprintf_s(buf, sizeof(buf), "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
              guid.Data1, guid.Data2, guid.Data3,
              guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

// 图片气泡内图片的最大展示尺寸（逻辑像素）：超出按比例缩小，小图保持原尺寸。
// 与单聊同口径（见 BalloonChatWindow.cpp）。
static const int kImageDisplayMaxW = 220;
static const int kImageDisplayMaxH = 260;

// 把图片真实宽高按比例缩到不超过 (kImageDisplayMaxW, kImageDisplayMaxH) 的展示尺寸；
// 输出至少 1×1。realW/realH 非正时回退到一个占位方块尺寸。
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

// EnumContent 的逐段收集回调：把（文本 / 内嵌图+标记）按文档顺序
// 收进 vector，供 BuildOutgoingParts 聚合成待发送部分。与单聊同口径。ctx 为收集目标。
static void CollectGroupInputSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
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

CBalloonGroupChatWindow::CBalloonGroupChatWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_pendingMemoCardId(0)
    , m_hMainDlg(NULL)
    , m_replyIndex(0)
    // kConnUiOnline：能开出群聊窗说明此刻本就在线，随后由主面板把真实状态推过来。
    // 初始化顺序须与声明顺序一致（本成员声明在 m_groupId 之前）。
    , m_connUiState(0)
    , m_groupId(0)
    , m_myRole(0)
    , m_atAllScope(balloonchatwnd::kGroupAtAllOwnerAdminVal)
    , m_muteAll(0)
    , m_notifyOnlyShown(-1)
    , m_mustReadPending(false)
    , m_groupBotsLoaded(false)
    , m_mustReadId(0)
    , m_mustReadHasItem(false)
    , m_mustReadPopupShown(false)
    , m_qaState(balloonchatwnd::kQaPanelLoading)
    , m_qaRequested(false)
    , m_qaRenderedWidth(0)
    , m_hostPanel(NULL)
    , m_pendingLocateMsgid(0)
    , m_pinnedCount(0)            //开窗先按"一条都没钉"起步，提示带隐藏；随后由服务端全集校准
    , m_shareBannerSelfHost(false)
    , m_shareBannerForced(false)
    , m_callPickMedia(0)
    , m_callInvitePending(false)
    , m_titleIconBmp(NULL)
    , m_hTaskIcon(NULL)
    , m_groupFacetype(0)          //默认自动头像（群名首字 + 按 groupid 取色）
    , m_hInputFont(NULL)
    , m_inputDefaultTextColor(RGB(0, 0, 0))
    , m_fileCategory(balloonchatwnd::kGroupFileCatAll)
    , m_filePage(0)
    , m_fileLoaded(false)
    , m_fileLoading(false)
    , m_mentionAtPos(-1)   // -1 = 当前不处于 @ 输入状态
    , m_focusArea(CHAT_FOCUS_INPUT)   // 开窗时焦点在输入框
    , m_dropToast(NULL)    // 提示条在 InitUi 里建好后才有值
    , m_noticeIndex(0)
    , m_noticeTotal(0)
    , m_noticeLoaded(false)
    , m_noticeLoading(false)
    , m_noticeReqOffset(-1)   //-1=还没发过拉公告的请求，此时不认领任何应答
{
    // 成员列表的视图在 InitUi 里按账号配置设好；放在函数体里赋值，免得与成员的声明顺序对不上
    m_memberTreeView = false;
}

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


void CBalloonGroupChatWindow::SetGroupId(int64_t groupId)
{
    m_groupId = groupId;
}

void CBalloonGroupChatWindow::SetGroupName(LPCTSTR name)
{
    m_groupName = (name != NULL) ? name : _T("");
    // 已显示则即时刷新标题（桌面字幕上的群名由主面板直接更新，字幕由主面板持有）。群名只显示在标题栏，头部左侧的信息块不再重复画它（2026-10-03 起）。
    if (IsWindow() && !m_groupName.IsEmpty())
    {
        SetTitle(m_groupName);
    }
}

void CBalloonGroupChatWindow::SetGroupAvatar(int facetype, const std::string& customfaceUtf8)
{
    //值没变就不必重建图标 —— 群资料应答与变更推送都会调过来，多数时候头像并没改。
    if (m_groupFacetype == facetype && m_groupCustomface == customfaceUtf8)
    {
        return;
    }

    m_groupFacetype   = facetype;
    m_groupCustomface = customfaceUtf8;

    //窗口还没建好时只记值，CreateAndShow 里会调 RebuildGroupIcons 用上它。
    if (IsWindow())
    {
        RebuildGroupIcons();
    }
}

void CBalloonGroupChatWindow::RebuildGroupIcons()
{
    if (m_groupId == 0)
    {
        return;   // 调试入口不设图标
    }

    //按群的 (facetype, customface) 解出该画什么：字形 / 自动头像给字形 + 底色；
    //真图命中本地缓存给路径（未命中会入队下载，下好后 OnAvatarDownloaded 回刷）。
    //取色 id 用 groupid，与联系人树、会话列表里的群头像同口径。
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        m_groupId, m_groupFacetype, m_groupCustomface,
        m_groupName.IsEmpty() ? NULL : (LPCTSTR)m_groupName, selfId);
    const bool useImage = !ar.m_imagePath.IsEmpty();

    //标题栏图标：先释放旧位图再重建（48px 生成，标题栏 StretchBlt 缩到 16px，
    //放大生成再缩更平滑）。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (useImage)
    {
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIconFromImage(
            (LPCWSTR)ar.m_imagePath, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp == NULL)
    {
        //没有真图、或真图解码失败：回退字形。
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIcon(
            ar.m_glyph, ar.m_color, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    //任务栏 / Alt-Tab 图标：先毁旧 HICON，再用 32px 位图重建。
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    HBITMAP bmp32 = NULL;
    if (useImage)
    {
        //圆形真图（圆角半径 = 半边长即圆，与联系人树一致）。
        bmp32 = balloonmain::MakeRoundedAvatarFromFile((LPCWSTR)ar.m_imagePath, 32, 16);
    }
    if (bmp32 == NULL)
    {
        bmp32 = balloonmain::MakeMonogramAvatar(ar.m_glyph, ar.m_color, 32);
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

void CBalloonGroupChatWindow::SetHostPanelHwnd(HWND hostPanel)
{
    m_hostPanel = hostPanel;
}

void CBalloonGroupChatWindow::SetMemberPresence(int64_t userid, int presence)
{
    m_memberPresence[userid] = presence;
    if (!IsWindow())
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupMemberList* memberList =
        dynamic_cast<balloonchatwnd::GroupMemberList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMemberList));
    if (memberList != NULL)
    {
        memberList->SetMemberPresence(userid, presence);
    }

    // 消息区里该成员发的气泡头像随之变灰 / 恢复彩色。本人的气泡只看本端连接态，不在这里处理
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    if (userid != selfId)
    {
        balloonchatwnd::ChatMessageList* msgList = dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
        if (msgList != NULL)
        {
            msgList->SetSenderAvatarGray(userid, balloonmain::ShouldGrayUserAvatar(false, presence, m_connUiState));
        }
    }
}

void CBalloonGroupChatWindow::ApplyAvatarGrayToMessageList()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* msgList = dynamic_cast<balloonchatwnd::ChatMessageList*>(
        content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (msgList == NULL)
    {
        return;
    }
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    for (std::map<int64_t, int>::const_iterator it = m_memberPresence.begin();
         it != m_memberPresence.end(); ++it)
    {
        if (it->first != selfId)
        {
            msgList->SetSenderAvatarGray(it->first,
                                         balloonmain::ShouldGrayUserAvatar(false, it->second, m_connUiState));
        }
    }
    msgList->SetSelfAvatarGray(balloonmain::ShouldGrayAvatar(m_connUiState));
}

void CBalloonGroupChatWindow::SetMyRole(int role)
{
    //「未确认必读」标记只给群主与管理员看：本人是否为管理员这一点变了，成员边栏要按新角色重建
    const bool wasAdmin = balloonchatwnd::ShouldShowMustReadMark(m_myRole, 1);
    m_myRole = role;
    const bool isAdmin = balloonchatwnd::ShouldShowMustReadMark(m_myRole, 1);
    if (IsWindow() && wasAdmin != isAdmin && !m_lastMembers.empty())
    {
        RebuildMemberRows();
    }
    if (IsWindow())
    {
        ApplyAdminGearVisibility();
        //角色变了，能不能在仅通知模式下发言也随之变化
        ApplyNotifyOnlyState();
        //本人角色变了，气泡 @ 菜单里的管理项跟着变，故同步给消息区；成员侧栏各行的
        //右键菜单同理，也要当场同步。不能指望随后的成员列表重填：角色变更时主面板先发
        //成员列表请求、后发群资料请求，成员列表先到时各行按旧角色重建，之后群资料到达
        //只调到这里，侧栏会一直停在旧角色上
        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* msgList =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (msgList != NULL)
            {
                msgList->SetMyNetRole(m_myRole);
                //「添加到群备忘」只给能新建群备忘的人（docs/备忘与提醒方案.md §8），角色变化时一并更新
                msgList->SetMemoMenuMode(balloonmemo::CanEditGroupMemo(m_myRole)
                                             ? balloonchatwnd::kChatMemoMenuGroupBoth
                                             : balloonchatwnd::kChatMemoMenuGroupMine);
            }
            balloonchatwnd::GroupMemberList* memberList =
                dynamic_cast<balloonchatwnd::GroupMemberList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMemberList));
            if (memberList != NULL)
            {
                memberList->SetMyNetRoleForAll(m_myRole);
            }
        }
        //「备忘」面板的「＋ 新建群备忘」只对群主、管理员显示，角色变了跟着重画
        RenderMemoPanel();
        //「问答」面板同理：「＋ 新建问答」与卡片上的「修改」「删除」只给群主、管理员
        RenderQaPanel();
    }
}

void CBalloonGroupChatWindow::SetAtAllScope(int scope)
{
    m_atAllScope = scope;
}

void CBalloonGroupChatWindow::SetMuteAll(int muteall)
{
    m_muteAll = (muteall != 0) ? 1 : 0;
    if (IsWindow())
    {
        ApplyNotifyOnlyState();
    }
}

bool CBalloonGroupChatWindow::IsNotifyOnlyForMe() const
{
    return !balloonchatwnd::CanSpeakInGroup(m_muteAll, m_myRole);
}

void CBalloonGroupChatWindow::ApplyNotifyOnlyState()
{
    //仅通知模式与入群必读共用同一行提示：待确认入群必读时显示可点的「点此阅读」，否则显示仅通知的灰字
    const int state = balloonchatwnd::DecideGroupInputBlock(m_muteAll, m_myRole, m_mustReadPending);
    if (state == m_notifyOnlyShown)
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    const bool blocked = (state != balloonchatwnd::kGroupInputOpen);
    if (blocked)
    {
        //待发的引用随输入框一起看不见了，留着的话恢复发言后第一条消息会带上一个看不见的引用
        CancelPendingReply();
        //焦点移开已隐藏的输入框
        SetDuiFocus(NULL);
    }
    balloonwjui::DuiLabel* bar = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNotifyOnlyBar));
    if (bar != NULL)
    {
        if (state == balloonchatwnd::kGroupInputMustRead)
        {
            bar->SetText(balloonchatwnd::MustReadInputHintText());
            bar->SetMode(balloonwjui::DuiLabel::ModeLink);
        }
        else
        {
            bar->SetText(balloonchatwnd::NotifyOnlyInputHintText());
            bar->SetMode(balloonwjui::DuiLabel::ModeText);
        }
    }
    if (!balloonchatwnd::ApplyNotifyOnlyInput(content, blocked))
    {
        return;
    }
    m_notifyOnlyShown = state;
    LOG_INFO("group chat window: input block state %d, group=%lld, muteall=%d, role=%d, mustread pending=%d.", state,
             (long long)m_groupId, m_muteAll, m_myRole, m_mustReadPending ? 1 : 0);
    InvalidateRect(NULL, FALSE);
}

void CBalloonGroupChatWindow::RefreshMustRead()
{
    if (m_groupId == 0)
    {
        return;
    }
    CIUSocket::GetInstance().SendGetMustRead(m_groupId);
}

void CBalloonGroupChatWindow::OnMustReadArrived(const net::MustReadResult& result)
{
    if (result.m_code != 0)
    {
        LOG_WARNING("group chat window: get must-read failed, group=%lld, code=%d.", (long long)m_groupId, result.m_code);
        return;
    }
    m_mustReadPending = result.m_pending;
    m_mustReadId = result.m_mustreadid;
    m_mustReadHasItem = result.m_hasAnnouncement;
    if (result.m_hasAnnouncement)
    {
        m_mustReadItem = result.m_announcement;
    }
    LOG_INFO("group chat window: must-read state, group=%lld, mustreadid=%lld, pending=%d, has item=%d.",
             (long long)m_groupId, (long long)m_mustReadId, m_mustReadPending ? 1 : 0, m_mustReadHasItem ? 1 : 0);
    if (IsWindow())
    {
        ApplyNotifyOnlyState();
    }

    //已确认（或必读被取消）：弹窗若还开着就收起来，免得再点「我已阅读」
    if (!m_mustReadPending && m_mustReadDlg.IsWindow() && m_mustReadDlg.IsWindowVisible())
    {
        m_mustReadDlg.ShowWindow(SW_HIDE);
    }

    if (balloonchatwnd::ShouldAutoPopupMustRead(m_mustReadPending, m_mustReadHasItem, m_mustReadPopupShown)
        && IsWindow())
    {
        m_mustReadPopupShown = true;
        PostMessage(kMustReadShowMsg, 0, 0);
    }
}

void CBalloonGroupChatWindow::ShowMustReadPopup()
{
    if (!m_mustReadHasItem)
    {
        //内容还没取到：再查一次，应答到达后若仍待确认会按 m_mustReadPopupShown 决定是否弹出
        m_mustReadPopupShown = false;
        RefreshMustRead();
        return;
    }
    const CString timeText = balloongroupmanage::FormatAnnouncementTime(m_mustReadItem.m_publishtime,
                                                                       NowMillisecondsForNotice());
    m_mustReadDlg.SetCaption(balloonchatwnd::MustReadPopupCaption());
    //以本窗口为所有者：弹窗总在群聊窗之上。此前没有所有者，打开群聊窗时弹窗被随后激活的群聊窗压在下面（2026-10-08 实测）
    m_mustReadDlg.SetOwnedByHost(true);
    //待确认时按钮是「我已阅读」；已确认的人点开必读卡片时按普通公告详情显示
    m_mustReadDlg.SetMustReadMode(m_mustReadPending, kMustReadConfirmMsg);
    m_mustReadDlg.ShowAnnouncement(m_hWnd, (int64_t)m_groupId, m_groupName, Utf8ToCString(m_mustReadItem.m_title),
                                   Utf8ToCString(m_mustReadItem.m_content), m_mustReadItem.m_severity,
                                   Utf8ToCString(m_mustReadItem.m_publishername), timeText);
    LOG_INFO("group chat window: must-read popup shown, group=%lld, announcement=%lld, pending=%d.",
             (long long)m_groupId, (long long)m_mustReadItem.m_id, m_mustReadPending ? 1 : 0);
}

LRESULT CBalloonGroupChatWindow::OnMustReadConfirmMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                                      BOOL& /*bHandled*/)
{
    if (m_groupId == 0)
    {
        return 0;
    }
    //界面不在这里乐观恢复：服务端确认成功后推 1317（changetype 7），主面板据此让本窗口重查一次再恢复输入区
    CIUSocket::GetInstance().SendConfirmMustRead(m_groupId);
    LOG_INFO("group chat window: must-read confirm sent, group=%lld.", (long long)m_groupId);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMustReadShowMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                                   BOOL& /*bHandled*/)
{
    ShowMustReadPopup();
    return 0;
}

// ---------------- 边栏「问答」页签（docs/群问答方案.md） ----------------

void CBalloonGroupChatWindow::RequestGroupQa()
{
    if (m_groupId == 0)
    {
        return;
    }
    m_qaRequested = true;
    if (m_qaItems.empty())
    {
        m_qaState = balloonchatwnd::kQaPanelLoading;
    }
    CIUSocket::GetInstance().SendGetGroupQa(m_groupId);
}

void CBalloonGroupChatWindow::OnGroupQaArrived(const net::GroupQaResult& result)
{
    if (result.m_code != 0)
    {
        LOG_WARNING("group chat window: get group qa failed, group=%lld, code=%d.", (long long)m_groupId, result.m_code);
        //已有列表时保留旧内容，只在还没有内容时显示「没有加载出来」
        if (m_qaItems.empty())
        {
            m_qaState = balloonchatwnd::kQaPanelFailed;
        }
    }
    else
    {
        m_qaItems = result.m_items;
        m_qaState = balloonchatwnd::kQaPanelLoaded;
        LOG_INFO("group chat window: group qa arrived, group=%lld, count=%u.", (long long)m_groupId,
                 (unsigned)m_qaItems.size());
    }
    RenderQaPanel();
}

void CBalloonGroupChatWindow::OnGroupQaChanged()
{
    if (!m_qaRequested)
    {
        return;
    }
    RequestGroupQa();
}

void CBalloonGroupChatWindow::RenderQaPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* panel = content->FindCtrlById(balloonchatwnd::kGIdQaPanel);
    if (panel == NULL || !panel->IsVisible())
    {
        return;
    }
    //卡片外面的底色：边栏是聊天窗根控件的白底
    const COLORREF kQaListBg = RGB(0xFF, 0xFF, 0xFF);
    balloonwjui::DuiScrollView* list =
        dynamic_cast<balloonwjui::DuiScrollView*>(content->FindCtrlById(balloonchatwnd::kGIdQaList));
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(balloonchatwnd::kGIdQaEmpty));
    balloonwjui::DuiEdit* search = dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(balloonchatwnd::kGIdQaSearch));
    balloonwjui::DuiControl* add = content->FindCtrlById(balloonchatwnd::kGIdQaAdd);

    const std::wstring keyword = (search != NULL) ? std::wstring((LPCWSTR)search->GetText()) : std::wstring();
    m_qaShown = balloonchatwnd::FilterGroupQa(m_qaItems, keyword);

    const int64_t now = NowMillisecondsForNotice();
    std::vector<balloonchatwnd::QaCardText> cards;
    for (size_t i = 0; i < m_qaShown.size(); ++i)
    {
        const net::GroupQaItem& item = m_qaItems[m_qaShown[i]];
        balloonchatwnd::QaCardText card;
        card.m_question = std::wstring((LPCWSTR)Utf8ToCString(item.m_question));
        card.m_answer = std::wstring((LPCWSTR)Utf8ToCString(item.m_answer));
        card.m_links = balloonchatwnd::ExtractQaUrls(card.m_answer, balloonchatwnd::kQaMaxLinksPerCard);
        card.m_editable = balloonchatwnd::CanEditQaItem(m_myRole, item.m_source);
        if (item.m_source == net::kGroupQaSourceEnterprise)
        {
            card.m_meta = TR(L"企业问答");
        }
        else
        {
            const CString when = balloongroupmanage::FormatAnnouncementTime(item.m_updatetime, now);
            card.m_meta = item.m_editorname.empty()
                          ? TRF(L"修改于 {0}", when)
                          : TRF(L"{0} 修改于 {1}", Utf8ToCString(item.m_editorname), when);
        }
        cards.push_back(card);
    }
    const int built = balloonchatwnd::SetQaCards(list, cards, kQaListBg);
    if (list != NULL && built >= 0)
    {
        const RECT rc = list->GetRect();
        m_qaRenderedWidth = rc.right - rc.left;
    }

    //空状态提示：有内容时隐藏，让出高度给卡片
    const std::wstring hint = balloonchatwnd::QaEmptyHintText(m_qaState, m_qaItems.size(), m_qaShown.size(),
                                                              !balloonchatwnd::TrimQaText(keyword).empty());
    bool relayout = false;
    if (empty != NULL)
    {
        empty->SetText(hint.c_str());
        const bool showEmpty = !hint.empty();
        if (empty->IsVisible() != showEmpty)
        {
            empty->SetVisible(showEmpty);
            relayout = true;
        }
    }
    if (add != NULL)
    {
        const bool showAdd = (m_groupId != 0) && balloonchatwnd::CanAddQa(m_myRole);
        if (add->IsVisible() != showAdd)
        {
            add->SetVisible(showAdd);
            relayout = true;
        }
    }
    //SetVisible 不会重排：显隐变了就对面板按当前矩形 ForceLayout（开发约定文档 UI 约定 5），再按新的视口宽度重建卡片
    if (relayout)
    {
        panel->ForceLayout(panel->GetRect());
        if (list != NULL)
        {
            const RECT rc = list->GetRect();
            if (rc.right - rc.left != m_qaRenderedWidth)
            {
                balloonchatwnd::SetQaCards(list, cards, kQaListBg);
                m_qaRenderedWidth = rc.right - rc.left;
            }
        }
        panel->Invalidate();
    }
}

void CBalloonGroupChatWindow::PostQaCardAction(int action, int cardIndex)
{
    if (action != kQaCardActionAdd)
    {
        if (cardIndex < 0 || (size_t)cardIndex >= m_qaShown.size() || m_qaShown[cardIndex] >= m_qaItems.size())
        {
            return;
        }
        m_pendingQaItem = m_qaItems[m_qaShown[cardIndex]];
    }
    PostMessage(kQaCardActionMsg, (WPARAM)action, 0);
}

LRESULT CBalloonGroupChatWindow::OnQaCardActionMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int action = (int)wParam;
    const net::GroupQaItem item = m_pendingQaItem;
    const std::wstring question((LPCWSTR)Utf8ToCString(item.m_question));
    const std::wstring answer((LPCWSTR)Utf8ToCString(item.m_answer));
    switch (action)
    {
    //「发到群里」：与按发送键同一道在线检查，问答以一条文字消息发出（仅通知模式、入群必读由服务端判断）
    case kQaCardActionSend:
        if (!CheckOnlineFor(balloonmain::kActionSendMessage))
        {
            break;
        }
        SendPlainText(balloonchatwnd::BuildQaSendText(question, answer));
        LOG_INFO("group chat window: qa sent to group, group=%lld, qa=%lld.", (long long)m_groupId, (long long)item.m_id);
        break;

    //「修改」：只有能修改的卡片才有这个按钮，这里再按角色判断一次（角色可能刚刚变过）
    case kQaCardActionEdit:
        if (balloonchatwnd::CanEditQaItem(m_myRole, item.m_source))
        {
            m_qaEditDlg.Open(m_hWnd, item.m_id, question, answer, kQaEditResultMsg);
        }
        break;

    //「删除」：确认后发送删除请求，界面等 1339 推送后重拉
    case kQaCardActionDelete:
        if (!balloonchatwnd::CanEditQaItem(m_myRole, item.m_source))
        {
            break;
        }
        if (::MessageBox(m_hWnd, TRF(L"确定删除问答「{0}」吗？", question).c_str(), TR(L"删除问答"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            break;
        }
        if (!CheckOnlineFor(balloonmain::kActionSendMessage))
        {
            break;
        }
        CIUSocket::GetInstance().SendDeleteGroupQa(m_groupId, item.m_id);
        break;

    //「＋ 新建问答」
    case kQaCardActionAdd:
        if (balloonchatwnd::CanAddQa(m_myRole))
        {
            m_qaEditDlg.Open(m_hWnd, 0, std::wstring(), std::wstring(), kQaEditResultMsg);
        }
        break;

    //未知操作：不处理
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnQaEditResultMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为编辑窗堆上分配的结果，用完在这里释放
    std::unique_ptr<balloonchatwnd::QaEditResult> result((balloonchatwnd::QaEditResult*)lParam);
    if (!result || m_groupId == 0)
    {
        return 0;
    }
    if (!CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return 0;
    }
    CIUSocket::GetInstance().SendSaveGroupQa(m_groupId, result->m_id, result->m_questionUtf8, result->m_answerUtf8);
    LOG_INFO("group chat window: qa save sent, group=%lld, qa=%lld.", (long long)m_groupId, (long long)result->m_id);
    return 0;
}

void CBalloonGroupChatWindow::ApplyAdminGearVisibility()
{
    // 群角色取值：1 管理员、2 群主（与 group_member_role 对齐）。
    const int kRoleAdminValue = 1;
    const int kRoleOwnerValue = 2;

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* gear =
        content->FindCtrlById(balloonchatwnd::kGIdHdrAdmin);
    if (gear == NULL)
    {
        return;
    }

    const bool show = (m_myRole == kRoleAdminValue || m_myRole == kRoleOwnerValue);
    const bool changed = (gear->IsVisible() != show);
    gear->SetVisible(show);

    // SetVisible 只置标志位、不重新排版，而头部的水平布局排版时会跳过隐藏的子控件：齿轮隐藏期间
    // 排过的那次版里它没有位置。窗口开着时角色变化（被转让为群主、被设为管理员）把它显示出来，
    // 若不重排，它会停在宽度为 0 的旧矩形上、看不见，要重开窗口才出现。kRelayoutMsg 只重排消息区，
    // 管不到头部，故显隐真正变化时对齿轮所在的头部行按当前矩形 ForceLayout 一次。必须作用在这一行
    // 本身：对客户区根容器 ForceLayout 只强制根这一层，往下仍走 SetRect，而头部行的矩形没变，
    // SetRect 会直接返回（2026-10-02 联调实测）。窗口还没建出来时由 InitUi 首次排版处理
    if (changed && IsWindow())
    {
        balloonwjui::DuiControl* headerRow = gear->GetParent();
        if (headerRow != NULL)
        {
            headerRow->ForceLayout(headerRow->GetRect());
        }
    }
}

void CBalloonGroupChatWindow::OpenFontPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbFont);
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
    // 本群设过自定义字体时，「使用默认字体」才可点
    const bool hasOverride = (m_lpFMGClient != NULL && m_groupId != 0)
        && m_lpFMGClient->m_UserConfig.ChatFonts().HasOverride(kChatFontCtypeGroup, m_groupId);
    m_fontPopup.SetUseDefaultEnabled(hasOverride);
    m_fontPopup.OpenAt(anchor, m_hWnd);
}

void CBalloonGroupChatWindow::OnFontChanged(void* userdata, const net::TextFont& font)
{
    CBalloonGroupChatWindow* self = (CBalloonGroupChatWindow*)userdata;
    if (self == NULL)
    {
        return;
    }
    self->m_currentFont = font;
    self->ApplyCurrentFontToInput();

    // 存为本群的自定义字体（只影响这个群，重启后仍然有效）
    if (self->m_lpFMGClient != NULL && self->m_groupId != 0)
    {
        self->m_lpFMGClient->m_UserConfig.ChatFonts().SetOverride(
            kChatFontCtypeGroup, self->m_groupId, font);
        self->SaveChatFontPrefs();
        self->m_fontPopup.SetUseDefaultEnabled(true);
    }
}

void CBalloonGroupChatWindow::OnUseDefaultFont(void* userdata)
{
    CBalloonGroupChatWindow* self = (CBalloonGroupChatWindow*)userdata;
    if (self == NULL || self->m_lpFMGClient == NULL || self->m_groupId == 0)
    {
        return;
    }

    // 删掉本群的自定义字体，改用全局默认（全局默认也没设过时恢复默认外观）
    self->m_lpFMGClient->m_UserConfig.ChatFonts().RemoveOverride(
        kChatFontCtypeGroup, self->m_groupId);
    self->SaveChatFontPrefs();
    self->m_currentFont = self->ResolveChatFont();
    self->ApplyCurrentFontToInput();

    // 浮层保持打开，回显改用后的字体；按钮变为不可点
    self->m_fontPopup.SetFont(self->m_currentFont);
    self->m_fontPopup.SetUseDefaultEnabled(false);
    LOG_INFO("chat font reset to default for group chat, groupid=%lld.",
             (long long)self->m_groupId);
}

net::TextFont CBalloonGroupChatWindow::ResolveChatFont() const
{
    if (m_lpFMGClient == NULL || m_groupId == 0)
    {
        return net::TextFont();
    }
    return m_lpFMGClient->m_UserConfig.ChatFonts().Resolve(kChatFontCtypeGroup, m_groupId);
}

void CBalloonGroupChatWindow::ApplyCurrentFontToInput()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
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

void CBalloonGroupChatWindow::SaveChatFontPrefs()
{
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->SaveUserConfig();
    }
}

void CBalloonGroupChatWindow::ApplyDefaultChatFontIfNotOverridden()
{
    if (m_lpFMGClient == NULL || m_groupId == 0)
    {
        return;
    }
    // 本群单独设过字体：不跟随全局默认
    if (m_lpFMGClient->m_UserConfig.ChatFonts().HasOverride(kChatFontCtypeGroup, m_groupId))
    {
        return;
    }
    m_currentFont = ResolveChatFont();
    ApplyCurrentFontToInput();
    // 字体浮层若正开着，回显新的默认字体
    m_fontPopup.SetFont(m_currentFont);
}

void CBalloonGroupChatWindow::OpenEmojiPicker()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbEmoji);
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

    m_facePopup.SetPickCallback(&CBalloonGroupChatWindow::OnFacePicked, this);
    m_facePopup.OpenAt(anchor, m_hWnd);
}

void CBalloonGroupChatWindow::OnFacePicked(void* userdata, int faceId, const wchar_t* facePath)
{
    CBalloonGroupChatWindow* self = (CBalloonGroupChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertFaceToInput(faceId, facePath);
    }
}

void CBalloonGroupChatWindow::InsertFaceToInput(int faceId, const wchar_t* facePath)
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 以"带 faceId 标记的内联图"插入当前光标处（所见即所得）。标记按 ChatFaceTag
    // 约定编码进 REOBJECT.dwUser，发送时据此把表情与截图区分、内联进文本消息。
    // 面板保持打开便于连续选，故不在此抢焦点。
    input->InsertTaggedImage(facePath,
                             balloonchatwnd::FaceTagFromId(faceId),
                             balloonchatwnd::kFaceDisplayPx);
}

CBalloonGroupChatWindow::~CBalloonGroupChatWindow()
{
    // 没有经过 OnCloseMsg 就销毁时（如登出时直接销毁窗口），同样取消或丢弃还没发送的录音、停止试听
    m_voiceSession.OnWindowClosing();
    // 本窗口正在播放语音时一并停止，之后语音中心不再给本窗口发播放通知（第 3e 步）
    balloonvoice::CVoiceHub::GetInstance().DetachPlayListener(this);
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
    // 「发送代码」窗口同理
    if (m_codeComposeWnd)
    {
        m_codeComposeWnd->Close();
    }
    // 「发起接龙」窗口是独立的顶层窗口，对象析构时仍持有窗口句柄会触发断言
    if (m_relayComposeDlg && m_relayComposeDlg->IsWindow())
    {
        m_relayComposeDlg->DestroyWindow();
    }
    // 群通话选人框同理
    if (m_callPicker && m_callPicker->IsWindow())
    {
        m_callPicker->DestroyWindow();
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonGroupChatWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    //分段计时：建窗口、构建客户区、首次显示各花多久；成员与首屏历史到达时再看累计耗时。
    m_openStopWatch.Restart();
    BUI_TRACE("GROUPWND-OPEN-BEGIN group=%lld", (long long)m_groupId);

    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("group chat window: Create failed, group=%lld.", (long long)m_groupId);
        return;
    }
    const double createdMs = m_openStopWatch.ElapsedMs();

    // 标题栏：常规（非透明）标题栏，承载 min/max/close 三按钮。标题取真实群名；
    // 群名还没到位（仅占位）时先用占位，群资料到达后主面板会再 SetGroupName 刷新。
    CString title = m_groupName.IsEmpty() ? CString(TR(L"群聊")) : m_groupName;
    SetTitle(title);
    SetTitleBarHeight(kGroupChatWindowTitleBarHeight);
    SetButtons(true, true, true);
    SetMinSize(balloonchatwnd::kGroupChatWindowMinWidth, balloonchatwnd::kGroupChatWindowMinHeight);

    // 开启双击合成，让群聊气泡能收到双击（双击图片放大查看）。
    // （2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);

    // 标题栏图标 = 群头像（真图或字形 monogram），同时设到标题栏左侧与任务栏 /
    // Alt-Tab（照单聊窗口径）。仅真实群（绑定了 groupid）设置。
    RebuildGroupIcons();

    // 真实群：拉群成员，填成员侧栏并建发送人映射（应答经主面板路由回
    // OnGroupMembersArrived）。无群 id 的调试入口保持 mock，不拉。
    // 以下三个请求放在 InitUi 之前发出（2026-10-04 起），理由同单聊窗 CBalloonChatWindow::CreateAndShow：
    // 服务端处理与本端建界面同时进行，且排在 InitUi 里发出的文件共享、屏幕共享状态查询之前；
    // 应答要等本函数返回、本窗口登记进主面板的窗口表之后才会被界面线程取出处理。
    if (m_groupId != 0)
    {
        CIUSocket::GetInstance().RequestGroupMembers(m_groupId);

        // 首屏历史与群成员<u>同时</u>请求（2026-09-30 起）。原先要等成员回来才请求历史，
        // 两次网络往返串行，本机实测多等约 100 毫秒。历史若先到，对方消息先按备用信息
        // （"用户<id>"）显示，成员到达后由 OnGroupMembersArrived 就地换成真实名字与头像。
        // 更早的消息由用户往上滚时按页续拉（见 OnChatLoadOlder）。应答经主面板路由回
        // OnHistoryMessages。
        CIUSocket::GetInstance().RequestSyncMessages(
            2, m_groupId, 0, balloonchatwnd::kHistoryPageSize);

        // 再拉一次本群被 Pin 的消息。上面那次只覆盖"最近一页"，而被钉的往往恰恰是
        // 更早的老消息 —— 不单独拉一次，历史里那些气泡就不会带图钉标记。应答经主面板
        // 的 OnPinnedMessagesArrived 落库并回投到本窗口。
        CIUSocket::GetInstance().GetPinnedMessages(conversation_group, m_groupId);

        // 查询本人在本群的入群必读状态：待确认时输入区换成「点此阅读」并自动弹出必读公告（docs/群公告接入方案.md §14）。
        // 应答经主面板路由回 OnMustReadArrived
        CIUSocket::GetInstance().SendGetMustRead(m_groupId);

        // 拉取本群可用的机器人（docs/机器人方案.md §4.2）：发送者名字、「机器人」徽章、@ 候选与成员页签都用它。
        // 应答经主面板路由回 OnGroupBotsArrived；在那之前机器人区域保持隐藏
        RefreshBotMemberSection();
        RequestGroupBots();
    }

    InitUi();
    const double initUiMs = m_openStopWatch.ElapsedMs();
    ResizeClient(kGroupChatWindowWidth, kGroupChatWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    const double shownMs = m_openStopWatch.ElapsedMs();
    BUI_TRACE("GROUPWND-OPEN-SHOWN create=%.3fms initui=%.3fms shown=%.3fms",
              createdMs, initUiMs, shownMs);

    if (m_groupId != 0)
    {
        // 群文件也在开窗时就拉：边栏「群文件」Tab 上要显示真实数量，不能等用户切到
        // 那个 Tab 才知道有几个（切过去时的那次拉取因此多半直接返回，见 StartLoadGroupFiles）。
        StartLoadGroupFiles();
    }
    else
    {
        // 调试入口（--groupchatwindow）：用 mock 群成员填 m_memberInfo，好让 @提醒
        // 浮层在没有服务器的情况下也能弹出、可点、可插入 —— 否则这条交互只能登录
        // 进真实群才验得了，每改一次都得手工重来一遍。
        // uid 用 1000+下标 造一批不与真实 userid 冲突的占位值。
        const std::vector<balloonchatwnd::GroupMember>& mocks =
            balloonchatwnd::MockGroupMembers();
        for (size_t i = 0; i < mocks.size(); ++i)
        {
            MemberInfo info;
            info.m_name  = mocks[i].name;
            info.m_glyph = mocks[i].avatarText;
            info.m_color = mocks[i].avatarColor;
            m_memberInfo[(int64_t)(1000 + i)] = info;
        }
        LOG_INFO("群聊窗调试入口：已填入 %u 个 mock 群成员供 @提醒 使用。",
                 (uint32_t)m_memberInfo.size());
    }

    // 边栏两个 Tab 的数量文案落一次初值：真实群此刻成员与文件都还在路上，显示光秃秃的
    // "成员" / "群文件"，等应答到达再由各自的回调刷成真实数；调试入口的 mock 成员与
    // mock 文件库都已就位，这一次就能写出真实数量。
    RefreshSideTabCounts();

    //三个时刻都是从打开起算的累计值：create 为窗口建好，initui 为客户区控件树建好，
    //shown 为首次显示并同步重绘完成。成员与首屏历史要等服务端应答，另见各自的回调。
    LOG_INFO("group chat window shown: group=%lld, create=%.1fms, initui=%.1fms, shown=%.1fms.",
             (long long)m_groupId, createdMs, initUiMs, shownMs);
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonGroupChatWindow::InitUi()
{
    // 构建群聊窗客户区控件树（来自 GroupChatWindowLayout 的内嵌 XML）。真实群
    // （m_groupId != 0）起始为空，不预填 mock 消息 / 成员；调试入口才用 mock。
    std::unique_ptr<balloonwjui::DuiControl> root =
        balloonchatwnd::BuildGroupChatWindowBody(m_groupId == 0);
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildGroupChatWindowBody 返回空 —— 群聊窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 成员侧栏贴着窗口右缘：成员行与滚动条滑块都要给窗口缩放边框让出宽度，否则滑块落在
    // 缩放边框里，按下只会拖动窗口边框、抓不住滑块。与消息记录窗列表的让位同一口径
    balloonchatwnd::GroupMemberList* memberList = dynamic_cast<balloonchatwnd::GroupMemberList*>(
        content->FindCtrlById(balloonchatwnd::kGIdMemberList));
    if (memberList != NULL)
    {
        const int insetPx = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(), GetDpi(),
                                                                    IsResizable());
        memberList->SetRightInset(insetPx);
        LOG_INFO("group chat window: member list right inset %d px (border %d, dpi %d).",
                 insetPx, GetBorderPx(), GetDpi());
        // 成员列表的视图（平铺 / 按部门）用账号配置里记住的那一种，所有群共用
        m_memberTreeView = (m_lpFMGClient != NULL) && m_lpFMGClient->m_UserConfig.IsGroupMemberTreeView();
        memberList->SetTreeMode(m_memberTreeView);
        balloonchatwnd::ApplyMemberViewToggle(content, m_memberTreeView);
    }

    // 调试入口（无真实群）：用 mock 群文件预填库并置已加载，切到群文件 Tab 即渲染。
    // 真实群（m_groupId != 0）的文件由首次切到群文件 Tab 时 cmd_get_group_files 拉取填充。
    if (m_groupId == 0)
    {
        m_fileLibrary.SetAll(MockGroupFileEntries());
        m_fileLoaded = true;
    }

    // 消息输入框是无窗口富文本控件的聊天定制子类（见 ChatInputRichEdit.h），
    // 构造完即可用，下面各项属性都可以直接设置。
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input != NULL)
    {
        input->SetShowBorder(false);   // 输入框无边框，融入输入区
        // 多行 + 自动换行。这两项在无窗口控件上是随时可改的属性，顺序无讲究。
        input->SetMultiLine(true);
        input->SetWordWrap(true);
        // 输入区高度由用户拖分割条决定，内容多了在框内滚动，不随内容长高。
        input->SetAutoGrow(false);
        {
            // 粘贴一律按纯文本（聊天输入不要带来源的颜色 / 字体 / 链接 / 对象）。
            input->SetPasteAsPlainTextDefault(true);
            // 关掉输入框自带的拖放：拖放在聊天窗里是整窗接收的，两处都注册会
            // 互相抢（详见单聊窗同一处的说明）。
            input->SetDragDropEnabled(false);

            // 回车发送、「正在输入」、以及 @ 提醒这三组通知。原先是往子窗口上
            // 挂三段子类过程，现在由控件自己处理 —— 消息协议没变，本窗口接消息
            // 那一侧的代码一行都不用改。
            input->SetNotifyTarget(m_hWnd);
            input->SetSendOnEnterMsg(kSendOnEnterMsg);
            input->SetSendOnEnterQuery(&ChatSendOnEnterQuery, NULL);
            // 粘贴 / 拖入的聊天内容里有图片时，由本窗口登记源文件并带标记插入。
            input->SetImageStager(&CBalloonGroupChatWindow::StageImageThunk, this);
            // 群聊窗没有单独的「正在输入」通知，内容变化统一走 @ 提醒那一路。
            input->SetMentionMsgs(kMentionChangedMsg, kMentionNavMsg, kMentionWheelMsg);
            // 粘贴识别：往空输入框里粘贴像代码的文字时显示「以代码片段发送」提示条（docs/代码片段方案.md 第 3.1 节）
            input->SetPasteTextMsg(balloonchatwnd::kMsgChatCodePaste);

            // 浮窗提前建好（此刻不显示）。建窗 + 建控件树是同步的，留到首次键入 '@'
            // 时再做，会把那一拍拖长、让刚敲的 '@' 迟迟不显示。
            m_atDropdown.EnsureCreated();
            m_atDropdown.SetPickCallback(&CBalloonGroupChatWindow::OnAtPickThunk, this);

            // 给消息区装"成员是否仍在群里"的查询钩子：气泡里 @某人 的右键菜单据此
            // 决定「再 @ 一次 TA」要不要置灰。只有群聊窗需要装。
            balloonchatwnd::ChatMessageList* msgList =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (msgList != NULL)
            {
                msgList->SetMemberInGroupQuery(
                    &CBalloonGroupChatWindow::IsMemberInGroupThunk, this);
                // 同理装"查成员角色"的钩子，并把本人角色同步下去：气泡里 @某人 的
                // 右键菜单末尾要不要给出「设为 / 撤销管理员」，靠这两样判定。
                msgList->SetMemberRoleQuery(
                    &CBalloonGroupChatWindow::MemberRoleThunk, this);
                msgList->SetMyNetRole(m_myRole);
                // 文字消息右键菜单里的「添加到我的备忘」，能新建群备忘的人再多一项「添加到群备忘」
                msgList->SetMemoMenuMode(balloonmemo::CanEditGroupMemo(m_myRole)
                                             ? balloonchatwnd::kChatMemoMenuGroupBoth
                                             : balloonchatwnd::kChatMemoMenuGroupMine);
                // 再装"是否被特别关注"的钩子：气泡里 @某人 与发送者头像的右键菜单
                // 据它决定那一项的文案是「特别关注」还是「取消特别关注」。
                msgList->SetStarredQuery(
                    &CBalloonGroupChatWindow::StarredThunk, this);
                // 「关注 TA 在本群的发言」的文案同样要现问主面板的群关注名单
                msgList->SetGroupFollowQuery(
                    &CBalloonGroupChatWindow::GroupFollowThunk, this);
            }
        }
        // 按"本群自定义 > 全局默认"取聊天字体并应用到输入框（所见即所得）；两者都
        // 没设过则保持默认外观。先记下输入框原本的文字色，恢复默认外观时要用。
        m_inputDefaultTextColor = input->GetTextColor();
        m_currentFont = ResolveChatFont();
        ApplyCurrentFontToInput();
    }

    // 字体设置浮层：装"变化"与"使用默认字体"两个回调（this 作上下文）。
    m_fontPopup.SetChangeCallback(&CBalloonGroupChatWindow::OnFontChanged, this);
    m_fontPopup.SetUseDefaultCallback(&CBalloonGroupChatWindow::OnUseDefaultFont, this);

    // 给头部 / 输入工具栏 / 发送按钮 / @提醒 注册 tooltip。
    RegisterTip(content, balloonchatwnd::kGIdHdrVoice,     TR(L"语音通话"));
    RegisterTip(content, balloonchatwnd::kGIdHdrVideo,     TR(L"视频通话"));
    RegisterTip(content, balloonchatwnd::kGIdHdrScreenShare, TR(L"共享屏幕"));
    RegisterTip(content, balloonchatwnd::kGIdTbRecord, TR(L"屏幕录制"));
    RegisterTip(content, balloonchatwnd::kGIdHdrHistory,   TR(L"消息记录"));
    // 「字幕模式」按钮的提示随本群字幕的开关切换（用户 2026-10-05 确认），状态由主面板经 SetSubtitleHighlight 设置
    balloonchatwnd::ChatIconButton* subtitleBtn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kGIdHdrSubtitle))
        : NULL;
    if (subtitleBtn != NULL)
    {
        subtitleBtn->SetToggleTips(TR(L"字幕已开启"), TR(L"字幕已关闭"));
    }
    RegisterTip(content, balloonchatwnd::kGIdHdrAdmin,     TR(L"群管理"));
    RegisterTip(content, balloonchatwnd::kGIdHdrMore,      TR(L"更多"));
    RegisterTip(content, balloonchatwnd::kGIdTbEmoji,      TR(L"表情"));
    RegisterTip(content, balloonchatwnd::kGIdTbImage,      TR(L"图片"));
    // 群聊里这个按钮上传的是群文件（进右侧群文件库），不是发给某个人的文件消息，
    // 提示文字写清楚，免得用户以为是单聊那种"发文件"。第二行说明也可以拖进输入框上传（2026-10-07 起；
    // 群聊不支持发送文件夹，拖进来的文件夹会被跳过并提示，所以这里只提文件）
    RegisterTip(content, balloonchatwnd::kGIdTbFile,       TR(L"上传群文件\n也可以把文件直接拖到下面的输入框里上传"));
    RefreshScreenshotTip();   // 剪刀：「截图（Alt+Z）」，随设置里的截图快捷键变化
    RegisterTip(content, balloonchatwnd::kGIdTbScreenshotMenu, TR(L"截图选项"));
    RegisterTip(content, balloonchatwnd::kGIdTbVoice,      TR(L"语音消息"));
    RegisterTip(content, balloonchatwnd::kGIdTbFont,       TR(L"字体设置"));
    RegisterTip(content, balloonchatwnd::kGIdTbCode,       TR(L"发送代码"));
    RegisterTip(content, balloonchatwnd::kGIdHdrShare,      TR(L"文件共享"));
    RegisterTip(content, balloonchatwnd::kGIdHdrRelay,     TR(L"发起接龙"));
    RegisterTip(content, balloonchatwnd::kGIdTbPlus,       TR(L"更多功能"));
    RegisterTip(content, balloonchatwnd::kGIdMention,      TR(L"@提醒成员"));
    RegisterTip(content, balloonchatwnd::kGIdSendButton,   TR(L"发送"));
    // Markdown 编辑器按钮：打开独立的 Markdown 编辑器并预先选好本群；FlamingoMarkdown.dll 不可用时隐藏
    //（docs/Markdown消息方案.md 第 11.3 节）
    RegisterTip(content, balloonchatwnd::kGIdTbMarkdown, TR(L"Markdown 编辑器"));
    balloonchatwnd::ApplyMarkdownEditorButton(content, balloonchatwnd::kGIdTbMarkdown);
    // 截图的剪刀与 ▾：截图模块 FlamingoScreenshot.dll 不可用时隐藏（docs/截图独立模块方案.md 3.3 节）
    balloonchatwnd::ApplyScreenshotButtons(content, balloonchatwnd::kGIdTbScreenshot, balloonchatwnd::kGIdTbScreenshotMenu,
                                           balloonscreenshot::IsScreenshotAvailable());
    ApplyShareButtonVisibility();
    ApplyScreenShareButtonVisibility();
    ApplyGroupCallButtonVisibility();
    ApplyVoiceButtonVisibility();
    // 录音条（语音消息第 3b、6g 步）：由录音会话在布局的占位里建出控件，平时隐藏
    {
        balloonchatwnd::VoiceRecordBarIds voiceIds;
        voiceIds.m_pause = balloonchatwnd::kGIdVoicePause;
        voiceIds.m_stop = balloonchatwnd::kGIdVoiceStop;
        voiceIds.m_preview = balloonchatwnd::kGIdVoicePreview;
        voiceIds.m_cancel = balloonchatwnd::kGIdVoiceCancel;
        voiceIds.m_send = balloonchatwnd::kGIdVoiceSend;
        voiceIds.m_confirm = balloonchatwnd::kGIdVoiceConfirm;
        voiceIds.m_back = balloonchatwnd::kGIdVoiceBack;
        m_voiceSession.Init(this, dynamic_cast<balloonwjui::DuiHBox*>(content->FindCtrlById(balloonchatwnd::kGIdVoiceBar)),
                            voiceIds);
    }
    // 登记本群：文件共享模块数出可用共享的个数后经主面板设置头部按钮的角标
    if (m_groupId != 0)
    {
        balloonfileshare::CFileShareHost::GetInstance().WatchConversationShares(conversation_group, m_groupId);
    }
    // 屏幕共享横幅先按已知状态显示（之前在别处收到过本群的开始推送时立即可见）
    RefreshScreenShareBanner();
    // 本次登录还不知道本群有没有进行中的屏幕共享：查一次，结果回来后历史里的共享卡片与横幅据此显示状态
    if (m_groupId != 0)
    {
        balloonscreenshare::CScreenShareHub::GetInstance().Query(conversation_group, (int64_t)m_groupId);
    }
    // 群通话横幅同理：先按已知状态显示，再查一次本群有没有进行中的群通话（docs/群音视频通话方案.md 6.3）
    RefreshGroupCallBanner();
    if (m_groupId != 0)
    {
        balloongroupcall::CGroupCallHub::GetInstance().Query((int64_t)m_groupId);
    }

    // 按持久化的发送模式刷新左下角提示与输入框 placeholder（重启后保留上次选择）。
    RefreshSendModeUi();

    // 按本人角色决定"群管理"齿轮是否显示（仅群主 / 管理员可见）。
    ApplyAdminGearVisibility();

    // 仅通知模式：本人是普通成员且本群开着时，输入区换成提示行（开关与角色可能在建窗之前就已推来）
    ApplyNotifyOnlyState();

    // 气泡头像的灰彩：消息列表此刻才存在，把已知成员的在线状态与本端连接态告知它
    ApplyAvatarGrayToMessageList();

    // 注册到图片解码缓存：群里图片气泡发起后台解码后，缩放完成经 WM_CHAT_IMAGE_READY
    // 通知本窗口重绘。注销在 OnCloseMsg。
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);

    // 群文件面板底部的翻页 ‹ › 与「上传文件」按钮改用 Default 变体（白底 + 1px 灰边 +
    // 深字，低调的次操作样式）。默认的 Primary 实心蓝按钮在窄边栏里显得太重，对照
    // design/群文件.png 应是轻量样式。
    {
        const UINT kSubtleBtnIds[3] =
        {
            balloonchatwnd::kGIdFilePagePrev,
            balloonchatwnd::kGIdFilePageNext,
            balloonchatwnd::kGIdFileUpload,
        };
        for (int i = 0; i < 3; ++i)
        {
            balloonwjui::DuiButton* btn =
                dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(kSubtleBtnIds[i]));
            if (btn != NULL)
            {
                btn->SetVariant(balloonwjui::DuiButton::Variant::Default);
            }
        }
    }

    // 调试入口（--groupchatwindow，没有主面板也就没有服务端）下，顶部「已 Pin N 条」
    // 提示带的条数没有别处可问，就照 mock 消息里带 Pin 标记的行数凑一个。理由与真实
    // 会话为何不能这么算，见单聊窗 InitUi 里同一段注释。
    if (m_groupId == 0)
    {
        balloonchatwnd::ChatMessageList* mockList =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
        if (mockList != NULL)
        {
            SetPinnedCount(mockList->CountPinned());
        }
    }

    // 轻量提示条：挂在客户区根容器末尾（末尾即最上层），Fixed(0) 不占 vbox 主轴空间。
    balloonwjui::DuiVBox* rootBox = dynamic_cast<balloonwjui::DuiVBox*>(content);
    if (rootBox != NULL)
    {
        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        m_dropToast = toast.get();
        rootBox->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));
    }

    // 拖文件进窗口：把拖放目标注册到本窗口。第一个参数传本窗口自身作宿主，接收器
    // 会在注册之前先让出窗口上可能已被 balloonui 控件占用的那份拖放目标 —— 一个
    // 窗口只能注册一个，被占住时本窗口这份会静默失败，症状是拖任何文件进来都没有
    // 反应（见 ChatDropController.h 的文件头）。输入框已换成无窗口控件、没有子窗口
    // 需要摘，故第二个参数传空。
    m_dropCtrl.Attach(this, NULL, this);

    // 备忘：登记为备忘中枢的观察者，备忘簿变化时收 FMG_MSG_MEMO_CHANGED；先按现有数据画一次页签角标
    if (m_groupId != 0)
    {
        balloonmemo::CMemoCenter::Instance().RegisterObserver(m_hWnd);
        RefreshMemoTabBadge();
    }

    LOG_INFO(_T("群聊窗客户区 UI 初始化完成。"));
}

void CBalloonGroupChatWindow::RefreshLinkRendering()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->RefreshAllRows();
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

balloonwjui::DuiVBox* CBalloonGroupChatWindow::InputBlockCtrl()
{
    // 发送框 = 输入框（richedit）所在的那个 vbox，与单聊窗同一套结构。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    balloonwjui::DuiControl* input = content->FindCtrlById(balloonchatwnd::kGIdInputEdit);
    if (input == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonwjui::DuiVBox*>(input->GetParent());
}

bool CBalloonGroupChatWindow::IsChatDropZone(const POINT& ptClient)
{
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block == NULL)
    {
        return false;
    }
    RECT rc = block->GetRect();
    return ::PtInRect(&rc, ptClient) != FALSE;
}

void CBalloonGroupChatWindow::OnChatDropHintChanged(bool visible)
{
    // 提示落在发送框上（整块浅蓝底 + 左下角提示文字），与单聊窗同一套做法，
    // 为什么用底色而不是描边见那边的注释。
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block != NULL)
    {
        const COLORREF kDropZoneBg = RGB(0xE6, 0xF1, 0xFD);
        block->SetBgColor(visible ? kDropZoneBg : CLR_INVALID);
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdHintLabel));
        if (hint != NULL)
        {
            if (visible)
            {
                // 群里拖文件的落点是群文件库，文案要说清楚，免得用户以为是发消息。
                hint->SetText(TR(L"松开发送：图片进聊天，文件进群文件"));
            }
            else
            {
                RefreshSendModeUi();
            }
        }
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::OnChatContentDropped(
    const std::vector<balloonchatwnd::ChatClipSegment>& segs)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
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

void CBalloonGroupChatWindow::OnChatFilesDropped(const std::vector<std::wstring>& paths,
                                                 bool forceAsFile)
{
    // 调试入口（无群绑定）没有服务器可发。
    if (m_groupId == 0)
    {
        if (m_dropToast != NULL)
        {
            m_dropToast->Show(TR(L"调试窗口没有绑定群，无法发送"));
        }
        return;
    }

    const balloonchatwnd::DropPlan plan =
        balloonchatwnd::BuildDropPlanFromPaths(paths, forceAsFile);

    if (plan.m_needConfirm)
    {
        CString ask;
        ask.Format(TR(L"确定要发送 %d 个文件吗？"), (int)plan.m_items.size());
        if (::MessageBoxW(m_hWnd, (LPCWSTR)ask, TR(L"发送文件"),
                          MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        {
            LOG_INFO("群聊窗拖放：用户取消了 %d 个文件的发送。", (int)plan.m_items.size());
            return;
        }
    }

    // 图片发群图片消息，其余上传群文件库。至少上传了一笔才把边栏切到「群文件」，
    // 与工具栏「文件」按钮的做法一致 —— 纯图片的一次拖放不该把边栏切走。
    bool anyUpload = false;
    for (std::vector<balloonchatwnd::DropSendItem>::size_type i = 0;
         i < plan.m_items.size(); ++i)
    {
        const balloonchatwnd::DropSendItem& item = plan.m_items[i];
        if (item.m_kind == balloonchatwnd::kDropSendImage)
        {
            SendImageFile(item.m_path);
        }
        else if (UploadGroupFile(item.m_path))
        {
            anyUpload = true;
        }
    }
    if (anyUpload)
    {
        SwitchSideTab(balloonchatwnd::kGIdTabFiles);
    }

    // 文件夹传输一期只做单聊（见 docs/单聊文件夹传输方案.md 第 13 节），群聊里拖进来的
    // 文件夹一律不处理。归类模块是两个聊天窗共用的，它只负责把文件夹拣出来，拦不拦由
    // 各窗口自己定 —— 这里必须显式拦下并说明原因，否则用户拖了没反应，还以为在传。
    CString folderNote;
    if (!plan.m_folders.empty())
    {
        folderNote.Format(TR(L"群聊暂不支持发送文件夹（%d 个未发送）"), (int)plan.m_folders.size());
    }

    // 提示只有一条浮层可用，故把"文件夹没发"与"另有几项发不出去"拼成一句，
    // 免得后一句把前一句顶掉。
    if (m_dropToast != NULL && (!folderNote.IsEmpty() || !plan.m_skipped.empty()))
    {
        CString note = folderNote;
        if (!plan.m_skipped.empty())
        {
            const std::wstring skippedNote = balloonchatwnd::DescribeDropSkipped(plan.m_skipped);
            if (!note.IsEmpty() && !skippedNote.empty())
            {
                note += _T("、");
            }
            note += skippedNote.c_str();
        }
        m_dropToast->Show((LPCTSTR)note);
    }

    LOG_INFO("Group chat drop handled: files %d, folders %d (not supported here), skipped %d, shift=%d.",
             (int)plan.m_items.size(), (int)plan.m_folders.size(),
             (int)plan.m_skipped.size(), forceAsFile ? 1 : 0);
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonGroupChatWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_ACTIVATE，理由与口径同单聊窗（见 CBalloonChatWindow::OnActivateMsg）。
    bHandled = FALSE;

    if (LOWORD(wParam) == WA_INACTIVE)
    {
        return 0;
    }
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return 0;
    }
    if (m_groupId <= 0)
    {
        return 0;
    }

    long long* pCid = new long long((long long)m_groupId);
    LOG_INFO("群聊窗被激活，请求清除该会话未读: group=%lld.", (long long)m_groupId);
    ::PostMessage(m_hostPanel, FMG_MSG_CONV_MARK_READ, (WPARAM)2, (LPARAM)pCid);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // m_hMainDlg 非空（调试入口 --groupchatwindow）：本窗口是唯一可见
    // 窗口，关闭即请求退出程序。
    // m_hMainDlg 为空（从主面板打开）：关闭只销毁自身。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
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
    // 「发起接龙」窗口随本窗口关闭
    if (m_relayComposeDlg && m_relayComposeDlg->IsWindow())
    {
        m_relayComposeDlg->DestroyWindow();
    }
    // 群通话选人框随本窗口关闭；它的结果接收方由本窗口持有，窗口销毁后不能再收到确认
    if (m_callPicker && m_callPicker->IsWindow())
    {
        m_callPicker->DestroyWindow();
    }
    // 桌面字幕由主面板持有，不随群聊窗关闭（docs/聊天字幕模式方案.md 第 1 节第 10 条），这里不动它
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatFocusArea(UINT /*uMsg*/, WPARAM wParam,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam == (WPARAM)CHAT_FOCUS_INPUT || wParam == (WPARAM)CHAT_FOCUS_MESSAGES)
    {
        m_focusArea = (int)wParam;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnLButtonDownMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
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

    balloonwjui::DuiControl* msgArea = content->FindCtrlById(balloonchatwnd::kGIdMessageArea);
    if (msgArea != NULL)
    {
        RECT rc = msgArea->GetRect();
        if (::PtInRect(&rc, pt))
        {
            m_focusArea = CHAT_FOCUS_MESSAGES;
            return 0;
        }
    }

    // 点发送框那一整块（工具栏、提示行、发送按钮等）同样算回到发送框。
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

LRESULT CBalloonGroupChatWindow::OnChatCopySelection(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                     LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 焦点在发送框时这一下不归消息区，返回 0 让输入框自己复制。
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return 0;
    }
    return list->CopySelectedTextToClipboard() ? 1 : 0;
}

LRESULT CBalloonGroupChatWindow::OnKeyDownMsg(UINT uMsg, WPARAM wParam,
                                              LPARAM lParam, BOOL& bHandled)
{
    // 录音条显示时：Enter 等于「发送」、Esc 等于「取消」，确认放弃状态下 Esc 等于「返回」（语音消息第 3b、6g 步）。
    // 输入框此时隐藏但可能仍持有焦点，必须在这里截住，否则 Enter 会被输入框当成发送文字消息
    if (m_voiceSession.HandleKey((UINT)wParam))
    {
        return 0;
    }
    // 焦点不在输入框时的 Ctrl+C。焦点在输入框里时走的是输入框键盘钩子那条路。
    if (wParam == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        BOOL dummy = TRUE;
        if (OnChatCopySelection(uMsg, 0, 0, dummy) != 0)
        {
            return 0;
        }
    }
    bHandled = FALSE;
    (void)lParam;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenLink(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 不立刻打开：先记下来、等一个系统双击间隔（见 kLinkOpenTimerId 的注释）。
    m_pendingLinkUrl = *pUrl;
    delete pUrl;
    if (IsWindow())
    {
        ::SetTimer(m_hWnd, kLinkOpenTimerId, ::GetDoubleClickTime(), NULL);
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatCancelLinkOpen(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (IsWindow())
    {
        ::KillTimer(m_hWnd, kLinkOpenTimerId);
    }
    m_pendingLinkUrl.clear();
    return 0;
}

void CBalloonGroupChatWindow::OpenPendingLink()
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
        LOG_INFO("group chat window: share link clicked.");
        balloonscreenshare::CScreenShareHub::GetInstance().JoinByLink(url);
        return;
    }
    // 加群链接（docs/邀请入群方案.md §12）：交给主面板查群并弹确认框，不交给浏览器
    std::string joinCode;
    if (balloonchatwnd::IsJoinGroupLinkUrl(url, &joinCode))
    {
        LOG_INFO("group chat window: join-group link clicked.");
        std::string* pCode = new std::string(joinCode);
        if (m_hostPanel == NULL || !::PostMessage(m_hostPanel, FMG_MSG_OPEN_JOIN_GROUP_LINK, 0, (LPARAM)pCode))
        {
            delete pCode;
        }
        return;
    }
    // 协议白名单：只放行 http / https。理由与单聊窗同一处注释。
    if (balloonchatwnd::IsSafeLinkUrl(url))
    {
        ::ShellExecuteW(m_hWnd, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("群聊窗拒绝打开非 http/https 链接。");
    }
}

LRESULT CBalloonGroupChatWindow::OnViewImageMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    // 群聊气泡双击图片冒泡上来：lParam 为图片本地路径。打开进程内共享看图窗放大查看。
    LPCTSTR path = (LPCTSTR)lParam;
    if (path != NULL && path[0] != _T('\0'))
    {
        CBalloonImageViewerWindow::OpenShared(m_hWnd, path);
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnImageReady(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 后台某张图片缩放完成：重绘消息区，图片气泡再画一次时查 ChatImageCache 命中、换成
    // 真图。图片展示尺寸不变，无需重排，直接 Invalidate。
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    // WM_DUI_NOTIFY 的 LPARAM 指向一个 DuiNotify（仅在本次同步派发内有效）。
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    // 问答面板：搜索框文字变化（每键触发）→ 按问题或答案筛选，重建卡片
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdQaSearch)
    {
        RenderQaPanel();
        return 0;
    }
    // 问答卡片上的「发到群里」「修改」「删除」：ctrlId 落在问答卡片的 id 段内才进分支，等这次通知处理完再执行
    int qaCardIndex = 0;
    int qaCardPart = 0;
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK
        && balloonchatwnd::QaCardFromCtrlId(pNotify->ctrlId, qaCardIndex, qaCardPart)
        && (qaCardPart == balloonchatwnd::kQaCardPartSend || qaCardPart == balloonchatwnd::kQaCardPartEdit
            || qaCardPart == balloonchatwnd::kQaCardPartDelete))
    {
        const int action = (qaCardPart == balloonchatwnd::kQaCardPartSend) ? kQaCardActionSend
                         : (qaCardPart == balloonchatwnd::kQaCardPartEdit) ? kQaCardActionEdit
                                                                           : kQaCardActionDelete;
        PostQaCardAction(action, qaCardIndex);
        return 0;
    }
    // 输入区的「点此阅读」提示行（只在待确认入群必读时是链接）：等这次通知处理完再弹出必读公告
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kGIdNotifyOnlyBar && m_mustReadPending)
    {
        PostMessage(kMustReadShowMsg, 0, 0);
        return 0;
    }
    // 备忘卡片：「修改」「删除」、双击修改或查看、右键弹菜单。卡片里每个控件的 ctrlId 都落在卡片的 id 段内，
    // 反推得出是第几张卡片才进分支，别的控件的同码通知照常往下走。操作都先投给自己再做（见 kMemoCardActionMsg）
    int memoCardIndex = 0;
    int memoCardPart = 0;
    const bool onMemoCard = (pNotify != NULL)
        && balloonmemo::MemoCardFromCtrlId(pNotify->ctrlId, memoCardIndex, memoCardPart);
    const bool onMemoCardButton = onMemoCard
        && (memoCardPart == balloonmemo::kMemoCardPartEdit || memoCardPart == balloonmemo::kMemoCardPartDelete);
    if (onMemoCard && pNotify->code == balloonwjui::DUIN_CLICK && memoCardPart == balloonmemo::kMemoCardPartEdit)
    {
        PostMemoCardAction(kMemoCardActionOpen, memoCardIndex);
        return 0;
    }
    if (onMemoCard && pNotify->code == balloonwjui::DUIN_CLICK && memoCardPart == balloonmemo::kMemoCardPartDelete)
    {
        PostMemoCardAction(kMemoCardActionDelete, memoCardIndex);
        return 0;
    }
    if (onMemoCard && pNotify->code == balloonwjui::DUIN_DBLCLK && !onMemoCardButton)
    {
        PostMemoCardAction(kMemoCardActionOpen, memoCardIndex);
        return 0;
    }
    if (onMemoCard && pNotify->code == balloonwjui::DUIN_RCLICK)
    {
        PostMemoCardAction(kMemoCardActionMenu, memoCardIndex);
        return 0;
    }
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        // 「发送」按钮 → 发消息。
        if (pNotify->ctrlId == balloonchatwnd::kGIdSendButton)
        {
            DoSend();
            return 0;
        }
        // 工具栏的 Markdown 编辑器按钮：打开空白编辑器，发送对象预选本群（docs/Markdown消息方案.md 第 11.3 节）
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbMarkdown)
        {
            OpenMarkdownEditor(std::string());
            return 0;
        }
        // 顶部离线提示带只作告知、不可点击，故这里没有它的分支 —— 恢复在线统一走
        // 托盘 / 主面板的状态菜单那个唯一入口。

        // 右侧边栏四个 Tab → 切换选中。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTabMembers ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabFiles ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabNotice ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabMemo ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabQa)
        {
            SwitchSideTab(pNotify->ctrlId);
            return 0;
        }

        // 成员页签的机器人区域（docs/机器人方案.md §4.2）：点某个机器人弹资料卡，等这次通知处理完再做
        // （弹框期间 2801 到达会重建这些行，UI 约定第 13 条）
        if (pNotify->ctrlId >= balloonchatwnd::kGIdBotRowFirst && pNotify->ctrlId <= balloonchatwnd::kGIdBotRowLast)
        {
            PostMessage(kGroupBotActionMsg, (WPARAM)kGroupBotActionCard,
                        (LPARAM)(pNotify->ctrlId - balloonchatwnd::kGIdBotRowFirst));
            return 0;
        }

        // 问答面板：「＋ 新建问答」，等这次通知处理完再打开编辑窗
        if (pNotify->ctrlId == balloonchatwnd::kGIdQaAdd)
        {
            PostQaCardAction(kQaCardActionAdd, -1);
            return 0;
        }

        // 备忘面板：「＋ 新建群备忘」
        if (pNotify->ctrlId == balloonchatwnd::kGIdMemoAdd)
        {
            OpenGroupMemoEditor(0);
            return 0;
        }

        // 公告面板：上一条 / 下一条。
        if (pNotify->ctrlId == balloonchatwnd::kGIdNoticePrev)
        {
            StepNotice(-1);
            return 0;
        }
        if (pNotify->ctrlId == balloonchatwnd::kGIdNoticeNext)
        {
            StepNotice(1);
            return 0;
        }

        // 群文件面板：分类筛选 chip → 切分类、回到第 1 页、重渲染。
        {
            int chipCat = -1;
            switch (pNotify->ctrlId)
            {
            case balloonchatwnd::kGIdFileChipAll:
                chipCat = balloonchatwnd::kGroupFileCatAll;
                break;
            case balloonchatwnd::kGIdFileChipPdf:
                chipCat = balloonchatwnd::kGroupFileCatPdf;
                break;
            case balloonchatwnd::kGIdFileChipDoc:
                chipCat = balloonchatwnd::kGroupFileCatDoc;
                break;
            case balloonchatwnd::kGIdFileChipSheet:
                chipCat = balloonchatwnd::kGroupFileCatSheet;
                break;
            case balloonchatwnd::kGIdFileChipDesign:
                chipCat = balloonchatwnd::kGroupFileCatDesign;
                break;
            default:
                break;
            }
            if (chipCat >= 0)
            {
                ApplyFileChipSelection(chipCat);
                m_filePage = 0;
                RenderFilePanel();
                return 0;
            }
        }
        // 群文件面板：上一页。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFilePagePrev)
        {
            if (m_filePage > 0)
            {
                m_filePage--;
                RenderFilePanel();
            }
            return 0;
        }
        // 群文件面板：下一页（越界由 RenderFilePanel 内的 Query 钳回末页）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFilePageNext)
        {
            m_filePage++;
            RenderFilePanel();
            return 0;
        }
        // 群文件面板：上传文件 → 选文件并交 FileTransfer 群文件上传。每点一次发起一笔，
        // 可多笔并发（最多 N 笔同时传、超出排队），各占面板顶部一行「上传中」。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFileUpload)
        {
            PickAndUploadGroupFile();
            return 0;
        }
        // 点击"文件"工具栏按钮 → 与群文件面板的「上传文件」按钮走同一条路：选文件并
        // 上传进群文件库（群聊里发文件即入群文件库，不在消息区发文件气泡）。真的发起了
        // 一笔才把边栏切到「群文件」Tab，让用户立刻看到顶部那行「上传中 NN%」；用户在
        // 选文件框点了取消就什么都不做，不平白把边栏切走。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbFile)
        {
            if (PickAndUploadGroupFile())
            {
                SwitchSideTab(balloonchatwnd::kGIdTabFiles);
            }
            return 0;
        }
        // 点击「代码」工具栏按钮 → 打开「发送代码」窗口（docs/代码片段方案.md 第 3.1 节）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbCode)
        {
            OpenCodeCompose(std::wstring(), RememberedCodeLang());
            return 0;
        }
        // 点了粘贴识别提示条上的「以代码片段发送」→ 把输入框里的文字移进发送代码窗口（语言按自动识别），输入框清空。
        if (pNotify->ctrlId == balloonchatwnd::kGIdCodePasteSend)
        {
            balloonwjui::DuiControl* pasteContent = GetClientContent();
            balloonchatwnd::ChatInputRichEdit* pasteInput = (pasteContent != NULL)
                ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(pasteContent->FindCtrlById(balloonchatwnd::kGIdInputEdit))
                : NULL;
            // 先收起提示条（同时清掉记下的文字），清空输入框引起的变化通知就不会再处理提示条
            ShowCodePasteBar(false);
            if (pasteInput != NULL)
            {
                const std::wstring code =
                    balloonchatwnd::NormalizeCodeNewlines(std::wstring((LPCWSTR)pasteInput->GetText()));
                pasteInput->SetText(_T(""));
                m_pendingMentions.clear();   // 输入框清空了，记下的 @ 一并作废
                OpenCodeCompose(code, std::string());
            }
            return 0;
        }
        // 点了粘贴识别提示条上的「不用了」→ 只收起提示条，输入框不动，回车照常按普通文字发送。
        if (pNotify->ctrlId == balloonchatwnd::kGIdCodePasteDismiss)
        {
            ShowCodePasteBar(false);
            return 0;
        }
        // 点击"字体"工具栏按钮（AF）→ 打开字体设置面板。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbFont)
        {
            OpenFontPanel();
            return 0;
        }
        // 点击头部的「发起接龙」按钮 → 打开「发起接龙」窗口。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrRelay)
        {
            OpenRelayCompose();
            return 0;
        }
        // 点击头部的「文件共享」按钮 → 在按钮旁弹出本会话的共享列表浮层
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrShare)
        {
            OpenConversationShares();
            return 0;
        }
        // 点击"表情"工具栏按钮 → 打开 程序目录/Face 图片表情选择面板。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbEmoji)
        {
            OpenEmojiPicker();
            return 0;
        }
        // 点击「@提醒」按钮 → 等价于在光标处敲一个 '@'：往输入框插入 '@' 并把焦点
        // 交回去，随后钩子会照常投 kMentionChangedMsg，浮层就按正常路径弹出来 ——
        // 这样按钮和手敲 '@' 走的是同一套逻辑，不会出现两条各自演化的分支。
        // 引用条上的 ✕：取消本次引用（清空待发快照并收起引用条）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdReplyClose)
        {
            CancelPendingReply();
            return 0;
        }
        // 成员搜索框右边的「按部门 / 平铺」切换按钮：切换成员列表的视图（docs/群成员部门树方案.md）
        if (pNotify->ctrlId == balloonchatwnd::kGIdMemberViewToggle)
        {
            ToggleMemberView();
            return 0;
        }

        if (pNotify->ctrlId == balloonchatwnd::kGIdMention)
        {
            balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
            if (edit != NULL)
            {
                edit->SetFocus();
                edit->ReplaceSel(_T("@"));
                ::PostMessage(m_hWnd, kMentionChangedMsg, 0, 0);
            }
            return 0;
        }
        // 点击"图片"工具栏按钮 → 选图并作为群图片消息发送。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbImage)
        {
            PickAndSendImage();
            return 0;
        }
        // 点击"截图"工具栏按钮 → 发起截图。只投递自定义消息、不直接发起：截图浮层是
        // 模态消息循环，在 DUI 鼠标事件派发栈内启动会引发捕获 / 重入问题，导致截图后
        // 发送按钮等点击失灵（与发送▼菜单同理）。延后到鼠标事件外再发起。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbScreenshot)
        {
            ::PostMessage(m_hWnd, kDoScreenshotMsg, (WPARAM)balloonchatwnd::kShotReqCapture, 0);
            return 0;
        }
        // 点击剪刀右边的 ▾ → 弹出截图选项菜单。同样延后到鼠标事件之外再弹（模态菜单）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbScreenshotMenu)
        {
            ::PostMessage(m_hWnd, kDoScreenshotMsg, (WPARAM)balloonchatwnd::kShotReqMenu, 0);
            return 0;
        }
        // 头部「消息记录」按钮 → 请主面板打开本群的历史消息记录窗，落在"全部"分类上。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrHistory)
        {
            RequestOpenHistory(balloonhistory::kHistoryTypeAll);
            return 0;
        }
        // 头部「字幕模式」按钮 → 进入或退出字幕模式（桌面上显示本群最新一条消息的字幕）
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrSubtitle)
        {
            ToggleSubtitleMode();
            return 0;
        }
        // 头部下方的「已 Pin N 条」提示带 → 同样打开历史消息记录窗，但直接切到 Pin
        // 分类。与单聊窗同一套做法，理由见 CBalloonChatWindow::OnDuiNotify 里那段注释。
        if (pNotify->ctrlId == balloonchatwnd::kGIdPinBanner)
        {
            RequestOpenHistory(balloonhistory::kHistoryTypePinned);
            return 0;
        }
        // 群通话横幅 → 加入本群进行中的群通话（本人已在通话里时 DLL 把通话窗提到前面）。离线时加入必然失败，在入口拦住
        if (pNotify->ctrlId == balloonchatwnd::kGIdGroupCallBanner)
        {
            if (!m_callBannerRoomId.empty() && CheckOnlineFor(balloonmain::kActionRealtimeCall))
            {
                balloongroupcall::CGroupCallHub::GetInstance().JoinCall((int64_t)m_groupId, m_groupName,
                                                                        m_callBannerRoomId);
            }
            return 0;
        }
        // 屏幕共享横幅 → 加入本群进行中的共享（本人发起的共享横幅只告知，点击不加入）。离线时加入必然失败，在入口拦住
        if (pNotify->ctrlId == balloonchatwnd::kGIdScreenShareBanner)
        {
            if (!m_shareBannerSelfHost && !m_shareBannerRoomId.empty()
                && CheckOnlineFor(balloonmain::kActionRealtimeCall))
            {
                balloonscreenshare::CScreenShareHub::GetInstance().JoinShare(
                    conversation_group, (int64_t)m_groupId, m_shareBannerRoomId, m_shareBannerHostName,
                    m_shareBannerForced);
            }
            return 0;
        }
        // 头部「群管理」齿轮 → 请主面板打开本群的群管理对话框（仅群主 / 管理员；
        // 齿轮本就只对他们显示，这里再防御一次）。群 id 经堆上 int64_t 随消息回投，
        // 由主面板处理后 delete。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrAdmin)
        {
            const int kRoleAdminValue = 1;
            const int kRoleOwnerValue = 2;
            if ((m_myRole == kRoleAdminValue || m_myRole == kRoleOwnerValue)
                && m_groupId > 0
                && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
            {
                int64_t* pGroupId = new int64_t(m_groupId);
                if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_GROUP_MANAGE, 0,
                                   (LPARAM)pGroupId))
                {
                    delete pGroupId;   // 投递失败（主面板已销毁）自行回收，杜绝泄漏
                }
            }
            return 0;
        }
        // 工具栏「语音消息」→ 开始录音并显示录音条；录音条上的各个按钮交给录音会话（语音消息第 3b、6g 步）。
        // 录音会话只认自己那几个按钮编号，别的控件返回 false、照常往下分派
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbVoice)
        {
            StartVoiceRecording();
            return 0;
        }
        if (m_voiceSession.HandleButton(pNotify->ctrlId))
        {
            return 0;
        }
        // 工具栏「屏幕录制」→ 弹出录制菜单（开始录制 / 停止录制、录制管理，docs/语音字幕方案.md 第 2 节第 10 条），
        // 菜单不能在鼠标事件里弹，先投递消息
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbRecord)
        {
            PostMessage(kOpenRecordMenuMsg);
            return 0;
        }
        // 头部「共享屏幕」按钮 → 经屏幕共享中心交给 FlamingoMedia.dll：先弹出共享内容选择窗，选好后才发起。
        // 正在共享或观看、系统不支持时由 DLL 提示原因。离线时发起必然失败，在入口就拦住
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrScreenShare)
        {
            if (CheckOnlineFor(balloonmain::kActionRealtimeCall) && m_groupId != 0)
            {
                balloonscreenshare::CScreenShareHub::GetInstance().StartShare(conversation_group, (int64_t)m_groupId,
                                                                              m_groupName);
            }
            return 0;
        }
        // 头部「语音通话」/「视频通话」按钮 → 群通话（docs/群音视频通话方案.md）：本群已有群通话时直接加入，
        // 否则先选要振铃的人再发起
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrVoice
            || pNotify->ctrlId == balloonchatwnd::kGIdHdrVideo)
        {
            StartGroupCallFromHeader(pNotify->ctrlId == balloonchatwnd::kGIdHdrVideo ? balloongroupcall::kGroupCallVideo
                                                                                     : balloongroupcall::kGroupCallVoice);
            return 0;
        }
    }

    // 群文件面板：搜索框文字变化（每键触发）→ 按文件名实时过滤、回到第 1 页、重渲染。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdFileSearch)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiEdit* search = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEdit*>(
                  content->FindCtrlById(balloonchatwnd::kGIdFileSearch))
            : NULL;
        if (search != NULL)
        {
            m_fileSearch = CStringToUtf8(search->GetText());
            m_filePage = 0;
            RenderFilePanel();
        }
        return 0;
    }

    // 成员侧栏：搜索框文字变化（每键触发）→ 按名字实时过滤成员行，规则与群管理窗成员页
    // 相同（名字子串、拼音首字母、全拼）。只隐藏没命中的行，不重建。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdMemberSearch)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiEdit* search = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEdit*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMemberSearch))
            : NULL;
        balloonchatwnd::GroupMemberList* memberList = (content != NULL)
            ? dynamic_cast<balloonchatwnd::GroupMemberList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMemberList))
            : NULL;
        if (search != NULL && memberList != NULL)
        {
            memberList->SetFilterKeyword(search->GetText());
            //不记关键词原文（可能是同事的名字），只记长度与命中行数，够判断过滤是否生效
            LOG_INFO("group chat window: member search, group=%lld, keyword length %d, visible %d of %u.",
                     (long long)m_groupId, memberList->FilterKeyword().GetLength(),
                     memberList->VisibleRowCount(), (unsigned)memberList->Children().size());
        }
        return 0;
    }

    // 点发送按钮右侧「▼」→ 弹发送模式切换菜单。这里只投递自定义消息、不直接弹，
    // 避免在 DUI 鼠标事件的同步派发栈里启动模态菜单导致捕获/重入问题。
    if (pNotify != NULL
        && pNotify->code == balloonchatwnd::ChatSendButton::DUIN_SENDBTN_DROPDOWN
        && pNotify->ctrlId == balloonchatwnd::kGIdSendButton)
    {
        ::PostMessage(m_hWnd, kOpenSendMenuMsg, 0, 0);
        return 0;
    }

    // 用户拖完左列中间分隔条（kGIdInputSplitter）→ 记下当前"输入区块高度"（与
    // 单聊共用），供本次运行期间新开的聊天窗、以及本窗口后续缩放时套用。
    // 输入区块高度 = 分隔轴可用高度 - 消息区(pane0)高度。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdInputSplitter)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiSplitter* sp = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiSplitter*>(
                  content->FindCtrlById(balloonchatwnd::kGIdInputSplitter))
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

    // 其它控件的通知本阶段不处理。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam,
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

    if (wParam == (WPARAM)kRelayExpiryTimerId)
    {
        // 一次性定时器：最早的接龙截止时间到了，重画到期的卡片并安排下一次
        OnRelayExpiryTimer();
        return 0;
    }

    if (wParam == (WPARAM)kMemoPanelRefreshTimerId)
    {
        // 一次性定时器：「备忘」页签上最早的提醒时刻到了，重画卡片；RenderMemoPanel 会安排下一次
        ::KillTimer(m_hWnd, kMemoPanelRefreshTimerId);
        RenderMemoPanel();
        return 0;
    }

    if (wParam == (WPARAM)kReplyTimerId)
    {
        // 一次性定时器：先关掉，再追加一条某位群成员的 mock 回复。
        ::KillTimer(m_hWnd, kReplyTimerId);

        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* list =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (list != NULL)
            {
                list->AppendMessage(
                    balloonchatwnd::MakeGroupAutoReply(m_replyIndex));
                ++m_replyIndex;
            }
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnSendOnEnter(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 群聊输入框按 Enter：等同点「发送」按钮。Ctrl+Enter 的换行已由过滤器在框内
    // 就地处理，不会走到这里。
    DoSend();
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnDoScreenshot(UINT /*uMsg*/, WPARAM wParam,
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

LRESULT CBalloonGroupChatWindow::OnOpenRecordMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbRecord);
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
        hub.StartRecording(conversation_group, (int64_t)m_groupId, m_groupName);
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

LRESULT CBalloonGroupChatWindow::OnOpenSendMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdSendButton);
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
    //点了一条快捷回复：直接发出去，输入框里的文字、待发的引用与 @ 都不动
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

void CBalloonGroupChatWindow::SendQuickReply(const CString& reply)
{
    //与按发送键发消息同一道在线检查：离线时给出相同的提示，不发送。仅通知模式、禁言由服务端判断，
    //被拒时气泡标为发送失败，与手工发送的结果一致
    if (!CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return;
    }
    const CString body = balloonchatwnd::QuickReplyBody(reply);
    if (body.IsEmpty())
    {
        return;
    }
    LOG_INFO("group chat window: quick reply sent, group=%lld, chars=%d.", (long long)m_groupId, reply.GetLength());
    //SendPlainText 不带引用与 @、不读也不清输入框
    SendPlainText(std::wstring((LPCWSTR)body));
}

void CBalloonGroupChatWindow::RefreshSendModeUi()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::SendKeyMode mode = balloonchatwnd::GetChatSendMode();

    // 左下角提示文字随模式联动。
    balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdHintLabel));
    if (hint != NULL)
    {
        hint->SetText(balloonchatwnd::GetChatSendHintText(mode));
    }

    // 输入框 placeholder 随模式联动。
    balloonchatwnd::ChatInputRichEdit* input = dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input != NULL)
    {
        input->SetPlaceholder(balloonchatwnd::GetChatSendPlaceholder(mode));
    }
}

void CBalloonGroupChatWindow::ApplyConnectionState(int state)
{
    // 记下变更前的连接态，用于判断本次是不是"从断线恢复到在线"这一跳。
    const bool wasOnline = (m_connUiState == (int)balloonmain::kConnUiOnline);
    m_connUiState = state;

    // 断线期间发出的「拉全量群文件」请求既发不出去也等不到应答，m_fileLoading 会一直
    // 卡在 true —— 此后连切到群文件 Tab 都不会重试，边栏 Tab 上的文件数量就永远出不来。
    // 故在"恢复到在线"这一跳上把那笔作废并重拉一次。只在这一跳做，避免在线态被重复推送
    // 时把正在进行的拉取打断、重复累积。
    if (!wasOnline && state == (int)balloonmain::kConnUiOnline
        && m_groupId != 0 && !m_fileLoaded && m_fileLoading)
    {
        LOG_INFO("群聊窗恢复在线：作废断线期间未完成的群文件拉取并重拉。group=%lld。",
                 (long long)m_groupId);
        m_fileLoading = false;
        m_fileLoadBuffer.clear();
        StartLoadGroupFiles();
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等窗口创建完主面板会再推一次
    }

    const bool bOnline = (state == (int)balloonmain::kConnUiOnline);

    // 顶部提示带：在线时不显示，离线 / 正在连接时显示对应文字。做法与单聊窗一致。
    balloonchatwnd::ChatBannerLabel* banner =
        dynamic_cast<balloonchatwnd::ChatBannerLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdOfflineBanner));
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
            // 两种态都是普通文字、整条不可点，只有配色不同。与单聊窗同一套色值。
            if (state == (int)balloonmain::kConnUiOffline)
            {
                banner->SetBannerColors(kGroupOfflineBannerBgColor,
                                        kGroupOfflineBannerBorderColor);
                banner->SetTextColor(kGroupOfflineBannerColor);
            }
            else
            {
                banner->SetBannerColors(kGroupConnectingBannerBgColor,
                                        kGroupConnectingBannerBorderColor);
                banner->SetTextColor(kGroupConnectingBannerColor);
            }
            banner->SetVisible(true);
        }
        bannerVisibilityChanged = (banner->IsVisible() != wasVisible);
    }

    // 离线时只把发送按钮置灰并不再响应点击；输入框保持可编辑，用户可以继续写草稿，
    // 恢复在线后再发送（2026-10-01 起，此前离线时输入框置为只读）。离线时按回车发送
    // 同样发不出去：DoSend 开头的 CheckOnlineFor 会给出提示并返回，草稿留在输入框里。
    balloonwjui::DuiControl* sendBtn = content->FindCtrlById(balloonchatwnd::kGIdSendButton);
    if (sendBtn != NULL)
    {
        sendBtn->SetEnabled(bOnline);
    }

    // 成员侧栏里本人那一行的头像随本端连接态变灰 / 恢复彩色；别人的头像只看各自的在线状态
    balloonchatwnd::GroupMemberList* memberList =
        dynamic_cast<balloonchatwnd::GroupMemberList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMemberList));
    if (memberList != NULL)
    {
        memberList->SetSelfConnectionStateForAll(state);
    }
    // 消息区里本人的气泡头像同理
    ApplyAvatarGrayToMessageList();

    // 提示条显隐改变了客户区顶部的占位高度，必须补一次整树布局才生效 ——
    // 原因与单聊窗 CBalloonChatWindow::ApplyConnectionState 完全一致，详见那里的说明。
    if (bannerVisibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
    LOG_INFO("GroupChatWindow::ApplyConnectionState: group=%lld, state=%d (0=online 1=connecting 2=offline), "
             "banner visibility changed %d.",
        (long long)m_groupId, state, (int)bannerVisibilityChanged);
}

bool CBalloonGroupChatWindow::CheckOnlineFor(int action)
{
    // 放行与否统一问 OnlineCapability，不在本窗口就地写"连接态是不是在线"——
    // 那样等于把同一条规则在三个窗口各抄一份，日后某类操作要放宽条件时必然漏改。
    if (balloonmain::IsActionAllowedInState(m_connUiState, action))
    {
        return true;
    }

    LOG_INFO("GroupChatWindow::CheckOnlineFor: blocked. group=%lld, action=%d, "
             "connUiState=%d (0=online 1=connecting 2=offline).",
        (long long)m_groupId, action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonGroupChatWindow::DoSend()
{
    // 离线 / 连接未就绪时不发：消息到不了服务器，气泡却会留在窗口里误导用户。
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (input == NULL || list == NULL)
    {
        return;
    }

    // 按文档顺序枚举输入框内容（文本段 / 内嵌图段+标记），聚合成待发送部分序列：
    // 表情（带 faceId 标记的内嵌图）随文字编码进文本部分，截图等非表情图独立成段。
    std::vector<balloonchatwnd::RichInputSegment> segs;
    input->EnumContent(&CollectGroupInputSegment, &segs);
    std::vector<balloonchatwnd::RichOutgoingPart> parts =
        balloonchatwnd::BuildOutgoingParts(segs);
    if (parts.empty())
    {
        // 空 / 全空白，不发。输入框里有内容却聚合不出可发送部分时记一条，以后同类问题有迹可查（BUG-106）
        if (!segs.empty())
        {
            LOG_INFO("GroupChatWindow::DoSend: nothing to send, %u input segment(s) are blank, group=%lld.",
                     (unsigned)segs.size(), (long long)m_groupId);
        }
        return;
    }

    // 整条消息以 ``` 包起来时按代码片段发送（docs/代码片段方案.md 第 3.1 节）：只有一段文字、没有图片时才判断。
    // 聊天输入框已没有 Markdown 模式（docs/Markdown消息方案.md 第 11.2 节），故第二个参数恒为 false。
    // 代码片段不带 @；没有发出（离线、超限）时输入框原样保留
    if (parts.size() == 1 && parts[0].kind == balloonchatwnd::kRichPartText)
    {
        std::wstring fencedCode;
        int fencedLang = codesnippet::code_lang_plain;
        if (balloonchatwnd::TryCodeFenceSend(std::wstring((LPCWSTR)parts[0].text), false, fencedCode, fencedLang))
        {
            if (SendCodeSnippet(fencedCode, fencedLang))
            {
                input->SetText(_T(""));
                m_pendingMentions.clear();
            }
            return;
        }
    }

    // 调试入口（--groupchatwindow，无群 id）：不支持发图。把各文字段拼起来做 mock
    // 回显 + 自动回复，保留离线核对体验。
    if (m_groupId == 0)
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
        // 调试入口也要走一遍 @ 的重扫与换算，否则这条路径下发出来的消息不高亮，
        // 与真实群聊表现不一致，@ 的渲染问题也就没法在调试入口里复现。
        {
            const std::string utf8Echo = CStringToUtf8(joined);
            const std::vector<balloonchatwnd::MentionSpan> atsEcho =
                balloonchatwnd::RescanMentions(utf8Echo, m_pendingMentions);
            if (!atsEcho.empty())
            {
                echo.ats = balloonchatwnd::ConvertSpansForRender(utf8Echo, atsEcho, 0);
            }
        }
        FillSelfSenderAvatar(m_lpFMGClient, echo);   // 自己气泡显示本端真实头像（字形 / 真图）
        list->AppendMessage(echo);
        m_pendingMentions.clear();
        ::SetTimer(m_hWnd, kReplyTimerId, kReplyDelayMs, NULL);
        return;
    }

    // 发送前检查每段文字的体积：超过单条消息上限的，服务端会拒收。
    // 任何一段超限就整条不发、不生成气泡，输入框与 @ 记录原样保留，方便用户删减后再发。
    // @ 按每段重扫后计入，与 SendGroupTextPart 的做法相同。
    const int oversized = balloonchatwnd::FindOversizedTextPart(
        parts, m_currentFont, m_pendingQuote, m_pendingMentions);
    if (oversized >= 0)
    {
        LOG_WARNING("group chat window: text part %d is too large to send, group=%lld.",
                    oversized, (long long)m_groupId);
        ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    // 真实群会话：按文档顺序逐段发出（文字段→群文本消息，内嵌图→群图片消息）。各段
    // 乐观气泡按序追加。注意：发图依赖 RichEdit 里的内嵌对象，故须在全部发送完成<u>之后</u>
    // 才清空输入框（提前清空会把内嵌图一起抹掉）。
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartText)
        {
            SendGroupTextPart(parts[i].text);
        }
        else
        {
            SendGroupImagePart(parts[i].imageIndex);
        }
    }
    input->SetText(_T(""));
    m_pendingShotPaths.clear();   // 内嵌图已发完，清空源文件追踪
    m_pendingMentions.clear();    // @ 记录随本次发送作废，下一条重新累积
}

// =======================================================================
// @提醒：输入侧
// =======================================================================

// 「@提醒」在输入框里的着色（与气泡里 @ 的主题橙同色系，见 ChatBubble.cpp 的
// kAtMeFgOnOther）。插入时给 `@显示名` 这段设成它，与普通正文一眼分开。
static const COLORREF kMentionInputColor = RGB(0xD9, 0x53, 0x1E);

// @ 浮层相对 '@' 字符再往下让出的高度（像素），免得浮层压住正在输入的那一行。
// 取输入框默认行高的近似值即可，不需要精确。
static const int kMentionAnchorLineHeight = 20;

balloonchatwnd::ChatInputRichEdit* CBalloonGroupChatWindow::GetInputEdit()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
}

bool CBalloonGroupChatWindow::ResolveMentionPrefix(CString& prefixOut, int& atPosOut)
{
    prefixOut.Empty();
    atPosOut = -1;

    balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
    if (edit == NULL)
    {
        return false;
    }

    // 取光标位置（选区非空时按选区尾算）与全文。
    long selStart = 0;
    long selEnd   = 0;
    edit->GetSel(selStart, selEnd);
    const int caret = (int)selEnd;

    const CString text = edit->GetText();
    const int len = text.GetLength();
    if (len <= 0 || caret <= 0 || caret > len)
    {
        return false;
    }

    // 从光标往前找最近的 '@'。中途遇到空白 / 换行就放弃 —— 说明这一段不是
    // 正在编辑的 @ 片段（名字里不含空格，@ 后跟空格即视为该片段已结束）。
    int pos = -1;
    for (int i = caret - 1; i >= 0; --i)
    {
        const TCHAR ch = text.GetAt(i);
        if (ch == _T('@'))
        {
            pos = i;
            break;
        }
        if (ch == _T(' ') || ch == _T('\t') || ch == _T('\r') || ch == _T('\n'))
        {
            return false;
        }
    }
    if (pos < 0)
    {
        return false;
    }

    // 这个 '@' 必须位于文本开头或紧跟空白之后，否则是邮箱之类，不当 mention
    //（与发送前重扫 RescanMentions 的判定口径保持一致）。
    if (pos > 0)
    {
        const TCHAR prev = text.GetAt(pos - 1);
        if (prev != _T(' ') && prev != _T('\t') && prev != _T('\r') && prev != _T('\n'))
        {
            return false;
        }
    }

    prefixOut = text.Mid(pos + 1, caret - pos - 1);
    atPosOut  = pos;
    return true;
}

void CBalloonGroupChatWindow::InsertMention(int64_t uid, const CString& name)
{
    balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
    if (edit == NULL || m_mentionAtPos < 0)
    {
        return;
    }

    // @ 浮层这条路：要替换掉的是"从用户键入的 '@' 到当前光标"这一段（即 `@已键入前缀`）。
    long selStart = 0;
    long selEnd   = 0;
    edit->GetSel(selStart, selEnd);
    InsertMentionInRange(uid, name, m_mentionAtPos, (int)selEnd);
    m_mentionAtPos = -1;
}

void CBalloonGroupChatWindow::InsertMentionAtCaret(int64_t uid, const CString& name)
{
    balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
    if (edit == NULL)
    {
        return;
    }

    // 气泡右键「再 @ 一次 TA」这条路：没有任何已键入的 `@前缀` 要替换，就在光标处插入
    // （若当前有选区则替换选区，与普通编辑器的插入手感一致）。
    long selStart = 0;
    long selEnd   = 0;
    edit->GetSel(selStart, selEnd);

    // 必要时补前导空格：重扫只认「文本开头、或紧跟空白之后」的 '@'，
    // 直接接在"你好"后面插出来的 "你好@张三 " 不会被识别成 @提醒。
    if (selStart > 0 && !PrevCharIsWhitespace(edit, (int)selStart))
    {
        edit->SetSel(selStart, selEnd);
        edit->ReplaceSel(_T(" "));
        selStart += 1;
        selEnd = selStart;
    }

    InsertMentionInRange(uid, name, (int)selStart, (int)selEnd);
}

void CBalloonGroupChatWindow::InsertMentionInRange(int64_t uid, const CString& name,
                                                   int begin, int end)
{
    balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
    if (edit == NULL || begin < 0 || end < begin)
    {
        return;
    }

    edit->SetSel(begin, end);

    CString inserted;
    inserted.Format(_T("@%s "), (LPCTSTR)name);   // 末尾空格作分隔，且不计入高亮片段
    edit->ReplaceSel(inserted);

    // 给刚插入的 `@显示名`（不含尾随空格）设主题橙。插入是从 begin 处开始的，
    // 故这一段就是 [begin, begin + 1 + 名字长度)。
    const int nameEnd = begin + 1 + name.GetLength();
    edit->SetSel(begin, nameEnd);
    edit->SetSelTextColor(kMentionInputColor);

    // 光标移到尾随空格之后，并把格式恢复成默认色 —— 否则用户接着打的字会
    // 一路继承橙色。
    const int caretAfter = begin + inserted.GetLength();
    edit->SetSel(caretAfter, caretAfter);
    // 恢复默认文字色。没有对应的窄接口 —— 「自动色」这一档只能靠效果位表达，
    // 用颜色值表达不了，故直接给引擎下发一条设置字符格式的消息。
    CHARFORMAT2 cf;
    ::ZeroMemory(&cf, sizeof(cf));
    cf.cbSize    = sizeof(cf);
    cf.dwMask    = CFM_COLOR;
    cf.dwEffects = CFE_AUTOCOLOR;
    edit->SendMessageToEngine(EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 记一条候选。发送时还会以最终文本为准重扫，这里只是"用户点过谁"的线索。
    balloonchatwnd::MentionCandidate cand;
    cand.m_uid         = uid;
    cand.m_displayName = CStringToUtf8(name);
    m_pendingMentions.push_back(cand);

    // 注意：m_mentionAtPos 由调用方各自收尾 —— @ 浮层那条路要清掉它（那一次
    // "用户键入的 @" 已经被消费），右键菜单这条路它本来就是 -1，不该在这里统一动。
    edit->SetFocus();   // 焦点交回输入框，用户可以接着打字

    LOG_INFO("群聊窗插入 @提醒: uid=%lld.", (long long)uid);
}

void CBalloonGroupChatWindow::OnAtPickThunk(void* userdata, int64_t uid, const CString& name)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self != NULL)
    {
        self->InsertMention(uid, name);
    }
}

LRESULT CBalloonGroupChatWindow::OnMentionChanged(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 先把输入框待重绘的内容刷出去，再做后面可能很慢的浮层工作。
    //
    // 为什么必须这么做：WM_PAINT 是最低优先级消息，而本消息是 PostMessage 投来的，
    // 会<u>排在重绘之前</u>被处理。下面建浮窗 / 填列表 / 摆位置是同步的，几十毫秒过去
    // 才轮到 EDIT 重绘 —— 用户看到的就是"刚敲的 @ 不显示，等浮层弹出来才冒出来"。
    // 让输入框把刚才的改动立刻画出来。无窗口控件没有自己的窗口，失效标记打在
    // 宿主窗口上，由宿主统一重画。
    balloonchatwnd::ChatInputRichEdit* editForPaint = GetInputEdit();
    if (editForPaint != NULL)
    {
        editForPaint->Invalidate();
        ::UpdateWindow(m_hWnd);
    }

    // wParam 非 0 = 钩子告知输入框已失焦。
    if (wParam != 0)
    {
        // 但要排除"失焦是因为点了浮层本身"这种情形：浮层是独立顶层浮窗，点它的
        // 成员行会让输入框<u>瞬间</u>失焦，而失焦发生在鼠标<u>按下</u>那一刻 ——
        // 此时若直接收起，鼠标还没抬起浮层就没了，那一行根本点不中。
        // 故先看光标是不是正落在浮层上：是就保留，让这次点击走完；否则才收起。
        //（主面板的搜索下拉早踩过同一个坑，见 BalloonMainPanel.cpp 的失焦分支。）
        POINT pt;
        ::GetCursorPos(&pt);
        const HWND hUnder    = ::WindowFromPoint(pt);
        const HWND hDropdown = m_atDropdown.GetHwnd();
        if (hDropdown != NULL && hUnder == hDropdown)
        {
            return 0;
        }

        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    // 群成员还没到（或调试入口无群）时不做 @ —— 没有候选可选。
    if (m_memberInfo.empty())
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    CString prefix;
    int     atPos = -1;
    if (!ResolveMentionPrefix(prefix, atPos))
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    const bool wasShowing = (m_mentionAtPos >= 0);
    m_mentionAtPos = atPos;

    if (wasShowing && m_atDropdown.IsShowing())
    {
        m_atDropdown.UpdateFilter(prefix);   // 只是前缀变了，刷新过滤即可
        return 0;
    }

    // 首次进入 @ 状态：把群成员（排除自己）整理成候选并弹出浮层。
    const UINT selfId = (m_lpFMGClient != NULL)
                      ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::vector<balloonchatwnd::AtCandidate> cands;

    // 「所有人」置顶，按群设置「谁可以 @ 全体」开放：群主、管理员恒可，群主放开给所有人时
    // 普通成员也可以（判定见 CanMentionAll，与服务端同一规则）。
    // 没有权限的人看不到这一项，也就没法从浮层里选中它；即便手动打出 "@所有人"，发送前
    // 的重扫也只认"用户真正选过的候选"，扫不出片段，于是只当普通文字发出去。
    if (balloonchatwnd::CanMentionAll(m_myRole, m_atAllScope))
    {
        balloonchatwnd::AtCandidate all;
        all.m_uid  = balloonchatwnd::kMentionUidAll;
        all.m_name = Utf8ToCString(balloonchatwnd::kMentionAllNameUtf8);
        cands.push_back(all);
    }

    //本群可被 @ 的机器人排在成员之前（docs/机器人方案.md §4.2）：只列在本群生效、且有被 @ 规则的机器人
    for (size_t i = 0; i < m_groupBots.size(); ++i)
    {
        const net::GroupBotInfo& b = m_groupBots[i];
        if (!b.m_active || !b.m_atable || b.m_userId <= 0)
        {
            continue;
        }
        balloonchatwnd::AtCandidate one;
        one.m_uid  = b.m_userId;
        one.m_name = Utf8ToCString(b.m_name);
        cands.push_back(one);
    }

    for (std::map<int64_t, MemberInfo>::const_iterator it = m_memberInfo.begin();
         it != m_memberInfo.end(); ++it)
    {
        if (selfId != 0 && (UINT)it->first == selfId)
        {
            continue;   // 不用 @ 自己
        }
        balloonchatwnd::AtCandidate one;
        one.m_uid  = it->first;
        one.m_name = it->second.m_name;
        if (one.m_name.IsEmpty())
        {
            one.m_name.Format(TR(L"用户%lld"), (long long)it->first);
        }
        cands.push_back(one);
    }
    if (cands.empty())
    {
        m_mentionAtPos = -1;
        return 0;
    }

    // 落点取输入框里 '@' 那个字符的位置（转屏幕坐标），并让浮层落在它下一行。
    balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
    POINT anchor;
    anchor.x = 0;
    anchor.y = 0;
    if (edit != NULL)
    {
        // **富文本控件的「按字符位置取坐标」是填出参，不是读返回值**，与普通
        // 编辑框的约定相反：那边把位置当参数传、从返回值里取坐标。照搬过来会
        // 把一个小整数当指针写，后果严重。
        POINTL pt;
        pt.x = 0;
        pt.y = 0;
        edit->SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)atPos);
        anchor.x = pt.x;
        anchor.y = pt.y;
        // 往下挪一行高，免得浮层压住正在输入的那行文字。
        anchor.y += kMentionAnchorLineHeight;
        // 坐标本来就是宿主窗口的客户区坐标 —— 无窗口控件与宿主共用一套坐标。
        ::ClientToScreen(m_hWnd, &anchor);
    }

    m_atDropdown.EnsureCreated();
    m_atDropdown.SetPickCallback(&CBalloonGroupChatWindow::OnAtPickThunk, this);
    m_atDropdown.Show(anchor, cands, prefix);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMentionNav(UINT /*uMsg*/, WPARAM wParam,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_atDropdown.IsShowing())
    {
        return 0;   // 浮层没显示，这次按键与 @ 无关，放行给 EDIT
    }

    switch (wParam)
    {
    case VK_UP:
        m_atDropdown.MoveSelection(-1);
        return 1;
    case VK_DOWN:
        m_atDropdown.MoveSelection(1);
        return 1;
    case VK_RETURN:
    case VK_TAB:
        m_atDropdown.CommitCurrent();   // 回调里会插入并收起浮层
        return 1;
    case VK_ESCAPE:
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 1;
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMentionWheel(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_atDropdown.IsShowing())
    {
        return 0;   // 浮层没显示，这次滚轮与 @ 无关，放行给输入框自己滚
    }
    m_atDropdown.ScrollByWheel((short)wParam);
    return 1;
}

LRESULT CBalloonGroupChatWindow::OnLButtonDownForMention(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                         LPARAM lParam, BOOL& bHandled)
{
    // 点在输入框之外才收起浮层，点在输入框内部要保留。
    //
    // 这条路径的判定方式换过一次。输入框还是内嵌真 Win32 子窗口的时候，点它的鼠标
    // 消息由系统直送该子窗口、不会到达本窗口，所以本窗口收到本消息就等于"点的是
    // 别处"，无须判落点。输入框改成无窗口控件之后这条前提不再成立：点输入框的鼠标
    // 消息同样会到达本窗口，若仍旧无条件收起，用户在浮层显示期间点一下输入框挪动
    // 光标，浮层立即消失，而按设计此时应当保留。故改为在这里自己判落点。
    if (m_atDropdown.IsShowing())
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        // 控件矩形与本消息的鼠标坐标同为宿主窗口客户区坐标，可直接比对。
        balloonchatwnd::ChatInputRichEdit* edit = GetInputEdit();
        bool bInsideInput = false;
        if (edit != NULL)
        {
            RECT rcEdit = edit->GetRect();
            bInsideInput = (::PtInRect(&rcEdit, pt) != FALSE);
        }

        if (bInsideInput)
        {
            // 点在输入框内：这次点击随后会把光标挪到落点上，而输入框只在内容变化时
            // 通知本窗口，光标单纯移动没有通知，浮层的过滤前缀会停在旧值上。故在此
            // 投递一次重算请求，由 OnMentionChanged 按新的光标位置决定是刷新过滤还是
            // 收起浮层（光标已经离开那段 @ 片段时就该收起）。
            // 必须用投递而非直接调用：本次点击尚未交给 DuiHost 派发，光标此刻仍停在
            // 旧位置，就地重算读到的是旧值。
            if (IsWindow())
            {
                ::PostMessage(m_hWnd, kMentionChangedMsg, 0, 0);
            }
        }
        else
        {
            m_atDropdown.Hide();
            m_mentionAtPos = -1;
        }
    }
    bHandled = FALSE;   // 不消费这次点击，继续交给 DuiFrameWindow / DuiHost 派发
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnActivateForMention(UINT /*uMsg*/, WPARAM wParam,
                                                      LPARAM lParam, BOOL& bHandled)
{
    // 切到别的窗口时收起浮层 —— 它是独立顶层窗口，不会跟着宿主一起隐藏。
    //
    // 但要排除"失活是因为焦点转给了浮层自己"：lParam 是本次得到 / 失去激活的
    // 那个窗口，等于浮层就说明用户正在点它，此时收起会让这一次点击落空。
    //（浮层已在 WM_MOUSEACTIVATE 返回 MA_NOACTIVATE 从根上避免夺焦点，这里再兜一道。）
    const HWND hOther = (HWND)lParam;
    if (LOWORD(wParam) == WA_INACTIVE
        && m_atDropdown.IsShowing()
        && hOther != m_atDropdown.GetHwnd())
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
    }
    bHandled = FALSE;
    return 0;
}

// 前置声明：本函数定义在稍后的接收路径附近，而发送路径（乐观气泡）更靠前，先声明一次。
static CString ResolveQuoteThumbPath(long long selfUserId, const net::ReplyQuote& q,
                                     balloonchatwnd::ChatMessageList* list);

void CBalloonGroupChatWindow::SendPlainText(const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    SendGroupTextPart(CString(text.c_str()), false);
}

void CBalloonGroupChatWindow::SendGroupTextPart(const CString& text, bool withQuote)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_groupId == 0)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 先生成 clientMsgId 再建气泡：气泡要带上它，回执到达后才能按它找回本行、把服务器
    // msgid 与时间戳回填进去（撤回菜单的时限判断依赖这两个值）。
    std::string clientMsgId = GenClientMsgId();

    // @提醒：以本段最终文本为准重扫，扫不到的候选自动作废（用户可能把插入的
    // "@张三" 删了一半、或在中间插了字，插入时记的偏移已经不准）。
    std::string utf8 = CStringToUtf8(text);
    //不带引用时（SendPlainText，屏幕共享第 25 期）也不带 @ 提醒：用空引用、空的提醒片段
    const net::ReplyQuote noQuote;
    const net::ReplyQuote& quote = withQuote ? m_pendingQuote : noQuote;
    const std::vector<balloonchatwnd::MentionSpan> ats =
        withQuote ? balloonchatwnd::RescanMentions(utf8, m_pendingMentions)
                  : std::vector<balloonchatwnd::MentionSpan>();

    // 乐观回显气泡（带当前字体）。此刻 msgid / timestampMs 还未知，留 0 待回执回填。
    balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, text);
    echo.font = m_currentFont;
    echo.clientMsgId = clientMsgId;
    // 自己发出的这条也要高亮 @ 片段：把协议的字节口径换算成渲染的字符口径。
    // 自己 @ 的人里若正好有自己，isMe 才为真（一般不会），故仍按本人 uid 判定。
    if (!ats.empty())
    {
        const int64_t selfId = (m_lpFMGClient != NULL)
                             ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        echo.ats = balloonchatwnd::ConvertSpansForRender(utf8, ats, selfId);
    }
    // 本端乐观气泡也要显示引用块 —— 否则"发出去自己看不到引用、对方却看得到"。
    const int64_t selfForQuote = (m_lpFMGClient != NULL)
                               ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::FillQuoteForRender(quote, echo, ResolveQuoteThumbPath(selfForQuote, quote, list));
    FillSelfSenderAvatar(m_lpFMGClient, echo);   // 自己气泡显示本端真实头像（字形 / 真图）
    list->AppendMessage(echo);

    // 经网络层把文本发给群（ctype=2、cid=群id）。回执经 cmd_send_message 应答异步回投；
    // 服务端随后向全群 fan-out。带 @ 时走能写 content.at 的重载。
    // 两个分支都要传 m_pendingQuote：上面的乐观气泡已按它画了引用块，漏传时本端看到引用、
    // 群里其他人收到的却是一条普通文字。
    if (ats.empty())
    {
        CIUSocket::GetInstance().SendGroupTextMessage(m_groupId, clientMsgId, utf8,
                                                      m_currentFont, quote);
    }
    else
    {
        // atAll 由片段推导，不写死 —— 正文里既然有一段 @所有人，协议的
        // content.at.all 就必须跟着置上，否则对端按标志位判"是否@到我"时会漏。
        CIUSocket::GetInstance().SendGroupTextMessageWithMentions(
            m_groupId, clientMsgId, utf8, ats,
            balloonchatwnd::HasMentionAll(ats), m_currentFont, quote);
    }

    // 引用是"一次性"的：这一条带出去之后就该清掉并收起引用条，
    // 否则后面每条消息都会带同一个引用。没带引用时引用条保持原样
    if (withQuote)
    {
        CancelPendingReply();
    }
    LOG_INFO("群聊窗发送一段文字: group=%lld, clientmsgid=%s.",
             (long long)m_groupId, clientMsgId.c_str());

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。
    {
        ConvSendBumpData bump;
        bump.ctype   = 2;                 // 群聊
        bump.cid     = (long long)m_groupId;
        bump.msgType = kMsgTypeText;
        // 会话列表摘要：内联表情标记替换成 [表情]、转义斜杠还原，不暴露 /f["id"]。
        bump.digest  = CStringToUtf8(balloonchat::BodyToDigest(text));
        m_sendBumps.Add(clientMsgId, bump);
    }
}

bool CBalloonGroupChatWindow::SendMarkdownDocument(const std::string& sourceUtf8)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_groupId == 0 || sourceUtf8.empty())
    {
        return false;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return false;
    }
    //源文本按聊天正文的口径转义（字面的 /f[" 写成 //f["），接收方用 MarkdownSourceFromBody 还原
    const std::string bodyUtf8 = balloonmd::MarkdownBodyFromSourceUtf8(sourceUtf8);
    const std::string clientMsgId = GenClientMsgId();
    //乐观气泡：Markdown 消息显示为卡片；不带引用与 @，字体用默认值（卡片不受聊天字体影响）
    balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, Utf8ToCString(bodyUtf8));
    echo.isMarkdown = true;
    echo.clientMsgId = clientMsgId;
    FillSelfSenderAvatar(m_lpFMGClient, echo);
    list->AppendMessage(echo);
    CIUSocket::GetInstance().SendGroupTextMessage(m_groupId, clientMsgId, bodyUtf8, net::TextFont(), net::ReplyQuote(),
                                                  net::text_format_markdown);
    //窗口刚打开、首屏历史还没渲染时，上面插入的卡片会在首屏渲染时被清掉，记下来以便之后重新拉取
    m_mdEarlySends.OnSent(clientMsgId);
    LOG_INFO("group chat window: markdown document sent, group=%lld, clientmsgid=%s, bytes=%u, early=%u.",
             (long long)m_groupId, clientMsgId.c_str(), (unsigned)bodyUtf8.size(),
             (unsigned)m_mdEarlySends.PendingCount());

    // 登记待冒泡：回执成功后主面板把会话冒泡到顶部，预览文字为 Markdown 摘要
    ConvSendBumpData bump;
    bump.ctype   = 2;
    bump.cid     = (long long)m_groupId;
    bump.msgType = kMsgTypeText;
    bump.digest  = balloonchatwnd::MarkdownDigestFromUtf8Body(bodyUtf8);
    m_sendBumps.Add(clientMsgId, bump);
    return true;
}

void CBalloonGroupChatWindow::OpenMarkdownEditor(const std::string& sourceUtf8)
{
    balloonmd::MarkdownSendTarget target;
    target.m_isGroup = true;
    target.m_id = m_groupId;
    target.m_name = std::wstring((LPCWSTR)m_groupName);
    balloonmd::CMarkdownEditorManager::GetInstance().OpenEditor(sourceUtf8, (m_groupId != 0) ? &target : NULL);
    LOG_INFO("group chat window: open markdown editor, group=%lld, bytes=%u.", (long long)m_groupId,
             (unsigned)sourceUtf8.size());
}

void CBalloonGroupChatWindow::OpenCodeCompose(const std::wstring& initialCode, const std::string& langId,
                                              const std::vector<codesnippet::CodeNote>& notes)
{
    std::wstring code = initialCode;
    // 没有指定预填内容时，取输入框里选中的文字（RichEdit 的字符位置与 GetText 的下标一致）
    if (code.empty())
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatInputRichEdit* input = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kGIdInputEdit))
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

std::string CBalloonGroupChatWindow::RememberedCodeLang() const
{
    return (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserConfig.GetCodeSnippetLang() : std::string();
}

long long CBalloonGroupChatWindow::SelfUserIdForCode() const
{
    return (m_lpFMGClient != NULL) ? (long long)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
}

std::map<long long, std::wstring> CBalloonGroupChatWindow::CodeNoteAuthorNames(
    const std::vector<codesnippet::CodeNote>& notes, long long baseSenderId) const
{
    // 要取名字的人：本人、代码片段的发送者、各条批注的作者
    std::vector<long long> ids;
    ids.push_back(SelfUserIdForCode());
    ids.push_back(baseSenderId);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        ids.push_back(notes[i].m_authorId);
    }
    std::map<long long, std::wstring> names;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const long long id = ids[i];
        if (id <= 0 || names.find(id) != names.end())
        {
            continue;
        }
        // 与引用回复同一口径：群成员表里的显示名，取不到时写「用户<id>」
        CString name;
        std::map<int64_t, MemberInfo>::const_iterator it = m_memberInfo.find((int64_t)id);
        if (it != m_memberInfo.end())
        {
            name = it->second.m_name;
        }
        if (name.IsEmpty())
        {
            name.Format(TR(L"用户%lld"), id);
        }
        names[id] = std::wstring((LPCWSTR)name);
    }
    return names;
}

net::ReplyQuote CBalloonGroupChatWindow::CodeBaseQuote(long long baseMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage src;
    if (list == NULL || !list->GetMessageByMsgid(baseMsgId, src))
    {
        LOG_INFO("group chat window: base code snippet %lld is not in the list, the annotation is sent without a quote.",
                 baseMsgId);
        return net::ReplyQuote();
    }
    // 与「引用回复」同一口径：群成员表里的显示名，取不到时写「用户<id>」
    const long long senderId = src.fromMe ? SelfUserIdForCode() : (long long)src.senderUserId;
    CString senderName;
    std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find((int64_t)senderId);
    if (it != m_memberInfo.end())
    {
        senderName = it->second.m_name;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(TR(L"用户%lld"), senderId);
    }
    return balloonchatwnd::BuildQuoteFromMessage(src, senderName);
}

void CBalloonGroupChatWindow::ShowCodeViewer(const std::wstring& code, int lang, const std::vector<codesnippet::CodeNote>& notes,
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
    LOG_INFO("group chat window: code viewer requested, key=%s, msgid=%lld, sender=%lld, notes=%u, edited=%u, open notes=%d.",
             key.c_str(), msgid, senderId, (unsigned)notes.size(), (unsigned)editedNotes.size(), openNotesPane ? 1 : 0);
}

LRESULT CBalloonGroupChatWindow::OnCodeComposeSend(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
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

bool CBalloonGroupChatWindow::SendCodeSnippet(const std::wstring& code, int lang,
                                              const std::vector<codesnippet::CodeNote>& notes,
                                              const net::ReplyQuote* quoteOverride)
{
    // 带上的引用：追加批注时是原消息，平常是输入框里待发的引用
    const net::ReplyQuote& quote = (quoteOverride != NULL) ? *quoteOverride : m_pendingQuote;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || code.empty())
    {
        return false;
    }
    // 离线 / 连接未就绪时不发：消息到不了服务器，气泡却会留在窗口里（调试入口没有真实会话，不检查）
    if (!(m_groupId == 0) && !CheckOnlineFor(balloonmain::kActionSendMessage))
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
        LOG_WARNING("group chat window: code snippet is too large to send, group=%lld, bytes=%u, notes=%u.",
                    (long long)m_groupId, (unsigned)utf8.size(), (unsigned)notes.size());
        ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return false;
    }
    // 调试入口（没有真实会话）：只在本地显示
    if (m_groupId == 0)
    {
        balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, text);
        echo.isCode = true;
        echo.codeLang = lang;
        echo.codeNotes = notes;
        FillSelfSenderAvatar(m_lpFMGClient, echo);
        list->AppendMessage(echo);
        return true;
    }

    // 与 SendGroupTextPart 同一条路径：先生成 clientMsgId 再建乐观气泡，回执到达后按它回填 msgid 与时间。代码片段不带 @ 与字体
    const std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, text);
    echo.isCode = true;
    echo.codeLang = lang;
    echo.codeNotes = notes;
    echo.clientMsgId = clientMsgId;
    const int64_t selfForQuote = (m_lpFMGClient != NULL)
                               ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::FillQuoteForRender(quote, echo, ResolveQuoteThumbPath(selfForQuote, quote, list));
    FillSelfSenderAvatar(m_lpFMGClient, echo);   // 自己气泡显示本端真实头像（字形 / 真图）
    list->AppendMessage(echo);
    CIUSocket::GetInstance().SendGroupTextMessage(m_groupId, clientMsgId, utf8, net::TextFont(), quote,
                                                  net::text_format_code, langId, noteData);
    // 引用是一次性的：带出去之后清掉并收起引用条。追加批注时引用的是原消息，输入框里待发的引用保持不动
    if (quoteOverride == NULL)
    {
        CancelPendingReply();
    }
    LOG_INFO("group chat window: code snippet sent, group=%lld, clientmsgid=%s, lang=%s, bytes=%u, notes=%u.",
             (long long)m_groupId, clientMsgId.c_str(), langId.c_str(), (unsigned)utf8.size(), (unsigned)notes.size());

    // 登记待冒泡：回执成功后主面板把会话冒泡到顶部，摘要写「[代码] 第一行」（追加批注时为「[批注] 说明」）
    ConvSendBumpData bump;
    bump.ctype   = 2;
    bump.cid     = (long long)m_groupId;
    bump.msgType = kMsgTypeText;
    bump.digest  = CStringToUtf8(CString(balloonchatwnd::CodeDigestFromCode(code, notes).c_str()));
    m_sendBumps.Add(clientMsgId, bump);
    return true;
}

void CBalloonGroupChatWindow::ShowCodePasteBar(bool show)
{
    if (!show)
    {
        m_codePasteText.clear();
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* bar = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kGIdCodePasteBar) : NULL;
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

LRESULT CBalloonGroupChatWindow::OnCodePasteNotify(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatInputRichEdit* input = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kGIdInputEdit))
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
            LOG_INFO("group chat window: pasted text looks like code, offer code snippet, group=%lld, chars=%u.",
                     (long long)m_groupId, (unsigned)text.size());
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

void CBalloonGroupChatWindow::SendGroupImagePart(int imageIndex)
{
    // imageIndex 是源文件在 m_pendingShotPaths 里的下标：带标记插入的图从标记读回（见
    // StageImageInInput），未带标记的旧式内嵌图按出现顺序对应。直接发登记下来的原文件。
    if (imageIndex < 0 || imageIndex >= (int)m_pendingShotPaths.size())
    {
        LOG_WARNING("群聊窗发送：内嵌图序号 %d 无对应源文件（图序与追踪不一致），跳过。",
                    imageIndex);
        return;
    }
    SendImageFile(m_pendingShotPaths[imageIndex]);
}

void CBalloonGroupChatWindow::PickAndSendImage()
{
    // 图片要先上传到图片服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无群绑定）不支持真实发图。
    if (m_groupId == 0)
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

void CBalloonGroupChatWindow::SendImageFile(const std::wstring& path)
{
    // 真实群会话才发图（调试入口无群绑定）。截图「发送」与选图发送共用本函数。
    if (m_groupId == 0)
    {
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"的图片气泡（显示本地图），再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        true, CString(path.c_str()), dispW, dispH, clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSelfSenderAvatar(m_lpFMGClient, msg);
    list->AppendMessage(msg);

    // 交图片传输工作线程：申请上传→传字节→发图片消息（scene=2 群聊、cid=群 id）。
    // 进度 / 结果经 FMG_MSG_V2_IMAGE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::ImageTransfer::GetInstance().EnqueueSend(
        2 /*群聊*/, (int64_t)m_groupId, clientMsgId, path);
    LOG_INFO("群聊窗发图: group=%lld, clientmsgid=%s.", (long long)m_groupId, clientMsgId.c_str());

    // 登记待冒泡：发送回执成功后才通知主面板把该会话冒泡到顶部、刷新预览，被服务端拒绝的消息
    // 不冒泡，预览也就不会出现被拦下的内容（docs/敏感词方案.md 8.5）。
    {
        ConvSendBumpData bump;
        bump.ctype   = 2;                 // 群聊
        bump.cid     = (long long)m_groupId;
        bump.msgType = kMsgTypeImage;
        bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeImage, std::string());
        m_sendBumps.Add(clientMsgId, bump);
    }
}

void CBalloonGroupChatWindow::DoScreenshot(bool fullScreen)
{
    // 调试入口（无群绑定）截图无处可发，直接不弹。
    if (m_groupId == 0)
    {
        return;
    }
    // 截图模块没有加载成功时截图按钮已隐藏，这里拦住截图快捷键转来的请求
    if (!balloonscreenshot::IsScreenshotAvailable())
    {
        LOG_INFO("%s: screenshot ignored, the screenshot module is unavailable.", "GroupChatWindow");
        return;
    }
    RefreshScreenshotTip();
    // 模态发起截图：复制 / 保存由浮层内部完成；「发送」立即发图、「发送到对话框」
    // 把截图内联插入输入框（经下面两个回调）。「截图时隐藏当前窗口」开启时本窗口不出现在截图里。
    bool hideSelf = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hideWindow;
    balloonscreenshot::ScreenshotOptions opts;
    opts.m_excludeHwnd = hideSelf ? m_hWnd : NULL;
    opts.m_startFullScreen = fullScreen;
    opts.m_sendFn = &CBalloonGroupChatWindow::OnScreenshotSend;
    opts.m_stageFn = &CBalloonGroupChatWindow::OnScreenshotStage;
    opts.m_ctx = this;
    LOG_INFO("GroupChatWindow::DoScreenshot: group=%lld, full screen=%d, hide self=%d.",
             (long long)m_groupId, fullScreen ? 1 : 0, hideSelf ? 1 : 0);
    // 截图界面在 FlamingoScreenshot.dll 里（2026-10-07，docs/截图独立模块方案.md），经截图宿主模态运行
    balloonscreenshot::RunScreenshot(m_hWnd, opts);
}

void CBalloonGroupChatWindow::RequestScreenshot(bool fullScreen)
{
    if (!IsWindow())
    {
        return;
    }
    ::PostMessage(m_hWnd, kDoScreenshotMsg,
                  (WPARAM)(fullScreen ? balloonchatwnd::kShotReqFullScreen : balloonchatwnd::kShotReqCapture), 0);
}

void CBalloonGroupChatWindow::OpenScreenshotMenu()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_lpFMGClient == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* cut = content->FindCtrlById(balloonchatwnd::kGIdTbScreenshot);
    balloonwjui::DuiControl* caret = content->FindCtrlById(balloonchatwnd::kGIdTbScreenshotMenu);
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
        LOG_INFO("GroupChatWindow: screenshot hide-window option set to %d.", prefs.m_hideWindow ? 1 : 0);
        break;
    //没有选
    case balloonchatwnd::kShotMenuNone:
    default:
        break;
    }
}

void CBalloonGroupChatWindow::RefreshScreenshotTip()
{
    DWORD hotkey = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey;
    CString tip = balloonscreenshot::ScreenshotCommandText(hotkey);
    RegisterTip(GetClientContent(), balloonchatwnd::kGIdTbScreenshot, tip);
}

void CBalloonGroupChatWindow::OnScreenshotSend(void* ctx, const wchar_t* pngPath)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送」走与选图发送同一条链路（乐观气泡 + imgserver 上传 + 会话冒泡）。
    self->SendImageFile(pngPath);
}

void CBalloonGroupChatWindow::OnScreenshotStage(void* ctx, const wchar_t* pngPath)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送到对话框」：把截图作为内联图片插入输入框（按上限缩放展示），不立即发送；
    // 用户可再配文字，按「发送」时由 DoSend 按文档顺序连同文字一起逐段发出。
    self->StageImageInInput(pngPath);
}

bool CBalloonGroupChatWindow::StageImageInInput(const std::wstring& path)
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input == NULL)
    {
        return false;
    }
    // 下标取登记前的表长：这张图的源文件将落在表里的这个位置。标记随图片对象存在文档里，
    // 发送时 BuildOutgoingParts 从标记读回下标，不再依赖"文档第几张 = 第几个源文件"。
    const int index = (int)m_pendingShotPaths.size();
    if (!input->InsertTaggedImage(path.c_str(), balloonchatwnd::ImageTagFromIndex(index),
                                  kGroupInlineImageMaxPx))
    {
        LOG_WARNING("group chat window: failed to stage image into input, index=%d.", index);
        return false;
    }
    // 发送时直接发这个源文件，不从内嵌对象抽位图（那条路在静态图对象上取到的是黑块）。
    m_pendingShotPaths.push_back(path);
    return true;
}

bool CBalloonGroupChatWindow::StageImageThunk(void* ctx, const std::wstring& path)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || !self->IsWindow())
    {
        return false;
    }
    return self->StageImageInInput(path);
}

void CBalloonGroupChatWindow::OnImageSendProgress(const std::string& clientMsgId,
                                                  long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnImageSendResult(bool ok, const std::string& clientMsgId,
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileSendProgress(const std::string& clientMsgId,
                                                 long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId,
                                               int errcode)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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
    }
    else
    {
        list->MarkFileUploadFailed(clientMsgId);
        //气泡下方写原因（如文件名包含敏感词）；文件没能发出，不冒泡
        list->MarkSendFailed(clientMsgId, CString(balloonchatwnd::FileSendFailureText(errcode, IsNotifyOnlyForMe()).c_str()));
        m_sendBumps.Drop(clientMsgId);
    }
}

void CBalloonGroupChatWindow::OnFileDownloadProgress(long long msgid, long long received, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->SetFileDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonGroupChatWindow::OnVoiceSendResult(bool ok, const std::string& clientMsgId, int errcode)
{
    //上传成功：等 1503 的发送回执（FMG_MSG_V2_SEND_CHATMSG_ACK）回填 msgid、去掉发送中的圆环，这里不改气泡
    if (ok)
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list != NULL)
    {
        //气泡下方写「语音发送失败（错误码 N）」；气泡左侧改画红色感叹号，点它可重发
        list->MarkSendFailed(clientMsgId, CString(balloonchatwnd::VoiceSendFailureText(errcode).c_str()));
    }
    //语音没能发出，不冒泡
    m_sendBumps.Drop(clientMsgId);
}

void CBalloonGroupChatWindow::OnVoicePlayedElsewhere(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL && list->MarkVoicePlayedByMsgid(msgid))
    {
        LOG_INFO("group chat window: voice played elsewhere, red dot cleared, group=%lld, msgid=%lld.",
                 (long long)m_groupId, msgid);
    }
}

void CBalloonGroupChatWindow::OnVoiceDownloadResult(const std::string& md5, bool ok, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->ApplyVoiceDownloadResult(md5, ok, CString(localPath.c_str()));
    }
}

void CBalloonGroupChatWindow::OnFileDownloadFailed(long long msgid, bool cancelled)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::RenderIncomingGroupImage(balloonchatwnd::ChatMessageList* list,
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

    // 当前登录用户 userid：缓存按账号隔离（Users\<userId>\ChatImage\），先取好。
    UINT    selfId = 0;
    CString selfName;
    if (m_lpFMGClient != NULL)
    {
        selfId   = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
        selfName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
    }

    // 发送人显示信息：自己用本端头像；他人取群成员映射（查不到回退"用户<id>"）。
    // 群聊气泡显示发送人名（仅他人），与文本气泡同口径。真图头像取成员映射里解析好的路径。
    CString  senderName;
    CString  glyph;
    COLORREF color;
    CString  imagePath;   // 发送人真图头像本地路径（命中缓存才非空）
    if (fromMe)
    {
        glyph = selfName.IsEmpty() ? CString(TR(L"我")) : selfName.Left(1);
        color = AvatarColorForUser(selfId);
    }
    else
    {
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(notify.m_from);
        senderName = sender.m_name;
        color      = sender.m_color;
        glyph      = sender.m_glyph;
        imagePath  = sender.m_imagePath;
    }

    // 缓存命中检查：图片按内容 MD5 缓存在本地账号目录，命中则直接渲染原图、不重复下载
    // （否则每次渲染历史 / 收推送都重下，把 imgserver 刷爆、还堵住串行发送队列）。用文件
    // 实际大小与期望大小比对兜底；期望缺失(<=0)时退回只判存在。与单聊同口径。
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
                cached = true;
            }
            else
            {
                ULONGLONG actualBytes =
                    (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                cached = (actualBytes == static_cast<ULONGLONG>(notify.m_imageSize));
            }
        }
    }

    balloonchatwnd::ChatMessage msg;
    if (cached)
    {
        msg = balloonchatwnd::MakeImageMessage(
            fromMe, CString(cachePath.c_str()), dispW, dispH, std::string(), 0 /*正常*/);
        msg.msgid = (long long)notify.m_msgid;
    }
    else
    {
        msg = balloonchatwnd::MakeDownloadingImageMessage(
            fromMe, dispW, dispH, (long long)notify.m_msgid);
    }
    // 记下原图的 fileid / md5：本条消息将来被别人引用时，要把它们写进 content.quote，
    // 收方才能在引用块里画出缩略图（按 md5 找本地缓存 / 按 fileid 下载）。
    msg.imageFileId = Utf8ToCString(notify.m_imageFileId);
    msg.imageMd5    = Utf8ToCString(notify.m_imageMd5);

    if (fromMe)
    {
        // 自己发的图：本端真实头像（字形 / 真图），不显示发送人名。
        FillSelfSenderAvatar(m_lpFMGClient, msg);
    }
    else
    {
        msg.senderName            = senderName;
        msg.senderUserId          = notify.m_from;
        msg.senderAvatarText      = glyph;
        msg.senderAvatarColor     = color;
        msg.senderAvatarImagePath = imagePath;
    }
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("群聊窗收到图片(缓存命中,免下载): msgid=%lld, md5=%s.",
                 (long long)notify.m_msgid, notify.m_imageMd5.c_str());
        return;
    }
    // 未命中才触发原图下载（fileid 来自消息 content）。结果按 msgid 回投，落地到当前
    // 登录账号缓存目录（selfId）。
    if (!notify.m_imageFileId.empty())
    {
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)notify.m_msgid, notify.m_imageFileId,
            notify.m_imageMd5, notify.m_imageSize, net::CurrentUserAccount());
    }
}

void CBalloonGroupChatWindow::RenderIncomingGroupFile(balloonchatwnd::ChatMessageList* list,
                                                      bool fromMe, const net::MessageNotify& notify)
{
    if (list == NULL)
    {
        return;
    }

    //本地落点查找与缓存命中判定与单聊窗共用；群里没有接收 / 拒绝流程（directDownload=true）
    bool cached = false;
    balloonchatwnd::ChatMessage msg =
        balloonchatwnd::BuildReceivedFileMessage(fromMe, notify, true, &cached);
    msg.timestampMs = notify.m_timestamp;

    if (fromMe)
    {
        // 自己发的：本端真实头像（字形 / 真图），不显示发送人名
        FillSelfSenderAvatar(m_lpFMGClient, msg);
    }
    else
    {
        // 别人发的：真实发送人名 / 头像（取自群成员映射，查不到回退"用户<id>"）
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(notify.m_from);
        msg.senderName            = sender.m_name;
        msg.senderUserId          = notify.m_from;
        msg.senderAvatarText      = sender.m_glyph;
        msg.senderAvatarColor     = sender.m_color;
        msg.senderAvatarImagePath = sender.m_imagePath;
    }
    list->AppendMessage(msg);

    LOG_INFO("group chat window: file message shown, group=%lld, from=%lld, msgid=%lld, cached=%d.",
             (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid,
             cached ? 1 : 0);
}

void CBalloonGroupChatWindow::OnImageDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->SetImageDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonGroupChatWindow::OnImageDownloadFailed(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->MarkImageDownloadFailedByMsgid(msgid);
    }
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

void CBalloonGroupChatWindow::OnRecvGroupMessage(const net::MessageNotify& notify)
{
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 服务端把群消息推给包括发送者在内的全体成员，本人发出的消息也会推回来。本人普通发送
    // 的消息已有乐观气泡，要跳过；本人转发到本群、或在别的设备上发出的，列表里没有，要画成
    // 本人的气泡。两者都按列表里有没有同一条（clientmsgid 或 msgid）区分。
    const bool fromMe = (selfId != 0 && (UINT)notify.m_from == selfId);

    // 桌面字幕由主面板持有，收到的群消息由主面板直接交给字幕（群聊窗关着也要显示），这里不再处理

    // 图片消息：插"下载中"气泡并触发原图下载；下载完成后经
    // FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 回投、按 msgid 回填。
    if (notify.m_msgtype == kMsgTypeImage)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        if (content == NULL)
        {
            return;
        }
        balloonchatwnd::ChatMessageList* list =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
        if (list == NULL)
        {
            return;
        }
        // 本人的回显：乐观气泡或已显示的同一条在列表里就跳过
        if (fromMe && list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        // 去重：该图（同 msgid）已在列表里（如历史已先渲染）则不重复追加。
        if (list->HasImageMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("群聊窗收到图片已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingGroupImage(list, fromMe, notify);
        LOG_INFO("群聊窗收到图片消息: group=%lld, from=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid);
        return;
    }

    // 文件消息（转发进群的文件）：画成文件气泡，不自动下载。本人的回显、以及同一条的重复
    // 推送都按列表里有没有同一条去重（文件气泡的服务器消息ID 记在 fileMsgid 上，也在比对之列）
    if (notify.m_msgtype == kMsgTypeFile)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        RenderIncomingGroupFile(list, fromMe, notify);
        return;
    }

    // 语音消息：画成语音气泡，本机没有这段语音就在后台下载。本人的回显（发送时已有本地气泡）与同一条的重复推送
    // 都按列表里有没有同一条（clientmsgid 或 msgid）去重
    if (notify.m_msgtype == kMsgTypeVoice)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::VoicePlayedQuery played;
        played.m_ctype = conversation_group;
        played.m_cid = m_groupId;
        played.m_msgids.push_back(notify.m_msgid);
        balloonchatwnd::QueryVoicePlayed(m_hostPanel, played);
        RenderIncomingGroupVoice(list, fromMe, notify, played);
        return;
    }

    // 合并转发的聊天记录：画成卡片。本人的回显照样按列表里有没有同一条去重
    if (notify.m_msgtype == kMsgTypeMerged)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupMergedMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: merged record shown, group=%lld, from=%lld, msgid=%lld, items=%d.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid,
                 card.mergedCount);
        return;
    }

    // 文件共享卡片：画成卡片，按列表里有没有同一条去重
    if (notify.m_msgtype == kMsgTypeFileShare)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupShareMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: share card shown, group=%lld, from=%lld, msgid=%lld, shareid=%lld.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid, card.shareId);
        //新的共享卡片：重新拉取本群的共享，新发起的共享由此计入头部按钮的角标
        balloonfileshare::CFileShareHost::GetInstance().WatchConversationShares(conversation_group, m_groupId);
        return;
    }

    // 群通话卡片：画成卡片（复用屏幕共享卡片的版式），按列表里有没有同一条去重（服务端把卡片推给包括发起人在内的全体成员）
    if (notify.m_msgtype == kMsgTypeRtc)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupCallCardMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: group call card shown, group=%lld, from=%lld, msgid=%lld, room=%s, state=%d.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid,
                 card.screenShareRoomId.c_str(), card.screenShareState);
        return;
    }

    // 屏幕共享卡片：画成卡片，按列表里有没有同一条去重（服务端把卡片推给包括发起人在内的全体成员）
    if (notify.m_msgtype == kMsgTypeScreenShare)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupScreenShareMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: screen share card shown, group=%lld, from=%lld, msgid=%lld, room=%s, state=%d.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid,
                 card.screenShareRoomId.c_str(), card.screenShareState);
        return;
    }

    // 群公告卡片、群备忘卡片：画成卡片（复用屏幕共享卡片的版式），按列表里有没有同一条去重。服务端把卡片推给
    // 包括操作人本人在内的全部可见者，本人的各台设备上同样出现这张卡片
    if (notify.m_msgtype == kMsgTypeAnnouncement || notify.m_msgtype == kMsgTypeMemo)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupInfoCardMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: info card shown, group=%lld, from=%lld, msgid=%lld, msgtype=%d, ref=%lld, openable=%d.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid, notify.m_msgtype,
                 card.infoCardRefId, card.screenShareButtonEnabled ? 1 : 0);
        return;
    }

    // 云端录像卡片：画成卡片（复用屏幕共享卡片的版式），按列表里有没有同一条去重。服务端把卡片推给包括开启者本人在内的
    // 全体群成员，本人的各台设备上同样出现这张卡片
    if (notify.m_msgtype == kMsgTypeCloudRecord)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupCloudRecordMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: cloud record card shown, group=%lld, from=%lld, msgid=%lld, record=%lld.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid, card.infoCardRefId);
        return;
    }

    // 群接龙卡片：画成卡片。服务端把卡片推给包括发起人在内的全体成员，本人的回显同样按列表里
    // 有没有同一条去重（发起时没有乐观气泡，回显就是发起人看到的那一张）
    if (notify.m_msgtype == kMsgTypeRelay)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        balloonchatwnd::ChatMessage card = MakeGroupRelayMessage(notify, fromMe, list);
        list->AppendMessage(card);
        LOG_INFO("group chat window: relay card shown, group=%lld, from=%lld, msgid=%lld, items=%d, state=%d.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid,
                 card.relayCount, card.relayState);
        return;
    }

    // 服务端生成的系统提示：画成居中提示行，没有发送人气泡。按 msgid 去重
    if (notify.m_msgtype == kMsgTypeSystem)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::ChatMessageList* list = (content != NULL)
            ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
                  content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
            : NULL;
        if (list == NULL || list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        list->AppendMessage(balloonchatwnd::MakeSystemTipRow(notify));
        LOG_INFO("group chat window: system tip shown, group=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_msgid);
        return;
    }

    // 其余非文本类型：解析层只在文本类型时填 m_text，空即非文本（暂不渲染）。
    if (notify.m_text.empty())
    {
        LOG_INFO("群聊窗收到非文本消息，暂不渲染: from=%lld, msgtype=%d.",
                 (long long)notify.m_from, notify.m_msgtype);
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 本人发出的文字：列表里已有同一条（普通发送的乐观气泡、或重复推送）就跳过；
    // 没有（转发到本群、在别的设备上发出）就画成右侧的本端气泡
    if (fromMe)
    {
        if (list->ContainsMessage((long long)notify.m_msgid, notify.m_clientmsgid))
        {
            return;
        }
        list->AppendMessage(MakeSelfTextMessage(notify, Utf8ToCString(notify.m_text), list));
        LOG_INFO("group chat window: own message shown from echo, group=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_msgid);
        return;
    }

    // 发送人显示信息：取自群成员映射（card→nickname），查不到回退"用户<id>"；
    // 机器人消息在本群机器人列表里也查不到时按消息自带的机器人名字显示。
    const balloonchatwnd::GroupSenderRender sender =
        ResolveSenderWithBotMark(notify.m_from, notify.m_botId, notify.m_botName);
    const CString  senderName      = sender.m_name;
    const CString  senderGlyph     = sender.m_glyph;
    const COLORREF senderColor     = sender.m_color;
    const CString  senderImagePath = sender.m_imagePath;   // 发送人真图头像本地路径（命中缓存才非空）

    // 渲染为对方气泡（fromMe=false），并设真实发送人名 / 头像（群聊气泡显示发送人）。
    // 按该条消息携带的字体渲染（对端设过字体时与其一致）。
    balloonchatwnd::ChatMessage msg =
        balloonchatwnd::MakeTextMessage(false, Utf8ToCString(notify.m_text));
    msg.font                  = notify.m_font;
    msg.isMarkdown            = (notify.m_textFormat == net::text_format_markdown);
    balloonchatwnd::ApplyCodeFields(msg, notify.m_textFormat, notify.m_codeLang, notify.m_codeNotes, notify.m_from);
    //撤回判定所需：服务器 msgid 与消息时间，按报文原样带上。
    msg.msgid                 = notify.m_msgid;
    msg.timestampMs           = notify.m_timestamp;
    msg.senderName            = senderName;
    msg.senderUserId          = notify.m_from;
    msg.senderAvatarText      = senderGlyph;
    msg.senderAvatarColor     = senderColor;
    msg.senderAvatarImagePath = senderImagePath;
    msg.senderIsBot           = sender.m_isBot;
    // @提醒：把协议的 UTF-8 字节偏移换算成渲染用的 UTF-16 字符下标，并按本人 uid
    // 标好 isMe（决定气泡里用橙色还是蓝色）。本条不含 @ 时为空，气泡走原快路径。
    // 与 OnHistoryMessages 走同一个 BuildRenderSpansForMessage，两条路不会再跑偏。
    msg.ats = balloonchatwnd::BuildRenderSpansForMessage(
        notify.m_text, notify.m_ats, notify.m_atAll, (int64_t)selfId);
    // 引用回复：与历史渲染共用 FillQuoteForRender，两条路不会跑偏（@ 那次的教训）。
    balloonchatwnd::FillQuoteForRender(notify.m_quote, msg,
                                       ResolveQuoteThumbPath((int64_t)selfId, notify.m_quote, list));
    list->AppendMessage(msg);

    LOG_INFO("群聊窗渲染收到的群消息: group=%lld, from=%lld, msgid=%lld.",
             (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid);
}

void CBalloonGroupChatWindow::RebuildMemberRows()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupMemberList* list =
        dynamic_cast<balloonchatwnd::GroupMemberList*>(content->FindCtrlById(balloonchatwnd::kGIdMemberList));
    if (list == NULL)
    {
        return;
    }
    // 建成员行的上下文：本人、本人角色、本端连接态、各成员的在线状态，以及右键菜单前问主面板关注名单的钩子
    // （与消息区共用同一对钩子：别处改过关注名单之后这里不会因此重建，故不能存状态位）
    balloonchatwnd::GroupMemberRowContext ctx;
    ctx.m_selfId = (m_lpFMGClient != NULL) ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    ctx.m_myRole = m_myRole;
    ctx.m_connUiState = m_connUiState;
    ctx.m_presence = &m_memberPresence;
    ctx.m_starredFn = &CBalloonGroupChatWindow::StarredThunk;
    ctx.m_starredUser = this;
    ctx.m_followFn = &CBalloonGroupChatWindow::GroupFollowThunk;
    ctx.m_followUser = this;
    // 群聊窗有输入框，右键菜单里保留「@TA」
    ctx.m_allowMention = true;
    // 平铺：群主 → 管理员 → 普通成员；按部门：部门标题行与成员行按部门树的顺序挂上（docs/群成员部门树方案.md）。
    // 挂完之后按搜索框里的关键词重新过滤、重新排版
    const size_t rows = balloonchatwnd::FillGroupMemberList(list, m_lastMembers, m_memberInfo, m_memberTreeView,
                                                            m_orgDepartments, ctx);
    if (m_memberTreeView)
    {
        LOG_INFO("group chat window: member dept tree rebuilt, group=%lld, members=%u, rows=%u, depts=%u.",
                 (long long)m_groupId, (unsigned)m_lastMembers.size(), (unsigned)rows,
                 (unsigned)m_orgDepartments.size());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::SetOrgDepartments(const std::vector<net::DepartmentItem>& departments)
{
    m_orgDepartments = departments;
    // 部门树视图且成员已到齐：按新的部门数组重建；平铺视图不受部门影响
    if (m_memberTreeView && !m_lastMembers.empty())
    {
        RebuildMemberRows();
    }
}

void CBalloonGroupChatWindow::ToggleMemberView()
{
    m_memberTreeView = !m_memberTreeView;
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->m_UserConfig.SetGroupMemberTreeView(m_memberTreeView);
        m_lpFMGClient->SaveUserConfig();
    }
    LOG_INFO("group chat window: member view toggled, group=%lld, tree=%d.", (long long)m_groupId,
             m_memberTreeView ? 1 : 0);
    balloonchatwnd::ApplyMemberViewToggle(GetClientContent(), m_memberTreeView);
    RebuildMemberRows();
}

void CBalloonGroupChatWindow::OnGroupMembersArrived(const net::GroupMembersResult& members)
{
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 建"发送人 userid → 昵称/头像"映射（群名片优先→账号昵称→"用户<id>"）。头像按成员 (facetype, customface)
    // 解析：字形 / 自动头像给字形 + 底色，真图头像缓存命中给真图路径、未命中入队下载（完成后 OnAvatarDownloaded
    // 回刷）。角色一并记下，气泡里 @某人 的右键菜单要按角色决定管理项（见 MemberRoleThunk）。
    // 算法与帖子群的群成员面板共用（GroupMemberPanelKit）
    m_memberInfo = balloonchatwnd::BuildGroupMemberRowInfo(members.m_members, selfId);
    // 非成员的通讯录查询结果随成员表一起作废：有人可能重新入群，通讯录也可能已更新
    m_nonMemberSenders.clear();
    //群通话窗请求过「邀请成员」、当时成员表还没加载：现在补开选人框
    if (m_callInvitePending && !m_memberInfo.empty())
    {
        m_callInvitePending = false;
        const std::vector<int64_t> exclude = m_callInviteExclude;
        m_callInviteExclude.clear();
        OpenGroupCallInvitePicker(exclude);
    }

    // 填右侧成员侧栏：记下这份成员，按当前视图（平铺 / 按部门）重建成员行
    m_lastMembers = members.m_members;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    RebuildMemberRows();

    // 成员到齐 → 边栏「成员」Tab 换成真实人数（"成员 12"）。群成员变动推送会让主面板
    // 重新拉一次成员（见 CBalloonMainPanel::OnGroupMemberChanged），故人数能一直是活的。
    RefreshSideTabCounts();

    // 首屏历史与成员是同时请求的，历史可能已经先到并渲染出来了：那时成员表还空着，
    // 对方消息是按备用信息（"用户<id>"）画的，这里就地换成真实名字与头像。群成员变动
    // 导致成员表重新拉取时也走这里，只刷新显示信息，不再像原先那样重拉首屏历史（那会
    // 清空列表、滚动位置跳回底部）。
    balloonchatwnd::ChatMessageList* msgList =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    int refreshed = 0;
    if (msgList != NULL)
    {
        //先成员表、再通讯录：已退群的人以前发的消息也能换成真名
        refreshed = balloonchatwnd::ApplyGroupSendersToList(
            *msgList, &CBalloonGroupChatWindow::LookupSenderChain, this);
    }
    //接龙卡片里的名字同理：先于成员表渲染的卡片上写的是「用户<id>」，按成员表重填一遍
    RefreshAllRelayCards();

    //sinceOpen：从打开窗口到成员填充完成的累计毫秒数；refreshed：先于成员渲染出来、
    //这次被换成真实发送者信息的气泡数。
    BUI_TRACE("GROUPWND-MEMBERS-FILLED group=%lld count=%u refreshed=%d sinceOpen=%.3fms",
              (long long)m_groupId, (unsigned)members.m_members.size(), refreshed,
              m_openStopWatch.ElapsedMs());
    LOG_INFO("group chat members filled: group=%lld, count=%u, refreshed bubbles=%d, "
             "sinceOpen=%.1fms.", (long long)m_groupId, (uint32_t)members.m_members.size(),
             refreshed, m_openStopWatch.ElapsedMs());
}

void CBalloonGroupChatWindow::SetSubtitleHighlight(bool active)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatIconButton* btn =
        dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kGIdHdrSubtitle));
    if (btn != NULL)
    {
        btn->SetActive(active);
    }
}

void CBalloonGroupChatWindow::ToggleSubtitleMode()
{
    //字幕由主面板持有（不随本窗口关闭）：同步请主面板切换，按结果高亮按钮（主面板的状态回调也会再设一次）
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        LOG_INFO("group chat window: subtitle toggle ignored, no host panel, group=%lld.", (long long)m_groupId);
        return;
    }
    int64_t cid = m_groupId;
    const bool active = ::SendMessage(m_hostPanel, FMG_MSG_TOGGLE_SUBTITLE, (WPARAM)conversation_group,
                                      reinterpret_cast<LPARAM>(&cid)) != 0;
    SetSubtitleHighlight(active);
    LOG_INFO("group chat window: subtitle mode %s, group=%lld.", active ? "on" : "off", (long long)m_groupId);
}

balloonchatwnd::GroupSenderRender CBalloonGroupChatWindow::ResolveSender(int64_t userId) const
{
    balloonchatwnd::GroupSenderRender found;
    const bool hit = LookupSenderChain(const_cast<CBalloonGroupChatWindow*>(this), userId, found);
    return balloonchatwnd::ResolveGroupSender(userId, hit ? &found : NULL,
                                              AvatarColorForUser(userId));
}

balloonchatwnd::GroupSenderRender CBalloonGroupChatWindow::ResolveSenderWithBotMark(int64_t userId, int64_t botId,
                                                                                    const std::string& botNameUtf8) const
{
    balloonchatwnd::GroupSenderRender found;
    bool hit = LookupSenderChain(const_cast<CBalloonGroupChatWindow*>(this), userId, found);
    //三处都查不到、消息又带机器人标记：机器人已删除或不再用于本群，按标记里的名字显示
    if (!hit)
    {
        hit = balloonchatwnd::SenderFromBotMark(botId, Utf8ToCString(botNameUtf8), found);
    }
    return balloonchatwnd::ResolveGroupSender(userId, hit ? &found : NULL,
                                              AvatarColorForUser(userId));
}

bool CBalloonGroupChatWindow::LookupSenderChain(void* ctx, int64_t userId,
                                                balloonchatwnd::GroupSenderRender& out)
{
    //机器人账号不在成员表里，先查本群机器人列表，免得落到通讯录查询、被缓存成「查不到」
    if (LookupBotSender(ctx, userId, out))
    {
        return true;
    }
    return balloonchatwnd::LookupGroupSenderChain(userId,
                                                  &CBalloonGroupChatWindow::LookupMemberSender, ctx,
                                                  &CBalloonGroupChatWindow::LookupContactSender, ctx,
                                                  out);
}

bool CBalloonGroupChatWindow::LookupContactSender(void* ctx, int64_t userId,
                                                  balloonchatwnd::GroupSenderRender& out)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || userId <= 0)
    {
        return false;
    }

    std::map<int64_t, NonMemberSender>::const_iterator cached = self->m_nonMemberSenders.find(userId);
    if (cached != self->m_nonMemberSenders.end())
    {
        if (cached->second.m_found)
        {
            out = cached->second.m_render;
        }
        return cached->second.m_found;
    }

    //拿不到主面板（调试入口）：不缓存，直接按查不到处理
    if (self->m_hostPanel == NULL || !::IsWindow(self->m_hostPanel))
    {
        return false;
    }

    //同步问主面板 —— 两者都在 UI 线程上，SendMessage 就是一次直接调用；查询结构在本栈上，
    //主面板就地填写，不转移所有权
    balloonchatwnd::ContactBriefQuery query;
    query.m_userId = userId;
    ::SendMessage(self->m_hostPanel, FMG_MSG_QUERY_CONTACT_BRIEF, 0, (LPARAM)&query);

    NonMemberSender entry;
    entry.m_found = query.m_found;
    if (query.m_found)
    {
        const int64_t selfId = (self->m_lpFMGClient != NULL)
                               ? (int64_t)self->m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        CString name = Utf8ToCString(query.m_nickname);
        if (name.IsEmpty())
        {
            name.Format(TR(L"用户%lld"), (long long)userId);
        }
        //头像解析与成员同一套：字形头像显示选定字形，真图命中缓存给路径、未命中入队下载
        //（下载完成后 OnAvatarDownloaded 按 m_customface 回填）
        const balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
            userId, query.m_facetype, query.m_customface, name, selfId);
        entry.m_render.m_name      = name;
        entry.m_render.m_glyph     = ar.m_glyph;
        entry.m_render.m_color     = ar.m_color;
        entry.m_render.m_imagePath = ar.m_imagePath;
        entry.m_facetype           = query.m_facetype;
        entry.m_customface         = query.m_customface;
        out = entry.m_render;
    }
    self->m_nonMemberSenders[userId] = entry;
    LOG_INFO("group chat window: sender %lld not in member list, contact lookup found=%d, group=%lld.",
             (long long)userId, (int)query.m_found, (long long)self->m_groupId);
    return query.m_found;
}

bool CBalloonGroupChatWindow::LookupBotSender(void* ctx, int64_t userId, balloonchatwnd::GroupSenderRender& out)
{
    const CBalloonGroupChatWindow* self = static_cast<const CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || userId <= 0)
    {
        return false;
    }
    for (size_t i = 0; i < self->m_groupBots.size(); ++i)
    {
        const net::GroupBotInfo& b = self->m_groupBots[i];
        if (b.m_userId != userId)
        {
            continue;
        }
        out.m_name = Utf8ToCString(b.m_name);
        out.m_glyph = balloonchatwnd::BotAvatarText(b).c_str();
        //头像底色 "#RRGGBB"；没有设置或格式不对时按 userid 取默认色
        out.m_color = balloonchatwnd::BotAvatarColor(b.m_color, AvatarColorForUser(userId));
        out.m_imagePath.Empty();
        out.m_isBot = true;
        return true;
    }
    return false;
}

void CBalloonGroupChatWindow::RequestGroupBots()
{
    if (m_groupId == 0 || m_lpFMGClient == NULL || !m_lpFMGClient->m_UserMgr.m_bBotFeature)
    {
        return;
    }
    CIUSocket::GetInstance().SendGetGroupBots(m_groupId);
}

void CBalloonGroupChatWindow::OnGroupBotsArrived(const net::GroupBotsResult& result)
{
    if (result.m_code != 0)
    {
        LOG_WARNING("group chat window: group bots request failed, group=%lld, code=%d.", (long long)m_groupId,
                    result.m_code);
        return;
    }
    m_groupBots = result.m_bots;
    m_groupBotCaps = result.m_caps;
    m_groupBotsLoaded = true;
    //先于机器人列表渲染出来的机器人消息：名字、头像与「机器人」徽章就地刷新
    int refreshed = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* msgList = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (msgList != NULL)
    {
        refreshed = balloonchatwnd::ApplyGroupSendersToList(*msgList, &CBalloonGroupChatWindow::LookupSenderChain, this);
    }
    RefreshBotMemberSection();
    LOG_INFO("group chat window: group bots arrived, group=%lld, bots=%u, owner=%d, refreshed=%d.", (long long)m_groupId,
             (unsigned)m_groupBots.size(), m_groupBotCaps.m_isOwner ? 1 : 0, refreshed);
}

void CBalloonGroupChatWindow::OnGroupBotsChanged()
{
    //企业关闭了机器人：清空列表并隐藏机器人区域；已显示的气泡保留原样（历史消息的名字仍可从通讯录或「用户<id>」得到）
    if (m_lpFMGClient == NULL || !m_lpFMGClient->m_UserMgr.m_bBotFeature)
    {
        m_groupBots.clear();
        m_groupBotsLoaded = false;
        RefreshBotMemberSection();
        return;
    }
    RequestGroupBots();
}

const std::vector<net::GroupBotInfo>& CBalloonGroupChatWindow::GetGroupBots() const
{
    return m_groupBots;
}

void CBalloonGroupChatWindow::RefreshBotMemberSection()
{
    //这一栏只用来查看：群里有机器人时所有人都看得到；添加与管理在群管理窗口的「群机器人」页（2026-10-08 用户确认）
    const bool botOn = (m_lpFMGClient != NULL) && m_lpFMGClient->m_UserMgr.m_bBotFeature;
    balloonchatwnd::GroupBotSectionView view;
    view.m_show = balloonchatwnd::ShouldShowBotSection(botOn, m_groupBotsLoaded, m_groupBots.size());
    if (view.m_show)
    {
        view.m_title = TRF(L"机器人 {0}", (int)m_groupBots.size());
        //最多 kGIdBotRowLast - kGIdBotRowFirst + 1 行，超出的不显示
        const size_t maxRows = (size_t)(balloonchatwnd::kGIdBotRowLast - balloonchatwnd::kGIdBotRowFirst + 1);
        view.m_rows = balloonchatwnd::BuildBotSectionRows(m_groupBots, balloonchatwnd::kGIdBotRowFirst, maxRows);
    }
    const bool applied = balloonchatwnd::ApplyGroupBotSection(GetClientContent(), view);
    LOG_INFO("group chat window: bot section refreshed, group=%lld, show=%d, bots=%u, applied=%d.", (long long)m_groupId,
             view.m_show ? 1 : 0, (unsigned)m_groupBots.size(), applied ? 1 : 0);
}

LRESULT CBalloonGroupChatWindow::OnGroupBotActionMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    switch ((int)wParam)
    {
    //点了某个机器人：弹出资料卡
    case kGroupBotActionCard:
    {
        const size_t index = (size_t)lParam;
        if (index >= m_groupBots.size())
        {
            break;
        }
        const net::GroupBotInfo& b = m_groupBots[index];
        m_botCard.Open(m_hWnd, balloonchatwnd::MakeBotCardView(b, AvatarColorForUser(b.m_userId)));
        break;
    }
    //不认识的操作
    default:
        break;
    }
    return 0;
}

bool CBalloonGroupChatWindow::LookupMemberSender(void* ctx, int64_t userId,
                                                 balloonchatwnd::GroupSenderRender& out)
{
    const CBalloonGroupChatWindow* self = static_cast<const CBalloonGroupChatWindow*>(ctx);
    if (self == NULL)
    {
        return false;
    }
    std::map<int64_t, MemberInfo>::const_iterator it = self->m_memberInfo.find(userId);
    if (it == self->m_memberInfo.end())
    {
        return false;
    }
    out.m_name      = it->second.m_name;
    out.m_glyph     = it->second.m_glyph;       // 字形头像显示成员选定字形
    out.m_color     = it->second.m_color;
    out.m_imagePath = it->second.m_imagePath;   // 真图头像命中缓存的本地路径
    return true;
}

void CBalloonGroupChatWindow::OnAvatarDownloaded(const std::string& fileid)
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

    // 刚下好的真图缓存路径（取不到则不刷）。
    std::wstring path =
        net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), fileid);
    if (path.empty() || ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    CString imagePath = path.c_str();

    //刚下好的若是本群的群头像，重建标题栏 / 任务栏图标（此前画的是字形占位）。
    if (!m_groupCustomface.empty() && m_groupCustomface == fileid)
    {
        RebuildGroupIcons();
    }

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;

    // 成员映射里 customface 命中该 fileid 的成员：回填其真图路径 + 刷新气泡 + 成员侧栏。
    for (std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.begin();
         it != m_memberInfo.end(); ++it)
    {
        if (it->second.m_facetype != balloonmain::avatar_kind_image
            || it->second.m_customface != fileid)
        {
            continue;
        }
        it->second.m_imagePath = imagePath;
        if (list != NULL)
        {
            list->RefreshSenderAvatar(it->first, imagePath);
        }
    }

    // 已退群成员（经通讯录查到的）的真图头像命中：回填路径并刷新他们的气泡。
    for (std::map<int64_t, NonMemberSender>::iterator nit = m_nonMemberSenders.begin();
         nit != m_nonMemberSenders.end(); ++nit)
    {
        if (!nit->second.m_found
            || nit->second.m_facetype != balloonmain::avatar_kind_image
            || nit->second.m_customface != fileid)
        {
            continue;
        }
        nit->second.m_render.m_imagePath = imagePath;
        if (list != NULL)
        {
            list->RefreshSenderAvatar(nit->first, imagePath);
        }
    }

    // 本人真图头像命中（自己的头像通常本地已缓存，少经此路径）：刷新本人气泡。
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

    // 成员侧栏：按 userid 找到引用该真图头像的成员行，更新其真图路径并重绘。
    if (content != NULL)
    {
        balloonchatwnd::GroupMemberList* memberList =
            dynamic_cast<balloonchatwnd::GroupMemberList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMemberList));
        if (memberList != NULL)
        {
            const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
                memberList->Children();
            for (size_t i = 0; i < kids.size(); ++i)
            {
                balloonchatwnd::GroupMemberItem* item =
                    dynamic_cast<balloonchatwnd::GroupMemberItem*>(kids[i].get());
                if (item == NULL)
                {
                    continue;
                }
                std::map<int64_t, MemberInfo>::iterator mi =
                    m_memberInfo.find(item->UserId());
                if (mi != m_memberInfo.end()
                    && mi->second.m_facetype == balloonmain::avatar_kind_image
                    && mi->second.m_customface == fileid)
                {
                    item->SetAvatarImage(imagePath);
                }
            }
            memberList->Invalidate();
        }
    }
}

void CBalloonGroupChatWindow::LocateMessage(long long msgid)
{
    m_pendingLocateMsgid = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || !list->ScrollToMsgid(msgid))
    {
        //此刻没找到（历史还在途 / 该消息较早未加载）：记为待定，等历史渲染后补定位。
        m_pendingLocateMsgid = msgid;
    }
}

void CBalloonGroupChatWindow::OnHistoryMessages(const net::SyncMessagesResult& result)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 本次应答是首屏还是"往上翻的一页"？服务端应答里不回显 before，只能靠本端
    // 记的在途标志区分 —— 只有 OnChatLoadOlder 发出的翻页请求会把它置真。
    const bool isOlderPage = m_paging.m_loading;

    //应答到达时刻（从打开窗口起算）与本次渲染耗时，写进下面的日志供性能排查。
    const double arrivedMs = m_openStopWatch.ElapsedMs();
    PerfStopWatch renderStopWatch;
    BUI_TRACE("GROUPWND-HISTORY-ARRIVED group=%lld count=%u older=%d sinceOpen=%.3fms",
              (long long)m_groupId, (unsigned)result.m_messages.size(),
              isOlderPage ? 1 : 0, arrivedMs);

    //按消息类型分段统计渲染耗时，写进下面的 GROUPWND-HISTORY-BREAKDOWN 跟踪行，
    //用来判断首屏渲染的时间花在哪一类消息上
    balloonchatwnd::HistoryRenderBreakdown breakdown;
    PerfStopWatch stageWatch;

    if (isOlderPage)
    {
        // 翻页：绝不能清空。开一个"头部插入批次"，下面照常按时间正序渲染，
        // 批次结束时这一段会被整体挪到列表头部，滚动位置也一并补偿。
        list->BeginPrependBatch();
    }
    else
    {
        // 首屏是"最近 N 条"完整快照：渲染前先清空列表，避免与窗口刚创建时（拉历史
        // 仍在途中）收到推送、被 OnRecvGroupMessage 乐观追加进来的那几行叠加，导致
        // 这些 live 行卡在最前面 / 重复。清空后按历史时间正序重渲，位置即正确。
        // 与单聊窗 CBalloonChatWindow::OnHistoryMessages 同一套处理。
        list->ClearAllMessages();
    }
    breakdown.m_prepareMs = stageWatch.ElapsedMs();

    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 本页里的语音消息：一次查出本地库记为已听的，供下面决定画不画红点。历史先显示后入库，
    // 库里还没有的别人的语音按未听处理（与随后入库时写入的未听一致）
    balloonchatwnd::VoicePlayedQuery voicePlayed;
    voicePlayed.m_ctype = conversation_group;
    voicePlayed.m_cid = m_groupId;
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
    recallDrafts.m_ctype = conversation_group;
    recallDrafts.m_cid = m_groupId;
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

    // 服务端以 msgid 升序（时间正序、最旧在前）返回 —— 见 MessageManager.cpp 翻历史
    // 模式把 DESC 查询结果反转为升序（out.assign(rows.rbegin(), rows.rend())），
    // buildSyncMessagesResponse 再按该升序数组原样写 JSON。实测亦然：某群 22 条历史
    // 的 first.msgid=14、last.msgid=168。
    //
    // 故这里必须<u>正序</u>遍历，才得到"最旧在上、最新在下"。此处原先照"最新在前"
    // 倒序遍历，与服务端的升序叠加成两次反转，最新消息被追加到列表最顶端 —— 表现
    // 为重启后打开群聊，滚到底也找不到刚发的那条（它跑到最上面去了）。
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& m = result.m_messages[k];
        bool fromMe = (selfId != 0 && (UINT)m.m_from == selfId);
        PerfStopWatch itemWatch;

        // 已撤回的消息：服务端拉历史时照样下发（带 recalled: true），这里换成撤回提示行，与在线收到
        // 撤回推送时气泡就地改成的提示行相同；不拦下的话撤回的内容在重开窗口后又会显示出来
        if (m.m_recalled)
        {
            //提示里写撤回者：群主 / 管理员撤回别人的消息时，撤回者不是发送人
            const int64_t recaller = balloonchatwnd::RecallOperatorOf(m);
            const bool recalledByMe = (selfId != 0 && recaller == (int64_t)selfId);
            balloonchatwnd::ChatMessage tipRow =
                balloonchatwnd::MakeRecalledTipRow(m, recalledByMe, ResolveSender(recaller).m_name);
            //本人发出、本人撤回，本机库里有快照且尚未到期：提示行带「编辑」
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
            RenderIncomingGroupImage(list, fromMe, m);
            breakdown.Add(balloonchatwnd::kHistoryRenderImage, itemWatch.ElapsedMs());
            continue;
        }

        // 文件消息：画成文件气泡（本地有完整文件就是完成态，否则待下载），与实时推送同一构造
        if (m.m_msgtype == kMsgTypeFile)
        {
            RenderIncomingGroupFile(list, fromMe, m);
            breakdown.Add(balloonchatwnd::kHistoryRenderFile, itemWatch.ElapsedMs());
            continue;
        }

        // 语音消息：与实时推送同一构造；本机没有这段语音就在后台下载。耗时计入「文字及其它」一类
        if (m.m_msgtype == kMsgTypeVoice)
        {
            RenderIncomingGroupVoice(list, fromMe, m, voicePlayed);
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 服务端生成的系统提示：居中提示行，与实时推送同一构造
        if (m.m_msgtype == kMsgTypeSystem)
        {
            list->AppendMessage(balloonchatwnd::MakeSystemTipRow(m));
            breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
            continue;
        }

        // 文本取正文；非文本用占位文字渲染成文本气泡，避免历史空洞。
        CString text;
        if (!m.m_text.empty())
        {
            text = Utf8ToCString(m.m_text);
        }
        else
        {
            text = Utf8ToCString(net::MessageProtocolClient::digestForMsgType(m.m_msgtype, m.m_text));
        }

        if (m.m_msgtype == kMsgTypeMerged)
        {
            // 合并转发的聊天记录：画成卡片，构造与实时推送共用 MakeGroupMergedMessage
            list->AppendMessage(MakeGroupMergedMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeFileShare)
        {
            // 文件共享卡片：画成卡片，构造与实时推送共用 MakeGroupShareMessage
            list->AppendMessage(MakeGroupShareMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeScreenShare)
        {
            // 屏幕共享卡片：画成卡片，构造与实时推送共用 MakeGroupScreenShareMessage
            list->AppendMessage(MakeGroupScreenShareMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeAnnouncement || m.m_msgtype == kMsgTypeMemo)
        {
            // 群公告卡片、群备忘卡片：画成卡片，构造与实时推送共用 MakeGroupInfoCardMessage。公告被删除后
            // 服务端已改写了卡片，这里拿到的就是已删除形态
            list->AppendMessage(MakeGroupInfoCardMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeCloudRecord)
        {
            // 云端录像卡片：画成卡片，构造与实时推送共用 MakeGroupCloudRecordMessage
            list->AppendMessage(MakeGroupCloudRecordMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeRtc)
        {
            // 群通话卡片：画成卡片，构造与实时推送共用 MakeGroupCallCardMessage
            list->AppendMessage(MakeGroupCallCardMessage(m, fromMe, list));
        }
        else if (m.m_msgtype == kMsgTypeRelay)
        {
            // 群接龙卡片：画成卡片，构造与实时推送共用 MakeGroupRelayMessage。历史里的 content
            // 已由服务端合并了名单与状态（见 docs/协议设计-v2.md §8.1）
            list->AppendMessage(MakeGroupRelayMessage(m, fromMe, list));
        }
        else if (selfId != 0 && (UINT)m.m_from == selfId)
        {
            // 自己发的：右侧本端气泡，构造与实时回显共用 MakeSelfTextMessage。
            list->AppendMessage(MakeSelfTextMessage(m, text, list));
        }
        else
        {
            // 对方发的：左侧气泡 + 真实发送人名/头像（取自群成员映射，查不到回退"用户<id>"）。
            // 成员与历史并行请求，历史先到时这里拿到的是备用信息，成员到达后再就地刷新。
            const balloonchatwnd::GroupSenderRender sender = ResolveSenderWithBotMark(m.m_from, m.m_botId, m.m_botName);
            balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(false, text);
            msg.font                  = m.m_font;
            msg.isMarkdown            = (m.m_textFormat == net::text_format_markdown);
            balloonchatwnd::ApplyCodeFields(msg, m.m_textFormat, m.m_codeLang, m.m_codeNotes, m.m_from);
            msg.msgid                 = m.m_msgid;
            msg.timestampMs           = m.m_timestamp;
            msg.senderName            = sender.m_name;
            msg.senderUserId          = m.m_from;
            msg.senderAvatarText      = sender.m_glyph;
            msg.senderAvatarColor     = sender.m_color;
            msg.senderAvatarImagePath = sender.m_imagePath;
            msg.senderIsBot           = sender.m_isBot;
            //@提醒还原，理由同上面自己发的那一支。isMe 按<u>当前</u>登录用户重新判定，
            //所以换个账号登录看同一条历史，高亮配色会随之变化，这是预期行为。
            msg.ats = balloonchatwnd::BuildRenderSpansForMessage(
                m.m_text, m.m_ats, m.m_atAll, (int64_t)selfId);
            balloonchatwnd::FillQuoteForRender(m.m_quote, msg,
                ResolveQuoteThumbPath((int64_t)selfId, m.m_quote, list));
            list->AppendMessage(msg);
        }
        breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
    }
    //本页有带「编辑」的撤回提示行：定好到期定时器
    if (anyRecallEdit)
    {
        RefreshRecallEditTimer();
    }
    stageWatch.Restart();
    // 本批最早那条的 msgid，作为下一页的 before 锚点。服务端按时间正序返回，
    // 故取第一条；本批为空时给 0，表示锚点不变（见 OnOlderPageLoaded）。
    const long long oldestInBatch = result.m_messages.empty()
                                  ? 0
                                  : (long long)result.m_messages[0].m_msgid;

    if (isOlderPage)
    {
        list->EndPrependBatch();
        balloonchatwnd::OnOlderPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
        LOG_INFO("group chat older page rendered: group=%lld, count=%u, oldest msgid=%lld, "
                 "hasmore=%d, render=%.1fms.",
                 (long long)m_groupId, (uint32_t)result.m_messages.size(),
                 (long long)oldestInBatch, result.m_hasmore ? 1 : 0,
                 renderStopWatch.ElapsedMs());
        return;   // 翻页不滚到底、也不参与"定位"补办
    }

    balloonchatwnd::OnFirstPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
    list->ScrollToBottom();
    // 历史已渲染：若有从历史窗"定位"过来、当时消息还没加载的待办，现在补定位，成功则清。
    if (m_pendingLocateMsgid != 0 && list->ScrollToMsgid(m_pendingLocateMsgid))
    {
        m_pendingLocateMsgid = 0;
    }
    // 首屏渲染前发出的 Markdown 文档的卡片也被清掉了：它们都已确认入库时重新拉一次最新一页
    m_mdEarlySends.OnFirstPageRendered();
    ReloadIfMarkdownEarlySendsDone();
    // 一并记下首尾 msgid：服务端约定按 msgid 升序（最旧在前）返回，本函数据此正序
    // 渲染。这两个值就是该约定的现场凭据 —— 正常应有 first < last；哪天两边的顺序
    // 约定又对不上（历史上出现过：这里曾按"最新在前"倒序遍历，与服务端的升序叠成
    // 两次反转，最新消息被排到列表最顶端，表现为重开窗口后滚到底也找不到刚发的那
    // 条），看这一行就能立刻发现，不必再埋探针。
    long long firstMsgid = 0;
    long long lastMsgid  = 0;
    if (!result.m_messages.empty())
    {
        firstMsgid = (long long)result.m_messages.front().m_msgid;
        lastMsgid  = (long long)result.m_messages.back().m_msgid;
    }
    //arrived 为应答到达时刻、render 为本次渲染耗时，都是毫秒；arrived 从打开窗口起算，
    //它减去成员填充日志里的 sinceOpen 大致就是等历史应答的时间。
    const double renderMs = renderStopWatch.ElapsedMs();
    breakdown.m_finishMs = stageWatch.ElapsedMs();
    BUI_TRACE("GROUPWND-HISTORY-BREAKDOWN group=%lld prepare=%.3fms text=%u/%.3fms image=%u/%.3fms "
              "file=%u/%.3fms finish=%.3fms",
              (long long)m_groupId, breakdown.m_prepareMs,
              breakdown.m_count[balloonchatwnd::kHistoryRenderText], breakdown.m_ms[balloonchatwnd::kHistoryRenderText],
              breakdown.m_count[balloonchatwnd::kHistoryRenderImage], breakdown.m_ms[balloonchatwnd::kHistoryRenderImage],
              breakdown.m_count[balloonchatwnd::kHistoryRenderFile], breakdown.m_ms[balloonchatwnd::kHistoryRenderFile],
              breakdown.m_finishMs);
    BUI_TRACE("GROUPWND-HISTORY-RENDERED group=%lld render=%.3fms sinceOpen=%.3fms",
              (long long)m_groupId, renderMs, m_openStopWatch.ElapsedMs());
    LOG_INFO("group chat first page rendered: group=%lld, count=%u, first.msgid=%lld, "
             "last.msgid=%lld, arrived=%.1fms, render=%.1fms.",
             (long long)m_groupId, (uint32_t)result.m_messages.size(),
             firstMsgid, lastMsgid, arrivedMs, renderMs);
}

void CBalloonGroupChatWindow::SwitchSideTab(UINT tabId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 五个 Tab 中被点的设为选中，其余取消选中。
    const int kSideTabCount = 5;
    const UINT kTabIds[kSideTabCount] =
    {
        balloonchatwnd::kGIdTabMembers,
        balloonchatwnd::kGIdTabFiles,
        balloonchatwnd::kGIdTabNotice,
        balloonchatwnd::kGIdTabMemo,
        balloonchatwnd::kGIdTabQa,
    };
    for (int i = 0; i < kSideTabCount; ++i)
    {
        balloonchatwnd::GroupSideTab* tab =
            dynamic_cast<balloonchatwnd::GroupSideTab*>(
                content->FindCtrlById(kTabIds[i]));
        if (tab != NULL)
        {
            tab->SetActive(kTabIds[i] == tabId);
        }
    }

    // 切换边栏下部的内容容器：成员 Tab → 成员面板；群文件 Tab → 群文件面板；
    // 公告 Tab → 公告面板。三者互斥地占同一个槽位。
    balloonwjui::DuiControl* membersPanel =
        content->FindCtrlById(balloonchatwnd::kGIdMembersPanel);
    balloonwjui::DuiControl* filesPanel =
        content->FindCtrlById(balloonchatwnd::kGIdFilesPanel);
    balloonwjui::DuiControl* noticePanel =
        content->FindCtrlById(balloonchatwnd::kGIdNoticePanel);
    balloonwjui::DuiControl* memoPanel =
        content->FindCtrlById(balloonchatwnd::kGIdMemoPanel);
    balloonwjui::DuiControl* qaPanel =
        content->FindCtrlById(balloonchatwnd::kGIdQaPanel);
    const bool showMembers = (tabId == balloonchatwnd::kGIdTabMembers);
    const bool showFiles   = (tabId == balloonchatwnd::kGIdTabFiles);
    const bool showNotice  = (tabId == balloonchatwnd::kGIdTabNotice);
    const bool showMemo    = (tabId == balloonchatwnd::kGIdTabMemo);
    const bool showQa      = (tabId == balloonchatwnd::kGIdTabQa);

    // 三个容器占边栏下部的同一槽位。切换前先抓住当前可见容器的矩形作槽位——
    // 成员容器一开始就可见、已布局过，矩形可靠；据此给切换后新显示的容器
    // 直接 ForceLayout，绕开「切可见性后子控件停 {0,0,0,0}」的 balloonui 布局 gotcha
    // （见记忆 reference-balloonui-layout-gotchas：须对容器逐个 ForceLayout）。
    RECT slot = { 0, 0, 0, 0 };
    if (membersPanel != NULL && membersPanel->IsVisible())
    {
        slot = membersPanel->GetRect();
    }
    else if (filesPanel != NULL && filesPanel->IsVisible())
    {
        slot = filesPanel->GetRect();
    }
    else if (noticePanel != NULL && noticePanel->IsVisible())
    {
        slot = noticePanel->GetRect();
    }
    else if (memoPanel != NULL && memoPanel->IsVisible())
    {
        slot = memoPanel->GetRect();
    }
    else if (qaPanel != NULL && qaPanel->IsVisible())
    {
        slot = qaPanel->GetRect();
    }

    if (membersPanel != NULL)
    {
        membersPanel->SetVisible(showMembers);
    }
    if (filesPanel != NULL)
    {
        filesPanel->SetVisible(showFiles);
    }
    if (noticePanel != NULL)
    {
        noticePanel->SetVisible(showNotice);
    }
    if (memoPanel != NULL)
    {
        memoPanel->SetVisible(showMemo);
    }
    if (qaPanel != NULL)
    {
        qaPanel->SetVisible(showQa);
    }

    // 用槽位矩形强制重排新显示的容器（其子控件随之就位）。槽位无效时退回整树重排兜底。
    const bool slotValid = (slot.right > slot.left && slot.bottom > slot.top);
    if (slotValid && showMembers && membersPanel != NULL)
    {
        membersPanel->ForceLayout(slot);
    }
    else if (slotValid && showFiles && filesPanel != NULL)
    {
        filesPanel->ForceLayout(slot);
    }
    else if (slotValid && showNotice && noticePanel != NULL)
    {
        noticePanel->ForceLayout(slot);
    }
    else if (slotValid && showMemo && memoPanel != NULL)
    {
        memoPanel->ForceLayout(slot);
    }
    else if (slotValid && showQa && qaPanel != NULL)
    {
        qaPanel->ForceLayout(slot);
    }
    else
    {
        content->ForceLayout(content->GetRect());
    }

    // 切到群文件 Tab：渲染面板。真实群且尚未拉取时先触发拉全量（异步，应答回来由
    // OnGroupFilesArrived 再渲染——增量3c 接入）；mock 调试入口已在 InitUi 预填库，
    // 此处直接渲染（RenderFilePanel 内部会再按面板矩形重排一次列表）。
    if (showFiles)
    {
        // 兜底触发一次拉取：正常情况下开窗时已经拉过（见 CreateAndShow），此处多半直接
        // 返回；只有开窗那次因故没发出去时才在这里补上。先渲染一次（库为空时显示空列表
        // + "共 0"作加载态），数据到位后 OnGroupFilesArrived 会重渲染。
        StartLoadGroupFiles();
        RenderFilePanel();
    }

    // 切到公告 Tab：首次进来拉一次公告（懒加载，与群文件同一套做法），之后靠
    // 1326 推送更新。先渲染一次（此刻列表为空，显示"正在加载"），数据到位后
    // OnAnnouncementsArrived 会重渲染。
    if (showNotice)
    {
        if (m_groupId != 0 && !m_noticeLoaded && !m_noticeLoading)
        {
            //首次进来看最新的那条（第 0 条），同时拿回总条数
            RequestNoticeAt(0);
        }
        RenderNoticePanel();
    }

    // 切到备忘 Tab：新到的群备忘计为已看过，角标清零；数据就在备忘中枢的备忘簿里，直接渲染
    if (showMemo)
    {
        if (m_groupId != 0)
        {
            balloonmemo::CMemoCenter::Instance().MarkGroupSeen(m_groupId);
        }
        RenderMemoPanel();
    }

    // 切到问答 Tab：首次进来拉一次问答（懒加载），之后靠 1339 推送重拉。先画一次（「正在加载」），
    // 数据到位后 OnGroupQaArrived 会重画
    if (showQa)
    {
        if (!m_qaRequested)
        {
            RequestGroupQa();
        }
        RenderQaPanel();
    }
    RefreshMemoTabBadge();

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::RequestNoticeAt(int index)
{
    if (m_groupId == 0 || m_noticeLoading)
    {
        return;
    }
    if (index < 0)
    {
        index = 0;
    }

    m_noticeLoading = true;
    //一次只要一条：offset 走当前下标、count 取 1。公告有几千条也不受服务端
    //单页上限影响，本地也只留手上这一条。
    //记下 offset 供应答认领 —— 群管理窗也在拉公告，两边页大小不同，不比对
    //就会把对方那份收下。
    m_noticeReqOffset = index;
    CIUSocket::GetInstance().SendGetGroupAnnouncements(
        m_groupId, index, kNoticeFetchPageSize);
}

void CBalloonGroupChatWindow::OnAnnouncementsArrived(const net::GroupAnnouncementsResult& result)
{
    //先认领：群管理窗也在拉同一个群的公告，两边页大小不同，收下对方那份会让
    //边栏显示成别人要的内容（见 m_noticeReqOffset 的说明）。
    if (!result.MatchesRequest(m_noticeReqOffset, kNoticeFetchPageSize))
    {
        return;
    }

    m_noticeLoading = false;
    //失败也要落下"已加载" —— 否则面板会一直停在"正在加载"那句话上，用户以为卡死了
    m_noticeLoaded = true;

    if (result.m_code != 0)
    {
        LOG_INFO("群聊窗拉取群公告未通过: groupid=%lld, code=%d。",
                 (long long)m_groupId, result.m_code);
        RenderNoticePanel();
        return;
    }

    m_noticeTotal = result.m_total;

    //本次要的那一条回来了就换上；空的说明这个下标已经越界（比如刚才看的是最后
    //一条而它被删了），把下标夹回最后一条再要一次。
    if (!result.m_announcements.empty())
    {
        m_notice = result.m_announcements[0];
    }
    else if (m_noticeTotal > 0 && m_noticeIndex >= m_noticeTotal)
    {
        m_noticeIndex = m_noticeTotal - 1;
        RequestNoticeAt(m_noticeIndex);
        return;
    }
    else
    {
        //本群一条公告都没有了
        m_notice = net::GroupAnnouncementItem();
        m_noticeIndex = 0;
    }

    RenderNoticePanel();

    LOG_INFO("群聊窗群公告到达: groupid=%lld, index=%d, total=%d。",
             (long long)m_groupId, m_noticeIndex, m_noticeTotal);
}

void CBalloonGroupChatWindow::OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify)
{
    //公告被删除：消息列表里那张公告卡片换成已删除形态（docs/群公告接入方案.md §13.3）。
    //这一步与公告面板有没有打开过无关，放在下面的提前返回之前
    if (m_groupId != 0 && notify.m_changetype == group_announcement_change_delete && notify.m_cardMsgId > 0)
    {
        ApplyAnnouncementCardDeleted(notify.m_cardMsgId, notify.m_cardContent);
    }

    //公告面板还没打开过就什么都不做 —— 用户真去点那个 Tab 时自然会拉到最新的，
    //现在拉一次只是白费一个来回。
    if (m_groupId == 0 || !m_noticeLoaded)
    {
        return;
    }

    //新公告发布时跳到第一条（它排最前，用户多半就想看它）；删除则留在原位，
    //由应答里的 total 与空结果把越界的下标夹回来。
    if (notify.m_changetype == group_announcement_change_publish)
    {
        m_noticeIndex = 0;
    }

    //推送到达时可能正好有一次翻页在途，此时 m_noticeLoading 为真会让这次重拉被
    //丢掉。清掉它 —— 在途那次拿回的是旧数据，本来就该被这次覆盖。
    m_noticeLoading = false;
    RequestNoticeAt(m_noticeIndex);

    LOG_INFO("群聊窗收到群公告变更推送: groupid=%lld, changetype=%d, 重拉第 %d 条。",
             (long long)notify.m_groupid, notify.m_changetype, m_noticeIndex);
}

void CBalloonGroupChatWindow::StepNotice(int delta)
{
    if (m_noticeTotal <= 0)
    {
        return;
    }

    int next = m_noticeIndex + delta;
    //越界就地夹住而不循环：只有两三条时循环翻页会让人分不清自己看到第几条了
    if (next < 0)
    {
        next = 0;
    }
    if (next >= m_noticeTotal)
    {
        next = m_noticeTotal - 1;
    }
    if (next == m_noticeIndex)
    {
        return;
    }

    m_noticeIndex = next;
    //翻到哪条要哪条。请求发出到应答回来之间界面先不动 —— 公告是低频浏览，
    //一个来回的延迟感知不明显，不值得为它加一层本地缓存。
    RequestNoticeAt(m_noticeIndex);
}

void CBalloonGroupChatWindow::RenderNoticePanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiControl* card =
        content->FindCtrlById(balloonchatwnd::kGIdNoticeCard);
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeEmpty));
    balloonwjui::DuiLabel* count = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeCount));
    balloonwjui::DuiLabel* index = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeIndex));

    const bool hasNotice = (m_noticeTotal > 0 && m_notice.m_id > 0);

    //有公告时显示公告卡、藏起空状态提示；没有时反过来
    if (card != NULL)
    {
        card->SetVisible(hasNotice);
    }
    if (empty != NULL)
    {
        empty->SetVisible(!hasNotice);
        //还没拉回来与"拉过了但确实没有公告"要分开说，否则用户会以为公告丢了
        empty->SetText(m_noticeLoaded ? TR(L"本群还没有群公告") : TR(L"正在加载公告…"));
    }

    //翻页按钮：只有一条时两个都不可点
    balloonwjui::DuiButton* prev = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticePrev));
    balloonwjui::DuiButton* next = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeNext));
    if (prev != NULL)
    {
        prev->SetEnabled(m_noticeIndex > 0);
    }
    if (next != NULL)
    {
        next->SetEnabled(m_noticeIndex + 1 < m_noticeTotal);
    }

    if (count != NULL)
    {
        CString text;
        text.Format(TR(L"群公告 · %d 条"), m_noticeTotal);
        count->SetText(text);
    }
    if (index != NULL)
    {
        CString text;
        //一条都没有时显示 0/0 比 1/0 讲得通
        text.Format(_T("%d/%d"), hasNotice ? (m_noticeIndex + 1) : 0, m_noticeTotal);
        index->SetText(text);
    }

    if (hasNotice)
    {
        const net::GroupAnnouncementItem& a = m_notice;

        balloonwjui::DuiLabel* title = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeTitle));
        if (title != NULL)
        {
            title->SetText(Utf8ToCString(a.m_title));
        }

        balloonwjui::DuiLabel* body = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeBody));
        if (body != NULL)
        {
            body->SetText(Utf8ToCString(a.m_content));
        }

        balloonwjui::DuiLabel* meta = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeMeta));
        if (meta != NULL)
        {
            //发布人昵称由服务端下发（可能已退群，本地查不到）；缺了就只显示时间
            CString who = Utf8ToCString(a.m_publishername);
            CString when = balloongroupmanage::FormatAnnouncementTime(
                a.m_publishtime, NowMillisecondsForNotice());
            CString text;
            if (who.IsEmpty())
            {
                text = when;
            }
            else
            {
                text.Format(_T("%s · %s"), (LPCTSTR)who, (LPCTSTR)when);
            }
            meta->SetText(text);
        }
    }

    //公告面板此刻可能正显示着，改完文本要按当前矩形重排一次（正文是自动换行的
    //多行 label，内容变了行数也会变）。
    balloonwjui::DuiControl* panel =
        content->FindCtrlById(balloonchatwnd::kGIdNoticePanel);
    if (panel != NULL && panel->IsVisible())
    {
        const RECT rc = panel->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            panel->ForceLayout(rc);
        }
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::SetGroupIntro(LPCTSTR intro)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupChatHeaderInfo* header =
        dynamic_cast<balloonchatwnd::GroupChatHeaderInfo*>(
            content->FindCtrlById(balloonchatwnd::kGIdHeaderInfo));
    if (header != NULL)
    {
        header->SetGroupIntro(intro);
    }
}

void CBalloonGroupChatWindow::ActivateNoticeTab()
{
    //复用与用户点 Tab 完全相同的那条路径：懒加载、面板显隐、ForceLayout 都在里面，
    //另写一份迟早会跟它走岔。
    SwitchSideTab(balloonchatwnd::kGIdTabNotice);
}

void CBalloonGroupChatWindow::RenderFilePanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return;
    }

    // 取当前分类 / 搜索 / 页的本页条目与分页信息。
    std::vector<net::GroupFileEntry> items;
    int total = 0;
    int pageCount = 0;
    int curPage = 0;
    m_fileLibrary.Query(m_fileCategory, m_fileSearch, m_filePage, kGroupFilePageSize,
                        items, total, pageCount, curPage);
    m_filePage = curPage;   // 钳定后回写（翻到越界页时跟随收回）

    // 清空旧文件行，按本页条目重建。
    while (!list->Children().empty())
    {
        list->RemoveChild(list->Children()[0].get());
    }

    // 先把「正在上传」的若干笔钉在最顶部（不受分类 / 搜索筛选影响，因为它们还没进库）。
    // m_uploadTracker.Items() 已按最近发起在最前排序，依次 AddChild 即新发起的在最上。
    const std::vector<balloonchatwnd::GroupUploadEntry>& ups = m_uploadTracker.Items();
    for (size_t i = 0; i < ups.size(); ++i)
    {
        const balloonchatwnd::GroupUploadEntry& u = ups[i];
        std::unique_ptr<balloonchatwnd::GroupFileItem> uitem(
            new balloonchatwnd::GroupFileItem());
        // 说明行留空：上传态下 OnPaint 会改显「上传中 NN%」，文件名 / 扩展名用于显示与角标。
        uitem->SetFile(CString(u.m_name.c_str()), CString(), CString(u.m_ext.c_str()));
        uitem->SetUploading(u.m_uploadId, u.m_percent);
        list->AddChild(std::unique_ptr<balloonwjui::DuiControl>(uitem.release()));
    }

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::string selfIdStr = (selfId != 0) ? std::to_string(selfId) : std::string();
    long long nowMs = (long long)time(NULL) * 1000LL;

    for (size_t i = 0; i < items.size(); ++i)
    {
        const net::GroupFileEntry& e = items[i];
        // 上传者：本人显示"我"，否则显示服务端给的昵称。
        CString uploader;
        if (!selfIdStr.empty() && e.m_uploaderid == selfIdStr)
        {
            uploader = TR(L"我");
        }
        else
        {
            uploader = Utf8ToCString(e.m_uploadername);
        }
        CString timeStr = balloonchatwnd::FormatRelativeDayTime(e.m_uploadtime, nowMs).c_str();
        CString sizeStr = balloonchatwnd::FormatFileSize(e.m_size).c_str();
        // 拼「上传者 · 时间 · 大小」，缺项跳过（避免出现多余的" · "）。
        CString meta = uploader;
        if (!timeStr.IsEmpty())
        {
            if (!meta.IsEmpty())
            {
                meta += _T(" · ");
            }
            meta += timeStr;
        }
        if (!sizeStr.IsEmpty())
        {
            if (!meta.IsEmpty())
            {
                meta += _T(" · ");
            }
            meta += sizeStr;
        }

        std::unique_ptr<balloonchatwnd::GroupFileItem> item(
            new balloonchatwnd::GroupFileItem());
        item->SetFile(Utf8ToCString(e.m_name), meta, Utf8ToCString(e.m_ext));
        item->SetIds(Utf8ToCString(e.m_entryid), Utf8ToCString(e.m_fileid));
        // 删除门控：群主 / 管理员可删群内任意，上传者本人可删自己上传的（与服务端同规则）。
        // 可删行在鼠标悬停时于下载箭头左侧显示垃圾桶。
        bool isOwnUpload = (!selfIdStr.empty() && e.m_uploaderid == selfIdStr);
        item->SetDeletable(
            balloonchatwnd::GroupFileLibrary::CanDeleteGroupFile(m_myRole, isOwnUpload));
        // 主面板全局搜索定位到的那一行以淡黄底标出（见 ShowGroupFileEntry）。
        if (!m_highlightEntryId.empty() && e.m_entryid == m_highlightEntryId)
        {
            item->SetHighlighted(true);
        }
        // 若该条目正在下载（翻页 / 推送重渲染前已在途）：把"下载中"态补回这一新建行，
        // 保证进度条与百分比跨重渲染不丢。
        std::map<std::string, int>::const_iterator dlIt =
            m_downloadingFiles.find(e.m_entryid);
        if (dlIt != m_downloadingFiles.end())
        {
            item->SetDownloadProgress(dlIt->second);
        }
        list->AddChild(std::unique_ptr<balloonwjui::DuiControl>(item.release()));
    }

    // 同步滚动视口内容高度，并回到顶部（翻页 / 筛选后从头看）。
    balloonwjui::DuiScrollView* sv =
        dynamic_cast<balloonwjui::DuiScrollView*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileScroll));
    if (sv != NULL)
    {
        sv->SetContentHeight(list->ContentHeight());
        sv->SetScrollPos(0);
        // 强制滚动视图重排：DoLayout 据新内容高度重算内容名义矩形，再给内容控件
        // SetRect。否则 SetContentHeight 只更新高度、不重算名义矩形，内容矩形不变、
        // DuiScrollView 给内容用的 SetRect 提前返回，新增文件行就停在 {0,0,0,0}。
        RECT svRect = sv->GetRect();
        if (svRect.right > svRect.left && svRect.bottom > svRect.top)
        {
            sv->ForceLayout(svRect);
        }
    }
    // 再直接强制文件列表容器重排兜底：即便本次行数与上次相同（内容名义矩形未变、
    // 上面那次 SetRect 仍会提前返回），ForceLayout 直接调 Layout 不经 SetRect 的
    // 提前返回，保证本批新建的文件行从 {0,0,0,0} 就位。
    {
        RECT listRect = list->GetRect();
        if (listRect.right > listRect.left)
        {
            list->ForceLayout(listRect);
        }
    }

    // 刷新分页信息文字："共 N · 第 x/y 页"。
    balloonwjui::DuiLabel* pageInfo =
        dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdFilePageInfo));
    if (pageInfo != NULL)
    {
        CString text;
        text.Format(TR(L"共 %d · 第 %d/%d 页"), total, curPage + 1, pageCount);
        pageInfo->SetText(text);
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::RefreshSideTabCounts()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 成员数：m_memberInfo 是「userid → 显示信息」映射，规模即群成员总数（真实群由
    // OnGroupMembersArrived 整体重填，调试入口用 mock 填）。还没填过说明成员尚未到达，
    // 按「数量未知」处理，Tab 只显示"成员"。
    const int memberCount = m_memberInfo.empty()
                            ? balloonchatwnd::kSideTabCountUnknown
                            : (int)m_memberInfo.size();
    balloonchatwnd::GroupSideTab* memberTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(
            content->FindCtrlById(balloonchatwnd::kGIdTabMembers));
    if (memberTab != NULL)
    {
        memberTab->SetText(
            balloonchatwnd::MakeSideTabText(TRC(L"群聊窗右侧页签", L"成员"), memberCount).c_str());
    }

    // 群文件数：取库里的全量条数（不受分类 chip / 搜索框影响）。库还没拉到手时按
    // 「数量未知」处理 —— 此时写 0 会和「群里确实一个文件都没有」混淆。
    // 正在上传、尚未登记进库的那几笔不计入：等服务端登记成功推回来自然会 +1。
    const int fileCount = m_fileLoaded
                          ? m_fileLibrary.TotalAll()
                          : balloonchatwnd::kSideTabCountUnknown;
    balloonchatwnd::GroupSideTab* fileTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(
            content->FindCtrlById(balloonchatwnd::kGIdTabFiles));
    if (fileTab != NULL)
    {
        fileTab->SetText(
            balloonchatwnd::MakeSideTabText(TRC(L"群聊窗右侧页签", L"群文件"), fileCount).c_str());
    }

    //公告、备忘两个 Tab 不带数量。四个 Tab 的文字都用带上下文的 TRC：右栏只有 240 像素宽，
    //译文可以取比别处更短的写法（多语言第 4 期）
    balloonchatwnd::GroupSideTab* noticeTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(content->FindCtrlById(balloonchatwnd::kGIdTabNotice));
    if (noticeTab != NULL)
    {
        noticeTab->SetText(TRC(L"群聊窗右侧页签", L"公告"));
    }
    balloonchatwnd::GroupSideTab* memoTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(content->FindCtrlById(balloonchatwnd::kGIdTabMemo));
    if (memoTab != NULL)
    {
        memoTab->SetText(TRC(L"群聊窗右侧页签", L"备忘"));
    }
    balloonchatwnd::GroupSideTab* qaTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(content->FindCtrlById(balloonchatwnd::kGIdTabQa));
    if (qaTab != NULL)
    {
        qaTab->SetText(TRC(L"群聊窗右侧页签", L"问答"));
    }
    //按各 Tab 文字的实际宽度重新分配右栏宽度（2026-10-07 起有五个 Tab，简体中文也按宽度分配）
    balloonchatwnd::ApplyGroupSideTabWeights(content);

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::StartLoadGroupFiles()
{
    // 调试入口（无群绑定）的库已在 InitUi 用 mock 预填并置 m_fileLoaded，无须拉取。
    if (m_groupId == 0)
    {
        return;
    }
    // 已拉过或正在拉：不重复发请求（本函数在开窗时与首次切到群文件 Tab 时各调一次）。
    if (m_fileLoaded || m_fileLoading)
    {
        return;
    }

    m_fileLoadBuffer.clear();
    m_fileLoading = true;
    CIUSocket::GetInstance().RequestGroupFiles(
        m_groupId, group_file_type_all, 0, kGroupFileFetchPageSize);
    LOG_INFO("群文件拉全量已发起: group=%lld。", (long long)m_groupId);
}

void CBalloonGroupChatWindow::ShowGroupFileEntry(const std::string& entryId)
{
    LOG_INFO("group chat window: locate group file, group=%lld, entry=%s, loaded=%d.",
             (long long)m_groupId, entryId.c_str(), m_fileLoaded ? 1 : 0);

    // 先切到「群文件」Tab：库还没拉过时它会顺带发起拉取（见 SwitchSideTab）。
    SwitchSideTab(balloonchatwnd::kGIdTabFiles);
    if (entryId.empty())
    {
        return;
    }

    // 库拉全后才知道这个文件在第几页：没拉全就先记下，OnGroupFilesArrived 拉全后再定位。
    m_pendingLocateEntryId = entryId;
    if (m_fileLoaded)
    {
        ApplyPendingFileLocate();
    }
}

void CBalloonGroupChatWindow::ApplyPendingFileLocate()
{
    if (m_pendingLocateEntryId.empty())
    {
        return;
    }
    const std::string entryId = m_pendingLocateEntryId;
    m_pendingLocateEntryId.clear();

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 1) 分类回到「全部」、清空文件名搜索，保证这个文件不会被当前的筛选条件挡住。
    //    搜索框用不发通知的方式清空：文字变化通知会重置页码并重渲染，这里随后自己渲染。
    balloonwjui::DuiEdit* search = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(balloonchatwnd::kGIdFileSearch));
    if (search != NULL && !search->GetText().IsEmpty())
    {
        search->SetTextNoNotify(_T(""));
    }
    m_fileSearch.clear();
    ApplyFileChipSelection(balloonchatwnd::kGroupFileCatAll);

    // 2) 翻到它所在的页；库里没有（已被删除）就停在第 1 页，不标高亮。
    const int page = m_fileLibrary.PageOfEntry(balloonchatwnd::kGroupFileCatAll, std::string(),
                                               entryId, kGroupFilePageSize);
    if (page < 0)
    {
        LOG_WARNING("group chat window: group file %s not found in library, group=%lld.",
                    entryId.c_str(), (long long)m_groupId);
        m_filePage = 0;
        RenderFilePanel();
        return;
    }
    m_filePage         = page;
    m_highlightEntryId = entryId;
    RenderFilePanel();

    // 3) RenderFilePanel 会把滚动位置归零；这一行在视口下方看不见时，滚到刚好露出它。
    balloonchatwnd::GroupFileList* list = dynamic_cast<balloonchatwnd::GroupFileList*>(
        content->FindCtrlById(balloonchatwnd::kGIdFileList));
    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        content->FindCtrlById(balloonchatwnd::kGIdFileScroll));
    if (list == NULL || sv == NULL)
    {
        return;
    }
    for (size_t i = 0; i < list->Children().size(); ++i)
    {
        balloonchatwnd::GroupFileItem* item =
            dynamic_cast<balloonchatwnd::GroupFileItem*>(list->Children()[i].get());
        if (item == NULL || CStringToUtf8(item->EntryId()) != entryId)
        {
            continue;
        }
        const int rowBottom  = item->GetRect().bottom - list->GetRect().top;
        const int viewHeight = sv->GetRect().bottom - sv->GetRect().top;
        if (viewHeight > 0 && rowBottom > viewHeight)
        {
            sv->SetScrollPos(rowBottom - viewHeight);
        }
        break;
    }
}

void CBalloonGroupChatWindow::ApplyFileChipSelection(int category)
{
    m_fileCategory = category;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    // chip 控件 id ↔ 分类的对应表。
    struct ChipMap
    {
        UINT id;
        int  cat;
    };
    const ChipMap kChips[5] =
    {
        { balloonchatwnd::kGIdFileChipAll,    balloonchatwnd::kGroupFileCatAll    },
        { balloonchatwnd::kGIdFileChipPdf,    balloonchatwnd::kGroupFileCatPdf    },
        { balloonchatwnd::kGIdFileChipDoc,    balloonchatwnd::kGroupFileCatDoc    },
        { balloonchatwnd::kGIdFileChipSheet,  balloonchatwnd::kGroupFileCatSheet  },
        { balloonchatwnd::kGIdFileChipDesign, balloonchatwnd::kGroupFileCatDesign },
    };
    for (int i = 0; i < 5; ++i)
    {
        balloonwjui::DuiButton* btn =
            dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(kChips[i].id));
        if (btn != NULL)
        {
            // 选中=品牌实心（Primary，蓝）；未选=白底灰边（Default）。
            btn->SetVariant(kChips[i].cat == category
                            ? balloonwjui::DuiButton::Variant::Primary
                            : balloonwjui::DuiButton::Variant::Default);
        }
    }
}

void CBalloonGroupChatWindow::OnGroupFilesArrived(const net::GetGroupFilesResult& result)
{
    // 失败应答：收尾本次拉取，用已累积的（可能为空）灌库并渲染，记日志。
    if (result.m_code != 0)
    {
        LOG_ERROR("群文件列表应答错误: group=%lld, code=%d, msg=%s。",
                  (long long)m_groupId, result.m_code, result.m_msg.c_str());
        m_fileLibrary.SetAll(m_fileLoadBuffer);
        m_fileLoadBuffer.clear();
        m_fileLoading = false;
        m_fileLoaded  = true;
        m_filePage    = 0;
        RenderFilePanel();
        RefreshSideTabCounts();
        ApplyPendingFileLocate();
        return;
    }

    // 累积本页条目。
    for (size_t i = 0; i < result.m_files.size(); ++i)
    {
        m_fileLoadBuffer.push_back(result.m_files[i]);
    }

    // 还有后续页：从下一偏移继续拉（Option B 全量缓存）。
    if (result.m_hasmore)
    {
        int nextOffset = result.m_offset + (int)result.m_files.size();
        CIUSocket::GetInstance().RequestGroupFiles(
            m_groupId, group_file_type_all, nextOffset, kGroupFileFetchPageSize);
        return;
    }

    // 拉全：整批灌进库、回到第 1 页、渲染。
    m_fileLibrary.SetAll(m_fileLoadBuffer);
    m_fileLoadBuffer.clear();
    m_fileLoading = false;
    m_fileLoaded  = true;
    m_filePage    = 0;
    LOG_INFO("群文件拉全完成: group=%lld, 共 %d 个文件。",
             (long long)m_groupId, m_fileLibrary.TotalAll());
    RenderFilePanel();
    // 文件数到手 → 边栏「群文件」Tab 换成真实数量（"群文件 8"）。
    RefreshSideTabCounts();
    // 开窗时就被要求定位某个文件（主面板全局搜索）：库拉全了，现在定位。
    ApplyPendingFileLocate();
}

void CBalloonGroupChatWindow::OnGroupFilesChanged(const net::GroupFilesChangedNotify& notify)
{
    // 库尚未拉取时不动：待首次拉全量时 SetAll 会带上服务端最新状态。
    if (!m_fileLoaded)
    {
        return;
    }
    if (notify.m_changetype == group_file_change_added)
    {
        m_fileLibrary.Upsert(notify.m_file);
    }
    else if (notify.m_changetype == group_file_change_removed)
    {
        m_fileLibrary.RemoveByEntryId(notify.m_entryid);
    }
    else
    {
        return;   // 未知变更类型，忽略
    }
    // 重渲染（当前页越界由 RenderFilePanel 内 Query 钳定；面板隐藏时渲染也无妨，
    // 下次切到群文件 Tab 会再渲染）。文件数变了，Tab 上的数量同步跟上 —— 边栏此刻
    // 停在成员 Tab 也要更新，否则切过去才看到新数。
    RenderFilePanel();
    RefreshSideTabCounts();
}

void CBalloonGroupChatWindow::OnGroupFileUploaded(const net::GroupFileUploadResult& result)
{
    // 宿主「发送到会话」登记过的上传：不论成败、取消，先取出登记（2026-10-05），成功时下面再发文件消息
    balloonchatwnd::GroupFileChatPost post;
    const bool postToChat = m_chatPoster.Take(result, post);

    // 取消与真失败的区分：用户取消时窗口已先把这一行从跟踪器移除（见 OnGroupFileUploadCancelMsg），
    // 故此处按 uploadId 查不到 → 判定为「已取消」，静默忽略（不弹失败框、不刷面板）。
    if (m_uploadTracker.Find(result.m_uploadid) == NULL)
    {
        LOG_INFO("群文件上传结果到达但该笔已不在跟踪器(多半已取消): group=%lld, uploadId=%d, ok=%d。",
                 (long long)m_groupId, result.m_uploadid, result.m_ok ? 1 : 0);
        return;
    }

    // 上传结束（成功 / 失败）：把这一笔从跟踪器移除，面板顶部那一行随之消失。
    m_uploadTracker.Remove(result.m_uploadid);
    RenderFilePanel();

    if (!result.m_ok)
    {
        LOG_ERROR("群文件上传失败: group=%lld, uploadId=%d, code=%d, msg=%s。",
                  (long long)m_groupId, result.m_uploadid, result.m_errcode, result.m_errmsg.c_str());
        // 给用户可见反馈（否则上传被拒后没动静、易困惑）。文件名包含敏感词有专门的说法，
        // 其余失败一般是网络 / 服务端原因，统一通用提示。
        if (IsWindow())
        {
            ::MessageBoxW(m_hWnd, balloonchatwnd::GroupFileUploadFailureText(result.m_errcode).c_str(),
                          TR(L"群文件上传"), MB_OK | MB_ICONWARNING);
        }
        return;
    }
    // 上传成功 → 发 cmd_add_group_file 登记进群文件库。服务端登记成功后向全群（含本人）
    // 推 1806，本端经 OnGroupFilesChanged Upsert 刷新面板，无需在此手动改库。
    CIUSocket::GetInstance().RequestAddGroupFile(m_groupId, result.m_fileid);
    LOG_INFO("群文件上传成功, 发起登记: group=%lld, uploadId=%d, fileid=%s, name=%s。",
             (long long)m_groupId, result.m_uploadid, result.m_fileid.c_str(), result.m_filename.c_str());
    if (postToChat)
        SendUploadedFileMessage(post);
}

void CBalloonGroupChatWindow::SendUploadedFileMessage(const balloonchatwnd::GroupFileChatPost& post)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || m_groupId == 0)
    {
        return;
    }
    //本人的文件气泡：文件已在本机，点开直接打开本地文件；记下 fileid 与 md5 供被拒绝后重发；msgid 与时间待回执回填
    const std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeGroupFileEcho(post, clientMsgId);
    FillSelfSenderAvatar(m_lpFMGClient, echo);
    list->AppendMessage(echo);
    SendGroupFileMessage(clientMsgId, post.m_fileid, post.m_filename, static_cast<long long>(post.m_size), post.m_md5);
}

void CBalloonGroupChatWindow::SendGroupFileMessage(const std::string& clientMsgId, const std::string& fileId,
                                                   const std::string& fileNameUtf8, long long sizeBytes,
                                                   const std::string& md5)
{
    CIUSocket::GetInstance().SendChatFileMessage(conversation_group, (int64_t)m_groupId, clientMsgId, fileId,
                                                 fileNameUtf8, sizeBytes, md5);
    LOG_INFO("group chat: file message sent, group=%lld, clientmsgid=%s, fileid=%s.", (long long)m_groupId,
             clientMsgId.c_str(), fileId.c_str());
    //登记待冒泡：回执成功后才把本会话冒泡到会话列表顶部、预览写「[文件]」
    ConvSendBumpData bump;
    bump.ctype   = conversation_group;
    bump.cid     = (long long)m_groupId;
    bump.msgType = kMsgTypeFile;
    bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeFile, std::string());
    m_sendBumps.Add(clientMsgId, bump);
}

void CBalloonGroupChatWindow::ResendGroupFileMessage(const balloonchatwnd::ChatFileActionData& d)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || m_groupId == 0)
    {
        return;
    }
    //气泡位置不变，清掉失败标记、回到已完成；不是本人发送失败的那条时不重发
    if (!list->RestartFileMessageSendByClientMsgId(d.clientMsgId))
    {
        LOG_INFO("group chat: file message resend skipped, not a failed own message, clientmsgid=%s.",
                 d.clientMsgId.c_str());
        return;
    }
    LOG_INFO("group chat: file message resend, group=%lld, clientmsgid=%s.", (long long)m_groupId, d.clientMsgId.c_str());
    SendGroupFileMessage(d.clientMsgId, d.fileId, CStringToUtf8(d.fileName), d.sizeBytes, d.md5);
}

void CBalloonGroupChatWindow::OnGroupFileDownloaded(const net::GroupFileDownloadResult& result)
{
    // 下载结束（成功 / 失败）：清掉该条目的"下载中"态（移出进度表 + 恢复可见行的下载箭头）。
    m_downloadingFiles.erase(result.m_entryid);
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::GroupFileItem* item =
            FindGroupFileItemByEntryId(content, CString(Utf8ToCString(result.m_entryid)));
        if (item != NULL)
        {
            item->ClearDownloadProgress();
        }
    }

    if (!result.m_ok || result.m_localpath.empty())
    {
        LOG_ERROR("群文件下载失败: group=%lld, entryid=%s, code=%d。",
                  (long long)m_groupId, result.m_entryid.c_str(), result.m_errcode);
        return;
    }
    // 已下载到用户「另存为」选定的位置，按其意愿仅保存、不自动打开。
    // 路径先转 UTF-8 再以 %s 记日志：narrow 日志器对 %ls + 含中文宽字符串会因
    // 默认 "C" locale 转多字节失败导致缓冲溢出断言（见本轮上传崩溃修复）。
    std::string pathUtf8 = CStringToUtf8(CString(result.m_localpath.c_str()));
    LOG_INFO("群文件下载完成并保存: group=%lld, entryid=%s, path=%s。",
             (long long)m_groupId, result.m_entryid.c_str(), pathUtf8.c_str());
}

void CBalloonGroupChatWindow::OnGroupFileUploadProgress(const net::GroupFileUploadProgress& prog)
{
    int pct = (prog.m_total > 0)
              ? static_cast<int>((prog.m_sent * 100) / prog.m_total) : 0;
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    // 已不在跟踪器（已取消 / 已完成）：忽略迟到的进度。
    if (m_uploadTracker.Find(prog.m_uploadid) == NULL)
    {
        return;
    }
    // 记进跟踪器（跨重渲染保持），并就地更新顶部那一行「上传中」的百分比。
    m_uploadTracker.SetProgress(prog.m_uploadid, pct);
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item = FindGroupFileUploadItem(content, prog.m_uploadid);
    if (item != NULL)
    {
        item->SetUploading(prog.m_uploadid, pct);
    }
}

void CBalloonGroupChatWindow::OnGroupFileDownloadProgress(const net::GroupFileDownloadProgress& prog)
{
    int pct = (prog.m_total > 0)
              ? static_cast<int>((prog.m_received * 100) / prog.m_total) : 0;
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    // 记进"下载中"表（跨换页 / 推送重渲染保持该行的下载态）。
    m_downloadingFiles[prog.m_entryid] = pct;
    // 若该条目当前可见：就地更新那一行的进度。
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item =
        FindGroupFileItemByEntryId(content, Utf8ToCString(prog.m_entryid));
    if (item != NULL)
    {
        item->SetDownloadProgress(pct);
    }
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDownloadMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                        LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    delete pEntryId;

    // 调试入口（mock）无服务器，下载不可行；记日志返回。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件下载：调试入口无群绑定，忽略 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 据 entryid 在库里取完整条目（拿 fileid / name / md5 / size 供下载）。先把要用的字段
    // 拷出来——下面的「另存为」对话框是模态、会泵消息，期间若收到 1806 推送会改动
    // m_fileLibrary（Upsert/Remove 可能令 vector 重分配），直接持 e 指针会失效。
    const net::GroupFileEntry* e = m_fileLibrary.FindByEntryId(entryid);
    if (e == NULL)
    {
        LOG_ERROR("群文件下载：库中找不到 entryid=%s。", entryid.c_str());
        return 0;
    }
    std::string dlEntryId = e->m_entryid;
    std::string dlFileId  = e->m_fileid;
    std::string dlName    = e->m_name;
    std::string dlMd5     = e->m_md5;
    int64_t     dlSize    = e->m_size;

    // 弹「另存为」对话框选保存位置，默认填原文件名。用户取消则不下载。
    wchar_t szFile[1024] = { 0 };
    CString defName = Utf8ToCString(dlName);
    wcsncpy_s(szFile, defName, _TRUNCATE);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"所有文件"), L"*.*");
    ofn.lpstrFilter = fileFilter.c_str();
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = 1024;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (!::GetSaveFileNameW(&ofn))
    {
        return 0;   // 用户取消
    }
    std::wstring destPath = szFile;

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    net::FileTransfer::GetInstance().EnqueueGroupFileDownload(
        m_groupId, dlEntryId, dlFileId, dlName, dlMd5, dlSize, selfId, destPath);

    // 即时进入「下载中 0%」态：记进进度表并刷新可见行，不必等第一个进度回投。
    m_downloadingFiles[dlEntryId] = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item =
        FindGroupFileItemByEntryId(content, Utf8ToCString(dlEntryId));
    if (item != NULL)
    {
        item->SetDownloadProgress(0);
    }

    // 路径先转 UTF-8 再以 %s 记日志（含中文路径用 %ls 会触发 narrow 日志器缓冲溢出断言）。
    std::string destUtf8 = CStringToUtf8(CString(destPath.c_str()));
    LOG_INFO("群文件下载已入队: group=%lld, entryid=%s, name=%s, dest=%s。",
             (long long)m_groupId, dlEntryId.c_str(), dlName.c_str(), destUtf8.c_str());
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnGroupFileUploadCancelMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                            LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 即被取消那笔的 uploadId（int 直接装在 LPARAM 里）。
    int uploadId = (int)lParam;

    // 先从跟踪器移除（顶部那一行随重渲染消失），再调 FileTransfer 真正取消传输。
    // 顺序要紧：先移除，使稍后工作线程回投的失败结果在 OnGroupFileUploaded 里查不到这笔、
    // 据此判定为「已取消」而静默忽略，不弹失败框。
    bool removed = m_uploadTracker.Remove(uploadId);
    net::FileTransfer::GetInstance().CancelGroupFileUpload(uploadId);
    if (removed)
    {
        RenderFilePanel();
    }
    LOG_INFO("群文件上传取消: group=%lld, uploadId=%d。", (long long)m_groupId, uploadId);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDownloadCancelMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    CString entryidCStr = *pEntryId;
    delete pEntryId;

    // 移出「下载中」表（跨重渲染不再恢复下载态），调 FileTransfer 真正取消，并就地把这一行
    // 恢复成下载箭头。下载失败本就静默，故无需像上传那样区分取消与失败。
    m_downloadingFiles.erase(entryid);
    net::FileTransfer::GetInstance().CancelGroupFileDownload(entryid);
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item = FindGroupFileItemByEntryId(content, entryidCStr);
    if (item != NULL)
    {
        item->ClearDownloadProgress();
    }
    LOG_INFO("群文件下载取消: group=%lld, entryid=%s。", (long long)m_groupId, entryid.c_str());
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatRecallRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
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

    //调试入口（m_groupId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_groupId == 0)
    {
        return 0;
    }

    //群聊：ctype=2、cid=群 id。界面改动等服务端推回撤回通知后再做。
    CIUSocket::GetInstance().RecallMessage(conversation_group, m_groupId, msgid);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatPinRequest(UINT /*uMsg*/, WPARAM wParam,
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

    //调试入口（m_groupId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_groupId == 0)
    {
        return 0;
    }

    //群聊：ctype=2、cid=群 id。标记改动等服务端推回 Pin 通知后再做。
    CIUSocket::GetInstance().PinMessage(conversation_group, m_groupId, msgid, wParam != 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatDeleteRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
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

    //删除只对本人生效，群里其他成员不受影响，确认文案与单聊相同
    const int answer = ::MessageBox(m_hWnd,
        balloonchat::BuildDeleteMessagesConfirmText(msgids.size()),
        TR(L"删除消息"), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDOK)
    {
        return 0;
    }

    //调试入口（m_groupId==0，走 mock 数据）下没有服务端可问，直接在本窗口移除
    if (m_groupId == 0)
    {
        OnMessagesDeleted(msgids);
        return 0;
    }

    //cmd_delete_message 一次只收一个 msgid，多选时逐条发；气泡等删除推送到达后再移除
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        CIUSocket::GetInstance().DeleteMessage((int64_t)msgids[i]);
    }
    LOG_INFO("Group chat window requests delete messages, group=%lld, count=%u.",
             (long long)m_groupId, (unsigned)msgids.size());
    return 0;
}

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeSelfTextMessage(
    const net::MessageNotify& m, const CString& text, balloonchatwnd::ChatMessageList* list)
{
    const UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 右侧本端气泡（群聊本端气泡不显示发送人名）。本端真实头像（字形 / 真图）。
    balloonchatwnd::ChatMessage mine = balloonchatwnd::MakeTextMessage(true, text);
    mine.font = m.m_font;
    mine.isMarkdown = (m.m_textFormat == net::text_format_markdown);
    balloonchatwnd::ApplyCodeFields(mine, m.m_textFormat, m.m_codeLang, m.m_codeNotes, m.m_from);
    //msgid 与时间：重开窗口后仍在两分钟内的消息应可撤回。
    mine.msgid = m.m_msgid;
    mine.timestampMs = m.m_timestamp;
    mine.clientMsgId = m.m_clientmsgid;
    //@提醒要还原 —— 历史响应与推送里 content.at 都是带回来的（服务端把整个 content
    //原样落库、fillMessageFromObject 也解析了），漏填的话 @ 的高亮就没了。
    mine.ats = balloonchatwnd::BuildRenderSpansForMessage(
        m.m_text, m.m_ats, m.m_atAll, (int64_t)selfId);
    //引用回复同样要还原，理由与 ats 相同。
    balloonchatwnd::FillQuoteForRender(m.m_quote, mine,
        ResolveQuoteThumbPath((int64_t)selfId, m.m_quote, list));
    FillSelfSenderAvatar(m_lpFMGClient, mine);
    return mine;
}

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupShareMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    balloonchatwnd::ChatMessage card;
    CString ownerName;
    if (fromMe)
    {
        // 本人发出的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, TR(L"[共享]"), list);
    }
    else
    {
        // 别人发出的：左侧气泡 + 真实发送人名 / 头像（取自群成员映射，查不到回退"用户<id>"）；卡片由服务端以分享者
        // 的名义发出，发送人就是分享者
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, TR(L"[共享]"));
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
        ownerName = sender.m_name;
    }
    balloonchatwnd::ApplyFileShareCard(card, m.m_shareContent, ownerName,
                                       balloonchatwnd::MakeShareCardLiveState(m.m_shareContent));
    return card;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenShare(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
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
    LOG_INFO("group chat window opens share from card, group=%lld, shareid=%lld, own=%d, ok=%d.", (long long)m_groupId,
             shareId, own ? 1 : 0, ok ? 1 : 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    if (m_groupId != 0)
    {
        balloonfileshare::CFileShareHost::GetInstance().UnwatchConversationShares(conversation_group, m_groupId);
    }
    balloonmemo::CMemoCenter::Instance().UnregisterObserver(m_hWnd);
    //点群公告卡片打开过的公告详情窗：宿主是本窗口，随本窗口一并销毁
    if (m_announceCardDlg.IsWindow())
    {
        m_announceCardDlg.DestroyWindow();
    }
    //入群必读弹窗：宿主同样是本窗口
    if (m_mustReadDlg.IsWindow())
    {
        m_mustReadDlg.DestroyWindow();
    }
    //问答编辑窗：关闭并恢复本窗口的可用状态（本窗口随即销毁，恢复与否已无影响，统一走关闭路径）
    m_qaEditDlg.CloseDialog();
    //机器人资料卡：宿主同样是本窗口
    m_botCard.CloseCard();
    return 0;
}

// ---------------- 边栏「备忘」页签 ----------------

LRESULT CBalloonGroupChatWindow::OnMemoChanged(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int64_t groupid = (int64_t)wParam;
    if (groupid != 0 && groupid != m_groupId)
    {
        return 0;
    }
    RenderMemoPanel();
    RefreshMemoTabBadge();
    //消息列表里的群备忘卡片：备忘被删除、本人被移出提醒对象之后转为失效，重新出现时恢复
    RefreshMemoCardsAvailability();
    return 0;
}

void CBalloonGroupChatWindow::RefreshMemoTabBadge()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_groupId == 0)
    {
        return;
    }
    balloonchatwnd::GroupSideTab* tab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(content->FindCtrlById(balloonchatwnd::kGIdTabMemo));
    if (tab == NULL)
    {
        return;
    }
    balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    if (tab->IsActive())
    {
        center.MarkGroupSeen(m_groupId);
    }
    //标题「备忘 N」：备忘簿还没拉到（或拉取超时）时数量未知，只写「备忘」，与「已拉到且为 0」区分开
    int memoCount = balloonchatwnd::kSideTabCountUnknown;
    if (center.Book().IsLoaded())
    {
        std::vector<net::MemoItem> memos;
        center.Book().ListGroup(m_groupId, memos);
        memoCount = (int)memos.size();
    }
    tab->SetText(balloonchatwnd::MakeSideTabText(TR(L"备忘"), memoCount).c_str());
    tab->SetBadgeCount(center.Book().UnseenCount(m_groupId));
    tab->Invalidate();
}

void CBalloonGroupChatWindow::RenderMemoPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //卡片外面的底色：边栏是聊天窗根控件的白底
    const COLORREF kMemoListBg = RGB(0xFF, 0xFF, 0xFF);
    balloonwjui::DuiScrollView* list =
        dynamic_cast<balloonwjui::DuiScrollView*>(content->FindCtrlById(balloonchatwnd::kGIdMemoList));
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(balloonchatwnd::kGIdMemoEmpty));
    balloonwjui::DuiControl* add = content->FindCtrlById(balloonchatwnd::kGIdMemoAdd);
    balloonwjui::DuiControl* panel = content->FindCtrlById(balloonchatwnd::kGIdMemoPanel);

    const balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    std::vector<net::MemoItem> memos;
    center.Book().ListGroup(m_groupId, memos);
    const memo::LocalMinute now = memo::NowLocalMinute();
    const bool canEdit = balloonmemo::CanEditGroupMemo(m_myRole);
    std::vector<balloonmemo::MemoCardText> cards;
    m_memoCardIds.clear();
    for (size_t i = 0; i < memos.size(); ++i)
    {
        const net::MemoItem& item = memos[i];
        //创建人按本群成员表解析（群名片优先），与接龙详情窗同一口径
        const std::wstring creator((LPCTSTR)ResolveRelayMemberName(item.m_creator));
        cards.push_back(balloonmemo::BuildGroupTabCard(item, now, creator,
                                                       balloonmemo::CMemoCenter::CreatedAtOf(item), canEdit));
        m_memoCardIds.push_back(item.m_memoid);
    }
    balloonmemo::SetMemoCards(list, cards, kMemoListBg);

    //到了最早的那个提醒时刻，那张卡片的时间要换成下一次事件或「已结束」：安排一次重画
    memo::LocalMinute earliest = 0;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        if (cards[i].m_remindAt > 0 && (earliest == 0 || cards[i].m_remindAt < earliest))
        {
            earliest = cards[i].m_remindAt;
        }
    }
    if (IsWindow())
    {
        ::KillTimer(m_hWnd, kMemoPanelRefreshTimerId);
        if (earliest > 0)
        {
            long long delay = memo::MillisUntilLocalMinute(earliest) + kMemoPanelRefreshSlackMs;
            if (delay > kMemoPanelRefreshMaxDelayMs)
            {
                delay = kMemoPanelRefreshMaxDelayMs;
            }
            ::SetTimer(m_hWnd, kMemoPanelRefreshTimerId, (UINT)delay, NULL);
        }
    }

    //空状态提示在卡片上方，只在没有卡片时显示；拉取超时时提示没有加载出来，并置灰新建
    const bool timedOut = center.ListTimedOut();
    if (empty != NULL)
    {
        empty->SetVisible(cards.empty());
        if (timedOut)
        {
            empty->SetText(TR(L"备忘没有加载出来，服务器可能还不支持备忘"));
        }
        else
        {
            empty->SetText(center.Book().IsLoaded() ? TR(L"本群还没有群备忘") : TR(L"正在加载……"));
        }
    }
    if (add != NULL)
    {
        add->SetVisible(m_groupId != 0 && canEdit);
        add->SetEnabled(!timedOut);
    }
    //显隐变化之后对面板按它当前的矩形强制重排（开发约定文档 UI 约定第 5 条）
    if (panel != NULL && panel->IsVisible())
    {
        panel->ForceLayout(panel->GetRect());
    }
    RefreshMemoHoverAtCursor();
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

std::vector<net::ContactItem> CBalloonGroupChatWindow::BuildMemoCandidates() const
{
    std::vector<net::ContactItem> out;
    const int64_t selfId = (m_lpFMGClient != NULL) ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    for (std::map<int64_t, MemberInfo>::const_iterator it = m_memberInfo.begin(); it != m_memberInfo.end(); ++it)
    {
        if (it->first == selfId)
        {
            continue;
        }
        net::ContactItem c;
        c.m_userid = it->first;
        //先转成 std::wstring：传 LPCTSTR 会选中返回堆内存 char* 的那个重载
        c.m_nickname = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)it->second.m_name));
        c.m_facetype = it->second.m_facetype;
        c.m_customface = it->second.m_customface;
        out.push_back(c);
    }
    return out;
}

void CBalloonGroupChatWindow::OpenGroupMemoEditor(int64_t memoid)
{
    if (m_groupId == 0)
    {
        return;
    }
    balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    const bool canEdit = balloonmemo::CanEditGroupMemo(m_myRole);
    if (memoid == 0)
    {
        if (!canEdit || center.ListTimedOut())
        {
            return;
        }
        const memo::LocalMinute now = memo::NowLocalMinute();
        net::MemoItem item = balloonmemo::DefaultMemoForDay(now / memo::kMinutesPerDay, now, net::kMemoCtypeGroup, m_groupId);
        item.m_targetAll = true;
        center.OpenEditor(m_hWnd, item, balloonmemo::kMemoEditNew, BuildMemoCandidates());
        return;
    }
    const net::MemoItem* item = center.Book().Find(memoid);
    if (item == NULL)
    {
        return;
    }
    center.OpenEditor(m_hWnd, *item, canEdit ? balloonmemo::kMemoEditExisting : balloonmemo::kMemoEditReadOnly,
                      BuildMemoCandidates());
}

void CBalloonGroupChatWindow::OpenGroupMemoEditorWithPrefill(const std::string& titleUtf8, const std::string& noteUtf8)
{
    if (m_groupId == 0)
    {
        return;
    }
    balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    //权限与备忘列表的检查与边栏「新建」相同：角色可能在菜单弹出之后才变化
    if (!balloonmemo::CanEditGroupMemo(m_myRole) || center.ListTimedOut())
    {
        LOG_WARNING("group chat window: add to group memo ignored, group=%lld, role=%d.", (long long)m_groupId, m_myRole);
        return;
    }
    const memo::LocalMinute now = memo::NowLocalMinute();
    net::MemoItem item = balloonmemo::DefaultMemoForDay(now / memo::kMinutesPerDay, now, net::kMemoCtypeGroup, m_groupId);
    item.m_targetAll = true;
    item.m_title = titleUtf8;
    item.m_note = noteUtf8;
    center.OpenEditor(m_hWnd, item, balloonmemo::kMemoEditNew, BuildMemoCandidates());
}

LRESULT CBalloonGroupChatWindow::OnChatAddToMemo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::ChatAddToMemoRequest* req = reinterpret_cast<balloonchatwnd::ChatAddToMemoRequest*>(lParam);
    if (req == NULL)
    {
        return 0;
    }
    //来源：群名与发送人（本人发的写「我」）
    balloonchatwnd::MemoPrefillSource src;
    src.m_groupName = m_groupName;
    src.m_senderName = req->m_msg.senderName;
    src.m_fromMe = req->m_msg.fromMe;
    src.m_timestampMs = req->m_msg.timestampMs;
    const balloonchatwnd::MemoPrefill prefill = balloonchatwnd::BuildMemoPrefill(req->m_msg, src);
    const int target = req->m_target;
    LOG_INFO("group chat window: add to memo, group=%lld, msgid=%lld, target=%d, title bytes=%u, note bytes=%u.",
             (long long)m_groupId, req->m_msg.msgid, target, (unsigned)prefill.m_title.size(),
             (unsigned)prefill.m_note.size());
    delete req;

    if (target == balloonchatwnd::kMemoPrefillGroup)
    {
        OpenGroupMemoEditorWithPrefill(prefill.m_title, prefill.m_note);
        return 0;
    }
    //本人备忘：与「备忘与提醒」窗口的「新建」相同，备忘列表还没取到时不打开
    balloonmemo::CMemoCenter& center = balloonmemo::CMemoCenter::Instance();
    if (center.ListTimedOut())
    {
        LOG_WARNING("group chat window: add to memo ignored, memo list not available, group=%lld.", (long long)m_groupId);
        return 0;
    }
    const memo::LocalMinute now = memo::NowLocalMinute();
    net::MemoItem item = balloonmemo::DefaultMemoForDay(now / memo::kMinutesPerDay, now, net::kMemoCtypePersonal, 0);
    item.m_title = prefill.m_title;
    item.m_note = prefill.m_note;
    center.OpenEditor(m_hWnd, item, balloonmemo::kMemoEditNew, std::vector<net::ContactItem>());
    return 0;
}

void CBalloonGroupChatWindow::ShowGroupMemoMenu(int64_t memoid)
{
    if (balloonmemo::CMemoCenter::Instance().Book().Find(memoid) == NULL)
    {
        return;
    }
    const bool canEdit = balloonmemo::CanEditGroupMemo(m_myRole);
    //菜单命令 id
    const UINT kMenuIdOpen = 1;
    const UINT kMenuIdDelete = 2;
    balloonwjui::DuiMenu menu;
    menu.AppendItem(kMenuIdOpen, canEdit ? TR(L"修改") : TR(L"查看"));
    if (canEdit)
    {
        menu.AppendSeparator();
        menu.AppendItem(kMenuIdDelete, TR(L"删除"));
    }
    POINT pt;
    ::GetCursorPos(&pt);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    if (chosen == kMenuIdOpen)
    {
        OpenGroupMemoEditor(memoid);
    }
    else if (chosen == kMenuIdDelete)
    {
        balloonmemo::CMemoCenter::Instance().ConfirmAndDelete(m_hWnd, memoid);
    }
}

void CBalloonGroupChatWindow::PostMemoCardAction(int action, int cardIndex)
{
    if (cardIndex < 0 || cardIndex >= (int)m_memoCardIds.size())
    {
        return;
    }
    m_pendingMemoCardId = m_memoCardIds[cardIndex];
    PostMessage(kMemoCardActionMsg, (WPARAM)action, 0);
}

LRESULT CBalloonGroupChatWindow::OnMemoCardActionMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int64_t memoid = m_pendingMemoCardId;
    m_pendingMemoCardId = 0;
    if (memoid == 0)
    {
        return 0;
    }
    switch ((int)wParam)
    {
    //「修改」或双击
    case kMemoCardActionOpen:
        OpenGroupMemoEditor(memoid);
        break;
    //「删除」：只有群主、管理员的卡片上有这个按钮；中枢弹确认框，删除成功后备忘簿变化、面板随之重绘
    case kMemoCardActionDelete:
        if (balloonmemo::CanEditGroupMemo(m_myRole))
        {
            balloonmemo::CMemoCenter::Instance().ConfirmAndDelete(m_hWnd, memoid);
        }
        break;
    //右键
    case kMemoCardActionMenu:
        ShowGroupMemoMenu(memoid);
        break;
    //未定义的操作：不会出现，忽略
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMemoMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiControl* content = GetClientContent();
    //「备忘」面板隐藏时卡片视口自己的可见标志仍为 true，不判面板的话鼠标在成员列表上移动也会改卡片的悬停态
    balloonwjui::DuiControl* memoPanel = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kGIdMemoPanel) : NULL;
    if (memoPanel != NULL && memoPanel->IsVisible())
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        balloonmemo::UpdateMemoCardHover(
            dynamic_cast<balloonwjui::DuiScrollView*>(content->FindCtrlById(balloonchatwnd::kGIdMemoList)), pt);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMemoMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //鼠标离开整个窗口：撤掉全部悬停，否则会留一张卡片的按钮一直显示
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        POINT none;
        none.x = -1;
        none.y = -1;
        balloonmemo::UpdateMemoCardHover(
            dynamic_cast<balloonwjui::DuiScrollView*>(content->FindCtrlById(balloonchatwnd::kGIdMemoList)), none);
    }
    bHandled = FALSE;
    return 0;
}

void CBalloonGroupChatWindow::RefreshMemoHoverAtCursor()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || !IsWindow())
    {
        return;
    }
    POINT screen;
    ::GetCursorPos(&screen);
    POINT pt = screen;
    ScreenToClient(&pt);
    //光标不在本窗口上（被别的窗口挡住、或在窗口外）时撤掉全部悬停
    if (::WindowFromPoint(screen) != m_hWnd)
    {
        pt.x = -1;
        pt.y = -1;
    }
    balloonmemo::UpdateMemoCardHover(
        dynamic_cast<balloonwjui::DuiScrollView*>(content->FindCtrlById(balloonchatwnd::kGIdMemoList)), pt);
}

void CBalloonGroupChatWindow::SetShareBadgeCount(int count)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatIconButton* btn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kGIdHdrShare))
        : NULL;
    if (btn == NULL)
    {
        return;
    }
    btn->SetBadgeCount(count);
}

int CBalloonGroupChatWindow::GetShareBadgeCount()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatIconButton* btn = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatIconButton*>(content->FindCtrlById(balloonchatwnd::kGIdHdrShare))
        : NULL;
    return (btn != NULL) ? btn->GetBadgeCount() : 0;
}

void CBalloonGroupChatWindow::OpenConversationShares()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* btn = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kGIdHdrShare) : NULL;
    if (btn == NULL || m_groupId == 0)
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
        m_hWnd, conversation_group, (int64_t)m_groupId, &anchor);
    LOG_INFO("group chat window opens conversation shares, group=%lld, ok=%d.", (long long)m_groupId, ok ? 1 : 0);
}

void CBalloonGroupChatWindow::ApplyShareButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* btn = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kGIdHdrShare) : NULL;
    if (btn == NULL)
    {
        return;
    }
    const bool show = balloonfileshare::CFileShareHost::GetInstance().IsModuleReady();
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

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupScreenShareMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    balloonchatwnd::ChatMessage card;
    CString hostName;
    if (fromMe)
    {
        // 本人发起的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, TR(L"[屏幕共享]"), list);
    }
    else
    {
        // 别人发起的：左侧气泡 + 真实发送人名 / 头像；卡片由服务端以发起人的名义发出，发送人就是发起人
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, TR(L"[屏幕共享]"));
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
        hostName = sender.m_name;
    }
    const std::string roomId = balloonchatwnd::ScreenShareRoomIdOf(m.m_screenShareContent);
    const int state = balloonscreenshare::CScreenShareHub::GetInstance().RoomState(
        conversation_group, (int64_t)m_groupId, roomId);
    balloonchatwnd::ApplyScreenShareCard(card, m.m_screenShareContent, hostName, state);
    return card;
}

// ---------------- 群公告卡片与群备忘卡片（docs/群公告接入方案.md §13.4、docs/备忘与提醒方案.md §7.3） ----------------

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupInfoCardMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    const bool isAnnouncement = (m.m_msgtype == kMsgTypeAnnouncement);
    const CString placeholder = isAnnouncement ? TR(L"[群公告]") : TR(L"[群备忘]");
    balloonchatwnd::ChatMessage card;
    if (fromMe)
    {
        // 本人发的（本人发布的公告、本人新建或修改的备忘）：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, placeholder, list);
    }
    else
    {
        // 别人发的：左侧气泡 + 真实发送人名 / 头像；卡片由服务端以操作人的名义发出，发送人就是操作人
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, placeholder);
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
    }

    const CString actor = fromMe ? CString() : InfoCardActorName(m.m_from);
    if (isAnnouncement)
    {
        balloonchatwnd::ApplyAnnouncementCard(card, m.m_announcementContent, actor);
        //入群必读卡片（docs/群公告接入方案.md §14）：服务端以系统名义（发送者 0）发出，只有新成员本人看得到。
        //发送人一栏显示「入群必读」，不按 userid 0 去查成员
        balloonchatwnd::AnnouncementCardInfo info;
        if (!fromMe && balloonchatwnd::ParseAnnouncementCard(m.m_announcementContent, info) && info.m_mustRead)
        {
            const CString name = balloonchatwnd::MustReadSenderName();
            card.senderName            = name;
            card.senderUserId          = 0;
            card.senderAvatarText      = name.Left(1);
            card.senderAvatarColor     = kMustReadAvatarColor;
            card.senderAvatarImagePath.Empty();
        }
        return card;
    }

    balloonchatwnd::ApplyMemoCard(card, m.m_memoCardContent, actor, memo::NowLocalMinute());
    //备忘簿里已经没有这一条（备忘被删除、或本人已不在提醒对象中）时转为失效
    const memo::CMemoBook& book = balloonmemo::CMemoCenter::Instance().Book();
    const bool inBook = (card.infoCardRefId > 0) && (book.Find(card.infoCardRefId) != NULL);
    balloonchatwnd::SetMemoCardAvailable(card, balloonchatwnd::IsMemoCardAvailable(book.IsLoaded(), inBook));
    return card;
}

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupCloudRecordMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    const CString placeholder = TR(L"[云端录像]");
    balloonchatwnd::ChatMessage card;
    if (fromMe)
    {
        // 本人分享的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, placeholder, list);
    }
    else
    {
        // 别人分享的：左侧气泡 + 真实发送人名 / 头像；卡片由服务端以开启者的名义发出，发送人就是开启者
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, placeholder);
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
    }
    // 副标题里的名字：成员表或通讯录查得到时用那里的显示名；content 里没有服务端写的昵称，查不到时退回气泡上的发送人名
    CString actor;
    if (!fromMe)
    {
        actor = InfoCardActorName(m.m_from);
        if (actor.IsEmpty())
            actor = card.senderName;
    }
    balloonchatwnd::ApplyCloudRecordCard(card, m.m_cloudRecordContent, actor);
    return card;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenCloudRecordCard(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                           BOOL& /*bHandled*/)
{
    balloonchatwnd::CloudRecordOpenRequest* pReq = (balloonchatwnd::CloudRecordOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    const int64_t recordId = (int64_t)pReq->m_recordId;
    const std::wstring title((LPCWSTR)pReq->m_title);
    delete pReq;
    const bool ok = balloonscreenshare::CScreenShareHub::GetInstance().OpenCloudRecord(m_hWnd, recordId, title);
    LOG_INFO("group chat window: cloud record card play, group=%lld, record=%lld, ok=%d.",
             (long long)m_groupId, (long long)recordId, ok ? 1 : 0);
    return 0;
}

CString CBalloonGroupChatWindow::InfoCardActorName(int64_t userId) const
{
    balloonchatwnd::GroupSenderRender found;
    if (userId <= 0 || !LookupSenderChain(const_cast<CBalloonGroupChatWindow*>(this), userId, found))
    {
        return CString();
    }
    return found.m_name;
}

bool CBalloonGroupChatWindow::IsShowingAnyMessage(const std::vector<int64_t>& msgids)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
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

void CBalloonGroupChatWindow::ApplyAnnouncementCardDeleted(int64_t cardMsgId, const std::string& cardContent)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || cardContent.empty() || !list->GetMessageByMsgid((long long)cardMsgId, msg)
        || !msg.screenShareIsAnnouncement)
    {
        return;
    }
    //副标题里的发布人名字沿用卡片上已经显示的那一个，不随删除改变
    balloonchatwnd::ApplyAnnouncementCard(msg, cardContent, msg.fromMe ? CString() : msg.screenShareHostName);
    list->SetMessageByMsgid((long long)cardMsgId, msg);
    LOG_INFO("group chat window: announcement card rewritten as deleted, group=%lld, msgid=%lld.",
             (long long)m_groupId, (long long)cardMsgId);
}

int CBalloonGroupChatWindow::RefreshMemoCardsAvailability()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }
    const memo::CMemoBook& book = balloonmemo::CMemoCenter::Instance().Book();
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgScreenShare);
    int refreshed = 0;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        if (!list->GetMessageByMsgid(msgids[i], msg) || !msg.screenShareIsMemo || msg.infoCardRefId <= 0)
        {
            continue;
        }
        const bool inBook = (book.Find(msg.infoCardRefId) != NULL);
        if (balloonchatwnd::SetMemoCardAvailable(msg, balloonchatwnd::IsMemoCardAvailable(book.IsLoaded(), inBook)))
        {
            list->SetMessageByMsgid(msgids[i], msg);
            ++refreshed;
        }
    }
    if (refreshed > 0)
    {
        LOG_INFO("group chat window: memo cards availability refreshed, group=%lld, cards=%d.",
                 (long long)m_groupId, refreshed);
    }
    return refreshed;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenAnnouncementCard(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                            BOOL& /*bHandled*/)
{
    balloonchatwnd::InfoCardOpenRequest* pReq = (balloonchatwnd::InfoCardOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    const long long msgid = pReq->m_msgid;
    delete pReq;

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetMessageByMsgid(msgid, msg) || !balloonchatwnd::IsAnnouncementCardOpenable(msg))
    {
        return 0;
    }
    balloonchatwnd::AnnouncementCardInfo info;
    if (!balloonchatwnd::ParseAnnouncementCard(msg.screenShareContent, info) || info.m_deleted)
    {
        return 0;
    }
    //入群必读卡片、本人仍待确认：按入群必读模式弹出（带「我已阅读」），与输入区「点此阅读」是同一个弹窗
    if (info.m_mustRead && m_mustReadPending && m_mustReadHasItem && info.m_announcementId == m_mustReadItem.m_id)
    {
        ShowMustReadPopup();
        return 0;
    }

    //发布人：本人发的写本人昵称；别人发的取卡片上显示的那个名字
    CString publisher = msg.fromMe ? CString() : msg.screenShareHostName;
    if (publisher.IsEmpty())
    {
        publisher = info.m_publisherName;
    }
    //时间的写法与公告边栏相同（同一个 FormatAnnouncementTime、同一个取当前时间的函数）
    const CString timeText = balloongroupmanage::FormatAnnouncementTime(info.m_publishTimeMs, NowMillisecondsForNotice());
    //公告详情窗的宿主是本窗口：居中到本窗口，「查看全部」发回本窗口（FMG_MSG_OPEN_GROUP_ANNOUNCE）。
    //从卡片点开的是一条已有的公告，标题写「群公告」，不沿用新公告提示的「新群公告」
    m_announceCardDlg.SetCaption(TR(L"群公告"));
    m_announceCardDlg.ShowAnnouncement(m_hWnd, (int64_t)m_groupId, m_groupName, info.m_title, info.m_body,
                                       info.m_severity, publisher, timeText);
    LOG_INFO("group chat window: announcement card opened, group=%lld, msgid=%lld, announcement=%lld.",
             (long long)m_groupId, msgid, info.m_announcementId);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenCodeViewer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::CodeViewerOpenRequest* pReq = (balloonchatwnd::CodeViewerOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    // 代码片段的发送者：本人发的取本人 userid，别人发的取消息上的发送者
    const long long senderId = pReq->m_fromMe ? SelfUserIdForCode() : pReq->m_senderId;
    // 打开代码查看窗，已发出的代码片段可以在查看窗里追加批注（docs/代码片段方案.md 第 12 节）；
    // 聊天卡片右键「添加批注…」打开时展开批注栏
    ShowCodeViewer(pReq->m_code, pReq->m_lang, pReq->m_notes, pReq->m_key, pReq->m_msgid, senderId, pReq->m_fromMe,
                   pReq->m_action == balloonchatwnd::kCodeViewerActionAnnotate);
    delete pReq;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenMarkdownPreview(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::MarkdownPreviewOpenRequest> req((balloonchatwnd::MarkdownPreviewOpenRequest*)lParam);
    if (!req)
    {
        return 0;
    }
    const std::wstring sender = req->m_fromMe ? std::wstring(TR(L"我")) : req->m_sender;
    balloonmd::MarkdownPreviewDoc doc;
    doc.m_sourceUtf8 = req->m_sourceUtf8;
    doc.m_title = req->m_title;
    doc.m_meta = balloonmd::MarkdownPreviewMeta(sender, req->m_timestampMs);
    doc.m_key = req->m_key;
    balloonmd::CMarkdownEditorManager::GetInstance().OpenPreview(doc);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatOpenMemoCard(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::InfoCardOpenRequest* pReq = (balloonchatwnd::InfoCardOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    const long long msgid = pReq->m_msgid;
    const int64_t memoid = (int64_t)pReq->m_refId;
    delete pReq;
    if (memoid <= 0)
    {
        return 0;
    }

    //备忘簿里已经没有这一条：卡片转为失效、不打开。点的那一刻与最近一次刷新之间备忘簿可能刚刚变过
    if (balloonmemo::CMemoCenter::Instance().Book().Find(memoid) == NULL)
    {
        RefreshMemoCardsAvailability();
        LOG_INFO("group chat window: memo card no longer available, group=%lld, msgid=%lld, memoid=%lld.",
                 (long long)m_groupId, msgid, (long long)memoid);
        return 0;
    }
    //打开方式与「备忘」页签双击卡片相同：群主、管理员修改，其他人只读
    OpenGroupMemoEditor(memoid);
    LOG_INFO("group chat window: memo card opened, group=%lld, msgid=%lld, memoid=%lld.",
             (long long)m_groupId, msgid, (long long)memoid);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnOpenGroupAnnounceMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                                        BOOL& /*bHandled*/)
{
    if ((int64_t)wParam != (int64_t)m_groupId)
    {
        return 0;
    }
    ActivateNoticeTab();
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatJoinScreenShare(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                       BOOL& /*bHandled*/)
{
    //lParam 为堆上的加入请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::ScreenShareJoinRequest> req((balloonchatwnd::ScreenShareJoinRequest*)lParam);
    if (!req || m_groupId == 0)
    {
        return 0;
    }
    // 加入要走信令通道，离线时必然失败，在入口就拦住。
    if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return 0;
    }
    //群通话卡片复用屏幕共享卡片的版式与这条消息：加入的是群通话
    if (req->m_groupCall)
    {
        balloongroupcall::CGroupCallHub::GetInstance().JoinCall((int64_t)m_groupId, m_groupName, req->m_roomId);
        return 0;
    }
    balloonscreenshare::CScreenShareHub::GetInstance().JoinShare(conversation_group, (int64_t)m_groupId,
                                                                 req->m_roomId, req->m_hostName, req->m_forced);
    return 0;
}

//本账号的语音缓存目录 Users\<账号>\ChatVoice\（语音消息第 3b 步，见 docs/语音消息方案.md 第 7.3 节）
static std::wstring CurrentUserVoiceDir()
{
    return net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(), L"ChatVoice");
}

void CBalloonGroupChatWindow::RenderIncomingGroupVoice(balloonchatwnd::ChatMessageList* list, bool fromMe,
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
    if (fromMe)
    {
        // 自己发的：本端真实头像（字形 / 真图），不显示发送人名
        FillSelfSenderAvatar(m_lpFMGClient, msg);
    }
    else
    {
        // 别人发的：真实发送人名 / 头像（取自群成员映射，查不到回退"用户<id>"）
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(notify.m_from);
        msg.senderName            = sender.m_name;
        msg.senderUserId          = notify.m_from;
        msg.senderAvatarText      = sender.m_glyph;
        msg.senderAvatarColor     = sender.m_color;
        msg.senderAvatarImagePath = sender.m_imagePath;
    }
    list->AppendMessage(msg);

    // 本机没有这段语音：在后台下载到 ChatVoice\<md5>.ogg，结果按 md5 经主面板广播回来（OnVoiceDownloadResult）
    if (needDownload)
    {
        net::FileTransfer::GetInstance().EnqueueVoiceDownload(
            notify.m_voiceFileId, notify.m_voiceMd5, notify.m_voiceSize,
            balloonvoice::VoiceCachePath(voiceDir, notify.m_voiceMd5));
    }
    LOG_INFO("group chat window: voice message shown, group=%lld, from=%lld, msgid=%lld, played=%d, download=%d.",
             (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid, isPlayed ? 1 : 0,
             needDownload ? 1 : 0);
}

void CBalloonGroupChatWindow::StartVoiceRecording()
{
    //已经有录音条在显示时录音会话直接忽略；失败时由它提示原因（经 ShowVoiceRecordToast）
    m_voiceSession.Start(CurrentUserVoiceDir(), GenClientMsgId());
}

void CBalloonGroupChatWindow::SendRecordedVoice(const std::string& clientMsgId,
                                                const balloonvoice::VoiceRecordOutput& out)
{
    LOG_INFO("group chat window: voice recorded, clientmsgid %s, %lld ms, %lld bytes.", clientMsgId.c_str(),
             static_cast<long long>(out.m_durationMs), static_cast<long long>(out.m_fileBytes));

    //录好的文件已在 ChatVoice\<md5>.ogg：先在列表末尾插一条发送中的语音气泡，再交给文件传输线程上传并发出
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeSendingVoiceMessage(
        clientMsgId, out.m_durationMs, out.m_wave, out.m_path, out.m_md5, out.m_fileBytes);
    // 自己发的：本端真实头像（字形 / 真图），不显示发送人名
    FillSelfSenderAvatar(m_lpFMGClient, msg);
    list->AppendMessage(msg);
    StartVoiceUpload(msg);
}

LRESULT CBalloonGroupChatWindow::OnChatVoiceAction(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
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

void CBalloonGroupChatWindow::ResendVoice(const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
            LOG_INFO("group chat window: voice resend refused, file missing, clientmsgid=%s.", clientMsgId.c_str());
        }
        return;
    }
    //气泡位置不变、回到发送中并清掉失败原因，再用原 clientmsgid 重新上传（文件已在服务端时只重发 1503）
    if (!list->RestartVoiceSendByClientMsgId(clientMsgId))
    {
        return;
    }
    LOG_INFO("group chat window: voice resend, clientmsgid=%s.", clientMsgId.c_str());
    StartVoiceUpload(msg);
}

void CBalloonGroupChatWindow::StartVoiceUpload(const balloonchatwnd::ChatMessage& msg)
{
    //调试入口没有会话绑定，不真正发送
    if (m_groupId == 0)
    {
        LOG_INFO("group chat window: voice upload skipped, no conversation bound, clientmsgid=%s.", msg.clientMsgId.c_str());
        return;
    }
    net::FileTransfer::GetInstance().EnqueueSendVoice(conversation_group, (int64_t)m_groupId, msg.clientMsgId,
                                                      std::wstring((LPCWSTR)msg.voiceLocalPath), msg.voiceDuration,
                                                      net::EncodeVoiceWave(msg.voiceWave));
    //登记待冒泡：发送回执成功后才把本会话冒泡到会话列表顶部、预览写「[语音]」，被服务端拒绝的消息不冒泡
    ConvSendBumpData bump;
    bump.ctype   = conversation_group;
    bump.cid     = (long long)m_groupId;
    bump.msgType = kMsgTypeVoice;
    bump.digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeVoice, std::string());
    m_sendBumps.Add(msg.clientMsgId, bump);
}

void CBalloonGroupChatWindow::HandleVoiceClick(long long msgid, const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
        LOG_INFO("group chat window: voice download retried, md5=%s.", msg.voiceMd5.c_str());
        break;
    }
    //其它取值不会出现
    default:
        break;
    }
}

void CBalloonGroupChatWindow::StartVoicePlayback(const balloonchatwnd::ChatMessage& msg)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
        LOG_WARNING("group chat window: voice play failed, msgid=%lld.", msg.msgid);
        return;
    }
    list->SetVoicePlaying(msg.msgid, msg.clientMsgId, true);
    //对方发来、还没听过：请主面板在本地库标为已听（红点只记在本机，方案 V7）
    if (!msg.fromMe && !msg.voicePlayed && msg.msgid > 0 && ::IsWindow(m_hostPanel))
    {
        balloonchatwnd::VoicePlayedMark* mark = new balloonchatwnd::VoicePlayedMark();
        mark->m_ctype = conversation_group;
        mark->m_cid = (int64_t)m_groupId;
        mark->m_msgid = msg.msgid;
        if (!::PostMessage(m_hostPanel, FMG_MSG_VOICE_PLAYED, 0, (LPARAM)mark))
        {
            delete mark;
        }
    }
}

void CBalloonGroupChatWindow::OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs, int64_t totalMs)
{
    (void)totalMs;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list != NULL)
    {
        list->SetVoicePlayFrame(key.m_msgid, key.m_clientMsgId, balloonchatwnd::VoicePlayFrame(playedMs));
    }
}

void CBalloonGroupChatWindow::OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
        LOG_INFO("group chat window: voice auto-play next, msgid=%lld.", next.msgid);
        StartVoicePlayback(next);
    }
}

void CBalloonGroupChatWindow::ShowVoiceRecordToast(const std::wstring& text)
{
    ShowToast(text.c_str());
}

void CBalloonGroupChatWindow::ShowVoiceRecordBarUi(bool show)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* input = content->FindCtrlById(balloonchatwnd::kGIdInputEdit);
    balloonchatwnd::ShowVoiceRecordBar(input, content->FindCtrlById(balloonchatwnd::kGIdInputBottomRow),
                                       content->FindCtrlById(balloonchatwnd::kGIdVoiceBar), show);
    //录音时把焦点从（已隐藏的）输入框移开；收起后还给输入框
    SetDuiFocus(show ? NULL : input);
    InvalidateRect(NULL, FALSE);
}

void CBalloonGroupChatWindow::ApplyVoiceButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //「语音消息」的录音与播放都由 FlamingoMedia.dll 提供：模块没有加载时隐藏
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbVoice);
    const bool show = balloonvoice::CVoiceHub::GetInstance().IsModuleReady();
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

void CBalloonGroupChatWindow::ApplyScreenShareButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //头部「共享屏幕」与工具栏「屏幕录制」都由 FlamingoMedia.dll 提供：模块没有加载（录屏另要求系统支持）时隐藏
    const balloonscreenshare::CScreenShareHub& hub = balloonscreenshare::CScreenShareHub::GetInstance();
    const UINT ids[] = { balloonchatwnd::kGIdHdrScreenShare, balloonchatwnd::kGIdTbRecord };
    const bool shows[] = { hub.IsModuleReady(), hub.IsRecordSupported() };
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

void CBalloonGroupChatWindow::RefreshScreenShareBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonscreenshare::ScreenShareActiveInfo info;
    std::wstring text;
    m_shareBannerRoomId.clear();
    m_shareBannerHostName.Empty();
    m_shareBannerSelfHost = false;
    m_shareBannerForced = false;
    if (balloonscreenshare::CScreenShareHub::GetInstance().GetActive(conversation_group, (int64_t)m_groupId, info))
    {
        const int64_t selfId = (m_lpFMGClient != NULL)
                               ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        //查询应答不带发起人名：按群成员资料补上
        CString hostName = info.m_hostName.c_str();
        if (hostName.IsEmpty() && info.m_hostId > 0)
        {
            hostName = ResolveSender(info.m_hostId).m_name;
        }
        m_shareBannerRoomId = info.m_roomId;
        m_shareBannerHostName = hostName;
        m_shareBannerSelfHost = (selfId != 0 && info.m_hostId == selfId);
        m_shareBannerForced = info.m_forced;
        text = balloonchatwnd::MakeScreenShareBannerText(m_shareBannerSelfHost, std::wstring(hostName.GetString()),
                                                         info.m_locked, info.m_forced, info.m_title);
    }
    //横幅显隐改变了消息区上方的占位高度：与 Pin 提示带同一处理，先整树重排，再请本窗把输入区块高度钉回
    if (balloonchatwnd::UpdateScreenShareBanner(content, text))
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }
}

bool CBalloonGroupChatWindow::OnScreenShareInfoChanged(int64_t msgid, const std::string& contentJson)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
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
    LOG_INFO("group chat window: screen share card info refreshed, group=%lld, msgid=%lld.", (long long)m_groupId,
             (long long)msgid);
    return true;
}

int CBalloonGroupChatWindow::OnScreenShareStatusChanged()
{
    RefreshScreenShareBanner();
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
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
        //群通话卡片复用这种气泡，状态另由 OnGroupCallStatusChanged 刷新；群公告卡片、群备忘卡片、云端录像卡片同样复用
        //这种气泡，没有房间，不参与屏幕共享状态的刷新
        if (!list->GetMessageByMsgid(msgids[i], msg) || msg.screenShareIsGroupCall
            || msg.screenShareIsAnnouncement || msg.screenShareIsMemo || msg.screenShareIsCloudRecord)
        {
            continue;
        }
        const int state = hub.RoomState(conversation_group, (int64_t)m_groupId, msg.screenShareRoomId);
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

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupCallCardMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    balloonchatwnd::ChatMessage card;
    CString initiatorName;
    if (fromMe)
    {
        // 本人发起的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, TR(L"[群语音通话]"), list);
    }
    else
    {
        // 别人发起的：左侧气泡 + 真实发送人名 / 头像；卡片由服务端以发起人的名义发出，发送人就是发起人
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, TR(L"[群语音通话]"));
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
        initiatorName = sender.m_name;
    }
    const balloongroupcall::CGroupCallHub& hub = balloongroupcall::CGroupCallHub::GetInstance();
    const std::string roomId = balloonchatwnd::GroupCallRoomIdOf(m.m_groupCallContent);
    const int state = hub.RoomState((int64_t)m_groupId, roomId);
    //进行中的那一场取状态缓存里的人数，其余情况不显示人数
    int count = 0;
    balloongroupcall::GroupCallActiveInfo active;
    if (state == balloongroupcall::kGroupCallRoomActive && hub.GetActive((int64_t)m_groupId, active))
    {
        count = active.m_count;
    }
    balloonchatwnd::ApplyGroupCallCard(card, m.m_groupCallContent, initiatorName, state, count);
    return card;
}

void CBalloonGroupChatWindow::StartGroupCallFromHeader(int media)
{
    if (m_groupId == 0 || m_lpFMGClient == NULL || !CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return;
    }
    balloongroupcall::CGroupCallHub& hub = balloongroupcall::CGroupCallHub::GetInstance();
    if (!hub.IsModuleReady())
    {
        ::MessageBox(m_hWnd, TR(L"群通话组件没有加载，暂时无法发起群通话。"), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    //本群已有进行中的群通话：不另起一场，直接加入那一场（一个群同时只有一场，docs/群音视频通话方案.md 第 2 节）
    balloongroupcall::GroupCallActiveInfo active;
    if (hub.GetActive((int64_t)m_groupId, active) && !active.m_roomId.empty())
    {
        LOG_INFO("group chat window: group call already active, join instead of start. group=%lld, room=%s.",
                 (long long)m_groupId, active.m_roomId.c_str());
        hub.JoinCall((int64_t)m_groupId, m_groupName, active.m_roomId);
        return;
    }
    const std::vector<net::ContactItem> candidates = BuildMemoCandidates();
    if (candidates.empty())
    {
        ::MessageBox(m_hWnd, TR(L"成员列表还没有加载，请稍后再试。"), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!m_callPicker)
    {
        m_callPicker.reset(new CCreateGroupDlg(NULL));
    }
    if (!m_callPickSink)
    {
        m_callPickSink.reset(new GroupCallPickSink(this));
    }
    m_callPickMedia = media;
    m_callPicker->SetPickSink(m_callPickSink.get());
    m_callPicker->OpenForPick(m_hWnd, CCreateGroupDlg::kPurposeGroupCall, (int64_t)m_groupId,
                              (UINT)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID, candidates,
                              std::vector<net::DepartmentItem>());
    LOG_INFO("group chat window: group call picker opened, group=%lld, media=%d, candidates=%u.",
             (long long)m_groupId, media, (uint32_t)candidates.size());
}

void CBalloonGroupChatWindow::OpenGroupCallInvitePicker(const std::vector<int64_t>& exclude)
{
    if (m_groupId == 0 || m_lpFMGClient == NULL || !IsWindow())
    {
        return;
    }
    //成员表还没到（成员表里至少有本人，为空就是没加载）：记下请求，OnGroupMembersArrived 里补开
    if (m_memberInfo.empty())
    {
        m_callInvitePending = true;
        m_callInviteExclude = exclude;
        LOG_INFO("group chat window: group call invite deferred until members arrive, group=%lld.",
                 (long long)m_groupId);
        return;
    }
    const std::vector<net::ContactItem> members = BuildMemoCandidates();
    //候选：本群成员（不含自己）去掉已加入或振铃中的人
    const std::set<int64_t> excluded(exclude.begin(), exclude.end());
    std::vector<net::ContactItem> candidates;
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (excluded.count(members[i].m_userid) == 0)
        {
            candidates.push_back(members[i]);
        }
    }
    if (candidates.empty())
    {
        ::MessageBox(m_hWnd, TR(L"本群成员都已在通话中或正在被邀请。"), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!m_callPicker)
    {
        m_callPicker.reset(new CCreateGroupDlg(NULL));
    }
    if (!m_callPickSink)
    {
        m_callPickSink.reset(new GroupCallPickSink(this));
    }
    //0 表示这一次是通话中追加邀请，不是发起
    m_callPickMedia = 0;
    m_callPicker->SetPickSink(m_callPickSink.get());
    m_callPicker->OpenForPick(m_hWnd, CCreateGroupDlg::kPurposeGroupCallInvite, (int64_t)m_groupId,
                              (UINT)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID, candidates,
                              std::vector<net::DepartmentItem>());
    LOG_INFO("group chat window: group call invite picker opened, group=%lld, excluded=%u, candidates=%u.",
             (long long)m_groupId, (uint32_t)exclude.size(), (uint32_t)candidates.size());
}

void CBalloonGroupChatWindow::OnGroupCallMembersPicked(const std::vector<int64_t>& userIds)
{
    if (m_groupId == 0 || !IsWindow() || userIds.empty())
    {
        return;
    }
    if (userIds.size() > kGroupCallMaxInvitees)
    {
        //人数上限含本人，所以提示里的数是可邀请人数加一
        const std::wstring tip = TRF(L"群通话最多 {0} 人，请少选几位成员。", (int)(kGroupCallMaxInvitees + 1));
        ::MessageBox(m_hWnd, tip.c_str(), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    balloongroupcall::CGroupCallHub& hub = balloongroupcall::CGroupCallHub::GetInstance();
    bool ok = false;
    if (m_callPickMedia > 0)
    {
        ok = hub.StartCall((int64_t)m_groupId, m_groupName, m_callPickMedia, userIds);
    }
    else
    {
        ok = hub.InviteMembers((int64_t)m_groupId, userIds);
    }
    LOG_INFO("group chat window: group call members picked, group=%lld, media=%d, count=%u, ok=%d.",
             (long long)m_groupId, m_callPickMedia, (uint32_t)userIds.size(), ok ? 1 : 0);
}

int CBalloonGroupChatWindow::OnGroupCallStatusChanged()
{
    RefreshGroupCallBanner();
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }
    const balloongroupcall::CGroupCallHub& hub = balloongroupcall::CGroupCallHub::GetInstance();
    balloongroupcall::GroupCallActiveInfo active;
    const bool hasActive = hub.GetActive((int64_t)m_groupId, active);
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgScreenShare);
    int refreshed = 0;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage msg;
        //屏幕共享卡片另由 OnScreenShareStatusChanged 刷新
        if (!list->GetMessageByMsgid(msgids[i], msg) || !msg.screenShareIsGroupCall)
        {
            continue;
        }
        const int state = hub.RoomState((int64_t)m_groupId, msg.screenShareRoomId);
        const int count = (state == balloongroupcall::kGroupCallRoomActive && hasActive) ? active.m_count : 0;
        const int oldState = msg.screenShareState;
        const CString oldStatus = msg.screenShareStatus;
        const bool oldEnabled = msg.screenShareButtonEnabled;
        balloonchatwnd::ApplyGroupCallState(msg, state, count);
        //状态、状态文字（含人数）与按钮都没变时不重画
        if (oldState == msg.screenShareState && oldStatus == msg.screenShareStatus
            && oldEnabled == msg.screenShareButtonEnabled)
        {
            continue;
        }
        list->SetMessageByMsgid(msgids[i], msg);
        ++refreshed;
    }
    return refreshed;
}

bool CBalloonGroupChatWindow::OnGroupCallCardChanged(int64_t msgid, const std::string& contentJson)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || msgid <= 0)
    {
        return false;
    }
    balloonchatwnd::ChatMessage msg;
    if (!list->GetMessageByMsgid(msgid, msg) || !msg.screenShareIsGroupCall)
    {
        return false;
    }
    //改写后的卡片带了时长，ApplyGroupCallCard 把它画成已结束、去掉按钮；发起人名沿用构造卡片时按群成员资料取到的那个
    const int state = balloongroupcall::CGroupCallHub::GetInstance().RoomState((int64_t)m_groupId,
                                                                               msg.screenShareRoomId);
    balloonchatwnd::ApplyGroupCallCard(msg, contentJson, msg.fromMe ? CString() : msg.screenShareHostName, state, 0);
    list->SetMessageByMsgid(msgid, msg);
    LOG_INFO("group chat window: group call card rewritten, group=%lld, msgid=%lld.", (long long)m_groupId,
             (long long)msgid);
    return true;
}

void CBalloonGroupChatWindow::RefreshGroupCallBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    std::wstring text;
    m_callBannerRoomId.clear();
    balloongroupcall::GroupCallActiveInfo info;
    if (balloongroupcall::CGroupCallHub::GetInstance().GetActive((int64_t)m_groupId, info))
    {
        m_callBannerRoomId = info.m_roomId;
        //本人是否已在通话里只有 FlamingoMedia.dll 知道：横幅一律写「点击加入」，已在通话里时点横幅由 DLL 把通话窗提到前面
        text = balloonchatwnd::MakeGroupCallBannerText(false, info.m_media == balloongroupcall::kGroupCallVideo,
                                                       info.m_count);
    }
    //横幅显隐改变了消息区上方的占位高度：与屏幕共享横幅同样处理，先整树重排，再请本窗把输入区块高度钉回
    if (balloonchatwnd::UpdateGroupCallBanner(content, text))
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }
}

int CBalloonGroupChatWindow::GetMemberRole(int64_t userId) const
{
    std::map<int64_t, MemberInfo>::const_iterator it = m_memberInfo.find(userId);
    if (it == m_memberInfo.end())
    {
        return -1;
    }
    return it->second.m_role;
}

void CBalloonGroupChatWindow::ApplyGroupCallButtonVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //头部「语音通话」「视频通话」由 FlamingoMedia.dll 提供，另受本企业的群通话功能开关控制
    const bool show = balloongroupcall::CGroupCallHub::GetInstance().IsModuleReady()
                      && m_lpFMGClient != NULL && m_lpFMGClient->m_UserMgr.m_bGroupCallFeature;
    const UINT ids[] = { balloonchatwnd::kGIdHdrVoice, balloonchatwnd::kGIdHdrVideo };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        balloonwjui::DuiControl* btn = content->FindCtrlById(ids[i]);
        if (btn == NULL || btn->IsVisible() == show)
        {
            continue;
        }
        btn->SetVisible(show);
        //显隐变化后对按钮所在的那一行重排（开发约定文档「UI 约定」第 5 条）
        balloonwjui::DuiControl* row = btn->GetParent();
        if (row != NULL)
        {
            row->ForceLayout(row->GetRect());
        }
    }
}

int CBalloonGroupChatWindow::OnShareCardStateChanged(long long shareId, int state, int reason)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    return balloonchatwnd::RefreshShareCardsInList(list, shareId, state, reason);
}

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupMergedMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    balloonchatwnd::ChatMessage card;
    if (fromMe)
    {
        // 本人发出的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, TR(L"[聊天记录]"), list);
    }
    else
    {
        // 别人发出的：左侧气泡 + 真实发送人名 / 头像（取自群成员映射，查不到回退"用户<id>"）
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, TR(L"[聊天记录]"));
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
    }
    balloonchatwnd::ApplyMergedCard(card, m.m_mergedContent);
    return card;
}

balloonchatwnd::ChatMessage CBalloonGroupChatWindow::MakeGroupRelayMessage(
    const net::MessageNotify& m, bool fromMe, balloonchatwnd::ChatMessageList* list)
{
    balloonchatwnd::ChatMessage card;
    if (fromMe)
    {
        // 本人发起的：右侧本端气泡，头像与 msgid 的填法与本人文字气泡相同
        card = MakeSelfTextMessage(m, TR(L"[接龙]"), list);
    }
    else
    {
        // 别人发起的：左侧气泡 + 真实发送人名 / 头像（取自群成员映射，查不到回退"用户<id>"）
        const balloonchatwnd::GroupSenderRender sender = ResolveSender(m.m_from);
        card = balloonchatwnd::MakeTextMessage(false, TR(L"[接龙]"));
        card.msgid                 = m.m_msgid;
        card.timestampMs           = m.m_timestamp;
        card.senderName            = sender.m_name;
        card.senderUserId          = m.m_from;
        card.senderAvatarText      = sender.m_glyph;
        card.senderAvatarColor     = sender.m_color;
        card.senderAvatarImagePath = sender.m_imagePath;
    }
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const RelayNameResolver names(*this);
    balloonchatwnd::ApplyRelayCard(card, m.m_relayContent, selfId, kRelaySelfMutedUnknown,
                                   NowMillisecondsForRelay(), names);
    TrackRelayDeadline(card);
    return card;
}

bool CBalloonGroupChatWindow::OnRelayChanged(const net::RelayChangedNotify& notify)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage card;
    if (list == NULL || !list->GetMessageByMsgid((long long)notify.m_msgid, card))
    {
        LOG_INFO("group chat window: relay changed notify, card not in view, group=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_msgid);
        return false;
    }

    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const RelayNameResolver names(*this);
    if (!balloonchatwnd::ApplyRelayNotifyToMessage(card, notify, selfId, kRelaySelfMutedUnknown,
                                                   NowMillisecondsForRelay(), names))
    {
        //列表里这一行已经不是接龙卡片（卡片被撤回、变成了提示行）
        LOG_INFO("group chat window: relay changed notify ignored, row is not a relay card, group=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_msgid);
        return false;
    }
    list->SetMessageByMsgid((long long)notify.m_msgid, card);
    TrackRelayDeadline(card);
    LOG_INFO("group chat window: relay card refreshed, group=%lld, msgid=%lld, items=%d, state=%d.",
             (long long)m_groupId, (long long)notify.m_msgid, card.relayCount, card.relayState);
    return true;
}

CString CBalloonGroupChatWindow::ResolveRelayMemberName(int64_t userid) const
{
    return ResolveSender(userid).m_name;
}

LRESULT CBalloonGroupChatWindow::OnChatRelayAction(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为气泡堆分配的卡片 msgid，无论成败都在这里释放
    std::unique_ptr<long long> pMsgid((long long*)lParam);
    if (!pMsgid || *pMsgid <= 0)
    {
        return 0;
    }
    const long long msgid = *pMsgid;

    std::unique_ptr<ballooncard::RelayDetailRequest> request(new ballooncard::RelayDetailRequest());
    request->m_ctype      = conversation_group;
    request->m_cid        = m_groupId;
    request->m_msgid      = msgid;
    request->m_selfRole   = m_myRole;
    request->m_focusInput = ((int)wParam == balloonchatwnd::kChatRelayActionJoin);
    //详情窗是本群聊窗的模态窗口：以本窗口为 owner，打开期间本窗口不可操作
    request->m_ownerHwnd  = m_hWnd;

    //卡片在当前视图里：带上它的内容（已合并收到过的推送）与发起人
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    balloonchatwnd::ChatMessage card;
    if (list != NULL && list->GetMessageByMsgid(msgid, card))
    {
        //列表里那一行已经不是卡片：卡片被撤回了，方案 §3.4 约定点击提示行无反应
        if (card.kind != balloonchatwnd::kMsgRelay)
        {
            LOG_INFO("group chat window: relay action ignored, card recalled, group=%lld, msgid=%lld.",
                     (long long)m_groupId, msgid);
            return 0;
        }
        const int64_t selfId = (m_lpFMGClient != NULL)
                               ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        request->m_contentJson = card.relayContent;
        request->m_creatorId   = card.fromMe ? selfId : card.senderUserId;
    }

    //调试入口没有主面板，详情窗由主面板持有，这里无处可投
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        LOG_INFO("group chat window: relay action dropped, no host panel, msgid=%lld.", msgid);
        return 0;
    }
    if (::PostMessage(m_hostPanel, FMG_MSG_OPEN_RELAY_DETAIL, 0, (LPARAM)request.get()))
    {
        request.release();   //投递成功，所有权交给主面板
    }
    LOG_INFO("group chat window: open relay detail requested, group=%lld, msgid=%lld, focus=%d, inView=%d.",
             (long long)m_groupId, msgid, (int)wParam, card.kind == balloonchatwnd::kMsgRelay ? 1 : 0);
    return 0;
}

void CBalloonGroupChatWindow::TrackRelayDeadline(const balloonchatwnd::ChatMessage& card)
{
    if (card.kind != balloonchatwnd::kMsgRelay || card.msgid <= 0)
    {
        return;
    }
    //还在进行中且有截止时间才需要定时重画；已结束、已截止、不限时的卡片从登记表里去掉
    if (card.relayState == relay_state_open && card.relayDeadline > 0)
    {
        m_relayDeadlines[card.msgid] = card.relayDeadline;
    }
    else
    {
        m_relayDeadlines.erase(card.msgid);
    }
    ScheduleRelayExpiryTimer();
}

void CBalloonGroupChatWindow::ScheduleRelayExpiryTimer()
{
    if (!::IsWindow(m_hWnd))
    {
        return;
    }
    if (m_relayDeadlines.empty())
    {
        ::KillTimer(m_hWnd, kRelayExpiryTimerId);
        return;
    }

    long long earliest = 0;
    for (std::map<long long, long long>::const_iterator it = m_relayDeadlines.begin();
         it != m_relayDeadlines.end(); ++it)
    {
        if (earliest == 0 || it->second < earliest)
        {
            earliest = it->second;
        }
    }
    long long delay = earliest - (long long)NowMillisecondsForRelay();
    if (delay < kRelayExpiryMinDelayMs)
    {
        delay = kRelayExpiryMinDelayMs;
    }
    if (delay > kRelayExpiryMaxDelayMs)
    {
        delay = kRelayExpiryMaxDelayMs;
    }
    //同一个 id 再次 SetTimer 会替换原来的定时器，不会叠加
    ::SetTimer(m_hWnd, kRelayExpiryTimerId, (UINT)delay, NULL);
}

void CBalloonGroupChatWindow::OnRelayExpiryTimer()
{
    ::KillTimer(m_hWnd, kRelayExpiryTimerId);

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    const int64_t nowMs = NowMillisecondsForRelay();
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const RelayNameResolver names(*this);

    //先挑出到期的再逐个处理：处理过程中会改动登记表
    std::vector<long long> due;
    for (std::map<long long, long long>::const_iterator it = m_relayDeadlines.begin();
         it != m_relayDeadlines.end(); ++it)
    {
        if (nowMs >= it->second)
        {
            due.push_back(it->first);
        }
    }
    for (size_t i = 0; i < due.size(); ++i)
    {
        m_relayDeadlines.erase(due[i]);
        balloonchatwnd::ChatMessage card;
        if (list == NULL || !list->GetMessageByMsgid(due[i], card)
            || !balloonchatwnd::RelayCardDueForExpiry(card, nowMs))
        {
            continue;   //卡片已不在列表里，或已被推送改成了别的状态
        }
        balloonchatwnd::RefreshRelayCard(card, selfId, kRelaySelfMutedUnknown, nowMs, names);
        list->SetMessageByMsgid(due[i], card);
        LOG_INFO("group chat window: relay card expired, group=%lld, msgid=%lld.",
                 (long long)m_groupId, due[i]);
    }
    ScheduleRelayExpiryTimer();
}

void CBalloonGroupChatWindow::RefreshAllRelayCards()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    const std::vector<long long> msgids = list->GetMsgidsOfKind(balloonchatwnd::kMsgRelay);
    if (msgids.empty())
    {
        return;
    }
    const int64_t nowMs = NowMillisecondsForRelay();
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const RelayNameResolver names(*this);
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        balloonchatwnd::ChatMessage card;
        if (!list->GetMessageByMsgid(msgids[i], card))
        {
            continue;
        }
        balloonchatwnd::RefreshRelayCard(card, selfId, kRelaySelfMutedUnknown, nowMs, names);
        list->SetMessageByMsgid(msgids[i], card);
        TrackRelayDeadline(card);
    }
    LOG_INFO("group chat window: %u relay card(s) refreshed after members arrived, group=%lld.",
             (unsigned)msgids.size(), (long long)m_groupId);
}

void CBalloonGroupChatWindow::ShowToast(LPCTSTR text)
{
    if (m_dropToast != NULL && text != NULL)
    {
        m_dropToast->Show(text);
    }
}

LRESULT CBalloonGroupChatWindow::OnChatOpenMergedRecord(UINT /*uMsg*/, WPARAM /*wParam*/,
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

LRESULT CBalloonGroupChatWindow::OnChatForwardRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM lParam, BOOL& /*bHandled*/)
{
    //多选操作条「逐条转发」「合并转发」冒泡上来：lParam 为堆上 ChatForwardRequest，处理后 delete
    std::unique_ptr<balloonchatwnd::ChatForwardRequest> pSel(
        (balloonchatwnd::ChatForwardRequest*)lParam);
    if (!pSel || pSel->m_msgids.empty())
    {
        return 0;
    }

    //调试入口（m_groupId==0，走 mock 数据）没有主面板，也没有服务端可问
    if (m_groupId == 0 || !::IsWindow(m_hostPanel))
    {
        LOG_INFO("Group chat window forward ignored in debug entry, merge=%d, count=%u.",
                 pSel->m_merge ? 1 : 0, (unsigned)pSel->m_msgids.size());
        return 0;
    }

    balloonmain::ForwardRequest* pReq = new balloonmain::ForwardRequest();
    pReq->m_srcCtype = conversation_group;
    pReq->m_srcCid   = m_groupId;
    pReq->m_srcWnd   = m_hWnd;
    pReq->m_merge    = pSel->m_merge;
    pReq->m_msgids.assign(pSel->m_msgids.begin(), pSel->m_msgids.end());
    pReq->m_skipped  = pSel->m_skipped;
    //合并转发的标题由主面板统一生成（单聊不能用含备注的显示名，两种会话放在一处处理）

    //主面板持有会话、联系人与群的全部数据，目标选择窗只能由它打开
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_FORWARD_PICKER, 0, (LPARAM)pReq))
    {
        delete pReq;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
        return 0;
    }
    LOG_INFO("Group chat window requests forward, group=%lld, merge=%d, count=%u, skipped=%u.",
             (long long)m_groupId, pSel->m_merge ? 1 : 0, (unsigned)pSel->m_msgids.size(),
             (unsigned)pSel->m_skipped);
    return 0;
}

// 输入框里 pos 之前那一个字符是不是空白（或者 pos 本身就在文本开头）。
//
// 「再 @ 一次 TA」据此决定要不要先补一个空格：发送前的重扫只认「位于文本开头、
// 或紧跟空白之后」的 '@'（见 GroupMentionModel 的 RescanMentions），直接接在
// "你好" 后面插出来的 "你好@张三 " 会被当成普通文字，@ 不生效。
//   hEdit：输入框（RichEdit）句柄。
//   pos：  光标位置（字符下标）。
// @return pos 为 0，或前一个字符是空白时返回 true。
static bool PrevCharIsWhitespace(balloonchatwnd::ChatInputRichEdit* edit, int pos)
{
    if (edit == NULL || pos <= 0)
    {
        return true;   // 已在文本开头，本来就满足重扫的要求
    }
    // 选中 [pos-1, pos) 把那一个字符读回来。内嵌对象（表情图）会读成 U+FFFC，
    // 它不是空白，于是照样补空格 —— 正是想要的。
    edit->SetSel(pos - 1, pos);
    wchar_t buf[8] = { 0 };
    edit->SendMessageToEngine(EM_GETSELTEXT, 0, (LPARAM)buf);
    return buf[0] == L' ' || buf[0] == L'\t' || buf[0] == L'\r' || buf[0] == L'\n';
}

// 把一段文字放进剪贴板。@ 菜单的「复制名字」用。
//   hOwner：剪贴板属主窗口。
//   text：  待复制的文字；空串直接返回，不清空用户原有的剪贴板内容。
static void PutMentionNameToClipboard(HWND hOwner, const CString& text)
{
    if (text.IsEmpty() || !::OpenClipboard(hOwner))
    {
        return;
    }
    ::EmptyClipboard();
    const std::size_t bytes = ((std::size_t)text.GetLength() + 1) * sizeof(WCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != NULL)
    {
        void* p = ::GlobalLock(hMem);
        if (p != NULL)
        {
            ::memcpy(p, (LPCWSTR)text, bytes);
            ::GlobalUnlock(hMem);
            //交给剪贴板之后所有权即归系统，不能再 GlobalFree。
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

bool CBalloonGroupChatWindow::IsMemberInGroupThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL)
    {
        return true;   // 拿不到窗口就别平白置灰菜单项
    }
    return self->m_memberInfo.find(uid) != self->m_memberInfo.end();
}

bool CBalloonGroupChatWindow::StarredThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL || self->m_hostPanel == NULL || !::IsWindow(self->m_hostPanel))
    {
        return false;   // 拿不到主面板（调试入口）：按"未关注"处理
    }

    //同步问主面板要结果 —— 两者都在 UI 线程上，SendMessage 就是一次直接调用，
    //不会阻塞；用 PostMessage 则既拿不到返回值，&uid 指向的栈变量也早没了。
    const int64_t target = (int64_t)uid;
    return ::SendMessage(self->m_hostPanel, FMG_MSG_QUERY_STARRED, 0, (LPARAM)&target) != 0;
}

bool CBalloonGroupChatWindow::GroupFollowThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL || self->m_hostPanel == NULL || !::IsWindow(self->m_hostPanel))
    {
        return false;   // 拿不到主面板（调试入口）：按"未关注"处理
    }

    //同步问主面板：两者都在 UI 线程上，负载放在本函数的栈上即可
    balloonchatwnd::GroupFollowTarget target;
    target.m_groupId = self->m_groupId;
    target.m_userId  = (int64_t)uid;
    return ::SendMessage(self->m_hostPanel, FMG_MSG_QUERY_GROUP_FOLLOW, 0, (LPARAM)&target) != 0;
}

int CBalloonGroupChatWindow::MemberRoleThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL)
    {
        return -1;   // 拿不到窗口：按"角色未知"处理，管理项不出现
    }
    std::map<int64_t, MemberInfo>::const_iterator it = self->m_memberInfo.find(uid);
    if (it == self->m_memberInfo.end())
    {
        return -1;   // 不在本群成员表里
    }
    return it->second.m_role;
}

LRESULT CBalloonGroupChatWindow::OnChatLoadOlder(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_groupId == 0)
    {
        return 0;   // 调试入口没有真实会话，无历史可翻
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
        2, m_groupId, m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("群聊窗请求更早一页历史: group=%lld, before=%lld, limit=%d.",
             (long long)m_groupId, (long long)m_paging.m_oldestMsgid,
             balloonchatwnd::kHistoryPageSize);
    return 0;
}

// 引用条的配色。人名用主题橙（与气泡里引用块的人名同色），摘要用灰 —— 一眼分清
// "回复谁"和"回复的是什么内容"。
static const COLORREF kReplyNameColor   = RGB(0x3A, 0x86, 0xD9);   // 人名：蓝，与截图一致
static const COLORREF kReplyDigestColor = RGB(0x6B, 0x6B, 0x6B);   // 被引用内容：中灰

void CBalloonGroupChatWindow::RefreshReplyBar()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* bar = content->FindCtrlById(balloonchatwnd::kGIdReplyBar);
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
            content->FindCtrlById(balloonchatwnd::kGIdReplyName));
        balloonwjui::DuiLabel* digestLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdReplyDigest));

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
    // 卡片占一块固定高度，显隐会改变输入区块的内部排布，必须重排。
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

void CBalloonGroupChatWindow::CancelPendingReply()
{
    m_pendingQuote = net::ReplyQuote();
    RefreshReplyBar();
}

LRESULT CBalloonGroupChatWindow::OnChatLocateQuoted(UINT /*uMsg*/, WPARAM /*wParam*/,
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
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    // 原消息不在已加载范围内时 ScrollToMsgid 返回 false —— 本期到此为止，
    // 「自动回拉历史直到那一条出现」属于后续阶段。
    const bool located = list->ScrollToMsgid(msgid);
    LOG_INFO("群聊窗定位被引用消息: msgid=%lld, 命中=%d.", msgid, located ? 1 : 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatReplyRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
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
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    balloonchatwnd::ChatMessage src;
    if (!list->GetMessageByMsgid(msgid, src))
    {
        return 0;   // 该行已不在列表里（被删 / 尚未加载），安静忽略
    }

    // 发送人显示名：群聊取群名片优先的成员映射，查不到回退"用户<id>"。快照在此刻定格，
    // 群名片之后再改也不影响已发出的引用 —— 这正是存快照而不是只存 msgid 的意义。
    CString senderName;
    std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find(src.senderUserId);
    if (it != m_memberInfo.end())
    {
        senderName = it->second.m_name;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(TR(L"用户%lld"), (long long)src.senderUserId);
    }

    // 被引用的若是图片，把原图的 fileid / md5 一并带上 —— 收方据此在引用块里画
    // 缩略图。ChatMessage 上这两项由收图时记下（见 imageFileId / imageMd5 的说明）。
    m_pendingQuote = balloonchatwnd::BuildQuoteFromMessage(
        src, senderName, src.imageFileId, src.imageMd5);
    RefreshReplyBar();
    LOG_INFO("群聊窗挂起引用回复: quote.msgid=%lld, valid=%d.",
             msgid, m_pendingQuote.m_valid ? 1 : 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatImageAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    //图片气泡右键菜单冒泡上来：lParam 为堆上 ChatImageActionData，处理后 delete。
    balloonchatwnd::ChatImageActionData* d = (balloonchatwnd::ChatImageActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    //菜单只在图片就绪时才给这两项，正常不会拿到空路径；真拿到就是状态判定出了岔子，
    //记一条日志便于回查，不再往下走。
    if (d->imagePath.empty())
    {
        LOG_ERROR("群聊窗图片菜单动作缺本地路径: group=%lld, action=%d.",
                  (long long)m_groupId, (int)d->id);
        delete d;
        return 0;
    }

    switch (d->id)
    {
    case balloonchatwnd::kChatMsgMenuSaveImageAs:
        //弹保存对话框 + 按原字节拷贝，与单聊窗共用同一份实现。
        balloonchatwnd::SaveChatImageAs(m_hWnd, d->imagePath, d->saveName);
        break;

    case balloonchatwnd::kChatMsgMenuResendImage:
        //再次发送 = 把这张图作为一条<u>新</u>图片消息发到本群，与选图 / 截图发送
        //共用同一条链路（新的 clientMsgId、重新上传），不是重发原来那条。
        LOG_INFO("群聊窗再次发送图片: group=%lld.", (long long)m_groupId);
        SendImageFile(d->imagePath);
        break;

    //图片文字识别：前置判断（系统不支持、没有识别语言时提示）后打开识别结果窗口
    case balloonchatwnd::kChatMsgMenuOcr:
        LOG_INFO("group chat window: ocr requested for an image, group=%lld.", (long long)m_groupId);
        ocr::LaunchOcrForFile(m_hWnd, d->imagePath, 0);
        break;

    default:
        LOG_ERROR("群聊窗收到未知的图片菜单动作: action=%d.", (int)d->id);
        break;
    }

    delete d;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatFileAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM lParam, BOOL& /*bHandled*/)
{
    //文件气泡按钮点击冒泡上来：lParam 为堆上 ChatFileActionData，处理后 delete
    std::unique_ptr<balloonchatwnd::ChatFileActionData> d(
        (balloonchatwnd::ChatFileActionData*)lParam);
    if (!d)
    {
        return 0;
    }

    //打开文件、打开所在文件夹、另存为与会话无关，与单聊窗共用同一份处理
    if (balloonchatwnd::HandleLocalFileAction(m_hWnd, *d))
    {
        return 0;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;

    switch (d->action)
    {
    //「下载」与失败 / 取消后的「重新下载」：从服务器取文件，群里没有接收回执要发。本人发出、被服务端拒绝的文件消息
    //（2026-10-05 起，经「发送到会话」发出的录像等）点「重新发送」时重发文件消息，不走下载
    case balloonchatwnd::kChatFileAccept:
    case balloonchatwnd::kChatFileRetry:
    {
        const balloonchatwnd::GroupFileRetryKind kind =
            balloonchatwnd::ChooseGroupFileRetry(d->fromMe, (long long)d->msgid, d->fileId);
        if (kind == balloonchatwnd::group_file_retry_resend)
        {
            ResendGroupFileMessage(*d);
        }
        else if (kind == balloonchatwnd::group_file_retry_download)
        {
            balloonchatwnd::StartChatFileDownload(*d, list);
        }
        else
        {
            LOG_INFO("group chat window: file retry ignored, group=%lld, fromme=%d, msgid=%lld.", (long long)m_groupId,
                     d->fromMe ? 1 : 0, (long long)d->msgid);
        }
        break;
    }

    //取消下载：只置位取消标志，气泡等 FileTransfer 回投结果后再置「已取消」态，
    //避免这里先改状态、传输线程稍后又回投一个失败态覆盖掉
    case balloonchatwnd::kChatFileCancel:
        if (d->msgid > 0)
        {
            LOG_INFO("group chat window cancels file download: group=%lld, msgid=%lld.",
                     (long long)m_groupId, (long long)d->msgid);
            net::FileTransfer::GetInstance().CancelDownload(d->msgid);
        }
        break;

    //「拒绝」只在单聊待接收态出现，群聊气泡不会给出；其余未知动作同样忽略
    default:
        LOG_ERROR("group chat window got unexpected file action: group=%lld, action=%d.",
                  (long long)m_groupId, d->action);
        break;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatMentionAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                     LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡里 @某人 的右键菜单选中项冒泡上来：lParam 为堆上负载，处理后 delete。
    balloonchatwnd::MentionActionData* pData = (balloonchatwnd::MentionActionData*)lParam;
    if (pData == NULL)
    {
        return 0;
    }
    const balloonchatwnd::MentionMenuId action = pData->id;
    const long long    uid  = pData->uid;
    const CString      name = pData->displayName.c_str();
    delete pData;

    LOG_INFO("群聊窗 @ 菜单动作: group=%lld, uid=%lld, action=%d.",
             (long long)m_groupId, (long long)uid, (int)action);

    switch (action)
    {
    case balloonchatwnd::kMentionMenuChat:
        //打开与该成员的单聊窗。本窗口不持有会话管理，交主面板统一路由（与聊天窗
        //头部点击打开资料窗是同一条路子）。
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CHAT, (WPARAM)uid, 0);
        }
        break;
    case balloonchatwnd::kMentionMenuProfile:
        //打开该成员的联系人资料窗，同样经主面板路由。
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CONTACT_PROFILE, (WPARAM)uid, 0);
        }
        break;
    case balloonchatwnd::kMentionMenuMentionMore:
        //在输入框光标处插入「@显示名 」，并把这一次 @ 记进待发候选。走的是
        //InsertMentionAtCaret 而不是 InsertMention —— 后者要替换用户已键入的
        //`@前缀`、依赖 m_mentionAtPos，而从气泡右键进来时用户根本没打过 '@'。
        InsertMentionAtCaret(uid, name);
        break;
    case balloonchatwnd::kMentionMenuCopyName:
        PutMentionNameToClipboard(m_hWnd, name);
        break;
    case balloonchatwnd::kMentionMenuStarred:
        //标记 / 取消特别关注。名单在主面板手上（发协议、乐观更新、失败回滚都在那边），
        //本窗口只把"用户点了这一项"转过去；目标状态由主面板按现状取反，故不用传。
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_TOGGLE_STARRED, (WPARAM)uid, 0);
        }
        break;
    //关注 / 取消关注 TA 在本群的发言：名单在主面板手上，由它按现状取反后发 cmd_set_group_follow
    //（docs/消息提醒模式与消息盒子方案.md §12）
    case balloonchatwnd::kMentionMenuGroupFollow:
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            balloonchatwnd::GroupFollowTarget* pTarget = new balloonchatwnd::GroupFollowTarget();
            pTarget->m_groupId = m_groupId;
            pTarget->m_userId  = (int64_t)uid;
            if (!::PostMessage(m_hostPanel, FMG_MSG_TOGGLE_GROUP_FOLLOW, 0, (LPARAM)pTarget))
            {
                delete pTarget;
            }
        }
        break;
    case balloonchatwnd::kMentionMenuSetAdmin:
        //设为管理员。菜单只对群主出现，服务端还会再校验一次（仅群主可设），
        //本端不预改界面 —— 等 cmd_group_member_changed_notify 推回来统一刷新。
        LOG_INFO("请求设为管理员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        CIUSocket::GetInstance().SendSetMemberRole(m_groupId, uid, true);
        break;
    case balloonchatwnd::kMentionMenuCancelAdmin:
        LOG_INFO("请求撤销管理员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        CIUSocket::GetInstance().SendSetMemberRole(m_groupId, uid, false);
        break;
    //转让群主：菜单只对群主、且目标是本群其他成员时出现。本窗口弹确认框，用户确认后交给主面板
    //发协议并登记等待应答，结果（成功的轻提示、失败的原因）由主面板给出
    case balloonchatwnd::kMentionMenuTransferOwner:
    {
        const std::wstring tip = balloonmain::BuildTransferConfirmText((LPCTSTR)name, false);
        if (::MessageBox(m_hWnd, tip.c_str(), TR(L"转让群主"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            break;
        }
        if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
        {
            LOG_WARNING("Transfer owner from chat window ignored: no host panel, group=%lld.",
                        (long long)m_groupId);
            break;
        }
        balloonmain::TransferOwnerRequest* req = new balloonmain::TransferOwnerRequest();
        req->m_groupId = m_groupId;
        req->m_newOwner = uid;
        req->m_newOwnerName = (LPCTSTR)name;
        req->m_thenLeave = false;
        if (!::PostMessage(m_hostPanel, FMG_MSG_TRANSFER_GROUP_OWNER, 0, (LPARAM)req))
        {
            delete req;
            LOG_ERROR("Post transfer owner request failed: group=%lld, uid=%lld.",
                      (long long)m_groupId, (long long)uid);
            break;
        }
        LOG_INFO("Transfer owner requested from chat window: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        break;
    }
    case balloonchatwnd::kMentionMenuKick:
    {
        //移出不可撤销，先确认。文案与群管理窗的「移出成员」保持一致。
        CString tip;
        tip.Format(TR(L"确定把「%s」移出本群吗？\n\n移出后该成员将无法再看到本群消息。"),
                   (LPCTSTR)name);
        if (::MessageBox(m_hWnd, tip, TR(L"移出成员"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            break;
        }
        LOG_INFO("请求移出群成员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        std::vector<int64_t> ids;
        ids.push_back(uid);
        CIUSocket::GetInstance().SendKickGroupMembers(m_groupId, ids);
        break;
    }
    default:
        break;
    }
    return 0;
}

void CBalloonGroupChatWindow::OnSendChatAckBackfill(const std::string& clientMsgId, long long msgid,
                                                    long long timestampMs)
{
    //发送成功：此时才把群会话冒泡到顶部
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //本窗口没有该 clientMsgId（是别的会话发出的）时安静忽略。
    list->BackfillSentMessage(clientMsgId, msgid, timestampMs);

    //「发起接龙」窗口发出的卡片：交给它补发「我的接龙」并关闭窗口。卡片本身没有乐观气泡，
    //靠服务端回显显示
    if (m_relayComposeDlg)
    {
        m_relayComposeDlg->OnSendAck(clientMsgId, 0, (int64_t)msgid);
    }

    //首屏渲染前发出的 Markdown 文档已入库：条件满足时重新拉最新一页，把被首屏冲掉的卡片显示出来
    if (m_mdEarlySends.OnAck(clientMsgId, true))
    {
        ReloadIfMarkdownEarlySendsDone();
    }
}

void CBalloonGroupChatWindow::ReloadIfMarkdownEarlySendsDone()
{
    if (!m_mdEarlySends.TakeReloadNeeded())
    {
        return;
    }
    LOG_INFO("group chat window: reload latest history to show markdown documents sent before the first page, "
             "group=%lld.", (long long)m_groupId);
    ReloadLatestMessages();
}

bool CBalloonGroupChatWindow::OnSendChatAckFailed(const std::string& clientMsgId, int code)
{
    //被拒绝的消息不冒泡
    m_sendBumps.Drop(clientMsgId);

    //首屏渲染前发出、被拒绝的 Markdown 文档没有入库，不再等它；其余已成功的可能因此满足重新拉取的条件
    if (m_mdEarlySends.OnAck(clientMsgId, false))
    {
        ReloadIfMarkdownEarlySendsDone();
    }

    //先问「发起接龙」窗口：它发出的卡片没有乐观气泡，由它在窗口里提示原因并保持打开
    if (m_relayComposeDlg && m_relayComposeDlg->OnSendAck(clientMsgId, code, 0))
    {
        return true;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return false;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return false;
    }
    //本窗口没有该 clientMsgId（是别的会话发出的）时返回 false，主面板接着问别的窗口
    const bool marked = list->MarkSendFailed(clientMsgId,
                                             CString(balloonchatwnd::SendFailureText(code, IsNotifyOnlyForMe()).c_str()));
    if (marked)
    {
        LOG_INFO("group chat window: send rejected, marked bubble failed, group=%lld, code=%d, clientmsgid=%s.",
                 (long long)m_groupId, code, clientMsgId.c_str());
    }
    return marked;
}

LRESULT CBalloonGroupChatWindow::OnChatReeditRequest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                     BOOL& /*bHandled*/)
{
    balloonchatwnd::ChatReeditData* data = reinterpret_cast<balloonchatwnd::ChatReeditData*>(lParam);
    if (data == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatInputRichEdit* input = (content == NULL) ? NULL
        : dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    // 代码片段：打开发送代码窗口并预填原文与语言，不放回输入框（docs/代码片段方案.md 第 3.1 节）
    if (data->m_isCode)
    {
        // 追加批注的消息（批注里有标「新」的、且引用了原消息）：打开原消息的代码查看窗并放回批注（docs/代码片段方案.md
        // 第 12 节）。原消息还在列表里时以它的批注为底，否则以这条消息里没有标「新」的批注代替
        if (data->m_codeQuoteMsgid > 0 && codesnippet::CountNewCodeNotes(data->m_codeNotes) > 0)
        {
            balloonchatwnd::ChatMessageList* codeList = (content == NULL) ? NULL
                : dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            balloonchatwnd::ChatMessage base;
            if (codeList != NULL && codeList->GetMessageByMsgid(data->m_codeQuoteMsgid, base) && base.isCode)
            {
                const long long baseSenderId = base.fromMe ? SelfUserIdForCode() : (long long)base.senderUserId;
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
        LOG_INFO("group chat window: reedit code snippet, group=%lld, chars=%u, notes=%u.", (long long)m_groupId,
                 (unsigned)data->m_code.size(), (unsigned)data->m_codeNotes.size());
        delete data;
        return 0;
    }
    // Markdown：打开 Markdown 编辑器并预填源文本，发送对象预选本群，不放回输入框（docs/Markdown消息方案.md 第 11.3 节）
    if (data->m_isMarkdown)
    {
        OpenMarkdownEditor(data->m_markdownSource);
        LOG_INFO("group chat window: reedit markdown in editor, group=%lld, bytes=%u.", (long long)m_groupId,
                 (unsigned)data->m_markdownSource.size());
        delete data;
        return 0;
    }
    if (input != NULL)
    {
        //正文里的 @ 重新登记为待发候选，发送时照常按最终文本重扫
        balloonchatwnd::ApplyReeditToInput(*data, input, &m_pendingMentions);
        input->SetFocus();
        LOG_INFO("group chat window: reedit applied, group=%lld, segments=%u, mentions=%u.",
                 (long long)m_groupId, (unsigned)data->m_segments.size(), (unsigned)data->m_mentions.size());
    }
    delete data;
    return 0;
}

void CBalloonGroupChatWindow::PostSendBump(const ConvSendBumpData& data)
{
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;
    }
    ConvSendBumpData* pBump = new ConvSendBumpData(data);
    if (!::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP, 0, (LPARAM)pBump))
    {
        delete pBump;   //投递失败（主面板已销毁）自行回收
    }
}

void CBalloonGroupChatWindow::OpenRelayCompose()
{
    if (m_groupId <= 0)
    {
        LOG_INFO("group chat window: relay compose not available in debug entry.");
        return;
    }
    if (!m_relayComposeDlg)
    {
        m_relayComposeDlg.reset(new CRelayComposeDlg());
    }
    m_relayComposeDlg->Open(m_hWnd, m_groupId);
}

std::string CBalloonGroupChatWindow::OnMessageRecalled(long long msgid, const CString& tipText, bool recalledByMe,
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return std::string();
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略。
    std::string draftJson;
    list->RecallMessageByMsgid(msgid, tipText, recalledByMe, nowMs, &draftJson);
    if (!draftJson.empty())
    {
        //提示行带上了「编辑」：定好到期定时器，5 分钟到点时让它自动消失
        RefreshRecallEditTimer();
    }
    return draftJson;
}

void CBalloonGroupChatWindow::RefreshRecallEditTimer()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return;
    }
    const long long nowMs = balloonchatwnd::RecallEditNowMs();
    const long long next = list->ExpireRecallEdits(nowMs);
    balloonchatwnd::RescheduleRecallEditTimer(m_hWnd, kRecallEditTimerId, next, nowMs);
}

void CBalloonGroupChatWindow::OnSendModeChanged()
{
    RefreshSendModeUi();
}

void CBalloonGroupChatWindow::OnMessagesDeleted(const std::vector<long long>& msgids)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //没加载进当前视图的那些行安静忽略：服务端已把它们对本人过滤掉，往上翻页也不会再出现
    const int removed = list->RemoveMessagesByMsgid(msgids);
    LOG_INFO("Group chat window removed deleted messages, group=%lld, requested=%u, removed=%d.",
             (long long)m_groupId, (unsigned)msgids.size(), removed);
}

//调用方：主面板（服务端删除了本会话的消息之后），以及本窗口的 ReloadIfMarkdownEarlySendsDone
//（首屏渲染前发出的 Markdown 文档被首屏清掉之后）
void CBalloonGroupChatWindow::ReloadLatestMessages()
{
    //调试入口没有群，也没有服务端可问
    if (m_groupId <= 0)
    {
        return;
    }

    //翻页状态回到"首屏还没到位"：应答到达时 OnHistoryMessages 据 m_loading 为假按首屏
    //处理，先清空列表再按服务端快照重新渲染；翻页锚点清零，首屏到达前不允许往上翻
    m_paging = balloonchatwnd::HistoryPagingState();
    //在新快照渲染之前发出的 Markdown 文档同样会被清掉，重新开始记录
    m_mdEarlySends.OnReloadRequested();
    CIUSocket::GetInstance().RequestSyncMessages(
        2, m_groupId, 0, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("Group chat window reloads latest history (after a server-side delete, or to show markdown documents sent before the first page), group=%lld.",
             (long long)m_groupId);
}

void CBalloonGroupChatWindow::OnMessagePinned(long long msgid, bool pinned)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略：本地库那份标记已经落好，
    //下次翻历史渲染出来时自然带着。
    list->SetPinnedByMsgid(msgid, pinned);
}

void CBalloonGroupChatWindow::SetPinnedCount(int count)
{
    m_pinnedCount = (count > 0) ? count : 0;
    RefreshPinBanner();
}

void CBalloonGroupChatWindow::AdjustPinnedCount(int delta)
{
    m_pinnedCount += delta;
    if (m_pinnedCount < 0)
    {
        m_pinnedCount = 0;
    }
    RefreshPinBanner();
}

void CBalloonGroupChatWindow::RefreshPinBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   //控件树还没建好，等 InitUi 之后主面板会再校准一次
    }

    const bool visibilityChanged =
        balloonchatwnd::UpdatePinBanner(content, balloonchatwnd::kGIdPinBanner, m_pinnedCount);

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

void CBalloonGroupChatWindow::RequestOpenHistory(int initialTypeFilter)
{
    if (m_groupId <= 0 || m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;   //调试入口下没有主面板，安静忽略
    }

    //群 id 经堆 int64_t 随消息回投，由主面板处理后 delete。会话类型与"预选哪个类型
    //胶囊"一起塞在 wParam 里（约定见 UserSessionData.h 的 FMG_MSG_OPEN_HISTORY）。
    int64_t* pCid = new int64_t(m_groupId);
    const WPARAM wp = (WPARAM)(conversation_group | (initialTypeFilter << 8));
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_HISTORY, wp, (LPARAM)pCid))
    {
        delete pCid;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
    }
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDeleteMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    delete pEntryId;

    // 调试入口（mock）无服务器，删除不可行；记日志返回。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件删除：调试入口无群绑定，忽略 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 据 entryid 在库里取完整条目（拿文件名做确认提示 + 上传者做权限复检）。
    const net::GroupFileEntry* e = m_fileLibrary.FindByEntryId(entryid);
    if (e == NULL)
    {
        LOG_ERROR("群文件删除：库中找不到 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 权限复检（与垃圾桶门控、服务端规则同一判据）：群主 / 管理员可删任意，上传者本人可删自己的。
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::string selfIdStr = (selfId != 0) ? std::to_string(selfId) : std::string();
    bool isOwnUpload = (!selfIdStr.empty() && e->m_uploaderid == selfIdStr);
    if (!balloonchatwnd::GroupFileLibrary::CanDeleteGroupFile(m_myRole, isOwnUpload))
    {
        LOG_ERROR("群文件删除：无权限(role=%d, own=%d), entryid=%s。",
                  m_myRole, isOwnUpload ? 1 : 0, entryid.c_str());
        return 0;
    }

    // 确认（删除是破坏性且影响全群，必弹确认框）。
    CString name = Utf8ToCString(e->m_name);
    CString prompt;
    prompt.Format(TR(L"确定删除群文件「%s」吗？\n删除后全群成员都将看不到此文件。"), (LPCTSTR)name);
    if (::MessageBoxW(m_hWnd, prompt, TR(L"删除群文件"), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return 0;
    }

    // 发删除请求；删成功后服务端推 1806(removed)，经 OnGroupFilesChanged 把该行移出全群面板。
    CIUSocket::GetInstance().RequestDeleteGroupFile(m_groupId, entryid);
    LOG_INFO("群文件删除已发起: group=%lld, entryid=%s, name=%s。",
             (long long)m_groupId, entryid.c_str(), e->m_name.c_str());
    return 0;
}

void CBalloonGroupChatWindow::SendLocalImage(const std::wstring& path)
{
    LOG_INFO("group chat: send local image requested by host, group=%lld.", static_cast<long long>(m_groupId));
    SendImageFile(path);
}

void CBalloonGroupChatWindow::SendLocalFile(const std::wstring& path)
{
    if (!CheckOnlineFor(balloonmain::kActionTransferFile) || m_groupId == 0 || path.empty())
    {
        return;
    }
    LOG_INFO("group chat: upload local file to group files requested by host, group=%lld.", (long long)m_groupId);
    //上传成功后还要在群里发一条文件消息，聊天里才看得到（2026-10-05 用户反馈录完屏「发送」看不到效果）
    int uploadId = 0;
    if (UploadGroupFile(path, &uploadId))
        m_chatPoster.Mark(uploadId, path);
}

bool CBalloonGroupChatWindow::PickAndUploadGroupFile()
{
    // 调试入口（无群绑定）无服务器，不支持真实上传。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件上传：调试入口无群绑定，忽略。");
        return false;
    }

    // 弹系统选文件框选一个文件（任意类型）。
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
        return false;   // 用户取消
    }

    return UploadGroupFile(szFile);
}

bool CBalloonGroupChatWindow::UploadGroupFile(const std::wstring& path, int* uploadIdOut)
{
    // 调试入口（无群绑定）无服务器，不支持真实上传。
    if (m_groupId == 0 || path.empty())
    {
        return false;
    }

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 入队上传，拿回客户端分配的 uploadId（可多笔并发，每笔一个 id）。
    int uploadId = net::FileTransfer::GetInstance().EnqueueGroupFileUpload(m_groupId, path, selfId);
    if (uploadIdOut != NULL)
        *uploadIdOut = uploadId;

    // 从路径拆出文件名与扩展名，登记进上传跟踪器，使面板顶部立刻冒出一行「上传中 0%」。
    size_t slash = path.find_last_of(L"\\/");
    std::wstring nameW = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t dot = nameW.find_last_of(L'.');
    std::wstring extW = (dot == std::wstring::npos) ? std::wstring() : nameW.substr(dot + 1);
    m_uploadTracker.Begin(uploadId, nameW, extW);
    RenderFilePanel();

    // 路径先转 UTF-8 再以 %s 记日志（含中文路径用 %ls 会触发 narrow 日志器缓冲溢出断言）。
    std::string pathUtf8 = CStringToUtf8(CString(path.c_str()));
    LOG_INFO("群文件上传已入队: group=%lld, uploadId=%d, path=%s。",
             (long long)m_groupId, uploadId, pathUtf8.c_str());
    return true;
}

LRESULT CBalloonGroupChatWindow::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_SIZE —— 仍交 DuiFrameWindow 链缩放客户区与后备缓冲。
    // 投递自定义消息：等客户区尺寸更新后再强制重排控件树。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnRelayout(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }

    // 把输入区块稳定在用户选定（或默认）的高度：窗口缩放时让消息区吃掉高度
    // 变化、输入区高度基本不变 —— 与改造前"输入区固定、消息区撑满"的手感一致。
    // 与单聊共用同一记忆。须放在下面消息区 ForceLayout 之前。
    balloonwjui::DuiSplitter* sp = dynamic_cast<balloonwjui::DuiSplitter*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputSplitter));
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
        content->FindCtrlById(balloonchatwnd::kGIdMessageArea);
    if (msgArea != NULL)
    {
        msgArea->ForceLayout(msgArea->GetRect());
    }

    // 「问答」面板开着且宽度变了：卡片高度随宽度变化，按新宽度重建
    balloonwjui::DuiControl* qaList = content->FindCtrlById(balloonchatwnd::kGIdQaList);
    if (qaList != NULL && qaList->IsVisible())
    {
        const RECT rcQa = qaList->GetRect();
        if (rcQa.right - rcQa.left != m_qaRenderedWidth)
        {
            RenderQaPanel();
        }
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}
