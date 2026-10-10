/**
 *  ChatBubble.cpp
 *
 *  单聊窗消息行的绘制实现。形状（头像 / 气泡 / 引用块 / 图片占位 / 文件
 *  类型块）用 GDI+ 抗锯齿；文字用 GDI DrawText（清晰）。文字气泡支持
 *  自动折行：超过气泡最大宽度的长文字在气泡内换行、气泡随之变高。
 *  本行还支持右键上下文菜单与多选模式（详见 ChatBubble.h）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <atlgdi.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatBubble.h"
#include "ChatImageCache.h"    // 图片后台解码 + 缩放缓存（大图首次显示不冻结 UI）
#include "../../Common/Log/IULog.h"          // 引用缩略图的诊断日志
#include "ChatIcons.h"
#include "ChatFileVisual.h"   // FileTypeColor（与消息记录窗共用同一套配色）
#include "ChatFileCard.h"     // 文件卡片的尺寸、绘制与点击判断（与聊天记录查看窗共用）
#include "ChatPaintUtil.h"    // AddRoundRect / DrawGdiText / MeasureTextWidth（与文件卡片共用）
#include "ChatMessageList.h"
#include "ChatFileAction.h"    // ChatFileActionKind / ChatFileActionData（文件气泡按钮动作负载）
#include "ChatFolderAction.h"  // ChatFolderActionKind / ChatFolderActionData（文件夹气泡按钮动作负载）
#include "ChatFaceTag.h"       // kFaceDisplayPx（内联表情显示边长）
#include "ChatInlineText.h"    // 「文字 + 内联表情」的排版与绘制（与消息记录窗共用）
#include "ChatTextSelection.h" // 选中文字的纯逻辑（命中测试 / 选区矩形 / 取文本）
#include "GroupMentionMenu.h"  // @某人 片段的右键菜单模型与投递负载
#include "ChatMessageMenu.h"   // 气泡自身右键菜单的行模型与投递负载（含图片两项）
#include "../../Feature/Ocr/OcrLauncher.h"   // IsOcrEntryVisible：图片菜单里「图片文字识别」是否出现
#include "ImagePlaceholder.h"  // 图片无法显示时的破图占位符（排版、失败判断与绘制）
#include "ChatReedit.h"        // 「重新编辑」的可用判定与投递负载（docs/敏感词方案.md 8.4）
#include "RecallEdit.h"        // 撤回提示行的「编辑」：快照解析与到期判断（docs/撤回后重新编辑方案.md）
#include "ChatMemoPrefill.h"   // ChatAddToMemoRequest：「添加到备忘」投给宿主的负载（docs/备忘与提醒方案.md §8）
#include "ChatCodeMessage.h"   // SaveCodeWithDialog：代码卡片右键「另存为」
#include "ChatGroupInviteCard.h" // GroupInviteButtonText：入群邀请卡片的按钮文字（docs/邀请入群方案.md §3.3）
#include "ChatSecretMode.h"      // SecretInviteButtonText / SecretInviteButtonAction：密聊邀请卡片的按钮（docs/密聊方案.md 4.8）
#include "ChatVoiceBubble.h"     // 语音气泡的宽度、时长文字、红点条件与点击动作（docs/语音消息方案.md 第 3.2 节）
#include "ChatVoicePaint.h"      // 语音条的绘制（与消息记录窗、合并记录查看窗的语音条控件共用，第 4 期）
#include "ChatFileStateModel.h" // kFileFailed（发送失败时把文件行置为失败态）
#include "ChatDirectFileModel.h" // FolderBubbleShowsP2PBadge：文件夹气泡的「P2P」徽章（2026-10-04 起）；DirectFileForwardBlocked：右键转发在线直传文件
#include "../BalloonMain/P2PBadge.h"   // ShouldShowP2PBadge：链路是否直连
#include "ChatClipboardIo.h"   // 复制时写入纯文字 + Flamingo 格式（粘回输入框能还原表情、图片）
#include "ChatDragSource.h"    // 把选中的文字与表情、整张图片拖出去
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 图钉的「取消 Pin」悬停提示
#include "../../../third-party/balloonui/balloonui/DuiTrace.h"   // BUI_TRACE：各行实际渲染耗时的时序跟踪
#include "../../Common/Util/PerfStopWatch.h"                      // 渲染计时
#include "ChatAvatarBitmapCache.h"   // 发送者真图头像的共享位图缓存（同一头像只解码、缩放一次）
#include "ChatRelayCard.h"           // RelayCardButton、RelayCopyTextForMessage（接龙卡片的按钮与复制文字）
#include "ChatFileShareCard.h"       // ShareCardButtonLabel（共享卡片的按钮文字）
#include "ChatScreenShareCard.h"     // ScreenShareJoinRequest（屏幕共享卡片「加入」随消息投递的堆对象）
#include "ChatMarkdown.h"            // Markdown 消息：由正文取源文本、图片转链接的前缀（卡片见 docs/Markdown消息方案.md 第 11.3 节）
#include "ChatMarkdownCard.h"        // Markdown 卡片的宽度、标题与摘要、高度与内容绘制（与独立卡片控件共用）
#include "../../net/Protocol.h"      // share_kind（共享卡片按类型选图标）
#include "../../Common/Graphics/ShellTypeIcon.h"   // 文件共享卡片标题前的本机系统类型图标
#include "../../App/CustomMsgDef.h"   // WM_VIEW_CHAT_IMAGE / WM_CHAT_FILE_ACTION / WM_CHAT_IMAGE_ACTION
#include "../BalloonChat/BalloonChatMsgFormat.h"  // ParseSendBody / ChatSegment（解析内联表情）
#include "../../net/MessageProtocolClient.h"  // canRecallMessage（右键菜单里"撤回"项的时限判断）
#include <ctime>               // time —— 取当前时刻做撤回时限比较
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

#include <map>
#include <algorithm>   // std::min / std::max（选区变化区间）
#include <utility>     // std::pair / std::make_pair
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../BalloonMain/AvatarGray.h"   // MakeGrayColor：离线头像的灰色底
#include "../BalloonMain/AvatarGlyph.h"  // DrawCenteredGlyphGdi：头像字居中

namespace balloonchatwnd {

//圆角矩形、GDI 文字与量宽三个工具函数与文件卡片共用一份实现（见 ChatPaintUtil.h）
using chatpaint::AddRoundRect;
using chatpaint::DrawGdiText;
using chatpaint::MeasureTextWidth;

namespace {

// ---- 几何常量（逻辑像素）---------------------------------------------

const int kTimeRowHeight  = 38;   // 时间分隔行高
const int kTimeTextPx     = 11;   // 时间分隔行与系统提示行的字号
const int kAvatarSize     = 34;   // 头像边长
const int kEdgePad        = 16;   // 头像距窗口边
const int kAvatarGap      = 10;   // 头像与气泡列的间距
const int kRowVMargin     = 7;    // 行上下外边距
const int kTextBubbleH    = 36;   // 单行文字气泡高（折行测量失败时的回退）
const int kEmojiBubbleH   = 46;   // emoji 气泡高
const int kFileBubbleH    = 58;   // 文件气泡高
const int kImageBubblePad = 8;    // 图片气泡内边距（4 每侧）
// 引用块（气泡内部顶端那一块，标明本条回复的是谁的哪句话）。
// 早先它画在气泡外面、只有一行；现按设计稿挪进气泡内部并改成两行 —— 第一行人名、
// 第二行被引用内容，左侧一条竖线标明这是引用。
const int kQuoteLineH     = 17;   // 引用块内每行文字的行高
const int kQuoteBlockH    = kQuoteLineH * 2 + 4;   // 引用块总高（两行 + 行间距）
const int kQuoteGap       = 6;    // 引用块与其下方正文之间的间距
const int kQuoteBarW      = 3;    // 左侧竖线宽
const int kQuoteBarGap    = 7;    // 竖线与文字之间的间距
const int kQuoteThumbSize = kQuoteBlockH;   // 引用图片消息时缩略图的边长（与引用块等高）
const int kQuoteThumbGap  = 8;              // 缩略图与右侧文字的间距
const int kQuoteMinTextW  = 120;            // 引用块内文字至少留出的宽度（保证人名 / 摘要看得见）
const int kSenderNameHeight = 17; // 群聊气泡上方发送者名行高
const int kReceiptHeight  = 18;   // "已读"回执行高
const int kReceiptTopGap  = 2;    // 回执行与气泡底边的间距
const int kReceiptTextPx  = 10;   // 回执文字字号（像素）
const int kReceiptCheckSize      = 11;   // "已读"前面那个勾的边长
const int kReceiptCheckGap       = 3;    // 勾与"已读"文字之间的间距
const int kReceiptCheckTopOffset = 2;    // 勾相对回执行顶边的下移量，使其与文字垂直对齐
const Gdiplus::REAL kReceiptCheckStroke = 2.0f;   // 勾的线宽

// 文字气泡内边距 / 折行布局参数。
const int    kTextPadH = 12;            // 文字气泡水平内边距（每侧）
const int    kTextPadV = 9;             // 文字气泡垂直内边距（每侧）
const int    kMinTextWidth = 40;        // 折行测量时文字区最小宽度
const double kBubbleMaxWidthRatio = 0.62;  // 气泡最大宽 = 内容区宽 × 此比例

// 超长气泡分段缓存的默认阈值（像素）：行高超过它的文字气泡只缓存可见区附近的一段。取 2048：
// 单聊窗消息区四五百像素高，加上下各一段余量（kTallSliceMarginPx）后的缓存段约一千五百像素，低于阈值；
// 而普通消息远不到这个高度，行为与改动前完全相同。
const int kDefaultTallSliceThresholdPx = 2048;

// 超长气泡分段缓存时，缓存段在要画的部分上下各多画的余量（像素）。滚动不超过这段距离时直接用缓存，
// 不必重画；取约一屏多一点，缓存段总高约为可视区高度加上两段余量
const int kTallSliceMarginPx = 512;

// 多选模式：行首勾选列。
const int kCheckColW = 30;        // 勾选列宽（内容整体右移这么多）
const int kCheckSize = 18;        // 勾选圈直径

const Gdiplus::REAL kBubbleRadius = 12.0f;   // 气泡圆角
const Gdiplus::REAL kAvatarRadius = 7.0f;    // 头像圆角

// ---- 配色 --------------------------------------------------------------

const Gdiplus::Color kBubbleOther(255, 0xF3, 0xF2, 0xEF);  // 对方气泡底（浅灰）
const Gdiplus::Color kBubbleSelf (255, 0xFF, 0xE6, 0xD8);  // 自己气泡底（浅橙）
const Gdiplus::Color kQuoteFill  (255, 0xEF, 0xEE, 0xEB);  // 引用块底
const Gdiplus::Color kAccent     (255, 0xFF, 0x7A, 0x45);  // 主题橙
const Gdiplus::Color kCheckRing  (255, 0xC2, 0xC0, 0xBB);  // 未选中勾选圈描边
const COLORREF kBubbleText  = RGB(0x2A, 0x2A, 0x2A);       // 气泡文字
const COLORREF kQuoteName   = RGB(0xFF, 0x7A, 0x45);       // 引用人名
const COLORREF kQuoteText   = RGB(0x8A, 0x8A, 0x8A);       // 引用内容
const COLORREF kTimeColor   = RGB(0xA8, 0xA6, 0xA2);       // 时间分隔文字
const int kRecallEditGapPx  = 6;    // 撤回提示行里提示文字与「编辑」之间的间距（像素）
const COLORREF kReceiptText = RGB(0xA8, 0xA6, 0xA2);       // "已读"文字
const COLORREF kReceiptUnreadText = RGB(0xFF, 0x7A, 0x45); // "未读"文字（主题橙，与灰色的"已读"区分）

// ---- 发送失败（服务端拒绝，见 docs/敏感词方案.md 8.3）----
const int kFailMarkSize   = 16;   // 气泡左侧红色圆形感叹号的直径
const int kFailMarkGap    = 6;    // 感叹号与气泡左边之间的水平间距
const int kFailMarkTextPx = 12;   // 感叹号里「!」的字号（像素）
const int kFailReasonHeight = kReceiptHeight;   // 原因行高：与回执行同高，单聊里正好占用回执行的位置
const COLORREF kFailMarkText = RGB(0xFF, 0xFF, 0xFF);   // 感叹号里「!」的颜色
const COLORREF kAvatarText  = RGB(255, 255, 255);          // 头像字母

// 气泡内文字 / emoji 字号。
const int kBubbleTextPx = 14;
const int kEmojiPx      = 30;

// ---- @某人 右键菜单的尺寸估值（逻辑像素）------------------------------
//
// Win32 的 HMENU 在真正弹出之前拿不到精确尺寸，而落点夹取又必须在弹出<u>之前</u>
// 算好，故这里按"每项一行"估一个够用的尺寸。估大不会误伤：夹取只在放不下时才
// 改落点，估大顶多让菜单在靠近屏幕边缘时更早翻向左 / 上，不会让正常位置跑偏。
const int kMentionMenuEstWidth      = 140;   // 菜单估计宽（最长项"取消特别关注"六个汉字约 100px + 边距）
const int kMentionMenuEstItemHeight = 26;    // 每项估计高
const int kMessageMenuEstWidth      = 130;   // 气泡自身右键菜单的估计宽（最长项"图片另存为"约 100px + 边距）

// 注：气泡自身右键菜单的命令 id 与行模型已挪到 ChatMessageMenu.h
//（enum ChatMessageMenuId / BuildChatMessageMenuModel），那边是纯函数、有单测。

// ---- Pin 标记（被钉住的消息在气泡外侧上角画一枚小图钉）----
const int kPinIconSize = 14;   // 图钉边长（逻辑像素）
const int kPinIconGap  = 6;    // 图钉与气泡边之间的水平间距
// 暖琥珀色：比正文显眼，又不像红色那样读作"出错了"。
const Gdiplus::Color kPinIconColor(255, 0xE6, 0x8A, 0x2E);
// 图钉可点击区域在图标四周各外扩的像素数。图标只有 14px，不外扩的话很难点中。
// 取值须小于 kPinIconGap，点击区域才不会伸进气泡、截走点在气泡边缘的操作。
const int kPinHitPad = 4;
// 鼠标悬停在图钉上时的提示文字：点击图钉就是取消 Pin。
const wchar_t* const kPinTooltipText = TR_NOOP(L"取消 Pin");

// 注：文件类型色块配色已挪到 ChatFileVisual.h / .cpp —— 「消息记录」窗的列表行
// 也要画这个色块，同一个 .xlsx 在两处得是同一个绿，故抽出来共用。

// ---- 文件气泡（kMsgFile）几何与状态布局 --------------------------------

// 注：文件态取值（kFileNormal / kFileUploading / ... / kFileRejected）与「状态 → 可用
// 操作」的映射（FileStateHasProgress / FileStateButtonCount / FileButtonActions）已抽到
// ChatFileStateModel.h/.cpp，那是可单元测试的纯逻辑；本文件经 ChatFileAction.h 取用。

// 文件卡片的几何常量、按状态算高度（FileCardHeight）、是否省掉「拒绝」（FileCardSkipsReject）
// 与绘制都在 ChatFileCard.h/.cpp，与聊天记录查看窗共用一份。

// ---- 文件夹气泡（kMsgFolder）几何与状态布局 ----------------------------
//
// 注：文件夹传输状态取值（kFolderInviting / kFolderTransferring / ... / kFolderUserCanceled）
// 与「状态 → 可用操作」的映射（FolderStateHasProgress / FolderStateButtonCount /
// FolderButtonActions / FolderRetryEnabled / FolderErrorText）已在 ChatFolderStateModel.h/.cpp，
// 那是可单元测试的纯逻辑；本文件经 ChatFolderAction.h 取用，绝不在绘制代码里另写一份状态判断。

// 文件夹气泡几何常量（逻辑像素）。整体沿用文件气泡那一套，只有下面三处不同：
//   · 气泡更宽（344 而非 300）—— 状态行要放下"正在传输 <文件名>（第 i / 共 n 个）"，
//     实测 320 宽时这一串在文件名之后就被省略号截断，"共 n 个"看不见了；
//   · 头部之下多一行状态行（当前文件名、第 i / 共 n 个），传输中才有内容；
//   · 失败态再多一行失败原因。
const int kFolderBubbleW     = 344;  // 文件夹气泡固定宽
const int kFolderPadH        = 14;   // 气泡水平内边距（每侧）
const int kFolderIconSize    = 40;   // 图标底块边长（圆角方块，与文件气泡的类型块同尺寸）
const int kFolderIconTop     = 10;   // 图标底块距气泡顶
const int kFolderGlyphSize   = 24;   // 底块内文件夹图标的绘制边长（kChatIconFolder 按 viewBox 24 缩放到此尺寸）
const int kFolderNameTop     = 12;   // 文件夹名距气泡顶
const int kFolderNameH       = 20;   // 文件夹名行高
const int kFolderSummaryTop  = 34;   // 摘要行（文件数 / 目录数 / 总大小）距气泡顶
const int kFolderSummaryH    = 18;   // 摘要行行高
const int kFolderTextGap     = 12;   // 图标底块与右侧文字之间的间距
const int kFolderHeaderH     = 60;   // 头部区高（图标 + 文件夹名 + 摘要行）
const int kFolderProgressH   = 5;    // 进度条厚
const int kFolderStatusTopGap = 8;   // 状态行距上方（进度条或头部）
const int kFolderStatusH     = 18;   // 状态行行高
const int kFolderPctW        = 44;   // 状态行右端为百分比预留的宽度。百分比右对齐画在这一格里，
                                     // 左侧的状态文字相应收窄并以省略号收尾，两者永不叠字
                                     // （"100%" 在 11px 字号下约 28px，余量给中文括号留出空隙）
const int kFolderCounterGap  = 8;    // 状态行主文字与右侧"第 i / 共 n 个"计数段之间的间距
const int kFolderErrorTopGap = 4;    // 失败原因行距状态行
const int kFolderErrorH      = 18;   // 失败原因行行高
const int kFolderBtnTopGap   = 8;    // 按钮行距上方
const int kFolderBtnH        = 30;   // 按钮行高（同时是按钮的命中矩形高）
const int kFolderBtnBottomPad = 10;  // 按钮行下边距
const int kFolderLinkPx      = 13;   // 底部操作链接文字字号
const int kFolderLinkGap     = 18;   // 相邻操作链接的水平间距

// 文件夹图标底块的底色（暖琥珀）。文件气泡按扩展名取色（见 ChatFileVisual），文件夹
// 没有扩展名可言，故固定一色；选暖琥珀是因为它与主题橙同色系但更沉，两种气泡并排时
// 一眼能分出"这是一个文件夹"而不是又一个文件。
const COLORREF kFolderBlockColor = RGB(0xE8, 0x9C, 0x3A);

// 本状态下是否要单独画一行失败原因。
// 只对"没有正常传完"的几种终态给这一行，且要求宿主确实带回了错误码 —— errcode 为 0
// 时 FolderErrorText 只会返回一句兜底文案，画出来等于占着一行说废话。
// 发送端在应答前主动撤回邀请（kFolderInviteCanceled）不算失败，不给这一行。
//   state：   传输状态，取值见 ChatFolderState
//   errcode： 失败原因错误码（error_code_folder_*），0=宿主未记录
bool FolderBubbleHasErrorLine(int state, int errcode)
{
    if (errcode == 0)
    {
        return false;
    }
    return state == kFolderFailed
        || state == kFolderExpired
        || state == kFolderUserCanceled
        || state == kFolderRejected
        || state == kFolderPeerOffline
        || state == kFolderTimeout;
}

// 文件夹气泡总高（随状态变化：有无进度条 / 失败原因行 / 按钮行）。
// 三处可变项<u>必须</u>与 RenderContent 里排布这三块时问的是同一批函数，否则会出现
// 「按钮画出来了却被裁在气泡外面点不到」这种错位——文件气泡的注释里说的就是这件事。
// 按钮全部排在<u>同一行</u>内（RenderContent 里按各自文字宽度从左往右顺排），故这里只按
// 「有没有按钮」加一次行高，与按钮具体有几个无关；2026-08-14 把邀请态接收端从两个按钮
// 加到三个（接收 / 另存为 / 拒绝）时，高度因此不必改动。
//   state：   传输状态，取值见 ChatFolderState
//   fromMe：  本行是否本端发起（影响按钮数：接收端在邀请态有三个、失败态一个都没有）
//   errcode： 失败原因错误码，决定要不要留出失败原因那一行
int FolderBubbleHeight(int state, bool fromMe, int errcode)
{
    int h = kFolderHeaderH;
    if (FolderStateHasProgress(state))
    {
        h += kFolderProgressH;
    }
    //状态行任何状态都有（等待应答 / 正在创建目录 / 正在传输 / 已完成 / 已取消……），
    //故不设条件；它是用户判断"这次传输眼下到哪一步了"的唯一一行字。
    h += kFolderStatusTopGap + kFolderStatusH;
    if (FolderBubbleHasErrorLine(state, errcode))
    {
        h += kFolderErrorTopGap + kFolderErrorH;
    }
    if (FolderStateButtonCount(state, fromMe) > 0)
    {
        h += kFolderBtnTopGap + kFolderBtnH + kFolderBtnBottomPad;
    }
    return h;
}

// ---- 合并转发聊天记录卡片（kMsgMerged）几何 ------------------------------
//
// 版式自上而下：标题一行（粗体）、最多 kMergedCardPreviewLines 行「发送者：摘要」（灰）、
// 一条分隔线、底部「聊天记录（共 N 条）」（小号灰字）。卡片内容由 ChatMergedCard 解析，
// 本文件只管画。整张卡片可点，点击打开聊天记录查看窗。
const int kMergedBubbleW     = 280;  // 卡片固定宽
const int kMergedPadH        = 14;   // 水平内边距（每侧）
const int kMergedTitleTop    = 10;   // 标题距卡片顶
const int kMergedTitleH      = 22;   // 标题行高
const int kMergedTitlePx     = 14;   // 标题字号
const int kMergedLinesGap    = 4;    // 标题与第一行预览之间的间距
const int kMergedLineH       = 18;   // 每行预览的行高
const int kMergedLinePx      = 12;   // 预览字号
const int kMergedFooterGap   = 8;    // 最后一行预览与分隔线之间的间距
const int kMergedFooterH     = 28;   // 分隔线下方底部文字行的高度
const int kMergedFooterPx    = 11;   // 底部文字字号
const COLORREF kMergedLineColor   = RGB(0x8A, 0x8A, 0x8A);   // 预览与底部文字的灰色
const Gdiplus::Color kMergedDivider(255, 0xE2, 0xDE, 0xDA);  // 分隔线颜色

// 合并卡片的总高：随预览行数变化（0 ~ kMergedCardPreviewLines 行）。
int MergedBubbleHeight(size_t lineCount)
{
    return kMergedTitleTop + kMergedTitleH + kMergedLinesGap
         + (int)lineCount * kMergedLineH + kMergedFooterGap + kMergedFooterH;
}

// ---- Markdown 消息卡片（kMsgText + isMarkdown）----------------------------
//
// 卡片的宽度、标题与摘要的取法、高度与内容绘制都在 ChatMarkdownCard（与消息记录窗、聊天记录查看窗的独立卡片控件
// 共用，2026-10-07 从本文件抽出）；本文件只管把卡片放进气泡、记下命中区域、点击后请宿主打开预览窗口。

// ---- 文件共享卡片（kMsgFileShare）几何 ----------------------------------
//
// 版式自上而下：标题一行（类型图标 + 粗体名称）、显示路径一行（灰，为空时不画）、「某某的共享 · 状态」一行（小号灰字）；
// 有按钮时再画一条分隔线和一行居中的按钮文字（主题橙，不可点时为灰）。卡片要显示的文字由 ChatFileShareCard 算好放在
// ChatMessage 里，本文件只管画。按钮可点时整张卡片都可点，点击打开共享。
const int kShareBubbleW      = 280;  // 卡片固定宽，与合并卡片、接龙卡片相同
const int kSharePadH         = 14;   // 水平内边距（每侧）
const int kShareTitleTop     = 10;   // 标题距卡片顶
const int kShareTitleH       = 22;   // 标题行高
const int kShareTitlePx      = 14;   // 标题字号
const int kShareIconSize     = 18;   // 标题前类型图标的边长
const int kShareIconGap      = 6;    // 图标与名称之间的间距
const int kShareTypeIconSize = 16;   // 文件共享卡片标题前系统类型图标的边长（系统小图标的原尺寸，不缩放）
const Gdiplus::REAL kShareIconStroke = 1.8f;   // 图标描边宽度（viewBox 24 单位）
const int kSharePathGap      = 2;    // 标题与路径之间的间距
const int kSharePathH        = 18;   // 路径行高
const int kSharePathPx       = 12;   // 路径字号
const int kShareStatusGap    = 4;    // 路径（没有路径时为标题）与状态行之间的间距
const int kShareStatusH      = 18;   // 状态行行高
const int kShareStatusPx     = 11;   // 状态行字号
const int kShareBottomPad    = 8;    // 没有按钮时状态行下方的留白
const int kShareBtnGap       = 6;    // 状态行与按钮上方分隔线之间的间距
const int kShareBtnH         = 34;   // 分隔线下方按钮行的高度，也是按钮的命中矩形高
const int kShareBtnPx        = 13;   // 按钮文字字号
const int kShareNoticeGap    = 2;    // 副标题与说明块之间的间距（第 14 期，屏幕共享卡片的强制模式说明）
const int kShareNoticeLineH  = 16;   // 说明块每行高
const int kShareNoticeLines  = 2;    // 说明块最多两行（卡片宽 280 时，说明约占一行半）
const int kShareNoticePx     = 11;   // 说明文字字号
const int kShareDescGap      = 2;    // 副标题与描述块之间的间距（第 15 期，屏幕共享卡片的共享描述）
const int kShareDescLineH    = 16;   // 描述块每行高
const size_t kShareDescLines = 2;    // 描述块最多两行，超出部分省略；只有一行时按一行留高度
const int kShareDescPx       = 11;   // 描述文字字号
const COLORREF kShareGrayText     = RGB(0x8A, 0x8A, 0x8A);   // 路径与状态行的灰色
const COLORREF kShareBtnText      = RGB(0xFF, 0x7A, 0x45);   // 按钮文字（主题橙，与接龙卡片一致）
const COLORREF kShareBtnHoverText = RGB(0xCC, 0x62, 0x37);   // 鼠标悬停时的按钮文字
const COLORREF kShareBtnDisabledText = RGB(0xB8, 0xB8, 0xB8);   // 按钮不可点时的文字
const Gdiplus::Color kShareIconColor(255, 0xFF, 0x7A, 0x45);   // 类型图标（主题橙）
const Gdiplus::Color kShareDivider(255, 0xE2, 0xDE, 0xDA);     // 按钮上方分隔线，与合并卡片相同

// ---- 语音气泡（kMsgVoice，docs/语音消息方案.md 第 3.2 节）----
// 一行高的气泡：左侧喇叭图标，中间波形，右侧时长；宽度随时长变化（ChatVoiceBubble 的 VoiceBubbleWidth）。几何、配色与
// 绘制在 ChatVoicePaint 里，与消息记录窗、合并记录查看窗的语音条共用（第 4 期起）。

// ---- 屏幕共享卡片（kMsgScreenShare）----
// 与文件共享卡片同一套几何与配色：标题行（屏幕共享图标 + 粗体「屏幕共享」）、副标题行（灰，「某某发起的共享」，占文件
// 共享卡片的路径行）、状态行（小号灰字），别人发起的卡片再加一行「加入」按钮。按钮可点时整张卡片都可点，点击加入共享。
const wchar_t* const kScreenShareCardTitle = TR_NOOP(L"屏幕共享");   // 标题行文字：第 15 期起写共享标题，没有标题的旧卡片写这一句
const wchar_t* const kScreenShareCardButton = TR_NOOP(L"加入");      // 按钮文字
const wchar_t* const kInfoCardButton = TR_NOOP(L"查看");             // 群公告卡片、群备忘卡片复用本版式时的按钮文字
const wchar_t* const kCloudRecordCardButton = TR_NOOP(L"播放");      // 云端录像卡片复用本版式时的按钮文字
const int kGroupCallCardVideo = 2;   // 群通话卡片的媒体类型取值：群视频（标题前画摄像机，其余画电话）

// ---- 密聊邀请卡片（kMsgSecretInvite，docs/密聊方案.md 4.8）----
// 与入群邀请卡片同一套几何与配色：标题行（挂锁 + 粗体标题）、说明行（灰）、剩余秒数（小号灰字），底部按钮行。
// 发起方一侧只有「取消」一个按钮、占满整行；被邀请方一侧左半「同意」右半「忽略」，中间画一条竖向分隔线。
const int kSecretBtnDividerInset = 8;   // 按钮行中间竖向分隔线上下各缩进的像素数，不与横向分隔线、卡片底边相接

// 共享卡片的总高：随有无路径行、描述块的行数（屏幕共享卡片的共享描述，第 15 期，0 表示没有描述）、有无说明块
// （屏幕共享卡片的强制模式说明，第 14 期）、有无按钮变化。
int ShareBubbleHeight(bool hasPath, bool hasButton, bool hasNotice = false, size_t descLines = 0)
{
    int h = kShareTitleTop + kShareTitleH;
    if (hasPath)
    {
        h += kSharePathGap + kSharePathH;
    }
    if (descLines > 0)
    {
        h += kShareDescGap + static_cast<int>(descLines) * kShareDescLineH;
    }
    if (hasNotice)
    {
        h += kShareNoticeGap + kShareNoticeLines * kShareNoticeLineH;
    }
    h += kShareStatusGap + kShareStatusH;
    h += hasButton ? (kShareBtnGap + kShareBtnH) : kShareBottomPad;
    return h;
}

// 文件共享卡片标题前的系统类型图标按哪一类取：磁盘与单个文件照原样，其余（文件夹，以及卡片内容不可用时）按文件夹。
int ShareCardIconItemKind(int shareKind)
{
    if (shareKind == share_kind_disk || shareKind == share_kind_file)
        return shareKind;
    return shellicon::kItemFolder;
}

// ---- 群接龙卡片（kMsgRelay）几何 ----------------------------------------
//
// 版式自上而下：标题一行（接龙图标 + 粗体标题）、说明至多两行（灰）、至多 kRelayCardPreviewLines
// 条接龙（深色；一条都没有时写「还没有人参与」）、状态行（小号灰字）；有按钮时再画一条分隔线和
// 一行居中的按钮文字（主题橙）。卡片要显示的文字由 ChatRelayCard 算好放在 ChatMessage 里，
// 本文件只管画。整张卡片可点（打开详情窗），按钮那一行单独命中（打开详情窗并聚焦输入框）。
const int kRelayBubbleW      = 280;  // 卡片固定宽，与合并卡片相同
const int kRelayPadH         = 14;   // 水平内边距（每侧）
const int kRelayTitleTop     = 10;   // 标题距卡片顶
const int kRelayTitleH       = 22;   // 标题行高
const int kRelayTitlePx      = 14;   // 标题字号
const int kRelayIconSize     = 16;   // 标题前接龙图标的边长
const int kRelayIconGap      = 6;    // 图标与标题文字之间的间距
const Gdiplus::REAL kRelayIconStroke = 2.0f;   // 图标描边宽度（viewBox 24 单位）
const int kRelayNoteGap      = 2;    // 标题与说明之间的间距
const int kRelayNoteLineH    = 18;   // 说明每行的行高
const int kRelayNotePx       = 12;   // 说明字号
const size_t kRelayNoteMaxLines = 2; // 说明最多显示几行，再多以省略号收尾（方案 §3.2）
const int kRelayEntriesGap   = 6;    // 说明（没有说明时为标题）与第一条接龙之间的间距
const int kRelayEntryH       = 20;   // 每条接龙的行高
const int kRelayEntryPx      = 13;   // 接龙文字字号
const int kRelayStatusGap    = 4;    // 最后一条接龙与状态行之间的间距
const int kRelayStatusH      = 18;   // 状态行行高
const int kRelayStatusPx     = 11;   // 状态行字号
const int kRelayBottomPad    = 8;    // 没有按钮时状态行下方的留白
const int kRelayBtnGap       = 6;    // 状态行与按钮上方分隔线之间的间距
const int kRelayBtnH         = 34;   // 分隔线下方按钮行的高度，也是按钮的命中矩形高
const int kRelayBtnPx        = 13;   // 按钮文字字号
const COLORREF kRelayGrayText  = RGB(0x8A, 0x8A, 0x8A);   // 说明、状态行与「还没有人参与」的灰色
const COLORREF kRelayBtnText   = RGB(0xFF, 0x7A, 0x45);   // 按钮文字（主题橙）
const COLORREF kRelayBtnHoverText = RGB(0xCC, 0x62, 0x37);   // 鼠标悬停时的按钮文字（主题橙加深）
const Gdiplus::Color kRelayIconColor(255, 0xFF, 0x7A, 0x45);   // 标题前图标（主题橙）
const Gdiplus::Color kRelayDivider(255, 0xE2, 0xDE, 0xDA);     // 按钮上方分隔线，与合并卡片相同

// 接龙卡片的总高：随说明行数（0 ~ kRelayNoteMaxLines）、接龙条数（0 ~ kRelayCardPreviewLines，
// 0 条时仍占一行写「还没有人参与」）与有无按钮变化。
int RelayBubbleHeight(size_t noteLines, size_t entryLines, bool hasButton)
{
    int h = kRelayTitleTop + kRelayTitleH;
    if (noteLines > 0)
    {
        h += kRelayNoteGap + (int)noteLines * kRelayNoteLineH;
    }
    const size_t shownEntries = (entryLines > 0) ? entryLines : 1;
    h += kRelayEntriesGap + (int)shownEntries * kRelayEntryH;
    h += kRelayStatusGap + kRelayStatusH;
    if (hasButton)
    {
        h += kRelayBtnGap + kRelayBtnH;
    }
    else
    {
        h += kRelayBottomPad;
    }
    return h;
}

// 接龙卡片底部按钮的文字；不显示按钮时返回 NULL。
LPCWSTR RelayButtonLabel(int button)
{
    switch (button)
    {
    //本人还没参与、名额未满
    case kRelayButtonJoin:
        return TR(L"参与接龙");

    //本人已参与，修改自己那一条
    case kRelayButtonModify:
        return TR(L"修改我的");

    //已结束、已截止、被禁言，或名额已满且本人未参与
    default:
        return NULL;
    }
}

// 把接龙说明按 width 像素折成至多 maxLines 行：说明里的换行照常分段（空行跳过），段内按字符
// 折行；放不下的部分在最后一行末尾以省略号收尾。hdc 上须已选好说明所用的字体。
void WrapRelayNote(HDC hdc, const CString& note, int width, size_t maxLines, std::vector<CString>& out)
{
    out.clear();
    if (note.IsEmpty() || width <= 0 || maxLines == 0)
    {
        return;
    }

    CString normalized = note;
    normalized.Remove(L'\r');

    bool truncated = false;
    int pos = 0;
    const int total = normalized.GetLength();
    while (pos < total)
    {
        int lineEnd = normalized.Find(L'\n', pos);
        if (lineEnd < 0)
        {
            lineEnd = total;
        }
        CString para = normalized.Mid(pos, lineEnd - pos);
        pos = lineEnd + 1;
        if (para.IsEmpty())
        {
            continue;
        }

        // 段内折行：每次取能放进 width 的最多字符数；一个字符都放不下时也至少取一个，避免死循环
        while (!para.IsEmpty())
        {
            if (out.size() >= maxLines)
            {
                truncated = true;
                break;
            }
            int fit = 0;
            SIZE sz = { 0, 0 };
            ::GetTextExtentExPointW(hdc, (LPCWSTR)para, para.GetLength(), width, &fit, NULL, &sz);
            if (fit <= 0)
            {
                fit = 1;
            }
            out.push_back(para.Left(fit));
            para = para.Mid(fit);
        }
        if (truncated)
        {
            break;
        }
        if (out.size() >= maxLines && pos < total)
        {
            // 行数已满而后面还有内容：只要后面不全是空行就算被截断
            CString rest = normalized.Mid(pos);
            rest.Remove(L'\n');
            truncated = !rest.IsEmpty();
            break;
        }
    }

    // 被截断时把最后一行缩短到能接上省略号
    if (truncated && !out.empty())
    {
        const wchar_t* const kEllipsis = L"\x2026";
        CString& last = out.back();
        for (;;)
        {
            CString candidate = last + kEllipsis;
            SIZE sz = { 0, 0 };
            ::GetTextExtentPoint32W(hdc, (LPCWSTR)candidate, candidate.GetLength(), &sz);
            if (sz.cx <= width || last.IsEmpty())
            {
                last = candidate;
                break;
            }
            last = last.Left(last.GetLength() - 1);
        }
    }
}

//文件夹气泡状态行的一行文字及其配色。之所以把三样打包返回，是因为它们必须一起决定：
//同一句话在不同状态下颜色与字重都不同（进行中用主题橙半粗、失败用红、已结束用灰）。
struct FolderStatusLine
{
    CString  m_text;      // 状态行主文字（已按状态拼好，绘制侧直接画；过长时以省略号收尾）
    CString  m_counter;   // "第 i / 共 n 个"计数段，仅传输中且已知序号时非空。它<u>不</u>并进
                          // m_text：文件名长短不可控，并成一串时先被省略号吃掉的正是这一段，
                          // 而"传到第几个了"恰恰是用户最想看的。绘制侧把它右对齐单独成段，
                          // 只让文件名那部分收缩
    COLORREF m_color;     // 文字颜色（主文字与计数段同色）
    bool     m_bold;      // 是否用半粗字重（进行中与失败的状态需要，其余不需要）
};

/**
 *  按文件夹传输状态拼出气泡里的状态行。
 *  @param msg 本行承载的消息（读 folderState / fromMe / folderCurFile / folderCurIndex /
 *             folderFileCount 五个字段）
 *  @return 状态行的主文字、计数段与配色。未知状态返回一句中性文案，保证这一行不会空着
 */
FolderStatusLine BuildFolderStatusLine(const ChatMessage& msg)
{
    FolderStatusLine line;
    line.m_color = RGB(0x8A, 0x8A, 0x8A);   // 默认灰：已结束、不再变化的那几种状态
    line.m_bold  = false;

    switch (msg.folderState)
    {
    //邀请已发出、等待应答：发送端在等，接收端要当场决定收不收
    case kFolderInviting:
        line.m_text  = msg.fromMe ? TR(L"等待对方接收…") : TR(L"对方想给你发送一个文件夹");
        line.m_color = RGB(0xFF, 0x7A, 0x45);
        line.m_bold  = true;
        break;

    //接收端正在逐级建目录，还没有字节传出去
    case kFolderPreparing:
        line.m_text  = msg.fromMe ? TR(L"对方正在创建目录…") : TR(L"正在创建目录…");
        line.m_color = RGB(0xFF, 0x7A, 0x45);
        line.m_bold  = true;
        break;

    //正在数据通道上收发文件字节：报当前传的是哪个文件、排第几个
    case kFolderTransferring:
        if (!msg.folderCurFile.IsEmpty())
        {
            line.m_text.Format(TR(L"正在传输 %s"), (LPCTSTR)msg.folderCurFile);
        }
        else
        {
            line.m_text = TR(L"正在传输…");
        }
        if (msg.folderCurIndex > 0)
        {
            //序号没报上来时（刚开始传第一个文件）这一段留空，不硬凑一个"第 0 个"
            line.m_counter.Format(TR(L"第 %d / 共 %d 个"),
                                  msg.folderCurIndex, msg.folderFileCount);
        }
        line.m_color = RGB(0xFF, 0x6A, 0x3D);
        line.m_bold  = true;
        break;

    //全部文件传完并通过校验
    case kFolderCompleted:
        line.m_text  = msg.fromMe ? TR(L"全部发送完成") : TR(L"全部接收完成");
        line.m_color = RGB(0x3B, 0xA5, 0x5D);   // 绿：这次传输已经安全落地
        line.m_bold  = true;
        break;

    //接收端点了「拒绝」，一个字节都没传
    case kFolderRejected:
        line.m_text = msg.fromMe ? TR(L"对方已拒绝接收") : TR(L"已拒绝接收");
        break;

    //发送端在对方应答前撤回了邀请
    case kFolderInviteCanceled:
        line.m_text = msg.fromMe ? TR(L"已撤回邀请") : TR(L"对方已撤回邀请");
        break;

    //发起邀请时对方无在线连接，或响铃期间对方退出登录
    case kFolderPeerOffline:
        line.m_text = TR(L"对方不在线，传输未开始");
        break;

    //限定时间内无人应答，服务端自动结束了会话
    case kFolderTimeout:
        line.m_text = TR(L"邀请已超时");
        break;

    //传到一半失败：具体原因由下面单独的一行给出，这里只给状态本身
    case kFolderFailed:
        line.m_text  = TR(L"传输失败");
        line.m_color = RGB(0xE5, 0x39, 0x35);
        line.m_bold  = true;
        break;

    //失败且已超出保留期：续传位点作废，措辞要让用户明白"重试已经没用了"
    case kFolderExpired:
        line.m_text  = TR(L"传输失败，已超过可续传的时限");
        line.m_color = RGB(0xE5, 0x39, 0x35);
        break;

    //任一端在传输过程中点了「取消」
    case kFolderUserCanceled:
        line.m_text = TR(L"已取消");
        break;

    //未知状态：宿主漏填 folderState（留了 0），或今后新增状态而本表没跟上。
    //给一句中性文案，好过让这一行空着
    default:
        line.m_text = TR(L"文件夹传输");
        break;
    }
    return line;
}

/**
 *  拼出文件夹气泡的摘要行文字，形如「128 个文件 · 12 个目录 · 共 2.4 GB」。
 *  目录数为 0 时省掉中间那一段——单层目录的文件夹说"0 个目录"没有意义。
 *  @param msg 本行承载的消息（读 folderFileCount / folderDirCount / folderSize）
 *  @return 摘要行文字
 */
CString BuildFolderSummaryText(const ChatMessage& msg)
{
    CString text;
    if (msg.folderDirCount > 0)
    {
        text.Format(TR(L"%d 个文件 · %d 个目录 · 共 %s"),
                    msg.folderFileCount, msg.folderDirCount,
                    (LPCTSTR)msg.folderSize);
    }
    else
    {
        text.Format(TR(L"%d 个文件 · 共 %s"),
                    msg.folderFileCount, (LPCTSTR)msg.folderSize);
    }
    return text;
}

// 一条文本消息解析出来的绘制样式（字号 / 字形 / 颜色）。
//
// 它与消息记录窗要用的是同一种东西，故结构本体已随排版引擎一起搬到
// ChatInlineText.h（改名 InlineTextStyle）；这里留一个别名，使本文件里既有的
// 上百处 BubbleTextStyle 一个字都不用改。
typedef InlineTextStyle BubbleTextStyle;

// ---- 群聊 @提醒 的配色与几何（见 docs/群聊@提醒方案.md §5.2 / §5.3）----------
//
// 四格：两种气泡底（对方浅灰 / 自己浅橙）× 两种 @ 类型（@我 / @别人）。
// @我 走主题橙系，与品牌色一脉相承、最醒目；自己气泡本身就是浅橙底，故其上的
// 橙块要压深一档才拉得开层次。@别人 走中性蓝，两种气泡底共用一套 —— 蓝块落在
// 浅橙底上略跳，但正因为跳才能一眼与「@我」区分。
const COLORREF kAtMeBgOnOther    = RGB(0xFF, 0xDD, 0xCB);   // 对方气泡里 @我 的底色块
const COLORREF kAtMeFgOnOther    = RGB(0xD9, 0x53, 0x1E);   // 对方气泡里 @我 的文字
const COLORREF kAtMeBgOnSelf     = RGB(0xFF, 0xC9, 0xAE);   // 自己气泡里 @我 的底色块
const COLORREF kAtMeFgOnSelf     = RGB(0xB8, 0x43, 0x1A);   // 自己气泡里 @我 的文字
const COLORREF kAtOtherBg        = RGB(0xE4, 0xEE, 0xFB);   // @别人 的底色块（两种气泡共用）
const COLORREF kAtOtherFg        = RGB(0x2F, 0x6F, 0xD0);   // @别人 的文字（两种气泡共用）

const int kAtBlockRadius  = 3;   // 底色块圆角半径（像素）
const int kAtBlockPadX    = 2;   // 底色块相对文字左右各外扩（像素）
const int kAtBlockPadY    = 1;   // 底色块相对文字上下各外扩（像素）

// 取某个 @ 片段该用的底色与文字色。
//   fromMe：本条消息是不是自己发的（决定气泡底是浅橙还是浅灰）。
//   isMe：  这个片段 @ 的是不是本人。
//   bg / fg：[出参] 底色块颜色与文字颜色。
void ResolveMentionColors(bool fromMe, bool isMe, COLORREF& bg, COLORREF& fg)
{
    if (!isMe)
    {
        bg = kAtOtherBg;
        fg = kAtOtherFg;
        return;
    }
    if (fromMe)
    {
        bg = kAtMeBgOnSelf;
        fg = kAtMeFgOnSelf;
    }
    else
    {
        bg = kAtMeBgOnOther;
        fg = kAtMeFgOnOther;
    }
}

// 确保 GDI+ 在本模块可用。
//
// 本进程<u>没有</u>全局的 GdiplusStartup —— 各模块按仓库既有做法各自惰性初始化
// （见 ChatImageCache.cpp / AvatarImageUtil.cpp / BalloonAboutDlg.cpp）。本文件此前
// 也用 GDI+ 画气泡底与头像，之所以一直没出问题，是因为那些绘制发生在<u>绘制阶段</u>，
// 那时别的模块已经初始化过了。
//
// 而 @提醒 的排版是在 Measure()（<u>布局阶段</u>）就要用 GDI+ 的，比任何绘制都早。
// 此时 GDI+ 未初始化，Graphics / FontFamily 的状态会是 GdiplusNotInitialized(18)，
// 且 new Gdiplus::Font 会<u>返回空指针而不是抛异常</u>（GdiplusBase::operator new 内部
// 调 GdipAlloc，失败即返回 NULL，构造函数根本不执行）—— 表现为气泡尺寸为 0、
// 一个字都画不出来。故这里必须自己兜底初始化一次。
// 多次 GdiplusStartup 是安全的：各模块各持一个 token，进程级只需保证启动过。
void EnsureGdiplusForBubble()
{
    static ULONG_PTR s_token = 0;
    if (s_token != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_token, &input, NULL);
}

// 把各 @ 片段的圆角底色块画在文字之下。
//
// 矩形取自「排版模型 + 扁平口径的片段」，与正文用的是同一次排版，故片段跨行时
// 每行自然得到一块，不必自己按行切分。
//   hdc：     本行绘制目标。
//   layout：  字符级排版模型（Measure 产出）。
//   atsFlat： 扁平口径的 @ 片段。
//   fromMe：  本条是不是自己发的（决定底色深浅，与气泡底配合）。
//   origin：  文本区原点（绝对坐标，已按渲染缓存平移过）。
void DrawMentionBlocks(HDC hdc, const SelLayout& layout,
                       const std::vector<RenderSpan>& atsFlat,
                       bool fromMe, const POINT& origin)
{
    if (atsFlat.empty() || layout.m_lines.empty())
    {
        return;
    }
    // 本进程没有全局的 GdiplusStartup，各模块各自惰性初始化（原委见
    // EnsureGdiplusForBubble 的注释）。这里是本文件里唯一还用 GDI+ 画 @ 的地方，
    // 少了这一句，Graphics / GraphicsPath 会处在 GdiplusNotInitialized 状态，
    // FillPath 直接访问违例。
    EnsureGdiplusForBubble();

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    for (std::size_t i = 0; i < atsFlat.size(); ++i)
    {
        COLORREF bg = 0;
        COLORREF fg = 0;
        ResolveMentionColors(fromMe, atsFlat[i].m_isMe, bg, fg);
        Gdiplus::SolidBrush brush(
            Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));

        const std::vector<SelRect> rects = BuildSelectionRects(
            layout, atsFlat[i].m_off, atsFlat[i].m_off + atsFlat[i].m_len);
        for (std::size_t r = 0; r < rects.size(); ++r)
        {
            // 底色块比文字四周各外扩一点，视觉上像个标签而不是紧贴字形的色条。
            const Gdiplus::REAL x = (Gdiplus::REAL)(origin.x + rects[r].m_left - kAtBlockPadX);
            const Gdiplus::REAL y = (Gdiplus::REAL)(origin.y + rects[r].m_top  - kAtBlockPadY);
            const Gdiplus::REAL w = (Gdiplus::REAL)(rects[r].m_right - rects[r].m_left + kAtBlockPadX * 2);
            const Gdiplus::REAL h = (Gdiplus::REAL)(rects[r].m_bottom - rects[r].m_top + kAtBlockPadY * 2);

            Gdiplus::GraphicsPath path;
            AddRoundRect(path, x, y, w, h, (Gdiplus::REAL)kAtBlockRadius);
            g.FillPath(&brush, &path);
        }
    }
}

// 把各 @ 片段的文字用高亮色重画一遍。
//
// 「按片段矩形设裁剪区、在裁剪区里重画」那套手法已抽到 ChatInlineText 的
// DrawInlinePlacedRestyledRanges，与超链接渲染共用同一份实现（理由见那边的注释）；
// 本函数只负责按配色把片段分组：@本人 与 @别人 各一种颜色，同色的片段一次画完。
// 正文的排版结果由调用方传入（气泡缓存的那一份），这里不再排版。
//   hdc：     本行绘制目标。
//   placed：  正文按绘制样式排出的放置项（与正文那一层用的同一份）。
//   layout：  字符级排版模型。
//   atsFlat： 扁平口径的 @ 片段。
//   fromMe：  本条是不是自己发的。
//   txRc：    文本区矩形（绝对坐标）。
//   st：      本条消息的文字样式；本函数只改颜色，其余照用。
void DrawMentionHighlightText(HDC hdc,
                              const std::vector<InlinePlaced>& placed,
                              const SelLayout& layout,
                              const std::vector<RenderSpan>& atsFlat,
                              bool fromMe,
                              const RECT& txRc,
                              const BubbleTextStyle& st)
{
    if (atsFlat.empty() || layout.m_lines.empty())
    {
        return;
    }

    //按被 @ 的是不是本人分成两组，配色由 ResolveMentionColors 决定，组内同色
    std::vector<InlineCharRange> meRanges;
    std::vector<InlineCharRange> otherRanges;
    for (std::size_t i = 0; i < atsFlat.size(); ++i)
    {
        const InlineCharRange range(atsFlat[i].m_off, atsFlat[i].m_off + atsFlat[i].m_len);
        if (atsFlat[i].m_isMe)
        {
            meRanges.push_back(range);
        }
        else
        {
            otherRanges.push_back(range);
        }
    }

    const bool kIsMeGroups[] = { true, false };
    for (std::size_t g = 0; g < sizeof(kIsMeGroups) / sizeof(kIsMeGroups[0]); ++g)
    {
        const std::vector<InlineCharRange>& ranges = kIsMeGroups[g] ? meRanges : otherRanges;
        if (ranges.empty())
        {
            continue;
        }
        COLORREF bg = 0;
        COLORREF fg = 0;
        ResolveMentionColors(fromMe, kIsMeGroups[g], bg, fg);
        BubbleTextStyle hi = st;
        hi.color = fg;
        DrawInlinePlacedRestyledRanges(hdc, placed, layout, ranges, txRc, hi);
    }
}

// 超链接文字色。两种气泡（对方浅灰底 / 自己浅橙底）共用同一档蓝 —— 与 @别人 的
// 文字同色，那一档已经在两种底色上都验证过可读。链接靠"蓝 + 下划线"与 @ 区分开
// （@ 有圆角底色块、没有下划线）。
const COLORREF kLinkFg = RGB(0x2F, 0x6F, 0xD0);

// 把各超链接片段用链接色 + 下划线重画一遍。
//
// 与 @提醒 的高亮共用 DrawInlinePlacedRestyledRanges（裁剪重画那套手法），故跨行的
// 长链接天然正确，不必自己按行拆。所有链接一次画完、不再排版（用调用方传入的放置项）：
// 2026-10-01 之前是逐个链接调用、每次整条重新排版，一条几十行、每行一个链接的消息首次
// 绘制要好几秒。
//   hdc：      本行绘制目标。
//   placed：   正文按绘制样式排出的放置项（与正文那一层用的同一份）。
//   layout：   字符级排版模型。
//   links：    扁平口径的链接片段。
//   txRc：     文本区矩形（绝对坐标）。
//   st：       本条消息的文字样式；本函数只改颜色并打开下划线，其余照用。
void DrawLinkText(HDC hdc,
                  const std::vector<InlinePlaced>& placed,
                  const SelLayout& layout,
                  const std::vector<LinkSpan>& links,
                  const RECT& txRc,
                  const BubbleTextStyle& st)
{
    if (links.empty() || layout.m_lines.empty())
    {
        return;
    }

    BubbleTextStyle ls = st;
    ls.color     = kLinkFg;
    ls.underline = true;

    std::vector<InlineCharRange> ranges;
    ranges.reserve(links.size());
    for (std::size_t i = 0; i < links.size(); ++i)
    {
        ranges.push_back(InlineCharRange(links[i].m_off, links[i].m_off + links[i].m_len));
    }
    DrawInlinePlacedRestyledRanges(hdc, placed, layout, ranges, txRc, ls);
}

// 从一条消息的字体设置解出绘制样式。font.m_valid 为 false 时回退到气泡默认
// （kBubbleTextPx、不加粗、默认色、无斜体/下划线/删除线）。net::TextFont 的颜色
// 是 0xRRGGBB，COLORREF 是 0x00BBGGRR，需把 R / B 调换。
BubbleTextStyle ResolveTextStyle(const ChatMessage& msg)
{
    BubbleTextStyle st;
    st.sizePx    = kBubbleTextPx;
    st.bold      = false;
    st.italic    = false;
    st.underline = false;
    st.strike    = false;
    st.color     = kBubbleText;
    if (msg.font.m_valid)
    {
        st.sizePx    = msg.font.m_size;
        st.bold      = msg.font.m_bold;
        st.italic    = msg.font.m_italic;
        st.underline = msg.font.m_underline;
        st.strike    = msg.font.m_strike;
        const unsigned int rgb = msg.font.m_colorRgb;
        st.color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    return st;
}

// 剪贴板写入已抽到 ChatTextSelection（右键「复制」与消息区 Ctrl+C 共用同一份，
// 见那边的 PutTextToClipboard 注释），本文件直接用它。

// 秒换算成毫秒：复制接龙卡片时按当前时刻写末行，time() 给的是秒
const int64_t kMsPerSecondForCopy = 1000;

// 把一条消息的可复制文本取出来（不同 kind 取不同字段）。
CString MessageCopyText(const ChatMessage& msg)
{
    switch (msg.kind)
    {
    case kMsgText:
    case kMsgEmoji:
        return msg.text;
    case kMsgFile:
        return msg.fileName;
    case kMsgFolder:
        //与文件同理：给文件夹名比给"[文件夹]"有信息量，一个会话里可能发过好几个文件夹
        return msg.folderName;
    case kMsgImage:
        return msg.imageCaption.IsEmpty()
                   ? CString(TR(L"[图片]")) : msg.imageCaption;
    case kMsgRelay:
        //与详情窗「复制为文本」同一份文字（方案 §3.7），末行按复制时刻现拼
        return RelayCopyTextForMessage(msg, (int64_t)time(NULL) * kMsPerSecondForCopy);
    default:
        return msg.text;
    }
}

} // anonymous namespace

// 重量级折行测量的进程级计数器（性能探针，详见 ChatBubble.h 的声明）。
unsigned long ChatBubble::s_heavyMeasureCount = 0;

unsigned long ChatBubble::HeavyMeasureCount()
{
    return s_heavyMeasureCount;
}

void ChatBubble::ResetHeavyMeasureCount()
{
    s_heavyMeasureCount = 0;
}

// 渲染缓存重画次数的进程级计数器与超长气泡分段阈值（详见 ChatBubble.h 的声明）。
unsigned long ChatBubble::s_renderPassCount = 0;
int ChatBubble::s_tallSliceThresholdPx = kDefaultTallSliceThresholdPx;

int ChatBubble::TallSliceThreshold()
{
    return s_tallSliceThresholdPx;
}

void ChatBubble::SetTallSliceThreshold(int px)
{
    s_tallSliceThresholdPx = px;
}

int ChatBubble::GetRenderCacheHeight() const
{
    return (m_renderDC != NULL) ? m_renderH : 0;
}

unsigned long ChatBubble::RenderPassCount()
{
    return s_renderPassCount;
}

void ChatBubble::ResetRenderPassCount()
{
    s_renderPassCount = 0;
}

ChatBubble::ChatBubble()
    : m_measuredWidth(-1)
    , m_measuredMultiSel(false)
    , m_textBubbleW(0)
    , m_textBubbleH(kTextBubbleH)
    , m_selected(false)
    , m_selAnchor(-1)
    , m_selCaret(-1)
    , m_selDragging(false)
    , m_dragArmed(false)
    , m_dragPressPt()
    , m_readReceiptEnabled(false)
    , m_avatarGray(false)
    , m_fileBtnCount(0)
    , m_fileHoverIdx(-1)
    , m_folderBtnCount(0)
    , m_folderHoverIdx(-1)
    , m_renderDC(NULL)
    , m_renderBmp(NULL)
    , m_renderOldBmp(NULL)
    , m_renderW(0)
    , m_renderH(0)
    , m_renderMultiSel(false)
    , m_renderDirty(true)
    , m_renderRowH(0)
    , m_renderSliceTop(0)
    , m_selBandDirty(false)
    , m_selBandRect()
    , m_textPlacedValid(false)
    , m_textPlacedW(0)
    , m_measuredLineW(-1)
    , m_measuredMaxTextW(0)
    , m_markdownActive(false)
    , m_markdownFailed(false)
    , m_mdCardSummaryH(0)
    , m_mdCardRel()
    , m_codeActive(false)
    , m_codePrepared(false)
    , m_codeCopied(false)
    , m_codeHover(kCodePartNone)
    , m_codeCardRel()
    , m_quoteRectRel()
    , m_quoteThumbLogged(false)
    , m_pinRectRel()
    , m_pinHover(false)
    , m_linkPressIdx(-1)
    , m_mergedPressed(false)
    , m_relayPressed(false)
    , m_relayBtnHover(false)
    , m_recallEditPressed(false)
    , m_recallEditHover(false)
    , m_sharePressed(false)
    , m_shareBtnHover(false)
    , m_secretBtnHover(-1)
    , m_secretBtnPressed(-1)
    , m_voicePressed(false)
    , m_voiceFailPressed(false)
{
    m_msg.kind = kMsgText;
    m_msg.fromMe = false;
    m_msg.imageW = m_msg.imageH = 0;
    m_msg.fileProgress = 0;
    m_msg.hasQuote = false;
    m_msg.read = false;
    ::SetRectEmpty(&m_imageRect);
    ::SetRectEmpty(&m_mergedRect);
    ::SetRectEmpty(&m_relayRect);
    ::SetRectEmpty(&m_relayBtnRect);
    ::SetRectEmpty(&m_shareRect);
    ::SetRectEmpty(&m_shareBtnRect);
    ::SetRectEmpty(&m_voiceRect);
    ::SetRectEmpty(&m_voiceFailRect);
    for (int i = 0; i < 3; ++i)
    {
        ::SetRectEmpty(&m_fileBtnRect[i]);
        m_fileBtnAction[i] = 0;
    }
    for (int i = 0; i < kMaxChatFolderActions; ++i)
    {
        ::SetRectEmpty(&m_folderBtnRect[i]);
        m_folderBtnAction[i]  = 0;
        m_folderBtnEnabled[i] = false;
    }
}

ChatBubble::~ChatBubble()
{
    ReleaseRenderCache();
}

void ChatBubble::Invalidate()
{
    // 内容变 → 渲染缓存置脏，下次 OnPaint 重画缓存位图。再走基类原有重绘请求。
    m_renderDirty = true;
    DuiControl::Invalidate();
}

bool ChatBubble::IsRenderCacheable() const
{
    // 引用了一张图片的文字消息不能缓存：缩略图要等后台解码完成、之后重绘才画得出来，
    // 而缓存会把第一次画的那块占位一直搬下去 —— 解码完成后虽然重绘了，缓存却仍然有效
    // （内容没变、宽高没变），于是缩略图永远出不来，表现为引用块左侧一直是一块空白。
    // 图片气泡本身正是因为同样的原因才不缓存，这里与之保持一致。缩略图就绪之后其实
    // 可以恢复缓存，但带图片引用的消息本就少，不值得为此加一套状态机。
    if (m_msg.hasQuote && m_msg.quoteMsgType == kMsgImage)
    {
        return false;
    }

    // 文字 / emoji / 时间分隔行绘制时不记录任何宿主坐标系命中矩形，可安全缓存。
    // 图片 / 文件 / 文件夹 / 合并卡片 / 接龙卡片行绘制时要写 m_imageRect / m_fileBtnRect /
    // m_folderBtnRect / m_mergedRect / m_relayRect 供命中测试，不能缓存。
    return m_msg.kind == kMsgText
        || m_msg.kind == kMsgEmoji
        || m_msg.kind == kMsgTime;
}

void ChatBubble::EnsureRenderCache(HDC ref, int w, int h)
{
    if (m_renderDC != NULL && m_renderW == w && m_renderH == h)
    {
        return;   // 尺寸未变，复用现有缓存位图
    }
    ReleaseRenderCache();
    if (w <= 0 || h <= 0)
    {
        return;
    }
    m_renderDC = ::CreateCompatibleDC(ref);
    if (m_renderDC == NULL)
    {
        return;
    }
    m_renderBmp = ::CreateCompatibleBitmap(ref, w, h);
    if (m_renderBmp == NULL)
    {
        ::DeleteDC(m_renderDC);
        m_renderDC = NULL;
        return;
    }
    m_renderOldBmp = (HBITMAP)::SelectObject(m_renderDC, m_renderBmp);
    m_renderW = w;
    m_renderH = h;
}

void ChatBubble::ReleaseRenderCache()
{
    if (m_renderDC != NULL)
    {
        if (m_renderOldBmp != NULL)
        {
            ::SelectObject(m_renderDC, m_renderOldBmp);
            m_renderOldBmp = NULL;
        }
        ::DeleteDC(m_renderDC);
        m_renderDC = NULL;
    }
    if (m_renderBmp != NULL)
    {
        ::DeleteObject(m_renderBmp);
        m_renderBmp = NULL;
    }
    m_renderW = 0;
    m_renderH = 0;
}

// =======================================================================
// 气泡内文字选中
// =======================================================================

// 选区高亮底色。对方气泡（浅灰底）与自己气泡（浅橙底）各用一档，保证盖在底色上
// 仍能看清、且文字不被糊住。取值参照系统高亮色的浅色版。
static const COLORREF kSelHighlightOnOther = RGB(0xCC, 0xE0, 0xF8);   // 浅蓝
static const COLORREF kSelHighlightOnSelf  = RGB(0xFF, 0xD2, 0xB0);   // 浅橙（比气泡底深一档）

bool ChatBubble::IsTextSelectable() const
{
    // 所有文字气泡都支持选中 —— 含 @ 的已经和不含 @ 的走同一套手写折行排版，
    // 选区模型与画出来的文字同源，不会错位（早先含 @ 的走 GDI+，两套排版对不上，
    // 才不得不禁掉选中）。时间分隔 / 撤回提示 / 图片 / 文件行都没有可选文字。
    // 代码卡片没有可选文字（第一版不支持拖选，见 docs/代码片段方案.md 第 8 节）
    if (CodeActive())
    {
        return false;
    }
    // Markdown 卡片没有可选文字：点卡片打开预览窗口，在那里选中复制（docs/Markdown消息方案.md 第 11.3 节）
    if (MarkdownActive())
    {
        return false;
    }
    return m_msg.kind == kMsgText
        && !m_selLayout.m_lines.empty();
}

bool ChatBubble::MarkdownActive() const
{
    return m_markdownActive && m_msg.kind == kMsgText && m_markdown.IsParsed();
}

int ChatBubble::SelectableTextLength() const
{
    return (int)m_selFlatText.size();
}

std::wstring ChatBubble::LinkUrlByIndex(int index) const
{
    if (index < 0 || (std::size_t)index >= m_linksFlat.size())
    {
        return std::wstring();
    }
    return m_linksFlat[(std::size_t)index].m_url;
}

bool ChatBubble::PointInMarkdownCard(POINT pt) const
{
    if (!MarkdownActive() || ::IsRectEmpty(&m_mdCardRel))
    {
        return false;
    }
    RECT rc = m_mdCardRel;
    ::OffsetRect(&rc, m_rcItem.left, m_rcItem.top);
    return ::PtInRect(&rc, pt) != FALSE;
}

void ChatBubble::PostMarkdownPreviewOpen()
{
    if (!MarkdownActive())
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    MarkdownPreviewOpenRequest* pReq = new MarkdownPreviewOpenRequest();
    pReq->m_sourceUtf8 = MarkdownSourceFromBody(m_msg.text);
    pReq->m_title = m_mdCardTitle;
    pReq->m_sender = std::wstring((LPCWSTR)m_msg.senderName);
    pReq->m_fromMe = m_msg.fromMe;
    pReq->m_timestampMs = m_msg.timestampMs;
    pReq->m_key = (m_msg.msgid > 0) ? std::to_string(m_msg.msgid) : m_msg.clientMsgId;
    const unsigned bytes = (unsigned)pReq->m_sourceUtf8.size();
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_MARKDOWN_PREVIEW, 0, (LPARAM)pReq))
    {
        delete pReq;
        return;
    }
    LOG_INFO("chat bubble: open markdown preview requested, msgid=%lld, bytes=%u.", m_msg.msgid, bytes);
}

// ====================================================================
// 代码片段（docs/代码片段方案.md 第 5.3 节）
// ====================================================================

namespace {

// 代码卡片四周留出的气泡边宽度（像素），与图片气泡每侧的内边距相同
const int kCodeBubblePad = kImageBubblePad / 2;

// 代码卡片可用的整列宽度：内容区宽度减去两侧的头像列（边距、头像、间距），比文字气泡的 62% 宽
int CodeColumnWidth(int contentW)
{
    const int w = contentW - 2 * (kEdgePad + kAvatarSize + kAvatarGap);
    return (w > 0) ? w : 0;
}

} // 匿名 namespace（代码片段）

bool ChatBubble::CodeActive() const
{
    return m_codeActive && m_msg.kind == kMsgText && m_msg.isCode;
}

bool ChatBubble::MeasureCode(HDC hdc, int contentW)
{
    m_codeActive = false;
    if (m_msg.kind != kMsgText || !m_msg.isCode)
    {
        return false;
    }
    // 规范化与着色只在内容变化后做一次（SetMessage 会清空）
    if (!m_codePrepared)
    {
        m_codeContent = PrepareCodeBlockContent(std::wstring((LPCWSTR)m_msg.text), m_msg.codeLang, m_msg.codeNotes);
        m_codePrepared = true;
    }
    const int columnW = CodeColumnWidth(contentW);
    CodeBlockOptions options;
    options.m_footerOpensViewer = true;
    options.m_copied = m_codeCopied;
    LayoutCodeBlock(hdc, m_codeContent, options, columnW - kCodeBubblePad * 2, m_codeLayout);

    // 普通文字的字符级模型一律清空：它们只属于普通文字路径，留着会让命中、菜单按旧内容判断
    m_selLayout = SelLayout();
    m_selFlatText.clear();
    m_measuredPlaced.clear();
    m_measuredLineW = -1;
    m_linksFlat.clear();
    m_atsFlat.clear();

    m_codeActive = true;
    m_textBubbleW = m_codeLayout.m_size.cx + kCodeBubblePad * 2;
    m_textBubbleH = m_codeLayout.m_size.cy + kCodeBubblePad * 2;
    if (m_msg.hasQuote)
    {
        // 引用块按文字气泡的内边距画在顶部，卡片接在它下面：引用块的高度与宽度下限照文字气泡计入，
        // 顶部再补上文字气泡与卡片气泡边之间的差值
        ApplyQuoteToTextBubble(columnW);
        m_textBubbleH += kTextPadV - kCodeBubblePad;
    }
    if (m_textBubbleW > columnW)
    {
        m_textBubbleW = columnW;
    }
    return true;
}

int ChatBubble::CodePartAtPoint(POINT pt) const
{
    if (!CodeActive() || ::IsRectEmpty(&m_codeCardRel))
    {
        return kCodePartNone;
    }
    POINT local = { pt.x - (m_rcItem.left + m_codeCardRel.left), pt.y - (m_rcItem.top + m_codeCardRel.top) };
    return HitTestCodeBlock(m_codeLayout, local);
}

bool ChatBubble::UpdateCodeHover(POINT pt, bool inside)
{
    int part = kCodePartNone;
    if (inside && !ParentInMultiSelect())
    {
        part = CodePartAtPoint(pt);
    }
    // 只有「复制」与底栏加悬停效果（下划线），正文不加
    if (part == kCodePartBody)
    {
        part = kCodePartNone;
    }
    bool changed = false;
    if (part != m_codeHover)
    {
        m_codeHover = part;
        changed = true;
    }
    // 鼠标离开本行：「已复制」恢复为「复制」
    if (!inside && m_codeCopied)
    {
        m_codeCopied = false;
        SetCodeBlockCopied(m_codeLayout, false);
        changed = true;
    }
    if (changed)
    {
        Invalidate();
    }
    return changed;
}

void ChatBubble::CopyCodeToClipboard(HWND hwnd)
{
    if (!CodeActive() || !::IsWindow(hwnd))
    {
        return;
    }
    // 只写纯文字：Flamingo 自己的剪贴板格式会把正文里的 /f["N"] 当作表情还原，代码里恰好有这种写法时会被改掉
    const std::wstring text = CodeTextForClipboard(m_codeContent.m_code);
    PutTextToClipboard(hwnd, text.c_str());
    m_codeCopied = true;
    SetCodeBlockCopied(m_codeLayout, true);
    Invalidate();
    LOG_INFO("chat bubble: code snippet copied, msgid=%lld, %u chars.", m_msg.msgid, (unsigned)text.size());
}

void ChatBubble::PostCodeViewerOpen()
{
    PostCodeViewerOpen(kCodeViewerActionView);
}

void ChatBubble::PostCodeViewerOpen(int action)
{
    if (!CodeActive())
    {
        return;
    }
    //追加批注要有服务器 msgid（引用原消息时用到）
    if (action == kCodeViewerActionAnnotate && !CanAnnotateCodeMessage(m_msg))
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    CodeViewerOpenRequest* pReq = new CodeViewerOpenRequest();
    pReq->m_action = action;
    pReq->m_code = m_codeContent.m_code;
    pReq->m_lang = m_msg.codeLang;
    pReq->m_key = (m_msg.msgid > 0) ? std::to_string(m_msg.msgid) : m_msg.clientMsgId;
    pReq->m_notes = m_msg.codeNotes;
    pReq->m_msgid = m_msg.msgid;
    pReq->m_senderId = m_msg.senderUserId;
    pReq->m_fromMe = m_msg.fromMe;
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_CODE_VIEWER, 0, (LPARAM)pReq))
    {
        delete pReq;
        return;
    }
    LOG_INFO("chat bubble: code snippet request posted, action=%d, msgid=%lld, lines=%d, notes=%u.", action,
             m_msg.msgid, m_codeContent.m_lineCount, (unsigned)m_msg.codeNotes.size());
}

bool ChatBubble::HasTextSelection() const
{
    if (m_selAnchor < 0 || m_selCaret < 0)
    {
        return false;
    }
    return m_selAnchor != m_selCaret;
}

void ChatBubble::ClearTextSelection()
{
    if (m_selAnchor < 0 && m_selCaret < 0 && !m_selDragging)
    {
        return;   // 本就没有选区，避免无谓重绘
    }
    const int oldAnchor = m_selAnchor;
    const int oldCaret  = m_selCaret;
    m_selAnchor   = -1;
    m_selCaret    = -1;
    m_selDragging = false;
    m_dragArmed   = false;   //选区没了，"准备拖出选区"也随之作废
    InvalidateSelectionChange(oldAnchor, oldCaret);
}

void ChatBubble::SelectAllText()
{
    if (!IsTextSelectable())
    {
        return;
    }
    const int oldAnchor = m_selAnchor;
    const int oldCaret  = m_selCaret;
    m_selAnchor = 0;
    m_selCaret  = SelectableTextLength();
    InvalidateSelectionChange(oldAnchor, oldCaret);
}

CString ChatBubble::GetSelectedText() const
{
    if (!HasTextSelection())
    {
        return CString();
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    // 表情替换成 [表情]，与会话列表预览同口径（BodyToDigest 也是这么做的）。
    const std::wstring picked = ExtractSelectedText(m_selFlatText, s, e, TR(L"[表情]"));
    return CString(picked.c_str());
}

std::vector<ChatClipSegment> ChatBubble::GetSelectedClipSegments() const
{
    if (!HasTextSelection())
    {
        return std::vector<ChatClipSegment>();
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    // 选区下标落在扁平串上，而扁平串是 Measure() 用 ParseSendBody(m_msg.text) 的结果
    // 拼出来的（见 m_selFlatText 的赋值处），这里用同一份正文重新解析，两边一一对应。
    return BuildClipSegmentsFromSelection(balloonchat::ParseSendBody(m_msg.text), s, e);
}

std::vector<ChatClipSegment> ChatBubble::WholeMessageClipSegments() const
{
    switch (m_msg.kind)
    {
    //文字消息与大表情：按正文拆成文字 / 表情
    case kMsgText:
    case kMsgEmoji:
        return BuildClipSegmentsFromBody(m_msg.text);

    //图片：本地原图可用时复制图片本身；还没下载、下载失败、文件丢失时返回空序列，
    //调用方退回只写"[图片]"
    case kMsgImage:
        if (IsLocalImageAvailable())
        {
            return BuildClipSegmentsForImage(m_msg.imagePath);
        }
        return std::vector<ChatClipSegment>();

    //文件、文件夹等：没有可还原的内容，只写纯文字
    default:
        return std::vector<ChatClipSegment>();
    }
}

bool ChatBubble::IsLocalImageAvailable() const
{
    return m_msg.kind == kMsgImage
        && EffectiveImageState() == kChatImageStateReady
        && !m_msg.imagePath.IsEmpty()
        && ::GetFileAttributesW((LPCWSTR)m_msg.imagePath) != INVALID_FILE_ATTRIBUTES;
}

bool ChatBubble::PointInTextSelection(POINT pt) const
{
    if (!HasTextSelection())
    {
        return false;
    }
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return false;
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    //按字形矩形判断：点在选中文字所在行的行尾空白处不算，免得"想重新选一段"被当成拖拽。
    const std::vector<std::pair<int, int> > ranges(1, std::make_pair(s, e));
    return HitTestCharRanges(m_selLayout, ranges, pt.x - origin.x, pt.y - origin.y) == 0;
}

void ChatBubble::BeginChatDrag()
{
    m_dragArmed = false;
    //拖拽期间由系统接管鼠标：先交还本控件的捕获，否则拖拽过程中收不到鼠标。
    ReleaseCapture();

    const std::vector<ChatClipSegment> segs =
        IsTextSelectable() ? GetSelectedClipSegments() : WholeMessageClipSegments();
    if (segs.empty())
    {
        return;
    }
    const int kind = (int)m_msg.kind;
    LOG_INFO("chat bubble: drag started, kind=%d, %u segment(s).", kind, (unsigned)segs.size());
    //模态：拖拽结束才返回。此后只用局部变量 —— 拖放期间界面可能已经重建，本对象未必还在。
    const DWORD effect = DoChatDragDrop(segs);
    LOG_INFO("chat bubble: drag finished, kind=%d, effect=%lu.", kind, (unsigned long)effect);
}

void ChatBubble::InvalidateSelectionChange(int oldAnchor, int oldCaret)
{
    // 新旧选区各自规整成 [起, 止)；任一端为 -1 或两端相同都算"没有选区"
    const bool hadSel = (oldAnchor >= 0 && oldCaret >= 0 && oldAnchor != oldCaret);
    const bool hasSel = HasTextSelection();
    int os = 0;
    int oe = 0;
    int ns = 0;
    int ne = 0;
    if (hadSel)
    {
        NormalizeSelRange(oldAnchor, oldCaret, os, oe);
    }
    if (hasSel)
    {
        NormalizeSelRange(m_selAnchor, m_selCaret, ns, ne);
    }

    // 高亮发生变化的字符区间：只有一边有选区时就是那一边；两边都有时是两者的"对称差"，即
    // 两个起点之间、两个终点之间各一段（拖选时锚点不动，只有终点那一段在变）
    std::vector<std::pair<int, int> > spans;
    if (hadSel && hasSel)
    {
        spans.push_back(std::make_pair((std::min)(os, ns), (std::max)(os, ns)));
        spans.push_back(std::make_pair((std::min)(oe, ne), (std::max)(oe, ne)));
    }
    else if (hadSel)
    {
        spans.push_back(std::make_pair(os, oe));
    }
    else if (hasSel)
    {
        spans.push_back(std::make_pair(ns, ne));
    }

    // 把这些区间占的行合成一个横条（行内相对坐标，左右铺满整行）
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    RECT band = { 0, 0, 0, 0 };
    bool any = false;
    if (ok)
    {
        const int rowW = m_rcItem.right - m_rcItem.left;
        for (size_t i = 0; i < spans.size(); ++i)
        {
            if (spans[i].second <= spans[i].first)
            {
                continue;
            }
            const std::vector<SelRect> rects = BuildSelectionRects(m_selLayout, spans[i].first, spans[i].second);
            for (size_t r = 0; r < rects.size(); ++r)
            {
                RECT line = { 0, origin.y - m_rcItem.top + rects[r].m_top,
                              rowW, origin.y - m_rcItem.top + rects[r].m_bottom };
                if (!any)
                {
                    band = line;
                    any = true;
                }
                else
                {
                    ::UnionRect(&band, &band, &line);
                }
            }
        }
    }

    if (!ok)
    {
        // 算不出文字区原点（尚未测量等）：退回整张重画
        Invalidate();
        return;
    }
    if (!any)
    {
        return;   // 高亮没有任何变化（如选区从无到"只有插入点"），不必重画
    }
    if (!m_renderDirty)
    {
        if (m_selBandDirty)
        {
            ::UnionRect(&m_selBandRect, &m_selBandRect, &band);
        }
        else
        {
            m_selBandRect = band;
            m_selBandDirty = true;
        }
    }
    // 只请求宿主重画，不置脏整张渲染缓存（ChatBubble::Invalidate 才会置脏）
    DuiControl::Invalidate();
}

POINT ChatBubble::TextOriginInHost(bool& okOut) const
{
    POINT origin;
    origin.x = 0;
    origin.y = 0;
    okOut = false;
    if (!IsTextSelectable() || m_measuredWidth < 0)
    {
        return origin;
    }

    // 与 RenderContent 里算 txRc 同口径：气泡左上角 + 内边距。气泡横向位置取决于
    // 是自己发的（靠右）还是对方发的（靠左），这里复用同一套几何。
    const int bubbleW = m_textBubbleW;
    int bubbleX = 0;
    if (m_msg.fromMe)
    {
        bubbleX = m_rcItem.right - kEdgePad - kAvatarSize - kAvatarGap - bubbleW;
    }
    else
    {
        bubbleX = m_rcItem.left + kEdgePad + kAvatarSize + kAvatarGap;
    }
    int bubbleY = m_rcItem.top + kRowVMargin;
    if (!m_msg.fromMe && !m_msg.senderName.IsEmpty())
    {
        bubbleY += kSenderNameHeight;   // 群聊：对方气泡上方有发送人名
    }
    origin.x = bubbleX + kTextPadH;
    origin.y = bubbleY + kTextPadV;
    if (m_msg.hasQuote)
    {
        // 引用块占据气泡内部顶端，正文从它下方开始。
        origin.y += kQuoteBlockH + kQuoteGap;
    }
    okOut = true;
    return origin;
}

int ChatBubble::CharIndexFromHostPoint(POINT pt) const
{
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return 0;
    }
    return CharIndexFromPoint(m_selLayout, pt.x - origin.x, pt.y - origin.y);
}

void ChatBubble::DrawTextSelection(HDC hdc, const POINT& origin) const
{
    if (!HasTextSelection())
    {
        return;
    }
    int s = 0;
    int e = 0;
    NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    const std::vector<SelRect> rects = BuildSelectionRects(m_selLayout, s, e);
    if (rects.empty())
    {
        return;
    }

    const COLORREF fill = m_msg.fromMe ? kSelHighlightOnSelf : kSelHighlightOnOther;
    HBRUSH brush = ::CreateSolidBrush(fill);
    if (brush == NULL)
    {
        return;
    }
    for (size_t i = 0; i < rects.size(); ++i)
    {
        RECT rc;
        rc.left   = origin.x + rects[i].m_left;
        rc.top    = origin.y + rects[i].m_top;
        rc.right  = origin.x + rects[i].m_right;
        rc.bottom = origin.y + rects[i].m_bottom;
        ::FillRect(hdc, &rc, brush);
    }
    ::DeleteObject(brush);
}

void ChatBubble::SetMessage(const ChatMessage& msg)
{
    m_msg = msg;
    // 换了消息：旧选区与旧排版全部作废。
    m_selAnchor   = -1;
    m_selCaret    = -1;
    m_selDragging = false;
    m_selLayout   = SelLayout();
    m_selFlatText.clear();
    // 内容变了，折行测量缓存作废，须重新 Measure。
    m_measuredWidth = -1;
    m_measuredLineW = -1;       // 测量阶段的排版结果随之作废，绘制时不再复用
    // @ 片段的扁平口径副本跟着作废。非文字消息的 Measure 会提前返回、不重建它，
    // 若不在这里清掉，一条"文字消息改成图片消息"的气泡会留着上一条的 @ 片段，
    // 右键时按早已不存在的区间做命中。
    m_atsFlat.clear();
    ::SetRectEmpty(&m_quoteRectRel);
    // 图钉区域同理：新消息未必被 Pin，下次绘制时再按新消息记录。鼠标若正停在旧图钉上，
    // 一并撤销悬停提示的登记。
    ::SetRectEmpty(&m_pinRectRel);
    POINT noPoint = { 0, 0 };
    SetPinHover(false, noPoint);
    // Markdown 文档按新消息重新解析（下次 Measure 时），解析失败的记录与卡片内容一并复位
    m_markdown.Reset();
    m_markdownActive = false;
    m_markdownFailed = false;
    m_mdCardTitle.clear();
    m_mdCardSummary.clear();
    m_mdCardSummaryH = 0;
    ::SetRectEmpty(&m_mdCardRel);
    // 代码卡片按新消息重新准备（下次 Measure 时），悬停与「已复制」状态一并复位
    m_codeContent = CodeBlockContent();
    m_codeLayout = CodeBlockLayout();
    m_codeActive = false;
    m_codePrepared = false;
    m_codeCopied = false;
    m_codeHover = kCodePartNone;
    ::SetRectEmpty(&m_codeCardRel);
    // 换了消息（可能换了发送者头像）：头像位图由 ChatAvatarBitmapCache 按路径共用，
    // 下次绘制按新消息的路径去取即可，本行不持有需要释放的位图。
    Invalidate();
}

// ---- 已读回执 ----------------------------------------------------------

ChatReceiptKind DecideReadReceipt(const ChatMessage& msg, bool receiptEnabled)
{
    // 群聊不开启回执：按协议群聊不做已读回执，服务端不推送 cmd_message_read_notify。
    if (!receiptEnabled)
    {
        return kReceiptNone;
    }
    // 只有自己发出的消息才有「对方读没读」。
    if (!msg.fromMe)
    {
        return kReceiptNone;
    }
    // 时间分隔行不是消息；文件夹行是本地气泡，不进服务端消息表（见 ChatMsgKind 的注释）。
    if (msg.kind == kMsgTime || msg.kind == kMsgFolder)
    {
        return kReceiptNone;
    }
    // 还没拿到服务器 msgid：发送中或发送失败，服务端都还没收到，谈不上对方读没读。
    // 已读位点也是按 msgid 比较的，没有 msgid 的消息无从判定。
    if (msg.msgid <= 0)
    {
        return kReceiptNone;
    }
    return msg.read ? kReceiptRead : kReceiptUnread;
}

void ChatBubble::SetSendFailed(const CString& reason)
{
    m_msg.sendFailed = true;
    m_msg.failReason = reason;
    //图片、文件行同时置为各自的失败态：气泡本体按原有的失败样式画（破图占位、文件失败状态与重试入口）
    if (m_msg.kind == kMsgImage)
    {
        m_msg.imageState = kChatImageStateFailed;
    }
    else if (m_msg.kind == kMsgFile)
    {
        m_msg.fileState = kFileFailed;
    }
    else if (m_msg.kind == kMsgVoice)
    {
        //语音：不再画「发送中」的圆环，改画红色感叹号（点它重发）
        m_msg.voiceState = kVoiceStateReady;
    }
    Invalidate();
}

bool ChatBubble::MarkPeerRead()
{
    if (m_msg.read)
    {
        return false;
    }
    m_msg.read = true;
    Invalidate();
    return true;
}

void ChatBubble::SetReadReceiptEnabled(bool b)
{
    if (m_readReceiptEnabled == b)
    {
        return;
    }
    m_readReceiptEnabled = b;
    Invalidate();
}

void ChatBubble::BackfillServerIds(long long msgid, long long timestampMs)
{
    // 这几个字段只被撤回时限判断与按 msgid 的定位读取，不进折行、不进绘制，所以这里
    // 既不置 m_renderDirty、也不动 m_measuredWidth / m_selLayout —— 保住已经算好的
    // 折行结果与选区模型，本行才不会在回执到达后突然选不中文字。
    m_msg.msgid       = msgid;
    m_msg.timestampMs = timestampMs;

    // 文件气泡另有一份 fileMsgid（与图片的 msgid 是各自独立的字段），本端发出的那条
    // 全靠这里回填 —— 不填的话它恒为 0，之后"对方收取到什么程度"的推送就按 msgid
    // 找不到本行，状态行永远不会变。
    if (m_msg.kind == kMsgFile)
    {
        m_msg.fileMsgid = msgid;
    }

    // 语音：回执成功，左侧「发送中」的圆环消失
    if (m_msg.kind == kMsgVoice && m_msg.voiceState == kVoiceStateSending)
    {
        m_msg.voiceState = kVoiceStateReady;
        Invalidate();
    }

    // 开启回执的行拿到 msgid 后，回执行从「不显示」变成「未读」，要重画一次。只重画、
    // 不重排：回执行的高度在拿到 msgid 之前就已经预留好了。
    if (m_readReceiptEnabled && m_msg.fromMe)
    {
        Invalidate();
    }
}

void ChatBubble::SetSenderAvatarImage(const CString& path)
{
    m_msg.senderAvatarImagePath = path;
    // 本方法在头像下载完成后调用。路径没变也要处理：同一路径的文件可能刚被新下载的
    // 头像覆盖，共享缓存里那张还是旧图（或是当初文件还不存在时记下的失败），得作废，
    // 下次绘制才会重新解码。同一发送者的多个气泡会各调一次，作废是幂等的，只在随后
    // 绘制时解码一次。空路径表示回退字形，没有缓存可作废。
    if (!path.IsEmpty())
    {
        ChatAvatarBitmapCache::Inst().Invalidate(std::wstring((LPCWSTR)path));
    }
    Invalidate();
}

void ChatBubble::SetAvatarGray(bool gray)
{
    if (m_avatarGray == gray)
    {
        return;
    }
    m_avatarGray = gray;
    //本类的同名 Invalidate 会把渲染缓存置脏：文字气泡的头像画在渲染缓存里，不置脏就仍显示旧颜色
    Invalidate();
}

bool ChatBubble::SetSenderInfo(const CString& name, const CString& glyph, COLORREF color,
                               const CString& imagePath)
{
    if (m_msg.senderName == name && m_msg.senderAvatarText == glyph
        && m_msg.senderAvatarColor == color && m_msg.senderAvatarImagePath == imagePath)
    {
        return false;
    }
    m_msg.senderName            = name;
    m_msg.senderAvatarText      = glyph;
    m_msg.senderAvatarColor     = color;
    m_msg.senderAvatarImagePath = imagePath;
    //本类的同名 Invalidate 会把渲染缓存置脏，文字气泡下一帧按新的名字与头像重画
    Invalidate();
    return true;
}

bool ChatBubble::SetSenderBot(bool isBot)
{
    if (m_msg.senderIsBot == isBot)
    {
        return false;
    }
    m_msg.senderIsBot = isBot;
    //本类的同名 Invalidate 会把渲染缓存置脏，下一帧按新的标记重画名字行
    Invalidate();
    return true;
}

void ChatBubble::SetImageUploadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.imageState    = 1;   // 上传中
    m_msg.imageProgress = percent;
    Invalidate();
}

void ChatBubble::SetImageUploadFailed()
{
    m_msg.imageState = 2;   // 失败
    Invalidate();
}

void ChatBubble::SetImageUploadDone()
{
    m_msg.imageState    = 0;   // 正常
    m_msg.imageProgress = 100;
    Invalidate();
}

void ChatBubble::SetImageDownloaded(const CString& localPath)
{
    m_msg.imagePath  = localPath;
    m_msg.imageState = 0;   // 正常（已就绪）
    // 换了图源（新本地路径）：下次绘制时 ChatImageCache 会按新路径解码 + 缩放并缓存。
    Invalidate();
}

void ChatBubble::SetImageDownloadFailed()
{
    //与上传失败分开记：两者显示的说明文字不同（"图片加载失败" / "图片发送失败"）。
    m_msg.imageState = kChatImageStateDownloadFailed;
    Invalidate();
}

void ChatBubble::SetVoiceDownloaded(const CString& localPath)
{
    m_msg.voiceLocalPath = localPath;
    m_msg.voiceState = kVoiceStateReady;
    Invalidate();
}

void ChatBubble::SetVoiceDownloadFailed()
{
    m_msg.voiceState = kVoiceStateDownloadFailed;
    Invalidate();
}

void ChatBubble::SetVoiceDownloading()
{
    m_msg.voiceState = kVoiceStateDownloading;
    Invalidate();
}

void ChatBubble::SetVoicePlaying(bool playing)
{
    m_msg.voicePlaying = playing;
    m_msg.voicePlayFrame = 0;
    //开始播放即算已听：对方发来的未听红点消失（本地库由宿主聊天窗另行标记）
    if (playing)
    {
        m_msg.voicePlayed = true;
    }
    Invalidate();
}

void ChatBubble::SetVoicePlayFrame(int frame)
{
    m_msg.voicePlayFrame = frame;
    Invalidate();
}

void ChatBubble::SetVoicePlayed()
{
    m_msg.voicePlayed = true;
    Invalidate();
}

int ChatBubble::EffectiveImageState() const
{
    if (m_msg.kind != kMsgImage || m_msg.imageState != kChatImageStateReady
        || m_msg.imagePath.IsEmpty() || m_msg.imageW <= 0 || m_msg.imageH <= 0)
    {
        return m_msg.imageState;
    }
    const std::wstring path((LPCWSTR)m_msg.imagePath);
    if (ChatImageCache::Inst().IsFailed(path, m_msg.imageW, m_msg.imageH))
    {
        return kChatImageStateDownloadFailed;
    }
    return m_msg.imageState;
}

// ---- 文件气泡态设置 ----
// 注：这些改 fileState 会改变气泡高度（FileCardHeight），故 ChatMessageList 在调用
// 后须 ForceLayout 重排；本处的 Invalidate 仅触发重绘，重排由列表负责。

void ChatBubble::SetFileUploadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.fileState    = 1;   // 上传中
    m_msg.fileProgress = percent;
    Invalidate();
}

void ChatBubble::SetFileUploadDone()
{
    m_msg.fileState    = 0;   // 完成
    m_msg.fileProgress = 100;
    Invalidate();
}

void ChatBubble::SetFileUploadFailed()
{
    m_msg.fileState = 2;   // 失败（保留当前进度作红色停位）
    Invalidate();
}

void ChatBubble::SetFileDownloadProgress(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_msg.fileState    = 3;   // 下载中
    m_msg.fileProgress = percent;
    Invalidate();
}

void ChatBubble::SetFileDownloaded(const CString& localPath)
{
    m_msg.fileLocalPath = localPath;
    m_msg.fileState     = 0;   // 完成
    m_msg.fileProgress  = 100;
    Invalidate();
}

void ChatBubble::SetFileDownloadFailed()
{
    m_msg.fileState = 2;   // 失败
    Invalidate();
}

void ChatBubble::SetFileCancelled()
{
    m_msg.fileState = 4;   // 已取消（保留当前进度作灰色停位）
    Invalidate();
}

void ChatBubble::SetFileDeleted()
{
    m_msg.fileState = 5;   // 已删除 / 失效
    Invalidate();
}

bool ChatBubble::CanResendFile() const
{
    //判定与文件卡片绘制「重新发送 / 重新下载」文案时用的是同一个函数（见 ChatFileCard.h）
    return FileCardCanResend(m_msg);
}

void ChatBubble::SetFileRejected()
{
    m_msg.fileState = 7;   // 已拒绝接收（可再点「重新下载」反悔）
    Invalidate();
}

void ChatBubble::SetFileDownloadStarted()
{
    m_msg.fileState    = 3;   // 下载中
    m_msg.fileProgress = 0;   // 从头开始，清掉上一轮可能残留的进度
    Invalidate();
}

void ChatBubble::SetFilePeerState(int peerState)
{
    m_msg.filePeerState = peerState;
    Invalidate();
}

void ChatBubble::SetFileUploadStarted()
{
    m_msg.fileState    = 1;   // 上传中
    m_msg.fileProgress = 0;   // 重发从头开始，清掉上一轮停在半截的进度
    //重发时清掉上一次被服务端拒绝留下的失败标记与原因：否则这次发送成功后红色感叹号与原因仍在。
    //群聊里原因行随之去掉、行高变矮，调用方（ChatMessageList::MarkFileUploadStartedByClientMsgId）会重排
    m_msg.sendFailed = false;
    m_msg.failReason.Empty();
    Invalidate();
}

bool ChatBubble::IsRealMessage() const
{
    // 时间分隔与"撤回了一条消息"提示行都用 kMsgTime 样式，均非真实消息。
    return m_msg.kind != kMsgTime;
}

bool ChatBubble::ParentInMultiSelect() const
{
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    return (list != NULL) && list->IsMultiSelectMode();
}

void ChatBubble::SetSelected(bool selected)
{
    if (m_selected != selected)
    {
        m_selected = selected;
        Invalidate();
    }
}

void ChatBubble::SetPinned(bool pinned)
{
    if (m_msg.pinned != pinned)
    {
        m_msg.pinned = pinned;
        //取消 Pin 时鼠标可能正停在图钉上（典型情形：刚点过它，服务端推送随即到达）。
        //图钉即将消失，悬停提示的登记要一并撤销，否则提示会留在空白处。
        if (!pinned)
        {
            POINT noPoint = { 0, 0 };
            SetPinHover(false, noPoint);
        }
        //Invalidate 是本类的同名版本，会顺带把渲染缓存置脏（见头文件说明），
        //故文字气泡下一帧会重画、图钉能真的出现或消失。
        Invalidate();
    }
}

void ChatBubble::RecallToTip(const CString& tipText)
{
    // 就地变成一条居中的系统提示行。
    m_msg.kind = kMsgTime;
    m_msg.text = tipText;
    m_msg.hasQuote = false;
    m_selected = false;
    m_measuredWidth = -1;
    m_measuredLineW = -1;       // 测量阶段的排版结果随之作废，绘制时不再复用
    // 撤回后本行变成系统提示，原消息的 @ 片段随之作废（理由同 SetMessage）。
    m_msg.ats.clear();
    m_atsFlat.clear();
    Invalidate();
}

// ====================================================================
// @ 片段的下标换算 —— 内联表情的排版与绘制已抽到 ChatInlineText.{h,cpp} 共用，
// 这里只剩气泡自己才有的这一件：把 @ 片段换算到排版模型认的扁平口径。
// ====================================================================

namespace {

// 把 @ 片段从「发送正文下标」换算到「扁平文本下标」。
//
// 两套下标只在正文里出现表情（/f["5"]）或转义斜杠（//）时才不同，但一旦不同，
// @ 的高亮区间就整体错位，所以必须显式换算，不能直接拿正文下标去查排版模型。
//   body：   发送正文（即 m_msg.text）。
//   ats：    正文口径的片段（来自协议换算，见 GroupMentionModel）。
//   flatLen：扁平文本长度，用于夹取，防越界。
// @return 扁平口径的片段；换算后落空的片段会被丢弃（不让坏数据毁掉整条渲染）。
std::vector<RenderSpan> ConvertAtsToFlat(const CString& body,
                                         const std::vector<RenderSpan>& ats,
                                         int flatLen)
{
    std::vector<RenderSpan> out;
    if (ats.empty() || flatLen <= 0)
    {
        return out;
    }
    const std::vector<int> map = balloonchat::BuildFlatIndexMap(body);
    const int rawLen = body.GetLength();
    out.reserve(ats.size());

    for (std::size_t i = 0; i < ats.size(); ++i)
    {
        const RenderSpan& src = ats[i];
        if (src.m_off < 0 || src.m_len <= 0 || src.m_off + src.m_len > rawLen)
        {
            continue;   // 越界片段丢弃 —— 上游已校验一遍，这里再挡一道
        }
        const int flatBegin = map[(std::size_t)src.m_off];
        const int flatEnd   = map[(std::size_t)(src.m_off + src.m_len)];
        if (flatEnd <= flatBegin || flatBegin < 0 || flatEnd > flatLen)
        {
            continue;   // 换算后为空区间（如整段都落在一个表情标记里）
        }

        RenderSpan dst = src;
        dst.m_off = flatBegin;
        dst.m_len = flatEnd - flatBegin;
        out.push_back(dst);
    }
    return out;
}


} // 匿名 namespace（@ 片段下标换算）

namespace {

// 按本条消息的字体量出单行文字气泡的高度（文字行高加上下内边距，不低于 kTextBubbleH）。语音气泡用它，
// 与单行文字气泡同高：两者用同一个字体与同一个排版函数，字号、界面语言的字体变了也保持一致。
// 量的是消息自身的文字（语音为占位文字「[语音]」，不含表情、不会折行）；取不到设备上下文或建不了字体时返回 kTextBubbleH
int MeasureSingleLineBubbleHeight(const ChatMessage& msg)
{
    int height = kTextBubbleH;
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return height;
    }
    const BubbleTextStyle st = ResolveTextStyle(msg);
    HFONT hFont = ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont != NULL)
    {
        HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(msg.text);
        //排版宽度给到语音气泡的最大宽度：占位文字很短，不会折成两行
        const SIZE sz = LayoutInlineContent(hdc, segs, kVoiceBubbleMaxWidth, kFaceDisplayPx, NULL);
        height = sz.cy + kTextPadV * 2;
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);
    if (height < kTextBubbleH)
    {
        height = kTextBubbleH;
    }
    return height;
}

} // namespace

void ChatBubble::Measure(int availWidth)
{
    // 接龙卡片：宽度固定，说明的折行与可用宽度无关，只在消息内容变过之后（SetMessage 把
    // m_measuredWidth 置为 -1）重算一次。说明的行数决定卡片高度，故必须在 GetRowHeight 之前算好。
    if (m_msg.kind == kMsgRelay)
    {
        if (m_measuredWidth < 0)
        {
            MeasureRelayNote();
        }
        m_measuredWidth = availWidth;
        return;
    }

    // 屏幕共享卡片（第 15 期）：宽度固定，描述的折行只在消息内容变过之后重算一次，行数决定卡片高度
    if (m_msg.kind == kMsgScreenShare)
    {
        if (m_measuredWidth < 0)
        {
            MeasureScreenShareDesc();
        }
        m_measuredWidth = availWidth;
        return;
    }

    // 语音气泡：一行高、与单行文字气泡同高。宽度随时长变化、与可用宽度无关，故只在消息内容变过之后
    // （SetMessage 把 m_measuredWidth 置为 -1）按本条消息的字体量一次高度
    if (m_msg.kind == kMsgVoice)
    {
        if (m_measuredWidth < 0)
        {
            m_textBubbleH = MeasureSingleLineBubbleHeight(m_msg);
        }
        m_measuredWidth = availWidth;
        return;
    }

    // 仅文字气泡需要折行测量；其余 kind 行高是定值，测量无副作用。仍记录
    // availWidth，便于 GetRowHeight 据 m_measuredWidth>=0 判定"已测量"。
    if (m_msg.kind != kMsgText)
    {
        m_measuredWidth = availWidth;
        return;
    }

    // 多选模式下内容区让出勾选列、气泡最大宽度随之收窄 —— 故多选状态与
    // availWidth 一起决定折行结果，二者共同构成折行缓存键。
    const bool multiSel = (ParentInMultiSelect() && IsRealMessage());

    // 折行缓存命中：宽度与多选状态都没变即直接复用上次结果（内容若变，
    // SetMessage / RecallToTip 已把 m_measuredWidth 置 -1，使下式不成立而
    // 强制重测）。命中时跳过昂贵的 GetDC + 建字体 + DrawText 折行测量 ——
    // 这是窗口缩放流畅的关键：一步缩放里的多遍同宽重排只在第一遍真测，
    // 纵向拖动（宽度不变）则完全不再重新折行。
    if (m_measuredWidth == availWidth && m_measuredMultiSel == multiSel)
    {
        return;
    }
    m_measuredWidth    = availWidth;
    m_measuredMultiSel = multiSel;
    // 真正重折行（宽度 / 多选态变了）→ 折行结果将变，渲染缓存置脏；垂直拖动走上面
    // 的早返回、不到这里，故不影响拖动性能。
    m_renderDirty = true;

    const int selW = multiSel ? kCheckColW : 0;
    int maxBubbleW = (int)((availWidth - selW) * kBubbleMaxWidthRatio);
    int maxTextW = maxBubbleW - kTextPadH * 2;
    if (maxTextW < kMinTextWidth)
    {
        maxTextW = kMinTextWidth;
    }

    m_textBubbleW = 0;
    m_textBubbleH = kTextBubbleH;

    // 走到这里即将执行一次真正的重量级折行测量 —— 计入性能探针。
    ++s_heavyMeasureCount;

    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return;
    }
    // 代码片段：画成代码卡片，尺寸取卡片的排版结果，不建普通文字与 Markdown 的模型
    if (MeasureCode(hdc, availWidth - selW))
    {
        ::ReleaseDC(NULL, hdc);
        return;
    }
    // Markdown 消息：DLL 可用且排版成功时，气泡尺寸取它的结果，不再建本类的字符级模型
    if (MeasureMarkdown(hdc, maxTextW, maxBubbleW))
    {
        ::ReleaseDC(NULL, hdc);
        return;
    }
    // 折行测量须按本条消息的实际字体（字号 / 粗体 / 斜体影响文字宽高），否则
    // 自定义字号的气泡会被错误地按默认 14px 测量而裁切或留白。
    const BubbleTextStyle st = ResolveTextStyle(m_msg);
    HFONT hFont = ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont != NULL)
    {
        HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);

        // 选区模型每次重测都要重建 —— 它记的是字符级坐标，宽度一变就全废。
        m_selLayout = SelLayout();
        m_selFlatText.clear();

        // 所有文字气泡 —— 纯文字、含内联表情、含 @提醒 —— 统一走手写折行排版。
        //
        // 含 @ 的曾经单独走 GDI+（当时只有它能按字符区间要到高亮矩形），代价是：
        //   · 选中文字用不了 —— GDI+ 的排版与本引擎产出的选区模型不同源，硬凑会错位，
        //     于是含 @ 的消息干脆禁止选中；
        //   · @ 与内联表情混排时，GDI+ 直接画正文原文，表情显示成 /f["5"] 这样的转义；
        //   · 两套排版并存，度量差异是长期的坑（气泡空白那次就是其一）。
        // 现在收敛成一条路：本引擎的 InlinePlaced 本来就带 charBegin / charCount 与矩形，
        // 字符级模型既够选中文字用，也够 @ 片段要矩形用，一份数据两用。
        //
        // 纯文字以前走 DrawText 一把梭，早先已改成与含表情的同一条路 —— 因为
        // "选中文字"要求把坐标映射到字符下标，而 DrawText 的折行是黑盒，做不到。
        std::vector<balloonchat::ChatSegment> segs =
            balloonchat::ParseSendBody(m_msg.text);
        std::vector<InlinePlaced> placed;
        SIZE sz = LayoutInlineContent(hdc, segs, maxTextW, kFaceDisplayPx, &placed);
        m_textBubbleW = sz.cx + kTextPadH * 2;
        m_textBubbleH = sz.cy + kTextPadV * 2;

        // 与排版同源地产出选区模型：同一次排版、同一套字体，选区矩形才不会与
        // 画出来的文字错位。
        m_selFlatText = BuildFlatText(segs);
        BuildSelLayoutFromPlaced(hdc, placed, (int)m_selFlatText.size(), m_selLayout);

        // 留下这次的放置项与折行参数，供 RenderContent 复用（见 m_measuredPlaced 的说明）
        m_measuredPlaced.swap(placed);
        m_measuredLineW = (int)sz.cx;
        m_measuredMaxTextW = maxTextW;

        // 顺带识别正文里的超链接。口径与选区模型一致（扁平文本下标），故渲染时能直接
        // 拿去要矩形、命中时能直接拿去做区间命中。识别开关关掉时这里得到空表，
        // 后面渲染 / 命中 / 菜单三处自然什么都不做。
        m_linksFlat.clear();
        //屏幕共享的分享链接（第 25 期）是站内功能，不受「识别链接」开关影响；网址按开关识别
        m_linksFlat = DetectLinks(m_selFlatText, ChatLinkOptionEnabled(), true);

        // 带引用的消息：引用块的高度与宽度下限，与 Markdown 路径共用
        ApplyQuoteToTextBubble(maxBubbleW);

        // @ 片段换算到扁平口径，与 m_selLayout 对齐，供画底色块与右键命中使用。
        m_atsFlat = ConvertAtsToFlat(m_msg.text, m_msg.ats, (int)m_selFlatText.size());
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);

    // 夹紧：高不小于单行高、宽不超过气泡最大宽。
    if (m_textBubbleH < kTextBubbleH)
    {
        m_textBubbleH = kTextBubbleH;
    }
    if (m_textBubbleW > maxBubbleW)
    {
        m_textBubbleW = maxBubbleW;
    }
}

void ChatBubble::ApplyQuoteToTextBubble(int maxBubbleW)
{
    // 带引用的消息：引用块占据气泡内部顶端一块固定高度，正文从它下方开始，
    // 故气泡总高要把它算进去（早先引用画在气泡外，是加在行高上的）。
    if (!m_msg.hasQuote)
    {
        return;
    }
    m_textBubbleH += kQuoteBlockH + kQuoteGap;

    // 气泡宽度也要照顾引用块：正文很短时（比如只回一个字），按正文算出来的
    // 气泡窄得放不下引用块里的缩略图与两行文字，内容会溢出到气泡外面去。
    // 故给带引用的气泡定一个下限：竖线 + 间距 +（有图时的缩略图）+ 一段文字。
    int quoteMinW = kTextPadH * 2 + kQuoteBarW + kQuoteBarGap + kQuoteMinTextW;
    if (m_msg.quoteMsgType == kMsgImage)
    {
        quoteMinW += kQuoteThumbSize + kQuoteThumbGap;
    }
    if (quoteMinW > maxBubbleW)
    {
        quoteMinW = maxBubbleW;   // 再窄也不能超过气泡最大宽
    }
    if (m_textBubbleW < quoteMinW)
    {
        m_textBubbleW = quoteMinW;
    }
}

bool ChatBubble::MeasureMarkdown(HDC hdc, int maxTextW, int maxBubbleW)
{
    m_markdownActive = false;
    if (m_msg.kind != kMsgText || !m_msg.isMarkdown || m_msg.isCode || m_markdownFailed)
    {
        return false;
    }
    // 解析只在内容变化后做一次（SetMessage 会释放文档）；DLL 尚未就绪时不记失败，下次测量再试
    if (!m_markdown.IsParsed())
    {
        if (!markdown::IsMarkdownAvailable())
        {
            return false;
        }
        if (!m_markdown.Parse(MarkdownSourceFromBody(m_msg.text), MarkdownImageLinkPrefix()))
        {
            m_markdownFailed = true;
            return false;
        }
    }

    //卡片文字：标题优先取第一个标题块；旧版 DLL 没有这个接口时取渲染后文字的第一行作标题、其余作摘要（ChatMarkdownCard）
    GetMarkdownCardText(m_markdown, m_mdCardTitle, m_mdCardSummary);

    //卡片宽度与合并转发卡片相同，不超过本行允许的最大宽度
    int cardW = kMarkdownCardWidth;
    if (cardW > maxBubbleW)
    {
        cardW = maxBubbleW;
    }
    (void)maxTextW;
    //摘要按卡片内宽折行，最多三行
    m_mdCardSummaryH = MeasureMarkdownCardSummary(hdc, m_mdCardSummary, cardW);

    // 普通文字的字符级模型一律清空：它们只属于普通文字路径，留着会让命中、菜单按旧内容判断
    m_selLayout = SelLayout();
    m_selFlatText.clear();
    m_measuredPlaced.clear();
    m_measuredLineW = -1;
    m_linksFlat.clear();
    m_atsFlat.clear();

    m_markdownActive = true;
    m_textBubbleW = cardW;
    m_textBubbleH = MarkdownCardHeight(m_mdCardSummaryH);
    return true;
}

void ChatBubble::MeasureRelayNote()
{
    m_relayNoteLines.clear();
    if (m_msg.relayNote.IsEmpty())
    {
        return;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return;
    }
    // 字体参数与绘制说明时 DrawGdiText 建的字体完全一致，量出来的宽度才与画出来的相符
    HFONT hFont = ::CreateFontW(
        -kRelayNotePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont != NULL)
    {
        HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
        WrapRelayNote(hdc, m_msg.relayNote, kRelayBubbleW - kRelayPadH * 2, kRelayNoteMaxLines,
                      m_relayNoteLines);
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);
}

void ChatBubble::MeasureScreenShareDesc()
{
    m_shareDescLines.clear();
    if (m_msg.screenShareDesc.IsEmpty())
    {
        return;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return;
    }
    // 字体参数与绘制描述时 DrawGdiText 建的字体完全一致，量出来的宽度才与画出来的相符
    HFONT hFont = ::CreateFontW(
        -kShareDescPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont != NULL)
    {
        HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
        WrapRelayNote(hdc, m_msg.screenShareDesc, kShareBubbleW - kSharePadH * 2, kShareDescLines, m_shareDescLines);
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);
}

bool ChatBubble::RelayTipAtPoint(POINT pt) const
{
    if (m_msg.kind != kMsgTime || m_msg.relayCardMsgid <= 0 || ParentInMultiSelect())
    {
        return false;
    }
    if (!::PtInRect(&m_rcItem, pt))
    {
        return false;
    }
    // 提示文字居中画在整行里，只有落在文字范围内才算点中，行两侧的空白不算
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return false;
    }
    const int textW = MeasureTextWidth(hdc, (LPCWSTR)m_msg.text, kTimeTextPx);
    ::ReleaseDC(NULL, hdc);
    const int center = (m_rcItem.left + m_rcItem.right) / 2;
    return pt.x >= center - textW / 2 && pt.x <= center + textW / 2;
}

void ChatBubble::SetRecallEdit(const std::string& draftJson, long long deadlineMs)
{
    m_msg.recallDraftJson = draftJson;
    m_msg.recallEditDeadlineMs = draftJson.empty() ? 0 : deadlineMs;
    m_recallEditHover = false;
    m_recallEditPressed = false;
    Invalidate();
}

bool ChatBubble::HasRecallEdit(long long nowMs) const
{
    return m_msg.kind == kMsgTime && !m_msg.recallDraftJson.empty()
        && IsRecallEditAlive(m_msg.recallEditDeadlineMs, nowMs);
}

bool ChatBubble::ExpireRecallEdit(long long nowMs)
{
    if (m_msg.recallDraftJson.empty() || IsRecallEditAlive(m_msg.recallEditDeadlineMs, nowMs))
    {
        return false;
    }
    SetRecallEdit(std::string(), 0);
    return true;
}

void ChatBubble::RecallEditLayout(HDC hdc, int& tipLeft, int& linkLeft, int& linkWidth) const
{
    const int tipW = MeasureTextWidth(hdc, (LPCWSTR)m_msg.text, kTimeTextPx);
    linkWidth = MeasureTextWidth(hdc, TR(L"编辑"), kTimeTextPx);
    const int total = tipW + kRecallEditGapPx + linkWidth;
    const int center = (m_rcItem.left + m_rcItem.right) / 2;
    tipLeft = center - total / 2;
    linkLeft = tipLeft + tipW + kRecallEditGapPx;
}

bool ChatBubble::RecallEditAtPoint(POINT pt) const
{
    if (ParentInMultiSelect() || !HasRecallEdit(RecallEditNowMs()))
    {
        return false;
    }
    if (!::PtInRect(&m_rcItem, pt))
    {
        return false;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return false;
    }
    int tipLeft = 0;
    int linkLeft = 0;
    int linkWidth = 0;
    RecallEditLayout(hdc, tipLeft, linkLeft, linkWidth);
    ::ReleaseDC(NULL, hdc);
    //只有「编辑」两个字可点，前面的提示文字与行两侧的空白都不算
    return pt.x >= linkLeft && pt.x <= linkLeft + linkWidth;
}

void ChatBubble::PostRecallEditRequest()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    // 异步 PostMessage，故堆分配传递；宿主处理完 delete，投递失败自行释放。
    ChatReeditData* d = new ChatReeditData();
    if (!ParseRecallDraft(m_msg.recallDraftJson, *d))
    {
        LOG_WARNING("chat bubble: recall draft of msgid=%lld cannot be parsed, edit ignored.", m_msg.msgid);
        delete d;
        return;
    }
    LOG_INFO("chat bubble: recall edit requested, msgid=%lld, segments=%u, markdown=%d, code=%d.", m_msg.msgid,
             (unsigned)d->m_segments.size(), d->m_isMarkdown ? 1 : 0, d->m_isCode ? 1 : 0);
    if (!::PostMessage(hwnd, WM_CHAT_REEDIT_REQUEST, 0, (LPARAM)d))
    {
        delete d;
    }
}

void ChatBubble::PostRelayAction(long long cardMsgid, int action)
{
    if (cardMsgid <= 0)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    long long* pMsgid = new long long(cardMsgid);
    if (!::PostMessage(hwnd, WM_CHAT_RELAY_ACTION, (WPARAM)action, (LPARAM)pMsgid))
    {
        delete pMsgid;
    }
}

void ChatBubble::PostShareOpen()
{
    if (m_msg.shareId <= 0 || !m_msg.shareButtonEnabled)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    long long* pShareId = new long long(m_msg.shareId);
    //本人发起的共享带上标记，宿主据此在本机打开，而不是打开浏览窗口
    const WPARAM own = m_msg.shareIsOwner ? kChatOpenShareOwn : 0;
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_SHARE, own, (LPARAM)pShareId))
    {
        delete pShareId;
    }
}

bool ChatBubble::GroupInviteButtonClickable() const
{
    return m_msg.kind == kMsgGroupInvite && m_msg.inviteHasButton && !m_msg.inviteJoining && m_msg.inviteId > 0;
}

void ChatBubble::PostGroupInviteJoin()
{
    if (!GroupInviteButtonClickable())
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    GroupInviteJoinRequest* pReq = new GroupInviteJoinRequest();
    pReq->m_msgid = m_msg.msgid;
    pReq->m_inviteId = m_msg.inviteId;
    if (!::PostMessage(hwnd, WM_CHAT_JOIN_GROUP_INVITE, 0, (LPARAM)pReq))
    {
        delete pReq;
    }
}

int ChatBubble::SecretInviteButtonAt(POINT pt) const
{
    if (m_msg.kind != kMsgSecretInvite || m_msg.secretInviteButtons <= 0 || m_msg.secretSessionId == 0
        || ParentInMultiSelect() || !::PtInRect(&m_shareBtnRect, pt))
    {
        return -1;
    }
    //两个按钮时以按钮行正中为界，与绘制时的竖向分隔线同一口径；分界点归右侧的「忽略」
    if (m_msg.secretInviteButtons > 1)
    {
        const int midX = m_shareBtnRect.left + (m_shareBtnRect.right - m_shareBtnRect.left) / 2;
        return (pt.x < midX) ? 0 : 1;
    }
    return 0;
}

void ChatBubble::PostSecretInviteAction(int index)
{
    const int action = SecretInviteButtonAction(m_msg.secretInviteButtons, index);
    if (action == 0 || m_msg.secretSessionId == 0)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    unsigned long long* pSessionId = new unsigned long long(m_msg.secretSessionId);
    if (!::PostMessage(hwnd, WM_CHAT_SECRET_INVITE_ACTION, (WPARAM)action, (LPARAM)pSessionId))
    {
        delete pSessionId;
    }
}

bool ChatBubble::IsSecretRow() const
{
    return m_msg.isSecret || m_msg.kind == kMsgSecretInvite;
}

bool ChatBubble::IsSecretLocked() const
{
    if (IsSecretRow())
    {
        return true;
    }
    const ChatMessageList* list = dynamic_cast<const ChatMessageList*>(GetParent());
    return list != NULL && list->IsSecretRestricted();
}

void ChatBubble::WipeSecretText()
{
    const int len = m_msg.text.GetLength();
    if (len > 0)
    {
        ::SecureZeroMemory(m_msg.text.GetBuffer(), static_cast<size_t>(len) * sizeof(wchar_t));
        m_msg.text.ReleaseBuffer(0);
    }
    if (!m_selFlatText.empty())
    {
        ::SecureZeroMemory(&m_selFlatText[0], m_selFlatText.size() * sizeof(wchar_t));
        m_selFlatText.clear();
    }
}

void ChatBubble::PostVoiceAction(int action)
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    VoiceActionRequest* pReq = new VoiceActionRequest();
    pReq->m_msgid = m_msg.msgid;
    pReq->m_clientMsgId = m_msg.clientMsgId;
    if (!::PostMessage(hwnd, WM_CHAT_VOICE_ACTION, (WPARAM)action, (LPARAM)pReq))
    {
        delete pReq;
    }
}

void ChatBubble::PostScreenShareJoin()
{
    if (m_msg.screenShareRoomId.empty() || !m_msg.screenShareButtonEnabled)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    ScreenShareJoinRequest* pReq = new ScreenShareJoinRequest();
    pReq->m_roomId = m_msg.screenShareRoomId;
    pReq->m_hostName = m_msg.screenShareHostName;
    pReq->m_forced = m_msg.screenShareForced;
    pReq->m_groupCall = m_msg.screenShareIsGroupCall;
    if (!::PostMessage(hwnd, WM_CHAT_JOIN_SCREENSHARE, 0, (LPARAM)pReq))
    {
        delete pReq;
    }
}

void ChatBubble::PostInfoCardOpen()
{
    if (m_msg.infoCardRefId <= 0 || !m_msg.screenShareButtonEnabled
        || (!m_msg.screenShareIsAnnouncement && !m_msg.screenShareIsMemo))
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    InfoCardOpenRequest* pReq = new InfoCardOpenRequest();
    pReq->m_msgid = m_msg.msgid;
    pReq->m_refId = m_msg.infoCardRefId;
    const UINT msgId = m_msg.screenShareIsAnnouncement ? WM_CHAT_OPEN_ANNOUNCEMENT_CARD : WM_CHAT_OPEN_MEMO_CARD;
    if (!::PostMessage(hwnd, msgId, 0, (LPARAM)pReq))
    {
        delete pReq;
    }
}

void ChatBubble::PostCloudRecordOpen()
{
    if (m_msg.infoCardRefId <= 0 || !m_msg.screenShareButtonEnabled || !m_msg.screenShareIsCloudRecord)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    CloudRecordOpenRequest* pReq = new CloudRecordOpenRequest();
    pReq->m_recordId = m_msg.infoCardRefId;
    pReq->m_title = m_msg.screenShareTitle;
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_CLOUD_RECORD_CARD, 0, (LPARAM)pReq))
    {
        delete pReq;
    }
}

int ChatBubble::GetRowHeight() const
{
    if (m_msg.kind == kMsgTime)
    {
        return kTimeRowHeight;
    }

    int bubbleH = kTextBubbleH;
    switch (m_msg.kind)
    {
    case kMsgText:
        bubbleH = (m_measuredWidth >= 0) ? m_textBubbleH : kTextBubbleH;
        break;
    case kMsgEmoji:
        bubbleH = kEmojiBubbleH;
        break;
    case kMsgImage:
        bubbleH = m_msg.imageH + kImageBubblePad;
        break;
    case kMsgFile:
        // 文件气泡高随状态变化（有无进度条 / 按钮行）。
        bubbleH = FileCardHeight(m_msg);
        break;
    case kMsgFolder:
        // 文件夹气泡高随状态变化（有无进度条 / 失败原因行 / 按钮行）。
        bubbleH = FolderBubbleHeight(m_msg.folderState, m_msg.fromMe, m_msg.folderErrCode);
        break;
    case kMsgMerged:
        // 合并卡片高随预览行数变化。
        bubbleH = MergedBubbleHeight(m_msg.mergedLines.size());
        break;
    case kMsgRelay:
        // 接龙卡片高随说明行数、接龙条数与有无按钮变化。说明行数由 Measure 算好。
        bubbleH = RelayBubbleHeight(m_relayNoteLines.size(), m_msg.relayLines.size(),
                                    RelayButtonLabel(m_msg.relayButton) != NULL);
        break;
    case kMsgFileShare:
        // 共享卡片高随有无路径行、有无按钮变化。
        bubbleH = ShareBubbleHeight(!m_msg.sharePath.IsEmpty(), ShareCardButtonLabel(m_msg.shareButton) != NULL);
        break;
    case kMsgScreenShare:
        // 屏幕共享卡片：副标题行始终有，高随有无按钮变化。
        bubbleH = ShareBubbleHeight(true, m_msg.screenShareHasButton, !m_msg.screenShareNotice.IsEmpty(),
                                    m_shareDescLines.size());
        break;
    case kMsgGroupInvite:
        // 入群邀请卡片：与屏幕共享卡片同一套几何，副标题行（群名与成员数）始终有，高随有无按钮变化。
        bubbleH = ShareBubbleHeight(true, m_msg.inviteHasButton);
        break;
    case kMsgSecretInvite:
        // 密聊邀请卡片：与入群邀请卡片同一套几何，说明行始终有，高随有无按钮变化。
        bubbleH = ShareBubbleHeight(true, m_msg.secretInviteButtons > 0);
        break;
    case kMsgVoice:
        // 语音气泡：一行高，与单行文字气泡相同（高度由 Measure 按字体量出；还没测量时用回退值）
        bubbleH = (m_measuredWidth >= 0) ? m_textBubbleH : kTextBubbleH;
        break;
    default:
        bubbleH = kTextBubbleH;
        break;
    }

    int stackH = bubbleH;
    // 引用块现在画在气泡内部，其高度已由 Measure 计入 m_textBubbleH，这里不能再加
    // 一次，否则每条带引用的消息都会多留出一块空白。
    if (!m_msg.fromMe && !m_msg.senderName.IsEmpty())
    {
        // 群聊：对方气泡上方显示发送者名，占一行。
        stackH += kSenderNameHeight;
    }
    // 回执行只在开启回执的列表（单聊）里为自己的消息预留。尚未拿到 msgid 的消息同样
    // 预留：msgid 回填后回执从无到有，行高不能跟着跳。群聊不预留，否则自己的消息下方
    // 会多出一行空白。
    if (m_msg.fromMe && m_readReceiptEnabled)
    {
        stackH += kReceiptHeight;
    }
    // 发送失败的原因行：单聊已为回执预留了一行，失败的消息没有回执，原因就画在那里；
    // 群聊不预留回执行，另加一行。
    if (m_msg.fromMe && m_msg.sendFailed && !m_readReceiptEnabled)
    {
        stackH += kFailReasonHeight;
    }
    if (stackH < kAvatarSize)
    {
        stackH = kAvatarSize;
    }
    return stackH + kRowVMargin * 2;
}

bool ChatBubble::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 点在已 Pin 消息旁的图钉上：请求取消 Pin，与右键菜单「取消 Pin」走同一条路径
    // （RequestPinBubble），界面等服务端推送 cmd_message_pinned_notify 回来再改。
    // 必须放在最前：图钉画在气泡外侧、仍属于本行，而文字气泡在本行内任意位置按下都会
    // 开始拖选文字，不先判断图钉的话这次点击会被文字选择处理掉。多选模式下
    // PinIconAtPoint 恒为 false，整行照旧作为勾选目标。
    if (PinIconAtPoint(pt))
    {
        LOG_INFO("chat bubble: pin icon clicked, request unpin, msgid=%lld.", m_msg.msgid);
        //先收起悬停提示：点击后鼠标仍停在图钉上，提示不收起的话会一直浮在那里，
        //直到服务端推送到达、图钉消失。
        balloonwjui::DuiToolTipMgr::Inst().OnLeave(this);
        ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
        if (list != NULL)
        {
            list->RequestPinBubble(this, false);
        }
        return true;
    }

    // 点在引用块上：请宿主定位到被引用的原消息。放在最前 —— 否则这一下会被下面的
    // 拖拽选择吃掉，引用块就点不动了。多选模式下整行是勾选目标，不做定位。
    if (!ParentInMultiSelect() && m_msg.hasQuote && m_msg.quoteMsgid != 0
        && !::IsRectEmpty(&m_quoteRectRel))
    {
        RECT hit = m_quoteRectRel;
        ::OffsetRect(&hit, m_rcItem.left, m_rcItem.top);
        if (::PtInRect(&hit, pt))
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (::IsWindow(hwnd))
            {
                long long* pMsgid = new long long(m_msg.quoteMsgid);
                if (!::PostMessage(hwnd, WM_CHAT_LOCATE_QUOTED, 0, (LPARAM)pMsgid))
                {
                    delete pMsgid;
                }
            }
            return true;
        }
    }

    // 代码卡片：点「复制」复制原文；点底栏「查看全部」或卡片的其它位置打开代码查看窗。多选模式下整行是勾选目标
    if (!ParentInMultiSelect() && CodeActive())
    {
        const int part = CodePartAtPoint(pt);
        if (part == kCodePartCopy)
        {
            balloonwjui::DuiHost* host = GetHost();
            CopyCodeToClipboard((host != NULL) ? host->m_hWnd : NULL);
            return true;
        }
        if (part != kCodePartNone)
        {
            PostCodeViewerOpen();
            return true;
        }
    }

    // Markdown 卡片：点卡片任意位置打开 Markdown 预览窗口（docs/Markdown消息方案.md 第 11.3 节）。多选模式下整行是勾选目标
    if (!ParentInMultiSelect() && PointInMarkdownCard(pt))
    {
        PostMarkdownPreviewOpen();
        return true;
    }

    // 文字气泡：非多选模式下，左键按下即开始拖拽选择文字。
    // 放在最前 —— 多选模式下整行是勾选目标，不做文字选择（下面那支会先返回）。
    if (!ParentInMultiSelect() && IsTextSelectable())
    {
        // 按在已选中的文字上：先不动选区，准备把这段内容拖出去（见 ChatDragSource.h）。
        // 移动超过系统拖动阈值才真正开始拖；没拖就松手，按普通单击处理（见 OnLButtonUp）。
        if (PointInTextSelection(pt) && IsChatDragOrigin(m_msg.kind, true, false, false) && !IsSecretLocked())
        {
            m_dragArmed    = true;
            m_dragPressPt  = pt;
            m_linkPressIdx = LinkAtPoint(pt);   //没拖就松手时，仍按单击处理链接
            Capture();
            return true;
        }

        // 记下按下时命中的链接（没命中为 -1）。这一下<u>不</u>消费 —— 按下仍要照常
        // 开始拖拽选择，链接是在抬起时才判定的：只有"按下与抬起落在同一条链接上、
        // 且期间没有真的选出一段文字"才算点击，否则从链接上起手选文字就会被误当成点击。
        m_linkPressIdx = LinkAtPoint(pt);

        // 同一时刻只允许一个气泡有选区 —— 在别的气泡上开始新选择时，先清掉旧的，
        // 否则会出现两条消息同时高亮、而"复制"只取其中一条的割裂感。
        ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
        if (list != NULL)
        {
            list->ClearTextSelectionExcept(this);
        }

        const int idx = CharIndexFromHostPoint(pt);
        const int oldAnchor = m_selAnchor;
        const int oldCaret  = m_selCaret;
        m_selAnchor   = idx;
        m_selCaret    = idx;
        m_selDragging = true;
        // 抓取鼠标：拖出气泡甚至拖出窗口都要继续收到移动事件，否则一出界选区就断了。
        Capture();
        InvalidateSelectionChange(oldAnchor, oldCaret);
        return true;   // 消费掉，避免这次点击被别处再解释一遍
    }

    // 多选模式下、且本行是真实消息时，左键点击切换选中态（此模式下文件按钮不响应）。
    if (ParentInMultiSelect())
    {
        //密聊消息与密聊邀请卡片不能多选（docs/密聊方案.md 4.5）
        if (!IsRealMessage() || IsSecretRow())
        {
            return false;
        }
        ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
        m_selected = !m_selected;
        Invalidate();
        if (list != NULL)
        {
            list->NotifySelectionChanged();
        }
        return true;
    }

    // 非多选：按在合并卡片上，抬起时仍在卡片内才打开聊天记录查看窗（见 OnLButtonUp）。
    // 抓取鼠标，拖出卡片甚至拖出窗口再松手时也能收到抬起、复位按下状态
    if (m_msg.kind == kMsgMerged && ::PtInRect(&m_mergedRect, pt))
    {
        m_mergedPressed = true;
        Capture();
        return true;
    }

    // 非多选：按在语音气泡上播放 / 停止，按在发送失败的红色感叹号上重发；抬起时仍在原处才算（见 OnLButtonUp）
    if (m_msg.kind == kMsgVoice)
    {
        if (::PtInRect(&m_voiceFailRect, pt))
        {
            m_voiceFailPressed = true;
            Capture();
            return true;
        }
        if (::PtInRect(&m_voiceRect, pt))
        {
            m_voicePressed = true;
            Capture();
            return true;
        }
    }

    // 非多选：按在可点的共享卡片上，抬起时仍在卡片内才打开共享（见 OnLButtonUp），理由同合并卡片
    if (((m_msg.kind == kMsgFileShare && m_msg.shareButtonEnabled)
         || (m_msg.kind == kMsgScreenShare && m_msg.screenShareButtonEnabled))
        && ::PtInRect(&m_shareRect, pt))
    {
        m_sharePressed = true;
        Capture();
        return true;
    }

    // 非多选：入群邀请卡片只有按钮那一行可点（整张卡片可点容易误点就入了群），抬起时仍在按钮行内才加入
    if (m_msg.kind == kMsgGroupInvite && GroupInviteButtonClickable() && ::PtInRect(&m_shareBtnRect, pt))
    {
        m_sharePressed = true;
        Capture();
        return true;
    }

    // 非多选：密聊邀请卡片同样只有按钮行可点，记下按在第几个按钮上，抬起时仍在同一个按钮上才执行（见 OnLButtonUp）
    {
        const int secretBtn = SecretInviteButtonAt(pt);
        if (secretBtn >= 0)
        {
            m_secretBtnPressed = secretBtn;
            Capture();
            return true;
        }
    }

    // 非多选：按在接龙卡片上（或带 cardmsgid 的接龙提示行的文字上），抬起时仍在原处才打开
    // 详情窗（见 OnLButtonUp），理由同合并卡片
    if ((m_msg.kind == kMsgRelay && ::PtInRect(&m_relayRect, pt)) || RelayTipAtPoint(pt))
    {
        m_relayPressed = true;
        Capture();
        return true;
    }

    // 非多选：按在撤回提示行的「编辑」上，抬起时仍在「编辑」上才把原文放回输入框（见 OnLButtonUp）
    if (RecallEditAtPoint(pt))
    {
        m_recallEditPressed = true;
        Capture();
        return true;
    }

    // 非多选：文件气泡底部操作链接命中 → 发动作给宿主聊天窗（打开 / 打开所在文件夹 /
    // 另存为 / 取消 / 重试）。
    if (m_msg.kind == kMsgFile)
    {
        int idx = FileLinkAtPoint(pt);
        if (idx >= 0)
        {
            HandleFileButton(m_fileBtnAction[idx]);
            return true;
        }
    }

    // 非多选：文件夹气泡底部操作链接命中 → 发动作给宿主聊天窗（接收 / 另存为 / 拒绝 /
    // 取消 / 重试续传 / 重新发送 / 打开文件夹）。不可用的按钮在 FolderLinkAtPoint 里就已被
    // 排除，走不到这里。
    if (m_msg.kind == kMsgFolder)
    {
        int idx = FolderLinkAtPoint(pt);
        if (idx >= 0)
        {
            HandleFolderButton(m_folderBtnAction[idx]);
            return true;
        }
    }

    // 非多选：按在本地原图可用的图片上，准备把图片拖出去。不消费这一下 —— 单击、双击
    // 放大都照旧；只有移动超过系统拖动阈值才真正开始拖。
    // 密聊图片不能拖出（docs/密聊方案.md 4.5）
    if (m_msg.kind == kMsgImage && ::PtInRect(&m_imageRect, pt) && !IsSecretLocked()
        && IsChatDragOrigin(m_msg.kind, false, true, IsLocalImageAvailable()))
    {
        m_dragArmed   = true;
        m_dragPressPt = pt;
        Capture();
    }
    return false;
}

void ChatBubble::HandleFileButton(int action)
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }

    // 组负载发给宿主聊天窗；宿主处理后 delete（异步 PostMessage，故堆分配传递）。
    ChatFileActionData* d = new ChatFileActionData();
    d->action      = action;
    d->fromMe      = m_msg.fromMe;
    d->clientMsgId = m_msg.fileClientMsgId;
    d->msgid       = m_msg.fileMsgid;
    d->localPath   = m_msg.fileLocalPath;
    d->fileName    = m_msg.fileName;
    d->fileId      = m_msg.fileFileId;
    d->md5         = m_msg.fileMd5;
    d->sizeBytes   = m_msg.fileSizeBytes;
    d->direct      = m_msg.fileDirect;
    d->directSessionId = m_msg.fileDirectSessionId;
    ::PostMessage(hwnd, WM_CHAT_FILE_ACTION, 0, (LPARAM)d);
}

int ChatBubble::FileLinkAtPoint(POINT pt) const
{
    if (m_msg.kind != kMsgFile)
    {
        return -1;
    }
    for (int bi = 0; bi < m_fileBtnCount; ++bi)
    {
        if (::PtInRect(&m_fileBtnRect[bi], pt))
        {
            return bi;
        }
    }
    return -1;
}

void ChatBubble::HandleFolderButton(int action)
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }

    // 组负载发给宿主聊天窗；宿主处理后 delete（异步 PostMessage，故堆分配传递）。
    // 气泡不认识传输会话，也不碰资源管理器与网络，只如实上报"这条气泡上点了哪个动作"。
    ChatFolderActionData* d = new ChatFolderActionData();
    d->action     = action;
    d->fromMe     = m_msg.fromMe;
    d->sessionId  = m_msg.folderSessionId;
    d->folderName = m_msg.folderName;
    d->localRoot  = m_msg.folderLocalRoot;
    d->fileCount  = m_msg.folderFileCount;
    d->sizeBytes  = m_msg.folderSizeBytes;
    if (!::PostMessage(hwnd, WM_CHAT_FOLDER_ACTION, 0, (LPARAM)d))
    {
        delete d;
    }
}

int ChatBubble::FolderLinkAtPoint(POINT pt) const
{
    if (m_msg.kind != kMsgFolder)
    {
        return -1;
    }
    for (int bi = 0; bi < m_folderBtnCount; ++bi)
    {
        //置灰的按钮只是告知"有这条路、眼下走不通"，不参与命中：既不给手型光标，
        //也不给悬停高亮，更不该点得动
        if (!m_folderBtnEnabled[bi])
        {
            continue;
        }
        if (::PtInRect(&m_folderBtnRect[bi], pt))
        {
            return bi;
        }
    }
    return -1;
}

bool ChatBubble::PinIconAtPoint(POINT pt) const
{
    // 取消 Pin 请求要带服务器 msgid，没有 msgid 时发出去必被判为消息不存在；
    // 多选模式下整行是勾选目标，图钉不单独响应。
    if (!m_msg.pinned || !IsRealMessage() || m_msg.msgid <= 0 || ParentInMultiSelect())
    {
        return false;
    }
    if (::IsRectEmpty(&m_pinRectRel))
    {
        return false;   // 图钉还没画过，无从判断它在哪里
    }
    RECT hit = m_pinRectRel;
    ::OffsetRect(&hit, m_rcItem.left, m_rcItem.top);
    return ::PtInRect(&hit, pt) != FALSE;
}

void ChatBubble::SetPinHover(bool hover, POINT ptHost)
{
    if (hover == m_pinHover)
    {
        return;
    }
    m_pinHover = hover;

    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();
    // 先复位：DuiToolTipMgr 对同一控件「已在等待显示」时会直接返回，不复位就无法
    // 按新位置重新计时（做法与 ImageViewerToolbar::UpdateTooltip 相同）。
    tips.OnLeave(this);
    if (!hover)
    {
        tips.Unregister(this);
        return;
    }

    tips.Register(this, TR(kPinTooltipText));
    POINT screen = ptHost;
    balloonwjui::DuiHost* host = GetHost();
    if (host != NULL && ::IsWindow(host->m_hWnd))
    {
        ::ClientToScreen(host->m_hWnd, &screen);
    }
    tips.OnHover(this, screen);
}

bool ChatBubble::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    // 按在选区内 / 图片上之后移动：超过系统拖动阈值就开始把内容拖出去。
    if (m_dragArmed)
    {
        if (IsBeyondDragThreshold(pt.x - m_dragPressPt.x, pt.y - m_dragPressPt.y,
                                  ::GetSystemMetrics(SM_CXDRAG), ::GetSystemMetrics(SM_CYDRAG)))
        {
            BeginChatDrag();   //模态，拖拽结束才返回；返回后不再访问成员
        }
        return true;
    }

    // 正在拖拽选择文字：把当前点映射成字符下标作为选区另一端。
    if (m_selDragging)
    {
        const int idx = CharIndexFromHostPoint(pt);
        if (idx != m_selCaret)
        {
            const int oldCaret = m_selCaret;
            m_selCaret = idx;
            InvalidateSelectionChange(m_selAnchor, oldCaret);
        }
        return true;
    }

    // 图钉：鼠标停上去时登记「取消 Pin」悬停提示，移开时撤销（状态不变时 SetPinHover
    // 什么都不做）。
    SetPinHover(PinIconAtPoint(pt), pt);

    // 代码卡片：「复制」与底栏的悬停效果
    UpdateCodeHover(pt, true);

    // 文件行：据落点更新底部操作链接的悬停高亮；悬停目标变化时才重绘（变深色 + 下划线）。
    if (m_msg.kind == kMsgFile)
    {
        const int idx = FileLinkAtPoint(pt);
        if (idx != m_fileHoverIdx)
        {
            m_fileHoverIdx = idx;
            Invalidate();
        }
    }

    // 文件夹行：同上，各自维护自己的悬停下标。
    if (m_msg.kind == kMsgFolder)
    {
        const int idx = FolderLinkAtPoint(pt);
        if (idx != m_folderHoverIdx)
        {
            m_folderHoverIdx = idx;
            Invalidate();
        }
    }

    // 共享卡片：鼠标进出底部按钮行时重绘，按钮文字随之加深 / 恢复；按钮不可点、多选模式下不给悬停效果
    if (m_msg.kind == kMsgFileShare || m_msg.kind == kMsgScreenShare || m_msg.kind == kMsgGroupInvite)
    {
        bool enabled = false;
        if (m_msg.kind == kMsgFileShare)
            enabled = m_msg.shareButtonEnabled;
        else if (m_msg.kind == kMsgScreenShare)
            enabled = m_msg.screenShareButtonEnabled;
        else
            enabled = GroupInviteButtonClickable();
        const bool hover = enabled && !ParentInMultiSelect()
                           && ::PtInRect(&m_shareBtnRect, pt) != FALSE;
        if (hover != m_shareBtnHover)
        {
            m_shareBtnHover = hover;
            Invalidate();
        }
    }

    // 密聊邀请卡片：鼠标在两个按钮之间移动或移出按钮行时重绘，悬停的那个按钮加深并加下划线
    if (m_msg.kind == kMsgSecretInvite)
    {
        const int hoverIdx = SecretInviteButtonAt(pt);
        if (hoverIdx != m_secretBtnHover)
        {
            m_secretBtnHover = hoverIdx;
            Invalidate();
        }
    }

    // 撤回提示行：鼠标进出「编辑」时重绘，「编辑」随之加上 / 去掉下划线
    if (m_msg.kind == kMsgTime && !m_msg.recallDraftJson.empty())
    {
        const bool hover = RecallEditAtPoint(pt);
        if (hover != m_recallEditHover)
        {
            m_recallEditHover = hover;
            Invalidate();
        }
    }

    // 接龙卡片：鼠标进出底部按钮行时重绘，按钮文字随之加深 / 恢复。多选模式下按钮不可点，不给悬停效果
    if (m_msg.kind == kMsgRelay || m_msg.relayCardMsgid > 0)
    {
        const bool hover = (m_msg.kind == kMsgRelay)
                           ? (!ParentInMultiSelect() && ::PtInRect(&m_relayBtnRect, pt) != FALSE)
                           : RelayTipAtPoint(pt);
        if (hover != m_relayBtnHover)
        {
            m_relayBtnHover = hover;
            Invalidate();
        }
    }
    return false;   // 不消费，鼠标移动交由框架继续分发
}

bool ChatBubble::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 按在合并卡片上后松手：仍在卡片内就请宿主聊天窗打开聊天记录查看窗，content 原文
    // 随消息交给宿主（宿主再转给主面板，由主面板持有查看窗）
    if (m_mergedPressed)
    {
        m_mergedPressed = false;
        ReleaseCapture();
        if (::PtInRect(&m_mergedRect, pt) && !m_msg.mergedContent.empty())
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (::IsWindow(hwnd))
            {
                std::string* pContent = new std::string(m_msg.mergedContent);
                if (!::PostMessage(hwnd, WM_CHAT_OPEN_MERGED_RECORD, 0, (LPARAM)pContent))
                {
                    delete pContent;
                }
            }
        }
        return true;
    }

    // 按在语音气泡 / 红色感叹号上后松手：仍在原处才请宿主聊天窗播放 / 停止或重发
    if (m_voicePressed || m_voiceFailPressed)
    {
        const bool resend = m_voiceFailPressed;
        m_voicePressed = false;
        m_voiceFailPressed = false;
        ReleaseCapture();
        if (resend && ::PtInRect(&m_voiceFailRect, pt))
        {
            PostVoiceAction(kVoiceActionResend);
        }
        else if (!resend && ::PtInRect(&m_voiceRect, pt))
        {
            PostVoiceAction(kVoiceActionToggle);
        }
        return true;
    }

    // 按在密聊邀请卡片的按钮上后松手：仍在同一个按钮上才请宿主单聊窗执行（同意、忽略或取消）
    if (m_secretBtnPressed >= 0)
    {
        const int pressed = m_secretBtnPressed;
        m_secretBtnPressed = -1;
        ReleaseCapture();
        if (SecretInviteButtonAt(pt) == pressed)
        {
            PostSecretInviteAction(pressed);
        }
        return true;
    }

    // 按在共享卡片上后松手：仍在卡片内就请宿主聊天窗打开这个共享（宿主经文件共享宿主交给 DLL）
    if (m_sharePressed)
    {
        m_sharePressed = false;
        ReleaseCapture();
        //入群邀请卡片：只认按钮那一行（见 OnLButtonDown），抬起时仍在按钮行内才请宿主加入
        if (m_msg.kind == kMsgGroupInvite)
        {
            if (::PtInRect(&m_shareBtnRect, pt))
            {
                PostGroupInviteJoin();
            }
            return true;
        }
        if (::PtInRect(&m_shareRect, pt))
        {
            //同一组矩形也用于屏幕共享卡片：按种类决定是打开文件共享还是加入屏幕共享；
            //复用屏幕共享版式的群公告卡片、群备忘卡片改为请宿主打开那条公告或备忘
            if (m_msg.kind == kMsgScreenShare && (m_msg.screenShareIsAnnouncement || m_msg.screenShareIsMemo))
            {
                PostInfoCardOpen();
            }
            //云端录像卡片（2026-10-06 起同样复用这套版式）：请宿主播放那条录像
            else if (m_msg.kind == kMsgScreenShare && m_msg.screenShareIsCloudRecord)
            {
                PostCloudRecordOpen();
            }
            else if (m_msg.kind == kMsgScreenShare)
            {
                PostScreenShareJoin();
            }
            else
            {
                PostShareOpen();
            }
        }
        return true;
    }

    // 按在撤回提示行的「编辑」上后松手：仍在「编辑」上才算一次单击，把撤回的内容投给宿主放回输入框
    if (m_recallEditPressed)
    {
        m_recallEditPressed = false;
        ReleaseCapture();
        if (RecallEditAtPoint(pt))
        {
            PostRecallEditRequest();
        }
        return true;
    }

    // 按在接龙卡片上后松手：仍在卡片内才算一次单击。落在按钮行上是「参与接龙 / 修改我的」，
    // 打开详情窗并聚焦输入框；落在卡片其它位置只打开详情窗。详情窗由主面板持有，宿主转交
    if (m_relayPressed)
    {
        m_relayPressed = false;
        ReleaseCapture();
        if (m_msg.kind == kMsgRelay && ::PtInRect(&m_relayRect, pt))
        {
            const bool onButton = ::PtInRect(&m_relayBtnRect, pt) != FALSE;
            PostRelayAction(m_msg.msgid, onButton ? kChatRelayActionJoin : kChatRelayActionOpen);
        }
        else if (RelayTipAtPoint(pt))
        {
            //提示行：打开它所属那张卡片的详情窗
            PostRelayAction(m_msg.relayCardMsgid, kChatRelayActionOpen);
        }
        return true;
    }

    // 按在选区内 / 图片上但没拖动就松手：当成一次普通单击。
    if (m_dragArmed)
    {
        m_dragArmed = false;
        ReleaseCapture();
        if (!IsTextSelectable())
        {
            return false;   //图片：单击本就没有动作，双击放大由 OnLButtonDblClk 处理
        }
        // 文字：与在别处单击一样，选区收成一个插入点、高亮消失；落在按下那条链接上则打开它。
        const int idx = CharIndexFromHostPoint(pt);
        const int oldAnchor = m_selAnchor;
        const int oldCaret  = m_selCaret;
        m_selAnchor = idx;
        m_selCaret  = idx;
        const int pressed = m_linkPressIdx;
        m_linkPressIdx = -1;
        if (pressed >= 0 && LinkAtPoint(pt) == pressed)
        {
            OpenLinkByIndex(pressed);
        }
        InvalidateSelectionChange(oldAnchor, oldCaret);
        return true;
    }

    // 结束拖拽选择：定下选区另一端并释放捕获。
    if (m_selDragging)
    {
        const int oldCaret = m_selCaret;
        m_selCaret    = CharIndexFromHostPoint(pt);
        m_selDragging = false;
        ReleaseCapture();

        // 抬起处若仍在按下那条链接上、且这一趟没有真的选出文字（按下与抬起的字符
        // 下标相同），就当成一次点击，请宿主聊天窗打开它。
        const int pressed = m_linkPressIdx;
        m_linkPressIdx = -1;
        if (pressed >= 0 && !HasTextSelection() && LinkAtPoint(pt) == pressed)
        {
            OpenLinkByIndex(pressed);
        }

        InvalidateSelectionChange(m_selAnchor, oldCaret);
        return true;
    }
    m_linkPressIdx = -1;
    return false;
}

void ChatBubble::OpenLinkByIndex(int index)
{
    const std::wstring url = LinkUrlByIndex(index);
    if (url.empty())
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }

    // 气泡自己不拉起外部程序：URL 经宿主聊天窗统一过一遍协议白名单再打开，
    // 安全判断只留一处（见 CustomMsgDef.h 里 WM_CHAT_OPEN_LINK 的说明）。
    std::wstring* pUrl = new std::wstring(url);
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_LINK, 0, (LPARAM)pUrl))
    {
        delete pUrl;
    }
}

bool ChatBubble::OnMouseLeave()
{
    // 光标离开本行：清除文件 / 文件夹链接的悬停态并重绘。
    if (m_fileHoverIdx != -1)
    {
        m_fileHoverIdx = -1;
        Invalidate();
    }
    if (m_folderHoverIdx != -1)
    {
        m_folderHoverIdx = -1;
        Invalidate();
    }
    if (m_relayBtnHover)
    {
        m_relayBtnHover = false;
        Invalidate();
    }
    if (m_recallEditHover)
    {
        m_recallEditHover = false;
        Invalidate();
    }
    if (m_secretBtnHover != -1)
    {
        m_secretBtnHover = -1;
        Invalidate();
    }
    if (m_shareBtnHover)
    {
        m_shareBtnHover = false;
        Invalidate();
    }
    // 从图钉上直接移出本行：撤销「取消 Pin」悬停提示的登记。
    POINT noPoint = { 0, 0 };
    SetPinHover(false, noPoint);
    // 代码卡片：撤掉悬停效果，「已复制」恢复为「复制」
    UpdateCodeHover(noPoint, false);
    return false;
}

bool ChatBubble::OnSetCursor(POINT pt)
{
    // 光标落在可点击的图钉上：显示手型，提示点一下就能取消 Pin。
    if (PinIconAtPoint(pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在合并卡片上：显示手型，提示单击可以打开聊天记录。多选模式下整行是勾选目标，
    // 不给手型
    if (!ParentInMultiSelect() && m_msg.kind == kMsgMerged && ::PtInRect(&m_mergedRect, pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在语音气泡或发送失败的红色感叹号上：显示手型，单击播放 / 停止或重发
    if (!ParentInMultiSelect() && m_msg.kind == kMsgVoice
        && (::PtInRect(&m_voiceRect, pt) || ::PtInRect(&m_voiceFailRect, pt)))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在可点的共享卡片上：显示手型，单击打开共享
    if (!ParentInMultiSelect()
        && ((m_msg.kind == kMsgFileShare && m_msg.shareButtonEnabled)
            || (m_msg.kind == kMsgScreenShare && m_msg.screenShareButtonEnabled))
        && ::PtInRect(&m_shareRect, pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在密聊邀请卡片的按钮上：显示手型
    if (SecretInviteButtonAt(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在入群邀请卡片可点的「加入群聊」按钮行上：显示手型
    if (!ParentInMultiSelect() && m_msg.kind == kMsgGroupInvite && GroupInviteButtonClickable()
        && ::PtInRect(&m_shareBtnRect, pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在撤回提示行的「编辑」上：显示手型，单击把撤回的内容放回输入框
    if (RecallEditAtPoint(pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在接龙卡片、或可点击的接龙提示行文字上：同样显示手型，单击打开详情窗
    if ((!ParentInMultiSelect() && m_msg.kind == kMsgRelay && ::PtInRect(&m_relayRect, pt))
        || RelayTipAtPoint(pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在文件操作链接上：显示手型光标（超链接感）。
    if (FileLinkAtPoint(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 文件夹操作链接同理（置灰的按钮不在其中，故不会给出可点的暗示）。
    if (FolderLinkAtPoint(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在正文里的超链接上：同样显示手型。多选模式下整行是勾选目标，
    // 链接不可点，就别给手型误导用户。
    if (!ParentInMultiSelect() && LinkAtPoint(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在 Markdown 卡片上：显示手型，提示单击可以打开预览窗口
    if (!ParentInMultiSelect() && PointInMarkdownCard(pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    // 光标落在代码卡片上（「复制」、底栏与正文都可以点）：显示手型
    if (!ParentInMultiSelect() && CodePartAtPoint(pt) != kCodePartNone)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

bool ChatBubble::OnLButtonDblClk(POINT pt, UINT /*mkFlags*/)
{
    // 双击不是拖拽：若上一下单击留下了"准备拖出"的状态，这里复位。
    if (m_dragArmed)
    {
        m_dragArmed = false;
        ReleaseCapture();
    }

    // 文字气泡：双击 = 选中整条（与用户对齐的行为）。想选其中一段仍然靠单击拖动。
    if (!ParentInMultiSelect() && IsTextSelectable())
    {
        const int oldAnchor = m_selAnchor;
        const int oldCaret  = m_selCaret;
        m_selAnchor   = 0;
        m_selCaret    = SelectableTextLength();
        m_selDragging = false;
        m_linkPressIdx = -1;

        // 双击的第一下本身是一次完整单击：落在链接上时它已经请求打开浏览器了。
        // 双击的语义是"选中整条"，故在这里把那次打开撤销掉（宿主窗为此把打开
        // 延后了一个双击间隔，见 WM_CHAT_OPEN_LINK / WM_CHAT_CANCEL_LINK_OPEN）。
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (::IsWindow(hwnd))
            {
                ::PostMessage(hwnd, WM_CHAT_CANCEL_LINK_OPEN, 0, 0);
            }
        }

        InvalidateSelectionChange(oldAnchor, oldCaret);
        return true;
    }

    // 仅"已就绪的图片消息"（imageState==0，本地路径有效）响应双击放大。上传中 /
    // 下载中 / 失败态不开；状态值虽为正常、但本地文件丢失或无法解码的也不开。
    if (m_msg.kind != kMsgImage || EffectiveImageState() != kChatImageStateReady)
    {
        return false;
    }
    if (m_msg.imagePath.IsEmpty())
    {
        return false;
    }
    // 双击点须落在图片矩形内（避开头像 / 空白）。
    if (!::PtInRect(&m_imageRect, pt))
    {
        return false;
    }

    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return false;
    }

    // 同步发给宿主聊天窗：lParam 为图片本地路径。SendMessage 同步执行，期间
    // m_msg.imagePath 缓冲区有效；聊天窗在处理函数里据此打开看图窗。
    ::SendMessage(hwnd, WM_VIEW_CHAT_IMAGE, 0, (LPARAM)(LPCTSTR)m_msg.imagePath);
    return true;
}

void ChatBubble::InvalidateMeasureCache()
{
    m_measuredWidth = -1;     // 下次 Measure 不再命中缓存，会重新排版并重算链接片段
    m_measuredLineW = -1;       // 测量阶段的排版结果随之作废，绘制时不再复用
    m_renderDirty   = true;   // 渲染缓存里那张位图也作废
    Invalidate();
}

int ChatBubble::LinkAtPoint(POINT pt) const
{
    // Markdown 卡片上没有可点的链接（链接在预览窗口里点）
    if (MarkdownActive())
    {
        return -1;
    }
    if (m_linksFlat.empty() || m_selLayout.m_lines.empty())
    {
        return -1;   // 本条没有链接，或还没测量过
    }

    // 命中测试在"相对文本区原点"的坐标系里做，与排版模型同一套坐标（与 @提醒
    // 的 TryPopupMentionMenu 完全一致）。
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return -1;
    }

    std::vector<std::pair<int, int> > ranges;
    ranges.reserve(m_linksFlat.size());
    for (std::size_t i = 0; i < m_linksFlat.size(); ++i)
    {
        ranges.push_back(std::make_pair(m_linksFlat[i].m_off,
                                        m_linksFlat[i].m_off + m_linksFlat[i].m_len));
    }
    return HitTestCharRanges(m_selLayout, ranges, pt.x - origin.x, pt.y - origin.y);
}

bool ChatBubble::TryPopupMentionMenu(const POINT& pt, HWND hwnd)
{
    if (m_atsFlat.empty() || m_selLayout.m_lines.empty())
    {
        return false;   // 本条不含 @，或还没测量过
    }

    // 命中测试在"相对文本区原点"的坐标系里做，与排版模型同一套坐标。
    bool ok = false;
    const POINT origin = TextOriginInHost(ok);
    if (!ok)
    {
        return false;
    }

    std::vector<std::pair<int, int> > ranges;
    ranges.reserve(m_atsFlat.size());
    for (std::size_t i = 0; i < m_atsFlat.size(); ++i)
    {
        ranges.push_back(std::make_pair(m_atsFlat[i].m_off,
                                        m_atsFlat[i].m_off + m_atsFlat[i].m_len));
    }
    const int hit = HitTestCharRanges(m_selLayout, ranges, pt.x - origin.x, pt.y - origin.y);
    if (hit < 0)
    {
        return false;   // 点在正文或空白上，交回给气泡自己的菜单
    }

    const long long mentionUid = m_atsFlat[(std::size_t)hit].m_uid;
    if (!ShouldShowMentionMenu(mentionUid))
    {
        return false;   // @所有人：没有具体的人可操作，当普通文字处理
    }

    // 显示名 = 片段文字去掉开头的 '@'。取自扁平文本而不是原始正文 —— 片段偏移
    // 本来就是扁平口径（见 m_atsFlat 的说明）。
    std::wstring displayName;
    {
        const int begin = m_atsFlat[(std::size_t)hit].m_off;
        const int len   = m_atsFlat[(std::size_t)hit].m_len;
        if (begin >= 0 && len > 1 && (std::size_t)(begin + len) <= m_selFlatText.size())
        {
            displayName = m_selFlatText.substr((std::size_t)begin + 1, (std::size_t)len - 1);
        }
    }

    // 「@ 的是不是我自己」直接读片段上的 m_isMe —— 它在协议换算时就按本人 uid
    // 标好了（见 GroupMentionModel），气泡不必再去问谁是当前登录用户。
    const bool isSelf = m_atsFlat[(std::size_t)hit].m_isMe;

    return PopupMemberMenu(mentionUid, displayName, isSelf, pt, hwnd);
}

RECT ChatBubble::AvatarRectInHost() const
{
    RECT rc = { 0, 0, 0, 0 };
    // 时间分隔 / 撤回提示行整行只有一句居中文字，没有头像。
    if (!IsRealMessage())
    {
        return rc;
    }

    // 多选模式下行首让出勾选列、内容整体右移 —— 与 RenderContent 里 cx/cw 的算法一致。
    const int selW = ParentInMultiSelect() ? kCheckColW : 0;
    const int cx   = m_rcItem.left + selW;
    const int cw   = (m_rcItem.right - m_rcItem.left) - selW;
    if (cw <= 0)
    {
        return rc;
    }

    const int avX = m_msg.fromMe ? (cx + cw - kEdgePad - kAvatarSize) : (cx + kEdgePad);
    const int avY = m_rcItem.top + kRowVMargin;
    rc.left   = avX;
    rc.top    = avY;
    rc.right  = avX + kAvatarSize;
    rc.bottom = avY + kAvatarSize;
    return rc;
}

bool ChatBubble::TryPopupAvatarMenu(const POINT& pt, HWND hwnd)
{
    // 只在群聊里生效：单聊窗上下就两个人，对着头像弹"发消息 / @TA"没有意义。
    // senderName 只有群聊才填（与 RenderContent 里 showSender 同一口径）。
    if (m_msg.fromMe || m_msg.senderName.IsEmpty())
    {
        return false;
    }
    if (m_msg.senderUserId <= 0)
    {
        return false;   // 发送者 userid 未知（老数据 / mock 消息），无从操作
    }

    const RECT av = AvatarRectInHost();
    if (!::PtInRect(&av, pt))
    {
        return false;   // 没点在头像上，交回给气泡自己的菜单
    }

    // 走到这里目标必然不是本人（上面已经把 fromMe 挡掉了），故 isSelf 传 false。
    return PopupMemberMenu(m_msg.senderUserId, (LPCWSTR)m_msg.senderName, false, pt, hwnd);
}

bool ChatBubble::PopupMemberMenu(long long uid, const std::wstring& displayName,
                                 bool isSelf, const POINT& pt, HWND hwnd)
{
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    const bool stillInGroup = (list != NULL) ? list->IsMemberInGroup(uid) : true;

    // 本人与目标在本群的角色：菜单末尾的「设为 / 撤销管理员」据此决定要不要出现。
    // 两者都由消息列表代问宿主群聊窗 —— 气泡不认识"群成员表"这类业务对象。
    // 单聊窗没装查询钩子，两者均为负值，管理项自然不出现。
    const int myNetRole     = (list != NULL) ? list->MyNetRole() : -1;
    const int targetNetRole = (list != NULL) ? list->MemberNetRole(uid) : -1;

    // 「特别关注」那一项的文案同样由消息列表代问宿主群聊窗 —— 关注名单只有主面板
    // 那一份。单聊窗没装这个钩子，恒返回 false（按"未关注"显示）。
    const bool isStarred = (list != NULL) ? list->IsStarred(uid) : false;
    // 「关注 TA 在本群的发言」同理，名单在主面板（docs/消息提醒模式与消息盒子方案.md §12）
    const bool isGroupFollowed = (list != NULL) ? list->IsGroupFollowed(uid) : false;

    // selfUid 传法：BuildMentionMenuModel 只用它和第一个参数比对判本人，这里已经
    // 有现成的 isSelf，故把两者传成"相等 / 不等"即可，不必真去取本人 uid。
    const std::vector<MentionMenuEntry> model =
        BuildMentionMenuModel(uid, isSelf ? uid : 0, stillInGroup,
                              myNetRole, targetNetRole, isStarred, isGroupFollowed);
    if (model.empty())
    {
        return false;
    }

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return false;
    }
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].isSeparator)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        const UINT flags = MF_STRING | (model[i].enabled ? MF_ENABLED : MF_GRAYED);
        ::AppendMenuW(menu, flags, (UINT_PTR)model[i].id, model[i].label);
    }

    // 落点必须夹进锚点所在显示器的工作区 —— 气泡靠近屏幕右 / 下边缘时，直接把光标
    // 坐标交给 TrackPopupMenu 会让菜单跑到桌面外看不见（开发约定文档的菜单不越屏规则）。
    // 菜单尺寸按行数估算：Win32 的 HMENU 在弹出前拿不到精确尺寸，估大一点即可 ——
    // 夹取只在"放不下"时才生效，估大只会让靠边时更早翻向左 / 上，不会误伤。
    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    SIZE menuSize;
    menuSize.cx = kMentionMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kMentionMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(scr, menuSize);

    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        at.x, at.y, 0, hwnd, NULL);
    ::DestroyMenu(menu);

    if (cmd == 0)
    {
        return true;   // 用户按 Esc / 点别处取消 —— 已经处理过了，不再弹气泡菜单
    }

    // 这些动作全要碰业务（打开单聊 / 资料窗、往输入框插字、写剪贴板、改关注名单），
    // 气泡不认识这些对象，打包投给宿主聊天窗去执行。投递失败时自行释放，避免泄漏。
    MentionActionData* pData = new MentionActionData();
    pData->id          = (MentionMenuId)cmd;
    pData->uid         = uid;
    pData->displayName = displayName;
    if (!::PostMessage(hwnd, WM_CHAT_MENTION_ACTION, 0, (LPARAM)pData))
    {
        delete pData;
    }
    return true;
}

bool ChatBubble::OnRButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 时间分隔 / 撤回提示行不弹菜单。
    if (!IsRealMessage())
    {
        return false;
    }
    // 密聊消息、密聊邀请卡片与密聊状态下的整张列表都没有右键菜单：不能复制、转发、引用、Pin、另存
    // （docs/密聊方案.md 4.5）。返回 true 表示已处理，消息列表不再替它弹出任何菜单
    if (IsSecretLocked())
    {
        return true;
    }
    ChatMessageList* list = dynamic_cast<ChatMessageList*>(GetParent());
    if (list == NULL)
    {
        return false;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return false;
    }

    // 右键是不是点在正文里的超链接上。命中时把「在浏览器中打开 / 复制链接地址」
    // 两项<u>加在菜单最前面</u>，而不是换掉整份菜单 —— 复制 / 引用 / 撤回那些照样要
    // 有、也照样要能用（早先做成了独立菜单，一含链接就把它们全挡没了）。
    const int linkIdx = LinkAtPoint(pt);

    // 再看这一下是不是点在某个 @某人 的片段上 —— 是的话弹「@ 的菜单」而不是气泡
    // 自己的菜单（复制 / 引用 / 撤回那套）。点空了照旧走下面的气泡菜单。
    if (TryPopupMentionMenu(pt, hwnd))
    {
        return true;
    }

    // 再看是不是点在发送者头像上 —— 是的话弹同一套成员菜单（只在群聊里，见
    // TryPopupAvatarMenu 的说明）。点空了照旧走下面的气泡菜单。
    if (TryPopupAvatarMenu(pt, hwnd))
    {
        return true;
    }

    // 撤回只对自己发出、且仍在可撤回时限内的消息开放。时限判断与服务端同口径
    // （见 net::MessageProtocolClient::canRecallMessage）：超时的消息直接不显示该项，
    // 免得用户点了才被服务端以 error_code_recall_timeout 拒绝。翻上去的历史消息
    // 因此不再出现"撤回"。
    bool canRecall = false;
    if (m_msg.fromMe)
    {
        //本地当前时刻（秒 → 毫秒），与消息的服务器时间戳同口径比较。
        long long nowMs = (long long)::time(NULL) * 1000;
        canRecall = net::MessageProtocolClient::canRecallMessage(
            m_msg.msgid, m_msg.timestampMs, nowMs);
    }

    // 菜单该有哪几行交给纯模型算（图片行会多出「图片另存为」「再次发送」两项），
    // 这里只负责照着它建真菜单。
    // Pin 只要求服务器已给这条消息分配了 msgid —— 没有 msgid 时发过去必被判
    // message_not_exist，故整项不显示。取消 Pin 的权限不在这里预判：本端不一定知道
    // 一条历史消息是谁钉的，预判会出现"该显示的没显示"，交给服务端拒绝并提示。
    const bool canPin = (m_msg.msgid > 0);

    // 转发同样要求有服务器 msgid；单聊里收到的文件与历史文件 msgid 为 0、服务器 msgid
    // 放在 fileMsgid 里，也能转发。判定规则见 CanForwardMessage。
    const bool canForward = CanForwardMessage(m_msg.kind, m_msg.msgid, m_msg.fileMsgid);

    // 菜单该有哪几行交给纯模型算（图片行会多出「图片另存为」「再次发送」两项），
    // 这里只负责照着它建真菜单。
    // 发送失败的行：服务端不认识这条消息，只留「复制」「删除」「多选」，文字消息多一项「重新编辑」。
    // 图片文字识别模块（FlamingoOcr.dll）可用时，图片行在「图片另存为」之后多一项「图片文字识别」。
    // 「添加到备忘」的出现方式由宿主聊天窗设在消息列表上（单聊一项、群聊一到两项，其它窗口不出现）
    const ChatMessageList* memoList = dynamic_cast<const ChatMessageList*>(GetParent());
    const int memoMenu = (memoList != NULL) ? memoList->MemoMenuMode() : kChatMemoMenuNone;
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(m_msg.kind, EffectiveImageState(),
                                  !m_msg.imagePath.IsEmpty(), canRecall,
                                  canPin, m_msg.pinned, canForward,
                                  m_msg.sendFailed, CanReeditMessage(m_msg),
                                  ocr::IsOcrEntryVisible(), memoMenu);

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return false;
    }

    // 命中链接时，两项链接操作排在最前，再用分隔线与气泡本身的那套隔开。
    // 顺序是"点哪儿就先给哪儿的操作"，与浏览器 / 各家 IM 的习惯一致。
    if (linkIdx >= 0)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuOpenLink, TR(L"在浏览器中打开"));
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuCopyLink, TR(L"复制链接地址"));
        ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }

    for (std::size_t mi = 0; mi < model.size(); ++mi)
    {
        if (model[mi].isSeparator)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        // 代码卡片：「复制」写成「复制代码」，之后多两项「在窗口中查看」「另存为…」
        if (model[mi].id == kChatMsgMenuCopy && CodeActive())
        {
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuCopy, TR(L"复制代码"));
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuViewCode, TR(L"在窗口中查看"));
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuSaveCodeAs, TR(L"另存为…"));
            //已发出的代码片段可以追加批注（docs/代码片段方案.md 第 11.3 节）
            if (CanAnnotateCodeMessage(m_msg))
            {
                ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuAnnotateCode, TR(L"添加批注…"));
            }
            continue;
        }
        // Markdown 卡片：「复制」写成「复制 Markdown 源文本」，之后多一项「在窗口中查看」（docs/Markdown消息方案.md 第 11.3 节）
        if (model[mi].id == kChatMsgMenuCopy && MarkdownActive())
        {
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuCopyMarkdownSource, TR(L"复制 Markdown 源文本"));
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kChatMsgMenuViewMarkdown, TR(L"在窗口中查看"));
            continue;
        }
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)model[mi].id, model[mi].label);
    }

    // pt 为 host 客户区坐标，TrackPopupMenu 要屏幕坐标。
    // 落点同样要夹进锚点所在显示器的工作区 —— 气泡靠近屏幕右 / 下边缘时（典型：
    // 消息区最后一行、窗口贴着任务栏），直接把光标坐标交给 TrackPopupMenu 会让菜单
    // 跑到桌面外看不见。这是开发约定文档里「弹出菜单必须整体落在工作区内」的要求，
    // 与上面 @ 菜单用的是同一个夹取函数。项数含分隔符，按整项高度估算偏大，只会让
    // 靠边时更早翻向左 / 上，不会误伤正常位置。
    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    SIZE msgMenuSize;
    msgMenuSize.cx = kMessageMenuEstWidth;
    msgMenuSize.cy = ::GetMenuItemCount(menu) * kMentionMenuEstItemHeight;
    const POINT msgMenuAt = balloonmain::ClampPopupMenuToWorkArea(scr, msgMenuSize);
    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        msgMenuAt.x, msgMenuAt.y, 0, hwnd, NULL);
    ::DestroyMenu(menu);

    // 注意：kChatMsgMenuDelete 分支可能经 RequestDeleteBubble 销毁 this（本行没有服务器
    // msgid 时），其后不得再访问任何成员 —— 各分支处理完立即 return。
    switch (cmd)
    {
    case kChatMsgMenuOpenLink:
        OpenLinkByIndex(linkIdx);
        return true;
    case kChatMsgMenuCopyLink:
        // 复制的是规范化之后的 URL（www. 开头的那种已经补上了 https://），
        // 粘到浏览器地址栏里直接能用。
        {
            const std::wstring url = LinkUrlByIndex(linkIdx);
            if (!url.empty())
            {
                PutTextToClipboard(hwnd, url.c_str());
            }
        }
        return true;
    // 在窗口中查看：代码卡片请宿主打开代码查看窗
    case kChatMsgMenuViewCode:
        PostCodeViewerOpen();
        return true;
    // 添加批注…：请宿主打开发送代码窗口的批注模式（docs/代码片段方案.md 第 11.3 节）
    case kChatMsgMenuAnnotateCode:
        PostCodeViewerOpen(kCodeViewerActionAnnotate);
        return true;
    // 另存为：把代码卡片的代码写进用户选的文件
    case kChatMsgMenuSaveCodeAs:
        if (CodeActive())
        {
            SaveCodeWithDialog(hwnd, m_codeContent.m_code, m_msg.codeLang);
        }
        return true;
    // 复制 Markdown 源文本：只写纯文字（正文转义已还原），粘到编辑器或别的应用里都是可用的 Markdown
    case kChatMsgMenuCopyMarkdownSource:
        {
            const std::string source = MarkdownSourceFromBody(m_msg.text);
            const int len = ::MultiByteToWideChar(CP_UTF8, 0, source.data(), (int)source.size(), NULL, 0);
            std::wstring wide((size_t)(len > 0 ? len : 0), L'\0');
            if (len > 0)
            {
                ::MultiByteToWideChar(CP_UTF8, 0, source.data(), (int)source.size(), &wide[0], len);
            }
            //剪贴板里的换行写成 \r\n，粘到记事本等程序里才是分行的
            std::wstring crlf;
            crlf.reserve(wide.size() + wide.size() / 16);
            for (size_t i = 0; i < wide.size(); ++i)
            {
                if (wide[i] == L'\n')
                {
                    crlf += L'\r';
                }
                crlf += wide[i];
            }
            PutTextToClipboard(hwnd, crlf.c_str());
        }
        return true;
    // 在窗口中查看：Markdown 卡片请宿主打开预览窗口
    case kChatMsgMenuViewMarkdown:
        PostMarkdownPreviewOpen();
        return true;
    case kChatMsgMenuCopy:
        // 代码卡片：复制代码原文（纯文字），不走下面的片段拆分
        if (CodeActive())
        {
            CopyCodeToClipboard(hwnd);
            return true;
        }
        // 有选区就只复制选中的那一段，没有选区才复制整条 —— 不新增菜单项，
        // "复制"这一项按当前是否有选区自动切换含义，与浏览器 / 各家 IM 一致。
        //
        // 能拆成片段的（文字、表情、本地可用的图片）同时写纯文字与 Flamingo 自己的格式，
        // 粘到聊天输入框时表情、图片能还原；拆不出片段的（文件、文件夹、本地没有原图的
        // 图片）照旧只写纯文字。
        {
            const std::vector<ChatClipSegment> segs =
                HasTextSelection() ? GetSelectedClipSegments() : WholeMessageClipSegments();
            if (!segs.empty())
            {
                WriteChatContentToClipboard(hwnd, segs);
            }
            else if (HasTextSelection())
            {
                PutTextToClipboard(hwnd, (LPCWSTR)GetSelectedText());
            }
            else
            {
                PutTextToClipboard(hwnd, (LPCWSTR)MessageCopyText(m_msg));
            }
        }
        return true;
    case kChatMsgMenuSaveImageAs:
        PostImageMenuAction(hwnd, kChatMsgMenuSaveImageAs);
        return true;
    // 图片文字识别：与「图片另存为」同一条通路投给宿主聊天窗，由它打开识别结果窗口
    case kChatMsgMenuOcr:
        PostImageMenuAction(hwnd, kChatMsgMenuOcr);
        return true;
    case kChatMsgMenuResendImage:
        PostImageMenuAction(hwnd, kChatMsgMenuResendImage);
        return true;
    case kChatMsgMenuReply:
        list->RequestReplyBubble(this);
        return true;
    case kChatMsgMenuForward:
        // 本机已没有文件的在线直传文件无法转发：不进入多选，请宿主直接提示「在线直传的文件无法转发」
        // （2026-10-05 起，docs/单聊文件P2P直传与P2P徽章方案.md 第 7 节）
        if (DirectFileForwardBlocked(m_msg.kind == kMsgFile, m_msg.fileDirect,
                                     DirectFileHasLocalCopy(std::wstring((LPCWSTR)m_msg.fileLocalPath),
                                                            m_msg.fileSizeBytes)))
        {
            list->PostDirectForwardUnavailable();
            return true;
        }
        // 不直接打开目标选择窗：进入多选模式并预先勾选本条，由用户在多选操作条上
        // 选择逐条转发还是合并转发（与用户确认的交互，见 docs/聊天消息转发方案.md §3.1）
        list->EnterMultiSelectWith(this);
        return true;
    case kChatMsgMenuRecall:
        list->RequestRecallBubble(this);
        return true;
    case kChatMsgMenuPin:
        // 界面不就地改：把请求投给宿主窗发出去，等服务端推 cmd_message_pinned_notify
        // 回来再改标记。与撤回同口径，这样本端与对端看到的结果必然一致。
        list->RequestPinBubble(this, !m_msg.pinned);
        return true;
    case kChatMsgMenuMultiSelect:
        list->EnterMultiSelectWith(this);
        return true;
    case kChatMsgMenuReedit:
        // 原文放回输入框由宿主聊天窗做（输入框、待发 @ 候选都在它那里）
        PostReeditRequest(hwnd);
        return true;
    // 添加到备忘 / 添加到我的备忘：宿主补上来源信息后打开新建本人备忘的窗口（docs/备忘与提醒方案.md §8）
    case kChatMsgMenuAddMemo:
        PostAddToMemo(hwnd, kMemoPrefillPersonal);
        return true;
    // 添加到群备忘：宿主打开新建群备忘的窗口，权限与边栏「新建」相同
    case kChatMsgMenuAddGroupMemo:
        PostAddToMemo(hwnd, kMemoPrefillGroup);
        return true;
    case kChatMsgMenuResendVoice:
        // 与点红色感叹号同一条路：请宿主聊天窗用原 clientmsgid 重发
        PostVoiceAction(kVoiceActionResend);
        return true;
    case kChatMsgMenuDelete:
        // 本行没有服务器 msgid 时此调用会销毁 this；有 msgid 时只转交宿主发删除请求，
        // 气泡等删除推送到达后再移除
        list->RequestDeleteBubble(this);
        return true;
    default:
        return true;
    }
}

void ChatBubble::PostAddToMemo(HWND hwnd, int target)
{
    // 异步 PostMessage，故堆分配传递；宿主处理完 delete，投递失败自行释放。
    ChatAddToMemoRequest* req = new ChatAddToMemoRequest();
    req->m_target = target;
    req->m_msg = m_msg;
    LOG_INFO("chat bubble: add to memo requested, msgid=%lld, target=%d.", m_msg.msgid, target);
    if (!::PostMessage(hwnd, WM_CHAT_ADD_TO_MEMO, 0, (LPARAM)req))
    {
        delete req;
    }
}

void ChatBubble::PostReeditRequest(HWND hwnd)
{
    // 异步 PostMessage，故堆分配传递；宿主处理完 delete，投递失败自行释放。
    ChatReeditData* d = new ChatReeditData(BuildReeditData(m_msg));
    LOG_INFO("chat bubble: reedit requested, clientmsgid=%s, segments=%u.", m_msg.clientMsgId.c_str(),
             (unsigned)d->m_segments.size());
    if (!::PostMessage(hwnd, WM_CHAT_REEDIT_REQUEST, 0, (LPARAM)d))
    {
        delete d;
    }
}

void ChatBubble::PostImageMenuAction(HWND hwnd, int menuId)
{
    // 另存为要弹模态文件对话框、再次发送要走宿主窗的发图链路，气泡两样都不该碰，
    // 打包投给宿主聊天窗执行。异步 PostMessage，故堆分配传递；投递失败自行释放。
    ChatImageActionData* d = new ChatImageActionData();
    d->id        = (ChatMessageMenuId)menuId;
    d->imagePath = (LPCWSTR)m_msg.imagePath;
    d->saveName  = MakeImageSaveAsDefaultName(d->imagePath, m_msg.msgid);
    if (!::PostMessage(hwnd, WM_CHAT_IMAGE_ACTION, 0, (LPARAM)d))
    {
        delete d;
    }
}

void ChatBubble::OnPaint(HDC hdc, const RECT& rcInter)
{
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 图片 / 文件行：逐帧实绘 —— 绘制时要更新 m_imageRect / m_fileBtnRect 命中矩形，
    // 不能缓存。
    if (!IsRenderCacheable())
    {
        PerfStopWatch sw;
        //逐帧实绘的行不保留正文排版缓存：内容或宽度变化时这条路径不经过 m_renderDirty，
        //每次都重新排版最稳妥（与改动前的开销相同）
        m_textPlacedValid = false;
        RenderContent(hdc);
        BUI_TRACE("CHATBUBBLE-RENDER kind=%d h=%d cached=0 cost=%.3fms",
                  (int)m_msg.kind, h, sw.ElapsedMs());
        return;
    }

    // 文字 / emoji / 时间分隔行：内容 / 行宽高 / 多选态都不变就复用缓存位图，省掉每帧
    // 用 GDI+ 重画整条气泡 —— 拖动分隔条 / 滚动时这是主要的每帧开销。
    const bool multi = ParentInMultiSelect();

    // 这一次要画的是本行与脏区相交的那一段；不相交就什么都不用画
    RECT inter;
    if (!::IntersectRect(&inter, &m_rcItem, &rcInter))
    {
        return;
    }
    const int needTop = inter.top - m_rcItem.top;         // 要画的那一段的上沿（行内纵坐标）
    const int needBottom = inter.bottom - m_rcItem.top;   // 要画的那一段的下沿（行内纵坐标）

    // 内容、行宽、行高或多选态变了：缓存整张作废，正文也要重新排版
    const bool contentChanged = m_renderDirty || m_renderDC == NULL || m_renderW != w
                                || m_renderRowH != h || m_renderMultiSel != multi;

    // 决定缓存覆盖行内的哪一段（2026-10-04 起）。普通行整条缓存；行高超过分段阈值的超长行只缓存要画的
    // 那一段及其上下各 kTallSliceMarginPx 的余量 —— 上万像素高的气泡整条画进缓存要几百毫秒，屏幕上却
    // 只露出几百像素。已有的缓存段覆盖得住这次要画的部分就直接使用，滚出这一段时再按新位置重画一段。
    int sliceTop = 0;
    int sliceH = h;
    bool needRender = contentChanged;
    if (h > s_tallSliceThresholdPx)
    {
        const bool covered = !contentChanged
                             && m_renderSliceTop <= needTop
                             && needBottom <= m_renderSliceTop + m_renderH;
        if (covered)
        {
            sliceTop = m_renderSliceTop;
            sliceH = m_renderH;
        }
        else
        {
            sliceTop = needTop - kTallSliceMarginPx;
            if (sliceTop < 0)
            {
                sliceTop = 0;
            }
            int sliceBottom = needBottom + kTallSliceMarginPx;
            if (sliceBottom > h)
            {
                sliceBottom = h;
            }
            sliceH = sliceBottom - sliceTop;
            needRender = true;
        }
    }
    else if (m_renderSliceTop != 0 || m_renderH != h)
    {
        // 缓存还是分段时建的（如分段阈值刚被调大）：改回整条缓存
        needRender = true;
    }

    if (needRender)
    {
        EnsureRenderCache(hdc, w, sliceH);
        if (m_renderDC != NULL)
        {
            // 缓存位图先铺消息行底色（与 ChatMessageList 行底一致），气泡抗锯齿边据此
            // 混色；再把本行画进缓存。行矩形临时摆成「缓存段上沿对齐位图的 0」，整条缓存时即
            // 平移到原点（复用 RenderContent 的相对坐标）；缓存段以外的部分落在位图外面，画不出来，
            // 正文那一层也会按位图边界跳过段外的文字，耗时只与缓存段的大小有关。
            HBRUSH bg = ::CreateSolidBrush(RGB(0xFA, 0xF9, 0xF6));
            RECT lr = { 0, 0, w, sliceH };
            ::FillRect(m_renderDC, &lr, bg);
            ::DeleteObject(bg);

            const RECT saved = m_rcItem;
            m_rcItem.left   = 0;
            m_rcItem.top    = -sliceTop;
            m_rcItem.right  = w;
            m_rcItem.bottom = h - sliceTop;
            PerfStopWatch sw;
            if (contentChanged)
            {
                m_textPlacedValid = false;   //内容、宽度或样式可能都变了，正文重新排版；只是换段时沿用已有的排版
            }
            RenderContent(m_renderDC);
            ++s_renderPassCount;
            BUI_TRACE("CHATBUBBLE-RENDER kind=%d h=%d cached=1 slice=%d..%d cost=%.3fms",
                      (int)m_msg.kind, h, sliceTop, sliceTop + sliceH, sw.ElapsedMs());
            m_rcItem = saved;

            m_renderDirty    = false;
            m_renderMultiSel = multi;
            m_renderRowH     = h;
            m_renderSliceTop = sliceTop;
            m_selBandDirty   = false;   //整张（整段）都重画过了，待重画的横条自然作废
        }
    }
    else if (m_selBandDirty)
    {
        // 只有选区变了：只重画变化的那几行。先画进一张只有横条那么高的临时位图，再拷回缓存位图
        // 的对应位置。不用"在缓存位图上设裁剪区再重画"：GDI+ 在与屏幕兼容的位图（DDB）上绘制时
        // 会改动设备上下文的裁剪区，后面的文字层就不再受横条限制，横条外的文字被重新叠画一遍，
        // 既慢又会让字形变深（2026-10-01 实测）。临时位图的物理边界不受这类改动影响；正文那一层
        // 按位图边界跳过横条之外的文字段，耗时只与横条大小有关。
        // 分段缓存时只更新横条落在缓存段里的部分；段以外的部分不在缓存里，以后换段整段重画时
        // 自然按最新的选区画出来。
        m_selBandDirty = false;
        const RECT cached = { 0, m_renderSliceTop, w, m_renderSliceTop + m_renderH };
        RECT band;
        if (::IntersectRect(&band, &m_selBandRect, &cached))
        {
            const int bandH = band.bottom - band.top;
            HDC tmpDC = ::CreateCompatibleDC(m_renderDC);
            HBITMAP tmpBmp = (tmpDC != NULL) ? ::CreateCompatibleBitmap(m_renderDC, w, bandH) : NULL;
            if (tmpBmp != NULL)
            {
                HGDIOBJ oldBmp = ::SelectObject(tmpDC, tmpBmp);
                // 与整张重画同一流程：先铺消息行底色，再把本行画进去。行矩形临时摆成"横条上沿
                // 对齐临时位图的 0"，横条以外的部分落在位图外面，画不出来
                HBRUSH bg = ::CreateSolidBrush(RGB(0xFA, 0xF9, 0xF6));
                RECT tr = { 0, 0, w, bandH };
                ::FillRect(tmpDC, &tr, bg);
                ::DeleteObject(bg);

                const RECT saved = m_rcItem;
                m_rcItem.left   = 0;
                m_rcItem.top    = -band.top;
                m_rcItem.right  = w;
                m_rcItem.bottom = h - band.top;
                PerfStopWatch sw;
                RenderContent(tmpDC);
                m_rcItem = saved;
                ::BitBlt(m_renderDC, 0, band.top - m_renderSliceTop, w, bandH, tmpDC, 0, 0, SRCCOPY);
                BUI_TRACE("CHATBUBBLE-RENDER-BAND kind=%d h=%d band=%d..%d cost=%.3fms",
                          (int)m_msg.kind, h, band.top, band.bottom, sw.ElapsedMs());
                ::SelectObject(tmpDC, oldBmp);
                ::DeleteObject(tmpBmp);
            }
            else
            {
                // 临时位图建不出来：退回整张重画，下次绘制时生效
                m_renderDirty = true;
                DuiControl::Invalidate();
            }
            if (tmpDC != NULL)
            {
                ::DeleteDC(tmpDC);
            }
        }
    }

    if (m_renderDC != NULL)
    {
        // 只把本行与脏区相交的那块从缓存位图搬回 hdc。缓存位图的第 0 行对应行内的 m_renderSliceTop。
        ::BitBlt(hdc, inter.left, inter.top,
                 inter.right - inter.left, inter.bottom - inter.top,
                 m_renderDC,
                 inter.left - m_rcItem.left, inter.top - m_rcItem.top - m_renderSliceTop,
                 SRCCOPY);
    }
    else
    {
        RenderContent(hdc);   // 缓存创建失败的退化路径：直接实绘
    }
}

// 把本行按 m_rcItem 的相对坐标实际绘制到 hdc（原 OnPaint 主体，逻辑未变）。
void ChatBubble::RenderContent(HDC hdc)
{
    // 图钉区域每次绘制都重新记录：本次若不画图钉（未 Pin、已变成撤回提示等），就保持
    // 为空矩形，点击与悬停都不会再命中上一次的位置。
    ::SetRectEmpty(&m_pinRectRel);

    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // ---- 时间分隔 / 撤回提示 ----
    if (m_msg.kind == kMsgTime)
    {
        // 本人撤回的文字类消息在 5 分钟内带「编辑」：提示文字照旧是灰色，后面隔一小段画链接色的「编辑」，
        // 鼠标停在「编辑」上时加下划线；两段合起来居中（docs/撤回后重新编辑方案.md §3.2）
        if (HasRecallEdit(RecallEditNowMs()))
        {
            int tipLeft = 0;
            int linkLeft = 0;
            int linkWidth = 0;
            RecallEditLayout(hdc, tipLeft, linkLeft, linkWidth);
            RECT tipRc = m_rcItem;
            tipRc.left = tipLeft;
            DrawGdiText(hdc, (LPCWSTR)m_msg.text, tipRc, kTimeTextPx, false, kTimeColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            RECT linkRc = m_rcItem;
            linkRc.left = linkLeft;
            DrawGdiText(hdc, TR(L"编辑"), linkRc, kTimeTextPx, false, kLinkFg,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP,
                        false /*italic*/, m_recallEditHover /*underline*/, false /*strike*/);
            return;
        }
        // 带 cardmsgid 的接龙提示行可以点击：鼠标停在文字上时加下划线
        const bool tipHover = (m_msg.relayCardMsgid > 0 && m_relayBtnHover);
        DrawGdiText(hdc, (LPCWSTR)m_msg.text, m_rcItem, kTimeTextPx, false, kTimeColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP,
                    false /*italic*/, tipHover /*underline*/, false /*strike*/);
        return;
    }

    const bool isMe = m_msg.fromMe;
    // 群聊：对方气泡上方显示发送者名。
    const bool showSender = (!isMe && !m_msg.senderName.IsEmpty());

    // 多选模式：行首让出 kCheckColW 宽的勾选列，气泡内容整体右移。
    const bool multi = ParentInMultiSelect();
    const int  selW  = multi ? kCheckColW : 0;
    const int  cx    = x + selW;   // 内容区左边界
    const int  cw    = w - selW;   // 内容区宽

    // 头像底色 / 字：优先用消息自带的发送者头像（真实单聊每条都带本端/对端
    // 头像；群聊每条发送者不同）；无则回退到 mock 的 ChatMe / ChatPartner。
    CString  avatarText;
    COLORREF avatarColor = RGB(0xFF, 0x7A, 0x45);
    if (!m_msg.senderAvatarText.IsEmpty())
    {
        avatarText  = m_msg.senderAvatarText;
        avatarColor = m_msg.senderAvatarColor;
    }
    else if (isMe)
    {
        avatarText  = ChatMe().avatarText;
        avatarColor = ChatMe().avatarColor;
    }
    else
    {
        avatarText  = ChatPartner().avatarText;
        avatarColor = ChatPartner().avatarColor;
    }
    // 发送者离线（本人的气泡则是本端掉线）时，字形头像的底色换成等亮度的灰色
    if (m_avatarGray)
    {
        avatarColor = balloonmain::MakeGrayColor(avatarColor);
    }

    // ---- 头像位置 ----
    const int avX = isMe ? (cx + cw - kEdgePad - kAvatarSize) : (cx + kEdgePad);
    const int avY = y + kRowVMargin;

    // ---- 气泡尺寸 ----
    const int maxBubbleW = (int)(cw * kBubbleMaxWidthRatio);
    int bubbleW = 0;
    int bubbleH = kTextBubbleH;
    switch (m_msg.kind)
    {
    case kMsgText:
        if (m_measuredWidth >= 0)
        {
            bubbleW = m_textBubbleW;
            bubbleH = m_textBubbleH;
        }
        else
        {
            // 尚未折行测量时的单行回退：按本条消息字号估宽。
            bubbleW = MeasureTextWidth(hdc, (LPCWSTR)m_msg.text,
                                       ResolveTextStyle(m_msg).sizePx)
                      + kTextPadH * 2;
            bubbleH = kTextBubbleH;
        }
        // 代码卡片可以用满两侧头像之间的整列宽度，其余文字气泡最宽为内容区的 62%
        if (CodeActive())
        {
            const int codeMaxW = CodeColumnWidth(cw);
            if (bubbleW > codeMaxW)
            {
                bubbleW = codeMaxW;
            }
        }
        else if (bubbleW > maxBubbleW)
        {
            bubbleW = maxBubbleW;
        }
        break;
    case kMsgEmoji:
        bubbleW = 56;
        bubbleH = kEmojiBubbleH;
        break;
    case kMsgImage:
        bubbleW = m_msg.imageW + kImageBubblePad;
        bubbleH = m_msg.imageH + kImageBubblePad;
        break;
    case kMsgFile:
        bubbleW = kFileCardWidth;
        bubbleH = FileCardHeight(m_msg);
        break;
    case kMsgFolder:
        bubbleW = kFolderBubbleW;
        bubbleH = FolderBubbleHeight(m_msg.folderState, m_msg.fromMe, m_msg.folderErrCode);
        break;
    case kMsgMerged:
        bubbleW = kMergedBubbleW;
        bubbleH = MergedBubbleHeight(m_msg.mergedLines.size());
        break;
    case kMsgRelay:
        bubbleW = kRelayBubbleW;
        bubbleH = RelayBubbleHeight(m_relayNoteLines.size(), m_msg.relayLines.size(),
                                    RelayButtonLabel(m_msg.relayButton) != NULL);
        break;
    case kMsgFileShare:
        bubbleW = kShareBubbleW;
        bubbleH = ShareBubbleHeight(!m_msg.sharePath.IsEmpty(), ShareCardButtonLabel(m_msg.shareButton) != NULL);
        break;
    case kMsgScreenShare:
        bubbleW = kShareBubbleW;
        bubbleH = ShareBubbleHeight(true, m_msg.screenShareHasButton, !m_msg.screenShareNotice.IsEmpty(),
                                    m_shareDescLines.size());
        break;
    case kMsgGroupInvite:
        bubbleW = kShareBubbleW;
        bubbleH = ShareBubbleHeight(true, m_msg.inviteHasButton);
        break;
    case kMsgSecretInvite:
        bubbleW = kShareBubbleW;
        bubbleH = ShareBubbleHeight(true, m_msg.secretInviteButtons > 0);
        break;
    case kMsgVoice:
        //宽度随时长变化，不超过本行气泡的最大宽度
        bubbleW = VoiceBubbleWidth(m_msg.voiceDuration);
        if (bubbleW > maxBubbleW)
        {
            bubbleW = maxBubbleW;
        }
        //高度与 GetRowHeight 同一口径
        bubbleH = (m_measuredWidth >= 0) ? m_textBubbleH : kTextBubbleH;
        break;
    default:
        break;
    }

    // 气泡列：自己靠右、对方靠左。
    const int colLeft  = avX + kAvatarSize + kAvatarGap;
    const int colRight = avX - kAvatarGap;
    const int bubbleX  = isMe ? (colRight - bubbleW) : colLeft;

    int contentTop = y + kRowVMargin;
    // 群聊：对方气泡上方留出发送者名一行（名字本身在文字层绘制）。
    if (showSender)
    {
        contentTop += kSenderNameHeight;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // ---- 头像 ----
    // 真图头像(facetype=2)命中本地缓存时画圆角真图，否则填底色 + 后面画字形。
    bool drewAvatarImage = false;
    {
        Gdiplus::GraphicsPath av;
        AddRoundRect(av, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                     (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                     kAvatarRadius);

        // 真图头像从共享缓存取：同一张头像只解码、缩放一次，已经是本气泡头像的尺寸、
        // 带好圆角与预乘透明通道（见 ChatAvatarBitmapCache.h）。解码失败缓存里记为空，
        // 这里退回画字形。
        PerfStopWatch avatarStopWatch;
        HBITMAP avatarBmp = NULL;
        if (!m_msg.senderAvatarImagePath.IsEmpty())
        {
            const std::wstring avatarPath((LPCWSTR)m_msg.senderAvatarImagePath);
            // 发送者离线时取灰度副本；副本取不到时退回彩色原图，不画成字形
            if (m_avatarGray)
            {
                avatarBmp = ChatAvatarBitmapCache::Inst().GetGray(avatarPath, kAvatarSize, (int)kAvatarRadius);
            }
            if (avatarBmp == NULL)
            {
                avatarBmp = ChatAvatarBitmapCache::Inst().Get(avatarPath, kAvatarSize, (int)kAvatarRadius);
            }
        }
        const double avatarFetchMs = avatarStopWatch.ElapsedMs();

        DIBSECTION avatarDib;
        if (avatarBmp != NULL
            && ::GetObject(avatarBmp, sizeof(avatarDib), &avatarDib) == sizeof(avatarDib)
            && avatarDib.dsBm.bmBits != NULL)
        {
            // 用 GDI+ 包一层直接引用位图像素（不拷贝），按预乘透明通道 1:1 贴上去：
            // 尺寸已经对上，不再做缩放；圆角外的像素是透明的，不必再设裁剪区。
            avatarStopWatch.Restart();
            Gdiplus::Bitmap wrap(avatarDib.dsBm.bmWidth, avatarDib.dsBm.bmHeight,
                                 avatarDib.dsBm.bmWidthBytes, PixelFormat32bppPARGB,
                                 static_cast<BYTE*>(avatarDib.dsBm.bmBits));
            g.DrawImage(&wrap, avX, avY, kAvatarSize, kAvatarSize);
            drewAvatarImage = true;
            BUI_TRACE("CHATBUBBLE-AVATAR fetch=%.3fms draw=%.3fms",
                      avatarFetchMs, avatarStopWatch.ElapsedMs());
        }
        else
        {
            // 无真图 / 解码失败：填底色，字形稍后在文字层绘制。底色按像素格对齐地填充，与头像字、照片头像对齐（见 AvatarGlyph.h）
            Gdiplus::SolidBrush avBrush(Gdiplus::Color(255,
                GetRValue(avatarColor), GetGValue(avatarColor),
                GetBValue(avatarColor)));
            balloonmain::FillAvatarBackground(g, avBrush, av);
        }
    }

    // ---- 气泡底 ----
    const int bubbleY = contentTop;
    {
        Gdiplus::GraphicsPath bub;
        AddRoundRect(bub, (Gdiplus::REAL)bubbleX, (Gdiplus::REAL)bubbleY,
                     (Gdiplus::REAL)bubbleW, (Gdiplus::REAL)bubbleH, kBubbleRadius);
        // 文件"已删除 / 失效"态：整条气泡置灰（淡化），区别于正常发 / 收气泡。
        Gdiplus::Color bg = isMe ? kBubbleSelf : kBubbleOther;
        if (m_msg.kind == kMsgFile && m_msg.fileState == kFileDeleted)
        {
            bg = Gdiplus::Color(255, 0xED, 0xEA, 0xE8);
        }
        Gdiplus::SolidBrush bubBrush(bg);
        g.FillPath(&bubBrush, &bub);
    }

    // 图片无法显示时的失败类型与破图占位的排版。在形状层算好，后面的 GDI 文字层据此画
    // 说明文字 —— 两层共用同一份结论，不会出现图形画了破图、文字却按别的状态写的情况。
    ChatImageFailure       imageFailure = kChatImageNotFailed;
    ImagePlaceholderLayout imageFailLayout = ComputeImagePlaceholderLayout(RECT(), 0, 0);

    // ---- 图片占位 / 文件类型块（GDI+）----
    if (m_msg.kind == kMsgImage)
    {
        const Gdiplus::Rect imgRect(bubbleX + 4, bubbleY + 4,
                                    m_msg.imageW, m_msg.imageH);

        // 记下图片在宿主坐标系里的矩形，供双击命中测试（双击图片打开看图窗）。
        m_imageRect.left   = bubbleX + 4;
        m_imageRect.top    = bubbleY + 4;
        m_imageRect.right  = bubbleX + 4 + m_msg.imageW;
        m_imageRect.bottom = bubbleY + 4 + m_msg.imageH;

        // 先判断这张图是否失败：上传失败、下载失败，或状态正常但本地文件丢失 / 无法解码
        // （ChatImageCache 记过失败）。失败就画破图占位，保持原图的显示尺寸不变。
        std::wstring imgPath((LPCWSTR)m_msg.imagePath);
        const bool hasPath = !m_msg.imagePath.IsEmpty();
        const bool decodeFailed = hasPath && m_msg.imageState == kChatImageStateReady
            && ChatImageCache::Inst().IsFailed(imgPath, m_msg.imageW, m_msg.imageH);
        imageFailure = ResolveChatImageFailure(m_msg.imageState, hasPath, decodeFailed);

        // 用 ChatImageCache 取"后台解码 + 缩放好的展示尺寸图"：命中即直接 1:1 画；未命中
        // 则发起一次后台请求并先画占位 —— 原图解码与首次缩放都在后台线程做，UI 线程零
        // 冻结（后台完成后会 PostMessage(WM_CHAT_IMAGE_READY) 让聊天窗重绘，届时命中缓存）。
        Gdiplus::Bitmap* ready = (m_msg.imageW > 0 && m_msg.imageH > 0)
            ? ChatImageCache::Inst().Get(imgPath, m_msg.imageW, m_msg.imageH)
            : NULL;
        if (imageFailure != kChatImageNotFailed)
        {
            const SIZE cap = MeasureImagePlaceholderCaption(
                hdc, ChatImageFailureCaption(imageFailure), kImagePlaceholderCaptionPx);
            imageFailLayout = ComputeImagePlaceholderLayout(m_imageRect, cap.cx, cap.cy);
            PaintImagePlaceholderShapes(g, m_imageRect, imageFailLayout, kImagePlaceholderBg);
        }
        else if (ready != NULL)
        {
            g.SetClip(imgRect);
            g.DrawImage(ready, imgRect);
            g.ResetClip();
        }
        else
        {
            if (!m_msg.imagePath.IsEmpty())
            {
                ChatImageCache::Inst().Request(imgPath, m_msg.imageW, m_msg.imageH);
            }
            // 未就绪 / 无图：占位块 + 斜向纹理（后台缩放好后会重绘换成真图）。
            Gdiplus::SolidBrush imgBrush(Gdiplus::Color(255, 0xEC, 0xEA, 0xE6));
            g.FillRectangle(&imgBrush, imgRect);
            g.SetClip(imgRect);
            Gdiplus::Pen hatch(Gdiplus::Color(40, 0, 0, 0), 1.0f);
            for (int hx = bubbleX + 4 - m_msg.imageH; hx < bubbleX + 4 + m_msg.imageW;
                 hx += 14)
            {
                g.DrawLine(&hatch, (Gdiplus::REAL)hx, (Gdiplus::REAL)(bubbleY + 4),
                           (Gdiplus::REAL)(hx + m_msg.imageH),
                           (Gdiplus::REAL)(bubbleY + 4 + m_msg.imageH));
            }
            g.ResetClip();
        }

        // 上传中 / 下载中：盖一层半透明黑遮罩，衬白色"上传中 NN%" / "下载中"（状态文字在
        // 后面的 GDI 文字层画）。失败态已改画破图占位，不再盖原先那层红色遮罩。
        if (m_msg.imageState == kChatImageStateUploading
            || m_msg.imageState == kChatImageStateDownloading)
        {
            Gdiplus::SolidBrush mask(Gdiplus::Color(110, 0, 0, 0));
            g.FillRectangle(&mask, imgRect);
        }
    }
    else if (m_msg.kind == kMsgFile)
    {
        // ---- 文件卡片的图形部分（类型色块、角标、进度条），与聊天记录查看窗共用 ----
        const RECT card = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        PaintFileCardShapes(g, card, m_msg);

        // ---- 底部操作链接的文字与点击矩形在下面的 GDI 文字层排布、就地填 m_fileBtnRect /
        //      m_fileBtnCount；此处先清空（仅文件行需要）。----
        m_fileBtnCount = 0;
        for (int bi = 0; bi < 3; ++bi)
            ::SetRectEmpty(&m_fileBtnRect[bi]);
    }
    else if (m_msg.kind == kMsgFolder)
    {
        const int st    = m_msg.folderState;
        const int iconX = bubbleX + kFolderPadH;
        const int iconY = bubbleY + kFolderIconTop;

        // ---- 图标底块（圆角方块）+ 块内的文件夹图标 ----
        // 与文件气泡的类型色块同尺寸同圆角，只是底色固定、块内画的是线性图标而不是
        // 扩展名文字 —— 两种气泡并排时，形状一致而内容有别，一眼分得清。
        {
            Gdiplus::GraphicsPath blockPath;
            AddRoundRect(blockPath, (Gdiplus::REAL)iconX, (Gdiplus::REAL)iconY,
                         (Gdiplus::REAL)kFolderIconSize, (Gdiplus::REAL)kFolderIconSize, 6.0f);
            Gdiplus::SolidBrush blockBrush(Gdiplus::Color(255,
                GetRValue(kFolderBlockColor), GetGValue(kFolderBlockColor),
                GetBValue(kFolderBlockColor)));
            g.FillPath(&blockBrush, &blockPath);
        }
        {
            //图标在底块内居中；描边宽度按 viewBox 24 的口径给（与工具栏那枚同一套画法）
            const Gdiplus::REAL glyphX = (Gdiplus::REAL)(iconX + (kFolderIconSize - kFolderGlyphSize) / 2);
            const Gdiplus::REAL glyphY = (Gdiplus::REAL)(iconY + (kFolderIconSize - kFolderGlyphSize) / 2);
            DrawChatIcon(g, kChatIconFolder, glyphX, glyphY,
                         (Gdiplus::REAL)kFolderGlyphSize,
                         Gdiplus::Color(255, 255, 255, 255), 2.0f);
        }

        // ---- 进度条（传输中 / 失败 / 已过保留期 / 用户取消）----
        // 画不画一律问 FolderStateHasProgress，绝不在这里另写一份状态判断 —— 它与
        // FolderBubbleHeight 里留高度的那次判断必须是同一个答案。
        if (FolderStateHasProgress(st))
        {
            const int barX = bubbleX + kFolderPadH;
            const int barY = bubbleY + kFolderHeaderH;
            const int barW = bubbleW - kFolderPadH * 2;
            {
                Gdiplus::GraphicsPath track;
                AddRoundRect(track, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                             (Gdiplus::REAL)barW, (Gdiplus::REAL)kFolderProgressH, 2.5f);
                Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 0xE3, 0xE0, 0xDC));
                g.FillPath(&trackBrush, &track);
            }
            int pct = m_msg.folderProgress;
            if (pct < 0)
                pct = 0;
            if (pct > 100)
                pct = 100;
            const int fillW = barW * pct / 100;
            if (fillW > 0)
            {
                //传输中用主题橙，失败用红（停在中断处），已过保留期与用户取消用灰 ——
                //后两者点不动续传了，颜色上就不该再显得"还在进行"
                COLORREF pc = (st == kFolderTransferring) ? RGB(0xFF, 0x7A, 0x45)
                            : (st == kFolderFailed)       ? RGB(0xE5, 0x39, 0x35)
                            :                               RGB(0xB0, 0xAD, 0xA8);
                Gdiplus::GraphicsPath fill;
                AddRoundRect(fill, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                             (Gdiplus::REAL)fillW, (Gdiplus::REAL)kFolderProgressH, 2.5f);
                Gdiplus::SolidBrush fb(Gdiplus::Color(255,
                    GetRValue(pc), GetGValue(pc), GetBValue(pc)));
                g.FillPath(&fb, &fill);
            }
        }

        // ---- 底部操作与文件气泡同为文字超链接：文字与命中矩形在下面的 GDI 文字层
        //      按各自文字宽度从左到右排布、就地填 m_folderBtnRect / m_folderBtnCount；
        //      此处先清空（仅文件夹行需要）。----
        m_folderBtnCount = 0;
        for (int bi = 0; bi < kMaxChatFolderActions; ++bi)
        {
            ::SetRectEmpty(&m_folderBtnRect[bi]);
            m_folderBtnEnabled[bi] = false;
        }
    }
    else if (m_msg.kind == kMsgMerged)
    {
        // 合并卡片：预览行与底部文字之间画一条分隔线，文字在下面的 GDI 文字层画。
        // 记下卡片矩形，供点击命中（点卡片任意位置打开聊天记录查看窗）。
        m_mergedRect.left   = bubbleX;
        m_mergedRect.top    = bubbleY;
        m_mergedRect.right  = bubbleX + bubbleW;
        m_mergedRect.bottom = bubbleY + bubbleH;
        const Gdiplus::REAL lineY = (Gdiplus::REAL)(bubbleY + bubbleH - kMergedFooterH);
        Gdiplus::Pen divider(kMergedDivider, 1.0f);
        g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kMergedPadH), lineY,
                   (Gdiplus::REAL)(bubbleX + bubbleW - kMergedPadH), lineY);
    }
    else if (m_msg.kind == kMsgRelay)
    {
        // 接龙卡片：标题前画接龙图标；有按钮时在按钮行上方画一条分隔线。文字在下面的 GDI
        // 文字层画。记下整张卡片与按钮行的矩形，供点击命中。
        m_relayRect.left   = bubbleX;
        m_relayRect.top    = bubbleY;
        m_relayRect.right  = bubbleX + bubbleW;
        m_relayRect.bottom = bubbleY + bubbleH;
        DrawChatIcon(g, kChatIconRelay, (Gdiplus::REAL)(bubbleX + kRelayPadH),
                     (Gdiplus::REAL)(bubbleY + kRelayTitleTop + (kRelayTitleH - kRelayIconSize) / 2),
                     (Gdiplus::REAL)kRelayIconSize, kRelayIconColor, kRelayIconStroke);
        if (RelayButtonLabel(m_msg.relayButton) != NULL)
        {
            const int btnTop = bubbleY + bubbleH - kRelayBtnH;
            Gdiplus::Pen divider(kRelayDivider, 1.0f);
            g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kRelayPadH), (Gdiplus::REAL)btnTop,
                       (Gdiplus::REAL)(bubbleX + bubbleW - kRelayPadH), (Gdiplus::REAL)btnTop);
            m_relayBtnRect.left   = bubbleX;
            m_relayBtnRect.top    = btnTop;
            m_relayBtnRect.right  = bubbleX + bubbleW;
            m_relayBtnRect.bottom = bubbleY + bubbleH;
        }
        else
        {
            ::SetRectEmpty(&m_relayBtnRect);
        }
    }
    else if (m_msg.kind == kMsgVoice)
    {
        // 语音气泡：左侧喇叭图标（播放中按帧轮换声波道数），中间波形，右侧时长在下面的 GDI 文字层画。
        // 记下气泡矩形供点击命中；本人发的「发送中」在左侧画静态圆环，对方发来且未听过的在右侧画红点
        m_voiceRect.left   = bubbleX;
        m_voiceRect.top    = bubbleY;
        m_voiceRect.right  = bubbleX + bubbleW;
        m_voiceRect.bottom = bubbleY + bubbleH;
        ::SetRectEmpty(&m_voiceFailRect);
        //喇叭图标、波形与对方发来未听的红点：与语音条控件共用 ChatVoicePaint 的画法
        PaintVoiceBarShapes(g, m_voiceRect, m_msg, true);
        //发送中：左侧静态圆环（位置与发送失败的红色感叹号相同，2026-10-04 用户确认）
        if (m_msg.fromMe && !m_msg.sendFailed && m_msg.voiceState == kVoiceStateSending)
        {
            DrawDownloadingBadge(g, (float)(bubbleX - kFailMarkGap - kFailMarkSize / 2),
                                 (float)(bubbleY + bubbleH / 2), (float)kFailMarkSize);
        }
    }
    else if (m_msg.kind == kMsgScreenShare)
    {
        // 屏幕共享卡片：标题前画屏幕共享图标；有按钮时在按钮行上方画一条分隔线，几何与文件共享卡片相同。
        // 文字在下面的 GDI 文字层画。记下整张卡片与按钮行的矩形（与文件共享卡片共用这两个成员），供点击命中。
        m_shareRect.left   = bubbleX;
        m_shareRect.top    = bubbleY;
        m_shareRect.right  = bubbleX + bubbleW;
        m_shareRect.bottom = bubbleY + bubbleH;
        //群通话卡片与一对一通话卡片复用这套版式，标题前按媒体类型画摄像机或电话
        //群公告卡片画喇叭、群备忘卡片画日历（2026-10-05 起同样复用这套版式）
        const bool callCard = m_msg.screenShareIsGroupCall || m_msg.screenShareIsCallCard;
        int cardIcon = !callCard ? kChatIconScreenShare
                       : (m_msg.groupCallMedia == kGroupCallCardVideo ? kChatIconVideo : kChatIconPhone);
        if (m_msg.screenShareIsAnnouncement)
            cardIcon = kChatIconAnnounce;
        else if (m_msg.screenShareIsMemo)
            cardIcon = kChatIconMemo;
        else if (m_msg.screenShareIsCloudRecord)
            cardIcon = m_msg.cloudRecordAudioOnly ? kChatIconMic : kChatIconRecord;
        DrawChatIcon(g, cardIcon, (Gdiplus::REAL)(bubbleX + kSharePadH),
                     (Gdiplus::REAL)(bubbleY + kShareTitleTop + (kShareTitleH - kShareIconSize) / 2),
                     (Gdiplus::REAL)kShareIconSize, kShareIconColor, kShareIconStroke);
        if (m_msg.screenShareHasButton)
        {
            const int btnTop = bubbleY + bubbleH - kShareBtnH;
            Gdiplus::Pen divider(kShareDivider, 1.0f);
            g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kSharePadH), (Gdiplus::REAL)btnTop,
                       (Gdiplus::REAL)(bubbleX + bubbleW - kSharePadH), (Gdiplus::REAL)btnTop);
            m_shareBtnRect.left   = bubbleX;
            m_shareBtnRect.top    = btnTop;
            m_shareBtnRect.right  = bubbleX + bubbleW;
            m_shareBtnRect.bottom = bubbleY + bubbleH;
        }
        else
        {
            ::SetRectEmpty(&m_shareBtnRect);
        }
    }
    else if (m_msg.kind == kMsgGroupInvite)
    {
        // 入群邀请卡片：与屏幕共享卡片同一套几何。标题前画入群邀请图标；有按钮时在按钮行上方画一条分隔线。
        // 文字在下面的 GDI 文字层画。记下整张卡片与按钮行的矩形（与共享卡片共用这两个成员），供点击命中。
        m_shareRect.left   = bubbleX;
        m_shareRect.top    = bubbleY;
        m_shareRect.right  = bubbleX + bubbleW;
        m_shareRect.bottom = bubbleY + bubbleH;
        DrawChatIcon(g, kChatIconGroupInvite, (Gdiplus::REAL)(bubbleX + kSharePadH),
                     (Gdiplus::REAL)(bubbleY + kShareTitleTop + (kShareTitleH - kShareIconSize) / 2),
                     (Gdiplus::REAL)kShareIconSize, kShareIconColor, kShareIconStroke);
        if (m_msg.inviteHasButton)
        {
            const int btnTop = bubbleY + bubbleH - kShareBtnH;
            Gdiplus::Pen divider(kShareDivider, 1.0f);
            g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kSharePadH), (Gdiplus::REAL)btnTop,
                       (Gdiplus::REAL)(bubbleX + bubbleW - kSharePadH), (Gdiplus::REAL)btnTop);
            m_shareBtnRect.left   = bubbleX;
            m_shareBtnRect.top    = btnTop;
            m_shareBtnRect.right  = bubbleX + bubbleW;
            m_shareBtnRect.bottom = bubbleY + bubbleH;
        }
        else
        {
            ::SetRectEmpty(&m_shareBtnRect);
        }
    }
    else if (m_msg.kind == kMsgSecretInvite)
    {
        // 密聊邀请卡片：与入群邀请卡片同一套几何。标题前画挂锁；有按钮时在按钮行上方画一条横向分隔线，两个按钮时
        // 再在按钮行正中画一条竖向分隔线（左「同意」右「忽略」）。文字在下面的 GDI 文字层画。记下整张卡片与按钮行的
        // 矩形（与共享卡片共用这两个成员），按钮的左右分界由 SecretInviteButtonAt 按同一口径计算。
        m_shareRect.left   = bubbleX;
        m_shareRect.top    = bubbleY;
        m_shareRect.right  = bubbleX + bubbleW;
        m_shareRect.bottom = bubbleY + bubbleH;
        DrawChatIcon(g, kChatIconLock, (Gdiplus::REAL)(bubbleX + kSharePadH),
                     (Gdiplus::REAL)(bubbleY + kShareTitleTop + (kShareTitleH - kShareIconSize) / 2),
                     (Gdiplus::REAL)kShareIconSize, kShareIconColor, kShareIconStroke);
        if (m_msg.secretInviteButtons > 0)
        {
            const int btnTop = bubbleY + bubbleH - kShareBtnH;
            Gdiplus::Pen divider(kShareDivider, 1.0f);
            g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kSharePadH), (Gdiplus::REAL)btnTop,
                       (Gdiplus::REAL)(bubbleX + bubbleW - kSharePadH), (Gdiplus::REAL)btnTop);
            if (m_msg.secretInviteButtons > 1)
            {
                const int midX = bubbleX + bubbleW / 2;
                g.DrawLine(&divider, (Gdiplus::REAL)midX, (Gdiplus::REAL)(btnTop + kSecretBtnDividerInset),
                           (Gdiplus::REAL)midX, (Gdiplus::REAL)(bubbleY + bubbleH - kSecretBtnDividerInset));
            }
            m_shareBtnRect.left   = bubbleX;
            m_shareBtnRect.top    = btnTop;
            m_shareBtnRect.right  = bubbleX + bubbleW;
            m_shareBtnRect.bottom = bubbleY + bubbleH;
        }
        else
        {
            ::SetRectEmpty(&m_shareBtnRect);
        }
    }
    else if (m_msg.kind == kMsgFileShare)
    {
        // 共享卡片：有按钮时在按钮行上方画一条分隔线。标题前的系统类型图标与文字在下面的 GDI 文字层画。
        // 记下整张卡片与按钮行的矩形，供点击命中。
        m_shareRect.left   = bubbleX;
        m_shareRect.top    = bubbleY;
        m_shareRect.right  = bubbleX + bubbleW;
        m_shareRect.bottom = bubbleY + bubbleH;
        if (ShareCardButtonLabel(m_msg.shareButton) != NULL)
        {
            const int btnTop = bubbleY + bubbleH - kShareBtnH;
            Gdiplus::Pen divider(kShareDivider, 1.0f);
            g.DrawLine(&divider, (Gdiplus::REAL)(bubbleX + kSharePadH), (Gdiplus::REAL)btnTop,
                       (Gdiplus::REAL)(bubbleX + bubbleW - kSharePadH), (Gdiplus::REAL)btnTop);
            m_shareBtnRect.left   = bubbleX;
            m_shareBtnRect.top    = btnTop;
            m_shareBtnRect.right  = bubbleX + bubbleW;
            m_shareBtnRect.bottom = bubbleY + bubbleH;
        }
        else
        {
            ::SetRectEmpty(&m_shareBtnRect);
        }
    }

    // ---- 文字层（GDI）----

    // 头像字母（已画真图则跳过，避免字形盖在真图上）。
    if (!drewAvatarImage && !avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        // 按字形笔画外框居中（见 AvatarGlyph.h），DrawText 按单元格居中会让字偏低
        balloonmain::DrawCenteredGlyphGdi(hdc, (LPCWSTR)avatarText, avRc, 16, true, kAvatarText);
    }

    // 群聊发送者名（对方气泡上方）。
    if (showSender)
    {
        RECT snRc = { bubbleX + 2, y + kRowVMargin,
                      cx + cw, y + kRowVMargin + kSenderNameHeight };
        DrawGdiText(hdc, (LPCWSTR)m_msg.senderName, snRc, 11, false,
                    RGB(0x8A, 0x8A, 0x8A),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        //机器人发送者：名字后面画「机器人」徽章（docs/机器人方案.md §4.2）
        if (m_msg.senderIsBot)
        {
            const int nameW = chatpaint::MeasureTextWidth(hdc, (LPCWSTR)m_msg.senderName, 11, false);
            chatpaint::DrawTextBadgeInRow(hdc, TR(L"机器人"), chatpaint::kBotBadgeBgColor, chatpaint::kBotBadgeTextColor,
                                          snRc.left + nameW + chatpaint::kBotBadgeGapPx, snRc.top, snRc.bottom,
                                          snRc.right);
        }
    }

    // 引用块（气泡内部顶端）：左侧一条竖线 + 两行文字（人名 / 被引用内容）。
    // 位置与 TextOriginInHost、Measure 里的算法必须一致 —— 三处都按
    // 「气泡内上边距之下、正文之上」这一块来算，改一处就要一起改。
    if (m_msg.hasQuote)
    {
        const int qLeft  = bubbleX + kTextPadH;
        const int qTop    = bubbleY + kTextPadV;
        const int qRight = bubbleX + bubbleW - kTextPadH;

        // 左侧竖线：标明这一块是引用而不是正文。
        RECT barRc = { qLeft, qTop, qLeft + kQuoteBarW, qTop + kQuoteBlockH };
        HBRUSH barBrush = ::CreateSolidBrush(kQuoteName);
        if (barBrush != NULL)
        {
            ::FillRect(hdc, &barRc, barBrush);
            ::DeleteObject(barBrush);
        }

        // 被引用的是图片时，竖线右侧先画一张方形缩略图，文字再往右让开。
        int qTextL = qLeft + kQuoteBarW + kQuoteBarGap;
        if (m_msg.quoteMsgType == kMsgImage)
        {
            const Gdiplus::Rect thumbRc(qTextL, qTop, kQuoteThumbSize, kQuoteThumbSize);
            Gdiplus::Graphics gq(hdc);
            gq.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // 与图片气泡同一套缓存：命中就画，未命中先入队后台解码 + 缩放，
            // 完成后 ChatImageCache 会 PostMessage(WM_CHAT_IMAGE_READY) 让聊天窗重绘。
            // 方框是固定的正方形，按居中裁切缩放，非方形的图不会被压扁；Get 与 Request 须传同一个方式。
            std::wstring thumbPath((LPCWSTR)m_msg.quoteImagePath);
            Gdiplus::Bitmap* thumb = thumbPath.empty()
                ? NULL
                : ChatImageCache::Inst().Get(thumbPath, kQuoteThumbSize, kQuoteThumbSize, kImageFitCover);
            if (thumb != NULL)
            {
                // 显式裁到目标方框内再画。缓存返回的位图未必正好是这个尺寸，
                // 不裁的话长宽比不同的图会画出方框、溢出到气泡外（与图片气泡同样处理）。
                gq.SetClip(thumbRc);
                gq.DrawImage(thumb, thumbRc);
                gq.ResetClip();
            }
            else
            {
                if (!thumbPath.empty())
                {
                    ChatImageCache::Inst().Request(thumbPath, kQuoteThumbSize, kQuoteThumbSize, kImageFitCover);
                    // 每帧都会走到这里，故只在第一次入队时记一条，避免刷屏。
                    // 若这条只出现一两次之后缩略图仍不出现，说明解码没回来或失败；
                    // 若反复出现，说明每次重绘都当成新请求 —— 那是 key 对不上。
                    if (!m_quoteThumbLogged)
                    {
                        m_quoteThumbLogged = true;
                        LOG_INFO("引用缩略图入队解码: %dx%d, path=%s.",
                                 kQuoteThumbSize, kQuoteThumbSize,
                                 (LPCSTR)CW2A(m_msg.quoteImagePath, CP_UTF8));
                    }
                }
                // 未就绪（或本端还没下到这张图）：先画一个浅灰占位块，
                // 下载 / 解码完成后重绘自然换成真图。
                Gdiplus::SolidBrush ph(Gdiplus::Color(255, 0xEC, 0xEA, 0xE6));
                gq.FillRectangle(&ph, thumbRc);
            }
            qTextL += kQuoteThumbSize + kQuoteThumbGap;
        }
        RECT qnRc = { qTextL, qTop, qRight, qTop + kQuoteLineH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.quoteName, qnRc, 11, false, kQuoteName,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 第二行：被引用内容。过长省略号收尾 —— 引用块高度固定，绝不因内容长短变形。
        // 记下引用块矩形供左键命中用。转成行内相对坐标 —— 绘制可能发生在渲染缓存里，
        // 那时 m_rcItem 被临时移到 (0,0)，存宿主坐标会失效。
        m_quoteRectRel.left   = qLeft - m_rcItem.left;
        m_quoteRectRel.top    = qTop - m_rcItem.top;
        m_quoteRectRel.right  = qRight - m_rcItem.left;
        m_quoteRectRel.bottom = qTop + kQuoteBlockH - m_rcItem.top;

        RECT qcRc = { qTextL, qTop + kQuoteLineH + 4, qRight, qTop + kQuoteBlockH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.quoteText, qcRc, 11, false, kQuoteText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    // 气泡内容文字。
    if (m_msg.kind == kMsgText && CodeActive())
    {
        // 代码卡片：画在气泡里，四周留出 kCodeBubblePad 的气泡边；带引用时接在引用块下面（与 MeasureCode 同一口径）。
        // 卡片位置记成行内相对坐标（理由同 m_quoteRectRel），供命中测试
        const int cardLeft = bubbleX + kCodeBubblePad;
        const int cardTop = bubbleY + (m_msg.hasQuote ? kTextPadV + kQuoteBlockH + kQuoteGap : kCodeBubblePad);
        PaintCodeBlock(hdc, m_codeLayout, cardLeft, cardTop, m_codeHover);
        ::SetRect(&m_codeCardRel, cardLeft - m_rcItem.left, cardTop - m_rcItem.top,
                  cardLeft - m_rcItem.left + m_codeLayout.m_size.cx, cardTop - m_rcItem.top + m_codeLayout.m_size.cy);
    }
    else if (m_msg.kind == kMsgText && MarkdownActive())
    {
        // Markdown 卡片（docs/Markdown消息方案.md 第 11.3 节）：标题、最多三行摘要、分隔线、底部「M↓ Markdown 文档」。
        // 卡片矩形记成行内相对坐标（理由同 m_quoteRectRel），供点击命中
        ::SetRect(&m_mdCardRel, bubbleX - m_rcItem.left, bubbleY - m_rcItem.top,
                  bubbleX + bubbleW - m_rcItem.left, bubbleY + bubbleH - m_rcItem.top);
        // 卡片内容的绘制与消息记录窗的独立卡片控件共用（ChatMarkdownCard），底色就是气泡
        const RECT cardRc = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        PaintMarkdownCardContent(hdc, cardRc, m_mdCardTitle, m_mdCardSummary, m_mdCardSummaryH);
    }
    else if (m_msg.kind == kMsgText)
    {
        RECT txRc = { bubbleX + kTextPadH, bubbleY + kTextPadV,
                      bubbleX + bubbleW - kTextPadH, bubbleY + bubbleH - kTextPadV };
        if (m_msg.hasQuote)
        {
            // 引用块占据气泡内部顶端，正文从它下方开始。这个偏移与 Measure 里
            // 计入气泡高度、以及 TextOriginInHost 里算选区原点的那两处必须一致。
            txRc.top += kQuoteBlockH + kQuoteGap;
        }
        // 按本条消息的字体设置渲染（默认时等同原 kBubbleTextPx / 默认色 / 不加粗）。
        const BubbleTextStyle st = ResolveTextStyle(m_msg);
        // 所有文字气泡（纯文字 / 含表情 / 含 @）走同一条手写折行排版，与 Measure
        // 产出选区模型的那次排版是同一套规则、同一套字体。分四层，自下而上：
        //   一、@ 片段的圆角底色块
        //   二、选中文字的高亮底
        //   三、正文（表情画成小图，文字用正文色）
        //   四、@ 片段按矩形裁剪、用高亮色重画那几个字
        // 底色块在最下面，选区高亮压在它上面，文字再压在两者之上，谁都不会被盖住。
        POINT origin;
        origin.x = txRc.left;
        origin.y = txRc.top;

        // 正文的排版结果按绘制样式、文本区宽度缓存（与 DrawInlineContent 内部的排版完全相同）：
        // 整张缓存重画时由 OnPaint 置为失效、在这里重新排版；选区变化时只局部重画，沿用这份
        // 结果，不再排版。各层绘制都只画与当前裁剪区相交的部分
        const int txW = txRc.right - txRc.left;
        if (!m_textPlacedValid || m_textPlacedW != txW)
        {
            if (m_measuredLineW >= 0 && txW >= m_measuredLineW && txW <= m_measuredMaxTextW)
            {
                // 文字区宽度落在测量那次排版的最大行宽与可用宽度之间：重新排版的结果与测量时的完全相同
                // （贪心折行在这个宽度范围内的断行位置不变，字体也只差下划线、删除线这两个不影响字宽的
                // 标志），直接复用，省掉一次整段排版
                m_textPlaced = m_measuredPlaced;
            }
            else
            {
                const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(m_msg.text);
                m_textPlaced.clear();
                LayoutInlineContentForStyle(hdc, segs, txW, kFaceDisplayPx, st, &m_textPlaced);
            }
            m_textPlacedValid = true;
            m_textPlacedW = txW;
        }

        DrawMentionBlocks(hdc, m_selLayout, m_atsFlat, m_msg.fromMe, origin);
        DrawTextSelection(hdc, origin);
        DrawInlinePlaced(hdc, m_textPlaced, txRc, st);
        DrawMentionHighlightText(hdc, m_textPlaced, m_selLayout, m_atsFlat, m_msg.fromMe, txRc, st);
        // 第五层：超链接按矩形裁剪、用链接色 + 下划线重画那几个字。放在 @ 之后 ——
        // 两者理论上不会重叠（@ 片段是昵称、链接是 URL），真撞上时以链接为准，
        // 因为链接是可点的、必须让用户看出来。
        DrawLinkText(hdc, m_textPlaced, m_selLayout, m_linksFlat, txRc, st);
    }
    else if (m_msg.kind == kMsgEmoji)
    {
        RECT emRc = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.text, emRc, kEmojiPx, false, kBubbleText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    else if (m_msg.kind == kMsgImage && imageFailure != kChatImageNotFailed)
    {
        // 破图占位的说明文字（"图片发送失败" / "图片加载失败"）。放不下时排版结果里
        // m_showCaption 为 false，只留破图图标。字体参数与测量时一致（见 ImagePlaceholder.h）。
        if (imageFailLayout.m_showCaption)
        {
            DrawGdiText(hdc, ChatImageFailureCaption(imageFailure), imageFailLayout.m_captionRect,
                        kImagePlaceholderCaptionPx, false, ChatImageFailureCaptionColor(imageFailure),
                        DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
        }
    }
    else if (m_msg.kind == kMsgImage)
    {
        // 覆盖文字：上传中显示进度、下载中显示"下载中"；正常态回退到占位标签（真实图片
        // 占位标签一般为空，不画字）。失败态走上面那个分支，不经过这里。
        CString overlay;
        if (m_msg.imageState == kChatImageStateUploading)
        {
            overlay.Format(TR(L"上传中 %d%%"), m_msg.imageProgress);
        }
        else if (m_msg.imageState == kChatImageStateDownloading)
        {
            //密聊图片经服务端分块中转，收到首块之后就有进度（docs/密聊方案.md 4.10）
            overlay = (m_msg.isSecret && m_msg.imageProgress > 0) ? SecretImageReceivingText(m_msg.imageProgress)
                                                                  : CString(TR(L"下载中…"));
        }
        else
        {
            overlay = m_msg.imageCaption;
        }

        if (!overlay.IsEmpty())
        {
            RECT capRc = { bubbleX + 4, bubbleY + 4,
                           bubbleX + 4 + m_msg.imageW, bubbleY + 4 + m_msg.imageH };
            // 有遮罩（上传中 / 下载中）时用白字；正常占位文字用浅灰。
            COLORREF capColor = (m_msg.imageState != kChatImageStateReady)
                ? RGB(255, 255, 255) : RGB(0x9A, 0x97, 0x92);
            DrawGdiText(hdc, (LPCWSTR)overlay, capRc, 12, false, capColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
    }
    else if (m_msg.kind == kMsgFile)
    {
        // ---- 文件卡片的文字部分（类型字、文件名、状态行、操作链接），与聊天记录查看窗共用。
        //      操作链接的点击矩形与动作照旧记进 m_fileBtnRect / m_fileBtnAction，供按下与
        //      悬停时判断 ----
        const RECT card = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        FileCardLinks links;
        PaintFileCardText(hdc, g, card, m_msg, m_fileHoverIdx, links);
        m_fileBtnCount = links.m_count;
        for (int bi = 0; bi < links.m_count; ++bi)
        {
            m_fileBtnRect[bi]   = links.m_rect[bi];
            m_fileBtnAction[bi] = links.m_action[bi];
        }
    }

    else if (m_msg.kind == kMsgFolder)
    {
        const int st      = m_msg.folderState;
        const int iconX   = bubbleX + kFolderPadH;
        const int textX   = iconX + kFolderIconSize + kFolderTextGap;   // 文件夹名 / 摘要行左
        const int textR   = bubbleX + bubbleW - kFolderPadH;            // 气泡内文字右边界
        const int rowLeft = bubbleX + kFolderPadH;                      // 状态行 / 失败原因行 / 按钮行左

        // ---- 标题行：文件夹名 ----
        RECT nameRc = { textX, bubbleY + kFolderNameTop,
                        textR, bubbleY + kFolderNameTop + kFolderNameH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.folderName, nameRc, 14, false, kBubbleText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // ---- 副标题行：文件数 / 目录数 / 总大小 ----
        const CString summary = BuildFolderSummaryText(m_msg);
        RECT sumRc = { textX, bubbleY + kFolderSummaryTop,
                       textR, bubbleY + kFolderSummaryTop + kFolderSummaryH };
        DrawGdiText(hdc, (LPCWSTR)summary, sumRc, 11, false, RGB(0x8A, 0x8A, 0x8A),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // ---- 「P2P」徽章：数据经直连链路传输时画在摘要行文字之后（2026-10-04 起）----
        if (FolderBubbleShowsP2PBadge(st, balloonmain::ShouldShowP2PBadge(m_msg.folderLinkPath)))
        {
            const int summaryW = MeasureTextWidth(hdc, (LPCWSTR)summary, 11);
            chatpaint::DrawP2PBadgeInRow(hdc, textX + summaryW + chatpaint::kP2PBadgeGapPx, sumRc.top, sumRc.bottom,
                                         textR);
        }

        // ---- 状态行：眼下进行到哪一步（传输中还带右端的百分比）----
        // 有没有进度条一律问 FolderStateHasProgress，与上面 GDI+ 层画进度条、以及
        // FolderBubbleHeight 里留高度问的是同一个函数，三处不会走偏。
        const bool hasProgress   = FolderStateHasProgress(st);
        const int  statusTop     = bubbleY + kFolderHeaderH
                                 + (hasProgress ? kFolderProgressH : 0)
                                 + kFolderStatusTopGap;
        const int  statusBottom  = statusTop + kFolderStatusH;
        const FolderStatusLine statusLine = BuildFolderStatusLine(m_msg);

        //状态行从右往左分三段：百分比（有进度条时）、"第 i / 共 n 个"计数段、主文字。
        //先把右侧两段的位置定下来，主文字用剩下的宽度并以省略号收尾 —— 这样被截断的
        //只会是文件名，"传到第几个了"与百分比始终完整可见。
        int statusRight = textR;
        if (hasProgress)
        {
            statusRight -= kFolderPctW;
        }
        if (!statusLine.m_counter.IsEmpty())
        {
            const int counterW = MeasureTextWidth(hdc, (LPCWSTR)statusLine.m_counter, 11);
            RECT counterRc = { statusRight - counterW, statusTop, statusRight, statusBottom };
            DrawGdiText(hdc, (LPCWSTR)statusLine.m_counter, counterRc, 11, statusLine.m_bold,
                        statusLine.m_color,
                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            statusRight -= counterW + kFolderCounterGap;
        }
        RECT stRc = { rowLeft, statusTop, statusRight, statusBottom };
        DrawGdiText(hdc, (LPCWSTR)statusLine.m_text, stRc, 11, statusLine.m_bold,
                    statusLine.m_color,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (hasProgress)
        {
            int pct = m_msg.folderProgress;
            if (pct < 0)
                pct = 0;
            if (pct > 100)
                pct = 100;
            CString pctText;
            pctText.Format(_T("%d%%"), pct);
            RECT pctRc = { textR - kFolderPctW, statusTop, textR, statusBottom };
            DrawGdiText(hdc, (LPCWSTR)pctText, pctRc, 11, statusLine.m_bold,
                        statusLine.m_color,
                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        // ---- 失败原因行：只在"没有正常传完 + 宿主带回了错误码"时出现 ----
        // 文案一律取自 FolderErrorText：同一个错误码在收发两端映射出的是同一句话，
        // 两个人对着屏幕说"我这边写着什么"时才对得上。
        const bool hasErrorLine = FolderBubbleHasErrorLine(st, m_msg.folderErrCode);
        if (hasErrorLine)
        {
            const int errTop = statusBottom + kFolderErrorTopGap;
            RECT errRc = { rowLeft, errTop, textR, errTop + kFolderErrorH };
            DrawGdiText(hdc, FolderErrorText(m_msg.folderErrCode), errRc, 11, false,
                        RGB(0xE5, 0x39, 0x35),
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        // ---- 底部操作：与文件气泡同为文字超链接，按各自文字宽度从左到右排布，
        //      整行高度作命中矩形（便于点击），就地填 m_folderBtnRect / m_folderBtnAction /
        //      m_folderBtnEnabled。按钮数与动作全部取自 FolderButtonActions，本处不做
        //      任何状态判断 —— 这正是把那套映射抽成纯逻辑模块的目的。----
        int acts[kMaxChatFolderActions];
        m_folderBtnCount = FolderButtonActions(st, m_msg.fromMe, acts);
        if (m_folderBtnCount > 0)
        {
            const int rowTop    = statusBottom
                                + (hasErrorLine ? (kFolderErrorTopGap + kFolderErrorH) : 0)
                                + kFolderBtnTopGap;
            const int rowBottom = rowTop + kFolderBtnH;
            int linkX = rowLeft;
            for (int bi = 0; bi < m_folderBtnCount; ++bi)
            {
                const int act = acts[bi];
                CString   label;
                COLORREF  lblColor = RGB(0xFF, 0x7A, 0x45);   // 默认主题橙（可点链接）
                bool      enabled  = true;                     // 默认可用；只有「重试」会被置灰
                switch (act)
                {
                //接收：接收端同意接收，直接落到本机默认的文件夹接收目录，不再打断用户
                //选位置。主动作，用主题橙
                case kChatFolderAccept:
                    label = TR(L"接收");
                    break;

                //另存为：同样是同意接收，但先由宿主弹出「选择保存位置」。它与「接收」
                //只差落地目录从哪来，属于同一类动作的次要入口，故用灰色链接，把主题橙
                //留给「接收」
                case kChatFolderSaveAs:
                    label    = TR(L"另存为");
                    lblColor = RGB(0x8A, 0x8A, 0x8A);
                    break;

                //拒绝：次要动作，灰色链接，免得与「接收」抢注意力
                case kChatFolderReject:
                    label    = TR(L"拒绝");
                    lblColor = RGB(0x8A, 0x8A, 0x8A);
                    break;

                //取消：邀请态是撤回邀请、传输中是中止传输，两端都可能出现。同为次要动作
                case kChatFolderCancel:
                    label    = TR(L"取消");
                    lblColor = RGB(0x8A, 0x8A, 0x8A);
                    break;

                //打开文件夹：完成后两端都有。接收端开落地目录、发送端开源目录，
                //开哪个由宿主按 fromMe 决定，气泡这一层不区分
                case kChatFolderOpenFolder:
                    label = TR(L"打开文件夹");
                    break;

                //重新发送：会话已不可续传（保留期过 / 被取消），从零发起一次完整传输。
                //\x21BB = ↻ 循环箭头，与文件气泡的重试用同一枚字符
                case kChatFolderResend:
                    label = TR(L"\x21BB 重新发送");
                    break;

                //重试续传：仅失败态的发送端有。可用性问 FolderRetryEnabled ——
                //保留期已过时按钮仍画出来（让用户知道有这条路），但置灰且点不动
                case kChatFolderRetry:
                default:
                    label   = TR(L"\x21BB 重试");
                    enabled = FolderRetryEnabled(st, m_msg.fromMe, m_msg.folderWithinRetry);
                    lblColor = enabled ? RGB(0xE5, 0x39, 0x35) : RGB(0xB8, 0xB5, 0xB1);
                    break;
                }

                const int linkW = MeasureTextWidth(hdc, (LPCWSTR)label, kFolderLinkPx);
                RECT linkRc = { linkX, rowTop, linkX + linkW, rowBottom };
                // 悬停的链接：颜色加深（×4/5）+ 加下划线，作 hover 反馈。置灰的按钮
                // 不参与悬停（FolderLinkAtPoint 已把它排除），故这里恒为 false。
                const bool hover = (bi == m_folderHoverIdx);
                const COLORREF drawColor = hover
                    ? RGB(GetRValue(lblColor) * 4 / 5, GetGValue(lblColor) * 4 / 5,
                          GetBValue(lblColor) * 4 / 5)
                    : lblColor;
                //文字垂直居中于按钮行；命中矩形取满行高，整列可点
                DrawGdiText(hdc, (LPCWSTR)label, linkRc, kFolderLinkPx, false, drawColor,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP,
                            false /*italic*/, hover /*underline*/, false /*strike*/);
                m_folderBtnRect[bi]    = linkRc;
                m_folderBtnAction[bi]  = act;
                m_folderBtnEnabled[bi] = enabled;
                linkX += linkW + kFolderLinkGap;
            }
        }
    }
    else if (m_msg.kind == kMsgMerged)
    {
        const int textL = bubbleX + kMergedPadH;
        const int textR = bubbleX + bubbleW - kMergedPadH;

        // 标题：一行，过长以省略号收尾。
        int lineTop = bubbleY + kMergedTitleTop;
        RECT titleRc = { textL, lineTop, textR, lineTop + kMergedTitleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.mergedTitle, titleRc, kMergedTitlePx, true, kBubbleText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        // 预览行：「发送者：摘要」，每行一条，过长以省略号收尾。
        lineTop += kMergedTitleH + kMergedLinesGap;
        for (size_t li = 0; li < m_msg.mergedLines.size(); ++li)
        {
            RECT lineRc = { textL, lineTop, textR, lineTop + kMergedLineH };
            DrawGdiText(hdc, (LPCWSTR)m_msg.mergedLines[li], lineRc, kMergedLinePx, false,
                        kMergedLineColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            lineTop += kMergedLineH;
        }

        // 底部：「聊天记录（共 N 条）」；内容不可用（条数为 0）时只写「聊天记录」。
        CString footer;
        if (m_msg.mergedCount > 0)
        {
            footer.Format(TR(L"聊天记录（共 %d 条）"), m_msg.mergedCount);
        }
        else
        {
            footer = TR(L"聊天记录");
        }
        RECT footRc = { textL, bubbleY + bubbleH - kMergedFooterH, textR, bubbleY + bubbleH };
        DrawGdiText(hdc, (LPCWSTR)footer, footRc, kMergedFooterPx, false, kMergedLineColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else if (m_msg.kind == kMsgRelay)
    {
        const int textL = bubbleX + kRelayPadH;
        const int textR = bubbleX + bubbleW - kRelayPadH;
        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        // 标题：画在图标右侧，一行，过长以省略号收尾
        int lineTop = bubbleY + kRelayTitleTop;
        RECT titleRc = { textL + kRelayIconSize + kRelayIconGap, lineTop, textR, lineTop + kRelayTitleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.relayTitle, titleRc, kRelayTitlePx, true, kBubbleText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kRelayTitleH;

        // 说明：Measure 已按卡片内宽折好行、截断处带了省略号，这里逐行画
        if (!m_relayNoteLines.empty())
        {
            lineTop += kRelayNoteGap;
            for (size_t li = 0; li < m_relayNoteLines.size(); ++li)
            {
                RECT noteRc = { textL, lineTop, textR, lineTop + kRelayNoteLineH };
                DrawGdiText(hdc, (LPCWSTR)m_relayNoteLines[li], noteRc, kRelayNotePx, false,
                            kRelayGrayText, kOneLine);
                lineTop += kRelayNoteLineH;
            }
        }

        // 接龙：每行一条「序号. 名字　内容」，过长以省略号收尾；一条都没有时写一行灰字
        lineTop += kRelayEntriesGap;
        if (m_msg.relayLines.empty())
        {
            RECT emptyRc = { textL, lineTop, textR, lineTop + kRelayEntryH };
            DrawGdiText(hdc, TR(L"还没有人参与"), emptyRc, kRelayEntryPx, false, kRelayGrayText, kOneLine);
            lineTop += kRelayEntryH;
        }
        else
        {
            for (size_t li = 0; li < m_msg.relayLines.size(); ++li)
            {
                RECT entryRc = { textL, lineTop, textR, lineTop + kRelayEntryH };
                DrawGdiText(hdc, (LPCWSTR)m_msg.relayLines[li], entryRc, kRelayEntryPx, false,
                            kBubbleText, kOneLine | DT_END_ELLIPSIS);
                lineTop += kRelayEntryH;
            }
        }

        // 状态行：「共 N 人参与 · 截止 10-03 18:00」之类，ChatRelayCard 已拼好
        lineTop += kRelayStatusGap;
        RECT statusRc = { textL, lineTop, textR, lineTop + kRelayStatusH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.relayStatus, statusRc, kRelayStatusPx, false, kRelayGrayText,
                    kOneLine | DT_END_ELLIPSIS);

        // 按钮：分隔线下方一整行居中写按钮文字，悬停时颜色加深并加下划线
        LPCWSTR label = RelayButtonLabel(m_msg.relayButton);
        if (label != NULL)
        {
            RECT btnRc = { bubbleX, bubbleY + bubbleH - kRelayBtnH, bubbleX + bubbleW, bubbleY + bubbleH };
            DrawGdiText(hdc, label, btnRc, kRelayBtnPx, true,
                        m_relayBtnHover ? kRelayBtnHoverText : kRelayBtnText,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        false /*italic*/, m_relayBtnHover /*underline*/, false /*strike*/);
        }
    }

    if (m_msg.kind == kMsgFileShare)
    {
        const int textL = bubbleX + kSharePadH;
        const int textR = bubbleX + bubbleW - kSharePadH;
        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        // 标题前的类型图标：本机的系统图标，单个文件按显示路径（没有时按名称）的扩展名取
        int lineTop = bubbleY + kShareTitleTop;
        const int iconTop = lineTop + (kShareTitleH - kShareTypeIconSize) / 2;
        const RECT iconRc = { textL, iconTop, textL + kShareTypeIconSize, iconTop + kShareTypeIconSize };
        const CString& iconName = m_msg.sharePath.IsEmpty() ? m_msg.shareName : m_msg.sharePath;
        shellicon::DrawItemIcon(hdc, ShareCardIconItemKind(m_msg.shareKind), std::wstring((LPCWSTR)iconName), iconRc);

        // 名称：画在图标右侧，一行，过长以省略号收尾
        RECT titleRc = { textL + kShareTypeIconSize + kShareIconGap, lineTop, textR, lineTop + kShareTitleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.shareName, titleRc, kShareTitlePx, true, kBubbleText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kShareTitleH;

        // 显示路径：路径过长时省略中间部分
        if (!m_msg.sharePath.IsEmpty())
        {
            lineTop += kSharePathGap;
            RECT pathRc = { textL, lineTop, textR, lineTop + kSharePathH };
            DrawGdiText(hdc, (LPCWSTR)m_msg.sharePath, pathRc, kSharePathPx, false, kShareGrayText,
                        kOneLine | DT_PATH_ELLIPSIS);
            lineTop += kSharePathH;
        }

        // 「某某的共享 · 状态」
        lineTop += kShareStatusGap;
        CString statusLine = m_msg.shareOwnerLine;
        if (!m_msg.shareStatus.IsEmpty())
        {
            statusLine += L" · ";
            statusLine += m_msg.shareStatus;
        }
        RECT statusRc = { textL, lineTop, textR, lineTop + kShareStatusH };
        DrawGdiText(hdc, (LPCWSTR)statusLine, statusRc, kShareStatusPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);

        // 按钮：分隔线下方一整行居中写按钮文字；可点时悬停加深并加下划线，不可点时为灰
        LPCWSTR label = ShareCardButtonLabel(m_msg.shareButton);
        if (label != NULL)
        {
            RECT btnRc = { bubbleX, bubbleY + bubbleH - kShareBtnH, bubbleX + bubbleW, bubbleY + bubbleH };
            const bool hover = m_shareBtnHover && m_msg.shareButtonEnabled;
            const COLORREF color = !m_msg.shareButtonEnabled ? kShareBtnDisabledText
                                                              : (hover ? kShareBtnHoverText : kShareBtnText);
            DrawGdiText(hdc, label, btnRc, kShareBtnPx, true, color,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        false /*italic*/, hover /*underline*/, false /*strike*/);
        }
    }

    // 语音气泡：右侧的时长文字（与语音条控件共用 ChatVoicePaint 的画法）
    if (m_msg.kind == kMsgVoice)
    {
        const RECT voiceBar = { bubbleX, bubbleY, bubbleX + bubbleW, bubbleY + bubbleH };
        PaintVoiceBarText(hdc, voiceBar, m_msg);
    }

    if (m_msg.kind == kMsgScreenShare)
    {
        const int textL = bubbleX + kSharePadH;
        const int textR = bubbleX + bubbleW - kSharePadH;
        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        // 标题：画在图标右侧
        int lineTop = bubbleY + kShareTitleTop;
        RECT titleRc = { textL + kShareIconSize + kShareIconGap, lineTop, textR, lineTop + kShareTitleH };
        //第 15 期起写共享标题；没有标题的旧卡片照旧写「屏幕共享」
        const wchar_t* titleText =
            m_msg.screenShareTitle.IsEmpty() ? TR(kScreenShareCardTitle) : (LPCWSTR)m_msg.screenShareTitle;
        DrawGdiText(hdc, titleText, titleRc, kShareTitlePx, true, kBubbleText, kOneLine | DT_END_ELLIPSIS);
        lineTop += kShareTitleH;

        // 副标题：「某某发起的共享」，发起人名过长时省略
        lineTop += kSharePathGap;
        RECT subRc = { textL, lineTop, textR, lineTop + kSharePathH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.screenShareSubtitle, subRc, kSharePathPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kSharePathH;

        // 描述块（第 15 期）：灰字，Measure 已按卡片内宽折成至多两行，超出部分以省略号收尾
        if (!m_shareDescLines.empty())
        {
            lineTop += kShareDescGap;
            for (size_t li = 0; li < m_shareDescLines.size(); ++li)
            {
                RECT descRc = { textL, lineTop, textR, lineTop + kShareDescLineH };
                DrawGdiText(hdc, (LPCWSTR)m_shareDescLines[li], descRc, kShareDescPx, false, kShareGrayText,
                            kOneLine | DT_END_ELLIPSIS);
                lineTop += kShareDescLineH;
            }
        }

        // 说明块（第 14 期）：强制模式时写明效果，最多两行、主题橙色提醒，自动换行
        if (!m_msg.screenShareNotice.IsEmpty())
        {
            lineTop += kShareNoticeGap;
            RECT noticeRc = { textL, lineTop, textR, lineTop + kShareNoticeLines * kShareNoticeLineH };
            DrawGdiText(hdc, (LPCWSTR)m_msg.screenShareNotice, noticeRc, kShareNoticePx, false, kShareBtnText,
                        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
            lineTop += kShareNoticeLines * kShareNoticeLineH;
        }

        // 状态
        lineTop += kShareStatusGap;
        RECT statusRc = { textL, lineTop, textR, lineTop + kShareStatusH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.screenShareStatus, statusRc, kShareStatusPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);

        // 按钮：与文件共享卡片相同，可点时悬停加深并加下划线，共享已结束时为灰
        if (m_msg.screenShareHasButton)
        {
            RECT btnRc = { bubbleX, bubbleY + bubbleH - kShareBtnH, bubbleX + bubbleW, bubbleY + bubbleH };
            const bool hover = m_shareBtnHover && m_msg.screenShareButtonEnabled;
            const COLORREF color = !m_msg.screenShareButtonEnabled ? kShareBtnDisabledText
                                                                    : (hover ? kShareBtnHoverText : kShareBtnText);
            const bool infoCard = m_msg.screenShareIsAnnouncement || m_msg.screenShareIsMemo;
            const wchar_t* btnText = infoCard ? TR(kInfoCardButton) : TR(kScreenShareCardButton);
            if (m_msg.screenShareIsCloudRecord)
                btnText = TR(kCloudRecordCardButton);
            DrawGdiText(hdc, btnText, btnRc, kShareBtnPx, true, color,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        false /*italic*/, hover /*underline*/, false /*strike*/);
        }
    }

    if (m_msg.kind == kMsgGroupInvite)
    {
        const int textL = bubbleX + kSharePadH;
        const int textR = bubbleX + bubbleW - kSharePadH;
        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        // 标题：画在图标右侧，名字过长时以省略号收尾
        int lineTop = bubbleY + kShareTitleTop;
        RECT titleRc = { textL + kShareIconSize + kShareIconGap, lineTop, textR, lineTop + kShareTitleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.inviteTitle, titleRc, kShareTitlePx, true, kBubbleText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kShareTitleH;

        // 副标题：「群名 · N 名成员」，群名过长时省略
        lineTop += kSharePathGap;
        RECT subRc = { textL, lineTop, textR, lineTop + kSharePathH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.inviteSubtitle, subRc, kSharePathPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kSharePathH;

        // 状态
        lineTop += kShareStatusGap;
        RECT statusRc = { textL, lineTop, textR, lineTop + kShareStatusH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.inviteStatus, statusRc, kShareStatusPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);

        // 按钮：「加入群聊」，点过之后等服务端应答期间写「正在加入…」且为灰；可点时悬停加深并加下划线
        if (m_msg.inviteHasButton)
        {
            RECT btnRc = { bubbleX, bubbleY + bubbleH - kShareBtnH, bubbleX + bubbleW, bubbleY + bubbleH };
            const bool clickable = GroupInviteButtonClickable();
            const bool hover = m_shareBtnHover && clickable;
            const COLORREF color = !clickable ? kShareBtnDisabledText : (hover ? kShareBtnHoverText : kShareBtnText);
            const CString label = GroupInviteButtonText(m_msg.inviteJoining);
            DrawGdiText(hdc, (LPCWSTR)label, btnRc, kShareBtnPx, true, color,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        false /*italic*/, hover /*underline*/, false /*strike*/);
        }
    }

    if (m_msg.kind == kMsgSecretInvite)
    {
        const int textL = bubbleX + kSharePadH;
        const int textR = bubbleX + bubbleW - kSharePadH;
        const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        // 标题：画在挂锁右侧，名字过长时以省略号收尾
        int lineTop = bubbleY + kShareTitleTop;
        RECT titleRc = { textL + kShareIconSize + kShareIconGap, lineTop, textR, lineTop + kShareTitleH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.secretInviteTitle, titleRc, kShareTitlePx, true, kBubbleText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kShareTitleH;

        // 说明：「密聊消息不会被记录，退出后全部清除」，对方无法防截图时写明
        lineTop += kSharePathGap;
        RECT subRc = { textL, lineTop, textR, lineTop + kSharePathH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.secretInviteSubtitle, subRc, kSharePathPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);
        lineTop += kSharePathH;

        // 剩余秒数
        lineTop += kShareStatusGap;
        RECT statusRc = { textL, lineTop, textR, lineTop + kShareStatusH };
        DrawGdiText(hdc, (LPCWSTR)m_msg.secretInviteStatus, statusRc, kShareStatusPx, false, kShareGrayText,
                    kOneLine | DT_END_ELLIPSIS);

        // 按钮：一个时占满整行，两个时左右各占一半；悬停的那个加深并加下划线（多选模式下不给悬停效果）
        const int buttons = m_msg.secretInviteButtons;
        for (int bi = 0; bi < buttons; ++bi)
        {
            const int btnTop = bubbleY + bubbleH - kShareBtnH;
            const int btnL = (buttons > 1 && bi == 1) ? (bubbleX + bubbleW / 2) : bubbleX;
            const int btnR = (buttons > 1 && bi == 0) ? (bubbleX + bubbleW / 2) : (bubbleX + bubbleW);
            RECT btnRc = { btnL, btnTop, btnR, bubbleY + bubbleH };
            const bool hover = (m_secretBtnHover == bi);
            const COLORREF color = hover ? kShareBtnHoverText : kShareBtnText;
            const CString label = SecretInviteButtonText(buttons, bi);
            DrawGdiText(hdc, (LPCWSTR)label, btnRc, kShareBtnPx, true, color,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        false /*italic*/, hover /*underline*/, false /*strike*/);
        }
    }

    // ---- 已读回执（单聊里自己发出的消息）----
    //
    // 显示什么由 DecideReadReceipt 统一判定：群聊、对方的消息、文件夹行、尚未拿到服务器
    // msgid 的消息都不画；其余按对方的已读位点画「✓ 已读」（灰）或「未读」（主题橙）。
    // 2026-09-30 之前这里对所有自己发的消息无条件画「已读」，给离线的人发消息也显示已读。
    const ChatReceiptKind receipt = DecideReadReceipt(m_msg, m_readReceiptEnabled);
    if (receipt == kReceiptRead)
    {
        const int rcY = bubbleY + bubbleH + kReceiptTopGap;
        const int txtW = MeasureTextWidth(hdc, TR(L"已读"), kReceiptTextPx);
        const int receiptW = kReceiptCheckSize + kReceiptCheckGap + txtW;
        const int receiptX = bubbleX + bubbleW - receiptW;
        DrawChatIcon(g, kChatIconCheck, (Gdiplus::REAL)receiptX,
                     (Gdiplus::REAL)(rcY + kReceiptCheckTopOffset), (Gdiplus::REAL)kReceiptCheckSize,
                     Gdiplus::Color(255, GetRValue(kReceiptText),
                         GetGValue(kReceiptText), GetBValue(kReceiptText)), kReceiptCheckStroke);
        RECT rcRc = { receiptX + kReceiptCheckSize + kReceiptCheckGap, rcY,
                      bubbleX + bubbleW, rcY + kReceiptHeight };
        DrawGdiText(hdc, TR(L"已读"), rcRc, kReceiptTextPx, false, kReceiptText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    else if (receipt == kReceiptUnread)
    {
        // 「未读」不带勾，用主题橙与灰色的「已读」区分开 —— 发送方一眼能看出对方还没看。
        const int rcY = bubbleY + bubbleH + kReceiptTopGap;
        const int txtW = MeasureTextWidth(hdc, TR(L"未读"), kReceiptTextPx);
        RECT rcRc = { bubbleX + bubbleW - txtW, rcY,
                      bubbleX + bubbleW, rcY + kReceiptHeight };
        DrawGdiText(hdc, TR(L"未读"), rcRc, kReceiptTextPx, false, kReceiptUnreadText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 发送失败（自己发出、被服务端拒绝的消息）----
    // 气泡左侧垂直居中画一个红色圆形感叹号，气泡下方右对齐一行灰色小字写原因。失败的行没有 msgid，
    // 不会有回执，也不会有图钉，两者不会与这里画的东西重叠。
    if (m_msg.fromMe && m_msg.sendFailed)
    {
        const int markX = bubbleX - kFailMarkGap - kFailMarkSize;
        const int markY = bubbleY + (bubbleH - kFailMarkSize) / 2;
        Gdiplus::SolidBrush markBrush(Gdiplus::Color(255, GetRValue(kImagePlaceholderCaptionRed),
                                                     GetGValue(kImagePlaceholderCaptionRed),
                                                     GetBValue(kImagePlaceholderCaptionRed)));
        g.FillEllipse(&markBrush, (Gdiplus::REAL)markX, (Gdiplus::REAL)markY,
                      (Gdiplus::REAL)kFailMarkSize, (Gdiplus::REAL)kFailMarkSize);
        RECT markRc = { markX, markY, markX + kFailMarkSize, markY + kFailMarkSize };
        //语音：记下感叹号的矩形，点它重发（2026-10-04 用户确认）
        if (m_msg.kind == kMsgVoice)
        {
            m_voiceFailRect = markRc;
        }
        DrawGdiText(hdc, L"!", markRc, kFailMarkTextPx, true, kFailMarkText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

        if (!m_msg.failReason.IsEmpty())
        {
            const int rcY = bubbleY + bubbleH + kReceiptTopGap;
            //原因可能比气泡宽：右边与气泡对齐，向左可以一直延伸到本行左边
            RECT reasonRc = { x, rcY, bubbleX + bubbleW, rcY + kFailReasonHeight };
            DrawGdiText(hdc, (LPCWSTR)m_msg.failReason, reasonRc, kReceiptTextPx, false, kReceiptText,
                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    // ---- Pin 标记（气泡外侧上角的一枚小图钉）----
    // 画在气泡<u>外侧</u>而不是气泡里：不占内容宽度、不改行高，故 SetPinned 只需重绘
    // 不必重排。左右按发送方向取反 —— 自己的气泡靠右、图钉放它左边，对方的气泡靠左、
    // 图钉放它右边，两种情形下图钉都落在中间那片空白里，不会压到头像。
    if (m_msg.pinned && IsRealMessage())
    {
        const int pinX = isMe ? (bubbleX - kPinIconGap - kPinIconSize)
                              : (bubbleX + bubbleW + kPinIconGap);
        DrawChatIcon(g, kChatIconPin, (Gdiplus::REAL)pinX, (Gdiplus::REAL)contentTop,
                     (Gdiplus::REAL)kPinIconSize, kPinIconColor, 1.4f);

        // 记下可点击区域（图标四周各外扩 kPinHitPad），换算成相对本行左上角的坐标。
        // 走渲染缓存时 m_rcItem 已被临时移到原点，这样算出的相对坐标同样正确。
        m_pinRectRel.left   = pinX - kPinHitPad - x;
        m_pinRectRel.top    = contentTop - kPinHitPad - y;
        m_pinRectRel.right  = pinX + kPinIconSize + kPinHitPad - x;
        m_pinRectRel.bottom = contentTop + kPinIconSize + kPinHitPad - y;
    }

    // ---- 多选勾选圈（行首勾选列内，垂直居中）。密聊内容不能多选，不画勾选圈 ----
    if (multi && IsRealMessage() && !IsSecretRow())
    {
        const Gdiplus::REAL chkX = (Gdiplus::REAL)(x + (kCheckColW - kCheckSize) / 2);
        const Gdiplus::REAL chkY = (Gdiplus::REAL)(y + (h - kCheckSize) / 2);
        if (m_selected)
        {
            Gdiplus::SolidBrush fill(kAccent);
            g.FillEllipse(&fill, chkX, chkY,
                          (Gdiplus::REAL)kCheckSize, (Gdiplus::REAL)kCheckSize);
            DrawChatIcon(g, kChatIconCheck, chkX + 3.0f, chkY + 3.0f,
                         (Gdiplus::REAL)(kCheckSize - 6),
                         Gdiplus::Color(255, 255, 255, 255), 2.0f);
        }
        else
        {
            Gdiplus::Pen ring(kCheckRing, 1.6f);
            g.DrawEllipse(&ring, chkX + 0.8f, chkY + 0.8f,
                          (Gdiplus::REAL)kCheckSize - 1.6f,
                          (Gdiplus::REAL)kCheckSize - 1.6f);
        }
    }
}

} // namespace balloonchatwnd
