/**
 *  聊天卡片纯逻辑的实现，ChatCardModel.cpp。对应关系见 ChatCardModel.h 与 docs/聊天卡片美化方案.md 第 3、5 节。
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"
#include "ChatCardModel.h"

#include "ChatCardStyle.h"
#include "ChatFileCard.h"                                  // FileCardButtons：文件卡片的按钮
#include "ChatFolderStateModel.h"                         // 文件夹传输状态与按钮动作
#include "ChatFileShareCard.h"                             // ShareCardButtonLabel：文件共享卡片的按钮文字
#include "ChatGroupInviteCard.h"                           // GroupInviteButtonText：入群邀请卡片的按钮文字
#include "ChatSecretMode.h"                                // SecretInviteButtonText：密聊邀请卡片的按钮文字
#include "ChatIcons.h"
#include "FileBubbleFormat.h"                                // FormatFileSize：文件夹进度的「已传 / 总大小」
#include "ChatRelayCard.h"                                 // RelayCardButton：接龙卡片底部按钮的取值
#include "../../net/Protocol.h"                            // share_state_invalid
#include "../../Common/I18n/I18n.h"                        // TR / TR_NOOP：界面文字的多语言查找
#include "../../Feature/GroupCall/GroupCallStatusCache.h"  // kGroupCallRoomEnded
#include "../../Feature/ScreenShare/ScreenShareRoomState.h" // kScreenShareRoomEnded

namespace balloonchatwnd {

namespace {

// 群通话、一对一通话卡片的媒体类型取值：视频（其余按语音处理）
const int kCardMediaVideo = 2;

// 文件共享的共享类型取值（share_kind）：磁盘、单个文件；其余（文件夹，以及内容不可用时）画文件共享图标
const int kCardShareKindDisk = 1;
const int kCardShareKindFile = 3;

/**
 *  一种卡片的类型色。图标块底色是方案 3.1 节的类型色按一定不透明度（11%～14%，取自效果图）叠在白底上的结果，
 *  预先算成不透明色：每个分量为 255 + (类型色分量 - 255) × 不透明度，四舍五入。例如屏幕共享 #4C6EF5 按 12% 叠在白底上
 *  得 #EAEEFE。图标颜色多数就是类型色；琥珀、青、橙、灰蓝四种在浅底上对比不够，图标改用同色系深一档的颜色（与效果图一致）。
 */
struct CardPalette
{
    COLORREF m_tile;    // 图标块底色
    COLORREF m_glyph;   // 图标颜色
};

const CardPalette kPaletteScreenShare = { RGB(0xEA, 0xEE, 0xFE), RGB(0x4C, 0x6E, 0xF5) };   // #4C6EF5，12%
const CardPalette kPaletteCall = { RGB(0xE4, 0xF3, 0xEB), RGB(0x1F, 0x9D, 0x5A) };          // #1F9D5A，12%
const CardPalette kPaletteAnnouncement = { RGB(0xFC, 0xEB, 0xEB), RGB(0xE5, 0x48, 0x4D) };  // #E5484D，11%
const CardPalette kPaletteAmber = { RGB(0xFC, 0xF2, 0xE3), RGB(0xD9, 0x8E, 0x1F) };         // 备忘、文件夹：#E8A33A，14%
const CardPalette kPaletteCloudRecord = { RGB(0xF1, 0xEB, 0xFE), RGB(0x8B, 0x5C, 0xF6) };   // #8B5CF6，12%
const CardPalette kPaletteFileShare = { RGB(0xE2, 0xF4, 0xF6), RGB(0x0E, 0x97, 0xA8) };     // #0EA5B7，12%
const CardPalette kPaletteRelay = { RGB(0xFF, 0xEE, 0xE7), RGB(0xF2, 0x6A, 0x33) };         // 品牌橙 #FF7A45，13%
const CardPalette kPaletteGroupInvite = { RGB(0xE6, 0xF0, 0xFD), RGB(0x2F, 0x80, 0xED) };   // #2F80ED，12%
const CardPalette kPaletteSlate = { RGB(0xEA, 0xEB, 0xEC), RGB(0x3C, 0x46, 0x54) };         // 密聊、Markdown：#3C4654，11%
const CardPalette kPaletteMerged = { RGB(0xEC, 0xEE, 0xF1), RGB(0x5E, 0x6B, 0x80) };        // 聊天记录：#6B7A90，13%

// 底部类型标签的原文（TR_NOOP 只做标记，取用时再经 TR 翻译）
const wchar_t* const kLabelFile = TR_NOOP(L"文件");
const wchar_t* const kLabelFolder = TR_NOOP(L"文件夹");
const wchar_t* const kLabelMerged = TR_NOOP(L"聊天记录");
const wchar_t* const kLabelRelay = TR_NOOP(L"接龙");
const wchar_t* const kLabelFileShare = TR_NOOP(L"文件共享");
const wchar_t* const kLabelScreenShare = TR_NOOP(L"屏幕共享");
const wchar_t* const kLabelVoiceCall = TR_NOOP(L"语音通话");
const wchar_t* const kLabelVideoCall = TR_NOOP(L"视频通话");
const wchar_t* const kLabelGroupCall = TR_NOOP(L"群通话");
const wchar_t* const kLabelAnnouncement = TR_NOOP(L"群公告");
const wchar_t* const kLabelMemo = TR_NOOP(L"群备忘");
const wchar_t* const kLabelCloudRecord = TR_NOOP(L"云端录像");
const wchar_t* const kLabelGroupInvite = TR_NOOP(L"群聊邀请");
const wchar_t* const kLabelSecretInvite = TR_NOOP(L"密聊");
const wchar_t* const kLabelMarkdown = TR_NOOP(L"Markdown 文档");
const wchar_t* const kLabelCode = TR_NOOP(L"代码片段");

// 密聊邀请卡片的按钮个数取值（ChatMessage::secretInviteButtons）与各按钮的编号（SecretInviteButtonAction 的 index）
const int kSecretInviteOneButton = 1;    // 发起方：只有「取消」
const int kSecretInviteTwoButtons = 2;   // 被邀请方：「同意」与「忽略」
const int kSecretAcceptIndex = 0;        // 被邀请方的「同意」
const int kSecretIgnoreIndex = 1;        // 被邀请方的「忽略」
const int kSecretCancelIndex = 0;        // 发起方的「取消」

// 按钮文字的原文
const wchar_t* const kButtonJoin = TR_NOOP(L"加入");   // 屏幕共享、群通话
const wchar_t* const kButtonView = TR_NOOP(L"查看");   // 群公告、群备忘
const wchar_t* const kButtonPlay = TR_NOOP(L"播放");   // 云端录像
const wchar_t* const kButtonRelayJoin = TR_NOOP(L"参与接龙");     // 接龙：本人还没参与
const wchar_t* const kButtonRelayModify = TR_NOOP(L"修改我的");   // 接龙：本人已参与

// 屏幕共享、群通话是否已结束：只有这两种卡片有「可以加入」的状态，结束后图标块画成灰色（方案 3.1 节）
bool IsEndedCard(ChatCardType type, const ChatMessage& msg)
{
    if (type == kCardTypeScreenShare)
        return msg.screenShareState == kScreenShareRoomEnded;
    //群通话：状态缓存判定结束；或卡片已被服务端改写为结束，此时 ApplyGroupCallCard 不给按钮
    if (type == kCardTypeGroupCall)
        return msg.screenShareState == balloongroupcall::kGroupCallRoomEnded || !msg.screenShareHasButton;
    return false;
}

// 按卡片种类与消息取图标；文件共享按共享类型、通话按媒体类型、云端录像按是否只有声音区分
int GlyphOf(ChatCardType type, const ChatMessage& msg)
{
    switch (type)
    {
    //文件：底部标签用文件图标（标题前画的是文档图标，不走这里）
    case kCardTypeFile:
        return kChatIconFile;
    //文件夹
    case kCardTypeFolder:
        return kChatIconFolder;
    //合并转发的聊天记录
    case kCardTypeMerged:
        return kChatIconChatRecord;
    //群接龙
    case kCardTypeRelay:
        return kChatIconRelay;
    //文件共享：磁盘、单个文件各有图标，文件夹与内容不可用时画文件共享图标
    case kCardTypeFileShare:
        if (msg.shareKind == kCardShareKindDisk)
            return kChatIconDisk;
        if (msg.shareKind == kCardShareKindFile)
            return kChatIconFile;
        return kChatIconShare;
    //屏幕共享
    case kCardTypeScreenShare:
        return kChatIconScreenShare;
    //一对一通话与群通话：视频画摄像机，其余画电话听筒的轮廓线（实心听筒在线条图标中间显得偏重）
    case kCardTypeCall:
    case kCardTypeGroupCall:
        return (msg.groupCallMedia == kCardMediaVideo) ? kChatIconVideo : kChatIconPhoneLine;
    //群公告
    case kCardTypeAnnouncement:
        return kChatIconAnnounce;
    //群备忘
    case kCardTypeMemo:
        return kChatIconMemo;
    //云端录像：只有声音的录音画话筒
    case kCardTypeCloudRecord:
        return msg.cloudRecordAudioOnly ? kChatIconMic : kChatIconCloudRecord;
    //入群邀请
    case kCardTypeGroupInvite:
        return kChatIconGroupInvite;
    //密聊邀请
    case kCardTypeSecretInvite:
        return kChatIconLock;
    //Markdown 文档
    case kCardTypeMarkdown:
        return kChatIconMarkdown;
    //代码片段：底部标签用代码图标（头部没有图标块）
    case kCardTypeCode:
        return kChatIconCode;
    //不是卡片
    default:
        return kChatIconFile;
    }
}

// 按卡片种类取类型色；文件与代码片段没有图标块，取不到时返回 false
bool PaletteOf(ChatCardType type, CardPalette& out)
{
    switch (type)
    {
    //屏幕共享
    case kCardTypeScreenShare:
        out = kPaletteScreenShare;
        return true;
    //一对一通话与群通话
    case kCardTypeCall:
    case kCardTypeGroupCall:
        out = kPaletteCall;
        return true;
    //群公告
    case kCardTypeAnnouncement:
        out = kPaletteAnnouncement;
        return true;
    //群备忘与文件夹同用琥珀色
    case kCardTypeMemo:
    case kCardTypeFolder:
        out = kPaletteAmber;
        return true;
    //云端录像
    case kCardTypeCloudRecord:
        out = kPaletteCloudRecord;
        return true;
    //文件共享
    case kCardTypeFileShare:
        out = kPaletteFileShare;
        return true;
    //接龙用品牌橙
    case kCardTypeRelay:
        out = kPaletteRelay;
        return true;
    //入群邀请
    case kCardTypeGroupInvite:
        out = kPaletteGroupInvite;
        return true;
    //密聊与 Markdown 同用石板灰
    case kCardTypeSecretInvite:
    case kCardTypeMarkdown:
        out = kPaletteSlate;
        return true;
    //合并转发的聊天记录
    case kCardTypeMerged:
        out = kPaletteMerged;
        return true;
    //文件、代码片段没有图标块；不是卡片
    default:
        return false;
    }
}

// 文件夹卡片底部按钮的文字（原样取自 2026-10-14 之前 ChatBubble 里文件夹按钮的写法）
CString FolderButtonLabel(int act)
{
    switch (act)
    {
    //接收：接收端同意接收，直接落到本机默认的文件夹接收目录
    case kChatFolderAccept:
        return CString(TR(L"接收"));
    //另存为：同样是同意接收，但先由宿主弹出「选择保存位置」
    case kChatFolderSaveAs:
        return CString(TR(L"另存为"));
    //拒绝
    case kChatFolderReject:
        return CString(TR(L"拒绝"));
    //取消：邀请态是撤回邀请、传输中是中止传输
    case kChatFolderCancel:
        return CString(TR(L"取消"));
    //打开文件夹：完成后两端都有
    case kChatFolderOpenFolder:
        return CString(TR(L"打开文件夹"));
    //重新发送：会话已不可续传，从零发起一次完整传输（\x21BB 为循环箭头）
    case kChatFolderResend:
        return CString(TR(L"\x21BB 重新发送"));
    //重试续传：仅失败态的发送端有
    default:
        return CString(TR(L"\x21BB 重试"));
    }
}

// 文件夹卡片按钮的级别：接收为主要，打开文件夹、重新发送、重试为查看，其余为次要
int FolderActionLevel(int act)
{
    switch (act)
    {
    //接收：改变状态的操作
    case kChatFolderAccept:
        return kCardButtonPrimary;
    //打开文件夹、重新发送、重试：查看类的操作
    case kChatFolderOpenFolder:
    case kChatFolderResend:
    case kChatFolderRetry:
        return kCardButtonView;
    //另存为、拒绝、取消：次要操作
    default:
        return kCardButtonSecondary;
    }
}

// 按钮的重要程度，ChatCardArrangeButtons 据此排序：次要、查看、主要
int ButtonRank(int level)
{
    if (level == kCardButtonPrimary)
        return 2;
    if (level == kCardButtonView)
        return 1;
    return 0;
}

// 本状态下是否要在信息行写失败原因：只对没有正常传完的几种终态、且宿主带回了错误码时给（原样取自 ChatBubble 的
// FolderBubbleHasErrorLine）。发送端在应答前主动撤回邀请不算失败
bool FolderHasErrorText(int state, int errcode)
{
    if (errcode == 0)
        return false;
    return state == kFolderFailed || state == kFolderExpired || state == kFolderUserCanceled || state == kFolderRejected
        || state == kFolderPeerOffline || state == kFolderTimeout;
}

// 文件夹进度的满值
const int kFolderFullPercent = 100;

} // namespace

ChatCardType ChatCardTypeOf(const ChatMessage& msg)
{
    switch (msg.kind)
    {
    //文件
    case kMsgFile:
        return kCardTypeFile;
    //文件夹整体传输
    case kMsgFolder:
        return kCardTypeFolder;
    //合并转发的聊天记录
    case kMsgMerged:
        return kCardTypeMerged;
    //群接龙
    case kMsgRelay:
        return kCardTypeRelay;
    //文件共享
    case kMsgFileShare:
        return kCardTypeFileShare;
    //屏幕共享一族：按标志区分，都不是时为屏幕共享本身
    case kMsgScreenShare:
        if (msg.screenShareIsCallCard)
            return kCardTypeCall;
        if (msg.screenShareIsGroupCall)
            return kCardTypeGroupCall;
        if (msg.screenShareIsAnnouncement)
            return kCardTypeAnnouncement;
        if (msg.screenShareIsMemo)
            return kCardTypeMemo;
        if (msg.screenShareIsCloudRecord)
            return kCardTypeCloudRecord;
        return kCardTypeScreenShare;
    //入群邀请
    case kMsgGroupInvite:
        return kCardTypeGroupInvite;
    //密聊邀请
    case kMsgSecretInvite:
        return kCardTypeSecretInvite;
    //文字消息里的代码片段与 Markdown 文档；两个标志不会同时为真，按代码片段优先与气泡的判断顺序一致
    case kMsgText:
        if (msg.isCode)
            return kCardTypeCode;
        if (msg.isMarkdown)
            return kCardTypeMarkdown;
        return kCardTypeNone;
    //时间分隔行、大表情、图片、语音等
    default:
        return kCardTypeNone;
    }
}

ChatCardTypeStyle ChatCardTypeStyleOf(const ChatMessage& msg)
{
    ChatCardTypeStyle style;
    style.m_type = ChatCardTypeOf(msg);
    style.m_hasTile = false;
    style.m_ended = false;
    style.m_tileFill = cardstyle::kTileEndedFill;
    style.m_glyphColor = cardstyle::kTileEndedGlyph;
    style.m_labelGlyphColor = cardstyle::kTileEndedGlyph;
    style.m_glyph = GlyphOf(style.m_type, msg);
    if (style.m_type == kCardTypeNone)
        return style;

    CardPalette palette;
    style.m_hasTile = PaletteOf(style.m_type, palette);
    //文件与代码片段没有图标块，底部标签的图标保持灰色
    if (!style.m_hasTile)
        return style;
    style.m_ended = IsEndedCard(style.m_type, msg);
    if (!style.m_ended)
    {
        style.m_tileFill = palette.m_tile;
        style.m_glyphColor = palette.m_glyph;
        style.m_labelGlyphColor = palette.m_glyph;
    }
    return style;
}

CString ChatCardTypeLabel(const ChatMessage& msg)
{
    const wchar_t* label = NULL;
    switch (ChatCardTypeOf(msg))
    {
    //文件
    case kCardTypeFile:
        label = kLabelFile;
        break;
    //文件夹
    case kCardTypeFolder:
        label = kLabelFolder;
        break;
    //合并转发的聊天记录
    case kCardTypeMerged:
        label = kLabelMerged;
        break;
    //群接龙
    case kCardTypeRelay:
        label = kLabelRelay;
        break;
    //文件共享
    case kCardTypeFileShare:
        label = kLabelFileShare;
        break;
    //屏幕共享
    case kCardTypeScreenShare:
        label = kLabelScreenShare;
        break;
    //一对一通话：按媒体类型写语音或视频
    case kCardTypeCall:
        label = (msg.groupCallMedia == kCardMediaVideo) ? kLabelVideoCall : kLabelVoiceCall;
        break;
    //群通话
    case kCardTypeGroupCall:
        label = kLabelGroupCall;
        break;
    //群公告
    case kCardTypeAnnouncement:
        label = kLabelAnnouncement;
        break;
    //群备忘
    case kCardTypeMemo:
        label = kLabelMemo;
        break;
    //云端录像
    case kCardTypeCloudRecord:
        label = kLabelCloudRecord;
        break;
    //入群邀请
    case kCardTypeGroupInvite:
        label = kLabelGroupInvite;
        break;
    //密聊邀请
    case kCardTypeSecretInvite:
        label = kLabelSecretInvite;
        break;
    //Markdown 文档
    case kCardTypeMarkdown:
        label = kLabelMarkdown;
        break;
    //代码片段
    case kCardTypeCode:
        label = kLabelCode;
        break;
    //不是卡片
    default:
        return CString();
    }
    return CString(TR(label));
}

void SetCardChip(ChatMessage& msg, const CString& chipText, int tone, const CString& metaText)
{
    msg.cardChipText = chipText;
    msg.cardChipTone = chipText.IsEmpty() ? kCardChipNone : tone;
    msg.cardMetaText = metaText;
}

bool ChatCardFooterVisible(bool titleIsTypeName, size_t buttonCount)
{
    return !(titleIsTypeName && buttonCount == 0);
}

bool ChatCardTitleIsTypeName(const ChatMessage& msg)
{
    const ChatCardType type = ChatCardTypeOf(msg);
    //一对一通话、群通话的标题一律是「语音通话」「群视频通话」这类类型名
    if (type == kCardTypeCall || type == kCardTypeGroupCall)
        return true;
    //屏幕共享一族的其余卡片：没有标题时标题写的就是类型名
    if (msg.kind == kMsgScreenShare)
        return msg.screenShareTitle == ChatCardTypeLabel(msg);
    return false;
}

std::vector<ChatCardButton> ChatCardButtonsOf(const ChatMessage& msg)
{
    std::vector<ChatCardButton> buttons;
    switch (ChatCardTypeOf(msg))
    {
    //屏幕共享、群通话：「加入」只有可点时才出现。按钮不可点只有一种情况 —— 共享或通话已结束，属于永久不可用
    case kCardTypeScreenShare:
    case kCardTypeGroupCall:
        if (msg.screenShareHasButton && msg.screenShareButtonEnabled)
            buttons.push_back(ChatCardButton(CString(TR(kButtonJoin)), kCardButtonPrimary, true, 0));
        break;
    //群公告：按钮不可点只有一种情况 —— 公告已删除，属于永久不可用
    case kCardTypeAnnouncement:
        if (msg.screenShareHasButton && msg.screenShareButtonEnabled)
            buttons.push_back(ChatCardButton(CString(TR(kButtonView)), kCardButtonView, true, 0));
        break;
    //群备忘：不可点时置灰。删除与被移出提醒对象在客户端分不出来，按可能恢复处理（方案第 7 节）
    case kCardTypeMemo:
        if (msg.screenShareHasButton)
            buttons.push_back(ChatCardButton(CString(TR(kButtonView)), kCardButtonView, msg.screenShareButtonEnabled, 0));
        break;
    //云端录像：内容可用时总有「播放」
    case kCardTypeCloudRecord:
        if (msg.screenShareHasButton)
            buttons.push_back(ChatCardButton(CString(TR(kButtonPlay)), kCardButtonView, msg.screenShareButtonEnabled, 0));
        break;
    //文件共享：已失效、内容不可用属于永久不可用，不出现；模块没有加载、本企业关闭了功能属于暂时不可用，置灰
    case kCardTypeFileShare:
        {
            const LPCWSTR label = ShareCardButtonLabel(msg.shareButton);
            if (label != NULL && msg.shareId > 0 && msg.shareState != share_state_invalid)
                buttons.push_back(ChatCardButton(CString(label), kCardButtonView, msg.shareButtonEnabled, 0));
        }
        break;
    //入群邀请：被邀请人待加入时「加入群聊」；点过之后等服务端应答期间写「正在加入…」并置灰
    case kCardTypeGroupInvite:
        if (msg.inviteHasButton && msg.inviteId > 0)
            buttons.push_back(ChatCardButton(GroupInviteButtonText(msg.inviteJoining), kCardButtonPrimary, !msg.inviteJoining, 0));
        break;
    //密聊邀请：被邀请方左「忽略」右「同意」，发起方只有「取消」。m_action 取原来的按钮编号（SecretInviteButtonAction 的 index）
    case kCardTypeSecretInvite:
        if (msg.secretInviteButtons == kSecretInviteTwoButtons)
        {
            buttons.push_back(ChatCardButton(SecretInviteButtonText(msg.secretInviteButtons, kSecretIgnoreIndex),
                                             kCardButtonSecondary, true, kSecretIgnoreIndex));
            buttons.push_back(ChatCardButton(SecretInviteButtonText(msg.secretInviteButtons, kSecretAcceptIndex),
                                             kCardButtonPrimary, true, kSecretAcceptIndex));
        }
        else if (msg.secretInviteButtons == kSecretInviteOneButton)
        {
            buttons.push_back(ChatCardButton(SecretInviteButtonText(msg.secretInviteButtons, kSecretCancelIndex),
                                             kCardButtonSecondary, true, kSecretCancelIndex));
        }
        break;
    //接龙：没参与时「参与接龙」，参与过时「修改我的」；已结束、已截止、被禁言、名额已满且本人未参与时没有按钮
    //（ChatRelayCard 的 DecideRelayButton 已经判断好，都属于永久不可用或要等推送刷新）
    case kCardTypeRelay:
        if (msg.relayButton == kRelayButtonJoin)
            buttons.push_back(ChatCardButton(CString(TR(kButtonRelayJoin)), kCardButtonPrimary, true, 0));
        else if (msg.relayButton == kRelayButtonModify)
            buttons.push_back(ChatCardButton(CString(TR(kButtonRelayModify)), kCardButtonSecondary, true, 0));
        break;
    //文件：按文件态给出（接收、打开、取消、重试……），排列与级别见 ChatFileCard 的 FileCardButtons
    case kCardTypeFile:
        buttons = FileCardButtons(msg);
        break;
    //文件夹：按传输状态给出；保留期已过的「重试」属于永久不可用，不出现（方案 5.1，原来是置灰）
    case kCardTypeFolder:
        {
            int acts[kMaxChatFolderActions];
            const int count = FolderButtonActions(msg.folderState, msg.fromMe, acts);
            for (int i = 0; i < count; ++i)
            {
                if (acts[i] == kChatFolderRetry && !FolderRetryEnabled(msg.folderState, msg.fromMe, msg.folderWithinRetry))
                    continue;
                buttons.push_back(ChatCardButton(FolderButtonLabel(acts[i]), FolderActionLevel(acts[i]), true, acts[i]));
            }
            ChatCardArrangeButtons(buttons);
        }
        break;
    //一对一通话、合并转发没有按钮；其余卡片随后续步骤补上
    default:
        break;
    }
    return buttons;
}

void ChatCardArrangeButtons(std::vector<ChatCardButton>& buttons)
{
    std::vector<ChatCardButton> arranged;
    for (int rank = 0; rank <= ButtonRank(kCardButtonPrimary); ++rank)
    {
        for (size_t k = buttons.size(); k > 0; --k)
        {
            if (ButtonRank(buttons[k - 1].m_level) == rank)
                arranged.push_back(buttons[k - 1]);
        }
    }
    buttons.swap(arranged);
}

void FolderCardStatus(const ChatMessage& msg, CString& chipText, int& tone, CString& metaText)
{
    chipText.Empty();
    metaText.Empty();
    tone = kCardChipEnded;
    switch (msg.folderState)
    {
    //邀请已发出、等待应答：接收端要当场决定收不收
    case kFolderInviting:
        tone = kCardChipPending;
        if (msg.fromMe)
        {
            chipText = TR(L"等待对方接收…");
        }
        else
        {
            chipText = TR(L"待接收");
            metaText = TR(L"对方想给你发送一个文件夹");
        }
        break;
    //接收端正在逐级建目录
    case kFolderPreparing:
        chipText = msg.fromMe ? TR(L"对方正在创建目录…") : TR(L"正在创建目录…");
        tone = kCardChipPending;
        break;
    //传输中：不画标签，进度写在进度条下面
    case kFolderTransferring:
        tone = kCardChipNone;
        break;
    //全部传完并通过校验
    case kFolderCompleted:
        chipText = msg.fromMe ? TR(L"全部发送完成") : TR(L"全部接收完成");
        tone = kCardChipLive;
        break;
    //接收端点了「拒绝」
    case kFolderRejected:
        chipText = msg.fromMe ? TR(L"对方已拒绝接收") : TR(L"已拒绝接收");
        break;
    //发送端在对方应答前撤回了邀请
    case kFolderInviteCanceled:
        chipText = msg.fromMe ? TR(L"已撤回邀请") : TR(L"对方已撤回邀请");
        break;
    //对方不在线
    case kFolderPeerOffline:
        chipText = TR(L"对方不在线，传输未开始");
        break;
    //邀请超时
    case kFolderTimeout:
        chipText = TR(L"邀请已超时");
        break;
    //传到一半失败
    case kFolderFailed:
        chipText = TR(L"传输失败");
        tone = kCardChipBad;
        break;
    //失败且已超出保留期：续传位点作废
    case kFolderExpired:
        chipText = TR(L"传输失败，已超过可续传的时限");
        tone = kCardChipBad;
        break;
    //任一端在传输过程中点了「取消」
    case kFolderUserCanceled:
        chipText = TR(L"已取消");
        break;
    //未知状态：宿主漏填，或今后新增状态而本表没跟上
    default:
        chipText = TR(L"文件夹传输");
        break;
    }
    if (FolderHasErrorText(msg.folderState, msg.folderErrCode))
        metaText = FolderErrorText(msg.folderErrCode);
}

void FolderCardProgressText(const ChatMessage& msg, CString& left, CString& right)
{
    left.Empty();
    right.Empty();
    int pct = msg.folderProgress;
    if (pct < 0)
        pct = 0;
    if (pct > kFolderFullPercent)
        pct = kFolderFullPercent;
    if (msg.folderState == kFolderTransferring)
    {
        if (!msg.folderCurFile.IsEmpty())
            left.Format(TR(L"正在传输 %s"), (LPCTSTR)msg.folderCurFile);
        else
            left = TR(L"正在传输…");
        if (msg.folderCurIndex > 0)
            right.Format(L"%d / %d \u00B7 ", msg.folderCurIndex, msg.folderFileCount);
    }
    else if (msg.folderTotalBytes > 0)
    {
        left = FormatFileSize(msg.folderSentBytes).c_str();
        left += L" / ";
        left += FormatFileSize(msg.folderTotalBytes).c_str();
    }
    CString pctText;
    pctText.Format(L"%d%%", pct);
    right += pctText;
}

} // namespace balloonchatwnd
