/**
 *  ChatSecretMode.h
 *
 *  单聊窗密聊状态的纯逻辑（docs/密聊方案.md 4.6~4.8）：三种窗口状态下各个按钮是否可用、头部条与提示的文字、
 *  邀请卡片的内容、密聊图片的内存伪路径与大小限制。不碰窗口与绘制，可被 FlamingoUnit 直接测试（标签 [secretchat]）。
 *
 *  典型用法（单聊窗切换状态后刷新按钮）：
 *      for (each managed ctrlId)
 *          ctrl->SetVisible(balloonchatwnd::IsSecretCtrlAvailable(state, ctrlId) && 其它条件);
 *  插入邀请卡片：
 *      balloonchatwnd::ChatMessage card;
 *      balloonchatwnd::ApplySecretInviteCard(card, false, peerName, sessionId, remainSeconds, peerProtect);
 *      list->AppendMessage(card);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgSecretInvite

namespace balloonchatwnd {

/**
 *  单聊窗的三种状态（docs/密聊方案.md 4.7）。
 */
enum ChatSecretState
{
    kSecretStateNormal   = 0,   // 普通聊天：没有密聊，或本人已退出密聊
    kSecretStateActive   = 1,   // 密聊中：可以收发密聊消息
    kSecretStateReadOnly = 2,   // 只能查看：会话在服务端结束、本人还没退出，内容保留，不能发送
};

/**
 *  邀请卡片上的按钮动作（ChatSecretInviteRequest::m_action）。
 */
enum ChatSecretInviteAction
{
    kSecretInviteAccept = 1,   // 被邀请方点了「同意」
    kSecretInviteIgnore = 2,   // 被邀请方点了「忽略」
    kSecretInviteCancel = 3,   // 发起方点了「取消」
};

//本机才有的结束原因，接在服务端 secret_end_reason（1~5）之后，只在「只能查看」状态的头部条文字里用到
const int kSecretEndLocalError   = 100;   // 本机出错：密钥协商失败、收到无法解密的帧（DLL 报 session_state_local_error）
const int kSecretEndLocalOffline = 101;   // 本机网络断开或退出登录：连接一断会话即在服务端结束

//单张密聊图片的字节上限，与 FlamingoSecretChat/SecretChatApi.h 的 kMaxImageBytes 一致（原图发送，用户 2026-10-06 确认）
const uint64_t kSecretMaxImageBytes = 10 * 1024 * 1024;

/**
 *  是否属于密聊状态（密聊中或只能查看）：防截图、按钮限制、菜单限制在这两种状态下都生效。
 */
bool IsSecretState(int state);

/**
 *  某个控件在给定状态下是否可用（不可用时隐藏或置灰）。只管密聊带来的限制，其它显隐条件（模块是否加载等）由调用方另判。
 *    - 普通状态：全部可用；
 *    - 密聊中：头部的语音、视频、远程桌面、共享屏幕、文件共享、字幕、消息记录、更多、密聊全部隐藏，工具栏只保留表情与图片，
 *      输入框与发送按钮可用；
 *    - 只能查看：同上，另外表情、图片、输入框、发送按钮都不可用。
 *  @param state  取值见 ChatSecretState
 *  @param ctrlId 单聊窗控件 id（ChatWindowLayout.h 的 CtrlId）；不在受管名单里的一律返回 true
 *  @return 可用返回 true
 */
bool IsSecretCtrlAvailable(int state, unsigned int ctrlId);

/**
 *  受密聊状态管理显隐的头部与工具栏控件 id 名单（不含输入框与发送按钮，它们按可用与否置灰而不是隐藏）。
 *  @param count [出参] 名单长度
 *  @return 名单首地址（静态存储）
 */
const unsigned int* SecretManagedCtrlIds(size_t& count);

/**
 *  密聊头部条左侧的状态文字（已按界面语言翻译）。
 *  @param state     取值见 ChatSecretState；普通状态返回空串
 *  @param endReason 只能查看时的结束原因：secret_end_reason（1~5）或 kSecretEndLocalError / kSecretEndLocalOffline
 *  @return 文字
 */
std::wstring SecretBannerText(int state, int endReason);

/**
 *  防截图提示（头部条第二行）：本机或对方任一方做不到防截图（级别为 0）时返回提示，否则返回空串。
 *  @param myLevel   本机对本窗口实际达到的级别（secretchat::CaptureProtectLevel）
 *  @param peerLevel 对方报到的级别
 */
std::wstring SecretProtectWarning(int myLevel, int peerLevel);

/**
 *  头部条上是否显示「重新邀请」：只能查看、且入口可用（模块就绪、企业开关打开）时显示。
 */
bool ShowSecretReinvite(int state, bool entryAvailable);

/**
 *  头部条上是否显示「安全码」：密聊中才显示（只能查看时会话密钥已清零，算不出安全码）。
 */
bool ShowSecretSafetyCode(int state);

/**
 *  发起邀请失败时的提示（已按界面语言翻译）。
 *  @param code 2602 应答的错误码（error_code_secret_*），或本机拒绝发出时的 -1
 */
std::wstring SecretInviteFailureText(int code);

/**
 *  一条密聊消息没有发出去时写在气泡下方的原因（已按界面语言翻译）。
 *  @param code OnSendResult 的错误码（error_code_secret_*），或本机拒绝发出时的 -1
 */
std::wstring SecretSendFailureText(int code);

/**
 *  邀请没有建立就结束时插入的本地提示（已按界面语言翻译）。
 *  @param state      OnSessionState 的 state（2 未接受、3 已撤回、4 已结束、100 本机出错）
 *  @param iAmInviter 本人是不是发起方
 *  @return 提示文字；不需要提示时（如 5 已在其他设备处理）返回空串
 */
std::wstring SecretInviteEndTip(int state, bool iAmInviter);

/**
 *  本人退出密聊后插入列表的本地提示：「你已退出密聊，聊天内容已清除」。
 */
std::wstring SecretExitTip();

/**
 *  关闭处于密聊状态的窗口时的二次确认文字。
 */
std::wstring SecretCloseConfirmText();

/**
 *  密聊状态下的窗口标题：原标题后加「（密聊）」。
 *  @param baseTitle 原标题（对方名字）
 */
std::wstring SecretWindowTitle(const std::wstring& baseTitle);

/**
 *  密聊状态下拒收文件、文件夹等内容时输入框上方的提示：「密聊只能发送文字、表情与图片」。
 */
std::wstring SecretOnlyTextAndImageTip();

/**
 *  图片超过 10 MB 被拒绝发送时的提示。
 */
std::wstring SecretImageTooLargeTip();

/**
 *  一张图片的字节数是否允许以密聊发送（大于 0 且不超过 kSecretMaxImageBytes）。
 */
bool IsSecretImageSizeAcceptable(uint64_t bytes);

/**
 *  密聊图片在 ChatImageCache 里的内存伪路径：secretmem://<对方>/<会话>/<m|p><编号>，m 为本人发出、p 为对方发来。
 *  同一窗口的全部密聊图片共享 SecretImageMemoryPrefix(peerId) 这个前缀，退出密聊时按前缀一次删除。
 */
std::wstring SecretImageMemoryPath(uint64_t peerId, uint64_t sessionId, bool mine, uint32_t imageId);

/**
 *  某个单聊窗全部密聊图片的伪路径前缀：secretmem://<对方>/
 */
std::wstring SecretImageMemoryPrefix(uint64_t peerId);

/**
 *  密聊文字在消息列表里的本地 clientMsgId（用来与发送结果对回）：secret-<会话>-<m|p>-<编号>。
 */
std::string SecretClientMsgId(uint64_t sessionId, bool mine, uint32_t id);

/**
 *  密聊图片在消息列表里的本地 clientMsgId（用来与收发进度、结果对回）：secret-<会话>-<m|p>i-<编号>。
 *  图片编号与文字编号各自从 1 起，加上 i 与文字的 clientMsgId 区分开。
 */
std::string SecretImageClientMsgId(uint64_t sessionId, bool mine, uint32_t imageId);

/**
 *  密聊中拒绝从其它入口发出普通消息时的提示（快捷回复以外的录屏发送、共享邀请链接、Markdown 文档等）：
 *  「密聊中不能发送这类消息，请先退出密聊」。
 */
std::wstring SecretBlockedSendTip();

/**
 *  图片接收进度的覆盖文字：「接收中 NN%」。
 *  @param percent 0~100
 */
CString SecretImageReceivingText(int percent);

/**
 *  把一张密聊邀请卡片套用到气泡数据上：kind 改为 kMsgSecretInvite、本地气泡（msgid 为 0），填会话号、标题、说明、
 *  剩余秒数与按钮。发起方的卡片（fromMe 为 true）画在右侧、只有「取消」；被邀请方的卡片画在左侧，有「同意」「忽略」。
 *  @param msg           [入出参] 气泡数据；头像等字段由调用方另填
 *  @param iAmInviter    本人是不是发起方
 *  @param peerName      对方显示名（被邀请方的标题里用）
 *  @param sessionId     会话号
 *  @param remainSeconds 剩余秒数
 *  @param peerProtect   对方的防截图级别：为 0 时说明行写明「对方的系统无法防止截图」
 */
void ApplySecretInviteCard(ChatMessage& msg, bool iAmInviter, const CString& peerName, uint64_t sessionId,
                           uint32_t remainSeconds, int peerProtect);

/**
 *  只刷新卡片上的剩余秒数（倒计时定时器每秒调用）。
 *  @param msg           [入出参] 已套用过 ApplySecretInviteCard 的气泡数据
 *  @param remainSeconds 剩余秒数
 */
void UpdateSecretInviteRemain(ChatMessage& msg, uint32_t remainSeconds);

/**
 *  邀请卡片按钮的文字（已按界面语言翻译）。
 *  @param buttons 卡片的按钮数（1 或 2）
 *  @param index   第几个按钮（从 0 起）
 *  @return 「取消」「同意」「忽略」；越界返回空串
 */
CString SecretInviteButtonText(int buttons, int index);

/**
 *  邀请卡片第 index 个按钮对应的动作（ChatSecretInviteAction）；越界返回 0。
 */
int SecretInviteButtonAction(int buttons, int index);

} // namespace balloonchatwnd
