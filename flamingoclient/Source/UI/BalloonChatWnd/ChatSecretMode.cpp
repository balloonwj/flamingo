/**
 *  ChatSecretMode.cpp
 *
 *  单聊窗密聊状态的纯逻辑，说明见 ChatSecretMode.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-07
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatGroupInviteCard.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "ChatSecretMode.h"
#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签

#include <stdio.h>

#include "../../net/Protocol.h"          // error_code_secret_* / secret_state_* / secret_end_*
#include "../../Common/I18n/I18n.h"      // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//单聊窗控件 id，与 ChatWindowLayout.h 的 CtrlId 一致（这里不包含那个头文件，它要求先引入 WTL）
const unsigned int kCtrlInputEdit     = 401;   // kIdInputEdit
const unsigned int kCtrlSendButton    = 402;   // kIdSendButton
const unsigned int kCtrlTbEmoji       = 420;   // kIdTbEmoji
const unsigned int kCtrlTbImage       = 421;   // kIdTbImage

//密聊中隐藏的头部按钮与工具栏按钮（表情、图片除外）
const unsigned int kHiddenWhenSecret[] = {
    410,   // kIdHdrVoice 语音通话
    411,   // kIdHdrVideo 视频通话
    412,   // kIdHdrRemoteDesktop 远程桌面
    417,   // kIdHdrScreenShare 共享屏幕
    431,   // kIdHdrShare 文件共享
    441,   // kIdHdrSubtitle 字幕模式
    414,   // kIdHdrHistory 消息记录
    413,   // kIdHdrMore 更多
    466,   // kIdHdrSecret 密聊
    422,   // kIdTbFile 文件
    428,   // kIdTbFolder 文件夹
    423,   // kIdTbScreenshot 截图
    442,   // kIdTbScreenshotMenu 截图选项
    429,   // kIdTbRecord 屏幕录制
    424,   // kIdTbVoice 语音消息
    427,   // kIdTbFont 字体
    443,   // kIdTbMarkdown Markdown 编辑器
    448,   // kIdTbCode 发送代码
    426,   // kIdTbShake 窗口抖动
    425,   // kIdTbPlus 更多
};

//受管名单：上面那些，再加上只能查看时也要隐藏的表情与图片
const unsigned int kManagedCtrlIds[] = {
    410, 411, 412, 417, 431, 441, 414, 413, 466,
    420, 421, 422, 428, 423, 442, 429, 424, 427, 443, 448, 426, 425,
};

//伪路径、clientMsgId 里数字的格式化缓冲长度（足够容纳两个 64 位十进制数与分隔符）
const size_t kIdTextChars = 96;

bool InList(const unsigned int* list, size_t count, unsigned int id)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (list[i] == id)
        {
            return true;
        }
    }
    return false;
}

//卡片在 text 字段里的摘要：引用、复制等按 text 取摘要的地方拿到它（密聊卡片没有菜单，留作兜底显示）
const wchar_t* const kSecretInviteDigest = TR_NOOP(L"[密聊邀请]");

} // namespace

bool IsSecretState(int state)
{
    return state == kSecretStateActive || state == kSecretStateReadOnly;
}

bool IsSecretCtrlAvailable(int state, unsigned int ctrlId)
{
    if (!IsSecretState(state))
    {
        return true;
    }
    const size_t hiddenCount = sizeof(kHiddenWhenSecret) / sizeof(kHiddenWhenSecret[0]);
    if (InList(kHiddenWhenSecret, hiddenCount, ctrlId))
    {
        return false;
    }
    //表情、图片、输入框、发送按钮：密聊中可用，只能查看时不可用
    if (ctrlId == kCtrlTbEmoji || ctrlId == kCtrlTbImage || ctrlId == kCtrlInputEdit || ctrlId == kCtrlSendButton)
    {
        return state == kSecretStateActive;
    }
    return true;
}

const unsigned int* SecretManagedCtrlIds(size_t& count)
{
    count = sizeof(kManagedCtrlIds) / sizeof(kManagedCtrlIds[0]);
    return kManagedCtrlIds;
}

std::wstring SecretBannerText(int state, int endReason)
{
    if (state == kSecretStateActive)
    {
        return TR(L"密聊中 · 消息不会被记录，退出后全部清除");
    }
    if (state != kSecretStateReadOnly)
    {
        return std::wstring();
    }
    switch (endReason)
    {
    //对方点了「退出密聊」或关闭了窗口
    case secret_end_peer_left:
        return TR(L"对方已退出密聊 · 你退出后内容将全部清除");

    //对方掉线、退出登录
    case secret_end_peer_offline:
        return TR(L"对方已离线，密聊已结束 · 你退出后内容将全部清除");

    //管理员在控制台关闭了本企业或对方企业的密聊
    case secret_end_disabled:
        return TR(L"管理员已关闭密聊 · 你退出后内容将全部清除");

    //服务器维护
    case secret_end_maintenance:
        return TR(L"服务器维护，密聊已结束 · 你退出后内容将全部清除");

    //本机网络断开：会话随连接一起在服务端结束
    case kSecretEndLocalOffline:
        return TR(L"网络已断开，密聊已结束 · 你退出后内容将全部清除");

    //协议错误、本机解密失败等异常
    default:
        return TR(L"密聊出现异常，已结束 · 你退出后内容将全部清除");
    }
}

std::wstring SecretProtectWarning(int myLevel, int peerLevel)
{
    if (myLevel > 0 && peerLevel > 0)
    {
        return std::wstring();
    }
    return TR(L"某一方的系统无法防止截图，聊天内容可能被截图");
}

bool ShowSecretReinvite(int state, bool entryAvailable)
{
    return state == kSecretStateReadOnly && entryAvailable;
}

bool ShowSecretSafetyCode(int state)
{
    return state == kSecretStateActive;
}

std::wstring SecretInviteFailureText(int code)
{
    switch (code)
    {
    //本企业或对方企业关闭了密聊
    case error_code_secret_disabled:
        return TR(L"本企业或对方所在企业没有开启密聊");

    //对方不在线，或用的客户端不支持密聊
    case error_code_secret_peer_unavailable:
        return TR(L"对方当前不在线，或使用的客户端不支持密聊");

    //与对方已有待应答或进行中的密聊（多半是对方同时也邀请了你）
    case error_code_secret_busy:
        return TR(L"你们之间已有一个密聊邀请，请先处理");

    //发起过于频繁或同时进行的密聊太多
    case error_code_secret_too_many:
        return TR(L"发起密聊过于频繁，请稍后再试");

    //已被对方加入黑名单
    case error_code_secret_blocked:
        return TR(L"无法向对方发起密聊");

    //协议版本不受支持
    case error_code_secret_version_unsupported:
        return TR(L"当前客户端版本不支持密聊，请升级");

    //本机没能发出邀请（未登录、模块未就绪等）
    case -1:
        return TR(L"暂时无法发起密聊，请稍后再试");

    //其余错误码
    default:
        return TRF(L"发起密聊失败（错误码 {0}）", code);
    }
}

std::wstring SecretSendFailureText(int code)
{
    switch (code)
    {
    //单个会话转发过快（服务端每秒 64 帧）
    case error_code_secret_too_many:
        return TR(L"发送过快，请稍后再发");

    //单帧超过服务端允许的长度
    case error_code_secret_too_large:
        return TR(L"内容过长，没有发出");

    //会话已结束（对方退出、掉线）
    case error_code_secret_not_exist:
        return TR(L"密聊已结束，没有发出");

    //本机没能发出（会话不在进行中、正文非法）及其余错误码
    default:
        return TR(L"发送失败");
    }
}

std::wstring SecretInviteEndTip(int state, bool iAmInviter)
{
    switch (state)
    {
    //被忽略或超时：对发起方不区分这两种情况（docs/密聊方案.md 4.2）
    case secret_state_not_accepted:
        return iAmInviter ? TR(L"对方暂未接受密聊邀请") : TR(L"密聊邀请已过期");

    //发起方撤回了邀请
    case secret_state_canceled:
        return iAmInviter ? std::wstring() : TR(L"对方已撤回密聊邀请");

    //邀请阶段就被服务端结束（如管理员关闭了密聊）
    case secret_state_ended:
        return TR(L"密聊邀请已失效");

    //已在本人的其他设备上处理：卡片直接移除，不留提示
    case secret_state_handled_elsewhere:
        return std::wstring();

    //本机出错（密钥协商失败等）
    default:
        return TR(L"密聊建立失败，请重新发起");
    }
}

std::wstring SecretExitTip()
{
    return TR(L"你已退出密聊，聊天内容已清除");
}

std::wstring SecretCloseConfirmText()
{
    return TR(L"关闭窗口将退出密聊，聊天内容会被清除。确定要关闭吗？");
}

std::wstring SecretWindowTitle(const std::wstring& baseTitle)
{
    return TRF(L"{0}（密聊）", baseTitle);
}

std::wstring SecretOnlyTextAndImageTip()
{
    return TR(L"密聊只能发送文字、表情与图片");
}

std::wstring SecretImageTooLargeTip()
{
    return TR(L"图片超过 10 MB，不能在密聊中发送");
}

bool IsSecretImageSizeAcceptable(uint64_t bytes)
{
    return bytes > 0 && bytes <= kSecretMaxImageBytes;
}

std::wstring SecretImageMemoryPath(uint64_t peerId, uint64_t sessionId, bool mine, uint32_t imageId)
{
    wchar_t tail[kIdTextChars] = { 0 };
    ::swprintf_s(tail, L"%llu/%llu/%c%u", static_cast<unsigned long long>(peerId),
                 static_cast<unsigned long long>(sessionId), mine ? L'm' : L'p', imageId);
    return std::wstring(L"secretmem://") + tail;
}

std::wstring SecretImageMemoryPrefix(uint64_t peerId)
{
    wchar_t tail[kIdTextChars] = { 0 };
    ::swprintf_s(tail, L"%llu/", static_cast<unsigned long long>(peerId));
    return std::wstring(L"secretmem://") + tail;
}

std::string SecretClientMsgId(uint64_t sessionId, bool mine, uint32_t id)
{
    char buf[kIdTextChars] = { 0 };
    ::sprintf_s(buf, "secret-%llu-%c-%u", static_cast<unsigned long long>(sessionId), mine ? 'm' : 'p', id);
    return std::string(buf);
}

std::string SecretImageClientMsgId(uint64_t sessionId, bool mine, uint32_t imageId)
{
    char buf[kIdTextChars] = { 0 };
    ::sprintf_s(buf, "secret-%llu-%ci-%u", static_cast<unsigned long long>(sessionId), mine ? 'm' : 'p', imageId);
    return std::string(buf);
}

std::wstring SecretBlockedSendTip()
{
    return TR(L"密聊中不能发送这类消息，请先退出密聊");
}

CString SecretImageReceivingText(int percent)
{
    return CString(TRF(L"接收中 {0}%", percent).c_str());
}

void ApplySecretInviteCard(ChatMessage& msg, bool iAmInviter, const CString& peerName, uint64_t sessionId,
                           uint32_t remainSeconds, int peerProtect)
{
    msg.kind = kMsgSecretInvite;
    msg.fromMe = iAmInviter;
    msg.msgid = 0;
    msg.secretSessionId = sessionId;
    msg.text = TR(kSecretInviteDigest);
    if (iAmInviter)
    {
        msg.secretInviteTitle = TR(L"已邀请对方进入密聊");
        msg.secretInviteSubtitle = TR(L"等待对方同意");
        msg.secretInviteButtons = 1;
    }
    else
    {
        msg.secretInviteTitle = TRF(L"{0} 邀请你进入密聊", std::wstring((LPCWSTR)peerName)).c_str();
        msg.secretInviteSubtitle = TR(L"密聊消息不会被记录，退出后全部清除");
        msg.secretInviteButtons = 2;
    }
    //对方做不到防截图时如实写明（docs/密聊方案.md 4.6 第 1 条）
    if (peerProtect <= 0)
    {
        msg.secretInviteSubtitle = TR(L"对方的系统无法防止截图");
    }
    UpdateSecretInviteRemain(msg, remainSeconds);
}

void UpdateSecretInviteRemain(ChatMessage& msg, uint32_t remainSeconds)
{
    msg.secretInviteStatus = TRF(L"剩余 {0} 秒", remainSeconds).c_str();
    //倒计时用橙色标签（docs/聊天卡片美化方案.md 5.4）
    SetCardChip(msg, msg.secretInviteStatus, kCardChipPending);
}

CString SecretInviteButtonText(int buttons, int index)
{
    const int action = SecretInviteButtonAction(buttons, index);
    switch (action)
    {
    //被邀请方的第一个按钮
    case kSecretInviteAccept:
        return CString(TR(L"同意"));

    //被邀请方的第二个按钮
    case kSecretInviteIgnore:
        return CString(TR(L"忽略"));

    //发起方唯一的按钮
    case kSecretInviteCancel:
        return CString(TR(L"取消"));

    //越界
    default:
        return CString();
    }
}

int SecretInviteButtonAction(int buttons, int index)
{
    if (buttons == 1 && index == 0)
    {
        return kSecretInviteCancel;
    }
    if (buttons == 2 && index == 0)
    {
        return kSecretInviteAccept;
    }
    if (buttons == 2 && index == 1)
    {
        return kSecretInviteIgnore;
    }
    return 0;
}

} // namespace balloonchatwnd
